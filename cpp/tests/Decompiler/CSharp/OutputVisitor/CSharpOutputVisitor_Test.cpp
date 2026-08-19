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

// Tests for the `CSharpOutputVisitor` infrastructure skeleton
// (OutputVisitor/CSharpOutputVisitor.hpp) -- the first in-order slice of the C# pretty-printer.
// The class implements `IAstVisitor` (130 `Visit` methods), but only the infrastructure helpers
// (the two ctors, `StartNode`/`EndNode`, the `Comma` family, the token/brace writers, the
// write-construct helpers) are ported; the 130 `Visit` methods are throwing stubs (verified here
// to throw `std::logic_error`). The infrastructure is exercised directly through a recording
// inner `TokenWriter` (the visitor's second ctor wraps it in an `InsertRequiredSpacesDecorator`;
// the decorator only inserts a space where two tokens would merge, so the recorded call sequence
// for the punctuation/keyword/newline calls under test is exactly what the visitor wrote). This is
// the next in-order Phase-5 piece of the output stage per the D322 decision-log entry.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/CSharpOutputVisitor.hpp"
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
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
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::AstType;
using ILSpy::Decompiler::CSharp::Syntax::ArraySpecifier;
using ILSpy::Decompiler::CSharp::Syntax::BlockStatement;
using ILSpy::Decompiler::CSharp::Syntax::BreakStatement;
using ILSpy::Decompiler::CSharp::Syntax::BaseReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::ComposedType;
using ILSpy::Decompiler::CSharp::Syntax::Comment;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::ContinueStatement;
using ILSpy::Decompiler::CSharp::Syntax::EmptyStatement;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirective;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::ThisReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::YieldBreakStatement;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentExpression;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::CheckedExpression;
using ILSpy::Decompiler::CSharp::Syntax::ConditionalExpression;
using ILSpy::Decompiler::CSharp::Syntax::DirectionExpression;
using ILSpy::Decompiler::CSharp::Syntax::FieldDirection;
using ILSpy::Decompiler::CSharp::Syntax::ParenthesizedExpression;
using ILSpy::Decompiler::CSharp::Syntax::ThrowExpression;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::UncheckedExpression;

namespace {

// A recording `TokenWriter` that appends a short string per call into a vector -- the inner sink
// the visitor's `InsertRequiredSpacesDecorator` wraps. The recorded sequence lets the tests assert
// exactly what the visitor drove through the writer (the decorator inserts a space only where two
// tokens would merge, so for the punctuation/keyword/newline calls under test the sequence is
// exactly what the visitor wrote).
class RecordingTokenWriter : public TokenWriter {
public:
	std::vector<std::string> calls;

	void StartNode(AstNode* /*node*/) override { calls.push_back("start"); }
	void EndNode(AstNode* /*node*/) override { calls.push_back("end"); }
	void WriteIdentifier(Identifier* identifier) override {
		calls.push_back("id:" + std::string(identifier->Name()));
	}
	void WriteKeyword(std::string_view keyword) override {
		calls.push_back("kw:" + std::string(keyword));
	}
	void WriteToken(std::string_view token) override {
		calls.push_back("tok:" + std::string(token));
	}
	void WritePrimitiveType(std::string_view type) override {
		calls.push_back("primtype:" + std::string(type));
	}
	void WriteInterpolatedText(std::string_view text) override {
		calls.push_back("interp:" + std::string(text));
	}
	void WritePrimitiveValue(const PrimitiveValue& /*value*/, LiteralFormat /*format*/) override {
		calls.push_back("primval");
	}
	void Space() override { calls.push_back("space"); }
	void Indent() override { calls.push_back("indent"); }
	void Unindent() override { calls.push_back("unindent"); }
	void NewLine() override { calls.push_back("newline"); }
	void WriteComment(CommentType /*commentType*/, std::string_view content) override {
		calls.push_back("comment:" + std::string(content));
	}
	void WritePreProcessorDirective(PreProcessorDirectiveType /*type*/, std::optional<std::string_view> argument) override {
		calls.push_back("pp:" + (argument.has_value() ? std::string(*argument) : std::string("<null>")));
	}
};

// A fresh inner recorder + visitor pair per test (the visitor wraps the recorder in an
// `InsertRequiredSpacesDecorator` it owns; the recorder is kept alive here as the inner sink).
struct V {
	RecordingTokenWriter inner;
	std::unique_ptr<CSharpOutputVisitor> visitor;
	V() : visitor(std::make_unique<CSharpOutputVisitor>(static_cast<TokenWriter*>(&inner), CSharpFormattingOptions{})) {}
};

// A `unique_ptr<Identifier>` owning a token created via the public `Identifier::Create` factory
// (the 2-arg ctor is private, D241/D257).
std::unique_ptr<Identifier> MakeId(std::string name) {
	return std::unique_ptr<Identifier>(Identifier::Create(std::move(name)));
}

}  // namespace

// ---- ctors ----------------------------------------------------------------

// The ostream ctor rejects a null stream (the C# `ArgumentNullException` -> `std::invalid_argument`).
TEST(CSharp_OutputVisitor, OstreamCtorRejectsNullStream) {
	EXPECT_THROW(CSharpOutputVisitor v(static_cast<std::ostream*>(nullptr), CSharpFormattingOptions{}), std::invalid_argument);
}

// The `TokenWriter` ctor rejects a null writer.
TEST(CSharp_OutputVisitor, TokenWriterCtorRejectsNullWriter) {
	EXPECT_THROW(CSharpOutputVisitor v(static_cast<TokenWriter*>(nullptr), CSharpFormattingOptions{}), std::invalid_argument);
}

// Both ctors construct a usable visitor (the `writer_` is set; driving a keyword records it).
TEST(CSharp_OutputVisitor, TokenWriterCtorConstructs) {
	RecordingTokenWriter inner;
	CSharpOutputVisitor v(&inner, CSharpFormattingOptions{});
	v.WriteKeyword("void");
	ASSERT_EQ(inner.calls.size(), 1u);
	EXPECT_EQ(inner.calls[0], "kw:void");
}

// ---- StartNode/EndNode ----------------------------------------------------

// `StartNode`/`EndNode` on a trivia-free node push/pop the nesting stack and drive the writer
// (no trivia loop, so no `AcceptVisitor` call).
TEST(CSharp_OutputVisitor, StartNodeEndNodeTriviaFree) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->StartNode(node.get());
	h.visitor->EndNode(node.get());
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "end");
}

// ---- Comma ----------------------------------------------------------------

// `Comma` writes a comma (the default policy has no bracket-comma spaces, so just the token).
TEST(CSharp_OutputVisitor, CommaWritesComma) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->Comma(node.get());
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "tok:,");
}

// ---- Write tokens ---------------------------------------------------------

TEST(CSharp_OutputVisitor, WriteKeyword) {
	V h;
	h.visitor->WriteKeyword("void");
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "kw:void");
}

TEST(CSharp_OutputVisitor, WriteIdentifier) {
	V h;
	auto id = MakeId("Foo");
	h.visitor->WriteIdentifier(id.get());
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "id:Foo");
}

TEST(CSharp_OutputVisitor, WriteToken) {
	V h;
	h.visitor->WriteToken(";");
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "tok:;");
}

TEST(CSharp_OutputVisitor, LParRPar) {
	V h;
	h.visitor->LPar();
	h.visitor->RPar();
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "tok:(");
	EXPECT_EQ(h.inner.calls[1], "tok:)");
}

// `Semicolon` on a node with no slot kind (the default case) writes a semicolon and a newline.
TEST(CSharp_OutputVisitor, SemicolonDefault) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->StartNode(node.get());
	h.visitor->Semicolon();
	h.visitor->EndNode(node.get());
	// start, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:;");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// ---- Space / NewLine ------------------------------------------------------

TEST(CSharp_OutputVisitor, Space) {
	V h;
	h.visitor->Space();          // addSpace=true, isAfterSpace=false -> writes a space
	h.visitor->Space(false);    // addSpace=false -> no-op
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "space");
}

TEST(CSharp_OutputVisitor, NewLine) {
	V h;
	h.visitor->NewLine();
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "newline");
}

// ---- OpenBrace / CloseBrace -----------------------------------------------

// `OpenBrace(EndOfLine)` + `CloseBrace(EndOfLine)`: at the start of a line, just `{` (no leading
// space), then the new-line+indent, then unindent + `}`.
TEST(CSharp_OutputVisitor, OpenCloseBraceEndOfLine) {
	V h;
	h.visitor->OpenBrace(BraceStyle::EndOfLine);
	h.visitor->CloseBrace(BraceStyle::EndOfLine);
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "tok:{");
	EXPECT_EQ(h.inner.calls[1], "indent");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "unindent");
	EXPECT_EQ(h.inner.calls[4], "tok:}");
}

// `OpenBrace(NextLineShifted)` returns early (no trailing `Indent`+`NewLine`); the `{` follows a
// `NewLine`+`Indent` and the `NextLineShifted` case then emits its own `NewLine`.
TEST(CSharp_OutputVisitor, OpenBraceNextLineShifted) {
	V h;
	h.visitor->OpenBrace(BraceStyle::NextLineShifted);
	// NewLine, Indent, tok:{, NewLine (the early return skips the trailing Indent+NewLine)
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "newline");
	EXPECT_EQ(h.inner.calls[1], "indent");
	EXPECT_EQ(h.inner.calls[2], "tok:{");
	EXPECT_EQ(h.inner.calls[3], "newline");
}

// ---- WriteBlock ------------------------------------------------------------

// `WriteBlock` on an empty block: `StartNode`, `OpenBrace`, (no statements), `CloseBrace`,
// `EndNode`.
TEST(CSharp_OutputVisitor, WriteBlockEmpty) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	h.visitor->WriteBlock(block.get(), BraceStyle::EndOfLine);
	// start, tok:{, indent, newline, unindent, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[2], "indent");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "unindent");
	EXPECT_EQ(h.inner.calls[5], "tok:}");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// ---- Write constructs ------------------------------------------------------

// `WriteTypeArguments` / `WriteTypeParameters` on an empty list write nothing (the non-empty case
// recurses via `AcceptVisitor`, which the throwing stubs reject, so only the empty case is
// exercised here).
TEST(CSharp_OutputVisitor, WriteTypeArgumentsEmpty) {
	V h;
	h.visitor->WriteTypeArguments({});
	EXPECT_TRUE(h.inner.calls.empty());
}

TEST(CSharp_OutputVisitor, WriteTypeParametersEmpty) {
	V h;
	h.visitor->WriteTypeParameters({});
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WriteModifiers` iterates `CSharpModifiers::AllModifiers` in output order and writes each set
// bit's keyword + a trailing space.
TEST(CSharp_OutputVisitor, WriteModifiers) {
	V h;
	h.visitor->WriteModifiers(Modifiers::Static | Modifiers::Public);
	// AllModifiers order: Public, Private, Protected, Internal, New, Unsafe, Static, ...
	// Public then Static are set -> kw:public, space, kw:static, space
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "kw:public");
	EXPECT_EQ(h.inner.calls[1], "space");
	EXPECT_EQ(h.inner.calls[2], "kw:static");
	EXPECT_EQ(h.inner.calls[3], "space");
}

// `WriteModifiers(None)` writes nothing (no bits set; `Any` is not set either).
TEST(CSharp_OutputVisitor, WriteModifiersNone) {
	V h;
	h.visitor->WriteModifiers(Modifiers::None);
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WriteQualifiedIdentifier` writes a `.` between identifiers (and the identifiers themselves).
TEST(CSharp_OutputVisitor, WriteQualifiedIdentifier) {
	V h;
	auto a = MakeId("System");
	auto b = MakeId("Math");
	h.visitor->WriteQualifiedIdentifier({a.get(), b.get()});
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "id:System");
	EXPECT_EQ(h.inner.calls[1], "tok:.");
	EXPECT_EQ(h.inner.calls[2], "id:Math");
}

// `WriteEmbeddedStatement(nullptr)` just writes a newline.
TEST(CSharp_OutputVisitor, WriteEmbeddedStatementNull) {
	V h;
	h.visitor->WriteEmbeddedStatement(nullptr);
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "newline");
}

// `WriteEmbeddedStatement(block)` writes the block then a trailing newline.
TEST(CSharp_OutputVisitor, WriteEmbeddedStatementBlock) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	h.visitor->WriteEmbeddedStatement(block.get());
	// start, tok:{, indent, newline, unindent, tok:}, end, newline
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "newline");
}

// `WriteMethodBody(nullptr)` writes a semicolon (the empty-body case). `Semicolon` reads the
// current node's slot kind from the nesting stack (the C# `containerStack.Peek()`), so a node is
// `StartNode`'d first, as a real `VisitMethodDeclaration` would have it.
TEST(CSharp_OutputVisitor, WriteMethodBodyNull) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->StartNode(node.get());
	h.visitor->WriteMethodBody(nullptr, BraceStyle::EndOfLine);
	h.visitor->EndNode(node.get());
	// start, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:;");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `WriteMethodBody(block)` writes the block then a trailing newline.
TEST(CSharp_OutputVisitor, WriteMethodBodyBlock) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	h.visitor->WriteMethodBody(block.get(), BraceStyle::EndOfLine);
	// start, tok:{, indent, newline, unindent, tok:}, end, newline
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "newline");
}

// `WriteAttributes` on an empty list writes nothing (the non-empty case recurses via
// `AcceptVisitor`, which the throwing stubs reject).
TEST(CSharp_OutputVisitor, WriteAttributesEmpty) {
	V h;
	h.visitor->WriteAttributes({});
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WritePrivateImplementationType(nullptr)` writes nothing (the non-null case recurses via
// `AcceptVisitor`, which the throwing stubs reject).
TEST(CSharp_OutputVisitor, WritePrivateImplementationTypeNull) {
	V h;
	h.visitor->WritePrivateImplementationType(nullptr);
	EXPECT_TRUE(h.inner.calls.empty());
}

// `WriteCommaSeparatedListInParenthesis` on an empty list writes just the parens.
TEST(CSharp_OutputVisitor, WriteCommaSeparatedListInParenthesisEmpty) {
	V h;
	h.visitor->WriteCommaSeparatedListInParenthesis<AstNode>({}, false);
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "tok:(");
	EXPECT_EQ(h.inner.calls[1], "tok:)");
}

// ---- The 130 Visit stubs throw ---------------------------------------------

// A representative still-unported `Visit` method throws `std::logic_error` (the throwing-stub
// design); `VisitBlockStatement` remains a stub (the leaf `Visit` methods below are implemented).
TEST(CSharp_OutputVisitor, VisitStubThrows) {
	V h;
	auto stmt = std::make_unique<BlockStatement>();
	EXPECT_THROW(h.visitor->VisitBlockStatement(stmt.get()), std::logic_error);
}

// ---- The implemented leaf Visit methods ------------------------------------

// `VisitNullReferenceExpression` writes the `null` literal: `StartNode`, `WritePrimitiveValue`
// (the monostate/null alternative), `EndNode` (the `isAfterSpace_ = false` is internal state).
TEST(CSharp_OutputVisitor, VisitNullReferenceExpression) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	h.visitor->VisitNullReferenceExpression(node.get());
	// start, primval, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "primval");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitThisReferenceExpression` writes the `this` keyword.
TEST(CSharp_OutputVisitor, VisitThisReferenceExpression) {
	V h;
	auto node = std::make_unique<ThisReferenceExpression>();
	h.visitor->VisitThisReferenceExpression(node.get());
	// start, kw:this, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:this");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitBaseReferenceExpression` writes the `base` keyword.
TEST(CSharp_OutputVisitor, VisitBaseReferenceExpression) {
	V h;
	auto node = std::make_unique<BaseReferenceExpression>();
	h.visitor->VisitBaseReferenceExpression(node.get());
	// start, kw:base, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:base");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitPrimitiveExpression` writes the literal value via `WritePrimitiveValue`.
TEST(CSharp_OutputVisitor, VisitPrimitiveExpression) {
	V h;
	auto node = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	h.visitor->VisitPrimitiveExpression(node.get());
	// start, primval, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "primval");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitBreakStatement` writes `break` then a semicolon (the `Semicolon` helper writes the token
// and a trailing newline for a parentless node's default slot kind).
TEST(CSharp_OutputVisitor, VisitBreakStatement) {
	V h;
	auto node = std::make_unique<BreakStatement>();
	h.visitor->VisitBreakStatement(node.get());
	// start, kw:break, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:break");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitContinueStatement` writes `continue` then a semicolon.
TEST(CSharp_OutputVisitor, VisitContinueStatement) {
	V h;
	auto node = std::make_unique<ContinueStatement>();
	h.visitor->VisitContinueStatement(node.get());
	// start, kw:continue, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:continue");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitYieldBreakStatement` writes `yield break;` -- the `InsertRequiredSpacesDecorator` inserts
// the inter-keyword space the C# formatter relies on it to insert (two consecutive keywords
// would otherwise merge into one lexeme).
TEST(CSharp_OutputVisitor, VisitYieldBreakStatement) {
	V h;
	auto node = std::make_unique<YieldBreakStatement>();
	h.visitor->VisitYieldBreakStatement(node.get());
	// start, kw:yield, space, kw:break, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:yield");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:break");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitEmptyStatement` (no trivia) writes a bare semicolon; a statement carrying a comment
// would render as just that comment (the trivia path needs `VisitComment` to exercise end-to-end).
TEST(CSharp_OutputVisitor, VisitEmptyStatement) {
	V h;
	auto node = std::make_unique<EmptyStatement>();
	h.visitor->VisitEmptyStatement(node.get());
	// start, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:;");
	EXPECT_EQ(h.inner.calls[2], "newline");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// An end-to-end snippet: a `break;` statement driven through the visitor and its
// `InsertRequiredSpacesDecorator` into the recording sink yields the `break ;` token stream (the
// decorator inserts no space here -- a keyword followed by a punctuation token does not merge).
TEST(CSharp_OutputVisitor, VisitBreakStatementEndToEnd) {
	V h;
	auto node = std::make_unique<BreakStatement>();
	h.visitor->VisitBreakStatement(node.get());
	// The recorded sequence is the visitor's full output for the node.
	ASSERT_GE(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[1], "kw:break");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
}

// ---- The AstType Visit family --------------------------------------------

// `VisitIdentifier` writes the identifier token directly (NO `StartNode`/`EndNode` -- the C#
// deliberately omits them so the `ITokenWriter` treats the identifier as a flat token, not a
// nested child node).
TEST(CSharp_OutputVisitor, VisitIdentifier) {
	V h;
	auto id = MakeId("Foo");
	h.visitor->VisitIdentifier(id.get());
	ASSERT_EQ(h.inner.calls.size(), 1u);
	EXPECT_EQ(h.inner.calls[0], "id:Foo");
}

// `VisitPrimitiveType` writes the primitive-type keyword (e.g. `int`) via `WritePrimitiveType`.
TEST(CSharp_OutputVisitor, VisitPrimitiveType) {
	V h;
	auto node = std::make_unique<PrimitiveType>(std::string("int"));
	h.visitor->VisitPrimitiveType(node.get());
	// start, primtype:int, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "primtype:int");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitArraySpecifier` of rank 1 renders as `[]`.
TEST(CSharp_OutputVisitor, VisitArraySpecifierRank1) {
	V h;
	auto node = std::make_unique<ArraySpecifier>(1);
	h.visitor->VisitArraySpecifier(node.get());
	// start, tok:[, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:[");
	EXPECT_EQ(h.inner.calls[2], "tok:]");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitArraySpecifier` of rank 2 renders as `[,]` (one comma for the second dimension).
TEST(CSharp_OutputVisitor, VisitArraySpecifierRank2) {
	V h;
	auto node = std::make_unique<ArraySpecifier>(2);
	h.visitor->VisitArraySpecifier(node.get());
	// start, tok:[, tok:,, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:[");
	EXPECT_EQ(h.inner.calls[2], "tok:,");
	EXPECT_EQ(h.inner.calls[3], "tok:]");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitSimpleType` (named, no type arguments) writes the backing `IdentifierToken`.
TEST(CSharp_OutputVisitor, VisitSimpleTypeNamed) {
	V h;
	auto node = std::make_unique<SimpleType>(std::string("Foo"));
	h.visitor->VisitSimpleType(node.get());
	// start, id:Foo, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitSimpleType` with no backing token (the nameless `typeof(List<>)` case) writes nothing
// but the `StartNode`/`EndNode` pair (no identifier, no type arguments).
TEST(CSharp_OutputVisitor, VisitSimpleTypeNameless) {
	V h;
	auto node = std::make_unique<SimpleType>();
	h.visitor->VisitSimpleType(node.get());
	// start, end
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "end");
}

// `VisitMemberType` writes `Target.MemberName` (a dot separator by default): the target `SimpleType`
// recurses through `VisitSimpleType`, then the dot, then the member-name identifier.
TEST(CSharp_OutputVisitor, VisitMemberType) {
	V h;
	auto target = std::make_unique<SimpleType>(std::string("System"));
	auto node = std::make_unique<MemberType>(target.get(), std::string("Math"));
	h.visitor->VisitMemberType(node.get());
	// start(MemberType), start(SimpleType), id:System, end(SimpleType), tok:., id:Math, end(MemberType)
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:System");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:.");
	EXPECT_EQ(h.inner.calls[5], "id:Math");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitComposedType` with a bare `BaseType` (no specifiers) recurses into the base type only.
TEST(CSharp_OutputVisitor, VisitComposedTypeBare) {
	V h;
	auto baseType = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<ComposedType>();
	node->BaseType(baseType.get());
	h.visitor->VisitComposedType(node.get());
	// start(ComposedType), start(PrimitiveType), primtype:int, end(PrimitiveType), end(ComposedType)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitComposedType` with a nullable specifier, a pointer rank, and an array specifier renders
// `int?**[]` after the base type (the `?`, two `*`, then the array specifier recurses).
TEST(CSharp_OutputVisitor, VisitComposedTypeNullablePointerArray) {
	V h;
	auto baseType = std::make_unique<PrimitiveType>(std::string("int"));
	auto arrSpec = std::make_unique<ArraySpecifier>(1);
	auto node = std::make_unique<ComposedType>();
	node->BaseType(baseType.get());
	node->HasNullableSpecifier(true);
	node->PointerRank(2);
	node->ArraySpecifiers().Add(arrSpec.get());
	h.visitor->VisitComposedType(node.get());
	// start, start(primtype), primtype:int, end(primtype), tok:?, tok:*, tok:*, start(arrspec),
	// tok:[, tok:], end(arrspec), end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[4], "tok:?");
	EXPECT_EQ(h.inner.calls[5], "tok:*");
	EXPECT_EQ(h.inner.calls[6], "tok:*");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "tok:[");
	EXPECT_EQ(h.inner.calls[9], "tok:]");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitTypeReferenceExpression` writes just its `Type` child (a bare type-name expression
// such as `int` used as a value renders the type alone).
TEST(CSharp_OutputVisitor, VisitTypeReferenceExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TypeReferenceExpression>(type.get());
	h.visitor->VisitTypeReferenceExpression(node.get());
	// start(TypeReferenceExpression), start(PrimitiveType), primtype:int, end(PrimitiveType),
	// end(TypeReferenceExpression)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// ---- The Trivia Visit leaves ---------------------------------------------

// `VisitComment` drives the writer directly (NOT the visitor's `StartNode`/`EndNode`): a comment
// is trivia emitted as a flat token group (start, the comment text, end).
TEST(CSharp_OutputVisitor, VisitComment) {
	V h;
	auto node = std::make_unique<Comment>(std::string("hello"));
	h.visitor->VisitComment(node.get());
	// start, comment:hello, end (no visitor StartNode/EndNode, so no trivia walk)
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "comment:hello");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitPreProcessorDirective` drives the writer directly with the directive type and its
// optional argument (a `#if DEBUG` renders as start, pp:DEBUG, end).
TEST(CSharp_OutputVisitor, VisitPreProcessorDirective) {
	V h;
	auto node = std::make_unique<PreProcessorDirective>();
	node->Type(PreProcessorDirectiveType::If);
	node->Argument(std::string("DEBUG"));
	h.visitor->VisitPreProcessorDirective(node.get());
	// start, pp:DEBUG, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "pp:DEBUG");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitPreProcessorDirective` with no argument renders the null argument as `<null>`.
TEST(CSharp_OutputVisitor, VisitPreProcessorDirectiveNoArgument) {
	V h;
	auto node = std::make_unique<PreProcessorDirective>();
	node->Type(PreProcessorDirectiveType::Region);
	h.visitor->VisitPreProcessorDirective(node.get());
	// start, pp:<null>, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "pp:<null>");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// ---- The operator-bearing + simple Expression Visit methods (D326) --------

// `VisitBinaryOperatorExpression` over `a + b` (default policy: no space around additive
// operators) renders as start, the left `primval`, the `+` token, the right `primval`, end.
// The default `CSharpFormattingOptions` has `SpaceAroundAdditiveOperator=false`, so no spaces.
TEST(CSharp_OutputVisitor, VisitBinaryOperatorExpressionAdd) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<BinaryOperatorExpression>(left.get(), BinaryOperatorType::Add, right.get());
	h.visitor->VisitBinaryOperatorExpression(node.get());
	// start, start, primval, end, tok:+, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:+");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitBinaryOperatorExpression` over `a ?? b` -- the `NullCoalescing` operator always has
// `spacePolicy=true`, so spaces are inserted around the `??` token.
TEST(CSharp_OutputVisitor, VisitBinaryOperatorExpressionNullCoalescing) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<BinaryOperatorExpression>(left.get(), BinaryOperatorType::NullCoalescing, right.get());
	h.visitor->VisitBinaryOperatorExpression(node.get());
	// start, start, primval, end, space, tok:??, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:??");
	EXPECT_EQ(h.inner.calls[6], "space");
}

// `VisitBinaryOperatorExpression` over the `is` pattern operator uses `WriteKeyword` (the token
// equals `BinaryOperatorExpression::IsKeyword`), not `WriteToken`; `IsPattern` always has
// `spacePolicy=true`.
TEST(CSharp_OutputVisitor, VisitBinaryOperatorExpressionIsPattern) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<BinaryOperatorExpression>(left.get(), BinaryOperatorType::IsPattern, right.get());
	h.visitor->VisitBinaryOperatorExpression(node.get());
	// start, start, primval, end, space, kw:is, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[5], "kw:is");
}

// `VisitAssignmentExpression` over `a = b` (default policy: no space around assignment).
TEST(CSharp_OutputVisitor, VisitAssignmentExpression) {
	V h;
	auto left = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto right = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<AssignmentExpression>(left.get(), right.get());
	h.visitor->VisitAssignmentExpression(node.get());
	// start, start, primval, end, tok:=, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[4], "tok:=");
}

// `VisitUnaryOperatorExpression` over `-a` (prefix minus, no space policy).
TEST(CSharp_OutputVisitor, VisitUnaryOperatorExpressionMinus) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<UnaryOperatorExpression>(expr.get(), UnaryOperatorType::Minus);
	h.visitor->VisitUnaryOperatorExpression(node.get());
	// start, tok:-, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:-");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primval");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "end");
}

// `VisitUnaryOperatorExpression` over `await a` -- the `Await` operator uses `WriteKeyword` +
// an explicit `Space()`.
TEST(CSharp_OutputVisitor, VisitUnaryOperatorExpressionAwait) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<UnaryOperatorExpression>(expr.get(), UnaryOperatorType::Await);
	h.visitor->VisitUnaryOperatorExpression(node.get());
	// start, kw:await, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:await");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitUnaryOperatorExpression` over `a++` (postfix increment -- the token is written AFTER
// the expression).
TEST(CSharp_OutputVisitor, VisitUnaryOperatorExpressionPostIncrement) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<UnaryOperatorExpression>(expr.get(), UnaryOperatorType::PostIncrement);
	h.visitor->VisitUnaryOperatorExpression(node.get());
	// start, start, primval, end, tok:++, end
	ASSERT_EQ(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:++");
	EXPECT_EQ(h.inner.calls[5], "end");
}

// `VisitConditionalExpression` over `a ? b : c` (default policy: no conditional spaces).
TEST(CSharp_OutputVisitor, VisitConditionalExpression) {
	V h;
	auto cond = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto trueExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto falseExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(3)));
	auto node = std::make_unique<ConditionalExpression>(cond.get(), trueExpr.get(), falseExpr.get());
	h.visitor->VisitConditionalExpression(node.get());
	// start, start, primval, end, tok:?, start, primval, end, tok::, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[4], "tok:?");
	EXPECT_EQ(h.inner.calls[8], "tok::");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// `VisitParenthesizedExpression` over `(a)` (default policy: no spaces within parentheses).
TEST(CSharp_OutputVisitor, VisitParenthesizedExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<ParenthesizedExpression>(inner.get());
	h.visitor->VisitParenthesizedExpression(node.get());
	// start, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primval");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitCheckedExpression` over `checked(a)` -- writes the `checked` keyword then parens.
TEST(CSharp_OutputVisitor, VisitCheckedExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<CheckedExpression>(inner.get());
	h.visitor->VisitCheckedExpression(node.get());
	// start, kw:checked, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:checked");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitUncheckedExpression` over `unchecked(a)` -- writes the `unchecked` keyword then parens.
TEST(CSharp_OutputVisitor, VisitUncheckedExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<UncheckedExpression>(inner.get());
	h.visitor->VisitUncheckedExpression(node.get());
	// start, kw:unchecked, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:unchecked");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitDirectionExpression` over `out a` -- writes the `out` keyword then a space then the expr.
TEST(CSharp_OutputVisitor, VisitDirectionExpressionOut) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<DirectionExpression>(FieldDirection::Out, inner.get());
	h.visitor->VisitDirectionExpression(node.get());
	// start, kw:out, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:out");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitDirectionExpression` over `ref a` -- writes the `ref` keyword then a space then the expr.
TEST(CSharp_OutputVisitor, VisitDirectionExpressionRef) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<DirectionExpression>(FieldDirection::Ref, inner.get());
	h.visitor->VisitDirectionExpression(node.get());
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[1], "kw:ref");
}

// `VisitThrowExpression` over `throw a` -- writes the `throw` keyword then a space then the expr.
TEST(CSharp_OutputVisitor, VisitThrowExpression) {
	V h;
	auto inner = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<ThrowExpression>(inner.get());
	h.visitor->VisitThrowExpression(node.get());
	// start, kw:throw, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:throw");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}
