// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction including without limitation the rights to use, copy, modify, merge,
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

// Port of the `Constraint` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/Constraint.cs (the generated
// `Constraint.g.cs` + the hand-written partial, which declares only the two slot properties --
// no ctors, no helpers). The next in-order Phase-5 piece per the D282 plan: the dependency of
// `MethodDeclaration.Constraints` (its `Constraints` collection -- the `where` clauses of a
// generic method/declaration -- that `MethodDeclaration` declares as an
// `AstNodeCollection<Constraint>`; `Constraint` ports before `MethodDeclaration` so the
// element type is available). `type_parameter_constraints_clause ::= 'where' type ':' type+`
// (C# grammar 15.2.5): the `where T : ...` clause on a generic method or type -- a
// `SimpleType` naming the type parameter the clause constrains (`T`), followed by one or more
// base-type `AstType` constraints (the `: Base1, Base2, ...` list -- `new()`, `struct`, and
// `class` constraints are represented using a `PrimitiveType` `"new"`/`"struct"`/`"class"`, all
// `AstType`-derived).
//
// The hand-written partial declares only the two slot properties. It is a sealed `AstNode`
// (deriving DIRECTLY from the `AstNode` root, NOT `EntityDeclaration`/`Expression`/`Statement`/
// `AstType` -- a constraint is a structural node owned by a declaration's `Constraints`
// collection, not a member declaration; it carries no `SymbolKind`/`Modifiers`/
// `MatchAttributesAndModifiers` -- the `VariableInitializer` D266 / `CatchClause` D269 /
// `ParameterDeclaration` D278 / `TypeParameterDeclaration` D282 direct-`AstNode` precedent).
// The `[DecompilerAstNode]` default `hasPatternPlaceholder` is `false`, so `final` (the C#
// `sealed`).
//
// The two slots in source declaration order: a REQUIRED (non-nullable) `SimpleType`
// `TypeParameter` `[Slot("ConstraintTypeParameter")]` single slot at flattened index 0 (the
// type parameter the clause constrains -- the `T` in `where T : ...`; the slot is the node's
// FIRST slot and no collection precedes it, so the generator's
// `constIndex = !slots.Take(0).Any(IsCollection)` is `true` and the `TypeParameter` setter uses
// the const-index `SetChildNode(ref field, value, 0)`); and a `BaseTypes`
// `AstNodeCollection<AstType>` collection `[Slot("BaseType")]` at flattened index 1 (the
// `: Base1, Base2, ...` list -- the base-type constraints, each an `AstType`). It is
// structurally the `SwitchStatement` D268 shape (a single required child at index 0 + a
// collection at index 1, incremental) but with a direct-`AstNode` base (not `Statement`) and
// the child `SimpleType` (not `Expression`) and the collection element `AstType` (not
// `SwitchSection`). The generator's `supportsIncremental` flag is
// `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 1` and the
// `BaseTypes` collection IS the last slot (`slotIndex == 1 == slots.Count - 1`), so
// `supportsIncremental` is `TRUE`: an element's flattened `ChildIndex` is exactly
// `1 + its local position`, maintained incrementally by `Add`/`Insert`/`Remove`. The
// `TypeParameter` single slot PRECEDES the collection (no collection precedes it), so its
// setter uses the const-index `SetChildNode(ref field, value, 0)`.
//
// Its generated `DoMatch` has TWO terms in `MembersToMatch` (source declaration) order: a
// non-nullable recursive `TypeParameter` term (dispatched through `MatchRequired` -- the D231
// `[class.access.derived]` workaround, since a derived node may not call the protected
// `DoMatch` through a base `SimpleType*`) and a collection recursive `BaseTypes` term
// (`this.BaseTypes.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child). There
// is no scalar term (the hand-written partial declares no scalars -- only the two slot
// properties). So the generated `DoMatch` is:
// `return other is Constraint o && this.TypeParameter.DoMatch(o.TypeParameter, match) &&
// this.BaseTypes.DoMatch(o.BaseTypes, match)`. A type-only mismatch (not a `Constraint`) rejects
// early.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitConstraint(this)` (the class name does not end in "AstType", so the generator's
// visit-method-name default yields `VisitConstraint`). The generated slot statics are
// `TypeParameterSlot` (a `CSharpSlotInfoT<SimpleType>` pointing at `Slots.ConstraintTypeParameter`,
// required -- the `TypeParameter` is non-nullable) and `BaseTypesSlot` (a
// `CSharpSlotInfoT<AstType>` pointing at `Slots.BaseType`, collection). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the `TypeParameter` through the setter (which re-parents; `SimpleType::Clone()`
// returns `SimpleType*`, the covariant override, which `TypeParameter(SimpleType*)` accepts
// directly) and every `BaseTypes` element through `Add` (which re-parents and re-indexes;
// `AstType::Clone()` returns `AstType*`, which `Add(AstType*)` accepts directly), and copies the
// annotation channel.
//
// C++ name-shadowing crux: NONE. The `TypeParameter()` accessor does NOT collide with any class
// in the `Syntax` namespace (no class named `TypeParameter` -- there is `TypeParameterDeclaration`,
// not `TypeParameter`), and the `BaseTypes()` accessor does NOT collide either (no class named
// `BaseTypes`). The element types (`SimpleType`/`AstType`) do not collide with any member name
// (no member is named `SimpleType`/`AstType`), so no elaborated-type-specifier is needed anywhere.
// This is the cleanest single-child-plus-incremental-collection port so far: no name shadowing,
// no new `AstNode` helper (`MatchRequired` from D231 + the collection `DoMatch` from D222 already
// exist).
//
// NO new `Slots` constant lives in `Slots.hpp` this iteration:
// - `Slots::BaseType` (a `CSharpSlotInfoT<AstType>`, the collection of base-type `AstType`
//   constraints) ports INTO `Slots.hpp` (the `AstType` abstract base does NOT include `Slots.hpp`
//   -- the `Slots.TypeArgument`/`Slots.Type`/`Slots.PrivateImplementationType` precedent -- so no
//   include cycle). No `Slots` variable is named `AstType`, and no class named `BaseType` lives in
//   `Syntax`, so the unqualified `AstType` resolves to the class and no elaborated specifier is
//   needed.
// - `Slots::ConstraintTypeParameter` (a `CSharpSlotInfoT<SimpleType>`, the single `SimpleType` of
//   the constrained type parameter) is cycle-broken into `SimpleType.hpp` after the `SimpleType`
//   class (`SimpleType.hpp` includes `Slots.hpp` for its per-node `IdentifierTokenSlot`/
//   `TypeArgumentsSlot`, so the kind cannot live in `Slots.hpp` -- a circular include -- and is
//   defined in `SimpleType.hpp` after the class, the `Slots::Attribute` D241 /
//   `Slots::AttributeSection` D242 / `Slots::Parameter` D279 cycle-breaking precedent applied to a
//   `SimpleType`-typed single kind). The kind name `ConstraintTypeParameter` collides with no class
//   in `Syntax` (there is `TypeParameterDeclaration`, not `ConstraintTypeParameter`), so no
//   elaborated specifier is needed.
//
// `Constraint.cs` declares NO hand-written ctors (only the two slot properties), so the port
// carries only the generated ctors. The generated collection ctors (the
// `(SimpleType, IEnumerable<AstType>)` all-params ctor and any `params` overload) use `AddRange`,
// which lands with the collection convenience mutators (the D222 deferral), so they are DEFERRED;
// the empty + the `(SimpleType)` required-prefix ctors cover the construction API (the
// `TypeParameter` is the only required ctor param before the `BaseTypes` collection). A base-types
// list is built via `BaseTypes().Add(...)` until `AddRange` lands. The `(SimpleType)` ctor is
// `explicit` (a single-argument ctor is a converting ctor by default), matching the generator's
// public ctor but avoiding an implicit `SimpleType -> Constraint` conversion (the
// `SwitchStatement` D268 / `InvocationExpression` D248 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRAINT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRAINT_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class Constraint : AstNode`. `final` (the C# `sealed`): no
// further derivation. The `SwitchStatement` D268 shape (a single required child at index 0 + an
// incremental collection at index 1) with a direct-`AstNode` base, the child `SimpleType`, and the
// collection element `AstType`.
class Constraint final : public AstNode {
public:
    ~Constraint() override = default;

    // The generated empty ctor (the C# `public Constraint()`). The `BaseTypes` collection is a
    // member (the D222 always-present-stack-member design), initialized here with `baseIndex = 1`
    // (the `TypeParameter` single slot at index 0 precedes it) and `supportsIncremental = true`
    // (it is the node's only collection and its last slot, so an element's flattened `ChildIndex` is
    // exactly `1 + its local position`). `TypeParameter` defaults to null (no constrained type
    // parameter); it is a required slot, so a default-constructed node is only valid until the type
    // parameter is set (or until `DoMatch`/`CheckInvariant` observe the missing slot).
    Constraint() : baseTypes_(this, &BaseTypesSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public Constraint(SimpleType typeParameter)`):
    // the required prefix runs through the last non-optional ctor param (`TypeParameter` is
    // required; `BaseTypes` is an optional collection). Sets `TypeParameter` in declaration order.
    // Delegates to the empty ctor so the collection member is initialized. `explicit` (a
    // single-argument ctor is a converting ctor by default), matching the generator's public ctor
    // but avoiding an implicit `SimpleType -> Constraint` conversion (the `SwitchStatement` D268
    // precedent).
    explicit Constraint(SimpleType* typeParameter) : Constraint() {
        TypeParameter(typeParameter);
    }

    // ---- The `TypeParameter` slot (a single REQUIRED `SimpleType` child) ----------------
    // The generated `[Slot("ConstraintTypeParameter")] public partial SimpleType TypeParameter` --
    // a single non-nullable `SimpleType` slot at flattened index 0 (the type parameter the
    // `where` clause constrains, the `T` in `where T : ...`). The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. NO name shadowing (the `TypeParameter()` accessor does NOT collide
    // with any class in `Syntax` -- no class named `TypeParameter` -- there is
    // `TypeParameterDeclaration`, not `TypeParameter`), so the element type is the plain
    // `SimpleType`.
    SimpleType* TypeParameter() const { return typeParameter_; }
    void TypeParameter(SimpleType* value) {
        SetChildNode(typeParameter_, value, 0);
    }

    // ---- The `BaseTypes` collection slot ------------------------------------------------
    // The generated `[Slot("BaseType")] public partial AstNodeCollection<AstType> BaseTypes` --
    // the collection of base-type `AstType` constraints (the `: Base1, Base2, ...` list of a
    // `where T : ...` clause; `new()`/`struct`/`class` are `PrimitiveType`s) at flattened index 1,
    // the node's only collection and last slot. The C# lazily allocates the wrapper; the D222 port
    // makes the collection an always-present stack member, so the accessor returns the member
    // directly (the empty-until-first-`Add` element-list profile is preserved).
    // `supportsIncremental` is `true` (the node's only collection and last slot), so `Add`
    // maintains each element's flattened `ChildIndex` incrementally. NO name shadowing (the
    // `BaseTypes()` accessor does NOT collide with any class -- no class named `BaseTypes`), so
    // the `AstType` element type needs no elaborated specifier.
    AstNodeCollectionT<AstType>& BaseTypes() { return baseTypes_; }
    const AstNodeCollectionT<AstType>& BaseTypes() const { return baseTypes_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // The `TypeParameterSlot` (a `CSharpSlotInfoT<SimpleType>` pointing at
    // `Slots.ConstraintTypeParameter`, required -- the `TypeParameter` is non-nullable, so
    // `IsCollection || IsOptional` is `false`); the `BaseTypesSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.BaseType`, collection). NO name shadowing (no member is named
    // `SimpleType`/`AstType`), so the element types are the plain classes. `Slots.ConstraintTypeParameter`
    // is cycle-broken into `SimpleType.hpp` (visible here via the `SimpleType.hpp` include);
    // `Slots.BaseType` lives in `Slots.hpp`.
    static inline const CSharpSlotInfoT<SimpleType> TypeParameterSlot{"TypeParameter", false, &Slots::ConstraintTypeParameter, false};
    static inline const CSharpSlotInfoT<AstType> BaseTypesSlot{"BaseTypes", true, &Slots::BaseType, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitConstraint`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitConstraint(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitConstraint`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitConstraint(this);
    }

    // ---- Slot storage (the generated overrides) -----------------------------------------
    // A `TypeParameter` single slot at index 0 and a `BaseTypes` collection occupying the
    // contiguous range `[1, 1 + Count)`. `GetChildCount` is `1 + Count` (the single slot plus the
    // collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single case
    // then a collection step). `GetCollectionByKind` returns the `BaseTypes` collection for the
    // `BaseType` kind (the node's only collection). This is the `SwitchStatement` D268 dispatch
    // shape.

    int GetChildCount() const override { return 1 + baseTypes_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return typeParameter_;
        i--;
        int n = baseTypes_.Count();
        if (i < n)
            return baseTypes_.At(i);
        throw std::out_of_range("Constraint::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(typeParameter_, static_cast<SimpleType*>(value), index);
            return;
        }
        i--;
        int n = baseTypes_.Count();
        if (i < n) {
            baseTypes_.SetAt(i, static_cast<AstType*>(value));
            return;
        }
        throw std::out_of_range("Constraint::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TypeParameterSlot;
        i--;
        int n = baseTypes_.Count();
        if (i < n)
            return &BaseTypesSlot;
        throw std::out_of_range("Constraint::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::BaseType)
            return &baseTypes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is Constraint o && this.TypeParameter.DoMatch(o.TypeParameter, match) &&
    // this.BaseTypes.DoMatch(o.BaseTypes, match)`. The terms are in `MembersToMatch` order, which
    // is the source declaration order (`TypeParameter`, `BaseTypes`). The `TypeParameter` term is a
    // non-nullable recursive child, so the generator emits a DIRECT
    // `this.TypeParameter.DoMatch(o.TypeParameter, match)` -- ported through `MatchRequired` (the
    // D231 `[class.access.derived]` workaround); the `BaseTypes` term is the collection recursive
    // match (the generator emits the collection-typed recursive term directly, NOT `MatchOptional`,
    // which it emits only for a nullable non-collection child). A type-only mismatch (not a
    // `Constraint`) rejects early. The `TypeParameter` `MatchRequired` is the first term, so a
    // half-constructed pattern (a null `TypeParameter`) rejects without crashing (the
    // `MatchRequired` null-pattern guard).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<Constraint*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(typeParameter_, o->typeParameter_, match)
            && baseTypes_.DoMatch(o->baseTypes_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `TypeParameter` deep-cloned through the setter (which re-parents; `SimpleType::Clone()`
    // returns `SimpleType*`, the covariant override, which `TypeParameter(SimpleType*)` accepts
    // directly), and every `BaseTypes` element deep-cloned through `Add` (which re-parents and
    // re-indexes; `AstType::Clone()` returns `AstType*`, which `Add(AstType*)` accepts directly).
    // No own location fields (`Constraint` does not derive `EndLocation`), so the print-time
    // `StartLocation`/`EndLocation` are not copied (the `SwitchStatement` D268 no-location-copy
    // precedent). The covariant return is `Constraint*` (through `AstNode*`, the `AstNode::Clone`
    // virtual).
    Constraint* Clone() const override {
        auto* node = new Constraint();
        node->CloneAnnotationsFrom(*this);
        if (typeParameter_ != nullptr)
            node->TypeParameter(typeParameter_->Clone());
        for (int i = 0; i < baseTypes_.Count(); i++)
            node->baseTypes_.Add(baseTypes_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `typeParameter_` is null until the constrained type parameter is set (a
    // required slot -- `CheckInvariant` asserts it is filled); `baseTypes_` is the always-present
    // collection member (empty until the first `Add`, incremental). NO name shadowing (no member is
    // named `SimpleType`/`AstType`), so the field types are the plain classes.
    SimpleType* typeParameter_ = nullptr;
    AstNodeCollectionT<AstType> baseTypes_;
};

// The `Constraint` kind -- a collection of `Constraint` (the generic `where` clauses of a generic
// method or type: `MethodDeclaration.Constraints`/`TypeDeclaration.Constraints`/`DelegateDeclaration.
// Constraints`, an `AstNodeCollection<Constraint>`). Unique to `MethodDeclaration` among the ported
// nodes (the first owning declaration); `TypeDeclaration`/`DelegateDeclaration` will reuse it when
// they port.
//
// Defined HERE (in `Constraint.hpp`, after the `Constraint` class) rather than in `Slots.hpp`
// because `CSharpSlotInfoT<Constraint>` needs `Constraint` complete (the `dynamic_cast<const
// Constraint*>` is-a test in the ctor), and `Constraint` is a concrete node with per-node slot
// statics (its `TypeParameterSlot`/`BaseTypesSlot` reference `&Slots::ConstraintTypeParameter`/
// `&Slots::BaseType`, so this header includes `Slots.hpp`). Placing the kind in `Slots.hpp` would
// form a circular include: `Slots.hpp` would have to include `Constraint.hpp` (for the complete
// `Constraint`), but `Constraint.hpp` includes `Slots.hpp` (for `Slots::ConstraintTypeParameter`/
// `Slots::BaseType`), and with `Slots.hpp`'s guard set those definitions would not be visible where
// `Constraint`'s class body needs them. After the class both `CSharpSlotInfoT` (visible via the
// `Slots.hpp` include) and `Constraint` are complete, so the kind defines cleanly. The `inline`
// variable still has external linkage and one address across translation units (the C++17
// `inline` guarantee), preserving the pointer-identity comparison `node.Slot.Kind ==
// &Slots::Constraint` the slot system relies on. This is the `Slots::Attribute` D241 /
// `Slots::AttributeSection` D242 / `Slots::Parameter` D279 / `Slots::Variable` D267 /
// `Slots::ConstructorInitializer` D281 / `Slots::TypeParameter` (just added this iteration into
// `TypeParameterDeclaration.hpp`) cycle-breaking precedent applied to a `Constraint`-typed
// collection kind. The shared constant is constructed non-collection/non-optional (`{"Constraint",
// false, nullptr, false}`); the per-node `ConstraintsSlot` on the owning node carries the
// `IsCollection` flag. The kind name `Constraint` lives in the `Slots` namespace and collides with
// no class in the `Syntax` namespace (the `Constraint` CLASS is in the parent `Syntax` namespace,
// the `Slots::Constraint` VARIABLE is in the nested `Slots` namespace -- distinct scopes, no
// collision; the `Slots::Variable` D267 precedent where the kind name matches no class in the same
// scope), so no elaborated-type-specifier is needed.
namespace Slots {
inline const CSharpSlotInfoT<Constraint> Constraint{"Constraint", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRAINT_HPP
