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

// Port of the `UncheckedExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/UncheckedExpression.cs (the generated
// `UncheckedExpression.g.cs` + the hand-written partial). The seventh slot-bearing C# AST
// node (PORT_PLAN.md section 5.2 / decision D1: port the generated *output* by hand) -- the
// next in-order Phase-5 piece per the D233 plan ("CheckedExpression/UncheckedExpression (a
// single Expression operand each, the same one-single-required-slot shape as
// ParenthesizedExpression/UnaryOperatorExpression -- they reuse Slots::Expression and add
// only a CheckedKeyword/UncheckedKeyword const string; a natural pair to port together)").
// It is the simplest slot-bearing shape (the ParenthesizedExpression shape): a single,
// REQUIRED (non-nullable) `Expression` child and NO scalar enum, so its generated `DoMatch`
// is a single non-nullable recursive term with no `Any`-wildcard. It adds one thing beyond
// that shape: the `UncheckedKeyword` const string ("unchecked"), the token the output visitor
// emits for the `unchecked` keyword (CSharpOutputVisitor.VisitUncheckedExpression calls
// `WriteKeyword(UncheckedExpression.UncheckedKeyword)`).
//
// `unchecked_expression ::= 'unchecked' '(' expression ')'` (C# grammar 12.8.20): an
// `Expression` with a single `[Slot] Expression` child (the operand, non-nullable in the C#
// source -- `[Slot("Expression")] public partial Expression Expression`, no `?`, so the slot
// is required) and a `public const string UncheckedKeyword` field. The slot kind is
// `Expression` (the `[Slot]` argument), so the per-node slot REUSES the already-ported
// `Slots::Expression` kind (the same kind UnaryOperatorExpression registered in D231 and
// ParenthesizedExpression reused in D233) -- no new `Slots.hpp` constant. The generator emits
// the const-index `SetChildNode(ref field, value, 0)` setter (the single slot is the first
// and only slot, so the flattened index is the constant 0), the `GetChildCount`/`GetChild`/
// `SetChild`/`GetChildSlotInfo` overrides over the one single slot, and the `DoMatch`
// `return other is UncheckedExpression o && this.Expression.DoMatch(o.Expression, match)`.
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it
// (no `MemberwiseClone`): there is no scalar member (the const string is a static literal,
// not instance state), so `Clone` copies the annotation channel and deep-clones the child
// through the setter (which re-parents).
//
// The `UncheckedKeyword` const string is part of the node's public API (the output visitor
// reads `UncheckedExpression::UncheckedKeyword`), so the port carries it as a `static
// constexpr` `const char*`. It is a compile-time literal (a static field in C#), not an
// instance member, so it is NOT part of `MembersToMatch` (the generator iterates instance
// `IPropertySymbol`s only, and a `const string` field is neither a property nor an instance
// member -- verified in DecompilerSyntaxTreeGenerator.cs), so it does not appear in the
// generated `DoMatch`.
//
// C++ name-shadowing crux: the C# property is `Expression` of type `Expression` (a property
// named the same as its type -- legal in C#, which keeps property and type names in separate
// spaces). The faithful port names the accessor `Expression()`, but a member function named
// `Expression` SHADOWS the `Expression` base type within this class scope (C++ unqualified
// name lookup finds the member and stops, even though it is not a type -- the D224
// `Annotation<T>()`-shadows-the-`Annotation`-type crux, the D231 `UnaryOperatorExpression` /
// D233 `ParenthesizedExpression` precedent). Every type usage AFTER the `Expression()`
// getter is declared therefore uses the ELABORATED-TYPE-SPECIFIER `class Expression`
// (basic.lookup.elab: an elaborated specifier ignores non-type names and finds the hidden
// class), so the setter parameter, the slot static, the `static_cast`, and the backing
// field all spell the operand type as `class Expression`. The ctor parameter and the getter
// return type precede the getter's declaration, so they use the plain `Expression` (no
// member function is in scope there yet).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNCHECKEDEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNCHECKEDEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class UncheckedExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The seventh concrete node with `[Slot]` children,
// and the simplest shape (the ParenthesizedExpression shape): a single required
// (non-nullable) `Expression` child, no scalar member, plus the `UncheckedKeyword` const
// string.
class UncheckedExpression final : public Expression {
public:
    ~UncheckedExpression() override = default;

    // The C# `public const string UncheckedKeyword = "unchecked"` -- the token the output
    // visitor emits for the `unchecked` keyword (CSharpOutputVisitor.VisitUncheckedExpression).
    // A compile-time literal carried as a `static constexpr const char*` (a static field,
    // not instance state, so it is not part of `MembersToMatch`/`DoMatch`).
    static constexpr const char* UncheckedKeyword = "unchecked";

    // The generated empty ctor (the C# `public UncheckedExpression()`). `Expression`
    // defaults to null (no operand). A null operand violates the required-slot invariant,
    // so a default-constructed node is only valid until `Expression` is set (or until
    // `DoMatch`/`CheckInvariant` observe the missing child).
    UncheckedExpression() = default;

    // The generated all-params ctor (the C# `public UncheckedExpression(Expression
    // expression)`). Delegated to the empty ctor then the setter so the slot machinery
    // re-parents and re-indexes the child. The parameter type precedes the `Expression()`
    // accessor declarations, so the plain `Expression` (the base type) is unshadowed here;
    // the body is in complete-class context, so the `Expression(expression)` call resolves
    // to the setter declared below.
    explicit UncheckedExpression(Expression* expression)
        : UncheckedExpression() {
        Expression(expression);
    }

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
    // D233), so no new `Slots.hpp` constant is needed -- the per-node `ExpressionSlot`
    // points at the shared kind, and the pointer-identity comparison
    // `node.Slot().Kind() == &Slots::Expression` the slot system relies on is preserved
    // across `UnaryOperatorExpression`/`ParenthesizedExpression`/`CheckedExpression`/
    // `UncheckedExpression` (all have a single required `Expression` operand at the
    // `Expression` position). The element type uses the elaborated `class Expression` (the
    // `Expression()` accessor shadows the base type in this scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitUncheckedExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUncheckedExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so
    // `GetChildCount` is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape, with a single
    // case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("UncheckedExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("UncheckedExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("UncheckedExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is UncheckedExpression o && this.Expression.DoMatch(o.Expression, match)`.
    // `Expression` is a NON-NULLABLE recursive child, so the generator emits the single
    // direct `this.Expression.DoMatch(o.Expression, match)` term (NOT `MatchOptional`, which
    // the generator emits only for a nullable recursive child); there is no scalar enum, so
    // there is no `Any`-wildcard term -- the simplest slot-bearing `DoMatch`. The
    // `UncheckedKeyword` const string is a static field, not an instance property, so it is
    // not part of `MembersToMatch` and does not appear here. A type-only mismatch (not an
    // `UncheckedExpression`) rejects early.
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
        auto* o = dynamic_cast<UncheckedExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar instance member (the
    // `UncheckedKeyword` const string is a static literal, not instance state), so `Clone`
    // copies the annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern) and deep-clones the child through the setter (which re-parents
    // and re-indexes via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are
    // not stored on this node (no own location fields -- the base fields hold the print-time
    // span), so only the child + annotation channel are copied. The `static_cast` uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in
    // this scope). The child is skipped if absent (`Clone` tolerates a missing child even
    // though the slot is required -- the invariant is enforced by `CheckInvariant`, not by
    // `Clone`).
    UncheckedExpression* Clone() const override {
        auto* node = new UncheckedExpression();
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

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNCHECKEDEXPRESSION_HPP
