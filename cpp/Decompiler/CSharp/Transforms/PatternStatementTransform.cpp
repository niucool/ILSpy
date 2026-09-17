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

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::BinaryOperatorExpression;
using Syntax::BinaryOperatorType;
using Syntax::Expression;
using Syntax::UnaryOperatorExpression;
using Syntax::UnaryOperatorType;

void PatternStatementTransform::Run(AstNode& rootNode, TransformContext& context) {
    // The C# `if (this.context != null) throw new InvalidOperationException(
    // "Reentrancy in PatternStatementTransform.Run?")`. The port throws std::logic_error (the
    // port's InvalidOperationException convention).
    if (context_ != nullptr)
        throw std::logic_error("Reentrancy in PatternStatementTransform.Run?");
    context_ = &context;
    try {
        Initialize(context);
        rootNode.AcceptVisitorAstNode(*this);
    } catch (...) {
        context_ = nullptr;
        Uninitialize();
        throw;
    }
    // The C# `finally`: clear the run state and the context slots (`declareVariables
    // .ClearAnalysisResults()` is omitted with the deferred analysis).
    context_ = nullptr;
    Uninitialize();
}

AstNode* PatternStatementTransform::VisitChildren(AstNode* node) {
    // The C# `for (AstNode? child = node.FirstChild; child != null; child =
    // child.NextSibling) { ... do { oldChild = child; child = child.AcceptVisitor(this); }
    // while (child != oldChild); }`. The inner loop re-visits a child while the visit returns
    // a different node, because a sub-transform may delete/replace nodes around the visited
    // one and the returned node is where the walk resumes.
    for (AstNode* child = node->FirstChild(); child != nullptr; child = child->NextSibling()) {
        AstNode* oldChild;
        do {
            oldChild = child;
            child = child->AcceptVisitorAstNode(*this);
            // The C# `Debug.Assert(child != null && child.Parent == node)`: every ported
            // sub-transform returns the (possibly replaced) node, never null. Like the
            // C# assert, the check is compiled out of release builds (the `CheckInvariant`
            // convention: an assert is debug-only, not a runtime guard).
#ifndef NDEBUG
            if (child == nullptr || child->Parent() != node)
                throw std::logic_error(
                    "PatternStatementTransform ran into an inconsistent AST");
#endif
        } while (child != oldChild);
    }
    return node;
}

AstNode* PatternStatementTransform::VisitBinaryOperatorExpression(
    BinaryOperatorExpression* expr) {
    switch (expr->Operator()) {
        case BinaryOperatorType::ConditionalAnd:
        case BinaryOperatorType::ConditionalOr: {
            // a && (b && c) ==> (a && b) && c
            auto* bAndC = dynamic_cast<BinaryOperatorExpression*>(expr->Right());
            if (bAndC != nullptr && bAndC->Operator() == expr->Operator()) {
                context_->Step("Reassociate conditional logic", expr);
                // Make bAndC the parent and expr the child. A conditional-and/or operator
                // always has both operands present.
                Expression* b = Syntax::Detach(bAndC->Left());
                Expression* c = Syntax::Detach(bAndC->Right());
                expr->ReplaceWith(Syntax::Detach(bAndC));
                bAndC->Left(expr);
                bAndC->Right(c);
                expr->Right(b);
                context_->EndStep(bAndC);
                return Syntax::DepthFirstAstVisitorAstNode::VisitBinaryOperatorExpression(bAndC);
            }
            break;
        }
        default:
            break;
    }
    return Syntax::DepthFirstAstVisitorAstNode::VisitBinaryOperatorExpression(expr);
}

AstNode* PatternStatementTransform::VisitUnaryOperatorExpression(
    UnaryOperatorExpression* expr) {
    if (expr->Operator() == UnaryOperatorType::Not) {
        auto* binary = dynamic_cast<BinaryOperatorExpression*>(expr->Expression());
        if (binary != nullptr && binary->Operator() == BinaryOperatorType::Equality) {
            context_->Step("Replace negated equality with inequality", expr);
            binary->Operator(BinaryOperatorType::InEquality);
            expr->ReplaceWith(Syntax::Detach(binary));
            context_->EndStep(binary);
            return VisitBinaryOperatorExpression(binary);
        }
    }
    return Syntax::DepthFirstAstVisitorAstNode::VisitUnaryOperatorExpression(expr);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
