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
#include "Decompiler/Util/Span.hpp"

#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

using winmd::impl::image_dos_header;
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
    }

    bool Valid() const noexcept { return sections_ != nullptr; }
    const std::uint8_t* Data() const noexcept { return bytes_->data(); }
    std::size_t Size() const noexcept { return bytes_->size(); }

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

private:
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    const image_section_header* sections_ = nullptr;
    std::uint32_t sectionCount_ = 0;
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

        if (!isFat) {
            // Tiny: (codeSize << 2) | 0x02. Single-byte header.
            codeSize = header0 >> 2;
            ilBase = p + 1;
        } else {
            // Fat: 12-byte header. Bytes 0-1 = Flags(12) | Size(4); Size is the
            // header length in 4-byte units (3 -> 12 bytes).
            if (pe_.Size() < static_cast<std::size_t>(p - pe_.Data()) + 12) return body;
            std::uint32_t first16 = ReadLe<2>(p);
            std::uint32_t hdrWords = (first16 >> 12) & 0xF;
            std::uint32_t hdrBytes = hdrWords * 4;
            if (hdrBytes < 12) return body;
            moreSects = (first16 & 0x8) != 0;
            maxStack = ReadLe<2>(p + 2);
            codeSize = ReadLe<4>(p + 4);
            localVarSigTok = ReadLe<4>(p + 8);
            ilBase = p + hdrBytes;
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
                   maxStack, codeSize, localVarSigTok, isFat);
        return body;
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> image_;
    PeImage pe_;
};

} // namespace ILSpy::Decompiler::Metadata
