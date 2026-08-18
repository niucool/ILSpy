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

// Port of the `QueryExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryExpression.g.cs` + the hand-written partial, which declares only the `Clauses` slot
// property -- no ctors, no helpers, no const strings). The next in-order Phase-5 piece per the
// D309 plan. `query_expression ::= query_clause+` (C# grammar 12.23.1): a `QueryExpression`
// is a sealed `Expression` whose sole child slot is the `Clauses` collection of `QueryClause`
// (the comma- (and implicitly semicolon-) separated clause list of the query).
//
// It is the `ArrayInitializerExpression` D250 / `TupleExpression` D296 /
// `AnonymousTypeCreateExpression` D304 / `InterpolatedStringExpression` D309 collection-only
// shape applied to the query-expression production: the `Clauses` collection (an
// `AstNodeCollection<QueryClause>`) is the node's only slot and its last slot, so
// `supportsIncremental` is true (an element's flattened `ChildIndex` is exactly its local
// position) and `GetChildCount` is the collection's current length (an empty node reports
// `GetChildCount` 0). `QueryExpression` carries a plain `[DecompilerAstNode]` (no arg), so
// `hasPatternPlaceholder` defaults to false and the C# `sealed` ports to C++ `final`.
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Clauses.DoMatch(o.Clauses, match)` (the generator emits the collection-typed recursive
// term directly -- NOT `MatchOptional`, which it emits only for a nullable NON-collection
// child). Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from
// the generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitQueryExpression(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitQueryExpression`). The generated slot
// static is `ClausesSlot` (a `CSharpSlotInfoT<QueryClause>` pointing at `Slots.Clause`,
// collection).
//
// NO C++ name-shadowing crux: the `Clauses()` accessor is a member function, but no class
// named `Clauses` lives in the `Syntax` namespace, and no member is named `Expression`
// (the `Clauses` accessor does not collide with the `Expression` base type -- it is
// `Clauses()`, not `Expression()`), so no elaborated-type-specifier (`class Expression`) is
// needed anywhere, and the plain `Expression` resolves to the base class in every type
// position (the `ArrayInitializerExpression`/`TupleExpression` differently-named-property
// precedent).
//
// ONE new `Slots` constant in `Slots.hpp`: `Slots::Clause` (a `CSharpSlotInfoT<QueryClause>`
// collection kind, the `Clauses` position). `QueryClause.hpp` (the abstract base) does NOT
// include `Slots.hpp` (no per-node slot statics), so the kind lives in `Slots.hpp` with a
// `QueryClause.hpp` include and no include cycle (the `Slots.Statement`/`Slots.ArraySpecifier`/
// `Slots.Content` precedent applied to a `QueryClause` abstract-base element type). The kind
// name `Clause` collides with no class in the `Syntax` namespace, so no
// elaborated-type-specifier is needed.
//
// `QueryExpression.cs` declares NO hand-written ctors (only the `Clauses` slot property), so
// the port carries only the generated ctors. The generated collection ctors
// (`QueryExpression(IEnumerable<QueryClause>)` and the `params QueryClause[]` form) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so they
// are DEFERRED; the empty ctor is the only portable ctor, and a clause list is built via
// `Clauses().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_QUERYEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_QUERYEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryExpression : Expression` -- the query-expression
// node. `final` (the C# is `sealed`; `[DecompilerAstNode]` with no arg means
// `hasPatternPlaceholder` defaults to false, so no `PatternPlaceholder` derives from it). The
// collection-only shape: the only child slot is the `Clauses` collection.
class QueryExpression final : public Expression {
public:
    ~QueryExpression() override = default;

    // The generated empty ctor (the C# `public QueryExpression()`). The `Clauses` collection
    // is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (it is the node's only slot, so the first element's flattened
    // `ChildIndex` is 0) and `supportsIncremental = true` (it is the node's only collection and
    // last slot, so an element's flattened `ChildIndex` is exactly its local position). The
    // collection starts empty (no clauses); the node has no required single slots, so a
    // default-constructed node is a valid empty query (`CheckInvariant` passes).
    QueryExpression() : clauses_(this, &ClausesSlot, 0, true) {}

    // ---- The `Clauses` collection slot -----------------------------------------
    // The generated `[Slot("Clause")] public partial AstNodeCollection<QueryClause> Clauses`
    // -- the collection of query clauses (a `CSharpSlotInfoT<QueryClause>` slot at flattened
    // index 0, the node's only collection and last slot). The C# lazily allocates the wrapper;
    // the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-Add element-list profile is
    // preserved -- `list_` is empty until the first `Add`).
    AstNodeCollectionT<QueryClause>& Clauses() { return clauses_; }
    const AstNodeCollectionT<QueryClause>& Clauses() const { return clauses_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag
    // is true (the slot is a collection); the `IsOptional` flag is true (the generator sets it
    // true for every collection slot). The kind is `Slots::Clause` (the shared "Clause" slot
    // name, added to Slots.hpp this iteration). No name shadowing (no member is named
    // `QueryClause`), so the element type is the plain `QueryClause`.
    static inline const CSharpSlotInfoT<QueryClause> ClausesSlot{"Clauses", true, &Slots::Clause, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryExpression(this);
    }

    // ---- Slot storage (the generated overrides) ----------------------------------------
    // A single `Clauses` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- no single-slot count
    // term); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single collection slot (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape
    // with one collection step and no single step). `GetCollectionByKind` returns the
    // `Clauses` collection for the `Clause` kind.

    int GetChildCount() const override { return clauses_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = clauses_.Count();
        if (i < n)
            return clauses_.At(i);
        throw std::out_of_range("QueryExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = clauses_.Count();
        if (i < n) {
            clauses_.SetAt(i, static_cast<QueryClause*>(value));
            return;
        }
        throw std::out_of_range("QueryExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = clauses_.Count();
        if (i < n)
            return &ClausesSlot;
        throw std::out_of_range("QueryExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Clause)
            return &clauses_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryExpression o && this.Clauses.DoMatch(o.Clauses, match)`. The single
    // term is the collection recursive match (the generator emits the collection-typed
    // recursive term directly, NOT `MatchOptional`, which it emits only for a nullable
    // NON-collection child). A type-only mismatch (not a `QueryExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryExpression*>(other);
        if (o == nullptr)
            return false;
        return clauses_.DoMatch(o->clauses_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Clauses` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `QueryClause::Clone()` returns `QueryClause*`, the covariant override on the abstract
    // base, which `Add(QueryClause*)` accepts directly -- no `static_cast`, since
    // `QueryClause` redeclares a typed covariant `Clone`, the `InterpolatedStringContent`
    // D309 abstract-base-collection precedent). No own location fields (`StartLocation`/
    // `EndLocation` are the print-time base fields set by the unported output visitor), so
    // they are not copied (the `ArrayInitializerExpression`/`ConditionalExpression`/`SimpleType`
    // precedent for nodes without derived locations). The covariant return is
    // `QueryExpression*` (through `Expression*`, the `Expression::Clone` pure-virtual).
    QueryExpression* Clone() const override {
        auto* node = new QueryExpression();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < clauses_.Count(); i++)
            node->clauses_.Add(clauses_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `clauses_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no member is named `QueryClause`), so the field type
    // is the plain class.
    AstNodeCollectionT<QueryClause> clauses_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_QUERYEXPRESSION_HPP
