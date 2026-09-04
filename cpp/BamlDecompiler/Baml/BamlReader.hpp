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

// Port of the BamlReader half of ICSharpCode.BamlDecompiler/Baml/BamlReader.cs
// (Ki, 2015, MIT): the signature/header parse and the record dispatch loop
// that produces the BamlDocument.
//
// C#-to-C++ porting decisions:
//  * The C# takes a seekable Stream and a CancellationToken; the port takes
//    the .baml bytes directly (the byte-span convention, ResourcesFile's
//    shape). The token is dropped (the ported CLI callers pass
//    CancellationToken.None; the established CancellationToken deferral).
//  * NotSupportedException maps to std::invalid_argument carrying the
//    parameterless default message ("Specified method is not supported.");
//    InvalidDataException maps to std::out_of_range (the malformed-data
//    family; see BamlBinaryReader.hpp).
//  * IsBamlHeader: the C# reads from the stream's CURRENT position and
//    restores it in a finally; the port's span has no cursor state, so it
//    always reads from offset 0 (the signature-holding streams every
//    consumer passes start at 0). A stream shorter than 4 bytes still
//    throws the EndOfStream message (the C# lets it escape).

#pragma once

#include "BamlDecompiler/Baml/BamlDocument.hpp"

#include <cstdint>

namespace ILSpy::BamlDecompiler::Baml {

// The C# `BamlReader.IsBamlHeader(Stream)`: the "MSBAML" signature check
// over the first bytes (the length-prefixed UTF-16 signature form). Throws
// std::out_of_range ("Unable to read beyond the end of the stream.") for a
// stream shorter than the 4-byte length prefix, exactly as the C# lets the
// BinaryReader throw escape.
bool IsBamlHeader(const std::uint8_t* data, std::size_t size);

// The C# `BamlReader.ReadDocument(Stream, CancellationToken)`: parses the
// signature and the three format versions (all must be 0.96), then the
// flat record list, then resolves every defer record's target against the
// positions of the parsed records. Throws std::invalid_argument for the
// unsupported signature, versions, or record types, and std::out_of_range
// for the malformed-data arms (the truncated stream, the invalid signature
// length, a defer record pointing at an offset that is not a record
// boundary, the malformed defer blocks).
BamlDocument ReadDocument(const std::uint8_t* data, std::size_t size);

} // namespace ILSpy::BamlDecompiler::Baml
