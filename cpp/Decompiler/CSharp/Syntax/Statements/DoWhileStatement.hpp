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

// Port of the `DoWhileStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/DoWhileStatement.cs (the generated
// `DoWhileStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D257 plan ("the collection-bearing Condition+embedded-statement nodes: ... DoWhileStatement"),
// grouped with `IfElseStatement`/`WhileStatement`.
//
// `do_statement ::= 'do' statement 'while' '(' expression ')' ';'` (C# grammar 13.9.3): a
// `Statement` with two single, REQUIRED (non-nullable) `[Slot]` children -- an `EmbeddedStatement`
// `Statement` (the loop body) and a `Condition` `Expression` (the loop test) -- and NO scalar
// enum. It is the `WhileStatement` shape with the slot ORDER REVERSED: the C# declares
// `EmbeddedStatement` BEFORE `Condition`, so the source declaration order (the `GetMembers()`
// order the generator iterates) puts `EmbeddedStatement` at flattened index 0 and `Condition` at
// flattened index 1 -- the reverse of `WhileStatement`'s `Condition`-0/`EmbeddedStatement`-1.
// The all-params generated ctor, the `DoMatch` term order, and the const-index slot assignment
// ALL follow this source order.
//
// The generator emits two typed slot statics (`EmbeddedStatementSlot` pointing at the shared
// `Slots::EmbeddedStatement` kind, `ConditionSlot` pointing at the shared `Slots::Condition` kind
// -- both already ported, by `WhileStatement` and `ConditionalExpression` respectively, so no new
// `Slots` constant), the const-index `SetChildNode(ref field, value, index)` setters, the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the two single slots,
// and the `DoMatch` `return other is DoWhileStatement o &&
// this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match) &&
// this.Condition.DoMatch(o.Condition, match)`. Both children are NON-NULLABLE recursive, so the
// generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (the
// `MatchRequired` same-class static in the port). `Clone` is inherited in C# (`MemberwiseClone`
// + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the two
// children through the setters (which re-parent) and copies the annotation channel.
//
// HAND-WRITTEN ctor: `DoWhileStatement.cs` declares a convenience ctor
// `public DoWhileStatement(Expression condition, Statement embeddedStatement)` that takes the
// `condition` FIRST (the more natural reading order -- `do ... while (condition)`), the reverse
// of the generated all-params ctor's `(Statement embeddedStatement, Expression condition)`
// declaration order. The port carries BOTH: the generated `(Statement*, Expression*)` ctor (the
// faithful generated signature, declaration order) and the hand-written `(Expression*, Statement*)`
// ctor (the convenience, reversed). `Statement` and `Expression` are sibling `AstNode` subclasses
// (neither derives from the other), so the two 2-arg ctors are NOT implicitly convertible to
// each other and overload cleanly -- `DoWhileStatement(s, e)` selects the generated ctor,
// `DoWhileStatement(e, s)` selects the hand-written one.
//
// NO C++ name-shadowing crux: the accessors are `EmbeddedStatement`/`Condition` (none named
// `Expression` or `Statement`), so the `Expression`/`Statement` base types are unshadowed in
// this class scope and the plain base types are used throughout -- the `IfElseStatement`/
// `WhileStatement` precedent (this same iteration).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_DOWHILESTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_DOWHILESTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class DoWhileStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The `WhileStatement` shape with the slot order reversed
// (`EmbeddedStatement`-0/`Condition`-1).
class DoWhileStatement final : public Statement {
public:
    ~DoWhileStatement() override = default;

    // The generated empty ctor (the C# `public DoWhileStatement()`). Both slots default to null.
    // Null slots violate the required-slot invariant, so a default-constructed node is only valid
    // until the slots are set (or until `DoMatch`/`CheckInvariant` observe the missing children).
    DoWhileStatement() = default;

    // The generated all-params ctor (the C# `public DoWhileStatement(Statement embeddedStatement,
    // Expression condition)`) in SOURCE DECLARATION ORDER (`EmbeddedStatement` is declared before
    // `Condition`, so it is the first param). `RequiredConstructorPrefixLength` is 2 (both
    // required) and there is no collection, so the single full ctor IS the required-prefix ctor.
    // Delegated to the empty ctor then the setters so the slot machinery re-parents and re-indexes
    // the children.
    DoWhileStatement(Statement* embeddedStatement, Expression* condition)
        : DoWhileStatement() {
        EmbeddedStatement(embeddedStatement);
        Condition(condition);
    }

    // The hand-written convenience ctor (the C# `public DoWhileStatement(Expression condition,
    // Statement embeddedStatement)`) with the PARAM ORDER REVERSED relative to the generated
    // ctor -- `condition` first (the natural `do ... while (condition)` reading order). The C#
    // body assigns `this.Condition = condition; this.EmbeddedStatement = embeddedStatement;`, so
    // the port delegates to the empty ctor then the setters in the same assignment order (the
    // setter call order does not affect the flattened indices, which are fixed by the const-index
    // `SetChildNode`). `Statement*` and `Expression*` are sibling types (neither derives from the
    // other), so this `(Expression*, Statement*)` ctor overloads cleanly with the generated
    // `(Statement*, Expression*)` ctor above.
    DoWhileStatement(Expression* condition, Statement* embeddedStatement)
        : DoWhileStatement() {
        Condition(condition);
        EmbeddedStatement(embeddedStatement);
    }

    // The C# `[Slot("EmbeddedStatement")] Statement EmbeddedStatement` -- a single, REQUIRED
    // (non-nullable) `Statement` child at flattened index 0 (declared first). The generator emits
    // the const-index `SetChildNode(ref field, value, 0)` setter. No name shadowing (the
    // `EmbeddedStatement()` accessor does not collide with the `Statement` base type).
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value, 0);
    }

    // The C# `[Slot("Condition")] Expression Condition` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 1 (declared second). The generator emits the
    // const-index `SetChildNode(ref field, value, 1)` setter. No name shadowing (the `Condition()`
    // accessor does not collide with the `Expression` base type).
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. Both kinds are
    // already ported (`Slots::EmbeddedStatement` by `WhileStatement`, `Slots::Condition` by
    // `ConditionalExpression`), so no new `Slots` constant. The `IsOptional` flag is false for
    // each (both required). No name shadowing (no member is named `Expression`/`Statement`), so
    // the element types are the plain base types.
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, false};

    // The C# `public const string DoKeyword = "do"` / `public const string WhileKeyword = "while"`
    // (the keyword tokens the output visitor emits) -- ports as `static constexpr const char*`
    // (static fields, not instance state), so the generator's `MembersToMatch` (which iterates
    // only instance `IPropertySymbol`s) excludes them from the `DoMatch` (the `WhileStatement`
    // precedent applied to the do/while pair).
    static constexpr const char* DoKeyword = "do";
    static constexpr const char* WhileKeyword = "while";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitDoWhileStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitDoWhileStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitDoWhileStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitDoWhileStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`EmbeddedStatement`/`Condition` -- the source
    // declaration order, the reverse of `WhileStatement`); no collection, so `GetChildCount` is
    // the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return embeddedStatement_;
            case 1: return condition_;
            default: throw std::out_of_range("DoWhileStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(embeddedStatement_, static_cast<Statement*>(value), 0); break;
            case 1: SetChildNode(condition_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("DoWhileStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &EmbeddedStatementSlot;
            case 1: return &ConditionSlot;
            default: throw std::out_of_range("DoWhileStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is DoWhileStatement o && this.EmbeddedStatement.DoMatch(o.EmbeddedStatement,
    // match) && this.Condition.DoMatch(o.Condition, match)` -- the terms in SOURCE DECLARATION
    // order (`EmbeddedStatement` before `Condition`), the reverse of `WhileStatement`. Both
    // children are NON-NULLABLE recursive, so the generator emits the direct
    // `this.{member}.DoMatch(o.{member}, match)` term for each (NOT `MatchOptional`, which the
    // generator emits only for a nullable recursive child); there is no scalar enum, so there
    // is no `Any`-wildcard term. A type-only mismatch (not a `DoWhileStatement`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `Statement*`/`Expression*`. `MatchRequired` guards a missing
    // pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (both children set) the behavior is identical to the
    // C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<DoWhileStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(embeddedStatement_, o->embeddedStatement_, match)
            && MatchRequired(condition_, o->condition_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the two children through the setters (which re-parent and
    // re-index via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not stored
    // on this node (no own location fields), so only the children + annotation channel are
    // copied. Each child is skipped if absent (`Clone` tolerates a missing child even though the
    // slots are required -- the invariant is enforced by `CheckInvariant`, not by `Clone`).
    // `Statement::Clone()` returns `Statement*` and `Expression::Clone()` returns `Expression*`
    // (the covariant overrides), which the setters accept directly. No name shadowing, so no
    // elaborated-type-specifier casts are needed.
    DoWhileStatement* Clone() const override {
        auto* node = new DoWhileStatement();
        node->CloneAnnotationsFrom(*this);
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(static_cast<Statement*>(embeddedStatement_->Clone()));
        if (condition_ != nullptr)
            node->Condition(static_cast<Expression*>(condition_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is null
    // until the child is set. The declaration order (`EmbeddedStatement` first) matches the slot
    // order for clarity. No name shadowing (no member is named `Expression`/`Statement`), so the
    // field types are the plain base types.
    Statement* embeddedStatement_ = nullptr;
    Expression* condition_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_DOWHILESTATEMENT_HPP
