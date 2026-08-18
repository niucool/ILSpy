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

// Port of the `SwitchExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/SwitchExpression.cs (the generated
// `SwitchExpression.g.cs` + the hand-written partial, which declares only the `SwitchKeyword`
// const and the two slot properties, no ctors, no helpers). The next in-order Phase-5 piece per
// the D306 plan (a remaining Expression node whose dependencies are all ported -- the
// `SwitchExpressionSection` just ported is the collection element type):
// `switch_expression ::= expression 'switch' '{' switch_expression_arm* '}'` (C# grammar 12.12)
// -- a `switch` expression is the governing `Expression` (the value being switched on) followed
// by zero or more `SwitchSections` (the `switch_expression_arm` cases, each a
// `SwitchExpressionSection`).
//
// It is structurally the `InvocationExpression` (D248) shape (a single REQUIRED `Expression`
// child at flattened index 0 + a collection at baseIndex 1, the node's only collection and last
// slot so `supportsIncremental` is true) but with the collection element type a concrete node
// (`SwitchExpressionSection`, with per-node slot statics) rather than `Expression`, and with the
// `Expression()` accessor that SHADOWS the `Expression` base type (the `CastExpression` D243
// name-shadowing crux: the property is literally named `Expression` of type `Expression`, so the
// accessor shadows the base class in C++ class scope, forcing the elaborated `class Expression`
// specifier in every type position after the getter -- setter param, slot static element type,
// `SetChild`/`Clone` `static_cast`, and the backing field; the ctor param and getter return type
// precede the getter so they use the plain `Expression`).
//
// Its generated `DoMatch` has two terms in source declaration order: a non-nullable recursive
// `Expression` term (dispatched through `MatchRequired` -- the D231 [class.access.derived]
// workaround, since a derived node may not call the protected `DoMatch` through a base
// `Expression*`) and a collection recursive `SwitchSections` term
// (`this.SwitchSections.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection child).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitSwitchExpression(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitSwitchExpression`). The generated slot
// statics are `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
// required -- the `Expression` `Expression` is non-nullable) and `SwitchSectionsSlot` (a
// `CSharpSlotInfoT<SwitchExpressionSection>` pointing at `Slots.SwitchExpressionSection`,
// collection). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Expression`
// deep-cloned through the setter (which re-parents; `Expression::Clone()` returns `Expression*`,
// the covariant override, which `Expression(Expression*)` accepts directly), and every
// `SwitchSections` element deep-cloned through `Add` (which re-parents and re-indexes;
// `SwitchExpressionSection::Clone()` returns `SwitchExpressionSection*`, which
// `Add(SwitchExpressionSection*)` accepts directly).
//
// NO new `Slots` constant: both slot kinds are already ported (`Slots::Expression` by
// `UnaryOperatorExpression` D231, `Slots::SwitchExpressionSection` by `SwitchExpressionSection`),
// so `Slots.hpp` is unchanged. `SwitchExpression.cs` declares NO hand-written ctors (only the
// `SwitchKeyword` const and the two slot properties), so the port carries only the generated ctors
// (the empty ctor + the `(Expression)` required-prefix ctor). The collection ctors that take
// arguments (the `SwitchExpression(Expression, IEnumerable<SwitchExpressionSection>)` and the
// `params SwitchExpressionSection[]` form) are DEFERRED: they use `AddRange`, which lands with the
// collection convenience mutators (the D222 deferral); a sections list is built via
// `SwitchSections().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_SWITCHEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_SWITCHEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/SwitchExpressionSection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class SwitchExpression : Expression`. `final` (the C# `sealed`):
// no further derivation. The `InvocationExpression` (D248) single-`Expression`-slot-plus-a-collection
// shape with the collection element type a concrete node (`SwitchExpressionSection`) and the
// `Expression()` accessor shadowing the base type.
class SwitchExpression final : public Expression {
public:
    ~SwitchExpression() override = default;

    // The generated empty ctor (the C# `public SwitchExpression()`). The `SwitchSections`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `Expression` single slot at index 0 precedes it) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so an
    // element's flattened `ChildIndex` is exactly `1 + its local position`). The `Expression`
    // defaults to null (no governing expression) via its default member initializer; it is a
    // required slot, so a default-constructed node is only valid until the expression is set (or
    // until `DoMatch`/`CheckInvariant` observe the missing slot).
    SwitchExpression() : switchSections_(this, &SwitchSectionsSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public SwitchExpression(Expression
    // expression)`): the required prefix runs through the last non-optional ctor param
    // (`Expression` is required; `SwitchSections` is an optional collection). Sets `Expression` in
    // declaration order. Delegates to the empty ctor so the collection member is initialized.
    // `explicit` (a single-argument ctor is a converting ctor by default), matching the
    // generator's public ctor but avoiding an implicit `Expression -> SwitchExpression` conversion.
    explicit SwitchExpression(Expression* expression) : SwitchExpression() {
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
    // non-nullable `Expression` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. The `Expression()` accessor SHADOWS the `Expression` base type in this
    // class scope (the `CastExpression` D243 name-shadowing crux), so the getter return type (which
    // precedes the getter's declaration) uses the plain `Expression` (the base type is unshadowed
    // there), but every type position AFTER the getter uses the elaborated specifier
    // `class Expression` (the setter parameter, the slot static element type, the `SetChild`
    // `static_cast`, the `Clone` deep-clone, and the backing field).
    Expression* Expression() const { return expression_; }
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // ---- The `SwitchSections` collection slot ---------------------------------------
    // The generated `[Slot("SwitchExpressionSection")] public partial
    // AstNodeCollection<SwitchExpressionSection> SwitchSections` -- the collection of switch
    // expression arms (a `CSharpSlotInfoT<SwitchExpressionSection>` slot at flattened index 1, the
    // node's only collection and last slot). The C# lazily allocates the wrapper; the D222 port
    // makes the collection an always-present stack member, so the accessor returns the member
    // directly (the empty-until-first-Add element-list profile is preserved -- `list_` is empty
    // until the first `Add`). `supportsIncremental` is `true` (the only collection and last slot),
    // so `Add` maintains an element's `ChildIndex` incrementally. No name shadowing (the
    // `SwitchSections()` accessor does not collide with the `SwitchExpressionSection` class -- no
    // member is named `SwitchExpressionSection`).
    AstNodeCollectionT<SwitchExpressionSection>& SwitchSections() { return switchSections_; }
    const AstNodeCollectionT<SwitchExpressionSection>& SwitchSections() const { return switchSections_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) --------------
    // The `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
    // required -- the `Expression` `Expression` is non-nullable); the `SwitchSectionsSlot` (a
    // `CSharpSlotInfoT<SwitchExpressionSection>` pointing at `Slots.SwitchExpressionSection`,
    // collection). The `ExpressionSlot` element type uses the elaborated `class Expression` (the
    // `Expression()` accessor shadows the base type in this scope -- the `CastExpression` D243
    // precedent); the `SwitchSectionsSlot` element type is the plain `SwitchExpressionSection` (no
    // shadowing -- no member is named `SwitchExpressionSection`).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};
    static inline const CSharpSlotInfoT<SwitchExpressionSection> SwitchSectionsSlot{"SwitchSections", true, &Slots::SwitchExpressionSection, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSwitchExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSwitchExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // An `Expression` single slot at index 0 and a `SwitchSections` collection occupying the
    // contiguous range [1, 1 + Count). `GetChildCount` is `1 + Count` (the single slot plus the
    // collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single case
    // then a collection step). `GetCollectionByKind` returns the `SwitchSections` collection for
    // the `SwitchExpressionSection` kind (the node's only collection).

    int GetChildCount() const override { return 1 + switchSections_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return expression_;
        i--;
        int n = switchSections_.Count();
        if (i < n)
            return switchSections_.At(i);
        throw std::out_of_range("SwitchExpression::GetChild");
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
            switchSections_.SetAt(i, static_cast<SwitchExpressionSection*>(value));
            return;
        }
        throw std::out_of_range("SwitchExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &ExpressionSlot;
        i--;
        int n = switchSections_.Count();
        if (i < n)
            return &SwitchSectionsSlot;
        throw std::out_of_range("SwitchExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::SwitchExpressionSection)
            return &switchSections_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SwitchExpression o && this.Expression.DoMatch(o.Expression, match) &&
    // this.SwitchSections.DoMatch(o.SwitchSections, match)`. The terms are in `MembersToMatch`
    // order, which is the source declaration order (`Expression`, `SwitchSections`). The
    // `Expression` term is a non-nullable recursive child, so the generator emits a DIRECT
    // `this.Expression.DoMatch(o.Expression, match)` -- ported through `MatchRequired` (the D231
    // [class.access.derived] workaround, since a derived node may not call the protected
    // `DoMatch` through a base `Expression*`); the `SwitchSections` term is the collection
    // recursive match (the generator emits the collection-typed recursive term directly, NOT
    // `MatchOptional`, which it emits only for a nullable non-collection child). A type-only
    // mismatch (not a `SwitchExpression`) rejects early. The `Expression` `MatchRequired` is the
    // first term, so a half-constructed pattern (a null `Expression`) rejects without crashing
    // (the `MatchRequired` null-pattern guard).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<SwitchExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && switchSections_.DoMatch(o->switchSections_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `Expression` deep-cloned through the setter (which re-parents; `Expression::Clone()`
    // returns `Expression*`, the covariant override, which `Expression(Expression*)` accepts
    // directly), and every `SwitchSections` element deep-cloned through `Add` (which re-parents
    // and re-indexes; `SwitchExpressionSection::Clone()` returns `SwitchExpressionSection*`,
    // which `Add(SwitchExpressionSection*)` accepts directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `SwitchExpression` does not derive `EndLocation`), so they are not copied (the
    // `InvocationExpression`/`CastExpression` no-location-copy precedent). The covariant return
    // is `SwitchExpression*` (through `Expression*`, the `Expression::Clone` pure-virtual).
    SwitchExpression* Clone() const override {
        auto* node = new SwitchExpression();
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(expression_->Clone());
        for (int i = 0; i < switchSections_.Count(); i++)
            node->switchSections_.Add(switchSections_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `expression_` is null until the governing expression is set (a
    // required slot -- `CheckInvariant` asserts it is filled); `switchSections_` is the
    // always-present collection member (empty until the first `Add`, incremental). The
    // `expression_` field type uses the elaborated `class Expression` (the `Expression()` accessor
    // shadows the base type in this scope -- the `CastExpression` D243 precedent); the
    // `switchSections_` field type is the plain `AstNodeCollectionT<SwitchExpressionSection>` (no
    // shadowing).
    class Expression* expression_ = nullptr;
    AstNodeCollectionT<SwitchExpressionSection> switchSections_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_SWITCHEXPRESSION_HPP
