// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
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
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `SyntaxTree` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/SyntaxTree.cs (the generated `SyntaxTree.g.cs` + the
// hand-written partial, which declares only the `Members` slot and the `GetTypes` walk helper).
// The last remaining GeneralScope node and the 108th in-order Phase-5 piece per the D314 plan
// ("the next in-order Phase-5 piece is SyntaxTree (the root compilation_unit node reusing
// Slots::Member), the last remaining GeneralScope node, then the OutputVisitor").
//
// `compilation_unit ::= extern_alias_directive* using_directive* global_attributes?
//                        compilation_unit_body` (C# grammar 14.2): the root of a decompiled
// source file -- the top-level container of the file's `extern alias`/`using` directives,
// global attributes, and namespace/type declarations. A `SyntaxTree` is the root an
// `ILAstToCSharp` run builds and the output visitor pretty-prints; it is NOT itself a member
// declaration, an expression, a statement, or a type -- it derives DIRECTLY from the `AstNode`
// root (the `NamespaceDeclaration` D293 / `Constraint` D283 / `CatchClause` D269
// direct-`AstNode` precedent), carrying no `SymbolKind`/`Modifiers`/`MatchAttributesAndModifiers`
// (those live on `EntityDeclaration`-derived members).
//
// It is the SIMPLEST collection-only port applied to a direct-`AstNode` root: the
// `ArrayInitializerExpression` D250 / `TupleExpression` D296 / `AnonymousTypeCreateExpression`
// D304 / `BlockStatement` D256 collection-only shape (a sole collection slot, no single slots,
// no scalars) with the collection element the ABSTRACT `AstNode` root. The single slot:
//   * `[Slot("Member")] public partial AstNodeCollection<AstNode> Members { get; }` -- the
//     collection of top-level members (the `extern_alias_directive*`/`using_directive*`/
//     `global_attributes?`/`namespace_declaration*`/`type_declaration*` of the compilation
//     unit body), an `AstNodeCollection<AstNode>` at flattened index 0. The element type is the
//     ABSTRACT `AstNode` root (a compilation unit holds any `AstNode`-derived top-level member:
//     `NamespaceDeclaration`s, `TypeDeclaration`s, `DelegateDeclaration`s, `UsingDeclaration`s,
//     `ExternAliasDeclaration`s, ...) -- the `NamespaceDeclaration.Members` D293
//     `AstNode`-typed-collection precedent. The collection is the node's only collection and
//     its last/only slot, so `supportsIncremental` is `TRUE` (`collectionCount == 1 &&
//     slotIndex == slots.Count - 1`): an element's flattened `ChildIndex` is exactly its local
//     position, maintained incrementally by `Add`/`Insert`/`Remove`. A collection-recursive
//     `DoMatch` term (the generator emits the collection-typed recursive term directly, NOT
//     `MatchOptional`).
//
// The generated `DoMatch` has a SINGLE term: `return other is SyntaxTree o &&
// this.Members.DoMatch(o.Members, match)`. The single term is the collection recursive match; a
// type-only mismatch (not a `SyntaxTree`) rejects early. This is the simplest generated
// `DoMatch` shape (the `ArrayInitializerExpression` D250 single-collection-term precedent
// applied to a direct-`AstNode` root).
//
// The hand-written `public IEnumerable<EntityDeclaration> GetTypes(bool includeInnerTypes =
// false)` -- a stack-based tree walk that yields every `TypeDeclaration`/`DelegateDeclaration`
// in the tree (descending into non-`Statement`/non-`Expression` children whose slot kind is not
// `Slots.TypeMember`, optionally including inner types) -- is DEFERRED: it is a behaviour helper
// consumed only by the unported output/resolver stage (no ported engine code calls it -- the
// `NamespaceDeclaration.Name`/`FullName`/`Identifiers` D293 value-vs-behaviour discriminator
// applied to a tree-walk helper). Its dependencies (`Children` D221, the `TypeDeclaration`/
// `DelegateDeclaration` is-a checks, `Slot`/`Kind`, `Slots.TypeMember`) ARE all ported, but a
// `SyntaxTree` is built and walked by the engine through the `Members` collection directly; the
// helper lands with the output stage.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitSyntaxTree(this)` (the class name does not end in "AstType", so the generator's
// visit-method-name default yields `VisitSyntaxTree`). The generated slot static is `MembersSlot`
// (a `CSharpSlotInfoT<AstNode>` pointing at `Slots.Member`, collection). `Clone` is inherited in
// C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// every `Members` element is deep-cloned through `Add` (which re-parents and re-indexes; the
// element type is the abstract `AstNode` root, which inherits `AstNode::Clone` returning
// `AstNode*`, so each element's `Clone()` returns `AstNode*` which `Add(AstNode*)` accepts
// directly -- NO `static_cast` needed, the `NamespaceDeclaration.Members` D293
// `AstNode`-IS-the-root precedent). No own location fields (`SyntaxTree` does not derive
// `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
// `NamespaceDeclaration` D293 / `Constraint` D283 no-location-copy precedent).
//
// C++ name-shadowing crux: NONE. The `Members()` accessor does NOT collide with any class in the
// `Syntax` namespace (no class named `Member` or `Members` -- there is `MemberType`/
// `MemberReferenceExpression`, not `Member` or `Members`), and the `AstNode` element type does
// not collide with any member name (no member is named `AstNode`), so no elaborated-type-specifier
// is needed anywhere. This is the cleanest collection-only `AstNode`-root port: no name
// shadowing, no new `Slots` constant (`Slots.Member` was added by `NamespaceDeclaration` D293),
// no new `AstNode` helper (the collection `DoMatch` from D222 already exists), no new enum.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Members (collection, optional)]`
// -- there are no non-optional ctor params, so `RequiredConstructorPrefixLength` is 0 and the
// only generated ctor is the empty one; the collection-param ctors (the `IEnumerable<AstNode>`
// and the `params AstNode[]` forms) call `AddRange` (the D222 deferral), so they are DEFERRED.
// The empty ctor is the only portable ctor; a members list is built via `Members().Add(...)`
// until `AddRange` lands (the `ArrayInitializerExpression` D250 / `AnonymousTypeCreateExpression`
// D304 collection-only-ctor precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SYNTAXTREE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SYNTAXTREE_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class SyntaxTree : AstNode`. `final` (the C# `sealed`): no
// further derivation (the `[DecompilerAstNode]` default `hasPatternPlaceholder` is `false`, so no
// `PatternPlaceholder` derives from it). The `ArrayInitializerExpression` D250 collection-only
// shape applied to a direct-`AstNode` root -- the simplest collection-only port: a sole
// `Members` collection (incremental, the node's only collection and last/only slot) and nothing
// else.
class SyntaxTree final : public AstNode {
public:
    ~SyntaxTree() override = default;

    // The generated empty ctor (the C# `public SyntaxTree()`). The `Members` collection is a
    // member (the D222 always-present-stack-member design), initialized here with `baseIndex = 0`
    // (it is the node's only slot, so the first element's flattened `ChildIndex` is 0) and
    // `supportsIncremental = true` (it is the node's only collection and last slot, so an
    // element's flattened `ChildIndex` is exactly its local position). The collection starts
    // empty (no members); the node has no required single slots, so a default-constructed
    // `SyntaxTree` is a valid empty compilation unit (`CheckInvariant` passes).
    SyntaxTree() : members_(this, &MembersSlot, 0, true) {}

    // ---- The `Members` collection slot -----------------------------------------------
    // The generated `[Slot("Member")] public partial AstNodeCollection<AstNode> Members` -- the
    // collection of top-level members (the compilation-unit body), an `AstNodeCollection<AstNode>`
    // at flattened index 0, the node's only collection and last/only slot. The element type is the
    // ABSTRACT `AstNode` root (a compilation unit holds any `AstNode`-derived top-level member --
    // the `NamespaceDeclaration.Members` D293 `AstNode`-typed-collection precedent). The C#
    // lazily allocates the wrapper; the D222 port makes the collection an always-present stack
    // member, so the accessor returns the member directly (the empty-until-first-`Add`
    // element-list profile is preserved). `supportsIncremental` is `true` (the node's only
    // collection and last slot), so `Add` maintains each element's flattened `ChildIndex`
    // incrementally. NO name shadowing (the `Members()` accessor does not collide with any class
    // -- no class named `Member` or `Members` in `Syntax`), so the `AstNode` element type needs
    // no elaborated specifier.
    AstNodeCollectionT<AstNode>& Members() { return members_; }
    const AstNodeCollectionT<AstNode>& Members() const { return members_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag is
    // true (the slot is a collection); the `IsOptional` flag is true (the generator sets it true
    // for every collection slot). The kind is `Slots::Member` (the shared "Member" slot name,
    // ported by `NamespaceDeclaration` D293 as the collection kind for a namespace body -- reused
    // here as the kind for the compilation-unit body, the second reuse of `Slots::Member` as a
    // collection kind). NO name shadowing (no member is named `AstNode`), so the element type is
    // the plain class.
    static inline const CSharpSlotInfoT<AstNode> MembersSlot{"Members", true, &Slots::Member, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSyntaxTree`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSyntaxTree(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------
    // A single `Members` collection occupying the contiguous range `[0, Count)`. `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- no single-slot count term,
    // the `ArrayInitializerExpression` D250 collection-only precedent); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the single collection slot (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape with one
    // collection step and no single step). `GetCollectionByKind` returns the `Members` collection
    // for the `Member` kind (the node's only collection).

    int GetChildCount() const override { return members_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = members_.Count();
        if (i < n)
            return members_.At(i);
        throw std::out_of_range("SyntaxTree::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = members_.Count();
        if (i < n) {
            members_.SetAt(i, value);
            return;
        }
        throw std::out_of_range("SyntaxTree::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = members_.Count();
        if (i < n)
            return &MembersSlot;
        throw std::out_of_range("SyntaxTree::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Member)
            return &members_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SyntaxTree o && this.Members.DoMatch(o.Members, match)`. The single term
    // is the collection recursive match (the generator emits the collection-typed recursive term
    // directly, NOT `MatchOptional`, which it emits only for a nullable NON-collection child). A
    // type-only mismatch (not a `SyntaxTree`) rejects early. This is the simplest generated
    // `DoMatch` (the `ArrayInitializerExpression` D250 single-collection-term precedent).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<SyntaxTree*>(other);
        if (o == nullptr)
            return false;
        return members_.DoMatch(o->members_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied (`CloneAnnotationsFrom`
    // + `ReparentTrivia`, the D223 concrete-clone pattern), and every `Members` element deep-cloned
    // through `Add` (which re-parents and re-indexes; the element type is the abstract `AstNode`
    // root, which inherits `AstNode::Clone` returning `AstNode*`, so each element's `Clone()`
    // returns `AstNode*` which `Add(AstNode*)` accepts directly -- NO `static_cast` needed, the
    // `NamespaceDeclaration.Members` D293 `AstNode`-IS-the-root precedent). No own location fields
    // (`SyntaxTree` does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation`
    // are not copied (the `NamespaceDeclaration` D293 no-location-copy precedent). The covariant
    // return is `SyntaxTree*` (through `AstNode*`, the `AstNode::Clone` virtual).
    SyntaxTree* Clone() const override {
        auto* node = new SyntaxTree();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < members_.Count(); i++)
            node->members_.Add(members_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `members_` is the always-present collection member (empty until the
    // first `Add`, incremental). NO name shadowing (no member is named `AstNode`), so the field
    // type is the plain class.
    AstNodeCollectionT<AstNode> members_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SYNTAXTREE_HPP
