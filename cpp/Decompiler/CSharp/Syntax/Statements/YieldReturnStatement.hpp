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

// Port of the `YieldReturnStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/YieldStatement.cs (the generated
// `YieldReturnStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece per
// the D258 plan ("the remaining concrete statements: YieldReturnStatement ..."). The
// `yield_statement ::= 'yield' 'return' expression ';'` (C# grammar 13.15): a sealed `Statement`
// with a single REQUIRED (non-nullable) `Expression` child slot (the yielded value) plus the
// `YieldKeyword`/`ReturnKeyword` const strings. It is the `ExpressionStatement` D255 shape (a
// single required `Expression` slot at flattened index 0, no scalar) applied to the yield
// family, plus two keyword consts -- structurally the simplest slot-bearing statement that
// reuses the already-ported `Slots::Expression` kind with no new `Slots` constant.
//
// The hand-written partial declares only the `Expression` slot property and the two const
// strings, no ctors, no helpers. The generator emits the `ExpressionSlot` slot static pointing
// at the shared `Slots::Expression` kind (already ported by `UnaryOperatorExpression` -- no new
// `Slots` constant), the const-index `SetChildNode(ref field, value, 0)` setter, the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single slot,
// and the `DoMatch` `return other is YieldReturnStatement o && this.Expression.DoMatch(
// o.Expression, match)` -- a NON-nullable recursive child, so the generator emits the direct
// dispatch (NOT `MatchOptional`, which it emits only for a nullable recursive child); routed
// through `AstNode::MatchRequired` (the D231 same-class static helper, since C++
// `[class.access.derived]` forbids a derived node from calling the protected `DoMatch` through
// a base `Expression*`). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`);
// the port overrides it (no `MemberwiseClone`): deep-clones the child through the setter and
// copies the annotation channel.
//
// C++ name-shadowing crux (the `UnaryOperatorExpression` D231 / `ExpressionStatement` D255
// precedent): the C# property is `Expression` of type `Expression` (a property named the same
// as its type). The faithful port names the accessor `Expression()`, which SHADOWS the
// `Expression` class in this class scope (C++ unqualified name lookup finds the member and
// stops, even though it is not a type). Every type usage AFTER the `Expression()` getter is
// declared therefore uses the elaborated-type-specifier `class Expression`; the ctor parameter
// and the getter return type precede the getter so they use the plain `Expression`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_YIELDRETURNSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_YIELDRETURNSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class YieldReturnStatement : Statement`. `final` (the C#
// `sealed`): no further derivation. The `ExpressionStatement` D255 shape (a single REQUIRED
// `Expression` slot) applied to the yield family, plus the `yield`/`return` keyword consts.
class YieldReturnStatement final : public Statement {
public:
    ~YieldReturnStatement() override = default;

    // The generated empty ctor (the C# `public YieldReturnStatement()`). `Expression` defaults
    // to null (no yielded value). A null operand violates the required-slot invariant, so a
    // default-constructed node is only valid until `Expression` is set (or until `DoMatch`/
    // `CheckInvariant` observe the missing child) -- the `UnaryOperatorExpression` D231 /
    // `ExpressionStatement` D255 required-slot behavior.
    YieldReturnStatement() = default;

    // The generated all-params ctor (the C# `public YieldReturnStatement(Expression
    // expression)`); the `Expression` slot is REQUIRED so the required-prefix length is 1,
    // which IS the full count (1), so this single-arg form is both the required-prefix ctor and
    // the all-params ctor (no shorter prefix ctor and no params overload since there is no
    // collection). `explicit` because a single-argument ctor is a converting ctor by default
    // (the `ExpressionStatement` D255 / `TypeReferenceExpression` D248 precedent). The
    // parameter type precedes the `Expression()` accessor declaration, so the plain
    // `Expression` (the base type) is unshadowed here; the body resolves the
    // `Expression(expression)` call to the setter declared below.
    explicit YieldReturnStatement(Expression* expression)
        : YieldReturnStatement() {
        Expression(expression);
    }

    // The C# `public const string YieldKeyword = "yield"` and `ReturnKeyword = "return"` (the
    // `yield`/`return` keyword tokens the output visitor emits) -- ports as
    // `static constexpr const char*` (static fields, not instance state), so the generator's
    // `MembersToMatch` (which iterates only instance `IPropertySymbol`s) excludes them from the
    // `DoMatch` (the `BreakStatement.BreakKeyword` D254 / `GotoStatement.GotoKeyword` D257
    // precedent applied to the yield family).
    static constexpr const char* YieldKeyword = "yield";
    static constexpr const char* ReturnKeyword = "return";

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED (non-nullable)
    // `Expression` child at flattened index 0. The const-index `SetChildNode(ref field, value,
    // 0)` setter (no collection precedes this slot). The C# getter returns the backing field
    // null-forgiving (`field!`) because the slot is required; the port returns the raw pointer
    // (a required slot is non-null only by invariant, not by type).
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable); the
    // kind carries identity only. `Slots::Expression` is already ported (by
    // `UnaryOperatorExpression`), so no new `Slots` constant. The element type uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in this
    // scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitYieldReturnStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitYieldReturnStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so `GetChildCount` is
    // the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("YieldReturnStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("YieldReturnStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("YieldReturnStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is YieldReturnStatement o && this.Expression.DoMatch(o.Expression, match)`.
    // `Expression` is a NON-NULLABLE recursive child, so the generator emits the direct
    // `this.Expression.DoMatch(o.Expression, match)` term (NOT `MatchOptional`, which it emits
    // only for a nullable recursive child). The C# direct dispatch assumes the required child
    // is present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing operand
    // defensively (a null pattern child does not match; the C# would null-deref), and a null
    // candidate child flows through the operand's `DoMatch(nullptr)` which returns false. For
    // well-formed nodes (the operand always set) the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<YieldReturnStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Expression` child deep-cloned through the setter (which re-parents and re-indexes via
    // `SetChildNode`). No own location fields (it does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `ExpressionStatement` D255 /
    // `BreakStatement` D254 no-location-copy precedent). The `static_cast` uses the elaborated
    // `class Expression` (the `Expression()` accessor shadows the base type in this scope).
    YieldReturnStatement* Clone() const override {
        auto* node = new YieldReturnStatement();
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
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_YIELDRETURNSTATEMENT_HPP
