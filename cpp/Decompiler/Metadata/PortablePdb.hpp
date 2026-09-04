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

// The portable-PDB debug-table reader: the C++ port's stand-in for reading a
// standalone portable PDB through System.Reflection.Metadata (the
// MetadataReaderProvider.FromPortablePdbStream / MetadataReader surface the
// C# PdbProvider chain consumes).
//
// A portable PDB is a metadata-only file: the CLI metadata root (the "BSJB"
// signature, II.24.2.1) sits at file offset 0 with no PE wrapper, so the
// vendored winmd reader (which requires a DOS/PE container) cannot open it.
// This reader is port-authored per PORT_PLAN.md 5.1 and mirrors the pinned
// System.Reflection.Metadata semantics exactly:
//   - the #Pdb stream (ID, entry point, the referenced type-system table row
//     counts that size the columns pointing into the assembly's tables),
//   - the eight debug tables (0x30-0x37) with SRM's column-width rules,
//   - the sequence-point blob decode (SequencePointCollection), the document
//     name blob decode (BlobHeap.GetDocumentName), and the local-scope
//     range lookups (GetLocalScopeRange / GetLocalVariableRange).
//
// Construction never throws: a malformed file leaves IsValid() false and the
// reads return empty results, matching the decompiler's graceful-degradation
// convention (the C# DebugInfoUtils swallows BadImageFormatException the same
// way). The decoders throw std::out_of_range for truncated or out-of-range
// data, the port's convention for the C# Throw.* paths (the provider catches
// and degrades, as the C# GetSequencePoints catch does).

#pragma once

#include "Decompiler/Util/Span.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The portable-PDB debug tables (the Portable PDB spec; winmd's table set
// stops at 0x27 and carries none of these).
enum class PdbTable : std::uint8_t {
    Document = 0x30,
    MethodDebugInformation = 0x31,
    LocalScope = 0x32,
    LocalVariable = 0x33,
    LocalConstant = 0x34,
    ImportScope = 0x35,
    StateMachineMethod = 0x36,
    CustomDebugInformation = 0x37,
};

// One Document row: the name is a blob (the separator-and-parts encoding
// GetDocumentName decodes), the hash algorithm and language are #GUID heap
// indexes (1-based; 0 = Guid.Empty), the hash is a blob.
struct PdbDocumentRow {
    std::uint32_t NameBlob = 0;
    std::uint32_t HashAlgorithmGuid = 0;
    std::uint32_t HashBlob = 0;
    std::uint32_t LanguageGuid = 0;
};

// One MethodDebugInformation row, keyed by the METHOD DEF row number (the
// table is one-row-per-method; method N's debug info is row N). A zero
// Document means the sequence-point blob names its own document; a zero
// SequencePoints blob means the method has no sequence points.
struct PdbMethodDebugInformationRow {
    std::uint32_t Document = 0;
    std::uint32_t SequencePointsBlob = 0;
};

// One LocalScope row. Method is a 1-based MethodDef row of the ASSEMBLY the
// PDB was generated for (sized from the #Pdb stream's referenced row counts,
// not from any table in this file). VariablesList/ConstantsStart are 1-based
// starts of this scope's LocalVariable/LocalConstant row ranges (0 = none).
struct PdbLocalScopeRow {
    std::uint32_t Method = 0;
    std::uint32_t ImportScope = 0;
    std::uint32_t VariablesStart = 0;
    std::uint32_t ConstantsStart = 0;
    std::int32_t StartOffset = 0;
    std::int32_t Length = 0;
    std::int32_t EndOffset() const noexcept { return StartOffset + Length; }
};

// One LocalVariable row (LocalVariableAttributes: 0 = None, 1 =
// DebuggerHidden; the raw column bits).
struct PdbLocalVariableRow {
    std::uint16_t Attributes = 0;
    std::uint16_t Index = 0;
    std::uint32_t Name = 0;  // #Strings heap offset
};

// One LocalConstant row: the name is a #Strings offset, the signature a blob
// (a type signature the provider decodes; carried raw here).
struct PdbLocalConstantRow {
    std::uint32_t Name = 0;
    std::uint32_t Signature = 0;  // #Blob heap offset
};

// One ImportScope row: Parent is a 1-based ImportScope row (0 = the root
// scope of the method), Imports a blob (the ImportDefinition list).
struct PdbImportScopeRow {
    std::uint32_t Parent = 0;
    std::uint32_t Imports = 0;  // #Blob heap offset
};

// One StateMachineMethod row: both columns are 1-based MethodDef rows of the
// assembly (the async-iterator kickoff relation).
struct PdbStateMachineMethodRow {
    std::uint32_t MoveNextMethod = 0;
    std::uint32_t KickoffMethod = 0;
};

// One CustomDebugInformation row: the parent is a full metadata token (the
// HasCustomDebugInformation coded index already resolved through SRM's
// 27-entry tag map), Kind a #GUID heap index, Value a blob.
struct PdbCustomDebugInformationRow {
    std::uint32_t ParentToken = 0;
    std::uint32_t KindGuid = 0;
    std::uint32_t Value = 0;  // #Blob heap offset
};

// A decoded sequence point (the SRM SequencePoint values; hidden points carry
// the 0xfeefee line marker in both line fields and zero columns).
struct PdbSequencePoint {
    std::uint32_t Document = 0;   // 1-based Document row; 0 = nil
    std::uint32_t Offset = 0;
    std::int32_t StartLine = 0;
    std::int32_t EndLine = 0;
    std::int32_t StartColumn = 0;
    std::int32_t EndColumn = 0;

    static constexpr std::int32_t HiddenLine = 0xfeefee;
    bool IsHidden() const noexcept { return StartLine == HiddenLine; }
};

// A [first, last] row range, 1-based and inclusive. Empty ranges carry
// first=1, last=0 (the SRM range convention).
struct PdbRowRange {
    std::int32_t First = 1;
    std::int32_t Last = 0;
    bool IsEmpty() const noexcept { return First > Last; }
    bool Contains(std::uint32_t row) const noexcept {
        return static_cast<std::int32_t>(row) >= First
            && static_cast<std::int32_t>(row) <= Last;
    }
};

class PortablePdb {
public:
    // Parses a portable-PDB image held in memory. Never throws: a malformed
    // file leaves the instance invalid.
    explicit PortablePdb(std::shared_ptr<const std::vector<std::uint8_t>> bytes);

    // Reads and parses a file. Returns an invalid instance when the file
    // cannot be read or parsed.
    static PortablePdb LoadFile(const std::string& path);
    static bool IsPortablePdbImage(const std::uint8_t* data, std::size_t size) noexcept;

    bool IsValid() const noexcept { return valid_; }
    // The metadata root's version string ("PDB v1.0" for Roslyn PDBs); empty
    // when the file is invalid.
    std::string VersionString() const;

    // --- the #Pdb stream (a standalone PDB's header) ---
    bool HasPdbStream() const noexcept;
    // The 20-byte PDB ID (the CodeView entry's GUID + age) that pairs this
    // PDB with its assembly. Null when the file has no #Pdb stream.
    const std::uint8_t* PdbId() const noexcept;
    // The entry point method token, or 0.
    std::uint32_t EntryPointToken() const noexcept { return entryPointToken_; }
    // A row count of the assembly this PDB was generated for, from the
    // ReferencedTypeSystemTables list (0 for unreferenced tables and when the
    // file has no #Pdb stream). MethodDebugInformation/LocalScope/
    // StateMachineMethod columns that point into the assembly's tables are
    // sized from these counts.
    std::uint32_t ExternalRowCount(std::uint32_t tableNumber) const;

    // --- table counts and row reads (rows are 1-based; a row past the table
    //     reads as all-zero, matching the C# nil-handle shapes) ---
    std::uint32_t RowCount(PdbTable table) const;
    PdbDocumentRow GetDocument(std::uint32_t row) const;
    // The MethodDebugInformation row of a MethodDef row (the tables are
    // row-aligned: method N's debug info is row N; methods beyond the table
    // have none).
    PdbMethodDebugInformationRow GetMethodDebugInformation(std::uint32_t methodRow) const;
    PdbLocalScopeRow GetLocalScope(std::uint32_t row) const;
    PdbLocalVariableRow GetLocalVariable(std::uint32_t row) const;
    PdbLocalConstantRow GetLocalConstant(std::uint32_t row) const;
    PdbImportScopeRow GetImportScope(std::uint32_t row) const;
    PdbStateMachineMethodRow GetStateMachineMethod(std::uint32_t row) const;
    PdbCustomDebugInformationRow GetCustomDebugInformation(std::uint32_t row) const;

    // --- heap reads ---
    // The NUL-terminated UTF-8 string at a #Strings heap offset ("" for 0).
    std::string GetString(std::uint32_t offset) const;
    // The blob (compressed length prefix consumed) at a #Blob heap offset.
    // An empty span for offset 0 / out-of-range offsets.
    Util::Span<const std::uint8_t> GetBlob(std::uint32_t offset) const;
    // The #GUID heap read (MetadataReader.GetGuid): a nil index (0) is
    // Guid.Empty (all zeros); index N reads the 16 bytes at (N-1)*16.
    // nullopt for an out-of-range index or a heap absent from the file.
    std::optional<std::array<std::uint8_t, 16>> TryGetGuid(std::uint32_t index) const noexcept;

    // --- decoders ---
    // BlobHeap.GetDocumentName: a separator byte (0 = none) followed by
    // compressed #Blob offsets, each a UTF-8 part, joined by the separator.
    // Throws std::out_of_range when the separator byte is > 127 (the C#
    // BadImageFormatException on an invalid document name).
    std::string GetDocumentName(std::uint32_t nameBlobOffset) const;

    // The sequence points of a MethodDef row, in order (SequencePointCollection
    // semantics: hidden points, document switches, the mid-point offset
    // deltas). Empty when the method has no debug information. Throws
    // std::out_of_range for truncated blobs and out-of-range line/column
    // values (the C# Throw.SequencePointValueOutOfRange paths).
    std::vector<PdbSequencePoint> GetSequencePoints(std::uint32_t methodRow) const;
    // The local-signature StandaloneSig row encoded as the first compressed
    // integer of the sequence-point blob (0 when the method has no sequence
    // points).
    std::uint32_t GetLocalSignature(std::uint32_t methodRow) const;

    // The LocalScope rows of a method, in table order (the table is sorted
    // by Method). Empty when the method has no scopes.
    PdbRowRange GetLocalScopeRange(std::uint32_t methodRow) const;
    // The LocalVariable/LocalConstant rows of a LocalScope row: from the
    // scope's own list start to the next scope row's start - 1, through the
    // table end for the last scope row. A zero own start is an empty range.
    PdbRowRange GetLocalVariableRange(std::uint32_t scopeRow) const;
    PdbRowRange GetLocalConstantRange(std::uint32_t scopeRow) const;

    // The kickoff MethodDef row of a state machine's MoveNext method, or 0
    // when the method is not a state machine's MoveNext.
    std::uint32_t FindStateMachineKickoffMethod(std::uint32_t moveNextMethodRow) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    bool valid_ = false;
    std::uint32_t entryPointToken_ = 0;

    // Set by the ctor; the decoders consult these (never null when valid_).
    Impl& RequireImpl() const;
};

} // namespace ILSpy::Decompiler::Metadata
