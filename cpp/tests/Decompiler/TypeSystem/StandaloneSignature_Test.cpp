// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the MetadataModule DecodeMethodSignature / DecodeLocalSignature
// port (MetadataModule.cs lines 820-838, the "#region Decode Standalone
// Signature") plus the walker's DecodeTypeSequence zero-count rejection the
// gold exposed: every expectation gold-dumped from the REAL
// ICSharpCode.Decompiler 11.0 driven over the identical fixtures
// (C:/temp-probe/SsProbe: a SimpleCompilation per corpus file, the crafted
// 19-row SsSynth.dll manifest, and the out-of-range/nil/class-context
// failure arms; the full dump in C:/temp-probe/SsProbe/gold_raw.txt).
//
// The whole-corpus sweeps fold every StandaloneSig row of mscorlib (3908
// rows: 3895 local-variable sigs, 13 field-kind) and System.dll (2788 rows)
// through DecodeLocalSignature, and CoreLib's 24 method-kind rows (the
// calli standalone sigs -- the only local corpus carrying any) through
// DecodeMethodSignature, each as the established FNV-1a-64 digest over the
// probe's exact line renders. The crafted drives pin every header shape and
// failure arm line-for-line.

#include "TestFixtures/SsSynth.hpp"
#include <cstdlib>

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
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

// A module reference resolving to an externally-owned module (the
// ResolveMethod_Test fixture precedent).
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

// The port's SimpleCompilation with the protected Init exposed.
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// The probe's per-file compilation shape: one MetadataFile, one module over
// it, a SimpleCompilation resolving through it.
template <typename TFixture>
struct FileFixtureBase {
    TM::MetadataFile file;
    TestCompilation comp;
    TS::MetadataModule module;
    FixedModuleRef ref;

    explicit FileFixtureBase(const char* path)
        : file(path),
          module(comp, &file, TS::TypeSystemOptions::Default),
          ref(&module) {
        comp.Initialize(ref, {});
    }
};

struct MscorlibFixture : FileFixtureBase<MscorlibFixture> {
    MscorlibFixture() : FileFixtureBase(MscorlibPath()) {}
};

struct SystemFixture : FileFixtureBase<SystemFixture> {
    SystemFixture() : FileFixtureBase(SystemPath()) {}
};

// The crafted 19-row manifest (the temp-file writer from the shared fixture
// header).
struct SynthFixture {
    std::string path = WriteSsSynthDll();
    TM::MetadataFile file;
    TestCompilation comp;
    TS::MetadataModule module;
    FixedModuleRef ref;

    SynthFixture()
        : file(path),
          module(comp, &file, TS::TypeSystemOptions::Default),
          ref(&module) {
        comp.Initialize(ref, {});
    }
};

// The established FNV-1a-64 form (the ResolveMethod_Test Fnv64 -- the
// xor-then-prime per byte plus the 0xff line terminator).
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

// The `TypeKind.ToString()` spelling (the C# enum names; the port's enum
// carries the identical member set).
std::string TypeKindName(TS::TypeKind kind)
{
    switch (kind) {
        case TS::TypeKind::Other: return "Other";
        case TS::TypeKind::Class: return "Class";
        case TS::TypeKind::Interface: return "Interface";
        case TS::TypeKind::Struct: return "Struct";
        case TS::TypeKind::Delegate: return "Delegate";
        case TS::TypeKind::Enum: return "Enum";
        case TS::TypeKind::Void: return "Void";
        case TS::TypeKind::Unknown: return "Unknown";
        case TS::TypeKind::Null: return "Null";
        case TS::TypeKind::None: return "None";
        case TS::TypeKind::Dynamic: return "Dynamic";
        case TS::TypeKind::UnboundTypeArgument: return "UnboundTypeArgument";
        case TS::TypeKind::TypeParameter: return "TypeParameter";
        case TS::TypeKind::Array: return "Array";
        case TS::TypeKind::Pointer: return "Pointer";
        case TS::TypeKind::ByReference: return "ByReference";
        case TS::TypeKind::Intersection: return "Intersection";
        case TS::TypeKind::ArgList: return "ArgList";
        case TS::TypeKind::Tuple: return "Tuple";
        case TS::TypeKind::ModOpt: return "ModOpt";
        case TS::TypeKind::ModReq: return "ModReq";
        case TS::TypeKind::NInt: return "NInt";
        case TS::TypeKind::NUInt: return "NUInt";
        case TS::TypeKind::FunctionPointer: return "FunctionPointer";
    }
    return "?";
}

// The probe's Quote escape form.
std::string Quote(const std::string& s)
{
    std::string result = "\"";
    char buf[8];
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32 || c > 126) {
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    result += buf;
                } else {
                    result += ch;
                }
                break;
        }
    }
    return result + "\"";
}

// The probe's RenderTypes: "[n]|Kind:\"name\"|...".
std::string RenderTypes(const std::vector<TS::ITypePtr>& types)
{
    std::string result = "[" + std::to_string(types.size()) + "]";
    for (const auto& t : types)
        result += "|" + TypeKindName(t->Kind()) + ":"
            + Quote(t->ReflectionName());
    return result;
}

// The probe's RenderFpt: the header raw value, the numeric calling
// convention, the custom conventions, the return type, and the parameters
// with their numeric reference kinds.
std::string RenderFpt(const TS::MetadataModule::DecodedStandaloneMethodSignature& s)
{
    char buf[16];
    std::string result = "hdr=0x";
    std::snprintf(buf, sizeof buf, "%02X", s.Header.RawValue());
    result += buf;
    result += "|cc=" + std::to_string(
        static_cast<int>(s.Type->CallingConvention()));
    result += "|rcc=" + std::to_string(
        s.Type->CustomCallingConventions().size());
    for (const auto& c : s.Type->CustomCallingConventions())
        result += ":" + Quote(c->ReflectionName());
    result += "|ret=" + TypeKindName(s.Type->ReturnType()->Kind()) + ":"
        + Quote(s.Type->ReturnType()->ReflectionName());
    result += s.Type->ReturnIsRefReadOnly() ? "|retRO=True" : "|retRO=False";
    result += "|params=" + std::to_string(
        s.Type->ParameterTypes().size());
    for (std::size_t i = 0; i < s.Type->ParameterTypes().size(); i++) {
        result += "|" + std::to_string(static_cast<int>(
            s.Type->ParameterReferenceKinds()[i]));
        result += ":" + TypeKindName(s.Type->ParameterTypes()[i]->Kind());
        result += ":" + Quote(s.Type->ParameterTypes()[i]->ReflectionName());
    }
    return result;
}

std::string Hex2(std::uint8_t v)
{
    char buf[4];
    std::snprintf(buf, sizeof buf, "%02X", v);
    return buf;
}

// The row's 0x11...... token rendered the probe's way.
std::string RowOf(std::uint32_t token)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", token);
    return buf;
}

// The raw method-body local-sig token of a MethodDef row (the probe's
// GetLocalSignature over the PEReader's method-body block).
std::uint32_t LocalSigTokenOf(const TM::MetadataFile& file,
                              std::uint32_t methodToken)
{
    std::uint32_t rva = file.GetMethodRVA(methodToken);
    if (rva == 0) return 0;
    TM::MethodBody body = file.GetMethodBody(rva);
    return body.LocalVarSigToken();
}

} // namespace

// ---------------------------------------------------------------------------
// StandaloneSignatureTest: the decode entries over the real corpora
// ---------------------------------------------------------------------------

// The mscorlib sweep: every StandaloneSig row (3908 rows: 3895
// local-variable-kind decodes, 13 field-kind "O" lines), the digest the
// probe folded over the identical lines (DIGEST|MSC|fnv=57D922DA3032FBF4).
TEST(StandaloneSignatureTest, MscorlibLocalSignatureSweep)
{
    ASSERT_TRUE(FileExists(MscorlibPath()));
    MscorlibFixture f;
    ASSERT_TRUE(f.file.IsValid());
    Fnv64 fnv;
    int rows = 0, local = 0, other = 0, err = 0;
    for (std::uint32_t token : f.file.StandaloneSignatureTokens()) {
        rows++;
        auto blob = f.file.GetStandaloneSignatureBlob(token);
        ASSERT_TRUE(blob.has_value());
        if (!blob->empty() && ((*blob)[0] & 0x0F) == 7) {
            local++;
            std::string line;
            try {
                auto types = f.module.DecodeLocalSignature(
                    token, TS::GenericContext());
                line = "L " + RowOf(token) + " " + RenderTypes(types);
            } catch (const std::invalid_argument& ex) {
                err++;
                line = "L " + RowOf(token) + " EX invalid_argument: "
                    + Quote(ex.what());
            }
            fnv.Add(line);
        } else {
            other++;
            std::string nibble = blob->empty()
                ? "-" : std::to_string((*blob)[0] & 0x0F);
            fnv.Add("O " + RowOf(token) + " nibble=" + nibble);
        }
    }
    EXPECT_EQ(rows, 3908);
    EXPECT_EQ(local, 3895);
    EXPECT_EQ(other, 13);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(fnv.Digest(), 0x57D922DA3032FBF4ULL);
}

// The System.dll sweep (DIGEST|SYS|fnv=2556DC198237BD10).
TEST(StandaloneSignatureTest, SystemLocalSignatureSweep)
{
    ASSERT_TRUE(FileExists(SystemPath()));
    SystemFixture f;
    ASSERT_TRUE(f.file.IsValid());
    Fnv64 fnv;
    int rows = 0, local = 0, other = 0, err = 0;
    for (std::uint32_t token : f.file.StandaloneSignatureTokens()) {
        rows++;
        auto blob = f.file.GetStandaloneSignatureBlob(token);
        ASSERT_TRUE(blob.has_value());
        if (!blob->empty() && ((*blob)[0] & 0x0F) == 7) {
            local++;
            std::string line;
            try {
                auto types = f.module.DecodeLocalSignature(
                    token, TS::GenericContext());
                line = "L " + RowOf(token) + " " + RenderTypes(types);
            } catch (const std::invalid_argument& ex) {
                err++;
                line = "L " + RowOf(token) + " EX invalid_argument: "
                    + Quote(ex.what());
            }
            fnv.Add(line);
        } else {
            other++;
            std::string nibble = blob->empty()
                ? "-" : std::to_string((*blob)[0] & 0x0F);
            fnv.Add("O " + RowOf(token) + " nibble=" + nibble);
        }
    }
    EXPECT_EQ(rows, 2788);
    EXPECT_EQ(local, 2781);
    EXPECT_EQ(other, 7);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(fnv.Digest(), 0x2556DC198237BD10ULL);
}

// The CoreLib method-kind sweep: the 24 calli standalone signatures -- the
// only local corpus rows whose header nibble is a method kind (the
// MDIGEST|COREM|fnv=F1A521A25AD45A09 gold).
TEST(StandaloneSignatureTest, CoreLibMethodSignatureSweep)
{
    ASSERT_TRUE(CoreLibPath()[0] != '\0');
    ASSERT_TRUE(FileExists(CoreLibPath()));
    FileFixtureBase<struct CoreLibTag> f(CoreLibPath());
    ASSERT_TRUE(f.file.IsValid());
    Fnv64 fnv;
    int rows = 0, ok = 0, err = 0;
    for (std::uint32_t token : f.file.StandaloneSignatureTokens()) {
        auto blob = f.file.GetStandaloneSignatureBlob(token);
        ASSERT_TRUE(blob.has_value());
        if (blob->empty()) continue;
        int n = (*blob)[0] & 0x0F;
        bool isMethod = n <= 5 || n == 9;
        if (!isMethod) continue;
        rows++;
        std::string line;
        try {
            auto sig = f.module.DecodeMethodSignature(
                token, TS::GenericContext());
            line = "M " + RowOf(token) + " " + RenderFpt(sig);
            ok++;
        } catch (const std::invalid_argument& ex) {
            err++;
            line = "M " + RowOf(token) + " EX invalid_argument: "
                + Quote(ex.what());
        }
        fnv.Add(line);
    }
    EXPECT_EQ(rows, 24);
    EXPECT_EQ(ok, 24);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(fnv.Digest(), 0xF1A521A25AD45A09ULL);
}

// The crafted-manifest drives: all 19 rows through both entries, every
// render and failure message line-for-line against the gold (the probe's
// CraftedDrives section; the port's BadImageFormatException mapping is
// std::invalid_argument, the message texts verbatim).
TEST(StandaloneSignatureTest, CraftedManifestDrives)
{
    SynthFixture f;
    ASSERT_FALSE(f.path.empty());
    ASSERT_TRUE(f.file.IsValid());
    std::vector<std::string> expected = {
        "M 1 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 1 0x07 [2]|Unknown:\"System.Int32\"|Unknown:\"System.String\"",
        "M 2 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 2 0x07 [2]|Other:\"System.String pinned\"|ByReference:\"System.Int32&\"",
        "M 3 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 3 0x07 [2]|Array:\"System.Int32[]\"|Array:\"System.Boolean[,]\"",
        "M 4 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 4 0x07 [2]|Unknown:\"System.TypedReference\"|Array:\"System.Object[]\"",
        "M 5 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 5 0x07 [1]|FunctionPointer:\"delegate*\"",
        "M 6 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 6 0x07 [2]|TypeParameter:\"`0\"|TypeParameter:\"``0\"",
        "M 7 0x07 EX invalid_argument: \"Expected Method signature\"",
        "L 7 0x07 EX logic_error: \"Signature type sequence must have at least one element.\"",
        "M 8 0x00 hdr=0x00|cc=0|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=0",
        "L 8 0x00 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 9 0x05 hdr=0x05|cc=5|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=2|0:Unknown:\"System.Int32\"|0:Unknown:\"System.String\"",
        "L 9 0x05 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 10 0x01 hdr=0x01|cc=1|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=0",
        "L 10 0x01 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 11 0x09 hdr=0x09|cc=9|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=0",
        "L 11 0x09 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 12 0x20 hdr=0x20|cc=0|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=1|0:Unknown:\"System.String\"",
        "L 12 0x20 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 13 0x10 hdr=0x10|cc=0|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=1|0:TypeParameter:\"``0\"",
        "L 13 0x10 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 14 0x00 hdr=0x00|cc=0|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=1|0:FunctionPointer:\"delegate*\"",
        "L 14 0x00 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 15 0x00 hdr=0x00|cc=0|rcc=0|ret=ByReference:\"System.Int32&\"|retRO=False|params=0",
        "L 15 0x00 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 16 0x00 hdr=0x00|cc=0|rcc=0|ret=Unknown:\"System.Void\"|retRO=False|params=1|0:Unknown:\"System.Int32\"",
        "L 16 0x00 EX invalid_argument: \"Expected LocalVariables signature\"",
        "M 17 0x06 EX invalid_argument: \"Format of the executable (.exe) or library (.dll) is invalid.\"",
        "L 17 0x06 EX invalid_argument: \"Format of the executable (.exe) or library (.dll) is invalid.\"",
        "M 18 0x08 EX invalid_argument: \"Format of the executable (.exe) or library (.dll) is invalid.\"",
        "L 18 0x08 EX invalid_argument: \"Format of the executable (.exe) or library (.dll) is invalid.\"",
        "M 19 - EX invalid_argument: \"Read out of bounds.\"",
        "L 19 - EX invalid_argument: \"Read out of bounds.\"",
    };
    std::vector<std::string> actual;
    std::uint32_t row = 0;
    for (std::uint32_t token : f.file.StandaloneSignatureTokens()) {
        row++;
        auto blob = f.file.GetStandaloneSignatureBlob(token);
        ASSERT_TRUE(blob.has_value());
        std::string head = blob->empty()
            ? "-" : "0x" + Hex2((*blob)[0]);
        try {
            auto sig = f.module.DecodeMethodSignature(
                token, TS::GenericContext());
            actual.push_back("M " + std::to_string(row) + " " + head
                + " " + RenderFpt(sig));
        } catch (const std::invalid_argument& ex) {
            actual.push_back("M " + std::to_string(row) + " " + head
                + " EX invalid_argument: " + Quote(ex.what()));
        } catch (const std::logic_error& ex) {
            actual.push_back("M " + std::to_string(row) + " " + head
                + " EX logic_error: " + Quote(ex.what()));
        }
        try {
            auto types = f.module.DecodeLocalSignature(
                token, TS::GenericContext());
            actual.push_back("L " + std::to_string(row) + " " + head
                + " " + RenderTypes(types));
        } catch (const std::invalid_argument& ex) {
            actual.push_back("L " + std::to_string(row) + " " + head
                + " EX invalid_argument: " + Quote(ex.what()));
        } catch (const std::logic_error& ex) {
            actual.push_back("L " + std::to_string(row) + " " + head
                + " EX logic_error: " + Quote(ex.what()));
        }
    }
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); i++) {
        EXPECT_EQ(actual[i], expected[i]) << "line " << i;
    }
}

// The failure arms (the probe's FailureArms section): the out-of-range and
// nil handles, the class-context drives, and the method-body local-sig
// token of List`1.Add resolved with its declaring type's context.
TEST(StandaloneSignatureTest, FailureArms)
{
    SynthFixture f;
    ASSERT_FALSE(f.path.empty());
    ASSERT_TRUE(f.file.IsValid());
    std::uint32_t count = static_cast<std::uint32_t>(
        f.file.StandaloneSignatureTokens().size());
    ASSERT_EQ(count, 19u);

    // Out-of-range handle: one past the last row.
    try {
        f.module.DecodeLocalSignature((0x11u << 24) | (count + 1),
                                      TS::GenericContext());
        FAIL() << "expected the out-of-range throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Read out of bounds.");
    }
    try {
        f.module.DecodeMethodSignature((0x11u << 24) | (count + 1),
                                       TS::GenericContext());
        FAIL() << "expected the out-of-range throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Read out of bounds.");
    }
    // Nil handle (row 0).
    try {
        f.module.DecodeLocalSignature(0, TS::GenericContext());
        FAIL() << "expected the nil throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Read out of bounds.");
    }
    try {
        f.module.DecodeMethodSignature(0, TS::GenericContext());
        FAIL() << "expected the nil throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Read out of bounds.");
    }

    // The VAR/MVAR row with the Ns.T`1 class context (row 6): the U
    // parameter's positional reflection names.
    {
        const TS::ITypeDefinition* t1 = f.module.GetTypeDefinition(
            TS::TopLevelTypeName("Ns", "T", 1));
        ASSERT_NE(t1, nullptr);
        TS::GenericContext ctx(t1->TypeParameters());
        auto types = f.module.DecodeLocalSignature((0x11u << 24) | 6, ctx);
        EXPECT_EQ(RenderTypes(types),
                  "[2]|TypeParameter:\"`0\"|TypeParameter:\"``0\"");
    }

    // The mscorlib List`1.Add local sig with its declaring type's context
    // (the probe's listAddCtx drive).
    ASSERT_TRUE(FileExists(MscorlibPath()));
    MscorlibFixture m;
    const TS::ITypeDefinition* list = m.module.GetTypeDefinition(
        TS::TopLevelTypeName("System.Collections.Generic", "List", 1));
    ASSERT_NE(list, nullptr);
    const TS::IMethod* add = nullptr;
    for (const TS::IMethod* method : list->GetMethods(
            [](const TS::IMethod* candidate) {
                return candidate->Name() == "Add";
            },
            TS::GetMemberOptions::IgnoreInheritedMembers)) {
        add = method;
        break;
    }
    ASSERT_NE(add, nullptr);
    std::uint32_t localSig = LocalSigTokenOf(m.file, add->MetadataToken());
    ASSERT_NE(localSig, 0u);
    auto types = m.module.DecodeLocalSignature(
        localSig, TS::GenericContext(list->TypeParameters()));
    EXPECT_EQ(RenderTypes(types), "[1]|Struct:\"System.Int32\"");
}
