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

// Port of the `FixedVariableInitializer` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/FixedVariableInitializer.cs (the generated
// `FixedVariableInitializer.g.cs` + the hand-written partial, which declares only the two slot
// properties -- no ctors, no helpers, no const strings). The next in-order Phase-5 piece per the
// D285 plan ("the remaining TypeMember hierarchy: FixedFieldDeclaration, FixedVariableInitializer;
// then the OutputVisitor"). The `fixed_size_buffer_declarator ::= identifier '[' expression ']'`
// (C# grammar 24.8.2): the element of a `FixedFieldDeclaration`'s `Variables` collection (a
// `name[count]` pair naming the fixed buffer and its element count), the dependency of
// `FixedFieldDeclaration.Variables` (`AstNodeCollection<FixedVariableInitializer>`).
//
// The hand-written partial declares only the two slot properties (no ctors, no helpers, no const
// strings). The `[DecompilerAstNode]` (the default `hasPatternPlaceholder: false`) means the node
// IS `sealed` (no `PatternPlaceholder` derives from it, so `final`). It derives DIRECTLY from
// `AstNode` (not `EntityDeclaration`/`Expression`/`Statement`/`AstType`) -- a fixed-buffer
// declarator is a structural node owned by a `FixedFieldDeclaration`'s `Variables` collection, not
// a member declaration, so it carries no `SymbolKind`/`Modifiers`/`MatchAttributesAndModifiers`
// (the `VariableInitializer` D266 / `CatchClause` D269 / `ParameterDeclaration` D278
// direct-`AstNode` precedent).
//
// The two slots in source declaration order: a REQUIRED (non-nullable) `string Name` string-name
// `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot at flattened index 0 (the
// `LabelStatement` D259 / `VariableInitializer` D266 non-nullable-string-name-`[Slot]` shape --
// `Identifier::Create` not `CreateIfNotEmpty`, `std::string` return, required token), and a
// REQUIRED (non-nullable) `Expression CountExpression` `[Slot("Expression")]` single slot at
// flattened index 1 (the `UnaryOperatorExpression` D231 / `ExpressionStatement` D255
// required-`Expression`-slot shape -- `MatchRequired`, NOT `MatchOptional` which is only for a
// nullable recursive child; the C# `Expression` type has no `?` so the child is non-nullable). The
// generator emits the const-index `SetChildNode(ref field, value, index)` setters for both (no
// collection precedes either slot, so each flattened index is the constant slot position 0/1);
// `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
// two-case index switch.
//
// NO C++ name-shadowing crux (the `Name()`/`NameToken()`/`CountExpression()` accessors do NOT
// collide with any class in the `Syntax` namespace -- no member is named `Identifier` or
// `Expression`; the `CountExpression` accessor is named `CountExpression`, not `Expression`, so it
// does NOT shadow the `Expression` base type -- the `MemberReferenceExpression.Target` D247
// differently-named-property precedent; the `Name()` accessor is named `Name`, not `Identifier`,
// so it does NOT shadow the `Identifier` class -- the `LabelStatement` D259 /
// `VariableInitializer` D266 differently-named-property precedent), so NO elaborated-type-specifier
// (`class Identifier`/`class Expression`) is needed anywhere, and the `Identifier::Create` factory
// call in the `Name` setter is unqualified.
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is FixedVariableInitializer o && MatchString(this.Name,
// o.Name) && this.CountExpression.DoMatch(o.CountExpression, match)`. The `Name` term is a
// `String` `MatchString` (the `$any$` wildcard in the pattern's `Name` matches any candidate
// name); the backing `NameToken` is a generated non-`partial` `[Slot]` (not seen by the
// source-property scan at generation time), so it never appears in `MembersToMatch` (no
// double-match). The `CountExpression` term is a NON-NULLABLE recursive child, so the generator
// emits the DIRECT `this.CountExpression.DoMatch(o.CountExpression, match)` term (NOT
// `MatchOptional`, which it emits only for a nullable recursive child). The C# direct dispatch
// assumes the required child is present; the port routes it through `AstNode::MatchRequired` (the
// same-class static helper) because C++ `[class.access.derived]` forbids a derived node from
// calling the protected `DoMatch` through a base `Expression*` (the `UnaryOperatorExpression` D231
// / `ExpressionStatement` D255 precedent). `MatchRequired` guards a missing operand defensively (a
// null pattern child does not match; the C# would null-deref), and a null candidate child flows
// through the operand's `DoMatch(nullptr)` which returns false. For well-formed nodes (the operand
// always set) the behavior is identical to the C#. A type-only mismatch (not a
// `FixedVariableInitializer`) rejects early.
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule), and the
// `CountExpression` is non-nullable so it IS in the required prefix --
// `RequiredConstructorPrefixLength` is 2 (through the last non-optional param `CountExpression` at
// index 1), `ConstructorPrefixLengths` is {2} (reqLen == cp.Count == 2, no shorter prefix), and
// there is no collection so no `params` overload. The generated ctors are the empty ctor + the
// `(string name, Expression countExpression)` all-params ctor. The `(string, Expression)` body
// chains to the empty ctor then sets `Name` (which creates the token via `Identifier::Create`)
// then sets `CountExpression`. The two-arg ctor is NOT `explicit` (a multi-arg ctor is not a
// converting ctor); the single-arg `(string)` is NOT generated (the required prefix IS the full
// set -- `CountExpression` is required, so no shorter prefix ctor).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitFixedVariableInitializer
// (this)` (the class name does not end in "AstType", so the generator's visit-method-name default
// yields `VisitFixedVariableInitializer`). The generated slot statics are `NameTokenSlot` (a
// `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required) and
// `CountExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
// required). NO new `Slots` constant: `Slots::Identifier` is already ported (by `SimpleType` D237)
// and `Slots::Expression` is already ported (by `UnaryOperatorExpression` D231). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken` deep-cloned through the
// setter, and the `CountExpression` deep-cloned through the setter (which re-parent and re-index
// via `SetChildNode`). No own location fields (it does not derive `EndLocation`), so they are not
// copied (the `LabelStatement` D259 / `VariableInitializer` D266 no-location-copy precedent). The
// covariant return is `FixedVariableInitializer*` (through `AstNode*`, the `AstNode::Clone`
// virtual). NO elaborated specifiers (no member is named `Identifier` or `Expression`); the
// `Identifier::Clone` returns `Identifier*` which `NameToken(Identifier*)` accepts directly, and
// `Expression::Clone` returns `Expression*` which `CountExpression(Expression*)` accepts directly.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_FIXEDVARIABLEINITIALIZER_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_FIXEDVARIABLEINITIALIZER_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class FixedVariableInitializer : AstNode`. `final` (the C#
// `sealed`): no further derivation. A direct-`AstNode` node (not `EntityDeclaration`), the
// non-nullable-string-name-`[Slot]`-plus-a-required-`Expression`-slot shape: a required `NameToken`
// (the backing `Identifier` of the non-nullable `string Name`) at flattened index 0 plus a
// required `CountExpression` `Expression` at flattened index 1.
class FixedVariableInitializer final : public AstNode {
public:
    ~FixedVariableInitializer() override = default;

    // The generated empty ctor (the C# `public FixedVariableInitializer()`). Both slots default to
    // null; both are REQUIRED slots, so a default-constructed node is only valid until the name and
    // count are set (or until `DoMatch`/`CheckInvariant` observe the missing children) -- the
    // `UnaryOperatorExpression` D231 / `LabelStatement` D259 required-slot behavior.
    // `CheckInvariant` rejects an empty node (the `NameToken` required-slot invariant fires).
    FixedVariableInitializer() = default;

    // The generated all-params ctor (the C# `public FixedVariableInitializer(string name,
    // Expression countExpression)`); both slots are required (non-nullable) so the required prefix
    // IS the full set (reqLen == cp.Count == 2), and this two-arg form is the only parametrized
    // form (no shorter prefix ctor, no `params` overload -- no collection). The body sets `Name`
    // (which creates the token via `Identifier::Create`) then sets `CountExpression`. NOT
    // `explicit` (a multi-arg ctor is not a converting ctor).
    FixedVariableInitializer(std::string name, Expression* countExpression)
        : FixedVariableInitializer() {
        Name(std::move(name));
        CountExpression(countExpression);
    }

    // ---- The `NameToken` slot (the backing `Identifier` token of the name) ---------------
    // The generated `[Slot("Identifier")] public partial Identifier NameToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). NO name shadowing
    // (the `NameToken` accessor does NOT collide with the `Identifier` class -- no member is
    // named `Identifier`), so the element type is the plain `Identifier` and the
    // `Identifier::Create` factory is unqualified (the `LabelStatement` D259 /
    // `VariableInitializer` D266 differently-named-property precedent).
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value, 0);
    }

    // ---- The `Name` string-name accessor (over the token) --------------------------------
    // The generated `public partial string Name` -- a convenience string over the `NameToken`
    // slot. A NON-optional name (the C# `string`, not `string?`): `get` returns
    // `NameToken.Name` (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `VariableInitializer.Name` D266
    // precedent). `Name()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is unqualified (the `Name()` accessor does NOT shadow the
    // `Identifier` class -- no member is named `Identifier`).
    std::string Name() const { return nameToken_->Name(); }
    void Name(std::string_view value) {
        NameToken(Identifier::Create(std::string(value)));
    }

    // ---- The `CountExpression` slot (the required Expression) ----------------------------
    // The generated `[Slot("Expression")] public partial Expression CountExpression` -- a single
    // REQUIRED (non-nullable) `Expression` slot at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it). NO name shadowing
    // (the `CountExpression` accessor is named `CountExpression`, NOT `Expression`, so it does
    // NOT collide with the `Expression` base type -- the `MemberReferenceExpression.Target` D247
    // differently-named-property precedent), so the element type is the plain `Expression`.
    Expression* CountExpression() const { return countExpression_; }
    void CountExpression(Expression* value) {
        SetChildNode(countExpression_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // The `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot, `IsOptional=false`).
    // NO name shadowing (`Identifier` resolves to the class -- no member is named `Identifier`),
    // so the element type is the plain `Identifier`. `Slots::Identifier` is already ported (by
    // `SimpleType` D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};

    // The `CountExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
    // required -- the `CountExpression` `Expression` is non-nullable, so `IsOptional=false`).
    // NO name shadowing (no member is named `Expression`), so the element type is the plain
    // `Expression`. `Slots::Expression` is already ported (by `UnaryOperatorExpression` D231), so
    // no new `Slots` constant.
    static inline const CSharpSlotInfoT<Expression> CountExpressionSlot{"CountExpression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitFixedVariableInitializer` (`FixedVariableInitializer` does not end in
    // "AstType", so the generator's visit-method-name default yields
    // `VisitFixedVariableInitializer`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitFixedVariableInitializer(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitFixedVariableInitializer`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitFixedVariableInitializer(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0 (`NameToken`) and 1 (`CountExpression`); no
    // collection, so `GetChildCount` is the constant 2 (each slot counts even when its child is
    // absent) and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return nameToken_;
            case 1: return countExpression_;
            default: throw std::out_of_range("FixedVariableInitializer::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(nameToken_, static_cast<Identifier*>(value), 0); break;
            case 1: SetChildNode(countExpression_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("FixedVariableInitializer::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &NameTokenSlot;
            case 1: return &CountExpressionSlot;
            default: throw std::out_of_range("FixedVariableInitializer::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is FixedVariableInitializer o && MatchString(this.Name, o.Name) &&
    // this.CountExpression.DoMatch(o.CountExpression, match)`. The `Name` term is a `String`
    // `MatchString` (the `$any$` wildcard in the pattern's `Name` matches any candidate name); the
    // backing `NameToken` is a generated non-`partial` `[Slot]` (not seen by the source-property
    // scan at generation time), so it never appears in `MembersToMatch` (no double-match). The
    // `CountExpression` term is a NON-NULLABLE recursive child, so the generator emits the DIRECT
    // `this.CountExpression.DoMatch(o.CountExpression, match)` term (NOT `MatchOptional`, which it
    // emits only for a nullable recursive child). The C# direct dispatch assumes the required
    // child is present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*` (the `UnaryOperatorExpression` D231 /
    // `ExpressionStatement` D255 precedent). `MatchRequired` guards a missing operand
    // defensively (a null pattern child does not match; the C# would null-deref), and a null
    // candidate child flows through the operand's `DoMatch(nullptr)` which returns false. For
    // well-formed nodes (the operand always set) the behavior is identical to the C#. A type-only
    // mismatch (not a `FixedVariableInitializer`) rejects early. `Name()` returns `std::string`
    // (the token's name); `Pattern::MatchString` takes `std::optional<std::string_view>`, so the
    // view is built per side. `Name` is non-nullable, so the `std::optional<std::string_view>` is
    // always engaged (a real name, never `nullopt` -- the `MemberType.MemberName` D238 precedent).
    // The `Name()` calls are INLINED in the `MatchString` arguments (not pre-computed in locals)
    // so the C# `&&` short-circuit is preserved: `o->Name()` derefs the candidate's token only
    // after the type check passed. The `std::string` temporaries live until the end of the full
    // `return` expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<FixedVariableInitializer*>(other);
        if (o == nullptr)
            return false;
        if (!PatternMatching::Pattern::MatchString(
                std::optional<std::string_view>(std::string_view(Name())),
                std::optional<std::string_view>(std::string_view(o->Name()))))
            return false;
        return MatchRequired(countExpression_, o->countExpression_, std::move(match));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken`
    // deep-cloned through the setter when present (which re-parents; the cloned token carries its
    // own `Name`), and the `CountExpression` deep-cloned through the setter when present (which
    // re-parents and re-indexes via `SetChildNode`). No scalar to copy (the `Name` string is
    // derived from the token, so cloning the token carries it). The `FixedVariableInitializer`
    // itself has no own location fields (`StartLocation`/`EndLocation` are the print-time base
    // fields set by the unported output visitor), so they are not copied (the `LabelStatement`
    // D259 / `VariableInitializer` D266 no-location-copy precedent). NO elaborated specifiers (no
    // member is named `Identifier` or `Expression`); the `Identifier::Clone` returns `Identifier*`
    // which `NameToken(Identifier*)` accepts directly, and `Expression::Clone` returns
    // `Expression*` which `CountExpression(Expression*)` accepts directly.
    FixedVariableInitializer* Clone() const override {
        auto* node = new FixedVariableInitializer();
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        if (countExpression_ != nullptr)
            node->CountExpression(static_cast<Expression*>(countExpression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `nameToken_` is null until the name is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled. `countExpression_` is null until the count is set;
    // the slot is REQUIRED, so `CheckInvariant` asserts it is filled (unlike `VariableInitializer`
    // D266 whose `Initializer` is nullable). NO name shadowing (no member is named `Identifier` or
    // `Expression`), so the field types are the plain `Identifier`/`Expression`.
    Identifier* nameToken_ = nullptr;
    Expression* countExpression_ = nullptr;
};

// The `FixedVariable` kind -- the collection slot kind for every
// `[Slot("FixedVariable")] AstNodeCollection<FixedVariableInitializer>`
// (`FixedFieldDeclaration.Variables`). A `CSharpSlotInfoT<FixedVariableInitializer>` (the element
// type is the concrete `FixedVariableInitializer` node).
//
// Defined HERE (in FixedVariableInitializer.hpp, after the `FixedVariableInitializer` class)
// rather than in Slots.hpp because `CSharpSlotInfoT<FixedVariableInitializer>` needs
// `FixedVariableInitializer` complete (the `dynamic_cast<const FixedVariableInitializer*>` is-a
// test in the ctor), and `FixedVariableInitializer` is a concrete node with per-node slot statics
// (its `NameTokenSlot`/`CountExpressionSlot` reference `&Slots::Identifier`/`&Slots::Expression`,
// so this header includes Slots.hpp). Placing the kind in Slots.hpp would form a circular include:
// Slots.hpp would have to include FixedVariableInitializer.hpp (for the complete
// `FixedVariableInitializer`), but FixedVariableInitializer.hpp includes Slots.hpp (for
// `Slots::Identifier`/`Slots::Expression`), and with Slots.hpp's guard set those definitions
// would not be visible where FixedVariableInitializer's class body needs them. After the class
// both `CSharpSlotInfoT` (visible via the Slots.hpp include) and `FixedVariableInitializer` are
// complete, so the kind defines cleanly. The `inline` variable still has external linkage and one
// address across translation units (the C++17 `inline` guarantee), preserving the
// pointer-identity comparison `node.Slot.Kind == &Slots::FixedVariable` the slot system relies on.
// This is the `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Variable` cycle-breaking
// precedent (D241/D242/D267) applied to a collection kind. The shared constant is constructed
// non-collection/non-optional (`{"FixedVariable", false, nullptr, false}`); the per-node
// `VariablesSlot` on the owning node carries the `IsCollection` flag (the collection `[Slot]` makes
// the per-node slot a collection). The kind name `FixedVariable` collides with no class in the
// `Syntax` namespace (there is `FixedVariableInitializer`/`VariableInitializer`/`VariableDesignation`,
// not `FixedVariable`), so no elaborated-type-specifier is needed.
namespace Slots {
inline const CSharpSlotInfoT<FixedVariableInitializer> FixedVariable{"FixedVariable", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_FIXEDVARIABLEINITIALIZER_HPP
