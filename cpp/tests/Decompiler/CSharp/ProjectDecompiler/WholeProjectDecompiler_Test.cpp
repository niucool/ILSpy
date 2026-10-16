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

// The WholeProjectDecompiler name-sanitizer family (the C#
// ReservedFileSystemNameTests matrix plus the port's gold-pinned extras):
// Windows reserves the device names below as file/directory names -- both
// bare ("CON") and, on many Windows versions, with an extension appended
// ("CON.cs" resolves to \\.\CON) -- so every sanitizer escapes the base name
// (the part before the first dot) with a trailing underscore (issue #3775).
//
// Every expectation below was dumped from the REAL
// ICSharpCode.Decompiler 11.0's own WholeProjectDecompiler (the SDK-10
// probe project C:\temp-probe\CharProbe driving the installed ilspycmd
// tool's ICSharpCode.Decompiler.dll over this exact input matrix); the
// reserved-name and cap tests build their long expectations structurally
// (the input name + "_", std::string(251, 'a') + ".txt") exactly the way
// the C# test suite does.

#include "Decompiler/CSharp/ProjectDecompiler/WholeProjectDecompiler.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

namespace ProjectDecompiler =
    ILSpy::Decompiler::CSharp::ProjectDecompiler;

// The C# `Path.DirectorySeparatorChar` (the separator the path-aware
// sanitizers emit).
#if defined(_WIN32)
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

// The 22 reserved Windows device names (the C# test suite's list).
const char* kReservedNames[] = {
    "AUX", "CON", "NUL", "PRN",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

std::string Sep() { return std::string(1, kSep); }

std::string Lower(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return r;
}

}  // namespace

// The C# CleanUpFileNameEscapesReservedNames matrix: the reserved name
// with a '.cs' extension becomes the escaped base name plus the extension.
TEST(WholeProjectDecompilerTest, CleanUpFileNameEscapesReservedNames)
{
    for (const char* name : kReservedNames)
        EXPECT_EQ(ProjectDecompiler::CleanUpFileName(name, ".cs"),
            std::string(name) + "_.cs") << name;
}

// The C# CleanUpFileNameEscapesReservedNamesCaseInsensitively matrix.
TEST(WholeProjectDecompilerTest, CleanUpFileNameEscapesReservedNamesCaseInsensitively)
{
    for (const char* name : kReservedNames) {
        std::string lower = Lower(name);
        EXPECT_EQ(ProjectDecompiler::CleanUpFileName(lower, ".cs"),
            lower + "_.cs") << name;
    }
}

// The C# CleanUpFileNameEscapesTheBaseNameOfMultiDotNames: the underscore
// lands on the base name (Windows device-name parsing ignores everything
// after the first dot, so "con.fig.cs_" would still resolve to \\.\CON).
TEST(WholeProjectDecompilerTest, CleanUpFileNameEscapesTheBaseNameOfMultiDotNames)
{
    EXPECT_EQ(ProjectDecompiler::CleanUpFileName("con.fig", ".cs"),
        "con_.fig.cs");
    // Without the extension: the plain directory-name path escapes the same
    // base name (gold: CleanUpDirectoryName("con.fig") == "con_.fig").
    EXPECT_EQ(ProjectDecompiler::CleanUpDirectoryName("con.fig"), "con_.fig");
    // The path form (dots as separators): each segment's own base name.
    EXPECT_EQ(ProjectDecompiler::CleanUpPath("con.fig"), "con_" + Sep() + "fig");
}

// The C# SanitizeFileNameEscapesReservedNames matrix.
TEST(WholeProjectDecompilerTest, SanitizeFileNameEscapesReservedNames)
{
    for (const char* name : kReservedNames) {
        EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
            std::string(name) + ".txt"), std::string(name) + "_.txt") << name;
        // The bare reserved name (no extension to preserve).
        EXPECT_EQ(ProjectDecompiler::SanitizeFileName(name),
            std::string(name) + "_") << name;
    }
}

// The C# SanitizeFileNameEscapesReservedDirectorySegments matrix: the
// reserved name leading a path.
TEST(WholeProjectDecompilerTest, SanitizeFileNameEscapesReservedDirectorySegments)
{
    for (const char* name : kReservedNames)
        EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
            std::string(name) + "/data.bin"),
            std::string(name) + "_" + Sep() + "data.bin") << name;
}

// The C# SanitizeFileNameEscapesReservedFinalSegments matrix: the
// reserved name ending a path.
TEST(WholeProjectDecompilerTest, SanitizeFileNameEscapesReservedFinalSegments)
{
    for (const char* name : kReservedNames)
        EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
            "dir/" + std::string(name) + ".png"),
            "dir" + Sep() + name + "_.png") << name;
}

// The C# CleanUpDirectoryNameEscapesReservedNames matrix.
TEST(WholeProjectDecompilerTest, CleanUpDirectoryNameEscapesReservedNames)
{
    for (const char* name : kReservedNames)
        EXPECT_EQ(ProjectDecompiler::CleanUpDirectoryName(name),
            std::string(name) + "_") << name;
}

// The C# CleanUpPathEscapesReservedLeadingSegments matrix.
TEST(WholeProjectDecompilerTest, CleanUpPathEscapesReservedLeadingSegments)
{
    for (const char* name : kReservedNames)
        EXPECT_EQ(ProjectDecompiler::CleanUpPath(
            std::string(name) + ".Foo"),
            std::string(name) + "_" + Sep() + "Foo") << name;
}

// The C# CleanUpPathEscapesReservedTrailingSegments matrix.
TEST(WholeProjectDecompilerTest, CleanUpPathEscapesReservedTrailingSegments)
{
    for (const char* name : kReservedNames)
        EXPECT_EQ(ProjectDecompiler::CleanUpPath("Foo." + std::string(name)),
            "Foo" + Sep() + name + "_") << name;
}

// The C# NamesMerelyStartingWithReservedNamesAreNotEscaped matrix.
TEST(WholeProjectDecompilerTest, NamesMerelyStartingWithReservedNamesAreNotEscaped)
{
    for (const char* name :
        { "Console", "CONtoso", "con1", "com10", "lpt10" }) {
        EXPECT_EQ(ProjectDecompiler::CleanUpFileName(name, ".cs"),
            std::string(name) + ".cs") << name;
        EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
            std::string(name) + ".txt"), std::string(name) + ".txt") << name;
        EXPECT_EQ(ProjectDecompiler::CleanUpDirectoryName(name), name)
            << name;
    }
}

// The C# OrdinaryPathsPassThroughUnchanged matrix.
TEST(WholeProjectDecompilerTest, OrdinaryPathsPassThroughUnchanged)
{
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName("config.txt"), "config.txt");
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName("dir/file.png"),
        "dir" + Sep() + "file.png");
    EXPECT_EQ(ProjectDecompiler::CleanUpPath("Foo.Bar"), "Foo" + Sep() + "Bar");
}

// The extension concatenation ternary: an extension without its leading
// dot gets one inserted; an empty extension (the C# null) concatenates
// nothing and turns the file-name treatment off.
TEST(WholeProjectDecompilerTest, CleanUpFileNameExtensionConcatenation)
{
    EXPECT_EQ(ProjectDecompiler::CleanUpFileName("CON", "cs"), "CON_.cs");
    EXPECT_EQ(ProjectDecompiler::CleanUpFileName("con", "cs"), "con_.cs");
    EXPECT_EQ(ProjectDecompiler::CleanUpFileName("Foo", ""), "Foo");
    // The empty extension keeps the whole name (no escape needed for a
    // non-reserved name; the file-name treatment is off).
    EXPECT_EQ(ProjectDecompiler::CleanUpFileName("Foo.Bar", ""), "Foo.Bar");
}

// The gold-pinned port extras over the tricky inputs (every expectation
// dumped from the real C# sanitizer; non-ASCII as raw UTF-8 hex escapes).
TEST(WholeProjectDecompilerTest, SanitizeFileNameGoldMatrix)
{
    struct Case { const char* input; const char* expected; };
    const Case cases[] = {
        // The Unicode passthrough: '中文' are letters (OtherLetter), so
        // they survive the whitelist and the extension check, byte-exactly.
        {"Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87",
            "Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87"},
        {"\xE4\xB8\xAD\xE6\x96\x87\xE8\xB5\x84\xE6\xBA\x90.txt",
            "\xE4\xB8\xAD\xE6\x96\x87\xE8\xB5\x84\xE6\xBA\x90.txt"},
        {"name.\xE4\xB8\xAD\xE6\x96\x87", "name.\xE4\xB8\xAD\xE6\x96\x87"},
        // A surrogate pair yields ONE replacement character (the high
        // surrogate is skipped, the low one renders '-').
        {"a" "\xF0\x9F\x98\x80" "b.txt", "a-b.txt"},
        // Spaces (and every non-letter/digit) render '-', never trimmed
        // from the middle.
        {"a b c.txt", "a-b-c.txt"},
        {"sp  ace.txt", "sp--ace.txt"},
        // An extension with an invalid character falls back to being part
        // of the name (no extension preserved).
        {"x.a b", "x.a-b"},
        // The ':' rooted-path strip (only AFTER the first unit) and the
        // '`' generics strip.
        {"C:foo/bar", "C"},
        {"C:foo.txt", "C.txt"},
        {":foo.txt", "-foo.txt"},
        {"List`1.txt", "List.txt"},
        {"Foo`1`2.txt", "Foo.txt"},
        // The whole extension survives the '`' strip (the strip truncates
        // the whole name).
        {"a`1<T>.b", "a.b"},
        // Trim removes the Unicode whitespace (incl. NBSP) from both ends.
        {"foo  .txt", "foo.txt"},
        {"  Foo  ", "Foo"},
        {"Foo\xC2\xA0\xC2\xA0.txt", "Foo.txt"},
        {" ", "-"},
        // The dot edges: an extension that IS a dot; a leading dot renders
        // '-'; the empty name.
        {"..", "-."},
        {".", "-."},
        {"...", "-.."},
        {"", "-"},
        // The allowed '-'/'_' and the odd LetterOrDigit units.
        {"a-b_c.txt", "a-b_c.txt"},
        // U+00AA, U+00B5, U+00BA are letters (OtherLetter).
        {"\xC2\xAA\xC2\xB5\xC2\xBA.txt", "\xC2\xAA\xC2\xB5\xC2\xBA.txt"},
        // U+017F (long s) is a letter -- it passes through (only its
        // UPPER case is ASCII).
        {"\xC5\xBF.txt", "\xC5\xBF.txt"},
        // U+FFFD is not a letter: one '-'.
        {"\xEF\xBF\xBD.txt", "-.txt"},
    };
    for (const auto& c : cases)
        EXPECT_EQ(ProjectDecompiler::SanitizeFileName(c.input),
            c.expected) << c.input;

    // An interior '\\' splits a segment like '/' does (the same escape
    // the '/' form gets above).
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName("dir\\file.png"),
        "dir" + Sep() + "file.png");
}

// The gold-pinned CleanUpPath / CleanUpDirectoryName extras: the
// consecutive-dot edge (the second dot renders '-' between segments) and
// the trim interaction.
TEST(WholeProjectDecompilerTest, CleanUpPathGoldMatrix)
{
    EXPECT_EQ(ProjectDecompiler::CleanUpPath(".."), "-" + Sep());
    EXPECT_EQ(ProjectDecompiler::CleanUpPath("Foo..Bar"),
        "Foo" + Sep() + "-Bar");
    // A bare reserved name through the path form escapes the whole
    // segment (the path form is not file-name aware: no extension
    // handling, so the bare name is the base name).
    EXPECT_EQ(ProjectDecompiler::CleanUpPath("CON"), "CON_");
    EXPECT_EQ(ProjectDecompiler::CleanUpPath(" Foo.Bar "), "Foo" + Sep() + "Bar");
}

// The 255-unit segment cap (the C# maxSegmentLength) and the
// extension-length trim, structurally (the gold lengths: a 300-unit name
// truncates to 255; with a 4-unit extension the kept name is 251 units so
// the total stays 255).
TEST(WholeProjectDecompilerTest, SanitizeFileNameSegmentCap)
{
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(std::string(300, 'a')),
        std::string(255, 'a'));
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        std::string(300, 'a') + ".txt"), std::string(251, 'a') + ".txt");
    // The cap lands mid-name: the last 4 units of the built name are
    // dropped so the extension fits.
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        std::string(255, 'b') + ".txt"), std::string(251, 'b') + ".txt");
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        std::string(254, 'b') + ".txt"), std::string(250, 'b') + ".txt");
    // A reserved-name prefix stays (the cap hits mid-segment).
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        "con" + std::string(300, 'x') + ".txt"),
        "con" + std::string(248, 'x') + ".txt");
    // A path: the cap is per segment, and the trim shortens the LAST
    // segment so the whole name plus extension fits (the gold: the trim
    // cuts at 254 units -- exactly "/foo" + the 250-b segment -- so the
    // whole "bar" segment falls away and only the extension survives).
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        "foo/" + std::string(250, 'b') + "/bar.txt"),
        "foo" + Sep() + std::string(250, 'b') + ".txt");
    // The path and directory forms cap without extension handling.
    EXPECT_EQ(ProjectDecompiler::CleanUpPath(std::string(300, 'b')),
        std::string(255, 'b'));
    EXPECT_EQ(ProjectDecompiler::CleanUpDirectoryName(std::string(300, 'b')),
        std::string(255, 'b'));
}

// The give-up extension arm: an extension at least maxSegmentLength units
// long is not extracted (no preservation, no file-name treatment) -- the
// whole name walks the whitelist and caps at 255 units.
TEST(WholeProjectDecompilerTest, SanitizeFileNameLongExtensionGiveUp)
{
    // 'a.' + 300 b's: the extension candidate is 301 units >= 255, so no
    // extension; the walk renders 'a' + '.' + 253 b's (255 units).
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        "a." + std::string(300, 'b')), "a." + std::string(253, 'b'));
    // 300 b's + '.x': the 2-unit extension IS extracted, but the name
    // trim then keeps 253 b's so the total is 255.
    EXPECT_EQ(ProjectDecompiler::SanitizeFileName(
        std::string(300, 'b') + ".x"), std::string(253, 'b') + ".x");
}
