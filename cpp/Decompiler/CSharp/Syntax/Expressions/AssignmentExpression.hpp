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

// Port of the `AssignmentExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.cs (the generated
// `AssignmentExpression.g.cs` + the hand-written partial). The second slot-bearing C# AST
// node (PORT_PLAN.md section 5.2 / decision D1: port the generated *output* by hand) -- the
// next in-order Phase-5 piece per the D229 plan ("the remaining single-slot-only Expression
// nodes (AssignmentExpression, UnaryOperatorExpression, CastExpression, ConditionalExpression,
// ...) can land alongside it without AstType"). It shares the slot-storage contract with
// `BinaryOperatorExpression` (the first slot-bearing node): two single, nullable
// `[Slot]` `Expression` children (`Left`/`Right`) and a scalar `Operator` enum (not a slot).
//
// `assignment_expression ::= expression assignment_operator expression` (C# grammar 12.24):
// `assignment_operator ::= '=' | '+=' | '-=' | '*=' | '/=' | '%=' | '<<=' | '>>=' | '>>>='
// | '&=' | '|=' | '^='`. The generator emits one typed `CSharpSlotInfo<Expression>` slot
// static per slot (`LeftSlot`/`RightSlot`) pointing at the shared `Slots::Left`/`Slots::Right`
// kind, the const-index `SetChildNode(ref field, value, index)` setters (no collection precedes
// either slot, so the flattened index is the constant slot position), the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the two single slots,
// and the `DoMatch` `return other is AssignmentExpression o && MatchOptional(this.Left,
// o.Left, match) && MatchOptional(this.Right, o.Right, match) && (this.Operator ==
// AssignmentOperatorType.Any || this.Operator == o.Operator)`. `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// copies the scalar `Operator`, deep-clones the children through the setters (which re-parent),
// and copies the annotation channel.
//
// The hand-written `(left, right)` convenience ctor (the common `a = b` shape) defaults
// `Operator` to `Assign` (the enum's zero value -- the C# comment: "Simple assignment
// convenience: Operator defaults to Assign"); the generated empty ctor likewise leaves
// `Operator = Assign`, and the generated all-params `(left, op, right)` ctor is also carried.
//
// The hand-written `GetOperatorToken` static helper and the token-string constants
// (`AssignToken`, `AddToken`, ...) are now ported (the D326 output-visitor slice that
// implements `VisitAssignmentExpression`); `GetCorrespondingBinaryOperator`/
// `GetLinqNodeType`/`GetAssignmentOperatorTypeFromExpressionType` (map to `System.Linq.
// Expressions.ExpressionType`, a BCL enum) stay deferred until the resolver consumes them.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ASSIGNMENTEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ASSIGNMENTEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum AssignmentOperatorType` -- the kind of assignment operator. The enum's
// zero value is `Assign` (the plain `=`), so the C# default `Operator` (an uninitialized
// `AssignmentOperatorType` property) is `Assign`. `Any` (the last value) is the
// pattern-matching wildcard: the generator emits the `== Any || == o.Operator` term in
// `DoMatch` because this enum declares an `Any` member (the generator's `hasAny` detection,
// matching the `BinaryOperatorType.Any` precedent -- the `Any` value's position is not
// load-bearing; the generator checks for the member by name). A `std::uint8_t`-based
// `enum class` (the C# `enum AssignmentOperatorType` defaults to `int`; the underlying width
// is not load-bearing here, so the default `int`-sized `enum class` is faithful).
enum class AssignmentOperatorType {
    Assign,            // left = right
    Add,               // left += right
    Subtract,          // left -= right
    Multiply,          // left *= right
    Divide,            // left /= right
    Modulus,           // left %= right
    ShiftLeft,         // left <<= right
    ShiftRight,        // left >>= right
    UnsignedShiftRight,  // left >>>= right
    BitwiseAnd,        // left &= right
    BitwiseOr,         // left |= right
    ExclusiveOr,       // left ^= right
    Any                // Any operator (used in pattern matching)
};

// The C# `public sealed partial class AssignmentExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The second concrete node with `[Slot]` children,
// sharing the `BinaryOperatorExpression` two-single-slot shape.
class AssignmentExpression final : public Expression {
public:
    ~AssignmentExpression() override = default;

    // The C# `public const string` token constants -- the compound-assignment operator symbol
    // strings the output visitor emits via `WriteToken`. Compile-time literals carried as
    // `static constexpr const char*` (static fields, not instance state).
    static constexpr const char* AssignToken = "=";
    static constexpr const char* AddToken = "+=";
    static constexpr const char* SubtractToken = "-=";
    static constexpr const char* MultiplyToken = "*=";
    static constexpr const char* DivideToken = "/=";
    static constexpr const char* ModulusToken = "%=";
    static constexpr const char* ShiftLeftToken = "<<=";
    static constexpr const char* ShiftRightToken = ">>=";
    static constexpr const char* UnsignedShiftRightToken = ">>>=";
    static constexpr const char* BitwiseAndToken = "&=";
    static constexpr const char* BitwiseOrToken = "|=";
    static constexpr const char* ExclusiveOrToken = "^=";

    // The C# `public static string GetOperatorToken(AssignmentOperatorType op)` -- maps the
    // enum to its token string. The `default` case throws `NotSupportedException`.
    static const char* GetOperatorToken(AssignmentOperatorType op) {
        switch (op) {
            case AssignmentOperatorType::Assign: return AssignToken;
            case AssignmentOperatorType::Add: return AddToken;
            case AssignmentOperatorType::Subtract: return SubtractToken;
            case AssignmentOperatorType::Multiply: return MultiplyToken;
            case AssignmentOperatorType::Divide: return DivideToken;
            case AssignmentOperatorType::Modulus: return ModulusToken;
            case AssignmentOperatorType::ShiftLeft: return ShiftLeftToken;
            case AssignmentOperatorType::ShiftRight: return ShiftRightToken;
            case AssignmentOperatorType::UnsignedShiftRight: return UnsignedShiftRightToken;
            case AssignmentOperatorType::BitwiseAnd: return BitwiseAndToken;
            case AssignmentOperatorType::BitwiseOr: return BitwiseOrToken;
            case AssignmentOperatorType::ExclusiveOr: return ExclusiveOrToken;
            default: throw std::out_of_range("Invalid value for AssignmentOperatorType");
        }
    }

    // The generated empty ctor (the C# `public AssignmentExpression()`). `Operator` defaults
    // to `Assign` (the enum's zero value -- the C# default for an uninitialized
    // `AssignmentOperatorType` property); `Left`/`Right` default to null (no operand).
    AssignmentExpression() = default;

    // The hand-written `(left, right)` convenience ctor (the common `a = b` shape). Defaults
    // `Operator` to `Assign` (the C# comment: "Simple assignment convenience: Operator defaults
    // to Assign"). Delegated to the empty ctor then the setters so the slot machinery
    // re-parents and re-indexes the children.
    AssignmentExpression(Expression* left, Expression* right)
        : AssignmentExpression() {
        Left(left);
        Right(right);
    }

    // The generated all-params ctor (the C# `public AssignmentExpression(Expression? left,
    // AssignmentOperatorType op, Expression? right)`). Sets `Left` and `Operator` (the
    // required prefix), then `Right`. Delegated to the empty ctor then the setters so the
    // slot machinery re-parents and re-indexes the children.
    AssignmentExpression(Expression* left, AssignmentOperatorType op, Expression* right)
        : AssignmentExpression() {
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

    // The C# `AssignmentOperatorType Operator` -- a scalar enum (not a `[Slot]`). A settable
    // enum-typed scalar the generator adds to `MembersToMatch` (with the `Any`-wildcard term)
    // and to the ctor params.
    AssignmentOperatorType Operator() const { return op_; }
    void Operator(AssignmentOperatorType value) { op_ = value; }

    // The C# `[Slot("Right")] Expression? Right` -- a single, nullable `Expression` child at
    // flattened index 1. The const-index `SetChildNode(ref field, value, 1)` setter.
    Expression* Right() const { return right_; }
    void Right(Expression* value) {
        SetChildNode(right_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is true (the slot is nullable); the kind carries identity only. The
    // `Slots::Left`/`Slots::Right` constants are shared with `BinaryOperatorExpression` (they
    // are the same `Expression`-typed operand positions).
    static inline const CSharpSlotInfoT<Expression> LeftSlot{"Left", false, &Slots::Left, true};
    static inline const CSharpSlotInfoT<Expression> RightSlot{"Right", false, &Slots::Right, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitAssignmentExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitAssignmentExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitAssignmentExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitAssignmentExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0 (`Left`) and 1 (`Right`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape, matching the
    // `BinaryOperatorExpression` precedent).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return left_;
            case 1: return right_;
            default: throw std::out_of_range("AssignmentExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(left_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(right_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("AssignmentExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &LeftSlot;
            case 1: return &RightSlot;
            default: throw std::out_of_range("AssignmentExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is AssignmentExpression o && MatchOptional(this.Left, o.Left, match)
    // && MatchOptional(this.Right, o.Right, match) && (this.Operator ==
    // AssignmentOperatorType.Any || this.Operator == o.Operator)`. `Left`/`Right` are nullable
    // recursive children, so the generator emits `MatchOptional` (both absent, or both present
    // and the pattern's `DoMatch` decides); `Operator` is a settable enum with an `Any`
    // member, so the generator emits the `Any`-wildcard term (`Any` matches any candidate
    // operator). A type-only mismatch (not an `AssignmentExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<AssignmentExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(left_, o->left_, match)
            && MatchOptional(right_, o->right_, match)
            && (op_ == AssignmentOperatorType::Any || op_ == o->op_);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the scalar `Operator`, deep-clones the
    // children through the setters (which re-parent and re-index via `SetChildNode`), and
    // copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). The print-time `StartLocation`/`EndLocation` are not stored
    // on this node (no own location fields -- the base fields hold the print-time span), so
    // only the scalar + children + annotation channel are copied.
    AssignmentExpression* Clone() const override {
        auto* node = new AssignmentExpression();
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
    AssignmentOperatorType op_ = AssignmentOperatorType::Assign;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ASSIGNMENTEXPRESSION_HPP
