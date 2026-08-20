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

// Port of the `OutVarDeclarationExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.cs (the generated
// `OutVarDeclarationExpression.g.cs` + the hand-written partial, which declares the `OutKeyword`
// const string, the two slot properties, and the hand-written `(AstType, string)` convenience
// ctor). The next in-order Phase-5 piece per the D298 plan (a remaining `Expression` node whose
// dependencies are all ported -- `VariableInitializer` D266 and the `OutKeyword` on
// `DirectionExpression` D235).
//
// `out_var_declaration_expression ::= type variable_initializer` (C# grammar section 12.20): a
// sealed `Expression` with two single, REQUIRED (non-nullable) `[Slot]` children -- an `AstType`
// `Type` (the declared type) at flattened index 0 and a `VariableInitializer` `Variable` (the
// `name = initializer` declarator) at flattened index 1 -- plus the `public const string
// OutKeyword` aliased to `DirectionExpression.OutKeyword` (the canonical `out` literal the
// output visitor emits for the `out` parameter direction). The C# source declares `Type` before
// `Variable`, so `Type` is at flattened index 0 and `Variable` at flattened index 1 (the
// `MembersToMatch`/ctor-param/slot order is the source declaration order -- the `GetMembers()`
// order the generator iterates). The generator emits two typed slot statics (`TypeSlot` pointing
// at the shared `Slots::Type` kind, `VariableSlot` pointing at the cycle-broken `Slots::Variable`
// kind -- both already ported, so no new `Slots` constant), the const-index
// `SetChildNode(ref field, value, index)` setters (no collection precedes either slot, so each
// flattened index is the constant slot position 0/1), the `GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` overrides over the two single slots, and the `DoMatch`
// `return other is OutVarDeclarationExpression o && this.Type.DoMatch(o.Type, match) &&
// this.Variable.DoMatch(o.Variable, match)`. Both children are NON-NULLABLE recursive, so the
// generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each (NOT
// `MatchOptional`, which the generator emits only for a nullable recursive child); there is no
// scalar enum, so there is no `Any`-wildcard term. `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the two children
// through the setters (which re-parent) and copies the annotation channel.
//
// C++ name-shadowing crux (NONE here): the `Type()` accessor does NOT collide with any class in
// the `Syntax` namespace (there is `AstType`, not `Type` -- the `Attribute` D240 lesson), and the
// `Variable()` accessor does NOT collide either (there is `VariableInitializer`/
// `VariableDesignation`, not `Variable` -- the `FixedStatement` D267 / `FieldDeclaration` D273
// differently-named-property precedent), so no elaborated-type-specifier is needed anywhere and
// the `Identifier::Create` factory is NOT referenced (the `Name` string lives on the
// `VariableInitializer` child, not this node). The `OutKeyword` const string is a static literal
// (not instance state), so it is NOT part of `MembersToMatch`/`DoMatch`.
//
// The generated ctors (the generator's `WriteConstructors`): `CtorParams` is `[Type, Variable]`
// (both non-nullable `[Slot]` children, so both are required ctor params); no scalar and no
// collection, so `RequiredConstructorPrefixLength` is 2 (== `cp.Count`), `ConstructorPrefixLengths`
// is `{2}`, and there is no `params` overload. The generated ctors are the empty ctor + the
// `(AstType, VariableInitializer)` all-params ctor; there is no shorter prefix ctor (the required
// prefix IS the full set). The HAND-WRITTEN `(AstType, string)` convenience ctor (the common
// `out type name` shape) creates a `new VariableInitializer(name)` and sets `Variable` to it --
// `VariableInitializer(string)` D266 is already ported, and the slot setter is already ported, so
// the convenience ctor ports now. The two 2-arg ctors -- the generated `(AstType*,
// VariableInitializer*)` and the hand-written `(AstType*, std::string)` -- overload without
// ambiguity because `VariableInitializer*` and `std::string` are distinct non-convertible types
// (the `AssignmentExpression` D230 / `DoWhileStatement` D258 two-ctor-overload precedent -- the
// second param distinguishes the overloads). The hand-written convenience ctor is NOT `explicit`
// (a multi-arg ctor is not a converting ctor); the generated all-params ctor is likewise NOT
// `explicit` (the `IfElseStatement` D258 `(Expression, Statement)` 2-arg precedent). The
// internally-created `VariableInitializer` is `new`-ed and set into the `Variable` slot; the
// port's non-owning model does not delete it on the parent's destruction (the `Clone`-produced-
// subtree leak profile -- the `VariableDeclarationStatement` D270 hand-written-ctor precedent).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the node is hand-translated from the generated output.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_OUTVARDECLARATIONEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_OUTVARDECLARATIONEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class OutVarDeclarationExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The `out_var_declaration_expression ::= type
// variable_initializer` shape: two single, REQUIRED (non-nullable) `[Slot]` children (`Type`
// `AstType` + `Variable` `VariableInitializer`), plus the `OutKeyword` const string aliased to
// `DirectionExpression.OutKeyword`.
class OutVarDeclarationExpression final : public Expression {
public:
    ~OutVarDeclarationExpression() override = default;

    // The C# `public const string OutKeyword = DirectionExpression.OutKeyword` -- the `out`
    // keyword token the output visitor emits (the `out` parameter direction). Aliased to the
    // `OutKeyword` const on `DirectionExpression` (which carries the canonical `out` literal).
    // Ports as a `static constexpr const char*` initialized from `DirectionExpression::OutKeyword`
    // (faithful to the C# alias; the value is the same single source of truth, the D235 / D262
    // `UsingStatement.AwaitKeyword`-aliased-to-`UnaryOperatorExpression::AwaitKeyword` precedent).
    static constexpr const char* OutKeyword = DirectionExpression::OutKeyword;

    // The generated empty ctor (the C# `public OutVarDeclarationExpression()`). Both slots
    // default to null (no type, no variable). Null slots violate the required-slot invariant, so
    // a default-constructed node is only valid until the slots are set (or until `DoMatch`/
    // `CheckInvariant` observe the missing children).
    OutVarDeclarationExpression() = default;

    // The generated all-params ctor (the C# `public OutVarDeclarationExpression(AstType type,
    // VariableInitializer variable)`). Delegated to the empty ctor then the setters so the slot
    // machinery re-parents and re-indexes the children. NOT `explicit` (a multi-arg ctor is not
    // a converting ctor -- the `IfElseStatement` D258 2-arg precedent). The parameter order is
    // the source declaration order (`Type` before `Variable`).
    OutVarDeclarationExpression(AstType* type, VariableInitializer* variable)
        : OutVarDeclarationExpression() {
        Type(type);
        Variable(variable);
    }

    // The HAND-WRITTEN convenience ctor (the C# `public OutVarDeclarationExpression(AstType type,
    // string name)`) -- the common `out type name` shape that creates a single
    // `VariableInitializer(name)` and sets `Variable` to it. The `VariableInitializer(string)`
    // 1-arg ctor is already ported (D266, marked `explicit` -- but a delegating-ctor body may call
    // an `explicit` ctor; the `new VariableInitializer(name)` here is a direct construction,
    // which `explicit` permits). NOT `explicit` (a multi-arg ctor is not a converting ctor). The
    // created `VariableInitializer` is `new`-ed and set into the `Variable` slot; the port's
    // non-owning model does not delete it on the parent's destruction (the
    // `VariableDeclarationStatement` D270 hand-written-ctor leak profile -- the same loose
    // ownership as a `Clone` deep-copy).
    OutVarDeclarationExpression(AstType* type, std::string name)
        : OutVarDeclarationExpression() {
        Type(type);
        Variable(new VariableInitializer(std::move(name)));
    }

    // The C# `[Slot("Type")] public partial AstType Type` -- a single, REQUIRED (non-nullable)
    // `AstType` child at flattened index 0 (the source declares it first, so the generator
    // assigns the const index 0). The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). NO name shadowing
    // (the `Type()` accessor does not collide with the `AstType` base type -- no class named
    // `Type` lives in the `Syntax` namespace, the `Attribute` D240 / `FixedStatement` D267
    // differently-named-property precedent), so the operand type is the plain `AstType`.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // The C# `[Slot("Variable")] public partial VariableInitializer Variable` -- a single,
    // REQUIRED (non-nullable) `VariableInitializer` child at flattened index 1. The generator
    // emits the const-index `SetChildNode(ref field, value, 1)` setter (no collection precedes
    // it). NO name shadowing (the `Variable()` accessor does not collide with the
    // `VariableInitializer` class -- no class named `Variable` lives in the `Syntax` namespace,
    // the `FixedStatement` D267 / `FieldDeclaration` D273 differently-named-property precedent),
    // so the operand type is the plain `VariableInitializer`.
    VariableInitializer* Variable() const { return variable_; }
    void Variable(VariableInitializer* value) {
        SetChildNode(variable_, value, 1);
    }

    // The generated slot statics (per-node), pointing at the shared `Slots` kinds. Both kinds
    // are already ported (`Slots::Type` by `Attribute` D240, `Slots::Variable` cycle-broken into
    // `VariableInitializer.hpp` by `FixedStatement` D267), so no new `Slots` constant is added.
    // The `IsOptional` flag is false for each (the slots are required -- the C# properties are
    // non-nullable); the kind carries identity only. NO name shadowing (`AstType`/
    // `VariableInitializer` resolve to the classes -- no member is named either), so the element
    // types are the plain classes.
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<VariableInitializer> VariableSlot{"Variable", false, &Slots::Variable, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitOutVarDeclarationExpression` (the class name does not end in
    // "AstType", so the generator's visit-method-name default yields
    // `VisitOutVarDeclarationExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitOutVarDeclarationExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitOutVarDeclarationExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitOutVarDeclarationExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1 (`Type`/`Variable`); no collection, so
    // `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
    // index switch (the generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return variable_;
            default: throw std::out_of_range("OutVarDeclarationExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(variable_, static_cast<VariableInitializer*>(value), 1); break;
            default: throw std::out_of_range("OutVarDeclarationExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &VariableSlot;
            default: throw std::out_of_range("OutVarDeclarationExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is OutVarDeclarationExpression o && this.Type.DoMatch(o.Type, match) &&
    // this.Variable.DoMatch(o.Variable, match)`. Both children are NON-NULLABLE recursive, so
    // the generator emits the direct `this.{member}.DoMatch(o.{member}, match)` term for each
    // (NOT `MatchOptional`, which it emits only for a nullable recursive child); there is no
    // scalar enum, so there is no `Any`-wildcard term. A type-only mismatch (not an
    // `OutVarDeclarationExpression`) rejects early.
    //
    // The C# direct dispatch (`this.{member}.DoMatch`) assumes each required child is present;
    // the port routes each through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `AstType*`/`VariableInitializer*`. `MatchRequired` guards a
    // missing pattern-side child defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the child's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (both children set) the behavior is identical to C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<OutVarDeclarationExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match)
            && MatchRequired(variable_, o->variable_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): there is no scalar instance member (the `OutKeyword` const string
    // is a static literal, not instance state), so `Clone` copies the annotation channel
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern) and deep-clones
    // the two children through the setters (which re-parent and re-index via `SetChildNode`). The
    // print-time `StartLocation`/`EndLocation` are not stored on this node (no own location
    // fields -- the base fields hold the print-time span -- the node does not derive
    // `EndLocation`), so only the children + annotation channel are copied. Each child is
    // skipped if absent (`Clone` tolerates a missing child even though the slot is required -- the
    // invariant is enforced by `CheckInvariant`, not by `Clone`). `AstType::Clone()` returns
    // `AstType*` (the covariant override), which `Type(AstType*)` accepts directly;
    // `VariableInitializer::Clone()` returns `VariableInitializer*` (the covariant override),
    // which `Variable(VariableInitializer*)` accepts directly.
    OutVarDeclarationExpression* Clone() const override {
        auto* node = new OutVarDeclarationExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (variable_ != nullptr)
            node->Variable(variable_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. A required slot is non-null only by invariant, so each pointer is
    // null until the child is set. NO name shadowing (`AstType`/`VariableInitializer` resolve to
    // the classes -- no member is named either), so the field types are the plain classes.
    AstType* type_ = nullptr;
    VariableInitializer* variable_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_OUTVARDECLARATIONEXPRESSION_HPP
