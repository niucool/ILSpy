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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `PointerReferenceExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.cs (the
// generated `PointerReferenceExpression.g.cs` + the hand-written partial -- the hand-written
// partial declares the `ArrowToken` const and the three slot properties, no ctors, no helpers).
// `pointer_member_access ::= expression '->' identifier ( '<' type ( ',' type )* '>' )?`
// (C# grammar 24.6.3) -- a `Target` `Expression`, a `->` separator, a `MemberName` identifier,
// and zero or more `TypeArguments` (`AstType` children).
//
// It is the structural twin of `MemberReferenceExpression` (D247): the SAME `MemberType` (D238)
// shape -- a `Target` single slot + a `MemberName` string-name `[Slot]` over a backing
// `MemberNameToken` + a `TypeArguments` collection -- with the `Target` an `Expression`, the
// base class `Expression`, and no scalar. The only divergences are the class name, the
// `ArrowToken` const string `"->"` (the output-visitor keyword token; `MemberReferenceExpression`
// carries no const token since the `.` separator is implicit in C#), and the visit method
// (`VisitPointerReferenceExpression`). The `MemberName` is a NON-nullable `string` (the C#
// source declares `public partial string MemberName`, not `string?`), so the backing token is a
// REQUIRED (non-nullable) slot (the `MemberReferenceExpression.MemberName` / `MemberType.MemberName`
// precedent): the string getter DEREFS the token (returning `std::string`, not
// `std::optional<std::string>` -- a null token is a half-constructed node that would
// `NullReferenceException` in C#), and the string setter uses `Identifier.Create` (NOT
// `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
// empty name yields a token with an empty `Name`, not a null token). It exercises the
// collection-aware `GetChild`/`SetChild`/`GetChildSlotInfo`/`GetCollectionByKind` dispatch (a
// `Target` single slot at index 0, a `MemberNameToken` single slot at index 1, and a
// `TypeArguments` collection occupying the contiguous range [2, 2 + Count)).
//
// Its generated `DoMatch` has a non-nullable recursive term (the `Target`, dispatched through
// `MatchRequired` -- the D231 [class.access.derived] workaround, since a derived node may not
// call the protected `DoMatch` through a base `Expression*`), a `MatchString` on the member
// name, and a recursive collection term (`this.TypeArguments.DoMatch`); there is no scalar, so
// no plain-equality term. The terms are in `MembersToMatch` order, the source declaration order
// (`Target`, `MemberName`, `TypeArguments`). The `ArrowToken` const string is a static field
// (not instance state), so the generator's `MembersToMatch` (which iterates only instance
// `IPropertySymbol`s) excludes it from the `DoMatch`.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitPointerReferenceExpression(this)`; `PointerReferenceExpression` does not end in
// "AstType", so the generator's visit-method-name default (`targetSymbol.Name`) yields
// `VisitPointerReferenceExpression`. The generated slot statics are `TargetSlot` (a
// `CSharpSlotInfo<Expression>` pointing at `Slots.TargetExpression`, required -- the `Target`
// `Expression` is non-nullable), `MemberNameTokenSlot` (a `CSharpSlotInfo<Identifier>` pointing
// at `Slots.Identifier`, required -- the `MemberName` string is non-nullable so the token is a
// required slot), and `TypeArgumentsSlot` (a `CSharpSlotInfo<AstType>` pointing at
// `Slots.TypeArgument`, collection). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): deep-clones the `Target`
// and the `MemberNameToken` through the setters (which re-parent), deep-clones every type
// argument through `Add` (which re-parents and re-indexes), and copies the annotation channel.
//
// NO C++ name-shadowing crux (the `MemberReferenceExpression` D247 / `MemberType` D238
// differently-named-property precedent): the `[Slot("Identifier")] public partial string
// MemberName` names the slot KIND "Identifier" but the PROPERTY "MemberName", so the string
// accessor is `MemberName()` (not `Identifier()`), and the backing token accessor is
// `MemberNameToken()` (not `IdentifierToken()`). Neither shadows the `Identifier` CLASS in this
// class scope (no member is named `Identifier`); the `Target`/`TypeArguments` accessors do not
// collide with the `Expression`/`AstType` base types (no member is named `Expression` or
// `AstType` -- the `Target` accessor is `Target()`, not `Expression()`). So NO
// elaborated-type-specifier (`class Expression` / `class Identifier` / `class AstType`) is needed
// anywhere, and the `Identifier::Create` factory call in the `MemberName` setter is unqualified
// (the `Identifier` class is found in the enclosing namespace). The discriminator: the
// shadowing happens only when a member's NAME collides with a CLASS IN THE SAME NAMESPACE;
// here no member is named `Identifier`/`Expression`/`AstType`, so none shadows.
//
// The collection ctors that take type arguments (`PointerReferenceExpression(Expression, string,
// IEnumerable<AstType>)` and the `params AstType[]` form) are DEFERRED: they use `AddRange`,
// which lands with the collection convenience mutators (the D222 deferral). The empty + the
// `(Expression, string)` required-prefix ctors cover the construction API; a type-argument list
// is built via `TypeArguments().Add(...)` until `AddRange` lands. `PointerReferenceExpression.cs`
// declares NO hand-written ctors (only the `ArrowToken` const and the three slot properties), so
// the port carries only the generated ctors.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_POINTERREFERENCEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_POINTERREFERENCEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class PointerReferenceExpression : Expression`. `final` (the
// C# `sealed`): no further derivation. The structural twin of `MemberReferenceExpression` (D247):
// the same `MemberType` (D238) shape (a `Target` single slot + a `MemberName` string-name
// `[Slot]` over a backing `MemberNameToken` + a `TypeArguments` collection) with an `Expression`
// target and no scalar, plus the `ArrowToken` const string `"->"` (the output-visitor keyword
// token the C# `public const string ArrowToken = "->"` declares).
class PointerReferenceExpression final : public Expression {
public:
    ~PointerReferenceExpression() override = default;

    // The `->` keyword token (the C# `public const string ArrowToken = "->"`), a static field
    // carrying the pointer-member-access separator the output visitor emits. A static field
    // (not instance state), so it is NOT in `MembersToMatch`/`DoMatch` (the generator iterates
    // only instance `IPropertySymbol`s), matching the `CheckedExpression.CheckedKeyword` (D234)
    // / `ThrowExpression.ThrowKeyword` (D235) precedent.
    static constexpr const char* ArrowToken = "->";

    // The generated empty ctor (the C# `public PointerReferenceExpression()`). The
    // `TypeArguments` collection is a member (the D222 always-present-stack-member design),
    // initialized here with `baseIndex = 2` (the `Target` single slot at index 0 plus the
    // `MemberNameToken` single slot at index 1 precede it) and `supportsIncremental = true`
    // (it is the node's only collection and its last slot, so an element's flattened
    // `ChildIndex` is exactly `2 + its local position`). The `Target` and `MemberNameToken`
    // default to null (no target, no name) via their default member initializers; both are
    // required slots, so a default-constructed node is only valid until the target and name are
    // set (or until `DoMatch`/`CheckInvariant` observe the missing slots).
    PointerReferenceExpression() : typeArguments_(this, &TypeArgumentsSlot, 2, true) {}

    // The generated required-prefix ctor (the C# `public PointerReferenceExpression(Expression
    // target, string memberName)`): the required prefix runs through the last non-optional
    // ctor param (`Target` and `MemberName` are both required; `TypeArguments` is an optional
    // collection). Sets `Target` then `MemberName` in declaration order (the generator's ctor
    // body emits the assignments in `CtorParams` order, which is the source declaration order).
    // Delegates to the empty ctor so the collection member is initialized.
    PointerReferenceExpression(Expression* target, std::string memberName)
        : PointerReferenceExpression() {
        Target(target);
        MemberName(std::move(memberName));
    }

    // ---- The `Target` slot (a single REQUIRED `Expression` child) ----------------------
    // The generated `[Slot("TargetExpression")] public partial Expression Target` -- a single
    // non-nullable `Expression` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. No name shadowing (the `Target()` accessor does not collide with the
    // `Expression` base type -- no member is named `Expression`), so the operand type is the
    // plain `Expression` (no elaborated specifier, unlike `CastExpression` whose `Expression()`
    // accessor shadows the base type).
    Expression* Target() const { return target_; }
    void Target(Expression* value) {
        SetChildNode(target_, value, 0);
    }

    // ---- The `MemberNameToken` slot (the backing token of the member name) ------------
    // The generated backing `Identifier` token of the string-name `[Slot]` -- a single
    // NON-nullable `Identifier` slot at flattened index 1 (the C# `MemberName` is `string`,
    // not `string?`, so the token is a REQUIRED slot, unlike `SimpleType.IdentifierToken` which
    // is optional). The const-index `SetChildNode(ref field, value, 1)` setter (only the
    // `Target` single slot precedes it, no collection) re-parents and re-indexes in place. No
    // name shadowing (the `MemberNameToken()` accessor is not named `Identifier`), so the token
    // type is the plain `Identifier`.
    Identifier* MemberNameToken() const { return memberNameToken_; }
    void MemberNameToken(Identifier* value) {
        SetChildNode(memberNameToken_, value, 1);
    }

    // ---- The `MemberName` string-name accessor (over the token) ----------------------
    // The generated `public partial string MemberName` -- a convenience string over the
    // `MemberNameToken` slot. A NON-optional name (the C# `string`, not `string?`): `get`
    // returns `MemberNameToken.Name` (deref the token -- a null token is a half-constructed
    // node and would `NullReferenceException` in C#, so the port derefs faithfully); `set`
    // creates the token via `Identifier.Create(value)` (NOT `CreateIfNotEmpty` -- a non-nullable
    // name creates a token even for an empty string, so an empty name yields a token with an
    // empty `Name`, not a null token). `MemberName()` returns `std::string` (a copy of the
    // token's name); the `Identifier::Create` factory call is unqualified (no member shadows
    // the `Identifier` class in this scope, unlike `IdentifierExpression` whose `Identifier()`
    // accessor shadows it).
    std::string MemberName() const { return memberNameToken_->Name(); }
    void MemberName(std::string_view value) {
        MemberNameToken(Identifier::Create(std::string(value)));
    }

    // ---- The `TypeArguments` collection slot -------------------------------------------
    // The generated `public partial AstNodeCollection<AstType> TypeArguments` -- the collection
    // of type arguments (a `CSharpSlotInfoT<AstType>` slot at flattened index 2, the node's only
    // collection and last slot). The C# lazily allocates the wrapper (`field ??= new
    // AstNodeCollection<AstType>(...)`); the D222 port makes the collection an always-present
    // stack member, so the accessor returns the member directly (the empty-until-first-Add
    // element-list profile is preserved -- `list_` is empty until the first `Add`).
    AstNodeCollectionT<AstType>& TypeArguments() { return typeArguments_; }
    const AstNodeCollectionT<AstType>& TypeArguments() const { return typeArguments_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TargetSlot` (a `CSharpSlotInfo<Expression>` pointing at `Slots.TargetExpression`,
    // required -- the `Target` `Expression` is non-nullable); the `MemberNameTokenSlot` (a
    // `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`, required -- the `MemberName`
    // string is non-nullable so the token is a required slot); the `TypeArgumentsSlot` (a
    // `CSharpSlotInfo<AstType>` pointing at `Slots.TypeArgument`, collection). No name
    // shadowing (`Expression`/`Identifier`/`AstType` resolve to the classes -- no member is
    // named `Expression`/`Identifier`/`AstType`). All three kinds are already ported
    // (`TargetExpression` by `MemberReferenceExpression`, `Identifier`/`TypeArgument` by
    // `SimpleType`); no new `Slots` constant is added.
    static inline const CSharpSlotInfoT<Expression> TargetSlot{"Target", false, &Slots::TargetExpression, false};
    static inline const CSharpSlotInfoT<Identifier> MemberNameTokenSlot{"MemberNameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<AstType> TypeArgumentsSlot{"TypeArguments", true, &Slots::TypeArgument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitPointerReferenceExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitPointerReferenceExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitPointerReferenceExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitPointerReferenceExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A `Target` single slot at index 0, a `MemberNameToken` single slot at index 1, and a
    // `TypeArguments` collection occupying the contiguous range [2, 2 + Count). `GetChildCount`
    // is `2 + Count` (the two single slots plus the collection's current length); `GetChild`/
    // `SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a running
    // index (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections`
    // shape -- two single cases then a collection step). `GetCollectionByKind` returns the
    // `TypeArguments` collection for the `TypeArgument` kind (the node's only collection).

    int GetChildCount() const override { return 2 + typeArguments_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return target_;
        i--;
        if (i == 0)
            return memberNameToken_;
        i--;
        int n = typeArguments_.Count();
        if (i < n)
            return typeArguments_.At(i);
        throw std::out_of_range("PointerReferenceExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(target_, static_cast<Expression*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(memberNameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        int n = typeArguments_.Count();
        if (i < n) {
            typeArguments_.SetAt(i, static_cast<AstType*>(value));
            return;
        }
        throw std::out_of_range("PointerReferenceExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TargetSlot;
        i--;
        if (i == 0)
            return &MemberNameTokenSlot;
        i--;
        int n = typeArguments_.Count();
        if (i < n)
            return &TypeArgumentsSlot;
        throw std::out_of_range("PointerReferenceExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::TypeArgument)
            return &typeArguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is PointerReferenceExpression o && this.Target.DoMatch(o.Target, match) &&
    // MatchString(this.MemberName, o.MemberName) && this.TypeArguments.DoMatch(o.TypeArguments,
    // match)`. The terms are in `MembersToMatch` order, which is the source declaration order
    // (`Target`, `MemberName`, `TypeArguments`). The `Target` term is a non-nullable recursive
    // child, so the generator emits a DIRECT `this.Target.DoMatch(o.Target, match)` -- ported
    // through `MatchRequired` (the D231 [class.access.derived] workaround, since a derived node
    // may not call the protected `DoMatch` through a base `Expression*`); the `MemberName` term
    // is a `MatchString` (a `String` term); the `TypeArguments` term is the collection recursive
    // match (the generator emits the collection-typed recursive term directly, NOT
    // `MatchOptional`, which the generator emits only for a nullable non-collection child). A
    // type-only mismatch (not a `PointerReferenceExpression`) rejects early.
    //
    // The `MemberName()` calls are INLINED in the `MatchString` arguments (not pre-computed in
    // locals) so the C# `&&` short-circuit is preserved: `o->MemberName()` derefs the
    // candidate's token, which is only reached when the `Target` term already passed (matching
    // the C#, which would `NullReferenceException` on a half-constructed candidate only if the
    // prior term passed -- the `MemberName` D238 precedent; a nameless candidate is UB/NRE in
    // both languages, so the tests never pass one). The `std::string` temporaries live until
    // the end of the full `return` expression, so the `std::string_view` views are valid for the
    // `MatchString` call. `MemberName` is non-nullable, so the `std::optional<std::string_view>`
    // is always engaged (a real name, never `nullopt`).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<PointerReferenceExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(target_, o->target_, match)
            && PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(MemberName())),
                   std::optional<std::string_view>(std::string_view(o->MemberName())))
            && typeArguments_.DoMatch(o->typeArguments_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Target`
    // deep-cloned through the setter (which re-parents; `Expression::Clone()` returns
    // `Expression*`, the covariant override, which `Target(Expression*)` accepts directly),
    // the `MemberNameToken` deep-cloned through the setter (`Identifier::Clone()` returns
    // `Identifier*`, which `MemberNameToken(Identifier*)` accepts directly), and every
    // `TypeArguments` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AstType::Clone()` returns `AstType*`, the covariant override, which `Add(AstType*)`
    // accepts directly). The `MemberName` string is derived from the token, so cloning the token
    // carries it. No own location fields (`StartLocation`/`EndLocation` are the print-time base
    // fields set by the unported output visitor), so they are not copied (the
    // ConditionalExpression/SimpleType/MemberType/MemberReferenceExpression precedent).
    PointerReferenceExpression* Clone() const override {
        auto* node = new PointerReferenceExpression();
        node->CloneAnnotationsFrom(*this);
        if (target_ != nullptr)
            node->Target(target_->Clone());
        if (memberNameToken_ != nullptr)
            node->MemberNameToken(memberNameToken_->Clone());
        for (int i = 0; i < typeArguments_.Count(); i++)
            node->typeArguments_.Add(typeArguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `target_` is null until the target is set (a required slot --
    // `CheckInvariant` asserts it is filled); `memberNameToken_` is null until the name is set
    // (a required slot); `typeArguments_` is the always-present collection member (empty until
    // the first `Add`). No name shadowing (no member is named `Expression`/`Identifier`/
    // `AstType`), so the field types are the plain classes.
    Expression* target_ = nullptr;
    Identifier* memberNameToken_ = nullptr;
    AstNodeCollectionT<AstType> typeArguments_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_POINTERREFERENCEEXPRESSION_HPP
