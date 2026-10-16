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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the four `TokenWriter` static factory functions (`Create`,
// `CreateWriterThatSetsLocationsInAST`, `InsertRequiredSpaces`,
// `WrapInWriterThatSetsLocationsInAST`) defined in
// `OutputVisitor/TokenWriter.cpp` (the D316-deferred surface, now unblocked by the concrete
// `TextWriterTokenWriter` D319, `InsertRequiredSpacesDecorator` D320, and
// `InsertMissingTokensDecorator` D321 ports). The tests verify the factories compose the
// documented writer+decorator stacks, that the owning `Create` factories own the whole stack
// (a single `std::unique_ptr<TokenWriter>` handle drives the pipeline end-to-end and cleans it
// up), that the non-owning `Insert`/`Wrap` factories wrap a caller-owned writer (the caller keeps
// the inner writer alive after the returned decorator is destroyed), and that
// `WrapInWriterThatSetsLocationsInAST` throws on a writer that does not implement `ILocatable`
// (the `dynamic_cast` cross-cast failure -> `std::logic_error`).

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/InsertMissingTokensDecorator.hpp"
#include "Decompiler/CSharp/OutputVisitor/InsertRequiredSpacesDecorator.hpp"
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"
#include "Decompiler/CSharp/OutputVisitor/TextWriterTokenWriter.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;

namespace {

// A minimal `TokenWriter` that is NOT an `ILocatable` -- used to exercise the
// `WrapInWriterThatSetsLocationsInAST` rejection (a writer that does not provide locations).
// It records each call as a short string so the tests can also assert that the non-owning
// `InsertRequiredSpaces` factory forwarded writes to a caller-owned sink.
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
	void WriteInterpolatedText(std::string_view /*text*/) override {
		calls.push_back("itext");
	}
	void WritePrimitiveValue(const PrimitiveValue& /*value*/,
		LiteralFormat /*format*/) override {
		calls.push_back("pval");
	}
	void Space() override { calls.push_back("space"); }
	void Indent() override { calls.push_back("indent"); }
	void Unindent() override { calls.push_back("unindent"); }
	void NewLine() override { calls.push_back("newline"); }
	void WriteComment(CommentType /*commentType*/, std::string_view /*content*/) override {
		calls.push_back("comment");
	}
	void WritePreProcessorDirective(PreProcessorDirectiveType /*type*/,
		std::optional<std::string_view> /*argument*/) override {
		calls.push_back("pp");
	}
};

}  // namespace

// ---- The owning `Create` factory (the spaces-only stack) ----

// `Create` composes a `TextWriterTokenWriter` wrapped in `InsertRequiredSpacesDecorator`; driving
// the returned handle writes through to the stream, with the decorator inserting the strictly
// required space between a keyword and a following identifier.
TEST(TokenWriterFactories, CreateWritesThroughSpacesOnlyPipeline) {
	std::ostringstream ss;
	auto writer = TokenWriter::Create(&ss);
	ASSERT_NE(writer, nullptr);

	std::unique_ptr<Identifier> id(Identifier::Create("Foo"));
	writer->WriteKeyword("class");
	writer->WriteIdentifier(id.get());

	EXPECT_EQ(ss.str(), "class Foo");
}

// `Create` applies the caller-provided `IndentationString` to the inner `TextWriterTokenWriter`
// (the C# object-initializer `{ IndentationString = indentation }` ports to a post-ctor setter
// call) -- a custom two-space indent shows up on the indented second line.
TEST(TokenWriterFactories, CreateAppliesCustomIndentationString) {
	std::ostringstream ss;
	auto writer = TokenWriter::Create(&ss, "  ");
	ASSERT_NE(writer, nullptr);

	writer->WriteKeyword("class");
	writer->NewLine();
	writer->Indent();
	writer->WriteKeyword("x");

	EXPECT_EQ(ss.str(), "class\r\n  x");
}

// `Create`'s stack has NO `InsertMissingTokensDecorator`, so an identifier's span is NOT recorded
// (it stays empty) -- the discriminator vs the `CreateWriterThatSetsLocationsInAST` stack.
TEST(TokenWriterFactories, CreateDoesNotRecordLocations) {
	std::ostringstream ss;
	auto writer = TokenWriter::Create(&ss);
	ASSERT_NE(writer, nullptr);

	std::unique_ptr<Identifier> id(Identifier::Create("Foo"));
	writer->StartNode(id.get());
	writer->WriteIdentifier(id.get());
	writer->EndNode(id.get());

	EXPECT_TRUE(id->StartLocation().IsEmpty());
}

// `Create` returns a handle that owns the whole two-layer stack; destroying it (by scope exit or
// reset) does not crash and leaves the caller's stream intact (the writer never owned the
// `std::ostream`).
TEST(TokenWriterFactories, CreateOwnsStackAndDestroysCleanly) {
	std::ostringstream ss;
	{
		auto writer = TokenWriter::Create(&ss);
		ASSERT_NE(writer, nullptr);
		writer->WriteKeyword("x");
	}  // destroy the whole stack here
	EXPECT_EQ(ss.str(), "x");
}

// ---- The owning `CreateWriterThatSetsLocationsInAST` factory (the full stack) ----

// `CreateWriterThatSetsLocationsInAST` composes `TextWriterTokenWriter` wrapped in
// `InsertMissingTokensDecorator` then `InsertRequiredSpacesDecorator`; the inner
// `InsertMissingTokensDecorator` records the identifier's start location as the writer prints
// it (the discriminator vs the `Create` stack).
TEST(TokenWriterFactories, CreateWriterThatSetsLocationsInASTRecordsSpans) {
	std::ostringstream ss;
	auto writer = TokenWriter::CreateWriterThatSetsLocationsInAST(&ss);
	ASSERT_NE(writer, nullptr);

	std::unique_ptr<Identifier> id(Identifier::Create("Foo"));
	writer->StartNode(id.get());
	writer->WriteIdentifier(id.get());
	writer->EndNode(id.get());

	EXPECT_FALSE(id->StartLocation().IsEmpty());
}

// `CreateWriterThatSetsLocationsInAST` still drives the text through the whole stack to the
// stream (the spaces decorator and the inner writer all forward).
TEST(TokenWriterFactories, CreateWriterThatSetsLocationsInASTWritesThrough) {
	std::ostringstream ss;
	auto writer = TokenWriter::CreateWriterThatSetsLocationsInAST(&ss);
	ASSERT_NE(writer, nullptr);

	std::unique_ptr<Identifier> id(Identifier::Create("Foo"));
	writer->WriteKeyword("class");
	writer->WriteIdentifier(id.get());

	EXPECT_EQ(ss.str(), "class Foo");
}

// `CreateWriterThatSetsLocationsInAST` owns the whole three-layer stack; destroying the handle
// cleans up every layer without crashing.
TEST(TokenWriterFactories, CreateWriterThatSetsLocationsInASTOwnsStackAndDestroysCleanly) {
	std::ostringstream ss;
	{
		auto writer = TokenWriter::CreateWriterThatSetsLocationsInAST(&ss);
		ASSERT_NE(writer, nullptr);
		writer->WriteKeyword("x");
	}
	EXPECT_EQ(ss.str(), "x");
}

// ---- The non-owning `InsertRequiredSpaces` factory ----

// `InsertRequiredSpaces` wraps a CALLER-OWNED writer; the returned handle owns only the new
// decorator. Writes flow through to the inner writer, and the inner writer is still alive
// (and still owns its stream) after the decorator is destroyed -- the non-owning contract.
TEST(TokenWriterFactories, InsertRequiredSpacesWrapsCallerOwnedWriter) {
	std::ostringstream ss;
	auto inner = std::make_unique<TextWriterTokenWriter>(&ss);
	ASSERT_NE(inner, nullptr);

	std::unique_ptr<TokenWriter> wrapper = TokenWriter::InsertRequiredSpaces(inner.get());
	ASSERT_NE(wrapper, nullptr);

	std::unique_ptr<Identifier> id(Identifier::Create("Foo"));
	wrapper->WriteKeyword("class");
	wrapper->WriteIdentifier(id.get());

	// Destroy the decorator only; the caller-owned inner writer survives.
	wrapper.reset();
	ASSERT_NE(inner, nullptr);
	EXPECT_EQ(ss.str(), "class Foo");
}

// `InsertRequiredSpaces` returns a `std::unique_ptr<TokenWriter>` (the type the CSharpOutputVisitor
// ctor stores) so it is movable into a `std::unique_ptr<TokenWriter>` lvalue.
TEST(TokenWriterFactories, InsertRequiredSpacesReturnsConvertibleHandle) {
	std::ostringstream ss;
	auto inner = std::make_unique<TextWriterTokenWriter>(&ss);
	std::unique_ptr<TokenWriter> writer = TokenWriter::InsertRequiredSpaces(inner.get());
	EXPECT_NE(writer, nullptr);
}

// ---- The non-owning `WrapInWriterThatSetsLocationsInAST` factory ----

// `WrapInWriterThatSetsLocationsInAST` wraps a CALLER-OWNED `ILocatable` writer (the concrete
// `TextWriterTokenWriter` IS-A `ILocatable`); the inner `InsertMissingTokensDecorator` records
// the identifier's span by reading the writer's `ILocatable` subobject.
TEST(TokenWriterFactories, WrapInWriterThatSetsLocationsInASTWrapsLocatableWriter) {
	std::ostringstream ss;
	auto inner = std::make_unique<TextWriterTokenWriter>(&ss);
	ASSERT_NE(inner, nullptr);

	std::unique_ptr<TokenWriter> wrapper = TokenWriter::WrapInWriterThatSetsLocationsInAST(inner.get());
	ASSERT_NE(wrapper, nullptr);

	std::unique_ptr<Identifier> id(Identifier::Create("Foo"));
	wrapper->StartNode(id.get());
	wrapper->WriteIdentifier(id.get());
	wrapper->EndNode(id.get());

	EXPECT_FALSE(id->StartLocation().IsEmpty());

	// Destroy the decorator only; the caller-owned inner writer survives.
	wrapper.reset();
	ASSERT_NE(inner, nullptr);
}

// `WrapInWriterThatSetsLocationsInAST` throws `std::logic_error` (the C#
// `InvalidOperationException`) when the writer does not implement `ILocatable` (the
// `dynamic_cast` cross-cast returns null) -- a `RecordingTokenWriter` (only a `TokenWriter`) is
// the rejected case.
TEST(TokenWriterFactories, WrapInWriterThatSetsLocationsInASTThrowsOnNonLocatableWriter) {
	auto inner = std::make_unique<RecordingTokenWriter>();
	ASSERT_NE(inner, nullptr);

	EXPECT_THROW(
		{
			std::unique_ptr<TokenWriter> wrapper =
				TokenWriter::WrapInWriterThatSetsLocationsInAST(inner.get());
			(void)wrapper;
		},
		std::logic_error);

	// The caller-owned inner writer survives the throw (the factory failed before constructing
	// the decorator).
	ASSERT_NE(inner, nullptr);
	EXPECT_TRUE(inner->calls.empty());
}

// A null writer is rejected by the decorator's null-check (a `std::invalid_argument`, not the
// `ILocatable` `logic_error`) -- `dynamic_cast<ILocatable*>(nullptr)` returns null first, so the
// `logic_error` path is the one that fires.
TEST(TokenWriterFactories, WrapInWriterThatSetsLocationsInASTThrowsOnNullWriter) {
	EXPECT_THROW(
		{
			std::unique_ptr<TokenWriter> wrapper =
				TokenWriter::WrapInWriterThatSetsLocationsInAST(nullptr);
			(void)wrapper;
		},
		std::logic_error);
}

// `InsertRequiredSpaces` with a null writer defers to the `InsertRequiredSpacesDecorator` ctor's
// `DecoratingTokenWriter` base null-check (a `std::invalid_argument`).
TEST(TokenWriterFactories, InsertRequiredSpacesThrowsOnNullWriter) {
	EXPECT_THROW(
		{
			std::unique_ptr<TokenWriter> wrapper = TokenWriter::InsertRequiredSpaces(nullptr);
			(void)wrapper;
		},
		std::invalid_argument);
}
