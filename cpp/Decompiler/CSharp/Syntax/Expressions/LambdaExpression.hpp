// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `LambdaExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/LambdaExpression.cs (the generated
// `LambdaExpression.g.cs` + the hand-written partial, which declares the `AsyncModifier` const
// and the three slot properties -- no ctors, no helpers). The next in-order Phase-5 piece per the
// D304 plan (a remaining Expression node whose dependencies are all ported -- `Expression` D226,
// `Slots::AttributeSection` D241, `Slots::Parameter` D279, and the new `Slots::LambdaBody` kind).
//
// `lambda_expression ::= attribute_section* 'async'? parameter* '=>' ( block | expression )`
// (C# grammar 12.22.1): a sealed `Expression` (the `[DecompilerAstNode]` default
// `hasPatternPlaceholder: false`, so `final` -- no pattern placeholder; `NeedsVisitor` is
// `!IsAbstract && base.IsAbstract` = true, so the generator emits the `AcceptVisitor` override +
// the `Visit` method). NOT an `EntityDeclaration` (derives directly from `Expression`, so there is
// no `MatchAttributesAndModifiers`/`Name`/`ReturnType` base machinery -- the `Attributes`
// collection appears as a plain collection `DoMatch` term). Three `[Slot]` children in source
// declaration order:
//   * `[Slot("AttributeSection")] public partial AstNodeCollection<AttributeSection> Attributes` --
//     the attribute sections on the lambda (a COLLECTION at slot 0, reusing the cycle-broken
//     `Slots::AttributeSection` kind). The collection is the node's FIRST of TWO collections, so
//     `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex == slots.Count - 1` is
//     false -- `collectionCount` is 2): every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES
//     the parent's indices for a lazy `EnsureChildIndices` rebuild (the `ComposedType` D242
//     two-collection / `ConstructorDeclaration` D281 / `MethodDeclaration` D284 precedent).
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` --
//     the lambda's parameter list (a COLLECTION at slot 1, the `parameter*` between `(` and `)`;
//     reusing the cycle-broken `Slots::Parameter` kind added by `IndexerDeclaration` D279). The
//     collection is the node's SECOND of TWO collections, so `supportsIncremental` is FALSE;
//     `baseIndex` is 1 (the slot index; the generator passes the slot index, not the dynamic
//     flattened index -- unused on the non-incremental fast path).
//   * `[Slot("LambdaBody")] public partial AstNode Body` -- the lambda body (a single REQUIRED
//     `AstNode` slot at slot 2; the `=> block | expression`, typed the abstract `AstNode` base
//     because the production takes EITHER a `BlockStatement` (the block form) OR an `Expression`
//     (the expression form); both derive from `AstNode`, so the slot accepts either). The slot
//     FOLLOWS the `Parameters` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection). Reusing
//     the new `Slots::LambdaBody` kind (a `CSharpSlotInfoT<AstNode>`, the `UsingStatement`
//     `ResourceAcquisition` D262 `AstNode`-typed-slot precedent applied to a trailing single).
//
// Plus one non-`[Slot]` scalar: `public bool IsAsync` (the leading `async` modifier; a plain bool
// field, set via the property setter; NOT a ctor param -- the generator adds only settable
// ENUM-typed scalars to `CtorParams`; a bool is in `MembersToMatch` with the fall-through
// plain-equality `DoMatch` term, the `ComposedType.HasRefSpecifier` precedent).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): `LambdaExpression` is NOT
// derived from `EntityDeclaration`, so the explicit `MatchAttributesAndModifiers`/`Name`/
// `ReturnType` additions are skipped; the per-property scan adds the non-override non-
// `[ExcludeFromMatch]` instance properties in source declaration order: `Attributes` (an
// `AstNodeCollection`, `RecursiveMatch: true` -> the collection recursive `DoMatch` term), `IsAsync`
// (a bool -> the fall-through plain-equality term), `Parameters` (an `AstNodeCollection` -> the
// collection recursive `DoMatch` term), `Body` (an `AstNode`, `RecursiveMatch: true`,
// `Nullable: false` -> the direct `this.Body.DoMatch(o.Body, match)` term, which the port routes
// through `AstNode::MatchRequired` -- the `[class.access.derived]` workaround, the `UsingStatement`
// `ResourceAcquisition` D262 / `UnaryOperatorExpression` `Expression` D231 precedent). So
// `MembersToMatch` is `[Attributes, IsAsync, Parameters, Body]`, and the generated `DoMatch` is
// `return other is LambdaExpression o && this.Attributes.DoMatch(o.Attributes, match) &&
// this.IsAsync == o.IsAsync && this.Parameters.DoMatch(o.Parameters, match) &&
// this.Body.DoMatch(o.Body, match)`. A type-only mismatch (not a `LambdaExpression`) rejects early.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// Parameters (collection, optional), Body (single, required)]` (the `IsAsync` bool is NOT a ctor
// param -- the generator adds only settable ENUM-typed scalars). `RequiredConstructorPrefixLength`
// is 3 (through the last non-optional param `Body` at index 2). `ConstructorPrefixLengths` is
// `{3}` (`reqLen` 3, then no collection at-or-after `reqLen` since both collections precede the
// required `Body`, then `cp.Count` 3 -- so just the one length). The (len=3) ctor's body calls
// `this.Attributes.AddRange(...)` and `this.Parameters.AddRange(...)` for the two collections
// (both `AddRange` conveniences are the D222 deferral), so it is DEFERRED; the empty ctor is the
// only portable ctor. A `LambdaExpression` is built via the empty ctor + `Attributes().Add(...)` +
// `Parameters().Add(...)` + `Body(...)` + `IsAsync(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitLambdaExpression(this)`
// (the class name does not end in "AstType", so the visit-method-name default yields
// `VisitLambdaExpression`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
// collection -- the node's second collection), and `BodySlot` (a `CSharpSlotInfoT<AstNode>`
// pointing at `Slots.LambdaBody`, required -- the `Body` `AstNode` is non-nullable). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `IsAsync` scalar copied, the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), every `Attributes`
// element deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
// returns `AttributeSection*`, which `Add(AttributeSection*)` accepts directly), every `Parameters`
// element deep-cloned through `Add` (which re-parents and re-indexes; `ParameterDeclaration::Clone()`
// returns `ParameterDeclaration*`, which `Add(ParameterDeclaration*)` accepts directly), and the
// `Body` deep-cloned through the setter (which re-parents; `AstNode::Clone()` returns `AstNode*`,
// which `Body(AstNode*)` accepts directly -- the slot is typed the abstract base, so no cast, the
// `UsingStatement` `ResourceAcquisition` D262 precedent). No own location fields (does not derive
// `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
// `ConstructorDeclaration` D281 / `MethodDeclaration` D284 no-location-copy precedent). The
// covariant return is `LambdaExpression*` (through `Expression*`, the `Expression::Clone`
// pure-virtual).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`ParameterDeclaration`/
// `AstNode` -- the `Attributes()`/`Parameters()`/`Body()` accessors do not collide with any class
// in the `Syntax` namespace, and the `Body` accessor does not collide with the `AstNode` base
// type -- a member named `Body` is not the name `AstNode`), so no elaborated-type-specifier is
// needed anywhere; the plain element types resolve to the classes. This is the
// `ConstructorDeclaration` D281 two-collection shape (two collections + a trailing single)
// generalized with an `AstNode`-typed trailing single (the `UsingStatement` D262 precedent) and a
// bool scalar, applied to the `Expression` hierarchy (NOT an `EntityDeclaration`, so no
// `MatchAttributesAndModifiers`).
//
// NO new `Slots` constant in `Slots.hpp` beyond `Slots::LambdaBody` (added this iteration):
// `Slots::AttributeSection` (cycle-broken in `AttributeSection.hpp` D241) and `Slots::Parameter`
// (cycle-broken in `ParameterDeclaration.hpp` D279) are both already ported.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_LAMBDAEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_LAMBDAEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class LambdaExpression : Expression`. `final` (the C# `sealed`):
// no further derivation. The `ConstructorDeclaration` D281 two-collection shape (an `Attributes`
// collection + a `Parameters` collection + a trailing single `Body`) generalized with an
// `AstNode`-typed trailing single (the `UsingStatement` D262 `ResourceAcquisition` precedent) and
// an `IsAsync` bool scalar, applied to the `Expression` hierarchy (NOT an `EntityDeclaration`, so
// no `MatchAttributesAndModifiers`/`Name`/`ReturnType` base machinery).
class LambdaExpression final : public Expression {
public:
    ~LambdaExpression() override = default;

    // The generated empty ctor (the C# `public LambdaExpression()`). The `Attributes` and
    // `Parameters` collections are members (the D222 always-present-stack-member design),
    // initialized here with `baseIndex = 0` for `Attributes` (the first collection) and
    // `baseIndex = 1` for `Parameters` (the slot index, the generator-passed value -- unused on
    // the non-incremental fast path) and `supportsIncremental = false` for both (the node has
    // two collections, so neither is incremental -- the `ComposedType` D242 / `ConstructorDeclaration`
    // D281 two-collection precedent). `Body` defaults to null (a REQUIRED slot --
    // `CheckInvariant` asserts it is filled).
    LambdaExpression() : attributes_(this, &AttributesSlot, 0, false),
                         parameters_(this, &ParametersSlot, 1, false) {}

    // The C# `public const string AsyncModifier = "async"` -- the `async` modifier token (the
    // leading `async` of an `async` lambda). Ports as a `static constexpr const char*` (a static
    // field, not instance state), so the generator's `MembersToMatch` (which iterates only
    // instance `IPropertySymbol`s) excludes it from the `DoMatch` (the `BreakStatement` D254 /
    // `CheckedExpression.CheckedKeyword` D234 / `AnonymousTypeCreateExpression.NewKeyword` D304
    // precedent). This is the CANONICAL source of the `async` literal aliased by
    // `AnonymousMethodExpression.AsyncModifier` (the not-yet-ported sibling -- the
    // `UnaryOperatorExpression.AwaitKeyword` D262 canonical-source precedent, the value-vs-behaviour
    // discriminator: a public-API const string aliased by siblings ports now, distinct from the
    // D229-deferred per-operator token-string lookup tables consumed only by the unported
    // output/resolver stage).
    static constexpr const char* AsyncModifier = "async";

    // ---- The `Attributes` collection slot (NOT a base virtual) -------------------------------
    // The generated `[Slot("AttributeSection")] public partial AstNodeCollection<AttributeSection>
    // Attributes` -- the attribute sections on the lambda (a `CSharpSlotInfoT<AttributeSection>` slot
    // at slot 0, non-incremental -- the node's first of two collections). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual (`Expression` declares no `Attributes`; this
    // is a plain non-virtual accessor). Reuses the cycle-broken `Slots::AttributeSection` kind.
    // A `const` convenience overload returns `const&` for a `const LambdaExpression*`.
    AstNodeCollectionT<AttributeSection>& Attributes() { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // The C# `public bool IsAsync { get; set; }` -- whether the lambda carries a leading `async`
    // modifier (the `async` in `async (x) => ...`). A plain bool field: not a child slot (no
    // `[Slot]`), not a ctor param (the generator adds only settable ENUM-typed scalars), so it is
    // set via the property setter. It IS in `MembersToMatch` (the generator adds every
    // non-`[Slot]` instance property), and a bool (not an enum, no `Any`, not a string) emits the
    // fall-through plain-equality `DoMatch` term (the `ComposedType.HasRefSpecifier` D242 /
    // `StackAllocExpression` no-`Any`-bool precedent). No name shadowing.
    bool IsAsync() const { return isAsync_; }
    void IsAsync(bool value) { isAsync_ = value; }

    // ---- The `Parameters` collection slot (NOT a base virtual) -------------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the lambda's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot at
    // slot 1, the node's SECOND of TWO collections, non-incremental). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses the
    // cycle-broken `Slots::Parameter` kind (added by `IndexerDeclaration` D279). A `const`
    // convenience overload returns `const&` for a `const LambdaExpression*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Body` slot (a REQUIRED single `AstNode`, NOT a base virtual) -----------------
    // The generated `[Slot("LambdaBody")] public partial AstNode Body` -- a single REQUIRED
    // `AstNode` slot at slot 2 (the lambda body -- `=> block | expression`; typed the abstract
    // `AstNode` base because the production takes EITHER a `BlockStatement` (the block form) OR an
    // `Expression` (the expression form), both `AstNode`-derived). The slot FOLLOWS the
    // `Parameters` collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field,
    // value)` (the dynamic flattened index after a collection). NOT an override, so a plain
    // non-virtual accessor. `CheckInvariant` asserts the `Body` is filled (the slot is required --
    // the C# `AstNode Body` is non-nullable). Reuses the new `Slots::LambdaBody` kind (the
    // `UsingStatement` `ResourceAcquisition` D262 `AstNode`-typed-slot precedent applied to a
    // trailing single). No name shadowing (the `Body()` accessor does not collide with the
    // `AstNode` base type -- a member named `Body` is not the name `AstNode`), so the setter takes
    // a plain `AstNode*` (no cast -- the `SetChild` override passes the incoming `AstNode*`
    // straight through, the slot being typed the abstract base).
    AstNode* Body() const { return body_; }
    void Body(AstNode* value) {
        SetChildNode(body_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `ParametersSlot` (a
    // `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection -- the
    // node's second collection); `BodySlot` (a `CSharpSlotInfoT<AstNode>` pointing at
    // `Slots.LambdaBody`, required -- the `Body` `AstNode` is non-nullable). NO name shadowing (no
    // member is named `AttributeSection`/`ParameterDeclaration`/`AstNode`), so the element types
    // are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<AstNode> BodySlot{"Body", false, &Slots::LambdaBody, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitLambdaExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitLambdaExpression(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Three slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `Parameters` collection at slot 1 (the contiguous range
    // `[attrCount, attrCount + paramCount)`), and a `Body` single at slot 2 (`attrCount +
    // paramCount`). `GetChildCount` is `attrCount + paramCount + 1` (the two collections' current
    // lengths plus the one single -- the single slot contributes 1 to the flattened count
    // regardless of whether it is filled); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, a collection step, then a single step). `GetCollectionByKind` returns the `Attributes`
    // collection for the `AttributeSection` kind and the `Parameters` collection for the
    // `Parameter` kind. This is the `ConstructorDeclaration` D281 two-collection shape with one
    // trailing single (instead of three) and no `NameToken`.

    int GetChildCount() const override { return attributes_.Count() + parameters_.Count() + 1; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        if (i == 0)
            return body_;
        throw std::out_of_range("LambdaExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n) {
                attributes_.SetAt(i, static_cast<AttributeSection*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n) {
                parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(body_, value, index);
            return;
        }
        throw std::out_of_range("LambdaExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return &AttributesSlot;
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        if (i == 0)
            return &BodySlot;
        throw std::out_of_range("LambdaExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::Parameter)
            return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is LambdaExpression o && this.Attributes.DoMatch(o.Attributes, match) &&
    // this.IsAsync == o.IsAsync && this.Parameters.DoMatch(o.Parameters, match) &&
    // this.Body.DoMatch(o.Body, match)`. `LambdaExpression` is NOT an `EntityDeclaration`, so there
    // is no `MatchAttributesAndModifiers`/`Name`/`ReturnType` term (the `Attributes` collection is a
    // plain collection `DoMatch` term). The `IsAsync` bool is the fall-through plain-equality term;
    // the `Attributes`/`Parameters` terms are the collection recursive `DoMatch` (the generator emits
    // a collection-typed recursive term directly, NOT `MatchOptional` -- the
    // `FieldDeclaration.Variables` D273 / `MethodDeclaration.Parameters` D284 precedent); the `Body`
    // term is the direct `this.Body.DoMatch(o.Body, match)` for a NON-NULLABLE recursive child,
    // which the port routes through `AstNode::MatchRequired` (the `[class.access.derived]`
    // workaround -- a derived node may not call the protected `DoMatch` through a base
    // `AstNode*`; a static member of `AstNode` may, the `MatchOptional`/`MatchRequired` precedent).
    // A type-only mismatch (not a `LambdaExpression`) rejects early. The terms are in
    // `MembersToMatch` (source declaration) order: `Attributes`/`IsAsync`/`Parameters`/`Body`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<LambdaExpression*>(other);
        if (o == nullptr)
            return false;
        return attributes_.DoMatch(o->attributes_, match)
            && isAsync_ == o->isAsync_
            && parameters_.DoMatch(o->parameters_, match)
            && MatchRequired(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `IsAsync` scalar copied, the annotation channel
    // copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), every
    // `Attributes` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AttributeSection::Clone()` returns `AttributeSection*`, which `Add(AttributeSection*)`
    // accepts directly), every `Parameters` element deep-cloned through `Add` (which re-parents
    // and re-indexes; `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, which
    // `Add(ParameterDeclaration*)` accepts directly), and the `Body` deep-cloned through the setter
    // (which re-parents; `AstNode::Clone()` returns `AstNode*`, which `Body(AstNode*)` accepts
    // directly -- the slot is typed the abstract base, so no cast). No own location fields (does
    // not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied.
    // Each child is skipped if absent (`Clone` tolerates a missing `Body` even though the slot is
    // required -- the invariant is enforced by `CheckInvariant`, not by `Clone`, the `UsingStatement`
    // D262 / `CastExpression` D243 precedent). The covariant return is `LambdaExpression*` (through
    // `Expression*`, the `Expression::Clone` pure-virtual).
    LambdaExpression* Clone() const override {
        auto* node = new LambdaExpression();
        node->isAsync_ = isAsync_;
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`parameters_` are the always-present collection members
    // (empty until the first `Add`, both non-incremental -- the node has two collections);
    // `isAsync_` defaults to `false` (no `async` modifier by default); `body_` is null until the
    // body is set (a REQUIRED slot -- `CheckInvariant` asserts it is filled). NO name shadowing (no
    // member is named `AttributeSection`/`ParameterDeclaration`/`AstNode`), so the field types are
    // the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    bool isAsync_ = false;
    AstNode* body_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_LAMBDAEXPRESSION_HPP
