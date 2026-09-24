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
#include "Decompiler/TypeSystem/IParameter.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class ILTransformContext;

// The C# `enum ILFunctionKind` (ILFunction.cs lines 466-490): TopLevelFunction /
// Delegate / ExpressionTree / LocalFunction. Introduced by the decompiler
// pipeline steps (DelegateConstruction, TransformExpressionTrees,
// LocalFunctionDecompiler); the IL reader defaults to TopLevelFunction.
enum class ILFunctionKind {
    TopLevelFunction,
    Delegate,
    ExpressionTree,
    LocalFunction,
};

class ILFunction : public ILInstruction {
public:
    std::unique_ptr<BlockContainer> Body;
    // The C# `public readonly ILFunctionKind Kind` field. The port's reader and
    // the tests construct functions directly, so a public field replaces the C#
    // ctor parameter (the C# `ILFunction(..., ILFunctionKind kind =
    // ILFunctionKind.TopLevelFunction)` default mirrors the field initializer).
    ILFunctionKind Kind = ILFunctionKind::TopLevelFunction;
    std::vector<ILVariablePtr> Variables;

    // The C# `public readonly IType ReturnType` (ILFunction.cs) -- the declared
    // return type; set by the IL reader from the method signature (the port's
    // consumer sets it at visit time when resolving, or leaves it null for
    // void). Null means void (the C# `void` methods carry null too).
    TypeSystem::ITypePtr ReturnType;
    // The C# `public bool IsIterator` (ILFunction.cs) -- set by the
    // YieldReturnDecompiler; makes `return;` render as `yield break;`.
    bool IsIterator = false;
    // The C# `public IType? AsyncReturnType` (ILFunction.cs) -- the Task{T}
    // element type for async methods (the C# `IsAsync => AsyncReturnType !=
    // null` derives from it). Null for non-async functions.
    TypeSystem::ITypePtr AsyncReturnType;

    // The C# `public bool IsAsync => AsyncReturnType != null` (ILFunction.cs).
    bool IsAsync() const { return AsyncReturnType != nullptr; }

    // The C# `public IType DelegateType { get; set; }` (ILFunction.cs) -- the
    // delegate type a lambda/delayed-delegate construction was built against;
    // set by DelegateConstruction and TransformExpressionTrees (the C# reader
    // leaves it null for the top-level function).
    TypeSystem::ITypePtr DelegateType;

    // The C# `public IReadOnlyList<IParameter> Parameters => method.Parameters`
    // (ILFunction.cs) -- the method's parameter list, the pre-resolved subset of
    // the C# `ILFunction.Method` handle this port carries (the
    // IsConstructor/IsStatic pre-resolved-fields precedent; the port's reader
    // leaves it empty -- the closure/lambda slices populate it for the
    // DecompiledLambdaResolveResult consumer). Owned here; the C# GC reference
    // is a mutable handle.
    std::vector<std::shared_ptr<const TypeSystem::IParameter>> Parameters;

    // The C# `public string Name` (a get/set field the local-function decoders
    // assign): the source-side name of this function. Empty for functions whose
    // producer did not set one (the ExpressionBuilder's HidesVariableWithName
    // consults it over nested local functions).
    std::string Name;

    // The C# `public BlockContainer? DeclarationScope { get; internal set; }`
    // (ILFunction.cs): the scope the function is declared in -- the closest
    // container of the captured variables' initializers
    // (LocalFunctionDecompiler) or the container the lambda was found in
    // (DelegateConstruction). Null until the scope machinery assigns it.
    // Non-owning: the scope is a node of the enclosing tree.
    BlockContainer* DeclarationScope = nullptr;

    // The C# `public InstructionCollection<ILFunction> LocalFunctions` (child
    // slot 1): the local functions / lambdas nested in this function. Owned here
    // (the C# tree's parent ownership); appended by the closure-decoder slices.
    std::vector<std::unique_ptr<ILFunction>> LocalFunctions;

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

    // The C# `public void RunTransforms(IEnumerable<IILTransform> transforms,
    // ILTransformContext context)` (ILFunction.cs line 402): the fixed
    // per-body pipeline driver -- CheckInvariant before, then per transform
    // the step group (the C# StepStartGroup(transform type name); the
    // port's single Step hook carries the type name), the Run, and the
    // invariant check after (the C# cancellation/token and trace-
    // stopwatch bookkeeping are deferred with those surfaces). Implemented
    // out-of-line (the IILTransform definition is not included here --
    // ILFunction.hpp and IILTransform.hpp would cycle; the .cpp includes
    // the transform header).
    void RunTransforms(
        const std::vector<std::unique_ptr<class IILTransform>>& transforms,
        ILTransformContext& context);

    ILFunction() : ILInstruction(OpCode::ILFunction) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Void; }
    bool IsRoot() const override { return true; }

    // The C# ILFunction children: the Body (slot 0) plus the LocalFunctions
    // collection (slot 1, in order).
    int ChildCount() const override { return 1 + static_cast<int>(LocalFunctions.size()); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return static_cast<ILInstruction*>(Body.get());
        if (i - 1 < static_cast<int>(LocalFunctions.size()))
            return static_cast<ILInstruction*>(LocalFunctions[static_cast<std::size_t>(i - 1)].get());
        return nullptr;
    }

    void WriteTo(std::string& out) const override {
        out += "ILFunction {\n  ";
        if (Body) Body->WriteTo(out); else out += "(no body)";
        out += "\n}";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i != 0) {
            // Slot 1+ are the LocalFunctions (the C# LocalFunctionsSlot).
            assert(i - 1 >= 0 && i - 1 <= static_cast<int>(LocalFunctions.size()));
            auto& slot = LocalFunctions[static_cast<std::size_t>(i - 1)];
            auto old = std::move(slot);
            slot.reset(static_cast<ILFunction*>(n.release()));
            return old;
        }
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
