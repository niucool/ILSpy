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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `AssemblyNameInfo` (see the header for the port contract).
// The parser and the formatter below are straight ports of the decompiled
// System.Private.CoreLib 10.0.8 `System.Reflection.AssemblyNameParser` and
// `System.Reflection.AssemblyNameFormatter` -- the state machines are kept
// structurally identical to the decompiled code so every crafted-input quirk
// (the quoted-string terminator rules, the escape table, the ushort sentinel
// collision in the version components) survives the translation.

#include "Decompiler/Metadata/AssemblyNameInfo.hpp"

#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <algorithm>
#include <stdexcept>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The C# `private enum Token` -- the three token kinds the display-name
// grammar is built from. The numeric value 0 is the C# default(Token) the
// scanner yields on a failed read (an embedded NUL); the port keeps `Invalid`
// named so the failure paths read like the C# `token = (Token)0`.
enum class Token {
    Invalid = 0,
    Equals = 1,
    Comma = 2,
    String = 3,
    End = 4,
};

// The C# `private enum AttributeKind` -- the seen-set bits for
// `TryRecordNewSeen` (duplicate attributes are a parse failure; unknown
// attribute names are silently ignored).
enum class AttributeKind {
    Version = 1,
    Culture = 2,
    PublicKeyOrToken = 4,
    ProcessorArchitecture = 8,
    Retargetable = 0x10,
    ContentType = 0x20,
};

// The local `IsWhiteSpace` of AssemblyNameParser -- a FOUR-character set
// (tab, newline, carriage return, space), deliberately NOT the Unicode
// char.IsWhiteSpace the type-name parser's TrimStart uses: a NBSP inside an
// assembly-name token is content here, not whitespace.
bool ParserIsWhiteSpace(char16_t ch)
{
    switch (ch) {
        case u'\t':
        case u'\n':
        case u'\r':
        case u' ':
            return true;
        default:
            return false;
    }
}

// The `char.IsWhiteSpace`-driven two-sided trim (the C# `span.Trim()` inside
// AppendQuoted's quoting decision).
std::u16string_view Trim(std::u16string_view s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && Util::IsWhiteSpace(s[begin]))
        begin++;
    while (end > begin && Util::IsWhiteSpace(s[end - 1]))
        end--;
    return s.substr(begin, end - begin);
}

// The `IsAttribute` OrdinalIgnoreCase equality against an ASCII key. The
// ASCII upper-fold follows the repo's `StringComparer::OrdinalIgnoreCase`
// convention (the non-ASCII units whose invariant fold is an ASCII letter are
// a documented divergence -- unreachable with real assembly names).
bool IsAttribute(std::u16string_view candidate, std::u16string_view attributeKind)
{
    if (candidate.size() != attributeKind.size())
        return false;
    for (std::size_t i = 0; i < candidate.size(); i++) {
        char16_t c = candidate[i];
        char16_t k = attributeKind[i];
        if (c >= u'a' && c <= u'z')
            c = static_cast<char16_t>(c - u'a' + u'A');
        if (k >= u'a' && k <= u'z')
            k = static_cast<char16_t>(k - u'a' + u'A');
        if (c != k)
            return false;
    }
    return true;
}

// `ushort.TryParse(text, NumberStyles.None, InvariantInfo)` -- digits only:
// no sign, no whitespace, value 0..65535 (an empty or overlong text fails).
bool TryParseUShortNone(std::u16string_view text, std::uint16_t& value)
{
    if (text.empty())
        return false;
    std::uint32_t parsed = 0;
    for (char16_t c : text) {
        if (c < u'0' || c > u'9')
            return false;
        parsed = parsed * 10 + static_cast<std::uint32_t>(c - u'0');
        if (parsed > 0xFFFF)
            return false;
    }
    value = static_cast<std::uint16_t>(parsed);
    return true;
}

// `HexConverter.TryDecodeFromUtf16` -- strict ASCII hex, two units per byte.
bool TryDecodeHex(std::u16string_view text, std::vector<std::uint8_t>& bytes)
{
    bytes.assign(text.size() / 2, 0);
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        std::uint32_t hi;
        char16_t c = text[i];
        if (c >= u'0' && c <= u'9')
            hi = static_cast<std::uint32_t>(c - u'0');
        else if (c >= u'a' && c <= u'f')
            hi = static_cast<std::uint32_t>(c - u'a' + 10);
        else if (c >= u'A' && c <= u'F')
            hi = static_cast<std::uint32_t>(c - u'A' + 10);
        else
            return false;
        std::uint32_t lo;
        c = text[i + 1];
        if (c >= u'0' && c <= u'9')
            lo = static_cast<std::uint32_t>(c - u'0');
        else if (c >= u'a' && c <= u'f')
            lo = static_cast<std::uint32_t>(c - u'a' + 10);
        else if (c >= u'A' && c <= u'F')
            lo = static_cast<std::uint32_t>(c - u'A' + 10);
        else
            return false;
        bytes[i / 2] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return true;
}

std::string ToUtf8(std::u16string_view s)
{
    return Util::Utf16ToUtf8(s);
}

std::u16string ToUtf16(std::string_view s)
{
    return Util::Utf8ToUtf16(s);
}

char HexDigitLower(std::uint8_t value)
{
    return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

// `internal ref struct AssemblyNameParser` -- the display-name tokenizer. The
// port is a plain class over a UTF-16 view; `TryGetNextChar` keeps the exact
// C# tri-state (false = an embedded NUL failed the read; true with ch == 0 =
// end of input), which the token scanner's failure arms depend on.
class AssemblyNameParser {
public:
    explicit AssemblyNameParser(std::u16string_view input)
        : input_(input)
    {
    }

    // The `internal static bool TryParse(ReadOnlySpan<char>, ref AssemblyNameParts)`.
    bool TryParse(std::string& name, std::optional<TypeSystem::Version>& version,
                  std::optional<std::string>& cultureName, AssemblyNameFlags& flags,
                  std::optional<std::vector<std::uint8_t>>& publicKeyOrToken)
    {
        std::u16string tokenString;
        Token token;
        if (!TryGetNextToken(tokenString, token) || token != Token::String || tokenString.empty())
            return false;
        std::u16string name16 = tokenString;

        version = std::nullopt;
        cultureName = std::nullopt;
        publicKeyOrToken = std::nullopt;
        flags = AssemblyNameFlags::None;
        std::int32_t seenAttributes = 0;

        if (!TryGetNextToken(tokenString, token))
            return false;
        do {
            switch (token) {
                case Token::Invalid:
                    return false;
                case Token::Comma: {
                    std::u16string attributeName;
                    if (!TryGetNextToken(attributeName, token) || token != Token::String)
                        return false;
                    if (!TryGetNextToken(tokenString, token) || token != Token::Equals)
                        return false;
                    std::u16string attributeValue;
                    if (!TryGetNextToken(attributeValue, token) || token != Token::String)
                        return false;
                    if (attributeName.empty())
                        return false;
                    if (IsAttribute(attributeName, u"Version")) {
                        if (!TryRecordNewSeen(seenAttributes, AttributeKind::Version))
                            return false;
                        if (!TryParseVersion(attributeValue, version))
                            return false;
                    } else if (IsAttribute(attributeName, u"Culture")) {
                        if (!TryRecordNewSeen(seenAttributes, AttributeKind::Culture))
                            return false;
                        if (!TryParseCulture(attributeValue, cultureName))
                            return false;
                    } else if (IsAttribute(attributeName, u"PublicKeyToken")) {
                        if (!TryRecordNewSeen(seenAttributes, AttributeKind::PublicKeyOrToken))
                            return false;
                        if (!TryParsePKT(attributeValue, true, publicKeyOrToken))
                            return false;
                    } else if (IsAttribute(attributeName, u"PublicKey")) {
                        if (!TryRecordNewSeen(seenAttributes, AttributeKind::PublicKeyOrToken))
                            return false;
                        if (!TryParsePKT(attributeValue, false, publicKeyOrToken))
                            return false;
                        flags = static_cast<AssemblyNameFlags>(
                            static_cast<std::int32_t>(flags)
                            | static_cast<std::int32_t>(AssemblyNameFlags::PublicKey));
                    } else if (IsAttribute(attributeName, u"ProcessorArchitecture")) {
                        if (!TryRecordNewSeen(seenAttributes,
                                              AttributeKind::ProcessorArchitecture))
                            return false;
                        ProcessorArchitecture architecture;
                        if (!TryParseProcessorArchitecture(attributeValue, architecture))
                            return false;
                        flags = static_cast<AssemblyNameFlags>(
                            static_cast<std::int32_t>(flags)
                            | (static_cast<std::int32_t>(architecture) << 4));
                    } else if (IsAttribute(attributeName, u"Retargetable")) {
                        if (!TryRecordNewSeen(seenAttributes, AttributeKind::Retargetable))
                            return false;
                        if (EqualsIgnoreCase(attributeValue, u"Yes")) {
                            flags = static_cast<AssemblyNameFlags>(
                                static_cast<std::int32_t>(flags)
                                | static_cast<std::int32_t>(AssemblyNameFlags::Retargetable));
                        } else if (!EqualsIgnoreCase(attributeValue, u"No")) {
                            return false;
                        }
                    } else if (IsAttribute(attributeName, u"ContentType")) {
                        if (!TryRecordNewSeen(seenAttributes, AttributeKind::ContentType))
                            return false;
                        if (!EqualsIgnoreCase(attributeValue, u"WindowsRuntime"))
                            return false;
                        flags = static_cast<AssemblyNameFlags>(
                            static_cast<std::int32_t>(flags) | 0x200);
                    }
                    // Unknown attribute names fall through every IsAttribute arm
                    // and are silently ignored (the C# else-if chain has no
                    // final else); their values were already consumed.
                    break;
                }
                case Token::End:
                    name = ToUtf8(name16);
                    return true;
                default:
                    return false;
            }
        } while (TryGetNextToken(tokenString, token));
        return false;
    }

private:
    static bool TryRecordNewSeen(std::int32_t& seenAttributes, AttributeKind newAttribute)
    {
        std::int32_t bit = static_cast<std::int32_t>(newAttribute);
        if ((seenAttributes & bit) != 0)
            return false;
        seenAttributes |= bit;
        return true;
    }

    // `TryParseVersion` -- the ushort sentinel trick: the scratch array starts
    // as { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF } and a component equal to 65535
    // reads as "absent", so "1.2.65535" parses as 1.2, "1.2.3.65535" as
    // 1.2.3, and "1.2.65535.65535" as 1.2 (gold-pinned). The two mandatory
    // components collide with the sentinel and fail the parse.
    static bool TryParseVersion(std::u16string_view attributeValue,
                                std::optional<TypeSystem::Version>& version)
    {
        // `source.Split(destination, '.')` over a 5-slot buffer: the part
        // count is capped at 5 (a longer remainder stays inside part 5, which
        // the length check rejects before any parsing happens).
        std::vector<std::u16string_view> parts;
        std::size_t start = 0;
        for (std::size_t i = 0; i < attributeValue.size() && parts.size() < 5; i++) {
            if (attributeValue[i] == u'.') {
                parts.push_back(attributeValue.substr(start, i - start));
                start = i + 1;
            }
        }
        if (parts.size() < 5)
            parts.push_back(attributeValue.substr(start));
        std::size_t length = parts.size();
        if (length < 2 || length > 4)
            return false;

        std::uint16_t components[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
        for (std::size_t i = 0; i < length; i++) {
            if (!TryParseUShortNone(parts[i], components[i]))
                return false;
        }
        if (components[0] == 0xFFFF || components[1] == 0xFFFF)
            return false;
        if (components[2] == 0xFFFF) {
            version = TypeSystem::Version(components[0], components[1]);
        } else if (components[3] == 0xFFFF) {
            version = TypeSystem::Version(components[0], components[1], components[2]);
        } else {
            version = TypeSystem::Version(components[0], components[1], components[2],
                                           components[3]);
        }
        return true;
    }

    static bool TryParseCulture(std::u16string_view attributeValue,
                                std::optional<std::string>& result)
    {
        if (EqualsIgnoreCase(attributeValue, u"Neutral")) {
            result = std::string();
            return true;
        }
        result = ToUtf8(attributeValue);
        return true;
    }

    // `TryParsePKT` -- the explicit `null`/empty spellings yield the EMPTY
    // byte array (a distinct state from "no key attribute given": the former
    // renders `PublicKeyToken=null`, the latter renders no segment at all).
    // A token must be exactly 16 hex units (8 bytes); a public key may be any
    // even count.
    static bool TryParsePKT(std::u16string_view attributeValue, bool isToken,
                            std::optional<std::vector<std::uint8_t>>& result)
    {
        if (EqualsIgnoreCase(attributeValue, u"null") || attributeValue.empty()) {
            result = std::vector<std::uint8_t>();
            return true;
        }
        if (attributeValue.size() % 2 != 0 || (isToken && attributeValue.size() != 16)) {
            result = std::nullopt;
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!TryDecodeHex(attributeValue, bytes)) {
            result = std::nullopt;
            return false;
        }
        result = std::move(bytes);
        return true;
    }

    static bool TryParseProcessorArchitecture(std::u16string_view attributeValue,
                                               ProcessorArchitecture& result)
    {
        ProcessorArchitecture architecture;
        if (EqualsIgnoreCase(attributeValue, u"msil"))
            architecture = ProcessorArchitecture::MSIL;
        else if (EqualsIgnoreCase(attributeValue, u"x86"))
            architecture = ProcessorArchitecture::X86;
        else if (EqualsIgnoreCase(attributeValue, u"ia64"))
            architecture = ProcessorArchitecture::IA64;
        else if (EqualsIgnoreCase(attributeValue, u"amd64"))
            architecture = ProcessorArchitecture::Amd64;
        else if (EqualsIgnoreCase(attributeValue, u"arm"))
            architecture = ProcessorArchitecture::Arm;
        else
            architecture = ProcessorArchitecture::None;
        result = architecture;
        return architecture != ProcessorArchitecture::None;
    }

    static bool EqualsIgnoreCase(std::u16string_view a, std::u16string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); i++) {
            char16_t ca = a[i];
            char16_t cb = b[i];
            if (ca >= u'a' && ca <= u'z')
                ca = static_cast<char16_t>(ca - u'a' + u'A');
            if (cb >= u'a' && cb <= u'z')
                cb = static_cast<char16_t>(cb - u'a' + u'A');
            if (ca != cb)
                return false;
        }
        return true;
    }

    bool TryGetNextChar(char16_t& ch)
    {
        if (index_ < input_.size()) {
            ch = input_[index_++];
            if (ch == 0)
                return false;
        } else {
            ch = 0;
        }
        return true;
    }

    // The `TryGetNextToken` state machine, kept structurally identical to the
    // decompiled scanner: leading whitespace skip (the four-char set), the
    // ','/'='/end short-circuits, the quote-character modes (an unquoted
    // '"'/'\'' FAILS the token; a quoted token ends at its matching quote
    // with no trailing trim), the backslash escapes (the five escapable
    // punctuation, the \t/\r/\n controls, anything else fails), and the
    // unquoted terminator (','/'=' unconsumed, trailing whitespace trimmed).
    bool TryGetNextToken(std::u16string& tokenString, Token& token)
    {
        tokenString.clear();
        char16_t ch;
        do {
            if (!TryGetNextChar(ch)) {
                token = Token::Invalid;
                return false;
            }
            switch (ch) {
                case u',':
                    token = Token::Comma;
                    return true;
                case u'=':
                    token = Token::Equals;
                    return true;
                case 0:
                    token = Token::End;
                    return true;
                default:
                    break;
            }
        } while (ParserIsWhiteSpace(ch));

        char16_t quote = 0;
        if (ch == u'"' || ch == u'\'') {
            quote = ch;
            if (!TryGetNextChar(ch)) {
                token = Token::Invalid;
                return false;
            }
        }

        std::u16string value;
        while (true) {
            if (ch == 0) {
                if (quote != 0) {
                    // Unterminated quote at the end of input.
                    token = Token::Invalid;
                    tokenString.clear();
                    return false;
                }
                // End of input in unquoted mode: fall through to the
                // completion below.
            } else if (quote == 0 || ch != quote) {
                if (quote == 0 && (ch == u',' || ch == u'=')) {
                    index_--; // unconsume the terminator
                    break;
                }
                if (quote == 0 && (ch == u'"' || ch == u'\'')) {
                    token = Token::Invalid;
                    tokenString.clear();
                    return false;
                }
                if (ch == u'\\') {
                    if (!TryGetNextChar(ch)) {
                        token = Token::Invalid;
                        tokenString.clear();
                        return false;
                    }
                    switch (ch) {
                        case u'"':
                        case u'\'':
                        case u',':
                        case u'=':
                        case u'\\':
                            value.push_back(ch);
                            break;
                        case u't':
                            value.push_back(u'\t');
                            break;
                        case u'r':
                            value.push_back(u'\r');
                            break;
                        case u'n':
                            value.push_back(u'\n');
                            break;
                        default:
                            token = Token::Invalid;
                            tokenString.clear();
                            return false;
                    }
                    if (!TryGetNextChar(ch)) {
                        token = Token::Invalid;
                        tokenString.clear();
                        return false;
                    }
                    continue;
                }
                value.push_back(ch);
                if (!TryGetNextChar(ch)) {
                    token = Token::Invalid;
                    tokenString.clear();
                    return false;
                }
                continue;
            }
            // A quoted token reaching its matching quote: the content ends
            // with NO trailing-whitespace trim.
            break;
        }

        std::size_t length = value.size();
        if (quote == 0) {
            while (length > 0 && ParserIsWhiteSpace(value[length - 1]))
                length--;
        }
        tokenString = value.substr(0, length);
        token = Token::String;
        return true;
    }

    std::u16string_view input_;
    std::size_t index_ = 0;
};

// `AssemblyNameFormatter.AppendQuoted` -- escape every '"', '\'', ',', '=' and
// '\\'; render the \t/\r/\n controls through their escapes; wrap the whole in
// double quotes only when the name has leading/trailing Unicode whitespace or
// contains a quote character. The escaping ALWAYS runs (an input
// "My,Name" re-renders escaped as My\,Name -- gold-pinned).
void AppendQuoted(std::u16string& vsb, std::u16string_view s)
{
    bool needsQuotes = s.size() != Trim(s).size();
    for (char16_t c : s) {
        if (c == u'"' || c == u'\'') {
            needsQuotes = true;
            break;
        }
    }
    if (needsQuotes)
        vsb.push_back(u'"');
    for (char16_t c : s) {
        switch (c) {
            case u'"':
            case u'\'':
            case u',':
            case u'=':
            case u'\\':
                vsb.push_back(u'\\');
                vsb.push_back(c);
                continue;
            case u'\t':
                vsb.push_back(u'\\');
                vsb.push_back(u't');
                continue;
            case u'\r':
                vsb.push_back(u'\\');
                vsb.push_back(u'r');
                continue;
            case u'\n':
                vsb.push_back(u'\\');
                vsb.push_back(u'n');
                continue;
            default:
                vsb.push_back(c);
                break;
        }
    }
    if (needsQuotes)
        vsb.push_back(u'"');
}

void AppendU16(std::u16string& vsb, std::uint32_t value)
{
    char digits[10];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    for (int i = n - 1; i >= 0; i--)
        vsb.push_back(static_cast<char16_t>(digits[i]));
}

// `AssemblyNameFormatter.AppendDisplayName` -- the fixed segment order
// (Version, Culture, PublicKeyToken/PublicKey, Retargetable, ContentType)
// regardless of the input order; the ushort-cast sentinel checks that drop
// each unspecified trailing version component; "neutral" for an empty
// culture; "null" for an empty key; lowercase hex for the bytes. The
// ProcessorArchitecture bits never render (a parse-only field).
void AppendDisplayName(std::u16string& vsb, std::u16string_view name,
                       const std::optional<TypeSystem::Version>& version,
                       const std::optional<std::string>& cultureName,
                       const std::vector<std::uint8_t>* pkt,
                       AssemblyNameFlags flags, AssemblyContentType contentType,
                       const std::vector<std::uint8_t>* pk)
{
    AppendQuoted(vsb, name);
    if (version.has_value()) {
        auto component = [](int value) -> std::uint16_t {
            return static_cast<std::uint16_t>(static_cast<unsigned int>(value) & 0xFFFF);
        };
        std::uint16_t major = component(version->Major);
        if (major != 0xFFFF) {
            vsb += u", Version=";
            AppendU16(vsb, major);
            std::uint16_t minor = component(version->Minor);
            if (minor != 0xFFFF) {
                vsb.push_back(u'.');
                AppendU16(vsb, minor);
                std::uint16_t build = component(version->Build);
                if (build != 0xFFFF) {
                    vsb.push_back(u'.');
                    AppendU16(vsb, build);
                    std::uint16_t revision = component(version->Revision);
                    if (revision != 0xFFFF) {
                        vsb.push_back(u'.');
                        AppendU16(vsb, revision);
                    }
                }
            }
        }
    }
    if (cultureName.has_value()) {
        std::u16string culture = ToUtf16(*cultureName);
        if (culture.empty())
            culture = u"neutral";
        vsb += u", Culture=";
        AppendQuoted(vsb, culture);
    }
    const std::vector<std::uint8_t>* array = pkt != nullptr ? pkt : pk;
    if (array != nullptr) {
        if (pkt != nullptr) {
            if (array->size() > 8)
                throw std::invalid_argument("Value does not fall within the expected range.");
            vsb += u", PublicKeyToken=";
        } else {
            vsb += u", PublicKey=";
        }
        if (array->empty()) {
            vsb += u"null";
        } else {
            for (std::uint8_t byte : *array) {
                vsb.push_back(static_cast<char16_t>(HexDigitLower(byte >> 4)));
                vsb.push_back(static_cast<char16_t>(HexDigitLower(byte & 0xF)));
            }
        }
    }
    if ((static_cast<std::int32_t>(flags)
         & static_cast<std::int32_t>(AssemblyNameFlags::Retargetable)) != 0) {
        vsb += u", Retargetable=Yes";
    }
    if (contentType == AssemblyContentType::WindowsRuntime) {
        vsb += u", ContentType=WindowsRuntime";
    }
}

// `AssemblyNameInfo.ExtractAssemblyNameFlags` -- `combinedFlags & -3825`
// (0xFFFFF10F): strips the PublicKey bit, the ProcessorArchitecture bits
// (4-6), bit 7 and the ContentType bits (9-11), keeping Retargetable and
// any high bits. Only the Retargetable bit is ever read downstream.
// (`ExtractProcessorArchitecture` -- bits 4-6 -- has no ported consumer until
// `ToAssemblyName` lands and stays unported with it.)
AssemblyNameFlags ExtractAssemblyNameFlags(AssemblyNameFlags combinedFlags)
{
    return static_cast<AssemblyNameFlags>(
        static_cast<std::int32_t>(combinedFlags) & (-3825));
}

// `AssemblyNameInfo.ExtractAssemblyContentType` -- bits 9-11.
AssemblyContentType ExtractAssemblyContentType(AssemblyNameFlags flags)
{
    return static_cast<AssemblyContentType>(
        (static_cast<std::int32_t>(flags) >> 9) & 7);
}

} // namespace

AssemblyNameInfo::AssemblyNameInfo(std::string name,
                                   std::optional<TypeSystem::Version> version,
                                   std::optional<std::string> cultureName,
                                   AssemblyNameFlags flags,
                                   std::optional<std::vector<std::uint8_t>> publicKeyOrToken)
    : name_(std::move(name)),
      version_(std::move(version)),
      cultureName_(std::move(cultureName)),
      flags_(flags),
      publicKeyOrToken_(std::move(publicKeyOrToken))
{
    if (name_.empty()) {
        // The C# ArgumentNullException for a null name; the port's UTF-8
        // boundary has no null string, and no ported caller reaches the arm.
        throw std::invalid_argument("Value cannot be null. (Parameter 'name')");
    }
}

const std::string& AssemblyNameInfo::FullName() const
{
    if (!fullNameComputed_) {
        std::u16string builder;
        AppendFullName(builder);
        fullName_ = ToUtf8(builder);
        fullNameComputed_ = true;
    }
    return fullName_;
}

std::shared_ptr<AssemblyNameInfo> AssemblyNameInfo::Parse(std::string_view assemblyName)
{
    std::shared_ptr<AssemblyNameInfo> result;
    if (!TryParse(assemblyName, result))
        throw std::invalid_argument(
            "The given assembly name was invalid. (Parameter 'assemblyName')");
    return result;
}

bool AssemblyNameInfo::TryParse(std::string_view assemblyName,
                                std::shared_ptr<AssemblyNameInfo>& result)
{
    std::u16string input = ToUtf16(assemblyName);
    if (input.empty())
        return false;
    std::string name;
    std::optional<TypeSystem::Version> version;
    std::optional<std::string> cultureName;
    AssemblyNameFlags flags = AssemblyNameFlags::None;
    std::optional<std::vector<std::uint8_t>> publicKeyOrToken;
    AssemblyNameParser parser(input);
    if (!parser.TryParse(name, version, cultureName, flags, publicKeyOrToken))
        return false;
    result = std::make_shared<AssemblyNameInfo>(std::move(name), std::move(version),
                                                std::move(cultureName), flags,
                                                std::move(publicKeyOrToken));
    return true;
}

void AssemblyNameInfo::AppendFullName(std::u16string& builder) const
{
    if (fullNameComputed_) {
        builder += ToUtf16(fullName_);
        return;
    }
    bool isPublicKey = (static_cast<std::int32_t>(flags_)
                        & static_cast<std::int32_t>(AssemblyNameFlags::PublicKey)) != 0;
    const std::vector<std::uint8_t>* array = publicKeyOrToken_
                                                 ? &publicKeyOrToken_.value()
                                                 : nullptr;
    AppendDisplayName(builder, ToUtf16(name_), version_, cultureName_,
                      isPublicKey ? nullptr : array, ExtractAssemblyNameFlags(flags_),
                      ExtractAssemblyContentType(flags_), isPublicKey ? array : nullptr);
}

} // namespace ILSpy::Decompiler::Metadata
