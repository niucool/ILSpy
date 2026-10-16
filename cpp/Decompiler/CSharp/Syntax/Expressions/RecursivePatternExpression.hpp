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

// Port of the `RecursivePatternExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/RecursivePatternExpression.cs (the generated
// `RecursivePatternExpression.g.cs` + the hand-written partial, which declares only the three
// slot properties and the `IsPositional` bool scalar, no ctors, no helpers). The next in-order
// Phase-5 piece per the D307 plan ("RecursivePatternExpression -- the nullable-AstType-plus-
// Expression-collection-plus-VariableDesignation-trailing-single shape"):
// `recursive_pattern ::= type? '{' pattern* '}' variable_designation?`
//                     | `type? '(' pattern* ')' variable_designation?` (C# grammar 11.2.5/11.2.6)
// -- a recursive pattern is an optional `Type` reference (the type being matched, e.g. `Point`
// in `Point { X = 1, Y = 2 }`), a `SubPatterns` collection of `Expression` (the nested patterns
// -- the C# AST models the pattern DSL as `Expression` nodes), an optional `Designation`
// (`VariableDesignation`, the trailing `var x`/`x`/`(x, y)` declaration), and an `IsPositional`
// bool distinguishing the `(...)` positional form from the `{...}` property form.
//
// It is the `ObjectCreateExpression` (D251) shape (a single + a non-incremental collection + a
// trailing nullable single) with TWO divergences: the leading `Type` single is NULLABLE (not
// required -- `AstType?`), and the trailing `Designation` is a `VariableDesignation?` (not an
// `ArrayInitializerExpression?`). PLUS a non-`[Slot]` `IsPositional` bool scalar. The slot
// layout in source declaration order is: `Type` (a single NULLABLE `AstType` at slot 0),
// `SubPatterns` (an `AstNodeCollection<Expression>` collection at slot 1), and `Designation` (a
// single NULLABLE `VariableDesignation` at slot 2). The generator's `supportsIncremental` flag
// is `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 1` but the
// `SubPatterns` collection is at `slotIndex == 1`, NOT the last slot (`slots.Count - 1` == 2 --
// the `Designation` single slot follows), so `supportsIncremental` is FALSE: every
// `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's indices for a lazy rebuild
// (`EnsureChildIndices`), and `IndexOf` falls back to a linear identity search (the
// `ObjectCreateExpression` D251 non-incremental-collection precedent). The `Designation`
// single slot FOLLOWS a collection, so the generator's `constIndex =
// !slots.Take(slotIndex).Any(IsCollection)` is `false`, and the `Designation` property setter
// uses the index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear, since
// the flattened index is dynamic -- the `SubPatterns` count can change); the `SetChild` override
// still passes the known flattened `index` to the const-index `SetChildNode(ref field, value,
// index)`. The `Type` single slot PRECEDES the collection, so its setter uses the const-index
// `SetChildNode(ref field, value, 0)` (the `ObjectCreateExpression.Type` precedent).
//
// Its generated `DoMatch` has FOUR terms in `MembersToMatch` (source declaration) order: a
// nullable recursive `Type` term (dispatched through `MatchOptional` -- the
// `BinaryOperatorExpression` D229 nullable-child path), a collection recursive `SubPatterns`
// term (`this.SubPatterns.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection child), a
// nullable recursive `Designation` term (dispatched through `MatchOptional` -- the first
// nullable single child that FOLLOWS a collection, the `ObjectCreateExpression.Initializer`
// D251 precedent), and a plain-equality `IsPositional` bool term (the
// `ObjectCreateExpression`/`ComposedType`/`Accessor` plain-bool precedent -- a bool is not an
// enum, so no `Any`-wildcard, and not a string, so no `MatchString`). It is the first ported
// node to combine two `MatchOptional` terms (the leading `Type` and the trailing `Designation`)
// with a collection-`DoMatch` term and a plain-bool term across one `DoMatch`.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitRecursivePatternExpression(this)` (the class name does not end in "AstType", so
// the generator's visit-method-name default yields `VisitRecursivePatternExpression`). The
// generated slot statics are `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`,
// optional -- the `Type` `AstType` is nullable, so `IsOptional` is `true`), `SubPatternsSlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.SubPattern`, collection), and
// `DesignationSlot` (a `CSharpSlotInfoT<VariableDesignation>` pointing at
// `Slots.VariableDesignation`, optional -- the `Designation` is nullable, so `IsOptional` is
// `true`). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the `IsPositional` scalar copied, the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
// concrete-clone pattern), the `Type` deep-cloned through the setter when present (which
// re-parents; `AstType::Clone()` returns `AstType*`, the covariant override), every `SubPatterns`
// element deep-cloned through `Add` (which re-parents and re-indexes; `Expression::Clone()`
// returns `Expression*`, which `Add(Expression*)` accepts directly), and the `Designation`
// deep-cloned through the setter when present (which re-parents;
// `VariableDesignation::Clone()` returns `VariableDesignation*`, the covariant override, which
// `Designation(VariableDesignation*)` accepts directly).
//
// NO C++ name-shadowing crux (the `ObjectCreateExpression` D251 / `Attribute` D240
// differently-named-property precedent): the `Type()`/`SubPatterns()`/`Designation()`/
// `IsPositional()` accessors are member functions, but no class named `Type`/`SubPatterns`/
// `Designation`/`IsPositional` lives in the `Syntax` namespace (there is `AstType`, not `Type`;
// `VariableDesignation`, not `Designation`), and no member is named `Expression`/`AstType`/
// `VariableDesignation`. So no elaborated-type-specifier is needed anywhere, and the plain
// `AstType`/`Expression`/`VariableDesignation` resolve to the classes in every type position.
//
// ONE new `Slots` constant: `Slots::SubPattern` (a `CSharpSlotInfoT<Expression>` collection kind,
// the `SubPatterns` position -- the `[Slot("SubPattern")]` argument names the kind). It lives in
// `Slots.hpp` with no include cycle since `Expression.hpp` (the abstract base) does NOT include
// `Slots.hpp` (the `Slots.Argument`/`Slots.EnumMemberInitializer` precedent). `Slots::Type`
// (by `Attribute` D240) and `Slots::VariableDesignation` (by `ParenthesizedVariableDesignation`
// D264) are both already ported, so they are reused with no new constant.
//
// `RecursivePatternExpression.cs` declares NO hand-written ctors (only the three slot properties
// and the `IsPositional` scalar), so the port carries only the generated ctors. The generated
// collection ctors (the `(AstType?, IEnumerable<Expression>)` and the `params Expression[]` forms
// at constructor-prefix length 2, and the `(AstType?, IEnumerable<Expression>,
// VariableDesignation?)` all-params ctor at length 3) use `AddRange`, which lands with the
// collection convenience mutators (the D222 deferral), so they are DEFERRED; the empty ctor is the
// only portable ctor -- a node is built via `Type(...)`/`SubPatterns().Add(...)`/
// `Designation(...)`/`IsPositional(true)` until `AddRange` lands. `RequiredConstructorPrefixLength`
// is 0 (every ctor param -- `Type`/`SubPatterns`/`Designation` -- is optional), so only the full
// constructor-prefix length is emitted, and that one calls `AddRange` for the `SubPatterns`
// collection (deferred). `IsPositional` is a bool (not an enum), so the generator's
// `CtorParams`-adds-only-settable-enum-typed-scalars rule (line 186-191) excludes it from
// `CtorParams` -- it is set via the property setter.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_RECURSIVEPATTERNEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_RECURSIVEPATTERNEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class RecursivePatternExpression : Expression`. `final` (the
// C# `sealed`): no further derivation (`[DecompilerAstNode]` with no arg means
// `hasPatternPlaceholder` defaults to `false`, so no `PatternPlaceholder` derives from it).
class RecursivePatternExpression final : public Expression {
public:
    ~RecursivePatternExpression() override = default;

    // The generated empty ctor (the C# `public RecursivePatternExpression()`). The `SubPatterns`
    // collection is a member (the D222 always-present-stack-member design), initialized here
    // with `baseIndex = 1` (the `Type` single slot at slot 0 precedes it) and
    // `supportsIncremental = false` (the collection is the node's only collection but NOT its
    // last slot -- the `Designation` single slot follows at slot 2 -- so an element's flattened
    // `ChildIndex` is NOT a simple `baseIndex + local position`; every `Add`/`Insert`/`Remove`/
    // single-slot-set invalidates the parent's indices for a lazy rebuild). `Type` and
    // `Designation` default to null via their default member initializers (no type, no
    // designation); `IsPositional` defaults to false.
    RecursivePatternExpression() : subPatterns_(this, &SubPatternsSlot, 1, false) {}

    // ---- The `Type` slot (a single NULLABLE `AstType` child) -------------------------
    // The generated `[Slot("Type")] public partial AstType? Type` -- a single nullable
    // `AstType` slot at flattened index 0. The const-index `SetChildNode(ref field, value, 0)`
    // setter (no collection precedes it) re-parents and re-indexes in place. No name shadowing
    // (the `Type()` accessor does not collide with the `AstType` base type -- no class named
    // `Type` lives in the `Syntax` namespace, the `ObjectCreateExpression` D251 precedent), so
    // the operand type is the plain `AstType` (no elaborated specifier).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `SubPatterns` collection slot -------------------------------------------
    // The generated `[Slot("SubPattern")] public partial AstNodeCollection<Expression>
    // SubPatterns` -- the collection of nested pattern expressions (a
    // `CSharpSlotInfoT<Expression>` slot at slot index 1). The C# lazily allocates the wrapper;
    // the D222 port makes the collection an always-present stack member, so the accessor returns
    // the member directly (the empty-until-first-Add element-list profile is preserved).
    // `supportsIncremental` is `false` (the collection is not the node's last slot -- the
    // `Designation` single slot follows), so `Add` invalidates the parent's indices (the
    // `ObjectCreateExpression` D251 non-incremental-collection precedent).
    AstNodeCollectionT<Expression>& SubPatterns() { return subPatterns_; }
    const AstNodeCollectionT<Expression>& SubPatterns() const { return subPatterns_; }

    // ---- The `Designation` slot (a single NULLABLE `VariableDesignation` child) ------
    // The generated `[Slot("VariableDesignation")] public partial VariableDesignation?
    // Designation` -- a single nullable `VariableDesignation` slot at slot index 2 (the
    // trailing `var x`/`x`/`(x, y)` declaration of a recursive pattern). A COLLECTION precedes it
    // (`SubPatterns` at slot 1), so the generator's `constIndex =
    // !slots.Take(2).Any(IsCollection)` is `false`, and the setter uses the index-less
    // `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the flattened
    // index is dynamic -- the `SubPatterns` count can change). This is the
    // `ObjectCreateExpression.Initializer` (D251) pattern applied to a NULLABLE single child
    // after a collection. No name shadowing (no class named `Designation` lives in the `Syntax`
    // namespace -- there is `VariableDesignation`, not `Designation`), so the operand type is
    // the plain `VariableDesignation` (no elaborated specifier).
    VariableDesignation* Designation() const { return designation_; }
    void Designation(VariableDesignation* value) {
        SetChildNode(designation_, value);
    }

    // ---- The `IsPositional` scalar (a non-[Slot] bool) -------------------------------
    // The C# `public bool IsPositional { get; set; }` -- distinguishes the `(...)` positional
    // form (true) from the `{...}` property form (false). A plain non-[Slot] bool property, not
    // an enum (so not a ctor param) and not a string. No name shadowing (`IsPositional` is not a
    // class name), so the plain `bool` resolves.
    bool IsPositional() const { return isPositional_; }
    void IsPositional(bool value) { isPositional_ = value; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, optional -- the
    // `Type` `AstType` is nullable, so `IsOptional` is `true`); the `SubPatternsSlot` (a
    // `CSharpSlotInfoT<Expression>` pointing at `Slots.SubPattern`, collection); the
    // `DesignationSlot` (a `CSharpSlotInfoT<VariableDesignation>` pointing at
    // `Slots.VariableDesignation`, optional -- the `Designation` is nullable, so `IsOptional`
    // is `true`). No name shadowing (`AstType`/`Expression`/`VariableDesignation` resolve to the
    // classes -- no member is named any of them).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, true};
    static inline const CSharpSlotInfoT<Expression> SubPatternsSlot{"SubPatterns", true, &Slots::SubPattern, true};
    static inline const CSharpSlotInfoT<VariableDesignation> DesignationSlot{"Designation", false, &Slots::VariableDesignation, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitRecursivePatternExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitRecursivePatternExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitRecursivePatternExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitRecursivePatternExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Three slots in declaration order: a `Type` single slot at index 0, a `SubPatterns`
    // collection occupying the contiguous range `[1, 1 + Count)`, and a `Designation` single
    // slot at index `1 + Count` (the trailing single slot after the collection). `GetChildCount`
    // is `2 + Count` (the two single slots plus the collection's current length);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
    // running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a single step, a collection step, then a single
    // step). `GetCollectionByKind` returns the `SubPatterns` collection for the `SubPattern`
    // kind (the node's only collection). This is the `ObjectCreateExpression` (D251) single ->
    // collection -> single dispatch shape.

    int GetChildCount() const override { return 2 + subPatterns_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return type_;
        i--;
        {
            int n = subPatterns_.Count();
            if (i < n)
                return subPatterns_.At(i);
            i -= n;
        }
        if (i == 0)
            return designation_;
        throw std::out_of_range("RecursivePatternExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(type_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        {
            int n = subPatterns_.Count();
            if (i < n) {
                subPatterns_.SetAt(i, static_cast<Expression*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(designation_, static_cast<VariableDesignation*>(value), index);
            return;
        }
        throw std::out_of_range("RecursivePatternExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TypeSlot;
        i--;
        {
            int n = subPatterns_.Count();
            if (i < n)
                return &SubPatternsSlot;
            i -= n;
        }
        if (i == 0)
            return &DesignationSlot;
        throw std::out_of_range("RecursivePatternExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::SubPattern)
            return &subPatterns_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is RecursivePatternExpression o && MatchOptional(this.Type, o.Type, match)
    // && this.SubPatterns.DoMatch(o.SubPatterns, match) && MatchOptional(this.Designation,
    // o.Designation, match) && this.IsPositional == o.IsPositional`. The four terms are in
    // `MembersToMatch` order, which is the source declaration order (`Type`, `SubPatterns`,
    // `Designation`, `IsPositional`). The `Type` term is a nullable non-collection recursive
    // child, so the generator emits `MatchOptional` (the `BinaryOperatorExpression` D229
    // nullable-child path); the `SubPatterns` term is the collection recursive match (the
    // generator emits the collection-typed recursive term directly, NOT `MatchOptional`); the
    // `Designation` term is a nullable non-collection recursive child that FOLLOWS a collection,
    // so the generator emits `MatchOptional` (the first nullable single child after a
    // collection, the `ObjectCreateExpression.Initializer` D251 precedent); the `IsPositional`
    // term is a non-recursive, non-enum, non-string bool, so the generator emits the plain
    // `this.IsPositional == o.IsPositional` (the `Accessor` D274 / `ObjectCreateExpression`
    // plain-bool fall-through). A type-only mismatch (not a `RecursivePatternExpression`)
    // rejects early. `MatchOptional` returns true when BOTH sides are absent (the common bare
    // `{ ... }` / `( ... )` shape with no type and no designation), and delegates to the
    // pattern's `DoMatch` when the pattern carries the child.

protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<RecursivePatternExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(type_, o->type_, match)
            && subPatterns_.DoMatch(o->subPatterns_, match)
            && MatchOptional(designation_, o->designation_, match)
            && isPositional_ == o->isPositional_;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the `IsPositional` scalar copied, the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern), the `Type` deep-cloned through the setter when present (which
    // re-parents; `AstType::Clone()` returns `AstType*`, the covariant override), every
    // `SubPatterns` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Expression::Clone()` returns `Expression*`, which `Add(Expression*)` accepts directly),
    // and the `Designation` deep-cloned through the setter when present (which re-parents;
    // `VariableDesignation::Clone()` returns `VariableDesignation*`, the covariant override,
    // which `Designation(VariableDesignation*)` accepts directly). The deep-copy order follows
    // the slot declaration order (`Type`, `SubPatterns`, `Designation`) matching the generated
    // `CloneChildrenInto`; with the `SubPatterns` collection non-incremental every mutation
    // invalidates, so the order does not affect the final rebuilt state. No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `RecursivePatternExpression` does not derive `EndLocation`), so they are not
    // copied (the `ObjectCreateExpression`/`ConditionalExpression` no-location-copy precedent).
    // The covariant return is `RecursivePatternExpression*` (through `Expression*`, the
    // `Expression::Clone` pure-virtual).
    RecursivePatternExpression* Clone() const override {
        auto* node = new RecursivePatternExpression();
        node->isPositional_ = isPositional_;
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        for (int i = 0; i < subPatterns_.Count(); i++)
            node->subPatterns_.Add(subPatterns_.At(i)->Clone());
        if (designation_ != nullptr)
            node->Designation(designation_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is null until the type is set (an OPTIONAL slot --
    // `CheckInvariant` passes with it null); `designation_` is null until the designation is set
    // (an OPTIONAL slot -- `CheckInvariant` passes with it null); `subPatterns_` is the
    // always-present collection member (empty until the first `Add`, non-incremental);
    // `isPositional_` defaults to false. No name shadowing (no member is named
    // `AstType`/`Expression`/`VariableDesignation`), so the field types are the plain classes.
    AstType* type_ = nullptr;
    AstNodeCollectionT<Expression> subPatterns_;
    VariableDesignation* designation_ = nullptr;
    bool isPositional_ = false;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_RECURSIVEPATTERNEXPRESSION_HPP
