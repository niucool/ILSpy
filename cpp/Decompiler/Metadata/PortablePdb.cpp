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

#include "Decompiler/Metadata/PortablePdb.hpp"

#include <cstring>
#include <fstream>
#include <utility>

namespace ILSpy::Decompiler::Metadata {

namespace {

// Little-endian reads.
std::uint32_t ReadLe2(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
}
std::uint32_t ReadLe4(const std::uint8_t* p) {
    return ReadLe2(p) | (ReadLe2(p + 2) << 16);
}

// The II.23.2 unsigned compressed integer (the SRM BlobReader
// ReadCompressedInteger contract: 1 byte 0x00-0x7F, 2 bytes 0x80-0xBF, 4 bytes
// 0xC0-0xDF; the 0xE0-0xFF prefixes are invalid). Throws std::out_of_range on
// truncation and invalid prefixes (the port's truncated-operand convention).
class BlobCursor {
public:
    BlobCursor(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::size_t Remaining() const { return pos_ < size_ ? size_ - pos_ : 0; }

    std::uint8_t Byte() {
        if (pos_ >= size_) throw std::out_of_range("PDB blob: truncated");
        return data_[pos_++];
    }

    std::uint32_t CompressedUnsigned() {
        std::uint32_t first = Byte();
        if ((first & 0x80) == 0) return first;
        if ((first & 0xC0) == 0x80)
            return ((first & 0x3Fu) << 8) | Byte();
        if ((first & 0xE0) == 0xC0)
            return ((first & 0x1Fu) << 24) | (Byte() << 16) | (Byte() << 8) | Byte();
        throw std::out_of_range("PDB blob: invalid compressed integer");
    }

    // The II.23.2 signed compressed integer: the low bit is the sign, the
    // payload the value rotated left once; a negative value sign-extends
    // across the carrier's payload bits
    // (BlobReader.ReadCompressedSignedInteger).
    std::int32_t CompressedSigned() {
        std::size_t start = pos_;
        std::uint32_t raw = CompressedUnsigned();
        std::size_t bytes = pos_ - start;
        std::int32_t value = static_cast<std::int32_t>(raw >> 1);
        if ((raw & 1) != 0) {
            switch (bytes) {
                case 1: value = static_cast<std::int32_t>(0xFFFFFFC0) | value; break;
                case 2: value = static_cast<std::int32_t>(0xFFFFE000) | value; break;
                default: value = static_cast<std::int32_t>(0xF0000000) | value; break;
            }
        }
        return value;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// The HasCustomDebugInformation coded index: 5 tag bits over the 27
// participating tables. Large (4-byte) form when any participating table's
// row count reaches 2048 (1 << (16 - 5)).
constexpr std::uint32_t kHasCustomDebugInfoTables[] = {
    0x00, 0x01, 0x02, 0x04, 0x06, 0x08, 0x09, 0x0A, 0x0E, 0x11, 0x14,
    0x17, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2B, 0x2C,
    0x30, 0x32, 0x33, 0x34, 0x35,
};

// The tag -> table-number map (the SRM TagToTokenTypeArray, as table numbers
// instead of tokens). Tags 27-31 are invalid (uint.MaxValue in SRM).
constexpr std::uint32_t kHasCustomDebugInfoTags[] = {
    0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17,
    0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A,
    0x2C, 0x2B, 0x30, 0x32, 0x33, 0x34, 0x35,
};
constexpr std::size_t kHasCustomDebugInfoTagCount = 27;

std::optional<std::uint32_t> TagToTableNumber(std::uint32_t tag) {
    if (tag >= kHasCustomDebugInfoTagCount) return std::nullopt;
    return kHasCustomDebugInfoTags[tag];
}

std::uint32_t TableIndex(PdbTable table) {
    return static_cast<std::uint32_t>(table);
}

// The debug tables in table-number order (the #~ stream concatenates the
// present tables' row data in increasing table-number order).
constexpr PdbTable kDebugTables[] = {
    PdbTable::Document, PdbTable::MethodDebugInformation, PdbTable::LocalScope,
    PdbTable::LocalVariable, PdbTable::LocalConstant, PdbTable::ImportScope,
    PdbTable::StateMachineMethod, PdbTable::CustomDebugInformation,
};
constexpr std::size_t kDebugTableCount = 8;

bool IsDebugTable(std::uint32_t tableNumber) {
    return tableNumber >= 0x30 && tableNumber <= 0x37;
}

} // namespace

struct PortablePdb::Impl {
    // The whole image (the reader keeps the bytes alive).
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    // Heap bounds (offsets are relative to the image start; the metadata root
    // sits at file offset 0).
    const std::uint8_t* stringsBase = nullptr;
    const std::uint8_t* stringsEnd = nullptr;
    const std::uint8_t* blobBase = nullptr;
    const std::uint8_t* blobEnd = nullptr;
    const std::uint8_t* guidBase = nullptr;
    const std::uint8_t* guidEnd = nullptr;

    // The #Pdb stream (null when absent).
    const std::uint8_t* pdbBase = nullptr;
    const std::uint8_t* pdbEnd = nullptr;

    std::string version;

    // The referenced type-system table row counts from the #Pdb stream, in
    // table-number order (all zero when the file has no #Pdb stream).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> externalRowCounts;

    // The heap-reference widths the column layout is computed from.
    std::uint32_t stringIdx = 2;
    std::uint32_t guidIdx = 2;
    std::uint32_t blobIdx = 2;
    std::uint32_t methodRefSize = 2;
    std::uint32_t hasCustomDebugInfoSize = 2;

    // Per-debug-table facts, indexed by table number - 0x30.
    struct TableInfo {
        std::uint32_t rowCount = 0;
        std::uint32_t rowSize = 0;
        const std::uint8_t* rows = nullptr;  // the first row's bytes
    };
    TableInfo tables[kDebugTableCount];

    std::uint32_t ExternalCount(std::uint32_t tableNumber) const {
        for (const auto& [table, count] : externalRowCounts)
            if (table == tableNumber) return count;
        return 0;
    }

    // The combined row count the way SRM's CombineRowCounts sees it: the
    // assembly's (#Pdb-sourced) count for the type-system tables, this file's
    // own count for the debug tables.
    std::uint32_t CombinedCount(std::uint32_t tableNumber) const {
        if (IsDebugTable(tableNumber))
            return tables[tableNumber - 0x30].rowCount;
        return ExternalCount(tableNumber);
    }

    // A plain row-index column width (4 bytes once a table holds >= 65536 rows).
    std::uint32_t RefSize(std::uint32_t tableNumber) const {
        return CombinedCount(tableNumber) >= 65536 ? 4 : 2;
    }

    // The row-data pointer; nullptr for a row past the table's end (the C#
    // nil-handle shapes read as zeros).
    const std::uint8_t* RowPtr(PdbTable table, std::uint32_t row) const {
        const TableInfo& info = tables[TableIndex(table) - 0x30];
        if (row == 0 || row > info.rowCount) return nullptr;
        return info.rows + static_cast<std::size_t>(row - 1) * info.rowSize;
    }

    std::uint32_t ReadCol(const std::uint8_t* row, std::uint32_t offset,
                          std::uint32_t width) const {
        if (!row) return 0;
        return width == 4 ? ReadLe4(row + offset) : ReadLe2(row + offset);
    }

    // The #Blob heap read: the entry at an offset is a compressed length
    // followed by the payload.
    Util::Span<const std::uint8_t> BlobAt(std::uint32_t offset) const {
        if (!blobBase) return {};
        std::size_t heapSize = static_cast<std::size_t>(blobEnd - blobBase);
        if (offset >= heapSize) return {};
        const std::uint8_t* p = blobBase + offset;
        std::uint8_t first = p[0];
        std::uint32_t prefixBytes = 0;
        std::uint32_t length = 0;
        if ((first & 0x80) == 0) {
            prefixBytes = 1;
            length = first;
        } else if ((first & 0xC0) == 0x80) {
            if (offset + 1 >= heapSize) return {};
            prefixBytes = 2;
            length = ((first & 0x3Fu) << 8) | p[1];
        } else if ((first & 0xE0) == 0xC0) {
            if (offset + 3 >= heapSize) return {};
            prefixBytes = 4;
            length = ((first & 0x1Fu) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
        } else {
            return {};
        }
        std::uint64_t end = static_cast<std::uint64_t>(offset) + prefixBytes + length;
        if (end > heapSize) return {};
        return Util::Span<const std::uint8_t>(p + prefixBytes, length);
    }
};

PortablePdb::PortablePdb(std::shared_ptr<const std::vector<std::uint8_t>> bytes)
    : impl_(std::make_shared<Impl>()) {
    impl_->bytes = std::move(bytes);
    if (!impl_->bytes) return;
    impl_->data = impl_->bytes->data();
    impl_->size = impl_->bytes->size();
    if (!IsPortablePdbImage(impl_->data, impl_->size)) return;

    const std::uint8_t* base = impl_->data;
    const std::uint8_t* end = base + impl_->size;
    std::uint32_t versionLength = ReadLe4(base + 12);
    const std::uint8_t* p = base + 16;  // the version string
    if (static_cast<std::uint64_t>(16) + versionLength > impl_->size) return;
    // The version string is NUL-terminated inside its (padded) slot.
    const char* versionChars = reinterpret_cast<const char*>(p);
    std::size_t versionLen = 0;
    while (versionLen < versionLength && versionChars[versionLen] != 0) ++versionLen;
    impl_->version.assign(versionChars, versionLen);
    p += versionLength;
    if (p + 4 > end) return;  // flags(2) + stream count(2)
    std::uint32_t streamCount = ReadLe2(p + 2);
    p += 4;

    // The stream headers: offset(4) size(4) name (NUL-terminated, the field
    // padded to a 4-byte boundary). Offsets are relative to the metadata root
    // (the image start).
    const std::uint8_t* tableStream = nullptr;
    std::size_t tableStreamSize = 0;
    for (std::uint32_t i = 0; i < streamCount && p + 8 <= end; ++i) {
        std::uint32_t offset = ReadLe4(p);
        std::uint32_t size = ReadLe4(p + 4);
        const char* name = reinterpret_cast<const char*>(p + 8);
        std::size_t nameLen = 0;
        while (p + 8 + nameLen < end && name[nameLen] != 0) ++nameLen;
        if (p + 8 + nameLen >= end) return;
        std::size_t padded = ((nameLen + 1 + 3) / 4) * 4;
        if (static_cast<std::uint64_t>(offset) + size > impl_->size) return;
        const std::uint8_t* streamData = base + offset;
        if (nameLen == 8 && std::memcmp(name, "#Strings", 8) == 0) {
            impl_->stringsBase = streamData;
            impl_->stringsEnd = streamData + size;
        } else if (nameLen == 5 && std::memcmp(name, "#Blob", 5) == 0) {
            impl_->blobBase = streamData;
            impl_->blobEnd = streamData + size;
        } else if (nameLen == 5 && std::memcmp(name, "#GUID", 5) == 0) {
            impl_->guidBase = streamData;
            impl_->guidEnd = streamData + size;
        } else if (nameLen == 2 && std::memcmp(name, "#~", 2) == 0) {
            tableStream = streamData;
            tableStreamSize = size;
        } else if (nameLen == 4 && std::memcmp(name, "#Pdb", 4) == 0) {
            impl_->pdbBase = streamData;
            impl_->pdbEnd = streamData + size;
        } else if (nameLen == 3 && std::memcmp(name, "#US", 3) == 0) {
            // Present in Roslyn PDBs; unused by the debug-table reads.
        } else {
            return;  // an unknown stream marks an unsupported image
        }
        p += 8 + padded;
    }

    if (!impl_->stringsBase || !impl_->blobBase || !tableStream) return;

    // The #Pdb stream: the 20-byte PDB ID, the entry point token (nil or a
    // MethodDef token), the ReferencedTypeSystemTables bit vector, then a row
    // count per set bit.
    if (impl_->pdbBase) {
        const std::uint8_t* q = impl_->pdbBase;
        if (impl_->pdbEnd - q < 32) return;
        q += 20;  // the PDB ID (exposed via PdbId())
        std::uint32_t entry = ReadLe4(q);
        q += 4;
        if (entry != 0
            && ((entry & 0x7F000000u) != 0x06000000u || (entry & 0x00FFFFFFu) == 0))
            return;
        entryPointToken_ = entry;
        std::uint64_t referenced = static_cast<std::uint64_t>(ReadLe4(q))
            | (static_cast<std::uint64_t>(ReadLe4(q + 4)) << 32);
        q += 8;
        for (std::uint32_t t = 0; t < 64; ++t) {
            if ((referenced & (1ull << t)) == 0) continue;
            if (impl_->pdbEnd - q < 4) return;
            impl_->externalRowCounts.emplace_back(t, ReadLe4(q));
            q += 4;
        }
    }

    // The #~ stream header (II.24.2.6): reserved(4) major(1) minor(1)
    // heap sizes(1) reserved(1) valid(8) sorted(8), then a row count per
    // present table in table-number order, the extra-data word when the
    // HeapSizes ExtraData flag is set, then the rows.
    const std::uint8_t* q = tableStream;
    const std::uint8_t* tEnd = tableStream + tableStreamSize;
    if (tEnd - q < 24) return;
    std::uint32_t heapSizes = q[6];
    std::uint64_t valid = static_cast<std::uint64_t>(ReadLe4(q + 8))
        | (static_cast<std::uint64_t>(ReadLe4(q + 12)) << 32);
    q += 24;

    std::uint32_t localCounts[kDebugTableCount] = {};
    for (std::uint32_t t = 0; t < 64; ++t) {
        if ((valid & (1ull << t)) == 0) continue;
        if (tEnd - q < 4) return;
        std::uint32_t count = ReadLe4(q);
        q += 4;
        if (count > 0x00FFFFFFu) return;  // the SRM row-count bound
        if (IsDebugTable(t)) {
            localCounts[t - 0x30] = count;
        } else if (count > 0) {
            // A standalone PDB carries only the debug tables; rows of other
            // tables cannot be located without the full table schema.
            return;
        }
    }
    if ((heapSizes & 0x40) != 0) q += 4;  // the ExtraData word

    impl_->stringIdx = (heapSizes & 0x01) != 0 ? 4 : 2;
    impl_->guidIdx = (heapSizes & 0x02) != 0 ? 4 : 2;
    impl_->blobIdx = (heapSizes & 0x04) != 0 ? 4 : 2;
    // The MethodDef-pointing columns are sized from the ASSEMBLY's row count
    // (the #Pdb stream), and the HasCustomDebugInformation coded index from
    // the 2048-row threshold over its 27 participating tables
    // (SRM InitializeTableReaders).
    impl_->methodRefSize = impl_->RefSize(0x06);
    impl_->hasCustomDebugInfoSize = 2;
    for (std::uint32_t t : kHasCustomDebugInfoTables) {
        if (impl_->CombinedCount(t) >= 2048) {
            impl_->hasCustomDebugInfoSize = 4;
            break;
        }
    }

    // The rows area: the present debug tables' rows concatenated in
    // table-number order (any other table with rows was rejected above).
    std::uint32_t rowSizes[kDebugTableCount] = {};
    std::size_t rowAreaSize = 0;
    for (std::size_t i = 0; i < kDebugTableCount; ++i) {
        std::uint32_t size;
        switch (kDebugTables[i]) {
            case PdbTable::Document:
                size = impl_->blobIdx + impl_->guidIdx + impl_->blobIdx + impl_->guidIdx;
                break;
            case PdbTable::MethodDebugInformation:
                size = impl_->RefSize(0x30) + impl_->blobIdx;
                break;
            case PdbTable::LocalScope:
                size = impl_->methodRefSize + impl_->RefSize(0x35)
                    + impl_->RefSize(0x33) + impl_->RefSize(0x34) + 4 + 4;
                break;
            case PdbTable::LocalVariable:
                size = 2 + 2 + impl_->stringIdx;
                break;
            case PdbTable::LocalConstant:
                size = impl_->stringIdx + impl_->blobIdx;
                break;
            case PdbTable::ImportScope:
                size = impl_->RefSize(0x35) + impl_->blobIdx;
                break;
            case PdbTable::StateMachineMethod:
                size = impl_->methodRefSize * 2;
                break;
            case PdbTable::CustomDebugInformation:
                size = impl_->hasCustomDebugInfoSize + impl_->guidIdx + impl_->blobIdx;
                break;
            default:
                size = 0;
                break;
        }
        rowSizes[i] = size;
        rowAreaSize += static_cast<std::size_t>(size) * localCounts[i];
    }
    if (static_cast<std::size_t>(tEnd - q) < rowAreaSize) return;

    for (std::size_t i = 0; i < kDebugTableCount; ++i) {
        Impl::TableInfo& info = impl_->tables[i];
        info.rowCount = localCounts[i];
        info.rowSize = rowSizes[i];
        info.rows = q;
        q += static_cast<std::size_t>(info.rowSize) * info.rowCount;
    }

    valid_ = true;
}

PortablePdb PortablePdb::LoadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return PortablePdb(nullptr);
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0) return PortablePdb(nullptr);
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    bytes->resize(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes->data()),
           static_cast<std::streamsize>(bytes->size()));
    if (f.gcount() != static_cast<std::streamsize>(bytes->size()))
        return PortablePdb(nullptr);
    return PortablePdb(std::move(bytes));
}

bool PortablePdb::IsPortablePdbImage(const std::uint8_t* data, std::size_t size) noexcept {
    return data && size >= 16 && ReadLe4(data) == 0x424A5342u;  // "BSJB"
}

std::string PortablePdb::VersionString() const {
    return valid_ ? impl_->version : std::string{};
}

bool PortablePdb::HasPdbStream() const noexcept {
    return valid_ && impl_->pdbBase != nullptr;
}

PortablePdb::Impl& PortablePdb::RequireImpl() const {
    return *impl_;
}

const std::uint8_t* PortablePdb::PdbId() const noexcept {
    if (!valid_ || !HasPdbStream()) return nullptr;
    return impl_->pdbBase;
}

std::uint32_t PortablePdb::ExternalRowCount(std::uint32_t tableNumber) const {
    if (!valid_) return 0;
    return impl_->ExternalCount(tableNumber);
}

std::uint32_t PortablePdb::RowCount(PdbTable table) const {
    if (!valid_) return 0;
    return impl_->tables[TableIndex(table) - 0x30].rowCount;
}

PdbDocumentRow PortablePdb::GetDocument(std::uint32_t row) const {
    PdbDocumentRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::Document, row);
    if (!p) return out;
    std::uint32_t b = impl.blobIdx, g = impl.guidIdx;
    out.NameBlob = impl.ReadCol(p, 0, b);
    out.HashAlgorithmGuid = impl.ReadCol(p, b, g);
    out.HashBlob = impl.ReadCol(p, b + g, b);
    out.LanguageGuid = impl.ReadCol(p, b + g + b, g);
    return out;
}

PdbMethodDebugInformationRow PortablePdb::GetMethodDebugInformation(
    std::uint32_t methodRow) const {
    PdbMethodDebugInformationRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::MethodDebugInformation, methodRow);
    if (!p) return out;
    std::uint32_t d = impl.RefSize(0x30);
    out.Document = impl.ReadCol(p, 0, d);
    out.SequencePointsBlob = impl.ReadCol(p, d, impl.blobIdx);
    return out;
}

PdbLocalScopeRow PortablePdb::GetLocalScope(std::uint32_t row) const {
    PdbLocalScopeRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::LocalScope, row);
    if (!p) return out;
    std::uint32_t m = impl.methodRefSize;
    std::uint32_t i = impl.RefSize(0x35);
    std::uint32_t v = impl.RefSize(0x33);
    std::uint32_t c = impl.RefSize(0x34);
    out.Method = impl.ReadCol(p, 0, m);
    out.ImportScope = impl.ReadCol(p, m, i);
    out.VariablesStart = impl.ReadCol(p, m + i, v);
    out.ConstantsStart = impl.ReadCol(p, m + i + v, c);
    out.StartOffset = static_cast<std::int32_t>(impl.ReadCol(p, m + i + v + c, 4));
    out.Length = static_cast<std::int32_t>(impl.ReadCol(p, m + i + v + c + 4, 4));
    return out;
}

PdbLocalVariableRow PortablePdb::GetLocalVariable(std::uint32_t row) const {
    PdbLocalVariableRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::LocalVariable, row);
    if (!p) return out;
    out.Attributes = static_cast<std::uint16_t>(impl.ReadCol(p, 0, 2));
    out.Index = static_cast<std::uint16_t>(impl.ReadCol(p, 2, 2));
    out.Name = impl.ReadCol(p, 4, impl.stringIdx);
    return out;
}

PdbLocalConstantRow PortablePdb::GetLocalConstant(std::uint32_t row) const {
    PdbLocalConstantRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::LocalConstant, row);
    if (!p) return out;
    out.Name = impl.ReadCol(p, 0, impl.stringIdx);
    out.Signature = impl.ReadCol(p, impl.stringIdx, impl.blobIdx);
    return out;
}

PdbImportScopeRow PortablePdb::GetImportScope(std::uint32_t row) const {
    PdbImportScopeRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::ImportScope, row);
    if (!p) return out;
    std::uint32_t i = impl.RefSize(0x35);
    out.Parent = impl.ReadCol(p, 0, i);
    out.Imports = impl.ReadCol(p, i, impl.blobIdx);
    return out;
}

PdbStateMachineMethodRow PortablePdb::GetStateMachineMethod(std::uint32_t row) const {
    PdbStateMachineMethodRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::StateMachineMethod, row);
    if (!p) return out;
    std::uint32_t m = impl.methodRefSize;
    out.MoveNextMethod = impl.ReadCol(p, 0, m);
    out.KickoffMethod = impl.ReadCol(p, m, m);
    return out;
}

PdbCustomDebugInformationRow PortablePdb::GetCustomDebugInformation(
    std::uint32_t row) const {
    PdbCustomDebugInformationRow out;
    if (!valid_) return out;
    const Impl& impl = RequireImpl();
    const std::uint8_t* p = impl.RowPtr(PdbTable::CustomDebugInformation, row);
    if (!p) return out;
    std::uint32_t h = impl.hasCustomDebugInfoSize;
    std::uint32_t raw = impl.ReadCol(p, 0, h);
    // The coded index: 5 tag bits over the HasCustomDebugInformation table
    // order, the rest the 1-based row (the SRM ConvertToHandle; the parent
    // reads as the full metadata token).
    std::uint32_t tag = raw & 0x1Fu;
    std::uint32_t rid = raw >> 5;
    auto table = TagToTableNumber(tag);
    if (table && rid != 0 && (rid & 0xFF000000u) == 0)
        out.ParentToken = (*table << 24) | rid;
    out.KindGuid = impl.ReadCol(p, h, impl.guidIdx);
    out.Value = impl.ReadCol(p, h + impl.guidIdx, impl.blobIdx);
    return out;
}

std::string PortablePdb::GetString(std::uint32_t offset) const {
    if (!valid_) return {};
    const Impl& impl = RequireImpl();
    if (!impl.stringsBase || offset >= static_cast<std::size_t>(impl.stringsEnd - impl.stringsBase))
        return {};
    const char* s = reinterpret_cast<const char*>(impl.stringsBase) + offset;
    std::size_t len = 0;
    while (s + len < reinterpret_cast<const char*>(impl.stringsEnd) && s[len] != 0) ++len;
    return std::string(s, len);
}

Util::Span<const std::uint8_t> PortablePdb::GetBlob(std::uint32_t offset) const {
    if (!valid_) return {};
    return RequireImpl().BlobAt(offset);
}

std::optional<std::array<std::uint8_t, 16>> PortablePdb::TryGetGuid(
    std::uint32_t index) const noexcept {
    if (!valid_) return std::nullopt;
    std::array<std::uint8_t, 16> out{};
    if (index == 0) return out;  // Guid.Empty
    const Impl& impl = *impl_;
    if (!impl.guidBase) return std::nullopt;
    std::uint64_t offset = (static_cast<std::uint64_t>(index) - 1) * 16;
    if (offset + 16 > static_cast<std::uint64_t>(impl.guidEnd - impl.guidBase))
        return std::nullopt;
    std::memcpy(out.data(), impl.guidBase + offset, 16);
    return out;
}

std::string PortablePdb::GetDocumentName(std::uint32_t nameBlobOffset) const {
    if (!valid_) return {};
    const Impl& impl = RequireImpl();
    Util::Span<const std::uint8_t> name = impl.BlobAt(nameBlobOffset);
    if (name.empty()) return {};
    BlobCursor cursor(name.data(), name.size());
    std::uint32_t separator = cursor.Byte();
    if (separator > 127)
        throw std::out_of_range("PDB document name: invalid separator byte");
    std::string result;
    bool first = true;
    while (cursor.Remaining() > 0) {
        if (separator != 0 && !first)
            result.push_back(static_cast<char>(separator));
        std::uint32_t partOffset = cursor.CompressedUnsigned();
        Util::Span<const std::uint8_t> part = impl.BlobAt(partOffset);
        result.append(reinterpret_cast<const char*>(part.data()), part.size());
        first = false;
    }
    return result;
}

std::vector<PdbSequencePoint> PortablePdb::GetSequencePoints(
    std::uint32_t methodRow) const {
    std::vector<PdbSequencePoint> points;
    if (!valid_) return points;
    const Impl& impl = RequireImpl();
    PdbMethodDebugInformationRow mdi = GetMethodDebugInformation(methodRow);
    if (mdi.SequencePointsBlob == 0) return points;

    Util::Span<const std::uint8_t> blob = impl.BlobAt(mdi.SequencePointsBlob);
    if (blob.empty()) return points;
    BlobCursor cursor(blob.data(), blob.size());

    // The SequencePointCollection.Enumerator algorithm: the blob opens with
    // the local-signature StandaloneSig row (a compressed integer consumed
    // here and exposed separately by GetLocalSignature); the first point then
    // carries its ABSOLUTE offset, later points carry deltas where a zero
    // delta switches the document (a compressed Document row id follows each
    // zero delta). Line/column values are absolute for the first non-hidden
    // point and signed deltas afterwards; deltaLines == 0 makes the column
    // delta unsigned, and both deltas zero marks a hidden point.
    std::uint32_t document = mdi.Document;
    std::int32_t offset = -1;
    std::int32_t previousNonHiddenStartLine = -1;
    std::int32_t previousNonHiddenStartColumn = 0;

    auto addOffsets = [](std::int32_t value, std::int32_t delta) {
        std::int32_t sum = value + delta;
        if (sum < 0)
            throw std::out_of_range("PDB sequence point: offset underflow");
        return sum;
    };
    auto addLines = [](std::int32_t value, std::int32_t delta) {
        std::int32_t sum = value + delta;
        if (sum < 0 || sum >= PdbSequencePoint::HiddenLine)
            throw std::out_of_range("PDB sequence point: line out of range");
        return sum;
    };
    auto addColumns = [](std::int32_t value, std::int32_t delta) {
        std::int32_t sum = value + delta;
        if (sum < 0 || sum >= 65535)
            throw std::out_of_range("PDB sequence point: column out of range");
        return sum;
    };
    auto readDocument = [&]() {
        std::uint32_t row = cursor.CompressedUnsigned();
        if (row == 0 || row > 0x00FFFFFFu)
            throw std::out_of_range("PDB sequence point: invalid document row");
        document = row;
    };

    bool firstPoint = true;
    while (cursor.Remaining() > 0) {
        std::int32_t delta;
        if (firstPoint) {
            cursor.CompressedUnsigned();  // the local-signature token
            if (document == 0) readDocument();
            offset = static_cast<std::int32_t>(cursor.CompressedUnsigned());
            firstPoint = false;
        } else {
            while ((delta = static_cast<std::int32_t>(cursor.CompressedUnsigned())) == 0)
                readDocument();
            offset = addOffsets(offset, delta);
        }

        std::int32_t deltaLines = static_cast<std::int32_t>(cursor.CompressedUnsigned());
        std::int32_t deltaColumns =
            deltaLines == 0
                ? static_cast<std::int32_t>(cursor.CompressedUnsigned())
                : cursor.CompressedSigned();

        PdbSequencePoint point;
        point.Document = document;
        point.Offset = static_cast<std::uint32_t>(offset);
        if (deltaLines == 0 && deltaColumns == 0) {
            // A hidden sequence point: both line fields carry the marker.
            point.StartLine = PdbSequencePoint::HiddenLine;
            point.EndLine = PdbSequencePoint::HiddenLine;
            point.StartColumn = 0;
            point.EndColumn = 0;
        } else {
            std::int32_t startLine, startColumn;
            if (previousNonHiddenStartLine < 0) {
                startLine = static_cast<std::int32_t>(cursor.CompressedUnsigned());
                std::int32_t raw = static_cast<std::int32_t>(cursor.CompressedUnsigned());
                if (raw > 65535)
                    throw std::out_of_range("PDB sequence point: column out of range");
                startColumn = raw;
            } else {
                startLine = addLines(previousNonHiddenStartLine, cursor.CompressedSigned());
                startColumn = addColumns(previousNonHiddenStartColumn,
                                         cursor.CompressedSigned());
            }
            previousNonHiddenStartLine = startLine;
            previousNonHiddenStartColumn = startColumn;
            point.StartLine = startLine;
            point.StartColumn = startColumn;
            point.EndLine = addLines(startLine, deltaLines);
            point.EndColumn = addColumns(startColumn, deltaColumns);
        }
        points.push_back(point);
    }
    return points;
}

std::uint32_t PortablePdb::GetLocalSignature(std::uint32_t methodRow) const {
    if (!valid_) return 0;
    const Impl& impl = RequireImpl();
    PdbMethodDebugInformationRow mdi = GetMethodDebugInformation(methodRow);
    if (mdi.SequencePointsBlob == 0) return 0;
    Util::Span<const std::uint8_t> blob = impl.BlobAt(mdi.SequencePointsBlob);
    if (blob.empty()) return 0;
    BlobCursor cursor(blob.data(), blob.size());
    return cursor.CompressedUnsigned();
}

PdbRowRange PortablePdb::GetLocalScopeRange(std::uint32_t methodRow) const {
    PdbRowRange range;  // empty by default (first=1, last=0)
    if (!valid_ || methodRow == 0) return range;
    std::uint32_t count = RowCount(PdbTable::LocalScope);
    // The table is sorted by Method; the range is the run of rows whose
    // Method column equals the requested method row (the SRM
    // GetLocalScopeRange binary search, as a linear scan over the sorted
    // column: the run is contiguous, so the scan stops at its end).
    for (std::uint32_t row = 1; row <= count; ++row) {
        if (GetLocalScope(row).Method != methodRow) {
            if (!range.IsEmpty()) break;  // past the run
            continue;                     // not reached the run yet
        }
        if (range.IsEmpty()) {
            range.First = static_cast<std::int32_t>(row);
        }
        range.Last = static_cast<std::int32_t>(row);
    }
    return range;
}

PdbRowRange PortablePdb::GetLocalVariableRange(std::uint32_t scopeRow) const {
    PdbRowRange range;  // empty (first=1, last=0)
    if (!valid_ || scopeRow == 0) return range;
    const Impl& impl = RequireImpl();
    PdbLocalScopeRow scope = GetLocalScope(scopeRow);
    std::uint32_t tableRows =
        impl.tables[TableIndex(PdbTable::LocalVariable) - 0x30].rowCount;
    std::uint32_t scopeRows =
        impl.tables[TableIndex(PdbTable::LocalScope) - 0x30].rowCount;
    if (scope.VariablesStart == 0) return range;  // nil own start -> empty
    range.First = static_cast<std::int32_t>(scope.VariablesStart);
    range.Last = scopeRow == scopeRows
        ? static_cast<std::int32_t>(tableRows)
        : static_cast<std::int32_t>(GetLocalScope(scopeRow + 1).VariablesStart) - 1;
    return range;
}

PdbRowRange PortablePdb::GetLocalConstantRange(std::uint32_t scopeRow) const {
    PdbRowRange range;  // empty (first=1, last=0)
    if (!valid_ || scopeRow == 0) return range;
    const Impl& impl = RequireImpl();
    PdbLocalScopeRow scope = GetLocalScope(scopeRow);
    std::uint32_t tableRows =
        impl.tables[TableIndex(PdbTable::LocalConstant) - 0x30].rowCount;
    std::uint32_t scopeRows =
        impl.tables[TableIndex(PdbTable::LocalScope) - 0x30].rowCount;
    if (scope.ConstantsStart == 0) return range;  // nil own start -> empty
    range.First = static_cast<std::int32_t>(scope.ConstantsStart);
    range.Last = scopeRow == scopeRows
        ? static_cast<std::int32_t>(tableRows)
        : static_cast<std::int32_t>(GetLocalScope(scopeRow + 1).ConstantsStart) - 1;
    return range;
}

std::uint32_t PortablePdb::FindStateMachineKickoffMethod(
    std::uint32_t moveNextMethodRow) const {
    if (!valid_ || moveNextMethodRow == 0) return 0;
    std::uint32_t count = RowCount(PdbTable::StateMachineMethod);
    for (std::uint32_t row = 1; row <= count; ++row) {
        PdbStateMachineMethodRow sm = GetStateMachineMethod(row);
        if (sm.MoveNextMethod == moveNextMethodRow) return sm.KickoffMethod;
    }
    return 0;
}

} // namespace ILSpy::Decompiler::Metadata
