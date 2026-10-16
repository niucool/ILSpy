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

// Port of ICSharpCode.ILSpyX/PdbProvider/PortableDebugInfoProvider.cs: the
// IDebugInfoProvider implementation over a portable PDB -- the provider the
// disassembler's ShowSequencePoints rendering and the .locals debug names
// consume, and the first piece of the Phase-8 PdbProvider
// (PORT_PLAN.md section 5.7: the Cecil bridge is replaced by the portable-PDB
// reader).
//
// C#-to-C++ porting decisions:
//  * The C# holds a MetadataReaderProvider and calls GetMetadataReader()
//    per operation (the parse is deferred, so a malformed file surfaces as
//    a caught BadImageFormatException/IOException that flips hasError and
//    yields null). The port's PortablePdb parses eagerly at construction
//    and never throws, so the provider+reader pair collapse into the one
//    reader value and the private Reader() plays the GetMetadataReader
//    role: an invalid reader returns null and flips hasError the same way.
//    A file that parses is never re-read, so the C# error state can only
//    be entered once here (the per-call re-validation is preserved).
//  * MetadataReaderOptions has no port analogue (the reader applies the
//    Default option set unconditionally); the parameter is dropped.
//  * The C# method parameters are MethodDefinitionHandles; the port's
//    provider contract passes raw metadata tokens, so the row is
//    (token & 0x00FFFFFF) -- the C# handle's RowId.
//  * The C# GetSequencePoints catch (BadImageFormatException) -- which
//    discards the points collected so far and returns the empty list --
//    ports as a catch (std::out_of_range): the reader throws that for
//    every malformed-blob path (the C# Throw.* translations).
//  * The C# LocalScopeHandleCollection maps a NIL method row to the WHOLE
//    LocalScope table (its ctor special-cases rowId 0 to [1,RowCount]);
//    the port's scope walks reproduce that quirk.
//  * The C# `string?[]?`/`bool[]?` PdbExtraTypeInfo fields are nullable
//    arrays; the provider's TryGetExtraTypeInfo contract is exactly
//    "either field was set", so the port's optional fields carry it (an
//    engaged optional holding an empty vector is a found result).
//  * Deferred: ToMetadataFile() (its only consumer is the ILSpy GUI's
//    metadata tree) and Dispose() (RAII -- the reader is a value member).

#pragma once

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/Metadata/PortablePdb.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX::PdbProvider {

class PortableDebugInfoProvider final : public Decompiler::DebugInfo::IDebugInfoProvider {
public:
    // The C# ctor(string moduleFileName, MetadataReaderProvider provider,
    // MetadataReaderOptions options, string? pdbFileName): a null
    // pdbFileName means the PDB is embedded in the module. The C# null
    // argument checks have no port analogue (the reader value has no null
    // state; an unparseable file is the Reader() error state).
    PortableDebugInfoProvider(std::string moduleFileName,
        Decompiler::Metadata::PortablePdb pdb,
        std::optional<std::string> pdbFileName = std::nullopt);

    // The C# `public bool IsEmbedded => pdbFileName == null`.
    bool IsEmbedded() const noexcept { return !pdbFileName_.has_value(); }

    // The C# `string Description` -- the load state the UI shows.
    std::string Description() const override;

    // The C# `string SourceFileName` -- the PDB for a standalone PDB, the
    // module itself for an embedded one.
    std::string SourceFileName() const override;

    // The C# `IList<SequencePoint> GetSequencePoints(MethodDefinitionHandle)`:
    // the method's sequence points in offset order, each carrying its
    // document's decoded name (the empty string for a nil document), or
    // the empty list when the reader failed or the blob is malformed.
    std::vector<Decompiler::DebugInfo::SequencePoint> GetSequencePoints(
        std::uint32_t methodToken) const override;

    // The C# `IList<Variable> GetVariables(MethodDefinitionHandle)`: the
    // method's locals across its scopes in table order.
    std::vector<Decompiler::DebugInfo::Variable> GetVariables(
        std::uint32_t methodToken) const override;

    // The C# `bool TryGetName(MethodDefinitionHandle, int, out string)`:
    // the name of the method's local at a slot index.
    bool TryGetName(std::uint32_t methodToken, int index,
        std::string& name) const override;

    // The C# `bool TryGetExtraTypeInfo(MethodDefinitionHandle, int, out
    // PdbExtraTypeInfo)`: the Roslyn CustomDebugInformation of the method's
    // local at a slot index -- the tuple element names and the dynamic
    // flags. False when the local carries neither kind.
    bool TryGetExtraTypeInfo(std::uint32_t methodToken, int index,
        Decompiler::DebugInfo::PdbExtraTypeInfo& extraTypeInfo) const override;

private:
    // The C# GetMetadataReader(): the parsed reader, or null when the PDB
    // failed to parse -- the state the Description reports as an error.
    const Decompiler::Metadata::PortablePdb* Reader() const;

    // The C# MethodDefinitionHandle row of a raw method token.
    static std::uint32_t MethodRow(std::uint32_t methodToken) noexcept {
        return methodToken & 0x00FFFFFFu;
    }

    // The LocalScope rows of a method in table order -- the C#
    // metadata.GetLocalScopes(method) collection, including its nil-row
    // whole-table rule. Invokes fn(row) per scope row (1-based).
    template <typename Fn>
    static void ForEachLocalScope(const Decompiler::Metadata::PortablePdb& pdb,
        std::uint32_t methodRow, Fn&& fn) {
        std::int32_t first = 0;
        std::int32_t last = -1;
        if (methodRow == 0) {
            // The C# LocalScopeHandleCollection maps a nil method to the
            // whole table.
            first = 1;
            last = static_cast<std::int32_t>(
                pdb.RowCount(Decompiler::Metadata::PdbTable::LocalScope));
        } else {
            auto range = pdb.GetLocalScopeRange(methodRow);
            first = range.First;
            last = range.Last;
        }
        for (std::int32_t row = first; row <= last; ++row) {
            fn(static_cast<std::uint32_t>(row));
        }
    }

    std::string moduleFileName_;
    Decompiler::Metadata::PortablePdb pdb_;
    std::optional<std::string> pdbFileName_;
    // The C# hasError state: set by the reads that fail on the reader
    // (mutable -- the const interface reads keep the C# property's
    // side effect).
    mutable bool hasError_ = false;
};

}  // namespace ILSpy::ILSpyX::PdbProvider
