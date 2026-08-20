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

// Port of the `CastExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/CastExpression.cs (the generated
// `CastExpression.g.cs`; the hand-written partial declares only the two slot properties,
// no ctors, no helpers). The first AstType-bearing `Expression` node (PORT_PLAN.md section
// 5.2 / decision D1: port the generated *output* by hand) -- the next in-order Phase-5 piece
// per the D242 plan ("the remaining AstType-bearing Expression nodes: CastExpression an
// Expression + an AstType ...").
//
// `cast_expression ::= '(' type ')' expression` (C# grammar 12.9.8): an `Expression` with two
// single, REQUIRED (non-nullable) `[Slot]` children -- a `Type` `AstType` (the target type)
// and an `Expression` (the operand being cast) -- and NO scalar enum. The generator emits
// two typed slot statics (`TypeSlot` pointing at the shared `Slots::Type` kind, `ExpressionSlot`
// pointing at the shared `Slots::Expression` kind -- both already ported, so no new `Slots`
// constant), the const-index `SetChildNode(ref field, value, index)` setters (no collection
// precedes either slot, so each flattened index is the constant slot position 0/1), the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the two single
// slots, and the `DoMatch` `return other is CastExpression o &&
// this.Type.DoMatch(o.Type, match) && this.Expression.DoMatch(o.Expression, match)`. Both
// children are NON-NULLABLE recursive, so the generator emits the direct
// `this.{member}.DoMatch(o.{member}, match)` term for each (NOT `MatchOptional`, which the
// generator emits only for a nullable recursive child); there is no scalar enum, so there is
// no `Any`-wildcard term. `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`);
// the port overrides it (no `MemberwiseClone`): deep-clones the two children through the
// setters (which re-parent) and copies the annotation channel. There is no scalar member, so
// `Clone` copies no scalar.
//
// C++ name-shadowing crux (the D231 `UnaryOperatorExpression` precedent): the C# property
// `Expression` of type `Expression` (a property named the same as its type -- legal in C#,
// which keeps property and type names in separate spaces) ports as an accessor `Expression()`
// that SHADOWS the `Expression` base type within this class scope (C++ unqualified name lookup
// finds the member and stops, even though it is not a type -- the D224 `Annotation<T>()`-shadows-
// the-`Annotation`-type crux). Every type usage AFTER the `Expression()` getter is declared
// therefore uses the ELABORATED-TYPE-SPECIFIER `class Expression` (basic.lookup.elab: an
// elaborated specifier ignores non-type names and finds the hidden class): the setter
// parameter, the slot static's element type, the `static_cast` in `SetChild`/`Clone`, and the
// backing field. The ctor parameter and the getter return type precede the getter's
// declaration, so they use the plain `Expression` (no member function is in scope there yet).
// The `Type` accessor does NOT collide with any class in the `Syntax` namespace (there is
// `AstType`, not `Type`), so no elaborated specifier is needed for the `AstType` operand type.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_CASTEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_CASTEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class CastExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The first AstType-bearing `Expression` node: two single,
// REQUIRED (non-nullable) `[Slot]` children (`Type` `AstType` + `Expression`), no scalar.
class CastExpression final : public Expression {
public:
    ~CastExpression() override = default;

    // The generated empty ctor (the C# `public CastExpression()`). Both slots default to
    // null (no type, no operand). Null slots violate the required-slot invariant, so a
    // default-constructed node is only valid until the slots are set (or until `DoMatch`/
    // `CheckInvariant` observe the missing children).
    CastExpression() = default;

    // The generated all-params ctor (the C# `public CastExpression(AstType type, Expression
    // expression)`). Delegated to the empty ctor then the setters so the slot machinery
    // re-parents and re-indexes the children. The parameter types precede the accessor
    // declarations, so the plain `AstType`/`Expression` (the base types) are unshadowed here;
    // the body is in complete-class context, so the `Type(type)`/`Expression(expression)`
    // calls resolve to the setters declared below. There is no scalar enum, so the two slot
    // children are the only ctor params (the generator emits a single full ctor -- both are
    // required, so the required prefix IS the full set; no shorter prefix ctor and no
    // `params` overload, since no collection slot is present).
    CastExpression(AstType* type, Expression* expression)
        : CastExpression() {
        Type(type);
        Expression(expression);
    }

    // The C# `[Slot("Type")] AstType Type` -- a single, REQUIRED (non-nullable) `AstType`
    // child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). The C# getter
    // returns the backing field null-forgiving (`field!`) because the slot is required; the
    // port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type), so callers must keep the child set. No name shadowing (the `Type()` accessor does
    // not collide with the `AstType` base type -- no class named `Type` lives in the `Syntax`
    // namespace), so the operand type is the plain `AstType`.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 1. The generator emits the const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it). The getter return
    // type precedes the getter's declaration, so the plain `Expression` (the base type) is
    // unshadowed there; the setter parameter type uses the elaborated specifier
    // `class Expression` because the `Expression()` getter declared just above shadows the
    // `Expression` base type in this class scope (the D231 crux).
    Expression* Expression() const { return expression_; }
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. Both
    // kinds are already ported (`Slots::Type` by `Attribute`, `Slots::Expression` by
    // `UnaryOperatorExpression`), so no new `Slots` constant is added. The `IsOptional` flag
    // is false for each (the slots are required -- the C# properties are non-nullable); the
    // kind carries identity only. The `ExpressionSlot` element type uses the elaborated
    // `class Expression` (the `Expression()` accessor shadows the base type in this scope);
    // the `TypeSlot` element type is the plain `AstType` (no shadowing).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitCastExpression` (the class name does not end in "AstType", so the
    // generator's visit-method-name default yields `VisitCastExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitCastExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitCastExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitCastExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`Type`/`Expression`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
    // index switch (the generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return expression_;
            default: throw std::out_of_range("CastExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(expression_, static_cast<class Expression*>(value), 1); break;
            default: throw std::out_of_range("CastExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &ExpressionSlot;
            default: throw std::out_of_range("CastExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is CastExpression o && this.Type.DoMatch(o.Type, match) &&
    // this.Expression.DoMatch(o.Expression, match)`. Both children are NON-NULLABLE recursive,
    // so the generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for
    // each (NOT `MatchOptional`, which the generator emits only for a nullable recursive
    // child); there is no scalar enum, so there is no `Any`-wildcard term. A type-only
    // mismatch (not a `CastExpression`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is
    // present; the port routes each through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `AstType*`/`Expression*`. `MatchRequired` guards a
    // missing pattern-side child defensively (a null pattern child does not match; the C#
    // would null-deref), and a null candidate child flows through the child's
    // `DoMatch(nullptr)` which returns false. For well-formed nodes (both children set) the
    // behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<CastExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && MatchRequired(expression_, o->expression_, match);
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
    // invariant is enforced by `CheckInvariant`, not by `Clone`). `AstType::Clone()` returns
    // `AstType*` (the covariant override), which `Type(AstType*)` accepts directly; the
    // operand clone is cast to the elaborated `class Expression*` (the `Expression()` accessor
    // shadows the base type in this scope).
    CastExpression* Clone() const override {
        auto* node = new CastExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. The `expression_` field uses the elaborated
    // `class Expression` (the `Expression()` accessor declared above shadows the `Expression`
    // base type in this class scope); the `type_` field is the plain `AstType` (no shadowing).
    AstType* type_ = nullptr;
    class Expression* expression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_CASTEXPRESSION_HPP
