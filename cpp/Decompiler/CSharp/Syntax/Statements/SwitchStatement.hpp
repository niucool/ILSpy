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

// Port of the `SwitchStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/SwitchStatement.cs (the generated
// `SwitchStatement.g.cs` + the hand-written partial, which declares only the `SwitchKeyword`
// const string and the two slot properties, no ctors, no helpers). The next in-order Phase-5
// piece per the D267 plan ("SwitchStatement, TryCatchStatement, LocalFunctionDeclarationStatement,
// VariableDeclarationStatement ... then the remaining TypeMember/GeneralScope hierarchies"):
// `switch_statement ::= 'switch' expression switch_section*` (C# grammar 13.8.3) -- a `switch`
// over a governing `Expression` (the value/condition being switched on) followed by zero or more
// `SwitchSections` (the `case`/`default` groups).
//
// The hand-written partial declares only the `SwitchKeyword` const string and the two slot
// properties. It is a sealed `Statement`. The two slots in source declaration order: a REQUIRED
// (non-nullable) `Expression` `Expression` `[Slot("Expression")]` single slot at flattened index 0
// (the governing expression) and a `SwitchSections` `AstNodeCollection<SwitchSection>` collection
// `[Slot("SwitchSection")]` at flattened index 1 (the switch sections). It is structurally the
// `InvocationExpression` D248 shape (a single required `Expression` child at index 0 + a
// collection at index 1, incremental) but with a `Statement` base (not `Expression`) and the
// collection element `SwitchSection` (not `Expression`). The generator's `supportsIncremental`
// flag is `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 1` and
// the `SwitchSections` collection IS the last slot (`slotIndex == 1 == slots.Count - 1`), so
// `supportsIncremental` is TRUE: an element's flattened `ChildIndex` is exactly `1 + its local
// position`, maintained incrementally by `Add`/`Insert`/`Remove`. The `Expression` single slot
// PRECEDES the collection (no collection precedes it), so the generator's
// `constIndex = !slots.Take(0).Any(IsCollection)` is `true`, and the `Expression` setter uses the
// const-index `SetChildNode(ref field, value, 0)`.
//
// Its generated `DoMatch` has TWO terms in `MembersToMatch` (source declaration) order: a
// non-nullable recursive `Expression` term (dispatched through `MatchRequired` -- the D231
// [class.access.derived] workaround, since a derived node may not call the protected `DoMatch`
// through a base `Expression*`) and a collection recursive `SwitchSections` term
// (`this.SwitchSections.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable non-collection child). There
// is no scalar term (the `SwitchKeyword` const string is a static field, excluded from
// `MembersToMatch`).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitSwitchStatement(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitSwitchStatement`). The generated slot statics
// are `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, required --
// the `Expression` is non-nullable) and `SwitchSectionsSlot` (a
// `CSharpSlotInfoT<SwitchSection>` pointing at `Slots.SwitchSection`, collection). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the `Expression` through the setter (which re-parents) and every
// `SwitchSections` element through `Add` (which re-parents and re-indexes), and copies the
// annotation channel.
//
// C++ name-shadowing crux (the `LockStatement` D261 / `ExpressionStatement` D255 /
// `ReturnStatement` D255 precedent): the C# property is `Expression` of type `Expression` (a
// property named the same as its type -- legal in C#, which keeps property and type names in
// separate spaces). The faithful port names the accessor `Expression()`, but a member function
// named `Expression` SHADOWS the `Expression` class in this class scope (C++ unqualified name
// lookup finds the member and stops, even though it is not a type -- the D224 `Annotation<T>()`
// crux). Every type usage AFTER the `Expression()` getter is declared therefore uses the
// elaborated-type-specifier `class Expression` (basic.lookup.elab: an elaborated specifier ignores
// non-type names and finds the hidden class), so the setter parameter, the slot static, the
// `static_cast`s, and the backing field spell the operand type as `class Expression`. The ctor
// parameter and the getter return type precede the getter's declaration, so they use the plain
// `Expression` (no member function is in scope there yet). The `SwitchSections()` accessor does
// NOT collide with the `SwitchSection` class (a member named `SwitchSections` is not the name
// `SwitchSection` -- the plural), so the `SwitchSection` element type needs no elaboration.
//
// NO new `Slots` constant for `Expression`: `Slots::Expression` is already ported (by
// `UnaryOperatorExpression` D231). The `Slots::SwitchSection` kind is NEW and cycle-broken into
// `SwitchSection.hpp` (the D241/D242/D251/D267 precedent: `SwitchSection.hpp` includes `Slots.hpp`
// for its per-node slot statics, so the kind cannot live in `Slots.hpp` -- a circular include -- and
// is defined in `SwitchSection.hpp` after the `SwitchSection` class).
//
// `SwitchStatement.cs` declares NO hand-written ctors (only the `SwitchKeyword` const and the two
// slot properties), so the port carries only the generated ctors. The generated collection ctors
// (the `(Expression, IEnumerable<SwitchSection>)` all-params ctor and any `params` overload) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so they
// are DEFERRED; the empty + the `(Expression)` required-prefix ctors cover the construction API
// (the `Expression` is the only required ctor param before the `SwitchSections` collection). A
// sections list is built via `SwitchSections().Add(...)` until `AddRange` lands. The
// `(Expression)` ctor is `explicit` (a single-argument ctor is a converting ctor by default),
// matching the generator's public ctor but avoiding an implicit `Expression -> SwitchStatement`
// conversion (the `InvocationExpression` D248 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_SWITCHSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_SWITCHSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class SwitchStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The `InvocationExpression` D248 shape (a single required `Expression`
// child at index 0 + a collection at index 1, incremental) with a `Statement` base and the
// collection element `SwitchSection`.
class SwitchStatement final : public Statement {
public:
    ~SwitchStatement() override = default;

    // The generated empty ctor (the C# `public SwitchStatement()`). The `SwitchSections` collection
    // is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `Expression` single slot at index 0 precedes it) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so an
    // element's flattened `ChildIndex` is exactly `1 + its local position`). `Expression` defaults
    // to null (no governing expression); it is a required slot, so a default-constructed node is
    // only valid until the expression is set (or until `DoMatch`/`CheckInvariant` observe the
    // missing slot).
    SwitchStatement() : switchSections_(this, &SwitchSectionsSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public SwitchStatement(Expression expression)`):
    // the required prefix runs through the last non-optional ctor param (`Expression` is
    // required; `SwitchSections` is an optional collection). Sets `Expression` in declaration
    // order. Delegates to the empty ctor so the collection member is initialized. `explicit` (a
    // single-argument ctor is a converting ctor by default), matching the generator's public ctor
    // but avoiding an implicit `Expression -> SwitchStatement` conversion (the
    // `InvocationExpression` D248 precedent). The parameter type precedes the `Expression()`
    // accessor declaration, so the plain `Expression` (the base type) is unshadowed here; the
    // body is in complete-class context, so the `Expression(expression)` call resolves to the
    // setter declared below.
    explicit SwitchStatement(Expression* expression) : SwitchStatement() {
        Expression(expression);
    }

    // ---- The const keyword token (the output-visitor token literal) ----------------
    // The C# `public const string SwitchKeyword = "switch"`. Part of the node's public API (the
    // output visitor reads it); port as a `static constexpr const char*` (the
    // `CheckedExpression.CheckedKeyword` D234 precedent). The generator excludes const string
    // fields from `MembersToMatch` (it iterates only instance `IPropertySymbol`s), so it never
    // appears in the generated `DoMatch`.
    static constexpr const char* SwitchKeyword = "switch";

    // ---- The `Expression` slot (a single REQUIRED `Expression` child) ----------------
    // The generated `[Slot("Expression")] public partial Expression Expression` -- a single
    // non-nullable `Expression` slot at flattened index 0 (the governing expression being
    // switched on). The const-index `SetChildNode(ref field, value, 0)` setter (no collection
    // precedes it) re-parents and re-indexes in place.
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // ---- The `SwitchSections` collection slot -------------------------------------------
    // The generated `[Slot("SwitchSection")] public partial AstNodeCollection<SwitchSection>
    // SwitchSections` -- the collection of switch sections (the `case`/`default` groups) at
    // flattened index 1, the node's only collection and last slot. The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-Add element-list profile is preserved).
    // `supportsIncremental` is `true` (the node's only collection and last slot), so `Add`
    // maintains each element's flattened `ChildIndex` incrementally. The `SwitchSections()`
    // accessor does NOT collide with the `SwitchSection` class (a member named `SwitchSections`
    // is not the name `SwitchSection` -- the plural), so the `SwitchSection` element type needs no
    // elaborated specifier.
    AstNodeCollectionT<SwitchSection>& SwitchSections() { return switchSections_; }
    const AstNodeCollectionT<SwitchSection>& SwitchSections() const { return switchSections_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
    // required -- the `Expression` is non-nullable, so `IsCollection || IsOptional` is `false`);
    // the `SwitchSectionsSlot` (a `CSharpSlotInfoT<SwitchSection>` pointing at
    // `Slots.SwitchSection`, collection). The `Expression` element type uses the elaborated
    // `class Expression` (the `Expression()` accessor shadows the base type in this scope); the
    // `SwitchSection` element type needs no elaboration (no member is named `SwitchSection`).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};
    static inline const CSharpSlotInfoT<SwitchSection> SwitchSectionsSlot{"SwitchSections", true, &Slots::SwitchSection, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSwitchStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSwitchStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // An `Expression` single slot at index 0 and a `SwitchSections` collection occupying the
    // contiguous range `[1, 1 + Count)`. `GetChildCount` is `1 + Count` (the single slot plus the
    // collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single case
    // then a collection step). `GetCollectionByKind` returns the `SwitchSections` collection for
    // the `SwitchSection` kind (the node's only collection). This is the `InvocationExpression`
    // D248 dispatch shape.

    int GetChildCount() const override { return 1 + switchSections_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return expression_;
        i--;
        int n = switchSections_.Count();
        if (i < n)
            return switchSections_.At(i);
        throw std::out_of_range("SwitchStatement::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(expression_, static_cast<class Expression*>(value), index);
            return;
        }
        i--;
        int n = switchSections_.Count();
        if (i < n) {
            switchSections_.SetAt(i, static_cast<SwitchSection*>(value));
            return;
        }
        throw std::out_of_range("SwitchStatement::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &ExpressionSlot;
        i--;
        int n = switchSections_.Count();
        if (i < n)
            return &SwitchSectionsSlot;
        throw std::out_of_range("SwitchStatement::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::SwitchSection)
            return &switchSections_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SwitchStatement o && this.Expression.DoMatch(o.Expression, match) &&
    // this.SwitchSections.DoMatch(o.SwitchSections, match)`. The terms are in `MembersToMatch`
    // order, which is the source declaration order (`Expression`, `SwitchSections`). The
    // `Expression` term is a non-nullable recursive child, so the generator emits a DIRECT
    // `this.Expression.DoMatch(o.Expression, match)` -- ported through `MatchRequired` (the D231
    // [class.access.derived] workaround); the `SwitchSections` term is the collection recursive
    // match (the generator emits the collection-typed recursive term directly, NOT `MatchOptional`,
    // which it emits only for a nullable non-collection child). A type-only mismatch (not a
    // `SwitchStatement`) rejects early. The `Expression` `MatchRequired` is the first term, so a
    // half-constructed pattern (a null `Expression`) rejects without crashing (the `MatchRequired`
    // null-pattern guard).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<SwitchStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && switchSections_.DoMatch(o->switchSections_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `Expression` deep-cloned through the setter (which re-parents; `Expression::Clone()` returns
    // `Expression*`, the covariant override, which `Expression(class Expression*)` accepts
    // directly), and every `SwitchSections` element deep-cloned through `Add` (which re-parents
    // and re-indexes; `SwitchSection::Clone()` returns `SwitchSection*`, which
    // `Add(SwitchSection*)` accepts directly). No own location fields (`SwitchStatement` does not
    // derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `InvocationExpression` D248 no-location-copy precedent). The covariant return is
    // `SwitchStatement*` (through `Statement*`, the `Statement::Clone` pure-virtual). The
    // `static_cast` uses the elaborated `class Expression` (the `Expression()` accessor shadows
    // the base type in this scope).
    SwitchStatement* Clone() const override {
        auto* node = new SwitchStatement();
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        for (int i = 0; i < switchSections_.Count(); i++)
            node->switchSections_.Add(switchSections_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `expression_` is null until the governing expression is set (a required
    // slot -- `CheckInvariant` asserts it is filled); `switchSections_` is the always-present
    // collection member (empty until the first `Add`, incremental). The `expression_` field type
    // uses the elaborated `class Expression` (the `Expression()` accessor shadows the base type in
    // this scope); `switchSections_` uses the plain `SwitchSection` (no shadowing).
    class Expression* expression_ = nullptr;
    AstNodeCollectionT<SwitchSection> switchSections_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_SWITCHSTATEMENT_HPP
