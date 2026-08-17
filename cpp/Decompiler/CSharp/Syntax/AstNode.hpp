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

// Port of the `AstNode` abstract base in ICSharpCode.Decompiler/CSharp/Syntax/AstNode.cs
// (the first slice -- the foundation the generated C# AST node hierarchy, the output
// visitor's span recording, and the pattern matcher all build on). This is the start of
// the "rest of the AstNode base" the D218/D219 plan pairs with `CSharpSlotInfo`; the two
// land together because the slot's is-a test (`CSharpSlotInfo.IsInstanceOfType`) needs the
// `AstNode` hierarchy to be polymorphic for its `dynamic_cast`-based type check.
//
// This header ports the stable, self-contained core of `AstNode`:
//   * the source-location pair (`StartLocation`/`EndLocation`, stored at print time and
//     overridable by single-token leaf nodes), reusing the ported `TextLocation`;
//   * the parent / flattened-child-index / lazy-reindex machinery (`Parent`, `ChildIndex`,
//     `ChildIndicesValid`, `InvalidateChildIndices`, `EnsureChildIndices`) the slot storage
//     contract and sibling navigation consult;
//   * the slot this node occupies in its parent (`Slot`), returning the parent's
//     `CSharpSlotInfo` for this child's flattened index;
//   * the slot storage contract the generated concrete nodes override -- `GetChildCount`,
//     `GetChild`, `SetChild`, `GetChildSlotInfo`, `GetCollectionByKind`, `CloneChildrenInto`
//     -- with the zero-child defaults a childless node (and the pattern placeholder) keep;
//   * sibling/child navigation (`NextSibling`, `PrevSibling`, `FirstChild`, `LastChild`,
//     `HasChildren`); and
//   * the `INode` pattern-match hooks (`DoMatch`/`DoMatchCollection`) -- `AstNode : INode`
//     in the C#, so the base implements the interface by delegating to the abstract
//     `DoMatch(AstNode*, Match)` the concrete nodes supply.
//
// Deferred to later Phase-5 slices (each is a larger, separately testable unit): the
// `AbstractAnnotatable` annotation channel (and the `NodeTrivia` trivia it carries), the
// `AstNodeCollection<T>` collection slots, the `ChildrenCollection`/`ChildEnumerator`
// enumerator, the ancestor/descendant walks, the mutation API (`AddChild`/`InsertChild*`/
// `Remove`/`ReplaceWith`/`Clone` with its `ValidateNewSingleChild` tree invariants), the
// `IAstVisitor`/`AcceptVisitor` dispatch, the `ToString`/`CSharpOutputVisitor` rendering,
// and the `Contains`/`IsInside`/`GetNextNode`/`GetPrevNode` queries. The `Trivia` branch of
// the sibling/slot accessors is dropped along with trivia itself (a trivia node has no
// child slot; it is restored when the trivia system lands).
//
// Field access: `parent_` stays private with a read-only `Parent()` and the `SetParent`/
// `ClearParentAndIndex` accessors the C# generated code calls (the C# `parent` is private
// and written only through those helpers). `ChildIndex` is a PUBLIC field, deviating from
// the C# `internal int childIndex`: the generated `SetChildNode` helpers and the collection
// write the child's index directly (`value.childIndex = index`), and C++'s protected-member
// access rule forbids a derived class from writing a protected base member through a base
// pointer (`value->childIndex_` where `value` is `AstNode*`), so a public field (the
// `ILInstruction::ChildIndex` precedent in the ported IL AST) is the faithful-enough
// adaptation. `childIndicesValid_` is private, read through `ChildIndicesValid()` and
// invalidated through `InvalidateChildIndices()`; the `internal` slot-storage-contract
// virtuals and helpers are public, matching the port's internal-to-public convention (the
// `Match` precedent). The future `AstNodeCollection` sibling (not derived from `AstNode`)
// reaches `ChildIndex` and the helpers through that public surface.

#pragma once

#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"

#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// Forward declaration: the collection slot type is ported later; the base only needs
// it as the (nullable) return of `GetCollectionByKind`.
class AstNodeCollection;

// The common base of every C# AST node. Abstract: a concrete node overrides at least
// `DoMatch` and the slot-storage virtuals for the slots it declares.
class AstNode : public PatternMatching::INode {
    TextLocation startLocation_ = TextLocation::Empty;
    TextLocation endLocation_ = TextLocation::Empty;

    AstNode* parent_ = nullptr;

    // Whether this node's children currently carry correct flattened `ChildIndex`
    // values. Cleared on every structural mutation, restored lazily on the first
    // index read, so bulk construction renumbers once instead of once per mutation.
    bool childIndicesValid_ = true;

public:
    // Flattened index of this node within its parent's child-index space (-1 when
    // unparented). Recomputed lazily by the parent (`EnsureChildIndices`) after a
    // structural mutation. A public field because the generated `SetChildNode`
    // helpers and the collection write it directly (see the file header note).
    int ChildIndex = -1;

    virtual ~AstNode() = default;

    AstNode() = default;
    AstNode(const AstNode&) = delete;
    AstNode& operator=(const AstNode&) = delete;

    // ---- Source location --------------------------------------------------
    // The C# `virtual TextLocation StartLocation`. Source locations are assigned at
    // print time (the output visitor brackets every node with StartNode/EndNode); a
    // single-token leaf overrides these with its own value.
    virtual TextLocation StartLocation() const { return startLocation_; }
    virtual TextLocation EndLocation() const { return endLocation_; }

    // The C# `internal void StorePrintStart/StorePrintEnd` -- the output visitor
    // records the writer's position bracketing this node.
    void StorePrintStart(TextLocation value) { startLocation_ = value; }
    void StorePrintEnd(TextLocation value) { endLocation_ = value; }

    // ---- Parent / child index --------------------------------------------
    // The C# `public AstNode? Parent` (read-only; written through the helpers below).
    AstNode* Parent() const { return parent_; }

    // The C# `internal bool ChildIndicesValid`.
    bool ChildIndicesValid() const { return childIndicesValid_; }

    // The C# `internal void InvalidateChildIndices` -- marks this node's children's
    // flattened indices stale (called by the slot setters and the collection after
    // any structural change).
    void InvalidateChildIndices() { childIndicesValid_ = false; }

    // The C# `internal void SetParent(AstNode newParent)`.
    void SetParent(AstNode* newParent) { parent_ = newParent; }

    // The C# `internal void ClearParentAndIndex` -- detaches this node (the Trivia
    // branch is dropped with trivia itself).
    void ClearParentAndIndex() {
        parent_ = nullptr;
        ChildIndex = -1;
    }

    // The C# `private void EnsureChildIndices` -- assigns each child its flattened
    // index if a mutation has invalidated them. The flattened-index arithmetic lives
    // in the (generated, overriding) `GetChild`/`GetChildCount`, which depend only on
    // the backing fields, so this is well-defined regardless of the current values.
    void EnsureChildIndices() {
        if (childIndicesValid_)
            return;
        childIndicesValid_ = true;
        int count = GetChildCount();
        for (int i = 0; i < count; i++) {
            AstNode* c = GetChild(i);
            if (c != nullptr)
                c->ChildIndex = i;
        }
    }

    // ---- Slot ------------------------------------------------------------
    // The C# `public CSharpSlotInfo? Slot` -- the slot this node occupies in its
    // parent, or null if unparented. (The Trivia branch is dropped with trivia.)
    const CSharpSlotInfo* Slot() {
        if (parent_ == nullptr)
            return nullptr;
        parent_->EnsureChildIndices();
        return parent_->GetChildSlotInfo(ChildIndex);
    }

    // ---- Slot storage contract (the generated overrides) ----------------
    // Each concrete node's slots form a flattened child-index space in source
    // declaration order. A single slot occupies one index (null when empty); a
    // collection slot occupies a contiguous run of its current length. The defaults
    // are the zero-child case a childless node (and the pattern placeholder) keep.

    virtual int GetChildCount() const { return 0; }

    virtual AstNode* GetChild(int index) const {
        throw std::out_of_range("AstNode::GetChild: index out of range");
    }

    virtual void SetChild(int index, AstNode* value) {
        throw std::out_of_range("AstNode::SetChild: index out of range");
    }

    // The CSharpSlotInfo for the slot at the given flattened index.
    virtual const CSharpSlotInfo* GetChildSlotInfo(int index) const {
        throw std::out_of_range("AstNode::GetChildSlotInfo: index out of range");
    }

    // The collection occupying the slot with the given kind, or null if none.
    virtual AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) {
        (void)kind;
        return nullptr;
    }

    // Deep-copies this node's children into the (memberwise-cloned) copy, which
    // initially shares this node's child references.
    virtual void CloneChildrenInto(AstNode* copy) { (void)copy; }

    // ---- Sibling / child navigation --------------------------------------
    // The C# `public AstNode? NextSibling`. The trivia fast-path is dropped with
    // trivia; the rest scans the parent's slot space from this node's index.
    AstNode* NextSibling() {
        if (parent_ == nullptr)
            return nullptr;
        if (!parent_->ChildIndicesValid())
            parent_->EnsureChildIndices();
        int count = parent_->GetChildCount();
        for (int i = ChildIndex + 1; i < count; i++) {
            AstNode* c = parent_->GetChild(i);
            if (c != nullptr)
                return c;
        }
        return nullptr;
    }

    // The C# `public AstNode? PrevSibling`.
    AstNode* PrevSibling() {
        if (parent_ == nullptr)
            return nullptr;
        if (!parent_->ChildIndicesValid())
            parent_->EnsureChildIndices();
        for (int i = ChildIndex - 1; i >= 0; i--) {
            AstNode* c = parent_->GetChild(i);
            if (c != nullptr)
                return c;
        }
        return nullptr;
    }

    // The C# `public AstNode? FirstChild`.
    AstNode* FirstChild() const {
        int count = GetChildCount();
        for (int i = 0; i < count; i++) {
            AstNode* c = GetChild(i);
            if (c != nullptr)
                return c;
        }
        return nullptr;
    }

    // The C# `public AstNode? LastChild`.
    AstNode* LastChild() const {
        for (int i = GetChildCount() - 1; i >= 0; i--) {
            AstNode* c = GetChild(i);
            if (c != nullptr)
                return c;
        }
        return nullptr;
    }

    // The C# `public bool HasChildren` => `FirstChild != null`.
    bool HasChildren() const { return FirstChild() != nullptr; }

    // ---- Pattern matching (INode) ---------------------------------------
    // The abstract `DoMatch(AstNode?, Match)` the concrete nodes supply (the C#
    // `protected internal abstract`). `protected`: the concrete nodes (derived)
    // override it.
protected:
    virtual bool DoMatch(AstNode* other, PatternMatching::Match match) = 0;

public:
    // The C# `bool INode.DoMatch(INode?, Match)` explicit implementation: a non-null
    // candidate that is not an AstNode fails (matches only AstNodes or an absent
    // child), then delegates to the typed `DoMatch`.
    bool DoMatch(PatternMatching::INode* other, PatternMatching::Match match) override {
        AstNode* o = dynamic_cast<AstNode*>(other);
        return (other == nullptr || o != nullptr) && DoMatch(o, match);
    }

    // The C# `bool INode.DoMatchCollection(IReadOnlyList<INode>, int, Match,
    // BacktrackingInfo)`: a single AstNode matches a single element -- succeeds only
    // if the element is absent or an AstNode -- so the backtracking stack is unused.
    bool DoMatchCollection(const std::vector<PatternMatching::INode*>& other, int pos,
                           PatternMatching::Match match,
                           PatternMatching::BacktrackingInfo& /*backtrackingInfo*/) override {
        PatternMatching::INode* raw = pos < static_cast<int>(other.size())
            ? other[static_cast<std::size_t>(pos)] : nullptr;
        AstNode* o = dynamic_cast<AstNode*>(raw);
        return (raw == nullptr || o != nullptr) && DoMatch(o, match);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax
