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

// Port of the `InvocationAstType` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/InvocationAstType.cs (the generated `InvocationAstType.g.cs`
// -- the hand-written partial declares only the two slot properties, no ctors, no helpers). The
// next in-order Phase-5 piece per the D312 plan ("the remaining GeneralScope nodes
// (FunctionPointerAstType/InvocationAstType/SyntaxTree) then the OutputVisitor/ITextOutput/
// TokenWriter output stage"). `invocation_ast_type ::= type '(' argument_list? ')'` (no spec
// grammar production -- an ILSpy-internal type form used when a type appears applied to
// arguments, e.g. an attribute type written with its constructor arguments): an `AstType` whose
// `Arguments` collection holds the parenthesized argument list and whose `BaseType` slot holds
// the type being applied.
//
// It is the `AnonymousMethodExpression` D306 one-non-incremental-collection-plus-a-required-
// trailing-single shape applied to the `AstType` hierarchy (NOT the `Expression` hierarchy -- so
// there is no `EntityDeclaration` base machinery; the `Arguments` collection is a plain
// collection-`DoMatch` term, unlike `Accessor` D274's `MatchAttributesAndModifiers`). The
// `Arguments` collection (an `AstNodeCollection<Expression>`) is the node's ONLY collection, but
// it is NOT at the last slot (`BaseType` trails it), so `supportsIncremental` is FALSE
// (`collectionCount == 1 && slotIndex == slots.Count - 1` is false -- `slotIndex` 0 !=
// `slots.Count - 1` 1): every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's
// indices for a lazy `EnsureChildIndices` rebuild (the `Accessor` D274 / `ComposedType` D242
// collection-not-at-the-last-slot precedent). `baseIndex` is 0 (the slot index; the generator
// passes the slot index, not the dynamic flattened index -- unused on the non-incremental fast
// path). The `BaseType` single slot (a REQUIRED `AstType`) FOLLOWS the `Arguments` collection, so
// the property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened
// index after a collection, the `Accessor` D274 / `AnonymousMethodExpression` D306
// single-after-a-collection precedent).
//
// The C# declares `public sealed partial class InvocationAstType : AstType` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder` is false, so `final`).
//
// Its generated `DoMatch` has two terms: the collection recursive match
// `this.Arguments.DoMatch(o.Arguments, match)` (the generator emits the collection-typed recursive
// term directly -- NOT `MatchOptional`, which it emits only for a nullable NON-collection child --
// the `FieldDeclaration.Variables` D273 / `AnonymousMethodExpression.Parameters` D306 precedent)
// and the direct `this.BaseType.DoMatch(o.BaseType, match)` for the NON-NULLABLE recursive child,
// which the port routes through `AstNode::MatchRequired` (the `[class.access.derived]` workaround --
// a derived node may not call the protected `DoMatch` through a base `AstType*`; a static member of
// `AstNode` may, the `MatchOptional`/`MatchRequired` precedent). The terms are in `MembersToMatch`
// (source declaration) order: `Arguments` then `BaseType`.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitInvocationType(this)`
// (`InvocationAstType` ends in "AstType", so the generator's visit-method-name rewriting
// (`s.Replace("AstType", "Type")`) yields `VisitInvocationType` -- the
// `FunctionPointerAstType`/`TupleAstType` D290 rewriting the D236 note flagged). The generated slot
// statics are `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
// collection -- the node's only collection) and `BaseTypeSlot` (a `CSharpSlotInfoT<AstType>`
// pointing at `Slots.Type`, required -- the `BaseType` `AstType` is non-nullable).
//
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), every `Arguments` element deep-cloned through
// `Add` (which re-parents and re-indexes; `Expression::Clone()` returns `Expression*`, the covariant
// override through `AstNode*`, which `Add(Expression*)` accepts directly -- the `Expression` D226
// abstract base redeclares the typed covariant `Clone`, so no `static_cast` is needed, the
// `InterpolatedStringContent` D309 abstract-base-typed-`Clone` precedent), and the `BaseType`
// deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`, which
// `BaseType(AstType*)` accepts directly -- the slot is typed the abstract `AstType` base, so the
// covariant return is accepted directly, the `CastExpression` D243 / `Constraint` D283
// `AstType`-typed-slot precedent). No own location fields (does not derive `EndLocation`), so the
// print-time `StartLocation`/`EndLocation` are not copied (the `SimpleType` D237 / `TupleAstType` D290
// no-location-copy precedent). The covariant return is `InvocationAstType*` (through `AstType*`, the
// `AstType::Clone` pure-virtual -- the typed return the C# `new AstType Clone()` gives, applied to
// the concrete `InvocationAstType`).
//
// NO C++ name-shadowing crux: the `Arguments()`/`BaseType()` accessors do not collide with any class
// in the `Syntax` namespace (there is `AstType`, not `Type` -- the `Attribute` D240 no-class-named-
// `Type` lesson; and `Arguments` is not `Expression`), so no elaborated-type-specifier is needed
// anywhere, and the plain `Expression`/`AstType` resolve to the classes. This is the
// `AnonymousMethodExpression` D306 collection-then-required-trailing-single shape (MINUS the
// `IsAsync` bool scalar and the const strings) applied to the `AstType` hierarchy -- structurally a
// simplified `LambdaExpression` D305 (no `Attributes` collection, an `AstType` trailing single
// instead of an `AstNode` body, and no `EntityDeclaration` base machinery -- so no
// `MatchAttributesAndModifiers`/`Name`/`ReturnType`).
//
// NO new `Slots` constant: the `Arguments` collection's `[Slot("Expression")]` argument names the
// slot KIND "Expression" (the kind-collapsing design is by `[Slot]` name regardless of
// single-vs-collection -- the `ArrayInitializerExpression` D250 / `BlockStatement` D256 /
// `TupleExpression` D296 / `AnonymousTypeCreateExpression` D304 precedent of reusing
// `Slots::Expression`, originally a single-`Expression` operand position by
// `UnaryOperatorExpression` D231, as a collection kind), and the `BaseType` single's
// `[Slot("Type")]` argument names the kind "Type" (the `Attribute` D240 / `CastExpression` D243
// precedent). Both kinds are already ported, so `Slots.hpp` is unchanged.
//
// `InvocationAstType.cs` declares NO hand-written ctors (only the two slot properties), so the port
// carries only the generated ctors. The generated collection ctor (`InvocationAstType(
// IEnumerable<Expression>)` and the `params Expression[]` form) uses `AddRange`, which lands with the
// collection convenience mutators (the D222 deferral), so it is DEFERRED; the empty ctor is the only
// portable ctor, and an argument list is built via `Arguments().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_INVOCATIONASTTYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_INVOCATIONASTTYPE_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class InvocationAstType : AstType`. `final` (the C# `sealed`): no
// further derivation. The `AnonymousMethodExpression` D306 one-non-incremental-collection-plus-a-
// required-trailing-single shape applied to the `AstType` hierarchy -- structurally a simplified
// `LambdaExpression` D305 (no `Attributes` collection, an `AstType` trailing single instead of an
// `AstNode` body, and no `EntityDeclaration` base machinery -- so no
// `MatchAttributesAndModifiers`/`Name`/`ReturnType`).
class InvocationAstType final : public AstType {
public:
    ~InvocationAstType() override = default;

    // The generated empty ctor (the C# `public InvocationAstType()`). The `Arguments` collection is
    // a member (the D222 always-present-stack-member design), initialized here with `baseIndex = 0`
    // (the collection is the first slot) and `supportsIncremental = false` (the collection is the
    // node's ONLY collection but NOT the last slot -- `BaseType` trails it -- so an element's
    // flattened `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation, the
    // `Accessor` D274 / `AnonymousMethodExpression` D306 collection-not-at-the-last-slot
    // precedent). `BaseType` defaults to null (a REQUIRED slot -- `CheckInvariant` asserts it is
    // filled).
    InvocationAstType() : arguments_(this, &ArgumentsSlot, 0, false) {}

    // ---- The `Arguments` collection slot ----------------------------------------------
    // The generated `[Slot("Expression")] public partial AstNodeCollection<Expression> Arguments`
    // -- the parenthesized argument list (a `CSharpSlotInfoT<Expression>` slot at slot 0, the
    // node's ONLY collection, non-incremental -- NOT the last slot). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-Add element-list profile is preserved --
    // `list_` is empty until the first `Add`). NOT a base virtual (`AstType` declares no
    // `Arguments`; this is a plain non-virtual accessor). Reuses the already-ported
    // `Slots::Expression` kind (originally a single-`Expression` operand position, reused as a
    // collection kind -- the kind-collapsing design is by `[Slot]` name regardless of
    // single-vs-collection, the `ArrayInitializerExpression` D250 / `TupleExpression` D296 /
    // `AnonymousTypeCreateExpression` D304 precedent). A `const` convenience overload returns
    // `const&` for a `const InvocationAstType*`.
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The `BaseType` slot (a REQUIRED single `AstType`, NOT a base virtual) ---------
    // The generated `[Slot("Type")] public partial AstType BaseType` -- a single REQUIRED `AstType`
    // slot at slot 1 (the type being applied to the arguments). The slot FOLLOWS the `Arguments`
    // collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the
    // dynamic flattened index after a collection). NOT an override, so a plain non-virtual
    // accessor. `CheckInvariant` asserts the `BaseType` is filled (the slot is required -- the C#
    // `AstType BaseType` is non-nullable). Reuses the already-ported `Slots::Type` kind (the
    // `Attribute` D240 / `CastExpression` D243 / `Constraint` D283 precedent). No name shadowing
    // (the `BaseType()` accessor does not collide with the `AstType` class -- a member named
    // `BaseType` is not the name `AstType`), so the setter takes a plain `AstType*` (no cast -- the
    // `SetChild` override `static_cast`s the incoming `AstNode*` to `AstType*`, the slot being
    // typed the abstract `AstType` base).
    AstType* BaseType() const { return baseType_; }
    void BaseType(AstType* value) {
        SetChildNode(baseType_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, collection --
    // the node's only collection); `BaseTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at
    // `Slots.Type`, required -- the `BaseType` `AstType` is non-nullable). NO name shadowing (no
    // member is named `Expression`/`AstType` -- the `Arguments`/`BaseType` accessors do not
    // collide with the `Expression`/`AstType` classes), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Expression, true};
    static inline const CSharpSlotInfoT<AstType> BaseTypeSlot{"BaseType", false, &Slots::Type, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitInvocationType` (`InvocationAstType` ends in "AstType", so the
    // generator's visit-method-name rewriting yields `VisitInvocationType`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitInvocationType(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Two slots in declaration order: an `Arguments` collection at slot 0 (the contiguous range
    // `[0, argCount)`) and a `BaseType` single slot at slot 1 (index `argCount`). `GetChildCount`
    // is `argCount + 1` (the collection's current length plus the one single slot -- the single
    // slot contributes 1 to the flattened count even when the `BaseType` is null); `GetChild`/
    // `SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a running
    // index (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections`
    // shape -- a collection step, then a single step). `GetCollectionByKind` returns the
    // `Arguments` collection for the `Expression` kind. This is the `AnonymousMethodExpression` D306
    // collection -> single dispatch shape with the collection FIRST and the `BaseType` REQUIRED.

    int GetChildCount() const override { return arguments_.Count() + 1; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = arguments_.Count();
            if (i < n)
                return arguments_.At(i);
            i -= n;
        }
        if (i == 0)
            return baseType_;
        throw std::out_of_range("InvocationAstType::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = arguments_.Count();
            if (i < n) {
                arguments_.SetAt(i, static_cast<Expression*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(baseType_, static_cast<AstType*>(value), index);
            return;
        }
        throw std::out_of_range("InvocationAstType::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = arguments_.Count();
            if (i < n)
                return &ArgumentsSlot;
            i -= n;
        }
        if (i == 0)
            return &BaseTypeSlot;
        throw std::out_of_range("InvocationAstType::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Expression)
            return &arguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is InvocationAstType o && this.Arguments.DoMatch(o.Arguments, match) &&
    // this.BaseType.DoMatch(o.BaseType, match)`. `InvocationAstType` is NOT an `EntityDeclaration`
    // (it is an `AstType`), so there is no `MatchAttributesAndModifiers`/`Name`/`ReturnType` term.
    // The `Arguments` term is the collection recursive `DoMatch` (the generator emits a
    // collection-typed recursive term directly, NOT `MatchOptional` -- the `FieldDeclaration.
    // Variables` D273 / `AnonymousMethodExpression.Parameters` D306 precedent); the `BaseType`
    // term is the direct `this.BaseType.DoMatch(o.BaseType, match)` for a NON-NULLABLE recursive
    // child, which the port routes through `AstNode::MatchRequired` (the `[class.access.derived]`
    // workaround -- a derived node may not call the protected `DoMatch` through a base `AstType*`;
    // a static member of `AstNode` may, the `MatchOptional`/`MatchRequired` precedent). A type-only
    // mismatch (not an `InvocationAstType`) rejects early. The terms are in `MembersToMatch`
    // (source declaration) order: `Arguments`/`BaseType`.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<InvocationAstType*>(other);
        if (o == nullptr)
            return false;
        return arguments_.DoMatch(o->arguments_, match)
            && MatchRequired(baseType_, o->baseType_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), every
    // `Arguments` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Expression::Clone()` returns `Expression*`, the covariant override through `AstNode*`,
    // which `Add(Expression*)` accepts directly -- the `Expression` D226 abstract base redeclares
    // the typed covariant `Clone`, so no `static_cast`, the `InterpolatedStringContent` D309
    // abstract-base-typed-`Clone` precedent), and the `BaseType` deep-cloned through the setter
    // (which re-parents; `AstType::Clone()` returns `AstType*`, which `BaseType(AstType*)` accepts
    // directly -- the `CastExpression` D243 / `Constraint` D283 `AstType`-typed-slot precedent).
    // No own location fields (does not derive `EndLocation`), so the print-time `StartLocation`/
    // `EndLocation` are not copied. The `BaseType` is skipped if absent (`Clone` tolerates a
    // missing `BaseType` even though the slot is required -- the invariant is enforced by
    // `CheckInvariant`, not by `Clone`, the `AnonymousMethodExpression` D306 / `CastExpression` D243
    // precedent). The covariant return is `InvocationAstType*` (through `AstType*`, the
    // `AstType::Clone` pure-virtual -- the typed return the C# `new AstType Clone()` gives, applied
    // to the concrete `InvocationAstType`).
    InvocationAstType* Clone() const override {
        auto* node = new InvocationAstType();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        if (baseType_ != nullptr)
            node->BaseType(baseType_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `arguments_` is the always-present collection member (empty until the
    // first `Add`, non-incremental -- the node's only collection but NOT the last slot);
    // `baseType_` is null until the type is set (a REQUIRED slot -- `CheckInvariant` asserts it is
    // filled). NO name shadowing (no member is named `Expression`/`AstType`), so the field types
    // are the plain classes.
    AstNodeCollectionT<Expression> arguments_;
    AstType* baseType_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_INVOCATIONASTTYPE_HPP
