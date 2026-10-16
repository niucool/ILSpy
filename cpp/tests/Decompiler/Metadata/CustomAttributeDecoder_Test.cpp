// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CustomAttributeDecoder port (the SRM DecodeValue + the repo
// copy's provideBoxingTypeInfo fusion), gold-pinned against the real engines
// through the CadProbe dumps embedded in TestFixtures/CadDecoderGold.hpp:
//   * the synthetic manifest (every exotic arm + every failure arm) decoded
//     through both the minimal provider and the module-backed provider
//   * the boxing decoder over the standalone named-arg bytes
//   * the real mscorlib / System.dll / facade / CoreLib partitions, the
//     running FNV digests over every row's full render (the whole-table
//     content pin at four lines), and the curated exact row groups
// The gold dump format (both engines produce identical lines): per row
// `tag:row=N:ctorKind=...:ok:F=..:N=..` plus one `F<i>/N<i>` line per
// argument, or the `EXCEPTION:Type:Message` line for a throwing row.

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include <cstdlib>
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "TestFixtures/CadDecoderGold.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
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

const char* SystemDllPath() {
#if defined(_WIN32)
    // The SAME copy the gold probe read (the Framework64 System.dll, NOT the
    // GAC layout -- the two carry different row orders).
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

const char* FacadePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
#else
    return "/usr/lib/mono/4.5/System.Runtime.dll";
#endif
}

// The .NET 10 shared-runtime CoreLib (the newest installed
// Microsoft.NETCore.App -- the standing glob pattern).
std::string CoreLibPath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (fs::exists(candidate, ec)) best = candidate;
    }
    return best;
}

// Writes the embedded manifest to a temp file (MetadataFile needs a real
// file) -- the AssemblyIdentityFixtures WriteTempAssembly convention.
std::string WriteSynthManifest() {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "CadSynth_test.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(ILSpy::Tests::kCadSynthManifest, 1,
        ILSpy::Tests::kCadSynthManifestSize, out);
    std::fclose(out);
    return path.string();
}

// --- the gold-dump renderers (mirror the CadProbe Program.cs) ---

std::string Esc(const std::string& utf8) {
    // The probe escapes per UTF-16 code unit; the port's strings are UTF-8.
    std::u16string units = ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    std::string out;
    char buf[8];
    for (char16_t c : units) {
        if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20 || c > 0x7E) {
            std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::string RenderVal(const std::any& v);

std::string RenderTypedArg(
    const ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument& arg) {
    std::string type = arg.Type() ? arg.Type()->ReflectionName() : "<null>";
    return "type=<" + type + ">:val=" + RenderVal(arg.Value());
}

std::string RenderVal(const std::any& v) {
    using ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument;
    if (!v.has_value()) return "null";
    if (auto b = std::any_cast<bool>(&v))
        return *b ? "bool:True" : "bool:False";
    if (auto s = std::any_cast<std::string>(&v))
        return "str:\"" + Esc(*s) + "\"";
    if (auto ch = std::any_cast<char16_t>(&v)) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "char:0x%04X", static_cast<unsigned>(*ch));
        return buf;
    }
    if (auto t = std::any_cast<TS::ITypePtr>(&v)) {
        if (!*t) return "null";
        return "type:<" + (*t)->ReflectionName() + ">";
    }
    if (auto arr =
            std::any_cast<std::vector<CustomAttributeTypedArgument>>(&v)) {
        std::string out = "arr:n=" + std::to_string(arr->size()) + ":[";
        for (std::size_t i = 0; i < arr->size(); i++) {
            if (i > 0) out += "|";
            out += RenderVal((*arr)[i].Value());
        }
        out += "]";
        return out;
    }
    if (auto boxed = std::any_cast<CustomAttributeTypedArgument>(&v)) {
        std::string type =
            boxed->Type() ? boxed->Type()->ReflectionName() : "<null>";
        return "boxed:type=<" + type + ">:val=" + RenderVal(boxed->Value());
    }
    if (auto f = std::any_cast<float>(&v))
        return "float:" + ILSpy::Decompiler::Disassembler::FormatRoundTrip(*f);
    if (auto d = std::any_cast<double>(&v))
        return "double:" + ILSpy::Decompiler::Disassembler::FormatRoundTrip(*d);
    if (auto u8 = std::any_cast<std::uint8_t>(&v))
        return "num:Byte:" + std::to_string(*u8);
    if (auto i8 = std::any_cast<std::int8_t>(&v))
        return "num:SByte:" + std::to_string(*i8);
    if (auto i16 = std::any_cast<std::int16_t>(&v))
        return "num:Int16:" + std::to_string(*i16);
    if (auto u16 = std::any_cast<std::uint16_t>(&v))
        return "num:UInt16:" + std::to_string(*u16);
    if (auto i32 = std::any_cast<std::int32_t>(&v))
        return "num:Int32:" + std::to_string(*i32);
    if (auto u32 = std::any_cast<std::uint32_t>(&v))
        return "num:UInt32:" + std::to_string(*u32);
    if (auto i64 = std::any_cast<std::int64_t>(&v))
        return "num:Int64:" + std::to_string(*i64);
    if (auto u64 = std::any_cast<std::uint64_t>(&v))
        return "num:UInt64:" + std::to_string(*u64);
    return "num:?:?";
}

// The C# exception-type name for a port exception (the convention-(d)
// mapping: BadImageFormatException -> std::invalid_argument, the NRE ->
// std::runtime_error; EnumUnderlyingTypeResolveException is itself). The
// message is escaped exactly as the probe's ExText does (the full Esc).
std::string RenderEx(const std::exception& ex, const char* csharpName) {
    return std::string("EXCEPTION:") + csharpName + ":" + Esc(ex.what());
}

// The running FNV-1a-64 over the rendered lines (the probe's digest).
struct Fnv {
    std::uint64_t h = 14695981039346656037ULL;
    void Line(const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        h ^= '\n'; h *= 1099511628211ULL;
    }
    std::string Str() const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llX",
            static_cast<unsigned long long>(h));
        return buf;
    }
};

std::string CtorKindName(std::uint32_t ctorToken) {
    switch (ctorToken >> 24) {
        case 0x06: return "MethodDefinition";
        case 0x0A: return "MemberReference";
        default: return "Handle";
    }
}

// Renders one row's decode exactly as the probe's DumpAttr does (the ok line
// plus the argument lines, or the exception line).
std::vector<std::string> RenderRow(const std::string& tag, int row,
    const TM::MetadataFile& metadata,
    const TM::CustomAttributeDecoder& decoder, std::uint32_t ctorToken,
    const std::vector<std::uint8_t>& valueBlob) {
    std::vector<std::string> lines;
    std::string prefix = tag + ":row=" + std::to_string(row) + ":";
    try {
        TM::CustomAttributeValue value =
            decoder.DecodeValue(ctorToken, valueBlob.data(), valueBlob.size());
        lines.push_back(prefix + "ctorKind=" + CtorKindName(ctorToken)
            + ":ok:F=" + std::to_string(value.FixedArguments.size())
            + ":N=" + std::to_string(value.NamedArguments.size()));
        for (std::size_t i = 0; i < value.FixedArguments.size(); i++) {
            const auto& a = value.FixedArguments[i];
            std::string type = a.Type() ? a.Type()->ReflectionName() : "<null>";
            lines.push_back(prefix + "F" + std::to_string(i) + ":type=<" + type
                + ">:val=" + RenderVal(a.Value()));
        }
        for (std::size_t i = 0; i < value.NamedArguments.size(); i++) {
            const auto& a = value.NamedArguments[i];
            std::string type = a.Type() ? a.Type()->ReflectionName() : "<null>";
            std::string kind = a.Kind()
                    == ILSpy::Decompiler::TypeSystem::
                        CustomAttributeNamedArgumentKind::Field
                ? "Field" : "Property";
            lines.push_back(prefix + "N" + std::to_string(i) + ":name=" + a.Name()
                + ":kind=" + kind + ":type=<" + type + ">:val="
                + RenderVal(a.Value()));
        }
    } catch (const TM::EnumUnderlyingTypeResolveException& ex) {
        lines.push_back(prefix + "ctorKind=" + CtorKindName(ctorToken) + ":"
            + RenderEx(ex, "EnumUnderlyingTypeResolveException"));
    } catch (const std::invalid_argument& ex) {
        lines.push_back(prefix + "ctorKind=" + CtorKindName(ctorToken) + ":"
            + RenderEx(ex, "BadImageFormatException"));
    } catch (const std::runtime_error& ex) {
        lines.push_back(prefix + "ctorKind=" + CtorKindName(ctorToken) + ":"
            + RenderEx(ex, "NullReferenceException"));
    }
    return lines;
}

// Drives every CustomAttribute row of a file through the decoder (the
// probe's SectionFile loop) and returns the per-section lines.
std::vector<std::string> DecodeAllRows(const std::string& tag,
    const TM::MetadataFile& metadata, TS::TypeProvider& provider,
    Fnv& digest) {
    std::vector<std::string> all;
    std::uint32_t rowCount =
        metadata.CorTableRowCount(TM::CorTableIndex::CustomAttribute);
    TM::CustomAttributeDecoder decoder(metadata, provider);
    for (std::uint32_t row = 1; row <= rowCount; row++) {
        auto info = metadata.GetCustomAttribute(0x0C000000 | row);
        EXPECT_TRUE(info.has_value()) << tag << " row " << row;
        if (!info || !info->ValueBlob)
            continue;
        auto lines = RenderRow(tag, static_cast<int>(row), metadata, decoder,
            info->ConstructorToken, *info->ValueBlob);
        for (const auto& l : lines) {
            all.push_back(l);
            digest.Line(l);
        }
    }
    return all;
}

// A module reference resolving to an externally-owned module (the
// TypeProvider_Test FixedModuleRef pattern).
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

class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// Compares a produced line list against a gold slice, reporting the first
// divergence with its index.
void ExpectLinesMatch(const std::vector<std::string>& produced,
                      const std::vector<std::string>& gold,
                      const char* what) {
    ASSERT_EQ(produced.size(), gold.size()) << what << " (line count)";
    for (std::size_t i = 0; i < produced.size(); i++) {
        ASSERT_EQ(produced[i], gold[i])
            << what << " line " << i << " of " << produced.size();
    }
}

// Extracts the gold lines for one (tag, row) group.
std::vector<std::string> GoldRows(const std::vector<std::string>& gold,
    const std::string& tag, int row) {
    std::string prefix = tag + ":row=" + std::to_string(row) + ":";
    std::vector<std::string> out;
    for (const auto& l : gold)
        if (l.rfind(prefix, 0) == 0) out.push_back(l);
    return out;
}

} // namespace

// The synthetic manifest decoded through the MINIMAL provider (the exact
// ApplyAttributeTypeVisitor shape): every ok row, every failure arm, and the
// minimal-provider quirks (the enum-arm EnumUnderlyingTypeResolveException,
// the UnknownType fallbacks).
TEST(CustomAttributeDecoderTest, SyntheticManifestMinimalProviderMatchesGold) {
    TM::MetadataFile manifest(WriteSynthManifest());
    TM::CustomAttributeDecoder decoder(manifest,
        TM::MinimalAttributeTypeProvider());
    std::vector<std::string> produced;
    std::uint32_t rowCount =
        manifest.CorTableRowCount(TM::CorTableIndex::CustomAttribute);
    ASSERT_EQ(rowCount, 25u);
    for (std::uint32_t row = 1; row <= rowCount; row++) {
        auto info = manifest.GetCustomAttribute(0x0C000000 | row);
        ASSERT_TRUE(info.has_value());
        auto lines = RenderRow("syn", static_cast<int>(row), manifest, decoder,
            info->ConstructorToken, *info->ValueBlob);
        produced.insert(produced.end(), lines.begin(), lines.end());
    }
    std::vector<std::string> gold;
    for (const auto& l : ILSpy::Tests::kCadSynthGold)
        if (l.rfind("syn:", 0) == 0 && l.find(":digest=") == std::string::npos)
            gold.push_back(l);
    ExpectLinesMatch(produced, gold, "synthetic/minimal");
}

// The synthetic manifest decoded through the MODULE-BACKED provider (the
// AttributeListBuilder shape: a SimpleCompilation with the manifest as the
// main module and mscorlib as the reference): the enum arms resolve, the
// fixed-arg enum TypeHandle reaches the module's entity cache.
TEST(CustomAttributeDecoderTest, SyntheticManifestModuleProviderMatchesGold) {
    std::string synthPath = WriteSynthManifest();
    TM::MetadataFile manifest(synthPath);
    TM::MetadataFile mscorlib(MscorlibPath());
    TestCompilation compilation;
    auto manifestModule = std::make_unique<TS::MetadataModule>(
        compilation, &manifest, TS::TypeSystemOptions::Default);
    auto mscorlibModule = std::make_unique<TS::MetadataModule>(
        compilation, &mscorlib, TS::TypeSystemOptions::Default);
    FixedModuleRef manifestRef(manifestModule.get());
    FixedModuleRef mscorlibRef(mscorlibModule.get());
    compilation.Initialize(manifestRef, { &mscorlibRef });
    TS::TypeProvider& provider =
        const_cast<TS::TypeProvider&>(manifestModule->TypeProvider());
    TM::CustomAttributeDecoder decoder(manifest, provider);
    std::vector<std::string> produced;
    std::uint32_t rowCount =
        manifest.CorTableRowCount(TM::CorTableIndex::CustomAttribute);
    for (std::uint32_t row = 1; row <= rowCount; row++) {
        auto info = manifest.GetCustomAttribute(0x0C000000 | row);
        ASSERT_TRUE(info.has_value());
        auto lines = RenderRow("mod", static_cast<int>(row), manifest, decoder,
            info->ConstructorToken, *info->ValueBlob);
        produced.insert(produced.end(), lines.begin(), lines.end());
    }
    std::vector<std::string> gold;
    for (const auto& l : ILSpy::Tests::kCadSynthGold)
        if (l.rfind("mod:", 0) == 0 && l.find(":digest=") == std::string::npos)
            gold.push_back(l);
    ExpectLinesMatch(produced, gold, "synthetic/module");
}

// The repo copy's provideBoxingTypeInfo=true shape over the standalone
// named-arg bytes: the object-typed named argument wraps its payload in a
// boxed nested CustomAttributeTypedArgument.
TEST(CustomAttributeDecoderTest, BoxingDecoderMatchesGold) {
    TM::MetadataFile manifest(WriteSynthManifest());
    TM::CustomAttributeDecoder decoder(manifest,
        TM::MinimalAttributeTypeProvider(), true);
    // The probe's standalone named-arg bytes (five arguments: a bool, a
    // string, an int32[], a Type, and an object carrying a string).
    const std::uint8_t namedBytes[] = {
        0x54, 0x02, 0x02, 0x70, 0x62, 0x01,
        0x53, 0x0E, 0x02, 0x66, 0x73, 0x02, 0x76, 0x73,
        0x54, 0x1D, 0x08, 0x02, 0x70, 0x69,
            0x03, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
            0x08, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00,
        0x53, 0x50, 0x02, 0x66, 0x74, 0x09,
            'N', 'S', '.', 'M', 'y', 'E', 'n', 'u', 'm',
        0x54, 0x51, 0x02, 0x70, 0x6F, 0x0E, 0x08,
            'b', 'o', 'x', 'e', 'd', '-', 'i', 'n',
    };
    std::size_t pos = 0;
    auto named = decoder.DecodeNamedArguments(namedBytes,
        sizeof(namedBytes), pos, 5);
    ASSERT_EQ(named.size(), 5u);
    std::vector<std::string> produced;
    produced.push_back("box:N=5");
    for (std::size_t i = 0; i < named.size(); i++) {
        const auto& a = named[i];
        std::string type = a.Type() ? a.Type()->ReflectionName() : "<null>";
        std::string kind = a.Kind()
                == ILSpy::Decompiler::TypeSystem::
                    CustomAttributeNamedArgumentKind::Field
            ? "Field" : "Property";
        produced.push_back("box:N" + std::to_string(i) + ":name=" + a.Name()
            + ":kind=" + kind + ":type=<" + type + ">:val="
            + RenderVal(a.Value()));
    }
    std::vector<std::string> gold;
    for (const auto& l : ILSpy::Tests::kCadSynthGold)
        if (l.rfind("box:", 0) == 0) gold.push_back(l);
    ExpectLinesMatch(produced, gold, "boxing");
}

// The real-file whole-table pin: the partition counts, the running FNV
// digest over every row's full render (the strongest compact invariant -- a
// single diverging render anywhere flips the digest), and the curated exact
// row groups.
TEST(CustomAttributeDecoderTest, RealFilesMatchGoldPartitionsAndDigests) {
    Fnv digest;
    struct Section {
        const char* tag;
        const char* path;
    };
    std::string coreLib = CoreLibPath();
    ASSERT_FALSE(coreLib.empty()) << "the .NET 10 CoreLib fixture is missing";
    Section sections[] = {
        { "msc", MscorlibPath() },
        { "sys", SystemDllPath() },
        { "fcd", FacadePath() },
        { "cor", coreLib.c_str() },
    };
    std::vector<std::string> producedPartitions;
    for (const auto& section : sections) {
        TM::MetadataFile file(section.path);
        std::vector<std::string> lines =
            DecodeAllRows(section.tag, file, TM::MinimalAttributeTypeProvider(),
                digest);
        // The partition line: the ok/throw counts by exception type.
        std::map<std::string, int> exCounts;
        std::size_t ok = 0, ex = 0;
        for (const auto& l : lines) {
            if (l.find(":ok:F=") != std::string::npos) ok++;
            else if (l.find(":EXCEPTION:") != std::string::npos) {
                ex++;
                auto pos1 = l.find(":EXCEPTION:") + 11;
                auto pos2 = l.find(':', pos1);
                exCounts[l.substr(pos1, pos2 - pos1)]++;
            }
        }
        std::string partition;
        // Recompute the total from the row walk (every row emits >= 1 line).
        std::size_t total = 0;
        {
            std::uint32_t rowCount =
                file.CorTableRowCount(TM::CorTableIndex::CustomAttribute);
            total = rowCount;
        }
        partition = std::string(section.tag) + ":total=" + std::to_string(total)
            + ":ok=" + std::to_string(ok) + ":ex=" + std::to_string(ex);
        for (const auto& kv : exCounts)
            partition += ":" + kv.first + "=" + std::to_string(kv.second);
        producedPartitions.push_back(partition);
        producedPartitions.push_back(std::string(section.tag)
            + ":digest=" + digest.Str());
    }
    std::vector<std::string> goldPartitions;
    for (const auto& l : ILSpy::Tests::kCadPartitions) goldPartitions.push_back(l);
    ExpectLinesMatch(producedPartitions, goldPartitions, "partitions/digests");
}

// The curated exact row groups: the full fixed/named argument renders of
// chosen real rows (the string/bool/byte[]/Type fixed args, the named args,
// the MemberRef ctors, the enum-arm throws, the non-ASCII strings).
TEST(CustomAttributeDecoderTest, CuratedRealRowsMatchGold) {
    struct Curated {
        const char* tag;
        int row;
    };
    const Curated curated[] = {
        { "msc", 6 }, { "msc", 11 }, { "msc", 12 }, { "msc", 29 },
        { "msc", 307 }, { "msc", 2172 },
        { "sys", 5 }, { "sys", 11 }, { "sys", 31 }, { "sys", 78 },
        { "sys", 632 }, { "sys", 982 }, { "sys", 116 },
        { "fcd", 1 }, { "fcd", 2 }, { "fcd", 3 }, { "fcd", 4 }, { "fcd", 5 },
        { "fcd", 6 }, { "fcd", 7 }, { "fcd", 8 }, { "fcd", 9 },
        { "cor", 5 }, { "cor", 6 }, { "cor", 71 }, { "cor", 81 },
        { "cor", 845 }, { "cor", 3727 },
    };
    // Load each distinct file once.
    std::string coreLib = CoreLibPath();
    ASSERT_FALSE(coreLib.empty());
    std::map<std::string, std::unique_ptr<TM::MetadataFile>> files;
    files["msc"] = std::make_unique<TM::MetadataFile>(MscorlibPath());
    files["sys"] = std::make_unique<TM::MetadataFile>(SystemDllPath());
    files["fcd"] = std::make_unique<TM::MetadataFile>(FacadePath());
    files["cor"] = std::make_unique<TM::MetadataFile>(coreLib);
    for (const auto& c : curated) {
        TM::MetadataFile& file = *files[c.tag];
        TM::CustomAttributeDecoder decoder(file,
            TM::MinimalAttributeTypeProvider());
        auto info = file.GetCustomAttribute(0x0C000000
            | static_cast<std::uint32_t>(c.row));
        ASSERT_TRUE(info.has_value()) << c.tag << " row " << c.row;
        auto produced = RenderRow(c.tag, c.row, file, decoder,
            info->ConstructorToken, *info->ValueBlob);
        auto gold = GoldRows(ILSpy::Tests::kCadCuratedRealRows, c.tag, c.row);
        ASSERT_FALSE(gold.empty()) << c.tag << " row " << c.row;
        ExpectLinesMatch(produced, gold, "curated row");
    }
}
