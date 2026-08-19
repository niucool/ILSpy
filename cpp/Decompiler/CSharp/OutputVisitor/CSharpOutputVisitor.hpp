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

// Port of the `CSharpOutputVisitor` skeleton in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/CSharpOutputVisitor.cs -- the pretty-printer that
// renders a C# AST tree to text by driving a `TokenWriter` through a stream of
// `StartNode`/`WriteKeyword`/`WriteToken`/`WriteIdentifier`/`Space`/`NewLine`/`EndNode` calls.
// The class implements `IAstVisitor` (one `Visit` method per concrete AST node, dispatched from
// `AstNode::AcceptVisitor`); each `Visit` method wraps its output in a `StartNode`/`EndNode`
// pair and recurses into children via `child->AcceptVisitor(*this)`.
//
// This header ports the INFRASTRUCTURE SKELETON (the first in-order slice of the output visitor,
// the D322 next-in-order piece): the two ctors, the `writer`/`policy`/`containerStack` fields
// and the `isAtStartOfLine`/`isAfterSpace` state, the `StartNode`/`EndNode` nesting, the `Comma`
// family, the `WriteKeyword`/`WriteIdentifier`/`WriteToken`/`LPar`/`RPar`/`Semicolon`/`Space`/
// `NewLine` token writers, the `OpenBrace`/`CloseBrace` brace writers, the `WriteBlock` block
// writer, and the `WriteTypeArguments`/`WriteTypeParameters`/`WriteModifiers`/
// `WriteQualifiedIdentifier`/`WriteEmbeddedStatement`/`WriteMethodBody`/`WriteAttributes`/
// `WritePrivateImplementationType` write-construct helpers -- the machinery every per-node
// `Visit` method builds on. The 130 per-node `Visit` methods are declared (so the class is a
// concrete, instantiable `IAstVisitor`) but defined as THROWING STUBS in
// `CSharpOutputVisitor.cpp` (`NotImplemented()` -> `std::logic_error`); they are implemented one
// node-family at a time in subsequent iterations, replacing the throwing stubs with real bodies
// (the incremental port-the-output strategy from PORT_PLAN.md section 5.2 / decision D1).
//
// C#-to-C++ porting decisions:
//  * `public class CSharpOutputVisitor : IAstVisitor` -> a C++ class deriving publicly from
//    `Syntax::IAstVisitor`. The 130 pure-virtual `Visit` methods are overridden (here declared,
//    in the .cpp defined as throwing stubs).
//  * `readonly protected TokenWriter writer` -> a non-owning `TokenWriter* writer_` pointing at
//    the top of the writer stack, plus a private `std::unique_ptr<TokenWriter> writerOwner_`
//    owning the stack (the two ctors compose a stack and store it in `writerOwner_`; `writer_`
//    points at the top). The C# relies on the GC to own the stack; the C++ port owns it via the
//    `unique_ptr` (the D322 ownership design applied to the visitor's own writer stack).
//  * `readonly protected CSharpFormattingOptions policy` -> a by-value `CSharpFormattingOptions
//    policy_` member (the data class is cheaply copyable -- the D317 `Clone()`-via-copy-ctor
//    precedent -- so the visitor keeps its own copy, avoiding the lifetime coupling of a
//    reference). `readonly protected Stack<AstNode> containerStack` ->
//    `std::stack<AstNode*> containerStack_` (the node-nesting stack for `StartNode`/`EndNode`).
//  * The first ctor's `TextWriter` -> `std::ostream*` (the port's `TextWriter` equivalent); it
//    composes a `TokenWriter::Create(ostream, indent)` stack. The second ctor takes a
//    caller-owned `TokenWriter*` and wraps it in an `InsertRequiredSpacesDecorator` the visitor
//    owns (the C# `this.writer = new InsertRequiredSpacesDecorator(writer)`; the caller keeps
//    the inner writer alive, faithful to the C# reference semantics).
//  * `string` keyword/token parameters -> `std::string_view` (the established read-only-string
//    convention; binds directly to the `Tokens::LPar` `inline constexpr const char*` literals
//    without an allocation, the D323 `Tokens` precedent).
//  * `IEnumerable<AstNode>`/`IEnumerable<AstType>`/... collection parameters -> the typed
//    `std::vector<T*>` the caller builds from the `AstNodeCollection` snapshot (the C# lazy
//    `IEnumerable` ports to an eager vector, the established D221 tree-walk convention). The
//    element `AcceptVisitor(*this)` calls recurse; the `WriteCommaSeparatedList`/`InParenthesis`
//    helpers are templates (defined inline here) so they accept any element type that upcasts to
//    `AstNode` and dispatches via its virtual `AcceptVisitor`.
//
// Deferred (land with the first `Visit` method that needs them):
//  * `WriteIdentifier(string identifier)` -- it calls the D236-deferred `AstType.Create`, so the
//    string overload lands when `AstType.Create` does.
//  * `GetCallChainLengthLimited`/`ShouldInsertNewLineWhenInMethodCallChain`/
//    `InsertNewLineWhenInMethodCallChain` -- the method-call-chain new-line helpers consumed
//    only by `VisitMemberReferenceExpression`; they land with that `Visit` method.
//  * The `IsKeyword` static helper is already factored out to the free `IsKeyword` helper in
//    `CSharpKeywordCheck.hpp` (the D318 factoring), so it is NOT a member here.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPOUTPUTVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPOUTPUTVISITOR_HPP

#include <memory>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"  // policy_, BraceStyle, NewLinePlacement, PropertyFormatting
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"               // writer_ (+ brings AstNode complete)
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"                       // base + the 130 forward declarations
#include "Decompiler/CSharp/Syntax/AstType.hpp"                          // AstType (the infra write-construct helpers name it by pointer)
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"               // Statement (WriteEmbeddedStatement)
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"                         // Modifiers, CSharpModifiers

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

// The abstract AST bases the infrastructure signatures name by pointer (`AstType`/`Statement`/
// `Expression`) are made complete via the includes above (`AstType.hpp`/`Statement.hpp`, and
// `Expression` via `TokenWriter.hpp` -> `PrimitiveExpression.hpp`). The concrete element types
// (`BlockStatement`/`TypeParameterDeclaration`/`ParameterDeclaration`/`AttributeSection`/
// `Identifier`) are forward-declared by `IAstVisitor.hpp`.

// The `CSharpOutputVisitor` -- the C# pretty-printer. Derives from `Syntax::IAstVisitor` (the
// 130 per-node `Visit` methods are overridden; the unported ones throw `std::logic_error`).
class CSharpOutputVisitor : public Syntax::IAstVisitor {
public:
	// The C# `CSharpOutputVisitor(TextWriter textWriter, CSharpFormattingOptions)` -- composes
	// a `TokenWriter::Create(ostream, indent)` stack the visitor owns, and stores a copy of the
	// formatting policy. The C# `ArgumentNullException` on a null stream/policy ports to
	// `std::invalid_argument` (the D316 precedent).
	CSharpOutputVisitor(std::ostream* textWriter, CSharpFormattingOptions formattingPolicy);

	// The C# `CSharpOutputVisitor(TokenWriter writer, CSharpFormattingOptions)` -- wraps the
	// caller-owned `writer` in an `InsertRequiredSpacesDecorator` the visitor owns. The caller
	// keeps the inner writer alive (faithful to the C# reference semantics).
	CSharpOutputVisitor(TokenWriter* writer, CSharpFormattingOptions formattingPolicy);

	~CSharpOutputVisitor() override;

	// ---- StartNode/EndNode ------------------------------------------------
	// The C# `protected virtual void StartNode(AstNode node)` / `EndNode(AstNode node)` --
	// bracket a node's output (push/pop the nesting stack, drive the writer, walk the node's
	// leading/trailing trivia). Made `protected` (subclasses and the per-node `Visit` methods
	// call them) and `virtual` (the C# `virtual`).
	virtual void StartNode(Syntax::AstNode* node);
	virtual void EndNode(Syntax::AstNode* node);

	// ---- Comma ------------------------------------------------------------
	virtual void Comma(Syntax::AstNode* nextNode, bool noSpaceAfterComma = false);

	// The C# `WriteCommaSeparatedList(IEnumerable<AstNode> list)` -- a template over the element
	// type (the eager `std::vector<T*>` the caller builds from the collection snapshot); each
	// element recurses via `node->AcceptVisitor(*this)` (a comma separates all but the first).
	template <typename T>
	void WriteCommaSeparatedList(const std::vector<T*>& list) {
		bool isFirst = true;
		for (T* node : list) {
			if (isFirst) {
				isFirst = false;
			} else {
				Comma(node);
			}
			node->AcceptVisitor(*this);
		}
	}

	// The C# `WriteCommaSeparatedListInParenthesis(IEnumerable<AstNode> list, bool spaceWithin)`
	// -- wraps the comma list in parentheses (with optional inner spaces when non-empty).
	template <typename T>
	void WriteCommaSeparatedListInParenthesis(const std::vector<T*>& list, bool spaceWithin) {
		LPar();
		if (!list.empty()) {
			Space(spaceWithin);
			WriteCommaSeparatedList(list);
			Space(spaceWithin);
		}
		RPar();
	}

	// The C# `WriteCommaSeparatedListInBrackets(IEnumerable<ParameterDeclaration> list, bool
	// spaceWithin)` and `(...IEnumerable<Expression> list)` -- two distinct overloads (the C#
	// distinguishes them by element type; the C++ port keeps them as distinct typed-vector
	// overloads). Defined out-of-line in the .cpp.
	virtual void WriteCommaSeparatedListInBrackets(const std::vector<Syntax::ParameterDeclaration*>& list, bool spaceWithin);
	virtual void WriteCommaSeparatedListInBrackets(const std::vector<Syntax::Expression*>& list);

	// ---- Write tokens -----------------------------------------------------
	// The C# `protected bool isAtStartOfLine = true` / `bool isAfterSpace` -- the inter-token
	// whitespace state. In-class default initializers reproduce the C# field initializers
	// (`isAtStartOfLine = true`, `isAfterSpace` defaults `false`).
protected:
	bool isAtStartOfLine_ = true;
	bool isAfterSpace_ = false;

public:
	virtual void WriteKeyword(std::string_view keyword);
	virtual void WriteIdentifier(Syntax::Identifier* identifier);
	virtual void WriteToken(std::string_view token);
	virtual void LPar();
	virtual void RPar();

	// The C# `protected virtual void Semicolon()` -- marks the end of a statement; honours the
	// `ForInitializer`/`Iterator`/`ResourceAcquisition` slot kinds (no semicolon) and the
	// auto-property accessor formatting (no trailing newline). Reads the current node's slot
	// kind from the top of the nesting stack.
	virtual void Semicolon();

	virtual void Space(bool addSpace = true);
	virtual void NewLine();

	virtual void OpenBrace(BraceStyle style, bool newLine = true);
	virtual void CloseBrace(BraceStyle style, bool unindent = true);

	// ---- Write constructs -------------------------------------------------
	// The C# `WriteBlock(BlockStatement, BraceStyle)` -- writes a block's braces with the
	// statements recursed between them.
	virtual void WriteBlock(Syntax::BlockStatement* blockStatement, BraceStyle style);

	virtual void WriteTypeArguments(const std::vector<Syntax::AstType*>& typeArguments);
	virtual void WriteTypeParameters(const std::vector<Syntax::TypeParameterDeclaration*>& typeParameters);
	virtual void WriteModifiers(Syntax::Modifiers modifiers);
	virtual void WriteQualifiedIdentifier(const std::vector<Syntax::Identifier*>& identifiers);
	virtual void WriteEmbeddedStatement(Syntax::Statement* embeddedStatement,
		NewLinePlacement nlp = NewLinePlacement::NewLine);
	virtual void WriteMethodBody(Syntax::BlockStatement* body, BraceStyle style, bool newLine = true);
	virtual void WriteAttributes(const std::vector<Syntax::AttributeSection*>& attributes);
	virtual void WritePrivateImplementationType(Syntax::AstType* privateImplementationType);

	// ---- Initializer helpers -----------------------------------------------
	// The C# `protected virtual void PrintInitializerElements(AstNodeCollection<Expression>)`/
	// `protected bool IsObjectOrCollectionInitializer(AstNode?)`/`protected bool
	// CanBeConfusedWithObjectInitializer(Expression)` -- consulted by
	// `VisitArrayInitializerExpression` (and `VisitAnonymousTypeCreateExpression` when it lands).
	// `new List<int> { { 1 } }` and `new List<int> { 1 }` are the same semantically; the AST always
	// uses two nested `ArrayInitializerExpression`s for collection initializers, and the output
	// visitor omits the nested braces when they are optional (a single non-assignment element whose
	// enclosing initializer is an object/collection initializer slot). The eager-vector convention
	// (the D221/D325 precedent) makes `PrintInitializerElements` take a `std::vector<Expression*>`
	// snapshot the caller builds via `ToVector`.
	virtual void PrintInitializerElements(const std::vector<Syntax::Expression*>& elements);
	bool IsObjectOrCollectionInitializer(Syntax::AstNode* node);
	bool CanBeConfusedWithObjectInitializer(Syntax::Expression* expr);

	// ---- Method-call-chain newline helpers --------------------------------
	// The C# `GetCallChainLengthLimited`/`ShouldInsertNewLineWhenInMethodCallChain` (private)
	// and `InsertNewLineWhenInMethodCallChain` (protected virtual) -- consulted by
	// `VisitMemberReferenceExpression`/`VisitInvocationExpression` to break long
	// `a.B().C().D()` chains across lines. The chain length is the count of
	// `InvocationExpression`-over-`MemberReferenceExpression` links above the given member
	// reference; a chain of length >= 3 in a statement/lambda context (NOT an interpolated
	// string) inserts a `NewLine` (and an `Indent` at exactly 3) before the dot, with a
	// matching `Unindent` after the closing token.
protected:
	int GetCallChainLengthLimited(Syntax::MemberReferenceExpression* expr);
	int ShouldInsertNewLineWhenInMethodCallChain(Syntax::MemberReferenceExpression* expr);
	virtual bool InsertNewLineWhenInMethodCallChain(Syntax::MemberReferenceExpression* expr);

	// The C# `protected bool LambdaNeedsParenthesis(LambdaExpression)` -- consulted by
	// `VisitLambdaExpression` to decide whether the parameter list needs parentheses. A lambda
	// with exactly one parameter that has no type, no modifier, and no `params` may omit the
	// parentheses (`x => ...`); every other shape (zero, two+, or a typed/modified/params single)
	// needs them. The `Parameters.Single()` ports to `Parameters().At(0)` (the single element).
	bool LambdaNeedsParenthesis(Syntax::LambdaExpression* lambdaExpression);
public:
	// ---- The 130 IAstVisitor Visit methods --------------------------------
	// Declared here (override the IAstVisitor pure-virtuals) so the class is a concrete,
	// instantiable visitor; the unported ones are defined as throwing stubs in the .cpp and
	// replaced with real bodies one node-family at a time.
	void VisitIdentifier(Syntax::Identifier*) override;
	void VisitNullReferenceExpression(Syntax::NullReferenceExpression*) override;
	void VisitThisReferenceExpression(Syntax::ThisReferenceExpression*) override;
	void VisitBaseReferenceExpression(Syntax::BaseReferenceExpression*) override;
	void VisitPrimitiveExpression(Syntax::PrimitiveExpression*) override;
	void VisitBinaryOperatorExpression(Syntax::BinaryOperatorExpression*) override;
	void VisitAssignmentExpression(Syntax::AssignmentExpression*) override;
	void VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression*) override;
	void VisitConditionalExpression(Syntax::ConditionalExpression*) override;
	void VisitParenthesizedExpression(Syntax::ParenthesizedExpression*) override;
	void VisitCheckedExpression(Syntax::CheckedExpression*) override;
	void VisitUncheckedExpression(Syntax::UncheckedExpression*) override;
	void VisitDirectionExpression(Syntax::DirectionExpression*) override;
	void VisitThrowExpression(Syntax::ThrowExpression*) override;
	void VisitPrimitiveType(Syntax::PrimitiveType*) override;
	void VisitSimpleType(Syntax::SimpleType*) override;
	void VisitMemberType(Syntax::MemberType*) override;
	void VisitArraySpecifier(Syntax::ArraySpecifier*) override;
	void VisitAttribute(Syntax::Attribute*) override;
	void VisitAttributeSection(Syntax::AttributeSection*) override;
	void VisitComposedType(Syntax::ComposedType*) override;
	void VisitCastExpression(Syntax::CastExpression*) override;
	void VisitAsExpression(Syntax::AsExpression*) override;
	void VisitIsExpression(Syntax::IsExpression*) override;
	void VisitTypeReferenceExpression(Syntax::TypeReferenceExpression*) override;
	void VisitTypeOfExpression(Syntax::TypeOfExpression*) override;
	void VisitDefaultValueExpression(Syntax::DefaultValueExpression*) override;
	void VisitSizeOfExpression(Syntax::SizeOfExpression*) override;
	void VisitIdentifierExpression(Syntax::IdentifierExpression*) override;
	void VisitMemberReferenceExpression(Syntax::MemberReferenceExpression*) override;
	void VisitPointerReferenceExpression(Syntax::PointerReferenceExpression*) override;
	void VisitInvocationExpression(Syntax::InvocationExpression*) override;
	void VisitIndexerExpression(Syntax::IndexerExpression*) override;
	void VisitArrayInitializerExpression(Syntax::ArrayInitializerExpression*) override;
	void VisitObjectCreateExpression(Syntax::ObjectCreateExpression*) override;
	void VisitArrayCreateExpression(Syntax::ArrayCreateExpression*) override;
	void VisitTupleExpression(Syntax::TupleExpression*) override;
	void VisitNamedExpression(Syntax::NamedExpression*) override;
	void VisitNamedArgumentExpression(Syntax::NamedArgumentExpression*) override;
	void VisitErrorExpression(Syntax::ErrorExpression*) override;
	void VisitOutVarDeclarationExpression(Syntax::OutVarDeclarationExpression*) override;
	void VisitWithInitializerExpression(Syntax::WithInitializerExpression*) override;
	void VisitUndocumentedExpression(Syntax::UndocumentedExpression*) override;
	void VisitStackAllocExpression(Syntax::StackAllocExpression*) override;
	void VisitContinueStatement(Syntax::ContinueStatement*) override;
	void VisitBreakStatement(Syntax::BreakStatement*) override;
	void VisitYieldBreakStatement(Syntax::YieldBreakStatement*) override;
	void VisitReturnStatement(Syntax::ReturnStatement*) override;
	void VisitThrowStatement(Syntax::ThrowStatement*) override;
	void VisitExpressionStatement(Syntax::ExpressionStatement*) override;
	void VisitBlockStatement(Syntax::BlockStatement*) override;
	void VisitGotoStatement(Syntax::GotoStatement*) override;
	void VisitGotoCaseStatement(Syntax::GotoCaseStatement*) override;
	void VisitGotoDefaultStatement(Syntax::GotoDefaultStatement*) override;
	void VisitIfElseStatement(Syntax::IfElseStatement*) override;
	void VisitWhileStatement(Syntax::WhileStatement*) override;
	void VisitDoWhileStatement(Syntax::DoWhileStatement*) override;
	void VisitYieldReturnStatement(Syntax::YieldReturnStatement*) override;
	void VisitEmptyStatement(Syntax::EmptyStatement*) override;
	void VisitLabelStatement(Syntax::LabelStatement*) override;
	void VisitCheckedStatement(Syntax::CheckedStatement*) override;
	void VisitUncheckedStatement(Syntax::UncheckedStatement*) override;
	void VisitUnsafeStatement(Syntax::UnsafeStatement*) override;
	void VisitLockStatement(Syntax::LockStatement*) override;
	void VisitUsingStatement(Syntax::UsingStatement*) override;
	void VisitForStatement(Syntax::ForStatement*) override;
	void VisitSingleVariableDesignation(Syntax::SingleVariableDesignation*) override;
	void VisitParenthesizedVariableDesignation(Syntax::ParenthesizedVariableDesignation*) override;
	void VisitForeachStatement(Syntax::ForeachStatement*) override;
	void VisitVariableInitializer(Syntax::VariableInitializer*) override;
	void VisitFixedStatement(Syntax::FixedStatement*) override;
	void VisitCaseLabel(Syntax::CaseLabel*) override;
	void VisitSwitchSection(Syntax::SwitchSection*) override;
	void VisitSwitchStatement(Syntax::SwitchStatement*) override;
	void VisitCatchClause(Syntax::CatchClause*) override;
	void VisitTryCatchStatement(Syntax::TryCatchStatement*) override;
	void VisitVariableDeclarationStatement(Syntax::VariableDeclarationStatement*) override;
	void VisitDestructorDeclaration(Syntax::DestructorDeclaration*) override;
	void VisitFieldDeclaration(Syntax::FieldDeclaration*) override;
	void VisitAccessor(Syntax::Accessor*) override;
	void VisitEnumMemberDeclaration(Syntax::EnumMemberDeclaration*) override;
	void VisitPropertyDeclaration(Syntax::PropertyDeclaration*) override;
	void VisitEventDeclaration(Syntax::EventDeclaration*) override;
	void VisitCustomEventDeclaration(Syntax::CustomEventDeclaration*) override;
	void VisitParameterDeclaration(Syntax::ParameterDeclaration*) override;
	void VisitIndexerDeclaration(Syntax::IndexerDeclaration*) override;
	void VisitOperatorDeclaration(Syntax::OperatorDeclaration*) override;
	void VisitConstructorInitializer(Syntax::ConstructorInitializer*) override;
	void VisitConstructorDeclaration(Syntax::ConstructorDeclaration*) override;
	void VisitTypeParameterDeclaration(Syntax::TypeParameterDeclaration*) override;
	void VisitConstraint(Syntax::Constraint*) override;
	void VisitMethodDeclaration(Syntax::MethodDeclaration*) override;
	void VisitExtensionDeclaration(Syntax::ExtensionDeclaration*) override;
	void VisitFixedVariableInitializer(Syntax::FixedVariableInitializer*) override;
	void VisitFixedFieldDeclaration(Syntax::FixedFieldDeclaration*) override;
	void VisitLocalFunctionDeclarationStatement(Syntax::LocalFunctionDeclarationStatement*) override;
	void VisitComment(Syntax::Comment*) override;
	void VisitExternAliasDeclaration(Syntax::ExternAliasDeclaration*) override;
	void VisitUsingDeclaration(Syntax::UsingDeclaration*) override;
	void VisitUsingAliasDeclaration(Syntax::UsingAliasDeclaration*) override;
	void VisitTupleTypeElement(Syntax::TupleTypeElement*) override;
	void VisitTupleType(Syntax::TupleAstType*) override;
	void VisitInvocationType(Syntax::InvocationAstType*) override;
	void VisitFunctionPointerType(Syntax::FunctionPointerAstType*) override;
	void VisitDelegateDeclaration(Syntax::DelegateDeclaration*) override;
	void VisitTypeDeclaration(Syntax::TypeDeclaration*) override;
	void VisitNamespaceDeclaration(Syntax::NamespaceDeclaration*) override;
	void VisitPreProcessorDirective(Syntax::PreProcessorDirective*) override;
	void VisitDocumentationReference(Syntax::DocumentationReference*) override;
	void VisitDeclarationExpression(Syntax::DeclarationExpression*) override;
	void VisitAnonymousTypeCreateExpression(Syntax::AnonymousTypeCreateExpression*) override;
	void VisitLambdaExpression(Syntax::LambdaExpression*) override;
	void VisitAnonymousMethodExpression(Syntax::AnonymousMethodExpression*) override;
	void VisitSwitchExpressionSection(Syntax::SwitchExpressionSection*) override;
	void VisitSwitchExpression(Syntax::SwitchExpression*) override;
	void VisitRecursivePatternExpression(Syntax::RecursivePatternExpression*) override;
	void VisitInterpolation(Syntax::Interpolation*) override;
	void VisitInterpolatedStringText(Syntax::InterpolatedStringText*) override;
	void VisitInterpolatedStringExpression(Syntax::InterpolatedStringExpression*) override;
	void VisitQueryOrdering(Syntax::QueryOrdering*) override;
	void VisitQueryExpression(Syntax::QueryExpression*) override;
	void VisitQueryWhereClause(Syntax::QueryWhereClause*) override;
	void VisitQuerySelectClause(Syntax::QuerySelectClause*) override;
	void VisitQueryOrderClause(Syntax::QueryOrderClause*) override;
	void VisitQueryLetClause(Syntax::QueryLetClause*) override;
	void VisitQueryGroupClause(Syntax::QueryGroupClause*) override;
	void VisitQueryFromClause(Syntax::QueryFromClause*) override;
	void VisitQueryContinuationClause(Syntax::QueryContinuationClause*) override;
	void VisitQueryJoinClause(Syntax::QueryJoinClause*) override;
	void VisitSyntaxTree(Syntax::SyntaxTree*) override;
private:
	// The throwing stub body shared by every not-yet-ported `Visit` method -- a clear, loud
	// "not implemented" that makes the unported state explicit (distinct from a no-op, which
	// would silently produce no output and could mask a forgotten node).
	[[noreturn]] static void NotImplemented();

	// ---- Fields ------------------------------------------------------------
	// The C# `readonly protected TokenWriter writer` -- the top of the writer stack the visitor
	// drives (non-owning; points into `writerOwner_`'s stack). Protected so a subclass and the
	// per-node `Visit` methods can drive it.
	TokenWriter* writer_;
	// The owning handle to the composed writer stack (the two ctors build a stack here; `writer_`
	// points at the top). The C# relies on the GC; the C++ port owns the stack via this
	// `unique_ptr` (the D322 ownership design). Declared before `writer_` is unused here (the
	// non-owning pointer is set in the ctor initializer list).
	std::unique_ptr<TokenWriter> writerOwner_;
	// The C# `readonly protected CSharpFormattingOptions policy` -- a by-value copy of the
	// formatting settings (cheaply copyable, the D317 precedent).
protected:
	CSharpFormattingOptions policy_;
	// The C# `readonly protected Stack<AstNode> containerStack` -- the node-nesting stack for
	// `StartNode`/`EndNode` (the current node chain).
	std::stack<Syntax::AstNode*> containerStack_;
};

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor

#endif // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_CSHARPOUTPUTVISITOR_HPP
