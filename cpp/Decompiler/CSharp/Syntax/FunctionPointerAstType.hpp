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

// Port of the `FunctionPointerAstType` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/FunctionPointerAstType.cs (the generated
// `FunctionPointerAstType.g.cs` + the hand-written partial, which declares only the
// `PointerToken` const, the `HasUnmanagedCallingConvention` bool, and the three slot
// properties -- no ctors, no helpers). The next in-order Phase-5 piece per the D313 plan (the
// remaining GeneralScope `AstType`-bearing nodes). `funcptr_type ::= 'delegate' '*'
// calling_convention_specifier? funcptr_parameter_list funcptr_return_type` (C# grammar 24.3.3) --
// a C# 9+ function pointer type `delegate*<...>`: the `CallingConventions` collection holds the
// calling-convention specifiers (`managed`/`unmanaged`/...), the `Parameters` collection holds the
// parameter list, and the `ReturnType` slot holds the return type.
//
// It is the `ArrayCreateExpression` D252 two-collection shape (both collections non-incremental)
// with a REQUIRED trailing single (`ReturnType` is non-nullable, unlike `ArrayCreateExpression`'s
// nullable `Initializer`) and NO leading single (unlike `ArrayCreateExpression`'s leading `Type`).
// The slot layout in source declaration order is: `CallingConventions` (an
// `AstNodeCollection<AstType>` collection at slot 0), `Parameters` (an
// `AstNodeCollection<ParameterDeclaration>` collection at slot 1), and `ReturnType` (a single
// REQUIRED `AstType` at slot 2). The generator's `supportsIncremental` flag is
// `collectionCount == 1 && slotIndex == slots.Count - 1`; here `collectionCount == 2`, so it is
// FALSE for BOTH collections: every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the
// parent's indices for a lazy rebuild (`EnsureChildIndices`), and `IndexOf` falls back to a linear
// identity search (the `ComposedType` D242 / `ArrayCreateExpression` D252 two-collection
// precedent). The `ReturnType` single slot FOLLOWS both collections, so the generator's
// `constIndex = !slots.Take(slotIndex).Any(IsCollection)` is `false` (two collections precede it),
// and the `ReturnType` setter uses the index-less `SetChildNode(ref field, value)` (which
// invalidates on a set/clear, since the flattened index is dynamic -- either collection's count can
// change); the `SetChild` override still passes the known flattened `index` to the const-index
// `SetChildNode(ref field, value, index)`.
//
// The `HasUnmanagedCallingConvention` bool is a NON-`[Slot]` instance property (a plain scalar),
// so it is in `MembersToMatch` (the generator's per-property scan adds every non-override
// non-`[ExcludeFromMatch]` instance property whose type is not `CSharpTokenNode`/`TextLocation` --
// the `ComposedType.HasRefSpecifier` D242 / `ParameterDeclaration.HasThisModifier` D278
// get-or-set-bool precedent), but it is NOT a ctor param (the generator adds only settable
// ENUM-typed scalars to `CtorParams`), so it is set via the property setter. A bool (not an enum,
// no `Any`) emits the `DoMatchTerm` fall-through plain-equality (`this.HasUnmanagedCallingConvention
// == o.HasUnmanagedCallingConvention`) -- the `ComposedType.HasRefSpecifier` D242 / `Accessor.Kind`
// D274 plain-bool-scalar precedent.
//
// Its generated `DoMatch` has FOUR terms in `MembersToMatch` (source declaration) order: a plain-bool
// `HasUnmanagedCallingConvention` term, a collection recursive `CallingConventions` term
// (`this.CallingConventions.DoMatch` -- the generator emits the collection-typed recursive term
// directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection child -- the
// `FieldDeclaration.Variables` D273 / `AnonymousMethodExpression.Parameters` D306 precedent), a
// collection recursive `Parameters` term (likewise direct), and a non-nullable recursive `ReturnType`
// term (the direct `this.ReturnType.DoMatch(o.ReturnType, match)` for a NON-NULLABLE recursive
// child, which the port routes through `AstNode::MatchRequired` -- the D231 [class.access.derived]
// workaround, since a derived node may not call the protected `DoMatch` through a base `AstType*`;
// a static member of `AstNode` may, the `MatchOptional`/`MatchRequired` precedent). `FunctionPointerAstType`
// is NOT an `EntityDeclaration` (it is an `AstType`), so there is no
// `MatchAttributesAndModifiers`/`Name`/`ReturnType` base-machinery term (the `ReturnType` here is
// the node's OWN `[Slot("Type")]` property, not the `EntityDeclaration.ReturnType` virtual). A
// type-only mismatch (not a `FunctionPointerAstType`) rejects early.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitFunctionPointerType(this)`
// (`FunctionPointerAstType` ends in "AstType", so the generator's visit-method-name rewriting
// (`s.Replace("AstType", "Type")`) yields `VisitFunctionPointerType` -- the `TupleAstType` D290 /
// `InvocationAstType` D313 rewriting the D236 note flagged). The generated slot statics are
// `CallingConventionsSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.CallingConvention`,
// collection -- the node's first collection), `ParametersSlot` (a
// `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection -- the node's
// second collection), and `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`,
// required -- the `ReturnType` `AstType` is non-nullable).
//
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `HasUnmanagedCallingConvention` scalar
// copied via the public getter/setter (the `EntityDeclaration.Modifiers` D272 base-scalar-copy
// precedent -- the backing field is private, so the concrete `Clone` copies the scalar through the
// public surface), every `CallingConventions` and `Parameters` element deep-cloned through `Add`
// (which re-parents and re-indexes; `AstType::Clone()` returns `AstType*` which `Add(AstType*)`
// accepts directly, and `ParameterDeclaration::Clone()` returns `ParameterDeclaration*` which
// `Add(ParameterDeclaration*)` accepts directly -- both bases redeclare the typed covariant
// `Clone`, so no `static_cast` is needed, the `InterpolatedStringContent` D309
// abstract-base-typed-`Clone` precedent), and the `ReturnType` deep-cloned through the setter
// (which re-parents; `AstType::Clone()` returns `AstType*`, which `ReturnType(AstType*)` accepts
// directly -- the `CastExpression` D243 / `Constraint` D283 `AstType`-typed-slot precedent). No own
// location fields (does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation`
// are not copied (the `SimpleType` D237 / `TupleAstType` D290 / `InvocationAstType` D313
// no-location-copy precedent). The `ReturnType` is skipped if absent (`Clone` tolerates a missing
// `ReturnType` even though the slot is required -- the invariant is enforced by `CheckInvariant`,
// not by `Clone`, the `AnonymousMethodExpression` D306 / `CastExpression` D243 / `InvocationAstType`
// D313 precedent). The covariant return is `FunctionPointerAstType*` (through `AstType*`, the
// `AstType::Clone` pure-virtual -- the typed return the C# `new AstType Clone()` gives, applied to
// the concrete `FunctionPointerAstType`).
//
// NO C++ name-shadowing crux: the `CallingConventions()`/`Parameters()`/`ReturnType()`/
// `HasUnmanagedCallingConvention()` accessors do not collide with any class in the `Syntax`
// namespace (there is `AstType`/`ParameterDeclaration`, not `CallingConvention`/`Parameter`/`Type`;
// and `HasUnmanagedCallingConvention`/`ReturnType` are not class names), so no
// elaborated-type-specifier is needed anywhere, and the plain `AstType`/`ParameterDeclaration`
// resolve to the classes (the `InvocationAstType` D313 differently-named-property precedent). This
// is the `ArrayCreateExpression` D252 two-collection shape with the trailing single REQUIRED
// (instead of nullable) and NO leading single -- the first ported `AstType` with two collections
// both followed by a required single.
//
// NO new `Slots` constant for `Parameters`/`ReturnType`: both kinds are already ported
// (`Slots::Parameter` cycle-broken into `ParameterDeclaration.hpp` by `IndexerDeclaration` D279;
// `Slots::Type` by `Attribute` D240). The `Slots::CallingConvention` kind is NEW and lives in
// `Slots.hpp` (no cycle: `AstType.hpp` is the abstract base with no per-node slot statics -- the
// `Slots.Type`/`Slots.BaseType`/`Slots.NamespaceName` precedent).
//
// `FunctionPointerAstType.cs` declares NO hand-written ctors (only the `PointerToken` const, the
// `HasUnmanagedCallingConvention` bool, and the three slot properties), so the port carries only
// the generated ctors. The generated collection ctors (the `(IEnumerable<AstType>,
// IEnumerable<ParameterDeclaration>, AstType)` all-params form and the `params` overloads) use
// `AddRange`, which lands with the collection convenience mutators (the D222 deferral), so they
// are DEFERRED; the empty ctor is the only portable ctor, and the calling-convention list,
// parameter list, and return type are built via `CallingConventions().Add(...)`/`Parameters().Add(...)`
// /`ReturnType(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_FUNCTIONPOINTERASTTYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_FUNCTIONPOINTERASTTYPE_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class FunctionPointerAstType : AstType`. `final` (the C#
// `sealed`): no further derivation. The first ported `AstType` with two collections both followed
// by a REQUIRED single (`CallingConventions` + `Parameters` + the trailing required `ReturnType`).
class FunctionPointerAstType final : public AstType {
public:
    ~FunctionPointerAstType() override = default;

    // The generated empty ctor (the C# `public FunctionPointerAstType()`). The
    // `CallingConventions` collection is a member (the D222 always-present-stack-member design),
    // initialized here with `baseIndex = 0` (its slot index 0) and `supportsIncremental = false`
    // (the node has TWO collections, so neither owns the contiguous `[slotIndex, ..)` range with
    // nothing after it); the `Parameters` collection is a member initialized with `baseIndex = 1`
    // (its slot index 1) and `supportsIncremental = false`. With both collections non-incremental,
    // every `Add`/`Insert`/`Remove`/single-slot-set invalidates the parent's indices for a lazy
    // rebuild (`EnsureChildIndices`). `ReturnType` defaults to null via its default member
    // initializer (a REQUIRED slot -- `CheckInvariant` asserts it is filled). The
    // `HasUnmanagedCallingConvention` bool defaults to `false` (the C# default).
    FunctionPointerAstType() : callingConventions_(this, &CallingConventionsSlot, 0, false),
                               parameters_(this, &ParametersSlot, 1, false) {}

    // ---- The const keyword token (the output-visitor token literal) -------------------
    // The C# `public const string PointerToken = "*"`. Part of the node's public API (the output
    // visitor reads it); port as a `static constexpr const char*` (the `CheckedExpression.
    // `CheckedKeyword` D234 / `ArrayCreateExpression.NewKeyword` D252 precedent). The generator
    // excludes `const string` fields from `MembersToMatch` (it iterates only instance
    // `IPropertySymbol`s), so it never appears in the generated `DoMatch`.
    static constexpr const char* PointerToken = "*";

    // ---- The `HasUnmanagedCallingConvention` scalar (a NON-`[Slot]` bool) --------------
    // The C# `public bool HasUnmanagedCallingConvention { get; set; }` -- a plain bool scalar
    // (NOT a `[Slot]`, so it is NOT in the slot storage; it IS in `MembersToMatch` as a plain-bool
    // plain-equality term; it is NOT a ctor param since the generator adds only settable
    // ENUM-typed scalars to `CtorParams`). No name shadowing (the
    // `HasUnmanagedCallingConvention()` accessor does not collide with any class), so a plain
    // `bool` getter/setter.
    bool HasUnmanagedCallingConvention() const { return hasUnmanagedCallingConvention_; }
    void HasUnmanagedCallingConvention(bool value) { hasUnmanagedCallingConvention_ = value; }

    // ---- The `CallingConventions` collection slot --------------------------------------
    // The generated `[Slot("CallingConvention")] public partial AstNodeCollection<AstType>
    // CallingConventions` -- the calling-convention specifier list (a `CSharpSlotInfoT<AstType>`
    // slot at slot index 0, the node's FIRST collection, non-incremental -- NOT the last slot).
    // The C# lazily allocates the wrapper; the D222 port makes the collection an always-present
    // stack member, so the accessor returns the member directly (the empty-until-first-Add
    // element-list profile is preserved). Reuses the NEW `Slots::CallingConvention` kind (the only
    // new `Slots` constant this node adds). A `const` convenience overload returns `const&` for a
    // `const FunctionPointerAstType*`.
    AstNodeCollectionT<AstType>& CallingConventions() { return callingConventions_; }
    const AstNodeCollectionT<AstType>& CallingConventions() const { return callingConventions_; }

    // ---- The `Parameters` collection slot -----------------------------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot at slot
    // index 1, the node's SECOND collection, non-incremental). `supportsIncremental` is `false`
    // (two collections), so `Add` invalidates the parent's indices. `baseIndex = 1` (the slot
    // index; the dynamic flattened index `ccCount` is rebuilt lazily by `EnsureChildIndices`,
    // since the fast path is off). Reuses the already-ported `Slots::Parameter` kind
    // (cycle-broken into `ParameterDeclaration.hpp` by `IndexerDeclaration` D279). No name
    // shadowing (the `Parameters()` accessor does not collide with the `ParameterDeclaration`
    // class -- a member named `Parameters` is not the name `ParameterDeclaration`).
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `ReturnType` slot (a REQUIRED single `AstType`, NOT a base virtual) ------
    // The generated `[Slot("Type")] public partial AstType ReturnType` -- a single REQUIRED
    // `AstType` slot at slot 2 (the return type of the function pointer). TWO COLLECTIONS precede
    // it (`CallingConventions` at slot 0, `Parameters` at slot 1), so the generator's
    // `constIndex = !slots.Take(2).Any(IsCollection)` is `false`, and the setter uses the
    // index-less `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the
    // flattened index is dynamic -- either collection's count can change). NOT an override, so a
    // plain non-virtual accessor. `CheckInvariant` asserts the `ReturnType` is filled (the slot
    // is required -- the C# `AstType ReturnType` is non-nullable). Reuses the already-ported
    // `Slots::Type` kind (the `Attribute` D240 / `CastExpression` D243 / `InvocationAstType` D313
    // precedent). No name shadowing (the `ReturnType()` accessor does not collide with the
    // `AstType` class -- a member named `ReturnType` is not the name `AstType`), so the setter
    // takes a plain `AstType*` (no cast -- the `SetChild` override `static_cast`s the incoming
    // `AstNode*` to `AstType*`).
    AstType* ReturnType() const { return returnType_; }
    void ReturnType(AstType* value) {
        SetChildNode(returnType_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ---------------
    // `CallingConventionsSlot` (a `CSharpSlotInfoT<AstType>` pointing at
    // `Slots.CallingConvention`, collection -- the node's first collection);
    // `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
    // collection -- the node's second collection); `ReturnTypeSlot` (a
    // `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the `ReturnType` `AstType`
    // is non-nullable, so `IsCollection || IsOptional` is `false`). No name shadowing (no member
    // is named `AstType`/`ParameterDeclaration` -- the `CallingConventions`/`Parameters`/
    // `ReturnType` accessors do not collide with the `AstType`/`ParameterDeclaration` classes), so
    // the element types are the plain classes.
    static inline const CSharpSlotInfoT<AstType> CallingConventionsSlot{"CallingConventions", true, &Slots::CallingConvention, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitFunctionPointerType` (`FunctionPointerAstType` ends in "AstType", so
    // the generator's visit-method-name rewriting yields `VisitFunctionPointerType` -- the
    // `TupleAstType` D290 / `InvocationAstType` D313 precedent).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitFunctionPointerType(this);
    }

    // ---- Slot storage (the generated overrides) -----------------------------------------
    // Three slots in declaration order: a `CallingConventions` collection occupying the contiguous
    // range `[0, ccCount)`, a `Parameters` collection occupying `[ccCount, ccCount + paramCount)`,
    // and a `ReturnType` single slot at index `ccCount + paramCount` (the trailing single slot
    // after TWO collections). `GetChildCount` is `1 + ccCount + paramCount` (the one single slot
    // plus both collections' current lengths -- the single slot contributes 1 to the flattened
    // count even when the `ReturnType` is null); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the
    // slots subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, a collection step, then a single step). `GetCollectionByKind` returns each collection
    // for its kind. This is the `ArrayCreateExpression` D252 two-collection shape with the
    // trailing single REQUIRED (not nullable) and NO leading single, so the dispatch walk is
    // collection -> collection -> single.

    int GetChildCount() const override { return 1 + callingConventions_.Count() + parameters_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = callingConventions_.Count();
            if (i < n)
                return callingConventions_.At(i);
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        if (i == 0)
            return returnType_;
        throw std::out_of_range("FunctionPointerAstType::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = callingConventions_.Count();
            if (i < n) {
                callingConventions_.SetAt(i, static_cast<AstType*>(value));
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
            SetChildNode(returnType_, static_cast<AstType*>(value), index);
            return;
        }
        throw std::out_of_range("FunctionPointerAstType::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = callingConventions_.Count();
            if (i < n)
                return &CallingConventionsSlot;
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        if (i == 0)
            return &ReturnTypeSlot;
        throw std::out_of_range("FunctionPointerAstType::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::CallingConvention)
            return &callingConventions_;
        if (kind == &Slots::Parameter)
            return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is FunctionPointerAstType o &&
    // this.HasUnmanagedCallingConvention == o.HasUnmanagedCallingConvention &&
    // this.CallingConventions.DoMatch(o.CallingConventions, match) &&
    // this.Parameters.DoMatch(o.Parameters, match) &&
    // this.ReturnType.DoMatch(o.ReturnType, match)`. `FunctionPointerAstType` is NOT an
    // `EntityDeclaration` (it is an `AstType`), so there is no `MatchAttributesAndModifiers`/
    // `Name`/`ReturnType` base term (the `ReturnType` here is the node's OWN `[Slot("Type")]`
    // property, not the `EntityDeclaration.ReturnType` virtual). The four terms are in
    // `MembersToMatch` (source declaration) order: the plain-bool `HasUnmanagedCallingConvention`
    // (the `DoMatchTerm` fall-through for a non-enum non-recursive non-string scalar), the
    // `CallingConventions` collection recursive match (the generator emits the collection-typed
    // recursive term directly, NOT `MatchOptional`), the `Parameters` collection recursive match
    // (likewise direct), and the `ReturnType` direct term for a NON-NULLABLE recursive child,
    // which the port routes through `AstNode::MatchRequired` (the D231 [class.access.derived]
    // workaround -- a derived node may not call the protected `DoMatch` through a base `AstType*`;
    // a static member of `AstNode` may, the `MatchOptional`/`MatchRequired` precedent). A
    // type-only mismatch (not a `FunctionPointerAstType`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<FunctionPointerAstType*>(other);
        if (o == nullptr)
            return false;
        return hasUnmanagedCallingConvention_ == o->hasUnmanagedCallingConvention_
            && callingConventions_.DoMatch(o->callingConventions_, match)
            && parameters_.DoMatch(o->parameters_, match)
            && MatchRequired(returnType_, o->returnType_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `HasUnmanagedCallingConvention` scalar copied via the public getter/setter (the
    // `EntityDeclaration.Modifiers` D272 base-scalar-copy precedent -- the backing field is
    // private, so the concrete `Clone` copies the scalar through the public surface), every
    // `CallingConventions` and `Parameters` element deep-cloned through `Add` (which re-parents
    // and re-indexes; `AstType::Clone()` returns `AstType*` which `Add(AstType*)` accepts
    // directly, and `ParameterDeclaration::Clone()` returns `ParameterDeclaration*` which
    // `Add(ParameterDeclaration*)` accepts directly -- both bases redeclare the typed covariant
    // `Clone`, so no `static_cast` is needed, the `InterpolatedStringContent` D309
    // abstract-base-typed-`Clone` precedent), and the `ReturnType` deep-cloned through the setter
    // when present (which re-parents; `AstType::Clone()` returns `AstType*`, which
    // `ReturnType(AstType*)` accepts directly -- the `CastExpression` D243 / `Constraint` D283 /
    // `InvocationAstType` D313 `AstType`-typed-slot precedent). No own location fields (does not
    // derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied. The
    // `ReturnType` is skipped if absent (`Clone` tolerates a missing `ReturnType` even though the
    // slot is required -- the invariant is enforced by `CheckInvariant`, not by `Clone`, the
    // `AnonymousMethodExpression` D306 / `InvocationAstType` D313 precedent). The covariant return
    // is `FunctionPointerAstType*` (through `AstType*`, the `AstType::Clone` pure-virtual -- the
    // typed return the C# `new AstType Clone()` gives, applied to the concrete
    // `FunctionPointerAstType`).
    FunctionPointerAstType* Clone() const override {
        auto* node = new FunctionPointerAstType();
        node->CloneAnnotationsFrom(*this);
        node->HasUnmanagedCallingConvention(hasUnmanagedCallingConvention_);
        for (int i = 0; i < callingConventions_.Count(); i++)
            node->callingConventions_.Add(callingConventions_.At(i)->Clone());
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `callingConventions_` and `parameters_` are the always-present
    // collection members (empty until the first `Add`, non-incremental); `returnType_` is null
    // until the type is set (a REQUIRED slot -- `CheckInvariant` asserts it is filled);
    // `hasUnmanagedCallingConvention_` is the plain bool scalar (default `false`, the C# default).
    // No name shadowing (no member is named `AstType`/`ParameterDeclaration`), so the field types
    // are the plain classes.
    bool hasUnmanagedCallingConvention_ = false;
    AstNodeCollectionT<AstType> callingConventions_;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    AstType* returnType_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_FUNCTIONPOINTERASTTYPE_HPP
