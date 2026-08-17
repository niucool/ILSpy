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

// Port of the `ComposedType` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/ComposedType.cs (the generated `ComposedType.g.cs` +
// the hand-written partial, which declares the const keyword tokens, the scalar properties,
// the slot properties, `ToString`, the `Make*` builders, and `CheckInvariant`; no hand-written
// ctors). The next in-order Phase-5 piece per the D241 plan ("ComposedType itself (Attributes
// collection + BaseType AstType + ArraySpecifiers collection + HasRefSpecifier/
// HasReadOnlySpecifier/HasNullableSpecifier bools + PointerRank int -- the first node with
// TWO collection slots, so the first whose second collection's baseIndex is dynamic and
// supportsIncremental is false for both)"):
// `array_type ::= non_array_type rank_specifier+` (C# grammar 8.2.1),
// `pointer_type ::= dataptr_type | funcptr_type | voidptr_type` (C# grammar 24.3.1),
// `nullable_reference_type ::= non_nullable_reference_type nullable_type_annotation` (8.2.1),
// `nullable_value_type ::= non_nullable_value_type nullable_type_annotation` (8.3.1) -- a
// `ComposedType` wraps a `BaseType` `AstType` with optional modifiers: a leading `ref`/
// `readonly` (`HasRefSpecifier`/`HasReadOnlySpecifier`), a trailing `?`
// (`HasNullableSpecifier`), a trailing `*`-run (`PointerRank`), and trailing rank specifiers
// (`ArraySpecifiers`, one `ArraySpecifier` per `[...]`). It also carries an `Attributes`
// collection of `AttributeSection`s (the `[Foo]`/`[return: Foo]` groups attached to the type).
//
// It is the third concrete `AstType` with a collection slot AND the FIRST ported node with TWO
// collection slots (`Attributes` at slot 0 and `ArraySpecifiers` at slot 2, with the required
// `BaseType` single slot between them at slot 1). The generator's `supportsIncremental` flag is
// `collectionCount == 1 && slotIndex == slots.Count - 1`; with two collections that is `false`
// for BOTH, so neither maintains its elements' flattened `ChildIndex` incrementally -- every
// `Add`/`Insert`/`Remove`/single-slot-set invalidates the parent's indices for a lazy rebuild
// (`EnsureChildIndices`), and `IndexOf` falls back to a linear identity search. This is the
// first ported node where a single slot (`BaseType`) FOLLOWS a collection, so its setter uses
// the index-less `SetChildNode(ref field, value)` (the generator's `constIndex =
// !slots.Take(slotIndex).Any(IsCollection)` is `false` when a collection precedes the slot),
// which invalidates on a set/clear (the flattened index is dynamic -- the `Attributes` count
// can change); the `SetChild` override still passes the known flattened `index` to the
// const-index `SetChildNode(ref field, value, index)`.
//
// Its generated `DoMatch` has SEVEN terms in `MembersToMatch` (source declaration) order: a
// collection recursive `Attributes` term, a plain-equality `HasRefSpecifier` bool term, a
// plain-equality `HasReadOnlySpecifier` bool term, a non-nullable recursive `BaseType` term
// (dispatched through `MatchRequired` -- the D231 [class.access.derived] workaround), a
// plain-equality `HasNullableSpecifier` bool term, a plain-equality `PointerRank` int term,
// and a collection recursive `ArraySpecifiers` term. It is the first ported node to interleave
// two collections, three bool scalars, an int scalar, and a single `AstType` child across one
// `DoMatch`, and the first whose `DoMatch` term order is NOT the pure slot order (the scalars
// declared between the slots appear between their recursive terms).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitComposedType(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitComposedType`; the `EndsWith("AstType")`
// rewriting to "...Type" applies only to `FunctionPointerAstType`/`InvocationAstType`/
// `TupleAstType`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection), the
// `BaseTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
// `BaseType` `AstType` is non-nullable), and `ArraySpecifiersSlot` (a
// `CSharpSlotInfoT<ArraySpecifier>` pointing at `Slots.ArraySpecifier`, collection). `Clone`
// is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): copies the four scalars, deep-clones the `BaseType` through the setter
// (which re-parents), and deep-clones every `Attributes`/`ArraySpecifiers` element through
// `Add` (which re-parents and re-indexes).
//
// NO C++ name-shadowing crux (unlike `SimpleType`/`UnaryOperatorExpression`): the `Attributes`/
// `BaseType`/`ArraySpecifiers` accessors do not collide with any class in the `Syntax` namespace
// (there is `AstType`, not `Type` or `BaseType`), and no member is named `AstType`/`Identifier`/
// `Expression`/`AttributeSection`/`ArraySpecifier`. So no elaborated-type-specifier is needed
// anywhere, and the `Slots.Type`/`Slots.AttributeSection`/`Slots.ArraySpecifier` references in
// the slot statics are unqualified (the `Slots` constants live in the `Slots` namespace).
//
// The generated ctors are DEFERRED except the empty ctor: every parametrized ctor takes the
// `Attributes` collection as its FIRST param (it precedes the required `BaseType` single slot,
// so the required-prefix ctor is `(IEnumerable<AttributeSection>, AstType)` and includes a
// collection param), and the generator's ctor body calls `this.Attributes.AddRange(...)` for
// it -- `AddRange` is the D222-deferred collection convenience mutator, so the whole
// parametrized-ctor surface (the required-prefix `(attributes, baseType)` ctor, the all-params
// `(attributes, baseType, arraySpecifiers)` ctor, and the `params ArraySpecifier[]` overload)
// is deferred until `AddRange` lands. This is the first ported node whose required-prefix ctor
// includes a collection param (because a collection precedes the required single slot), so the
// empty ctor is the only portable ctor; a node is built via the empty ctor + `Attributes().Add`
// / `BaseType(...)` / `ArraySpecifiers().Add`. `ComposedType.cs` declares NO hand-written ctors.
//
// `ToString(CSharpFormattingOptions)` and the `Make*` builders (`MakePointerType`/
// `MakeArrayType`/`MakeRefType`) are DEFERRED (output/resolver stage; the ported `AstType` base
// does not declare them, so there is nothing to override). The hand-written `CheckInvariant`
// override (asserts `PointerRank >= 0` after the inherited base check) is PORTED -- the second
// ported scalar-invariant override (the `ArraySpecifier.Dimensions >= 1` precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_COMPOSEDTYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_COMPOSEDTYPE_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <cassert>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ComposedType : AstType`. `final` (the C# `sealed`): no
// further derivation. The first ported node with TWO collection slots (`Attributes` + `ArraySpecifiers`),
// with the required `BaseType` single slot between them.
class ComposedType final : public AstType {
public:
    ~ComposedType() override = default;

    // The generated empty ctor (the C# `public ComposedType()`). The `Attributes` collection is
    // a member initialized with `baseIndex = 0` (its slot index 0) and `supportsIncremental =
    // false` (it is NOT the node's sole collection, so it does not own the contiguous
    // `[slotIndex, ..)` range with nothing after it); the `ArraySpecifiers` collection is a
    // member initialized with `baseIndex = 2` (its slot index 2 -- the generator passes the
    // slot index, not the dynamic flattened index) and `supportsIncremental = false`. With both
    // collections non-incremental, every `Add`/`Insert`/`Remove`/single-slot-set invalidates the
    // parent's indices for a lazy rebuild (`EnsureChildIndices`). `BaseType` defaults to null
    // (no base type); the four scalars default to `false`/`false`/`false`/`0`.
    ComposedType() : attributes_(this, &AttributesSlot, 0, false),
                     arraySpecifiers_(this, &ArraySpecifiersSlot, 2, false) {}

    // ---- The const keyword tokens (the output-visitor token literals) ----------------
    // The C# `public const string RefKeyword = "ref"` / `ReadonlyKeyword = "readonly"` /
    // `NullableToken = "?"` / `PointerToken = "*"`. Part of the node's public API (the output
    // visitor reads them); port as `static constexpr const char*` (the `CheckedExpression`.
    // `CheckedKeyword` precedent). The generator excludes const string fields from
    // `MembersToMatch` (it iterates only instance `IPropertySymbol`s), so they never appear in
    // the generated `DoMatch`.
    static constexpr const char* RefKeyword = "ref";
    static constexpr const char* ReadonlyKeyword = "readonly";
    static constexpr const char* NullableToken = "?";
    static constexpr const char* PointerToken = "*";

    // ---- The `HasRefSpecifier` / `HasReadOnlySpecifier` bool scalars ----------------
    // The C# `public bool HasRefSpecifier { get; set; }` / `HasReadOnlySpecifier` -- whether
    // the type carries a leading `ref`/`readonly` (C# 7 ref locals/ref returns). Plain bool
    // fields: not child slots (no `[Slot]`), not ctor params (the generator adds only settable
    // ENUM-typed scalars to `CtorParams`, and a bool is not an enum), so they are set via the
    // property setters. They ARE in `MembersToMatch` (the generator adds every non-`[Slot]`
    // instance property), and a bool (not an enum, no `Any`) emits the fall-through plain-equality
    // `DoMatch` term. No name shadowing.
    bool HasRefSpecifier() const { return hasRefSpecifier_; }
    void HasRefSpecifier(bool value) { hasRefSpecifier_ = value; }
    bool HasReadOnlySpecifier() const { return hasReadOnlySpecifier_; }
    void HasReadOnlySpecifier(bool value) { hasReadOnlySpecifier_ = value; }

    // ---- The `Attributes` collection slot -------------------------------------------
    // The generated `[Slot("AttributeSection")] public partial AstNodeCollection<AttributeSection>
    // Attributes` -- the collection of `AttributeSection`s (the `[...]`/`[target:...]` attribute
    // groups) at slot index 0. The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly
    // (the empty-until-first-Add element-list profile is preserved). `supportsIncremental` is
    // `false` (two collections), so `Add` invalidates the parent's indices.
    AstNodeCollectionT<AttributeSection>& Attributes() { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `BaseType` slot (a single REQUIRED `AstType` child) -------------------------
    // The generated `[Slot("Type")] public partial AstType BaseType` -- a single non-nullable
    // `AstType` slot at slot index 1. A COLLECTION precedes it (`Attributes` at slot 0), so the
    // generator's `constIndex = !slots.Take(1).Any(IsCollection)` is `false`, and the setter uses
    // the index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the
    // flattened index is dynamic -- the `Attributes` count can change). This is the first ported
    // single slot that FOLLOWS a collection, so the first to use the unknown-index setter path.
    // No name shadowing (no class named `BaseType` lives in the `Syntax` namespace), so the
    // operand type is the plain `AstType` (no elaborated specifier).
    AstType* BaseType() const { return baseType_; }
    void BaseType(AstType* value) {
        SetChildNode(baseType_, value);
    }

    // ---- The `HasNullableSpecifier` bool + `PointerRank` int scalars ----------------
    // The C# `public bool HasNullableSpecifier { get; set; }` (the trailing `?`) and
    // `public int PointerRank { get; set; }` (the number of trailing `*`s; non-negativity is
    // checked by `CheckInvariant`, not an eager setter guard). Plain scalar fields: not child
    // slots, not ctor params (the generator adds only settable ENUM-typed scalars), so they are
    // set via the property setters. Both ARE in `MembersToMatch`, and a bool/int (not an enum,
    // no `Any`, not a string) emits the fall-through plain-equality `DoMatch` term. `PointerRank`
    // is the second ported int-scalar `DoMatch` term (the `ArraySpecifier.Dimensions` precedent).
    bool HasNullableSpecifier() const { return hasNullableSpecifier_; }
    void HasNullableSpecifier(bool value) { hasNullableSpecifier_ = value; }
    int PointerRank() const { return pointerRank_; }
    void PointerRank(int value) { pointerRank_ = value; }

    // ---- The `ArraySpecifiers` collection slot -------------------------------------------
    // The generated `[Slot("ArraySpecifier")] public partial AstNodeCollection<ArraySpecifier>
    // ArraySpecifiers` -- the collection of rank specifiers (one `ArraySpecifier` per `[...]`)
    // at slot index 2. `supportsIncremental` is `false` (two collections), so `Add` invalidates
    // the parent's indices. `baseIndex = 2` (the slot index; the dynamic flattened index
    // `Attributes.Count + 1` is rebuilt lazily by `EnsureChildIndices`, since the fast path is
    // off).
    AstNodeCollectionT<ArraySpecifier>& ArraySpecifiers() { return arraySpecifiers_; }
    const AstNodeCollectionT<ArraySpecifier>& ArraySpecifiers() const { return arraySpecifiers_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at
    // `Slots.AttributeSection`, collection); the `BaseTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required -- the `BaseType` `AstType` is non-nullable, so
    // `IsCollection || IsNullable` is `false`); the `ArraySpecifiersSlot` (a
    // `CSharpSlotInfoT<ArraySpecifier>` pointing at `Slots.ArraySpecifier`, collection). No
    // name shadowing (no member is named `AttributeSection`/`AstType`/`ArraySpecifier`), so the
    // element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> BaseTypeSlot{"BaseType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<ArraySpecifier> ArraySpecifiersSlot{"ArraySpecifiers", true, &Slots::ArraySpecifier, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitComposedType`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitComposedType(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Three slots in declaration order: an `Attributes` collection at slot 0 (the contiguous
    // range `[0, attrCount)`), a `BaseType` single slot at slot 1 (index `attrCount`), and an
    // `ArraySpecifiers` collection at slot 2 (the range `[attrCount + 1, attrCount + 1 +
    // arrCount)`). `GetChildCount` is `1 + attrCount + arrCount` (the one single slot plus both
    // collections' current lengths); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, a single step, then a collection step). `GetCollectionByKind` returns each
    // collection for its kind. This is the first ported node with a single slot BETWEEN two
    // collections, so the first whose dispatch walk has a collection -> single -> collection shape.

    int GetChildCount() const override { return 1 + attributes_.Count() + arraySpecifiers_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return baseType_;
        i--;
        {
            int n = arraySpecifiers_.Count();
            if (i < n)
                return arraySpecifiers_.At(i);
        }
        throw std::out_of_range("ComposedType::GetChild");
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
            SetChildNode(baseType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        {
            int n = arraySpecifiers_.Count();
            if (i < n) {
                arraySpecifiers_.SetAt(i, static_cast<ArraySpecifier*>(value));
                return;
            }
        }
        throw std::out_of_range("ComposedType::SetChild");
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
            return &BaseTypeSlot;
        i--;
        {
            int n = arraySpecifiers_.Count();
            if (i < n)
                return &ArraySpecifiersSlot;
        }
        throw std::out_of_range("ComposedType::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::ArraySpecifier)
            return &arraySpecifiers_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ComposedType o && this.Attributes.DoMatch(o.Attributes, match) &&
    // this.HasRefSpecifier == o.HasRefSpecifier && this.HasReadOnlySpecifier == o.HasReadOnlySpecifier
    // && this.BaseType.DoMatch(o.BaseType, match) && this.HasNullableSpecifier == o.HasNullableSpecifier
    // && this.PointerRank == o.PointerRank && this.ArraySpecifiers.DoMatch(o.ArraySpecifiers, match)`.
    // The seven terms are in `MembersToMatch` order, which is the source declaration order
    // (`Attributes`, `HasRefSpecifier`, `HasReadOnlySpecifier`, `BaseType`, `HasNullableSpecifier`,
    // `PointerRank`, `ArraySpecifiers`) -- the scalars declared between the slots appear between
    // their recursive terms. The `Attributes`/`ArraySpecifiers` terms are collection recursive
    // matches (the generator emits the collection-typed recursive term directly, NOT
    // `MatchOptional`, which it emits only for a nullable non-collection child); the `BaseType`
    // term is a non-nullable recursive child, so the generator emits a DIRECT
    // `this.BaseType.DoMatch(o.BaseType, match)` -- ported through `MatchRequired` (the D231
    // [class.access.derived] workaround); the three bools and the int are fall-through
    // plain-equality terms. A type-only mismatch (not a `ComposedType`) rejects early. The
    // `BaseType` `MatchRequired` is reached only after the `Attributes` + two bool terms pass,
    // so a half-constructed pattern (a null `BaseType`) rejects without crashing (the
    // `MatchRequired` null-pattern guard), and a null candidate `BaseType` flows through
    // `DoMatch(nullptr)` which returns false.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ComposedType*>(other);
        if (o == nullptr)
            return false;
        return attributes_.DoMatch(o->attributes_, match)
            && hasRefSpecifier_ == o->hasRefSpecifier_
            && hasReadOnlySpecifier_ == o->hasReadOnlySpecifier_
            && MatchRequired(baseType_, o->baseType_, match)
            && hasNullableSpecifier_ == o->hasNullableSpecifier_
            && pointerRank_ == o->pointerRank_
            && arraySpecifiers_.DoMatch(o->arraySpecifiers_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the four scalars copied, the annotation
    // channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern), the `BaseType` deep-cloned through the setter (which re-parents;
    // `AstType::Clone()` returns `AstType*`, the covariant override), and every `Attributes`/
    // `ArraySpecifiers` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AttributeSection::Clone()` returns `AttributeSection*` and `ArraySpecifier::Clone()`
    // returns `ArraySpecifier*`, which the typed `Add`s accept directly). The deep-copy order
    // follows the slot declaration order (`Attributes`, `BaseType`, `ArraySpecifiers`) matching
    // the generated `CloneChildrenInto`; with both collections non-incremental every mutation
    // invalidates, so the order does not affect the final rebuilt state. No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `ComposedType` does not derive `EndLocation`), so they are not copied (the
    // `ConditionalExpression`/`SimpleType`/`MemberType`/`ArraySpecifier`/`Attribute` precedent).
    ComposedType* Clone() const override {
        auto* node = new ComposedType();
        node->CloneAnnotationsFrom(*this);
        node->hasRefSpecifier_ = hasRefSpecifier_;
        node->hasReadOnlySpecifier_ = hasReadOnlySpecifier_;
        node->hasNullableSpecifier_ = hasNullableSpecifier_;
        node->pointerRank_ = pointerRank_;
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (baseType_ != nullptr)
            node->BaseType(baseType_->Clone());
        for (int i = 0; i < arraySpecifiers_.Count(); i++)
            node->arraySpecifiers_.Add(arraySpecifiers_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

    // The C# `internal override void CheckInvariant()` -- the hand-written override that
    // asserts the node's own scalar invariant after the inherited base check. `PointerRank` is
    // the number of `*` specifiers; a transform that corrupts it (e.g. an unbalanced decrement)
    // is caught here. `AstType::CheckInvariant()` (inherited from `AstNode`) is a no-op in
    // `NDEBUG` (its body is `#ifndef NDEBUG`-guarded) and `assert` is a no-op in `NDEBUG`, so the
    // whole override is a no-op in release builds (mirrors the C# `[Conditional("DEBUG")]`).
    // The second ported scalar-invariant override (the `ArraySpecifier.Dimensions >= 1`
    // precedent).
    void CheckInvariant() override {
        AstType::CheckInvariant();
        assert(pointerRank_ >= 0 && "ComposedType.PointerRank must not be negative");
    }

private:
    // The backing fields. The four scalars default to `false`/`false`/`false`/`0`; `baseType_`
    // is null until the base type is set (a required slot -- `CheckInvariant` asserts it is
    // filled); `attributes_`/`arraySpecifiers_` are the always-present collection members (empty
    // until the first `Add`, non-incremental). No name shadowing (no member is named
    // `AttributeSection`/`AstType`/`ArraySpecifier`), so the field types are the plain classes.
    bool hasRefSpecifier_ = false;
    bool hasReadOnlySpecifier_ = false;
    AstType* baseType_ = nullptr;
    bool hasNullableSpecifier_ = false;
    int pointerRank_ = 0;
    AstNodeCollectionT<AttributeSection> attributes_;
    AstNodeCollectionT<ArraySpecifier> arraySpecifiers_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_COMPOSEDTYPE_HPP
