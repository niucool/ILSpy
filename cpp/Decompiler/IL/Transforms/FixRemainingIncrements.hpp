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

// Port of ICSharpCode.Decompiler/IL/Transforms/FixRemainingIncrements.cs.
// The C# FixRemainingIncrements runs in the GetILTransforms() order after the
// StatementTransform (after the deferred ProxyCallReplacer) and before
// CopyPropagation. It handles the user-defined `op_Increment`/`op_Decrement`
// calls that TransformAssignment's inc/dec folds did NOT fold -- the cases where
// "the variable-being-incremented was optimized out by Roslyn", e.g.
//   Console.WriteLine(++num);
// compiles to
//   Console.WriteLine(UserType.op_Increment(a + b));
// (the call's argument is not a load of the store variable, so TransformAssignment's
// MatchLdLoc gate rejects it). The transform turns
//   stloc V(call op_Increment(expr))
// into
//   stloc V(expr)
//   compound.assign op_Increment(V)        // EvaluatesToNewValue, Address target
// so a later pass renders `V = expr; ++V;` as the compound form.
//
// This iteration ports the primary branch (the call is a StLoc's Value and the
// StLoc is a non-terminal instruction in a Block -- the C#
// `call.SlotInfo == StLoc.ValueSlot && call.Parent.SlotInfo == Block.InstructionSlot`
// shape). The else branch (the call is in any other position) needs
// `ILInstruction.Extract` (ILExtraction.cs -- the fresh-temporary-for-expression
// machinery, a larger slice) and is deferred. Decimal op_Increment/op_Decrement
// is skipped (the C# handles it in ReplaceMethodCallsWithOperators; this port
// does not model that resolver path, so the call stays as a call).
//
// Gated indirectly via UserDefinedCompoundAssign::IsIncrementOrDecrement (which
// consults the CheckedOperators setting for the op_CheckedIncrement/Decrement
// variants). Fires 0 times on the .NET Framework 4 legacy-csc mscorlib corpus
// (mscorlib has no non-Decimal op_Increment/op_Decrement operator definitions,
// and the Roslyn "optimized-out variable" codegen is absent from legacy-csc
// output); it fires on Roslyn-compiled / modern .NET. Ported for faithfulness,
// matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNull-
// able precedent.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class FixRemainingIncrements : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
