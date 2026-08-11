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

// Port of ICSharpCode.Decompiler/IL/Transforms/ReduceNestingTransform.cs.
//
// ReduceNestingTransform improves code quality by duplicating keyword exits
// (return/break/continue) to reduce nesting and restoring IL order, plus a
// separate EliminateRedundantTryFinally pass. This iteration ports ONLY the
// EliminateRedundantTryFinally piece: the C# compiler sometimes wraps a `fixed`
// block (a PinnedRegion after DetectPinnedRegions) in a try-finally whose
// finally is an empty `leave (nop)`; once DetectPinnedRegions has formed the
// PinnedRegion the try-finally is redundant and is replaced with the
// PinnedRegion directly. The nesting-reduction pieces (Visit / ReduceNesting /
// ReduceSwitchNesting / ImproveILOrdering / ExtractElseBlock) need a general
// ILInstruction.Clone for the keyword-exit duplication and are deferred.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class ReduceNestingTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
