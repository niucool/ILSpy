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

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <any>
#include <cassert>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp {

// The C# `internal static bool IsSpanBasedStringConcat(IMethod method)`
// (CallBuilder.cs lines 300-318). The C# `method is not { Name: "Concat",
// IsStatic: true }` property-pattern guard ports to the two field checks; the
// C# `DeclaringType.IsKnownType(KnownTypeCode.String)` extension ports to the
// TypeSystemExtensions::IsKnownType free function over the resolved declaring
// type. The C# `p.Type.TypeArguments[0]` reads the generic instantiation's
// element type; the port's TypeArguments surface lives on ParameterizedType (the
// IType base does not carry it), so a non-ParameterizedType parameter type fails
// the span check -- an `IsKnownType(ReadOnlySpanOfT)` parameter type is always a
// closed `ReadOnlySpan<char>`/`ReadOnlySpan<T>` ParameterizedType in practice,
// and the C# would throw on a zero TypeArguments list where the port answers
// false (a C# `p.Type.TypeArguments[0]` ArgumentOutOfRangeException over an
// open generic parameter type; unreachable for a metadata-backed method).
bool CallBuilder::IsSpanBasedStringConcat(const TS::IMethod& method) {
    if (!(method.Name() == "Concat" && method.IsStatic())) {
        return false;
    }
    TS::ITypePtr declaringType = method.DeclaringType();
    if (!declaringType
        || !TS::IsKnownType(*declaringType,
                                    TS::KnownTypeCode::String)) {
        return false;
    }
    for (const TS::IParameter* p : method.Parameters()) {
        if (!p
            || !TS::IsKnownType(
                p->Type(), TS::KnownTypeCode::ReadOnlySpanOfT)) {
            return false;
        }
        auto* parameterized =
            dynamic_cast<const TS::ParameterizedType*>(&p->Type());
        if (!parameterized || parameterized->TypeArguments().empty()) {
            return false;
        }
        if (!TS::IsKnownType(*parameterized->TypeArguments()[0],
                                     TS::KnownTypeCode::Char)) {
            return false;
        }
    }
    return true;
}

// The C# `internal static bool IsStringToReadOnlySpanCharImplicitConversion(
// IMethod method)` (CallBuilder.cs lines 320-328). The `p.Type.TypeArguments[0]`
// element reads the ParameterizedType instantiation (the IsSpanBasedStringConcat
// note).
bool CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
    const TS::IMethod& method) {
    if (!method.IsOperator() || method.Name() != "op_Implicit")
        return false;
    if (method.Parameters().size() != 1)
        return false;
    const TS::IType& returnType = method.ReturnType();
    if (!TS::IsKnownType(returnType, TS::KnownTypeCode::ReadOnlySpanOfT))
        return false;
    auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(&returnType);
    if (parameterized == nullptr || parameterized->TypeArguments().empty()
        || !TS::IsKnownType(*parameterized->TypeArguments()[0],
                           TS::KnownTypeCode::Char))
        return false;
    const TS::IParameter* parameter = method.Parameters()[0];
    return parameter != nullptr
        && TS::IsKnownType(parameter->Type(), TS::KnownTypeCode::String);
}

// The C# `private static bool IsInterpolatedStringCreation(IMethod method,
// ArgumentList argumentList)` (CallBuilder.cs lines 755-766).
bool CallBuilder::IsInterpolatedStringCreation(const TS::IMethod& method,
                                               const ArgumentList& argumentList) {
    TS::ITypePtr declaring = method.DeclaringType();
    bool isCreation =
        method.IsStatic()
        && ((declaring && TS::IsKnownType(*declaring, TS::KnownTypeCode::String)
             && method.Name() == "Format")
            || (declaring && method.Name() == "Create"
                && declaring->Name() == "FormattableStringFactory"
                && declaring->Namespace() == "System.Runtime.CompilerServices"));
    if (!isCreation)
        return false;
    if (argumentList.ArgumentNames.has_value())
        return false;
    const auto& parameters = method.Parameters();
    bool lastIsParams = !parameters.empty() && parameters.back() != nullptr
        && parameters.back()->IsParams();
    return argumentList.IsExpandedForm
        || !lastIsParams
        || (argumentList.Length() == 2
            && dynamic_cast<Syntax::ArrayCreateExpression*>(
                   argumentList.Arguments[1].Expression()) != nullptr);
}

// The C# `static bool IsNullConditional(Expression expr)` (CallBuilder.cs
// lines 1480-1483).
bool CallBuilder::IsNullConditional(const Syntax::Expression* expr) {
    auto* unary = dynamic_cast<const Syntax::UnaryOperatorExpression*>(expr);
    return unary != nullptr
        && unary->Operator() == Syntax::UnaryOperatorType::NullConditional;
}

// The C# `private bool IsDelegateEqualityComparison(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1511-1523).
bool CallBuilder::IsDelegateEqualityComparison(
    const TS::IMethod& method,
    const std::vector<TranslatedExpression>& arguments) {
    if (!method.IsOperator())
        return false;
    TS::ITypePtr declaring = method.DeclaringType();
    if (!declaring || !TS::IsKnownType(*declaring, TS::KnownTypeCode::Delegate))
        return false;
    if (method.Name() != "op_Equality" && method.Name() != "op_Inequality")
        return false;
    if (arguments.size() != 2)
        return false;
    if (arguments[0].Type().Kind() != TS::TypeKind::Delegate)
        return false;
    return arguments[1].Type().Equals(arguments[0].Type());
}

// The C# `private Expression HandleDelegateEqualityComparison(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1524-1532).
Syntax::Expression* CallBuilder::HandleDelegateEqualityComparison(
    const TS::IMethod& method,
    const std::vector<TranslatedExpression>& arguments) {
    return new Syntax::BinaryOperatorExpression(
        arguments[0].Expression(),
        method.Name() == "op_Equality" ? Syntax::BinaryOperatorType::Equality
                                       : Syntax::BinaryOperatorType::InEquality,
        arguments[1].Expression());
}

// The C# `object.Equals(a, b)` over two boxed constant values (the
// IsOptionalArgument default comparison): both null (empty `std::any`) are
// equal, a null/non-null pair is not, a runtime-type mismatch is not, and
// same-typed primitives / strings / decimals compare by value. The
// CSharpOperators EqualsBoxedValues set plus the empty-any arm (that helper
// never sees the null shapes).
bool BoxedConstantEquals(const std::any& a, const std::any& b) {
    if (a.has_value() != b.has_value())
        return false;
    if (!a.has_value())
        return true;
    if (a.type() != b.type())
        return false;
    if (const bool* v = std::any_cast<bool>(&a))
        return *v == std::any_cast<bool>(b);
    if (const char16_t* v = std::any_cast<char16_t>(&a))
        return *v == std::any_cast<char16_t>(b);
    if (const std::int8_t* v = std::any_cast<std::int8_t>(&a))
        return *v == std::any_cast<std::int8_t>(b);
    if (const std::uint8_t* v = std::any_cast<std::uint8_t>(&a))
        return *v == std::any_cast<std::uint8_t>(b);
    if (const std::int16_t* v = std::any_cast<std::int16_t>(&a))
        return *v == std::any_cast<std::int16_t>(b);
    if (const std::uint16_t* v = std::any_cast<std::uint16_t>(&a))
        return *v == std::any_cast<std::uint16_t>(b);
    if (const std::int32_t* v = std::any_cast<std::int32_t>(&a))
        return *v == std::any_cast<std::int32_t>(b);
    if (const std::uint32_t* v = std::any_cast<std::uint32_t>(&a))
        return *v == std::any_cast<std::uint32_t>(b);
    if (const std::int64_t* v = std::any_cast<std::int64_t>(&a))
        return *v == std::any_cast<std::int64_t>(b);
    if (const std::uint64_t* v = std::any_cast<std::uint64_t>(&a))
        return *v == std::any_cast<std::uint64_t>(b);
    if (const float* v = std::any_cast<float>(&a))
        return *v == std::any_cast<float>(b);
    if (const double* v = std::any_cast<double>(&a))
        return *v == std::any_cast<double>(b);
    if (const Util::Decimal* v = std::any_cast<Util::Decimal>(&a))
        return Util::CompareDecimal(*v, std::any_cast<Util::Decimal>(b)) == 0;
    if (const std::string* v = std::any_cast<std::string>(&a))
        return *v == std::any_cast<std::string>(b);
    if (const TS::ITypePtr* v = std::any_cast<TS::ITypePtr>(&a))
        return *v == std::any_cast<TS::ITypePtr>(b);
    return false;
}

// ---------------------------------------------------------------------------
// CallBuilder instance (CallBuilder.cs constructor + BuildArgumentList)
// ---------------------------------------------------------------------------

CallBuilder::CallBuilder(ExpressionBuilder& expressionBuilder,
                         const DecompilerSettings& settings)
    : expressionBuilder_(&expressionBuilder), settings_(&settings) {}

// The C# `private ArgumentList BuildArgumentList(...)` (CallBuilder.cs lines
// 941-1052). The positional path is ported; the named-argument
// (`argumentToParameterMap`) path and the params-expansion
// (`TransformParamsArgument`) path throw the loud deferral (both need the
// unported overload-resolution machinery). The `target` parameter feeds only
// TransformParamsArgument, so it is unused on the ported path.
CallBuilder::ArgumentList CallBuilder::BuildArgumentList(
    const ExpectedTargetDetails& expectedTargetDetails, const Sem::ResolveResult* target,
    const TS::IMethod& method, int firstParamIndex,
    const std::vector<IL::ILInstruction*>& callArguments,
    const std::optional<std::vector<int>>& argumentToParameterMap)
{
    if (argumentToParameterMap.has_value())
    {
        throw std::logic_error(
            "CallBuilder::BuildArgumentList: the named-argument "
            "(argumentToParameterMap) path is not yet ported");
    }
    (void)target;
    const auto& parameters = method.Parameters();
    assert(static_cast<int>(callArguments.size())
           == firstParamIndex + static_cast<int>(parameters.size()));

    ArgumentList list;
    std::vector<TranslatedExpression> arguments;
    arguments.reserve(parameters.size());
    std::vector<const TS::IParameter*> expectedParameters;
    expectedParameters.reserve(parameters.size());
    bool isExpandedForm = false;
    Util::BitSet isPrimitiveValue(static_cast<int>(parameters.size()));

    // Optional arguments: -2 = none, -1 = forbidden, >= 0 = the first argument
    // that may be removed (the default value of the parameter).
    int firstOptionalArgumentIndex = settings_->OptionalArguments() ? -2 : -1;
    for (int i = firstParamIndex; i < static_cast<int>(callArguments.size()); ++i)
    {
        const TS::IParameter* parameter = parameters[i - firstParamIndex];
        TranslatedExpression arg =
            expressionBuilder_->Translate(callArguments[i], &parameter->Type());
        if (IsPrimitiveValueThatShouldBeNamedArgument(arg, method, *parameter))
            isPrimitiveValue.Set(static_cast<int>(arguments.size()));
        if (IsOptionalArgument(*parameter, arg))
        {
            if (firstOptionalArgumentIndex == -2)
                firstOptionalArgumentIndex = i - firstParamIndex;
        }
        else
        {
            if (firstOptionalArgumentIndex != -1)
                firstOptionalArgumentIndex = -2;
        }
        if (settings_->ExpandParamsArguments() && parameter->IsParams()
            && i + 1 == static_cast<int>(callArguments.size()))
        {
            if (TransformParamsArgument(expectedTargetDetails, target, method, *parameter,
                                        arg, expectedParameters, arguments))
            {
                firstOptionalArgumentIndex = -1;
                isExpandedForm = true;
                continue;
            }
        }

        const TS::IType* parameterType;
        if (parameter->Type().Kind() == TS::TypeKind::Dynamic)
            parameterType = &expressionBuilder_->compilation->FindType(
                TS::KnownTypeCode::Object);
        else
            parameterType = &parameter->Type();

        bool allowImplicitConversion = arg.Type().Kind() != TS::TypeKind::Dynamic;
        arg = arg.ConvertTo(const_cast<TS::IType&>(*parameterType), *expressionBuilder_,
                            /*checkForOverflow=*/false, allowImplicitConversion);

        if (parameter->ReferenceKind() != TS::ReferenceKind::None)
        {
            arg = ExpressionBuilder::ChangeDirectionExpressionTo(
                arg, parameter->ReferenceKind(),
                callArguments[i]->Op == IL::OpCode::AddressOf);
        }

        arguments.push_back(std::move(arg));
        expectedParameters.push_back(parameter);
    }

    list.Arguments = std::move(arguments);
    list.ExpectedParameters = std::move(expectedParameters);
    for (const TS::IParameter* p : list.ExpectedParameters)
        list.ParameterNames.push_back(p != nullptr ? p->Name() : std::string());
    list.ArgumentNames = std::nullopt;
    list.ArgumentToParameterMap = std::nullopt;
    list.IsExpandedForm = isExpandedForm;
    list.IsPrimitiveValue = std::move(isPrimitiveValue);
    list.FirstOptionalArgumentIndex = firstOptionalArgumentIndex;
    list.UseImplicitlyTypedOut = true;
    list.AddNamesToPrimitiveValues =
        settings_->NamedArguments() && settings_->NonTrailingNamedArguments();
    return list;
}

// The C# `private bool IsPrimitiveValueThatShouldBeNamedArgument(...)`
// (CallBuilder.cs lines 1054-1060).
bool CallBuilder::IsPrimitiveValueThatShouldBeNamedArgument(
    const TranslatedExpression& arg, const TS::IMethod& method,
    const TS::IParameter& p) const
{
    const Sem::ResolveResult* rr = arg.ResolveResult();
    if (rr == nullptr || !rr->IsCompileTimeConstant())
        return false;
    TS::ITypePtr declaringType = method.DeclaringType();
    if (declaringType
        && TS::IsKnownType(*declaringType, TS::KnownTypeCode::NullableOfT))
        return false;
    return TS::IsKnownType(p.Type(), TS::KnownTypeCode::Boolean);
}

// The C# `bool IsOptionalArgument(IParameter parameter, TranslatedExpression
// arg)` (CallBuilder.cs lines 1128-1141).
bool CallBuilder::IsOptionalArgument(const TS::IParameter& parameter,
                                     const TranslatedExpression& arg) const
{
    if (!parameter.IsOptional())
        return false;

    const Sem::ResolveResult* rr = arg.ResolveResult();
    bool nullLiteralConversion = false;
    if (auto* crr = dynamic_cast<const Sem::ConversionResolveResult*>(rr))
    {
        const Sem::Conversion* conversion = crr->ConversionProperty();
        nullLiteralConversion = conversion != nullptr
            && conversion->IsNullLiteralConversion();
    }
    if (!(rr != nullptr && rr->IsCompileTimeConstant()) && !nullLiteralConversion)
        return false;

    for (const TS::IAttribute* attribute : parameter.GetAttributes())
    {
        if (attribute == nullptr)
            continue;
        const TS::IType& attributeType = attribute->AttributeType();
        if (TS::IsKnownType(attributeType, TS::KnownAttribute::CallerMemberName)
            || TS::IsKnownType(attributeType, TS::KnownAttribute::CallerFilePath)
            || TS::IsKnownType(attributeType, TS::KnownAttribute::CallerLineNumber))
            return false;
    }
    return BoxedConstantEquals(parameter.GetConstantValue(), rr->ConstantValue());
}

// The C# `private bool TransformParamsArgument(...)` (CallBuilder.cs lines
// 1062-1126): deferred loudly. It needs the unported overload-resolution
// `IsUnambiguousCall` plus the CSharpInvocationResolveResult / array-create
// resolve-result arms; the positional non-params path never reaches it.
bool CallBuilder::TransformParamsArgument(
    const ExpectedTargetDetails& expectedTargetDetails,
    const Sem::ResolveResult* targetResolveResult, const TS::IMethod& method,
    const TS::IParameter& parameter, const TranslatedExpression& paramsArgument,
    std::vector<const TS::IParameter*>& expectedParameters,
    std::vector<TranslatedExpression>& arguments)
{
    (void)expectedTargetDetails;
    (void)targetResolveResult;
    (void)method;
    (void)parameter;
    (void)paramsArgument;
    (void)expectedParameters;
    (void)arguments;
    throw std::logic_error(
        "CallBuilder::TransformParamsArgument: the params-expansion path is not "
        "yet ported (needs the overload-resolution IsUnambiguousCall machinery)");
}

// ---------------------------------------------------------------------------
// CallBuilder::ArgumentList (CallBuilder.cs lines 48-200)
// ---------------------------------------------------------------------------

int CallBuilder::ArgumentList::GetActualArgumentCount() const
{
    if (FirstOptionalArgumentIndex < 0)
        return static_cast<int>(Arguments.size());
    return FirstOptionalArgumentIndex;
}

std::optional<std::vector<std::string>> CallBuilder::ArgumentList::GetArgumentNames(
    int skipCount) const
{
    std::optional<std::vector<std::string>> argumentNames = ArgumentNames;
    bool allParameterNamesNonEmpty = true;
    for (const std::string& name : ParameterNames)
    {
        if (name.empty())
        {
            allParameterNamesNonEmpty = false;
            break;
        }
    }
    if (AddNamesToPrimitiveValues && IsPrimitiveValue.Any() && !IsExpandedForm
        && allParameterNamesNonEmpty)
    {
        assert(skipCount == 0);
        if (!argumentNames.has_value())
            argumentNames = std::vector<std::string>(Arguments.size());
        for (std::size_t i = 0; i < Arguments.size(); ++i)
        {
            if (IsPrimitiveValue[static_cast<int>(i)] && (*argumentNames)[i].empty())
                (*argumentNames)[i] = ParameterNames[i];
        }
    }
    return argumentNames;
}

std::vector<std::shared_ptr<Sem::ResolveResult>>
CallBuilder::ArgumentList::GetArgumentResolveResults(int skipCount) const
{
    std::vector<std::shared_ptr<Sem::ResolveResult>> result;
    int count = GetActualArgumentCount();
    for (int i = skipCount; i < count; ++i)
    {
        const TranslatedExpression& expression = Arguments[i];
        const TS::IParameter* param = ExpectedParameters[i];
        if (UseImplicitlyTypedOut && param != nullptr
            && param->ReferenceKind() == TS::ReferenceKind::Out)
        {
            auto* byRef = dynamic_cast<TS::ByReferenceType*>(
                &const_cast<TS::IType&>(expression.Type()));
            if (byRef != nullptr)
            {
                result.push_back(std::make_shared<Sem::OutVarResolveResult>(byRef->Element()));
                continue;
            }
        }
        result.push_back(GetSharedResolveResult(*expression.Expression()));
    }
    return result;
}

std::vector<std::shared_ptr<Sem::ResolveResult>>
CallBuilder::ArgumentList::GetArgumentResolveResultsDirect(int skipCount) const
{
    std::vector<std::shared_ptr<Sem::ResolveResult>> result;
    int count = GetActualArgumentCount();
    for (int i = skipCount; i < count; ++i)
        result.push_back(GetSharedResolveResult(*Arguments[i].Expression()));
    return result;
}

std::vector<Syntax::Expression*> CallBuilder::ArgumentList::GetArgumentExpressions(
    int skipCount) const
{
    std::optional<std::vector<std::string>> argumentNames = GetArgumentNames(skipCount);
    int argumentCount = GetActualArgumentCount();
    auto addAnnotations = [&](Syntax::Expression* expression) -> Syntax::Expression* {
        if (!UseImplicitlyTypedOut)
            return expression;
        if (auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(
                GetResolveResult(*expression));
            brrr != nullptr && brrr->ReferenceKind() == TS::ReferenceKind::Out)
        {
            expression->AddAnnotation(UseImplicitlyTypedOutAnnotationHandle());
        }
        return expression;
    };
    std::vector<Syntax::Expression*> result;
    if (!argumentNames.has_value())
    {
        for (int i = skipCount; i < argumentCount; ++i)
            result.push_back(addAnnotations(Arguments[i].Expression()));
    }
    else
    {
        assert(skipCount == 0);
        int nameCount = static_cast<int>(argumentNames->size());
        int end = argumentCount < nameCount ? argumentCount : nameCount;
        for (int i = 0; i < end; ++i)
        {
            Syntax::Expression* expression = addAnnotations(Arguments[i].Expression());
            const std::string& name = (*argumentNames)[i];
            if (name.empty())
                result.push_back(expression);
            else
                result.push_back(new Syntax::NamedArgumentExpression(name, expression));
        }
    }
    return result;
}

bool CallBuilder::ArgumentList::CanInferAnonymousTypePropertyNamesFromArguments() const
{
    for (std::size_t i = 0; i < Arguments.size(); ++i)
    {
        std::string inferredName;
        if (auto* ident =
                dynamic_cast<Syntax::IdentifierExpression*>(Arguments[i].Expression()))
            inferredName = ident->Identifier();
        else if (auto* member = dynamic_cast<Syntax::MemberReferenceExpression*>(
                     Arguments[i].Expression()))
            inferredName = member->MemberName();
        if (ExpectedParameters[i] == nullptr
            || inferredName != ExpectedParameters[i]->Name())
            return false;
    }
    return true;
}

void CallBuilder::ArgumentList::CheckNoNamedOrOptionalArguments() const
{
    assert(!ArgumentToParameterMap.has_value() && !ArgumentNames.has_value()
           && FirstOptionalArgumentIndex < 0);
}

} // namespace ILSpy::Decompiler::CSharp
