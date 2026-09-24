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

#include "Decompiler/IL/Transforms/ILInlining.hpp"

#include "Decompiler/IL/Instructions/DeconstructInstruction.hpp"
#include "Decompiler/IL/Transforms/NamedArgumentTransform.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

bool VariableCanBeUsedForInlining(const ILVariable* v) {
    if (!v) return false;
    if (v->Kind == VariableKind::PinnedLocal) return false;
    if (v->StoreCount != 1) return false;
    if (v->LoadCount + v->AddressCount != 1) return false;
    return true;
}

} // namespace

// Find the single load of `v` (an LdLoc or an LdLoca) inside `expr` that can be
// replaced by `expressionBeingMoved`. Mirrors ILInlining.FindLoadInNext (subset:
// no SlotInfo restrictions, no named-argument handling). Faithful to the C#,
// which returns Found for both an LdLoc(v) and an LdLoca(v) match; the caller
// gates whether an ldloca can actually be inlined (this port defers the
// ldloca-into-addressof path, so InlineOneIfPossible skips an LdLoca found).
FindResult FindLoadInNext(ILInstruction* expr, ILVariable* v,
                          ILInstruction* expressionBeingMoved,
                          InliningOptions options) {
    if (!expr) return {FindResultType::Stop};
    if (expr->Op == OpCode::LdLoc) {
        auto* ld = static_cast<LdLoc*>(expr);
        if (ld->Variable.get() == v) return {FindResultType::Found, ld};
        if (MayReorder(expressionBeingMoved, expr))
            return {FindResultType::Continue};
        return {FindResultType::Stop};
    }
    if (expr->Op == OpCode::LdLoca) {
        auto* lda = static_cast<LdLoca*>(expr);
        if (lda->Variable.get() == v) return {FindResultType::Found, lda};
        if (MayReorder(expressionBeingMoved, expr))
            return {FindResultType::Continue};
        return {FindResultType::Stop};
    }
    if (auto* block = dynamic_cast<Block*>(expr);
        block != nullptr && block->Kind == BlockKind::CallWithNamedArgs) {
        // The C# `expr is Block { Kind: BlockKind.CallWithNamedArgs }` arm:
        // a named-argument block can host the load in any of its slots (the
        // C# `NamedArgumentTransform.CanExtendNamedArgument` static).
        return NamedArgumentCanExtend(block, v, expressionBeingMoved);
    }
    if ((static_cast<int>(options) & static_cast<int>(InliningOptions::FindDeconstruction)) != 0 &&
        dynamic_cast<DeconstructInstruction*>(expr) != nullptr) {
        // The C# `options.HasFlag(InliningOptions.FindDeconstruction) && expr
        // is DeconstructInstruction di` arm: the walk has REACHED a deconstruct
        // instruction; the result carries it and the caller (the
        // InlineDeconstructionInitializer) checks the load's location against
        // the deconstruct's assignments.
        return FindResult(FindResultType::Deconstruction, expr);
    }
    for (int i = 0; i < expr->ChildCount(); ++i) {
        FindResult r = FindLoadInNext(expr->GetChild(i), v, expressionBeingMoved,
                                      options);
        if (r.type != FindResultType::Continue) {
            if (r.type == FindResultType::Stop
                && options & InliningOptions::IntroduceNamedArguments) {
                if (auto* call = dynamic_cast<Call*>(expr)) {
                    return NamedArgumentCanIntroduce(
                        call, expr->GetChild(i), v, expressionBeingMoved);
                }
            }
            return r;
        }
    }
    if (MayReorder(expressionBeingMoved, expr))
        return {FindResultType::Continue};
    return {FindResultType::Stop};
}

// The C# `static bool IsSafeForInlineOver(ILInstruction expr, ILInstruction
// expressionBeingMoved)` (ILInlining.cs line 907): safe to move
// `expressionBeingMoved` past `expr` -- SemanticHelper.MayReorder (the port's
// flag-level MayReorder).
static bool IsSafeForInlineOver(const ILInstruction* expr,
                                const ILInstruction* expressionBeingMoved) {
    return MayReorder(expressionBeingMoved->Flags(), expr->Flags());
}

// The C# `public static bool CanMoveInto(...)` (ILInlining.cs line 933). The
// ancestor chain's slots must accept the inlining and the move must not reorder
// past any of the ancestors' earlier children. The port's per-node
// `CanInlineIntoSlot` (the Block/BlockContainer/IsInst overrides ported
// faithfully; the port's default is permissive -- see the ILInstruction note
// on the SlotInfo metadata deferral).
bool CanMoveInto(ILInstruction* expressionBeingMoved, ILInstruction* stmt,
                 ILInstruction* targetLoad) {
    assert(targetLoad->IsDescendantOf(stmt));
    for (ILInstruction* inst = targetLoad; inst != stmt; inst = inst->Parent) {
        if (!inst->Parent->CanInlineIntoSlot(inst->ChildIndex, expressionBeingMoved))
            return false;
        // Check whether re-ordering with predecessors is valid:
        const int childIndex = inst->ChildIndex;
        for (int i = 0; i < childIndex; ++i) {
            ILInstruction* predecessor = inst->Parent->GetChild(i);
            if (!IsSafeForInlineOver(predecessor, expressionBeingMoved))
                return false;
        }
    }
    return true;
}

// The C# `public static bool CanUninline(ILInstruction arg, ILInstruction stmt)`
// (ILInlining.cs line 980): moving into and moving out-of are equivalent.
bool CanUninline(ILInstruction* arg, ILInstruction* stmt) {
    return CanMoveInto(arg, stmt, arg);
}

// The C# `internal static bool IsUsedAsThisPointerInCall(LdLoca ldloca)`
// (ILInlining.cs lines 455-503): the ldloca flows into a call's `this` slot on a
// value type; the property-getter compound-assignment and the setter cases
// follow the C#. The Await/NullableUnwrap/MatchInstruction parent arms are
// deferred with those nodes (a nullptr parent chain ends the walk).
bool IsUsedAsThisPointerInCall(LdLoca* ldloca) {
    if (ldloca == nullptr || ldloca->Variable == nullptr)
        return false;
    if (ldloca->Variable->Type != nullptr
        && ldloca->Variable->Type->IsReferenceType() == true)
        return false;
    ILInstruction* inst = ldloca;
    // The C# `inst.Parent is LdObjIfRef` arm is deferred with the LdObjIfRef node
    // (the port's ldobj model folds it at read time); the LdFlda chain walk
    // follows the C#.
    while (inst->Parent != nullptr && inst->Parent->Op == OpCode::LdFlda) {
        inst = inst->Parent;
    }
    if (inst->ChildIndex != 0)
        return false;
    if (inst->Parent == nullptr)
        return false;
    switch (inst->Parent->Op) {
        case OpCode::Call:
        case OpCode::CallVirt: {
            auto* callInst = static_cast<Call*>(inst->Parent);
            if (callInst->Method == nullptr)
                return false;
            const TypeSystem::IMethod* method = callInst->Method.get();
            if (method->IsAccessor()) {
                if (method->AccessorKind()
                    == TypeSystem::MethodSemanticsAttributes::Getter) {
                    // C# doesn't allow property compound assignments on temporary
                    // structs: the parent-of-parent shape is a
                    // CompoundAssignmentInstruction with the property target.
                    // (The port's compound-assignment node is deferred; the
                    // getter-over-temporary case returns true here.)
                    return true;
                }
                // C# doesn't allow calling setters on temporary structs.
                return false;
            }
            return !method->IsStatic();
        }
        default:
            return false;
    }
}

// The top-level statement containing `inst`: the last ancestor (including inst
// itself) whose parent is a Block. Mirrors the C#
// `inst.Ancestors.LastOrDefault(instr => instr.Parent is Block)` -- the C#
// Ancestors enumerable starts at the node itself and walks up the Parent chain,
// so "last" is the one closest to the root (the outermost direct child of a
// Block). Returns null when no ancestor (including inst) has a Block parent.
ILInstruction* TopLevelStatement(const ILInstruction* inst) {
    ILInstruction* result = nullptr;
    for (const ILInstruction* node = inst; node != nullptr; node = node->Parent) {
        if (node->Parent != nullptr && node->Parent->Op == OpCode::Block)
            result = const_cast<ILInstruction*>(node);
    }
    return result;
}

// The C# `internal static bool MethodRequiresCopyForReadonlyLValue(IMethod
// method, IType constrainedTo = null)` (IL/Transforms/ILInlining.cs line 438).
// See the header comment for the ThisIsRefReadOnly deferral note.
bool MethodRequiresCopyForReadonlyLValue(const TypeSystem::IMethod* method,
                                         const TypeSystem::IType* constrainedTo) {
    if (method == nullptr)
        return true;
    TypeSystem::ITypePtr declaringType = method->DeclaringType();
    const TypeSystem::IType* type = constrainedTo;
    if (type == nullptr)
        type = declaringType.get();
    if (type == nullptr)
        return true;
    // The C# `type.IsReferenceType == true` (the nullable-bool comparison):
    // reference types are never implicitly copied.
    if (type->IsReferenceType() == std::optional<bool>(true))
        return false;
    if (method->ThisIsRefReadOnly())
        return false;
    return true;
}

// The C# `internal static bool IsReadOnlySpanCharCtor(IMethod method)`
// (IL/Transforms/ILInlining.cs lines 541-550). See the header comment for the
// TypeArguments/ByReferenceType read notes.
bool IsReadOnlySpanCharCtor(const TypeSystem::IMethod& method) {
    if (!method.IsConstructor()) return false;
    const std::vector<const TypeSystem::IParameter*> parameters = method.Parameters();
    if (parameters.size() != 1 || parameters[0] == nullptr) return false;
    TypeSystem::ITypePtr declaringType = method.DeclaringType();
    if (!declaringType
        || !TypeSystem::IsKnownType(*declaringType,
                                    TypeSystem::KnownTypeCode::ReadOnlySpanOfT)) {
        return false;
    }
    auto* parameterized =
        dynamic_cast<const TypeSystem::ParameterizedType*>(declaringType.get());
    if (parameterized == nullptr || parameterized->TypeArguments().size() != 1
        || !TypeSystem::IsKnownType(*parameterized->TypeArguments()[0],
                                    TypeSystem::KnownTypeCode::Char)) {
        return false;
    }
    auto* parameterType =
        dynamic_cast<const TypeSystem::ByReferenceType*>(&parameters[0]->Type());
    return parameterType != nullptr && parameterType->Element() != nullptr
        && TypeSystem::IsKnownType(*parameterType->Element(),
                                   TypeSystem::KnownTypeCode::Char);
}

// True when `inst` sits in the constructor initializer (before the chained
// `: base(...)`/`: this(...)` call). Faithful to the C#
// ILInlining.IsInConstructorInitializer: a null function or an instruction
// whose range ends after the chained call starts is not in the initializer; an
// instruction whose enclosing top-level statement also ends before the chained
// call is. When the function is not an instance constructor with a chained call
// the offset is -1, so any non-empty instruction range (EndILOffset > -1)
// short-circuits to false (matching the C#).
bool IsInConstructorInitializer(const ILFunction* function, const ILInstruction* inst) {
    if (!function || !inst) return false;
    const std::int32_t ctorCallStart = function->ChainedConstructorCallILOffset();
    if (inst->EndILOffset > ctorCallStart) return false;
    auto* topLevelInst = TopLevelStatement(inst);
    if (!topLevelInst) return false;
    return topLevelInst->EndILOffset <= ctorCallStart;
}

// Try to inline the StLoc at `pos` into the next instruction's load of its
// variable, or remove it if dead. Returns true if the stloc was consumed.
// `pos` may be out of range after a prior removal shrank the block (the
// per-statement driver loops at one position); guard and return false so the
// caller's while-loop terminates without reading out of bounds.
//
// At namespace scope (not in the anonymous namespace above) so other
// per-statement transforms (NullCoalescingTransform, ...) can call it after a
// fold that opens up an inlining opportunity -- mirroring the C#
// `ILInlining.InlineOneIfPossible(block, pos, InliningOptions.None, ctx)` static
// call. The helper it uses (VariableCanBeUsedForInlining) is in the anonymous
// namespace above and visible here; FindLoadInNext is also at namespace scope
// (declared in the header) so other transforms can call it directly.
bool InlineOneIfPossible(Block* block, int pos, ILTransformContext& ctx,
                         InliningOptions options) {
    if (pos < 0 || static_cast<std::size_t>(pos) >= block->Instructions.size()) return false;
    auto* stloc = dynamic_cast<StLoc*>(block->Instructions[static_cast<std::size_t>(pos)].get());
    if (!stloc) return false;
    ILVariable* v = stloc->Variable.get();
    if (!v) return false;

    if (VariableCanBeUsedForInlining(v)) {
        // The "next" instruction is the one after the stloc in the block, or
        // the block's final if the stloc is last (the C# counts the final in
        // its instruction list).
        ILInstruction* next = nullptr;
        if (static_cast<std::size_t>(pos) + 1 < block->Instructions.size())
            next = block->Instructions[static_cast<std::size_t>(pos) + 1].get();
        else
            next = block->FinalInstruction.get();
        auto value = stloc->TakeChild(0);
        FindResult r = FindLoadInNext(next, v, value.get(), options);
        // Only inline an LdLoc found (a NamedArgument result introduces the
        // named argument first, then inlines as usual). An LdLoca found is the
        // deferred ldloca-into-addressof path (needs an AddressOf node +
        // IsGeneratedTemporaryForAddressOf, not yet ported): skip it and fall
        // through to the dead-store check, preserving the prior LdLoca->Stop
        // behavior. Faithful to the C# DoInline, which gates an LdLoca found on
        // IsGeneratedTemporaryForAddressOf (always false in this port).
        if ((r.type == FindResultType::Found
             || r.type == FindResultType::NamedArgument)
            && r.loadInst && r.loadInst->Op == OpCode::LdLoc) {
            if (r.type == FindResultType::NamedArgument) {
                // The C# `NamedArgumentTransform.IntroduceNamedArgument(
                // r.CallArgument, context)`: promote the call argument to a
                // named argument; the original load is re-parented into the
                // promoted store and the inline continues as usual.
                NamedArgumentIntroduce(r.callArgument, ctx);
                // The introduce may have replaced the call in its parent slot
                // (the CallWithNamedArgs block) and re-parented the load;
                // `r.loadInst` (the LdLoc(v2) inside the promoted argument)
                // is still the inline target.
            }
            ctx.StepOnce("Inline variable");
            r.loadInst->ReplaceWith(std::move(value));
            block->RemoveInstructionAt(static_cast<std::size_t>(pos));
            return true;
        }
        stloc->SetChild(0, std::move(value));  // restore, try dead-store below
    }

    // Dead store: variable never loaded. CFS handles stack slots too; this
    // catches locals and the case where the single load was in another block.
    if (v->LoadCount == 0 && v->AddressCount == 0) {
        auto value = stloc->TakeChild(0);
        if (IsPure(value->Flags())) {
            ctx.StepOnce("Remove dead store");
            block->RemoveInstructionAt(static_cast<std::size_t>(pos));
            return true;
        }
        if (v->Kind == VariableKind::StackSlot) {
            ctx.StepOnce("Remove dead store, keep expression");
            stloc->ReplaceWith(std::move(value));
            return true;
        }
        stloc->SetChild(0, std::move(value));
    }

    return false;
}

namespace {

bool InlineAllInBlock(Block* block, ILTransformContext& ctx) {
    bool modified = false;
    for (int i = static_cast<int>(block->Instructions.size()) - 1; i >= 0; --i) {
        if (InlineOneIfPossible(block, i, ctx))
            modified = true;
    }
    return modified;
}

void Walk(ILInstruction* inst, const std::function<void(Block*)>& visit) {
    if (!inst) return;
    if (auto* b = dynamic_cast<Block*>(inst)) { visit(b); }
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

} // namespace

void ILInlining::Run(ILFunction& function, ILTransformContext& context) {
    ComputeVariableUsage(function);
    Walk(function.Body.get(), [&](Block* block) {
        InlineAllInBlock(block, context);
    });
    // Recompute usage so dead variables (inlined away) can be dropped from the
    // function's variable list (mirrors function.Variables.RemoveDead()).
    ComputeVariableUsage(function);
    auto& vars = function.Variables;
    vars.erase(
        std::remove_if(vars.begin(), vars.end(),
                        [](const ILVariablePtr& v) {
                            return v && v->StoreCount == 0 && v->LoadCount == 0 &&
                                   v->AddressCount == 0;
                        }),
        vars.end());
}

// IStatementTransform entry: the per-statement inlining pass the
// StatementTransform runs interleaved with the other per-statement transforms.
// Loops InlineOneIfPossible at `pos` until no change, mirroring the C#
// ILInlining.Run(Block, pos, ctx) overload. After a successful inline at the
// last position the block shrank (the final is separate in this port, so the
// last non-terminal is at Instructions.size()-1; RemoveInstructionAt drops the
// size by one), so the while guard re-checks `pos < size` and stops without
// reading out of bounds (the C# avoids this because the final lives in
// Instructions, so the last non-terminal is at Count-2 and the shifted-in
// instruction stays in range).
void ILInlining::Run(Block& block, int pos, StatementTransformContext& context) {
    while (pos >= 0 && static_cast<std::size_t>(pos) < block.Instructions.size()
           && InlineOneIfPossible(&block, pos, context.Base)) {
        // repeat inlining until nothing changes
    }
}

} // namespace ILSpy::Decompiler::IL
