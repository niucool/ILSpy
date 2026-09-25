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

// Port of ICSharpCode.Decompiler/IL/Transforms/CombineExitsTransform.cs -- the
// compact early transform that folds a release-mode control-flow tail
//
//   if (cond) leave (a); leave (b)
//
// into a single `leave (cond ? a : b)`, recursing through a nested
// `if (cond2) leave (a); leave (b)` arm so the whole decision tree becomes one
// leave whose value is a nested conditional. It is not part of the main
// GetILTransforms() pipeline: the C# appends it only in
// CSharpDecompiler.DecompileBodyForAnalysis (so a `return other != null && ...`
// body is a single statement even in release builds) and in
// DelegateConstruction. This port lands the transform itself; the
// `RunILTransformsForAnalysis` prefix that consumes it lives in
// GetILTransforms.hpp.
//
// Block-model adaptation: the C# reads the if and the following leave from
// block.Instructions (the if second-to-last, the leave last). This port stores
// the block terminator in Block.FinalInstruction, so the shape is "the if is
// the last non-final instruction, the leave is the final" and the folded
// result becomes the block's new final.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class Leave;

class CombineExitsTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

private:
    // The C# `static Leave CombineExits(Block block)`: fold the block's
    // `if (cond) leave(a); leave(b)` tail in place and return the new combined
    // leave, or null when the block does not have that shape. A true-branch
    // nested block of the same shape is folded recursively first.
    static Leave* CombineExits(Block* block);
};

} // namespace ILSpy::Decompiler::IL
