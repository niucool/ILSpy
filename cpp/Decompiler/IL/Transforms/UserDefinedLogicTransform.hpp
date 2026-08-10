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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/UserDefinedLogicTransform.cs
// (subset). The C# UserDefinedLogicTransform is an IStatementTransform (a child
// of the GetILTransforms() StatementTransform, after the deferred
// TransformArrayInitializers / TransformCollectionAndObjectInitializers /
// TransformExpressionTrees / IndexRangeTransform / DeconstructionTransform /
// NamedArgumentTransform / RemoveUnconstrainedGenericReferenceTypeCheck, before
// InterpolatedStringTransform) that constructs UserDefinedLogicOperator nodes
// (the C# user-defined short-circuiting `&&` / `||` operator, the
// `op_BitwiseAnd` / `op_BitwiseOr` overloads paired with `op_True` / `op_False`)
// from the legacy-csc and Roslyn-optimized block-tail shapes.
//
// This iteration ports the LegacyPattern (the legacy-csc shape) and the shared
// MatchCondition / MatchBitwiseCall static helpers, adapted to this
// port's if-as-final block model. The RoslynOptimized pattern (the
// "in combination with return statement" shape whose if has leave arms and a
// trailing leave) is deferred: its block-model shape (an if with leave
// early-return arms + a trailing leave as the fall-through) needs a pre-pipeline
// corpus probe of the real post-ConditionDetection form, and the
// .NET Framework 4 legacy-csc mscorlib corpus carries no op_True / op_False
// operator definitions (so neither pattern fires on it -- both are
// faithfulness-only on this corpus, matching the DetectCatchWhenConditionBlocks
// / LdLocaDupInitObj / SwitchOnNullable precedent).
//
// LegacyPattern (the legacy-csc shape), adapted to the if-as-final block model:
//   stloc s(lhsInst)
//   if (logic.not(call op_False(ldloc s))) Block { stloc s(call op_BitwiseAnd(ldloc s, rhsInst)) }
//   ->
//   stloc s(user.logic op_BitwiseAnd(lhsInst, rhsInst))
// The C# reads `block.Instructions[pos]` as the stloc and
// `block.Instructions[pos + 1]` as the if (a non-terminal); this port makes the
// IfInstruction the block's FinalInstruction, so the if is
// `block.FinalInstruction` and "removing" it means replacing the if-final with a
// Branch to the next block (the positional fall-through the if's null FalseInst
// represented -- the legacy pattern has no else; the else is the fall-through to
// the use). The `s.IsUsedWithin(call.Arguments[1])` reject (the rhs must not
// reference s, or short-circuiting would change semantics) is ported as a tree
// walk (the D110 TransformCatchVariable / D130 RecombineVariables precedent, not
// the deferred per-variable use lists).

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class Call;
class ILInstruction;
class ILVariable;

class UserDefinedLogicTransform : public IStatementTransform {
public:
    void Run(Block& block, int pos, StatementTransformContext& context) override;

    // MatchCondition: recognise `call op_True/op_False(ldloc v)` -- a static
    // operator Call (IsOperator, !IsInstanceCall, !IsLifted) with exactly 1
    // argument whose short method name is op_True or op_False, the argument a
    // load of v. Reports the loaded variable and the condition method name.
    // Faithful to the C# UserDefinedLogicTransform.MatchCondition (which checks
    // call.Method.IsOperator, Arguments.Count == 1, !call.IsLifted, the name,
    // and call.Arguments[0].MatchLdLoc(out v)).
    static bool MatchCondition(const ILInstruction* condition,
                               ILVariablePtr& v, std::string& conditionMethodName);

    // MatchBitwiseCall: recognise `call op_BitwiseAnd/op_BitwiseOr(ldloc v,
    // rhs)` -- a static operator Call (IsOperator, !IsInstanceCall, !IsLifted)
    // with exactly 2 arguments whose first is a load of v, and the method name
    // matches the condition (op_False -> op_BitwiseAnd, op_True ->
    // op_BitwiseOr). Faithful to the C# MatchBitwiseCall.
    static bool MatchBitwiseCall(const Call* call, const ILVariable* v,
                                const std::string& conditionMethodName);

    // The C# Transform static method (the shared core that builds a
    // UserDefinedLogicOperator from a condition + a true arm (ldloc) + a false
    // arm (the bitwise call), used by the RoslynOptimized pattern) is deferred:
    // this port's ILInstruction has no virtual Clone(), so the C# `new
    // UserDefinedLogicOperator(call.Method, call.Arguments[0], call.Arguments[1])`
    // (which passes the call's argument instructions, kept alive by GC) would need
    // either a general Clone or an owning-detach that the LegacyPattern does not
    // need (it inlines the construction with a TakeChild detach). It lands with
    // the RoslynOptimized pattern it is the helper for.

private:
    // LegacyPattern: the legacy-csc `stloc s(lhsInst); if (logic.not(call
    // op_False(ldloc s))) Block { stloc s(call op_BitwiseAnd(ldloc s, rhsInst))
    // }` -> `stloc s(user.logic op_BitwiseAnd(lhsInst, rhsInst))` fold, adapted
    // to the if-as-final block model. Returns true if a fold fired.
    bool LegacyPattern(Block& block, int pos, StatementTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
