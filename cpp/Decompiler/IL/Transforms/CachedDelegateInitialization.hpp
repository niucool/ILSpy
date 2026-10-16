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
// INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/CachedDelegateInitialization.cs
// (subset). The C# transform collapses the lazy delegate cache the C# and VB
// compilers emit for a lambda -- `if (cache == null) cache = new Delegate(...)`;
// `<use cache>` -- into the unconditional init `cache = new Delegate(...)`;
// `<use cache>`, so a later inlining pass can fold the cache temp into the use.
// It is a per-block IBlockTransform that runs in the BlockILTransform post-order
// set, after ConditionDetection / LockTransform / UsingTransform and before the
// StatementTransform (the GetILTransforms() position).
//
// This iteration ports the CachedDelegateInitializationWithLocal shape only:
//
//   if (comp(ldloc v == ldnull)) { Block { stloc v(DelegateConstruction) } }
//   <one usage of ldloc v>
//   =>
//   stloc v(DelegateConstruction)
//   <one usage of ldloc v>
//
// where v is a local with exactly two stores (the `stloc v(ldnull)` init and the
// `stloc v(Delegate)` cache fill), exactly two loads (the null check and the
// use), and no addresses. The null-init store is dropped and the cache-fill
// store replaces the if, so the lazy cache becomes an unconditional init.
//
// Adapted to this port's if-as-final block model (the C# carries the if as a
// non-terminal at block.Instructions[i] with the single usage as the next
// sibling instruction; this port makes the IfInstruction the block's
// FinalInstruction with an implicit fall-through to the next block in the
// container, so the "next instruction" is NextBlockInContainer -- the C#
// `inst.Parent.Children.ElementAtOrDefault(inst.ChildIndex + 1)` would return
// null for a block final, so the adaptation is essential). The
// `v.StoreInstructions` per-variable list this port does not maintain is
// replaced by a tree walk that gathers the StLoc stores to v (the repeatedly
// deferred infrastructure piece, D11/D62/D68), mirroring the
// PatternMatchingTransform.CheckAllUsesDominatedBy precedent.
//
// The rewrite: detach the cache-fill StLoc from the if's TrueInst Block, drop
// the null-init StLoc from its block, append the cache-fill StLoc to the host
// block's instructions, and replace the if-final with a Branch to the next
// block (the explicit fall-through the if's null FalseInst represented -- the
// block model requires a final, and the edge count is unchanged: the if's
// fall-through and the new Branch each contribute one edge to the next block).
//
// Deferred vs the C#: the field-cached shapes (CachedDelegateInitializationWithField,
// CachedDelegateInitializationRoslynInStaticWithLocal, CachedDelegateInitializationRoslynWithLocal,
// and the three VB shapes) need IField metadata (the cached delegate field's
// CompilerGenerated attribute, or its enclosing type's -- this port models a
// field as just a name string on LdsFlda/LdFlda, with no token or attributes);
// the temp-collapse (folding the cache temp into the use via ILInlining) needs
// the cache-fill store and the use in the same block, which in this port's
// block model requires merging the host block with the next block (a
// block-model-compensation step the C# does not need because its if is a
// non-terminal and the use is the next sibling instruction); and the
// ILInlining.InlineOneIfPossible follow-up the C# calls after the WithLocal
// fold is a no-op here until that merge lands (the use is in the next block,
// not findable by the same-block FindLoadInNext). The .NET Framework 4 legacy
// csc corpus uses the field-cached shape, not the local one, so this subset
// fires 0 times on mscorlib (confirmed by a full-corpus probe); the hand-built
// tests verify the rewrite and the sweep verifies the ILAst invariant holds,
// matching the DetectCatchWhenConditionBlocks / LdLocaDupInitObj /
// SwitchOnNullable precedent (a transform that fires only on Roslyn-compiled
// assemblies is still ported for faithfulness).

#pragma once

namespace ILSpy::Decompiler::IL {

class ILFunction;
class ILTransformContext;

class CachedDelegateInitialization {
public:
    void Run(ILFunction& function, ILTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
