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
// OTHERWISE, ARISING FROM OR CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Remove unreachable blocks from every BlockContainer. A block is reachable
// from the container entry (the first block) via a Branch.TargetBlock, or via
// fall-through from a reachable predecessor whose final does not transfer
// control unconditionally (an IfInstruction's false path falls through; a null
// final falls through; a Branch/Leave/Throw/SwitchInstruction final does not).
// Mirrors the C# BlockContainer.SortBlocks(deleteUnreachableBlocks: true)
// subset -- the C# reorders blocks topologically and drops the unreachable;
// this port keeps the existing (layout-faithful) order and only drops
// unreachable blocks. Run after the structure-changing transforms
// (LoopDetection, ConditionDetection, HighLevelLoopTransform) which can leave
// dead blocks behind (a loop body that branches back to the header leaves its
// fall-through successor unreachable; an inlined fall-through leaves the
// original next block unreachable).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class RemoveUnreachableBlocks : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
