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

// Port of the `CustomEventDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/EventDeclaration.cs (the generated
// `CustomEventDeclaration.g.cs` + the hand-written partial, which declares the `EventKeyword`
// const string, the `SymbolKind` override, and the six slot properties -- no ctors, no helpers).
// The next in-order Phase-5 piece per the D276 plan ("EventDeclaration/CustomEventDeclaration
// (Type + Variables + AddAccessor/RemoveAccessor -- needs Accessor, now ported)").
//
// `custom_event_declaration ::= attribute_section* modifier* 'event' type ( type '.' )? identifier
// '{' accessor accessor '}'` (C# grammar 15.8.1): a sealed `EntityDeclaration` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final` -- no pattern
// placeholder). Six `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the event (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's only collection but
//     NOT its last slot (five singles trail it), so `supportsIncremental` is FALSE: every
//     `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's indices for a lazy
//     `EnsureChildIndices` rebuild (the `DestructorDeclaration` D272 / `PropertyDeclaration` D276
//     non-incremental precedent).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the event's declared type (a
//     single REQUIRED `AstType` slot at slot 1, non-nullable -- the property type is `AstType`, no
//     `?`; reusing `Slots::Type`). The slot FOLLOWS the `Attributes` collection, so the setter
//     uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
//     collection; the `PropertyDeclaration` D276 precedent). Overrides the base
//     `EntityDeclaration::ReturnType` (the base body kind-walks for the `Type` kind; this override
//     returns the backing field directly, the generated `get => field!`).
//   * `[Slot("PrivateImplementationType")] public partial AstType? PrivateImplementationType` --
//     the explicit-interface-implementation type (a single NULLABLE `AstType?` slot at slot 2, e.g.
//     the `I` in `event EventHandler I.E { add; remove; }`; null when the event is not an explicit
//     interface implementation). The slot FOLLOWS the collection, so the setter uses the INDEX-LESS
//     `SetChildNode`. Reusing the `Slots::PrivateImplementationType` kind (added by
//     `PropertyDeclaration` D276). NOT an override, so a plain non-virtual accessor.
//   * `[Slot("Identifier")] public override partial Identifier NameToken` -- the event's name token
//     (a single REQUIRED `Identifier` slot at slot 3, non-nullable; reusing `Slots::Identifier`).
//     NOT `[ExcludeFromMatch]` (unlike `DestructorDeclaration`), so the generator adds the `Name`
//     `String` `MatchString` term to `DoMatch` (the event's name IS part of the structural match --
//     the `PropertyDeclaration` D276 / `EnumMemberDeclaration` D275 precedent). The slot FOLLOWS the
//     collection, so the setter uses the INDEX-LESS `SetChildNode`. Overrides the base
//     `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind; this
//     override returns the backing field directly). `Name` is NOT overridden (the C# declares no
//     `Name` override), so the inherited base `Name()` kind-walks for the `Identifier` kind and
//     returns the `NameToken`'s `Name` (the real name -- the `PropertyDeclaration` D276 precedent).
//   * `[Slot("AddAccessor")] public partial Accessor? AddAccessor` -- the `add` accessor (a single
//     NULLABLE `Accessor?` slot at slot 4; the `add` block of a custom event). The slot FOLLOWS the
//     collection, so the setter uses the INDEX-LESS `SetChildNode`. NOT an override, so a plain
//     non-virtual accessor. A NEW `Slots::AddAccessor` kind (cycle-broken into `Accessor.hpp` this
//     iteration, the `Slots::Getter`/`Slots::Setter` D276 cycle-breaking precedent applied to an
//     `Accessor`-typed kind).
//   * `[Slot("RemoveAccessor")] public partial Accessor? RemoveAccessor` -- the `remove` accessor
//     (a single NULLABLE `Accessor?` slot at slot 5; the `remove` block of a custom event). The
//     slot FOLLOWS the collection, so the setter uses the INDEX-LESS `SetChildNode`. NOT an
//     override, so a plain non-virtual accessor. A NEW `Slots::RemoveAccessor` kind (cycle-broken
//     into `Accessor.hpp` this iteration, alongside `Slots::AddAccessor`).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]` on
// `CustomEventDeclaration`, so the `Name` term IS added, the `PropertyDeclaration` D276 /
// `EnumMemberDeclaration` D275 precedent), `MatchAttributesAndModifiers`, and `ReturnType`
// explicitly for every `EntityDeclaration`-derived node, then the per-property scan adds the three
// non-override non-`[ExcludeFromMatch]` `[Slot]` children (`PrivateImplementationType`,
// `AddAccessor`, `RemoveAccessor`, each a `MatchOptional` recursive term, in source declaration
// order). So `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType,
// PrivateImplementationType, AddAccessor, RemoveAccessor]`, and the generated `DoMatch` is
// `return other is CustomEventDeclaration o && MatchString(this.Name, o.Name) &&
// this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
// match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match) &&
// MatchOptional(this.AddAccessor, o.AddAccessor, match) && MatchOptional(this.RemoveAccessor,
// o.RemoveAccessor, match)`. The `Name` term is a `MatchString` over the `NameToken`'s name (the
// inherited base `Name()` kind-walks for the `Identifier` kind and returns the `NameToken`'s
// `Name` -- `CustomEventDeclaration` does NOT override `Name`, so the inherited virtual returns
// the real name); the `ReturnType` term is `MatchOptional` (nullable recursive -- the generator
// treats the `EntityDeclaration` `ReturnType` uniformly as `MatchOptional`); the
// `PrivateImplementationType`/`AddAccessor`/`RemoveAccessor` terms are each `MatchOptional` over
// their nullable slots. The `MatchAttributesAndModifiers` helper (on `EntityDeclaration`,
// protected) matches the `Modifiers` scalar (the `Any`-wildcard) AND the `Attributes` collection
// together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// ReturnType (required), PrivateImplementationType (single, optional), NameToken (required),
// AddAccessor (single, optional), RemoveAccessor (single, optional)]` (the `Modifiers` scalar
// lives on the `EntityDeclaration` base and is NOT a declared member of `CustomEventDeclaration`,
// so `GetMembers()` does not add it to `CtorParams` -- the `DestructorDeclaration` D272 /
// `PropertyDeclaration` D276 precedent). `RequiredConstructorPrefixLength` is 4 (through the last
// non-optional param `NameToken` at index 3 -- the generator walks the whole `ctorParams` list and
// takes the last non-optional index + 1, the `CatchClause` D254 / `PropertyDeclaration` D276
// precedent). `ConstructorPrefixLengths` is {4, 6}; the (len=4) ctor
// `(IEnumerable<AttributeSection>, AstType, AstType, Identifier)` and the (len=6) ctor both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (the `AddRange` convenience is
// the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. A
// `CustomEventDeclaration` is built via the empty ctor + `ReturnType(...)` + `NameToken(...)` +
// `PrivateImplementationType(...)` + `AddAccessor(...)` + `RemoveAccessor(...)` +
// `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls
// `visitor.VisitCustomEventDeclaration(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitCustomEventDeclaration`). The generated slot
// statics are `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at
// `Slots.AttributeSection`, collection), `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at
// `Slots.Type`, required), `PrivateImplementationTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing
// at `Slots.PrivateImplementationType`, nullable), `NameTokenSlot` (a
// `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required), `AddAccessorSlot` (a
// `CSharpSlotInfoT<Accessor>` pointing at `Slots.AddAccessor`, nullable), `RemoveAccessorSlot` (a
// `CSharpSlotInfoT<Accessor>` pointing at `Slots.RemoveAccessor`, nullable). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// a fresh node, the `Modifiers` scalar copied via the public `Modifiers()` getter/setter (the
// base's private `modifiers_` is not accessible from the derived `Clone` -- the C#
// `MemberwiseClone` copies the private backing; the port uses the public surface, the
// `DestructorDeclaration` D272 precedent), the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType`/`NameToken` deep-cloned
// through their setters (`AstType::Clone()`/`Identifier::Clone()` return the covariant concrete
// types the setters accept directly), every `Attributes` element deep-cloned through `Add`
// (`AttributeSection::Clone()` returns `AttributeSection*`), and the
// `PrivateImplementationType`/`AddAccessor`/`RemoveAccessor` deep-cloned through their setters
// (`AstType::Clone()`/`Accessor::Clone()` return the covariant concrete types the setters accept
// directly). No own location fields (does not derive `EndLocation`), so the print-time
// `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
// `PropertyDeclaration` D276 no-location-copy precedent). The covariant return is
// `CustomEventDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual -- `EntityDeclaration`
// re-declares no typed `Clone`, faithful to its empty hand-written partial).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/`Identifier`/
// `Accessor`/`SymbolKind` -- the `Attributes()`/`ReturnType()`/`PrivateImplementationType()`/
// `NameToken()`/`AddAccessor()`/`RemoveAccessor()`/`SymbolKind()` accessors do not collide with any
// class in the `Syntax` namespace), so no elaborated-type-specifier is needed anywhere; the plain
// element types resolve to the classes. The `[Slot("Identifier")]`/`[Slot("AddAccessor")]`/
// `[Slot("RemoveAccessor")]` arguments name the slot KINDS but the PROPERTIES are `NameToken`/
// `AddAccessor`/`RemoveAccessor`, so the accessors do not collide with the `Identifier`/`Accessor`
// classes (the `MemberType.MemberName` D238 / `PropertyDeclaration` D276 differently-named-property
// precedent -- the `AddAccessor`/`RemoveAccessor` property names ARE the kind names but there is no
// class named `AddAccessor`/`RemoveAccessor` in the `Syntax` namespace, only `Accessor`, so no
// shadowing). This is the `PropertyDeclaration` D276 collection -> single -> single -> single ->
// single -> single dispatch shape (the `Attributes` collection plus five trailing singles) with
// two `Accessor?` nullable slots instead of the property's five nullable singles.
//
// TWO new `Slots` constants this iteration: `Slots::AddAccessor` and `Slots::RemoveAccessor` (both
// `Accessor`-typed, cycle-broken into `Accessor.hpp` -- `Accessor.hpp` includes `Slots.hpp` for its
// per-node `AttributesSlot`/`BodySlot`, the `Slots::Getter`/`Slots::Setter` D276 cycle-breaking
// precedent). `Slots::AttributeSection`, `Slots::Type`, `Slots::PrivateImplementationType`, and
// `Slots::Identifier` are already ported.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_CUSTOMEVENTDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_CUSTOMEVENTDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class CustomEventDeclaration : EntityDeclaration`. `final` (the
// C# `sealed`): no further derivation. The seventh concrete `TypeMember`, a custom event (an event
// with explicit `add`/`remove` accessors), deriving from `EntityDeclaration` (the `TypeMember`
// base), not from `Statement`/`Expression`/`AstType`. Structurally the `PropertyDeclaration` D276
// `Attributes` collection + five trailing singles shape (with two `Accessor?` nullable slots for
// the `add`/`remove` accessors).
class CustomEventDeclaration final : public EntityDeclaration {
public:
    ~CustomEventDeclaration() override = default;

    // The generated empty ctor (the C# `public CustomEventDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is NOT the node's last slot -- five singles trail it -- so an element's flattened
    // `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation). The five
    // singles default to null; `ReturnType` and `NameToken` are REQUIRED slots (a default-constructed
    // node violates their required-slot invariants, the `UnaryOperatorExpression` D231 precedent;
    // `CheckInvariant` rejects an empty node), the other three are nullable so their absence is
    // invariant-valid.
    CustomEventDeclaration() : attributes_(this, &AttributesSlot, 0, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Event; } }` -- the
    // kind of member this declaration is (an event). Overrides the base abstract `SymbolKind` (the
    // `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Event` avoids a `using` (the
    // enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Event;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the event (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly. Overrides the base `EntityDeclaration::Attributes`
    // (the base body returns a detached empty via `GetChildren`; this override returns the real
    // `attributes_` member). A `const` convenience overload returns `const&` for a `const
    // CustomEventDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the event's declared type; non-nullable, so required --
    // `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the property setter
    // uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
    // collection). Overrides the base `EntityDeclaration::ReturnType` (the base body kind-walks
    // for the `Type` kind; this override returns the backing field directly, the generated
    // `get => field!`). The getter returns the raw pointer (null for a half-constructed node; the
    // C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `PrivateImplementationType` slot (a NULLABLE single `AstType`, NOT a base virtual) -
    // The generated `[Slot("PrivateImplementationType")] public partial AstType?
    // PrivateImplementationType` -- a single NULLABLE `AstType?` slot at slot 2 (the
    // explicit-interface-implementation type, e.g. the `I` in `event EventHandler I.E { add;
    // remove; }`; null when the event is not an explicit interface implementation). The slot
    // FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)`. NOT an override (the `EntityDeclaration` base declares no
    // `PrivateImplementationType` virtual), so a plain non-virtual accessor. `CheckInvariant`
    // passes without a `PrivateImplementationType` (the slot is nullable). Reuses the
    // `Slots::PrivateImplementationType` kind (added by `PropertyDeclaration` D276).
    AstType* PrivateImplementationType() const { return privateImplementationType_; }
    void PrivateImplementationType(AstType* value) {
        SetChildNode(privateImplementationType_, value);
    }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Identifier")] public override partial Identifier NameToken` -- a
    // single REQUIRED `Identifier` slot at slot 3 (the event's name token; non-nullable, so
    // required -- `IsOptional` is false). NOT `[ExcludeFromMatch]` (unlike `DestructorDeclaration`),
    // so the `Name` `MatchString` term IS added to `DoMatch` (the event's name IS part of the
    // structural match -- the `PropertyDeclaration` D276 / `EnumMemberDeclaration` D275
    // precedent). The slot FOLLOWS the `Attributes` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)`. Overrides the base `EntityDeclaration::NameToken`
    // (the base body kind-walks for the `Identifier` kind; this override returns the backing field
    // directly, the generated `get => field!`). `Name` is NOT overridden (the C# declares no `Name`
    // override), so the inherited base `Name()` kind-walks for the `Identifier` kind and returns
    // the `NameToken`'s `Name` (the real name -- the `PropertyDeclaration` D276 precedent).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `AddAccessor` slot (a NULLABLE single `Accessor`, NOT a base virtual) ----------
    // The generated `[Slot("AddAccessor")] public partial Accessor? AddAccessor` -- a single
    // NULLABLE `Accessor?` slot at slot 4 (the `add` accessor of the custom event). The slot
    // FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual accessor.
    // `CheckInvariant` passes without an `AddAccessor` (the slot is nullable). A NEW
    // `Slots::AddAccessor` kind (an `Accessor`-typed kind, cycle-broken into `Accessor.hpp` this
    // iteration).
    Accessor* AddAccessor() const { return addAccessor_; }
    void AddAccessor(Accessor* value) {
        SetChildNode(addAccessor_, value);
    }

    // ---- The `RemoveAccessor` slot (a NULLABLE single `Accessor`, NOT a base virtual) -------
    // The generated `[Slot("RemoveAccessor")] public partial Accessor? RemoveAccessor` -- a single
    // NULLABLE `Accessor?` slot at slot 5 (the `remove` accessor of the custom event). The slot
    // FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual accessor.
    // `CheckInvariant` passes without a `RemoveAccessor` (the slot is nullable). A NEW
    // `Slots::RemoveAccessor` kind (an `Accessor`-typed kind, cycle-broken into `Accessor.hpp` this
    // iteration, alongside `Slots::AddAccessor`).
    Accessor* RemoveAccessor() const { return removeAccessor_; }
    void RemoveAccessor(Accessor* value) {
        SetChildNode(removeAccessor_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required); `PrivateImplementationTypeSlot` (a
    // `CSharpSlotInfoT<AstType>` pointing at `Slots.PrivateImplementationType`, nullable);
    // `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required);
    // `AddAccessorSlot` (a `CSharpSlotInfoT<Accessor>` pointing at `Slots.AddAccessor`, nullable);
    // `RemoveAccessorSlot` (a `CSharpSlotInfoT<Accessor>` pointing at `Slots.RemoveAccessor`,
    // nullable). NO name shadowing (no member is named `AttributeSection`/`AstType`/`Identifier`/
    // `Accessor`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<AstType> PrivateImplementationTypeSlot{"PrivateImplementationType", false, &Slots::PrivateImplementationType, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<Accessor> AddAccessorSlot{"AddAccessor", false, &Slots::AddAccessor, true};
    static inline const CSharpSlotInfoT<Accessor> RemoveAccessorSlot{"RemoveAccessor", false, &Slots::RemoveAccessor, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitCustomEventDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitCustomEventDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Six slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`) and five single slots at slots 1-5 (`ReturnType` at `attrCount`,
    // `PrivateImplementationType` at `attrCount + 1`, `NameToken` at `attrCount + 2`, `AddAccessor`
    // at `attrCount + 3`, `RemoveAccessor` at `attrCount + 4`). `GetChildCount` is `attrCount + 5`
    // (the collection's current length plus the five singles -- each single slot contributes 1 to
    // the flattened count regardless of whether it is filled); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the slots subtracting each one's width from a running index (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a
    // collection step, then five single steps). `GetCollectionByKind` returns the `Attributes`
    // collection for the `AttributeSection` kind. This is the `PropertyDeclaration` D276 collection
    // -> single -> single -> single -> single -> single dispatch shape (the `Attributes` collection
    // plus five trailing singles) with two `Accessor?` nullable slots.
    int GetChildCount() const override { return attributes_.Count() + 5; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return returnType_;
        i--;
        if (i == 0)
            return privateImplementationType_;
        i--;
        if (i == 0)
            return nameToken_;
        i--;
        if (i == 0)
            return addAccessor_;
        i--;
        if (i == 0)
            return removeAccessor_;
        throw std::out_of_range("CustomEventDeclaration::GetChild");
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
            SetChildNode(returnType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(privateImplementationType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(addAccessor_, static_cast<Accessor*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(removeAccessor_, static_cast<Accessor*>(value), index);
            return;
        }
        throw std::out_of_range("CustomEventDeclaration::SetChild");
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
            return &ReturnTypeSlot;
        i--;
        if (i == 0)
            return &PrivateImplementationTypeSlot;
        i--;
        if (i == 0)
            return &NameTokenSlot;
        i--;
        if (i == 0)
            return &AddAccessorSlot;
        i--;
        if (i == 0)
            return &RemoveAccessorSlot;
        throw std::out_of_range("CustomEventDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is CustomEventDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match)
    // && MatchOptional(this.AddAccessor, o.AddAccessor, match) &&
    // MatchOptional(this.RemoveAccessor, o.RemoveAccessor, match)`. The terms are in
    // `MembersToMatch` order (the generator adds `Name`, `MatchAttributesAndModifiers`, and
    // `ReturnType` explicitly for every `EntityDeclaration`-derived node -- `NameToken` is NOT
    // `[ExcludeFromMatch]` on `CustomEventDeclaration`, so the `Name` `String` term IS added, the
    // `PropertyDeclaration` D276 precedent; then the per-property scan adds
    // `PrivateImplementationType`/`AddAccessor`/`RemoveAccessor`, each a `MatchOptional` recursive
    // term, in source declaration order). The `Name` term is a `MatchString` over the `NameToken`'s
    // name (the inherited base `Name()` kind-walks for the `Identifier` kind and returns the
    // `NameToken`'s `Name` -- `CustomEventDeclaration` does NOT override `Name`, so the inherited
    // virtual returns the real name); the `ReturnType` term is `MatchOptional` (nullable recursive --
    // the generator treats the `EntityDeclaration` `ReturnType` uniformly as `MatchOptional`); the
    // `PrivateImplementationType`/`AddAccessor`/`RemoveAccessor` terms are each `MatchOptional` over
    // their nullable slots. A type-only mismatch (not a `CustomEventDeclaration`) rejects early.
    // The `Name()` calls are inlined in the `MatchString` arguments (the `MemberType` D238 /
    // `PropertyDeclaration` D276 precedent) so the C# `&&` short-circuit is preserved; the
    // `std::string` temporaries live until the end of the full `return` expression, so the
    // `std::string_view` views are valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<CustomEventDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && MatchOptional(privateImplementationType_, o->privateImplementationType_, match)
            && MatchOptional(addAccessor_, o->addAccessor_, match)
            && MatchOptional(removeAccessor_, o->removeAccessor_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType`
    // and `NameToken` deep-cloned through their setters (which re-parent; `AstType::Clone()`/
    // `Identifier::Clone()` return the covariant concrete types the setters accept directly),
    // every `Attributes` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AttributeSection::Clone()` returns `AttributeSection*`, which `Add(AttributeSection*)`
    // accepts directly), and the `PrivateImplementationType`/`AddAccessor`/`RemoveAccessor`
    // deep-cloned through their setters (which re-parent; `AstType::Clone()`/`Accessor::Clone()`
    // return the covariant concrete types the setters accept directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `CustomEventDeclaration` does not derive `EndLocation`), so they are not copied
    // (the `DestructorDeclaration` D272 / `PropertyDeclaration` D276 no-location-copy precedent).
    // The covariant return is `CustomEventDeclaration*` (through `AstNode*`, the `AstNode::Clone`
    // virtual -- `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty
    // hand-written partial; the covariant `CustomEventDeclaration*` is a valid override of
    // `AstNode::Clone`).
    CustomEventDeclaration* Clone() const override {
        auto* node = new CustomEventDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (privateImplementationType_ != nullptr)
            node->PrivateImplementationType(privateImplementationType_->Clone());
        if (addAccessor_ != nullptr)
            node->AddAccessor(addAccessor_->Clone());
        if (removeAccessor_ != nullptr)
            node->RemoveAccessor(removeAccessor_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `returnType_`/`nameToken_` are null until set (REQUIRED slots --
    // `CheckInvariant` asserts they are filled); the other three are null until set (NULLABLE slots
    // -- their absence is invariant-valid). NO name shadowing (no member is named
    // `AttributeSection`/`AstType`/`Identifier`/`Accessor`), so the field types are the plain
    // classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    AstType* privateImplementationType_ = nullptr;
    Identifier* nameToken_ = nullptr;
    Accessor* addAccessor_ = nullptr;
    Accessor* removeAccessor_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_CUSTOMEVENTDECLARATION_HPP
