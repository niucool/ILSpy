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
// PURPOSE, NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/InterpolatedStringTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/BlockKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <string_view>

namespace ILSpy::Decompiler::IL {

namespace {

// The short method name -- the substring after the last "::" in the Call's
// resolved MethodName ("Namespace.Type::Method" -> "Method"). Mirrors the
// NullableLiftingTransform::ShortMethodName / TransformAssignment helpers.
std::string_view ShortMethodName(std::string_view fullName) {
    auto pos = fullName.rfind("::");
    if (pos == std::string_view::npos) return fullName;
    return fullName.substr(pos + 2);
}

// Match an int32 constant (LdcI4). Mirrors the C# MatchLdcI4(out int) bare match
// (the reader does not wrap ldc.i4 in conv, so no UnwrapConv unwrap is needed).
bool IsLdcI4(const ILInstruction* inst) {
    return inst && inst->Op == OpCode::LdcI4;
}

// Match an `ldloca v` (a LdLoca of the given variable). Mirrors the C#
// `call.Arguments[0].MatchLdLoca(v)`.
bool MatchLdLoca(const ILInstruction* inst, const ILVariable* v) {
    if (!inst || inst->Op != OpCode::LdLoca) return false;
    return static_cast<const LdLoca*>(inst)->Variable.get() == v;
}

// The ldloca'd variable (or null when the instruction is not an ldloca).
ILVariable* MatchLdLocaVariable(ILInstruction* inst) {
    if (!inst || inst->Op != OpCode::LdLoca) return nullptr;
    return static_cast<LdLoca*>(inst)->Variable.get();
}

// A known Append call -- an instance call on DefaultInterpolatedStringHandler
// whose first arg is `ldloca v` and whose name/arity matches one of the
// AppendLiteral / AppendFormatted overloads the C# compiler emits for the
// interpolation holes. Mirrors the C# InterpolatedStringTransform.IsKnownCall.
bool IsKnownCall(Block& block, int pos, const ILVariable* v) {
    // The C# `pos >= block.Instructions.Count - 1` guard keeps the trailing
    // ToStringAndClear in the same block; this port's control-flow cleanup
    // does not merge a `br`-to-next-block tail, so the consumer can sit in
    // the FOLLOWING sibling block and an Append at the last position is
    // still valid. The guard relaxes to the block end; the consumer search
    // (FindToStringAndClear) follows the fall-through when needed.
    if (pos >= static_cast<int>(block.Instructions.size()))
        return false;
    auto* inst = block.Instructions[pos].get();
    if (!inst || inst->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(inst);
    if (call->Arguments.size() <= 1) return false;  // C#: Arguments.Count > 1
    if (!MatchLdLoca(call->Arguments[0].get(), v)) return false;
    // The C# `if (!call.Method.IsStatic) return false;` -- the Append calls are
    // instance calls on the handler. This port's IsInstanceCall is true for
    // instance call/callvirt (false for static and newobj); a newobj never
    // reaches here (the first-arg-ldloca-v + instance-call shape excludes it).
    if (!call->IsInstanceCall) return false;
    if (!NullableLiftingTransform::IsKnownType(call->DeclaringType.get(),
                                                TypeSystem::KnownTypeCode::DefaultInterpolatedStringHandler))
        return false;
    auto name = ShortMethodName(call->MethodName);
    if (name == "AppendLiteral") {
        return call->Arguments.size() == 2 && call->Arguments[1]->Op == OpCode::LdStr;
    }
    if (name == "AppendFormatted") {
        switch (call->Arguments.size()) {
            case 2:
                return true;
            case 3:
                return call->Arguments[2]->Op == OpCode::LdStr ||
                       call->Arguments[2]->Op == OpCode::LdcI4;
            case 4:
                return call->Arguments[2]->Op == OpCode::LdcI4 &&
                       call->Arguments[3]->Op == OpCode::LdStr;
            default:
                return false;
        }
    }
    return false;
}

// The first instruction of the block that follows `block` in its container
// (the fall-through target of a `br`-to-next tail). Null when the block has
// no parent container, is the last block, or its final instruction does not
// fall through (a terminal or an out-of-order branch).
ILInstruction* NextBlockFirstInstruction(Block& block) {
    auto* container = dynamic_cast<BlockContainer*>(block.Parent);
    if (container == nullptr) return nullptr;
    for (std::size_t i = 0; i + 1 < container->Blocks.size(); ++i) {
        if (container->Blocks[i].get() == &block) {
            auto* next = container->Blocks[i + 1].get();
            // Only a fall-through reaches the next block: no final
            // instruction, or a branch to the next block itself.
            if (block.FinalInstruction == nullptr) return next ? next->Instructions.empty() ? nullptr : next->Instructions[0].get() : nullptr;
            if (auto* br = dynamic_cast<Branch*>(block.FinalInstruction.get())) {
                if (br->TargetBlock == next)
                    return next->Instructions.empty() ? nullptr : next->Instructions[0].get();
            }
            return nullptr;
        }
    }
    return nullptr;
}

// Locate the `call ToStringAndClear(ldloca v)` that consumes the handler and
// yields the interpolated string. The C# searches block.Instructions[pos]
// (pos == interpolationEnd, the first instruction after the Append calls) for a
// load of v via ILInlining.FindLoadInNext; the load's parent is the
// ToStringAndClear call (insertionPoint). Mirrors the C#
// InterpolatedStringTransform.FindToStringAndClear. Returns true and sets
// insertionPoint to the call when found.
bool FindToStringAndClear(Block& block, int pos, int interpolationStart, int interpolationEnd,
                          const ILVariable* v, ILInstruction*& insertionPoint) {
    insertionPoint = nullptr;
    // The consumer instruction: block.Instructions[pos] when it exists, or
    // the first instruction of the fall-through sibling block when the
    // interpolation runs to the end of its block (the port's control-flow
    // cleanup leaves the `br`-to-next shape unmerged; the C#'s merged block
    // keeps the consumer in place).
    ILInstruction* expr = nullptr;
    if (pos < static_cast<int>(block.Instructions.size())) {
        expr = block.Instructions[pos].get();
    } else if (pos == static_cast<int>(block.Instructions.size())) {
        // The statement after the last instruction is the block's final
        // instruction (the inlined `leave call ToStringAndClear(...)`
        // tail) when there is one, otherwise the first instruction of the
        // fall-through sibling block.
        expr = block.FinalInstruction ? block.FinalInstruction.get()
                                      : NextBlockFirstInstruction(block);
    }
    if (!expr) return false;
    // The C# loop runs FindLoadInNext once per producer instruction
    // (interpolationEnd - interpolationStart times), all searching the same
    // block.Instructions[pos]; each iteration must return Found (the load is
    // there). A single call establishes the Found result and the load's parent;
    // the repeated calls only re-confirm Found (the load does not move between
    // iterations), so one call is equivalent.
    auto* expressionBeingMoved = block.Instructions[interpolationStart].get();
    auto result = FindLoadInNext(expr, const_cast<ILVariable*>(v), expressionBeingMoved);
    if (result.type != FindResultType::Found || !result.loadInst)
        return false;
    insertionPoint = result.loadInst->Parent;
    if (!insertionPoint || insertionPoint->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(insertionPoint);
    if (call->Arguments.size() != 1) return false;
    if (!call->IsInstanceCall) return false;  // C#: !Method.IsStatic
    return ShortMethodName(call->MethodName) == "ToStringAndClear";
}

} // namespace

void InterpolatedStringTransform::Run(Block& block, int pos, StatementTransformContext& context) {
    if (!context.Base.Settings.StringInterpolation)
        return;
    if (pos < 0 || pos >= static_cast<int>(block.Instructions.size()))
        return;
    int interpolationStart = pos;
    // The handler init. The C# ILAst folds a struct ctor on a local into
    // `stloc v(newobj DefaultInterpolatedStringHandler..ctor(ldc.i4,
    // ldc.i4))`; this port's reader keeps the raw call statement
    // `call .ctor(ldloca v, ldc.i4, ldc.i4)`, so the pattern matches that
    // shape (an instance ctor call on DefaultInterpolatedStringHandler
    // whose first argument is the handler's address and whose remaining
    // arguments are the two literal counts).
    ILVariable* v = nullptr;
    int literalArgCount = 0;
    auto* inst = block.Instructions[pos].get();
    if (inst != nullptr && inst->Op == OpCode::StLoc) {
        // The C# ILAst's fold: `stloc v(newobj DefaultInterpolatedStringHandler
        // ..ctor(ldc.i4, ldc.i4))` (the seeded tests build this shape).
        auto* stloc = static_cast<StLoc*>(inst);
        v = stloc->Variable.get();
        if (v == nullptr || v->Kind != VariableKind::Local) return;
        if (!NullableLiftingTransform::IsKnownType(v->Type.get(),
                                                   TypeSystem::KnownTypeCode::DefaultInterpolatedStringHandler))
            return;
        auto* value = stloc->Value.get();
        if (value == nullptr || value->Op != OpCode::Call) return;
        auto* newObj = static_cast<Call*>(value);
        if (!newObj->IsNewObj) return;
        literalArgCount = 2;
        if (newObj->Arguments.size() != (std::size_t)literalArgCount) return;
        if (!NullableLiftingTransform::IsKnownType(newObj->DeclaringType.get(),
                                                    TypeSystem::KnownTypeCode::DefaultInterpolatedStringHandler))
            return;
        if (!IsLdcI4(newObj->Arguments[0].get()) || !IsLdcI4(newObj->Arguments[1].get()))
            return;
    } else {
        // This port's reader keeps the raw call statement
        // `call .ctor(ldloca v, ldc.i4, ldc.i4)` -- an instance ctor call
        // on DefaultInterpolatedStringHandler whose first argument is the
        // handler's address and whose remaining arguments are the two
        // literal counts.
        if (inst == nullptr || inst->Op != OpCode::Call) return;
        auto* ctor = static_cast<Call*>(inst);
        if (!ctor->IsInstanceCall) return;
        if (ShortMethodName(ctor->MethodName) != ".ctor") return;
        if (ctor->Arguments.size() != 3) return;
        v = MatchLdLocaVariable(ctor->Arguments[0].get());
        if (!NullableLiftingTransform::IsKnownType(ctor->DeclaringType.get(),
                                                    TypeSystem::KnownTypeCode::DefaultInterpolatedStringHandler))
            return;
        if (!v || v->Kind != VariableKind::Local) return;
        literalArgCount = 3;
        if (!IsLdcI4(ctor->Arguments[1].get()) || !IsLdcI4(ctor->Arguments[2].get()))
            return;
    }

    // { call MethodName(ldloca v, ...) } -- collect the Append calls.
    int p = pos;
    do {
        p++;
    } while (IsKnownCall(block, p, v));
    int interpolationEnd = p;

    // ... call ToStringAndClear(ldloca v) ...
    ILInstruction* insertionPoint = nullptr;
    if (!FindToStringAndClear(block, interpolationEnd, interpolationStart, interpolationEnd,
                              v, insertionPoint))
        return;
    // The handler v is used exactly as the interpolation expects: no loads,
    // and the store/address counts match the shape -- the stloc shape has
    // one store and AddressCount == the N Append ldloca's + the
    // ToStringAndClear ldloa (== interpolationEnd - interpolationStart);
    // the raw-call shape has no store and one more address (the ctor's own
    // ldloca).
    if (v->LoadCount != 0) return;
    if (literalArgCount == 2) {
        if (!(v->StoreCount == 1 &&
              v->AddressCount == interpolationEnd - interpolationStart))
            return;
    } else {
        if (!(v->StoreCount == 0 &&
              v->AddressCount == interpolationEnd - interpolationStart + 1))
            return;
    }

    context.Base.StepOnce("Transform DefaultInterpolatedStringHandler");
    v->Kind = VariableKind::InitializerTarget;

    // Build the InterpolatedString block: move the stloc + Append calls into it.
    auto replacement = std::make_unique<Block>();
    replacement->Kind = BlockKind::InterpolatedString;
    for (int i = interpolationStart; i < interpolationEnd; ++i) {
        replacement->Add(std::move(block.Instructions[i]));  // Add re-parents.
    }

    // The ToStringAndClear call (insertionPoint) becomes the replacement block's
    // FinalInstruction. Detach it from its parent first (no GC; a raw view would
    // dangle once the slot is reassigned), then place the replacement block in
    // the freed slot. Capture the parent/index before TakeChild (the node is
    // destroyed-then-reparented by the slot swap).
    ILInstruction* ipParent = insertionPoint->Parent;
    int ipIndex = insertionPoint->ChildIndex;
    auto toStringCall = ipParent->TakeChild(ipIndex);
    replacement->SetFinal(std::move(toStringCall));
    ipParent->SetChild(ipIndex, std::move(replacement));

    // Drop the now-moved stloc + Append slots from the host block and renumber so
    // the replacement (sitting at the former interpolationEnd slot, shifted down
    // by the erase) keeps a consistent ChildIndex.
    block.Instructions.erase(block.Instructions.begin() + interpolationStart,
                             block.Instructions.begin() + interpolationEnd);
    block.RenumberChildren();
}

} // namespace ILSpy::Decompiler::IL
