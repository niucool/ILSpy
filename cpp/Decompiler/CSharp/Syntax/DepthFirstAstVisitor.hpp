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

// Port of the `DepthFirstAstVisitor` abstract class in
// ICSharpCode.Decompiler/CSharp/Syntax/DepthFirstAstVisitor.cs -- the default
// implementation of `IAstVisitor` whose every per-node `Visit<NodeName>` method delegates
// to `VisitChildren` (a depth-first walk of the node's children calling `AcceptVisitor`
// on each). Concrete visitors derive from it and override only the `Visit` methods they
// care about; the rest fall through to the depth-first walk.
//
// Paired with `IAstVisitor` (IAstVisitor.hpp) and `AstNode::AcceptVisitor` (the dispatch
// entry, declared abstract on `AstNode`). The C# generator emits three `DepthFirstAstVisitor`
// variants (void, `<T>`, `<T,S>`); only the void one is consumed by the engine and ported
// here (see IAstVisitor.hpp for the generic-variant deferral).
//
// This header ports the load-bearing `VisitChildren` default. The per-node `Visit` overrides
// are added as the concrete node hierarchy lands (each one is a one-liner calling
// `VisitChildren(node)`), matching the C# generator's emitted `public virtual void
// Visit<NodeName>(<Node> node) { VisitChildren(node); }`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITOR_HPP

#include "AstNode.hpp"
#include "IAstVisitor.hpp"
#include "Identifier.hpp"
#include "Expressions/AssignmentExpression.hpp"
#include "Expressions/BaseReferenceExpression.hpp"
#include "Expressions/BinaryOperatorExpression.hpp"
#include "Expressions/CheckedExpression.hpp"
#include "Expressions/ConditionalExpression.hpp"
#include "Expressions/DirectionExpression.hpp"
#include "Expressions/NullReferenceExpression.hpp"
#include "Expressions/ParenthesizedExpression.hpp"
#include "Expressions/PrimitiveExpression.hpp"
#include "Expressions/ThisReferenceExpression.hpp"
#include "Expressions/ThrowExpression.hpp"
#include "Expressions/UnaryOperatorExpression.hpp"
#include "Expressions/UncheckedExpression.hpp"
#include "PrimitiveType.hpp"
#include "SimpleType.hpp"
#include "MemberType.hpp"
#include "ArraySpecifier.hpp"
#include "Attribute.hpp"
#include "AttributeSection.hpp"
#include "ComposedType.hpp"
#include "Expressions/CastExpression.hpp"
#include "Expressions/AsExpression.hpp"
#include "Expressions/IsExpression.hpp"
#include "Expressions/TypeReferenceExpression.hpp"
#include "Expressions/TypeOfExpression.hpp"
#include "Expressions/DefaultValueExpression.hpp"
#include "Expressions/SizeOfExpression.hpp"
#include "Expressions/IdentifierExpression.hpp"
#include "Expressions/MemberReferenceExpression.hpp"
#include "Expressions/PointerReferenceExpression.hpp"
#include "Expressions/InvocationExpression.hpp"
#include "Expressions/IndexerExpression.hpp"
#include "Expressions/ArrayInitializerExpression.hpp"
#include "Expressions/ObjectCreateExpression.hpp"
#include "Expressions/ArrayCreateExpression.hpp"
#include "Statements/ContinueStatement.hpp"
#include "Statements/BreakStatement.hpp"
#include "Statements/YieldBreakStatement.hpp"
#include "Statements/ReturnStatement.hpp"
#include "Statements/ThrowStatement.hpp"
#include "Statements/ExpressionStatement.hpp"
#include "Statements/BlockStatement.hpp"
#include "Statements/GotoStatement.hpp"
#include "Statements/GotoCaseStatement.hpp"
#include "Statements/GotoDefaultStatement.hpp"
#include "Statements/IfElseStatement.hpp"
#include "Statements/WhileStatement.hpp"
#include "Statements/DoWhileStatement.hpp"
#include "Statements/YieldReturnStatement.hpp"
#include "Statements/EmptyStatement.hpp"
#include "Statements/LabelStatement.hpp"
#include "Statements/CheckedStatement.hpp"
#include "Statements/UncheckedStatement.hpp"
#include "Statements/UnsafeStatement.hpp"
#include "Statements/LockStatement.hpp"
#include "Statements/UsingStatement.hpp"
#include "Statements/ForStatement.hpp"
#include "Statements/ForeachStatement.hpp"
#include "SingleVariableDesignation.hpp"
#include "ParenthesizedVariableDesignation.hpp"
#include "VariableInitializer.hpp"
#include "Statements/FixedStatement.hpp"
#include "CaseLabel.hpp"
#include "SwitchSection.hpp"
#include "Statements/SwitchStatement.hpp"
#include "CatchClause.hpp"
#include "Statements/TryCatchStatement.hpp"
#include "Statements/VariableDeclarationStatement.hpp"
#include "DestructorDeclaration.hpp"
#include "FieldDeclaration.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract class DepthFirstAstVisitor : IAstVisitor` -- the depth-first
// visitor base. Abstract (a protected constructor): cannot be instantiated directly, only
// derived from, matching the C# `abstract class`. Per-node `Visit` overrides (added as
// the concrete node hierarchy lands) default to `VisitChildren`.
class DepthFirstAstVisitor : public IAstVisitor {
protected:
    DepthFirstAstVisitor() = default;

    // The C# `protected virtual void VisitChildren(AstNode node)`: visits every child of
    // `node` in document order by calling `child.AcceptVisitor(this)`. `Children()`
    // enumerates in document order and its enumerator captures each child's successor
    // before yielding it, so a `Visit` override that removes or replaces the current child
    // mid-walk does not lose the place -- the hand-over-hand guarantee the transforms rely
    // on. Each child's `AcceptVisitor(this)` dispatches back to this visitor's matching
    // `Visit<ChildNode>`, which (by default) recurses via `VisitChildren` -- the
    // depth-first traversal.
    virtual void VisitChildren(AstNode* node) {
        if (node == nullptr)
            return;
        for (AstNode* child : node->Children()) {
            child->AcceptVisitor(*this);
        }
    }

public:
    ~DepthFirstAstVisitor() override = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` overrides. Each defaults to
    // `VisitChildren(node)` (the depth-first walk), matching the C# generator's emitted
    // `public virtual void Visit<NodeName>(<Node> node) { VisitChildren(node); }`; a derived
    // visitor overrides only the nodes it cares about. The concrete leaf nodes land here (the
    // rest of the generated hierarchy follows): the `Identifier` token (a leaf, no children),
    // then the leaf expressions (the three reference expressions and the literal-carrying
    // `PrimitiveExpression`, all leaves). The concrete node headers are included above so the
    // implicit `ConcreteNode* -> AstNode*` upcast in `VisitChildren(node)` has the complete
    // derived type.
    virtual void VisitIdentifier(Identifier* node) {
        VisitChildren(node);
    }
    virtual void VisitNullReferenceExpression(NullReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitThisReferenceExpression(ThisReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitBaseReferenceExpression(BaseReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitPrimitiveExpression(PrimitiveExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitBinaryOperatorExpression(BinaryOperatorExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitAssignmentExpression(AssignmentExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitUnaryOperatorExpression(UnaryOperatorExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitConditionalExpression(ConditionalExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitParenthesizedExpression(ParenthesizedExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitCheckedExpression(CheckedExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitUncheckedExpression(UncheckedExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitDirectionExpression(DirectionExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitThrowExpression(ThrowExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitPrimitiveType(PrimitiveType* node) {
        VisitChildren(node);
    }
    virtual void VisitSimpleType(SimpleType* node) {
        VisitChildren(node);
    }
    virtual void VisitMemberType(MemberType* node) {
        VisitChildren(node);
    }
    virtual void VisitArraySpecifier(ArraySpecifier* node) {
        VisitChildren(node);
    }
    virtual void VisitAttribute(Attribute* node) {
        VisitChildren(node);
    }
    virtual void VisitAttributeSection(AttributeSection* node) {
        VisitChildren(node);
    }
    virtual void VisitComposedType(ComposedType* node) {
        VisitChildren(node);
    }
    virtual void VisitCastExpression(CastExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitAsExpression(AsExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitIsExpression(IsExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitTypeReferenceExpression(TypeReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitTypeOfExpression(TypeOfExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitDefaultValueExpression(DefaultValueExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitSizeOfExpression(SizeOfExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitIdentifierExpression(IdentifierExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitMemberReferenceExpression(MemberReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitPointerReferenceExpression(PointerReferenceExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitInvocationExpression(InvocationExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitIndexerExpression(IndexerExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitArrayInitializerExpression(ArrayInitializerExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitObjectCreateExpression(ObjectCreateExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitArrayCreateExpression(ArrayCreateExpression* node) {
        VisitChildren(node);
    }
    virtual void VisitContinueStatement(ContinueStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitBreakStatement(BreakStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitYieldBreakStatement(YieldBreakStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitReturnStatement(ReturnStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitThrowStatement(ThrowStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitExpressionStatement(ExpressionStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitBlockStatement(BlockStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitGotoStatement(GotoStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitGotoCaseStatement(GotoCaseStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitGotoDefaultStatement(GotoDefaultStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitIfElseStatement(IfElseStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitWhileStatement(WhileStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitDoWhileStatement(DoWhileStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitYieldReturnStatement(YieldReturnStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitEmptyStatement(EmptyStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitLabelStatement(LabelStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitCheckedStatement(CheckedStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitUncheckedStatement(UncheckedStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitUnsafeStatement(UnsafeStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitLockStatement(LockStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitUsingStatement(UsingStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitForStatement(ForStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitSingleVariableDesignation(SingleVariableDesignation* node) {
        VisitChildren(node);
    }
    virtual void VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation* node) {
        VisitChildren(node);
    }
    virtual void VisitForeachStatement(ForeachStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitVariableInitializer(VariableInitializer* node) {
        VisitChildren(node);
    }
    virtual void VisitFixedStatement(FixedStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitCaseLabel(CaseLabel* node) {
        VisitChildren(node);
    }
    virtual void VisitSwitchSection(SwitchSection* node) {
        VisitChildren(node);
    }
    virtual void VisitSwitchStatement(SwitchStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitCatchClause(CatchClause* node) {
        VisitChildren(node);
    }
    virtual void VisitTryCatchStatement(TryCatchStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitVariableDeclarationStatement(VariableDeclarationStatement* node) {
        VisitChildren(node);
    }
    virtual void VisitDestructorDeclaration(DestructorDeclaration* node) {
        VisitChildren(node);
    }
    virtual void VisitFieldDeclaration(FieldDeclaration* node) {
        VisitChildren(node);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITOR_HPP
