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

#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"

#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The KnownTypeCode of `type`'s generic definition: a KnownType yields its
// Code directly; a ParameterizedType (e.g. Nullable<int>) unwraps to its
// GenericType (Nullable`1) and recurses. KnownTypeCode::None when the type is
// not a known type (or is null). Mirrors the C#
// `call.Method.DeclaringTypeDefinition?.KnownTypeCode` access: the C# already
// holds the generic definition, so there is no unwrap to do there; this port's
// Call::DeclaringType may be the parameterized instantiation, hence the unwrap.
TypeSystem::KnownTypeCode KnownTypeCodeOf(const TypeSystem::IType* type) {
    if (!type) return TypeSystem::KnownTypeCode::None;
    if (auto* kt = dynamic_cast<const TypeSystem::KnownType*>(type))
        return kt->Code();
    if (auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(type)) {
        const auto& g = pt->GenericType();
        return KnownTypeCodeOf(g.get());
    }
    return TypeSystem::KnownTypeCode::None;
}

// The short method name (the part after "::") of a Call's resolved display name,
// e.g. "System.Nullable`1::get_HasValue" -> "get_HasValue". The IL reader builds
// MethodName as "Namespace.Type::Member"; the method-name check is on this short
// part. Returns the whole string when no "::" is present.
std::string_view ShortMethodName(std::string_view fullName) {
    auto pos = fullName.rfind("::");
    if (pos == std::string_view::npos) return fullName;
    return fullName.substr(pos + 2);
}

// Port of ILInstruction.MatchLogicNot(out arg): logic.not(X) is this port's
// `comp(Equality, X, ldc.i4(0))` shape (the reader's brfalse, per the
// SwitchAnalysis/ConditionDetection convention -- see MatchInstruction's
// MatchLogicNot). Sign-independent, so the Unsigned flag is irrelevant; the
// inner expression is comp->Left. Returns true and sets `arg` to the negated
// expression.
bool MatchLogicNot(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Comp) return false;
    auto* comp = static_cast<Comp*>(inst);
    if (comp->Kind != ComparisonKind::Equality || comp->Unsigned) return false;
    if (!comp->Right || comp->Right->Op != OpCode::LdcI4) return false;
    if (static_cast<LdcI4*>(comp->Right.get())->Value != 0) return false;
    arg = comp->Left.get();
    return true;
}

// Clone a pure expression (no side effects) so a lifted binary can embed a
// non-nullable pure operand independently of the original (DoLift "Creates a
// new lifted instruction without modifying the input instruction"). The C#
// uses a general virtual Clone(); this port has none, so the simple pure loads
// (the common operands -- constants and ldloc) are cloned and an uncloneable
// pure expression returns null, which makes DoLiftBinary bail (return failure)
// and the if stays as-is -- conservative-correct (the C# would embed it, this
// port leaves the block). Mirrors the ClonePureLoad fast path in
// ControlFlowSimplification (the same instruction kinds).
std::unique_ptr<ILInstruction> ClonePureExpression(const ILInstruction* v) {
    if (!v) return nullptr;
    switch (v->Op) {
        case OpCode::LdLoc:
            return std::make_unique<LdLoc>(static_cast<const LdLoc*>(v)->Variable);
        case OpCode::LdcI4:
            return std::make_unique<LdcI4>(static_cast<const LdcI4*>(v)->Value);
        case OpCode::LdcI8:
            return std::make_unique<LdcI8>(static_cast<const LdcI8*>(v)->Value);
        case OpCode::LdcF4:
            return std::make_unique<LdcF4>(static_cast<const LdcF4*>(v)->Value);
        case OpCode::LdcF8:
            return std::make_unique<LdcF8>(static_cast<const LdcF8*>(v)->Value);
        case OpCode::LdNull:
            return std::make_unique<LdNull>();
        case OpCode::LdStr:
            return std::make_unique<LdStr>(static_cast<const LdStr*>(v)->Value);
        default:
            return nullptr;
    }
}

} // namespace

bool NullableLiftingTransform::MatchHasValueCall(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(inst);
    if (call->Arguments.size() != 1) return false;
    if (ShortMethodName(call->MethodName) != "get_HasValue") return false;
    if (KnownTypeCodeOf(call->DeclaringType.get()) != TypeSystem::KnownTypeCode::NullableOfT)
        return false;
    arg = call->Arguments[0].get();
    return true;
}

bool NullableLiftingTransform::MatchGetValueOrDefault(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(inst);
    // The 1-argument form (the underlying-value accessor). The 2-argument form
    // (with a fallback default) is a separate overload below.
    if (call->Arguments.size() != 1) return false;
    if (ShortMethodName(call->MethodName) != "GetValueOrDefault") return false;
    if (KnownTypeCodeOf(call->DeclaringType.get()) != TypeSystem::KnownTypeCode::NullableOfT)
        return false;
    arg = call->Arguments[0].get();
    return true;
}

bool NullableLiftingTransform::MatchGetValueOrDefault(ILInstruction* inst,
                                                       ILInstruction*& nullableValue,
                                                       ILInstruction*& fallback) {
    nullableValue = nullptr;
    fallback = nullptr;
    if (!inst || inst->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(inst);
    // The 2-argument form (the value-or-fallback accessor -- the `a ?? b`
    // lowering). Consumed by ExpressionTransforms.VisitCall; the 1-arg form
    // above is the switch-on-nullable accessor.
    if (call->Arguments.size() != 2) return false;
    if (ShortMethodName(call->MethodName) != "GetValueOrDefault") return false;
    if (KnownTypeCodeOf(call->DeclaringType.get()) != TypeSystem::KnownTypeCode::NullableOfT)
        return false;
    nullableValue = call->Arguments[0].get();
    fallback = call->Arguments[1].get();
    return true;
}

bool NullableLiftingTransform::MatchCompOrDecimal(ILInstruction* inst, CompOrDecimal& result) {
    // The Comp branch: a non-lifted IL Comp reports its Kind/Left/Right/IsLifted.
    // The Decimal branch: a Call to one of the 6 comparison operators on
    // System.Decimal (op_Equality/op_Inequality/op_LessThan/op_LessThanOrEqual/
    // op_GreaterThan/op_GreaterThanOrEqual) -- Decimal has no IL Comp
    // instruction, so a Decimal comparison lowers to an op_* call. The C# gate
    // is `call.Method.IsOperator && call.Arguments.Count == 2 && !call.IsLifted`;
    // this port's Call has no IsLifted field, but the C# `Call.IsLifted` is for
    // lifted user-defined operators produced by CSharpOperators (which this port
    // does not produce), so every Call is non-lifted (faithful). The C# then
    // switches on `call.Method.Name` (the 6 comparison operators) and finally
    // requires `call.Method.DeclaringType.IsKnownType(KnownTypeCode.Decimal)` --
    // a user-defined op_Equality on another type does NOT match (DeclaringType
    // != Decimal), so MatchCompOrDecimal returns false for it. The Decimal lift
    // (LiftCSharpUserEqualityComparison/LiftCSharpUserComparison, which build a
    // lifted user-defined operator via CSharpOperators.LiftUserDefinedOperator) is
    // deferred -- needs the C# resolver; the recognition here is the foundation
    // those consumers will consult. `result.Left`/`Right` are the call's two
    // arguments (the C# `call.Arguments[0/1]`); `result.IsLifted` is false.
    result = CompOrDecimal{};
    if (!inst) return false;
    result.Instruction = inst;
    if (inst->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(inst);
        result.Kind = comp->Kind;
        result.Left = comp->Left.get();
        result.Right = comp->Right.get();
        result.IsLifted = comp->IsLifted();
        return true;
    }
    if (inst->Op == OpCode::Call) {
        auto* call = static_cast<Call*>(inst);
        if (!call->IsOperator) return false;
        if (call->Arguments.size() != 2) return false;
        auto name = ShortMethodName(call->MethodName);
        ComparisonKind kind;
        if (name == "op_Equality") kind = ComparisonKind::Equality;
        else if (name == "op_Inequality") kind = ComparisonKind::Inequality;
        else if (name == "op_LessThan") kind = ComparisonKind::LessThan;
        else if (name == "op_LessThanOrEqual") kind = ComparisonKind::LessThanOrEqual;
        else if (name == "op_GreaterThan") kind = ComparisonKind::GreaterThan;
        else if (name == "op_GreaterThanOrEqual") kind = ComparisonKind::GreaterThanOrEqual;
        else return false;
        // The C# final gate: the declaring type is System.Decimal. This narrows
        // the match to Decimal's comparison operators only (a user-defined
        // op_Equality on another type has IsOperator but not a Decimal
        // declaring type, so it returns false here).
        if (!IsKnownType(call->DeclaringType.get(), TypeSystem::KnownTypeCode::Decimal))
            return false;
        result.Kind = kind;
        result.Left = call->Arguments[0].get();
        result.Right = call->Arguments[1].get();
        result.IsLifted = false;
        return true;
    }
    return false;
}

const TypeSystem::IType* NullableLiftingTransform::GetUnderlyingTypeOfNullable(const TypeSystem::IType* type) {
    // Nullable<T> is a ParameterizedType whose generic definition is
    // KnownType(NullableOfT) with exactly one type argument T. A bare
    // Nullable`1 (the generic definition) has no type argument; the full C#
    // NullableType.GetUnderlyingType unwraps further wrappers -- deferred (the
    // ParameterizedType case is the shape the metadata reader produces for
    // every real Nullable<T> in this port).
    if (!type) return nullptr;
    if (auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(type)) {
        const auto& gen = pt->GenericType();
        if (gen) {
            if (auto* kt = dynamic_cast<const TypeSystem::KnownType*>(gen.get())) {
                if (kt->Code() == TypeSystem::KnownTypeCode::NullableOfT) {
                    const auto& args = pt->TypeArguments();
                    if (args.size() == 1) return args[0].get();
                }
            }
        }
    }
    return nullptr;
}

bool NullableLiftingTransform::IsKnownType(const TypeSystem::IType* type, TypeSystem::KnownTypeCode code) {
    if (!type) return false;
    if (auto* kt = dynamic_cast<const TypeSystem::KnownType*>(type))
        return kt->Code() == code;
    return false;
}

bool NullableLiftingTransform::MatchHasValueCall(ILInstruction* inst, ILVariablePtr& v) {
    // `call get_HasValue(ldloca v)`: the 1-arg form (recognised by the
    // `(inst, out ILInstruction arg)` overload) whose argument is a LdLoca.
    v.reset();
    ILInstruction* arg = nullptr;
    if (!MatchHasValueCall(inst, arg)) return false;
    if (!arg || arg->Op != OpCode::LdLoca) return false;
    v = static_cast<LdLoca*>(arg)->Variable;
    return true;
}

bool NullableLiftingTransform::MatchHasValueCall(ILInstruction* inst, const ILVariable* v) {
    // Match-against-v: the call's variable (reported by the ldloca-v overload)
    // must be the given `v` (the C# `MatchHasValueCall(inst, out v2) && v == v2`).
    ILVariablePtr v2;
    if (!MatchHasValueCall(inst, v2)) return false;
    return v2.get() == v;
}

bool NullableLiftingTransform::MatchGetValueOrDefault(ILInstruction* inst, ILVariablePtr& v) {
    // `call GetValueOrDefault(ldloca v)`: the 1-arg form whose argument is a
    // LdLoca. (The 2-arg value-or-fallback form is a separate overload.)
    v.reset();
    ILInstruction* arg = nullptr;
    if (!MatchGetValueOrDefault(inst, arg)) return false;
    if (!arg || arg->Op != OpCode::LdLoca) return false;
    v = static_cast<LdLoca*>(arg)->Variable;
    return true;
}

bool NullableLiftingTransform::MatchGetValueOrDefault(ILInstruction* inst, const ILVariable* v) {
    // Match-against-v: the call's variable (reported by the ldloca-v overload)
    // must be the given `v` (the C# `MatchGetValueOrDefault(inst, out v2) &&
    // v == v2`). Disjoint from the report overload by the shared_ptr/raw-pointer
    // split (the D94 MatchHasValueCall precedent) -- call with `v.get()`.
    ILVariablePtr v2;
    if (!MatchGetValueOrDefault(inst, v2)) return false;
    return v2.get() == v;
}

bool NullableLiftingTransform::MatchNegatedHasValueCall(ILInstruction* inst, const ILVariable* v) {
    // logic.not(call get_HasValue(ldloca v)): the logic.not is this port's
    // `comp(Equality, X, ldc.i4(0))` shape; the inner call must operate on `v`.
    if (!inst) return false;
    ILInstruction* arg = nullptr;
    if (!MatchLogicNot(inst, arg)) return false;
    ILVariablePtr hv;
    if (!MatchHasValueCall(arg, hv)) return false;
    return hv.get() == v;
}

bool NullableLiftingTransform::MatchNullableCtor(ILInstruction* inst,
                                                   const TypeSystem::IType*& underlyingType,
                                                   ILInstruction*& arg) {
    // `newobj Nullable<T>(arg)`: a newobj (Call with IsNewObj -- newobj is
    // always a constructor, so IsNewObj is the faithful equivalent of the C#
    // newobj.Method.IsConstructor) whose declaring type resolves to
    // NullableOfT with exactly one argument.
    underlyingType = nullptr;
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Call) return false;
    auto* call = static_cast<Call*>(inst);
    if (!call->IsNewObj) return false;
    if (call->Arguments.size() != 1) return false;
    if (KnownTypeCodeOf(call->DeclaringType.get()) != TypeSystem::KnownTypeCode::NullableOfT)
        return false;
    arg = call->Arguments[0].get();
    underlyingType = GetUnderlyingTypeOfNullable(call->DeclaringType.get());
    return true;
}

bool NullableLiftingTransform::MatchNull(ILInstruction* inst, const TypeSystem::IType*& underlyingType) {
    // `default(Nullable<T>)`: a DefaultValue whose Type is a Nullable<T> --
    // GetUnderlyingTypeOfNullable returns the type argument T (non-null only
    // for a Nullable<T> instantiation, the equivalent of the C#
    // NullableType.IsNullable check).
    underlyingType = nullptr;
    if (!inst || inst->Op != OpCode::DefaultValue) return false;
    auto* dv = static_cast<DefaultValue*>(inst);
    const TypeSystem::IType* ut = GetUnderlyingTypeOfNullable(dv->Type.get());
    if (!ut) return false;
    underlyingType = ut;
    return true;
}

bool NullableLiftingTransform::MatchDefaultValue(ILInstruction* inst, TypeSystem::ITypePtr& type) {
    // Port of ILInstruction.MatchDefaultValue(out var type): a DefaultValue
    // reports its Type. The general form of MatchNull (which narrows to a
    // Nullable<T> Type).
    type = nullptr;
    if (!inst || inst->Op != OpCode::DefaultValue) return false;
    type = static_cast<DefaultValue*>(inst)->Type;
    return true;
}

bool NullableLiftingTransform::IsGenericNewPattern(ILInstruction* compLeft,
                                                    ILInstruction* compRight,
                                                    ILInstruction* trueInst,
                                                    ILInstruction* falseInst) {
    // Port of NullableLiftingTransform.IsGenericNewPattern: the
    //   (default(T) == null) ? Activator.CreateInstance<T>() : default(T)
    //   => Activator.CreateInstance<T>()
    // fold. The false arm is `default(T)`, the true arm is a call to
    // `System.Activator.CreateInstance` with exactly one generic type argument,
    // the comp's left is another `default(T)` of the SAME type, and the comp's
    // right is ldnull. The C# checks `c.Method.FullName ==
    // "System.Activator.CreateInstance"` and `c.Method.TypeArguments.Count == 1`;
    // this port's Call::MethodName is the resolved "Namespace.Type::Method" form
    // (the MethodSpec unwrap resolves it), and Call::TypeArgumentsCount carries
    // the MethodSpec instantiation count.
    TypeSystem::ITypePtr type, type2;
    if (!MatchDefaultValue(falseInst, type)) return false;
    if (!trueInst || trueInst->Op != OpCode::Call) return false;
    auto* c = static_cast<Call*>(trueInst);
    if (c->MethodName != "System.Activator::CreateInstance") return false;
    if (c->TypeArgumentsCount != 1) return false;
    if (!type || type->Kind() != TypeSystem::TypeKind::TypeParameter) return false;
    if (!MatchDefaultValue(compLeft, type2)) return false;
    if (!type2 || !type->Equals(*type2)) return false;
    return compRight && compRight->Op == OpCode::LdNull;
}

NullableLiftingTransform::DoLiftResult NullableLiftingTransform::DoLift(
    ILInstruction* inst, const std::vector<ILVariablePtr>& nullableVars) {
    // Port of NullableLiftingTransform.DoLift(inst). The 5 self-contained cases
    // are ported; the 6th (a Call to a user-defined operator) is deferred and
    // falls through to the failure return. Each case builds a NEW lifted
    // instruction from the shape inside `inst` without modifying `inst` (the
    // C# "Creates a new lifted instruction without modifying the input").
    DoLiftResult failure{};  // (null Lifted, null Bits)
    if (!inst) return failure;

    // Case 1: `call GetValueOrDefault(ldloca v)` -> `ldloc v`. The relevance
    // bitset marks every nullableVars[i] equal to v.
    ILVariablePtr v;
    if (MatchGetValueOrDefault(inst, v)) {
        auto bits = std::make_unique<BitSet>(static_cast<int>(nullableVars.size()));
        bool found = false;
        for (std::size_t i = 0; i < nullableVars.size(); ++i) {
            if (nullableVars[i].get() == v.get()) {
                bits->Set(static_cast<int>(i));
                found = true;
            }
        }
        if (!found) return failure;  // GVO on a var not in nullableVars
        DoLiftResult r;
        r.Lifted = std::make_unique<LdLoc>(v);
        r.Bits = std::move(bits);
        return r;
    }

    // Case 2: Conv -> lifted Conv (gated on the MayThrow/CheckForOverflow guard:
    // a checked conv may throw, so it lifts only when every nullableVar is its
    // argument, else the lift would drop the throw when a var is null).
    if (inst->Op == OpCode::Conv) {
        auto* conv = static_cast<Conv*>(inst);
        auto argR = DoLift(conv->Argument.get(), nullableVars);
        if (argR.Lifted) {
            if (conv->CheckForOverflow && argR.Bits &&
                !argR.Bits->All(0, static_cast<int>(nullableVars.size()))) {
                return failure;
            }
            DoLiftResult r;
            r.Bits = std::move(argR.Bits);
            r.Lifted = std::make_unique<Conv>(
                std::move(argR.Lifted),
                conv->InputType, conv->InputSign, conv->TargetType,
                conv->CheckForOverflow, /*isLifted=*/true);
            return r;
        }
        return failure;
    }

    // Case 3: BitNot -> lifted BitNot.
    if (inst->Op == OpCode::BitNot) {
        auto* bitnot = static_cast<BitNot*>(inst);
        auto argR = DoLift(bitnot->Argument.get(), nullableVars);
        if (argR.Lifted) {
            DoLiftResult r;
            r.Bits = std::move(argR.Bits);
            r.Lifted = std::make_unique<BitNot>(
                std::move(argR.Lifted), /*isLifted=*/true, bitnot->ResultType());
            return r;
        }
        return failure;
    }

    // Case 4: BinaryNumericInstruction -> lifted binary via DoLiftBinary (the
    // MayThrow guard: a checked or div/rem binary may throw; lifts only when
    // every nullableVar is its argument). This port's BNI DirectFlags is None
    // (the MayThrow for CheckForOverflow/Div/Rem is computed inline here,
    // matching the C# HasDirectFlag(MayThrow)). DoLiftBinary returns the lifted
    // left + right; the lifted BNI is built from them.
    if (inst->Op == OpCode::BinaryNumericInstruction) {
        auto* bni = static_cast<BinaryNumericInstruction*>(inst);
        auto binR = DoLiftBinary(bni->Left.get(), bni->Right.get(),
                                  nullptr, nullptr, nullableVars);
        if (binR.Left && binR.Right) {
            bool mayThrow = bni->CheckForOverflow ||
                            bni->Operator == BinaryNumericOperator::Div ||
                            bni->Operator == BinaryNumericOperator::Rem;
            if (mayThrow && binR.Bits &&
                !binR.Bits->All(0, static_cast<int>(nullableVars.size()))) {
                return failure;
            }
            DoLiftResult r;
            r.Bits = std::move(binR.Bits);
            r.Lifted = std::make_unique<BinaryNumericInstruction>(
                std::move(binR.Left), std::move(binR.Right), bni->Operator,
                bni->ResultStackType, bni->CheckForOverflow, bni->Signed,
                /*isLifted=*/true);
            return r;
        }
        return failure;
    }

    // Case 5: the bool? operator! Comp -- `comp(eq, call GetValueOrDefault(
    // ldloca v), ldc.i4 0)` on a Nullable<bool> -> a ThreeValuedLogic-lifted
    // Comp. C# doesn't support ThreeValuedLogic except for operator! on bool?.
    if (inst->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(inst);
        if (!comp->IsLifted() && comp->Kind == ComparisonKind::Equality) {
            ILVariablePtr cv;
            if (MatchGetValueOrDefault(comp->Left.get(), cv) &&
                std::any_of(nullableVars.begin(), nullableVars.end(),
                            [&](const ILVariablePtr& nv) { return nv.get() == cv.get(); }) &&
                IsKnownType(GetUnderlyingTypeOfNullable(cv->Type.get()),
                            TypeSystem::KnownTypeCode::Boolean) &&
                comp->Right && comp->Right->Op == OpCode::LdcI4 &&
                static_cast<LdcI4*>(comp->Right.get())->Value == 0) {
                auto argR = DoLift(comp->Left.get(), nullableVars);
                // The inner GVO is in nullableVars (checked above), so argR
                // succeeds; clone the ldc.i4 0 right operand.
                auto rightClone = ClonePureExpression(comp->Right.get());
                if (argR.Lifted && rightClone) {
                    DoLiftResult r;
                    r.Bits = std::move(argR.Bits);
                    r.Lifted = std::make_unique<Comp>(
                        std::move(argR.Lifted), std::move(rightClone),
                        comp->Kind, ComparisonLiftingKind::ThreeValuedLogic,
                        comp->InputType, comp->Unsigned);
                    return r;
                }
                return failure;
            }
        }
        return failure;
    }

    // Case 6 (Call to a user-defined operator) is deferred: needs
    // Call.Method.IsOperator + CSharpOperators.LiftUserDefinedOperator. Returns
    // failure (the if stays as-is), matching the C# fall-through.
    return failure;
}

NullableLiftingTransform::DoLiftBinaryResult NullableLiftingTransform::DoLiftBinary(
    ILInstruction* lhs, ILInstruction* rhs,
    const TypeSystem::IType* leftExpectedType, const TypeSystem::IType* rightExpectedType,
    const std::vector<ILVariablePtr>& nullableVars) {
    // Port of NullableLiftingTransform.DoLiftBinary. Lifts both sides; when one
    // side lifts and the other is a pure non-nullable expression, the pure side
    // is embedded (NewNullable) so the lifted binary has two nullable operands.
    DoLiftBinaryResult failure{};  // (null, null, null)
    auto leftR = DoLift(lhs, nullableVars);
    auto rightR = DoLift(rhs, nullableVars);
    if (leftR.Lifted && !rightR.Lifted && IsPure(rhs->Flags())) {
        // Embed the non-nullable pure rhs in a lifted Nullable<T>. Clone it
        // (DoLift builds a new instruction without modifying the input); an
        // uncloneable pure rhs makes this bail (conservative -- the C# would
        // embed it, this port leaves the binary un-lifted).
        auto clone = ClonePureExpression(rhs);
        if (!clone) return failure;
        rightR.Lifted = NewNullable(std::move(clone), rightExpectedType);
        // rightR.Bits stays null (the embedded operand contributes no nullable
        // var), matching the C# `right = NewNullable(...)` (no bits assigned).
    }
    if (!leftR.Lifted && rightR.Lifted && IsPure(lhs->Flags())) {
        auto clone = ClonePureExpression(lhs);
        if (!clone) return failure;
        leftR.Lifted = NewNullable(std::move(clone), leftExpectedType);
    }
    if (leftR.Lifted && rightR.Lifted) {
        DoLiftBinaryResult r;
        // `bits = leftBits ?? rightBits; if (rightBits != null) bits.UnionWith(...)`.
        // The embedded side's bits are null, so the union degenerates to the
        // lifted side's bits; when both lifted, both contribute.
        std::unique_ptr<BitSet> bits = std::move(leftR.Bits);
        if (!bits) bits = std::move(rightR.Bits);
        else if (rightR.Bits) bits->UnionWith(*rightR.Bits);
        r.Left = std::move(leftR.Lifted);
        r.Right = std::move(rightR.Lifted);
        r.Bits = std::move(bits);
        return r;
    }
    return failure;
}

std::unique_ptr<ILInstruction> NullableLiftingTransform::NewNullable(
    std::unique_ptr<ILInstruction> inst, const TypeSystem::IType* underlyingType) {
    // Port of NullableLiftingTransform.NewNullable(inst, underlyingType). A
    // null underlyingType (the SpecialType.UnknownType sentinel -- this port
    // has no SpecialType) returns `inst` unchanged, matching the C#
    // `if (underlyingType == SpecialType.UnknownType) return inst`. A real type
    // would build `new Nullable<T>(inst)` (a newobj Call with a Nullable<T>
    // declaring type); that path is deferred -- it needs constructing a
    // Nullable<T> IType from the non-owning `underlyingType` (this port's IType
    // has no virtual Clone, and the only caller in the wired DoLift path passes
    // UnknownType for both operands, so the real-type path does not fire yet).
    // The deferred LiftCSharpComparison path will need a faithful NewNullable.
    (void)underlyingType;
    return inst;
}

std::unique_ptr<ILInstruction> CompOrDecimal::MakeLifted(
    ComparisonKind newComparisonKind,
    std::unique_ptr<ILInstruction> left,
    std::unique_ptr<ILInstruction> right) const {
    // Port of CompOrDecimal.MakeLifted (the Comp branch). Builds a C#-lifted Comp
    // (`Comp(newComparisonKind, ComparisonLiftingKind.CSharp, comp.InputType,
    // comp.Sign, left, right)`, the D91 model) carrying the original comp's
    // InputType/Unsigned so the lifted comparison's operands (whose ResultType is
    // O, a boxed Nullable<T>) compare the underlying value. The Decimal/Call
    // branch (a Call to a lifted user-defined operator, needs
    // CSharpOperators.LiftUserDefinedOperator) is deferred -- MatchCompOrDecimal
    // only matches the Comp branch, so a Call Instruction never reaches here and
    // the function returns null for a non-Comp Instruction.
    if (Instruction && Instruction->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(Instruction);
        return std::make_unique<Comp>(
            std::move(left), std::move(right),
            newComparisonKind, ComparisonLiftingKind::CSharp,
            comp->InputType, comp->Unsigned);
    }
    return nullptr;
}

std::unique_ptr<ILInstruction> NullableLiftingTransform::LiftCSharpEqualityComparison(
    const CompOrDecimal& valueComp, ComparisonKind newComparisonKind,
    ILInstruction* hasValueTest) {
    // Port of NullableLiftingTransform.LiftCSharpEqualityComparison (Comp
    // branch). `hasValueTest` is the trueInst after the equality swap (the C#
    // `Swap(ref trueInst, ref falseInst)` for Inequality is done by the caller).
    // The hasValueComp case (comparing two nullables) and the fall-back case
    // (comparing nullable with non-nullable -> LiftCSharpComparison) are ported;
    // the LiftCSharpUserEqualityComparison fall-back (?? LiftCSharpUserEquality-
    // Comparison, needs Call.Method.IsOperator + CSharpOperators) is deferred.

    // Peel logic.not from the hasValueTest (the C#
    // `while (hasValueTest.MatchLogicNot(out var arg))`).
    bool hasValueTestNegated = false;
    ILInstruction* peeled = nullptr;
    while (MatchLogicNot(hasValueTest, peeled)) {
        hasValueTest = peeled;
        hasValueTestNegated = !hasValueTestNegated;
    }

    if (hasValueTest && hasValueTest->Op == OpCode::Comp) {
        auto* hasValueComp = static_cast<Comp*>(hasValueTest);
        if (valueComp.IsLifted || hasValueComp->IsLifted()) return nullptr;
        // The HasValue comparison must be the same operator as the Value
        // comparison (the negated HasValue kind must match the new value kind).
        ComparisonKind effectiveKind = hasValueTestNegated
            ? NegateComparison(hasValueComp->Kind) : hasValueComp->Kind;
        if (effectiveKind != newComparisonKind) return nullptr;
        ILVariablePtr leftVar;
        if (!MatchHasValueCall(hasValueComp->Left.get(), leftVar)) return nullptr;
        ILVariablePtr rightVar;
        if (!MatchHasValueCall(hasValueComp->Right.get(), rightVar)) return nullptr;
        // DoLift the left with [leftVar], then replace [0] with rightVar and
        // DoLift the right (the C# `nullableVars = { leftVar }; DoLift(Left);
        // nullableVars[0] = rightVar; DoLift(Right)`). The single-bit relevance
        // gate `leftBits[0] && rightBits[0]` checks each side's GVO is on its var.
        std::vector<ILVariablePtr> nullableVars{leftVar};
        auto leftR = DoLift(valueComp.Left, nullableVars);
        nullableVars[0] = rightVar;
        auto rightR = DoLift(valueComp.Right, nullableVars);
        if (leftR.Lifted && rightR.Lifted && leftR.Bits && rightR.Bits &&
            (*leftR.Bits)[0] && (*rightR.Bits)[0] &&
            IsPure(leftR.Lifted->Flags()) && IsPure(rightR.Lifted->Flags())) {
            return valueComp.MakeLifted(newComparisonKind,
                std::move(leftR.Lifted), std::move(rightR.Lifted));
        }
        return nullptr;
    }
    // Fall-back: comparing nullable with non-nullable -> a single HasValue call
    // on the hasValueTest, fall back to the normal comparison code.
    ILVariablePtr v;
    if (newComparisonKind == ComparisonKind::Equality && !hasValueTestNegated &&
        MatchHasValueCall(hasValueTest, v)) {
        std::vector<ILVariablePtr> nullableVars{v};
        return LiftCSharpComparison(valueComp, newComparisonKind, nullableVars);
    }
    if (newComparisonKind == ComparisonKind::Inequality && hasValueTestNegated &&
        MatchHasValueCall(hasValueTest, v)) {
        std::vector<ILVariablePtr> nullableVars{v};
        return LiftCSharpComparison(valueComp, newComparisonKind, nullableVars);
    }
    return nullptr;
}

std::unique_ptr<ILInstruction> NullableLiftingTransform::LiftCSharpComparison(
    const CompOrDecimal& comp, ComparisonKind newComparisonKind,
    const std::vector<ILVariablePtr>& nullableVars) {
    // Port of NullableLiftingTransform.LiftCSharpComparison. The !comp.IsLifted
    // case (DoLiftBinary with both expected types UnknownType + MakeLifted,
    // gated on IsPure + bits.All) and the comp.IsLifted special case (legacy csc
    // `num.GetValueOrDefault() == const && num.HasValue`, where the comp was
    // already lifted by Run(Comp); clone the operands and MakeLifted) are ported.
    // The Comp branch's LeftExpectedType/RightExpectedType are nullptr
    // (SpecialType.UnknownType) -- the Call branch's parameter types are
    // deferred (no Call.Method parameters in this port).
    if (comp.IsLifted) {
        // The legacy csc case: the comp was already lifted (Run(Comp) lifted the
        // lhs of a logic.and). Treat it as if the transform had not undone the
        // optimization yet -- clone the operands and make a fresh lifted comp.
        // Only a single nullableVar is handled; one operand must be ldloc of it.
        if (nullableVars.size() != 1) return nullptr;
        auto isLdLocOfVar = [](ILInstruction* inst, const ILVariable* v) {
            return inst && inst->Op == OpCode::LdLoc &&
                static_cast<LdLoc*>(inst)->Variable.get() == v;
        };
        if (isLdLocOfVar(comp.Left, nullableVars[0].get()) ||
            isLdLocOfVar(comp.Right, nullableVars[0].get())) {
            auto leftClone = ClonePureExpression(comp.Left);
            auto rightClone = ClonePureExpression(comp.Right);
            if (leftClone && rightClone) {
                return comp.MakeLifted(newComparisonKind,
                    std::move(leftClone), std::move(rightClone));
            }
        }
        return nullptr;
    }
    auto binR = DoLiftBinary(comp.Left, comp.Right, nullptr, nullptr, nullableVars);
    if (binR.Left && binR.Right && binR.Bits &&
        IsPure(binR.Left->Flags()) && IsPure(binR.Right->Flags())) {
        if (!binR.Bits->All(0, static_cast<int>(nullableVars.size())))
            return nullptr;
        return comp.MakeLifted(newComparisonKind,
            std::move(binR.Left), std::move(binR.Right));
    }
    return nullptr;
}

} // namespace ILSpy::Decompiler::IL
