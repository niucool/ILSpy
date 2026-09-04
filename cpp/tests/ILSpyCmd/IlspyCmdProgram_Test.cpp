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
#include "ILSpyX/PdbProvider/PortableDebugInfoProvider.hpp"
#include "TestFixtures/DiscoveryNetModule.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using ILSpy::Decompiler::Metadata::MetadataFile;
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
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
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
