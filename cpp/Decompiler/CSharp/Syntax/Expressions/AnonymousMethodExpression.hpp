// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `AnonymousMethodExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.cs (the generated
// `AnonymousMethodExpression.g.cs` + the hand-written partial, which declares the two const
// strings and the three slot/scalar properties -- no ctors, no helpers). The next in-order
// Phase-5 piece per the D305 plan (a remaining Expression node whose dependencies are all ported
// -- `Expression` D226, `Slots::Parameter` D279 cycle-broken in `ParameterDeclaration.hpp`, and
// `Slots::Body` D260 cycle-broken in `BlockStatement.hpp`).
//
// `anonymous_method_expression ::= 'async'? 'delegate' parameter* block` (C# grammar 12.22.1):
// a sealed `Expression` (the `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so
// `final` -- no pattern placeholder; `NeedsVisitor` is `!IsAbstract && base.IsAbstract` = true,
// so the generator emits the `AcceptVisitor` override + the `Visit` method). NOT an
// `EntityDeclaration` (derives directly from `Expression`, so there is no
// `MatchAttributesAndModifiers`/`Name`/`ReturnType` base machinery -- unlike the sibling
// `LambdaExpression` D305, the anonymous method carries no `Attributes` collection). Two `[Slot]`
// children in source declaration order:
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` --
//     the anonymous method's parameter list (a COLLECTION at slot 0, the `parameter*` between
//     `(` and `)`; reusing the cycle-broken `Slots::Parameter` kind added by
//     `IndexerDeclaration` D279). The collection is the node's ONLY collection, but it is NOT at
//     the last slot (`Body` trails it), so `supportsIncremental` is FALSE
//     (`collectionCount == 1 && slotIndex == slots.Count - 1` is false -- `slotIndex` 0 !=
//     `slots.Count - 1` 1): every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's
//     indices for a lazy `EnsureChildIndices` rebuild (the `Accessor` D274 / `ComposedType` D242
//     collection-not-at-the-last-slot precedent). `baseIndex` is 0 (the slot index; the generator
//     passes the slot index, not the dynamic flattened index -- unused on the non-incremental fast
//     path).
//   * `[Slot("Body")] public partial BlockStatement Body` -- the anonymous method body (a single
//     REQUIRED `BlockStatement` slot at slot 1; the trailing `block` of the production -- unlike
//     `LambdaExpression` D305 whose `Body` is typed the abstract `AstNode` base because the lambda
//     production takes EITHER a `BlockStatement` OR an `Expression`, the anonymous method
//     production takes ONLY a `block`, so the slot is typed the concrete `BlockStatement`). The
//     slot FOLLOWS the `Parameters` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection). Reusing
//     the cycle-broken `Slots::Body` kind (a `CSharpSlotInfoT<BlockStatement>`, the
//     `CheckedStatement` D260 / `Accessor` D274 / `MethodDeclaration` D284 `BlockStatement`-body
//     precedent).
//
// Plus one non-`[Slot]` scalar: `public bool IsAsync` (the leading `async` modifier; a plain bool
// field, set via the property setter; NOT a ctor param -- the generator adds only settable
// ENUM-typed scalars to `CtorParams`; a bool is in `MembersToMatch` with the fall-through
// plain-equality `DoMatch` term, the `ComposedType.HasRefSpecifier` D242 / `LambdaExpression` D305
// precedent).
//
// Plus two const strings: `public const string DelegateKeyword = "delegate"` (the `delegate`
// keyword token the output visitor emits) and `public const string AsyncModifier =
// LambdaExpression.AsyncModifier` (the leading `async` modifier token, ALIASED to the canonical
// `LambdaExpression.AsyncModifier` source -- the `UnaryOperatorExpression.AwaitKeyword` D262
// canonical-source precedent). Both port as `static constexpr const char*` (static fields, not
// instance state), so the generator's `MembersToMatch` (which iterates only instance
// `IPropertySymbol`s) excludes them from the `DoMatch` (the `BreakStatement` D254 /
// `CheckedExpression.CheckedKeyword` D234 / `LambdaExpression.AsyncModifier` D305 precedent).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): `AnonymousMethodExpression` is
// NOT derived from `EntityDeclaration`, so the explicit `MatchAttributesAndModifiers`/`Name`/
// `ReturnType` additions are skipped; the per-property scan adds the non-override non-
// `[ExcludeFromMatch]` instance properties in source declaration order: `IsAsync` (a bool -> the
// fall-through plain-equality term), `Parameters` (an `AstNodeCollection`, `RecursiveMatch: true`
// -> the collection recursive `DoMatch` term), `Body` (a `BlockStatement`, `RecursiveMatch: true`,
// `Nullable: false` -> the direct `this.Body.DoMatch(o.Body, match)` term, which the port routes
// through `AstNode::MatchRequired` -- the `[class.access.derived]` workaround, the `Accessor` D274
// / `UnaryOperatorExpression` `Expression` D231 precedent). So `MembersToMatch` is
// `[IsAsync, Parameters, Body]`, and the generated `DoMatch` is
// `return other is AnonymousMethodExpression o && this.IsAsync == o.IsAsync &&
// this.Parameters.DoMatch(o.Parameters, match) && this.Body.DoMatch(o.Body, match)`. A type-only
// mismatch (not an `AnonymousMethodExpression`) rejects early.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Parameters (collection, optional),
// Body (single, required)]` (the `IsAsync` bool is NOT a ctor param -- the generator adds only
// settable ENUM-typed scalars). `RequiredConstructorPrefixLength` is 2 (through the last
// non-optional param `Body` at index 1). `ConstructorPrefixLengths` is `{2}` (`reqLen` 2, then no
// collection at-or-after `reqLen` since `Parameters` precedes the required `Body` (its index 0 + 1
// = 1 < `reqLen` 2), then `cp.Count` 2 -- so just the one length). The (len=2) ctor's body calls
// `this.Parameters.AddRange(...)` for the collection (the `AddRange` convenience is the D222
// deferral), so it is DEFERRED; the empty ctor is the only portable ctor. An
// `AnonymousMethodExpression` is built via the empty ctor + `Parameters().Add(...)` + `Body(...)`
// + `IsAsync(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls
// `visitor.VisitAnonymousMethodExpression(this)` (the class name does not end in "AstType", so the
// visit-method-name default yields `VisitAnonymousMethodExpression`). The generated slot statics
// are `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
// collection -- the node's only collection) and `BodySlot` (a `CSharpSlotInfoT<BlockStatement>`
// pointing at `Slots.Body`, required -- the `Body` `BlockStatement` is non-nullable). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `IsAsync` scalar copied, the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), every `Parameters`
// element deep-cloned through `Add` (which re-parents and re-indexes; `ParameterDeclaration::Clone()`
// returns `ParameterDeclaration*`, which `Add(ParameterDeclaration*)` accepts directly), and the
// `Body` deep-cloned through the setter (which re-parents; `BlockStatement::Clone()` returns
// `BlockStatement*`, which `Body(BlockStatement*)` accepts directly -- the slot is typed the
// concrete `BlockStatement`, so no cast, the `Accessor` D274 / `MethodDeclaration` D284 concrete-body
// precedent). No own location fields (does not derive `EndLocation`), so the print-time
// `StartLocation`/`EndLocation` are not copied (the `Accessor` D274 / `ConstructorDeclaration` D281
// no-location-copy precedent). The covariant return is `AnonymousMethodExpression*` (through
// `Expression*`, the `Expression::Clone` pure-virtual).
//
// NO C++ name-shadowing crux (no member is named `ParameterDeclaration`/`BlockStatement` -- the
// `Parameters()`/`Body()`/`IsAsync()` accessors do not collide with any class in the `Syntax`
// namespace, and the `Body` accessor does not collide with the `BlockStatement` type -- a member
// named `Body` is not the name `BlockStatement`), so no elaborated-type-specifier is needed
// anywhere; the plain element types resolve to the classes. This is the `Accessor` D274
// collection-then-required-single-`BlockStatement`-body shape (MINUS the `Attributes` collection and
// the `EntityDeclaration` base machinery) applied to the `Expression` hierarchy with an `IsAsync`
// bool scalar -- structurally a simplified `LambdaExpression` D305 (no `Attributes` collection, a
// concrete `BlockStatement` body instead of an abstract `AstNode` body).
//
// NO new `Slots` constant: `Slots::Parameter` (cycle-broken in `ParameterDeclaration.hpp` D279) and
// `Slots::Body` (cycle-broken in `BlockStatement.hpp` D260) are both already ported.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ANONYMOUSMETHODEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ANONYMOUSMETHODEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class AnonymousMethodExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The `Accessor` D274 collection-then-required-single-`BlockStatement`-
// body shape (MINUS the `Attributes` collection and the `EntityDeclaration` base machinery) applied
// to the `Expression` hierarchy with an `IsAsync` bool scalar -- structurally a simplified
// `LambdaExpression` D305 (no `Attributes` collection, a concrete `BlockStatement` body instead of
// an abstract `AstNode` body, and no `EntityDeclaration` base machinery -- so no
// `MatchAttributesAndModifiers`/`Name`/`ReturnType`).
class AnonymousMethodExpression final : public Expression {
public:
    ~AnonymousMethodExpression() override = default;

    // The generated empty ctor (the C# `public AnonymousMethodExpression()`). The `Parameters`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is the node's ONLY collection but NOT the last slot -- `Body` trails it -- so an
    // element's flattened `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a
    // mutation, the `Accessor` D274 collection-not-at-the-last-slot precedent). `Body` defaults to
    // null (a REQUIRED slot -- `CheckInvariant` asserts it is filled).
    AnonymousMethodExpression() : parameters_(this, &ParametersSlot, 0, false) {}

    // The C# `public const string DelegateKeyword = "delegate"` -- the `delegate` keyword token
    // (the `delegate` of an anonymous method). Ports as a `static constexpr const char*` (a static
    // field, not instance state), so the generator's `MembersToMatch` (which iterates only
    // instance `IPropertySymbol`s) excludes it from the `DoMatch` (the `BreakStatement` D254 /
    // `CheckedExpression.CheckedKeyword` D234 / `LambdaExpression.AsyncModifier` D305 precedent).
    static constexpr const char* DelegateKeyword = "delegate";

    // The C# `public const string AsyncModifier = LambdaExpression.AsyncModifier` -- the `async`
    // modifier token (the leading `async` of an `async` anonymous method), ALIASED to the canonical
    // `LambdaExpression.AsyncModifier` source (preserving the single source of truth, the
    // `UnaryOperatorExpression.AwaitKeyword` D262 canonical-source precedent). Ports as a
    // `static constexpr const char*` (a static field, not instance state), so the generator's
    // `MembersToMatch` excludes it from the `DoMatch` (the same precedent as `DelegateKeyword`).
    static constexpr const char* AsyncModifier = LambdaExpression::AsyncModifier;

    // The C# `public bool IsAsync { get; set; }` -- whether the anonymous method carries a leading
    // `async` modifier (the `async` in `async delegate { ... }`). A plain bool field: not a child
    // slot (no `[Slot]`), not a ctor param (the generator adds only settable ENUM-typed scalars),
    // so it is set via the property setter. It IS in `MembersToMatch` (the generator adds every
    // non-`[Slot]` instance property), and a bool (not an enum, no `Any`, not a string) emits the
    // fall-through plain-equality `DoMatch` term (the `ComposedType.HasRefSpecifier` D242 /
    // `LambdaExpression.IsAsync` D305 precedent). No name shadowing.
    bool IsAsync() const { return isAsync_; }
    void IsAsync(bool value) { isAsync_ = value; }

    // ---- The `Parameters` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the anonymous method's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>`
    // slot at slot 0, the node's ONLY collection, non-incremental -- NOT the last slot). The C#
    // lazily allocates the wrapper; the D222 port makes the collection an always-present stack
    // member, so the accessor returns the member directly. NOT a base virtual (`Expression`
    // declares no `Parameters`; this is a plain non-virtual accessor). Reuses the cycle-broken
    // `Slots::Parameter` kind (added by `IndexerDeclaration` D279). A `const` convenience overload
    // returns `const&` for a `const AnonymousMethodExpression*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Body` slot (a REQUIRED single `BlockStatement`, NOT a base virtual) ----------
    // The generated `[Slot("Body")] public partial BlockStatement Body` -- a single REQUIRED
    // `BlockStatement` slot at slot 1 (the anonymous method body -- the trailing `block` of the
    // production; typed the concrete `BlockStatement` because the anonymous method production takes
    // ONLY a `block`, unlike `LambdaExpression` D305 whose `Body` is the abstract `AstNode` base).
    // The slot FOLLOWS the `Parameters` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)` (the dynamic flattened index after a collection). NOT an
    // override, so a plain non-virtual accessor. `CheckInvariant` asserts the `Body` is filled
    // (the slot is required -- the C# `BlockStatement Body` is non-nullable). Reuses the
    // cycle-broken `Slots::Body` kind (the `CheckedStatement` D260 / `Accessor` D274 /
    // `MethodDeclaration` D284 `BlockStatement`-body precedent). No name shadowing (the `Body()`
    // accessor does not collide with the `BlockStatement` type -- a member named `Body` is not the
    // name `BlockStatement`), so the setter takes a plain `BlockStatement*` (no cast -- the
    // `SetChild` override `static_cast`s the incoming `AstNode*` to `BlockStatement*`, the slot
    // being typed the concrete `BlockStatement`).
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
    // collection -- the node's only collection); `BodySlot` (a `CSharpSlotInfoT<BlockStatement>`
    // pointing at `Slots.Body`, required -- the `Body` `BlockStatement` is non-nullable). NO name
    // shadowing (no member is named `ParameterDeclaration`/`BlockStatement`), so the element types
    // are the plain classes.
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitAnonymousMethodExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitAnonymousMethodExpression(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Two slots in declaration order: a `Parameters` collection at slot 0 (the contiguous range
    // `[0, paramCount)`) and a `Body` single slot at slot 1 (index `paramCount`). `GetChildCount`
    // is `paramCount + 1` (the collection's current length plus the one single slot -- the single
    // slot contributes 1 to the flattened count even when the `Body` is null); `GetChild`/
    // `SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a running
    // index (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections`
    // shape -- a collection step, then a single step). `GetCollectionByKind` returns the
    // `Parameters` collection for the `Parameter` kind. This is the `Accessor` D274
    // collection -> single dispatch shape with the collection FIRST, the `Body` REQUIRED (not
    // nullable as in `Accessor`), and no `EntityDeclaration` base machinery.

    int GetChildCount() const override { return parameters_.Count() + 1; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        if (i == 0)
            return body_;
        throw std::out_of_range("AnonymousMethodExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = parameters_.Count();
            if (i < n) {
                parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(body_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("AnonymousMethodExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        if (i == 0)
            return &BodySlot;
        throw std::out_of_range("AnonymousMethodExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Parameter)
            return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is AnonymousMethodExpression o && this.IsAsync == o.IsAsync &&
    // this.Parameters.DoMatch(o.Parameters, match) && this.Body.DoMatch(o.Body, match)`.
    // `AnonymousMethodExpression` is NOT an `EntityDeclaration`, so there is no
    // `MatchAttributesAndModifiers`/`Name`/`ReturnType` term (unlike the sibling `LambdaExpression`
    // D305, the anonymous method has no `Attributes` collection). The `IsAsync` bool is the
    // fall-through plain-equality term; the `Parameters` term is the collection recursive `DoMatch`
    // (the generator emits a collection-typed recursive term directly, NOT `MatchOptional` -- the
    // `FieldDeclaration.Variables` D273 / `LambdaExpression.Parameters` D305 precedent); the `Body`
    // term is the direct `this.Body.DoMatch(o.Body, match)` for a NON-NULLABLE recursive child,
    // which the port routes through `AstNode::MatchRequired` (the `[class.access.derived]`
    // workaround -- a derived node may not call the protected `DoMatch` through a base
    // `BlockStatement*`; a static member of `AstNode` may, the `MatchOptional`/`MatchRequired`
    // precedent). A type-only mismatch (not an `AnonymousMethodExpression`) rejects early. The
    // terms are in `MembersToMatch` (source declaration) order: `IsAsync`/`Parameters`/`Body`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<AnonymousMethodExpression*>(other);
        if (o == nullptr)
            return false;
        return isAsync_ == o->isAsync_
            && parameters_.DoMatch(o->parameters_, match)
            && MatchRequired(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `IsAsync` scalar copied, the annotation channel
    // copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), every
    // `Parameters` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, which
    // `Add(ParameterDeclaration*)` accepts directly), and the `Body` deep-cloned through the
    // setter (which re-parents; `BlockStatement::Clone()` returns `BlockStatement*`, which
    // `Body(BlockStatement*)` accepts directly -- the slot is typed the concrete `BlockStatement`,
    // so no cast, the `Accessor` D274 / `MethodDeclaration` D284 concrete-body precedent). No own
    // location fields (does not derive `EndLocation`), so the print-time `StartLocation`/
    // `EndLocation` are not copied. The `Body` is skipped if absent (`Clone` tolerates a missing
    // `Body` even though the slot is required -- the invariant is enforced by `CheckInvariant`,
    // not by `Clone`, the `LambdaExpression` D305 / `CastExpression` D243 precedent). The covariant
    // return is `AnonymousMethodExpression*` (through `Expression*`, the `Expression::Clone`
    // pure-virtual).
    AnonymousMethodExpression* Clone() const override {
        auto* node = new AnonymousMethodExpression();
        node->isAsync_ = isAsync_;
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `parameters_` is the always-present collection member (empty until the
    // first `Add`, non-incremental -- the node's only collection but NOT the last slot);
    // `isAsync_` defaults to `false` (no `async` modifier by default); `body_` is null until the
    // body is set (a REQUIRED slot -- `CheckInvariant` asserts it is filled). NO name shadowing
    // (no member is named `ParameterDeclaration`/`BlockStatement`), so the field types are the
    // plain classes.
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    bool isAsync_ = false;
    BlockStatement* body_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_ANONYMOUSMETHODEXPRESSION_HPP
