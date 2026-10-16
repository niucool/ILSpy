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

// Port of the `Comment` concrete node in ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/
// Comment.cs (the generated `Comment.g.cs` + the hand-written partial). The next in-order
// Phase-5 piece per the D287 plan ("the remaining GeneralScope and Expression C# AST nodes ...
// Comment ..."), and the FIRST concrete `Trivia`-derived node (the `Trivia` abstract base was
// ported in D224; `Comment`/`PreProcessorDirective` are the concrete trivia the output visitor
// emits verbatim). `comment ::= '//' input_character* | '/*' input_character* '*/'` (C# lexical
// grammar 6.3.3): a sealed `Trivia` leaf with NO `[Slot]` children (so the inherited zero-child
// slot-storage defaults apply) carrying two scalar instance properties -- a `CommentType` enum
// (the comment style) and a `string Content` (the comment text).
//
// The generator emits the `AcceptVisitor` override calling `visitor.VisitComment(this)` and the
// `DoMatch` over `MembersToMatch` (the generator's `WriteDoMatch`): the `CommentType` property is
// a settable enum-typed scalar the generator adds to `MembersToMatch` (with the PLAIN equality
// term -- `CommentType` declares NO `Any` member, so the generator's `hasAny` path does NOT
// fire and the term is `this.CommentType == o.CommentType`, the `DirectionExpression.Field
// Direction` D235 / `ReferenceKind` D278 no-`Any`-enum precedent); the `Content` property is
// a non-`[Slot]` public `string` property the generator adds to `MembersToMatch` as a `String`
// term (the `MatchString` call, the `Identifier.Name` D227 precedent -- the `$any$` wildcard in
// the pattern's `Content` matches any candidate content). The generated `DoMatch` is
// `return other is Comment o && this.CommentType == o.CommentType && MatchString(this.Content,
// o.Content)` (the scalars appear in source declaration order: `CommentType` before `Content`).
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it
// (no `MemberwiseClone`): copies the two scalars, the `Trivia` source span (via the public
// `SetStartLocation`/`SetEndLocation` -- the `Trivia` base's `startLocation_`/`endLocation_`
// are private to `Trivia`), and the annotation channel (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern). No children to deep-copy.
//
// C++ name-shadowing crux (the `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.
// OperatorType` D280 precedent applied to a `Trivia`-derived leaf): the C# `CommentType` property
// is `CommentType` of type `CommentType` -- a property named the same as its enum type, the enum
// equivalent of the `Expression`-of-type-`Expression` crux. The faithful port names the accessor
// `CommentType()`, which SHADOWS the `CommentType` enum in this class scope (C++ unqualified
// name lookup finds the member and stops, even though it is not a type). The elaborated
// type-specifier for an enum is `enum CommentType` (basic.lookup.elab ignores non-type names, so
// it finds the hidden enum); every type usage AFTER the `CommentType()` getter is declared (the
// setter parameter, the backing field type) uses `enum CommentType`. The getter return type and
// the ctor parameters precede the getter's declaration (the ctors are declared before the
// getter), so they use the plain `CommentType` (the enum is unshadowed there -- the member
// function is not in scope yet). The backing-field initializer uses the fully-qualified enum
// name (`::ILSpy::...::CommentType::SingleLine`) because the bare `CommentType::SingleLine`
// would resolve the unqualified `CommentType` to the member function and reject `::SingleLine`
// on a non-type. The `Comment(CommentType)` ctor body assigns the backing field directly (not
// the setter `CommentType(type)`) because that call is AMBIGUOUS -- it could be the setter or a
// functional cast of the enum (the `DirectionExpression.FieldDirection` D235 precedent). The
// `Content` property is a `string` named `Content`; no class named `Content` lives in the
// `Syntax` namespace, so the `Content()` accessor shadows nothing and no elaborated specifier
// is needed there.
//
// The generated ctors (the generator's `WriteConstructors`): `CtorParams` is `[CommentType]`
// (the only settable enum-typed scalar; `Content` is a `string`, NOT an enum and NOT a `[Slot]`,
// so it is NOT in `CtorParams` -- the D270 "only settable ENUM-typed scalars" rule).
// `RequiredConstructorPrefixLength` is 1 (`CommentType` is required), so `ConstructorPrefixLengths`
// is `{1}` -- the generated ctors are the empty ctor + the `(CommentType)` ctor. The hand-written
// partial adds two more: `Comment(string content, CommentType type = CommentType.SingleLine)`
// (sets both) and `Comment(CommentType commentType, TextLocation startLocation, TextLocation
// endLocation)` (delegates to the `Trivia(startLocation, endLocation)` base ctor, sets
// `CommentType`). The `(CommentType)` and `(string, CommentType = SingleLine)` ctors do NOT
// overload ambiguously: `Comment(CommentType::MultiLine)` selects the `(CommentType)` ctor (the
// `string` first param of the hand-written ctor does not accept a `CommentType`), and
// `Comment("x")` selects the hand-written ctor (`CommentType` does not convert to `string`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_COMMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_COMMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"

#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum CommentType` -- the style of a `Comment`. `SingleLine` is the zero value
// (the C# default for an uninitialized `CommentType` property -- a `//` comment). Unlike
// `UnaryOperatorType`/`BinaryOperatorType`/`AssignmentOperatorType`, this enum declares NO `Any`
// member, so the generator's `hasAny` path does NOT fire and the generated `DoMatch` term is the
// plain `this.CommentType == o.CommentType` (no `Any`-wildcard). A `std::uint8_t`-based
// `enum class` (the C# `enum CommentType` defaults to `int`; the underlying width is not
// load-bearing here, so the default `int`-sized `enum class` is faithful -- the
// `FieldDirection` D235 precedent).
enum class CommentType {
    // `"//"` comment (the zero value, the C# default).
    SingleLine,
    // `"/* */"` comment.
    MultiLine,
    // `"///"` documentation comment.
    Documentation,
    // Inactive code (code in a non-taken `#if`).
    InactiveCode,
    // `"/** */"` documentation comment.
    MultiLineDocumentation
};

// The C# `public sealed partial class Comment : Trivia`. `final` (the C# `sealed`): no further
// derivation. A leaf `Trivia` (no `[Slot]` children) carrying a `CommentType` enum scalar and
// a `Content` string -- the first concrete `Trivia`-derived node.
class Comment final : public Trivia {
public:
    ~Comment() override = default;

    // The generated empty ctor (the C# `public Comment()`). `CommentType` defaults to
    // `SingleLine` (the enum's zero value); `Content` defaults to the empty string. A
    // default-constructed `Comment` is a valid leaf (no required slots).
    Comment() = default;

    // The generated `(CommentType)` ctor (the C# `public Comment(CommentType commentType)`).
    // `CommentType` is the only `CtorParams` entry (a settable enum); the scalar is assigned
    // directly to the backing field (not via the setter) because the setter call
    // `CommentType(type)` is ambiguous with a functional cast of the enum (the D235 crux). The
    // parameter type precedes the `CommentType()` getter (the ctor is declared before the
    // getter), so the plain `CommentType` (the enum) is unshadowed here. A single-argument ctor
    // is a converting ctor by default, so `explicit` blocks the implicit
    // `Comment c = CommentType::MultiLine;` form.
    explicit Comment(CommentType type) : Comment() {
        commentType_ = type;
    }

    // The hand-written `Comment(string content, CommentType type = CommentType.SingleLine)`
    // (the C# convenience ctor that sets both the content and the type). The `type` default
    // value uses the fully-qualified enum name because the bare `CommentType::SingleLine`
    // would resolve the unqualified `CommentType` to the `CommentType()` member function in
    // complete-class scope (the field-initializer D235 precedent). The first parameter is
    // `std::string` (the `Content` value); the body assigns both backing fields directly.
    Comment(std::string content, CommentType type = ::ILSpy::Decompiler::CSharp::Syntax::CommentType::SingleLine)
        : Comment() {
        commentType_ = type;
        content_ = std::move(content);
    }

    // The hand-written `Comment(CommentType commentType, TextLocation startLocation,
    // TextLocation endLocation) : base(startLocation, endLocation)` (the C# ctor that records
    // the source span). Delegates to the `Trivia(startLocation, endLocation)` base ctor (which
    // sets the `Trivia` location pair), then assigns the `CommentType` backing field directly
    // (the D235 crux). The `CommentType` parameter type precedes the `CommentType()` getter,
    // so the plain `CommentType` (the enum) is unshadowed here.
    Comment(CommentType commentType, TextLocation startLocation, TextLocation endLocation)
        : Trivia(startLocation, endLocation) {
        commentType_ = commentType;
    }

    // The C# `public CommentType CommentType { get; set; }` -- a scalar enum (NOT a `[Slot]`).
    // A settable enum-typed scalar the generator adds to `MembersToMatch` (with the PLAIN
    // equality term, since `CommentType` has NO `Any` member) and to the ctor params. The return
    // type precedes the getter's own declaration, so the plain `CommentType` (the enum) is
    // unshadowed in the getter signature.
    CommentType CommentType() const { return commentType_; }
    // The setter parameter type uses the elaborated enum specifier `enum CommentType`: the
    // `CommentType()` getter declared just above shadows the `CommentType` enum in this class
    // scope, so the plain name would resolve to the member function (not a type).
    void CommentType(enum CommentType value) { commentType_ = value; }

    // The C# `public string Content { get; set; } = string.Empty;` -- the comment text. A plain
    // settable instance `string` property (NOT a `[Slot]`, NOT an enum -- the generator adds it
    // to `MembersToMatch` as a `String` `MatchString` term). Defaults to the empty string (the
    // C# `= string.Empty` initializer; `std::string`'s default ctor produces the empty string).
    // No name-shadowing crux (no class named `Content` lives in the `Syntax` namespace), so the
    // plain `std::string` is used throughout.
    const std::string& Content() const { return content_; }
    void Content(std::string value) { content_ = std::move(value); }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitComment`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitComment(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitComment`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitComment(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is Comment o && this.CommentType == o.CommentType && MatchString(
    // this.Content, o.Content)`. A type match, a plain-enum equality (no `Any`-wildcard, since
    // `CommentType` declares no `Any`), and a `MatchString` on `Content` (the `$any$` wildcard
    // `Pattern::AnyString` in the pattern's `Content` matches any candidate content). The
    // terms are in source declaration order (`CommentType` before `Content`); `match` is unused
    // (no captures, no recursive child match). `CommentType` is compared via the backing fields
    // directly (no type name appears, so no elaborated specifier is needed there).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<Comment*>(other);
        if (o == nullptr)
            return false;
        return commentType_ == o->commentType_
            && PatternMatching::Pattern::MatchString(std::string_view(content_),
                                                     std::string_view(o->content_));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node with the `CommentType` scalar and `Content` string
    // copied, the `Trivia` source span copied (via the public `SetStartLocation`/
    // `SetEndLocation` -- the `Trivia` base's `startLocation_`/`endLocation_` are private to
    // `Trivia`, so the public surface is the only way to copy them from a derived node), and
    // the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). No children to deep-copy (a leaf). The covariant return is
    // `Comment*` (through `AstNode*`, the `AstNode::Clone` virtual -- `Trivia` redeclares no
    // typed `Clone`, so the override returns through the base).
    Comment* Clone() const override {
        auto* node = new Comment();
        node->commentType_ = commentType_;
        node->content_ = content_;
        node->SetStartLocation(StartLocation());
        node->SetEndLocation(EndLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field for the `CommentType` scalar uses the elaborated enum specifier
    // `enum CommentType` (the `CommentType()` accessor declared above shadows the enum in this
    // class scope); the initializer uses the fully-qualified enum name because the bare
    // `CommentType::SingleLine` would resolve the unqualified `CommentType` to the member
    // function and reject `::SingleLine` on a non-type. `SingleLine` is the enum's zero value
    // (the C# default). The `Content` backing field is a plain `std::string` (no shadowing);
    // it defaults to the empty string (the C# `= string.Empty`).
    enum CommentType commentType_ = ::ILSpy::Decompiler::CSharp::Syntax::CommentType::SingleLine;
    std::string content_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_COMMENT_HPP
