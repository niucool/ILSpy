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

// Port of the concrete `TextWriterTokenWriter` in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/TextWriterTokenWriter.cs -- the sink the
// `CSharpOutputVisitor` drives a C# AST tree through to produce the decompiled text. The C#
// writes the token stream to a `System.IO.TextWriter`; this port writes to an `std::ostream`
// (the C# `TextWriter` -> `std::ostream` port -- the D316 `TokenWriter` abstract base already
// declares the interface; this is the concrete implementation). It tracks the current
// `Indentation`/line/column so it can report `Location`/`Length` (it implements `ILocatable`,
// the role the location-setting decorator reads to record spans back onto the AST nodes), and
// it renders literal values (`WritePrimitiveValue`) via the `PrimitiveValue` variant (the D228
// boxed-literal model) with the C# `ToString("R")` shortest-round-trip float/double rendering.
//
// This is the next in-order Phase-5 piece of the output stage per the D318 decision-log entry:
// the `IsKeyword` free helper (D318) factored out from `CSharpOutputVisitor.IsKeyword` cleared
// the `WriteIdentifier -> CSharpOutputVisitor.IsKeyword` tangle that otherwise blocked this
// writer, so `WriteIdentifier` can now resolve the verbatim-`@`-prefix check to the ported
// `IsKeyword` free function and this concrete writer can land before the 97k-line
// `CSharpOutputVisitor` pretty-printer.
//
// C#-to-C++ porting decisions:
//  * `readonly TextWriter textWriter` -> a non-owning `std::ostream*` member (null-checked in
//    the ctor; the C# `ArgumentNullException` -> `std::invalid_argument`, the
//    `DecoratingTokenWriter` D316 precedent). A pointer (not a reference) so the null-check can
//    throw before any deref.
//  * `TextWriter.NewLine` (the line terminator string, `Environment.NewLine` on the build
//    platform) -> a fixed `"\r\n"` (the Windows `Environment.NewLine` the ILSpy test suite
//    assumes; `Length` accounting is 2 per newline). Held as a `static constexpr` so the
//    `Length` accounting is deterministic across platforms.
//  * `char`/`string` text -> `std::string`/`std::string_view` (the port stores text as UTF-8
//    bytes, the standard C++ text convention). The C# escape helpers (`ConvertChar`/
//    `ConvertString`/`EscapeIdentifier`/`IsPrintableIdentifierChar`) classify each code point
//    via `char.GetUnicodeCategory` (a UTF-16 code unit -> a Unicode category). The port decodes
//    the UTF-8 to a `char32_t` code point and classifies the ASCII range faithfully (control
//    chars, the named escapes, the explicitly-allowed ` `/`_`/`` ` ``/`^`); for non-ASCII it
//    escapes only surrogates and non-characters and passes everything else through as raw UTF-8
//    (a documented divergence -- the C# also escapes marks/separators/format/private-use/etc.
//    via the category database, which is not available in the C++ port; for the decompiler's
//    typical ASCII output this divergence does not arise). This keeps the output valid UTF-8
//    while avoiding a full Unicode category database dependency.
//  * `decimal.ToString(Invariant)` -> `DecimalValue::ToString()` (the D228 faithful
//    `System.Decimal` model already formats invariantly) + the `m` suffix.
//  * `float`/`double.ToString("R", Invariant)` -> `std::to_chars` (the C++17 shortest-round-trip
//    representation, the same goal as the C# `"R"` format) with the exponent marker upper-cased
//    to `'E'` to match the C# `"R"` output. The negative-zero special case is handled
//    faithfully (the `1/f == NegativeInfinity` check). The infinity/NaN cases render
//    `float.PositiveInfinity`/`NegativeInfinity`/`NaN` (and the `double.` forms) literally --
//    these are not strictly `PrimitiveExpression` values but the C# writer supports them for
//    code generators.
//  * `int`/`uint`/`long`/`ulong.ToString(null, Invariant)` -> `std::to_string` (invariant);
//    `ToString("X", Invariant)` -> `std::snprintf("%X", unsigned-cast)` (uppercase hex of the
//    two's-complement value, matching the C# `"X"` format), with the `u`/`L`/`uL` suffixes the
//    C# `IFormattable` branch appends.
//  * the C# `public static string PrintPrimitiveValue(object value)` (build a `StringWriter`,
//    drive a `TextWriterTokenWriter`, return the string) ports as a `static` method building an
//    `std::ostringstream`.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_TEXTWRITERTOKENWRITER_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_TEXTWRITERTOKENWRITER_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <charconv>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "Decompiler/CSharp/OutputVisitor/CSharpKeywordCheck.hpp"   // IsKeyword
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"          // TokenWriter, ILocatable, PrimitiveValue, ...
#include "Decompiler/CSharp/Syntax/Identifier.hpp"                  // Identifier, Name(), IsVerbatim()

namespace ILSpy::Decompiler::CSharp {

// The `Syntax` types the writer is expressed in, brought into the `OutputVisitor` namespace so
// the signatures read unqualified. `DecimalValue` (the faithful `System.Decimal` model the
// `PrimitiveValue` variant names) is also brought in so the `WritePrimitiveValue` body can name
// it unqualified (it lives in `Syntax` via the `PrimitiveExpression` header, NOT re-exported by
// `TokenWriter.hpp`).
using Syntax::Identifier;
using Syntax::DecimalValue;

namespace OutputVisitor {

namespace detail {

// The line terminator the C# `TextWriter.NewLine` defaults to on the build platform
// (`Environment.NewLine`); `"\r\n"` (the Windows value the ILSpy test suite assumes). Held
// here so the `Length` accounting (which the C# derives from `textWriter.NewLine.Length`) is
// deterministic across platforms.
inline constexpr std::string_view kNewLine = "\r\n";

// Decode one UTF-8 sequence in `str` starting at `index` to a code point; returns the code
// point and the number of bytes consumed. For an invalid/overlong/surrogate sequence, returns
// the offending lead byte as a code point and consumes 1 byte (the caller escapes it). This is
// the faithful equivalent of the C# `char`-by-`char` iteration over a UTF-16 string, adapted to
// the port's UTF-8 `std::string` text convention.
inline std::pair<char32_t, std::size_t> DecodeUtf8(std::string_view str, std::size_t index) {
	if (index >= str.size())
		return {char32_t(0), 0};
	unsigned char b0 = static_cast<unsigned char>(str[index]);
	if (b0 < 0x80)
		return {char32_t(b0), 1};
	char32_t cp = 0;
	std::size_t n = 0;
	if ((b0 & 0xE0) == 0xC0) { n = 2; cp = char32_t(b0 & 0x1F); }
	else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = char32_t(b0 & 0x0F); }
	else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = char32_t(b0 & 0x07); }
	else
		return {char32_t(b0), 1};
	for (std::size_t i = 1; i < n; ++i) {
		if (index + i >= str.size())
			return {char32_t(b0), 1};
		unsigned char b = static_cast<unsigned char>(str[index + i]);
		if ((b & 0xC0) != 0x80)
			return {char32_t(b0), 1};
		cp = (cp << 6) | char32_t(b & 0x3F);
	}
	if (n == 2 && cp < 0x80) return {char32_t(b0), 1};           // overlong
	if (n == 3 && cp < 0x800) return {char32_t(b0), 1};          // overlong
	if (n == 3 && cp >= 0xD800 && cp <= 0xDFFF) return {char32_t(b0), 1};  // surrogate
	if (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return {char32_t(b0), 1};  // out of range
	return {cp, n};
}

// Encode a code point as UTF-8 (the printable-case output of `ConvertCharLiteral`/
// `EscapeIdentifier` -- the C# appends the `char`/surrogate-pair directly, which the port
// re-encodes as UTF-8 to match the `std::string` text convention).
inline std::string EncodeUtf8(char32_t ch) {
	std::string out;
	if (ch < 0x80) {
		out += static_cast<char>(ch);
	} else if (ch < 0x800) {
		out += static_cast<char>(0xC0 | (ch >> 6));
		out += static_cast<char>(0x80 | (ch & 0x3F));
	} else if (ch < 0x10000) {
		out += static_cast<char>(0xE0 | (ch >> 12));
		out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (ch & 0x3F));
	} else {
		out += static_cast<char>(0xF0 | (ch >> 18));
		out += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (ch & 0x3F));
	}
	return out;
}

// Escape a code point as `\uXXXX` (4 hex digits) -- the C# `"\\u" + ((int)ch).ToString("x4")`
// (lowercase hex). Used by `ConvertChar` for control/non-character code points.
inline std::string UnicodeEscape4(char32_t ch) {
	char buf[8];
	std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<std::uint32_t>(ch));
	return std::string(buf);
}

// Whether a code point needs a `\u`/`\U` escape in the C# `ConvertChar`/`IsPrintableIdentifierChar`
// sense. Faithful for ASCII (the control range, excluding the named escapes the caller handles);
// for non-ASCII, escapes surrogates and the non-character code points and treats everything else
// as printable (the documented divergence -- the C# consults `char.GetUnicodeCategory`, which
// requires a Unicode category database not available in the C++ port; the decompiler's typical
// ASCII output does not exercise the divergent non-ASCII cases).
inline bool NeedsEscape(char32_t ch) {
	if (ch < 0x20 || ch == 0x7F)
		return true;                       // ASCII control (the named ones handled by the caller)
	if (ch < 0x80)
		return false;                      // ASCII printable
	if (ch >= 0xD800 && ch <= 0xDFFF)
		return true;                       // surrogate (defensive: not valid UTF-8)
	if (ch >= 0xFDD0 && ch <= 0xFDEF)
		return true;                       // non-character block U+FDD0..U+FDEF
	if ((ch & 0xFFFE) == 0xFFFE)
		return true;                       // the last two code points of each plane (U+FFFE/FFFF..)
	return false;                          // non-ASCII printable: pass through (documented divergence)
}

// The C# `char.IsWhiteSpace` -- ASCII whitespace plus the common Unicode separators. Faithful
// for the ASCII set (the case the decompiler output exercises); the separators are also caught
// by `NeedsEscape`, so the exact Unicode whitespace set is best-effort.
inline bool IsWhiteSpace(char32_t ch) {
	switch (ch) {
	case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
	case 0x85: case 0xA0: case 0x1680:
	case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
		return true;
	default:
		return false;
	}
}

// The C# `static string? ConvertChar(char ch)` -- the escape sequence for a code point within a
// char/string literal (not including the surrounding quotes), or `nullopt` when the code point
// is printable. The named escapes (`\\`, `\0`, `\a`, ...); the explicitly-allowed ` `/`_`/`` ` `
// /`^`; the explicit `\ufffd` case; and the `NeedsEscape` fall-back.
inline std::optional<std::string> ConvertChar(char32_t ch) {
	switch (ch) {
	case 0x5C: return std::string("\\\\");   // '\\'
	case 0x00: return std::string("\\0");
	case 0x07: return std::string("\\a");
	case 0x08: return std::string("\\b");
	case 0x0C: return std::string("\\f");
	case 0x0A: return std::string("\\n");
	case 0x0D: return std::string("\\r");
	case 0x09: return std::string("\\t");
	case 0x0B: return std::string("\\v");
	case 0x20: case 0x5F: case 0x60: case 0x5E:   // ' ', '_', '`', '^'
		return std::nullopt;
	case 0xFFFD:
		return UnicodeEscape4(ch);
	default:
		break;
	}
	if (NeedsEscape(ch))
		return UnicodeEscape4(ch);
	return std::nullopt;
}

// The C# `static bool IsPrintableIdentifierChar(string identifier, int index)` -- whether the
// code point at `index` may appear literally in an identifier. `\\` is never printable; ` `
// /`_`/`` ` ``/`^` always are; otherwise the negation of `NeedsEscape` (the C# Unicode-category
// test, the documented divergence).
inline bool IsPrintableIdentifierChar(std::string_view identifier, std::size_t index) {
	auto [cp, n] = DecodeUtf8(identifier, index);
	(void)n;
	switch (cp) {
	case 0x5C: return false;                 // '\\'
	case 0x20: case 0x5F: case 0x60: case 0x5E:
		return true;
	default:
		break;
	}
	return !NeedsEscape(cp);
}

// The C# `std::to_chars`-based shortest-round-trip rendering of a `float`/`double`, with the
// exponent marker upper-cased to `'E'` to match the C# `ToString("R")` output.
inline std::string FormatFloatRoundTrip(float f) {
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof(buf), f);
	std::string s(buf, res.ptr);
	for (char& c : s) if (c == 'e') c = 'E';
	return s;
}
inline std::string FormatDoubleRoundTrip(double d) {
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof(buf), d);
	std::string s(buf, res.ptr);
	for (char& c : s) if (c == 'e') c = 'E';
	return s;
}

// The lowercase name of a `PreProcessorDirectiveType` -- the C# `type.ToString().ToLowerInvariant()`
// (the enum member names ARE the directive names, lowercased). Used by `WritePreProcessorDirective`.
inline std::string_view PreProcessorDirectiveName(PreProcessorDirectiveType type) {
	switch (type) {
	case PreProcessorDirectiveType::Invalid:    return "invalid";
	case PreProcessorDirectiveType::Region:     return "region";
	case PreProcessorDirectiveType::Endregion:   return "endregion";
	case PreProcessorDirectiveType::If:         return "if";
	case PreProcessorDirectiveType::Endif:      return "endif";
	case PreProcessorDirectiveType::Elif:      return "elif";
	case PreProcessorDirectiveType::Else:      return "else";
	case PreProcessorDirectiveType::Define:    return "define";
	case PreProcessorDirectiveType::Undef:     return "undef";
	case PreProcessorDirectiveType::Error:     return "error";
	case PreProcessorDirectiveType::Warning:   return "warning";
	case PreProcessorDirectiveType::Pragma:    return "pragma";
	case PreProcessorDirectiveType::Line:      return "line";
	}
	return "invalid";
}

}  // namespace detail

// The C# `public class TextWriterTokenWriter : TokenWriter, ILocatable` -- the concrete sink that
// writes the token stream to an `std::ostream`, tracks the indentation/line/column, and reports
// `Location`/`Length` (the `ILocatable` surface the location-setting decorator reads). One
// `override` per `TokenWriter` abstract method; the static escape helpers and `PrintPrimitiveValue`
// are ported as `static` members matching the C# `public static` API.
class TextWriterTokenWriter : public TokenWriter, public ILocatable {
public:
	// The C# ctor null-checks the `TextWriter`; the port null-checks the `std::ostream*` (the
	// `DecoratingTokenWriter` D316 `std::invalid_argument` precedent) and defaults the
	// `IndentationString` to `"\t"`, `line`/`column` to 1 (1-based, the C# `TextLocation` convention).
	explicit TextWriterTokenWriter(std::ostream* textWriter)
		: textWriter_(textWriter) {
		if (textWriter_ == nullptr)
			throw std::invalid_argument("TextWriterTokenWriter: textWriter must not be null");
		indentationString_ = "\t";
		line_ = 1;
		column_ = 1;
	}

	~TextWriterTokenWriter() override = default;

	// The C# `public int Indentation { get; set; }` -- the current indent level (in units of
	// `IndentationString`).
	int Indentation = 0;

	// The C# `public string IndentationString { get; set; }` -- the string emitted per indent
	// level (default `"\t"`); a public data member (the C# auto-property).
	std::string IndentationString() const { return indentationString_; }
	void IndentationString(std::string value) { indentationString_ = std::move(value); }

	// The C# `public TextLocation Location { get; }` (the `ILocatable` accessor) -- the current
	// source location, accounting for the pending indentation.
	TextLocation Location() const override {
		return TextLocation(line_, column_ + (needsIndent_ ? Indentation * static_cast<int>(indentationString_.size()) : 0));
	}

	// The C# `public int Length { get; private set; }` -- the total characters written.
	int Length() const override { return length_; }

	// ---- The `TokenWriter` overrides ----

	// The C# `public override void WriteIdentifier(Identifier identifier)` -- emit the
	// identifier, prefixing `@` when it is verbatim or a keyword in its context (the `IsKeyword`
	// free helper, the D318 factored-out `CSharpOutputVisitor.IsKeyword`), and escaping
	// non-printable characters via `EscapeIdentifier`.
	void WriteIdentifier(Identifier* identifier) override {
		WriteIndentation();
		if (identifier->IsVerbatim() || IsKeyword(identifier->Name(), identifier)) {
			*textWriter_ << '@';
			column_++;
			length_++;
		}
		std::string name = EscapeIdentifier(identifier->Name());
		*textWriter_ << name;
		column_ += static_cast<int>(name.size());
		length_ += static_cast<int>(name.size());
		isAtStartOfLine_ = false;
	}

	// The C# `public override void WriteKeyword(string keyword)` / `WriteToken(string token)` --
	// a keyword or a punctuation/operator token (with indentation).
	void WriteKeyword(std::string_view keyword) override {
		WriteIndentation();
		column_ += static_cast<int>(keyword.size());
		length_ += static_cast<int>(keyword.size());
		*textWriter_ << keyword;
		isAtStartOfLine_ = false;
	}
	void WriteToken(std::string_view token) override {
		WriteIndentation();
		column_ += static_cast<int>(token.size());
		length_ += static_cast<int>(token.size());
		*textWriter_ << token;
		isAtStartOfLine_ = false;
	}

	// The C# `public override void Space()` -- a single space (with indentation).
	void Space() override {
		WriteIndentation();
		column_++;
		length_++;
		*textWriter_ << ' ';
	}

	// The C# `protected void WriteIndentation()` -- emit the pending indentation once per line
	// (the `needsIndent` flag is cleared after the first emission and reset by `NewLine`/the
	// comment newlines).
	void WriteIndentation() {
		if (needsIndent_) {
			needsIndent_ = false;
			for (int i = 0; i < Indentation; ++i)
				*textWriter_ << indentationString_;
			column_ += Indentation * static_cast<int>(indentationString_.size());
			length_ += Indentation * static_cast<int>(indentationString_.size());
		}
	}

	// The C# `public override void NewLine()` -- the line terminator (the platform
	// `Environment.NewLine` -> the fixed `"\r\n"`); reset column, advance line, re-enable
	// indentation.
	void NewLine() override {
		*textWriter_ << detail::kNewLine;
		column_ = 1;
		line_++;
		length_ += static_cast<int>(detail::kNewLine.size());
		needsIndent_ = true;
		isAtStartOfLine_ = true;
	}

	void Indent() override { Indentation++; }
	void Unindent() override { Indentation--; }

	// The C# `public override void WriteComment(CommentType commentType, string content)` -- the
	// per-`CommentType` comment rendering (the `//`/`/* */`/`///`/`/** */` wrappers, the
	// `InactiveCode` fall-through). The multi-line forms advance line/column via the
	// `UpdateEndLocation` helper.
	void WriteComment(CommentType commentType, std::string_view content) override {
		WriteIndentation();
		switch (commentType) {
		case CommentType::SingleLine:
			*textWriter_ << "//" << content << detail::kNewLine;
			length_ += 2 + static_cast<int>(content.size()) + static_cast<int>(detail::kNewLine.size());
			column_ = 1;
			line_++;
			needsIndent_ = true;
			isAtStartOfLine_ = true;
			break;
		case CommentType::MultiLine:
			*textWriter_ << "/*" << content << "*/";
			length_ += 4 + static_cast<int>(content.size());
			column_ += 2;
			UpdateEndLocation(content, line_, column_);
			column_ += 2;
			isAtStartOfLine_ = false;
			break;
		case CommentType::Documentation:
			*textWriter_ << "///" << content << detail::kNewLine;
			length_ += 3 + static_cast<int>(content.size()) + static_cast<int>(detail::kNewLine.size());
			column_ = 1;
			line_++;
			needsIndent_ = true;
			isAtStartOfLine_ = true;
			break;
		case CommentType::MultiLineDocumentation:
			*textWriter_ << "/**" << content << "*/";
			length_ += 5 + static_cast<int>(content.size());
			column_ += 3;
			UpdateEndLocation(content, line_, column_);
			column_ += 2;
			isAtStartOfLine_ = false;
			break;
		default:  // `InactiveCode` and any future style: the raw content
			*textWriter_ << content;
			column_ += static_cast<int>(content.size());
			length_ += static_cast<int>(content.size());
			break;
		}
	}

	// The C# `static void UpdateEndLocation(string content, ref int line, ref int column)` --
	// advance `line`/`column` for the newlines embedded in a multi-line comment body. Each `\r\n`
	// (or lone `\r`/`\n`) starts a new line; the column is reset and then advances for the
	// trailing content on the last line.
	static void UpdateEndLocation(std::string_view content, int& line, int& column) {
		if (content.empty())
			return;
		for (std::size_t i = 0; i < content.size(); ++i) {
			char ch = content[i];
			switch (ch) {
			case '\r':
				if (i + 1 < content.size() && content[i + 1] == '\n')
					++i;
				[[fallthrough]];
			case '\n':
				line++;
				column = 0;
				break;
			default:
				break;
			}
			++column;
		}
	}

	// The C# `public override void WritePreProcessorDirective(PreProcessorDirectiveType type,
	// string? argument)` -- a preprocessor directive starts on its own line; emit the `#` +
	// the lowercase directive name + the optional argument, then a newline.
	void WritePreProcessorDirective(PreProcessorDirectiveType type, std::optional<std::string_view> argument) override {
		if (!isAtStartOfLine_)
			NewLine();
		WriteIndentation();
		*textWriter_ << '#';
		std::string_view directive = detail::PreProcessorDirectiveName(type);
		*textWriter_ << directive;
		column_ += 1 + static_cast<int>(directive.size());
		length_ += 1 + static_cast<int>(directive.size());
		if (argument.has_value() && !argument->empty()) {
			*textWriter_ << ' ' << *argument;
			column_ += 1 + static_cast<int>(argument->size());
			length_ += 1 + static_cast<int>(argument->size());
		}
		NewLine();
	}

	// The C# `public override void WriteInterpolatedText(string text)` -- the escaped
	// interpolated-string text (the `ConvertString` escape helper).
	void WriteInterpolatedText(std::string_view text) override {
		*textWriter_ << ConvertString(text);
	}

	// The C# `public override void WritePrimitiveType(string type)` -- a primitive-type keyword
	// (the C# special-cases `"new"` to append `"()"`, the target-typed `new()` form).
	void WritePrimitiveType(std::string_view type) override {
		*textWriter_ << type;
		column_ += static_cast<int>(type.size());
		length_ += static_cast<int>(type.size());
		if (type == "new") {
			*textWriter_ << "()";
			column_ += 2;
			length_ += 2;
		}
	}

	// The C# `public override void WritePrimitiveValue(object? value, LiteralFormat format =
	// LiteralFormat.None)` -- render a boxed literal (the `PrimitiveValue` variant) to text.
	// The default argument is NOT repeated on the override (the D316 default-arg-on-pure-virtual
	// rule -- it applies only through a `TokenWriter&` base, not the concrete type).
	void WritePrimitiveValue(const PrimitiveValue& value, LiteralFormat format) override {
		if (std::holds_alternative<std::monostate>(value)) {
			WriteRaw("null");
			return;
		}
		if (std::holds_alternative<bool>(value)) {
			if (std::get<bool>(value)) WriteRaw("true"); else WriteRaw("false");
			return;
		}
		if (std::holds_alternative<std::string>(value)) {
			const std::string& s = std::get<std::string>(value);
			std::string tmp = ConvertString(s);
			column_ += static_cast<int>(tmp.size()) + 2;
			length_ += static_cast<int>(tmp.size()) + 2;
			*textWriter_ << '"' << tmp << '"';
			if (format == LiteralFormat::Utf8Literal) {
				*textWriter_ << "u8";
				column_ += 2;
				length_ += 2;
			}
			return;
		}
		if (std::holds_alternative<char16_t>(value)) {
			char16_t ch = std::get<char16_t>(value);
			std::string tmp = ConvertCharLiteral(ch);
			column_ += static_cast<int>(tmp.size()) + 2;
			length_ += static_cast<int>(tmp.size()) + 2;
			*textWriter_ << '\'' << tmp << '\'';
			return;
		}
		if (std::holds_alternative<DecimalValue>(value)) {
			std::string str = std::get<DecimalValue>(value).ToString() + "m";
			column_ += static_cast<int>(str.size());
			length_ += static_cast<int>(str.size());
			*textWriter_ << str;
			return;
		}
		if (std::holds_alternative<float>(value)) {
			WriteFloat(std::get<float>(value));
			return;
		}
		if (std::holds_alternative<double>(value)) {
			WriteDouble(std::get<double>(value));
			return;
		}
		if (std::holds_alternative<std::int32_t>(value)
			|| std::holds_alternative<std::uint32_t>(value)
			|| std::holds_alternative<std::int64_t>(value)
			|| std::holds_alternative<std::uint64_t>(value)) {
			WriteInteger(value, format);
			return;
		}
		// `AnyValueTag` (the pattern wildcard, never output) and any future alternative: render
		// nothing (the C# `else` branch would call `value.ToString()`, which for the wildcard
		// `AnyValue` object yields the type name -- not a useful literal; the wildcard is set only
		// on pattern `PrimitiveExpression`s, never written to output).
	}

	// The C# `public override void StartNode(AstNode node)` -- emit the pending indentation so a
	// `StartNode` override can rely on `Length` for the node's start position; `EndNode` is a
	// no-op (the location-setting decorator records the span between them).
	void StartNode(AstNode* /*node*/) override { WriteIndentation(); }
	void EndNode(AstNode* /*node*/) override { }

	// ---- The C# `public static` escape helpers (the public API the output visitor and the
	// tests consult) ----

	// The C# `public static string ConvertCharLiteral(char ch)` -- the escape sequence for a
	// char literal (not including the surrounding `'`); `\'` for the quote, otherwise
	// `ConvertChar` or the code point itself.
	static std::string ConvertCharLiteral(char16_t ch) {
		if (ch == u'\'')
			return std::string("\\'");
		auto esc = detail::ConvertChar(char32_t(ch));
		if (esc)
			return *esc;
		return detail::EncodeUtf8(char32_t(ch));
	}

	// The C# `public static string ConvertString(string str)` -- escape the special characters
	// in a string literal body (not including the surrounding `"`); `"` -> `\"`, otherwise
	// `ConvertChar`, else the raw UTF-8 bytes.
	static std::string ConvertString(std::string_view str) {
		std::string out;
		std::size_t i = 0;
		while (i < str.size()) {
			auto [cp, n] = detail::DecodeUtf8(str, i);
			std::optional<std::string> esc;
			if (cp == 0x22)  // '"'
				esc = std::string("\\\"");
			else
				esc = detail::ConvertChar(cp);
			if (esc)
				out += *esc;
			else
				out += str.substr(i, n);   // the raw UTF-8 bytes (the C# appends the `char`)
			i += n;
		}
		return out;
	}

	// The C# `public static string EscapeIdentifier(string identifier)` -- escape the
	// non-printable code points in an identifier (the `\uXXXX`/`\UXXXXXXXX` forms), passing the
	// printable ones through (preserving surrogate pairs as raw UTF-8).
	static std::string EscapeIdentifier(std::string_view identifier) {
		if (identifier.empty())
			return std::string(identifier);
		std::string out;
		std::size_t i = 0;
		while (i < identifier.size()) {
			auto [cp, n] = detail::DecodeUtf8(identifier, i);
			if (detail::IsPrintableIdentifierChar(identifier, i)) {
				out += identifier.substr(i, n);
			} else if (cp > 0xFFFF) {
				char buf[16];
				std::snprintf(buf, sizeof(buf), "\\U%08x", static_cast<std::uint32_t>(cp));
				out += buf;
			} else {
				out += detail::UnicodeEscape4(cp);
			}
			i += n;
		}
		return out;
	}

	// The C# `public static bool ContainsNonPrintableIdentifierChar(string identifier)` --
	// whether an identifier contains a whitespace or otherwise non-printable code point.
	static bool ContainsNonPrintableIdentifierChar(std::string_view identifier) {
		if (identifier.empty())
			return false;
		std::size_t i = 0;
		while (i < identifier.size()) {
			auto [cp, n] = detail::DecodeUtf8(identifier, i);
			(void)n;
			if (detail::IsWhiteSpace(cp))
				return true;
			if (!detail::IsPrintableIdentifierChar(identifier, i))
				return true;
			i += n;
		}
		return false;
	}

	// The C# `public static string PrintPrimitiveValue(object value)` -- render a literal value
	// to a string (build a `StringWriter`, drive a `TextWriterTokenWriter`, return the text).
	static std::string PrintPrimitiveValue(const PrimitiveValue& value) {
		std::ostringstream oss;
		TextWriterTokenWriter w(&oss);
		// The 1-arg form resolves the default `LiteralFormat::None` only through a `TokenWriter&`
		// base reference (the D316 default-arg-on-pure-virtual rule); pass it explicitly here so
		// the call through the concrete `TextWriterTokenWriter` type resolves.
		static_cast<TokenWriter&>(w).WritePrimitiveValue(value);
		return oss.str();
	}

private:
	// Write a raw string to the stream and advance column/length (the C# `textWriter.Write(s);
	// column += s.Length; Length += s.Length;` pattern -- no indentation, no
	// `isAtStartOfLine` change; used by the `WritePrimitiveValue` literal-rendering branches).
	void WriteRaw(std::string_view s) {
		*textWriter_ << s;
		column_ += static_cast<int>(s.size());
		length_ += static_cast<int>(s.size());
	}

	// The C# `float` branch of `WritePrimitiveValue` -- the infinity/NaN special cases (rendered
	// as `float.PositiveInfinity`/`NegativeInfinity`/`NaN`) and the normal shortest-round-trip
	// rendering + the `f` suffix + the negative-zero special case.
	void WriteFloat(float f) {
		if (std::isinf(f) || std::isnan(f)) {
			WriteRaw("float");
			WriteToken(".");
			if (std::isinf(f) && !std::signbit(f)) {
				WriteRaw("PositiveInfinity");
			} else if (std::isinf(f) && std::signbit(f)) {
				WriteRaw("NegativeInfinity");
			} else {
				WriteRaw("NaN");
			}
			return;
		}
		std::string str = detail::FormatFloatRoundTrip(f) + "f";
		if (f == 0.0f && 1.0f / f == -std::numeric_limits<float>::infinity() && str[0] != '-') {
			// negative zero is a special case (not a primitive expression, but better handled
			// here than in every code generator) -- prepend '-' when the round-trip form lost it.
			str = '-' + str;
		}
		column_ += static_cast<int>(str.size());
		length_ += static_cast<int>(str.size());
		*textWriter_ << str;
	}

	// The C# `double` branch of `WritePrimitiveValue` -- the infinity/NaN special cases and the
	// normal shortest-round-trip rendering + the negative-zero special case + the `.0` append
	// for integral forms without an exponent (so the literal is unambiguously a `double`).
	void WriteDouble(double d) {
		if (std::isinf(d) || std::isnan(d)) {
			WriteRaw("double");
			WriteToken(".");
			if (std::isinf(d) && !std::signbit(d)) {
				WriteRaw("PositiveInfinity");
			} else if (std::isinf(d) && std::signbit(d)) {
				WriteRaw("NegativeInfinity");
			} else {
				WriteRaw("NaN");
			}
			return;
		}
		std::string number = detail::FormatDoubleRoundTrip(d);
		if (d == 0.0 && 1.0 / d == -std::numeric_limits<double>::infinity() && number[0] != '-') {
			number = '-' + number;
		}
		if (number.find('.') == std::string::npos && number.find('E') == std::string::npos) {
			number += ".0";
		}
		column_ += static_cast<int>(number.size());
		length_ += static_cast<int>(number.size());
		*textWriter_ << number;
	}

	// The C# `IFormattable` branch of `WritePrimitiveValue` -- the integer types
	// (`int32`/`uint32`/`int64`/`uint64`), rendered via `std::to_string` (invariant) or, for the
	// `HexadecimalNumber` format, `"0x" + uppercase-hex` (the C# `ToString("X")` of the
	// two's-complement value), with the `u`/`L`/`uL` suffixes the C# appends per type.
	void WriteInteger(const PrimitiveValue& value, LiteralFormat format) {
		std::string body;
		std::string_view suffix;
		if (std::holds_alternative<std::int32_t>(value)) {
			std::int32_t v = std::get<std::int32_t>(value);
			if (format == LiteralFormat::HexadecimalNumber) {
				char buf[16];
				std::snprintf(buf, sizeof(buf), "0x%X", static_cast<unsigned int>(v));
				body = buf;
			} else {
				body = std::to_string(v);
			}
		} else if (std::holds_alternative<std::uint32_t>(value)) {
			std::uint32_t v = std::get<std::uint32_t>(value);
			if (format == LiteralFormat::HexadecimalNumber) {
				char buf[16];
				std::snprintf(buf, sizeof(buf), "0x%X", static_cast<unsigned int>(v));
				body = buf;
			} else {
				body = std::to_string(v);
			}
			suffix = "u";
		} else if (std::holds_alternative<std::int64_t>(value)) {
			std::int64_t v = std::get<std::int64_t>(value);
			if (format == LiteralFormat::HexadecimalNumber) {
				char buf[24];
				std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
				body = buf;
			} else {
				body = std::to_string(v);
			}
			suffix = "L";
		} else {  // `std::uint64_t`
			std::uint64_t v = std::get<std::uint64_t>(value);
			if (format == LiteralFormat::HexadecimalNumber) {
				char buf[24];
				std::snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(v));
				body = buf;
			} else {
				body = std::to_string(v);
			}
			suffix = "uL";
		}
		*textWriter_ << body << suffix;
		column_ += static_cast<int>(body.size() + suffix.size());
		length_ += static_cast<int>(body.size() + suffix.size());
	}

	std::ostream* textWriter_;
	bool needsIndent_ = true;
	bool isAtStartOfLine_ = true;
	int line_ = 1;
	int column_ = 1;
	int length_ = 0;
	std::string indentationString_ = "\t";
};

}  // namespace OutputVisitor
}  // namespace ILSpy::Decompiler::CSharp

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_TEXTWRITERTOKENWRITER_HPP
