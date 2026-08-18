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

// Port of `InsertRequiredSpacesDecorator` in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/InsertRequiredSpacesDecorator.cs -- the
// `DecoratingTokenWriter` (D316) subclass that inserts the minimal amount of whitespace so the
// lexer recognizes the tokens the `CSharpOutputVisitor` (not yet ported) emits. The output visitor
// does not emit every inter-token space (it leaves them to this decorator); this decorator tracks
// the `LastWritten` token kind and inserts a space only where the next token would otherwise merge
// with the previous one into a single lexeme (two `+` tokens into `++`, a keyword followed by an
// identifier, an identifier followed by a number that could be read as a type suffix, a division
// operator followed by a `/*` comment or a `//` line comment, etc.).
//
// This is the next in-order Phase-5 piece of the output stage per the D319 decision-log entry,
// which named the two decorator writers (`InsertRequiredSpacesDecorator`,
// `InsertMissingTokensDecorator`) as the next-in-order pieces now that the concrete
// `TextWriterTokenWriter` (D319) is in place. Every dependency this decorator consults is
// already ported: the `DecoratingTokenWriter`/`TokenWriter` bases (D316), the `IsKeyword` free
// helper (D318, the `@`-prefix check in `WriteIdentifier`), the `Identifier` node (D227,
// `Name()`/`IsVerbatim()`), the `CommentType` enum (D288), the `PreProcessorDirectiveType` enum
// (D294), the `LiteralFormat` enum and `PrimitiveValue` variant (D228, with the `DecimalValue`
// `System.Decimal` model and the `float`/`double`/integer alternatives).
//
// C#-to-C++ porting decisions:
//  * the C# `LastWritten` enum (a private nested enum) ports as a private nested `enum class`
//    with the same members in the same order; `lastWritten` (the C# field) ports as
//    `lastWritten_` (private, trailing-underscore, the TextWriterTokenWriter convention).
//  * `WriteIdentifier(Identifier identifier)` -> `WriteIdentifier(Identifier* identifier)` (the
//    non-owning-pointer convention; the `Identifier` header is included so `Name()`/`IsVerbatim()`
//    are available). The `CSharpOutputVisitor.IsKeyword(identifier.Name, identifier)` call ports
//    to the `IsKeyword` free helper (D318): `IsKeyword(identifier->Name(), identifier)` -- the
//    `std::string` `Name()` implicitly binds to the `std::string_view` parameter.
//  * the C# `Space()` (unqualified -- the virtual override) vs `base.Space()` distinction is
//    preserved: the override sets `lastWritten_ = Whitespace` AND forwards; `base.Space()` (the
//    `DecoratingTokenWriter::Space()` qualified call) just forwards without touching
//    `lastWritten_`. `WriteIdentifier`'s strictly-required-space branch uses `base.Space()`, every
//    other not-strictly-required branch uses `Space()` (the override) -- faithfully mirroring the
//    C# comment "this space is not strictly required, so we call Space()" vs "this space is
//    strictly required, so we directly call the formatter".
//  * the `WritePrimitiveValue` override does NOT repeat the `LiteralFormat::None` default (the
//    D316 default-arg-on-pure-virtual crux: a default argument on a pure-virtual is resolved at
//    the static call-site type, so the base's default applies only through a `TokenWriter&`
//    reference, not the concrete override type; the override takes `format` by value and forwards
//    it explicitly to `DecoratingTokenWriter::WritePrimitiveValue(value, format)`).
//  * the C# `value is bool`/`is string`/`is char`/`is decimal`/`is float`/`is double`/
//    `is IFormattable` type tests port to `std::holds_alternative<T>` on the `PrimitiveValue`
//    variant (`std::monostate` is the C# `null`, `char16_t` is the C# `char`, `DecimalValue` is
//    the C# `decimal`, `IFormattable` is the integer alternatives `int32`/`uint32`/`int64`/
//    `uint64`); `float.IsInfinity`/`IsNaN` port to `std::isinf`/`std::isnan` (`<cmath>`).
//  * `token[0]` (the C# `char` first character) ports to `token.front()` guarded by
//    `!token.empty()` (the C# would throw on an empty `token`; the C++ `string_view::front` on
//    empty is UB, so a defensive empty guard avoids it -- the decompiler never writes an empty
//    token, so the observable behavior is unchanged for valid input).
// NO C++ name-shadowing crux (the `InsertRequiredSpacesDecorator` method/member names do not
// collide with any class in the `Syntax` or `OutputVisitor` namespaces). NO new `Slots` constant,
// NO new enum (the `LastWritten` is private to this class), NO new `AstNode` helper (this is the
// output-stage decorator, not an AST node). Namespace mapping
// `ICSharpCode.Decompiler.CSharp.OutputVisitor` -> `ILSpy::Decompiler::CSharp::OutputVisitor`
// (the convention). Header-only (no CMake source listing -- the TokenWriter/TextWriterTokenWriter
// header-only precedent), so the CSharp `CMakeLists.txt` is unchanged; only the test `.cpp` is
// added to `tests/CMakeLists.txt`.

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTREQUIREDSPACESDECORATOR_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTREQUIREDSPACESDECORATOR_HPP

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "Decompiler/CSharp/OutputVisitor/CSharpKeywordCheck.hpp"   // IsKeyword
#include "Decompiler/CSharp/OutputVisitor/TokenWriter.hpp"         // DecoratingTokenWriter, TokenWriter, PrimitiveValue, ...
#include "Decompiler/CSharp/Syntax/Identifier.hpp"                // Identifier, Name(), IsVerbatim()

namespace ILSpy::Decompiler::CSharp {

// `DecimalValue` (the faithful `System.Decimal` model the `PrimitiveValue` variant names) is
// brought into the `OutputVisitor` scope so the `WritePrimitiveValue` `std::holds_alternative`
// type test can name it unqualified (it lives in `Syntax` via the `PrimitiveExpression` header,
// NOT re-exported by `TokenWriter.hpp`).
using Syntax::DecimalValue;

namespace OutputVisitor {

// The C# `class InsertRequiredSpacesDecorator : DecoratingTokenWriter` -- inserts the minimal
// inter-token whitespace so the lexer recognizes the tokens the output visitor emits. Tracks the
// `LastWritten` kind of the most recently written token and inserts a `Space()` only where the
// next token would otherwise merge with the previous one into a single lexeme.
class InsertRequiredSpacesDecorator : public DecoratingTokenWriter {
public:
	explicit InsertRequiredSpacesDecorator(TokenWriter* writer)
		: DecoratingTokenWriter(writer) {}

	// The owning ctor (used by the `TokenWriter::Create` / `CreateWriterThatSetsLocationsInAST`
	// factories that compose a stack and return a single owning handle to the top) -- takes
	// ownership of the wrapped writer via the `DecoratingTokenWriter` owning-mode base ctor.
	explicit InsertRequiredSpacesDecorator(std::unique_ptr<TokenWriter> writer)
		: DecoratingTokenWriter(std::move(writer)) {}

	void StartNode(AstNode* node) override { DecoratingTokenWriter::StartNode(node); }
	void EndNode(AstNode* node) override { DecoratingTokenWriter::EndNode(node); }

	// The C# `public override void WriteIdentifier(Identifier identifier)` -- a verbatim or
	// keyword identifier needs a preceding space only if the last token was a keyword or
	// identifier (the space is not strictly required, so the override `Space()` is called); a
	// non-verbatim, non-keyword identifier needs a strictly-required space after a keyword or
	// identifier (so `base.Space()` is called directly, bypassing the override).
	void WriteIdentifier(Identifier* identifier) override {
		if (identifier->IsVerbatim() || IsKeyword(identifier->Name(), identifier)) {
			if (lastWritten_ == LastWritten::KeywordOrIdentifier) {
				// This space is not strictly required, so the override `Space()` is called
				// (it sets `lastWritten_ = Whitespace` then forwards).
				Space();
			}
		} else if (lastWritten_ == LastWritten::KeywordOrIdentifier) {
			// This space is strictly required, so the formatter is called directly (bypassing
			// the override, which would otherwise reset `lastWritten_`).
			DecoratingTokenWriter::Space();
		}
		DecoratingTokenWriter::WriteIdentifier(identifier);
		lastWritten_ = LastWritten::KeywordOrIdentifier;
	}

	// The C# `public override void WriteKeyword(string keyword)` -- a keyword always needs a
	// preceding space after a keyword or identifier.
	void WriteKeyword(std::string_view keyword) override {
		if (lastWritten_ == LastWritten::KeywordOrIdentifier) {
			Space();
		}
		DecoratingTokenWriter::WriteKeyword(keyword);
		lastWritten_ = LastWritten::KeywordOrIdentifier;
	}

	// The C# `public override void WriteToken(string token)` -- avoid merging two `+`/`-`/`&`/`?`
	// tokens into `++`/`--`/`&&`/`??`, and avoid a `*` after a `/` (the `/*` comment start) or a
	// `//` after a `/`. The single-char first-byte comparison is guarded against an empty token.
	void WriteToken(std::string_view token) override {
		const char first = token.empty() ? '\0' : token.front();
		if ((lastWritten_ == LastWritten::Plus && first == '+') ||
			(lastWritten_ == LastWritten::Minus && first == '-') ||
			(lastWritten_ == LastWritten::Ampersand && first == '&') ||
			(lastWritten_ == LastWritten::QuestionMark && first == '?') ||
			(lastWritten_ == LastWritten::Division && first == '*')) {
			DecoratingTokenWriter::Space();
		}
		DecoratingTokenWriter::WriteToken(token);
		if (token == "+") {
			lastWritten_ = LastWritten::Plus;
		} else if (token == "-") {
			lastWritten_ = LastWritten::Minus;
		} else if (token == "&") {
			lastWritten_ = LastWritten::Ampersand;
		} else if (token == "?") {
			lastWritten_ = LastWritten::QuestionMark;
		} else if (token == "/") {
			lastWritten_ = LastWritten::Division;
		} else {
			lastWritten_ = LastWritten::Other;
		}
	}

	// The C# `public override void Space()` -- record that whitespace was written.
	void Space() override {
		DecoratingTokenWriter::Space();
		lastWritten_ = LastWritten::Whitespace;
	}

	// The C# `public override void NewLine()` -- a newline is whitespace.
	void NewLine() override {
		DecoratingTokenWriter::NewLine();
		lastWritten_ = LastWritten::Whitespace;
	}

	// The C# `public override void WriteComment(CommentType, string)` -- a comment after a
	// division operator needs a preceding space (the `1.0 / /*comment*/a` case); otherwise the
	// comment is written and the last-written kind resets to whitespace.
	void WriteComment(CommentType commentType, std::string_view content) override {
		if (lastWritten_ == LastWritten::Division) {
			DecoratingTokenWriter::Space();
		}
		DecoratingTokenWriter::WriteComment(commentType, content);
		lastWritten_ = LastWritten::Whitespace;
	}

	// The C# `public override void WritePreProcessorDirective(PreProcessorDirectiveType, string?)`
	// -- a preprocessor directive resets the last-written kind to whitespace.
	void WritePreProcessorDirective(PreProcessorDirectiveType type, std::optional<std::string_view> argument) override {
		DecoratingTokenWriter::WritePreProcessorDirective(type, argument);
		lastWritten_ = LastWritten::Whitespace;
	}

	// The C# `public override void WritePrimitiveValue(object?, LiteralFormat = None)` -- a
	// literal value needs a preceding space after a keyword or identifier (to avoid the
	// identifier-then-number `1foo` merge); the last-written kind is set per the value's type so a
	// following identifier is not read as a type suffix. The default `LiteralFormat::None` argument
	// is NOT repeated (the D316 default-arg-on-pure-virtual crux: it applies only through a
	// `TokenWriter&` base reference, not the concrete override type).
	void WritePrimitiveValue(const PrimitiveValue& value, LiteralFormat format) override {
		if (lastWritten_ == LastWritten::KeywordOrIdentifier) {
			Space();
		}
		DecoratingTokenWriter::WritePrimitiveValue(value, format);
		// `std::monostate` is the C# `null`; `bool` carries no type suffix, so a following
		// identifier cannot be mistaken for one -- both return without setting the kind.
		if (std::holds_alternative<std::monostate>(value) || std::holds_alternative<bool>(value))
			return;
		if (std::holds_alternative<std::string>(value)) {
			if (format == LiteralFormat::VerbatimStringLiteral)
				lastWritten_ = LastWritten::KeywordOrIdentifier;
			else
				lastWritten_ = LastWritten::Other;
		} else if (std::holds_alternative<char16_t>(value)) {
			lastWritten_ = LastWritten::Other;
		} else if (std::holds_alternative<DecimalValue>(value)) {
			lastWritten_ = LastWritten::Other;
		} else if (std::holds_alternative<float>(value)) {
			float f = std::get<float>(value);
			if (std::isinf(f) || std::isnan(f))
				return;
			lastWritten_ = LastWritten::Other;
		} else if (std::holds_alternative<double>(value)) {
			double d = std::get<double>(value);
			if (std::isinf(d) || std::isnan(d))
				return;
			// Needs a space if an identifier follows the number; this avoids mistaking the
			// following identifier as a type suffix.
			lastWritten_ = LastWritten::KeywordOrIdentifier;
		} else if (std::holds_alternative<std::int32_t>(value) ||
				   std::holds_alternative<std::uint32_t>(value) ||
				   std::holds_alternative<std::int64_t>(value) ||
				   std::holds_alternative<std::uint64_t>(value)) {
			// The C# `else if (value is IFormattable)` branch -- the integer types. Needs a space
			// if an identifier follows the number (avoids mistaking it as a type suffix).
			lastWritten_ = LastWritten::KeywordOrIdentifier;
		} else {
			// `AnyValueTag` (the pattern wildcard) is not a real literal; it is never written by
			// the output visitor, but the fall-through is faithful to the C# `else` branch.
			lastWritten_ = LastWritten::Other;
		}
	}

	// The C# `public override void WritePrimitiveType(string type)` -- a primitive-type keyword
	// needs a preceding space after a keyword or identifier; `new` is special (it can be followed
	// by `()` as a target-typed default), so it is `Other`, every other primitive type is a
	// keyword-or-identifier for the trailing-space purposes.
	void WritePrimitiveType(std::string_view type) override {
		if (lastWritten_ == LastWritten::KeywordOrIdentifier) {
			Space();
		}
		DecoratingTokenWriter::WritePrimitiveType(type);
		if (type == "new") {
			lastWritten_ = LastWritten::Other;
		} else {
			lastWritten_ = LastWritten::KeywordOrIdentifier;
		}
	}

private:
	// The C# `enum LastWritten` -- the kind of the most recently written token, tracked so the
	// decorator can decide whether the next token needs a separating space.
	enum class LastWritten {
		Whitespace,
		Other,
		KeywordOrIdentifier,
		Plus,
		Minus,
		Ampersand,
		QuestionMark,
		Division
	};

	LastWritten lastWritten_ = LastWritten::Whitespace;
};

}  // namespace OutputVisitor
}  // namespace ILSpy::Decompiler::CSharp

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_INSERTREQUIREDSPACESDECORATOR_HPP
