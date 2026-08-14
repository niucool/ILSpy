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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/ControlFlow/ExitPoints.cs (subset).
// DetectExitPoints finds an "exit point" for a BlockContainer -- the block
// immediately following the container -- and replaces equivalent exit points
// WITHIN the container (a Branch to that block) with a Leave(container). This
// makes it easier for the following transforms (ConditionDetection,
// ReduceNesting) to construct control flow that falls out of blocks instead
// of using goto/break. The C# runs this before ConditionDetection.
//
// This subset handles the dominant case: a Loop/While/For/DoWhile container
// whose exit block (the block after the loop's holder, descending into a
// construct-leading next block) is a single block; every inner Branch to that
// block becomes a Leave(loop). Skipped: the full exit-point-equivalence
// analysis, the "introduce an exit point" case, and non-loop containers.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class DetectExitPoints : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
