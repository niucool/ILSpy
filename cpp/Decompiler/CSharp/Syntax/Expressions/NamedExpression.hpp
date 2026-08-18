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
// OTHERWISE, ARISING FROM, CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of the `NamedExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/NamedExpression.cs (the generated
// `NamedExpression.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D296 plan ("the remaining Expression nodes: AnonymousMethodExpression, LambdaExpression,
// DeclarationExpression, NamedExpression, NamedArgumentExpression, ..."). The
// `named_expression ::= identifier '=' expression`: the `name = value` form used in object
// initializers (member_initializer), anonymous-object members (member_declarator), and named
// attribute arguments (no standalone expression production).
//
// The hand-written partial declares only the two slot properties (no ctors, no helpers, no
// const strings). The `[DecompilerAstNode]` (the default `hasPatternPlaceholder: false`) means
// the node IS `sealed` (no `PatternPlaceholder` derives from it, so `final`). It derives from
// `Expression` (the `Expression` abstract base, D226).
//
// The two slots in source declaration order: a REQUIRED (non-nullable) `string Name` string-name
// `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot at flattened index 0 (the
// `LabelStatement` D259 / `VariableInitializer` D266 / `FixedVariableInitializer` D286
// non-nullable-string-name-`[Slot]` shape -- `Identifier::Create` not `CreateIfNotEmpty`,
// `std::string` return, required token), and a REQUIRED (non-nullable) `Expression Expression`
// `[Slot("Expression")]` single slot at flattened index 1 (the `UnaryOperatorExpression` D231 /
// `CastExpression` D243 / `FixedVariableInitializer` D286 required-`Expression`-slot shape --
// `MatchRequired`). The generator emits the const-index `SetChildNode(ref field, value, index)`
// setters for both (no collection precedes either slot, so each flattened index is the constant
// slot position 0/1); `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo`
// are a flat two-case index switch.
//
// C++ name-shadowing crux (the `CastExpression` D243 / `UnaryOperatorExpression` D231
// `Expression`-of-type-`Expression` precedent): the `[Slot("Expression")] public partial
// Expression Expression` property is named `Expression` of type `Expression`, so the
// `Expression()` accessor (a member function) shadows the `Expression` BASE TYPE in this class
// scope (C++ unqualified name lookup finds the member and stops, even though it is not a type).
// The getter return type precedes the getter's declaration, so the plain `Expression` (the base
// type) is unshadowed there; every type usage AFTER the getter uses the elaborated specifier
// `class Expression` (the setter parameter type, the slot static's element type, the `SetChild`/
// `Clone` `static_cast`, the backing field). The `[Slot("Identifier")]` argument names the slot
// KIND "Identifier" but the PROPERTY is "Name", so the string accessor is `Name()` (NOT
// `Identifier()`) and the backing token accessor is `NameToken()` (NOT `IdentifierToken()`) --
// neither shadows the `Identifier` CLASS in this class scope (no member is named `Identifier`),
// so NO `class Identifier` elaborated specifier is needed and the `Identifier::Create` factory
// call in the `Name` setter is unqualified (the `LabelStatement` D259 / `VariableInitializer` D266
// differently-named-property precedent).
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is NamedExpression o && MatchString(this.Name, o.Name) &&
// this.Expression.DoMatch(o.Expression, match)`. The `Name` term is a `String` `MatchString` (the
// `$any$` wildcard in the pattern's `Name` matches any candidate name); the backing `NameToken`
// is a generated non-`partial` `[Slot]` (not seen by the source-property scan at generation time),
// so it never appears in `MembersToMatch` (no double-match). The `Expression` term is a
// NON-NULLABLE recursive child, so the generator emits the DIRECT `this.Expression.DoMatch(...)`
// term (NOT `MatchOptional`, which it emits only for a nullable recursive child). The C# direct
// dispatch assumes the required child is present; the port routes it through
// `AstNode::MatchRequired` (the same-class static helper) because C++ `[class.access.derived]`
// forbids a derived node from calling the protected `DoMatch` through a base `Expression*` (the
// `UnaryOperatorExpression` D231 / `ExpressionStatement` D255 / `FixedVariableInitializer` D286
// precedent). `MatchRequired` guards a missing operand defensively (a null pattern child does not
// match; the C# would null-deref), and a null candidate child flows through the operand's
// `DoMatch(nullptr)` which returns false. For well-formed nodes (the operand always set) the
// behavior is identical to the C#. A type-only mismatch (not a `NamedExpression`) rejects early.
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule), and the
// `Expression` is non-nullable so it IS in the required prefix -- `RequiredConstructorPrefixLength`
// is 2 (through the last non-optional param `Expression` at index 1), `ConstructorPrefixLengths`
// is {2} (reqLen == cp.Count == 2, no shorter prefix), and there is no collection so no `params`
// overload. The generated ctors are the empty ctor + the `(string name, Expression expression)`
// all-params ctor. The `(string, Expression)` body chains to the empty ctor then sets `Name`
// (which creates the token via `Identifier::Create`) then sets `Expression`. The two-arg ctor is
// NOT `explicit` (a multi-arg ctor is not a converting ctor); the single-arg `(string)` is NOT
// generated (the required prefix IS the full set -- `Expression` is required, so no shorter
// prefix ctor) -- the `FixedVariableInitializer` D286 ctor pattern.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitNamedExpression(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitNamedExpression`). The generated slot statics are `NameTokenSlot` (a
// `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required) and `ExpressionSlot`
// (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, required). NO new `Slots`
// constant: `Slots::Identifier` is already ported (by `SimpleType` D237) and `Slots::Expression`
// is already ported (by `UnaryOperatorExpression` D231). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): a
// fresh node, the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the
// D223 concrete-clone pattern), the `NameToken` deep-cloned through the setter, and the
// `Expression` deep-cloned through the setter (which re-parent and re-index via `SetChildNode`).
// No own location fields (it does not derive `EndLocation`), so they are not copied (the
// `LabelStatement` D259 / `VariableInitializer` D266 / `FixedVariableInitializer` D286
// no-location-copy precedent). The covariant return is `NamedExpression*` (through `Expression*`,
// the `Expression::Clone` pure-virtual). The `Expression::Clone` returns `Expression*` which the
// `Expression(class Expression*)` setter accepts after the elaborated-specifier `static_cast`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_NAMEDEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_NAMEDEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class NamedExpression : Expression`. `final` (the C# `sealed`):
// no further derivation. The non-nullable-string-name-`[Slot]`-plus-a-required-`Expression`-slot
// shape: a required `NameToken` (the backing `Identifier` of the non-nullable `string Name`) at
// flattened index 0 plus a required `Expression` `Expression` at flattened index 1.
class NamedExpression final : public Expression {
public:
    ~NamedExpression() override = default;

    // The generated empty ctor (the C# `public NamedExpression()`). Both slots default to null;
    // both are REQUIRED slots, so a default-constructed node is only valid until the name and
    // expression are set (or until `DoMatch`/`CheckInvariant` observe the missing children) -- the
    // `UnaryOperatorExpression` D231 / `FixedVariableInitializer` D286 required-slot behavior.
    // `CheckInvariant` rejects an empty node (the `NameToken` required-slot invariant fires).
    NamedExpression() = default;

    // The generated all-params ctor (the C# `public NamedExpression(string name, Expression
    // expression)`); both slots are required (non-nullable) so the required prefix IS the full
    // set (reqLen == cp.Count == 2), and this two-arg form is the only parametrized form (no
    // shorter prefix ctor, no `params` overload -- no collection). The body sets `Name` (which
    // creates the token via `Identifier::Create`) then sets `Expression`. NOT `explicit` (a
    // multi-arg ctor is not a converting ctor).
    NamedExpression(std::string name, Expression* expression)
        : NamedExpression() {
        Name(std::move(name));
        Expression(expression);
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
    // slot. A NON-optional name (the C# `string`, not `string?`): `get` returns `NameToken.Name`
    // (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `FixedVariableInitializer.Name`
    // D286 precedent). `Name()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is unqualified (the `Name()` accessor does NOT shadow the
    // `Identifier` class -- no member is named `Identifier`).
    std::string Name() const { return nameToken_->Name(); }
    void Name(std::string_view value) {
        NameToken(Identifier::Create(std::string(value)));
    }

    // ---- The `Expression` slot (the required Expression) ---------------------------------
    // The generated `[Slot("Expression")] public partial Expression Expression` -- a single
    // REQUIRED (non-nullable) `Expression` slot at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it). NAME-SHADOWING
    // crux (the `CastExpression` D243 / `UnaryOperatorExpression` D231 precedent): the
    // `Expression()` accessor (a member function) shadows the `Expression` BASE TYPE in this
    // class scope, so the setter parameter type uses the elaborated specifier `class Expression`
    // (the getter return type precedes the getter's declaration, so the plain `Expression` is
    // unshadowed there).
    Expression* Expression() const { return expression_; }
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // The `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot, `IsOptional=false`).
    // NO name shadowing (`Identifier` resolves to the class -- no member is named `Identifier`),
    // so the element type is the plain `Identifier`. `Slots::Identifier` is already ported (by
    // `SimpleType` D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};

    // The `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
    // required -- the `Expression` `Expression` is non-nullable, so `IsOptional=false`). NAME
    // SHADOWING (the `Expression()` accessor shadows the `Expression` base type in this scope),
    // so the element type uses the elaborated `class Expression`. `Slots::Expression` is already
    // ported (by `UnaryOperatorExpression` D231), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitNamedExpression` (`NamedExpression` does not end in "AstType", so the
    // generator's visit-method-name default yields `VisitNamedExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitNamedExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0 (`NameToken`) and 1 (`Expression`); no collection,
    // so `GetChildCount` is the constant 2 (each slot counts even when its child is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the generator's
    // `WriteReturnDispatchSwitch` shape with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return nameToken_;
            case 1: return expression_;
            default: throw std::out_of_range("NamedExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(nameToken_, static_cast<Identifier*>(value), 0); break;
            case 1: SetChildNode(expression_, static_cast<class Expression*>(value), 1); break;
            default: throw std::out_of_range("NamedExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &NameTokenSlot;
            case 1: return &ExpressionSlot;
            default: throw std::out_of_range("NamedExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is NamedExpression o && MatchString(this.Name, o.Name) &&
    // this.Expression.DoMatch(o.Expression, match)`. The `Name` term is a `String` `MatchString`
    // (the `$any$` wildcard in the pattern's `Name` matches any candidate name); the backing
    // `NameToken` is a generated non-`partial` `[Slot]` (not seen by the source-property scan at
    // generation time), so it never appears in `MembersToMatch` (no double-match). The
    // `Expression` term is a NON-NULLABLE recursive child, so the generator emits the DIRECT
    // `this.Expression.DoMatch(o.Expression, match)` term (NOT `MatchOptional`, which it emits
    // only for a nullable recursive child). The C# direct dispatch assumes the required child is
    // present; the port routes it through `AstNode::MatchRequired` (the same-class static helper)
    // because C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `Expression*` (the `UnaryOperatorExpression` D231 /
    // `ExpressionStatement` D255 / `FixedVariableInitializer` D286 precedent). `MatchRequired`
    // guards a missing operand defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the operand's `DoMatch(nullptr)` which
    // returns false. For well-formed nodes (the operand always set) the behavior is identical to
    // the C#. A type-only mismatch (not a `NamedExpression`) rejects early. `Name()` returns
    // `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per side. `Name` is non-nullable, so
    // the `std::optional<std::string_view>` is always engaged (a real name, never `nullopt` --
    // the `MemberType.MemberName` D238 precedent). The `Name()` calls are INLINED in the
    // `MatchString` arguments (not pre-computed in locals) so the C# `&&` short-circuit is
    // preserved: `o->Name()` derefs the candidate's token only after the type check passed. The
    // `std::string` temporaries live until the end of the full `return` expression, keeping the
    // `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<NamedExpression*>(other);
        if (o == nullptr)
            return false;
        if (!PatternMatching::Pattern::MatchString(
                std::optional<std::string_view>(std::string_view(Name())),
                std::optional<std::string_view>(std::string_view(o->Name()))))
            return false;
        return MatchRequired(expression_, o->expression_, std::move(match));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken`
    // deep-cloned through the setter when present (which re-parents; the cloned token carries its
    // own `Name`), and the `Expression` deep-cloned through the setter when present (which
    // re-parents and re-indexes via `SetChildNode`). No scalar to copy (the `Name` string is
    // derived from the token, so cloning the token carries it). The `NamedExpression` itself has
    // no own location fields (`StartLocation`/`EndLocation` are the print-time base fields set by
    // the unported output visitor), so they are not copied (the `LabelStatement` D259 /
    // `FixedVariableInitializer` D286 no-location-copy precedent). The `Identifier::Clone` returns
    // `Identifier*` which `NameToken(Identifier*)` accepts directly; `Expression::Clone` returns
    // `Expression*` which the `Expression(class Expression*)` setter accepts after the
    // elaborated-specifier `static_cast`. The covariant return is `NamedExpression*` (through
    // `Expression*`, the `Expression::Clone` pure-virtual).
    NamedExpression* Clone() const override {
        auto* node = new NamedExpression();
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `nameToken_` is null until the name is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled. `expression_` is null until the expression is set;
    // the slot is REQUIRED, so `CheckInvariant` asserts it is filled. `nameToken_` is the plain
    // `Identifier` (no shadowing); `expression_` uses the elaborated `class Expression` (the
    // `Expression()` accessor declared above shadows the `Expression` base type in this scope).
    Identifier* nameToken_ = nullptr;
    class Expression* expression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_NAMEDEXPRESSION_HPP
