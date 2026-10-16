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

// Port of the `ParenthesizedVariableDesignation` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/VariableDesignation.cs (the generated
// `ParenthesizedVariableDesignation.g.cs` + the hand-written partial). The third piece of the
// `VariableDesignation` hierarchy (the next in-order Phase-5 piece per the D263 plan:
// `ForeachStatement` needs the `VariableDesignation` hierarchy). `tuple_designation ::= '('
// designations? ')'` (C# grammar 11.2.4): a `ParenthesizedVariableDesignation` is the
// deconstruction designation for a tuple -- a `VariableDesignation` whose sole child slot is the
// `VariableDesignations` collection of `VariableDesignation` (the comma-separated nested
// designations inside the parentheses). It is the `ArrayInitializerExpression` D250 /
// `BlockStatement` D256 collection-only shape applied to the `VariableDesignation` hierarchy:
// the SIMPLEST collection-slot shape (the only node with a COLLECTION slot and NO single child
// slot in this hierarchy), and a sealed leaf whose only value is the nested-designation list.
//
// The `VariableDesignations` collection (an `AstNodeCollection<VariableDesignation>`) is the
// node's only slot and its last slot, so `supportsIncremental` is true (an element's flattened
// `ChildIndex` is exactly its local position) and `GetChildCount` is the collection's current
// length (an empty node reports `GetChildCount` 0). The generated `DoMatch` has a single term:
// the collection recursive match `this.VariableDesignations.DoMatch(o.VariableDesignations,
// match)` (the generator emits the collection-typed recursive term directly -- NOT
// `MatchOptional`, which it emits only for a nullable non-collection child). Per PORT_PLAN.md
// section 5.2 / decision D1 the concrete node is hand-translated from the generated output rather
// than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitParenthesizedVariableDesignation(this)` (`ParenthesizedVariableDesignation` does
// not end in "AstType", so the generator's visit-method-name default yields
// `VisitParenthesizedVariableDesignation`). The generated slot static is `VariableDesignationsSlot`
// (a `CSharpSlotInfoT<VariableDesignation>` pointing at `Slots.VariableDesignation`,
// collection). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`).
//
// NO C++ name-shadowing crux: the `VariableDesignations()` accessor is a member function, but no
// class named `VariableDesignations` lives in the `Syntax` namespace, and no member is named
// `VariableDesignation` (the `[Slot("VariableDesignation")]` argument names the slot KIND
// "VariableDesignation" but the PROPERTY is "VariableDesignations", so the accessor is
// `VariableDesignations()`, NOT `VariableDesignation()` -- neither shadows the
// `VariableDesignation` class), so no elaborated-type-specifier (`class VariableDesignation`) is
// needed anywhere, and the plain `VariableDesignation` resolves to the base class in every type
// position (the `BlockStatement` D256 differently-named-property precedent applied to the
// designation hierarchy). The `VariableDesignation` element type is complete via the
// `VariableDesignation.hpp` include (transitively, via this header's own base include).
//
// NO new `Slots` constant on THIS node: the `[Slot("VariableDesignation")]` argument names a NEW
// slot kind not yet ported, a `CSharpSlotInfoT<VariableDesignation>` -- it is ADDED to `Slots.hpp`
// this iteration (the `VariableDesignation` kind). `VariableDesignation.hpp` does NOT include
// `Slots.hpp` (the abstract base has no per-node slot statics -- the `Slots.Statement`/`Slots.
// ArraySpecifier` precedent applied to the designation base), so the kind lives in `Slots.hpp`
// with no include cycle.
//
// `ParenthesizedVariableDesignation.cs` declares NO hand-written ctors (only the
// `VariableDesignations` slot property), so the port carries only the generated ctors. The
// generated collection ctors (`ParenthesizedVariableDesignation(IEnumerable<VariableDesignation>)`
// and the `params VariableDesignation[]` form) use `AddRange`, which lands with the collection
// convenience mutators (the D222 deferral), so they are DEFERRED; the empty ctor is the only
// portable ctor, and a nested-designation list is built via `VariableDesignations().Add(...)`
// until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PARENTHESIZEDVARIABLEDESIGNATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PARENTHESIZEDVARIABLEDESIGNATION_HPP

#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ParenthesizedVariableDesignation : VariableDesignation`.
// `final` (the C# `sealed`): no further derivation. The collection-only node of the
// `VariableDesignation` hierarchy (the `ArrayInitializerExpression` D250 / `BlockStatement` D256
// shape with a `VariableDesignation` base and element type, no const keyword, no single child
// slot).
class ParenthesizedVariableDesignation final : public VariableDesignation {
public:
    ~ParenthesizedVariableDesignation() override = default;

    // The generated empty ctor (the C# `public ParenthesizedVariableDesignation()`). The
    // `VariableDesignations` collection is a member (the D222 always-present-stack-member
    // design), initialized here with `baseIndex = 0` (it is the node's only slot, so the first
    // element's flattened `ChildIndex` is 0) and `supportsIncremental = true` (it is the node's
    // only collection and its last slot, so an element's flattened `ChildIndex` is exactly its
    // local position). The collection starts empty (no designations); the node has no required
    // single slots, so a default-constructed node is a valid empty tuple designation
    // (`CheckInvariant` passes).
    ParenthesizedVariableDesignation() : variableDesignations_(this, &VariableDesignationsSlot, 0, true) {}

    // ---- The `VariableDesignations` collection slot -----------------------------------
    // The generated `[Slot("VariableDesignation")] public partial AstNodeCollection<VariableDesignation>
    // VariableDesignations` -- the collection of nested designations (a `CSharpSlotInfoT<VariableDesignation>`
    // slot at flattened index 0, the node's only collection and last slot). The C# lazily
    // allocates the wrapper; the D222 port makes the collection an always-present stack member,
    // so the accessor returns the member directly (the empty-until-first-Add element-list profile
    // is preserved -- `list_` is empty until the first `Add`).
    AstNodeCollectionT<VariableDesignation>& VariableDesignations() { return variableDesignations_; }
    const AstNodeCollectionT<VariableDesignation>& VariableDesignations() const { return variableDesignations_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag is
    // true (the slot is a collection); the `IsOptional` flag is true (the generator sets it true
    // for every collection slot). The kind is `Slots::VariableDesignation` (the shared
    // "VariableDesignation" slot name, added this iteration to `Slots.hpp`). No name shadowing
    // (no member is named `VariableDesignation` -- the property is `VariableDesignations`), so
    // the element type is the plain `VariableDesignation` (the abstract base, complete via the
    // `VariableDesignation.hpp` base include).
    static inline const CSharpSlotInfoT<VariableDesignation> VariableDesignationsSlot{"VariableDesignations", true, &Slots::VariableDesignation, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitParenthesizedVariableDesignation`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitParenthesizedVariableDesignation(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitParenthesizedVariableDesignation`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitParenthesizedVariableDesignation(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `VariableDesignations` collection occupying the contiguous range [0, Count).
    // `GetChildCount` is the collection's current length (an empty node reports 0 -- no
    // single-slot count term); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single
    // collection slot (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections`
    // shape with one collection step and no single step). `GetCollectionByKind` returns the
    // `VariableDesignations` collection for the `VariableDesignation` kind.

    int GetChildCount() const override { return variableDesignations_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = variableDesignations_.Count();
        if (i < n)
            return variableDesignations_.At(i);
        throw std::out_of_range("ParenthesizedVariableDesignation::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = variableDesignations_.Count();
        if (i < n) {
            variableDesignations_.SetAt(i, static_cast<VariableDesignation*>(value));
            return;
        }
        throw std::out_of_range("ParenthesizedVariableDesignation::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = variableDesignations_.Count();
        if (i < n)
            return &VariableDesignationsSlot;
        throw std::out_of_range("ParenthesizedVariableDesignation::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::VariableDesignation)
            return &variableDesignations_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ParenthesizedVariableDesignation o &&
    // this.VariableDesignations.DoMatch(o.VariableDesignations, match)`. The single term is the
    // collection recursive match (the generator emits the collection-typed recursive term
    // directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child). A
    // type-only mismatch (not a `ParenthesizedVariableDesignation`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ParenthesizedVariableDesignation*>(other);
        if (o == nullptr)
            return false;
        return variableDesignations_.DoMatch(o->variableDesignations_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `VariableDesignations` element deep-cloned through `Add` (which re-parents and
    // re-indexes; `VariableDesignation::Clone()` returns `VariableDesignation*` -- the covariant
    // pure-virtual on the abstract base -- which `Add(VariableDesignation*)` accepts directly).
    // No own location fields (`StartLocation`/`EndLocation` are the print-time base fields set by
    // the unported output visitor), so they are not copied (the `ArrayInitializerExpression` D250
    // / `BlockStatement` D256 no-location-copy precedent). The covariant return is
    // `ParenthesizedVariableDesignation*` (through `VariableDesignation*`, the
    // `VariableDesignation::Clone` pure-virtual).
    ParenthesizedVariableDesignation* Clone() const override {
        auto* node = new ParenthesizedVariableDesignation();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < variableDesignations_.Count(); i++)
            node->variableDesignations_.Add(variableDesignations_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `variableDesignations_` is the always-present collection member (empty
    // until the first `Add`). No name shadowing (no member is named `VariableDesignation`), so
    // the field type is the plain `VariableDesignation` (the abstract base).
    AstNodeCollectionT<VariableDesignation> variableDesignations_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PARENTHESIZEDVARIABLEDESIGNATION_HPP
