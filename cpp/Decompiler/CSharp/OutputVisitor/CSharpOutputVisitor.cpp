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
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UncheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/CSharp/OutputVisitor/InsertRequiredSpacesDecorator.hpp"

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

using namespace ILSpy::Decompiler::CSharp::Syntax;

namespace {

// Builds an eager `std::vector<T*>` snapshot of an `AstNodeCollectionT<T>` -- the C# passes a
// lazy `IEnumerable<T>` to the write helpers; the port's helpers take an eager vector (the
// established D221 tree-walk convention), so each `Visit` method that calls `WriteTypeArguments`/
// `WriteCommaSeparatedList` builds its snapshot once (the collection is not mutated during the
// output walk, so the snapshot is faithful to the lazy view).
template <typename T>
std::vector<T*> ToVector(const AstNodeCollectionT<T>& collection) {
	std::vector<T*> result;
	int n = collection.Count();
	result.reserve(n);
	for (int i = 0; i < n; ++i) {
		result.push_back(collection.At(i));
	}
	return result;
}

}  // namespace

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
void CSharpOutputVisitor::VisitIdentifier(Syntax::Identifier* identifier) {
	// The C# deliberately does NOT call `StartNode`/`EndNode` for `Identifier` -- the
	// `ITokenWriter` assumes each node processed between a `StartNode`/`EndNode` pair is a child
	// of the parent node, and an `Identifier` is a token handled directly by the writer, so it
	// is emitted as a flat token (not a nested node).
	WriteIdentifier(identifier);
}
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
void CSharpOutputVisitor::VisitBinaryOperatorExpression(Syntax::BinaryOperatorExpression* binaryOperatorExpression) {
	StartNode(binaryOperatorExpression);
	if (binaryOperatorExpression->Left() != nullptr)
		binaryOperatorExpression->Left()->AcceptVisitor(*this);
	bool spacePolicy;
	switch (binaryOperatorExpression->Operator()) {
		case Syntax::BinaryOperatorType::BitwiseAnd:
		case Syntax::BinaryOperatorType::BitwiseOr:
		case Syntax::BinaryOperatorType::ExclusiveOr:
			spacePolicy = policy_.SpaceAroundBitwiseOperator;
			break;
		case Syntax::BinaryOperatorType::ConditionalAnd:
		case Syntax::BinaryOperatorType::ConditionalOr:
			spacePolicy = policy_.SpaceAroundLogicalOperator;
			break;
		case Syntax::BinaryOperatorType::GreaterThan:
		case Syntax::BinaryOperatorType::GreaterThanOrEqual:
		case Syntax::BinaryOperatorType::LessThanOrEqual:
		case Syntax::BinaryOperatorType::LessThan:
			spacePolicy = policy_.SpaceAroundRelationalOperator;
			break;
		case Syntax::BinaryOperatorType::Equality:
		case Syntax::BinaryOperatorType::InEquality:
			spacePolicy = policy_.SpaceAroundEqualityOperator;
			break;
		case Syntax::BinaryOperatorType::Add:
		case Syntax::BinaryOperatorType::Subtract:
			spacePolicy = policy_.SpaceAroundAdditiveOperator;
			break;
		case Syntax::BinaryOperatorType::Multiply:
		case Syntax::BinaryOperatorType::Divide:
		case Syntax::BinaryOperatorType::Modulus:
			spacePolicy = policy_.SpaceAroundMultiplicativeOperator;
			break;
		case Syntax::BinaryOperatorType::ShiftLeft:
		case Syntax::BinaryOperatorType::ShiftRight:
		case Syntax::BinaryOperatorType::UnsignedShiftRight:
			spacePolicy = policy_.SpaceAroundShiftOperator;
			break;
		case Syntax::BinaryOperatorType::NullCoalescing:
		case Syntax::BinaryOperatorType::IsPattern:
			spacePolicy = true;
			break;
		case Syntax::BinaryOperatorType::Range:
			spacePolicy = false;
			break;
		default:
			throw std::out_of_range("Invalid value for BinaryOperatorType");
	}
	Space(spacePolicy);
	const char* operatorToken = Syntax::BinaryOperatorExpression::GetOperatorToken(binaryOperatorExpression->Operator());
	if (operatorToken == Syntax::BinaryOperatorExpression::IsKeyword) {
		WriteKeyword(operatorToken);
	} else {
		WriteToken(operatorToken);
	}
	Space(spacePolicy);
	if (binaryOperatorExpression->Right() != nullptr)
		binaryOperatorExpression->Right()->AcceptVisitor(*this);
	EndNode(binaryOperatorExpression);
}

void CSharpOutputVisitor::VisitAssignmentExpression(Syntax::AssignmentExpression* assignmentExpression) {
	StartNode(assignmentExpression);
	assignmentExpression->Left()->AcceptVisitor(*this);
	Space(policy_.SpaceAroundAssignment);
	WriteToken(Syntax::AssignmentExpression::GetOperatorToken(assignmentExpression->Operator()));
	Space(policy_.SpaceAroundAssignment);
	assignmentExpression->Right()->AcceptVisitor(*this);
	EndNode(assignmentExpression);
}

void CSharpOutputVisitor::VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression* unaryOperatorExpression) {
	StartNode(unaryOperatorExpression);
	Syntax::UnaryOperatorType opType = unaryOperatorExpression->Operator();
	auto opSymbol = Syntax::UnaryOperatorExpression::GetOperatorToken(opType);
	if (opType == Syntax::UnaryOperatorType::Await || opType == Syntax::UnaryOperatorType::PatternNot) {
		WriteKeyword(*opSymbol);
		Space();
	} else if (!Syntax::UnaryOperatorExpression::IsPostfixOperator(opType) && opSymbol.has_value()) {
		WriteToken(*opSymbol);
	}
	unaryOperatorExpression->Expression()->AcceptVisitor(*this);
	if (Syntax::UnaryOperatorExpression::IsPostfixOperator(opType)) {
		WriteToken(*opSymbol);
	}
	EndNode(unaryOperatorExpression);
}

void CSharpOutputVisitor::VisitConditionalExpression(Syntax::ConditionalExpression* conditionalExpression) {
	StartNode(conditionalExpression);
	conditionalExpression->Condition()->AcceptVisitor(*this);
	Space(policy_.SpaceBeforeConditionalOperatorCondition);
	WriteToken(Syntax::ConditionalExpression::QuestionMarkToken);
	Space(policy_.SpaceAfterConditionalOperatorCondition);
	conditionalExpression->TrueExpression()->AcceptVisitor(*this);
	Space(policy_.SpaceBeforeConditionalOperatorSeparator);
	WriteToken(Syntax::ConditionalExpression::ColonToken);
	Space(policy_.SpaceAfterConditionalOperatorSeparator);
	conditionalExpression->FalseExpression()->AcceptVisitor(*this);
	EndNode(conditionalExpression);
}

void CSharpOutputVisitor::VisitParenthesizedExpression(Syntax::ParenthesizedExpression* parenthesizedExpression) {
	StartNode(parenthesizedExpression);
	LPar();
	Space(policy_.SpacesWithinParentheses);
	parenthesizedExpression->Expression()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinParentheses);
	RPar();
	EndNode(parenthesizedExpression);
}

void CSharpOutputVisitor::VisitCheckedExpression(Syntax::CheckedExpression* checkedExpression) {
	StartNode(checkedExpression);
	WriteKeyword(Syntax::CheckedExpression::CheckedKeyword);
	LPar();
	Space(policy_.SpacesWithinCheckedExpressionParantheses);
	checkedExpression->Expression()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinCheckedExpressionParantheses);
	RPar();
	EndNode(checkedExpression);
}

void CSharpOutputVisitor::VisitUncheckedExpression(Syntax::UncheckedExpression* uncheckedExpression) {
	StartNode(uncheckedExpression);
	WriteKeyword(Syntax::UncheckedExpression::UncheckedKeyword);
	LPar();
	Space(policy_.SpacesWithinCheckedExpressionParantheses);
	uncheckedExpression->Expression()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinCheckedExpressionParantheses);
	RPar();
	EndNode(uncheckedExpression);
}

void CSharpOutputVisitor::VisitDirectionExpression(Syntax::DirectionExpression* directionExpression) {
	StartNode(directionExpression);
	switch (directionExpression->FieldDirection()) {
		case Syntax::FieldDirection::Out:
			WriteKeyword(Syntax::DirectionExpression::OutKeyword);
			break;
		case Syntax::FieldDirection::Ref:
			WriteKeyword(Syntax::DirectionExpression::RefKeyword);
			break;
		case Syntax::FieldDirection::In:
			WriteKeyword(Syntax::DirectionExpression::InKeyword);
			break;
		default:
			throw std::out_of_range("Invalid value for FieldDirection");
	}
	Space();
	directionExpression->Expression()->AcceptVisitor(*this);
	EndNode(directionExpression);
}

void CSharpOutputVisitor::VisitThrowExpression(Syntax::ThrowExpression* throwExpression) {
	StartNode(throwExpression);
	WriteKeyword(Syntax::ThrowExpression::ThrowKeyword);
	Space();
	throwExpression->Expression()->AcceptVisitor(*this);
	EndNode(throwExpression);
}
void CSharpOutputVisitor::VisitPrimitiveType(Syntax::PrimitiveType* primitiveType) {
	StartNode(primitiveType);
	writer_->WritePrimitiveType(primitiveType->Keyword());
	isAfterSpace_ = false;
	EndNode(primitiveType);
}
void CSharpOutputVisitor::VisitSimpleType(Syntax::SimpleType* simpleType) {
	StartNode(simpleType);
	// An unbound generic type argument (the `<>` in `typeof(List<>)`) is a nameless `SimpleType`
	// whose backing `IdentifierToken` is null; only a named `SimpleType` writes its identifier.
	if (simpleType->IdentifierToken() != nullptr) {
		WriteIdentifier(simpleType->IdentifierToken());
	}
	WriteTypeArguments(ToVector(simpleType->TypeArguments()));
	EndNode(simpleType);
}
void CSharpOutputVisitor::VisitMemberType(Syntax::MemberType* memberType) {
	StartNode(memberType);
	memberType->Target()->AcceptVisitor(*this);
	WriteToken(memberType->IsDoubleColon() ? Tokens::DoubleColon : Tokens::Dot);
	WriteIdentifier(memberType->MemberNameToken());
	WriteTypeArguments(ToVector(memberType->TypeArguments()));
	EndNode(memberType);
}
void CSharpOutputVisitor::VisitArraySpecifier(Syntax::ArraySpecifier* arraySpecifier) {
	StartNode(arraySpecifier);
	WriteToken(Tokens::LBracket);
	// A rank of N renders as `[` + (N-1) commas + `]` (e.g. rank 1 -> `[]`, rank 2 -> `[,]`).
	for (int i = 0; i < arraySpecifier->Dimensions() - 1; ++i) {
		writer_->WriteToken(",");
	}
	WriteToken(Tokens::RBracket);
	EndNode(arraySpecifier);
}
void CSharpOutputVisitor::VisitAttribute(Syntax::Attribute*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAttributeSection(Syntax::AttributeSection*) { NotImplemented(); }
void CSharpOutputVisitor::VisitComposedType(Syntax::ComposedType* composedType) {
	StartNode(composedType);
	// The optional leading attributes (a `[Flags]`-style attribute on the type usage).
	auto& attrs = composedType->Attributes();
	int attrCount = attrs.Count();
	for (int i = 0; i < attrCount; ++i) {
		attrs.At(i)->AcceptVisitor(*this);
	}
	if (composedType->HasRefSpecifier()) {
		WriteKeyword(ComposedType::RefKeyword);
	}
	if (composedType->HasReadOnlySpecifier()) {
		WriteKeyword(ComposedType::ReadonlyKeyword);
	}
	composedType->BaseType()->AcceptVisitor(*this);
	if (composedType->HasNullableSpecifier()) {
		WriteToken(ComposedType::NullableToken);
	}
	for (int i = 0; i < composedType->PointerRank(); ++i) {
		WriteToken(ComposedType::PointerToken);
	}
	auto& arraySpecs = composedType->ArraySpecifiers();
	int specCount = arraySpecs.Count();
	for (int i = 0; i < specCount; ++i) {
		arraySpecs.At(i)->AcceptVisitor(*this);
	}
	EndNode(composedType);
}
void CSharpOutputVisitor::VisitCastExpression(Syntax::CastExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAsExpression(Syntax::AsExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitIsExpression(Syntax::IsExpression*) { NotImplemented(); }
void CSharpOutputVisitor::VisitTypeReferenceExpression(Syntax::TypeReferenceExpression* typeReferenceExpression) {
	StartNode(typeReferenceExpression);
	typeReferenceExpression->Type()->AcceptVisitor(*this);
	EndNode(typeReferenceExpression);
}
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
void CSharpOutputVisitor::VisitReturnStatement(Syntax::ReturnStatement* returnStatement) {
	StartNode(returnStatement);
	WriteKeyword(ReturnStatement::ReturnKeyword);
	if (returnStatement->Expression() != nullptr) {
		Space();
		returnStatement->Expression()->AcceptVisitor(*this);
	}
	Semicolon();
	EndNode(returnStatement);
}
void CSharpOutputVisitor::VisitThrowStatement(Syntax::ThrowStatement* throwStatement) {
	StartNode(throwStatement);
	WriteKeyword(ThrowStatement::ThrowKeyword);
	if (throwStatement->Expression() != nullptr) {
		Space();
		throwStatement->Expression()->AcceptVisitor(*this);
	}
	Semicolon();
	EndNode(throwStatement);
}
void CSharpOutputVisitor::VisitExpressionStatement(Syntax::ExpressionStatement* expressionStatement) {
	StartNode(expressionStatement);
	expressionStatement->Expression()->AcceptVisitor(*this);
	Semicolon();
	EndNode(expressionStatement);
}
void CSharpOutputVisitor::VisitBlockStatement(Syntax::BlockStatement* blockStatement) {
	WriteBlock(blockStatement, policy_.StatementBraceStyle);
	NewLine();
}
void CSharpOutputVisitor::VisitGotoStatement(Syntax::GotoStatement* gotoStatement) {
	StartNode(gotoStatement);
	WriteKeyword(GotoStatement::GotoKeyword);
	WriteIdentifier(gotoStatement->LabelToken());
	Semicolon();
	EndNode(gotoStatement);
}
void CSharpOutputVisitor::VisitGotoCaseStatement(Syntax::GotoCaseStatement* gotoCaseStatement) {
	StartNode(gotoCaseStatement);
	WriteKeyword(GotoCaseStatement::GotoKeyword);
	WriteKeyword(GotoCaseStatement::CaseKeyword);
	Space();
	gotoCaseStatement->LabelExpression()->AcceptVisitor(*this);
	Semicolon();
	EndNode(gotoCaseStatement);
}
void CSharpOutputVisitor::VisitGotoDefaultStatement(Syntax::GotoDefaultStatement* gotoDefaultStatement) {
	StartNode(gotoDefaultStatement);
	WriteKeyword(GotoDefaultStatement::GotoKeyword);
	WriteKeyword(GotoDefaultStatement::DefaultKeyword);
	Semicolon();
	EndNode(gotoDefaultStatement);
}
void CSharpOutputVisitor::VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) {
	StartNode(ifElseStatement);
	WriteKeyword(IfElseStatement::IfKeyword);
	Space(policy_.SpaceBeforeIfParentheses);
	LPar();
	Space(policy_.SpacesWithinIfParentheses);
	ifElseStatement->Condition()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinIfParentheses);
	RPar();

	if (ifElseStatement->FalseStatement() == nullptr) {
		WriteEmbeddedStatement(ifElseStatement->TrueStatement());
	} else {
		WriteEmbeddedStatement(ifElseStatement->TrueStatement(), policy_.ElseNewLinePlacement);
		WriteKeyword(IfElseStatement::ElseKeyword);
		// A nested `else if` is a single `IfElseStatement` as the `FalseStatement`: recurse
		// directly so `else` and `if` stay on one line (no newline between them).
		if (dynamic_cast<IfElseStatement*>(ifElseStatement->FalseStatement()) != nullptr) {
			ifElseStatement->FalseStatement()->AcceptVisitor(*this);
		} else {
			WriteEmbeddedStatement(ifElseStatement->FalseStatement());
		}
	}
	EndNode(ifElseStatement);
}
void CSharpOutputVisitor::VisitWhileStatement(Syntax::WhileStatement* whileStatement) {
	StartNode(whileStatement);
	WriteKeyword(WhileStatement::WhileKeyword);
	Space(policy_.SpaceBeforeWhileParentheses);
	LPar();
	Space(policy_.SpacesWithinWhileParentheses);
	whileStatement->Condition()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinWhileParentheses);
	RPar();
	WriteEmbeddedStatement(whileStatement->EmbeddedStatement());
	EndNode(whileStatement);
}
void CSharpOutputVisitor::VisitDoWhileStatement(Syntax::DoWhileStatement* doWhileStatement) {
	StartNode(doWhileStatement);
	WriteKeyword(DoWhileStatement::DoKeyword);
	WriteEmbeddedStatement(doWhileStatement->EmbeddedStatement(), policy_.WhileNewLinePlacement);
	WriteKeyword(DoWhileStatement::WhileKeyword);
	Space(policy_.SpaceBeforeWhileParentheses);
	LPar();
	Space(policy_.SpacesWithinWhileParentheses);
	doWhileStatement->Condition()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinWhileParentheses);
	RPar();
	Semicolon();
	EndNode(doWhileStatement);
}
void CSharpOutputVisitor::VisitYieldReturnStatement(Syntax::YieldReturnStatement* yieldReturnStatement) {
	StartNode(yieldReturnStatement);
	WriteKeyword(YieldReturnStatement::YieldKeyword);
	WriteKeyword(YieldReturnStatement::ReturnKeyword);
	Space();
	yieldReturnStatement->Expression()->AcceptVisitor(*this);
	Semicolon();
	EndNode(yieldReturnStatement);
}
void CSharpOutputVisitor::VisitEmptyStatement(Syntax::EmptyStatement* emptyStatement) {
	// An empty statement that carries a comment renders as just that comment (emitted as trivia by
	// `StartNode`/`EndNode`); otherwise it is a bare semicolon.
	StartNode(emptyStatement);
	if (emptyStatement->LeadingTrivia().empty() && emptyStatement->TrailingTrivia().empty()) {
		Semicolon();
	}
	EndNode(emptyStatement);
}
void CSharpOutputVisitor::VisitLabelStatement(Syntax::LabelStatement* labelStatement) {
	StartNode(labelStatement);
	WriteIdentifier(labelStatement->LabelToken());
	WriteToken(Tokens::Colon);
	// A label must be followed by a statement. If the label is the last sibling in its block
	// (no following sibling sharing its slot kind), emit a bare semicolon so the output stays
	// syntactically valid (the C# loop walks `NextSibling` looking for a following labelled
	// statement with the same slot kind).
	const CSharpSlotInfo* labelSlot = labelStatement->Slot();
	const CSharpSlotInfo* labelKind = (labelSlot != nullptr) ? labelSlot->Kind() : nullptr;
	bool foundLabelledStatement = false;
	for (AstNode* tmp = labelStatement->NextSibling(); tmp != nullptr; tmp = tmp->NextSibling()) {
		const CSharpSlotInfo* tmpSlot = tmp->Slot();
		const CSharpSlotInfo* tmpKind = (tmpSlot != nullptr) ? tmpSlot->Kind() : nullptr;
		if (tmpKind == labelKind) {
			foundLabelledStatement = true;
			break;
		}
	}
	if (!foundLabelledStatement) {
		WriteToken(Tokens::Semicolon);
	}
	NewLine();
	EndNode(labelStatement);
}
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
void CSharpOutputVisitor::VisitComment(Syntax::Comment* comment) {
	// A `Comment` is trivia -- the C# drives the writer DIRECTLY (`writer.StartNode`/`writer.EndNode`,
	// NOT the visitor's `StartNode`/`EndNode`): trivia is not part of the node-nesting stack and
	// must not trigger the visitor's leading/trailing-trivia walk (a comment has no slots), so it
	// is emitted as a flat token group by the writer.
	writer_->StartNode(comment);
	writer_->WriteComment(comment->CommentType(), comment->Content());
	writer_->EndNode(comment);
}
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
void CSharpOutputVisitor::VisitPreProcessorDirective(Syntax::PreProcessorDirective* preProcessorDirective) {
	// A `PreProcessorDirective` is trivia -- like `VisitComment`, it drives the writer DIRECTLY
	// (`writer.StartNode`/`writer.EndNode`, not the visitor's `StartNode`/`EndNode`), emitting the
	// directive type and its optional argument as a flat token group by the writer.
	writer_->StartNode(preProcessorDirective);
	writer_->WritePreProcessorDirective(preProcessorDirective->Type(), preProcessorDirective->Argument());
	writer_->EndNode(preProcessorDirective);
}
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
