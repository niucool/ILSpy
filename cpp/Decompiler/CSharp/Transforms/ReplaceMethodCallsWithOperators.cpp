// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

// The C# `bool IsStringParameter(IParameter p)` local function (lines
// 396-402): a `params` array parameter's element type is unwrapped before the
// known-type check (`if (p.IsParams && ty.Kind == TypeKind.Array) ty = ((ArrayType)
// ty).ElementType`). The port's IParameter carries no IsParams flag (the C#
// `bool IParameter.IsParams` lives on the resolved interface the minimal port
// does not model), so the array-unwrap arm reads false -- only a directly
// string-typed parameter matches (the conservative direction; the params form
// is deferred with the IParameter.IsParams surface).
bool IsStringParameter(const TS::IParameter& p) {
    return TS::IsKnownType(p.Type(), TS::KnownTypeCode::String);
}

} // namespace

// The C# `internal static bool HasCheckedEquivalent(IMethod method)`
// (ReplaceMethodCallsWithOperators.cs lines 264-271).
bool ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
    const TS::IMethod& method) {
    std::string name = method.Name();
    if (name.rfind("op_", 0) == 0)
        name = "op_Checked" + name.substr(3);
    // The C# `method.DeclaringType.GetMethods(m => m.IsOperator && m.Name == name).Any()`.
    TS::ITypePtr declaringType = method.DeclaringType();
    if (!declaringType)
        return false;
    auto methods = declaringType->GetMethods([&name](const TS::IMethod* m) {
        return m != nullptr && m->IsOperator() && m->Name() == name;
    });
    return !methods.empty();
}

// The port's ToStringCallPattern.Match stand-in (the C# `static readonly Pattern
// ToStringCallPattern` Choice, lines 339-351).
ReplaceMethodCallsWithOperators::ToStringCallMatch
ReplaceMethodCallsWithOperators::MatchToStringCallPattern(Syntax::Expression* expr) {
    ToStringCallMatch match;
    if (expr == nullptr)
        return match;
    // Pattern 1: `target.ToString()` -- InvocationExpression(MemberReference
    // Expression(AnyNode("target"), "ToString")).
    if (auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr)) {
        auto* memberRef =
            dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
        if (memberRef != nullptr && memberRef->MemberName() == "ToString") {
            match.call = invocation;
            match.target = memberRef->Target();
            match.nullConditional = false;
            return match;
        }
    }
    // Pattern 2: `target?.ToString()` -- UnaryOperatorExpression(
    // NullConditionalRewrap, InvocationExpression(MemberReferenceExpression(
    // UnaryOperatorExpression(NullConditional, AnyNode("target")), "ToString"))).
    if (auto* rewrap = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr)) {
        if (rewrap->Operator() == Syntax::UnaryOperatorType::NullConditionalRewrap) {
            auto* invocation =
                dynamic_cast<Syntax::InvocationExpression*>(rewrap->Expression());
            auto* memberRef =
                invocation != nullptr
                    ? dynamic_cast<Syntax::MemberReferenceExpression*>(
                          invocation->Target())
                    : nullptr;
            if (memberRef != nullptr && memberRef->MemberName() == "ToString") {
                if (auto* nullCheck =
                        dynamic_cast<Syntax::UnaryOperatorExpression*>(
                            memberRef->Target())) {
                    if (nullCheck->Operator()
                            == Syntax::UnaryOperatorType::NullConditional) {
                        match.call = invocation;
                        match.target = nullCheck->Expression();
                        match.nullConditional = true;
                        return match;
                    }
                }
            }
        }
    }
    return match;
}

// The C# `static bool ToStringIsKnownEffectFree(IType type)` (lines 406-427).
bool ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
    const TS::IType& type) {
    // The C# `NullableType.GetUnderlyingType(type)`.
    const TS::IType& unwrapped = TS::GetUnderlyingType(type);
    const TS::ITypeDefinition* definition = unwrapped.GetDefinition();
    if (definition == nullptr)
        return false;
    using TypeSystem::KnownTypeCode;
    switch (definition->KnownTypeCode()) {
        case KnownTypeCode::Boolean:
        case KnownTypeCode::Char:
        case KnownTypeCode::SByte:
        case KnownTypeCode::Byte:
        case KnownTypeCode::Int16:
        case KnownTypeCode::UInt16:
        case KnownTypeCode::Int32:
        case KnownTypeCode::UInt32:
        case KnownTypeCode::Int64:
        case KnownTypeCode::UInt64:
        case KnownTypeCode::Single:
        case KnownTypeCode::Double:
        case KnownTypeCode::Decimal:
        case KnownTypeCode::IntPtr:
        case KnownTypeCode::UIntPtr:
        case KnownTypeCode::String:
            return true;
        default:
            return false;
    }
}

// The C# `internal static Expression RemoveRedundantToStringInConcat(
// Expression expr, IMethod concatMethod, bool isLastArgument)` (lines 353-404).
Syntax::Expression* ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
    Syntax::Expression* expr, const TS::IMethod& concatMethod,
    bool isLastArgument) {
    ToStringCallMatch m = MatchToStringCallPattern(expr);
    if (m.call == nullptr)
        return expr;

    // The C# `if (!concatMethod.Parameters.All(IsStringParameter))`.
    for (const TypeSystem::IParameter* p : concatMethod.Parameters()) {
        if (p == nullptr || !IsStringParameter(*p))
            return expr;
    }

    // The C# `var toStringMethod = m.Get<Expression>("call").Single().GetSymbol()
    // as IMethod` -- the invocation's resolve-result symbol.
    const TS::ISymbol* symbol = CSharp::GetSymbol(*m.call);
    const TS::IMethod* toStringMethod =
        symbol != nullptr
            ? dynamic_cast<const TS::IMethod*>(symbol)
            : nullptr;

    // The C# `var type = target.GetResolveResult().Type`.
    const Semantics::ResolveResult* targetRR =
        m.target != nullptr ? CSharp::GetResolveResult(*m.target) : nullptr;
    if (targetRR == nullptr)
        return expr;
    const TS::IType& type = targetRR->Type();

    if (type.IsByRefLike()) {
        // ref structs cannot be converted to object for use with +
        return expr;
    }
    if (!(isLastArgument || ToStringIsKnownEffectFree(type))) {
        // ToString() order of evaluation matters, see CheckArgumentsForStringConcat().
        return expr;
    }
    if (type.IsReferenceType() != std::optional<bool>(false) && !m.nullConditional) {
        // ToString() might throw NullReferenceException, but the builtin operator+ doesn't.
        return expr;
    }
    if (!ToStringIsKnownEffectFree(type) && toStringMethod != nullptr
        && IL::MethodRequiresCopyForReadonlyLValue(toStringMethod)) {
        // ToString() on a struct may mutate the struct. For operator+ the C#
        // compiler creates a temporary copy before implicitly calling ToString(),
        // whereas an explicit ToString() call would mutate the original lvalue.
        // So we can't remove the compiler-generated ToString() call in cases
        // where this might make a difference.
        return expr;
    }

    // All checks succeeded, we can eliminate the ToString() call.
    // The C# compiler will generate an equivalent call if the code is recompiled.
    return m.target;
}

// The C# `static BinaryOperatorType? GetBinaryOperatorTypeFromMetadataName(string
// name, out bool isChecked, DecompilerSettings settings)` (lines 433-487).
std::optional<Syntax::BinaryOperatorType>
ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
    const std::string& name, bool& isChecked, const DecompilerSettings& settings) {
    isChecked = false;
    if (name == "op_Addition") return Syntax::BinaryOperatorType::Add;
    if (name == "op_Subtraction") return Syntax::BinaryOperatorType::Subtract;
    if (name == "op_Multiply") return Syntax::BinaryOperatorType::Multiply;
    if (name == "op_Division") return Syntax::BinaryOperatorType::Divide;
    if (name == "op_CheckedAddition" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Add;
    }
    if (name == "op_CheckedSubtraction" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Subtract;
    }
    if (name == "op_CheckedMultiply" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Multiply;
    }
    if (name == "op_CheckedDivision" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Divide;
    }
    if (name == "op_Modulus") return Syntax::BinaryOperatorType::Modulus;
    if (name == "op_BitwiseAnd") return Syntax::BinaryOperatorType::BitwiseAnd;
    if (name == "op_BitwiseOr") return Syntax::BinaryOperatorType::BitwiseOr;
    if (name == "op_ExclusiveOr") return Syntax::BinaryOperatorType::ExclusiveOr;
    if (name == "op_LeftShift") return Syntax::BinaryOperatorType::ShiftLeft;
    if (name == "op_RightShift") return Syntax::BinaryOperatorType::ShiftRight;
    if (name == "op_UnsignedRightShift" && settings.UnsignedRightShift())
        return Syntax::BinaryOperatorType::UnsignedShiftRight;
    if (name == "op_Equality") return Syntax::BinaryOperatorType::Equality;
    if (name == "op_Inequality") return Syntax::BinaryOperatorType::InEquality;
    if (name == "op_LessThan") return Syntax::BinaryOperatorType::LessThan;
    if (name == "op_LessThanOrEqual") return Syntax::BinaryOperatorType::LessThanOrEqual;
    if (name == "op_GreaterThan") return Syntax::BinaryOperatorType::GreaterThan;
    if (name == "op_GreaterThanOrEqual")
        return Syntax::BinaryOperatorType::GreaterThanOrEqual;
    return std::nullopt;
}

// The C# `static UnaryOperatorType? GetUnaryOperatorTypeFromMetadataName(string
// name, out bool isChecked, DecompilerSettings settings)` (lines 489-517).
std::optional<Syntax::UnaryOperatorType>
ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
    const std::string& name, bool& isChecked, const DecompilerSettings& settings) {
    isChecked = false;
    if (name == "op_LogicalNot") return Syntax::UnaryOperatorType::Not;
    if (name == "op_OnesComplement") return Syntax::UnaryOperatorType::BitNot;
    if (name == "op_UnaryNegation") return Syntax::UnaryOperatorType::Minus;
    if (name == "op_CheckedUnaryNegation" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Minus;
    }
    if (name == "op_UnaryPlus") return Syntax::UnaryOperatorType::Plus;
    if (name == "op_Increment") return Syntax::UnaryOperatorType::Increment;
    if (name == "op_Decrement") return Syntax::UnaryOperatorType::Decrement;
    if (name == "op_CheckedIncrement" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Increment;
    }
    if (name == "op_CheckedDecrement" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Decrement;
    }
    return std::nullopt;
}

// The C# `bool IsInstantiableTypeParameter(IType type)` (lines 272-275).
bool ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
    const TS::IType& type) {
    const auto* typeParameter = dynamic_cast<const TS::ITypeParameter*>(&type);
    return typeParameter != nullptr
        && typeParameter->HasDefaultConstructorConstraint();
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
