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

// A known Append call -- an instance call on DefaultInterpolatedStringHandler
// whose first arg is `ldloca v` and whose name/arity matches one of the
// AppendLiteral / AppendFormatted overloads the C# compiler emits for the
// interpolation holes. Mirrors the C# InterpolatedStringTransform.IsKnownCall.
bool IsKnownCall(Block& block, int pos, const ILVariable* v) {
    // The C# `pos >= block.Instructions.Count - 1` guard: the Append calls must
    // leave room for the trailing ToStringAndClear, so an Append at the last
    // instruction is rejected. The int cast keeps `size() - 1` signed (a 0-size
    // block yields -1 and any pos >= -1 returns false).
    if (pos >= static_cast<int>(block.Instructions.size()) - 1)
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
    if (pos >= static_cast<int>(block.Instructions.size()))
        return false;
    // The C# loop runs FindLoadInNext once per producer instruction
    // (interpolationEnd - interpolationStart times), all searching the same
    // block.Instructions[pos]; each iteration must return Found (the load is
    // there). A single call establishes the Found result and the load's parent;
    // the repeated calls only re-confirm Found (the load does not move between
    // iterations), so one call is equivalent.
    auto* expr = block.Instructions[pos].get();
    if (!expr) return false;
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
    // stloc v(newobj DefaultInterpolatedStringHandler..ctor(ldc.i4, ldc.i4))
    auto* inst = block.Instructions[pos].get();
    if (!inst || inst->Op != OpCode::StLoc) return;
    auto* stloc = static_cast<StLoc*>(inst);
    auto v = stloc->Variable;
    if (!v || v->Kind != VariableKind::Local) return;
    if (!NullableLiftingTransform::IsKnownType(v->Type.get(),
                                               TypeSystem::KnownTypeCode::DefaultInterpolatedStringHandler))
        return;
    auto* value = stloc->Value.get();
    if (!value || value->Op != OpCode::Call) return;
    auto* newObj = static_cast<Call*>(value);
    if (!newObj->IsNewObj) return;
    if (newObj->Arguments.size() != 2) return;
    if (!NullableLiftingTransform::IsKnownType(newObj->DeclaringType.get(),
                                                TypeSystem::KnownTypeCode::DefaultInterpolatedStringHandler))
        return;
    if (!IsLdcI4(newObj->Arguments[0].get()) || !IsLdcI4(newObj->Arguments[1].get()))
        return;

    // { call MethodName(ldloca v, ...) } -- collect the Append calls.
    int p = pos;
    do {
        p++;
    } while (IsKnownCall(block, p, v.get()));
    int interpolationEnd = p;

    // ... call ToStringAndClear(ldloca v) ...
    ILInstruction* insertionPoint = nullptr;
    if (!FindToStringAndClear(block, interpolationEnd, interpolationStart, interpolationEnd,
                              v.get(), insertionPoint))
        return;
    // The handler v is used exactly as the interpolation expects: one store
    // (the stloc), AddressCount == interpolationEnd - interpolationStart (the N
    // Append ldloca's + the ToStringAndClear ldloca), no loads.
    if (!(v->StoreCount == 1 &&
          v->AddressCount == interpolationEnd - interpolationStart &&
          v->LoadCount == 0))
        return;

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
