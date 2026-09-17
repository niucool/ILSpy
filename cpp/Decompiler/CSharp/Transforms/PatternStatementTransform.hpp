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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs -- the
// pattern-matching AST transform that recognizes the compiler's lowered statement shapes and
// rewrites them back to the high-level language forms (a `while`/`for` loop back to `for`,
// the array/`for` loop back to `foreach`, the getter/setter pair back to an automatic
// property, the `Finalize` method back to a destructor, the nested `try { try {} catch {} }
// finally {}` back to a single try-catch-finally, a cascading `if`/`else { if }` back to
// `else if`, and the `!`+`==`/associative conditional-logic forms back to their canonical
// spelling).
//
// The C# type is `sealed class PatternStatementTransform : ContextTrackingVisitor<AstNode>,
// IAstTransform`. The port derives from `ContextTrackingVisitor` (the `TResult = AstNode`
// instantiation) and overrides `Run`, `VisitChildren`, and the per-node visits the ported
// sub-transforms need.
//
// The port is landed in slices. The skeleton (`Run`, `VisitChildren`) and the two
// purely-structural sub-transforms that need no pattern tree landed first: the
// conditional-logic reassociation (`a && (b && c)` -> `(a && b) && c`) and the negated
// equality rewrite (`!(a == b)` -> `a != b`). This slice adds the pattern-based
// sub-transforms that need no `DeclareVariables` analysis and no resolver: the destructor
// (`Finalize` method / `try { ... } finally { base.Finalize(); }` body), the nested
// `try { try {} catch {} } finally {}` merge, and the cascading `if`/`else { if }`
// simplification. The remaining pattern-based sub-transforms (the `for`/`foreach` loops,
// the automatic property/event rewrites) and the `DeclareVariables` analysis they compose
// stay deferred -- each is named at the visit that would call it.

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Transforms/ContextTrackingVisitor.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

// Forward declarations of the node types the ported sub-transforms take and return. Their
// definitions are pulled into the .cpp (the visitor overrides only need the pointer types).
namespace ILSpy::Decompiler::CSharp::Syntax {
class DestructorDeclaration;
class IfElseStatement;
class MethodDeclaration;
class TryCatchStatement;
}

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public sealed class PatternStatementTransform`. `final` (the C# `sealed`).
class PatternStatementTransform final : public ContextTrackingVisitor, public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: rejects a
    // reentrant run, seeds the context, and walks the tree. The C# also runs the
    // `DeclareVariables` analysis here (and clears it afterwards); that analysis is not
    // ported yet, so the port omits the analyze/clear pair (documented deferral -- it is
    // only consumed by the deferred `for`/`foreach` sub-transforms).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public override AstNode VisitBinaryOperatorExpression(...)`: uses the
    // associativity of `&&`/`||` to avoid parentheses -- `a && (b && c)` becomes
    // `(a && b) && c` (the same for `||`). The rewrite makes the right operand the parent and
    // the visited expression its left operand, then continues the child walk from the new
    // parent.
    Syntax::AstNode* VisitBinaryOperatorExpression(
        Syntax::BinaryOperatorExpression* expr) override;

    // The C# `public override AstNode VisitUnaryOperatorExpression(...)`: replaces a negated
    // equality with the inequality spelling -- `!(a == b)` becomes `a != b`.
    Syntax::AstNode* VisitUnaryOperatorExpression(
        Syntax::UnaryOperatorExpression* expr) override;

    // The C# `public override AstNode VisitIfElseStatement(IfElseStatement ...)`: simplifies a
    // cascading `else { if (...) ... }` into `else if (...) ...`.
    Syntax::AstNode* VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) override;

    // The C# `public override AstNode VisitTryCatchStatement(TryCatchStatement ...)`: merges a
    // nested `try { try {} catch {} } finally {}` into a single try-catch-finally.
    Syntax::AstNode* VisitTryCatchStatement(Syntax::TryCatchStatement* tryCatchStatement) override;

    // The C# `public override AstNode VisitMethodDeclaration(MethodDeclaration ...)`: converts a
    // `Finalize` method into a destructor declaration.
    Syntax::AstNode* VisitMethodDeclaration(Syntax::MethodDeclaration* methodDeclaration) override;

    // The C# `public override AstNode VisitDestructorDeclaration(DestructorDeclaration ...)`:
    // simplifies a destructor's `try { ... } finally { base.Finalize(); }` body.
    Syntax::AstNode* VisitDestructorDeclaration(
        Syntax::DestructorDeclaration* destructorDeclaration) override;

protected:
    // The C# `protected override AstNode VisitChildren(AstNode node)`: walks the children and
    // keeps visiting a node as long as the visit returns a different node (some sub-transforms
    // delete/replace nodes before and after the visited node, so the returned node is the
    // resumption point). Returns the walked node.
    Syntax::AstNode* VisitChildren(Syntax::AstNode* node) override;

private:
    // The C# `AstNode? SimplifyCascadingIfElseStatements(IfElseStatement node)`: rewrites the
    // matched shape in place. The C# always returns null (the instance is not replaced), so the
    // port keeps the nullable return for fidelity and the caller always continues the walk.
    Syntax::AstNode* SimplifyCascadingIfElseStatements(Syntax::IfElseStatement* node);

    // The C# `TryCatchStatement? TransformTryCatchFinally(TryCatchStatement tryFinally)`: merges
    // the matched nested try-catch into `tryFinally` in place. The C# always returns null.
    Syntax::TryCatchStatement* TransformTryCatchFinally(Syntax::TryCatchStatement* tryFinally);

    // The C# `DestructorDeclaration? TransformDestructor(MethodDeclaration methodDef)`: converts a
    // matched `Finalize` method into a destructor, or null when the shape does not match.
    Syntax::DestructorDeclaration* TransformDestructor(Syntax::MethodDeclaration* methodDef);

    // The C# `DestructorDeclaration? TransformDestructorBody(DestructorDeclaration dtorDef)`:
    // simplifies a matched destructor body in place, or null when the shape does not match.
    Syntax::DestructorDeclaration* TransformDestructorBody(Syntax::DestructorDeclaration* dtorDef);

    // The C# `[AllowNull] TransformContext context` -- the run state. Null outside a run; the
    // reentrancy check in `Run` reads it.
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
