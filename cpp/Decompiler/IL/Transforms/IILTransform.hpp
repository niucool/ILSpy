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

// Port of ICSharpCode.Decompiler/IL/Transforms/IILTransform.cs: the per-function
// transform interface and its run context. The C# context carries the type
// system, debug-info provider, full DecompilerSettings and a debug Stepper;
// this port carries the settings the ported transforms consult (growing as
// more of Phase 4 lands) plus a simple step-trace hook.

#pragma once

#include <functional>

namespace ILSpy::Decompiler::IL {

class ILFunction;
class ILInstruction;

// Settings the IL transforms consult (a subset of DecompilerSettings for now).
struct ILTransformSettings {
    bool RemoveDeadStores = false;  // DecompilerSettings.RemoveDeadStores (default false)
    // Sort switch sections by their label value instead of by IL offset. The C#
    // default is false (sort by branch-target IL offset, preserving the original
    // case order); true is a diffing aid for obfuscated assemblies. Used by
    // SwitchDetection.SortSwitchSections.
    bool SortSwitchSections = false;
    // Whether to detect switches compiled to if-chains (non-contiguous case
    // labels) and reconstruct them as SwitchInstructions. DecompilerSettings.
    // SparseIntegerSwitch -- a C# 1.0 setting, default true. SwitchDetection.Run
    // is a no-op when this is off.
    bool SparseIntegerSwitch = true;
    // Whether to delete unreachable blocks left over after a transform.
    // DecompilerSettings.RemoveDeadAndSideEffectFreeCodeUseWithCaution -- an F#
    // decompilation aid, default false. SwitchDetection uses it to choose
    // between SortBlocks(deleteUnreachableBlocks) and Blocks.RemoveAll(empty);
    // this port leaves the dead blocks in place either way (see D58), so the
    // setting only affects whether the analysis allows unreachable cases.
    bool RemoveDeadCode = false;
    // Whether to lift nullable-value operations into nullable-aware forms.
    // DecompilerSettings.LiftNullables -- a C# 2.0 setting, default true (false
    // for C# 1). Gates SwitchOnNullableTransform and the nullable-lifting
    // expression transforms. The nullable-lifting helper subset
    // (MatchHasValueCall / MatchGetValueOrDefault) consults this implicitly via
    // the transforms that call them.
    bool LiftNullables = true;
    // Whether to detect C# 7.0 `is` patterns (type / non-null / var). Gates the
    // PatternMatchingTransform (the next in-order transform after the switch
    // family). DecompilerSettings.PatternMatching -- default true. The
    // MatchInstruction node itself is unconditional; this gates the transform
    // that builds it from isinst + null-test blocks.
    bool PatternMatching = true;
    // Whether to detect C# 8.0 recursive patterns (`expr is C { A: var x } z`).
    // DecompilerSettings.RecursivePatternMatching -- default true. Consulted by
    // the PatternMatchingTransform recursive-sub-pattern path (deferred).
    bool RecursivePatternMatching = true;
    // Whether to detect C# 9.0 `and` / `or` / `not` pattern combinators.
    // DecompilerSettings.PatternCombinators -- default true. MatchInstruction.
    // IsPatternMatch gates a negated pattern (logic.not of a pattern) on this.
    bool PatternCombinators = true;
    // Whether to detect C# 9.0 relational patterns (`is < 42`, `is >= 0`).
    // DecompilerSettings.RelationalPatterns -- default true. MatchInstruction.
    // IsPatternMatch gates comparison kinds other than == / != on this.
    bool RelationalPatterns = true;
    // Whether to detect `lock` statements (the Monitor.Enter/Exit try/finally
    // pattern). DecompilerSettings.LockStatement -- a C# 1.0 setting, default
    // true. Gates LockTransform.
    bool LockStatement = true;
    // Whether to detect `using` statements (the IDisposable try/finally
    // pattern). DecompilerSettings.UsingStatement -- a C# 1.0 setting, default
    // true. Gates UsingTransform (the UsingInstruction node itself is
    // unconditional; this gates the transform that builds it from the
    // stloc + try/finally + Dispose-block pattern).
    bool UsingStatement = true;
    // Whether to detect anonymous-method / lambda constructs (cached-delegate
    // initialization, delegate construction, ...). DecompilerSettings.
    // AnonymousMethods -- default true. Gates CachedDelegateInitialization
    // (the next in-order transform after UsingTransform); the
    // DelegateConstruction.MatchDelegateConstruction helper itself is
    // unconditional (like the NullableLifting helpers), but the transform that
    // consumes it is gated here.
    bool AnonymousMethods = true;
    // Whether to recover array/collection/object initializers (and the
    // compiler-generated ReadOnlySpan<char> cache Roslyn emits for a
    // multi-byte array literal on frameworks without RuntimeHelpers.
    // CreateSpan). DecompilerSettings.ArrayInitializers -- default true.
    // Gates CachedReadOnlySpanInitialization (the next in-order transform after
    // CachedDelegateInitialization), which collapses the lazy cache back to
    // the explicit ReadOnlySpan constructor so the later array-initializer
    // transforms recover the literal and the <PrivateImplementationDetails>
    // cache field disappears from the output.
    bool ArrayInitializers = true;
    // Whether to detect the C# 6.0 null-conditional operator (`?.`).
    // DecompilerSettings.NullPropagation -- default true. Gates
    // NullPropagationTransform (the `v != null ? v.AccessChain : null` ->
    // `v?.AccessChain` lowering), consulted first inside
    // NullableLiftingTransform.Lift (before the LiftNullables-gated lift paths)
    // and as a per-statement child of StatementTransform (the void-call /
    // unconstrained-generic patterns).
    bool NullPropagation = true;
    // Whether to emit throw expressions (`a ?? throw ...`, `arg ?? throw ...`).
    // DecompilerSettings.ThrowExpressions -- a C# 7.0 setting, default true
    // (false only for the C# 6 / .NET Framework 1.x compatibility profile).
    // Gates the NullCoalescingTransform throw-expression folds (the reference-
    // type `a ?? throw ...` arm and the value-type `?.`-with-throw folds) that
    // mutate the Throw node's resultType to O so the NullCoalescingInstruction
    // wrapping it has a matching reference-type result.
    bool ThrowExpressions = true;
};

class ILTransformContext {
public:
    ILTransformSettings Settings;
    // Debug transition log (C# ILTransformContext.Step). Set by tools/tests to
    // observe per-step rewrites; null in production.
    std::function<void(const char* what)> Step;

    void StepOnce(const char* what) const {
        if (Step) Step(what);
    }
};

// Per-function ILAst transform.
class IILTransform {
public:
    virtual ~IILTransform() = default;
    virtual void Run(ILFunction& function, ILTransformContext& context) = 0;
};

} // namespace ILSpy::Decompiler::IL
