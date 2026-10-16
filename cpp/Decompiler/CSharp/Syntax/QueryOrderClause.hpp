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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `QueryOrderClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryOrderClause.g.cs` + the hand-written partial, which declares the `OrderbyKeyword`
// const string and the `Orderings` slot property -- no ctors, no helpers). The next-in-order
// Phase-5 piece per the D309 plan. `orderby_clause ::= 'orderby' ordering ( ',' ordering )*`
// (C# grammar 12.23.1): an element of a `QueryExpression.Clauses` collection (the
// `orderby ...` ordering clause), a sealed `QueryClause` whose sole child slot is the
// `Orderings` collection of `QueryOrdering` (the comma-separated ordering list).
//
// It is the `ArrayInitializerExpression` D250 / `TupleExpression` D296 /
// `InterpolatedStringExpression` D309 collection-only shape applied to the `QueryClause`
// hierarchy: the `Orderings` collection (an `AstNodeCollection<QueryOrdering>`) is the node's
// only slot and its last slot, so `supportsIncremental` is true (an element's flattened
// `ChildIndex` is exactly its local position) and `GetChildCount` is the collection's current
// length (an empty node reports `GetChildCount` 0). `QueryOrderClause` carries a plain
// `[DecompilerAstNode]` (no arg), so `hasPatternPlaceholder` defaults to false and the C#
// `sealed` ports to C++ `final`.
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Orderings.DoMatch(o.Orderings, match)` (the generator emits the collection-typed
// recursive term directly -- NOT `MatchOptional`, which it emits only for a nullable
// NON-collection child). Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is
// hand-translated from the generated output rather than regenerated. The generated
// `AcceptVisitor` calls `visitor.VisitQueryOrderClause(this)` (the class name does not end in
// "AstType", so the generator's visit-method-name default yields `VisitQueryOrderClause`). The
// generated slot static is `OrderingsSlot` (a `CSharpSlotInfoT<QueryOrdering>` pointing at
// `Slots.Ordering`, collection).
//
// NO C++ name-shadowing crux: the `Orderings()` accessor is a member function, but no class
// named `Orderings` lives in the `Syntax` namespace, and no member is named `QueryOrdering`
// (the `Orderings` accessor does not collide with the `QueryOrdering` element type -- the
// `ArrayInitializerExpression`/`TupleExpression` differently-named-property precedent), so no
// elaborated-type-specifier is needed anywhere.
//
// NO new `Slots` constant in `Slots.hpp`: the `[Slot("Ordering")]` argument names the slot
// kind "Ordering", added THIS iteration cycle-broken into `QueryOrdering.hpp` (after the
// `QueryOrdering` class, since `QueryOrdering.hpp` includes `Slots.hpp` for its per-node
// `ExpressionSlot` -- the `Slots::Attribute`/`Slots::Variable`/`Slots::ConstructorInitializer`
// cycle-breaking precedent D241/D267/D281). The kind carries identity + the element type
// only; the per-position `IsCollection`/`IsOptional` flags live on the per-node slot.
//
// `QueryOrderClause.cs` declares NO hand-written ctors (only the `Orderings` slot property),
// so the port carries only the generated ctors. The generated collection ctors
// (`QueryOrderClause(IEnumerable<QueryOrdering>)` and the `params QueryOrdering[]` form) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so
// they are DEFERRED; the empty ctor is the only portable ctor, and an ordering list is built
// via `Orderings().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYORDERCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYORDERCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/QueryOrdering.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryOrderClause : QueryClause` -- the orderby clause
// node. `final` (the C# is `sealed`; `[DecompilerAstNode]` with no arg means
// `hasPatternPlaceholder` defaults to false, so no `PatternPlaceholder` derives from it). The
// collection-only shape: the only child slot is the `Orderings` collection.
class QueryOrderClause final : public QueryClause {
public:
    ~QueryOrderClause() override = default;

    // The C# `public const string OrderbyKeyword = "orderby"` -- the token the output visitor
    // emits for the `orderby` keyword. Compile-time literal carried as `static constexpr const
    // char*` (a static field, not instance state, so it is not part of
    // `MembersToMatch`/`DoMatch`).
    static constexpr const char* OrderbyKeyword = "orderby";

    // The generated empty ctor (the C# `public QueryOrderClause()`). The `Orderings`
    // collection is a member (the D222 always-present-stack-member design), initialized here
    // with `baseIndex = 0` (it is the node's only slot, so the first element's flattened
    // `ChildIndex` is 0) and `supportsIncremental = true` (it is the node's only collection and
    // last slot, so an element's flattened `ChildIndex` is exactly its local position). The
    // collection starts empty (no orderings); the node has no required single slots, so a
    // default-constructed node is a valid empty orderby (`CheckInvariant` passes).
    QueryOrderClause() : orderings_(this, &OrderingsSlot, 0, true) {}

    // ---- The `Orderings` collection slot -----------------------------------------
    // The generated `[Slot("Ordering")] public partial AstNodeCollection<QueryOrdering>
    // Orderings` -- the collection of orderings (a `CSharpSlotInfoT<QueryOrdering>` slot at
    // flattened index 0, the node's only collection and last slot). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly (the empty-until-first-Add element-list profile is
    // preserved -- `list_` is empty until the first `Add`).
    AstNodeCollectionT<QueryOrdering>& Orderings() { return orderings_; }
    const AstNodeCollectionT<QueryOrdering>& Orderings() const { return orderings_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag
    // is true (the slot is a collection); the `IsOptional` flag is true (the generator sets it
    // true for every collection slot). The kind is `Slots::Ordering` (the shared "Ordering" slot
    // name, cycle-broken into `QueryOrdering.hpp` this iteration). No name shadowing (no member
    // is named `QueryOrdering`), so the element type is the plain `QueryOrdering`.
    static inline const CSharpSlotInfoT<QueryOrdering> OrderingsSlot{"Orderings", true, &Slots::Ordering, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryOrderClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryOrderClause(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitQueryOrderClause`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitQueryOrderClause(this);
    }

    // ---- Slot storage (the generated overrides) ----------------------------------------
    // A single `Orderings` collection occupying the contiguous range [0, Count).
    // `GetChildCount` is the collection's current length (an empty node reports 0 -- no
    // single-slot count term); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single
    // collection slot (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape with one collection step and no single step).
    // `GetCollectionByKind` returns the `Orderings` collection for the `Ordering` kind.

    int GetChildCount() const override { return orderings_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = orderings_.Count();
        if (i < n)
            return orderings_.At(i);
        throw std::out_of_range("QueryOrderClause::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = orderings_.Count();
        if (i < n) {
            orderings_.SetAt(i, static_cast<QueryOrdering*>(value));
            return;
        }
        throw std::out_of_range("QueryOrderClause::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = orderings_.Count();
        if (i < n)
            return &OrderingsSlot;
        throw std::out_of_range("QueryOrderClause::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Ordering)
            return &orderings_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryOrderClause o && this.Orderings.DoMatch(o.Orderings, match)`. The
    // single term is the collection recursive match (the generator emits the collection-typed
    // recursive term directly, NOT `MatchOptional`, which it emits only for a nullable
    // NON-collection child). A type-only mismatch (not a `QueryOrderClause`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryOrderClause*>(other);
        if (o == nullptr)
            return false;
        return orderings_.DoMatch(o->orderings_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Orderings` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `QueryOrdering::Clone()` returns `QueryOrdering*`, which `Add(QueryOrdering*)` accepts
    // directly -- `QueryOrdering` derives directly from `AstNode` and its `Clone` is covariant
    // through `AstNode*`, no `static_cast` needed). No own location fields (`StartLocation`/
    // `EndLocation` are the print-time base fields set by the unported output visitor), so
    // they are not copied (the `ArrayInitializerExpression`/`TupleExpression` precedent for
    // nodes without derived locations). The covariant return is `QueryOrderClause*` (through
    // `QueryClause*`, the `QueryClause::Clone` pure-virtual).
    QueryOrderClause* Clone() const override {
        auto* node = new QueryOrderClause();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < orderings_.Count(); i++)
            node->orderings_.Add(orderings_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `orderings_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no member is named `QueryOrdering`), so the field type
    // is the plain class.
    AstNodeCollectionT<QueryOrdering> orderings_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYORDERCLAUSE_HPP
