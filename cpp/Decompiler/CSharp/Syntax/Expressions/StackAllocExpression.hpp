// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation, rights to use, copy, modify, merge,
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

// Port of the `StackAllocExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.cs (the generated
// `StackAllocExpression.g.cs`; the hand-written partial declares only the three slot
// properties and the `StackallocKeyword` const, no ctors, no helpers). The next in-order
// Phase-5 piece per the D301 plan ("the remaining Expression nodes: ... StackAllocExpression
// ...").
//
// `stackalloc_expression ::= 'stackalloc' type '[' expression ']' | 'stackalloc' type? '['
// expression? ']' array_initializer` (C# grammar section 12.8.22): a sealed `Expression` with
// three single, NULLABLE `[Slot]` children -- a `Type` `AstType?` (the element type, optional
// in the second production), a `CountExpression` `Expression?` (the element count, optional
// when an array_initializer supplies the size), and an `Initializer`
// `ArrayInitializerExpression?` (the optional trailing array_initializer) -- and NO scalar
// enum. The generator emits three typed slot statics (`TypeSlot` pointing at the shared
// `Slots::Type` kind, `CountExpressionSlot` pointing at the shared `Slots::Expression` kind,
// `InitializerSlot` pointing at the shared `Slots::Initializer` kind -- all three already
// ported: `Slots::Type` by `Attribute`, `Slots::Expression` by `UnaryOperatorExpression`,
// `Slots::Initializer` by `ObjectCreateExpression`/`ArrayCreateExpression` as the
// cycle-broken kind in `ArrayInitializerExpression.hpp` -- so no new `Slots` constant), the
// const-index `SetChildNode(ref field, value, index)` setters (no collection precedes any
// slot, so each flattened index is the constant slot position 0/1/2), the `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the three single slots, and the
// `DoMatch` `return other is StackAllocExpression o && MatchOptional(this.Type, o.Type,
// match) && MatchOptional(this.CountExpression, o.CountExpression, match) &&
// MatchOptional(this.Initializer, o.Initializer, match)`. All three children are NULLABLE
// recursive, so the generator emits `MatchOptional` for each (both absent, or both present
// and the pattern's own `DoMatch` decides); there is no scalar enum, so there is no
// `Any`-wildcard term. `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`);
// the port overrides it (no `MemberwiseClone`): deep-clones the three children through the
// setters (which re-parent, each skipped if absent) and copies the annotation channel. There
// is no scalar member, so `Clone` copies no scalar.
//
// NO C++ name-shadowing crux (unlike `CastExpression`/`WithInitializerExpression`): the C#
// property `CountExpression` of type `Expression?` has a property name DIFFERENT from its type
// (`CountExpression`, not `Expression` -- the `[Slot("Expression")]` argument names the slot
// KIND `Expression`, but the property is `CountExpression`), so the `CountExpression()`
// accessor does NOT collide with the `Expression` base type in this class scope (the D238
// `MemberType.MemberName` differently-named-property precedent). Likewise `Type()` does not
// collide (no class named `Type` lives in the `Syntax` namespace -- there is `AstType`, not
// `Type` -- the D240 `Attribute` precedent) and `Initializer()` does not collide (no class
// named `Initializer` -- the D266 `VariableInitializer.Initializer` precedent). So NO
// elaborated-type-specifier is needed anywhere, and the plain `Expression`/`AstType`/
// `ArrayInitializerExpression` operand types are used throughout.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_STACKALLOCEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_STACKALLOCEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class StackAllocExpression : Expression`. `final`
// (the C# `sealed`): no further derivation. The `[DecompilerAstNode]` (no arg) means
// `hasPatternPlaceholder` defaults false (no `PatternPlaceholder` nested class) and
// `NeedsVisitor` is `!IsAbstract && base.IsAbstract` = true (the `Expression` base is
// abstract), so the generator emits the `AcceptVisitor` override + the `Visit` method (no
// pattern placeholder). Three single, NULLABLE `[Slot]` children (`Type` `AstType?` +
// `CountExpression` `Expression?` + `Initializer` `ArrayInitializerExpression?`), no scalar.
class StackAllocExpression final : public Expression {
public:
    ~StackAllocExpression() override = default;

    // The `stackalloc` keyword token the output visitor emits (a static field, not instance
    // state, so the generator's `MembersToMatch` -- which iterates only instance
    // `IPropertySymbol`s -- excludes it from the `DoMatch`). The D234 `static constexpr const
    // char*` convention.
    static constexpr const char* StackallocKeyword = "stackalloc";

    // The generated empty ctor (the C# `public StackAllocExpression()`). All three slots
    // default to null (no type, no count, no initializer). All three slots are nullable, so a
    // default-constructed node is invariant-valid (no required slot is empty).
    StackAllocExpression() = default;

    // The generated all-params ctor (the C# `public StackAllocExpression(AstType? type,
    // Expression? countExpression, ArrayInitializerExpression? initializer)`). Delegated to
    // the empty ctor then the setters so the slot machinery re-parents and re-indexes the
    // children. There is no scalar enum, so the three slot children are the only ctor params.
    // All three are nullable, so `RequiredConstructorPrefixLength` is 0 and
    // `ConstructorPrefixLengths` is `{3}` (just the empty prefix, then the full set) -- the
    // generator emits a single full ctor (no shorter prefix ctor and no `params` overload,
    // since no collection slot is present).
    StackAllocExpression(AstType* type, Expression* countExpression,
                         ArrayInitializerExpression* initializer)
        : StackAllocExpression() {
        Type(type);
        CountExpression(countExpression);
        Initializer(initializer);
    }

    // The C# `[Slot("Type")] AstType? Type` -- a single, NULLABLE `AstType` child at flattened
    // index 0. The generator emits the const-index `SetChildNode(ref field, value, 0)` setter
    // (no collection precedes it). No name shadowing (no class named `Type` lives in the
    // `Syntax` namespace -- there is `AstType`, not `Type` -- the D240 `Attribute` precedent),
    // so the operand type is the plain `AstType`.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // The C# `[Slot("Expression")] Expression? CountExpression` -- a single, NULLABLE
    // `Expression` child at flattened index 1. The slot KIND is `Expression` (the `[Slot]`
    // argument); the property name is `CountExpression`, so the per-node slot static is
    // `CountExpressionSlot` (the generator names the static `{PropertyName}Slot`). The
    // generator emits the const-index `SetChildNode(ref field, value, 1)` setter. No name
    // shadowing (the `CountExpression()` accessor does not collide with the `Expression` base
    // type -- the property name differs from its type, the D238 differently-named-property
    // precedent), so the operand type is the plain `Expression` (no elaborated
    // `class Expression` specifier is needed, unlike `CastExpression`/`WithInitializerExpression`
    // whose `Expression()` accessor shadows the base type).
    Expression* CountExpression() const { return countExpression_; }
    void CountExpression(Expression* value) {
        SetChildNode(countExpression_, value, 1);
    }

    // The C# `[Slot("Initializer")] ArrayInitializerExpression? Initializer` -- a single,
    // NULLABLE `ArrayInitializerExpression` child at flattened index 2. The generator emits
    // the const-index `SetChildNode(ref field, value, 2)` setter (no collection precedes it).
    // No name shadowing (the `Initializer()` accessor does not collide with the
    // `ArrayInitializerExpression` type -- no class named `Initializer` lives in the `Syntax`
    // namespace, the D266 `VariableInitializer.Initializer` precedent), so the operand type
    // is the plain `ArrayInitializerExpression`.
    ArrayInitializerExpression* Initializer() const { return initializer_; }
    void Initializer(ArrayInitializerExpression* value) {
        SetChildNode(initializer_, value, 2);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. All three
    // kinds are already ported (`Slots::Type` by `Attribute`, `Slots::Expression` by
    // `UnaryOperatorExpression`, `Slots::Initializer` by `ObjectCreateExpression`/
    // `ArrayCreateExpression` as the cycle-broken kind in `ArrayInitializerExpression.hpp`),
    // so no new `Slots` constant is added. The `IsOptional` flag is true for each (the slots
    // are nullable -- the C# properties are `AstType?`/`Expression?`/
    // `ArrayInitializerExpression?`); the kind carries identity only. No elaborated
    // element-type specifiers are needed (none of the accessors collide with a class in the
    // `Syntax` namespace).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, true};
    static inline const CSharpSlotInfoT<Expression> CountExpressionSlot{"CountExpression", false, &Slots::Expression, true};
    static inline const CSharpSlotInfoT<ArrayInitializerExpression> InitializerSlot{"Initializer", false, &Slots::Initializer, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitStackAllocExpression` (the class name does not end in "AstType", so
    // the generator's visit-method-name default yields `VisitStackAllocExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitStackAllocExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitStackAllocExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitStackAllocExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Three single slots at flattened indices 0/1/2 (`Type`/`CountExpression`/`Initializer`);
    // no collection, so `GetChildCount` is the constant 3 and `GetChild`/`SetChild`/
    // `GetChildSlotInfo` are a flat index switch (the generator's `WriteReturnDispatchSwitch`
    // shape, with three cases).

    int GetChildCount() const override { return 3; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return countExpression_;
            case 2: return initializer_;
            default: throw std::out_of_range("StackAllocExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(countExpression_, static_cast<Expression*>(value), 1); break;
            case 2: SetChildNode(initializer_, static_cast<ArrayInitializerExpression*>(value), 2); break;
            default: throw std::out_of_range("StackAllocExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &CountExpressionSlot;
            case 2: return &InitializerSlot;
            default: throw std::out_of_range("StackAllocExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is StackAllocExpression o && MatchOptional(this.Type, o.Type, match) &&
    // MatchOptional(this.CountExpression, o.CountExpression, match) &&
    // MatchOptional(this.Initializer, o.Initializer, match)`. All three children are NULLABLE
    // recursive, so the generator emits `MatchOptional` for each (both absent, or both present
    // and the pattern's own `DoMatch` decides); there is no scalar enum, so there is no
    // `Any`-wildcard term. A type-only mismatch (not a `StackAllocExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<StackAllocExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(type_, o->type_, match)
            && MatchOptional(countExpression_, o->countExpression_, match)
            && MatchOptional(initializer_, o->initializer_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the three children through the setters (which re-parent and
    // re-index via `SetChildNode`, each skipped if absent). The print-time
    // `StartLocation`/`EndLocation` are not stored on this node (no own location fields -- the
    // base fields hold the print-time span), so only the children + annotation channel are
    // copied. Each child clone returns the covariant type the matching setter accepts
    // directly (`AstType::Clone()` -> `AstType*` -> `Type(AstType*)`; `Expression::Clone()` ->
    // `Expression*` -> `CountExpression(Expression*)`; `ArrayInitializerExpression::Clone()`
    // -> `ArrayInitializerExpression*` -> `Initializer(ArrayInitializerExpression*)`), so no
    // `static_cast` is needed (no elaborated specifier is in scope).
    StackAllocExpression* Clone() const override {
        auto* node = new StackAllocExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (countExpression_ != nullptr)
            node->CountExpression(countExpression_->Clone());
        if (initializer_ != nullptr)
            node->Initializer(initializer_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. Each pointer is null until the child is set (all three slots are
    // nullable, so a null field is invariant-valid). No elaborated-type-specifiers are needed
    // (none of the accessors collide with a class in the `Syntax` namespace).
    AstType* type_ = nullptr;
    Expression* countExpression_ = nullptr;
    ArrayInitializerExpression* initializer_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_STACKALLOCEXPRESSION_HPP
