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

// Port of ICSharpCode.Decompiler/Disassembler/ILStructure.cs: the structured
// control-flow analysis the MethodBodyDisassembler DetectControlStructure
// branch consumes -- a tree of .try/filter/handler/loop regions over a
// method body's IL byte range. The root constructor builds the
// exception-structure tree from the body's EH clauses and detects loops via
// backward branches (the C# "very simple loop detection"); AddNestedStructure
// maintains the tree shape; GetInnermost finds the region an offset renders
// inside.
//
// Divergences from the C# shape:
//  - The C# root ctor takes a MethodBodyBlock and reads body.ExceptionRegions
//    and body.GetILReader(); the port takes the IL span and the EH-clause span
//    directly (the MethodBody surface a caller already holds -- Disassemble
//    passes body.IL() and body.Handlers(); tests pass synthetic streams).
//  - The C# carries Module/MethodHandle/GenericContext on every node for the
//    WriteStructureHeader catch-type rendering; the port carries the same trio
//    (a non-owning module pointer, the raw method token, a copy of the
//    value-type generic context).
//  - The C# GC-owned Children list ports as std::unique_ptr ownership (the
//    AddNestedStructure move-existing-children step becomes a vector move).
//  - The C# List.Sort in SortChildren (unstable introsort) ports as
//    std::stable_sort: List.Sort insertion-sorts small lists, so stable
//    preserves the practical C# order for equal StartOffsets and is
//    deterministic run-to-run (the D164 determinism lesson).

#pragma once

#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Util/BitSet.hpp"
#include "Decompiler/Util/Span.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Disassembler {

// The C# `public enum ILStructureType`: specifies the type of an IL structure.
enum class ILStructureType {
    Root,    // the root block of the method
    Loop,    // a nested control structure representing a loop
    Try,     // a nested control structure representing a try block
    Handler, // a nested control structure representing a catch, finally, or fault block
    Filter,  // a nested control structure representing an exception filter block
};

// The C# `public class ILStructure`: an IL structure.
class ILStructure {
public:
    // The C# root ctor (module, handle, genericContext, body): the Root
    // structure spanning [0, il.size()), which builds the tree of exception
    // structures from the EH clauses, detects the loops, and sorts the
    // children.
    ILStructure(const Metadata::MetadataFile& module, std::uint32_t methodToken,
        const Metadata::MetadataGenericContext& genericContext,
        Util::Span<const std::uint8_t> il,
        Util::Span<const Metadata::ExceptionHandlerClause> handlers);

    // The C# (module, handle, genericContext, type, startOffset, endOffset,
    // handler) ctor for the exception structures (the C# `handler = default`
    // becomes the all-zero clause).
    ILStructure(const Metadata::MetadataFile& module, std::uint32_t methodToken,
        const Metadata::MetadataGenericContext& genericContext,
        ILStructureType type, int startOffset, int endOffset,
        const Metadata::ExceptionHandlerClause& handler);

    // The C# (module, handle, genericContext, type, startOffset, endOffset,
    // loopEntryPoint) ctor for the loop structures.
    ILStructure(const Metadata::MetadataFile& module, std::uint32_t methodToken,
        const Metadata::MetadataGenericContext& genericContext,
        ILStructureType type, int startOffset, int endOffset, int loopEntryPoint);

    // The C# public readonly fields.
    const Metadata::MetadataFile* Module;
    std::uint32_t MethodHandle;
    Metadata::MetadataGenericContext GenericContext;
    ILStructureType Type;

    // Start position of the structure.
    int StartOffset;

    // End position of the structure. (exclusive)
    int EndOffset;

    // The exception handler associated with the Try, Filter or Handler block
    // (the C# `default` ExceptionRegion for Root/Loop -- the all-zero clause).
    Metadata::ExceptionHandlerClause ExceptionHandler{};

    // The loop's entry point (-1 when the loop has no recorded entry point).
    int LoopEntryPointOffset = 0;

    // The list of child structures.
    std::vector<std::unique_ptr<ILStructure>> Children;

    // The C# `public ILStructure GetInnermost(int offset)`: gets the
    // innermost structure containing the specified offset.
    const ILStructure* GetInnermost(int offset) const;

private:
    // The C# `bool AddNestedStructure(ILStructure newStructure)`.
    bool AddNestedStructure(std::unique_ptr<ILStructure> newStructure);

    // The C# private struct Branch: the source interval [start, end) of the
    // branch instruction and its target offset (the C# Interval Source pair
    // flattened; the int-sized Util Interval is still deferred).
    struct Branch {
        int SourceStart;
        int SourceEnd;
        int Target;
    };

    // The C# `(List<Branch>, BitSet) FindAllBranches(BlobReader body)`:
    // finds all branches (multiple entries for a switch), sorted by source
    // offset, plus the is-after-unconditional-branch bitmap.
    static std::pair<std::vector<Branch>, Util::BitSet> FindAllBranches(
        Util::Span<const std::uint8_t> il);

    // The C# `static bool IsUnconditionalBranch(ILOpCode opCode)`.
    static bool IsUnconditionalBranch(Metadata::ILOpCode opCode);

    // The C# `void SortChildren()`.
    void SortChildren();
};

}  // namespace ILSpy::Decompiler::Disassembler
