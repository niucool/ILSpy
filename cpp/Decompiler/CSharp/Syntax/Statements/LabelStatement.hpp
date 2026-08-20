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

// Port of the `LabelStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/LabelStatement.cs (the generated
// `LabelStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D258 plan ("the remaining concrete statements: ... EmptyStatement/LabelStatement leaves"). The
// `labeled_statement ::= identifier ':' statement` (C# grammar 13.5): a sealed `Statement` with a
// single NON-nullable `string Label` string-name `[Slot("Identifier")]` over a backing
// `LabelToken` `Identifier` slot (the `GotoStatement` D257 string-name-`[Slot]` shape MINUS the
// nullable optionality and MINUS a const keyword -- a one-REQUIRED-string-name-slot node with no
// collection). The `LabelStatement` itself carries only the label name (the `identifier ':'`);
// the `statement` after the colon is a SIBLING in the enclosing block, not a child of the
// `LabelStatement` (verified by the C# source declaring only the `Label` slot).
//
// The hand-written partial declares only the `[Slot("Identifier")] public partial string Label`
// slot property (no ctors, no helpers, no const strings). The generator emits the `LabelTokenSlot`
// slot static pointing at the shared `Slots::Identifier` kind (already ported by `SimpleType` --
// no new `Slots` constant), the const-index `SetChildNode(ref field, value, 0)` setter, the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single token
// slot, and the `DoMatch` `return other is LabelStatement o && MatchString(this.Label, o.Label)`
// -- the `Label` is a `String` `MatchString` term (the `$any$` wildcard in the pattern's `Label`
// matches any candidate name); the backing `LabelToken` is a generated non-partial `[Slot]`
// (not seen by the source-property scan at generation time), so it never appears in
// `MembersToMatch`. `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the
// port overrides it (no `MemberwiseClone`): deep-clones the token and copies the annotation
// channel.
//
// NO C++ name-shadowing crux (unlike the `SimpleType` D237 / `IdentifierExpression` D246 whose
// `Identifier()` accessor shadows the `Identifier` class): the `[Slot("Identifier")]` argument
// names the slot KIND "Identifier" but the PROPERTY is "Label", so the string accessor is
// `Label()` (NOT `Identifier()`) and the backing token accessor is `LabelToken()` (NOT
// `IdentifierToken()`). Neither shadows the `Identifier` CLASS in this class scope (no member is
// named `Identifier`), so NO elaborated-type-specifier (`class Identifier`) is needed anywhere,
// and the `Identifier::Create` factory call in the `Label` setter is unqualified -- the
// `MemberType.MemberName` D238 / `GotoStatement` D257 differently-named-property precedent applied
// to the label statement.
//
// The NON-nullable `string Label` (vs `GotoStatement`'s nullable `string?`): the backing
// `LabelToken` is a REQUIRED (non-nullable) slot (IsOptional=false), the `Label()` getter DEREFS
// the token (returning `std::string`, NOT `std::optional<std::string>` -- a null token is a
// half-constructed node that would `NullReferenceException` in C#), and the `Label` setter uses
// `Identifier::Create` (NOT `CreateIfNotEmpty` -- a non-nullable name creates a token even for an
// empty string, so an empty name yields a token with an empty `Name`, not a null token -- the
// `MemberType.MemberName` D238 / `IdentifierExpression.Identifier` D246 precedent).
// `CheckInvariant` therefore passes only on a FILLED node (the token is a required slot, so an
// empty node violates the required-slot invariant -- the assert fires in debug), unlike
// `GotoStatement` whose optional token passes `CheckInvariant` on a nameless node. A nameless
// `LabelStatement` is a half-constructed node (UB to deref or pass to `DoMatch`), so the tests
// exercise the empty-name case as a real "" match (two empty names match; empty does not match
// non-empty), not a nameless/nullopt case.
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule -- the name is
// the primary construction value, optionality only governs the setter's empty-to-null behaviour
// and the property type), so `RequiredConstructorPrefixLength` is 1 and
// `ConstructorPrefixLengths` is {1}; the only generated ctors are the empty ctor + the
// `(string label)` all-params ctor. The `(string label)` ctor body is `this.Label = label;` --
// it calls the string setter, which creates the token via `Identifier::Create` (an empty name
// yields a token with an empty `Name`, NOT a null token -- the non-nullable behaviour, faithful
// to the C# `string`). The `(std::string)` single-arg ctor is `explicit` (a single-argument ctor
// is a converting ctor by default -- the `SimpleType` D237 / `GotoStatement` D257 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LABELSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LABELSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class LabelStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The one-REQUIRED-string-name-`[Slot]` node (the `GotoStatement` D257
// string-name-`[Slot]` shape with a NON-nullable `Label`), no const strings.
class LabelStatement final : public Statement {
public:
    ~LabelStatement() override = default;

    // The generated empty ctor (the C# `public LabelStatement()`). `LabelToken` defaults to
    // null (no name), but the token is a REQUIRED slot, so a default-constructed node is only
    // valid until the name is set (or until `DoMatch`/`CheckInvariant` observe the missing
    // token) -- the `IdentifierExpression` D246 / `ExpressionStatement` D255 required-slot
    // behavior.
    LabelStatement() = default;

    // The generated all-params ctor (the C# `public LabelStatement(string label)`); the `Label`
    // string-name `[Slot]` is a "required" ctor param regardless of optionality (the
    // generator's line-168 rule), so `RequiredConstructorPrefixLength` is 1 == the full count,
    // and this single-arg form is both the required-prefix ctor and the all-params ctor (no
    // shorter prefix ctor and no params overload since there is no collection). The generated
    // body is `this.Label = label;` -- it calls the string setter, which creates the token via
    // `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null token
    // -- the non-nullable behaviour, faithful to the C# `string`). `explicit` because a
    // single-argument ctor is a converting ctor by default (the `SimpleType` D237 /
    // `GotoStatement` D257 precedent). Braced-init in tests avoids the most-vexing-parse
    // (`LabelStatement s(std::string())` would declare `s` as a function -- the D236/D257
    // precedent).
    explicit LabelStatement(std::string label) : LabelStatement() {
        Label(std::move(label));
    }

    // ---- The `LabelToken` slot (the backing `Identifier` token of the label) ------------
    // The generated `[Slot("Identifier")] public partial Identifier LabelToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). NO name shadowing
    // (the `LabelToken` accessor does NOT collide with the `Identifier` class -- no member is
    // named `Identifier`).
    Identifier* LabelToken() const { return labelToken_; }
    void LabelToken(Identifier* value) {
        SetChildNode(labelToken_, value, 0);
    }

    // ---- The `Label` string-name accessor (over the token) ------------------------------
    // The generated `public partial string Label` -- a convenience string over the `LabelToken`
    // slot. A NON-optional name (the C# `string`, not `string?`): `get` returns
    // `LabelToken.Name` (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the
    // `MemberType.MemberName` D238 / `IdentifierExpression.Identifier` D246 precedent).
    // `Label()` returns `std::string` (a copy of the token's name); the `Identifier::Create`
    // factory call is unqualified (the `Label()` accessor does NOT shadow the `Identifier`
    // class -- no member is named `Identifier` -- the `MemberType.MemberName` D238 /
    // `GotoStatement` D257 differently-named-property precedent).
    std::string Label() const { return labelToken_->Name(); }
    void Label(std::string_view value) {
        LabelToken(Identifier::Create(std::string(value)));
    }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) ----------------
    // The `LabelTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot). NO name shadowing
    // (`Identifier` resolves to the class -- no member is named `Identifier`), so the element
    // type is the plain `Identifier`. `Slots::Identifier` is already ported (by `SimpleType`
    // D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> LabelTokenSlot{"LabelToken", false, &Slots::Identifier, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitLabelStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitLabelStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitLabelStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitLabelStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single REQUIRED `Identifier` slot at flattened index 0 (`LabelToken`); no collection,
    // so `GetChildCount` is the constant 1 (the slot counts even when the token is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return labelToken_;
            default: throw std::out_of_range("LabelStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(labelToken_, static_cast<Identifier*>(value), 0); break;
            default: throw std::out_of_range("LabelStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &LabelTokenSlot;
            default: throw std::out_of_range("LabelStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is LabelStatement o && MatchString(this.Label, o.Label)`. The `Label` term
    // is a `String` `MatchString` (the `$any$` wildcard in the pattern's `Label` matches any
    // candidate label); the backing `LabelToken` is a generated non-partial `[Slot]` (not seen
    // by the source-property scan at generation time), so it never appears in `MembersToMatch`
    // (no double-match). A type-only mismatch (not a `LabelStatement`) rejects early.
    // `Label()` returns `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per side. `Label` is
    // non-nullable, so the `std::optional<std::string_view>` is always engaged (a real name,
    // never `nullopt` -- the `MemberType.MemberName` D238 precedent). The `Label()` calls are
    // INLINED in the `MatchString` arguments (not pre-computed in locals) so the C# `&&`
    // short-circuit is preserved: `o->Label()` derefs the candidate's token only after the type
    // check passed. The `std::string` temporaries live until the end of the full `return`
    // expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<LabelStatement*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
            std::optional<std::string_view>(std::string_view(Label())),
            std::optional<std::string_view>(std::string_view(o->Label())));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `LabelToken` deep-cloned through the setter when present (which re-parents; the cloned
    // token carries its own `Name`). No scalar to copy (the `Label` string is derived from the
    // token, so cloning the token carries it). The `LabelStatement` itself has no own location
    // fields (`StartLocation`/`EndLocation` are the print-time base fields set by the unported
    // output visitor), so they are not copied (the `GotoStatement` D257 / `BreakStatement` D254
    // no-location-copy precedent). NO elaborated `class Identifier` (no member named
    // `Identifier` shadows the class); the token `Clone()` returns `Identifier*` (the
    // `Identifier::Clone` override), which the `LabelToken(Identifier*)` setter accepts
    // directly.
    LabelStatement* Clone() const override {
        auto* node = new LabelStatement();
        node->CloneAnnotationsFrom(*this);
        if (labelToken_ != nullptr)
            node->LabelToken(labelToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `labelToken_` is null until the name is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled (unlike `GotoStatement`'s optional token). NO name
    // shadowing (no member is named `Identifier`), so the field type is the plain `Identifier`.
    Identifier* labelToken_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LABELSTATEMENT_HPP
