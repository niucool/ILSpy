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

// Port of the match-extension methods the C# IL layer carries on
// `ILInstruction` (ICSharpCode.Decompiler/IL/Instructions/PatternMatching.cs
// plus the generated `IL/Instructions.cs` region): `MatchLdThis`, `MatchBox`,
// `MatchLdObj`, `MatchAddressOf`, `MatchLdFld`, the variable match helpers
// (`MatchLdLoc`, `MatchStLoc`), the field match helpers (`MatchLdsFld`,
// `MatchStsFld`, `MatchStFld`, `MatchLdsFlda`, `MatchLdFlda`), and the array
// match helpers (`MatchNewArr`, `MatchStObj`, `MatchLdElema`) as free functions
// over the port's IL node pointers.
//
// The C# methods return the matched CHILD REFERENCES through the `out`
// parameters; the port's IL tree owns its children via `unique_ptr` slots, so
// the child `out` parameters are NON-OWNING raw pointers (the instruction keeps
// the ownership) while the type `out` parameters alias the node's own
// `ITypePtr` field (the node keeps the ownership there too). Transforms that
// need to detach a child use the node classes' `SetChildRaw` surface instead.
//
// `PatternMatchingTransform.cpp` carries same-named file-local helpers in its
// anonymous namespace (an inner-scope shadow -- call sites there still resolve
// to the local ones); this header is the shared home new consumers use.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

// The C# `public bool MatchLdNull()` (Instructions.cs line 8659): the `ldnull`
// node -- a bare `LdNull` with no operand.
inline bool MatchLdNull(const ILInstruction* inst)
{
    return dynamic_cast<const LdNull*>(inst) != nullptr;
}

// The C# `public bool MatchLdThis()` (PatternMatching.cs line 114): the `ldloc`
// of the synthetic `this` parameter -- a `LdLoc` whose variable is a parameter
// with a negative index (the C# `Variable.Index < 0`; the port's ILVariable
// carries -1 for the synthetic this slot).
inline bool MatchLdThis(const ILInstruction* inst)
{
    const auto* ldloc = dynamic_cast<const LdLoc*>(inst);
    return ldloc != nullptr && ldloc->Variable != nullptr
        && ldloc->Variable->Kind == VariableKind::Parameter && ldloc->Variable->Index < 0;
}

// The C# `public bool MatchBox(out ILInstruction? argument, out IType? type)`
// (IL/Instructions.cs line 8874): the `box <T>` node.
inline bool MatchBox(const ILInstruction* inst, ILInstruction*& argument,
                     TypeSystem::ITypePtr& type)
{
    const auto* box = dynamic_cast<const Box*>(inst);
    if (box != nullptr) {
        argument = box->Argument.get();
        type = box->Type;
        return true;
    }
    argument = nullptr;
    type = nullptr;
    return false;
}

// The C# `public bool MatchLdObj(out ILInstruction? target, out IType? type)`
// (IL/Instructions.cs line 8833): the `ldobj <T>` node (the two-out-parameter
// overload; the `PatternMatching.cs` overload with the type-equality test is
// the three-argument form).
inline bool MatchLdObj(const ILInstruction* inst, ILInstruction*& target,
                       TypeSystem::ITypePtr& type)
{
    const auto* ldobj = dynamic_cast<const LdObj*>(inst);
    if (ldobj != nullptr) {
        target = ldobj->Target.get();
        type = ldobj->Type;
        return true;
    }
    target = nullptr;
    type = nullptr;
    return false;
}

// The C# `public bool MatchAddressOf(out ILInstruction? value, out IType? type)`
// (IL/Instructions.cs line 8532): the `addressof <T>` node.
inline bool MatchAddressOf(const ILInstruction* inst, ILInstruction*& value,
                           TypeSystem::ITypePtr& type)
{
    const auto* addressOf = dynamic_cast<const AddressOf*>(inst);
    if (addressOf != nullptr) {
        value = addressOf->Argument.get();
        type = addressOf->Type;
        return true;
    }
    value = nullptr;
    type = nullptr;
    return false;
}

// The C# `public bool MatchLdFld(out ILInstruction? target, out IField? field)`
// (PatternMatching.cs line 460): a field load -- an `ldobj` over an `ldflda` with
// no unaligned prefix and not volatile. `field` is the ldflda's resolved field.
// The target is the ldflda's target, except for a value-type field whose target
// is an `addressof` (then the addressof's value is the target).
inline bool MatchLdFld(const ILInstruction* inst, ILInstruction*& target,
                       const TypeSystem::IField*& field)
{
    const auto* ldobj = dynamic_cast<const LdObj*>(inst);
    if (ldobj != nullptr && ldobj->Target != nullptr) {
        const auto* ldflda = dynamic_cast<const LdFlda*>(ldobj->Target.get());
        if (ldflda != nullptr && ldobj->UnalignedPrefix == 0 && !ldobj->IsVolatile) {
            field = ldflda->Field.get();
            bool declaringTypeIsReference =
                ldflda->Field != nullptr && ldflda->Field->DeclaringType() != nullptr
                && ldflda->Field->DeclaringType()->IsReferenceType()
                    == std::optional<bool>(true);
            ILInstruction* addressTarget = nullptr;
            TypeSystem::ITypePtr addressType;
            if (declaringTypeIsReference
                || !MatchAddressOf(ldflda->Target.get(), addressTarget, addressType)) {
                target = ldflda->Target.get();
            } else {
                target = addressTarget;
            }
            return true;
        }
    }
    target = nullptr;
    field = nullptr;
    return false;
}

// The C# `public bool MatchLdLoc(ILVariable? variable)` (PatternMatching.cs line
// 78): a bare LdLoc whose variable is the given one. The port compares the raw
// ILVariable pointers (the C# reference equality on the shared variable object).
inline bool MatchLdLoc(const ILInstruction* inst, const ILVariable* variable)
{
    const auto* ldloc = dynamic_cast<const LdLoc*>(inst);
    return ldloc != nullptr && ldloc->Variable.get() == variable;
}

// The C# `public bool MatchStLoc(out ILVariable? variable)` (line 123): a StLoc
// with its variable.
inline bool MatchStLoc(ILInstruction* inst, ILVariable*& variable)
{
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc != nullptr) {
        variable = stloc->Variable.get();
        return true;
    }
    variable = nullptr;
    return false;
}

// The C# `public bool MatchStLoc(ILVariable? variable, out ILInstruction? value)`
// (line 135): a StLoc to the given variable, reporting the stored value.
inline bool MatchStLoc(ILInstruction* inst, const ILVariable* variable,
                       ILInstruction*& value)
{
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc != nullptr && stloc->Variable.get() == variable) {
        value = stloc->Value.get();
        return true;
    }
    value = nullptr;
    return false;
}

// The C# `public bool MatchLdsFld(out IField? field)` (line 476): a static field
// load -- an `ldobj` over an `ldsflda` with no unaligned prefix and not volatile.
inline bool MatchLdsFld(const ILInstruction* inst,
                        const TypeSystem::IField*& field)
{
    const auto* ldobj = dynamic_cast<const LdObj*>(inst);
    if (ldobj != nullptr && ldobj->Target != nullptr) {
        const auto* ldsflda = dynamic_cast<const LdsFlda*>(ldobj->Target.get());
        if (ldsflda != nullptr && ldobj->UnalignedPrefix == 0 && !ldobj->IsVolatile) {
            field = ldsflda->Field.get();
            return true;
        }
    }
    field = nullptr;
    return false;
}

// The C# `public bool MatchStsFld(out IField? field, out ILInstruction? value)`
// (line 492): a static field store -- an `stobj` over an `ldsflda` with no
// unaligned prefix and not volatile.
inline bool MatchStsFld(const ILInstruction* inst,
                        const TypeSystem::IField*& field, ILInstruction*& value)
{
    const auto* stobj = dynamic_cast<const StObj*>(inst);
    if (stobj != nullptr && stobj->Target != nullptr) {
        const auto* ldsflda = dynamic_cast<const LdsFlda*>(stobj->Target.get());
        if (ldsflda != nullptr && stobj->UnalignedPrefix == 0 && !stobj->IsVolatile) {
            field = ldsflda->Field.get();
            value = stobj->Value.get();
            return true;
        }
    }
    field = nullptr;
    value = nullptr;
    return false;
}

// The C# `public bool MatchStFld(out ILInstruction? target, out IField? field,
// out ILInstruction? value)` (line 505): an instance field store -- an `stobj`
// over an `ldflda` with no unaligned prefix and not volatile.
inline bool MatchStFld(const ILInstruction* inst, ILInstruction*& target,
                       const TypeSystem::IField*& field, ILInstruction*& value)
{
    const auto* stobj = dynamic_cast<const StObj*>(inst);
    if (stobj != nullptr && stobj->Target != nullptr) {
        const auto* ldflda = dynamic_cast<const LdFlda*>(stobj->Target.get());
        if (ldflda != nullptr && stobj->UnalignedPrefix == 0 && !stobj->IsVolatile) {
            target = ldflda->Target.get();
            field = ldflda->Field.get();
            value = stobj->Value.get();
            return true;
        }
    }
    target = nullptr;
    field = nullptr;
    value = nullptr;
    return false;
}

// The C# `public bool MatchLdsFlda(out IField? field)` (Instructions.cs line
// 8796): a bare static field address.
inline bool MatchLdsFlda(const ILInstruction* inst,
                         const TypeSystem::IField*& field)
{
    const auto* ldsflda = dynamic_cast<const LdsFlda*>(inst);
    if (ldsflda != nullptr) {
        field = ldsflda->Field.get();
        return true;
    }
    field = nullptr;
    return false;
}

// The C# `public bool MatchLdFlda(out ILInstruction? target, out IField? field)`
// (Instructions.cs line 8783): a bare instance field address.
inline bool MatchLdFlda(const ILInstruction* inst, ILInstruction*& target,
                        const TypeSystem::IField*& field)
{
    const auto* ldflda = dynamic_cast<const LdFlda*>(inst);
    if (ldflda != nullptr) {
        target = ldflda->Target.get();
        field = ldflda->Field.get();
        return true;
    }
    target = nullptr;
    field = nullptr;
    return false;
}

// The C# `public bool MatchNewArr(out IType? type)` (Instructions.cs line
// 8913): a `newarr <T>` node reporting its element type.
inline bool MatchNewArr(const ILInstruction* inst, TypeSystem::ITypePtr& type)
{
    const auto* newArr = dynamic_cast<const NewArr*>(inst);
    if (newArr != nullptr) {
        type = newArr->Type;
        return true;
    }
    type = nullptr;
    return false;
}

// The C# `public bool MatchStObj(out ILInstruction? target, out ILInstruction?
// value, out IType? type)` (Instructions.cs line 8859): a `stobj <T>` node
// reporting its target, value, and element type.
inline bool MatchStObj(const ILInstruction* inst, ILInstruction*& target,
                       ILInstruction*& value, TypeSystem::ITypePtr& type)
{
    const auto* stobj = dynamic_cast<const StObj*>(inst);
    if (stobj != nullptr) {
        target = stobj->Target.get();
        value = stobj->Value.get();
        type = stobj->Type;
        return true;
    }
    target = nullptr;
    value = nullptr;
    type = nullptr;
    return false;
}

// The C# `public bool MatchLdElema(out IType? type, out ILInstruction? array)`
// (Instructions.cs line 8966): an `ldelema <T>` node reporting its element type
// and array operand.
inline bool MatchLdElema(const ILInstruction* inst, TypeSystem::ITypePtr& type,
                         ILInstruction*& array)
{
    const auto* ldElema = dynamic_cast<const LdElema*>(inst);
    if (ldElema != nullptr) {
        type = ldElema->Type;
        array = ldElema->Array.get();
        return true;
    }
    type = nullptr;
    array = nullptr;
    return false;
}

// The C# `public bool MatchLdcI4(int val)` (PatternMatching.cs line 27): an
// LdcI4 constant with the given value. The ExpressionBuilder/transforms files
// carry same-named anonymous-namespace copies (the "copied next to its
// consumer" convention) that shadow this one inside those TUs; new consumers
// use this shared home.
inline bool MatchLdcI4(const ILInstruction* inst, std::int32_t val)
{
    const auto* ldc = dynamic_cast<const LdcI4*>(inst);
    return ldc != nullptr && ldc->Value == val;
}

// The C# `public bool MatchNop()` (Instructions.cs -- the generated
// instruction-extension region): a Nop node. The port's nullable Leave value
// (a null Value is the C#'s Nop-normalized value) reads as the Nop shape at
// the Leave call sites through MatchLeave below, not through this strict
// node form.
inline bool MatchNop(const ILInstruction* inst)
{
    return inst != nullptr && inst->Op == OpCode::Nop;
}

// The C# `public bool MatchBranch(out Block? targetBlock)` (PatternMatching.cs
// line 171): a Branch with its target block.
inline bool MatchBranch(ILInstruction* inst, Block*& targetBlock)
{
    auto* br = dynamic_cast<Branch*>(inst);
    if (br != nullptr)
    {
        targetBlock = br->TargetBlock;
        return true;
    }
    targetBlock = nullptr;
    return false;
}

// The C# `public bool MatchBranch(Block? targetBlock)` (line 183): a Branch
// targeting the given block. The parameter is CONST (the
// ReduceNestingTransform precedent): a plain `Block*` parameter is ambiguous
// with the out-form below for rvalue call sites under MSVC's rvalue-to-
// lvalue-reference binding extension (both rank identity), while the `const
// Block*` qualification conversion outranks it deterministically.
inline bool MatchBranch(ILInstruction* inst, const Block* targetBlock)
{
    auto* br = dynamic_cast<Branch*>(inst);
    return br != nullptr && br->TargetBlock == targetBlock;
}

// The C# `public bool MatchLeave(out BlockContainer? targetContainer, out
// ILInstruction? value)` (line 189): a Leave with its container and value.
inline bool MatchLeave(ILInstruction* inst, BlockContainer*& targetContainer,
                       ILInstruction*& value)
{
    auto* leave = dynamic_cast<Leave*>(inst);
    if (leave != nullptr)
    {
        targetContainer = leave->TargetContainer;
        value = leave->Value.get();
        return true;
    }
    targetContainer = nullptr;
    value = nullptr;
    return false;
}

// The C# `public bool MatchLeave(BlockContainer? targetContainer)` (line 227):
// a value-less Leave (a Nop value -- the port's null Value is the Nop shape)
// targeting the given container. The parameter is CONST (the MatchBranch
// precedent above).
inline bool MatchLeave(ILInstruction* inst, const BlockContainer* targetContainer)
{
    auto* leave = dynamic_cast<Leave*>(inst);
    return leave != nullptr && leave->TargetContainer == targetContainer
           && (leave->Value == nullptr || MatchNop(leave->Value.get()));
}

// The C# `public bool MatchIfInstruction(out ILInstruction? condition, out
// ILInstruction? trueInst, out ILInstruction? falseInst)` (line 233): an
// IfInstruction with its three children.
inline bool MatchIfInstruction(ILInstruction* inst, ILInstruction*& condition,
                               ILInstruction*& trueInst, ILInstruction*& falseInst)
{
    auto* ifInst = dynamic_cast<IfInstruction*>(inst);
    if (ifInst != nullptr)
    {
        condition = ifInst->Condition.get();
        trueInst = ifInst->TrueInst.get();
        falseInst = ifInst->FalseInst.get();
        return true;
    }
    condition = nullptr;
    trueInst = nullptr;
    falseInst = nullptr;
    return false;
}

// The C# `public bool MatchLogicAnd(out ILInstruction? lhs, out ILInstruction?
// rhs)` (PatternMatching.cs lines 287-301): the short-circuit-and pattern
// `if (a) b else ldc.i4 0`. Unlike C# `&&`, the ILAst form passes through any I4
// value on the rhs derived from the true arm, so the render needs the extra
// boolean guard the VisitIfInstruction arm applies.
inline bool MatchLogicAnd(const ILInstruction* inst, ILInstruction*& lhs,
                          ILInstruction*& rhs)
{
    const auto* ifInst = dynamic_cast<const IfInstruction*>(inst);
    if (ifInst != nullptr && MatchLdcI4(ifInst->FalseInst.get(), 0))
    {
        lhs = ifInst->Condition.get();
        rhs = ifInst->TrueInst.get();
        return true;
    }
    lhs = nullptr;
    rhs = nullptr;
    return false;
}

// The C# `public bool MatchLogicOr(out ILInstruction? lhs, out ILInstruction?
// rhs)` (PatternMatching.cs lines 306-319): the short-circuit-or pattern
// `if (a) ldc.i4 1 else b`.
inline bool MatchLogicOr(const ILInstruction* inst, ILInstruction*& lhs,
                         ILInstruction*& rhs)
{
    const auto* ifInst = dynamic_cast<const IfInstruction*>(inst);
    if (ifInst != nullptr && MatchLdcI4(ifInst->TrueInst.get(), 1))
    {
        lhs = ifInst->Condition.get();
        rhs = ifInst->FalseInst.get();
        return true;
    }
    lhs = nullptr;
    rhs = nullptr;
    return false;
}

} // namespace ILSpy::Decompiler::IL
