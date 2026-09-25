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
// namespace; the CSharpDecompiler-static home is the Phase-7 landing. The same
// header also carries the `CSharpDecompiler.DecompileBodyForAnalysis` prefix --
// the pipeline truncated after the last BlockILTransform plus
// CombineExitsTransform -- which the compiler-generated-code recognizers
// (AutoEventDecompiler, RecordDecompiler) run over a body before matching it
// structurally, and the fixed C# 1.0 settings that analysis runs with.
//
// The port-local approximations the CLI comments documented are carried as-is:
// a transform the C# pipeline has no port for is simply absent from this list
// (the same set of .Run calls the CLI made inline, in the same order).

#pragma once

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/CombineExitsTransform.hpp"
#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/Transforms/DeconstructionTransform.hpp"
#include "Decompiler/IL/Transforms/IndexRangeTransform.hpp"
#include "Decompiler/IL/Transforms/IntroduceNativeIntTypeOnLocals.hpp"
#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"
#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"
#include "Decompiler/IL/Transforms/TransformArrayInitializers.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/FixRemainingIncrements.hpp"
#include "Decompiler/IL/Transforms/ProxyCallReplacer.hpp"
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

// The fixed settings `DecompileBodyForAnalysis` runs with -- the C#
// `new DecompilerSettings(LanguageVersion.CSharp1)` (CSharpDecompiler.cs line
// 204). The C# builds a full DecompilerSettings so the analysis shape is
// independent of the caller's user-visible settings; the port maps the fields
// the ported transforms consult (the language-version-dependent feature gates:
// C# 2 dropped nullables and anonymous methods, C# 6 null propagation and
// string interpolation, C# 7 throw expressions and pattern matching, C# 8
// recursive patterns, C# 9 native integers / relational patterns / pattern
// combinators, C# 11 unsigned right shift and checked operators). Settings the
// port does not model (yield/async, expression trees, ...) are omitted with
// their transforms.
inline ILTransformSettings AnalysisTransformSettings()
{
    ILTransformSettings s;
    // The C# builds these settings internally (LanguageVersion.CSharp1), so the
    // analysis shape does not depend on the caller's user-visible settings.
    s.LiftNullables = false;
    s.AnonymousMethods = false;
    s.NullPropagation = false;
    s.StringInterpolation = false;
    s.ThrowExpressions = false;
    s.PatternMatching = false;
    s.RecursivePatternMatching = false;
    s.PatternCombinators = false;
    s.RelationalPatterns = false;
    s.NativeIntegers = false;
    s.UnsignedRightShift = false;
    s.CheckedOperators = false;
    return s;
}

// The prefix of the C# `CSharpDecompiler.GetILTransforms()` list up to and
// including the last BlockILTransform (the ConditionDetection..StatementTransform
// post-order set), driven inline in the GetILTransforms + RunTransforms order.
// This is the part `DecompileBodyForAnalysis` keeps; `RunGetILTransforms`
// continues with the late transforms. Every comment documents the C# transform
// it stands in for.
inline void RunILTransformsThroughBlockTransforms(ILFunction& function, ILTransformContext& context)
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
    // DetectExitPoints (the C#'s FIRST call, CSharpDecompiler.cs line 103):
    // convert the branches exiting the containers the reader emitted
    // (try regions, the root body) into leave instructions, so the
    // transforms below -- through the second CFS, SwitchDetection and
    // LoopDetection -- see leave-shaped exits (the C# history: "Run
    // IntroduceExitPoints before loop detection, and let loop detection
    // introduce its own exit points" -- the re-run after loop detection
    // handles the loops LoopDetection constructs).
    DetectExitPoints().Run(function, context);
    // ldloca; dup; initobj (Roslyn >= 2 codegen for `var v = default;`
    // + a use of &v): rewrite `stloc s(ldloca v); stobj(ldloc s, default T)`
    // to `stloc v(default T); stloc s(ldloca v)` so `s` can be inlined into
    // its subsequent uses. Runs after the first DetectExitPoints and before
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
    // SwitchOnStringTransform: switch-on-string reconstruction (the
    // string.GetHashCode dictionary or the Roslyn Length/Char shapes),
    // after SwitchDetection and before SwitchOnNullableTransform (per
    // GetILTransforms()). Gated on SwitchStatementOnString (default
    // true); its Length/Char arm on SwitchOnReadOnlySpanChar.
    SwitchOnStringTransform().Run(function, context);
    // SwitchOnNullable: fold the C# compiler's switch-on-
    // Nullable<T> shapes (legacy csc and Roslyn) into a lifted
    // SwitchInstruction with an explicit `case null:` arm. Runs
    // after SwitchDetection and before LoopDetection (per
    // GetILTransforms()), so ifs are still block finals with
    // positional fall-through. Gated on LiftNullables (default true).
    SwitchOnNullableTransform().Run(function, context);
    LoopDetection().Run(function, context);
    // DetectExitPoints (the re-run after loop detection, the C# comment at
    // CSharpDecompiler.cs line 127): replace inner Branch-to-loop-exit with
    // Leave(loop) so the following transforms can restructure
    // `if (cond) leave` patterns (invert to `if (!cond) { body }`).
    DetectExitPoints().Run(function, context);
    // PatternMatching: detect the C# 7.0 `is` patterns Roslyn emits
    // (a type test plus a variable capture) and rewrite the isinst +
    // null-test block tail into a single MatchInstruction condition
    // (`if (expr is T x) ...`). Runs after the DetectExitPoints re-run and
    // before ConditionDetection (per GetILTransforms()), so ifs are still
    // block finals with positional fall-through.
    PatternMatchingTransform().Run(function, context);
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
        // TransformArrayInitializers: the `new T[] { ... }` and
        // `stackalloc T[] { ... }` store-sequence folds (the next
        // per-statement child in the C# GetILTransforms() order, before
        // the collection-initializer and expression-tree children this
        // port does not run in the group). Gated on the ArrayInitializers
        // setting (default true).
        statementTransform.AddChild(std::make_unique<TransformArrayInitializers>());
        // IndexRangeTransform: the System.Index / System.Range pattern
        // folds (the per-statement child between the collection-initializer
        // and expression-tree children and the deconstruction child in the
        // C# GetILTransforms() order). Gated on Ranges (default true).
        statementTransform.AddChild(std::make_unique<IndexRangeTransform>());
        // DeconstructionTransform: the Deconstruct-call-rooted tuple
        // deconstruction fold (the next per-statement child in the C#
        // GetILTransforms() order). Gated on Deconstruction (default true).
        statementTransform.AddChild(std::make_unique<DeconstructionTransform>());
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
}

// The full C# `CSharpDecompiler.GetILTransforms()` list: the block-transform
// prefix plus the late transforms after the last BlockILTransform.
inline void RunGetILTransforms(ILFunction& function, ILTransformContext& context)
{
    RunILTransformsThroughBlockTransforms(function, context);
    // HighLevelLoopTransform: turn the `while (true)` + break
    // structure LoopDetection+ConditionDetection produced into a
    // `while (cond)` container. Runs after the StatementTransform
    // (per GetILTransforms()).
    HighLevelLoopTransform::Run(function, context);
    // ProxyCallReplacer: rewrite compiler-generated pass-through calls to
    // the methods they forward to (the C# slot after the last per-block
    // transform group and before FixRemainingIncrements). No-ops when the
    // AsyncAwait setting is off, when the callee is not a compiler-generated
    // same-class method, or when no DelegateBodyResolver hook is wired (the
    // bare CLI path).
    ProxyCallReplacer().Run(function, context);
    // FixRemainingIncrements: the user-defined `op_Increment`/
    // `op_Decrement` calls TransformAssignment did not fold. Runs
    // after the StatementTransform + HighLevelLoopTransform and before
    // CopyPropagation (per GetILTransforms: ProxyCallReplacer,
    // FixRemainingIncrements, CopyPropagation).
    FixRemainingIncrements().Run(function, context);
    // CopyPropagation: drop dead stores to stack slots and propagate
    // single-def stack slots. Runs late, after the StatementTransform +
    // HighLevelLoopTransform (per GetILTransforms).
    CopyPropagation().Run(function, context);
    // DelegateConstruction: embed the anonymous-method delegate bodies
    // (the C# `new DelegateConstruction()` slot, after CopyPropagation and
    // before the late naming/cleanup passes). No-ops when the
    // AnonymousMethods setting is off, when the target method is not an
    // anonymous method, or when no DelegateBodyResolver hook is wired (the
    // bare CLI path).
    DelegateConstruction().Run(function, context);
    // LocalFunctionDecompiler: embed the local-function definitions at
    // their declaration scopes (the C# slot after DelegateConstruction and
    // before TransformDisplayClassUsage). Gated on LocalFunctions (default
    // true); no-ops without the LocalFunctionBodyResolver hook (the bare CLI
    // path).
    LocalFunctionDecompiler().Run(function, context);
    // IntroduceNativeIntTypeOnLocals: retype the IntPtr/UIntPtr-typed
    // locals whose loads and stores are all native-int-shaped to
    // nint/nuint (the C# slot after IntroduceDynamicTypeOnLocals and
    // before AssignVariableNames). Gated on NativeIntegers.
    IntroduceNativeIntTypeOnLocals().Run(function, context);
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

// CSharpDecompiler.DecompileBodyForAnalysis's transform list
// (CSharpDecompiler.cs lines 205-212): the GetILTransforms() list truncated
// after the last BlockILTransform, with CombineExitsTransform appended so a
// release-mode `return a && b` body stays a single statement. The C# drops
// every transform after the last BlockILTransform (the late naming/cleanup
// passes) before appending it; this runner is the port's equivalent of that
// transform list. Settings are the caller's -- `DecompileBodyForAnalysis`
// supplies the fixed C# 1.0 set (AnalysisTransformSettings).
inline void RunILTransformsForAnalysis(ILFunction& function, ILTransformContext& context)
{
    RunILTransformsThroughBlockTransforms(function, context);
    // Without this, a release-mode `return other != null && ...;` is a chain
    // of conditional exits instead of one statement (the C# comment).
    CombineExitsTransform().Run(function, context);
}

// CSharpDecompiler.DecompileBodyForAnalysis (CSharpDecompiler.cs line 187):
// read a method body and run the analysis transform prefix, so that
// compiler-generated-code recognizers (AutoEventDecompiler, RecordDecompiler)
// structurally match the body regardless of the caller's user-visible settings.
// The C# returns the entry block (or null when the body is absent); the port
// returns the decoded function so the caller reads `function->Body`, and null
// in the same cases (nil token, no body, undecodable IL).
inline std::unique_ptr<ILFunction> DecompileBodyForAnalysis(const Metadata::MetadataFile& file,
                                                            std::uint32_t methodToken,
                                                            std::uint32_t rva)
{
    auto function = ReadIL(file, methodToken, rva);
    if (!function) return nullptr;
    ILTransformContext context;
    context.Settings = AnalysisTransformSettings();
    RunILTransformsForAnalysis(*function, context);
    return function;
}

} // namespace ILSpy::Decompiler::IL
