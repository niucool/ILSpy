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

// Tests for the `TokenWriter` abstract base, the `ILocatable` interface, and the
// `DecoratingTokenWriter` abstract base (OutputVisitor/TokenWriter.hpp) -- the foundation of
// the C# output stage, the first in-order Phase-5 piece of the OutputVisitor/ITextOutput/
// TokenWriter output stage per the D315 decision-log entry (the C# AST node hierarchy is now
// complete). The interface is exercised with a recording `TokenWriter` stub (the role the
// concrete `TextWriterTokenWriter` plays once it lands) and a passthrough decorator (the role
// `InsertRequiredSpacesDecorator`/`InsertMissingTokensDecorator` play), verifying the
// abstract-base shape, the per-method recording, the decorator's forwarding, an intercepting
// override, and the ctor null-check -- the D225 IAstVisitor abstract-interface test precedent
// applied to the output stage.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::NullReferenceExpression;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::TextLocation;

namespace {

// A recording `TokenWriter` that appends a short string per call into a vector -- the role the
// concrete `TextWriterTokenWriter` plays once it lands (it would write to an `std::ostream`
// instead of a vector, but the call sequence is the same).
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
	void WritePrimitiveValue(const PrimitiveValue& value, LiteralFormat format) override {
		std::string repr = "primval:";
		if (std::holds_alternative<std::int32_t>(value)) {
			repr += "int" + std::to_string(std::get<std::int32_t>(value));
		} else if (std::holds_alternative<std::monostate>(value)) {
			repr += "null";
		} else {
			repr += "other";
		}
		repr += ":" + FormatName(format);
		calls.push_back(std::move(repr));
	}
	void Space() override { calls.push_back("space"); }
	void Indent() override { calls.push_back("indent"); }
	void Unindent() override { calls.push_back("unindent"); }
	void NewLine() override { calls.push_back("newline"); }
	void WriteComment(CommentType commentType, std::string_view content) override {
		calls.push_back("comment:" + std::string(content) + ":" + CommentTypeName(commentType));
	}
	void WritePreProcessorDirective(PreProcessorDirectiveType type, std::optional<std::string_view> argument) override {
		calls.push_back("pp:" + PpTypeName(type) + ":" + (argument.has_value() ? std::string(*argument) : std::string("<null>")));
	}

private:
	static std::string FormatName(LiteralFormat format) {
		switch (format) {
		case LiteralFormat::None: return "None";
		case LiteralFormat::DecimalNumber: return "DecimalNumber";
		case LiteralFormat::HexadecimalNumber: return "HexadecimalNumber";
		case LiteralFormat::BinaryNumber: return "BinaryNumber";
		case LiteralFormat::StringLiteral: return "StringLiteral";
		case LiteralFormat::VerbatimStringLiteral: return "VerbatimStringLiteral";
		case LiteralFormat::CharLiteral: return "CharLiteral";
		case LiteralFormat::Utf8Literal: return "Utf8Literal";
		}
		return "?";
	}
	static std::string CommentTypeName(CommentType type) {
		switch (type) {
		case CommentType::SingleLine: return "SingleLine";
		case CommentType::MultiLine: return "MultiLine";
		case CommentType::Documentation: return "Documentation";
		case CommentType::InactiveCode: return "InactiveCode";
		case CommentType::MultiLineDocumentation: return "MultiLineDocumentation";
		}
		return "?";
	}
	static std::string PpTypeName(PreProcessorDirectiveType type) {
		switch (static_cast<int>(type)) {
		case static_cast<int>(PreProcessorDirectiveType::If): return "If";
		case static_cast<int>(PreProcessorDirectiveType::Define): return "Define";
		default: return "Other";
		}
	}
};

// A concrete decorator that overrides nothing -- concrete because `DecoratingTokenWriter`'s
// only pure-virtual is the destructor (which has a definition); all `TokenWriter` methods are
// forwarded. Used to exercise the full forwarding surface through a base `TokenWriter&`.
class PassthroughDecorator : public DecoratingTokenWriter {
public:
	using DecoratingTokenWriter::DecoratingTokenWriter;
};

// A decorator that overrides a single method (`Space`) to count intercepts without forwarding
// -- the shape `InsertRequiredSpacesDecorator` takes (intercept one method, forward the rest).
class CountingDecorator : public DecoratingTokenWriter {
public:
	using DecoratingTokenWriter::DecoratingTokenWriter;
	int spaceCount = 0;
	void Space() override { ++spaceCount; }
};

// A stub `ILocatable` returning a fixed location and length -- the role the concrete
// `TextWriterTokenWriter : ILocatable` plays once it lands.
class StubLocatable : public ILocatable {
public:
	TextLocation Location() const override { return location_; }
	int Length() const override { return length_; }
	TextLocation location_{3, 5};
	int length_ = 7;
};

}  // namespace

// The three output-stage interfaces are abstract bases (the C# `abstract class`/`interface`).
TEST(CSharp_TokenWriter, AbstractBases) {
	EXPECT_TRUE(std::is_abstract_v<TokenWriter>);
	EXPECT_TRUE(std::is_abstract_v<ILocatable>);
	EXPECT_TRUE(std::is_abstract_v<DecoratingTokenWriter>);
	// A concrete writer and a concrete decorator are NOT abstract.
	EXPECT_FALSE(std::is_abstract_v<RecordingTokenWriter>);
	EXPECT_FALSE(std::is_abstract_v<PassthroughDecorator>);
}

// The `DecoratingTokenWriter` ctor rejects a null decorated writer (the C# `ArgumentNullException`
// -> `std::invalid_argument`).
TEST(CSharp_TokenWriter, DecoratingCtorRejectsNull) {
	EXPECT_THROW({ PassthroughDecorator dec(nullptr); }, std::invalid_argument);
}

// Each `TokenWriter` method records the right entry when called through the concrete writer.
TEST(CSharp_TokenWriter, RecordingWriterRecordsEachMethod) {
	RecordingTokenWriter w;
	auto node = std::make_unique<NullReferenceExpression>();
	auto ident = std::unique_ptr<Identifier>(Identifier::Create("foo"));

	w.StartNode(node.get());
	w.WriteKeyword("if");
	w.Space();
	w.WriteIdentifier(ident.get());
	w.WriteToken("(");
	w.WritePrimitiveType("int");
	w.WritePrimitiveValue(PrimitiveValue(std::int32_t(42)), LiteralFormat::DecimalNumber);
	w.WritePrimitiveValue(PrimitiveValue(std::monostate{}), LiteralFormat::None);  // the C# default `LiteralFormat.None` (passed explicitly: the default on the base pure-virtual applies only through a `TokenWriter` base reference, not the concrete writer type)
	w.WriteInterpolatedText("hello");
	w.Indent();
	w.NewLine();
	w.Unindent();
	w.WriteComment(CommentType::SingleLine, "note");
	w.WritePreProcessorDirective(PreProcessorDirectiveType::If, std::optional<std::string_view>("DEBUG"));
	w.WritePreProcessorDirective(PreProcessorDirectiveType::Define, std::nullopt);
	w.EndNode(node.get());

	const std::vector<std::string> expected = {
		"start",
		"kw:if",
		"space",
		"id:foo",
		"tok:(",
		"primtype:int",
		"primval:int42:DecimalNumber",
		"primval:null:None",
		"interp:hello",
		"indent",
		"newline",
		"unindent",
		"comment:note:SingleLine",
		"pp:If:DEBUG",
		"pp:Define:<null>",
		"end",
	};
	EXPECT_EQ(w.calls, expected);
}

// The default `LiteralFormat::None` argument on `WritePrimitiveValue` applies when called
// through a `TokenWriter&` (the output visitor's call shape -- the default is resolved at the
// static call-site type).
TEST(CSharp_TokenWriter, WritePrimitiveValueDefaultFormat) {
	RecordingTokenWriter w;
	TokenWriter& base = w;
	base.WritePrimitiveValue(PrimitiveValue(std::int32_t(7)));  // no format -> None
	ASSERT_EQ(w.calls.size(), 1u);
	EXPECT_EQ(w.calls[0], "primval:int7:None");
}

// The passthrough decorator forwards every method to the wrapped writer (exercised through a
// base `TokenWriter&`, the output visitor's call shape).
TEST(CSharp_TokenWriter, PassthroughDecoratorForwardsAll) {
	RecordingTokenWriter inner;
	PassthroughDecorator dec(&inner);  // wraps `inner`
	TokenWriter& base = dec;           // drive it through the base reference

	auto node = std::make_unique<NullReferenceExpression>();
	auto ident = std::unique_ptr<Identifier>(Identifier::Create("bar"));

	base.StartNode(node.get());
	base.WriteKeyword("return");
	base.Space();
	base.WriteIdentifier(ident.get());
	base.WriteToken(";");
	base.WritePrimitiveType("bool");
	base.WritePrimitiveValue(PrimitiveValue(std::int32_t(1)), LiteralFormat::DecimalNumber);
	base.WriteInterpolatedText("x");
	base.Indent();
	base.NewLine();
	base.Unindent();
	base.WriteComment(CommentType::Documentation, "doc");
	base.WritePreProcessorDirective(PreProcessorDirectiveType::If, std::optional<std::string_view>("NET"));
	base.EndNode(node.get());

	const std::vector<std::string> expected = {
		"start", "kw:return", "space", "id:bar", "tok:;", "primtype:bool",
		"primval:int1:DecimalNumber", "interp:x", "indent", "newline", "unindent",
		"comment:doc:Documentation", "pp:If:NET", "end",
	};
	EXPECT_EQ(inner.calls, expected);
}

// A decorator that overrides a single method intercepts it (does NOT forward) while still
// forwarding every other method -- the `InsertRequiredSpacesDecorator` shape.
TEST(CSharp_TokenWriter, InterceptingDecoratorOverridesOneMethod) {
	RecordingTokenWriter inner;
	CountingDecorator dec(&inner);
	TokenWriter& base = dec;

	auto node = std::make_unique<NullReferenceExpression>();
	base.StartNode(node.get());
	base.Space();       // intercepted: counter++, NOT forwarded to `inner`
	base.Space();       // intercepted again
	base.WriteToken("+");  // forwarded (not overridden)
	base.EndNode(node.get());

	EXPECT_EQ(dec.spaceCount, 2);
	// `inner` records everything except the two intercepted `Space` calls.
	const std::vector<std::string> expected = {"start", "tok:+", "end"};
	EXPECT_EQ(inner.calls, expected);
}

// The `ILocatable` interface is exercised through a base reference (the location-setting
// decorator reads `Location`/`Length` off a writer that implements `ILocatable`).
TEST(CSharp_TokenWriter, LocatableInterface) {
	StubLocatable loc;
	ILocatable& base = loc;
	EXPECT_EQ(base.Location(), (TextLocation{3, 5}));
	EXPECT_EQ(base.Length(), 7);
}
