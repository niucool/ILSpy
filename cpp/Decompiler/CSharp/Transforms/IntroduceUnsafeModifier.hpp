// Copyright (c) 2026 Jun Cai
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

// Port of `ICSharpCode.Decompiler.CSharp.Transforms.IntroduceUnsafeModifier`
// (IntroduceUnsafeModifier.cs): the IAstTransform adding the `unsafe` modifier
// to every member declaration whose body uses pointer constructs -- the
// pointer-reference / sizeof / pointer-rank / function-pointer syntax, the
// dereference and address-of operators, the fixed initializer, and any
// expression whose resolve result carries a pointer-typed member or operand.
// The two pointer-expression rewrites ride along: `*(ptr + i)` becomes
// `ptr[i]` (the pointer-addition indexer form), and `(*ptr).Member` becomes
// `ptr->Member` (the pointer member access).

#pragma once

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorBool.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::TypeSystem {
class IType;
}

namespace ILSpy::Decompiler::CSharp::Transforms {

class IntroduceUnsafeModifier final
    : public Syntax::DepthFirstAstVisitorBool,
      public IAstTransform {
public:
    // The C# `public void Run(AstNode compilationUnit, TransformContext
    // context)`: the context lives for the walk (the C# try/finally nulls it
    // after).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public static bool IsUnsafe(AstNode node)`: the no-context
    // visitor drive (the syntax-level probe).
    static bool IsUnsafe(Syntax::AstNode* node);

    // The C# `protected override bool VisitChildren(AstNode node)`: the
    // OR-reduce over the children's results; a member declaration (not an
    // accessor) whose subtree is unsafe gets the `unsafe` modifier (and
    // stops the propagation -- the declaration is the boundary).
    bool VisitChildren(Syntax::AstNode* node) override;

    // The C# `VisitPointerReferenceExpression`: `p->M` needs unsafe.
    bool VisitPointerReferenceExpression(
        Syntax::PointerReferenceExpression* node) override;
    // The C# `VisitSizeOfExpression`: sizeof(MyStruct) needs unsafe.
    bool VisitSizeOfExpression(Syntax::SizeOfExpression* node) override;
    // The C# `VisitComposedType`: a `T*` type reference needs unsafe.
    bool VisitComposedType(Syntax::ComposedType* node) override;
    // The C# `VisitFunctionPointerType` (the FunctionPointerAstType node):
    // a function-pointer type needs unsafe (no children walk).
    bool VisitFunctionPointerType(Syntax::FunctionPointerAstType* node) override;
    // The C# `VisitUnaryOperatorExpression`: the dereference (with the
    // pointer-addition indexer rewrite) and the address-of need unsafe.
    bool VisitUnaryOperatorExpression(
        Syntax::UnaryOperatorExpression* node) override;
    // The C# `VisitMemberReferenceExpression` (with the `(*p).M` ->
    // `p->M` rewrite) and the resolve-result arms.
    bool VisitMemberReferenceExpression(
        Syntax::MemberReferenceExpression* node) override;
    bool VisitIdentifierExpression(
        Syntax::IdentifierExpression* node) override;
    bool VisitStackAllocExpression(
        Syntax::StackAllocExpression* node) override;
    bool VisitInvocationExpression(
        Syntax::InvocationExpression* node) override;
    bool VisitObjectCreateExpression(
        Syntax::ObjectCreateExpression* node) override;
    // The C# `VisitFixedVariableInitializer`: the fixed statement needs
    // unsafe.
    bool VisitFixedVariableInitializer(
        Syntax::FixedVariableInitializer* node) override;

private:
    // The C# `private bool HasUnsafeResolveResult(AstNode node)`: the
    // resolve result's type is unsafe, or the resolved member's parameters
    // / return type are.
    static bool HasUnsafeResolveResult(const Syntax::AstNode& node);

    // The C# `private bool IsUnsafeType(IType type)`: the pointer /
    // function-pointer kinds, recursing through the array / by-reference
    // element types.
    static bool IsUnsafeType(const ::ILSpy::Decompiler::TypeSystem::IType*
                                 type);

    // The C# `[AllowNull] TransformContext context` (null on the static
    // IsUnsafe drive).
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
