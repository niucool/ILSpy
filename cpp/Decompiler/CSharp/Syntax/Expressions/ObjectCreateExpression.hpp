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

// Port of the `ObjectCreateExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.cs (the generated
// `ObjectCreateExpression.g.cs` + the hand-written partial, which declares only the
// `NewKeyword` const and the three slot properties, no ctors, no helpers). The next in-order
// Phase-5 piece per the D250 plan ("ObjectCreateExpression -- a required AstType Type slot +
// an Arguments AstNodeCollection<Expression> collection + a nullable ArrayInitializerExpression
// Initializer slot, the first node with both a collection and a nullable single child slot,
// reusing Slots::Type/Slots::Argument and the new Slots::Initializer kind"):
// `object_create_expression ::= 'new' type '(' expression* ')' array_initializer?`
// (C# grammar 12.8.17.2.1) -- a `new T(...)` object-creation expression is a `Type` reference
// (the type being constructed), an `Arguments` collection (the constructor argument list, empty
// for the parameterless ctor), and an optional `Initializer` (a `ArrayInitializerExpression`
// collection-initializer, e.g. `new List<int> { 1, 2 }`).
//
// It is the first ported node to combine a COLLECTION with a NULLABLE single child slot (the
// `Initializer` is `[Slot("Initializer")] ArrayInitializerExpression?`). The slot layout in
// source declaration order is: `Type` (a single REQUIRED `AstType` at slot 0), `Arguments`
// (an `AstNodeCollection<Expression>` collection at slot 1), and `Initializer` (a single
// NULLABLE `ArrayInitializerExpression` at slot 2). The generator's `supportsIncremental`
// flag is `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 1`
// but the `Arguments` collection is at `slotIndex == 1`, NOT the last slot (`slots.Count - 1`
// == 2 -- the `Initializer` single slot follows), so `supportsIncremental` is FALSE: every
// `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's indices for a lazy rebuild
// (`EnsureChildIndices`), and `IndexOf` falls back to a linear identity search. The
// `Initializer` single slot FOLLOWS a collection, so the generator's `constIndex =
// !slots.Take(slotIndex).Any(IsCollection)` is `false`, and the `Initializer` property setter
// uses the index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear, since
// the flattened index is dynamic -- the `Arguments` count can change); the `SetChild` override
// still passes the known flattened `index` to the const-index `SetChildNode(ref field, value,
// index)`. The `Type` single slot PRECEDES the collection, so its setter uses the const-index
// `SetChildNode(ref field, value, 0)`. This is the `ComposedType` (D242) shape (a single slot
// after a collection) simplified to one collection and one trailing single slot, and the
// `Attribute` (D240) shape (a required `AstType` + a collection) plus a trailing nullable single.
//
// Its generated `DoMatch` has THREE terms in `MembersToMatch` (source declaration) order: a
// non-nullable recursive `Type` term (dispatched through `MatchRequired` -- the D231
// [class.access.derived] workaround), a collection recursive `Arguments` term
// (`this.Arguments.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection child), and
// a nullable recursive `Initializer` term (dispatched through `MatchOptional` -- the
// `BinaryOperatorExpression` D229 nullable-child path, the first nullable single child that
// FOLLOWS a collection). It is the first ported node to combine a `MatchRequired` + a
// collection-`DoMatch` + a `MatchOptional` across one `DoMatch`.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitObjectCreateExpression(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitObjectCreateExpression`). The generated
// slot statics are `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required --
// the `Type` `AstType` is non-nullable), `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>`
// pointing at `Slots.Argument`, collection), and `InitializerSlot` (a
// `CSharpSlotInfoT<ArrayInitializerExpression>` pointing at `Slots.Initializer`, optional -- the
// `Initializer` is nullable). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): a fresh node, the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
// concrete-clone pattern), the `Type` deep-cloned through the setter (which re-parents;
// `AstType::Clone()` returns `AstType*`, the covariant override), every `Arguments` element
// deep-cloned through `Add` (which re-parents and re-indexes; `Expression::Clone()` returns
// `Expression*`, which `Add(Expression*)` accepts directly), and the `Initializer` deep-cloned
// through the setter when present (which re-parents; `ArrayInitializerExpression::Clone()`
// returns `ArrayInitializerExpression*`, which `Initializer(ArrayInitializerExpression*)`
// accepts directly).
//
// NO C++ name-shadowing crux (the `Attribute` D240 differently-named-property precedent): the
// `Type()`/`Arguments()`/`Initializer()` accessors are member functions, but no class named
// `Type`/`Arguments`/`Initializer` lives in the `Syntax` namespace (there is `AstType`, not
// `Type`), and no member is named `Expression`/`AstType`/`ArrayInitializerExpression`. So no
// elaborated-type-specifier is needed anywhere, and the plain `AstType`/`Expression`/
// `ArrayInitializerExpression` resolve to the classes in every type position (the
// `MemberReferenceExpression`/`InvocationExpression` differently-named-property precedent).
//
// NO new `Slots` constant for `Type`/`Arguments`: both are already ported (`Slots::Type` by
// `Attribute` D240, `Slots::Argument` by `Attribute` D240). The `Slots::Initializer` kind is
// NEW and cycle-broken into `ArrayInitializerExpression.hpp` (the D241/D242 precedent:
// `ArrayInitializerExpression.hpp` includes `Slots.hpp` for its `ElementsSlot`, so the kind
// cannot live in `Slots.hpp` -- a circular include -- and is defined after the
// `ArrayInitializerExpression` class where both `CSharpSlotInfoT` and the concrete type are
// complete).
//
// `ObjectCreateExpression.cs` declares NO hand-written ctors (only the `NewKeyword` const and
// the three slot properties), so the port carries only the generated ctors. The generated
// collection ctors (the `(AstType, IEnumerable<Expression>)`, the `params Expression[]` form,
// and the `(AstType, IEnumerable<Expression>, ArrayInitializerExpression?)` all-params ctor) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so they
// are DEFERRED; the empty + the `(AstType)` required-prefix ctors cover the construction API
// (the `Type` is the only required ctor param -- `Arguments` is an optional collection and
// `Initializer` is an optional single). An argument list is built via `Arguments().Add(...)`
// and an initializer is set via `Initializer(...)` until `AddRange` lands. The `(AstType)` ctor
// is `explicit` (a single-argument ctor is a converting ctor by default), matching the
// generator's public ctor but avoiding an implicit `AstType` -> `ObjectCreateExpression`
// conversion.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_OBJECTCREATEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_OBJECTCREATEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ObjectCreateExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The first ported node to combine a collection with a
// nullable single child slot.
class ObjectCreateExpression final : public Expression {
public:
    ~ObjectCreateExpression() override = default;

    // The generated empty ctor (the C# `public ObjectCreateExpression()`). The `Arguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here
    // with `baseIndex = 1` (the `Type` single slot at slot 0 precedes it) and
    // `supportsIncremental = false` (the collection is the node's only collection but NOT its
    // last slot -- the `Initializer` single slot follows at slot 2 -- so an element's flattened
    // `ChildIndex` is NOT a simple `baseIndex + local position`; every `Add`/`Insert`/`Remove`/
    // single-slot-set invalidates the parent's indices for a lazy rebuild). `Type` and
    // `Initializer` default to null via their default member initializers (no type, no
    // initializer).
    ObjectCreateExpression() : arguments_(this, &ArgumentsSlot, 1, false) {}

    // The generated required-prefix ctor (the C# `public ObjectCreateExpression(AstType type)`)
    // -- the only required ctor param is `Type` (`Arguments` is an optional collection and
    // `Initializer` is an optional single). Sets `Type` in declaration order. Delegates to the
    // empty ctor so the collection member is initialized. `explicit` (a single-argument ctor is
    // a converting ctor by default).
    explicit ObjectCreateExpression(AstType* type) : ObjectCreateExpression() {
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
    // The generated `[Slot("Type")] public partial AstType Type` -- a single non-nullable
    // `AstType` slot at flattened index 0. The const-index `SetChildNode(ref field, value, 0)`
    // setter (no collection precedes it) re-parents and re-indexes in place. No name shadowing
    // (the `Type()` accessor does not collide with the `AstType` base type -- no class named
    // `Type` lives in the `Syntax` namespace, and no member is named `AstType`), so the operand
    // type is the plain `AstType` (no elaborated specifier).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `Arguments` collection slot -------------------------------------------
    // The generated `[Slot("Argument")] public partial AstNodeCollection<Expression>
    // Arguments` -- the collection of constructor argument expressions (a
    // `CSharpSlotInfoT<Expression>` slot at slot index 1). The C# lazily allocates the wrapper;
    // the D222 port makes the collection an always-present stack member, so the accessor returns
    // the member directly (the empty-until-first-Add element-list profile is preserved).
    // `supportsIncremental` is `false` (the collection is not the node's last slot -- the
    // `Initializer` single slot follows), so `Add` invalidates the parent's indices.
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The `Initializer` slot (a single NULLABLE `ArrayInitializerExpression` child) --
    // The generated `[Slot("Initializer")] public partial ArrayInitializerExpression?
    // Initializer` -- a single nullable `ArrayInitializerExpression` slot at slot index 2 (the
    // collection-initializer, e.g. `new List<int> { 1, 2 }`). A COLLECTION precedes it (`Arguments`
    // at slot 1), so the generator's `constIndex = !slots.Take(2).Any(IsCollection)` is `false`,
    // and the setter uses the index-less `SetChildNode(ref field, value)` (which invalidates on a
    // set/clear, since the flattened index is dynamic -- the `Arguments` count can change). This
    // is the `ComposedType.BaseType` (D242) pattern applied to a NULLABLE single child after a
    // collection. No name shadowing (no class named `Initializer` lives in the `Syntax`
    // namespace), so the operand type is the plain `ArrayInitializerExpression` (no elaborated
    // specifier).
    ArrayInitializerExpression* Initializer() const { return initializer_; }
    void Initializer(ArrayInitializerExpression* value) {
        SetChildNode(initializer_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
    // `Type` `AstType` is non-nullable, so `IsCollection || IsNullable` is `false`); the
    // `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`, collection);
    // the `InitializerSlot` (a `CSharpSlotInfoT<ArrayInitializerExpression>` pointing at
    // `Slots.Initializer`, optional -- the `Initializer` is nullable, so `IsNullable` is `true`).
    // No name shadowing (`AstType`/`Expression`/`ArrayInitializerExpression` resolve to the
    // classes -- no member is named any of them).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Argument, true};
    static inline const CSharpSlotInfoT<ArrayInitializerExpression> InitializerSlot{"Initializer", false, &Slots::Initializer, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitObjectCreateExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitObjectCreateExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Three slots in declaration order: a `Type` single slot at index 0, an `Arguments`
    // collection occupying the contiguous range `[1, 1 + Count)`, and an `Initializer` single
    // slot at index `1 + Count` (the trailing single slot after the collection). `GetChildCount`
    // is `2 + Count` (the two single slots plus the collection's current length);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
    // running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a single step, a collection step, then a single
    // step). `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind
    // (the node's only collection). This is the first ported node with a single slot AFTER a
    // collection that is itself NON-incremental (the `ComposedType` two-collection shape had a
    // single BETWEEN two collections; here a single TRAILS the one collection), so the dispatch
    // walk has a single -> collection -> single shape.

    int GetChildCount() const override { return 2 + arguments_.Count(); }

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
        if (i == 0)
            return initializer_;
        throw std::out_of_range("ObjectCreateExpression::GetChild");
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
        if (i == 0) {
            SetChildNode(initializer_, static_cast<ArrayInitializerExpression*>(value), index);
            return;
        }
        throw std::out_of_range("ObjectCreateExpression::SetChild");
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
        if (i == 0)
            return &InitializerSlot;
        throw std::out_of_range("ObjectCreateExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Argument)
            return &arguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ObjectCreateExpression o && this.Type.DoMatch(o.Type, match) &&
    // this.Arguments.DoMatch(o.Arguments, match) && MatchOptional(this.Initializer, o.Initializer,
    // match)`. The three terms are in `MembersToMatch` order, which is the source declaration
    // order (`Type`, `Arguments`, `Initializer`). The `Type` term is a non-nullable recursive
    // child, so the generator emits a DIRECT `this.Type.DoMatch(o.Type, match)` -- ported
    // through `MatchRequired` (the D231 [class.access.derived] workaround, since a derived
    // node may not call the protected `DoMatch` through a base `AstType*`); the `Arguments`
    // term is the collection recursive match (the generator emits the collection-typed
    // recursive term directly, NOT `MatchOptional`, which it emits only for a nullable
    // non-collection child); the `Initializer` term is a nullable non-collection recursive
    // child, so the generator emits `MatchOptional(this.Initializer, o.Initializer, match)`
    // (the `BinaryOperatorExpression` D229 nullable-child path, the first nullable single
    // child that FOLLOWS a collection). A type-only mismatch (not an `ObjectCreateExpression`)
    // rejects early. The `Type` `MatchRequired` is the first term, so a half-constructed
    // pattern (a null `Type`) rejects without crashing (the `MatchRequired` null-pattern
    // guard). The `Initializer` `MatchOptional` returns true when BOTH are absent (the common
    // `new T()` shape with no collection initializer), and delegates to the pattern's
    // `DoMatch` when the pattern carries an `Initializer`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ObjectCreateExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && arguments_.DoMatch(o->arguments_, match)
            && MatchOptional(initializer_, o->initializer_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
    // deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`,
    // the covariant override), every `Arguments` element deep-cloned through `Add` (which
    // re-parents and re-indexes; `Expression::Clone()` returns `Expression*`, which
    // `Add(Expression*)` accepts directly), and the `Initializer` deep-cloned through the
    // setter when present (which re-parents; `ArrayInitializerExpression::Clone()` returns
    // `ArrayInitializerExpression*`, which `Initializer(ArrayInitializerExpression*)` accepts
    // directly). The deep-copy order follows the slot declaration order (`Type`, `Arguments`,
    // `Initializer`) matching the generated `CloneChildrenInto`; with the `Arguments` collection
    // non-incremental every mutation invalidates, so the order does not affect the final
    // rebuilt state. No own location fields (`StartLocation`/`EndLocation` are the print-time
    // base fields set by the unported output visitor -- `ObjectCreateExpression` does not
    // derive `EndLocation`), so they are not copied (the `ConditionalExpression`/`SimpleType`/
    // `MemberType`/`Attribute`/`InvocationExpression` precedent). The covariant return is
    // `ObjectCreateExpression*` (through `Expression*`, the `Expression::Clone` pure-virtual).
    ObjectCreateExpression* Clone() const override {
        auto* node = new ObjectCreateExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        if (initializer_ != nullptr)
            node->Initializer(initializer_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is null until the type is set (a required slot --
    // `CheckInvariant` asserts it is filled); `initializer_` is null until the collection
    // initializer is set (an OPTIONAL slot -- `CheckInvariant` passes with it null); `arguments_`
    // is the always-present collection member (empty until the first `Add`, non-incremental). No
    // name shadowing (no member is named `AstType`/`Expression`/`ArrayInitializerExpression`),
    // so the field types are the plain classes.
    AstType* type_ = nullptr;
    AstNodeCollectionT<Expression> arguments_;
    ArrayInitializerExpression* initializer_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_OBJECTCREATEEXPRESSION_HPP
