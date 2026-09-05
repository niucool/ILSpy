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

// Implementation of `TypeName`/`TypeNameParseOptions` (see the header for the
// port contract). The parser below is a straight port of the decompiled
// System.Reflection.Metadata 10.0.8 `TypeNameParser` and
// `TypeNameParserHelpers` -- the state machines are kept structurally identical
// to the decompiled code so every crafted-input quirk (the escaped-delimiter
// scan, the two bracket forms, the decorator re-scan wrap, the dive counter)
// survives the translation.

#include "Decompiler/Metadata/TypeName.hpp"

#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <limits>
#include <stdexcept>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The internal rank/modifier constants (`TypeNameParserHelpers.SZArray`
// etc.) -- the `_rankOrModifier` encodings.
constexpr std::int32_t kSZArray = -1;
constexpr std::int32_t kPointer = -2;
constexpr std::int32_t kByRef = -3;

std::u16string ToUtf16(std::string_view s)
{
    return Util::Utf8ToUtf16(s);
}

std::string ToUtf8(std::u16string_view s)
{
    return Util::Utf16ToUtf8(s);
}

// `MemoryExtensions.TrimStart()` -- the char.IsWhiteSpace-driven trim.
std::u16string_view TrimStart(std::u16string_view s)
{
    std::size_t begin = 0;
    while (begin < s.size() && Util::IsWhiteSpace(s[begin]))
        begin++;
    return s.substr(begin);
}

// `MemoryExtensions.IndexOfAny(span, SearchValues.Create("[]&*,+\\"))` -- the
// first index of any end-of-type-name delimiter.
std::size_t IndexOfAnyDelimiter(std::u16string_view s)
{
    for (std::size_t i = 0; i < s.size(); i++) {
        char16_t c = s[i];
        if (c == u'[' || c == u']' || c == u'&' || c == u'*' || c == u',' || c == u'+'
            || c == u'\\')
            return i;
    }
    return std::u16string_view::npos;
}

// `IndexOfNamespaceDelimiter` -- the LAST '.' of the full name, stepping back
// over a doubled dot (an "A..B" full name yields the namespace "A").
std::ptrdiff_t IndexOfNamespaceDelimiter(std::u16string_view fullName)
{
    std::size_t num = fullName.rfind(u'.');
    std::ptrdiff_t result = num == std::u16string_view::npos
                                ? -1
                                : static_cast<std::ptrdiff_t>(num);
    if (result > 0 && fullName[static_cast<std::size_t>(result) - 1] == u'.')
        result--;
    return result;
}

// The type-name arm of `NeedsEscaping` -- the seven escapable characters.
bool NeedsEscaping(char16_t c)
{
    switch (c) {
        case u'&':
        case u'*':
        case u'+':
        case u',':
        case u'[':
        case u'\\':
        case u']':
            return true;
        default:
            return false;
    }
}

// The local `GetUnescapedOffset` of GetFullTypeNameLength: walk from the
// first delimiter (a backslash) skipping every complete escape, and stop at
// the next unescaped delimiter (or the end). A backslash not followed by an
// escapable character yields -1 (the caller's parse failure).
std::ptrdiff_t GetUnescapedOffset(std::u16string_view input, std::ptrdiff_t startOffset)
{
    std::size_t i = static_cast<std::size_t>(startOffset);
    while (i < input.size()) {
        char16_t c = input[i];
        if (c == u'\\') {
            i++;
            if (i == input.size() || !NeedsEscaping(input[i]))
                return -1;
            i++;
        } else if (NeedsEscaping(c)) {
            break;
        } else {
            i++;
        }
    }
    return static_cast<std::ptrdiff_t>(i);
}

// `TypeNameParserHelpers.GetFullTypeNameLength` -- the length of the next
// type-name segment: the first unescaped delimiter, the whole input when
// there is none, and -1 for a malformed escape (a backslash not followed by
// an escapable character). `isNestedType` reports whether the terminating
// delimiter is an unescaped '+' (an escaped '\\+' is transparent to the
// scan, so "A\\+B+C" splits at the SECOND '+').
std::ptrdiff_t GetFullTypeNameLength(std::u16string_view input, bool& isNestedType)
{
    isNestedType = false;
    std::size_t found = IndexOfAnyDelimiter(input);
    std::ptrdiff_t num;
    if (found == std::u16string_view::npos) {
        num = static_cast<std::ptrdiff_t>(input.size());
    } else {
        num = static_cast<std::ptrdiff_t>(found);
        if (input[found] == u'\\')
            num = GetUnescapedOffset(input, num);
    }
    isNestedType = num > 0 && num < static_cast<std::ptrdiff_t>(input.size())
                    && input[static_cast<std::size_t>(num)] == u'+';
    return num;
}

// `TryStripFirstCharAndTrailingSpaces` -- consume `value` when it is the next
// unit, then trim the leading whitespace (the Unicode set).
bool TryStripFirstCharAndTrailingSpaces(std::u16string_view& span, char16_t value)
{
    if (!span.empty() && span[0] == value) {
        span = TrimStart(span.substr(1));
        return true;
    }
    return false;
}

// `AppendRankOrModifierStringRepresentation` -- the decorator suffix: '&' for
// byref, '*' for pointer, "[]" for the SZ array, "[*]" for the rank-1
// variable-bound form, '[' + (rank-1) commas + ']' for the general array.
void AppendRankOrModifierStringRepresentation(std::int32_t rankOrModifier,
                                              std::u16string& builder)
{
    switch (rankOrModifier) {
        case kByRef:
            builder.push_back(u'&');
            return;
        case kPointer:
            builder.push_back(u'*');
            return;
        case kSZArray:
            builder += u"[]";
            return;
        case 1:
            builder += u"[*]";
            return;
        default:
            builder.push_back(u'[');
            builder.append(static_cast<std::size_t>(rankOrModifier - 1), u',');
            builder.push_back(u']');
            return;
    }
}

// `IsBeginningOfGenericArgs` -- recognize the start of a generic argument
// list and consume its opening bracket(s): '[[' starts a fully-qualified
// argument list (one that may carry per-argument assembly names), a lone '['
// followed by anything but '*', ',' or ']' starts a plain one. (The C#
// condition ends in `|| 1 == 0`, a constant-false disjunct; the effective
// test is the three-character exclusion.)
bool IsBeginningOfGenericArgs(std::u16string_view& span, bool& doubleBrackets)
{
    doubleBrackets = false;
    if (!span.empty() && span[0] == u'[') {
        std::u16string_view rest = TrimStart(span.substr(1));
        if (!rest.empty()) {
            if (rest[0] == u'[') {
                doubleBrackets = true;
                span = TrimStart(rest.substr(1));
                return true;
            }
            char16_t c = rest[0];
            if (c != u'*' && c != u',' && c != u']') {
                span = rest;
                return true;
            }
        }
    }
    return false;
}

// `IsMaxDepthExceeded`/`TryDive` -- the shared node budget: every constructed
// node (type names, nested segments, decorators, generic arguments and their
// constructed wrappers) consumes one unit of depth.
bool IsMaxDepthExceeded(const TypeNameParseOptions& options, std::int32_t depth)
{
    return depth > options.MaxNodes();
}

bool TryDive(const TypeNameParseOptions& options, std::int32_t& depth)
{
    depth++;
    return !IsMaxDepthExceeded(options, depth);
}

// `TryGetTypeNameInfo` -- consume one full type-name (its complete nested
// '+'-chain) and report the total consumed length plus the per-segment
// lengths the declaring-type chain is built from.
bool TryGetTypeNameInfo(const TypeNameParseOptions& options, std::u16string_view& input,
                        std::vector<std::int32_t>& nestedNameLengths, bool& hasNested,
                        std::int32_t& recursiveDepth, std::size_t& totalLength)
{
    totalLength = 0;
    bool isNestedType;
    do {
        std::ptrdiff_t fullTypeNameLength =
            GetFullTypeNameLength(input.substr(totalLength), isNestedType);
        if (fullTypeNameLength <= 0)
            return false;
        if (isNestedType) {
            if (!TryDive(options, recursiveDepth))
                return false;
            nestedNameLengths.push_back(static_cast<std::int32_t>(fullTypeNameLength));
            totalLength++;
        }
        totalLength += static_cast<std::size_t>(fullTypeNameLength);
    } while (isNestedType);
    hasNested = !nestedNameLengths.empty();
    return true;
}

// `TryParseNextDecorator` -- consume one decorator: '*' (pointer), '&'
// (byref), or an array bracket group ('[]' the SZ form, '[*]' the rank-1
// variable-bound form, '[,]' the rank-N form). On failure the input is
// restored to where it started.
bool TryParseNextDecorator(std::u16string_view& input, std::int32_t& rankOrModifier)
{
    std::u16string_view original = input;
    if (TryStripFirstCharAndTrailingSpaces(input, u'*')) {
        rankOrModifier = kPointer;
        return true;
    }
    if (TryStripFirstCharAndTrailingSpaces(input, u'&')) {
        rankOrModifier = kByRef;
        return true;
    }
    if (TryStripFirstCharAndTrailingSpaces(input, u'[')) {
        std::int32_t num = 1;
        bool starSeen = false;
        while (true) {
            if (TryStripFirstCharAndTrailingSpaces(input, u']')) {
                rankOrModifier = (num == 1 && !starSeen) ? kSZArray : num;
                return true;
            }
            if (starSeen)
                break;
            if (num == 1 && TryStripFirstCharAndTrailingSpaces(input, u'*')) {
                starSeen = true;
                continue;
            }
            if (!TryStripFirstCharAndTrailingSpaces(input, u','))
                break;
            if (num == std::numeric_limits<std::int32_t>::max())
                throw std::overflow_error("Arithmetic operation resulted in an overflow.");
            num++;
        }
    }
    input = original;
    rankOrModifier = 0;
    return false;
}

// `GetAssemblyNameCandidate` -- the assembly-name text runs to the first ']'
// that is not escaped by a preceding backslash (the simpler two-character
// escape scan of the assembly arm, distinct from the type-name arm).
std::u16string_view GetAssemblyNameCandidate(std::u16string_view input)
{
    std::size_t num = input.find(u']');
    if (num != std::u16string_view::npos && num > 0 && input[num - 1] == u'\\') {
        std::size_t i = num;
        while (i < input.size() && (input[i] != u']' || input[i - 1] == u'\\'))
            i++;
        num = i;
    }
    if (num != std::u16string_view::npos)
        return input.substr(0, num);
    return input;
}

// The exception helpers -- each carries the exact .NET message text (the
// ArgumentException/ArgumentOutOfRangeException formatting includes the
// parameter-name suffix; see the header's divergence note).
[[noreturn]] void ThrowArgumentException_InvalidTypeName(std::int32_t errorIndex)
{
    throw std::invalid_argument("The name of the type is invalid. (Parameter 'typeName@"
                                + std::to_string(errorIndex) + "')");
}

[[noreturn]] void ThrowInvalidOperation_MaxNodesExceeded(std::int32_t limit)
{
    throw std::runtime_error("Maximum node count of " + std::to_string(limit)
                             + " exceeded.");
}

[[noreturn]] void ThrowInvalidOperation_NotGenericType()
{
    throw std::runtime_error("This operation is only valid on generic types.");
}

[[noreturn]] void ThrowInvalidOperation_NotNestedType()
{
    throw std::runtime_error("This operation is only valid on nested types.");
}

[[noreturn]] void ThrowInvalidOperation_NoElement()
{
    throw std::runtime_error(
        "This operation is only valid on arrays, pointers and references.");
}

[[noreturn]] void ThrowInvalidOperation_HasToBeArrayClass()
{
    throw std::runtime_error("Must be an array type.");
}

[[noreturn]] void ThrowInvalidOperation_NestedTypeNamespace()
{
    throw std::runtime_error("Cannot retrieve the namespace of a nested type.");
}

[[noreturn]] void ThrowInvalidOperation_NotSimpleName(const std::string& fullName)
{
    throw std::runtime_error("'" + fullName + "' is not a simple TypeName.");
}

} // namespace

void TypeNameParseOptions::SetMaxNodes(std::int32_t value)
{
    // ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(value, 0, "value")
    // with the exact two-line .NET message.
    if (value <= 0) {
        throw std::out_of_range("value ('" + std::to_string(value)
                                + "') must be greater than '0'. (Parameter 'value')\nActual value was "
                                + std::to_string(value) + ".");
    }
    maxNodes_ = value;
}

// `internal ref struct TypeNameParser` -- the parse state machine over the
// remaining input. (Named-namespace rather than anonymous so the
// `friend class TypeNameParser` declaration in the header names this class.)
class TypeNameParser {
public:
    // The `internal static TypeName? Parse(ReadOnlySpan<char>, bool
    // throwOnError, TypeNameParseOptions?)` entry point.
    static std::shared_ptr<TypeName> Parse(std::u16string_view typeName, bool throwOnError,
                                           const TypeNameParseOptions* options)
    {
        std::u16string_view name = TrimStart(typeName);
        if (name.empty()) {
            if (throwOnError)
                ThrowArgumentException_InvalidTypeName(0);
            return nullptr;
        }
        std::int32_t recursiveDepth = 0;
        TypeNameParser parser(name, throwOnError, options);
        std::shared_ptr<TypeName> result =
            parser.ParseNextTypeName(true, recursiveDepth);
        if (result == nullptr || !parser.input_.empty()) {
            if (throwOnError) {
                if (IsMaxDepthExceeded(parser.options_, recursiveDepth))
                    ThrowInvalidOperation_MaxNodesExceeded(parser.options_.MaxNodes());
                ThrowArgumentException_InvalidTypeName(
                    static_cast<std::int32_t>(typeName.size() - parser.input_.size()));
            }
            return nullptr;
        }
        return result;
    }

private:
    TypeNameParser(std::u16string_view name, bool throwOnError,
                   const TypeNameParseOptions* options)
        : throwOnError_(throwOnError), options_(options != nullptr ? *options
                                                                    : TypeNameParseOptions()),
          input_(name)
    {
    }

    // The recursive body: one type name with its optional generic argument
    // list, decorator chain, assembly name and declaring-type chain. The
    // depth counter is shared across the whole parse (never decremented), so
    // it ends equal to the total node count the MaxNodes bound compares
    // against.
    std::shared_ptr<TypeName> ParseNextTypeName(bool allowFullyQualifiedName,
                                                 std::int32_t& recursiveDepth)
    {
        if (!TryDive(options_, recursiveDepth))
            return nullptr;
        std::vector<std::int32_t> nestedNameLengths;
        bool hasNested = false;
        std::size_t totalLength;
        if (!TryGetTypeNameInfo(options_, input_, nestedNameLengths, hasNested,
                                recursiveDepth, totalLength))
            return nullptr;
        std::u16string_view nameSpan = input_.substr(0, totalLength);
        input_ = input_.substr(totalLength);

        std::vector<std::shared_ptr<TypeName>> builder;
        bool haveBuilder = false;
        std::u16string_view savedInput = input_;
        bool doubleBrackets = false;
        if (IsBeginningOfGenericArgs(input_, doubleBrackets)) {
            while (true) {
                std::shared_ptr<TypeName> argumentType =
                    ParseNextTypeName(doubleBrackets, recursiveDepth);
                if (argumentType == nullptr)
                    return nullptr;
                if (doubleBrackets
                    && !TryStripFirstCharAndTrailingSpaces(input_, u']'))
                    return nullptr;
                builder.push_back(std::move(argumentType));
                haveBuilder = true;
                if (!TryStripFirstCharAndTrailingSpaces(input_, u','))
                    break;
                doubleBrackets = TryStripFirstCharAndTrailingSpaces(input_, u'[');
            }
            if (!TryStripFirstCharAndTrailingSpaces(input_, u']'))
                return nullptr;
        }
        if (!haveBuilder) {
            input_ = savedInput;
        } else if (!TryDive(options_, recursiveDepth)) {
            return nullptr;
        }

        // Consume the decorator chain once to count its nodes (the depth
        // budget), remembering only whether any decorator was present.
        std::int32_t num = 0;
        std::u16string_view decoratorInput = input_;
        std::int32_t rankOrModifier;
        while (TryParseNextDecorator(input_, rankOrModifier)) {
            if (!TryDive(options_, recursiveDepth))
                return nullptr;
            num = rankOrModifier;
        }

        std::shared_ptr<AssemblyNameInfo> assemblyName;
        if (allowFullyQualifiedName && !TryParseAssemblyName(assemblyName))
            return nullptr;

        std::u16string text(nameSpan);
        std::shared_ptr<TypeName> declaringType =
            hasNested ? GetDeclaringType(text, nestedNameLengths, assemblyName) : nullptr;
        std::shared_ptr<TypeName> result = std::shared_ptr<TypeName>(
            new TypeName(text, assemblyName, nullptr, declaringType,
                         std::vector<std::shared_ptr<TypeName>>()));
        if (haveBuilder) {
            result = std::shared_ptr<TypeName>(
                new TypeName(std::optional<std::u16string>(), assemblyName, result,
                             declaringType, std::move(builder)));
        }
        if (num != 0) {
            // Re-walk the saved decorator position, wrapping the name in a
            // decorated node per decorator (the first decorator becomes the
            // innermost element).
            std::int32_t rankOrModifier2;
            while (TryParseNextDecorator(decoratorInput, rankOrModifier2)) {
                result = std::shared_ptr<TypeName>(
                    new TypeName(std::optional<std::u16string>(), assemblyName, result,
                                 nullptr, std::vector<std::shared_ptr<TypeName>>(),
                                 rankOrModifier2));
            }
        }
        return result;
    }

    // `TryParseAssemblyName` -- after a ',' the assembly display name runs to
    // the first unescaped ']' (or the end) and parses through
    // AssemblyNameInfo.TryParse.
    bool TryParseAssemblyName(std::shared_ptr<AssemblyNameInfo>& assemblyName)
    {
        std::u16string_view inputString = input_;
        if (TryStripFirstCharAndTrailingSpaces(input_, u',')) {
            if (input_.empty()) {
                input_ = inputString;
                return false;
            }
            std::u16string_view assemblyNameCandidate = GetAssemblyNameCandidate(input_);
            if (!AssemblyNameInfo::TryParse(ToUtf8(assemblyNameCandidate), assemblyName))
                return false;
            input_ = input_.substr(assemblyNameCandidate.size());
            return true;
        }
        return true;
    }

    // `GetDeclaringType` -- build the declaring chain: every nested segment
    // link shares the WHOLE parsed full-name text and carries the cumulative
    // prefix length as its `_nestedNameLength` (the slice FullName and Name
    // render from).
    static std::shared_ptr<TypeName> GetDeclaringType(
        const std::u16string& fullTypeName, const std::vector<std::int32_t>& nestedNameLengths,
        const std::shared_ptr<AssemblyNameInfo>& assemblyName)
    {
        std::shared_ptr<TypeName> typeName;
        std::int32_t num = 0;
        for (std::int32_t nestedNameLength2 : nestedNameLengths) {
            std::int32_t nestedNameLength = num + nestedNameLength2;
            typeName = std::shared_ptr<TypeName>(
                new TypeName(std::optional<std::u16string>(fullTypeName), assemblyName,
                             nullptr, typeName, std::vector<std::shared_ptr<TypeName>>(),
                             0, nestedNameLength));
            num += nestedNameLength2 + 1;
        }
        return typeName;
    }

    bool throwOnError_;
    TypeNameParseOptions options_;
    std::u16string_view input_;
};

std::shared_ptr<TypeName> TypeName::Parse(std::string_view typeName,
                                          const TypeNameParseOptions* options)
{
    // The C# `Parse` is the throwing arm of the internal parser; every
    // failure raises (never a null return).
    std::u16string input = ToUtf16(typeName);
    return TypeNameParser::Parse(input, true, options);
}

bool TypeName::TryParse(std::string_view typeName, std::shared_ptr<TypeName>& result,
                        const TypeNameParseOptions* options)
{
    std::u16string input = ToUtf16(typeName);
    std::shared_ptr<TypeName> parsed = TypeNameParser::Parse(input, false, options);
    if (parsed == nullptr)
        return false;
    result = std::move(parsed);
    return true;
}

std::string TypeName::Unescape(std::string_view name)
{
    std::u16string input = ToUtf16(name);
    std::size_t num = input.find(u'\\');
    if (num == std::u16string::npos)
        return std::string(name);
    std::u16string vsb;
    vsb.reserve(input.size());
    vsb.append(input, 0, num);
    std::size_t pos = num;
    while (pos < input.size()) {
        char16_t c = input[pos++];
        if (c != u'\\' || pos == input.size()) {
            vsb.push_back(c);
        } else if (input[pos] == u'\\') {
            // A double backslash collapses to one; a backslash before any
            // other character is dropped (the C# writes neither branch's
            // append, consuming the escape pair).
            vsb.push_back(c);
            pos++;
        }
    }
    return ToUtf8(vsb);
}

bool TypeName::IsArray() const
{
    if (rankOrModifier_ != kSZArray)
        return rankOrModifier_ > 0;
    return true;
}

std::string TypeName::Name() const
{
    if (!name_.has_value()) {
        std::u16string builder;
        AppendName(builder);
        name_ = std::move(builder);
    }
    return ToUtf8(*name_);
}

std::string TypeName::Namespace() const
{
    if (!namespace_.has_value()) {
        const TypeName* typeName = this;
        while (!typeName->IsSimple())
            typeName = typeName->elementOrGenericType_.get();
        if (typeName->IsNested())
            ThrowInvalidOperation_NestedTypeNamespace();
        if (!typeName->namespace_.has_value()) {
            std::u16string_view fullName = typeName->fullName_.has_value()
                                               ? std::u16string_view(*typeName->fullName_)
                                               : std::u16string_view();
            if (typeName->nestedNameLength_ > 0
                && fullName.size() > static_cast<std::size_t>(typeName->nestedNameLength_))
                fullName = fullName.substr(0,
                                           static_cast<std::size_t>(
                                               typeName->nestedNameLength_));
            std::ptrdiff_t num = IndexOfNamespaceDelimiter(fullName);
            std::u16string computed;
            if (num >= 0)
                computed = fullName.substr(0, static_cast<std::size_t>(num));
            else
                computed = std::u16string();
            typeName->namespace_ = std::move(computed);
        }
        namespace_ = typeName->namespace_;
    }
    return ToUtf8(*namespace_);
}

std::string TypeName::FullName() const
{
    if (!fullName_.has_value()) {
        std::u16string builder;
        AppendFullName(builder);
        fullName_ = std::move(builder);
    } else if (nestedNameLength_ > 0
               && fullName_->size() > static_cast<std::size_t>(nestedNameLength_)) {
        *fullName_ = fullName_->substr(0, static_cast<std::size_t>(nestedNameLength_));
    }
    return ToUtf8(*fullName_);
}

std::string TypeName::AssemblyQualifiedName() const
{
    if (!assemblyQualifiedName_.has_value()) {
        if (fullName_.has_value() && assemblyName_ == nullptr) {
            std::string fullName = FullName();
            assemblyQualifiedName_ = ToUtf16(fullName);
        } else {
            std::u16string builder;
            AppendFullName(builder);
            if (assemblyName_ != nullptr) {
                builder += u", ";
                assemblyName_->AppendFullName(builder);
            }
            assemblyQualifiedName_ = std::move(builder);
            if (assemblyName_ == nullptr)
                fullName_ = *assemblyQualifiedName_;
        }
    }
    return ToUtf8(*assemblyQualifiedName_);
}

std::shared_ptr<TypeName> TypeName::DeclaringType() const
{
    if (declaringType_ == nullptr)
        ThrowInvalidOperation_NotNestedType();
    return declaringType_;
}

std::int32_t TypeName::GetArrayRank() const
{
    if (rankOrModifier_ != kSZArray && rankOrModifier_ <= 0)
        ThrowInvalidOperation_HasToBeArrayClass();
    if (rankOrModifier_ != kSZArray)
        return rankOrModifier_;
    return 1;
}

std::shared_ptr<TypeName> TypeName::GetElementType() const
{
    if (!IsArray() && !IsPointer() && !IsByRef())
        ThrowInvalidOperation_NoElement();
    return elementOrGenericType_;
}

std::shared_ptr<TypeName> TypeName::GetGenericTypeDefinition() const
{
    if (!IsConstructedGenericType())
        ThrowInvalidOperation_NotGenericType();
    return elementOrGenericType_;
}

std::int32_t TypeName::GetNodeCount() const
{
    std::int64_t num = 1;
    if (IsArray() || IsPointer() || IsByRef()) {
        num += GetElementType()->GetNodeCount();
    } else if (IsConstructedGenericType()) {
        num += GetGenericTypeDefinition()->GetNodeCount();
        for (const std::shared_ptr<TypeName>& argument : GetGenericArguments())
            num += argument->GetNodeCount();
    } else if (IsNested()) {
        num += DeclaringType()->GetNodeCount();
    }
    if (num > std::numeric_limits<std::int32_t>::max())
        throw std::overflow_error("Arithmetic operation resulted in an overflow.");
    return static_cast<std::int32_t>(num);
}

std::shared_ptr<TypeName> TypeName::WithAssemblyName(
    std::shared_ptr<AssemblyNameInfo> assemblyName) const
{
    if (!IsSimple())
        ThrowInvalidOperation_NotSimpleName(FullName());
    std::shared_ptr<TypeName> declaringType =
        IsNested() ? DeclaringType()->WithAssemblyName(assemblyName) : nullptr;
    return std::shared_ptr<TypeName>(
        new TypeName(fullName_, std::move(assemblyName), nullptr, std::move(declaringType),
                     std::vector<std::shared_ptr<TypeName>>(), 0, nestedNameLength_));
}

std::shared_ptr<TypeName> TypeName::MakeSZArrayTypeName() const
{
    return MakeElementTypeName(kSZArray);
}

std::shared_ptr<TypeName> TypeName::MakeArrayTypeName(std::int32_t rank) const
{
    if (rank > 0)
        return MakeElementTypeName(rank);
    throw std::out_of_range(
        "Specified argument was out of the range of valid values. (Parameter 'rank')");
}

std::shared_ptr<TypeName> TypeName::MakePointerTypeName() const
{
    return MakeElementTypeName(kPointer);
}

std::shared_ptr<TypeName> TypeName::MakeByRefTypeName() const
{
    return MakeElementTypeName(kByRef);
}

std::shared_ptr<TypeName> TypeName::MakeGenericTypeName(
    std::vector<std::shared_ptr<TypeName>> typeArguments) const
{
    if (!IsSimple())
        ThrowInvalidOperation_NotSimpleName(FullName());
    return std::shared_ptr<TypeName>(
        new TypeName(std::optional<std::u16string>(), assemblyName_,
                     std::const_pointer_cast<TypeName>(shared_from_this()), declaringType_,
                     std::move(typeArguments)));
}

std::shared_ptr<TypeName> TypeName::MakeElementTypeName(std::int32_t rankOrModifier) const
{
    return std::shared_ptr<TypeName>(
        new TypeName(std::optional<std::u16string>(), assemblyName_,
                     std::const_pointer_cast<TypeName>(shared_from_this()), nullptr,
                     std::vector<std::shared_ptr<TypeName>>(), rankOrModifier));
}

TypeName::TypeName(std::optional<std::u16string> fullName,
                   std::shared_ptr<AssemblyNameInfo> assemblyName,
                   std::shared_ptr<TypeName> elementOrGenericType,
                   std::shared_ptr<TypeName> declaringType,
                   std::vector<std::shared_ptr<TypeName>> genericArguments,
                   std::int32_t rankOrModifier, std::int32_t nestedNameLength)
    : fullName_(std::move(fullName)),
      assemblyName_(std::move(assemblyName)),
      elementOrGenericType_(std::move(elementOrGenericType)),
      declaringType_(std::move(declaringType)),
      genericArguments_(std::move(genericArguments)),
      rankOrModifier_(rankOrModifier),
      nestedNameLength_(nestedNameLength)
{
}

void TypeName::AppendFullName(std::u16string& builder) const
{
    if (!fullName_.has_value()) {
        if (IsConstructedGenericType()) {
            GetGenericTypeDefinition()->AppendFullName(builder);
            builder.push_back(u'[');
            for (const std::shared_ptr<TypeName>& genericArgument : GetGenericArguments()) {
                builder.push_back(u'[');
                genericArgument->AppendFullName(builder);
                if (genericArgument->assemblyName_ != nullptr) {
                    builder += u", ";
                    genericArgument->assemblyName_->AppendFullName(builder);
                }
                builder += u"],";
            }
            builder[builder.size() - 1] = u']';
        } else if (IsArray() || IsPointer() || IsByRef()) {
            GetElementType()->AppendFullName(builder);
            AppendRankOrModifierStringRepresentation(rankOrModifier_, builder);
        }
    } else if (nestedNameLength_ > 0
               && fullName_->size() > static_cast<std::size_t>(nestedNameLength_)) {
        builder.append(*fullName_, 0, static_cast<std::size_t>(nestedNameLength_));
    } else {
        builder += *fullName_;
    }
}

void TypeName::AppendName(std::u16string& builder) const
{
    if (IsConstructedGenericType()) {
        GetGenericTypeDefinition()->AppendName(builder);
        return;
    }
    if (IsPointer() || IsByRef() || IsArray()) {
        GetElementType()->AppendName(builder);
        AppendRankOrModifierStringRepresentation(rankOrModifier_, builder);
        return;
    }
    std::u16string_view span = *fullName_;
    if (nestedNameLength_ > 0)
        span = span.substr(0, static_cast<std::size_t>(nestedNameLength_));
    if (IsNested()) {
        span = span.substr(static_cast<std::size_t>(declaringType_->nestedNameLength_) + 1);
    } else {
        std::ptrdiff_t num = IndexOfNamespaceDelimiter(span);
        if (num >= 0)
            span = span.substr(static_cast<std::size_t>(num) + 1);
    }
    builder += span;
}

} // namespace ILSpy::Decompiler::Metadata
