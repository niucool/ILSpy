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
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/WithInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/ParenthesizedVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
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
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
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
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SwitchExpression.hpp"
#include "Decompiler/CSharp/Syntax/SwitchExpressionSection.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
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
using ILSpy::Decompiler::CSharp::Syntax::GotoCaseStatement;
using ILSpy::Decompiler::CSharp::Syntax::GotoDefaultStatement;
using ILSpy::Decompiler::CSharp::Syntax::GotoStatement;
using ILSpy::Decompiler::CSharp::Syntax::IfElseStatement;
using ILSpy::Decompiler::CSharp::Syntax::LabelStatement;
using ILSpy::Decompiler::CSharp::Syntax::EmptyStatement;
using ILSpy::Decompiler::CSharp::Syntax::ExpressionStatement;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::MemberType;
using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirective;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveExpression;
using ILSpy::Decompiler::CSharp::Syntax::ReturnStatement;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::SimpleType;
using ILSpy::Decompiler::CSharp::Syntax::ThisReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::YieldBreakStatement;
using ILSpy::Decompiler::CSharp::Syntax::YieldReturnStatement;
using ILSpy::Decompiler::CSharp::Syntax::WhileStatement;
using ILSpy::Decompiler::CSharp::Syntax::DoWhileStatement;
using ILSpy::Decompiler::CSharp::Syntax::ForStatement;
using ILSpy::Decompiler::CSharp::Syntax::ForeachStatement;
using ILSpy::Decompiler::CSharp::Syntax::LockStatement;
using ILSpy::Decompiler::CSharp::Syntax::UsingStatement;
using ILSpy::Decompiler::CSharp::Syntax::FixedStatement;
using ILSpy::Decompiler::CSharp::Syntax::VariableDeclarationStatement;
using ILSpy::Decompiler::CSharp::Syntax::SwitchStatement;
using ILSpy::Decompiler::CSharp::Syntax::SwitchSection;
using ILSpy::Decompiler::CSharp::Syntax::CaseLabel;
using ILSpy::Decompiler::CSharp::Syntax::SwitchExpression;
using ILSpy::Decompiler::CSharp::Syntax::SwitchExpressionSection;
using ILSpy::Decompiler::CSharp::Syntax::CatchClause;
using ILSpy::Decompiler::CSharp::Syntax::TryCatchStatement;
using ILSpy::Decompiler::CSharp::Syntax::DestructorDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentExpression;
using ILSpy::Decompiler::CSharp::Syntax::AssignmentOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::CheckedExpression;
using ILSpy::Decompiler::CSharp::Syntax::CheckedStatement;
using ILSpy::Decompiler::CSharp::Syntax::ConditionalExpression;
using ILSpy::Decompiler::CSharp::Syntax::DirectionExpression;
using ILSpy::Decompiler::CSharp::Syntax::FieldDirection;
using ILSpy::Decompiler::CSharp::Syntax::ParenthesizedExpression;
using ILSpy::Decompiler::CSharp::Syntax::ThrowExpression;
using ILSpy::Decompiler::CSharp::Syntax::AsExpression;
using ILSpy::Decompiler::CSharp::Syntax::IsExpression;
using ILSpy::Decompiler::CSharp::Syntax::CastExpression;
using ILSpy::Decompiler::CSharp::Syntax::TypeOfExpression;
using ILSpy::Decompiler::CSharp::Syntax::DefaultValueExpression;
using ILSpy::Decompiler::CSharp::Syntax::SizeOfExpression;
using ILSpy::Decompiler::CSharp::Syntax::IdentifierExpression;
using ILSpy::Decompiler::CSharp::Syntax::IndexerExpression;
using ILSpy::Decompiler::CSharp::Syntax::MemberReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PointerReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::InvocationExpression;
using ILSpy::Decompiler::CSharp::Syntax::TupleExpression;
using ILSpy::Decompiler::CSharp::Syntax::NamedExpression;
using ILSpy::Decompiler::CSharp::Syntax::NamedArgumentExpression;
using ILSpy::Decompiler::CSharp::Syntax::ThrowStatement;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorExpression;
using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
using ILSpy::Decompiler::CSharp::Syntax::UncheckedStatement;
using ILSpy::Decompiler::CSharp::Syntax::UnsafeStatement;
using ILSpy::Decompiler::CSharp::Syntax::UncheckedExpression;
using ILSpy::Decompiler::CSharp::Syntax::ObjectCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::ArrayCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::ArrayInitializerExpression;
using ILSpy::Decompiler::CSharp::Syntax::OutVarDeclarationExpression;
using ILSpy::Decompiler::CSharp::Syntax::WithInitializerExpression;
using ILSpy::Decompiler::CSharp::Syntax::UndocumentedExpression;
using ILSpy::Decompiler::CSharp::Syntax::UndocumentedExpressionType;
using ILSpy::Decompiler::CSharp::Syntax::StackAllocExpression;
using ILSpy::Decompiler::CSharp::Syntax::VariableInitializer;
using ILSpy::Decompiler::CSharp::Syntax::ErrorExpression;
using ILSpy::Decompiler::CSharp::Syntax::SingleVariableDesignation;
using ILSpy::Decompiler::CSharp::Syntax::ParenthesizedVariableDesignation;
using ILSpy::Decompiler::CSharp::Syntax::TupleTypeElement;
using ILSpy::Decompiler::CSharp::Syntax::TupleAstType;
using ILSpy::Decompiler::CSharp::Syntax::InvocationAstType;
using ILSpy::Decompiler::CSharp::Syntax::FunctionPointerAstType;
using ILSpy::Decompiler::CSharp::Syntax::AnonymousTypeCreateExpression;
using ILSpy::Decompiler::CSharp::Syntax::LambdaExpression;
using ILSpy::Decompiler::CSharp::Syntax::AnonymousMethodExpression;
using ILSpy::Decompiler::CSharp::Syntax::DeclarationExpression;
using ILSpy::Decompiler::CSharp::Syntax::ParameterDeclaration;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;

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
// design); `VisitDestructorDeclaration` remains a stub (the `VariableDeclarationStatement` and
// the try/catch family `Visit` methods above are now implemented, the TypeMember hierarchy
// below is still a stub).
TEST(CSharp_OutputVisitor, VisitStubThrows) {
	V h;
	auto decl = std::make_unique<DestructorDeclaration>();
	EXPECT_THROW(h.visitor->VisitDestructorDeclaration(decl.get()), std::logic_error);
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

// ---- The type-keyword expression Visit methods ----------------------------

// `VisitCastExpression` over `(int)a` -- writes `( ` + Type + ` )` + the cast operand; the default
// policy has no spaces within the cast parens nor after the typecast, so it renders `(int)a`.
TEST(CSharp_OutputVisitor, VisitCastExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<CastExpression>(type.get(), expr.get());
	h.visitor->VisitCastExpression(node.get());
	// start, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:),
	// start(PrimitiveExpression), primval, end(PrimitiveExpression), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primtype:int");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "primval");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitAsExpression` over `42 as int` -- writes the operand, an explicit space, the `as` keyword,
// an explicit space, then the type (the C# writes both spaces explicitly, so the decorator inserts
// none before the type).
TEST(CSharp_OutputVisitor, VisitAsExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<AsExpression>(expr.get(), type.get());
	h.visitor->VisitAsExpression(node.get());
	// start, start(PrimitiveExpression), primval, end(PrimitiveExpression), space, kw:as, space,
	// start(PrimitiveType), primtype:int, end(PrimitiveType), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:as");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primtype:int");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitIsExpression` over `42 is int` -- writes the operand, a space, the `is` keyword, then the
// type; the C# writes NO explicit space after `is`, so the decorator inserts the separating space
// before the primitive type (a keyword-then-primitive-type merge guard).
TEST(CSharp_OutputVisitor, VisitIsExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<IsExpression>(expr.get(), type.get());
	h.visitor->VisitIsExpression(node.get());
	// start, start(PrimitiveExpression), primval, end(PrimitiveExpression), space, kw:is,
	// start(PrimitiveType), space (decorator-inserted before the primitive type), primtype:int,
	// end(PrimitiveType), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:is");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "primtype:int");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitTypeOfExpression` over `typeof(int)` -- writes the `typeof` keyword, parens, and the type;
// the default policy has no spaces within the parens, so it renders `typeof(int)`.
TEST(CSharp_OutputVisitor, VisitTypeOfExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TypeOfExpression>(type.get());
	h.visitor->VisitTypeOfExpression(node.get());
	// start, kw:typeof, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:typeof");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitDefaultValueExpression` over `default(int)` -- structurally identical to `typeof(int)`
// but with the `default` keyword (the C# uses the same `SpacesWithinTypeOfParentheses` policy).
TEST(CSharp_OutputVisitor, VisitDefaultValueExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<DefaultValueExpression>(type.get());
	h.visitor->VisitDefaultValueExpression(node.get());
	// start, kw:default, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:default");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitSizeOfExpression` over `sizeof(int)` -- structurally identical to `typeof(int)` but with
// the `sizeof` keyword and the `SpacesWithinSizeOfParentheses` policy.
TEST(CSharp_OutputVisitor, VisitSizeOfExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<SizeOfExpression>(type.get());
	h.visitor->VisitSizeOfExpression(node.get());
	// start, kw:sizeof, tok:(, start(PrimitiveType), primtype:int, end(PrimitiveType), tok:), end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:sizeof");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "end");
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

// ---- The implemented statement Visit methods (the simple statements) ----------

// `VisitReturnStatement` with no expression writes `return;`.
TEST(CSharp_OutputVisitor, VisitReturnStatementNoExpression) {
	V h;
	auto node = std::make_unique<ReturnStatement>();
	h.visitor->VisitReturnStatement(node.get());
	// start, kw:return, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:return");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitReturnStatement` with an expression writes `return <expr>;`.
TEST(CSharp_OutputVisitor, VisitReturnStatementWithExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(42)));
	auto node = std::make_unique<ReturnStatement>();
	node->Expression(expr.get());
	h.visitor->VisitReturnStatement(node.get());
	// start, kw:return, space, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:return");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:;");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitThrowStatement` with no expression writes `throw;`.
TEST(CSharp_OutputVisitor, VisitThrowStatementNoExpression) {
	V h;
	auto node = std::make_unique<ThrowStatement>();
	h.visitor->VisitThrowStatement(node.get());
	// start, kw:throw, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[1], "kw:throw");
	EXPECT_EQ(h.inner.calls[2], "tok:;");
}

// `VisitThrowStatement` with an expression writes `throw <expr>;`.
TEST(CSharp_OutputVisitor, VisitThrowStatementWithExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<ThrowStatement>();
	node->Expression(expr.get());
	h.visitor->VisitThrowStatement(node.get());
	// start, kw:throw, space, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[4], "primval");
}

// `VisitExpressionStatement` writes the expression then a semicolon.
TEST(CSharp_OutputVisitor, VisitExpressionStatement) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(7)));
	auto node = std::make_unique<ExpressionStatement>(expr.get());
	h.visitor->VisitExpressionStatement(node.get());
	// start, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitGotoStatement` writes `goto <label>;` -- the `InsertRequiredSpacesDecorator`
// inserts the space between the `goto` keyword and the label identifier.
TEST(CSharp_OutputVisitor, VisitGotoStatement) {
	V h;
	auto node = std::make_unique<GotoStatement>(std::string("myLabel"));
	h.visitor->VisitGotoStatement(node.get());
	// start, kw:goto, space, id:myLabel, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:goto");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "id:myLabel");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitGotoCaseStatement` writes `goto case <expr>;` -- the decorator inserts the space
// between the two keywords, and the explicit `Space()` precedes the label expression.
TEST(CSharp_OutputVisitor, VisitGotoCaseStatement) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<GotoCaseStatement>(expr.get());
	h.visitor->VisitGotoCaseStatement(node.get());
	// start, kw:goto, space, kw:case, space, start, primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:goto");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:case");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitGotoDefaultStatement` writes `goto default;` -- the decorator inserts the space.
TEST(CSharp_OutputVisitor, VisitGotoDefaultStatement) {
	V h;
	auto node = std::make_unique<GotoDefaultStatement>();
	h.visitor->VisitGotoDefaultStatement(node.get());
	// start, kw:goto, space, kw:default, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:goto");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:default");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitBlockStatement` on an empty block writes `{` indent newline unindent `}` then a newline.
TEST(CSharp_OutputVisitor, VisitBlockStatementEmpty) {
	V h;
	auto node = std::make_unique<BlockStatement>();
	h.visitor->VisitBlockStatement(node.get());
	// start, tok:{, indent, newline, unindent, tok:}, end, newline
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[2], "indent");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "unindent");
	EXPECT_EQ(h.inner.calls[5], "tok:}");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "newline");
}

// `VisitLabelStatement` on a parentless label (no following sibling) emits a trailing
// semicolon so the label is syntactically valid as the last statement.
TEST(CSharp_OutputVisitor, VisitLabelStatementAloneEmitsSemicolon) {
	V h;
	auto node = std::make_unique<LabelStatement>(std::string("myLabel"));
	h.visitor->VisitLabelStatement(node.get());
	// start, id:myLabel, tok::, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 6u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:myLabel");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "tok:;");
	EXPECT_EQ(h.inner.calls[4], "newline");
	EXPECT_EQ(h.inner.calls[5], "end");
}

// `VisitLabelStatement` on a label that has a following sibling statement (same slot kind)
// emits no trailing semicolon -- the following statement already makes the label valid.
TEST(CSharp_OutputVisitor, VisitLabelStatementWithFollowingStatementNoSemicolon) {
	V h;
	auto block = std::make_unique<BlockStatement>();
	auto label = std::make_unique<LabelStatement>(std::string("myLabel"));
	auto brk = std::make_unique<BreakStatement>();
	block->Statements().Add(label.get());
	block->Statements().Add(brk.get());
	h.visitor->VisitLabelStatement(label.get());
	// start, id:myLabel, tok::, newline, end (no semicolon)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:myLabel");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "newline");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// ---- The control-flow statement Visit methods -----------------------------

// `VisitWhileStatement` writes `while (cond) body`: the condition is a required `Expression`
// (a `PrimitiveExpression` records `start`/`primval`/`end`), the body a required embedded
// `Statement`. With the default policy (no space before/within the parens), a non-block `break`
// body renders as newline + indent + the break's own sequence + unindent; the `Semicolon` emits a
// `tok:;` then a `newline`.
TEST(CSharp_OutputVisitor, VisitWhileStatementNonBlockBody) {
	V h;
	auto node = std::make_unique<WhileStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BreakStatement>();
	node->Condition(cond.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitWhileStatement(node.get());
	// start, kw:while, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:while");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitWhileStatement` with a `BlockStatement` body renders the block inline via
// `WriteEmbeddedStatement`'s block path (`WriteBlock` + newline). The `OpenBrace` for the default
// `EndOfLine` brace style inserts a `space` before `tok:{` because the line is not empty (the
// close paren was just written), so the block records `start`/`space`/`tok:{`/.../`tok:}`/`end`.
TEST(CSharp_OutputVisitor, VisitWhileStatementBlockBody) {
	V h;
	auto node = std::make_unique<WhileStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BlockStatement>();
	node->Condition(cond.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitWhileStatement(node.get());
	// start, kw:while, tok:(, start, primval, end, tok:), start, space, tok:{, indent, newline,
	// unindent, tok:}, end, newline, end
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:while");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "tok:{");
	EXPECT_EQ(h.inner.calls[10], "indent");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "unindent");
	EXPECT_EQ(h.inner.calls[13], "tok:}");
	EXPECT_EQ(h.inner.calls[14], "end");
	EXPECT_EQ(h.inner.calls[15], "newline");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitDoWhileStatement` writes `do body while (cond);`: the body precedes the `while` clause
// (the slot order is reversed relative to `WhileStatement`), the block body's `OpenBrace`
// inserts a `space` after `do`, and a trailing `Semicolon` (tok:; + newline) closes the statement.
TEST(CSharp_OutputVisitor, VisitDoWhileStatement) {
	V h;
	auto node = std::make_unique<DoWhileStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BlockStatement>();
	node->EmbeddedStatement(body.get());
	node->Condition(cond.get());
	h.visitor->VisitDoWhileStatement(node.get());
	// start, kw:do, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, kw:while,
	// tok:(, start, primval, end, tok:), tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 20u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:do");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "kw:while");
	EXPECT_EQ(h.inner.calls[12], "tok:(");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "primval");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "tok:)");
	EXPECT_EQ(h.inner.calls[17], "tok:;");
	EXPECT_EQ(h.inner.calls[18], "newline");
	EXPECT_EQ(h.inner.calls[19], "end");
}

// `VisitIfElseStatement` with no `FalseStatement` writes `if (cond) body` (the single-branch
// `WriteEmbeddedStatement(TrueStatement)` path). The condition `PrimitiveExpression` records its
// own `start`/`primval`/`end`; the non-block `break` body renders as newline + indent + the
// break's sequence + unindent.
TEST(CSharp_OutputVisitor, VisitIfElseStatementSingleBranch) {
	V h;
	auto node = std::make_unique<IfElseStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto body = std::make_unique<BreakStatement>();
	node->Condition(cond.get());
	node->TrueStatement(body.get());
	h.visitor->VisitIfElseStatement(node.get());
	// start, kw:if, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:if");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitIfElseStatement` with a non-`IfElseStatement` `FalseStatement` writes `if (cond) body
// else body` -- the `else` keyword follows the true branch (on its own line per the default
// `ElseNewLinePlacement`), then the false branch is written as another embedded statement. No
// `space` is inserted before `kw:else` because the true branch's `Semicolon` `newline` and the
// following `unindent` both leave `lastWritten` as `Whitespace`.
TEST(CSharp_OutputVisitor, VisitIfElseStatementElseBranch) {
	V h;
	auto node = std::make_unique<IfElseStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto trueBody = std::make_unique<BreakStatement>();
	auto falseBody = std::make_unique<ContinueStatement>();
	node->Condition(cond.get());
	node->TrueStatement(trueBody.get());
	node->FalseStatement(falseBody.get());
	h.visitor->VisitIfElseStatement(node.get());
	// start, kw:if, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, kw:else, newline, indent, start, kw:continue, tok:;, newline, end,
	// unindent, end
	ASSERT_EQ(h.inner.calls.size(), 25u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:if");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "kw:else");
	EXPECT_EQ(h.inner.calls[16], "newline");
	EXPECT_EQ(h.inner.calls[17], "indent");
	EXPECT_EQ(h.inner.calls[18], "start");
	EXPECT_EQ(h.inner.calls[19], "kw:continue");
	EXPECT_EQ(h.inner.calls[20], "tok:;");
	EXPECT_EQ(h.inner.calls[21], "newline");
	EXPECT_EQ(h.inner.calls[22], "end");
	EXPECT_EQ(h.inner.calls[23], "unindent");
	EXPECT_EQ(h.inner.calls[24], "end");
}

// `VisitIfElseStatement` with a nested `IfElseStatement` as the `FalseStatement` writes
// `else if` on one line -- the false branch is recursed directly (no `WriteEmbeddedStatement`
// newline/indent), and the `InsertRequiredSpacesDecorator` inserts a `space` between `else` and
// the nested `if` because the two `WriteKeyword` calls would merge (the `StartNode` between them
// does not reset `lastWritten`).
TEST(CSharp_OutputVisitor, VisitIfElseStatementElseIf) {
	V h;
	auto node = std::make_unique<IfElseStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(int32_t(1));
	auto trueBody = std::make_unique<BreakStatement>();
	auto nested = std::make_unique<IfElseStatement>();
	auto nestedCond = std::make_unique<PrimitiveExpression>(int32_t(2));
	auto nestedBody = std::make_unique<ContinueStatement>();
	nested->Condition(nestedCond.get());
	nested->TrueStatement(nestedBody.get());
	node->Condition(cond.get());
	node->TrueStatement(trueBody.get());
	node->FalseStatement(nested.get());
	h.visitor->VisitIfElseStatement(node.get());
	// start, kw:if, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, kw:else, start (nested), space (decorator), kw:if, tok:(, start,
	// primval, end, tok:), newline, indent, start, kw:continue, tok:;, newline, end, unindent,
	// end (nested), end (outer)
	ASSERT_EQ(h.inner.calls.size(), 34u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:if");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "kw:else");
	EXPECT_EQ(h.inner.calls[16], "start");
	EXPECT_EQ(h.inner.calls[17], "space");
	EXPECT_EQ(h.inner.calls[18], "kw:if");
	EXPECT_EQ(h.inner.calls[19], "tok:(");
	EXPECT_EQ(h.inner.calls[20], "start");
	EXPECT_EQ(h.inner.calls[21], "primval");
	EXPECT_EQ(h.inner.calls[22], "end");
	EXPECT_EQ(h.inner.calls[23], "tok:)");
	EXPECT_EQ(h.inner.calls[24], "newline");
	EXPECT_EQ(h.inner.calls[25], "indent");
	EXPECT_EQ(h.inner.calls[26], "start");
	EXPECT_EQ(h.inner.calls[27], "kw:continue");
	EXPECT_EQ(h.inner.calls[28], "tok:;");
	EXPECT_EQ(h.inner.calls[29], "newline");
	EXPECT_EQ(h.inner.calls[30], "end");
	EXPECT_EQ(h.inner.calls[31], "unindent");
	EXPECT_EQ(h.inner.calls[32], "end");
	EXPECT_EQ(h.inner.calls[33], "end");
}

// `VisitYieldReturnStatement` writes `yield return expr;`: two consecutive `WriteKeyword` calls
// (`yield` then `return`) would merge into one lexeme without the `InsertRequiredSpacesDecorator`,
// which inserts the inter-keyword `space`; then a `Space` + the expression (its own
// `start`/`primval`/`end`) + `Semicolon` (tok:; + newline).
TEST(CSharp_OutputVisitor, VisitYieldReturnStatement) {
	V h;
	auto node = std::make_unique<YieldReturnStatement>();
	auto expr = std::make_unique<PrimitiveExpression>(int32_t(1));
	node->Expression(expr.get());
	h.visitor->VisitYieldReturnStatement(node.get());
	// start, kw:yield, space (decorator-inserted), kw:return, space (visitor-written), start,
	// primval, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:yield");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:return");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitCheckedStatement` writes `checked` then recurses into the `Body` block via
// `AcceptVisitor` (dispatching to `VisitBlockStatement` -> `WriteBlock` + `NewLine`). The default
// `EndOfLine` brace style inserts a `space` before `tok:{` because the line is not empty (the
// `checked` keyword was just written), so the block records `start`/`space`/`tok:{`/`indent`/
// `newline`/`unindent`/`tok:}`/`end`/`newline`.
TEST(CSharp_OutputVisitor, VisitCheckedStatement) {
	V h;
	auto node = std::make_unique<CheckedStatement>();
	auto body = std::make_unique<BlockStatement>();
	node->Body(body.get());
	h.visitor->VisitCheckedStatement(node.get());
	// start, kw:checked, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:checked");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitUncheckedStatement` is the structural twin of `VisitCheckedStatement` with the
// `unchecked` keyword; the recorded sequence is identical except for the keyword.
TEST(CSharp_OutputVisitor, VisitUncheckedStatement) {
	V h;
	auto node = std::make_unique<UncheckedStatement>();
	auto body = std::make_unique<BlockStatement>();
	node->Body(body.get());
	h.visitor->VisitUncheckedStatement(node.get());
	// start, kw:unchecked, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:unchecked");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitUnsafeStatement` is the structural twin of `VisitCheckedStatement` with the `unsafe`
// keyword; the recorded sequence is identical except for the keyword.
TEST(CSharp_OutputVisitor, VisitUnsafeStatement) {
	V h;
	auto node = std::make_unique<UnsafeStatement>();
	auto body = std::make_unique<BlockStatement>();
	node->Body(body.get());
	h.visitor->VisitUnsafeStatement(node.get());
	// start, kw:unsafe, start, space, tok:{, indent, newline, unindent, tok:}, end, newline, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:unsafe");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// ---- The simple member/argument expression Visit methods -------------------

// `VisitIdentifierExpression` (named, no type arguments) writes the backing `IdentifierToken`.
// Structurally `VisitSimpleType` applied to the `Expression` hierarchy (the D325 `ToVector`
// snapshot of an empty `TypeArguments` collection writes nothing).
TEST(CSharp_OutputVisitor, VisitIdentifierExpressionNamed) {
	V h;
	auto node = std::make_unique<IdentifierExpression>(std::string("Foo"));
	h.visitor->VisitIdentifierExpression(node.get());
	// start, id:Foo, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:Foo");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitIdentifierExpression` with a `TypeArguments` collection renders `List<T>` -- the
// `WriteTypeArguments` writes the chevrons and recurses through `VisitSimpleType`.
TEST(CSharp_OutputVisitor, VisitIdentifierExpressionWithTypeArguments) {
	V h;
	auto typeArg = std::make_unique<SimpleType>(std::string("T"));
	auto node = std::make_unique<IdentifierExpression>(std::string("List"));
	node->TypeArguments().Add(typeArg.get());
	h.visitor->VisitIdentifierExpression(node.get());
	// start, id:List, tok:<, start(SimpleType), id:T, end(SimpleType), tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:List");
	EXPECT_EQ(h.inner.calls[2], "tok:<");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:T");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:>");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitIndexerExpression` over `a[0]` -- the `Target` recurses through `VisitIdentifierExpression`,
// then the bracketed argument list (the default policy has no spaces before/within the brackets).
TEST(CSharp_OutputVisitor, VisitIndexerExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto node = std::make_unique<IndexerExpression>(target.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitIndexerExpression(node.get());
	// start, start(IdentifierExpression), id:a, end, tok:[, start(PrimitiveExpression), primval,
	// end, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:[");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:]");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitIndexerExpression` with a null `Target` and an empty argument list renders `[]`.
TEST(CSharp_OutputVisitor, VisitIndexerExpressionNoTargetEmpty) {
	V h;
	auto node = std::make_unique<IndexerExpression>();
	h.visitor->VisitIndexerExpression(node.get());
	// start, tok:[, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:[");
	EXPECT_EQ(h.inner.calls[2], "tok:]");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitNamedArgumentExpression` over `name: 0` -- the name identifier, a colon, an explicit
// `Space()`, then the argument expression.
TEST(CSharp_OutputVisitor, VisitNamedArgumentExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto node = std::make_unique<NamedArgumentExpression>(std::string("name"), expr.get());
	h.visitor->VisitNamedArgumentExpression(node.get());
	// start, id:name, tok::, space, start(PrimitiveExpression), primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:name");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "primval");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitNamedExpression` over `name = 0` -- the name identifier, an explicit `Space()`, the
// assign token, another explicit `Space()`, then the initializer expression.
TEST(CSharp_OutputVisitor, VisitNamedExpression) {
	V h;
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto node = std::make_unique<NamedExpression>(std::string("name"), expr.get());
	h.visitor->VisitNamedExpression(node.get());
	// start, id:name, space, tok:=, space, start(PrimitiveExpression), primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:name");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:=");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitMemberReferenceExpression` over `a.B` -- the `Target` recurses through
// `VisitIdentifierExpression`, then the dot and the member-name identifier. A standalone member
// reference (the `Target` is not an `InvocationExpression`) has chain length 0, so no chain
// newline/indent is inserted.
TEST(CSharp_OutputVisitor, VisitMemberReferenceExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto node = std::make_unique<MemberReferenceExpression>(target.get(), std::string("B"));
	h.visitor->VisitMemberReferenceExpression(node.get());
	// start, start(IdentifierExpression), id:a, end, tok:., id:B, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:.");
	EXPECT_EQ(h.inner.calls[5], "id:B");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitMemberReferenceExpression` with a `TypeArguments` collection renders `a.B<T>` -- the
// `WriteTypeArguments` writes the chevrons and recurses through `VisitSimpleType`.
TEST(CSharp_OutputVisitor, VisitMemberReferenceExpressionWithTypeArguments) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto typeArg = std::make_unique<SimpleType>(std::string("T"));
	auto node = std::make_unique<MemberReferenceExpression>(target.get(), std::string("B"));
	node->TypeArguments().Add(typeArg.get());
	h.visitor->VisitMemberReferenceExpression(node.get());
	// start, start(id:a), id:a, end, tok:., id:B, tok:<, start(SimpleType), id:T, end, tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:.");
	EXPECT_EQ(h.inner.calls[5], "id:B");
	EXPECT_EQ(h.inner.calls[6], "tok:<");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:T");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:>");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitPointerReferenceExpression` over `a->B` -- the structural twin of the member reference,
// using the `ArrowToken` (`->`) instead of the dot. No chain newline logic (the C# source has
// none for pointer member access).
TEST(CSharp_OutputVisitor, VisitPointerReferenceExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("a"));
	auto node = std::make_unique<PointerReferenceExpression>(target.get(), std::string("B"));
	h.visitor->VisitPointerReferenceExpression(node.get());
	// start, start(IdentifierExpression), id:a, end, tok:->, id:B, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:a");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:->");
	EXPECT_EQ(h.inner.calls[5], "id:B");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitInvocationExpression` over `Foo()` -- the `Target` recurses through
// `VisitIdentifierExpression`, then the empty argument list in parentheses (the default policy
// has no space before/within the call parentheses).
TEST(CSharp_OutputVisitor, VisitInvocationExpression) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("Foo"));
	auto node = std::make_unique<InvocationExpression>(target.get());
	h.visitor->VisitInvocationExpression(node.get());
	// start, start(IdentifierExpression), id:Foo, end, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitInvocationExpression` over `Foo(1)` -- a single argument recurses through
// `VisitPrimitiveExpression` between the parentheses (no comma for a single-element list).
TEST(CSharp_OutputVisitor, VisitInvocationExpressionOneArg) {
	V h;
	auto target = std::make_unique<IdentifierExpression>(std::string("Foo"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<InvocationExpression>(target.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitInvocationExpression(node.get());
	// start, start(id:Foo), id:Foo, end, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitTupleExpression` over `(1, 2)` -- the elements in parentheses, comma-separated (the
// default policy has no bracket-comma spaces).
TEST(CSharp_OutputVisitor, VisitTupleExpression) {
	V h;
	auto e0 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<TupleExpression>();
	node->Elements().Add(e0.get());
	node->Elements().Add(e1.get());
	h.visitor->VisitTupleExpression(node.get());
	// start, tok:(, start, primval, end, tok:,, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "primval");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:,");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "primval");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "tok:)");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitObjectCreateExpression` over `new Foo()` -- no arguments and no initializer, so the
// `useParenthesis = Arguments.Any() || Initializer is null` gate emits the empty call parens
// (the default policy has no space before/within the call parentheses).
TEST(CSharp_OutputVisitor, VisitObjectCreateExpressionNoArgsNoInitializer) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("Foo"));
	auto node = std::make_unique<ObjectCreateExpression>(type.get());
	h.visitor->VisitObjectCreateExpression(node.get());
	// start, kw:new, start(SimpleType), space (decorator: new->identifier), id:Foo, end, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "id:Foo");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:(");
	EXPECT_EQ(h.inner.calls[7], "tok:)");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitObjectCreateExpression` over `new Foo(1)` -- one argument, so the call parens wrap the
// comma-separated argument list (the default policy has no inner spaces).
TEST(CSharp_OutputVisitor, VisitObjectCreateExpressionOneArg) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("Foo"));
	auto arg = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<ObjectCreateExpression>(type.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitObjectCreateExpression(node.get());
	// start, kw:new, start(SimpleType), space (decorator: new->identifier), id:Foo, end, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "id:Foo");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:(");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:)");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitArrayCreateExpression` over `new int[5]` -- a sized array (the `Type` recurses through
// `VisitPrimitiveType`, the size expression in brackets, no initializer).
TEST(CSharp_OutputVisitor, VisitArrayCreateExpressionSized) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto size = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(5)));
	auto node = std::make_unique<ArrayCreateExpression>(type.get());
	node->Arguments().Add(size.get());
	h.visitor->VisitArrayCreateExpression(node.get());
	// start, kw:new, start(PrimitiveType), space (decorator: new->primtype), primtype:int, end, tok:[, start, primval, end, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:[");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:]");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitArrayCreateExpression` over `new int[] { 1, 2 }` -- an empty size bracket followed by an
// `ArrayInitializerExpression` (the empty `Arguments` skips the bracket write; the initializer
// recurses through `VisitArrayInitializerExpression`).
TEST(CSharp_OutputVisitor, VisitArrayCreateExpressionWithInitializer) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto e0 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto init = std::make_unique<ArrayInitializerExpression>();
	init->Elements().Add(e0.get());
	init->Elements().Add(e1.get());
	auto node = std::make_unique<ArrayCreateExpression>(type.get());
	node->Initializer(init.get());
	h.visitor->VisitArrayCreateExpression(node.get());
	// start, kw:new, start(PrimitiveType), space (decorator: new->primtype), primtype:int, end, then the initializer
	// { 1, 2 }: start, space (OpenBrace, line not empty), tok:{, space, start, primval, end, tok:,, space,
	// start, primval, end, space, tok:}, end, then end.
	ASSERT_EQ(h.inner.calls.size(), 22u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "tok:{");
	EXPECT_EQ(h.inner.calls[9], "space");
	EXPECT_EQ(h.inner.calls[10], "start");
	EXPECT_EQ(h.inner.calls[11], "primval");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "tok:,");
	EXPECT_EQ(h.inner.calls[14], "space");
	EXPECT_EQ(h.inner.calls[15], "start");
	EXPECT_EQ(h.inner.calls[16], "primval");
	EXPECT_EQ(h.inner.calls[17], "end");
	EXPECT_EQ(h.inner.calls[18], "space");
	EXPECT_EQ(h.inner.calls[19], "tok:}");
	EXPECT_EQ(h.inner.calls[20], "end");
	EXPECT_EQ(h.inner.calls[21], "end");
}

// `VisitArrayCreateExpression` over `new int[5][]` -- a sized first bracket followed by an
// additional rank-2 `ArraySpecifier` (the trailing `[,]` without size info).
TEST(CSharp_OutputVisitor, VisitArrayCreateExpressionAdditionalSpecifier) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto size = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(5)));
	auto spec = std::make_unique<ArraySpecifier>(2);
	auto node = std::make_unique<ArrayCreateExpression>(type.get());
	node->Arguments().Add(size.get());
	node->AdditionalArraySpecifiers().Add(spec.get());
	h.visitor->VisitArrayCreateExpression(node.get());
	// start, kw:new, start(PrimitiveType), space (decorator: new->primtype), primtype:int, end, tok:[, start, primval, end, tok:],
	// then VisitArraySpecifier rank 2: start, tok:[, tok:,, tok:], end, then end.
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:[");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:]");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "tok:[");
	EXPECT_EQ(h.inner.calls[13], "tok:,");
	EXPECT_EQ(h.inner.calls[14], "tok:]");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitArrayInitializerExpression` over a standalone `{ 1, 2 }` -- the `PrintInitializerElements`
// path (the `bracesAreOptional` gate is false for a parentless initializer), the default policy
// does not wrap two simple primitives, so the braces hug with inner spaces.
TEST(CSharp_OutputVisitor, VisitArrayInitializerExpressionTwoElements) {
	V h;
	auto e0 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<ArrayInitializerExpression>();
	node->Elements().Add(e0.get());
	node->Elements().Add(e1.get());
	h.visitor->VisitArrayInitializerExpression(node.get());
	// start, tok:{, space, start, primval, end, tok:,, space, start, primval, end, space, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 14u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:{");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:,");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "primval");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "space");
	EXPECT_EQ(h.inner.calls[12], "tok:}");
	EXPECT_EQ(h.inner.calls[13], "end");
}

// `VisitArrayInitializerExpression` over the inner `{ 1 }` of `new List { { 1 } }` -- the
// `bracesAreOptional` gate is true (a single non-assignment element whose enclosing initializer is
// the `Initializer` slot of an `ObjectCreateExpression`), so the nested braces are omitted and only
// the single element renders. The full parent chain (`inner` is an `Elements` child of `outer`,
// which is the `Initializer` of `objCreate`) is built so `IsObjectOrCollectionInitializer` sees
// the outer initializer's slot kind.
TEST(CSharp_OutputVisitor, VisitArrayInitializerExpressionBracesOptional) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("List"));
	auto prim1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto inner = std::make_unique<ArrayInitializerExpression>();
	inner->Elements().Add(prim1.get());
	auto outer = std::make_unique<ArrayInitializerExpression>();
	outer->Elements().Add(inner.get());
	auto objCreate = std::make_unique<ObjectCreateExpression>(type.get());
	objCreate->Initializer(outer.get());
	h.visitor->VisitArrayInitializerExpression(inner.get());
	// start(inner), start(PrimitiveExpression), primval, end, end(inner) -- the nested braces are
	// optional so only the single element renders.
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitVariableInitializer` over `x` (a name with no initializer) -- the `NameToken` identifier is
// written as a flat token (no `StartNode`/`EndNode`), and the nullable `Initializer` is absent so
// the `=` clause is skipped.
TEST(CSharp_OutputVisitor, VisitVariableInitializer) {
	V h;
	auto node = std::make_unique<VariableInitializer>(std::string("x"));
	h.visitor->VisitVariableInitializer(node.get());
	// start, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:x");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitVariableInitializer` over `x = 1` -- the default `SpaceAroundAssignment=false` omits the
// spaces around the `=` token, and the `Initializer` recurses through `VisitPrimitiveExpression`.
TEST(CSharp_OutputVisitor, VisitVariableInitializerWithInitializer) {
	V h;
	auto init = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<VariableInitializer>(std::string("x"), init.get());
	h.visitor->VisitVariableInitializer(node.get());
	// start, id:x, tok:=, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:x");
	EXPECT_EQ(h.inner.calls[2], "tok:=");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitOutVarDeclarationExpression` over `out int x` -- the `out` keyword, an explicit space, the
// `Type` recursing through `VisitSimpleType`, another explicit space, and the `Variable` recursing
// through `VisitVariableInitializer` (the name only, no initializer).
TEST(CSharp_OutputVisitor, VisitOutVarDeclarationExpression) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto variable = std::make_unique<VariableInitializer>(std::string("x"));
	auto node = std::make_unique<OutVarDeclarationExpression>(type.get(), variable.get());
	h.visitor->VisitOutVarDeclarationExpression(node.get());
	// start, kw:out, space, start(SimpleType), id:int, end, space, start(VariableInitializer),
	// id:x, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:out");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:x");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitWithInitializerExpression` over `null with { }` -- the `Expression` recurses through
// `VisitNullReferenceExpression`, the `with` keyword follows, and the empty `Initializer`
// `ArrayInitializerExpression` renders its braces (the `bracesAreOptional` gate is false since the
// enclosing slot is the `WithInitializerExpression`, not an object/collection initializer). The
// `OpenBrace` inserts a space before `{` (the line is not empty), and `PrintInitializerElements`
// inserts another space before the closing `}` (the `if (!wrap) Space()` before the empty list).
TEST(CSharp_OutputVisitor, VisitWithInitializerExpression) {
	V h;
	auto expr = std::make_unique<NullReferenceExpression>();
	auto initializer = std::make_unique<ArrayInitializerExpression>();
	auto node = std::make_unique<WithInitializerExpression>(expr.get(), initializer.get());
	h.visitor->VisitWithInitializerExpression(node.get());
	// start, start(NullRef), primval, end, kw:with, start(ArrayInit), space, tok:{, space, tok:},
	// end, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "kw:with");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "tok:}");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitUndocumentedExpression` over `__arglist()` (the `ArgList` case with empty `Arguments`) --
// the `__arglist` keyword, no space (the default `SpaceBeforeMethodCallParentheses=false`), and the
// empty argument list in parentheses.
TEST(CSharp_OutputVisitor, VisitUndocumentedExpressionArgListEmpty) {
	V h;
	auto node = std::make_unique<UndocumentedExpression>(UndocumentedExpressionType::ArgList);
	h.visitor->VisitUndocumentedExpression(node.get());
	// start, kw:__arglist, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:__arglist");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:)");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitUndocumentedExpression` over `__arglist` (the `ArgListAccess` case) -- the `ArgListAccess`
// branch writes only the keyword and skips the argument-list parentheses (the C# `!= ArgListAccess`
// gate).
TEST(CSharp_OutputVisitor, VisitUndocumentedExpressionArgListAccess) {
	V h;
	auto node = std::make_unique<UndocumentedExpression>();
	h.visitor->VisitUndocumentedExpression(node.get());
	// start, kw:__arglist, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:__arglist");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitStackAllocExpression` over `stackalloc int[1]` (a `Type`, a `CountExpression`, no
// `Initializer`) -- the `stackalloc` keyword, the `Type` recursing through `VisitSimpleType` (the
// `InsertRequiredSpacesDecorator` inserts a space before `id:int` since `int` is a keyword), and the
// single-element `CountExpression` in brackets (the default `SpacesWithinBrackets=false` omits the
// inner spaces).
TEST(CSharp_OutputVisitor, VisitStackAllocExpression) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto count = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto node = std::make_unique<StackAllocExpression>();
	node->Type(type.get());
	node->CountExpression(count.get());
	h.visitor->VisitStackAllocExpression(node.get());
	// start, kw:stackalloc, start(SimpleType), space, id:int, end, tok:[, start, primval, end, tok:], end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:stackalloc");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:[");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:]");
	EXPECT_EQ(h.inner.calls[11], "end");
}


// `VisitErrorExpression` over a default-constructed node -- the C# `VisitErrorNode` is a
// `StartNode`/`EndNode` pair with no body (a leaf placeholder with no [Slot] children); the
// `InsertMissingTokensDecorator` records the span onto `ErrorExpression::Location` from
// `StartNode`'s `ILocatable` position.
TEST(CSharp_OutputVisitor, VisitErrorExpression) {
	V h;
	auto node = std::make_unique<ErrorExpression>();
	h.visitor->VisitErrorExpression(node.get());
	// start, end
	ASSERT_EQ(h.inner.calls.size(), 2u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "end");
}

// `VisitSingleVariableDesignation` over `x` -- the `StartNode`, the `IdentifierToken` rendered as
// a flat identifier (no nested StartNode/EndNode, the `VisitIdentifier` ITokenWriter contract),
// and the `EndNode`.
TEST(CSharp_OutputVisitor, VisitSingleVariableDesignation) {
	V h;
	auto node = std::make_unique<SingleVariableDesignation>(std::string("x"));
	h.visitor->VisitSingleVariableDesignation(node.get());
	// start, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 3u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "id:x");
	EXPECT_EQ(h.inner.calls[2], "end");
}

// `VisitParenthesizedVariableDesignation` over `()` -- the `LPar`, an empty comma list (no inner
// tokens), and the `RPar`.
TEST(CSharp_OutputVisitor, VisitParenthesizedVariableDesignationEmpty) {
	V h;
	auto node = std::make_unique<ParenthesizedVariableDesignation>();
	h.visitor->VisitParenthesizedVariableDesignation(node.get());
	// start, tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "tok:)");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitParenthesizedVariableDesignation` over `(x, y)` -- the two `SingleVariableDesignation`
// children recurse through `VisitSingleVariableDesignation` (the comma-separated list inserts a
// `tok:,` between them; the default policy has no bracket-comma spaces).
TEST(CSharp_OutputVisitor, VisitParenthesizedVariableDesignationTwo) {
	V h;
	auto a = std::make_unique<SingleVariableDesignation>(std::string("x"));
	auto b = std::make_unique<SingleVariableDesignation>(std::string("y"));
	auto node = std::make_unique<ParenthesizedVariableDesignation>();
	node->VariableDesignations().Add(a.get());
	node->VariableDesignations().Add(b.get());
	h.visitor->VisitParenthesizedVariableDesignation(node.get());
	// start, tok:(, start, id:x, end, tok:,, start, id:y, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "id:x");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "tok:,");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "id:y");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "tok:)");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitTupleTypeElement` with a name `int Name` -- the `Type` recurses through
// `VisitPrimitiveType`, then an explicit `Space`, then the `NameToken` recurses through
// `VisitIdentifier` (which is a flat token, no `StartNode`/`EndNode` per the D325 contract).
TEST(CSharp_OutputVisitor, VisitTupleTypeElementNamed) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TupleTypeElement>(type.get(), std::string("Name"));
	h.visitor->VisitTupleTypeElement(node.get());
	// start, start(PrimitiveType), primtype:int, end(PrimitiveType), space, id:Name, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "id:Name");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitTupleTypeElement` without a name `int` -- the nullable `NameToken` is absent, so the
// `Space` + `NameToken` recursion is skipped (the bare `type` element form); the node's own
// `StartNode`/`EndNode` still bracket the `Type` recursion.
TEST(CSharp_OutputVisitor, VisitTupleTypeElementNameless) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<TupleTypeElement>();
	node->Type(type.get());
	h.visitor->VisitTupleTypeElement(node.get());
	// start(TupleTypeElement), start(PrimitiveType), primtype:int, end(PrimitiveType), end(TupleTypeElement)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitTupleType` over `(int, string)` -- the elements in parentheses, comma-separated (the
// default policy has no bracket-comma spaces); each `TupleTypeElement` recurses through
// `VisitTupleTypeElement` (its own `start`/`end` plus the `Type`'s `start`/`primtype`/`end`), so
// the first element `int` contributes 5 entries and the second `string` contributes 5.
TEST(CSharp_OutputVisitor, VisitTupleType) {
	V h;
	auto intType = std::make_unique<PrimitiveType>(std::string("int"));
	auto e0 = std::make_unique<TupleTypeElement>();
	e0->Type(intType.get());
	auto strType = std::make_unique<SimpleType>(std::string("string"));
	auto e1 = std::make_unique<TupleTypeElement>();
	e1->Type(strType.get());
	auto node = std::make_unique<TupleAstType>();
	node->Elements().Add(e0.get());
	node->Elements().Add(e1.get());
	h.visitor->VisitTupleType(node.get());
	// start, tok:(, start, start, primtype:int, end, end, tok:,, start, start, id:string, end, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "tok:,");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "id:string");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "tok:)");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `VisitInvocationType` over `Foo()` -- the `BaseType` recurses through `VisitSimpleType`, then
// the empty `Arguments` list renders just the parens (the C# uses `WriteToken(Tokens.LPar/RPar)`
// rather than the `LPar()`/`RPar()` helpers).
TEST(CSharp_OutputVisitor, VisitInvocationTypeNoArgs) {
	V h;
	auto baseType = std::make_unique<SimpleType>(std::string("Foo"));
	auto node = std::make_unique<InvocationAstType>();
	node->BaseType(baseType.get());
	h.visitor->VisitInvocationType(node.get());
	// start, start(SimpleType), id:Foo, end(SimpleType), tok:(, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitInvocationType` over `Foo(null)` -- a single `Arguments` element recurses through
// `VisitNullReferenceExpression` (a `start/primval/end` sub-trace) between the parens.
TEST(CSharp_OutputVisitor, VisitInvocationTypeOneArg) {
	V h;
	auto baseType = std::make_unique<SimpleType>(std::string("Foo"));
	auto arg = std::make_unique<NullReferenceExpression>();
	auto node = std::make_unique<InvocationAstType>();
	node->BaseType(baseType.get());
	node->Arguments().Add(arg.get());
	h.visitor->VisitInvocationType(node.get());
	// start, start(SimpleType), id:Foo, end, tok:(, start, primval, end, tok:), end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:Foo");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitFunctionPointerType` over `delegate*<int>` -- no unmanaged calling convention, no
// calling-convention list, and an empty `Parameters` (so the angle-bracket list is just the
// `ReturnType` `int`); the `delegate` keyword and the `*` pointer token have no space between
// them (the decorator does not insert one for a keyword followed by `*`).
TEST(CSharp_OutputVisitor, VisitFunctionPointerTypeBare) {
	V h;
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<FunctionPointerAstType>();
	node->ReturnType(retType.get());
	h.visitor->VisitFunctionPointerType(node.get());
	// start, kw:delegate, tok:*, tok:<, start, primtype:int, end, tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:delegate");
	EXPECT_EQ(h.inner.calls[2], "tok:*");
	EXPECT_EQ(h.inner.calls[3], "tok:<");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "primtype:int");
	EXPECT_EQ(h.inner.calls[6], "end");
	EXPECT_EQ(h.inner.calls[7], "tok:>");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitFunctionPointerType` over `delegate* unmanaged[Cdecl]<int>` -- the
// `HasUnmanagedCallingConvention` gate inserts a `Space` + the `unmanaged` keyword, the
// `CallingConventions` list renders the `[Cdecl]` bracket group (the convention is a `SimpleType`
// recursing through `VisitSimpleType`), and the empty `Parameters` leaves the `ReturnType` `int`
// as the sole angle-bracket element.
TEST(CSharp_OutputVisitor, VisitFunctionPointerTypeUnmanagedConvention) {
	V h;
	auto conv = std::make_unique<SimpleType>(std::string("Cdecl"));
	auto retType = std::make_unique<PrimitiveType>(std::string("int"));
	auto node = std::make_unique<FunctionPointerAstType>();
	node->HasUnmanagedCallingConvention(true);
	node->CallingConventions().Add(conv.get());
	node->ReturnType(retType.get());
	h.visitor->VisitFunctionPointerType(node.get());
	// start, kw:delegate, tok:*, space, kw:unmanaged, tok:[, start, id:Cdecl, end, tok:], tok:<,
	// start, primtype:int, end, tok:>, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:delegate");
	EXPECT_EQ(h.inner.calls[2], "tok:*");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "kw:unmanaged");
	EXPECT_EQ(h.inner.calls[5], "tok:[");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "id:Cdecl");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "tok:]");
	EXPECT_EQ(h.inner.calls[10], "tok:<");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "primtype:int");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "tok:>");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// ---- VisitDeclarationExpression / VisitAnonymousTypeCreateExpression /
//      VisitLambdaExpression / VisitAnonymousMethodExpression ----------------

// `VisitDeclarationExpression` over `int x` (the `T x` declaration form) -- the `Type` recurses
// through `VisitPrimitiveType` (start/primtype:int/end), an explicit `Space`, then the
// `Designation` recurses through `VisitSingleVariableDesignation` (start/id:x/end).
TEST(CSharp_OutputVisitor, VisitDeclarationExpression) {
	V h;
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto designation = std::make_unique<SingleVariableDesignation>(std::string("x"));
	auto node = std::make_unique<DeclarationExpression>(type.get(), designation.get());
	h.visitor->VisitDeclarationExpression(node.get());
	// start, start(PrimitiveType), primtype:int, end, space, start(designation), id:x, end, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitDeclarationExpression` over `var (a, b)` (a deconstruction) -- the `Type` recurses through
// `VisitSimpleType` (start/id:var/end), an explicit `Space`, then the `ParenthesizedVariableDesignation`
// recurses through `VisitParenthesizedVariableDesignation` rendering `(a, b)`.
TEST(CSharp_OutputVisitor, VisitDeclarationExpressionVarDeconstruction) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("var"));
	auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
	auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
	auto designation = std::make_unique<ParenthesizedVariableDesignation>();
	designation->VariableDesignations().Add(a.get());
	designation->VariableDesignations().Add(b.get());
	auto node = std::make_unique<DeclarationExpression>(type.get(), designation.get());
	h.visitor->VisitDeclarationExpression(node.get());
	// start, start(SimpleType), id:var, end, space, start(ParenDesig), tok:(, start, id:a, end,
	// tok:,, start, id:b, end, tok:), end, end
	ASSERT_EQ(h.inner.calls.size(), 17u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:var");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "tok:(");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:a");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:,");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "id:b");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "tok:)");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "end");
}

// `VisitAnonymousTypeCreateExpression` over `new {}` (empty initializers) -- `new` then the
// initializer braces directly over the (empty) `Initializers` collection (no type, no nested
// `ArrayInitializerExpression` node). `PrintInitializerElements` emits the OpenBrace space (the
// line is not empty after `kw:new`), one inner space, and the close brace.
TEST(CSharp_OutputVisitor, VisitAnonymousTypeCreateExpressionEmpty) {
	V h;
	auto node = std::make_unique<AnonymousTypeCreateExpression>();
	h.visitor->VisitAnonymousTypeCreateExpression(node.get());
	// start, kw:new, space, tok:{, space, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:{");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:}");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitAnonymousTypeCreateExpression` over `new { 1, 2 }` (two simple primitive initializers) --
// `PrintInitializerElements` does not wrap (two simple primitives), so the braces hug the list
// with inner spaces and a bare comma between the elements.
TEST(CSharp_OutputVisitor, VisitAnonymousTypeCreateExpressionTwoElements) {
	V h;
	auto e1 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto e2 = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	auto node = std::make_unique<AnonymousTypeCreateExpression>();
	node->Initializers().Add(e1.get());
	node->Initializers().Add(e2.get());
	h.visitor->VisitAnonymousTypeCreateExpression(node.get());
	// start, kw:new, space, tok:{, space, start, primval, end, tok:,, space, start, primval, end,
	// space, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:new");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:{");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:,");
	EXPECT_EQ(h.inner.calls[9], "space");
	EXPECT_EQ(h.inner.calls[10], "start");
	EXPECT_EQ(h.inner.calls[11], "primval");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "space");
	EXPECT_EQ(h.inner.calls[14], "tok:}");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitLambdaExpression` over `() => null` (empty parameters, expression body) -- the empty
// parameter list is parenthesized (`LambdaNeedsParenthesis` returns true for zero parameters),
// then the `=>` arrow and the expression body (a `NullReferenceExpression`).
TEST(CSharp_OutputVisitor, VisitLambdaExpressionExpressionBody) {
	V h;
	auto body = std::make_unique<NullReferenceExpression>();
	auto node = std::make_unique<LambdaExpression>();
	node->Body(body.get());
	h.visitor->VisitLambdaExpression(node.get());
	// start, tok:(, tok:), space, tok:=>, space, start(NullRef), primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 10u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "tok:)");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:=>");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "primval");
	EXPECT_EQ(h.inner.calls[8], "end");
	EXPECT_EQ(h.inner.calls[9], "end");
}

// `VisitLambdaExpression` over `async () => null` (async, empty parameters, expression body) --
// the `async` modifier keyword plus an explicit `Space` precede the parenthesized parameter list.
TEST(CSharp_OutputVisitor, VisitLambdaExpressionAsync) {
	V h;
	auto body = std::make_unique<NullReferenceExpression>();
	auto node = std::make_unique<LambdaExpression>();
	node->IsAsync(true);
	node->Body(body.get());
	h.visitor->VisitLambdaExpression(node.get());
	// start, kw:async, space, tok:(, tok:), space, tok:=>, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:async");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "tok:(");
	EXPECT_EQ(h.inner.calls[4], "tok:)");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "tok:=>");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "primval");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}
// `VisitLambdaExpression` over `() => { }` (empty parameters, block body) -- the body is a
// `BlockStatement`, so it recurses through `WriteBlock` (no `Space` before the block, the
// `OpenBrace` inserts its own space since the line is not empty after `=>`).
TEST(CSharp_OutputVisitor, VisitLambdaExpressionBlockBody) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<LambdaExpression>();
	node->Body(body.get());
	h.visitor->VisitLambdaExpression(node.get());
	// start, tok:(, tok:), space, tok:=>, start(block), space, tok:{, indent, newline,
	// unindent, tok:}, end, end (the `WriteBlock` body's `OpenBrace` defaults `newLine=true`, so it
	// emits `indent`+`newline` after `{`, and `CloseBrace` defaults `unindent=true`, so it emits
	// `unindent` before `}`).
	ASSERT_EQ(h.inner.calls.size(), 14u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "tok:(");
	EXPECT_EQ(h.inner.calls[2], "tok:)");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:=>");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "unindent");
	EXPECT_EQ(h.inner.calls[11], "tok:}");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "end");
}

// `VisitAnonymousMethodExpression` over `delegate {}` (empty parameters, empty block body) --
// the `delegate` keyword, the parameter list omitted entirely (empty `Parameters`), then the
// `WriteBlock` body.
TEST(CSharp_OutputVisitor, VisitAnonymousMethodExpression) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<AnonymousMethodExpression>();
	node->Body(body.get());
	h.visitor->VisitAnonymousMethodExpression(node.get());
	// start, kw:delegate, start(block), space, tok:{, indent, newline, unindent, tok:}, end, end
	// (the `WriteBlock` body's `OpenBrace` defaults `newLine=true` -> `indent`+`newline`, and
	// `CloseBrace` defaults `unindent=true` -> `unindent`).
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:delegate");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitAnonymousMethodExpression` over `async delegate {}` (async, empty parameters, empty block
// body) -- the `async` modifier keyword plus an explicit `Space` precede the `delegate` keyword.
TEST(CSharp_OutputVisitor, VisitAnonymousMethodExpressionAsync) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto node = std::make_unique<AnonymousMethodExpression>();
	node->IsAsync(true);
	node->Body(body.get());
	h.visitor->VisitAnonymousMethodExpression(node.get());
	// start, kw:async, space, kw:delegate, start(block), space, tok:{, indent, newline, unindent,
	// tok:}, end, end (the `WriteBlock` body's `OpenBrace`/`CloseBrace` default `newLine=true`/
	// `unindent=true`).
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:async");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:delegate");
	EXPECT_EQ(h.inner.calls[4], "start");
	EXPECT_EQ(h.inner.calls[5], "space");
	EXPECT_EQ(h.inner.calls[6], "tok:{");
	EXPECT_EQ(h.inner.calls[7], "indent");
	EXPECT_EQ(h.inner.calls[8], "newline");
	EXPECT_EQ(h.inner.calls[9], "unindent");
	EXPECT_EQ(h.inner.calls[10], "tok:}");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// `VisitParameterDeclaration` over `int x` (a plain value parameter, no modifiers) -- the `Type`
// recurses through `VisitSimpleType` (start/id:int/end), a `Space` separates the type from the
// name (the `Type is not null && Name` non-empty gate), and the `NameToken` identifier follows.
// `WriteAttributes` over the empty `Attributes` collection records nothing.
TEST(CSharp_OutputVisitor, VisitParameterDeclarationPlain) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, start(SimpleType), id:int, end, space, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 7u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "id:x");
	EXPECT_EQ(h.inner.calls[6], "end");
}

// `VisitParameterDeclaration` over `out int x` (the `Out` `ParameterModifier` branch) -- the
// `out` keyword + an explicit `Space` precede the type/name (the `ReferenceKind.Out` switch
// case).
TEST(CSharp_OutputVisitor, VisitParameterDeclarationOut) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->ParameterModifier(ReferenceKind::Out);
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, kw:out, space, start, id:int, end, space, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:out");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "id:x");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// `VisitParameterDeclaration` over `ref readonly int x` (the `RefReadOnly` branch) -- the `ref`
// keyword, a decorator-inserted space (two consecutive `WriteKeyword` calls), the `readonly`
// keyword, an explicit `Space`, then the type/name. The `InsertRequiredSpacesDecorator` inserts
// the space between `ref` and `readonly` (its `WriteKeyword` override: `lastWritten ==
// KeywordOrIdentifier` -> `Space()`).
TEST(CSharp_OutputVisitor, VisitParameterDeclarationRefReadOnly) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->ParameterModifier(ReferenceKind::RefReadOnly);
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, kw:ref, space(decorator), kw:readonly, space(explicit), start, id:int, end, space,
	// id:x, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:ref");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:readonly");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:int");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "space");
	EXPECT_EQ(h.inner.calls[9], "id:x");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitParameterDeclaration` over `int x = 5` (a `DefaultExpression`) -- the type/name render,
// then the default (`= expr`) with the default `SpaceAroundAssignment=false` policy so no spaces
// around `=`, and the `DefaultExpression` recurses through `VisitPrimitiveExpression`.
TEST(CSharp_OutputVisitor, VisitParameterDeclarationWithDefault) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto defaultExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(5)));
	auto node = std::make_unique<ParameterDeclaration>();
	node->Type(type.get());
	node->Name("x");
	node->DefaultExpression(defaultExpr.get());
	h.visitor->VisitParameterDeclaration(node.get());
	// start, start(SimpleType), id:int, end, space, id:x, tok:=, start(Primitive), primval, end,
	// end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "id:x");
	EXPECT_EQ(h.inner.calls[6], "tok:=");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitParameterDeclaration` over a nameless `int` (a `Type` with no `Name`) -- the type
// recurses, but the `Type is not null && Name` non-empty gate is false (the name is empty) so
// no `Space` and no `WriteIdentifier`; the parameter renders just the type.
TEST(CSharp_OutputVisitor, VisitParameterDeclarationNameless) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->Type(type.get());
	h.visitor->VisitParameterDeclaration(node.get());
	// start, start(SimpleType), id:int, end, end (no name -> no space, no identifier)
	ASSERT_EQ(h.inner.calls.size(), 5u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "id:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "end");
}

// `VisitParameterDeclaration` over `this int x` (an extension-method `this` parameter) -- the
// `this` keyword + an explicit `Space` precede the type/name (the `HasThisModifier` branch).
TEST(CSharp_OutputVisitor, VisitParameterDeclarationThisModifier) {
	V h;
	auto type = std::make_unique<SimpleType>(std::string("int"));
	auto node = std::make_unique<ParameterDeclaration>();
	node->HasThisModifier(true);
	node->Type(type.get());
	node->Name("x");
	h.visitor->VisitParameterDeclaration(node.get());
	// start, kw:this, space, start(SimpleType), id:int, end, space, id:x, end
	ASSERT_EQ(h.inner.calls.size(), 9u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:this");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "id:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "id:x");
	EXPECT_EQ(h.inner.calls[8], "end");
}

// ---- The control-flow statement Visit methods (Lock/Using/For/Foreach/Fixed) -------------

// `VisitLockStatement` writes `lock (expr) body`: the `Expression` is a required slot recursed
// inside the parens, the body a required embedded `Statement`. With the default policy (no space
// before/within the lock parens) and a non-block `break` body, the sequence is the keyword, the
// parenthesized expression, then the embedded `break` (newline + indent + the break's own
// sequence + unindent).
TEST(CSharp_OutputVisitor, VisitLockStatement) {
	V h;
	auto node = std::make_unique<LockStatement>();
	auto expr = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->Expression(expr.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitLockStatement(node.get());
	// start, kw:lock, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:lock");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitUsingStatement` writes `using (resource) body` (the non-enhanced, non-async form). The
// `ResourceAcquisition` is a required `AstNode`-typed slot recursed inside the parens; the body a
// required embedded `Statement`. With the default policy and a non-block `break` body, the
// sequence mirrors `VisitLockStatement` (the `using` keyword, the parenthesized resource, the
// embedded `break`).
TEST(CSharp_OutputVisitor, VisitUsingStatement) {
	V h;
	auto node = std::make_unique<UsingStatement>();
	auto resource = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->ResourceAcquisition(resource.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitUsingStatement(node.get());
	// start, kw:using, tok:(, start, primval, end, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 16u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:using");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "newline");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "kw:break");
	EXPECT_EQ(h.inner.calls[11], "tok:;");
	EXPECT_EQ(h.inner.calls[12], "newline");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "end");
}

// `VisitUsingStatement` with `IsAsync` writes the leading `await` keyword before `using`. The
// `InsertRequiredSpacesDecorator` inserts a space between the two consecutive `WriteKeyword`
// calls (`await` then `using`) that the `Visit` method never writes explicitly, so the sequence
// carries a decorator-inserted `space` between `kw:await` and `kw:using`.
TEST(CSharp_OutputVisitor, VisitUsingStatementAsync) {
	V h;
	auto node = std::make_unique<UsingStatement>();
	auto resource = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->IsAsync(true);
	node->ResourceAcquisition(resource.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitUsingStatement(node.get());
	// start, kw:await, space(decorator), kw:using, tok:(, start, primval, end, tok:), newline,
	// indent, start, kw:break, tok:;, newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 18u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:await");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:using");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "indent");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "kw:break");
	EXPECT_EQ(h.inner.calls[13], "tok:;");
	EXPECT_EQ(h.inner.calls[14], "newline");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "unindent");
	EXPECT_EQ(h.inner.calls[17], "end");
}

// `VisitForStatement` writes `for (init; cond; iter) body`. With empty `Initializers`, a null
// `Condition`, and empty `Iterators` (the `for (;;)` form), the two `WriteToken(Semicolon)`
// calls emit two bare `tok:;` tokens (the default policy adds no surrounding spaces), then the
// closing paren and the embedded `break` body.
TEST(CSharp_OutputVisitor, VisitForStatementEmpty) {
	V h;
	auto node = std::make_unique<ForStatement>();
	auto body = std::make_unique<BreakStatement>();
	node->EmbeddedStatement(body.get());
	h.visitor->VisitForStatement(node.get());
	// start, kw:for, tok:(, tok:;, tok:;, tok:), newline, indent, start, kw:break, tok:;,
	// newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:for");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "tok:;");
	EXPECT_EQ(h.inner.calls[4], "tok:;");
	EXPECT_EQ(h.inner.calls[5], "tok:)");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "indent");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "kw:break");
	EXPECT_EQ(h.inner.calls[10], "tok:;");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "end");
	EXPECT_EQ(h.inner.calls[13], "unindent");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `VisitForeachStatement` writes `foreach (T x in e) body`: the `VariableType` recurses, an
// explicit `Space`, the `VariableDesignation` recurses, an explicit `Space`, the `in` keyword,
// an explicit `Space`, the `InExpression` recurses, then the closing paren and the embedded
// body. A `PrimitiveType` type and a `SingleVariableDesignation` designation each contribute their
// own `start`/.../`end` sub-trace.
TEST(CSharp_OutputVisitor, VisitForeachStatement) {
	V h;
	auto node = std::make_unique<ForeachStatement>();
	auto varType = std::make_unique<PrimitiveType>(std::string("int"));
	auto designation = std::make_unique<SingleVariableDesignation>(std::string("x"));
	auto inExpr = std::make_unique<NullReferenceExpression>();
	auto body = std::make_unique<BreakStatement>();
	node->VariableType(varType.get());
	node->VariableDesignation(designation.get());
	node->InExpression(inExpr.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitForeachStatement(node.get());
	// start, kw:foreach, tok:(, start, primtype:int, end, space, start, id:x, end, space, kw:in,
	// space, start, primval, end, tok:), newline, indent, start, kw:break, tok:;, newline, end,
	// unindent, end
	ASSERT_EQ(h.inner.calls.size(), 26u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:foreach");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:x");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "space");
	EXPECT_EQ(h.inner.calls[11], "kw:in");
	EXPECT_EQ(h.inner.calls[12], "space");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "primval");
	EXPECT_EQ(h.inner.calls[15], "end");
	EXPECT_EQ(h.inner.calls[16], "tok:)");
	EXPECT_EQ(h.inner.calls[17], "newline");
	EXPECT_EQ(h.inner.calls[18], "indent");
	EXPECT_EQ(h.inner.calls[19], "start");
	EXPECT_EQ(h.inner.calls[20], "kw:break");
	EXPECT_EQ(h.inner.calls[21], "tok:;");
	EXPECT_EQ(h.inner.calls[22], "newline");
	EXPECT_EQ(h.inner.calls[23], "end");
	EXPECT_EQ(h.inner.calls[24], "unindent");
	EXPECT_EQ(h.inner.calls[25], "end");
}

// `VisitFixedStatement` writes `fixed (T v) body`: the `Type` recurses, an explicit `Space`, the
// comma-separated `Variables` list (a single `VariableInitializer` rendering `start`/`id:x`/
// `end`), then the closing paren and the embedded `break` body. It reuses the `using` paren
// policy fields (`SpaceBeforeUsingParentheses`/`SpacesWithinUsingParentheses`), both default
// `false`, so no surrounding spaces appear.
TEST(CSharp_OutputVisitor, VisitFixedStatement) {
	V h;
	auto node = std::make_unique<FixedStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto var = std::make_unique<VariableInitializer>(std::string("x"));
	auto body = std::make_unique<BreakStatement>();
	node->Type(type.get());
	node->Variables().Add(var.get());
	node->EmbeddedStatement(body.get());
	h.visitor->VisitFixedStatement(node.get());
	// start, kw:fixed, tok:(, start, primtype:int, end, space, start, id:x, end, tok:), newline,
	// indent, start, kw:break, tok:;, newline, end, unindent, end
	ASSERT_EQ(h.inner.calls.size(), 20u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:fixed");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primtype:int");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "id:x");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "tok:)");
	EXPECT_EQ(h.inner.calls[11], "newline");
	EXPECT_EQ(h.inner.calls[12], "indent");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "kw:break");
	EXPECT_EQ(h.inner.calls[15], "tok:;");
	EXPECT_EQ(h.inner.calls[16], "newline");
	EXPECT_EQ(h.inner.calls[17], "end");
	EXPECT_EQ(h.inner.calls[18], "unindent");
	EXPECT_EQ(h.inner.calls[19], "end");
}

// `VisitCaseLabel` for `case 0:` -- the `case` keyword, an explicit `Space`, the case
// `Expression` recursing through `VisitPrimitiveExpression` (its own `start`/`primval`/`end`),
// then the `Colon` token. The `InsertRequiredSpacesDecorator` inserts no spurious spaces: the
// explicit `Space()` after `kw:case` resets `lastWritten` to `Whitespace`, so the primitive value
// needs no leading decorator space.
TEST(CSharp_OutputVisitor, VisitCaseLabel) {
	V h;
	auto node = std::make_unique<CaseLabel>();
	auto expr = std::make_unique<PrimitiveExpression>(PrimitiveValue(int32_t(0)));
	node->Expression(expr.get());
	h.visitor->VisitCaseLabel(node.get());
	// start, kw:case, space, start, primval, end, tok::, end
	ASSERT_EQ(h.inner.calls.size(), 8u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:case");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok::");
	EXPECT_EQ(h.inner.calls[7], "end");
}

// `VisitCaseLabel` for `default:` -- a null `Expression` selects the `DefaultKeyword` branch
// (no `case` keyword, no `Space`), then the `Colon` token. The decorator inserts no space between
// the `default` keyword and the `:` token (`:` is not one of the `+`/`-`/`&`/`?` merge-guards).
TEST(CSharp_OutputVisitor, VisitCaseLabelDefault) {
	V h;
	auto node = std::make_unique<CaseLabel>();
	h.visitor->VisitCaseLabel(node.get());
	// start, kw:default, tok::, end
	ASSERT_EQ(h.inner.calls.size(), 4u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:default");
	EXPECT_EQ(h.inner.calls[2], "tok::");
	EXPECT_EQ(h.inner.calls[3], "end");
}

// `VisitSwitchSection` -- one `default:` label + one `break;` statement (not a `BlockStatement`).
// The labels loop emits the label with no leading `NewLine` (the `first` gate), the `isBlock` test
// is `false` (the single statement is a `BreakStatement`, not a `BlockStatement`), so a `NewLine`
// separates the label from the statement, then the `break` recurses through
// `VisitBreakStatement` (a `Semicolon` emitting `tok:;` + `newline`). The default
// `IndentCaseBody=false` means the `Indent`/`Unindent` around the statements are skipped.
TEST(CSharp_OutputVisitor, VisitSwitchSection) {
	V h;
	auto section = std::make_unique<SwitchSection>();
	auto label = std::make_unique<CaseLabel>();
	auto stmt = std::make_unique<BreakStatement>();
	section->CaseLabels().Add(label.get());
	section->Statements().Add(stmt.get());
	h.visitor->VisitSwitchSection(section.get());
	// start, start, kw:default, tok::, end, newline, start, kw:break, tok:;, newline, end, end
	ASSERT_EQ(h.inner.calls.size(), 12u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "kw:default");
	EXPECT_EQ(h.inner.calls[3], "tok::");
	EXPECT_EQ(h.inner.calls[4], "end");
	EXPECT_EQ(h.inner.calls[5], "newline");
	EXPECT_EQ(h.inner.calls[6], "start");
	EXPECT_EQ(h.inner.calls[7], "kw:break");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
}

// `VisitSwitchStatement` -- a `switch (null) { default: break; }`. The `switch` keyword, the parens
// (default policy adds no surrounding spaces), the governing `Expression` recursing through
// `VisitNullReferenceExpression` (its own `start`/`primval`/`end`), then `OpenBrace` (EndOfLine
// style, default `newLine=true`) inserts a `space` before `{` (the line is not empty after `)`),
// then `{`, `indent`, `newline`; the default `IndentSwitchBody=false` `Unindent`s the body; the
// single section recurses through `VisitSwitchSection`; the `IndentSwitchBody=false` `Indent`
// restores the indent; `CloseBrace` `Unindent`s then emits `}`; a trailing `NewLine`.
TEST(CSharp_OutputVisitor, VisitSwitchStatement) {
	V h;
	auto node = std::make_unique<SwitchStatement>();
	auto governingExpr = std::make_unique<NullReferenceExpression>();
	auto section = std::make_unique<SwitchSection>();
	auto label = std::make_unique<CaseLabel>();
	auto stmt = std::make_unique<BreakStatement>();
	node->Expression(governingExpr.get());
	section->CaseLabels().Add(label.get());
	section->Statements().Add(stmt.get());
	node->SwitchSections().Add(section.get());
	h.visitor->VisitSwitchStatement(node.get());
	// start, kw:switch, tok:(, start, primval, end, tok:), space, tok:{, indent, newline, unindent,
	// start, start, kw:default, tok::, end, newline, start, kw:break, tok:;, newline, end, end,
	// indent, unindent, tok:}, newline, end
	ASSERT_EQ(h.inner.calls.size(), 29u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:switch");
	EXPECT_EQ(h.inner.calls[2], "tok:(");
	EXPECT_EQ(h.inner.calls[3], "start");
	EXPECT_EQ(h.inner.calls[4], "primval");
	EXPECT_EQ(h.inner.calls[5], "end");
	EXPECT_EQ(h.inner.calls[6], "tok:)");
	EXPECT_EQ(h.inner.calls[7], "space");
	EXPECT_EQ(h.inner.calls[8], "tok:{");
	EXPECT_EQ(h.inner.calls[9], "indent");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "unindent");
	EXPECT_EQ(h.inner.calls[12], "start");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "kw:default");
	EXPECT_EQ(h.inner.calls[15], "tok::");
	EXPECT_EQ(h.inner.calls[16], "end");
	EXPECT_EQ(h.inner.calls[17], "newline");
	EXPECT_EQ(h.inner.calls[18], "start");
	EXPECT_EQ(h.inner.calls[19], "kw:break");
	EXPECT_EQ(h.inner.calls[20], "tok:;");
	EXPECT_EQ(h.inner.calls[21], "newline");
	EXPECT_EQ(h.inner.calls[22], "end");
	EXPECT_EQ(h.inner.calls[23], "end");
	EXPECT_EQ(h.inner.calls[24], "indent");
	EXPECT_EQ(h.inner.calls[25], "unindent");
	EXPECT_EQ(h.inner.calls[26], "tok:}");
	EXPECT_EQ(h.inner.calls[27], "newline");
	EXPECT_EQ(h.inner.calls[28], "end");
}

// `VisitSwitchExpressionSection` -- a switch-expression arm `pattern => body`. The `Pattern`
// recurses through `VisitPrimitiveExpression` (its own `start`/`primval`/`end`), then an explicit
// `Space`, the `=>` arrow token, another explicit `Space`, and the `Body` recursing through
// `VisitPrimitiveExpression` (likewise `start`/`primval`/`end`). Both explicit `Space` calls reset
// the decorator's `lastWritten` so no decorator-inserted space precedes the arrow.
TEST(CSharp_OutputVisitor, VisitSwitchExpressionSection) {
	V h;
	auto section = std::make_unique<SwitchExpressionSection>();
	auto pattern = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto body = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	section->Pattern(pattern.get());
	section->Body(body.get());
	h.visitor->VisitSwitchExpressionSection(section.get());
	// start, start, primval, end, space, tok:=>, space, start, primval, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "tok:=>");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "start");
	EXPECT_EQ(h.inner.calls[8], "primval");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitSwitchExpression` -- `expr switch { arm => body, }`. The governing `Expression` recurses
// through `VisitPrimitiveExpression`, then an explicit `Space`, the `switch` keyword, `OpenBrace`
// (EndOfLine array-initializer style, `newLine=true`) inserts a `space` before `{` (the line is not
// empty after `switch`), then `{`, `indent`, `newline`; the single section recurses through
// `VisitSwitchExpressionSection`; `Comma` writes a trailing `tok:,` (C# writes a comma after every
// arm including the last) with no surrounding spaces (the default `SpaceBeforeBracketComma` /
// `SpaceAfterBracketComma` are both false); `NewLine`; `CloseBrace` (EndOfLine) `unindent`s then
// emits `}`.
TEST(CSharp_OutputVisitor, VisitSwitchExpression) {
	V h;
	auto node = std::make_unique<SwitchExpression>();
	auto governingExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	auto section = std::make_unique<SwitchExpressionSection>();
	auto pattern = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(1)));
	auto body = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(2)));
	node->Expression(governingExpr.get());
	section->Pattern(pattern.get());
	section->Body(body.get());
	node->SwitchSections().Add(section.get());
	h.visitor->VisitSwitchExpression(node.get());
	// start, start, primval, end, space, kw:switch, space, tok:{, indent, newline,
	// start, start, primval, end, space, tok:=>, space, start, primval, end, end,
	// tok:,, newline, unindent, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 26u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:switch");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "start");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "primval");
	EXPECT_EQ(h.inner.calls[13], "end");
	EXPECT_EQ(h.inner.calls[14], "space");
	EXPECT_EQ(h.inner.calls[15], "tok:=>");
	EXPECT_EQ(h.inner.calls[16], "space");
	EXPECT_EQ(h.inner.calls[17], "start");
	EXPECT_EQ(h.inner.calls[18], "primval");
	EXPECT_EQ(h.inner.calls[19], "end");
	EXPECT_EQ(h.inner.calls[20], "end");
	EXPECT_EQ(h.inner.calls[21], "tok:,");
	EXPECT_EQ(h.inner.calls[22], "newline");
	EXPECT_EQ(h.inner.calls[23], "unindent");
	EXPECT_EQ(h.inner.calls[24], "tok:}");
	EXPECT_EQ(h.inner.calls[25], "end");
}

// `VisitSwitchExpression` with no sections -- the `foreach` loop body never runs, so no `Comma` /
// `NewLine` per arm; `OpenBrace` and `CloseBrace` still bracket the (empty) arm list.
TEST(CSharp_OutputVisitor, VisitSwitchExpressionEmpty) {
	V h;
	auto node = std::make_unique<SwitchExpression>();
	auto governingExpr = std::make_unique<PrimitiveExpression>(PrimitiveValue(std::int32_t(0)));
	node->Expression(governingExpr.get());
	h.visitor->VisitSwitchExpression(node.get());
	// start, start, primval, end, space, kw:switch, space, tok:{, indent, newline, unindent, tok:}, end
	ASSERT_EQ(h.inner.calls.size(), 13u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primval");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "kw:switch");
	EXPECT_EQ(h.inner.calls[6], "space");
	EXPECT_EQ(h.inner.calls[7], "tok:{");
	EXPECT_EQ(h.inner.calls[8], "indent");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "unindent");
	EXPECT_EQ(h.inner.calls[11], "tok:}");
	EXPECT_EQ(h.inner.calls[12], "end");
}

// ---- The try/catch family ------------------------------------------------

// `VisitCatchClause` on a bare `catch {}` (no `Type`, no `VariableName`, no `Condition`, an
// empty `Body`): `StartNode`, `WriteKeyword(catch)`, then `WriteBlock(Body)` (the `OpenBrace`
// inserts a `Space` since the line is not empty after the keyword) directly (NOT via
// `VisitBlockStatement`, so no trailing `NewLine`), then `EndNode`.
TEST(CSharp_OutputVisitor, VisitCatchClauseBare) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto clause = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, body.get());
	h.visitor->VisitCatchClause(clause.get());
	// start, kw:catch, start, space, tok:{, indent, newline, unindent, tok:}, end, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:catch");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `VisitCatchClause` with a `when` filter and no caught type: `catch when (true) {}`. The
// `Condition` is a `PrimitiveExpression(true)` whose `VisitPrimitiveExpression` contributes its
// own `start`/`primval`/`end`. The `Space(SpaceBeforeIfParentheses=false)` and
// `Space(SpacesWithinIfParentheses=false)` calls are no-ops (default policy).
TEST(CSharp_OutputVisitor, VisitCatchClauseWithFilter) {
	V h;
	auto body = std::make_unique<BlockStatement>();
	auto cond = std::make_unique<PrimitiveExpression>(PrimitiveValue(true));
	auto clause = std::make_unique<CatchClause>(nullptr, std::string(), cond.get(), body.get());
	h.visitor->VisitCatchClause(clause.get());
	// start, kw:catch, space, kw:when, tok:(, start, primval, end, tok:),
	// start, space, tok:{, indent, newline, unindent, tok:}, end, end
	ASSERT_EQ(h.inner.calls.size(), 18u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:catch");
	EXPECT_EQ(h.inner.calls[2], "space");
	EXPECT_EQ(h.inner.calls[3], "kw:when");
	EXPECT_EQ(h.inner.calls[4], "tok:(");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "primval");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:)");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "space");
	EXPECT_EQ(h.inner.calls[11], "tok:{");
	EXPECT_EQ(h.inner.calls[12], "indent");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "unindent");
	EXPECT_EQ(h.inner.calls[15], "tok:}");
	EXPECT_EQ(h.inner.calls[16], "end");
	EXPECT_EQ(h.inner.calls[17], "end");
}

// `VisitTryCatchStatement` with `try {} catch {}` (an empty `TryBlock`, one bare `CatchClause`,
// no `FinallyBlock`). The default `CatchNewLinePlacement` is `DoNotCare` (not `SameLine`), so a
// `NewLine` precedes the catch clause; the catch clause recurses through `VisitCatchClause`; a
// trailing `NewLine` follows before `EndNode`. The catch clause is added to `CatchClauses` so its
// `Parent` is the `TryCatchStatement` (the `StartNode` nesting-order assert).
TEST(CSharp_OutputVisitor, VisitTryCatchStatement) {
	V h;
	auto tryBlock = std::make_unique<BlockStatement>();
	auto tryStmt = std::make_unique<TryCatchStatement>(tryBlock.get());
	auto catchBody = std::make_unique<BlockStatement>();
	auto clause = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, catchBody.get());
	tryStmt->CatchClauses().Add(clause.get());
	h.visitor->VisitTryCatchStatement(tryStmt.get());
	// start, kw:try, start, space, tok:{, indent, newline, unindent, tok:}, end,
	// newline, start, kw:catch, start, space, tok:{, indent, newline, unindent, tok:}, end, end,
	// newline, end
	ASSERT_EQ(h.inner.calls.size(), 24u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "kw:try");
	EXPECT_EQ(h.inner.calls[2], "start");
	EXPECT_EQ(h.inner.calls[3], "space");
	EXPECT_EQ(h.inner.calls[4], "tok:{");
	EXPECT_EQ(h.inner.calls[5], "indent");
	EXPECT_EQ(h.inner.calls[6], "newline");
	EXPECT_EQ(h.inner.calls[7], "unindent");
	EXPECT_EQ(h.inner.calls[8], "tok:}");
	EXPECT_EQ(h.inner.calls[9], "end");
	EXPECT_EQ(h.inner.calls[10], "newline");
	EXPECT_EQ(h.inner.calls[11], "start");
	EXPECT_EQ(h.inner.calls[12], "kw:catch");
	EXPECT_EQ(h.inner.calls[13], "start");
	EXPECT_EQ(h.inner.calls[14], "space");
	EXPECT_EQ(h.inner.calls[15], "tok:{");
	EXPECT_EQ(h.inner.calls[16], "indent");
	EXPECT_EQ(h.inner.calls[17], "newline");
	EXPECT_EQ(h.inner.calls[18], "unindent");
	EXPECT_EQ(h.inner.calls[19], "tok:}");
	EXPECT_EQ(h.inner.calls[20], "end");
	EXPECT_EQ(h.inner.calls[21], "end");
	EXPECT_EQ(h.inner.calls[22], "newline");
	EXPECT_EQ(h.inner.calls[23], "end");
}

// ---- VisitVariableDeclarationStatement -----------------------------------

// `VisitVariableDeclarationStatement` writes `Modifiers Type v1, v2, ...;`: the `WriteModifiers`
// (empty for `None`), the `Type` recursing (a `PrimitiveType` contributes `start`/`primtype`/
// `end`), an explicit `Space`, the comma-separated `Variables` list (a `VariableInitializer`
// contributes `start`/`id:x`/`end`), then `Semicolon` (`tok:;` + `newline`). The default
// `SpaceAroundAssignment`/`SpaceBeforeBracketComma`/`SpaceAfterBracketComma` policies are false,
// so no extra spaces appear; the `InsertRequiredSpacesDecorator` inserts none either (the
// explicit `Space()` resets `lastWritten`, so the identifier after it needs no leading space).
TEST(CSharp_OutputVisitor, VisitVariableDeclarationStatement) {
	V h;
	auto node = std::make_unique<VariableDeclarationStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto var = std::make_unique<VariableInitializer>(std::string("x"));
	node->Type(type.get());
	node->Variables().Add(var.get());
	h.visitor->VisitVariableDeclarationStatement(node.get());
	// start, start, primtype:int, end, space, start, id:x, end, tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 11u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:;");
	EXPECT_EQ(h.inner.calls[9], "newline");
	EXPECT_EQ(h.inner.calls[10], "end");
}

// `int x = 0;` -- the `VariableInitializer` carries a `PrimitiveExpression` initializer: the
// `=` token is written with `SpaceAroundAssignment=false` (no surrounding spaces), and the
// `PrimitiveExpression` recurses through `VisitPrimitiveExpression` (`start`/`primval`/`end`).
TEST(CSharp_OutputVisitor, VisitVariableDeclarationStatementWithInitializer) {
	V h;
	auto node = std::make_unique<VariableDeclarationStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto init = std::make_unique<PrimitiveExpression>(int32_t(0));
	auto var = std::make_unique<VariableInitializer>(std::string("x"), init.get());
	node->Type(type.get());
	node->Variables().Add(var.get());
	h.visitor->VisitVariableDeclarationStatement(node.get());
	// start, start, primtype:int, end, space, start, id:x, tok:=, start, primval, end, end,
	// tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "tok:=");
	EXPECT_EQ(h.inner.calls[8], "start");
	EXPECT_EQ(h.inner.calls[9], "primval");
	EXPECT_EQ(h.inner.calls[10], "end");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "tok:;");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "end");
}

// `int x, y;` -- two `VariableInitializer` elements in the `Variables` collection: the
// `WriteCommaSeparatedList` template emits a `Comma` (just `tok:,` with the default
// `SpaceBeforeBracketComma`/`SpaceAfterBracketComma` both false) between the two elements.
TEST(CSharp_OutputVisitor, VisitVariableDeclarationStatementTwoVariables) {
	V h;
	auto node = std::make_unique<VariableDeclarationStatement>();
	auto type = std::make_unique<PrimitiveType>(std::string("int"));
	auto var1 = std::make_unique<VariableInitializer>(std::string("x"));
	auto var2 = std::make_unique<VariableInitializer>(std::string("y"));
	node->Type(type.get());
	node->Variables().Add(var1.get());
	node->Variables().Add(var2.get());
	h.visitor->VisitVariableDeclarationStatement(node.get());
	// start, start, primtype:int, end, space, start, id:x, end, tok:, start, id:y, end,
	// tok:;, newline, end
	ASSERT_EQ(h.inner.calls.size(), 15u);
	EXPECT_EQ(h.inner.calls[0], "start");
	EXPECT_EQ(h.inner.calls[1], "start");
	EXPECT_EQ(h.inner.calls[2], "primtype:int");
	EXPECT_EQ(h.inner.calls[3], "end");
	EXPECT_EQ(h.inner.calls[4], "space");
	EXPECT_EQ(h.inner.calls[5], "start");
	EXPECT_EQ(h.inner.calls[6], "id:x");
	EXPECT_EQ(h.inner.calls[7], "end");
	EXPECT_EQ(h.inner.calls[8], "tok:,");
	EXPECT_EQ(h.inner.calls[9], "start");
	EXPECT_EQ(h.inner.calls[10], "id:y");
	EXPECT_EQ(h.inner.calls[11], "end");
	EXPECT_EQ(h.inner.calls[12], "tok:;");
	EXPECT_EQ(h.inner.calls[13], "newline");
	EXPECT_EQ(h.inner.calls[14], "end");
}
