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

// Port of the `QueryWhereClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryWhereClause.g.cs` + the hand-written partial, which declares the `WhereKeyword` const
// string and the `Condition` slot property -- no ctors, no helpers). The next in-order
// Phase-5 piece per the D309 plan. `where_clause ::= 'where' expression` (C# grammar 12.23.1):
// an element of a `QueryExpression.Clauses` collection (the `where expr` filter clause), a
// sealed `QueryClause` with a single required `Expression` child.
//
// It is the `UnaryOperatorExpression` D231 single-required-`Expression`-slot shape applied to
// the `QueryClause` hierarchy: a single, REQUIRED (non-nullable) `[Slot("Condition")]
// Expression Condition` child at flattened index 0. The slot kind is `Condition`, so the
// per-node `ConditionSlot` REUSES the already-ported `Slots::Condition` kind (the same kind
// `ConditionalExpression` registered in D232 and `IfElseStatement`/`WhileStatement`/... reused)
// -- no new `Slots.hpp` constant. The generator emits the const-index
// `SetChildNode(ref field, value, 0)` setter (the single slot is the first and only slot), the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single slot,
// and the `DoMatch` `return other is QueryWhereClause o && this.Condition.DoMatch(o.Condition,
// match)`.
//
// It also carries one keyword const string (`WhereKeyword`), the token the output visitor
// emits for the `where` keyword (the `CheckedKeyword`/`UncheckedKeyword` D234 precedent: a
// const string is a value, part of the node's public API surface, so it ports now as a
// `static constexpr const char*`).
//
// NO C++ name-shadowing crux: the `Condition()` accessor is a member function, but no class
// named `Condition` lives in the `Syntax` namespace, and no member is named `Expression`
// (the `Condition` accessor does not collide with the `Expression` base type -- it is
// `Condition()`, not `Expression()`), so no elaborated-type-specifier (`class Expression`) is
// needed anywhere (the `MemberReferenceExpression.Target` D247 / `QueryWhereClause.Condition`
// differently-named-property precedent).
//
// The generated ctors: the single `Condition` slot is REQUIRED, so
// `RequiredConstructorPrefixLength == 1 == ctorParams.Count` and `ConstructorPrefixLengths` is
// {1} (the full count only, no shorter prefix, no `params` overload since there is no
// collection), yielding the empty + the `(Expression)` required-prefix ctor. The single-arg
// ctor is `explicit` (a single-argument ctor is a converting ctor by default -- the
// `TypeReferenceExpression` D245 / `InvocationExpression` D248 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYWHERECLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYWHERECLAUSE_HPP

#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryWhereClause : QueryClause`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it). The single-required-`Expression`-slot shape
// applied to the `QueryClause` hierarchy.
class QueryWhereClause final : public QueryClause {
public:
    ~QueryWhereClause() override = default;

    // The C# `public const string WhereKeyword = "where"` -- the token the output visitor
    // emits for the `where` keyword. Compile-time literal carried as `static constexpr const
    // char*` (a static field, not instance state, so it is not part of
    // `MembersToMatch`/`DoMatch`).
    static constexpr const char* WhereKeyword = "where";

    // The generated empty ctor (the C# `public QueryWhereClause()`). `Condition` defaults to
    // null (no operand). A null operand violates the required-slot invariant, so a
    // default-constructed node is only valid until `Condition` is set (or until
    // `DoMatch`/`CheckInvariant` observe the missing child).
    QueryWhereClause() = default;

    // The generated required-prefix ctor (the C# `public QueryWhereClause(Expression
    // condition)`); the single `Condition` slot is REQUIRED, so this single-arg form is the
    // required-prefix ctor (and the full all-params ctor -- no other params). The generated
    // body is `this.Condition = condition;` -- it calls the slot setter, which re-parents and
    // re-indexes the child. `explicit` because a single-argument ctor is a converting ctor by
    // default (the `TypeReferenceExpression` D245 / `InvocationExpression` D248 precedent).
    explicit QueryWhereClause(Expression* condition) : QueryWhereClause() {
        Condition(condition);
    }

    // The C# `[Slot("Condition")] Expression Condition` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The const-index `SetChildNode(ref field,
    // value, 0)` setter (the single slot is the first and only slot, no collection precedes
    // it). NO name shadowing (the `Condition` accessor does NOT collide with the `Expression`
    // base type -- no member is named `Expression`), so the element type is the plain
    // `Expression`.
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable);
    // the kind carries identity only. `Slots::Condition` already exists (added for
    // `ConditionalExpression` in D232), so no new `Slots.hpp` constant. No name shadowing (no
    // member is named `Expression`), so the element type is the plain `Expression`.
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryWhereClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryWhereClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Condition`); no collection, so `GetChildCount`
    // is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch
    // (the generator's `WriteReturnDispatchSwitch` shape, with a single case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return condition_;
            default: throw std::out_of_range("QueryWhereClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(condition_, static_cast<Expression*>(value), 0); break;
            default: throw std::out_of_range("QueryWhereClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ConditionSlot;
            default: throw std::out_of_range("QueryWhereClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryWhereClause o && this.Condition.DoMatch(o.Condition, match)`.
    // `Condition` is a NON-NULLABLE recursive child, so the generator emits the direct
    // `this.Condition.DoMatch(o.Condition, match)` term (NOT `MatchOptional`). The
    // `WhereKeyword` const string is a static field, not an instance property, so it is not
    // part of `MembersToMatch` and does not appear here. A type-only mismatch (not a
    // `QueryWhereClause`) rejects early.
    //
    // The C# direct dispatch (`this.Condition.DoMatch`) assumes the required child is
    // present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing
    // operand defensively (a null pattern child does not match; the C# would null-deref),
    // and a null candidate child flows through the operand's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (the operand always set) the behavior is
    // identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryWhereClause*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(condition_, o->condition_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Condition` deep-cloned through the setter when present (which re-parents and
    // re-indexes via `SetChildNode`). No own location fields (`StartLocation`/`EndLocation`
    // are the print-time base fields set by the unported output visitor), so they are not
    // copied (the `UnaryOperatorExpression` D231 no-location-copy precedent). The covariant
    // return is `QueryWhereClause*` (through `QueryClause*`, the `QueryClause::Clone`
    // pure-virtual). The child is skipped if absent (`Clone` tolerates a missing child even
    // though the slot is required -- the invariant is enforced by `CheckInvariant`, not by
    // `Clone`).
    QueryWhereClause* Clone() const override {
        auto* node = new QueryWhereClause();
        node->CloneAnnotationsFrom(*this);
        if (condition_ != nullptr)
            node->Condition(static_cast<Expression*>(condition_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. No name shadowing (no member is named `Expression`), so the field
    // type is the plain `Expression`. A required slot is non-null only by invariant, so the
    // pointer is null until the operand is set.
    Expression* condition_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYWHERECLAUSE_HPP
