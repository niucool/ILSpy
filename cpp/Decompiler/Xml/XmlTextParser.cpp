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

// The implementation of the XmlReader text-parser stand-in behind
// XElement.Parse/XDocument.Parse (see XmlTextParser.hpp for the full
// behavioral contract). The grammar is a recursive-descent walk over a
// pre-normalized UTF-16 buffer mirroring the observable .NET 10
// XmlTextReaderImpl semantics (gold-pinned through the C:/temp-probe/
// XmlParseProbe probe; see the header for the position/whitespace/namespace
// rules this reproduces).

#include "XmlTextParser.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "XCData.hpp"
#include "XComment.hpp"
#include "XDocument.hpp"
#include "XDocumentType.hpp"
#include "XElement.hpp"
#include "XName.hpp"
#include "XNamespace.hpp"
#include "XProcessingInstruction.hpp"
#include "XText.hpp"
#include "XmlConvert.hpp"

namespace ILSpy::Decompiler::Xml {
namespace {

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16 (the permissive XmlConvert.cpp conventions: valid
// sequences decode to code points, surrogate-pair encodings of lone
// surrogates decode to the unit itself, malformed bytes decode to U+FFFD).
// ---------------------------------------------------------------------------

std::u16string DecodeUtf8(const std::string& s)
{
    std::u16string out;
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        std::uint32_t cp;
        std::size_t width;
        if (b < 0x80) {
            cp = b;
            width = 1;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            width = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            width = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            width = 4;
        } else {
            out.push_back(u'\uFFFD');
            ++i;
            continue;
        }
        bool ok = i + width <= s.size();
        for (std::size_t k = 1; ok && k < width; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) {
                ok = false;
            } else {
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        if (!ok) {
            out.push_back(u'\uFFFD');
            ++i;
            continue;
        }
        if (width == 4) {
            if (cp > 0x10FFFF) {
                out.push_back(u'\uFFFD');
            } else {
                cp -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
            }
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
        i += width;
    }
    return out;
}

std::string EncodeUtf8(const std::u16string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        char16_t u = s[i];
        if (u < 0x80) {
            out.push_back(static_cast<char>(u));
        } else if (u < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (u >> 6)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        } else if (u >= 0xD800 && u <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            std::uint32_t cp = 0x10000 + ((static_cast<std::uint32_t>(u) - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            ++i;
        } else {
            // Lone surrogate units encode as their WTF-8 pair form (they
            // round-trip through DecodeUtf8); everything else is a BMP unit.
            out.push_back(static_cast<char>(0xE0 | (u >> 12)));
            out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
        }
    }
    return out;
}

// The .NET "{0:X2}" formatting of a char value (hex, uppercase, minimum
// two digits).
std::string HexX2(std::int32_t value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02X", value);
    return buffer;
}

bool IsXmlWhitespaceCp(std::uint32_t cp)
{
    return cp == 0x20 || cp == 0x9 || cp == 0xA || cp == 0xD;
}

bool IsValidXmlCharCp(std::uint32_t cp)
{
    return cp == 0x9 || cp == 0xA || cp == 0xD
        || (cp >= 0x20 && cp <= 0xD7FF)
        || (cp >= 0xE000 && cp <= 0xFFFD)
        || (cp >= 0x10000 && cp <= 0x10FFFF);
}

// ---------------------------------------------------------------------------
// The parser.
// ---------------------------------------------------------------------------

struct EntityDecl {
    std::u16string value;
    bool external = false;
    bool parameter = false;
    // The declaration site (the value's opening-quote position, for the
    // entity-relative positions of the markup-in-value splice path).
    int declLineNo = 1;
    std::ptrdiff_t quoteLinePos = 0;
};

struct ParsedQName {
    std::u16string prefix;
    std::u16string local;

    std::u16string Raw() const
    {
        return prefix.empty() ? local : prefix + u":" + local;
    }
};

struct AttrData {
    std::u16string prefix;
    std::u16string local;
    std::u16string value;
    std::size_t nameStart = 0;
    std::size_t quotePos = 0; // the opening quote
    std::size_t valueStart = 0; // the char after the opening quote
};

struct OpenElem {
    std::u16string rawName;
    int startLine = 0;
    std::ptrdiff_t startPos = 0;
};

struct NsBinding {
    std::u16string prefix;
    std::u16string uri;
};

class Parser {
public:
    explicit Parser(bool documentMode)
        : documentMode_(documentMode)
    {
    }

    std::shared_ptr<XElement> ParseElementRoot(const std::string& text);
    std::shared_ptr<XDocument> ParseDocumentRoot(const std::string& text);

private:
    // ---- state ----
    bool documentMode_ = false;
    std::u16string buf_;
    std::size_t len_ = 0;
    std::size_t pos_ = 0;
    int lineNo_ = 1;
    std::ptrdiff_t lineStartPos_ = -1;

    bool rootElementParsed_ = false;
    bool sawDoctype_ = false;
    std::optional<XDeclaration> declaration_;
    std::shared_ptr<XDocumentType> docType_;
    std::vector<std::pair<std::u16string, EntityDecl>> entities_;
    std::vector<std::shared_ptr<XNode>> rootBefore_;
    std::vector<std::shared_ptr<XNode>> rootAfter_;

    std::vector<std::vector<NsBinding>> nsStack_;
    std::vector<OpenElem> openElements_;

    // The entity-splice context flag (SpliceMarkupEntity sets it; every
    // content loop -- including the ones ParseElement enters for child
    // elements -- respects it, so the entity EOF arms apply inside nested
    // elements too).
    bool inEntity_ = false;

    // The DOCTYPE context flag: name scans that hit the buffer end report
    // "Incomplete DTD content." instead of the "while parsing Name" EOF
    // arm (the DTD parser owns the EOF inside the doctype).
    bool inDoctype_ = false;

    // ---- positions ----
    std::ptrdiff_t LinePos(std::size_t p) const
    {
        return static_cast<std::ptrdiff_t>(p) - lineStartPos_;
    }

    // The code point (and unit width) at a position; surrogate pairs
    // combine.
    std::pair<std::uint32_t, std::size_t> CpAt(std::size_t p) const
    {
        if (p >= len_)
            return {0, 1};
        char16_t u = buf_[p];
        if (u >= 0xD800 && u <= 0xDBFF && p + 1 < len_ && buf_[p + 1] >= 0xDC00 && buf_[p + 1] <= 0xDFFF)
            return {0x10000 + ((static_cast<std::uint32_t>(u) - 0xD800) << 10) + (buf_[p + 1] - 0xDC00), 2};
        return {u, 1};
    }

    bool StartsWith(std::size_t p, const char16_t* literal) const
    {
        std::size_t i = p;
        for (const char16_t* q = literal; *q != 0; ++q, ++i) {
            if (i >= len_ || buf_[i] != *q)
                return false;
        }
        return true;
    }

    // ---- errors ----
    [[noreturn]] void Fail(const std::string& message, int line, std::ptrdiff_t linePos)
    {
        throw XmlException(message + " Line " + std::to_string(line) + ", position " + std::to_string(linePos) + ".");
    }

    [[noreturn]] void FailAt(const std::string& message, std::size_t p)
    {
        pos_ = p;
        Fail(message, lineNo_, LinePos(p));
    }

    [[noreturn]] void FailEof(const std::string& message)
    {
        FailAt(message, len_);
    }

    [[noreturn]] void FailNoLineInfo(const std::string& message)
    {
        throw XmlException(message);
    }

    [[noreturn]] void FailEofParsing(const std::string& what)
    {
        FailEof("Unexpected end of file while parsing " + what + " has occurred.");
    }

    // The same SR at an explicit position (the CDATA/comment entry checks
    // and the char-ref EOF arms, which report the entry char).
    [[noreturn]] void FailEofParsingAt(const std::string& what, std::size_t p)
    {
        FailAt("Unexpected end of file while parsing " + what + " has occurred.", p);
    }

    // The invalid-char pair at a buffer position (the .NET
    // BuildCharExceptionArgs over the unit and its successor; the successor
    // is NUL at the buffer end).
    void InvalidCharArgsAt(std::size_t p, std::string& display, std::int32_t& value) const
    {
        std::uint32_t inv = p < len_ ? buf_[p] : 0;
        std::uint32_t next = p + 1 < len_ ? buf_[p + 1] : 0;
        display = InvalidCharDisplay(inv, next);
        value = InvalidCharValue(inv, next);
    }

    [[noreturn]] void FailBadStartNameChar(std::size_t p)
    {
        std::string display;
        std::int32_t value;
        InvalidCharArgsAt(p, display, value);
        FailAt("Name cannot begin with the '" + display + "' character, hexadecimal value 0x" + HexX2(value) + ".", p);
    }

    [[noreturn]] void FailBadNameChar(std::size_t p)
    {
        std::string display;
        std::int32_t value;
        InvalidCharArgsAt(p, display, value);
        FailAt("The '" + display + "' character, hexadecimal value 0x" + HexX2(value) + ", cannot be included in a name.", p);
    }

    [[noreturn]] void FailInvalidChar(std::size_t p)
    {
        std::string display;
        std::int32_t value;
        InvalidCharArgsAt(p, display, value);
        FailAt("'" + display + "', hexadecimal value 0x" + HexX2(value) + ", is an invalid character.", p);
    }

    // The unexpected-token scan: a name run when the char is a name char,
    // otherwise that single char.
    std::u16string TokenAt(std::size_t p) const
    {
        if (p >= len_)
            return {};
        auto [cp, width] = CpAt(p);
        if (IsNameChar(cp)) {
            std::u16string token;
            std::size_t i = p;
            while (i < len_) {
                auto [c2, w2] = CpAt(i);
                if (!IsNameChar(c2))
                    break;
                token.append(buf_, i, w2);
                i += w2;
            }
            return token;
        }
        return buf_.substr(p, width);
    }

    [[noreturn]] void FailUnexpectedToken(std::size_t p, const std::string& expected)
    {
        std::u16string token = TokenAt(p);
        if (token.empty())
            FailAt("Unexpected end of file has occurred.", p);
        FailAt("'" + EncodeUtf8(token) + "' is an unexpected token. The expected token is '" + expected + "'.", p);
    }

    [[noreturn]] void FailUnexpectedToken2(std::size_t p, const std::string& expected1, const std::string& expected2)
    {
        std::u16string token = TokenAt(p);
        if (token.empty())
            FailAt("Unexpected end of file has occurred.", p);
        FailAt("'" + EncodeUtf8(token) + "' is an unexpected token. The expected token is '" + expected1 + "' or '" + expected2 + "'.", p);
    }

    [[noreturn]] void FailExpectingWhitespace(std::size_t p)
    {
        std::u16string token = TokenAt(p);
        if (token.empty())
            FailAt("Unexpected end of file has occurred.", p);
        FailAt("'" + EncodeUtf8(token) + "' is an unexpected token. Expecting whitespace.", p);
    }

    // The unclosed-elements list: the open elements innermost-first joined
    // ", " with a trailing "." ("" when empty).
    std::string UnclosedList() const
    {
        std::string list;
        for (auto it = openElements_.rbegin(); it != openElements_.rend(); ++it) {
            if (!list.empty())
                list += ", ";
            list += EncodeUtf8(it->rawName);
        }
        if (!list.empty())
            list += ".";
        return list;
    }

    [[noreturn]] void FailUnclosedEof()
    {
        FailAt("Unexpected end of file has occurred. The following elements are not closed: " + UnclosedList(), len_);
    }

    // ---- whitespace / lines ----
    void OnNewLine(std::size_t afterNewline)
    {
        ++lineNo_;
        lineStartPos_ = static_cast<std::ptrdiff_t>(afterNewline) - 1;
    }

    void SkipWs()
    {
        while (pos_ < len_) {
            auto [cp, width] = CpAt(pos_);
            if (!IsXmlWhitespaceCp(cp))
                break;
            if (cp == 0xA)
                OnNewLine(pos_ + width);
            pos_ += width;
        }
    }

    // ---- names ----
    // A QName: one NCName run, at most one ':' separator. Throws the exact
    // .NET name arms (a bad first char, a bad continuation, a second ':').
    ParsedQName ParseQName()
    {
        ParsedQName name;
        auto [cp, width] = CpAt(pos_);
        if (pos_ >= len_ || !IsStartNameChar(cp))
            FailBadStartNameChar(pos_);
        std::u16string& first = name.prefix;
        first.append(buf_, pos_, width);
        pos_ += width;
        while (pos_ < len_) {
            auto [c2, w2] = CpAt(pos_);
            if (IsNameChar(c2)) {
                first.append(buf_, pos_, w2);
                pos_ += w2;
                continue;
            }
            break;
        }
        if (pos_ < len_ && buf_[pos_] == u':') {
            pos_++;
            if (pos_ >= len_) {
                if (inDoctype_)
                    FailEof("Incomplete DTD content.");
                FailEofParsing("Name");
            }
            auto [c3, w3] = CpAt(pos_);
            if (!IsStartNameChar(c3))
                FailBadStartNameChar(pos_);
            name.local.append(buf_, pos_, w3);
            pos_ += w3;
            while (pos_ < len_) {
                auto [c4, w4] = CpAt(pos_);
                if (IsNameChar(c4)) {
                    name.local.append(buf_, pos_, w4);
                    pos_ += w4;
                    continue;
                }
                if (buf_[pos_] == u':')
                    FailBadNameChar(pos_);
                break;
            }
        } else {
            name.local = name.prefix;
            name.prefix.clear();
        }
        // A name that runs to the buffer end (no terminator) is the
        // .NET "while parsing Name" EOF arm ("Incomplete DTD content."
        // inside the DOCTYPE, where the DTD parser owns the EOF).
        if (pos_ >= len_) {
            if (inDoctype_)
                FailEof("Incomplete DTD content.");
            FailEofParsing("Name");
        }
        return name;
    }

    // ---- entity references ----
    const EntityDecl* LookupEntity(const std::u16string& name) const
    {
        for (const auto& entry : entities_) {
            if (entry.first == name && !entry.second.parameter)
                return &entry.second;
        }
        return nullptr;
    }

    // Expands an entity value at use: char refs and nested entity
    // references expand recursively. Markup inside the value is the
    // documented deferral (the .NET reader splices it as a nested parse
    // context).
    void AppendEntityValue(const EntityDecl& decl, std::u16string& out)
    {
        if (decl.external)
            return; // external entities expand to empty
        std::size_t i = 0;
        const std::u16string& v = decl.value;
        while (i < v.size()) {
            char16_t u = v[i];
            if (u == u'&') {
                std::size_t j = i + 1;
                if (j < v.size() && v[j] == u'#') {
                    ++j;
                    bool hex = j < v.size() && (v[j] == u'x' || v[j] == u'X');
                    if (hex)
                        ++j;
                    std::uint64_t value = 0;
                    bool any = false;
                    while (j < v.size()) {
                        char16_t d = v[j];
                        std::uint32_t digit = hex
                            ? (d >= u'0' && d <= u'9' ? d - u'0' : d >= u'a' && d <= u'f' ? d - u'a' + 10 : d >= u'A' && d <= u'F' ? d - u'A' + 10 : 32)
                            : (d >= u'0' && d <= u'9' ? d - u'0' : 32);
                        if (digit > static_cast<std::uint32_t>(hex ? 15 : 9))
                            break;
                        value = value * (hex ? 16 : 10) + digit;
                        any = true;
                        ++j;
                    }
                    if (any && j < v.size() && v[j] == u';') {
                        AppendCharRefUnits(value, out);
                        i = j + 1;
                        continue;
                    }
                    break; // malformed ref inside a value: end of the value
                }
                std::size_t k = j;
                while (k < v.size() && (IsNameChar(v[k]) || v[k] == u':'))
                    ++k;
                if (k > j && k < v.size() && v[k] == u';') {
                    std::u16string innerName(v, j, k - j);
                    // The five predefined entities expand without a
                    // declaration (like the content path).
                    if (innerName == u"lt") {
                        out.push_back(u'<');
                    } else if (innerName == u"gt") {
                        out.push_back(u'>');
                    } else if (innerName == u"amp") {
                        out.push_back(u'&');
                    } else if (innerName == u"quot") {
                        out.push_back(u'"');
                    } else if (innerName == u"apos") {
                        out.push_back(u'\'');
                    } else {
                        const EntityDecl* inner = LookupEntity(innerName);
                        if (inner == nullptr)
                            throw std::logic_error("Reference to undeclared entity '" + EncodeUtf8(innerName) + "' inside an entity value (the nested-entity position semantics are a documented deferral).");
                        AppendEntityValue(*inner, out);
                    }
                    i = k + 1;
                    continue;
                }
                break;
            }
            if (u == u'<')
                throw std::logic_error("Markup inside an entity value used in an attribute or nested entity is a documented deferral (in element content it splices as a nested parse context; the attribute path defers).");
            out.push_back(u);
            ++i;
        }
    }

    // Appends a char-ref value as UTF-16 units without validation (used for
    // refs nested inside declared entity values).
    void AppendCharRefUnits(std::uint64_t value, std::u16string& out)
    {
        if (value > 0xFFFF) {
            std::uint32_t cp = static_cast<std::uint32_t>(value);
            if (cp > 0x10FFFF)
                return;
            out.push_back(static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(value));
        }
    }

    // Parses a numeric char reference at an '&'; appends the units. The
    // exact .NET arms (the syntax errors at the offending char, the invalid
    // values at the first digit with the surrogate-construction overflow
    // quirk).
    void ParseCharRef(std::u16string& out)
    {
        std::size_t ampPos = pos_; // the '&' (the EOF arms report here)
        std::size_t p = pos_ + 2; // past "&#" or "&#"
        bool hex = false;
        if (pos_ + 1 < len_ && buf_[pos_ + 1] == u'#') {
            if (p < len_ && (buf_[p] == u'x' || buf_[p] == u'X')) {
                hex = true;
                ++p;
            }
        }
        std::size_t digitsStart = p;
        std::uint64_t value = 0;
        while (p < len_) {
            char16_t d = buf_[p];
            std::uint32_t digit = hex
                ? (d >= u'0' && d <= u'9' ? d - u'0' : d >= u'a' && d <= u'f' ? d - u'a' + 10 : d >= u'A' && d <= u'F' ? d - u'A' + 10 : 32)
                : (d >= u'0' && d <= u'9' ? d - u'0' : 32);
            if (digit > static_cast<std::uint32_t>(hex ? 15 : 9))
                break;
            value = value * (hex ? 16 : 10) + digit;
            ++p;
        }
        if (p == digitsStart) {
            if (p >= len_)
                FailEofParsingAt("", ampPos);
            FailAt(hex ? "Invalid syntax for a hexadecimal numeric entity reference." : "Invalid syntax for a decimal numeric entity reference.", p);
        }
        if (p >= len_)
            FailEofParsingAt("", ampPos);
        if (buf_[p] != u';')
            // A non-';' terminator after the digits is the SYNTAX form at
            // the terminator (not the unexpected-token form).
            FailAt(hex ? "Invalid syntax for a hexadecimal numeric entity reference." : "Invalid syntax for a decimal numeric entity reference.", p);
        ++p;
        if (value > 0xFFFF) {
            // The .NET surrogate-pair construction: the high half computed
            // even for overflowing values (the &#x110000; -> 0xDC00 quirk).
            std::uint32_t cp = static_cast<std::uint32_t>(value);
            std::uint32_t high = 0xD800 + ((cp - 0x10000) >> 10);
            if (cp > 0x10FFFF || high > 0xDBFF) {
                std::string display = InvalidCharDisplay(high, 0);
                FailAt("'" + display + "', hexadecimal value 0x" + HexX2(static_cast<std::int32_t>(high)) + ", is an invalid character.", digitsStart);
            }
            out.push_back(static_cast<char16_t>(high));
            out.push_back(static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF)));
        } else {
            std::uint32_t cp = static_cast<std::uint32_t>(value);
            if (!IsValidXmlCharCp(cp)) {
                std::string display = InvalidCharDisplay(cp, 0);
                FailAt("'" + display + "', hexadecimal value 0x" + HexX2(static_cast<std::int32_t>(cp)) + ", is an invalid character.", digitsStart);
            }
            out.push_back(static_cast<char16_t>(cp));
        }
        pos_ = p;
    }

    // Parses an entity/char reference at an '&' (pos_ is at the '&');
    // appends the expansion. In element content a markup-carrying entity
    // value splices as a nested parse context (see SpliceMarkupEntity).
    void ParseReference(std::u16string& out, bool inAttribute, XElement* element = nullptr)
    {
        (void)inAttribute;
        std::size_t ampPos = pos_;
        std::size_t p = ampPos + 1;
        if (p >= len_)
            FailAt("Unexpected end of file has occurred.", ampPos);
        if (buf_[p] == u'#') {
            ParseCharRef(out);
            return;
        }
        auto [cp, width] = CpAt(p);
        if (!IsStartNameChar(cp))
            FailAt("An error occurred while parsing EntityName.", p);
        std::u16string name;
        name.append(buf_, p, width);
        p += width;
        while (p < len_) {
            auto [c2, w2] = CpAt(p);
            if (IsNameChar(c2)) {
                name.append(buf_, p, w2);
                p += w2;
                continue;
            }
            break;
        }
        if (p >= len_)
            // EOF mid-name: the EntityName error at the NAME START (the
            // .NET ParseEntityName catch rethrows at the unconsumed
            // charPos).
            FailAt("An error occurred while parsing EntityName.", ampPos + 1);
        if (buf_[p] != u';')
            FailUnexpectedToken(p, ";");
        ++p;
        if (name == u"lt")
            out.push_back(u'<');
        else if (name == u"gt")
            out.push_back(u'>');
        else if (name == u"amp")
            out.push_back(u'&');
        else if (name == u"quot")
            out.push_back(u'"');
        else if (name == u"apos")
            out.push_back(u'\'');
        else {
            const EntityDecl* decl = LookupEntity(name);
            if (decl == nullptr)
                FailAt("Reference to undeclared entity '" + EncodeUtf8(name) + "'.", ampPos + 1);
            bool hasMarkup = false;
            for (char16_t v : decl->value) {
                if (v == u'<') {
                    hasMarkup = true;
                    break;
                }
            }
            if (hasMarkup && element != nullptr) {
                pos_ = p; // past the ';'
                SpliceMarkupEntity(*decl, *element, out);
            } else {
                AppendEntityValue(*decl, out);
            }
        }
        pos_ = p;
    }

    // The markup-carrying entity splice: an entity value containing '<'
    // parses as a nested content context (the .NET reader pushes the entity
    // value with entity-relative positions anchored at the declaration's
    // value quote). Structural nodes attach to the containing element; text
    // flows into the pending run. pos_ is at the ';'.
    void SpliceMarkupEntity(const EntityDecl& decl, XElement& element, std::u16string& run)
    {
        // Save the parser state and enter the entity context.
        std::u16string savedBuf;
        savedBuf.swap(buf_);
        std::size_t savedPos = pos_;
        std::size_t savedLen = len_;
        int savedLineNo = lineNo_;
        std::ptrdiff_t savedLineStartPos = lineStartPos_;
        buf_ = decl.value;
        len_ = buf_.size();
        pos_ = 0;
        lineNo_ = decl.declLineNo;
        lineStartPos_ = -decl.quoteLinePos - 1;
        bool savedInEntity = inEntity_;
        inEntity_ = true;
        ParseContentLoop(element, run);
        // Clean entity end: restore and continue in the main context.
        inEntity_ = savedInEntity;
        buf_.swap(savedBuf);
        pos_ = savedPos;
        len_ = savedLen;
        lineNo_ = savedLineNo;
        lineStartPos_ = savedLineStartPos;
    }

    // ---- quoted values ----
    // Scans a quoted value for an attribute / doctype id / entity
    // declaration (no normalization here: the callers normalize). Throws
    // the unclosed-literal EOF arm.
    std::u16string ScanQuoted()
    {
        char16_t quote = buf_[pos_];
        std::size_t quotePos = pos_;
        ++pos_;
        std::u16string value;
        while (true) {
            if (pos_ >= len_)
                FailEof("There is an unclosed literal string.");
            char16_t u = buf_[pos_];
            if (u == quote) {
                ++pos_;
                return value;
            }
            value.push_back(u);
            ++pos_;
        }
        (void)quotePos;
    }

    // The attribute value: normalization (literal whitespace -> space),
    // entity/char refs (unnormalized), the '<' attribute-character arm,
    // invalid chars, the unclosed-quote EOF.
    std::u16string ParseAttrValue(std::size_t quotePos)
    {
        (void)quotePos;
        char16_t quote = buf_[pos_];
        ++pos_;
        std::u16string value;
        while (true) {
            if (pos_ >= len_)
                FailEof("There is an unclosed literal string.");
            char16_t u = buf_[pos_];
            if (u == quote) {
                ++pos_;
                return value;
            }
            if (u == u'&') {
                ParseReference(value, true);
                continue;
            }
            if (u == u'<')
                FailAt("'<', hexadecimal value 0x3C, is an invalid attribute character.", pos_);
            auto [cp, width] = CpAt(pos_);
            if (cp >= 0x10000) {
                value.append(buf_, pos_, 2);
                pos_ += 2;
                continue;
            }
            if (u >= 0xD800 && u <= 0xDBFF) {
                // A lone high surrogate: reports at the NEXT char (the
                // comment/CDATA scan convention); at the buffer end the
                // unclosed-quote EOF arm fires at the quote-position.
                if (pos_ + 1 >= len_)
                    FailEof("There is an unclosed literal string.");
                FailInvalidChar(pos_ + 1);
            }
            if (!IsValidXmlCharCp(cp))
                FailInvalidChar(pos_);
            if (IsXmlWhitespaceCp(cp)) {
                // Literal whitespace normalizes to a space (\t and the
                // pre-normalized \n; \r cannot appear after the pre-pass).
                value.push_back(u' ');
                pos_ += width;
                continue;
            }
            value.append(buf_, pos_, width);
            pos_ += width;
        }
    }

    // ---- attributes ----
    // Parses one attribute (name, '=', quoted value). The duplicate check
    // and the namespace validations are the caller's (they need the parsed
    // list and the scope).
    AttrData ParseOneAttribute()
    {
        AttrData attr;
        attr.nameStart = pos_;
        ParsedQName name = ParseQName();
        attr.prefix = std::move(name.prefix);
        attr.local = std::move(name.local);
        SkipWs();
        if (pos_ >= len_)
            FailUnexpectedToken(pos_, "=");
        if (buf_[pos_] != u'=')
            FailUnexpectedToken(pos_, "=");
        ++pos_;
        SkipWs();
        if (pos_ >= len_)
            FailUnexpectedToken2(pos_, "\"", "'");
        if (buf_[pos_] != u'"' && buf_[pos_] != u'\'')
            FailUnexpectedToken2(pos_, "\"", "'");
        attr.quotePos = pos_;
        attr.value = ParseAttrValue(attr.quotePos);
        attr.valueStart = attr.quotePos + 1;
        return attr;
    }

    // ---- tree helpers ----
    std::string ResolvePrefix(const std::u16string& prefix, std::size_t nameStart)
    {
        if (prefix.empty())
            return std::string(); // the default namespace (attributes: no namespace)
        if (prefix == u"xml")
            return std::string("http://www.w3.org/XML/1998/namespace");
        for (auto scopeIt = nsStack_.rbegin(); scopeIt != nsStack_.rend(); ++scopeIt) {
            for (auto bindingIt = scopeIt->rbegin(); bindingIt != scopeIt->rend(); ++bindingIt) {
                if (bindingIt->prefix == prefix)
                    return EncodeUtf8(bindingIt->uri);
            }
        }
        FailAt("'" + EncodeUtf8(prefix) + "' is an undeclared prefix.", nameStart);
    }

    std::string DefaultNamespaceUri() const
    {
        for (auto scopeIt = nsStack_.rbegin(); scopeIt != nsStack_.rend(); ++scopeIt) {
            for (auto bindingIt = scopeIt->rbegin(); bindingIt != scopeIt->rend(); ++bindingIt) {
                if (bindingIt->prefix.empty())
                    return EncodeUtf8(bindingIt->uri);
            }
        }
        return std::string();
    }

    static std::string ElementUriFor(const std::u16string& prefix, const std::string& defaultUri)
    {
        if (prefix.empty())
            return defaultUri;
        if (prefix == u"xml")
            return std::string("http://www.w3.org/XML/1998/namespace");
        return std::string(); // unreachable (the resolver threw)
    }

    // ---- the xml declaration ----
    // pos_ at "<?xml"; requires the char after "xml" to not be a name char.
    void ParseXmlDeclaration()
    {
        pos_ += 5;
        int stage = 0; // 0 expecting version, 1 after version, 2 after encoding/standalone-eligible
        std::u16string version, encoding, standalone;
        bool hasVersion = false, hasEncoding = false, hasStandalone = false;
        while (true) {
            SkipWs();
            if (pos_ < len_ && buf_[pos_] == u'?') {
                if (pos_ + 1 < len_ && buf_[pos_ + 1] == u'>') {
                    pos_ += 2;
                    break;
                }
                if (pos_ + 1 >= len_)
                    FailEof("There is an unclosed literal string.");
                FailUnexpectedToken(pos_, "?>");
            }
            if (pos_ >= len_)
                FailEof("There is an unclosed literal string.");
            std::size_t attrNameStart = pos_;
            ParsedQName attrName = ParseQName();
            std::u16string attrRaw = attrName.Raw();
            bool isVersion = attrRaw == u"version";
            bool isEncoding = attrRaw == u"encoding";
            bool isStandalone = attrRaw == u"standalone";
            bool allowed = false;
            if (isVersion && stage == 0)
                allowed = true;
            else if (isEncoding && stage == 1)
                allowed = true;
            else if (isStandalone && (stage == 1 || stage == 2))
                allowed = true;
            if (!allowed)
                FailAt("Syntax for an XML declaration is invalid.", attrNameStart);
            SkipWs();
            if (pos_ >= len_ || buf_[pos_] != u'=') {
                if (pos_ >= len_)
                    FailUnexpectedToken(pos_, "=");
                FailUnexpectedToken(pos_, "=");
            }
            ++pos_;
            SkipWs();
            if (pos_ >= len_ || (buf_[pos_] != u'"' && buf_[pos_] != u'\'')) {
                if (pos_ >= len_)
                    FailUnexpectedToken2(pos_, "\"", "'");
                FailUnexpectedToken2(pos_, "\"", "'");
            }
            std::size_t quotePos = pos_;
            std::u16string value = ParseAttrValue(quotePos);
            std::size_t valueStart = quotePos + 1;
            if (isVersion) {
                // The version value must start with "1.0".
                if (value.size() < 3u || value.compare(0, 3u, u"1.0") != 0)
                    FailAt("Version number '" + EncodeUtf8(value) + "' is invalid.", valueStart);
                version = std::move(value);
                hasVersion = true;
                stage = 1;
            } else if (isEncoding) {
                encoding = std::move(value);
                hasEncoding = true;
                stage = 2;
            } else {
                if (value != u"yes" && value != u"no")
                    FailAt("Syntax for an XML declaration is invalid.", valueStart - 1);
                standalone = std::move(value);
                hasStandalone = true;
                stage = 3;
            }
        }
        if (!hasVersion)
            FailAt("Syntax for an XML declaration is invalid.", pos_ == 0 ? 0 : pos_ - 2);
        declaration_ = XDeclaration(
            EncodeUtf8(version),
            hasEncoding ? EncodeUtf8(encoding) : std::string(),
            hasStandalone ? EncodeUtf8(standalone) : std::string());
    }

    // ---- PIs / comments / CDATA ----
    // pos_ at the target start (after "<?").
    std::pair<std::u16string, std::u16string> ParsePIBody()
    {
        std::size_t targetStart = pos_;
        ParsedQName target = ParseQName();
        std::u16string targetRaw = target.Raw();
        bool isXml = targetRaw.size() == 3
            && (targetRaw[0] == u'x' || targetRaw[0] == u'X')
            && (targetRaw[1] == u'm' || targetRaw[1] == u'M')
            && (targetRaw[2] == u'l' || targetRaw[2] == u'L');
        if (isXml) {
            if (targetRaw == u"xml")
                FailAt("Unexpected XML declaration. The XML declaration must be the first node in the document, and no whitespace characters are allowed to appear before it.", targetStart);
            FailAt("'" + EncodeUtf8(targetRaw) + "' is an invalid name for processing instructions.", targetStart);
        }
        std::u16string data;
        if (pos_ >= len_) {
            // EOF right after the target (no '?' terminator).
            FailEofParsing("Name");
        }
        char16_t after = buf_[pos_];
        bool hadWs = false;
        {
            auto [cp, width] = CpAt(pos_);
            if (IsXmlWhitespaceCp(cp)) {
                hadWs = true;
                SkipWs();
            }
        }
        if (!hadWs) {
            if (after == u'?' && pos_ + 1 < len_ && buf_[pos_ + 1] == u'>') {
                pos_ += 2;
                return {targetRaw, data};
            }
            FailBadNameChar(pos_);
        }
        // Scan the data to '?>'.
        while (true) {
            if (pos_ >= len_)
                FailEofParsing("PI");
            char16_t u = buf_[pos_];
            if (u == u'?') {
                if (pos_ + 1 < len_ && buf_[pos_ + 1] == u'>') {
                    pos_ += 2;
                    return {targetRaw, data};
                }
                if (pos_ + 1 >= len_)
                    FailEofParsing("PI");
            }
            auto [cp2, width2] = CpAt(pos_);
            if (!IsValidXmlCharCp(cp2))
                FailInvalidChar(pos_);
            if (cp2 >= 0x10000) {
                data.append(buf_, pos_, 2);
                pos_ += 2;
                continue;
            }
            if (u >= 0xD800 && u <= 0xDFFF) {
                if (u <= 0xDBFF) {
                    if (pos_ + 1 >= len_)
                        FailEofParsing("PI");
                    FailInvalidChar(pos_ + 1);
                }
                FailInvalidChar(pos_);
            }
            if (cp2 == 0xA)
                OnNewLine(pos_ + 1);
            data.push_back(u);
            ++pos_;
        }
    }

    // pos_ at the content start (after "<!--"); returns the value.
    std::u16string ParseCommentBody()
    {
        std::size_t start = pos_;
        // The entry check: fewer than 3 characters left reports at the
        // entry position (the .NET ParseCDataOrComment top check).
        if (len_ - pos_ < 3)
            FailEofParsingAt("Comment", pos_);
        while (true) {
            if (pos_ >= len_)
                FailEofParsing("Comment");
            char16_t u = buf_[pos_];
            if (u == u'-') {
                if (pos_ + 1 < len_ && buf_[pos_ + 1] == u'-') {
                    if (pos_ + 2 < len_ && buf_[pos_ + 2] == u'>') {
                        std::u16string value(buf_, start, pos_ - start);
                        pos_ += 3;
                        return value;
                    }
                    if (pos_ + 2 >= len_)
                        FailEofParsingAt("Comment", pos_);
                    FailAt("An XML comment cannot contain '--', and '-' cannot be the last character.", pos_);
                }
                if (pos_ + 1 >= len_)
                    FailEofParsingAt("Comment", pos_);
                ++pos_;
                continue;
            }
            auto [cp, width] = CpAt(pos_);
            if (cp >= 0x10000) {
                pos_ += 2;
                continue;
            }
            if (u >= 0xD800 && u <= 0xDBFF) {
                // A lone high surrogate: EOF at the buffer end is the
                // comment EOF arm at the surrogate; otherwise the error
                // reports at the NEXT char.
                if (pos_ + 1 >= len_)
                    FailEofParsingAt("Comment", pos_);
                FailInvalidChar(pos_ + 1);
            }
            if (!IsValidXmlCharCp(cp))
                FailInvalidChar(pos_);
            if (cp == 0xA)
                OnNewLine(pos_ + 1);
            pos_ += width;
        }
    }

    // pos_ at the content start (after "<![CDATA["); returns the value.
    std::u16string ParseCDataBody()
    {
        std::size_t start = pos_;
        // The entry check (see ParseCommentBody).
        if (len_ - pos_ < 3)
            FailEofParsingAt("CDATA", pos_);
        while (true) {
            if (pos_ >= len_)
                FailEofParsing("CDATA");
            char16_t u = buf_[pos_];
            if (u == u']') {
                if (pos_ + 1 < len_ && buf_[pos_ + 1] == u']') {
                    if (pos_ + 2 < len_ && buf_[pos_ + 2] == u'>') {
                        std::u16string value(buf_, start, pos_ - start);
                        pos_ += 3;
                        return value;
                    }
                    if (pos_ + 2 >= len_)
                        FailEofParsingAt("CDATA", pos_);
                    // ']]' + non-'>' inside CDATA is legal (consume one ']').
                    ++pos_;
                    continue;
                }
                if (pos_ + 1 >= len_)
                    FailEofParsingAt("CDATA", pos_);
                ++pos_;
                continue;
            }
            auto [cp, width] = CpAt(pos_);
            if (cp >= 0x10000) {
                pos_ += 2;
                continue;
            }
            if (u >= 0xD800 && u <= 0xDBFF) {
                // See ParseCommentBody (the CDATA spelling).
                if (pos_ + 1 >= len_)
                    FailEofParsingAt("CDATA", pos_);
                FailInvalidChar(pos_ + 1);
            }
            if (!IsValidXmlCharCp(cp))
                FailInvalidChar(pos_);
            if (cp == 0xA)
                OnNewLine(pos_ + 1);
            pos_ += width;
        }
    }

    // ---- the DOCTYPE ----
    // pos_ at the '<' of "<!DOCTYPE".
    void ParseDoctype(std::size_t ltPos)
    {
        if (sawDoctype_)
            FailAt("Cannot have multiple DTDs.", ltPos);
        if (rootElementParsed_)
            FailAt("DTD must be defined before the document root element.", ltPos);
        bool savedInDoctype = inDoctype_;
        inDoctype_ = true;
        ParseDoctypeBody(ltPos);
        inDoctype_ = savedInDoctype;
    }

    void ParseDoctypeBody(std::size_t ltPos)
    {
        pos_ = ltPos + 9; // past "<!DOCTYPE"
        if (pos_ >= len_ || !IsXmlWhitespaceCp(buf_[pos_]))
            FailExpectingWhitespace(pos_);
        SkipWs();
        ParsedQName name = ParseQName();
        std::u16string doctypeName = name.local.empty() ? name.Raw() : name.Raw();
        SkipWs();
        std::u16string publicId, systemId;
        bool hasPublic = false, hasSystem = false;
        if (pos_ >= len_)
            FailEof("Incomplete DTD content.");
        char16_t c = buf_[pos_];
        if (c == u'P' || c == u'S') {
            const char16_t* keyword = c == u'P' ? u"PUBLIC" : u"SYSTEM";
            std::u16string kw;
            for (const char16_t* q = keyword; *q != 0; ++q)
                kw.push_back(*q);
            if (!StartsWith(pos_, keyword))
                FailUnexpectedToken(pos_, EncodeUtf8(kw));
            pos_ += kw.size();
            SkipWs();
            if (pos_ >= len_)
                FailEof("Incomplete DTD content.");
            // The keyword requires whitespace before the id.
            if (c == u'P') {
                // PUBLIC pubid sysid
                publicId = ScanQuoted();
                SkipWs();
                if (pos_ >= len_)
                    FailEof("Incomplete DTD content.");
                systemId = ScanQuoted();
                hasPublic = true;
            } else {
                systemId = ScanQuoted();
                hasSystem = true;
            }
            SkipWs();
        } else if (buf_[pos_] != u'[' && buf_[pos_] != u'>') {
            // The .NET junk arm: anything but PUBLIC/SYSTEM/'['/'>' between
            // the name and the ids/subset/close.
            FailAt("Expecting external ID, '[' or '>'.", pos_);
        }
        std::u16string internalSubset;
        if (pos_ >= len_)
            FailEof("Incomplete DTD content.");
        if (buf_[pos_] == u'[') {
            ++pos_;
            std::size_t subsetStart = pos_;
            ParseInternalSubset();
            internalSubset = std::u16string(buf_, subsetStart, pos_ - subsetStart); // up to (excluding) the ']'
            pos_++; // past ']'
            SkipWs();
        }
        if (pos_ >= len_)
            FailEof("Incomplete DTD content.");
        if (buf_[pos_] != u'>')
            FailUnexpectedToken(pos_, ">");
        ++pos_;
        sawDoctype_ = true;
        docType_ = std::make_shared<XDocumentType>(
            EncodeUtf8(doctypeName),
            hasPublic ? std::optional<std::string>(EncodeUtf8(publicId)) : std::nullopt,
            (hasPublic || hasSystem) ? std::optional<std::string>(EncodeUtf8(systemId)) : std::nullopt,
            std::optional<std::string>(EncodeUtf8(internalSubset)));
    }

    // pos_ just past '['; parses to the matching ']' (leaves pos_ AT the
    // ']').
    void ParseInternalSubset()
    {
        while (true) {
            SkipWs();
            if (pos_ >= len_)
                FailEof("Incomplete DTD content.");
            char16_t c = buf_[pos_];
            if (c == u']')
                return;
            if (c != u'<')
                FailAt("Expected DTD markup was not found.", pos_);
            // '<!--' comment / '<?pi' / '<!KEYWORD ...>'
            if (StartsWith(pos_, u"<!--")) {
                pos_ += 4;
                ParseCommentBody();
                continue;
            }
            if (StartsWith(pos_, u"<?")) {
                pos_ += 2;
                ParsePIBody();
                continue;
            }
            if (StartsWith(pos_, u"<!ENTITY")) {
                pos_ += 8;
                ParseEntityDecl();
                continue;
            }
            if (StartsWith(pos_, u"<!ELEMENT") || StartsWith(pos_, u"<!ATTLIST") || StartsWith(pos_, u"<!NOTATION")) {
                // Skipped declarations: scan to the closing '>' honoring
                // quotes.
                while (true) {
                    if (pos_ >= len_)
                        FailEof("Incomplete DTD content.");
                    char16_t u = buf_[pos_];
                    if (u == u'"' || u == u'\'') {
                        ScanQuoted();
                        continue;
                    }
                    if (u == u'>') {
                        ++pos_;
                        break;
                    }
                    if (u == u'\n')
                        OnNewLine(pos_ + 1);
                    ++pos_;
                }
                continue;
            }
            // Unknown '<!' markup.
            FailAt("Expected DTD markup was not found.", pos_);
        }
    }

    // pos_ just past "<!ENTITY".
    void ParseEntityDecl()
    {
        if (pos_ >= len_ || !IsXmlWhitespaceCp(buf_[pos_]))
            FailExpectingWhitespace(pos_);
        SkipWs();
        bool parameter = false;
        if (pos_ < len_ && buf_[pos_] == u'%') {
            parameter = true;
            ++pos_;
            SkipWs();
        }
        ParsedQName name = ParseQName();
        std::u16string entityName = name.Raw();
        SkipWs();
        if (pos_ >= len_)
            FailEof("Incomplete DTD content.");
        EntityDecl decl;
        if (buf_[pos_] == u'"' || buf_[pos_] == u'\'') {
            // The value is captured raw (refs expand at use); the quote
            // position anchors the entity-relative positions.
            std::size_t quotePos = pos_;
            int declLine = lineNo_;
            std::ptrdiff_t declQuoteLinePos = LinePos(quotePos);
            decl.value = ScanQuoted();
            decl.declLineNo = declLine;
            decl.quoteLinePos = declQuoteLinePos;
        } else if (buf_[pos_] == u'S' && StartsWith(pos_, u"SYSTEM")) {
            pos_ += 6;
            SkipWs();
            decl.external = true;
            if (pos_ < len_ && (buf_[pos_] == u'"' || buf_[pos_] == u'\''))
                ScanQuoted();
        } else if (buf_[pos_] == u'P' && StartsWith(pos_, u"PUBLIC")) {
            pos_ += 6;
            SkipWs();
            decl.external = true;
            if (pos_ < len_ && (buf_[pos_] == u'"' || buf_[pos_] == u'\''))
                ScanQuoted();
            SkipWs();
            if (pos_ < len_ && (buf_[pos_] == u'"' || buf_[pos_] == u'\''))
                ScanQuoted();
        } else {
            FailAt("Expecting whitespace.", pos_);
        }
        SkipWs();
        if (pos_ >= len_)
            FailEof("Incomplete DTD content.");
        if (buf_[pos_] != u'>')
            FailUnexpectedToken(pos_, ">");
        ++pos_;
        decl.parameter = parameter;
        // First declaration wins (silently, like the .NET dictionary).
        bool exists = false;
        for (const auto& entry : entities_) {
            if (entry.first == entityName) {
                exists = true;
                break;
            }
        }
        if (!exists)
            entities_.emplace_back(std::move(entityName), std::move(decl));
    }

    // ---- elements ----
    // The ']' check inside content text.
    void CheckCDataEndInText()
    {
        if (pos_ + 2 < len_ + 1 && pos_ + 1 < len_ && buf_[pos_ + 1] == u']' && pos_ + 2 < len_ && buf_[pos_ + 2] == u'>')
            FailAt("']]>' is not allowed in character data.", pos_);
    }

    // Flushes the pending text run into the element (dropping the
    // all-whitespace runs).
    static void FlushText(XContainer& container, std::u16string& run)
    {
        if (run.empty())
            return;
        bool allWhitespace = true;
        for (char16_t u : run) {
            if (u != u' ' && u != u'\t' && u != u'\n' && u != u'\r') {
                allWhitespace = false;
                break;
            }
        }
        if (!allWhitespace)
            container.Add(XContent(std::make_shared<XText>(EncodeUtf8(run))));
        run.clear();
    }

    // pos_ at the '<' of a start tag. Builds and returns the element (the
    // caller attaches it).
    std::shared_ptr<XElement> ParseElement()
    {
        std::size_t lt = pos_;
        pos_ = lt + 1;
        std::size_t nameStart = pos_;
        ParsedQName qname = ParseQName();
        int startLine = lineNo_;
        std::ptrdiff_t startPos = LinePos(nameStart);

        nsStack_.emplace_back();
        std::vector<AttrData> attrs;
        std::vector<NsBinding> bindings;
        bool isEmpty = false;
        bool tagClosed = false;
        // The post-name arms (no whitespace after the name): '>' opens the
        // content, '/' needs '>' directly (the ParseElement '/' arm --
        // errors AT the '/'), anything else is a bad name char.
        if (pos_ < len_ && !IsXmlWhitespaceCp(CpAt(pos_).first)) {
            char16_t after = buf_[pos_];
            if (after == u'>') {
                ++pos_;
                tagClosed = true;
            } else if (after == u'/') {
                if (pos_ + 1 < len_ && buf_[pos_ + 1] == u'>') {
                    pos_ += 2;
                    isEmpty = true;
                    tagClosed = true;
                } else if (pos_ + 1 >= len_) {
                    FailEofParsingAt(">", pos_);
                } else {
                    FailUnexpectedToken(pos_, ">");
                }
            } else {
                FailBadNameChar(pos_);
            }
        }
        bool afterValue = false;
        while (!tagClosed) {
            bool hadWs = pos_ < len_ && IsXmlWhitespaceCp(CpAt(pos_).first);
            bool skipped = hadWs;
            while (pos_ < len_ && IsXmlWhitespaceCp(CpAt(pos_).first)) {
                auto [wcp, ww] = CpAt(pos_);
                if (wcp == 0xA)
                    OnNewLine(pos_ + ww);
                pos_ += ww;
            }
            if (pos_ >= len_) {
                // EOF inside the start tag: the element is not pushed yet
                // (the unclosed list holds the OUTER elements only).
                FailUnclosedEof();
            }
            char16_t c = buf_[pos_];
            if (c == u'>') {
                ++pos_;
                break;
            }
            if (c == u'/') {
                if (pos_ + 1 < len_ && buf_[pos_ + 1] == u'>') {
                    pos_ += 2;
                    isEmpty = true;
                    break;
                }
                if (pos_ + 1 >= len_)
                    FailUnclosedEof();
                FailUnexpectedToken(pos_ + 1, ">");
            }
            if (!skipped && afterValue)
                FailExpectingWhitespace(pos_);
            // One attribute.
            AttrData attr = ParseOneAttribute();
            std::u16string rawName = attr.prefix.empty() ? attr.local : attr.prefix + u":" + attr.local;
            for (const auto& prev : attrs) {
                std::u16string prevRaw = prev.prefix.empty() ? prev.local : prev.prefix + u":" + prev.local;
                if (prevRaw == rawName)
                    FailAt("'" + EncodeUtf8(rawName) + "' is a duplicate attribute name.", attr.nameStart);
            }
            // The namespace-declaration validations (incremental, like the
            // .NET OnNamespaceDecl per attribute).
            bool isDefaultNsDecl = attr.prefix.empty() && attr.local == u"xmlns";
            bool isPrefixedNsDecl = attr.prefix == u"xmlns";
            if (isPrefixedNsDecl) {
                if (attr.value.empty())
                    FailAt("Invalid namespace declaration.", attr.quotePos);
                if (attr.local == u"xml") {
                    if (attr.value != u"http://www.w3.org/XML/1998/namespace")
                        FailAt("Prefix \"xml\" is reserved for use by XML and can be mapped only to namespace name \"http://www.w3.org/XML/1998/namespace\".", attr.nameStart);
                } else if (attr.value == u"http://www.w3.org/XML/1998/namespace") {
                    FailAt("Prefix '" + EncodeUtf8(attr.local) + "' cannot be mapped to namespace name reserved for \"xml\" or \"xmlns\".", attr.valueStart);
                }
                bindings.push_back(NsBinding{attr.local, attr.value});
            } else if (isDefaultNsDecl) {
                bindings.push_back(NsBinding{std::u16string(), attr.value});
            }
            attrs.push_back(std::move(attr));
            afterValue = true;
        }
        // Register the bindings in the element scope (innermost-last so an
        // identical rebind overwrites in the walk).
        nsStack_.back() = std::move(bindings);

        // Resolve the element name prefix (after all declarations).
        std::string elemUri;
        if (qname.prefix.empty()) {
            elemUri = DefaultNamespaceUri();
        } else if (qname.prefix == u"xml") {
            elemUri = "http://www.w3.org/XML/1998/namespace";
        } else {
            elemUri = ResolvePrefix(qname.prefix, nameStart);
        }
        auto element = std::make_shared<XElement>(
            XName::Get(EncodeUtf8(qname.local), elemUri));
        // Attach the attributes in document order.
        for (auto& attr : attrs) {
            std::string attrUri;
            if (attr.prefix.empty()) {
                attrUri = std::string();
            } else if (attr.prefix == u"xmlns") {
                attrUri = "http://www.w3.org/2000/xmlns/";
            } else if (attr.prefix == u"xml") {
                attrUri = "http://www.w3.org/XML/1998/namespace";
            } else {
                attrUri = ResolvePrefix(attr.prefix, attr.nameStart);
            }
            element->AppendAttributeSkipNotify(std::make_shared<XAttribute>(
                XName::Get(EncodeUtf8(attr.local), attrUri),
                EncodeUtf8(attr.value)));
        }

        if (isEmpty) {
            nsStack_.pop_back();
            return element;
        }

        openElements_.push_back(OpenElem{qname.Raw(), startLine, startPos});
        std::u16string contentRun;
        ParseContentLoop(*element, contentRun);
        nsStack_.pop_back();
        return element;
    }

    // pos_ just past the start tag's '>'; parses the content to the
    // matching end tag.
    // pos_ just past the start tag's '>'; parses the content to the
    // matching end tag (the main context) or to the entity buffer end (the
    // entity splice context). The text run is the CALLER's (the entity
    // splice shares it so entity text merges into the containing run).
    void ParseContentLoop(XElement& element, std::u16string& textRun)
    {
        std::size_t entityEntryDepth = openElements_.size();
        while (true) {
            if (pos_ >= len_) {
                if (inEntity_) {
                    if (openElements_.size() != entityEntryDepth)
                        FailAt("Incomplete entity contents.", len_);
                    // Clean entity end: the caller restores the parser state
                    // and the main parse continues.
                    FlushText(element, textRun);
                    return;
                }
                FailUnclosedEof();
            }
            char16_t c = buf_[pos_];
            if (c == u'<') {
                char16_t c2 = pos_ + 1 < len_ ? buf_[pos_ + 1] : 0;
                if (c2 == u'?') {
                    FlushText(element, textRun);
                    pos_ += 2;
                    auto [target, data] = ParsePIBody();
                    element.Add(XContent(std::make_shared<XProcessingInstruction>(
                        EncodeUtf8(target), EncodeUtf8(data))));
                    continue;
                }
                if (c2 == u'!') {
                    FlushText(element, textRun);
                    std::size_t bang = pos_ + 2;
                    if (StartsWith(bang, u"--")) {
                        pos_ = bang + 2;
                        std::u16string value = ParseCommentBody();
                        element.Add(XContent(std::make_shared<XComment>(EncodeUtf8(value))));
                        continue;
                    }
                    if (StartsWith(bang, u"[")) {
                        // <![CDATA[ (with the spelling check).
                        std::size_t bracket = bang + 1;
                        if (!StartsWith(bracket, u"CDATA["))
                            FailUnexpectedToken(bracket, "CDATA[");
                        pos_ = bracket + 6;
                        std::u16string value = ParseCDataBody();
                        element.Add(XContent(std::make_shared<XCData>(EncodeUtf8(value))));
                        continue;
                    }
                    if (StartsWith(bang, u"DOCTYPE"))
                        FailAt("Unexpected DTD declaration.", bang);
                    FailUnexpectedToken2(bang, "<!--", "<[CDATA[");
                }
                if (c2 == u'/') {
                    FlushText(element, textRun);
                    pos_ += 2;
                    // The end tag: the OPEN element's name must match the
                    // text (the .NET StartsWith-then-ThrowTagMismatch order);
                    // the actual-name parse (whose own EOF-in-name arm fires)
                    // names the mismatch message.
                    std::size_t endNameStart = pos_;
                    const OpenElem& top = openElements_.back();
                    const std::u16string& expected = top.rawName;
                    bool matched = pos_ + expected.size() <= len_;
                    if (matched) {
                        for (std::size_t i = 0; i < expected.size(); ++i) {
                            if (buf_[endNameStart + i] != expected[i]) {
                                matched = false;
                                break;
                            }
                        }
                        if (matched && pos_ + expected.size() < len_) {
                            auto [ncp, nw] = CpAt(pos_ + expected.size());
                            if (IsNameChar(ncp) || buf_[pos_ + expected.size()] == u':')
                                matched = false; // a longer name mismatches
                        }
                    }
                    if (!matched) {
                        pos_ = endNameStart;
                        ParsedQName endName = ParseQName();
                        FailAt("The '" + EncodeUtf8(top.rawName) + "' start tag on line " + std::to_string(top.startLine)
                            + " position " + std::to_string(top.startPos)
                            + " does not match the end tag of '" + EncodeUtf8(endName.Raw()) + "'.", endNameStart);
                    }
                    pos_ = endNameStart + expected.size();
                    SkipWs();
                    if (pos_ >= len_)
                        FailUnclosedEof();
                    if (buf_[pos_] != u'>')
                        FailUnexpectedToken(pos_, ">");
                    ++pos_;
                    openElements_.pop_back();
                    if (!inEntity_) {
                        // A childless element with an explicit end tag records
                        // the empty-string content (the C# EndElement arm) so
                        // it renders <a></a>, not the <a /> empty-element form.
                        if (element.FirstNode() == nullptr)
                            element.Add(XContent(std::string()));
                        return;
                    }
                    // In the entity context an end tag just continues the loop
                    // (the depth checks at the entity end fire the arms).
                    continue;
                }
                // A child element ('<' at the buffer end is the unclosed arm).
                if (pos_ + 1 >= len_)
                    FailUnclosedEof();
                FlushText(element, textRun);
                auto child = ParseElement();
                element.Add(XContent(std::move(child)));
                continue;
            }
            if (c == u'&') {
                ParseReference(textRun, false, &element);
                continue;
            }
            if (c == u']') {
                CheckCDataEndInText();
                textRun.push_back(u']');
                ++pos_;
                continue;
            }
            auto [cp, width] = CpAt(pos_);
            if (cp >= 0x10000) {
                // A surrogate pair (combined above): valid text.
                textRun.append(buf_, pos_, 2);
                pos_ += 2;
                continue;
            }
            if (c >= 0xD800 && c <= 0xDBFF) {
                // A lone high surrogate: the error reports at the NEXT char
                // (the .NET scan advances past the surrogate first); at the
                // buffer end it is the plain EOF arm at the surrogate.
                if (pos_ + 1 >= len_)
                    FailAt("Unexpected end of file has occurred.", pos_);
                FailInvalidChar(pos_ + 1);
            }
            if (!IsValidXmlCharCp(cp))
                FailInvalidChar(pos_);
            if (cp == 0xA)
                OnNewLine(pos_ + 1);
            textRun.append(buf_, pos_, width);
            pos_ += width;
        }
    }

    // ---- the root level ----
    // Handles one root-level markup construct; returns false at clean EOF
    // (the root element tracked separately).
    enum class RootResult {
        Eof,
        Element,
        Other,
    };

    RootResult ParseRootStep(bool& wasElement)
    {
        SkipWs();
        if (pos_ >= len_) {
            if (!rootElementParsed_)
                FailNoLineInfo("Root element is missing.");
            return RootResult::Eof;
        }
        char16_t c = buf_[pos_];
        if (c == u'<') {
            // The root guard: a '<' with fewer than 4 characters left.
            if (len_ - pos_ < 4)
                FailAt("Data at the root level is invalid.", pos_);
            char16_t c2 = pos_ + 1 < len_ ? buf_[pos_ + 1] : 0;
            if (c2 == u'?') {
                pos_ += 2;
                auto [target, data] = ParsePIBody();
                if (documentMode_)
                    (rootElementParsed_ ? rootAfter_ : rootBefore_).push_back(
                        std::make_shared<XProcessingInstruction>(EncodeUtf8(target), EncodeUtf8(data)));
                return RootResult::Other;
            }
            if (c2 == u'!') {
                std::size_t bang = pos_ + 2;
                if (StartsWith(bang, u"--")) {
                    pos_ = bang + 2;
                    std::u16string value = ParseCommentBody();
                    if (documentMode_)
                        (rootElementParsed_ ? rootAfter_ : rootBefore_).push_back(
                            std::make_shared<XComment>(EncodeUtf8(value)));
                    return RootResult::Other;
                }
                if (StartsWith(bang, u"[")) {
                    // CDATA at the root level is invalid.
                    FailAt("Data at the root level is invalid.", pos_);
                }
                // DOCTYPE (or the '<!X' fallback).
                if (len_ - bang < 8)
                    FailAt("Unexpected end of file while parsing DOCTYPE has occurred.", bang);
                if (!StartsWith(bang, u"DOCTYPE"))
                    FailUnexpectedToken(bang, "DOCTYPE");
                ParseDoctype(pos_);
                return RootResult::Other;
            }
            if (c2 == u'/') {
                // A stray end tag at the root level: the error is positioned
                // at the name start (past "</"), before any name parse.
                FailAt("Unexpected end tag.", pos_ + 2);
            }
            // An element.
            if (rootElementParsed_)
                FailAt("There are multiple root elements.", pos_ + 1);
            rootElementParsed_ = true;
            wasElement = true;
            return RootResult::Element;
        }
        if (c == u'&')
            FailAt("Data at the root level is invalid.", pos_);
        // Non-whitespace text at the root level (whitespace was skipped).
        FailAt("Data at the root level is invalid.", pos_);
    }

public:
    // The entry points.
    std::shared_ptr<XElement> RunElement(const std::string& text)
    {
        Init(text);
        // The xml declaration is only recognized at the reader's first
        // position (before any whitespace).
        if (len_ >= 6 && StartsWith(0, u"<?xml")) {
            auto [cp, width] = CpAt(5);
            if (!IsNameChar(cp))
                ParseXmlDeclaration();
        }
        std::shared_ptr<XElement> root;
        while (true) {
            bool wasElement = false;
            RootResult result = ParseRootStep(wasElement);
            if (result == RootResult::Eof)
                break;
            if (result == RootResult::Element) {
                root = ParseElement();
                continue;
            }
        }
        return root;
    }

    std::shared_ptr<XDocument> RunDocument(const std::string& text)
    {
        Init(text);
        if (len_ >= 6 && StartsWith(0, u"<?xml")) {
            auto [cp, width] = CpAt(5);
            if (!IsNameChar(cp))
                ParseXmlDeclaration();
        }
        auto document = std::make_shared<XDocument>();
        if (declaration_.has_value())
            document->Declaration(*declaration_);
        std::shared_ptr<XElement> root;
        while (true) {
            bool wasElement = false;
            RootResult result = ParseRootStep(wasElement);
            if (result == RootResult::Eof)
                break;
            if (result == RootResult::Element) {
                root = ParseElement();
                continue;
            }
        }
        if (docType_ != nullptr)
            document->Add(XContent(docType_));
        for (auto& node : rootBefore_)
            document->Add(XContent(node));
        if (root != nullptr)
            document->Add(XContent(root));
        for (auto& node : rootAfter_)
            document->Add(XContent(node));
        return document;
    }

private:
    void Init(const std::string& text)
    {
        std::u16string decoded = DecodeUtf8(text);
        buf_.clear();
        buf_.reserve(decoded.size());
        // The XML 1.0 line-ending pre-normalization (\r\n and \r -> \n).
        for (std::size_t i = 0; i < decoded.size(); ++i) {
            char16_t u = decoded[i];
            if (u == u'\r') {
                buf_.push_back(u'\n');
                if (i + 1 < decoded.size() && decoded[i + 1] == u'\n')
                    ++i;
                continue;
            }
            buf_.push_back(u);
        }
        len_ = buf_.size();
        pos_ = 0;
        lineNo_ = 1;
        lineStartPos_ = -1;
        rootElementParsed_ = false;
        sawDoctype_ = false;
        openElements_.clear();
        nsStack_.clear();
        entities_.clear();
        rootBefore_.clear();
        rootAfter_.clear();
        declaration_.reset();
        docType_.reset();
    }
};

} // namespace

std::shared_ptr<XElement> ParseElementText(const std::string& text)
{
    Parser parser(false);
    return parser.RunElement(text);
}

std::shared_ptr<XDocument> ParseDocumentText(const std::string& text)
{
    Parser parser(true);
    return parser.RunDocument(text);
}

std::shared_ptr<XElement> XElement::Parse(const std::string& text)
{
    return ParseElementText(text);
}

std::shared_ptr<XDocument> XDocument::Parse(const std::string& text)
{
    return ParseDocumentText(text);
}

} // namespace ILSpy::Decompiler::Xml
