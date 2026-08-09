// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.cs.
//
// Roslyn emits a compiler-synthesized lazy cache for a ReadOnlySpan<T> created
// from an array literal on target frameworks without RuntimeHelpers.CreateSpan
// (e.g. .NET Framework or netstandard2.0 + System.Memory):
//
//   stloc V(ldobj T[](ldsflda <PrivateImplementationDetails>.cache))
//   if (comp(ldloc V == ldnull)) {
//       stloc V(arrayInitializer)
//       stobj T[](ldsflda <PrivateImplementationDetails>.cache, ldloc V)
//   }
//   ... single usage of V, e.g. newobj ReadOnlySpan<T>(ldloc V) ...
//
// The transform replaces the load-from-cache with the initializer value and
// drops the if, so a later array-initializer transform recovers the array
// literal and the reference to the escaped <PrivateImplementationDetails>
// cache field (whose name is not expressible in C#) disappears. It mirrors
// CachedDelegateInitialization and runs right after it (the GetILTransforms()
// BlockILTransform post-order position).
//
// Adapted to this port's if-as-final block model. The C# carries the if as a
// non-terminal at block.Instructions[i] with the cache-load store at [i-1]; the
// if's body is a Block in the if's TrueInst. This port makes the IfInstruction
// the block's FinalInstruction, so the cache-load store is the block's last
// non-terminal instruction (Instructions.back()) and the if is the final. In
// this port's pipeline the post-ConditionDetection shape is the C# shape:
// ConditionDetection's TryInlineIfFallThrough inlines the body block into the
// if's FalseInst, then TryInvertIfExit (the usage block is the next block)
// inverts it -- negating the condition to `comp(V == null)` and moving the body
// into the TrueInst as a Block (FalseInst null, fall-through to the usage).
// The one port-specific difference: the body Block carries a trailing Branch
// to the usage block (the reader makes fall-through explicit), which the C#
// body does not; the fold preserves that trailing control flow as the host
// block's new final, dropping the goto when the body falls through to the
// next block (the common cache-pattern case) so no spurious goto survives.
//
// The cache-field gate uses the LdsFlda::IsCompilerGeneratedField flag the IL
// reader pre-resolves from the field token's [CompilerGenerated] custom
// attribute (or its declaring type's) -- the D78 IField foundation. Two
// LdsFlda nodes refer to the same cache field when their FieldToken matches
// (the reader populates it) or, failing that, their FieldName matches.
//
// ReadOnlySpan<T> is absent from the .NET Framework 4 mscorlib corpus, so this
// transform fires 0 times on it (it fires on Roslyn-compiled / modern .NET
// assemblies with System.Memory); the hand-built tests verify the rewrite and
// the sweep verifies the ILAst invariant holds, matching the
// DetectCatchWhenConditionBlocks / LdLocaDupInitObj / SwitchOnNullable
// precedent (a transform that fires only on Roslyn-compiled assemblies is
// still ported for faithfulness).

#pragma once

namespace ILSpy::Decompiler::IL {

class ILFunction;
class ILTransformContext;

class CachedReadOnlySpanInitialization {
public:
    void Run(ILFunction& function, ILTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
