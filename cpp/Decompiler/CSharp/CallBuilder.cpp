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
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/ArrayTypeReference.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/InheritanceHelper.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp {

// The owning shared_ptr of the node's resolve-result annotation (the
// ExpressionBuilder.cpp local copied beside its second consumer -- the
// annotation channel owns the shared handle the C# GC reference aliases).
namespace {

std::shared_ptr<Sem::ResolveResult> SharedResolveResultAnnotation(
    const Syntax::Expression& expr)
{
    for (const auto& a : expr.SharedAnnotations())
    {
        if (dynamic_cast<Sem::ResolveResult*>(a.get()) != nullptr)
            return std::static_pointer_cast<Sem::ResolveResult>(a);
    }
    return nullptr;
}

// The C# `declaringType.TypeArguments` read over the IType interface: the
// ParameterizedType's own arguments; every other type shape (an open generic
// definition, a wrapper) carries the empty list the C# AbstractType default
// yields. The [0] access over the empty shape is the C#
// ArgumentOutOfRangeException -- mapped to std::out_of_range (the
// empty-list-index convention).
std::vector<TS::ITypePtr> DeclaringTypeArguments(const TS::IType& declaringType)
{
    if (const auto* parameterized =
            dynamic_cast<const TS::ParameterizedType*>(&declaringType))
    {
        return parameterized->TypeArguments();
    }
    return {};
}

// The C# `method.TypeArguments.Any(a => a.ContainsAnonymousType())` predicate
// the RequireTypeArguments block reads (CallBuilder.cs line 1205): the port
// helper over the argument snapshot.
bool AnyTypeArgumentContainsAnonymousType(
    const std::vector<TS::ITypePtr>& typeArguments)
{
    for (const TS::ITypePtr& a : typeArguments)
    {
        if (a != nullptr && ContainsAnonymousType(*a))
            return true;
    }
    return false;
}

// The C# `GetActualArgumentCount()` body shared by the three argument slices.
int GetActualArgumentCount(int firstOptionalArgumentIndex,
                          int argumentCount)
{
    if (firstOptionalArgumentIndex < 0)
        return argumentCount;
    return firstOptionalArgumentIndex;
}

} // namespace

// ---------------------------------------------------------------------------
// CallBuilder instance surface

CallBuilder::CallBuilder(ExpressionBuilder* expressionBuilder,
                         const TS::ICompilation& typeSystem,
                         const DecompilerSettings* settings)
    : settings_(settings), typeSystem_(&typeSystem)
{
    if (expressionBuilder == nullptr)
    {
        // The C# `this.expressionBuilder = expressionBuilder` null-deref arm
        // (the established invalid_argument convention).
        throw std::invalid_argument(
            "NullReferenceException: Object reference not set to an instance of an object.");
    }
    expressionBuilder_ = expressionBuilder;
    resolver_ = expressionBuilder_->resolver;
}

// ---------------------------------------------------------------------------
// The span-based string-concat family

// The C# `internal static bool IsSpanBasedStringConcat(IMethod method)` (lines
// 300-318). The C# `p.Type.TypeArguments[0]` reads the generic instantiation's
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
// IMethod method)` (lines 322-330). The ReturnType `TypeArguments[0]` carries
// the same ParameterizedType divergence note as above.
bool CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
    const TS::IMethod* method) {
    if (method == nullptr || !method->IsOperator()
        || method->Name() != "op_Implicit")
    {
        return false;
    }
    if (method->Parameters().size() != 1)
        return false;
    if (!TS::IsKnownType(method->ReturnType(),
                             TS::KnownTypeCode::ReadOnlySpanOfT))
        return false;
    auto* returnTypeParameterized =
        dynamic_cast<const TS::ParameterizedType*>(&method->ReturnType());
    if (!returnTypeParameterized || returnTypeParameterized->TypeArguments().empty())
        return false;
    if (!TS::IsKnownType(*returnTypeParameterized->TypeArguments()[0],
                             TS::KnownTypeCode::Char))
        return false;
    return TS::IsKnownType(method->Parameters()[0]->Type(),
                               TS::KnownTypeCode::String);
}

// The C# `static bool IsSpanBasedStringConcat(CallInstruction call, out ...)
// operands)` (lines 275-298): the argument walk over the span-based call.
bool CallBuilder::IsSpanBasedStringConcat(
    const IL::Call& call,
    std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>>&
        operands)
{
    operands.reset();

    if (call.Method == nullptr || !IsSpanBasedStringConcat(*call.Method))
    {
        return false;
    }

    std::optional<int> firstStringArgumentIndex;
    operands = std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>();

    for (auto& argUnique : call.Arguments)
    {
        IL::ILInstruction* arg = argUnique.get();
        auto* callArg = arg != nullptr ? dynamic_cast<IL::Call*>(arg) : nullptr;
        if (callArg != nullptr && !callArg->IsNewObj
            && callArg->Method != nullptr
            && IsStringToReadOnlySpanCharImplicitConversion(
                callArg->Method.get()))
        {
            if (!firstStringArgumentIndex.has_value())
                firstStringArgumentIndex = arg->ChildIndex;
            // The C# `opImplicit.Arguments.Single()` (the .NET
            // InvalidOperationException messages for the degenerate shapes).
            if (callArg->Arguments.size() != 1)
            {
                throw std::runtime_error(
                    callArg->Arguments.empty()
                        ? "Sequence contains no elements"
                        : "Sequence contains more than one element.");
            }
            operands->emplace_back(callArg->Arguments[0].get(),
                                   TS::KnownTypeCode::String);
        }
        else if (callArg != nullptr && callArg->IsNewObj
                 && callArg->Method != nullptr
                 && callArg->Arguments.size() == 1
                 && callArg->Arguments[0]->Op == IL::OpCode::AddressOf
                 && IL::IsReadOnlySpanCharCtor(callArg->Method.get()))
        {
            auto* addressOf = static_cast<IL::AddressOf*>(callArg->Arguments[0].get());
            operands->emplace_back(addressOf->Argument.get(), TS::KnownTypeCode::Char);
        }
        else
        {
            return false;
        }
    }

    return static_cast<int>(call.Arguments.size()) >= 2
           && firstStringArgumentIndex.has_value()
           && *firstStringArgumentIndex <= 1;
}

// The C# `private ExpressionWithResolveResult BuildStringConcat(IMethod method,
// List<(ILInstruction, KnownTypeCode)> operands)` (lines 227-252): the `s1 +
// s2 + ...` fold. The C# reuses ONE MemberResolveResult(null, method)
// annotation for every node in the chain.
ExpressionWithResolveResult CallBuilder::BuildStringConcat(
    const TS::IMethod& method,
    const std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>& operands)
{
    assert(!operands.empty());
    const TS::IType* type = &typeSystem_->FindType(operands[0].second);
    // The C# implicit TranslatedExpression -> ExpressionWithResolveResult
    // conversion (TranslatedExpression.cs line 142).
    TranslatedExpression firstTranslated =
        expressionBuilder_->Translate(operands[0].first, type)
            .ConvertTo(const_cast<TS::IType&>(*type), *expressionBuilder_);
    ExpressionWithResolveResult result(firstTranslated.Expression(),
                                       firstTranslated.ResolveResult());
    auto rr = std::make_shared<Sem::MemberResolveResult>(
        static_cast<std::shared_ptr<Sem::ResolveResult>>(nullptr),
        static_cast<const TS::IMember*>(&method));

    for (std::size_t i = 1; i < operands.size(); i++)
    {
        type = &typeSystem_->FindType(operands[i].second);
        TranslatedExpression expr =
            expressionBuilder_->Translate(operands[i].first, type)
                .ConvertTo(const_cast<TS::IType&>(*type), *expressionBuilder_);
        auto* binary = new Syntax::BinaryOperatorExpression(
            result.Expression(), Syntax::BinaryOperatorType::Add, expr.Expression());
        result = WithRR(*binary, rr);
    }

    return result;
}

// ---------------------------------------------------------------------------
// The argument-list machinery (CallBuilder.cs lines 941-1150) and the
// overload-resolution composition (lines 1554-1650 + 1816-1831)

// The C# `object.Equals(parameter.GetConstantValue(), arg.ResolveResult.ConstantValue)`
// over two boxed operands (the file-local EqualsBoxedValues convention copied beside
// its consumer in CSharpOperators.cpp): a runtime-type mismatch yields false, null ==
// null is true, null vs non-null is false, same-type boxed values compare by value.
bool EqualsBoxedValues(const std::any& lhs, const std::any& rhs)
{
    if (!lhs.has_value() && !rhs.has_value())
        return true;
    if (!lhs.has_value() || !rhs.has_value())
        return false;
    if (lhs.type() != rhs.type())
        return false;
    if (const bool* v = std::any_cast<bool>(&lhs))
        return *v == std::any_cast<bool>(rhs);
    if (const char16_t* v = std::any_cast<char16_t>(&lhs))
        return *v == std::any_cast<char16_t>(rhs);
    if (const std::int8_t* v = std::any_cast<std::int8_t>(&lhs))
        return *v == std::any_cast<std::int8_t>(rhs);
    if (const std::uint8_t* v = std::any_cast<std::uint8_t>(&lhs))
        return *v == std::any_cast<std::uint8_t>(rhs);
    if (const std::int16_t* v = std::any_cast<std::int16_t>(&lhs))
        return *v == std::any_cast<std::int16_t>(rhs);
    if (const std::uint16_t* v = std::any_cast<std::uint16_t>(&lhs))
        return *v == std::any_cast<std::uint16_t>(rhs);
    if (const std::int32_t* v = std::any_cast<std::int32_t>(&lhs))
        return *v == std::any_cast<std::int32_t>(rhs);
    if (const std::uint32_t* v = std::any_cast<std::uint32_t>(&lhs))
        return *v == std::any_cast<std::uint32_t>(rhs);
    if (const std::int64_t* v = std::any_cast<std::int64_t>(&lhs))
        return *v == std::any_cast<std::int64_t>(rhs);
    if (const std::uint64_t* v = std::any_cast<std::uint64_t>(&lhs))
        return *v == std::any_cast<std::uint64_t>(rhs);
    if (const float* v = std::any_cast<float>(&lhs))
        return *v == std::any_cast<float>(rhs);
    if (const double* v = std::any_cast<double>(&lhs))
        return *v == std::any_cast<double>(rhs);
    if (const Util::Decimal* v = std::any_cast<Util::Decimal>(&lhs))
        return Util::CompareDecimal(*v, std::any_cast<Util::Decimal>(rhs)) == 0;
    if (const std::string* v = std::any_cast<std::string>(&lhs))
        return *v == std::any_cast<std::string>(rhs);
    return false;
}

ArgumentList CallBuilder::BuildArgumentList(
    const ExpectedTargetDetails& expectedTargetDetails,
    const Sem::ResolveResult* target, const TS::IMethod& method, int firstParamIndex,
    const std::vector<IL::ILInstruction*>& callArguments,
    const std::optional<std::vector<int>>& argumentToParameterMap)
{
    ArgumentList list;

    // Translate arguments to the expected parameter types
    std::vector<TranslatedExpression> arguments;
    std::optional<std::vector<std::string>> argumentNames;
    const auto methodParameters = method.Parameters();
    assert(static_cast<int>(callArguments.size())
           == firstParamIndex + static_cast<int>(methodParameters.size()));
    std::vector<const TS::IParameter*> expectedParameters; // parameters, but in argument order
    bool isExpandedForm = false;
    Util::BitSet isPrimitiveValue(static_cast<int>(methodParameters.size()));

    // Optional arguments:
    // This value has the following values:
    // -2 - there are no optional arguments
    // -1 - optional arguments are forbidden
    // >= 0 - the index of the first argument that can be removed, because it is optional
    // and is the default value of the parameter.
    int firstOptionalArgumentIndex =
        expressionBuilder_->settings->OptionalArguments() ? -2 : -1;
    for (int i = firstParamIndex; i < static_cast<int>(callArguments.size()); i++)
    {
        const TS::IParameter* parameter;
        if (argumentToParameterMap.has_value())
        {
            if (!argumentNames.has_value()
                && (*argumentToParameterMap)[i] != i - firstParamIndex)
            {
                // Starting at the first argument that is out-of-place,
                // assign names to that argument and all following arguments:
                argumentNames = std::vector<std::string>(methodParameters.size());
            }
            int mappedIndex = (*argumentToParameterMap)[i];
            if (mappedIndex < 0
                || mappedIndex >= static_cast<int>(methodParameters.size()))
            {
                // The C# `method.Parameters[argumentToParameterMap[i]]` indexer throw.
                throw std::out_of_range(
                    "Index was out of range. Must be non-negative and less than the "
                    "size of the collection. (Parameter 'index')");
            }
            parameter = methodParameters[mappedIndex];
            if (argumentNames.has_value()
                && IL::AssignVariableNames::IsValidName(parameter->Name()))
            {
                (*argumentNames)[static_cast<int>(arguments.size())] =
                    parameter->Name();
            }
        }
        else
        {
            parameter = methodParameters[i - firstParamIndex];
        }
        TranslatedExpression arg =
            expressionBuilder_->Translate(callArguments[i], &parameter->Type());
        if (IsPrimitiveValueThatShouldBeNamedArgument(arg, method, *parameter))
        {
            isPrimitiveValue.Set(static_cast<int>(arguments.size()));
        }
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
        if (expressionBuilder_->settings->ExpandParamsArguments()
            && parameter->IsParams() && i + 1 == static_cast<int>(callArguments.size())
            && !argumentToParameterMap.has_value())
        {
            // Parameter is marked params
            // If the argument is an array creation, inline all elements into the call
            // and add missing default values. Otherwise handle it normally.
            if (TransformParamsArgument(expectedTargetDetails, target, method,
                                        *parameter, arg, expectedParameters, arguments))
            {
                assert(!argumentNames.has_value());
                firstOptionalArgumentIndex = -1;
                isExpandedForm = true;
                continue;
            }
        }

        const TS::IType* parameterType;
        if (parameter->Type().Kind() == TS::TypeKind::Dynamic)
        {
            parameterType = &typeSystem_->FindType(TS::KnownTypeCode::Object);
        }
        else
        {
            parameterType = &parameter->Type();
        }

        arg = arg.ConvertTo(const_cast<TS::IType&>(*parameterType), *expressionBuilder_,
                            /*checkForOverflow=*/false,
                            /*allowImplicitConversion=*/arg.Type().Kind()
                                != TS::TypeKind::Dynamic);

        if (parameter->ReferenceKind() != TS::ReferenceKind::None)
        {
            arg = ExpressionBuilder::ChangeDirectionExpressionTo(
                arg, parameter->ReferenceKind(),
                callArguments[i]->Op == IL::OpCode::AddressOf);
        }

        arguments.push_back(arg);
        expectedParameters.push_back(parameter);
    }

    list.ExpectedParameters = expectedParameters;
    list.Arguments = arguments;
    list.ParameterNames.reserve(expectedParameters.size());
    for (const TS::IParameter* p : expectedParameters)
        list.ParameterNames.push_back(p->Name());
    list.ArgumentNames = argumentNames;
    list.ArgumentToParameterMap = argumentToParameterMap;
    list.IsExpandedForm = isExpandedForm;
    list.IsPrimitiveValue = isPrimitiveValue;
    list.FirstOptionalArgumentIndex = firstOptionalArgumentIndex;
    list.UseImplicitlyTypedOut = true;
    list.AddNamesToPrimitiveValues =
        expressionBuilder_->settings->NamedArguments()
        && expressionBuilder_->settings->NonTrailingNamedArguments();
    return list;
}

bool CallBuilder::IsPrimitiveValueThatShouldBeNamedArgument(
    const TranslatedExpression& arg, const TS::IMethod& method,
    const TS::IParameter& p)
{
    // The C# `method.DeclaringType` is the WITH-type-arguments declaring type
    // (null for a method without one -- the port's shared_ptr is null).
    TS::ITypePtr declaringType = method.DeclaringType();
    if (!arg.ResolveResult()->IsCompileTimeConstant()
        || (declaringType != nullptr
            && TS::IsKnownType(*declaringType, TS::KnownTypeCode::NullableOfT)))
        return false;
    return TS::IsKnownType(p.Type(), TS::KnownTypeCode::Boolean);
}

bool CallBuilder::TransformParamsArgument(
    const ExpectedTargetDetails& expectedTargetDetails,
    const Sem::ResolveResult* targetResolveResult, const TS::IMethod& method,
    const TS::IParameter& parameter, const TranslatedExpression& paramsArgument,
    std::vector<const TS::IParameter*>& expectedParameters,
    std::vector<TranslatedExpression>& arguments)
{
    // The C# `ExtractArguments` local function -- the three match arms over
    // paramsArgument.ResolveResult.
    TS::ITypePtr elementType;
    std::vector<const TS::IParameter*> expandedParameters;
    std::vector<TranslatedExpression> expandedArguments;
    bool extracted = false;
    const Sem::ResolveResult* rr = paramsArgument.ResolveResult();
    if (auto* invocation = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(rr))
    {
        const TS::IMethod* member = dynamic_cast<const TS::IMethod*>(invocation->Member());
        if (member == nullptr)
            return false;
        const auto& args = invocation->Arguments();
        // match System.Array.Empty<T>()
        if (args.empty() && member->IsStatic()
            && member->FullName() == "System.Array.Empty"
            && member->TypeArguments().size() == 1)
        {
            elementType = member->TypeArguments()[0];
            extracted = true;
        }
        // match System.ReadOnlySpan<T>..ctor(ref readonly T)
        else if (paramsArgument.Expression() != nullptr
                 && dynamic_cast<const Syntax::ObjectCreateExpression*>(
                        paramsArgument.Expression())
                        != nullptr
                 && member->IsConstructor() && member->Parameters().size() == 1
                 && member->Parameters()[0]->ReferenceKind() == TS::ReferenceKind::RefReadOnly
                 && dynamic_cast<const TS::ByReferenceType*>(
                        &member->Parameters()[0]->Type())
                        != nullptr
                 && member->DeclaringType() != nullptr
                 && DeclaringTypeArguments(*member->DeclaringType()).size() == 1
                 && TS::IsKnownType(*member->DeclaringType(),
                                    TS::KnownTypeCode::ReadOnlySpanOfT)
                 && static_cast<const TS::ByReferenceType&>(
                        member->Parameters()[0]->Type())
                        .Element()
                        ->Equals(
                            *DeclaringTypeArguments(*member->DeclaringType())[0]))
        {
            if (args.size() != 1)
            {
                // The C# `oce.Arguments.Single()` degenerate-shape throw.
                throw std::runtime_error(
                    args.empty() ? "Sequence contains no elements"
                                 : "Sequence contains more than one element.");
            }
            // The C# `new TranslatedExpression(oce.Arguments.Single())` -- the detached
            // argument node with its OWN resolve-result annotation (the node-reading
            // ctor, not the invocation's resolve result).
            auto* oce = const_cast<Syntax::ObjectCreateExpression*>(
                static_cast<const Syntax::ObjectCreateExpression*>(
                    paramsArgument.Expression()));
            Syntax::Expression* argument = Syntax::Detach(oce->Arguments().At(0));
            extracted = true;
            elementType = DeclaringTypeArguments(*member->DeclaringType()).at(0);
            expandedArguments.push_back(TranslatedExpression(argument));
            ownedParameters_.push_back(
                std::make_shared<TS::Implementation::DefaultParameter>(elementType,
                                                           std::string()));
            expandedParameters.push_back(ownedParameters_.back().get());
        }
    }
    else if (auto* arrayCreate = dynamic_cast<const Sem::ArrayCreateResolveResult*>(rr))
    {
        const auto* arrayType =
            dynamic_cast<const TS::ArrayType*>(arrayCreate->TypePtr().get());
        if (arrayType == nullptr || arrayCreate->SizeArguments().size() != 1)
            return false;
        const std::int32_t* arrayLength = std::any_cast<std::int32_t>(
            &arrayCreate->SizeArguments()[0]->ConstantValue());
        if (arrayLength == nullptr)
            return false;
        elementType = arrayType->Element();
        auto* ace =
            dynamic_cast<Syntax::ArrayCreateExpression*>(paramsArgument.Expression());
        assert(ace != nullptr);
        Syntax::ArrayInitializerExpression* initializer = ace->Initializer();
        if (initializer != nullptr)
        {
            for (int i = 0; i < initializer->Elements().Count(); i++)
                expandedArguments.push_back(
                    TranslatedExpression(initializer->Elements().At(i)));
        }
        for (int i = 0; i < *arrayLength; i++)
        {
            ownedParameters_.push_back(
                std::make_shared<TS::Implementation::DefaultParameter>(elementType,
                                                           std::string()));
            expandedParameters.push_back(ownedParameters_.back().get());
            if (static_cast<int>(expandedArguments.size()) <= i)
            {
                auto defaultExpr =
                    expressionBuilder_->GetDefaultValueExpression(
                        const_cast<TS::IType&>(*elementType));
                expandedArguments.push_back(WithoutILInstruction(defaultExpr));
            }
        }
        extracted = true;
    }
    if (!extracted)
        return false;

    // The C# expansion arm prepends the already-translated arguments, then validates
    // the expanded form through IsUnambiguousCall.
    expandedParameters.insert(expandedParameters.begin(), expectedParameters.begin(),
                              expectedParameters.end());
    expandedArguments.insert(expandedArguments.begin(), arguments.begin(),
                             arguments.end());
    std::vector<std::shared_ptr<Sem::ResolveResult>> expandedRRs;
    expandedRRs.reserve(expandedArguments.size());
    for (const TranslatedExpression& a : expandedArguments)
        expandedRRs.push_back(SharedResolveResultAnnotation(*a.Expression()));
    const TS::IParameterizedMember* foundMember = nullptr;
    bool bestCandidateIsExpandedForm = false;
    if (IsUnambiguousCall(expectedTargetDetails, method, targetResolveResult, {},
                          expandedRRs, std::nullopt, /*firstOptionalArgumentIndex=*/-1,
                          foundMember, bestCandidateIsExpandedForm)
            != Resolver::OverloadResolutionErrors::None
        || !bestCandidateIsExpandedForm)
    {
        return false;
    }
    expectedParameters = expandedParameters;
    arguments.reserve(expandedArguments.size());
    for (TranslatedExpression& a : expandedArguments)
        arguments.push_back(TranslatedExpression(Syntax::Detach(a.Expression()),
                                                 a.ResolveResult()));
    return true;
}

bool CallBuilder::IsOptionalArgument(const TS::IParameter& parameter,
                                     const TranslatedExpression& arg)
{
    if (!parameter.IsOptional())
        return false;

    if (!arg.ResolveResult()->IsCompileTimeConstant())
    {
        const Sem::ConversionResolveResult* crr =
            dynamic_cast<const Sem::ConversionResolveResult*>(arg.ResolveResult());
        if (crr == nullptr || !crr->ConversionProperty()->IsNullLiteralConversion())
            return false;
    }
    for (const TS::IAttribute* a : parameter.GetAttributes())
    {
        if (TS::IsKnownType(a->AttributeType(), TS::KnownAttribute::CallerMemberName)
            || TS::IsKnownType(a->AttributeType(), TS::KnownAttribute::CallerFilePath)
            || TS::IsKnownType(a->AttributeType(), TS::KnownAttribute::CallerLineNumber))
            return false;
    }
    return EqualsBoxedValues(parameter.GetConstantValue(),
                             arg.ResolveResult()->ConstantValue());
}

// The C# `method.TypeArguments.Any(a => a.ContainsAnonymousType())` predicate
// the RequireTypeArguments block reads (CallBuilder.cs line 1205): the port
// helper over the argument snapshot. Defined at the file's anonymous
// namespace head so the GetRequiredTransformationsForCall body resolves it.
// The C# `private CallTransformation GetRequiredTransformationsForCall(...)`
// (CallBuilder.cs lines 1152-1343): the overload-resolution driver. The
// C# `goto case` chain in the fix switch ports as the if-ladder below: the
// TypeInferenceFailed arm re-runs the WrongNumberOfTypeArguments body when
// RequireTypeArguments is allowed and falls to the default arm otherwise (the
// C# `goto case` / `goto default` pair), and every case arm that does not
// jump ends with the C# `continue` (the next loop iteration).
CallBuilder::CallTransformation CallBuilder::GetRequiredTransformationsForCall(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
    TranslatedExpression& target, ArgumentList& argumentList,
    CallTransformation allowedTransforms,
    const TS::IParameterizedMember*& foundMethod)
{
    CallTransformation transform = CallTransformation::None;

    // initialize requireTarget flag
    bool requireTarget;
    const Sem::ResolveResult* targetResolveResult;
    if ((allowedTransforms & CallTransformation::RequireTarget)
        != CallTransformation::None)
    {
        if (settings_->AlwaysQualifyMemberReferences()
            || expressionBuilder_->HidesVariableWithName(method.Name()))
        {
            requireTarget = true;
        }
        else
        {
            if (method.IsLocalFunction())
                requireTarget = false;
            else if (method.IsStatic())
                requireTarget =
                    !expressionBuilder_->IsCurrentOrContainingType(
                        method.DeclaringTypeDefinition())
                    || method.Name() == ".cctor";
            else if (method.Name() == ".ctor")
                // always use target for base/this-ctor-call, the constructor
                // initializer pattern depends on this
                requireTarget = true;
            else if (dynamic_cast<Syntax::BaseReferenceExpression*>(
                         target.Expression())
                != nullptr)
                requireTarget = (expectedTargetDetails.CallOpCode
                                    != IL::OpCode::CallVirt
                                && method.IsVirtual());
            else
                requireTarget = dynamic_cast<Syntax::ThisReferenceExpression*>(
                                    target.Expression())
                    == nullptr;
        }
        targetResolveResult =
            requireTarget ? target.ResolveResult() : nullptr;
    }
    else
    {
        // HACK: this is a special case for collection initializer calls, they
        // do not allow a target to be emitted, but we still need it for
        // overload resolution.
        requireTarget = true;
        targetResolveResult = target.ResolveResult();
    }

    // initialize requireTypeArguments flag
    bool requireTypeArguments;
    std::vector<TS::ITypePtr> typeArguments;
    bool appliedRequireTypeArgumentsShortcut = false;
    if (!method.TypeParameters().empty()
        && (allowedTransforms & CallTransformation::RequireTypeArguments)
            != CallTransformation::None
        && !IsPossibleExtensionMethodCallOnNull(method, argumentList.Arguments))
    {
        // The ambiguity resolution below only adds type arguments as last
        // resort measure, however there are methods, such as
        // Enumerable.OfType<TResult>(IEnumerable input) that always require
        // type arguments, as those cannot be inferred from the parameters,
        // which leads to bloated expressions full of extra casts that are no
        // longer required once we add the type arguments. We lend overload
        // resolution a hand by detecting such cases beforehand and requiring
        // type arguments, if necessary.
        if (!CanInferTypeArgumentsFromArguments(method, argumentList,
                                                expressionBuilder_->typeInference))
        {
            if (settings_->AnonymousTypes()
                && AnyTypeArgumentContainsAnonymousType(method.TypeArguments())
                && PinTypesOfNullArguments(argumentList)
                && CanInferTypeArgumentsFromArguments(method, argumentList,
                                                      expressionBuilder_->typeInference))
            {
                // Anonymous types cannot be written as explicit type
                // arguments; instead the null arguments were rewritten so
                // that all type arguments are inferable.
                requireTypeArguments = false;
                typeArguments = {};
            }
            else
            {
                requireTypeArguments = true;
                typeArguments = method.TypeArguments();
                appliedRequireTypeArgumentsShortcut = true;
            }
        }
        else
        {
            requireTypeArguments = false;
            typeArguments = {};
        }
    }
    else
    {
        requireTypeArguments = false;
        typeArguments = {};
    }

    bool targetCasted = false;
    bool argumentsCasted = false;
    bool originalRequireTarget = requireTarget;
    bool skipTargetCast =
        method.Accessibility() <= TS::Accessibility::Protected
        && expressionBuilder_->IsBaseTypeOfCurrentType(
            method.DeclaringTypeDefinition());
    Resolver::OverloadResolutionErrors errors;
    bool bestCandidateIsExpandedForm;
    while ((errors = IsUnambiguousCall(
                expectedTargetDetails, method, targetResolveResult, typeArguments,
                argumentList.GetArgumentResolveResults(),
                argumentList.GetArgumentNames(),
                argumentList.FirstOptionalArgumentIndex, foundMethod,
                bestCandidateIsExpandedForm))
            != Resolver::OverloadResolutionErrors::None
        || bestCandidateIsExpandedForm != argumentList.IsExpandedForm)
    {
        bool takeDefault = false;
        if (errors == Resolver::OverloadResolutionErrors::OutVarTypeMismatch)
        {
            assert(argumentList.UseImplicitlyTypedOut);
            argumentList.UseImplicitlyTypedOut = false;
        }
        else if (errors == Resolver::OverloadResolutionErrors::TypeInferenceFailed)
        {
            if ((allowedTransforms & CallTransformation::RequireTypeArguments)
                != CallTransformation::None)
            {
                // goto case WrongNumberOfTypeArguments
                if (requireTypeArguments)
                    takeDefault = true;
                else
                {
                    requireTypeArguments = true;
                    typeArguments = method.TypeArguments();
                }
            }
            else
            {
                // goto default
                takeDefault = true;
            }
        }
        else if (errors
            == Resolver::OverloadResolutionErrors::WrongNumberOfTypeArguments)
        {
            if (requireTypeArguments)
                takeDefault = true;
            else
            {
                requireTypeArguments = true;
                typeArguments = method.TypeArguments();
            }
        }
        else if (errors
            == Resolver::OverloadResolutionErrors::MissingArgumentForRequiredParameter)
        {
            if (argumentList.FirstOptionalArgumentIndex == -1)
                takeDefault = true;
            else
                argumentList.FirstOptionalArgumentIndex = -1;
        }
        else
        {
            takeDefault = true;
        }

        if (takeDefault)
        {
            // TODO : implement some more intelligent algorithm that decides
            // which of these fixes (cast args, add target, cast target, add
            // type args) is best in this case. Additionally we should not cast
            // all arguments at once, but step-by-step try to add only a minimal
            // number of casts.
            if (argumentList.AddNamesToPrimitiveValues)
            {
                argumentList.AddNamesToPrimitiveValues = false;
            }
            else if (argumentList.FirstOptionalArgumentIndex >= 0)
            {
                argumentList.FirstOptionalArgumentIndex = -1;
            }
            else if (!argumentsCasted)
            {
                // If we added type arguments beforehand, but that didn't make
                // the code any better, undo that decision and add casts first.
                if (appliedRequireTypeArgumentsShortcut)
                {
                    requireTypeArguments = false;
                    typeArguments = {};
                    appliedRequireTypeArgumentsShortcut = false;
                }
                argumentsCasted = true;
                argumentList.UseImplicitlyTypedOut = false;
                CastArguments(argumentList.Arguments,
                              argumentList.ExpectedParameters);
            }
            else if ((allowedTransforms & CallTransformation::RequireTarget)
                    != CallTransformation::None
                && !requireTarget)
            {
                requireTarget = true;
                targetResolveResult = target.ResolveResult();
            }
            else if ((allowedTransforms & CallTransformation::RequireTarget)
                    != CallTransformation::None
                && !targetCasted)
            {
                if (skipTargetCast && requireTarget != originalRequireTarget)
                {
                    requireTarget = originalRequireTarget;
                    if (!originalRequireTarget)
                        targetResolveResult = nullptr;
                    allowedTransforms = allowedTransforms
                        & ~CallTransformation::RequireTarget;
                }
                else
                {
                    targetCasted = true;
                    target = target.ConvertTo(*method.DeclaringType(),
                                              *expressionBuilder_);
                    targetResolveResult = target.ResolveResult();
                }
            }
            else if ((allowedTransforms & CallTransformation::RequireTypeArguments)
                    != CallTransformation::None
                && !requireTypeArguments)
            {
                requireTypeArguments = true;
                typeArguments = method.TypeArguments();
            }
            else if ((allowedTransforms & CallTransformation::EnforceExplicitIn)
                != CallTransformation::None)
            {
                EnforceExplicitIn(argumentList.Arguments,
                                  argumentList.ExpectedParameters);
                allowedTransforms = allowedTransforms
                    & ~CallTransformation::EnforceExplicitIn;
            }
            else
            {
                // We've given up.
                foundMethod = &method;
                break;
            }
        }
        // Every non-give-up path ends with the C# `continue`: the next loop
        // iteration re-runs the resolution.
    }
    if ((allowedTransforms & CallTransformation::RequireTarget)
            != CallTransformation::None
        && requireTarget)
        transform = transform | CallTransformation::RequireTarget;
    if ((allowedTransforms & CallTransformation::RequireTypeArguments)
            != CallTransformation::None
        && requireTypeArguments)
        transform = transform | CallTransformation::RequireTypeArguments;
    if (argumentList.FirstOptionalArgumentIndex < 0)
        transform = transform | CallTransformation::NoOptionalArgumentAllowed;
    if (!argumentList.AddNamesToPrimitiveValues)
        transform = transform | CallTransformation::NoNamedArgsForPrettiness;
    return transform;
}

// The C# `private void EnforceExplicitIn(...)` (lines 1345-1356).
void CallBuilder::EnforceExplicitIn(
    std::vector<TranslatedExpression>& arguments,
    const std::vector<const TS::IParameter*>& expectedParameters)
{
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        if (expectedParameters[i]->ReferenceKind() != TS::ReferenceKind::In)
            continue;
        if (dynamic_cast<Syntax::DirectionExpression*>(arguments[i].Expression())
            != nullptr)
            continue;

        arguments[i] = WrapInAsRefReadOnly(arguments[i]);
        // The C# `expressionBuilder.statementBuilder.EmitAsRefReadOnly = true`
        // write is deferred with the StatementBuilder slice (the port's
        // statementBuilder field is a forward-declared placeholder; the wrap
        // itself is the observable state).
    }
}

// The C# `private TranslatedExpression WrapInAsRefReadOnly(...)` (lines
// 1357-1368): the `in ILSpyHelper_AsRefReadOnly(arg)` invocation wrapped in an
// `in` DirectionExpression over a ByReferenceResolveResult of the argument's
// type, with no IL-instruction annotations.
TranslatedExpression CallBuilder::WrapInAsRefReadOnly(const TranslatedExpression& arg)
{
    auto* invocation = new Syntax::InvocationExpression();
    invocation->Target(new Syntax::IdentifierExpression("ILSpyHelper_AsRefReadOnly"));
    invocation->Arguments().Add(arg.Expression());
    auto* direction =
        new Syntax::DirectionExpression(Syntax::FieldDirection::In, invocation);
    return WithoutILInstruction(WithRR(
        *direction,
        std::make_shared<Sem::ByReferenceResolveResult>(
            const_cast<TS::IType&>(arg.Type()).shared_from_this(),
            TS::ReferenceKind::In)));
}

// The C# `private bool IsPossibleExtensionMethodCallOnNull(...)` (lines
// 1369-1373).
bool CallBuilder::IsPossibleExtensionMethodCallOnNull(
    const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments)
{
    return method.IsExtensionMethod() && !arguments.empty()
        && dynamic_cast<Syntax::NullReferenceExpression*>(
               arguments[0].Expression())
            != nullptr;
}

// The C# `static bool CanInferTypeArgumentsFromArguments(...)` (lines
// 1374-1403).
bool CallBuilder::CanInferTypeArgumentsFromArguments(
    const TS::IMethod& method, const ArgumentList& argumentList,
    const ExpressionBuilder::TypeInferenceInstance& typeInference)
{
    if (method.TypeParameters().empty())
        return true;
    // always use unspecialized member, otherwise type inference fails
    const TS::IMethod* definition =
        dynamic_cast<const TS::IMethod*>(method.MemberDefinition());
    if (definition == nullptr)
    {
        // The C# `(IMethod)method.MemberDefinition` hard cast succeeds over a
        // FakeMethod (the single-object model); the port's two-IMember-
        // subobject hierarchy answers null on the FakeMember view, so the
        // definition is the method itself (the MemberDefinition normalization
        // convention).
        definition = &method;
    }
    std::vector<TS::ITypePtr> paramTypesInArgumentOrder;
    if (!argumentList.ArgumentToParameterMap.has_value())
    {
        for (const TS::IParameter* p : definition->Parameters())
            paramTypesInArgumentOrder.push_back(
                const_cast<TS::IType*>(&p->Type())->shared_from_this());
    }
    else
    {
        for (int index : *argumentList.ArgumentToParameterMap)
        {
            paramTypesInArgumentOrder.push_back(
                index >= 0
                    ? const_cast<TS::IType*>(
                          &definition->Parameters()[static_cast<std::size_t>(index)]
                              ->Type())
                          ->shared_from_this()
                    : TS::UnknownType());
        }
    }
    std::vector<std::shared_ptr<Sem::ResolveResult>> argumentResolveResults;
    argumentResolveResults.reserve(argumentList.Arguments.size());
    for (const TranslatedExpression& a : argumentList.Arguments)
        argumentResolveResults.push_back(
            SharedResolveResultAnnotation(*a.Expression()));
    bool success = false;
    // The C# `typeInference.InferTypeArguments(...)` runs over the TypeInference
    // object's OWN conversions (the ctor's `CSharpConversions.Get(compilation)`
    // -- NOT the resolver's cached pair), so the port passes
    // CSharpConversions::Get over the instance's compilation.
    Resolver::Detail::InferTypeArguments(
        *typeInference.compilation,
        Resolver::CSharpConversions::Get(*typeInference.compilation),
        definition->TypeParameters(), argumentResolveResults,
        paramTypesInArgumentOrder, success, std::nullopt, typeInference.algorithm);
    return success;
}

// The C# `private bool PinTypesOfNullArguments(...)` (lines 1404-1430).
bool CallBuilder::PinTypesOfNullArguments(ArgumentList& argumentList)
{
    bool anyArgumentReplaced = false;
    for (int i = 0; i < argumentList.Length(); i++)
    {
        const TS::IType& expectedType =
            argumentList.ExpectedParameters[static_cast<std::size_t>(i)]->Type();
        if (dynamic_cast<Syntax::NullReferenceExpression*>(
                argumentList.Arguments[static_cast<std::size_t>(i)].Expression())
            == nullptr)
            continue;
        if (!IsAnonymousType(&expectedType))
            continue;
        std::unique_ptr<IL::Call> newObj = NewAnonymousTypeInstance(expectedType);
        if (newObj == nullptr)
            continue;
        TranslatedExpression nullLiteral =
            argumentList.Arguments[static_cast<std::size_t>(i)];
        Syntax::Expression* detached = Syntax::Detach(nullLiteral.Expression());
        TranslatedExpression translated =
            expressionBuilder_->Translate(newObj.get(), &expectedType);
        auto* conditional = new Syntax::ConditionalExpression(
            new Syntax::PrimitiveExpression(true), detached,
            translated.Expression());
        // The C# `new ConditionalExpression(...).WithILInstruction(list)
        //        .WithRR(rr)`: the bare-expression WithILInstruction returns
        // ExpressionWithILInstruction, whose WithRR overload returns the
        // TranslatedExpression.
        argumentList.Arguments[static_cast<std::size_t>(i)] = WithRR(
            WithILInstruction(*conditional, nullLiteral.ILInstructions()),
            std::make_shared<Sem::ResolveResult>(
                const_cast<TS::IType*>(&expectedType)->shared_from_this()));
        anyArgumentReplaced = true;
    }
    return anyArgumentReplaced;
}

// The C# `private NewObj? NewAnonymousTypeInstance(IType type)` (lines
// 1431-1445): the port's Call node models newobj through its IsNewObj flag,
// so the factory builds a Call with the flag set (the C# `new NewObj(...)`
// ctor shapes the resolved method and arguments the same way).
std::unique_ptr<IL::Call> CallBuilder::NewAnonymousTypeInstance(const TS::IType& type)
{
    std::vector<const TS::IMethod*> constructors = type.GetConstructors();
    if (constructors.size() != 1)
    {
        // The C# `.Single()` over the zero / multiple-ctor shapes throws
        // InvalidOperationException (the established std::runtime_error
        // convention).
        throw std::runtime_error(constructors.empty()
                ? "Sequence contains no elements"
                : "Sequence contains more than one element.");
    }
    auto newObj = std::make_unique<IL::Call>();
    newObj->IsNewObj = true;
    // The C# `new NewObj(method)` ctor's resolved-method reference ports to the
    // Arguments walk over the raw ctor pointer below: the port's Call::Method
    // optional is the test/reader-populated handle (no shared owner exists for
    // a freshly-resolved ctor), and the only ported consumer of the built node
    // (the Translate call in PinTypesOfNullArguments) is the unported VisitCall
    // fallback anyway.
    for (const TS::IParameter* parameter : constructors[0]->Parameters())
    {
        const TS::IType& parameterType = parameter->Type();
        std::unique_ptr<IL::ILInstruction> argument;
        if (IsAnonymousType(&parameterType))
        {
            std::unique_ptr<IL::Call> nested = NewAnonymousTypeInstance(parameterType);
            argument = std::move(nested);
        }
        else if (ContainsAnonymousType(parameterType))
        {
            // A property type that involves an anonymous type other than by
            // direct nesting: the default value expression would have to name
            // it, so the C# gives up on the whole construction.
            return nullptr;
        }
        else
        {
            argument = std::make_unique<IL::DefaultValue>(
                const_cast<TS::IType&>(parameterType).shared_from_this());
        }
        newObj->AddArg(std::move(argument));
    }
    return newObj;
}

// The C# `private void CastArguments(...)` (lines 1446-1478).
void CallBuilder::CastArguments(
    std::vector<TranslatedExpression>& arguments,
    const std::vector<const TS::IParameter*>& expectedParameters)
{
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        if (settings_->AnonymousTypes()
            && ContainsAnonymousType(expectedParameters[i]->Type()))
        {
            if (auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(
                    arguments[i].Expression()))
            {
                // The C# `ModifyReturnTypeOfLambda(lambda)` arm is DEFERRED
                // with the DecompiledLambdaResolveResult slice it consumes
                // (the resolve-result cast the C# lambda body conversion
                // reads).
                (void)lambda;
                throw std::logic_error("ModifyReturnTypeOfLambda is deferred "
                                       "with the DecompiledLambdaResolveResult "
                                       "slice");
            }
        }
        else
        {
            const TS::IParameter& parameter = *expectedParameters[i];
            const TS::IType* parameterType;
            if (parameter.Type().Kind() == TS::TypeKind::Dynamic)
            {
                parameterType =
                    &expressionBuilder_->compilation->FindType(TS::KnownTypeCode::Object);
            }
            else
            {
                parameterType = &parameter.Type();
            }

            if (parameter.ReferenceKind() == TS::ReferenceKind::In
                && dynamic_cast<const TS::ByReferenceType*>(parameterType)
                    != nullptr
                && dynamic_cast<const TS::ByReferenceType*>(&arguments[i].Type())
                    == nullptr)
            {
                parameterType = static_cast<const TS::ByReferenceType*>(parameterType)
                                    ->Element()
                                    .get();
            }

            arguments[i] = arguments[i].ConvertTo(
                *const_cast<TS::IType*>(parameterType), *expressionBuilder_,
                /*checkForOverflow=*/false, /*allowImplicitConversion=*/false);
        }
    }
}

Resolver::OverloadResolutionErrors CallBuilder::IsUnambiguousCall(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
    const Sem::ResolveResult* target,
    const std::vector<TS::ITypePtr>& typeArguments,
    std::vector<std::shared_ptr<Sem::ResolveResult>> arguments,
    std::optional<std::vector<std::string>> argumentNames,
    int firstOptionalArgumentIndex, const TS::IParameterizedMember*& foundMember,
    bool& bestCandidateIsExpandedForm) const
{
    foundMember = nullptr;
    bestCandidateIsExpandedForm = false;
    const TS::ITypeDefinition* currentTypeDefinition = resolver_->CurrentTypeDefinition();
    Resolver::MemberLookup lookup(
        currentTypeDefinition,
        currentTypeDefinition != nullptr ? currentTypeDefinition->ParentModule() : nullptr);

    if (firstOptionalArgumentIndex >= 0 && argumentNames.has_value())
    {
        argumentNames = std::vector<std::string>(
            argumentNames->begin(),
            argumentNames->begin() + firstOptionalArgumentIndex);
    }

    Resolver::OverloadResolution overloadResolution(
        *typeSystem_, arguments, argumentNames, typeArguments,
        &expressionBuilder_->resolver->Conversions());
    if (expectedTargetDetails.CallOpCode == IL::OpCode::NewObj)
    {
        TS::ITypePtr declaringType = method.DeclaringType();
        for (const TS::IMethod* ctor : declaringType->GetConstructors())
        {
            bool allowProtectedAccess =
                resolver_->CurrentTypeDefinition()
                == dynamic_cast<const TS::ITypeDefinition*>(declaringType.get());
            if (lookup.IsAccessible(*ctor, allowProtectedAccess))
            {
                Resolver::OverloadResolutionErrors errors =
                    overloadResolution.AddCandidate(*ctor);
                overloadResolution.LogCandidateAddingResult("  Candidate", *ctor,
                                                            errors);
            }
        }
    }
    else if (method.IsOperator())
    {
        std::vector<std::shared_ptr<TS::IMethod>> operatorCandidates;
        if (arguments.size() == 1)
        {
            operatorCandidates =
                expressionBuilder_->resolver->GetUserDefinedOperatorCandidates(
                    TS::GetUnderlyingType(*arguments[0]->TypePtr()),
                    method.Name().c_str());
            if (method.Name() == "op_Explicit")
            {
                // For casts, also consider candidates from the target type we are
                // casting to.
                for (const auto& m :
                     expressionBuilder_->resolver->GetUserDefinedOperatorCandidates(
                         TS::GetUnderlyingType(method.ReturnType()),
                         method.Name().c_str()))
                {
                    operatorCandidates.push_back(m);
                }
            }
        }
        else if (arguments.size() == 2)
        {
            for (const auto& m :
                 expressionBuilder_->resolver->GetUserDefinedOperatorCandidates(
                     TS::GetUnderlyingType(*arguments[0]->TypePtr()),
                     method.Name().c_str()))
                operatorCandidates.push_back(m);
            for (const auto& m :
                 expressionBuilder_->resolver->GetUserDefinedOperatorCandidates(
                     TS::GetUnderlyingType(*arguments[1]->TypePtr()),
                     method.Name().c_str()))
                operatorCandidates.push_back(m);
        }
        for (const auto& m : operatorCandidates)
        {
            overloadResolution.AddCandidate(*m);
        }
    }
    else if (target == nullptr)
    {
        auto result = std::dynamic_pointer_cast<Resolver::MethodGroupResolveResult>(
            resolver_->ResolveSimpleName(method.Name(), typeArguments, true));
        if (result == nullptr)
            return Resolver::OverloadResolutionErrors::AmbiguousMatch;
        overloadResolution.AddMethodLists(result->MethodsGroupedByDeclaringType());
    }
    else
    {
        auto result = std::dynamic_pointer_cast<Resolver::MethodGroupResolveResult>(
            lookup.Lookup(*target, method.Name(), typeArguments, true));
        if (result == nullptr)
            return Resolver::OverloadResolutionErrors::AmbiguousMatch;
        overloadResolution.AddMethodLists(result->MethodsGroupedByDeclaringType());
    }
    bestCandidateIsExpandedForm = overloadResolution.BestCandidateIsExpandedForm();
    if (overloadResolution.BestCandidateErrors() != Resolver::OverloadResolutionErrors::None)
        return overloadResolution.BestCandidateErrors();
    if (overloadResolution.IsAmbiguous())
        return Resolver::OverloadResolutionErrors::AmbiguousMatch;
    foundMember = overloadResolution.GetBestCandidateWithSubstitutedTypeArguments();
    if (!IsAppropriateCallTarget(expectedTargetDetails, method, *foundMember))
        return Resolver::OverloadResolutionErrors::AmbiguousMatch;
    std::optional<std::vector<int>> map = overloadResolution.GetArgumentToParameterMap();
    for (std::size_t i = 0; i < arguments.size(); i++)
    {
        const Sem::ResolveResult* arg = arguments[i].get();
        auto* outVar = dynamic_cast<const Sem::OutVarResolveResult*>(arg);
        int parameterIndex = map.has_value() && i < map->size() ? (*map)[i] : -1;
        if (outVar != nullptr && parameterIndex >= 0)
        {
            const TS::IParameter* param =
                foundMember->Parameters()[static_cast<std::size_t>(parameterIndex)];
            const TS::IType& paramType = TS::UnwrapByRef(param->Type());
            const TS::ITypePtr& originalType = outVar->OriginalVariableType();
            if (!paramType.Equals(*originalType))
                return Resolver::OverloadResolutionErrors::OutVarTypeMismatch;
        }
    }

    return Resolver::OverloadResolutionErrors::None;
}

bool CallBuilder::IsAppropriateCallTarget(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMember& expectedTarget,
    const TS::IMember& actualTarget) const
{
    // The C# `expectedTarget.Equals(actualTarget, ...)` is object identity plus the
    // type-erased structural comparison; the port normalizes both sides through
    // MemberDefinition() first (the canonical member view the two-IMember-subobject
    // hierarchy needs -- the OverloadResolutionHelpers convention).
    if (expectedTarget.MemberDefinition()->Equals(
            actualTarget.MemberDefinition(), &TS::NormalizeTypeVisitor::TypeErasure()))
        return true;

    if (expectedTargetDetails.CallOpCode == IL::OpCode::CallVirt
        && actualTarget.IsOverride())
    {
        if (expectedTargetDetails.NeedsBoxingConversion
            && actualTarget.DeclaringType() != nullptr
            && actualTarget.DeclaringType()->IsReferenceType()
                != std::optional<bool>(true))
            return false;
        for (const TS::IMember* possibleTarget :
             TS::InheritanceHelper::GetBaseMembers(actualTarget, false))
        {
            if (expectedTarget.MemberDefinition()->Equals(
                    possibleTarget->MemberDefinition(),
                    &TS::NormalizeTypeVisitor::TypeErasure()))
                return true;
            if (!possibleTarget->IsOverride())
                break;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------

// The ArgumentList helpers

std::optional<std::vector<std::string>> ArgumentList::GetArgumentNames(int skipCount)
{
    // The C# `argumentNames` local ALIASES the field's array when one exists (a
    // reference-type copy), so the primitive fills mutate the stored array in
    // place; when the field is null the fills land on a fresh local (never
    // stored back). The port reproduces the aliasing through the field itself.
    bool aliasesField = ArgumentNames.has_value();
    std::optional<std::vector<std::string>> freshNames;
    if (AddNamesToPrimitiveValues && IsPrimitiveValue.Any() && !IsExpandedForm
        && std::none_of(ParameterNames.begin(), ParameterNames.end(),
                        [](const std::string& n) { return n.empty(); }))
    {
        assert(skipCount == 0);
        if (!aliasesField)
        {
            freshNames = std::vector<std::string>(Arguments.size());
            for (int i = 0; i < static_cast<int>(Arguments.size()); i++)
            {
                if (IsPrimitiveValue[i] && (*freshNames)[i].empty())
                {
                    (*freshNames)[i] = ParameterNames[i];
                }
            }
            return freshNames;
        }
        for (int i = 0; i < static_cast<int>(Arguments.size()); i++)
        {
            if (IsPrimitiveValue[i] && (*ArgumentNames)[i].empty())
            {
                (*ArgumentNames)[i] = ParameterNames[i];
            }
        }
        return ArgumentNames;
    }

    return ArgumentNames;
}

std::vector<std::shared_ptr<Sem::ResolveResult>> ArgumentList::GetArgumentResolveResults(
    int skipCount)
{
    const auto& expectedParameters = ExpectedParameters;
    bool useImplicitlyTypedOut = UseImplicitlyTypedOut;

    std::vector<std::shared_ptr<Sem::ResolveResult>> results;
    results.reserve(Arguments.size());
    for (int i = 0; i < static_cast<int>(Arguments.size()); i++)
    {
        const TranslatedExpression& expression = Arguments[i];
        const TS::IParameter* param = expectedParameters[i];
        if (useImplicitlyTypedOut && param != nullptr
            && param->ReferenceKind() == TS::ReferenceKind::Out)
        {
            auto* brt = dynamic_cast<const TS::ByReferenceType*>(&expression.Type());
            if (brt != nullptr)
            {
                results.push_back(std::make_shared<Sem::OutVarResolveResult>(
                    brt->Element()));
                continue;
            }
        }
        results.push_back(SharedResolveResultAnnotation(*expression.Expression()));
    }

    int actualCount = GetActualArgumentCount();
    auto begin = results.begin() + std::min(skipCount, static_cast<int>(results.size()));
    auto end = begin + std::max(0, actualCount);
    if (end > results.end())
        end = results.end();
    return std::vector<std::shared_ptr<Sem::ResolveResult>>(begin, end);
}

std::vector<std::shared_ptr<Sem::ResolveResult>>
ArgumentList::GetArgumentResolveResultsDirect(int skipCount)
{
    std::vector<std::shared_ptr<Sem::ResolveResult>> results;
    results.reserve(Arguments.size());
    for (const TranslatedExpression& a : Arguments)
        results.push_back(SharedResolveResultAnnotation(*a.Expression()));

    int actualCount = GetActualArgumentCount();
    auto begin = results.begin() + std::min(skipCount, static_cast<int>(results.size()));
    auto end = begin + std::max(0, actualCount);
    if (end > results.end())
        end = results.end();
    return std::vector<std::shared_ptr<Sem::ResolveResult>>(begin, end);
}

std::vector<Syntax::Expression*> ArgumentList::GetArgumentExpressions(int skipCount)
{
    std::optional<std::vector<std::string>> argumentNames = GetArgumentNames(skipCount);
    int argumentCount = GetActualArgumentCount();
    bool useImplicitlyTypedOut = UseImplicitlyTypedOut;
    // The C# unnamed arm skips from `skipCount`; the named arm always starts at
    // zero (its Debug.Assert(skipCount == 0) documents the caller contract).
    int start = argumentNames.has_value() ? 0 : std::min(skipCount, argumentCount);

    std::vector<Syntax::Expression*> expressions;
    expressions.reserve(argumentCount);
    for (int i = start; i < argumentCount; i++)
    {
        Syntax::Expression* expression = Arguments[i].Expression();
        if (argumentNames.has_value())
        {
            const std::string& name = (*argumentNames)[i];
            if (!name.empty())
            {
                expressions.push_back(
                    new Syntax::NamedArgumentExpression(name, expression));
                continue;
            }
        }
        // The C# `AddAnnotations` local (the UseImplicitlyTypedOut rule).
        if (useImplicitlyTypedOut)
        {
            auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(
                GetResolveResult(*expression));
            if (brrr != nullptr
                && brrr->ReferenceKind() == TS::ReferenceKind::Out)
            {
                expression->AddAnnotation(
                    UseImplicitlyTypedOutAnnotation::SharedInstance());
            }
        }
        expressions.push_back(expression);
    }
    return expressions;
}

bool ArgumentList::CanInferAnonymousTypePropertyNamesFromArguments() const
{
    for (int i = 0; i < static_cast<int>(Arguments.size()); i++)
    {
        std::optional<std::string> inferredName;
        if (auto* identifier = dynamic_cast<const Syntax::IdentifierExpression*>(
                Arguments[i].Expression()))
        {
            inferredName = identifier->Identifier();
        }
        else if (auto* member = dynamic_cast<const Syntax::MemberReferenceExpression*>(
                     Arguments[i].Expression()))
        {
            inferredName = member->MemberName();
        }

        if (!inferredName.has_value()
            || *inferredName != ExpectedParameters[i]->Name())
        {
            return false;
        }
    }
    return true;
}

void ArgumentList::CheckNoNamedOrOptionalArguments() const
{
    assert(!ArgumentToParameterMap.has_value() && !ArgumentNames.has_value()
           && FirstOptionalArgumentIndex < 0);
}

} // namespace ILSpy::Decompiler::CSharp
