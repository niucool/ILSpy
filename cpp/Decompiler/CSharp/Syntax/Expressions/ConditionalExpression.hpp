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

// Port of the `ConditionalExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.cs (the generated
// `ConditionalExpression.g.cs` + the hand-written partial). The fourth slot-bearing C# AST
// node (PORT_PLAN.md section 5.2 / decision D1: port the generated *output* by hand) -- the
// next in-order Phase-5 piece per the D231 plan ("the remaining single-slot-only Expression
// nodes that do not need the AstType hierarchy: ConditionalExpression (Condition +
// TrueExpression + FalseExpression, three single `Expression` slots -- all non-nullable in
// the C# source, so it exercises the `MatchRequired` direct-dispatch pattern three times)").
// It is the first ported node with MORE THAN ONE required (non-nullable) child slot, so it
// is the first to exercise the generator's non-nullable recursive `DoMatch` term three times
// in one node (the direct dispatch `this.{member}.DoMatch(o.{member}, match)`, NOT
// `MatchOptional`, which the generator emits only for a nullable recursive child).
//
// `conditional_expression ::= expression '?' expression ':' expression` (C# grammar 12.21):
// an `Expression` with three single, REQUIRED (non-nullable) `[Slot]` `Expression` children
// (`Condition`/`TrueExpression`/`FalseExpression`) and NO scalar enum. The generator emits
// three typed `CSharpSlotInfo<Expression>` slot statics (`ConditionSlot`/`TrueExpressionSlot`/
// `FalseExpressionSlot`) pointing at the shared `Slots::Condition`/`Slots::True`/`Slots::False`
// kinds (three new kinds -- `Left`/`Right`/`Expression` are the only kinds ported so far), the
// const-index `SetChildNode(ref field, value, index)` setters (no collection precedes any slot,
// so each flattened index is the constant slot position 0/1/2), the `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the three single slots, and the
// `DoMatch` `return other is ConditionalExpression o && this.Condition.DoMatch(o.Condition,
// match) && this.TrueExpression.DoMatch(o.TrueExpression, match) &&
// this.FalseExpression.DoMatch(o.FalseExpression, match)`. `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the three children through the setters (which re-parent) and copies the
// annotation channel. There is no scalar member (no `Operator`), so `Clone` copies no scalar.
//
// The hand-written `QuestionMarkToken`/`ColonToken` const-string fields (the `?`/`:` token
// strings the output visitor emits) are now ported as `static constexpr const char*` public
// members (the D326 output-visitor slice that implements `VisitConditionalExpression`), the
// same convention as `CheckedExpression::CheckedKeyword`. `ColonToken` is the same value as
// `Tokens::Colon` (the C# `ColonToken = Tokens.Colon`).
//
// No C++ name-shadowing crux here: the accessors are `Condition`/`TrueExpression`/`FalseExpression`
// (none named `Expression`), so the `Expression` base type is unshadowed in this class scope and
// the plain `Expression` (the base type) is used throughout -- unlike `UnaryOperatorExpression`
// whose `Expression()` accessor shadows the `Expression` base type and forces the
// `class Expression` elaborated-type-specifier (the D231 crux).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_CONDITIONALEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_CONDITIONALEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ConditionalExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The fourth concrete node with `[Slot]` children,
// and the first with MORE THAN ONE required (non-nullable) child slot.
class ConditionalExpression final : public Expression {
public:
    ~ConditionalExpression() override = default;

    // The C# `public const string QuestionMarkToken = "?"` / `ColonToken = Tokens.Colon` --
    // the `?`/`:` tokens the output visitor emits for the conditional `cond ? a : b`. Compile-
    // time literals carried as `static constexpr const char*` (static fields, not instance
    // state), so they are not in `MembersToMatch`/`DoMatch` (the generator's scan adds only
    // instance `IPropertySymbol`s). `ColonToken` is the literal `":""` (same value as
    // `Tokens::Colon`, the C# `ColonToken = Tokens.Colon` alias).
    static constexpr const char* QuestionMarkToken = "?";
    static constexpr const char* ColonToken = ":";

    // The generated empty ctor (the C# `public ConditionalExpression()`). All three slots
    // default to null (no operands). Null slots violate the required-slot invariant, so a
    // default-constructed node is only valid until the slots are set (or until `DoMatch`/
    // `CheckInvariant` observe the missing children).
    ConditionalExpression() = default;

    // The generated all-params ctor (the C# `public ConditionalExpression(Expression
    // condition, Expression trueExpression, Expression falseExpression)`). Delegated to the
    // empty ctor then the setters so the slot machinery re-parents and re-indexes the
    // children. There is no scalar enum, so the three slot children are the only ctor params
    // (the generator emits a single full ctor -- all three are required, so the required
    // prefix IS the full set; no shorter prefix ctor and no `params` overload, since no
    // collection slot is present).
    ConditionalExpression(Expression* condition, Expression* trueExpression,
                           Expression* falseExpression)
        : ConditionalExpression() {
        Condition(condition);
        TrueExpression(trueExpression);
        FalseExpression(falseExpression);
    }

    // The C# `[Slot("Condition")] Expression Condition` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). The C# getter
    // returns the backing field null-forgiving (`field!`) because the slot is required; the
    // port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type), so callers must keep the child set.
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value, 0);
    }

    // The C# `[Slot("True")] Expression TrueExpression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 1. The slot KIND is `True` (the `[Slot]`
    // argument); the property name is `TrueExpression`, so the per-node slot static is
    // `TrueExpressionSlot` (the generator names the static `{PropertyName}Slot`).
    Expression* TrueExpression() const { return trueExpression_; }
    void TrueExpression(Expression* value) {
        SetChildNode(trueExpression_, value, 1);
    }

    // The C# `[Slot("False")] Expression FalseExpression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 2. The slot KIND is `False`; the per-node slot
    // static is `FalseExpressionSlot`.
    Expression* FalseExpression() const { return falseExpression_; }
    void FalseExpression(Expression* value) {
        SetChildNode(falseExpression_, value, 2);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. The
    // `IsOptional` flag is false for each (the slots are required -- the C# properties are
    // non-nullable); the kind carries identity only. `Condition` is a kind SHARED with several
    // statement nodes (`IfElseStatement`/`WhileStatement`/`DoWhileStatement`/`ForStatement`/
    // `TryCatchStatement`/`QueryExpression`, all `Expression`-typed) -- the first ported kind
    // shared across an `Expression` node and `Statement` nodes (which land later); `True`/
    // `False` are unique to `ConditionalExpression`.
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, false};
    static inline const CSharpSlotInfoT<Expression> TrueExpressionSlot{"TrueExpression", false, &Slots::True, false};
    static inline const CSharpSlotInfoT<Expression> FalseExpressionSlot{"FalseExpression", false, &Slots::False, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitConditionalExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitConditionalExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Three single slots at flattened indices 0/1/2 (`Condition`/`TrueExpression`/
    // `FalseExpression`); no collection, so `GetChildCount` is the constant 3 and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the generator's
    // `WriteReturnDispatchSwitch` shape, with three cases).

    int GetChildCount() const override { return 3; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return condition_;
            case 1: return trueExpression_;
            case 2: return falseExpression_;
            default: throw std::out_of_range("ConditionalExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(condition_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(trueExpression_, static_cast<Expression*>(value), 1); break;
            case 2: SetChildNode(falseExpression_, static_cast<Expression*>(value), 2); break;
            default: throw std::out_of_range("ConditionalExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ConditionSlot;
            case 1: return &TrueExpressionSlot;
            case 2: return &FalseExpressionSlot;
            default: throw std::out_of_range("ConditionalExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ConditionalExpression o && this.Condition.DoMatch(o.Condition, match) &&
    // this.TrueExpression.DoMatch(o.TrueExpression, match) && this.FalseExpression.DoMatch(
    // o.FalseExpression, match)`. All three children are NON-NULLABLE recursive, so the
    // generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each
    // (NOT `MatchOptional`, which the generator emits only for a nullable recursive child);
    // there is no scalar enum, so there is no `Any`-wildcard term. A type-only mismatch (not
    // a `ConditionalExpression`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is
    // present; the port routes each through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing
    // pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)`
    // which returns false. For well-formed nodes (all children set) the behavior is identical
    // to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ConditionalExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(condition_, o->condition_, match)
            && MatchRequired(trueExpression_, o->trueExpression_, match)
            && MatchRequired(falseExpression_, o->falseExpression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the three children through the setters (which re-parent and
    // re-index via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not
    // stored on this node (no own location fields -- the base fields hold the print-time
    // span), so only the children + annotation channel are copied. Each child is skipped if
    // absent (`Clone` tolerates a missing child even though the slot is required -- the
    // invariant is enforced by `CheckInvariant`, not by `Clone`).
    ConditionalExpression* Clone() const override {
        auto* node = new ConditionalExpression();
        node->CloneAnnotationsFrom(*this);
        if (condition_ != nullptr)
            node->Condition(static_cast<Expression*>(condition_->Clone()));
        if (trueExpression_ != nullptr)
            node->TrueExpression(static_cast<Expression*>(trueExpression_->Clone()));
        if (falseExpression_ != nullptr)
            node->FalseExpression(static_cast<Expression*>(falseExpression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. No elaborated-type-specifier is needed: the accessors are
    // `Condition`/`TrueExpression`/`FalseExpression` (none named `Expression`), so the
    // `Expression` base type is unshadowed in this class scope.
    Expression* condition_ = nullptr;
    Expression* trueExpression_ = nullptr;
    Expression* falseExpression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_CONDITIONALEXPRESSION_HPP
