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

// Port of the `Interpolation` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.cs (the
// generated `Interpolation.g.cs` + the hand-written partial). The next in-order Phase-5 piece
// per the D308 plan (one of the two `InterpolatedStringContent` concrete subclasses, alongside
// `InterpolatedStringText`). `interpolation ::= '{' expression ( ',' alignment )? ( ':'
// format )? '}'` (C# grammar 12.8.3): the `{expr}` / `{expr,align}` / `{expr,align:format}` arm
// inside an interpolated string -- an `InterpolatedStringContent` with a single REQUIRED
// `[Slot("Expression")] Expression` child (the interpolated expression) and two GET-ONLY
// scalar properties (`Alignment`, a `int` with no setter; `Suffix`, a `string?` with no
// setter) set by the hand-written ctor.
//
// The generated `DoMatch` (over `MembersToMatch`) is the first ported node to combine a
// non-nullable recursive child term with TWO plain/get-only scalar terms. KEY generator
// finding: the per-property `MembersToMatch` scan (DecompilerSyntaxTreeGenerator.cs) adds
// EVERY non-override, non-`[ExcludeFromMatch]` instance property whose type is NOT
// `CSharpTokenNode`/`TextLocation` -- it does NOT check `SetMethod != null`, so a GET-ONLY
// property IS added. So `Alignment` (a `int`, get-only) emits the plain-equality term
// `this.Alignment == o.Alignment` (the `DoMatchTerm` fall-through), and `Suffix` (a `string?`,
// get-only) emits the `MatchString(this.Suffix, o.Suffix)` term (the `String` branch). The
// `Expression` slot emits the non-nullable recursive `this.Expression.DoMatch(o.Expression,
// match)` term (a direct dispatch, NOT `MatchOptional`). So `MembersToMatch` is
// [Expression, Alignment, Suffix] in source declaration order, and the generated `DoMatch` is
// `return other is Interpolation o && this.Expression.DoMatch(o.Expression, match) &&
// this.Alignment == o.Alignment && MatchString(this.Suffix, o.Suffix)`. The `Alignment`/`Suffix`
// get-only properties are NOT ctor params via the generator's scalar rule (the generator adds
// only settable ENUM-typed scalars to `CtorParams`; a `int`/`string` get-only scalar is
// neither), so the generator's `CtorParams` is `[Expression]` only -- but a hand-written ctor
// `(Expression expression, int alignment = 0, string? suffix = null)` sets all three, and it
// coexists with the generated empty + `(Expression)` ctors. In C# overload resolution a call
// `new Interpolation(expr)` picks the generated `(Expression)` (the candidate with fewer
// optional params wins, C# spec 11.6.4); in C++ the two 1-arg candidates
// (`Interpolation(Expression*)` and `Interpolation(Expression*, int=0, optional=...)`) would
// be AMBIGUOUS (C++ has no "fewer optional params" tiebreaker for two non-templates), so the
// port drops the generated `(Expression)` ctor and keeps the empty + the hand-written ctor --
// a faithful equivalent (the hand-written 1-arg reduction sets `Alignment=0`/`Suffix=null`,
// exactly what the generated `(Expression)` would leave them at). Per PORT_PLAN.md section
// 5.2 / decision D1 the concrete node is hand-translated from the generated output. The
// generated `AcceptVisitor` calls `visitor.VisitInterpolation(this)` (the class name does not
// end in "AstType", so the visit-method-name default yields `VisitInterpolation`). The
// generated slot static is `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at
// `Slots.Expression`, required).
//
// C++ name-shadowing crux: the C# property is `Expression` of type `Expression` (a property
// named the same as its type -- legal in C#). The faithful port names the accessor
// `Expression()`, but a member function named `Expression` SHADOWS the `Expression` base type
// within this class scope (the D231 `UnaryOperatorExpression` / D224 `Annotation<T>()`
// crux). Every type usage AFTER the `Expression()` getter is declared therefore uses the
// ELABORATED-TYPE-SPECIFIER `class Expression`, so the setter parameter, the slot static, the
// `static_cast`s, and the backing field all spell the operand type as `class Expression`. The
// ctor parameter and the getter return type precede the getter's declaration, so they use the
// plain `Expression` (no member function is in scope there yet). `Alignment`/`Suffix` do not
// collide with any class (no class named `Alignment`/`Suffix` lives in the `Syntax` namespace),
// so no elaborated specifier is needed for them.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATION_HPP

#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class Interpolation : InterpolatedStringContent`. `final`
// (the C# `sealed`): no further derivation. The first concrete `InterpolatedStringContent`
// subclass. Carries one required `[Slot("Expression")]` child and two get-only scalars.
class Interpolation final : public InterpolatedStringContent {
public:
    ~Interpolation() override = default;

    // The generated empty ctor (the C# `public Interpolation()`). `Expression` defaults to
    // null (no operand); `Alignment` defaults to 0; `Suffix` defaults to null. A null operand
    // violates the required-slot invariant, so a default-constructed node is only valid until
    // `Expression` is set (or until `DoMatch`/`CheckInvariant` observe the missing child).
    Interpolation() = default;

    // The hand-written ctor (the C# `public Interpolation(Expression expression, int
    // alignment = 0, string? suffix = null)`). Sets `Expression` (the slot, via the setter
    // declared below) then `Alignment`/`Suffix` (the get-only fields, directly -- C# 6 get-only
    // auto-properties are assignable in the ctor body; the port stores them as plain fields).
    // The parameter type precedes the `Expression()` accessor declarations, so the plain
    // `Expression` (the base type) is unshadowed here; the body is in complete-class context,
    // so the `Expression(expression)` call resolves to the setter declared below. The generated
    // `(Expression)` ctor is DROPPED (it would be ambiguous with this ctor's 1-arg reduction
    // in C++ -- the hand-written 1-arg reduction is the faithful equivalent; see the header
    // comment).
    Interpolation(Expression* expression, int alignment = 0, std::optional<std::string> suffix = std::nullopt)
        : Interpolation() {
        Expression(expression);
        alignment_ = alignment;
        suffix_ = std::move(suffix);
    }

    // The C# `[Slot("Expression")] Expression Expression` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 0. The generator emits the
    // const-index `SetChildNode(ref field, value, 0)` setter (the single slot is the first and
    // only slot, no collection precedes it), so the index is assigned directly and the
    // parent's indices stay valid by construction.
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // The C# `public int Alignment { get; }` -- a get-only `int` scalar (not a `[Slot]`, not
    // settable post-construction). The generator's `MembersToMatch` scan adds it (no
    // settable check) with the plain-equality term; it is NOT a ctor param via the generator's
    // settable-enum rule, but the hand-written ctor sets it. Read-only accessor returning the
    // field (the C# 6 get-only auto-property is set in the ctor and immutable thereafter).
    int Alignment() const { return alignment_; }

    // The C# `public string? Suffix { get; }` -- a get-only nullable `string` scalar (not a
    // `[Slot]`, not settable post-construction). The generator's `MembersToMatch` scan adds it
    // with the `MatchString` term; the hand-written ctor sets it. Ported as an
    // `std::optional<std::string>` field so null and empty stay distinct (the C# `string?`
    // `null` vs `""`; `Pattern::MatchString` treats `nullopt` and `""` distinctly, the D219
    // faithful nullable-string semantics). Read-only accessor returning the field.
    std::optional<std::string> Suffix() const { return suffix_; }

    // The generated slot static (per-node), pointing at the shared `Slots::Expression` kind
    // (ported by `UnaryOperatorExpression` D231). The `IsOptional` flag is false (the slot is
    // required -- the C# property is non-nullable); the kind carries identity only. The
    // element type uses the elaborated `class Expression` (the `Expression()` accessor shadows
    // the base type in this scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitInterpolation`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitInterpolation(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitInterpolation`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitInterpolation(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so
    // `GetChildCount` is the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
    // flat index switch (the generator's `WriteReturnDispatchSwitch` shape, with a single
    // case). The `Alignment`/`Suffix` scalars are NOT `[Slot]` children, so they do not appear
    // in the slot storage.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("Interpolation::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("Interpolation::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("Interpolation::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is Interpolation o && this.Expression.DoMatch(o.Expression, match) &&
    // this.Alignment == o.Alignment && MatchString(this.Suffix, o.Suffix)`. `Expression` is a
    // NON-NULLABLE recursive child, so the generator emits the direct `this.Expression.DoMatch`
    // term (NOT `MatchOptional`); the port routes it through `AstNode::MatchRequired` (the
    // `[class.access.derived]` workaround, the `UnaryOperatorExpression` D231 precedent) -- a
    // derived node may not call the protected `DoMatch` through a base `Expression*`. `Alignment`
    // is a plain `int` (the `DoMatchTerm` fall-through, plain `==`). `Suffix` is a `string?`
    // (the `String` branch, `MatchString`); the port converts the `std::optional<std::string>`
    // field to `std::optional<std::string_view>` per argument (the `SimpleType` D237
    // nullable-string `MatchString` precedent -- one user-defined conversion per argument,
    // since `std::string`->`std::string_view`->`std::optional` is two conversions and C++
    // allows only one implicitly). A type-only mismatch (not an `Interpolation`) rejects
    // early. A null pattern operand does not match (`MatchRequired` guards it defensively; the
    // C# would null-deref); a null candidate operand flows through `DoMatch(nullptr)` which
    // returns false.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<Interpolation*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(expression_, o->expression_, match)
            && alignment_ == o->alignment_
            && PatternMatching::Pattern::MatchString(
                   suffix_.has_value() ? std::optional<std::string_view>(*suffix_) : std::nullopt,
                   o->suffix_.has_value() ? std::optional<std::string_view>(*o->suffix_) : std::nullopt);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): copies the two scalars (`Alignment`/`Suffix`),
    // deep-clones the `Expression` child through the setter (which re-parents and re-indexes
    // via `SetChildNode`), and copies the annotation channel (`CloneAnnotationsFrom` +
    // `ReparentTrivia`, the D223 concrete-clone pattern). No own location fields, so the
    // print-time `StartLocation`/`EndLocation` are not copied. The `static_cast` uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in this
    // scope). The covariant return is `Interpolation*` (through `InterpolatedStringContent*`,
    // the `InterpolatedStringContent::Clone` pure-virtual).
    Interpolation* Clone() const override {
        auto* node = new Interpolation();
        node->alignment_ = alignment_;
        node->suffix_ = suffix_;
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
    int alignment_ = 0;
    std::optional<std::string> suffix_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATION_HPP
