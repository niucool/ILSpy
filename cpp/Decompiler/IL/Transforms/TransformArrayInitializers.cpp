// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformArrayInitializers.cs --
// the simple single-dimensional array-initializer path (the C# Run's
// MatchNewArr + HandleSimpleArrayInitializer + BuildSimpleArrayInitializerBlock
// arm). See the header for the deferred arms and the ownership note.

#include "Decompiler/IL/Transforms/TransformArrayInitializers.hpp"

#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <cassert>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The port's array-instruction nodes live in ArrayInstructions.hpp (the NewArr
// / LdElema pair the reader decodes); stelem decodes as
// StObj(LdElema(type, array, indices), value, type) — the C# has no separate
// StElem node (ILReader.cs StElem(IType), line 1655).

// The C# `bool MatchNewArr(ILInstruction instruction, out IType arrayType,
// out int[] length)` (TransformArrayInitializers.cs lines 866-883): a newarr
// with positive constant lengths.
bool MatchNewArr(ILInstruction* instruction, TS::ITypePtr& arrayType,
                 std::vector<int>& length)
{
    auto* newArr = dynamic_cast<NewArr*>(instruction);
    if (newArr == nullptr)
        return false;
    arrayType = newArr->Type;
    length.clear();
    length.reserve(newArr->Indices.size());
    for (const auto& arg : newArr->Indices) {
        auto* ldc = dynamic_cast<LdcI4*>(arg.get());
        if (ldc == nullptr || ldc->Value <= 0)
            return false;
        length.push_back(ldc->Value);
    }
    return true;
}

// The value subtree must not reference the array variable (the C#
// `value.Descendants.OfType<IInstructionWithVariableOperand>()
// .Any(inst => inst.Variable == store)` walk).
bool ValueReferencesStore(ILInstruction* inst, ILVariable* store)
{
    if (inst == nullptr)
        return false;
    if (inst->Op == OpCode::LdLoc) {
        if (static_cast<LdLoc*>(inst)->Variable.get() == store)
            return true;
    }
    else if (inst->Op == OpCode::LdLoca) {
        if (static_cast<LdLoca*>(inst)->Variable.get() == store)
            return true;
    }
    for (int i = 0; i < inst->ChildCount(); ++i) {
        if (ValueReferencesStore(inst->GetChild(i), store))
            return true;
    }
    return false;
}

} // namespace

// The C# `internal static bool HandleSimpleArrayInitializer(ILFunction,
// Block block, int pos, ILVariable store, int[] arrayLength, out values, out
// instructionsToRemove)` (TransformArrayInitializers.cs lines 593-776): the
// stobj(ldelema(store, indices), value) scan with the next-minimum-index
// walk, the gap-tolerant fill loop, and the partial-initializer abort. The
// accepted elements are recorded with their shell pointers; the caller takes
// the values on success (the ownership note in the header). An abort leaves
// the block untouched (the C# `return false` paths never mutate).
bool TransformArrayInitializersHandleSimple(
    Block* block, int pos, ILVariable* store, const std::vector<int>& arrayLength,
    std::vector<TransformArrayInitializersElement>& elements,
    int& instructionsToRemove)
{
    instructionsToRemove = 0;
    int elementCount = 0;
    const int length = arrayLength[0];

    // The C# `int[] nextMinimumIndex` + the CalculateNextIndices local
    // function: this port's Run gate keeps arrayLength.Length == 1, so the
    // multi-dimensional carry loop collapses to the 1-dim increment (the
    // indices non-empty case with k == 0).
    int nextMinimumIndex = 0;

    int i = pos;
    bool exact = false;
    while (i < static_cast<int>(block->Instructions.size())) {
        std::vector<int> indices;
        ILInstruction* value = nullptr;
        ILInstruction* inst =
            block->Instructions[static_cast<std::size_t>(i)].get();
        int step = 0;
        StObj* elementShell = nullptr;
        // stobj elementType(ldelema elementType(ldloc store, indices), value)
        if (auto* stobj = dynamic_cast<StObj*>(inst)) {
            auto* ldelem = dynamic_cast<LdElema*>(stobj->Target.get());
            if (ldelem == nullptr)
                break;
            auto* ldloc = dynamic_cast<LdLoc*>(ldelem->Array.get());
            if (ldloc == nullptr || ldloc->Variable.get() != store)
                break;
            bool bad = false;
            for (const auto& idx : ldelem->Indices) {
                auto* ldc = dynamic_cast<LdcI4*>(idx.get());
                if (ldc == nullptr) {
                    bad = true; break;
                }
                indices.push_back(ldc->Value);
            }
            if (bad) break;
            value = stobj->Value.get();
            elementShell = stobj;
            step = 1;
            // stloc s(ldelema elementType(ldloc store, indices));
            // stobj elementType(ldloc s, value)
        }
        else if (auto* addressStore = dynamic_cast<StLoc*>(inst)) {
            ILVariable* addressTemporary = addressStore->Variable.get();
            if (addressTemporary == nullptr
                || !addressTemporary->IsSingleDefinition()
                || addressTemporary->LoadCount != 1)
                break;
            auto* ldelem = dynamic_cast<LdElema*>(addressStore->Value.get());
            if (ldelem == nullptr)
                break;
            auto* ldloc = dynamic_cast<LdLoc*>(ldelem->Array.get());
            if (ldloc == nullptr || ldloc->Variable.get() != store)
                break;
            bool bad2 = false;
            for (const auto& idx : ldelem->Indices) {
                auto* ldc = dynamic_cast<LdcI4*>(idx.get());
                if (ldc == nullptr) { bad2 = true; break; }
                indices.push_back(ldc->Value);
            }
            if (bad2) break;
            if (!(i + 1 < static_cast<int>(block->Instructions.size())))
                break;
            auto* stobj = dynamic_cast<StObj*>(
                block->Instructions[static_cast<std::size_t>(i) + 1].get());
            if (stobj == nullptr)
                break;
            auto* target = dynamic_cast<LdLoc*>(stobj->Target.get());
            if (target == nullptr
                || target->Variable.get() != addressTemporary)
                break;
            value = stobj->Value.get();
            elementShell = stobj;
            step = 2;
        }
        else {
            break;
        }
        if (ValueReferencesStore(value, store)) {
            break;
        }
        if (indices.size() != arrayLength.size()) {
            break;
        }
        if (length <= 0)
            break;
        do {
        // The C# do-loop passes the scanned `indices` on EVERY iteration
            // (the C# `CalculateNextIndices(indices, out exact)`): the minimum
            // advances per iteration and the value is recorded only when the
            // minimum catches the scanned index (the non-exact iterations
            // fill the skipped slots with null defaults).

            int captured = nextMinimumIndex;
            exact = true;
            // The captured slot is the current minimum; validate the scanned
            // indices against it (the C# CalculateNextIndices body).
            {
                bool previousComponentWasGreater = false;
                for (std::size_t k = 0; k < indices.size(); ++k) {
                    const int index = indices[k];
                    if (index < 0 || index >= arrayLength[k]
                        || (!previousComponentWasGreater
                            && index < nextMinimumIndex)) {
                        // `CalculateNextIndices` returning null aborts the
                        // transform (the C# `return false`).
                        return false;
                    }
                    if (index != nextMinimumIndex) {
                        exact = false;
                        if (index > nextMinimumIndex)
                            previousComponentWasGreater = true;
                    }
                }
            }
            if (exact) {
                elements.push_back({std::vector<int>{captured},
                                    elementShell, step});
                elementCount++;
                instructionsToRemove += step;
            }
            else {
                elements.push_back({std::vector<int>{captured}, nullptr, step});
            }
            // The increment: for the 1-dim case k == 0 terminates the
            // carry immediately (the C# `if (nextMinimumIndex[k] <
            // arrayLength[k] || k == 0) break;`).
            nextMinimumIndex++;
        } while (static_cast<int>(elements.size()) < length && !exact);
        i += step;
    }
    if (i < static_cast<int>(block->Instructions.size())) {
        // An element of the array is modified directly after the initializer:
        // abort the transform so that partial initializers are not constructed.
        if (auto* stobj =
                dynamic_cast<StObj*>(block->Instructions[static_cast<std::size_t>(i)].get())) {
            auto* ldelem = dynamic_cast<LdElema*>(stobj->Target.get());
            if (ldelem != nullptr) {
                auto* ldloc = dynamic_cast<LdLoc*>(ldelem->Array.get());
                if (ldloc != nullptr && ldloc->Variable.get() == store) {
                    return false;
                }
            }
        }
    }
    // The C# `pos + instructionsToRemove >= block.Instructions.Count` -- the
    // C# Instructions collection EXCLUDES the final (Block.cs: "the
    // FinalInstruction is included in Block.Children, but not in
    // Block.Instructions"), so the C# count equals the port's Instructions
    // size (the port stores the final in FinalInstruction). The bound maps
    // 1:1: at least one non-consumed instruction must remain after the
    // scanned range (in the pipeline the assignment store
    // `stloc a(ldloc v)` that follows the element stores).
    if (pos + instructionsToRemove
        >= static_cast<int>(block->Instructions.size()))
        return false;
    if (elementCount == 0)
        return false;

    // The C# `mustTransform = IsCatchWhenBlock(block) ||
    // IsInConstructorInitializer(...)`; the port has neither surface, so
    // mustTransform stays false (the C# arm is deferred with those surfaces).
    const bool mustTransform = false;
    if (elementCount < static_cast<int>(length) / 3 - 5) {
        if (!mustTransform)
            return false;
    }
    // The empty-slot defaults: fill the values list up to the array length.
    while (static_cast<int>(elements.size()) < length) {
        // `CalculateNextIndices(null, out _)` — the gap slots carry no value
        // shell (the caller default-fills them).
        elements.push_back({std::vector<int>{nextMinimumIndex}, nullptr, 0});
        nextMinimumIndex++;
    }
    return true;
}

// The C# `static Block BuildSimpleArrayInitializerBlock(ILVariable v, IType
// elementType, int[] arrayLength, values)` (lines 848-862): the
// ArrayInitializer block with the stobj(ldelema) element stores and the ldloc
// final. The values arrive as owned pointers (null = the DefaultValue filler,
// the C# `value ?? GetNullExpression(elementType)`).
std::unique_ptr<Block> TransformArrayInitializersBuildBlock(
    ILVariablePtr v, const TS::ITypePtr& elementType,
    const std::vector<int>& arrayLength,
    std::vector<std::pair<std::vector<int>, std::unique_ptr<ILInstruction>>>
        values)
{
    auto block = std::make_unique<Block>();
    block->Kind = BlockKind::ArrayInitializer;
    std::vector<std::unique_ptr<ILInstruction>> lengths;
    lengths.reserve(arrayLength.size());
    for (int l : arrayLength)
        lengths.push_back(std::make_unique<LdcI4>(l));
    block->Instructions.push_back(std::make_unique<StLoc>(
        ILVariablePtr(v), std::make_unique<NewArr>(elementType, std::move(lengths))));
    for (auto& entry : values) {
        std::vector<std::unique_ptr<ILInstruction>> indices;
        indices.reserve(entry.first.size());
        for (int idx : entry.first)
            indices.push_back(std::make_unique<LdcI4>(idx));
        auto ldelem = std::make_unique<LdElema>(
            elementType, std::make_unique<LdLoc>(v),
            std::move(indices));
        std::unique_ptr<ILInstruction> value = std::move(entry.second);
        if (value == nullptr) {
            // The empty-slot default (the C# GetNullExpression(elementType)).
            value = std::make_unique<DefaultValue>(elementType);
        }
        block->Instructions.push_back(
            std::make_unique<StObj>(std::move(ldelem), std::move(value),
                                    elementType));
    }
    block->SetFinal(std::make_unique<LdLoc>(v));
    return block;
}

// The C# `void IStatementTransform.Run(Block block, int pos,
// StatementTransformContext context)` (lines 38-56): the settings gate and the
// simple single-dim arm (the multi-dim/jagged/blob/span arms are deferred with
// their surfaces; see the header).
void TransformArrayInitializers::Run(Block& block, int pos,
                                     StatementTransformContext& context)
{
    if (!context.Base.Settings.ArrayInitializers)
        return;
    ILFunction* function = nullptr;
    for (const ILInstruction* p = &block; p != nullptr; p = p->Parent) {
        if (dynamic_cast<const ILFunction*>(p) != nullptr) {
            function =
                const_cast<ILFunction*>(static_cast<const ILFunction*>(p));
            break;
        }
    }
    if (function == nullptr)
        return;
    // The C# `pos >= body.Instructions.Count - 2` -- the C# Instructions
    // collection excludes the final, so its count equals the port's
    // Instructions size; the C# gate maps to `pos + 2 >= size` (at least two
    // instructions after pos: the scanned run plus the trailing consumer).
    if (pos < 0 || pos + 2 >= static_cast<int>(block.Instructions.size()))
        return;
    auto* stloc = dynamic_cast<StLoc*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (stloc == nullptr)
        return;
    TS::ITypePtr elementType;
    std::vector<int> arrayLength;
    if (!MatchNewArr(stloc->Value.get(), elementType, arrayLength))
        return;
    if (arrayLength.size() != 1)
        return;

    std::vector<TransformArrayInitializersElement> elements;
    int instructionsToRemove = 0;
    const bool handled = TransformArrayInitializersHandleSimple(
        &block, pos + 1, stloc->Variable.get(), arrayLength, elements,
        instructionsToRemove);
    if (!handled) {
        return;
    }
    context.Base.StepOnce("HandleSimpleArrayInitializer: single-dim");
    ILVariablePtr tempStore = function->RegisterVariable(
        VariableKind::InitializerTarget, stloc->Variable->Type);
    // Take the accepted element values out of their shells (the ownership
    // note in the header), then build the initializer block.
    std::vector<std::pair<std::vector<int>, std::unique_ptr<ILInstruction>>> values;
    values.reserve(elements.size());
    for (auto& element : elements) {
        std::unique_ptr<ILInstruction> value;
        if (element.shell != nullptr) {
            value = element.shell->TakeChild(1);
        }
        values.emplace_back(std::move(element.indices), std::move(value));
    }
    auto initializerBlock = TransformArrayInitializersBuildBlock(
        std::move(tempStore), stloc->Variable->Type, arrayLength,
        std::move(values));
    // Replace the stloc's value with the initializer block and drop the
    // consumed stelem instructions (the C# `body.Instructions[pos] =
    // newStore; body.Instructions.RemoveRange(pos + 1, instructionsToRemove);`
    // — the removal walks from the end so the earlier indices stay stable).
    for (int removed = 0; removed < instructionsToRemove; ++removed) {
        block.RemoveInstructionAt(
            static_cast<std::size_t>(pos) + 1
            + static_cast<std::size_t>(instructionsToRemove - 1 - removed));
    }
    block.Instructions[static_cast<std::size_t>(pos)] = std::make_unique<StLoc>(
        stloc->Variable, std::move(initializerBlock));
    // The C# ILVariable maintains its LoadInstructions/StoreCount lists on
    // every tree mutation, so after the RemoveRange the consumed ldelema
    // loads are gone and InlineIfPossible sees the live counts. The port's
    // counts are static, so they are recomputed from the function here (the
    // same recompute ILInlining::Run performs before its inline passes).
    ComputeVariableUsage(*function);
    context.Base.StepOnce("TransformArrayInitializers done");
    // The C# `ILInlining.InlineIfPossible(body, pos, context)` — the port's
    // InlineOneIfPossible (the C# InlineIfPossible wraps the aggressive
    // options).
    InlineOneIfPossible(&block, pos, context.Base);
}

} // namespace ILSpy::Decompiler::IL