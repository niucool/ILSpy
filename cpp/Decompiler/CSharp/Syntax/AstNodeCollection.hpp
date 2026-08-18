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

// Port of the `AstNodeCollection` / `AstNodeCollection<T>` collection-slot system in
// ICSharpCode.Decompiler/CSharp/Syntax/AstNodeCollection.cs -- the fifth in-order Phase-5
// piece (the C# AST + resolver + back end), per the D221 plan's "next in-order Phase-5
// pieces" (the mutation API and the collection slots it consults). This header ports the
// collection slots; the mutation API on `AstNode` (`AddChild`/`InsertChild*`/`Remove`/
// `ReplaceWith`/`Clone`) lands in a follow-up slice on top of it.
//
// A collection slot (e.g. `BlockStatement.Statements`) holds the children that occupy one
// `AstNodeCollection<T>` of a node. The elements live in a list owned by the collection;
// the parent's flattened child-index space contains them as a contiguous run, renumbered
// lazily by the parent after a mutation. The non-generic `AstNodeCollection` base lets the
// `AstNode` base manipulate a collection slot (add/insert/remove by reference) without
// knowing its element type; the generic `AstNodeCollectionT<T>` carries the element type
// for the typed accessors (`node.GetChildren(SomeNode.XSlot)`).
//
// The C# generic `AstNodeCollection<T>` cannot reuse the base name in C++ (a class
// template may not share a name with a non-template class), so the generic is
// `AstNodeCollectionT<T>` (the `T` suffix evokes the `<T>` type parameter), matching the
// `CSharpSlotInfo` / `CSharpSlotInfoT<T>` precedent. `T` must derive from `AstNode`
// (the C# `where T : AstNode`); the `dynamic_cast<T*>` the overrides use requires `T` to
// be polymorphic, which it is through `AstNode`'s virtual destructor.
//
// The C# lazily allocates the `List<T>` ("on first Add") and the `AstNodeCollection<T>`
// wrapper (the generated `field ??= new AstNodeCollection<T>(...)`). In C++ the collection
// is a member of the node (no heap allocation for the wrapper) and the `std::vector<T*>`
// is empty until the first `Add` -- the same lazy-element-list profile (the wrapper is a
// small stack object, so eagerly holding it costs nothing the C# laziness was avoiding).
//
// This slice ports the core collection API the mutation API consults (`Add`/`Insert`/
// `InsertBefore`/`InsertAfter`/`Remove`/`IndexOf`/`Contains`/`Clear`/`Count`/the indexer
// get+set/`ValidateNewChild`/`ReindexFrom`) and the four `AstNodeCollection` mutation
// virtuals (`AddNode`/`InsertNodeBefore`/`InsertNodeAfter`/`RemoveNode`). Deferred to
// later Phase-5 slices: the mutation-tolerant `Enumerator`/`GetEnumerator` (the
// per-collection `foreach` the transforms use; the `Children`/`ChildEnumerator` already
// covers the cross-collection walk), the convenience mutators `AddRange`/`ReplaceWith`/
// `MoveTo`/`Detach`/`FirstOrNull`/`LastOrNull` (land as the transforms that use them
// arrive), `Equals`/`GetHashCode` (identity), and the `IAstVisitor` `AcceptVisitor` (lands
// with the visitor). The pattern-matcher surface (`NodeCount`/`NodeAt`/`AsNodeList`/`DoMatch`
// -- the generated nodes' `DoMatch` calls `Pattern.DoMatchCollection` over the
// collection's node-list view) lands with the first generated node that has a collection
// slot (`SimpleType`'s `TypeArguments`).
//
// Field access: `parent_`, `kind_`, `baseIndex_`, `supportsIncremental_`, and `list_` are
// private; the public API is the typed surface (`Add`/`Insert*`/`Remove`/`IndexOf`/
// `Contains`/`Clear`/`Count`/`At`/`SetAt`/`operator[]`). The `AstNodeCollection` base's
// mutation virtuals are public (the port's internal-to-public convention; the `Match`
// precedent), since the `AstNode` base calls them through `GetCollectionByKind`.

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The non-generic base of `AstNodeCollectionT<T>`, letting the `AstNode` base manipulate
// a collection slot (add/insert/remove an element by reference) without knowing its
// element type. The `AstNode` mutation API consults these through `GetCollectionByKind`.
// Mirrors the C# `public abstract class AstNodeCollection` (the pattern-matcher surface
// `NodeCount`/`NodeAt`/`AsNodeList` is deferred with the generated nodes' `DoMatch`).
class AstNodeCollection {
public:
    virtual ~AstNodeCollection() = default;

    // The C# `internal abstract void AddNode(AstNode)` -- append to the collection.
    virtual void AddNode(AstNode* node) = 0;

    // The C# `internal abstract void InsertNodeBefore(AstNode?, AstNode)` -- insert
    // before the given sibling (appended when the sibling is not in this collection).
    virtual void InsertNodeBefore(AstNode* existing, AstNode* node) = 0;

    // The C# `internal abstract void InsertNodeAfter(AstNode?, AstNode)` -- insert after
    // the given sibling (inserted at the front when the sibling is not in this collection).
    virtual void InsertNodeAfter(AstNode* existing, AstNode* node) = 0;

    // The C# `internal abstract bool RemoveNode(AstNode)` -- remove the element by
    // reference; false when it is not in this collection.
    virtual bool RemoveNode(AstNode* node) = 0;

    // ---- Pattern matcher surface ------------------------------------------
    // The C# `private protected abstract int NodeCount` / `INode NodeAt(int)` -- the
    // pattern matcher consumes a collection as an `IReadOnlyList<INode>` (`AsNodeList`),
    // a view separate from the typed collection (having `AstNodeCollection<T>` implement
    // `IEnumerable<INode>` as well as `IEnumerable<T>` would make every LINQ call on a
    // typed collection ambiguous). The generic overrides expose the element list through
    // the `INode` interface; `AsNodeList` builds that view on demand.
    virtual int NodeCount() const = 0;
    virtual PatternMatching::INode* NodeAt(int index) const = 0;

    // The C# `internal IReadOnlyList<INode> AsNodeList()` -- the node-list view the
    // pattern matcher's `DoMatch` passes to `Pattern.DoMatchCollection`. The C# caches a
    // `NodeListView` (a live view reading through to the collection); this port builds a
    // fresh `std::vector<INode*>` snapshot each call (C++ has no live `IReadOnlyList` view
    // without copying, and `DoMatch` is called once per match with no interleaved mutation,
    // so a snapshot is faithful -- the elements are the same `INode*` pointers the C#
    // view would yield). The O(n) build matches the C# (the `NodeListView` iterates
    // `NodeAt` n times in `DoMatchCollection`).
    std::vector<PatternMatching::INode*> AsNodeList() const {
        std::vector<PatternMatching::INode*> nodes;
        int n = NodeCount();
        nodes.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; i++)
            nodes.push_back(NodeAt(i));
        return nodes;
    }
};

// The collection occupying one collection slot of a node. `T` is the element type (the
// C# `where T : AstNode`); it must derive from `AstNode`. The elements live in `list_`
// (empty until the first `Add`); the parent's flattened child-index space contains them
// as the contiguous run `[baseIndex, baseIndex + Count)`. When `supportsIncremental` is
// true and this is the parent's only collection at its last slot, an element's flattened
// `ChildIndex` is exactly `baseIndex + its local position`, so mutations maintain
// `ChildIndex` incrementally (no full re-index) and `IndexOf` runs in O(1); other shapes
// fall back to invalidate-and-rebuild. Mirrors the C# `public class AstNodeCollection<T>`.
template <class T>
class AstNodeCollectionT : public AstNodeCollection {
    AstNode* parent_;
    const CSharpSlotInfo* kind_;
    std::vector<T*> list_;
    int baseIndex_;
    bool supportsIncremental_;

public:
    // The detached-empty default ctor -- an empty collection NOT attached to any node.
    // Used only by `AstNode::GetChildren<T>` for the no-collection-of-this-kind case (a
    // read-only view: the C# `new AstNodeCollection<T>(this, slot)` for a kind the node
    // declares no collection slot for). The collection is empty so read paths (`Count`/
    // iteration/`AsNodeList`/`DoMatch`) are safe and never touch the null parent; MUTATION
    // (`Add`/`Insert`/`Remove`/`SetAt`) would dereference the null parent and is UB -- but no
    // caller mutates a `GetChildren`-returned detached empty (the D271 note: writes go through
    // `AddChild`/`SetChild`, which reject a missing slot). NOT for general construction (use the
    // `(parent, kind)` / `(parent, kind, baseIndex, incremental)` ctors for a real collection).
    AstNodeCollectionT() : parent_(nullptr), kind_(nullptr), baseIndex_(0),
                           supportsIncremental_(false) {}

    // The C# `public AstNodeCollection(AstNode, CSharpSlotInfo)` -- delegates to the
    // four-arg ctor with `baseIndex = 0`, `supportsIncremental = false`.
    AstNodeCollectionT(AstNode* parent, const CSharpSlotInfo* kind)
        : AstNodeCollectionT(parent, kind, 0, false) {}

    // The C# `public AstNodeCollection(AstNode, CSharpSlotInfo, int, bool)`. A null parent
    // is rejected (the C# `ArgumentNullException`); the kind is stored (the C# does not
    // null-check it; the slot is always provided).
    AstNodeCollectionT(AstNode* parent, const CSharpSlotInfo* kind, int baseIndex,
                       bool supportsIncremental)
        : parent_(parent), kind_(kind), baseIndex_(baseIndex),
          supportsIncremental_(supportsIncremental) {
        if (parent == nullptr)
            throw std::invalid_argument("AstNodeCollection: parent is null");
    }

    // The C# `int Count` -- the number of elements (0 until the first `Add`).
    int Count() const { return static_cast<int>(list_.size()); }

    // The C# `T this[int index]` getter -- the element at the local position. Throws
    // `out_of_range` when the index is not in `[0, Count)` (the C# `list![index]` throws
    // `ArgumentOutOfRangeException`).
    T* At(int index) const {
        if (index < 0 || index >= static_cast<int>(list_.size()))
            throw std::out_of_range("AstNodeCollection::At");
        return list_[static_cast<std::size_t>(index)];
    }
    T* operator[](int index) const { return At(index); }

    // The C# `T this[int index]` setter -- replace the element in place, carrying the old
    // element's flattened index to the new one (a stale carried value is corrected by the
    // next renumber). Detaches the old element and validates+parents the new one.
    void SetAt(int index, T* value) {
        if (index < 0 || index >= static_cast<int>(list_.size()))
            throw std::out_of_range("AstNodeCollection::SetAt");
        T* old = list_[static_cast<std::size_t>(index)];
        if (old == value)
            return;
        ValidateNewChild(value);
        int oldChildIndex = old->ChildIndex;
        old->ClearParentAndIndex();
        list_[static_cast<std::size_t>(index)] = value;
        value->SetParent(parent_);
        value->ChildIndex = oldChildIndex;
    }

    // The C# `void Add(T)` -- append, parent the element, and index it. A null element is
    // a no-op (the C# `if (element == null) return;`). On the incremental fast-path the
    // new element's `ChildIndex` is assigned directly (appending leaves every existing
    // index unchanged); otherwise the parent's indices are invalidated for a lazy rebuild.
    void Add(T* element) {
        if (element == nullptr)
            return;
        ValidateNewChild(element);
        list_.push_back(element);
        element->SetParent(parent_);
        if (supportsIncremental_ && parent_->ChildIndicesValid())
            element->ChildIndex = baseIndex_ + static_cast<int>(list_.size()) - 1;
        else
            parent_->InvalidateChildIndices();
    }

    // The C# `void InsertBefore(T?, T)` -- insert before the given sibling, or append
    // when the sibling is absent (a null `existingItem` yields `IndexOf == -1`, so the new
    // item is appended).
    void InsertBefore(T* existingItem, T* newItem) {
        int index = IndexOf(existingItem);
        Insert(index < 0 ? Count() : index, newItem);
    }

    // The C# `void InsertAfter(T?, T)` -- insert after the given sibling, or at the front
    // when the sibling is absent (a null `existingItem` yields `IndexOf == -1`, so the
    // new item is inserted at position 0).
    void InsertAfter(T* existingItem, T* newItem) {
        Insert(IndexOf(existingItem) + 1, newItem);
    }

    // The C# `int IndexOf(T)` -- the local position of the element, or -1. O(1) on the
    // incremental fast-path (the element's local position is its `ChildIndex` minus the
    // base) when the indices are current; otherwise a linear identity search.
    int IndexOf(T* element) const {
        if (element == nullptr || element->Parent() != parent_)
            return -1;
        if (supportsIncremental_ && parent_->ChildIndicesValid() && !list_.empty()) {
            int local = element->ChildIndex - baseIndex_;
            if (local >= 0 && local < static_cast<int>(list_.size()) &&
                list_[static_cast<std::size_t>(local)] == element)
                return local;
        }
        auto it = std::find(list_.begin(), list_.end(), element);
        return it == list_.end() ? -1 : static_cast<int>(it - list_.begin());
    }

    // The C# `bool Contains(T)` -- the element is in this collection (parented to the
    // owner and present in the list).
    bool Contains(T* element) const {
        return element != nullptr && element->Parent() == parent_ &&
               std::find(list_.begin(), list_.end(), element) != list_.end();
    }

    // The C# `bool Remove(T)` -- remove the element by reference; detach it and reindex
    // the elements after it (incremental) or invalidate the parent's indices. False when
    // the element is not in this collection.
    bool Remove(T* element) {
        int index = IndexOf(element);
        if (index < 0)
            return false;
        bool incremental = supportsIncremental_ && parent_->ChildIndicesValid();
        list_.erase(list_.begin() + index);
        element->ClearParentAndIndex();
        if (incremental)
            ReindexFrom(index);
        else
            parent_->InvalidateChildIndices();
        return true;
    }

    // The C# `void Clear()` -- detach every element and invalidate the parent's indices.
    // A no-op when the collection is empty (the C# `if (list == null) return;`; in C++ the
    // vector is always present, so the empty guard is `list_.empty()`).
    void Clear() {
        if (list_.empty())
            return;
        for (T* item : list_)
            item->ClearParentAndIndex();
        list_.clear();
        parent_->InvalidateChildIndices();
    }

    // ---- AstNodeCollection overrides ---------------------------------------
    // The four mutation virtuals the `AstNode` base calls through `GetCollectionByKind`.
    // The downcasts are unchecked (`static_cast`) -- the mutation API routes by slot kind,
    // so the element type is guaranteed; the `existing`/`node` `is T` tests use
    // `dynamic_cast` (the C# `is T`), returning null when the type does not match.

    void AddNode(AstNode* node) override { Add(static_cast<T*>(node)); }

    void InsertNodeBefore(AstNode* existing, AstNode* node) override {
        T* e = dynamic_cast<T*>(existing);
        if (e != nullptr && IndexOf(e) >= 0)
            InsertBefore(e, static_cast<T*>(node));
        else
            Add(static_cast<T*>(node));
    }

    void InsertNodeAfter(AstNode* existing, AstNode* node) override {
        T* e = dynamic_cast<T*>(existing);
        if (e != nullptr && IndexOf(e) >= 0)
            InsertAfter(e, static_cast<T*>(node));
        else
            Insert(0, static_cast<T*>(node));
    }

    bool RemoveNode(AstNode* node) override {
        T* typed = dynamic_cast<T*>(node);
        return typed != nullptr && Remove(typed);
    }

    // The C# `private protected override int NodeCount` -- the element count (the list
    // size; 0 until the first `Add`).
    int NodeCount() const override { return Count(); }

    // The C# `private protected override INode NodeAt(int)` -- the element at the local
    // position as an `INode*` (the upcast `T*` -> `INode*`; `T` derives from `AstNode` :
    // `INode`). Throws `out_of_range` past the end (the C# `list![index]`
    // `ArgumentOutOfRangeException`).
    PatternMatching::INode* NodeAt(int index) const override { return At(index); }

    // The C# `internal bool DoMatch(AstNodeCollection<T> other, Match match)` -- match this
    // collection against `other` element-by-element with backtracking over the
    // non-deterministic pattern nodes (`Repeat`/`OptionalNode`). Both collections are
    // already the per-slot child lists, so `Pattern.DoMatchCollection` walks them by index.
    bool DoMatch(const AstNodeCollectionT<T>& other, PatternMatching::Match match) const {
        return PatternMatching::Pattern::DoMatchCollection(AsNodeList(), other.AsNodeList(), match);
    }

    // Accessors the generated node's `GetChildCount`/`GetChild`/`SetChild` consult when
    // this collection is the parent's only slot (the common case). Exposed so a concrete
    // node can delegate its slot-storage contract into the collection it owns.
    int BaseIndex() const { return baseIndex_; }
    bool SupportsIncremental() const { return supportsIncremental_; }
    const CSharpSlotInfo* Kind() const { return kind_; }

private:
    // The C# `void ValidateNewChild(T)` -- the shared self-reference and two-tree guards.
    // A null child is rejected (the C# `ArgumentNullException`); a child that is the
    // parent itself is rejected; a child already parented elsewhere is rejected.
    void ValidateNewChild(T* child) {
        if (child == nullptr)
            throw std::invalid_argument("AstNodeCollection: child is null");
        if (child == parent_)
            throw std::invalid_argument("Cannot add a node to itself as a child.");
        if (child->Parent() != nullptr)
            throw std::invalid_argument("Node is already used in another tree.");
    }

    // The C# `void Insert(int, T)` -- the worker for `InsertBefore`/`InsertAfter`. Inserts
    // at the local position, parents the new element, and reindexes from the insertion
    // point (incremental) or invalidates the parent's indices. A null item is a no-op.
    void Insert(int index, T* newItem) {
        if (newItem == nullptr)
            return;
        if (index < 0 || index > static_cast<int>(list_.size()))
            throw std::out_of_range("AstNodeCollection::Insert");
        ValidateNewChild(newItem);
        bool incremental = supportsIncremental_ && parent_->ChildIndicesValid();
        list_.insert(list_.begin() + index, newItem);
        newItem->SetParent(parent_);
        if (incremental)
            ReindexFrom(index);
        else
            parent_->InvalidateChildIndices();
    }

    // The C# `void ReindexFrom(int)` -- reassign the flattened `ChildIndex` of every
    // element from `start` to the end after a shift, keeping the parent's indices valid
    // without a full rebuild. Only meaningful on the incremental fast-path.
    void ReindexFrom(int start) {
        for (int i = start; i < static_cast<int>(list_.size()); i++)
            list_[static_cast<std::size_t>(i)]->ChildIndex = baseIndex_ + i;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax
