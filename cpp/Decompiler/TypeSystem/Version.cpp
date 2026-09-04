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

// The `System.Version(String)` ctor and the `ToString(int fieldCount)` render,
// both decompiled from the .NET 10 runtime (`System.Version.ParseVersion`,
// `TryParseComponent`, `TryFormatCore`) and behavior-pinned against it (the
// C:\temp-probe\AnrProbe matrix). The two members serve
// `Metadata::AssemblyNameReference`: the ctor parses the `Version=...` component
// of an assembly full name, and `FullName` renders `ToString(fieldCount: 4)`.
//
// Exception mapping: `ArgumentException`/`FormatException` (the component-count
// and component-shape failures) port to `std::invalid_argument`;
// `OverflowException` (a component above int32) and `ArgumentOutOfRangeException`
// (a negative component) port to `std::out_of_range` -- each carrying the exact
// probed .NET message.

#include "Decompiler/TypeSystem/Version.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// The `Number.ParseBinaryInteger<int>(component, NumberStyles.Integer, Invariant)`
// behind `TryParseComponent`: leading/trailing whitespace (the char.IsWhiteSpace
// units -- the string.Trim() set) and an optional leading `+`/`-` sign, then one
// or more decimal digits. Anything else is the FormatException quoting the RAW
// component; a magnitude outside int32 is the OverflowException. The component
// decodes to UTF-16 first (the .NET string form) so the whitespace set matches
// char.IsWhiteSpace over code units exactly.
int ParseVersionComponent(const std::string& component, const char* componentName)
{
    std::u16string u16 = Util::Utf8ToUtf16(component);
    std::size_t begin = 0;
    std::size_t end = u16.size();
    while (begin < end && Util::IsWhiteSpace(u16[begin]))
        begin++;
    while (end > begin && Util::IsWhiteSpace(u16[end - 1]))
        end--;
    bool negative = false;
    if (begin < end && (u16[begin] == u'+' || u16[begin] == u'-')) {
        negative = u16[begin] == u'-';
        begin++;
    }
    if (begin == end)
        throw std::invalid_argument(
            "The input string '" + component + "' was not in a correct format.");
    std::int64_t value = 0;
    for (std::size_t i = begin; i < end; i++) {
        char16_t c = u16[i];
        if (c < u'0' || c > u'9')
            throw std::invalid_argument(
                "The input string '" + component + "' was not in a correct format.");
        value = value * 10 + (c - '0');
        // The OverflowException fires as soon as the magnitude cannot fit a signed
        // int32 (the .NET parser fails on the first component that overflows; a
        // negative zero is parsed as 0 and passes the non-negative check below).
        if (value > (negative ? INT64_C(2147483648) : INT64_C(2147483647)))
            throw std::out_of_range("Value was either too large or too small for an Int32.");
    }
    int parsed = negative ? static_cast<int>(-value) : static_cast<int>(value);
    // ArgumentOutOfRangeException.ThrowIfNegative(parsed, componentName):
    // "<name> ('<value>') must be a non-negative value. (Parameter '<name>')"
    // followed by "Actual value was <value>." on the next line.
    if (parsed < 0)
        throw std::out_of_range(
            std::string(componentName) + " ('" + std::to_string(parsed)
            + "') must be a non-negative value. (Parameter '" + componentName + "')\n"
            + "Actual value was " + std::to_string(parsed) + ".");
    return parsed;
}

} // namespace

// `System.Version.ParseVersion` decompiled: find the '.' separators; no '.' at
// all, or a fourth one, is the "too short or too long" ArgumentException; the
// components in order are named "input", "input", "build", "revision" (the two
// ArgumentOutOfRangeException parameter names a negative component reports).
Version::Version(const std::string& version)
{
    std::size_t num = version.find('.');
    if (num == std::string::npos)
        throw std::invalid_argument(
            "Version string portion was too short or too long. (Parameter 'input')");
    std::size_t num3 = std::string::npos;
    std::size_t num2 = std::string::npos;
    std::size_t rel = version.find('.', num + 1);
    if (rel != std::string::npos) {
        num3 = rel;
        std::size_t rel2 = version.find('.', num3 + 1);
        if (rel2 != std::string::npos) {
            num2 = rel2;
            if (version.find('.', num2 + 1) != std::string::npos)
                throw std::invalid_argument(
                    "Version string portion was too short or too long. (Parameter 'input')");
        }
    }
    int parsedComponent = ParseVersionComponent(version.substr(0, num), "input");
    if (num3 != std::string::npos) {
        int parsedComponent2 = ParseVersionComponent(version.substr(num + 1, num3 - num - 1), "input");
        if (num2 != std::string::npos) {
            int parsedComponent3 =
                ParseVersionComponent(version.substr(num3 + 1, num2 - num3 - 1), "build");
            int parsedComponent4 = ParseVersionComponent(version.substr(num2 + 1), "revision");
            Major = parsedComponent;
            Minor = parsedComponent2;
            Build = parsedComponent3;
            Revision = parsedComponent4;
        } else {
            Major = parsedComponent;
            Minor = parsedComponent2;
            Build = ParseVersionComponent(version.substr(num3 + 1), "build");
            Revision = -1;
        }
    } else {
        Major = parsedComponent;
        Minor = ParseVersionComponent(version.substr(num + 1), "input");
        Build = -1;
        Revision = -1;
    }
}

// `System.Version.TryFormatCore` decompiled: fieldCount outside [0, 4] throws
// "between 0 and 4"; a fieldCount of 3/4 over a version with `Build` unspecified
// throws "between 0 and 2"; a fieldCount of 4 with `Revision` unspecified throws
// "between 0 and 3" -- all naming 'fieldCount'. Then exactly `fieldCount`
// components render joined with '.'.
std::string Version::ToString(int fieldCount) const
{
    if (fieldCount < 0 || fieldCount > 4)
        throw std::invalid_argument("Argument must be between 0 and 4. (Parameter 'fieldCount')");
    if (fieldCount >= 3) {
        if (Build == -1)
            throw std::invalid_argument("Argument must be between 0 and 2. (Parameter 'fieldCount')");
        if (fieldCount == 4 && Revision == -1)
            throw std::invalid_argument("Argument must be between 0 and 3. (Parameter 'fieldCount')");
    }
    std::string r;
    for (int i = 0; i < fieldCount; i++) {
        if (i != 0)
            r += '.';
        switch (i) {
            case 0: r += std::to_string(Major); break;
            case 1: r += std::to_string(Minor); break;
            case 2: r += std::to_string(Build); break;
            default: r += std::to_string(Revision); break;
        }
    }
    return r;
}

} // namespace ILSpy::Decompiler::TypeSystem
