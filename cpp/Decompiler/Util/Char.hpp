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

// System.Char classification over UTF-16 code units (the char.IsLetterOrDigit /
// char.IsWhiteSpace / char.IsHighSurrogate semantics the ported .NET string
// handling needs). The port works in UTF-8 (decision D2), so these take the
// UTF-16 code unit a decoded string iterates -- the same unit granularity the
// C# `foreach (var c in text)` walks, including the surrogate-pair handling
// (a high surrogate is skipped and its low surrogate rendered as one
// replacement, so supplementary-plane classification never arises and only
// the BMP is tabulated).
//
// The range tables are probed unit-by-unit from the .NET 10 runtime
// (C:\temp-probe\CharProbe\Program.cs dumps every BMP unit whose
// char.IsLetterOrDigit / char.IsWhiteSpace returns true; the IsControl /
// IsSurrogate ranges come from the C:\temp-probe\PathProbe\Program.cs P5
// dump); no supplementary classification exists because surrogates are
// neither letters nor whitespace.
//
// char.IsLetterOrDigit covers the Unicode categories L* (letters) and Nd
// (decimal digits) -- U+00AA, U+00B5 and U+00BA are letters (OtherLetter),
// and U+017F (long s) is a letter. char.IsWhiteSpace covers the control
// characters 09-0D and NEL (85) plus the space-like separators (20, A0,
// 1680, 2000-200A, 2028-2029, 202F, 205F, 3000) -- string.Trim() removes
// exactly these from both ends.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::Util {

// The Unicode letter or decimal-digit units (L* and Nd) over the BMP.
// The C# `char.IsLetter(char)` -- the L* category over the BMP (the
// IsLetterOrDigit table minus the Nd digit units, probed unit-by-unit the
// same way). First consumer: AssignVariableNames.IsValidName.
bool IsLetter(char16_t c);

bool IsLetterOrDigit(char16_t c);

// The Unicode whitespace units (the string.Trim() set) over the BMP.
bool IsWhiteSpace(char16_t c);

// The UTF-16 high-surrogate units (U+D800..U+DBFF).
bool IsHighSurrogate(char16_t c);

// The UTF-16 surrogate units, BOTH halves (U+D800..U+DFFF) -- the
// char.IsSurrogate set (an unpaired half inside a UTF-16 string, which
// the escape rules render as its \uXXXX form).
bool IsSurrogate(char16_t c);

// The Unicode control units (category Cc: U+0000..U+001F and U+007F..U+009F)
// over the BMP -- the char.IsControl set.
bool IsControl(char16_t c);

}  // namespace ILSpy::Decompiler::Util
