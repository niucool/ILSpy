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
#include <vector>
#include <algorithm>

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
#include "Decompiler/CSharp/Syntax/Statements/CheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UncheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UnsafeStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/TupleTypeElement.hpp"
#include "Decompiler/CSharp/Syntax/TupleAstType.hpp"
#include "Decompiler/CSharp/Syntax/InvocationAstType.hpp"
#include "Decompiler/CSharp/Syntax/FunctionPointerAstType.hpp"
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
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TupleExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/WithInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SwitchExpression.hpp"
#include "Decompiler/CSharp/Syntax/SwitchExpressionSection.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/ParenthesizedVariableDesignation.hpp"
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

// The C# local functions `IsSimpleExpression`/`IsComplexExpression` nested in
// `PrintInitializerElements` (the array-initializer wrapping classifier). They do not access
// `this` or the policy (pure type tests on the element), so they port as file-local free
// functions in this anonymous namespace. `IsSimpleExpression`'s `MemberReferenceExpression {
// Target: ThisReferenceExpression or IdentifierExpression or BaseReferenceExpression }`
// recursive pattern ports to a `dynamic_cast<MemberReferenceExpression*>` plus a `dynamic_cast`
// is-a test on the `Target` against the three simple-target types.
bool IsSimpleExpression(Expression* ex) {
	if (dynamic_cast<NullReferenceExpression*>(ex) != nullptr)
		return true;
	if (dynamic_cast<ThisReferenceExpression*>(ex) != nullptr)
		return true;
	if (dynamic_cast<PrimitiveExpression*>(ex) != nullptr)
		return true;
	if (dynamic_cast<IdentifierExpression*>(ex) != nullptr)
		return true;
	if (auto* mre = dynamic_cast<MemberReferenceExpression*>(ex)) {
		AstNode* t = mre->Target();
		if (dynamic_cast<ThisReferenceExpression*>(t) != nullptr
			|| dynamic_cast<IdentifierExpression*>(t) != nullptr
			|| dynamic_cast<BaseReferenceExpression*>(t) != nullptr) {
			return true;
		}
	}
	return false;
}

bool IsComplexExpression(Expression* ex) {
	return dynamic_cast<AnonymousMethodExpression*>(ex) != nullptr
		|| dynamic_cast<LambdaExpression*>(ex) != nullptr
		|| dynamic_cast<AnonymousTypeCreateExpression*>(ex) != nullptr
		|| dynamic_cast<ObjectCreateExpression*>(ex) != nullptr
		|| dynamic_cast<NamedExpression*>(ex) != nullptr;
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

// ---- Initializer helpers --------------------------------------------------
// The C# `protected virtual void PrintInitializerElements(AstNodeCollection<Expression>)` --
// renders the braces around a comma-separated element list, wrapping to one-element-per-line
// when `ArrayInitializerWrapping == WrapAlways`, when any non-simple expression is present with
// more than one element, when any complex expression is present, or when there are more than 10
// elements. The eager-vector convention (the D221/D325 precedent) makes the port take a
// `std::vector<Expression*>` snapshot; the `foreach (var (idx, node) in elements.WithIndex())`
// ports to an indexed loop (the vector provides the index natively). The two local classifier
// functions are file-local free functions in the anonymous namespace above.
void CSharpOutputVisitor::PrintInitializerElements(const std::vector<Expression*>& elements) {
	bool wrapAlways = policy_.ArrayInitializerWrapping == Wrapping::WrapAlways
		|| (static_cast<int>(elements.size()) > 1
			&& std::any_of(elements.begin(), elements.end(), [](Expression* e) { return !IsSimpleExpression(e); }))
		|| std::any_of(elements.begin(), elements.end(), IsComplexExpression);
	bool wrap = wrapAlways || static_cast<int>(elements.size()) > 10;
	OpenBrace(wrap ? policy_.ArrayInitializerBraceStyle : BraceStyle::EndOfLine, wrap);
	if (!wrap)
		Space();
	for (std::size_t idx = 0; idx < elements.size(); ++idx) {
		if (idx > 0) {
			Comma(elements[idx], true);
			if (wrapAlways || idx % 10 == 0)
				NewLine();
			else
				Space();
		}
		elements[idx]->AcceptVisitor(*this);
	}
	if (wrap)
		NewLine();
	else
		Space();
	CloseBrace(wrap ? policy_.ArrayInitializerBraceStyle : BraceStyle::EndOfLine, wrap);
}

// The C# `protected bool IsObjectOrCollectionInitializer(AstNode?)` -- the `node` argument is the
// PARENT of the `ArrayInitializerExpression` being visited; it returns true when that parent is
// itself an `ArrayInitializerExpression` (the outer braces of `new T { { ... } }` / `name = { ... }`)
// whose own parent is an `ObjectCreateExpression` (the outer braces occupy the `Initializer` slot)
// or a `NamedExpression` (the outer braces occupy the `Expression` slot). The `node.Slot?.Kind ==
// Slots.X` null-propagating pointer compare ports to a null-safe `Slot()` + `Kind()` pointer compare
// (a parented node's `Slot()` is non-null; `Kind()` returns the shared `Slots` constant address).
bool CSharpOutputVisitor::IsObjectOrCollectionInitializer(AstNode* node) {
	if (dynamic_cast<ArrayInitializerExpression*>(node) == nullptr)
		return false;
	AstNode* parent = node->Parent();
	if (dynamic_cast<ObjectCreateExpression*>(parent) != nullptr) {
		const CSharpSlotInfo* slot = node->Slot();
		return slot != nullptr && slot->Kind() == &Slots::Initializer;
	}
	if (dynamic_cast<NamedExpression*>(parent) != nullptr) {
		const CSharpSlotInfo* slot = node->Slot();
		return slot != nullptr && slot->Kind() == &Slots::Expression;
	}
	return false;
}

// The C# `protected bool CanBeConfusedWithObjectInitializer(Expression)` -- an `AssignmentExpression`
// with the `Assign` operator inside a collection initializer would read as an object initializer
// (`{ a = 1 }`), so the nested braces cannot be omitted for it (the port keeps them).
bool CSharpOutputVisitor::CanBeConfusedWithObjectInitializer(Expression* expr) {
	auto* ae = dynamic_cast<AssignmentExpression*>(expr);
	return ae != nullptr && ae->Operator() == AssignmentOperatorType::Assign;
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

// ---- Method-call-chain newline helpers ------------------------------------

// The C# `int GetCallChainLengthLimited(MemberReferenceExpression expr)` -- walks the
// `Target` chain counting `InvocationExpression`-over-`MemberReferenceExpression` links, capped
// at 4 (the `callChainLength < 4` guard). A standalone `a.B` (the `Target` is an
// `IdentifierExpression`, not an `InvocationExpression`) yields 0.
int CSharpOutputVisitor::GetCallChainLengthLimited(MemberReferenceExpression* expr) {
	int callChainLength = 0;
	auto* node = expr;
	while (callChainLength < 4) {
		auto* invocation = dynamic_cast<InvocationExpression*>(node->Target());
		if (invocation == nullptr) {
			break;
		}
		auto* mre = dynamic_cast<MemberReferenceExpression*>(invocation->Target());
		if (mre == nullptr) {
			break;
		}
		node = mre;
		++callChainLength;
	}
	return callChainLength;
}

// The C# `int ShouldInsertNewLineWhenInMethodCallChain(MemberReferenceExpression expr)` --
// returns 0 (no break) for chains shorter than 3 or when the nearest
// statement/lambda/interpolated-string ancestor IS an interpolated string (a chain inside an
// interpolated-string hole stays on one line); otherwise returns the chain length.
int CSharpOutputVisitor::ShouldInsertNewLineWhenInMethodCallChain(MemberReferenceExpression* expr) {
	int callChainLength = GetCallChainLengthLimited(expr);
	if (callChainLength < 3) {
		return 0;
	}
	AstNode* ancestor = expr->GetParent([](AstNode* n) {
		return dynamic_cast<Statement*>(n) != nullptr
			|| dynamic_cast<LambdaExpression*>(n) != nullptr
			|| dynamic_cast<InterpolatedStringContent*>(n) != nullptr;
	});
	if (dynamic_cast<InterpolatedStringContent*>(ancestor) != nullptr) {
		return 0;
	}
	return callChainLength;
}

// The C# `protected virtual bool InsertNewLineWhenInMethodCallChain(MemberReferenceExpression
// expr)` -- inserts a `NewLine` (and an `Indent` at exactly chain length 3) before the dot when
// the chain should break; resets the inter-token whitespace state. Returns whether a newline
// was inserted (the caller unindents after the closing token when the member reference is not
// itself the target of an enclosing invocation).
bool CSharpOutputVisitor::InsertNewLineWhenInMethodCallChain(MemberReferenceExpression* expr) {
	int callChainLength = ShouldInsertNewLineWhenInMethodCallChain(expr);
	if (callChainLength == 0) {
		return false;
	}
	if (callChainLength == 3) {
		writer_->Indent();
	}
	writer_->NewLine();
	isAtStartOfLine_ = true;
	isAfterSpace_ = false;
	return true;
}

// The C# `protected bool LambdaNeedsParenthesis(LambdaExpression)` -- a lambda with exactly one
// parameter that has no type, no modifier, and no `params` may omit the parentheses
// (`x => ...`); every other shape needs them. The `Parameters.Single()` ports to `At(0)`.
bool CSharpOutputVisitor::LambdaNeedsParenthesis(Syntax::LambdaExpression* lambdaExpression) {
	if (lambdaExpression->Parameters().Count() != 1) {
		return true;
	}
	Syntax::ParameterDeclaration* p = lambdaExpression->Parameters().At(0);
	return !(p->Type() == nullptr
		&& p->ParameterModifier() == ILSpy::Decompiler::TypeSystem::ReferenceKind::None
		&& !p->IsParams());
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
void CSharpOutputVisitor::VisitCastExpression(Syntax::CastExpression* castExpression) {
	StartNode(castExpression);
	LPar();
	Space(policy_.SpacesWithinCastParentheses);
	castExpression->Type()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinCastParentheses);
	RPar();
	Space(policy_.SpaceAfterTypecast);
	castExpression->Expression()->AcceptVisitor(*this);
	EndNode(castExpression);
}
void CSharpOutputVisitor::VisitAsExpression(Syntax::AsExpression* asExpression) {
	StartNode(asExpression);
	asExpression->Expression()->AcceptVisitor(*this);
	Space();
	WriteKeyword(Syntax::AsExpression::AsKeyword);
	Space();
	asExpression->Type()->AcceptVisitor(*this);
	EndNode(asExpression);
}
void CSharpOutputVisitor::VisitIsExpression(Syntax::IsExpression* isExpression) {
	StartNode(isExpression);
	isExpression->Expression()->AcceptVisitor(*this);
	Space();
	WriteKeyword(Syntax::IsExpression::IsKeyword);
	isExpression->Type()->AcceptVisitor(*this);
	EndNode(isExpression);
}
void CSharpOutputVisitor::VisitTypeReferenceExpression(Syntax::TypeReferenceExpression* typeReferenceExpression) {
	StartNode(typeReferenceExpression);
	typeReferenceExpression->Type()->AcceptVisitor(*this);
	EndNode(typeReferenceExpression);
}
void CSharpOutputVisitor::VisitTypeOfExpression(Syntax::TypeOfExpression* typeOfExpression) {
	StartNode(typeOfExpression);

	WriteKeyword(Syntax::TypeOfExpression::TypeofKeyword);
	LPar();
	Space(policy_.SpacesWithinTypeOfParentheses);
	typeOfExpression->Type()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinTypeOfParentheses);
	RPar();

	EndNode(typeOfExpression);
}
void CSharpOutputVisitor::VisitDefaultValueExpression(Syntax::DefaultValueExpression* defaultValueExpression) {
	StartNode(defaultValueExpression);

	WriteKeyword(Syntax::DefaultValueExpression::DefaultKeyword);
	LPar();
	Space(policy_.SpacesWithinTypeOfParentheses);
	defaultValueExpression->Type()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinTypeOfParentheses);
	RPar();

	EndNode(defaultValueExpression);
}
void CSharpOutputVisitor::VisitSizeOfExpression(Syntax::SizeOfExpression* sizeOfExpression) {
	StartNode(sizeOfExpression);

	WriteKeyword(Syntax::SizeOfExpression::SizeofKeyword);
	LPar();
	Space(policy_.SpacesWithinSizeOfParentheses);
	sizeOfExpression->Type()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinSizeOfParentheses);
	RPar();

	EndNode(sizeOfExpression);
}
void CSharpOutputVisitor::VisitIdentifierExpression(Syntax::IdentifierExpression* identifierExpression) {
	StartNode(identifierExpression);
	WriteIdentifier(identifierExpression->IdentifierToken());
	WriteTypeArguments(ToVector(identifierExpression->TypeArguments()));
	EndNode(identifierExpression);
}
void CSharpOutputVisitor::VisitMemberReferenceExpression(Syntax::MemberReferenceExpression* memberReferenceExpression) {
	StartNode(memberReferenceExpression);
	memberReferenceExpression->Target()->AcceptVisitor(*this);
	bool insertedNewLine = InsertNewLineWhenInMethodCallChain(memberReferenceExpression);
	WriteToken(Tokens::Dot);
	WriteIdentifier(memberReferenceExpression->MemberNameToken());
	WriteTypeArguments(ToVector(memberReferenceExpression->TypeArguments()));
	if (insertedNewLine && dynamic_cast<Syntax::InvocationExpression*>(memberReferenceExpression->Parent()) == nullptr) {
		writer_->Unindent();
	}
	EndNode(memberReferenceExpression);
}
void CSharpOutputVisitor::VisitPointerReferenceExpression(Syntax::PointerReferenceExpression* pointerReferenceExpression) {
	StartNode(pointerReferenceExpression);
	pointerReferenceExpression->Target()->AcceptVisitor(*this);
	WriteToken(Syntax::PointerReferenceExpression::ArrowToken);
	WriteIdentifier(pointerReferenceExpression->MemberNameToken());
	WriteTypeArguments(ToVector(pointerReferenceExpression->TypeArguments()));
	EndNode(pointerReferenceExpression);
}
void CSharpOutputVisitor::VisitInvocationExpression(Syntax::InvocationExpression* invocationExpression) {
	StartNode(invocationExpression);
	invocationExpression->Target()->AcceptVisitor(*this);
	Space(policy_.SpaceBeforeMethodCallParentheses);
	WriteCommaSeparatedListInParenthesis(ToVector(invocationExpression->Arguments()), policy_.SpaceWithinMethodCallParentheses);
	if (dynamic_cast<Syntax::MemberReferenceExpression*>(invocationExpression->Parent()) == nullptr) {
		auto* mre = dynamic_cast<Syntax::MemberReferenceExpression*>(invocationExpression->Target());
		if (mre != nullptr) {
			if (ShouldInsertNewLineWhenInMethodCallChain(mre) >= 3)
				writer_->Unindent();
		}
	}
	EndNode(invocationExpression);
}
void CSharpOutputVisitor::VisitIndexerExpression(Syntax::IndexerExpression* indexerExpression) {
	StartNode(indexerExpression);
	if (indexerExpression->Target() != nullptr)
		indexerExpression->Target()->AcceptVisitor(*this);
	Space(policy_.SpaceBeforeMethodCallParentheses);
	WriteCommaSeparatedListInBrackets(ToVector(indexerExpression->Arguments()));
	EndNode(indexerExpression);
}
void CSharpOutputVisitor::VisitArrayInitializerExpression(Syntax::ArrayInitializerExpression* arrayInitializerExpression) {
	StartNode(arrayInitializerExpression);
	// "new List<int> { { 1 } }" and "new List<int> { 1 }" are the same semantically. The AST always
	// uses two nested ArrayInitializerExpressions for collection initializers; the output visitor
	// omits the nested braces when they are optional (a single non-assignment element whose
	// enclosing initializer is an object/collection initializer slot).
	auto& elements = arrayInitializerExpression->Elements();
	bool bracesAreOptional = elements.Count() == 1
		&& IsObjectOrCollectionInitializer(arrayInitializerExpression->Parent())
		&& !CanBeConfusedWithObjectInitializer(elements.At(0));
	if (bracesAreOptional) {
		elements.At(0)->AcceptVisitor(*this);
	} else {
		PrintInitializerElements(ToVector(elements));
	}
	EndNode(arrayInitializerExpression);
}
void CSharpOutputVisitor::VisitObjectCreateExpression(Syntax::ObjectCreateExpression* objectCreateExpression) {
	StartNode(objectCreateExpression);
	WriteKeyword(Syntax::ObjectCreateExpression::NewKeyword);
	objectCreateExpression->Type()->AcceptVisitor(*this);
	bool useParenthesis = objectCreateExpression->Arguments().Count() > 0
		|| objectCreateExpression->Initializer() == nullptr;
	if (useParenthesis) {
		Space(policy_.SpaceBeforeMethodCallParentheses);
		WriteCommaSeparatedListInParenthesis(ToVector(objectCreateExpression->Arguments()),
			policy_.SpaceWithinMethodCallParentheses);
	}
	if (objectCreateExpression->Initializer() != nullptr)
		objectCreateExpression->Initializer()->AcceptVisitor(*this);
	EndNode(objectCreateExpression);
}
void CSharpOutputVisitor::VisitArrayCreateExpression(Syntax::ArrayCreateExpression* arrayCreateExpression) {
	StartNode(arrayCreateExpression);
	WriteKeyword(Syntax::ArrayCreateExpression::NewKeyword);
	if (arrayCreateExpression->Type() != nullptr)
		arrayCreateExpression->Type()->AcceptVisitor(*this);
	if (arrayCreateExpression->Arguments().Count() > 0) {
		WriteCommaSeparatedListInBrackets(ToVector(arrayCreateExpression->Arguments()));
	}
	auto& specifiers = arrayCreateExpression->AdditionalArraySpecifiers();
	int sn = specifiers.Count();
	for (int i = 0; i < sn; ++i) {
		specifiers.At(i)->AcceptVisitor(*this);
	}
	if (arrayCreateExpression->Initializer() != nullptr)
		arrayCreateExpression->Initializer()->AcceptVisitor(*this);
	EndNode(arrayCreateExpression);
}
void CSharpOutputVisitor::VisitTupleExpression(Syntax::TupleExpression* tupleExpression) {
	StartNode(tupleExpression);
	LPar();
	WriteCommaSeparatedList(ToVector(tupleExpression->Elements()));
	RPar();
	EndNode(tupleExpression);
}
void CSharpOutputVisitor::VisitNamedExpression(Syntax::NamedExpression* namedExpression) {
	StartNode(namedExpression);
	WriteIdentifier(namedExpression->NameToken());
	Space();
	WriteToken(Tokens::Assign);
	Space();
	namedExpression->Expression()->AcceptVisitor(*this);
	EndNode(namedExpression);
}
void CSharpOutputVisitor::VisitNamedArgumentExpression(Syntax::NamedArgumentExpression* namedArgumentExpression) {
	StartNode(namedArgumentExpression);
	WriteIdentifier(namedArgumentExpression->NameToken());
	WriteToken(Tokens::Colon);
	Space();
	namedArgumentExpression->Expression()->AcceptVisitor(*this);
	EndNode(namedArgumentExpression);
}
void CSharpOutputVisitor::VisitErrorExpression(Syntax::ErrorExpression* errorExpression) {
	// The C# `void IAstVisitor.VisitErrorNode(AstNode errorNode)` -- a leaf placeholder with no
	// [Slot] children, so it renders as just a StartNode/EndNode pair (the `InsertMissingTokensDecorator`
	// records the span onto `ErrorExpression::Location` from `StartNode`'s `ILocatable` position).
	StartNode(errorExpression);
	EndNode(errorExpression);
}
void CSharpOutputVisitor::VisitOutVarDeclarationExpression(Syntax::OutVarDeclarationExpression* outVarDeclarationExpression) {
	StartNode(outVarDeclarationExpression);
	WriteKeyword(Syntax::OutVarDeclarationExpression::OutKeyword);
	Space();
	outVarDeclarationExpression->Type()->AcceptVisitor(*this);
	Space();
	outVarDeclarationExpression->Variable()->AcceptVisitor(*this);
	EndNode(outVarDeclarationExpression);
}
void CSharpOutputVisitor::VisitWithInitializerExpression(Syntax::WithInitializerExpression* withInitializerExpression) {
	StartNode(withInitializerExpression);
	withInitializerExpression->Expression()->AcceptVisitor(*this);
	WriteKeyword("with");
	withInitializerExpression->Initializer()->AcceptVisitor(*this);
	EndNode(withInitializerExpression);
}
void CSharpOutputVisitor::VisitUndocumentedExpression(Syntax::UndocumentedExpression* undocumentedExpression) {
	StartNode(undocumentedExpression);
	switch (undocumentedExpression->UndocumentedExpressionType()) {
		case Syntax::UndocumentedExpressionType::ArgList:
		case Syntax::UndocumentedExpressionType::ArgListAccess:
			WriteKeyword(Syntax::UndocumentedExpression::ArglistKeyword);
			break;
		case Syntax::UndocumentedExpressionType::MakeRef:
			WriteKeyword(Syntax::UndocumentedExpression::MakerefKeyword);
			break;
		case Syntax::UndocumentedExpressionType::RefType:
			WriteKeyword(Syntax::UndocumentedExpression::ReftypeKeyword);
			break;
		case Syntax::UndocumentedExpressionType::RefValue:
			WriteKeyword(Syntax::UndocumentedExpression::RefvalueKeyword);
			break;
	}
	if (undocumentedExpression->UndocumentedExpressionType() != Syntax::UndocumentedExpressionType::ArgListAccess) {
		Space(policy_.SpaceBeforeMethodCallParentheses);
		WriteCommaSeparatedListInParenthesis(ToVector(undocumentedExpression->Arguments()), policy_.SpaceWithinMethodCallParentheses);
	}
	EndNode(undocumentedExpression);
}
void CSharpOutputVisitor::VisitStackAllocExpression(Syntax::StackAllocExpression* stackAllocExpression) {
	StartNode(stackAllocExpression);
	WriteKeyword(Syntax::StackAllocExpression::StackallocKeyword);
	if (stackAllocExpression->Type() != nullptr)
		stackAllocExpression->Type()->AcceptVisitor(*this);
	{
		std::vector<Syntax::Expression*> count;
		if (stackAllocExpression->CountExpression() != nullptr)
			count.push_back(stackAllocExpression->CountExpression());
		WriteCommaSeparatedListInBrackets(count);
	}
	if (stackAllocExpression->Initializer() != nullptr)
		stackAllocExpression->Initializer()->AcceptVisitor(*this);
	EndNode(stackAllocExpression);
}
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
void CSharpOutputVisitor::VisitCheckedStatement(Syntax::CheckedStatement* checkedStatement) {
	StartNode(checkedStatement);
	WriteKeyword(CheckedStatement::CheckedKeyword);
	checkedStatement->Body()->AcceptVisitor(*this);
	EndNode(checkedStatement);
}
void CSharpOutputVisitor::VisitUncheckedStatement(Syntax::UncheckedStatement* uncheckedStatement) {
	StartNode(uncheckedStatement);
	WriteKeyword(UncheckedStatement::UncheckedKeyword);
	uncheckedStatement->Body()->AcceptVisitor(*this);
	EndNode(uncheckedStatement);
}
void CSharpOutputVisitor::VisitUnsafeStatement(Syntax::UnsafeStatement* unsafeStatement) {
	StartNode(unsafeStatement);
	WriteKeyword(UnsafeStatement::UnsafeKeyword);
	unsafeStatement->Body()->AcceptVisitor(*this);
	EndNode(unsafeStatement);
}
void CSharpOutputVisitor::VisitLockStatement(Syntax::LockStatement* lockStatement) {
	StartNode(lockStatement);
	WriteKeyword(LockStatement::LockKeyword);
	Space(policy_.SpaceBeforeLockParentheses);
	LPar();
	Space(policy_.SpacesWithinLockParentheses);
	lockStatement->Expression()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinLockParentheses);
	RPar();
	WriteEmbeddedStatement(lockStatement->EmbeddedStatement());
	EndNode(lockStatement);
}
void CSharpOutputVisitor::VisitUsingStatement(Syntax::UsingStatement* usingStatement) {
	StartNode(usingStatement);
	if (usingStatement->IsAsync()) {
		WriteKeyword(UsingStatement::AwaitKeyword);
	}
	WriteKeyword(UsingStatement::UsingKeyword);
	if (usingStatement->IsEnhanced()) {
		Space();
	} else {
		Space(policy_.SpaceBeforeUsingParentheses);
		LPar();
		Space(policy_.SpacesWithinUsingParentheses);
	}
	usingStatement->ResourceAcquisition()->AcceptVisitor(*this);
	if (usingStatement->IsEnhanced()) {
		Semicolon();
	} else {
		Space(policy_.SpacesWithinUsingParentheses);
		RPar();
	}
	if (usingStatement->IsEnhanced()) {
		BlockStatement* blockStatement = dynamic_cast<BlockStatement*>(usingStatement->EmbeddedStatement());
		if (blockStatement != nullptr) {
			StartNode(blockStatement);
			auto& stmts = blockStatement->Statements();
			int n = stmts.Count();
			for (int i = 0; i < n; ++i) {
				stmts.At(i)->AcceptVisitor(*this);
			}
			EndNode(blockStatement);
		} else {
			usingStatement->EmbeddedStatement()->AcceptVisitor(*this);
		}
	} else {
		WriteEmbeddedStatement(usingStatement->EmbeddedStatement());
	}
	EndNode(usingStatement);
}
void CSharpOutputVisitor::VisitForStatement(Syntax::ForStatement* forStatement) {
	StartNode(forStatement);
	WriteKeyword(ForStatement::ForKeyword);
	Space(policy_.SpaceBeforeForParentheses);
	LPar();
	Space(policy_.SpacesWithinForParentheses);
	WriteCommaSeparatedList(ToVector(forStatement->Initializers()));
	Space(policy_.SpaceBeforeForSemicolon);
	WriteToken(Tokens::Semicolon);
	Space(policy_.SpaceAfterForSemicolon);
	if (forStatement->Condition() != nullptr) {
		forStatement->Condition()->AcceptVisitor(*this);
	}
	Space(policy_.SpaceBeforeForSemicolon);
	WriteToken(Tokens::Semicolon);
	if (forStatement->Iterators().Count() > 0) {
		Space(policy_.SpaceAfterForSemicolon);
		WriteCommaSeparatedList(ToVector(forStatement->Iterators()));
	}
	Space(policy_.SpacesWithinForParentheses);
	RPar();
	WriteEmbeddedStatement(forStatement->EmbeddedStatement());
	EndNode(forStatement);
}
void CSharpOutputVisitor::VisitSingleVariableDesignation(Syntax::SingleVariableDesignation* singleVariableDesignation) {
	StartNode(singleVariableDesignation);
	WriteIdentifier(singleVariableDesignation->IdentifierToken());
	EndNode(singleVariableDesignation);
}

void CSharpOutputVisitor::VisitParenthesizedVariableDesignation(Syntax::ParenthesizedVariableDesignation* parenthesizedVariableDesignation) {
	StartNode(parenthesizedVariableDesignation);
	LPar();
	WriteCommaSeparatedList(ToVector(parenthesizedVariableDesignation->VariableDesignations()));
	RPar();
	EndNode(parenthesizedVariableDesignation);
}
void CSharpOutputVisitor::VisitForeachStatement(Syntax::ForeachStatement* foreachStatement) {
	StartNode(foreachStatement);
	if (foreachStatement->IsAsync()) {
		WriteKeyword(ForeachStatement::AwaitKeyword);
	}
	WriteKeyword(ForeachStatement::ForeachKeyword);
	Space(policy_.SpaceBeforeForeachParentheses);
	LPar();
	Space(policy_.SpacesWithinForeachParentheses);
	foreachStatement->VariableType()->AcceptVisitor(*this);
	Space();
	foreachStatement->VariableDesignation()->AcceptVisitor(*this);
	Space();
	WriteKeyword(ForeachStatement::InKeyword);
	Space();
	foreachStatement->InExpression()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinForeachParentheses);
	RPar();
	WriteEmbeddedStatement(foreachStatement->EmbeddedStatement());
	EndNode(foreachStatement);
}
void CSharpOutputVisitor::VisitVariableInitializer(Syntax::VariableInitializer* variableInitializer) {
	StartNode(variableInitializer);
	WriteIdentifier(variableInitializer->NameToken());
	if (variableInitializer->Initializer() != nullptr) {
		Space(policy_.SpaceAroundAssignment);
		WriteToken(Tokens::Assign);
		Space(policy_.SpaceAroundAssignment);
		variableInitializer->Initializer()->AcceptVisitor(*this);
	}
	EndNode(variableInitializer);
}
void CSharpOutputVisitor::VisitFixedStatement(Syntax::FixedStatement* fixedStatement) {
	StartNode(fixedStatement);
	WriteKeyword(FixedStatement::FixedKeyword);
	Space(policy_.SpaceBeforeUsingParentheses);
	LPar();
	Space(policy_.SpacesWithinUsingParentheses);
	fixedStatement->Type()->AcceptVisitor(*this);
	Space();
	WriteCommaSeparatedList(ToVector(fixedStatement->Variables()));
	Space(policy_.SpacesWithinUsingParentheses);
	RPar();
	WriteEmbeddedStatement(fixedStatement->EmbeddedStatement());
	EndNode(fixedStatement);
}
void CSharpOutputVisitor::VisitCaseLabel(Syntax::CaseLabel* caseLabel) {
	StartNode(caseLabel);
	if (caseLabel->Expression() == nullptr) {
		WriteKeyword(CaseLabel::DefaultKeyword);
	} else {
		WriteKeyword(CaseLabel::CaseKeyword);
		Space();
		caseLabel->Expression()->AcceptVisitor(*this);
	}
	WriteToken(Tokens::Colon);
	EndNode(caseLabel);
}
void CSharpOutputVisitor::VisitSwitchSection(Syntax::SwitchSection* switchSection) {
	StartNode(switchSection);
	bool first = true;
	auto& labels = switchSection->CaseLabels();
	int labelCount = labels.Count();
	for (int i = 0; i < labelCount; ++i) {
		if (!first) {
			NewLine();
		}
		labels.At(i)->AcceptVisitor(*this);
		first = false;
	}
	auto& stmts = switchSection->Statements();
	int stmtCount = stmts.Count();
	bool isBlock = (stmtCount == 1) && (dynamic_cast<BlockStatement*>(stmts.At(0)) != nullptr);
	if (policy_.IndentCaseBody && !isBlock) {
		writer_->Indent();
	}
	if (!isBlock) {
		NewLine();
	}
	for (int i = 0; i < stmtCount; ++i) {
		stmts.At(i)->AcceptVisitor(*this);
	}
	if (policy_.IndentCaseBody && !isBlock) {
		writer_->Unindent();
	}
	EndNode(switchSection);
}
void CSharpOutputVisitor::VisitSwitchStatement(Syntax::SwitchStatement* switchStatement) {
	StartNode(switchStatement);
	WriteKeyword(SwitchStatement::SwitchKeyword);
	Space(policy_.SpaceBeforeSwitchParentheses);
	LPar();
	Space(policy_.SpacesWithinSwitchParentheses);
	switchStatement->Expression()->AcceptVisitor(*this);
	Space(policy_.SpacesWithinSwitchParentheses);
	RPar();
	OpenBrace(policy_.StatementBraceStyle);
	if (!policy_.IndentSwitchBody) {
		writer_->Unindent();
	}
	auto& sections = switchStatement->SwitchSections();
	int sectionCount = sections.Count();
	for (int i = 0; i < sectionCount; ++i) {
		sections.At(i)->AcceptVisitor(*this);
	}
	if (!policy_.IndentSwitchBody) {
		writer_->Indent();
	}
	CloseBrace(policy_.StatementBraceStyle);
	NewLine();
	EndNode(switchStatement);
}
void CSharpOutputVisitor::VisitCatchClause(Syntax::CatchClause* catchClause) {
	StartNode(catchClause);
	WriteKeyword(Syntax::CatchClause::CatchKeyword);
	if (catchClause->Type() != nullptr) {
		Space(policy_.SpaceBeforeCatchParentheses);
		LPar();
		Space(policy_.SpacesWithinCatchParentheses);
		catchClause->Type()->AcceptVisitor(*this);
		auto name = catchClause->VariableName();
		if (name.has_value() && !name->empty()) {
			Space();
			WriteIdentifier(catchClause->VariableNameToken());
		}
		Space(policy_.SpacesWithinCatchParentheses);
		RPar();
	}
	if (catchClause->Condition() != nullptr) {
		Space();
		WriteKeyword(Syntax::CatchClause::WhenKeyword);
		Space(policy_.SpaceBeforeIfParentheses);
		WriteToken(Syntax::CatchClause::CondLPar);
		Space(policy_.SpacesWithinIfParentheses);
		catchClause->Condition()->AcceptVisitor(*this);
		Space(policy_.SpacesWithinIfParentheses);
		WriteToken(Syntax::CatchClause::CondRPar);
	}
	WriteBlock(catchClause->Body(), policy_.StatementBraceStyle);
	EndNode(catchClause);
}
void CSharpOutputVisitor::VisitTryCatchStatement(Syntax::TryCatchStatement* tryCatchStatement) {
	StartNode(tryCatchStatement);
	WriteKeyword(Syntax::TryCatchStatement::TryKeyword);
	WriteBlock(tryCatchStatement->TryBlock(), policy_.StatementBraceStyle);
	auto& clauses = tryCatchStatement->CatchClauses();
	int clauseCount = clauses.Count();
	for (int i = 0; i < clauseCount; ++i) {
		if (policy_.CatchNewLinePlacement == NewLinePlacement::SameLine)
			Space();
		else
			NewLine();
		clauses.At(i)->AcceptVisitor(*this);
	}
	if (tryCatchStatement->FinallyBlock() != nullptr) {
		if (policy_.FinallyNewLinePlacement == NewLinePlacement::SameLine)
			Space();
		else
			NewLine();
		WriteKeyword(Syntax::TryCatchStatement::FinallyKeyword);
		WriteBlock(tryCatchStatement->FinallyBlock(), policy_.StatementBraceStyle);
	}
	NewLine();
	EndNode(tryCatchStatement);
}
void CSharpOutputVisitor::VisitVariableDeclarationStatement(Syntax::VariableDeclarationStatement* variableDeclarationStatement) {
	StartNode(variableDeclarationStatement);
	WriteModifiers(variableDeclarationStatement->Modifiers());
	variableDeclarationStatement->Type()->AcceptVisitor(*this);
	Space();
	WriteCommaSeparatedList(ToVector(variableDeclarationStatement->Variables()));
	Semicolon();
	EndNode(variableDeclarationStatement);
}
void CSharpOutputVisitor::VisitDestructorDeclaration(Syntax::DestructorDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitFieldDeclaration(Syntax::FieldDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitAccessor(Syntax::Accessor*) { NotImplemented(); }
void CSharpOutputVisitor::VisitEnumMemberDeclaration(Syntax::EnumMemberDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitPropertyDeclaration(Syntax::PropertyDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitEventDeclaration(Syntax::EventDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitCustomEventDeclaration(Syntax::CustomEventDeclaration*) { NotImplemented(); }
void CSharpOutputVisitor::VisitParameterDeclaration(Syntax::ParameterDeclaration* parameterDeclaration) {
	StartNode(parameterDeclaration);
	WriteAttributes(ToVector(parameterDeclaration->Attributes()));
	if (parameterDeclaration->HasThisModifier()) {
		WriteKeyword(Syntax::ParameterDeclaration::ThisModifier);
		Space();
	}
	if (parameterDeclaration->IsParams()) {
		WriteKeyword(Syntax::ParameterDeclaration::ParamsModifier);
		Space();
	}
	if (parameterDeclaration->IsScopedRef()) {
		WriteKeyword(Syntax::ParameterDeclaration::ScopedRefKeyword);
		Space();
	}
	switch (parameterDeclaration->ParameterModifier()) {
		case ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref:
			WriteKeyword(Syntax::ParameterDeclaration::RefModifier);
			Space();
			break;
		case ILSpy::Decompiler::TypeSystem::ReferenceKind::RefReadOnly:
			WriteKeyword(Syntax::ParameterDeclaration::RefModifier);
			WriteKeyword(Syntax::ParameterDeclaration::ReadonlyModifier);
			Space();
			break;
		case ILSpy::Decompiler::TypeSystem::ReferenceKind::Out:
			WriteKeyword(Syntax::ParameterDeclaration::OutModifier);
			Space();
			break;
		case ILSpy::Decompiler::TypeSystem::ReferenceKind::In:
			WriteKeyword(Syntax::ParameterDeclaration::InModifier);
			Space();
			break;
		case ILSpy::Decompiler::TypeSystem::ReferenceKind::None:
			break;
	}
	if (auto* type = parameterDeclaration->Type()) {
		type->AcceptVisitor(*this);
	}
	auto name = parameterDeclaration->Name();
	if (parameterDeclaration->Type() != nullptr && name.has_value() && !name->empty()) {
		Space();
	}
	if (name.has_value() && !name->empty()) {
		WriteIdentifier(parameterDeclaration->NameToken());
	}
	if (auto* defaultExpr = parameterDeclaration->DefaultExpression()) {
		Space(policy_.SpaceAroundAssignment);
		WriteToken(Tokens::Assign);
		Space(policy_.SpaceAroundAssignment);
		defaultExpr->AcceptVisitor(*this);
	}
	EndNode(parameterDeclaration);
}
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
void CSharpOutputVisitor::VisitTupleTypeElement(Syntax::TupleTypeElement* tupleTypeElement) {
	// The C# `VisitTupleTypeElement`: `StartNode` + `Type.AcceptVisitor` + an optional `Space` +
	// `NameToken.AcceptVisitor` (the name is a nullable `string?` over a backing `NameToken`, so
	// the token guard is the C# `NameToken is not null`) + `EndNode`. The `Type` is a required
	// `AstType` slot (the C# does not guard it); the port guards it null-safely (the D327
	// `VisitMemberType` required-slot-guard precedent) so a half-constructed test node renders
	// just the node wrapper rather than dereferencing null.
	StartNode(tupleTypeElement);
	if (tupleTypeElement->Type() != nullptr) {
		tupleTypeElement->Type()->AcceptVisitor(*this);
	}
	if (tupleTypeElement->NameToken() != nullptr) {
		Space();
		tupleTypeElement->NameToken()->AcceptVisitor(*this);
	}
	EndNode(tupleTypeElement);
}

void CSharpOutputVisitor::VisitTupleType(Syntax::TupleAstType* tupleType) {
	// The C# `VisitTupleType`: `StartNode` + `LPar` + `WriteCommaSeparatedList(Elements)` + `RPar`
	// + `EndNode` (the `Debug.Assert(Elements.Count >= 2)` is a structural invariant the AST
	// maintains, not an output concern -- the port renders whatever elements the node carries).
	// The `LPar`/`RPar` helpers wrap `WriteToken(Tokens::LPar/RPar)`; the eager-vector snapshot of
	// the `Elements` collection is the D325 `ToVector` convention.
	StartNode(tupleType);
	LPar();
	WriteCommaSeparatedList(ToVector(tupleType->Elements()));
	RPar();
	EndNode(tupleType);
}

void CSharpOutputVisitor::VisitInvocationType(Syntax::InvocationAstType* invocationType) {
	// The C# `VisitInvocationType`: `StartNode` + `BaseType.AcceptVisitor` + `WriteToken(LPar)` +
	// `WriteCommaSeparatedList(Arguments)` + `WriteToken(RPar)` + `EndNode`. The C# uses
	// `WriteToken(Tokens.LPar/RPar)` (NOT the `LPar()`/`RPar()` helpers) faithfully; the `BaseType`
	// is a required `AstType` slot (guarded null-safely, the `VisitMemberType` precedent); the
	// `Arguments` collection snapshot is the `ToVector` convention.
	StartNode(invocationType);
	if (invocationType->BaseType() != nullptr) {
		invocationType->BaseType()->AcceptVisitor(*this);
	}
	WriteToken(Tokens::LPar);
	WriteCommaSeparatedList(ToVector(invocationType->Arguments()));
	WriteToken(Tokens::RPar);
	EndNode(invocationType);
}

void CSharpOutputVisitor::VisitFunctionPointerType(Syntax::FunctionPointerAstType* functionPointerType) {
	// The C# `VisitFunctionPointerType`: `StartNode` + `WriteKeyword(DelegateKeyword)` +
	// `WriteToken(PointerToken)` + an optional `Space` + `WriteKeyword("unmanaged")` (the
	// `HasUnmanagedCallingConvention` gate) + an optional `[CallingConventions]` bracket list +
	// `WriteToken(LChevron)` + `WriteCommaSeparatedList(Parameters.Concat(ReturnType))` +
	// `WriteToken(RChevron)` + `EndNode`. The `Parameters.Concat(ReturnType)` is a single
	// `IEnumerable<AstNode>` joining the parameter list with the return type (the return type is
	// the LAST element of the angle-bracket list); the port builds an eager `std::vector<AstNode*>`
	// combining the `Parameters` collection snapshot with the `ReturnType` (both upcast to
	// `AstNode*`, the common base the `WriteCommaSeparatedList<AstNode>` template recurses through).
	StartNode(functionPointerType);
	WriteKeyword(Tokens::DelegateKeyword);
	WriteToken(Syntax::FunctionPointerAstType::PointerToken);
	if (functionPointerType->HasUnmanagedCallingConvention()) {
		Space();
		WriteKeyword("unmanaged");
	}
	const auto& conventions = functionPointerType->CallingConventions();
	if (conventions.Count() > 0) {
		WriteToken(Tokens::LBracket);
		WriteCommaSeparatedList(ToVector(conventions));
		WriteToken(Tokens::RBracket);
	}
	WriteToken(Tokens::LChevron);
	std::vector<Syntax::AstNode*> combined;
	const auto& params = functionPointerType->Parameters();
	int paramCount = params.Count();
	combined.reserve(static_cast<std::size_t>(paramCount) + 1);
	for (int i = 0; i < paramCount; ++i) {
		combined.push_back(params.At(i));
	}
	if (functionPointerType->ReturnType() != nullptr) {
		combined.push_back(functionPointerType->ReturnType());
	}
	WriteCommaSeparatedList(combined);
	WriteToken(Tokens::RChevron);
	EndNode(functionPointerType);
}
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
void CSharpOutputVisitor::VisitDeclarationExpression(Syntax::DeclarationExpression* declarationExpression) {
	// The C# `VisitDeclarationExpression`: `StartNode` + `Type.AcceptVisitor` + `Space` +
	// `Designation.AcceptVisitor` + `EndNode` (the `T x` declaration form used by deconstruction
	// and out-var-declaration; both slots are required, so no null guards). The `Type` recurses
	// through its own `Visit` method (e.g. `VisitPrimitiveType`/`VisitSimpleType`), and the
	// `Designation` recurses through `VisitSingleVariableDesignation`/
	// `VisitParenthesizedVariableDesignation`.
	StartNode(declarationExpression);
	declarationExpression->Type()->AcceptVisitor(*this);
	Space();
	declarationExpression->Designation()->AcceptVisitor(*this);
	EndNode(declarationExpression);
}
void CSharpOutputVisitor::VisitAnonymousTypeCreateExpression(Syntax::AnonymousTypeCreateExpression* anonymousTypeCreateExpression) {
	// The C# `VisitAnonymousTypeCreateExpression`: `StartNode` + `WriteKeyword(NewKeyword)` +
	// `PrintInitializerElements(Initializers)` + `EndNode`. Unlike `VisitObjectCreateExpression`
	// (which renders the type then a parenthesized argument list), an anonymous object creation
	// renders just `new` followed by the initializer braces directly over the `Initializers`
	// collection (no nested `ArrayInitializerExpression` node, no type, no argument list).
	StartNode(anonymousTypeCreateExpression);
	WriteKeyword(Syntax::AnonymousTypeCreateExpression::NewKeyword);
	PrintInitializerElements(ToVector(anonymousTypeCreateExpression->Initializers()));
	EndNode(anonymousTypeCreateExpression);
}
void CSharpOutputVisitor::VisitLambdaExpression(Syntax::LambdaExpression* lambdaExpression) {
	// The C# `VisitLambdaExpression`: `StartNode` + `WriteAttributes(Attributes)` + the optional
	// `async` modifier + the parameter list (parenthesized when `LambdaNeedsParenthesis`, else
	// the single parameter inline) + `Space` + `WriteToken(Arrow)` + the body (a `WriteBlock` when
	// the body is a `BlockStatement`, else `Space` + the expression-body `AcceptVisitor`) +
	// `EndNode`. The `Body` is typed the abstract `AstNode` base (a lambda body is either a
	// `BlockStatement` or an `Expression`), so the `is BlockStatement` test ports to a
	// `dynamic_cast` is-a branch.
	StartNode(lambdaExpression);
	WriteAttributes(ToVector(lambdaExpression->Attributes()));
	if (lambdaExpression->IsAsync()) {
		WriteKeyword(Syntax::LambdaExpression::AsyncModifier);
		Space();
	}
	if (LambdaNeedsParenthesis(lambdaExpression)) {
		WriteCommaSeparatedListInParenthesis(ToVector(lambdaExpression->Parameters()),
			policy_.SpaceWithinMethodDeclarationParentheses);
	} else {
		lambdaExpression->Parameters().At(0)->AcceptVisitor(*this);
	}
	Space();
	WriteToken(Tokens::Arrow);
	if (auto* body = dynamic_cast<Syntax::BlockStatement*>(lambdaExpression->Body())) {
		WriteBlock(body, policy_.AnonymousMethodBraceStyle);
	} else {
		Space();
		lambdaExpression->Body()->AcceptVisitor(*this);
	}
	EndNode(lambdaExpression);
}
void CSharpOutputVisitor::VisitAnonymousMethodExpression(Syntax::AnonymousMethodExpression* anonymousMethodExpression) {
	// The C# `VisitAnonymousMethodExpression`: `StartNode` + the optional `async` modifier +
	// `WriteKeyword(DelegateKeyword)` + the parameter list (omitted entirely when `Parameters` is
	// empty, so `delegate {}` rather than `delegate() {}`) + `WriteBlock(Body)` + `EndNode`. The
	// `Body` is typed the concrete `BlockStatement` (an anonymous method always takes a block).
	StartNode(anonymousMethodExpression);
	if (anonymousMethodExpression->IsAsync()) {
		WriteKeyword(Syntax::AnonymousMethodExpression::AsyncModifier);
		Space();
	}
	WriteKeyword(Syntax::AnonymousMethodExpression::DelegateKeyword);
	if (anonymousMethodExpression->Parameters().Count() > 0) {
		Space(policy_.SpaceBeforeAnonymousMethodParentheses);
		WriteCommaSeparatedListInParenthesis(ToVector(anonymousMethodExpression->Parameters()),
			policy_.SpaceWithinAnonymousMethodParentheses);
	}
	WriteBlock(anonymousMethodExpression->Body(), policy_.AnonymousMethodBraceStyle);
	EndNode(anonymousMethodExpression);
}
void CSharpOutputVisitor::VisitSwitchExpressionSection(Syntax::SwitchExpressionSection* switchExpressionSection) {
	StartNode(switchExpressionSection);
	switchExpressionSection->Pattern()->AcceptVisitor(*this);
	Space();
	WriteToken(Tokens::Arrow);
	Space();
	switchExpressionSection->Body()->AcceptVisitor(*this);
	EndNode(switchExpressionSection);
}
void CSharpOutputVisitor::VisitSwitchExpression(Syntax::SwitchExpression* switchExpression) {
	StartNode(switchExpression);
	switchExpression->Expression()->AcceptVisitor(*this);
	Space();
	WriteKeyword(SwitchExpression::SwitchKeyword);
	OpenBrace(policy_.ArrayInitializerBraceStyle);
	auto& sections = switchExpression->SwitchSections();
	int sectionCount = sections.Count();
	for (int i = 0; i < sectionCount; ++i) {
		sections.At(i)->AcceptVisitor(*this);
		Comma(sections.At(i));
		NewLine();
	}
	CloseBrace(policy_.ArrayInitializerBraceStyle);
	EndNode(switchExpression);
}
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
