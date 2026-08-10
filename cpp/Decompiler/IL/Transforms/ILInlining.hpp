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

// Port of ICSharpCode.Decompiler/IL/Transforms/ILInlining.cs (subset). Inlines
// single-use StLoc values into their single load site and removes dead pure
// stores. The third transform in GetILTransforms(), after SplitVariables (which
// the port defers until the reaching-definitions dataflow lands; this subset
// works on the per-variable usage counts ComputeVariableUsage provides).
//
// The C# ILInlining implements three interfaces: IILTransform (whole-function),
// IBlockTransform (per-block), and IStatementTransform (per-statement, the first
// child of the GetILTransforms() StatementTransform). This port implements the
// IILTransform entry (the early whole-function pass) and the IStatementTransform
// entry (the second inlining pass the StatementTransform runs interleaved with
// the other per-statement transforms); the IBlockTransform entry is not wired
// (the port models BlockILTransform post-order transforms as IILTransform).
// The IStatementTransform Run loops InlineOneIfPossible at the given position
// until no change (the C# per-statement overload); the AllowInliningOfLdloca
// option (the ldloca-into-addressof path the C# second pass enables, which needs
// an AddressOf node + IsGeneratedTemporaryForAddressOf + ClassifyExpression) is
// deferred to a later iteration.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class ILFunction;
class ILInstruction;
class ILVariable;

// Inline the StLoc at `pos` into the next instruction's load of its variable,
// or remove it as a dead store. A free function mirroring the C#
// `ILInlining.InlineOneIfPossible(block, pos, InliningOptions.None, ctx)` static
// call (the C# InliningOptions enum is not modeled -- this port has no
// ldloca-inlining / SlotInfo restrictions). Returns true if the stloc was
// consumed. Exposed so other per-statement transforms (e.g.
// NullCoalescingTransform) can call it after a fold that opens up an inlining
// opportunity, matching the C#.
bool InlineOneIfPossible(Block* block, int pos, ILTransformContext& ctx);

// Result of ILInlining::FindLoadInNext -- the search for the single load of a
// variable inside an instruction subtree, into which an expression can be
// inlined. Faithful to the C# ILInlining.FindResultType / FindResult (subset:
// no NamedArgument / Deconstruction, which need SlotInfo / named-argument and
// deconstruct infrastructure this port defers).
//
//   Found    -- a load of the variable was found; inlining is possible (the
//               caller decides whether the load's slot permits it).
//   Stop     -- the load was not found and re-ordering is not possible; abort.
//   Continue -- the load was not found but the expression can be re-ordered
//               past the tested subtree; keep searching.
enum class FindResultType { Found, Stop, Continue };
struct FindResult {
    FindResultType type;
    ILInstruction* loadInst;  // the ldloc/ldloca found (valid when type == Found)
};

// Find the single load of `v` (an LdLoc or an LdLoca) inside `expr` that can be
// replaced by `expressionBeingMoved`, mirroring ILInlining.FindLoadInNext.
// Returns Found for both an LdLoc(v) and an LdLoca(v) match (faithful to the C#,
// which returns Found for both -- the CALLER gates whether an ldloca can
// actually be inlined; this port defers the ldloca-into-addressof path, so the
// inlining caller InlineOneIfPossible skips an LdLoca found). Exposed so other
// per-statement transforms (NullCoalescingTransform's value-types throw-
// expression fold and the hoisted-constructor-argument null guard) can locate
// the use they redirect, matching the C# static call.
FindResult FindLoadInNext(ILInstruction* expr, ILVariable* v,
                          ILInstruction* expressionBeingMoved);

// True when `inst` sits in the constructor initializer -- before the chained
// `: base(...)`/`: this(...)` call -- so a preceding hoisted argument null-guard
// is necessarily compiler-hoisted and can be folded into the call argument.
// Faithful to the C# ILInlining.IsInConstructorInitializer: returns false when
// the function is not an instance constructor with a chained call, or when the
// instruction (or its enclosing top-level statement) ends after the chained call
// starts. The top-level statement is the last ancestor (including `inst` itself)
// whose parent is a Block (the C# inst.Ancestors.LastOrDefault(.. Parent is Block),
// where Ancestors includes the node itself).
bool IsInConstructorInitializer(const ILFunction* function, const ILInstruction* inst);

// The top-level statement containing `inst` -- the last ancestor (including
// inst) whose parent is a Block, or null when no such ancestor exists. Exposed so
// the hoisted-constructor-argument null-guard fold can reuse the ancestor walk.
ILInstruction* TopLevelStatement(const ILInstruction* inst);

class ILInlining : public IILTransform, public IStatementTransform {
public:
    // IILTransform: the whole-function inlining pass (runs early in the
    // pipeline, before InlineReturnTransform).
    void Run(ILFunction& function, ILTransformContext& context) override;
    // IStatementTransform: the per-statement inlining pass (the first child of
    // the StatementTransform). Loops InlineOneIfPossible at `pos` until no
    // change, mirroring the C# ILInlining.Run(Block, pos, ctx) overload.
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
