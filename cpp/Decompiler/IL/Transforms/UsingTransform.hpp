// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/UsingTransform.cs (subset).
// Detects the C# `using` statement's try/finally pattern -- `stloc
// obj(resource); .try { body } finally { if (obj != null) callvirt
// Dispose(obj) }` -- and folds it into a UsingInstruction (`using (resource)
// { body }`). Runs after ConditionDetection in the BlockILTransform post-order
// set (the GetILTransforms() position, right after LockTransform).
//
// Three finally shapes are matched, all observed on the .NET Framework 4
// mscorlib corpus (probed before implementation, per the D73 precedent -- the
// if-as-final block model diverges from the C# structure, so the real shape must
// be discovered empirically):
//
//   Shape A (reference type, two-block, dominant ~270+): the C# single block
//     `if (obj != null) Block { Dispose(obj) }; leave` becomes TWO blocks here
//     (ConditionDetection's TryInlineIfFallThrough gate blocks inlining the
//     leave into the if's FalseInst): fin[0] = `if (comp(eq, ldloc obj,
//     ldnull)) leave(fin)` (empty Instructions, if-final, TrueInst = the skip
//     leave, FalseInst = nullptr, fall-through), fin[1] = `call
//     System.IDisposable::Dispose(ldloc obj); leave`. The condition is the
//     reader's brfalse form (`obj == null` -> skip), inverted vs the C# `obj !=
//     null` but equivalent.
//
//   Shape B (struct / ref-struct, one-block, ~120+): no null check (a struct
//     cannot be null), so the finally is ONE block: `call
//     System.IDisposable::Dispose(ldloca obj); leave`. The Dispose argument is
//     the address (ldloca), matching the C# MatchLdLocRef.
//
//   Shape C (reference type, two-block with isinst-temp, ~40+): the C# optional
//     `stloc temp(isinst IDisposable(ldloc obj))` at the finally entry stays as
//     fin[0]->Instructions[0], with the if as fin[0]'s FinalInstruction testing
//     the temp: `stloc temp(isinst IDisposable(ldloc obj)); if (comp(eq, ldloc
//     temp, ldnull)) leave(fin)` then fin[1] = `call Dispose(ldloc temp); leave`.
//     The temp must be single-definition and loaded exactly twice (the null
//     check and the Dispose); the UsingInstruction wraps the original resource
//     (the isinst-temp is the compiler's implementation of the null check and
//     is discarded with the finally).
//
// The resource `stloc obj(resource)` sits in the PRECEDING block (the block
// before the TryFinally's own block), the dominant mscorlib case -- CFS does
// not merge the EH wrapper with the preceding block for this shape (the same
// BlockBuilder quirk as the flag-based lock, D73: the fall-through branch into
// the TryFinally resolves to the try entry inside the try container, not the
// wrapper, so the wrapper has IncomingEdgeCount==0 and CFS leaves the stloc in
// a separate preceding block). The preceding-block fold puts the
// UsingInstruction in the preceding block, absorbs the TryFinally's block
// final (the after-using continuation) into it, discards the preceding block's
// `br` into the try (which would dangle into the using body once the TryFinally
// becomes a UsingInstruction), and drops the now-empty TryFinally block to a
// graveyard vector so the container iteration stays valid. The same-block case
// (stloc + TryFinally consecutive in one block, the C# literal shape) is also
// handled for faithfulness but does not arise from the real pipeline.
//
// Deferred vs the C#: TransformUsingVB (the stloc inside the try entry, a VB
// shape), TransformAsyncUsing (needs an Await node + IAsyncDisposable + the
// AsyncUsingAndForEachStatement setting), the NullableOfT dispose
// (MatchHasValueCall, ported in D68 but not yet consulted), the ref-struct
// `call StructType::Dispose` shape (this port matches `System.IDisposable::Dispose`
// only), the boxed-value and NullableRewrap null-check shapes, the
// MatchInstruction-based null check, the isinst-temp inlining inside the try,
// and the non-generic IEnumerator / foreach-pattern CheckResourceType special
// case. IsRefStruct is left false (this port does not model IsByRefLike or the
// IntroduceRefModifiersOnStructs setting). CheckResourceType is permissive
// (returns true): this port's minimal type system cannot check
// GetAllBaseTypes().Any(IDisposable), so the structural MatchDisposeBlock (the
// `System.IDisposable::Dispose` call name) is the real proof (D74).
// SortBlocks(deleteUnreachableBlocks) is unsafe in this port (D58); the
// preceding-block fold moves the dropped block to a graveyard rather than
// erasing it in place.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class UsingTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
