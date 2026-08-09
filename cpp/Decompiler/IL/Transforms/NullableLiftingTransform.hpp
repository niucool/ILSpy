// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING IN, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the static pattern-matching helpers of
// ICSharpCode.Decompiler/IL/Transforms/NullableLiftingTransform.cs that the
// switch-on-nullable family (SwitchOnNullableTransform, and SwitchDetection's
// AddNullCase) depend on. The full NullableLiftingStatementTransform / lift
// rewriting is a larger slice and is deferred; this header exposes the
// self-contained shape matchers that recognise Nullable<T>'s HasValue /
// GetValueOrDefault access patterns:
//
//   - MatchHasValueCall: `call get_HasValue(arg)` on System.Nullable<T> -> arg.
//   - MatchGetValueOrDefault: `call GetValueOrDefault(arg)` (the 1-arg form)
//     on System.Nullable<T> -> arg, and the 2-argument form
//     `call GetValueOrDefault(nullableValue, fallback)` -> (nullableValue, fallback)
//     consumed by the ExpressionTransforms.VisitCall `a ?? b` fold.
//   - MatchCompOrDecimal: recognises a non-lifted IL `Comp` and reports its
//     Kind/Left/Right/IsLifted via the CompOrDecimal struct; the lift
//     machinery consults it to recognise the C#-style lifted comparison shape.
//     The Decimal-operator branch (a Call to op_Equality/... on System.Decimal,
//     gated on Call::IsOperator + the 6-comparison-operator name switch + a
//     System.Decimal declaring type) is also ported; the Decimal lift itself
//     (LiftCSharpUser*, needs the C# resolver) stays deferred.
//
// The C# checks `call.Method.Name` and `call.Method.DeclaringTypeDefinition?
// .KnownTypeCode == KnownTypeCode.NullableOfT`. This port's Call carries the
// resolved declaring type as an IType (Call::DeclaringType, set by the IL
// reader); the generic definition is unwrapped (a ParameterizedType's
// GenericType, e.g. Nullable<int> -> Nullable`1) and its KnownTypeCode read.
// A null declaring type is treated like the C# null DeclaringTypeDefinition --
// the helpers return false, so a method whose declaring type cannot be
// resolved never matches.

#pragma once

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <memory>

namespace ILSpy::Decompiler::IL {

class ILInstruction;
class Conv;
class BinaryNumericInstruction;
class BitNot;
using BitSet = ILSpy::Decompiler::Util::BitSet;

// Port of NullableLiftingTransform.CompOrDecimal: either a non-lifted IL `Comp`
// or a call to one of the 6 comparison operators on System.Decimal. This port
// carries both cases: the `Comp` case (Kind/Left/Right/IsLifted from the Comp)
// and the `Decimal` case (a Call with IsOperator, 2 args, one of the 6
// comparison operator names, and a System.Decimal declaring type -- the C#
// `call.Method.IsOperator && Arguments.Count == 2 && !IsLifted` + the name
// switch + `DeclaringType.IsKnownType(KnownTypeCode.Decimal)`). The fields mirror
// the C# struct so the nullable-lifting lift machinery that consults it reads
// the same shape; `Instruction` is the matched node (a Comp or a Call);
// `Left`/`Right` are its operands (the Comp's children or the Call's 2 args);
// `Kind` is the comparison kind; `IsLifted` is Comp.IsLifted() / false (Call).
struct CompOrDecimal {
    ILInstruction* Instruction = nullptr;
    ComparisonKind Kind = ComparisonKind::Equality;
    ILInstruction* Left = nullptr;
    ILInstruction* Right = nullptr;
    bool IsLifted = false;

    // Port of CompOrDecimal.MakeLifted(newComparisonKind, left, right): builds a
    // C#-lifted instruction from the two (already-lifted) operands. The Comp
    // branch constructs a `Comp(newComparisonKind, ComparisonLiftingKind.CSharp,
    // comp.InputType, comp.Sign, left, right)` (the D91 model) -- a C#-style lifted
    // comparison whose operands' ResultType is O (Nullable<T>). The Decimal/Call
    // branch (a Call to a lifted user-defined operator, needs
    // CSharpOperators.LiftUserDefinedOperator) is deferred -- the existing
    // LiftCSharp* consumers bail for a Call CompOrDecimal (MakeLifted returns
    // null here, and DoLift/DoLiftBinary bail on the Call's non-nullable
    // arguments), so recognising a Decimal comparison call is safe (no fold fires
    // until the resolver-backed lift lands). `LeftExpectedType`/
    // `RightExpectedType` (the Call-branch parameter types) are not carried: for
    // the Comp branch both are SpecialType.UnknownType (nullptr), which the
    // caller passes directly to DoLiftBinary. Returns null for a non-Comp
    // Instruction (the deferred Call branch).
    std::unique_ptr<ILInstruction> MakeLifted(
        ComparisonKind newComparisonKind,
        std::unique_ptr<ILInstruction> left,
        std::unique_ptr<ILInstruction> right) const;
};

// The static helper subset of NullableLiftingTransform. The full
// NullableLiftingStatementTransform (the nullable-expression lifting) is
// deferred; only the shape matchers the switch-on-nullable family consumes are
// exposed here, as static methods so call sites read
// `NullableLiftingTransform::MatchHasValueCall(..)` as in the C#.
class NullableLiftingTransform {
public:
    // Port of NullableLiftingTransform.MatchHasValueCall(inst, out ILInstruction arg):
    // returns true and sets `arg` when `inst` is `call get_HasValue(arg)` on
    // System.Nullable<T> (1 argument). The call's declaring type must resolve to
    // KnownTypeCode::NullableOfT (a generic instantiation unwraps to its
    // generic definition); a null DeclaringType does not match.
    static bool MatchHasValueCall(ILInstruction* inst, ILInstruction*& arg);

    // Port of NullableLiftingTransform.MatchGetValueOrDefault(inst, out ILInstruction arg):
    // the 1-argument form `call GetValueOrDefault(arg)` on System.Nullable<T>
    // (the underlying-value accessor). The switch-on-nullable patterns use this
    // 1-arg form.
    static bool MatchGetValueOrDefault(ILInstruction* inst, ILInstruction*& arg);

    // Port of NullableLiftingTransform.MatchGetValueOrDefault(inst, out
    // nullableValue, out fallback): the 2-argument form
    // `call GetValueOrDefault(nullableValue, fallback)` on System.Nullable<T>
    // (the value-or-fallback accessor -- the `a ?? b` lowering the
    // ExpressionTransforms.VisitCall fold consumes). The call must resolve its
    // declaring type to KnownTypeCode::NullableOfT (a generic instantiation
    // unwraps to its generic definition); a null DeclaringType does not match.
    static bool MatchGetValueOrDefault(ILInstruction* inst,
                                       ILInstruction*& nullableValue,
                                       ILInstruction*& fallback);

    // Port of NullableLiftingTransform.MatchCompOrDecimal(inst, out result):
    // recognises a non-lifted IL `Comp` and reports its Kind/Left/Right/IsLifted
    // via `result`. The Decimal-operator branch (a Call to op_Equality/
    // op_Inequality/op_LessThan/op_LessThanOrEqual/op_GreaterThan/
    // op_GreaterThanOrEqual on System.Decimal) is also ported -- it needs
    // `Call::IsOperator` (set by the IL reader from the `op_*` method name) +
    // the 6-comparison-operator name switch + a System.Decimal declaring type.
    // The Decimal lift (LiftCSharpUser*, which build a lifted user-defined
    // operator via CSharpOperators.LiftUserDefinedOperator) is deferred -- needs
    // the C# resolver; the recognition here is the foundation those consumers
    // will consult. Returns false for every other instruction kind, matching the
    // C# fall-through.
    static bool MatchCompOrDecimal(ILInstruction* inst, CompOrDecimal& result);

    // Port of NullableType.GetUnderlyingType(type): for a Nullable<T> -- a
    // ParameterizedType whose generic definition is KnownType(NullableOfT) with
    // one type argument -- returns the type argument T (non-owning); otherwise
    // null. This port's IType for `Nullable<int>` is
    // `ParameterizedType(KnownType(NullableOfT), {Int32})`; a bare `Nullable`1`
    // (the generic definition) has no type argument and returns null. The full
    // C# NullableType.GetUnderlyingType also unwraps type parameters and other
    // nullable wrappers -- deferred (this port's minimal type system carries no
    // NullableType/TypeParameter-with-constraint machinery); the
    // ParameterizedType case is the shape the metadata reader produces for
    // every real Nullable<T>.
    static const TypeSystem::IType* GetUnderlyingTypeOfNullable(const TypeSystem::IType* type);

    // True when `type` is a KnownType of the given code. A faithful subset of
    // the C# `type.IsKnownType(code)` extension: this port's IType carries no
    // IsKnownType; a KnownType compares its Code directly, a ParameterizedType
    // (e.g. `Nullable<int>`) is not itself a known type, matching the C# which
    // reads the type's KnownTypeCode (None for a non-known type).
    static bool IsKnownType(const TypeSystem::IType* type, TypeSystem::KnownTypeCode code);

    // Port of NullableLiftingTransform.MatchHasValueCall(inst, out ILVariable v):
    // the ldloca-v overload. `call get_HasValue(ldloca v)` on System.Nullable<T>
    // (1 argument, the argument a LdLoca) -> v (the LdLoca's variable). The
    // 1-arg `(inst, out ILInstruction arg)` overload above recognises the call
    // and returns its argument; this overload additionally requires the
    // argument to be a `ldloca v` and reports the variable.
    static bool MatchHasValueCall(ILInstruction* inst, ILVariablePtr& v);

    // Port of NullableLiftingTransform.MatchGetValueOrDefault(inst, out ILVariable v):
    // the ldloca-v overload. `call GetValueOrDefault(ldloca v)` on
    // System.Nullable<T> (the 1-argument form, the argument a LdLoca) -> v.
    // The 1-arg `(inst, out ILInstruction arg)` overload recognises the call;
    // this overload additionally requires the argument to be a `ldloca v`.
    static bool MatchGetValueOrDefault(ILInstruction* inst, ILVariablePtr& v);

    // Port of NullableLiftingTransform.MatchHasValueCall(inst, ILVariable v):
    // the match-against-v overload. `call get_HasValue(ldloca v)` on
    // System.Nullable<T> (1 argument, the argument a LdLoca) whose variable is
    // the given `v`. The report-variable overload above recognises the call and
    // reports the variable; this overload additionally checks the variable
    // matches (the C# `MatchHasValueCall(inst, out v2) && v == v2`). An
    // ILVariablePtr lvalue binds to the `(inst, ILVariablePtr&)` report overload
    // (identity), not to `const ILVariable*` (no shared_ptr->raw conversion), so
    // the two overloads are disjoint -- call this overload with `v.get()`.
    static bool MatchHasValueCall(ILInstruction* inst, const ILVariable* v);

    // Port of NullableLiftingTransform.MatchGetValueOrDefault(inst, ILVariable v):
    // the match-against-v overload. `call GetValueOrDefault(ldloca v)` on
    // System.Nullable<T> (1 argument, the argument a LdLoca) whose variable is
    // the given `v`. The report-variable overload above recognises the call and
    // reports the variable; this overload additionally checks the variable
    // matches (the C# `MatchGetValueOrDefault(inst, out v2) && v == v2`).
    // Disjoint from the `(inst, ILVariablePtr&)` report overload by the same
    // shared_ptr/raw-pointer split as MatchHasValueCall -- call with `v.get()`.
    // Used by the LiftNormal conv.nop.lifted case to recognise the true arm is a
    // GetValueOrDefault call on the single nullable var.
    static bool MatchGetValueOrDefault(ILInstruction* inst, const ILVariable* v);

    // Port of NullableLiftingTransform.MatchNegatedHasValueCall(inst, ILVariable v):
    // `logic.not(call get_HasValue(ldloca v))` -> recognises v. The logic.not is
    // this port's `comp(Equality, X, ldc.i4(0))` shape (the reader's brfalse, per
    // the SwitchAnalysis/ConditionDetection convention); the inner
    // `call get_HasValue(ldloca v)` must operate on the given variable.
    static bool MatchNegatedHasValueCall(ILInstruction* inst, const ILVariable* v);

    // Port of NullableLiftingTransform.MatchNullableCtor(inst, out underlyingType,
    // out arg): `newobj Nullable<T>(arg)` -> (T, arg). A newobj (a Call with the
    // IsNewObj flag -- newobj is always a constructor, so IsNewObj is the
    // faithful equivalent of the C# `newobj.Method.IsConstructor`) whose
    // declaring type resolves to KnownTypeCode::NullableOfT with exactly one
    // argument; the underlying type is GetUnderlyingTypeOfNullable(DeclaringType).
    static bool MatchNullableCtor(ILInstruction* inst,
                                   const TypeSystem::IType*& underlyingType,
                                   ILInstruction*& arg);

    // Port of NullableLiftingTransform.MatchNull(inst, out underlyingType):
    // `default(Nullable<T>)` -> T. A DefaultValue whose Type is a Nullable<T>
    // (GetUnderlyingTypeOfNullable returns non-null); the underlying type is
    // the type argument. The C# also checks NullableType.IsNullable(type) --
    // GetUnderlyingTypeOfNullable returning non-null is the equivalent (only a
    // Nullable<T> instantiation unwraps).
    static bool MatchNull(ILInstruction* inst, const TypeSystem::IType*& underlyingType);

    // Port of the ILInstruction.MatchDefaultValue(out var type) extension: a
    // DefaultValue node reports its Type. The general form MatchNull narrows
    // (it requires the Type be a Nullable<T>); MatchDefaultValue matches any
    // DefaultValue. Used by IsGenericNewPattern (the `default(T) == null ?
    // Activator.CreateInstance<T>() : default(T)` fold checks both the comp's
    // left operand and the false arm are `default(T)` of the same type).
    static bool MatchDefaultValue(ILInstruction* inst, TypeSystem::ITypePtr& type);

    // Port of NullableLiftingTransform.IsGenericNewPattern(compLeft,
    // compRight, trueInst, falseInst): the `(default(T) == null) ?
    // Activator.CreateInstance<T>() : default(T)` => `Activator.CreateInstance<T>()`
    // fold. The condition compares `default(T)` (a DefaultValue whose Type is a
    // type parameter) against ldnull; the false arm is another `default(T)` of
    // the SAME type; the true arm is a call to `System.Activator.CreateInstance`
    // with exactly one generic type argument. Returns true when the shape
    // matches (the caller returns the true arm as the lifted value, dropping the
    // null check + default fallback).
    static bool IsGenericNewPattern(ILInstruction* compLeft, ILInstruction* compRight,
                                    ILInstruction* trueInst, ILInstruction* falseInst);

    // Port of NullableLiftingTransform.DoLift's result: a (lifted instruction,
    // relevance bitset) pair. A null Lifted means lifting failed (the bitset is
    // then also null); a non-null Lifted carries the relevance bitset (which
    // nullableVars contributed). The bitset is null when the lifted instruction
    // embeds a non-nullable pure operand (NewNullable) -- that operand
    // contributes no nullable var, matching the C# `(left, leftBits ?? rightBits)`
    // null-propagation.
    struct DoLiftResult {
        std::unique_ptr<ILInstruction> Lifted;
        std::unique_ptr<BitSet> Bits;
    };

    // Port of NullableLiftingTransform.DoLiftBinary's result: a (left, right,
    // relevance bitset) triple. Null Left/Right means lifting failed (the bitset
    // is then also null). The caller (DoLift's BinaryNumericInstruction case, or
    // the deferred LiftCSharpComparison) builds the lifted binary from Left +
    // Right; the bitset is the union of both sides' relevance.
    struct DoLiftBinaryResult {
        std::unique_ptr<ILInstruction> Left;
        std::unique_ptr<ILInstruction> Right;
        std::unique_ptr<BitSet> Bits;
    };

    // Port of NullableLiftingTransform.DoLift(inst): a recursive function that
    // lifts `inst` into a lifted Nullable<T> instruction without modifying the
    // input (it builds new nodes from the GVO/Conv/BinaryNumeric/Comp/BitNot
    // shape). `nullableVars` is the collected set of nullable locals the lift
    // operates over (the C# instance field `this.nullableVars`, set by
    // AnalyzeCondition before LiftNormal calls DoLift). On success returns the
    // lifted instruction + a bitset marking which nullableVars were relevant
    // (bitSet[i] == nullableVars[i] contributed); on failure returns (null,
    // null). The relevance gate `bits.All(0, nullableVars.Count)` (every nullable
    // var contributed) is consulted by the caller (LiftNormal). The 5
    // self-contained cases are ported: GetValueOrDefault -> LdLoc; Conv -> lifted
    // Conv (gated on the MayThrow/CheckForOverflow guard); BitNot -> lifted
    // BitNot; BinaryNumericInstruction -> lifted binary via DoLiftBinary; the
    // bool? operator! Comp (equality, GVO left, Boolean underlying, ldc.i4 0
    // right) -> a ThreeValuedLogic-lifted Comp. The 6th case (a Call to a
    // user-defined operator, needs Call.Method.IsOperator +
    // CSharpOperators.LiftUserDefinedOperator) is deferred and returns failure.
    static DoLiftResult DoLift(ILInstruction* inst,
                               const std::vector<ILVariablePtr>& nullableVars);

    // Port of NullableLiftingTransform.DoLiftBinary(lhs, rhs,
    // leftExpectedType, rightExpectedType): lifts both operands; when one side
    // lifts and the other is a pure non-nullable expression, the pure side is
    // embedded (NewNullable) so the lifted binary has two nullable operands.
    // Returns (left, right, bits) on success or (null, null, null) on failure;
    // the bitset is the union of both sides' relevance (the embedded side's
    // bits are null, so the union is just the lifted side's bits).
    static DoLiftBinaryResult DoLiftBinary(ILInstruction* lhs, ILInstruction* rhs,
                                     const TypeSystem::IType* leftExpectedType,
                                     const TypeSystem::IType* rightExpectedType,
                                     const std::vector<ILVariablePtr>& nullableVars);

    // Port of NullableLiftingTransform.NewNullable(inst, underlyingType):
    // wraps a non-nullable pure expression in `new Nullable<T>(inst)` so it can
    // be an operand of a lifted binary. A null underlyingType (the
    // SpecialType.UnknownType sentinel -- this port has no SpecialType) returns
    // `inst` unchanged, matching the C#; a real type builds a newobj
    // `Nullable<T>(inst)` (a Call with IsNewObj and a Nullable<T> declaring
    // type), the shape MatchNullableCtor recognises. Used by DoLiftBinary's
    // pure-non-nullable embedding.
    static std::unique_ptr<ILInstruction> NewNullable(std::unique_ptr<ILInstruction> inst,
                                                       const TypeSystem::IType* underlyingType);

    // Port of NullableLiftingTransform.LiftCSharpEqualityComparison(valueComp,
    // newComparisonKind, hasValueTest): the C#-style lifted (in)equality
    // comparison. `hasValueTest` is the trueInst (after the equality swap -- the
    // C# `Swap(ref trueInst, ref falseInst)` for Inequality is done by the
    // caller). The hasValueComp case (comparing two nullables: the HasValue
    // comparison must be the same operator as the Value comparison, DoLift both
    // sides with a single-var list, gate on leftBits[0] && rightBits[0] + IsPure)
    // and the fall-back case (comparing nullable with non-nullable: a single
    // HasValue call -> LiftCSharpComparison) are ported. The
    // LiftCSharpUserEqualityComparison fall-back (?? LiftCSharpUserEquality-
    // Comparison, needs Call.Method.IsOperator + CSharpOperators) is deferred.
    // Returns the lifted instruction or null.
    static std::unique_ptr<ILInstruction> LiftCSharpEqualityComparison(
        const CompOrDecimal& valueComp, ComparisonKind newComparisonKind,
        ILInstruction* hasValueTest);

    // Port of NullableLiftingTransform.LiftCSharpComparison(comp,
    // newComparisonKind): the C#-style lifted relational comparison. The
    // !comp.IsLifted case (DoLiftBinary with both expected types UnknownType +
    // MakeLifted, gated on IsPure + bits.All) and the comp.IsLifted special case
    // (legacy csc `num.GetValueOrDefault() == const && num.HasValue`, where the
    // comp was already lifted by Run(Comp); clone the operands and MakeLifted)
    // are ported. `nullableVars` is the per-call list (the C# instance field,
    // set by AnalyzeCondition or by LiftCSharpEqualityComparison before calling).
    // Returns the lifted instruction or null.
    static std::unique_ptr<ILInstruction> LiftCSharpComparison(
        const CompOrDecimal& comp, ComparisonKind newComparisonKind,
        const std::vector<ILVariablePtr>& nullableVars);
};

} // namespace ILSpy::Decompiler::IL
