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
// and `MatchLdObj` as free functions over the port's IL node pointers.
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
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/VariableKind.hpp"
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

} // namespace ILSpy::Decompiler::IL
