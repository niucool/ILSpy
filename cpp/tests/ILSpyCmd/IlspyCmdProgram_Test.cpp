// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR ANY OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The IlspyCmdProgram -il path: TryLoadPDB (the -usepdb dispatch over
// DebugInfoUtils LoadSymbols/FromFile) and ShowIL (the whole-module IL
// render -- the `// IL code: <name>` header line plus the
// ReflectionDisassembler WriteModuleContents walk with DebugInfo and
// ShowSequencePoints set). The exact renders are pinned against the REAL
// ilspycmd 11.0 `-il` output over the identical fixtures (tiny.netmodule,
// and the discovery module with its associated synthetic PDB -- the
// LoadSymbols discovery the bare -usepdb form takes): the gold dumps are
// byte-identical C# console output, CRLF line endings included.

#include "ILSpyCmd/IlspyCmdProgram.hpp"
#include <cstdlib>
#include "ILSpyCmd/ResourceExtensions.hpp"
#include "ILSpyX/PdbProvider/PortableDebugInfoProvider.hpp"
#include "TestFixtures/BamlResFixtures.hpp"
#include "TestFixtures/DiscoveryNetModule.hpp"
#include "TestFixtures/ResourcesTestFixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Tests::FixtureId;
using ILSpy::Tests::PatchedNetModule;
using ILSpy::Tests::PortableCodeViewEntry;
using ILSpy::Tests::WriteDiscoveryModule;
using ILSpy::Tests::WriteSyntheticPdbFile;
namespace Cmd = ILSpy::ILSpyCmd;
namespace Pdb = ILSpy::ILSpyX::PdbProvider;

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// The -o writer tests' temp directory (the DumpPackage_Test convention).
fs::path TempDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("ilspy_ilspycmdprogram_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// The whole file as raw bytes (the read-back side of the -o writer
// round trips).
std::string ReadFileBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>());
}

// The dynamic cast the TryLoadPDB tests inspect the provider through
// (null when the dispatch yielded no debug info).
const Pdb::PortableDebugInfoProvider* AsPortable(
    const std::unique_ptr<ILSpy::Decompiler::DebugInfo::IDebugInfoProvider>& provider) {
    return dynamic_cast<const Pdb::PortableDebugInfoProvider*>(provider.get());
}

// Writes the discovery module (a portable-CodeView entry whose CV path
// resolves to the adjacent foo.pdb) and its associated synthetic PDB; the
// bare -usepdb form's LoadSymbols discovery opens exactly this pair.
// Returns the module and PDB paths.
std::pair<std::string, std::string> WriteDiscoveryPair() {
    FixtureId id;
    PatchedNetModule module = WriteDiscoveryModule(
        {PortableCodeViewEntry(id, "some/build/dir/foo.pdb")});
    std::string pdbPath =
        (fs::path(module.Path).parent_path() / "foo.pdb").string();
    EXPECT_TRUE(WriteSyntheticPdbFile(pdbPath));
    return {module.Path, pdbPath};
}

// The real ilspycmd 11.0 `-il` output over the tiny.netmodule fixture
// (494 bytes): the `// IL code: tiny` header line (the Module table Name --
// a netmodule) plus the whole-module contents. No -usepdb, no sequence
// points.
const std::string kGoldTiny =
    "// IL code: tiny\r\n.class private auto ansi '<Module>'\r\n{"
    "\r\n} // end of class <Module>\r\n\r\n.class public auto ans"
    "i Tiny\r\n\textends [mscorlib]System.Object\r\n{\r\n\t// Met"
    "hods\r\n\t.method public hidebysig static \r\n\t\tint32 Add "
    "(\r\n\t\t\tint32 a,\r\n\t\t\tint32 b\r\n\t\t) cil managed \r"
    "\n\t{\r\n\t\t// Method begins at RVA 0x2050\r\n\t\t// Header"
    " size: 12\r\n\t\t// Code size: 4 (0x4)\r\n\t\t.maxstack 2\r\n"
    "\r\n\t\tIL_0000: ldarg.0\r\n\t\tIL_0001: ldarg.1\r\n\t\tIL_0"
    "002: add\r\n\t\tIL_0003: ret\r\n\t} // end of method Tiny::A"
    "dd\r\n\r\n} // end of class Tiny\r\n\r\n";

// The real ilspycmd 11.0 `-il --il-sequence-points -usepdb` output over the
// discovery module fixture (564 bytes): the same metadata render plus the
// sequence point the discovered synthetic PDB carries for Add
// (0x06000001) -- one point before IL_0000 (the PDB's later points sit
// past the 4-byte body and never render).
const std::string kGoldSequencePoints =
    "// IL code: tiny\r\n.class private auto ansi '<Module>'\r\n{"
    "\r\n} // end of class <Module>\r\n\r\n.class public auto ans"
    "i Tiny\r\n\textends [mscorlib]System.Object\r\n{\r\n\t// Met"
    "hods\r\n\t.method public hidebysig static \r\n\t\tint32 Add "
    "(\r\n\t\t\tint32 a,\r\n\t\t\tint32 b\r\n\t\t) cil managed \r"
    "\n\t{\r\n\t\t// Method begins at RVA 0x2050\r\n\t\t// Header"
    " size: 12\r\n\t\t// Code size: 4 (0x4)\r\n\t\t.maxstack 2\r\n"
    "\r\n\t\t// sequence point: (line 10, col 3) to (line 10, col"
    " 7) in C:\\a.cs\r\n\t\tIL_0000: ldarg.0\r\n\t\tIL_0001: ldar"
    "g.1\r\n\t\tIL_0002: add\r\n\t\tIL_0003: ret\r\n\t} // end of"
    " method Tiny::Add\r\n\r\n} // end of class Tiny\r\n\r\n";

}  // namespace

// ---- TryLoadPDB: the InputPDBFile dispatch ----

// An unset flag: no debug info (the C# `return null`).
TEST(IlspyCmdProgramTest, TryLoadPDBUnsetFlagYieldsNoDebugInfo) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    Cmd::InputPDBFile pdb;
    EXPECT_EQ(Cmd::TryLoadPDB(file, pdb), nullptr);
}

// The bare `-usepdb` form (Value nullopt): the module's PDB discovered
// through the PE debug directory (LoadSymbols) -- the associated foo.pdb
// the portable-CodeView entry names (gold: "Loaded from portable PDB:
// <temp>\foo.pdb").
TEST(IlspyCmdProgramTest, TryLoadPDBBareFlagDiscoversPdb) {
    auto [modulePath, pdbPath] = WriteDiscoveryPair();
    MetadataFile file(modulePath);
    ASSERT_TRUE(file.IsValid());
    Cmd::InputPDBFile pdb;
    pdb.IsSet = true;
    auto provider = Cmd::TryLoadPDB(file, pdb);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_EQ(portable->Description(), "Loaded from portable PDB: " + pdbPath);
    EXPECT_EQ(portable->SourceFileName(), pdbPath);
}

// The valued `-usepdb <file>` form: the named PDB opened (FromFile) -- the
// module's own debug directory is never consulted (mscorlib's native
// CodeView entry is ignored; the provider wraps the synthetic PDB).
TEST(IlspyCmdProgramTest, TryLoadPDBValuedFlagOpensPdbFromFile) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    auto [modulePath, pdbPath] = WriteDiscoveryPair();
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    Cmd::InputPDBFile pdb;
    pdb.IsSet = true;
    pdb.Value = pdbPath;
    auto provider = Cmd::TryLoadPDB(file, pdb);
    ASSERT_NE(provider, nullptr);
    const Pdb::PortableDebugInfoProvider* portable = AsPortable(provider);
    ASSERT_NE(portable, nullptr);
    EXPECT_EQ(portable->SourceFileName(), pdbPath);
}

// A valued form naming a missing file: no debug info (FromFile's null for
// a file that does not exist).
TEST(IlspyCmdProgramTest, TryLoadPDBMissingPdbFileYieldsNoDebugInfo) {
    auto [modulePath, pdbPath] = WriteDiscoveryPair();
    (void)pdbPath;
    MetadataFile file(modulePath);
    ASSERT_TRUE(file.IsValid());
    Cmd::InputPDBFile pdb;
    pdb.IsSet = true;
    pdb.Value = (fs::path(modulePath).parent_path() / "no-such.pdb").string();
    EXPECT_EQ(Cmd::TryLoadPDB(file, pdb), nullptr);
}

// ---- ShowIL: the whole-module -il render ----

// The no-flags render: the `// IL code: tiny` header line plus the
// WriteModuleContents walk, byte-identical to the real ilspycmd `-il`
// output over the same file.
TEST(IlspyCmdProgramTest, ShowILTinyNetmoduleNoFlagsExact) {
    std::string path = ::WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    Cmd::InputPDBFile pdb;
    EXPECT_EQ(Cmd::ShowIL(path, output, false, pdb), 0);
    EXPECT_EQ(output.str(), kGoldTiny);
}

// --il-sequence-points without -usepdb: ShowSequencePoints is set but
// DebugInfo is null -- the C# never discovers the PDB from the sequence
// points flag alone, so no sequence point renders (the render is the
// no-flags one over the same metadata).
TEST(IlspyCmdProgramTest, ShowILSequencePointsWithoutUsePdbRendersNoPoints) {
    auto [modulePath, pdbPath] = WriteDiscoveryPair();
    (void)pdbPath;
    std::ostringstream output;
    Cmd::InputPDBFile pdb;
    EXPECT_EQ(Cmd::ShowIL(modulePath, output, true, pdb), 0);
    EXPECT_EQ(output.str(), kGoldTiny);
}

// --il-sequence-points with the bare -usepdb: the discovered PDB's sequence
// point renders before the first instruction -- byte-identical to the real
// ilspycmd `-il --il-sequence-points -usepdb` output over the same pair.
TEST(IlspyCmdProgramTest, ShowILSequencePointsWithUsePdbExact) {
    auto [modulePath, pdbPath] = WriteDiscoveryPair();
    (void)pdbPath;
    std::ostringstream output;
    Cmd::InputPDBFile pdb;
    pdb.IsSet = true;
    EXPECT_EQ(Cmd::ShowIL(modulePath, output, true, pdb), 0);
    EXPECT_EQ(output.str(), kGoldSequencePoints);
}

// The valued -usepdb <file> form renders the same sequence points through
// the explicitly named PDB (the FromFile provider over the same file).
TEST(IlspyCmdProgramTest, ShowILUsePdbExplicitValueRendersSequencePoints) {
    auto [modulePath, pdbPath] = WriteDiscoveryPair();
    std::ostringstream output;
    Cmd::InputPDBFile pdb;
    pdb.IsSet = true;
    pdb.Value = pdbPath;
    EXPECT_EQ(Cmd::ShowIL(modulePath, output, true, pdb), 0);
    EXPECT_EQ(output.str(), kGoldSequencePoints);
}

// An unparseable file: the C# `new PEFile` throws (the CLI's catch); the
// port's never-throwing reader renders the bare header line and no
// contents (main.cpp gates IsValid before dispatching).
TEST(IlspyCmdProgramTest, ShowILUnparseableFileRendersBareHeader) {
    std::ostringstream output;
    Cmd::InputPDBFile pdb;
    EXPECT_EQ(Cmd::ShowIL("no-such-file-at-all.dll", output, false, pdb), 0);
    EXPECT_EQ(output.str(), "// IL code: \r\n");
}

// ---- MetadataFile.Name: the `// IL code: <name>` source ----

// The Assembly table Name for an assembly, the Module table Name for a
// netmodule, "" for an unparseable file.
TEST(IlspyCmdProgramTest, ModuleNameIsAssemblyNameElseModuleName) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFile mscorlib(path);
    ASSERT_TRUE(mscorlib.IsValid());
    EXPECT_EQ(mscorlib.Name(), "mscorlib");
    std::string tiny = ::WriteTinyNetModule();
    ASSERT_FALSE(tiny.empty());
    MetadataFile netmodule(tiny);
    ASSERT_TRUE(netmodule.IsValid());
    EXPECT_EQ(netmodule.Name(), "tiny");
    MetadataFile bogus("no-such-file-at-all.dll");
    EXPECT_FALSE(bogus.IsValid());
    EXPECT_EQ(bogus.Name(), "");
}

// ---- ListContent: the -l/--list render ----

// The tiny.netmodule fixture (two Class-kind typedefs, <Module> and Tiny):
// the -l c render is the REAL ilspycmd 11.0 output byte for byte -- the
// TypeKind enum name, a space, the GetFullTypeName reflection name, CRLF
// line endings, <Module> included.
TEST(IlspyCmdProgramTest, ListContentTinyNetmoduleExact) {
    std::string tiny = ::WriteTinyNetModule();
    ASSERT_FALSE(tiny.empty());
    {
        std::ostringstream output;
        EXPECT_EQ(Cmd::ListContent(tiny, output, {TypeKind::Class}), 0);
        EXPECT_EQ(output.str(), "Class <Module>\r\nClass Tiny\r\n");
    }
    // A kinds set that selects nothing renders nothing.
    {
        std::ostringstream output;
        EXPECT_EQ(Cmd::ListContent(tiny, output, {TypeKind::Interface}), 0);
        EXPECT_EQ(output.str(), "");
    }
    // Several kinds: the union, in the table order.
    {
        std::ostringstream output;
        EXPECT_EQ(Cmd::ListContent(tiny, output, {TypeKind::Class, TypeKind::Struct}), 0);
        EXPECT_EQ(output.str(), "Class <Module>\r\nClass Tiny\r\n");
    }
    // The empty kinds set: nothing matches, nothing renders.
    {
        std::ostringstream output;
        EXPECT_EQ(Cmd::ListContent(tiny, output, {}), 0);
        EXPECT_EQ(output.str(), "");
    }
}

// The mscorlib -l c render opens with the exact gold prefix (the first 12
// lines of the REAL tool's output -- the TypeDef table order: <Module>,
// the compiler-generated anonymous types, then the Microsoft.Win32
// classes), and carries the nested-type '+' renders.
TEST(IlspyCmdProgramTest, ListContentMscorlibFirstLinesAndNestedRenders) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFile mscorlib(path);
    ASSERT_TRUE(mscorlib.IsValid());
    std::ostringstream output;
    EXPECT_EQ(Cmd::ListContent(path, output, {TypeKind::Class}), 0);
    std::string text = output.str();
    // The exact gold prefix (the real tool's first 12 lines).
    EXPECT_EQ(text.rfind("Class <Module>\r\n"
                          "Class <>f__AnonymousType0`1\r\n"
                          "Class EmptyArray`1\r\n"
                          "Class FXAssembly\r\n"
                          "Class ThisAssembly\r\n"
                          "Class AssemblyRef\r\n"
                          "Class Microsoft.Win32.ASM_CACHE\r\n"
                          "Class Microsoft.Win32.CANOF\r\n"
                          "Class Microsoft.Win32.ASM_NAME\r\n"
                          "Class Microsoft.Win32.Fusion\r\n"
                          "Class Microsoft.Win32.Win32Native\r\n"
                          "Class Microsoft.Win32.OAVariantLib\r\n",
                          0),
        0);
    // The nested types render through the '+' separator at their physical
    // table rows (the GetFullTypeName declaring-chain walk) -- one render
    // per kind, each spot line from its own kind's list.
    {
        std::ostringstream all;
        EXPECT_EQ(Cmd::ListContent(path, all,
            {TypeKind::Class, TypeKind::Struct, TypeKind::Delegate, TypeKind::Enum}),
            0);
        std::string unionText = all.str();
        EXPECT_NE(unionText.find("Class Microsoft.Win32.Win32Native+OSVERSIONINFO\r\n"),
            std::string::npos);
        EXPECT_NE(unionText.find("Delegate Microsoft.Win32.Win32Native+ConsoleCtrlHandlerRoutine\r\n"),
            std::string::npos);
        EXPECT_NE(unionText.find("Struct System.Collections.Generic.Dictionary`2+Enumerator\r\n"),
            std::string::npos);
        EXPECT_NE(
            unionText.find(
                "Enum System.Diagnostics.Tracing.ActivityTracker+ActivityInfo+NumberListCodes\r\n"),
            std::string::npos);
    }
    // The framework base classes are Class (their base type is System.Object,
    // not ValueType -- the IsValueType rule the kind derivation ports).
    EXPECT_NE(text.find("Class System.Object\r\n"), std::string::npos);
    EXPECT_NE(text.find("Class System.Enum\r\n"), std::string::npos);
    EXPECT_NE(text.find("Class System.ValueType\r\n"), std::string::npos);
}

// The mscorlib kind counts -- every line of every kind's list starts with
// the TypeKind enum name, System.Void is NOT a Struct (its Kind is Void,
// outside the -l kinds), and the five lists partition the TypeDef table
// (the counts are the REAL tool's line counts over this fixture:
// 2033/324/415/75/508, plus the one Void-kind row == the 3356 typedefs).
TEST(IlspyCmdProgramTest, ListContentMscorlibKindPartition) {
    std::string path = MscorlibPath();
    if (!fs::exists(path))
        GTEST_SKIP() << "mscorlib fixture not present";
    MetadataFile mscorlib(path);
    ASSERT_TRUE(mscorlib.IsValid());
    struct KindCount {
        TypeKind kind;
        const char* name;
        std::size_t count;
    };
    const KindCount expected[] = {
        {TypeKind::Class, "Class", 2033},
        {TypeKind::Interface, "Interface", 324},
        {TypeKind::Struct, "Struct", 415},
        {TypeKind::Delegate, "Delegate", 75},
        {TypeKind::Enum, "Enum", 508},
    };
    std::size_t total = 0;
    for (const auto& e : expected) {
        std::ostringstream output;
        EXPECT_EQ(Cmd::ListContent(path, output, {e.kind}), 0);
        std::string text = output.str();
        // Line count (every line ends with CRLF).
        std::size_t lines = 0;
        for (std::size_t pos = text.find("\r\n"); pos != std::string::npos;
             pos = text.find("\r\n", pos + 2))
            ++lines;
        EXPECT_EQ(lines, e.count);
        // Every line starts with the kind name and a space.
        std::string prefix = std::string(e.name) + " ";
        for (std::size_t start = 0; start < text.size();) {
            std::size_t end = text.find("\r\n", start);
            ASSERT_NE(end, std::string::npos);
            EXPECT_EQ(text.rfind(prefix, start), start)
                << "line '" << text.substr(start, end - start) << "' in " << e.name;
            start = end + 2;
        }
        total += lines;
    }
    // System.Void is the one non-enumerated kind (its Kind is Void, so -l s
    // does not list it).
    {
        std::ostringstream output;
        EXPECT_EQ(Cmd::ListContent(path, output, {TypeKind::Struct}), 0);
        EXPECT_EQ(output.str().find("System.Void\r\n"), std::string::npos);
    }
    // The five kind lists partition the TypeDef table: the counts plus the
    // one Void-kind row are all 3356 typedefs.
    EXPECT_EQ(total + 1, mscorlib.TypeDefs().size());
}


// ---- The --resource extraction (ExtractResource)

// The resource renders over the value-decode manifest, byte-exact against
// the real ilspycmd tool driven over the same fixture: every value kind's
// ToString() text (no trailing newline -- the C# Write, not WriteLine),
// the byte[] values written raw, and the whole-container blob.
TEST(IlspyCmdProgramTest, ExtractResourceValueMatrix)
{
    std::string path = ILSpy::Tests::WriteValTestDll();
    ASSERT_FALSE(path.empty());

    // The text renders (the invariant-culture ToString forms).
    struct TextCase {
        const char* resource;
        const char* expected;
    };
    const TextCase textCases[] = {
        {"v2.resources/Str", "one"},
        {"v2.resources/Int", "42"},
        {"v2.resources/Bool", "True"},
        {"v2.resources/BoolF", "False"},
        {"v2.resources/Char", "\xE4\xB8\xAD"},
        {"v2.resources/Byte", "171"},
        {"v2.resources/SByte", "-12"},
        {"v2.resources/Short", "-1234"},
        {"v2.resources/UShort", "1234"},
        {"v2.resources/UInt", "4275878552"},
        {"v2.resources/Long", "-1234567890123456789"},
        {"v2.resources/ULong", "18364758544493064720"},
        {"v2.resources/Float", "1.5"},
        {"v2.resources/FloatSci", "1E+16"},
        {"v2.resources/FloatNegNaN", "NaN"},
        {"v2.resources/FloatPosInf", "Infinity"},
        {"v2.resources/Double", "2.25"},
        {"v2.resources/DoubleSci", "10000000000000000"},
        {"v2.resources/DoubleSmall", "0.0001"},
        {"v2.resources/DoubleExp", "1E+17"},
        {"v2.resources/DoubleNeg", "-0.5"},
        {"v2.resources/Decimal", "12345.6789"},
        {"v1.resources/V1Str", "hello v1"},
        {"v1.resources/V1Int", "-77"},
        {"v1.resources/V1Long", "1234605616436508552"},
        {"v1.resources/V1Double", "2.25"},
        {"v1.resources/V1Decimal", "12345.6789"},
        {"v1.resources/V1Date", "02/15/2024 10:30:45"},
        {"v1.resources/V1TS", "1.02:03:04"},
        // The null value: the empty text (value?.ToString() ?? "").
        {"v1.resources/V1Null", ""},
        // The empty byte array: no bytes.
        {"v2.resources/EmptyBytes", ""},
        // The zero-length serialized region: no bytes.
        {"v1.resources/V1UserEmpty", ""},
        {"serfmt.resources/SerUserEmpty", ""},
    };
    for (const auto& c : textCases) {
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, c.resource, output, errorOutput), 0)
            << c.resource;
        EXPECT_EQ(output.str(), c.expected) << c.resource;
        EXPECT_EQ(errorOutput.str(), "") << c.resource;
    }

    // The case-insensitive whole-path fold (the real tool's lookup).
    {
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, "V2.RESOURCES/STR", output, errorOutput), 0);
        EXPECT_EQ(output.str(), "one");
    }
}

// The byte renders: the byte[] values (the entry values reduced to bytes
// and the whole-resource blobs) written raw into the output block.
TEST(IlspyCmdProgramTest, ExtractResourceByteMatrix)
{
    std::string path = ILSpy::Tests::WriteValTestDll();
    ASSERT_FALSE(path.empty());
    MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());

    struct ByteCase {
        const char* resource;
        std::vector<std::uint8_t> expected;
    };
    const ByteCase byteCases[] = {
        {"v2.resources/Bytes", {0x01, 0x02, 0x03}},
        {"v2.resources/Stream", {0x07, 0x08, 0x09}},
        {"v1.resources/V1UserMid", {0xAA, 0xBB, 0x07}},
        {"v1.resources/V1User", {0xCC, 0xDD, 0xEE}},
        {"serfmt.resources/SerUserMid", {0x99, 0x88, 0x77}},
        {"serfmt.resources/SerUserLast", {0x55, 0x66}},
        {"bad.resources", {0xDE, 0xAD, 0xBE, 0xEF}},
        {"plain.nlp", {0x11, 0x22, 0x33}},
    };
    for (const auto& c : byteCases) {
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, c.resource, output, errorOutput), 0)
            << c.resource;
        std::string text = output.str();
        ASSERT_EQ(text.size(), c.expected.size()) << c.resource;
        for (std::size_t i = 0; i < c.expected.size(); i++)
            EXPECT_EQ(static_cast<std::uint8_t>(text[i]), c.expected[i])
                << c.resource << " byte " << i;
    }

    // The whole-container blob: the row's bytes after the length prefix
    // (the real tool's raw byte-for-byte output over the same row).
    {
        auto data = ILSpy::Tests::ValTestResourceData(module, "v2.resources");
        ASSERT_TRUE(data.has_value());
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, "v2.resources", output, errorOutput), 0);
        ASSERT_EQ(output.str().size(), data->size());
        EXPECT_EQ(0, std::memcmp(output.str().data(), data->data(), data->size()));
    }
}

// The not-found arm: the two stderr lines plus the available-resources
// listing (the EnumerateResourcePaths order, the two-space indent, the
// CRLF TextWriter convention), and EX_DATAERR (65).
TEST(IlspyCmdProgramTest, ExtractResourceNotFound)
{
    std::string path = ILSpy::Tests::WriteValTestDll();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_EQ(Cmd::ExtractResource(path, "nope", output, errorOutput), 65);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(errorOutput.str(),
        "Resource 'nope' not found.\r\n"
        "Available resources:\r\n"
        "  v2.resources/Double\r\n"
        "  v2.resources/DoubleSci\r\n"
        "  v2.resources/DoubleExp\r\n"
        "  v2.resources/DoubleNeg\r\n"
        "  v2.resources/UShort\r\n"
        "  v2.resources/Stream\r\n"
        "  v2.resources/Decimal\r\n"
        "  v2.resources/FloatPosInf\r\n"
        "  v2.resources/Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87\r\n"
        "  v2.resources/Int\r\n"
        "  v2.resources/Str\r\n"
        "  v2.resources/Float\r\n"
        "  v2.resources/BoolF\r\n"
        "  v2.resources/Bytes\r\n"
        "  v2.resources/ULong\r\n"
        "  v2.resources/Short\r\n"
        "  v2.resources/SByte\r\n"
        "  v2.resources/EmptyBytes\r\n"
        "  v2.resources/DoubleSmall\r\n"
        "  v2.resources/FloatNegNaN\r\n"
        "  v2.resources/FloatSci\r\n"
        "  v2.resources/Bool\r\n"
        "  v2.resources/Byte\r\n"
        "  v2.resources/Char\r\n"
        "  v2.resources/Long\r\n"
        "  v2.resources/UInt\r\n"
        "  v1.resources/V1Str\r\n"
        "  v1.resources/V1Int\r\n"
        "  v1.resources/V1Long\r\n"
        "  v1.resources/V1Double\r\n"
        "  v1.resources/V1Decimal\r\n"
        "  v1.resources/V1Date\r\n"
        "  v1.resources/V1TS\r\n"
        "  v1.resources/V1Null\r\n"
        "  v1.resources/V1UserMid\r\n"
        "  v1.resources/V1UserEmpty\r\n"
        "  v1.resources/V1User\r\n"
        "  serfmt.resources/SerUserMid\r\n"
        "  serfmt.resources/SerStr\r\n"
        "  serfmt.resources/SerUserEmpty\r\n"
        "  serfmt.resources/SerUserLast\r\n"
        "  bad.resources\r\n"
        "  plain.nlp\r\n"
        "  page.baml\r\n");
}

// The .baml arm: the landed BamlDecompiler. Over the BAML-resource fixture
// the real tool renders the decompiled XAML to stdout with no trailing
// newline, and over the valtest fixture's garbage .baml blob the
// BamlReader's signature-length rejection propagates out of ExtractResource
// to the global catch (the real tool's InvalidDataException -> EX_SOFTWARE
// 70 with the stack trace the port omits).
TEST(IlspyCmdProgramTest, ExtractResourceBamlToolBar)
{
    std::string path = ILSpy::Tests::WriteBamlResDll();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_EQ(Cmd::ExtractResource(path, "page.xaml.baml", output, errorOutput), 0);
    EXPECT_EQ(output.str(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
    EXPECT_EQ(errorOutput.str(), "");
}

TEST(IlspyCmdProgramTest, ExtractResourceBamlXClassRename)
{
    std::string path = ILSpy::Tests::WriteBamlResDll();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_EQ(Cmd::ExtractResource(path, "page2.xaml.baml", output, errorOutput), 0);
    EXPECT_EQ(output.str(),
        "<String xmlns=\"http://probe.pi/ns\">hello</String>");
    EXPECT_EQ(errorOutput.str(), "");
}

TEST(IlspyCmdProgramTest, ExtractResourceBamlGarbagePropagates)
{
    std::string path = ILSpy::Tests::WriteValTestDll();
    ASSERT_FALSE(path.empty());
    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_THROW(
        (void)Cmd::ExtractResource(path, "page.baml", output, errorOutput),
        std::exception);
    // A .baml-suffixed path that is not found takes the not-found arm (the
    // isBaml gate only applies to a found byte[] value).
    {
        std::ostringstream output2;
        std::ostringstream error2;
        EXPECT_EQ(Cmd::ExtractResource(path, "v2.resources/Str.baml", output2, error2), 65);
    }
}

// The real mscorlib resources: a container string entry, byte-exact
// against the real tool over the same row.
TEST(IlspyCmdProgramTest, ExtractResourceMscorlib)
{
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    {
        auto value = Cmd::TryGetResource(
            mscorlib, "mscorlib.resources/Interop.COM_TypeMismatch");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->str, "Type mismatch between source and destination types.");
    }
    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_EQ(Cmd::ExtractResource(MscorlibPath(),
        "mscorlib.resources/Interop.COM_TypeMismatch", output, errorOutput), 0);
    EXPECT_EQ(output.str(), "Type mismatch between source and destination types.");
}

// The -o outputDirectory branches of the --resource extraction (the
// C# `string fileName = WholeProjectDecompiler.SanitizeFileName(
// Path.GetFileName(resourceName)); File.WriteAllBytes/WriteText(
// Path.Combine(outputDirectory, fileName), ...)`): the sanitized name
// under the output directory (the container prefix stripped by
// GetFileName), the bytes verbatim for byte[] values, the text verbatim
// for the others, and NOTHING to stdout.
TEST(IlspyCmdProgramTest, ExtractResourceOutputDirectoryWritesFiles)
{
    std::string path = ILSpy::Tests::WriteValTestDll();
    ASSERT_FALSE(path.empty());
    fs::path dir = TempDir("resource-o");
    std::optional<std::string> outputDirectory = dir.string();

    struct FileCase {
        const char* resource;
        std::vector<std::uint8_t> expected;
    };
    const FileCase byteCases[] = {
        {"v2.resources/Bytes", {0x01, 0x02, 0x03}},
        {"bad.resources", {0xDE, 0xAD, 0xBE, 0xEF}},
        {"plain.nlp", {0x11, 0x22, 0x33}},
    };
    for (const auto& c : byteCases) {
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, c.resource, output,
            errorOutput, outputDirectory), 0) << c.resource;
        EXPECT_EQ(output.str(), "") << c.resource;
        EXPECT_EQ(errorOutput.str(), "") << c.resource;
        // The container prefix is stripped (GetFileName), the plain name
        // sanitizes to itself.
        std::string name = fs::path(c.resource).filename().string();
        fs::path file = dir / name;
        ASSERT_TRUE(fs::exists(file)) << c.resource;
        std::string read = ReadFileBytes(file);
        ASSERT_EQ(read.size(), c.expected.size()) << c.resource;
        for (std::size_t i = 0; i < c.expected.size(); i++)
            EXPECT_EQ(static_cast<std::uint8_t>(read[i]), c.expected[i])
                << c.resource << " byte " << i;
    }

    // The text value: the file holds the rendered text (File.WriteAllText
    // -- UTF-8 without a BOM) and stdout stays empty.
    {
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, "v2.resources/Str", output,
            errorOutput, outputDirectory), 0);
        EXPECT_EQ(output.str(), "");
        EXPECT_EQ(ReadFileBytes(dir / "Str"), "one");
    }

    // Without an outputDirectory the value goes to stdout again (the
    // default parameter -- the pre-existing behavior).
    {
        std::ostringstream output;
        std::ostringstream errorOutput;
        EXPECT_EQ(Cmd::ExtractResource(path, "v2.resources/Str", output,
            errorOutput), 0);
        EXPECT_EQ(output.str(), "one");
    }
}

// The non-ASCII extraction file name (the res-test manifest's CJK-named
// container entry): the letters survive the sanitizer (the file opens at
// the Unicode name through the native path), the container prefix
// stripped, the text verbatim.
TEST(IlspyCmdProgramTest, ExtractResourceOutputDirectoryNonAsciiName)
{
    std::string path = ILSpy::Tests::WriteResTestDll();
    ASSERT_FALSE(path.empty());
    fs::path dir = TempDir("resource-o-nonascii");
    std::optional<std::string> outputDirectory = dir.string();

    // "test.resources/Unicode.Name.\u4E2D\u6587" (UTF-8 bytes).
    std::string resource =
        "test.resources/Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87";
    std::string fileName =
        "Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87";
    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_EQ(Cmd::ExtractResource(path, resource, output, errorOutput,
        outputDirectory), 0);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(ReadFileBytes(dir / Cmd::ToNativePath(fileName)),
        "unicode value");
}

// The BAML arm's -o branch: the C# `string xamlFile =
// WholeProjectDecompiler.SanitizeFileName(Path.GetFileNameWithoutExtension(
// resourceName) + ".xaml")` -- 'page.xaml.baml' loses its extension
// ('page.xaml') and gains '.xaml' back, so the file is 'page.xaml.xaml' --
// holding the XDocument.Save render (the UTF-8 BOM, the declaration, the
// CRLF break, and the indented content), with nothing to stdout. The
// garbage-blob arm propagates with -o set exactly as it does without.
TEST(IlspyCmdProgramTest, ExtractResourceBamlOutputDirectorySavesXaml)
{
    std::string path = ILSpy::Tests::WriteBamlResDll();
    ASSERT_FALSE(path.empty());
    fs::path dir = TempDir("resource-o-baml");
    std::optional<std::string> outputDirectory = dir.string();

    std::ostringstream output;
    std::ostringstream errorOutput;
    EXPECT_EQ(Cmd::ExtractResource(path, "page.xaml.baml", output,
        errorOutput, outputDirectory), 0);
    EXPECT_EQ(output.str(), "");
    EXPECT_EQ(errorOutput.str(), "");
    // The XDocument.Save bytes: the BOM, the declaration, the CRLF, the
    // element, no trailing newline (94 bytes over the real fixture).
    const std::string expectedFile =
        "\xEF\xBB\xBF"
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>";
    EXPECT_EQ(ReadFileBytes(dir / "page.xaml.xaml"), expectedFile);

    // The garbage blob still propagates with -o set (no file created).
    std::string valPath = ILSpy::Tests::WriteValTestDll();
    ASSERT_FALSE(valPath.empty());
    fs::path dir2 = TempDir("resource-o-baml-bad");
    std::ostringstream output2;
    std::ostringstream errorOutput2;
    EXPECT_THROW(
        (void)Cmd::ExtractResource(valPath, "page.baml", output2,
            errorOutput2, dir2.string()),
        std::exception);
    EXPECT_FALSE(fs::exists(dir2 / "page.xaml.xaml"));
}

// ---- the -o writer branches: OutputFilePath + WriteOutputFile ----

// The `Path.GetFileNameWithoutExtension(fileName)` + `Path.Combine(
// outputDirectory, outputName) + extension` composition (the C#
// IlspyCmdProgram.cs PerformPerFileAction -o branches) over the .NET
// edge cases probed directly: the file-name component after the last
// separator (both separators strip), minus everything from its LAST dot
// (a trailing dot drops the dot, a leading dot yields the empty name,
// inner dots stay), and Path.Combine's separator rules (a trailing
// separator or a volume separator joins without inserting another).
TEST(IlspyCmdProgramTest, OutputFilePathComposition)
{
#if defined(_WIN32)
    constexpr const char* kSep = "\\";
#else
    constexpr const char* kSep = "/";
#endif
    std::string dir = (TempDir("outputpath") / "out").string();
    EXPECT_EQ(Cmd::OutputFilePath(dir, "mscorlib.dll", ".list.txt"),
        dir + kSep + "mscorlib.list.txt");
    // No extension in the input name.
    EXPECT_EQ(Cmd::OutputFilePath(dir, "tiny", ".il"),
        dir + kSep + "tiny.il");
    // Inner dots stay; only the last dot's tail is dropped.
    EXPECT_EQ(Cmd::OutputFilePath(dir, "a.b.c.dll", ".il"),
        dir + kSep + "a.b.c.il");
    // A trailing dot IS the extension separator ("foo." -> "foo").
    EXPECT_EQ(Cmd::OutputFilePath(dir, "foo.", ".il"),
        dir + kSep + "foo.il");
    // A leading dot is the LAST dot too (".dll" -> "") -- and an empty
    // name makes Path.Combine return the directory itself, so the
    // extension lands directly on it.
    EXPECT_EQ(Cmd::OutputFilePath(dir, ".dll", ".list.txt"),
        dir + ".list.txt");
    // The directory parts of the input path are stripped (both
    // separators, the input as given).
    EXPECT_EQ(Cmd::OutputFilePath(dir,
        "C:\\build\\dir\\tiny.netmodule", ".list.txt"),
        dir + kSep + "tiny.list.txt");
    EXPECT_EQ(Cmd::OutputFilePath(dir, "../../out/tiny.exe", ".resources.txt"),
        dir + kSep + "tiny.resources.txt");
    // A directory already ending in a separator joins without another.
    EXPECT_EQ(Cmd::OutputFilePath(dir + kSep, "mscorlib.dll", ".il"),
        dir + kSep + "mscorlib.il");
    // A bare drive is not a terminating separator (Path.Combine("C:",
    // "x") is "C:\\x" -- .NET inserts the separator).
    EXPECT_EQ(Cmd::OutputFilePath("C:", "mscorlib.dll", ".il"),
        std::string("C:") + kSep + "mscorlib.il");
}

// File.CreateText + the finally output.Close(): the file receives the
// rendered bytes verbatim -- UTF-8 WITHOUT a BOM (the first byte is the
// content's own), the CRLF text intact, and an existing file is
// truncated by the create (File.CreateText truncates).
TEST(IlspyCmdProgramTest, WriteOutputFileWritesBytesVerbatim)
{
    fs::path dir = TempDir("writeoutput");
    fs::path file = dir / "tiny.list.txt";
    // Pre-existing longer content: the create must truncate it.
    {
        std::ofstream f(file, std::ios::binary | std::ios::trunc);
        f << "OLD CONTENT THAT MUST GO AWAY - LONGER THAN THE NEW WRITE";
    }
    Cmd::WriteOutputFile(file.string(), "Class <Module>\r\nClass Tiny\r\n");
    std::string read = ReadFileBytes(file);
    EXPECT_EQ(read, "Class <Module>\r\nClass Tiny\r\n");
    EXPECT_EQ(read.size(), 28);  // no BOM, no added newline
    // A second write truncates again (CreateText, not AppendText).
    Cmd::WriteOutputFile(file.string(), "x");
    EXPECT_EQ(ReadFileBytes(file), "x");
}

// The File.CreateText failure arm: a path whose directory does not exist
// throws (the C# IOException escaping to the OnExecuteAsync global catch
// with EX_SOFTWARE; the port renders the message, no stack trace).
TEST(IlspyCmdProgramTest, WriteOutputFileMissingDirectoryThrows)
{
    fs::path dir = TempDir("writefail");
    fs::path nowhere = dir / "no" / "such" / "dir" / "tiny.il";
    EXPECT_THROW(
        Cmd::WriteOutputFile(nowhere.string(), "data"),
        std::runtime_error);
}

// The whole -l -o flow over the tiny.netmodule fixture: the render into
// the buffer, the output-file path composed from the input path, the
// write, and the read-back equals the byte-exact gold render (the same
// bytes the real tool writes to <dir>\tiny.list.txt).
TEST(IlspyCmdProgramTest, ListContentOutputFileRoundTrip)
{
    std::string tiny = ::WriteTinyNetModule();
    ASSERT_FALSE(tiny.empty());
    std::ostringstream output;
    EXPECT_EQ(Cmd::ListContent(tiny, output, {TypeKind::Class}), 0);
    fs::path dir = TempDir("roundtrip");
    std::string path = Cmd::OutputFilePath(dir.string(), tiny, ".list.txt");
    Cmd::WriteOutputFile(path, output.str());
    // The composed name takes the fixture's base name
    // (ilspy_tiny_test.netmodule).
    EXPECT_EQ(fs::path(path).filename().string(), "ilspy_tiny_test.list.txt");
    EXPECT_EQ(ReadFileBytes(path), "Class <Module>\r\nClass Tiny\r\n");
}

// A non-ASCII -o value: ResolveOutputDirectory must keep the UTF-8
// spelling -- fs::path::string() transcodes through the ANSI code page,
// which mangles the name on some systems and THROWS "No mapping for the
// Unicode character exists in the target multi-byte code page" when the
// page cannot represent it (the crash this pins the fix for). The C#
// composes Unicode paths end to end through System.IO, so the resolved
// value converted back to the native form is exactly the direct
// composition.
TEST(IlspyCmdProgramTest, ResolveOutputDirectoryPreservesNonAsciiNames)
{
    // "中文输出" as UTF-8 bytes.
    std::string name = "\xe4\xb8\xad\xe6\x96\x87\xe8\xbe\x93\xe5\x87\xba";
    auto resolved = Cmd::ResolveOutputDirectory(name);
    ASSERT_TRUE(resolved.has_value());
    fs::path native = Cmd::ToNativePath(*resolved);
    fs::path expected = (fs::current_path() / Cmd::ToNativePath(name))
                            .lexically_normal();
    EXPECT_EQ(native, expected);
}

// The output-file write into a non-ASCII output directory: the composed
// UTF-8 path opens the file at the correct (Unicode) name, the bytes
// verbatim.
TEST(IlspyCmdProgramTest, WriteOutputFileHandlesNonAsciiDirectory)
{
    std::string name = "\xe4\xb8\xad\xe6\x96\x87\xe8\xbe\x93\xe5\x87\xba";  // "中文输出"
    fs::path base = TempDir("nonascii");
    fs::path nonAscii = base / Cmd::ToNativePath(name);
    std::error_code ec;
    fs::create_directories(nonAscii, ec);
    ASSERT_FALSE(ec);
#if defined(_WIN32)
    const std::string sep = "\\";
#else
    const std::string sep = "/";
#endif
    std::string utf8Dir = base.string() + sep + name;
    std::string path = Cmd::OutputFilePath(utf8Dir, "tiny.netmodule", ".list.txt");
    Cmd::WriteOutputFile(path, "data\r\n");
    EXPECT_EQ(ReadFileBytes(nonAscii / "tiny.list.txt"), "data\r\n");
}

// ---------------------------------------------------------------------------
// The CLI load-failure classification (ClassifyCliOpenFailure): the C#
// IlspyCmdProgram arms -- the missing-path validation pair, the non-PE
// BadImageFormatException line, and the valid-PE-without-metadata
// MetadataFileNotSupportedException line, both exception arms with the
// EX_SOFTWARE exit code. The sweep's T11 rows (kernel32-64.dll_ and the
// malformed capa samples) pinned the C# first-line texts.
// ---------------------------------------------------------------------------

TEST(IlspyCmdProgramTest, CliOpenFailureMissingFileArm)
{
    fs::path missing = TempDir("cliopen") / "does_not_exist.dll";
    Cmd::CliOpenFailure failure = Cmd::ClassifyCliOpenFailure(missing.string());
    EXPECT_EQ(failure.errorLine,
        "File '" + missing.string() + "' does not exist!");
    EXPECT_EQ(failure.stdoutLine,
        "Specify --help for a list of available options and commands.");
    EXPECT_EQ(failure.exitCode, 1);
}

TEST(IlspyCmdProgramTest, CliOpenFailureNotAPeFileArm)
{
    fs::path path = TempDir("cliopen") / "not_a_pe.txt";
    {
        std::ofstream out(path, std::ios::binary);
        out << "hello!";
    }
    Cmd::CliOpenFailure failure = Cmd::ClassifyCliOpenFailure(path.string());
    // The SRM message for a six-byte file: "Image is too small.".
    EXPECT_EQ(failure.errorLine,
        "System.BadImageFormatException: Image is too small.");
    EXPECT_EQ(failure.stdoutLine, "");
    EXPECT_EQ(failure.exitCode, 70);  // ProgramExitCodes.EX_SOFTWARE
}

TEST(IlspyCmdProgramTest, CliOpenFailurePeWithoutManagedMetadataArm)
{
    // The tiny.netmodule with its COM-descriptor data-directory entry
    // (optional-header data directory 14) zeroed: a valid PE image without
    // managed metadata -- the shape the sweep's native-PE samples hit.
    std::string bytes = TinyNetModuleBytes();
    std::uint32_t peOffset = ILSpy::Tests::Rd32(bytes, 0x3C);
    std::size_t dataDirs = peOffset + 4 + 20 + 96;  // PE32: optional header
    ILSpy::Tests::Wr32(bytes, dataDirs + 14 * 8, 0);         // the COM dir RVA
    ILSpy::Tests::Wr32(bytes, dataDirs + 14 * 8 + 4, 0);     // and size
    fs::path path = TempDir("cliopen") / "no_metadata.dll";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good());
    }
    Cmd::CliOpenFailure failure = Cmd::ClassifyCliOpenFailure(path.string());
    EXPECT_EQ(failure.errorLine,
        "ICSharpCode.Decompiler.Metadata.MetadataFileNotSupportedException"
        ": PE file does not contain any managed metadata.");
    EXPECT_EQ(failure.stdoutLine, "");
    EXPECT_EQ(failure.exitCode, 70);
}

// The -o writer branch for the decompile path (IlspyCmdProgram.cs lines
// 406-411): `Path.Combine(outputDirectory, (string.IsNullOrEmpty(TypeName)
// ? outputName : TypeName)) + ".decompiled.cs"` -- the assembly's base
// name without -t, or the TYPE NAME VERBATIM with -t (the namespace dots
// are part of the name, not an extension).
TEST(IlspyCmdProgramTest, DecompiledOutputFilePathComposition)
{
#if defined(_WIN32)
    constexpr const char* kSep = "\\";
#else
    constexpr const char* kSep = "/";
#endif
    std::string dir = (TempDir("decompiledpath") / "out").string();
    // No -t: the assembly's base name.
    EXPECT_EQ(Cmd::DecompiledOutputFilePath(dir, "mscorlib.dll", ""),
        dir + kSep + "mscorlib.decompiled.cs");
    // With -t: the type name verbatim.
    EXPECT_EQ(Cmd::DecompiledOutputFilePath(dir, "mscorlib.dll", "MyApp.Page1"),
        dir + kSep + "MyApp.Page1.decompiled.cs");
    // The directory parts of the input path are stripped (the
    // FileNameWithoutExtensionOf convention).
    EXPECT_EQ(Cmd::DecompiledOutputFilePath(dir, "../../out/tiny.exe", ""),
        dir + kSep + "tiny.decompiled.cs");
}
