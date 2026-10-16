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

#include "Decompiler/CSharp/Transforms/CustomPatterns.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/INamedElement.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

namespace PM = Syntax::PatternMatching;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// The `UnknownType` class and the `UnknownType()` convenience function share the
// name in `TypeSystem` (the C++ tag-vs-ordinary-namespace distinction). The
// using-declaration makes the elaborated-type-specifier (`class UnknownType`) in
// the dynamic_cast below resolve to the CLASS (the TypeSystemAstBuilder.cpp
// precedent).
using TS::UnknownType;

// The C# `IType.Namespace` (an `IType : INamedElement` member with the `AbstractType`
// empty default) for the shapes the introduced patterns reach: an `IEntity`-carrying
// type (a type definition) reports `INamedElement::Namespace()`; a `ParameterizedType`
// delegates to its generic; an `UnknownType` reports its stored full type name's
// namespace; anything else is the empty default. Mirrors the identical helper in
// TypeSystemAstBuilder.cpp, which is file-local there.
std::string TypeNamespaceOf(const TS::IType& type) {
    if (const auto* named = dynamic_cast<const TS::INamedElement*>(&type))
        return named->Namespace();
    if (const auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(&type))
        return parameterized->GenericType() ? TypeNamespaceOf(*parameterized->GenericType())
                                            : std::string();
    if (const auto* unknown = dynamic_cast<const class UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    return std::string();
}

} // namespace

TypePattern::TypePattern(std::string namespaceName, std::string name)
    : namespace_(std::move(namespaceName)), name_(std::move(name)) {}

bool TypePattern::DoMatch(PM::INode* other, PM::Match match) {
    // The C# special case: a `ComposedType` that carries no modifiers is treated as
    // its `BaseType` for the annotation lookup (ILSpy sometimes leaves a
    // specifier-less `ComposedType` behind).
    Syntax::AstType* type = nullptr;
    auto* composed = dynamic_cast<Syntax::ComposedType*>(other);
    if (composed != nullptr && !composed->HasRefSpecifier()
        && !composed->HasNullableSpecifier() && composed->PointerRank() == 0
        && composed->ArraySpecifiers().Count() == 0) {
        type = composed->BaseType();
    } else {
        type = dynamic_cast<Syntax::AstType*>(other);
        if (type == nullptr)
            return false;
    }
    if (type == nullptr)
        return false;
    const Sem::ResolveResult* resolveResult = CSharp::GetResolveResult(*type);
    const auto* typeResolveResult =
        dynamic_cast<const Sem::TypeResolveResult*>(resolveResult);
    if (typeResolveResult == nullptr)
        return false;
    const TS::IType& resolved = typeResolveResult->Type();
    return TypeNamespaceOf(resolved) == namespace_ && resolved.Name() == name_;
}

LdTokenPattern::LdTokenPattern(std::string groupName)
    : childNode_(std::make_unique<PM::AnyNode>(std::move(groupName))) {}

bool LdTokenPattern::DoMatch(PM::INode* other, PM::Match match) {
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(other);
    if (invocation != nullptr
        && invocation->Annotation<CSharp::LdTokenAnnotation>() != nullptr
        && invocation->Arguments().Count() == 1) {
        return childNode_->DoMatch(invocation->Arguments()[0], match);
    }
    return false;
}

TypeOfPattern::TypeOfPattern(std::string groupName) {
    // The C# ctor builds `Type.GetTypeFromHandle(typeof(<groupName>)).TypeHandle`
    // with the `Type` reference itself a `TypePattern` placeholder.
    auto* typeReference = new Syntax::TypeReferenceExpression(
        PM::PatternExtensions::ToType(
            std::make_shared<TypePattern>("System", "Type")));
    auto* getTypeFromHandle = new Syntax::MemberReferenceExpression(
        typeReference, std::string("GetTypeFromHandle"));
    auto* invocation = new Syntax::InvocationExpression(getTypeFromHandle);
    invocation->Arguments().Add(new Syntax::TypeOfExpression(
        PM::PatternExtensions::ToType(std::make_shared<PM::AnyNode>(
            std::move(groupName)))));
    childNode_ = std::unique_ptr<PM::INode>(
        new Syntax::MemberReferenceExpression(invocation, std::string("TypeHandle")));
}

bool TypeOfPattern::DoMatch(PM::INode* other, PM::Match match) {
    return childNode_->DoMatch(other, match);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
