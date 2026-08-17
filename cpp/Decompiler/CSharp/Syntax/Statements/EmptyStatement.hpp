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

// Port of the `EmptyStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/EmptyStatement.cs (the generated
// `EmptyStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D258 plan ("the remaining concrete statements: ... EmptyStatement/LabelStatement leaves"). The
// `empty_statement ::= ';'` (C# grammar 13.4): a sealed `Statement` leaf with NO `[Slot]`
// children and no match members -- the cleanest leaf in the Statement hierarchy (no slots, no
// const keyword), but it is the first ported leaf to carry its OWN `Location` field (the `;`
// position) and override `StartLocation`/`EndLocation` (the span is one column: `Location` to
// `Location + 1`). It is the first ported statement to derive `EndLocation`, so it is also the
// first leaf statement whose `Clone` copies `StartLocation` (the Identifier/PrimitiveType
// precedent: a node that derives `EndLocation` copies `StartLocation` so the derived end is
// correct).
//
// The hand-written partial declares only the `Location` instance property (the `;` position) and
// the `StartLocation`/`EndLocation` overrides. The `Location` property is a plain settable
// instance property of type `TextLocation` -- it is NOT a `[Slot]` (no `[Slot]` attribute), so the
// generator's slot scan does not register it as a child slot (the node has NO `[Slot]` children,
// so the inherited zero-child `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` defaults
// apply). And the generator's `MembersToMatch` skips `TextLocation`-typed members (line 112 of
// `DecompilerSyntaxTreeGenerator.cs`: `if (property.Type.MetadataName is "CSharpTokenNode" or
// "TextLocation") continue;`), so `Location` is NOT in `MembersToMatch` and the generated
// `DoMatch` is the type-only match `return other is EmptyStatement` (no members to compare). The
// generator emits the `AcceptVisitor` override calling `visitor.VisitEmptyStatement(this)`.
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): copies `StartLocation` (which the derived `EndLocation` is computed from)
// and the annotation channel, with no children to deep-copy.
//
// PORT DESIGN (the Identifier D227 precedent): the C# declares its OWN `Location` field and
// overrides `StartLocation` to return it. This port stores `Location` in the INHERITED
// `startLocation_` field (set via `StorePrintStart`), so the base `StartLocation()` returns it
// WITHOUT an override -- the C# override is purely because it has its own field, the semantics
// are identical (the base `StartLocation()` returns `startLocation_`, which the `Location`
// setter wrote). The `EndLocation` override returns `new TextLocation(Location.Line,
// Location.Column + 1)` -- one column past the `;` position; ported as
// `TextLocation(StartLocation().Line, StartLocation().Column + 1)` (the `;` is a single column).
// This avoids a redundant shadowing `location_` field (the Identifier precedent applied to a
// statement leaf).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_EMPTYSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_EMPTYSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class EmptyStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. A leaf statement with no `[Slot]` children (the inherited zero-child
// slot defaults apply, the `NullReferenceExpression`/`BreakStatement` precedent) but with its
// own `Location` field and a derived `EndLocation`.
class EmptyStatement final : public Statement {
public:
    ~EmptyStatement() override = default;

    // The generated empty ctor (the C# `public EmptyStatement()`). No `[Slot]` children and no
    // ctor params, so the only generated ctor is the empty one (the generator's
    // `WriteConstructors` returns early when `slots.Count == 0` and there are no ctor params --
    // the `NullReferenceExpression`/`BreakStatement` leaf precedent). `Location` defaults to
    // `TextLocation::Empty` via the inherited `startLocation_` default.
    EmptyStatement() = default;

    // The C# `public TextLocation Location { get; set; }` -- the `;` position. A plain settable
    // instance property (NOT a `[Slot]`, NOT in `MembersToMatch` -- `TextLocation`-typed). The
    // port stores it in the INHERITED `startLocation_` field via `StorePrintStart` (the
    // Identifier D227 precedent), so the base `StartLocation()` returns it without an override.
    // `Location()` returns `StartLocation()` (the stored `startLocation_`); `Location(value)`
    // stores into it.
    TextLocation Location() const { return StartLocation(); }
    void Location(TextLocation value) { StorePrintStart(value); }

    // The C# `public override TextLocation StartLocation { get { return Location; } }` -- the
    // port does NOT override `StartLocation`: the base returns `startLocation_`, which the
    // `Location` setter wrote via `StorePrintStart`, so the semantics are identical (the C#
    // override is purely because it has its own field -- the Identifier D227 precedent).

    // The C# `public override TextLocation EndLocation { get { return new TextLocation(
    // Location.Line, Location.Column + 1); } }` -- one column past the `;` position (the `;` is
    // a single column). Derived from `StartLocation` (the stored `Location`).
    TextLocation EndLocation() const override {
        return TextLocation(StartLocation().Line, StartLocation().Column + 1);
    }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitEmptyStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitEmptyStatement(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)` for
    // a no-member node: `return other is EmptyStatement`. A type-only match -- any other
    // `EmptyStatement` matches, any other node does not. `Location` is `TextLocation`-typed so
    // the generator skips it (not in `MembersToMatch`); `match` is unused (no captures, no
    // recursive child match). (The `BreakStatement` D254 type-only-DoMatch precedent applied to
    // a leaf that carries its own `Location` field.)
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        return dynamic_cast<EmptyStatement*>(other) != nullptr;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node with `StartLocation` copied (the derived
    // `EndLocation` is computed from it -- the Identifier D227 / `PrimitiveType` D236
    // derives-EndLocation-copies-StartLocation precedent) plus the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern). No children
    // to deep-copy.
    EmptyStatement* Clone() const override {
        auto* node = new EmptyStatement();
        node->StorePrintStart(StartLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_EMPTYSTATEMENT_HPP
