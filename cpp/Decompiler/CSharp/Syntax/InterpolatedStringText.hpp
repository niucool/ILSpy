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

// Port of the `InterpolatedStringText` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.cs (the
// generated `InterpolatedStringText.g.cs` + the hand-written partial). The next in-order
// Phase-5 piece per the D308 plan (the second of the two `InterpolatedStringContent` concrete
// subclasses, alongside `Interpolation`). `interpolated_string_text ::= text_character+`
// (C# lexical grammar 12.8.3): the literal-text run between interpolations inside an
// interpolated string -- an `InterpolatedStringContent` leaf with NO `[Slot]` children and a
// single settable `string Text` property (initialized to `string.Empty`).
//
// Because it declares NO `[Slot]` children, the generator's `WriteConstructors` returns early
// (`if (... slots.Count == 0 ...) return;`) and emits NO generated ctors (not even the empty
// one) -- the hand-written `InterpolatedStringText()` and `InterpolatedStringText(string
// text)` are the only ctors. The port carries both. The single-arg `(std::string)` ctor is
// `explicit` (the D245 `TypeReferenceExpression` precedent: a single-argument ctor is a
// converting ctor by default, so `explicit` blocks an implicit `std::string` ->
// `InterpolatedStringText` conversion).
//
// The generated `DoMatch` (over `MembersToMatch`) has a single term: `Text` is a settable
// `string` property (not a `[Slot]`, not `CSharpTokenNode`/`TextLocation`-typed), so the
// per-property scan adds it (the `String` branch) and `DoMatchTerm` emits
// `MatchString(this.Text, o.Text)`. So `MembersToMatch` is `[Text]` and the generated `DoMatch`
// is `return other is InterpolatedStringText o && MatchString(this.Text, o.Text)`. The port
// constructs `std::string_view(text_)` per `MatchString` argument (the D227 `Identifier`
// precedent: a bare `std::string` needs an explicit `std::string_view` construction -- one
// user-defined conversion per argument, since `std::string`->`std::string_view`->
// `std::optional` is two and C++ allows only one implicitly). The generated `AcceptVisitor`
// calls `visitor.VisitInterpolatedStringText(this)`. There is NO generated slot static (no
// `[Slot]`), so the node inherits the `AstNode` zero-child slot defaults (`GetChildCount` 0,
// `GetChild`/`SetChild`/`GetChildSlotInfo` throw).
//
// NO C++ name-shadowing crux: the `Text()` accessor is a member function, but no class named
// `Text` lives in the `Syntax` namespace, so no elaborated-type-specifier is needed anywhere.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATEDSTRINGTEXT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATEDSTRINGTEXT_HPP

#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class InterpolatedStringText : InterpolatedStringContent`.
// `final` (the C# `sealed`): no further derivation. The second concrete
// `InterpolatedStringContent` subclass -- a leaf with no `[Slot]` children.
class InterpolatedStringText final : public InterpolatedStringContent {
public:
    ~InterpolatedStringText() override = default;

    // The hand-written empty ctor (the C# `public InterpolatedStringText()`). `Text` defaults
    // to `string.Empty` (the C# `= string.Empty` initializer); the port's `std::string` member
    // defaults to empty, matching it. No required slots, so a default-constructed node is a
    // valid empty text run (`CheckInvariant` passes).
    InterpolatedStringText() = default;

    // The hand-written ctor (the C# `public InterpolatedStringText(string text)`). Sets
    // `Text` to `text`. `explicit`: a single-argument ctor is a converting ctor by default, so
    // `explicit` blocks an implicit `std::string` -> `InterpolatedStringText` conversion (the
    // D245 `TypeReferenceExpression` precedent).
    explicit InterpolatedStringText(std::string text) : text_(std::move(text)) {}

    // The C# `public string Text { get; set; } = string.Empty` -- a settable `string` scalar
    // (not a `[Slot]`). The generator's `MembersToMatch` scan adds it (the `String` branch,
    // `MatchString` term); it is NOT a ctor param via the generator's settable-enum rule, but
    // the hand-written ctors set it. Ported as a plain `std::string` field (non-nullable -- the
    // C# `string` is non-nullable, default `string.Empty`).
    std::string Text() const { return text_; }
    void Text(std::string value) { text_ = std::move(value); }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitInterpolatedStringText`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitInterpolatedStringText(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitInterpolatedStringText`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitInterpolatedStringText(this);
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is InterpolatedStringText o && MatchString(this.Text, o.Text)`. The single
    // term is the `String` `MatchString` (the `DoMatchTerm` `String` branch). The port
    // constructs `std::string_view(text_)` per argument (the D227 `Identifier` precedent -- a
    // bare `std::string` needs an explicit `std::string_view` construction). A type-only
    // mismatch (not an `InterpolatedStringText`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<InterpolatedStringText*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
            std::string_view(text_), std::string_view(o->text_));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the `Text` scalar, copies the annotation
    // channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern),
    // and deep-clones no children (the node is a leaf). No own location fields, so the
    // print-time `StartLocation`/`EndLocation` are not copied. The covariant return is
    // `InterpolatedStringText*` (through `InterpolatedStringContent*`, the
    // `InterpolatedStringContent::Clone` pure-virtual).
    InterpolatedStringText* Clone() const override {
        auto* node = new InterpolatedStringText();
        node->text_ = text_;
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

private:
    std::string text_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATEDSTRINGTEXT_HPP
