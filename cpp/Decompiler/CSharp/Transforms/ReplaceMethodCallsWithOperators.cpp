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

} // namespace ILSpy::Decompiler::CSharp::Transforms
