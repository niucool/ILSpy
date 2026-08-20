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

// Port of the `PreProcessorDirective` family in ICSharpCode.Decompiler/CSharp/Syntax/
// GeneralScope/PreProcessorDirective.cs (the hand-written partial -- the generated
// `PreProcessorDirective.g.cs` carries only the `AcceptVisitor` override, since the source
// declares its own `DoMatch` and has NO `[Slot]` children). The next in-order Phase-5 piece
// per the D293 plan ("the remaining GeneralScope nodes ... PreProcessorDirective the concrete
// Trivia family with its LinePreprocessorDirective and PragmaWarningPreprocessorDirective
// sealed derived classes ..."). A connected grammatical family (the `pp_directive ::= '#'
// pp_kind new_line` lexical production, C# grammar 6.5.1) with a strict dependency chain
// (PreProcessorDirective -> the two sealed derived classes), ported together as one in-order
// piece (the goto D257 / switch D268 / try-catch D269 family precedent).
//
// `PreProcessorDirective : Trivia` -- the abstract-by-shape base of the family. NOT sealed in
// C# (the two sealed `LinePreprocessorDirective`/`PragmaWarningPreprocessorDirective` derive
// from it), so the port does NOT use `final` (the ArrayInitializerExpression D250
// not-sealed-concrete-node precedent). A leaf `Trivia` with NO `[Slot]` children (so the
// inherited zero-child slot-storage defaults apply) carrying two scalar instance properties:
// a `PreProcessorDirectiveType` enum (the directive kind) and a `string? Argument` (the
// optional rest-of-line text). The `DoMatch` is HAND-WRITTEN in the source (the generator's
// `WriteDoMatch` skips it -- `!targetSymbol.MemberNames.Contains("DoMatch")` is false, so
// `MembersToMatch` stays null): `return o != null && Type == o.Type && MatchString(Argument,
// o.Argument)`. The two sealed derived classes inherit this `DoMatch` unchanged (they do NOT
// add to it -- a `PragmaWarningPreprocessorDirective` pattern still matches ONLY on `Type` +
// `Argument`, NOT on its `Warnings` collection; this is the hand-written behavior, faithful to
// the C#).
//
// The generator emits only the `AcceptVisitor` override (calling
// `visitor.VisitPreProcessorDirective(this)`) for `PreProcessorDirective` (NeedsVisitor is
// true -- the base `Trivia` is abstract); the two sealed derived classes get NO `AcceptVisitor`
// override (their base `PreProcessorDirective` is concrete, so NeedsVisitor is false) and NO
// `Visit` method on `IAstVisitor` (the whole family dispatches polymorphically through the one
// `VisitPreProcessorDirective(PreProcessorDirective*)` -- the generator's `NeedsVisitor`
// logic). The `PreProcessorDirectiveType` enum lives at namespace scope (the `CommentType` D288
// precedent).
//
// C++ name-shadowing crux (none here): the `Type` property is `PreProcessorDirectiveType` --
// the accessor `Type()` does NOT share the enum's name (unlike `Comment.CommentType` of type
// `CommentType` D288), so NO elaborated enum specifier is needed; and no class named `Type`
// lives in the `Syntax` namespace (there is `AstType`, not `Type` -- the Attribute D240
// lesson), so `Type()` shadows nothing. The `Argument` property is `string?`; no class named
// `Argument` lives in `Syntax`, so `Argument()` shadows nothing. The cleanest `Trivia`-derived
// port: no elaborated specifier anywhere.
//
// Clone crux (the not-sealed-hierarchy + no-MemberwiseClone interaction): the C# `Clone` is
// inherited from `AstNode` (`MemberwiseClone` + `CloneChildrenInto`), and `MemberwiseClone`
// preserves the RUNTIME type, so cloning a `LinePreprocessorDirective` returns a
// `LinePreprocessorDirective` (not a sliced `PreProcessorDirective`). The port has no
// `MemberwiseClone` (the D223 per-concrete-node-Clone pattern), so each concrete class in the
// hierarchy MUST override `Clone` to create its own concrete type -- otherwise the virtual
// dispatch to the base `PreProcessorDirective::Clone` would create a `PreProcessorDirective`
// (slicing off the `LinePreprocessorDirective`/`PragmaWarningPreprocessorDirective` runtime
// type, and for the latter losing the `Warnings` deep-copy). `PreProcessorDirective::Clone`
// is therefore VIRTUAL (overriding `AstNode::Clone`) so the derived classes override it with
// covariant returns; each copies the `Type`/`Argument` scalars + the `Trivia` source span
// (via the public `SetStartLocation`/`SetEndLocation` -- the `Trivia` base's
// `startLocation_`/`endLocation_` are private to `Trivia`, the Comment D288 precedent) + the
// annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PREPROCESSORDIRECTIVE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PREPROCESSORDIRECTIVE_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum PreProcessorDirectiveType : byte` -- the kind of a preprocessor
// directive. `Invalid` is the zero value (the C# default for an uninitialized
// `PreProcessorDirectiveType` property). This enum declares NO `Any` member, so the
// hand-written `DoMatch` term is the plain `Type == o.Type` (no `Any`-wildcard). A
// `std::uint8_t`-based `enum class` (the C# `: byte`).
enum class PreProcessorDirectiveType : std::uint8_t {
    Invalid = 0,
    Region = 1,
    Endregion = 2,
    If = 3,
    Endif = 4,
    Elif = 5,
    Else = 6,
    Define = 7,
    Undef = 8,
    Error = 9,
    Warning = 10,
    Pragma = 11,
    Line = 12
};

// The C# `public partial class PreProcessorDirective : Trivia` (NOT sealed). A leaf `Trivia`
// (no `[Slot]` children) carrying a `PreProcessorDirectiveType` scalar and a nullable
// `Argument` string -- the base of the preprocessor-directive family. The two sealed derived
// classes (`LinePreprocessorDirective`, `PragmaWarningPreprocessorDirective`) inherit its
// `DoMatch`/`AcceptVisitor` and override `Clone` (the per-concrete-node-Clone pattern).
class PreProcessorDirective : public Trivia {
public:
    ~PreProcessorDirective() override = default;

    // A port-only default ctor (the C# has no parameterless ctor -- the two hand-written
    // ctors are the only public construction surface). Needed for `Clone` (a fresh node the
    // scalars/span/annotations are copied onto) and for tests that build a default directive;
    // `Type` defaults to `Invalid` (the enum's zero value) and `Argument` to `nullopt`. The
    // Comment D288 `Comment() = default;` precedent (a port convenience, not a faithful C#
    // ctor).
    PreProcessorDirective() = default;

    // The hand-written `PreProcessorDirective(PreProcessorDirectiveType type, TextLocation
    // startLocation, TextLocation endLocation) : base(startLocation, endLocation)` (the C# ctor
    // that records the source span). Delegates to the `Trivia(startLocation, endLocation)`
    // base ctor (which sets the `Trivia` location pair), then assigns the `Type` backing field
    // directly.
    PreProcessorDirective(PreProcessorDirectiveType type, TextLocation startLocation,
                          TextLocation endLocation)
        : Trivia(startLocation, endLocation) {
        type_ = type;
    }

    // The hand-written `PreProcessorDirective(PreProcessorDirectiveType type, string?
    // argument = null)` (the C# ctor that sets the directive kind and the optional rest-of-line
    // argument). The `Type`/`Argument` backing fields are assigned directly (the `Argument`
    // default is `nullopt`, the C# `null`).
    PreProcessorDirective(PreProcessorDirectiveType type,
                          std::optional<std::string> argument = std::nullopt) {
        type_ = type;
        argument_ = std::move(argument);
    }

    // The C# `public PreProcessorDirectiveType Type { get; set; }` -- the directive kind. The
    // accessor `Type()` does NOT share the enum's name (`PreProcessorDirectiveType`), so no
    // elaborated enum specifier is needed (the Comment D288 `CommentType`-shadows-`CommentType`
    // crux does NOT apply -- the property and the enum have different names).
    PreProcessorDirectiveType Type() const { return type_; }
    void Type(PreProcessorDirectiveType value) { type_ = value; }

    // The C# `public string? Argument { get; set; }` -- the optional rest-of-line text after
    // the directive keyword (e.g. the `#if` condition expression, the `#define` symbol). A
    // nullable string -> `std::optional<std::string>`. No name-shadowing crux (no class named
    // `Argument` lives in `Syntax`).
    const std::optional<std::string>& Argument() const { return argument_; }
    void Argument(std::optional<std::string> value) { argument_ = std::move(value); }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitPreProcessorDirective`. The two sealed derived classes inherit this
    // (they get no `AcceptVisitor` override of their own -- the generator's `NeedsVisitor`
    // logic), so visiting a `LinePreprocessorDirective`/`PragmaWarningPreprocessorDirective`
    // dispatches through this to `VisitPreProcessorDirective`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitPreProcessorDirective(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitPreProcessorDirective`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitPreProcessorDirective(this);
    }

    // ---- DoMatch (the HAND-WRITTEN pattern match, NOT generated) -------------------------
    // The C# `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `PreProcessorDirective? o = other as PreProcessorDirective; return o != null &&
    // Type == o.Type && MatchString(Argument, o.Argument)`. A subtype-accepting type match
    // (`other as PreProcessorDirective` accepts `LinePreprocessorDirective`/
    // `PragmaWarningPreprocessorDirective` too -- the inherited `DoMatch` is the family's
    // sole pattern match), a plain-enum equality on `Type` (no `Any`-wildcard), and a
    // `MatchString` on the nullable `Argument` (the `$any$` wildcard `Pattern::AnyString`
    // in the pattern's `Argument` matches any candidate argument; a `nullopt` pattern
    // matches only a `nullopt` candidate). `match` is unused (no captures, no recursive
    // child match). The `Argument` is `std::optional<std::string>`; `Pattern::MatchString`
    // takes `std::optional<std::string_view>`, so the view is built per side (nullopt passes
    // through as the C# null). The two sealed derived classes inherit this unchanged (a
    // `PragmaWarningPreprocessorDirective` pattern does NOT match its `Warnings` collection).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<PreProcessorDirective*>(other);
        if (o == nullptr)
            return false;
        auto thisArg = argument_;
        auto otherArg = o->argument_;
        return type_ == o->type_
            && PatternMatching::Pattern::MatchString(
                   thisArg ? std::optional<std::string_view>(*thisArg) : std::nullopt,
                   otherArg ? std::optional<std::string_view>(*otherArg) : std::nullopt);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh `PreProcessorDirective` with the `Type`
    // scalar and `Argument` string copied, the `Trivia` source span copied (via the public
    // `SetStartLocation`/`SetEndLocation` -- the `Trivia` base's `startLocation_`/
    // `endLocation_` are private to `Trivia`), and the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern). No
    // children to deep-copy (a leaf). VIRTUAL (not sealed, derived classes override it with
    // covariant returns to avoid slicing -- the per-concrete-node-Clone pattern for a
    // not-sealed hierarchy). The covariant return is `PreProcessorDirective*` (through
    // `AstNode*`, the `AstNode::Clone` virtual -- `Trivia` redeclares no typed `Clone`).
    virtual PreProcessorDirective* Clone() const override {
        auto* node = new PreProcessorDirective();
        node->type_ = type_;
        node->argument_ = argument_;
        node->SetStartLocation(StartLocation());
        node->SetEndLocation(EndLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is a plain `PreProcessorDirectiveType` (the `Type()`
    // accessor does not shadow the enum, so no elaborated specifier); it defaults to
    // `Invalid` (the enum's zero value, the C# default). `argument_` is a
    // `std::optional<std::string>` (the C# `string?`), defaulting to `nullopt`.
    PreProcessorDirectiveType type_ = PreProcessorDirectiveType::Invalid;
    std::optional<std::string> argument_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PREPROCESSORDIRECTIVE_HPP
