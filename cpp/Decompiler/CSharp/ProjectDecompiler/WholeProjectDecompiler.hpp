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

// The name-sanitizer family of WholeProjectDecompiler
// (ICSharpCode.Decompiler/CSharp/ProjectDecompiler/WholeProjectDecompiler.cs
// -- the static CleanUp* / SanitizeFileName members). The rest of the class
// (the whole-project decompile machinery: DecompileProject, the project
// writers, the language selection) is deferred with the Phase-5/7 decompiler
// back end; the sanitizers are the piece the ilspycmd --resource -o extraction
// and every future project-output path need, and they are pure string
// transforms with no decompiler dependency.
//
// The C# CleanUpName walk (the shared core of all four helpers) works over
// UTF-16 code units, so the port converts its UTF-8 inputs (decision D2) to
// std::u16string first and walks the same units -- including the
// surrogate-pair handling (the high surrogate is skipped and the low one
// renders a single '-', so an astral code point contributes one replacement
// -- exactly like the C#) and the Unicode letter/digit/whitespace
// classification (Util/Char.hpp, the probe-generated char.IsLetterOrDigit /
// char.IsWhiteSpace tables). The C# inputs are always well-formed .NET
// strings; the port's ill-formed UTF-8 inputs reach the walk as U+FFFD
// replacement units (the Util::Utf8ToUtf16 contract), which then sanitize as
// non-letters (one '-' per unit).
//
// Divergences (probed against the real .NET behavior):
//  * ToUpperInvariant inside the reserved-name check is ASCII-scoped
//    (char.ToUpperInvariant maps exactly one non-ASCII unit into an ASCII
//    letter: U+017F long s -> 'S', which no reserved name contains -- every
//    other non-ASCII unit uppercases outside ASCII or to itself).
//  * An extension longer than the base name (reachable only for a 254-unit
//    extension with a shorter base, where the C# `name.Remove(negative)`
//    throws ArgumentOutOfRangeException) throws std::out_of_range here (the
//    port's argument-exception convention); both escape to the CLI's global
//    catch and EX_SOFTWARE.

#pragma once

#include <string>

namespace ILSpy::Decompiler::CSharp::ProjectDecompiler {

// Cleans up a node name for use as a file name: the extension is
// concatenated first (a dot inserted unless it already starts with one),
// then CleanUpName runs with the extension-aware file-name treatment. A null
// or empty extension (the C# string? -- the port's empty string) concatenates
// nothing and turns the file-name treatment off.
std::string CleanUpFileName(const std::string& text, const std::string& extension);

// Removes invalid characters from file names and reduces their length
// (each '/'-or-'\\'-separated segment is capped at 255 units, the extension
// kept and re-appended), but keeps file extensions and path structure
// intact.
std::string SanitizeFileName(const std::string& fileName);

// Cleans up a node name for use as a directory name (no extension handling,
// no path structure).
std::string CleanUpDirectoryName(const std::string& text);

// Cleans up a node name for use as a path: dots are seen as segment
// separators while cleaning, then every surviving '.' becomes the
// platform's directory separator.
std::string CleanUpPath(const std::string& text);

}  // namespace ILSpy::Decompiler::CSharp::ProjectDecompiler
