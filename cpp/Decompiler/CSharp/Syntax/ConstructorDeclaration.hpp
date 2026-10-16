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
// OTHERWISE, ARISING, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// DEALINGS IN THE SOFTWARE.

// Port of the `ConstructorDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ConstructorDeclaration.cs (the generated
// `ConstructorDeclaration.g.cs` + the hand-written partial, which declares the `SymbolKind`
// override, the five slot properties, and the `[ExcludeFromMatch]` `NameToken` -- no ctors, no
// helpers). The next in-order Phase-5 piece per the D280 plan ("ConstructorDeclaration (NameToken +
// Parameters + ConstructorInitializer + Body -- needs ParameterDeclaration, now ported, plus
// ConstructorInitializer)") -- now unblocked by `ConstructorInitializer` (the dependency just ported
// for the `Initializer` slot) and `ParameterDeclaration` D278 (for the `Parameters` collection).
//
// `constructor_declaration ::= attribute_section* modifier* identifier '(' parameter* ')'
// constructor_initializer? ( block | ';' )` (C# grammar 15.11.1): a sealed `EntityDeclaration` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final` -- no pattern
// placeholder). Five `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the constructor (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of TWO
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 2): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `ComposedType` D242 two-collection / `IndexerDeclaration` D279 / `OperatorDeclaration`
//     D280 precedent).
//   * `[ExcludeFromMatch][Slot("Identifier")] public override partial Identifier NameToken` -- the
//     constructor's name token (a single REQUIRED `Identifier` slot at slot 1, non-nullable; reusing
//     `Slots::Identifier`). The slot FOLLOWS the `Attributes` collection, so the property setter
//     uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
//     collection). `[ExcludeFromMatch]` opts it out of `DoMatch` (the constructor's name is just the
//     declaring type name -- the `DestructorDeclaration` D272 `excludeName` precedent). Overrides
//     the base `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind;
//     this override returns the backing field directly, the generated `get => field!`).
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` -- the
//     constructor's parameter list (a COLLECTION at slot 2, the `( parameter* )`; reusing the
//     cycle-broken `Slots::Parameter` kind added by `IndexerDeclaration` D279). The collection is
//     the node's SECOND of TWO collections, so `supportsIncremental` is FALSE; `baseIndex` is 2
//     (the slot index; the generator passes the slot index, not the dynamic flattened index --
//     unused on the non-incremental fast path). NOT a base virtual, so a plain non-virtual
//     accessor.
//   * `[Slot("ConstructorInitializer")] public partial ConstructorInitializer? Initializer` -- the
//     optional `: base(...)` / `: this(...)` initializer (a single NULLABLE `ConstructorInitializer?`
//     slot at slot 3; absent for a constructor with no initializer -- `Foo() { }`). The slot
//     FOLLOWS the `Parameters` collection, so the setter uses the INDEX-LESS `SetChildNode`. Reusing
//     the cycle-broken `Slots::ConstructorInitializer` kind (added by `ConstructorInitializer`).
//     NOT an override, so a plain non-virtual accessor.
//   * `[Slot("Body")] public partial BlockStatement? Body` -- the constructor body (a single
//     NULLABLE `BlockStatement?` slot at slot 4; null for a constructor with no body, e.g. an
//     extern or abstract constructor declared in an interface). The slot FOLLOWS the `Parameters`
//     collection, so the setter uses the INDEX-LESS `SetChildNode`. Reusing the cycle-broken
//     `Slots::Body` kind (cycle-broken into `BlockStatement.hpp` by `CheckedStatement` D260). NOT
//     an override, so a plain non-virtual accessor.
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds
// `MatchAttributesAndModifiers` and `ReturnType` explicitly for every `EntityDeclaration`-derived
// node (`Name` is NOT added -- `NameToken` is `[ExcludeFromMatch]`, the `DestructorDeclaration`
// D272 precedent), then the per-property scan adds the non-override non-`[ExcludeFromMatch]`
// `[Slot]` children in source declaration order (`Parameters`, `Initializer`, `Body`). So
// `MembersToMatch` is `[MatchAttributesAndModifiers, ReturnType, Parameters, Initializer, Body]`, and
// the generated `DoMatch` is `return other is ConstructorDeclaration o &&
// this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType, match)
// && this.Parameters.DoMatch(o.Parameters, match) && MatchOptional(this.Initializer, o.Initializer,
// match) && MatchOptional(this.Body, o.Body, match)`. The `ReturnType` term is `MatchOptional`
// (nullable recursive -- the generator treats the `EntityDeclaration` `ReturnType` uniformly as
// `MatchOptional` -- a `ConstructorDeclaration` has no `ReturnType` slot, so the inherited base
// `ReturnType()` kind-walk returns null and the term is vacuously true); the `Parameters` term is
// the collection recursive `DoMatch` (the generator emits a collection-typed recursive term
// directly, NOT `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `OperatorDeclaration.
// Parameters` D280 precedent); the `Initializer`/`Body` terms are each `MatchOptional` over their
// nullable slots. A type-only mismatch (not a `ConstructorDeclaration`) rejects early.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// NameToken (required), Parameters (collection, optional), Initializer (single, optional), Body
// (single, optional)]` -- the `Modifiers` scalar lives on the `EntityDeclaration` base and is NOT a
// declared member of `ConstructorDeclaration`, so `GetMembers()` does not add it (the
// `DestructorDeclaration` D272 / `OperatorDeclaration` D280 precedent). `RequiredConstructorPrefixLength`
// is 2 (through the last non-optional param `NameToken` at index 1). `ConstructorPrefixLengths` is
// `{2, 5}`. The (len=2) ctor `(IEnumerable<AttributeSection>, Identifier)` and the (len=5) ctor both
// call `this.Attributes.AddRange(...)` for the `Attributes` collection (and the (len=5) ctor also
// calls `this.Parameters.AddRange(...)` for the `Parameters` collection -- both `AddRange`
// conveniences are the D222 deferral), so they are DEFERRED; the empty ctor is the only portable
// ctor. A `ConstructorDeclaration` is built via the empty ctor + `NameToken(...)` +
// `Parameters().Add(...)` + `Initializer(...)` + `Body(...)` + `Attributes().Add(...)` until
// `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitConstructorDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitConstructorDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required),
// `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
// collection -- the node's second collection), `InitializerSlot` (a
// `CSharpSlotInfoT<ConstructorInitializer>` pointing at `Slots.ConstructorInitializer`, nullable),
// and `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). `Clone`
// is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public `Modifiers()`
// getter/setter (the base's private `modifiers_` is not accessible from the derived `Clone` -- the
// C# `MemberwiseClone` copies the private backing; the port uses the public surface, the
// `DestructorDeclaration` D272 precedent), the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken` deep-cloned through the setter
// (which re-parents; `Identifier::Clone()` returns `Identifier*`), every `Attributes` element
// deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()` returns
// `AttributeSection*`), every `Parameters` element deep-cloned through `Add` (which re-parents and
// re-indexes; `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`), and the
// `Initializer`/`Body` deep-cloned through their setters (which re-parent;
// `ConstructorInitializer::Clone()` returns `ConstructorInitializer*`, `BlockStatement::Clone()`
// returns `BlockStatement*` -- the covariant concrete types the setters accept directly). No own
// location fields (does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation`
// are not copied (the `DestructorDeclaration` D272 / `OperatorDeclaration` D280 no-location-copy
// precedent).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`Identifier`/
// `ParameterDeclaration`/`ConstructorInitializer`/`BlockStatement` -- the `Attributes()`/
// `NameToken()`/`Parameters()`/`Initializer()`/`Body()` accessors do not collide with any class in
// the `Syntax` namespace), so no elaborated-type-specifier is needed anywhere; the plain element
// types resolve to the classes. This is the cleanest two-collection `EntityDeclaration` port -- the
// `IndexerDeclaration` D279 / `OperatorDeclaration` D280 two-collection shape with the
// `DestructorDeclaration` D272 `[ExcludeFromMatch]` `NameToken` and two trailing nullable singles
// (`Initializer`/`Body`) instead of trailing accessors, compiling on the first build with no crux.
//
// NO new `Slots` constant this iteration: `Slots::AttributeSection` (cycle-broken in
// `AttributeSection.hpp` D241), `Slots::Identifier` (D237), `Slots::Parameter` (cycle-broken in
// `ParameterDeclaration.hpp` D279), `Slots::ConstructorInitializer` (cycle-broken in
// `ConstructorInitializer.hpp` -- just ported), and `Slots::Body` (cycle-broken in
// `BlockStatement.hpp` D260) are all already ported.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRUCTORDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRUCTORDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorInitializer.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ConstructorDeclaration : EntityDeclaration`. `final` (the
// C# `sealed`): no further derivation. The second `EntityDeclaration` with a `Parameters`
// collection (the `OperatorDeclaration` D280 shape with two trailing nullable singles instead of
// the `OperatorType` scalar) and the `DestructorDeclaration` D272 `[ExcludeFromMatch]` `NameToken`.
class ConstructorDeclaration final : public EntityDeclaration {
public:
    ~ConstructorDeclaration() override = default;

    // The generated empty ctor (the C# `public ConstructorDeclaration()`). The `Attributes` and
    // `Parameters` collections are members (the D222 always-present-stack-member design),
    // initialized here with `baseIndex = 0` for `Attributes` (the first collection) and
    // `baseIndex = 2` for `Parameters` (the slot index, the generator-passed value -- unused on
    // the non-incremental fast path) and `supportsIncremental = false` for both (the node has two
    // collections, so neither is incremental -- the `ComposedType` D242 / `IndexerDeclaration` D279
    // two-collection precedent). `NameToken` defaults to null (a REQUIRED slot --
    // `CheckInvariant` asserts it is filled); `Initializer`/`Body` default to null (NULLABLE --
    // their absence is invariant-valid).
    ConstructorDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                               parameters_(this, &ParametersSlot, 2, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Constructor; } }` --
    // the kind of member this declaration is (a constructor). Overrides the base abstract
    // `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Constructor`
    // avoids a `using` (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the constructor (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of
    // two collections). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly. Overrides the base
    // `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
    // this override returns the real `attributes_` member). A `const` convenience overload returns
    // `const&` for a `const ConstructorDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[ExcludeFromMatch][Slot("Identifier")] public override partial Identifier
    // NameToken` -- a single REQUIRED `Identifier` slot at slot 1 (the constructor's name token; the
    // property type is `Identifier`, non-nullable, so the slot is required -- `IsOptional` is
    // false). The slot FOLLOWS the `Attributes` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a collection).
    // `[ExcludeFromMatch]` opts it out of `DoMatch` (the constructor's name is just the declaring
    // type name -- the `DestructorDeclaration` D272 precedent). Overrides the base
    // `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind; this
    // override returns the backing field directly, the generated `get => field!`). The getter
    // returns the raw pointer (null for a half-constructed node; the C# `!` null-forgiving).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `Parameters` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the constructor's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot
    // at slot 2, the node's SECOND of TWO collections, non-incremental). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses the
    // `Slots::Parameter` kind (cycle-broken into `ParameterDeclaration.hpp` by `IndexerDeclaration`
    // D279). A `const` convenience overload returns `const&` for a `const ConstructorDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Initializer` slot (a NULLABLE single `ConstructorInitializer`, NOT a base virtual) -
    // The generated `[Slot("ConstructorInitializer")] public partial ConstructorInitializer?
    // Initializer` -- a single NULLABLE `ConstructorInitializer?` slot at slot 3 (the optional
    // `: base(...)` / `: this(...)` initializer; absent for a constructor with no initializer --
    // `Foo() { }`). The slot FOLLOWS the `Parameters` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a collection).
    // NOT an override, so a plain non-virtual accessor. `CheckInvariant` passes without an
    // `Initializer` (the slot is nullable). Reuses the `Slots::ConstructorInitializer` kind
    // (cycle-broken into `ConstructorInitializer.hpp`).
    ConstructorInitializer* Initializer() const { return initializer_; }
    void Initializer(ConstructorInitializer* value) {
        SetChildNode(initializer_, value);
    }

    // ---- The `Body` slot (a NULLABLE single `BlockStatement`, NOT a base virtual) ----------
    // The generated `[Slot("Body")] public partial BlockStatement? Body` -- a single NULLABLE
    // `BlockStatement?` slot at slot 4 (the constructor body; null for a constructor with no body,
    // e.g. an extern or abstract constructor). The slot FOLLOWS the `Parameters` collection, so the
    // property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a
    // plain non-virtual accessor. `CheckInvariant` passes without a `Body` (the slot is nullable).
    // Reuses the `Slots::Body` kind (cycle-broken into `BlockStatement.hpp` by `CheckedStatement`
    // D260).
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required -- the `NameToken` `Identifier` is non-nullable);
    // `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
    // collection -- the node's second collection); `InitializerSlot` (a
    // `CSharpSlotInfoT<ConstructorInitializer>` pointing at `Slots.ConstructorInitializer`,
    // nullable); `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`,
    // nullable). NO name shadowing (no member is named `AttributeSection`/`Identifier`/
    // `ParameterDeclaration`/`ConstructorInitializer`/`BlockStatement`), so the element types are
    // the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<ConstructorInitializer> InitializerSlot{"Initializer", false, &Slots::ConstructorInitializer, true};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitConstructorDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitConstructorDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitConstructorDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitConstructorDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Five slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `NameToken` single at slot 1 (`attrCount`), a `Parameters` collection at
    // slot 2 (the contiguous range `[attrCount + 1, attrCount + 1 + paramCount)`), an `Initializer`
    // single at slot 3 (`attrCount + 1 + paramCount`), and a `Body` single at slot 4
    // (`attrCount + 1 + paramCount + 1`). `GetChildCount` is `attrCount + paramCount + 3` (the two
    // collections' current lengths plus the three singles -- each single slot contributes 1 to the
    // flattened count regardless of whether it is filled); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the slots subtracting each one's width from a running index (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a
    // collection step, a single step, a collection step, then two single steps).
    // `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind and
    // the `Parameters` collection for the `Parameter` kind. This is the `OperatorDeclaration` D280
    // two-collection shape with two trailing nullable singles (`Initializer`/`Body`) instead of
    // the `OperatorType` scalar.

    int GetChildCount() const override { return attributes_.Count() + parameters_.Count() + 3; }

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
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        if (i == 0)
            return initializer_;
        i--;
        if (i == 0)
            return body_;
        throw std::out_of_range("ConstructorDeclaration::GetChild");
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
        {
            int n = parameters_.Count();
            if (i < n) {
                parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(initializer_, static_cast<ConstructorInitializer*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(body_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("ConstructorDeclaration::SetChild");
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
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        if (i == 0)
            return &InitializerSlot;
        i--;
        if (i == 0)
            return &BodySlot;
        throw std::out_of_range("ConstructorDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::Parameter)
            return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ConstructorDeclaration o && this.MatchAttributesAndModifiers(o, match) &&
    // MatchOptional(this.ReturnType, o.ReturnType, match) && this.Parameters.DoMatch(o.Parameters,
    // match) && MatchOptional(this.Initializer, o.Initializer, match) && MatchOptional(this.Body,
    // o.Body, match)`. The terms are in `MembersToMatch` order (the generator adds
    // `MatchAttributesAndModifiers` and `ReturnType` explicitly for every `EntityDeclaration`-
    // derived node, then the per-property scan adds `Parameters`/`Initializer`/`Body` in source
    // declaration order; `Name` is NOT added -- `NameToken` is `[ExcludeFromMatch]`). The
    // `MatchAttributesAndModifiers` helper (on `EntityDeclaration`, protected) matches the
    // `Modifiers` scalar (the `Any`-wildcard) AND the `Attributes` collection together; the
    // `ReturnType` term is `MatchOptional` (nullable recursive) -- a `ConstructorDeclaration` has no
    // `ReturnType` slot, so the inherited base `ReturnType()` kind-walk returns null and the term is
    // vacuously true; the `Parameters` term is the collection recursive `DoMatch` (the generator
    // emits a collection-typed recursive term directly, NOT `MatchOptional`); the
    // `Initializer`/`Body` terms are each `MatchOptional` over their nullable slots. A type-only
    // mismatch (not a `ConstructorDeclaration`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ConstructorDeclaration*>(other);
        if (o == nullptr)
            return false;
        return MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && parameters_.DoMatch(o->parameters_, match)
            && MatchOptional(initializer_, o->initializer_, match)
            && MatchOptional(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken`
    // deep-cloned through the setter (which re-parents; `Identifier::Clone()` returns `Identifier*`,
    // which `NameToken(Identifier*)` accepts directly), every `Attributes` element deep-cloned
    // through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()` returns
    // `AttributeSection*`, which `Add(AttributeSection*)` accepts directly), every `Parameters`
    // element deep-cloned through `Add` (which re-parents and re-indexes;
    // `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, which
    // `Add(ParameterDeclaration*)` accepts directly), and the `Initializer`/`Body` deep-cloned
    // through their setters (which re-parent; `ConstructorInitializer::Clone()` returns
    // `ConstructorInitializer*`, which `Initializer(ConstructorInitializer*)` accepts directly;
    // `BlockStatement::Clone()` returns `BlockStatement*`, which `Body(BlockStatement*)` accepts
    // directly). No own location fields (does not derive `EndLocation`), so the print-time
    // `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
    // `OperatorDeclaration` D280 no-location-copy precedent). The covariant return is
    // `ConstructorDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written
    // partial; the covariant `ConstructorDeclaration*` is a valid override of `AstNode::Clone`).
    ConstructorDeclaration* Clone() const override {
        auto* node = new ConstructorDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        if (initializer_ != nullptr)
            node->Initializer(initializer_->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`parameters_` are the always-present collection members
    // (empty until the first `Add`, both non-incremental -- the node has two collections);
    // `nameToken_` is null until the name token is set (a REQUIRED slot -- `CheckInvariant`
    // asserts it is filled); `initializer_`/`body_` are null until set (NULLABLE slots -- their
    // absence is invariant-valid). NO name shadowing (no member is named `AttributeSection`/
    // `Identifier`/`ParameterDeclaration`/`ConstructorInitializer`/`BlockStatement`), so the field
    // types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    Identifier* nameToken_ = nullptr;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    ConstructorInitializer* initializer_ = nullptr;
    BlockStatement* body_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRUCTORDECLARATION_HPP
