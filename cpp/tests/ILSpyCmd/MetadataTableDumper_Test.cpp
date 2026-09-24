// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// The --dump-table MetadataTableDumper: the table-name parse (names,
// decimal and 0x-prefixed hex numbers over the 39-table supported set),
// the raw Cor-table row surface it reads through, the aligned console
// table, and the JSON document. Every expected console/JSON block below is
// pinned byte-identical against the REAL ilspycmd 11.0 --dump-table output
// over the identical fixtures (CRLF line endings included), and the
// implementation was additionally swept end to end: all 39 tables, both
// output formats, over mscorlib 4.8, System.dll 4.8, the .NET 10
// System.Private.CoreLib, and tiny.netmodule produced byte-identical
// output to the real tool (the iteration-level verification; the suite
// here pins the shapes that keep that parity from regressing).

#include "ILSpyCmd/MetadataTableDumper.hpp"
#include <cstdlib>
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

using ILSpy::Decompiler::Metadata::CorTableIndex;
using ILSpy::Decompiler::Metadata::MetadataFile;
namespace Cmd = ILSpy::ILSpyCmd;

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::string Dump(const std::string& path, CorTableIndex table, bool asJson) {
    std::ostringstream buffer;
    Cmd::DumpTable(path, buffer, table, asJson);
    return buffer.str();
}

CorTableIndex ParseOrDie(const std::string& name) {
    CorTableIndex table = CorTableIndex::Module;
    if (!Cmd::TryParseTableName(name, table))
        ADD_FAILURE() << "expected '" << name << "' to parse";
    return table;
}

} // namespace

TEST(MetadataTableDumperTest, TryParseTableNameNamesAndNumbers) {
    // The ECMA-335 names, case-insensitive.
    EXPECT_EQ(ParseOrDie("Module"), CorTableIndex::Module);
    EXPECT_EQ(ParseOrDie("module"), CorTableIndex::Module);
    EXPECT_EQ(ParseOrDie("MODULE"), CorTableIndex::Module);
    EXPECT_EQ(ParseOrDie("TypeDef"), CorTableIndex::TypeDef);
    EXPECT_EQ(ParseOrDie("typedef"), CorTableIndex::TypeDef);
    EXPECT_EQ(ParseOrDie("GenericParamConstraint"), CorTableIndex::GenericParamConstraint);
    EXPECT_EQ(ParseOrDie("methodsemantics"), CorTableIndex::MethodSemantics);
    // The Enum.TryParse whitespace trims.
    EXPECT_EQ(ParseOrDie("  TypeDef "), CorTableIndex::TypeDef);
    // Decimal and 0x-prefixed hex numbers (both case forms of the prefix):
    // the decimal number is the table id's numeric value, the 0x form hex.
    EXPECT_EQ(ParseOrDie("17"), CorTableIndex::StandAloneSig);  // 17 = 0x11
    EXPECT_EQ(ParseOrDie("0x17"), CorTableIndex::Property);    // 0x17 = 23
    EXPECT_EQ(ParseOrDie("42"), CorTableIndex::GenericParam);   // 42 = 0x2A
    EXPECT_EQ(ParseOrDie("0x2A"), CorTableIndex::GenericParam);
    EXPECT_EQ(ParseOrDie("44"), CorTableIndex::GenericParamConstraint);  // 44 = 0x2C
    EXPECT_EQ(ParseOrDie("0"), CorTableIndex::Module);  // 0 = 0x00
    // Unknown names and numbers outside the supported set.
    CorTableIndex table = CorTableIndex::Module;
    EXPECT_FALSE(Cmd::TryParseTableName("Bogus", table));
    EXPECT_FALSE(Cmd::TryParseTableName("AssemblyProcessor", table));  // modeled but unsupported
    EXPECT_FALSE(Cmd::TryParseTableName("99", table));
    EXPECT_FALSE(Cmd::TryParseTableName("30", table));  // 0x1E unused
    EXPECT_FALSE(Cmd::TryParseTableName("", table));
    EXPECT_FALSE(Cmd::TryParseTableName("-1", table));
}

TEST(MetadataTableDumperTest, SupportedTableNamesExact) {
    // The C# supportedTables join: every entry in declaration order with
    // its 2-digit hex id (pinned against the real tool's usage error).
    EXPECT_EQ(Cmd::SupportedTableNames(),
        "Module (0x00), TypeRef (0x01), TypeDef (0x02), FieldPtr (0x03), Field (0x04), "
        "MethodPtr (0x05), MethodDef (0x06), ParamPtr (0x07), Param (0x08), "
        "InterfaceImpl (0x09), MemberRef (0x0A), Constant (0x0B), CustomAttribute (0x0C), "
        "FieldMarshal (0x0D), DeclSecurity (0x0E), ClassLayout (0x0F), FieldLayout (0x10), "
        "StandAloneSig (0x11), EventMap (0x12), EventPtr (0x13), Event (0x14), "
        "PropertyMap (0x15), PropertyPtr (0x16), Property (0x17), MethodSemantics (0x18), "
        "MethodImpl (0x19), ModuleRef (0x1A), TypeSpec (0x1B), ImplMap (0x1C), "
        "FieldRva (0x1D), Assembly (0x20), AssemblyRef (0x23), File (0x26), "
        "ExportedType (0x27), ManifestResource (0x28), NestedClass (0x29), "
        "GenericParam (0x2A), MethodSpec (0x2B), GenericParamConstraint (0x2C)");
    EXPECT_EQ(Cmd::SupportedTables().size(), 39u);
}

TEST(MetadataTableDumperTest, CorTableRowSurface) {
    MetadataFile file(MscorlibPath());
    ASSERT_TRUE(file.IsValid());
    // The row counts the SRM GetTableRowCount reads.
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::Module), 1u);
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::TypeRef), 0u);  // mscorlib is self-contained
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::TypeDef), 3356u);
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::Assembly), 1u);
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::AssemblyRef), 0u);
    EXPECT_GT(file.CorTableRowCount(CorTableIndex::CustomAttribute), 20000u);
    // The *Ptr tables have no winmd model: 0 rows for every port-openable file.
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::FieldPtr), 0u);
    EXPECT_EQ(file.CorTableRowCount(CorTableIndex::ParamPtr), 0u);
    // Raw column reads: the winmd declaration-order indexes.
    // Module row 1: Generation 0, the Name/Mvid string and #GUID indexes.
    EXPECT_EQ(file.CorTableColumnValue(CorTableIndex::Module, 0, 0), 0u);
    EXPECT_EQ(file.CorString(file.CorTableColumnValue(CorTableIndex::Module, 0, 1)),
        "CommonLanguageRuntimeLibrary");
    auto mvid = file.CorTryGuid(file.CorTableColumnValue(CorTableIndex::Module, 0, 2));
    ASSERT_TRUE(mvid);
    // The raw #GUID bytes (the canonical little-endian binary form) of
    // mscorlib's MVID cfbe3cd1-8651-4c71-ae57-6a8663cf2300.
    const std::array<std::uint8_t, 16> expectedMvid = {
        0xD1, 0x3C, 0xBE, 0xCF, 0x51, 0x86, 0x71, 0x4C,
        0xAE, 0x57, 0x6A, 0x86, 0x63, 0xCF, 0x23, 0x00,
    };
    EXPECT_EQ(*mvid, expectedMvid);
    // The nil #GUID/`#Strings` indexes.
    EXPECT_EQ(file.CorString(0), "");
    EXPECT_FALSE(file.CorTryGuid(0).has_value());
    // The Assembly version column (the four UInt16 fields winmd merges).
    auto version = file.CorTableVersionValue(CorTableIndex::Assembly, 0);
    EXPECT_EQ(version.MajorVersion, 4u);
    EXPECT_EQ(version.MinorVersion, 0u);
    EXPECT_EQ(version.BuildNumber, 0u);
    EXPECT_EQ(version.RevisionNumber, 0u);
    // The TypeDef attributes/name of the <Module> row.
    EXPECT_EQ(file.CorTableColumnValue(CorTableIndex::TypeDef, 0, 0), 0u);
    EXPECT_EQ(file.CorString(file.CorTableColumnValue(CorTableIndex::TypeDef, 0, 1)),
        "<Module>");
    // An out-of-range row throws (the C# BadImageFormatException arm).
    EXPECT_THROW(file.CorTableColumnValue(CorTableIndex::Module, 1, 0),
        std::invalid_argument);
}

TEST(MetadataTableDumperTest, ConsoleTableTinyNetModule) {
    std::string path = WriteTinyNetModule();
    // The Module row: the aligned columns, the dashes line, the nil GUID
    // spellings (pinned against the real tool).
    EXPECT_EQ(Dump(path, CorTableIndex::Module, false),
        "RID  Token       Generation  Name  Mvid                                  "
        "GenerationId  BaseGenerationId\r\n"
        "---  ----------  ----------  ----  ------------------------------------  "
        "------------  ----------------\r\n"
        "1    0x00000001  0           tiny  163cefd4-809a-4644-82fd-23d0eb508d60  "
        "nil           nil\r\n");
    // The TypeDef rows: the TypeAttributes flags render ("NotPublic" for
    // the raw 0 -- the first declared zero-valued member), the running
    // FieldList/MethodList positions, the nil/TypeDefOrRef BaseType.
    EXPECT_EQ(Dump(path, CorTableIndex::TypeDef, false),
        "RID  Token       Attributes  Name      Namespace  BaseType    FieldList  MethodList\r\n"
        "---  ----------  ----------  --------  ---------  ----------  ---------  ----------\r\n"
        "1    0x02000001  NotPublic   <Module>             nil         1          1\r\n"
        "2    0x02000002  Public      Tiny                 0x01000001  1          1\r\n");
    // The MethodDef row: the RVA hex, the plain-enum ImplAttributes ("IL"),
    // the flags union, the blob heap offsets.
    EXPECT_EQ(Dump(path, CorTableIndex::MethodDef, false),
        "RID  Token       RVA         ImplAttributes  Attributes                 "
        "Name  Signature   ParamList\r\n"
        "---  ----------  ----------  --------------  -------------------------  "
        "----  ----------  ---------\r\n"
        "1    0x06000001  0x00002050  IL              Public, Static, HideBySig  "
        "Add   0x0000000A  1\r\n");
    // The Param rows: ParameterAttributes "None" for the raw 0.
    EXPECT_EQ(Dump(path, CorTableIndex::Param, false),
        "RID  Token       Attributes  SequenceNumber  Name\r\n"
        "---  ----------  ----------  --------------  ----\r\n"
        "1    0x08000001  None        1               a\r\n"
        "2    0x08000002  None        2               b\r\n");
    // An empty table prints "0 rows" -- including the *Ptr tables, whose
    // rows no port-openable file carries.
    EXPECT_EQ(Dump(path, CorTableIndex::FieldPtr, false), "0 rows\r\n");
    EXPECT_EQ(Dump(path, CorTableIndex::CustomAttribute, false), "0 rows\r\n");
}

TEST(MetadataTableDumperTest, ConsoleTableMscorlibTables) {
    std::string mscorlib = MscorlibPath();
    // The File table: the ContainsMetadata bool (the raw Flags == 0), the
    // blob hash offsets, the aligned decimal RIDs (width 1).
    EXPECT_EQ(Dump(mscorlib, CorTableIndex::File, false),
        "RID  Token       ContainsMetadata  Name          HashValue\r\n"
        "---  ----------  ----------------  ------------  ----------\r\n"
        "1    0x26000001  false             normidna.nlp  0x00021DD0\r\n"
        "2    0x26000002  false             normnfc.nlp   0x00021DE5\r\n"
        "3    0x26000003  false             normnfd.nlp   0x00021DFA\r\n"
        "4    0x26000004  false             normnfkc.nlp  0x00021E0F\r\n"
        "5    0x26000005  false             normnfkd.nlp  0x00021E24\r\n");
    // The ManifestResource table: the signed Offset column, the
    // ManifestResourceAttributes render ("Public"), the Implementation
    // coded index.
    EXPECT_EQ(Dump(mscorlib, CorTableIndex::ManifestResource, false),
        "RID  Token       Offset  Attributes  Name                Implementation\r\n"
        "---  ----------  ------  ----------  ------------------  --------------\r\n"
        "1    0x28000001  0       Public      normidna.nlp        0x26000001\r\n"
        "2    0x28000002  0       Public      normnfc.nlp         0x26000002\r\n"
        "3    0x28000003  0       Public      normnfd.nlp         0x26000003\r\n"
        "4    0x28000004  0       Public      normnfkc.nlp        0x26000004\r\n"
        "5    0x28000005  0       Public      normnfkd.nlp        0x26000005\r\n"
        "6    0x28000006  0       Public      mscorlib.resources  nil\r\n"
        "7    0x28000007  353040  Public      charinfo.nlp        nil\r\n"
        "8    0x28000008  390040  Public      codepages.nlp       nil\r\n");
    // The Module row over mscorlib: the SHA1-era assembly GUIDs and the
    // real name.
    std::string module = Dump(mscorlib, CorTableIndex::Module, false);
    EXPECT_NE(module.find(
        "1    0x00000001  0           CommonLanguageRuntimeLibrary  "
        "cfbe3cd1-8651-4c71-ae57-6a8663cf2300  nil           nil"),
        std::string::npos);
    // The Assembly row: the Sha1 hash algorithm spelling (the .NET member
    // name, not the IL spelling), the 4.0.0.0 version, the PublicKey flag,
    // the blob offset, the trailing empty Culture (the TrimEnd).
    std::string assembly = Dump(mscorlib, CorTableIndex::Assembly, false);
    EXPECT_NE(assembly.find("RID  Token       HashAlgorithm  Version  Flags      PublicKey   Name      Culture"),
        std::string::npos);
    EXPECT_NE(assembly.find(
        "1    0x20000001  Sha1           4.0.0.0  PublicKey  0x00012BED  mscorlib"),
        std::string::npos);
    // The DeclSecurity rows: the DeclarativeSecurityAction plain renders,
    // the HasDeclSecurity parents (Assembly/TypeDef/MethodDef tokens), the
    // permission-set blob offsets.
    std::string declSecurity = Dump(mscorlib, CorTableIndex::DeclSecurity, false);
    EXPECT_NE(declSecurity.find(
        "RID  Token       Action             Parent      PermissionSet"),
        std::string::npos);
    EXPECT_NE(declSecurity.find(
        "1    0x0E000001  RequestMinimum     0x20000001  0x0001D9E8"),
        std::string::npos);
    EXPECT_NE(declSecurity.find(
        "2    0x0E000002  LinkDemand         0x02000019  0x0001DA88"),
        std::string::npos);
    EXPECT_NE(declSecurity.find(
        "3    0x0E000003  InheritanceDemand  0x02000022  0x0001DB22"),
        std::string::npos);
    EXPECT_NE(declSecurity.find(
        "10   0x0E00000A  Demand             0x06000119  0x0001DD3A"),
        std::string::npos);
    // The ImplMap union renders: CharSetAuto (0x6) is a composite member the
    // flags algorithm matches before its component bits (the 0x116
    // FormatMessage row), and a plain union joins in ascending value order.
    std::string implMap = Dump(mscorlib, CorTableIndex::ImplMap, false);
    EXPECT_NE(implMap.find(
        "CharSetMask, BestFitMappingEnable, CallingConventionWinApi"),
        std::string::npos);
    EXPECT_NE(implMap.find(
        "CharSetMask, SetLastError, CallingConventionWinApi"),
        std::string::npos);
    EXPECT_NE(implMap.find(
        "1    0x1C000001  SetLastError, CallingConventionWinApi"),
        std::string::npos);
}

TEST(MetadataTableDumperTest, JsonTinyNetModule) {
    std::string path = WriteTinyNetModule();
    // The assembly value is the path as passed (the C# writes the input
    // verbatim through the JSON escaping); the forward-slash form keeps the
    // expected string free of the backslash escape (a backslash path
    // renders its separators as \\).
    for (char& c : path)
        if (c == '\\') c = '/';
    // The Module document: the RID number arm, every other cell a string,
    // the 2-space indentation, the CRLF breaks, the final newline.
    EXPECT_EQ(Dump(path, CorTableIndex::Module, true),
        "{\r\n"
        "  \"assembly\": \"" + path + "\",\r\n"
        "  \"table\": \"Module\",\r\n"
        "  \"rowCount\": 1,\r\n"
        "  \"rows\": [\r\n"
        "    {\r\n"
        "      \"RID\": 1,\r\n"
        "      \"Token\": \"0x00000001\",\r\n"
        "      \"Generation\": \"0\",\r\n"
        "      \"Name\": \"tiny\",\r\n"
        "      \"Mvid\": \"163cefd4-809a-4644-82fd-23d0eb508d60\",\r\n"
        "      \"GenerationId\": \"nil\",\r\n"
        "      \"BaseGenerationId\": \"nil\"\r\n"
        "    }\r\n"
        "  ]\r\n"
        "}\r\n");
    // The empty-table shape: the rows array on one line.
    EXPECT_EQ(Dump(path, CorTableIndex::FieldPtr, true),
        "{\r\n"
        "  \"assembly\": \"" + path + "\",\r\n"
        "  \"table\": \"FieldPtr\",\r\n"
        "  \"rowCount\": 0,\r\n"
        "  \"rows\": []\r\n"
        "}\r\n");
    // The TypeDef rows: the escaping (the <Module> angle brackets as their
    // \u00XX forms), the multi-row separators.
    EXPECT_EQ(Dump(path, CorTableIndex::TypeDef, true),
        "{\r\n"
        "  \"assembly\": \"" + path + "\",\r\n"
        "  \"table\": \"TypeDef\",\r\n"
        "  \"rowCount\": 2,\r\n"
        "  \"rows\": [\r\n"
        "    {\r\n"
        "      \"RID\": 1,\r\n"
        "      \"Token\": \"0x02000001\",\r\n"
        "      \"Attributes\": \"NotPublic\",\r\n"
        "      \"Name\": \"\\u003CModule\\u003E\",\r\n"
        "      \"Namespace\": \"\",\r\n"
        "      \"BaseType\": \"nil\",\r\n"
        "      \"FieldList\": \"1\",\r\n"
        "      \"MethodList\": \"1\"\r\n"
        "    },\r\n"
        "    {\r\n"
        "      \"RID\": 2,\r\n"
        "      \"Token\": \"0x02000002\",\r\n"
        "      \"Attributes\": \"Public\",\r\n"
        "      \"Name\": \"Tiny\",\r\n"
        "      \"Namespace\": \"\",\r\n"
        "      \"BaseType\": \"0x01000001\",\r\n"
        "      \"FieldList\": \"1\",\r\n"
        "      \"MethodList\": \"1\"\r\n"
        "    }\r\n"
        "  ]\r\n"
        "}\r\n");
    // The AssemblyRef row: the Version/Flags string cells and the nil
    // PublicKeyOrToken/HashValue blobs.
    EXPECT_EQ(Dump(path, CorTableIndex::AssemblyRef, true),
        "{\r\n"
        "  \"assembly\": \"" + path + "\",\r\n"
        "  \"table\": \"AssemblyRef\",\r\n"
        "  \"rowCount\": 1,\r\n"
        "  \"rows\": [\r\n"
        "    {\r\n"
        "      \"RID\": 1,\r\n"
        "      \"Token\": \"0x23000001\",\r\n"
        "      \"Version\": \"4.0.0.0\",\r\n"
        "      \"Flags\": \"0\",\r\n"
        "      \"PublicKeyOrToken\": \"0x00000001\",\r\n"
        "      \"Name\": \"mscorlib\",\r\n"
        "      \"Culture\": \"\",\r\n"
        "      \"HashValue\": \"nil\"\r\n"
        "    }\r\n"
        "  ]\r\n"
        "}\r\n");
}

TEST(MetadataTableDumperTest, JsonMscorlibEscapesAndRows) {
    std::string mscorlib = MscorlibPath();
    std::string typeDef = Dump(mscorlib, CorTableIndex::TypeDef, true);
    // The backtick escape (the `1 arity suffixes), the rowCount number, the
    // multi-row separators.
    EXPECT_NE(typeDef.find("\"Name\": \"Action\\u00601\","), std::string::npos);
    EXPECT_NE(typeDef.find("\"Name\": \"Action\\u00602\","), std::string::npos);
    EXPECT_NE(typeDef.find("\"Name\": \"\\u003C\\u003Ef__AnonymousType0\\u00601\","),
        std::string::npos);
    // The rowCount over the full table.
    EXPECT_NE(typeDef.find("\"rowCount\": 3356,"), std::string::npos);
    // The CustomAttribute document: the 5-bit HasCustomAttribute parents
    // (the Assembly token 0x20000001 the tag order must decode to).
    std::string customAttributes =
        Dump(mscorlib, CorTableIndex::CustomAttribute, true);
    EXPECT_NE(customAttributes.find(
        "\"Parent\": \"0x20000001\",\r\n      \"Constructor\": \"0x06005DA9\","),
        std::string::npos);
}

TEST(MetadataTableDumperTest, DumpTableInvalidFileDegrades) {
    // An invalid file dumps its (empty) tables without a throw -- the CLI
    // validates the file before dispatching (the C# PEFile constructor throw
    // is the caller's concern).
    std::ostringstream buffer;
    int rc = Cmd::DumpTable("no-such-assembly.dll", buffer,
        CorTableIndex::TypeDef, false);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(buffer.str(), "0 rows\r\n");
}

// TableName: the `table.ToString()` spelling the -o file name composes
// from (`<name>.{table}.{txt|json}`) -- the ECMA member names, case-
// sensitive, and the throw for an id with no named member (the C#
// ToString of an undefined enum value would render the decimal; every
// supported parse maps onto a named member, so the port's load-bearing
// surface is the named spellings).
TEST(MetadataTableDumperTest, TableNameSpellsTheTableIndexToString) {
    EXPECT_STREQ(Cmd::TableName(CorTableIndex::Module), "Module");
    EXPECT_STREQ(Cmd::TableName(CorTableIndex::TypeDef), "TypeDef");
    EXPECT_STREQ(Cmd::TableName(CorTableIndex::MethodSemantics), "MethodSemantics");
    EXPECT_STREQ(Cmd::TableName(CorTableIndex::ManifestResource), "ManifestResource");
    EXPECT_STREQ(Cmd::TableName(CorTableIndex::GenericParamConstraint),
        "GenericParamConstraint");
    // An alias parse resolves to the member's own spelling (the enum value,
    // not the input text, names the file).
    CorTableIndex table;
    ASSERT_TRUE(Cmd::TryParseTableName("typedef", table));
    EXPECT_STREQ(Cmd::TableName(table), "TypeDef");
    ASSERT_TRUE(Cmd::TryParseTableName("0x02", table));
    EXPECT_STREQ(Cmd::TableName(table), "TypeDef");
    // The -o file-name composition the CLI's dump-table branch builds.
    EXPECT_EQ(std::string(".") + Cmd::TableName(table) + ".txt", ".TypeDef.txt");
    EXPECT_EQ(std::string(".") + Cmd::TableName(table) + ".json", ".TypeDef.json");
    // An id with no named member throws (unreachable through the CLI's
    // TryParseTableName gate).
    EXPECT_THROW(
        Cmd::TableName(static_cast<CorTableIndex>(0x1E)),
        std::logic_error);
}
