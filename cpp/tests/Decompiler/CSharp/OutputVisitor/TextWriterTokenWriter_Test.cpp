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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the concrete `TextWriterTokenWriter` (OutputVisitor/TextWriterTokenWriter.hpp) --
// the sink the `CSharpOutputVisitor` drives a C# AST tree through to produce the decompiled
// text. The writer renders to an `std::ostringstream` (captured for assertion) and is exercised
// across the whole `TokenWriter` surface: the simple token methods, the comment/preprocessor
// rendering, the `WritePrimitiveValue` literal-rendering switch over the `PrimitiveValue`
// variant (every alternative incl. the float/double shortest-round-trip and the
// infinity/NaN/negative-zero special cases), the indentation/location/length tracking, the
// `ILocatable` surface, and the `public static` escape helpers and `PrintPrimitiveValue`. This
// is the next in-order Phase-5 piece of the output stage per the D318 decision-log entry (the
// `IsKeyword` free helper D318 cleared the `WriteIdentifier` tangle).

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Decompiler/CSharp/OutputVisitor/TextWriterTokenWriter.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"  // DecimalValue, FromInt32

using namespace ILSpy::Decompiler::CSharp::OutputVisitor;
using ILSpy::Decompiler::CSharp::Syntax::AstNode;
using ILSpy::Decompiler::CSharp::Syntax::CommentType;
using ILSpy::Decompiler::CSharp::Syntax::Identifier;
using ILSpy::Decompiler::CSharp::Syntax::LiteralFormat;
using ILSpy::Decompiler::CSharp::Syntax::PreProcessorDirectiveType;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;
using ILSpy::Decompiler::CSharp::Syntax::TextLocation;
using ILSpy::Decompiler::IL::DecimalValue;

namespace {

// A fresh writer over a fresh ostringstream, captured by the helper so each test builds a
// small output and asserts on it.
struct W {
	std::ostringstream oss;
	TextWriterTokenWriter w;
	W() : w(&oss) {}
};

}  // namespace

// The ctor null-checks the `std::ostream*` (the C# `ArgumentNullException` ->
// `std::invalid_argument`, the DecoratingTokenWriter D316 precedent).
TEST(CSharp_TextWriterTokenWriter, CtorRejectsNullStream) {
	EXPECT_THROW({ TextWriterTokenWriter w(nullptr); }, std::invalid_argument);
}

// The writer is a `TokenWriter` and an `ILocatable` (the C# `: TokenWriter, ILocatable`).
TEST(CSharp_TextWriterTokenWriter, IsTokenWriterAndILocatable) {
	W h;
	TokenWriter& tw = h.w;
	ILocatable& loc = h.w;
	(void)tw;
	(void)loc;
	EXPECT_FALSE(std::is_abstract_v<TextWriterTokenWriter>);
}

// The default state: 1-based line/column, no indentation, the `\t` indent string, zero length.
TEST(CSharp_TextWriterTokenWriter, DefaultState) {
	W h;
	EXPECT_EQ(h.w.Location(), (TextLocation{1, 1}));
	EXPECT_EQ(h.w.Length(), 0);
	EXPECT_EQ(h.w.Indentation, 0);
	EXPECT_EQ(h.w.IndentationString(), "\t");
}

// WriteKeyword emits the keyword (with indentation) and advances column/length.
TEST(CSharp_TextWriterTokenWriter, WriteKeyword) {
	W h;
	h.w.WriteKeyword("if");
	EXPECT_EQ(h.oss.str(), "if");
	EXPECT_EQ(h.w.Location(), (TextLocation{1, 3}));
	EXPECT_EQ(h.w.Length(), 2);
}

// WriteToken emits a punctuation/operator token.
TEST(CSharp_TextWriterTokenWriter, WriteToken) {
	W h;
	h.w.WriteToken(";");
	EXPECT_EQ(h.oss.str(), ";");
	EXPECT_EQ(h.w.Length(), 1);
}

// Space emits a single space and advances column/length.
TEST(CSharp_TextWriterTokenWriter, Space) {
	W h;
	h.w.WriteKeyword("return");
	h.w.Space();
	EXPECT_EQ(h.oss.str(), "return ");
	EXPECT_EQ(h.w.Length(), 7);
}

// NewLine writes the `\r\n` terminator, resets the column, advances the line, and re-enables
// the pending indentation; Length advances by 2 (the `kNewLine` size).
TEST(CSharp_TextWriterTokenWriter, NewLine) {
	W h;
	h.w.WriteKeyword("x");
	h.w.NewLine();
	EXPECT_EQ(h.oss.str(), "x\r\n");
	EXPECT_EQ(h.w.Location(), (TextLocation{2, 1}));
	EXPECT_EQ(h.w.Length(), 3);  // 1 + 2
}

// Indent/Unindent adjust the indent level; the next token emits the indentation once.
TEST(CSharp_TextWriterTokenWriter, Indentation) {
	W h;
	h.w.Indent();
	h.w.Indent();
	h.w.WriteKeyword("x");
	EXPECT_EQ(h.oss.str(), "\t\tx");
	h.w.Unindent();
	h.w.NewLine();
	h.w.WriteKeyword("y");
	EXPECT_EQ(h.oss.str(), "\t\tx\r\n\ty");
}

// IndentationString is settable and used for the emitted indentation and the Location offset.
TEST(CSharp_TextWriterTokenWriter, CustomIndentationString) {
	W h;
	h.w.IndentationString("  ");
	h.w.Indent();
	h.w.WriteKeyword("x");
	EXPECT_EQ(h.oss.str(), "  x");
	// Location after the write: needsIndent is false, so Location is just the column
	// (1 + 2 spaces of indentation + 1 char of "x" = column 4).
	EXPECT_EQ(h.w.Location(), (TextLocation{1, 4}));
}

// Location accounts for the pending indentation when no token has been written yet on the line.
TEST(CSharp_TextWriterTokenWriter, LocationAccountsForPendingIndent) {
	W h;
	h.w.IndentationString("  ");
	h.w.Indent();
	// No token written yet on this line -> needsIndent is true -> Location adds 2*1 = 2.
	EXPECT_EQ(h.w.Location(), (TextLocation{1, 3}));
}

// WriteIdentifier emits the identifier verbatim when it is not a keyword and not verbatim.
TEST(CSharp_TextWriterTokenWriter, WriteIdentifierPlain) {
	W h;
	auto id = std::unique_ptr<Identifier>(Identifier::Create("foo"));
	h.w.WriteIdentifier(id.get());
	EXPECT_EQ(h.oss.str(), "foo");
}

// WriteIdentifier prefixes `@` for a reserved keyword (the IsKeyword free helper).
TEST(CSharp_TextWriterTokenWriter, WriteIdentifierKeywordGetsAtPrefix) {
	W h;
	auto id = std::unique_ptr<Identifier>(Identifier::Create("class"));
	h.w.WriteIdentifier(id.get());
	EXPECT_EQ(h.oss.str(), "@class");
}

// WriteIdentifier prefixes `@` for a verbatim identifier.
TEST(CSharp_TextWriterTokenWriter, WriteIdentifierVerbatimGetsAtPrefix) {
	W h;
	auto id = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation::Empty, true));
	h.w.WriteIdentifier(id.get());
	EXPECT_EQ(h.oss.str(), "@foo");
}

// WritePrimitiveType emits a primitive-type keyword; `new` is special-cased to append `()`
// (the target-typed `new()` form).
TEST(CSharp_TextWriterTokenWriter, WritePrimitiveType) {
	W h;
	h.w.WritePrimitiveType("int");
	EXPECT_EQ(h.oss.str(), "int");
	h.w.WritePrimitiveType("new");
	// "new()" appended after "int" -> "intnew()"
	EXPECT_EQ(h.oss.str(), "intnew()");
}

// WriteComment renders each CommentType with its wrapper.
TEST(CSharp_TextWriterTokenWriter, WriteComment) {
	{
		W h;
		h.w.WriteComment(CommentType::SingleLine, "note");
		EXPECT_EQ(h.oss.str(), "//note\r\n");
	}
	{
		W h;
		h.w.WriteComment(CommentType::MultiLine, "x");
		EXPECT_EQ(h.oss.str(), "/*x*/");
	}
	{
		W h;
		h.w.WriteComment(CommentType::Documentation, "doc");
		EXPECT_EQ(h.oss.str(), "///doc\r\n");
	}
	{
		W h;
		h.w.WriteComment(CommentType::MultiLineDocumentation, "doc");
		EXPECT_EQ(h.oss.str(), "/**doc*/");
	}
	{
		W h;
		h.w.WriteComment(CommentType::InactiveCode, "dead");
		EXPECT_EQ(h.oss.str(), "dead");
	}
}

// WriteComment advances the line/column for newlines embedded in a multi-line body (the
// UpdateEndLocation helper): the final line/column reflect the trailing content.
TEST(CSharp_TextWriterTokenWriter, WriteCommentMultiLineAdvancesLocation) {
	W h;
	h.w.WriteComment(CommentType::MultiLine, "a\nb");
	EXPECT_EQ(h.oss.str(), "/*a\nb*/");
	// "/*" -> column += 2 -> 3; the "\n" -> line 2, column 0, then ++column -> 1; "b" -> ++column
	// -> 2; the trailing "*/" adds 2 to column (the writer) but UpdateEndLocation already
	// advanced for the body. The Location after is on line 2.
	EXPECT_EQ(h.w.Location().Line, 2);
}

// UpdateEndLocation advances line/column for embedded newlines (the static helper, exercised
// directly).
TEST(CSharp_TextWriterTokenWriter, UpdateEndLocation) {
	int line = 1, column = 5;
	TextWriterTokenWriter::UpdateEndLocation("ab\ncd", line, column);
	// 'a','b' -> column 6,7; '\n' -> line 2, column 0 then ++ -> 1; 'c','d' -> 2,3
	EXPECT_EQ(line, 2);
	EXPECT_EQ(column, 3);
	int line2 = 1, column2 = 1;
	TextWriterTokenWriter::UpdateEndLocation("x\r\ny", line2, column2);
	EXPECT_EQ(line2, 2);
	EXPECT_EQ(column2, 2);
}

// WritePreProcessorDirective starts on its own line, emits `#` + the lowercase directive name +
// the optional argument, then a newline.
TEST(CSharp_TextWriterTokenWriter, WritePreProcessorDirective) {
	{
		W h;
		h.w.WritePreProcessorDirective(PreProcessorDirectiveType::If, std::optional<std::string_view>("DEBUG"));
		EXPECT_EQ(h.oss.str(), "#if DEBUG\r\n");
	}
	{
		W h;
		h.w.WritePreProcessorDirective(PreProcessorDirectiveType::Define, std::nullopt);
		EXPECT_EQ(h.oss.str(), "#define\r\n");
	}
	{
		W h;
		h.w.WritePreProcessorDirective(PreProcessorDirectiveType::Region, std::optional<std::string_view>(""));
		// An empty argument is treated as absent (the C# `!string.IsNullOrEmpty(argument)`).
		EXPECT_EQ(h.oss.str(), "#region\r\n");
	}
}

// WritePreProcessorDirective starts a new line first when not already at the start of a line.
TEST(CSharp_TextWriterTokenWriter, WritePreProcessorDirectiveStartsNewLine) {
	W h;
	h.w.WriteKeyword("x");
	h.w.WritePreProcessorDirective(PreProcessorDirectiveType::If, std::optional<std::string_view>("A"));
	EXPECT_EQ(h.oss.str(), "x\r\n#if A\r\n");
}

// WriteInterpolatedText writes the escaped interpolated-string text (the ConvertString escape).
TEST(CSharp_TextWriterTokenWriter, WriteInterpolatedText) {
	W h;
	h.w.WriteInterpolatedText("a\nb");
	EXPECT_EQ(h.oss.str(), "a\\nb");  // the newline escaped to backslash-n
}

// StartNode emits the pending indentation; EndNode is a no-op (the location-setting decorator
// records the span between them).
TEST(CSharp_TextWriterTokenWriter, StartNodeEmitsIndentation) {
	W h;
	auto node = std::make_unique<Identifier>();  // any AstNode (the default ctor is public)
	h.w.Indent();
	h.w.StartNode(node.get());
	EXPECT_EQ(h.oss.str(), "\t");
	h.w.EndNode(node.get());
	EXPECT_EQ(h.oss.str(), "\t");  // EndNode adds nothing
}

// ---- WritePrimitiveValue over every PrimitiveValue alternative ----

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueNull) {
	W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::monostate{}), LiteralFormat::None);
	EXPECT_EQ(h.oss.str(), "null");
	EXPECT_EQ(h.w.Length(), 4);
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueBool) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(true), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "true");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(false), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "false");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueString) {
	W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::string("hello")), LiteralFormat::None);
	EXPECT_EQ(h.oss.str(), "\"hello\"");
	EXPECT_EQ(h.w.Length(), 7);  // 5 + 2 quotes
}

// A string with escapes renders the escape sequences within the quotes.
TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueStringWithEscapes) {
	W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::string("a\nb")), LiteralFormat::None);
	EXPECT_EQ(h.oss.str(), "\"a\\nb\"");
}

// The Utf8Literal format appends the `u8` suffix to a string literal.
TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueStringUtf8Suffix) {
	W h;
	h.w.WritePrimitiveValue(PrimitiveValue(std::string("x")), LiteralFormat::Utf8Literal);
	EXPECT_EQ(h.oss.str(), "\"x\"u8");
	EXPECT_EQ(h.w.Length(), 5);  // "\"x\"" (3: quotes + x) + "u8" (2)
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueChar) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(char16_t('a')), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "'a'");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(char16_t('\'')), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "'\\''");  // the escaped single-quote
	}
	{
		W h;
		h.w.WritePrimitiveValue(PrimitiveValue(char16_t(0x0A)), LiteralFormat::None);  // newline
		EXPECT_EQ(h.oss.str(), "'\\n'");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueDecimal) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(DecimalValue::FromInt32(100)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "100m");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(DecimalValue::FromInt32(-5)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "-5m");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueFloat) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(0.0f), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "0f");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(1.0f), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "1f");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(1.5f), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "1.5f");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(3.14f), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "3.14f");
	}
}

// The negative-zero float gets a leading '-' (the special case the C# prepends when the
// shortest-round-trip form dropped the sign).
TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueFloatNegativeZero) {
	W h;
	float negz = -0.0f;
		h.w.WritePrimitiveValue(PrimitiveValue(negz), LiteralFormat::None);
	EXPECT_EQ(h.oss.str(), "-0f");
}

// The float infinity/NaN cases render the `float.PositiveInfinity`/`NegativeInfinity`/`NaN` forms.
TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueFloatSpecial) {
	{
		W h;
		float pinf = std::numeric_limits<float>::infinity();
				h.w.WritePrimitiveValue(PrimitiveValue(pinf), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "float.PositiveInfinity");
	}
	{
		W h;
		float ninf = -std::numeric_limits<float>::infinity();
				h.w.WritePrimitiveValue(PrimitiveValue(ninf), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "float.NegativeInfinity");
	}
	{
		W h;
		float nan = std::numeric_limits<float>::quiet_NaN();
				h.w.WritePrimitiveValue(PrimitiveValue(nan), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "float.NaN");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueDouble) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(0.0), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "0.0");  // integral form -> ".0" appended
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(1.0), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "1.0");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(1.5), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "1.5");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(2.0), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "2.0");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueDoubleNegativeZero) {
	W h;
	double negz = -0.0;
		h.w.WritePrimitiveValue(PrimitiveValue(negz), LiteralFormat::None);
	EXPECT_EQ(h.oss.str(), "-0.0");
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueDoubleSpecial) {
	{
		W h;
		double pinf = std::numeric_limits<double>::infinity();
				h.w.WritePrimitiveValue(PrimitiveValue(pinf), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "double.PositiveInfinity");
	}
	{
		W h;
		double nan = std::numeric_limits<double>::quiet_NaN();
				h.w.WritePrimitiveValue(PrimitiveValue(nan), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "double.NaN");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueInt32) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(std::int32_t(42)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "42");
	}
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(std::int32_t(-1)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "-1");
	}
	{
		W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::int32_t(255)), LiteralFormat::HexadecimalNumber);
		EXPECT_EQ(h.oss.str(), "0xFF");
	}
	{
		W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::int32_t(-1)), LiteralFormat::HexadecimalNumber);
		EXPECT_EQ(h.oss.str(), "0xFFFFFFFF");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueUInt32) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(std::uint32_t(42)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "42u");
	}
	{
		W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::uint32_t(255)), LiteralFormat::HexadecimalNumber);
		EXPECT_EQ(h.oss.str(), "0xFFu");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueInt64) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(std::int64_t(42)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "42L");
	}
	{
		W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::int64_t(-1)), LiteralFormat::HexadecimalNumber);
		EXPECT_EQ(h.oss.str(), "0xFFFFFFFFFFFFFFFFL");
	}
}

TEST(CSharp_TextWriterTokenWriter, WritePrimitiveValueUInt64) {
	{
		W h;
				h.w.WritePrimitiveValue(PrimitiveValue(std::uint64_t(42)), LiteralFormat::None);
		EXPECT_EQ(h.oss.str(), "42uL");
	}
	{
		W h;
		h.w.WritePrimitiveValue(PrimitiveValue(std::uint64_t(255)), LiteralFormat::HexadecimalNumber);
		EXPECT_EQ(h.oss.str(), "0xFFuL");
	}
}

// PrintPrimitiveValue (the static) renders a literal to a string.
TEST(CSharp_TextWriterTokenWriter, PrintPrimitiveValue) {
	EXPECT_EQ(TextWriterTokenWriter::PrintPrimitiveValue(PrimitiveValue(std::int32_t(7))), "7");
	EXPECT_EQ(TextWriterTokenWriter::PrintPrimitiveValue(PrimitiveValue(std::monostate{})), "null");
	EXPECT_EQ(TextWriterTokenWriter::PrintPrimitiveValue(PrimitiveValue(true)), "true");
	EXPECT_EQ(TextWriterTokenWriter::PrintPrimitiveValue(PrimitiveValue(std::string("x"))), "\"x\"");
}

// ---- The public static escape helpers ----

TEST(CSharp_TextWriterTokenWriter, ConvertString) {
	EXPECT_EQ(TextWriterTokenWriter::ConvertString("plain"), "plain");
	EXPECT_EQ(TextWriterTokenWriter::ConvertString("a\nb"), "a\\nb");
	EXPECT_EQ(TextWriterTokenWriter::ConvertString("a\tb"), "a\\tb");
	EXPECT_EQ(TextWriterTokenWriter::ConvertString("a\"b"), "a\\\"b");
	EXPECT_EQ(TextWriterTokenWriter::ConvertString("a\\b"), "a\\\\b");
	EXPECT_EQ(TextWriterTokenWriter::ConvertString(""), "");
}

TEST(CSharp_TextWriterTokenWriter, ConvertCharLiteral) {
	EXPECT_EQ(TextWriterTokenWriter::ConvertCharLiteral('a'), "a");
	EXPECT_EQ(TextWriterTokenWriter::ConvertCharLiteral('\''), "\\'");
	EXPECT_EQ(TextWriterTokenWriter::ConvertCharLiteral('\"'), "\"");  // " is printable in a char literal
	EXPECT_EQ(TextWriterTokenWriter::ConvertCharLiteral('\n'), "\\n");
	EXPECT_EQ(TextWriterTokenWriter::ConvertCharLiteral('\t'), "\\t");
	EXPECT_EQ(TextWriterTokenWriter::ConvertCharLiteral('\\'), "\\\\");
}

TEST(CSharp_TextWriterTokenWriter, EscapeIdentifier) {
	EXPECT_EQ(TextWriterTokenWriter::EscapeIdentifier("foo"), "foo");
	EXPECT_EQ(TextWriterTokenWriter::EscapeIdentifier(""), "");
	EXPECT_EQ(TextWriterTokenWriter::EscapeIdentifier("a b"), "a b");  // space is printable here
	// A tab is non-printable -> escaped as \u0009.
	EXPECT_EQ(TextWriterTokenWriter::EscapeIdentifier("a\tb"), "a\\u0009b");
	// A backslash is non-printable -> \u005c.
	EXPECT_EQ(TextWriterTokenWriter::EscapeIdentifier("a\\b"), "a\\u005cb");
}

TEST(CSharp_TextWriterTokenWriter, ContainsNonPrintableIdentifierChar) {
	EXPECT_FALSE(TextWriterTokenWriter::ContainsNonPrintableIdentifierChar("foo"));
	EXPECT_FALSE(TextWriterTokenWriter::ContainsNonPrintableIdentifierChar(""));
	EXPECT_TRUE(TextWriterTokenWriter::ContainsNonPrintableIdentifierChar("a b"));  // space is whitespace
	EXPECT_TRUE(TextWriterTokenWriter::ContainsNonPrintableIdentifierChar("a\tb")); // tab
	EXPECT_TRUE(TextWriterTokenWriter::ContainsNonPrintableIdentifierChar("a\\b")); // backslash
}

// A small end-to-end snippet through the writer (the shape the output visitor drives): a
// keyword, a space, an identifier, a token, a primitive value, a newline.
TEST(CSharp_TextWriterTokenWriter, EndToEndSnippet) {
	W h;
	auto id = std::unique_ptr<Identifier>(Identifier::Create("x"));
	h.w.WriteKeyword("return");
	h.w.Space();
	h.w.WriteIdentifier(id.get());
	h.w.WriteToken(";");
	h.w.NewLine();
	EXPECT_EQ(h.oss.str(), "return x;\r\n");
	EXPECT_EQ(h.w.Location(), (TextLocation{2, 1}));
	EXPECT_EQ(h.w.Length(), 11);  // "return x;" (9) + "\r\n" (2)
}
