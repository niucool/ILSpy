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

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <string_view>

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
    // The Decimal branch (a Call to op_Equality/op_Inequality/op_LessThan/...
    // on System.Decimal) is deferred -- it needs Call.Method.IsOperator, which
    // this port's Call does not carry -- so a Call never matches here.
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

} // namespace ILSpy::Decompiler::IL
