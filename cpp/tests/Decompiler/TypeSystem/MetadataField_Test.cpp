// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
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

// Tests for the MetadataField port (MetadataField.cs + DecimalConstantHelper.cs +
// MetadataModule.GetDefinitionField + the IsFieldVisible visibility filter +
// MetadataTypeDefinition.Fields): the field entity's identity surface, the lazy
// field-signature decode (the GenericContext VAR resolution, the modreq(IsVolatile)
// test, the ApplyAttributeTypeVisitor wrap), the constant-value machinery (the
// Constant-table read, the [DecimalConstantAttribute] decode, the invalid-typecode
// and scale-29 arms), the entity-cache identities, and the visibility filter.
//
// Every expectation is gold-pinned against the REAL ICSharpCode.Decompiler 11.0 driven
// over the identical fixtures (the C:/temp-probe/MfProbe gold probe):
//   * the curated field dumps over mscorlib (String, Int32, Decimal, Math, Char,
//     Nullable`1, List`1, Array -- the flags matrix, the decoded types over the
//     module's TypeProvider, the constant values);
//   * the decimal constants (System.Decimal's [DecimalConstantAttribute] fields with
//     the GetBits word pair), the numeric/string constant renders, and the
//     FindType(IntPtr) identity (the ELEMENT_TYPE_I chain the HKEY_* fields' "nint"
//     renders flow through);
//   * the whole-corpus FNV-1a-64 digests over EVERY field of EVERY type of mscorlib
//     (14717 fields, E5326509C430BB40) and System.dll (15896 fields,
//     654D50EF771AF652) -- the strongest pin: every name, accessibility, flag,
//     decoded type, and constant value of both corpora byte-exact -- plus the
//     volatile census (365 / 222);
//   * the crafted MfSynth.dll manifest (the real MetadataBuilder bytes): the six
//     visibility kinds, the privatescope flags, the Constant rows (int32 42, string
//     "hi", the invalid-typecode 0x42 patch), the four [DecimalConstantAttribute]
//     forms (uint / int / malformed-3-arg / scale-29 -- the ArgumentOutOfRange
//     escaping BOTH GetConstantValue arms), the [IsReadOnly] flag, the
//     modreq(IsVolatile) signature, and the VAR 0 field;
//   * the entity-cache identities (Fields()[0] IS GetDefinitionField(row); the
//     Uncached option's fresh instances; Equals' handle + module-file identity;
//     the OnlyPublicAPI visibility drop).

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/DecimalConstantHelper.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataField.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "TestFixtures/MfSynth.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }

// A module reference resolving to an externally-owned module (the C#
// `PEFile : IModuleReference` shape the gold probe's
// `new SimpleCompilation(new PEFile(...), new PEFile(...))` drives -- the
// TypeProvider_Test fixture precedent).
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
// TypeProvider_Test TestCompilation pattern).
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
// byte-for-byte (the Quote / RenderConst / FieldLine / FNV quartet), so the
// gold lines compare directly.
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
// magnitude [lo, mid, hi], the scale, and the sign (the IlspyCmdProgram
// DecimalToString recipe -- repeated division by 10^9 over the three words).
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

// The probe's RenderConst over the port's `std::any` shapes.
std::string RenderConst(const std::any& val) {
    if (!val.has_value())
        return "<null>";
    char buffer[80];
    if (auto* b = std::any_cast<bool>(&val)) {
        return *b ? "bool:True" : "bool:False";
    }
    if (auto* c = std::any_cast<char16_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "char:%04X",
            static_cast<unsigned>(*c));
        return buffer;
    }
    if (auto* v = std::any_cast<std::int8_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "i8:%d", static_cast<int>(*v));
        return buffer;
    }
    if (auto* v = std::any_cast<std::uint8_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "u8:%u",
            static_cast<unsigned>(*v));
        return buffer;
    }
    if (auto* v = std::any_cast<std::int16_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "i16:%d", static_cast<int>(*v));
        return buffer;
    }
    if (auto* v = std::any_cast<std::uint16_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "u16:%u",
            static_cast<unsigned>(*v));
        return buffer;
    }
    if (auto* v = std::any_cast<std::int32_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "i32:%d", *v);
        return buffer;
    }
    if (auto* v = std::any_cast<std::uint32_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "u32:%u", *v);
        return buffer;
    }
    if (auto* v = std::any_cast<std::int64_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "i64:%lld",
            static_cast<long long>(*v));
        return buffer;
    }
    if (auto* v = std::any_cast<std::uint64_t>(&val)) {
        std::snprintf(buffer, sizeof(buffer), "u64:%llu",
            static_cast<unsigned long long>(*v));
        return buffer;
    }
    if (auto* v = std::any_cast<float>(&val)) {
        return "f32:"
            + ILSpy::Decompiler::Disassembler::FormatRoundTrip(*v);
    }
    if (auto* v = std::any_cast<double>(&val)) {
        return "f64:"
            + ILSpy::Decompiler::Disassembler::FormatRoundTrip(*v);
    }
    if (auto* v = std::any_cast<std::string>(&val)) {
        return "str:" + Quote(*v);
    }
    if (auto* d = std::any_cast<TI::DecimalConstant>(&val)) {
        char bits[160];
        std::snprintf(bits, sizeof(bits), "dec:%08X,%08X,%08X,%08X:%s",
            d->lo, d->mid, d->hi, d->Flags(), DecimalToString(*d).c_str());
        return bits;
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

// The C# `SymbolKind.ToString()` spelling (every entity here is a Field).
const char* SymbolKindSpelling(TS::SymbolKind k) {
    if (k == TS::SymbolKind::Field)
        return "Field";
    return "<other>";
}

// The GetHashCode access (the plain member needs the concrete cast).
int mfirstHashOf(const TS::IField* f) {
    const auto* m = dynamic_cast<const TI::MetadataField*>(f);
    return m != nullptr ? m->GetHashCode() : 0;
}

std::string TokenString(std::uint32_t token) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", token);
    return buffer;
}

// The probe's FieldLine (the `sweep` compact form and the full form).
std::string FieldLine(const TS::IField* f, bool sweep) {
    std::string token = TokenString(f->MetadataToken());
    std::string name = Quote(f->Name());
    std::string access = AccessibilitySpelling(f->Accessibility());
    std::string flags = std::string(f->IsStatic() ? "s" : "-")
        + (f->IsReadOnly() ? "r" : "-") + (f->IsConst() ? "c" : "-")
        + (f->IsVolatile() ? "v" : "-")
        + (f->ReturnTypeIsRefReadOnly() ? "R" : "-");
    std::string type = Quote(f->Type().ReflectionName());
    std::string cnst = RenderConst(f->GetConstantValue(false));
    if (sweep)
        return token + "|" + name + "|" + access + "|" + flags + "|" + type
            + "|" + cnst;
    const TS::ITypeDefinition* decl = f->DeclaringTypeDefinition();
    std::string declName =
        decl != nullptr ? Quote(decl->FullName()) : Quote("");
    std::string rt = Quote(f->ReturnType().ReflectionName());
    return "F " + token + " " + name + " kind=" + SymbolKindSpelling(
        f->SymbolKind()) + " access=" + access + " " + flags + " decl="
        + declName + " type=" + type + " rt=" + rt + " const=" + cnst;
}

// The probe's FNV-1a-64 (each line feeds the bytes, then 0xff, then the
// prime multiply).
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
// System ref), the B pair (System main + mscorlib ref), and the option
// variants (OnlyPublicAPI / Uncached) -- each with its own module instances
// (a MetadataModule binds to one compilation at construction).
// ---------------------------------------------------------------------------
struct MfFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile systemFile{ SystemPath() };

    // The A pair: mscorlib main + System ref (the probe's `comp`).
    TestCompilation compA;
    TS::MetadataModule mscA{ compA, &mscorlibFile,
                             TS::TypeSystemOptions::Default };
    TS::MetadataModule sysA{ compA, &systemFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef mscARef{ &mscA };
    FixedModuleRef sysARef{ &sysA };

    // The B pair: System main + mscorlib ref (the probe's `sysComp`).
    TestCompilation compB;
    TS::MetadataModule sysB{ compB, &systemFile,
                             TS::TypeSystemOptions::Default };
    TS::MetadataModule mscB{ compB, &mscorlibFile,
                            TS::TypeSystemOptions::Default };
    FixedModuleRef sysBRef{ &sysB };
    FixedModuleRef mscBRef{ &mscB };

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

    MfFixture() {
        compA.Initialize(mscARef, { &sysARef });
        compB.Initialize(sysBRef, { &mscBRef });
        compPub.Initialize(mscPubRef, {});
        compUnc.Initialize(mscUncRef, {});
    }

    const TS::ITypeDefinition* TypeA(const char* ns, const char* name,
                                     int arity = 0) {
        const TS::ITypeDefinition* d = mscA.GetTypeDefinition(
            TS::TopLevelTypeName(ns, name, arity));
        EXPECT_NE(d, nullptr)
            << "fixture type " << ns << "." << name << "`" << arity;
        return d;
    }
};

// The MfSynthBad.dll variant: the third Constant row's Type low byte (the
// unique 02 00 28 00 pattern -- Type Boolean, Parent Field row 10) patched
// to the invalid code 0x42 (the probe's PatchBad).
std::string WritePatchedMfSynth() {
    std::vector<std::uint8_t> bytes(std::begin(ILSpy::Tests::kMfSynthBytes),
                                    std::end(ILSpy::Tests::kMfSynthBytes));
    int found = -1;
    for (std::size_t i = 0; i + 4 <= bytes.size(); ++i) {
        if (bytes[i] == 0x02 && bytes[i + 1] == 0x00 && bytes[i + 2] == 0x28
            && bytes[i + 3] == 0x00) {
            found = static_cast<int>(i);
            break;
        }
    }
    if (found < 0)
        throw std::runtime_error("fBad constant row not found");
    bytes[found] = 0x42;
    namespace fs = std::filesystem;
    fs::path path =
        fs::temp_directory_path() / "ilspy_mfsynth_test_bad.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr)
        throw std::runtime_error("cannot write MfSynthBad.dll");
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    return path.string();
}

// ---------------------------------------------------------------------------
// The gold blocks (generated from the probe dump, C:/temp-probe/MfProbe/
// gold_final.txt; the generator scripts live beside it).
// ---------------------------------------------------------------------------
// The string block (the gold F lines, count header first).
static const char* kGoldString[] = {
    "F string type=\"System.String\" count=8",
    "F 04000283 \"m_stringLength\" kind=Field access=Private ----- decl=\"System.String\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000284 \"m_firstChar\" kind=Field access=Private ----- decl=\"System.String\" type=\"System.Char\" rt=\"System.Char\" const=<null>",
    "F 04000285 \"TrimHead\" kind=Field access=Private s-c-- decl=\"System.String\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:0",
    "F 04000286 \"TrimTail\" kind=Field access=Private s-c-- decl=\"System.String\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:1",
    "F 04000287 \"TrimBoth\" kind=Field access=Private s-c-- decl=\"System.String\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:2",
    "F 04000288 \"Empty\" kind=Field access=Public sr--- decl=\"System.String\" type=\"System.String\" rt=\"System.String\" const=<null>",
    "F 04000289 \"charPtrAlignConst\" kind=Field access=Private s-c-- decl=\"System.String\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:3",
    "F 0400028A \"alignConst\" kind=Field access=Private s-c-- decl=\"System.String\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:7",
};

// The int32 block (the gold F lines, count header first).
static const char* kGoldInt32[] = {
    "F int32 type=\"System.Int32\" count=3",
    "F 040005A2 \"m_value\" kind=Field access=Internal ----- decl=\"System.Int32\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 040005A3 \"MaxValue\" kind=Field access=Public s-c-- decl=\"System.Int32\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:2147483647",
    "F 040005A4 \"MinValue\" kind=Field access=Public s-c-- decl=\"System.Int32\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:-2147483648",
};

// The decimal block (the gold F lines, count header first).
static const char* kGoldDecimal[] = {
    "F decimal type=\"System.Decimal\" count=18",
    "F 0400054B \"SignMask\" kind=Field access=Private s-c-- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:-2147483648",
    "F 0400054C \"DECIMAL_NEG\" kind=Field access=Private s-c-- decl=\"System.Decimal\" type=\"System.Byte\" rt=\"System.Byte\" const=u8:128",
    "F 0400054D \"DECIMAL_ADD\" kind=Field access=Private s-c-- decl=\"System.Decimal\" type=\"System.Byte\" rt=\"System.Byte\" const=u8:0",
    "F 0400054E \"ScaleMask\" kind=Field access=Private s-c-- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:16711680",
    "F 0400054F \"ScaleShift\" kind=Field access=Private s-c-- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:16",
    "F 04000550 \"MaxInt32Scale\" kind=Field access=Private s-c-- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:9",
    "F 04000551 \"Powers10\" kind=Field access=Private s---- decl=\"System.Decimal\" type=\"System.UInt32[]\" rt=\"System.UInt32[]\" const=<null>",
    "F 04000552 \"Zero\" kind=Field access=Public src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000000,00000000,00000000,00000000:0",
    "F 04000553 \"One\" kind=Field access=Public src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000001,00000000,00000000,00000000:1",
    "F 04000554 \"MinusOne\" kind=Field access=Public src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000001,00000000,00000000,80000000:-1",
    "F 04000555 \"MaxValue\" kind=Field access=Public src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:FFFFFFFF,FFFFFFFF,FFFFFFFF,00000000:79228162514264337593543950335",
    "F 04000556 \"MinValue\" kind=Field access=Public src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:FFFFFFFF,FFFFFFFF,FFFFFFFF,80000000:-79228162514264337593543950335",
    "F 04000557 \"NearNegativeZero\" kind=Field access=Private src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000001,00000000,00000000,801B0000:-0.000000000000000000000000001",
    "F 04000558 \"NearPositiveZero\" kind=Field access=Private src-- decl=\"System.Decimal\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000001,00000000,00000000,001B0000:0.000000000000000000000000001",
    "F 04000559 \"flags\" kind=Field access=Private ----- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 0400055A \"hi\" kind=Field access=Private ----- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 0400055B \"lo\" kind=Field access=Private ----- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 0400055C \"mid\" kind=Field access=Private ----- decl=\"System.Decimal\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
};

// The math block (the gold F lines, count header first).
static const char* kGoldMath[] = {
    "F math type=\"System.Math\" count=5",
    "F 040005BB \"doubleRoundLimit\" kind=Field access=Private s---- decl=\"System.Math\" type=\"System.Double\" rt=\"System.Double\" const=<null>",
    "F 040005BC \"maxRoundingDigits\" kind=Field access=Private s-c-- decl=\"System.Math\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:15",
    "F 040005BD \"roundPower10Double\" kind=Field access=Private s---- decl=\"System.Math\" type=\"System.Double[]\" rt=\"System.Double[]\" const=<null>",
    "F 040005BE \"PI\" kind=Field access=Public s-c-- decl=\"System.Math\" type=\"System.Double\" rt=\"System.Double\" const=f64:3.141592653589793",
    "F 040005BF \"E\" kind=Field access=Public s-c-- decl=\"System.Math\" type=\"System.Double\" rt=\"System.Double\" const=f64:2.718281828459045",
};

// The char block (the gold F lines, count header first).
static const char* kGoldChar[] = {
    "F char type=\"System.Char\" count=9",
    "F 040003F7 \"m_value\" kind=Field access=Internal ----- decl=\"System.Char\" type=\"System.Char\" rt=\"System.Char\" const=<null>",
    "F 040003F8 \"MaxValue\" kind=Field access=Public s-c-- decl=\"System.Char\" type=\"System.Char\" rt=\"System.Char\" const=char:FFFF",
    "F 040003F9 \"MinValue\" kind=Field access=Public s-c-- decl=\"System.Char\" type=\"System.Char\" rt=\"System.Char\" const=char:0000",
    "F 040003FA \"categoryForLatin1\" kind=Field access=Private sr--- decl=\"System.Char\" type=\"System.Byte[]\" rt=\"System.Byte[]\" const=<null>",
    "F 040003FB \"UNICODE_PLANE00_END\" kind=Field access=Internal s-c-- decl=\"System.Char\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:65535",
    "F 040003FC \"UNICODE_PLANE01_START\" kind=Field access=Internal s-c-- decl=\"System.Char\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:65536",
    "F 040003FD \"UNICODE_PLANE16_END\" kind=Field access=Internal s-c-- decl=\"System.Char\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:1114111",
    "F 040003FE \"HIGH_SURROGATE_START\" kind=Field access=Internal s-c-- decl=\"System.Char\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:55296",
    "F 040003FF \"LOW_SURROGATE_END\" kind=Field access=Internal s-c-- decl=\"System.Char\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:57343",
};

// The nullable block (the gold F lines, count header first).
static const char* kGoldNullable[] = {
    "F nullable type=\"System.Nullable\" count=2",
    "F 0400074A \"hasValue\" kind=Field access=Private ----- decl=\"System.Nullable\" type=\"System.Boolean\" rt=\"System.Boolean\" const=<null>",
    "F 0400074B \"value\" kind=Field access=Internal ----- decl=\"System.Nullable\" type=\"`0\" rt=\"`0\" const=<null>",
};

// The list block (the gold F lines, count header first).
static const char* kGoldList[] = {
    "F list type=\"System.Collections.Generic.List\" count=6",
    "F 0400195B \"_defaultCapacity\" kind=Field access=Private s-c-- decl=\"System.Collections.Generic.List\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:4",
    "F 0400195C \"_items\" kind=Field access=Private ----- decl=\"System.Collections.Generic.List\" type=\"`0[]\" rt=\"`0[]\" const=<null>",
    "F 0400195D \"_size\" kind=Field access=Private ----- decl=\"System.Collections.Generic.List\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 0400195E \"_version\" kind=Field access=Private ----- decl=\"System.Collections.Generic.List\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 0400195F \"_syncRoot\" kind=Field access=Private ----- decl=\"System.Collections.Generic.List\" type=\"System.Object\" rt=\"System.Object\" const=<null>",
    "F 04001960 \"_emptyArray\" kind=Field access=Private sr--- decl=\"System.Collections.Generic.List\" type=\"`0[]\" rt=\"`0[]\" const=<null>",
};

// The array block (the gold F lines, count header first).
static const char* kGoldArray[] = {
    "F array type=\"System.Array\" count=2",
    "F 040001EE \"MaxArrayLength\" kind=Field access=Internal s-c-- decl=\"System.Array\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:2146435071",
    "F 040001EF \"MaxByteArrayLength\" kind=Field access=Internal s-c-- decl=\"System.Array\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:2147483591",
};

// The MfSynth.dll field lines (the gold S F lines, row order; the
// scale-29 fDecScale field (row 15 of 18) THROWS in FieldLine instead --
// its own assertion below).
static const char* kGoldSynth[] = {
    "F 04000001 \"fPub\" kind=Field access=Public s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000002 \"fPriv\" kind=Field access=Private s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000003 \"fAsm\" kind=Field access=Internal s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000004 \"fFam\" kind=Field access=Protected s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000005 \"fFamAnd\" kind=Field access=ProtectedAndInternal s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000006 \"fFamOr\" kind=Field access=ProtectedOrInternal s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000007 \"fScope\" kind=Field access=Private ----- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000008 \"fInt\" kind=Field access=Public s-c-- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=i32:42",
    "F 04000009 \"fStr\" kind=Field access=Public s-c-- decl=\"NS.T\" type=\"System.String\" rt=\"System.String\" const=str:\"hi\"",
    "F 0400000A \"fBad\" kind=Field access=Public s-c-- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=bool:True",
    "F 0400000B \"fNoConst\" kind=Field access=Public s---- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 0400000C \"fDec\" kind=Field access=Public src-- decl=\"NS.T\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000007,00000000,00000000,00000000:7",
    "F 0400000D \"fDecInt\" kind=Field access=Public src-- decl=\"NS.T\" type=\"System.Decimal\" rt=\"System.Decimal\" const=dec:00000001,00000002,00000003,80020000:-553402322297185894.41",
    "F 0400000E \"fDecBad\" kind=Field access=Public src-- decl=\"NS.T\" type=\"System.Decimal\" rt=\"System.Decimal\" const=<null>",
    "F 04000010 \"fRO\" kind=Field access=Public s---R decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000011 \"fVol\" kind=Field access=Public s--v- decl=\"NS.T\" type=\"System.Int32\" rt=\"System.Int32\" const=<null>",
    "F 04000012 \"fGen\" kind=Field access=Public ----- decl=\"NS.T\" type=\"`0\" rt=\"`0\" const=<null>",
};

// The first six mscorlib volatile-field gold lines (the census order).
static const char* kGoldVolatile[] = {
    "0400018E \"Microsoft.Win32.RegistryKey\".\"hkey\" type=\"Microsoft.Win32.SafeHandles.SafeRegistryHandle\"",
    "0400018F \"Microsoft.Win32.RegistryKey\".\"state\" type=\"System.Int32\"",
    "04000190 \"Microsoft.Win32.RegistryKey\".\"keyName\" type=\"System.String\"",
    "04000191 \"Microsoft.Win32.RegistryKey\".\"remoteKey\" type=\"System.Boolean\"",
    "04000192 \"Microsoft.Win32.RegistryKey\".\"checkMode\" type=\"Microsoft.Win32.RegistryKeyPermissionCheck\"",
    "04000193 \"Microsoft.Win32.RegistryKey\".\"regView\" type=\"Microsoft.Win32.RegistryView\"",
};

// The six string-constant samples (the token, the gold render).
static const struct { std::uint32_t token; const char* render; }
    kGoldStringSamples[] = {
    { 0x04000003u, "str:\"4.0.0.0\"" },
    { 0x04000004u, "str:\"mscorlib.dll\"" },
    { 0x04000005u, "str:\"mscorlib.dll\"" },
    { 0x04000006u, "str:\"mscorlib.dll\"" },
    { 0x04000007u, "str:\"\\u00a9 Microsoft Corporation.  All rights reserved.\"" },
    { 0x04000008u, "str:\"4.0.0.0\"" },
};


} // namespace

// ---------------------------------------------------------------------------
// The curated field dumps.
// ---------------------------------------------------------------------------
TEST(MetadataFieldTest, CuratedFieldsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MfFixture fx;
    auto drive = [&](const TS::ITypeDefinition* type,
                     const char* const* gold, std::size_t goldCount,
                     const char* label) {
        ASSERT_NE(type, nullptr) << label;
        std::vector<const TS::IField*> fields = type->Fields();
        // gold[0] is the count header "F <label> type=<...> count=N".
        ASSERT_EQ(fields.size() + 1, goldCount) << label;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            EXPECT_EQ(FieldLine(fields[i], false), gold[i + 1])
                << label << " field " << i;
        }
    };
    drive(fx.TypeA("System", "String"), kGoldString,
        std::size(kGoldString), "string");
    drive(fx.TypeA("System", "Int32"), kGoldInt32,
        std::size(kGoldInt32), "int32");
    drive(fx.TypeA("System", "Decimal"), kGoldDecimal,
        std::size(kGoldDecimal), "decimal");
    drive(fx.TypeA("System", "Math"), kGoldMath, std::size(kGoldMath),
        "math");
    drive(fx.TypeA("System", "Char"), kGoldChar, std::size(kGoldChar),
        "char");
    drive(fx.TypeA("System", "Nullable", 1), kGoldNullable,
        std::size(kGoldNullable), "nullable");
    drive(fx.TypeA("System.Collections.Generic", "List", 1), kGoldList,
        std::size(kGoldList), "list");
    drive(fx.TypeA("System", "Array"), kGoldArray, std::size(kGoldArray),
        "array");
}

// ---------------------------------------------------------------------------
// The constants: the decimal [DecimalConstantAttribute] fields, the numeric
// renders, and the string-const samples.
// ---------------------------------------------------------------------------
TEST(MetadataFieldTest, ConstantsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MfFixture fx;
    // The FindType identity chain (the ELEMENT_TYPE_I decode target the
    // HKEY_* fields' "nint" renders flow through).
    const TS::IType& ip = fx.compA.FindType(TS::KnownTypeCode::IntPtr);
    EXPECT_EQ(ip.ReflectionName(), "System.IntPtr");
    EXPECT_EQ(ip.Kind(), TS::TypeKind::Struct);
    EXPECT_EQ(ip.Name(), "IntPtr");
    const TS::ITypeDefinition* ipDef = ip.GetDefinition();
    ASSERT_NE(ipDef, nullptr);
    EXPECT_EQ(ipDef->Namespace(), "System");
    const TS::IType& up = fx.compA.FindType(TS::KnownTypeCode::UIntPtr);
    EXPECT_EQ(up.ReflectionName(), "System.UIntPtr");
    EXPECT_EQ(up.Kind(), TS::TypeKind::Struct);

    auto findField = [&](const TS::ITypeDefinition* type,
                         const char* name) -> const TS::IField* {
        for (const TS::IField* f : type->Fields())
            if (f->Name() == name)
                return f;
        return nullptr;
    };

    // The decimal constants: System.Decimal's own static readonly fields
    // carry [DecimalConstantAttribute] (IsConst without the Literal flag).
    const TS::ITypeDefinition* dec = fx.TypeA("System", "Decimal");
    ASSERT_NE(dec, nullptr);
    struct DecGold {
        const char* name;
        const char* token;
        const char* render;
    };
    static const DecGold kDec[] = {
        {"Zero", "04000552",
         "dec:00000000,00000000,00000000,00000000:0"},
        {"One", "04000553",
         "dec:00000001,00000000,00000000,00000000:1"},
        {"MinusOne", "04000554",
         "dec:00000001,00000000,00000000,80000000:-1"},
        {"MinValue", "04000556",
         "dec:FFFFFFFF,FFFFFFFF,FFFFFFFF,80000000:"
         "-79228162514264337593543950335"},
        {"MaxValue", "04000555",
         "dec:FFFFFFFF,FFFFFFFF,FFFFFFFF,00000000:"
         "79228162514264337593543950335"},
    };
    for (const auto& g : kDec) {
        const TS::IField* f = findField(dec, g.name);
        ASSERT_NE(f, nullptr) << g.name;
        EXPECT_EQ(TokenString(f->MetadataToken()), g.token) << g.name;
        EXPECT_TRUE(f->IsConst()) << g.name;
        EXPECT_TRUE(f->IsStatic()) << g.name;
        EXPECT_TRUE(f->IsReadOnly()) << g.name;
        EXPECT_EQ(f->Type().ReflectionName(), "System.Decimal") << g.name;
        EXPECT_EQ(RenderConst(f->GetConstantValue(false)), g.render)
            << g.name;
    }
    // The gold's <missing> arm: .NET Framework 4.8 has no public MinusZero.
    EXPECT_EQ(findField(dec, "MinusZero"), nullptr);

    // The single/double const renders (the .NET 'R' notation).
    const TS::ITypeDefinition* sng = fx.TypeA("System", "Single");
    ASSERT_NE(sng, nullptr);
    EXPECT_EQ(RenderConst(findField(sng, "MinValue")->GetConstantValue(false)),
        "f32:-3.4028235E+38");
    EXPECT_EQ(RenderConst(findField(sng, "Epsilon")->GetConstantValue(false)),
        "f32:1E-45");
    EXPECT_EQ(RenderConst(findField(sng, "MaxValue")->GetConstantValue(false)),
        "f32:3.4028235E+38");
    EXPECT_EQ(
        RenderConst(findField(sng, "PositiveInfinity")->GetConstantValue(false)),
        "f32:Infinity");
    EXPECT_EQ(
        RenderConst(findField(sng, "NegativeInfinity")->GetConstantValue(false)),
        "f32:-Infinity");
    EXPECT_EQ(RenderConst(findField(sng, "NaN")->GetConstantValue(false)),
        "f32:NaN");
    const TS::ITypeDefinition* dbl = fx.TypeA("System", "Double");
    ASSERT_NE(dbl, nullptr);
    EXPECT_EQ(RenderConst(findField(dbl, "MinValue")->GetConstantValue(false)),
        "f64:-1.7976931348623157E+308");
    EXPECT_EQ(RenderConst(findField(dbl, "MaxValue")->GetConstantValue(false)),
        "f64:1.7976931348623157E+308");
    EXPECT_EQ(RenderConst(findField(dbl, "Epsilon")->GetConstantValue(false)),
        "f64:5E-324");
    EXPECT_EQ(
        RenderConst(findField(dbl, "NegativeInfinity")->GetConstantValue(false)),
        "f64:-Infinity");
    EXPECT_EQ(
        RenderConst(findField(dbl, "PositiveInfinity")->GetConstantValue(false)),
        "f64:Infinity");
    EXPECT_EQ(RenderConst(findField(dbl, "NaN")->GetConstantValue(false)),
        "f64:NaN");

    // The string-constant samples (the <Module>-area compiler literal fields,
    // by token -- the first six string-typed constants in TypeDef order).
    for (const auto& g : kGoldStringSamples) {
        const TS::IField* f = fx.mscA.GetDefinitionField(g.token);
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(RenderConst(f->GetConstantValue(false)), g.render);
    }
}

// ---------------------------------------------------------------------------
// The entity-cache identities, Equals, and the option variants.
// ---------------------------------------------------------------------------
TEST(MetadataFieldTest, IdentitiesMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MfFixture fx;
    const TS::ITypeDefinition* strA = fx.TypeA("System", "String");
    ASSERT_NE(strA, nullptr);
    std::vector<const TS::IField*> fields = strA->Fields();
    ASSERT_GT(fields.size(), 1u);

    // The per-row entity cache: two GetDefinitionField reads and the
    // Fields()[0] element are the SAME instance.
    const TS::IField* d1 =
        fx.mscA.GetDefinitionField(fields[0]->MetadataToken());
    const TS::IField* d2 =
        fx.mscA.GetDefinitionField(fields[0]->MetadataToken());
    EXPECT_EQ(d1, d2);
    EXPECT_EQ(fields[0], d1);
    // The nil token -> null.
    EXPECT_EQ(fx.mscA.GetDefinitionField(0x04000000u), nullptr);
    // A row past the Field table -> the HandleOutOfRange throw.
    EXPECT_THROW(fx.mscA.GetDefinitionField(
                     0x04000000u
                     | (fx.mscA.MetadataFile()->FieldCount() + 1)),
                 std::out_of_range);

    // Equals: the handle + module-file identity.
    EXPECT_TRUE(d1->Equals(d2, nullptr));
    EXPECT_FALSE(d1->Equals(fields[1], nullptr));
    EXPECT_FALSE(d1->Equals(nullptr, nullptr));
    const TS::ITypeDefinition* uri = fx.sysB.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Uri"));
    ASSERT_NE(uri, nullptr);
    const TS::IField* uriFirst = uri->Fields().at(0);
    EXPECT_FALSE(d1->Equals(uriFirst, nullptr));
    // The hash is stable across reads (the C# object-identity component of
    // the hash is not portable -- only the stability is).
    // GetHashCode is a plain member (the port has no object-model virtual; the
    // MetadataField cast reaches it).
    EXPECT_EQ(mfirstHashOf(d1), mfirstHashOf(d2));

    // The identity renders.
    EXPECT_EQ(TokenString(d1->MetadataToken()), "04000283");
    EXPECT_EQ(d1->Name(), "m_stringLength");
    EXPECT_EQ(d1->SymbolKind(), TS::SymbolKind::Field);
    EXPECT_EQ(d1->FullName(), "System.String.m_stringLength");
    EXPECT_EQ(d1->ReflectionName(), "System.String.m_stringLength");
    EXPECT_EQ(d1->Namespace(), "System");
    // DeclaringTypeDefinition IS DeclaringType's definition.
    EXPECT_EQ(d1->DeclaringTypeDefinition(),
              d1->DeclaringType()->GetDefinition());
    // MemberDefinition is self; Substitution is the Identity singleton.
    EXPECT_EQ(d1->MemberDefinition(), d1);
    EXPECT_EQ(d1->Substitution(), &TS::TypeParameterSubstitution::Identity());
    // The interface/virtual constants.
    EXPECT_TRUE(d1->ExplicitlyImplementedInterfaceMembers().empty());
    EXPECT_FALSE(d1->IsExplicitInterfaceImplementation());
    EXPECT_FALSE(d1->IsVirtual());
    EXPECT_FALSE(d1->IsOverride());
    EXPECT_FALSE(d1->IsOverridable());
    EXPECT_FALSE(d1->IsAbstract());
    EXPECT_FALSE(d1->IsSealed());
    // ToString (the MetadataField plain member).
    const auto* mfirst =
        dynamic_cast<const TI::MetadataField*>(fields[0]);
    ASSERT_NE(mfirst, nullptr);
    EXPECT_EQ(mfirst->ToString(), "04000283 System.String.m_stringLength");

    // The Uncached option: fresh instances per GetDefinitionField call and
    // per Fields() read (the element identity differs).
    const TS::ITypeDefinition* strUnc = fx.mscUnc.GetTypeDefinition(
        TS::TopLevelTypeName("System", "String"));
    ASSERT_NE(strUnc, nullptr);
    std::uint32_t uncToken = strUnc->Fields().at(0)->MetadataToken();
    const TS::IField* u1 = fx.mscUnc.GetDefinitionField(uncToken);
    const TS::IField* u2 = fx.mscUnc.GetDefinitionField(uncToken);
    EXPECT_NE(u1, u2);
    EXPECT_NE(strUnc->Fields().at(0), u1);

    // The OnlyPublicAPI visibility filter: Int32 keeps only the public
    // MaxValue/MinValue (m_value is Internal).
    const TS::ITypeDefinition* intPub = fx.mscPub.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Int32"));
    ASSERT_NE(intPub, nullptr);
    std::vector<std::string> visible;
    for (const TS::IField* f : intPub->Fields())
        visible.push_back(f->Name());
    EXPECT_EQ(visible, (std::vector<std::string>{"MaxValue", "MinValue"}));
    const TS::ITypeDefinition* intAll = fx.TypeA("System", "Int32");
    ASSERT_NE(intAll, nullptr);
    EXPECT_EQ(intPub->Fields().size(), 2u);
    EXPECT_EQ(intAll->Fields().size(), 3u);
}

// ---------------------------------------------------------------------------
// The whole-corpus sweeps: every field of every type of both files, rendered
// and digested -- plus the volatile census.
// ---------------------------------------------------------------------------
TEST(MetadataFieldTest, SweepDigestsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MfFixture fx;
    Fnv64 fnv;
    long count = 0;
    std::vector<std::string> volatileLines;
    for (const TS::ITypeDefinition* td : fx.mscA.TypeDefinitions()) {
        for (const TS::IField* f : td->Fields()) {
            fnv.Add(FieldLine(f, true));
            count++;
            if (f->IsVolatile()) {
                volatileLines.push_back(
                    TokenString(f->MetadataToken()) + " "
                    + Quote(td->FullName()) + "." + Quote(f->Name())
                    + " type=" + Quote(f->Type().ReflectionName()));
            }
        }
    }
    EXPECT_EQ(count, 14717);
    EXPECT_EQ(fnv.Digest(), 0xE5326509C430BB40ULL);
    EXPECT_EQ(volatileLines.size(), 365u);
    ASSERT_GE(volatileLines.size(), 6u);
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(volatileLines[i], kGoldVolatile[i]) << "volatile " << i;
    }

    Fnv64 fnvSys;
    long countSys = 0;
    int volatileSys = 0;
    for (const TS::ITypeDefinition* td : fx.sysB.TypeDefinitions()) {
        for (const TS::IField* f : td->Fields()) {
            fnvSys.Add(FieldLine(f, true));
            countSys++;
            if (f->IsVolatile())
                volatileSys++;
        }
    }
    EXPECT_EQ(countSys, 15896);
    EXPECT_EQ(fnvSys.Digest(), 0x654D50EF771AF652ULL);
    EXPECT_EQ(volatileSys, 222);
}

// ---------------------------------------------------------------------------
// The crafted MfSynth.dll manifest.
// ---------------------------------------------------------------------------
TEST(MetadataFieldTest, SynthFieldsMatchGold) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MfFixture fx;
    std::string synthPath = ILSpy::Tests::WriteMfSynthDll();
    std::string badPath = WritePatchedMfSynth();
    TM::MetadataFile synthFile{ synthPath };
    TM::MetadataFile badFile{ badPath };

    TestCompilation compS;
    TS::MetadataModule synthModule{ compS, &synthFile,
                                    TS::TypeSystemOptions::Default };
    TS::MetadataModule mscS{ compS, &fx.mscorlibFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef synthRef{ &synthModule };
    FixedModuleRef mscSRef{ &mscS };
    compS.Initialize(synthRef, { &mscSRef });

    const TS::ITypeDefinition* t = synthModule.GetTypeDefinition(
        TS::TopLevelTypeName("NS", "T", 1));
    ASSERT_NE(t, nullptr);
    std::vector<const TS::IField*> fields = t->Fields();
    EXPECT_EQ(fields.size(), 18u);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i == 14)
            continue;  // fDecScale: its FieldLine throws (below)
        std::size_t goldIndex = i < 14 ? i : i - 1;
        EXPECT_EQ(FieldLine(fields[i], false), kGoldSynth[goldIndex])
            << "field " << i;
    }

    // fDecScale (row 14 of 18): the scale-29 [DecimalConstantAttribute] makes
    // the .NET decimal ctor's ArgumentOutOfRange escape BOTH GetConstantValue
    // arms (it is not a BadImageFormatException).
    const TS::IField* fDecScale = fields[14];
    ASSERT_EQ(fDecScale->Name(), "fDecScale");
    for (bool throwOnInvalid : { false, true }) {
        try {
            fDecScale->GetConstantValue(throwOnInvalid);
            FAIL() << "the scale-29 value must throw";
        } catch (const TI::ArgumentOutOfRangeException& e) {
            EXPECT_STREQ(e.what(),
                "scale ('29') must be less than or equal to '28'. "
                "(Parameter 'scale')\r\nActual value was 29.");
        }
    }

    // The malformed 3-arg DecimalConstantAttribute: IsConst true (the row is
    // classified), the decode null (the argument-count mismatch), no throw.
    const TS::IField* fDecBad = nullptr;
    for (const TS::IField* f : fields)
        if (f->Name() == "fDecBad")
            fDecBad = f;
    ASSERT_NE(fDecBad, nullptr);
    EXPECT_TRUE(fDecBad->IsConst());
    EXPECT_FALSE(fDecBad->GetConstantValue(false).has_value());

    // The patched invalid-typecode Constant row (MfSynthBad.dll): the
    // throwOnInvalid arm propagates "Constant with invalid typecode: 66";
    // the default arm swallows it into null.
    TestCompilation compBad;
    TS::MetadataModule badModule{ compBad, &badFile,
                                  TS::TypeSystemOptions::Default };
    TS::MetadataModule mscBad{ compBad, &fx.mscorlibFile,
                              TS::TypeSystemOptions::Default };
    FixedModuleRef badRef{ &badModule };
    FixedModuleRef mscBadRef{ &mscBad };
    compBad.Initialize(badRef, { &mscBadRef });
    const TS::ITypeDefinition* tBad = badModule.GetTypeDefinition(
        TS::TopLevelTypeName("NS", "T", 1));
    ASSERT_NE(tBad, nullptr);
    const TS::IField* fBad = nullptr;
    for (const TS::IField* f : tBad->Fields())
        if (f->Name() == "fBad")
            fBad = f;
    ASSERT_NE(fBad, nullptr);
    EXPECT_FALSE(fBad->GetConstantValue(false).has_value());
    try {
        fBad->GetConstantValue(true);
        FAIL() << "the invalid-typecode value must throw";
    } catch (const std::invalid_argument& e) {
        EXPECT_STREQ(e.what(), "Constant with invalid typecode: 66");
    }

    // The OnlyPublicAPI visibility filter over the synth's visibility kinds:
    // the private/assembly/famandassem/privatescope rows drop.
    TestCompilation compSPub;
    TS::MetadataModule synthPub{
        compSPub, &synthFile,
        TS::TypeSystemOptions::Default
            | TS::TypeSystemOptions::OnlyPublicAPI
    };
    TS::MetadataModule mscSPub{ compSPub, &fx.mscorlibFile,
                               TS::TypeSystemOptions::Default };
    FixedModuleRef synthPubRef{ &synthPub };
    FixedModuleRef mscSPubRef{ &mscSPub };
    compSPub.Initialize(synthPubRef, { &mscSPubRef });
    const TS::ITypeDefinition* tPub = synthPub.GetTypeDefinition(
        TS::TopLevelTypeName("NS", "T", 1));
    ASSERT_NE(tPub, nullptr);
    std::string joined;
    for (const TS::IField* f : tPub->Fields()) {
        if (!joined.empty())
            joined += ",";
        joined += f->Name();
    }
    EXPECT_EQ(joined,
        "fPub,fFam,fFamOr,fInt,fStr,fBad,fNoConst,fDec,fDecInt,fDecBad,"
        "fDecScale,fRO,fVol,fGen");

    // ToString + the Equals identities on the synth fields.
    const auto* m0 = dynamic_cast<const TI::MetadataField*>(fields[0]);
    ASSERT_NE(m0, nullptr);
    EXPECT_EQ(m0->ToString(), "04000001 NS.T`1.fPub");
    EXPECT_TRUE(fields[0]->Equals(
        synthModule.GetDefinitionField(fields[0]->MetadataToken()),
        nullptr));
    EXPECT_FALSE(fields[0]->Equals(fields[1], nullptr));
}

// The MakeDecimal scale-check message (the .NET 10 ArgumentOutOfRange form
// the gold probe pinned) and the GetBits flags word.
TEST(MetadataFieldTest, MakeDecimalScaleCheckMatchesGold) {
    EXPECT_NO_THROW(TI::MakeDecimal(1, 2, 3, false, 28));
    try {
        TI::MakeDecimal(1, 1, 1, false, 29);
        FAIL() << "the scale-29 construction must throw";
    } catch (const TI::ArgumentOutOfRangeException& e) {
        EXPECT_STREQ(e.what(),
            "scale ('29') must be less than or equal to '28'. "
            "(Parameter 'scale')\r\nActual value was 29.");
    }
    // The flags word packs the scale and the sign (the decimal.GetBits form).
    TI::DecimalConstant one{1, 0, 0, false, 0};
    EXPECT_EQ(one.Flags(), 0x00000000u);
    TI::DecimalConstant minusQuarter{25, 0, 0, true, 2};
    EXPECT_EQ(minusQuarter.Flags(), 0x80020000u);
}

// The deferral contracts: the AttributeListBuilder machinery and the
// SpecializedField::Create factory stay loud.
TEST(MetadataFieldTest, DeferralContractsThrow) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MfFixture fx;
    const TS::ITypeDefinition* string_ = fx.TypeA("System", "String");
    ASSERT_NE(string_, nullptr);
    const TS::IField* first = string_->Fields().at(0);
    ASSERT_NE(first, nullptr);
    EXPECT_THROW(first->GetAttributes(), std::logic_error);
    EXPECT_THROW(first->HasAttribute(TS::KnownAttribute::Obsolete),
                 std::logic_error);
    EXPECT_THROW(first->GetAttribute(TS::KnownAttribute::Obsolete),
                 std::logic_error);
    EXPECT_THROW(
        first->Specialize(&TS::TypeParameterSubstitution::Identity()),
        std::logic_error);
}
