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

// Port of the `BinaryOperatorExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.cs (the generated
// `BinaryOperatorExpression.g.cs` + the hand-written partial). The first slot-bearing C# AST
// node (PORT_PLAN.md section 5.2 / decision D1: port the generated *output* by hand) -- the
// next in-order Phase-5 piece per the D228 plan ("the slot-bearing Expression nodes ...
// BinaryOperatorExpression"). It exercises the slot-storage contract the generated concrete
// nodes override (`GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo`) and the generated
// `DoMatch` over a nullable recursive child (`MatchOptional`) for the first time in a real
// (non-stub) node, plus the `Any`-wildcard term the generator emits for a settable enum scalar
// whose type declares an `Any` member.
//
// `binary_operator_expression ::= expression binary_operator expression` (C# grammar
// 12.13-12.18, precedence-flattened): an `Expression` with two single, nullable `[Slot]`
// children (`Left`/`Right`, both `Expression?`) and a scalar `Operator` enum (not a slot). The
// generator emits one typed `CSharpSlotInfo<Expression>` slot static per slot
// (`LeftSlot`/`RightSlot`) pointing at the shared `Slots::Left`/`Slots::Right` kind, the
// const-index `SetChildNode(ref field, value, index)` setters (no collection precedes either
// slot, so the flattened index is the constant slot position), the `GetChildCount`/`GetChild`
// /`SetChild`/`GetChildSlotInfo` overrides over the two single slots, and the `DoMatch`
// `return other is BinaryOperatorExpression o && MatchOptional(this.Left, o.Left, match) &&
// MatchOptional(this.Right, o.Right, match) && (this.Operator == BinaryOperatorType.Any ||
// this.Operator == o.Operator)`. `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): copies the scalar
// `Operator`, deep-clones the children through the setters (which re-parent), and copies the
// annotation channel.
//
// The hand-written `GetOperatorToken` static helper and the token-string constants
// (`BitwiseAndToken`, ...) are now ported (the D326 output-visitor slice that implements
// `VisitBinaryOperatorExpression`); `GetLinqNodeType` (maps to `System.Linq.Expressions.
// ExpressionType`, a BCL enum) stays deferred until the resolver consumes it.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_BINARYOPERATOREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_BINARYOPERATOREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum BinaryOperatorType` -- the kind of binary operator. `Any` is the
// pattern-matching wildcard (the generator emits the `== Any || == o.Operator` term in
// `DoMatch` because this enum declares an `Any` member). A `std::uint8_t`-based `enum class`
// (the C# `enum BinaryOperatorType` defaults to `int`; the underlying width is not
// load-bearing here, so the default `int`-sized `enum class` is faithful).
enum class BinaryOperatorType {
    // Any binary operator (used in pattern matching).
    Any,
    // We avoid 'logical or' on purpose (see the C# comment): bitwise vs conditional is
    // ambiguous, so the naming is explicit.
    BitwiseAnd,    // left & right
    BitwiseOr,     // left | right
    ConditionalAnd,  // left && right
    ConditionalOr,   // left || right
    ExclusiveOr,     // left ^ right
    GreaterThan,          // left > right
    GreaterThanOrEqual,   // left >= right
    Equality,             // left == right
    InEquality,           // left != right
    LessThan,             // left < right
    LessThanOrEqual,      // left <= right
    Add,       // left + right
    Subtract,  // left - right
    Multiply,  // left * right
    Divide,    // left / right
    Modulus,    // left % right
    ShiftLeft,          // left << right
    ShiftRight,         // left >> right
    UnsignedShiftRight,  // left >>> right
    NullCoalescing,  // left ?? right
    Range,    // left .. right (left and right optional)
    IsPattern  // left is right (right must be a pattern)
};

// The C# `public sealed partial class BinaryOperatorExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The first concrete node with `[Slot]` children.
class BinaryOperatorExpression final : public Expression {
public:
    ~BinaryOperatorExpression() override = default;

    // The C# `public const string` token constants -- the operator symbol strings the output
    // visitor emits via `WriteToken`/`WriteKeyword`. Compile-time literals carried as
    // `static constexpr const char*` (static fields, not instance state), so they are not in
    // `MembersToMatch`/`DoMatch` (the generator's scan adds only instance `IPropertySymbol`s).
    static constexpr const char* BitwiseAndToken = "&";
    static constexpr const char* BitwiseOrToken = "|";
    static constexpr const char* ConditionalAndToken = "&&";
    static constexpr const char* ConditionalOrToken = "||";
    static constexpr const char* ExclusiveOrToken = "^";
    static constexpr const char* GreaterThanToken = ">";
    static constexpr const char* GreaterThanOrEqualToken = ">=";
    static constexpr const char* EqualityToken = "==";
    static constexpr const char* InEqualityToken = "!=";
    static constexpr const char* LessThanToken = "<";
    static constexpr const char* LessThanOrEqualToken = "<=";
    static constexpr const char* AddToken = "+";
    static constexpr const char* SubtractToken = "-";
    static constexpr const char* MultiplyToken = "*";
    static constexpr const char* DivideToken = "/";
    static constexpr const char* ModulusToken = "%";
    static constexpr const char* ShiftLeftToken = "<<";
    static constexpr const char* ShiftRightToken = ">>";
    static constexpr const char* UnsignedShiftRightToken = ">>>";
    static constexpr const char* NullCoalescingToken = "??";
    static constexpr const char* RangeToken = "..";
    // The C# `public const string IsKeyword = IsExpression.IsKeyword` -- the `is` keyword token
    // for the `IsPattern` operator. `IsExpression` is not yet ported; the value is the literal
    // "is" (the cross-reference to `IsExpression::IsKeyword` is re-pointed when that node lands).
    static constexpr const char* IsKeyword = "is";

    // The C# `public static string GetOperatorToken(BinaryOperatorType op)` -- maps the enum
    // to its token string. The `default` case throws `NotSupportedException` (an invalid enum
    // value), faithfully mirroring the C#.
    static const char* GetOperatorToken(BinaryOperatorType op) {
        switch (op) {
            case BinaryOperatorType::BitwiseAnd: return BitwiseAndToken;
            case BinaryOperatorType::BitwiseOr: return BitwiseOrToken;
            case BinaryOperatorType::ConditionalAnd: return ConditionalAndToken;
            case BinaryOperatorType::ConditionalOr: return ConditionalOrToken;
            case BinaryOperatorType::ExclusiveOr: return ExclusiveOrToken;
            case BinaryOperatorType::GreaterThan: return GreaterThanToken;
            case BinaryOperatorType::GreaterThanOrEqual: return GreaterThanOrEqualToken;
            case BinaryOperatorType::Equality: return EqualityToken;
            case BinaryOperatorType::InEquality: return InEqualityToken;
            case BinaryOperatorType::LessThan: return LessThanToken;
            case BinaryOperatorType::LessThanOrEqual: return LessThanOrEqualToken;
            case BinaryOperatorType::Add: return AddToken;
            case BinaryOperatorType::Subtract: return SubtractToken;
            case BinaryOperatorType::Multiply: return MultiplyToken;
            case BinaryOperatorType::Divide: return DivideToken;
            case BinaryOperatorType::Modulus: return ModulusToken;
            case BinaryOperatorType::ShiftLeft: return ShiftLeftToken;
            case BinaryOperatorType::ShiftRight: return ShiftRightToken;
            case BinaryOperatorType::UnsignedShiftRight: return UnsignedShiftRightToken;
            case BinaryOperatorType::NullCoalescing: return NullCoalescingToken;
            case BinaryOperatorType::Range: return RangeToken;
            case BinaryOperatorType::IsPattern: return IsKeyword;
            default: throw std::out_of_range("Invalid value for BinaryOperatorType");
        }
    }

    // The generated empty ctor (the C# `public BinaryOperatorExpression()`). `Operator`
    // defaults to `Any` (the enum's zero value, the C# default); `Left`/`Right` default to
    // null (no operand).
    BinaryOperatorExpression() = default;

    // The generated all-params ctor (the C# `public BinaryOperatorExpression(Expression?
    // left, BinaryOperatorType op, Expression? right)`). Sets `Left` and `Operator` (the
    // required prefix), then `Right`. Delegated to the empty ctor then the setters so the
    // slot machinery re-parents and re-indexes the children.
    BinaryOperatorExpression(Expression* left, BinaryOperatorType op, Expression* right)
        : BinaryOperatorExpression() {
        Left(left);
        Operator(op);
        Right(right);
    }

    // The C# `[Slot("Left")] Expression? Left` -- a single, nullable `Expression` child at
    // flattened index 0. The generator emits the const-index `SetChildNode(ref field, value,
    // 0)` setter (no collection precedes this slot), so the index is assigned directly and
    // the parent's indices stay valid by construction.
    Expression* Left() const { return left_; }
    void Left(Expression* value) {
        SetChildNode(left_, value, 0);
    }

    // The C# `BinaryOperatorType Operator` -- a scalar enum (not a `[Slot]`). A settable
    // enum-typed scalar the generator adds to `MembersToMatch` (with the `Any`-wildcard term)
    // and to the ctor params.
    BinaryOperatorType Operator() const { return op_; }
    void Operator(BinaryOperatorType value) { op_ = value; }

    // The C# `[Slot("Right")] Expression? Right` -- a single, nullable `Expression` child at
    // flattened index 1. The const-index `SetChildNode(ref field, value, 1)` setter.
    Expression* Right() const { return right_; }
    void Right(Expression* value) {
        SetChildNode(right_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is true (the slot is nullable); the kind carries identity only.
    static inline const CSharpSlotInfoT<Expression> LeftSlot{"Left", false, &Slots::Left, true};
    static inline const CSharpSlotInfoT<Expression> RightSlot{"Right", false, &Slots::Right, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitBinaryOperatorExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitBinaryOperatorExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0 (`Left`) and 1 (`Right`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return left_;
            case 1: return right_;
            default: throw std::out_of_range("BinaryOperatorExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(left_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(right_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("BinaryOperatorExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &LeftSlot;
            case 1: return &RightSlot;
            default: throw std::out_of_range("BinaryOperatorExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is BinaryOperatorExpression o && MatchOptional(this.Left, o.Left, match)
    // && MatchOptional(this.Right, o.Right, match) && (this.Operator == BinaryOperatorType.Any
    // || this.Operator == o.Operator)`. `Left`/`Right` are nullable recursive children, so the
    // generator emits `MatchOptional` (both absent, or both present and the pattern's
    // `DoMatch` decides); `Operator` is a settable enum with an `Any` member, so the generator
    // emits the `Any`-wildcard term (`Any` matches any candidate operator). A type-only
    // mismatch (not a `BinaryOperatorExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<BinaryOperatorExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(left_, o->left_, match)
            && MatchOptional(right_, o->right_, match)
            && (op_ == BinaryOperatorType::Any || op_ == o->op_);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the scalar `Operator`, deep-clones the
    // children through the setters (which re-parent and re-index via `SetChildNode`), and
    // copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). The print-time `StartLocation`/`EndLocation` are not stored
    // on this node (no own location fields -- the base fields hold the print-time span), so
    // only the scalar + children + annotation channel are copied.
    BinaryOperatorExpression* Clone() const override {
        auto* node = new BinaryOperatorExpression();
        node->op_ = op_;
        node->CloneAnnotationsFrom(*this);
        if (left_ != nullptr)
            node->Left(static_cast<Expression*>(left_->Clone()));
        if (right_ != nullptr)
            node->Right(static_cast<Expression*>(right_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    Expression* left_ = nullptr;
    Expression* right_ = nullptr;
    BinaryOperatorType op_ = BinaryOperatorType::Any;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_BINARYOPERATOREXPRESSION_HPP
