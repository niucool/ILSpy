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

// Port of the `UnaryOperatorExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.cs (the generated
// `UnaryOperatorExpression.g.cs` + the hand-written partial). The third slot-bearing C# AST
// node (PORT_PLAN.md section 5.2 / decision D1: port the generated *output* by hand) -- the
// next in-order Phase-5 piece per the D230 plan ("the remaining single-slot-only Expression
// nodes that do not need the AstType hierarchy: UnaryOperatorExpression ... the same
// one-single-slot shape"). It is the first ported node with a NON-NULLABLE (required) child
// slot, so it is the first real node to exercise the generator's non-nullable recursive
// `DoMatch` term (`this.Expression.DoMatch(o.Expression, match)`, a direct dispatch -- not
// `MatchOptional`, which the generator emits only for a nullable recursive child).
//
// `unary_operator_expression ::= unary_operator expression | expression postfix_unary_operator`
// (C# grammar 12.9, 12.8.16): an `Expression` with a single `[Slot] Expression` child (the
// operand, non-nullable in the C# source -- `[Slot("Expression")] public partial Expression
// Expression`, no `?`, so the slot is required) and a scalar `Operator` enum (not a slot).
// The generator emits one typed `CSharpSlotInfo<Expression>` slot static (`ExpressionSlot`)
// pointing at the shared `Slots::Expression` kind (a new kind -- `Left`/`Right` are the only
// kinds ported so far), the const-index `SetChildNode(ref field, value, 0)` setter (the single
// slot is the first and only slot, so the flattened index is the constant 0), the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single slot,
// and the `DoMatch` `return other is UnaryOperatorExpression o &&
// this.Expression.DoMatch(o.Expression, match) && (this.Operator == UnaryOperatorType.Any ||
// this.Operator == o.Operator)`. `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): copies the scalar
// `Operator`, deep-clones the child through the setter (which re-parents), and copies the
// annotation channel.
//
// The hand-written `GetOperatorToken`/`GetLinqNodeType` static helpers and the token-string
// constants (`NotToken`, `MinusToken`, ...) are DEFERRED: they map the operator to its token
// string and to `System.Linq.Expressions.ExpressionType` (a BCL enum), used by the
// resolver/output stage (not the AST structure); they land when the output visitor /
// resolver consume them (the D229 `BinaryOperatorExpression` / D230 `AssignmentExpression`
// precedent deferred the same helpers).
//
// C++ name-shadowing crux: the C# property is `Expression` of type `Expression` (a property
// named the same as its type -- legal in C#, which keeps property and type names in separate
// spaces). The faithful port names the accessor `Expression()`, but a member function named
// `Expression` SHADOWS the `Expression` base type within this class scope (C++ unqualified
// name lookup finds the member and stops, even though it is not a type -- the D224
// `Annotation<T>()`-shadows-the-`Annotation`-type crux). Every type usage AFTER the
// `Expression()` getter is declared therefore uses the ELABORATED-TYPE-SPECIFIER
// `class Expression` (basic.lookup.elab: an elaborated specifier ignores non-type names and
// finds the hidden class), so the setter parameter, the slot static, the `static_cast`s, and
// the backing field all spell the operand type as `class Expression`. The ctor parameter
// and the getter return type precede the getter's declaration, so they use the plain
// `Expression` (no member function is in scope there yet).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNARYOPERATOREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNARYOPERATOREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum UnaryOperatorType` -- the kind of unary operator. `Any` is the
// pattern-matching wildcard AND the enum's zero value (so the C# default `Operator` -- an
// uninitialized `UnaryOperatorType` property -- IS `Any`, unlike `AssignmentOperatorType`
// whose zero is `Assign`), so the generator emits the `== Any || == o.Operator` term in
// `DoMatch` (the `hasAny` path -- this enum declares an `Any` member, matching the
// `BinaryOperatorType` precedent). A `std::uint8_t`-based `enum class` (the C#
// `enum UnaryOperatorType` defaults to `int`; the underlying width is not load-bearing here,
// so the default `int`-sized `enum class` is faithful).
enum class UnaryOperatorType {
    // Any unary operator (used in pattern matching).
    Any,
    // Logical not (!a).
    Not,
    // Bitwise not (~a).
    BitNot,
    // Unary minus (-a).
    Minus,
    // Unary plus (+a).
    Plus,
    // Pre increment (++a).
    Increment,
    // Pre decrement (--a).
    Decrement,
    // Post increment (a++).
    PostIncrement,
    // Post decrement (a--).
    PostDecrement,
    // Dereferencing (*a).
    Dereference,
    // Get address (&a).
    AddressOf,
    // C# 5.0 await.
    Await,
    // C# 6 null-conditional operator (?. / ?[]).
    NullConditional,
    // Wrapper around a primary expression containing a null-conditional operator (no
    // syntax in C#; used to insert parentheses).
    NullConditionalRewrap,
    // Implicit call of "operator true".
    IsTrue,
    // C# 8 postfix ! operator (dammit operator).
    SuppressNullableWarning,
    // C# 8 prefix ^ operator.
    IndexFromEnd,
    // C# 9 not pattern.
    PatternNot,
    // C# 9 relational patterns.
    PatternRelationalLessThan,
    PatternRelationalLessThanOrEqual,
    PatternRelationalGreaterThan,
    PatternRelationalGreaterThanOrEqual
};

// The C# `public sealed partial class UnaryOperatorExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The third concrete node with `[Slot]` children,
// and the first with a required (non-nullable) child slot.
class UnaryOperatorExpression final : public Expression {
public:
    ~UnaryOperatorExpression() override = default;

    // The generated empty ctor (the C# `public UnaryOperatorExpression()`). `Operator`
    // defaults to `Any` (the enum's zero value, the C# default -- unlike
    // `AssignmentOperatorType` whose zero is `Assign`); `Expression` defaults to null (no
    // operand). A null operand violates the required-slot invariant, so a default-constructed
    // node is only valid until `Expression` is set (or until `DoMatch`/`CheckInvariant`
    // observe the missing child).
    UnaryOperatorExpression() = default;

    // The generated all-params ctor (the C# `public UnaryOperatorExpression(Expression?
    // expression, UnaryOperatorType op)`). Sets `Expression` then `Operator`. Delegated to
    // the empty ctor then the setters so the slot machinery re-parents and re-indexes the
    // child. The parameter type precedes the `Expression()` accessor declarations, so the
    // plain `Expression` (the base type) is unshadowed here; the body is in complete-class
    // context, so the `Expression(expression)` call resolves to the setter declared below.
    UnaryOperatorExpression(Expression* expression, UnaryOperatorType op)
        : UnaryOperatorExpression() {
        Expression(expression);
        Operator(op);
    }

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 0. The generator emits the
    // const-index `SetChildNode(ref field, value, 0)` setter (the single slot is the first
    // and only slot, no collection precedes it), so the index is assigned directly and the
    // parent's indices stay valid by construction. The C# getter returns the backing field
    // null-forgiving (`field!`) because the slot is required; the port returns the raw
    // pointer (a required slot is non-null only by invariant, not by type), so callers must
    // keep the child set.
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The C# `UnaryOperatorType Operator` -- a scalar enum (not a `[Slot]`). A settable
    // enum-typed scalar the generator adds to `MembersToMatch` (with the `Any`-wildcard
    // term) and to the ctor params.
    UnaryOperatorType Operator() const { return op_; }
    void Operator(UnaryOperatorType value) { op_ = value; }

    // The C# `public const string AwaitKeyword = "await"` (the `await` keyword token the
    // output visitor emits for the `Await` operator). Part of the node's public API: it is
    // aliased by sibling nodes that also carry an `await` modifier (`UsingStatement.AwaitKeyword`,
    // `ForeachStatement.AwaitKeyword` both `= UnaryOperatorExpression.AwaitKeyword` in C#), so it
    // ports now (unlike the per-operator token-string constants `NotToken`/`MinusToken`/...
    // deferred in D229, which are a lookup table consumed only by the output/resolver stage).
    // Ports as a `static constexpr const char*` (a static field, not instance state), so the
    // generator's `MembersToMatch` (which iterates only instance `IPropertySymbol`s) excludes it
    // from the `DoMatch` (the `CheckedExpression.CheckedKeyword` D234 precedent).
    static constexpr const char* AwaitKeyword = "await";

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable);
    // the kind carries identity only. The `Slots::Expression` constant is a new kind (the
    // `Left`/`Right` operand positions are distinct from this operand position). The
    // element type uses the elaborated `class Expression` (the `Expression()` accessor
    // shadows the base type in this scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitUnaryOperatorExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUnaryOperatorExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so
    // `GetChildCount` is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape, with a single
    // case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("UnaryOperatorExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("UnaryOperatorExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("UnaryOperatorExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is UnaryOperatorExpression o && this.Expression.DoMatch(o.Expression,
    // match) && (this.Operator == UnaryOperatorType.Any || this.Operator == o.Operator)`.
    // `Expression` is a NON-NULLABLE recursive child, so the generator emits the direct
    // `this.Expression.DoMatch(o.Expression, match)` term (NOT `MatchOptional`, which the
    // generator emits only for a nullable recursive child); `Operator` is a settable enum
    // with an `Any` member, so the generator emits the `Any`-wildcard term. A type-only
    // mismatch (not a `UnaryOperatorExpression`) rejects early.
    //
    // The C# direct dispatch (`this.Expression.DoMatch`) assumes the required child is
    // present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing
    // operand defensively (a null pattern child does not match; the C# would null-deref),
    // and a null candidate child flows through the operand's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (the operand always set) the behavior is
    // identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<UnaryOperatorExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && (op_ == UnaryOperatorType::Any || op_ == o->op_);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the scalar `Operator`, deep-clones the
    // child through the setter (which re-parents and re-indexes via `SetChildNode`), and
    // copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). The print-time `StartLocation`/`EndLocation` are not stored
    // on this node (no own location fields -- the base fields hold the print-time span), so
    // only the scalar + child + annotation channel are copied. The `static_cast` uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in
    // this scope).
    UnaryOperatorExpression* Clone() const override {
        auto* node = new UnaryOperatorExpression();
        node->op_ = op_;
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field uses the elaborated `class Expression` (the `Expression()` accessor
    // declared above shadows the `Expression` base type in this class scope). A required slot
    // is non-null only by invariant, so the pointer is null until the operand is set.
    class Expression* expression_ = nullptr;
    UnaryOperatorType op_ = UnaryOperatorType::Any;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNARYOPERATOREXPRESSION_HPP
