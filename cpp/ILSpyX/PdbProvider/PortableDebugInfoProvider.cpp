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

// Port of ICSharpCode.ILSpyX/PdbProvider/PortableDebugInfoProvider.cs -- see
// the header for the porting decisions. The reads below compose the
// PortablePdb debug-table reader (the MetadataReader stand-in); every
// expected value is pinned against the real C# provider by the test suite
// (tests/ILSpyX/PdbProvider/PortableDebugInfoProvider_Test.cpp).

#include "ILSpyX/PdbProvider/PortableDebugInfoProvider.hpp"

#include "Decompiler/DebugInfo/KnownGuids.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace ILSpy::ILSpyX::PdbProvider {

namespace {

namespace DI = ILSpy::Decompiler::DebugInfo;
namespace Md = ILSpy::Decompiler::Metadata;

// The token table byte of the LocalVariable table (HandleKind.LocalVariable).
constexpr std::uint32_t kLocalVariableTable = 0x33000000u;

// The C# `string.IsNullOrWhiteSpace` of the tuple-name decode, over the
// ASCII whitespace set (a tuple element name is an identifier; the Unicode
// whitespace of the C# check cannot appear in one).
bool IsWhitespaceOrEmpty(const std::string& s) {
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\v' && c != '\f'
            && c != '\r') {
            return false;
        }
    }
    return true;
}

}  // namespace

PortableDebugInfoProvider::PortableDebugInfoProvider(std::string moduleFileName,
    Decompiler::Metadata::PortablePdb pdb, std::optional<std::string> pdbFileName)
    : moduleFileName_(std::move(moduleFileName)),
      pdb_(std::move(pdb)),
      pdbFileName_(std::move(pdbFileName)) {}

const Md::PortablePdb* PortableDebugInfoProvider::Reader() const {
    hasError_ = false;
    if (!pdb_.IsValid()) {
        hasError_ = true;
        return nullptr;
    }
    return &pdb_;
}

std::string PortableDebugInfoProvider::Description() const {
    if (IsEmbedded()) {
        if (hasError_) {
            return "Error while loading the PDB stream embedded in this assembly";
        }
        return "Embedded in this assembly";
    }
    if (hasError_) {
        return "Error while loading portable PDB: " + *pdbFileName_;
    }
    return "Loaded from portable PDB: " + *pdbFileName_;
}

std::string PortableDebugInfoProvider::SourceFileName() const {
    return pdbFileName_.has_value() ? *pdbFileName_ : moduleFileName_;
}

std::vector<DI::SequencePoint> PortableDebugInfoProvider::GetSequencePoints(
    std::uint32_t methodToken) const {
    const Md::PortablePdb* metadata = Reader();
    if (metadata == nullptr) {
        return {};
    }
    std::vector<DI::SequencePoint> sequencePoints;
    try {
        for (const Md::PdbSequencePoint& point :
            metadata->GetSequencePoints(MethodRow(methodToken))) {
            std::string documentFileName;
            if (point.Document != 0) {
                Md::PdbDocumentRow document =
                    metadata->GetDocument(point.Document);
                // The C# metadata.GetString(document.Name) over a
                // DocumentNameHandle is the document-name blob decode.
                documentFileName = metadata->GetDocumentName(document.NameBlob);
            }
            DI::SequencePoint sequencePoint;
            sequencePoint.Offset = static_cast<int>(point.Offset);
            sequencePoint.StartLine = point.StartLine;
            sequencePoint.StartColumn = point.StartColumn;
            sequencePoint.EndLine = point.EndLine;
            sequencePoint.EndColumn = point.EndColumn;
            sequencePoint.DocumentUrl = std::move(documentFileName);
            sequencePoints.push_back(std::move(sequencePoint));
        }
        return sequencePoints;
    } catch (const std::out_of_range&) {
        // The C# catch (BadImageFormatException): the points collected so
        // far are discarded.
        return {};
    }
}

std::vector<DI::Variable> PortableDebugInfoProvider::GetVariables(
    std::uint32_t methodToken) const {
    const Md::PortablePdb* metadata = Reader();
    if (metadata == nullptr) {
        return {};
    }
    std::vector<DI::Variable> variables;
    ForEachLocalScope(*metadata, MethodRow(methodToken),
        [&](std::uint32_t scopeRow) {
            Md::PdbRowRange range = metadata->GetLocalVariableRange(scopeRow);
            for (std::int32_t row = range.First; row <= range.Last; ++row) {
                Md::PdbLocalVariableRow localVariable =
                    metadata->GetLocalVariable(static_cast<std::uint32_t>(row));
                variables.emplace_back(localVariable.Index,
                    metadata->GetString(localVariable.Name));
            }
        });
    return variables;
}

bool PortableDebugInfoProvider::TryGetName(std::uint32_t methodToken, int index,
    std::string& name) const {
    const Md::PortablePdb* metadata = Reader();
    name.clear();
    if (metadata == nullptr) {
        return false;
    }
    bool found = false;
    ForEachLocalScope(*metadata, MethodRow(methodToken),
        [&](std::uint32_t scopeRow) {
            if (found) {
                return;
            }
            Md::PdbRowRange range = metadata->GetLocalVariableRange(scopeRow);
            for (std::int32_t row = range.First; row <= range.Last; ++row) {
                Md::PdbLocalVariableRow localVariable =
                    metadata->GetLocalVariable(static_cast<std::uint32_t>(row));
                if (localVariable.Index == index) {
                    name = metadata->GetString(localVariable.Name);
                    found = true;
                    break;
                }
            }
        });
    return found;
}

bool PortableDebugInfoProvider::TryGetExtraTypeInfo(std::uint32_t methodToken,
    int index, DI::PdbExtraTypeInfo& extraTypeInfo) const {
    const Md::PortablePdb* metadata = Reader();
    extraTypeInfo = DI::PdbExtraTypeInfo{};
    if (metadata == nullptr) {
        return false;
    }

    // The local variable row the index names: the first row with the slot
    // index, across the method's scopes (the C# LocalVariableHandle).
    std::uint32_t localVariableRow = 0;
    ForEachLocalScope(*metadata, MethodRow(methodToken),
        [&](std::uint32_t scopeRow) {
            if (localVariableRow != 0) {
                return;
            }
            Md::PdbRowRange range = metadata->GetLocalVariableRange(scopeRow);
            for (std::int32_t row = range.First; row <= range.Last; ++row) {
                Md::PdbLocalVariableRow localVariable =
                    metadata->GetLocalVariable(static_cast<std::uint32_t>(row));
                if (localVariable.Index == index) {
                    localVariableRow = static_cast<std::uint32_t>(row);
                    break;
                }
            }
        });

    std::uint32_t rowCount = metadata->RowCount(Md::PdbTable::CustomDebugInformation);
    for (std::uint32_t row = 1; row <= rowCount; ++row) {
        Md::PdbCustomDebugInformationRow cdi =
            metadata->GetCustomDebugInformation(row);
        if (cdi.ParentToken == 0) {
            continue;  // the C# cdi.Parent.IsNil
        }
        if ((cdi.ParentToken >> 24) != (kLocalVariableTable >> 24)) {
            continue;  // the C# cdi.Parent.Kind != HandleKind.LocalVariable
        }
        if (localVariableRow == 0
            || cdi.ParentToken != (kLocalVariableTable | localVariableRow)) {
            continue;
        }
        if (cdi.Value == 0 || cdi.KindGuid == 0) {
            continue;  // the C# cdi.Value.IsNil / cdi.Kind.IsNil
        }
        std::optional<std::array<std::uint8_t, 16>> kind =
            metadata->TryGetGuid(cdi.KindGuid);
        if (!kind.has_value()) {
            // An out-of-range kind index throws in the C# (MetadataReader
            // .GetGuid validates the heap); the reader degrades to empty
            // here, the port's graceful-degradation convention.
            continue;
        }
        if (kind == DI::KnownGuids::TupleElementNames
            && !extraTypeInfo.TupleElementNames.has_value()) {
            ILSpy::Decompiler::Util::Span<const std::uint8_t> blob =
                metadata->GetBlob(cdi.Value);
            std::vector<std::string> list;
            std::size_t position = 0;
            while (position < blob.size()) {
                // Read a UTF-8 NUL-terminated string and skip the
                // terminator. A missing terminator throws, like the C#
                // ReadUTF8(IndexOf(0) == -1) does.
                std::size_t terminator = position;
                while (terminator < blob.size() && blob[terminator] != 0) {
                    ++terminator;
                }
                if (terminator == blob.size()) {
                    throw std::out_of_range(
                        "PDB tuple element names: missing null terminator");
                }
                std::string name(reinterpret_cast<const char*>(blob.data() + position),
                    terminator - position);
                position = terminator + 1;
                // The C# stores null for a whitespace-or-empty name (the
                // unnamed-tuple-element marker); the port carries it as the
                // empty string.
                if (IsWhitespaceOrEmpty(name)) {
                    name.clear();
                }
                list.push_back(std::move(name));
            }
            extraTypeInfo.TupleElementNames = std::move(list);
        } else if (kind == DI::KnownGuids::DynamicLocalVariables
            && !extraTypeInfo.DynamicFlags.has_value()) {
            ILSpy::Decompiler::Util::Span<const std::uint8_t> blob =
                metadata->GetBlob(cdi.Value);
            std::vector<bool> flags(blob.size() * 8);
            std::size_t flagIndex = 0;
            for (std::size_t i = 0; i < blob.size(); ++i) {
                std::uint8_t byte = blob[i];
                for (int bit = 1; bit != 0x100; bit <<= 1) {
                    flags[flagIndex++] = (byte & bit) != 0;
                }
            }
            extraTypeInfo.DynamicFlags = std::move(flags);
        }

        if (extraTypeInfo.TupleElementNames.has_value()
            && extraTypeInfo.DynamicFlags.has_value()) {
            break;
        }
    }

    return extraTypeInfo.TupleElementNames.has_value()
        || extraTypeInfo.DynamicFlags.has_value();
}

}  // namespace ILSpy::ILSpyX::PdbProvider
