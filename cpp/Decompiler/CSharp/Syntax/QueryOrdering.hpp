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

// Port of the `QueryOrdering` concrete node and the `QueryOrderingDirection` enum in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryOrdering.g.cs` + the hand-written partial, which declares the `AscendingKeyword`/
// `DescendingKeyword` const strings and the `Expression` slot property and the `Direction`
// scalar property -- no ctors, no helpers). The next in-order Phase-5 piece per the D309
// plan. `ordering ::= expression ( 'ascending' | 'descending' )?` (C# grammar 12.23.1): an
// element of a `QueryOrderClause.Orderings` collection (an `expression` plus an optional
// ordering direction), the dependency unblocking `QueryOrderClause`.
//
// `QueryOrdering` derives DIRECTLY from `AstNode` (NOT `QueryClause` -- it is a structural
// element of `QueryOrderClause.Orderings`, not itself a query clause). It is the
// `UnaryOperatorExpression` D231 shape (a single, REQUIRED (non-nullable) `Expression` child
// slot plus a scalar enum) where the scalar enum (`QueryOrderingDirection`) declares NO `Any`
// member, so its generated `DoMatch` uses a PLAIN equality term
// (`this.Direction == o.Direction`) rather than an `Any`-wildcard term -- the
// `DirectionExpression.FieldDirection` D235 no-`Any`-enum precedent. It also carries two
// keyword const strings (`AscendingKeyword`/`DescendingKeyword`), the tokens the output
// visitor emits for the `ascending`/`descending` modifiers (the `CheckedKeyword`/
// `UncheckedKeyword` D234 precedent: a const string is a value, part of the node's public API
// surface, so it ports now as a `static constexpr const char*`).
//
// The single `[Slot("Expression")] Expression Expression` child (the operand, non-nullable in
// the C# source) is at flattened index 0; the slot kind is `Expression`, so the per-node
// `ExpressionSlot` REUSES the already-ported `Slots::Expression` kind (the same kind
// `UnaryOperatorExpression` registered in D231) -- no new `Slots.hpp` constant. The generator
// emits the const-index `SetChildNode(ref field, value, 0)` setter (the single slot is the
// first and only slot), the `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides
// over the one single slot, and the `DoMatch` `return other is QueryOrdering o &&
// this.Expression.DoMatch(o.Expression, match) && this.Direction == o.Direction`.
//
// C++ name-shadowing crux #1 (the `Expression` property): the C# property is `Expression` of
// type `Expression` (a property named the same as its type). The faithful port names the
// accessor `Expression()`, which SHADOWS the `Expression` base type within this class scope
// (C++ unqualified name lookup finds the member and stops, even though it is not a type -- the
// D224 `Annotation<T>()`-shadows-the-`Annotation`-type crux, the D231 `UnaryOperatorExpression`
// / D243 `CastExpression` precedent). Every type usage AFTER the `Expression()` getter is
// declared therefore uses the ELABORATED-TYPE-SPECIFIER `class Expression`
// (basic.lookup.elab: an elaborated specifier ignores non-type names and finds the hidden
// class), so the setter parameter, the slot static, the `static_cast`, and the backing field
// all spell the operand type as `class Expression`. The ctor parameter and the getter return
// type precede the getter's declaration, so they use the plain `Expression` (no member
// function is in scope there yet).
//
// C++ name-shadowing crux #2 (the `Direction` property): the C# property is `Direction` of
// type `QueryOrderingDirection`. The accessor name `Direction()` does NOT collide with the
// `QueryOrderingDirection` enum (the property name differs from the enum name), so NO
// elaborated enum specifier is needed (the `Accessor.Kind` D274 / `ParameterDeclaration.
// ParameterModifier`/`ReferenceKind` D278 accessor-name-differs-from-enum-name precedent); the
// plain `QueryOrderingDirection` resolves in both the getter and setter signatures.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYORDERING_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYORDERING_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum QueryOrderingDirection` -- the direction of an `orderby` ordering.
// `None` is the zero value (the C# default for an uninitialized `Direction` property -- no
// explicit `ascending`/`descending`, which the language treats as ascending). Unlike
// `UnaryOperatorType`/`BinaryOperatorType`/`AssignmentOperatorType`, this enum declares NO
// `Any` member, so the generator's `hasAny` path does not fire and the generated `DoMatch`
// term is the plain `this.Direction == o.Direction` (no `Any`-wildcard) -- the
// `DirectionExpression.FieldDirection` D235 no-`Any`-enum precedent. A `std::uint8_t`-based
// `enum class` (the `SymbolKind` D271 / `ReferenceKind` D278 / `VarianceModifier` D282
// TypeSystem-enum precedent applied to a Syntax-namespace enum).
enum class QueryOrderingDirection {
    // No explicit direction (the language default -- treated as ascending).
    None,
    // `ascending` ordering modifier.
    Ascending,
    // `descending` ordering modifier.
    Descending
};

// The C# `public sealed partial class QueryOrdering : AstNode`. `final` (the C# `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it). Derives DIRECTLY from `AstNode` (NOT `QueryClause`).
// The `UnaryOperatorExpression` shape (a single required `Expression` slot + a scalar enum)
// where the scalar enum has no `Any` member.
class QueryOrdering final : public AstNode {
public:
    ~QueryOrdering() override = default;

    // The C# `public const string AscendingKeyword = "ascending"` / `DescendingKeyword =
    // "descending"` -- the tokens the output visitor emits for the `ascending`/`descending`
    // modifiers. Compile-time literals carried as `static constexpr const char*` (static
    // fields, not instance state, so they are not part of `MembersToMatch`/`DoMatch`).
    static constexpr const char* AscendingKeyword = "ascending";
    static constexpr const char* DescendingKeyword = "descending";

    // The generated empty ctor (the C# `public QueryOrdering()`). `Direction` defaults to
    // `None` (the enum's zero value, the C# default); `Expression` defaults to null (no
    // operand). A null operand violates the required-slot invariant, so a default-constructed
    // node is only valid until `Expression` is set (or until `DoMatch`/`CheckInvariant`
    // observe the missing child).
    QueryOrdering() = default;

    // The generated all-params ctor (the C# `public QueryOrdering(Expression expression)`).
    // `Direction` is NOT a ctor param (the generator adds only settable ENUM-typed scalars to
    // `CtorParams`, and `Direction` is the only scalar; a settable enum IS a ctor param, so
    // `Direction` is added too -- the `OperatorDeclaration.OperatorType` D280 precedent).
    // `CtorParams` is `[Expression, Direction]` in source declaration order (`Expression` is
    // declared before `Direction` in the .cs source), so `Expression` precedes `Direction`
    // in the generated all-params ctor. The ctor body is `this.Expression = expression;
    // this.Direction = direction;`. `RequiredConstructorPrefixLength` is 2 (through the last
    // non-optional param -- both are non-optional), `ConstructorPrefixLengths` is {2} (the
    // full count only, no shorter prefix, no `params` overload since there is no collection),
    // yielding the empty + the `(Expression, QueryOrderingDirection)` all-params ctor. The
    // `Expression` parameter type precedes the `Expression()` getter, so the plain
    // `Expression` (the base type) is unshadowed here. The `Direction` scalar is assigned
    // directly to the backing field (not via the setter) to match the
    // `DirectionExpression.FieldDirection` D235 direct-field-assignment pattern (the setter
    // call is unambiguous here since `Direction` != `QueryOrderingDirection`, but the
    // direct-assignment is the faithful equivalent and keeps the field-init order
    // deterministic). The body is in complete-class context, so the `Expression(expression)`
    // call resolves to the setter declared below.
    QueryOrdering(Expression* expression, QueryOrderingDirection direction)
        : QueryOrdering() {
        Expression(expression);
        direction_ = direction;
    }

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The const-index `SetChildNode(ref field,
    // value, 0)` setter (the single slot is the first and only slot, no collection precedes
    // it). The C# getter returns the backing field null-forgiving (`field!`) because the slot
    // is required; the port returns the raw pointer (a required slot is non-null only by
    // invariant, not by type).
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The C# `public QueryOrderingDirection Direction { get; set; }` -- a scalar enum (not a
    // `[Slot]`). A settable enum-typed scalar the generator adds to `MembersToMatch` (with
    // the PLAIN equality term, since `QueryOrderingDirection` has no `Any` member) and to the
    // ctor params. NO name shadowing (the `Direction()` accessor name differs from the
    // `QueryOrderingDirection` enum name), so the plain `QueryOrderingDirection` resolves in
    // both signatures.
    QueryOrderingDirection Direction() const { return direction_; }
    void Direction(QueryOrderingDirection value) { direction_ = value; }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable);
    // the kind carries identity only. `Slots::Expression` already exists (added for
    // `UnaryOperatorExpression` in D231), so no new `Slots.hpp` constant. The element type
    // uses the elaborated `class Expression` (the `Expression()` accessor shadows the base
    // type in this scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryOrdering` (`QueryOrdering` does not end in "AstType", so the
    // generator's visit-method-name default yields `VisitQueryOrdering`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryOrdering(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so `GetChildCount`
    // is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch
    // (the generator's `WriteReturnDispatchSwitch` shape, with a single case). The
    // `Direction` scalar is NOT a slot, so it does not appear here.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("QueryOrdering::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("QueryOrdering::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("QueryOrdering::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryOrdering o && this.Expression.DoMatch(o.Expression, match) &&
    // this.Direction == o.Direction`. `Expression` is a NON-NULLABLE recursive child, so the
    // generator emits the direct `this.Expression.DoMatch(o.Expression, match)` term (NOT
    // `MatchOptional`); `Direction` is a settable enum with NO `Any` member, so the generator
    // emits the PLAIN `this.Direction == o.Direction` term (the `DoMatchTerm` fall-through
    // for a non-`Any` enum scalar -- NOT the `== Any || == o.Field` wildcard). The
    // `AscendingKeyword`/`DescendingKeyword` const strings are static fields, not instance
    // properties, so they are not part of `MembersToMatch` and do not appear here. A type-only
    // mismatch (not a `QueryOrdering`) rejects early.
    //
    // The C# direct dispatch (`this.Expression.DoMatch`) assumes the required child is
    // present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing
    // operand defensively (a null pattern child does not match; the C# would null-deref),
    // and a null candidate child flows through the operand's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (the operand always set) the behavior is
    // identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryOrdering*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && direction_ == o->direction_;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the scalar `Direction`, deep-clones the
    // child through the setter (which re-parents and re-indexes via `SetChildNode`), and
    // copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). The print-time `StartLocation`/`EndLocation` are not stored on
    // this node (no own location fields -- the base fields hold the print-time span), so only
    // the scalar + child + annotation channel are copied. The `static_cast` uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in
    // this scope). The child is skipped if absent (`Clone` tolerates a missing child even
    // though the slot is required -- the invariant is enforced by `CheckInvariant`, not by
    // `Clone`).
    QueryOrdering* Clone() const override {
        auto* node = new QueryOrdering();
        node->direction_ = direction_;
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field for the `Expression` slot uses the elaborated `class Expression`
    // (the `Expression()` accessor declared above shadows the `Expression` base type in this
    // class scope). A required slot is non-null only by invariant, so the pointer is null
    // until the operand is set. The `Direction` scalar backing field uses the plain
    // `QueryOrderingDirection` (no name shadowing -- the accessor name `Direction` differs
    // from the enum name); `None` is the enum's zero value (the C# default).
    class Expression* expression_ = nullptr;
    QueryOrderingDirection direction_ = QueryOrderingDirection::None;
};

// The `Ordering` kind -- the collection slot kind for
// `[Slot("Ordering")] AstNodeCollection<QueryOrdering>` (`QueryOrderClause.Orderings`). A
// `CSharpSlotInfoT<QueryOrdering>` (the element type is the concrete `QueryOrdering` node).
//
// Defined HERE (in QueryOrdering.hpp, after the `QueryOrdering` class) rather than in
// Slots.hpp because `CSharpSlotInfoT<QueryOrdering>` needs `QueryOrdering` complete (the
// `dynamic_cast<const QueryOrdering*>` is-a test in the ctor), and `QueryOrdering` is a
// concrete node with a per-node slot static (its `ExpressionSlot` references
// `&Slots::Expression`, so this header includes Slots.hpp). Placing the kind in Slots.hpp
// would form a circular include: Slots.hpp would have to include QueryOrdering.hpp (for the
// complete `QueryOrdering`), but QueryOrdering.hpp includes Slots.hpp (for `Slots::Expression`),
// and with Slots.hpp's guard set those definitions would not be visible where QueryOrdering's
// class body needs them. After the class both `CSharpSlotInfoT` (visible via the Slots.hpp
// include) and `QueryOrdering` are complete, so the kind defines cleanly. The `inline`
// variable still has external linkage and one address across translation units (the C++17
// `inline` guarantee), preserving the pointer-identity comparison `node.Slot.Kind ==
// &Slots::Ordering` the slot system relies on. This is the `Slots::Attribute`/`Slots::Variable`/
// `Slots::ConstructorInitializer` cycle-breaking precedent (D241/D267/D281) applied to a
// collection kind. The shared constant is constructed non-collection/non-optional
// (`{"Ordering", false, nullptr, false}`); the per-node `OrderingsSlot` on `QueryOrderClause`
// carries the `IsCollection` flag (the collection `[Slot]` makes the per-node slot a
// collection). The kind name `Ordering` collides with no class in the `Syntax` namespace
// (there is `QueryOrdering`, not `Ordering`), so no elaborated-type-specifier is needed.
namespace Slots {
inline const CSharpSlotInfoT<QueryOrdering> Ordering{"Ordering", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYORDERING_HPP
