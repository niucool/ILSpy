// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// CSharpDecompiler.GetILTransforms() + ILFunction.RunTransforms -- the fixed
// per-body transform pipeline the C# engine runs over every decompiled method.
// The C# returns the list of IILTransforms and lets callers iterate
// (RunTransforms is a plain loop over Run with invariant checks); this port
// flattens the list into one function so every consumer drives the same
// sequence (previously the --csharp CLI carried it inline, and the BamlDecompiler's
// ConnectionIdRewritePass -- the C# `function.RunTransforms(
// CSharpDecompiler.GetILTransforms(), context)` site -- needs the same list).
// The port has no CSharpDecompiler class yet, so the runner lives in the IL
// namespace; the CSharpDecompiler-static home is the Phase-7 landing.
//
// The port-local approximations the CLI comments documented are carried as-is:
// a transform the C# pipeline has no port for is simply absent from this list
// (the same set of .Run calls the CLI made inline, in the same order).

#pragma once

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/FixRemainingIncrements.hpp"
#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/InterpolatedStringTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/NullCoalescingTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/UserDefinedLogicTransform.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/DetectExitPoints.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"

namespace ILSpy::Decompiler::IL {

// The C# `CSharpDecompiler.GetILTransforms()` list, driven inline in the
// GetILTransforms + RunTransforms order. Every comment documents the C#
// transform it stands in for.
inline void RunGetILTransforms(ILFunction& function, ILTransformContext& context)
{
    // ControlFlowSimplification (1st pass): block cleanup the rest of the
    // pipeline expects.
    ControlFlowSimplification().Run(function, context);
    // StObjToStLoc (the StObjToStLoc piece of EarlyExpressionTransforms the
    // C# folds into its own transform): stobj(ldloca V, value) -> stloc V.
    StObjToStLoc().Run(function, context);
    // ILInlining: the pipeline's first inlining pass.
    ILInlining().Run(function, context);
    // InlineReturnTransform: must run before DetectPinnedRegions (per the
    // C# GetILTransforms() order).
    InlineReturnTransform().Run(function, context);
    // Remove infeasible paths: a block that stores a known constant
    // to a stack slot and branches to a multi-pred test block skips
    // the test (redirected to the feasible exit; dead store dropped).
    RemoveInfeasiblePathTransform().Run(function, context);
    // Detect pinned regions (`fixed` blocks): must run after inlining
    // and before loop detection (per the C# GetILTransforms() order).
    DetectPinnedRegions().Run(function, context);
    // Detect catch-when filter entry points: a `catch (T e) when (...)`
    // filter starts with a redundant isinst type test (the catch is
    // already typed T); drop it so the entry branches straight to the
    // when-condition block. Must run after inlining and before loop
    // detection (per the C# GetILTransforms() order).
    DetectCatchWhenConditionBlocks().Run(function, context);
    // ldloca; dup; initobj (Roslyn >= 2 codegen for `var v = default;`
    // + a use of &v): rewrite `stloc s(ldloca v); stobj(ldloc s, default T)`
    // to `stloc v(default T); stloc s(ldloca v)` so `s` can be inlined into
    // its subsequent uses. Runs after DetectCatchWhenConditionBlocks (the
    // deferred DetectExitPoints would sit here in the C# order) and before
    // the second CFS, per GetILTransforms().
    LdLocaDupInitObjTransform().Run(function, context);
    // Early expression-level rewrites the rest of the pipeline
    // depends on: stobj(ldloca V, ..) -> stloc V, .., ldobj(ldloca V)
    // -> ldloc V (so ILInlining can fold them), and comparison-kind
    // normalization against ldnull. Runs after LdLocaDupInitObjTransform,
    // before the second CFS (per GetILTransforms()).
    EarlyExpressionTransforms().Run(function, context);
    // Remove dead stores to never-read variables: a variable flagged
    // RemoveIfRedundant (by RemoveInfeasiblePath) or under the
    // RemoveDeadStores setting, with no loads or addresses, has its
    // stores dropped. Runs after EarlyExpressionTransforms and before
    // the second CFS, per GetILTransforms().
    RemoveDeadVariableInit().Run(function, context);
    // Re-run CFS so the duplicated 1-pred return blocks merge and
    // the single-definition variable inlines to `leave (expr)`.
    ControlFlowSimplification().Run(function, context);
    // SwitchDetection: reconstruct a C# switch compiled to a sequence
    // of if-statements (non-contiguous case labels) as a single
    // SwitchInstruction, and run SimplifySwitchInstruction as the 2nd
    // pass on the SwitchInstructions the reader emits. Runs after the
    // second CFS and before LoopDetection (per GetILTransforms()), so
    // loops are still flat back-edges the continue/break analysis walks.
    SwitchDetection().Run(function, context);
    // SwitchOnNullable: fold the C# compiler's switch-on-
    // Nullable<T> shapes (legacy csc and Roslyn) into a lifted
    // SwitchInstruction with an explicit `case null:` arm. Runs
    // after SwitchDetection and before LoopDetection (per
    // GetILTransforms()), so ifs are still block finals with
    // positional fall-through. Gated on LiftNullables (default true).
    SwitchOnNullableTransform().Run(function, context);
    LoopDetection().Run(function, context);
    // PatternMatching: detect the C# 7.0 `is` patterns Roslyn emits
    // (a type test plus a variable capture) and rewrite the isinst +
    // null-test block tail into a single MatchInstruction condition
    // (`if (expr is T x) ...`). Runs after LoopDetection and before
    // ConditionDetection (per GetILTransforms()), so ifs are still
    // block finals with positional fall-through.
    PatternMatchingTransform().Run(function, context);
    // DetectExitPoints: replace inner Branch-to-loop-exit with
    // Leave(loop) so the following ConditionDetection can restructure
    // `if (cond) leave` patterns (invert to `if (!cond) { body }`).
    DetectExitPoints().Run(function, context);
    ConditionDetection().Run(function, context);
    // LockTransform: detect the Monitor.Enter/Exit try/finally pattern
    // and fold it into a `lock (expr) { body }`. Runs after
    // ConditionDetection in the BlockILTransform post-order set (per
    // GetILTransforms()). Gated on the LockStatement setting (default true).
    LockTransform().Run(function, context);
    // UsingTransform: detect the IDisposable try/finally pattern and
    // fold it into a `using (resource) { body }`. Runs after
    // ConditionDetection and LockTransform in the BlockILTransform
    // post-order set (per GetILTransforms()). Gated on the
    // UsingStatement setting (default true).
    UsingTransform().Run(function, context);
    // CachedDelegateInitialization: collapse the lazy delegate
    // cache (`if (v == null) v = new Delegate(...)`) into the
    // unconditional init. Runs after ConditionDetection /
    // LockTransform / UsingTransform in the BlockILTransform
    // post-order set, before the StatementTransform (per GetILTransforms()).
    // Gated on the AnonymousMethods setting (default true).
    CachedDelegateInitialization().Run(function, context);
    // CachedReadOnlySpanInitialization: collapse the lazy
    // ReadOnlySpan<T>-from-array-literal cache into the unconditional
    // init. Runs right after CachedDelegateInitialization in the
    // BlockILTransform post-order set (per GetILTransforms()).
    // Gated on the ArrayInitializers setting (default true).
    CachedReadOnlySpanInitialization().Run(function, context);
    // StatementTransform: the BlockILTransform post-order set's final
    // member, a per-statement driver that runs the interleaved
    // statement transforms statement-by-statement with rerun
    // mechanics (per GetILTransforms()).
    {
        StatementTransform statementTransform;
        // ILInlining: the per-statement child, first in the C#
        // GetILTransforms() order (it doesn't trigger re-runs).
        statementTransform.AddChild(std::make_unique<ILInlining>());
        // ExpressionTransforms: the second per-statement child (the C#
        // GetILTransforms() order) -- simple expression folds
        // (comp-not/comparison-against-zero normalization).
        statementTransform.AddChild(std::make_unique<ExpressionTransforms>());
        // TransformAssignment: the inline- and compound-assignment
        // folds (the next per-statement child in the C# GetILTransforms()
        // order).
        statementTransform.AddChild(std::make_unique<TransformAssignment>());
        // NullCoalescingTransform: the reference-type `??` fold
        // (the next per-statement child in the C# GetILTransforms()
        // order).
        statementTransform.AddChild(std::make_unique<NullCoalescingTransform>());
        // NullableLiftingStatementTransform: the block-tail nullable
        // expression lift (the next per-statement child in the C#
        // GetILTransforms() order).
        statementTransform.AddChild(std::make_unique<NullableLiftingStatementTransform>());
        // NullPropagationStatementTransform: the block-tail `?.` /
        // `x ?? y` statement lift (the next per-statement child in the
        // C# GetILTransforms() order).
        statementTransform.AddChild(std::make_unique<NullPropagationStatementTransform>());
        // UserDefinedLogicTransform: the lifted user-defined &&/||/
        // ?? operators on nullable operands (a later per-statement
        // child in the C# GetILTransforms() order).
        statementTransform.AddChild(std::make_unique<UserDefinedLogicTransform>());
        // InterpolatedStringTransform: the C# 10/.NET 6 `$"..."` fold
        // (the last per-statement child in the C# GetILTransforms()
        // order). Gated on the StringInterpolation setting (default true).
        statementTransform.AddChild(std::make_unique<InterpolatedStringTransform>());
        statementTransform.Run(function, context);
    }
    // HighLevelLoopTransform: turn the `while (true)` + break
    // structure LoopDetection+ConditionDetection produced into a
    // `while (cond)` container. Runs after the StatementTransform
    // (per GetILTransforms()).
    HighLevelLoopTransform::Run(function, context);
    // FixRemainingIncrements: the user-defined `op_Increment`/
    // `op_Decrement` calls TransformAssignment did not fold. Runs
    // after the StatementTransform + HighLevelLoopTransform and before
    // CopyPropagation (per GetILTransforms: ProxyCallReplacer,
    // FixRemainingIncrements, CopyPropagation; ProxyCallReplacer is
    // deferred).
    FixRemainingIncrements().Run(function, context);
    // CopyPropagation: drop dead stores to stack slots and propagate
    // single-def stack slots. Runs late, after the StatementTransform +
    // HighLevelLoopTransform (per GetILTransforms).
    CopyPropagation().Run(function, context);
    AssignVariableNames().Run(function, context);
    // ReduceNestingTransform: EliminateRedundantTryFinally +
    // ImproveILOrdering. Runs after HighLevelLoopTransform (per
    // GetILTransforms: the C# order is HighLevelLoopTransform,
    // ReduceNestingTransform, RemoveRedundantReturn).
    ReduceNestingTransform().Run(function, context);
    // RemoveUnreachableBlocks: drop blocks with no reachable path from
    // the container entry (the C# BlockContainer.SortBlocks(
    // deleteUnreachableBlocks: true) subset). Runs BEFORE
    // RemoveRedundantReturn so a trailing empty dead block does not
    // shadow the real last reachable block's `return;`.
    RemoveUnreachableBlocks().Run(function, context);
    RemoveRedundantReturn().Run(function, context);
}

} // namespace ILSpy::Decompiler::IL
