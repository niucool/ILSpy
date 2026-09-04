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

// Port of ICSharpCode.Decompiler/DebugInfo/IDebugInfoProvider.cs: the
// provider contract the MethodBodyDisassembler consumes when DebugInfo is
// set -- the sequence-point lines (ShowSequencePoints) and the debug local
// names appended to the .locals block. The provider implementation that
// reads real PDBs is the Phase-8 PdbProvider (the portable-PDB debug
// tables); the interface is the consumer-side contract.
//
// C#-to-C++ porting decisions:
//  * The C# `MethodDefinitionHandle` parameters port as the raw metadata
//    token (uint32_t) -- the port's handle convention.
//  * The C# `out` parameters (`TryGetName`, `TryGetExtraTypeInfo`) port as
//    reference out-parameters with the same bool return.
//  * `Description`/`SourceFileName` (C# properties) port as pure-virtual
//    getters; the provider is consumed read-only, so every member is const.

#pragma once

#include "Decompiler/DebugInfo/SequencePoint.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::DebugInfo {

// The C# `public struct Variable(int index, string name)`.
struct Variable {
    Variable() = default;
    Variable(int index, std::string name)
        : Index(index), Name(std::move(name)) {}

    int Index = 0;
    std::string Name;
};

// The C# `public struct PdbExtraTypeInfo` -- the dynamic/tuple names
// Roslyn records in CustomDebugInformation.
struct PdbExtraTypeInfo {
    std::vector<std::string> TupleElementNames;
    std::vector<bool> DynamicFlags;
};

// The C# `public interface IDebugInfoProvider`.
class IDebugInfoProvider {
public:
    virtual ~IDebugInfoProvider() = default;

    // The C# `string Description { get; }`.
    virtual std::string Description() const = 0;

    // The C# `string SourceFileName { get; }`.
    virtual std::string SourceFileName() const = 0;

    // The C# `IList<SequencePoint> GetSequencePoints(MethodDefinitionHandle
    // method)`.
    virtual std::vector<SequencePoint> GetSequencePoints(
        std::uint32_t methodToken) const = 0;

    // The C# `IList<Variable> GetVariables(MethodDefinitionHandle method)`.
    virtual std::vector<Variable> GetVariables(
        std::uint32_t methodToken) const = 0;

    // The C# `bool TryGetName(MethodDefinitionHandle method, int index, out
    // string name)`.
    virtual bool TryGetName(std::uint32_t methodToken, int index,
        std::string& name) const = 0;

    // The C# `bool TryGetExtraTypeInfo(MethodDefinitionHandle method, int
    // index, out PdbExtraTypeInfo extraTypeInfo)`.
    virtual bool TryGetExtraTypeInfo(std::uint32_t methodToken, int index,
        PdbExtraTypeInfo& extraTypeInfo) const = 0;
};

}  // namespace ILSpy::Decompiler::DebugInfo
