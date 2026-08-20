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

// Port of the `ReturnStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/ReturnStatement.cs (the generated
// `ReturnStatement.g.cs` + the hand-written partial). The first slot-bearing C# AST statement
// node (PORT_PLAN.md section 5.2 / decision D1: port the generated *output* by hand) -- the next
// in-order Phase-5 piece per the D254 plan ("the slot-bearing statements: ReturnStatement with
// a nullable Expression ..."). It is the first ported node with a single NULLABLE `[Slot]`
// child and NO scalar (the simplest nullable-single-slot shape): a `return_statement ::=
// 'return' expression? ';'` (C# grammar 13.10.5), the `return` keyword const string plus a
// single nullable `Expression` child (the value being returned, absent for a bare `return;`).
//
// The hand-written partial declares only the `ReturnKeyword` const string and the `Expression`
// slot property, no ctors, no helpers. The generator emits one typed `CSharpSlotInfo<Expression>`
// slot static (`ExpressionSlot`) pointing at the shared `Slots::Expression` kind (already
// ported by `UnaryOperatorExpression` -- no new `Slots` constant), the const-index
// `SetChildNode(ref field, value, 0)` setter (the single slot is the first and only slot, no
// collection precedes it), the `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides
// over the one single slot, and the `DoMatch` `return other is ReturnStatement o &&
// MatchOptional(this.Expression, o.Expression, match)` -- a nullable recursive child, so the
// generator emits `MatchOptional` (both absent, or both present and the pattern's `DoMatch`
// decides), with NO scalar term (the const string is a static field, not instance state, so the
// generator's `MembersToMatch` excludes it). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the child
// through the setter (when present) and copies the annotation channel.
//
// C++ name-shadowing crux (the `UnaryOperatorExpression` D231 precedent): the C# property is
// `Expression` of type `Expression` (a property named the same as its type -- legal in C#,
// which keeps property and type names in separate spaces). The faithful port names the accessor
// `Expression()`, but a member function named `Expression` SHADOWS the `Expression` class in this
// class scope (C++ unqualified name lookup finds the member and stops, even though it is not a
// type -- the D224 `Annotation<T>()` crux). Every type usage AFTER the `Expression()` getter is
// declared therefore uses the elaborated-type-specifier `class Expression` (basic.lookup.elab:
// an elaborated specifier ignores non-type names and finds the hidden class), so the setter
// parameter, the slot static, the `static_cast`s, and the backing field spell the operand type
// as `class Expression`. The ctor parameter and the getter return type precede the getter's
// declaration, so they use the plain `Expression` (no member function is in scope there yet).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_RETURNSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_RETURNSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ReturnStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The first concrete statement node with a `[Slot]` child, and the first
// with a single NULLABLE slot (the simplest nullable-single-slot shape).
class ReturnStatement final : public Statement {
public:
    ~ReturnStatement() override = default;

    // The generated empty ctor (the C# `public ReturnStatement()`). `Expression` defaults to
    // null (no returned value -- a bare `return;`). A null operand is valid here (the slot is
    // nullable), so a default-constructed node is invariant-valid (unlike a required-slot node).
    ReturnStatement() = default;

    // The generated all-params ctor (the C# `public ReturnStatement(Expression? expression)`);
    // the `Expression` slot is nullable so the required-prefix length is 0 and this single-arg
    // form IS the full all-params ctor (no shorter prefix ctor and no params overload since
    // there is no collection). `explicit` because a single-argument ctor is a converting ctor
    // by default (the `TypeReferenceExpression` D248 / `InvocationExpression` D248 precedent).
    // The parameter type precedes the `Expression()` accessor declaration, so the plain
    // `Expression` (the base type) is unshadowed here; the body is in complete-class context,
    // so the `Expression(expression)` call resolves to the setter declared below.
    explicit ReturnStatement(Expression* expression)
        : ReturnStatement() {
        Expression(expression);
    }

    // The C# `[Slot("Expression")] Expression? Expression` -- a single, NULLABLE `Expression`
    // child at flattened index 0. The generator emits the const-index `SetChildNode(ref field,
    // value, 0)` setter (the single slot is the first and only slot, no collection precedes
    // it), so the index is assigned directly and the parent's indices stay valid by construction.
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is true (the slot is nullable -- the C# property is `Expression?`); the
    // kind carries identity only. `Slots::Expression` is already ported (by
    // `UnaryOperatorExpression`), so no new `Slots` constant. The element type uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in this
    // scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, true};

    // The C# `public const string ReturnKeyword = "return"` (the `return` keyword token the
    // output visitor emits) -- ports as a `static constexpr const char*` (a static field, not
    // instance state), so the generator's `MembersToMatch` (which iterates only instance
    // `IPropertySymbol`s) excludes it from the `DoMatch` (the `BreakStatement.BreakKeyword`
    // D254 / `CheckedExpression.CheckedKeyword` D234 precedent applied to a slot-bearing
    // statement).
    static constexpr const char* ReturnKeyword = "return";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitReturnStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitReturnStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitReturnStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitReturnStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so `GetChildCount` is
    // the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with a single case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("ReturnStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("ReturnStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("ReturnStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ReturnStatement o && MatchOptional(this.Expression, o.Expression, match)`.
    // `Expression` is a NULLABLE recursive child, so the generator emits `MatchOptional` (both
    // absent, or both present and the pattern's `DoMatch` decides); there is no scalar term
    // (the `ReturnKeyword` const string is a static field, excluded from `MembersToMatch`). A
    // type-only mismatch (not a `ReturnStatement`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ReturnStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Expression` child deep-cloned through the setter (when present, which re-parents and
    // re-indexes via `SetChildNode`). No own location fields (it does not derive
    // `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `BreakStatement` D254 / `ArraySpecifier` D239 no-location-copy precedent). The
    // `static_cast` uses the elaborated `class Expression` (the `Expression()` accessor
    // shadows the base type in this scope).
    ReturnStatement* Clone() const override {
        auto* node = new ReturnStatement();
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field uses the elaborated `class Expression` (the `Expression()` accessor
    // declared above shadows the `Expression` base type in this class scope). A nullable slot
    // is null until the value is set.
    class Expression* expression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_RETURNSTATEMENT_HPP
