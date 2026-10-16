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

// Port of the `EnumMemberDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/EnumMemberDeclaration.cs (the generated
// `EnumMemberDeclaration.g.cs` + the hand-written partial, which declares only the `SymbolKind`
// override and the three slot properties -- no ctors, no helpers, no const strings). The next
// in-order Phase-5 piece per the D274 plan ("EnumMemberDeclaration (a leaf EntityDeclaration
// needing a new Slots::EnumMemberInitializer Expression kind)").
//
// `enum_member_declaration ::= attribute_section* identifier ( '=' expression )?` (C# grammar
// 20.4): a sealed `EntityDeclaration` (the `[DecompilerAstNode]` with the default
// `hasPatternPlaceholder: false`, so `final` -- no pattern placeholder). Three `[Slot]` children
// in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the enum member (a COLLECTION at slot 0, reusing
//     the cycle-broken `Slots::AttributeSection` kind). The collection is the node's only
//     collection but NOT its last slot (the `NameToken` and `Initializer` singles trail it), so
//     `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex == 0 == slots.Count - 1`
//     is false -- `slots.Count` is 3): every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES
//     the parent's indices for a lazy `EnsureChildIndices` rebuild (the `DestructorDeclaration`
//     D272 / `Accessor` D274 non-incremental precedent).
//   * `[Slot("Identifier")] public override partial Identifier NameToken` -- the enum member's
//     name token (a single REQUIRED `Identifier` slot at slot 1, non-nullable -- the property type
//     is `Identifier`, no `?`; reusing `Slots::Identifier`). NOT `[ExcludeFromMatch]` (unlike
//     `DestructorDeclaration` whose `NameToken` IS `[ExcludeFromMatch]`), so the generator adds the
//     `Name` `String` `MatchString` term to `DoMatch` (the enum member's name IS part of the
//     structural match -- a pattern `A = 1` matches only a candidate named `A`). The slot FOLLOWS
//     the `Attributes` collection, so its property setter uses the INDEX-LESS `SetChildNode(ref
//     field, value)` (the dynamic flattened index after a collection; the `DestructorDeclaration`
//     D272 / `ComposedType.BaseType` D242 precedent). Overrides the base
//     `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind; this
//     override returns the backing field directly, the generated `get => field!`).
//   * `[Slot("EnumMemberInitializer")] public partial Expression? Initializer` -- the optional
//     `= expression` initializer (a single NULLABLE `Expression?` slot at slot 2; absent for a
//     plain `enum E { A }` with no explicit value). The slot FOLLOWS the `Attributes` collection,
//     so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. A NEW
//     `Slots::EnumMemberInitializer` kind (a `CSharpSlotInfoT<Expression>`, added to `Slots.hpp`
//     this iteration -- the `Expression` abstract base does not include `Slots.hpp`, so the kind
//     lives in `Slots.hpp` with no include cycle, the `Slots.Statement`/`Slots.Argument` precedent).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]` on `EnumMemberDeclaration`,
// so the `Name` term IS added, unlike `DestructorDeclaration` where it is absent),
// `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every `EntityDeclaration`-derived
// node, then the per-property scan adds `Initializer` (a non-override, non-`[ExcludeFromMatch]`
// `AstNode`-derived `Expression?` property -> a `MatchOptional` recursive term). So
// `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType, Initializer]`, and the
// generated `DoMatch` is `return other is EnumMemberDeclaration o && MatchString(this.Name,
// o.Name) && this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType,
// o.ReturnType, match) && MatchOptional(this.Initializer, o.Initializer, match)`. The `Name` term
// is a `MatchString` over the `NameToken`'s name (the inherited base `Name()` kind-walks for the
// `Identifier` kind and returns the `NameToken`'s `Name` -- `EnumMemberDeclaration` does NOT
// override `Name`, so the inherited virtual is used, returning the real name); the `ReturnType`
// term is `MatchOptional` (nullable recursive) -- an `EnumMemberDeclaration` has no `ReturnType`
// slot, so the inherited base `ReturnType()` kind-walk returns null and `MatchOptional(null, null)`
// is always true (the term is structurally present but vacuous for an enum member); the
// `Initializer` term is `MatchOptional` over the nullable `Initializer` slot. The
// `MatchAttributesAndModifiers` helper (on `EntityDeclaration`, protected) matches the `Modifiers`
// scalar (the `Any`-wildcard) AND the `Attributes` collection together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// NameToken (required), Initializer (single, optional)]`, `RequiredConstructorPrefixLength` is 2
// (through the last non-optional param `NameToken`), `ConstructorPrefixLengths` is {2, 3}. The
// (len=2) ctor `(IEnumerable<AttributeSection>?, Identifier)` and the (len=3) ctor
// `(IEnumerable<AttributeSection>?, Identifier, Expression?)` both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (the `AddRange` convenience is
// the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. An
// `EnumMemberDeclaration` is built via the empty ctor + `NameToken(...)` + `Initializer(...)` +
// `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitEnumMemberDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitEnumMemberDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required -- the
// `NameToken` `Identifier` is non-nullable), and `InitializerSlot` (a `CSharpSlotInfoT<Expression>`
// pointing at `Slots.EnumMemberInitializer`, nullable). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): a
// fresh node, the `Modifiers` scalar copied via the public `Modifiers()` getter/setter (the base's
// private `modifiers_` is not accessible from the derived `Clone` -- the C# `MemberwiseClone`
// copies the private backing; the port uses the public surface, the `DestructorDeclaration` D272
// precedent), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
// concrete-clone pattern), the `NameToken` deep-cloned through the setter (which re-parents;
// `Identifier::Clone()` returns `Identifier*`, which `NameToken(Identifier*)` accepts directly),
// every `Attributes` element deep-cloned through `Add` (which re-parents and re-indexes;
// `AttributeSection::Clone()` returns `AttributeSection*`, which `Add(AttributeSection*)` accepts
// directly), and the `Initializer` deep-cloned through the setter (which re-parents;
// `Expression::Clone()` returns `Expression*`, which `Initializer(Expression*)` accepts directly).
// No own location fields (does not derive `EndLocation`), so the print-time `StartLocation`/
// `EndLocation` are not copied (the `DestructorDeclaration` D272 no-location-copy precedent). The
// covariant return is `EnumMemberDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
// `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial).
//
// NO C++ name-shadowing crux (no member is named `Identifier`/`Expression`/`AttributeSection`/
// `SymbolKind` -- the `NameToken()`/`Initializer()`/`Attributes()`/`SymbolKind()` accessors do not
// collide with any class in the `Syntax` namespace), so no elaborated-type-specifier is needed
// anywhere; the plain element types resolve to the classes. This is the `DestructorDeclaration`
// D272 shape with the `Body` `BlockStatement` replaced by the `Initializer` `Expression` (a
// positional/type change, not a structural one) plus the `Name` `MatchString` term (since
// `NameToken` is NOT `[ExcludeFromMatch]`).
//
// ONE new `Slots` constant: `Slots::EnumMemberInitializer` (a `CSharpSlotInfoT<Expression>`, added
// to `Slots.hpp` this iteration -- the `Expression` abstract base does not include `Slots.hpp`, so
// no include cycle, the `Slots.Argument`/`Slots.TargetExpression` precedent). `Slots::AttributeSection`
// is cycle-broken in `AttributeSection.hpp` (D241), `Slots::Identifier` is in `Slots.hpp` (D237),
// so only the one new kind is added.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ENUMMEMBERDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ENUMMEMBERDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class EnumMemberDeclaration : EntityDeclaration`. `final` (the
// C# `sealed`): no further derivation. A type-member declaration (an enum member), deriving from
// `EntityDeclaration` (the `TypeMember` base), not from `Statement`/`Expression`/`AstType`.
class EnumMemberDeclaration final : public EntityDeclaration {
public:
    ~EnumMemberDeclaration() override = default;

    // The generated empty ctor (the C# `public EnumMemberDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is NOT the node's last slot -- `NameToken` and `Initializer` trail it -- so an
    // element's flattened `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a
    // mutation). `NameToken` and `Initializer` default to null (`NameToken` is a required slot --
    // a default-constructed node violates the required-slot invariant, the
    // `UnaryOperatorExpression` D231 precedent; `Initializer` is nullable so its absence is
    // invariant-valid).
    EnumMemberDeclaration() : attributes_(this, &AttributesSlot, 0, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Field; } }` -- the
    // kind of member this declaration is (an enum member is field-like: it is a named constant in
    // an enum, reported as `SymbolKind.Field` by the C# source). Overrides the base abstract
    // `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Field`
    // avoids a `using` (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the enum member (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental). The C# lazily
    // allocates the wrapper; the D222 port makes the collection an always-present stack member, so
    // the accessor returns the member directly. Overrides the base `EntityDeclaration::Attributes`
    // (the base body returns a detached empty via `GetChildren`; this override returns the real
    // `attributes_` member). A `const` convenience overload returns `const&` for a `const
    // EnumMemberDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Identifier")] public override partial Identifier NameToken` -- a
    // single REQUIRED `Identifier` slot at slot 1 (the enum member's name token; the property type
    // is `Identifier`, non-nullable, so the slot is required -- `IsOptional` is false). The slot
    // FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)` (the dynamic flattened index after a collection). NOT
    // `[ExcludeFromMatch]` (unlike `DestructorDeclaration`), so the `Name` `MatchString` term IS
    // added to `DoMatch` (the enum member's name IS part of the structural match). Overrides the
    // base `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind;
    // this override returns the backing field directly, the generated `get => field!`). The getter
    // returns the raw pointer (null for a half-constructed node; the C# `!` null-forgiving).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `Initializer` slot (a NULLABLE single `Expression`, NOT a base virtual) --------
    // The generated `[Slot("EnumMemberInitializer")] public partial Expression? Initializer` --
    // a single NULLABLE `Expression?` slot at slot 2 (the optional `= expression` initializer;
    // absent for a plain `enum E { A }` with no explicit value). The slot FOLLOWS the `Attributes`
    // collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`
    // (the dynamic flattened index). NOT an override (the `EntityDeclaration` base declares no
    // `Initializer` virtual), so a plain non-virtual accessor. `CheckInvariant` passes without an
    // `Initializer` (the slot is nullable). A NEW `Slots::EnumMemberInitializer` kind (an
    // `Expression`-typed kind added this iteration).
    Expression* Initializer() const { return initializer_; }
    void Initializer(Expression* value) {
        SetChildNode(initializer_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required -- the `NameToken` `Identifier` is non-nullable);
    // `InitializerSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.EnumMemberInitializer`,
    // nullable). NO name shadowing (no member is named `AttributeSection`/`Identifier`/
    // `Expression`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<Expression> InitializerSlot{"Initializer", false, &Slots::EnumMemberInitializer, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitEnumMemberDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitEnumMemberDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitEnumMemberDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitEnumMemberDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Three slots in declaration order: an `Attributes` collection at slot 0 (the contiguous
    // range `[0, attrCount)`), a `NameToken` single slot at slot 1 (index `attrCount`), and an
    // `Initializer` single slot at slot 2 (index `attrCount + 1`). `GetChildCount` is
    // `attrCount + 2` (the collection's current length plus the two singles); `GetChild`/
    // `SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a running
    // index (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections`
    // shape -- a collection step, then two single steps). `GetCollectionByKind` returns the
    // `Attributes` collection for the `AttributeSection` kind. This is the `DestructorDeclaration`
    // D272 collection -> single -> single dispatch shape (the collection FIRST, then two singles).
    int GetChildCount() const override { return attributes_.Count() + 2; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return nameToken_;
        i--;
        if (i == 0)
            return initializer_;
        throw std::out_of_range("EnumMemberDeclaration::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n) {
                attributes_.SetAt(i, static_cast<AttributeSection*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(initializer_, static_cast<Expression*>(value), index);
            return;
        }
        throw std::out_of_range("EnumMemberDeclaration::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return &AttributesSlot;
            i -= n;
        }
        if (i == 0)
            return &NameTokenSlot;
        i--;
        if (i == 0)
            return &InitializerSlot;
        throw std::out_of_range("EnumMemberDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is EnumMemberDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && MatchOptional(this.Initializer, o.Initializer, match)`. The terms are in
    // `MembersToMatch` order (the generator adds `Name`, `MatchAttributesAndModifiers`, and
    // `ReturnType` explicitly for every `EntityDeclaration`-derived node -- `NameToken` is NOT
    // `[ExcludeFromMatch]` on `EnumMemberDeclaration`, so the `Name` `String` term IS added, unlike
    // `DestructorDeclaration`; then the per-property scan adds `Initializer` (a `MatchOptional`
    // recursive term)). The `Name` term is a `MatchString` over the `NameToken`'s name (the
    // inherited base `Name()` kind-walks for the `Identifier` kind and returns the `NameToken`'s
    // `Name` -- `EnumMemberDeclaration` does NOT override `Name`, so the inherited virtual returns
    // the real name); the `ReturnType` term is `MatchOptional` (nullable recursive) -- an
    // `EnumMemberDeclaration` has no `ReturnType` slot, so the inherited base `ReturnType()`
    // kind-walk returns null and `MatchOptional(null, null)` is always true (the term is
    // structurally present but vacuous for an enum member); the `Initializer` term is
    // `MatchOptional` over the nullable `Initializer` slot. A type-only mismatch (not an
    // `EnumMemberDeclaration`) rejects early. The `Name()` calls are inlined in the `MatchString`
    // arguments (the `MemberType` D238 / `Accessor` D274 precedent) so the C# `&&` short-circuit
    // is preserved; the `std::string` temporaries live until the end of the full `return`
    // expression, so the `std::string_view` views are valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<EnumMemberDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && MatchOptional(initializer_, o->initializer_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken`
    // deep-cloned through the setter (which re-parents; `Identifier::Clone()` returns
    // `Identifier*`, which `NameToken(Identifier*)` accepts directly), every `Attributes` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
    // returns `AttributeSection*`, which `Add(AttributeSection*)` accepts directly), and the
    // `Initializer` deep-cloned through the setter (which re-parents; `Expression::Clone()` returns
    // `Expression*`, which `Initializer(Expression*)` accepts directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `EnumMemberDeclaration` does not derive `EndLocation`), so they are not copied
    // (the `DestructorDeclaration` D272 no-location-copy precedent). The covariant return is
    // `EnumMemberDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial;
    // the covariant `EnumMemberDeclaration*` is a valid override of `AstNode::Clone`).
    EnumMemberDeclaration* Clone() const override {
        auto* node = new EnumMemberDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (initializer_ != nullptr)
            node->Initializer(initializer_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `nameToken_` is null until the name token is set (a REQUIRED
    // slot -- `CheckInvariant` asserts it is filled); `initializer_` is null until the initializer
    // is set (a NULLABLE slot -- its absence is invariant-valid). NO name shadowing (no member is
    // named `AttributeSection`/`Identifier`/`Expression`), so the field types are the plain
    // classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    Identifier* nameToken_ = nullptr;
    Expression* initializer_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ENUMMEMBERDECLARATION_HPP
