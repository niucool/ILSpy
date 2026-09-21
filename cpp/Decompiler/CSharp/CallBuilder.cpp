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
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>

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
