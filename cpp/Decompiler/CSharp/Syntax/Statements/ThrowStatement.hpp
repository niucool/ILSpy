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

// Port of the `ThrowStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/ThrowStatement.cs (the generated
// `ThrowStatement.g.cs` + the hand-written partial). The sibling of `ReturnStatement` (the
// next in-order Phase-5 piece per the D254 plan) -- a `throw_statement ::= 'throw' expression?
// ';'` (C# grammar 13.10.6), the `throw` keyword const string plus a single NULLABLE
// `Expression` child (the exception being thrown, absent for a bare rethrow `throw;` in a
// `catch`). It is structurally identical to `ReturnStatement` (the same single nullable
// `Expression` slot at flattened index 0, no scalar); the two are disjoint concrete types
// distinguished by the pattern matcher's `other is ThrowStatement` gate.

// The hand-written partial declares only the `ThrowKeyword` const string and the `Expression`
// slot property, no ctors, no helpers. The generator emits the `ExpressionSlot` slot static
// pointing at the shared `Slots::Expression` kind (already ported by `UnaryOperatorExpression`
// -- no new `Slots` constant), the const-index `SetChildNode(ref field, value, 0)` setter, the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single slot,
// and the `DoMatch` `return other is ThrowStatement o && MatchOptional(this.Expression,
// o.Expression, match)` (a nullable recursive child -> `MatchOptional`, no scalar term -- the
// const string is excluded from `MembersToMatch`). `Clone` is inherited in C# (`MemberwiseClone`
// + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the child
// through the setter (when present) and copies the annotation channel.
//
// C++ name-shadowing crux (the `ReturnStatement` / `UnaryOperatorExpression` D231 precedent):
// the C# property is `Expression` of type `Expression`. The faithful port names the accessor
// `Expression()`, which SHADOWS the `Expression` class in this class scope (C++ unqualified
// name lookup finds the member and stops, even though it is not a type). Every type usage
// AFTER the `Expression()` getter is declared therefore uses the elaborated-type-specifier
// `class Expression`; the ctor parameter and the getter return type precede the getter so they
// use the plain `Expression`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_THROWSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_THROWSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ThrowStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. Structurally identical to `ReturnStatement` (a single NULLABLE
// `Expression` slot); the two are disjoint concrete types.
class ThrowStatement final : public Statement {
public:
    ~ThrowStatement() override = default;

    // The generated empty ctor (the C# `public ThrowStatement()`). `Expression` defaults to
    // null (no thrown exception -- a bare rethrow `throw;`). A null operand is valid here
    // (the slot is nullable), so a default-constructed node is invariant-valid.
    ThrowStatement() = default;

    // The generated all-params ctor (the C# `public ThrowStatement(Expression? expression)`),
    // the structural twin of `ReturnStatement`'s. `explicit` because a single-argument ctor
    // is a converting ctor by default (the `ReturnStatement` / `InvocationExpression` D248
    // precedent). The parameter type precedes the `Expression()` accessor declaration, so the
    // plain `Expression` (the base type) is unshadowed here; the body resolves the
    // `Expression(expression)` call to the setter declared below.
    explicit ThrowStatement(Expression* expression)
        : ThrowStatement() {
        Expression(expression);
    }

    // The C# `[Slot("Expression")] Expression? Expression` -- a single, NULLABLE `Expression`
    // child at flattened index 0. The const-index `SetChildNode(ref field, value, 0)` setter
    // (no collection precedes this slot).
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope.
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is true (the slot is nullable). `Slots::Expression` is already ported
    // (by `UnaryOperatorExpression`), so no new `Slots` constant. The element type uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in this
    // scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, true};

    // The C# `public const string ThrowKeyword = "throw"` (the `throw` keyword token the
    // output visitor emits) -- ports as a `static constexpr const char*` (a static field, not
    // instance state), so the generator's `MembersToMatch` excludes it from the `DoMatch` (the
    // `ReturnStatement.ReturnKeyword` / `BreakStatement.BreakKeyword` D254 precedent).
    static constexpr const char* ThrowKeyword = "throw";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitThrowStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitThrowStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so `GetChildCount` is
    // the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("ThrowStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("ThrowStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("ThrowStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ThrowStatement o && MatchOptional(this.Expression, o.Expression, match)`.
    // A nullable recursive child -> `MatchOptional` (both absent, or both present and the
    // pattern's `DoMatch` decides); no scalar term (the `ThrowKeyword` const string is a
    // static field, excluded from `MembersToMatch`). A type-only mismatch (not a
    // `ThrowStatement`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ThrowStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Expression` child deep-cloned through the setter (when present). No own location fields
    // (it does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are
    // not copied (the `ReturnStatement` / `BreakStatement` D254 no-location-copy precedent).
    ThrowStatement* Clone() const override {
        auto* node = new ThrowStatement();
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

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_THROWSTATEMENT_HPP
