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
// already evaluates to 1/0), plus the VisitBox rewrite (`box ref-type(arg)` ->
// `arg`; for a reference type, box is a no-op), the VisitLdElema / VisitNewArr
// CleanUpArrayIndices rewrite (drop the redundant `conv.i` widening of an array
// index -- a SignExtend / ZeroExtend / checked-Truncate conv whose ResultType
// is I -- now that the Conv node carries its ConversionKind, D85), and the
// VisitConv conv.r.un combining rewrite (`conv.r4(conv.r.un(x))` /
// `conv.r8(conv.r.un(x))` -> `conv.r4.un(x)` / `conv.r8.un(x)` -- now unblocked
// by the D85 Conv Kind model), and the VisitBinaryNumericInstruction shift-size
// rewrite (`a << (b & 31)` / `a >> (b & 63)` -> `a << b` / `a >> b` -- a shift's
// right operand masked with the bit-width minus one is redundant in C#), and the
// VisitTryCatchHandler rewrite (TransformCatchVariable / TransformCatchWhen:
// inline the catch-variable copy csc emits at the start of every catch block --
// `catch T; stloc V_0(ldloc E)` -> `catch V_0 : T { <body using V_0> }`).
// Deferred vs the C#: the NullableLiftingTransform
// call (needs the full nullable-lift transform), FixComparisonKindLdNull
// (already in the standalone EarlyExpressionTransforms, D61), the ldlen /
// conv o->i null-comparison special cases (need the LdLen model divergence
// reconciliation -- the VisitConv `conv.i4(ldlen)` first rewrite is still
// blocked by it), the remaining VisitCall pieces (TransformArrayInitializers /
// InlineArrayTransform / TransformAssignment.HandleCompoundAssign -- the
// Nullable<T>.GetValueOrDefault(a, b) -> a ?? b fold is now ported) /
// VisitNewObj / VisitLdObj / VisitLdObjIfRef / VisitStObj / VisitStLoc
// (TransformAssignment.HandleCompoundAssign) / the remaining VisitIfInstruction
// pieces (the NullableLifting Run(IfInstruction) bool? equality folds, the
// `&`/`|` on bool? folds, and the AnalyzeCondition/LiftNormal `v.HasValue ? v :
// fallback => v ?? fallback` early-out are now ported; the remaining Run(IfInstruction)
// paths -- the DoLift / LiftCSharpUserComparison / conv.nop.lifted rest of
// LiftNormal, MatchCompOrDecimal/LiftCSharp*, NullPropagation -- and
// Run(BinaryNumericInstruction) + UserDefinedLogic,
// TransformDynamicAddAssignOrRemoveAssign) / HandleSwitchExpression (needs
// SwitchExpressions setting + SwitchInstruction guards) / VisitDynamic* /
// VisitTryCatchHandler's "remove inlined UnboxAny" branch (needs catch-type
// resolution + per-variable load lists) -- each needs further infrastructure
// (AddressOf, LdcDecimal, dynamic nodes, the resolver, MatchLogicAnd/Or,
// IndexRangeTransform, TransformAssignment, ...) and is a later iteration.

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class ILInstruction;
class Comp;
class Conv;
class Box;
class Call;
class IfInstruction;
class LdElema;
class NewArr;
class BinaryNumericInstruction;
class TryCatchHandler;

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

    // Port of NullableLiftingTransform.Run(Comp comp): the VS2022.10 / Roslyn 4.10.0
    // optimization that turns `a == 42` into `a.GetValueOrDefault() == 42`
    // (no HasValue check) is recognised and lifted back to `comp.lifted[C#](a ==
    // 42)`. A non-lifted equality/inequality whose one side is
    // `call GetValueOrDefault(arg)` on System.Nullable<T> and whose other side is
    // a non-zero integer constant has the GetValueOrDefault call replaced by
    // `ldobj Nullable<T>(arg)` and is marked C#-lifted. Runs after the head
    // rewrites (which handle the value==0 case as logic.not / comp(!=0)=>x) and
    // before recursing into the operands, matching the C# VisitComp order. Gated
    // on LiftNullables (the C# `context.Settings.LiftNullables`). A Comp is always
    // a value, so this is a clean child-slot swap (no block-model adaptation).
    void RunCompNullableLift(Comp* comp);

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

    // RunIfNullableLift: thin caller over LiftNullableCore (the shared Lift core)
    // for NullableLiftingTransform.Run(IfInstruction). On a successful lift,
    // ReplaceIfWithLiftedValue applies the block-model adaptation (ReplaceWith for
    // a sub-expression value-if, or the lifted value becomes a non-terminal + a
    // Branch final for a block-final if). Gated on LiftNullables (checked inside
    // LiftNullableCore). Returns true if the fold fired (the if is destroyed).
    bool RunIfNullableLift(IfInstruction* iff);

    // RunBinaryNumericNullableLift: port of NullableLiftingTransform.Run(
    // BinaryNumericInstruction) -- the VS2017.8 / Roslyn 2.9 `&&`-as-`&`
    // optimization on bool operands, analysed as-if short-circuit via
    // LiftNullableCore (condition = bni.Left, true arm = bni.Right, false arm = a
    // fresh LdcI4(0) owned by falseSink and consumed via ConsumeArm only when a
    // fold needs it). A BNI is always a value, so the lifted result replaces bni
    // via a clean ReplaceWith (no block-model adaptation). Gated on LiftNullables
    // (checked inside LiftNullableCore); the caller additionally gates on both
    // operands being Boolean-typed via IsBooleanValue. Returns true if the lift
    // fired (the BNI is destroyed).
    bool RunBinaryNumericNullableLift(BinaryNumericInstruction* bni);

    // LiftNullableCore: the shared NullableLiftingTransform.Lift core --
    // analyses a conditional (condition ? trueInst : falseInst) for a nullable
    // lift, returning the lifted instruction (owned) or nullptr if no fold fired.
    // `ifInst` is the IfInstruction being lifted (nullptr for the
    // BinaryNumericInstruction caller -- a BNI is not an IfInstruction, so
    // IsProtectedIfInst returns false for it, matching the C# `ifInst as
    // IfInstruction` yielding null). The condition is read-only (detached only
    // by the `&`/`|` on bool? path); the arms are detached on-demand via
    // ConsumeArm, which handles an in-tree arm (an if's arm or a BNI operand --
    // has a Parent) and a fresh-node arm (the BNI falseInst = a fresh LdcI4(0),
    // owned by falseSink and consumed via ConsumeArm only when a fold needs it).
    // The logic.not unwrap loop swaps both the views and the sinks. Gated on
    // NullPropagation (checked before the LiftNullables gate, matching the C#
    // Lift order) then LiftNullables. The ported pieces: the AnalyzeCondition/
    // LiftNormal early-out + conv.nop.lifted + DoLift paths (D97/D98/D100), the
    // MatchCompOrDecimal/LiftCSharp* path (D101), the bool? equality folds
    // (D94), and the `&`/`|` on bool? folds (D96). Deferred: the
    // LiftCSharpUserComparison rest of LiftNormal (needs Call.Method.IsOperator),
    // NullPropagation's remaining modes (NullableByValue / NullableByReference
    // / UnconstrainedType), the Decimal/Call branches of MatchCompOrDecimal,
    // and IsGenericNewPattern. Exposed as a public static method (taking the
    // settings explicitly) so NullableLiftingStatementTransform.RunStatements can
    // call the shared Lift without duplicating it; the two ExpressionTransforms
    // callers (RunIfNullableLift / RunBinaryNumericNullableLift) pass settings_.
public:
    static std::unique_ptr<ILInstruction> LiftNullableCore(
        const ILTransformSettings* settings,
        IfInstruction* ifInst,
        ILInstruction* condition, ILInstruction* trueInst, ILInstruction* falseInst,
        std::unique_ptr<ILInstruction>& trueSink, std::unique_ptr<ILInstruction>& falseSink);
private:

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

    // VisitConv (the conv.r.un combining subset): `conv.r4(conv.r.un(x))` /
    // `conv.r8(conv.r.un(x))` -> `conv.r4.un(x)` / `conv.r8.un(x)`. IL conv.r.un
    // does not indicate whether to convert the target to R4 or R8, so the C#
    // compiler usually follows it with an explicit conv.r4 or conv.r8; the two
    // conversions are combined into one that carries the inner conv's input
    // type/sign but the outer's target (R4/R8). The C# checks
    // `inst.TargetType.IsFloatType() && inst.Argument is Conv conv && conv.Kind
    // == ConversionKind.IntToFloat && conv.TargetType == PrimitiveType.R`; this
    // requires the Conv node's ConversionKind (D85). The `conv.i4(ldlen)` first
    // rewrite is still blocked by the LdLen model divergence (this port's LdLen
    // already returns I4), so only the conv.r.un combining is ported here.
    void VisitConv(Conv* inst);

    // VisitBox: `box ref-type(arg)` -> `arg`. For a reference type, box is a
    // no-op (the value is already on the heap). The C# checks
    // `inst.Type.IsReferenceType == true && inst.Argument.ResultType ==
    // inst.ResultType`; the ResultType guard (the arg is stack-type O, the box is
    // O) is the real protection against a value-type mis-fire (a value type's arg
    // is I4/I8/.., never O), and IsReferenceType (TypeUtils) handles the
    // reference-type kinds, returning nullopt for the uncertain kinds
    // (TypeParameter/ByRef/Pointer/Unknown/...) so the fold is conservative.
    void VisitBox(Box* box);

    // VisitLdElema / VisitNewArr: visit the children (the array and the index
    // expressions), then run CleanUpArrayIndices on the index expressions. The
    // C# VisitLdElema additionally calls IndexRangeTransform.HandleLdElema
    // (deferred -- needs IndexRangeTransform); the CleanUpArrayIndices part is
    // self-contained and fires on the corpus (array accesses with a `conv.i`-
    // widened index). Mirrors ExpressionTransforms.cs.
    void VisitLdElema(LdElema* inst);
    void VisitNewArr(NewArr* inst);

    // CleanUpArrayIndices: drop a `conv.i` (or `conv.ovf.i`) widening of an
    // array index -- a Conv whose ResultType is I and whose Kind is SignExtend,
    // ZeroExtend, or a checked Truncate (Kind == Truncate && CheckForOverflow).
    // Such a conv only widens the index to native int and is redundant in C#
    // (the index is implicitly native-int). An unchecked Truncate (conv.i from
    // I8, no overflow check) is a real truncation and is kept. Mirrors
    // ExpressionTransforms.CleanUpArrayIndices; requires the Conv node's
    // ConversionKind (D85).
    void CleanUpArrayIndices(std::vector<std::unique_ptr<ILInstruction>>& indices);

    // VisitBinaryNumericInstruction (the shift-size subset + the BitAnd nullable
    // lift): `a << (b & 31)` / `a >> (b & 31)` -> `a << b` / `a >> b` -- a shift's
    // right operand masked with the bit-width minus one is redundant in C# (the
    // shift already masks the count). The mask is dropped when it is the expected
    // width for the shift's result type (31 for I4, 63 for I8). The native-int (I)
    // case -- `sizeof(IntPtr) * 8 - 1` -- is deferred (needs SizeOf to carry an
    // IType with GetStackType). The BitAnd case (the C# `case BitAnd` arm) wires
    // NullableLiftingTransform.Run(BinaryNumericInstruction) when both operands
    // are Boolean-typed (the C# InferType == Boolean, via the conservative
    // IsBooleanValue recognizer) -- the `&&`-as-`&` Roslyn optimization on bool,
    // analysed as-if short-circuit. Mirrors ExpressionTransforms.cs.
    void VisitBinaryNumericInstruction(BinaryNumericInstruction* inst);

    // VisitCall (the Nullable<T>.GetValueOrDefault(a, b) -> a ?? b subset): a
    // 2-arg `call GetValueOrDefault(nullableValue, fallback)` on
    // System.Nullable<T> with a pure fallback folds into a NullCoalescingInstruction
    // (NullCoalescingKind::NullableWithValueFallback) whose ValueInst is
    // `ldobj Nullable<T>(nullableValue)` and FallbackInst is the fallback; the
    // UnderlyingResultType is the fallback's ResultType. A Call is always a value
    // (never a block final -- it is not control flow), so the fold is a clean
    // value-position ReplaceWith (the C# `inst.ReplaceWith(replacement)`); no
    // block-model adaptation is needed. The remaining VisitCall pieces
    // (TransformArrayInitializers.TransformRuntimeHelpersCreateSpanInitialization,
    // InlineArrayTransform.RunOnExpression, TransformAssignment.HandleCompoundAssign)
    // are deferred. Mirrors ExpressionTransforms.cs.
    void VisitCall(Call* inst);

    // VisitTryCatchHandler (the TransformCatchVariable / TransformCatchWhen
    // subset): inlines the catch-variable copy csc emits at the start of every
    // catch block. The IL `catch T; stloc V_0(ldloc E)` (the runtime pushes the
    // caught exception into the E_<offset> exception stack slot; csc copies it
    // into the local V_0 and the body uses V_0) becomes `catch V_0 : T { <body
    // using V_0> }` -- V_0 is promoted to VariableKind::ExceptionLocal and becomes
    // the handler's Variable, eliminating the copy. The guard requires
    // handler.Variable (the E slot) to be IsSingleDefinition with LoadCount == 1
    // (this port's ComputeVariableUsage counts the TryCatchHandler itself as the
    // slot's single store -- the caught exception -- so an E slot loaded once by
    // the copy stloc is IsSingleDefinition), and the copy's value to be
    // `ldloc handler.Variable` (optionally wrapped in an unbox.any for a type-
    // parameter catch, deferred while the catch type stays unresolved). Every
    // use of the promoted local must stay inside the handler (a tree walk replaces
    // the C# per-variable LoadInstructions/StoreInstructions/AddressInstructions
    // lists). TransformCatchWhen additionally inlines a single-Leave catch-when
    // filter condition (the filter entry block's leave is this port's
    // FinalInstruction, not a non-terminal as in the C#). The "remove inlined
    // UnboxAny" branch (when the copy was already inlined and the E slot's single
    // load sits inside an UnboxAny) is deferred (needs catch-type resolution +
    // per-variable load lists). Mirrors ExpressionTransforms.cs.
    void VisitTryCatchHandler(TryCatchHandler* handler);
    // TransformCatchVariable: promote the catch entry's `stloc v(ldloc E)` copy
    // local `v` to the handler's catch variable. `isCatchBlock` selects the catch
    // body (true) vs the catch-when filter (false); the ILRange bookkeeping the
    // C# does only for the catch body is deferred (this port carries no
    // per-instruction ILRange).
    void TransformCatchVariable(TryCatchHandler* handler, Block* entryPoint,
                                bool isCatchBlock);
    // TransformCatchWhen: run TransformCatchVariable on the filter entry, then
    // inline a single-instruction catch-when filter (the filter entry block's
    // leave carries the condition) by replacing the filter BlockContainer with
    // the leave's Value.
    void TransformCatchWhen(TryCatchHandler* handler, Block* entryPoint);

    // The settings snapshot for the duration of a Run (the C# stores the
    // StatementTransformContext as a member). Consulted by IsPatternMatch in
    // FoldMatchTrueFalse; null only between Run calls.
    const ILTransformSettings* settings_ = nullptr;
};

} // namespace ILSpy::Decompiler::IL
