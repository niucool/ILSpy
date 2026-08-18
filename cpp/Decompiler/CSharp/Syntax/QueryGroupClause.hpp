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

// Port of the `QueryGroupClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryGroupClause.g.cs` + the hand-written partial, which declares only the `Projection` and
// `Key` slot properties plus the `GroupKeyword`/`ByKeyword` consts -- no ctors, no helpers).
// The next in-order Phase-5 piece per the D310 plan ("the remaining string-name-[Slot] and
// multi-slot clauses"). `group_clause ::= 'group' expression 'by' expression` (C# grammar
// 12.23.1): a `QueryClause` grouping the `Projection` by the `Key`.
//
// It is the `ConditionalExpression` D232 / `CastExpression` D243 two-required-single-slot shape
// (two REQUIRED `Expression` children) applied to the `QueryClause` hierarchy: a `Projection`
// `Expression` at flattened index 0 and a `Key` `Expression` at flattened index 1, both
// non-nullable, with the const-index `SetChildNode` setters (no collection precedes either),
// the flat two-case slot-storage switch, and the two-term `MatchRequired` `DoMatch` in source
// declaration order. The `Projection`/`Key` slot KIND NAMES are distinct from the already-ported
// `Slots::Expression`/`Slots::Condition`/etc., so TWO new `Slots` constants are added to
// `Slots.hpp` (`Slots::Projection` and `Slots::Key`, both `CSharpSlotInfoT<Expression>` with the
// element type qualified since they are defined after the `Slots::Expression` variable -- the
// `Expression`/`Condition`/`AdditionalArraySpecifier` collision precedent).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitQueryGroupClause(this)`. The generated slot statics are `ProjectionSlot` (a
// `CSharpSlotInfo<Expression>` pointing at `Slots.Projection`, required) and `KeySlot` (a
// `CSharpSlotInfo<Expression>` pointing at `Slots.Key`, required). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones both `Expression` children through the setters (which re-parent), and copies the
// annotation channel.
//
// NO C++ name-shadowing crux: the `Projection()` and `Key()` accessors are member functions, but
// no class named `Projection`/`Key` lives in the `Syntax` namespace (and no member is named
// `Expression`), so no elaborated-type-specifier is needed anywhere -- the plain `Expression`
// resolves to the base class in every type position (the `BinaryOperatorExpression` D229 /
// `ConditionalExpression` D232 differently-named-property precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYGROUPCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYGROUPCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryGroupClause : QueryClause`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it). The two-required-`Expression`-slot shape applied
// to the `QueryClause` hierarchy.
class QueryGroupClause final : public QueryClause {
public:
    ~QueryGroupClause() override = default;

    // The C# `public const string GroupKeyword = "group"` and `ByKeyword = "by"` -- the tokens
    // the output visitor emits for the `group`/`by` keywords. Compile-time literals carried as
    // `static constexpr const char*` (static fields, not instance state, so they are not part of
    // `MembersToMatch`/`DoMatch`).
    static constexpr const char* GroupKeyword = "group";
    static constexpr const char* ByKeyword = "by";

    // The generated empty ctor (the C# `public QueryGroupClause()`). Both slots default to null
    // (no projection, no key). Null slots violate the required-slot invariants, so a
    // default-constructed node is only valid until `Projection`/`Key` are set (or until
    // `DoMatch`/`CheckInvariant` observe the missing children).
    QueryGroupClause() = default;

    // The generated all-params ctor (the C# `public QueryGroupClause(Expression projection,
    // Expression key)`); both slots are REQUIRED, so `RequiredConstructorPrefixLength` equals
    // `ctorParams.Count` and this is the only parametrized ctor (no shorter prefix, no
    // collection so no `params` overload). The generated body sets the slots via the property
    // setters.
    QueryGroupClause(Expression* projection, Expression* key) : QueryGroupClause() {
        Projection(projection);
        Key(key);
    }

    // ---- The `Projection` slot ---------------------------------------
    // The generated `[Slot("Projection")] Expression Projection` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (the slot is the first and no collection
    // precedes it) re-parents and re-indexes in place. NO name shadowing (no class named
    // `Projection` in `Syntax`, no member named `Expression`), so the element type is the plain
    // `Expression`.
    Expression* Projection() const { return projection_; }
    void Projection(Expression* value) {
        SetChildNode(projection_, value, 0);
    }

    // ---- The `Key` slot -----------------------------------------------
    // The generated `[Slot("Key")] Expression Key` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 1. The const-index `SetChildNode(ref field,
    // value, 1)` setter (no collection precedes it). NO name shadowing (no class named `Key`
    // in `Syntax`, no member named `Expression`), so the element type is the plain
    // `Expression`.
    Expression* Key() const { return key_; }
    void Key(Expression* value) {
        SetChildNode(key_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------
    // `ProjectionSlot` (a `CSharpSlotInfo<Expression>` pointing at `Slots.Projection`,
    // required) and `KeySlot` (a `CSharpSlotInfo<Expression>` pointing at `Slots.Key`,
    // required). NO name shadowing (no member is named `Expression`), so the element type is the
    // plain `Expression`. `Slots::Projection` and `Slots::Key` are added to `Slots.hpp` this
    // iteration (the two new kind names for the group-clause's two `Expression` positions).
    static inline const CSharpSlotInfoT<Expression> ProjectionSlot{"Projection", false, &Slots::Projection, false};
    static inline const CSharpSlotInfoT<Expression> KeySlot{"Key", false, &Slots::Key, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryGroupClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryGroupClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1; no collection, so `GetChildCount` is the
    // constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return projection_;
            case 1: return key_;
            default: throw std::out_of_range("QueryGroupClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(projection_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(key_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("QueryGroupClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ProjectionSlot;
            case 1: return &KeySlot;
            default: throw std::out_of_range("QueryGroupClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryGroupClause o && this.Projection.DoMatch(o.Projection, match) &&
    // this.Key.DoMatch(o.Key, match)` (source declaration order: `Projection` before `Key`).
    // Both terms are NON-NULLABLE recursive children, so the generator emits the direct
    // `this.X.DoMatch(o.X, match)` terms (NOT `MatchOptional`). The `GroupKeyword`/`ByKeyword`
    // const strings are static fields, not instance properties, so they are not part of
    // `MembersToMatch` and do not appear here. A type-only mismatch (not a `QueryGroupClause`)
    // rejects early.
    //
    // The C# direct dispatch (`this.Projection.DoMatch`) assumes the required child is present;
    // the port routes it through `AstNode::MatchRequired` (the same-class static helper) because
    // C++ `[class.access.derived]` forbids a derived node from calling the protected `DoMatch`
    // through a base `Expression*`. `MatchRequired` guards a missing operand defensively.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryGroupClause*>(other);
        if (o == nullptr)
            return false;
        if (!MatchRequired(projection_, o->projection_, match))
            return false;
        return MatchRequired(key_, o->key_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and both
    // `Expression` children deep-cloned through the setters when present (which re-parent and
    // re-index via `SetChildNode`). No own location fields, so they are not copied (the
    // `ConditionalExpression` D232 / `BinaryOperatorExpression` D229 no-location-copy
    // precedent). The covariant return is `QueryGroupClause*` (through `QueryClause*`, the
    // `QueryClause::Clone` pure-virtual). Each child is skipped if absent (`Clone` tolerates a
    // missing child even though the slot is required -- the invariant is enforced by
    // `CheckInvariant`, not by `Clone`).
    QueryGroupClause* Clone() const override {
        auto* node = new QueryGroupClause();
        node->CloneAnnotationsFrom(*this);
        if (projection_ != nullptr)
            node->Projection(static_cast<Expression*>(projection_->Clone()));
        if (key_ != nullptr)
            node->Key(static_cast<Expression*>(key_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. No name shadowing (no member is named `Expression`), so the field
    // type is the plain `Expression`. Required slots are non-null only by invariant, so the
    // pointers are null until the slots are set.
    Expression* projection_ = nullptr;
    Expression* key_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYGROUPCLAUSE_HPP
