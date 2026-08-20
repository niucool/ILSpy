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
// `T = bool` -- the default implementation of `IAstVisitor<bool>` whose every per-node
// `Visit<NodeName>` method delegates to `VisitChildren` (a depth-first walk of the node's
// children calling `AcceptVisitorBool` on each) and returns `default(bool)` (`false`).
// Concrete visitors derive from it and override only the `Visit` methods (and/or
// `VisitChildren`) they care about; the rest fall through to the depth-first walk.
//
// Paired with `IAstVisitor<bool>` (IAstVisitorBool.hpp) and `AstNode::AcceptVisitorBool`
// (the `<bool>`-variant dispatch entry, declared abstract on `AstNode`). Mirrors the void
// `DepthFirstAstVisitor` (DepthFirstAstVisitor.hpp); the only differences are the `bool`
// return type and the `AcceptVisitorBool` dispatch (C++ has no virtual template methods, so
// the C# generic `AcceptVisitor<T>` is realized as a per-instantiation `AcceptVisitorBool`,
// matching the void `AcceptVisitor(IAstVisitor&)` precedent on `AstNode`).
//
// The sole engine consumer is `GenericGrammarAmbiguityVisitor : DepthFirstAstVisitor<bool>`
// (the "F(G<A,B>(7));" grammar-ambiguity resolver), which overrides `VisitChildren` (to
// return `true` -- stop visiting on an unhandled node) and four per-node `Visit` methods;
// the C# decompiler pipeline runs it over the root `SyntaxTree` immediately after
// `InsertParenthesesVisitor` (so the parenthesization the ambiguity depends on is in place).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITORBOOL_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITORBOOL_HPP

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract class DepthFirstAstVisitor<T> : IAstVisitor<T>` (instantiated
// `T = bool`) -- the depth-first visitor base for the `bool`-returning visitor. Abstract (a
// protected constructor): cannot be instantiated directly, only derived from. Per-node
// `Visit` overrides default to `VisitChildren` (returning `default(bool)` = `false`); a
// derived visitor overrides only the nodes/`VisitChildren` it cares about.
class DepthFirstAstVisitorBool : public IAstVisitorBool {
protected:
    DepthFirstAstVisitorBool() = default;

    // The C# `protected virtual T VisitChildren(AstNode node)`: visits every child of `node`
    // in document order by calling `child.AcceptVisitor(this)` (the child's `AcceptVisitorBool`
    // dispatches back to this visitor's matching `Visit<ChildNode>`). The return of each
    // `child.AcceptVisitorBool` is discarded -- `VisitChildren` walks for its side effect and
    // returns `default(T)` (`false` for `bool`); the per-node `Visit` overrides that need the
    // stop/continue result inspect it directly (`GenericGrammarAmbiguityVisitor`'s
    // `VisitBinaryOperatorExpression`/`VisitMemberReferenceExpression` do this). A null `node`
    // is a no-op (every call site guards null; the guard matches the void
    // `DepthFirstAstVisitor::VisitChildren`).
    virtual bool VisitChildren(AstNode* node) {
        if (node == nullptr)
            return false;
        for (AstNode* child : node->Children()) {
            child->AcceptVisitorBool(*this);
        }
        return false;
    }

public:
    ~DepthFirstAstVisitorBool() override = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` overrides. Each defaults to
    // `VisitChildren(node)` (returning `false` = `default(bool)`), matching the C#
    // `public virtual T Visit<NodeName>(<Node> node) { return VisitChildren(node); }`; a
    // derived visitor overrides only the nodes it cares about.
    virtual bool VisitIdentifier(Identifier* node) { return VisitChildren(node); }
    virtual bool VisitNullReferenceExpression(NullReferenceExpression* node) { return VisitChildren(node); }
    virtual bool VisitThisReferenceExpression(ThisReferenceExpression* node) { return VisitChildren(node); }
    virtual bool VisitBaseReferenceExpression(BaseReferenceExpression* node) { return VisitChildren(node); }
    virtual bool VisitPrimitiveExpression(PrimitiveExpression* node) { return VisitChildren(node); }
    virtual bool VisitBinaryOperatorExpression(BinaryOperatorExpression* node) { return VisitChildren(node); }
    virtual bool VisitAssignmentExpression(AssignmentExpression* node) { return VisitChildren(node); }
    virtual bool VisitUnaryOperatorExpression(UnaryOperatorExpression* node) { return VisitChildren(node); }
    virtual bool VisitConditionalExpression(ConditionalExpression* node) { return VisitChildren(node); }
    virtual bool VisitParenthesizedExpression(ParenthesizedExpression* node) { return VisitChildren(node); }
    virtual bool VisitCheckedExpression(CheckedExpression* node) { return VisitChildren(node); }
    virtual bool VisitUncheckedExpression(UncheckedExpression* node) { return VisitChildren(node); }
    virtual bool VisitDirectionExpression(DirectionExpression* node) { return VisitChildren(node); }
    virtual bool VisitThrowExpression(ThrowExpression* node) { return VisitChildren(node); }
    virtual bool VisitPrimitiveType(PrimitiveType* node) { return VisitChildren(node); }
    virtual bool VisitSimpleType(SimpleType* node) { return VisitChildren(node); }
    virtual bool VisitMemberType(MemberType* node) { return VisitChildren(node); }
    virtual bool VisitArraySpecifier(ArraySpecifier* node) { return VisitChildren(node); }
    virtual bool VisitAttribute(Attribute* node) { return VisitChildren(node); }
    virtual bool VisitAttributeSection(AttributeSection* node) { return VisitChildren(node); }
    virtual bool VisitComposedType(ComposedType* node) { return VisitChildren(node); }
    virtual bool VisitCastExpression(CastExpression* node) { return VisitChildren(node); }
    virtual bool VisitAsExpression(AsExpression* node) { return VisitChildren(node); }
    virtual bool VisitIsExpression(IsExpression* node) { return VisitChildren(node); }
    virtual bool VisitTypeReferenceExpression(TypeReferenceExpression* node) { return VisitChildren(node); }
    virtual bool VisitTypeOfExpression(TypeOfExpression* node) { return VisitChildren(node); }
    virtual bool VisitDefaultValueExpression(DefaultValueExpression* node) { return VisitChildren(node); }
    virtual bool VisitSizeOfExpression(SizeOfExpression* node) { return VisitChildren(node); }
    virtual bool VisitIdentifierExpression(IdentifierExpression* node) { return VisitChildren(node); }
    virtual bool VisitMemberReferenceExpression(MemberReferenceExpression* node) { return VisitChildren(node); }
    virtual bool VisitPointerReferenceExpression(PointerReferenceExpression* node) { return VisitChildren(node); }
    virtual bool VisitInvocationExpression(InvocationExpression* node) { return VisitChildren(node); }
    virtual bool VisitIndexerExpression(IndexerExpression* node) { return VisitChildren(node); }
    virtual bool VisitArrayInitializerExpression(ArrayInitializerExpression* node) { return VisitChildren(node); }
    virtual bool VisitObjectCreateExpression(ObjectCreateExpression* node) { return VisitChildren(node); }
    virtual bool VisitArrayCreateExpression(ArrayCreateExpression* node) { return VisitChildren(node); }
    virtual bool VisitTupleExpression(TupleExpression* node) { return VisitChildren(node); }
    virtual bool VisitNamedExpression(NamedExpression* node) { return VisitChildren(node); }
    virtual bool VisitNamedArgumentExpression(NamedArgumentExpression* node) { return VisitChildren(node); }
    virtual bool VisitErrorExpression(ErrorExpression* node) { return VisitChildren(node); }
    virtual bool VisitOutVarDeclarationExpression(OutVarDeclarationExpression* node) { return VisitChildren(node); }
    virtual bool VisitWithInitializerExpression(WithInitializerExpression* node) { return VisitChildren(node); }
    virtual bool VisitUndocumentedExpression(UndocumentedExpression* node) { return VisitChildren(node); }
    virtual bool VisitStackAllocExpression(StackAllocExpression* node) { return VisitChildren(node); }
    virtual bool VisitContinueStatement(ContinueStatement* node) { return VisitChildren(node); }
    virtual bool VisitBreakStatement(BreakStatement* node) { return VisitChildren(node); }
    virtual bool VisitYieldBreakStatement(YieldBreakStatement* node) { return VisitChildren(node); }
    virtual bool VisitReturnStatement(ReturnStatement* node) { return VisitChildren(node); }
    virtual bool VisitThrowStatement(ThrowStatement* node) { return VisitChildren(node); }
    virtual bool VisitExpressionStatement(ExpressionStatement* node) { return VisitChildren(node); }
    virtual bool VisitBlockStatement(BlockStatement* node) { return VisitChildren(node); }
    virtual bool VisitGotoStatement(GotoStatement* node) { return VisitChildren(node); }
    virtual bool VisitGotoCaseStatement(GotoCaseStatement* node) { return VisitChildren(node); }
    virtual bool VisitGotoDefaultStatement(GotoDefaultStatement* node) { return VisitChildren(node); }
    virtual bool VisitIfElseStatement(IfElseStatement* node) { return VisitChildren(node); }
    virtual bool VisitWhileStatement(WhileStatement* node) { return VisitChildren(node); }
    virtual bool VisitDoWhileStatement(DoWhileStatement* node) { return VisitChildren(node); }
    virtual bool VisitYieldReturnStatement(YieldReturnStatement* node) { return VisitChildren(node); }
    virtual bool VisitEmptyStatement(EmptyStatement* node) { return VisitChildren(node); }
    virtual bool VisitLabelStatement(LabelStatement* node) { return VisitChildren(node); }
    virtual bool VisitCheckedStatement(CheckedStatement* node) { return VisitChildren(node); }
    virtual bool VisitUncheckedStatement(UncheckedStatement* node) { return VisitChildren(node); }
    virtual bool VisitUnsafeStatement(UnsafeStatement* node) { return VisitChildren(node); }
    virtual bool VisitLockStatement(LockStatement* node) { return VisitChildren(node); }
    virtual bool VisitUsingStatement(UsingStatement* node) { return VisitChildren(node); }
    virtual bool VisitForStatement(ForStatement* node) { return VisitChildren(node); }
    virtual bool VisitSingleVariableDesignation(SingleVariableDesignation* node) { return VisitChildren(node); }
    virtual bool VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation* node) { return VisitChildren(node); }
    virtual bool VisitForeachStatement(ForeachStatement* node) { return VisitChildren(node); }
    virtual bool VisitVariableInitializer(VariableInitializer* node) { return VisitChildren(node); }
    virtual bool VisitFixedStatement(FixedStatement* node) { return VisitChildren(node); }
    virtual bool VisitCaseLabel(CaseLabel* node) { return VisitChildren(node); }
    virtual bool VisitSwitchSection(SwitchSection* node) { return VisitChildren(node); }
    virtual bool VisitSwitchStatement(SwitchStatement* node) { return VisitChildren(node); }
    virtual bool VisitCatchClause(CatchClause* node) { return VisitChildren(node); }
    virtual bool VisitTryCatchStatement(TryCatchStatement* node) { return VisitChildren(node); }
    virtual bool VisitVariableDeclarationStatement(VariableDeclarationStatement* node) { return VisitChildren(node); }
    virtual bool VisitDestructorDeclaration(DestructorDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitFieldDeclaration(FieldDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitAccessor(Accessor* node) { return VisitChildren(node); }
    virtual bool VisitEnumMemberDeclaration(EnumMemberDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitPropertyDeclaration(PropertyDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitEventDeclaration(EventDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitCustomEventDeclaration(CustomEventDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitParameterDeclaration(ParameterDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitIndexerDeclaration(IndexerDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitOperatorDeclaration(OperatorDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitConstructorInitializer(ConstructorInitializer* node) { return VisitChildren(node); }
    virtual bool VisitConstructorDeclaration(ConstructorDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitTypeParameterDeclaration(TypeParameterDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitConstraint(Constraint* node) { return VisitChildren(node); }
    virtual bool VisitMethodDeclaration(MethodDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitExtensionDeclaration(ExtensionDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitFixedVariableInitializer(FixedVariableInitializer* node) { return VisitChildren(node); }
    virtual bool VisitFixedFieldDeclaration(FixedFieldDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitLocalFunctionDeclarationStatement(LocalFunctionDeclarationStatement* node) { return VisitChildren(node); }
    virtual bool VisitComment(Comment* node) { return VisitChildren(node); }
    virtual bool VisitExternAliasDeclaration(ExternAliasDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitUsingDeclaration(UsingDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitUsingAliasDeclaration(UsingAliasDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitTupleTypeElement(TupleTypeElement* node) { return VisitChildren(node); }
    virtual bool VisitTupleType(TupleAstType* node) { return VisitChildren(node); }
    virtual bool VisitInvocationType(InvocationAstType* node) { return VisitChildren(node); }
    virtual bool VisitFunctionPointerType(FunctionPointerAstType* node) { return VisitChildren(node); }
    virtual bool VisitDelegateDeclaration(DelegateDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitTypeDeclaration(TypeDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitNamespaceDeclaration(NamespaceDeclaration* node) { return VisitChildren(node); }
    virtual bool VisitPreProcessorDirective(PreProcessorDirective* node) { return VisitChildren(node); }
    virtual bool VisitDocumentationReference(DocumentationReference* node) { return VisitChildren(node); }
    virtual bool VisitDeclarationExpression(DeclarationExpression* node) { return VisitChildren(node); }
    virtual bool VisitAnonymousTypeCreateExpression(AnonymousTypeCreateExpression* node) { return VisitChildren(node); }
    virtual bool VisitLambdaExpression(LambdaExpression* node) { return VisitChildren(node); }
    virtual bool VisitAnonymousMethodExpression(AnonymousMethodExpression* node) { return VisitChildren(node); }
    virtual bool VisitSwitchExpressionSection(SwitchExpressionSection* node) { return VisitChildren(node); }
    virtual bool VisitSwitchExpression(SwitchExpression* node) { return VisitChildren(node); }
    virtual bool VisitRecursivePatternExpression(RecursivePatternExpression* node) { return VisitChildren(node); }
    virtual bool VisitInterpolation(Interpolation* node) { return VisitChildren(node); }
    virtual bool VisitInterpolatedStringText(InterpolatedStringText* node) { return VisitChildren(node); }
    virtual bool VisitInterpolatedStringExpression(InterpolatedStringExpression* node) { return VisitChildren(node); }
    virtual bool VisitQueryOrdering(QueryOrdering* node) { return VisitChildren(node); }
    virtual bool VisitQueryExpression(QueryExpression* node) { return VisitChildren(node); }
    virtual bool VisitQueryWhereClause(QueryWhereClause* node) { return VisitChildren(node); }
    virtual bool VisitQuerySelectClause(QuerySelectClause* node) { return VisitChildren(node); }
    virtual bool VisitQueryOrderClause(QueryOrderClause* node) { return VisitChildren(node); }
    virtual bool VisitQueryLetClause(QueryLetClause* node) { return VisitChildren(node); }
    virtual bool VisitQueryGroupClause(QueryGroupClause* node) { return VisitChildren(node); }
    virtual bool VisitQueryFromClause(QueryFromClause* node) { return VisitChildren(node); }
    virtual bool VisitQueryContinuationClause(QueryContinuationClause* node) { return VisitChildren(node); }
    virtual bool VisitQueryJoinClause(QueryJoinClause* node) { return VisitChildren(node); }
    virtual bool VisitSyntaxTree(SyntaxTree* node) { return VisitChildren(node); }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DEPTHFIRSTASTVISITORBOOL_HPP
