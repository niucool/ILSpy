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

// ILFunction: the root of an ILAst tree for one method body. Owns the body
// BlockContainer and the function's variables. Transforms grow the tree downward
// by grafting in nested ILFunctions (lambdas, local functions, expression trees).

#pragma once

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class ILFunction : public ILInstruction {
public:
    std::unique_ptr<BlockContainer> Body;
    std::vector<ILVariablePtr> Variables;

    // The constructor/static status of this function's method, the pre-resolved
    // subset of the C# ILFunction.Method handle the transforms consult. Defaults
    // false (a null Method, matching the C# `function?.Method is not {...}` bail)
    // and is populated by the IL reader from the MethodDef flags/name. The gate
    // the NullCoalescingTransform hoisted-constructor-argument null-guard fold
    // consults is `IsConstructor && !IsStatic` (an instance constructor).
    bool IsConstructor = false;
    bool IsStatic = false;

    // The IL offset of the first chained `: base(...)`/`: this(...)` constructor
    // call in this function's body, or -1 when this is not an instance
    // constructor or no chained call was found. Lazy and cached (the C#
    // ILFunction.ChainedConstructorCallILOffset). The chained call is the first
    // descendant `Call` (not newobj) whose method is a constructor on a
    // reference-type declaring type and whose parent is a Block; its
    // StartILOffset is the offset the ILInlining.IsInConstructorInitializer gate
    // compares hoisted null-guards against. -1 means "no initializer", so any
    // instruction's EndILOffset > -1 (i.e. any non-empty range) short-circuits the
    // gate to false, matching the C#.
    std::int32_t ChainedConstructorCallILOffset() const;

    // Create and register a new ILVariable on this function (the C#
    // ILFunction.RegisterVariable). A blank name is replaced with the generated
    // `I_<n>` form (HasGeneratedName set); the counter is per-function. The
    // NullCoalescingTransform hoisted-constructor-argument null-guard fold uses
    // this to allocate the temp that redirects the parameter's first use.
    ILVariablePtr RegisterVariable(VariableKind kind, TypeSystem::ITypePtr type,
                                   const std::string& name = std::string());

    // Recombine split variables by replacing all occurrences of variable2 with
    // variable1 (the C# ILFunction.RecombineVariables). variable1 and variable2
    // are "equal" per the C# ILVariableEqualityComparer -- split fragments of one
    // original variable (same Function, Kind, Index) that the decompiler wants
    // to treat as a single variable again; the LdLoc/StLoc match in
    // TransformAssignment.IsMatchingCompoundLoad calls this as its finalizeMatch
    // so a `stloc V(binary.op(ldloc V, rhs))` whose load and store are split
    // fragments collapses to one variable for the `V op= rhs` compound assign.
    // Every load/store/address of variable2 in the body is reassigned to
    // variable1, variable1's usage counts are incremented for the reassigned
    // uses, variable2's counts are zeroed, and variable2 is removed from the
    // function's Variables list. This port has no per-variable instruction
    // lists, so a tree walk replaces the C# list iteration and a manual count
    // increment replaces the C# property-setter's list maintenance. A no-op
    // when variable1 and variable2 are the same variable (or either is null).
    void RecombineVariables(ILVariablePtr variable1, ILVariablePtr variable2);

    ILFunction() : ILInstruction(OpCode::ILFunction) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Void; }
    bool IsRoot() const override { return true; }

    int ChildCount() const override { return 1; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? static_cast<ILInstruction*>(Body.get()) : nullptr; }

    void WriteTo(std::string& out) const override {
        out += "ILFunction {\n  ";
        if (Body) Body->WriteTo(out); else out += "(no body)";
        out += "\n}";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Body);
        // The function body slot always holds a BlockContainer.
        Body.reset(static_cast<BlockContainer*>(n.release()));
        return old;
    }
private:
    // Counter for generated helper-variable names (I_0, I_1, ...), the C#
    // ILFunction.helperVariableCount. Mutated by RegisterVariable; mutable so the
    // otherwise-const ChainedConstructorCallILOffset cache can stay separate.
    int helperVariableCount_ = 0;
    // Lazy cache for ChainedConstructorCallILOffset (the C# uses int.MinValue as
    // the not-computed sentinel; this port uses an explicit flag).
    mutable bool chainedCtorOffsetComputed_ = false;
    mutable std::int32_t chainedCtorOffset_ = -1;
};

} // namespace ILSpy::Decompiler::IL
