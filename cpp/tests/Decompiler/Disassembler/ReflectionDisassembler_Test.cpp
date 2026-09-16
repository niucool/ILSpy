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
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the ReflectionDisassembler instance skeleton and its
// output-shaping helpers (cpp/Decompiler/Disassembler/
// ReflectionDisassembler.{hpp,cpp}): the two constructors (the owned and
// the caller-supplied MethodBodyDisassembler) with the flag properties
// delegating to it, the WriteBlob "(xx xx ...)" hex dump with its 16-byte
// line geometry, the OpenBlock/CloseBlock "{" ... "} // comment" pair,
// the WriteMarshalInfo/WriteNativeType II.23.4 marshalling-descriptor walk
// (the simple native-type spellings, the array/fixed-sysstring/safearray/
// fixed-array/custom-marshaler shapes over synthetic blobs), the constant
// and parameter renderers, the attribute writers, and the method member
// renderer (DisassembleMethod/Header/HeaderInternal/Block with
// WriteSecurityDeclarations and WriteTypeParameters, plus the underlying
// MetadataFile reads: GetMethodImplAttributes, GetMethodImport,
// GetMethodImplementations, GetDeclarativeSecurityAttributes).

#include "Decompiler/Disassembler/MethodBodyDisassembler.hpp"
#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "TestFixtures/TinyNetModule.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace DA = ILSpy::Decompiler::Disassembler;
namespace MD = ILSpy::Decompiler::Metadata;
namespace OUT = ILSpy::Decompiler::Output;
namespace DebugInfo = ILSpy::Decompiler::DebugInfo;

namespace {

#if defined(_WIN32)
const char* MscorlibPath() { return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll"; }
#else
const char* MscorlibPath() { return "/usr/lib/mono/4.5/mscorlib.dll"; }
#endif

// Render through a fresh ReflectionDisassembler over a string stream.

// A SerString (the II.23.3 compressed-length-prefixed string form the
// CustomMarshaler arm reads): the length byte plus the ASCII bytes.
std::vector<std::uint8_t> SerString(const char* s) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(std::strlen(s)));
    out.insert(out.end(), s, s + std::strlen(s));
    return out;
}

std::vector<std::uint8_t> operator+(const std::vector<std::uint8_t>& a,
    const std::vector<std::uint8_t>& b) {
    std::vector<std::uint8_t> out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

std::vector<std::uint8_t> Bytes(std::initializer_list<std::uint8_t> bs) {
    return std::vector<std::uint8_t>(bs);
}

// One hex-byte line of the WriteBlob geometry ("00 01 ... 0f" -- lowercase
// two-digit hex joined by single spaces).
std::string HexLine(int from, int to) {
    std::string line;
    char buf[3];
    for (int i = from; i <= to; ++i) {
        if (i > from) line += ' ';
        std::snprintf(buf, sizeof(buf), "%02x", i);
        line += buf;
    }
    return line;
}

std::vector<std::uint8_t> Range(int from, int to) {
    std::vector<std::uint8_t> v;
    for (int i = from; i <= to; ++i) v.push_back(static_cast<std::uint8_t>(i));
    return v;
}

std::string Render(const std::function<void(OUT::ITextOutput&)>& body) {
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    body(output);
    return stream.str();
}

std::string RenderWithDisassembler(
    const std::function<void(DA::ReflectionDisassembler&)>& body) {
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    DA::ReflectionDisassembler rd(output);
    body(rd);
    return stream.str();
}

std::uint32_t FindTypeDefTokenIn(const MD::MetadataFile& f, std::string_view ns,
    std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindMethodIn(const MD::MetadataFile& f, std::uint32_t typeToken,
    std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

std::uint32_t FindFieldIn(const MD::MetadataFile& f, std::uint32_t typeToken,
    std::string_view name) {
    for (const auto& fd : f.GetFields(typeToken)) {
        if (fd.Name == name) return fd.Token;
    }
    return 0;
}

// A nested TypeDef by name (the GetNestedTypes walk the type renderer
// itself drives; the TypeDefs() table scan cannot pick nested rows apart
// by name when siblings collide).
std::uint32_t FindNestedTypeIn(const MD::MetadataFile& f,
    std::uint32_t enclosingToken, std::string_view name) {
    for (std::uint32_t t : f.GetNestedTypes(enclosingToken)) {
        auto info = f.GetTypeDefNameInfo(t);
        if (info && info->Name == name) return t;
    }
    return 0;
}

// The MethodSignatureT of a real method (the DisassemblerSignatureTypeProvider
// decode the C# DisassembleMethodHeader runs). The holder keeps the provider
// alive: the signature's deferred writers capture it (the provider-must-
// outlive-the-writers liveness contract).
struct MethodSignatureHolder {
    std::unique_ptr<DA::DisassemblerSignatureTypeProvider> provider;
    std::unique_ptr<MD::SignatureTypeProviderDecoder<DA::DisassemblerSignatureTypeProvider>> decoder;
    MD::MethodSignatureT sig;
};

MethodSignatureHolder MethodSignatureOf(const MD::MetadataFile& f,
    std::uint32_t methodToken, OUT::ITextOutput& output) {
    auto blob = f.GetSignatureBlob(methodToken);
    MethodSignatureHolder holder;
    holder.provider = std::make_unique<DA::DisassemblerSignatureTypeProvider>(
        f, output);
    holder.decoder = std::make_unique<
        MD::SignatureTypeProviderDecoder<DA::DisassemblerSignatureTypeProvider>>(
        *holder.provider, f);
    holder.sig = holder.decoder->DecodeMethodSignature(blob->data(), blob->size(),
        MD::MetadataGenericContext::ForMethod(methodToken, f));
    return holder;
}

// A synthetic ConstantInfo (the WriteConstant drive).
MD::ConstantInfo Constant(std::uint8_t typeCode, std::vector<std::uint8_t> value) {
    MD::ConstantInfo info;
    info.TypeCode = typeCode;
    info.Value = std::move(value);
    return info;
}

// A synthetic ParameterInfo row (the WriteParameters drive).
MD::ParameterInfo Param(std::uint16_t sequence, std::uint32_t attributes,
    const char* name) {
    MD::ParameterInfo info;
    info.SequenceNumber = sequence;
    info.Attributes = attributes;
    info.Name = name;
    return info;
}

}  // namespace

// ---------------------------------------------------------------------------
// The constructors and the flag properties (ReflectionDisassembler.cs
// lines 44-99): the delegating pairs go through the MethodBodyDisassembler,
// ExpandMemberDefinitions/DecodeCustomAttributeBlobs are plain flags.
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, OwnedCtorDefaults) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    EXPECT_TRUE(rd.DetectControlStructure());
    EXPECT_FALSE(rd.ShowSequencePoints());
    EXPECT_FALSE(rd.ShowMetadataTokens());
    EXPECT_FALSE(rd.ShowMetadataTokensInBase10());
    EXPECT_FALSE(rd.ShowRawRVAOffsetAndBytes());
    EXPECT_FALSE(rd.ExpandMemberDefinitions);
    EXPECT_FALSE(rd.DecodeCustomAttributeBlobs);
    rd.DetectControlStructure(false);
    EXPECT_FALSE(rd.DetectControlStructure());
}

TEST(ReflectionDisassemblerTest, FlagsDelegateToMethodBodyDisassembler) {
    OUT::PlainTextOutput output;
    DA::MethodBodyDisassembler mbd(output);
    DA::ReflectionDisassembler rd(output, mbd);

    EXPECT_TRUE(rd.DetectControlStructure());
    rd.DetectControlStructure(false);
    EXPECT_FALSE(mbd.DetectControlStructure);
    rd.DetectControlStructure(true);
    EXPECT_TRUE(mbd.DetectControlStructure);

    EXPECT_FALSE(rd.ShowSequencePoints());
    rd.ShowSequencePoints(true);
    EXPECT_TRUE(mbd.ShowSequencePoints);
    EXPECT_TRUE(rd.ShowSequencePoints());
    rd.ShowSequencePoints(false);
    EXPECT_FALSE(mbd.ShowSequencePoints);

    EXPECT_FALSE(rd.ShowMetadataTokens());
    rd.ShowMetadataTokens(true);
    EXPECT_TRUE(mbd.ShowMetadataTokens);
    EXPECT_TRUE(rd.ShowMetadataTokens());
    rd.ShowMetadataTokens(false);
    EXPECT_FALSE(mbd.ShowMetadataTokens);

    EXPECT_FALSE(rd.ShowMetadataTokensInBase10());
    rd.ShowMetadataTokensInBase10(true);
    EXPECT_TRUE(mbd.ShowMetadataTokensInBase10);
    rd.ShowMetadataTokensInBase10(false);
    EXPECT_FALSE(mbd.ShowMetadataTokensInBase10);

    EXPECT_FALSE(rd.ShowRawRVAOffsetAndBytes());
    rd.ShowRawRVAOffsetAndBytes(true);
    EXPECT_TRUE(mbd.ShowRawRVAOffsetAndBytes);
    rd.ShowRawRVAOffsetAndBytes(false);
    EXPECT_FALSE(mbd.ShowRawRVAOffsetAndBytes);

    // The C# `public IDebugInfoProvider DebugInfo { get =>
    // methodBodyDisassembler.DebugInfo; set => ... }` -- the provider pointer
    // delegates through to the same disassembler.
    class NullDebugInfoProvider final : public DebugInfo::IDebugInfoProvider {
    public:
        std::string Description() const override { return {}; }
        std::string SourceFileName() const override { return {}; }
        std::vector<DebugInfo::SequencePoint> GetSequencePoints(
            std::uint32_t) const override { return {}; }
        std::vector<DebugInfo::Variable> GetVariables(
            std::uint32_t) const override { return {}; }
        bool TryGetName(std::uint32_t, int, std::string&) const override { return false; }
        bool TryGetExtraTypeInfo(std::uint32_t, int,
            DebugInfo::PdbExtraTypeInfo&) const override { return false; }
    };
    EXPECT_EQ(rd.DebugInfo(), nullptr);
    EXPECT_EQ(mbd.DebugInfo, nullptr);
    NullDebugInfoProvider provider;
    rd.DebugInfo(&provider);
    EXPECT_EQ(mbd.DebugInfo, &provider);
    EXPECT_EQ(rd.DebugInfo(), &provider);
    rd.DebugInfo(nullptr);
    EXPECT_EQ(mbd.DebugInfo, nullptr);
}

// ---------------------------------------------------------------------------
// WriteBlob (ReflectionDisassembler.cs lines 1926-1948): "(" Indent, one
// lowercase two-digit hex byte per position with a space before every byte
// except the line-start ones (a newline before each 16th byte that is not
// the last), WriteLine, Unindent, ")".
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, WriteBlobEmptyRendersBareParens) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    rd.WriteBlob(nullptr, 0);
    EXPECT_EQ(output.ToString(), "()");
}

TEST(ReflectionDisassemblerTest, WriteBlobThreeBytesGeometry) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    const std::uint8_t blob[] = {0xAA, 0xBB, 0xCC};
    rd.WriteBlob(blob, 3);
    // The byte run is indented one level inside the parens.
    EXPECT_EQ(output.ToString(), "(\r\n\taa bb cc\r\n)");
}

TEST(ReflectionDisassemblerTest, WriteBlobSixteenBytesSingleLine) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    std::vector<std::uint8_t> blob = Range(0, 15);
    rd.WriteBlob(blob.data(), blob.size());
    // The newline fires before byte 0 only; all 16 bytes share the line.
    EXPECT_EQ(output.ToString(), "(\r\n\t" + HexLine(0, 15) + "\r\n)");
}

TEST(ReflectionDisassemblerTest, WriteBlobThirtyTwoBytesTwoLines) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    std::vector<std::uint8_t> blob = Range(0, 31);
    rd.WriteBlob(blob.data(), blob.size());
    // A second newline fires before byte 16 (each line holds 16 bytes).
    EXPECT_EQ(output.ToString(), "(\r\n\t" + HexLine(0, 15) + "\r\n\t" + HexLine(16, 31) + "\r\n)");
}

TEST(ReflectionDisassemblerTest, WriteBlobSeventeenthByteStaysOnFirstLine) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    std::vector<std::uint8_t> blob = Range(0, 16);
    rd.WriteBlob(blob.data(), blob.size());
    // 17 bytes: the newline condition is i % 16 == 0 && i < length - 1, so
    // the 17th byte (i == 16 == length - 1) gets a space, not a newline --
    // the whole blob renders on a single 17-byte line.
    EXPECT_EQ(output.ToString(), "(\r\n\t" + HexLine(0, 16) + "\r\n)");
}

// ---------------------------------------------------------------------------
// OpenBlock/CloseBlock (ReflectionDisassembler.cs lines 1952-1969): the "{"
// ... "}" block pair with the "// comment" suffix and the fold markers.
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, OpenBlockCloseBlockShapes) {
    {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.OpenBlock(false);
        rd.CloseBlock();
        EXPECT_EQ(output.ToString(), "\r\n{\r\n}\r\n");
    }
    {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.OpenBlock(false);
        rd.CloseBlock("end of class Foo");
        EXPECT_EQ(output.ToString(), "\r\n{\r\n} // end of class Foo\r\n");
    }
    {
        // A write inside the block lands one indent level in; the closing
        // brace returns to the outer level.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.OpenBlock(false);
        output.Write("x");
        rd.CloseBlock("end of class Foo");
        EXPECT_EQ(output.ToString(), "\r\n{\r\n\tx} // end of class Foo\r\n");
    }
}

// ---------------------------------------------------------------------------
// WriteMarshalInfo/WriteNativeType (ReflectionDisassembler.cs lines
// 875-1103): "marshal(" + the II.23.4 native type + ") ".
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, WriteMarshalInfoSimpleNativeTypes) {
    const struct {
        std::uint8_t type;
        const char* text;
    } cases[] = {
        {0x02, "bool"},
        {0x03, "int8"},
        {0x04, "unsigned int8"},
        {0x05, "int16"},
        {0x06, "unsigned int16"},
        {0x07, "int32"},
        {0x08, "unsigned int32"},
        {0x09, "int64"},
        {0x0A, "unsigned int64"},
        {0x0B, "float32"},
        {0x0C, "float64"},
        {0x14, "lpstr"},
        {0x1F, "int"},
        {0x20, "unsigned int"},
        {0x26, "Func"},
        {0x0F, "currency"},
        {0x13, "bstr"},
        {0x15, "lpwstr"},
        {0x16, "lptstr"},
        {0x19, "iunknown"},
        {0x1A, "idispatch"},
        {0x1B, "struct"},
        {0x1C, "interface"},
        {0x22, "byvalstr"},
        {0x23, "ansi bstr"},
        {0x24, "tbstr"},
        {0x25, "variant bool"},
        {0x28, "as any"},
        {0x2B, "lpstruct"},
        {0x2D, "error"},
        // The None/Max spellings render nothing between the parens.
        {0x66, ""},
        {0x50, ""},
        // Every other byte renders as its decimal value.
        {0x6F, "111"},
    };
    for (const auto& c : cases) {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteMarshalInfo(&c.type, 1);
        EXPECT_EQ(output.ToString(), std::string("marshal(") + c.text + ") ")
            << "native type 0x" << std::hex << static_cast<int>(c.type);
    }
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeArrayShapes) {
    {
        // Nested element type + the three size fields. The C# reads the
        // size parameter INDEX first (it renders after " + ") and the SIZE
        // second (it renders inside the brackets), so the trailing bytes
        // 05 03 02 render "[3 + 5]".
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2A, 0x02, 0x05, 0x03, 0x02});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(bool[3 + 5]) ");
    }
    {
        // A zero multiplier suppresses the " + index" part (the size is
        // still the second field: 03).
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2A, 0x02, 0x05, 0x03, 0x00});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(bool[3]) ");
    }
    {
        // No bytes after the element type: the Try reads all fail, both
        // size and index stay -1, and the brackets render empty.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2A, 0x02});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(bool[]) ");
    }
    {
        // No bytes at all after the array tag: no nested type either.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2A});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal([]) ");
    }
    {
        // The nested element type is itself rendered as a native type
        // (here unsigned int8).
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2A, 0x04, 0x03, 0x05, 0x03});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(unsigned int8[5 + 3]) ");
    }
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeFixedSysString) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    auto blob = Bytes({0x17, 0x2A});
    rd.WriteMarshalInfo(blob.data(), blob.size());
    EXPECT_EQ(output.ToString(), "marshal(fixed sysstring[42]) ");
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeSafeArrayElements) {
    const struct {
        std::vector<std::uint8_t> blob;
        const char* text;
    } cases[] = {
        {Bytes({0x1D, 0x08}), "safearray bstr"},
        {Bytes({0x1D, 0x0B}), "safearray bool"},
        {Bytes({0x1D, 0x04}), "safearray float32"},
        {Bytes({0x1D, 0x10}), "safearray int8"},
        {Bytes({0x1D, 0x11}), "safearray unsigned int8"},
        {Bytes({0x1D, 0x16}), "safearray int"},
        {Bytes({0x1D, 0x17}), "safearray unsigned int"},
        {Bytes({0x1D, 0x00}), "safearray "},
        {Bytes({0x1D}), "safearray "},
        // Unknown element types render as their decimal value.
        {Bytes({0x1D, 0x1F}), "safearray 31"},
    };
    for (const auto& c : cases) {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteMarshalInfo(c.blob.data(), c.blob.size());
        EXPECT_EQ(output.ToString(), std::string("marshal(") + c.text + ") ")
            << "safearray blob " << (c.blob.size() > 1 ? static_cast<int>(c.blob[1]) : -1);
    }
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeFixedArrayShapes) {
    {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x1E, 0x0A});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(fixed array[10]) ");
    }
    {
        // No size byte: the Try read fails and the size renders as 0.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x1E});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(fixed array[0]) ");
    }
    {
        // A nested native type follows the size, joined by a space.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x1E, 0x0A, 0x02});
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(fixed array[10] bool) ");
    }
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeCustomMarshalerAllFields) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    auto blob = Bytes({0x2C})
        + SerString("D0E836E1-2B8D-4F5B-9C5F-6C5A8BC4D4E8")
        + SerString("IUnknown")
        + SerString("MyMarshaler")
        + SerString("cookie");
    rd.WriteMarshalInfo(blob.data(), blob.size());
    // The GUID renders through its "D" form (lowercase hex, dashes); the
    // managed type and the cookie come first, the GUID and unmanaged type
    // pair only when either is present.
    EXPECT_EQ(output.ToString(),
        "marshal(custom(\"MyMarshaler\", \"cookie\", "
        "\"d0e836e1-2b8d-4f5b-9c5f-6c5a8bc4d4e8\", \"IUnknown\")) ");
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeCustomMarshalerEmptyGuid) {
    {
        // Both the GUID and the unmanaged type empty: just the managed
        // type and the cookie.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2C}) + SerString("") + SerString("")
            + SerString("MyMarshaler") + SerString("cookie");
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(), "marshal(custom(\"MyMarshaler\", \"cookie\")) ");
    }
    {
        // An empty GUID with a present unmanaged type: Guid.Empty renders
        // as the all-zeros "D" form.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        auto blob = Bytes({0x2C}) + SerString("") + SerString("IUnknown")
            + SerString("MyMarshaler") + SerString("cookie");
        rd.WriteMarshalInfo(blob.data(), blob.size());
        EXPECT_EQ(output.ToString(),
            "marshal(custom(\"MyMarshaler\", \"cookie\", "
            "\"00000000-0000-0000-0000-000000000000\", \"IUnknown\")) ");
    }
}

TEST(ReflectionDisassemblerTest, WriteNativeTypeDirectCursorDrive) {
    // WriteNativeType takes the (base, size, pos) cursor and renders from
    // it in place (the array/fixed-array recursions share it).
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    const std::uint8_t blob[] = {0x02, 0x03};
    std::size_t pos = 1;
    rd.WriteNativeType(blob, 2, pos);
    EXPECT_EQ(output.ToString(), "int8");
    EXPECT_EQ(pos, 2u);
}

TEST(ReflectionDisassemblerTest, WriteMarshalInfoEmptyBlobThrows) {
    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    EXPECT_THROW(rd.WriteMarshalInfo(nullptr, 0), std::out_of_range);
}

// ---------------------------------------------------------------------------
// MetadataFile::GetConstant (the Constant table read behind the C#
// `metadata.GetConstant(row.GetDefaultValue())` pair): a Field/Param/Property
// token resolves to its II.23.2 constant row (Type column + value blob).
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, GetConstantFieldParent) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t int32Type = FindTypeDefTokenIn(f, "System", "Int32");
    ASSERT_NE(int32Type, 0u);
    std::uint32_t maxValue = FindFieldIn(f, int32Type, "MaxValue");
    ASSERT_NE(maxValue, 0u);
    auto constant = f.GetConstant(maxValue);
    ASSERT_TRUE(constant.has_value());
    EXPECT_EQ(constant->TypeCode, 0x08);  // ConstantTypeCode.Int32
    EXPECT_EQ(constant->Value,
        (std::vector<std::uint8_t>{0xff, 0xff, 0xff, 0x7f}));
    EXPECT_NE(constant->Token, 0u);
    EXPECT_EQ(constant->Token >> 24, 0x0Bu);
    // A field without a constant has no row.
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t emptyField = FindFieldIn(f, stringType, "Empty");
    ASSERT_NE(emptyField, 0u);
    EXPECT_FALSE(f.GetConstant(emptyField).has_value());
    // Bogus parents: a non-HasConstant table token and a nil row.
    EXPECT_FALSE(f.GetConstant(0x06000001).has_value());
    EXPECT_FALSE(f.GetConstant(0x04000000).has_value());
    EXPECT_FALSE(f.GetConstant(0x04000000u | 0x00FFFFFFu).has_value());
}

TEST(ReflectionDisassemblerTest, GetConstantParamParent) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Every mscorlib constant is field- or param-parented; one EventSource
    // ctor's optional `traits` parameter carries the null default (the raw
    // 0x12 Type column the C# reads as ConstantTypeCode.NullReference).
    std::uint32_t eventSourceType =
        FindTypeDefTokenIn(f, "System.Diagnostics.Tracing", "EventSource");
    ASSERT_NE(eventSourceType, 0u);
    const MD::ParameterInfo* traits = nullptr;
    MD::ParameterInfo traitsCopy;
    for (const auto& m : f.GetMethods(eventSourceType)) {
        for (const auto& p : f.GetParameters(m.Token)) {
            // Several EventSource methods carry a `traits` parameter; the
            // optional one (HasDefault) is the null-default carrier.
            if (p.Name == "traits"
                && (p.Attributes & 0x1000u) == 0x1000u) {
                traits = &p;
                traitsCopy = p;
            }
        }
    }
    ASSERT_NE(traits, nullptr);
    EXPECT_EQ(traitsCopy.SequenceNumber, 4);
    EXPECT_EQ(traitsCopy.Attributes, 0x1010u);  // Optional | HasDefault
    auto constant = f.GetConstant(traitsCopy.Token);
    ASSERT_TRUE(constant.has_value());
    EXPECT_EQ(constant->TypeCode, 0x12);  // NullReference (ELEMENT_TYPE_CLASS)
    EXPECT_EQ(constant->Value, (std::vector<std::uint8_t>{0, 0, 0, 0}));
    // A parameter without a default has no row.
    bool sawPlainParam = false;
    for (const auto& m : f.GetMethods(eventSourceType)) {
        for (const auto& p : f.GetParameters(m.Token)) {
            if (p.Name != "traits" && !f.GetConstant(p.Token).has_value()) {
                sawPlainParam = true;
            }
        }
    }
    EXPECT_TRUE(sawPlainParam);
    // mscorlib carries no property-parented constants; a Property token
    // resolves to nothing (the tag still decodes).
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t lengthProp = 0;
    for (const auto& pr : f.GetProperties(stringType)) {
        if (pr.Name == "Length") lengthProp = pr.Token;
    }
    ASSERT_NE(lengthProp, 0u);
    EXPECT_FALSE(f.GetConstant(lengthProp).has_value());
}

// ---------------------------------------------------------------------------
// MetadataFile::GetParameters (the Param table read behind the C#
// `methodDefinition.GetParameters()`): the MethodDef.ParamList rows with
// their Sequence/Flags/Name columns and the FieldMarshal blob.
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, GetParametersBasics) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    auto params = f.GetParameters(copy);
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].SequenceNumber, 1);
    EXPECT_EQ(params[0].Attributes, 0u);
    EXPECT_EQ(params[0].Name, "str");
    EXPECT_FALSE(params[0].MarshallingDescriptor.has_value());
    EXPECT_EQ(params[0].Token >> 24, 0x08u);
    EXPECT_NE(params[0].Token, 0u);

    // Int32.TryParse(String s, out Int32 result): the out parameter carries
    // the Out flag.
    std::uint32_t int32Type = FindTypeDefTokenIn(f, "System", "Int32");
    std::uint32_t tryParse = FindMethodIn(f, int32Type, "TryParse");
    ASSERT_NE(tryParse, 0u);
    params = f.GetParameters(tryParse);
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0].Name, "s");
    EXPECT_EQ(params[0].Attributes, 0u);
    EXPECT_EQ(params[1].Name, "result");
    EXPECT_EQ(params[1].Attributes, 0x02u);  // ParameterAttributes.Out
    EXPECT_EQ(params[1].SequenceNumber, 2);

    // A marshalled parameter: some mscorlib method carries a FieldMarshal
    // NativeType blob on a Param row (826 FieldMarshal rows exist). Scan a
    // bounded slice of methods for one.
    bool foundMarshal = false;
    std::uint32_t scanned = 0;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (++scanned > 4000) break;
            for (const auto& p : f.GetParameters(m.Token)) {
                if (p.MarshallingDescriptor.has_value()) {
                    foundMarshal = true;
                    EXPECT_FALSE(p.MarshallingDescriptor->empty());
                }
            }
        }
        if (foundMarshal || scanned > 4000) break;
    }
    EXPECT_TRUE(foundMarshal);

    // Non-MethodDef tokens yield nothing.
    EXPECT_TRUE(f.GetParameters(stringType).empty());
    EXPECT_TRUE(f.GetParameters(0x06000000).empty());
}

// ---------------------------------------------------------------------------
// WriteConstant (ReflectionDisassembler.cs lines 1220-1268): the II.23.2
// constant value render -- "nullref" for the NullReference code, the quoted
// string for String, and <il-type>(<value>) for the numeric codes with the
// NaN/infinity float/double bit patterns, and the invalid-typecode comment.
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, WriteConstantNumericMatrix) {
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x02, {0x01}));
    }), "bool(true)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x02, {0x00}));
    }), "bool(false)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x03, {0x41, 0x00}));
    }), "char(65)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x04, {0x80}));
    }), "int8(-128)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x05, {0xff}));
    }), "uint8(255)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x06, {0xff, 0x7f}));
    }), "int16(32767)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x07, {0xff, 0xff}));
    }), "uint16(65535)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x08, {0xff, 0xff, 0xff, 0x7f}));
    }), "int32(2147483647)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x08, {0x00, 0x00, 0x00, 0x80}));
    }), "int32(-2147483648)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x09, {0xff, 0xff, 0xff, 0xff}));
    }), "uint32(4294967295)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0a,
            {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f}));
    }), "int64(9223372036854775807)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0b,
            {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}));
    }), "uint64(18446744073709551615)");
}

TEST(ReflectionDisassemblerTest, WriteConstantFloatDoubleSpecials) {
    // NaN / infinities render the IEEE bit pattern; finite values the
    // round-trip format; negative zero keeps the WriteOperand '-0.0'.
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0c, {0x00, 0x00, 0xc0, 0xff}));
    }), "float32(0xffc00000)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0c, {0x00, 0x00, 0x80, 0x7f}));
    }), "float32(0x7f800000)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0c, {0x00, 0x00, 0x00, 0x00}));
    }), "float32(0.0)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0c, {0xff, 0xff, 0x7f, 0x7f}));
    }), "float32(3.4028235E+38)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0d,
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff}));
    }), "float64(0xfff8000000000000)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0d,
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x7f}));
    }), "float64(0x7ff0000000000000)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0d,
            {0x18, 0x2d, 0x44, 0x54, 0xfb, 0x21, 0x09, 0x40}));
    }), "float64(3.141592653589793)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0d,
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80}));
    }), "float64(-0.0)");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0d,
            {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xef, 0x7f}));
    }), "float64(1.7976931348623157E+308)");
}

TEST(ReflectionDisassemblerTest, WriteConstantStringNullrefAndInvalid) {
    // The String arm renders the quoted escaped literal (no type wrapper).
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0e,
            {'4', 0, '.', 0, '0', 0, '.', 0, '0', 0, '.', 0, '0', 0}));
    }), "\"4.0.0.0\"");
    // An odd trailing byte is dropped (ReadUTF16 takes byteCount/2 chars).
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x0e, {'h', 0, 'i', 0, 0x99}));
    }), "\"hi\"");
    // The NullReference code (the raw 0x12 slot) renders without touching
    // the blob.
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x12, {0x99, 0x99, 0x99, 0x99}));
    }), "nullref");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x12, {}));
    }), "nullref");
    // Unknown codes render the invalid-typecode comment with the raw value
    // (the C# enum ToString of an unnamed member).
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x00, {}));
    }), "/* Constant with invalid typecode: 0 */");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x18, {0, 0, 0, 0}));
    }), "/* Constant with invalid typecode: 24 */");
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x1c, {0, 0, 0, 0}));
    }), "/* Constant with invalid typecode: 28 */");
    // A truncated numeric blob fails inside ReadConstant and lands in the
    // same comment (the C# catch conflates the two).
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x08, {0x01, 0x02}));
    }), "/* Constant with invalid typecode: 8 */");
    // A truncated Boolean constant too.
    EXPECT_EQ(RenderWithDisassembler([](DA::ReflectionDisassembler& rd) {
        rd.WriteConstant(Constant(0x02, {}));
    }), "/* Constant with invalid typecode: 2 */");
}

TEST(ReflectionDisassemblerTest, WriteConstantRealMscorlibFields) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto render = [&](std::string_view ns, std::string_view typeName,
                     std::string_view fieldName) {
        std::uint32_t type = FindTypeDefTokenIn(f, ns, typeName);
        EXPECT_NE(type, 0u);
        std::uint32_t field = FindFieldIn(f, type, fieldName);
        EXPECT_NE(field, 0u);
        auto constant = f.GetConstant(field);
        EXPECT_TRUE(constant.has_value());
        return RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
            rd.WriteConstant(*constant);
        });
    };
    EXPECT_EQ(render("System", "Int32", "MaxValue"), "int32(2147483647)");
    EXPECT_EQ(render("System", "Char", "MaxValue"), "char(65535)");
    EXPECT_EQ(render("System", "Char", "MinValue"), "char(0)");
    EXPECT_EQ(render("System", "Byte", "MaxValue"), "uint8(255)");
    EXPECT_EQ(render("System", "SByte", "MinValue"), "int8(-128)");
    EXPECT_EQ(render("System", "Int16", "MaxValue"), "int16(32767)");
    EXPECT_EQ(render("System", "UInt16", "MaxValue"), "uint16(65535)");
    EXPECT_EQ(render("System", "UInt64", "MaxValue"),
        "uint64(18446744073709551615)");
    EXPECT_EQ(render("System", "Single", "NaN"), "float32(0xffc00000)");
    EXPECT_EQ(render("System", "Single", "PositiveInfinity"),
        "float32(0x7f800000)");
    EXPECT_EQ(render("System", "Single", "MaxValue"), "float32(3.4028235E+38)");
    EXPECT_EQ(render("System", "Double", "NaN"),
        "float64(0xfff8000000000000)");
    EXPECT_EQ(render("System", "Double", "PositiveInfinity"),
        "float64(0x7ff0000000000000)");
    EXPECT_EQ(render("System", "Double", "MaxValue"),
        "float64(1.7976931348623157E+308)");
    EXPECT_EQ(render("", "ThisAssembly", "Version"), "\"4.0.0.0\"");
    EXPECT_EQ(render("System.Security", "SecurityRuntime", "StackContinue"),
        "bool(true)");
    EXPECT_EQ(render("System.Security", "SecurityRuntime", "StackHalt"),
        "bool(false)");
}

// Every field constant in mscorlib renders one of the legal shapes (the
// full-fixture invariant sweep).
TEST(ReflectionDisassemblerTest, WriteConstantMscorlibInvariantSweep) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::size_t rendered = 0;
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    DA::ReflectionDisassembler rd(output);
    for (const auto& t : f.TypeDefs()) {
        for (const auto& fd : f.GetFields(t.Token)) {
            auto constant = f.GetConstant(fd.Token);
            if (!constant.has_value()) continue;
            stream.str("");
            stream.clear();
            rd.WriteConstant(*constant);
            std::string rendered2 = stream.str();
            ASSERT_FALSE(rendered2.empty());
            bool nullref = rendered2 == "nullref";
            bool comment = rendered2.rfind(
                "/* Constant with invalid typecode: ", 0) == 0;
            bool quoted = !rendered2.empty() && rendered2[0] == '"' &&
                rendered2[rendered2.size() - 1] == '"';
            bool typed = rendered2.find('(') != std::string::npos &&
                rendered2[rendered2.size() - 1] == ')';
            // Exactly one shape; none is a mix.
            int shapes = (nullref ? 1 : 0) + (comment ? 1 : 0) +
                (quoted ? 1 : 0) + (typed ? 1 : 0);
            ASSERT_EQ(shapes, 1);
            ++rendered;
        }
    }
    // The whole-table count from the raw Constant table probe: every
    // field-parented row is reachable through its Field token.
    EXPECT_EQ(rendered, 6245u);
}

// ---------------------------------------------------------------------------
// WriteParameters (ReflectionDisassembler.cs lines 1107-1160): the
// '( params )' list -- the sequence-driven walk with gap filling, the
// [in]/[out]/[opt] prefixes, the marshalling descriptor, and the unnamed
// param references.
// ---------------------------------------------------------------------------
TEST(ReflectionDisassemblerTest, WriteParametersSyntheticShapes) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // (int32, string), instance: the signature every drive below reuses. The
    // provider stays alive for the whole test -- the signature's deferred
    // writers capture it.
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    DA::DisassemblerSignatureTypeProvider provider(f, output);
    MD::SignatureTypeProviderDecoder decoder(provider, f);
    const std::uint8_t twoParam[] = {0x20, 0x02, 0x08, 0x08, 0x0e};
    MD::MethodSignatureT sig = decoder.DecodeMethodSignature(
        twoParam, sizeof(twoParam), MD::MetadataGenericContext{});
    ASSERT_EQ(sig.ParameterTypes.size(), 2u);
    ASSERT_EQ(sig.RequiredParameterCount, 2u);
    ASSERT_TRUE(sig.Header.IsInstance());
    // (int32, string, bool) for the gap case.
    const std::uint8_t threeParam[] = {0x20, 0x03, 0x08, 0x08, 0x0e, 0x02};
    MD::MethodSignatureT threeSig = decoder.DecodeMethodSignature(
        threeParam, sizeof(threeParam), MD::MetadataGenericContext{});
    ASSERT_EQ(threeSig.ParameterTypes.size(), 3u);

    auto write = [&](const std::vector<MD::ParameterInfo>& params,
                     const MD::MethodSignatureT& signature) {
        stream.str("");
        stream.clear();
        DA::ReflectionDisassembler rd(output);
        rd.WriteParameters(params, signature);
        return stream.str();
    };
    auto write2 = [&](const std::vector<MD::ParameterInfo>& params) {
        return write(params, sig);
    };

    // Two named parameters (PlainTextOutput writes the TextWriter CRLF).
    EXPECT_EQ(write2({Param(1, 0, "a"), Param(2, 0, "b")}),
        "int32 a,\r\nstring b\r\n");
    // The [in]/[out]/[opt] attribute prefixes.
    EXPECT_EQ(write2({Param(1, 0x1, "a"), Param(2, 0x2, "b")}),
        "[in] int32 a,\r\n[out] string b\r\n");
    // [opt] on the first row leaves the second unnamed tail.
    EXPECT_EQ(write2({Param(1, 0x10, "a")}),
        "[opt] int32 a,\r\nstring ''\r\n");
    // An unnamed parameter renders the '' reference (and the remaining
    // signature slots follow as unnamed parameters).
    EXPECT_EQ(write2({Param(1, 0, "")}), "int32 '',\r\nstring ''\r\n");
    // A gap in the sequence fills the missing slot with the '' reference
    // (over the 3-param signature: the middle slot fills, the row renders
    // at its own slot).
    EXPECT_EQ(write({Param(1, 0, "a"), Param(3, 0, "c")}, threeSig),
        "int32 a,\r\nstring '',\r\nbool c\r\n");
    // A missing leading row fills from slot 0.
    EXPECT_EQ(write2({Param(2, 0, "b")}),
        "int32 '',\r\nstring b\r\n");
    // The sequence-0 return row is skipped.
    EXPECT_EQ(write2({Param(0, 0, ""), Param(1, 0, "a"), Param(2, 0, "b")}),
        "int32 a,\r\nstring b\r\n");
    // A signature shorter than the rows is the C# IndexOutOfRange crash;
    // the port throws std::out_of_range (loud rather than wrong).
    {
        DA::ReflectionDisassembler rd(output);
        EXPECT_THROW(
            rd.WriteParameters({Param(1, 0, "a"), Param(2, 0, "b"),
                Param(3, 0, "c")}, sig),
            std::out_of_range);
    }
    // Unnamed tail parameters: the remaining-signature loop.
    const std::uint8_t oneParam[] = {0x20, 0x01, 0x08, 0x08};
    MD::MethodSignatureT oneSig = decoder.DecodeMethodSignature(
        oneParam, sizeof(oneParam), MD::MetadataGenericContext{});
    EXPECT_EQ(write({Param(1, 0, "a")}, oneSig), "int32 a\r\n");
    EXPECT_EQ(write({}, oneSig), "int32 ''\r\n");
    // A marshalling descriptor renders between the type and the name.
    MD::ParameterInfo marshalled = Param(1, 0, "p");
    marshalled.MarshallingDescriptor = std::vector<std::uint8_t>{0x15};
    EXPECT_EQ(write({marshalled}, oneSig), "int32 marshal(lpwstr) p\r\n");
}

TEST(ReflectionDisassemblerTest, WriteParametersRealMscorlibMethods) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);

    auto render = [&](std::uint32_t methodToken) {
        stream.str("");
        stream.clear();
        MethodSignatureHolder holder = MethodSignatureOf(f, methodToken, output);
        DA::ReflectionDisassembler rd(output);
        rd.WriteParameters(f.GetParameters(methodToken), holder.sig);
        return stream.str();
    };

    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    EXPECT_EQ(render(copy), "string str\r\n");

    std::uint32_t int32Type = FindTypeDefTokenIn(f, "System", "Int32");
    std::uint32_t tryParse = FindMethodIn(f, int32Type, "TryParse");
    ASSERT_NE(tryParse, 0u);
    // The out parameter is byref: "int32&" at signature syntax.
    EXPECT_EQ(render(tryParse), "string s,\r\n[out] int32& result\r\n");

    // Every method over every type renders without throwing (bounded sweep).
    std::size_t rendered = 0;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            stream.str("");
            stream.clear();
            MethodSignatureHolder holder =
                MethodSignatureOf(f, m.Token, output);
            DA::ReflectionDisassembler rd(output);
            rd.WriteParameters(f.GetParameters(m.Token), holder.sig);
            ++rendered;
        }
    }
    EXPECT_GT(rendered, 5000u);
}


// ---------------------------------------------------------------------------
// WriteAttributes / WriteGenericParametersAndAttributes /
// WriteParameterAttributes (ReflectionDisassembler.cs lines 1851-1872,
// 1175-1200, 1202-1218): the ".custom" attribute lines every member header
// embeds, the ".param type"/".param constraint" generic-parameter blocks, and
// the ".param [N]" parameter blocks. Fixtures pin the exact renders the
// ilspycmd -il gold output shows for the same rows (mscorlib's
// AggregateException .ctors, System.Exception/EventSource, System.dll's
// IInternetSecurityManager MemberRef-ctor attributes, and the .NET 10
// System.Private.CoreLib nullable-annotation rows -- the .NET Framework 4.8
// mscorlib carries no generic-parameter or constraint attributes at all).
// ---------------------------------------------------------------------------

namespace {

#if defined(_WIN32)
// The newest installed .NET shared runtime's System.Private.CoreLib.dll --
// the only local fixture carrying custom attributes on generic parameters
// and constraint rows (C# 8+ nullable annotations; the 4.8 framework
// assemblies have none, verified over the full table).
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
#else
std::string CoreLibPath() { return "/usr/share/dotnet/shared/System.Private.CoreLib.dll"; }
#endif

std::string Hex8(std::uint32_t token) {
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(token));
    return buf;
}

// The ".custom <ctor> = ( blob )" line over an already-rendered ctor string
// (the ".custom " prefix + the space-separated blob inside parens, the bytes
// at one indent level, 16 per line).
std::string CustomLine(const std::string& ctorRender,
    const std::vector<std::uint8_t>& blob) {
    std::string line = ".custom " + ctorRender + " = (";
    if (!blob.empty()) {
        line += "\r\n";
        for (std::size_t i = 0; i < blob.size(); ++i) {
            if (i > 0) {
                // A newline before each 16th byte except the last (the
                // WriteBlob geometry); every other byte gets a space.
                line += (i % 16 == 0 && i < blob.size() - 1) ? "\r\n\t" : " ";
            } else {
                line += '\t';
            }
            char buf[3];
            std::snprintf(buf, sizeof(buf), "%02x", blob[i]);
            line += buf;
        }
        line += "\r\n";
    }
    line += ")\r\n";
    return line;
}

// A test EntityProcessor that reverses the collection (the visible routing
// fixture -- SortByNameProcessor's key order over the attribute sets below
// already equals the row order, so it cannot show the hook).
class ReversingEntityProcessor : public DA::IEntityProcessor {
public:
    std::vector<std::uint32_t> Process(const MD::MetadataFile&,
        const std::vector<std::uint32_t>& items,
        DA::ProcessedEntityKind) const override {
        return std::vector<std::uint32_t>(items.rbegin(), items.rend());
    }
};

}  // namespace

TEST(ReflectionDisassemblerTest, WriteAttributesRendersCustomAttributeLine) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t aggregateType = FindTypeDefTokenIn(f, "System", "AggregateException");
    ASSERT_NE(aggregateType, 0u);
    // The first .ctor with exactly one method attribute (the
    // __DynamicallyInvokableAttribute row the -il gold shows for these).
    std::uint32_t method = 0;
    for (const auto& m : f.GetMethods(aggregateType)) {
        if (f.GetCustomAttributeTokens(m.Token).size() == 1) { method = m.Token; break; }
    }
    ASSERT_NE(method, 0u);
    auto tokens = f.GetCustomAttributeTokens(method);
    ASSERT_EQ(tokens.size(), 1u);

    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    rd.WriteAttributes(f, tokens);
    EXPECT_EQ(output.ToString(),
        CustomLine("instance void __DynamicallyInvokableAttribute::.ctor()",
            Bytes({0x01, 0x00, 0x00, 0x00})));
}

TEST(ReflectionDisassemblerTest, WriteAttributesShowsMetadataTokenComment) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t aggregateType = FindTypeDefTokenIn(f, "System", "AggregateException");
    ASSERT_NE(aggregateType, 0u);
    std::uint32_t method = 0;
    for (const auto& m : f.GetMethods(aggregateType)) {
        if (f.GetCustomAttributeTokens(m.Token).size() == 1) { method = m.Token; break; }
    }
    ASSERT_NE(method, 0u);
    auto tokens = f.GetCustomAttributeTokens(method);
    ASSERT_EQ(tokens.size(), 1u);

    OUT::PlainTextOutput output;
    DA::ReflectionDisassembler rd(output);
    rd.ShowMetadataTokens(true);
    rd.WriteAttributes(f, tokens);
    // The "/* XXXXXXXX */ " comment between ".custom " and the ctor render.
    EXPECT_EQ(output.ToString(),
        ".custom /* " + Hex8(tokens[0]) + " */ instance void __DynamicallyInvokableAttribute::.ctor() = (\r\n"
        "\t01 00 00 00\r\n)\r\n");
}

TEST(ReflectionDisassemblerTest, WriteAttributesMemberRefCtorAndEntityProcessorRouting) {
    MD::MetadataFile f("C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll");
    ASSERT_TRUE(f.IsValid());
    std::uint32_t type = FindTypeDefTokenIn(f, "Microsoft.Win32", "IInternetSecurityManager");
    ASSERT_NE(type, 0u);
    auto tokens = f.GetCustomAttributeTokens(type);
    ASSERT_EQ(tokens.size(), 3u);

    // The three gold -il lines, in row order (no processor):
    // [ComVisible(false)], [Guid("7 9eac9ee-baf9-11ce-8c82-00aa004ba90b")],
    // [InterfaceType(ComInterfaceType)] -- the ctors are MemberRefs into
    // mscorlib (the "[mscorlib]" scope prefix).
    const std::string comVisible = CustomLine(
        "instance void [mscorlib]System.Runtime.InteropServices.ComVisibleAttribute::.ctor(bool)",
        Bytes({0x01, 0x00, 0x00, 0x00, 0x00}));
    const std::string guid = CustomLine(
        "instance void [mscorlib]System.Runtime.InteropServices.GuidAttribute::.ctor(string)",
        Bytes({0x01, 0x00, 0x24, 0x37, 0x39, 0x65, 0x61, 0x63, 0x39, 0x65, 0x65,
               0x2d, 0x62, 0x61, 0x66, 0x39, 0x2d, 0x31, 0x31, 0x63, 0x65, 0x2d,
               0x38, 0x63, 0x38, 0x32, 0x2d, 0x30, 0x30, 0x61, 0x61, 0x30, 0x30,
               0x34, 0x62, 0x61, 0x39, 0x30, 0x62, 0x00, 0x00}));
    const std::string interfaceType = CustomLine(
        "instance void [mscorlib]System.Runtime.InteropServices.InterfaceTypeAttribute::"
        ".ctor(valuetype [mscorlib]System.Runtime.InteropServices.ComInterfaceType)",
        Bytes({0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}));

    {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteAttributes(f, tokens);
        EXPECT_EQ(output.ToString(), comVisible + guid + interfaceType);
    }
    {
        // The EntityProcessor hook routes the collection (reversed order).
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        ReversingEntityProcessor processor;
        rd.EntityProcessor(&processor);
        rd.WriteAttributes(f, tokens);
        EXPECT_EQ(output.ToString(), interfaceType + guid + comVisible);
    }
}

TEST(ReflectionDisassemblerTest, WriteAttributesDecodeBlobsFlagAndInvalidToken) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t aggregateType = FindTypeDefTokenIn(f, "System", "AggregateException");
    ASSERT_NE(aggregateType, 0u);
    std::uint32_t method = 0;
    for (const auto& m : f.GetMethods(aggregateType)) {
        if (f.GetCustomAttributeTokens(m.Token).size() == 1) { method = m.Token; break; }
    }
    ASSERT_NE(method, 0u);
    auto tokens = f.GetCustomAttributeTokens(method);
    ASSERT_EQ(tokens.size(), 1u);

    {
        // With the flag off (the default) the blob is the raw hex dump
        // (pinned by the other tests); with DecodeCustomAttributeBlobs on,
        // the unported WriteDecodedCustomAttributeBlob path is loud rather
        // than wrong.
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteAttributes(f, tokens);
        EXPECT_EQ(output.ToString(),
            CustomLine("instance void __DynamicallyInvokableAttribute::.ctor()",
                Bytes({0x01, 0x00, 0x00, 0x00})));
        rd.DecodeCustomAttributeBlobs = true;
        EXPECT_THROW(rd.WriteAttributes(f, tokens), std::logic_error);
    }
    {
        // An out-of-range attribute token (the C#
        // metadata.GetCustomAttribute(handle) throws for an invalid handle).
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        EXPECT_THROW(rd.WriteAttributes(f, {0x0CFFFFFFu}), std::out_of_range);
    }
}

TEST(ReflectionDisassemblerTest, WriteParameterAttributesConstantAndAttributeShapes) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // AggregateException..ctor(params Exception[] innerExceptions): the
    // param [1] row carries [ParamArray] (the gold ".param [1]" block).
    std::uint32_t aggregateType = FindTypeDefTokenIn(f, "System", "AggregateException");
    ASSERT_NE(aggregateType, 0u);
    bool found = false;
    for (const auto& m : f.GetMethods(aggregateType)) {
        for (const auto& p : f.GetParameters(m.Token)) {
            if (p.SequenceNumber != 1 || p.Name != "innerExceptions") continue;
            if (f.GetCustomAttributeTokens(p.Token).empty()) continue;
            OUT::PlainTextOutput output;
            DA::ReflectionDisassembler rd(output);
            rd.WriteParameterAttributes(f, p);
            EXPECT_EQ(output.ToString(),
                ".param [1]\r\n\t.custom instance void System.ParamArrayAttribute::.ctor() = (\r\n"
                "\t\t01 00 00 00\r\n\t)\r\n");
            found = true;
        }
    }
    // A plain parameter (String.Copy's str) has neither a default nor
    // attributes: the member writes nothing at all.
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    for (const auto& p : f.GetParameters(copy)) {
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteParameterAttributes(f, p);
        EXPECT_EQ(output.ToString(), "");
    }
    EXPECT_TRUE(found);

    // System.Exception::AddExceptionDataForRestrictedErrorInfo: the [opt]
    // hasrestrictedLanguageErrorObject default renders inline.
    std::uint32_t exceptionType = FindTypeDefTokenIn(f, "System", "Exception");
    ASSERT_NE(exceptionType, 0u);
    std::uint32_t addData = FindMethodIn(f, exceptionType, "AddExceptionDataForRestrictedErrorInfo");
    ASSERT_NE(addData, 0u);
    for (const auto& p : f.GetParameters(addData)) {
        if (p.SequenceNumber != 5) continue;
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteParameterAttributes(f, p);
        EXPECT_EQ(output.ToString(), ".param [5] = bool(false)\r\n");
    }

    // EventSource..ctor(..., [opt] string[] traits = null): the nullref
    // default (the ELEMENT_TYPE_CLASS-encoded null constant).
    std::uint32_t eventSourceType = FindTypeDefTokenIn(f, "System.Diagnostics.Tracing", "EventSource");
    ASSERT_NE(eventSourceType, 0u);
    for (const auto& m : f.GetMethods(eventSourceType)) {
        for (const auto& p : f.GetParameters(m.Token)) {
            if (p.Name != "traits" || !f.GetConstant(p.Token).has_value()) continue;
            OUT::PlainTextOutput output;
            DA::ReflectionDisassembler rd(output);
            rd.WriteParameterAttributes(f, p);
            EXPECT_EQ(output.ToString(),
                ".param [" + std::to_string(p.SequenceNumber) + "] = nullref\r\n");
        }
    }
}

TEST(ReflectionDisassemblerTest, WriteGenericParametersAndAttributesEmptyForPlainRows) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // The 4.8 mscorlib carries NO custom attributes on generic parameters or
    // constraint rows (verified over the full tables): every generic
    // parameter renders nothing. Sweep the generic parameters of the first
    // types that carry them.
    std::size_t driven = 0;
    for (const auto& t : f.TypeDefs()) {
        auto genericParameters = f.GetGenericParameters(t.Token);
        if (genericParameters.empty()) continue;
        MD::MetadataGenericContext context = MD::MetadataGenericContext::ForType(t.Token, f);
        for (const auto& gp : genericParameters) {
            OUT::PlainTextOutput output;
            DA::ReflectionDisassembler rd(output);
            rd.WriteGenericParametersAndAttributes(f, context, gp.Token);
            EXPECT_EQ(output.ToString(), "");
            ++driven;
        }
        if (driven >= 300) break;
    }
    EXPECT_GT(driven, 100u);
}

TEST(ReflectionDisassemblerTest, CoreLibGenericParameterAndConstraintBlocks) {
    std::string path = CoreLibPath();
    if (path.empty()) {
        GTEST_SKIP() << "no .NET shared runtime System.Private.CoreLib.dll";
    }
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // System.Array::AsReadOnly<T>(T[] array): the method's T row carries
    // [Nullable(2)] -- the gold ".param type T" block.
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    ASSERT_NE(arrayType, 0u);
    std::uint32_t asReadOnly = FindMethodIn(f, arrayType, "AsReadOnly");
    ASSERT_NE(asReadOnly, 0u);
    {
        auto genericParameters = f.GetGenericParameters(asReadOnly);
        ASSERT_EQ(genericParameters.size(), 1u);
        EXPECT_EQ(genericParameters[0].Name, "T");
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteGenericParametersAndAttributes(f,
            MD::MetadataGenericContext::ForMethod(asReadOnly, f),
            genericParameters[0].Token);
        EXPECT_EQ(output.ToString(),
            ".param type T\r\n"
            "\t.custom instance void System.Runtime.CompilerServices.NullableAttribute::.ctor(uint8) = (\r\n"
            "\t\t01 00 02 00 00\r\n\t)\r\n");
    }

    // System.Delegate::EnumerateInvocationList<TDelegate>(TDelegate d) where
    // TDelegate : Delegate: the TDelegate row itself has no attributes, but
    // its constraint row does -- the gold ".param constraint" block.
    std::uint32_t delegateType = FindTypeDefTokenIn(f, "System", "Delegate");
    ASSERT_NE(delegateType, 0u);
    std::uint32_t enumerate = FindMethodIn(f, delegateType, "EnumerateInvocationList");
    ASSERT_NE(enumerate, 0u);
    {
        auto genericParameters = f.GetGenericParameters(enumerate);
        ASSERT_EQ(genericParameters.size(), 1u);
        auto constraints = f.GetGenericParameterConstraints(genericParameters[0].Token);
        ASSERT_EQ(constraints.size(), 1u);
        EXPECT_EQ(constraints[0].TypeToken, 0x02000088u);  // System.Delegate
        OUT::PlainTextOutput output;
        DA::ReflectionDisassembler rd(output);
        rd.WriteGenericParametersAndAttributes(f,
            MD::MetadataGenericContext::ForMethod(enumerate, f),
            genericParameters[0].Token);
        EXPECT_EQ(output.ToString(),
            ".param constraint TDelegate, System.Delegate\r\n"
            "\t.custom instance void System.Runtime.CompilerServices.NullableAttribute::.ctor(uint8) = (\r\n"
            "\t\t01 00 01 00 00\r\n\t)\r\n");
    }

    // The same method's param rows: the seq-0 RETURN row carries
    // [Nullable((byte[])...)] (the gold ".param [0]" block -- the caller
    // passes every Param row, the seq-0 skip is WriteParameters' rule).
    {
        auto rows = f.GetParameters(enumerate);
        ASSERT_EQ(rows.size(), 2u);
        {
            OUT::PlainTextOutput output;
            DA::ReflectionDisassembler rd(output);
            rd.WriteParameterAttributes(f, rows[0]);
            EXPECT_EQ(rows[0].SequenceNumber, 0u);
            EXPECT_EQ(output.ToString(),
                ".param [0]\r\n"
                "\t.custom instance void System.Runtime.CompilerServices.NullableAttribute::.ctor(uint8[]) = (\r\n"
                "\t\t01 00 02 00 00 00 00 01 00 00\r\n\t)\r\n");
        }
        {
            OUT::PlainTextOutput output;
            DA::ReflectionDisassembler rd(output);
            rd.WriteParameterAttributes(f, rows[1]);
            EXPECT_EQ(output.ToString(),
                ".param [1]\r\n"
                "\t.custom instance void System.Runtime.CompilerServices.NullableAttribute::.ctor(uint8) = (\r\n"
                "\t\t01 00 02 00 00\r\n\t)\r\n");
        }
    }

    // System.Decimal::TryFormat(..., [opt] ReadOnlySpan<char> format = null,
    // [opt] IFormatProvider provider = null): the format row carries BOTH a
    // nullref default and [StringSyntax("NumericFormat")] -- the gold
    // ".param [3] = nullref" with the attribute block inside.
    std::uint32_t decimalType = FindTypeDefTokenIn(f, "System", "Decimal");
    ASSERT_NE(decimalType, 0u);
    bool found = false;
    for (const auto& m : f.GetMethods(decimalType)) {
        if (m.Name != "TryFormat") continue;
        for (const auto& p : f.GetParameters(m.Token)) {
            if (p.Name != "format" || !f.GetConstant(p.Token).has_value()) continue;
            if (f.GetCustomAttributeTokens(p.Token).empty()) continue;
            OUT::PlainTextOutput output;
            DA::ReflectionDisassembler rd(output);
            rd.WriteParameterAttributes(f, p);
            EXPECT_EQ(output.ToString(),
                ".param [3] = nullref\r\n"
                "\t.custom instance void System.Diagnostics.CodeAnalysis.StringSyntaxAttribute::.ctor(string) = (\r\n"
                "\t\t01 00 0d 4e 75 6d 65 72 69 63 46 6f 72 6d 61 74\r\n"
                "\t\t00 00\r\n\t)\r\n");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(ReflectionDisassemblerTest, CoreLibAttributeWritersSweep) {
    std::string path = CoreLibPath();
    if (path.empty()) {
        GTEST_SKIP() << "no .NET shared runtime System.Private.CoreLib.dll";
    }
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // Drive the three writers over every generic parameter and Param row of
    // a few large types: no throw, and every output line carries the known
    // block prefixes.
    std::size_t attributeLines = 0;
    std::size_t driven = 0;
    auto checkLines = [&](const std::string& text) {
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t eol = text.find("\r\n", pos);
            if (eol == std::string::npos) break;
            std::string line = text.substr(pos, eol - pos);
            pos = eol + 2;
            if (line.empty()) continue;
            bool known = line.find(".param type ") == 0
                || line.find(".param constraint ") == 0
                || line.find(".param [") == 0
                || line.find(".custom ") == 0
                || line.find("\t.custom ") == 0
                || line.find("\t\t") == 0
                || line == "\t)"
                || line.find('\t') == std::string::npos;
            EXPECT_TRUE(known) << "unexpected line: " << line;
            if (line.find(".custom ") != std::string::npos) ++attributeLines;
        }
    };
    for (const char* typeName : {"String", "Decimal", "Delegate", "Array"}) {
        std::uint32_t type = FindTypeDefTokenIn(f, "System", typeName);
        ASSERT_NE(type, 0u) << typeName;
        for (const auto& m : f.GetMethods(type)) {
            auto genericParameters = f.GetGenericParameters(m.Token);
            if (!genericParameters.empty()) {
                MD::MetadataGenericContext context =
                    MD::MetadataGenericContext::ForMethod(m.Token, f);
                for (const auto& gp : genericParameters) {
                    OUT::PlainTextOutput output;
                    DA::ReflectionDisassembler rd(output);
                    rd.WriteGenericParametersAndAttributes(f, context, gp.Token);
                    checkLines(output.ToString());
                    ++driven;
                }
            }
            for (const auto& p : f.GetParameters(m.Token)) {
                OUT::PlainTextOutput output;
                DA::ReflectionDisassembler rd(output);
                rd.WriteParameterAttributes(f, p);
                checkLines(output.ToString());
                ++driven;
            }
        }
    }
    EXPECT_GT(driven, 100u);
    EXPECT_GT(attributeLines, 100u);
}

// ---------------------------------------------------------------------------
// The metadata reads behind the method chain: GetMethodImplAttributes,
// GetMethodImport (the ImplMap row), GetMethodImplementations (the MethodImpl
// rows), GetDeclarativeSecurityAttributes (the DeclSecurity rows). The
// fixtures are mscorlib rows the gold ilspycmd -il output pins: the
// PreserveSig pinvoke imports, the Array explicit-interface-implementation
// override, and the RegistryKey::get_Handle demand permission set.
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, GetMethodImplAttributesCilAndPreserveSig)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    // String::Copy: IL + Managed (both the zero defaults) and nothing else.
    EXPECT_EQ(f.GetMethodImplAttributes(copy), 0u);

    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    std::uint32_t localAlloc = FindMethodIn(f, win32, "LocalAlloc_NoSafeHandle");
    ASSERT_NE(localAlloc, 0u);
    // The pinvoke preserve-sig imports carry PreserveSig (0x0080).
    EXPECT_EQ(f.GetMethodImplAttributes(localAlloc), 0x80u);
}

TEST(ReflectionDisassemblerTest, GetMethodImportPinvokeRows)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");

    // GetSystemInfo: SetLastError (0x40) + WinApi (0x100); the import name is
    // the declared name, so the gold header shows no "as" alias.
    std::uint32_t getSystemInfo = FindMethodIn(f, win32, "GetSystemInfo");
    ASSERT_NE(getSystemInfo, 0u);
    auto info = f.GetMethodImport(getSystemInfo);
    ASSERT_TRUE(info.has_value());
    auto moduleName = f.GetModuleReferenceName(info->ModuleRefToken);
    ASSERT_TRUE(moduleName.has_value());
    EXPECT_EQ(*moduleName, "kernel32.dll");
    EXPECT_TRUE(info->Name.has_value());
    EXPECT_EQ(*info->Name, "GetSystemInfo");
    EXPECT_EQ(info->Attributes, 0x140u);

    // FormatMessage: CharSetAuto (0x06) + BestFitMappingEnable (0x10, the
    // raw row bit the header render never spells) + WinApi (0x100).
    std::uint32_t formatMessage = FindMethodIn(f, win32, "FormatMessage");
    ASSERT_NE(formatMessage, 0u);
    info = f.GetMethodImport(formatMessage);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->Attributes, 0x116u);

    // LocalAlloc_NoSafeHandle: the aliased import -- the gold header spells
    // pinvokeimpl("kernel32.dll" as "LocalAlloc" winapi).
    std::uint32_t localAlloc = FindMethodIn(f, win32, "LocalAlloc_NoSafeHandle");
    ASSERT_NE(localAlloc, 0u);
    info = f.GetMethodImport(localAlloc);
    ASSERT_TRUE(info.has_value());
    ASSERT_TRUE(info->Name.has_value());
    EXPECT_EQ(*info->Name, "LocalAlloc");
    EXPECT_EQ(info->Attributes, 0x100u);

    // A method without a pinvoke import has no ImplMap row.
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    EXPECT_FALSE(f.GetMethodImport(copy).has_value());
}

TEST(ReflectionDisassemblerTest, GetMethodImplementationsOverrideRows)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    std::uint32_t getCount = FindMethodIn(f, arrayType,
        "System.Collections.ICollection.get_Count");
    ASSERT_NE(getCount, 0u);
    auto impls = f.GetMethodImplementations(getCount);
    ASSERT_EQ(impls.size(), 1u);
    // The declaration is System.Collections.ICollection::get_Count -- a
    // MethodDef in mscorlib itself (the gold .override line renders it).
    EXPECT_EQ(impls[0].MethodDeclarationToken >> 24, 0x06u);
    EXPECT_EQ(f.GetMethodName(impls[0].MethodDeclarationToken), "get_Count");

    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    EXPECT_TRUE(f.GetMethodImplementations(copy).empty());
}

TEST(ReflectionDisassemblerTest, GetDeclarativeSecurityAttributesDemand)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t registryKey = FindTypeDefTokenIn(f, "Microsoft.Win32",
        "RegistryKey");
    std::uint32_t getHandle = FindMethodIn(f, registryKey, "get_Handle");
    ASSERT_NE(getHandle, 0u);
    auto rows = f.GetDeclarativeSecurityAttributes(getHandle);
    ASSERT_EQ(rows.size(), 1u);
    // The DeclarativeSecurityAction.Demand row with the SecurityPermission
    // blob the gold .permissionset line dumps (201 bytes, ".\x01\x80\x84..."
    // -- the XML-compressed permission set).
    EXPECT_EQ(rows[0].Action, 2u);
    ASSERT_EQ(rows[0].PermissionSet.size(), 201u);
    EXPECT_EQ(rows[0].PermissionSet[0], 0x2eu);
    EXPECT_EQ(rows[0].PermissionSet[1], 0x01u);
    EXPECT_EQ(rows[0].PermissionSet[2], 0x80u);
    EXPECT_EQ(rows[0].PermissionSet[3], 0x84u);
    EXPECT_EQ(rows[0].PermissionSet[200], 0x00u);

    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    EXPECT_TRUE(f.GetDeclarativeSecurityAttributes(copy).empty());
}

// ---------------------------------------------------------------------------
// WriteTypeParameters (ReflectionDisassembler.cs lines 1755-1802): the
// generic-parameter list render, over real mscorlib rows the gold output
// pins (the Nullable`1 class header's valuetype .ctor constraint pair, the
// variance prefixes, and Enum::TryParse<TEnum>).
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, WriteTypeParametersConstraintAndVariance)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    DA::ReflectionDisassembler rd(output);

    // Nullable`1: <valuetype .ctor (System.ValueType) T> (the class-header
    // gold shape, driven here at the type context).
    std::uint32_t nullable = FindTypeDefTokenIn(f, "System", "Nullable`1");
    ASSERT_NE(nullable, 0u);
    stream.str("");
    rd.WriteTypeParameters(output, f,
        MD::MetadataGenericContext::ForType(nullable, f),
        f.GetGenericParameters(nullable));
    EXPECT_EQ(stream.str(), "<valuetype .ctor (System.ValueType) T>");

    // IEnumerable`1: <+T> (the covariant prefix).
    std::uint32_t enumerable = FindTypeDefTokenIn(f, "System.Collections.Generic",
        "IEnumerable`1");
    ASSERT_NE(enumerable, 0u);
    stream.str("");
    rd.WriteTypeParameters(output, f,
        MD::MetadataGenericContext::ForType(enumerable, f),
        f.GetGenericParameters(enumerable));
    EXPECT_EQ(stream.str(), "<+T>");

    // Action`1: <-T> (the contravariant prefix).
    std::uint32_t action = FindTypeDefTokenIn(f, "System", "Action`1");
    ASSERT_NE(action, 0u);
    stream.str("");
    rd.WriteTypeParameters(output, f,
        MD::MetadataGenericContext::ForType(action, f),
        f.GetGenericParameters(action));
    EXPECT_EQ(stream.str(), "<-T>");

    // A non-generic owner renders nothing.
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    stream.str("");
    rd.WriteTypeParameters(output, f,
        MD::MetadataGenericContext::ForType(stringType, f),
        f.GetGenericParameters(stringType));
    EXPECT_EQ(stream.str(), "");
}

TEST(ReflectionDisassemblerTest, WriteTypeParametersGenericMethod)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t enumType = FindTypeDefTokenIn(f, "System", "Enum");
    ASSERT_NE(enumType, 0u);
    std::uint32_t tryParse = FindMethodIn(f, enumType, "TryParse");
    ASSERT_NE(tryParse, 0u);
    auto genericParameters = f.GetGenericParameters(tryParse);
    ASSERT_EQ(genericParameters.size(), 1u);

    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    DA::ReflectionDisassembler rd(output);
    rd.WriteTypeParameters(output, f,
        MD::MetadataGenericContext::ForMethod(tryParse, f), genericParameters);
    // The gold header of Enum::TryParse: <valuetype .ctor (System.ValueType)
    // TEnum>.
    EXPECT_EQ(stream.str(), "<valuetype .ctor (System.ValueType) TEnum>");
}

// ---------------------------------------------------------------------------
// The method header chain (ReflectionDisassembler.cs lines 153-318): the
// exact header renders, de-indented one level from the gold ilspycmd -il
// module dump (which renders methods inside their type). The PlainTextOutput
// writes CRLF line endings.
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, DisassembleMethodHeaderStaticMethod)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleMethodHeader(f, copy);
    });
    // The header ends mid-line (no trailing newline): the C# header writer
    // leaves the line for the caller's OpenBlock to terminate.
    EXPECT_EQ(actual,
        ".method public hidebysig static \r\n"
        "\tstring Copy (\r\n"
        "\t\tstring str\r\n"
        "\t) cil managed ");
}

TEST(ReflectionDisassemblerTest, DisassembleMethodHeaderInstanceEmptyParams)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringComparer = FindTypeDefTokenIn(f, "System",
        "StringComparer");
    ASSERT_NE(stringComparer, 0u);
    std::uint32_t ctor = FindMethodIn(f, stringComparer, ".ctor");
    ASSERT_NE(ctor, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleMethodHeader(f, ctor);
    });
    // The family-visibility WriteEnum spelling and the empty "()" parameter
    // list (no indented parameter block at all).
    EXPECT_EQ(actual,
        ".method family hidebysig specialname rtspecialname \r\n"
        "\tinstance void .ctor () cil managed ");
}

TEST(ReflectionDisassemblerTest, DisassembleMethodHeaderGenericConstraints)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t enumType = FindTypeDefTokenIn(f, "System", "Enum");
    std::uint32_t tryParse = FindMethodIn(f, enumType, "TryParse");
    ASSERT_NE(tryParse, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleMethodHeader(f, tryParse);
    });
    // The gold header of Enum::TryParse<TEnum>: the generic-method MVAR
    // parameter types (!!TEnum&) and the escaped 'value' parameter name.
    EXPECT_EQ(actual,
        ".method public hidebysig static \r\n"
        "\tbool TryParse<valuetype .ctor (System.ValueType) TEnum> (\r\n"
        "\t\tstring 'value',\r\n"
        "\t\t[out] !!TEnum& result\r\n"
        "\t) cil managed ");
}

TEST(ReflectionDisassemblerTest, DisassembleMethodHeaderPinvokeImpl)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    std::uint32_t getSystemInfo = FindMethodIn(f, win32, "GetSystemInfo");
    ASSERT_NE(getSystemInfo, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleMethodHeader(f, getSystemInfo);
    });
    // The pinvokeimpl arm over the ImplMap row, the preservesig
    // methodImpl flag, and the nested SYSTEM_INFO& parameter type.
    EXPECT_EQ(actual,
        ".method assembly hidebysig static pinvokeimpl(\"kernel32.dll\" lasterr winapi) \r\n"
        "\tvoid GetSystemInfo (\r\n"
        "\t\tvaluetype Microsoft.Win32.Win32Native/SYSTEM_INFO& lpSystemInfo\r\n"
        "\t) cil managed preservesig ");
}

TEST(ReflectionDisassemblerTest, DisassembleMethodHeaderMetadataTokenComment)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.ShowMetadataTokens(true);
        rd.DisassembleMethodHeader(f, copy);
    });
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/* %08X */", copy);
    // The WriteMetadataToken comment with spaceAfter, after the ".method"
    // reference and its space.
    EXPECT_EQ(actual,
        ".method " + std::string(buf) +
        " public hidebysig static \r\n"
        "\tstring Copy (\r\n"
        "\t\tstring str\r\n"
        "\t) cil managed ");
}

// ---------------------------------------------------------------------------
// DisassembleMethodBlock and the full DisassembleMethod render: the gold
// blocks for the no-body pinvoke alias (the .custom + close comment), the
// explicit-interface override with a body, and the .permissionset raw dump.
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, DisassembleMethodFullNoBodyPinvokeAlias)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    std::uint32_t localAlloc = FindMethodIn(f, win32, "LocalAlloc_NoSafeHandle");
    ASSERT_NE(localAlloc, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleMethod(f, localAlloc);
    });
    // The gold block, de-indented: the aliased pinvokeimpl header, the
    // typed ReliabilityContractAttribute custom attribute, no body (the
    // pinvoke-only method has no RVA), and the close comment.
    EXPECT_EQ(actual,
        ".method assembly hidebysig static pinvokeimpl(\"kernel32.dll\" as \"LocalAlloc\" winapi) \r\n"
        "\tnative int LocalAlloc_NoSafeHandle (\r\n"
        "\t\tint32 uFlags,\r\n"
        "\t\tnative uint sizetdwBytes\r\n"
        "\t) cil managed preservesig \r\n"
        "{\r\n"
        "\t.custom instance void System.Runtime.ConstrainedExecution.ReliabilityContractAttribute::.ctor(valuetype System.Runtime.ConstrainedExecution.Consistency, valuetype System.Runtime.ConstrainedExecution.Cer) = (\r\n"
        "\t\t01 00 03 00 00 00 01 00 00 00 00 00\r\n"
        "\t)\r\n"
        "} // end of method Win32Native::LocalAlloc_NoSafeHandle\r\n");
}

TEST(ReflectionDisassemblerTest, DisassembleMethodFullOverrideWithBody)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    std::uint32_t getCount = FindMethodIn(f, arrayType,
        "System.Collections.ICollection.get_Count");
    ASSERT_NE(getCount, 0u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleMethod(f, getCount);
    });
    // The gold block, de-indented: the newslot virtual flag order, the
    // __DynamicallyInvokableAttribute custom attribute, the .override line
    // (the MethodImpl row's declaration), and the three-instruction body
    // through the MethodBodyDisassembler.
    EXPECT_EQ(actual,
        ".method private final hidebysig specialname newslot virtual \r\n"
        "\tinstance int32 System.Collections.ICollection.get_Count () cil managed \r\n"
        "{\r\n"
        "\t.custom instance void __DynamicallyInvokableAttribute::.ctor() = (\r\n"
        "\t\t01 00 00 00\r\n"
        "\t)\r\n"
        "\t.override method instance int32 System.Collections.ICollection::get_Count()\r\n"
        "\t// Method begins at RVA 0x692c\r\n"
        "\t// Header size: 1\r\n"
        "\t// Code size: 7 (0x7)\r\n"
        "\t.maxstack 8\r\n"
        "\r\n"
        "\tIL_0000: ldarg.0\r\n"
        "\tIL_0001: call instance int32 System.Array::get_Length()\r\n"
        "\tIL_0006: ret\r\n"
        "} // end of method Array::System.Collections.ICollection.get_Count\r\n");
}

TEST(ReflectionDisassemblerTest, WriteSecurityDeclarationsRawBlob)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t registryKey = FindTypeDefTokenIn(f, "Microsoft.Win32",
        "RegistryKey");
    std::uint32_t getHandle = FindMethodIn(f, registryKey, "get_Handle");
    ASSERT_NE(getHandle, 0u);
    auto rows = f.GetDeclarativeSecurityAttributes(getHandle);
    ASSERT_EQ(rows.size(), 1u);

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.WriteSecurityDeclarations(f, rows);
    });
    // The gold .permissionset demand line for get_Handle, de-indented: the
    // action spelling, the " = " and the raw 201-byte blob through the
    // WriteBlob 16-per-line geometry (twelve full lines and a nine-byte tail).
    EXPECT_EQ(actual,
        ".permissionset demand = (\r\n"
        "\t2e 01 80 84 53 79 73 74 65 6d 2e 53 65 63 75 72\r\n"
        "\t69 74 79 2e 50 65 72 6d 69 73 73 69 6f 6e 73 2e\r\n"
        "\t53 65 63 75 72 69 74 79 50 65 72 6d 69 73 73 69\r\n"
        "\t6f 6e 41 74 74 72 69 62 75 74 65 2c 20 6d 73 63\r\n"
        "\t6f 72 6c 69 62 2c 20 56 65 72 73 69 6f 6e 3d 34\r\n"
        "\t2e 30 2e 30 2e 30 2c 20 43 75 6c 74 75 72 65 3d\r\n"
        "\t6e 65 75 74 72 61 6c 2c 20 50 75 62 6c 69 63 4b\r\n"
        "\t65 79 54 6f 6b 65 6e 3d 62 37 37 61 35 63 35 36\r\n"
        "\t31 39 33 34 65 30 38 39 40 01 54 55 32 53 79 73\r\n"
        "\t74 65 6d 2e 53 65 63 75 72 69 74 79 2e 50 65 72\r\n"
        "\t6d 69 73 73 69 6f 6e 73 2e 53 65 63 75 72 69 74\r\n"
        "\t79 50 65 72 6d 69 73 73 69 6f 6e 46 6c 61 67 05\r\n"
        "\t46 6c 61 67 73 02 00 00 00\r\n"
        ")\r\n");
}

TEST(ReflectionDisassemblerTest, WriteSecurityDeclarationsEmptyRendersNothing)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    auto rows = f.GetDeclarativeSecurityAttributes(copy);
    ASSERT_TRUE(rows.empty());

    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.WriteSecurityDeclarations(f, rows);
    });
    EXPECT_EQ(actual, "");
}

// ---------------------------------------------------------------------------
// An invariant sweep: every method of a few large mscorlib types renders a
// DisassembleMethodHeader without throwing, and the renders carry the
// ".method " prefix, the "cil managed"/"unmanaged" tail, and a name.
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, DisassembleMethodHeaderInvariantSweep)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t rendered = 0;
    std::uint32_t managed = 0;
    for (const char* typeName : {"String", "Enum", "Array", "GC"}) {
        std::uint32_t type = FindTypeDefTokenIn(f, "System", typeName);
        ASSERT_NE(type, 0u) << typeName;
        for (const auto& m : f.GetMethods(type)) {
            std::ostringstream stream;
            OUT::PlainTextOutput output(stream);
            DA::ReflectionDisassembler rd(output);
            rd.DisassembleMethodHeader(f, m.Token);
            std::string text = stream.str();
            EXPECT_NE(text.find(".method "), std::string::npos) << m.Name;
            // The header ends mid-line after the code-type/managed words
            // (any methodImpl tail flags follow them).
            EXPECT_NE(text.find("managed "), std::string::npos) << m.Name;
            EXPECT_TRUE(text.find("cil ") != std::string::npos
                || text.find("native ") != std::string::npos
                || text.find("runtime ") != std::string::npos
                || text.find("optil ") != std::string::npos)
                << m.Name << ": " << text;
            if (text.find("cil managed ") != std::string::npos)
                ++managed;
            ++rendered;
        }
    }
    EXPECT_GT(rendered, 200u);
    EXPECT_GT(managed, 200u);
}

// ---------------------------------------------------------------------------
// The field member renderer (ReflectionDisassembler.cs lines 1270-1435:
// DisassembleField / DisassembleFieldHeader / DisassembleFieldHeaderInternal
// / GetRVASectionPrefix). Every exact render below is de-indented from the
// ilspycmd 11.0 `-il` gold dump of the same Framework64 mscorlib (the
// ReflectionDisassembler output); the fixture facts (RVA 0x4E8FF8, the
// ClassLayout size 40, the 40 initial-value bytes, the lpwstr blob 0x15) are
// verified independently against the BCL MetadataReader/PEReader (see the
// SRMExtensions FieldValueSizeDecoder tests).
// ---------------------------------------------------------------------------

// A nested TypeDef token by its own name and its declaring type's name (the
// nested rows carry an empty namespace column; the SRMExtensions test's
// FindNestedType shape).
std::uint32_t FindNestedTypeIn(const MD::MetadataFile& f, std::string_view name,
    std::string_view declaringName) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Name != name) continue;
        auto info = f.GetTypeDefNameInfo(t.Token);
        if (!info || info->DeclaringTypeToken == 0) continue;
        auto declaring = f.GetTypeDefNameInfo(info->DeclaringTypeToken);
        if (declaring && declaring->Name == declaringName) return t.Token;
    }
    return 0;
}

TEST(ReflectionDisassemblerTest, DisassembleFieldHeaderShapes)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t opCodeValues = FindTypeDefTokenIn(f, "System.Reflection.Emit", "OpCodeValues");
    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    std::uint32_t pic = FindTypeDefTokenIn(f, "", "<PrivateImplementationDetails>");
    ASSERT_NE(opCodeValues, 0u);
    ASSERT_NE(win32, 0u);
    ASSERT_NE(pic, 0u);
    std::uint32_t information = FindNestedTypeIn(f,
        "CLAIM_SECURITY_ATTRIBUTE_INFORMATION_V1", "Win32Native");
    std::uint32_t fqbnType = FindNestedTypeIn(f,
        "CLAIM_SECURITY_ATTRIBUTE_FQBN_VALUE", "Win32Native");
    ASSERT_NE(information, 0u);
    ASSERT_NE(fqbnType, 0u);
    std::uint32_t literalField = FindFieldIn(f, opCodeValues, "Conv_Ovf_I_Un");
    std::uint32_t nativeIntField = FindFieldIn(f, information, "pAttributeV1");
    std::uint32_t marshalField = FindFieldIn(f, fqbnType, "Name");
    std::uint32_t dataField = FindFieldIn(f, pic,
        "001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB");
    ASSERT_NE(literalField, 0u);
    ASSERT_NE(nativeIntField, 0u);
    ASSERT_NE(marshalField, 0u);
    ASSERT_NE(dataField, 0u);

    // The literal: visibility WriteEnum + the attribute WriteFlags, the
    // valuetype render at Signature syntax, and the Constant-table tail.
    // (Gold line: ".field public static literal valuetype
    // System.Reflection.Emit.OpCodeValues Conv_Ovf_I_Un = int32(138)").
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleFieldHeader(f, literalField);
    }),
        ".field public static literal valuetype System.Reflection.Emit.OpCodeValues"
        " Conv_Ovf_I_Un = int32(138)");

    // The explicit-layout offset prefix "[0] " and the native int render.
    // (Gold line: ".field [0] public native int pAttributeV1".)
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleFieldHeader(f, nativeIntField);
    }),
        ".field [0] public native int pAttributeV1");

    // The marshalling descriptor renders between the flags and the type.
    // (Gold line: ".field public marshal(lpwstr) string Name".)
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleFieldHeader(f, marshalField);
    }),
        ".field public marshal(lpwstr) string Name");

    // The HasFieldRVA " at <prefix>_<rva>" tail -- the .text section's 'I'
    // prefix, the escaped hex-name field, and the nested valuetype render.
    // (Gold line: ".field assembly static initonly valuetype
    // '<PrivateImplementationDetails>'/'__StaticArrayInitTypeSize=40'
    // '001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB' at
    // I_004E8FF8".)
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleFieldHeader(f, dataField);
    }),
        ".field assembly static initonly valuetype"
        " '<PrivateImplementationDetails>'/'__StaticArrayInitTypeSize=40'"
        " '001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB'"
        " at I_004E8FF8");
}

TEST(ReflectionDisassemblerTest, DisassembleFieldHeaderShowsMetadataTokens)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t emptyField = FindFieldIn(f, stringType, "Empty");
    ASSERT_NE(emptyField, 0u);

    // The field header's token comment comes BEFORE the flags with both
    // surrounding spaces on ("/* 0400xxxx */ " -- the spaceBefore=true split
    // from the method header's spaceBefore=false).
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.ShowMetadataTokens(true);
        rd.DisassembleFieldHeader(f, emptyField);
    }),
        ".field /* " + Hex8(emptyField) + " */ public static initonly string Empty");
}

TEST(ReflectionDisassemblerTest, DisassembleFieldLiteralAndDataFieldRenders)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t opCodeValues = FindTypeDefTokenIn(f, "System.Reflection.Emit", "OpCodeValues");
    std::uint32_t pic = FindTypeDefTokenIn(f, "", "<PrivateImplementationDetails>");
    ASSERT_NE(opCodeValues, 0u);
    ASSERT_NE(pic, 0u);
    std::uint32_t literalField = FindFieldIn(f, opCodeValues, "Conv_Ovf_I_Un");
    std::uint32_t dataField = FindFieldIn(f, pic,
        "001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB");
    ASSERT_NE(literalField, 0u);
    ASSERT_NE(dataField, 0u);

    // A field without attributes or data renders the header plus the one
    // terminating line break -- nothing else.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleField(f, literalField);
    }),
        ".field public static literal valuetype System.Reflection.Emit.OpCodeValues"
        " Conv_Ovf_I_Un = int32(138)\r\n");

    // The HasFieldRVA data field: the header, then the ".data cil" block --
    // the .text section kind, the I_<rva> data name, and the 40-byte blob
    // (the gold's bytearray dump: ff*8 0d 00 00 00 04 00 00 00 / ff*12
    // 0f 00 00 00 / ff*4 0c 00 00 00).
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleField(f, dataField);
    }),
        ".field assembly static initonly valuetype"
        " '<PrivateImplementationDetails>'/'__StaticArrayInitTypeSize=40'"
        " '001F1D86E0BD2B1A9BF6D7CD56529284FCDA770A6E6E0EF7CF8B2238118033CB'"
        " at I_004E8FF8\r\n"
        ".data cil I_004E8FF8 = bytearray (\r\n"
        "\tff ff ff ff ff ff ff ff 0d 00 00 00 04 00 00 00\r\n"
        "\tff ff ff ff ff ff ff ff ff ff ff ff 0f 00 00 00\r\n"
        "\tff ff ff ff 0c 00 00 00\r\n"
        ")\r\n");
}

TEST(ReflectionDisassemblerTest, DisassembleFieldAttributeFieldRender)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32, 0u);
    std::uint32_t findData = FindNestedTypeIn(f, "WIN32_FIND_DATA", "Win32Native");
    ASSERT_NE(findData, 0u);
    std::uint32_t fixedBufferField = FindFieldIn(f, findData, "_cFileName");
    ASSERT_NE(fixedBufferField, 0u);

    // The attribute-bearing field: the header, then the .custom lines at the
    // header's own indentation (no braces, no extra indent), the
    // FixedBufferAttribute blob from the gold dump.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleField(f, fixedBufferField);
    }),
        ".field private valuetype Microsoft.Win32.Win32Native/WIN32_FIND_DATA/"
        "'<_cFileName>e__FixedBuffer' _cFileName\r\n"
        + CustomLine(
            "instance void System.Runtime.CompilerServices.FixedBufferAttribute::"
            ".ctor(class System.Type, int32)",
            Bytes({0x01, 0x00, 0x0b, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x2e,
                0x43, 0x68, 0x61, 0x72, 0x04, 0x01,
                0x00, 0x00, 0x00, 0x00})));
}

TEST(ReflectionDisassemblerTest, GetRVASectionPrefixShapes)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    DA::ReflectionDisassembler rd(output);

    // .text (the code section) carries the 'I' prefix.
    EXPECT_EQ(rd.GetRVASectionPrefix(f, 0x4E8FF8), 'I');
    // An RVA in no section reads the default 'D'.
    EXPECT_EQ(rd.GetRVASectionPrefix(f, 0x7FFFFFFF), 'D');
    // .rsrc (a non-.text/.tls section) reads the default 'D' as well.
    EXPECT_EQ(rd.GetRVASectionPrefix(f, 0x4FA000), 'D');
}

TEST(ReflectionDisassemblerTest, DisassembleFieldHeaderMscorlibSweep)
{
    // A bounded no-throw sweep: every field of Microsoft.Win32.Win32Native
    // and ALL its nested types (the marshalling-descriptor shapes), plus
    // String, OpCodeValues, and the <PrivateImplementationDetails> data fields.
    // Every render starts with the ".field " reference and ends mid-line
    // (the header carries no line break of its own).
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32 = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t opCodeValues = FindTypeDefTokenIn(f, "System.Reflection.Emit", "OpCodeValues");
    std::uint32_t pic = FindTypeDefTokenIn(f, "", "<PrivateImplementationDetails>");
    ASSERT_NE(win32, 0u);
    ASSERT_NE(stringType, 0u);
    ASSERT_NE(opCodeValues, 0u);
    ASSERT_NE(pic, 0u);

    std::vector<std::uint32_t> types = {win32, stringType, opCodeValues, pic};
    for (const auto& t : f.TypeDefs()) {
        auto info = f.GetTypeDefNameInfo(t.Token);
        if (info && info->DeclaringTypeToken == win32)
            types.push_back(t.Token);
    }
    int rendered = 0;
    int dataFields = 0;
    for (std::uint32_t typeToken : types) {
        for (const auto& fd : f.GetFields(typeToken)) {
            std::ostringstream stream;
            OUT::PlainTextOutput output(stream);
            DA::ReflectionDisassembler rd(output);
            rd.DisassembleFieldHeader(f, fd.Token);
            std::string text = stream.str();
            EXPECT_NE(text.find(".field "), std::string::npos) << fd.Name;
            EXPECT_NE(text.find(" "), std::string::npos) << fd.Name;
            if (text.find(" at ") != std::string::npos)
                ++dataFields;
            ++rendered;
        }
    }
    EXPECT_GT(rendered, 400u);
    EXPECT_GT(dataFields, 100u);
}

// ---------------------------------------------------------------------------
// The property and event member renderers (ReflectionDisassembler.cs lines
// 1424-1596) and the accessor reads they compose (the MethodSemantics walk).
// The exact renders are de-indented from the gold ilspycmd -il output
// (C:\temp-probe\mscorlib.il / System.dll.il): the property/event headers
// carry NO flag words on every local fixture -- all .NET Framework 4.8 and
// .NET 10 CoreLib Property/Event attribute columns are zero (probed over
// the full tables: mscorlib 5011/33, System.dll 4089/115, CoreLib
// 5581/32, not a single nonzero row), so the specialname/rtspecialname/
// hasdefault WriteFlags arms are unobservable on real assemblies and the
// exact renders pin the zero-flag shapes.
// ---------------------------------------------------------------------------
namespace {

std::uint32_t FindPropertyIn(const MD::MetadataFile& f,
    std::uint32_t typeToken, std::string_view name) {
    for (const auto& p : f.GetProperties(typeToken)) {
        if (p.Name == name) return p.Token;
    }
    return 0;
}

std::uint32_t FindEventIn(const MD::MetadataFile& f,
    std::uint32_t typeToken, std::string_view name) {
    for (const auto& e : f.GetEvents(typeToken)) {
        if (e.Name == name) return e.Token;
    }
    return 0;
}

std::string SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

}  // namespace

TEST(ReflectionDisassemblerTest, GetPropertyAccessorsBasics) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // A getter-only indexer: System.String::Chars.
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t chars = FindPropertyIn(f, stringType, "Chars");
    ASSERT_NE(chars, 0u);
    auto accessors = f.GetPropertyAccessors(chars);
    EXPECT_NE(accessors.GetterToken, 0u);
    EXPECT_EQ(f.GetMethodName(accessors.GetterToken), "get_Chars");
    EXPECT_EQ(accessors.SetterToken, 0u);
    EXPECT_TRUE(accessors.OtherTokens.empty());
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(accessors.GetterToken), stringType);

    // A get+set indexer: System.Array::System.Collections.IList.Item.
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    ASSERT_NE(arrayType, 0u);
    std::uint32_t item = FindPropertyIn(f, arrayType,
        "System.Collections.IList.Item");
    ASSERT_NE(item, 0u);
    accessors = f.GetPropertyAccessors(item);
    EXPECT_NE(accessors.GetterToken, 0u);
    EXPECT_EQ(f.GetMethodName(accessors.GetterToken),
        "System.Collections.IList.get_Item");
    EXPECT_NE(accessors.SetterToken, 0u);
    EXPECT_EQ(f.GetMethodName(accessors.SetterToken),
        "System.Collections.IList.set_Item");
    EXPECT_TRUE(accessors.OtherTokens.empty());
    EXPECT_EQ(f.GetMethodDeclaringTypeToken(accessors.GetterToken), arrayType);

    // A setter-only property (the GetAny()-falls-to-the-setter shape):
    // System.IO.StreamWriter::HaveWrittenPreamble.
    std::uint32_t writerType = FindTypeDefTokenIn(f, "System.IO",
        "StreamWriter");
    ASSERT_NE(writerType, 0u);
    std::uint32_t preamble = FindPropertyIn(f, writerType,
        "HaveWrittenPreamble");
    ASSERT_NE(preamble, 0u);
    accessors = f.GetPropertyAccessors(preamble);
    EXPECT_EQ(accessors.GetterToken, 0u);
    EXPECT_NE(accessors.SetterToken, 0u);
    EXPECT_EQ(f.GetMethodName(accessors.SetterToken),
        "set_HaveWrittenPreamble");
    EXPECT_TRUE(accessors.OtherTokens.empty());

    // An invalid token resolves to the all-nil set (never throws).
    accessors = f.GetPropertyAccessors(0x17FFFFFFu);
    EXPECT_EQ(accessors.GetterToken, 0u);
    EXPECT_EQ(accessors.SetterToken, 0u);
    EXPECT_TRUE(accessors.OtherTokens.empty());
}

TEST(ReflectionDisassemblerTest, GetEventAccessorsAndEventTypeBasics) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // An event with a TypeSpec delegate type (a closed generic
    // instantiation): System.Exception::SerializeObjectState.
    std::uint32_t exceptionType = FindTypeDefTokenIn(f, "System", "Exception");
    ASSERT_NE(exceptionType, 0u);
    std::uint32_t sos = FindEventIn(f, exceptionType, "SerializeObjectState");
    ASSERT_NE(sos, 0u);
    auto accessors = f.GetEventAccessors(sos);
    EXPECT_NE(accessors.AdderToken, 0u);
    EXPECT_EQ(f.GetMethodName(accessors.AdderToken),
        "add_SerializeObjectState");
    EXPECT_NE(accessors.RemoverToken, 0u);
    EXPECT_EQ(f.GetMethodName(accessors.RemoverToken),
        "remove_SerializeObjectState");
    EXPECT_EQ(accessors.RaiserToken, 0u);
    EXPECT_TRUE(accessors.OtherTokens.empty());
    std::uint32_t eventType = f.GetEventTypeToken(sos);
    EXPECT_EQ(eventType >> 24, 0x1Bu);  // TypeSpec
    auto blob = f.GetTypeSpecSignatureBlob(eventType);
    ASSERT_TRUE(blob.has_value());
    ASSERT_FALSE(blob->empty());
    EXPECT_EQ((*blob)[0], 0x15u);  // GENERICINST | CLASS

    // An event with a same-assembly TypeDef delegate type:
    // System.AppDomain::AssemblyLoad.
    std::uint32_t appDomain = FindTypeDefTokenIn(f, "System", "AppDomain");
    ASSERT_NE(appDomain, 0u);
    std::uint32_t assemblyLoad = FindEventIn(f, appDomain, "AssemblyLoad");
    ASSERT_NE(assemblyLoad, 0u);
    accessors = f.GetEventAccessors(assemblyLoad);
    EXPECT_EQ(f.GetMethodName(accessors.AdderToken), "add_AssemblyLoad");
    EXPECT_EQ(f.GetMethodName(accessors.RemoverToken), "remove_AssemblyLoad");
    EXPECT_EQ(accessors.RaiserToken, 0u);
    EXPECT_EQ(f.GetEventTypeToken(assemblyLoad) >> 24, 0x02u);  // TypeDef

    // An invalid token: all-nil accessors, nil type (never throws).
    accessors = f.GetEventAccessors(0x14FFFFFFu);
    EXPECT_EQ(accessors.AdderToken, 0u);
    EXPECT_EQ(f.GetEventTypeToken(0x14FFFFFFu), 0u);
}

TEST(ReflectionDisassemblerTest, DisassemblePropertyHeaderShapes) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    std::uint32_t segmentType = FindTypeDefTokenIn(f, "System",
        "ArraySegment`1");
    std::uint32_t writerType = FindTypeDefTokenIn(f, "System.IO",
        "StreamWriter");
    ASSERT_NE(stringType, 0u);
    ASSERT_NE(arrayType, 0u);
    ASSERT_NE(segmentType, 0u);
    ASSERT_NE(writerType, 0u);

    // A no-parameter property: ".property instance int32 Length()".
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassemblePropertyHeader(f,
            FindPropertyIn(f, arrayType, "Length"));
    }),
        ".property instance int32 Length()");

    // An indexer with a named parameter: the param block is Indent-wrapped
    // WriteParameters lines between the '(' and the ')'.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassemblePropertyHeader(f, FindPropertyIn(f, stringType,
            "Chars"));
    }),
        ".property instance char Chars(\r\n"
        "\tint32 index\r\n"
        ")");

    // A generic indexer: the !T context scopes to the declaring
    // ArraySegment`1, and the dotted name keeps the quoted escape.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassemblePropertyHeader(f, FindPropertyIn(f, segmentType,
            "System.Collections.Generic.IList<T>.Item"));
    }),
        ".property instance !T 'System.Collections.Generic.IList<T>.Item'(\r\n"
        "\tint32 index\r\n"
        ")");

    // A get+set indexer (System.Array::IList.Item): with a getter the C#
    // takes the full Param row list.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassemblePropertyHeader(f, FindPropertyIn(f, arrayType,
            "System.Collections.IList.Item"));
    }),
        ".property instance object System.Collections.IList.Item(\r\n"
        "\tint32 index\r\n"
        ")");

    // A setter-only property: no "instance" (static accessors) and the
    // set_HaveWrittenPreamble shape.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassemblePropertyHeader(f, FindPropertyIn(f, writerType,
            "HaveWrittenPreamble"));
    }),
        ".property instance bool HaveWrittenPreamble()");

    // The ShowMetadataTokens comment between the ".property" and the flags.
    std::uint32_t chars = FindPropertyIn(f, stringType, "Chars");
    ASSERT_NE(chars, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.ShowMetadataTokens(true);
        rd.DisassemblePropertyHeader(f, chars);
    }),
        ".property /* " + Hex8(chars) + " */ instance char Chars(\r\n"
        "\tint32 index\r\n"
        ")");
}

TEST(ReflectionDisassemblerTest, DisassemblePropertyFullRender) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);

    // The gold -il render of System.String::Chars, de-indented: the header,
    // the uncollapsed attribute block with the .custom line, and the .get
    // accessor line through EntityHandle.WriteTo.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleProperty(f, FindPropertyIn(f, stringType, "Chars"));
    }),
        ".property instance char Chars(\r\n"
        "\tint32 index\r\n"
        ")\r\n"
        "{\r\n"
        "\t.custom instance void __DynamicallyInvokableAttribute::.ctor() = (\r\n"
        "\t\t01 00 00 00\r\n"
        "\t)\r\n"
        "\t.get instance char System.String::get_Chars(int32)\r\n"
        "}\r\n");

    // The setter-only shape: no attribute lines, the .set line only.
    std::uint32_t writerType = FindTypeDefTokenIn(f, "System.IO",
        "StreamWriter");
    ASSERT_NE(writerType, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleProperty(f, FindPropertyIn(f, writerType,
            "HaveWrittenPreamble"));
    }),
        ".property instance bool HaveWrittenPreamble()\r\n"
        "{\r\n"
        "\t.set instance void System.IO.StreamWriter::set_HaveWrittenPreamble(bool)\r\n"
        "}\r\n");

    // A get+set indexer: the .get and .set lines in that order.
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    ASSERT_NE(arrayType, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleProperty(f, FindPropertyIn(f, arrayType,
            "System.Collections.IList.Item"));
    }),
        ".property instance object System.Collections.IList.Item(\r\n"
        "\tint32 index\r\n"
        ")\r\n"
        "{\r\n"
        "\t.custom instance void __DynamicallyInvokableAttribute::.ctor() = (\r\n"
        "\t\t01 00 00 00\r\n"
        "\t)\r\n"
        "\t.get instance object System.Array::System.Collections.IList.get_Item(int32)\r\n"
        "\t.set instance void System.Array::System.Collections.IList.set_Item(int32, object)\r\n"
        "}\r\n");
}

TEST(ReflectionDisassemblerTest, DisassembleEventShapes) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // The TypeSpec arm: the delegate type decoded from the signature blob
    // (the closed generic instantiation) at the declaring type's context.
    std::uint32_t exceptionType = FindTypeDefTokenIn(f, "System", "Exception");
    ASSERT_NE(exceptionType, 0u);
    std::uint32_t sos = FindEventIn(f, exceptionType, "SerializeObjectState");
    ASSERT_NE(sos, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleEventHeader(f, sos);
    }),
        ".event class System.EventHandler`1<class System.Runtime.Serialization."
        "SafeSerializationEventArgs> SerializeObjectState");

    // The TypeDef arm (the same-assembly delegate type renders unprefixed)
    // and the full block with the .addon/.removeon lines.
    std::uint32_t appDomain = FindTypeDefTokenIn(f, "System", "AppDomain");
    ASSERT_NE(appDomain, 0u);
    std::uint32_t assemblyLoad = FindEventIn(f, appDomain, "AssemblyLoad");
    ASSERT_NE(assemblyLoad, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleEventHeader(f, assemblyLoad);
    }),
        ".event System.AssemblyLoadEventHandler AssemblyLoad");
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleEvent(f, assemblyLoad);
    }),
        ".event System.AssemblyLoadEventHandler AssemblyLoad\r\n"
        "{\r\n"
        "\t.addon instance void System.AppDomain::add_AssemblyLoad(class System.AssemblyLoadEventHandler)\r\n"
        "\t.removeon instance void System.AppDomain::remove_AssemblyLoad(class System.AssemblyLoadEventHandler)\r\n"
        "}\r\n");

    // The ShowMetadataTokens comment between the ".event" and the type.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.ShowMetadataTokens(true);
        rd.DisassembleEventHeader(f, assemblyLoad);
    }),
        ".event /* " + Hex8(assemblyLoad) + " */ System.AssemblyLoadEventHandler AssemblyLoad");

    // The TypeRef arm (System.dll): the delegate type from another assembly
    // keeps the "[mscorlib]" scope prefix; PowerModeChanged is the
    // same-assembly TypeDef shape with static accessors.
    MD::MetadataFile sys(SystemDllPath());
    ASSERT_TRUE(sys.IsValid());
    std::uint32_t systemEvents = FindTypeDefTokenIn(sys, "Microsoft.Win32",
        "SystemEvents");
    ASSERT_NE(systemEvents, 0u);
    std::uint32_t palette = FindEventIn(sys, systemEvents, "PaletteChanged");
    ASSERT_NE(palette, 0u);
    std::uint32_t powerMode = FindEventIn(sys, systemEvents,
        "PowerModeChanged");
    ASSERT_NE(powerMode, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleEventHeader(sys, palette);
    }),
        ".event [mscorlib]System.EventHandler PaletteChanged");
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleEvent(sys, powerMode);
    }),
        ".event Microsoft.Win32.PowerModeChangedEventHandler PowerModeChanged\r\n"
        "{\r\n"
        "\t.addon void Microsoft.Win32.SystemEvents::add_PowerModeChanged(class Microsoft.Win32.PowerModeChangedEventHandler)\r\n"
        "\t.removeon void Microsoft.Win32.SystemEvents::remove_PowerModeChanged(class Microsoft.Win32.PowerModeChangedEventHandler)\r\n"
        "}\r\n");
}

TEST(ReflectionDisassemblerTest, PropertyEventRenderSweep) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // Every property and event of a spread of mscorlib types renders
    // without throwing and starts with the ".property"/".event" prefix;
    // the accessor lines cover the shapes the gold -il output carries.
    std::vector<std::uint32_t> types;
    for (const auto& t : f.TypeDefs()) {
        if (t.Name == "String" || t.Name == "Array" || t.Name == "AppDomain"
            || t.Name == "Exception" || t.Name == "ArraySegment`1"
            || t.Name == "StreamWriter" || t.Name == "Timer")
            types.push_back(t.Token);
    }
    EXPECT_GT(types.size(), 5u);

    int properties = 0;
    int indexerProperties = 0;
    int events = 0;
    for (std::uint32_t typeToken : types) {
        for (const auto& p : f.GetProperties(typeToken)) {
            std::ostringstream stream;
            OUT::PlainTextOutput output(stream);
            DA::ReflectionDisassembler rd(output);
            rd.DisassembleProperty(f, p.Token);
            std::string text = stream.str();
            EXPECT_NE(text.find(".property "), std::string::npos) << p.Name;
            EXPECT_NE(text.find("\r\n{\r\n"), std::string::npos) << p.Name;
            // An indexer: the header's '(' opens a line-broken parameter
            // block (the \r\n\t param line), unlike the empty "()".
            if (text.find("(\r\n\t") != std::string::npos)
                ++indexerProperties;
            ++properties;
        }
        for (const auto& e : f.GetEvents(typeToken)) {
            std::ostringstream stream;
            OUT::PlainTextOutput output(stream);
            DA::ReflectionDisassembler rd(output);
            rd.DisassembleEvent(f, e.Token);
            std::string text = stream.str();
            EXPECT_NE(text.find(".event "), std::string::npos) << e.Name;
            ++events;
        }
    }
    EXPECT_GT(properties, 40u);
    EXPECT_GT(indexerProperties, 3u);
    EXPECT_GT(events, 8u);
}

// ---------------------------------------------------------------------------
// The type member renderer (ReflectionDisassembler.cs lines 1598-1753):
// DisassembleTypeHeader/Internal's flag splits and name forms, and the full
// DisassembleType render -- the implements list, the layout lines, the
// .interfaceimpl blocks, the member sections, and the end-of-class comment.
// The exact renders are the gold ilspycmd 11.0 -il output (mscorlib 4.8 and
// the .NET 10 CoreLib) de-indented one level.
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, GetTypeLayoutPackingAndSize)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32Native = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32Native, 0u);
    std::uint32_t findData = FindNestedTypeIn(f, win32Native, "WIN32_FIND_DATA");
    ASSERT_NE(findData, 0u);
    std::uint32_t fixedBuffer = FindNestedTypeIn(f, findData, "<_cFileName>e__FixedBuffer");
    ASSERT_NE(fixedBuffer, 0u);
    std::uint32_t alternateBuffer = FindNestedTypeIn(f, findData, "<_cAlternateFileName>e__FixedBuffer");
    ASSERT_NE(alternateBuffer, 0u);

    // The fixed buffers' ClassLayout rows: .pack 0 / .size 520 and 28.
    auto big = f.GetTypeLayout(fixedBuffer);
    EXPECT_EQ(big.PackingSize, 0);
    EXPECT_EQ(big.ClassSize, 520);
    EXPECT_FALSE(big.IsDefault());
    auto small = f.GetTypeLayout(alternateBuffer);
    EXPECT_EQ(small.PackingSize, 0);
    EXPECT_EQ(small.ClassSize, 28);

    // A type without a ClassLayout row reads the default (the sequential
    // MEMORY_BASIC_INFORMATION flag carries no row -- the runtime packs it).
    std::uint32_t memoryBasic = FindNestedTypeIn(f, win32Native, "MEMORY_BASIC_INFORMATION");
    ASSERT_NE(memoryBasic, 0u);
    auto none = f.GetTypeLayout(memoryBasic);
    EXPECT_EQ(none.PackingSize, 0);
    EXPECT_EQ(none.ClassSize, 0);
    EXPECT_TRUE(none.IsDefault());

    // A non-TypeDef / out-of-range token reads the default; never throws.
    auto invalid = f.GetTypeLayout(0x06000001);
    EXPECT_TRUE(invalid.IsDefault());
}

TEST(ReflectionDisassemblerTest, GetNestedTypesEnclosingWalk)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // Microsoft.Win32.Win32Native: 53 nested types in table order, the
    // WIN32_FIND_DATA row among them, and that row's own two fixed buffers.
    std::uint32_t win32Native = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32Native, 0u);
    auto nested = f.GetNestedTypes(win32Native);
    EXPECT_EQ(nested.size(), 53u);
    EXPECT_NE(FindNestedTypeIn(f, win32Native, "WIN32_FIND_DATA"), 0u);
    std::uint32_t findData = FindNestedTypeIn(f, win32Native, "WIN32_FIND_DATA");
    auto fixedBuffers = f.GetNestedTypes(findData);
    ASSERT_EQ(fixedBuffers.size(), 2u);
    auto firstName = f.GetTypeDefNameInfo(fixedBuffers[0]);
    ASSERT_TRUE(firstName.has_value());
    EXPECT_EQ(firstName->Name, "<_cFileName>e__FixedBuffer");

    // A top-level type with no nested rows reads empty; a non-TypeDef
    // token reads empty too.
    std::uint32_t resolveArgs = FindTypeDefTokenIn(f, "System", "ResolveEventArgs");
    ASSERT_NE(resolveArgs, 0u);
    EXPECT_TRUE(f.GetNestedTypes(resolveArgs).empty());
    EXPECT_TRUE(f.GetNestedTypes(0x06000001).empty());
}

TEST(ReflectionDisassemblerTest, GetBaseTypeTokenExtendsColumn)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // The same-module TypeDef bases mscorlib 4.8 carries (ResolveEventArgs
    // extends the System.EventArgs TypeDef, String the System.Object one).
    std::uint32_t resolveArgs = FindTypeDefTokenIn(f, "System", "ResolveEventArgs");
    ASSERT_NE(resolveArgs, 0u);
    std::uint32_t eventArgsBase = f.GetBaseTypeToken(resolveArgs);
    ASSERT_NE(eventArgsBase, 0u);
    EXPECT_EQ(eventArgsBase >> 24, 0x02u);
    auto baseName = f.GetTypeDefNameInfo(eventArgsBase);
    ASSERT_TRUE(baseName.has_value());
    EXPECT_EQ(baseName->Namespace, "System");
    EXPECT_EQ(baseName->Name, "EventArgs");

    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t objectBase = f.GetBaseTypeToken(stringType);
    ASSERT_NE(objectBase, 0u);
    EXPECT_EQ(objectBase >> 24, 0x02u);
    auto objectName = f.GetTypeDefNameInfo(objectBase);
    ASSERT_TRUE(objectName.has_value());
    EXPECT_EQ(objectName->Name, "Object");

    // An interface with no base reads the nil token.
    std::uint32_t iasm = FindTypeDefTokenIn(f, "Microsoft.Win32", "IAssemblyEnum");
    ASSERT_NE(iasm, 0u);
    EXPECT_EQ(f.GetBaseTypeToken(iasm), 0u);
    // A non-TypeDef token reads nil; never throws.
    EXPECT_EQ(f.GetBaseTypeToken(0x06000001), 0u);
}

TEST(ReflectionDisassemblerTest, DisassembleTypeHeaderShapes)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // A plain public class: the visibility/layout/string-format enums, the
    // beforefieldinit flag, and the indented extends line.
    std::uint32_t resolveArgs = FindTypeDefTokenIn(f, "System", "ResolveEventArgs");
    ASSERT_NE(resolveArgs, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleTypeHeader(f, resolveArgs);
    }),
        ".class public auto ansi beforefieldinit System.ResolveEventArgs\r\n"
        "\textends System.EventArgs\r\n");

    // An interface: the "interface " prefix replaces the class default, the
    // abstract + import flags render, and the nil base writes no extends.
    std::uint32_t iasm = FindTypeDefTokenIn(f, "Microsoft.Win32", "IAssemblyEnum");
    ASSERT_NE(iasm, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleTypeHeader(f, iasm);
    }),
        ".class interface private auto ansi abstract import Microsoft.Win32.IAssemblyEnum\r\n");

    // A generic type: the arity in the name and the WriteTypeParameters
    // variance list, plus a delegate base.
    std::uint32_t predicate = FindTypeDefTokenIn(f, "System", "Predicate`1");
    ASSERT_NE(predicate, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleTypeHeader(f, predicate);
    }),
        ".class public auto ansi sealed System.Predicate`1<-T>\r\n"
        "\textends System.MulticastDelegate\r\n");

    // A nested type: the nested-visibility enum, the sequential/unicode
    // masks, and the escaped short name.
    std::uint32_t win32Native = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32Native, 0u);
    std::uint32_t findData = FindNestedTypeIn(f, win32Native, "WIN32_FIND_DATA");
    ASSERT_NE(findData, 0u);
    std::uint32_t fixedBuffer = FindNestedTypeIn(f, findData, "<_cFileName>e__FixedBuffer");
    ASSERT_NE(fixedBuffer, 0u);
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleTypeHeader(f, fixedBuffer);
    }),
        ".class nested public sequential unicode sealed beforefieldinit '<_cFileName>e__FixedBuffer'\r\n"
        "\textends System.ValueType\r\n");

    // The ShowMetadataTokens token comment after the ".class" reference.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.ShowMetadataTokens(true);
        rd.DisassembleTypeHeader(f, resolveArgs);
    }),
        ".class /* 02000091 */ public auto ansi beforefieldinit System.ResolveEventArgs\r\n"
        "\textends System.EventArgs\r\n");
}

TEST(ReflectionDisassemblerTest, DisassembleTypeFullRenderFixedBuffer)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t win32Native = FindTypeDefTokenIn(f, "Microsoft.Win32", "Win32Native");
    ASSERT_NE(win32Native, 0u);
    std::uint32_t findData = FindNestedTypeIn(f, win32Native, "WIN32_FIND_DATA");
    ASSERT_NE(findData, 0u);
    std::uint32_t fixedBuffer = FindNestedTypeIn(f, findData, "<_cFileName>e__FixedBuffer");
    ASSERT_NE(fixedBuffer, 0u);

    // The gold -il render de-indented one level: the two .custom lines,
    // the .pack/.size pair with its blank, the // Fields section, and the
    // short-name end-of-class comment.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleType(f, fixedBuffer);
    }),
        ".class nested public sequential unicode sealed beforefieldinit '<_cFileName>e__FixedBuffer'\r\n"
        "\textends System.ValueType\r\n"
        "{\r\n"
        "\t.custom instance void System.Runtime.CompilerServices.CompilerGeneratedAttribute::.ctor() = (\r\n"
        "\t\t01 00 00 00\r\n"
        "\t)\r\n"
        "\t.custom instance void System.Runtime.CompilerServices.UnsafeValueTypeAttribute::.ctor() = (\r\n"
        "\t\t01 00 00 00\r\n"
        "\t)\r\n"
        "\t.pack 0\r\n"
        "\t.size 520\r\n"
        "\r\n"
        "\t// Fields\r\n"
        "\t.field public char FixedElementField\r\n"
        "\r\n"
        "} // end of class <_cFileName>e__FixedBuffer\r\n");
}

TEST(ReflectionDisassemblerTest, DisassembleTypeFullRenderCoreLibInterface)
{
    std::string path = CoreLibPath();
    if (path.empty()) {
        GTEST_SKIP() << "no .NET shared runtime System.Private.CoreLib.dll";
    }
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    std::uint32_t readOnlyCollection =
        FindTypeDefTokenIn(f, "System.Collections.Generic", "IReadOnlyCollection`1");
    ASSERT_NE(readOnlyCollection, 0u);

    // The gold -il render: the generic interface header with the two-line
    // implements list (the comma ends the first line, eleven spaces start
    // the second), the .param type block, the .interfaceimpl block with the
    // row's own attributes, the method/property sections, and the arity in
    // the end-of-class reflection name.
    EXPECT_EQ(RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleType(f, readOnlyCollection);
    }),
        ".class interface public auto ansi abstract beforefieldinit System.Collections.Generic.IReadOnlyCollection`1<+T>\r\n"
        "\timplements class System.Collections.Generic.IEnumerable`1<!T>,\r\n"
        "\t           System.Collections.IEnumerable\r\n"
        "{\r\n"
        "\t.param type T\r\n"
        "\t\t.custom instance void System.Runtime.CompilerServices.NullableAttribute::.ctor(uint8) = (\r\n"
        "\t\t\t01 00 02 00 00\r\n"
        "\t\t)\r\n"
        "\t.interfaceimpl type class System.Collections.Generic.IEnumerable`1<!T>\r\n"
        "\t\t.custom instance void System.Runtime.CompilerServices.NullableAttribute::.ctor(uint8[]) = (\r\n"
        "\t\t\t01 00 02 00 00 00 00 01 00 00\r\n"
        "\t\t)\r\n"
        "\r\n"
        "\t// Methods\r\n"
        "\t.method public hidebysig specialname newslot abstract virtual \r\n"
        "\t\tinstance int32 get_Count () cil managed \r\n"
        "\t{\r\n"
        "\t} // end of method IReadOnlyCollection`1::get_Count\r\n"
        "\r\n"
        "\t// Properties\r\n"
        "\t.property instance int32 Count()\r\n"
        "\t{\r\n"
        "\t\t.get instance int32 System.Collections.Generic.IReadOnlyCollection`1::get_Count()\r\n"
        "\t}\r\n"
        "\r\n"
        "} // end of class System.Collections.Generic.IReadOnlyCollection`1\r\n");
}

TEST(ReflectionDisassemblerTest, DisassembleTypeEntityProcessorRoutesMembers)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t resolveArgs = FindTypeDefTokenIn(f, "System", "ResolveEventArgs");
    ASSERT_NE(resolveArgs, 0u);

    // The reversing processor flips every member section: the fields render
    // _RequestingAssembly first (row order is _Name, _RequestingAssembly)
    // and the .custom-less property order flips too.
    ReversingEntityProcessor processor;
    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.EntityProcessor(&processor);
        rd.DisassembleType(f, resolveArgs);
    });
    EXPECT_NE(actual.find(
                  "\t// Fields\r\n"
                  "\t.field private class System.Reflection.Assembly _RequestingAssembly\r\n"
                  "\t.field private string _Name\r\n"),
        std::string::npos);
    // The properties flip (Name renders after RequestingAssembly), the
    // method order reverses, and the close comment is unchanged.
    EXPECT_LT(actual.find("RequestingAssembly()"), actual.find("Name()\r"));
    EXPECT_NE(actual.rfind("} // end of class System.ResolveEventArgs\r\n"),
        std::string::npos);
}

TEST(ReflectionDisassemblerTest, DisassembleTypeStructuralSweep)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // Microsoft.Win32.RegistryKey: the implements line, the // Nested Types
    // section with the nested enum's full render, and the section order.
    std::uint32_t registryKey = FindTypeDefTokenIn(f, "Microsoft.Win32", "RegistryKey");
    ASSERT_NE(registryKey, 0u);
    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleType(f, registryKey);
    });
    EXPECT_EQ(actual.rfind(
        ".class public auto ansi sealed beforefieldinit Microsoft.Win32.RegistryKey\r\n"
        "\textends System.MarshalByRefObject\r\n"
        "\timplements System.IDisposable\r\n"
        "{\r\n", 0),
        0u);
    EXPECT_NE(actual.find("\t// Nested Types\r\n"), std::string::npos);
    EXPECT_NE(actual.find(
                  ".class nested private auto ansi sealed RegistryInternalCheck\r\n"
                  "\t\textends System.Enum\r\n"),
        std::string::npos);
    EXPECT_NE(actual.find("} // end of class RegistryInternalCheck\r\n"), std::string::npos);
    EXPECT_NE(actual.find("\t// Fields\r\n"), std::string::npos);
    EXPECT_NE(actual.find("\t// Methods\r\n"), std::string::npos);
    std::string registryKeyTail = "} // end of class Microsoft.Win32.RegistryKey\r\n";
    EXPECT_EQ(actual.substr(actual.size() - registryKeyTail.size()),
        registryKeyTail);

    // The sections render in the C# order: nested, fields, methods,
    // events, properties.
    auto nestedPos = actual.find("\t// Nested Types\r\n");
    auto fieldsPos = actual.find("\t// Fields\r\n");
    auto methodsPos = actual.find("\t// Methods\r\n");
    auto propsPos = actual.find("\t// Properties\r\n");
    EXPECT_LT(nestedPos, fieldsPos);
    EXPECT_LT(fieldsPos, methodsPos);
    EXPECT_LT(methodsPos, propsPos);

    // System.Array: the multi-line implements list with the 11-space
    // continuation lines, six interfaces.
    std::uint32_t arrayType = FindTypeDefTokenIn(f, "System", "Array");
    ASSERT_NE(arrayType, 0u);
    std::string arrayText = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.DisassembleType(f, arrayType);
    });
    EXPECT_EQ(arrayText.rfind(
        ".class public auto ansi abstract serializable beforefieldinit System.Array\r\n"
        "\textends System.Object\r\n"
        "\timplements System.ICloneable,\r\n"
        "\t           System.Collections.IList,\r\n"
        "\t           System.Collections.ICollection,\r\n"
        "\t           System.Collections.IEnumerable,\r\n"
        "\t           System.Collections.IStructuralComparable,\r\n"
        "\t           System.Collections.IStructuralEquatable\r\n"
        "{\r\n", 0),
        0u);

    // A no-throw render sweep over a spread of mscorlib types: every render
    // opens with the ".class" reference and closes with the end-of-class
    // comment line.
    int rendered = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Name != "String" && t.Name != "Enum" && t.Name != "ArraySegment`1"
            && t.Name != "Type" && t.Name != "BitConverter")
            continue;
        std::ostringstream stream;
        OUT::PlainTextOutput output(stream);
        DA::ReflectionDisassembler rd(output);
        rd.DisassembleType(f, t.Token);
        std::string text = stream.str();
        EXPECT_EQ(text.rfind(".class ", 0), 0u) << t.Name;
        EXPECT_NE(text.rfind("\r\n} // end of class "), std::string::npos) << t.Name;
        ++rendered;
    }
    EXPECT_EQ(rendered, 5);
}

// ---------------------------------------------------------------------------
// DisassembleNamespace / WriteAssemblyHeader / WriteAssemblyReferences (the
// ReflectionDisassembler.cs lines 2034-2120 port) with the underlying
// MetadataFile reads (GetAssemblyDefinition, GetAssemblyReferences,
// GetModuleReferences). The exact renders are pinned against the real C#
// output: the installed ilspycmd 11.0 tool ships ICSharpCode.Decompiler.dll,
// which a standalone probe project calls directly (the same
// ReflectionDisassembler/PlainTextOutput pair) to dump the gold over the
// same local fixtures.
// ---------------------------------------------------------------------------

TEST(ReflectionDisassemblerTest, AssemblyAndReferenceMetadataReads)
{
    MD::MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());

    // The mscorlib manifest row: name, the PublicKey flag (0x1) without
    // WindowsRuntime, SHA1, 4:0:0:0, and the 16-byte public key blob.
    auto def = mscorlib.GetAssemblyDefinition();
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->Token, 0x20000001u);
    EXPECT_EQ(def->Name, "mscorlib");
    EXPECT_EQ(def->Flags, 0x00000001u);
    EXPECT_EQ(def->HashAlgorithm, 0x00008004u);
    EXPECT_EQ(def->MajorVersion, 4);
    EXPECT_EQ(def->MinorVersion, 0);
    EXPECT_EQ(def->BuildNumber, 0);
    EXPECT_EQ(def->RevisionNumber, 0);
    const std::uint8_t expectedKey[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_EQ(def->PublicKey,
        std::vector<std::uint8_t>(std::begin(expectedKey), std::end(expectedKey)));

    // mscorlib references nothing (self-contained) but declares 17 module
    // refs; the two api-ms-* names are not valid ILAsm identifiers and
    // render quoted (the Escape contract).
    EXPECT_TRUE(mscorlib.GetAssemblyReferences().empty());
    auto moduleRefs = mscorlib.GetModuleReferences();
    ASSERT_EQ(moduleRefs.size(), 17u);
    EXPECT_EQ(moduleRefs.front().Token, 0x1A000001u);
    EXPECT_EQ(moduleRefs.front().Name, "kernel32.dll");
    EXPECT_EQ(moduleRefs[10].Name, "combase.dll");
    EXPECT_EQ(moduleRefs[11].Name, "QCall");
    EXPECT_EQ(moduleRefs[15].Name, "api-ms-win-core-winrt-error-l1-1-1.dll");
    EXPECT_EQ(moduleRefs[16].Name, "api-ms-win-core-winrt-string-l1-1-0.dll");

    MD::MetadataFile systemDll(SystemDllPath());
    ASSERT_TRUE(systemDll.IsValid());
    auto systemDef = systemDll.GetAssemblyDefinition();
    ASSERT_TRUE(systemDef.has_value());
    EXPECT_EQ(systemDef->Name, "System");
    EXPECT_EQ(systemDef->MajorVersion, 4);
    EXPECT_EQ(systemDef->HashAlgorithm, 0x00008004u);

    // System.dll's three extern refs with their public key tokens and
    // versions (row order).
    auto refs = systemDll.GetAssemblyReferences();
    ASSERT_EQ(refs.size(), 3u);
    EXPECT_EQ(refs[0].Token, 0x23000001u);
    EXPECT_EQ(refs[0].Name, "mscorlib");
    EXPECT_EQ(refs[0].Flags, 0u);
    const std::uint8_t mscorlibToken[] = {0xb7, 0x7a, 0x5c, 0x56, 0x19, 0x34,
        0xe0, 0x89};
    EXPECT_EQ(refs[0].PublicKeyOrToken,
        std::vector<std::uint8_t>(std::begin(mscorlibToken),
            std::end(mscorlibToken)));
    EXPECT_EQ(refs[1].Name, "System.Configuration");
    const std::uint8_t configToken[] = {0xb0, 0x3f, 0x5f, 0x7f, 0x11, 0xd5,
        0x0a, 0x3a};
    EXPECT_EQ(refs[1].PublicKeyOrToken,
        std::vector<std::uint8_t>(std::begin(configToken),
            std::end(configToken)));
    EXPECT_EQ(refs[2].Name, "System.Xml");
    for (const auto& r : refs) {
        EXPECT_EQ(r.MajorVersion, 4);
        EXPECT_EQ(r.MinorVersion, 0);
        EXPECT_EQ(r.BuildNumber, 0);
        EXPECT_EQ(r.RevisionNumber, 0);
    }
}

TEST(ReflectionDisassemblerTest, WriteAssemblyReferencesSystemDllExact)
{
    MD::MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());

    // The exact gold: 25 ".module extern" lines, then the three
    // ".assembly extern" blocks with publickeytoken and .ver lines.
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteAssemblyReferences(f); });
    std::string expected =
        ".module extern kernel32.dll\r\n"
        ".module extern advapi32.dll\r\n"
        ".module extern user32.dll\r\n"
        ".module extern psapi.dll\r\n"
        ".module extern shell32.dll\r\n"
        ".module extern ntdll.dll\r\n"
        ".module extern gdi32.dll\r\n"
        ".module extern perfcounter.dll\r\n"
        ".module extern wldp.dll\r\n"
        ".module extern wtsapi32.dll\r\n"
        ".module extern version.dll\r\n"
        ".module extern ole32.dll\r\n"
        ".module extern crypt32.dll\r\n"
        ".module extern rasapi32.dll\r\n"
        ".module extern secur32.dll\r\n"
        ".module extern ws2_32.dll\r\n"
        ".module extern httpapi.dll\r\n"
        ".module extern wininet.dll\r\n"
        ".module extern mswsock.dll\r\n"
        ".module extern winhttp.dll\r\n"
        ".module extern tokenbinding.dll\r\n"
        ".module extern websocket.dll\r\n"
        ".module extern iphlpapi.dll\r\n"
        ".module extern winmm.dll\r\n"
        ".assembly extern mscorlib\r\n"
        "{\r\n"
        "\t.publickeytoken = (\r\n"
        "\t\tb7 7a 5c 56 19 34 e0 89\r\n"
        "\t)\r\n"
        "\t.ver 4:0:0:0\r\n"
        "}\r\n"
        ".assembly extern System.Configuration\r\n"
        "{\r\n"
        "\t.publickeytoken = (\r\n"
        "\t\tb0 3f 5f 7f 11 d5 0a 3a\r\n"
        "\t)\r\n"
        "\t.ver 4:0:0:0\r\n"
        "}\r\n"
        ".assembly extern System.Xml\r\n"
        "{\r\n"
        "\t.publickeytoken = (\r\n"
        "\t\tb7 7a 5c 56 19 34 e0 89\r\n"
        "\t)\r\n"
        "\t.ver 4:0:0:0\r\n"
        "}\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteAssemblyReferencesMscorlibModuleRefsOnly)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // mscorlib declares no assembly refs: the 17 module extern lines only,
    // with the two api-ms-* names escaped in single quotes.
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteAssemblyReferences(f); });
    std::string expected =
        ".module extern kernel32.dll\r\n"
        ".module extern oleaut32.dll\r\n"
        ".module extern advapi32.dll\r\n"
        ".module extern ole32.dll\r\n"
        ".module extern user32.dll\r\n"
        ".module extern shell32.dll\r\n"
        ".module extern secur32.dll\r\n"
        ".module extern bcrypt.dll\r\n"
        ".module extern clr.dll\r\n"
        ".module extern ntdll.dll\r\n"
        ".module extern combase.dll\r\n"
        ".module extern QCall\r\n"
        ".module extern advapi32\r\n"
        ".module extern crypt32\r\n"
        ".module extern mscoree.dll\r\n"
        ".module extern 'api-ms-win-core-winrt-error-l1-1-1.dll'\r\n"
        ".module extern 'api-ms-win-core-winrt-string-l1-1-0.dll'\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteAssemblyHeaderMscorlibExactShape)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteAssemblyHeader(f); });

    // The block opens with the manifest name (no blank line -- the
    // OpenBlock's first WriteLine terminates the mid-line ".assembly" name)
    // and the first attribute is the extension attribute.
    EXPECT_EQ(actual.rfind(
        ".assembly mscorlib\r\n"
        "{\r\n"
        "\t.custom instance void System.Runtime.CompilerServices."
        "ExtensionAttribute::.ctor() = (\r\n"
        "\t\t01 00 00 00\r\n"
        "\t)\r\n", 0), 0u);

    // 36 assembly-level custom attributes and the one assembly-level
    // security declaration (RequestMinimum -- "reqmin").
    int customLines = 0, permissionsets = 0;
    for (std::size_t p = 0; (p = actual.find("\t.custom ", p)) !=
        std::string::npos; p += 1) customLines++;
    for (std::size_t p = 0; (p = actual.find(".permissionset ", p)) !=
        std::string::npos; p += 1) permissionsets++;
    EXPECT_EQ(customLines, 36);
    EXPECT_EQ(permissionsets, 1);
    EXPECT_NE(actual.find("\t.permissionset reqmin = (\r\n"
        "\t\t2e 01 80 84 53 79 73 74 65 6d 2e 53 65 63 75 72\r\n"),
        std::string::npos);

    // The exact tail: public key, hash algorithm with the SHA1 comment,
    // version, close brace.
    std::string tail =
        "\t.publickey = (\r\n"
        "\t\t00 00 00 00 00 00 00 00 04 00 00 00 00 00 00 00\r\n"
        "\t)\r\n"
        "\t.hash algorithm 0x00008004 // SHA1\r\n"
        "\t.ver 4:0:0:0\r\n"
        "}\r\n";
    EXPECT_EQ(actual.substr(actual.size() - tail.size()), tail);
}

TEST(ReflectionDisassemblerTest, WriteAssemblyHeaderCoreLibNoPermissionset)
{
    std::string path = CoreLibPath();
    if (path.empty()) {
        GTEST_SKIP() << "no .NET shared runtime System.Private.CoreLib.dll";
    }
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteAssemblyHeader(f); });

    // CoreLib has no assembly-level security declarations (the
    // WriteSecurityDeclarations early-out) and a 160-byte public key that
    // spans 10 blob lines.
    EXPECT_EQ(actual.rfind(".assembly System.Private.CoreLib\r\n{\r\n", 0), 0u);
    EXPECT_EQ(actual.find(".permissionset"), std::string::npos);
    int customLines = 0;
    for (std::size_t p = 0; (p = actual.find("\t.custom ", p)) !=
        std::string::npos; p += 1) customLines++;
    EXPECT_EQ(customLines, 22);
    std::string tail =
        "\t.publickey = (\r\n"
        "\t\t00 24 00 00 04 80 00 00 94 00 00 00 06 02 00 00\r\n"
        "\t\t00 24 00 00 52 53 41 31 00 04 00 00 01 00 01 00\r\n"
        "\t\t8d 56 c7 6f 9e 86 49 38 30 49 f3 83 c4 4b e0 ec\r\n"
        "\t\t20 41 81 82 2a 6c 31 cf 5e b7 ef 48 69 44 d0 32\r\n"
        "\t\t18 8e a1 d3 92 07 63 71 2c cb 12 d7 5f b7 7e 98\r\n"
        "\t\t11 14 9e 61 48 e5 d3 2f ba ab 37 61 1c 18 78 dd\r\n"
        "\t\tc1 9e 20 ef 13 5d 0c b2 cf f2 bf ec 3d 11 58 10\r\n"
        "\t\tc3 d9 06 96 38 fe 4b e2 15 db f7 95 86 19 20 e5\r\n"
        "\t\tab 6f 7d b2 e2 ce ef 13 6a c2 3d 5d d2 bf 03 17\r\n"
        "\t\t00 ae c2 32 f6 c6 b1 c7 85 b4 30 5c 12 3b 37 ab\r\n"
        "\t)\r\n"
        "\t.hash algorithm 0x00008004 // SHA1\r\n"
        "\t.ver 10:0:0:0\r\n"
        "}\r\n";
    EXPECT_EQ(actual.substr(actual.size() - tail.size()), tail);
}

TEST(ReflectionDisassemblerTest, WriteAssemblyHeaderNetModuleRendersNothing)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());

    // A netmodule (an empty Assembly table) is not an assembly: the read is
    // nullopt and the manifest header renders nothing at all.
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    EXPECT_FALSE(f.GetAssemblyDefinition().has_value());
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteAssemblyHeader(f); });
    EXPECT_TRUE(actual.empty());

    // The netmodule still declares its autodetected mscorlib extern (with
    // the resolved public key token), rendered by WriteAssemblyReferences.
    std::string refs = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteAssemblyReferences(f); });
    EXPECT_EQ(refs,
        ".assembly extern mscorlib\r\n"
        "{\r\n"
        "\t.publickeytoken = (\r\n"
        "\t\tb7 7a 5c 56 19 34 e0 89\r\n"
        "\t)\r\n"
        "\t.ver 4:0:0:0\r\n"
        "}\r\n");
    std::remove(path.c_str());
}

TEST(ReflectionDisassemblerTest, DisassembleNamespaceRendersNamespaceBlock)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // The first two Microsoft.Runtime.Hosting types in table order
    // (StrongNameHelpers, IClrStrongNameUsingIntPtr -- the same pair the
    // gold render pinned).
    std::uint32_t strongNameHelpers =
        FindTypeDefTokenIn(f, "Microsoft.Runtime.Hosting", "StrongNameHelpers");
    std::uint32_t usingIntPtr =
        FindTypeDefTokenIn(f, "Microsoft.Runtime.Hosting", "IClrStrongNameUsingIntPtr");
    ASSERT_NE(strongNameHelpers, 0u);
    ASSERT_NE(usingIntPtr, 0u);

    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) {
            rd.DisassembleNamespace("Microsoft.Runtime.Hosting", f,
                {strongNameHelpers, usingIntPtr});
        });

    // The wrapper block opens with the escaped namespace name (no blank
    // line -- OpenBlock's first WriteLine terminates the mid-line header)
    // and the type renders land at one indent level.
    EXPECT_EQ(actual.rfind(
        ".namespace Microsoft.Runtime.Hosting\r\n"
        "{\r\n"
        "\t.class private auto ansi abstract sealed beforefieldinit "
        "Microsoft.Runtime.Hosting.StrongNameHelpers\r\n"
        "\t\textends System.Object\r\n"
        "\t{\r\n", 0), 0u);

    // The two type renders are separated by exactly one blank line (the
    // loop's WriteLine after each type).
    EXPECT_NE(actual.find(
        "\t} // end of class Microsoft.Runtime.Hosting.StrongNameHelpers\r\n"
        "\r\n"
        "\t.class "), std::string::npos);

    // The block closes after the last type's trailing blank line.
    std::string tail =
        "\t} // end of class Microsoft.Runtime.Hosting.IClrStrongNameUsingIntPtr\r\n"
        "\r\n"
        "}\r\n";
    EXPECT_EQ(actual.substr(actual.size() - tail.size()), tail);
}

TEST(ReflectionDisassemblerTest, DisassembleNamespaceEmptyRendersBareTypes)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // An empty namespace string renders no wrapper: the types land at top
    // level, each followed by one blank line (the ILSpy app's global
    // namespace grouping).
    std::uint32_t clrStrongName =
        FindTypeDefTokenIn(f, "Microsoft.Runtime.Hosting", "IClrStrongName");
    ASSERT_NE(clrStrongName, 0u);

    std::string typeRender = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.DisassembleType(f, clrStrongName); });
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) {
            rd.DisassembleNamespace("", f, {clrStrongName});
        });
    EXPECT_EQ(actual, typeRender + "\r\n");
    EXPECT_EQ(actual.find(".namespace"), std::string::npos);
}


// ===========================================================================
// WriteModuleHeader / WriteModuleContents (ReflectionDisassembler.cs lines
// 2119-2206) -- the module-level chain that completes the ilspycmd --il
// whole-module path (the Tester.cs gold order: WriteAssemblyReferences,
// WriteAssemblyHeader, WriteModuleHeader, WriteModuleContents).
// ===========================================================================

// The tiny_manifest.dll fixture: a 2 KB multi-file-assembly manifest built
// with System.Reflection.Metadata's MetadataBuilder/ManagedPEBuilder -- the
// File-implementation and nested-ExportedType arms no installed DLL carries
// (every local assembly forwards through AssemblyRefs only). Six
// ExportedType rows cover: a File implementation with a nonzero TypeDefId
// (the ".class 0x..." line), a plain File row, a nested exported type
// (Implementation -> the enclosing ExportedType row), a doubly nested one
// (the chain links render concatenated, faithful to the C# while-loop), an
// AssemblyRef forwarder, and a zero-flags File row (no "forwarder" prefix --
// the SRM IsForwarder property fuses the flag with an AssemblyReference
// implementation). The MVID carries hex letters (pinning the
// ToUpperInvariant render), the subsystem is 17 (unnamed -> the decimal
// spelling) and the corflags carry an unmatched bit (the full-value decimal
// fallback). It declares no TypeDefs, so WriteModuleContents renders nothing
// over it. The exact renders are byte-identical to the real C#
// ReflectionDisassembler over the same bytes (gold dumped from the installed
// ilspycmd's own ICSharpCode.Decompiler.dll).
const char* kTinyManifestHex[] = {
    "4D5A90000300000004000000FFFF0000B8000000000000004000000000000000",
    "0000000000000000000000000000000000000000000000000000000080000000",
    "0E1FBA0E00B409CD21B8014CCD21546869732070726F6772616D2063616E6E6F",
    "742062652072756E20696E20444F53206D6F64652E0D0D0A2400000000000000",
    "504500004C010200AA919A6A0000000000000000E00000200B01300000040000",
    "0002000000000000DE2200000020000000400000000040000020000000020000",
    "0400000000000000040000000000000000600000000200000000000011004085",
    "0000100000100000000010000010000000000000100000000000000000000000",
    "8C2200004F000000000000000000000000000000000000000000000000000000",
    "004000000C000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000020000008000000",
    "0000000000000000082000004800000000000000000000002E74657874000000",
    "E402000000200000000400000002000000000000000000000000000020000060",
    "2E72656C6F6300000C0000000040000000020000000600000000000000000000",
    "0000000040000042000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "C022000000000000480000000200050050200000BC0100000900004000000000",
    "00000000000000000C2200008000000000000000000000000000000000000000",
    "0000000000000000000000000000000042534A42010001000000000014000000",
    "74696E795F6D616E69666573742E646C6C0000000000050074000000C0000000",
    "237E0000340100007000000023537472696E677300000000A401000004000000",
    "23555300A8010000100000002347554944000000B80100000400000023426C6F",
    "62000000000000000200000101000000C900000000FA01330016000001000000",
    "0100000001000000010000000600000000002700010000000000048000000100",
    "0200030004000000000000005C00000004000000000000000000000000000300",
    "00000000000000000C00000000002000000400006A0039000400000020000000",
    "00005600460004000000200000040000500000000A0000002000000000004B00",
    "00000E0000002000000000001F00390005000000000000000000400001000400",
    "00000000004E006D73636F726C69620074696E7966696C652E6E65746D6F6475",
    "6C6500467764547970650074696E795F6D616E69666573742E646C6C00537973",
    "74656D00506C61696E0044656D6F004465657000496E6E6572004F7574657200",
    "74696E795F6D616E69666573740054696E7900000000000001EFCDAB45238967",
    "ABCDEF0123456789000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "000000000000000000000000B42200000000000000000000CE22000000200000",
    "0000000000000000000000000000000000000000C02200000000000000000000",
    "00005F436F72446C6C4D61696E006D73636F7265652E646C6C0000000000FF25",
    "0020400000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "002000000C000000E03200000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000",
    "0000000000000000000000000000000000000000000000000000000000000000"
};

// Writes the embedded manifest bytes to a temp file and returns the path
// (MetadataFile needs a real file).
std::string WriteTinyManifest() {
    std::string bytes;
    for (const char* line : kTinyManifestHex) {
        std::size_t len = std::strlen(line);
        EXPECT_EQ(len, 64u);
        for (std::size_t i = 0; i + 1 < len; i += 2) {
            int hi = line[i] <= '9' ? line[i] - '0' : (line[i] | 32) - 'a' + 10;
            int lo = line[i + 1] <= '9' ? line[i + 1] - '0' : (line[i + 1] | 32) - 'a' + 10;
            bytes.push_back(static_cast<char>(hi * 16 + lo));
        }
    }
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "ilspy_tiny_manifest_test.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    return path.string();
}

TEST(ReflectionDisassemblerTest, ModuleHeaderMetadataReads)
{
    MD::MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());

    // The Module table's single row: mscorlib's module name is the authored
    // "CommonLanguageRuntimeLibrary", and the MVID GUID bytes render in the
    // brace form little-endian for the first three groups.
    auto moduleDef = mscorlib.GetModuleDefinition();
    ASSERT_TRUE(moduleDef.has_value());
    EXPECT_EQ(moduleDef->Name, "CommonLanguageRuntimeLibrary");
    const std::uint8_t expectedMvid[] = {0x0E, 0xD9, 0x6A, 0xF1, 0xD0, 0x81,
        0xDF, 0x45, 0xA3, 0x41, 0x0E, 0x50, 0xBB, 0x08, 0xED, 0x27};
    EXPECT_EQ(std::vector<std::uint8_t>(moduleDef->Mvid.begin(),
                  moduleDef->Mvid.end()),
        std::vector<std::uint8_t>(std::begin(expectedMvid),
            std::end(expectedMvid)));
    // An invalid file reports no module row.
    MD::MetadataFile missing("Z:\\no\\such\\file.dll");
    EXPECT_FALSE(missing.IsValid());
    EXPECT_FALSE(missing.GetModuleDefinition().has_value());

    // The PE-header values mscorlib's module header renders: a PE32+ image
    // (the 64-bit ImageBase renders all its digits at the x8 minimum
    // width), stack reserve 4 MB, console subsystem 3, ILOnly +
    // StrongNameSigned.
    auto headers = mscorlib.GetPeHeaderInfo();
    ASSERT_TRUE(headers.has_value());
    EXPECT_EQ(headers->ImageBase, 0x64478000000ull);
    EXPECT_EQ(headers->FileAlignment, 0x200u);
    EXPECT_EQ(headers->SizeOfStackReserve, 0x400000ull);
    EXPECT_EQ(headers->Subsystem, 3u);
    EXPECT_EQ(headers->CorFlags, 9u);
    EXPECT_FALSE(missing.GetPeHeaderInfo().has_value());

    // mscorlib carries no exported types (it is self-contained; System.dll
    // is the local forwarder fixture) but declares five File-table rows
    // (the .nlp normalization data files) -- row 1 is normidna.nlp.
    EXPECT_TRUE(mscorlib.GetExportedTypes().empty());
    EXPECT_FALSE(mscorlib.GetExportedType(0x27000001u).has_value());
    EXPECT_EQ(mscorlib.GetAssemblyFileName(0x26000001u),
        std::optional<std::string>("normidna.nlp"));

    // The top-level type definitions: row order, the <Module> row first,
    // every nested type excluded (mscorlib declares 3356 TypeDefs, 660 of
    // them nested -- 2696 remain), String top-level and Win32Native's
    // SystemTime not.
    auto topLevel = mscorlib.GetTopLevelTypeDefinitions();
    ASSERT_EQ(topLevel.size(), 2696u);
    EXPECT_EQ(topLevel.front(), 0x02000001u);
    EXPECT_EQ(topLevel[1], 0x02000002u);
    bool sawString = false, sawSystemTime = false;
    for (std::uint32_t t : topLevel) {
        if (auto info = mscorlib.GetTypeDefNameInfo(t)) {
            if (info->Name == "String" && info->Namespace == "System")
                sawString = true;
            if (info->Name == "SystemTime") sawSystemTime = true;
        }
    }
    EXPECT_TRUE(sawString);
    EXPECT_FALSE(sawSystemTime);

    MD::MetadataFile systemDll(SystemDllPath());
    ASSERT_TRUE(systemDll.IsValid());
    auto exported = systemDll.GetExportedTypes();
    ASSERT_EQ(exported.size(), 1u);
    EXPECT_EQ(exported[0].Token, 0x27000001u);
    EXPECT_EQ(exported[0].Namespace, "System.Threading");
    EXPECT_FALSE(exported[0].NamespaceNil);
    EXPECT_EQ(exported[0].Name, "SemaphoreFullException");
    EXPECT_EQ(exported[0].Attributes, 0x00200000u);
    EXPECT_EQ(exported[0].TypeDefinitionId, 0u);
    EXPECT_EQ(exported[0].ImplementationToken, 0x23000001u);
    // The single-row read over the same token.
    auto one = systemDll.GetExportedType(0x27000001u);
    ASSERT_TRUE(one.has_value());
    EXPECT_EQ(one->Name, "SemaphoreFullException");
    EXPECT_FALSE(systemDll.GetExportedType(0x02000001u).has_value());
    EXPECT_FALSE(systemDll.GetExportedType(0x27000002u).has_value());
    // The forwarder's AssemblyRef name read (the block body's
    // ".assembly extern mscorlib" line).
    EXPECT_EQ(systemDll.GetAssemblyReferenceName(0x23000001u),
        std::optional<std::string>("mscorlib"));
    EXPECT_FALSE(systemDll.GetAssemblyReferenceName(0x23000009u).has_value());
    EXPECT_FALSE(systemDll.GetAssemblyReferenceName(0x06000001u).has_value());

    // The synthetic manifest's File row: the ".file tinyfile.netmodule"
    // body line, plus the nested-row reads (nil namespaces, the chain
    // targets).
    std::string manifestPath = WriteTinyManifest();
    ASSERT_FALSE(manifestPath.empty());
    MD::MetadataFile manifest(manifestPath);
    ASSERT_TRUE(manifest.IsValid());
    EXPECT_EQ(manifest.GetAssemblyFileName(0x26000001u),
        std::optional<std::string>("tinyfile.netmodule"));
    EXPECT_FALSE(manifest.GetAssemblyFileName(0x26000002u).has_value());
    auto rows = manifest.GetExportedTypes();
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_EQ(rows[0].Token, 0x27000001u);
    EXPECT_EQ(rows[0].Name, "Tiny");
    EXPECT_EQ(rows[0].Namespace, "System");
    EXPECT_EQ(rows[0].TypeDefinitionId, 0x400u);
    EXPECT_EQ(rows[0].ImplementationToken, 0x26000001u);
    EXPECT_EQ(rows[0].Attributes, 0x00200000u);
    // The nested rows carry nil namespaces and point at the enclosing row.
    EXPECT_EQ(rows[2].Name, "Inner");
    EXPECT_TRUE(rows[2].NamespaceNil);
    EXPECT_EQ(rows[2].Namespace, "");
    EXPECT_EQ(rows[2].ImplementationToken, 0x27000002u);
    EXPECT_EQ(rows[3].Name, "Deep");
    EXPECT_EQ(rows[3].ImplementationToken, 0x27000003u);
    // The zero-flags row.
    EXPECT_EQ(rows[5].Attributes, 0u);
    EXPECT_EQ(rows[5].ImplementationToken, 0x26000001u);
    // The manifest's own module row and the weird PE values.
    auto manifestDef = manifest.GetModuleDefinition();
    ASSERT_TRUE(manifestDef.has_value());
    EXPECT_EQ(manifestDef->Name, "tiny_manifest.dll");
    auto manifestHeaders = manifest.GetPeHeaderInfo();
    ASSERT_TRUE(manifestHeaders.has_value());
    EXPECT_EQ(manifestHeaders->ImageBase, 0x400000ull);
    EXPECT_EQ(manifestHeaders->Subsystem, 17u);
    EXPECT_EQ(manifestHeaders->CorFlags, 0x40000009u);
    // No TypeDefs: the contents walk is empty.
    EXPECT_TRUE(manifest.GetTopLevelTypeDefinitions().empty());
}

TEST(ReflectionDisassemblerTest, WriteModuleHeaderMscorlibExact)
{
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // The exact gold (byte-identical to the real C# render): no exported
    // types, the module name, the MVID, the PE32+ imagebase at the x8
    // minimum width, the subsystem/corflags spellings, and the module's own
    // UnverifiableCodeAttribute (a same-module TypeRef, so no "[mscorlib]"
    // scope prefix).
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleHeader(f); });
    std::string expected =
        ".module CommonLanguageRuntimeLibrary\r\n"
        "// MVID: {F16AD90E-81D0-45DF-A341-0E50BB08ED27}\r\n"
        ".imagebase 0x64478000000\r\n"
        ".file alignment 0x00000200\r\n"
        ".stackreserve 0x00400000\r\n"
        ".subsystem 0x0003 // WindowsCui\r\n"
        ".corflags 0x00000009 // ILOnly, StrongNameSigned\r\n"
        ".custom instance void System.Security.UnverifiableCodeAttribute::.ctor() = (\r\n"
        "\t01 00 00 00\r\n"
        ")\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteModuleHeaderSystemDllExact)
{
    MD::MetadataFile f(SystemDllPath());
    ASSERT_TRUE(f.IsValid());

    // The exact gold: the one forwarder block (the SRM IsForwarder fuse: the
    // flag AND an AssemblyReference implementation), the ".assembly extern"
    // body, and the module attribute's cross-assembly "[mscorlib]" scope.
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleHeader(f); });
    std::string expected =
        ".class extern forwarder System.Threading.SemaphoreFullException\r\n"
        "{\r\n"
        "\t.assembly extern mscorlib\r\n"
        "}\r\n"
        ".module System.dll\r\n"
        "// MVID: {402AF08D-116E-430D-9D66-BA6EE06AD4BB}\r\n"
        ".imagebase 0x7a540000\r\n"
        ".file alignment 0x00000200\r\n"
        ".stackreserve 0x00100000\r\n"
        ".subsystem 0x0003 // WindowsCui\r\n"
        ".corflags 0x00000009 // ILOnly, StrongNameSigned\r\n"
        ".custom instance void [mscorlib]System.Security.UnverifiableCodeAttribute::.ctor() = (\r\n"
        "\t01 00 00 00\r\n"
        ")\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteModuleHeaderNetModuleExact)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The exact gold: a netmodule's module header (module name + MVID + the
    // PE lines; corflags ILOnly only -- ilasm-assembled modules are not
    // strong-name signed).
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleHeader(f); });
    std::string expected =
        ".module tiny\r\n"
        "// MVID: {163CEFD4-809A-4644-82FD-23D0EB508D60}\r\n"
        ".imagebase 0x00400000\r\n"
        ".file alignment 0x00000200\r\n"
        ".stackreserve 0x00100000\r\n"
        ".subsystem 0x0003 // WindowsCui\r\n"
        ".corflags 0x00000001 // ILOnly\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteModuleHeaderSkipMvidOmitsMvidLine)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The skipMVID=true variant (the Tester.cs gold-generator's form): the
    // MVID line is the only thing omitted.
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleHeader(f, true); });
    std::string expected =
        ".module tiny\r\n"
        ".imagebase 0x00400000\r\n"
        ".file alignment 0x00000200\r\n"
        ".stackreserve 0x00100000\r\n"
        ".subsystem 0x0003 // WindowsCui\r\n"
        ".corflags 0x00000001 // ILOnly\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteModuleHeaderSyntheticArmsExact)
{
    std::string path = WriteTinyManifest();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The exact gold over every exported-type arm: the File implementation
    // with the nonzero TypeDefId, the nested row (the header renders the
    // row's own nil-namespace name; the body renders the enclosing row),
    // the doubly nested one (the chain links concatenate -- faithful to the
    // C# while-loop), the AssemblyRef forwarder, the zero-flags row (no
    // "forwarder" prefix), then the module name, the uppercased hex-letter
    // MVID, the unnamed subsystem 17, and the unmatched-bit corflags.
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleHeader(f); });
    std::string expected =
        ".class extern System.Tiny\r\n"
        "{\r\n"
        "\t.file tinyfile.netmodule\r\n"
        "\t.class 0x00000400\r\n"
        "}\r\n"
        ".class extern Demo.Outer\r\n"
        "{\r\n"
        "\t.file tinyfile.netmodule\r\n"
        "}\r\n"
        ".class extern Inner\r\n"
        "{\r\n"
        "\t.class extern Demo.Outer\r\n"
        "}\r\n"
        ".class extern Deep\r\n"
        "{\r\n"
        "\t.class extern InnerDemo.Outer\r\n"
        "}\r\n"
        ".class extern forwarder System.FwdType\r\n"
        "{\r\n"
        "\t.assembly extern mscorlib\r\n"
        "}\r\n"
        ".class extern N.Plain\r\n"
        "{\r\n"
        "\t.file tinyfile.netmodule\r\n"
        "}\r\n"
        ".module tiny_manifest.dll\r\n"
        "// MVID: {ABCDEF01-2345-6789-ABCD-EF0123456789}\r\n"
        ".imagebase 0x00400000\r\n"
        ".file alignment 0x00000200\r\n"
        ".stackreserve 0x00100000\r\n"
        ".subsystem 0x0011 // 17\r\n"
        ".corflags 0x40000009 // 1073741833\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteModuleHeaderSystemRuntimeForwarderSweep)
{
    std::string path = "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System"
        ".Runtime\\v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
    if (!std::filesystem::exists(path))
        GTEST_SKIP() << "the GAC System.Runtime facade is Windows-only";
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The System.Runtime facade is the many-forwarder fixture: 279
    // ExportedType rows -- 273 AssemblyRef forwarders plus SIX nested
    // exported types (the exported-type rows whose Implementation points
    // at another row: System.Diagnostics.DebuggingModes,
    // ConditionalWeakTable`2, RuntimeHelpers twice, AdjustmentRule and
    // TransitionTime) -- then the module tail with no module-level
    // custom attributes.
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleHeader(f); });
    ASSERT_EQ(f.GetExportedTypes().size(), 279u);
    std::size_t blocks = 0;
    for (std::size_t pos = 0;
         (pos = actual.find(".class extern forwarder ", pos)) != std::string::npos;
         pos += 1)
        blocks++;
    EXPECT_EQ(blocks, 273u);
    // The nested exported type chain body (a REAL nested-row fixture
    // beyond the synthetic manifest: the header renders the row's own
    // nil-namespace name, the body the enclosing row).
    std::string nestedBlock =
        ".class extern DebuggingModes\r\n"
        "{\r\n"
        "\t.class extern System.Diagnostics.DebuggableAttribute\r\n"
        "}\r\n";
    EXPECT_NE(actual.find(nestedBlock), std::string::npos);
    std::string prefix = ".class extern forwarder System.Action";
    EXPECT_TRUE(actual.rfind(prefix, 0) == 0);
    std::string tail =
        ".module System.Runtime.dll\r\n"
        "// MVID: {BC07CD82-B9CF-482E-8948-4C252F8E1FDB}\r\n"
        ".imagebase 0x10000000\r\n"
        ".file alignment 0x00000200\r\n"
        ".stackreserve 0x00100000\r\n"
        ".subsystem 0x0003 // WindowsCui\r\n"
        ".corflags 0x00000009 // ILOnly, StrongNameSigned\r\n";
    ASSERT_GE(actual.size(), tail.size());
    EXPECT_EQ(actual.substr(actual.size() - tail.size()), tail);
}

TEST(ReflectionDisassemblerTest, WriteModuleContentsTinyExact)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The exact gold: the CLI's whole-module -il render over the tiny
    // netmodule -- <Module> and Tiny in top-level row order, each type
    // followed by a blank line (including the last).
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleContents(f); });
    std::string expected =
        ".class private auto ansi '<Module>'\r\n"
        "{\r\n"
        "} // end of class <Module>\r\n"
        "\r\n"
        ".class public auto ansi Tiny\r\n"
        "\textends [mscorlib]System.Object\r\n"
        "{\r\n"
        "\t// Methods\r\n"
        "\t.method public hidebysig static \r\n"
        "\t\tint32 Add (\r\n"
        "\t\t\tint32 a,\r\n"
        "\t\t\tint32 b\r\n"
        "\t\t) cil managed \r\n"
        "\t{\r\n"
        "\t\t// Method begins at RVA 0x2050\r\n"
        "\t\t// Header size: 12\r\n"
        "\t\t// Code size: 4 (0x4)\r\n"
        "\t\t.maxstack 2\r\n"
        "\r\n"
        "\t\tIL_0000: ldarg.0\r\n"
        "\t\tIL_0001: ldarg.1\r\n"
        "\t\tIL_0002: add\r\n"
        "\t\tIL_0003: ret\r\n"
        "\t} // end of method Tiny::Add\r\n"
        "\r\n"
        "} // end of class Tiny\r\n"
        "\r\n";
    EXPECT_EQ(actual, expected);
}

TEST(ReflectionDisassemblerTest, WriteModuleContentsRoutesThroughEntityProcessor)
{
    std::string path = WriteTinyNetModule();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The EntityProcessor hook routes the top-level collection: a reversing
    // processor renders Tiny first (the DisassembleTypeEntityProcessor
    // convention for the member collections, applied to the module walk).
    ReversingEntityProcessor processor;
    std::string actual = RenderWithDisassembler([&](DA::ReflectionDisassembler& rd) {
        rd.EntityProcessor(&processor);
        rd.WriteModuleContents(f);
    });
    // Tiny first, <Module> second (the reversal), each still blank-line
    // separated.
    std::string tinyHead = ".class public auto ansi Tiny\r\n";
    EXPECT_TRUE(actual.rfind(tinyHead, 0) == 0);
    std::string tail = "} // end of class <Module>\r\n\r\n";
    ASSERT_GE(actual.size(), tail.size());
    EXPECT_EQ(actual.substr(actual.size() - tail.size()), tail);
}

TEST(ReflectionDisassemblerTest, WriteModuleContentsEmptyModuleRendersNothing)
{
    std::string path = WriteTinyManifest();
    ASSERT_FALSE(path.empty());
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // The synthetic manifest declares no types: the whole-module render is
    // empty (the C# loop body never runs).
    std::string actual = RenderWithDisassembler(
        [&](DA::ReflectionDisassembler& rd) { rd.WriteModuleContents(f); });
    EXPECT_TRUE(actual.empty());
}

