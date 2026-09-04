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
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace DA = ILSpy::Decompiler::Disassembler;
namespace OUT = ILSpy::Decompiler::Output;

namespace {

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
