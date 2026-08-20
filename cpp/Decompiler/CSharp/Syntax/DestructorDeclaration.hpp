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

// Port of the `DestructorDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/DestructorDeclaration.cs (the generated
// `DestructorDeclaration.g.cs` + the hand-written partial, which declares only the `TildeToken`
// const string, the `SymbolKind` override, and the three slot properties -- no ctors, no
// helpers). The next in-order Phase-5 piece per the D271 plan ("the first concrete TypeMember
// (DestructorDeclaration -- the simplest EntityDeclaration: an Attributes AttributeSection
// collection + a NameToken Identifier + a nullable BlockStatement Body, reusing the
// already-ported Slots::AttributeSection/Identifier/Body kinds with no new Slots constant)").
//
// `finalizer_declaration ::= attribute_section* modifier* '~' identifier '(' ')' ( block | ';' )`
// (C# grammar 15.13): a sealed `EntityDeclaration` (the `[DecompilerAstNode]` with the default
// `hasPatternPlaceholder: false`, so `final` -- no pattern placeholder). The three `[Slot]`
// children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the finalizer (a COLLECTION at slot 0, reusing
//     the cycle-broken `Slots::AttributeSection` kind). The collection is the node's only
//     collection but NOT its last slot (the `NameToken` and `Body` singles trail it), so
//     `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex == 0 == slots.Count - 1`
//     is false -- `slots.Count` is 3): every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES
//     the parent's indices for a lazy `EnsureChildIndices` rebuild (the `ComposedType` D242 /
//     `ObjectCreateExpression` D251 non-incremental precedent).
//   * `[ExcludeFromMatch][Slot("Identifier")] public override partial Identifier NameToken` --
//     the finalizer's name token (a single REQUIRED `Identifier` slot at slot 1, non-nullable --
//     the property type is `Identifier`, no `?`; reusing `Slots::Identifier`). `[ExcludeFromMatch]`
//     opts the `NameToken` out of the generated `DoMatch` (the finalizer's name is just the
//     declaring type name, so it is not part of the structural match -- the generator's
//     `excludeName` path). The slot FOLLOWS the `Attributes` collection, so its property setter
//     uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
//     collection; the `SetChild` override still passes the caller-computed flattened index to the
//     const-index `SetChildNode` -- the `ComposedType.BaseType` D242 precedent).
//   * `[Slot("Body")] public partial BlockStatement? Body` -- the finalizer's body (a single
//     NULLABLE `BlockStatement?` slot at slot 2, reusing the cycle-broken `Slots::Body` kind).
//     Also index-less (follows the `Attributes` collection).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds
// `MatchAttributesAndModifiers` and `ReturnType` explicitly for every `EntityDeclaration`-derived
// node (the `Name` term is NOT added -- `NameToken` is `[ExcludeFromMatch]`), then the per-property
// scan adds `Body` (a non-override, non-`[ExcludeFromMatch]` `AstNode`-derived property). So
// `MembersToMatch` is `[MatchAttributesAndModifiers, ReturnType, Body]`, and the generated
// `DoMatch` is `return other is DestructorDeclaration o && this.MatchAttributesAndModifiers(o,
// match) && MatchOptional(this.ReturnType, o.ReturnType, match) && MatchOptional(this.Body,
// o.Body, match)`. The `ReturnType` term is `MatchOptional` (nullable recursive) -- a
// `DestructorDeclaration` has no `ReturnType` slot, so the inherited base `ReturnType()` kind-walk
// returns null and `MatchOptional(null, null)` is always true (the term is structurally present but
// vacuous for a finalizer). The `Body` term is `MatchOptional` over the nullable `Body` slot. The
// `MatchAttributesAndModifiers` helper (on `EntityDeclaration`) matches the `Modifiers` scalar
// (the `Any`-wildcard) AND the `Attributes` collection together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// NameToken (required), Body (optional)]`, `RequiredConstructorPrefixLength` is 2 (through the last
// non-optional param `NameToken`), `ConstructorPrefixLengths` is {2, 3}. The (len=2) ctor
// `(IEnumerable<AttributeSection>?, Identifier*)` and the (len=3) ctor
// `(IEnumerable<AttributeSection>?, Identifier*, BlockStatement?)` both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (the `AddRange` convenience is
// the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. A
// `DestructorDeclaration` is built via the empty ctor + `NameToken(...)` + `Body(...)` +
// `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitDestructorDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitDestructorDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required), and
// `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
// `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
// `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public
// surface), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
// concrete-clone pattern), the `NameToken` deep-cloned through the setter, every `Attributes`
// element deep-cloned through `Add`, and the `Body` deep-cloned through the setter.
//
// NO C++ name-shadowing crux (no member is named `Identifier`/`Expression`/`AstType`/
// `BlockStatement`/`AttributeSection`/`SymbolKind` -- the `NameToken()`/`Body()`/`Attributes()`/
// `SymbolKind()` accessors do not collide with any class in the `Syntax` namespace), so no
// elaborated-type-specifier is needed anywhere; the plain element types resolve to the classes.
// This is the cleanest `EntityDeclaration` subclass: the `Name()`/`ReturnType()` virtuals are
// inherited (the base kind-walk), `NameToken()`/`Attributes()`/`SymbolKind()` are overridden.
//
// NO new `Slots` constant: `Slots::AttributeSection` is cycle-broken in `AttributeSection.hpp`
// (D241), `Slots::Identifier` is in `Slots.hpp` (D237), and `Slots::Body` is cycle-broken in
// `BlockStatement.hpp` (D260), so `Slots.hpp` is unchanged.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DESTRUCTORDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DESTRUCTORDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class DestructorDeclaration : EntityDeclaration`. `final` (the
// C# `sealed`): no further derivation. A type-member declaration (a finalizer), deriving from
// `EntityDeclaration` (the `TypeMember` base), not from `Statement`/`Expression`/`AstType`.
class DestructorDeclaration final : public EntityDeclaration {
public:
    ~DestructorDeclaration() override = default;

    // The generated empty ctor (the C# `public DestructorDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is NOT the node's last slot -- `NameToken` and `Body` trail it -- so an element's
    // flattened `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation).
    // `NameToken` and `Body` default to null (the `NameToken` is a required slot -- a default-
    // constructed node violates the required-slot invariant, the `UnaryOperatorExpression` D231
    // precedent; `Body` is nullable so its absence is invariant-valid).
    DestructorDeclaration() : attributes_(this, &AttributesSlot, 0, false) {}

    // The `TildeToken = "~"` const string (the C# `public const string TildeToken`). A static
    // field (not instance state), so the generator's `MembersToMatch` (which iterates only
    // instance `IPropertySymbol`s) excludes it from `DoMatch` (the `BreakStatement.BreakKeyword`
    // D254 / `WhileStatement.WhileKeyword` D258 precedent). The output visitor reads it; it ports
    // as a `static constexpr const char*` (the `CheckedExpression.CheckedKeyword` D234 precedent).
    static constexpr const char* TildeToken = "~";

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Destructor; } }` --
    // the kind of member this declaration is. Overrides the base abstract `SymbolKind` (the
    // `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Destructor` avoids a `using`
    // (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Destructor;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the finalizer (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental). The C# lazily
    // allocates the wrapper; the D222 port makes the collection an always-present stack member, so
    // the accessor returns the member directly. Overrides the base `EntityDeclaration::Attributes`
    // (the base body returns a detached empty via `GetChildren`; this override returns the real
    // `attributes_` member). A `const` convenience overload returns `const&` for a `const
    // DestructorDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[ExcludeFromMatch][Slot("Identifier")] public override partial Identifier
    // NameToken` -- a single REQUIRED `Identifier` slot at slot 1 (the finalizer's name token; the
    // property type is `Identifier`, non-nullable, so the slot is required -- `IsOptional` is
    // false). The slot FOLLOWS the `Attributes` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a collection).
    // `[ExcludeFromMatch]` opts it out of `DoMatch` (the finalizer's name is just the declaring
    // type name). Overrides the base `EntityDeclaration::NameToken` (the base body kind-walks for
    // the `Identifier` kind; this override returns the backing field directly, the generated
    // `get => field!`). The getter returns the raw pointer (null for a half-constructed node; the
    // C# `!` null-forgiving).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `Body` slot (a NULLABLE single `BlockStatement`, NOT a base virtual) -----------
    // The generated `[Slot("Body")] public partial BlockStatement? Body` -- a single NULLABLE
    // `BlockStatement?` slot at slot 2 (the finalizer's body; absent for `~Foo();` -- a finalizer
    // with no body, just the `;`). The slot FOLLOWS the `Attributes` collection, so the property
    // setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index).
    // NOT an override (the `EntityDeclaration` base declares no `Body` virtual), so a plain
    // non-virtual accessor. `CheckInvariant` passes without a `Body` (the slot is nullable).
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required -- the `NameToken` `Identifier` is non-nullable);
    // `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). NO name
    // shadowing (no member is named `AttributeSection`/`Identifier`/`BlockStatement`), so the
    // element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitDestructorDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitDestructorDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitDestructorDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitDestructorDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Three slots in declaration order: an `Attributes` collection at slot 0 (the contiguous
    // range `[0, attrCount)`), a `NameToken` single slot at slot 1 (index `attrCount`), and a
    // `Body` single slot at slot 2 (index `attrCount + 1`). `GetChildCount` is `attrCount + 2`
    // (the collection's current length plus the two singles); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the slots subtracting each one's width from a running index (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a
    // collection step, then two single steps). `GetCollectionByKind` returns the `Attributes`
    // collection for the `AttributeSection` kind. This is the `ObjectCreateExpression` D251
    // collection -> single -> single dispatch shape with the collection FIRST.

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
            return body_;
        throw std::out_of_range("DestructorDeclaration::GetChild");
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
            SetChildNode(body_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("DestructorDeclaration::SetChild");
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
            return &BodySlot;
        throw std::out_of_range("DestructorDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is DestructorDeclaration o && this.MatchAttributesAndModifiers(o, match) &&
    // MatchOptional(this.ReturnType, o.ReturnType, match) && MatchOptional(this.Body, o.Body,
    // match)`. The terms are in `MembersToMatch` order (the generator adds
    // `MatchAttributesAndModifiers` and `ReturnType` explicitly for every `EntityDeclaration`-
    // derived node, then the per-property scan adds `Body`; `Name` is NOT added -- `NameToken` is
    // `[ExcludeFromMatch]`). The `MatchAttributesAndModifiers` helper (on `EntityDeclaration`,
    // protected) matches the `Modifiers` scalar (the `Any`-wildcard) AND the `Attributes`
    // collection; the `ReturnType` term is `MatchOptional` (nullable recursive) -- a
    // `DestructorDeclaration` has no `ReturnType` slot, so the inherited base `ReturnType()`
    // kind-walk returns null and the term is vacuously true; the `Body` term is `MatchOptional`
    // over the nullable `Body` slot. A type-only mismatch (not a `DestructorDeclaration`) rejects
    // early. `MatchOptional` is a protected static on `AstNode` (the `BinaryOperatorExpression`
    // D229 precedent); `MatchAttributesAndModifiers` is a protected member on `EntityDeclaration`
    // (the derived `DoMatch` calls it).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<DestructorDeclaration*>(other);
        if (o == nullptr)
            return false;
        return MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && MatchOptional(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`,
    // the D223 concrete-clone pattern), the `NameToken` deep-cloned through the setter (which
    // re-parents; `Identifier::Clone()` returns `Identifier*`, which `NameToken(Identifier*)`
    // accepts directly), every `Attributes` element deep-cloned through `Add` (which re-parents
    // and re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`, which
    // `Add(AttributeSection*)` accepts directly), and the `Body` deep-cloned through the setter
    // (which re-parents; `BlockStatement::Clone()` returns `BlockStatement*`, which
    // `Body(BlockStatement*)` accepts directly). No own location fields (`StartLocation`/
    // `EndLocation` are the print-time base fields set by the unported output visitor), so they
    // are not copied (the `FixedStatement` D267 no-location-copy precedent). The covariant return
    // is `DestructorDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written
    // partial; the covariant `DestructorDeclaration*` is a valid override of `AstNode::Clone`).
    DestructorDeclaration* Clone() const override {
        auto* node = new DestructorDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `nameToken_` is null until the name token is set (a REQUIRED
    // slot -- `CheckInvariant` asserts it is filled); `body_` is null until the body is set (a
    // NULLABLE slot -- its absence is invariant-valid). NO name shadowing (no member is named
    // `AttributeSection`/`Identifier`/`BlockStatement`), so the field types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    Identifier* nameToken_ = nullptr;
    BlockStatement* body_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DESTRUCTORDECLARATION_HPP
