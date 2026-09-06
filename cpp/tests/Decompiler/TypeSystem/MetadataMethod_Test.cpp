// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The MetadataMethod test suite (gold-pinned against the real ICSharpCode.
// Decompiler 11.0 via the C:/temp-probe/MmProbe gold probe):
//   * the curated method dumps over mscorlib + System.dll (String.Substring /
//     the .ctor forms / .cctor, the getter accessors, the op_ operators, the
//     params/optional/out shapes, List`1's generic method, Object.Finalize
//     the Destructor arm, the abstract Stream.Read, the pinvoke
//     LocalAlloc_NoSafeHandle with the nint/nuint renders, the extension
//     methods, Enum.HasFlag, the vararg trio with the __arglist sentinel);
//   * the whole-corpus FNV-1a-64 digests over EVERY MethodDef row of mscorlib
//     (29257 methods, CE7B80A943BA15AF) and System.dll (18170,
//     3E1EBB7024DBD4BC) -- every name, kind, accessibility, flag matrix,
//     type parameter, decoded return type and parameter (with the reference
//     kinds and constant values) of both corpora byte-exact;
//   * the .NET 10 CoreLib curated drives (the IsInitOnly setter, the
//     [return: IsReadOnly] method, the readonly-struct ThisIsRefReadOnly
//     census, the Span`1 operator/implicit/GenericEnumerator shapes);
//   * the entity-cache identities (GetDefinitionMethod twice, the Uncached
//     variant, Equals' handle + module-file identity, ToString, the
//     declaring-type identity, MemberDefinition/Substitution, the
//     type-parameter snapshot, the accessor-owner deferral);
//   * the IsVisible(MethodAttributes) visibility filter matrix (the
//     OnlyPublicAPI variant);
//   * the deferral contracts (AccessorOwner / the explicit-interface members
//     / the attribute members / Specialize).

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/DecimalConstantHelper.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataParameter.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;
namespace TI = ILSpy::Decompiler::TypeSystem::Implementation;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

const char* CoreLibPath() {
#if defined(_WIN32)
    return "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\"
           "System.Private.CoreLib.dll";
#else
    return "";
#endif
}

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }
bool CoreLibAvailable() { return FileExists(CoreLibPath()); }

// A module reference resolving to an externally-owned module (the C#
// `PEFile : IModuleReference` shape the gold probe drives -- the
// MetadataField_Test fixture precedent).
class FixedModuleRef : public TS::IModuleReference {
public:
    explicit FixedModuleRef(const TS::IModule* module = nullptr)
        : module_(module) {}

    const TS::IModule* Resolve(
        const TS::ITypeResolveContext&) const override {
        return module_;
    }

private:
    const TS::IModule* module_;
};

// The port's SimpleCompilation with the protected Init exposed (the
// TestCompilation pattern).
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// ---------------------------------------------------------------------------
// The render helpers -- each mirrors the gold probe's Program.cs shapes
// byte-for-byte (the Quote / RenderConst / MethodLine / FNV quartet).
// ---------------------------------------------------------------------------

std::string Quote(const std::string& utf8) {
    std::u16string utf16 = ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    std::string out = "\"";
    char buffer[16];
    for (char16_t ch : utf16) {
        if (ch == u'\\') {
            out += "\\\\";
        } else if (ch == u'"') {
            out += "\\\"";
        } else if (ch == u'\n') {
            out += "\\n";
        } else if (ch == u'\r') {
            out += "\\r";
        } else if (ch == u'\t') {
            out += "\\t";
        } else if (ch < 32 || ch > 126) {
            std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                static_cast<unsigned>(ch));
            out += buffer;
        } else {
            out += static_cast<char>(ch);
        }
    }
    return out + "\"";
}

// The C# `decimal.ToString(CultureInfo.InvariantCulture)` over the 96-bit
// magnitude (the MetadataField_Test DecimalToString recipe).
std::string DecimalToString(const TI::DecimalConstant& d) {
    std::uint32_t words[3] = {d.lo, d.mid, d.hi};
    std::string digits;
    do {
        std::uint64_t remainder = 0;
        for (int i = 2; i >= 0; i--) {
            std::uint64_t cur = (remainder << 32) | words[i];
            words[i] = static_cast<std::uint32_t>(cur / 1000000000u);
            remainder = cur % 1000000000u;
        }
        bool more = (words[0] | words[1] | words[2]) != 0;
        char group[16];
        if (more) {
            std::snprintf(group, sizeof(group), "%09u",
                static_cast<unsigned>(remainder));
        } else {
            std::snprintf(group, sizeof(group), "%u",
                static_cast<unsigned>(remainder));
        }
        digits = group + digits;
    } while ((words[0] | words[1] | words[2]) != 0);
    std::uint32_t scale = d.scale;
    std::string s;
    if (scale == 0) {
        s = digits;
    } else if (digits.size() <= scale) {
        s = "0.";
        s.append(scale - digits.size(), '0');
        s += digits;
    } else {
        s = digits.substr(0, digits.size() - scale);
        s.push_back('.');
        s.append(digits, digits.size() - scale, std::string::npos);
    }
    if (d.isNegative)
        s.insert(s.begin(), '-');
    return s;
}

// The probe's RenderConst (the METHOD form): "<null>" for null, the bare
// bool/int/decimal spellings, the R-format floats, the quoted strings, the
// single-quoted chars.
std::string RenderConst(const std::any& val) {
    if (!val.has_value())
        return "<null>";
    if (auto* b = std::any_cast<bool>(&val)) {
        return *b ? "true" : "false";
    }
    if (auto* c = std::any_cast<char16_t>(&val)) {
        return "'" + ILSpy::Decompiler::Util::Utf16ToUtf8(
            std::u16string(1, *c)) + "'";
    }
    if (auto* v = std::any_cast<std::int8_t>(&val)) {
        return std::to_string(static_cast<int>(*v));
    }
    if (auto* v = std::any_cast<std::uint8_t>(&val)) {
        return std::to_string(static_cast<unsigned>(*v));
    }
    if (auto* v = std::any_cast<std::int16_t>(&val)) {
        return std::to_string(static_cast<int>(*v));
    }
    if (auto* v = std::any_cast<std::uint16_t>(&val)) {
        return std::to_string(static_cast<unsigned>(*v));
    }
    if (auto* v = std::any_cast<std::int32_t>(&val)) {
        return std::to_string(*v);
    }
    if (auto* v = std::any_cast<std::uint32_t>(&val)) {
        return std::to_string(*v);
    }
    if (auto* v = std::any_cast<std::int64_t>(&val)) {
        return std::to_string(*v);
    }
    if (auto* v = std::any_cast<std::uint64_t>(&val)) {
        return std::to_string(*v);
    }
    if (auto* v = std::any_cast<float>(&val)) {
        return ILSpy::Decompiler::Disassembler::FormatRoundTrip(*v);
    }
    if (auto* v = std::any_cast<double>(&val)) {
        return ILSpy::Decompiler::Disassembler::FormatRoundTrip(*v);
    }
    if (auto* v = std::any_cast<std::string>(&val)) {
        return Quote(*v);
    }
    if (auto* d = std::any_cast<TI::DecimalConstant>(&val)) {
        return DecimalToString(*d);
    }
    return "other:";
}

// The C# `Accessibility.ToString()` spelling.
const char* AccessibilitySpelling(TS::Accessibility a) {
    switch (a) {
        case TS::Accessibility::None: return "None";
        case TS::Accessibility::Private: return "Private";
        case TS::Accessibility::ProtectedAndInternal:
            return "ProtectedAndInternal";
        case TS::Accessibility::Internal: return "Internal";
        case TS::Accessibility::Protected: return "Protected";
        case TS::Accessibility::ProtectedOrInternal:
            return "ProtectedOrInternal";
        case TS::Accessibility::Public: return "Public";
    }
    return "<unknown>";
}

// The C# `SymbolKind.ToString()` spellings the method corpus reaches.
const char* SymbolKindSpelling(TS::SymbolKind k) {
    switch (k) {
        case TS::SymbolKind::Method: return "Method";
        case TS::SymbolKind::Accessor: return "Accessor";
        case TS::SymbolKind::Constructor: return "Constructor";
        case TS::SymbolKind::Destructor: return "Destructor";
        case TS::SymbolKind::Operator: return "Operator";
        default: return "<other>";
    }
}

// The C# `ReferenceKind.ToString()` spelling.
const char* ReferenceKindSpelling(TS::ReferenceKind r) {
    switch (r) {
        case TS::ReferenceKind::None: return "None";
        case TS::ReferenceKind::Out: return "Out";
        case TS::ReferenceKind::Ref: return "Ref";
        case TS::ReferenceKind::In: return "In";
        case TS::ReferenceKind::RefReadOnly: return "RefReadOnly";
    }
    return "<unknown>";
}

// The C# `MethodSemanticsAttributes.ToString()` spellings the corpus reaches.
const char* AccessorKindSpelling(TS::MethodSemanticsAttributes k) {
    switch (k) {
        // The BCL `System.Reflection.MethodSemanticsAttributes` enum carries
        // no None member -- the .NET `ToString()` for the value 0 renders the
        // decimal "0" (the port's D384 `None = 0` member is the port-side
        // spelling; the gold probe renders the BCL form).
        case TS::MethodSemanticsAttributes::None: return "0";
        case TS::MethodSemanticsAttributes::Setter: return "Setter";
        case TS::MethodSemanticsAttributes::Getter: return "Getter";
        case TS::MethodSemanticsAttributes::Other: return "Other";
        case TS::MethodSemanticsAttributes::Adder: return "Adder";
        case TS::MethodSemanticsAttributes::Remover: return "Remover";
        case TS::MethodSemanticsAttributes::Raiser: return "Raiser";
    }
    return "<unknown>";
}

std::string TokenString(std::uint32_t token) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", token);
    return buffer;
}

// The probe's ParamLine: `name~refkind~opt~isParams~hasConstVis~type~const`.
std::string ParamLine(const TS::IParameter* p) {
    return p->Name() + "~" + ReferenceKindSpelling(p->ReferenceKind()) + "~"
        + (p->IsOptional() ? "True" : "False") + "~"
        + (p->IsParams() ? "True" : "False") + "~"
        + (p->HasConstantValueInSignature() ? "True" : "False") + "~"
        + p->Type().ReflectionName() + "~"
        + RenderConst(p->GetConstantValue(false));
}

// The probe's MethodLine (the `sweep` compact form and the full form).
std::string MethodLine(const TS::IMethod* m, bool sweep) {
    std::string token = TokenString(m->MetadataToken());
    std::string name = Quote(m->Name());
    std::string access = AccessibilitySpelling(m->Accessibility());
    std::string flags = std::string(m->IsStatic() ? "s" : "-")
        + (m->IsAbstract() ? "a" : "-") + (m->IsSealed() ? "e" : "-")
        + (m->IsVirtual() ? "v" : "-") + (m->IsOverride() ? "o" : "-")
        + (m->IsOverridable() ? "O" : "-");
    std::string kind = SymbolKindSpelling(m->SymbolKind());
    std::string tp;
    {
        std::vector<const TS::ITypeParameter*> tps = m->TypeParameters();
        bool first = true;
        for (const auto* t : tps) {
            if (!first)
                tp += "|";
            tp += t->Name();
            first = false;
        }
    }
    std::string ret = Quote(m->ReturnType().ReflectionName());
    std::string ps;
    {
        std::vector<const TS::IParameter*> params = m->Parameters();
        bool first = true;
        for (const auto* p : params) {
            if (!first)
                ps += "|";
            ps += ParamLine(p);
            first = false;
        }
    }
    std::string meta = std::string("I=")
        + (m->IsInitOnly() ? "True" : "False") + " R="
        + (m->ReturnTypeIsRefReadOnly() ? "True" : "False") + " T="
        + (m->ThisIsRefReadOnly() ? "True" : "False");
    std::string x = std::string("X=")
        + (m->IsExtensionMethod() ? "True" : "False") + " B="
        + (m->HasBody() ? "True" : "False") + " A="
        + (m->IsAccessor() ? "True" : "False")
        + " K=" + AccessorKindSpelling(m->AccessorKind()) + " C="
        + (m->IsConstructor() ? "True" : "False") + " D="
        + (m->IsDestructor() ? "True" : "False") + " P="
        + (m->IsOperator() ? "True" : "False") + " L="
        + (m->IsLocalFunction() ? "True" : "False");
    if (sweep)
        return token + "|" + name + "|" + kind + "|" + access + "|" + flags
            + "|" + tp + "|" + ret + "|" + ps + "|" + meta + "|" + x;
    const TS::ITypeDefinition* decl = m->DeclaringTypeDefinition();
    std::string declName =
        decl != nullptr ? Quote(decl->FullName()) : Quote("");
    std::string fn = Quote(m->FullName());
    std::string rn = Quote(m->ReflectionName());
    std::string ns = Quote(m->Namespace());
    return "M " + token + " " + name + " kind=" + kind + " access=" + access
        + " " + flags + " tpc=" + std::to_string(m->TypeParameters().size())
        + " decl=" + declName + " ret=" + ret + " pc="
        + std::to_string(m->Parameters().size()) + " [" + ps + "] " + meta
        + " " + x + " fn=" + fn + " rn=" + rn + " ns=" + ns;
}

// The probe's FNV-1a-64.
class Fnv64 {
public:
    void Add(const std::string& s) {
        for (char ch : s) {
            fnv_ ^= static_cast<std::uint8_t>(ch);
            fnv_ *= 0x100000001b3ULL;
        }
        fnv_ ^= 0xff;
        fnv_ *= 0x100000001b3ULL;
    }
    std::uint64_t Digest() const { return fnv_; }

private:
    std::uint64_t fnv_ = 0xcbf29ce484222325ULL;
};

// ---------------------------------------------------------------------------
// The fixture: the probe's compilation shapes -- the A pair (mscorlib main +
// System ref), the option variants (OnlyPublicAPI / Uncached), and the
// CoreLib module (the CL curated drives) -- each with its own module
// instances.
// ---------------------------------------------------------------------------
struct MmFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile systemFile{ SystemPath() };
    TM::MetadataFile coreLibFile{ CoreLibPath() };

    // The A pair: mscorlib main + System ref (the probe's `comp`).
    TestCompilation compA;
    TS::MetadataModule mscA{ compA, &mscorlibFile,
                             TS::TypeSystemOptions::Default };
    TS::MetadataModule sysA{ compA, &systemFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef mscARef{ &mscA };
    FixedModuleRef sysARef{ &sysA };

    // The OnlyPublicAPI variant over mscorlib (the probe's `comp4`).
    TestCompilation compPub;
    TS::MetadataModule mscPub{ compPub, &mscorlibFile,
                               TS::TypeSystemOptions::Default
                                   | TS::TypeSystemOptions::OnlyPublicAPI };
    FixedModuleRef mscPubRef{ &mscPub };

    // The Uncached variant over mscorlib (the probe's `comp3`).
    TestCompilation compUnc;
    TS::MetadataModule mscUnc{ compUnc, &mscorlibFile,
                               TS::TypeSystemOptions::Default
                                   | TS::TypeSystemOptions::Uncached };
    FixedModuleRef mscUncRef{ &mscUnc };

    // The single-module sweep compilations (the probe's SWEEP section drives
    // `new SimpleCompilation(new PEFile(path))` -- the System.dll sweep
    // WITHOUT the mscorlib reference, so its cross-module TypeRefs stay
    // UNRESOLVED: `System.IntPtr` renders as the unresolved name, not the
    // nint substitution (which fires only over the RESOLVED definition
    // whose KnownTypeCode is IntPtr).
    TestCompilation compMscSingle;
    TS::MetadataModule mscSingle{ compMscSingle, &mscorlibFile,
                                  TS::TypeSystemOptions::Default };
    FixedModuleRef mscSingleRef{ &mscSingle };
    TestCompilation compSysSingle;
    TS::MetadataModule sysSingle{ compSysSingle, &systemFile,
                                  TS::TypeSystemOptions::Default };
    FixedModuleRef sysSingleRef{ &sysSingle };

    // The CoreLib module (the probe's CL section).
    TestCompilation compCore;
    TS::MetadataModule core{ compCore, &coreLibFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef coreRef{ &core };

    MmFixture() {
        compA.Initialize(mscARef, { &sysARef });
        compMscSingle.Initialize(mscSingleRef, {});
        compSysSingle.Initialize(sysSingleRef, {});
        compPub.Initialize(mscPubRef, {});
        compUnc.Initialize(mscUncRef, {});
        compCore.Initialize(coreRef, {});
    }

    const TS::ITypeDefinition* TypeA(const char* ns, const char* name,
                                     int arity = 0) {
        const TS::ITypeDefinition* d = mscA.GetTypeDefinition(
            TS::TopLevelTypeName(ns, name, arity));
        EXPECT_NE(d, nullptr)
            << "fixture type " << ns << "." << name << "`" << arity;
        return d;
    }

    // The probe's `Get`: the n-th method row of the type with the given name.
    const TS::IMethod* Get(TS::MetadataModule& module,
        const TS::ITypeDefinition* td, const char* name, int n) {
        auto rows = module.MetadataFile()->GetMethods(td->MetadataToken());
        int i = 0;
        for (const auto& row : rows) {
            if (row.Name == name) {
                if (i == n)
                    return module.GetDefinitionMethod(row.Token);
                i++;
            }
        }
        ADD_FAILURE() << "fixture method missing: " << td->Name() << "."
                      << name << "#" << n;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// The curated gold drives (the probe's section M -- every line byte-exact).
// ---------------------------------------------------------------------------
class MetadataMethodTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

TEST_F(MetadataMethodTest, CuratedMethodGold)
{
    MmFixture fx;
    const TS::ITypeDefinition* str = fx.TypeA("System", "String");
    const TS::ITypeDefinition* dec = fx.TypeA("System", "Decimal");
    const TS::ITypeDefinition* obj = fx.TypeA("System", "Object");
    const TS::ITypeDefinition* list = fx.TypeA(
        "System.Collections.Generic", "List", 1);
    const TS::ITypeDefinition* int32 = fx.TypeA("System", "Int32");
    const TS::ITypeDefinition* stream = fx.TypeA("System.IO", "Stream");
    const TS::ITypeDefinition* win32 = fx.TypeA("Microsoft.Win32", "Win32Native");
    const TS::ITypeDefinition* enumerable = fx.TypeA(
        "System.Collections", "IEnumerable");
    const TS::ITypeDefinition* enumTd = fx.TypeA("System", "Enum");

    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "Substring", 0), false),
        "M 060004DF \"Substring\" kind=Method access=Public ------ tpc=0 "
        "decl=\"System.String\" ret=\"System.String\" pc=1 "
        "[startIndex~None~False~False~False~System.Int32~<null>] I=False "
        "R=False T=False X=False B=True A=False K=0 C=False D=False "
        "P=False L=False fn=\"System.String.Substring\" "
        "rn=\"System.String.Substring\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, ".ctor", 3), false),
        "M 060004E8 \".ctor\" kind=Constructor access=Public ------ tpc=0 "
        "decl=\"System.String\" ret=\"System.Void\" pc=3 "
        "[value~None~False~False~False~System.SByte*~<null>|"
        "startIndex~None~False~False~False~System.Int32~<null>|"
        "length~None~False~False~False~System.Int32~<null>] I=False R=False "
        "T=False X=False B=False A=False K=0 C=True D=False P=False "
        "L=False fn=\"System.String..ctor\" rn=\"System.String..ctor\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, dec, ".cctor", 0), false),
        "M 06000D93 \".cctor\" kind=Constructor access=Private s----- "
        "tpc=0 decl=\"System.Decimal\" ret=\"System.Void\" pc=0 [] I=False "
        "R=False T=False X=False B=True A=False K=0 C=True D=False "
        "P=False L=False fn=\"System.Decimal..cctor\" "
        "rn=\"System.Decimal..cctor\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "get_Length", 0), false),
        "M 060004D3 \"get_Length\" kind=Accessor access=Public ------ tpc=0 "
        "decl=\"System.String\" ret=\"System.Int32\" pc=0 [] I=False R=False "
        "T=False X=False B=False A=True K=Getter C=False D=False P=False "
        "L=False fn=\"System.String.get_Length\" "
        "rn=\"System.String.get_Length\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "get_Chars", 0), false),
        "M 060004C8 \"get_Chars\" kind=Accessor access=Public ------ tpc=0 "
        "decl=\"System.String\" ret=\"System.Char\" pc=1 "
        "[index~None~False~False~False~System.Int32~<null>] I=False R=False "
        "T=False X=False B=False A=True K=Getter C=False D=False P=False "
        "L=False fn=\"System.String.get_Chars\" "
        "rn=\"System.String.get_Chars\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "op_Equality", 0), false),
        "M 060004C6 \"op_Equality\" kind=Operator access=Public s----- tpc=0 "
        "decl=\"System.String\" ret=\"System.Boolean\" pc=2 "
        "[a~None~False~False~False~System.String~<null>|"
        "b~None~False~False~False~System.String~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=True "
        "L=False fn=\"System.String.op_Equality\" "
        "rn=\"System.String.op_Equality\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "CopyTo", 0), false),
        "M 060004C9 \"CopyTo\" kind=Method access=Public ------ tpc=0 "
        "decl=\"System.String\" ret=\"System.Void\" pc=4 "
        "[sourceIndex~None~False~False~False~System.Int32~<null>|"
        "destination~None~False~False~False~System.Char[]~<null>|"
        "destinationIndex~None~False~False~False~System.Int32~<null>|"
        "count~None~False~False~False~System.Int32~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.String.CopyTo\" rn=\"System.String.CopyTo\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "Join", 0), false),
        "M 060004B4 \"Join\" kind=Method access=Public s----- tpc=0 "
        "decl=\"System.String\" ret=\"System.String\" pc=2 "
        "[separator~None~False~False~False~System.String~<null>|"
        "value~None~False~True~False~System.String[]~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.String.Join\" rn=\"System.String.Join\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, str, "Format", 1), false),
        "M 06000547 \"Format\" kind=Method access=Public s----- tpc=0 "
        "decl=\"System.String\" ret=\"System.String\" pc=3 "
        "[format~None~False~False~False~System.String~<null>|"
        "arg0~None~False~False~False~System.Object~<null>|"
        "arg1~None~False~False~False~System.Object~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.String.Format\" rn=\"System.String.Format\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, int32, "TryParse", 0), false),
        "M 06000F58 \"TryParse\" kind=Method access=Public s----- tpc=0 "
        "decl=\"System.Int32\" ret=\"System.Boolean\" pc=2 "
        "[s~None~False~False~False~System.String~<null>|"
        "result~Out~False~False~False~System.Int32&~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.Int32.TryParse\" rn=\"System.Int32.TryParse\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, list, "Add", 0), false),
        "M 06003AFC \"Add\" kind=Method access=Public ------ tpc=0 "
        "decl=\"System.Collections.Generic.List\" ret=\"System.Void\" pc=1 "
        "[item~None~False~False~False~`0~<null>] I=False R=False T=False "
        "X=False B=True A=False K=0 C=False D=False P=False L=False "
        "fn=\"System.Collections.Generic.List.Add\" "
        "rn=\"System.Collections.Generic.List`1.Add\" "
        "ns=\"System.Collections.Generic\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, list, "ConvertAll", 0), false),
        "M 06003B06 \"ConvertAll\" kind=Method access=Public ------ tpc=1 "
        "decl=\"System.Collections.Generic.List\" "
        "ret=\"System.Collections.Generic.List`1[[``0]]\" pc=1 "
        "[converter~None~False~False~False~"
        "System.Converter`2[[`0],[``0]]~<null>] I=False R=False T=False "
        "X=False B=True A=False K=0 C=False D=False P=False L=False "
        "fn=\"System.Collections.Generic.List.ConvertAll\" "
        "rn=\"System.Collections.Generic.List`1.ConvertAll\" "
        "ns=\"System.Collections.Generic\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, list, "AsReadOnly", 0), false),
        "M 06003AFF \"AsReadOnly\" kind=Method access=Public ------ tpc=0 "
        "decl=\"System.Collections.Generic.List\" "
        "ret=\"System.Collections.ObjectModel."
        "ReadOnlyCollection`1[[`0]]\" pc=0 [] "
        "I=False R=False T=False X=False B=True A=False K=0 C=False "
        "D=False P=False L=False fn=\"System.Collections.Generic.List."
        "AsReadOnly\" rn=\"System.Collections.Generic.List`1.AsReadOnly\" "
        "ns=\"System.Collections.Generic\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, obj, "Finalize", 0), false),
        "M 06000231 \"Finalize\" kind=Destructor access=Protected ---v-O "
        "tpc=0 decl=\"System.Object\" ret=\"System.Void\" pc=0 [] I=False "
        "R=False T=False X=False B=True A=False K=0 C=False D=True "
        "P=False L=False fn=\"System.Object.Finalize\" "
        "rn=\"System.Object.Finalize\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, obj, "ToString", 0), false),
        "M 0600022B \"ToString\" kind=Method access=Public ---v-O tpc=0 "
        "decl=\"System.Object\" ret=\"System.String\" pc=0 [] I=False "
        "R=False T=False X=False B=True A=False K=0 C=False D=False "
        "P=False L=False fn=\"System.Object.ToString\" "
        "rn=\"System.Object.ToString\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, obj, "ReferenceEquals", 0), false),
        "M 0600022E \"ReferenceEquals\" kind=Method access=Public s----- "
        "tpc=0 decl=\"System.Object\" ret=\"System.Boolean\" pc=2 "
        "[objA~None~False~False~False~System.Object~<null>|"
        "objB~None~False~False~False~System.Object~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.Object.ReferenceEquals\" "
        "rn=\"System.Object.ReferenceEquals\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, dec, "op_Addition", 0), false),
        "M 06000D78 \"op_Addition\" kind=Operator access=Public s----- tpc=0 "
        "decl=\"System.Decimal\" ret=\"System.Decimal\" pc=2 "
        "[d1~None~False~False~False~System.Decimal~<null>|"
        "d2~None~False~False~False~System.Decimal~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=True "
        "L=False fn=\"System.Decimal.op_Addition\" "
        "rn=\"System.Decimal.op_Addition\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, dec, "op_Explicit", 5), false),
        "M 06000D6C \"op_Explicit\" kind=Operator access=Public s----- tpc=0 "
        "decl=\"System.Decimal\" ret=\"System.Int16\" pc=1 "
        "[value~None~False~False~False~System.Decimal~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=True "
        "L=False fn=\"System.Decimal.op_Explicit\" "
        "rn=\"System.Decimal.op_Explicit\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, stream, "Read", 0), false),
        "M 06001997 \"Read\" kind=Method access=Public -a---O tpc=0 "
        "decl=\"System.IO.Stream\" ret=\"System.Int32\" pc=3 "
        "[buffer~None~False~False~False~System.Byte[]~<null>|"
        "offset~None~False~False~False~System.Int32~<null>|"
        "count~None~False~False~False~System.Int32~<null>] I=False R=False "
        "T=False X=False B=False A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.IO.Stream.Read\" rn=\"System.IO.Stream.Read\" "
        "ns=\"System.IO\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, win32, "LocalAlloc_NoSafeHandle", 0),
        false),
        "M 0600001D \"LocalAlloc_NoSafeHandle\" kind=Method access=Internal "
        "s----- tpc=0 decl=\"Microsoft.Win32.Win32Native\" ret=\"nint\" "
        "pc=2 "
        "[uFlags~None~False~False~False~System.Int32~<null>|"
        "sizetdwBytes~None~False~False~False~nuint~<null>] I=False R=False "
        "T=False X=False B=False A=False K=0 C=False D=False P=False "
        "L=False fn=\"Microsoft.Win32.Win32Native.LocalAlloc_NoSafeHandle\" "
        "rn=\"Microsoft.Win32.Win32Native.LocalAlloc_NoSafeHandle\" "
        "ns=\"Microsoft.Win32\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, enumerable, "GetEnumerator", 0),
        false),
        "M 060038BB \"GetEnumerator\" kind=Method access=Public -a---O tpc=0 "
        "decl=\"System.Collections.IEnumerable\" "
        "ret=\"System.Collections.IEnumerator\" pc=0 [] I=False R=False "
        "T=False X=False B=False A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.Collections.IEnumerable.GetEnumerator\" "
        "rn=\"System.Collections.IEnumerable.GetEnumerator\" "
        "ns=\"System.Collections\"");
    const TS::ITypeDefinition* uri =
        fx.sysA.GetTypeDefinition(TS::TopLevelTypeName("System", "Uri"));
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(MethodLine(fx.Get(fx.sysA, uri, "get_AbsoluteUri", 0), false),
        "M 0600032E \"get_AbsoluteUri\" kind=Accessor access=Public ------ "
        "tpc=0 decl=\"System.Uri\" ret=\"System.String\" pc=0 [] I=False "
        "R=False T=False X=False B=True A=True K=Getter C=False D=False "
        "P=False L=False fn=\"System.Uri.get_AbsoluteUri\" "
        "rn=\"System.Uri.get_AbsoluteUri\" ns=\"System\"");
    const TS::ITypeDefinition* traceListener = fx.sysA.GetTypeDefinition(
        TS::TopLevelTypeName("System.Diagnostics", "TraceListener"));
    ASSERT_NE(traceListener, nullptr);
    EXPECT_EQ(MethodLine(fx.Get(fx.sysA, traceListener, "get_IndentSize", 0),
        false),
        "M 06002CCD \"get_IndentSize\" kind=Accessor access=Public ------ "
        "tpc=0 decl=\"System.Diagnostics.TraceListener\" "
        "ret=\"System.Int32\" pc=0 [] I=False R=False T=False X=False B=True "
        "A=True K=Getter C=False D=False P=False L=False "
        "fn=\"System.Diagnostics.TraceListener.get_IndentSize\" "
        "rn=\"System.Diagnostics.TraceListener.get_IndentSize\" "
        "ns=\"System.Diagnostics\"");
    EXPECT_EQ(MethodLine(fx.Get(fx.mscA, enumTd, "HasFlag", 0), false),
        "M 06000E0C \"HasFlag\" kind=Method access=Public ------ tpc=0 "
        "decl=\"System.Enum\" ret=\"System.Boolean\" pc=1 "
        "[flag~None~False~False~False~System.Enum~<null>] I=False R=False "
        "T=False X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.Enum.HasFlag\" rn=\"System.Enum.HasFlag\" "
        "ns=\"System\"");

    // The vararg trio (mscorlib's only 0x05-convention rows): the __arglist
    // sentinel parameter.
    EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(0x06000553), false),
        "M 06000553 \"Concat\" kind=Method access=Public s----- tpc=0 "
        "decl=\"System.String\" ret=\"System.String\" pc=5 "
        "[arg0~None~False~False~False~System.Object~<null>|"
        "arg1~None~False~False~False~System.Object~<null>|"
        "arg2~None~False~False~False~System.Object~<null>|"
        "arg3~None~False~False~False~System.Object~<null>|"
        "~None~False~False~False~__arglist~<null>] I=False R=False T=False "
        "X=False B=True A=False K=0 C=False D=False P=False L=False "
        "fn=\"System.String.Concat\" rn=\"System.String.Concat\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(0x06000B7F), false),
        "M 06000B7F \"WriteLine\" kind=Method access=Public s----- tpc=0 "
        "decl=\"System.Console\" ret=\"System.Void\" pc=6 "
        "[format~None~False~False~False~System.String~<null>|"
        "arg0~None~False~False~False~System.Object~<null>|"
        "arg1~None~False~False~False~System.Object~<null>|"
        "arg2~None~False~False~False~System.Object~<null>|"
        "arg3~None~False~False~False~System.Object~<null>|"
        "~None~False~False~False~__arglist~<null>] I=False R=False T=False "
        "X=False B=True A=False K=0 C=False D=False P=False L=False "
        "fn=\"System.Console.WriteLine\" rn=\"System.Console.WriteLine\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(0x06000B84), false),
        "M 06000B84 \"Write\" kind=Method access=Public s----- tpc=0 "
        "decl=\"System.Console\" ret=\"System.Void\" pc=6 "
        "[format~None~False~False~False~System.String~<null>|"
        "arg0~None~False~False~False~System.Object~<null>|"
        "arg1~None~False~False~False~System.Object~<null>|"
        "arg2~None~False~False~False~System.Object~<null>|"
        "arg3~None~False~False~False~System.Object~<null>|"
        "~None~False~False~False~__arglist~<null>] I=False R=False T=False "
        "X=False B=True A=False K=0 C=False D=False P=False L=False "
        "fn=\"System.Console.Write\" rn=\"System.Console.Write\" "
        "ns=\"System\"");

    // The extension methods: mscorlib's own [Extension] classes.
    const TS::ITypeDefinition* tupleExt =
        fx.mscA.GetTypeDefinition(TS::TopLevelTypeName("System",
            "TupleExtensions"));
    ASSERT_NE(tupleExt, nullptr);
    {
        auto rows = fx.mscA.MetadataFile()->GetMethods(
            tupleExt->MetadataToken());
        std::vector<std::uint32_t> extRows;
        for (const auto& row : rows) {
            const TS::IMethod* m =
                fx.mscA.GetDefinitionMethod(row.Token);
            if (m != nullptr && m->IsExtensionMethod())
                extRows.push_back(row.Token);
        }
        EXPECT_EQ(extRows.size(), 63u);
        ASSERT_GE(extRows.size(), 3u);
        EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(extRows[0]), false),
            "M 06000473 \"Deconstruct\" kind=Method access=Public s----- "
            "tpc=1 decl=\"System.TupleExtensions\" ret=\"System.Void\" pc=2 "
            "[value~None~False~False~False~System.Tuple`1[[``0]]~<null>|"
            "item1~Out~False~False~False~``0&~<null>] I=False R=False "
            "T=False X=True B=True A=False K=0 C=False D=False P=False "
            "L=False fn=\"System.TupleExtensions.Deconstruct\" "
            "rn=\"System.TupleExtensions.Deconstruct\" ns=\"System\"");
        EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(extRows[1]), false),
            "M 06000474 \"Deconstruct\" kind=Method access=Public s----- "
            "tpc=2 decl=\"System.TupleExtensions\" ret=\"System.Void\" pc=3 "
            "[value~None~False~False~False~"
            "System.Tuple`2[[``0],[``1]]~<null>|"
            "item1~Out~False~False~False~``0&~<null>|"
            "item2~Out~False~False~False~``1&~<null>] I=False R=False "
            "T=False X=True B=True A=False K=0 C=False D=False P=False "
            "L=False fn=\"System.TupleExtensions.Deconstruct\" "
            "rn=\"System.TupleExtensions.Deconstruct\" ns=\"System\"");
        EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(extRows[2]), false),
            "M 06000475 \"Deconstruct\" kind=Method access=Public s----- "
            "tpc=3 decl=\"System.TupleExtensions\" ret=\"System.Void\" pc=4 "
            "[value~None~False~False~False~"
            "System.Tuple`3[[``0],[``1],[``2]]~<null>|"
            "item1~Out~False~False~False~``0&~<null>|"
            "item2~Out~False~False~False~``1&~<null>|"
            "item3~Out~False~False~False~``2&~<null>] I=False R=False "
            "T=False X=True B=True A=False K=0 C=False D=False P=False "
            "L=False fn=\"System.TupleExtensions.Deconstruct\" "
            "rn=\"System.TupleExtensions.Deconstruct\" ns=\"System\"");
    }
    const TS::ITypeDefinition* glob = fx.mscA.GetTypeDefinition(
        TS::TopLevelTypeName("System.Globalization", "GlobalizationExtensions"));
    ASSERT_NE(glob, nullptr);
    {
        auto rows = fx.mscA.MetadataFile()->GetMethods(glob->MetadataToken());
        std::vector<std::uint32_t> extRows;
        for (const auto& row : rows) {
            const TS::IMethod* m = fx.mscA.GetDefinitionMethod(row.Token);
            if (m != nullptr && m->IsExtensionMethod())
                extRows.push_back(row.Token);
        }
        EXPECT_EQ(extRows.size(), 1u);
        ASSERT_EQ(extRows.size(), 1u);
        EXPECT_EQ(MethodLine(fx.mscA.GetDefinitionMethod(extRows[0]), false),
            "M 06002F5C \"GetStringComparer\" kind=Method access=Public "
            "s----- tpc=0 decl=\"System.Globalization.GlobalizationExtensions\" "
            "ret=\"System.StringComparer\" pc=2 "
            "[compareInfo~None~False~False~False~"
            "System.Globalization.CompareInfo~<null>|"
            "options~None~False~False~False~"
            "System.Globalization.CompareOptions~<null>] I=False R=False "
            "T=False X=True B=True A=False K=0 C=False D=False P=False "
            "L=False "
            "fn=\"System.Globalization.GlobalizationExtensions.GetString"
            "Comparer\" "
            "rn=\"System.Globalization.GlobalizationExtensions.GetString"
            "Comparer\" ns=\"System.Globalization\"");
    }
}

// ---------------------------------------------------------------------------
// The whole-corpus digests (the probe's SWEEP section).
// ---------------------------------------------------------------------------
TEST_F(MetadataMethodTest, WholeCorpusDigests)
{
    MmFixture fx;
    {
        Fnv64 fnv;
        std::uint32_t count = 0;
        std::uint32_t total = fx.mscSingle.MetadataFile()->MethodCount();
        for (std::uint32_t row = 1; row <= total; row++) {
            const TS::IMethod* m =
                fx.mscSingle.GetDefinitionMethod(0x06000000u | row);
            ASSERT_NE(m, nullptr);
            fnv.Add(MethodLine(m, true));
            count++;
        }
        EXPECT_EQ(count, 29257u);
        EXPECT_EQ(fnv.Digest(), 0xCE7B80A943BA15AFULL);
    }
    {
        Fnv64 fnv;
        std::uint32_t count = 0;
        std::uint32_t total = fx.sysSingle.MetadataFile()->MethodCount();
        for (std::uint32_t row = 1; row <= total; row++) {
            const TS::IMethod* m =
                fx.sysSingle.GetDefinitionMethod(0x06000000u | row);
            ASSERT_NE(m, nullptr);
            fnv.Add(MethodLine(m, true));
            count++;
        }
        EXPECT_EQ(count, 18170u);
        EXPECT_EQ(fnv.Digest(), 0x3E1EBB7024DBD4BCULL);
    }
}

// ---------------------------------------------------------------------------
// The .NET 10 CoreLib curated drives (the probe's CL section): the
// IsInitOnly setter, the [return: IsReadOnly] method, the readonly-struct
// census, and the Span`1 shapes.
// ---------------------------------------------------------------------------
class MetadataMethodCoreLibTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable() || !CoreLibAvailable())
            GTEST_SKIP() << "CoreLib fixture not available";
    }
};

TEST_F(MetadataMethodCoreLibTest, CoreLibCuratedGold)
{
    MmFixture fx;
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x06000593), false),
        "M 06000593 \"set_UserTime\" kind=Accessor access=Internal ------ "
        "tpc=0 decl=\"System.Environment.ProcessCpuUsage\" "
        "ret=\"System.Void\" pc=1 "
        "[value~None~False~False~False~System.TimeSpan~<null>] I=True "
        "R=False T=True X=False B=True A=True K=Setter C=False D=False "
        "P=False L=False fn=\"System.Environment.ProcessCpuUsage."
        "set_UserTime\" rn=\"System.Environment+ProcessCpuUsage.set_UserTime\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x060009BE), false),
        "M 060009BE \"GetPinnableReference\" kind=Method access=Public ------ "
        "tpc=0 decl=\"System.String\" ret=\"System.Char&\" pc=0 [] I=False "
        "R=True T=False X=False B=True A=False K=0 C=False D=False "
        "P=False L=False fn=\"System.String.GetPinnableReference\" "
        "rn=\"System.String.GetPinnableReference\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x060002FA), false),
        "M 060002FA \".ctor\" kind=Constructor access=Internal ------ tpc=0 "
        "decl=\"System.Array.SorterObjectArray\" ret=\"System.Void\" pc=3 "
        "[keys~None~False~False~False~System.Object[]~<null>|"
        "items~None~False~False~False~System.Object[]~<null>|"
        "comparer~None~False~False~False~System.Collections.IComparer~"
        "<null>] I=False R=False T=True X=False B=True A=False K=0 "
        "C=True D=False P=False L=False fn=\"System.Array.SorterObjectArray"
        "..ctor\" rn=\"System.Array+SorterObjectArray..ctor\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x06001E7B), false),
        "M 06001E7B \"op_Inequality\" kind=Operator access=Public s----- "
        "tpc=0 decl=\"System.Span\" ret=\"System.Boolean\" pc=2 "
        "[left~None~False~False~False~System.Span`1[[`0]]~<null>|"
        "right~None~False~False~False~System.Span`1[[`0]]~<null>] I=False "
        "R=False T=True X=False B=True A=False K=0 C=False D=False "
        "P=True L=False fn=\"System.Span.op_Inequality\" "
        "rn=\"System.Span`1.op_Inequality\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x06001E7C), false),
        "M 06001E7C \"Equals\" kind=Method access=Public ----oO tpc=0 "
        "decl=\"System.Span\" ret=\"System.Boolean\" pc=1 "
        "[obj~None~False~False~False~System.Object~<null>] I=False R=False "
        "T=True X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.Span.Equals\" rn=\"System.Span`1.Equals\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x06001E7D), false),
        "M 06001E7D \"GetHashCode\" kind=Method access=Public ----oO tpc=0 "
        "decl=\"System.Span\" ret=\"System.Int32\" pc=0 [] I=False R=False "
        "T=True X=False B=True A=False K=0 C=False D=False P=False "
        "L=False fn=\"System.Span.GetHashCode\" "
        "rn=\"System.Span`1.GetHashCode\" ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x06001E7E), false),
        "M 06001E7E \"op_Implicit\" kind=Operator access=Public s----- tpc=0 "
        "decl=\"System.Span\" ret=\"System.Span`1[[`0]]\" pc=1 "
        "[array~None~False~False~False~`0[]~<null>] I=False R=False T=True "
        "X=False B=True A=False K=0 C=False D=False P=True L=False "
        "fn=\"System.Span.op_Implicit\" rn=\"System.Span`1.op_Implicit\" "
        "ns=\"System\"");
    EXPECT_EQ(MethodLine(fx.core.GetDefinitionMethod(0x06001E81), false),
        "M 06001E81 \"GetEnumerator\" kind=Method access=Public ------ tpc=0 "
        "decl=\"System.Span\" ret=\"System.Span`1+Enumerator[[`0]]\" pc=0 [] "
        "I=False R=False T=True X=False B=True A=False K=0 C=False "
        "D=False P=False L=False fn=\"System.Span.GetEnumerator\" "
        "rn=\"System.Span`1.GetEnumerator\" ns=\"System\"");
}

TEST_F(MetadataMethodCoreLibTest, CoreLibRefReadonlyCensus)
{
    // The probe's CL census over the first 12000 rows: the IsInitOnly /
    // ReturnTypeIsRefReadOnly / ThisIsRefReadOnly counts.
    MmFixture fx;
    int initOnly = 0, retRO = 0, thisRO = 0, total = 0;
    std::uint32_t limit =
        std::min<std::uint32_t>(12000, fx.coreLibFile.MethodCount());
    for (std::uint32_t row = 1; row <= limit; row++) {
        const TS::IMethod* m = fx.core.GetDefinitionMethod(0x06000000u | row);
        ASSERT_NE(m, nullptr);
        total++;
        if (m->IsInitOnly())
            initOnly++;
        if (m->ReturnTypeIsRefReadOnly())
            retRO++;
        if (m->ThisIsRefReadOnly())
            thisRO++;
    }
    EXPECT_EQ(total, 12000);
    EXPECT_EQ(initOnly, 2);
    EXPECT_EQ(retRO, 7);
    EXPECT_EQ(thisRO, 3948);
}

// ---------------------------------------------------------------------------
// The entity-cache identities (the probe's R section).
// ---------------------------------------------------------------------------
TEST_F(MetadataMethodTest, EntityCacheIdentities)
{
    MmFixture fx;
    const TS::ITypeDefinition* str = fx.TypeA("System", "String");
    const TS::IMethod* m1 = fx.Get(fx.mscA, str, "Substring", 0);
    const TS::IMethod* m2 = fx.Get(fx.mscA, str, "Substring", 0);
    ASSERT_NE(m1, nullptr);
    ASSERT_NE(m2, nullptr);
    EXPECT_EQ(m1, m2);

    // The per-row entity cache through a FRESH compilation (the handle form).
    TM::MetadataFile mscFile2{ MscorlibPath() };
    TestCompilation comp2;
    TS::MetadataModule main2{ comp2, &mscFile2,
                              TS::TypeSystemOptions::Default };
    FixedModuleRef main2Ref{ &main2 };
    comp2.Initialize(main2Ref, {});
    // The identity drives pin Object.ToString (0x0600022B): the probe
    // reached it as the first entry of String's inherited GetMethods() list;
    // the port has no Methods() enumeration yet, so the row is driven
    // directly (the same row the probe's lines pin).
    const TS::IMethod* d1 = main2.GetDefinitionMethod(0x0600022Bu);
    const TS::IMethod* d2 = main2.GetDefinitionMethod(0x0600022Bu);
    ASSERT_NE(d1, nullptr);
    EXPECT_EQ(d1, d2);

    // The Uncached variant: two GetDefinitionMethod calls construct fresh.
    const TS::IMethod* u1 =
        fx.mscUnc.GetDefinitionMethod(0x0600022Bu);
    const TS::IMethod* u2 =
        fx.mscUnc.GetDefinitionMethod(0x0600022Bu);
    EXPECT_NE(u1, u2);
    EXPECT_NE(u1, nullptr);

    // The out-of-range handle: the cached arm's HandleOutOfRange.
    {
        std::uint32_t count = main2.MetadataFile()->MethodCount();
        try {
            main2.GetDefinitionMethod(0x06000000u | (count + 1));
            FAIL() << "expected the out-of-range throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Handle with invalid row number.");
        }
    }
    // The nil handle: null.
    EXPECT_EQ(main2.GetDefinitionMethod(0x06000000u), nullptr);

    // Equals: same row twice -> true; another row -> false; the same ROW
    // NUMBER in a different module -> false.
    const auto* dm1 = dynamic_cast<const TI::MetadataMethod*>(d1);
    const auto* dm2 = dynamic_cast<const TI::MetadataMethod*>(d2);
    ASSERT_NE(dm1, nullptr);
    EXPECT_TRUE(dm1->Equals(dm2));
    const TS::IMethod* other = main2.GetDefinitionMethod(0x0600022Cu);
    const auto* dmOther =
        dynamic_cast<const TI::MetadataMethod*>(other);
    ASSERT_NE(dmOther, nullptr);
    EXPECT_FALSE(dm1->Equals(dmOther));
    const TS::IMethod* sysUri = fx.sysA.GetDefinitionMethod(0x0600032Eu);
    ASSERT_NE(sysUri, nullptr);
    const auto* dmSys = dynamic_cast<const TI::MetadataMethod*>(sysUri);
    ASSERT_NE(dmSys, nullptr);
    EXPECT_FALSE(dm1->Equals(dmSys));
    EXPECT_FALSE(dm1->Equals(nullptr));
    // The same row number in the OTHER mscorlib module (a different
    // MetadataFile instance) is a different method.
    const TS::IMethod* crossRow = fx.mscA.GetDefinitionMethod(0x0600022Bu);
    const auto* dmCross =
        dynamic_cast<const TI::MetadataMethod*>(crossRow);
    ASSERT_NE(dmCross, nullptr);
    EXPECT_FALSE(dm1->Equals(dmCross));
    EXPECT_EQ(dm1->GetHashCode(), dm2->GetHashCode());

    // ToString / names / declaring type.
    EXPECT_EQ(dm1->ToString(), "0600022B System.Object.ToString");
    EXPECT_EQ(d1->MetadataToken(), 0x0600022Bu);
    EXPECT_EQ(d1->Name(), "ToString");
    EXPECT_EQ(d1->SymbolKind(), TS::SymbolKind::Method);
    EXPECT_EQ(d1->FullName(), "System.Object.ToString");
    EXPECT_EQ(d1->ReflectionName(), "System.Object.ToString");
    EXPECT_EQ(d1->Namespace(), "System");
    EXPECT_EQ(d1->DeclaringType()->ReflectionName(), "System.Object");
    EXPECT_EQ(d1->DeclaringTypeDefinition(),
        dynamic_cast<const TS::ITypeDefinition*>(
            d1->DeclaringType().get()));
    EXPECT_EQ(d1->MemberDefinition(), d1);
    EXPECT_EQ(d1->Substitution(), &TS::TypeParameterSubstitution::Identity());
    EXPECT_EQ(d1->ReducedFrom(), nullptr);
    EXPECT_FALSE(d1->IsLocalFunction());

    // The type-parameter snapshot: two reads give the same instances;
    // TypeArguments carries the same set.
    std::vector<const TS::ITypeParameter*> tps1 = d1->TypeParameters();
    std::vector<const TS::ITypeParameter*> tps2 = d1->TypeParameters();
    EXPECT_EQ(tps1, tps2);
    EXPECT_EQ(d1->TypeArguments().size(), tps1.size());

    // A generic method's type parameters resolve through the snapshot.
    const TS::ITypeDefinition* list = fx.TypeA(
        "System.Collections.Generic", "List", 1);
    const TS::IMethod* convertAll = fx.Get(fx.mscA, list, "ConvertAll", 0);
    ASSERT_NE(convertAll, nullptr);
    std::vector<const TS::ITypeParameter*> tps = convertAll->TypeParameters();
    ASSERT_EQ(tps.size(), 1u);
    EXPECT_EQ(tps[0]->Name(), "TOutput");
    EXPECT_EQ(tps[0]->Index(), 0);

    // The accessor owner: a non-accessor answers null; an accessor's owner
    // is the property/event entity (the real engine: the Chars indexer
    // property -- the port's loud deferral until the property/event caches
    // land, pinned below in DeferralContracts).
    const TS::IMethod* plain = fx.Get(fx.mscA, str, "Substring", 0);
    EXPECT_EQ(plain->AccessorOwner(), nullptr);
}

// ---------------------------------------------------------------------------
// The IsVisible(MethodAttributes) filter matrix (the probe's O section).
// ---------------------------------------------------------------------------
TEST_F(MetadataMethodTest, VisibilityFilterMatrix)
{
    MmFixture fx;
    EXPECT_TRUE(fx.mscA.IncludeInternalMembers());
    for (std::uint32_t att : { 0x0000u, 0x0001u, 0x0002u, 0x0003u, 0x0004u,
                              0x0005u, 0x0006u, 0x0007u }) {
        EXPECT_TRUE(fx.mscA.IsMethodVisible(att)) << "att=" << att;
    }
    EXPECT_FALSE(fx.mscPub.IncludeInternalMembers());
    EXPECT_FALSE(fx.mscPub.IsMethodVisible(0x0000u));
    EXPECT_FALSE(fx.mscPub.IsMethodVisible(0x0001u));
    EXPECT_FALSE(fx.mscPub.IsMethodVisible(0x0002u));
    EXPECT_FALSE(fx.mscPub.IsMethodVisible(0x0003u));
    EXPECT_TRUE(fx.mscPub.IsMethodVisible(0x0004u));
    EXPECT_TRUE(fx.mscPub.IsMethodVisible(0x0005u));
    EXPECT_TRUE(fx.mscPub.IsMethodVisible(0x0006u));
}

// ---------------------------------------------------------------------------
// The deferral contracts.
// ---------------------------------------------------------------------------
TEST_F(MetadataMethodTest, DeferralContracts)
{
    MmFixture fx;
    const TS::ITypeDefinition* str = fx.TypeA("System", "String");
    const TS::IMethod* accessor = fx.Get(fx.mscA, str, "get_Chars", 0);
    ASSERT_NE(accessor, nullptr);
    // AccessorOwner LANDED (the MetadataProperty/MetadataEvent slice): the
    // getter of String.Chars routes to the property itself (the
    // MetadataPropertyEvent_Test suite pins the whole-corpus round trip).
    const TS::IMember* owner = accessor->AccessorOwner();
    ASSERT_NE(owner, nullptr);
    const TS::IProperty* ownerProp =
        dynamic_cast<const TS::IProperty*>(owner);
    ASSERT_NE(ownerProp, nullptr);
    EXPECT_EQ(ownerProp->Name(), "Chars");
    EXPECT_EQ(ownerProp->Getter(), accessor);
    const TS::IMethod* plain = fx.Get(fx.mscA, str, "Substring", 0);
    ASSERT_NE(plain, nullptr);
    // The explicit-interface surface LANDED (the resolve-method slice): a
    // plain method answers false / the empty list; the positive drives live
    // in ResolveMethod_Test (Array's System.Collections.ICollection
    // override rows).
    EXPECT_FALSE(plain->IsExplicitInterfaceImplementation());
    EXPECT_TRUE(plain->ExplicitlyImplementedInterfaceMembers().empty());
    // The attribute members LANDED (the MetadataMethod attribute slice;
    // the AttributeListBuilderTest Method* suite pins the full corpus
    // byte-exactly -- the gold: String.Substring's single
    // [__DynamicallyInvokable] row, the empty return-type list).
    {
        std::vector<const TS::IAttribute*> attrs = plain->GetAttributes();
        ASSERT_EQ(attrs.size(), static_cast<std::size_t>(1));
        EXPECT_EQ(attrs[0]->AttributeType().ReflectionName(),
            "__DynamicallyInvokableAttribute");
        EXPECT_TRUE(plain->GetReturnTypeAttributes().empty());
    }
    EXPECT_FALSE(plain->HasAttribute(TS::KnownAttribute::Obsolete));
    EXPECT_EQ(plain->GetAttribute(TS::KnownAttribute::Obsolete), nullptr);
    // `Specialize` LANDED: the Identity substitution returns the same
    // instance (the Specialize_Test suite pins the full arm matrix).
    EXPECT_EQ(plain->Specialize(nullptr),
        static_cast<const TS::IMethod*>(plain));
}

// A MetadataParameter drive over a real parameter row (the ctor-owned row):
// the name/refkind/optional surface.
TEST_F(MetadataMethodTest, ParameterSurfaceOverRealRows)
{
    MmFixture fx;
    const TS::ITypeDefinition* int32 = fx.TypeA("System", "Int32");
    const TS::IMethod* tryParse = fx.Get(fx.mscA, int32, "TryParse", 0);
    ASSERT_NE(tryParse, nullptr);
    std::vector<const TS::IParameter*> ps = tryParse->Parameters();
    ASSERT_EQ(ps.size(), 2u);
    // The out parameter: the raw Out flag over the byref type.
    EXPECT_EQ(ps[1]->Name(), "result");
    EXPECT_EQ(ps[1]->ReferenceKind(), TS::ReferenceKind::Out);
    EXPECT_FALSE(ps[1]->IsOptional());
    EXPECT_FALSE(ps[1]->IsParams());
    EXPECT_EQ(ps[1]->Owner(), tryParse);
    EXPECT_EQ(ps[1]->SymbolKind(), TS::SymbolKind::Parameter);
    EXPECT_FALSE(ps[1]->IsConst());
    EXPECT_EQ(ps[1]->Type().ReflectionName(), "System.Int32&");
    const auto* mp = dynamic_cast<const TI::MetadataParameter*>(ps[1]);
    ASSERT_NE(mp, nullptr);
    // The port's IParameter surface carries no MetadataToken virtual (the
    // IEntity convention); the concrete row's plain member reads the raw
    // 0x08...... token.
    EXPECT_EQ(mp->MetadataToken() >> 24, 0x08u);
    EXPECT_NE(mp->MetadataToken() & 0x00FFFFFFu, 0u);
    // The ToString: the token + the DefaultParameter static render
    // (`<refkind> name:Type`).
    EXPECT_EQ(mp->ToString(),
        TokenString(mp->MetadataToken()) + " out result:System.Int32&");
    EXPECT_FALSE(ps[1]->HasConstantValueInSignature());
    // The GetAttributes member LANDED (the AttributeListBuilder slice; the
    // TryParse out-param carries no custom rows -- the [Out] synthetic row
    // comes through the AttributeListBuilder Optional/In/Out logic).
    EXPECT_NO_THROW(mp->GetAttributes());
}

} // namespace
