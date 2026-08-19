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

// The out-of-line definitions for `CSharpOutputVisitor` (see the header for the design). This
// translation unit holds the infrastructure methods (the two ctors, the `StartNode`/`EndNode`
// nesting, the `Comma` family, the token/brace writers, the write-construct helpers) and the
// 130 per-node `Visit` methods as THROWING STUBS (each calls `NotImplemented()`); the stubs are
// replaced with real bodies one node-family at a time in subsequent iterations.

#include "Decompiler/CSharp/OutputVisitor/CSharpOutputVisitor.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

// The concrete AST nodes the infrastructure methods dereference (the throwing `Visit` stubs only
// name their parameter types by pointer, so they need only the forward declarations the included
// `IAstVisitor.hpp` already provides; the infrastructure needs the complete types).
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Tokens.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/OutputVisitor/InsertRequiredSpacesDecorator.hpp"

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

using namespace ILSpy::Decompiler::CSharp::Syntax;

// ---- ctors / dtor ---------------------------------------------------------

// The C# `CSharpOutputVisitor(TextWriter textWriter, CSharpFormattingOptions)` -- composes a
// `TokenWriter::Create(ostream, indent)` stack the visitor owns. The C# `ArgumentNullException` on
// a null stream ports to `std::invalid_argument`; the policy is a value type (not nullable), so
// the C# policy null check is dropped (no null value-type exists).
CSharpOutputVisitor::CSharpOutputVisitor(std::ostream* textWriter, CSharpFormattingOptions formattingPolicy)
	: writer_(nullptr), writerOwner_(TokenWriter::Create(textWriter, formattingPolicy.IndentationString)),
	  policy_(formattingPolicy) {
	if (textWriter == nullptr) {
		throw std::invalid_argument("CSharpOutputVisitor: textWriter must not be null");
	}
	writer_ = writerOwner_.get();
}

// The C# `CSharpOutputVisitor(TokenWriter writer, CSharpFormattingOptions)` -- wraps the
// caller-owned `writer` in an `InsertRequiredSpacesDecorator` the visitor owns (the decorator's
// non-owning ctor takes the caller's `TokenWriter*`; the visitor owns the decorator, the caller
// keeps the inner writer alive -- faithful to the C# reference semantics).
CSharpOutputVisitor::CSharpOutputVisitor(TokenWriter* writer, CSharpFormattingOptions formattingPolicy)
	: writer_(nullptr), writerOwner_(std::make_unique<InsertRequiredSpacesDecorator>(writer)),
	  policy_(formattingPolicy) {
	if (writer == nullptr) {
		throw std::invalid_argument("CSharpOutputVisitor: writer must not be null");
	}
	writer_ = writerOwner_.get();
}

// The default dtor releases the owning `writerOwner_` (destroying the whole writer stack); the
// non-owning `writer_` and the value `policy_`/`containerStack_` clean up automatically.
CSharpOutputVisitor::~CSharpOutputVisitor() = default;

// The throwing-stub body shared by every not-yet-ported `Visit` method.
[[noreturn]] void CSharpOutputVisitor::NotImplemented() {
	throw std::logic_error("CSharpOutputVisitor: Visit method not yet implemented");
}

// ---- StartNode/EndNode ----------------------------------------------------

void CSharpOutputVisitor::StartNode(AstNode* node) {
	// The C# `Debug.Assert` on the nesting order (with the `IPatternPlaceholder` branch dropped --
	// the pattern-matching DSL is deferred per D226). A debug-only invariant.
	assert(containerStack_.empty() || node->Parent() == containerStack_.top());
	containerStack_.push(node);
	writer_->StartNode(node);
	for (Trivia* trivia : node->LeadingTrivia()) {
		trivia->AcceptVisitor(*this);
	}
}

void CSharpOutputVisitor::EndNode(AstNode* node) {
	for (Trivia* trivia : node->TrailingTrivia()) {
		trivia->AcceptVisitor(*this);
	}
	assert(node == containerStack_.top());
	containerStack_.pop();
	writer_->EndNode(node);
}

// ---- Comma ----------------------------------------------------------------

void CSharpOutputVisitor::Comma(AstNode* nextNode, bool noSpaceAfterComma) {
	(void)nextNode;  // the C# TODO comma policy does not yet use the next node
	Space(policy_.SpaceBeforeBracketComma);
	writer_->WriteToken(",");
	isAfterSpace_ = false;
	Space(!noSpaceAfterComma && policy_.SpaceAfterBracketComma);
}

void CSharpOutputVisitor::WriteCommaSeparatedListInBrackets(const std::vector<ParameterDeclaration*>& list, bool spaceWithin) {
	WriteToken(Tokens::LBracket);
	if (!list.empty()) {
		Space(spaceWithin);
		WriteCommaSeparatedList(list);
		Space(spaceWithin);
	}
	WriteToken(Tokens::RBracket);
}

void CSharpOutputVisitor::WriteCommaSeparatedListInBrackets(const std::vector<Expression*>& list) {
	WriteToken(Tokens::LBracket);
	if (!list.empty()) {
		Space(policy_.SpacesWithinBrackets);
		WriteCommaSeparatedList(list);
		Space(policy_.SpacesWithinBrackets);
	}
	WriteToken(Tokens::RBracket);
}

// ---- Write tokens ---------------------------------------------------------

void CSharpOutputVisitor::WriteKeyword(std::string_view keyword) {
	writer_->WriteKeyword(keyword);
	isAtStartOfLine_ = false;
	isAfterSpace_ = false;
}

void CSharpOutputVisitor::WriteIdentifier(Identifier* identifier) {
	writer_->WriteIdentifier(identifier);
	isAtStartOfLine_ = false;
	isAfterSpace_ = false;
}

void CSharpOutputVisitor::WriteToken(std::string_view token) {
	writer_->WriteToken(token);
	isAtStartOfLine_ = false;
	isAfterSpace_ = false;
}

void CSharpOutputVisitor::LPar() {
	WriteToken(Tokens::LPar);
}

void CSharpOutputVisitor::RPar() {
	WriteToken(Tokens::RPar);
}

void CSharpOutputVisitor::Semicolon() {
	// Get the slot kind of the current node (the top of the nesting stack).
	const CSharpSlotInfo* kind = nullptr;
	if (!containerStack_.empty()) {
		const CSharpSlotInfo* slot = containerStack_.top()->Slot();
		if (slot != nullptr) {
			kind = slot->Kind();
		}
	}

	auto skipToken = [](const CSharpSlotInfo* k) {
		return k == &Slots::ForInitializer
			|| k == &Slots::Iterator
			|| k == &Slots::ResourceAcquisition;
	};

	auto skipNewLine = [&](const CSharpSlotInfo* k) -> bool {
		Accessor* accessor = dynamic_cast<Accessor*>(containerStack_.top());
		if (accessor == nullptr) {
			return false;
		}
		if (!(k == &Slots::Getter || k == &Slots::Setter)) {
			return false;
		}
		bool isAutoProperty = accessor->Body() == nullptr
			&& accessor->Attributes().Count() == 0
			&& policy_.AutoPropertyFormatting == PropertyFormatting::SingleLine;
		return isAutoProperty;
	};

	if (!skipToken(kind)) {
		WriteToken(Tokens::Semicolon);
		if (!skipNewLine(kind)) {
			NewLine();
		} else {
			Space();
		}
	}
}

void CSharpOutputVisitor::Space(bool addSpace) {
	if (addSpace && !isAfterSpace_) {
		writer_->Space();
		isAfterSpace_ = true;
	}
}

void CSharpOutputVisitor::NewLine() {
	writer_->NewLine();
	isAtStartOfLine_ = true;
	isAfterSpace_ = false;
}

void CSharpOutputVisitor::OpenBrace(BraceStyle style, bool newLine) {
	switch (style) {
		case BraceStyle::EndOfLine:
		case BraceStyle::BannerStyle:
			if (!isAtStartOfLine_) {
				Space();
			}
			WriteToken("{");
			break;
		case BraceStyle::EndOfLineWithoutSpace:
			WriteToken("{");
			break;
		case BraceStyle::NextLine:
			if (!isAtStartOfLine_) {
				NewLine();
			}
			WriteToken("{");
			break;
		case BraceStyle::NextLineShifted:
			NewLine();
			writer_->Indent();
			WriteToken("{");
			NewLine();
			return;
		case BraceStyle::NextLineShifted2:
			NewLine();
			writer_->Indent();
			WriteToken("{");
			break;
		default:
			throw std::out_of_range("CSharpOutputVisitor::OpenBrace: invalid BraceStyle");
	}
	if (newLine) {
		writer_->Indent();
		NewLine();
	}
}

void CSharpOutputVisitor::CloseBrace(BraceStyle style, bool unindent) {
	switch (style) {
		case BraceStyle::EndOfLine:
		case BraceStyle::EndOfLineWithoutSpace:
		case BraceStyle::NextLine:
			if (unindent) {
				writer_->Unindent();
			}
			WriteToken("}");
			break;
		case BraceStyle::BannerStyle:
		case BraceStyle::NextLineShifted:
			WriteToken("}");
			if (unindent) {
				writer_->Unindent();
			}
			break;
		case BraceStyle::NextLineShifted2:
			if (unindent) {
				writer_->Unindent();
			}
			WriteToken("}");
			if (unindent) {
				writer_->Unindent();
			}
			break;
		default:
			throw std::out_of_range("CSharpOutputVisitor::CloseBrace: invalid BraceStyle");
	}
}

// ---- Write constructs -----------------------------------------------------

void CSharpOutputVisitor::WriteBlock(BlockStatement* blockStatement, BraceStyle style) {
	StartNode(blockStatement);
	OpenBrace(style);
	auto& stmts = blockStatement->Statements();
	int n = stmts.Count();
	for (int i = 0; i < n; ++i) {
		stmts.At(i)->AcceptVisitor(*this);
	}
	CloseBrace(style);
	EndNode(blockStatement);
}

void CSharpOutputVisitor::WriteTypeArguments(const std::vector<AstType*>& typeArguments) {
	if (!typeArguments.empty()) {
		WriteToken(Tokens::LChevron);
		WriteCommaSeparatedList(typeArguments);
		WriteToken(Tokens::RChevron);
	}
}

void CSharpOutputVisitor::WriteTypeParameters(const std::vector<TypeParameterDeclaration*>& typeParameters) {
	if (!typeParameters.empty()) {
		WriteToken(Tokens::LChevron);
		WriteCommaSeparatedList(typeParameters);
		WriteToken(Tokens::RChevron);
	}
}

void CSharpOutputVisitor::WriteModifiers(Modifiers modifiers) {
	for (Modifiers modifier : CSharpModifiers::AllModifiers) {
		if ((modifiers & modifier) == modifier) {
			WriteKeyword(CSharpModifiers::GetModifierName(modifier));
			Space();
		}
	}
}

void CSharpOutputVisitor::WriteQualifiedIdentifier(const std::vector<Identifier*>& identifiers) {
	bool first = true;
	for (Identifier* ident : identifiers) {
		if (first) {
			first = false;
		} else {
			writer_->WriteToken(".");
		}
		writer_->WriteIdentifier(ident);
	}
}

void CSharpOutputVisitor::WriteEmbeddedStatement(Statement* embeddedStatement, NewLinePlacement nlp) {
	if (embeddedStatement == nullptr) {
		NewLine();
		return;
	}
	BlockStatement* block = dynamic_cast<BlockStatement*>(embeddedStatement);
	if (block != nullptr) {
		WriteBlock(block, policy_.StatementBraceStyle);
		if (nlp == NewLinePlacement::SameLine) {
			Space();
		} else {
			NewLine();
		}
	} else {
		NewLine();
		writer_->Indent();
		embeddedStatement->AcceptVisitor(*this);
		writer_->Unindent();
	}
}

void CSharpOutputVisitor::WriteMethodBody(BlockStatement* body, BraceStyle style, bool newLine) {
	(void)newLine;  // the C# leftover param (unused in the body)
	if (body == nullptr) {
		Semicolon();
	} else {
		WriteBlock(body, style);
		NewLine();
	}
}

void CSharpOutputVisitor::WriteAttributes(const std::vector<AttributeSection*>& attributes) {
	for (AttributeSection* attr : attributes) {
		attr->AcceptVisitor(*this);
	}
}

void CSharpOutputVisitor::WritePrivateImplementationType(AstType* privateImplementationType) {
	if (privateImplementationType != nullptr) {
		privateImplementationType->AcceptVisitor(*this);
		WriteToken(Tokens::Dot);
	}
}

// ---- The 130 IAstVisitor Visit methods (throwing stubs) -------------------
void CSharpOutputVisitor::VisitIdentifier(Syntax::Identifier*) { NotImplemented(); }
void CSharpOutputVisitor::VisitNullReferenceExpression(Syntax::NullReferenceExpression* nullReferenceExpression) {
	// The C# `writer.WritePrimitiveValue(null)` -- the default-constructed `PrimitiveValue` holds
	// `std::monostate` (the C# `null` boxed object), rendered by the writer as the `null` literal.
	StartNode(nullReferenceExpression);
	writer_->WritePrimitiveValue(PrimitiveValue());
	isAfterSpace_ = false;
	EndNode(nullReferenceExpression);
}

void CSharpOutputVisitor::VisitThisReferenceExpression(Syntax::ThisReferenceExpression* thisReferenceExpression) {
	StartNode(thisReferenceExpression);
	WriteKeyword("this");
	EndNode(thisReferenceExpression);
}

void CSharpOutputVisitor::VisitBaseReferenceExpression(Syntax::BaseReferenceExpression* baseReferenceExpression) {
	StartNode(baseReferenceExpression);
	WriteKeyword("base");
	EndNode(baseReferenceExpression);
}

void CSharpOutputVisitor::VisitPrimitiveExpression(Syntax::PrimitiveExpression* primitiveExpression) {
	StartNode(primitiveExpression);
	writer_->WritePrimitiveValue(primitiveExpression->Value(), primitiveExpression->Format());
	isAfterSpace_ = false;
	EndNode(primitiveExpression);
}
void CSharpOutputVisitor::VisitBinaryOperatorExpression(Syntax::BinaryOperatorExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAssignmentExpression(Syntax::AssignmentExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitConditionalExpression(Syntax::ConditionalExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitParenthesizedExpression(Syntax::ParenthesizedExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCheckedExpression(Syntax::CheckedExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUncheckedExpression(Syntax::UncheckedExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDirectionExpression(Syntax::DirectionExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitThrowExpression(Syntax::ThrowExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitPrimitiveType(Syntax::PrimitiveType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSimpleType(Syntax::SimpleType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitMemberType(Syntax::MemberType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitArraySpecifier(Syntax::ArraySpecifier*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAttribute(Syntax::Attribute*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAttributeSection(Syntax::AttributeSection*) { NotImplemented(); }
void CSharpOutputVisitor::VisitComposedType(Syntax::ComposedType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCastExpression(Syntax::CastExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAsExpression(Syntax::AsExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitIsExpression(Syntax::IsExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTypeReferenceExpression(Syntax::TypeReferenceExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTypeOfExpression(Syntax::TypeOfExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDefaultValueExpression(Syntax::DefaultValueExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSizeOfExpression(Syntax::SizeOfExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitIdentifierExpression(Syntax::IdentifierExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitMemberReferenceExpression(Syntax::MemberReferenceExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitPointerReferenceExpression(Syntax::PointerReferenceExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitInvocationExpression(Syntax::InvocationExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitIndexerExpression(Syntax::IndexerExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitArrayInitializerExpression(Syntax::ArrayInitializerExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitObjectCreateExpression(Syntax::ObjectCreateExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitArrayCreateExpression(Syntax::ArrayCreateExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTupleExpression(Syntax::TupleExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitNamedExpression(Syntax::NamedExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitNamedArgumentExpression(Syntax::NamedArgumentExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitErrorExpression(Syntax::ErrorExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitOutVarDeclarationExpression(Syntax::OutVarDeclarationExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitWithInitializerExpression(Syntax::WithInitializerExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUndocumentedExpression(Syntax::UndocumentedExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitStackAllocExpression(Syntax::StackAllocExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitContinueStatement(Syntax::ContinueStatement* continueStatement) {
	StartNode(continueStatement);
	WriteKeyword("continue");
	Semicolon();
	EndNode(continueStatement);
}

void CSharpOutputVisitor::VisitBreakStatement(Syntax::BreakStatement* breakStatement) {
	StartNode(breakStatement);
	WriteKeyword("break");
	Semicolon();
	EndNode(breakStatement);
}

void CSharpOutputVisitor::VisitYieldBreakStatement(Syntax::YieldBreakStatement* yieldBreakStatement) {
	StartNode(yieldBreakStatement);
	WriteKeyword(YieldBreakStatement::YieldKeyword);
	WriteKeyword(YieldBreakStatement::BreakKeyword);
	Semicolon();
	EndNode(yieldBreakStatement);
}
void CSharpOutputVisitor::VisitReturnStatement(Syntax::ReturnStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitThrowStatement(Syntax::ThrowStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitExpressionStatement(Syntax::ExpressionStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitBlockStatement(Syntax::BlockStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitGotoStatement(Syntax::GotoStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitGotoCaseStatement(Syntax::GotoCaseStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitGotoDefaultStatement(Syntax::GotoDefaultStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitIfElseStatement(Syntax::IfElseStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitWhileStatement(Syntax::WhileStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDoWhileStatement(Syntax::DoWhileStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitYieldReturnStatement(Syntax::YieldReturnStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitEmptyStatement(Syntax::EmptyStatement* emptyStatement) {
	// An empty statement that carries a comment renders as just that comment (emitted as trivia by
	// `StartNode`/`EndNode`); otherwise it is a bare semicolon.
	StartNode(emptyStatement);
	if (emptyStatement->LeadingTrivia().empty() && emptyStatement->TrailingTrivia().empty()) {
		Semicolon();
	}
	EndNode(emptyStatement);
}
void CSharpOutputVisitor::VisitLabelStatement(Syntax::LabelStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCheckedStatement(Syntax::CheckedStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUncheckedStatement(Syntax::UncheckedStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUnsafeStatement(Syntax::UnsafeStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitLockStatement(Syntax::LockStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUsingStatement(Syntax::UsingStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitForStatement(Syntax::ForStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSingleVariableDesignation(Syntax::SingleVariableDesignation*) { NotImplemented(); }
void CSharpOutputVisitor::VisitParenthesizedVariableDesignation(Syntax::ParenthesizedVariableDesignation*) { NotImplemented(); }
void CSharpOutputVisitor::VisitForeachStatement(Syntax::ForeachStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitVariableInitializer(Syntax::VariableInitializer*) { NotImplemented(); }
void CSharpOutputVisitor::VisitFixedStatement(Syntax::FixedStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCaseLabel(Syntax::CaseLabel*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSwitchSection(Syntax::SwitchSection*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSwitchStatement(Syntax::SwitchStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCatchClause(Syntax::CatchClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTryCatchStatement(Syntax::TryCatchStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitVariableDeclarationStatement(Syntax::VariableDeclarationStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDestructorDeclaration(Syntax::DestructorDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitFieldDeclaration(Syntax::FieldDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAccessor(Syntax::Accessor*) { NotImplemented(); }
void CSharpOutputVisitor::VisitEnumMemberDeclaration(Syntax::EnumMemberDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitPropertyDeclaration(Syntax::PropertyDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitEventDeclaration(Syntax::EventDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCustomEventDeclaration(Syntax::CustomEventDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitParameterDeclaration(Syntax::ParameterDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitIndexerDeclaration(Syntax::IndexerDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitOperatorDeclaration(Syntax::OperatorDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitConstructorInitializer(Syntax::ConstructorInitializer*) { NotImplemented(); }
void CSharpOutputVisitor::VisitConstructorDeclaration(Syntax::ConstructorDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTypeParameterDeclaration(Syntax::TypeParameterDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitConstraint(Syntax::Constraint*) { NotImplemented(); }
void CSharpOutputVisitor::VisitMethodDeclaration(Syntax::MethodDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitExtensionDeclaration(Syntax::ExtensionDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitFixedVariableInitializer(Syntax::FixedVariableInitializer*) { NotImplemented(); }
void CSharpOutputVisitor::VisitFixedFieldDeclaration(Syntax::FixedFieldDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitLocalFunctionDeclarationStatement(Syntax::LocalFunctionDeclarationStatement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitComment(Syntax::Comment*) { NotImplemented(); }
void CSharpOutputVisitor::VisitExternAliasDeclaration(Syntax::ExternAliasDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUsingDeclaration(Syntax::UsingDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitUsingAliasDeclaration(Syntax::UsingAliasDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTupleTypeElement(Syntax::TupleTypeElement*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTupleType(Syntax::TupleAstType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitInvocationType(Syntax::InvocationAstType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitFunctionPointerType(Syntax::FunctionPointerAstType*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDelegateDeclaration(Syntax::DelegateDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTypeDeclaration(Syntax::TypeDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitNamespaceDeclaration(Syntax::NamespaceDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitPreProcessorDirective(Syntax::PreProcessorDirective*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDocumentationReference(Syntax::DocumentationReference*) { NotImplemented(); }
void CSharpOutputVisitor::VisitDeclarationExpression(Syntax::DeclarationExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAnonymousTypeCreateExpression(Syntax::AnonymousTypeCreateExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitLambdaExpression(Syntax::LambdaExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAnonymousMethodExpression(Syntax::AnonymousMethodExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSwitchExpressionSection(Syntax::SwitchExpressionSection*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSwitchExpression(Syntax::SwitchExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitRecursivePatternExpression(Syntax::RecursivePatternExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitInterpolation(Syntax::Interpolation*) { NotImplemented(); }
void CSharpOutputVisitor::VisitInterpolatedStringText(Syntax::InterpolatedStringText*) { NotImplemented(); }
void CSharpOutputVisitor::VisitInterpolatedStringExpression(Syntax::InterpolatedStringExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryOrdering(Syntax::QueryOrdering*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryExpression(Syntax::QueryExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryWhereClause(Syntax::QueryWhereClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQuerySelectClause(Syntax::QuerySelectClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryOrderClause(Syntax::QueryOrderClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryLetClause(Syntax::QueryLetClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryGroupClause(Syntax::QueryGroupClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryFromClause(Syntax::QueryFromClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryContinuationClause(Syntax::QueryContinuationClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitQueryJoinClause(Syntax::QueryJoinClause*) { NotImplemented(); }
void CSharpOutputVisitor::VisitSyntaxTree(Syntax::SyntaxTree*) { NotImplemented(); }

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor
