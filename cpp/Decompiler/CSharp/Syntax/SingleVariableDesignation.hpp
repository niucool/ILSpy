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

// Port of the `SingleVariableDesignation` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/VariableDesignation.cs (the generated
// `SingleVariableDesignation.g.cs` + the hand-written partial). The second piece of the
// `VariableDesignation` hierarchy (the next in-order Phase-5 piece per the D263 plan:
// `ForeachStatement` needs the `VariableDesignation` hierarchy).
// `single_variable_designation ::= identifier` (C# grammar 11.2.2): a `SingleVariableDesignation`
// is the deconstruction designation that names a single variable -- a `VariableDesignation` whose
// sole value is the identifier name. It is a sealed leaf carrying a single REQUIRED (non-nullable)
// `string Identifier` string-name `[Slot("Identifier")]` over a backing `IdentifierToken`
// `Identifier` slot (the `LabelStatement` D259 string-name-`[Slot]` shape MINUS the `Statement`
// base -- here a `VariableDesignation`, with no const keyword and no collection). The generated
// `DoMatch` is `return other is SingleVariableDesignation o && MatchString(this.Identifier,
// o.Identifier)` -- a single `String` `MatchString` term (the `$any$` wildcard in the pattern's
// `Identifier` matches any candidate name); the backing `IdentifierToken` is a generated
// non-`partial` `[Slot]` (not seen by the source-property scan at generation time), so it never
// appears in `MembersToMatch`. `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the token and
// copies the annotation channel.
//
// The NON-nullable `string Identifier` (vs `SimpleType`'s nullable `string?`): the backing
// `IdentifierToken` is a REQUIRED (non-nullable) slot (IsOptional=false), the `Identifier()`
// getter DEREFS the token (returning `std::string`, NOT `std::optional<std::string>` -- a null
// token is a half-constructed node that would `NullReferenceException` in C#), and the `Identifier`
// setter uses `Identifier::Create` (NOT `CreateIfNotEmpty` -- a non-nullable name creates a
// token even for an empty string, so an empty name yields a token with an empty `Name`, not a
// null token -- the `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `IdentifierExpression.
// Identifier` D246 precedent). `CheckInvariant` therefore passes only on a FILLED node (the
// token is a required slot, so an empty node violates the required-slot invariant -- the assert
// fires in debug), unlike `SimpleType` whose optional token passes `CheckInvariant` on a
// nameless node. A nameless `SingleVariableDesignation` is a half-constructed node (UB to deref
// or pass to `DoMatch`), so the tests exercise the empty-name case as a real "" match (two
// empty names match; empty does not match non-empty), not a nameless/nullopt case.
//
// C++ name-shadowing crux (the `SimpleType` D237 / `IdentifierExpression` D246 pattern): the
// string accessor `Identifier()` (a member function) shadows the `Identifier` CLASS in this
// class scope (C++ unqualified name lookup finds the member in the complete-class context and
// stops, even though it is not a type), so the token type is the elaborated-type-specifier
// `class Identifier` (`basic.lookup.elab` ignores non-type names) in EVERY type position (the
// ctor parameter, the token accessor's signature, the setter, the slot static's element type,
// the `SetChild` `static_cast`, the `Clone`, and the backing field), and the `Identifier::Create`
// factory call in the setter and the generated ctor is fully-qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create`) -- the C# generator's `global::`
// qualification for the same shadowing case (a `[Slot("Identifier")] string Identifier`
// property literally named "Identifier" shadows the `Identifier` type inside its own setter).
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule -- the name
// is the primary construction value, optionality only governs the setter's empty-to-null
// behaviour and the property type), so `RequiredConstructorPrefixLength` is 1 and
// `ConstructorPrefixLengths` is {1}; the only generated ctors are the empty ctor + the
// `(string identifier)` all-params ctor. The `(string identifier)` ctor body is
// `this.Identifier = identifier;` -- it calls the string setter, which creates the token via
// `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null token --
// the non-nullable behaviour, faithful to the C# `string`). The `(std::string)` single-arg ctor
// is `explicit` (a single-argument ctor is a converting ctor by default -- the `SimpleType`
// D237 / `LabelStatement` D259 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SINGLEVARIABLEDESIGNATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SINGLEVARIABLEDESIGNATION_HPP

#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class SingleVariableDesignation : VariableDesignation`. `final`
// (the C# `sealed`): no further derivation. The one-REQUIRED-string-name-`[Slot]` leaf of the
// `VariableDesignation` hierarchy (the `LabelStatement` D259 shape with a `VariableDesignation`
// base, no const keyword, no collection), with the `SimpleType`/`IdentifierExpression`
// `Identifier` name-shadowing crux (the property is `Identifier`, which shadows the `Identifier`
// class).
class SingleVariableDesignation final : public VariableDesignation {
public:
    ~SingleVariableDesignation() override = default;

    // The generated empty ctor (the C# `public SingleVariableDesignation()`). `IdentifierToken`
    // defaults to null (no name), but the token is a REQUIRED slot, so a default-constructed node
    // is only valid until the name is set (or until `DoMatch`/`CheckInvariant` observe the
    // missing token) -- the `IdentifierExpression` D246 / `LabelStatement` D259 required-slot
    // behavior.
    SingleVariableDesignation() = default;

    // The generated all-params ctor (the C# `public SingleVariableDesignation(string
    // identifier)`); the `Identifier` string-name `[Slot]` is a "required" ctor param regardless
    // of optionality (the generator's line-168 rule), so `RequiredConstructorPrefixLength` is 1
    // == the full count, and this single-arg form is both the required-prefix ctor and the
    // all-params ctor (no shorter prefix ctor and no params overload since there is no
    // collection). The generated body is `this.Identifier = identifier;` -- it calls the string
    // setter, which creates the token via `Identifier::Create` (an empty name yields a token with
    // an empty `Name`, NOT a null token -- the non-nullable behaviour, faithful to the C#
    // `string`). `explicit` because a single-argument ctor is a converting ctor by default (the
    // `SimpleType` D237 / `LabelStatement` D259 precedent). Braced-init in tests avoids the
    // most-vexing-parse (`SingleVariableDesignation s(std::string())` would declare `s` as a
    // function -- the D236/D257 precedent).
    explicit SingleVariableDesignation(std::string identifier) : SingleVariableDesignation() {
        Identifier(std::move(identifier));
    }

    // ---- The `IdentifierToken` slot (the backing `Identifier` token of the name) ---------
    // The generated `[Slot("Identifier")] public partial Identifier IdentifierToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). `class Identifier`
    // (the `Identifier()` string accessor shadows the `Identifier` class in this class scope --
    // the complete-class context, so the shadowing is visible throughout the class body, not
    // just after the accessor; the elaborated specifier `basic.lookup.elab` finds the class).
    class Identifier* IdentifierToken() const { return identifierToken_; }
    void IdentifierToken(class Identifier* value) {
        SetChildNode(identifierToken_, value, 0);
    }

    // ---- The `Identifier` string-name accessor (over the token) --------------------------
    // The generated `public partial string Identifier` -- a convenience string over the
    // `IdentifierToken` slot. A NON-optional name (the C# `string`, not `string?`): `get`
    // returns `IdentifierToken.Name` (deref the token -- a null token is a half-constructed node
    // that would `NullReferenceException` in C#); `set` creates the token via
    // `Identifier.Create` (NOT `CreateIfNotEmpty` -- a non-nullable name creates a token even for
    // an empty string, so an empty name yields a token with an empty `Name`, not a null token --
    // the `MemberType.MemberName` D238 / `LabelStatement.Label` D259 precedent). `Identifier()`
    // returns `std::string` (a copy of the token's name); the `Identifier::Create` factory call
    // is fully-qualified (the `Identifier()` accessor shadows the `Identifier` class in its own
    // setter, the generator's `global::` case -- the `SimpleType` D237 / `IdentifierExpression`
    // D246 precedent).
    std::string Identifier() const { return identifierToken_->Name(); }
    void Identifier(std::string_view value) {
        IdentifierToken(::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create(std::string(value)));
    }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) ----------------
    // The `IdentifierTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot, `IsOptional=false`).
    // `class Identifier` (the `Identifier()` accessor shadows the `Identifier` class in this
    // class scope). `Slots::Identifier` is already ported (by `SimpleType` D237), so no new
    // `Slots` constant.
    static inline const CSharpSlotInfoT<class Identifier> IdentifierTokenSlot{"IdentifierToken", false, &Slots::Identifier, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSingleVariableDesignation` (`SingleVariableDesignation` does not end
    // in "AstType", so the generator's visit-method-name default yields
    // `VisitSingleVariableDesignation`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSingleVariableDesignation(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single REQUIRED `Identifier` slot at flattened index 0 (`IdentifierToken`); no
    // collection, so `GetChildCount` is the constant 1 (the slot counts even when the token is
    // absent) and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return identifierToken_;
            default: throw std::out_of_range("SingleVariableDesignation::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(identifierToken_, static_cast<class Identifier*>(value), index); break;
            default: throw std::out_of_range("SingleVariableDesignation::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &IdentifierTokenSlot;
            default: throw std::out_of_range("SingleVariableDesignation::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SingleVariableDesignation o && MatchString(this.Identifier, o.Identifier)`.
    // The `Identifier` term is a `String` `MatchString` (the `$any$` wildcard in the pattern's
    // `Identifier` matches any candidate name); the backing `IdentifierToken` is a generated
    // non-`partial` `[Slot]` (not seen by the source-property scan at generation time), so it
    // never appears in `MembersToMatch` (no double-match). A type-only mismatch (not a
    // `SingleVariableDesignation`) rejects early. `Identifier()` returns `std::string` (the
    // token's name); `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view
    // is built per side. `Identifier` is non-nullable, so the `std::optional<std::string_view>`
    // is always engaged (a real name, never `nullopt` -- the `MemberType.MemberName` D238
    // precedent). The `Identifier()` calls are INLINED in the `MatchString` arguments (not
    // pre-computed in locals) so the C# `&&` short-circuit is preserved: `o->Identifier()` derefs
    // the candidate's token only after the type check passed. The `std::string` temporaries live
    // until the end of the full `return` expression, keeping the `std::string_view` views valid
    // for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<SingleVariableDesignation*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
            std::optional<std::string_view>(std::string_view(Identifier())),
            std::optional<std::string_view>(std::string_view(o->Identifier())));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `IdentifierToken` deep-cloned through the setter when present (which re-parents; the
    // cloned token carries its own `Name`). No scalar to copy (the `Identifier` string is derived
    // from the token, so cloning the token carries it). The `SingleVariableDesignation` itself
    // has no own location fields (`StartLocation`/`EndLocation` are the print-time base fields
    // set by the unported output visitor), so they are not copied (the `LabelStatement` D259 /
    // `BreakStatement` D254 no-location-copy precedent). `class Identifier` (the `Identifier()`
    // accessor shadows the class); the token `Clone()` returns `Identifier*` (the
    // `Identifier::Clone` override), which the `IdentifierToken(class Identifier*)` setter
    // accepts directly.
    SingleVariableDesignation* Clone() const override {
        auto* node = new SingleVariableDesignation();
        node->CloneAnnotationsFrom(*this);
        if (identifierToken_ != nullptr)
            node->IdentifierToken(identifierToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `identifierToken_` is null until the name is set; the slot is REQUIRED,
    // so `CheckInvariant` asserts it is filled (unlike `SimpleType`'s optional token). `class
    // Identifier` (the `Identifier()` accessor shadows the `Identifier` class in this scope; the
    // elaborated specifier finds the class).
    class Identifier* identifierToken_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SINGLEVARIABLEDESIGNATION_HPP
