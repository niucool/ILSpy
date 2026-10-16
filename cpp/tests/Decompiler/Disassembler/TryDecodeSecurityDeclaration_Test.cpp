// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE, NONINFRINGEMENT, OR AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the PD5 slice -- the last Phase-6 deferral: the
// Output::TextOutputWithRollback port (PlainTextOutput.cs lines 168-257),
// the ReflectionDisassembler::TryDecodeSecurityDeclaration port
// (ReflectionDisassembler.cs lines 678-766), and the resolver arms of
// ReflectionDisassembler::WriteSecurityDeclarations (lines 450-466).
//
// The fixtures: a recording mock ITextOutput (the rollback semantics), the
// synthetic manifest (the hand-built permission-set blobs over the
// "CadSynth" assembly name), and the staged mscorlib's real
// assembly-level permission set (the '.' binary form -- the only
// mscorlib row, action reqmin, a SecurityPermissionAttribute with the
// SkipVerification property).

#include <cstdlib>
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Disassembler/OpCodeInfo.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "TestFixtures/CadDecoderGold.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TD = ILSpy::Decompiler::Disassembler;
namespace TO = ILSpy::Decompiler::Output;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

bool FileAvailable(const char* path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Writes the embedded manifest to a temp file (MetadataFile needs a real
// file) -- the AssemblyIdentityFixtures WriteTempAssembly convention.
std::string WriteSynthManifest() {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "SecDeclRollback_test.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(ILSpy::Tests::kCadSynthManifest, 1,
        ILSpy::Tests::kCadSynthManifestSize, out);
    std::fclose(out);
    return path.string();
}

// The recording mock ITextOutput -- every call appends a rendered line.
class MockTextOutput final : public TO::ITextOutput {
public:
    std::string IndentationString() const override {
        return "<indent-get:" + indentationString_ + ">";
    }
    void IndentationString(std::string value) override {
        indentationString_ = std::move(value);
        log_ += "setIndent(" + indentationString_ + ")\n";
    }
    void Indent() override { log_ += "indent\n"; }
    void Unindent() override { log_ += "unindent\n"; }
    void Write(char ch) override { log_ += std::string("write(") + ch + ")\n"; }
    void Write(std::string_view text) override {
        log_ += "write(" + std::string(text) + ")\n";
    }
    void WriteLine() override { log_ += "writeln\n"; }
    void WriteReference(const TD::OpCodeInfo& opCode,
        bool omitSuffix) override {
        log_ += "opRef(" + opCode.Name() + ","
            + (omitSuffix ? "true" : "false") + ")\n";
    }
    void WriteReference(const TM::MetadataFile&, std::uint32_t handle,
        std::string_view text, std::string_view protocol,
        bool isDefinition) override {
        log_ += "modRef(" + std::to_string(handle) + ","
            + std::string(text) + "," + std::string(protocol) + ","
            + (isDefinition ? "true" : "false") + ")\n";
    }
    void WriteReference(const ILSpy::Decompiler::TypeSystem::IType&,
        std::string_view, bool) override {
        log_ += "typeRef\n";
    }
    void WriteReference(const ILSpy::Decompiler::TypeSystem::IMember&,
        std::string_view, bool) override {
        log_ += "memberRef\n";
    }
    void WriteLocalReference(std::string_view text, const void* reference,
        bool isDefinition, bool isHoverOnly) override {
        log_ += "localRef(" + std::string(text) + ","
            + (reference ? "ref" : "null") + ","
            + (isDefinition ? "true" : "false") + ","
            + (isHoverOnly ? "true" : "false") + ")\n";
    }
    void MarkFoldStart(std::string_view collapsedText, bool defaultCollapsed,
        bool isDefinition) override {
        log_ += "foldStart(" + std::string(collapsedText) + ","
            + (defaultCollapsed ? "true" : "false") + ","
            + (isDefinition ? "true" : "false") + ")\n";
    }
    void MarkDefinitionStart() override { log_ += "defStart\n"; }
    void MarkFoldEnd() override { log_ += "foldEnd\n"; }

    const std::string& Log() const { return log_; }

private:
    std::string log_;
    std::string indentationString_ = "\t";
};

TEST(TextOutputWithRollbackTest, CommitReplaysTheRecordedActionsInOrder) {
    MockTextOutput target;
    TO::TextOutputWithRollback rollback(target);
    // Nothing reaches the target before the commit.
    rollback.Write('x');
    rollback.Write("y");
    rollback.WriteLine();
    rollback.Indent();
    rollback.Write("z");
    rollback.Unindent();
    EXPECT_EQ(target.Log(), "");

    rollback.Commit();
    EXPECT_EQ(target.Log(),
        "write(x)\n"
        "write(y)\n"
        "writeln\n"
        "indent\n"
        "write(z)\n"
        "unindent\n");
}

TEST(TextOutputWithRollbackTest, NoCommitMeansTheActionsAreDiscarded) {
    MockTextOutput target;
    {
        TO::TextOutputWithRollback rollback(target);
        rollback.Write("discarded");
        rollback.WriteLine();
        // No Commit: the destructor discards the recorded actions.
    }
    EXPECT_EQ(target.Log(), "");
}

TEST(TextOutputWithRollbackTest, IndentationStringForwardsDirectly) {
    MockTextOutput target;
    TO::TextOutputWithRollback rollback(target);
    // The C# forwards IndentationString to the target immediately (it is
    // NOT a recorded action).
    rollback.IndentationString("  ");
    EXPECT_EQ(target.Log(), "setIndent(  )\n");
    EXPECT_EQ(rollback.IndentationString(), "<indent-get:  >");
    rollback.Commit();
    // Still only the direct forward -- the commit has nothing to replay.
    EXPECT_EQ(target.Log(), "setIndent(  )\n");
}

TEST(TextOutputWithRollbackTest, TheCSharpParameterDroppingQuirks) {
    MockTextOutput target;
    TO::TextOutputWithRollback rollback(target);
    int refToken = 42;
    // The C# lambda drops WriteLocalReference's isHoverOnly parameter.
    rollback.WriteLocalReference("name", &refToken, /*isDefinition=*/false,
        /*isHoverOnly=*/true);
    // The C# lambda drops WriteReference(OpCodeInfo)'s omitSuffix
    // parameter (an opcode reference always replays with omitSuffix
    // unset).
    TD::OpCodeInfo opCode(TM::ILOpCode::Nop, "nop");
    rollback.WriteReference(opCode, /*omitSuffix=*/true);
    rollback.Commit();
    EXPECT_EQ(target.Log(),
        "localRef(name,ref,false,false)\n"
        "opRef(nop,false)\n");
}

TEST(TextOutputWithRollbackTest, DefaultedParametersReplayVerbatim) {
    MockTextOutput target;
    TO::TextOutputWithRollback rollback(target);
    // The C# WriteLocalReference lambda forwards the default-argument
    // surface (text, reference, isDefinition) -- the defaults materialize
    // at the record site.
    // The defaults live on the interface declaration (a C++ override may
    // not restate them): reach them through the ITextOutput& view.
    TO::ITextOutput& rollbackView = rollback;
    rollbackView.WriteLocalReference("loc");
    rollbackView.MarkFoldStart("...");
    TM::MetadataFile* file = nullptr;
    rollback.WriteReference(*file, 0x06000001, "text", "decompile", true);
    rollback.Commit();
    EXPECT_EQ(target.Log(),
        "localRef(loc,null,false,false)\n"
        "foldStart(...,false,false)\n"
        "modRef(100663297,text,decompile,true)\n");
}

// A SerString helper for the hand-built permission-set blobs.
void AppendSerString(std::vector<std::uint8_t>& blob, const std::string& s) {
    blob.push_back(static_cast<std::uint8_t>(s.size()));
    blob.insert(blob.end(), s.begin(), s.end());
}

TEST(TryDecodeSecurityDeclarationTest, DecodesTheHandBuiltTwoEntryBlob) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    // The hand-built blob: the '.' marker, two entries -- the first an
    // assembly-qualified name (the "[assembly]Type" form, since the
    // manifest's own assembly name is "CadSynth") with two named
    // arguments, the second an unqualified name (the "class Type" form)
    // with no arguments.
    std::vector<std::uint8_t> blob{'.'};
    blob.push_back(2);  // the entry count
    AppendSerString(blob,
        "System.Security.Permissions.SecurityPermissionAttribute, mscorlib");
    blob.push_back(1);  // the C#'s unread compressed integer ("?")
    blob.push_back(2);  // the named-argument count
    // property bool SkipVerification = true
    blob.push_back(0x54);
    blob.push_back(0x02);
    AppendSerString(blob, "SkipVerification");
    blob.push_back(0x01);
    // field string XML = "abc"
    blob.push_back(0x53);
    blob.push_back(0x0E);
    AppendSerString(blob, "XML");
    AppendSerString(blob, "abc");
    // entry 2: an unqualified type, no arguments.
    AppendSerString(blob, "SomeType");
    blob.push_back(0);  // the unread integer
    blob.push_back(0);  // the named-argument count

    // The C# receives the blob reader positioned AFTER the '.' marker
    // (the WriteSecurityDeclarations gate's ReadByte advanced it).
    TM::BlobReader reader{blob.data(), blob.size(), 0};
    ASSERT_EQ(static_cast<char>(reader.ReadByte()), '.');
    TO::PlainTextOutput decoded;
    TO::TextOutputWithRollback rollback(decoded);
    disassembler.TryDecodeSecurityDeclaration(rollback, reader, manifest);
    rollback.Commit();
    EXPECT_EQ(decoded.ToString(),
        " = {\r\n"
        "\t[mscorlib]System.Security.Permissions.SecurityPermissionAttribute"
        " = {\r\n"
        "\t\tproperty bool SkipVerification = bool(true)\r\n"
        "\t\tfield string XML = string('abc')\r\n"
        "\t},\r\n"
        "\tclass SomeType = {}\r\n"
        "}\r\n");
}

TEST(TryDecodeSecurityDeclarationTest, MalformedBlobThrows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    // '.' + a reserved compressed-integer prefix: the entry count read
    // throws (the Invalid compressed integer text).
    const std::uint8_t blob[] = {0x2E, 0xFF};
    TM::BlobReader reader{blob, sizeof(blob), 0};
    ASSERT_EQ(static_cast<char>(reader.ReadByte()), '.');
    TO::PlainTextOutput decoded;
    TO::TextOutputWithRollback rollback(decoded);
    EXPECT_THROW(disassembler.TryDecodeSecurityDeclaration(rollback, reader,
                     manifest),
        std::invalid_argument);
}

TEST(TryDecodeSecurityDeclarationTest, RealMscorlibAssemblyLevelPermissionSet)
{
    if (!FileAvailable(MscorlibPath()))
        GTEST_SKIP() << "mscorlib fixture not available";

    TM::MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    auto decls = mscorlib.GetDeclarativeSecurityAttributes(0x20000001);
    // The single assembly-level row (action reqmin, the
    // SecurityPermissionAttribute with SkipVerification) -- a missing row
    // is a fixture-generation break, not a pass.
    ASSERT_EQ(decls.size(), 1u);

    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    TM::BlobReader reader{decls[0].PermissionSet.data(),
        decls[0].PermissionSet.size(), 0};
    ASSERT_EQ(static_cast<char>(reader.ReadByte()), '.');
    TO::PlainTextOutput decoded;
    TO::TextOutputWithRollback rollback(decoded);
    disassembler.TryDecodeSecurityDeclaration(rollback, reader, mscorlib);
    rollback.Commit();
    // The mono mscorlib's assembly name is "mscorlib", so the second name
    // part matches the current assembly -- the "class " form.
    // The escaped full type name is quoted (a ", "-containing name is not
    // a valid identifier, so DisassemblerHelpers.Escape wraps it in
    // single quotes).
    EXPECT_EQ(decoded.ToString(),
        " = {\r\n"
        "\tclass 'System.Security.Permissions.SecurityPermissionAttribute,"
        " mscorlib, Version=4.0.0.0, Culture=neutral,"
        " PublicKeyToken=b77a5c561934e089' = {\r\n"
        "\t\tproperty bool SkipVerification = bool(true)\r\n"
        "\t}\r\n"
        "}\r\n");
}

// The miss-everything stub resolver (the WriteSecurityDeclarations shape:
// a resolver that resolves no assembly -- only the non-null value gates
// the arms).
class MissResolver final : public TM::IAssemblyResolver {
public:
    const TM::MetadataFile* Resolve(
        const TM::IAssemblyReference&) const override {
        return nullptr;
    }
    const TM::MetadataFile* ResolveModule(const TM::MetadataFile&,
        const std::string&) const override {
        return nullptr;
    }
};

TEST(WriteSecurityDeclarationsTest, NullResolverKeepsTheRawDump) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    std::vector<TM::MetadataFile::DeclarativeSecurityInfo> decls{
        TM::MetadataFile::DeclarativeSecurityInfo{0, 8, {0x3C, 0x78, 0x6D}}};
    disassembler.WriteSecurityDeclarations(manifest, decls);
    EXPECT_EQ(output.ToString(),
        ".permissionset reqmin = (\r\n\t3c 78 6d\r\n)\r\n");
}

TEST(WriteSecurityDeclarationsTest, NonDotBlobRendersTheByteArrayArm) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    MissResolver resolver;
    disassembler.AssemblyResolver(&resolver);

    // A blob not starting with '.': the indented "bytearray" + raw dump.
    std::vector<TM::MetadataFile::DeclarativeSecurityInfo> decls{
        TM::MetadataFile::DeclarativeSecurityInfo{0, 8, {0x3C, 0x78, 0x6D}}};
    disassembler.WriteSecurityDeclarations(manifest, decls);
    EXPECT_EQ(output.ToString(),
        ".permissionset reqmin\r\n\tbytearray(\r\n\t\t3c 78 6d\r\n\t)\r\n");
}

TEST(WriteSecurityDeclarationsTest, DecodableDotBlobRendersTheDecodedForm)
{
    if (!FileAvailable(MscorlibPath()))
        GTEST_SKIP() << "mscorlib fixture not available";

    TM::MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    auto decls = mscorlib.GetDeclarativeSecurityAttributes(0x20000001);
    ASSERT_EQ(decls.size(), 1u);

    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    MissResolver resolver;
    disassembler.AssemblyResolver(&resolver);
    disassembler.WriteSecurityDeclarations(mscorlib, decls);
    EXPECT_EQ(output.ToString(),
        ".permissionset reqmin = {\r\n"
        "\tclass 'System.Security.Permissions.SecurityPermissionAttribute,"
        " mscorlib, Version=4.0.0.0, Culture=neutral,"
        " PublicKeyToken=b77a5c561934e089' = {\r\n"
        "\t\tproperty bool SkipVerification = bool(true)\r\n"
        "\t}\r\n"
        "}\r\n");
}

TEST(WriteSecurityDeclarationsTest, UndecodableDotBlobFallsBackToRawDump) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    MissResolver resolver;
    disassembler.AssemblyResolver(&resolver);

    // '.' + a reserved prefix: the decode throws, the catch arm renders
    // the raw dump (" = " + WriteBlob).
    std::vector<TM::MetadataFile::DeclarativeSecurityInfo> decls{
        TM::MetadataFile::DeclarativeSecurityInfo{0, 8, {0x2E, 0xFF}}};
    disassembler.WriteSecurityDeclarations(manifest, decls);
    EXPECT_EQ(output.ToString(),
        ".permissionset reqmin = (\r\n\t2e ff\r\n)\r\n");
}

TEST(WriteSecurityDeclarationsTest, EmptyBlobWithResolverThrows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    MissResolver resolver;
    disassembler.AssemblyResolver(&resolver);

    // The C# reads the marker byte OUTSIDE the try -- an empty permission
    // set with a resolver set throws the BadImageFormatException family
    // out of WriteSecurityDeclarations (the port's std::invalid_argument).
    std::vector<TM::MetadataFile::DeclarativeSecurityInfo> decls{
        TM::MetadataFile::DeclarativeSecurityInfo{0, 8, {}}};
    EXPECT_THROW(disassembler.WriteSecurityDeclarations(manifest, decls),
        std::invalid_argument);
}

} // namespace
