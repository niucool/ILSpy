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

// See WholeProjectDecompiler.hpp for the port contract (the CleanUpName walk
// over UTF-16 units, the probe-backed classification, the divergences).

#include "Decompiler/CSharp/ProjectDecompiler/WholeProjectDecompiler.hpp"

#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cstddef>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::ProjectDecompiler {
namespace {

// The C# `const int maxSegmentLength = 255`: every '/'-or-'\\'-separated
// segment is capped at this many units (counted in UTF-8 bytes on non-
// Windows platforms -- the C# `countBytes` rule).
constexpr int kMaxSegmentLength = 255;

// The C# `Path.DirectorySeparatorChar`.
#if defined(_WIN32)
constexpr char16_t kDirectorySeparatorChar = u'\\';
#else
constexpr char16_t kDirectorySeparatorChar = u'/';
#endif

// The C# `bool countBytes = !RuntimeInformation.IsOSPlatform(OSPlatform.Windows)`:
// on Windows every allowed unit counts one toward the segment cap, on the
// other platforms its UTF-8 byte count. The units reaching this arm are
// letters/digits/'-'/'_' only -- never surrogates -- so the plain UTF-8
// length of a BMP unit is the Encoding.UTF8.GetByteCount value (1/2/3,
// probed: U+00E9 -> 2, U+4E2D -> 3).
#if defined(_WIN32)
constexpr bool kCountBytes = false;
#else
constexpr bool kCountBytes = true;
#endif

int Utf8ByteCount(char16_t c) {
    if (c <= 0x7F) return 1;
    if (c <= 0x7FF) return 2;
    return 3;
}

// The C# `string.Trim()`: the char.IsWhiteSpace units removed from both
// ends (the Util::IsWhiteSpace table).
std::u16string Trim(std::u16string_view s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && Util::IsWhiteSpace(s[begin]))
        ++begin;
    while (end > begin && Util::IsWhiteSpace(s[end - 1]))
        --end;
    return std::u16string(s.substr(begin, end - begin));
}

// The ASCII-scoped invariant-culture uppercase (the divergence the header
// contract documents: U+017F is the only non-ASCII unit uppercasing into an
// ASCII letter, to 'S', which no reserved name contains).
std::u16string ToUpperInvariantAscii(std::u16string_view name) {
    std::u16string upper;
    upper.reserve(name.size());
    for (char16_t c : name)
        upper.push_back(c >= u'a' && c <= u'z'
                ? static_cast<char16_t>(c - u'a' + u'A') : c);
    return upper;
}

// The C# `static bool IsReservedFileSystemName(string name)`: the 22 Windows
// reserved device names (case-insensitively).
bool IsReservedFileSystemName(std::u16string_view name) {
    std::u16string upper = ToUpperInvariantAscii(name);
    return upper == u"AUX" || upper == u"COM1" || upper == u"COM2"
        || upper == u"COM3" || upper == u"COM4" || upper == u"COM5"
        || upper == u"COM6" || upper == u"COM7" || upper == u"COM8"
        || upper == u"COM9" || upper == u"CON" || upper == u"LPT1"
        || upper == u"LPT2" || upper == u"LPT3" || upper == u"LPT4"
        || upper == u"LPT5" || upper == u"LPT6" || upper == u"LPT7"
        || upper == u"LPT8" || upper == u"LPT9" || upper == u"NUL"
        || upper == u"PRN";
}

// The C# `static void EscapeReservedFileSystemName(StringBuilder b,
// int segmentStart, int baseNameEnd)`: appends an underscore to the segment
// [segmentStart..) of b if its base name (the part before baseNameEnd, or
// the whole segment when there was no dot) is a reserved Windows device
// name -- "con" becomes "con_" and "con.txt" becomes "con_.txt" (Windows
// device-name parsing ignores everything after the first dot, so the
// underscore goes before it, not at the end).
void EscapeReservedFileSystemName(std::u16string& b, int segmentStart, int baseNameEnd) {
    if (baseNameEnd < 0)
        baseNameEnd = static_cast<int>(b.size());
    int baseNameLength = baseNameEnd - segmentStart;
    // All reserved names are 3 or 4 units long; checking the length first
    // avoids building a substring for segments that cannot be reserved.
    if ((baseNameLength == 3 || baseNameLength == 4)
        && IsReservedFileSystemName(std::u16string_view(b)
            .substr(static_cast<std::size_t>(segmentStart),
                static_cast<std::size_t>(baseNameLength)))) {
        b.insert(static_cast<std::size_t>(baseNameEnd), 1, u'_');
    }
}

// The C# `static string CleanUpName(string text, bool separateAtDots,
// bool treatAsFileName, bool treatAsPath)` over UTF-16 code units (the
// header contract documents the walk): the extension extraction, the
// rooted-path/generics stripping, the whitelist loop with the per-segment
// cap, the reserved-name escaping, and the extension re-append.
std::u16string CleanUpName(std::u16string text, bool separateAtDots,
    bool treatAsFileName, bool treatAsPath)
{
    std::u16string extension;  // empty == the C# null
    bool hasExtension = false;
    int currentSegmentLength = 0;
    // Extract extension from the end of the name, if valid
    if (treatAsFileName)
    {
        // Check if input is a file name, i.e., has a valid extension. If
        // yes, preserve the extension and append it at the end -- but only
        // if the extension length does not exceed maxSegmentLength; in that
        // case we just give up and treat the extension no different from
        // the file name.
        std::size_t lastDot = text.rfind(u'.');
        if (lastDot != std::u16string::npos
            && static_cast<int>(text.size() - lastDot) < kMaxSegmentLength)
        {
            std::u16string originalText = text;
            extension = text.substr(lastDot);
            hasExtension = true;
            text = text.substr(0, lastDot);
            for (char16_t c : extension)
            {
                if (!(Util::IsLetterOrDigit(c) || c == u'-' || c == u'_' || c == u'.'))
                {
                    // extension contains an invalid character, therefore
                    // cannot be a valid extension.
                    extension.clear();
                    hasExtension = false;
                    text = originalText;
                    break;
                }
            }
        }
    }
    // Remove anything that could be confused with a rooted path.
    std::size_t pos = text.find(u':');
    if (pos != std::u16string::npos && pos > 0)
        text = text.substr(0, pos);
    text = Trim(text);
    // Remove generics
    pos = text.find(u'`');
    if (pos != std::u16string::npos && pos > 0)
        text = Trim(text.substr(0, pos));
    // Whitelist allowed characters, replace everything else:
    std::u16string b;
    b.reserve(text.size() + extension.size());
    int segmentStart = 0;
    int baseNameEnd = -1;
    for (char16_t c : text)
    {
        if (Util::IsLetterOrDigit(c) || c == u'-' || c == u'_')
        {
            currentSegmentLength += kCountBytes ? Utf8ByteCount(c) : 1;
            // if the current segment exceeds maxSegmentLength characters,
            // skip until the end of the segment.
            if (currentSegmentLength <= kMaxSegmentLength)
                b.push_back(c);
        }
        else if (c == u'.' && !b.empty() && b.back() != u'.')
        {
            currentSegmentLength++;
            if (separateAtDots)
            {
                // The dot ends the current segment.
                EscapeReservedFileSystemName(b, segmentStart, baseNameEnd);
                b.push_back(u'.');
                segmentStart = static_cast<int>(b.size());
                baseNameEnd = -1;
                // Reset length at end of segment.
                currentSegmentLength = 0;
            }
            else if (currentSegmentLength <= kMaxSegmentLength)
            {
                // The first dot ends the segment's base name, the part
                // Windows device-name parsing looks at.
                if (baseNameEnd < 0)
                    baseNameEnd = static_cast<int>(b.size());
                b.push_back(u'.');  // allow dot, but never two in a row
            }
        }
        else if (treatAsPath && (c == u'/' || c == u'\\') && currentSegmentLength > 0)
        {
            // if we treat this as a file name, we've started a new segment
            EscapeReservedFileSystemName(b, segmentStart, baseNameEnd);
            b.push_back(kDirectorySeparatorChar);
            segmentStart = static_cast<int>(b.size());
            baseNameEnd = -1;
            currentSegmentLength = 0;
        }
        else
        {
            if (Util::IsHighSurrogate(c))
            {
                // only add one replacement character for surrogate pairs
                continue;
            }

            currentSegmentLength++;
            // if the current segment exceeds maxSegmentLength characters,
            // skip until the end of the segment.
            if (currentSegmentLength <= kMaxSegmentLength)
                b.push_back(u'-');
        }
    }
    if (b.empty())
        b.push_back(u'-');
    EscapeReservedFileSystemName(b, segmentStart, baseNameEnd);
    std::u16string name = b;
    if (hasExtension)
    {
        // make sure that adding the extension to the filename does not
        // exceed maxSegmentLength; trim the name if necessary. (A base
        // name shorter than the extension is the unreachable-in-practice
        // C# ArgumentOutOfRange arm -- the port maps it to
        // std::out_of_range, the argument-exception convention.)
        if (static_cast<int>(name.size() + extension.size()) > kMaxSegmentLength)
        {
            if (name.size() < extension.size())
                throw std::out_of_range(
                    "WholeProjectDecompiler::CleanUpName: the file name is "
                    "shorter than the extension it must keep.");
            name = name.substr(0, name.size() - extension.size());
        }
        name += extension;
    }

    if (name == u".")
        return u"_";
    return name;
}

}  // namespace

std::string CleanUpFileName(const std::string& text, const std::string& extension) {
    // The C# `$"{text}{extension}"` / `$"{text}.{extension}"` pair: a null
    // or empty extension concatenates nothing, an extension already
    // starting with '.' concatenates directly, anything else gets the dot
    // inserted. The file-name treatment is on only for a non-empty
    // extension (the C# !string.IsNullOrEmpty).
    std::u16string combined;
    if (extension.empty() || extension[0] == '.') {
        combined = Util::Utf8ToUtf16(text) + Util::Utf8ToUtf16(extension);
    } else {
        combined = Util::Utf8ToUtf16(text) + u'.' + Util::Utf8ToUtf16(extension);
    }
    return Util::Utf16ToUtf8(CleanUpName(std::move(combined),
        false, !extension.empty(), false));
}

std::string SanitizeFileName(const std::string& fileName) {
    return Util::Utf16ToUtf8(CleanUpName(Util::Utf8ToUtf16(fileName),
        false, true, true));
}

std::string CleanUpDirectoryName(const std::string& text) {
    return Util::Utf16ToUtf8(CleanUpName(Util::Utf8ToUtf16(text),
        false, false, false));
}

std::string CleanUpPath(const std::string& text) {
    // The C# `.Replace('.', Path.DirectorySeparatorChar)` over the cleaned
    // name: every '.' the separateAtDots walk kept becomes the platform's
    // own separator.
    std::u16string cleaned = CleanUpName(Util::Utf8ToUtf16(text), true, false, true);
    for (char16_t& c : cleaned)
        if (c == u'.')
            c = kDirectorySeparatorChar;
    return Util::Utf16ToUtf8(cleaned);
}

}  // namespace ILSpy::Decompiler::CSharp::ProjectDecompiler
