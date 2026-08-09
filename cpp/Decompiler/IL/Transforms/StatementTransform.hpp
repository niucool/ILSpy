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

// Port of ICSharpCode.Decompiler/IL/Transforms/StatementTransform.cs: the
// per-statement transform framework. The C# GetILTransforms() BlockILTransform
// post-order set ends with a StatementTransform holding the interleaved
// per-statement transforms (ILInlining, ExpressionTransforms, TransformAssignment,
// ...) that run statement-by-statement with rerun mechanics. This port models the
// BlockILTransform post-order transforms as IILTransform (whole-function, iterating
// blocks internally), so StatementTransform is an IILTransform that walks every
// Block and runs the per-statement driver on it.
//
// The per-statement driver (StatementTransform.RunBlock) walks the block's
// non-terminal Instructions from last to first. At each position it runs every
// child IStatementTransform in order; a child may call RequestRerun(pos) to jump
// back to pos and re-run all children, or RequestRerun() to re-run all children at
// the current position. This interleaving lets a transform that opens up a new
// inlining opportunity (e.g. TransformAssignment) trigger the ILInlining child to
// fold it, without a separate full-block pass. This iteration ports the
// orchestration and wires the first child (ILInlining, the C# pipeline's second
// inlining pass); the remaining children (ExpressionTransforms, TransformAssignment,
// NullCoalescingTransform, ...) are deferred, and the ILInlining AllowInliningOfLdloca
// option (the ldloca-into-addressof path that needs an AddressOf node +
// IsGeneratedTemporaryForAddressOf + ClassifyExpression) is deferred to a later
// iteration.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::IL {

class Block;

// Per-statement transform context: wraps the underlying ILTransformContext (the
// settings the children consult + the step hook) and carries the rerun state.
// Mirrors the C# StatementTransformContext (a subclass of ILTransformContext that
// adds BlockContext, the current Block, and the rerunPosition/rerunCurrentPosition
// fields).
class StatementTransformContext {
public:
    // The underlying function-level context (settings + step hook).
    ILTransformContext& Base;
    // The block the per-statement driver is running on.
    Block* const Block_;

    StatementTransformContext(ILTransformContext& base, Block* block)
        : Base(base), Block_(block) {}

    // After the current statement transform has completed, re-run all statement
    // transforms (including the current one) starting at the specified position.
    // The driver jumps pos back to `pos` (which must be >= the current pos; the
    // children only ever request a rerun at or before the current position so
    // the driver does not skip unprocessed statements).
    void RequestRerun(int pos) {
        if (!hasRerunPosition_ || pos > rerunPosition_) {
            rerunPosition_ = pos;
            hasRerunPosition_ = true;
        }
    }
    // After the current statement transform has completed, repeat all statement
    // transforms at the current position.
    void RequestRerunCurrentPosition() {
        rerunCurrentPosition_ = true;
    }

    bool HasRerunPosition() const { return hasRerunPosition_; }
    int RerunPosition() const { return rerunPosition_; }
    bool HasRerunCurrentPosition() const { return rerunCurrentPosition_; }
    void ClearRerunPosition() { hasRerunPosition_ = false; }
    void ClearRerunCurrentPosition() { rerunCurrentPosition_ = false; }

private:
    bool rerunCurrentPosition_ = false;
    bool hasRerunPosition_ = false;
    int rerunPosition_ = 0;
};

// A transform that runs on a sequence of statements within a block. The driver
// calls Run(block, pos, ctx) for pos = block.Instructions.Count-1, Count-2, ...,
// 0; the transform may only modify block.Instructions[pos..] (statements at or
// after pos). Mirrors the C# IStatementTransform.
class IStatementTransform {
public:
    virtual ~IStatementTransform() = default;
    virtual void Run(Block& block, int pos, StatementTransformContext& context) = 0;
};

// Block transform that runs a list of statement transforms, interleaved per
// statement. Mirrors the C# StatementTransform (an IBlockTransform). This port
// makes it an IILTransform that iterates every Block in the function and runs the
// per-statement driver on each.
class StatementTransform : public IILTransform {
public:
    StatementTransform() = default;

    // Append a per-statement transform (the C# ctor takes a params array; the
    // port builds the list with AddChild so the children can be constructed
    // individually at the call site).
    void AddChild(std::unique_ptr<IStatementTransform> child) {
        children_.push_back(std::move(child));
    }

    void Run(ILFunction& function, ILTransformContext& context) override;

private:
    std::vector<std::unique_ptr<IStatementTransform>> children_;

    void RunBlock(Block* block, ILTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
