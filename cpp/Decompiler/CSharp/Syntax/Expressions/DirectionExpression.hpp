// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to do in the Software
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

// Port of the `DirectionExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/DirectionExpression.cs (the generated
// `DirectionExpression.g.cs` + the hand-written partial). The next in-order Phase-5 piece per
// the D234 plan ("the other one-required-Expression-slot nodes still unported ... if they
// share the shape"). It is the `UnaryOperatorExpression` shape (a single, REQUIRED
// (non-nullable) `Expression` child slot plus a scalar enum) but is the FIRST ported node
// whose scalar enum (`FieldDirection`) declares NO `Any` member, so its generated `DoMatch`
// uses a PLAIN equality term (`this.FieldDirection == o.FieldDirection`) rather than the
// `Any`-wildcard term the `Operator`-bearing nodes (`UnaryOperatorExpression`/`BinaryOperator
// Expression`/`AssignmentExpression`) emit. It also carries three keyword const strings
// (`RefKeyword`/`OutKeyword`/`InKeyword`), the tokens the output visitor emits for the
// `ref`/`out`/`in` modifiers (the `CheckedKeyword`/`UncheckedKeyword` D234 precedent: a const
// string is a value, part of the node's public API surface, so it ports now as a `static
// constexpr const char*`).
//
// `argument_value ::= ( 'ref' | 'out' | 'in' )? expression` (C# grammar 12.6.2.1): an
// `Expression` with a single `[Slot("Expression")] Expression` child (the operand,
// non-nullable in the C# source -- `[Slot("Expression")] public partial Expression Expression`,
// no `?`, so the slot is required) and a scalar `FieldDirection` enum (not a slot). The slot
// kind is `Expression` (the `[Slot]` argument), so the per-node `ExpressionSlot` REUSES the
// already-ported `Slots::Expression` kind (the same kind `UnaryOperatorExpression`
// registered in D231 and `ParenthesizedExpression`/`CheckedExpression`/`UncheckedExpression`
// reused) -- no new `Slots.hpp` constant. The generator emits the const-index
// `SetChildNode(ref field, value, 0)` setter (the single slot is the first and only slot, so
// the flattened index is the constant 0), the `GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` overrides over the one single slot, and the `DoMatch`
// `return other is DirectionExpression o && this.Expression.DoMatch(o.Expression, match)
// && this.FieldDirection == o.FieldDirection`. `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// copies the scalar `FieldDirection`, deep-clones the child through the setter (which
// re-parents), and copies the annotation channel.
//
// The `FieldDirection` enum has NO `Any` member (its members are `None`/`Out`/`Ref`/`In`),
// so the generator's `hasAny` path (which detects an `Any` member by name and emits the
// `== Any || == o.Field` wildcard term) does NOT fire -- the DoMatch term is the plain
// `this.FieldDirection == o.FieldDirection` (the generator's `DoMatchTerm` fall-through for
// a non-`Any` enum scalar). This is the first ported node to exercise that path
// (`UnaryOperatorType`/`BinaryOperatorType`/`AssignmentOperatorType` all declare `Any`).
//
// The `RefKeyword`/`OutKeyword`/`InKeyword` const strings are part of the node's public API
// (the output visitor reads them), so the port carries them as `static constexpr const char*`
// (the D234 `CheckedKeyword`/`UncheckedKeyword` precedent). They are compile-time literals
// (static fields, not instance state), so they are NOT part of `MembersToMatch` (the
// generator iterates instance `IPropertySymbol`s only), so they do not appear in the
// generated `DoMatch`.
//
// C++ name-shadowing crux #1 (the `Expression` property): the C# property is `Expression` of
// type `Expression` (a property named the same as its type). The faithful port names the
// accessor `Expression()`, but a member function named `Expression` SHADOWS the `Expression`
// base type within this class scope (C++ unqualified name lookup finds the member and stops,
// even though it is not a type -- the D224 `Annotation<T>()`-shadows-the-`Annotation`-type
// crux, the D231 `UnaryOperatorExpression` / D233 `ParenthesizedExpression` / D234
// `CheckedExpression` precedent). Every type usage AFTER the `Expression()` getter is
// declared therefore uses the ELABORATED-TYPE-SPECIFIER `class Expression`
// (basic.lookup.elab: an elaborated specifier ignores non-type names and finds the hidden
// class), so the setter parameter, the slot static, the `static_cast`, and the backing field
// all spell the operand type as `class Expression`. The ctor parameter and the getter return
// type precede the getter's declaration, so they use the plain `Expression` (no member
// function is in scope there yet).
//
// C++ name-shadowing crux #2 (the `FieldDirection` property): the C# property is
// `FieldDirection` of type `FieldDirection` -- a property named the same as its enum type,
// the enum equivalent of crux #1. The faithful port names the accessor `FieldDirection()`,
// which SHADOWS the `FieldDirection` enum in this class scope. The elaborated-type-specifier
// for an enum is `enum FieldDirection` (basic.lookup.elab ignores non-type names, so it finds
// the hidden enum), the enum equivalent of `class Expression`; every type usage AFTER the
// `FieldDirection()` getter is declared (the setter parameter, the backing field type) uses
// `enum FieldDirection`. The getter return type and the ctor parameter precede the getter's
// declaration, so they use the plain `FieldDirection` (the member function is not in scope
// there yet -- the ctor is declared before the getter). The backing-field initializer uses
// the fully-qualified enum name (`::ILSpy::...::FieldDirection::None`) because the bare
// `FieldDirection::None` would resolve the unqualified `FieldDirection` to the member
// function (in class scope) and reject `::None` on a non-type. The ctor body assigns the
// backing field directly (`fieldDirection_ = fieldDirection`) rather than calling the setter
// `FieldDirection(fieldDirection)`, because that call is AMBIGUOUS -- it could be the
// setter or a functional cast of the enum (the `Expression(fieldDirection)` shape is
// unambiguous only because `Expression` is an abstract class that cannot be cast-constructed).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_DIRECTIONEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_DIRECTIONEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum FieldDirection` -- the direction of a `ref`/`out`/`in` argument.
// `None` is the zero value (the C# default for an uninitialized `FieldDirection` property --
// no direction, a plain value argument) AND the "no modifier" case. Unlike
// `UnaryOperatorType`/`BinaryOperatorType`/`AssignmentOperatorType`, this enum declares NO
// `Any` member, so the generator's `hasAny` path does not fire and the generated `DoMatch`
// term is the plain `this.FieldDirection == o.FieldDirection` (no `Any`-wildcard). A
// `std::uint8_t`-based `enum class` (the C# `enum FieldDirection` defaults to `int`; the
// underlying width is not load-bearing here, so the default `int`-sized `enum class` is
// faithful).
enum class FieldDirection {
    // No direction (a plain value argument, the default).
    None,
    // `out` parameter modifier.
    Out,
    // `ref` parameter modifier.
    Ref,
    // `in` parameter modifier.
    In
};

// The C# `public sealed partial class DirectionExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The seventh concrete node with `[Slot]` children,
// and the `UnaryOperatorExpression` shape (a single required `Expression` slot + a scalar
// enum) but the first whose scalar enum has no `Any` member.
class DirectionExpression final : public Expression {
public:
    ~DirectionExpression() override = default;

    // The C# `public const string RefKeyword = "ref"` / `OutKeyword = "out"` / `InKeyword =
    // "in"` -- the tokens the output visitor emits for the `ref`/`out`/`in` modifiers
    // (CSharpOutputVisitor.VisitDirectionExpression). Compile-time literals carried as
    // `static constexpr const char*` (static fields, not instance state, so they are not part
    // of `MembersToMatch`/`DoMatch`).
    static constexpr const char* RefKeyword = "ref";
    static constexpr const char* OutKeyword = "out";
    static constexpr const char* InKeyword = "in";

    // The generated empty ctor (the C# `public DirectionExpression()`). `FieldDirection`
    // defaults to `None` (the enum's zero value, the C# default); `Expression` defaults to
    // null (no operand). A null operand violates the required-slot invariant, so a
    // default-constructed node is only valid until `Expression` is set (or until
    // `DoMatch`/`CheckInvariant` observe the missing child).
    DirectionExpression() = default;

    // The generated all-params ctor (the C# `public DirectionExpression(FieldDirection
    // fieldDirection, Expression expression)`). The ctor params are in declaration order
    // (`FieldDirection` is declared before `Expression` in the .cs source), so the scalar
    // precedes the child -- the `AssignmentExpression` D230 faithful-to-generated precedent,
    // not the `UnaryOperatorExpression` D231 `(expression, op)` reordering. The scalar is
    // assigned directly to the backing field (not via the setter) because the setter call
    // `FieldDirection(fieldDirection)` is ambiguous with a functional cast of the enum (the
    // crux #2 note above); the child is set through its setter so the slot machinery
    // re-parents and re-indexes it. The `FieldDirection` parameter type precedes the
    // `FieldDirection()` getter (the ctor is declared before the getter), so the plain
    // `FieldDirection` (the enum) is unshadowed here; the `Expression` parameter type
    // precedes the `Expression()` getter, so the plain `Expression` (the base type) is
    // unshadowed here too. The body is in complete-class context, so the
    // `Expression(expression)` call resolves to the setter declared below.
    DirectionExpression(FieldDirection fieldDirection, Expression* expression)
        : DirectionExpression() {
        fieldDirection_ = fieldDirection;
        Expression(expression);
    }

    // The C# `public FieldDirection FieldDirection { get; set; }` -- a scalar enum (not a
    // `[Slot]`). A settable enum-typed scalar the generator adds to `MembersToMatch` (with
    // the PLAIN equality term, since `FieldDirection` has no `Any` member) and to the ctor
    // params. The return type precedes the getter's own declaration, so the plain
    // `FieldDirection` (the enum) is unshadowed in the getter signature.
    FieldDirection FieldDirection() const { return fieldDirection_; }
    // The setter parameter type uses the elaborated enum specifier `enum FieldDirection`: the
    // `FieldDirection()` getter declared just above shadows the `FieldDirection` enum in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void FieldDirection(enum FieldDirection value) { fieldDirection_ = value; }

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

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable);
    // the kind carries identity only. The `Slots::Expression` kind already exists (it was
    // added for `UnaryOperatorExpression` in D231 and reused by `ParenthesizedExpression` in
    // D233 and `CheckedExpression`/`UncheckedExpression` in D234), so no new `Slots.hpp`
    // constant is needed. The element type uses the elaborated `class Expression` (the
    // `Expression()` accessor shadows the base type in this scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitDirectionExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitDirectionExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitDirectionExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitDirectionExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so
    // `GetChildCount` is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape, with a single
    // case). The `FieldDirection` scalar is NOT a slot, so it does not appear here.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("DirectionExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("DirectionExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("DirectionExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is DirectionExpression o && this.Expression.DoMatch(o.Expression, match)
    // && this.FieldDirection == o.FieldDirection`. `Expression` is a NON-NULLABLE recursive
    // child, so the generator emits the direct `this.Expression.DoMatch(o.Expression, match)`
    // term (NOT `MatchOptional`, which the generator emits only for a nullable recursive
    // child); `FieldDirection` is a settable enum with NO `Any` member, so the generator
    // emits the PLAIN `this.FieldDirection == o.FieldDirection` term (the `DoMatchTerm`
    // fall-through for a non-`Any` enum scalar -- NOT the `== Any || == o.Field` wildcard the
    // `Operator`-bearing nodes emit). The `RefKeyword`/`OutKeyword`/`InKeyword` const strings
    // are static fields, not instance properties, so they are not part of `MembersToMatch`
    // and do not appear here. A type-only mismatch (not a `DirectionExpression`) rejects
    // early.
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
        auto* o = dynamic_cast<DirectionExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && fieldDirection_ == o->fieldDirection_;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the scalar `FieldDirection`, deep-clones
    // the child through the setter (which re-parents and re-indexes via `SetChildNode`), and
    // copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). The print-time `StartLocation`/`EndLocation` are not stored on
    // this node (no own location fields -- the base fields hold the print-time span), so
    // only the scalar + child + annotation channel are copied. The `static_cast` uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in
    // this scope). The child is skipped if absent (`Clone` tolerates a missing child even
    // though the slot is required -- the invariant is enforced by `CheckInvariant`, not by
    // `Clone`).
    DirectionExpression* Clone() const override {
        auto* node = new DirectionExpression();
        node->fieldDirection_ = fieldDirection_;
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field for the `Expression` slot uses the elaborated `class Expression`
    // (the `Expression()` accessor declared above shadows the `Expression` base type in this
    // class scope). A required slot is non-null only by invariant, so the pointer is null
    // until the operand is set. The `FieldDirection` scalar backing field uses the elaborated
    // enum specifier `enum FieldDirection` (the `FieldDirection()` accessor declared above
    // shadows the enum in this class scope); the initializer uses the fully-qualified enum
    // name because the bare `FieldDirection::None` would resolve the unqualified
    // `FieldDirection` to the member function and reject `::None` on a non-type. `None` is
    // the enum's zero value (the C# default).
    class Expression* expression_ = nullptr;
    enum FieldDirection fieldDirection_ = ::ILSpy::Decompiler::CSharp::Syntax::FieldDirection::None;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_DIRECTIONEXPRESSION_HPP
