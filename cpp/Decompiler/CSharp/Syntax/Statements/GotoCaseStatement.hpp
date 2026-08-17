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

// Port of the `GotoCaseStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/GotoStatement.cs (the generated
// `GotoCaseStatement.g.cs` + the hand-written partial). The second member of the goto family
// (a sealed `Statement` with a single REQUIRED `Expression` child -- the `ExpressionStatement`
// D255 shape applied to the goto family) plus the `GotoKeyword`/`CaseKeyword` const strings,
// plugging into the `IAstVisitor`/`AcceptVisitor` dispatch. The next in-order Phase-5 piece
// per the D256 plan ("the EmptyStatement/GotoStatement/LabelStatement leaves").
//
// `goto_statement ::= 'goto' 'case' expression ';'` (C# grammar 13.10.4): a `goto case`
// whose target value is the `LabelExpression` `Expression` child. The hand-written part
// declares only the two const strings and the `[Slot("Expression")] Expression
// LabelExpression` slot property (no ctors, no helpers); the generator emits the
// `LabelExpressionSlot` slot static pointing at the shared `Slots::Expression` kind (already
// ported by `UnaryOperatorExpression` -- no new `Slots` constant), the const-index
// `SetChildNode(ref field, value, 0)` setter, the `GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` overrides over the one single slot, and the `DoMatch` `return other is
// GotoCaseStatement o && this.LabelExpression.DoMatch(o.LabelExpression, match)` -- a
// NON-nullable recursive child, so the generator emits the direct dispatch (NOT
// `MatchOptional`, which it emits only for a nullable recursive child); routed through
// `AstNode::MatchRequired` (the D231 same-class static helper, since C++
// `[class.access.derived]` forbids a derived node from calling the protected `DoMatch` through
// a base `Expression*`). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`);
// the port overrides it (no `MemberwiseClone`): deep-clones the child through the setter and
// copies the annotation channel.
//
// NO C++ name-shadowing crux (unlike the `ExpressionStatement` D255 whose `Expression()`
// accessor shadows the `Expression` class): the slot property is `LabelExpression` (NOT
// `Expression`), so the `LabelExpression()` accessor does NOT collide with the `Expression`
// base type in this class scope (no member is named `Expression`), and no
// elaborated-type-specifier (`class Expression`) is needed -- the plain `Expression` resolves
// to the base class in every type position (the `TypeReferenceExpression` D245 / `Attribute`
// D240 differently-named-property precedent). The const strings are static fields, so the
// generator's `MembersToMatch` (which iterates only instance `IPropertySymbol`s) excludes them
// from the `DoMatch`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_GOTOCASESTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_GOTOCASESTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class GotoCaseStatement : Statement`. `final` (the C#
// `sealed`): no further derivation. The `ExpressionStatement` D255 shape (a single REQUIRED
// `Expression` slot) applied to the goto family, plus the `goto`/`case` keyword const strings.
class GotoCaseStatement final : public Statement {
public:
    ~GotoCaseStatement() override = default;

    // The generated empty ctor (the C# `public GotoCaseStatement()`). `LabelExpression`
    // defaults to null (no case value). A null operand violates the required-slot invariant,
    // so a default-constructed node is only valid until `LabelExpression` is set (or until
    // `DoMatch`/`CheckInvariant` observe the missing child) -- the `ExpressionStatement` D255
    // / `UnaryOperatorExpression` D231 required-slot behavior.
    GotoCaseStatement() = default;

    // The generated all-params ctor (the C# `public GotoCaseStatement(Expression
    // labelExpression)`); the `LabelExpression` slot is REQUIRED so the required-prefix length
    // is 1, which IS the full count (1), so this single-arg form is both the required-prefix ctor
    // and the all-params ctor (no shorter prefix ctor and no params overload since there is no
    // collection). `explicit` because a single-argument ctor is a converting ctor by default
    // (the `ExpressionStatement` D255 / `TypeReferenceExpression` D248 precedent).
    explicit GotoCaseStatement(Expression* labelExpression)
        : GotoCaseStatement() {
        LabelExpression(labelExpression);
    }

    // The C# `public const string GotoKeyword = "goto"` / `public const string CaseKeyword =
    // "case"` (the keyword tokens the output visitor emits) -- port as `static constexpr
    // const char*` (static fields, not instance state), so the generator's `MembersToMatch`
    // (which iterates only instance `IPropertySymbol`s) excludes them from the `DoMatch` (the
    // `BreakStatement.BreakKeyword` D254 / `DirectionExpression` ref-out-in-keyword D235
    // precedent applied to the goto family).
    static constexpr const char* GotoKeyword = "goto";
    static constexpr const char* CaseKeyword = "case";

    // The C# `[Slot("Expression")] Expression LabelExpression` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes this slot). The C#
    // getter returns the backing field null-forgiving (`field!`) because the slot is required;
    // the port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type). NO elaborated `class Expression` (the `LabelExpression()` accessor does NOT shadow
    // the `Expression` base type -- the member is named `LabelExpression`, not `Expression`).
    Expression* LabelExpression() const { return labelExpression_; }
    void LabelExpression(Expression* value) {
        SetChildNode(labelExpression_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable); the
    // kind carries identity only. `Slots::Expression` is already ported (by
    // `UnaryOperatorExpression`), so no new `Slots` constant. NO elaborated `class Expression`
    // (no member named `Expression` shadows the base type in this scope).
    static inline const CSharpSlotInfoT<Expression> LabelExpressionSlot{"LabelExpression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitGotoCaseStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitGotoCaseStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`LabelExpression`); no collection, so
    // `GetChildCount` is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
    // index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return labelExpression_;
            default: throw std::out_of_range("GotoCaseStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(labelExpression_, static_cast<Expression*>(value), 0); break;
            default: throw std::out_of_range("GotoCaseStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &LabelExpressionSlot;
            default: throw std::out_of_range("GotoCaseStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is GotoCaseStatement o && this.LabelExpression.DoMatch(o.LabelExpression,
    // match)`. `LabelExpression` is a NON-NULLABLE recursive child, so the generator emits the
    // direct `this.LabelExpression.DoMatch(o.LabelExpression, match)` term (NOT `MatchOptional`,
    // which it emits only for a nullable recursive child). The C# direct dispatch assumes the
    // required child is present; the port routes it through `AstNode::MatchRequired` (the
    // same-class static helper) because C++ `[class.access.derived]` forbids a derived node from
    // calling the protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a
    // missing operand defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the operand's `DoMatch(nullptr)`
    // which returns false. For well-formed nodes (the operand always set) the behavior is
    // identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<GotoCaseStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(labelExpression_, o->labelExpression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `LabelExpression` child deep-cloned through the setter (which re-parents and re-indexes
    // via `SetChildNode`). No own location fields (it does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `ExpressionStatement` D255 /
    // `BreakStatement` D254 no-location-copy precedent). NO elaborated `class Expression` (no
    // member named `Expression` shadows the base type in this scope); the child `Clone()`
    // returns `Expression*` (the `Expression::Clone` covariant override), which the
    // `LabelExpression(Expression*)` setter accepts directly.
    GotoCaseStatement* Clone() const override {
        auto* node = new GotoCaseStatement();
        node->CloneAnnotationsFrom(*this);
        if (labelExpression_ != nullptr)
            node->LabelExpression(static_cast<Expression*>(labelExpression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. NO elaborated `class Expression` (the `LabelExpression()` accessor
    // declared above does NOT shadow the `Expression` base type -- no member is named
    // `Expression`). A required slot is non-null only by invariant, so the pointer is null
    // until the operand is set.
    Expression* labelExpression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_GOTOCASESTATEMENT_HPP
