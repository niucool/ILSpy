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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Disassembler/IEntityProcessor.cs: the
// interface the ReflectionDisassembler's member/attribute collections can be
// routed through for reordering (the C# test harness plugs in
// SortByNameProcessor to get deterministic member orders). The C# interface
// carries seven `Process` overloads typed by the handle element kind
// (IReadOnlyCollection<MethodDefinitionHandle>, ...CustomAttributeHandle, ...);
// this port's handles are raw metadata tokens (every collection is a
// vector<uint32_t>), so the seven overloads collapse into one method tagged by
// ProcessedEntityKind -- the tag plays the C# overload-resolution role.

#pragma once

#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::Disassembler {

// The handle kind of a Process call -- the C# method-overload dimension.
enum class ProcessedEntityKind {
    InterfaceImplementation,
    TypeDefinition,
    MethodDefinition,
    PropertyDefinition,
    EventDefinition,
    FieldDefinition,
    CustomAttribute,
};

class IEntityProcessor {
public:
    virtual ~IEntityProcessor() = default;

    // The C# `IReadOnlyCollection<THandle> Process(MetadataFile module,
    // IReadOnlyCollection<THandle> items)`: the reordered token collection.
    virtual std::vector<std::uint32_t> Process(
        const Metadata::MetadataFile& module,
        const std::vector<std::uint32_t>& items,
        ProcessedEntityKind kind) const = 0;
};

}  // namespace ILSpy::Decompiler::Disassembler
