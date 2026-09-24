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

// Tests for the GuessFileType port (ICSharpCode.ILSpyX/Util/GuessFileType.cs)
// -- every class of the C# three-stage pipeline: the two-byte BOM dispatch
// (UTF-8/UTF-16LE/UTF-16BE/UTF-32LE, the truncated-BOM rejection), the
// RFC 3629 state machine (valid multi-byte sequences, the 0xc0/0xc1/0xf5+
// rejects, the 500 KB cap), and the XML probe over the decoded text
// (well-formed documents Xml, everything else Text).

#include "ILSpyX/Util/GuessFileType.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace Util = ILSpy::ILSpyX::Util;
using Util::FileType;

Util::FileType Detect(const std::string& bytes)
{
    return Util::DetectFileType(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

Util::FileType DetectText(const std::string& text)
{
    return Detect(text);
}

}  // namespace

// ---- The size gate: anything under two bytes is Binary.

TEST(GuessFileTypeTest, EmptyAndOneByteInputsAreBinary)
{
    EXPECT_EQ(Detect(""), FileType::Binary);
    EXPECT_EQ(Detect("a"), FileType::Binary);
}

// ---- Plain UTF-8 text: valid ASCII and multi-byte sequences are Text.

TEST(GuessFileTypeTest, AsciiTextIsText)
{
    EXPECT_EQ(DetectText("hello world"), FileType::Text);
    EXPECT_EQ(DetectText("ab"), FileType::Text);
}

TEST(GuessFileTypeTest, ValidUtf8MultibyteTextIsText)
{
    // 2-byte (U+00E9 e-acute), 3-byte (U+20AC euro), 4-byte (U+1F600 emoji)
    // sequences embedded in ASCII text.
    EXPECT_EQ(DetectText("caf\xC3\xA9"), FileType::Text);
    EXPECT_EQ(DetectText("\xE2\x82\xAC 100"), FileType::Text);
    EXPECT_EQ(DetectText("\xF0\x9F\x98\x80!"), FileType::Text);
}

// ---- The RFC 3629 state machine: invalid UTF-8 is Binary.

TEST(GuessFileTypeTest, InvalidUtf8BytesAreBinary)
{
    EXPECT_EQ(Detect("plain\xFFjunk"), FileType::Binary);
    // 0xc0 and 0xc1 are invalid lead bytes (overlong encodings).
    EXPECT_EQ(Detect("\xC0\x80text"), FileType::Binary);
    EXPECT_EQ(Detect("\xC1\xBFtext"), FileType::Binary);
    // 0xf5 through 0xff are invalid lead bytes.
    EXPECT_EQ(Detect("\xF5text"), FileType::Binary);
    EXPECT_EQ(Detect("\xFF\xFF\xFF"), FileType::Binary);
}

TEST(GuessFileTypeTest, TruncatedUtf8SequenceIsBinary)
{
    // A 3-byte lead followed by a continuation then ASCII: the sequence is
    // cut short by an ASCII char while state == UTF8Sequence -> Error.
    EXPECT_EQ(Detect("\xE2\x82x"), FileType::Binary);
    // A continuation byte with no lead.
    EXPECT_EQ(Detect("\x80abc"), FileType::Binary);
}

// ---- The UTF-8 BOM arm.

TEST(GuessFileTypeTest, Utf8BomTextIsTextAndXmlIsXml)
{
    const std::string bom = "\xEF\xBB\xBF";
    EXPECT_EQ(Detect(bom + "just text"), FileType::Text);
    EXPECT_EQ(Detect(bom + "<root>\n  <a />\n</root>"), FileType::Xml);
}

TEST(GuessFileTypeTest, TruncatedUtf8BomIsBinary)
{
    // EF BB followed by anything but BF: rejected before the text stage.
    EXPECT_EQ(Detect("\xEF\xBBplain"), FileType::Binary);
    EXPECT_EQ(Detect(std::string("\xEF\xBB") + std::string(1, '\x00')),
        FileType::Binary);
}

TEST(GuessFileTypeTest, Utf8BomWithInvalidUtf8PayloadIsTextNotBinary)
{
    // The BOM arm skips the IsUTF8 gate entirely: the C# UTF-8
    // StreamReader decodes invalid bytes to U+FFFD (it never throws), so
    // the payload reaches the XML stage and classifies as Text.
    const std::string bom = "\xEF\xBB\xBF";
    EXPECT_EQ(Detect(bom + "junk\xFF\xFEbytes"), FileType::Text);
}

TEST(GuessFileTypeTest, TwoByteBomAloneDecodesToEmptyTextAndIsText)
{
    // A lone UTF-16LE BOM: the decode consumes it, the empty text fails
    // the XML probe ("Root element is missing"), and the empty input is
    // never Binary because the BOM arm bypasses the UTF-8 gate.
    EXPECT_EQ(Detect("\xFF\xFE"), FileType::Text);
}

// ---- The UTF-16 BOM arms.

TEST(GuessFileTypeTest, Utf16LeBomTextIsTextAndXmlIsXml)
{
    // UTF-16LE "hello": BOM + one NUL-padded code unit per char (the
    // NUL is appended as a char, not via the "\0" literal whose strlen
    // is zero).
    std::string text = "\xFF\xFE";
    for (char c : std::string("hello")) {
        text += std::string(1, c);
        text += '\0';
    }
    EXPECT_EQ(Detect(text), FileType::Text);

    std::string xml = "\xFF\xFE";
    for (char c : std::string("<root />")) {
        xml += std::string(1, c);
        xml += '\0';
    }
    EXPECT_EQ(Detect(xml), FileType::Xml);
}

TEST(GuessFileTypeTest, Utf16BeBomTextIsText)
{
    // UTF-16BE "hi": BOM + NUL-prefixed code units.
    std::string text = "\xFE\xFF";
    for (char c : std::string("hi"))
        text += std::string(1, '\0') + std::string(1, c);
    EXPECT_EQ(Detect(text), FileType::Text);
}

TEST(GuessFileTypeTest, Utf16LeBomWithNonZeroSecondUnitIsUtf16NotUtf32)
{
    // FF FE 00 41 is a UTF-16LE BOM followed by U+4100 (not a UTF-32LE
    // BOM, which needs 00 00 in the second unit).
    std::string text = "\xFF\xFE\x00\x41";
    EXPECT_EQ(Detect(text), FileType::Text);
}

TEST(GuessFileTypeTest, Utf32LeBomTextIsText)
{
    // UTF-32LE "hi": FF FE 00 00 then two NUL-padded 4-byte code units.
    std::string text = "\xFF\xFE\x00\x00";
    text += "h\0\0\0";
    text += "i\0\0\0";
    EXPECT_EQ(Detect(text), FileType::Text);
}

// ---- UTF-16 without a BOM: the bytes are often valid UTF-8, and the
// NUL units then fail the XML probe -> Text.

TEST(GuessFileTypeTest, Utf16WithoutBomDecodesAsUtf8WithNulsAndIsText)
{
    std::string text;
    for (char c : std::string("hello")) {
        text += std::string(1, c);
        text += '\0';
    }
    EXPECT_EQ(Detect(text), FileType::Text);
}

// ---- The XML probe: well-formed documents are Xml; malformed starts and
// plain text are Text.

TEST(GuessFileTypeTest, WellFormedXmlIsXml)
{
    EXPECT_EQ(DetectText("<root>\n  <a attr=\"1\" />\n</root>"), FileType::Xml);
    EXPECT_EQ(DetectText("<root/>"), FileType::Xml);
    EXPECT_EQ(DetectText("<root></root>"), FileType::Xml);
}

TEST(GuessFileTypeTest, XmlWithDeclarationPrologueAndCommentsIsXml)
{
    EXPECT_EQ(DetectText("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<root />"),
        FileType::Xml);
    EXPECT_EQ(DetectText("<!-- a comment -->\n<?pi data?>\n<root />"),
        FileType::Xml);
    EXPECT_EQ(DetectText("\n  <root />\n"), FileType::Xml);
}

TEST(GuessFileTypeTest, MalformedXmlStartIsText)
{
    EXPECT_EQ(DetectText("<root"), FileType::Text);          // truncated tag
    EXPECT_EQ(DetectText("<root></other>"), FileType::Text); // mismatched end
    EXPECT_EQ(DetectText("</root>"), FileType::Text);        // stray end tag
    EXPECT_EQ(DetectText("<1invalid/>"), FileType::Text);    // bad name start
}

// ---- The 500 KB cap: the UTF-8 validation scans at most 500000 bytes, so
// invalid bytes past the cap do not make the file Binary.

TEST(GuessFileTypeTest, Utf8ValidationStopsAt500KB)
{
    std::string text(500000, 'a');
    text += "\xFF\xFF";  // invalid past the cap
    EXPECT_EQ(Detect(text), FileType::Text);

    // The invalid byte AT the cap position is still scanned.
    std::string atCap(499999, 'a');
    atCap += "\xFF";
    EXPECT_EQ(Detect(atCap), FileType::Binary);
}
