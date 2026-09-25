// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs --
// the pattern-based statement transforms over the C# AST. Ported arms: the
// cascading if-else simplification (`if (a) A else { if (b) B }` -> `else if`),
// the conditional-logic reassociation (`a && (b && c)` -> `(a && b) && c`),
// and the negated-equality rewrite (`!(a == b)` -> `a != b`).
//
// The C# `PatternStatementTransform : ContextTrackingVisitor<AstNode>, IAstTransform`
// ports to the IAstTransform + internal DepthFirstAstVisitor convention (the
// NormalizeBlockStatements precedent). The ContextTrackingVisitor<AstNode> base
// ports with it: the type/method context tracking (Initialize/Uninitialize and
// the per-declaration overrides feeding currentTypeDefinition/currentMethod) and
// the re-visit loop -- VisitChildren keeps visiting a child as long as the visit
// REPLACES it, because some transforms delete/replace nodes before and after the
// node being transformed. The C# relies on the visitor's AstNode return value to
// know where to keep iterating; the port's void visitor carries the same value in
// the visitor's `lastResult` slot (every Visit override records there the node the
// C# method returns, and the re-visit loop reads it after each AcceptVisitor).
//
// DEFERRED loudly at their C# slots, landing with their own slices (the smallest-
// first order of the port plan): TransformFor (the while->for reshape; needs
// ForStatementUsesVariable/IsVariableUsedAfter/IteratorVariablesDeclaredInsideLoop
// Body/DescendIntoStatement and the continue-in-while bail), foreach-on-array /
// inline-array / multi-dim, TransformAutomaticProperty, the destructor
// TransformDestructorFinalizerWithPattern, TransformTryCatchFinally, the C# 7.3
// pattern-based fixed, the C# 8.0 enhanced using, and the Identifier
// backing-field rewrite. The `DeclareVariables declareVariables` member (the C#
// Run's `declareVariables.Analyze(rootNode)` + `ClearAnalysisResults`) lands with
// the DeclareVariables port; none of the ported arms read its analysis.

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"

#include <cassert>
#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace PatternMatching = ::ILSpy::Decompiler::CSharp::Syntax::PatternMatching;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

// ---- Simplify cascading if-else-if statements ----------------------------------------

// The C# `static readonly IfElseStatement cascadingIfElsePattern` (line ~1013):
//
//     IfElseStatement {
//         Condition = AnyNode, TrueStatement = AnyNode,
//         FalseStatement = BlockStatement { Statements = {
//             NamedNode("nestedIfStatement", IfElseStatement {
//                 Condition = AnyNode, TrueStatement = AnyNode,
//                 FalseStatement = OptionalNode(AnyNode) }) } } }
//
// The C# embeds the pattern nodes into the statement slots through the generated
// `implicit operator Expression/Statement(Pattern)` conversions; the port uses the
// `Expression::ToExpression` / `Statement::ToStatement` wrappers (the
// PatternPlaceholder bridge). Built lazily as a process-lifetime singleton (the
// GetForeachPatterns convention); the placeholder nodes the wrappers allocate are
// the singleton's own (never freed, like the C# static's GC graph).
struct CascadingIfElsePatternHolder {
    // The nested if-else's pattern children.
    PatternMatching::AnyNode nestedCondition;
    PatternMatching::AnyNode nestedTrueStatement;
    PatternMatching::AnyNode nestedFalseStatement;
    PatternMatching::OptionalNode nestedFalse{nestedFalseStatement};
    // The nested `if` -- both required operands present, the optional else slot
    // holding the OptionalNode.
    Syntax::IfElseStatement nestedIf{
        Syntax::Expression::ToExpression(nestedCondition),
        Syntax::Statement::ToStatement(nestedTrueStatement),
        Syntax::Statement::ToStatement(nestedFalse)};
    PatternMatching::NamedNode nestedIfStatement{"nestedIfStatement", nestedIf};
    // The else block carrying exactly the nested if (a BlockStatement pattern whose
    // Statements collection holds the single NamedNode).
    Syntax::BlockStatement elseBlock;
    // The outer if-else's pattern children.
    PatternMatching::AnyNode outerCondition;
    PatternMatching::AnyNode outerTrueStatement;
    Syntax::IfElseStatement pattern{
        Syntax::Expression::ToExpression(outerCondition),
        Syntax::Statement::ToStatement(outerTrueStatement)};

    CascadingIfElsePatternHolder() {
        elseBlock.Statements().Add(Syntax::Statement::ToStatement(nestedIfStatement));
        pattern.FalseStatement(&elseBlock);
    }
};

Syntax::IfElseStatement& CascadingIfElsePattern() {
    static CascadingIfElsePatternHolder holder;
    return holder.pattern;
}

} // namespace

// The visitor half (the C# class body): the ContextTrackingVisitor<AstNode> state
// (the context field, the current type/method tracking) plus the per-node
// overrides. The C# `IAstTransform.Run` explicit implementation is the outer Run's
// drive below.
class PatternStatementTransformVisitor final : public Syntax::DepthFirstAstVisitor {
public:
    // The C# `[AllowNull] TransformContext context`.
    TransformContext* context = nullptr;

    // The C# `protected ITypeDefinition? currentTypeDefinition` / `IMethod?
    // currentMethod` (the ContextTrackingVisitor tracking slots; the deferred
    // arms -- the destructor name, the automatic-property accessor owner -- read
    // them, carried now so the shell is complete).
    const TS::ITypeDefinition* currentTypeDefinition = nullptr;
    const TS::IMethod* currentMethod = nullptr;

    // The C# visitor's AstNode RETURN VALUE: the node the matching C# Visit method
    // returns -- the visited node after an in-place rewrite, or its replacement.
    // The re-visit loop in VisitChildren reads it after each child's
    // AcceptVisitor; the per-node defaults and every override below keep it
    // current (the base VisitChildren sets it to the visited node, the C#
    // `return node`).
    Syntax::AstNode* lastResult = nullptr;

    // The C# `protected void Initialize(TransformContext context)` /
    // `Uninitialize` (the ContextTrackingVisitor members; the Uninitialize half is
    // the visitor's destruction here -- the visitor is scoped to one Run).
    void Initialize(TransformContext& context) {
        currentTypeDefinition = context.CurrentTypeDefinition;
        currentMethod = dynamic_cast<const TS::IMethod*>(context.CurrentMember);
    }

    // ---- The ContextTrackingVisitor<AstNode>.VisitChildren re-visit loop ------
    //
    // Go through the children, and keep visiting a node as long as it changes.
    // Because some transforms delete/replace nodes before and after the node being
    // transformed, the transform's return value (lastResult) says where iteration
    // needs to continue. The C# `Debug.Assert(child != null && child.Parent ==
    // node)` ports as the assert (the return contract: a Visit records the node
    // now occupying the visited child's slot).
    void VisitChildren(Syntax::AstNode* node) override {
        for (Syntax::AstNode* child = node->FirstChild(); child != nullptr;
             child = child->NextSibling()) {
            Syntax::AstNode* oldChild;
            do {
                oldChild = child;
                child->AcceptVisitor(*this);
                child = lastResult;
                assert(child != nullptr && child->Parent() == node);
            } while (child != oldChild);
        }
        lastResult = node;
    }

    // ---- The ContextTrackingVisitor type/method tracking ----------------------
    // The C# `public override TResult VisitTypeDeclaration(...)` family: track the
    // enclosing type/method through the walk (restored on the way out; the C#
    // try/finally reduces to the sequential restore -- the engine's normal path
    // carries no exception past a Visit).

    void VisitTypeDeclaration(Syntax::TypeDeclaration* typeDeclaration) override {
        const TS::ITypeDefinition* oldType = currentTypeDefinition;
        currentTypeDefinition =
            dynamic_cast<const TS::ITypeDefinition*>(CS::GetSymbol(*typeDeclaration));
        Syntax::DepthFirstAstVisitor::VisitTypeDeclaration(typeDeclaration);
        currentTypeDefinition = oldType;
    }

    void VisitMethodDeclaration(Syntax::MethodDeclaration* methodDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*methodDeclaration));
        Syntax::DepthFirstAstVisitor::VisitMethodDeclaration(methodDeclaration);
        currentMethod = oldMethod;
    }

    void VisitConstructorDeclaration(
        Syntax::ConstructorDeclaration* constructorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*constructorDeclaration));
        Syntax::DepthFirstAstVisitor::VisitConstructorDeclaration(constructorDeclaration);
        currentMethod = oldMethod;
    }

    void VisitDestructorDeclaration(
        Syntax::DestructorDeclaration* destructorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*destructorDeclaration));
        Syntax::DepthFirstAstVisitor::VisitDestructorDeclaration(destructorDeclaration);
        currentMethod = oldMethod;
    }

    void VisitOperatorDeclaration(
        Syntax::OperatorDeclaration* operatorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*operatorDeclaration));
        Syntax::DepthFirstAstVisitor::VisitOperatorDeclaration(operatorDeclaration);
        currentMethod = oldMethod;
    }

    void VisitAccessor(Syntax::Accessor* accessor) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod = dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*accessor));
        Syntax::DepthFirstAstVisitor::VisitAccessor(accessor);
        currentMethod = oldMethod;
    }

    // ---- The ported arms --------------------------------------------------------

    // The C# `public override AstNode VisitIfElseStatement(IfElseStatement ...)`.
    // (The C# also routes VisitExpressionStatement/VisitForStatement through the
    // foreach/for arms and VisitPropertyDeclaration/VisitEventDeclaration/
    // VisitMethodDeclaration/VisitTryCatchStatement/VisitFixedStatement/
    // VisitUsingStatement through their arms -- all deferred, loud in the file
    // header.)
    void VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) override {
        Syntax::AstNode* simplifiedIfElse =
            SimplifyCascadingIfElseStatements(ifElseStatement);
        if (simplifiedIfElse != nullptr) {
            lastResult = simplifiedIfElse;
            return;
        }
        Syntax::DepthFirstAstVisitor::VisitIfElseStatement(ifElseStatement);
    }

    // The C# `AstNode? SimplifyCascadingIfElseStatements(IfElseStatement node)`
    // (line ~1028): the else block wrapping a lone if-else collapses to a bare
    // `else if`. The node itself is not changed in identity, so the visitor
    // continues as usual -- return null.
    Syntax::AstNode* SimplifyCascadingIfElseStatements(Syntax::IfElseStatement* node) {
        PatternMatching::Match m = Syntax::MatchNode(CascadingIfElsePattern(), node);
        if (m.Success()) {
            context->StepOnce("Simplify cascading if-else", node);
            std::vector<Syntax::IfElseStatement*> nested =
                m.Get<Syntax::IfElseStatement>("nestedIfStatement");
            // The C# `.Single()` (exactly one capture by construction).
            assert(nested.size() == 1);
            node->FalseStatement(Syntax::Detach(nested.front()));
        }

        return nullptr;
    }

    // The C# `public override AstNode VisitBinaryOperatorExpression(...)`
    // (line ~1046): use the associativity of the conditional logic operators to
    // avoid parentheses.
    void VisitBinaryOperatorExpression(
        Syntax::BinaryOperatorExpression* expr) override {
        switch (expr->Operator()) {
            case Syntax::BinaryOperatorType::ConditionalAnd:
            case Syntax::BinaryOperatorType::ConditionalOr:
                // a && (b && c) ==> (a && b) && c
                if (auto* bAndC =
                        dynamic_cast<Syntax::BinaryOperatorExpression*>(expr->Right());
                    bAndC != nullptr && bAndC->Operator() == expr->Operator()) {
                    context->StepOnce("Reassociate conditional logic", expr);
                    // make bAndC the parent and expr the child.
                    // A conditional-and/or operator always has both operands present.
                    Syntax::Expression* b = Syntax::Detach(bAndC->Left());
                    Syntax::Expression* c = Syntax::Detach(bAndC->Right());
                    expr->ReplaceWith(Syntax::Detach(bAndC));
                    bAndC->Left(expr);
                    bAndC->Right(c);
                    expr->Right(b);
                    // The C# `context.EndStep(bAndC)` (the step-group close) folds
                    // onto the single step hook.
                    Syntax::DepthFirstAstVisitor::VisitBinaryOperatorExpression(bAndC);
                    return;
                }
                break;
            default:
                break;
        }
        Syntax::DepthFirstAstVisitor::VisitBinaryOperatorExpression(expr);
    }

    // The C# `public override AstNode VisitUnaryOperatorExpression(...)` (line
    // ~1073): `!(a == b)` becomes `a != b` (the replacement then goes through the
    // binary-operator arm, the C# `VisitBinaryOperatorExpression(binary)` virtual
    // call).
    void VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression* expr) override {
        if (expr->Operator() == Syntax::UnaryOperatorType::Not) {
            auto* binary =
                dynamic_cast<Syntax::BinaryOperatorExpression*>(expr->Expression());
            if (binary != nullptr &&
                binary->Operator() == Syntax::BinaryOperatorType::Equality) {
                context->StepOnce("Replace negated equality with inequality", expr);
                binary->Operator(Syntax::BinaryOperatorType::InEquality);
                expr->ReplaceWith(Syntax::Detach(binary));
                // The C# `context.EndStep(binary)` folds onto the single step hook.
                VisitBinaryOperatorExpression(binary);
                return;
            }
        }
        Syntax::DepthFirstAstVisitor::VisitUnaryOperatorExpression(expr);
    }
};

void PatternStatementTransform::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    // The C# reentrancy guard (`InvalidOperationException` -> the port's
    // logic_error convention).
    if (context_ != nullptr)
        throw std::logic_error("Reentrancy in PatternStatementTransform.Run?");
    context_ = &context;
    try {
        PatternStatementTransformVisitor visitor;
        visitor.context = &context;
        visitor.Initialize(context);
        // The C# `declareVariables.Analyze(rootNode)` (+ the finally's
        // ClearAnalysisResults) is deferred with the DeclareVariables port; the
        // ported arms read none of its analysis.
        rootNode.AcceptVisitor(visitor);
        // The C# finally also Uninitializes; the visitor's destruction covers it.
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
