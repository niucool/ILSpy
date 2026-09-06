// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port-authored System.Reflection.PortableExecutable.PEReader/PEHeaders
// eager-parse stand-in (the .NET 10 semantics decompiled from the pinned
// 10.0.8 runtime with this repo's own ilspycmd).
//
// The C# `new PEReader(stream, streamOptions)` validates the PE headers when
// its `PEHeaders` property is first read, which every `MetadataFile`
// constructor reaches through the `reader.HasMetadata` test. The port's
// `MetadataFile` constructor never throws (it reports `IsValid()`), so the
// consumers that must reproduce the C# exception behavior (the
// `XamlDecompiler.LoadPEFile` chain, the resolver's
// `CreatePEFileFromFileName`) classify the failure classes over the file
// bytes through this parse: it walks the decompiled
// SkipDosHeader -> CoffHeader -> PEHeader -> ReadSectionHeaders ->
// TryCalculateCorHeaderOffset -> CalculateMetadataLocation chain and throws
// std::invalid_argument carrying the exact .NET BadImageFormatException
// message for every malformed arm, or answers the metadata block's location
// (the C# `MetadataStartOffset`/`MetadataSize` -- `HasMetadata` is
// `MetadataSize > 0`).
//
// Documented divergences:
//  * A COFF-only image (a headerless "pure" COFF object file) whose
//    `.cormeta` section carries metadata parses a working MetadataFile in
//    the C# (the metadata is the section's raw data); the port's
//    MetadataFile reads the cor20 directory only, so the COFF-only arm
//    always answers "no metadata" -- the caller throws
//    MetadataFileNotSupportedException where the C# succeeds. Pure-IL
//    object files are the only shape that reaches it.
//  * The metadata CONTENT parse (the C# `GetMetadataReader` inside the
//    MetadataFile constructor) stays the caller's `IsValid()` gate; the
//    real engine's exception for a corrupt table root is an
//    OverflowException ("Arithmetic operation resulted in an overflow.")
//    for the stream-count shape and BadImageFormatException for the
//    others -- the port cannot classify which corruption through the
//    never-throwing constructor, so the caller's arm carries the
//    representative OverflowException message.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The C# `PEReader.HasMetadata` answer plus the metadata block's location.
struct MetadataBlock {
    std::size_t offset = 0; // the C# `MetadataStartOffset` (a file offset)
    std::size_t size = 0;   // the C# `MetadataSize`
};

// The decompiled .NET 10 `PEHeaders(Stream, int, bool)` eager parse over the
// raw image bytes. Throws std::invalid_argument with the exact .NET
// BadImageFormatException message for every malformed arm:
//   "Image is too small." / "Image is either too small or contains an
//    invalid byte offset or count." / "Invalid PE signature." /
//    "Unknown file format." / "Unknown PE Magic value." /
//    "Invalid number of sections declared in PE header." /
//    "Invalid COR header size." / "Section too small." /
//    "Missing data directory." / "Invalid metadata section span."
MetadataBlock ParsePEReaderHeaders(const std::vector<std::uint8_t>& image);

} // namespace ILSpy::Decompiler::Metadata
