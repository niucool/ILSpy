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
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

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
