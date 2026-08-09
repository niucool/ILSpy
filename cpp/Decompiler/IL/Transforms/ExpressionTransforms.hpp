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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING IN, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/ExpressionTransforms.cs (subset).
// The C# ExpressionTransforms is an ILVisitor IStatementTransform (the second
// child of the GetILTransforms() StatementTransform, after ILInlining) that
// detects simple expression patterns and replaces them with cleaner forms --
// `logic.not(comp op)` -> `comp(op.Negate)` (push negation into the comparison),
// `comp(x != 0)` -> `x` (drop the redundant comparison against 0), and
// `comp.unsigned(left > 0)` / `comp.unsigned(left <= 0)` -> `comp(left != 0)` /
// `comp(left == 0)` (an unsigned compare against 0 is just a null/zero test).
// Should run after inlining so the patterns are visible; the per-statement
// interleaving lets a rewrite that opens up an inlining opportunity trigger the
// ILInlining child without a separate pass.
//
// This port models it as an IStatementTransform with a recursive Visit that
// dispatches on OpCode (the C# ILVisitor's AcceptVisitor). The subset ported
// here is the VisitComp rewrites (D81) plus the VisitIfInstruction rewrites:
// HandleConditionalOperator (`if (cond) stloc A(V1) else stloc A(V2)` ->
// `stloc A(if (cond) V1 else V2)`, the conditional/ternary operator fold), the
// logic.and/or canonicalization (`if (cond) ldc.i4 0 else RHS` ->
// `if (!cond) RHS else ldc.i4 0`, the &&/|| form normalization), and the
// `match(x) ? true : false -> match(x)` fold (a conditional whose condition is
// a pattern match and whose arms are ldc.i4 1/0 is redundant -- the MatchInstruction
// already evaluates to 1/0). Deferred vs the C#: the NullableLiftingTransform
// call (needs the full nullable-lift transform), FixComparisonKindLdNull
// (already in the standalone EarlyExpressionTransforms, D61), the Conv unwrap
// of the right operand (this port's Conv carries no Kind/SignExtend/ZeroExtend,
// so UnwrapConv cannot be faithful), the ldlen / conv o->i null-comparison
// special cases (need Conv Kind), VisitConv / VisitBox / VisitLdElema /
// VisitNewArr / VisitCall / VisitNewObj / VisitLdObj / VisitLdObjIfRef /
// VisitStObj / VisitStLoc (TransformAssignment.HandleCompoundAssign) / the
// remaining VisitIfInstruction pieces (NullableLifting, UserDefinedLogic,
// TransformDynamicAddAssignOrRemoveAssign) / HandleSwitchExpression (needs
// SwitchExpressions setting + SwitchInstruction guards) / VisitDynamic* /
// VisitBinaryNumericInstruction (shift-size) / VisitTryCatchHandler -- each
// needs further infrastructure (AddressOf, LdcDecimal, dynamic nodes, the
// resolver, MatchLogicAnd/Or, IndexRangeTransform, TransformAssignment, ...)
// and is a later iteration.

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Comp;
class IfInstruction;

class ExpressionTransforms : public IStatementTransform {
public:
    // IStatementTransform: visit the statement at `pos` and its children, then
    // (block-model compensation) the if-final's condition when `pos` is the
    // last non-terminal position.
    void Run(Block& block, int pos, StatementTransformContext& context) override;

private:
    // Recursive visitor (the C# ILVisitor's AcceptVisitor / Default). Dispatches
    // on OpCode; non-Comp/non-If nodes recurse into their children (the C# Default).
    void Visit(ILInstruction* inst);

    // VisitComp head rewrites: logic.not push and `comp(x != 0) => x`. Returns
    // true if a rewrite fired (the node was replaced and the result re-visited),
    // so the caller does not recurse into the destroyed node's children.
    bool VisitCompHeadRewrites(Comp* comp);

    // VisitComp tail rewrites (run after recursing into the children):
    // `comp.unsigned(left > 0)` / `comp.unsigned(left <= 0)` normalization.
    // Returns true if a rewrite fired (the node was mutated and re-visited).
    bool VisitCompTailRewrites(Comp* comp);

    // VisitIfInstruction: visit the arms, run HandleConditionalOperator, run the
    // logic.and/or canonicalization, then visit the condition. Adapted to the
    // if-as-final block model (the C# carries the if as a non-terminal at
    // Instructions[Count-2]; this port makes it the block's FinalInstruction).
    void VisitIfInstruction(IfInstruction* iff);

    // Visit an if-arm (the C# visits TrueInst/FalseInst). A Block arm with a
    // FinalInstruction is a control-flow block the block transform already
    // handled (the C# skips BlockKind.ControlFlow); an expression Block arm (no
    // FinalInstruction) and a bare non-Block arm are visited.
    void VisitArm(ILInstruction* arm);

    // HandleConditionalOperator: `if (cond) stloc A(V1) else stloc A(V2)` ->
    // `stloc A(if (!cond) V2 else V1))` (the conditional/ternary operator fold).
    // Both arms must be expression Blocks (no FinalInstruction) with exactly one
    // StLoc to the same variable. Adapted to the if-as-final block model: the
    // StLoc goes into the block's Instructions and a Branch to the next block
    // replaces the if-final (the C# does an in-place ReplaceWith since the if is
    // a non-terminal). Returns true if the rewrite fired (the if is destroyed).
    bool HandleConditionalOperator(IfInstruction* iff);

    // logic.and/or canonicalization: `if (cond) ldc.i4 0 else RHS` ->
    // `if (!cond) RHS else ldc.i4 0` and `if (cond) RHS else ldc.i4 1` ->
    // `if (!cond) ldc.i4 1 else RHS` (swap the arms + negate the condition to
    // bring &&/|| into their canonical forms). The if stays the block's final
    // (no block-model issue); an arm is `ldc.i4 N` either bare or a single-
    // instruction expression Block. Returns true if the arms were swapped.
    bool CanonicalizeLogicAndOr(IfInstruction* iff);

    // `match(x) ? true : false -> match(x)`: a conditional whose condition is a
    // pattern match (MatchInstruction.IsPatternMatch) and whose arms are
    // ldc.i4 1 / ldc.i4 0 is redundant -- the MatchInstruction already evaluates
    // to 1 (matched) / 0 (not matched). The if is replaced by the condition (the
    // pattern match). When the if is a sub-expression value, ReplaceWith is a
    // clean in-place swap (the C# does `inst.ReplaceWith(matchCondition)`); when
    // the if is a block's FinalInstruction (a statement-if), the MatchInstruction
    // (a value, not control flow) cannot be the final, so it becomes a non-
    // terminal statement (its side effect -- storing into Variable -- is
    // preserved) and a Branch to the next block replaces the if-final (the
    // HandleConditionalOperator block-model adaptation). Returns true if the
    // fold fired (the if is destroyed).
    bool FoldMatchTrueFalse(IfInstruction* iff);

    // The settings snapshot for the duration of a Run (the C# stores the
    // StatementTransformContext as a member). Consulted by IsPatternMatch in
    // FoldMatchTrueFalse; null only between Run calls.
    const ILTransformSettings* settings_ = nullptr;
};

} // namespace ILSpy::Decompiler::IL
