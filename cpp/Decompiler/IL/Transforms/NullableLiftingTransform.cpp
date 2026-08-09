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

} // namespace ILSpy::Decompiler::IL
