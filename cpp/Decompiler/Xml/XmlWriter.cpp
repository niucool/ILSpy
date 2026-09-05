// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The XmlWriter implementation (see XmlWriter.hpp for the flattened-class
// design). Every method mirrors its decompiled .NET 10 counterpart; the
// escaping blocks reproduce the pointer loops as index loops with the same
// flush points.

#include "XmlWriter.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include "../Util/Char.hpp"
#include "../Util/Utf.hpp"
#include "XmlConvert.hpp"

namespace ILSpy::Decompiler::Xml {

namespace {

// --- the XmlCharType classification bits (probed from the .NET 10 runtime's
// charProperties table; see the XmlWriteProbe extraction script) ---

struct CharTypeRange {
    std::uint16_t first;
    std::uint16_t last;
};

bool ContainsUnit(const CharTypeRange* ranges, std::size_t count, std::uint32_t unit)
{
    if (unit > 0xFFFF)
        return false;
    const CharTypeRange* it = std::lower_bound(
        ranges, ranges + count, unit,
        [](const CharTypeRange& r, std::uint32_t value) { return r.last < value; });
    if (it == ranges + count || it->first > unit)
        return false;
    return true;
}

// Whitespace (bit 0x01): 3 ranges over 65536 units
inline constexpr CharTypeRange kWhitespaceRanges[] = {
    {0x0009, 0x000A}, {0x000D, 0x000D}, {0x0020, 0x0020},
};

// CharData (bit 0x10): 4 ranges over 65536 units
inline constexpr CharTypeRange kCharDataRanges[] = {
    {0x0009, 0x000A}, {0x000D, 0x000D}, {0x0020, 0xD7FF}, {0xE000, 0xFFFD},
};

// TextChar (bit 0x40): 5 ranges over 65536 units
inline constexpr CharTypeRange kTextCharRanges[] = {
    {0x0020, 0x0025}, {0x0027, 0x003B}, {0x003D, 0x005C}, {0x005E, 0xD7FF}, {0xE000, 0xFFFD},
};

// AttributeValueChar (bit 0x80): 6 ranges over 65536 units
inline constexpr CharTypeRange kAttributeValueRanges[] = {
    {0x0020, 0x0021}, {0x0023, 0x0025}, {0x0028, 0x003B}, {0x003D, 0x003D},
    {0x003F, 0xD7FF}, {0xE000, 0xFFFD},
};

bool IsXmlWhitespaceUnit(std::uint32_t unit)
{
    return ContainsUnit(kWhitespaceRanges, std::size(kWhitespaceRanges), unit);
}

bool IsCharDataUnit(std::uint32_t unit)
{
    return ContainsUnit(kCharDataRanges, std::size(kCharDataRanges), unit);
}

bool IsTextCharUnit(std::uint32_t unit)
{
    return ContainsUnit(kTextCharRanges, std::size(kTextCharRanges), unit);
}

bool IsAttributeValueCharUnit(std::uint32_t unit)
{
    return ContainsUnit(kAttributeValueRanges, std::size(kAttributeValueRanges), unit);
}

bool IsSurrogateUnit(std::uint32_t unit)
{
    return unit >= 0xD800 && unit <= 0xDFFF;
}

// XmlCharType.IsOnlyWhitespace.
bool IsOnlyWhitespace(std::u16string_view s)
{
    for (char16_t c : s)
        if (!IsXmlWhitespaceUnit(c))
            return false;
    return true;
}

// The s_publicIdChars literal (SearchValues.Create over the exact set).
bool IsPublicIdChar(std::uint32_t unit)
{
    switch (unit) {
    case '\n': case '\r': case ' ': case '!': case '#': case '$': case '%':
    case '\'': case '(': case ')': case '*': case '+': case ',': case '-':
    case '.': case '/': case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9': case ':': case ';':
    case '=': case '?': case '@': case 'A': case 'B': case 'C': case 'D':
    case 'E': case 'F': case 'G': case 'H': case 'I': case 'J': case 'K':
    case 'L': case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
    case 'S': case 'T': case 'U': case 'V': case 'W': case 'X': case 'Y':
    case 'Z': case '_': case 'a': case 'b': case 'c': case 'd': case 'e':
    case 'f': case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
    case 'm': case 'n': case 'o': case 'p': case 'q': case 'r': case 's':
    case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        return true;
    default:
        return false;
    }
}

// XmlCharType.IsOnlyCharData: the ASCII s_asciiCharDataChars fast set plus
// the IsCharData classification for the rest (accepting surrogate pairs).
bool IsOnlyCharData(std::u16string_view s)
{
    for (std::size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        bool asciiOk = (c >= 0x20 && c <= 0x7E) || c == 0x09 || c == 0x0A
            || c == 0x0D || c == 0x7F;
        if (asciiOk)
            continue;
        if (IsCharDataUnit(c))
            continue;
        // A surrogate pair of valid halves counts as char data.
        if (i + 1 < s.size() && c >= 0xD800 && c <= 0xDBFF
            && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

// The "0x{...:X2}" hex render of XmlException.BuildCharExceptionArgs.
std::string FormatHexX2(std::int32_t value)
{
    std::string out = "0x";
    char buf[8];
    // int32 prints at least two hex digits, in full 32-bit width for
    // negative values (the CombineSurrogateChar garbage values).
    std::uint32_t u = static_cast<std::uint32_t>(value);
    int digits = value < 0 ? 8 : 1;
    std::uint32_t v = u;
    while (v > 0xF) {
        v >>= 4;
        ++digits;
    }
    if (value < 0)
        digits = 8;
    for (int k = digits - 1; k >= 0; --k) {
        buf[k] = "0123456789ABCDEF"[(u >> (k * 4)) & 0xF];
    }
    out.append(buf, static_cast<std::size_t>(digits));
    return out;
}

// ArgumentException.ThrowIfNullOrEmpty (the empty-string arm; the port's
// std::string parameters are never null).
std::string EmptyStringMessage(const char* paramName)
{
    return std::string("The value cannot be an empty string. (Parameter '") + paramName + "')";
}

// SR.Xml_WrongToken.
std::string WrongTokenMessage(const char* token, const char* state)
{
    std::string out = std::string("Token ") + token + " in state " + state
        + " would result in an invalid XML document.";
    return out;
}

} // namespace

// --- the state tables (transcribed from the decompiled s_stateTableDocument /
// s_stateTableAuto arrays: 15 tokens x 16 states, indexed [token * 16 + state]) ---

const XmlWriter::State* XmlWriter::StateTableDocument() {
    static const State kTable[240] = {
        State::Document, State::Error, State::Error, State::Error, State::Error, State::PostB64Cont, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::PostB64Cont, State::Error, State::EndDocument, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::StartDoc, State::TopLevel, State::Document, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::AfterRootEle,
        State::EndAttrSCont, State::EndAttrSCont, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartDoc, State::TopLevel,
        State::Document, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::AfterRootEle, State::EndAttrSCont, State::EndAttrSCont, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::StartDoc, State::TopLevel, State::Document, State::Error, State::Error, State::PostB64Cont,
        State::PostB64Attr, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::StartDocEle, State::Element, State::Element, State::StartContentEle, State::Element, State::PostB64Cont, State::PostB64Attr, State::Error, State::EndAttrSEle, State::EndAttrSEle,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartContent,
        State::Content, State::PostB64Cont, State::PostB64Attr, State::Error, State::EndAttrEEle, State::EndAttrEEle, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Attribute, State::Error, State::PostB64Cont, State::PostB64Attr, State::Error,
        State::EndAttrSAttr, State::EndAttrSAttr, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::PostB64Cont, State::PostB64Attr, State::Error, State::Element, State::Element, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartContent, State::Content, State::PostB64Cont,
        State::PostB64Attr, State::Error, State::Attribute, State::SpecialAttr, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::Error, State::EndAttrSCont, State::EndAttrSCont,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartContent,
        State::Content, State::PostB64Cont, State::PostB64Attr, State::Error, State::Attribute, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::StartContentB64, State::B64Content, State::B64Content, State::B64Attribute, State::Error,
        State::B64Attribute, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartDoc, State::Error,
        State::Document, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::AfterRootEle, State::Attribute, State::SpecialAttr, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::StartDoc, State::TopLevel, State::Document, State::StartContent, State::Content, State::PostB64Cont,
        State::PostB64Attr, State::AfterRootEle, State::Attribute, State::SpecialAttr, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
    };
    return kTable;
}

const XmlWriter::State* XmlWriter::StateTableAuto() {
    static const State kTable[240] = {
        State::Document, State::Error, State::Error, State::Error, State::Error, State::PostB64Cont, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::PostB64Cont, State::Error, State::EndDocument, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::TopLevel, State::TopLevel, State::Error, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::AfterRootEle,
        State::EndAttrSCont, State::EndAttrSCont, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::TopLevel, State::TopLevel,
        State::Error, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::AfterRootEle, State::EndAttrSCont, State::EndAttrSCont, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::Error, State::StartDoc, State::TopLevel, State::Error, State::Error, State::Error, State::PostB64Cont,
        State::PostB64Attr, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error,
        State::StartFragEle, State::Element, State::Error, State::StartContentEle, State::Element, State::PostB64Cont, State::PostB64Attr, State::Element, State::EndAttrSEle, State::EndAttrSEle,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartContent,
        State::Content, State::PostB64Cont, State::PostB64Attr, State::Error, State::EndAttrEEle, State::EndAttrEEle, State::Error, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::RootLevelAttr, State::Error, State::Error, State::Attribute, State::Error, State::PostB64Cont, State::PostB64Attr, State::Error,
        State::EndAttrSAttr, State::EndAttrSAttr, State::Error, State::StartRootLevelAttr, State::StartRootLevelAttr, State::PostB64RootAttr, State::RootLevelAttr, State::Error, State::Error, State::Error,
        State::Error, State::Error, State::Error, State::PostB64Cont, State::PostB64Attr, State::Error, State::Element, State::Element, State::Error, State::AfterRootLevelAttr,
        State::AfterRootLevelAttr, State::PostB64RootAttr, State::Error, State::Error, State::StartFragCont, State::StartFragCont, State::Error, State::StartContent, State::Content, State::PostB64Cont,
        State::PostB64Attr, State::Content, State::Attribute, State::SpecialAttr, State::Error, State::RootLevelAttr, State::RootLevelSpecAttr, State::PostB64RootAttr, State::Error, State::Error,
        State::StartFragCont, State::StartFragCont, State::Error, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::Content, State::EndAttrSCont, State::EndAttrSCont,
        State::Error, State::Error, State::Error, State::Error, State::Error, State::Error, State::StartFragCont, State::StartFragCont, State::Error, State::StartContent,
        State::Content, State::PostB64Cont, State::PostB64Attr, State::Content, State::Attribute, State::Error, State::Error, State::RootLevelAttr, State::Error, State::PostB64RootAttr,
        State::Error, State::Error, State::StartFragB64, State::StartFragB64, State::Error, State::StartContentB64, State::B64Content, State::B64Content, State::B64Attribute, State::B64Content,
        State::B64Attribute, State::Error, State::Error, State::RootLevelB64Attr, State::Error, State::RootLevelB64Attr, State::Error, State::Error, State::StartFragCont, State::TopLevel,
        State::Error, State::StartContent, State::Content, State::PostB64Cont, State::PostB64Attr, State::Content, State::Attribute, State::SpecialAttr, State::Error, State::RootLevelAttr,
        State::RootLevelSpecAttr, State::PostB64RootAttr, State::AfterRootLevelAttr, State::Error, State::TopLevel, State::TopLevel, State::Error, State::StartContent, State::Content, State::PostB64Cont,
        State::PostB64Attr, State::AfterRootEle, State::Attribute, State::SpecialAttr, State::Error, State::RootLevelAttr, State::RootLevelSpecAttr, State::PostB64RootAttr, State::AfterRootLevelAttr, State::Error,
    };
    return kTable;
}

const char* XmlWriter::StateName(State state)
{
    // stateName (the 17 real states; the pseudo-states never reach a message).
    static const char* kNames[] = {
        "Start", "TopLevel", "Document", "Element Start Tag", "Element Content", "Element Content",
        "Attribute", "EndRootElement", "Attribute", "Special Attribute", "End Document",
        "Root Level Attribute Value", "Root Level Special Attribute Value", "Root Level Base64 Attribute Value",
        "After Root Level Attribute", "Closed", "Error",
    };
    if (static_cast<int>(state) >= 17)
        return "Error";
    return kNames[static_cast<int>(state)];
}

const char* XmlWriter::TokenName(Token token)
{
    static const char* kNames[] = {
        "StartDocument", "EndDocument", "PI", "Comment", "DTD", "StartElement", "EndElement",
        "StartAttribute", "EndAttribute", "Text", "CDATA", "Atomic value", "Base64", "RawData",
        "Whitespace",
    };
    return kNames[static_cast<int>(token)];
}

XmlWriter::XmlWriter(XmlWriterSettings settings, XmlWriterSink sink)
    : settings_(std::move(settings))
    , sinkKind_(sink)
{
    // The Text sink carries the TextWriter's own encoding ("utf-16" for the
    // StringWriter analog); the File sink keeps the settings' encoding.
    encodingWebName_ = sinkKind_ == XmlWriterSink::Text ? std::string("utf-16") : settings_.Encoding;
    checkCharacters_ = settings_.CheckCharacters;
    omitDuplNamespaces_ = (static_cast<std::uint32_t>(settings_.NamespaceHandling)
        & static_cast<std::uint32_t>(NamespaceHandling::OmitDuplicates)) != 0;
    writeEndDocumentOnClose_ = settings_.WriteEndDocumentOnClose;
    conformanceLevel_ = settings_.ConformanceLevel;
    stateTable_ = conformanceLevel_ == ConformanceLevel::Document ? StateTableDocument() : StateTableAuto();
    nsStack_.resize(8);
    nsStack_[0] = {"xmlns", "http://www.w3.org/2000/xmlns/", NamespaceKind::Special};
    nsStack_[1] = {"xml", "http://www.w3.org/XML/1998/namespace", NamespaceKind::Special};
    nsStack_[2] = {"", "", NamespaceKind::Implied};
    nsTop_ = 2;
    elemScopeStack_.resize(8);
    elemScopeStack_[0] = {"", "", "", nsTop_};
    elemTop_ = 0;
    attrStack_.resize(8);
    // The raw writer buffer (kBufLen units + 32 of headroom; index 0 is the
    // FlushBuffer carry slot).
    bufChars_.assign(kBufLen + 32, u'\0');
    bufPos_ = 1;
    textPos_ = 1;
    if (settings_.Indent) {
        indentLevel_ = 0;
        mixedContent_ = false;
    }
}

// --- the raw-writer buffer machinery ---

void XmlWriter::FlushBuffer()
{
    if (writeToNull_)
        return;
    if (bufPos_ - 1 > 0)
        sink_.append(bufChars_.data() + 1, bufPos_ - 1);
    bufChars_[0] = bufChars_[bufPos_ - 1];
    textPos_ = (textPos_ == bufPos_) ? 1 : 0;
    attrEndPos_ = (attrEndPos_ == bufPos_) ? 1 : 0;
    contentPos_ = 0;
    cdataPos_ = 0;
    bufPos_ = 1;
}

void XmlWriter::EnsureBuffer(std::size_t extra)
{
    // The C# buffer is a fixed 6144+32 array; the port grows it only to
    // guarantee indexing safety (the flush logic keeps bufPos_ <= kBufLen in
    // practice; growing is a no-op divergence guard for oversized single
    // writes that the blocks' flush loops handle).
    if (bufPos_ + extra + 32 > bufChars_.size())
        bufChars_.resize(bufPos_ + extra + 32, u'\0');
}

void XmlWriter::InvalidXmlChar(char16_t ch, bool entitize)
{
    if (checkCharacters_) {
        // XmlConvert.CreateInvalidCharException(ch, '\0') -- the port maps the
        // XmlException to the Xml type.
        std::string display = InvalidCharDisplay(ch, 0);
        throw XmlException("'" + display + "', hexadecimal value " + FormatHexX2(InvalidCharValue(ch, 0))
            + ", is an invalid character.");
    }
    if (entitize) {
        // CharEntity: &#x{X}; of the unit value.
        std::string entity = "&#x";
        std::uint32_t u = ch;
        char buf[4];
        for (int k = 3; k >= 0; --k) {
            buf[k] = "0123456789ABCDEF"[(u >> (k * 4)) & 0xF];
        }
        entity.append(buf, 4);
        entity += ";";
        RawText(Util::Utf8ToUtf16(entity));
        return;
    }
    bufChars_[bufPos_++] = ch;
}

void XmlWriter::RawText(std::u16string_view s)
{
    // The pointer RawText(char*, char*) loop: fast-copy units below 0xD800,
    // flush at the buffer bound, validate surrogates and reject 0xFFFE/0xFFFF.
    std::size_t src = 0;
    while (true) {
        std::size_t room = kBufLen - bufPos_;
        std::size_t take = std::min(room, s.size() - src);
        std::size_t copied = 0;
        while (copied < take) {
            char16_t c = s[src + copied];
            if (static_cast<std::uint32_t>(c) >= 55296)
                break;
            bufChars_[bufPos_ + copied] = c;
            ++copied;
        }
        bufPos_ += copied;
        src += copied;
        if (src >= s.size())
            break;
        if (bufPos_ >= kBufLen) {
            FlushBuffer();
            continue;
        }
        char16_t c = s[src++];
        if (IsSurrogateUnit(c)) {
            // EncodeSurrogate: a high surrogate must be followed by a low one.
            if (static_cast<std::uint32_t>(c) <= 56319) {
                if (src < s.size()) {
                    char16_t next = s[src];
                    if (static_cast<std::uint32_t>(next) >= 56320 && static_cast<std::uint32_t>(next) <= 57343) {
                        bufChars_[bufPos_++] = c;
                        bufChars_[bufPos_++] = next;
                        ++src;
                        continue;
                    }
                    throw std::invalid_argument("The surrogate pair is invalid. Missing a low surrogate character.");
                }
                throw std::invalid_argument("The surrogate pair is invalid. Missing a low surrogate character.");
            }
            throw std::invalid_argument("'\\uD800' - '\\\\uDBFF' is an unexpected high surrogate character.");
        }
        if (static_cast<std::uint32_t>(c) <= 127 || static_cast<std::uint32_t>(c) >= 65534) {
            InvalidXmlChar(c, false);
            continue;
        }
        bufChars_[bufPos_++] = c;
    }
}

void XmlWriter::WriteAttributeTextBlock(std::u16string_view s)
{
    std::size_t src = 0;
    while (true) {
        std::size_t room = kBufLen - bufPos_;
        std::size_t take = std::min(room, s.size() - src);
        std::size_t copied = 0;
        while (copied < take) {
            char16_t c = s[src + copied];
            if (!IsAttributeValueCharUnit(c)) {
                break;
            }
            bufChars_[bufPos_ + copied] = c;
            ++copied;
        }
        bufPos_ += copied;
        src += copied;
        if (src >= s.size())
            break;
        if (bufPos_ >= kBufLen) {
            FlushBuffer();
            continue;
        }
        char16_t c = s[src++];
        switch (c) {
        case u'&':
            RawText(u"&amp;");
            break;
        case u'<':
            RawText(u"&lt;");
            break;
        case u'>':
            RawText(u"&gt;");
            break;
        case u'"':
            RawText(u"&quot;");
            break;
        case u'\'':
            bufChars_[bufPos_++] = c;
            break;
        case u'\t':
            if (settings_.NewLineHandling == NewLineHandling::None)
                bufChars_[bufPos_++] = c;
            else
                RawText(u"&#x9;");
            break;
        case u'\r':
            if (settings_.NewLineHandling == NewLineHandling::None)
                bufChars_[bufPos_++] = c;
            else
                RawText(u"&#xD;");
            break;
        case u'\n':
            if (settings_.NewLineHandling == NewLineHandling::None)
                bufChars_[bufPos_++] = c;
            else
                RawText(u"&#xA;");
            break;
        default:
            if (IsSurrogateUnit(c)) {
                if (static_cast<std::uint32_t>(c) <= 56319) {
                    if (src < s.size()) {
                        char16_t next = s[src];
                        if (static_cast<std::uint32_t>(next) >= 56320 && static_cast<std::uint32_t>(next) <= 57343) {
                            bufChars_[bufPos_++] = c;
                            bufChars_[bufPos_++] = next;
                            ++src;
                            break;
                        }
                    }
                    throw std::invalid_argument("The surrogate pair is invalid. Missing a low surrogate character.");
                }
                throw std::invalid_argument("'\\uD800' - '\\\\uDBFF' is an unexpected high surrogate character.");
            }
            if (static_cast<std::uint32_t>(c) <= 127 || static_cast<std::uint32_t>(c) >= 65534) {
                InvalidXmlChar(c, true);
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        }
    }
}

void XmlWriter::WriteElementTextBlock(std::u16string_view s)
{
    std::size_t src = 0;
    while (true) {
        std::size_t room = kBufLen - bufPos_;
        std::size_t take = std::min(room, s.size() - src);
        std::size_t copied = 0;
        while (copied < take) {
            char16_t c = s[src + copied];
            if (!IsAttributeValueCharUnit(c)) {
                break;
            }
            bufChars_[bufPos_ + copied] = c;
            ++copied;
        }
        bufPos_ += copied;
        src += copied;
        if (src >= s.size())
            break;
        if (bufPos_ >= kBufLen) {
            FlushBuffer();
            continue;
        }
        char16_t c = s[src++];
        switch (c) {
        case u'&':
            RawText(u"&amp;");
            break;
        case u'<':
            RawText(u"&lt;");
            break;
        case u'>':
            RawText(u"&gt;");
            break;
        case u'\t':
        case u'"':
        case u'\'':
            bufChars_[bufPos_++] = c;
            break;
        case u'\n':
            if (settings_.NewLineHandling == NewLineHandling::Replace) {
                RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        case u'\r':
            switch (settings_.NewLineHandling) {
            case NewLineHandling::Replace:
                if (src < s.size() && s[src] == u'\n')
                    ++src;
                RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
                break;
            case NewLineHandling::Entitize:
                RawText(u"&#xD;");
                break;
            case NewLineHandling::None:
                bufChars_[bufPos_++] = c;
                break;
            }
            break;
        default:
            if (IsSurrogateUnit(c)) {
                if (static_cast<std::uint32_t>(c) <= 56319) {
                    if (src < s.size()) {
                        char16_t next = s[src];
                        if (static_cast<std::uint32_t>(next) >= 56320 && static_cast<std::uint32_t>(next) <= 57343) {
                            bufChars_[bufPos_++] = c;
                            bufChars_[bufPos_++] = next;
                            ++src;
                            break;
                        }
                    }
                    throw std::invalid_argument("The surrogate pair is invalid. Missing a low surrogate character.");
                }
                throw std::invalid_argument("'\\uD800' - '\\\\uDBFF' is an unexpected high surrogate character.");
            }
            if (static_cast<std::uint32_t>(c) <= 127 || static_cast<std::uint32_t>(c) >= 65534) {
                InvalidXmlChar(c, true);
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        }
    }
    textPos_ = bufPos_;
    contentPos_ = 0;
}

void XmlWriter::WriteCommentOrPi(std::u16string_view s, char16_t stopChar)
{
    if (s.empty()) {
        if (bufPos_ >= kBufLen)
            FlushBuffer();
        return;
    }
    std::size_t src = 0;
    while (true) {
        std::size_t room = kBufLen - bufPos_;
        std::size_t take = std::min(room, s.size() - src);
        std::size_t copied = 0;
        while (copied < take) {
            char16_t c = s[src + copied];
            if (!IsTextCharUnit(c) || c == stopChar) {
                break;
            }
            bufChars_[bufPos_ + copied] = c;
            ++copied;
        }
        bufPos_ += copied;
        src += copied;
        if (src >= s.size())
            break;
        if (bufPos_ >= kBufLen) {
            FlushBuffer();
            continue;
        }
        char16_t c = s[src++];
        switch (c) {
        case u'-':
            bufChars_[bufPos_++] = u'-';
            if (c == stopChar && (src == s.size() || s[src] == u'-')) {
                // A "--" pair (or a trailing '-') inside a comment gets a
                // space inserted after the first '-'.
                bufChars_[bufPos_++] = u' ';
            }
            break;
        case u'?':
            bufChars_[bufPos_++] = u'?';
            if (c == stopChar && src < s.size() && s[src] == u'>') {
                bufChars_[bufPos_++] = u' ';
            }
            break;
        case u']':
            bufChars_[bufPos_++] = u']';
            break;
        case u'\r':
            if (settings_.NewLineHandling == NewLineHandling::Replace) {
                if (src < s.size() && s[src] == u'\n')
                    ++src;
                RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
            } else {
                bufChars_[bufPos_++] = c;
            }
            break;
        case u'\n':
            if (settings_.NewLineHandling == NewLineHandling::Replace) {
                RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        case u'\t':
        case u'&':
        case u'<':
            bufChars_[bufPos_++] = c;
            break;
        default:
            if (IsSurrogateUnit(c)) {
                if (static_cast<std::uint32_t>(c) <= 56319) {
                    if (src < s.size()) {
                        char16_t next = s[src];
                        if (static_cast<std::uint32_t>(next) >= 56320 && static_cast<std::uint32_t>(next) <= 57343) {
                            bufChars_[bufPos_++] = c;
                            bufChars_[bufPos_++] = next;
                            ++src;
                            break;
                        }
                    }
                    throw std::invalid_argument("The surrogate pair is invalid. Missing a low surrogate character.");
                }
                throw std::invalid_argument("'\\uD800' - '\\\\uDBFF' is an unexpected high surrogate character.");
            }
            if (static_cast<std::uint32_t>(c) <= 127 || static_cast<std::uint32_t>(c) >= 65534) {
                InvalidXmlChar(c, false);
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        }
    }
}

void XmlWriter::WriteCDataSection(std::u16string_view s)
{
    if (s.empty()) {
        if (bufPos_ >= kBufLen)
            FlushBuffer();
        return;
    }
    std::size_t src = 0;
    while (true) {
        std::size_t room = kBufLen - bufPos_;
        std::size_t take = std::min(room, s.size() - src);
        std::size_t copied = 0;
        while (copied < take) {
            char16_t c = s[src + copied];
            if (!IsAttributeValueCharUnit(c) || c == u']') {
                break;
            }
            bufChars_[bufPos_ + copied] = c;
            ++copied;
        }
        bufPos_ += copied;
        src += copied;
        if (src >= s.size())
            break;
        if (bufPos_ >= kBufLen) {
            FlushBuffer();
            continue;
        }
        char16_t c = s[src++];
        switch (c) {
        case u'>':
            // "]]>" splits into "]]]]><![CDATA[>".
            if (hadDoubleBracket_ && bufPos_ > 0 && bufChars_[bufPos_ - 1] == u']') {
                RawText(u"]]>");
                RawText(u"<![CDATA[");
            }
            bufChars_[bufPos_++] = u'>';
            break;
        case u']':
            if (bufPos_ > 0 && bufChars_[bufPos_ - 1] == u']')
                hadDoubleBracket_ = true;
            else
                hadDoubleBracket_ = false;
            bufChars_[bufPos_++] = u']';
            break;
        case u'\r':
            if (settings_.NewLineHandling == NewLineHandling::Replace) {
                if (src < s.size() && s[src] == u'\n')
                    ++src;
                RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
            } else {
                bufChars_[bufPos_++] = c;
            }
            break;
        case u'\n':
            if (settings_.NewLineHandling == NewLineHandling::Replace) {
                RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        case u'\t':
        case u'"':
        case u'&':
        case u'\'':
        case u'<':
            bufChars_[bufPos_++] = c;
            break;
        default:
            if (IsSurrogateUnit(c)) {
                if (static_cast<std::uint32_t>(c) <= 56319) {
                    if (src < s.size()) {
                        char16_t next = s[src];
                        if (static_cast<std::uint32_t>(next) >= 56320 && static_cast<std::uint32_t>(next) <= 57343) {
                            bufChars_[bufPos_++] = c;
                            bufChars_[bufPos_++] = next;
                            ++src;
                            break;
                        }
                    }
                    throw std::invalid_argument("The surrogate pair is invalid. Missing a low surrogate character.");
                }
                throw std::invalid_argument("'\\uD800' - '\\\\uDBFF' is an unexpected high surrogate character.");
            }
            if (static_cast<std::uint32_t>(c) <= 127 || static_cast<std::uint32_t>(c) >= 65534) {
                InvalidXmlChar(c, false);
                break;
            }
            bufChars_[bufPos_++] = c;
            break;
        }
    }
}

void XmlWriter::WriteIndent()
{
    RawText(Util::Utf8ToUtf16(settings_.NewLineChars));
    for (int i = indentLevel_; i > 0; --i)
        RawText(Util::Utf8ToUtf16(settings_.IndentChars));
}

// --- the well-formed state machine ---

void XmlWriter::AdvanceState(Token token)
{
    if (static_cast<int>(currentState_) >= static_cast<int>(State::Closed)) {
        if (currentState_ == State::Closed || currentState_ == State::Error)
            throw std::runtime_error("The Writer is closed or in error state.");
        throw std::runtime_error(WrongTokenMessage(TokenName(token), StateName(currentState_)));
    }
    State state;
    while (true) {
        state = stateTable_[static_cast<int>(token) * 16 + static_cast<int>(currentState_)];
        switch (state) {
        case State::Error: {
            // ThrowInvalidStateTransition.
            std::string text = WrongTokenMessage(TokenName(token), StateName(currentState_));
            if ((currentState_ == State::Start || currentState_ == State::AfterRootEle)
                && conformanceLevel_ == ConformanceLevel::Document) {
                text += " Make sure that the ConformanceLevel setting is set to ConformanceLevel.Fragment or ConformanceLevel.Auto if you want to write an XML fragment. ";
                throw std::runtime_error(text);
            }
            throw std::runtime_error(text);
        }
        case State::StartContent:
        case State::StartContentB64:
            StartElementContent();
            state = State::Content;
            break;
        case State::StartContentEle:
            StartElementContent();
            state = State::Element;
            break;
        case State::StartDoc:
            WriteStartDocument();
            state = State::Document;
            break;
        case State::StartDocEle:
            WriteStartDocument();
            state = State::Element;
            break;
        case State::EndAttrSEle:
            WriteEndAttribute();
            StartElementContent();
            state = State::Element;
            break;
        case State::EndAttrEEle:
            WriteEndAttribute();
            StartElementContent();
            state = State::Content;
            break;
        case State::EndAttrSCont:
            WriteEndAttribute();
            StartElementContent();
            state = State::Content;
            break;
        case State::EndAttrSAttr:
            WriteEndAttribute();
            state = State::Attribute;
            break;
        case State::PostB64Cont:
        case State::PostB64Attr:
        case State::PostB64RootAttr:
            currentState_ = state;
            continue;
        case State::StartFragEle:
        case State::StartFragCont:
        case State::StartFragB64:
            StartFragment();
            state = static_cast<State>(static_cast<int>(state) - static_cast<int>(State::StartFragEle)
                + static_cast<int>(State::Element));
            break;
        case State::StartRootLevelAttr:
            WriteEndAttribute();
            state = State::RootLevelAttr;
            break;
        default:
            break;
        }
        break;
    }
    currentState_ = state;
}

void XmlWriter::StartFragment()
{
    conformanceLevel_ = ConformanceLevel::Fragment;
}

void XmlWriter::StartElementContent()
{
    // The well-formed writer: flush the NeedToWrite namespace declarations
    // (newest first), then the indent/raw writers' content start ('>').
    std::size_t prevNSTop = elemScopeStack_[elemTop_].prevNSTop;
    for (std::size_t i = nsTop_; i > prevNSTop; --i) {
        if (nsStack_[i].kind == NamespaceKind::NeedToWrite) {
            // Namespace.WriteDecl over the raw writer (WriteNamespaceDeclaration
            // = start-decl + raw WriteString + end-decl; the internal WriteString
            // dispatches through the indent writer, so it marks mixed content
            // -- the StartElementContent reset below cleans it).
            RawWriteNamespaceDeclaration(nsStack_[i].prefix, nsStack_[i].namespaceUri);
            nsStack_[i].kind = NamespaceKind::Written;
        }
    }
    // The indent writer's StartElementContent.
    if (settings_.Indent) {
        if (indentLevel_ == 1 && rootConformanceLevel_ == ConformanceLevel::Document)
            mixedContent_ = false;
        else
            mixedContent_ = mixedContentStack_.empty() ? false : mixedContentStack_.back();
    }
    // The raw writer's StartElementContent.
    bufChars_[bufPos_++] = u'>';
    contentPos_ = bufPos_;
}

// --- the namespace stack ---

std::ptrdiff_t XmlWriter::LookupNamespaceIndex(const std::string& prefix) const
{
    for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(nsTop_); i >= 0; --i) {
        if (nsStack_[i].prefix == prefix)
            return i;
    }
    return -1;
}

std::string XmlWriter::LookupNamespace(const std::string& prefix) const
{
    for (std::size_t i = nsTop_ + 1; i-- > 0;) {
        if (nsStack_[i].prefix == prefix)
            return nsStack_[i].namespaceUri;
    }
    return {};
}

std::string XmlWriter::LookupPrefix(const std::string& ns) const
{
    for (std::size_t i = nsTop_ + 1; i-- > 0;) {
        if (nsStack_[i].namespaceUri == ns) {
            const std::string& prefix = nsStack_[i].prefix;
            // A later re-binding of the same prefix shadows this one.
            for (std::size_t j = i + 1; j <= nsTop_; ++j) {
                if (nsStack_[j].prefix == prefix)
                    return {};
            }
            return prefix;
        }
    }
    return {};
}

std::string XmlWriter::LookupLocalNamespace(const std::string& prefix) const
{
    for (std::size_t i = nsTop_ + 1; i-- > elemScopeStack_[elemTop_].prevNSTop + 1;) {
        if (nsStack_[i].prefix == prefix)
            return nsStack_[i].namespaceUri;
    }
    return {};
}

std::string XmlWriter::GeneratePrefix()
{
    std::string text = "p" + std::to_string(nsTop_ - 2);
    if (LookupNamespace(text).empty())
        return text;
    int n = 0;
    std::string text2;
    do {
        text2 = text + std::to_string(n);
        ++n;
    } while (!LookupNamespace(text2).empty());
    return text2;
}

void XmlWriter::AddNamespace(std::string prefix, std::string ns, NamespaceKind kind)
{
    std::size_t i = ++nsTop_;
    if (i == nsStack_.size())
        nsStack_.resize(i * 2);
    nsStack_[i] = {std::move(prefix), std::move(ns), kind};
}

void XmlWriter::PushNamespaceImplicit(const std::string& prefix, const std::string& ns)
{
    std::ptrdiff_t index = LookupNamespaceIndex(prefix);
    NamespaceKind kind;
    if (index != -1) {
        std::size_t num = static_cast<std::size_t>(index);
        if (num > elemScopeStack_[elemTop_].prevNSTop) {
            if (nsStack_[num].namespaceUri != ns) {
                throw XmlException("The prefix '" + prefix + "' cannot be redefined from '"
                    + nsStack_[num].namespaceUri + "' to '" + ns + "' within the same start element tag.");
            }
            return;
        }
        if (nsStack_[num].kind == NamespaceKind::Special) {
            if (prefix != "xml")
                throw std::invalid_argument("Prefix \"xmlns\" is reserved for use by XML.");
            if (ns != nsStack_[num].namespaceUri)
                throw std::invalid_argument("Prefix \"xml\" is reserved for use by XML and can be mapped only to namespace name \"http://www.w3.org/XML/1998/namespace\".");
            kind = NamespaceKind::Implied;
        } else {
            kind = nsStack_[num].namespaceUri == ns ? NamespaceKind::Implied : NamespaceKind::NeedToWrite;
        }
    } else {
        if ((ns == "http://www.w3.org/XML/1998/namespace" && prefix != "xml")
            || (ns == "http://www.w3.org/2000/xmlns/" && prefix != "xmlns")) {
            throw std::invalid_argument("Prefix '" + prefix + "' cannot be mapped to namespace name reserved for \"xml\" or \"xmlns\".");
        }
        kind = NamespaceKind::NeedToWrite;
    }
    AddNamespace(prefix, ns, kind);
}

bool XmlWriter::PushNamespaceExplicit(const std::string& prefix, const std::string& ns)
{
    bool result = true;
    std::ptrdiff_t index = LookupNamespaceIndex(prefix);
    if (index != -1) {
        std::size_t num = static_cast<std::size_t>(index);
        if (num > elemScopeStack_[elemTop_].prevNSTop) {
            if (nsStack_[num].namespaceUri != ns) {
                throw XmlException("The prefix '" + prefix + "' cannot be redefined from '"
                    + nsStack_[num].namespaceUri + "' to '" + ns + "' within the same start element tag.");
            }
            NamespaceKind kind = nsStack_[num].kind;
            if (kind == NamespaceKind::Written) {
                std::string arg = prefix.empty() ? std::string("xmlns")
                    : "xmlns:" + prefix;
                throw XmlException("'" + arg + "' is a duplicate attribute name.");
            }
            if (omitDuplNamespaces_ && kind != NamespaceKind::NeedToWrite)
                result = false;
            nsStack_[num].kind = NamespaceKind::Written;
            return result;
        }
        if (nsStack_[num].namespaceUri == ns && omitDuplNamespaces_)
            result = false;
    }
    if ((ns == "http://www.w3.org/XML/1998/namespace" && prefix != "xml")
        || (ns == "http://www.w3.org/2000/xmlns/" && prefix != "xmlns")) {
        throw std::invalid_argument("Prefix '" + prefix + "' cannot be mapped to namespace name reserved for \"xml\" or \"xmlns\".");
    }
    if (!prefix.empty() && prefix[0] == 'x') {
        if (prefix == "xml") {
            if (ns != "http://www.w3.org/XML/1998/namespace")
                throw std::invalid_argument("Prefix \"xml\" is reserved for use by XML and can be mapped only to namespace name \"http://www.w3.org/XML/1998/namespace\".");
        } else if (prefix == "xmlns") {
            throw std::invalid_argument("Prefix \"xmlns\" is reserved for use by XML.");
        }
    }
    AddNamespace(prefix, ns, NamespaceKind::Written);
    return result;
}

void XmlWriter::CheckNCName(const std::string& ncname)
{
    std::u16string units = Util::Utf8ToUtf16(ncname);
    if (units.empty())
        throw std::invalid_argument(EmptyStringMessage("ncname"));
    // CheckNCName walks the units; InvalidCharsException formats the bad
    // unit through XmlException.BuildCharExceptionArgs.
    for (std::size_t i = 0; i < units.size(); ++i) {
        bool ok = i == 0 ? IsStartNameChar(units[i]) : IsNameChar(units[i]);
        if (!ok) {
            std::uint32_t invChar = units[i];
            std::uint32_t nextChar = 0;
            if (i + 1 < units.size())
                nextChar = units[i + 1];
            throw std::invalid_argument("Invalid name character in '" + ncname + "'. The '"
                + InvalidCharDisplay(invChar, nextChar) + "' character, hexadecimal value "
                + FormatHexX2(InvalidCharValue(invChar, nextChar))
                + ", cannot be included in a name.");
        }
    }
}

bool XmlWriter::AttrName::IsDuplicate(const std::string& prefix, const std::string& localName,
    const std::string& namespaceUri) const
{
    if (this->localName == localName) {
        if (!(this->prefix == prefix))
            return this->namespaceUri == namespaceUri;
        return true;
    }
    return false;
}

void XmlWriter::AddAttribute(const std::string& prefix, const std::string& localName,
    const std::string& namespaceName)
{
    std::size_t num = attrCount_++;
    if (num == attrStack_.size())
        attrStack_.resize(num * 2);
    attrStack_[num] = {prefix, localName, namespaceName};
    for (std::size_t i = 0; i < num; ++i) {
        if (attrStack_[i].IsDuplicate(prefix, localName, namespaceName)) {
            std::string arg = prefix.empty() ? localName : prefix + ":" + localName;
            throw XmlException("'" + arg + "' is a duplicate attribute name.");
        }
    }
}

void XmlWriter::WriteStartDocument()
{
    WriteStartDocumentImpl(XmlStandalone::Omit);
}

void XmlWriter::WriteStartDocument(bool standalone)
{
    WriteStartDocumentImpl(standalone ? XmlStandalone::Yes : XmlStandalone::No);
}

void XmlWriter::WriteStartDocumentImpl(XmlStandalone standalone)
{
    try {
        AdvanceState(Token::StartDocument);
        if (conformanceLevel_ == ConformanceLevel::Auto) {
            conformanceLevel_ = ConformanceLevel::Document;
            stateTable_ = StateTableDocument();
        } else if (conformanceLevel_ == ConformanceLevel::Fragment) {
            throw std::runtime_error("WriteStartDocument cannot be called on writers created with ConformanceLevel.Fragment.");
        }
        // The raw writer's WriteXmlDeclaration.
        if (!settings_.OmitXmlDeclaration && !xmlDeclFollows_) {
            RawText(u"<?xml version=\"");
            RawText(u"1.0");
            if (!encodingWebName_.empty()) {
                RawText(u"\" encoding=\"");
                RawText(Util::Utf8ToUtf16(encodingWebName_));
            }
            if (standalone != XmlStandalone::Omit) {
                RawText(u"\" standalone=\"");
                RawText(standalone == XmlStandalone::Yes ? u"yes" : u"no");
            }
            RawText(u"\"?>");
        }
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteEndDocument()
{
    try {
        while (elemTop_ > 0)
            WriteEndElement();
        State current = currentState_;
        AdvanceState(Token::EndDocument);
        if (current != State::AfterRootEle)
            throw std::invalid_argument("Document does not have a root element.");
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteDocType(const std::string& name, const char* pubid, const char* sysid, const char* subset)
{
    try {
        if (name.empty())
            throw std::invalid_argument(EmptyStringMessage("name"));
        // XmlConvert.VerifyQName(name, ExceptionType.XmlException) -- the
        // port's VerifyName throws the same XmlException shapes.
        VerifyName(name);
        if (conformanceLevel_ == ConformanceLevel::Fragment)
            throw std::runtime_error("DTD is not allowed in XML fragments.");
        AdvanceState(Token::Dtd);
        if (dtdWritten_) {
            currentState_ = State::Error;
            throw std::runtime_error("The DTD has already been written out.");
        }
        if (conformanceLevel_ == ConformanceLevel::Auto) {
            conformanceLevel_ = ConformanceLevel::Document;
            stateTable_ = StateTableDocument();
        }
        if (checkCharacters_) {
            // The three ArgumentException arms all format Xml_InvalidCharacter
            // with XmlException.BuildCharExceptionArgs over the first bad unit.
            if (pubid != nullptr) {
                std::u16string p = Util::Utf8ToUtf16(pubid);
                for (std::size_t i = 0; i < p.size(); ++i) {
                    if (!IsPublicIdChar(p[i])) {
                        std::uint32_t nextChar = i + 1 < p.size() ? p[i + 1] : 0;
                        throw std::invalid_argument("'" + InvalidCharDisplay(p[i], nextChar)
                            + "', hexadecimal value " + FormatHexX2(InvalidCharValue(p[i], nextChar))
                            + ", is an invalid character.");
                    }
                }
            }
            if (sysid != nullptr) {
                std::u16string p = Util::Utf8ToUtf16(sysid);
                if (!IsOnlyCharData(p)) {
                    for (std::size_t i = 0; i < p.size(); ++i) {
                        bool pairOk = i + 1 < p.size() && p[i] >= 0xD800 && p[i] <= 0xDBFF
                            && p[i + 1] >= 0xDC00 && p[i + 1] <= 0xDFFF;
                        if (IsCharDataUnit(p[i]) || pairOk) {
                            if (pairOk)
                                ++i;
                            continue;
                        }
                        std::uint32_t nextChar = i + 1 < p.size() ? p[i + 1] : 0;
                        throw std::invalid_argument("'" + InvalidCharDisplay(p[i], nextChar)
                            + "', hexadecimal value " + FormatHexX2(InvalidCharValue(p[i], nextChar))
                            + ", is an invalid character.");
                    }
                }
            }
            if (subset != nullptr) {
                std::u16string p = Util::Utf8ToUtf16(subset);
                if (!IsOnlyCharData(p)) {
                    for (std::size_t i = 0; i < p.size(); ++i) {
                        bool pairOk = i + 1 < p.size() && p[i] >= 0xD800 && p[i] <= 0xDBFF
                            && p[i + 1] >= 0xDC00 && p[i + 1] <= 0xDFFF;
                        if (IsCharDataUnit(p[i]) || pairOk) {
                            if (pairOk)
                                ++i;
                            continue;
                        }
                        std::uint32_t nextChar = i + 1 < p.size() ? p[i + 1] : 0;
                        throw std::invalid_argument("'" + InvalidCharDisplay(p[i], nextChar)
                            + "', hexadecimal value " + FormatHexX2(InvalidCharValue(p[i], nextChar))
                            + ", is an invalid character.");
                    }
                }
            }
        }
        // The raw writer's WriteDocType.
        RawText(u"<!DOCTYPE ");
        RawText(Util::Utf8ToUtf16(name));
        if (pubid != nullptr) {
            RawText(u" PUBLIC \"");
            RawText(Util::Utf8ToUtf16(pubid));
            RawText(u"\" \"");
            if (sysid != nullptr)
                RawText(Util::Utf8ToUtf16(sysid));
            bufChars_[bufPos_++] = u'"';
        } else if (sysid != nullptr) {
            RawText(u" SYSTEM \"");
            RawText(Util::Utf8ToUtf16(sysid));
            bufChars_[bufPos_++] = u'"';
        } else {
            bufChars_[bufPos_++] = u' ';
        }
        if (subset != nullptr) {
            bufChars_[bufPos_++] = u'[';
            RawText(Util::Utf8ToUtf16(subset));
            bufChars_[bufPos_++] = u']';
        }
        bufChars_[bufPos_++] = u'>';
        dtdWritten_ = true;
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteStartElement(const char* prefix, const std::string& localName, const char* ns)
{
    try {
        if (localName.empty())
            throw std::invalid_argument(EmptyStringMessage("localName"));
        CheckNCName(localName);
        AdvanceState(Token::StartElement);
        std::string prefixValue = prefix != nullptr ? std::string(prefix) : std::string();
        std::string nsValue = ns != nullptr ? std::string(ns) : std::string();
        bool prefixIsNull = prefix == nullptr;
        bool nsIsNull = ns == nullptr;
        if (prefixIsNull) {
            if (!nsIsNull) {
                std::string p = LookupPrefix(nsValue);
                if (!p.empty())
                    prefixValue = p;
            }
            // A null prefix that resolves to nothing stays empty (the
            // LookupPrefix("")-shaped default).
        } else if (!prefixValue.empty()) {
            CheckNCName(prefixValue);
            if (nsIsNull) {
                nsValue = LookupNamespace(prefixValue);
                nsIsNull = false;
            }
            if (nsValue.empty())
                throw std::invalid_argument("Cannot use a prefix with an empty namespace.");
        }
        if (nsIsNull) {
            nsValue = LookupNamespace(prefixValue);
        }
        if (elemTop_ == 0) {
            // The raw writer's OnRootElement: the indent writer captures the
            // conformance level for its StartElementContent root rule.
            rootConformanceLevel_ = conformanceLevel_;
        }
        // The indent writer's WriteStartElement.
        if (settings_.Indent) {
            if (!mixedContent_ && textPos_ != bufPos_)
                WriteIndent();
            ++indentLevel_;
            mixedContentStack_.push_back(mixedContent_);
        }
        // The raw writer's WriteStartElement.
        bufChars_[bufPos_++] = u'<';
        if (!prefixValue.empty()) {
            RawText(Util::Utf8ToUtf16(prefixValue));
            bufChars_[bufPos_++] = u':';
        }
        RawText(Util::Utf8ToUtf16(localName));
        attrEndPos_ = bufPos_;

        std::size_t num = ++elemTop_;
        if (num == elemScopeStack_.size())
            elemScopeStack_.resize(num * 2);
        elemScopeStack_[num] = {prefixValue, localName, nsValue, nsTop_};
        PushNamespaceImplicit(prefixValue, nsValue);
        attrCount_ = 0;
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteEndElement()
{
    try {
        AdvanceState(Token::EndElement);
        std::size_t top = elemTop_;
        if (top == 0)
            throw XmlException("There was no XML start tag open.");
        const ElementScope& scope = elemScopeStack_[top];
        // The indent writer's WriteEndElement.
        if (settings_.Indent) {
            --indentLevel_;
            if (!mixedContent_ && contentPos_ != bufPos_ && textPos_ != bufPos_)
                WriteIndent();
            if (!mixedContentStack_.empty()) {
                mixedContent_ = mixedContentStack_.back();
                mixedContentStack_.pop_back();
            }
        }
        // The raw writer's WriteEndElement (self-closing when no content).
        if (contentPos_ != bufPos_) {
            bufChars_[bufPos_++] = u'<';
            bufChars_[bufPos_++] = u'/';
            if (!scope.prefix.empty()) {
                RawText(Util::Utf8ToUtf16(scope.prefix));
                bufChars_[bufPos_++] = u':';
            }
            RawText(Util::Utf8ToUtf16(scope.localName));
            bufChars_[bufPos_++] = u'>';
        } else {
            --bufPos_;
            bufChars_[bufPos_++] = u' ';
            bufChars_[bufPos_++] = u'/';
            bufChars_[bufPos_++] = u'>';
        }
        nsTop_ = scope.prevNSTop;
        if ((elemTop_ = top - 1) == 0) {
            if (conformanceLevel_ == ConformanceLevel::Document)
                currentState_ = State::AfterRootEle;
            else
                currentState_ = State::TopLevel;
        }
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteFullEndElement()
{
    try {
        AdvanceState(Token::EndElement);
        std::size_t top = elemTop_;
        if (top == 0)
            throw XmlException("There was no XML start tag open.");
        const ElementScope& scope = elemScopeStack_[top];
        // The indent writer's WriteFullEndElement.
        if (settings_.Indent) {
            --indentLevel_;
            if (!mixedContent_ && contentPos_ != bufPos_ && textPos_ != bufPos_)
                WriteIndent();
            if (!mixedContentStack_.empty()) {
                mixedContent_ = mixedContentStack_.back();
                mixedContentStack_.pop_back();
            }
        }
        // The raw writer's WriteFullEndElement.
        bufChars_[bufPos_++] = u'<';
        bufChars_[bufPos_++] = u'/';
        if (!scope.prefix.empty()) {
            RawText(Util::Utf8ToUtf16(scope.prefix));
            bufChars_[bufPos_++] = u':';
        }
        RawText(Util::Utf8ToUtf16(scope.localName));
        bufChars_[bufPos_++] = u'>';
        nsTop_ = scope.prevNSTop;
        if ((elemTop_ = top - 1) == 0) {
            if (conformanceLevel_ == ConformanceLevel::Document)
                currentState_ = State::AfterRootEle;
            else
                currentState_ = State::TopLevel;
        }
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteStartAttribute(const char* prefix, const std::string& localName, const char* ns)
{
    // The C# WriteStartAttribute verbatim: the empty-localName normalization,
    // the null-prefix/null-namespace resolution, and the four dispatch arms
    // (normal attribute, default xmlns, prefixed xmlns, xml:space/xml:lang).
    try {
        std::string localNameValue = localName;
        std::string prefixValue;
        bool prefixIsNull = prefix == nullptr;
        if (!prefixIsNull)
            prefixValue = prefix;
        if (localNameValue.empty()) {
            if (prefixValue != "xmlns")
                throw std::invalid_argument("The empty string '' is not a valid local name.");
            localNameValue = "xmlns";
            prefixValue = "";
            prefixIsNull = false;
        }
        CheckNCName(localNameValue);
        AdvanceState(Token::StartAttribute);
        std::string namespaceName = ns != nullptr ? std::string(ns) : std::string();
        bool nsIsNull = ns == nullptr;
        if (prefixIsNull) {
            if (!nsIsNull && (!(localNameValue == "xmlns")
                || !(namespaceName == "http://www.w3.org/2000/xmlns/"))) {
                std::string p = LookupPrefix(namespaceName);
                if (!p.empty()) {
                    prefixValue = p;
                    prefixIsNull = false;
                }
            }
        }
        if (prefixIsNull)
            prefixValue = "";
        if (nsIsNull) {
            if (!prefixValue.empty())
                namespaceName = LookupNamespace(prefixValue);
        }
        if (prefixValue.empty()) {
            if (localNameValue[0] != 'x' || localNameValue != "xmlns") {
                // A normal attribute: a non-empty namespace resolves to a
                // usable in-scope prefix or a generated one (the default
                // namespace prefix is not usable for attributes).
                if (!namespaceName.empty()) {
                    std::string p = LookupPrefix(namespaceName);
                    if (!p.empty())
                        prefixValue = p;
                    else
                        prefixValue = GeneratePrefix();
                }
                // IL_01f2
                if (!prefixValue.empty())
                    PushNamespaceImplicit(prefixValue, namespaceName);
                AddAttribute(prefixValue, localNameValue, namespaceName);
                RawWriteStartAttribute(prefixValue, localNameValue);
                return;
            }
            // The default xmlns declaration (prefix "", localName "xmlns").
            if (!namespaceName.empty() && namespaceName != "http://www.w3.org/2000/xmlns/")
                throw std::invalid_argument("Prefix \"xmlns\" is reserved for use by XML.");
            curDeclPrefix_ = "";
            SetSpecialAttribute(SpecialAttribute::DefaultXmlns);
            AddAttribute(prefixValue, localNameValue, namespaceName);
            return;
        }
        if (prefixValue[0] == 'x' && prefixValue == "xmlns") {
            if (!namespaceName.empty() && namespaceName != "http://www.w3.org/2000/xmlns/")
                throw std::invalid_argument("Prefix \"xmlns\" is reserved for use by XML.");
            curDeclPrefix_ = localNameValue;
            SetSpecialAttribute(SpecialAttribute::PrefixedXmlns);
            AddAttribute(prefixValue, localNameValue, namespaceName);
            return;
        }
        if (prefixValue[0] == 'x' && prefixValue == "xml") {
            if (!namespaceName.empty() && namespaceName != "http://www.w3.org/XML/1998/namespace")
                throw std::invalid_argument("Prefix \"xml\" is reserved for use by XML and can be mapped only to namespace name \"http://www.w3.org/XML/1998/namespace\".");
            if (localNameValue == "space") {
                SetSpecialAttribute(SpecialAttribute::XmlSpace);
                AddAttribute(prefixValue, localNameValue, namespaceName);
                return;
            }
            if (localNameValue == "lang") {
                SetSpecialAttribute(SpecialAttribute::XmlLang);
                AddAttribute(prefixValue, localNameValue, namespaceName);
                return;
            }
            // fall through to the normal path
        }
        // IL_01bf
        CheckNCName(prefixValue);
        if (namespaceName.empty()) {
            prefixValue = "";
        } else {
            std::string local = LookupLocalNamespace(prefixValue);
            if (!local.empty() && local != namespaceName)
                prefixValue = GeneratePrefix();
        }
        // IL_01f2
        if (!prefixValue.empty())
            PushNamespaceImplicit(prefixValue, namespaceName);
        AddAttribute(prefixValue, localNameValue, namespaceName);
        RawWriteStartAttribute(prefixValue, localNameValue);
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::RawWriteStartAttribute(const std::string& prefix, const std::string& localName)
{
    // The raw writer's WriteStartAttribute (+ the indent writer's
    // NewLineOnAttributes indent, not reachable: the setting defaults false).
    if (attrEndPos_ == bufPos_)
        bufChars_[bufPos_++] = u' ';
    if (!prefix.empty()) {
        RawText(Util::Utf8ToUtf16(prefix));
        bufChars_[bufPos_++] = u':';
    }
    RawText(Util::Utf8ToUtf16(localName));
    bufChars_[bufPos_++] = u'=';
    bufChars_[bufPos_++] = u'"';
    inAttributeValue_ = true;
}

void XmlWriter::SetSpecialAttribute(SpecialAttribute special)
{
    specAttr_ = special;
    if (currentState_ == State::Attribute)
        currentState_ = State::SpecialAttr;
    else if (currentState_ == State::RootLevelAttr)
        currentState_ = State::RootLevelSpecAttr;
    attrValueCache_.clear();
}

void XmlWriter::WriteEndAttribute()
{
    try {
        AdvanceState(Token::EndAttribute);
        if (specAttr_ != SpecialAttribute::No) {
            switch (specAttr_) {
            case SpecialAttribute::DefaultXmlns: {
        std::string stringValue = attrValueCache_;
        if (PushNamespaceExplicit("", stringValue)) {
            RawWriteNamespaceDeclaration("", stringValue);
        }
        curDeclPrefix_.clear();
        break;
    }
    case SpecialAttribute::PrefixedXmlns: {
        std::string stringValue = attrValueCache_;
        if (stringValue.empty())
            throw std::invalid_argument("Cannot use a prefix with an empty namespace.");
                if (stringValue == "http://www.w3.org/2000/xmlns/"
                    || (stringValue == "http://www.w3.org/XML/1998/namespace" && curDeclPrefix_ != "xml")) {
                    throw std::invalid_argument("Cannot bind to the reserved namespace.");
                }
                if (PushNamespaceExplicit(curDeclPrefix_, stringValue)) {
                    RawWriteNamespaceDeclaration(curDeclPrefix_, stringValue);
                }
                curDeclPrefix_.clear();
                break;
            }
            case SpecialAttribute::XmlSpace: {
                // The cache Trim() happens only for xml:space.
                attrValueCache_ = TrimUtf16(attrValueCache_);
                std::string stringValue = attrValueCache_;
                if (stringValue != "default") {
                    if (stringValue != "preserve")
                        throw std::invalid_argument("'" + stringValue + "' is an invalid xml:space value.");
                }
                RawWriteStartAttribute("xml", "space");
                WriteStringRaw(stringValue);
                inAttributeValue_ = false;
                bufChars_[bufPos_++] = u'"';
                attrEndPos_ = bufPos_;
                break;
            }
            case SpecialAttribute::XmlLang: {
                std::string stringValue = attrValueCache_;
                RawWriteStartAttribute("xml", "lang");
                WriteStringRaw(stringValue);
                inAttributeValue_ = false;
                bufChars_[bufPos_++] = u'"';
                attrEndPos_ = bufPos_;
                break;
            }
            default:
                break;
            }
            specAttr_ = SpecialAttribute::No;
            attrValueCache_.clear();
        } else {
            // The raw writer's WriteEndAttribute.
            inAttributeValue_ = false;
            bufChars_[bufPos_++] = u'"';
            attrEndPos_ = bufPos_;
        }
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

std::string XmlWriter::TrimUtf16(const std::string& utf8)
{
    std::u16string units = Util::Utf8ToUtf16(utf8);
    std::size_t b = 0, e = units.size();
    while (b < e && IsXmlWhitespaceUnit(units[b]))
        ++b;
    while (e > b && IsXmlWhitespaceUnit(units[e - 1]))
        --e;
    return Util::Utf16ToUtf8(std::u16string_view(units.data() + b, e - b));
}

void XmlWriter::RawWriteNamespaceDeclaration(const std::string& prefix, const std::string& namespaceName)
{
    // The raw writer's WriteStartNamespaceDeclaration + WriteString +
    // WriteEndNamespaceDeclaration.
    if (attrEndPos_ == bufPos_)
        bufChars_[bufPos_++] = u' ';
    if (prefix.empty()) {
        RawText(u"xmlns=\"");
    } else {
        RawText(u"xmlns:");
        RawText(Util::Utf8ToUtf16(prefix));
        bufChars_[bufPos_++] = u'=';
        bufChars_[bufPos_++] = u'"';
    }
    inAttributeValue_ = true;
    WriteStringRaw(namespaceName);
    inAttributeValue_ = false;
    bufChars_[bufPos_++] = u'"';
    attrEndPos_ = bufPos_;
}

void XmlWriter::WriteAttributeString(const char* prefix, const std::string& localName, const char* ns, const char* value)
{
    WriteStartAttribute(prefix, localName, ns);
    if (value != nullptr)
        WriteString(std::string(value));
    WriteEndAttribute();
}

void XmlWriter::WriteString(const std::string& text)
{
    try {
        AdvanceState(Token::Text);
        if (specAttr_ != SpecialAttribute::No) {
            // The AttributeValueCache accumulation (string chunks only; the
            // entity chunk kinds are not reachable from the XNode paths).
            attrValueCache_ += text;
        } else {
            WriteStringRaw(text);
        }
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteString(std::nullptr_t)
{
    // The C# WriteString(null) is a no-op after the state advance.
    try {
        AdvanceState(Token::Text);
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteStringRaw(const std::string& text)
{
    // The raw writer's WriteString (+ the indent writer's mixed-content mark).
    std::u16string units = Util::Utf8ToUtf16(text);
    if (settings_.Indent)
        mixedContent_ = true;
    if (inAttributeValue_)
        WriteAttributeTextBlock(units);
    else
        WriteElementTextBlock(units);
}

void XmlWriter::WriteComment(const std::string& text)
{
    try {
        AdvanceState(Token::Comment);
        // The indent writer's WriteComment.
        if (settings_.Indent && !mixedContent_ && textPos_ != bufPos_)
            WriteIndent();
        // The raw writer's WriteComment.
        bufChars_[bufPos_++] = u'<';
        bufChars_[bufPos_++] = u'!';
        bufChars_[bufPos_++] = u'-';
        bufChars_[bufPos_++] = u'-';
        WriteCommentOrPi(Util::Utf8ToUtf16(text), u'-');
        bufChars_[bufPos_++] = u'-';
        bufChars_[bufPos_++] = u'-';
        bufChars_[bufPos_++] = u'>';
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteCData(const std::string& text)
{
    try {
        AdvanceState(Token::CData);
        // The indent writer's WriteCData (mixed-content mark, no indent
        // insertion -- the C# WriteCData does not check _textPos).
        if (settings_.Indent)
            mixedContent_ = true;
        // The raw writer's WriteCData (MergeCDataSections defaults false).
        bufChars_[bufPos_++] = u'<';
        bufChars_[bufPos_++] = u'!';
        bufChars_[bufPos_++] = u'[';
        bufChars_[bufPos_++] = u'C';
        bufChars_[bufPos_++] = u'D';
        bufChars_[bufPos_++] = u'A';
        bufChars_[bufPos_++] = u'T';
        bufChars_[bufPos_++] = u'A';
        bufChars_[bufPos_++] = u'[';
        WriteCDataSection(Util::Utf8ToUtf16(text));
        bufChars_[bufPos_++] = u']';
        bufChars_[bufPos_++] = u']';
        bufChars_[bufPos_++] = u'>';
        textPos_ = bufPos_;
        cdataPos_ = bufPos_;
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteProcessingInstruction(const std::string& name, const std::string& text)
{
    try {
        if (name.empty())
            throw std::invalid_argument(EmptyStringMessage("name"));
        CheckNCName(name);
        std::string data = text;
        bool isXmlDeclName = name.size() == 3
            && (name[0] == 'x' || name[0] == 'X')
            && (name[1] == 'm' || name[1] == 'M')
            && (name[2] == 'l' || name[2] == 'L');
        if (isXmlDeclName) {
            if (currentState_ != State::Start) {
                throw std::invalid_argument(conformanceLevel_ == ConformanceLevel::Document
                    ? std::string("Cannot write XML declaration. WriteStartDocument method has already written it.")
                    : std::string("Cannot write XML declaration. XML declaration can be only at the beginning of the document."));
            }
            xmlDeclFollows_ = true;
            AdvanceState(Token::PI);
            if (!settings_.OmitXmlDeclaration) {
                // The raw writer's WriteXmlDeclaration(string): a PI written
                // as-is.
                bufChars_[bufPos_++] = u'<';
                bufChars_[bufPos_++] = u'?';
                RawText(u"xml");
                if (!data.empty()) {
                    bufChars_[bufPos_++] = u' ';
                    WriteCommentOrPi(Util::Utf8ToUtf16(data), u'?');
                }
                bufChars_[bufPos_++] = u'?';
                bufChars_[bufPos_++] = u'>';
            }
        } else {
            AdvanceState(Token::PI);
            // The indent writer's WriteProcessingInstruction.
            if (settings_.Indent && !mixedContent_ && textPos_ != bufPos_)
                WriteIndent();
            // The raw writer's WriteProcessingInstruction.
            bufChars_[bufPos_++] = u'<';
            bufChars_[bufPos_++] = u'?';
            RawText(Util::Utf8ToUtf16(name));
            if (!data.empty()) {
                bufChars_[bufPos_++] = u' ';
                WriteCommentOrPi(Util::Utf8ToUtf16(data), u'?');
            }
            bufChars_[bufPos_++] = u'?';
            bufChars_[bufPos_++] = u'>';
        }
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::WriteWhitespace(const std::string& ws)
{
    try {
        std::u16string units = Util::Utf8ToUtf16(ws);
        if (!IsOnlyWhitespace(units))
            throw std::invalid_argument("Only whitespace characters should be used.");
        AdvanceState(Token::Whitespace);
        if (settings_.Indent)
            mixedContent_ = true;
        if (inAttributeValue_)
            WriteAttributeTextBlock(units);
        else
            WriteElementTextBlock(units);
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::Flush()
{
    try {
        FlushBuffer();
    } catch (...) {
        currentState_ = State::Error;
        throw;
    }
}

void XmlWriter::Close()
{
    if (currentState_ == State::Closed)
        return;
    try {
        if (writeEndDocumentOnClose_) {
            while (currentState_ != State::Error && elemTop_ > 0)
                WriteEndElement();
        } else if (currentState_ != State::Error && elemTop_ > 0) {
            try {
                AdvanceState(Token::EndElement);
            } catch (...) {
                currentState_ = State::Error;
                throw;
            }
        }
        FlushBuffer();
    } catch (...) {
        writeToNull_ = true;
        currentState_ = State::Closed;
        throw;
    }
    writeToNull_ = true;
    currentState_ = State::Closed;
}

std::string XmlWriter::OutputUtf8() const
{
    return Util::Utf16ToUtf8(sink_);
}

std::vector<std::uint8_t> XmlWriter::FileBytes() const
{
    std::vector<std::uint8_t> out;
    if (sinkKind_ == XmlWriterSink::File) {
        // The UTF-8 encoding preamble (Encoding.UTF8 emits the BOM).
        out.push_back(0xEF);
        out.push_back(0xBB);
        out.push_back(0xBF);
    }
    std::string utf8 = Util::Utf16ToUtf8(sink_);
    out.insert(out.end(), utf8.begin(), utf8.end());
    return out;
}

} // namespace ILSpy::Decompiler::Xml
