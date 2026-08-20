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

// Port of the `WhileStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/WhileStatement.cs (the generated
// `WhileStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D257 plan ("the collection-bearing Condition+embedded-statement nodes: ... WhileStatement ...
// DoWhileStatement"), grouped with `IfElseStatement`.
//
// `while_statement ::= 'while' '(' expression ')' statement` (C# grammar 13.9.2): a `Statement`
// with two single, REQUIRED (non-nullable) `[Slot]` children -- a `Condition` `Expression` (the
// loop test) and an `EmbeddedStatement` `Statement` (the loop body) -- and NO scalar enum. The
// generator emits two typed slot statics (`ConditionSlot` pointing at the shared `Slots::Condition`
// kind, `EmbeddedStatementSlot` pointing at the new `Slots::EmbeddedStatement` kind), the
// const-index `SetChildNode(ref field, value, index)` setters (no collection precedes either
// slot, so each flattened index is the constant slot position 0/1), the `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the two single slots, and the
// `DoMatch` `return other is WhileStatement o && this.Condition.DoMatch(o.Condition, match) &&
// this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. Both children are NON-NULLABLE
// recursive, so the generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term
// for each (the `MatchRequired` same-class static in the port); there is no scalar enum, so
// there is no `Any`-wildcard term. `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the two
// children through the setters (which re-parent) and copies the annotation channel.
//
// NO C++ name-shadowing crux: the accessors are `Condition`/`EmbeddedStatement` (none named
// `Expression` or `Statement`), so the `Expression`/`Statement` base types are unshadowed in
// this class scope and the plain base types are used throughout -- the `IfElseStatement`
// precedent (this same iteration). The `Condition()` accessor does not collide with the
// `Expression` base type; the `EmbeddedStatement()` accessor does not collide with the
// `Statement` base type (a member named `EmbeddedStatement` is not the name `Statement`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_WHILESTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_WHILESTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class WhileStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The simplest Condition+EmbeddedStatement shape (both required, no
// nullable slot, no scalar).
class WhileStatement final : public Statement {
public:
    ~WhileStatement() override = default;

    // The generated empty ctor (the C# `public WhileStatement()`). Both slots default to null.
    // Null slots violate the required-slot invariant, so a default-constructed node is only valid
    // until the slots are set (or until `DoMatch`/`CheckInvariant` observe the missing children).
    WhileStatement() = default;

    // The generated all-params ctor (the C# `public WhileStatement(Expression condition,
    // Statement embeddedStatement)`). `RequiredConstructorPrefixLength` is 2 (both required) and
    // there is no collection, so the single full ctor IS the required-prefix ctor (no shorter
    // prefix ctor and no `params` overload). Delegated to the empty ctor then the setters so the
    // slot machinery re-parents and re-indexes the children.
    WhileStatement(Expression* condition, Statement* embeddedStatement)
        : WhileStatement() {
        Condition(condition);
        EmbeddedStatement(embeddedStatement);
    }

    // The C# `[Slot("Condition")] Expression Condition` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter. No name shadowing (the `Condition()` accessor
    // does not collide with the `Expression` base type).
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value, 0);
    }

    // The C# `[Slot("EmbeddedStatement")] Statement EmbeddedStatement` -- a single, REQUIRED
    // (non-nullable) `Statement` child at flattened index 1. The generator emits the const-index
    // `SetChildNode(ref field, value, 1)` setter. No name shadowing (the `EmbeddedStatement()`
    // accessor does not collide with the `Statement` base type -- a member named
    // `EmbeddedStatement` is not the name `Statement`).
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. The
    // `IsOptional` flag is false for each (both required); the kind carries identity only.
    // `Condition` is already ported (by `ConditionalExpression`); `EmbeddedStatement` is the new
    // kind added with this node. No name shadowing (no member is named `Expression`/`Statement`),
    // so the element types are the plain base types.
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, false};
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};

    // The C# `public const string WhileKeyword = "while"` (the keyword token the output visitor
    // emits) -- ports as a `static constexpr const char*` (a static field, not instance state),
    // so the generator's `MembersToMatch` (which iterates only instance `IPropertySymbol`s)
    // excludes it from the `DoMatch` (the `BreakStatement` D254 / `IfElseStatement` precedent
    // applied to a two-slot statement).
    static constexpr const char* WhileKeyword = "while";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitWhileStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitWhileStatement(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitWhileStatement`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitWhileStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`Condition`/`EmbeddedStatement`); no collection,
    // so `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return condition_;
            case 1: return embeddedStatement_;
            default: throw std::out_of_range("WhileStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(condition_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(embeddedStatement_, static_cast<Statement*>(value), 1); break;
            default: throw std::out_of_range("WhileStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ConditionSlot;
            case 1: return &EmbeddedStatementSlot;
            default: throw std::out_of_range("WhileStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is WhileStatement o && this.Condition.DoMatch(o.Condition, match) &&
    // this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. Both children are
    // NON-NULLABLE recursive, so the generator emits the direct `this.{member}.DoMatch(o.{member},
    // match)` term for each (NOT `MatchOptional`, which the generator emits only for a nullable
    // recursive child); there is no scalar enum, so there is no `Any`-wildcard term. A type-only
    // mismatch (not a `WhileStatement`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `Expression*`/`Statement*`. `MatchRequired` guards a missing
    // pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (both children set) the behavior is identical to the
    // C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<WhileStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(condition_, o->condition_, match)
            && MatchRequired(embeddedStatement_, o->embeddedStatement_, match);
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
    // `Expression::Clone()` returns `Expression*` and `Statement::Clone()` returns `Statement*`
    // (the covariant overrides), which the setters accept directly. No name shadowing, so no
    // elaborated-type-specifier casts are needed.
    WhileStatement* Clone() const override {
        auto* node = new WhileStatement();
        node->CloneAnnotationsFrom(*this);
        if (condition_ != nullptr)
            node->Condition(static_cast<Expression*>(condition_->Clone()));
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(static_cast<Statement*>(embeddedStatement_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. No name shadowing (no member is named `Expression`/
    // `Statement`), so the field types are the plain base types.
    Expression* condition_ = nullptr;
    Statement* embeddedStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_WHILESTATEMENT_HPP
