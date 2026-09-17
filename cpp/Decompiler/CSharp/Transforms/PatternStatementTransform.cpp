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

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::BinaryOperatorExpression;
using Syntax::BinaryOperatorType;
using Syntax::Expression;
using Syntax::UnaryOperatorExpression;
using Syntax::UnaryOperatorType;

namespace {

namespace PM = Syntax::PatternMatching;

// Owns the nodes of a pattern tree built for one sub-transform invocation. The C# pattern
// definitions are `static readonly` (process lifetime); the port rebuilds an equivalent tree
// per call and keeps every node in this holder, so the non-owning child pointers the pattern
// nodes carry stay valid for the whole match. Nodes are owned through `INode` (both `Pattern`
// and `AstNode` derive from it, and it has a virtual destructor). A pattern node wrapped in a
// `PatternPlaceholderNode` is allocated as a `shared_ptr` by the caller and owned by the
// placeholder, so it is NOT put in this holder.
class PatternTree {
public:
    template <class T, class... Args>
    T* Make(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = node.get();
        nodes_.push_back(std::move(node));
        return result;
    }

    // The generated `implicit operator <TNode>(Pattern)`: wrap a pattern in a placeholder so
    // it can occupy an AST slot while still matching (PatternPlaceholder.hpp).
    template <class TNode>
    TNode* Wrap(std::shared_ptr<PM::Pattern> pattern) {
        return Make<Syntax::PatternPlaceholderNode<TNode>>(std::move(pattern));
    }

private:
    std::vector<std::unique_ptr<PM::INode>> nodes_;
};

// The C# `static readonly BlockStatement destructorBodyPattern` -- the shared body shape
// `try { <body> } finally { base.Finalize(); }`. Built into `tree` each call.
Syntax::BlockStatement* BuildDestructorBodyPattern(PatternTree& tree) {
    auto* bodyPattern = tree.Make<Syntax::BlockStatement>();
    auto* tryCatch = tree.Make<Syntax::TryCatchStatement>();
    tryCatch->TryBlock(tree.Wrap<Syntax::BlockStatement>(std::make_shared<PM::AnyNode>("body")));
    auto* finallyBlock = tree.Make<Syntax::BlockStatement>();
    auto* baseReference = tree.Make<Syntax::BaseReferenceExpression>();
    auto* memberReference = tree.Make<Syntax::MemberReferenceExpression>(
        baseReference, std::string("Finalize"));
    finallyBlock->Statements().Add(
        tree.Make<Syntax::ExpressionStatement>(tree.Make<Syntax::InvocationExpression>(memberReference)));
    tryCatch->FinallyBlock(finallyBlock);
    bodyPattern->Statements().Add(tryCatch);
    return bodyPattern;
}

// The C# `static readonly Expression addressOfPinnableReference` -- the shared pattern
// `&<target>.GetPinnableReference()` (the C# 7.3 pattern-based-`fixed` shape for value types).
// Built into `tree` each call.
Syntax::Expression* BuildAddressOfPinnableReferencePattern(PatternTree& tree) {
    auto* memberReference = tree.Make<Syntax::MemberReferenceExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::AnyNode>("target")),
        std::string("GetPinnableReference"));
    auto* invocation = tree.Make<Syntax::InvocationExpression>(memberReference);
    return tree.Make<Syntax::UnaryOperatorExpression>(
        invocation, Syntax::UnaryOperatorType::AddressOf);
}

} // namespace

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

AstNode* PatternStatementTransform::VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) {
    // The C# returns the (always null) result of the simplifier, then continues the walk.
    SimplifyCascadingIfElseStatements(ifElseStatement);
    return Syntax::DepthFirstAstVisitorAstNode::VisitIfElseStatement(ifElseStatement);
}

AstNode* PatternStatementTransform::VisitTryCatchStatement(
    Syntax::TryCatchStatement* tryCatchStatement) {
    // The C# `return TransformTryCatchFinally(...) ?? base.VisitTryCatchStatement(...)`.
    if (TransformTryCatchFinally(tryCatchStatement) != nullptr)
        return tryCatchStatement;
    return Syntax::DepthFirstAstVisitorAstNode::VisitTryCatchStatement(tryCatchStatement);
}

AstNode* PatternStatementTransform::VisitFixedStatement(Syntax::FixedStatement* fixedStatement) {
    if (context_->Settings().PatternBasedFixedStatement()) {
        for (int i = 0; i < fixedStatement->Variables().Count(); i++) {
            Syntax::VariableInitializer* variable = fixedStatement->Variables()[i];
            PatternTree tree;
            auto* pattern = BuildAddressOfPinnableReferencePattern(tree);
            PM::Match m = PM::PatternExtensions::Match(*pattern, variable->Initializer());
            if (m.Success()) {
                Syntax::Expression* target = m.Get<Syntax::Expression>("target").front();
                // The C# `target.GetResolveResult().Type.IsReferenceType == false`: only a value
                // type is taken by pattern-based `fixed` (reference types are handled by the
                // pinned-region detection). A null `IsReferenceType` (unknown) is not `false`.
                const Sem::ResolveResult* resolveResult = GetResolveResult(*target);
                if (resolveResult->Type().IsReferenceType() == std::optional<bool>(false)) {
                    context_->Step("Use pattern-based fixed statement", fixedStatement);
                    variable->Initializer(Syntax::Detach(target));
                }
            }
        }
    }
    return Syntax::DepthFirstAstVisitorAstNode::VisitFixedStatement(fixedStatement);
}

AstNode* PatternStatementTransform::VisitUsingStatement(Syntax::UsingStatement* usingStatement) {
    usingStatement = static_cast<Syntax::UsingStatement*>(
        Syntax::DepthFirstAstVisitorAstNode::VisitUsingStatement(usingStatement));
    if (!context_->Settings().UseEnhancedUsing())
        return usingStatement;

    if (Syntax::GetNextStatement(usingStatement) != nullptr
        || dynamic_cast<Syntax::BlockStatement*>(usingStatement->Parent()) == nullptr) {
        return usingStatement;
    }

    if (dynamic_cast<Syntax::VariableDeclarationStatement*>(
            usingStatement->ResourceAcquisition()) == nullptr) {
        return usingStatement;
    }

    context_->Step("Use enhanced using statement", usingStatement);
    usingStatement->IsEnhanced(true);
    return usingStatement;
}

AstNode* PatternStatementTransform::VisitMethodDeclaration(
    Syntax::MethodDeclaration* methodDeclaration) {
    // The C# `return TransformDestructor(methodDeclaration) ?? base.VisitMethodDeclaration(...)`.
    // `base` is ContextTrackingVisitor, which seeds `currentMethod` for the child walk.
    if (Syntax::DestructorDeclaration* destructor = TransformDestructor(methodDeclaration))
        return destructor;
    return ContextTrackingVisitor::VisitMethodDeclaration(methodDeclaration);
}

AstNode* PatternStatementTransform::VisitDestructorDeclaration(
    Syntax::DestructorDeclaration* destructorDeclaration) {
    // The C# `return TransformDestructorBody(...) ?? base.VisitDestructorDeclaration(...)`.
    if (Syntax::DestructorDeclaration* destructor = TransformDestructorBody(destructorDeclaration))
        return destructor;
    return ContextTrackingVisitor::VisitDestructorDeclaration(destructorDeclaration);
}

// ---- Cascading if-else -------------------------------------------------------------

AstNode* PatternStatementTransform::SimplifyCascadingIfElseStatements(
    Syntax::IfElseStatement* node) {
    // The C# `static readonly IfElseStatement cascadingIfElsePattern`: an `if` whose else is a
    // single block wrapping a nested `if` whose else is optional.
    PatternTree tree;
    auto* pattern = tree.Make<Syntax::IfElseStatement>();
    pattern->Condition(tree.Wrap<Expression>(std::make_shared<PM::AnyNode>()));
    pattern->TrueStatement(tree.Wrap<Syntax::Statement>(std::make_shared<PM::AnyNode>()));
    auto* nested = tree.Make<Syntax::IfElseStatement>();
    nested->Condition(tree.Wrap<Expression>(std::make_shared<PM::AnyNode>()));
    nested->TrueStatement(tree.Wrap<Syntax::Statement>(std::make_shared<PM::AnyNode>()));
    auto* innerAny = tree.Make<PM::AnyNode>();
    nested->FalseStatement(
        tree.Wrap<Syntax::Statement>(std::make_shared<PM::OptionalNode>(innerAny)));
    auto* falseBlock = tree.Make<Syntax::BlockStatement>();
    falseBlock->Statements().Add(
        tree.Wrap<Syntax::Statement>(std::make_shared<PM::NamedNode>("nestedIfStatement", nested)));
    pattern->FalseStatement(falseBlock);

    PM::Match m = PM::PatternExtensions::Match(*pattern, node);
    if (m.Success()) {
        context_->Step("Simplify cascading if-else", node);
        // The C# `m.Get<IfElseStatement>("nestedIfStatement").Single()` -- the captured node is
        // the real nested `if` in the input tree, so it is detached and becomes the else branch.
        Syntax::IfElseStatement* elseIf =
            m.Get<Syntax::IfElseStatement>("nestedIfStatement").front();
        node->FalseStatement(Syntax::Detach(elseIf));
    }
    // The C# always returns null (the instance is not replaced).
    return nullptr;
}

// ---- Try-catch-finally -------------------------------------------------------------

Syntax::TryCatchStatement* PatternStatementTransform::TransformTryCatchFinally(
    Syntax::TryCatchStatement* tryFinally) {
    // The C# `static readonly TryCatchStatement tryCatchFinallyPattern`: a `try` block that
    // consists of a single nested try-catch, plus a `finally` block.
    PatternTree tree;
    auto* pattern = tree.Make<Syntax::TryCatchStatement>();
    auto* outerTryBlock = tree.Make<Syntax::BlockStatement>();
    auto* innerTry = tree.Make<Syntax::TryCatchStatement>();
    innerTry->TryBlock(tree.Wrap<Syntax::BlockStatement>(std::make_shared<PM::AnyNode>()));
    auto* catchAny = tree.Make<PM::AnyNode>();
    innerTry->CatchClauses().Add(
        tree.Wrap<Syntax::CatchClause>(std::make_shared<PM::Repeat>(catchAny)));
    outerTryBlock->Statements().Add(innerTry);
    pattern->TryBlock(outerTryBlock);
    pattern->FinallyBlock(tree.Wrap<Syntax::BlockStatement>(std::make_shared<PM::AnyNode>()));

    if (PM::PatternExtensions::IsMatch(*pattern, tryFinally)) {
        context_->Step("Merge nested try-catch-finally", tryFinally);
        // The matched shape guarantees the outer try block holds exactly the nested try.
        auto* tryCatch = static_cast<Syntax::TryCatchStatement*>(
            tryFinally->TryBlock()->Statements()[0]);
        tryFinally->TryBlock(Syntax::Detach(tryCatch->TryBlock()));
        tryCatch->CatchClauses().MoveTo(tryFinally->CatchClauses());
    }
    // The C# always returns null (the instance is not replaced).
    return nullptr;
}

// ---- Destructor --------------------------------------------------------------------

Syntax::DestructorDeclaration* PatternStatementTransform::TransformDestructor(
    Syntax::MethodDeclaration* methodDef) {
    // The C# `static readonly MethodDeclaration destructorPattern`: a `void Finalize()` method
    // whose body is the destructor-body pattern.
    PatternTree tree;
    auto* pattern = tree.Make<Syntax::MethodDeclaration>();
    auto* attributeAny = tree.Make<PM::AnyNode>();
    pattern->Attributes().Add(
        tree.Wrap<Syntax::AttributeSection>(std::make_shared<PM::Repeat>(attributeAny)));
    pattern->Modifiers(Syntax::Modifiers::Any);
    pattern->ReturnType(tree.Make<Syntax::PrimitiveType>(std::string("void")));
    pattern->Name("Finalize");
    pattern->Body(BuildDestructorBodyPattern(tree));

    PM::Match m = PM::PatternExtensions::Match(*pattern, methodDef);
    if (!m.Success())
        return nullptr;
    context_->Step("Convert Finalize method to destructor", methodDef);
    auto* destructor = new Syntax::DestructorDeclaration();
    methodDef->Attributes().MoveTo(destructor->Attributes());
    CopyAnnotationsFrom(destructor, *methodDef);
    destructor->Modifiers(methodDef->Modifiers() &
                          ~(Syntax::Modifiers::Protected | Syntax::Modifiers::Override));
    destructor->Body(Syntax::Detach(m.Get<Syntax::BlockStatement>("body").front()));
    // The C# relies on the enclosing type context (`currentTypeDefinition!`) being set by the
    // `ContextTrackingVisitor` walk; a method declaration only appears inside a type.
    destructor->Name(currentTypeDefinition->Name());
    methodDef->ReplaceWith(destructor);
    context_->EndStep(destructor);
    return destructor;
}

Syntax::DestructorDeclaration* PatternStatementTransform::TransformDestructorBody(
    Syntax::DestructorDeclaration* dtorDef) {
    PatternTree tree;
    auto* bodyPattern = BuildDestructorBodyPattern(tree);

    PM::Match m = PM::PatternExtensions::Match(*bodyPattern, dtorDef->Body());
    if (!m.Success())
        return nullptr;
    context_->Step("Simplify destructor body", dtorDef);
    dtorDef->Body(Syntax::Detach(m.Get<Syntax::BlockStatement>("body").front()));
    return dtorDef;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
