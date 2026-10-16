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

// The `<AstNode>` realization of the C# generic `abstract T AcceptVisitor<T>(IAstVisitor<T>)`
// dispatch on `AstNode`. The node-virtualized dispatch already exists for the `bool`
// instantiation (`AcceptVisitorBool`); this file reuses it for the `AstNode` instantiation
// through a small private adapter that forwards each per-node call to the matching
// `IAstVisitorAstNode::Visit<NodeName>` and captures the `AstNode*` result, so no second
// per-node virtual is added to the concrete node classes.
//
// `AstNode::AcceptVisitorAstNode(IAstVisitorAstNode&)` runs `AcceptVisitorBool` with the
// adapter (the node routes to the right per-node visit), then returns the captured result.
// Recursion (`DepthFirstAstVisitorAstNode::VisitChildren`) re-enters through
// `AcceptVisitorAstNode`, creating a fresh adapter per call, so the captured result always
// belongs to the outermost dispatch.

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorAstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

namespace {

// The `IAstVisitorBool` adapter that realizes the `IAstVisitor<AstNode>` dispatch over the
// `bool` node dispatch: each per-node `Visit<NodeName>` forwards to the target's
// `AstNode*`-returning method and stores the result. The `bool` return value is ignored (the
// `AstNode` result is the payload); it always returns `false`.
class AstNodeVisitorAstNodeAdapter final : public IAstVisitorBool {
public:
    explicit AstNodeVisitorAstNodeAdapter(IAstVisitorAstNode& target) : target_(&target) {}

    AstNode* result = nullptr;

    bool VisitIdentifier(Identifier* node) override { result = target_->VisitIdentifier(node); return false; }
    bool VisitNullReferenceExpression(NullReferenceExpression* node) override { result = target_->VisitNullReferenceExpression(node); return false; }
    bool VisitThisReferenceExpression(ThisReferenceExpression* node) override { result = target_->VisitThisReferenceExpression(node); return false; }
    bool VisitBaseReferenceExpression(BaseReferenceExpression* node) override { result = target_->VisitBaseReferenceExpression(node); return false; }
    bool VisitPrimitiveExpression(PrimitiveExpression* node) override { result = target_->VisitPrimitiveExpression(node); return false; }
    bool VisitBinaryOperatorExpression(BinaryOperatorExpression* node) override { result = target_->VisitBinaryOperatorExpression(node); return false; }
    bool VisitAssignmentExpression(AssignmentExpression* node) override { result = target_->VisitAssignmentExpression(node); return false; }
    bool VisitUnaryOperatorExpression(UnaryOperatorExpression* node) override { result = target_->VisitUnaryOperatorExpression(node); return false; }
    bool VisitConditionalExpression(ConditionalExpression* node) override { result = target_->VisitConditionalExpression(node); return false; }
    bool VisitParenthesizedExpression(ParenthesizedExpression* node) override { result = target_->VisitParenthesizedExpression(node); return false; }
    bool VisitCheckedExpression(CheckedExpression* node) override { result = target_->VisitCheckedExpression(node); return false; }
    bool VisitUncheckedExpression(UncheckedExpression* node) override { result = target_->VisitUncheckedExpression(node); return false; }
    bool VisitDirectionExpression(DirectionExpression* node) override { result = target_->VisitDirectionExpression(node); return false; }
    bool VisitThrowExpression(ThrowExpression* node) override { result = target_->VisitThrowExpression(node); return false; }
    bool VisitPrimitiveType(PrimitiveType* node) override { result = target_->VisitPrimitiveType(node); return false; }
    bool VisitSimpleType(SimpleType* node) override { result = target_->VisitSimpleType(node); return false; }
    bool VisitMemberType(MemberType* node) override { result = target_->VisitMemberType(node); return false; }
    bool VisitArraySpecifier(ArraySpecifier* node) override { result = target_->VisitArraySpecifier(node); return false; }
    bool VisitAttribute(Attribute* node) override { result = target_->VisitAttribute(node); return false; }
    bool VisitAttributeSection(AttributeSection* node) override { result = target_->VisitAttributeSection(node); return false; }
    bool VisitComposedType(ComposedType* node) override { result = target_->VisitComposedType(node); return false; }
    bool VisitCastExpression(CastExpression* node) override { result = target_->VisitCastExpression(node); return false; }
    bool VisitAsExpression(AsExpression* node) override { result = target_->VisitAsExpression(node); return false; }
    bool VisitIsExpression(IsExpression* node) override { result = target_->VisitIsExpression(node); return false; }
    bool VisitTypeReferenceExpression(TypeReferenceExpression* node) override { result = target_->VisitTypeReferenceExpression(node); return false; }
    bool VisitTypeOfExpression(TypeOfExpression* node) override { result = target_->VisitTypeOfExpression(node); return false; }
    bool VisitDefaultValueExpression(DefaultValueExpression* node) override { result = target_->VisitDefaultValueExpression(node); return false; }
    bool VisitSizeOfExpression(SizeOfExpression* node) override { result = target_->VisitSizeOfExpression(node); return false; }
    bool VisitIdentifierExpression(IdentifierExpression* node) override { result = target_->VisitIdentifierExpression(node); return false; }
    bool VisitMemberReferenceExpression(MemberReferenceExpression* node) override { result = target_->VisitMemberReferenceExpression(node); return false; }
    bool VisitPointerReferenceExpression(PointerReferenceExpression* node) override { result = target_->VisitPointerReferenceExpression(node); return false; }
    bool VisitInvocationExpression(InvocationExpression* node) override { result = target_->VisitInvocationExpression(node); return false; }
    bool VisitIndexerExpression(IndexerExpression* node) override { result = target_->VisitIndexerExpression(node); return false; }
    bool VisitArrayInitializerExpression(ArrayInitializerExpression* node) override { result = target_->VisitArrayInitializerExpression(node); return false; }
    bool VisitObjectCreateExpression(ObjectCreateExpression* node) override { result = target_->VisitObjectCreateExpression(node); return false; }
    bool VisitArrayCreateExpression(ArrayCreateExpression* node) override { result = target_->VisitArrayCreateExpression(node); return false; }
    bool VisitTupleExpression(TupleExpression* node) override { result = target_->VisitTupleExpression(node); return false; }
    bool VisitNamedExpression(NamedExpression* node) override { result = target_->VisitNamedExpression(node); return false; }
    bool VisitNamedArgumentExpression(NamedArgumentExpression* node) override { result = target_->VisitNamedArgumentExpression(node); return false; }
    bool VisitErrorExpression(ErrorExpression* node) override { result = target_->VisitErrorExpression(node); return false; }
    bool VisitOutVarDeclarationExpression(OutVarDeclarationExpression* node) override { result = target_->VisitOutVarDeclarationExpression(node); return false; }
    bool VisitWithInitializerExpression(WithInitializerExpression* node) override { result = target_->VisitWithInitializerExpression(node); return false; }
    bool VisitUndocumentedExpression(UndocumentedExpression* node) override { result = target_->VisitUndocumentedExpression(node); return false; }
    bool VisitStackAllocExpression(StackAllocExpression* node) override { result = target_->VisitStackAllocExpression(node); return false; }
    bool VisitContinueStatement(ContinueStatement* node) override { result = target_->VisitContinueStatement(node); return false; }
    bool VisitBreakStatement(BreakStatement* node) override { result = target_->VisitBreakStatement(node); return false; }
    bool VisitYieldBreakStatement(YieldBreakStatement* node) override { result = target_->VisitYieldBreakStatement(node); return false; }
    bool VisitReturnStatement(ReturnStatement* node) override { result = target_->VisitReturnStatement(node); return false; }
    bool VisitThrowStatement(ThrowStatement* node) override { result = target_->VisitThrowStatement(node); return false; }
    bool VisitExpressionStatement(ExpressionStatement* node) override { result = target_->VisitExpressionStatement(node); return false; }
    bool VisitBlockStatement(BlockStatement* node) override { result = target_->VisitBlockStatement(node); return false; }
    bool VisitGotoStatement(GotoStatement* node) override { result = target_->VisitGotoStatement(node); return false; }
    bool VisitGotoCaseStatement(GotoCaseStatement* node) override { result = target_->VisitGotoCaseStatement(node); return false; }
    bool VisitGotoDefaultStatement(GotoDefaultStatement* node) override { result = target_->VisitGotoDefaultStatement(node); return false; }
    bool VisitIfElseStatement(IfElseStatement* node) override { result = target_->VisitIfElseStatement(node); return false; }
    bool VisitWhileStatement(WhileStatement* node) override { result = target_->VisitWhileStatement(node); return false; }
    bool VisitDoWhileStatement(DoWhileStatement* node) override { result = target_->VisitDoWhileStatement(node); return false; }
    bool VisitYieldReturnStatement(YieldReturnStatement* node) override { result = target_->VisitYieldReturnStatement(node); return false; }
    bool VisitEmptyStatement(EmptyStatement* node) override { result = target_->VisitEmptyStatement(node); return false; }
    bool VisitLabelStatement(LabelStatement* node) override { result = target_->VisitLabelStatement(node); return false; }
    bool VisitCheckedStatement(CheckedStatement* node) override { result = target_->VisitCheckedStatement(node); return false; }
    bool VisitUncheckedStatement(UncheckedStatement* node) override { result = target_->VisitUncheckedStatement(node); return false; }
    bool VisitUnsafeStatement(UnsafeStatement* node) override { result = target_->VisitUnsafeStatement(node); return false; }
    bool VisitLockStatement(LockStatement* node) override { result = target_->VisitLockStatement(node); return false; }
    bool VisitUsingStatement(UsingStatement* node) override { result = target_->VisitUsingStatement(node); return false; }
    bool VisitForStatement(ForStatement* node) override { result = target_->VisitForStatement(node); return false; }
    bool VisitSingleVariableDesignation(SingleVariableDesignation* node) override { result = target_->VisitSingleVariableDesignation(node); return false; }
    bool VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation* node) override { result = target_->VisitParenthesizedVariableDesignation(node); return false; }
    bool VisitForeachStatement(ForeachStatement* node) override { result = target_->VisitForeachStatement(node); return false; }
    bool VisitVariableInitializer(VariableInitializer* node) override { result = target_->VisitVariableInitializer(node); return false; }
    bool VisitFixedStatement(FixedStatement* node) override { result = target_->VisitFixedStatement(node); return false; }
    bool VisitCaseLabel(CaseLabel* node) override { result = target_->VisitCaseLabel(node); return false; }
    bool VisitSwitchSection(SwitchSection* node) override { result = target_->VisitSwitchSection(node); return false; }
    bool VisitSwitchStatement(SwitchStatement* node) override { result = target_->VisitSwitchStatement(node); return false; }
    bool VisitCatchClause(CatchClause* node) override { result = target_->VisitCatchClause(node); return false; }
    bool VisitTryCatchStatement(TryCatchStatement* node) override { result = target_->VisitTryCatchStatement(node); return false; }
    bool VisitVariableDeclarationStatement(VariableDeclarationStatement* node) override { result = target_->VisitVariableDeclarationStatement(node); return false; }
    bool VisitDestructorDeclaration(DestructorDeclaration* node) override { result = target_->VisitDestructorDeclaration(node); return false; }
    bool VisitFieldDeclaration(FieldDeclaration* node) override { result = target_->VisitFieldDeclaration(node); return false; }
    bool VisitAccessor(Accessor* node) override { result = target_->VisitAccessor(node); return false; }
    bool VisitEnumMemberDeclaration(EnumMemberDeclaration* node) override { result = target_->VisitEnumMemberDeclaration(node); return false; }
    bool VisitPropertyDeclaration(PropertyDeclaration* node) override { result = target_->VisitPropertyDeclaration(node); return false; }
    bool VisitEventDeclaration(EventDeclaration* node) override { result = target_->VisitEventDeclaration(node); return false; }
    bool VisitCustomEventDeclaration(CustomEventDeclaration* node) override { result = target_->VisitCustomEventDeclaration(node); return false; }
    bool VisitParameterDeclaration(ParameterDeclaration* node) override { result = target_->VisitParameterDeclaration(node); return false; }
    bool VisitIndexerDeclaration(IndexerDeclaration* node) override { result = target_->VisitIndexerDeclaration(node); return false; }
    bool VisitOperatorDeclaration(OperatorDeclaration* node) override { result = target_->VisitOperatorDeclaration(node); return false; }
    bool VisitConstructorInitializer(ConstructorInitializer* node) override { result = target_->VisitConstructorInitializer(node); return false; }
    bool VisitConstructorDeclaration(ConstructorDeclaration* node) override { result = target_->VisitConstructorDeclaration(node); return false; }
    bool VisitTypeParameterDeclaration(TypeParameterDeclaration* node) override { result = target_->VisitTypeParameterDeclaration(node); return false; }
    bool VisitConstraint(Constraint* node) override { result = target_->VisitConstraint(node); return false; }
    bool VisitMethodDeclaration(MethodDeclaration* node) override { result = target_->VisitMethodDeclaration(node); return false; }
    bool VisitExtensionDeclaration(ExtensionDeclaration* node) override { result = target_->VisitExtensionDeclaration(node); return false; }
    bool VisitFixedVariableInitializer(FixedVariableInitializer* node) override { result = target_->VisitFixedVariableInitializer(node); return false; }
    bool VisitFixedFieldDeclaration(FixedFieldDeclaration* node) override { result = target_->VisitFixedFieldDeclaration(node); return false; }
    bool VisitLocalFunctionDeclarationStatement(LocalFunctionDeclarationStatement* node) override { result = target_->VisitLocalFunctionDeclarationStatement(node); return false; }
    bool VisitComment(Comment* node) override { result = target_->VisitComment(node); return false; }
    bool VisitExternAliasDeclaration(ExternAliasDeclaration* node) override { result = target_->VisitExternAliasDeclaration(node); return false; }
    bool VisitUsingDeclaration(UsingDeclaration* node) override { result = target_->VisitUsingDeclaration(node); return false; }
    bool VisitUsingAliasDeclaration(UsingAliasDeclaration* node) override { result = target_->VisitUsingAliasDeclaration(node); return false; }
    bool VisitTupleTypeElement(TupleTypeElement* node) override { result = target_->VisitTupleTypeElement(node); return false; }
    bool VisitTupleType(TupleAstType* node) override { result = target_->VisitTupleType(node); return false; }
    bool VisitInvocationType(InvocationAstType* node) override { result = target_->VisitInvocationType(node); return false; }
    bool VisitFunctionPointerType(FunctionPointerAstType* node) override { result = target_->VisitFunctionPointerType(node); return false; }
    bool VisitDelegateDeclaration(DelegateDeclaration* node) override { result = target_->VisitDelegateDeclaration(node); return false; }
    bool VisitTypeDeclaration(TypeDeclaration* node) override { result = target_->VisitTypeDeclaration(node); return false; }
    bool VisitNamespaceDeclaration(NamespaceDeclaration* node) override { result = target_->VisitNamespaceDeclaration(node); return false; }
    bool VisitPreProcessorDirective(PreProcessorDirective* node) override { result = target_->VisitPreProcessorDirective(node); return false; }
    bool VisitDocumentationReference(DocumentationReference* node) override { result = target_->VisitDocumentationReference(node); return false; }
    bool VisitDeclarationExpression(DeclarationExpression* node) override { result = target_->VisitDeclarationExpression(node); return false; }
    bool VisitAnonymousTypeCreateExpression(AnonymousTypeCreateExpression* node) override { result = target_->VisitAnonymousTypeCreateExpression(node); return false; }
    bool VisitLambdaExpression(LambdaExpression* node) override { result = target_->VisitLambdaExpression(node); return false; }
    bool VisitAnonymousMethodExpression(AnonymousMethodExpression* node) override { result = target_->VisitAnonymousMethodExpression(node); return false; }
    bool VisitSwitchExpressionSection(SwitchExpressionSection* node) override { result = target_->VisitSwitchExpressionSection(node); return false; }
    bool VisitSwitchExpression(SwitchExpression* node) override { result = target_->VisitSwitchExpression(node); return false; }
    bool VisitRecursivePatternExpression(RecursivePatternExpression* node) override { result = target_->VisitRecursivePatternExpression(node); return false; }
    bool VisitInterpolation(Interpolation* node) override { result = target_->VisitInterpolation(node); return false; }
    bool VisitInterpolatedStringText(InterpolatedStringText* node) override { result = target_->VisitInterpolatedStringText(node); return false; }
    bool VisitInterpolatedStringExpression(InterpolatedStringExpression* node) override { result = target_->VisitInterpolatedStringExpression(node); return false; }
    bool VisitQueryOrdering(QueryOrdering* node) override { result = target_->VisitQueryOrdering(node); return false; }
    bool VisitQueryExpression(QueryExpression* node) override { result = target_->VisitQueryExpression(node); return false; }
    bool VisitQueryWhereClause(QueryWhereClause* node) override { result = target_->VisitQueryWhereClause(node); return false; }
    bool VisitQuerySelectClause(QuerySelectClause* node) override { result = target_->VisitQuerySelectClause(node); return false; }
    bool VisitQueryOrderClause(QueryOrderClause* node) override { result = target_->VisitQueryOrderClause(node); return false; }
    bool VisitQueryLetClause(QueryLetClause* node) override { result = target_->VisitQueryLetClause(node); return false; }
    bool VisitQueryGroupClause(QueryGroupClause* node) override { result = target_->VisitQueryGroupClause(node); return false; }
    bool VisitQueryFromClause(QueryFromClause* node) override { result = target_->VisitQueryFromClause(node); return false; }
    bool VisitQueryContinuationClause(QueryContinuationClause* node) override { result = target_->VisitQueryContinuationClause(node); return false; }
    bool VisitQueryJoinClause(QueryJoinClause* node) override { result = target_->VisitQueryJoinClause(node); return false; }
    bool VisitSyntaxTree(SyntaxTree* node) override { result = target_->VisitSyntaxTree(node); return false; }

    bool VisitPatternPlaceholder(AstNode* placeholder, PatternMatching::Pattern& pattern) override {
        result = target_->VisitPatternPlaceholder(placeholder, pattern);
        return false;
    }

private:
    IAstVisitorAstNode* target_;
};

} // namespace

AstNode* AstNode::AcceptVisitorAstNode(IAstVisitorAstNode& visitor) {
    AstNodeVisitorAstNodeAdapter adapter(visitor);
    AcceptVisitorBool(adapter);
    return adapter.result;
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
