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

// Port of the `TokenWriter` abstract base, the `ILocatable` interface, and the
// `DecoratingTokenWriter` abstract base in
// ICSharpCode.Decompiler/CSharp/OutputVisitor/ITokenWriter.cs -- the foundation of the C#
// output stage (Phase 5, section 5.2: the output stage lands after the C# AST node hierarchy,
// which the D315 decision-log entry confirmed is now complete). `CSharpOutputVisitor` (the
// pretty-printer that implements a `Visit` method per ported node) renders an `AstNode` tree to
// text by driving a `TokenWriter` through a stream of `StartNode`/`WriteKeyword`/`WriteToken`/
// `WriteIdentifier`/`Space`/`NewLine`/`EndNode`/... calls; the concrete `TextWriterTokenWriter`
// writes the tokens to an `std::ostream`, while two decorators (`InsertRequiredSpacesDecorator`,
// `InsertMissingTokensDecorator`) sit between the output visitor and the concrete writer to
// insert the spaces the visitor omits and to record source spans back onto the AST nodes. This
// header is the interface the concrete writers and the decorators plug into, exactly mirroring
// the role `IAstVisitor` plays for the visitor dispatch (the D225 precedent -- the abstract
// interface lands first, the concrete implementations follow).
//
// The C# `ITokenWriter.cs` also declares four static factory methods on `TokenWriter`
// (`Create`, `CreateWriterThatSetsLocationsInAST`, `InsertRequiredSpaces`,
// `WrapInWriterThatSetsLocationsInAST`) that construct the concrete writer + decorator stacks.
// Those factories depend on the concrete `TextWriterTokenWriter` /
// `InsertRequiredSpacesDecorator` / `InsertMissingTokensDecorator` types, which are NOT yet
// ported, so the factories are DEFERRED (the value-vs-behaviour / dependency discriminator: a
// factory that references an unported concrete type lands with that type -- the same
// deferral pattern as the deferred hand-written ctors that call unported helpers throughout the
// AST node hierarchy, e.g. the D289 UsingDeclaration `ConstructNamespace` helper). The
// concrete writer and the two decorators land next as separate in-order pieces; once they are
// ported, the four factories can be added.
//
// C#-to-C++ porting decisions:
//  * `abstract class TokenWriter` -> a C++ abstract base with a virtual destructor and 15
//    pure-virtual methods (one per C# abstract method).
//  * `AstNode`/`Identifier` parameters -> raw non-owning pointers (`AstNode*`/`Identifier*`),
//    the established non-owning-pointer convention (the C# passes the reference types by
//    reference; the location-setting decorator mutates the node, so the pointer is non-`const`).
//    `AstNode` is made complete by the transitive includes (the `Comment`/`PreProcessorDirective`
//    /`PrimitiveExpression` headers all pull in `AstNode.hpp`); `Identifier` is left
//    forward-declared -- only a pointer parameter is needed here.
//  * `string` read-only parameters -> `std::string_view` (zero-copy; accepts the `const char*`
//    keyword/token literals the AST nodes carry as `static constexpr const char*` without an
//    allocation, the established read-only-string convention -- the `Pattern::MatchString`
//    `std::optional<std::string_view>` precedent).
//  * `object? value` of `WritePrimitiveValue` -> `const PrimitiveValue&` (the port's faithful
//    boxed-literal `std::variant` defined in `PrimitiveExpression.hpp`, the D228 design -- the
//    `std::monostate` alternative is the C# `null`). The `LiteralFormat format` default argument
//    (`LiteralFormat.None`) is kept verbatim (a default argument on a pure-virtual is legal C++;
//    it is resolved at the static call-site type, which is faithful for the call-through-base
//    shape the output visitor uses).
//  * `string? argument` of `WritePreProcessorDirective` -> `std::optional<std::string_view>`
//    (the C# nullable string; `std::nullopt` is the null argument, a non-empty view is the
//    directive's argument text).
//  * `interface ILocatable` -> a C++ abstract base with a virtual destructor and two
//    pure-virtual `const` accessors (`Location()` returns `TextLocation` by value, `Length()`
//    returns `int`).
//  * `abstract class DecoratingTokenWriter : TokenWriter` -> a C++ abstract base (made
//    abstract by a pure-virtual destructor with an inline `= default` definition, the idiomatic
//    C++ "abstract but every method implemented" mirror of the C# `abstract` class) that stores
//    a non-owning `TokenWriter*` and forwards every method. The ctor null-checks its argument
//    (the C# `ArgumentNullException` -> `std::invalid_argument`).

#ifndef ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_TOKENWRITER_HPP
#define ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_TOKENWRITER_HPP

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "Decompiler/CSharp/Syntax/Comment.hpp"                       // CommentType
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"  // LiteralFormat, PrimitiveValue
#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"         // PreProcessorDirectiveType
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"                   // TextLocation (ILocatable::Location)

namespace ILSpy::Decompiler::CSharp {

// `Identifier` is passed to `WriteIdentifier` by pointer only; the concrete writers that need
// to read its `Name`/`IsVerbatim` include `Identifier.hpp` themselves. (`AstNode` is already
// complete via the transitive includes above, so it is used unqualified without a forward
// declaration here.)
namespace Syntax { class Identifier; }

namespace OutputVisitor {

// The `Syntax` types the token-writer interface is expressed in, brought into the
// `OutputVisitor` namespace so the method signatures read unqualified.
using Syntax::AstNode;
using Syntax::Identifier;
using Syntax::CommentType;
using Syntax::PreProcessorDirectiveType;
using Syntax::LiteralFormat;
using Syntax::PrimitiveValue;
using Syntax::TextLocation;

// The C# `public abstract class TokenWriter` -- the sink the output visitor drives a C# AST
// tree through. One pure-virtual method per C# abstract method; a virtual destructor makes the
// polymorphic deletion of the concrete writers and decorators safe.
class TokenWriter {
public:
	virtual ~TokenWriter() = default;

	// The C# `public abstract void StartNode(AstNode node)` / `EndNode(AstNode node)` -- bracket
	// the tokens of a single AST node (the location-setting decorator records the node's
	// source span between them).
	virtual void StartNode(AstNode* node) = 0;
	virtual void EndNode(AstNode* node) = 0;

	// The C# `public abstract void WriteIdentifier(Identifier identifier)` -- an identifier
	// token (the concrete writer reads `Name`/`IsVerbatim`).
	virtual void WriteIdentifier(Identifier* identifier) = 0;

	// The C# `public abstract void WriteKeyword(string keyword)` / `WriteToken(string token)` /
	// `WritePrimitiveType(string type)` / `WriteInterpolatedText(string text)` -- a keyword, a
	// punctuation/operator token, a primitive-type keyword, or a run of interpolated-string
	// text. The string parameters are read-only, so `std::string_view` (zero-copy).
	virtual void WriteKeyword(std::string_view keyword) = 0;
	virtual void WriteToken(std::string_view token) = 0;
	virtual void WritePrimitiveType(std::string_view type) = 0;
	virtual void WriteInterpolatedText(std::string_view text) = 0;

	// The C# `public abstract void WritePrimitiveValue(object? value, LiteralFormat format =
	// LiteralFormat.None)` -- a literal value (the boxed C# literal ports as the
	// `PrimitiveValue` variant; `std::monostate` is the C# `null`) plus its lexical-format hint.
	virtual void WritePrimitiveValue(const PrimitiveValue& value, LiteralFormat format = LiteralFormat::None) = 0;

	// The C# `public abstract void Space()` / `Indent()` / `Unindent()` / `NewLine()` -- the
	// whitespace/indentation controls.
	virtual void Space() = 0;
	virtual void Indent() = 0;
	virtual void Unindent() = 0;
	virtual void NewLine() = 0;

	// The C# `public abstract void WriteComment(CommentType commentType, string content)` --
	// a comment (the `CommentType` style plus the comment text).
	virtual void WriteComment(CommentType commentType, std::string_view content) = 0;

	// The C# `public abstract void WritePreProcessorDirective(PreProcessorDirectiveType type,
	// string? argument)` -- a preprocessor directive (the `PreProcessorDirectiveType` kind
	// plus an optional argument text; `std::nullopt` is the C# `null` argument).
	virtual void WritePreProcessorDirective(PreProcessorDirectiveType type, std::optional<std::string_view> argument) = 0;
};

// The C# `public interface ILocatable` -- a writer (or decorator) that can report the current
// source location and the length written so far, used by the location-setting decorator to
// record spans back onto the AST nodes. A C++ abstract base with two pure-virtual `const`
// accessors (the C# properties are get-only).
class ILocatable {
public:
	virtual ~ILocatable() = default;
	virtual TextLocation Location() const = 0;
	virtual int Length() const = 0;
};

// The C# `public abstract class DecoratingTokenWriter : TokenWriter` -- the base for the
// decorator writers (`InsertRequiredSpacesDecorator`, `InsertMissingTokensDecorator`) that
// intercept a few methods and forward the rest to a wrapped `TokenWriter`. The C# is
// `abstract` to prevent direct instantiation (a decorator must override something); the C++
// port mirrors that with a pure-virtual destructor (the idiomatic "abstract but every method
// implemented" pattern -- the destructor is the only pure-virtual, and it has an inline
// `= default` definition so subclasses can destroy). Every method forwards to the wrapped
// writer, so a decorator that overrides a single method inherits the pass-through for the rest.
class DecoratingTokenWriter : public TokenWriter {
public:
	// The C# ctor throws `ArgumentNullException` on a null decorated writer.
	explicit DecoratingTokenWriter(TokenWriter* decoratedWriter)
		: decoratedWriter_(decoratedWriter) {
		if (decoratedWriter_ == nullptr) {
			throw std::invalid_argument("DecoratingTokenWriter: decoratedWriter must not be null");
		}
	}

	~DecoratingTokenWriter() override = 0;

	void StartNode(AstNode* node) override { decoratedWriter_->StartNode(node); }
	void EndNode(AstNode* node) override { decoratedWriter_->EndNode(node); }
	void WriteIdentifier(Identifier* identifier) override { decoratedWriter_->WriteIdentifier(identifier); }
	void WriteKeyword(std::string_view keyword) override { decoratedWriter_->WriteKeyword(keyword); }
	void WriteToken(std::string_view token) override { decoratedWriter_->WriteToken(token); }
	void WritePrimitiveType(std::string_view type) override { decoratedWriter_->WritePrimitiveType(type); }
	void WriteInterpolatedText(std::string_view text) override { decoratedWriter_->WriteInterpolatedText(text); }
	void WritePrimitiveValue(const PrimitiveValue& value, LiteralFormat format) override { decoratedWriter_->WritePrimitiveValue(value, format); }
	void Space() override { decoratedWriter_->Space(); }
	void Indent() override { decoratedWriter_->Indent(); }
	void Unindent() override { decoratedWriter_->Unindent(); }
	void NewLine() override { decoratedWriter_->NewLine(); }
	void WriteComment(CommentType commentType, std::string_view content) override { decoratedWriter_->WriteComment(commentType, content); }
	void WritePreProcessorDirective(PreProcessorDirectiveType type, std::optional<std::string_view> argument) override { decoratedWriter_->WritePreProcessorDirective(type, argument); }

protected:
	TokenWriter* decoratedWriter_;
};

// The pure-virtual destructor's definition -- required so subclasses can destroy, and kept
// inline (header-only) to match the rest of the output-stage port.
inline DecoratingTokenWriter::~DecoratingTokenWriter() = default;

}  // namespace OutputVisitor
}  // namespace ILSpy::Decompiler::CSharp

#endif  // ILSPY_DECOMPILER_CSHARP_OUTPUTVISITOR_TOKENWRITER_HPP
