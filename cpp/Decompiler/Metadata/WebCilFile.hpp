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

// Port of ICSharpCode.Decompiler/Metadata/WebCilFile.cs (the static
// structural surface): the WASM container walk and the WebCIL segment
// probe that locate the embedded ECMA-335 metadata stream inside a
// .wasm WebCIL file, plus the RVA range resolution against the WebCIL
// COFF-style section table.
//
// Porting decisions:
//  * The static surface: TryParse / TryGetSectionDataRange (the
//    counterparts of FromFile's structural half and the internal
//    TryGetSectionDataRange), plus BuildPeImage -- the port-side adapter
//    that presents the container to the PE-shaped MetadataFile reader
//    (the C# instead derives a WebCilFile kind; see BuildPeImage's
//    note). The WebCilFileLoader (ILSpyX/FileLoaders/) consumes both.
//  * The C# memory-maps the file and reads through a BinaryReader whose
//    overrun throws EndOfStreamException; TranslateRVA throws
//    BadImageFormatException when no section contains an RVA. FromFile
//    catches (EndOfStream, Overflow, BadImageFormat) and returns null.
//    The port reads a bounds-checked span whose overruns throw
//    std::out_of_range (the established InvalidData mapping) and
//    TranslateRVA's miss throws the same, so one catch-all reduces the
//    same crafted/truncated inputs to nullopt.
//  * The C# `MetadataReaderProvider.FromMetadataStream` call at the end
//    of FromFile validates the located metadata stream; this slice
//    stops at the structural parse (the C# tests pin only the null
//    rejections and the range arithmetic). The integration slice adds
//    the stream validation when the metadata reader is wired up.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The C# `public enum WasmSectionId : byte` -- the values are the
// WebAssembly spec section ids; order matters.
enum class WasmSectionId : std::uint8_t {
    Custom = 0,
    Type = 1,
    Import = 2,
    Function = 3,
    Table = 4,
    Memory = 5,
    Global = 6,
    Export = 7,
    Start = 8,
    Element = 9,
    Code = 10,
    Data = 11,
    DataCount = 12,
};

// The C# `public class WasmSection` (Id/Offset/Size; the C# also carries
// the mapped-view accessor, which nothing but the full-file read uses --
// the port's parse works over the byte image directly).
struct WasmSection {
    WasmSectionId Id;
    std::uint64_t Offset;
    std::uint32_t Size;
};

// The C# `public struct WebcilHeader`.
struct WebcilHeader {
    std::uint16_t VersionMajor = 0;
    std::uint16_t VersionMinor = 0;
    std::uint16_t CoffSections = 0;
    std::uint32_t PECliHeaderRVA = 0;
    std::uint32_t PECliHeaderSize = 0;
    std::uint32_t PEDebugRVA = 0;
    std::uint32_t PEDebugSize = 0;
};

// The C# reuses System.Reflection.PortableExecutable.SectionHeader for
// the WebCIL COFF-style section table; the reader consumes exactly these
// four fields, so the port carries them as-is.
struct WebcilSectionHeader {
    std::uint32_t VirtualSize = 0;
    std::uint32_t VirtualAddress = 0;
    std::uint32_t RawDataSize = 0;
    std::uint32_t RawDataPtr = 0;
};

// The successful structural parse: where the WebCIL blob sits in the
// container (WebcilOffset), where the embedded ECMA-335 metadata stream
// starts (MetadataOffset, absolute in the file), and the parsed tables.
struct WebCilParseResult {
    std::int64_t WebcilOffset = 0;
    std::int64_t MetadataOffset = 0;
    WebcilHeader Header;
    std::vector<WebcilSectionHeader> SectionHeaders;
    std::vector<WasmSection> WasmSections;
};

// The pure container reader -- the static surface of the C#
// `public sealed class WebCilFile`.
class WebCilFile {
public:
    // The C# `WebCilFile.FromFile` structural half: the WASM container
    // walk plus the WebCIL segment probe over the file at `path`.
    // nullopt when the file is not a recognized WebCIL container --
    // including every truncated or crafted shape (the C# catch-all over
    // EndOfStream/Overflow/BadImageFormat maps to the port's
    // std::out_of_range catch-all).
    static std::optional<WebCilParseResult> TryParse(std::string_view path);

    // The port-side adapter (no C# counterpart): the C# models WebCilFile
    // as its own MetadataFile kind -- a MetadataReaderProvider over the
    // extracted metadata stream plus the WebCIL section overrides. The
    // port instead reuses its PE-shaped MetadataFile reader: this lays
    // the original container bytes behind a synthetic minimal PE header
    // whose section table is the WebCIL COFF table verbatim (the raw
    // pointers shifted into the adapted image), so every RVA -- the CLI
    // header, the metadata stream, the method bodies -- resolves through
    // the established PE-reader paths unchanged. The WebCIL payload is
    // byte-preserved; only the DOS/NT headers around it are fabricated.
    static std::vector<std::uint8_t> BuildPeImage(
        const std::vector<std::uint8_t>& container,
        const WebCilParseResult& parsed);

    // The C# `internal static bool TryGetSectionDataRange(...)`: resolves
    // an RVA to the (file offset, length) of its section's raw data
    // inside the container image, or returns false when no section
    // contains the RVA or the raw-data range falls outside the view. All
    // arithmetic widens to 64 bits so crafted uint header fields cannot
    // wrap the range check or narrow into a length that looks valid.
    static bool TryGetSectionDataRange(
        const std::vector<WebcilSectionHeader>& sectionHeaders,
        std::int64_t webcilOffset, std::int64_t viewLength, std::int32_t rva,
        std::int64_t& offset, std::int32_t& length);
};

}  // namespace ILSpy::Decompiler::Metadata
