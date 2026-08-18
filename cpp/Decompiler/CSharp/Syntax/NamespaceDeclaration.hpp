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

// Port of the `NamespaceDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/NamespaceDeclaration.cs (the generated
// `NamespaceDeclaration.g.cs` + the hand-written partial, which declares the `IsFileScoped`
// scalar, the two slot properties, the `AddMember` convenience, and the computed
// `Name`/`FullName`/`Identifiers` reads plus the `(string name)` ctor and the
// `ConstructType`/`BuildQualifiedName` helpers). The next in-order Phase-5 piece per the D292
// plan ("the remaining GeneralScope nodes: NamespaceDeclaration the namespace container, ...").
//
// `namespace_declaration ::= 'namespace' type '{' namespace_member* '}' ';'?
//                            | 'namespace' type ';' namespace_member*` (C# grammar 14.3): the
// `namespace Foo.Bar { ... }` block (or the C# 10 file-scoped `namespace Foo.Bar;` form). A sealed
// `AstNode` deriving DIRECTLY from the `AstNode` root (NOT `EntityDeclaration`/`Expression`/
// `Statement`/`AstType` -- a namespace is a container node, not a member declaration; it carries
// no `SymbolKind`/`Modifiers`/`MatchAttributesAndModifiers` -- the `VariableInitializer` D266 /
// `CatchClause` D269 / `Constraint` D283 / `TypeParameterDeclaration` D282 direct-`AstNode`
// precedent). The `[DecompilerAstNode]` default `hasPatternPlaceholder` is `false`, so `final`
// (the C# `sealed`).
//
// The slots and scalars in source declaration order:
//   * `public bool IsFileScoped { get; set; }` -- a non-`[Slot]` settable bool scalar (the C# 10
//     file-scoped `namespace Foo.Bar;` form sets it `true`). Not a `[Slot]` (no child slot), not a
//     ctor param (the generator adds only settable ENUM-typed scalars to `CtorParams`, and a bool
//     is not an enum), so it is set via the property setter. It IS in `MembersToMatch` (the
//     generator adds every non-`[Slot]` instance property), and a bool (not an enum, no `Any`)
//     emits the fall-through plain-equality `DoMatch` term -- the `Attribute.HasArgumentList` D240
//     / `ComposedType.IsDoubleColon` D238 plain-bool precedent. Declared FIRST, so it is the FIRST
//     `DoMatch` term.
//   * `[Slot("NamespaceName")] public partial AstType NamespaceName { get; set; }` -- a single
//     REQUIRED `AstType` child slot at flattened index 0 (the namespace's dotted name, a
//     `MemberType`/`SimpleType` tree, e.g. `Foo.Bar` -> a `MemberType` whose `Target` is
//     `SimpleType("Foo")` and whose `MemberName` is `"Bar"`). The const-index
//     `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
//     re-indexes in place. A `MatchRequired` `DoMatch` term (a non-nullable recursive child -- the
//     generator emits a DIRECT `this.NamespaceName.DoMatch(o.NamespaceName, match)`, ported through
//     `MatchRequired`, the D231 `[class.access.derived]` workaround).
//   * `Name` / `FullName` / `Identifiers` -- computed reads (`[ExcludeFromMatch]`, so NOT in
//     `DoMatch`): `Name` getter calls `UsingDeclaration.ConstructNamespace(NamespaceName)` (the
//     D289-deferred helper, itself deferred because it consumes the D236-deferred `AstType.Create`
//     factory); `FullName` walks the parent `NamespaceDeclaration` chain; `Identifiers` walks the
//     `NamespaceName` `MemberType`/`SimpleType` tree. All DEFERRED (computed reads consumed by the
//     unported output/resolver stage -- the `UsingDeclaration.Namespace`/`ConstructNamespace` D289
//     value-vs-behaviour discriminator).
//   * `[Slot("Member")] public partial AstNodeCollection<AstNode> Members { get; }` -- the
//     collection of namespace-body members (the `{ namespace_member* }` of a block-scoped
//     namespace, or the top-level members of a file-scoped namespace), an
//     `AstNodeCollection<AstNode>` at flattened index 1. The element type is the ABSTRACT `AstNode`
//     root (a namespace body holds any `AstNode`-derived member: `TypeDeclaration`s, other
//     `NamespaceDeclaration`s, `UsingDeclaration`s, `ExternAliasDeclaration`s, ...) -- the
//     `UsingStatement.ResourceAcquisition` D262 `AstNode`-typed-slot precedent applied to a
//     COLLECTION. The collection is the node's only collection and its last slot, so
//     `supportsIncremental` is `TRUE` (`collectionCount == 1 && slotIndex == slots.Count - 1`):
//     an element's flattened `ChildIndex` is exactly `1 + its local position`, maintained
//     incrementally by `Add`/`Insert`/`Remove`. A collection-recursive `DoMatch` term (the
//     generator emits the collection-typed recursive term directly, NOT `MatchOptional`).
//
// The generated `DoMatch` has THREE terms in `MembersToMatch` (source declaration) order:
// `return other is NamespaceDeclaration o && this.IsFileScoped == o.IsFileScoped &&
// this.NamespaceName.DoMatch(o.NamespaceName, match) && this.Members.DoMatch(o.Members, match)`.
// The `IsFileScoped` term is the plain `==` (a bool scalar, not an enum, no `Any`); the
// `NamespaceName` term is a non-nullable recursive child dispatched through `MatchRequired` (the
// D231 `[class.access.derived]` workaround); the `Members` term is the collection recursive
// `DoMatch`. A type-only mismatch (not a `NamespaceDeclaration`) rejects early.
//
// The hand-written `AddMember(AstNode child)` -- a thin convenience that appends to the `Members`
// collection via `AddChild(child, Slots.Member)` (the D223 mutation API). `AddChild` is ported
// (D223), `Slots.Member` lands this iteration, so `AddMember` ports now (the
// `VariableDeclarationStatement` D270 hand-written-ctor-using-`Add` precedent -- a convenience
// whose dependencies are all ported).
//
// The computed `Name`/`FullName`/`Identifiers` reads, the `ConstructType`/`BuildQualifiedName`
// helpers, and the hand-written `(string name)` ctor (which calls `this.Name = name`, whose setter
// uses `ConstructType`) are DEFERRED: the `Name` getter consumes the D289-deferred
// `UsingDeclaration.ConstructNamespace` (which consumes the D236-deferred `AstType.Create`
// factory); the helpers are output/resolver-stage behaviour. A `NamespaceDeclaration` is built
// via the empty or `(AstType)` ctor + `IsFileScoped(...)` + `Members().Add(...)` (or `AddMember`)
// until `ConstructType`/`ConstructNamespace`/`AddRange` land.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitNamespaceDeclaration(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitNamespaceDeclaration`). The generated slot
// statics are `NamespaceNameSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.NamespaceName`,
// required -- the `NamespaceName` is non-nullable) and `MembersSlot` (a
// `CSharpSlotInfoT<AstNode>` pointing at `Slots.Member`, collection). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the `NamespaceName` through the setter (which re-parents; `AstType::Clone()` returns
// `AstType*`, the covariant override, which `NamespaceName(AstType*)` accepts directly) and every
// `Members` element through `Add` (which re-parents and re-indexes; the element type is the
// abstract `AstNode` root, which inherits `AstNode::Clone` returning `AstNode*`, so each element's
// `Clone()` returns `AstNode*` which `Add(AstNode*)` accepts directly -- NO `static_cast` needed,
// unlike the `ExtensionDeclaration.Members` D285 abstract-`EntityDeclaration`-collection path
// where `EntityDeclaration` redeclares no typed `Clone`; `AstNode` IS the root and its `Clone`
// returns `AstNode*`), and copies the `IsFileScoped` scalar and the annotation channel.
//
// C++ name-shadowing crux: NONE. The `IsFileScoped()`/`NamespaceName()`/`Members()` accessors do
// NOT collide with any class in the `Syntax` namespace (no class named `IsFileScoped`/
// `NamespaceName`/`Members` -- there is `NamespaceDeclaration`, not `NamespaceName`; there is
// `MemberType`/`MemberReferenceExpression`, not `Member` or `Members`). The `AstType` element
// type does not collide with any member name (no member is named `AstType`), so no
// elaborated-type-specifier is needed anywhere. The element type `AstNode` likewise does not
// collide (no member is named `AstNode`). This is the cleanest `AstNode`-typed-collection port:
// no name shadowing, no new `AstNode` helper (`MatchRequired` from D231 + the collection
// `DoMatch` from D222 already exist).
//
// TWO new `Slots` constants land in `Slots.hpp` this iteration (no include cycle):
// - `Slots::NamespaceName` (a `CSharpSlotInfoT<AstType>`, the single `AstType` of the namespace's
//   dotted name). The `AstType` abstract base does NOT include `Slots.hpp` (the `Slots.Type`/
//   `Slots.Target`/`Slots.BaseType`/`Slots.Import` precedent), so this kind lives in `Slots.hpp`
//   (no include cycle). No `Slots` variable is named `AstType`, and no class named
//   `NamespaceName` lives in `Syntax`, so the unqualified `AstType` resolves to the class and no
//   elaborated specifier is needed.
// - `Slots::Member` (a `CSharpSlotInfoT<AstNode>`, the collection of namespace-body members). The
//   `AstNode` root base does NOT include `Slots.hpp` (the `Slots.ResourceAcquisition` D262
//   `AstNode`-typed-kind precedent -- the only other `CSharpSlotInfoT<AstNode>`), so this kind
//   lives in `Slots.hpp` (no include cycle). No `Slots` variable is named `AstNode`, and no class
//   named `Member` lives in `Syntax`, so the unqualified `AstNode` resolves to the class and no
//   elaborated specifier is needed. The kind is the FIRST `AstNode`-typed COLLECTION kind (the
//   `Slots.ResourceAcquisition` precedent was a single slot); the shared constant is constructed
//   non-collection/non-optional; the per-node `MembersSlot` carries the `IsCollection` flag.
//
// The generated ctors (`WriteConstructors`): `CtorParams` in declaration order is `[NamespaceName
// (required single), Members (collection, optional)]` -- the `IsFileScoped` bool is NOT a ctor
// param (only settable ENUM-typed scalars are). `RequiredConstructorPrefixLength` is 1 (through
// the last non-optional param `NamespaceName` at index 0); `ConstructorPrefixLengths` is `{1, 2}`.
// The `(len=2)` all-params ctor calls `this.Members.AddRange(...)` (the `AddRange` convenience,
// the D222 deferral), so it is DEFERRED; the empty + the `(AstType)` required-prefix ctors cover
// the construction API. The `(AstType)` ctor is `explicit` (a single-argument ctor is a converting
// ctor by default), matching the generator's public ctor but avoiding an implicit
// `AstType -> NamespaceDeclaration` conversion (the `Constraint` D283 / `SwitchStatement` D268
// precedent). A members list is built via `Members().Add(...)` (or `AddMember(...)`) until
// `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_NAMESPACEDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_NAMESPACEDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class NamespaceDeclaration : AstNode`. `final` (the C# `sealed`):
// no further derivation. The `Constraint` D283 shape (a single required child at index 0 + an
// incremental collection at index 1) with a direct-`AstNode` base, the child `AstType`, the
// collection element the abstract `AstNode` root, and an `IsFileScoped` bool scalar declared
// before the slots.
class NamespaceDeclaration final : public AstNode {
public:
    ~NamespaceDeclaration() override = default;

    // The generated empty ctor (the C# `public NamespaceDeclaration()`). The `Members` collection
    // is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `NamespaceName` single slot at index 0 precedes it) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so an
    // element's flattened `ChildIndex` is exactly `1 + its local position`). `NamespaceName`
    // defaults to null (no name); it is a required slot, so a default-constructed node is only
    // valid until the name is set (or until `DoMatch`/`CheckInvariant` observe the missing slot).
    // `IsFileScoped` defaults to false (the block-scoped form).
    NamespaceDeclaration() : members_(this, &MembersSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public NamespaceDeclaration(AstType
    // namespaceName)`): the required prefix runs through the last non-optional ctor param
    // (`NamespaceName` is required; `Members` is an optional collection). Sets `NamespaceName` in
    // declaration order. Delegates to the empty ctor so the collection member is initialized.
    // `explicit` (a single-argument ctor is a converting ctor by default), matching the
    // generator's public ctor but avoiding an implicit `AstType -> NamespaceDeclaration`
    // conversion (the `Constraint` D283 precedent).
    explicit NamespaceDeclaration(AstType* namespaceName) : NamespaceDeclaration() {
        NamespaceName(namespaceName);
    }

    // ---- The `IsFileScoped` bool scalar (a plain property, not a `[Slot]`) -----------------
    // The C# `public bool IsFileScoped { get; set; }` -- whether the namespace is the C# 10
    // file-scoped `namespace Foo.Bar;` form (`true`) or the classic `namespace Foo.Bar { ... }`
    // block form (`false`). A plain bool field: not a child slot (no `[Slot]`), not a ctor param
    // (the generator adds only settable ENUM-typed scalars to `CtorParams`, and a bool is not an
    // enum), so it is set via the property setter. It IS in `MembersToMatch` (the generator adds
    // every non-`[Slot]` instance property), and a bool (not an enum, no `Any`) emits the
    // fall-through plain-equality `DoMatch` term -- the `Attribute.HasArgumentList` D240 /
    // `ComposedType.IsDoubleColon` D238 plain-bool precedent. Declared FIRST in the source, so it
    // is the FIRST `DoMatch` term. No name shadowing (no class named `IsFileScoped`).
    bool IsFileScoped() const { return isFileScoped_; }
    void IsFileScoped(bool value) { isFileScoped_ = value; }

    // ---- The `NamespaceName` slot (a single REQUIRED `AstType` child) ----------------------
    // The generated `[Slot("NamespaceName")] public partial AstType NamespaceName` -- a single
    // non-nullable `AstType` slot at flattened index 0 (the namespace's dotted name, a
    // `MemberType`/`SimpleType` tree). The const-index `SetChildNode(ref field, value, 0)` setter
    // (no collection precedes it) re-parents and re-indexes in place. NO name shadowing (the
    // `NamespaceName()` accessor does not collide with the `AstType` base type -- no class named
    // `NamespaceName` lives in the `Syntax` namespace, and no member is named `AstType`), so the
    // operand type is the plain `AstType` (no elaborated specifier).
    AstType* NamespaceName() const { return namespaceName_; }
    void NamespaceName(AstType* value) {
        SetChildNode(namespaceName_, value, 0);
    }

    // ---- The `Members` collection slot ----------------------------------------------------
    // The generated `[Slot("Member")] public partial AstNodeCollection<AstNode> Members` -- the
    // collection of namespace-body members (the `{ namespace_member* }` of a block-scoped
    // namespace or the top-level members of a file-scoped namespace), an
    // `AstNodeCollection<AstNode>` at flattened index 1, the node's only collection and last slot.
    // The element type is the ABSTRACT `AstNode` root (a namespace body holds any `AstNode`-derived
    // member: `TypeDeclaration`s, other `NamespaceDeclaration`s, `UsingDeclaration`s,
    // `ExternAliasDeclaration`s, ...) -- the `UsingStatement.ResourceAcquisition` D262
    // `AstNode`-typed-slot precedent applied to a COLLECTION (the first `AstNode`-typed
    // collection kind). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly (the
    // empty-until-first-`Add` element-list profile is preserved). `supportsIncremental` is `true`
    // (the node's only collection and last slot), so `Add` maintains each element's flattened
    // `ChildIndex` incrementally. NO name shadowing (the `Members()` accessor does not collide with
    // any class -- no class named `Member` or `Members` in `Syntax`), so the `AstNode` element
    // type needs no elaborated specifier.
    AstNodeCollectionT<AstNode>& Members() { return members_; }
    const AstNodeCollectionT<AstNode>& Members() const { return members_; }

    // The hand-written `public void AddMember(AstNode child)` -- a thin convenience that appends
    // to the `Members` collection via `AddChild(child, Slots.Member)` (the D223 mutation API).
    // `AddChild` is ported (D223) and `Slots.Member` lands this iteration, so this ports now (a
    // null child is a no-op, the `AddChild` null guard). The `VariableDeclarationStatement` D270
    // hand-written-ctor-using-`Add` precedent applied to an `Add`-member convenience.
    void AddMember(AstNode* child) {
        AddChild(child, &Slots::Member);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // The `NamespaceNameSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.NamespaceName`,
    // required -- the `NamespaceName` is non-nullable, so `IsCollection || IsOptional` is
    // `false`); the `MembersSlot` (a `CSharpSlotInfoT<AstNode>` pointing at `Slots.Member`,
    // collection). NO name shadowing (no member is named `AstType`/`AstNode`), so the element types
    // are the plain classes.
    static inline const CSharpSlotInfoT<AstType> NamespaceNameSlot{"NamespaceName", false, &Slots::NamespaceName, false};
    static inline const CSharpSlotInfoT<AstNode> MembersSlot{"Members", true, &Slots::Member, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitNamespaceDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitNamespaceDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // A `NamespaceName` single slot at index 0 and a `Members` collection occupying the
    // contiguous range `[1, 1 + Count)`. `GetChildCount` is `1 + Count` (the single slot plus the
    // collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single case
    // then a collection step). `GetCollectionByKind` returns the `Members` collection for the
    // `Member` kind (the node's only collection). This is the `Constraint` D283 / `Attribute` D240
    // dispatch shape.

    int GetChildCount() const override { return 1 + members_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return namespaceName_;
        i--;
        int n = members_.Count();
        if (i < n)
            return members_.At(i);
        throw std::out_of_range("NamespaceDeclaration::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(namespaceName_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        int n = members_.Count();
        if (i < n) {
            members_.SetAt(i, value);
            return;
        }
        throw std::out_of_range("NamespaceDeclaration::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &NamespaceNameSlot;
        i--;
        int n = members_.Count();
        if (i < n)
            return &MembersSlot;
        throw std::out_of_range("NamespaceDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Member)
            return &members_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is NamespaceDeclaration o && this.IsFileScoped == o.IsFileScoped &&
    // this.NamespaceName.DoMatch(o.NamespaceName, match) && this.Members.DoMatch(o.Members, match)`.
    // The terms are in `MembersToMatch` order, which is the source declaration order
    // (`IsFileScoped`, `NamespaceName`, `Members` -- the `Name`/`FullName`/`Identifiers` reads are
    // `[ExcludeFromMatch]` and skipped). The `IsFileScoped` term is the fall-through
    // plain-equality (a bool scalar, not an enum, no `Any`); the `NamespaceName` term is a
    // non-nullable recursive child, so the generator emits a DIRECT
    // `this.NamespaceName.DoMatch(o.NamespaceName, match)` -- ported through `MatchRequired` (the
    // D231 `[class.access.derived]` workaround); the `Members` term is the collection recursive
    // match (the generator emits the collection-typed recursive term directly, NOT `MatchOptional`,
    // which it emits only for a nullable non-collection child). A type-only mismatch (not a
    // `NamespaceDeclaration`) rejects early. The `IsFileScoped` plain-`==` is the FIRST term, so a
    // scalar mismatch rejects before the recursive terms are observed.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<NamespaceDeclaration*>(other);
        if (o == nullptr)
            return false;
        return isFileScoped_ == o->isFileScoped_
            && MatchRequired(namespaceName_, o->namespaceName_, match)
            && members_.DoMatch(o->members_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `IsFileScoped` scalar copied, the annotation
    // channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern),
    // the `NamespaceName` deep-cloned through the setter (which re-parents; `AstType::Clone()`
    // returns `AstType*`, the covariant override, which `NamespaceName(AstType*)` accepts
    // directly), and every `Members` element deep-cloned through `Add` (which re-parents and
    // re-indexes; the element type is the abstract `AstNode` root, which inherits
    // `AstNode::Clone` returning `AstNode*`, so each element's `Clone()` returns `AstNode*` which
    // `Add(AstNode*)` accepts directly -- NO `static_cast` needed, unlike the
    // `ExtensionDeclaration.Members` D285 abstract-`EntityDeclaration`-collection path where
    // `EntityDeclaration` redeclares no typed `Clone`; `AstNode` IS the root and its `Clone`
    // returns `AstNode*`). No own location fields (`NamespaceDeclaration` does not derive
    // `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `Constraint` D283 no-location-copy precedent). The covariant return is `NamespaceDeclaration*`
    // (through `AstNode*`, the `AstNode::Clone` virtual).
    NamespaceDeclaration* Clone() const override {
        auto* node = new NamespaceDeclaration();
        node->isFileScoped_ = isFileScoped_;
        node->CloneAnnotationsFrom(*this);
        if (namespaceName_ != nullptr)
            node->NamespaceName(namespaceName_->Clone());
        for (int i = 0; i < members_.Count(); i++)
            node->members_.Add(members_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `isFileScoped_` is the plain bool scalar (default false -- the block-scoped
    // form); `namespaceName_` is null until the name is set (a required slot -- `CheckInvariant`
    // asserts it is filled); `members_` is the always-present collection member (empty until the
    // first `Add`, incremental). NO name shadowing (no member is named `AstType`/`AstNode`), so the
    // field types are the plain classes.
    bool isFileScoped_ = false;
    AstType* namespaceName_ = nullptr;
    AstNodeCollectionT<AstNode> members_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_NAMESPACEDECLARATION_HPP
