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

// Port of the `SwitchExpressionSection` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/SwitchExpression.cs (the generated
// `SwitchExpressionSection.g.cs` + the hand-written partial, which declares only the two slot
// properties, no ctors, no helpers). The next in-order Phase-5 piece per the D306 plan (a
// remaining Expression-family node whose dependencies are all ported):
// `switch_expression_arm ::= pattern '=>' expression` (C# grammar 12.12) -- one arm of a
// `switch` expression is a `Pattern` (the `Expression` on the left of `=>`, the C# AST models
// the pattern DSL as `Expression` nodes) and a `Body` (the `Expression` on the right of `=>`).
//
// It derives DIRECTLY from `AstNode` (not `Expression`/`Statement`/`AstType`) -- a switch
// expression arm is a structural node owned by a `SwitchExpression.SwitchSections` collection,
// not an expression itself (the C# `public sealed partial class SwitchExpressionSection :
// AstNode`). It is the `CastExpression` (D243) two-REQUIRED-single-`Expression`-slot shape applied
// to a direct-`AstNode` base: a required `Pattern` `Expression` at flattened index 0 plus a
// required `Body` `Expression` at flattened index 1, both non-nullable. The generator emits the
// const-index `SetChildNode(ref field, value, index)` setters for both (no collection precedes
// either slot, so each flattened index is the constant slot position 0/1); `GetChildCount` is the
// constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat two-case index switch.
//
// NO C++ name-shadowing crux: the `Pattern()` accessor is a member function, but no class named
// `Pattern` lives in the `Syntax` namespace (the `PatternMatching::Pattern` is in the nested
// `PatternMatching` namespace, not found by unqualified lookup in `Syntax`), and no member is
// named `Expression`, so the `Pattern`/`Body` operand types are the plain `Expression` (no
// elaborated specifier, unlike `CastExpression` whose `Expression()` accessor shadows the base
// type -- the `MemberReferenceExpression.Target` D247 differently-named-property precedent).
//
// Two NEW `Slots` kinds land in `Slots.hpp` for this node's two slots (`Slots::Pattern` and
// `Slots::SwitchExpressionBody`, both `CSharpSlotInfoT<Expression>`): the `AstType` abstract
// base does not include `Slots.hpp`, so they live in `Slots.hpp` with no include cycle -- the
// `Slots.Type`/`Slots.Condition` precedent. The `Slots::SwitchExpressionSection` kind (a
// `CSharpSlotInfoT<SwitchExpressionSection>` COLLECTION kind, consumed by
// `SwitchExpression.SwitchSections`) is cycle-broken into THIS header after the class: this
// header includes `Slots.hpp` for the per-node `PatternSlot`/`BodySlot` statics (referencing
// `&Slots::Pattern`/`&Slots::SwitchExpressionBody`), so the kind cannot live in `Slots.hpp` (a
// circular include) and is defined after the `SwitchExpressionSection` class where both
// `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and the concrete type are complete --
// the `Slots::Attribute` (D241)/`Slots::Variable` (D267) cycle-breaking precedent applied to a
// collection kind.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitSwitchExpressionSection(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitSwitchExpressionSection`). The generated slot
// statics are `PatternSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Pattern`,
// required) and `BodySlot` (a `CSharpSlotInfoT<Expression>` pointing at
// `Slots.SwitchExpressionBody`, required). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): a fresh node, the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
// pattern), the `Pattern` deep-cloned through the setter (which re-parents; `Expression::Clone()`
// returns `Expression*`, the covariant override, which `Pattern(Expression*)` accepts directly),
// and the `Body` deep-cloned through the setter (likewise). No own location fields
// (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
// visitor), so they are not copied (the `CastExpression` no-location-copy precedent).
//
// The generated ctors (the generator's `WriteConstructors`): both slots are REQUIRED
// (non-nullable) so `RequiredConstructorPrefixLength == 2 == ctorParams.Count`, and
// `ConstructorPrefixLengths` is {2} (the full count only -- `reqLen` is added, then `cp.Count` is
// added but it is already present; no shorter prefix and no `params` overload since there is no
// collection). The generated ctors are the empty ctor + the `(Expression, Expression)` all-params
// ctor. The all-params ctor is not `explicit` (a multi-arg ctor is not a converting ctor).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SWITCHEXPRESSIONSECTION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SWITCHEXPRESSIONSECTION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class SwitchExpressionSection : AstNode`. `final` (the C#
// `sealed`): no further derivation. The `CastExpression` (D243) two-REQUIRED-single-`Expression`-slot
// shape applied to a direct-`AstNode` base: a required `Pattern` `Expression` at flattened index 0
// plus a required `Body` `Expression` at flattened index 1.
class SwitchExpressionSection final : public AstNode {
public:
    ~SwitchExpressionSection() override = default;

    // The generated empty ctor (the C# `public SwitchExpressionSection()`). Both `Pattern` and
    // `Body` default to null via their default member initializers (no pattern, no body); both are
    // required slots, so a default-constructed node is only valid until both are set (or until
    // `DoMatch`/`CheckInvariant` observe the missing slots) -- the `CastExpression` D243 /
    // `ConditionalExpression` D232 required-slot behavior.
    SwitchExpressionSection() = default;

    // The generated all-params ctor (the C# `public SwitchExpressionSection(Expression pattern,
    // Expression body)`); both slots are REQUIRED so this two-arg form is the only parametrized ctor
    // (no shorter required prefix, no `params` overload). Sets `Pattern` then `Body` in declaration
    // order. Not `explicit` (a multi-arg ctor is not a converting ctor).
    SwitchExpressionSection(Expression* pattern, Expression* body) : SwitchExpressionSection() {
        Pattern(pattern);
        Body(body);
    }

    // ---- The `Pattern` slot (a single REQUIRED `Expression` child) -------------------
    // The generated `[Slot("Pattern")] public partial Expression Pattern` -- a single
    // non-nullable `Expression` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. No name shadowing (the `Pattern()` accessor does not collide with the
    // `Expression` base type -- no member is named `Expression`, and no class named `Pattern` lives
    // in the `Syntax` namespace -- the `PatternMatching::Pattern` is in the nested
    // `PatternMatching` namespace, not found by unqualified lookup), so the operand type is the
    // plain `Expression` (no elaborated specifier, unlike `CastExpression` whose `Expression()`
    // accessor shadows the base type).
    Expression* Pattern() const { return pattern_; }
    void Pattern(Expression* value) {
        SetChildNode(pattern_, value, 0);
    }

    // ---- The `Body` slot (a single REQUIRED `Expression` child) ------------------------
    // The generated `[Slot("SwitchExpressionBody")] public partial Expression Body` -- a single
    // non-nullable `Expression` slot at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it) re-parents and
    // re-indexes in place. No name shadowing (the `Body()` accessor does not collide with the
    // `Expression` base type -- no member is named `Expression`), so the operand type is the plain
    // `Expression` (no elaborated specifier).
    Expression* Body() const { return body_; }
    void Body(Expression* value) {
        SetChildNode(body_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) --------------
    // The `PatternSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Pattern`, required --
    // the `Pattern` `Expression` is non-nullable); the `BodySlot` (a `CSharpSlotInfoT<Expression>`
    // pointing at `Slots.SwitchExpressionBody`, required -- the `Body` `Expression` is
    // non-nullable). No name shadowing (`Expression` resolves to the base class -- no member is
    // named `Expression`).
    static inline const CSharpSlotInfoT<Expression> PatternSlot{"Pattern", false, &Slots::Pattern, false};
    static inline const CSharpSlotInfoT<Expression> BodySlot{"SwitchExpressionBody", false, &Slots::SwitchExpressionBody, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSwitchExpressionSection`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSwitchExpressionSection(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // Two single slots at flattened indices 0/1 (`Pattern`/`Body`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
    // two-case index switch (the generator's `WriteReturnDispatchSwitch` shape).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return pattern_;
            case 1: return body_;
            default: throw std::out_of_range("SwitchExpressionSection::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(pattern_, static_cast<Expression*>(value), 0); break;
            case 1: SetChildNode(body_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("SwitchExpressionSection::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &PatternSlot;
            case 1: return &BodySlot;
            default: throw std::out_of_range("SwitchExpressionSection::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SwitchExpressionSection o && this.Pattern.DoMatch(o.Pattern, match) &&
    // this.Body.DoMatch(o.Body, match)`. Both children are NON-NULLABLE recursive, so the generator
    // emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (NOT
    // `MatchOptional`, which it emits only for a nullable recursive child); there is no scalar
    // enum, so there is no `Any`-wildcard term. A type-only mismatch (not a
    // `SwitchExpressionSection`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing pattern-side child
    // defensively (a null pattern child does not match; the C# would null-deref), and a null
    // candidate child flows through the child's `DoMatch(nullptr)` which returns false.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<SwitchExpressionSection*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(pattern_, o->pattern_, match)
            && MatchRequired(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Pattern`
    // deep-cloned through the setter (which re-parents; `Expression::Clone()` returns
    // `Expression*`, the covariant override, which `Pattern(Expression*)` accepts directly), and
    // the `Body` deep-cloned through the setter (likewise). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `SwitchExpressionSection` does not derive `EndLocation`), so they are not copied
    // (the `CastExpression`/`ConditionalExpression` no-location-copy precedent). The covariant
    // return is `SwitchExpressionSection*` (through `AstNode*`, the `AstNode::Clone` virtual).
    SwitchExpressionSection* Clone() const override {
        auto* node = new SwitchExpressionSection();
        node->CloneAnnotationsFrom(*this);
        if (pattern_ != nullptr)
            node->Pattern(pattern_->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `pattern_` is null until the pattern is set (a required slot --
    // `CheckInvariant` asserts it is filled); `body_` is null until the body is set (a required
    // slot -- `CheckInvariant` asserts it is filled). No name shadowing (no member is named
    // `Expression`), so the field types are the plain class.
    Expression* pattern_ = nullptr;
    Expression* body_ = nullptr;
};

// The `Slots::SwitchExpressionSection` kind -- a COLLECTION of `SwitchExpressionSection` (the
// `SwitchExpression.SwitchSections`, an `AstNodeCollection<SwitchExpressionSection>` -- the
// `switch_expression_arm*` of a `switch` expression's `{ ... }`). A
// `CSharpSlotInfoT<SwitchExpressionSection>` (the element type is the concrete
// `SwitchExpressionSection` node).
//
// Defined HERE (in SwitchExpressionSection.hpp, after the `SwitchExpressionSection` class)
// rather than in Slots.hpp because `CSharpSlotInfoT<SwitchExpressionSection>` needs
// `SwitchExpressionSection` complete (the `dynamic_cast<const SwitchExpressionSection*>` is-a
// test in the ctor), and `SwitchExpressionSection` is a concrete node with per-node slot statics
// (its `PatternSlot`/`BodySlot` reference `&Slots::Pattern`/`&Slots::SwitchExpressionBody`, so
// this header includes Slots.hpp). Placing the kind in Slots.hpp would form a circular include:
// Slots.hpp would have to include SwitchExpressionSection.hpp (for the complete
// `SwitchExpressionSection`), but SwitchExpressionSection.hpp includes Slots.hpp (for
// `Slots::Pattern`/`&Slots::SwitchExpressionBody`), and with Slots.hpp's guard set those
// definitions would not be visible where SwitchExpressionSection's class body needs them. After
// the class both `CSharpSlotInfoT` (visible via the Slots.hpp include) and `SwitchExpressionSection`
// are complete, so the kind defines cleanly. The `inline` variable still has external linkage
// and one address across translation units (the C++17 `inline` guarantee), preserving the
// pointer-identity comparison `node.Slot.Kind == &Slots::SwitchExpressionSection` the slot system
// relies on. This is the `Slots::Attribute`/`Slots::Variable`/`Slots::Constraint` cycle-breaking
// precedent (D241/D267/D283) applied to a collection kind. The shared constant is constructed
// non-collection/non-optional (`{"SwitchExpressionSection", false, nullptr, false}`); the
// per-node `SwitchSectionsSlot` on `SwitchExpression` carries the `IsCollection` flag (the
// collection `[Slot]` makes the per-node slot a collection). The kind name
// `SwitchExpressionSection` collides with the `SwitchExpressionSection` CLASS in the parent
// `Syntax` namespace (the `Expression`/`Identifier`/`Statement` collision pattern): the template
// argument in this definition resolves to the class (the constant being declared is not yet in
// scope at the point its type is parsed), and a LATER `Slots` entry wanting the
// `SwitchExpressionSection` class as its element type must qualify it
// (`::ILSpy::Decompiler::CSharp::Syntax::SwitchExpressionSection`) to avoid resolving to this
// constant.
namespace Slots {
inline const CSharpSlotInfoT<SwitchExpressionSection> SwitchExpressionSection{"SwitchExpressionSection", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SWITCHEXPRESSIONSECTION_HPP
