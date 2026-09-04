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

// Port of ICSharpCode.Decompiler/Disassembler/ILStructure.cs (see the header
// for the port notes).

#include "Decompiler/Disassembler/ILStructure.hpp"

#include "Decompiler/Disassembler/ILParser.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace ILSpy::Decompiler::Disassembler {

ILStructure::ILStructure(const Metadata::MetadataFile& module, std::uint32_t methodToken,
    const Metadata::MetadataGenericContext& genericContext,
    Util::Span<const std::uint8_t> il,
    Util::Span<const Metadata::ExceptionHandlerClause> handlers)
    : ILStructure(module, methodToken, genericContext, ILStructureType::Root, 0,
          static_cast<int>(il.size()), Metadata::ExceptionHandlerClause{})
{
    // Build the tree of exception structures:
    for (std::size_t i = 0; i < handlers.size(); i++) {
        const Metadata::ExceptionHandlerClause& eh = handlers[i];
        // The C# `!body.ExceptionRegions.Take(i).Any(oldEh => oldEh.TryOffset
        // == eh.TryOffset && oldEh.TryLength == eh.TryLength)`: only the
        // first clause of a shared try range adds the Try structure.
        bool sharedTry = false;
        for (std::size_t j = 0; j < i; j++) {
            if (handlers[j].TryOffset == eh.TryOffset && handlers[j].TryLength == eh.TryLength) {
                sharedTry = true;
                break;
            }
        }
        if (!sharedTry) {
            AddNestedStructure(std::make_unique<ILStructure>(module, methodToken,
                genericContext, ILStructureType::Try, static_cast<int>(eh.TryOffset),
                static_cast<int>(eh.TryOffset + eh.TryLength), eh));
        }
        if (eh.Kind == Metadata::ExceptionHandlerKind::Filter) {
            AddNestedStructure(std::make_unique<ILStructure>(module, methodToken,
                genericContext, ILStructureType::Filter,
                static_cast<int>(eh.ClassTokenOrFilterOffset),
                static_cast<int>(eh.HandlerOffset), eh));
        }
        AddNestedStructure(std::make_unique<ILStructure>(module, methodToken,
            genericContext, ILStructureType::Handler, static_cast<int>(eh.HandlerOffset),
            static_cast<int>(eh.HandlerOffset + eh.HandlerLength), eh));
    }
    // Very simple loop detection: look for backward branches
    auto [allBranches, isAfterUnconditionalBranch] = FindAllBranches(il);
    // We go through the branches in reverse so that we find the biggest
    // possible loop boundary first (think loops with "continue;")
    for (int i = static_cast<int>(allBranches.size()) - 1; i >= 0; i--) {
        int loopEnd = allBranches[i].SourceEnd;
        int loopStart = allBranches[i].Target;
        if (loopStart < loopEnd) {
            // We found a backward branch. This is a potential loop.
            // Check that is has only one entry point:
            int entryPoint = -1;

            // entry point is first instruction in loop if prev inst isn't an unconditional branch
            if (loopStart > 0 && !isAfterUnconditionalBranch[loopStart])
                entryPoint = allBranches[i].Target;

            bool multipleEntryPoints = false;
            for (const auto& branch : allBranches) {
                if (branch.SourceStart < loopStart || branch.SourceStart >= loopEnd) {
                    if (loopStart <= branch.Target && branch.Target < loopEnd) {
                        // jump from outside the loop into the loop
                        if (entryPoint < 0)
                            entryPoint = branch.Target;
                        else if (branch.Target != entryPoint)
                            multipleEntryPoints = true;
                    }
                }
            }
            if (!multipleEntryPoints) {
                AddNestedStructure(std::make_unique<ILStructure>(module, methodToken,
                    genericContext, ILStructureType::Loop, loopStart, loopEnd, entryPoint));
            }
        }
    }
    SortChildren();
}

ILStructure::ILStructure(const Metadata::MetadataFile& module, std::uint32_t methodToken,
    const Metadata::MetadataGenericContext& genericContext,
    ILStructureType type, int startOffset, int endOffset,
    const Metadata::ExceptionHandlerClause& handler)
    : Module(&module), MethodHandle(methodToken), GenericContext(genericContext),
      Type(type), StartOffset(startOffset), EndOffset(endOffset),
      ExceptionHandler(handler)
{
    assert(startOffset < endOffset);
}

ILStructure::ILStructure(const Metadata::MetadataFile& module, std::uint32_t methodToken,
    const Metadata::MetadataGenericContext& genericContext,
    ILStructureType type, int startOffset, int endOffset, int loopEntryPoint)
    : Module(&module), MethodHandle(methodToken), GenericContext(genericContext),
      Type(type), StartOffset(startOffset), EndOffset(endOffset),
      LoopEntryPointOffset(loopEntryPoint)
{
    assert(startOffset < endOffset);
}

bool ILStructure::AddNestedStructure(std::unique_ptr<ILStructure> newStructure)
{
    // special case: don't consider the loop-like structure of "continue;" statements to be nested loops
    if (Type == ILStructureType::Loop && newStructure->Type == ILStructureType::Loop
        && newStructure->StartOffset == StartOffset)
        return false;
    if (newStructure->StartOffset < 0)
        return false;

    // use <= for end-offset comparisons because both end and EndOffset are exclusive
    assert(StartOffset <= newStructure->StartOffset && newStructure->EndOffset <= EndOffset);
    for (auto& childPtr : Children) {
        ILStructure* child = childPtr.get();
        if (child->StartOffset <= newStructure->StartOffset && newStructure->EndOffset <= child->EndOffset) {
            return child->AddNestedStructure(std::move(newStructure));
        } else if (!(child->EndOffset <= newStructure->StartOffset || newStructure->EndOffset <= child->StartOffset)) {
            // child and newStructure overlap
            if (!(newStructure->StartOffset <= child->StartOffset && child->EndOffset <= newStructure->EndOffset)) {
                // Invalid nesting, can't build a tree. -> Don't add the new structure.
                return false;
            }
        }
    }
    // Move existing structures into the new structure:
    for (std::size_t i = 0; i < Children.size(); i++) {
        const ILStructure* child = Children[i].get();
        if (newStructure->StartOffset <= child->StartOffset && child->EndOffset <= newStructure->EndOffset) {
            newStructure->Children.push_back(std::move(Children[i]));
            Children.erase(Children.begin() + static_cast<std::ptrdiff_t>(i));
            i--;
        }
    }
    // Add the structure here:
    Children.push_back(std::move(newStructure));
    return true;
}

std::pair<std::vector<ILStructure::Branch>, Util::BitSet> ILStructure::FindAllBranches(
    Util::Span<const std::uint8_t> il)
{
    std::vector<Branch> result;
    // The C# `new BitSet(body.Length + 1)`: the +1 covers the end offset of
    // the last instruction.
    Util::BitSet bitset(static_cast<int>(il.size()) + 1);
    const std::uint8_t* base = il.data();
    std::size_t size = il.size();
    std::size_t pos = 0;
    while (pos < size) {
        std::size_t offset = pos;
        std::size_t endOffset;
        Metadata::ILOpCode thisOpCode = Metadata::DecodeOpCode(base, size, pos);
        switch (Metadata::GetOperandType(thisOpCode)) {
            case Metadata::OperandType::BrTarget:
            case Metadata::OperandType::ShortBrTarget: {
                int target = DecodeBranchTarget(base, size, pos, thisOpCode);
                endOffset = pos;
                result.push_back(Branch{static_cast<int>(offset),
                    static_cast<int>(endOffset), target});
                if (IsUnconditionalBranch(thisOpCode))
                    bitset.Set(static_cast<int>(endOffset));
                else
                    bitset.Clear(static_cast<int>(endOffset));
                break;
            }
            case Metadata::OperandType::Switch: {
                auto targets = DecodeSwitchTargets(base, size, pos);
                for (int t : targets)
                    result.push_back(Branch{static_cast<int>(offset),
                        static_cast<int>(pos), t});
                break;
            }
            default:
                SkipOperand(base, size, pos, thisOpCode);
                if (IsUnconditionalBranch(thisOpCode))
                    bitset.Set(static_cast<int>(pos));
                else
                    bitset.Clear(static_cast<int>(pos));
                break;
        }
    }
    return {std::move(result), std::move(bitset)};
}

bool ILStructure::IsUnconditionalBranch(Metadata::ILOpCode opCode)
{
    switch (opCode) {
        case Metadata::ILOpCode::Br:
        case Metadata::ILOpCode::Br_s:
        case Metadata::ILOpCode::Ret:
        case Metadata::ILOpCode::Endfilter:
        case Metadata::ILOpCode::Endfinally:
        case Metadata::ILOpCode::Throw:
        case Metadata::ILOpCode::Rethrow:
        case Metadata::ILOpCode::Leave:
        case Metadata::ILOpCode::Leave_s:
            return true;
        default:
            return false;
    }
}

void ILStructure::SortChildren()
{
    // The C# `Children.Sort((a, b) => a.StartOffset.CompareTo(b.StartOffset))`
    // (List.Sort, unstable) ports as std::stable_sort: List.Sort
    // insertion-sorts small lists, so stable preserves the practical C#
    // order for equal StartOffsets and is deterministic run-to-run.
    std::stable_sort(Children.begin(), Children.end(),
        [](const std::unique_ptr<ILStructure>& a, const std::unique_ptr<ILStructure>& b) {
            return a->StartOffset < b->StartOffset;
        });
    for (const auto& child : Children)
        child->SortChildren();
}

const ILStructure* ILStructure::GetInnermost(int offset) const
{
    assert(StartOffset <= offset && offset < EndOffset);
    for (const auto& child : Children) {
        if (child->StartOffset <= offset && offset < child->EndOffset)
            return child->GetInnermost(offset);
    }
    return this;
}

}  // namespace ILSpy::Decompiler::Disassembler
