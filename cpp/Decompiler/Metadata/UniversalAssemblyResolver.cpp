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

// The implementation of UniversalAssemblyResolver.hpp (the first sub-slice of
// ICSharpCode.Decompiler/Metadata/UniversalAssemblyResolver.cs: the enums and
// the ParseTargetFramework classifier).

#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"

#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The .NET `string.Trim()` (both ends) over the char.IsWhiteSpace set. The
// C# calls Trim() on the comma-separated tokens; a token consisting only of
// whitespace becomes the empty string.
std::string TrimString(std::string_view text) {
    std::u16string wide = Util::Utf8ToUtf16(text);
    std::size_t begin = 0;
    std::size_t end = wide.size();
    while (begin < end && Util::IsWhiteSpace(wide[begin])) begin++;
    while (end > begin && Util::IsWhiteSpace(wide[end - 1])) end--;
    if (begin == 0 && end == wide.size()) return std::string(text);
    return Util::Utf16ToUtf8(std::u16string_view(wide).substr(begin, end - begin));
}

// The .NET `string.ToUpperInvariant()` restricted to the units that can
// reach an ASCII letter: the ASCII block plus U+017F (long s), the only BMP
// unit whose invariant uppercase is an ASCII letter (probed unit-by-unit
// over the BMP -- the iteration-31 sanitizer finding). Every other unit
// uppercases to a non-ASCII unit and can never equal the pure-ASCII
// ".NETCOREAPP"-style keys, so leaving it untouched is exact for the
// comparison.
std::string ToUpperInvariantAscii(std::string_view text) {
    std::u16string wide = Util::Utf8ToUtf16(text);
    for (char16_t& c : wide) {
        if (c >= u'a' && c <= u'z') {
            c = static_cast<char16_t>(c - u'a' + u'A');
        } else if (c == u'\u017F') {
            c = u'S';
        }
    }
    return Util::Utf16ToUtf8(wide);
}

// The .NET `Version.TryParse(string, out Version)`: the string ctor's parse
// (the same NumberStyles.Integer per-component shape) with the ctor's
// exception family caught into false.
bool TryParseVersion(const std::string& text, TypeSystem::Version& result) {
    try {
        result = TypeSystem::Version(text);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    } catch (const std::out_of_range&) {
        return false;
    }
}

// The .NET `string.Split(',')` / `Split('=')`: every occurrence of the
// separator splits, and EMPTY entries are kept ("a,,b" -> ["a", "", "b"]).
std::vector<std::string> SplitOn(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t pos = text.find(separator, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
}

}  // namespace

ParsedTargetFramework ParseTargetFramework(const std::string& targetFramework) {
    if (targetFramework.empty())
        return {TargetFrameworkIdentifier::NETFramework, ZeroVersion()};

    std::vector<std::string> tokens = SplitOn(targetFramework, ',');
    TargetFrameworkIdentifier identifier;

    std::string head = ToUpperInvariantAscii(TrimString(tokens[0]));
    if (head == ".NETCOREAPP") {
        identifier = TargetFrameworkIdentifier::NETCoreApp;
    } else if (head == ".NETSTANDARD") {
        identifier = TargetFrameworkIdentifier::NETStandard;
    } else if (head == "SILVERLIGHT") {
        identifier = TargetFrameworkIdentifier::Silverlight;
    } else {
        identifier = TargetFrameworkIdentifier::NETFramework;
    }

    // The C# nullable local: null until a "Version" pair parses.
    std::optional<TypeSystem::Version> version;

    for (std::size_t i = 1; i < tokens.size(); i++) {
        std::vector<std::string> pair = SplitOn(TrimString(tokens[i]), '=');
        if (pair.size() != 2) continue;

        if (ToUpperInvariantAscii(TrimString(pair[0])) == "VERSION") {
            // The C# `pair[1].TrimStart('v', 'V', ' ', '\t')`: a char-set
            // prefix strip (not a whitespace trim).
            std::string value = TrimString(pair[1]);
            std::size_t start = 0;
            while (start < value.size()) {
                char c = value[start];
                if (c == 'v' || c == 'V' || c == ' ' || c == '\t') {
                    start++;
                } else {
                    break;
                }
            }
            std::string versionString = value.substr(start);

            TypeSystem::Version parsed;
            if (TryParseVersion(versionString, parsed)) {
                // The C# re-wrap: (Major, Minor, Build < 0 ? 0 : Build) --
                // the REVISION is dropped.
                version = TypeSystem::Version(parsed.Major, parsed.Minor,
                    parsed.Build < 0 ? 0 : parsed.Build);
            } else {
                version = std::nullopt;
            }
            // ".NET 5 or greater still use .NETCOREAPP as
            // TargetFrameworkAttribute value..."
            if (version && version->Major >= 5
                && identifier == TargetFrameworkIdentifier::NETCoreApp) {
                identifier = TargetFrameworkIdentifier::NET;
            }
        }
    }

    return {identifier, version.value_or(ZeroVersion())};
}

const TypeSystem::Version& ZeroVersion() {
    // The C# `internal static Version ZeroVersion = new Version(0, 0, 0, 0)`
    // -- a process-lifetime singleton.
    static const TypeSystem::Version zero(0, 0, 0, 0);
    return zero;
}

}  // namespace ILSpy::Decompiler::Metadata
