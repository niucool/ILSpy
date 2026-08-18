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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `DeclarationExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/DeclarationExpression.cs (the generated
// `DeclarationExpression.g.cs`; the hand-written partial declares only the two slot
// properties, no ctors, no helpers, no const strings). The next in-order Phase-5 piece per
// the D302 plan (a remaining Expression node whose dependencies are all ported -- `AstType`
// D236, `VariableDesignation` D264, and `Slots::Type` by `Attribute` D240, `Slots::VariableDesignation`
// by `ParenthesizedVariableDesignation` D264 as a collection kind, reused by `ForeachStatement`
// D265 as a single).
//
// `declaration_expression ::= type variable_designation` (C# grammar 12.20): an `Expression`
// with two single, REQUIRED (non-nullable) `[Slot]` children -- a `Type` `AstType` (the
// declared type) and a `Designation` `VariableDesignation` (the single/tuple designation,
// e.g. `x` or `(x, y)`) -- and NO scalar enum. The `CastExpression` D243 two-required-single-slot
// shape with the second operand a `VariableDesignation` instead of an `Expression`: the
// generator emits two typed slot statics (`TypeSlot` pointing at the shared `Slots::Type` kind,
// `DesignationSlot` pointing at the shared `Slots::VariableDesignation` kind -- both already
// ported, so no new `Slots` constant), the const-index `SetChildNode(ref field, value, index)`
// setters (no collection precedes either slot, so each flattened index is the constant slot
// position 0/1), the `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over
// the two single slots, and the `DoMatch` `return other is DeclarationExpression o &&
// this.Type.DoMatch(o.Type, match) && this.Designation.DoMatch(o.Designation, match)`. Both
// children are NON-NULLABLE recursive, so the generator emits the direct
// `this.{member}.DoMatch(o.{member}, match)` term for each (NOT `MatchOptional`, which the
// generator emits only for a nullable recursive child); there is no scalar enum, so there is
// no `Any`-wildcard term. `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`);
// the port overrides it (no `MemberwiseClone`): deep-clones the two children through the
// setters (which re-parent) and copies the annotation channel. There is no scalar member, so
// `Clone` copies no scalar.
//
// NO C++ name-shadowing crux (the cleanest aspect, unlike `CastExpression` D243 whose
// `Expression()` accessor shadows the `Expression` base type): the C# property `Type` of type
// `AstType` has a name that does not collide with any class in the `Syntax` namespace (there is
// `AstType`, not `Type` -- the `Attribute` D240 precedent), and the C# property `Designation`
// of type `VariableDesignation` has a name that does not collide with the `VariableDesignation`
// class (the property is `Designation`, not `VariableDesignation` -- the `[Slot("VariableDesignation")]`
// argument names the slot KIND, but the property is `Designation`, so the `Designation()` accessor
// does not collide with the `VariableDesignation` class, the `ForeachStatement.InExpression` D265
// / `MemberReferenceExpression.Target` D247 differently-named-property precedent). So NO
// elaborated-type-specifier is needed anywhere, and the plain `AstType`/`VariableDesignation`
// operand types are used throughout -- the per-node slot statics, the `SetChild`/`Clone`
// `static_cast`s, and the backing fields all use the plain types.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_DECLARATIONEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_DECLARATIONEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class DeclarationExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. A two-single-required-slot `Expression` (`Type` `AstType` +
// `Designation` `VariableDesignation`), no scalar.
class DeclarationExpression final : public Expression {
public:
    ~DeclarationExpression() override = default;

    // The generated empty ctor (the C# `public DeclarationExpression()`). Both slots default
    // to null (no type, no designation). Null slots violate the required-slot invariant, so a
    // default-constructed node is only valid until the slots are set (or until `DoMatch`/
    // `CheckInvariant` observe the missing children).
    DeclarationExpression() = default;

    // The generated all-params ctor (the C# `public DeclarationExpression(AstType type,
    // VariableDesignation designation)`). Delegated to the empty ctor then the setters so the
    // slot machinery re-parents and re-indexes the children. The parameter types precede the
    // accessor declarations, so the plain `AstType`/`VariableDesignation` (the base types) are
    // unshadowed here; the body is in complete-class context, so the `Type(type)`/
    // `Designation(designation)` calls resolve to the setters declared below. There is no
    // scalar enum, so the two slot children are the only ctor params (the generator emits a
    // single full ctor -- both are required, so the required prefix IS the full set; no
    // shorter prefix ctor and no `params` overload, since no collection slot is present). The
    // two-arg ctor is explicit (a two-arg ctor is not a converting ctor by default, but the
    // cast/declaration grammars are distinguished by the surrounding parse, not implicit
    // conversion, so `explicit` documents the no-implicit-conversion intent of the faithful
    // generated ctor).
    explicit DeclarationExpression(AstType* type, VariableDesignation* designation)
        : DeclarationExpression() {
        Type(type);
        Designation(designation);
    }

    // The C# `[Slot("Type")] AstType Type` -- a single, REQUIRED (non-nullable) `AstType`
    // child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). The C# getter
    // returns the backing field null-forgiving (`field!`) because the slot is required; the
    // port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type), so callers must keep the child set. No name shadowing (the `Type()` accessor does
    // not collide with any class -- no class named `Type` lives in the `Syntax` namespace,
    // there is `AstType` -- the `Attribute` D240 precedent), so the operand type is the plain
    // `AstType`.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // The C# `[Slot("VariableDesignation")] VariableDesignation Designation` -- a single,
    // REQUIRED (non-nullable) `VariableDesignation` child at flattened index 1. The generator
    // emits the const-index `SetChildNode(ref field, value, 1)` setter (no collection precedes
    // it). The `[Slot("VariableDesignation")]` argument names the shared `Slots::VariableDesignation`
    // KIND, but the PROPERTY is `Designation`, so the `Designation()` accessor does NOT collide
    // with the `VariableDesignation` class (a member named `Designation` is not the name
    // `VariableDesignation` -- the `ForeachStatement.InExpression` D265 / `MemberReferenceExpression.Target`
    // D247 differently-named-property discriminator). No elaborated-type-specifier is needed;
    // the plain `VariableDesignation` is used throughout.
    VariableDesignation* Designation() const { return designation_; }
    void Designation(VariableDesignation* value) {
        SetChildNode(designation_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. Both
    // kinds are already ported (`Slots::Type` by `Attribute` D240, `Slots::VariableDesignation`
    // by `ParenthesizedVariableDesignation` D264 and reused as a single by `ForeachStatement`
    // D265), so no new `Slots` constant is added. The `IsOptional` flag is false for each (the
    // slots are required -- the C# properties are non-nullable); the kind carries identity
    // only. No accessor shadows any class, so both slot element types are the plain
    // `AstType`/`VariableDesignation` (no elaborated specifier).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<VariableDesignation> DesignationSlot{"VariableDesignation", false, &Slots::VariableDesignation, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitDeclarationExpression` (`DeclarationExpression` does not end in
    // "AstType", so the generator's visit-method-name default yields `VisitDeclarationExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitDeclarationExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`Type`/`Designation`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
    // index switch (the generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return designation_;
            default: throw std::out_of_range("DeclarationExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(designation_, static_cast<VariableDesignation*>(value), 1); break;
            default: throw std::out_of_range("DeclarationExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &DesignationSlot;
            default: throw std::out_of_range("DeclarationExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is DeclarationExpression o && this.Type.DoMatch(o.Type, match) &&
    // this.Designation.DoMatch(o.Designation, match)`. Both children are NON-NULLABLE recursive,
    // so the generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for
    // each (NOT `MatchOptional`, which the generator emits only for a nullable recursive
    // child); there is no scalar enum, so there is no `Any`-wildcard term. A type-only
    // mismatch (not a `DeclarationExpression`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is
    // present; the port routes each through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `AstType*`/`VariableDesignation*`. `MatchRequired`
    // guards a missing pattern-side child defensively (a null pattern child does not match;
    // the C# would null-deref), and a null candidate child flows through the child's
    // `DoMatch(nullptr)` which returns false. For well-formed nodes (both children set) the
    // behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<DeclarationExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && MatchRequired(designation_, o->designation_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the two children through the setters (which re-parent and
    // re-index via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not
    // stored on this node (no own location fields -- the base fields hold the print-time
    // span), so only the children + annotation channel are copied. Each child is skipped if
    // absent (`Clone` tolerates a missing child even though the slot is required -- the
    // invariant is enforced by `CheckInvariant`, not by `Clone`). `AstType::Clone()` returns
    // `AstType*` (the covariant override), which `Type(AstType*)` accepts directly;
    // `VariableDesignation::Clone()` returns `VariableDesignation*` (the covariant override),
    // which `Designation(VariableDesignation*)` accepts directly -- no `static_cast` is needed
    // (no accessor shadows any class, so no elaborated-type-specifier cast is required).
    DeclarationExpression* Clone() const override {
        auto* node = new DeclarationExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (designation_ != nullptr)
            node->Designation(designation_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. No accessor shadows any class, so both fields use the plain
    // element types (no elaborated specifier).
    AstType* type_ = nullptr;
    VariableDesignation* designation_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_DECLARATIONEXPRESSION_HPP
