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

// Port of ICSharpCode.Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.cs.
//
// A `catch (T e) when (cond)` filter is compiled to a filter block that first
// tests whether the caught object is T (an isinst + null check), branching to
// the when-condition block on success and to a "return 0" block on failure
// (the failure block stores 0 and branches to a shared exit that leaves the
// filter container with the result). The catch is already typed T, so the type
// test is redundant: this transform drops it, leaving the entry block branching
// straight to the when-condition block, and (in the extra-store variant) copying
// the caught exception into the temp the when-condition block reads.
//
//   Block entry (1 pred) {
//     stloc temp(isinst T(ldloc ex))      // 3-instruction variant
//     if (comp(ldloc temp != ldnull)) br whenCond   // block final
//     // fall through to falseBlock
//   }
//   Block falseBlock (1 pred) { stloc ret(ldc.i4 0); br exitBlock }
//   Block exitBlock (2 pred) { leave container(ldloc ret) }
//   =>
//   Block entry (1 pred) { stloc temp(ldloc ex); br whenCond }
//   (falseBlock is now unreachable; exitBlock drops to 1 pred)
//
// This port's block model makes the IfInstruction the block's final with an
// implicit fall-through to the next block (the C# carries the if as a
// non-terminal with an explicit `br falseBlock` as the block's last
// instruction), so the `br falseBlock` is the positional fall-through to the
// next block rather than a separate instruction. The 2-instruction variant
// (no extra temp store) inlines the isinst into the comparison.
//
// Subset ported: the entry-point type-test elision. Skipped:
// `PropagateExceptionVariable` (copy-propagates the exception variable to
// replace the temp/intermediate copies the filter introduces) -- it needs the
// per-variable load/store instruction lists this port does not maintain, and
// is a rendering nicety, not a correctness concern. Also skipped:
// `SortBlocks(deleteUnreachableBlocks: true)` -- erasing the now-unreachable
// falseBlock is unsafe in this port (a block may hold nested containers whose
// inner blocks are still referenced by branches elsewhere; see decision D58),
// so the dead blocks stay in the tree, harmless.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class DetectCatchWhenConditionBlocks : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
