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
// here is the VisitComp rewrites above (the highest-value, self-contained
// piece). Deferred vs the C#: the NullableLiftingTransform call (needs the full
// nullable-lift transform), FixComparisonKindLdNull (already in the standalone
// EarlyExpressionTransforms, D61), the Conv unwrap of the right operand (this
// port's Conv carries no Kind/SignExtend/ZeroExtend, so UnwrapConv cannot be
// faithful), the ldlen / conv o->i null-comparison special cases (need Conv
// Kind), VisitConv / VisitBox / VisitLdElema / VisitNewArr / VisitCall /
// VisitNewObj / VisitLdObj / VisitLdObjIfRef / VisitStObj / VisitStLoc /
// VisitIfInstruction (HandleConditionalOperator, logic.and/or canonicalization,
// NullableLifting, UserDefinedLogic, match(x)?true:false) / HandleSwitchExpression
// (needs SwitchExpressions setting + SwitchInstruction guards) / VisitDynamic*
// / VisitBinaryNumericInstruction (shift-size) / VisitTryCatchHandler -- each
// needs further infrastructure (AddressOf, LdcDecimal, dynamic nodes, the
// resolver, MatchLogicAnd/Or, IndexRangeTransform, TransformAssignment, ...) and
// is a later iteration.

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Comp;

class ExpressionTransforms : public IStatementTransform {
public:
    // IStatementTransform: visit the statement at `pos` and its children, then
    // (block-model compensation) the if-final's condition when `pos` is the
    // last non-terminal position.
    void Run(Block& block, int pos, StatementTransformContext& context) override;

private:
    // Recursive visitor (the C# ILVisitor's AcceptVisitor / Default). Dispatches
    // on OpCode; non-Comp nodes recurse into their children (the C# Default).
    void Visit(ILInstruction* inst);

    // VisitComp head rewrites: logic.not push and `comp(x != 0) => x`. Returns
    // true if a rewrite fired (the node was replaced and the result re-visited),
    // so the caller does not recurse into the destroyed node's children.
    bool VisitCompHeadRewrites(Comp* comp);

    // VisitComp tail rewrites (run after recursing into the children):
    // `comp.unsigned(left > 0)` / `comp.unsigned(left <= 0)` normalization.
    // Returns true if a rewrite fired (the node was mutated and re-visited).
    bool VisitCompTailRewrites(Comp* comp);
};

} // namespace ILSpy::Decompiler::IL
