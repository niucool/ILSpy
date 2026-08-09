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

// Port of ICSharpCode.Decompiler/IL/Transforms/RemoveDeadVariableInit.cs (core).
// Runs after EarlyExpressionTransforms (so stobj(ldloca V, ...) has collapsed to
// stloc(V, ...)) and before the second ControlFlowSimplification, per the C#
// GetILTransforms() order. The ported core removes dead stores to variables that
// are never read from: a variable flagged RemoveIfRedundant (e.g. by
// RemoveInfeasiblePath) or under the RemoveDeadStores setting, with LoadCount and
// AddressCount both 0, has every StLoc to it dropped (the value's side effect is
// kept by unwrapping when it is not pure). Dead-copy chains (a removed store whose
// value was a load of a now-dead variable) are handled by a recompute fixpoint.
//
// Skipped vs the C#: ResetUsesInitialValueFlag (needs the DefiniteAssignmentVisitor
// dataflow, a larger slice) and the StackType.Ref stack-slot IType inference (needs
// InferType). IsAsync/IsIterator and StateMachineField are not modeled, so
// removeDeadStores is just the setting (the C# fallback when neither flag is set).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class RemoveDeadVariableInit : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
