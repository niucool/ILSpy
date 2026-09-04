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

// Port of the BamlWriter half of ICSharpCode.BamlDecompiler/Baml/BamlWriter.cs
// (Ki, 2015, MIT): the document serializer -- the signature block, the three
// format versions, every record in list order, then the defer patches that
// back-fill the offset placeholders the writes recorded.
//
// C#-to-C++ porting decisions:
//  * The C# takes a Stream and constructs its writers over it; the port
//    writes into a caller-owned byte vector through BamlBinaryWriter
//    (WriteDocument clears it first).

#pragma once

#include "BamlDecompiler/Baml/BamlDocument.hpp"

#include <cstdint>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

// The C# `BamlWriter.WriteDocument(BamlDocument doc, Stream str)`: writes
// the whole document (the signature block, the versions, the records, then
// the defer patches). Mutates the records (the Position fields and the
// defer records' pos placeholders -- the C# assigns the same fields).
void WriteDocument(BamlDocument& doc, std::vector<std::uint8_t>& out);

} // namespace ILSpy::BamlDecompiler::Baml
