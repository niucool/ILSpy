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

// The decompiled .NET 10 PEHeaders eager parse (see the header for the
// porting contract): the SkipDosHeader/CoffHeader/PEHeader/ReadSectionHeaders/
// TryCalculateCorHeaderOffset/CalculateMetadataLocation chain over the raw
// image bytes, with the exact .NET BadImageFormatException messages.

#include "Decompiler/Metadata/PEReaderParse.hpp"

#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The .NET 10 SR texts (FxResources.System.Reflection.Metadata.SR.resources).
constexpr const char* kImageTooSmall = "Image is too small.";
constexpr const char* kImageTooSmallOrInvalid =
    "Image is either too small or contains an invalid byte offset or count.";
constexpr const char* kInvalidPESignature = "Invalid PE signature.";
constexpr const char* kUnknownFileFormat = "Unknown file format.";
constexpr const char* kUnknownPEMagicValue = "Unknown PE Magic value.";
constexpr const char* kInvalidNumberOfSections =
    "Invalid number of sections declared in PE header.";
constexpr const char* kInvalidCorHeaderSize = "Invalid COR header size.";
constexpr const char* kSectionTooSmall = "Section too small.";
constexpr const char* kMissingDataDirectory = "Missing data directory.";
constexpr const char* kInvalidMetadataSectionSpan =
    "Invalid metadata section span.";

// The C# `PEHeaders` internal constants.
constexpr std::uint16_t kDosSignature = 0x5A4D; // 23117
constexpr std::uint32_t kPESignature = 0x00004550; // 17744
constexpr std::int32_t kPESignatureOffsetLocation = 60;
constexpr std::uint16_t kPEMagicPE32 = 0x10B;
constexpr std::uint16_t kPEMagicPE32Plus = 0x20B;

// The decompiled `PEBinaryReader`: a position + the fixed max offset over the
// image block. The two CheckBounds variants carry DIFFERENT messages:
// `CheckBounds(uint)` on the fixed-size reads throws "Image is too small."
// while `CheckBounds(long, int)` (the `ReadBytes` and the `Offset` setter)
// throws "Image is either too small or contains an invalid byte offset or
// count." -- the distinction is load-bearing for the failure matrix.
class PeBinaryReader {
public:
    PeBinaryReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::size_t Offset() const { return pos_; }

    // The C# `Offset` setter: `CheckBounds(_startOffset, value)` -- the
    // two-argument variant (a negative value's wrapped uint always
    // overflows).
    void SetOffset(std::int64_t value) {
        const std::uint64_t unsignedValue = static_cast<std::uint64_t>(value);
        if (unsignedValue > size_)
            throw std::invalid_argument(kImageTooSmallOrInvalid);
        pos_ = static_cast<std::size_t>(value);
    }

    std::uint16_t ReadUInt16() {
        Check(2);
        const std::uint16_t v = static_cast<std::uint16_t>(
            data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }

    std::int16_t ReadInt16() { return static_cast<std::int16_t>(ReadUInt16()); }

    std::uint32_t ReadUInt32() {
        Check(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; i++)
            v |= static_cast<std::uint32_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 4;
        return v;
    }

    std::int32_t ReadInt32() { return static_cast<std::int32_t>(ReadUInt32()); }

    std::uint64_t ReadUInt64() {
        Check(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 8;
        return v;
    }

    std::uint8_t ReadByte() {
        Check(1);
        return data_[pos_++];
    }

    // The C# `ReadNullPaddedUTF8(8)` (the section name): `ReadBytes(8)` is
    // the two-argument CheckBounds variant.
    void SkipSectionName() {
        if (pos_ + 8 > size_)
            throw std::invalid_argument(kImageTooSmallOrInvalid);
        pos_ += 8;
    }

private:
    // The C# `CheckBounds(uint count)` (the one-argument variant).
    void Check(std::size_t count) {
        if (pos_ + count > size_)
            throw std::invalid_argument(kImageTooSmall);
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// The decompiled `DirectoryEntry` (two ints).
struct DirectoryEntry {
    std::int32_t RelativeVirtualAddress = 0;
    std::int32_t Size = 0;
};

DirectoryEntry ReadDirectoryEntry(PeBinaryReader& reader) {
    DirectoryEntry entry;
    entry.RelativeVirtualAddress = reader.ReadInt32();
    entry.Size = reader.ReadInt32();
    return entry;
}

// The decompiled `SectionHeader` fields the parse consumes.
struct SectionHeaderInfo {
    std::int32_t VirtualSize = 0;
    std::int32_t VirtualAddress = 0;
    std::int32_t SizeOfRawData = 0;
    std::int32_t PointerToRawData = 0;
};

// The decompiled `ReadSectionHeaders`: the count bound, then the 40-byte
// headers (the name read is the two-argument CheckBounds variant).
void ReadSectionHeaders(PeBinaryReader& reader, std::size_t imageSize,
                        std::size_t coffHeaderEnd,
                        std::int32_t numberOfSections,
                        std::vector<SectionHeaderInfo>& sections) {
    if (numberOfSections < 0
        || static_cast<std::int64_t>(numberOfSections) * 40
            > static_cast<std::int64_t>(imageSize - coffHeaderEnd)) {
        throw std::invalid_argument(kInvalidNumberOfSections);
    }
    for (std::int32_t i = 0; i < numberOfSections; i++) {
        SectionHeaderInfo info;
        reader.SkipSectionName();
        info.VirtualSize = reader.ReadInt32();
        info.VirtualAddress = reader.ReadInt32();
        info.SizeOfRawData = reader.ReadInt32();
        info.PointerToRawData = reader.ReadInt32();
        // PointerToRelocations/PointerToLineNumbers (ints), the
        // relocations/line-numbers counts (ushorts), and the characteristics
        // (uint): 4 + 4 + 2 + 2 + 4 bytes the port discards.
        reader.ReadInt32();
        reader.ReadInt32();
        reader.ReadUInt16();
        reader.ReadUInt16();
        reader.ReadUInt32();
        sections.push_back(info);
    }
}

// The decompiled `TryGetDirectoryOffset(directory, canCrossSectionBoundary:
// false)`: the containing-section lookup, the size-within-section check (the
// `SectionTooSmall` arm), and the non-loaded-image file offset.
bool TryGetDirectoryOffset(
    const std::vector<SectionHeaderInfo>& sections,
    const DirectoryEntry& directory, std::int64_t& offset) {
    std::int64_t containing = -1;
    for (std::size_t i = 0; i < sections.size(); i++) {
        const SectionHeaderInfo& section = sections[i];
        if (section.VirtualAddress <= directory.RelativeVirtualAddress
            && directory.RelativeVirtualAddress
                < section.VirtualAddress + section.VirtualSize) {
            containing = static_cast<std::int64_t>(i);
            break;
        }
    }
    if (containing < 0) {
        offset = -1;
        return false;
    }
    const SectionHeaderInfo& section =
        sections[static_cast<std::size_t>(containing)];
    const std::int64_t num =
        static_cast<std::int64_t>(directory.RelativeVirtualAddress)
        - section.VirtualAddress;
    if (static_cast<std::int64_t>(directory.Size)
        > static_cast<std::int64_t>(section.VirtualSize) - num) {
        throw std::invalid_argument(kSectionTooSmall);
    }
    offset = static_cast<std::int64_t>(section.PointerToRawData) + num;
    return true;
}

// The decompiled `PEHeader` ctor: the full optional-header field walk, whose
// observable effects are the PE32/PE32+ magic gate and the bounds checks of
// every read. The cor header table directory (entry 14) is stored.
void ReadPEHeader(PeBinaryReader& reader, DirectoryEntry& corDirectory) {
    const std::uint16_t magic = reader.ReadUInt16();
    if (magic != kPEMagicPE32 && magic != kPEMagicPE32Plus)
        throw std::invalid_argument(kUnknownPEMagicValue);
    reader.ReadByte(); // MajorLinkerVersion
    reader.ReadByte(); // MinorLinkerVersion
    reader.ReadInt32(); // SizeOfCode
    reader.ReadInt32(); // SizeOfInitializedData
    reader.ReadInt32(); // SizeOfUninitializedData
    reader.ReadInt32(); // AddressOfEntryPoint
    reader.ReadInt32(); // BaseOfCode
    if (magic != kPEMagicPE32Plus)
        reader.ReadInt32(); // BaseOfData (PE32 only)
    if (magic == kPEMagicPE32Plus)
        reader.ReadUInt64(); // ImageBase
    else
        reader.ReadUInt32();
    reader.ReadInt32(); // SectionAlignment
    reader.ReadInt32(); // FileAlignment
    reader.ReadUInt16(); // MajorOperatingSystemVersion
    reader.ReadUInt16(); // MinorOperatingSystemVersion
    reader.ReadUInt16(); // MajorImageVersion
    reader.ReadUInt16(); // MinorImageVersion
    reader.ReadUInt16(); // MajorSubsystemVersion
    reader.ReadUInt16(); // MinorSubsystemVersion
    reader.ReadUInt32(); // Win32VersionValue
    reader.ReadInt32(); // SizeOfImage
    reader.ReadInt32(); // SizeOfHeaders
    reader.ReadUInt32(); // CheckSum
    reader.ReadUInt16(); // Subsystem
    reader.ReadUInt16(); // DllCharacteristics
    if (magic == kPEMagicPE32Plus) {
        reader.ReadUInt64(); // SizeOfStackReserve
        reader.ReadUInt64(); // SizeOfStackCommit
        reader.ReadUInt64(); // SizeOfHeapReserve
        reader.ReadUInt64(); // SizeOfHeapCommit
    } else {
        reader.ReadUInt32();
        reader.ReadUInt32();
        reader.ReadUInt32();
        reader.ReadUInt32();
    }
    reader.ReadUInt32(); // LoaderFlags
    reader.ReadInt32(); // NumberOfRvaAndSizes
    // The 16 directory entries; entry 14 is the cor header table.
    for (int i = 0; i < 16; i++) {
        DirectoryEntry entry = ReadDirectoryEntry(reader);
        if (i == 14)
            corDirectory = entry;
    }
}

} // namespace

MetadataBlock ParsePEReaderHeaders(const std::vector<std::uint8_t>& image) {
    PeBinaryReader reader(image.data(), image.size());

    // The decompiled `SkipDosHeader`.
    const std::uint16_t dosMagic = reader.ReadUInt16();
    bool isCoffOnly;
    if (dosMagic != kDosSignature) {
        if (dosMagic == 0 && reader.ReadUInt16() == 0xFFFF)
            throw std::invalid_argument(kUnknownFileFormat);
        isCoffOnly = true;
        reader.SetOffset(0);
    } else {
        isCoffOnly = false;
    }

    if (!isCoffOnly) {
        reader.SetOffset(kPESignatureOffsetLocation);
        const std::int32_t peHeaderOffset = reader.ReadInt32();
        reader.SetOffset(peHeaderOffset);
        if (reader.ReadUInt32() != kPESignature)
            throw std::invalid_argument(kInvalidPESignature);
    }

    // The decompiled `CoffHeader` (20 bytes).
    reader.ReadUInt16(); // Machine
    const std::int16_t numberOfSections = reader.ReadInt16();
    reader.ReadInt32(); // TimeDateStamp
    reader.ReadInt32(); // PointerToSymbolTable
    reader.ReadInt32(); // NumberOfSymbols
    const std::int16_t sizeOfOptionalHeader = reader.ReadInt16();
    reader.ReadUInt16(); // Characteristics

    if (isCoffOnly) {
        // The COFF-only arm: `SizeOfOptionalHeader != 0` is the
        // UnknownFileFormat rejection; otherwise the .cormeta section is the
        // only metadata source and the port does not read it (the documented
        // divergence), so the image answers "no metadata".
        if (sizeOfOptionalHeader != 0)
            throw std::invalid_argument(kUnknownFileFormat);
        std::vector<SectionHeaderInfo> sections;
        ReadSectionHeaders(reader, image.size(), reader.Offset(),
                           numberOfSections, sections);
        return MetadataBlock{};
    }

    // The decompiled `PEHeader` (the optional header).
    DirectoryEntry corDirectory;
    ReadPEHeader(reader, corDirectory);

    std::vector<SectionHeaderInfo> sections;
    ReadSectionHeaders(reader, image.size(), reader.Offset(), numberOfSections,
                       sections);

    // The decompiled `TryCalculateCorHeaderOffset` + the CorHeader ctor: the
    // cor directory entry resolves through the section table (no crossing),
    // then the 72-byte minimum, then the cor header itself is read.
    std::int64_t corHeaderStartOffset = 0;
    bool hasCorHeader = false;
    DirectoryEntry metadataDirectory;
    if (TryGetDirectoryOffset(sections, corDirectory, corHeaderStartOffset)) {
        if (corDirectory.Size < 72)
            throw std::invalid_argument(kInvalidCorHeaderSize);
        reader.SetOffset(corHeaderStartOffset);
        reader.ReadInt32(); // cb
        reader.ReadUInt16(); // MajorRuntimeVersion
        reader.ReadUInt16(); // MinorRuntimeVersion
        metadataDirectory = ReadDirectoryEntry(reader); // MetadataDirectory
        reader.ReadUInt32(); // Flags
        reader.ReadInt32(); // EntryPointTokenOrRelativeVirtualAddress
        // Resources/StrongNameSignature/CodeManagerTable/VtableFixups/
        // ExportAddressTableJumps/ManagedNativeHeader directories.
        for (int i = 0; i < 6; i++)
            reader.ReadInt32(), reader.ReadInt32();
        hasCorHeader = true;
    }

    // The decompiled `CalculateMetadataLocation` (the non-COFF-only arm).
    MetadataBlock block;
    if (hasCorHeader) {
        std::int64_t start = 0;
        if (!TryGetDirectoryOffset(sections, metadataDirectory, start))
            throw std::invalid_argument(kMissingDataDirectory);
        const std::int64_t size = metadataDirectory.Size;
        if (start < 0 || start >= static_cast<std::int64_t>(image.size())
            || size <= 0
            || start > static_cast<std::int64_t>(image.size()) - size) {
            throw std::invalid_argument(kInvalidMetadataSectionSpan);
        }
        block.offset = static_cast<std::size_t>(start);
        block.size = static_cast<std::size_t>(size);
    }
    // hasCorHeader == false keeps the zero block (the C#
    // `_corHeader == null -> start = 0, size = 0` arm).
    return block;
}

} // namespace ILSpy::Decompiler::Metadata
