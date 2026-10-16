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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/IntroduceExtensionMethods.cs -- the
// transform that re-renders a static extension-method call `C.M(x, args)` (or the bare
// `M(x, args)` form) as the extension syntax `x.M(args)`:
//   * `Run` builds a per-run `CSharpResolver` from the syntax-tree root's `UsingScope`
//     annotation (attached by `IntroduceUsingDeclarations`, which must run first) and the
//     current type definition's namespace, then walks the tree with this visitor.
//   * `VisitNamespaceDeclaration` descends the resolver's using scope by the declaration's
//     dotted name; `VisitTypeDeclaration` switches the resolver's current type definition.
//   * `VisitInvocationExpression` asks `CanTransformToExtensionMethodCall` whether the
//     invocation is an eligible extension-method call, then rewrites the invocation's
//     target: the first argument becomes the receiver, the target method's name (with its
//     type arguments) the member, and the receiver a `ref`/`out`-unwrapped or `null`-cast
//     operand; the `CSharpInvocationResolveResult` annotation is replaced by an
//     `IsExtensionMethodInvocation` copy.
//
// All prerequisites are ported: `CSharpResolver.CanTransformToExtensionMethodCall`, the
// `CSharpInvocationResolveResult`, `CSharpConversions`, `UsingScope` /
// `CSharpTypeResolveContext`, and `TransformContext`. The `UsingScope` annotation is
// carried by the port's `UsingScopeAnnotation` holder (Annotations.hpp); the C#
// `IntroduceUsingDeclarations` pass that attaches it is not ported yet, so until it lands
// the caller (or a test) must attach the scope via `WithUsingScope`.

#pragma once

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {
class AstNode;
class AstType;
class Expression;
class IdentifierExpression;
class InvocationExpression;
class MemberReferenceExpression;
class NamespaceDeclaration;
class TypeDeclaration;
} // namespace ILSpy::Decompiler::CSharp::Syntax

namespace ILSpy::Decompiler::Semantics {
class ResolveResult;
} // namespace ILSpy::Decompiler::Semantics

namespace ILSpy::Decompiler::CSharp::Resolver {
class CSharpConversions;
class CSharpResolver;
} // namespace ILSpy::Decompiler::CSharp::Resolver

namespace ILSpy::Decompiler::CSharp::Transforms {

class TransformContext;

// The C# `public class IntroduceExtensionMethods : DepthFirstAstVisitor, IAstTransform`.
class IntroduceExtensionMethods : public Syntax::DepthFirstAstVisitor, public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: stores the
    // context, resolves the per-compilation `CSharpConversions`, initializes the resolver
    // from the root's `UsingScope` annotation, and walks the tree.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public override void VisitNamespaceDeclaration(NamespaceDeclaration)`: descends
    // the resolver's using scope by each identifier of the declaration, recurses, then
    // restores the previous resolver.
    void VisitNamespaceDeclaration(Syntax::NamespaceDeclaration* namespaceDeclaration) override;

    // The C# `public override void VisitTypeDeclaration(TypeDeclaration)`: switches the
    // resolver's current type definition to the visited type, recurses, then restores it.
    void VisitTypeDeclaration(Syntax::TypeDeclaration* typeDeclaration) override;

    // The C# `public override void VisitInvocationExpression(InvocationExpression)`: recurses
    // first, then rewrites an eligible extension-method call to the extension syntax.
    void VisitInvocationExpression(Syntax::InvocationExpression* invocationExpression) override;

    // The C# `static bool CanTransformToExtensionMethodCall(CSharpResolver, InvocationExpression,
    // out MemberReferenceExpression?, out ResolveResult?, out Expression?)` -- the AST-shape
    // gate plus the resolver's eligibility decision. `memberRefExpr` is null for the
    // `IdentifierExpression` target shape (the caller then builds a fresh member reference).
    static bool CanTransformToExtensionMethodCall(
        Resolver::CSharpResolver& resolver,
        Syntax::InvocationExpression& invocationExpression,
        Syntax::MemberReferenceExpression*& memberRefExpr,
        std::shared_ptr<Semantics::ResolveResult>& target,
        Syntax::Expression*& firstArgument);

private:
    // The C# `void InitializeContext(UsingScope usingScope)`: descends the scope by the
    // current type definition's namespace and builds the run resolver. The parameter is a
    // type-erased `shared_ptr<void>` so this header does not name (and therefore does not
    // declare) the sibling `CSharp::TypeSystem` namespace -- that would shadow unqualified
    // `TypeSystem::` lookups in every translation unit that includes this header. The .cpp
    // casts it back to `TypeSystem::UsingScope`.
    void InitializeContext(std::shared_ptr<void> usingScope);

    // The C# `[AllowNull] TransformContext context` run state (null outside a run).
    TransformContext* context_ = nullptr;
    // The C# `[AllowNull] CSharpResolver resolver` run state (replaced while descending the
    // namespace/type nesting; null outside a run).
    std::shared_ptr<Resolver::CSharpResolver> resolver_;
    // The C# `[AllowNull] CSharpConversions conversions` (the per-compilation instance; null
    // outside a run).
    Resolver::CSharpConversions* conversions_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
