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
// line geometry, the OpenBlock/CloseBlock "{" ... "} // comment" pair, and
// the WriteMarshalInfo/WriteNativeType II.23.4 marshalling-descriptor walk
// (the simple native-type spellings, the array/fixed-sysstring/safearray/
// fixed-array/custom-marshaler shapes over synthetic blobs).

#include "Decompiler/Disassembler/MethodBodyDisassembler.hpp"
#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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

// The MethodSignatureT of a real method (the DisassemblerSignatureTypeProvider
// decode the C# DisassembleMethodHeader runs). The holder keeps the provider
// alive: the signature's deferred writers capture it (the provider-must-
// outlive-the-writers liveness contract).
struct MethodSignatureHolder {
    std::unique_ptr<DA::DisassemblerSignatureTypeProvider> provider;
    std::unique_ptr<MD::SignatureTypeProviderDecoder> decoder;
    MD::MethodSignatureT sig;
};

MethodSignatureHolder MethodSignatureOf(const MD::MetadataFile& f,
    std::uint32_t methodToken, OUT::ITextOutput& output) {
    auto blob = f.GetSignatureBlob(methodToken);
    MethodSignatureHolder holder;
    holder.provider = std::make_unique<DA::DisassemblerSignatureTypeProvider>(
        f, output);
    holder.decoder = std::make_unique<MD::SignatureTypeProviderDecoder>(
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
