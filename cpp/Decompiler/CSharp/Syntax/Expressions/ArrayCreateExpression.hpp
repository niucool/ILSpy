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

// Port of the `ArrayCreateExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.cs (the generated
// `ArrayCreateExpression.g.cs` + the hand-written partial, which declares only the
// `NewKeyword` const and the four slot properties, no ctors, no helpers). The next in-order
// Phase-5 piece per the D251 plan ("ArrayCreateExpression -- a Type AstType slot + an Arguments
// collection + an Initializer, structurally similar to ObjectCreateExpression but with
// additional array-rank slots"):
// `array_creation_expression ::= 'new' type '[' expression* ']' array_specifier*
// array_initializer?` (C# grammar 12.8.17.5) -- a `new T[...]` array-creation expression is a
// `Type` reference (the element type), an `Arguments` collection (the size expressions inside
// the first `[...]`, empty for `new T[,...]` with commas only), a collection of
// `AdditionalArraySpecifiers` (the trailing `[...]`s WITHOUT size info, e.g. the `[]` in
// `new int[5][]`), and an optional `Initializer` (an `ArrayInitializerExpression`, e.g.
// `new int[] { 1, 2 }`).
//
// It is the first ported node with TWO collections FOLLOWED BY a single slot. The slot layout
// in source declaration order is: `Type` (a single REQUIRED `AstType` at slot 0), `Arguments`
// (an `AstNodeCollection<Expression>` collection at slot 1), `AdditionalArraySpecifiers` (an
// `AstNodeCollection<ArraySpecifier>` collection at slot 2), and `Initializer` (a single NULLABLE
// `ArrayInitializerExpression` at slot 3). The generator's `supportsIncremental` flag is
// `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 2`, so it is
// FALSE for BOTH collections: every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the
// parent's indices for a lazy rebuild (`EnsureChildIndices`), and `IndexOf` falls back to a
// linear identity search. This is the `ComposedType` (D242) two-collection shape (both
// collections non-incremental) combined with the `ObjectCreateExpression` (D251) trailing
// nullable single -- the first node where a single slot TRAILS TWO collections.
//
// The `Type` single slot PRECEDES both collections, so its setter uses the const-index
// `SetChildNode(ref field, value, 0)`. The `Initializer` single slot FOLLOWS both collections,
// so the generator's `constIndex = !slots.Take(slotIndex).Any(IsCollection)` is `false` (two
// collections precede it), and the `Initializer` setter uses the index-less
// `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the flattened index
// is dynamic -- either collection's count can change); the `SetChild` override still passes the
// known flattened `index` to the const-index `SetChildNode(ref field, value, index)`.
//
// Its generated `DoMatch` has FOUR terms in `MembersToMatch` (source declaration) order: a
// non-nullable recursive `Type` term (dispatched through `MatchRequired` -- the D231
// [class.access.derived] workaround), a collection recursive `Arguments` term
// (`this.Arguments.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection child), a
// collection recursive `AdditionalArraySpecifiers` term (likewise direct), and a nullable
// recursive `Initializer` term (dispatched through `MatchOptional` -- the
// `BinaryOperatorExpression` D229 nullable-child path). It is the first ported node to combine
// a `MatchRequired` + TWO collection-`DoMatch` terms + a `MatchOptional` across one `DoMatch`,
// and the first whose dispatch walk has a single -> collection -> collection -> single shape.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitArrayCreateExpression(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitArrayCreateExpression`). The generated
// slot statics are `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required --
// the `Type` `AstType` is non-nullable), `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>`
// pointing at `Slots.Argument`, collection), `AdditionalArraySpecifiersSlot` (a
// `CSharpSlotInfoT<ArraySpecifier>` pointing at `Slots.AdditionalArraySpecifier`, collection), and
// `InitializerSlot` (a `CSharpSlotInfoT<ArrayInitializerExpression>` pointing at
// `Slots.Initializer`, optional -- the `Initializer` is nullable). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): a
// fresh node, the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the
// D223 concrete-clone pattern), the `Type` deep-cloned through the setter (which re-parents;
// `AstType::Clone()` returns `AstType*`, the covariant override), every `Arguments` and
// `AdditionalArraySpecifiers` element deep-cloned through `Add` (which re-parents and re-indexes;
// `Expression::Clone()` returns `Expression*` and `ArraySpecifier::Clone()` returns
// `ArraySpecifier*`, which the typed `Add`s accept directly), and the `Initializer` deep-cloned
// through the setter when present (which re-parents; `ArrayInitializerExpression::Clone()`
// returns `ArrayInitializerExpression*`, which `Initializer(ArrayInitializerExpression*)`
// accepts directly).
//
// NO C++ name-shadowing crux (the `ObjectCreateExpression` D251 differently-named-property
// precedent): the `Type()`/`Arguments()`/`AdditionalArraySpecifiers()`/`Initializer()` accessors
// are member functions, but no class named `Type`/`Arguments`/`AdditionalArraySpecifiers`/
// `Initializer` lives in the `Syntax` namespace (there is `AstType`, not `Type`), and no member
// is named `Expression`/`AstType`/`ArraySpecifier`/`ArrayInitializerExpression`. So no
// elaborated-type-specifier is needed anywhere, and the plain `AstType`/`Expression`/
// `ArraySpecifier`/`ArrayInitializerExpression` resolve to the classes in every type position.
//
// NO new `Slots` constant for `Type`/`Arguments`/`Initializer`: all three are already ported
// (`Slots::Type` by `Attribute` D240, `Slots::Argument` by `Attribute` D240, `Slots::Initializer`
// cycle-broken into `ArrayInitializerExpression.hpp` by D251). The `Slots::AdditionalArraySpecifier`
// kind is NEW and lives in `Slots.hpp` (no cycle: `ArraySpecifier.hpp` is a leaf with no
// per-node slot statics, so it does not include `Slots.hpp` -- the `Slots::ArraySpecifier`
// precedent).
//
// `ArrayCreateExpression.cs` declares NO hand-written ctors (only the `NewKeyword` const and the
// four slot properties), so the port carries only the generated ctors. The generated collection
// ctors (the `(AstType, IEnumerable<Expression>)` required-prefix-with-a-collection form, the
// all-params `(AstType, IEnumerable<Expression>, IEnumerable<ArraySpecifier>,
// ArrayInitializerExpression?)` ctor, and the `params` overloads) use `AddRange`, which lands
// with the collection convenience mutators (the D222 deferral), so they are DEFERRED; the empty +
// the `(AstType)` required-prefix ctors cover the construction API (`Type` is the only required
// ctor param -- `Arguments`/`AdditionalArraySpecifiers` are optional collections and `Initializer`
// is an optional single). A size list is built via `Arguments().Add(...)`, additional ranks via
// `AdditionalArraySpecifiers().Add(...)`, and an initializer via `Initializer(...)` until `AddRange`
// lands. The `(AstType)` ctor is `explicit` (a single-argument ctor is a converting ctor by
// default), matching the generator's public ctor but avoiding an implicit `AstType` ->
// `ArrayCreateExpression` conversion.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ARRAYCREATEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ARRAYCREATEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ArrayCreateExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The first ported node with TWO collections followed by a
// single slot (`Arguments` + `AdditionalArraySpecifiers` + the trailing nullable `Initializer`).
class ArrayCreateExpression final : public Expression {
public:
    ~ArrayCreateExpression() override = default;

    // The generated empty ctor (the C# `public ArrayCreateExpression()`). The `Arguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here
    // with `baseIndex = 1` (its slot index 1 -- the `Type` single slot at slot 0 precedes it)
    // and `supportsIncremental = false` (the node has TWO collections, so neither owns the
    // contiguous `[slotIndex, ..)` range with nothing after it); the `AdditionalArraySpecifiers`
    // collection is a member initialized with `baseIndex = 2` (its slot index 2) and
    // `supportsIncremental = false`. With both collections non-incremental, every
    // `Add`/`Insert`/`Remove`/single-slot-set invalidates the parent's indices for a lazy rebuild
    // (`EnsureChildIndices`). `Type` and `Initializer` default to null via their default member
    // initializers (no type, no initializer).
    ArrayCreateExpression() : arguments_(this, &ArgumentsSlot, 1, false),
                              additionalArraySpecifiers_(this, &AdditionalArraySpecifiersSlot, 2, false) {}

    // The generated required-prefix ctor (the C# `public ArrayCreateExpression(AstType type)`)
    // -- the only required ctor param is `Type` (`Arguments`/`AdditionalArraySpecifiers` are
    // optional collections and `Initializer` is an optional single; the generator's
    // `RequiredConstructorPrefixLength` stops at the first non-required single, which is the
    // `Arguments` collection at slot 1). Sets `Type` in declaration order. Delegates to the
    // empty ctor so the collection members are initialized. `explicit` (a single-argument ctor
    // is a converting ctor by default).
    explicit ArrayCreateExpression(AstType* type) : ArrayCreateExpression() {
        Type(type);
    }

    // ---- The const keyword token (the output-visitor token literal) ----------------
    // The C# `public const string NewKeyword = "new"`. Part of the node's public API (the output
    // visitor reads it); port as a `static constexpr const char*` (the `CheckedExpression`.
    // `CheckedKeyword` precedent). The generator excludes const string fields from
    // `MembersToMatch` (it iterates only instance `IPropertySymbol`s), so it never appears in
    // the generated `DoMatch`.
    static constexpr const char* NewKeyword = "new";

    // ---- The `Type` slot (a single REQUIRED `AstType` child) -------------------------
    // The generated `[Slot("Type")] public partial AstType? Type` -- a single non-nullable
    // `AstType` slot at flattened index 0. The const-index `SetChildNode(ref field, value, 0)`
    // setter (no collection precedes it) re-parents and re-indexes in place. No name shadowing
    // (the `Type()` accessor does not collide with the `AstType` base type -- no class named
    // `Type` lives in the `Syntax` namespace, the `Attribute` D240 lesson), so the operand type
    // is the plain `AstType` (no elaborated specifier).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `Arguments` collection slot -------------------------------------------
    // The generated `[Slot("Argument")] public partial AstNodeCollection<Expression>
    // Arguments` -- the collection of size expressions (the contents of the first `[...]`, a
    // `CSharpSlotInfoT<Expression>` slot at slot index 1). The C# lazily allocates the wrapper;
    // the D222 port makes the collection an always-present stack member, so the accessor returns
    // the member directly (the empty-until-first-Add element-list profile is preserved).
    // `supportsIncremental` is `false` (two collections), so `Add` invalidates the parent's
    // indices.
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The `AdditionalArraySpecifiers` collection slot -------------------------------------------
    // The generated `[Slot("AdditionalArraySpecifier")] public partial
    // AstNodeCollection<ArraySpecifier> AdditionalArraySpecifiers` -- the collection of
    // additional rank specifiers (the trailing `[...]`s WITHOUT size info, one `ArraySpecifier`
    // per `[...]`, e.g. the `[]` in `new int[5][]`; a `CSharpSlotInfoT<ArraySpecifier>` slot at
    // slot index 2). `supportsIncremental` is `false` (two collections), so `Add` invalidates the
    // parent's indices. `baseIndex = 2` (the slot index; the dynamic flattened index
    // `1 + Arguments.Count` is rebuilt lazily by `EnsureChildIndices`, since the fast path is
    // off). The `[Slot("AdditionalArraySpecifier")]` argument names a kind DISTINCT from
    // `ComposedType.ArraySpecifiers`'s `[Slot("ArraySpecifier")]` (same `ArraySpecifier` element
    // type, different kind names), so the slot system can route by kind.
    AstNodeCollectionT<ArraySpecifier>& AdditionalArraySpecifiers() { return additionalArraySpecifiers_; }
    const AstNodeCollectionT<ArraySpecifier>& AdditionalArraySpecifiers() const { return additionalArraySpecifiers_; }

    // ---- The `Initializer` slot (a single NULLABLE `ArrayInitializerExpression` child) --
    // The generated `[Slot("Initializer")] public partial ArrayInitializerExpression?
    // Initializer` -- a single nullable `ArrayInitializerExpression` slot at slot index 3 (the
    // collection-initializer, e.g. `new int[] { 1, 2 }`). TWO COLLECTIONS precede it (`Arguments`
    // at slot 1, `AdditionalArraySpecifiers` at slot 2), so the generator's `constIndex =
    // !slots.Take(3).Any(IsCollection)` is `false`, and the setter uses the index-less
    // `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the flattened
    // index is dynamic -- either collection's count can change). This is the
    // `ObjectCreateExpression.Initializer` (D251) pattern applied to a nullable single child
    // after TWO collections. No name shadowing (no class named `Initializer` lives in the
    // `Syntax` namespace), so the operand type is the plain `ArrayInitializerExpression` (no
    // elaborated specifier).
    ArrayInitializerExpression* Initializer() const { return initializer_; }
    void Initializer(ArrayInitializerExpression* value) {
        SetChildNode(initializer_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
    // `Type` `AstType` is non-nullable, so `IsCollection || IsNullable` is `false`); the
    // `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`, collection);
    // the `AdditionalArraySpecifiersSlot` (a `CSharpSlotInfoT<ArraySpecifier>` pointing at
    // `Slots.AdditionalArraySpecifier`, collection); the `InitializerSlot` (a
    // `CSharpSlotInfoT<ArrayInitializerExpression>` pointing at `Slots.Initializer`, optional --
    // the `Initializer` is nullable, so `IsNullable` is `true`). No name shadowing
    // (`AstType`/`Expression`/`ArraySpecifier`/`ArrayInitializerExpression` resolve to the
    // classes -- no member is named any of them).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Argument, true};
    static inline const CSharpSlotInfoT<ArraySpecifier> AdditionalArraySpecifiersSlot{"AdditionalArraySpecifiers", true, &Slots::AdditionalArraySpecifier, true};
    static inline const CSharpSlotInfoT<ArrayInitializerExpression> InitializerSlot{"Initializer", false, &Slots::Initializer, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitArrayCreateExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitArrayCreateExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitArrayCreateExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitArrayCreateExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Four slots in declaration order: a `Type` single slot at index 0, an `Arguments` collection
    // occupying the contiguous range `[1, 1 + argCount)`, an `AdditionalArraySpecifiers`
    // collection occupying `[1 + argCount, 1 + argCount + arrCount)`, and an `Initializer` single
    // slot at index `1 + argCount + arrCount` (the trailing single slot after TWO collections).
    // `GetChildCount` is `2 + argCount + arrCount` (the two single slots plus both collections'
    // current lengths); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each
    // one's width from a running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a single step, a collection step, a collection
    // step, then a single step). `GetCollectionByKind` returns each collection for its kind.
    // This is the first ported node with a single slot AFTER TWO collections, so the first whose
    // dispatch walk has a single -> collection -> collection -> single shape.

    int GetChildCount() const override { return 2 + arguments_.Count() + additionalArraySpecifiers_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return type_;
        i--;
        {
            int n = arguments_.Count();
            if (i < n)
                return arguments_.At(i);
            i -= n;
        }
        {
            int n = additionalArraySpecifiers_.Count();
            if (i < n)
                return additionalArraySpecifiers_.At(i);
            i -= n;
        }
        if (i == 0)
            return initializer_;
        throw std::out_of_range("ArrayCreateExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(type_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        {
            int n = arguments_.Count();
            if (i < n) {
                arguments_.SetAt(i, static_cast<Expression*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = additionalArraySpecifiers_.Count();
            if (i < n) {
                additionalArraySpecifiers_.SetAt(i, static_cast<ArraySpecifier*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(initializer_, static_cast<ArrayInitializerExpression*>(value), index);
            return;
        }
        throw std::out_of_range("ArrayCreateExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TypeSlot;
        i--;
        {
            int n = arguments_.Count();
            if (i < n)
                return &ArgumentsSlot;
            i -= n;
        }
        {
            int n = additionalArraySpecifiers_.Count();
            if (i < n)
                return &AdditionalArraySpecifiersSlot;
            i -= n;
        }
        if (i == 0)
            return &InitializerSlot;
        throw std::out_of_range("ArrayCreateExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Argument)
            return &arguments_;
        if (kind == &Slots::AdditionalArraySpecifier)
            return &additionalArraySpecifiers_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ArrayCreateExpression o && this.Type.DoMatch(o.Type, match) &&
    // this.Arguments.DoMatch(o.Arguments, match) && this.AdditionalArraySpecifiers.DoMatch(
    // o.AdditionalArraySpecifiers, match) && MatchOptional(this.Initializer, o.Initializer,
    // match)`. The four terms are in `MembersToMatch` order, which is the source declaration
    // order (`Type`, `Arguments`, `AdditionalArraySpecifiers`, `Initializer`). The `Type` term is
    // a non-nullable recursive child, so the generator emits a DIRECT
    // `this.Type.DoMatch(o.Type, match)` -- ported through `MatchRequired` (the D231
    // [class.access.derived] workaround, since a derived node may not call the protected
    // `DoMatch` through a base `AstType*`); the `Arguments`/`AdditionalArraySpecifiers` terms are
    // collection recursive matches (the generator emits the collection-typed recursive term
    // directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child);
    // the `Initializer` term is a nullable non-collection recursive child, so the generator emits
    // `MatchOptional(this.Initializer, o.Initializer, match)` (the `BinaryOperatorExpression`
    // D229 nullable-child path). A type-only mismatch (not an `ArrayCreateExpression`) rejects
    // early. The `Type` `MatchRequired` is the first term, so a half-constructed pattern (a null
    // `Type`) rejects without crashing (the `MatchRequired` null-pattern guard). The
    // `Initializer` `MatchOptional` returns true when BOTH are absent (the common
    // `new T[5]` shape with no collection initializer), and delegates to the pattern's `DoMatch`
    // when the pattern carries an `Initializer`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ArrayCreateExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && arguments_.DoMatch(o->arguments_, match)
            && additionalArraySpecifiers_.DoMatch(o->additionalArraySpecifiers_, match)
            && MatchOptional(initializer_, o->initializer_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
    // deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`,
    // the covariant override), every `Arguments` and `AdditionalArraySpecifiers` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `Expression::Clone()` returns
    // `Expression*` and `ArraySpecifier::Clone()` returns `ArraySpecifier*`, which the typed
    // `Add`s accept directly), and the `Initializer` deep-cloned through the setter when present
    // (which re-parents; `ArrayInitializerExpression::Clone()` returns
    // `ArrayInitializerExpression*`, which `Initializer(ArrayInitializerExpression*)` accepts
    // directly). The deep-copy order follows the slot declaration order (`Type`, `Arguments`,
    // `AdditionalArraySpecifiers`, `Initializer`) matching the generated `CloneChildrenInto`;
    // with both collections non-incremental every mutation invalidates, so the order does not
    // affect the final rebuilt state. No own location fields (`StartLocation`/`EndLocation` are
    // the print-time base fields set by the unported output visitor -- `ArrayCreateExpression`
    // does not derive `EndLocation`), so they are not copied (the `ObjectCreateExpression`/
    // `ComposedType` precedent for nodes without derived locations). The covariant return is
    // `ArrayCreateExpression*` (through `Expression*`, the `Expression::Clone` pure-virtual).
    ArrayCreateExpression* Clone() const override {
        auto* node = new ArrayCreateExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        for (int i = 0; i < additionalArraySpecifiers_.Count(); i++)
            node->additionalArraySpecifiers_.Add(additionalArraySpecifiers_.At(i)->Clone());
        if (initializer_ != nullptr)
            node->Initializer(initializer_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is null until the type is set (a required slot --
    // `CheckInvariant` asserts it is filled); `initializer_` is null until the collection
    // initializer is set (an OPTIONAL slot -- `CheckInvariant` passes with it null); `arguments_`
    // and `additionalArraySpecifiers_` are the always-present collection members (empty until the
    // first `Add`, non-incremental). No name shadowing (no member is named `AstType`/`Expression`/
    // `ArraySpecifier`/`ArrayInitializerExpression`), so the field types are the plain classes.
    AstType* type_ = nullptr;
    AstNodeCollectionT<Expression> arguments_;
    AstNodeCollectionT<ArraySpecifier> additionalArraySpecifiers_;
    ArrayInitializerExpression* initializer_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ARRAYCREATEEXPRESSION_HPP
