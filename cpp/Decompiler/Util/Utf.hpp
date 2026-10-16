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

// UTF-8 <-> UTF-16 conversion.
//
// The port uses UTF-8 (std::string) everywhere internally (decision D2). The
// ECMA-335 #US heap and .NET string literals are UTF-16LE, so conversion lives
// at these boundaries. The implementations are dependency-free and handle
// surrogate pairs and 1-4 byte UTF-8 sequences; ill-formed input is replaced
// with U+FFFD rather than throwing, matching the decompiler's robustness tenet.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::Util {

// UTF-16LE/UTF-16 (char16_t) -> UTF-8.
std::string Utf16ToUtf8(std::u16string_view in);

// UTF-8 -> UTF-16 (char16_t).
std::u16string Utf8ToUtf16(std::string_view in);

} // namespace ILSpy::Decompiler::Util
