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

#include <cstdint>
#include <functional>
#include <memory>

namespace ILSpy::Decompiler::CSharp::Resolver {
class CSharpResolver;
}

namespace ILSpy::Decompiler {
class DecompilerSettings;
}

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::TypeSystem {
class ICompilation;
}

namespace ILSpy::Decompiler::TypeSystem {
class ITypeDefinition;
}  // namespace ILSpy::Decompiler::TypeSystem

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
    // Whether to decompile switch statements over strings using
    // string.GetHashCode. DecompilerSettings.SwitchStatementOnString
    // (default true; SwitchOnStringTransform's Run gate).
    bool SwitchStatementOnString = true;
    // Whether to use the Length/Char optimization for switches over
    // ReadOnlySpan<char>. DecompilerSettings.SwitchOnReadOnlySpanChar
    // (default true; SwitchOnStringTransform's Length/Char arm).
    bool SwitchOnReadOnlySpanChar = true;
    // Whether to decompile local functions. DecompilerSettings.LocalFunctions
    // (default true; LocalFunctionDecompiler's gate).
    bool LocalFunctions = true;
    // Whether to use C# 8.0 index/range syntax.
    // DecompilerSettings.Ranges (default true; IndexRangeTransform's gate).
    bool Ranges = true;
    // Whether to decompile yield-return enumerators.
    // DecompilerSettings.YieldReturn (default true;
    // YieldReturnDecompiler's gate).
    bool YieldReturn = true;
    // Whether to deconstruct tuples into separate variables.
    // DecompilerSettings.Deconstruction (default true;
    // DeconstructionTransform's gate).
    bool Deconstruction = true;
    // Whether to decompile inline arrays (C# 12 stackalloc-shaped
    // InlineArray structs). DecompilerSettings.InlineArrays (default true;
    // InlineArrayTransform's gate).
    bool InlineArrays = true;
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
    // Whether the decompiler may assume that `ldlen; conv.i4.ovf` does not throw
    // an overflow exception (array lengths fit into int32). DecompilerSettings.
    // AssumeArrayLengthFitsIntoInt32 -- default true. Gates the VisitConv
    // `conv.iN(ldlen)` fold for the checked (`conv.ovf`) variants, so a checked
    // conv.i4.ovf(ldlen) folds to ldlen.i4 only when the assumption holds.
    bool AssumeArrayLengthFitsIntoInt32 = true;
    // Whether to emit compound assignment expressions (`+=`, `-=`, ...).
    // DecompilerSettings.MakeAssignmentExpressions -- a C# 2.0 setting, default
    // true. Gates TransformAssignment.HandleCompoundAssign (the next in-order
    // per-statement child of StatementTransform), which folds
    // `stloc V(binary.op(ldloc V, rhs))` into a NumericCompoundAssign
    // (`V op= rhs`). The CompoundAssignmentInstruction / NumericCompoundAssign
    // ILAst nodes (the foundation) are unconditional; this gates the transform
    // that builds them.
    bool MakeAssignmentExpressions = true;
    // Whether to emit pre/post-increment and -decrement (`++V`/`V--`).
    // DecompilerSettings.IntroduceIncrementAndDecrement -- default true. Gates
    // the increment/decrement cases of TransformAssignment.HandleCompoundAssign
    // (a NumericCompoundAssign with Add/Sub + a ldc.i4 1 RHS and the
    // EvaluatesToOldValue (post) / EvaluatesToNewValue (pre) EvalMode). Both
    // MakeAssignmentExpressions and IntroduceIncrementAndDecrement must be true
    // for any compound assignment (incl. increment/decrement) to be introduced.
    bool IntroduceIncrementAndDecrement = true;
    // Whether to use the C# 9.0 `nint`/`nuint` native-integer types.
    // DecompilerSettings.NativeIntegers -- a C# 9.0 setting, default true.
    // NumericCompoundAssign.IsBinaryCompatibleWithType consults this: a
    // compound assign to a System.IntPtr/UIntPtr LHS (but not nint/nuint) is
    // only allowed when native integers are available (the RHS must be cast to
    // n(u)int); with the setting off the compound assign is rejected.
    bool NativeIntegers = true;
    // Whether to use the C# 11.0 unsigned right-shift operator (`>>>`).
    // DecompilerSettings.UnsignedRightShift -- a C# 11.0 setting, default
    // true. NumericCompoundAssign.IsBinaryCompatibleWithType consults this:
    // an unsigned right shift against a type whose sign does not match is
    // allowed only when the unsigned-right-shift operator is available (the
    // `signMismatchAllowed` gate).
    bool UnsignedRightShift = true;
    // Whether to use C# 11.0 user-defined checked operators
    // (`op_CheckedIncrement` / `op_CheckedDecrement`). DecompilerSettings.
    // CheckedOperators -- a C# 11.0 setting, default true (false only for the
    // C# 10 / .NET Framework 1.x compatibility profile). Consulted by
    // `UserDefinedCompoundAssign::IsIncrementOrDecrement`: the checked
    // operator names are only recognised as increment/decrement when this is
    // on (the C# `settings?.CheckedOperators ?? true`); with it off a checked
    // operator call does not fold to `++`/`--` and stays as a regular call.
    bool CheckedOperators = true;

    // DecompilerSettings.StringInterpolation -- a C# 6.0 setting, default true
    // (false only for the C# < 6.0 / VS 2013 compatibility profile, per
    // SetLanguageVersion). Gates InterpolatedStringTransform (the C# 10/.NET 6
    // `$"..."` via DefaultInterpolatedStringHandler lowering): with it off the
    // handler-construction + AppendLiteral/AppendFormatted + ToStringAndClear
    // call sequence stays as the raw calls instead of folding to a $"..."
    // InterpolatedString block.
    bool StringInterpolation = true;
    // DecompilerSettings.NamedArguments (the C# `UseNamedArguments`, default
    // true). Gates NamedArgumentTransform: with it off a call whose argument
    // ordering blocks inlining keeps the original order instead of being
    // rewritten to a named-argument call. Consulted by the transform's Run.
    bool NamedArguments = true;
    // DecompilerSettings.ObjectOrCollectionInitializers -- a C# 3.0 setting,
    // default true. Gates TransformCollectionAndObjectInitializers: with it
    // off the `stloc v(newobj T(...)); call set_P(ldloc v, ...)` statement
    // sequences stay as separate statements instead of folding into an
    // object/collection-initializer block.
    bool ObjectOrCollectionInitializers = true;
    // DecompilerSettings.UseObjectCreationOfGenericTypeParameter -- a C# 2.0
    // setting, default true. Consulted by the same transform's
    // Activator.CreateInstance<T> arm: with it off the `stloc v(call
    // Activator.CreateInstance<T>())` head stays a call instead of becoming
    // `new T()` (the `default(T)` object-creation render).
    bool UseObjectCreationOfGenericTypeParameter = true;
    // DecompilerSettings.WithExpressions -- a C# 9.0 setting, default true.
    // Consulted by the same transform's record-clone arm: with it off a
    // `<Clone>$` call head stays a call instead of becoming a
    // BlockKind.WithInitializer block.
    bool WithExpressions = true;
    // DecompilerSettings.DictionaryInitializers -- a C# 6.0 setting, default
    // true. Consulted by TransformCollectionAndObjectInitializers.
    // IsPartOfInitializer: with it off the single-definition local stores the
    // scan collects as possible index variables (the C# 6
    // `{ [key] = value }` dictionary-initializer indices) are rejected, and
    // GetAccessPath's accessor-path arm drops parameterized accesses (the
    // `settings?.DictionaryInitializers == false` gate).
    bool DictionaryInitializers = true;
};

class ILTransformContext {
public:
    ILTransformSettings Settings;
    // The C# ILTransformContext's full DecompilerSettings (`context.Settings`,
    // defaulting to `new DecompilerSettings()` when the caller passes null) and
    // its lazily-created CSharpResolver (`context.CSharpResolver`) -- the two
    // C#-layer fields the C# context carries natively, which cross-layer
    // transforms (TransformCollectionAndObjectInitializers's IsPartOfInitializer
    // passes both into AccessPathElement.GetAccessPath) consult. The port keeps
    // them as nullable non-owning handles following the D78 convention: the
    // IL-layer pipeline callers (the seed CLI paths) leave them null, which
    // reproduces the C# default-constructed-settings gates (both consulted
    // settings default on) and skips the resolver-driven applicability checks
    // (GetAccessPath's `resolver != null` branches); the C#-layer pipeline and
    // the tests set them for the faithful resolver-checked behavior. The
    // resolver member name avoids the self-named-member trap (a member named
    // CSharpResolver would shadow the CSharpResolver TYPE in the class scope).
    const ::ILSpy::Decompiler::DecompilerSettings* CSharpSettings = nullptr;
    ::ILSpy::Decompiler::CSharp::Resolver::CSharpResolver* Resolver = nullptr;
    // Debug transition log (C# ILTransformContext.Step). Set by tools/tests to
    // observe per-step rewrites; null in production.
    std::function<void(const char* what)> Step;

    // The delegate-body deep-decode hook (the facade's addition): resolves a
    // delegate-construction method's body out-of-band through the metadata. The
    // hook takes the MethodDef token and the RVA and returns the decoded
    // ILFunction (the caller sets Kind/DelegateType), or null when the body does
    // not decode. Unset by default; production wiring is ReadIL(module, token,
    // rva) at the facade call site.
    std::function<std::unique_ptr<ILFunction>(std::uint32_t methodToken,
                                              std::uint32_t methodRva)>
        DelegateBodyResolver;

    // The module the C# ILTransformContext carries as its PEFile slot (the
    // metadata the DelegateConstruction transform reads the method bodies and
    // the local-function gates through). Null on the bare CLI path (the seed
    // readers own their file handles) and in tests that wire the
    // DelegateBodyResolver hook directly; the facade sets it.
    Metadata::MetadataFile* Metadata = nullptr;

    // The C# `public IDecompilerTypeSystem TypeSystem` (ILTransformContext)
    // -- the decompilation's type system the transforms consult (the port
    // stores the ICompilation; the port's DecompilerTypeSystem IS one). Null
    // in the minimal construction; the facade and the tests set it.
    ::ILSpy::Decompiler::TypeSystem::ICompilation* TypeSystem = nullptr;
    // The C# `ReadLocalFunctionDefinition` deep-decode entry
    // (LocalFunctionDecompiler.cs): resolves a local function's decoded body
    // from the metadata (the C# path reads the method body through
    // context.CreateILReader(); the port's hook is the CreateILReader
    // bridge, the DelegateBodyResolver convention). The hook takes the full
    // method name (the "Namespace.Type::<caller>g__fn|n" identity the IL
    // reader records) and returns the decoded ILFunction, or null when the
    // method has no decodable body. Unset by default; the
    // LocalFunctionDecompiler walk consults it on a use-site's first
    // sighting.
    std::function<std::unique_ptr<ILFunction>(const std::string&)>
        LocalFunctionBodyResolver;
    // The C# `resolveContext.CurrentTypeDefinition` (the
    // SimpleTypeResolveContext(function.Method) of LocalFunctionDecompiler.Run):
    // the declaring type of the function being decompiled -- the current-type
    // anchor the closure-parameter / potential-closure checks consult. Null in
    // the minimal construction (the checks then reject: a null current type is
    // not part of any type tree).
    const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition*
        CurrentTypeDefinition = nullptr;

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
