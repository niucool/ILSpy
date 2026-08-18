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

// Port of the `QueryContinuationClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryContinuationClause.g.cs` + the hand-written partial, which declares only the
// `PrecedingQuery` and `Identifier` slot properties plus the `IntoKeyword` const -- no ctors,
// no helpers). The next in-order Phase-5 piece per the D310 plan ("the remaining
// string-name-[Slot] and multi-slot clauses"). `query_continuation ::= query_expression
// 'into' identifier` (C# grammar 12.23.1): a `QueryClause` that is always the first clause of
// the `QueryExpression` containing it, binding the result of a preceding `QueryExpression` to a
// range variable.
//
// It is the `FixedVariableInitializer` D286 / `NamedExpression` D297 two-required-single-slot
// shape (a REQUIRED `QueryExpression` `PrecedingQuery` at flattened index 0 plus a REQUIRED
// non-nullable string `Identifier` over a backing `IdentifierToken` at index 1) applied to the
// `QueryClause` hierarchy. The `PrecedingQuery` is a concrete `QueryExpression` (not an
// abstract base), so its `Clone()` returns `QueryExpression*` which the
// `PrecedingQuery(QueryExpression*)` setter accepts directly (no `static_cast`, no downcast).
// The `Identifier` is a NON-nullable `string` (the C# source declares
// `public partial string Identifier`, not `string?`), so the backing token is a REQUIRED
// (non-nullable) slot, the string getter DEREFS the token (returning `std::string`, not
// `std::optional<std::string>` -- a null token is a half-constructed node that would
// `NullReferenceException` in C#), and the string setter uses `Identifier::Create` (NOT
// `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
// empty name yields a token with an empty `Name`, not a null token -- the
// `MemberType.MemberName` D238 / `LabelStatement` D259 / `IdentifierExpression` D246
// precedent).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitQueryContinuationClause(this)`. The generated slot statics are
// `PrecedingQuerySlot` (a `CSharpSlotInfo<QueryExpression>` pointing at `Slots.PrecedingQuery`,
// required) and `IdentifierTokenSlot` (a `CSharpSlotInfo<Identifier>` pointing at
// `Slots.Identifier`, required). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the
// `PrecedingQuery` and the `IdentifierToken` through the setters (which re-parent), and copies
// the annotation channel.
//
// ONE new `Slots` kind -- `Slots::PrecedingQuery` (a `CSharpSlotInfoT<QueryExpression>`) -- is
// CYCLE-BROKEN into `QueryExpression.hpp` after the `QueryExpression` class (not into
// `Slots.hpp`): `QueryExpression.hpp` includes `Slots.hpp` for its own per-node `ClausesSlot`
// (referencing `Slots::Clause`), so a `QueryExpression`-typed kind cannot live in `Slots.hpp`
// (including `QueryExpression.hpp` from `Slots.hpp` would form a circular include -- the
// `Slots::Attribute` D241 / `Slots::AttributeSection` D242 / `Slots::Initializer` D251 /
// `Slots::Variable` D267 cycle-breaking precedent applied to a `QueryExpression`-typed single
// kind). `Slots::Identifier` (by `SimpleType` D237) already exists, so no new `Slots.hpp`
// constant for the `Identifier` slot.
//
// C++ name-shadowing crux (the D237 `SimpleType` / D246 `IdentifierExpression` precedent): the
// string accessor `Identifier()` (a member function) shadows the `Identifier` CLASS in this
// class scope (C++ unqualified name lookup finds the member and stops, even though it is not a
// type), so the token type is the elaborated-type-specifier `class Identifier`
// (`basic.lookup.elab` ignores non-type names) in every type position AFTER the accessor (the
// backing field, the slot static's element type, the `SetChild` `static_cast`, the token
// accessor's signature, and the ctor parameter), and the `Identifier::Create` factory call in
// the setter is fully-qualified (`::ILSpy::Decompiler::CSharp::Syntax::Identifier::...`). The
// `PrecedingQuery()` accessor does NOT shadow (no class named `PrecedingQuery` in `Syntax`),
// so the `QueryExpression` element type is the plain `QueryExpression` throughout (the
// `MemberType.MemberName` D238 / `Accessor.Body` D274 differently-named-property precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYCONTINUATIONCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYCONTINUATIONCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryContinuationClause : QueryClause`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it). The two-required-single-slot shape (a
// `QueryExpression` + a non-nullable string `Identifier`) applied to the `QueryClause`
// hierarchy.
class QueryContinuationClause final : public QueryClause {
public:
    ~QueryContinuationClause() override = default;

    // The C# `public const string IntoKeyword = "into"` -- the token the output visitor emits
    // for the `into` keyword. Compile-time literal carried as `static constexpr const char*`
    // (a static field, not instance state, so it is not part of `MembersToMatch`/`DoMatch`).
    static constexpr const char* IntoKeyword = "into";

    // The generated empty ctor (the C# `public QueryContinuationClause()`). Both slots default
    // to null (no preceding query, no name). Null slots violate the required-slot invariants, so
    // a default-constructed node is only valid until `PrecedingQuery`/`Identifier` are set (or
    // until `DoMatch`/`CheckInvariant` observe the missing children).
    QueryContinuationClause() = default;

    // The generated all-params ctor (the C# `public QueryContinuationClause(QueryExpression
    // precedingQuery, string identifier)`); both slots are REQUIRED, so
    // `RequiredConstructorPrefixLength` equals `ctorParams.Count` and this is the only
    // parametrized ctor (no shorter prefix, no collection so no `params` overload). The
    // generated body sets the slots via the property setters; the string setter creates the
    // token via `Identifier::Create`. `precedingQuery` is the plain `QueryExpression` (no
    // shadowing -- the `PrecedingQuery()` accessor name does not collide with the
    // `QueryExpression` class), and `identifier` is a `string` (the ctor param type precedes
    // the `Identifier()` accessor, so the plain type resolves to `std::string`).
    QueryContinuationClause(QueryExpression* precedingQuery, std::string identifier)
        : QueryContinuationClause() {
        PrecedingQuery(precedingQuery);
        Identifier(std::move(identifier));
    }

    // ---- The `PrecedingQuery` slot -------------------------------------
    // The generated `[Slot("PrecedingQuery")] QueryExpression PrecedingQuery` -- a single,
    // REQUIRED (non-nullable) `QueryExpression` child at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (the slot is the first and no collection
    // precedes it) re-parents and re-indexes in place. NO name shadowing (the
    // `PrecedingQuery()` accessor does not collide with the `QueryExpression` class -- no
    // member is named `QueryExpression`), so the element type is the plain `QueryExpression`.
    QueryExpression* PrecedingQuery() const { return precedingQuery_; }
    void PrecedingQuery(QueryExpression* value) {
        SetChildNode(precedingQuery_, value, 0);
    }

    // ---- The `IdentifierToken` slot (the backing token of the string name) ------------
    // The generated `[Slot("Identifier")] string Identifier`'s backing `IdentifierToken` child
    // slot -- a REQUIRED (non-nullable) `Identifier` at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it) re-parents and
    // re-indexes in place. `class Identifier` (the `Identifier()` accessor shadows the class).
    class Identifier* IdentifierToken() const { return identifierToken_; }
    void IdentifierToken(class Identifier* value) {
        SetChildNode(identifierToken_, value, 1);
    }

    // The `Identifier` string-name accessor (over the token). A NON-optional name (the C#
    // `string`, not `string?`): `get` returns `IdentifierToken.Name` (derefs the token -- a
    // null token is a half-constructed node that would NRE in C#), `set` creates the token via
    // `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null
    // token). `Identifier()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is fully-qualified (the `Identifier()` accessor
    // shadows the `Identifier` class in its own setter, the generator's `global::` case).
    std::string Identifier() const { return identifierToken_->Name(); }
    void Identifier(std::string_view value) {
        IdentifierToken(::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create(std::string(value)));
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // `PrecedingQuerySlot` (`CSharpSlotInfo<QueryExpression>` pointing at the cycle-broken
    // `Slots.PrecedingQuery`, required); `class Identifier` (the `Identifier()` accessor
    // shadows the class). `Slots::PrecedingQuery` is added to `QueryExpression.hpp` this
    // iteration (the cycle-broken kind); `Slots::Identifier` (by `SimpleType` D237) already
    // exists.
    static inline const CSharpSlotInfoT<QueryExpression> PrecedingQuerySlot{"PrecedingQuery", false, &Slots::PrecedingQuery, false};
    static inline const CSharpSlotInfoT<class Identifier> IdentifierTokenSlot{"IdentifierToken", false, &Slots::Identifier, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryContinuationClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryContinuationClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1; no collection, so `GetChildCount` is the
    // constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return precedingQuery_;
            case 1: return identifierToken_;
            default: throw std::out_of_range("QueryContinuationClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(precedingQuery_, static_cast<QueryExpression*>(value), 0); break;
            case 1: SetChildNode(identifierToken_, static_cast<class Identifier*>(value), 1); break;
            default: throw std::out_of_range("QueryContinuationClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &PrecedingQuerySlot;
            case 1: return &IdentifierTokenSlot;
            default: throw std::out_of_range("QueryContinuationClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryContinuationClause o
    //      && this.PrecedingQuery.DoMatch(o.PrecedingQuery, match)
    //      && MatchString(this.Identifier, o.Identifier)` (source declaration order:
    // `PrecedingQuery` before `Identifier`). The `PrecedingQuery` term is a NON-NULLABLE
    // recursive child (the direct dispatch); the `Identifier` term is a `String` `MatchString`
    // (the `$any$` wildcard in the pattern's `Identifier` matches any candidate name). The
    // `IntoKeyword` const string is a static field, not an instance property, so it is not part
    // of `MembersToMatch` and does not appear here. A type-only mismatch (not a
    // `QueryContinuationClause`) rejects early.
    //
    // The C# direct dispatch (`this.PrecedingQuery.DoMatch`) assumes the required child is
    // present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `QueryExpression*`. `MatchRequired` guards a missing
    // operand defensively.
    //
    // `Identifier()` returns `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per argument. The `Identifier()`
    // calls are INLINED in the `MatchString` arguments (not pre-computed in locals) so the C#
    // `&&` short-circuits: `o->Identifier()` derefs the candidate's token only when the
    // `PrecedingQuery` term passed, avoiding eager UB on a half-constructed candidate; the
    // `std::string` temporaries live until the end of the full `return` expression so the
    // `std::string_view` views stay valid for the `MatchString` call (the D227 / D238
    // inlined-MatchString-arguments precedent).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryContinuationClause*>(other);
        if (o == nullptr)
            return false;
        if (!MatchRequired(precedingQuery_, o->precedingQuery_, match))
            return false;
        return PatternMatching::Pattern::MatchString(
            std::string_view(this->Identifier()),
            std::string_view(o->Identifier()));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `PrecedingQuery` deep-cloned through the setter when present (which re-parents and
    // re-indexes via `SetChildNode`), and the `IdentifierToken` deep-cloned through the setter
    // when present. No own location fields, so they are not copied (the `FixedVariableInitializer`
    // D286 / `NamedExpression` D297 no-location-copy precedent). The covariant return is
    // `QueryContinuationClause*` (through `QueryClause*`, the `QueryClause::Clone`
    // pure-virtual). Each child is skipped if absent (`Clone` tolerates a missing child even
    // though the slot is required -- the invariant is enforced by `CheckInvariant`, not by
    // `Clone`). `class Identifier` (the `Identifier()` accessor shadows the class); the token
    // `Clone()` returns `Identifier*`, which the typed setter accepts directly. The
    // `QueryExpression` `Clone()` returns `QueryExpression*` (covariant through `Expression*`),
    // which the `PrecedingQuery(QueryExpression*)` setter accepts directly (no `static_cast`).
    QueryContinuationClause* Clone() const override {
        auto* node = new QueryContinuationClause();
        node->CloneAnnotationsFrom(*this);
        if (precedingQuery_ != nullptr)
            node->PrecedingQuery(precedingQuery_->Clone());
        if (identifierToken_ != nullptr)
            node->IdentifierToken(identifierToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `QueryExpression` is unshadowed (no class named
    // `PrecedingQuery`); `class Identifier` (the `Identifier()` accessor shadows the class).
    // Required slots are non-null only by invariant, so the pointers are null until the slots
    // are set.
    QueryExpression* precedingQuery_ = nullptr;
    class Identifier* identifierToken_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYCONTINUATIONCLAUSE_HPP
