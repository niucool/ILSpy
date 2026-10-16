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

// Port of the `IAstVisitor<out S>` generic visitor interface in
// ICSharpCode.Decompiler/CSharp/Syntax (the generated `IAstVisitor.g.cs`, emitted by

// Port of the `IAstVisitor<out S>` generic visitor interface instantiated `S = AstNode` --
// the result-returning dispatch interface the `DepthFirstAstVisitor<T>` (here
// `DepthFirstAstVisitor<AstNode>`) consumes. Mirrors `IAstVisitorBool` (IAstVisitorBool.hpp)
// but each per-node `Visit<NodeName>(ConcreteNode*)` returns the visitor's `AstNode*` result
// instead of `bool`.
//
// The C# generic `abstract T AcceptVisitor<T>(IAstVisitor<T>)` cannot be ported verbatim (C++
// has no virtual template methods), so the port realizes each needed instantiation as a
// per-result-type virtual on `AstNode`; `AstNode::AcceptVisitorAstNode` is the `<AstNode>`
// instantiation. Because a node's per-node dispatch is already virtualized for the `bool`
// instantiation (`AcceptVisitorBool`), the `<AstNode>` dispatch reuses it through a private
// adapter (see AstNodeVisitorAstNode.cpp): the node routes to `AcceptVisitorBool`, whose
// adapter forwards each per-node call to the matching `Visit<NodeName>` here and captures the
// `AstNode*` result. This avoids a second per-node virtual on every concrete node while
// keeping this interface's signatures faithful to the C# `IAstVisitor<T>`.
//
// The sole engine consumer is `ContextTrackingVisitor` (the base of the pattern-based
// transforms such as PatternStatementTransform), which needs the node a visit returned so the
// walk can iterate a replaced node.
//
// This header reuses `IAstVisitor.hpp`'s forward declarations of the concrete node types (a
// pointer parameter needs only a forward declaration), so it includes `IAstVisitor.hpp`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITORASTNODE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITORASTNODE_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public interface IAstVisitor<out S>` (instantiated `S = AstNode`). An abstract base
// (a protected constructor + a virtual destructor): it cannot be instantiated directly, only
// derived from, matching the C# interface. A concrete node's `AcceptVisitorAstNode` routes
// through `AcceptVisitorBool` to the matching `Visit<NodeName>(this)` implemented here.
class IAstVisitorAstNode {
protected:
    IAstVisitorAstNode() = default;

public:
    virtual ~IAstVisitorAstNode() = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` pure-virtual methods, each returning the
    // visitor's `AstNode*` result. A derived visitor overrides only the nodes it cares about;
    // the rest fall through to the `DepthFirstAstVisitor<AstNode>` base (which returns null).
    virtual AstNode* VisitIdentifier(Identifier*) = 0;
    virtual AstNode* VisitNullReferenceExpression(NullReferenceExpression*) = 0;
    virtual AstNode* VisitThisReferenceExpression(ThisReferenceExpression*) = 0;
    virtual AstNode* VisitBaseReferenceExpression(BaseReferenceExpression*) = 0;
    virtual AstNode* VisitPrimitiveExpression(PrimitiveExpression*) = 0;
    virtual AstNode* VisitBinaryOperatorExpression(BinaryOperatorExpression*) = 0;
    virtual AstNode* VisitAssignmentExpression(AssignmentExpression*) = 0;
    virtual AstNode* VisitUnaryOperatorExpression(UnaryOperatorExpression*) = 0;
    virtual AstNode* VisitConditionalExpression(ConditionalExpression*) = 0;
    virtual AstNode* VisitParenthesizedExpression(ParenthesizedExpression*) = 0;
    virtual AstNode* VisitCheckedExpression(CheckedExpression*) = 0;
    virtual AstNode* VisitUncheckedExpression(UncheckedExpression*) = 0;
    virtual AstNode* VisitDirectionExpression(DirectionExpression*) = 0;
    virtual AstNode* VisitThrowExpression(ThrowExpression*) = 0;
    virtual AstNode* VisitPrimitiveType(PrimitiveType*) = 0;
    virtual AstNode* VisitSimpleType(SimpleType*) = 0;
    virtual AstNode* VisitMemberType(MemberType*) = 0;
    virtual AstNode* VisitArraySpecifier(ArraySpecifier*) = 0;
    virtual AstNode* VisitAttribute(Attribute*) = 0;
    virtual AstNode* VisitAttributeSection(AttributeSection*) = 0;
    virtual AstNode* VisitComposedType(ComposedType*) = 0;
    virtual AstNode* VisitCastExpression(CastExpression*) = 0;
    virtual AstNode* VisitAsExpression(AsExpression*) = 0;
    virtual AstNode* VisitIsExpression(IsExpression*) = 0;
    virtual AstNode* VisitTypeReferenceExpression(TypeReferenceExpression*) = 0;
    virtual AstNode* VisitTypeOfExpression(TypeOfExpression*) = 0;
    virtual AstNode* VisitDefaultValueExpression(DefaultValueExpression*) = 0;
    virtual AstNode* VisitSizeOfExpression(SizeOfExpression*) = 0;
    virtual AstNode* VisitIdentifierExpression(IdentifierExpression*) = 0;
    virtual AstNode* VisitMemberReferenceExpression(MemberReferenceExpression*) = 0;
    virtual AstNode* VisitPointerReferenceExpression(PointerReferenceExpression*) = 0;
    virtual AstNode* VisitInvocationExpression(InvocationExpression*) = 0;
    virtual AstNode* VisitIndexerExpression(IndexerExpression*) = 0;
    virtual AstNode* VisitArrayInitializerExpression(ArrayInitializerExpression*) = 0;
    virtual AstNode* VisitObjectCreateExpression(ObjectCreateExpression*) = 0;
    virtual AstNode* VisitArrayCreateExpression(ArrayCreateExpression*) = 0;
    virtual AstNode* VisitTupleExpression(TupleExpression*) = 0;
    virtual AstNode* VisitNamedExpression(NamedExpression*) = 0;
    virtual AstNode* VisitNamedArgumentExpression(NamedArgumentExpression*) = 0;
    virtual AstNode* VisitErrorExpression(ErrorExpression*) = 0;
    virtual AstNode* VisitOutVarDeclarationExpression(OutVarDeclarationExpression*) = 0;
    virtual AstNode* VisitWithInitializerExpression(WithInitializerExpression*) = 0;
    virtual AstNode* VisitUndocumentedExpression(UndocumentedExpression*) = 0;
    virtual AstNode* VisitStackAllocExpression(StackAllocExpression*) = 0;
    virtual AstNode* VisitContinueStatement(ContinueStatement*) = 0;
    virtual AstNode* VisitBreakStatement(BreakStatement*) = 0;
    virtual AstNode* VisitYieldBreakStatement(YieldBreakStatement*) = 0;
    virtual AstNode* VisitReturnStatement(ReturnStatement*) = 0;
    virtual AstNode* VisitThrowStatement(ThrowStatement*) = 0;
    virtual AstNode* VisitExpressionStatement(ExpressionStatement*) = 0;
    virtual AstNode* VisitBlockStatement(BlockStatement*) = 0;
    virtual AstNode* VisitGotoStatement(GotoStatement*) = 0;
    virtual AstNode* VisitGotoCaseStatement(GotoCaseStatement*) = 0;
    virtual AstNode* VisitGotoDefaultStatement(GotoDefaultStatement*) = 0;
    virtual AstNode* VisitIfElseStatement(IfElseStatement*) = 0;
    virtual AstNode* VisitWhileStatement(WhileStatement*) = 0;
    virtual AstNode* VisitDoWhileStatement(DoWhileStatement*) = 0;
    virtual AstNode* VisitYieldReturnStatement(YieldReturnStatement*) = 0;
    virtual AstNode* VisitEmptyStatement(EmptyStatement*) = 0;
    virtual AstNode* VisitLabelStatement(LabelStatement*) = 0;
    virtual AstNode* VisitCheckedStatement(CheckedStatement*) = 0;
    virtual AstNode* VisitUncheckedStatement(UncheckedStatement*) = 0;
    virtual AstNode* VisitUnsafeStatement(UnsafeStatement*) = 0;
    virtual AstNode* VisitLockStatement(LockStatement*) = 0;
    virtual AstNode* VisitUsingStatement(UsingStatement*) = 0;
    virtual AstNode* VisitForStatement(ForStatement*) = 0;
    virtual AstNode* VisitSingleVariableDesignation(SingleVariableDesignation*) = 0;
    virtual AstNode* VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation*) = 0;
    virtual AstNode* VisitForeachStatement(ForeachStatement*) = 0;
    virtual AstNode* VisitVariableInitializer(VariableInitializer*) = 0;
    virtual AstNode* VisitFixedStatement(FixedStatement*) = 0;
    virtual AstNode* VisitCaseLabel(CaseLabel*) = 0;
    virtual AstNode* VisitSwitchSection(SwitchSection*) = 0;
    virtual AstNode* VisitSwitchStatement(SwitchStatement*) = 0;
    virtual AstNode* VisitCatchClause(CatchClause*) = 0;
    virtual AstNode* VisitTryCatchStatement(TryCatchStatement*) = 0;
    virtual AstNode* VisitVariableDeclarationStatement(VariableDeclarationStatement*) = 0;
    virtual AstNode* VisitDestructorDeclaration(DestructorDeclaration*) = 0;
    virtual AstNode* VisitFieldDeclaration(FieldDeclaration*) = 0;
    virtual AstNode* VisitAccessor(Accessor*) = 0;
    virtual AstNode* VisitEnumMemberDeclaration(EnumMemberDeclaration*) = 0;
    virtual AstNode* VisitPropertyDeclaration(PropertyDeclaration*) = 0;
    virtual AstNode* VisitEventDeclaration(EventDeclaration*) = 0;
    virtual AstNode* VisitCustomEventDeclaration(CustomEventDeclaration*) = 0;
    virtual AstNode* VisitParameterDeclaration(ParameterDeclaration*) = 0;
    virtual AstNode* VisitIndexerDeclaration(IndexerDeclaration*) = 0;
    virtual AstNode* VisitOperatorDeclaration(OperatorDeclaration*) = 0;
    virtual AstNode* VisitConstructorInitializer(ConstructorInitializer*) = 0;
    virtual AstNode* VisitConstructorDeclaration(ConstructorDeclaration*) = 0;
    virtual AstNode* VisitTypeParameterDeclaration(TypeParameterDeclaration*) = 0;
    virtual AstNode* VisitConstraint(Constraint*) = 0;
    virtual AstNode* VisitMethodDeclaration(MethodDeclaration*) = 0;
    virtual AstNode* VisitExtensionDeclaration(ExtensionDeclaration*) = 0;
    virtual AstNode* VisitFixedVariableInitializer(FixedVariableInitializer*) = 0;
    virtual AstNode* VisitFixedFieldDeclaration(FixedFieldDeclaration*) = 0;
    virtual AstNode* VisitLocalFunctionDeclarationStatement(LocalFunctionDeclarationStatement*) = 0;
    virtual AstNode* VisitComment(Comment*) = 0;
    virtual AstNode* VisitExternAliasDeclaration(ExternAliasDeclaration*) = 0;
    virtual AstNode* VisitUsingDeclaration(UsingDeclaration*) = 0;
    virtual AstNode* VisitUsingAliasDeclaration(UsingAliasDeclaration*) = 0;
    virtual AstNode* VisitTupleTypeElement(TupleTypeElement*) = 0;
    virtual AstNode* VisitTupleType(TupleAstType*) = 0;
    virtual AstNode* VisitInvocationType(InvocationAstType*) = 0;
    virtual AstNode* VisitFunctionPointerType(FunctionPointerAstType*) = 0;
    virtual AstNode* VisitDelegateDeclaration(DelegateDeclaration*) = 0;
    virtual AstNode* VisitTypeDeclaration(TypeDeclaration*) = 0;
    virtual AstNode* VisitNamespaceDeclaration(NamespaceDeclaration*) = 0;
    virtual AstNode* VisitPreProcessorDirective(PreProcessorDirective*) = 0;
    virtual AstNode* VisitDocumentationReference(DocumentationReference*) = 0;
    virtual AstNode* VisitDeclarationExpression(DeclarationExpression*) = 0;
    virtual AstNode* VisitAnonymousTypeCreateExpression(AnonymousTypeCreateExpression*) = 0;
    virtual AstNode* VisitLambdaExpression(LambdaExpression*) = 0;
    virtual AstNode* VisitAnonymousMethodExpression(AnonymousMethodExpression*) = 0;
    virtual AstNode* VisitSwitchExpressionSection(SwitchExpressionSection*) = 0;
    virtual AstNode* VisitSwitchExpression(SwitchExpression*) = 0;
    virtual AstNode* VisitRecursivePatternExpression(RecursivePatternExpression*) = 0;
    virtual AstNode* VisitInterpolation(Interpolation*) = 0;
    virtual AstNode* VisitInterpolatedStringText(InterpolatedStringText*) = 0;
    virtual AstNode* VisitInterpolatedStringExpression(InterpolatedStringExpression*) = 0;
    virtual AstNode* VisitQueryOrdering(QueryOrdering*) = 0;
    virtual AstNode* VisitQueryExpression(QueryExpression*) = 0;
    virtual AstNode* VisitQueryWhereClause(QueryWhereClause*) = 0;
    virtual AstNode* VisitQuerySelectClause(QuerySelectClause*) = 0;
    virtual AstNode* VisitQueryOrderClause(QueryOrderClause*) = 0;
    virtual AstNode* VisitQueryLetClause(QueryLetClause*) = 0;
    virtual AstNode* VisitQueryGroupClause(QueryGroupClause*) = 0;
    virtual AstNode* VisitQueryFromClause(QueryFromClause*) = 0;
    virtual AstNode* VisitQueryContinuationClause(QueryContinuationClause*) = 0;
    virtual AstNode* VisitQueryJoinClause(QueryJoinClause*) = 0;
    virtual AstNode* VisitSyntaxTree(SyntaxTree*) = 0;

    // The C# generator's `T VisitPatternPlaceholder(AstNode placeholder, Pattern pattern)`
    // (the generic visitor's shared arm): the `<AstNode>` realization of the placeholder
    // dispatch. `DepthFirstAstVisitorAstNode` supplies the default walk (which returns null).
    virtual AstNode* VisitPatternPlaceholder(AstNode* placeholder, PatternMatching::Pattern& pattern) = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITORASTNODE_HPP
