// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.cs.
//
// A block that stores a known constant (0 or 1) to a stack-slot variable and
// branches to a multi-predecessor block that tests that variable can skip the
// test: the branch is redirected straight to the feasible exit (the arm the
// constant makes the test take), and the now-dead constant store is dropped.
//
//   Block b0 { stloc s(ldc.i4 1); br b1 }      // s is a StackSlot
//   Block b1 (>1 pred) { if (comp(eq, ldloc s, 0)) br X; [fall through to Y] }
//                         // b1's IfInstruction is its final; the fall-through is Y
//   -> b0's `br b1` becomes `br Y` (s == 1 makes comp(eq,1,0) false -> fall through)
//      and b0's dead `stloc s` is removed. b1 keeps its other predecessors.
//
// The C# block model carries the if as a non-terminal with an explicit
// fall-through branch as the block's last instruction; this port's block model
// makes the IfInstruction the block's final with an implicit fall-through to the
// next block, so the two exits are the if's TrueInst target (X) and the next
// block in b1's container (Y).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class RemoveInfeasiblePathTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
