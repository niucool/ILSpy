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
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::BlockStatement;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::ThisReferenceExpression;

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

// A representative unported `Visit` method throws `std::logic_error` (the throwing-stub design).
TEST(CSharp_OutputVisitor, VisitStubThrows) {
	V h;
	auto node = std::make_unique<NullReferenceExpression>();
	EXPECT_THROW(h.visitor->VisitNullReferenceExpression(node.get()), std::logic_error);
	auto stmt = std::make_unique<BlockStatement>();
	EXPECT_THROW(h.visitor->VisitBlockStatement(stmt.get()), std::logic_error);
}
