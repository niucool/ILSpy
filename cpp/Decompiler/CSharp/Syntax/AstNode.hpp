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
//     `HasChildren`);
//   * child enumeration (`Children` -- the `ChildrenCollection` view + the
//     `ChildEnumerator` mutation-tolerant enumerator);
//   * the ancestor/descendant tree walks (`Ancestors`/`AncestorsAndSelf`,
//     `Descendants`/`DescendantsAndSelf`/`DescendantNodes`/`DescendantNodesAndSelf`),
//     the `GetParent<T>`/`GetParent(pred)` ancestor lookups, the `GetNextNode`/`GetPrevNode`
//     document-order walk, and the `Contains`/`IsInside` location queries; and
//   * the `INode` pattern-match hooks (`DoMatch`/`DoMatchCollection`) -- `AstNode : INode`
//     in the C#, so the base implements the interface by delegating to the abstract
//     `DoMatch(AstNode*, Match)` the concrete nodes supply.
//
// Deferred to later Phase-5 slices (each is a larger, separately testable unit): the
// `AbstractAnnotatable` annotation channel (and the `NodeTrivia` trivia it carries), the
// `AstNodeCollection<T>` collection slots, the mutation API (`AddChild`/`InsertChild*`/
// `Remove`/`ReplaceWith`/`Clone` with its `ValidateNewSingleChild` tree invariants), the
// `IAstVisitor`/`AcceptVisitor` dispatch, the `ToString`/`CSharpOutputVisitor` rendering,
// and the `GetNextNode`/`GetPrevNode`/`GetNextSibling`/`GetPrevSibling` predicate overloads.
// The `Trivia` branch of the sibling/slot accessors is dropped along with trivia itself (a
// trivia node has no child slot; it is restored when the trivia system lands).
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

#include <functional>
#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// Forward declaration: the collection slot type is ported later; the base only needs
// it as the (nullable) return of `GetCollectionByKind`.
class AstNodeCollection;

// Forward declarations: the child-enumeration helper types are defined after the `AstNode`
// class (their method bodies call `AstNode` members, so they need the complete class).
// `Children()` returns `ChildrenCollection` by value, so it is declared here and defined
// after the helper types.
class ChildEnumerator;
class ChildrenCollection;

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

    // ---- Child enumeration ----------------------------------------------
    // The C# `public ChildrenCollection Children => new ChildrenCollection(this)` -- the
    // children of this node in document order (see `ChildrenCollection`/`ChildEnumerator`
    // below). Returned by value; the view holds a pointer to this node, so it is cheap.
    ChildrenCollection Children();

    // ---- Ancestor / descendant walks ------------------------------------
    // The C# `public IEnumerable<AstNode> Ancestors` -- the parent chain, excluding this
    // node. Eagerly collected (C++ has no `yield return`; the C# lazy sequence ports to a
    // vector with the same document order).
    std::vector<AstNode*> Ancestors() {
        std::vector<AstNode*> result;
        for (AstNode* cur = Parent(); cur != nullptr; cur = cur->Parent())
            result.push_back(cur);
        return result;
    }

    // The C# `public IEnumerable<AstNode> AncestorsAndSelf` -- including this node.
    std::vector<AstNode*> AncestorsAndSelf() {
        std::vector<AstNode*> result;
        for (AstNode* cur = this; cur != nullptr; cur = cur->Parent())
            result.push_back(cur);
        return result;
    }

    // The C# `public IEnumerable<AstNode> Descendants` -- the pre-order descendant walk,
    // excluding this node.
    std::vector<AstNode*> Descendants() {
        return GetDescendantsImpl(false, {});
    }

    // The C# `public IEnumerable<AstNode> DescendantsAndSelf` -- including this node.
    std::vector<AstNode*> DescendantsAndSelf() {
        return GetDescendantsImpl(true, {});
    }

    // The C# `public IEnumerable<AstNode> DescendantNodes(Func<AstNode,bool>?)` -- the
    // pre-order walk, optionally skipping the children of any node for which
    // `descendIntoChildren` returns false (an empty function descends into all).
    std::vector<AstNode*> DescendantNodes(
        const std::function<bool(AstNode*)>& descendIntoChildren = {}) {
        return GetDescendantsImpl(false, descendIntoChildren);
    }

    // The C# `public IEnumerable<AstNode> DescendantNodesAndSelf(...)`.
    std::vector<AstNode*> DescendantNodesAndSelf(
        const std::function<bool(AstNode*)>& descendIntoChildren = {}) {
        return GetDescendantsImpl(true, descendIntoChildren);
    }

    // ---- Ancestor lookup -------------------------------------------------
    // The C# `public T? GetParent<T>()` -- the first ancestor of type `T`, or null. `T`
    // must be polymorphic (derive from `AstNode`, which has a virtual destructor) for the
    // `dynamic_cast` is-a test.
    template <typename T>
    T* GetParent() {
        for (AstNode* cur = Parent(); cur != nullptr; cur = cur->Parent()) {
            if (T* t = dynamic_cast<T*>(cur))
                return t;
        }
        return nullptr;
    }

    // The C# `public AstNode? GetParent(Func<AstNode,bool>?)` -- the first ancestor
    // matching `pred` (or the first ancestor when `pred` is empty), or null.
    AstNode* GetParent(const std::function<bool(AstNode*)>& pred = {}) {
        for (AstNode* cur = Parent(); cur != nullptr; cur = cur->Parent()) {
            if (!pred || pred(cur))
                return cur;
        }
        return nullptr;
    }

    // ---- Document-order navigation --------------------------------------
    // The C# `public AstNode? GetNextNode` -- the next node in document order: the next
    // sibling, else the parent's next node. (The Trivia branch is dropped with trivia.)
    AstNode* GetNextNode() {
        AstNode* s = NextSibling();
        if (s != nullptr)
            return s;
        AstNode* p = Parent();
        return p != nullptr ? p->GetNextNode() : nullptr;
    }

    // The C# `public AstNode? GetPrevNode` -- the previous node in document order.
    AstNode* GetPrevNode() {
        AstNode* s = PrevSibling();
        if (s != nullptr)
            return s;
        AstNode* p = Parent();
        return p != nullptr ? p->GetPrevNode() : nullptr;
    }

    // ---- Location queries -----------------------------------------------
    // The C# `public bool Contains(int,int)` / `Contains(TextLocation)` -- the location is
    // in the half-open [StartLocation, EndLocation) range.
    bool Contains(int line, int column) const { return Contains(TextLocation(line, column)); }
    bool Contains(TextLocation location) const {
        return StartLocation() <= location && location < EndLocation();
    }

    // The C# `public bool IsInside(int,int)` / `IsInside(TextLocation)` -- the location is
    // in the closed [StartLocation, EndLocation] range.
    bool IsInside(int line, int column) const { return IsInside(TextLocation(line, column)); }
    bool IsInside(TextLocation location) const {
        return StartLocation() <= location && location <= EndLocation();
    }

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

private:
    // The C# `IEnumerable<AstNode> GetDescendantsImpl(bool, Func<AstNode,bool>?)` -- the
    // pre-order DFS: at each node, remember its next sibling (so a mid-walk
    // removal/replacement of the current node does not lose the place), yield the node,
    // then descend into its first child (unless `descendIntoChildren` vetoes it) or resume
    // from the stacked sibling. The stack is seeded with a null sentinel so the walk ends
    // when nothing remains. Eagerly collected (C++ has no `yield`).
    std::vector<AstNode*> GetDescendantsImpl(bool includeSelf,
        const std::function<bool(AstNode*)>& descendIntoChildren) {
        std::vector<AstNode*> result;
        if (includeSelf) {
            result.push_back(this);
            if (descendIntoChildren && !descendIntoChildren(this))
                return result;
        }
        std::vector<AstNode*> nextStack;
        nextStack.push_back(nullptr);
        AstNode* pos = FirstChild();
        while (pos != nullptr) {
            AstNode* posNext = pos->NextSibling();
            if (posNext != nullptr)
                nextStack.push_back(posNext);
            result.push_back(pos);
            AstNode* posFirstChild = pos->FirstChild();
            if (posFirstChild != nullptr && (!descendIntoChildren || descendIntoChildren(pos)))
                pos = posFirstChild;
            else {
                pos = nextStack.back();
                nextStack.pop_back();
            }
        }
        return result;
    }
};

// ---- Child enumeration (helper types for `AstNode::Children`) ------------
// The C# `AstNode.ChildrenCollection` readonly struct and `ChildEnumerator` struct (nested
// in `AstNode`): a view over a node's children in document order with an enumerator that
// captures each child's successor before handing it out, so the loop body may remove or
// replace the current child mid-traversal without losing the place. Ported as free types
// (their method bodies call `AstNode` members, so they are defined after the `AstNode`
// class); the `Trivia` fast-path in the C# enumerator is dropped with trivia itself.

class ChildEnumerator {
    AstNode* node_ = nullptr;
    AstNode* current_ = nullptr;
    AstNode* next_ = nullptr;
    bool started_ = false;

public:
    ChildEnumerator() = default;
    explicit ChildEnumerator(AstNode* node) : node_(node) {}

    // The C# `bool MoveNext()`: hands out the first child on the first call, then the
    // successor captured before the previous yield. Returns false when exhausted.
    bool MoveNext() {
        current_ = started_ ? next_ : (node_ != nullptr ? node_->FirstChild() : nullptr);
        started_ = true;
        if (current_ == nullptr)
            return false;
        next_ = current_->NextSibling();
        return true;
    }

    AstNode* Current() const { return current_; }

    void Reset() {
        current_ = nullptr;
        next_ = nullptr;
        started_ = false;
    }

    // Range-for input-iterator interface: `begin()` is a `MoveNext`'d enumerator (at the
    // first child or exhausted), `end()` is the default sentinel (`current_ == nullptr`);
    // `operator!=` discriminates by `current_`, so a non-empty enumerator is not equal to
    // the sentinel until it is advanced past its last child.
    AstNode* operator*() const { return current_; }
    ChildEnumerator& operator++() { MoveNext(); return *this; }
    bool operator!=(const ChildEnumerator& rhs) const { return current_ != rhs.current_; }
    bool operator==(const ChildEnumerator& rhs) const { return current_ == rhs.current_; }
};

class ChildrenCollection {
    AstNode* node_ = nullptr;

public:
    ChildrenCollection() = default;
    explicit ChildrenCollection(AstNode* node) : node_(node) {}

    // The C# `GetEnumerator()` -- the mutation-tolerant enumerator (call `MoveNext` then
    // `Current` in a loop).
    ChildEnumerator GetEnumerator() const { return ChildEnumerator(node_); }

    // Range-for support (`for (AstNode* child : node.Children()) ...`).
    ChildEnumerator begin() const {
        ChildEnumerator e(node_);
        e.MoveNext();
        return e;
    }
    ChildEnumerator end() const { return ChildEnumerator(); }

    // The C# `int Count` -- the number of present (non-null) children.
    int Count() const {
        int count = 0;
        for (ChildEnumerator e = GetEnumerator(); e.MoveNext(); )
            count++;
        return count;
    }

    // The C# `AstNode this[int index]` -- the i-th present child in document order.
    AstNode* At(int index) const {
        int i = 0;
        for (AstNode* child : *this) {
            if (i++ == index)
                return child;
        }
        throw std::out_of_range("ChildrenCollection::At");
    }
    AstNode* operator[](int index) const { return At(index); }
};

inline ChildrenCollection AstNode::Children() {
    return ChildrenCollection(this);
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
