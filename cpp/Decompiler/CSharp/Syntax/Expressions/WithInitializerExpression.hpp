// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
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

// Port of the `WithInitializerExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/WithInitializerExpression.cs (the generated
// `WithInitializerExpression.g.cs`; the hand-written partial declares only the two slot
// properties, no ctors, no helpers, no const strings). The next in-order Phase-5 piece per
// the D299 plan ("the remaining Expression nodes: ... WithInitializerExpression ...").
//
// `with_expression ::= expression 'with' array_initializer` (C# grammar section 12.10): a
// sealed `Expression` with two single, REQUIRED (non-nullable) `[Slot]` children -- an
// `Expression` (the receiver of the `with`) and an `Initializer` `ArrayInitializerExpression`
// (the member initializer list inside the braces) -- and NO scalar enum. The generator emits
// two typed slot statics (`ExpressionSlot` pointing at the shared `Slots::Expression` kind,
// `InitializerSlot` pointing at the shared `Slots::Initializer` kind -- both already ported:
// `Slots::Expression` by `UnaryOperatorExpression`, `Slots::Initializer` by
// `ObjectCreateExpression`/`ArrayCreateExpression` as the cycle-broken kind in
// `ArrayInitializerExpression.hpp` -- so no new `Slots` constant), the const-index
// `SetChildNode(ref field, value, index)` setters (no collection precedes either slot, so
// each flattened index is the constant slot position 0/1), the `GetChildCount`/`GetChild`/
// `SetChild`/`GetChildSlotInfo` overrides over the two single slots, and the `DoMatch`
// `return other is WithInitializerExpression o &&
// this.Expression.DoMatch(o.Expression, match) && this.Initializer.DoMatch(o.Initializer,
// match)`. Both children are NON-NULLABLE recursive, so the generator emits the direct
// `this.{member}.DoMatch(o.{member}, match)` term for each (NOT `MatchOptional`, which the
// generator emits only for a nullable recursive child); there is no scalar enum, so there is
// no `Any`-wildcard term. `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`);
// the port overrides it (no `MemberwiseClone`): deep-clones the two children through the
// setters (which re-parent) and copies the annotation channel. There is no scalar member, so
// `Clone` copies no scalar.
//
// C++ name-shadowing crux (the D231 `UnaryOperatorExpression`/D243 `CastExpression` precedent):
// the C# property `Expression` of type `Expression` (a property named the same as its type --
// legal in C#, which keeps property and type names in separate spaces) ports as an accessor
// `Expression()` that SHADOWS the `Expression` base type within this class scope (C++
// unqualified name lookup finds the member and stops, even though it is not a type -- the D224
// `Annotation<T>()`-shadows-the-`Annotation`-type crux). Every type usage AFTER the
// `Expression()` getter is declared therefore uses the ELABORATED-TYPE-SPECIFIER
// `class Expression` (basic.lookup.elab: an elaborated specifier ignores non-type names and
// finds the hidden class): the setter parameter, the slot static's element type, the
// `static_cast` in `SetChild`/`Clone`, and the backing field. The ctor parameter and the
// getter return type precede the getter's declaration, so they use the plain `Expression` (no
// member function is in scope there yet). The `Initializer` accessor does NOT collide with
// any class in the `Syntax` namespace (there is `ArrayInitializerExpression`, not
// `Initializer` -- the `MemberType.MemberName` D238 / `VariableInitializer.Initializer` D266
// differently-named-property precedent), so no elaborated specifier is needed for the
// `ArrayInitializerExpression` operand type.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_WITHINITIALIZEREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_WITHINITIALIZEREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class WithInitializerExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The `[DecompilerAstNode]` (no arg) means
// `hasPatternPlaceholder` defaults false (no `PatternPlaceholder` nested class) and
// `NeedsVisitor` is `!IsAbstract && base.IsAbstract` = true (the `Expression` base is
// abstract), so the generator emits the `AcceptVisitor` override + the `Visit` method (no
// pattern placeholder). Two single, REQUIRED (non-nullable) `[Slot]` children (`Expression`
// + `Initializer` `ArrayInitializerExpression`), no scalar.
class WithInitializerExpression final : public Expression {
public:
    ~WithInitializerExpression() override = default;

    // The generated empty ctor (the C# `public WithInitializerExpression()`). Both slots
    // default to null (no receiver, no initializer). Null slots violate the required-slot
    // invariant, so a default-constructed node is only valid until the slots are set (or
    // until `DoMatch`/`CheckInvariant` observe the missing children).
    WithInitializerExpression() = default;

    // The generated all-params ctor (the C# `public WithInitializerExpression(Expression
    // expression, ArrayInitializerExpression initializer)`). Delegated to the empty ctor
    // then the setters so the slot machinery re-parents and re-indexes the children. The
    // parameter types precede the accessor declarations, so the plain `Expression`/
    // `ArrayInitializerExpression` are unshadowed here; the body is in complete-class context,
    // so the `Expression(expression)`/`Initializer(initializer)` calls resolve to the setters
    // declared below. There is no scalar enum, so the two slot children are the only ctor
    // params (the generator emits a single full ctor -- both are required, so the required
    // prefix IS the full set; no shorter prefix ctor and no `params` overload, since no
    // collection slot is present).
    WithInitializerExpression(Expression* expression, ArrayInitializerExpression* initializer)
        : WithInitializerExpression() {
        Expression(expression);
        Initializer(initializer);
    }

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). The getter
    // return type precedes the getter's declaration, so the plain `Expression` (the base
    // type) is unshadowed there; the setter parameter type uses the elaborated specifier
    // `class Expression` because the `Expression()` getter declared just above shadows the
    // `Expression` base type in this class scope (the D231 crux).
    Expression* Expression() const { return expression_; }
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The C# `[Slot("Initializer")] ArrayInitializerExpression Initializer` -- a single,
    // REQUIRED (non-nullable) `ArrayInitializerExpression` child at flattened index 1. The
    // generator emits the const-index `SetChildNode(ref field, value, 1)` setter (no
    // collection precedes it). No name shadowing (the `Initializer()` accessor does not
    // collide with the `ArrayInitializerExpression` type -- no class named `Initializer`
    // lives in the `Syntax` namespace, the `MemberType.MemberName` D238 differently-named-
    // property precedent), so the operand type is the plain `ArrayInitializerExpression`.
    ArrayInitializerExpression* Initializer() const { return initializer_; }
    void Initializer(ArrayInitializerExpression* value) {
        SetChildNode(initializer_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. Both
    // kinds are already ported (`Slots::Expression` by `UnaryOperatorExpression`,
    // `Slots::Initializer` by `ObjectCreateExpression`/`ArrayCreateExpression` as the
    // cycle-broken kind in `ArrayInitializerExpression.hpp`), so no new `Slots` constant is
    // added. The `IsOptional` flag is false for each (the slots are required -- the C#
    // properties are non-nullable); the kind carries identity only. The `ExpressionSlot`
    // element type uses the elaborated `class Expression` (the `Expression()` accessor
    // shadows the base type in this scope); the `InitializerSlot` element type is the plain
    // `ArrayInitializerExpression` (no shadowing).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};
    static inline const CSharpSlotInfoT<ArrayInitializerExpression> InitializerSlot{"Initializer", false, &Slots::Initializer, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitWithInitializerExpression` (the class name does not end in
    // "AstType", so the generator's visit-method-name default yields
    // `VisitWithInitializerExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitWithInitializerExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`Expression`/`Initializer`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
    // index switch (the generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            case 1: return initializer_;
            default: throw std::out_of_range("WithInitializerExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            case 1: SetChildNode(initializer_, static_cast<ArrayInitializerExpression*>(value), 1); break;
            default: throw std::out_of_range("WithInitializerExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            case 1: return &InitializerSlot;
            default: throw std::out_of_range("WithInitializerExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is WithInitializerExpression o && this.Expression.DoMatch(o.Expression,
    // match) && this.Initializer.DoMatch(o.Initializer, match)`. Both children are
    // NON-NULLABLE recursive, so the generator emits the direct `this.{member}.DoMatch(
    // o.{member}, match)` term for each (NOT `MatchOptional`, which the generator emits only
    // for a nullable recursive child); there is no scalar enum, so there is no `Any`-wildcard
    // term. A type-only mismatch (not a `WithInitializerExpression`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is
    // present; the port routes each through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`/`ArrayInitializerExpression*`.
    // `MatchRequired` guards a missing pattern-side child defensively (a null pattern child
    // does not match; the C# would null-deref), and a null candidate child flows through the
    // child's `DoMatch(nullptr)` which returns false. For well-formed nodes (both children
    // set) the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<WithInitializerExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && MatchRequired(initializer_, o->initializer_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the two children through the setters (which re-parent and
    // re-index via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not
    // stored on this node (no own location fields -- the base fields hold the print-time
    // span), so only the children + annotation channel are copied. Each child is skipped if
    // absent (`Clone` tolerates a missing child even though the slot is required -- the
    // invariant is enforced by `CheckInvariant`, not by `Clone`). The operand clone is cast
    // to the elaborated `class Expression*` (the `Expression()` accessor shadows the base
    // type in this scope); the `ArrayInitializerExpression::Clone()` returns
    // `ArrayInitializerExpression*` (the covariant override through `Expression*`), which
    // `Initializer(ArrayInitializerExpression*)` accepts directly.
    WithInitializerExpression* Clone() const override {
        auto* node = new WithInitializerExpression();
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        if (initializer_ != nullptr)
            node->Initializer(static_cast<ArrayInitializerExpression*>(initializer_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. The `expression_` field uses the elaborated
    // `class Expression` (the `Expression()` accessor declared above shadows the `Expression`
    // base type in this class scope); the `initializer_` field is the plain
    // `ArrayInitializerExpression` (no shadowing).
    class Expression* expression_ = nullptr;
    ArrayInitializerExpression* initializer_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_WITHINITIALIZEREXPRESSION_HPP
