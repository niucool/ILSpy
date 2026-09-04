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

// Port of ICSharpCode.Decompiler/Disassembler/SortByNameProcessor.cs: the
// IEntityProcessor that orders every collection by the entity's name --
// the generic-instantiation head for interfaces, the full type name for
// type definitions, and the method name + arity + parameter list for methods
// (so overloads cluster); the sort itself runs through the .NET
// culture-linguistic comparison (Util/CompareInvariantCulture), because the
// C# `OrderBy(item => key)` compares the keys with
// Comparer<string>.Default -- string.CompareTo -- not ordinal comparison
// (see StringComparers.hpp for the probed collation and the
// SortMembers.expected.il evidence).
//
// C#-to-C++ porting decisions:
//  * The seven `Process` overloads collapse into the IEntityProcessor
//    Process(module, items, kind) tagged form (the raw-token port -- see
//    IEntityProcessor.hpp).
//  * The private static `GetSortKey(handle, module)` overloads collapse the
//    same way into one public GetSortKey(module, token, kind); public so the
//    tests can pin the key render directly (the port's private-members-are-
//    public convention).
//  * The C# `items.OrderBy(key).ToArray()` is a stable sort over the
//    precomputed keys (std::stable_sort + Util::CompareInvariantCulture);
//    `OrderBy`'s stability means equal-key items keep their row order, which
//    the nine equal InternalsVisibleTo rows of the mscorlib assembly pin.

#pragma once

#include "Decompiler/Disassembler/IEntityProcessor.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::Disassembler {

class SortByNameProcessor final : public IEntityProcessor {
public:
    // The C# `IReadOnlyCollection<T> Process(MetadataFile module,
    // IReadOnlyCollection<T> items)`: the tokens ordered by their sort keys.
    std::vector<std::uint32_t> Process(const Metadata::MetadataFile& module,
        const std::vector<std::uint32_t>& items,
        ProcessedEntityKind kind) const override;

    // The C# private static `GetSortKey` overloads, tagged by the entity
    // kind:
    //   * InterfaceImplementation: the implemented interface's full type
    //     name (a TypeSpec row shrinks to its generic head);
    //   * TypeDefinition: the full type name (ToILNameString);
    //   * MethodDefinition: the method name, "`N" for the signature's
    //     generic parameter count, then the parameter list rendered at the
    //     disassembler signature syntax -- "Copy(string)", "Empty`1()";
    //   * PropertyDefinition/EventDefinition/FieldDefinition: the member
    //     name;
    //   * CustomAttribute: the constructor's declaring type's full name.
    static std::string GetSortKey(const Metadata::MetadataFile& module,
        std::uint32_t token, ProcessedEntityKind kind);
};

}  // namespace ILSpy::Decompiler::Disassembler
