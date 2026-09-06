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

// Internal: the ECMA-335 II.25.4 method-body decoder. Not part of the public
// metadata surface; included only by MetadataFile.cpp. Includes winmd's
// impl:: PE header structs for RVA -> file offset, so this header pulls in
// <windows.h> on Windows -- keep it out of the public MetadataFile.hpp.

#pragma once

#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Metadata/PortablePdb.hpp"
#include "Decompiler/Util/Span.hpp"
#include "Decompiler/Util/Utf.hpp"

#include "Decompiler/Disassembler/ReflectionAttributes.hpp"

#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

using winmd::impl::image_dos_header;
using winmd::impl::image_data_directory;
using winmd::impl::image_cor20_header;
using winmd::impl::image_nt_headers32;
using winmd::impl::image_nt_headers32plus;
using winmd::impl::image_section_header;

// Read a little-endian integer of N bytes from an unaligned address.
template <std::size_t N>
std::uint32_t ReadLe(const std::uint8_t* p);
template <> inline std::uint32_t ReadLe<2>(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
}
template <> inline std::uint32_t ReadLe<3>(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16);
}
template <> inline std::uint32_t ReadLe<4>(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// ECMA-335 II.25.4.3 section kind flags. EHTable is 0x01
// (CorILMethod_Sect_EHTable); the fat-format bit is 0x40, more-sections 0x80.
constexpr std::uint8_t kSectEHTable = 0x01;
constexpr std::uint8_t kSectFatFormat = 0x40;
constexpr std::uint8_t kSectMoreSects = 0x80;

// CorExceptionFlag values (EH clause Flags).
constexpr std::uint32_t kCorEHClauseFilter = 0x01;
constexpr std::uint32_t kCorEHClauseFinally = 0x02;
constexpr std::uint32_t kCorEHClauseFault = 0x04;

// A minimal view of the PE image: just enough to resolve RVAs to file offsets.
// Built once per image; the section table covers every section in the file so
// any RVA (method bodies, metadata, resources) resolves.
class PeImage {
public:
    explicit PeImage(std::shared_ptr<const std::vector<std::uint8_t>> bytes)
        : bytes_(std::move(bytes)) {
        if (!bytes_ || bytes_->size() < sizeof(image_dos_header)) return;
        const auto* base = bytes_->data();
        const auto& dos = *reinterpret_cast<const image_dos_header*>(base);
        if (dos.e_signature != 0x5A4D) return;
        if (bytes_->size() < dos.e_lfanew + sizeof(image_nt_headers32)) return;
        const auto* nt = reinterpret_cast<const image_nt_headers32*>(base + dos.e_lfanew);
        sectionCount_ = nt->FileHeader.NumberOfSections;
        sections_ = reinterpret_cast<const image_section_header*>(
            base + dos.e_lfanew + (nt->OptionalHeader.Magic == 0x20B
                                       ? sizeof(image_nt_headers32plus)
                                       : sizeof(image_nt_headers32)));
        // The optional-header values the disassembler's module-header lines
        // render (.imagebase/.file alignment/.stackreserve/.subsystem --
        // the C# `peFile.Reader.PEHeaders.PEHeader` members; PE32 and PE32+
        // place them at the same offsets but widen ImageBase and
        // SizeOfStackReserve to 8 bytes in PE32+).
        if (nt->OptionalHeader.Magic == 0x20B) {
            const auto& opt = reinterpret_cast<const image_nt_headers32plus*>(
                base + dos.e_lfanew)->OptionalHeader;
            imageBase_ = opt.ImageBase;
            fileAlignment_ = opt.FileAlignment;
            sizeOfStackReserve_ = opt.SizeOfStackReserve;
            subsystem_ = opt.Subsystem;
            debugDirectoryRva_ = opt.DataDirectory[6].VirtualAddress;
            debugDirectorySize_ = opt.DataDirectory[6].Size;
        } else {
            const auto& opt = nt->OptionalHeader;
            imageBase_ = opt.ImageBase;
            fileAlignment_ = opt.FileAlignment;
            sizeOfStackReserve_ = opt.SizeOfStackReserve;
            subsystem_ = opt.Subsystem;
            debugDirectoryRva_ = opt.DataDirectory[6].VirtualAddress;
            debugDirectorySize_ = opt.DataDirectory[6].Size;
        }
        // The cor20 header's Flags field (the C# `peFile.Reader.PEHeaders
        // .CorHeader.Flags`). A COM descriptor RVA of 0 (a non-managed PE)
        // leaves the flags 0.
        std::uint32_t comRva = nt->OptionalHeader.Magic == 0x20B
            ? reinterpret_cast<const image_nt_headers32plus*>(
                  base + dos.e_lfanew)->OptionalHeader.DataDirectory[14].VirtualAddress
            : nt->OptionalHeader.DataDirectory[14].VirtualAddress;
        if (comRva != 0) {
            const auto* cor = reinterpret_cast<const image_cor20_header*>(RvaToPtr(comRva));
            // The cor20 header's Flags and Resources directory -- the
            // C# `peFile.Reader.PEHeaders.CorHeader.Flags` and the
            // `CorHeader.Resources` data directory the embedded-resource
            // read resolves (MetadataResource.TryReadResource).
            if (cor) {
                corFlags_ = cor->Flags;
                resourcesDirectoryRva_ = cor->Resources.VirtualAddress;
            }
        }
    }

    bool Valid() const noexcept { return sections_ != nullptr; }
    const std::uint8_t* Data() const noexcept { return bytes_->data(); }
    std::size_t Size() const noexcept { return bytes_->size(); }

    // The optional-header values the WriteModuleHeader PE lines render. The
    // C# PEHeader members are widened (ImageBase/SizeOfStackReserve are
    // ulong even for a PE32 image); the port carries the widened 64-bit
    // values. Zero when the image is not a valid PE.
    std::uint64_t ImageBase() const noexcept { return imageBase_; }
    std::uint32_t FileAlignment() const noexcept { return fileAlignment_; }
    std::uint64_t SizeOfStackReserve() const noexcept { return sizeOfStackReserve_; }
    std::uint16_t Subsystem() const noexcept { return subsystem_; }
    // The cor20 header's Flags field (the .corflags line). Zero when the
    // image has no COM descriptor.
    std::uint32_t CorFlags() const noexcept { return corFlags_; }
    // The cor20 header's Resources data directory RVA (the C#
    // `CorHeader.Resources.RelativeVirtualAddress`) -- the start of the
    // managed-resources block the embedded ManifestResource rows offset
    // into. Zero when the image has no COM header or no resources
    // directory (the C# `resources.RelativeVirtualAddress <= 0` arm).
    std::uint32_t ResourcesDirectoryRva() const noexcept {
        return resourcesDirectoryRva_;
    }

    // The C# `PEHeaders.GetContainingSectionIndex(int relativeVirtualAddress)`
    // -- the index of the section whose [VirtualAddress, VirtualAddress +
    // VirtualSize) range contains the RVA (-1 when none; the section-data
    // mapping below uses the section's RAW extent, not this one).
    int GetContainingSectionIndex(std::uint32_t rva) const noexcept {
        if (!sections_) return -1;
        for (std::uint32_t i = 0; i < sectionCount_; i++) {
            const auto& s = sections_[i];
            if (s.VirtualAddress <= rva && rva < s.VirtualAddress + s.Misc.VirtualSize)
                return static_cast<int>(i);
        }
        return -1;
    }

    // A section header's 8-byte name field (IMAGE_SIZEOF_SHORT_NAME),
    // trimmed at the first NUL. "" for an out-of-range index.
    std::string SectionName(int index) const {
        if (!sections_ || index < 0
            || static_cast<std::uint32_t>(index) >= sectionCount_)
            return {};
        const char* name = reinterpret_cast<const char*>(sections_[index].Name);
        std::size_t len = 0;
        while (len < 8 && name[len] != 0) ++len;
        return std::string(name, len);
    }

    // The C# `PEReader.GetSectionData(int relativeVirtualAddress)` -- the
    // memory block from the RVA to the END of the containing section's raw
    // data ([PointerToRawData, PointerToRawData + SizeOfRawData)), the block
    // the HasFieldRVA initial-value read slices. Empty when the RVA is in no
    // section or falls past the raw block (a section's virtual-only tail).
    // The view points into this image's bytes (valid while the image is).
    struct SectionDataView {
        const std::uint8_t* base = nullptr;
        std::size_t length = 0;
    };
    SectionDataView GetSectionData(std::uint32_t rva) const noexcept {
        if (!sections_) return {};
        int index = GetContainingSectionIndex(rva);
        if (index < 0) return {};
        const auto& s = sections_[index];
        std::size_t rawAvail = 0;
        if (s.PointerToRawData <= bytes_->size())
            rawAvail = static_cast<std::size_t>(
                std::min<std::uint64_t>(s.SizeOfRawData,
                    bytes_->size() - s.PointerToRawData));
        std::uint64_t num = static_cast<std::uint64_t>(rva) - s.VirtualAddress;
        if (num > rawAvail) return {};
        return { bytes_->data() + s.PointerToRawData + num,
                 static_cast<std::size_t>(rawAvail - num) };
    }

    // The C# `System.Reflection.PortableExecutable.DebugDirectoryEntry` --
    // one IMAGE_DEBUG_DIRECTORY row (28 bytes: Characteristics, which is
    // reserved and must be 0, is validated away by ReadDebugDirectory).
    // Type is the raw int32 field; the Disassembler::DebugDirectoryEntryType
    // enum view of it is the MetadataFile::DebugDirectoryEntryInfo mapping.
    struct DebugDirectoryEntry {
        std::uint32_t Stamp = 0;
        std::uint16_t MajorVersion = 0;
        std::uint16_t MinorVersion = 0;
        std::int32_t Type = 0;
        std::int32_t DataSize = 0;
        std::int32_t DataRelativeVirtualAddress = 0;
        std::int32_t DataPointer = 0;
    };

    // The C# `PEReader.ReadDebugDirectory()` -- the IMAGE_DEBUG_DIRECTORY
    // array the debug data directory (optional-header data directory index
    // 6) points at, the PdbProvider's PDB discovery walks. Empty when the
    // image is invalid or carries no debug directory. Throws
    // std::out_of_range (the C# BadImageFormatException arms) when the
    // directory RVA resolves into no section, the size is not a multiple
    // of the 28-byte entry, the directory block runs past the file, or an
    // entry carries a nonzero Characteristics field (reserved, always 0).
    std::vector<DebugDirectoryEntry> ReadDebugDirectory() const {
        if (!sections_ || debugDirectorySize_ == 0) return {};
        const std::uint8_t* p = RvaToPtr(debugDirectoryRva_);
        if (!p) throw std::out_of_range("Invalid debug directory RVA");
        if (debugDirectorySize_ % 28 != 0)
            throw std::out_of_range("Invalid debug directory size");
        if (static_cast<std::uint64_t>(p - bytes_->data()) + debugDirectorySize_
                > static_cast<std::uint64_t>(bytes_->size()))
            throw std::out_of_range("Debug directory block is truncated");
        std::vector<DebugDirectoryEntry> entries;
        entries.reserve(debugDirectorySize_ / 28);
        for (std::uint32_t off = 0; off < debugDirectorySize_; off += 28) {
            const std::uint8_t* e = p + off;
            if (ReadLe<4>(e) != 0)
                throw std::out_of_range(
                    "Invalid debug directory entry characteristics");
            DebugDirectoryEntry entry{};
            entry.Stamp = ReadLe<4>(e + 4);
            entry.MajorVersion = ReadLe<2>(e + 8);
            entry.MinorVersion = ReadLe<2>(e + 10);
            entry.Type = static_cast<std::int32_t>(ReadLe<4>(e + 12));
            entry.DataSize = static_cast<std::int32_t>(ReadLe<4>(e + 16));
            entry.DataRelativeVirtualAddress =
                static_cast<std::int32_t>(ReadLe<4>(e + 20));
            entry.DataPointer = static_cast<std::int32_t>(ReadLe<4>(e + 24));
            entries.push_back(entry);
        }
        return entries;
    }

    // The C# `GetDebugDirectoryEntryDataBlock` (internal on PEReader): the
    // raw block an entry's data lives in. A file-backed image -- the port is
    // never a loaded image -- reads at the raw FILE POINTER the entry's
    // PointerToRawData field carries (the C# `IsLoadedImage ? DataRVA :
    // DataPointer` non-loaded arm), so the data may sit past the sections
    // (entry-data blobs are not required to live inside any section). The
    // view points into this image's bytes; empty for a zero DataSize.
    // Throws std::out_of_range when the block runs past the file.
    SectionDataView GetDebugDirectoryEntryData(const DebugDirectoryEntry& entry) const {
        std::int64_t start = entry.DataPointer;
        std::int64_t end = start + entry.DataSize;
        if (start < 0 || end > static_cast<std::int64_t>(bytes_->size()))
            throw std::out_of_range("Debug data block is out of range");
        return { bytes_->data() + start,
                 entry.DataSize > 0 ? static_cast<std::size_t>(entry.DataSize) : 0 };
    }

    // The C# `CodeViewDebugDirectoryData` -- the CV_INFO_PDB70 blob a
    // CodeView entry points at: the "RSDS" signature, the PDB's GUID (the
    // raw 16 bytes as stored, the canonical little-endian Guid form), the
    // age, and the null-terminated UTF-8 path. The path is kept as the raw
    // UTF-8 bytes (ASCII in practice; the port converts at boundaries).
    struct CodeViewDebugDirectoryData {
        std::array<std::uint8_t, 16> Guid{};
        std::int32_t Age = 0;
        std::string Path;
    };

    // The C# `PEReader.ReadCodeViewDebugDirectoryData(entry)`. Throws
    // std::invalid_argument when the entry is not a CodeView entry (the
    // C# ArgumentException) and std::out_of_range for a block that is
    // truncated or does not start with the RSDS signature (the C#
    // BadImageFormatException arms). A path with no NUL terminator inside
    // the block yields the whole remaining block as the path (the C#
    // ReadUtf8NullTerminated end-of-blob behavior -- no throw).
    CodeViewDebugDirectoryData ReadCodeViewDebugDirectoryData(
        const DebugDirectoryEntry& entry) const {
        if (entry.Type != static_cast<std::int32_t>(
                Disassembler::DebugDirectoryEntryType::CodeView)) {
            throw std::invalid_argument("entry is not a CodeView entry");
        }
        SectionDataView data = GetDebugDirectoryEntryData(entry);
        if (data.length < 24) throw std::out_of_range("Truncated CodeView data");
        if (data.base[0] != 0x52 || data.base[1] != 0x53
            || data.base[2] != 0x44 || data.base[3] != 0x53) {
            throw std::out_of_range("Unexpected CodeView data signature");
        }
        CodeViewDebugDirectoryData result;
        std::memcpy(result.Guid.data(), data.base + 4, 16);
        result.Age = static_cast<std::int32_t>(ReadLe<4>(data.base + 20));
        const std::uint8_t* p = data.base + 24;
        const std::uint8_t* end = data.base + data.length;
        while (p < end && *p != 0) ++p;
        result.Path.assign(reinterpret_cast<const char*>(data.base + 24),
                           static_cast<std::size_t>(p - (data.base + 24)));
        return result;
    }

    // The C# `PEReader.TryOpenAssociatedPortablePdb(peImagePath,
    // pdbFileStreamProvider, out pdbReaderProvider, out pdbPath)` -- the
    // discovery of the portable PDB associated with this PE image: the
    // portable-CodeView entry's file (the entry's PDB path resolved against
    // the PE image's own directory, opened through the provider and matched
    // against the entry's BlobContentId -- the CV GUID + the entry's Stamp
    // vs the PDB's #Pdb ID), falling back to the embedded-PDB entry (the
    // MPDB blob). True when a matching PDB was opened: `pdbReaderProvider`
    // then holds the parsed reader (invalid when false) and `pdbPath` the
    // file it came from (empty for an embedded PDB, the C# null).
    // Throws std::out_of_range at the end (the C# rethrows the first
    // recorded BadImageFormatException/IOException through
    // ExceptionDispatchInfo -- the port maps the BadImageFormat arms to
    // std::out_of_range) when no PDB opened but one was found and failed
    // to parse or decode: a garbage associated file, an invalid
    // embedded-PDB entry (bad version, wrong signature, size mismatch). A
    // file the provider does not serve or an ID that does not match is NOT
    // an error (false, no throw). The C# Throw.ArgumentNull argument checks
    // have no port analogue (the reference parameters cannot be null).
    // The provider: the C# Func<string, Stream?> -- returns the file's
    // bytes, or null when the file does not exist or should be ignored
    // (the C# FileNotFoundException catch maps the not-found shape onto
    // the same null).
    bool TryOpenAssociatedPortablePdb(const std::string& peImagePath,
        const PdbStreamProvider& pdbFileStreamProvider,
        PortablePdb& pdbReaderProvider, std::string& pdbPath) const;

    // The C# `PEReader.ReadEmbeddedPortablePdbDebugDirectoryData(entry)` --
    // the MPDB blob an EmbeddedPortablePdb entry points at, deflated: the
    // "MPDB" signature, the declared uncompressed size, and the raw-deflate
    // stream, decoded to the parsed PDB reader. Throws
    // std::invalid_argument when the entry is not an EmbeddedPortablePdb
    // entry (the C# ArgumentException) and std::out_of_range for the version
    // checks (MajorVersion < 256 / MinorVersion != 256), a truncated or
    // out-of-file data block, a wrong signature, a bad declared size, and
    // a deflate stream that does not inflate to exactly the declared size
    // consuming the whole block (the C# SizeMismatch/DataTooBig
    // BadImageFormatException arms).
    PortablePdb ReadEmbeddedPortablePdbDebugDirectoryData(
        const DebugDirectoryEntry& entry) const;

    // Resolve an RVA to a file offset, or nullptr if it falls outside every
    // section (e.g. RVA 0 for abstract/extern methods).
    const std::uint8_t* RvaToPtr(std::uint32_t rva) const noexcept {
        if (!sections_) return nullptr;
        for (std::uint32_t i = 0; i < sectionCount_; ++i) {
            const auto& s = sections_[i];
            if (rva >= s.VirtualAddress && rva < s.VirtualAddress + s.Misc.VirtualSize) {
                std::uint32_t offset = rva - s.VirtualAddress + s.PointerToRawData;
                if (offset >= bytes_->size()) return nullptr;
                return bytes_->data() + offset;
            }
        }
        return nullptr;
    }

    // Read a user string from the #US heap (token table 0x70; the row is the
    // byte offset into the heap). Returns an empty string if the heap could
    // not be located or the offset is out of range. The #US heap is located by
    // parsing the CLI metadata root's stream headers (winmd keeps #Strings/#Blob
    // but discards #US, so we re-parse just that stream lazily).
    std::string GetUserString(std::uint32_t token) const noexcept {
        if (!sections_) return {};
        std::uint32_t off = token & 0x00FFFFFFu;
        const std::uint8_t* base = UsBase();
        const std::uint8_t* end = UsEnd();
        if (!base || off >= static_cast<std::size_t>(end - base)) return {};
        const std::uint8_t* p = base + off;
        // ECMA-335 II.23.2: compressed unsigned length (1/2/4 bytes).
        std::uint32_t len = 0;
        std::uint8_t b0 = p[0];
        std::size_t lenBytes = 0;
        if ((b0 & 0x80) == 0) { len = b0; lenBytes = 1; }
        else if ((b0 & 0xC0) == 0x80) { len = ((b0 & 0x3F) << 8) | p[1]; lenBytes = 2; }
        else { len = ((b0 & 0x1F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; lenBytes = 4; }
        if (off + lenBytes + len > static_cast<std::size_t>(end - base) || len < 1) return {};
        const std::uint8_t* chars = p + lenBytes;
        // The last byte of the blob is a trailing flag byte (not part of the
        // string); the rest are UTF-16LE code units. The port's convention is
        // UTF-8 everywhere (PORT_PLAN.md 5.6), so decode both bytes of every
        // unit -- surrogate pairs included -- through the Util converter.
        std::size_t charBytes = len - 1;
        std::u16string units(charBytes / 2, u'\0');
        for (std::size_t i = 0; i + 1 < charBytes; i += 2)
            units[i / 2] = static_cast<char16_t>(
                chars[i] | (static_cast<std::uint16_t>(chars[i + 1]) << 8));
        return Util::Utf16ToUtf8(units);
    }

    // The C# `MetadataReader.GetUserString(UserStringHandle)` returns null for
    // an out-of-range handle (the WriteInstruction String arm renders nothing
    // and prints the null-handle token comment); GetUserString's graceful {}
    // cannot distinguish that from a VALID empty #US row (len == 1). The Try
    // variant carries the distinction: nullopt for the invalid cases (no
    // sections, out-of-bounds offset, a truncated/short row), the string
    // (possibly empty) otherwise. Same no-throw convention.
    std::optional<std::string> TryGetUserString(std::uint32_t token) const noexcept {
        if (!sections_) return std::nullopt;
        std::uint32_t off = token & 0x00FFFFFFu;
        const std::uint8_t* base = UsBase();
        const std::uint8_t* end = UsEnd();
        if (!base || off >= static_cast<std::size_t>(end - base)) return std::nullopt;
        const std::uint8_t* p = base + off;
        std::uint32_t len = 0;
        std::uint8_t b0 = p[0];
        std::size_t lenBytes = 0;
        if ((b0 & 0x80) == 0) { len = b0; lenBytes = 1; }
        else if ((b0 & 0xC0) == 0x80) { len = ((b0 & 0x3F) << 8) | p[1]; lenBytes = 2; }
        else { len = ((b0 & 0x1F) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; lenBytes = 4; }
        if (off + lenBytes + len > static_cast<std::size_t>(end - base) || len < 1)
            return std::nullopt;
        const std::uint8_t* chars = p + lenBytes;
        // Same decode as GetUserString: UTF-16LE units (the trailing flag byte
        // excluded) to UTF-8, both bytes of every unit.
        std::size_t charBytes = len - 1;
        std::u16string units(charBytes / 2, u'\0');
        for (std::size_t i = 0; i + 1 < charBytes; i += 2)
            units[i / 2] = static_cast<char16_t>(
                chars[i] | (static_cast<std::uint16_t>(chars[i + 1]) << 8));
        return Util::Utf16ToUtf8(units);
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    const image_section_header* sections_ = nullptr;
    std::uint32_t sectionCount_ = 0;

    // Lazily located #US heap bounds (mutable: computed on first use).
    mutable const std::uint8_t* usBase_ = nullptr;
    mutable const std::uint8_t* usEnd_ = nullptr;
    mutable bool usLocated_ = false;

    // Lazily located #GUID heap bounds (mutable: computed on first use).
    mutable const std::uint8_t* guidBase_ = nullptr;
    mutable const std::uint8_t* guidEnd_ = nullptr;
    mutable bool guidLocated_ = false;

    // The optional-header values captured during the PE parse (the ctor
    // reads them eagerly; they are constant per image).
    std::uint64_t imageBase_ = 0;
    std::uint32_t fileAlignment_ = 0;
    std::uint64_t sizeOfStackReserve_ = 0;
    std::uint16_t subsystem_ = 0;
    std::uint32_t corFlags_ = 0;
    std::uint32_t resourcesDirectoryRva_ = 0;

    // The debug data directory (optional-header data directory index 6)
    // the ReadDebugDirectory entry array lives at, captured during the PE
    // parse like the other optional-header values.
    std::uint32_t debugDirectoryRva_ = 0;
    std::uint32_t debugDirectorySize_ = 0;

public:
    // The cor20 header's EntryPointTokenOrRelativeVirtualAddress, or 0 when
    // the image has no COM header (the C# `module.CorHeader?... ?? 0`). The
    // token is captured during the cor-header parse (a const loader path, so
    // the field is mutable).
    mutable std::uint32_t entryPointToken_ = 0;
    std::uint32_t EntryPointToken() const { return entryPointToken_; }

    const std::uint8_t* UsBase() const {
        if (!usLocated_) LocateUsHeap();
        return usBase_;
    }
    const std::uint8_t* UsEnd() const {
        if (!usLocated_) LocateUsHeap();
        return usEnd_;
    }

    // The C# `MetadataReader.MetadataVersion` -- the version string of the
    // metadata root's storage signature (ECMA-335 II.24.2.1: the length at
    // root+12, the NUL-padded string at root+16). SRM's ReadMetadataHeader
    // reads it as PeekUtf8NullTerminated over the padded length: the bytes
    // up to the first NUL, or the whole padded block when it has no NUL.
    // Empty when the image has no COM header or the root cannot be located;
    // never throws (the port's never-throws accessor convention; the C#
    // BadImageFormatException arms are unreachable for any file winmd
    // opened, which is every file the port can construct).
    std::string MetadataVersionString() const noexcept {
        const std::uint8_t* root = MetadataRootPtr();
        if (!root) return {};
        const std::uint8_t* base = bytes_->data();
        std::size_t size = bytes_->size();
        if (root + 16 > base + size) return {};
        std::uint32_t versionLength = ReadLe<4>(root + 12);
        if (static_cast<std::uint64_t>(16 + versionLength) >
            static_cast<std::uint64_t>(base + size - root))
            return {};
        const std::uint8_t* str = root + 16;
        std::size_t len = 0;
        while (len < versionLength && str[len] != 0) ++len;
        return std::string(reinterpret_cast<const char*>(str), len);
    }

    // The C# `MetadataReader.GetGuid(GuidHandle)` -- a nil index (0) is the
    // all-zeros GUID (Guid.Empty); a valid index N reads the 16 GUID bytes
    // at heap offset (N-1)*16 (II.24.2.5: GUID heap indexing is 1-based).
    // nullopt for an out-of-range index or a heap absent from the image;
    // never throws.
    std::optional<std::array<std::uint8_t, 16>> TryGetGuid(
        std::uint32_t heapIndex) const noexcept {
        std::array<std::uint8_t, 16> out{};
        if (heapIndex == 0) return out;  // Guid.Empty
        if (!sections_) return std::nullopt;
        if (!guidLocated_) LocateGuidHeap();
        if (!guidBase_) return std::nullopt;
        std::uint64_t offset = (static_cast<std::uint64_t>(heapIndex) - 1) * 16;
        if (offset + 16 > static_cast<std::uint64_t>(guidEnd_ - guidBase_))
            return std::nullopt;
        std::memcpy(out.data(), guidBase_ + offset, 16);
        return out;
    }

    // The PE -> cor20 -> metadata-root walk shared by the lazily located heap
    // bounds and the version string (LocateUsHeap/LocateGuidHeap keep their
    // own copies for the entry-point side effect; this one is the plain
    // locator).
    const std::uint8_t* MetadataRootPtr() const {
        if (!sections_ || !bytes_) return nullptr;
        const std::uint8_t* base = bytes_->data();
        std::size_t size = bytes_->size();
        if (size < sizeof(image_dos_header)) return nullptr;
        const auto& dos = *reinterpret_cast<const image_dos_header*>(base);
        if (dos.e_signature != 0x5A4D) return nullptr;
        if (size < dos.e_lfanew + sizeof(image_nt_headers32)) return nullptr;
        const auto* nt = reinterpret_cast<const image_nt_headers32*>(base + dos.e_lfanew);
        std::uint32_t comRva = 0;
        if (nt->OptionalHeader.Magic == 0x20B) {
            const auto* ntPlus = reinterpret_cast<const image_nt_headers32plus*>(base + dos.e_lfanew);
            comRva = ntPlus->OptionalHeader.DataDirectory[14].VirtualAddress;
        } else {
            comRva = nt->OptionalHeader.DataDirectory[14].VirtualAddress;
        }
        if (comRva == 0) return nullptr;
        const auto* cor = reinterpret_cast<const image_cor20_header*>(RvaToPtr(comRva));
        if (!cor) return nullptr;
        return RvaToPtr(cor->MetaData.VirtualAddress);
    }

    // Locates the #GUID stream by walking the metadata root's stream headers
    // (the same walk LocateUsHeap performs for #US; the body sits below the
    // LocateUsHeap definition).
    void LocateUsHeap() const {
        usLocated_ = true;
        if (!sections_ || !bytes_) return;
        const std::uint8_t* base = bytes_->data();
        std::size_t size = bytes_->size();
        if (size < sizeof(image_dos_header)) return;
        const auto& dos = *reinterpret_cast<const image_dos_header*>(base);
        if (dos.e_signature != 0x5A4D) return;
        if (size < dos.e_lfanew + sizeof(image_nt_headers32)) return;
        const auto* nt = reinterpret_cast<const image_nt_headers32*>(base + dos.e_lfanew);
        // PE32 (0x10B) and PE32+ (0x20B) place the data directories at different
        // optional-header offsets; read the COM descriptor RVA from the right layout
        // (Framework64 assemblies are PE32+).
        std::uint32_t comRva = 0;
        if (nt->OptionalHeader.Magic == 0x20B) {
            const auto* ntPlus = reinterpret_cast<const image_nt_headers32plus*>(base + dos.e_lfanew);
            comRva = ntPlus->OptionalHeader.DataDirectory[14].VirtualAddress;
        } else {
            comRva = nt->OptionalHeader.DataDirectory[14].VirtualAddress;
        }
        if (comRva == 0) return;
        const auto* cor = reinterpret_cast<const image_cor20_header*>(RvaToPtr(comRva));
        if (!cor) return;
        entryPointToken_ = cor->dummyunionname.EntryPointToken;
        std::uint32_t mdRva = cor->MetaData.VirtualAddress;
        const std::uint8_t* root = RvaToPtr(mdRva);
        if (!root) return;
        // ECMA-335 II.24.2.1: signature at +0, version length at +12, stream
        // count at +versionLength+18, stream headers at +versionLength+20.
        if (root + 16 > base + size) return;
        std::uint32_t versionLength = ReadLe<4>(root + 12);
        std::size_t hdrsAt = static_cast<std::size_t>(versionLength + 20);
        if (root + hdrsAt + 2 > base + size) return;
        std::uint32_t streamCount = ReadLe<2>(root + versionLength + 18);
        const std::uint8_t* p = root + hdrsAt;
        const std::uint8_t* imageEnd = base + size;
        for (std::uint32_t i = 0; i < streamCount && p + 8 <= imageEnd; ++i) {
            std::uint32_t sOff = ReadLe<4>(p);
            std::uint32_t sSize = ReadLe<4>(p + 4);
            // Name: null-terminated, padded to a 4-byte boundary.
            const char* name = reinterpret_cast<const char*>(p + 8);
            const char* nameEnd = name;
            while (nameEnd < reinterpret_cast<const char*>(imageEnd) && *nameEnd != 0) ++nameEnd;
            std::size_t nameLen = static_cast<std::size_t>(nameEnd - name);
            if (nameLen == 3 && name[0] == '#' && name[1] == 'U' && name[2] == 'S') {
                usBase_ = root + sOff;
                usEnd_ = (sSize && static_cast<std::size_t>(sOff + sSize) <= static_cast<std::size_t>(imageEnd - root))
                             ? root + sOff + sSize : imageEnd;
                return;
            }
            // Advance past offset(4) + size(4) + name (padded to a 4-byte
            // boundary, the padding already covers the null terminator).
            std::size_t padding = 4 - (nameLen % 4);
            if (padding == 0) padding = 4;
            p += 8 + nameLen + padding;
        }
    }

    // The same metadata-root stream walk for the #GUID heap (the MVID bytes
    // the module header renders).
    void LocateGuidHeap() const {
        guidLocated_ = true;
        if (!sections_ || !bytes_) return;
        const std::uint8_t* base = bytes_->data();
        std::size_t size = bytes_->size();
        if (size < sizeof(image_dos_header)) return;
        const auto& dos = *reinterpret_cast<const image_dos_header*>(base);
        if (dos.e_signature != 0x5A4D) return;
        if (size < dos.e_lfanew + sizeof(image_nt_headers32)) return;
        const auto* nt = reinterpret_cast<const image_nt_headers32*>(base + dos.e_lfanew);
        std::uint32_t comRva = 0;
        if (nt->OptionalHeader.Magic == 0x20B) {
            const auto* ntPlus = reinterpret_cast<const image_nt_headers32plus*>(base + dos.e_lfanew);
            comRva = ntPlus->OptionalHeader.DataDirectory[14].VirtualAddress;
        } else {
            comRva = nt->OptionalHeader.DataDirectory[14].VirtualAddress;
        }
        if (comRva == 0) return;
        const auto* cor = reinterpret_cast<const image_cor20_header*>(RvaToPtr(comRva));
        if (!cor) return;
        const std::uint8_t* root = RvaToPtr(cor->MetaData.VirtualAddress);
        if (!root) return;
        if (root + 16 > base + size) return;
        std::uint32_t versionLength = ReadLe<4>(root + 12);
        std::size_t hdrsAt = static_cast<std::size_t>(versionLength + 20);
        if (root + hdrsAt + 2 > base + size) return;
        std::uint32_t streamCount = ReadLe<2>(root + versionLength + 18);
        const std::uint8_t* p = root + hdrsAt;
        const std::uint8_t* imageEnd = base + size;
        for (std::uint32_t i = 0; i < streamCount && p + 8 <= imageEnd; ++i) {
            std::uint32_t sOff = ReadLe<4>(p);
            std::uint32_t sSize = ReadLe<4>(p + 4);
            const char* name = reinterpret_cast<const char*>(p + 8);
            const char* nameEnd = name;
            while (nameEnd < reinterpret_cast<const char*>(imageEnd) && *nameEnd != 0) ++nameEnd;
            std::size_t nameLen = static_cast<std::size_t>(nameEnd - name);
            if (nameLen == 5 && name[0] == '#' && name[1] == 'G' && name[2] == 'U'
                && name[3] == 'I' && name[4] == 'D') {
                guidBase_ = root + sOff;
                guidEnd_ = (sSize && static_cast<std::size_t>(sOff + sSize) <= static_cast<std::size_t>(imageEnd - root))
                               ? root + sOff + sSize : imageEnd;
                return;
            }
            std::size_t padding = 4 - (nameLen % 4);
            if (padding == 0) padding = 4;
            p += 8 + nameLen + padding;
        }
    }
};

inline ExceptionHandlerKind ClauseKindFromFlags(std::uint32_t flags) {
    if (flags & kCorEHClauseFilter) return ExceptionHandlerKind::Filter;
    if (flags & kCorEHClauseFinally) return ExceptionHandlerKind::Finally;
    if (flags & kCorEHClauseFault) return ExceptionHandlerKind::Fault;
    return ExceptionHandlerKind::Catch;
}

// Decode the EH section(s) following a fat method body's code into clauses.
// Sections are 4-byte aligned and form a linked list via kSectMoreSects. Each
// section's DataSize (24-bit, bytes 1-3 of the header) INCLUDES the 4-byte
// header, so clause bytes = DataSize - 4. Small sections (no 0x40 bit) use
// 12-byte clauses with BYTE-sized Try/Handler lengths; fat sections use
// 24-byte clauses with DWORD fields (ECMA-335 II.25.4.3).
inline void DecodeEhSections(const std::uint8_t* sectionsBase,
                             const std::uint8_t* imageEnd,
                             std::vector<ExceptionHandlerClause>& out) {
    const std::uint8_t* p = sectionsBase;
    while (p + 4 <= imageEnd) {
        std::uint8_t kind = p[0];
        if ((kind & kSectEHTable) == 0) break; // not an EH section
        std::uint32_t dataSize = ReadLe<3>(p + 1);
        const std::uint8_t* clauseData = p + 4;
        std::uint32_t clauseBytes = dataSize >= 4 ? dataSize - 4 : 0;
        if (clauseData + clauseBytes > imageEnd) break;

        bool isFat = (kind & kSectFatFormat) != 0;
        const std::size_t clauseSize = isFat ? 24 : 12;
        for (std::size_t off = 0; off + clauseSize <= clauseBytes; off += clauseSize) {
            const std::uint8_t* c = clauseData + off;
            ExceptionHandlerClause clause{};
            if (isFat) {
                std::uint32_t flags = ReadLe<4>(c);
                clause.TryOffset = ReadLe<4>(c + 4);
                clause.TryLength = ReadLe<4>(c + 8);
                clause.HandlerOffset = ReadLe<4>(c + 12);
                clause.HandlerLength = ReadLe<4>(c + 16);
                clause.ClassTokenOrFilterOffset = ReadLe<4>(c + 20);
                clause.Kind = ClauseKindFromFlags(flags);
            } else {
                // Small clause, 12 bytes packed:
                //   Flags(WORD) TryOffset(WORD) TryLength(BYTE)
                //   HandlerOffset(WORD) HandlerLength(BYTE) ClassToken(DWORD)
                std::uint32_t flags = ReadLe<2>(c);
                clause.TryOffset = ReadLe<2>(c + 2);
                clause.TryLength = c[4];
                clause.HandlerOffset = ReadLe<2>(c + 5);
                clause.HandlerLength = c[7];
                clause.ClassTokenOrFilterOffset = ReadLe<4>(c + 8);
                clause.Kind = ClauseKindFromFlags(flags);
            }
            out.push_back(clause);
        }

        if ((kind & kSectMoreSects) == 0) break;
        p = clauseData + clauseBytes;
        std::size_t rem = (reinterpret_cast<std::uintptr_t>(p)) & 3u;
        if (rem) p += 4 - rem;
    }
}

class MethodBodyReader {
public:
    explicit MethodBodyReader(std::shared_ptr<const std::vector<std::uint8_t>> image)
        : image_(std::move(image)), pe_(image_) {}

    bool HasImage() const noexcept { return pe_.Valid(); }

    // Decode a user-string token (table 0x70) to its text (best-effort: an
    // empty string if the #US heap is absent or the offset is out of range).
    std::string GetUserString(std::uint32_t token) const noexcept {
        return pe_.GetUserString(token);
    }

    std::optional<std::string> TryGetUserString(std::uint32_t token) const noexcept {
        return pe_.TryGetUserString(token);
    }

    // The cor20 header's EntryPointTokenOrRelativeVirtualAddress (0 when the
    // image has no COM header -- the C# `module.CorHeader?... ?? 0`).
    std::uint32_t EntryPointToken() const { return pe_.EntryPointToken(); }

    // The metadata root's version string (the C#
    // `MetadataReader.MetadataVersion`). Straight PeImage passthrough.
    std::string MetadataVersionString() const {
        return pe_.MetadataVersionString();
    }

    // The PE-section reads the ReflectionDisassembler field renderer drives:
    // the containing-section index and name (the `.data` section-kind prefix
    // and the GetRVASectionPrefix walk) and the section-data block (the
    // HasFieldRVA initial-value read). Straight PeImage passthroughs.
    int GetContainingSectionIndex(std::uint32_t rva) const {
        return pe_.GetContainingSectionIndex(rva);
    }
    std::string GetSectionName(int index) const { return pe_.SectionName(index); }
    PeImage::SectionDataView GetSectionData(std::uint32_t rva) const {
        return pe_.GetSectionData(rva);
    }

    // The PE-header values the module-header lines render (the C#
    // `peFile.Reader.PEHeaders.PEHeader`/`CorHeader` members) and the #GUID
    // heap read (MetadataReader.GetGuid). Straight PeImage passthroughs.
    std::uint64_t ImageBase() const noexcept { return pe_.ImageBase(); }
    std::uint32_t FileAlignment() const noexcept { return pe_.FileAlignment(); }
    std::uint64_t SizeOfStackReserve() const noexcept { return pe_.SizeOfStackReserve(); }
    std::uint16_t Subsystem() const noexcept { return pe_.Subsystem(); }
    std::uint32_t CorFlags() const noexcept { return pe_.CorFlags(); }
    // The cor20 header's Resources directory RVA (a straight PeImage
    // passthrough -- the embedded-resource read the ManifestResource
    // surface composes).
    std::uint32_t ResourcesDirectoryRva() const noexcept {
        return pe_.ResourcesDirectoryRva();
    }
    std::optional<std::array<std::uint8_t, 16>> TryGetGuid(
        std::uint32_t heapIndex) const noexcept {
        return pe_.TryGetGuid(heapIndex);
    }

    // The PE debug-directory reads the PdbProvider's PDB discovery composes
    // (the C# `PEReader.ReadDebugDirectory()` /
    // `ReadCodeViewDebugDirectoryData(entry)` pair). Straight PeImage
    // passthroughs; the throw shapes propagate (the C# BadImageFormatException
    // and ArgumentException arms).
    std::vector<PeImage::DebugDirectoryEntry> ReadDebugDirectory() const {
        return pe_.ReadDebugDirectory();
    }
    PeImage::CodeViewDebugDirectoryData ReadCodeViewDebugDirectoryData(
        const PeImage::DebugDirectoryEntry& entry) const {
        return pe_.ReadCodeViewDebugDirectoryData(entry);
    }

    // The associated/embedded portable-PDB discovery (the C#
    // `PEReader.TryOpenAssociatedPortablePdb`), a straight PeImage
    // passthrough (see MethodBodyReader.hpp's PeImage block for the full
    // contract).
    bool TryOpenAssociatedPortablePdb(const std::string& peImagePath,
        const PdbStreamProvider& pdbFileStreamProvider,
        PortablePdb& pdbReaderProvider, std::string& pdbPath) const {
        return pe_.TryOpenAssociatedPortablePdb(
            peImagePath, pdbFileStreamProvider, pdbReaderProvider, pdbPath);
    }

    // The MPDB blob decode (the C#
    // `PEReader.ReadEmbeddedPortablePdbDebugDirectoryData`), a straight
    // PeImage passthrough.
    PortablePdb ReadEmbeddedPortablePdbDebugDirectoryData(
        const PeImage::DebugDirectoryEntry& entry) const {
        return pe_.ReadEmbeddedPortablePdbDebugDirectoryData(entry);
    }

    // Decode the method body at `rva`. Returns an invalid MethodBody if the RVA
    // is 0 (abstract/extern) or the header is malformed -- graceful degradation
    // rather than throwing, matching the decompiler's robustness tenet.
    MethodBody Read(std::uint32_t rva) {
        MethodBody body;
        if (rva == 0 || !pe_.Valid()) return body;
        const std::uint8_t* p = pe_.RvaToPtr(rva);
        if (!p) return body;

        std::uint8_t header0 = p[0];
        bool isFat = (header0 & 0x3) == 0x3;
        const std::uint8_t* ilBase = nullptr;
        std::uint32_t codeSize = 0;
        std::uint32_t maxStack = 8;        // tiny bodies assume 8
        std::uint32_t localVarSigTok = 0;
        bool moreSects = false;
        std::uint32_t headerSize = 0;
        bool initLocals = false;

        if (!isFat) {
            // Tiny: (codeSize << 2) | 0x02. Single-byte header.
            codeSize = header0 >> 2;
            ilBase = p + 1;
            headerSize = 1;
        } else {
            // Fat: 12-byte header. Bytes 0-1 = Flags(12) | Size(4); Size is the
            // header length in 4-byte units (3 -> 12 bytes).
            if (pe_.Size() < static_cast<std::size_t>(p - pe_.Data()) + 12) return body;
            std::uint32_t first16 = ReadLe<2>(p);
            std::uint32_t hdrWords = (first16 >> 12) & 0xF;
            std::uint32_t hdrBytes = hdrWords * 4;
            if (hdrBytes < 12) return body;
            moreSects = (first16 & 0x8) != 0;
            initLocals = (first16 & 0x10) != 0;
            maxStack = ReadLe<2>(p + 2);
            codeSize = ReadLe<4>(p + 4);
            localVarSigTok = ReadLe<4>(p + 8);
            ilBase = p + hdrBytes;
            headerSize = hdrBytes;
        }

        if (ilBase + codeSize > pe_.Data() + pe_.Size()) return body;

        std::shared_ptr<std::vector<ExceptionHandlerClause>> handlers;
        if (moreSects && isFat) {
            handlers = std::make_shared<std::vector<ExceptionHandlerClause>>();
            // Sections start after the code, aligned to a 4-byte boundary from
            // the start of the method body.
            const std::uint8_t* bodyStart = p;
            std::size_t afterCode = static_cast<std::size_t>(ilBase + codeSize - bodyStart);
            std::size_t aligned = (afterCode + 3u) & ~static_cast<std::size_t>(3);
            const std::uint8_t* sectionsBase = bodyStart + aligned;
            const std::uint8_t* imageEnd = pe_.Data() + pe_.Size();
            DecodeEhSections(sectionsBase, imageEnd, *handlers);
        }

        body.Adopt(image_, handlers,
                   Util::Span<const std::uint8_t>(ilBase, codeSize),
                   maxStack, codeSize, localVarSigTok, isFat,
                   headerSize, initLocals);
        return body;
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> image_;
    PeImage pe_;
};

} // namespace ILSpy::Decompiler::Metadata
