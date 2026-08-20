// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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

// Port of the `LockStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/LockStatement.cs (the generated
// `LockStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the D260
// plan ("the remaining collection/multi-slot statements: ... LockStatement ...").
//
// `lock_statement ::= 'lock' '(' expression ')' statement` (C# grammar 13.13): a `Statement`
// with two single, REQUIRED (non-nullable) `[Slot]` children -- an `Expression` (the lock object)
// and an `EmbeddedStatement` `Statement` (the lock body) -- and NO scalar enum. It is the
// `WhileStatement` D258 two-required-single-slot shape (the Condition+EmbeddedStatement loop
// shape) with the loop test slot renamed `Expression` -- the simplest remaining statement, and
// the cleanest re-port of an already-established shape.
//
// The hand-written partial declares only the two slot properties, no ctors, no helpers. The
// generator emits two typed slot statics (`ExpressionSlot` pointing at the shared
// `Slots::Expression` kind -- already ported by `UnaryOperatorExpression` -- and
// `EmbeddedStatementSlot` pointing at the shared `Slots::EmbeddedStatement` kind -- already
// ported by `WhileStatement`), the const-index `SetChildNode(ref field, value, index)` setters
// (no collection precedes either slot, so each flattened index is the constant slot position
// 0/1), the `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the two
// single slots, and the `DoMatch` `return other is LockStatement o && this.Expression.DoMatch(
// o.Expression, match) && this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. Both
// children are NON-NULLABLE recursive, so the generator emits the direct
// `this.{member}.DoMatch(o.{member}, match)` term for each (the `MatchRequired` same-class
// static in the port); there is no scalar enum, so there is no `Any`-wildcard term. `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the two children through the setters (which re-parent) and
// copies the annotation channel.
//
// C++ name-shadowing crux (the `UnaryOperatorExpression` D231 / `ExpressionStatement` D255
// precedent): the C# property is `Expression` of type `Expression` (a property named the same
// as its type). The faithful port names the accessor `Expression()`, which SHADOWS the
// `Expression` class in this class scope (C++ unqualified name lookup finds the member and
// stops, even though it is not a type). Every type usage AFTER the `Expression()` getter is
// declared therefore uses the elaborated-type-specifier `class Expression`; the ctor parameter
// and the getter return type precede the getter so they use the plain `Expression`. The
// `EmbeddedStatement()` accessor does NOT shadow the `Statement` base type (a member named
// `EmbeddedStatement` is not the name `Statement`), so the plain `Statement` is used throughout
// the `EmbeddedStatement` slot -- the `WhileStatement` D258 no-shadowing-on-EmbeddedStatement
// precedent.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LOCKSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LOCKSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class LockStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The `WhileStatement` D258 two-required-single-slot shape with the loop
// test slot renamed `Expression`.
class LockStatement final : public Statement {
public:
    ~LockStatement() override = default;

    // The generated empty ctor (the C# `public LockStatement()`). Both slots default to null.
    // Null slots violate the required-slot invariant, so a default-constructed node is only
    // valid until the slots are set (or until `DoMatch`/`CheckInvariant` observe the missing
    // children) -- the `WhileStatement` D258 required-slot behavior.
    LockStatement() = default;

    // The generated all-params ctor (the C# `public LockStatement(Expression expression,
    // Statement embeddedStatement)`). `RequiredConstructorPrefixLength` is 2 (both required)
    // and there is no collection, so the single full ctor IS the required-prefix ctor (no
    // shorter prefix ctor and no `params` overload). The parameter types precede the
    // `Expression()` accessor declaration, so the plain `Expression`/`Statement` (the base
    // types) are unshadowed here; the body resolves the `Expression(expression)` call to the
    // setter declared below.
    LockStatement(Expression* expression, Statement* embeddedStatement)
        : LockStatement() {
        Expression(expression);
        EmbeddedStatement(embeddedStatement);
    }

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes this slot). The C#
    // getter returns the backing field null-forgiving (`field!`) because the slot is required;
    // the port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type). The getter return type precedes the getter name so it uses the plain `Expression`.
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The C# `[Slot("EmbeddedStatement")] Statement EmbeddedStatement` -- a single, REQUIRED
    // (non-nullable) `Statement` child at flattened index 1. The generator emits the
    // const-index `SetChildNode(ref field, value, 1)` setter. No name shadowing (the
    // `EmbeddedStatement()` accessor does not collide with the `Statement` base type -- a
    // member named `EmbeddedStatement` is not the name `Statement`), so the plain `Statement`
    // is used throughout.
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. The
    // `IsOptional` flag is false for each (both required); the kind carries identity only.
    // `Slots::Expression` is already ported (by `UnaryOperatorExpression`); `Slots::EmbeddedStatement`
    // is already ported (by `WhileStatement`), so no new `Slots` constant. The `ExpressionSlot`
    // element type uses the elaborated `class Expression` (the `Expression()` accessor shadows
    // the base type in this scope); the `EmbeddedStatementSlot` element type is the plain
    // `Statement` (no shadowing).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};

    // The C# `public const string LockKeyword = "lock"` (the keyword token the output visitor
    // emits) -- ports as a `static constexpr const char*` (a static field, not instance
    // state), so the generator's `MembersToMatch` (which iterates only instance
    // `IPropertySymbol`s) excludes it from the `DoMatch` (the `BreakStatement` D254 /
    // `IfElseStatement` D258 precedent applied to the lock statement).
    static constexpr const char* LockKeyword = "lock";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitLockStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitLockStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitLockStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitLockStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`Expression`/`EmbeddedStatement`); no
    // collection, so `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/
    // `GetChildSlotInfo` are a flat index switch (the generator's `WriteReturnDispatchSwitch`
    // shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            case 1: return embeddedStatement_;
            default: throw std::out_of_range("LockStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            case 1: SetChildNode(embeddedStatement_, static_cast<Statement*>(value), 1); break;
            default: throw std::out_of_range("LockStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            case 1: return &EmbeddedStatementSlot;
            default: throw std::out_of_range("LockStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is LockStatement o && this.Expression.DoMatch(o.Expression, match) &&
    // this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. Both children are
    // NON-NULLABLE recursive, so the generator emits the direct `this.{member}.DoMatch(
    // o.{member}, match)` term for each (NOT `MatchOptional`, which the generator emits only
    // for a nullable recursive child); there is no scalar enum, so there is no `Any`-wildcard
    // term. A type-only mismatch (not a `LockStatement`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `Expression*`/`Statement*`. `MatchRequired` guards a missing
    // pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)`
    // which returns false. For well-formed nodes (both children set) the behavior is identical
    // to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<LockStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && MatchRequired(embeddedStatement_, o->embeddedStatement_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the two children through the setters (which re-parent and
    // re-index via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not stored
    // on this node (no own location fields), so only the children + annotation channel are
    // copied. Each child is skipped if absent (`Clone` tolerates a missing child even though
    // the slots are required -- the invariant is enforced by `CheckInvariant`, not by `Clone`).
    // `Expression::Clone()` returns `Expression*` and `Statement::Clone()` returns `Statement*`
    // (the covariant overrides), which the setters accept directly. The `Expression` setter
    // takes the elaborated `class Expression*` (the `Expression()` accessor shadows the base
    // type in this scope), so the deep-clone cast uses the elaborated specifier; the
    // `EmbeddedStatement` setter takes the plain `Statement*` (no shadowing).
    LockStatement* Clone() const override {
        auto* node = new LockStatement();
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(static_cast<Statement*>(embeddedStatement_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. The `expression_` field uses the elaborated `class
    // Expression` (the `Expression()` accessor declared above shadows the `Expression` base
    // type in this class scope); the `embeddedStatement_` field is the plain `Statement` (no
    // shadowing).
    class Expression* expression_ = nullptr;
    Statement* embeddedStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LOCKSTATEMENT_HPP
