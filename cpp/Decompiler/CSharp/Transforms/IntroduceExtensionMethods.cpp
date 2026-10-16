// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The IntroduceExtensionMethods implementation (see the header for the port notes).
//
// Include order is load-bearing: Annotations.hpp reaches TranslatedExpression.hpp's
// unqualified `TypeSystem::IType` (the outer Decompiler::TypeSystem), so it must be parsed
// before UsingScopeAnnotation.hpp / UsingScope.hpp declare the sibling CSharp::TypeSystem
// namespace and shadow that lookup.
#include "Decompiler/CSharp/Annotations.hpp"

#include "Decompiler/CSharp/Transforms/IntroduceExtensionMethods.hpp"
#include "Decompiler/CSharp/UsingScopeAnnotation.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace TSys = ILSpy::Decompiler::CSharp::TypeSystem;

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

// The owning `shared_ptr` handle behind a node's `ResolveResult` annotation (the C#
// `node.GetResolveResult()` returns the annotated result, or the
// `ErrorResolveResult.UnknownError` singleton when none is attached). The port's
// annotation channel stores the result in a `shared_ptr<AnnotationBase>`, so a consumer
// that needs to keep or pass the result by value reads the handle here; a missing
// annotation yields an aliasing handle to the `UnknownError` singleton (the
// `UseImplicitlyTypedOutAnnotation::SharedInstance` convention).
std::shared_ptr<Sem::ResolveResult> GetResolveResultHandle(const Syntax::AstNode& node) {
    for (const auto& annotation : node.SharedAnnotations()) {
        if (dynamic_cast<Sem::ResolveResult*>(annotation.get()) != nullptr)
            return std::static_pointer_cast<Sem::ResolveResult>(annotation);
    }
    return std::shared_ptr<Sem::ResolveResult>(
        const_cast<Sem::ErrorResolveResult*>(&Sem::ErrorResolveResult::UnknownError()),
        [](Sem::ResolveResult*) {});
}

} // namespace

// The C# `public void Run(AstNode rootNode, TransformContext context)`.
void IntroduceExtensionMethods::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    context_ = &context;
    conversions_ = &Resolver::CSharpConversions::Get(context.TypeSystem());
    std::shared_ptr<TSys::UsingScope> usingScope = GetUsingScope(rootNode);
    InitializeContext(std::move(usingScope));
    rootNode.AcceptVisitor(*this);
}

// The C# `void InitializeContext(UsingScope usingScope)`.
void IntroduceExtensionMethods::InitializeContext(std::shared_ptr<void> usingScopeErasure) {
    std::shared_ptr<TSys::UsingScope> usingScope =
        std::static_pointer_cast<TSys::UsingScope>(std::move(usingScopeErasure));
    if (usingScope == nullptr) {
        // The C# `rootNode.Annotation<UsingScope>()!` -- the null-forgiving deref throws
        // a NullReferenceException when the annotation is missing (IntroduceUsingDeclarations
        // must have run first).
        throw std::logic_error(
            "IntroduceExtensionMethods.Run: the root node carries no UsingScope annotation");
    }
    const std::string& currentNamespace =
        context_->CurrentTypeDefinition() != nullptr
            ? context_->CurrentTypeDefinition()->Namespace()
            : std::string();
    if (!currentNamespace.empty()) {
        std::string part;
        for (std::size_t i = 0; i <= currentNamespace.size(); i++) {
            if (i == currentNamespace.size() || currentNamespace[i] == '.') {
                usingScope = usingScope->WithNestedNamespace(part);
                part.clear();
            } else {
                part.push_back(currentNamespace[i]);
            }
        }
    }
    auto currentContext = std::make_shared<TSys::CSharpTypeResolveContext>(
        context_->TypeSystem().MainModule(), std::move(usingScope),
        context_->CurrentTypeDefinition());
    resolver_ = std::make_shared<Resolver::CSharpResolver>(std::move(currentContext));
}

// The C# `public override void VisitNamespaceDeclaration(NamespaceDeclaration)`.
void IntroduceExtensionMethods::VisitNamespaceDeclaration(
    Syntax::NamespaceDeclaration* namespaceDeclaration) {
    std::shared_ptr<TSys::UsingScope> usingScope = resolver_->CurrentUsingScope();
    for (const std::string& ident : namespaceDeclaration->Identifiers())
        usingScope = usingScope->WithNestedNamespace(ident);
    std::shared_ptr<Resolver::CSharpResolver> previousResolver = resolver_;
    resolver_ = resolver_->WithCurrentUsingScope(std::move(usingScope));
    try {
        Syntax::DepthFirstAstVisitor::VisitNamespaceDeclaration(namespaceDeclaration);
    } catch (...) {
        resolver_ = std::move(previousResolver);
        throw;
    }
    resolver_ = std::move(previousResolver);
}

// The C# `public override void VisitTypeDeclaration(TypeDeclaration)`.
void IntroduceExtensionMethods::VisitTypeDeclaration(
    Syntax::TypeDeclaration* typeDeclaration) {
    std::shared_ptr<Resolver::CSharpResolver> previousResolver = resolver_;
    const auto* typeDefinition =
        dynamic_cast<const TS::ITypeDefinition*>(GetSymbol(*typeDeclaration));
    resolver_ = resolver_->WithCurrentTypeDefinition(typeDefinition);
    try {
        Syntax::DepthFirstAstVisitor::VisitTypeDeclaration(typeDeclaration);
    } catch (...) {
        resolver_ = std::move(previousResolver);
        throw;
    }
    resolver_ = std::move(previousResolver);
}

// The C# `public override void VisitInvocationExpression(InvocationExpression)`.
void IntroduceExtensionMethods::VisitInvocationExpression(
    Syntax::InvocationExpression* invocationExpression) {
    Syntax::DepthFirstAstVisitor::VisitInvocationExpression(invocationExpression);
    Syntax::MemberReferenceExpression* memberRefExpr = nullptr;
    std::shared_ptr<Sem::ResolveResult> target;
    Syntax::Expression* firstArgument = nullptr;
    if (!CanTransformToExtensionMethodCall(*resolver_, *invocationExpression, memberRefExpr,
                                           target, firstArgument)) {
        return;
    }
    // `CanTransformToExtensionMethodCall` returned true only when the target symbol is an
    // `IMethod`.
    const auto* method =
        dynamic_cast<const TS::IMethod*>(GetSymbol(*invocationExpression));
    bool stepped = false;
    if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(firstArgument)) {
        if (!context_->Settings().RefExtensionMethods()
            || dirExpr->FieldDirection() == Syntax::FieldDirection::Out) {
            return;
        }
        context_->Step("Introduce extension method call", invocationExpression);
        stepped = true;
        // A ref/out direction expression always wraps an operand.
        firstArgument = dirExpr->Expression();
        target = GetResolveResultHandle(*firstArgument);
        Syntax::Detach(dirExpr);
    } else if (dynamic_cast<Syntax::NullReferenceExpression*>(firstArgument) != nullptr) {
        context_->Step("Introduce extension method call", invocationExpression);
        stepped = true;
        // The replacement is a freshly created CastExpression, so the result is non-null.
        TS::ITypePtr receiverType =
            std::const_pointer_cast<TS::IType>(method->Parameters()[0]->Type().shared_from_this());
        firstArgument = static_cast<Syntax::Expression*>(firstArgument->ReplaceWith(
            [&](Syntax::AstNode* expr) -> Syntax::AstNode* {
                return new Syntax::CastExpression(
                    context_->TypeSystemAstBuilder().ConvertType(*receiverType),
                    static_cast<Syntax::Expression*>(expr));
            }));
    }
    if (auto* identifierExpression =
            dynamic_cast<Syntax::IdentifierExpression*>(invocationExpression->Target())) {
        if (!stepped)
            context_->Step("Introduce extension method call", invocationExpression);
        Syntax::Detach(identifierExpression);
        auto* newMemberReference = new Syntax::MemberReferenceExpression(
            Syntax::Detach(firstArgument), method->Name());
        const int typeArgumentCount = identifierExpression->TypeArguments().Count();
        for (int i = 0; i < typeArgumentCount; i++) {
            newMemberReference->TypeArguments().Add(
                Syntax::Detach(identifierExpression->TypeArguments()[i]));
        }
        memberRefExpr = newMemberReference;
        invocationExpression->Target(newMemberReference);
    } else {
        if (!stepped)
            context_->Step("Introduce extension method call", invocationExpression);
        // The target is not an IdentifierExpression, so CanTransformToExtensionMethodCall
        // matched the MemberReferenceExpression case and memberRefExpr is non-null.
        memberRefExpr->Target(Syntax::Detach(firstArgument));
    }
    if (const auto* irr = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
            GetResolveResult(*invocationExpression))) {
        // Do not forget to update the CSharpInvocationResolveResult: set
        // IsExtensionMethodInvocation == true. The port builds the replacement BEFORE
        // removing the old annotation: the C# GC keeps `irr` alive through the removal, but
        // the port's annotation channel owns the result by shared_ptr, so dropping the last
        // reference first would free the object the copies below read from.
        auto newResolveResult = std::make_shared<Resolver::CSharpInvocationResolveResult>(
            irr->TargetResultHandle(), irr->Member(), irr->Arguments(),
            irr->OverloadResolutionErrors(),
            /*isExtensionMethodInvocation*/ true, irr->IsExpandedForm(),
            irr->IsDelegateInvocation(), irr->GetArgumentToParameterMap(),
            irr->InitializerStatements());
        invocationExpression->RemoveAnnotations<Resolver::CSharpInvocationResolveResult>();
        invocationExpression->AddAnnotation(std::move(newResolveResult));
    }
}

// The C# `static bool CanTransformToExtensionMethodCall(CSharpResolver resolver,
// InvocationExpression invocationExpression, out MemberReferenceExpression? memberRefExpr,
// out ResolveResult? target, out Expression? firstArgument)`.
bool IntroduceExtensionMethods::CanTransformToExtensionMethodCall(
    Resolver::CSharpResolver& resolver,
    Syntax::InvocationExpression& invocationExpression,
    Syntax::MemberReferenceExpression*& memberRefExpr,
    std::shared_ptr<Sem::ResolveResult>& target,
    Syntax::Expression*& firstArgument) {
    const auto* method =
        dynamic_cast<const TS::IMethod*>(GetSymbol(invocationExpression));
    memberRefExpr = nullptr;
    target = nullptr;
    firstArgument = nullptr;
    if (method == nullptr || !method->IsExtensionMethod()
        || invocationExpression.Arguments().Count() == 0) {
        return false;
    }
    std::vector<TS::ITypePtr> typeArguments;
    if (auto* mre =
            dynamic_cast<Syntax::MemberReferenceExpression*>(invocationExpression.Target())) {
        typeArguments = mre->TypeArguments().Count() > 0 ? method->TypeArguments()
                                                         : std::vector<TS::ITypePtr>{};
        memberRefExpr = mre;
    } else if (auto* ide =
                   dynamic_cast<Syntax::IdentifierExpression*>(invocationExpression.Target())) {
        typeArguments = ide->TypeArguments().Count() > 0 ? method->TypeArguments()
                                                         : std::vector<TS::ITypePtr>{};
        memberRefExpr = nullptr;
    } else {
        return false;
    }

    firstArgument = invocationExpression.Arguments()[0];
    if (dynamic_cast<Syntax::NamedArgumentExpression*>(firstArgument) != nullptr)
        return false;
    target = GetResolveResultHandle(*firstArgument);
    if (const auto* crr = dynamic_cast<const Sem::ConstantResolveResult*>(target.get());
        crr != nullptr && !crr->ConstantValue().has_value()) {
        target = std::make_shared<Sem::ConversionResolveResult>(
            std::const_pointer_cast<TS::IType>(
                method->Parameters()[0]->Type().shared_from_this()),
            target, Sem::Conversions::NullLiteralConversion());
    } else if (auto* de = dynamic_cast<Syntax::DirectionExpression*>(firstArgument)) {
        target = GetResolveResultHandle(*de->Expression());
    }

    std::vector<std::shared_ptr<Sem::ResolveResult>> args(
        static_cast<std::size_t>(invocationExpression.Arguments().Count() - 1));
    std::optional<std::vector<std::string>> argNames;
    int pos = 0;
    for (int i = 1; i < invocationExpression.Arguments().Count(); i++) {
        Syntax::Expression* arg = invocationExpression.Arguments()[i];
        if (auto* nae = dynamic_cast<Syntax::NamedArgumentExpression*>(arg)) {
            if (!argNames.has_value())
                argNames = std::vector<std::string>(args.size());
            (*argNames)[static_cast<std::size_t>(pos)] = nae->Name();
            args[static_cast<std::size_t>(pos)] = GetResolveResultHandle(*nae->Expression());
        } else {
            args[static_cast<std::size_t>(pos)] = GetResolveResultHandle(*arg);
        }
        pos++;
    }
    return resolver.CanTransformToExtensionMethodCall(
        *method, std::move(typeArguments), target, args, argNames);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
