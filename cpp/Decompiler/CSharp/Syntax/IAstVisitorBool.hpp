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
// DecompilerSyntaxTreeGenerator.cs), instantiated `S = bool` -- the dispatch interface the
// generic `DepthFirstAstVisitor<T>` (here `DepthFirstAstVisitor<bool>`) consumes. Mirrors
// `IAstVisitor` (the void variant, IAstVisitor.hpp) but each per-node
// `Visit<NodeName>(ConcreteNode*)` returns `S` (the visitor's result) instead of `void`.
//
// The C# generator emits three visitor interfaces -- `IAstVisitor` (returns void), `IAstVisitor<out S>`
// (returns S), and `IAstVisitor<in T, out S>` (returns S, takes T data) -- each carrying one
// `Visit<NodeName>(ConcreteNode)` method per concrete node. Only the void `IAstVisitor` is
// consumed by the pretty-printer (`CSharpOutputVisitor : IAstVisitor`); the `<S>` variant's
// sole engine consumer is `GenericGrammarAmbiguityVisitor : DepthFirstAstVisitor<bool>` (the
// "F(G<A,B>(7));" grammar-ambiguity resolver). C++ has no virtual template methods, so the
// C# generic `abstract T AcceptVisitor<T>(IAstVisitor<T>)` dispatch cannot be ported verbatim;
// the port realizes each needed instantiation as a per-result-type `AcceptVisitor<S>` virtual
// (`AcceptVisitorBool` for `S = bool`) on `AstNode`, mirroring the void
// `AcceptVisitor(IAstVisitor&)` dispatch (the same limitation the void variant's deferral note
// in IAstVisitor.hpp documents). The `<T,S>` two-argument generic variant stays deferred (no
// engine consumer). This header reuses `IAstVisitor.hpp`'s forward declarations of the 130
// concrete node types (a pointer parameter needs only a forward declaration), so it includes
// `IAstVisitor.hpp` rather than re-declaring them.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITORBOOL_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITORBOOL_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public interface IAstVisitor<out S>` (instantiated `S = bool`). A concrete node's
// `AcceptVisitorBool` override routes back to the matching `Visit<NodeName>(this)`; the generic
// `DepthFirstAstVisitor<bool>` base (DepthFirstAstVisitorBool.hpp) supplies the default
// `VisitChildren` walk every per-node `Visit` override delegates to. An abstract base (a
// protected constructor + a virtual destructor): it cannot be instantiated directly, only
// derived from, matching the C# interface.
class IAstVisitorBool {
protected:
    IAstVisitorBool() = default;

public:
    virtual ~IAstVisitorBool() = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` pure-virtual methods, each returning the
    // visitor's `bool` result. Mirrors the void `IAstVisitor`'s per-node methods: a derived
    // visitor overrides only the nodes it cares about; the rest fall through to the
    // `DepthFirstAstVisitor<bool>` base's `VisitChildren` default.
    virtual bool VisitIdentifier(Identifier*) = 0;
    virtual bool VisitNullReferenceExpression(NullReferenceExpression*) = 0;
    virtual bool VisitThisReferenceExpression(ThisReferenceExpression*) = 0;
    virtual bool VisitBaseReferenceExpression(BaseReferenceExpression*) = 0;
    virtual bool VisitPrimitiveExpression(PrimitiveExpression*) = 0;
    virtual bool VisitBinaryOperatorExpression(BinaryOperatorExpression*) = 0;
    virtual bool VisitAssignmentExpression(AssignmentExpression*) = 0;
    virtual bool VisitUnaryOperatorExpression(UnaryOperatorExpression*) = 0;
    virtual bool VisitConditionalExpression(ConditionalExpression*) = 0;
    virtual bool VisitParenthesizedExpression(ParenthesizedExpression*) = 0;
    virtual bool VisitCheckedExpression(CheckedExpression*) = 0;
    virtual bool VisitUncheckedExpression(UncheckedExpression*) = 0;
    virtual bool VisitDirectionExpression(DirectionExpression*) = 0;
    virtual bool VisitThrowExpression(ThrowExpression*) = 0;
    virtual bool VisitPrimitiveType(PrimitiveType*) = 0;
    virtual bool VisitSimpleType(SimpleType*) = 0;
    virtual bool VisitMemberType(MemberType*) = 0;
    virtual bool VisitArraySpecifier(ArraySpecifier*) = 0;
    virtual bool VisitAttribute(Attribute*) = 0;
    virtual bool VisitAttributeSection(AttributeSection*) = 0;
    virtual bool VisitComposedType(ComposedType*) = 0;
    virtual bool VisitCastExpression(CastExpression*) = 0;
    virtual bool VisitAsExpression(AsExpression*) = 0;
    virtual bool VisitIsExpression(IsExpression*) = 0;
    virtual bool VisitTypeReferenceExpression(TypeReferenceExpression*) = 0;
    virtual bool VisitTypeOfExpression(TypeOfExpression*) = 0;
    virtual bool VisitDefaultValueExpression(DefaultValueExpression*) = 0;
    virtual bool VisitSizeOfExpression(SizeOfExpression*) = 0;
    virtual bool VisitIdentifierExpression(IdentifierExpression*) = 0;
    virtual bool VisitMemberReferenceExpression(MemberReferenceExpression*) = 0;
    virtual bool VisitPointerReferenceExpression(PointerReferenceExpression*) = 0;
    virtual bool VisitInvocationExpression(InvocationExpression*) = 0;
    virtual bool VisitIndexerExpression(IndexerExpression*) = 0;
    virtual bool VisitArrayInitializerExpression(ArrayInitializerExpression*) = 0;
    virtual bool VisitObjectCreateExpression(ObjectCreateExpression*) = 0;
    virtual bool VisitArrayCreateExpression(ArrayCreateExpression*) = 0;
    virtual bool VisitTupleExpression(TupleExpression*) = 0;
    virtual bool VisitNamedExpression(NamedExpression*) = 0;
    virtual bool VisitNamedArgumentExpression(NamedArgumentExpression*) = 0;
    virtual bool VisitErrorExpression(ErrorExpression*) = 0;
    virtual bool VisitOutVarDeclarationExpression(OutVarDeclarationExpression*) = 0;
    virtual bool VisitWithInitializerExpression(WithInitializerExpression*) = 0;
    virtual bool VisitUndocumentedExpression(UndocumentedExpression*) = 0;
    virtual bool VisitStackAllocExpression(StackAllocExpression*) = 0;
    virtual bool VisitContinueStatement(ContinueStatement*) = 0;
    virtual bool VisitBreakStatement(BreakStatement*) = 0;
    virtual bool VisitYieldBreakStatement(YieldBreakStatement*) = 0;
    virtual bool VisitReturnStatement(ReturnStatement*) = 0;
    virtual bool VisitThrowStatement(ThrowStatement*) = 0;
    virtual bool VisitExpressionStatement(ExpressionStatement*) = 0;
    virtual bool VisitBlockStatement(BlockStatement*) = 0;
    virtual bool VisitGotoStatement(GotoStatement*) = 0;
    virtual bool VisitGotoCaseStatement(GotoCaseStatement*) = 0;
    virtual bool VisitGotoDefaultStatement(GotoDefaultStatement*) = 0;
    virtual bool VisitIfElseStatement(IfElseStatement*) = 0;
    virtual bool VisitWhileStatement(WhileStatement*) = 0;
    virtual bool VisitDoWhileStatement(DoWhileStatement*) = 0;
    virtual bool VisitYieldReturnStatement(YieldReturnStatement*) = 0;
    virtual bool VisitEmptyStatement(EmptyStatement*) = 0;
    virtual bool VisitLabelStatement(LabelStatement*) = 0;
    virtual bool VisitCheckedStatement(CheckedStatement*) = 0;
    virtual bool VisitUncheckedStatement(UncheckedStatement*) = 0;
    virtual bool VisitUnsafeStatement(UnsafeStatement*) = 0;
    virtual bool VisitLockStatement(LockStatement*) = 0;
    virtual bool VisitUsingStatement(UsingStatement*) = 0;
    virtual bool VisitForStatement(ForStatement*) = 0;
    virtual bool VisitSingleVariableDesignation(SingleVariableDesignation*) = 0;
    virtual bool VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation*) = 0;
    virtual bool VisitForeachStatement(ForeachStatement*) = 0;
    virtual bool VisitVariableInitializer(VariableInitializer*) = 0;
    virtual bool VisitFixedStatement(FixedStatement*) = 0;
    virtual bool VisitCaseLabel(CaseLabel*) = 0;
    virtual bool VisitSwitchSection(SwitchSection*) = 0;
    virtual bool VisitSwitchStatement(SwitchStatement*) = 0;
    virtual bool VisitCatchClause(CatchClause*) = 0;
    virtual bool VisitTryCatchStatement(TryCatchStatement*) = 0;
    virtual bool VisitVariableDeclarationStatement(VariableDeclarationStatement*) = 0;
    virtual bool VisitDestructorDeclaration(DestructorDeclaration*) = 0;
    virtual bool VisitFieldDeclaration(FieldDeclaration*) = 0;
    virtual bool VisitAccessor(Accessor*) = 0;
    virtual bool VisitEnumMemberDeclaration(EnumMemberDeclaration*) = 0;
    virtual bool VisitPropertyDeclaration(PropertyDeclaration*) = 0;
    virtual bool VisitEventDeclaration(EventDeclaration*) = 0;
    virtual bool VisitCustomEventDeclaration(CustomEventDeclaration*) = 0;
    virtual bool VisitParameterDeclaration(ParameterDeclaration*) = 0;
    virtual bool VisitIndexerDeclaration(IndexerDeclaration*) = 0;
    virtual bool VisitOperatorDeclaration(OperatorDeclaration*) = 0;
    virtual bool VisitConstructorInitializer(ConstructorInitializer*) = 0;
    virtual bool VisitConstructorDeclaration(ConstructorDeclaration*) = 0;
    virtual bool VisitTypeParameterDeclaration(TypeParameterDeclaration*) = 0;
    virtual bool VisitConstraint(Constraint*) = 0;
    virtual bool VisitMethodDeclaration(MethodDeclaration*) = 0;
    virtual bool VisitExtensionDeclaration(ExtensionDeclaration*) = 0;
    virtual bool VisitFixedVariableInitializer(FixedVariableInitializer*) = 0;
    virtual bool VisitFixedFieldDeclaration(FixedFieldDeclaration*) = 0;
    virtual bool VisitLocalFunctionDeclarationStatement(LocalFunctionDeclarationStatement*) = 0;
    virtual bool VisitComment(Comment*) = 0;
    virtual bool VisitExternAliasDeclaration(ExternAliasDeclaration*) = 0;
    virtual bool VisitUsingDeclaration(UsingDeclaration*) = 0;
    virtual bool VisitUsingAliasDeclaration(UsingAliasDeclaration*) = 0;
    virtual bool VisitTupleTypeElement(TupleTypeElement*) = 0;
    virtual bool VisitTupleType(TupleAstType*) = 0;
    virtual bool VisitInvocationType(InvocationAstType*) = 0;
    virtual bool VisitFunctionPointerType(FunctionPointerAstType*) = 0;
    virtual bool VisitDelegateDeclaration(DelegateDeclaration*) = 0;
    virtual bool VisitTypeDeclaration(TypeDeclaration*) = 0;
    virtual bool VisitNamespaceDeclaration(NamespaceDeclaration*) = 0;
    virtual bool VisitPreProcessorDirective(PreProcessorDirective*) = 0;
    virtual bool VisitDocumentationReference(DocumentationReference*) = 0;
    virtual bool VisitDeclarationExpression(DeclarationExpression*) = 0;
    virtual bool VisitAnonymousTypeCreateExpression(AnonymousTypeCreateExpression*) = 0;
    virtual bool VisitLambdaExpression(LambdaExpression*) = 0;
    virtual bool VisitAnonymousMethodExpression(AnonymousMethodExpression*) = 0;
    virtual bool VisitSwitchExpressionSection(SwitchExpressionSection*) = 0;
    virtual bool VisitSwitchExpression(SwitchExpression*) = 0;
    virtual bool VisitRecursivePatternExpression(RecursivePatternExpression*) = 0;
    virtual bool VisitInterpolation(Interpolation*) = 0;
    virtual bool VisitInterpolatedStringText(InterpolatedStringText*) = 0;
    virtual bool VisitInterpolatedStringExpression(InterpolatedStringExpression*) = 0;
    virtual bool VisitQueryOrdering(QueryOrdering*) = 0;
    virtual bool VisitQueryExpression(QueryExpression*) = 0;
    virtual bool VisitQueryWhereClause(QueryWhereClause*) = 0;
    virtual bool VisitQuerySelectClause(QuerySelectClause*) = 0;
    virtual bool VisitQueryOrderClause(QueryOrderClause*) = 0;
    virtual bool VisitQueryLetClause(QueryLetClause*) = 0;
    virtual bool VisitQueryGroupClause(QueryGroupClause*) = 0;
    virtual bool VisitQueryFromClause(QueryFromClause*) = 0;
    virtual bool VisitQueryContinuationClause(QueryContinuationClause*) = 0;
    virtual bool VisitQueryJoinClause(QueryJoinClause*) = 0;
    virtual bool VisitSyntaxTree(SyntaxTree*) = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITORBOOL_HPP
