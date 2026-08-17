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

// Port of the `GotoStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/GotoStatement.cs (the generated
// `GotoStatement.g.cs` + the hand-written partial). The first member of the goto family (a
// sealed `Statement` with a single NULLABLE string-name `[Slot]` -- the `SimpleType` D237
// string-name-`[Slot]` shape MINUS the `TypeArguments` collection, a one-nullable-string-slot
// node with no collection) plus the `GotoKeyword` const string, plugging into the
// `IAstVisitor`/`AcceptVisitor` dispatch. The next in-order Phase-5 piece per the D256 plan
// ("the EmptyStatement/GotoStatement/LabelStatement leaves").
//
// `goto_statement ::= 'goto' identifier ';'` (C# grammar 13.10.4): a `goto label` whose target
// label is the `Label` string-name `[Slot("Identifier")]` (a generated backing `LabelToken`
// `Identifier` child slot at flattened index 0 -- an OPTIONAL nullable token, since `Label` is
// `string?`, the name may be absent). The hand-written part declares only the `GotoKeyword`
// const string and the `[Slot("Identifier")] public partial string? Label` slot property (no
// ctors, no helpers); the generator emits the `LabelTokenSlot` slot static pointing at the
// shared `Slots::Identifier` kind (already ported by `SimpleType` -- no new `Slots` constant),
// the const-index `SetChildNode(ref field, value, 0)` setter, the `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single token slot, and the
// `DoMatch` `return other is GotoStatement o && MatchString(this.Label, o.Label)` -- the `Label`
// is a `String` `MatchString` term (the `$any$` wildcard in the pattern's `Label` matches any
// candidate name); the backing `LabelToken` is a generated non-partial `[Slot]` (not seen by the
// source-property scan at generation time), so it never appears in `MembersToMatch`. `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the token (if present) and copies the annotation channel.
//
// NO C++ name-shadowing crux (unlike the `SimpleType` D237 / `IdentifierExpression` D245 whose
// `Identifier()` accessor shadows the `Identifier` class): the `[Slot("Identifier")]` argument
// names the slot KIND "Identifier" but the PROPERTY is "Label", so the string accessor is
// `Label()` (NOT `Identifier()`) and the backing token accessor is `LabelToken()` (NOT
// `IdentifierToken()`). Neither shadows the `Identifier` CLASS in this class scope (no member is
// named `Identifier`), so NO elaborated-type-specifier (`class Identifier`) is needed anywhere,
// and the `Identifier::CreateIfNotEmpty` factory call in the `Label` setter is unqualified --
// the `MemberType.MemberName` D238 / `AttributeSection.AttributeTarget` D241
// differently-named-property precedent applied to the goto family.
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule -- the name is
// the primary construction value, optionality only governs the setter's empty-to-null
// behaviour and the property type), so `RequiredConstructorPrefixLength` is 1 and
// `ConstructorPrefixLengths` is {1}; the only generated ctors are the empty ctor + the
// `(string label)` all-params ctor. The `(string label)` ctor body is `this.Label = label;` --
// it calls the string setter, which creates the token via `Identifier.CreateIfNotEmpty` (an
// empty/null label clears the token, faithful to the C# `string?` optionality). The
// `(std::string)` single-arg ctor is `explicit` (a single-argument ctor is a converting ctor by
// default -- the `SimpleType` D237 / `ExpressionStatement` D255 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_GOTOSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_GOTOSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class GotoStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The one-nullable-string-name-`[Slot]` node (the `SimpleType` D237
// string-name-`[Slot]` shape minus the `TypeArguments` collection), plus the `goto` keyword
// const string.
class GotoStatement final : public Statement {
public:
    ~GotoStatement() override = default;

    // The generated empty ctor (the C# `public GotoStatement()`). `LabelToken` defaults to null
    // (no label) via its default member initializer; an absent token is the faithful `string?`
    // null (a bare `goto default;` is a separate `GotoDefaultStatement`, not a label-less
    // `GotoStatement`). The slot is nullable so an empty node is invariant-valid (the
    // `ReturnStatement` D255 nullable-slot precedent).
    GotoStatement() = default;

    // The generated all-params ctor (the C# `public GotoStatement(string label)`); the `Label`
    // string-name `[Slot]` is a "required" ctor param regardless of optionality (the
    // generator's line-168 rule), so `RequiredConstructorPrefixLength` is 1 == the full count,
    // and this single-arg form is both the required-prefix ctor and the all-params ctor (no
    // shorter prefix ctor and no params overload since there is no collection). The generated
    // body is `this.Label = label;` -- it calls the string setter, which creates the token via
    // `Identifier::CreateIfNotEmpty` (an empty/null label clears the token, faithful to the C#
    // `string?` optionality). `explicit` because a single-argument ctor is a converting ctor by
    // default (the `SimpleType` D237 / `ExpressionStatement` D255 precedent).
    explicit GotoStatement(std::string label) : GotoStatement() {
        Label(label);
    }

    // The C# `public const string GotoKeyword = "goto"` (the `goto` keyword token the output
    // visitor emits) -- ports as a `static constexpr const char*` (a static field, not instance
    // state), so the generator's `MembersToMatch` (which iterates only instance
    // `IPropertySymbol`s) excludes it from the `DoMatch` (the `BreakStatement.BreakKeyword`
    // D254 / `GotoDefaultStatement.GotoKeyword` precedent applied to the goto family).
    static constexpr const char* GotoKeyword = "goto";

    // ---- The `LabelToken` slot (the backing `Identifier` token of the label) ------------
    // The generated `[Slot("Identifier")] public partial Identifier? LabelToken` -- a single
    // OPTIONAL (nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. Absent for a label-less `goto` (a bare `goto default;` is a
    // separate `GotoDefaultStatement`). NO name shadowing (the `LabelToken` accessor does NOT
    // collide with the `Identifier` class -- no member is named `Identifier`).
    Identifier* LabelToken() const { return labelToken_; }
    void LabelToken(Identifier* value) {
        SetChildNode(labelToken_, value, 0);
    }

    // ---- The `Label` string-name accessor (over the token) ------------------------------
    // The generated `public partial string? Label` -- a convenience string over the
    // `LabelToken` slot. An OPTIONAL name (the C# `string?`): `get` returns null when the token
    // is absent; `set` creates the token via `Identifier.CreateIfNotEmpty`, so an empty/null
    // name clears the token (the C# `LabelToken = Identifier.CreateIfNotEmpty(value)`). The
    // `Label()` returns `std::optional<std::string>` (nullopt when the token is absent -- the
    // faithful `string?`); the `Identifier::CreateIfNotEmpty` factory call is unqualified (the
    // `Label()` accessor does NOT shadow the `Identifier` class -- no member is named
    // `Identifier` -- the `MemberType.MemberName` D238 / `AttributeSection.AttributeTarget` D241
    // differently-named-property precedent).
    std::optional<std::string> Label() const {
        return labelToken_ != nullptr
            ? std::optional<std::string>(labelToken_->Name()) : std::nullopt;
    }
    void Label(std::string_view value) {
        LabelToken(Identifier::CreateIfNotEmpty(value));
    }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) ----------------
    // The `LabelTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // optional -- the label may be absent). NO name shadowing (`Identifier` resolves to the
    // class -- no member is named `Identifier`), so the element type is the plain `Identifier`.
    // `Slots::Identifier` is already ported (by `SimpleType` D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> LabelTokenSlot{"LabelToken", false, &Slots::Identifier, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitGotoStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitGotoStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single OPTIONAL `Identifier` slot at flattened index 0 (`LabelToken`); no collection,
    // so `GetChildCount` is the constant 1 (the slot counts even when the token is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return labelToken_;
            default: throw std::out_of_range("GotoStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(labelToken_, static_cast<Identifier*>(value), 0); break;
            default: throw std::out_of_range("GotoStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &LabelTokenSlot;
            default: throw std::out_of_range("GotoStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is GotoStatement o && MatchString(this.Label, o.Label)`. The `Label` term is
    // a `String` `MatchString` (the `$any$` wildcard in the pattern's `Label` matches any
    // candidate label); the backing `LabelToken` is a generated non-partial `[Slot]` (not seen
    // by the source-property scan at generation time), so it never appears in `MembersToMatch`
    // (no double-match). A type-only mismatch (not a `GotoStatement`) rejects early.
    // `Label()` returns `std::optional<std::string>` (nullopt when the token is absent);
    // `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view is built per
    // side (nullopt passes through as the C# null -- a label-less `goto` matches another
    // label-less `goto`). The `std::string` temporaries live until the end of the full `return`
    // expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<GotoStatement*>(other);
        if (o == nullptr)
            return false;
        auto thisLabel = Label();
        auto otherLabel = o->Label();
        return PatternMatching::Pattern::MatchString(
            thisLabel ? std::optional<std::string_view>(*thisLabel) : std::nullopt,
            otherLabel ? std::optional<std::string_view>(*otherLabel) : std::nullopt);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `LabelToken` deep-cloned through the setter when present (which re-parents; the cloned
    // token carries its own `Name`). No scalar to copy (the `Label` string is derived from the
    // token, so cloning the token carries it). The `GotoStatement` itself has no own location
    // fields (`StartLocation`/`EndLocation` are the print-time base fields set by the unported
    // output visitor), so they are not copied (the `SimpleType` D237 / `BreakStatement` D254
    // no-location-copy precedent). NO elaborated `class Identifier` (no member named `Identifier`
    // shadows the class); the token `Clone()` returns `Identifier*` (the `Identifier::Clone`
    // override), which the `LabelToken(Identifier*)` setter accepts directly.
    GotoStatement* Clone() const override {
        auto* node = new GotoStatement();
        node->CloneAnnotationsFrom(*this);
        if (labelToken_ != nullptr)
            node->LabelToken(labelToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `labelToken_` is null until the label is set (an optional slot --
    // `CheckInvariant` does NOT assert it is filled, unlike a required-slot node). NO name
    // shadowing (no member is named `Identifier`), so the field type is the plain `Identifier`.
    Identifier* labelToken_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_GOTOSTATEMENT_HPP
