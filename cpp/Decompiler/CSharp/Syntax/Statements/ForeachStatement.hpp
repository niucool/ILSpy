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

// Port of the `ForeachStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/ForeachStatement.cs (the generated
// `ForeachStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D263 plan ("ForeachStatement (now unblocked -- it needs the `VariableDesignation` hierarchy just
// ported, plus the already-ported `Slots::Type`/`Slots::Expression`/`Slots::EmbeddedStatement`
// kinds, plus the already-ported `UnaryOperatorExpression::AwaitKeyword` const it aliases)").
//
// `foreach_statement ::= 'await'? 'foreach' '(' type variable_designation 'in' expression ')'
// statement` (C# grammar 13.9.5.1): a `Statement` with four single, REQUIRED (non-nullable)
// `[Slot]` children -- a `VariableType` `AstType` (the element type, e.g. `int` in
// `foreach (int x in c)`), a `VariableDesignation` `VariableDesignation` (the loop variable or
// deconstruction, e.g. `x` or `(x, y)` -- the `VariableDesignation` hierarchy just ported), an
// `InExpression` `Expression` (the collection, the expression after `in`), and an
// `EmbeddedStatement` `Statement` (the loop body) -- plus one bool scalar (`IsAsync`, the leading
// `await`) and NO scalar enum.
//
// The generator emits four typed slot statics (`VariableTypeSlot` pointing at the shared
// `Slots::Type` kind, `VariableDesignationSlot` pointing at the shared `Slots::VariableDesignation`
// kind -- both already ported, by `Attribute` and `ParenthesizedVariableDesignation` respectively --
// `InExpressionSlot` pointing at the shared `Slots::Expression` kind, already ported by
// `UnaryOperatorExpression`, and `EmbeddedStatementSlot` pointing at the shared
// `Slots::EmbeddedStatement` kind, already ported by `WhileStatement`), the const-index
// `SetChildNode(ref field, value, index)` setters (no collection precedes any slot, so each
// flattened index is the constant slot position 0/1/2/3), the `GetChildCount`/`GetChild`/
// `SetChild`/`GetChildSlotInfo` overrides over the four single slots, and the `DoMatch` `return
// other is ForeachStatement o && this.IsAsync == o.IsAsync && this.VariableType.DoMatch(
// o.VariableType, match) && this.VariableDesignation.DoMatch(o.VariableDesignation, match) &&
// this.InExpression.DoMatch(o.InExpression, match) && this.EmbeddedStatement.DoMatch(
// o.EmbeddedStatement, match)`. The `IsAsync` bool is a fall-through plain-equality term; all
// four children are NON-NULLABLE recursive, so the generator emits the direct
// `this.{member}.DoMatch(o.{member}, match)` term for each (the `MatchRequired` same-class static
// in the port). There is no scalar enum, so there is no `Any`-wildcard term. `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// copies the `IsAsync` scalar, deep-clones the four children through the setters (which
// re-parent), and copies the annotation channel.
//
// C++ name-shadowing crux (the `UnaryOperatorExpression` D231 / `CastExpression` D243 / `Identifier`
// D227 pattern, applied to a `VariableDesignation`-typed slot accessor): the
// `VariableDesignation()` accessor (a member function) shadows the `VariableDesignation` CLASS in
// this class scope (C++ unqualified name lookup finds the member and stops, even though it is not
// a type), so the token type is the elaborated-type-specifier `class VariableDesignation`
// (`basic.lookup.elab` ignores non-type names) in every type position AFTER the getter declaration
// (the setter parameter, the slot static's element type, the `SetChild`/`Clone` `static_cast`s,
// and the backing field); the ctor parameter and getter return type precede the getter so they
// use the plain `VariableDesignation`. The other three slot accessors do NOT shadow their element
// types: `VariableType()` does not collide with `AstType` (no class named `VariableType` or
// `Type` lives in the `Syntax` namespace -- the `Attribute` D240 lesson), `InExpression()` does not
// collide with `Expression` (a member named `InExpression` is not the name `Expression`), and
// `EmbeddedStatement()` does not collide with `Statement` (a member named `EmbeddedStatement` is
// not the name `Statement`), so those three use the plain element types throughout.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FOREACHSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FOREACHSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ForeachStatement : Statement`. `final` (the C# `sealed`):
// no further derivation. The first ported statement with FOUR required single `[Slot]` children
// (the `ConditionalExpression` D232 three-required-slot shape plus one more), spanning three
// distinct child hierarchies (`AstType`/`VariableDesignation`/`Expression`/`Statement`).
class ForeachStatement final : public Statement {
public:
    ~ForeachStatement() override = default;

    // The generated empty ctor (the C# `public ForeachStatement()`). All four slots default to
    // null. All four slots are required, so a default-constructed node is only valid until the
    // slots are set (or until `DoMatch`/`CheckInvariant` observe the missing children) -- the
    // `WhileStatement` D258 / `IfElseStatement` D258 required-slot behavior. The `IsAsync` bool
    // defaults to `false` (no leading `await` by default).
    ForeachStatement() = default;

    // The generated all-params ctor (the C# `public ForeachStatement(AstType variableType,
    // VariableDesignation variableDesignation, Expression inExpression, Statement
    // embeddedStatement)`). `RequiredConstructorPrefixLength` is 4 (all four required) and there
    // is no collection, so the single full ctor IS the required-prefix ctor (no shorter prefix
    // ctor and no `params` overload). Delegated to the empty ctor then the setters so the slot
    // machinery re-parents and re-indexes the children. The `IsAsync` bool scalar is not a ctor
    // param (the generator adds only settable ENUM-typed scalars to `CtorParams`; a bool is not
    // an enum), so it is left at its `false` default and set via the property setter. The
    // `variableDesignation` parameter uses the plain `VariableDesignation` (it precedes the
    // `VariableDesignation()` getter declaration, so the class is unshadowed there).
    ForeachStatement(AstType* variableType, VariableDesignation* variableDesignation,
                      Expression* inExpression, Statement* embeddedStatement)
        : ForeachStatement() {
        VariableType(variableType);
        VariableDesignation(variableDesignation);
        InExpression(inExpression);
        EmbeddedStatement(embeddedStatement);
    }

    // The C# `public bool IsAsync { get; set; }` -- whether the statement carries a leading
    // `await` (the `await foreach` form). A plain bool field: not a child slot (no `[Slot]`), not
    // a ctor param (the generator adds only settable ENUM-typed scalars), so it is set via the
    // property setter. It IS in `MembersToMatch` (the generator adds every non-`[Slot]` instance
    // property), and a bool (not an enum, no `Any`, not a string) emits the fall-through
    // plain-equality `DoMatch` term (the `ComposedType.HasRefSpecifier` D242 / `UsingStatement.
    // IsAsync` D262 precedent). No name shadowing.
    bool IsAsync() const { return isAsync_; }
    void IsAsync(bool value) { isAsync_ = value; }

    // The C# `[Slot("Type")] AstType VariableType` -- a single, REQUIRED (non-nullable) `AstType`
    // child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). No name shadowing
    // (the `VariableType()` accessor does not collide with the `AstType` base type -- no class
    // named `VariableType` or `Type` lives in the `Syntax` namespace -- the `Attribute` D240
    // lesson), so the element type is the plain `AstType` throughout.
    AstType* VariableType() const { return variableType_; }
    void VariableType(AstType* value) {
        SetChildNode(variableType_, value, 0);
    }

    // The C# `[Slot("VariableDesignation")] VariableDesignation VariableDesignation` -- a single,
    // REQUIRED (non-nullable) `VariableDesignation` child at flattened index 1. The generator
    // emits the const-index `SetChildNode(ref field, value, 1)` setter. C++ name-shadowing crux:
    // the `VariableDesignation()` accessor (a member function) shadows the `VariableDesignation`
    // CLASS in this class scope, so the setter parameter type uses the elaborated specifier
    // `class VariableDesignation` (the `Expression()`-of-type-`Expression` D231 crux applied to a
    // `VariableDesignation`-typed slot accessor). The getter return type precedes the getter
    // declaration, so it uses the plain `VariableDesignation`.
    VariableDesignation* VariableDesignation() const { return variableDesignation_; }
    void VariableDesignation(class VariableDesignation* value) {
        SetChildNode(variableDesignation_, value, 1);
    }

    // The C# `[Slot("Expression")] Expression InExpression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 2. The generator emits the const-index
    // `SetChildNode(ref field, value, 2)` setter. The `[Slot("Expression")]` argument names the
    // shared `Slots::Expression` KIND, but the PROPERTY is `InExpression`, so the `InExpression()`
    // accessor does NOT collide with the `Expression` base type (a member named `InExpression`
    // is not the name `Expression` -- the `MemberReferenceExpression.Target` D247
    // differently-named-property discriminator). No elaborated-type-specifier is needed; the
    // plain `Expression` is used throughout.
    Expression* InExpression() const { return inExpression_; }
    void InExpression(Expression* value) {
        SetChildNode(inExpression_, value, 2);
    }

    // The C# `[Slot("EmbeddedStatement")] Statement EmbeddedStatement` -- a single, REQUIRED
    // (non-nullable) `Statement` child at flattened index 3. The generator emits the const-index
    // `SetChildNode(ref field, value, 3)` setter. No name shadowing (the `EmbeddedStatement()`
    // accessor does not collide with the `Statement` base type -- a member named
    // `EmbeddedStatement` is not the name `Statement`), so the plain `Statement` is used
    // throughout.
    Statement* EmbeddedStatement() const { return embeddedStatement_; }
    void EmbeddedStatement(Statement* value) {
        SetChildNode(embeddedStatement_, value, 3);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. The
    // `IsOptional` flag is false for each (all four required); the kind carries identity only.
    // All four kinds are already ported (`Slots::Type` by `Attribute`, `Slots::VariableDesignation`
    // by `ParenthesizedVariableDesignation`, `Slots::Expression` by `UnaryOperatorExpression`,
    // `Slots::EmbeddedStatement` by `WhileStatement`), so no new `Slots` constant is added. The
    // `VariableDesignationSlot` element type uses the elaborated `class VariableDesignation` (the
    // `VariableDesignation()` accessor shadows the class in this scope); the other three use the
    // plain element types (no shadowing).
    static inline const CSharpSlotInfoT<AstType> VariableTypeSlot{"VariableType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<class VariableDesignation> VariableDesignationSlot{"VariableDesignation", false, &Slots::VariableDesignation, false};
    static inline const CSharpSlotInfoT<Expression> InExpressionSlot{"InExpression", false, &Slots::Expression, false};
    static inline const CSharpSlotInfoT<Statement> EmbeddedStatementSlot{"EmbeddedStatement", false, &Slots::EmbeddedStatement, false};

    // The C# `public const string ForeachKeyword = "foreach"` / `public const string InKeyword =
    // "in"` (the keyword tokens the output visitor emits) -- ports as `static constexpr const
    // char*` (static fields, not instance state), so the generator's `MembersToMatch` (which
    // iterates only instance `IPropertySymbol`s) excludes them from the `DoMatch` (the
    // `BreakStatement` D254 / `WhileStatement` D258 / `UsingStatement` D262 precedent applied to the
    // foreach statement).
    static constexpr const char* ForeachKeyword = "foreach";
    static constexpr const char* InKeyword = "in";

    // The C# `public const string AwaitKeyword = UnaryOperatorExpression.AwaitKeyword` -- the
    // `await` keyword token (the `await foreach` form), aliased to the `AwaitKeyword` const on
    // `UnaryOperatorExpression` (which carries the canonical `await` literal for the `Await`
    // unary operator; `UsingStatement.AwaitKeyword` D262 aliases the same const). Ports as a
    // `static constexpr const char*` initialized from `UnaryOperatorExpression::AwaitKeyword`
    // (faithful to the C# alias; the value is the same compile-time constant `"await"`, so the
    // alias preserves the single-source-of-truth). A static field (not instance state), so
    // excluded from `MembersToMatch`/`DoMatch`.
    static constexpr const char* AwaitKeyword = UnaryOperatorExpression::AwaitKeyword;

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitForeachStatement` (`ForeachStatement` does not end in "AstType", so the
    // generator's visit-method-name default yields `VisitForeachStatement`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitForeachStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Four single slots at flattened indices 0/1/2/3 (`VariableType`/`VariableDesignation`/
    // `InExpression`/`EmbeddedStatement`); no collection, so `GetChildCount` is the constant 4
    // and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the generator's
    // `WriteReturnDispatchSwitch` shape, with four cases). The `VariableDesignation` slot is
    // typed `VariableDesignation`, so `SetChild` downcasts to the elaborated
    // `class VariableDesignation*` (the slot element type); the other three downcast to their
    // plain element types (`AstType*`/`Expression*`/`Statement*`).

    int GetChildCount() const override { return 4; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return variableType_;
            case 1: return variableDesignation_;
            case 2: return inExpression_;
            case 3: return embeddedStatement_;
            default: throw std::out_of_range("ForeachStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(variableType_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(variableDesignation_, static_cast<class VariableDesignation*>(value), 1); break;
            case 2: SetChildNode(inExpression_, static_cast<Expression*>(value), 2); break;
            case 3: SetChildNode(embeddedStatement_, static_cast<Statement*>(value), 3); break;
            default: throw std::out_of_range("ForeachStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &VariableTypeSlot;
            case 1: return &VariableDesignationSlot;
            case 2: return &InExpressionSlot;
            case 3: return &EmbeddedStatementSlot;
            default: throw std::out_of_range("ForeachStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ForeachStatement o && this.IsAsync == o.IsAsync &&
    // this.VariableType.DoMatch(o.VariableType, match) && this.VariableDesignation.DoMatch(
    // o.VariableDesignation, match) && this.InExpression.DoMatch(o.InExpression, match) &&
    // this.EmbeddedStatement.DoMatch(o.EmbeddedStatement, match)`. The `IsAsync` bool is a
    // fall-through plain-equality term; all four children are NON-NULLABLE recursive, so the
    // generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (NOT
    // `MatchOptional`, which the generator emits only for a nullable recursive child); there is
    // no scalar enum, so there is no `Any`-wildcard term. A type-only mismatch (not a
    // `ForeachStatement`) rejects early. The terms are in `MembersToMatch` (source declaration)
    // order: the `IsAsync` bool scalar precedes the four `[Slot]` children in the C# source, so
    // it appears before the recursive terms.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `AstType*`/`VariableDesignation*`/`Expression*`/`Statement*`.
    // `MatchRequired` guards a missing pattern-side child defensively (a null pattern child does
    // not match; the C# would null-deref), and a null candidate child flows through the child's
    // `DoMatch(nullptr)` which returns false. For well-formed nodes (all four children set) the
    // behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ForeachStatement*>(other);
        if (o == nullptr)
            return false;
        return isAsync_ == o->isAsync_
            && MatchRequired(variableType_, o->variableType_, match)
            && MatchRequired(variableDesignation_, o->variableDesignation_, match)
            && MatchRequired(inExpression_, o->inExpression_, match)
            && MatchRequired(embeddedStatement_, o->embeddedStatement_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): copies the `IsAsync` scalar and the annotation channel
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and
    // deep-clones the four children through the setters (which re-parent and re-index via
    // `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not stored on this node
    // (no own location fields -- the base fields hold the print-time span), so only the scalar +
    // children + annotation channel are copied. Each child is skipped if absent (`Clone`
    // tolerates a missing child even though the slots are required -- the invariant is enforced
    // by `CheckInvariant`, not by `Clone`). `AstType::Clone()` returns `AstType*` (the covariant
    // override), which `VariableType(AstType*)` accepts directly; the `VariableDesignation` child
    // clones through `VariableDesignation::Clone()` (covariant, returns `VariableDesignation*`),
    // cast to the elaborated `class VariableDesignation*` for the setter (the
    // `VariableDesignation()` accessor shadows the class); `Expression::Clone()` returns
    // `Expression*` and `Statement::Clone()` returns `Statement*` (the covariant overrides),
    // which the setters accept directly.
    ForeachStatement* Clone() const override {
        auto* node = new ForeachStatement();
        node->isAsync_ = isAsync_;
        node->CloneAnnotationsFrom(*this);
        if (variableType_ != nullptr)
            node->VariableType(variableType_->Clone());
        if (variableDesignation_ != nullptr)
            node->VariableDesignation(static_cast<class VariableDesignation*>(variableDesignation_->Clone()));
        if (inExpression_ != nullptr)
            node->InExpression(static_cast<Expression*>(inExpression_->Clone()));
        if (embeddedStatement_ != nullptr)
            node->EmbeddedStatement(static_cast<Statement*>(embeddedStatement_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. The `IsAsync` bool defaults to `false`; each required-slot pointer is
    // null until the child is set (non-null only by invariant). The `variableDesignation_` field
    // uses the elaborated `class VariableDesignation` (the `VariableDesignation()` accessor
    // declared above shadows the `VariableDesignation` class in this class scope); the other three
    // use the plain element types (no shadowing).
    bool isAsync_ = false;
    AstType* variableType_ = nullptr;
    class VariableDesignation* variableDesignation_ = nullptr;
    Expression* inExpression_ = nullptr;
    Statement* embeddedStatement_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_FOREACHSTATEMENT_HPP
