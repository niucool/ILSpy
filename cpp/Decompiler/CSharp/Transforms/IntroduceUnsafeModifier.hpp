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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.cs -- the
// AST transform that marks the declaring entities of pointer / function-pointer / by-ref
// / array-typed expressions (and the syntactic `sizeof`/`stackalloc`/`->`/`fixed` forms)
// with the `unsafe` modifier.
//
// The C# class is `class IntroduceUnsafeModifier : DepthFirstAstVisitor<bool>, IAstTransform`.
// The transform has two entry points: `Run` (the `IAstTransform` contract, storing the
// context so the insertions can be step-recorded) and the static `IsUnsafe` (a one-shot
// query the resolver/ExpressionBuilder uses to test whether a subtree requires an unsafe
// context). Both drive the same `bool`-returning visitor: the per-node visits return
// `true` when the node (or a descendant) needs an unsafe context, and the overridden
// `VisitChildren` ORs the children's results and, when any child reported unsafe, adds
// `Modifiers.Unsafe` to the enclosing `EntityDeclaration` (except an `Accessor`, whose
// parent member absorbs the modifier).
//
// The port keeps the public class name and both entry points. `IsUnsafeType` maps the C#
// `TypeWithElementType` cast to the port's concrete `ArrayType`/`ByReferenceType` leaf
// classes (the port flattens `TypeWithElementType`, the documented minimal-port
// divergence); the `Kind` switch is otherwise faithful.

#pragma once

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorBool.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class TransformContext;

// The C# `public class IntroduceUnsafeModifier : DepthFirstAstVisitor<bool>, IAstTransform`.
class IntroduceUnsafeModifier : public Syntax::DepthFirstAstVisitorBool, public IAstTransform {
public:
    // The C# `public void Run(AstNode compilationUnit, TransformContext context)`: stores
    // the context, drives the visitor over the root, and clears the context on the way out
    // (the C# `try/finally`).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public static bool IsUnsafe(AstNode node)`: a context-less one-shot visitor
    // query. The static entry point drives the visitor with no context, so the step
    // recording is skipped (the visitor guards a null context).
    static bool IsUnsafe(Syntax::AstNode& node);

    // The C# per-node arms. Every arm returns `true` when the node (or a descendant)
    // requires an unsafe context. The `VisitUnaryOperatorExpression` and
    // `VisitMemberReferenceExpression` arms also rewrite the pointer dereference/add and
    // dereference/member-access shapes into the indexer/`->` forms.
    bool VisitPointerReferenceExpression(Syntax::PointerReferenceExpression* pointerReferenceExpression) override;
    bool VisitSizeOfExpression(Syntax::SizeOfExpression* sizeOfExpression) override;
    bool VisitComposedType(Syntax::ComposedType* composedType) override;
    bool VisitFunctionPointerType(Syntax::FunctionPointerAstType* functionPointerType) override;
    bool VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression* unaryOperatorExpression) override;
    bool VisitMemberReferenceExpression(Syntax::MemberReferenceExpression* memberReferenceExpression) override;
    bool VisitIdentifierExpression(Syntax::IdentifierExpression* identifierExpression) override;
    bool VisitStackAllocExpression(Syntax::StackAllocExpression* stackAllocExpression) override;
    bool VisitInvocationExpression(Syntax::InvocationExpression* invocationExpression) override;
    bool VisitObjectCreateExpression(Syntax::ObjectCreateExpression* objectCreateExpression) override;
    bool VisitFixedVariableInitializer(Syntax::FixedVariableInitializer* fixedVariableInitializer) override;

protected:
    // The C# `protected override bool VisitChildren(AstNode node)`: ORs every child's
    // accept-result (storing the next sibling before the accept, so a child replacement or
    // removal during the walk cannot strand the loop), then, when any child reported
    // unsafe, adds `Modifiers.Unsafe` to an enclosing `EntityDeclaration` that is not an
    // `Accessor` and returns `false` (the modifier absorbs the report for this level).
    bool VisitChildren(Syntax::AstNode* node) override;

private:
    // The C# `private bool HasUnsafeResolveResult(AstNode node)`: whether the node's
    // resolve result -- or the member/parameters of a member or method-group result -- is
    // an unsafe type.
    bool HasUnsafeResolveResult(const Syntax::AstNode& node);

    // The C# `private bool IsUnsafeType(IType type)`: pointer and function-pointer types
    // are unsafe; array and by-reference types recurse into their element type; everything
    // else is not.
    static bool IsUnsafeType(const ILSpy::Decompiler::TypeSystem::IType& type);

    // The C# `TransformContext? context` -- null while the static `IsUnsafe` entry point
    // drives the visitor (the C# `Run` sets it and the `finally` clears it).
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
