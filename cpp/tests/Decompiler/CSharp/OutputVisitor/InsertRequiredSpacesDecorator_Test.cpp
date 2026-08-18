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

// Tests for `InsertRequiredSpacesDecorator` (OutputVisitor/InsertRequiredSpacesDecorator.hpp)
// -- the `DecoratingTokenWriter` subclass that inserts the minimal inter-token whitespace so the
// lexer recognizes the tokens the output visitor emits. The decorator tracks the `LastWritten`
// kind and inserts a `Space()` only where the next token would otherwise merge with the previous
// one. The tests drive the decorator over a recording inner `TokenWriter` and assert on the
// recorded call sequence, verifying the space is inserted (or omitted) exactly where the C#
// `InsertRequiredSpacesDecorator` does, across every overridden method. This is the next in-order
// Phase-5 piece of the output stage per the D319 decision-log entry.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "Decompiler/CSharp/OutputVisitor/InsertRequiredSpacesDecorator.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"  // DecimalValue, FromInt32

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::IL::DecimalValue;

namespace {

// A recording `TokenWriter` that appends a short string per call into a vector -- the inner sink
// the decorator wraps. The recorded sequence lets the tests assert exactly where the decorator
// inserted (or omitted) a `Space()`. `WritePrimitiveValue` records just the `LiteralFormat` (the
// value rendering is exercised by the `TextWriterTokenWriter` tests; here only the
// space-insertion ordering matters).
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
	void WritePrimitiveValue(const PrimitiveValue& /*value*/, LiteralFormat format) override {
		calls.push_back("primval:" + FormatName(format));
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
};

// A fresh inner recorder + decorator pair per test.
struct D {
	RecordingTokenWriter inner;
	InsertRequiredSpacesDecorator dec;
	D() : dec(&inner) {}
};

// A `unique_ptr<Identifier>` owning a token created via the public `Identifier::Create` factory
// (the 2-arg ctor is private, D241/D257).
std::unique_ptr<Identifier> MakeId(std::string name) {
	return std::unique_ptr<Identifier>(Identifier::Create(std::move(name)));
}

}  // namespace

// The decorator is a concrete `TokenWriter` and a `DecoratingTokenWriter` subclass.
TEST(CSharp_InsertRequiredSpacesDecorator, IsDecoratingTokenWriter) {
	D h;
	TokenWriter& tw = h.dec;
	(void)tw;
	EXPECT_TRUE((std::is_base_of_v<DecoratingTokenWriter, InsertRequiredSpacesDecorator>));
	EXPECT_FALSE(std::is_abstract_v<InsertRequiredSpacesDecorator>);
}

// The ctor null-check is inherited from `DecoratingTokenWriter` (the C# `ArgumentNullException` ->
// `std::invalid_argument`).
TEST(CSharp_InsertRequiredSpacesDecorator, CtorRejectsNull) {
	EXPECT_THROW({ InsertRequiredSpacesDecorator dec(nullptr); }, std::invalid_argument);
}

// A non-verbatim, non-keyword identifier after a keyword/identifier gets a strictly-required space
// (the `base.Space()` branch); an identifier after whitespace/other gets no space.
TEST(CSharp_InsertRequiredSpacesDecorator, WriteIdentifierInsertsSpaceAfterKeywordOrIdentifier) {
	D h;
	auto id = MakeId("x");  // "x" is not a C# keyword
	h.dec.WriteKeyword("return");   // lastWritten = KeywordOrIdentifier
	h.dec.WriteIdentifier(id.get());  // strictly-required space inserted
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteIdentifierNoSpaceAfterOther) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteToken(";");  // lastWritten = Other
	h.dec.WriteIdentifier(id.get());  // no space
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:;", "id:x"}));
}

// A keyword identifier (e.g. an identifier named "if") after a keyword/identifier gets a
// not-strictly-required space (the override `Space()` branch, via the `IsKeyword` check).
TEST(CSharp_InsertRequiredSpacesDecorator, WriteKeywordIdentifierInsertsSpace) {
	D h;
	auto kw = MakeId("if");  // "if" IS a keyword -> needs the @-prefix, so the verbatim/keyword branch
	h.dec.WriteKeyword("return");
	h.dec.WriteIdentifier(kw.get());
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "id:if"}));
}

// A verbatim identifier (`IsVerbatim` set) also takes the verbatim/keyword branch.
TEST(CSharp_InsertRequiredSpacesDecorator, WriteVerbatimIdentifierInsertsSpace) {
	D h;
	auto id = MakeId("var");
	id->IsVerbatim(true);
	h.dec.WriteKeyword("return");
	h.dec.WriteIdentifier(id.get());
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "id:var"}));
}

// `WriteKeyword` always inserts a space after a keyword or identifier.
TEST(CSharp_InsertRequiredSpacesDecorator, WriteKeywordInsertsSpaceAfterKeywordOrIdentifier) {
	D h;
	h.dec.WriteKeyword("if");    // lastWritten = KeywordOrIdentifier
	h.dec.WriteKeyword("else");  // space inserted
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:if", "space", "kw:else"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteKeywordNoSpaceAfterOther) {
	D h;
	h.dec.WriteToken(";");  // lastWritten = Other
	h.dec.WriteKeyword("if");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:;", "kw:if"}));
}

// `WriteToken` avoids merging two `+` (or `-`/`&`/`?`) tokens and a `*` after a `/`.
TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenInsertsSpaceToAvoidPlusPlus) {
	D h;
	h.dec.WriteToken("+");  // lastWritten = Plus
	h.dec.WriteToken("+");  // space inserted to avoid "++"
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:+", "space", "tok:+"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenInsertsSpaceToAvoidMinusMinus) {
	D h;
	h.dec.WriteToken("-");
	h.dec.WriteToken("-");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:-", "space", "tok:-"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenInsertsSpaceToAvoidAmpersandMerge) {
	D h;
	h.dec.WriteToken("&");
	h.dec.WriteToken("&");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:&", "space", "tok:&"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenInsertsSpaceToAvoidQuestionMerge) {
	D h;
	h.dec.WriteToken("?");
	h.dec.WriteToken("?");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:?", "space", "tok:?"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenInsertsSpaceBeforeStarAfterDivision) {
	D h;
	h.dec.WriteToken("/");  // lastWritten = Division
	h.dec.WriteToken("*");  // space inserted to avoid "/*" comment start
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:/", "space", "tok:*"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenNoSpaceBetweenUnmergeableTokens) {
	D h;
	h.dec.WriteToken(";");  // lastWritten = Other
	h.dec.WriteToken(";");  // no space
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:;", "tok:;"}));
}

// A `+` after a keyword/identifier does NOT insert a space (only the same-operator merge is
// guarded); `+` after `Other` does not either.
TEST(CSharp_InsertRequiredSpacesDecorator, WriteTokenPlusAfterOtherNoSpace) {
	D h;
	h.dec.WriteToken(";");  // Other
	h.dec.WriteToken("+");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:;", "tok:+"}));
}

// `Space()` resets the last-written kind to whitespace, so a following identifier after an
// explicit `Space()` does NOT get a second space.
TEST(CSharp_InsertRequiredSpacesDecorator, SpaceResetsLastWritten) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");  // KeywordOrIdentifier
	h.dec.Space();                  // explicit space -> lastWritten = Whitespace
	h.dec.WriteIdentifier(id.get());  // no extra space (lastWritten is Whitespace, not KeywordOrIdentifier)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "id:x"}));
}

// `NewLine()` resets the last-written kind to whitespace.
TEST(CSharp_InsertRequiredSpacesDecorator, NewLineResetsLastWritten) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");
	h.dec.NewLine();               // lastWritten = Whitespace
	h.dec.WriteIdentifier(id.get());  // no space
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "newline", "id:x"}));
}

// `WriteComment` inserts a space after a division operator (the `1.0 / /*comment*/a` case).
TEST(CSharp_InsertRequiredSpacesDecorator, WriteCommentInsertsSpaceAfterDivision) {
	D h;
	h.dec.WriteToken("/");  // Division
	h.dec.WriteComment(CommentType::SingleLine, "c");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:/", "space", "comment:c"}));
}

// `WriteComment` after a non-division token gets no space.
TEST(CSharp_InsertRequiredSpacesDecorator, WriteCommentNoSpaceAfterOther) {
	D h;
	h.dec.WriteToken(";");  // Other
	h.dec.WriteComment(CommentType::SingleLine, "c");
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"tok:;", "comment:c"}));
}

// `WriteComment` resets the last-written kind to whitespace (a comment followed by an identifier
// needs no space, unlike after a keyword).
TEST(CSharp_InsertRequiredSpacesDecorator, WriteCommentResetsLastWritten) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");
	h.dec.WriteComment(CommentType::SingleLine, "c");  // lastWritten = Whitespace
	h.dec.WriteIdentifier(id.get());                     // no space
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "comment:c", "id:x"}));
}

// `WritePreProcessorDirective` resets the last-written kind to whitespace.
TEST(CSharp_InsertRequiredSpacesDecorator, WritePreProcessorDirectiveResetsLastWritten) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");
	h.dec.WritePreProcessorDirective(PreProcessorDirectiveType::If, std::optional<std::string_view>("DEBUG"));
	h.dec.WriteIdentifier(id.get());  // no space (the directive reset lastWritten to Whitespace)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "pp:DEBUG", "id:x"}));
}

// `WritePrimitiveValue` (int) inserts a space after a keyword/identifier and sets lastWritten to
// KeywordOrIdentifier (so a following identifier is also spaced, avoiding the `42foo` type-suffix
// merge).
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueIntInsertsSpaceAndNeedsTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");  // KeywordOrIdentifier
	h.dec.WritePrimitiveValue(PrimitiveValue(std::int32_t(42)), LiteralFormat::DecimalNumber);
	h.dec.WriteIdentifier(id.get());  // space (the int left lastWritten = KeywordOrIdentifier)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "primval:DecimalNumber", "space", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueNoSpaceAfterWhitespace) {
	D h;
	h.dec.NewLine();  // Whitespace
	h.dec.WritePrimitiveValue(PrimitiveValue(std::int32_t(42)), LiteralFormat::DecimalNumber);
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:DecimalNumber"}));
}

// A double leaves lastWritten = KeywordOrIdentifier (the type-suffix avoidance), so a following
// identifier is spaced.
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueDoubleNeedsTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();  // Whitespace (no leading space)
	h.dec.WritePrimitiveValue(PrimitiveValue(1.5), LiteralFormat::None);
	h.dec.WriteIdentifier(id.get());  // space (double left lastWritten = KeywordOrIdentifier)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:None", "space", "id:x"}));
}

// A string in the verbatim-string format leaves lastWritten = KeywordOrIdentifier; a non-verbatim
// string leaves it = Other (so a following identifier is not spaced).
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueVerbatimStringNeedsTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveValue(PrimitiveValue(std::string("s")), LiteralFormat::VerbatimStringLiteral);
	h.dec.WriteIdentifier(id.get());  // space (verbatim string left lastWritten = KeywordOrIdentifier)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:VerbatimStringLiteral", "space", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValuePlainStringNoTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveValue(PrimitiveValue(std::string("s")), LiteralFormat::StringLiteral);
	h.dec.WriteIdentifier(id.get());  // no space (plain string left lastWritten = Other)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:StringLiteral", "id:x"}));
}

// A `char`, `decimal`, or finite `float` leaves lastWritten = Other (no trailing space needed).
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueCharNoTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveValue(PrimitiveValue(char16_t('a')), LiteralFormat::CharLiteral);
	h.dec.WriteIdentifier(id.get());  // no space (char left lastWritten = Other)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:CharLiteral", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueDecimalNoTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveValue(PrimitiveValue(DecimalValue::FromInt32(5)), LiteralFormat::DecimalNumber);
	h.dec.WriteIdentifier(id.get());  // no space (decimal left lastWritten = Other)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:DecimalNumber", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueFloatNoTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveValue(PrimitiveValue(1.5f), LiteralFormat::None);
	h.dec.WriteIdentifier(id.get());  // no space (finite float left lastWritten = Other)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primval:None", "id:x"}));
}

// An infinite/NaN float or double returns without setting lastWritten (the special-case literals
// are rendered as their keyword forms, not numeric, so no type-suffix concern).
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueFloatInfinityLeavesLastWritten) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");  // KeywordOrIdentifier
	h.dec.WritePrimitiveValue(PrimitiveValue(std::numeric_limits<float>::infinity()), LiteralFormat::None);
	h.dec.WriteIdentifier(id.get());  // space (the leading Space() from KeywordOrIdentifier; the inf
	                                  // float returned without changing lastWritten, so the explicit Space()
	                                  // left it = Whitespace, and no trailing space is added)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "primval:None", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueDoubleInfinityLeavesLastWritten) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");
	h.dec.WritePrimitiveValue(PrimitiveValue(std::numeric_limits<double>::infinity()), LiteralFormat::None);
	h.dec.WriteIdentifier(id.get());
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "primval:None", "id:x"}));
}

// A `null` (monostate) or `bool` returns without setting lastWritten (no type suffix).
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueNullLeavesLastWrittenWhitespace) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");
	h.dec.WritePrimitiveValue(PrimitiveValue(std::monostate{}), LiteralFormat::None);
	h.dec.WriteIdentifier(id.get());  // no trailing space (null returned with lastWritten = Whitespace)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "primval:None", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveValueBoolLeavesLastWrittenWhitespace) {
	D h;
	auto id = MakeId("x");
	h.dec.WriteKeyword("return");
	h.dec.WritePrimitiveValue(PrimitiveValue(true), LiteralFormat::None);
	h.dec.WriteIdentifier(id.get());  // no trailing space (bool returned with lastWritten = Whitespace)
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "primval:None", "id:x"}));
}

// `WritePrimitiveType` inserts a space after a keyword/identifier; `new` leaves lastWritten = Other
// (the target-typed `new()` form), every other primitive type leaves it = KeywordOrIdentifier.
TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveTypeInsertsSpaceAfterKeywordOrIdentifier) {
	D h;
	h.dec.WriteKeyword("return");  // KeywordOrIdentifier
	h.dec.WritePrimitiveType("int");  // space inserted
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"kw:return", "space", "primtype:int"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveTypeNewLeavesOtherNoTrailingSpace) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveType("new");  // lastWritten = Other
	h.dec.WriteIdentifier(id.get());   // no space
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primtype:new", "id:x"}));
}

TEST(CSharp_InsertRequiredSpacesDecorator, WritePrimitiveTypeOtherLeavesKeywordOrIdentifier) {
	D h;
	auto id = MakeId("x");
	h.dec.NewLine();
	h.dec.WritePrimitiveType("int");  // lastWritten = KeywordOrIdentifier
	h.dec.WriteIdentifier(id.get());  // space
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{"newline", "primtype:int", "space", "id:x"}));
}

// An end-to-end snippet exercising the space insertion across a realistic token sequence:
// `return x + y;` -- the `return`/`x` need a space, `x`/`+` need a space (KeywordOrIdentifier then
// Other), `+`/`y` need a space (Plus would not self-merge with `y`, but `y` after Plus: Plus is
// not KeywordOrIdentifier, so no space -- wait, the C# does not space between `+` and `y`), then
// `y`/`;` no space. Verifies the decorator's state machine across mixed token kinds.
TEST(CSharp_InsertRequiredSpacesDecorator, EndToEndReturnXPlusY) {
	D h;
	auto x = MakeId("x");
	auto y = MakeId("y");
	h.dec.WriteKeyword("return");   // kw:return, lastWritten = KeywordOrIdentifier
	h.dec.WriteIdentifier(x.get()); // space (kw->id), id:x, lastWritten = KeywordOrIdentifier
	h.dec.WriteToken("+");          // no space (Plus != KeywordOrIdentifier merge guard), tok:+, lastWritten = Plus
	h.dec.WriteIdentifier(y.get()); // no space (Plus is not KeywordOrIdentifier; the verbatim/keyword and
	                                // else-if branches both check KeywordOrIdentifier, which is false), id:y
	h.dec.WriteToken(";");          // no space (Plus then ";" -> no merge guard), tok:;, lastWritten = Other
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{
		"kw:return", "space", "id:x", "tok:+", "id:y", "tok:;"}));
}

// A snippet exercising the `+` self-merge guard in a sequence: `x++; y;` (a post-increment then a
// separate statement).
TEST(CSharp_InsertRequiredSpacesDecorator, EndToEndPostIncrementThenStatement) {
	D h;
	auto x = MakeId("x");
	auto y = MakeId("y");
	h.dec.WriteIdentifier(x.get());  // id:x, lastWritten = KeywordOrIdentifier
	h.dec.WriteToken("+");           // no space (KeywordOrIdentifier then "+": no merge guard for kw/id -> "+"), tok:+, Plus
	h.dec.WriteToken("+");           // space (Plus -> Plus merge guard), tok:+, Plus
	h.dec.WriteToken(";");            // no space, tok:;, Other
	h.dec.WriteIdentifier(y.get());  // no space (Other != KeywordOrIdentifier), id:y
	h.dec.WriteToken(";");           // tok:;
	EXPECT_EQ(h.inner.calls, (std::vector<std::string>{
		"id:x", "tok:+", "space", "tok:+", "tok:;", "id:y", "tok:;"}));
}
