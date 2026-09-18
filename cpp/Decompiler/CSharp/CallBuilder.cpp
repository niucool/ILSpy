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
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
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
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TupleExpression.hpp"
#include "Decompiler/Semantics/TupleResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/InitializedObjectResolveResult.hpp"
#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/IL/Instructions/LdObjIfRef.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/OpCodeName.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/Implementation/SyntheticRangeIndexer.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
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

// The non-owning shared alias over a raw resolve-result pointer (the
// KnownTypeCache convention (d) no-op-deleter alias): the C# GC roots the
// caller's ResolveResult through its own graph, so the alias borrows it for
// the CSharpInvocationResolveResult ctor that stores the shared handle.
std::shared_ptr<Sem::ResolveResult> AliasResolveResult(
    const Sem::ResolveResult* target)
{
    if (target == nullptr)
        return nullptr;
    return std::shared_ptr<Sem::ResolveResult>(
        const_cast<Sem::ResolveResult*>(target), [](Sem::ResolveResult*) {});
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

// The C# `string.Split(char[] separators, int count)` for the token shapes:
// split at the first `count - 1` separator occurrences, the last piece
// carrying the remainder verbatim (including further separators); a string
// with no separator is one piece.
std::vector<std::string> SplitWithCount(const std::string& value,
                                        const std::string& separators, int count)
{
    std::vector<std::string> pieces;
    std::size_t start = 0;
    while (static_cast<int>(pieces.size()) + 1 < count)
    {
        std::size_t next = std::string::npos;
        for (char sep : separators)
        {
            std::size_t at = value.find(sep, start);
            if (at != std::string::npos && (next == std::string::npos || at < next))
                next = at;
        }
        if (next == std::string::npos)
            break;
        pieces.push_back(value.substr(start, next - start));
        start = next + 1;
    }
    pieces.push_back(value.substr(start));
    return pieces;
}

// The .NET `int.TryParse(string, out int)` over NumberStyles.Integer and the
// invariant culture: leading/trailing whitespace (the char.IsWhiteSpace units
// -- the Util::IsWhiteSpace table over UTF-16 code units), an optional leading
// sign, one or more decimal digits, and the int32 magnitude gate. A failing
// parse leaves `result` untouched (the .NET out-parameter 0 assignment is
// unobservable through the call sites -- every failure rejects the token).
bool TryParseInt32(const std::string& text, std::int32_t& result)
{
    std::u16string u16 = Util::Utf8ToUtf16(text);
    std::size_t begin = 0;
    std::size_t end = u16.size();
    while (begin < end && Util::IsWhiteSpace(u16[begin]))
        begin++;
    while (end > begin && Util::IsWhiteSpace(u16[end - 1]))
        end--;
    bool negative = false;
    if (begin < end && (u16[begin] == u'+' || u16[begin] == u'-'))
    {
        negative = u16[begin] == u'-';
        begin++;
    }
    if (begin == end)
        return false;
    std::int64_t value = 0;
    for (std::size_t i = begin; i < end; i++)
    {
        char16_t c = u16[i];
        if (c < u'0' || c > u'9')
            return false;
        value = value * 10 + (c - '0');
        if (value > (negative ? INT64_C(2147483648) : INT64_C(2147483647)))
            return false;
    }
    result = negative ? static_cast<std::int32_t>(-value) : static_cast<std::int32_t>(value);
    return true;
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
                ModifyReturnTypeOfLambda(*lambda);
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

// The C# `private void ModifyReturnTypeOfLambda(LambdaExpression lambda)` (lines 1485-1493).
void CallBuilder::ModifyReturnTypeOfLambda(Syntax::LambdaExpression& lambda)
{
    // The C# `(DecompiledLambdaResolveResult)lambda.GetResolveResult()` -- an
    // unchecked cast that throws InvalidCastException on any other resolve result.
    // The lambda translation attaches this annotation; anything else is an internal
    // invariant violation, so the port fails loudly.
    auto* resolveResult = dynamic_cast<Resolver::DecompiledLambdaResolveResult*>(
        const_cast<Sem::ResolveResult*>(GetResolveResult(lambda)));
    if (resolveResult == nullptr)
        throw std::logic_error(
            "ModifyReturnTypeOfLambda: the lambda has no "
            "DecompiledLambdaResolveResult annotation");

    // The C# `resolveResult.ReturnType` and the `InferredReturnType` assignment share the
    // same IType instance; `ReturnType()` returns a const reference, so the shared handle
    // comes off the const-cast (the node owns it).
    TS::IType& returnType = const_cast<TS::IType&>(resolveResult->ReturnType());
    if (auto* exprBody = dynamic_cast<Syntax::Expression*>(lambda.Body()))
    {
        TranslatedExpression converted =
            TranslatedExpression(Syntax::Detach(exprBody))
                .ConvertTo(returnType, *expressionBuilder_);
        lambda.Body(converted.Expression());
    }
    else
    {
        // The C# passes the LAMBDA (not its body) so the walk starts at the lambda's
        // children; the block body is just its first recursed child.
        ModifyReturnStatementInsideLambda(returnType, lambda);
    }
    resolveResult->InferredReturnType = returnType.shared_from_this();
}

// The C# `private void ModifyReturnStatementInsideLambda(IType returnType, AstNode
// parent)` (lines 1495-1509).
void CallBuilder::ModifyReturnStatementInsideLambda(const TS::IType& returnType,
                                                    Syntax::AstNode& parent)
{
    TS::IType& targetType = const_cast<TS::IType&>(returnType);
    for (Syntax::AstNode* child : parent.Children())
    {
        if (dynamic_cast<Syntax::LambdaExpression*>(child) != nullptr
            || dynamic_cast<Syntax::AnonymousMethodExpression*>(child) != nullptr)
            continue; // a nested function's returns belong to that function
        if (auto* ret = dynamic_cast<Syntax::ReturnStatement*>(child))
        {
            if (ret->Expression() != nullptr)
            {
                TranslatedExpression converted =
                    TranslatedExpression(Syntax::Detach(ret->Expression()))
                        .ConvertTo(targetType, *expressionBuilder_);
                ret->Expression(converted.Expression());
            }
            continue;
        }
        ModifyReturnStatementInsideLambda(returnType, *child);
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
    if (!IsAppropriateCallTarget(expectedTargetDetails, method, foundMember))
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
    const TS::IMember* actualTarget) const
{
    // The C# `expectedTarget.Equals(actualTarget, ...)` is object identity plus the
    // type-erased structural comparison; the port normalizes both sides through
    // MemberDefinition() first (the canonical member view the two-IMember-subobject
    // hierarchy needs -- the OverloadResolutionHelpers convention). A null
    // actualTarget answers false (the C# object-Equals reference-equality with null).
    if (actualTarget != nullptr
        && expectedTarget.MemberDefinition()->Equals(actualTarget->MemberDefinition(),
            &TS::NormalizeTypeVisitor::TypeErasure()))
        return true;

    if (expectedTargetDetails.CallOpCode == IL::OpCode::CallVirt
        && actualTarget->IsOverride())
    {
        if (expectedTargetDetails.NeedsBoxingConversion
            && actualTarget->DeclaringType() != nullptr
            && actualTarget->DeclaringType()->IsReferenceType()
                != std::optional<bool>(true))
            return false;
        for (const TS::IMember* possibleTarget :
             TS::InheritanceHelper::GetBaseMembers(*actualTarget, false))
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

bool CallBuilder::IsUnambiguousAccess(
    const ExpectedTargetDetails& expectedTargetDetails, const Sem::ResolveResult* target,
    const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments,
    const std::optional<std::vector<std::string>>& argumentNames,
    const TS::IMember*& foundMember) const
{
    foundMember = nullptr;
    if (target == nullptr)
    {
        auto lookupResult = resolver_->ResolveSimpleName(method.AccessorOwner()->Name(),
                                                         {}, false);
        auto* result = dynamic_cast<const Sem::MemberResolveResult*>(lookupResult.get());
        if (result == nullptr || result->IsError())
            return false;
        foundMember = result->Member();
    }
    else
    {
        const TS::ITypeDefinition* currentTypeDefinition = resolver_->CurrentTypeDefinition();
        Resolver::MemberLookup lookup(
            currentTypeDefinition,
            currentTypeDefinition != nullptr ? currentTypeDefinition->ParentModule()
                                             : nullptr);
        if (method.AccessorOwner()->SymbolKind() == TS::SymbolKind::Indexer)
        {
            std::vector<std::shared_ptr<Sem::ResolveResult>> argumentResolveResults;
            argumentResolveResults.reserve(arguments.size());
            for (const TranslatedExpression& a : arguments)
                argumentResolveResults.push_back(SharedResolveResultAnnotation(*a.Expression()));
            Resolver::OverloadResolution or_(resolver_->Compilation(),
                std::move(argumentResolveResults), argumentNames,
                std::vector<TS::ITypePtr>{},
                &expressionBuilder_->resolver->Conversions());
            or_.AddMethodLists(lookup.LookupIndexers(*target));
            if (or_.BestCandidateErrors() != Resolver::OverloadResolutionErrors::None)
                return false;
            if (or_.IsAmbiguous())
                return false;
            foundMember = or_.GetBestCandidateWithSubstitutedTypeArguments();
        }
        else
        {
            auto lookupResult =
                lookup.Lookup(*target, method.AccessorOwner()->Name(), {}, false);
            auto* result = dynamic_cast<const Sem::MemberResolveResult*>(lookupResult.get());
            if (result == nullptr || result->IsError())
                return false;
            foundMember = result->Member();
        }
    }
    return foundMember != nullptr
        && IsAppropriateCallTarget(expectedTargetDetails, *method.AccessorOwner(),
                                   foundMember);
}

ExpressionWithResolveResult CallBuilder::HandleAccessorCall(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
    TranslatedExpression target, std::vector<TranslatedExpression> arguments,
    std::optional<std::vector<std::string>> argumentNames)
{
    bool requireTarget;
    if (settings_->AlwaysQualifyMemberReferences()
        || method.AccessorOwner()->SymbolKind() == TS::SymbolKind::Indexer
        || expressionBuilder_->HidesVariableWithName(method.AccessorOwner()->Name()))
        requireTarget = true;
    else if (method.IsStatic())
        requireTarget =
            !expressionBuilder_->IsCurrentOrContainingType(method.DeclaringTypeDefinition());
    else
        requireTarget =
            dynamic_cast<const Syntax::ThisReferenceExpression*>(target.Expression())
            == nullptr;
    bool targetCasted = false;
    bool isSetter = TS::IsKnownType(method.ReturnType(), TS::KnownTypeCode::Void);
    bool argumentsCasted =
        (isSetter && method.Parameters().size() == 1)
        || (!isSetter && method.Parameters().empty());
    const Sem::ResolveResult* targetResolveResult =
        requireTarget ? target.ResolveResult() : nullptr;

    TranslatedExpression value;
    if (isSetter)
    {
        // The C# `value = arguments.Last(); arguments.Remove(value);` -- Last() on an
        // empty list throws the .NET InvalidOperationException; the value's own
        // expression (possibly null for the empty-list shape the LINQ throw
        // prevents) reaches the AssignmentExpression's Right slot unchanged.
        if (arguments.empty())
            throw std::runtime_error("Sequence contains no elements.");
        value = arguments.back();
        arguments.pop_back();
    }

    const TS::IMember* foundMember;
    while (!IsUnambiguousAccess(expectedTargetDetails, targetResolveResult, method,
                                arguments, argumentNames, foundMember))
    {
        if (!argumentsCasted)
        {
            argumentsCasted = true;
            CastArguments(arguments, method.Parameters());
        }
        else if (!requireTarget)
        {
            requireTarget = true;
            targetResolveResult = target.ResolveResult();
        }
        else if (!targetCasted)
        {
            targetCasted = true;
            target = target.ConvertTo(*method.AccessorOwner()->DeclaringType(),
                                      *expressionBuilder_);
            targetResolveResult = target.ResolveResult();
        }
        else
        {
            foundMember = method.AccessorOwner();
            break;
        }
    }

    auto rr = std::make_shared<Sem::MemberResolveResult>(
        SharedResolveResultAnnotation(*target.Expression()), foundMember);

    if (isSetter)
    {
        Syntax::Expression* expr;

        if (!arguments.empty())
        {
            auto* indexer = new Syntax::IndexerExpression(
                dynamic_cast<const Sem::InitializedObjectResolveResult*>(
                    target.ResolveResult())
                    != nullptr
                    ? nullptr
                    : target.Expression());
            for (const TranslatedExpression& a : arguments)
                indexer->Arguments().Add(a.Expression());
            expr = WithoutILInstruction(WithRR(*indexer, rr)).Expression();
        }
        else if (requireTarget)
        {
            auto* mre = new Syntax::MemberReferenceExpression(
                target.Expression(), method.AccessorOwner()->Name());
            expr = WithoutILInstruction(WithRR(*mre, rr)).Expression();
        }
        else
        {
            auto* ide = new Syntax::IdentifierExpression(method.AccessorOwner()->Name());
            expr = WithoutILInstruction(WithRR(*ide, rr)).Expression();
        }

        Syntax::AssignmentOperatorType op = Syntax::AssignmentOperatorType::Assign;
        const TS::IEvent* parentEvent =
            dynamic_cast<const TS::IEvent*>(method.AccessorOwner());
        if (parentEvent != nullptr)
        {
            // The C# `method.Equals(parentEvent.AddAccessor)` binds to
            // object.Equals (reference equality -- no single-arg Equals overload
            // exists on ISymbol/IMember), so the port compares the canonical
            // member views (the two-IMember-subobject convention).
            const TS::IMethod* addAccessor = parentEvent->AddAccessor();
            if (addAccessor != nullptr
                && method.MemberDefinition() == addAccessor->MemberDefinition())
                op = Syntax::AssignmentOperatorType::Add;
            const TS::IMethod* removeAccessor = parentEvent->RemoveAccessor();
            if (removeAccessor != nullptr
                && method.MemberDefinition() == removeAccessor->MemberDefinition())
                op = Syntax::AssignmentOperatorType::Subtract;
        }
        return WithRR(
            *new Syntax::AssignmentExpression(expr, op, value.Expression()),
            std::make_shared<Sem::TypeResolveResult>(
                const_cast<TS::IType&>(method.AccessorOwner()->ReturnType())
                    .shared_from_this()));
    }
    else
    {
        if (!arguments.empty())
        {
            auto* indexer = new Syntax::IndexerExpression(target.Expression());
            for (const TranslatedExpression& a : arguments)
                indexer->Arguments().Add(a.Expression());
            return WithRR(*indexer, rr);
        }
        else if (requireTarget)
        {
            auto* mre = new Syntax::MemberReferenceExpression(
                target.Expression(), method.AccessorOwner()->Name());
            return WithRR(*mre, rr);
        }
        else
        {
            auto* ide = new Syntax::IdentifierExpression(method.AccessorOwner()->Name());
            return WithRR(*ide, rr);
        }
    }
}

// ---------------------------------------------------------------------------

// The call-build composition (CallBuilder.cs lines 202-594)

namespace {

// The C# `IL.Transforms.TupleTransform.MatchTupleConstruction(inst as NewObj, out
// var tupleElements)` over the port's one-Call-node model (the C# `inst as NewObj`
// nulls for a non-newobj call, which fails the match): the ValueTuple newobj
// element flattening -- 'newobj TupleType(...)' with the 8-ary Rest nesting. The
// C# `TupleType.IsTupleCompatible(newobj.Method.DeclaringType, out int
// elementCount)` drives the shape; the port's IsTupleCompatible free function is
// the same helper. The C# `newobj.Arguments.Last() as NewObj` ports to a
// dynamic_cast over the last child Call (the port's one-Call-node model -- every
// child is a Call node, its IsNewObj flag the C# type test).
bool MatchTupleConstruction(const IL::Call* inst,
                            std::vector<IL::ILInstruction*>& arguments)
{
    arguments.clear();
    if (inst == nullptr || !inst->IsNewObj)
        return false;
    int elementCount = 0;
    if (!TS::IsTupleCompatible(*inst->Method->DeclaringType(), elementCount))
        return false;
    int outIndex = 0;
    while (elementCount >= TS::TupleRestPosition)
    {
        if (inst->Arguments.size() != TS::TupleRestPosition)
            return false;
        for (int pos = 1; pos < TS::TupleRestPosition; pos++)
        {
            arguments.push_back(inst->Arguments[static_cast<std::size_t>(pos - 1)].get());
            outIndex++;
        }
        elementCount -= TS::TupleRestPosition - 1;
        const IL::Call* rest =
            dynamic_cast<const IL::Call*>(inst->Arguments.back().get());
        if (rest == nullptr || !rest->IsNewObj)
            return false;
        inst = rest;
        int restElementCount = 0;
        if (!TS::IsTupleCompatible(*inst->Method->DeclaringType(), restElementCount))
            return false;
        if (restElementCount != elementCount)
            return false;
    }
    if (inst->Arguments.size() != static_cast<std::size_t>(elementCount))
        return false;
    for (int i = 0; i < elementCount; i++)
    {
        arguments.push_back(inst->Arguments[static_cast<std::size_t>(i)].get());
        outIndex++;
    }
    return true;
}

// The C# `new PrimitiveExpression(true)` is a std::make_shared over the bool value.
Syntax::Expression* TruePrimitive()
{
    return new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(true));
}

// The children of the Call node as raw pointers (the C# `inst.Arguments` is an
// IReadOnlyList<ILInstruction> of the same children).
std::vector<IL::ILInstruction*> CallChildPtrs(const IL::Call& call)
{
    std::vector<IL::ILInstruction*> children;
    children.reserve(call.Arguments.size());
    for (const std::unique_ptr<IL::ILInstruction>& child : call.Arguments)
        children.push_back(child.get());
    return children;
}

} // namespace

// The C# `public TranslatedExpression Build(CallInstruction inst, IType? typeHint
// = null)` (CallBuilder.cs lines 202-241).
TranslatedExpression CallBuilder::Build(const IL::Call& inst, const TS::IType* typeHint)
{
    if (inst.IsNewObj)
    {
        IL::DelegateConstructionMatch delegateMatch;
        if (IL::DelegateConstruction::MatchDelegateConstruction(
                const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)),
                delegateMatch))
        {
            // The C# `return HandleDelegateConstruction(newobj)` -- the
            // render already carries the .WithILInstruction(inst) (the
            // C# HandleDelegateConstruction applies it on the object create),
            // so the entry must not double-annotate.
            return HandleDelegateConstruction(inst);
        }
    }
    if (settings_->TupleTypes())
    {
        std::vector<IL::ILInstruction*> tupleElements;
        if (MatchTupleConstruction(&inst, tupleElements) && tupleElements.size() >= 2)
        {
            // The C# tuple-expression render (CallBuilder.cs lines 209-240): the
            // TupleExpression + TupleResolveResult arm.
            auto elementTypes = TS::GetTupleElementTypes(*inst.Method->DeclaringType());
            // C# `typeHint is TupleType tt ? tt.ElementNames : default`.
            std::optional<std::vector<std::string>> elementNames;
            if (const auto* tupleHint = dynamic_cast<const TS::TupleType*>(typeHint))
                elementNames = tupleHint->ElementNames();
            // The C# `Debug.Assert(!elementTypes.IsDefault, ...)` and
            // `Debug.Assert(elementTypes.Length == tupleElements.Length)` are
            // compiled out of the release engine; the port keeps them as assert().
            assert(elementTypes.has_value());
            assert(elementTypes->size() == tupleElements.size());
            auto* tuple = new Syntax::TupleExpression();
            std::vector<std::shared_ptr<Sem::ResolveResult>> elementRRs;
            for (std::size_t index = 0; index < tupleElements.size(); index++)
            {
                // ConvertTo takes a NON-CONST IType& (the ChangeNullability shared_from_this
                // convention), so the element handle derefs non-const; the visitor never
                // mutates the hint type.
                TS::IType& elementType = *(*elementTypes)[index];
                TranslatedExpression translatedElement =
                    expressionBuilder_->Translate(tupleElements[index], &elementType)
                        .ConvertTo(elementType, *expressionBuilder_,
                                   /*checkForOverflow=*/false,
                                   /*allowImplicitConversion=*/true);
                // C# `elementNames.IsDefaultOrEmpty || elementNames.
                // ElementAtOrDefault(index) is not string { Length: > 0 }` -- a null
                // OR empty name renders the bare element (the port's ElementNames
                // maps the C# null entries to empty strings, so one check covers
                // both).
                const bool hasName = elementNames.has_value() && !elementNames->empty()
                    && index < elementNames->size() && !(*elementNames)[index].empty();
                if (!hasName)
                {
                    tuple->Elements().Add(translatedElement.Expression());
                }
                else
                {
                    tuple->Elements().Add(new Syntax::NamedArgumentExpression(
                        (*elementNames)[index], translatedElement.Expression()));
                }
                // C# `elementRRs.Add(translatedElement.ResolveResult)` -- the C#
                // GC references the element resolve result; the port needs the
                // OWNING shared handle off the node's annotation channel (the
                // UnknownError fallback path leaves no annotation, so the alias
                // over the raw pointer covers that shape).
                std::shared_ptr<Sem::ResolveResult> elementRR =
                    SharedResolveResultAnnotation(*translatedElement.Expression());
                if (!elementRR)
                    elementRR = AliasResolveResult(translatedElement.ResolveResult());
                elementRRs.push_back(std::move(elementRR));
            }
            // C# `inst.Method.DeclaringType.GetDefinition()?.ParentModule`.
            const TS::ITypeDefinition* valueTupleDef =
                inst.Method->DeclaringType()->GetDefinition();
            const TS::IModule* valueTupleAssembly =
                valueTupleDef != nullptr ? valueTupleDef->ParentModule() : nullptr;
            return WithILInstruction(
                WithRR(*tuple, std::make_shared<Sem::TupleResolveResult>(
                                  *expressionBuilder_->compilation,
                                  std::move(elementRRs), std::move(elementNames),
                                  valueTupleAssembly)),
                const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
        }
    }
    {
        std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>> operands;
        if (settings_->StringConcat() && IsSpanBasedStringConcat(inst, operands))
        {
            return WithILInstruction(BuildStringConcat(*inst.Method, *operands),
                const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
        }
    }
    // The C# `inst.OpCode` -- the port's one-Call-node model hardcodes `Op ==
    // Call`; the decoded opcode is the IsNewObj flag (the call-vs-callvirt
    // distinction is deferred with the reader's decode, the documented
    // ExpectedTargetDetails convention).
    IL::OpCode instOpCode = inst.IsNewObj ? IL::OpCode::NewObj : IL::OpCode::Call;
    const TS::IType* constrained = inst.ConstrainedTo ? inst.ConstrainedTo.get() : nullptr;
    TranslatedExpression result = WithILInstruction(
        Build(instOpCode, *inst.Method, CallChildPtrs(inst), std::nullopt, constrained),
        const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
    if (inst.IsTail)
    {
        // Surface the IL 'tail.' prefix as an inline marker, e.g. '/*tail.*/Callee(x)'.
        // F# emits tail calls pervasively, and the prefix is otherwise dropped entirely.
        result.Expression()->AddLeadingTrivia(new Syntax::Comment(
            std::string("tail."), Syntax::CommentType::MultiLine));
    }
    return result;
}

// The C# `static bool IsNullConditional(Expression expr)` (CallBuilder.cs lines
// 1480-1485).
bool CallBuilder::IsNullConditional(const Syntax::Expression* expr)
{
    const auto* uoe = dynamic_cast<const Syntax::UnaryOperatorExpression*>(expr);
    return uoe != nullptr
        && uoe->Operator() == Syntax::UnaryOperatorType::NullConditional;
}

// The C# `private bool IsDelegateEqualityComparison(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1523-1534).
bool CallBuilder::IsDelegateEqualityComparison(
    const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments)
{
    // Comparison on a delegate type is a C# builtin operator
    // that compiles down to a Delegate.op_Equality call.
    // We handle this as a special case to avoid inserting a cast to System.Delegate.
    return method.IsOperator()
        && method.DeclaringType() != nullptr
        && TS::IsKnownType(*method.DeclaringType(), TS::KnownTypeCode::Delegate)
        && (method.Name() == "op_Equality" || method.Name() == "op_Inequality")
        && arguments.size() == 2
        && arguments[0].Type().Kind() == TS::TypeKind::Delegate
        && arguments[1].Type().Equals(arguments[0].Type());
}

// The C# `private Expression HandleDelegateEqualityComparison(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1536-1543).
Syntax::Expression* CallBuilder::HandleDelegateEqualityComparison(
    const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments)
{
    return new Syntax::BinaryOperatorExpression(
        arguments[0].Expression(),
        method.Name() == "op_Equality" ? Syntax::BinaryOperatorType::Equality
                                       : Syntax::BinaryOperatorType::InEquality,
        arguments[1].Expression());
}

// The C# `private ExpressionWithResolveResult HandleImplicitConversion(IMethod
// method, TranslatedExpression argument)` (CallBuilder.cs lines 1545-1565).
ExpressionWithResolveResult CallBuilder::HandleImplicitConversion(
    const TS::IMethod& method, TranslatedExpression argument)
{
    Resolver::CSharpConversions conversions =
        Resolver::CSharpConversions::Get(*expressionBuilder_->compilation);
    TS::IType& targetType = const_cast<TS::IType&>(method.ReturnType());
    std::shared_ptr<Sem::Conversion> conv =
        conversions.ImplicitConversion(const_cast<TS::IType&>(argument.Type()), targetType);
    bool userDefinedValid = conv != nullptr && conv->IsUserDefined() && conv->IsValid()
        && conv->Method() != nullptr
        && conv->Method()->Equals(&method, &TS::NormalizeTypeVisitor::TypeErasure());
    if (!userDefinedValid)
    {
        // implicit conversion to targetType isn't directly possible, so first insert a cast to the argument type
        argument = argument.ConvertTo(
            const_cast<TS::IType&>(method.Parameters()[0]->Type()), *expressionBuilder_);
        conv = conversions.ImplicitConversion(
            const_cast<TS::IType&>(argument.Type()), targetType);
    }
    {
        const auto* direction =
            dynamic_cast<const Syntax::DirectionExpression*>(argument.Expression());
        if (direction != nullptr
            && direction->FieldDirection() == Syntax::FieldDirection::In
            && direction->Expression() != nullptr)
        {
            // `(TargetType)(in arg)` is invalid syntax.
            // Also, `f(in arg)` is invalid when there's an implicit conversion involved.
            argument = argument.UnwrapChild(direction->Expression());
        }
    }
    auto* cast = new Syntax::CastExpression(expressionBuilder_->ConvertType(targetType),
                                            argument.Expression());
    return WithRR(*cast, std::make_shared<Sem::ConversionResolveResult>(
                             const_cast<TS::IType&>(targetType).shared_from_this(),
                             SharedResolveResultAnnotation(*argument.Expression()), conv));
}

// The C# `private ExpressionWithResolveResult HandleConstructorCall(
// ExpectedTargetDetails expectedTargetDetails, ResolveResult? target, IMethod
// method, ArgumentList argumentList)` (CallBuilder.cs lines 1836-1900).
ExpressionWithResolveResult CallBuilder::HandleConstructorCall(
    const ExpectedTargetDetails& expectedTargetDetails, const Sem::ResolveResult* target,
    const TS::IMethod& method, ArgumentList argumentList)
{
    (void)expectedTargetDetails;
    if (settings_->AnonymousTypes()
        && IsAnonymousType(method.DeclaringType().get()))
    {
        // The C# Debug.Assert(argumentList.ArgumentToParameterMap == null &&
        // argumentList.ArgumentNames == null &&
        // argumentList.FirstOptionalArgumentIndex < 0) -- compiled out of the
        // shipped release assembly, so the port does not assert either.
        auto* atce = new Syntax::AnonymousTypeCreateExpression();
        if (argumentList.CanInferAnonymousTypePropertyNamesFromArguments())
        {
            for (Syntax::Expression* arg : argumentList.GetArgumentExpressions())
                atce->Initializers().Add(arg);
        }
        else
        {
            for (int i = 0; i < argumentList.Length(); i++)
            {
                atce->Initializers().Add(new Syntax::NamedExpression(
                    argumentList.ExpectedParameters[i]->Name(),
                    argumentList.Arguments[i]
                        .ConvertTo(const_cast<TS::IType&>(
                            argumentList.ExpectedParameters[i]->Type()),
                            *expressionBuilder_)
                        .Expression()));
            }
        }
        return WithRR(
            *atce,
            std::make_shared<Resolver::CSharpInvocationResolveResult>(
                target != nullptr ? AliasResolveResult(target) : nullptr,
                &method, argumentList.GetArgumentResolveResults(),
                Resolver::OverloadResolutionErrors::None, false,
                argumentList.IsExpandedForm, false,
                argumentList.ArgumentToParameterMap));
    }
    else
    {
        const TS::IParameterizedMember* foundMember;
        bool bestCandidateIsExpandedForm;
        while (IsUnambiguousCall(expectedTargetDetails, method, nullptr, {},
                   argumentList.GetArgumentResolveResults(),
                   argumentList.GetArgumentNames(),
                   argumentList.FirstOptionalArgumentIndex, foundMember,
                   bestCandidateIsExpandedForm)
                != Resolver::OverloadResolutionErrors::None
            || bestCandidateIsExpandedForm != argumentList.IsExpandedForm)
        {
            if (argumentList.AddNamesToPrimitiveValues)
            {
                argumentList.AddNamesToPrimitiveValues = false;
                continue;
            }
            if (argumentList.FirstOptionalArgumentIndex >= 0)
            {
                argumentList.FirstOptionalArgumentIndex = -1;
                continue;
            }
            CastArguments(argumentList.Arguments, argumentList.ExpectedParameters);
            break; // make sure that we don't not end up in an infinite loop
        }
        TS::ITypePtr returnTypeOverride;
        {
            // The C# `typeSystem.MainModule.TypeSystemOptions.HasFlag(...)` -- the
            // narrowed main-module options; the port reads the compilation-level
            // accessor (the DecompilerTypeSystem.TypeSystemOptions override carries
            // the same settings-derived value the narrowed MetadataModule does).
            TS::TypeSystemOptions options = typeSystem_->TypeSystemOptions();
            if ((options & TS::TypeSystemOptions::NativeIntegersWithoutAttribute)
                == TS::TypeSystemOptions::NativeIntegersWithoutAttribute)
            {
                // For DeclaringType, we don't use nint/nuint (so that
                // DeclaringType.GetConstructors etc. works), but in
                // NativeIntegersWithoutAttribute mode we must use nint/nuint for
                // expression types, so that the appropriate set of conversions is
                // used for further overload resolution.
                if (TS::IsKnownType(*method.DeclaringType(), TS::KnownTypeCode::IntPtr))
                    returnTypeOverride = TS::NInt();
                else if (TS::IsKnownType(*method.DeclaringType(), TS::KnownTypeCode::UIntPtr))
                    returnTypeOverride = TS::NUInt();
            }
        }
        auto* oce = new Syntax::ObjectCreateExpression(
            expressionBuilder_->ConvertType(const_cast<TS::IType&>(
                *method.DeclaringType())));
        for (Syntax::Expression* arg : argumentList.GetArgumentExpressions())
            oce->Arguments().Add(arg);
        return WithRR(
            *oce,
            std::make_shared<Resolver::CSharpInvocationResolveResult>(
                target != nullptr ? AliasResolveResult(target) : nullptr,
                &method, argumentList.GetArgumentResolveResults(),
                Resolver::OverloadResolutionErrors::None, false,
                argumentList.IsExpandedForm, false,
                argumentList.ArgumentToParameterMap, std::vector<std::shared_ptr<Sem::ResolveResult>>(),
                returnTypeOverride));
    }
}

// The C# `public ExpressionWithResolveResult BuildCollectionInitializerExpression(
// OpCode callOpCode, IMethod method, InitializedObjectResolveResult target,
// IReadOnlyList<ILInstruction> callArguments)` (CallBuilder.cs lines 667-726).
ExpressionWithResolveResult CallBuilder::BuildCollectionInitializerExpression(
    IL::OpCode callOpCode, const TS::IMethod& method,
    std::shared_ptr<Sem::InitializedObjectResolveResult> target,
    const std::vector<IL::ILInstruction*>& callArguments)
{
    ExpectedTargetDetails expectedTargetDetails;
    expectedTargetDetails.CallOpCode = callOpCode;
    // The C# `var unused = new IdentifierExpression("initializedObject")
    // .WithRR(target).WithoutILInstruction()`: the identifier exists only to
    // carry the initialized-object resolve result through the fix ladder and
    // the resolve-result annotations (it is never rendered).
    // The C# passes the ORIGINAL `target` to the invocation resolve result even
    // after the fix ladder may have re-pointed `unused` (the ladder's target
    // cast), so the handle is kept rather than moved.
    auto* unusedNode = new Syntax::IdentifierExpression("initializedObject");
    TranslatedExpression unused =
        WithoutILInstruction(WithRR(*unusedNode, target));

    std::vector<IL::ILInstruction*> args = callArguments;
    IL::Nop extensionTarget;
    if (method.IsExtensionMethod())
        args.insert(args.begin(), &extensionTarget);

    ArgumentList argumentList = BuildArgumentList(
        expectedTargetDetails, unused.ResolveResult(), method, 0, args,
        std::nullopt);
    argumentList.ArgumentNames = std::nullopt;
    argumentList.AddNamesToPrimitiveValues = false;
    argumentList.UseImplicitlyTypedOut = false;
    const TS::IParameterizedMember* foundMethod = nullptr;
    CallTransformation transform = GetRequiredTransformationsForCall(
        expectedTargetDetails, method, unused, argumentList,
        CallTransformation::None, foundMethod);

    // The C# `Debug.Assert((transform & ~(NoOptionalArgumentAllowed |
    // NoNamedArgsForPrettiness)) == 0)` is compiled out of the release engine.

    // Calls with only one argument do not need an array initializer expression
    // to wrap them. Any special cases are handled by the caller.
    // Note: we intentionally ignore the firstOptionalArgumentIndex here.
    int skipCount;
    if (method.IsExtensionMethod())
    {
        if (argumentList.Length() == 2)
            return ExpressionWithResolveResult(
                argumentList.Arguments[1].Expression(),
                argumentList.Arguments[1].ResolveResult());
        skipCount = 1;
    }
    else
    {
        if (argumentList.Length() == 1)
            return ExpressionWithResolveResult(
                argumentList.Arguments[0].Expression(),
                argumentList.Arguments[0].ResolveResult());
        skipCount = 0;
    }

    if ((transform & CallTransformation::NoOptionalArgumentAllowed)
        != CallTransformation::None)
        argumentList.FirstOptionalArgumentIndex = -1;

    auto* initializer = new Syntax::ArrayInitializerExpression();
    for (Syntax::Expression* element : argumentList.GetArgumentExpressions(skipCount))
        initializer->Elements().Add(element);
    // The C# target resolve result is the initialized-object result (the
    // original `target`, not the possibly fix-ladder re-pointed `unused`).
    return WithRR(
        *initializer,
        std::make_shared<Resolver::CSharpInvocationResolveResult>(
            AliasResolveResult(target.get()), &method,
            argumentList.GetArgumentResolveResults(skipCount),
            Resolver::OverloadResolutionErrors::None, method.IsExtensionMethod(),
            argumentList.IsExpandedForm, false));
}

// The C# `public ExpressionWithResolveResult BuildDictionaryInitializerExpression(
// OpCode callOpCode, IMethod method, InitializedObjectResolveResult target,
// IReadOnlyList<ILInstruction> indices, ILInstruction? value = null)`
// (CallBuilder.cs lines 728-753).
ExpressionWithResolveResult CallBuilder::BuildDictionaryInitializerExpression(
    IL::OpCode callOpCode, const TS::IMethod& method,
    std::shared_ptr<Sem::InitializedObjectResolveResult> target,
    const std::vector<IL::ILInstruction*>& indices, IL::ILInstruction* value)
{
    ExpectedTargetDetails expectedTargetDetails;
    expectedTargetDetails.CallOpCode = callOpCode;

    // The C# `callArguments = [new LdNull(), ...indices, value ?? new Nop()]`:
    // the leading null is the `this` slot BuildArgumentList skips at
    // firstParamIndex 1 (the C# indexer setter's own parameter list).
    std::vector<IL::ILInstruction*> callArguments;
    IL::LdNull thisSlot;
    IL::Nop missingValue;
    callArguments.push_back(&thisSlot);
    for (IL::ILInstruction* index : indices)
        callArguments.push_back(index);
    callArguments.push_back(value != nullptr ? value
                                             : static_cast<IL::ILInstruction*>(&missingValue));

    ArgumentList argumentList = BuildArgumentList(
        expectedTargetDetails, target.get(), method, 1, callArguments,
        std::nullopt);
    auto* unusedNode = new Syntax::IdentifierExpression("initializedObject");
    TranslatedExpression unused =
        WithoutILInstruction(WithRR(*unusedNode, std::move(target)));

    ExpressionWithResolveResult assignment = HandleAccessorCall(
        expectedTargetDetails, method, unused, argumentList.Arguments,
        argumentList.ArgumentNames);

    auto* assignmentExpr =
        dynamic_cast<Syntax::AssignmentExpression*>(assignment.Expression());
    assert(assignmentExpr != nullptr);
    if (auto* indexer =
            dynamic_cast<Syntax::IndexerExpression*>(assignmentExpr->Left()))
    {
        if (indexer->Target() != nullptr)
            indexer->Target()->Remove();
    }

    if (value != nullptr)
        return assignment;

    return ExpressionWithResolveResult(Syntax::Detach(assignmentExpr->Left()));
}

// The C# `private static bool IsInterpolatedStringCreation(IMethod method,
// ArgumentList argumentList)` (CallBuilder.cs lines 755-765).
bool CallBuilder::IsInterpolatedStringCreation(const TS::IMethod& method,
                                               const ArgumentList& argumentList)
{
    const TS::ITypeDefinition* declaringDef =
        method.DeclaringType() != nullptr ? method.DeclaringType()->GetDefinition() : nullptr;
    bool nameArm = method.IsStatic()
        && ((declaringDef != nullptr
                && TS::IsKnownType(*method.DeclaringType(), TS::KnownTypeCode::String)
                && method.Name() == "Format")
            || (method.Name() == "Create"
                && declaringDef != nullptr
                && declaringDef->Name() == "FormattableStringFactory"
                && declaringDef->Namespace() == "System.Runtime.CompilerServices"));
    if (!nameArm || argumentList.ArgumentNames.has_value())
        return false;
    // The C# `(argumentList.IsExpandedForm || !method.Parameters.Last().IsParams
    // || (argumentList.Length == 2 && argumentList.Arguments[1].Expression is
    // ArrayCreateExpression))` chain: the .NET `Last()` over a parameterless
    // method throws 'Sequence contains no elements' (the established
    // out_of_range convention), and the C# || short-circuit makes the
    // expanded-form arm bypass the params check entirely.
    if (argumentList.IsExpandedForm)
        return true;
    const std::vector<const TS::IParameter*>& parameters = method.Parameters();
    if (parameters.empty())
        throw std::out_of_range("Sequence contains no elements");
    if (!parameters.back()->IsParams())
        return true;
    return argumentList.Length() == 2
        && dynamic_cast<const Syntax::ArrayCreateExpression*>(
               argumentList.Arguments[1].Expression())
               != nullptr;
}

// The C# `private ExpressionWithResolveResult HandleStringInterpolation(
// IMethod method, ArgumentList argumentList)` (CallBuilder.cs lines 595-648).
ExpressionWithResolveResult CallBuilder::HandleStringInterpolation(
    const TS::IMethod& method, ArgumentList argumentList)
{
    std::optional<std::string> format;
    std::optional<std::vector<FormatToken>> tokens;
    if (!TryGetStringInterpolationTokens(argumentList, format, tokens))
        return ExpressionWithResolveResult();

    const std::vector<TranslatedExpression>& arguments = argumentList.Arguments;
    std::vector<Syntax::InterpolatedStringContent*> content;

    // The C# `bool unpackSingleElementArray` local: only a two-argument call
    // whose second argument is an array-literal creation with exactly one
    // element. The C# `argumentList.Length == 2 && ...` chain SHORT-CIRCUITS
    // before indexing Arguments[1] -- the port mirrors that order (a shorter
    // argument list never indexes past the end; the MSVC debug-iterator
    // assertion would abort the otherwise-valid shorter-call arms).
    const Syntax::ArrayCreateExpression* arrayCreation = nullptr;
    const Sem::ArrayCreateResolveResult* arrayCreationRR = nullptr;
    bool unpackSingleElementArray = false;
    if (!argumentList.IsExpandedForm && argumentList.Length() == 2)
    {
        arrayCreation = dynamic_cast<const Syntax::ArrayCreateExpression*>(
            arguments[1].Expression());
        arrayCreationRR = dynamic_cast<const Sem::ArrayCreateResolveResult*>(
            arguments[1].ResolveResult());
        unpackSingleElementArray = arrayCreation != nullptr
            && arrayCreation->Initializer() != nullptr
            && arrayCreation->Initializer()->Elements().Count() == 1
            && arrayCreationRR != nullptr
            && arrayCreationRR->InitializerElements().has_value()
            && !arrayCreationRR->InitializerElements()->empty();
    }

    // The C# `void UnpackSingleElementArray(ref TranslatedExpression argument)`
    // local function: replace the argument with the initializer element (the
    // node detached from the initializer, its own resolve result read through
    // the ArrayCreateResolveResult.InitializerElements entry -- both annotated
    // on the detached node, so the TranslatedExpression ctor assert holds).
    auto unpackArgument = [&]() -> TranslatedExpression {
        if (!unpackSingleElementArray)
            return TranslatedExpression();
        Syntax::Expression* element = arrayCreation->Initializer()->Elements().At(0);
        element->Remove();
        return TranslatedExpression(
            element, (*arrayCreationRR->InitializerElements())[0].get());
    };

    if (tokens->empty())
    {
        return ExpressionWithResolveResult();
    }

    for (const FormatToken& token : *tokens)
    {
        TranslatedExpression argument;
        switch (token.Kind)
        {
            case TokenKind::String:
                content.push_back(new Syntax::InterpolatedStringText(*token.Format));
                break;
            case TokenKind::Argument:
                argument = arguments[token.Index + 1];
                {
                    TranslatedExpression unpacked = unpackArgument();
                    if (unpacked.Expression() != nullptr)
                        argument = unpacked;
                }
                content.push_back(new Syntax::Interpolation(argument.Expression()));
                break;
            case TokenKind::ArgumentWithFormat:
                argument = arguments[token.Index + 1];
                {
                    TranslatedExpression unpacked = unpackArgument();
                    if (unpacked.Expression() != nullptr)
                        argument = unpacked;
                }
                content.push_back(
                    new Syntax::Interpolation(argument.Expression(), 0, token.Format));
                break;
            case TokenKind::ArgumentWithAlignment:
                argument = arguments[token.Index + 1];
                {
                    TranslatedExpression unpacked = unpackArgument();
                    if (unpacked.Expression() != nullptr)
                        argument = unpacked;
                }
                content.push_back(
                    new Syntax::Interpolation(argument.Expression(), token.Alignment));
                break;
            case TokenKind::ArgumentWithAlignmentAndFormat:
                argument = arguments[token.Index + 1];
                {
                    TranslatedExpression unpacked = unpackArgument();
                    if (unpacked.Expression() != nullptr)
                        argument = unpacked;
                }
                content.push_back(new Syntax::Interpolation(
                    argument.Expression(), token.Alignment, token.Format));
                break;
            case TokenKind::Error:
                // The C# switch has no Error arm (TryGetStringInterpolationTokens
                // rejects every Error token before the render runs).
                break;
        }
    }
    auto formattableStringType =
        const_cast<TS::IType&>(
            expressionBuilder_->compilation->FindType(TS::KnownTypeCode::FormattableString))
            .shared_from_this();
    auto resolveResults = argumentList.GetArgumentResolveResults(1);
    auto isrr = std::make_shared<Sem::InterpolatedStringResolveResult>(
        const_cast<TS::IType&>(
            expressionBuilder_->compilation->FindType(TS::KnownTypeCode::String))
            .shared_from_this(),
        *format, std::move(resolveResults));
    auto* expr = new Syntax::InterpolatedStringExpression();
    for (Syntax::InterpolatedStringContent* c : content)
    {
        expr->Content().Add(c);
    }
    if (method.Name() == "Format")
        return WithRR(*expr, isrr);
    auto* cast = new Syntax::CastExpression(
        expressionBuilder_->ConvertType(*formattableStringType),
        WithRR(*expr, isrr).Expression());
    return WithRR(
        *cast,
        std::make_shared<Sem::ConversionResolveResult>(
            formattableStringType, isrr,
            Sem::Conversions::ImplicitInterpolatedStringConversion()));
}

// The C# `private bool TryGetStringInterpolationTokens(ArgumentList
// argumentList, out string? format, out List<(...)>? tokens)` (CallBuilder.cs
// lines 766-842).
bool CallBuilder::TryGetStringInterpolationTokens(
    const ArgumentList& argumentList, std::optional<std::string>& format,
    std::optional<std::vector<FormatToken>>& tokens) const
{
    tokens.reset();
    format.reset();
    const std::vector<TranslatedExpression>& arguments = argumentList.Arguments;
    if (arguments.empty() || argumentList.ArgumentNames.has_value()
        || argumentList.ArgumentToParameterMap.has_value())
        return false;
    const auto* crr = dynamic_cast<const Sem::ConstantResolveResult*>(
        arguments[0].ResolveResult());
    if (crr == nullptr
        || !TS::IsKnownType(crr->Type(), TS::KnownTypeCode::String))
        return false;
    for (std::size_t i = 1; i < arguments.size(); i++)
    {
        for (const Syntax::AstNode* node :
             arguments[i].Expression()->DescendantsAndSelf())
        {
            if (auto* primitive = dynamic_cast<const Syntax::PrimitiveExpression*>(node))
            {
                if (std::holds_alternative<std::string>(primitive->Value()))
                    return false;
            }
        }
    }
    tokens.emplace();
    int i2 = 0;
    format = std::any_cast<std::string>(crr->ConstantValue());
    for (const auto& [kind, data] : TokenizeFormatString(*format))
    {
        int index = 0;
        std::vector<std::string> arg;
        int alignment = 0;
        switch (kind)
        {
            case TokenKind::Error:
                return false;
            case TokenKind::String:
                tokens->push_back({kind, -1, 0, data});
                break;
            case TokenKind::Argument:
                if (!TryParseInt32(*data, index) || index != i2)
                    return false;
                i2++;
                tokens->push_back({kind, index, 0, std::nullopt});
                break;
            case TokenKind::ArgumentWithFormat:
                arg = SplitWithCount(*data, ":", 2);
                if (arg.size() != 2 || arg[1].empty())
                    return false;
                if (!TryParseInt32(arg[0], index) || index != i2)
                    return false;
                i2++;
                tokens->push_back({kind, index, 0, arg[1]});
                break;
            case TokenKind::ArgumentWithAlignment:
                arg = SplitWithCount(*data, ",", 2);
                if (arg.size() != 2 || arg[1].empty())
                    return false;
                if (!TryParseInt32(arg[0], index) || index != i2)
                    return false;
                if (!TryParseInt32(arg[1], alignment))
                    return false;
                i2++;
                tokens->push_back({kind, index, alignment, std::nullopt});
                break;
            case TokenKind::ArgumentWithAlignmentAndFormat:
                arg = SplitWithCount(*data, ",:", 3);
                if (arg.size() != 3 || arg[1].empty() || arg[2].empty())
                    return false;
                if (!TryParseInt32(arg[0], index) || index != i2)
                    return false;
                if (!TryParseInt32(arg[1], alignment))
                    return false;
                i2++;
                tokens->push_back({kind, index, alignment, arg[2]});
                break;
        }
    }
    return i2 == static_cast<int>(arguments.size()) - 1;
}

// The C# `private IEnumerable<(TokenKind, string?)> TokenizeFormatString(
// string value)` (CallBuilder.cs lines 883-935) -- the `Peek`/`Next` local
// functions inlined over the string (the C# iterator yields pairs; the port
// returns them, the text nullopt for the C# null).
std::vector<std::pair<CallBuilder::TokenKind, std::optional<std::string>>>
CallBuilder::TokenizeFormatString(const std::string& value)
{
    std::vector<std::pair<TokenKind, std::optional<std::string>>> tokens;
    int pos = -1;
    TokenKind kind = TokenKind::String;
    std::string sb;

    // The C# `int Peek(int steps = 1)` local: the code unit `steps` past `pos`,
    // or -1 past the end. The port iterates UTF-8 bytes: every character the
    // state machine tests (`{`, `}`, `:`, `,`) is ASCII and single-byte, and
    // every appended byte preserves the UTF-8 sequence, so the state machine
    // is byte-faithful over the port's UTF-8 strings.
    auto peek = [&value, &pos](int steps = 1) -> int {
        if (pos + steps < static_cast<int>(value.size()))
            return static_cast<unsigned char>(value[pos + steps]);
        return -1;
    };
    // The C# `int Next()` local: `Peek()` then advance.
    auto next = [&pos, &peek]() -> int {
        int val = peek();
        pos++;
        return val;
    };

    int nextChar;
    while ((nextChar = next()) > -1)
    {
        switch (static_cast<char>(nextChar))
        {
            case '{':
                if (peek() == '{')
                {
                    kind = TokenKind::String;
                    sb += "{{";
                    next();
                }
                else
                {
                    if (!sb.empty())
                    {
                        tokens.emplace_back(kind, sb);
                    }
                    kind = TokenKind::Argument;
                    sb.clear();
                }
                break;
            case '}':
                if (kind != TokenKind::String)
                {
                    tokens.emplace_back(kind, sb);
                    sb.clear();
                    kind = TokenKind::String;
                }
                else if (peek() == '}')
                {
                    sb += "}}";
                    next();
                }
                else
                {
                    tokens.emplace_back(TokenKind::Error, std::nullopt);
                }
                break;
            case ':':
                if (kind == TokenKind::Argument)
                {
                    kind = TokenKind::ArgumentWithFormat;
                }
                else if (kind == TokenKind::ArgumentWithAlignment)
                {
                    kind = TokenKind::ArgumentWithAlignmentAndFormat;
                }
                sb += ':';
                break;
            case ',':
                if (kind == TokenKind::Argument)
                {
                    kind = TokenKind::ArgumentWithAlignment;
                }
                sb += ',';
                break;
            default:
                sb += static_cast<char>(nextChar);
                break;
        }
    }
    if (!sb.empty())
    {
        if (kind == TokenKind::String)
            tokens.emplace_back(kind, sb);
        else
            tokens.emplace_back(TokenKind::Error, std::nullopt);
    }
    return tokens;
}

// The C# `private bool HandleRangeConstruction(out ExpressionWithResolveResult
// result, OpCode callOpCode, IMethod method, TranslatedExpression target,
// ArgumentList argumentList)` (CallBuilder.cs lines 2245-2300).
bool CallBuilder::HandleRangeConstruction(
    ExpressionWithResolveResult& result, IL::OpCode callOpCode, const TS::IMethod& method,
    const TranslatedExpression& target, ArgumentList argumentList)
{
    result = ExpressionWithResolveResult();
    if (argumentList.ArgumentNames.has_value())
    {
        return false; // range syntax doesn't support named arguments
    }
    auto memberRR = [&](const TS::IMethod& m, const TS::IMember* owner) {
        return std::make_shared<Sem::MemberResolveResult>(
            SharedResolveResultAnnotation(*target.Expression()),
            owner != nullptr ? owner : &m);
    };
    if (method.DeclaringType() != nullptr
        && TS::IsKnownType(*method.DeclaringType(), TS::KnownTypeCode::Range))
    {
        if (callOpCode == IL::OpCode::NewObj && argumentList.Length() == 2)
        {
            result = WithRR(
                *new Syntax::BinaryOperatorExpression(argumentList.Arguments[0].Expression(),
                    Syntax::BinaryOperatorType::Range, argumentList.Arguments[1].Expression()),
                memberRR(method, nullptr));
            return true;
        }
        if (callOpCode == IL::OpCode::Call && method.Name() == "get_All"
            && argumentList.Length() == 0)
        {
            result = WithRR(*new Syntax::BinaryOperatorExpression(nullptr,
                                Syntax::BinaryOperatorType::Range, nullptr),
                            memberRR(method,
                                method.AccessorOwner() != nullptr ? method.AccessorOwner() : &method));
            return true;
        }
        if (callOpCode == IL::OpCode::Call && method.Name() == "StartAt"
            && argumentList.Length() == 1)
        {
            result = WithRR(*new Syntax::BinaryOperatorExpression(
                                argumentList.Arguments[0].Expression(),
                                Syntax::BinaryOperatorType::Range, nullptr),
                            memberRR(method, nullptr));
            return true;
        }
        if (callOpCode == IL::OpCode::Call && method.Name() == "EndAt"
            && argumentList.Length() == 1)
        {
            result = WithRR(*new Syntax::BinaryOperatorExpression(nullptr,
                                Syntax::BinaryOperatorType::Range,
                                argumentList.Arguments[0].Expression()),
                            memberRR(method, nullptr));
            return true;
        }
    }
    else if (callOpCode == IL::OpCode::NewObj && method.DeclaringType() != nullptr
             && TS::IsKnownType(*method.DeclaringType(), TS::KnownTypeCode::Index))
    {
        if (argumentList.Length() != 2)
            return false;
        const auto* pe = dynamic_cast<const Syntax::PrimitiveExpression*>(
            argumentList.Arguments[1].Expression());
        bool isTrue = false;
        if (pe != nullptr)
        {
            // The C# `pe.Value is true` -- the boxed-Boolean identity test.
            const bool* b = std::get_if<bool>(&pe->Value());
            isTrue = b != nullptr && *b;
        }
        if (!isTrue)
            return false;
        result = WithRR(*new Syntax::UnaryOperatorExpression(
                            argumentList.Arguments[0].Expression(),
                            Syntax::UnaryOperatorType::IndexFromEnd),
                        memberRR(method, nullptr));
        return true;
    }
    else if (const auto* rangeIndexAccessor =
                 dynamic_cast<const TS::SyntheticRangeIndexAccessor*>(&method);
             rangeIndexAccessor != nullptr && rangeIndexAccessor->IsSlicing())
    {
        // For slicing the method is called Slice()/Substring(), but we still need to output indexer notation.
        // So special-case range-based slicing here.
        auto* indexer = new Syntax::IndexerExpression(target.Expression());
        for (Syntax::Expression* arg : argumentList.GetArgumentExpressions())
            indexer->Arguments().Add(arg);
        result = WithRR(*indexer, memberRR(method, nullptr));
        return true;
    }
    return false;
}

// The C# `public ExpressionWithResolveResult Build(OpCode callOpCode, IMethod
// method, IReadOnlyList<ILInstruction> callArguments,
// IReadOnlyList<int>? argumentToParameterMap = null, IType? constrainedTo = null)`
// (CallBuilder.cs lines 332-594) -- the mainline call render.
ExpressionWithResolveResult CallBuilder::Build(
    IL::OpCode callOpCode, const TS::IMethod& method,
    const std::vector<IL::ILInstruction*>& callArguments,
    const std::optional<std::vector<int>>& argumentToParameterMap,
    const TS::IType* constrainedTo)
{
    const TS::IMethod* resolvedMethod = &method;
    if (resolvedMethod->IsExplicitInterfaceImplementation()
        && callOpCode == IL::OpCode::Call)
    {
        // Direct non-virtual call to explicit interface implementation.
        // This can't really be represented in C#, but at least in the case where
        // the class is sealed, we can equivalently call the interface member instead:
        std::vector<const TS::IMember*> interfaceMembers =
            resolvedMethod->ExplicitlyImplementedInterfaceMembers();
        const TS::ITypeDefinition* declaringTypeDefinition =
            resolvedMethod->DeclaringTypeDefinition();
        if (declaringTypeDefinition != nullptr
            && declaringTypeDefinition->Kind() == TS::TypeKind::Class
            && declaringTypeDefinition->IsSealed() && interfaceMembers.size() == 1)
        {
            // The C# `.Single()` -- Count == 1 guarantees the single element.
            const TS::IMethod* interfaceMethod =
                dynamic_cast<const TS::IMethod*>(interfaceMembers.front());
            assert(interfaceMethod != nullptr);
            resolvedMethod = interfaceMethod;
            callOpCode = IL::OpCode::CallVirt;
        }
    }
    // Used for Call, CallVirt and NewObj
    ExpectedTargetDetails expectedTargetDetails;
    expectedTargetDetails.CallOpCode = callOpCode;
    IL::ILFunction* localFunction = nullptr;
    if (resolvedMethod->IsLocalFunction())
    {
        localFunction = expressionBuilder_->ResolveLocalFunction(*resolvedMethod);
        assert(localFunction != nullptr);
    }
    TranslatedExpression target;
    if (callOpCode == IL::OpCode::NewObj)
    {
        target = TranslatedExpression(); // no target
    }
    else if (localFunction != nullptr)
    {
        auto* ide = new Syntax::IdentifierExpression(localFunction->Name);
        if (!resolvedMethod->TypeArguments().empty())
        {
            for (const TS::ITypePtr& typeArgument : resolvedMethod->TypeArguments())
                ide->TypeArguments().Add(expressionBuilder_->ConvertType(*typeArgument));
        }
        WithILFunction(*ide, localFunction);
        target = WithRR(WithoutILInstruction(*ide), ToMethodGroup(*resolvedMethod, *localFunction));
    }
    else
    {
        IL::ILInstruction* thisArg = nullptr;
        if (!callArguments.empty())
            thisArg = callArguments.front();
        if (const auto* ldObjIfRef = dynamic_cast<const IL::LdObjIfRef*>(thisArg);
            ldObjIfRef != nullptr)
        {
            assert(constrainedTo != nullptr);
            thisArg = ldObjIfRef->Target();
        }
        target = expressionBuilder_->TranslateTarget(
            thisArg,
            callOpCode == IL::OpCode::Call || resolvedMethod->IsConstructor(),
            resolvedMethod->IsStatic(), *resolvedMethod->DeclaringType(), constrainedTo);
        if (constrainedTo == nullptr)
        {
            const auto* cast = dynamic_cast<const Syntax::CastExpression*>(target.Expression());
            const auto* conversion =
                dynamic_cast<const Sem::ConversionResolveResult*>(target.ResolveResult());
            if (cast != nullptr && conversion != nullptr
                && TS::IsKnownType(target.Type(), TS::KnownTypeCode::Object)
                && conversion->ConversionProperty()->IsBoxingConversion())
            {
                // boxing conversion on call target?
                // let's see if we can make that implicit:
                target = target.UnwrapChild(const_cast<Syntax::CastExpression*>(cast)->Expression());
                // we'll need to make sure the boxing effect is preserved
                expectedTargetDetails.NeedsBoxingConversion = true;
            }
        }
    }

    int firstParamIndex =
        (resolvedMethod->IsStatic() || callOpCode == IL::OpCode::NewObj) ? 0 : 1;
    assert(firstParamIndex == 0 || !argumentToParameterMap.has_value()
           || argumentToParameterMap->front() == -1);

    ArgumentList argumentList = BuildArgumentList(
        expectedTargetDetails,
        target.Expression() != nullptr
            ? SharedResolveResultAnnotation(*target.Expression()).get()
            : nullptr,
        *resolvedMethod, firstParamIndex, callArguments, argumentToParameterMap);

    if (localFunction != nullptr)
    {
        auto* invocation = new Syntax::InvocationExpression(target.Expression());
        for (Syntax::Expression* arg : argumentList.GetArgumentExpressions())
            invocation->Arguments().Add(arg);
        return WithRR(*invocation, std::make_shared<Resolver::CSharpInvocationResolveResult>(
            SharedResolveResultAnnotation(*target.Expression()), resolvedMethod,
            argumentList.GetArgumentResolveResults(),
            Resolver::OverloadResolutionErrors::None, false,
            argumentList.IsExpandedForm));
    }

    if (const auto* varArgMethod =
            dynamic_cast<const TS::VarArgInstanceMethod*>(resolvedMethod);
        varArgMethod != nullptr)
    {
        argumentList.FirstOptionalArgumentIndex = -1;
        argumentList.AddNamesToPrimitiveValues = false;
        argumentList.UseImplicitlyTypedOut = false;
        int regularParameterCount = varArgMethod->RegularParameterCount();
        auto* argListArg = new Syntax::UndocumentedExpression();
        argListArg->UndocumentedExpressionType(
            Syntax::UndocumentedExpressionType::ArgList);
        std::size_t paramIndex = static_cast<std::size_t>(regularParameterCount);
        for (std::size_t i = static_cast<std::size_t>(regularParameterCount);
             i < argumentList.Arguments.size(); i++, paramIndex++)
        {
            argListArg->Arguments().Add(
                argumentList.Arguments[i]
                    .ConvertTo(
                        const_cast<TS::IType&>(
                            argumentList.ExpectedParameters[paramIndex]->Type()),
                        *expressionBuilder_)
                    .Expression());
        }
        TranslatedExpression argListRR = WithRR(
            WithoutILInstruction(*new Syntax::UndocumentedExpression()),
            std::make_shared<Sem::ResolveResult>(TS::ArgList()));
        auto kept = std::vector<TranslatedExpression>(
            argumentList.Arguments.begin(),
            argumentList.Arguments.begin() + regularParameterCount);
        kept.push_back(argListRR);
        argumentList.Arguments = std::move(kept);
        resolvedMethod = varArgMethod->BaseMethod();
        argumentList.ExpectedParameters = resolvedMethod->Parameters();
    }

    if (settings_->Ranges())
    {
        ExpressionWithResolveResult rangeResult;
        if (HandleRangeConstruction(rangeResult, callOpCode, *resolvedMethod, target,
                argumentList))
        {
            return rangeResult;
        }
    }

    if (callOpCode == IL::OpCode::NewObj)
    {
        return HandleConstructorCall(expectedTargetDetails, target.ResolveResult(),
            *resolvedMethod, argumentList);
    }

    if (resolvedMethod->Name() == "Invoke"
        && resolvedMethod->DeclaringType() != nullptr
        && resolvedMethod->DeclaringType()->Kind() == TS::TypeKind::Delegate
        && !IsNullConditional(target.Expression()))
    {
        auto* invocation = new Syntax::InvocationExpression(target.Expression());
        for (Syntax::Expression* arg : argumentList.GetArgumentExpressions())
            invocation->Arguments().Add(arg);
        return WithRR(*invocation, std::make_shared<Resolver::CSharpInvocationResolveResult>(
            SharedResolveResultAnnotation(*target.Expression()), resolvedMethod,
            argumentList.GetArgumentResolveResults(),
            Resolver::OverloadResolutionErrors::None, false,
            argumentList.IsExpandedForm, true));
    }

    if (settings_->StringInterpolation()
        && IsInterpolatedStringCreation(*resolvedMethod, argumentList))
    {
        // The C# `var result = HandleStringInterpolation(method, argumentList);`
        // and falls through to the accessor arm when the tokens do not parse
        // (the default-result shape).
        ExpressionWithResolveResult result =
            HandleStringInterpolation(*resolvedMethod, argumentList);
        if (result.Expression() != nullptr)
            return result;
    }

    int allowedParamCount =
        (TS::IsKnownType(resolvedMethod->ReturnType(), TS::KnownTypeCode::Void) ? 1 : 0);
    if (resolvedMethod->IsAccessor()
        && (resolvedMethod->AccessorOwner()->SymbolKind() == TS::SymbolKind::Indexer
            || static_cast<int>(argumentList.ExpectedParameters.size()) == allowedParamCount))
    {
        argumentList.CheckNoNamedOrOptionalArguments();
        // The C# `argumentList.Arguments.ToList()` copies the argument list; the
        // names array is shared (neither the accessor fix loop nor the overload
        // resolution mutates it, so the port's by-value copy is faithful).
        return HandleAccessorCall(expectedTargetDetails, *resolvedMethod, target,
                                  argumentList.Arguments, argumentList.ArgumentNames);
    }

    if (IsDelegateEqualityComparison(*resolvedMethod, argumentList.Arguments))
    {
        argumentList.CheckNoNamedOrOptionalArguments();
        Syntax::Expression* delegateExpr =
            HandleDelegateEqualityComparison(*resolvedMethod, argumentList.Arguments);
        return WithRR(*delegateExpr,
            std::make_shared<Resolver::CSharpInvocationResolveResult>(
                SharedResolveResultAnnotation(*target.Expression()), resolvedMethod,
                argumentList.GetArgumentResolveResults(),
                Resolver::OverloadResolutionErrors::None, false,
                argumentList.IsExpandedForm));
    }

    if (resolvedMethod->IsOperator() && resolvedMethod->Name() == "op_Implicit"
        && argumentList.Length() == 1)
    {
        argumentList.CheckNoNamedOrOptionalArguments();
        return HandleImplicitConversion(*resolvedMethod, argumentList.Arguments[0]);
    }

    if (settings_->InlineArrays() && resolvedMethod->DeclaringType() != nullptr
        && resolvedMethod->DeclaringType()->GetDefinition() != nullptr
        && resolvedMethod->DeclaringType()->GetDefinition()->FullName()
            == "<PrivateImplementationDetails>"
        && (resolvedMethod->Name() == "InlineArrayAsSpan"
            || resolvedMethod->Name() == "InlineArrayAsReadOnlySpan")
        && argumentList.Length() == 2)
    {
        argumentList.CheckNoNamedOrOptionalArguments();
        const TS::IType& arrayType = *resolvedMethod->TypeArguments().at(0);
        std::optional<int> arrayLength = TS::GetInlineArrayLength(arrayType);
        TS::ITypePtr arrayElementType = TS::GetInlineArrayElementType(arrayType);
        TranslatedExpression argument = argumentList.Arguments[0];
        TranslatedExpression spanLengthExpr = argumentList.Arguments[1];
        TS::ITypePtr targetType = const_cast<TS::IType&>(resolvedMethod->ReturnType())
                                      .shared_from_this();
        TS::ITypePtr spanType = const_cast<TS::IType&>(
            typeSystem_->FindType(TS::KnownTypeCode::SpanOfT))
                                    .shared_from_this();
        {
            const auto* direction =
                dynamic_cast<const Syntax::DirectionExpression*>(argument.Expression());
            if (direction != nullptr
                && (direction->FieldDirection() == Syntax::FieldDirection::In
                    || direction->FieldDirection() == Syntax::FieldDirection::Ref)
                && direction->Expression() != nullptr)
            {
                // `(TargetType)(in arg)` is invalid syntax.
                // Also, `f(in arg)` is invalid when there's an implicit conversion involved.
                argument = argument.UnwrapChild(direction->Expression());
            }
        }
        const std::any& spanLengthValueAny = spanLengthExpr.ResolveResult()->ConstantValue();
        const std::int32_t* spanLength = std::any_cast<std::int32_t>(&spanLengthValueAny);
        bool spanLengthMatches = false;
        int spanLengthValue = 0;
        if (spanLength != nullptr)
        {
            spanLengthValue = *spanLength;
            spanLengthMatches = arrayLength.has_value() && spanLengthValue <= *arrayLength;
        }
        if (spanLengthMatches)
        {
            if (spanLengthValue < *arrayLength)
            {
                auto* indexer = new Syntax::IndexerExpression(argument.Expression());
                indexer->Arguments().Add(new Syntax::BinaryOperatorExpression(nullptr,
                    Syntax::BinaryOperatorType::Range, spanLengthExpr.Expression()));
                argument = WithoutILInstruction(WithRR(
                    *indexer,
                    std::make_shared<Sem::ResolveResult>(
                        std::make_shared<TS::ParameterizedType>(
                            spanType, std::vector<TS::ITypePtr>{arrayElementType}))));
                if (TS::IsKnownType(*targetType, TS::KnownTypeCode::SpanOfT))
                {
                    return ExpressionWithResolveResult(argument.Expression(),
                                                       argument.ResolveResult());
                }
            }
            auto* castExpr =
                new Syntax::CastExpression(expressionBuilder_->ConvertType(*targetType),
                                            argument.Expression());
            return WithRR(*castExpr, std::make_shared<Sem::ConversionResolveResult>(
                                 targetType,
                                 SharedResolveResultAnnotation(*argument.Expression()),
                                 Sem::Conversions::InlineArrayConversion()));
        }
    }

    if (settings_->LiftNullables() && resolvedMethod->Name() == "GetValueOrDefault"
        && resolvedMethod->DeclaringType() != nullptr
        && TS::IsKnownType(*resolvedMethod->DeclaringType(),
                           TS::KnownTypeCode::NullableOfT)
        && TS::IsKnownType(*DeclaringTypeArguments(*resolvedMethod->DeclaringType())
                               .at(0),
            TS::KnownTypeCode::Boolean)
        && argumentList.Length() == 0)
    {
        argumentList.CheckNoNamedOrOptionalArguments();
        auto* comparison = new Syntax::BinaryOperatorExpression(
            target.Expression(), Syntax::BinaryOperatorType::Equality, TruePrimitive());
        return WithRR(*comparison,
            std::make_shared<Resolver::CSharpInvocationResolveResult>(
                SharedResolveResultAnnotation(*target.Expression()), resolvedMethod,
                argumentList.GetArgumentResolveResults(),
                Resolver::OverloadResolutionErrors::None, false,
                argumentList.IsExpandedForm));
    }

    const TS::IParameterizedMember* foundMethod = nullptr;
    CallTransformation transform = GetRequiredTransformationsForCall(
        expectedTargetDetails, *resolvedMethod, target, argumentList,
        CallTransformation::All, foundMethod);
    // GetRequiredTransformationsForCall always assigns foundMethod (the resolved overload or 'method').
    assert(foundMethod != nullptr);

    // Note: after this, 'method' and 'foundMethod' may differ,
    // but as far as allowed by IsAppropriateCallTarget().

    // Need to update list of parameter names, because foundMethod is different and thus might use different names.
    bool methodEqualsFoundMethod = resolvedMethod->MemberDefinition()
        == foundMethod->MemberDefinition();
    if (!methodEqualsFoundMethod
        && static_cast<int>(argumentList.ParameterNames.size())
            >= static_cast<int>(foundMethod->Parameters().size()))
    {
        for (std::size_t i = 0; i < foundMethod->Parameters().size(); i++)
        {
            argumentList.ParameterNames[i] = foundMethod->Parameters()[i]->Name();
        }
    }

    Syntax::Expression* targetExpr;
    std::string methodName = resolvedMethod->Name();
    Syntax::AstNodeCollectionT<Syntax::AstType>* typeArgumentList = nullptr;
    if ((transform & CallTransformation::NoNamedArgsForPrettiness)
        != CallTransformation::None)
    {
        argumentList.AddNamesToPrimitiveValues = false;
    }
    if ((transform & CallTransformation::NoOptionalArgumentAllowed)
        != CallTransformation::None)
    {
        argumentList.FirstOptionalArgumentIndex = -1;
    }
    if ((transform & CallTransformation::RequireTarget) != CallTransformation::None)
    {
        auto* memberRef =
            new Syntax::MemberReferenceExpression(target.Expression(), methodName);
        targetExpr = memberRef;
        typeArgumentList = &memberRef->TypeArguments();

        // HACK : convert this.Dispose() to ((IDisposable)this).Dispose(), if Dispose is an explicitly implemented interface method.
        // settings.AlwaysCastTargetsOfExplicitInterfaceImplementationCalls == true is used in Windows Forms' InitializeComponent methods.
        if (resolvedMethod->IsExplicitInterfaceImplementation()
            && (dynamic_cast<const Syntax::ThisReferenceExpression*>(target.Expression())
                    != nullptr
                || settings_->AlwaysCastTargetsOfExplicitInterfaceImplementationCalls()))
        {
            const TS::IMember* interfaceMember =
                resolvedMethod->ExplicitlyImplementedInterfaceMembers().front();
            auto* castExpression = new Syntax::CastExpression(
                expressionBuilder_->ConvertType(*interfaceMember->DeclaringType()),
                Syntax::Detach(target.Expression()));
            methodName = interfaceMember->Name();
            auto* renamed = new Syntax::MemberReferenceExpression(
                castExpression, methodName);
            targetExpr = renamed;
            typeArgumentList = &renamed->TypeArguments();
        }
        if (constrainedTo != nullptr)
        {
            const auto* memberRefTarget =
                dynamic_cast<const Syntax::MemberReferenceExpression*>(targetExpr);
            if (memberRefTarget != nullptr)
            {
                auto* cast = dynamic_cast<Syntax::CastExpression*>(
                    memberRefTarget->Target());
                if (cast != nullptr)
                {
                    cast->AddTrailingTrivia(new Syntax::Comment(
                        std::string("cast due to constrained. prefix"),
                        Syntax::CommentType::MultiLine));
                }
            }
        }
    }
    else
    {
        auto* identifier = new Syntax::IdentifierExpression(methodName);
        targetExpr = identifier;
        typeArgumentList = &identifier->TypeArguments();
    }

    if ((transform & CallTransformation::RequireTypeArguments)
            != CallTransformation::None
        && (!settings_->AnonymousTypes()
            || !AnyTypeArgumentContainsAnonymousType(resolvedMethod->TypeArguments())))
    {
        for (const TS::ITypePtr& typeArgument : resolvedMethod->TypeArguments())
            typeArgumentList->Add(expressionBuilder_->ConvertType(*typeArgument));
    }
    auto* invocation = new Syntax::InvocationExpression(targetExpr);
    for (Syntax::Expression* arg : argumentList.GetArgumentExpressions())
        invocation->Arguments().Add(arg);
    return WithRR(*invocation, std::make_shared<Resolver::CSharpInvocationResolveResult>(
        SharedResolveResultAnnotation(*target.Expression()), foundMethod,
        argumentList.GetArgumentResolveResultsDirect(),
        Resolver::OverloadResolutionErrors::None, false,
        argumentList.IsExpandedForm));
}

// The C# `static MethodGroupResolveResult ToMethodGroup(IMethod method,
// ILFunction localFunction)` (CallBuilder.cs lines 2199-2210).
std::shared_ptr<Resolver::MethodGroupResolveResult> CallBuilder::ToMethodGroup(
    const TS::IMethod& method, const IL::ILFunction& localFunction)
{
    return std::make_shared<Resolver::MethodGroupResolveResult>(
        nullptr, localFunction.Name,
        std::vector<Resolver::MethodListWithDeclaringType>{
            Resolver::MethodListWithDeclaringType(method.DeclaringType(),
                {static_cast<const TS::IParameterizedMember*>(&method)})},
        method.TypeArguments());
}

// ---------------------------------------------------------------------------

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

    // The C# `.Skip(skipCount).Take(GetActualArgumentCount())`: Take caps the
    // slice at the remaining elements, so the count is clamped BEFORE the
    // iterator arithmetic (an unclamped begin + count seeks past end(), the
    // MSVC debug-iterator assertion the interpolation slice's skipCount=1
    // call first exercised).
    int actualCount = GetActualArgumentCount();
    int skipped = std::min(skipCount, static_cast<int>(results.size()));
    actualCount = std::max(0, std::min(actualCount, static_cast<int>(results.size()) - skipped));
    auto begin = results.begin() + skipped;
    auto end = begin + actualCount;
    return std::vector<std::shared_ptr<Sem::ResolveResult>>(begin, end);
}

std::vector<std::shared_ptr<Sem::ResolveResult>>
ArgumentList::GetArgumentResolveResultsDirect(int skipCount)
{
    std::vector<std::shared_ptr<Sem::ResolveResult>> results;
    results.reserve(Arguments.size());
    for (const TranslatedExpression& a : Arguments)
        results.push_back(SharedResolveResultAnnotation(*a.Expression()));

    // The same clamped-before-arithmetic slice (the C# Skip/Take cap).
    int actualCount = GetActualArgumentCount();
    int skipped = std::min(skipCount, static_cast<int>(results.size()));
    actualCount = std::max(0, std::min(actualCount, static_cast<int>(results.size()) - skipped));
    auto begin = results.begin() + skipped;
    auto end = begin + actualCount;
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

// -- The delegate-reference family (CallBuilder.cs lines 1905-2212) ---------------

// The C# `private bool CanUseDelegateConstruction(IMethod targetMethod,
// ILInstruction thisArg, IMethod invokeMethod)` (lines 1937-1967).
bool CallBuilder::CanUseDelegateConstruction(const TS::IMethod& targetMethod,
                                             const IL::ILInstruction* thisArg,
                                             const TS::IMethod* invokeMethod)
{
    // Accessors cannot be directly referenced as method group in C#
    // see https://github.com/icsharpcode/ILSpy/issues/1741#issuecomment-540179101
    if (targetMethod.IsAccessor())
        return false;
    if (targetMethod.IsStatic())
    {
        // If the invoke method is known, we can compare the parameter counts to figure out whether the
        // delegate is static or binds the first argument
        if (invokeMethod != nullptr)
        {
            if (invokeMethod->Parameters().size() == targetMethod.Parameters().size())
            {
                return MatchLdNull(thisArg);
            }
            else if (targetMethod.IsExtensionMethod()
                     && invokeMethod->Parameters().size()
                            == targetMethod.Parameters().size() - 1)
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        else
        {
            // delegate type unknown:
            return MatchLdNull(thisArg) || targetMethod.IsExtensionMethod();
        }
    }
    else
    {
        // targetMethod is instance method
        if (invokeMethod != nullptr
            && invokeMethod->Parameters().size() != targetMethod.Parameters().size())
            return false;
        return true;
    }
}

// The C# `TranslatedExpression HandleDelegateConstruction(CallInstruction inst)`
// (lines 1905-1935).
TranslatedExpression CallBuilder::HandleDelegateConstruction(const IL::Call& inst)
{
    IL::ILInstruction* thisArg = inst.Arguments[0].get();
    IL::ILInstruction* func = inst.Arguments[1].get();
    const TS::IMethod* method = nullptr;
    ExpectedTargetDetails expectedTargetDetails;
    switch (func->Op)
    {
        case IL::OpCode::LdFtn:
            method = static_cast<IL::LdFtn*>(func)->Method.get();
            expectedTargetDetails.CallOpCode = IL::OpCode::Call;
            break;
        case IL::OpCode::LdVirtFtn:
            method = static_cast<IL::LdVirtFtn*>(func)->Method.get();
            expectedTargetDetails.CallOpCode = IL::OpCode::CallVirt;
            break;
        default:
            throw std::invalid_argument(
                "Unknown instruction type: " + std::string(IL::OpCodeName(func->Op)));
    }
    // The C# reads `inst.Method.DeclaringType` (a null reference would NRE --
    // the Call node's Method must be resolved for this arm).
    assert(inst.Method != nullptr);
    TS::ITypePtr declaringType = inst.Method->DeclaringType();
    const TS::IMethod* invokeMethod =
        declaringType ? TS::GetDelegateInvokeMethod(*declaringType) : nullptr;
    if (CanUseDelegateConstruction(*method, thisArg, invokeMethod))
    {
        return HandleDelegateConstruction(*declaringType, *method,
                                          expectedTargetDetails, thisArg,
                                          const_cast<IL::ILInstruction*>(
                                              static_cast<const IL::ILInstruction*>(&inst)));
    }
    else
    {
        ArgumentList argumentList = BuildArgumentList(
            expectedTargetDetails, nullptr, *inst.Method, 0, CallChildPtrs(inst),
            std::nullopt);
        ExpectedTargetDetails newObjDetails;
        newObjDetails.CallOpCode = IL::OpCode::NewObj;
        return WithILInstruction(
            HandleConstructorCall(newObjDetails, nullptr, *inst.Method,
                                  std::move(argumentList)),
            const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
    }
}

// The C# `(TranslatedExpression target, bool addTypeArguments, string methodName,
// ResolveResult result) DisambiguateDelegateReference(...)` (lines 2029-2131),
// ported as a private local structure.
namespace {
struct DisambiguationOutcome {
    TranslatedExpression target;
    bool addTypeArguments = false;
    std::string methodName;
    std::shared_ptr<Sem::ResolveResult> result;
};
} // namespace

DisambiguationOutcome CallBuilderDisambiguateDelegateReference(
    CallBuilder& self, ExpressionBuilder& expressionBuilder,
    const Resolver::CSharpResolver& resolver, const TS::IMethod& method,
    const TS::IMethod* invokeMethod,
    const ExpectedTargetDetails& expectedTargetDetails,
    IL::ILInstruction* thisArg)
{
    (void)self;
    if (method.IsLocalFunction())
    {
        IL::ILFunction* localFunction = expressionBuilder.ResolveLocalFunction(method);
        assert(localFunction != nullptr);
        DisambiguationOutcome out;
        out.addTypeArguments = true;
        out.methodName = localFunction->Name;
        out.result = CallBuilder::ToMethodGroup(method, *localFunction);
        return out;
    }
    const TS::IMethod* extInvoke = invokeMethod;
    if (method.IsExtensionMethod() && extInvoke != nullptr
        && method.Parameters().size() - 1 == extInvoke->Parameters().size())
    {
        // The extension-method arm.
        TS::ITypePtr targetType =
            const_cast<TS::IType&>(method.Parameters()[0]->Type()).shared_from_this();
        if (targetType->Kind() == TS::TypeKind::ByReference && thisArg != nullptr)
        {
            IL::ILInstruction* boxArgument = nullptr;
            TS::ITypePtr boxType;
            if (MatchBox(thisArg, boxArgument, boxType))
            {
                targetType = std::static_pointer_cast<TS::ByReferenceType>(targetType)
                                 ->Element();
                thisArg = boxArgument;
            }
        }
        TranslatedExpression target = expressionBuilder.Translate(thisArg, targetType.get());
        TranslatedExpression currentTarget = target;
        bool targetCasted = false;
        bool addTypeArguments = false;
        // Initial inputs for IsUnambiguousMethodReference:
        const Sem::ResolveResult* targetResolveResult = target.ResolveResult();
        std::vector<TS::ITypePtr> typeArguments;
        if (MatchLdNull(thisArg))
        {
            targetCasted = true;
            currentTarget = currentTarget.ConvertTo(*targetType, expressionBuilder);
            targetResolveResult = currentTarget.ResolveResult();
        }
        // Find somewhat minimal solution:
        std::shared_ptr<Sem::ResolveResult> result;
        while (!self.IsUnambiguousMethodReference(
            expectedTargetDetails, method, targetResolveResult, typeArguments,
            true, result))
        {
            if (!targetCasted)
            {
                // try casting target
                targetCasted = true;
                currentTarget = currentTarget.ConvertTo(*targetType, expressionBuilder);
                targetResolveResult = currentTarget.ResolveResult();
                continue;
            }
            if (!addTypeArguments)
            {
                // try adding type arguments
                addTypeArguments = true;
                typeArguments = method.TypeArguments();
                continue;
            }
            break;
        }
        DisambiguationOutcome out;
        out.target = currentTarget;
        out.addTypeArguments = addTypeArguments;
        out.methodName = method.Name();
        out.result = std::move(result);
        return out;
    }
    else
    {
        // Prepare call target
        TS::ITypePtr targetType = method.DeclaringType();
        if (targetType && targetType->IsReferenceType() == false && thisArg != nullptr)
        {
            // Normal struct instance method calls (which TranslateTarget is meant for) expect a 'ref T',
            // but delegate construction uses a 'box T'.
            IL::ILInstruction* boxArgument = nullptr;
            TS::ITypePtr boxType;
            if (MatchBox(thisArg, boxArgument, boxType))
            {
                IL::ILInstruction* ldobjTarget = nullptr;
                TS::ITypePtr ldobjType;
                if (MatchLdObj(boxArgument, ldobjTarget, ldobjType))
                {
                    thisArg = ldobjTarget;
                }
                else
                {
                    thisArg = new IL::AddressOf(
                        boxArgument ? boxArgument->Clone() : nullptr,
                        std::move(boxType));
                }
            }
        }
        TranslatedExpression target = expressionBuilder.TranslateTarget(
            thisArg,
            expectedTargetDetails.CallOpCode == IL::OpCode::Call,
            method.IsStatic(), *targetType);
        // check if target is required
        bool requireTarget = expressionBuilder.HidesVariableWithName(method.Name())
            || (method.IsStatic()
                    ? !expressionBuilder.IsCurrentOrContainingType(
                          method.DeclaringTypeDefinition())
                    : dynamic_cast<const Syntax::ThisReferenceExpression*>(
                          target.Expression()) == nullptr);
        // Try to find minimal expression
        // If target is required, include it from the start
        bool targetAdded = requireTarget;
        TranslatedExpression currentTarget = targetAdded ? target : TranslatedExpression();
        // Remember other decisions:
        bool targetCasted = false;
        bool addTypeArguments = false;
        // Initial inputs for IsUnambiguousMethodReference:
        const Sem::ResolveResult* targetResolveResult =
            targetAdded ? target.ResolveResult() : nullptr;
        std::vector<TS::ITypePtr> typeArguments;
        // Find somewhat minimal solution:
        std::shared_ptr<Sem::ResolveResult> result;
        while (!self.IsUnambiguousMethodReference(
            expectedTargetDetails, method, targetResolveResult, typeArguments,
            false, result))
        {
            if (!addTypeArguments)
            {
                // try adding type arguments
                addTypeArguments = true;
                typeArguments = method.TypeArguments();
                continue;
            }
            if (!targetAdded)
            {
                // try adding target
                targetAdded = true;
                currentTarget = target;
                targetResolveResult = target.ResolveResult();
                continue;
            }
            if (!targetCasted)
            {
                // try casting target
                targetCasted = true;
                currentTarget = currentTarget.ConvertTo(*targetType, expressionBuilder);
                targetResolveResult = currentTarget.ResolveResult();
                continue;
            }
            break;
        }
        if (auto* mgrr = dynamic_cast<Resolver::MethodGroupResolveResult*>(result.get()))
        {
            std::unique_ptr<Resolver::MethodGroupResolveResult> withChosen =
                mgrr->WithChosenMethod(&method);
            result = std::move(withChosen);
        }
        DisambiguationOutcome out;
        out.target = currentTarget;
        out.addTypeArguments = addTypeArguments;
        out.methodName = method.Name();
        out.result = std::move(result);
        return out;
    }
}

// The C# `ExpressionWithResolveResult BuildDelegateReference(IMethod method,
// IMethod? invokeMethod, ExpectedTargetDetails expectedTargetDetails,
// ILInstruction? thisArg)` (lines 2004-2026).
ExpressionWithResolveResult CallBuilder::BuildDelegateReference(
    const TS::IMethod& method, const TS::IMethod* invokeMethod,
    const ExpectedTargetDetails& expectedTargetDetails, IL::ILInstruction* thisArg)
{
    ExpressionBuilder& expressionBuilder = *expressionBuilder_;
    ExpressionWithResolveResult targetExpression;
    DisambiguationOutcome outcome = CallBuilderDisambiguateDelegateReference(
        *this, expressionBuilder, *resolver_, method, invokeMethod,
        expectedTargetDetails, thisArg);
    if (outcome.target.Expression() != nullptr)
    {
        auto* mre = new Syntax::MemberReferenceExpression(outcome.target.Expression(),
                                                         outcome.methodName);
        if (outcome.addTypeArguments)
        {
            for (TS::ITypePtr& ta : method.TypeArguments())
                mre->TypeArguments().Add(expressionBuilder.ConvertType(*ta));
        }
        return WithRR(*mre, std::move(outcome.result));
    }
    else
    {
        auto* ide = new Syntax::IdentifierExpression(outcome.methodName);
        if (outcome.addTypeArguments)
        {
            for (TS::ITypePtr& ta : method.TypeArguments())
                ide->TypeArguments().Add(expressionBuilder.ConvertType(*ta));
        }
        return WithRR(*ide, std::move(outcome.result));
    }
}

// The C# `TranslatedExpression HandleDelegateConstruction(IType delegateType,
// IMethod method, ExpectedTargetDetails expectedTargetDetails, ILInstruction
// thisArg, ILInstruction inst)` (lines 2138-2152).
TranslatedExpression CallBuilder::HandleDelegateConstruction(
    const TS::IType& delegateType, const TS::IMethod& method,
    const ExpectedTargetDetails& expectedTargetDetails, IL::ILInstruction* thisArg,
    IL::ILInstruction* inst)
{
    TS::ITypePtr delegateTypePtr =
        const_cast<TS::IType&>(delegateType).shared_from_this();
    const TS::IMethod* invokeMethod = TS::GetDelegateInvokeMethod(delegateType);
    ExpressionWithResolveResult targetExpression =
        BuildDelegateReference(method, invokeMethod, expectedTargetDetails, thisArg);
    auto* oce = new Syntax::ObjectCreateExpression(
        expressionBuilder_->ConvertType(const_cast<TS::IType&>(delegateType)));
    oce->Arguments().Add(targetExpression.Expression());
    // The C# `new ObjectCreateExpression(...).WithILInstruction(inst).WithRR(new
    // ConversionResolveResult(...))` chain: the IL annotation first, then the
    // conversion resolve result re-wraps the node as a TranslatedExpression.
    return WithRR(
        WithILInstruction(*oce, inst),
        std::make_shared<Sem::ConversionResolveResult>(
            std::move(delegateTypePtr),
            AliasResolveResult(targetExpression.ResolveResult()),
            Sem::Conversions::MethodGroupConversion(
                &method,
                expectedTargetDetails.CallOpCode == IL::OpCode::CallVirt, false)));
}

// The C# `bool IsUnambiguousMethodReference(ExpectedTargetDetails
// expectedTargetDetails, IMethod method, ResolveResult? target,
// IReadOnlyList<IType> typeArguments, bool isExtensionMethodReference,
// out ResolveResult? result)` (lines 2154-2200).
bool CallBuilder::IsUnambiguousMethodReference(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
    const Sem::ResolveResult* target,
    const std::vector<TS::ITypePtr>& typeArguments, bool isExtensionMethodReference,
    std::shared_ptr<Sem::ResolveResult>& result)
{
    const Resolver::CSharpResolver& resolver = *resolver_;

    std::unique_ptr<Resolver::OverloadResolution> or_;

    if (isExtensionMethodReference)
    {
        std::shared_ptr<Sem::ResolveResult> accessResult = resolver.ResolveMemberAccess(
            AliasResolveResult(target), method.Name(), typeArguments,
            Resolver::NameLookupMode::InvocationTarget);
        auto* mgrr = dynamic_cast<Resolver::MethodGroupResolveResult*>(
            accessResult.get());
        if (mgrr == nullptr)
        {
            result = nullptr;
            return false;
        }
        or_ = mgrr->PerformOverloadResolution(
            resolver.Compilation(),
            [&method]()
            {
                std::vector<std::shared_ptr<Sem::ResolveResult>> args;
                for (const TS::IParameter* p : method.Parameters())
                    args.push_back(std::make_shared<Sem::TypeResolveResult>(
                        const_cast<TS::IType&>(p->Type()).shared_from_this()));
                return args;
            }(),
            std::nullopt, /*allowExtensionMethods*/ true);
        if (or_ == nullptr || or_->IsAmbiguous())
        {
            result = nullptr;
            return false;
        }
    }
    else
    {
        std::vector<std::shared_ptr<Sem::ResolveResult>> args;
        for (const TS::IParameter* p : method.Parameters())
            args.push_back(std::make_shared<Sem::TypeResolveResult>(
                        const_cast<TS::IType&>(p->Type()).shared_from_this()));
        or_ = std::make_unique<Resolver::OverloadResolution>(
            resolver.Compilation(), std::move(args), std::nullopt,
            typeArguments.empty() ? std::nullopt
                                  : std::optional<std::vector<TS::ITypePtr>>(typeArguments),
            &resolver.Conversions());
        if (target == nullptr)
        {
            result = resolver.ResolveSimpleName(method.Name(), typeArguments, false);
            auto* mgrr = dynamic_cast<Resolver::MethodGroupResolveResult*>(result.get());
            if (mgrr == nullptr)
                return false;
            or_->AddMethodLists(mgrr->MethodsGroupedByDeclaringType());
        }
        else
        {
            Resolver::MemberLookup lookup(
                resolver.CurrentTypeDefinition(),
                resolver.CurrentTypeDefinition()->ParentModule());
            result = lookup.Lookup(*target, method.Name(), typeArguments, false);
            auto* mgrr = dynamic_cast<Resolver::MethodGroupResolveResult*>(result.get());
            if (mgrr == nullptr)
                return false;
            or_->AddMethodLists(mgrr->MethodsGroupedByDeclaringType());
        }
    }

    const TS::IParameterizedMember* foundMember =
        or_->GetBestCandidateWithSubstitutedTypeArguments();
    const TS::IMethod* foundMethod =
        dynamic_cast<const TS::IMethod*>(foundMember);
    if (!IsAppropriateCallTarget(expectedTargetDetails, method, foundMethod))
    {
        result = nullptr;
        return false;
    }
    // The C# `return result is MethodGroupResolveResult`.
    return dynamic_cast<Resolver::MethodGroupResolveResult*>(result.get()) != nullptr;
}

// The C# `internal TranslatedExpression Build(LdVirtDelegate inst)` (lines
// 1969-1971).
TranslatedExpression CallBuilder::Build(const IL::LdVirtDelegate& inst)
{
    assert(inst.Method != nullptr);
    ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;
    return HandleDelegateConstruction(*inst.Type, *inst.Method, details,
                                      inst.Argument.get(),
                                      const_cast<IL::LdVirtDelegate*>(&inst));
}

// The C# `internal ExpressionWithResolveResult BuildMethodReference(IMethod
// method, bool isVirtual)` (lines 1973-1977).
ExpressionWithResolveResult CallBuilder::BuildMethodReference(
    const TS::IMethod& method, bool isVirtual)
{
    ExpectedTargetDetails details;
    details.CallOpCode = isVirtual ? IL::OpCode::CallVirt : IL::OpCode::Call;
    ExpressionWithResolveResult expr =
        BuildDelegateReference(method, nullptr, details, nullptr);
    // The C# `expr.Expression.RemoveAnnotations<ResolveResult>()` -- the
    // resolve-result annotation channel is REPLACED with the fresh plain
    // MemberResolveResult over a null target.
    expr.Expression()->RemoveAnnotations<Sem::ResolveResult>();
    return WithRR(*expr.Expression(),
                  std::make_shared<Sem::MemberResolveResult>(nullptr, &method));
}

} // namespace ILSpy::Decompiler::CSharp
