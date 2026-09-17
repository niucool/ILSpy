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

// Port of the `DepthFirstAstVisitor<T>` generic visitor base in
// ICSharpCode.Decompiler/CSharp/Syntax/DepthFirstAstVisitor.cs (lines 706-1379), instantiated

// Port of the `DepthFirstAstVisitor<T>` generic visitor base (DepthFirstAstVisitor.cs lines
// 706-1379), instantiated `T = AstNode` -- the default implementation of
// `IAstVisitor<AstNode>` whose every per-node `Visit<NodeName>` method delegates to
// `VisitChildren` (a depth-first walk of the node's children calling `AcceptVisitorAstNode`
// on each) and returns `default(AstNode)` (null), matching the C# `default(T)!`.
//
// Paired with `IAstVisitorAstNode` (IAstVisitorAstNode.hpp) and `AstNode::AcceptVisitorAstNode`
// (the `<AstNode>`-variant dispatch entry). Mirrors the bool `DepthFirstAstVisitorBool`
// (DepthFirstAstVisitorBool.hpp); the only differences are the `AstNode*` return type and the
// `AcceptVisitorAstNode` dispatch. The sole engine consumer is `ContextTrackingVisitor` (the
// base of the pattern-based transforms such as PatternStatementTransform), whose overridden
// `VisitChildren` returns the node it walked so the transform's walk can iterate a replaced
// node.
//
// IMPORTANT: unlike `DepthFirstAstVisitorBool` (whose default result is `false`), the default
// result here is `nullptr` -- the C# `default(AstNode)` -- so a per-node `Visit` that is not
// overridden returns null, not the node. `PatternStatementTransform` overrides `VisitChildren`
// precisely because it needs to return the node.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITORASTNODE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITORASTNODE_HPP

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorAstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract class DepthFirstAstVisitor<T> : IAstVisitor<T>` (instantiated
// `T = AstNode`) -- the depth-first visitor base for the result-returning visitor. Abstract (a
// protected constructor): cannot be instantiated directly, only derived from. Per-node
// `Visit` overrides default to `VisitChildren` (returning null); a derived visitor overrides
// only the nodes/`VisitChildren` it cares about.
class DepthFirstAstVisitorAstNode : public IAstVisitorAstNode {
protected:
    DepthFirstAstVisitorAstNode() = default;

    // The C# `protected virtual T VisitChildren(AstNode node)`: visits every child of `node` in
    // document order by calling `child.AcceptVisitor(this)` (the child's `AcceptVisitorAstNode`
    // dispatches back to this visitor's matching `Visit<ChildNode>`), then returns
    // `default(T)` (`nullptr`). A null `node` is a no-op (every call site guards null; the
    // guard matches the bool `DepthFirstAstVisitorBool::VisitChildren`). `PatternStatement
    // Transform` overrides this to return the walked node.
    virtual AstNode* VisitChildren(AstNode* node) {
        if (node == nullptr)
            return nullptr;
        for (AstNode* child : node->Children()) {
            child->AcceptVisitorAstNode(*this);
        }
        return nullptr;
    }

public:
    ~DepthFirstAstVisitorAstNode() override = default;

    virtual AstNode* VisitIdentifier(Identifier* node) { return VisitChildren(node); }
    virtual AstNode* VisitNullReferenceExpression(NullReferenceExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitThisReferenceExpression(ThisReferenceExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitBaseReferenceExpression(BaseReferenceExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitPrimitiveExpression(PrimitiveExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitBinaryOperatorExpression(BinaryOperatorExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitAssignmentExpression(AssignmentExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitUnaryOperatorExpression(UnaryOperatorExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitConditionalExpression(ConditionalExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitParenthesizedExpression(ParenthesizedExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitCheckedExpression(CheckedExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitUncheckedExpression(UncheckedExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitDirectionExpression(DirectionExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitThrowExpression(ThrowExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitPrimitiveType(PrimitiveType* node) { return VisitChildren(node); }
    virtual AstNode* VisitSimpleType(SimpleType* node) { return VisitChildren(node); }
    virtual AstNode* VisitMemberType(MemberType* node) { return VisitChildren(node); }
    virtual AstNode* VisitArraySpecifier(ArraySpecifier* node) { return VisitChildren(node); }
    virtual AstNode* VisitAttribute(Attribute* node) { return VisitChildren(node); }
    virtual AstNode* VisitAttributeSection(AttributeSection* node) { return VisitChildren(node); }
    virtual AstNode* VisitComposedType(ComposedType* node) { return VisitChildren(node); }
    virtual AstNode* VisitCastExpression(CastExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitAsExpression(AsExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitIsExpression(IsExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitTypeReferenceExpression(TypeReferenceExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitTypeOfExpression(TypeOfExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitDefaultValueExpression(DefaultValueExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitSizeOfExpression(SizeOfExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitIdentifierExpression(IdentifierExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitMemberReferenceExpression(MemberReferenceExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitPointerReferenceExpression(PointerReferenceExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitInvocationExpression(InvocationExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitIndexerExpression(IndexerExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitArrayInitializerExpression(ArrayInitializerExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitObjectCreateExpression(ObjectCreateExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitArrayCreateExpression(ArrayCreateExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitTupleExpression(TupleExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitNamedExpression(NamedExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitNamedArgumentExpression(NamedArgumentExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitErrorExpression(ErrorExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitOutVarDeclarationExpression(OutVarDeclarationExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitWithInitializerExpression(WithInitializerExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitUndocumentedExpression(UndocumentedExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitStackAllocExpression(StackAllocExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitContinueStatement(ContinueStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitBreakStatement(BreakStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitYieldBreakStatement(YieldBreakStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitReturnStatement(ReturnStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitThrowStatement(ThrowStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitExpressionStatement(ExpressionStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitBlockStatement(BlockStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitGotoStatement(GotoStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitGotoCaseStatement(GotoCaseStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitGotoDefaultStatement(GotoDefaultStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitIfElseStatement(IfElseStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitWhileStatement(WhileStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitDoWhileStatement(DoWhileStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitYieldReturnStatement(YieldReturnStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitEmptyStatement(EmptyStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitLabelStatement(LabelStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitCheckedStatement(CheckedStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitUncheckedStatement(UncheckedStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitUnsafeStatement(UnsafeStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitLockStatement(LockStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitUsingStatement(UsingStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitForStatement(ForStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitSingleVariableDesignation(SingleVariableDesignation* node) { return VisitChildren(node); }
    virtual AstNode* VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation* node) { return VisitChildren(node); }
    virtual AstNode* VisitForeachStatement(ForeachStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitVariableInitializer(VariableInitializer* node) { return VisitChildren(node); }
    virtual AstNode* VisitFixedStatement(FixedStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitCaseLabel(CaseLabel* node) { return VisitChildren(node); }
    virtual AstNode* VisitSwitchSection(SwitchSection* node) { return VisitChildren(node); }
    virtual AstNode* VisitSwitchStatement(SwitchStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitCatchClause(CatchClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitTryCatchStatement(TryCatchStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitVariableDeclarationStatement(VariableDeclarationStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitDestructorDeclaration(DestructorDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitFieldDeclaration(FieldDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitAccessor(Accessor* node) { return VisitChildren(node); }
    virtual AstNode* VisitEnumMemberDeclaration(EnumMemberDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitPropertyDeclaration(PropertyDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitEventDeclaration(EventDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitCustomEventDeclaration(CustomEventDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitParameterDeclaration(ParameterDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitIndexerDeclaration(IndexerDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitOperatorDeclaration(OperatorDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitConstructorInitializer(ConstructorInitializer* node) { return VisitChildren(node); }
    virtual AstNode* VisitConstructorDeclaration(ConstructorDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitTypeParameterDeclaration(TypeParameterDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitConstraint(Constraint* node) { return VisitChildren(node); }
    virtual AstNode* VisitMethodDeclaration(MethodDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitExtensionDeclaration(ExtensionDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitFixedVariableInitializer(FixedVariableInitializer* node) { return VisitChildren(node); }
    virtual AstNode* VisitFixedFieldDeclaration(FixedFieldDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitLocalFunctionDeclarationStatement(LocalFunctionDeclarationStatement* node) { return VisitChildren(node); }
    virtual AstNode* VisitComment(Comment* node) { return VisitChildren(node); }
    virtual AstNode* VisitExternAliasDeclaration(ExternAliasDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitUsingDeclaration(UsingDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitUsingAliasDeclaration(UsingAliasDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitTupleTypeElement(TupleTypeElement* node) { return VisitChildren(node); }
    virtual AstNode* VisitTupleType(TupleAstType* node) { return VisitChildren(node); }
    virtual AstNode* VisitInvocationType(InvocationAstType* node) { return VisitChildren(node); }
    virtual AstNode* VisitFunctionPointerType(FunctionPointerAstType* node) { return VisitChildren(node); }
    virtual AstNode* VisitDelegateDeclaration(DelegateDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitTypeDeclaration(TypeDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitNamespaceDeclaration(NamespaceDeclaration* node) { return VisitChildren(node); }
    virtual AstNode* VisitPreProcessorDirective(PreProcessorDirective* node) { return VisitChildren(node); }
    virtual AstNode* VisitDocumentationReference(DocumentationReference* node) { return VisitChildren(node); }
    virtual AstNode* VisitDeclarationExpression(DeclarationExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitAnonymousTypeCreateExpression(AnonymousTypeCreateExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitLambdaExpression(LambdaExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitAnonymousMethodExpression(AnonymousMethodExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitSwitchExpressionSection(SwitchExpressionSection* node) { return VisitChildren(node); }
    virtual AstNode* VisitSwitchExpression(SwitchExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitRecursivePatternExpression(RecursivePatternExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitInterpolation(Interpolation* node) { return VisitChildren(node); }
    virtual AstNode* VisitInterpolatedStringText(InterpolatedStringText* node) { return VisitChildren(node); }
    virtual AstNode* VisitInterpolatedStringExpression(InterpolatedStringExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryOrdering(QueryOrdering* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryExpression(QueryExpression* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryWhereClause(QueryWhereClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQuerySelectClause(QuerySelectClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryOrderClause(QueryOrderClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryLetClause(QueryLetClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryGroupClause(QueryGroupClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryFromClause(QueryFromClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryContinuationClause(QueryContinuationClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitQueryJoinClause(QueryJoinClause* node) { return VisitChildren(node); }
    virtual AstNode* VisitSyntaxTree(SyntaxTree* node) { return VisitChildren(node); }

    // The C# `public virtual T VisitPatternPlaceholder(AstNode placeholder, Pattern pattern)`
    // -- the `<AstNode>` default walk (the placeholder has no AST children), returning
    // `VisitChildren`'s `nullptr` (the `default(AstNode)`).
    virtual AstNode* VisitPatternPlaceholder(AstNode* placeholder, PatternMatching::Pattern& /*pattern*/) {
        return VisitChildren(placeholder);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITORASTNODE_HPP
