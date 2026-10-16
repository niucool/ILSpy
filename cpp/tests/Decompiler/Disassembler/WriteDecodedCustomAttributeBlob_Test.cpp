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

// Tests for the ReflectionDisassembler::WriteDecodedCustomAttributeBlob port
// (ReflectionDisassembler.cs lines 1873-1915) -- the DecodeCustomAttributeBlobs
// path of WriteAttributes: the attribute value decoded through the
// CustomAttributeDecoderT<SecurityDeclarationDecoder> instantiation and
// rendered as the "{ fixed... named... }" block (the PD3 WriteValue /
// WriteSimpleValue renderers), with the BadImageFormatException catch arm
// rendering the "/* Could not decode attribute value */" comment plus the
// raw blob dump. The fixtures: the synthetic manifest's attribute rows
// (the ctor signatures and blobs the CustomAttributeDecoderPair tests
// already pin on the decode side).

#include <cstdlib>
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "TestFixtures/CadDecoderGold.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace {

namespace TM = ILSpy::Decompiler::Metadata;
namespace TD = ILSpy::Decompiler::Disassembler;
namespace TO = ILSpy::Decompiler::Output;

// The miss-everything stub resolver (the WriteSecurityDeclarations shape:
// a resolver that resolves no assembly -- the ResolveType else-arm falls
// back to the current module).
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

// Writes the embedded manifest to a temp file (MetadataFile needs a real
// file) -- the AssemblyIdentityFixtures WriteTempAssembly convention.
std::string WriteSynthManifest() {
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "WdCabSynth_test.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(ILSpy::Tests::kCadSynthManifest, 1,
        ILSpy::Tests::kCadSynthManifestSize, out);
    std::fclose(out);
    return path.string();
}

// The manifest's attribute row (1-based).
TM::CustomAttributeRowInfo AttributeRow(
    const TM::MetadataFile& manifest, std::uint32_t row) {
    auto info = manifest.GetCustomAttribute(0x0C000000 | row);
    // A missing row is a fixture-generation break, not a pass.
    if (!info)
        ADD_FAILURE() << "attribute row " << row << " missing";
    return info.value();
}

TEST(WriteDecodedCustomAttributeBlobTest, DecodesTheFixedArgumentRows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    // Row 9 (one Int32[] fixed argument [1, 2, 3]): the "{ ... }" block
    // over the WriteValue render, one indented line per argument.
    disassembler.WriteDecodedCustomAttributeBlob(manifest,
        AttributeRow(manifest, 9));
    EXPECT_EQ(output.ToString(), "{\r\n\tint32[3](1 2 3)\r\n}");

    // Row 10 (no arguments): the empty block (the closing WriteLine still
    // fires).
    TO::PlainTextOutput empty;
    TD::ReflectionDisassembler emptyDisassembler(empty);
    emptyDisassembler.WriteDecodedCustomAttributeBlob(manifest,
        AttributeRow(manifest, 10));
    EXPECT_EQ(empty.ToString(), "{\r\n}");

    // Row 12 (the MemberReference constructor, one Int32 fixed argument).
    TO::PlainTextOutput member;
    TD::ReflectionDisassembler memberDisassembler(member);
    memberDisassembler.WriteDecodedCustomAttributeBlob(manifest,
        AttributeRow(manifest, 12));
    EXPECT_EQ(member.ToString(), "{\r\n\tint32(77)\r\n}");
}

TEST(WriteDecodedCustomAttributeBlobTest, DecodesTheNamedArgumentRows) {
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    // A hand-built value blob over row 9's Int32[] constructor signature:
    // two fixed values plus one named property -- the named-argument render
    // (the "field "/"property " prefix, the type name, the escaped member
    // name, " = ", and the WriteValue render).
    const std::uint8_t valueBlob[] = {
        0x01, 0x00,                                    // prologue
        0x02, 0x00, 0x00, 0x00,                        // array count 2
        0x01, 0x00, 0x00, 0x00,                        // item 1
        0x02, 0x00, 0x00, 0x00,                        // item 2
        0x01, 0x00,                                    // named-arg count 1
        0x54, 0x02, 0x02, 0x70, 0x62, 0x01,            // property bool pb = true
    };
    TM::CustomAttributeRowInfo row;
    row.ConstructorToken = AttributeRow(manifest, 9).ConstructorToken;
    row.ValueBlob = std::vector<std::uint8_t>(std::begin(valueBlob),
        std::end(valueBlob));
    disassembler.WriteDecodedCustomAttributeBlob(manifest, row);
    EXPECT_EQ(output.ToString(),
        "{\r\n\tint32[2](1 2)\r\n\tproperty bool pb = bool(true)\r\n}");
}

TEST(WriteDecodedCustomAttributeBlobTest, TypeTypedFixedArgumentsFallBackToRawBlob)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    // The SecurityDeclarationDecoder quirk: a Type-typed FIXED constructor
    // argument never decodes -- GetTypeFromDefinition returns the full type
    // name ("System.Type"), which IsSystemType ("type" == Name) rejects, so
    // the TypeCode falls to the underlying-enum code (0 = Invalid) and the
    // DecodeArgument switch throws (the BadImageFormatException family).
    // The catch arm renders the comment plus the raw blob dump (the value
    // bytes past the prologue never matter -- the throw fires at the
    // signature read).
    // A miss-everything resolver: the intended combination (the GUI always
    // sets one with the flag on).
    MissResolver miss;
    disassembler.AssemblyResolver(&miss);
    const std::uint8_t valueBlob[] = {
        0x01, 0x00,  // prologue
        0x00, 0x00,  // named-arg count 0
    };
    TM::CustomAttributeRowInfo row;
    // Row 1's constructor: (bool[], string, class System.Type, string).
    row.ConstructorToken = AttributeRow(manifest, 1).ConstructorToken;
    row.ValueBlob = std::vector<std::uint8_t>(std::begin(valueBlob),
        std::end(valueBlob));
    disassembler.WriteDecodedCustomAttributeBlob(manifest, row);
    EXPECT_EQ(output.ToString(),
        "/* Could not decode attribute value */ (\r\n\t01 00 00 00\r\n)");

    // The real row 1 blob: the same fallback over the authored bytes.
    TO::PlainTextOutput real;
    TD::ReflectionDisassembler realDisassembler(real);
    realDisassembler.AssemblyResolver(&miss);
    realDisassembler.WriteDecodedCustomAttributeBlob(manifest,
        AttributeRow(manifest, 1));
    EXPECT_EQ(real.ToString().substr(0, 36),
        "/* Could not decode attribute value ");
    EXPECT_NE(real.ToString().find("\r\n\t01 00 02 00 00 00 01 00"),
        std::string::npos);

    // The null-resolver quirk: the C# ResolveType derefs the null resolver
    // for an assembly-qualified name (a TypeRef-scoped signature handle) --
    // the NullReferenceException the C# catch arms do NOT catch; the port
    // maps it to std::runtime_error (the BamlNode NRE convention) so it
    // propagates the same way instead of crashing.
    TO::PlainTextOutput nullResolver;
    TD::ReflectionDisassembler nullDisassembler(nullResolver);
    EXPECT_THROW(nullDisassembler.WriteDecodedCustomAttributeBlob(manifest,
                     AttributeRow(manifest, 1)),
        std::runtime_error);
}

TEST(WriteDecodedCustomAttributeBlobTest, WriteAttributesRoutesThroughTheDecode)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    disassembler.DecodeCustomAttributeBlobs = true;

    // The WriteAttributes path with the flag on: the constructor render
    // (already pinned by the WriteAttributes tests) followed by the decoded
    // block instead of the raw dump.
    disassembler.WriteAttributes(manifest, {0x0C000009});
    const std::string text = output.ToString();
    EXPECT_EQ(text.substr(text.size() - 27),
        " = {\r\n\tint32[3](1 2 3)\r\n}\r\n");
    EXPECT_EQ(text.substr(0, 8), ".custom ");
}

TEST(WriteDecodedCustomAttributeBlobTest, WriteAttributesRawPathUnchangedByDefault)
{
    TM::MetadataFile manifest(WriteSynthManifest());
    ASSERT_TRUE(manifest.IsValid());
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);

    // The default (the flag off) keeps the raw blob dump -- the CLI's shape.
    disassembler.WriteAttributes(manifest, {0x0C00000C});
    const std::string text = output.ToString();
    EXPECT_EQ(text.substr(0, 8), ".custom ");
    // The raw dump of row 12's blob (prologue, the Int32 77, the named-arg
    // count 0): the WriteBlob hex form.
    EXPECT_NE(text.find("01 00 4d 00 00 00 00 00\r\n"), std::string::npos);
}

} // namespace
