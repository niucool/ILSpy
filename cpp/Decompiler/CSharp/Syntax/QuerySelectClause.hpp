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

// Port of the `QuerySelectClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QuerySelectClause.g.cs` + the hand-written partial, which declares the `SelectKeyword`
// const string and the `Expression` slot property -- no ctors, no helpers). The next-in-order
// Phase-5 piece per the D309 plan. `select_clause ::= 'select' expression` (C# grammar
// 12.23.1): an element of a `QueryExpression.Clauses` collection (the `select expr` projection
// clause), a sealed `QueryClause` with a single required `Expression` child.
//
// It is the `UnaryOperatorExpression` D231 single-required-`Expression`-slot shape applied to
// the `QueryClause` hierarchy: a single, REQUIRED (non-nullable) `[Slot("Expression")]
// Expression Expression` child at flattened index 0. The slot kind is `Expression`, so the
// per-node `ExpressionSlot` REUSES the already-ported `Slots::Expression` kind (the same kind
// `UnaryOperatorExpression` registered in D231) -- no new `Slots.hpp` constant. The generator
// emits the const-index `SetChildNode(ref field, value, 0)` setter (the single slot is the
// first and only slot), the `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides
// over the one single slot, and the `DoMatch` `return other is QuerySelectClause o &&
// this.Expression.DoMatch(o.Expression, match)`.
//
// It also carries one keyword const string (`SelectKeyword`), the token the output visitor
// emits for the `select` keyword (the `CheckedKeyword`/`UncheckedKeyword` D234 precedent: a
// const string is a value, part of the node's public API surface, so it ports now as a
// `static constexpr const char*`).
//
// C++ name-shadowing crux (the `Expression` property): the C# property is `Expression` of
// type `Expression` (a property named the same as its type). The faithful port names the
// accessor `Expression()`, but a member function named `Expression` SHADOWS the `Expression`
// base type within this class scope (C++ unqualified name lookup finds the member and stops,
// even though it is not a type -- the D224 `Annotation<T>()`-shadows-the-`Annotation`-type
// crux, the D231 `UnaryOperatorExpression` / D243 `CastExpression` precedent). Every type
// usage AFTER the `Expression()` getter is declared therefore uses the
// ELABORATED-TYPE-SPECIFIER `class Expression` (basic.lookup.elab: an elaborated specifier
// ignores non-type names and finds the hidden class), so the setter parameter, the slot
// static, the `static_cast`, and the backing field all spell the operand type as
// `class Expression`. The ctor parameter and the getter return type precede the getter's
// declaration, so they use the plain `Expression` (no member function is in scope there yet).
//
// The generated ctors: the single `Expression` slot is REQUIRED, so
// `RequiredConstructorPrefixLength == 1 == ctorParams.Count` and `ConstructorPrefixLengths` is
// {1} (the full count only, no shorter prefix, no `params` overload since there is no
// collection), yielding the empty + the `(Expression)` required-prefix ctor. The single-arg
// ctor is `explicit` (a single-argument ctor is a converting ctor by default -- the
// `TypeReferenceExpression` D245 / `InvocationExpression` D248 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYSELECTCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYSELECTCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QuerySelectClause : QueryClause`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it). The single-required-`Expression`-slot shape
// applied to the `QueryClause` hierarchy.
class QuerySelectClause final : public QueryClause {
public:
    ~QuerySelectClause() override = default;

    // The C# `public const string SelectKeyword = "select"` -- the token the output visitor
    // emits for the `select` keyword. Compile-time literal carried as `static constexpr const
    // char*` (a static field, not instance state, so it is not part of
    // `MembersToMatch`/`DoMatch`).
    static constexpr const char* SelectKeyword = "select";

    // The generated empty ctor (the C# `public QuerySelectClause()`). `Expression` defaults
    // to null (no operand). A null operand violates the required-slot invariant, so a
    // default-constructed node is only valid until `Expression` is set (or until
    // `DoMatch`/`CheckInvariant` observe the missing child).
    QuerySelectClause() = default;

    // The generated required-prefix ctor (the C# `public QuerySelectClause(Expression
    // expression)`); the single `Expression` slot is REQUIRED, so this single-arg form is the
    // required-prefix ctor (and the full all-params ctor). The generated body is
    // `this.Expression = expression;` -- it calls the slot setter, which re-parents and
    // re-indexes the child. `explicit` because a single-argument ctor is a converting ctor by
    // default (the `TypeReferenceExpression` D245 / `InvocationExpression` D248 precedent).
    // The `Expression` parameter type precedes the `Expression()` getter, so the plain
    // `Expression` (the base type) is unshadowed here. The body is in complete-class context,
    // so the `Expression(expression)` call resolves to the setter declared below.
    explicit QuerySelectClause(Expression* expression) : QuerySelectClause() {
        Expression(expression);
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

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable);
    // the kind carries identity only. `Slots::Expression` already exists (added for
    // `UnaryOperatorExpression` in D231), so no new `Slots.hpp` constant. The element type
    // uses the elaborated `class Expression` (the `Expression()` accessor shadows the base
    // type in this scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQuerySelectClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQuerySelectClause(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitQuerySelectClause`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitQuerySelectClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so `GetChildCount`
    // is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch
    // (the generator's `WriteReturnDispatchSwitch` shape, with a single case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("QuerySelectClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("QuerySelectClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("QuerySelectClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QuerySelectClause o && this.Expression.DoMatch(o.Expression, match)`.
    // `Expression` is a NON-NULLABLE recursive child, so the generator emits the direct
    // `this.Expression.DoMatch(o.Expression, match)` term (NOT `MatchOptional`). The
    // `SelectKeyword` const string is a static field, not an instance property, so it is not
    // part of `MembersToMatch` and does not appear here. A type-only mismatch (not a
    // `QuerySelectClause`) rejects early.
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
        auto* o = dynamic_cast<QuerySelectClause*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Expression` deep-cloned through the setter when present (which re-parents and
    // re-indexes via `SetChildNode`). No own location fields (`StartLocation`/`EndLocation`
    // are the print-time base fields set by the unported output visitor), so they are not
    // copied (the `UnaryOperatorExpression` D231 no-location-copy precedent). The
    // `static_cast` uses the elaborated `class Expression` (the `Expression()` accessor
    // shadows the base type in this scope). The covariant return is `QuerySelectClause*`
    // (through `QueryClause*`, the `QueryClause::Clone` pure-virtual). The child is skipped if
    // absent (`Clone` tolerates a missing child even though the slot is required -- the
    // invariant is enforced by `CheckInvariant`, not by `Clone`).
    QuerySelectClause* Clone() const override {
        auto* node = new QuerySelectClause();
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
    // until the operand is set.
    class Expression* expression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYSELECTCLAUSE_HPP
