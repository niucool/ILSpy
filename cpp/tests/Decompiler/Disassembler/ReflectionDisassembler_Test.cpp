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
