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
// the negated-equality rewrite (`!(a == b)` -> `a != b`), and the for reshape
// (`v = init; while (v op end) { stmts; v = ...; }` -> `for (...) { stmts; }`,
// plus the declaration merge into an existing for's initializers).
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
// first order of the port plan): foreach-on-array / inline-array / multi-dim,
// TransformAutomaticProperty, the destructor
// TransformDestructorFinalizerWithPattern, TransformTryCatchFinally, the C# 7.3
// pattern-based fixed, the C# 8.0 enhanced using, and the Identifier
// backing-field rewrite. The `DeclareVariables declareVariables` member's
// analysis (Analyze/GetDeclarationPoint, feeding the for reshape's
// iterator-variable bail) is ported; the DeclareVariables mutation half
// (Run/InsertVariableDeclarations/UpdateAnnotations and its GetAstTransforms
// slot) lands with its own slice.

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
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

// ---- for ------------------------------------------------------------------------------

// The C# `static readonly AstNode variableAssignPattern` + `static readonly
// WhileStatement forPattern` (lines ~157-183), the same lazy process-lifetime
// singleton convention:
//
//     variableAssignPattern: ExpressionStatement(
//         AssignmentExpression(NamedNode("variable", IdentifierExpression(AnyString)),
//                              AnyNode("initializer")))
//
//     forPattern: WhileStatement {
//         Condition = BinaryOperatorExpression {
//             Left = NamedNode("ident", IdentifierExpression(AnyString)),
//             Operator = BinaryOperatorType.Any,
//             Right = AnyNode("endExpr") },
//         EmbeddedStatement = BlockStatement { Statements = {
//             Repeat(AnyNode("statement")),
//             NamedNode("iterator", ExpressionStatement(
//                 AssignmentExpression {
//                     Left = Backreference("ident"),
//                     Operator = AssignmentOperatorType.Any,
//                     Right = AnyNode() })) } } }
struct TransformForPatternsHolder {
    // -- `$variable = $initializer;`
    PatternMatching::AnyNode initializer{"initializer"};
    Syntax::IdentifierExpression variable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode variableNamed{"variable", variable};
    Syntax::AssignmentExpression variableAssign{
        Syntax::Expression::ToExpression(variableNamed),
        Syntax::AssignmentOperatorType::Assign,
        Syntax::Expression::ToExpression(initializer)};
    Syntax::ExpressionStatement variableAssignPattern{&variableAssign};

    // -- `while ($ident $op $endExpr) { $statement...; $ident = ...; }`
    Syntax::IdentifierExpression ident{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode identNamed{"ident", ident};
    PatternMatching::AnyNode endExpr{"endExpr"};
    Syntax::BinaryOperatorExpression loopCondition{
        Syntax::Expression::ToExpression(identNamed),
        Syntax::BinaryOperatorType::Any,
        Syntax::Expression::ToExpression(endExpr)};
    PatternMatching::AnyNode statement{"statement"};
    PatternMatching::Repeat statementRepeat{statement};
    PatternMatching::Backreference identBackreference{"ident"};
    PatternMatching::AnyNode iteratorRight;
    Syntax::AssignmentExpression iteratorAssign{
        Syntax::Expression::ToExpression(identBackreference),
        Syntax::AssignmentOperatorType::Any,
        Syntax::Expression::ToExpression(iteratorRight)};
    Syntax::ExpressionStatement iteratorStatement{&iteratorAssign};
    PatternMatching::NamedNode iteratorNamed{"iterator", iteratorStatement};
    Syntax::BlockStatement loopBody;
    Syntax::WhileStatement forPattern{&loopCondition, &loopBody};

    TransformForPatternsHolder() {
        loopBody.Statements().Add(Syntax::Statement::ToStatement(statementRepeat));
        loopBody.Statements().Add(Syntax::Statement::ToStatement(iteratorNamed));
    }
};

TransformForPatternsHolder& GetTransformForPatterns() {
    static TransformForPatternsHolder holder;
    return holder;
}

// The C# `bool DescendIntoStatement(AstNode node)` -- the continue-scan gate:
// do not descend into expressions (their identifier references are not
// statements) or into NESTED loops (a continue there targets the nested loop,
// not this one). Stateless, so a file-local free function (the port's probe
// convention).
bool DescendIntoStatement(Syntax::AstNode* node) {
    if (dynamic_cast<Syntax::Expression*>(node) != nullptr ||
        dynamic_cast<Syntax::ExpressionStatement*>(node) != nullptr)
        return false;
    if (dynamic_cast<Syntax::WhileStatement*>(node) != nullptr ||
        dynamic_cast<Syntax::ForeachStatement*>(node) != nullptr ||
        dynamic_cast<Syntax::DoWhileStatement*>(node) != nullptr ||
        dynamic_cast<Syntax::ForStatement*>(node) != nullptr)
        return false;
    return true;
}

// The C# `bool ForStatementUsesVariable(ForStatement statement, ILVariable?
// variable)` -- the for statement reads the variable in its condition or one of
// its iterators.
bool ForStatementUsesVariable(Syntax::ForStatement* statement,
                              IL::ILVariable* variable) {
    if (statement->Condition() != nullptr) {
        for (Syntax::AstNode* node : statement->Condition()->DescendantsAndSelf()) {
            if (auto* identifierExpression =
                    dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifierExpression != nullptr &&
                CS::GetILVariable(*identifierExpression) == variable)
                return true;
        }
    }
    for (int i = 0; i < statement->Iterators().Count(); i++) {
        for (Syntax::AstNode* node : statement->Iterators().At(i)->DescendantsAndSelf()) {
            if (auto* identifierExpression =
                    dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifierExpression != nullptr &&
                CS::GetILVariable(*identifierExpression) == variable)
                return true;
        }
    }
    return false;
}

// The C# `bool IsVariableUsedAfter(Statement loop, ILVariable variable)` -- any
// use of the variable in the statements following the loop.
bool IsVariableUsedAfter(Syntax::Statement* loop, IL::ILVariable* variable) {
    for (Syntax::AstNode* sibling = loop->NextSibling(); sibling != nullptr;
         sibling = sibling->NextSibling()) {
        for (Syntax::AstNode* node : sibling->DescendantsAndSelf()) {
            if (auto* identifierExpression =
                    dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifierExpression != nullptr &&
                CS::GetILVariable(*identifierExpression) == variable)
                return true;
        }
    }
    return false;
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
    // The transform's DeclareVariables analysis (the C# reads the same
    // instance's member through the transform object).
    DeclareVariables* declareVariables = nullptr;

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

    // The C# `public override AstNode VisitExpressionStatement(ExpressionStatement
    // ...)` (line ~82): the foreach-on-multidim-array arm routes first in the C#;
    // that family is deferred (the file header), so the for reshape is the first
    // consumer here.
    void VisitExpressionStatement(
        Syntax::ExpressionStatement* expressionStatement) override {
        if (Syntax::AstNode* result = TransformFor(expressionStatement)) {
            lastResult = result;
            return;
        }
        Syntax::DepthFirstAstVisitor::VisitExpressionStatement(expressionStatement);
    }

    // The C# `public ForStatement? TransformFor(ExpressionStatement node)` (line
    // ~186): `v = init; while (v op end) { stmts; v = ...; }` becomes
    // `for (v = init; v op end; v = ...) { stmts; }`, and `v = init;` immediately
    // before an existing for statement that uses `v` moves into that for's
    // initializers.
    Syntax::ForStatement* TransformFor(Syntax::ExpressionStatement* node) {
        if (!context->DecompileRun->Settings().ForStatement())
            return nullptr;
        PatternMatching::Match m1 =
            Syntax::MatchNode(GetTransformForPatterns().variableAssignPattern, node);
        if (!m1.Success())
            return nullptr;
        std::vector<Syntax::IdentifierExpression*> variables =
            m1.Get<Syntax::IdentifierExpression>("variable");
        // The C# `.Single()` (exactly one capture by construction).
        assert(variables.size() == 1);
        IL::ILVariable* variable = CS::GetILVariable(*variables.front());
        Syntax::AstNode* next = node->NextSibling();
        if (next == nullptr)
            return nullptr;
        if (auto* nextForStatement = dynamic_cast<Syntax::ForStatement*>(next);
            nextForStatement != nullptr &&
            ForStatementUsesVariable(nextForStatement, variable)) {
            context->StepOnce("Move declaration into for initializer", node);
            node->Remove();
            next->InsertChildAfter(nullptr, node, &Syntax::Slots::ForInitializer);
            return nextForStatement;
        }
        PatternMatching::Match m3 =
            Syntax::MatchNode(GetTransformForPatterns().forPattern, next);
        if (!m3.Success())
            return nullptr;
        // ensure the variable in the for pattern is the same as in the declaration
        std::vector<Syntax::IdentifierExpression*> idents =
            m3.Get<Syntax::IdentifierExpression>("ident");
        assert(idents.size() == 1);
        if (variable != CS::GetILVariable(*idents.front()))
            return nullptr;
        auto* loop = dynamic_cast<Syntax::WhileStatement*>(next);
        // The forPattern is a WhileStatement pattern, so a successful match
        // guarantees the cast.
        assert(loop != nullptr);
        // Cannot convert to for loop, if the iteration variable is a ref local
        // used after the loop: its declaration is hoisted in front, leaving a
        // headless `for (; cond; v = ref ...)` whose only initialization is the
        // for-initializer ref-assignment -- which can't be split from a ref local
        // (CS8174). Keeping it a while-loop matches the source and keeps the
        // initializer on the decl.
        if (variable != nullptr && variable->Type->IsByRefLike() &&
            IsVariableUsedAfter(loop, variable))
            return nullptr;
        std::vector<Syntax::Statement*> iteratorCaptures =
            m3.Get<Syntax::Statement>("iterator");
        assert(iteratorCaptures.size() == 1);
        Syntax::Statement* iteratorStatement = iteratorCaptures.front();
        // Cannot convert to for loop, if any variable that is used in the
        // "iterator" part of the pattern, will be declared in the body of
        // the while-loop.
        if (IteratorVariablesDeclaredInsideLoopBody(iteratorStatement))
            return nullptr;
        // Cannot convert to for loop, because that would change the semantics of
        // the program: continue in while jumps to the condition block, whereas
        // continue in for jumps to the increment block.
        for (Syntax::AstNode* descendant :
             loop->DescendantNodes(DescendIntoStatement)) {
            auto* statement = dynamic_cast<Syntax::Statement*>(descendant);
            if (statement != nullptr &&
                dynamic_cast<Syntax::ContinueStatement*>(statement) != nullptr)
                return nullptr;
        }
        context->StepOnce("Transform while loop to for", loop);
        node->Remove();
        auto* newBody = new Syntax::BlockStatement();
        for (Syntax::Statement* stmt : m3.Get<Syntax::Statement>("statement"))
            newBody->Statements().Add(Syntax::Detach(stmt));
        auto* forStatement = new Syntax::ForStatement();
        CS::CopyAnnotationsFrom(forStatement, *loop);
        forStatement->Initializers().Add(node);
        forStatement->Condition(Syntax::Detach(loop->Condition()));
        forStatement->Iterators().Add(Syntax::Detach(iteratorStatement));
        forStatement->EmbeddedStatement(newBody);
        loop->ReplaceWith(forStatement);
        // The C# `context.EndStep(forStatement)` (the step-group close) folds
        // onto the single step hook.
        return forStatement;
    }

    // The C# `bool IteratorVariablesDeclaredInsideLoopBody(Statement
    // iteratorStatement)` (line ~266): a variable whose declaration point sits
    // in the same block as the iterator (the loop body) would be declared in
    // the body scope, which the for's iterator slot cannot see.
    bool IteratorVariablesDeclaredInsideLoopBody(Syntax::Statement* iteratorStatement) {
        for (Syntax::AstNode* node : iteratorStatement->DescendantsAndSelf()) {
            auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(node);
            if (identifier == nullptr)
                continue;
            IL::ILVariable* v = CS::GetILVariable(*identifier);
            if (v == nullptr || !DeclareVariables::VariableNeedsDeclaration(v->Kind))
                continue;
            if (declareVariables->GetDeclarationPoint(v)->Parent() ==
                iteratorStatement->Parent())
                return true;
        }
        return false;
    }

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
        visitor.declareVariables = &declareVariables_;
        visitor.Initialize(context);
        declareVariables_.Analyze(rootNode);
        rootNode.AcceptVisitor(visitor);
        // The C# finally also Uninitializes; the visitor's destruction covers it.
    } catch (...) {
        context_ = nullptr;
        declareVariables_.ClearAnalysisResults();
        throw;
    }
    context_ = nullptr;
    declareVariables_.ClearAnalysisResults();
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
