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
// The port is landed in slices. This slice lands the transform skeleton (`Run`,
// `VisitChildren`) and the two purely-structural sub-transforms that need no pattern tree:
// the conditional-logic reassociation (`a && (b && c)` -> `(a && b) && c`) and the negated
// equality rewrite (`!(a == b)` -> `a != b`). The pattern-based sub-transforms (the `for`/
// `foreach`/automatic-property/automatic-event/destructor/try-catch-finally/cascading-if
// rewrites), and the `DeclareVariables` analysis they compose, stay deferred -- each is named
// at the visit that would call it. This keeps the `VisitChildren` replace-and-revisit walk
// and the reentrancy contract exercised now, so the later slices only add sub-transforms.

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Transforms/ContextTrackingVisitor.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

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

protected:
    // The C# `protected override AstNode VisitChildren(AstNode node)`: walks the children and
    // keeps visiting a node as long as the visit returns a different node (some sub-transforms
    // delete/replace nodes before and after the visited node, so the returned node is the
    // resumption point). Returns the walked node.
    Syntax::AstNode* VisitChildren(Syntax::AstNode* node) override;

private:
    // The C# `[AllowNull] TransformContext context` -- the run state. Null outside a run; the
    // reentrancy check in `Run` reads it.
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
