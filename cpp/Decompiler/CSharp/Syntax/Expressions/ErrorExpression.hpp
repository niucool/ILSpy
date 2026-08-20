// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `ErrorExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ErrorExpression.cs (the generated
// `ErrorExpression.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D297 plan ("the remaining Expression nodes: ... ErrorExpression, etc.").
//
// `error_expression ::= /* unparseable input */` (no C# spec grammar production): a placeholder
// inserted when the AST is built from input that does not parse as valid C#. A sealed
// `Expression` leaf with NO `[Slot]` children (the inherited zero-child slot defaults apply, the
// `NullReferenceExpression`/`BreakStatement` precedent) but with its own `Location` field and
// overridden `StartLocation`/`EndLocation`. The hand-written partial declares only the
// `Location` instance property (the error position) and the `StartLocation`/`EndLocation`
// overrides; the hand-written partial also adds a `(string error)` convenience ctor that attaches
// the error text as a trailing multi-line `Comment`.
//
// The `Location` property is a plain settable instance property of type `TextLocation` -- it is
// NOT a `[Slot]` (no `[Slot]` attribute), so the generator's slot scan does not register it as a
// child slot (the node has NO `[Slot]` children, so the inherited zero-child `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` defaults apply). And the generator's `MembersToMatch`
// skips `TextLocation`-typed members (line 112 of `DecompilerSyntaxTreeGenerator.cs`:
// `if (property.Type.MetadataName is "CSharpTokenNode" or "TextLocation") continue;`), so
// `Location` is NOT in `MembersToMatch` and the generated `DoMatch` is the type-only match
// `return other is ErrorExpression` (no members to compare). The generator emits the
// `AcceptVisitor` override calling `visitor.VisitErrorExpression(this)` and the empty generated
// ctor.
//
// PORT DESIGN (the Identifier D227 / EmptyStatement D259 precedent): the C# declares its OWN
// `Location` field and overrides `StartLocation` to return it. This port stores `Location` in
// the INHERITED `startLocation_` field (set via `StorePrintStart`), so the base `StartLocation()`
// returns it WITHOUT an override -- the C# override is purely because it has its own field, the
// semantics are identical (the base `StartLocation()` returns `startLocation_`, which the
// `Location` setter wrote). The `EndLocation` override returns `Location` (the SAME span as
// `StartLocation` -- a zero-width point, distinct from `EmptyStatement` D259 whose `EndLocation`
// is `Location + 1`). Ported as `StartLocation()` (the stored `Location`). This avoids a
// redundant shadowing `location_` field (the Identifier D227 / EmptyStatement D259 precedent
// applied to an expression leaf).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ERROREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ERROREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ErrorExpression : Expression`. `final` (the C# `sealed`):
// no further derivation. A leaf expression with no `[Slot]` children (the inherited zero-child
// slot defaults apply, the `NullReferenceExpression`/`BreakStatement` precedent) but with its
// own `Location` field and a derived `EndLocation`.
class ErrorExpression final : public Expression {
public:
    ~ErrorExpression() override = default;

    // The generated empty ctor (the C# `public ErrorExpression()`). No `[Slot]` children and no
    // ctor params, so the only generated ctor is the empty one (the generator's
    // `WriteConstructors` returns early when `slots.Count == 0` and there are no ctor params --
    // the `NullReferenceExpression`/`BreakStatement` leaf precedent). `Location` defaults to
    // `TextLocation::Empty` via the inherited `startLocation_` default.
    ErrorExpression() = default;

    // The hand-written `ErrorExpression(string error)` convenience ctor -- attaches the error
    // text as a trailing multi-line `Comment`. `Comment(string, CommentType)` is the D288
    // hand-written ctor (defaults to `CommentType::SingleLine`, here overridden to
    // `CommentType::MultiLine`); `AddTrailingTrivia` is the D224 trivia mutation path (it takes
    // ownership of the `Comment*`, upcasting `Comment*` -> `Trivia*`). The `Comment` is created
    // with `new` (matching the C# `new Comment(error, CommentType.MultiLine)`); the trivia holder
    // takes ownership via `unique_ptr` (the D224 ownership model).
    explicit ErrorExpression(std::string error) {
        AddTrailingTrivia(new Comment(std::move(error), CommentType::MultiLine));
    }

    // The C# `public TextLocation Location { get; set; }` -- the error position. A plain
    // settable instance property (NOT a `[Slot]`, NOT in `MembersToMatch` -- `TextLocation`-
    // typed). The port stores it in the INHERITED `startLocation_` field via `StorePrintStart`
    // (the Identifier D227 / EmptyStatement D259 precedent), so the base `StartLocation()`
    // returns it without an override. `Location()` returns `StartLocation()` (the stored
    // `startLocation_`); `Location(value)` stores into it.
    TextLocation Location() const { return StartLocation(); }
    void Location(TextLocation value) { StorePrintStart(value); }

    // The C# `public override TextLocation StartLocation { get { return Location; } }` -- the
    // port does NOT override `StartLocation`: the base returns `startLocation_`, which the
    // `Location` setter wrote via `StorePrintStart`, so the semantics are identical (the C#
    // override is purely because it has its own field -- the Identifier D227 / EmptyStatement
    // D259 precedent).

    // The C# `public override TextLocation EndLocation { get { return Location; } }` -- the
    // SAME span as `StartLocation` (a zero-width point, distinct from `EmptyStatement` D259
    // whose `EndLocation` is `Location + 1`). Derived from `StartLocation` (the stored
    // `Location`).
    TextLocation EndLocation() const override {
        return StartLocation();
    }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitErrorExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitErrorExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitErrorExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitErrorExpression(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)` for
    // a no-member node: `return other is ErrorExpression`. A type-only match -- any other
    // `ErrorExpression` matches, any other node does not. `Location` is `TextLocation`-typed so
    // the generator skips it (not in `MembersToMatch`); `match` is unused (no captures, no
    // recursive child match). (The `BreakStatement` D254 / `EmptyStatement` D259 type-only-DoMatch
    // precedent applied to a leaf that carries its own `Location` field.)
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        return dynamic_cast<ErrorExpression*>(other) != nullptr;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node with `StartLocation` copied (the derived
    // `EndLocation` is computed from it -- the Identifier D227 / `PrimitiveType` D236 /
    // `EmptyStatement` D259 derives-EndLocation-copies-StartLocation precedent) plus the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-
    // clone pattern -- this deep-copies any trailing `Comment` the hand-written ctor attached,
    // since `NodeTrivia::Clone` deep-copies the trivia lists). No `[Slot]` children to
    // deep-copy.
    ErrorExpression* Clone() const override {
        auto* node = new ErrorExpression();
        node->StorePrintStart(StartLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ERROREXPRESSION_HPP
