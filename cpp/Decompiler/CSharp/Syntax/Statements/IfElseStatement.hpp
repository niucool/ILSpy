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

// Port of the `IfElseStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/IfElseStatement.cs (the generated
// `IfElseStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D257 plan ("the collection-bearing Condition+embedded-statement nodes: IfElseStatement/
// WhileStatement/DoWhileStatement"). (The D257 "collection-bearing" label is loose -- these
// nodes have NO collection slots; they are single-slot statements each carrying a `Condition`
// `Expression` plus one or two embedded `Statement` children.)
//
// `if_statement ::= 'if' '(' expression ')' statement ( 'else' statement )?` (C# grammar
// 13.8.2): a `Statement` with three single `[Slot]` children -- a REQUIRED `Expression`
// `Condition` (the test), a REQUIRED `Statement` `TrueStatement` (the `then` branch), and a
// NULLABLE `Statement?` `FalseStatement` (the `else` branch, absent for a bare `if`) -- and NO
// scalar enum. The generator emits three typed slot statics (`ConditionSlot` pointing at the
// shared `Slots::Condition` kind, `TrueStatementSlot` pointing at the new `Slots::TrueStatement`
// kind, `FalseStatementSlot` pointing at the new `Slots::FalseStatement` kind), the const-index
// `SetChildNode(ref field, value, index)` setters (no collection precedes any slot, so each
// flattened index is the constant slot position 0/1/2), the `GetChildCount`/`GetChild`/
// `SetChild`/`GetChildSlotInfo` overrides over the three single slots, and the `DoMatch` `return
// other is IfElseStatement o && this.Condition.DoMatch(o.Condition, match) &&
// this.TrueStatement.DoMatch(o.TrueStatement, match) && MatchOptional(this.FalseStatement,
// o.FalseStatement, match)`. `Condition`/`TrueStatement` are NON-NULLABLE recursive, so the
// generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (the
// `MatchRequired` same-class static in the port); `FalseStatement` is NULLABLE recursive, so the
// generator emits `MatchOptional` (both absent, or both present and the pattern's `DoMatch`
// decides). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): deep-clones the three children through the setters (which
// re-parent) and copies the annotation channel.
//
// NO C++ name-shadowing crux: the accessors are `Condition`/`TrueStatement`/`FalseStatement`
// (none named `Expression` or `Statement`), so the `Expression`/`Statement` base types are
// unshadowed in this class scope and the plain base types are used throughout -- the
// `ConditionalExpression` D232 / `BlockStatement` D256 differently-named-property precedent.
// The `Condition()` accessor does not collide with the `Expression` base type; the
// `TrueStatement()`/`FalseStatement()` accessors do not collide with the `Statement` base type
// (a member named `TrueStatement` is not the name `Statement`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_IFELSESTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_IFELSESTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class IfElseStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The first ported statement node with MORE THAN ONE required child slot
// and the first to combine required + nullable single slots (2 required `Expression`/`Statement`
// + 1 nullable `Statement`).
class IfElseStatement final : public Statement {
public:
    ~IfElseStatement() override = default;

    // The generated empty ctor (the C# `public IfElseStatement()`). All three slots default to
    // null. The two required slots (`Condition`/`TrueStatement`) are null until set, so a
    // default-constructed node is only valid until the slots are set (or until `DoMatch`/
    // `CheckInvariant` observe the missing children); the nullable `FalseStatement` may stay
    // null (an `if` without `else`).
    IfElseStatement() = default;

    // The generated required-prefix ctor (the C# `public IfElseStatement(Expression condition,
    // Statement trueStatement)`). `RequiredConstructorPrefixLength` is 2 (`Condition` +
    // `TrueStatement`, both required); the prefix-through-the-last-required is exactly these two
    // (the `FalseStatement` optional slot trails them). Delegated to the empty ctor then the
    // setters so the slot machinery re-parents and re-indexes the children.
    IfElseStatement(Expression* condition, Statement* trueStatement)
        : IfElseStatement() {
        Condition(condition);
        TrueStatement(trueStatement);
    }

    // The generated all-params ctor (the C# `public IfElseStatement(Expression condition,
    // Statement trueStatement, Statement? falseStatement)`). The full set -- the nullable
    // `FalseStatement` (absent for a bare `if`).
    IfElseStatement(Expression* condition, Statement* trueStatement,
                     Statement* falseStatement)
        : IfElseStatement() {
        Condition(condition);
        TrueStatement(trueStatement);
        FalseStatement(falseStatement);
    }

    // The C# `[Slot("Condition")] Expression Condition` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). The C# getter
    // returns the backing field null-forgiving (`field!`) because the slot is required; the
    // port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type). No name shadowing (the `Condition()` accessor does not collide with the `Expression`
    // base type).
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value, 0);
    }

    // The C# `[Slot("TrueStatement")] Statement TrueStatement` -- a single, REQUIRED
    // (non-nullable) `Statement` child at flattened index 1. The generator emits the const-index
    // `SetChildNode(ref field, value, 1)` setter. No name shadowing (the `TrueStatement()`
    // accessor does not collide with the `Statement` base type -- a member named `TrueStatement`
    // is not the name `Statement`).
    Statement* TrueStatement() const { return trueStatement_; }
    void TrueStatement(Statement* value) {
        SetChildNode(trueStatement_, value, 1);
    }

    // The C# `[Slot("FalseStatement")] Statement? FalseStatement` -- a single, NULLABLE
    // `Statement` child at flattened index 2. The generator emits the const-index
    // `SetChildNode(ref field, value, 2)` setter. A null `FalseStatement` is an `if` without
    // `else`; the slot is optional, so a node with no `else` branch is invariant-valid.
    Statement* FalseStatement() const { return falseStatement_; }
    void FalseStatement(Statement* value) {
        SetChildNode(falseStatement_, value, 2);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. The
    // `IsOptional` flag is false for `Condition`/`TrueStatement` (required) and true for
    // `FalseStatement` (nullable); the kind carries identity only. `Condition` is already
    // ported (by `ConditionalExpression`); `TrueStatement`/`FalseStatement` are the new kinds
    // added with this node. No name shadowing (no member is named `Expression`/`Statement`), so
    // the element types are the plain base types.
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, false};
    static inline const CSharpSlotInfoT<Statement> TrueStatementSlot{"TrueStatement", false, &Slots::TrueStatement, false};
    static inline const CSharpSlotInfoT<Statement> FalseStatementSlot{"FalseStatement", false, &Slots::FalseStatement, true};

    // The C# `public const string IfKeyword = "if"` / `public const string ElseKeyword = "else"`
    // (the keyword tokens the output visitor emits) -- ports as `static constexpr const char*`
    // (static fields, not instance state), so the generator's `MembersToMatch` (which iterates
    // only instance `IPropertySymbol`s) excludes them from the `DoMatch` (the `BreakStatement`
    // D254 / `GotoStatement` D257 precedent applied to a multi-slot statement).
    static constexpr const char* IfKeyword = "if";
    static constexpr const char* ElseKeyword = "else";

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitIfElseStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitIfElseStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Three single slots at flattened indices 0/1/2 (`Condition`/`TrueStatement`/`FalseStatement`);
    // no collection, so `GetChildCount` is the constant 3 and `GetChild`/`SetChild`/
    // `GetChildSlotInfo` are a flat index switch (the generator's `WriteReturnDispatchSwitch`
    // shape, with three cases).

    int GetChildCount() const override { return 3; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return condition_;
            case 1: return trueStatement_;
            case 2: return falseStatement_;
            default: throw std::out_of_range("IfElseStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(condition_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(trueStatement_, static_cast<Statement*>(value), 1); break;
            case 2: SetChildNode(falseStatement_, static_cast<Statement*>(value), 2); break;
            default: throw std::out_of_range("IfElseStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ConditionSlot;
            case 1: return &TrueStatementSlot;
            case 2: return &FalseStatementSlot;
            default: throw std::out_of_range("IfElseStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is IfElseStatement o && this.Condition.DoMatch(o.Condition, match) &&
    // this.TrueStatement.DoMatch(o.TrueStatement, match) && MatchOptional(this.FalseStatement,
    // o.FalseStatement, match)`. `Condition`/`TrueStatement` are NON-NULLABLE recursive, so the
    // generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (the
    // `MatchRequired` same-class static in the port); `FalseStatement` is NULLABLE recursive, so
    // the generator emits `MatchOptional` (both absent, or both present and the pattern's
    // `DoMatch` decides). There is no scalar enum, so there is no `Any`-wildcard term. A
    // type-only mismatch (not an `IfElseStatement`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `Expression*`/`Statement*`. `MatchRequired` guards a missing
    // pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (the required children set) the behavior is identical
    // to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<IfElseStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(condition_, o->condition_, match)
            && MatchRequired(trueStatement_, o->trueStatement_, match)
            && MatchOptional(falseStatement_, o->falseStatement_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the three children through the setters (which re-parent and
    // re-index via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not stored
    // on this node (no own location fields -- the base fields hold the print-time span), so only
    // the children + annotation channel are copied. Each child is skipped if absent (`Clone`
    // tolerates a missing child even though the required slots are empty -- the invariant is
    // enforced by `CheckInvariant`, not by `Clone`). `Expression::Clone()` returns `Expression*`
    // and `Statement::Clone()` returns `Statement*` (the covariant overrides), which the setters
    // accept directly. No name shadowing, so no elaborated-type-specifier casts are needed.
    IfElseStatement* Clone() const override {
        auto* node = new IfElseStatement();
        node->CloneAnnotationsFrom(*this);
        if (condition_ != nullptr)
            node->Condition(static_cast<Expression*>(condition_->Clone()));
        if (trueStatement_ != nullptr)
            node->TrueStatement(static_cast<Statement*>(trueStatement_->Clone()));
        if (falseStatement_ != nullptr)
            node->FalseStatement(static_cast<Statement*>(falseStatement_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. Required slots are non-null only by invariant; the nullable
    // `FalseStatement` is null until the `else` branch is set. No name shadowing (no member is
    // named `Expression`/`Statement`), so the field types are the plain base types.
    Expression* condition_ = nullptr;
    Statement* trueStatement_ = nullptr;
    Statement* falseStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_IFELSESTATEMENT_HPP
