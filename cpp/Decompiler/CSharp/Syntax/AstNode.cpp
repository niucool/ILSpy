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

// Out-of-line definitions of the `AstNode` mutation-API members that dereference
// `AstNodeCollection*` (`AddChildUnsafe`/`InsertChildBeforeUnsafe`/`InsertChildAfterUnsafe`/
// `Remove`). `AstNode.hpp` only forward-declares `AstNodeCollection` (the full definition
// lives in `AstNodeCollection.hpp`, which includes `AstNode.hpp` -- a circular include if
// pulled in from the header), so these members are declared in the header and defined here,
// where `AstNodeCollection.hpp` is visible. The template members (`AddChild`/
// `InsertChildBefore`/`InsertChildAfter`) and the non-dereferencing members
// (`SetChildByKindUntyped`/`ReplaceWith`/`SetChildNode`/`ValidateNewSingleChild`/`Clone`)
// stay inline in the header; they call these out-of-line helpers by member-function call
// (resolved at link time, the declaration being visible in the class body).
//
// This file also defines the trivia mutation path (`AddLeadingTrivia`/
// `PrependLeadingTrivia`/`AddTrailingTrivia`/`CopyTriviaFrom`/`ReparentTrivia`/
// `LeadingTrivia`/`TrailingTrivia`/`GetOrCreateTrivia`), the `CheckInvariant`/
// `CheckTriviaInvariant` debug machinery, and the file-static `ReindexTrivia`/
// `ValidateNewTrivia`/`InsertTrivia` helpers the path consults. `AstNode.hpp` only
// forward-declares `Trivia`/`NodeTrivia` (the holder needs the complete `Trivia` type,
// and `Annotation<NodeTrivia>` needs `NodeTrivia` complete), so these are declared in the
// header and defined here, where `Trivia.hpp` is visible.

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"

#include <cassert>
#include <cstddef>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `internal void AddChildUnsafe(AstNode, CSharpSlotInfo)` -- route to the collection
// for `kind` (append), or fill the single slot via `SetChildByKindUntyped`.
void AstNode::AddChildUnsafe(AstNode* child, const CSharpSlotInfo* kind) {
    AstNodeCollection* collection = GetCollectionByKind(kind);
    if (collection != nullptr)
        collection->AddNode(child);
    else
        SetChildByKindUntyped(kind, child);
}

// The C# `internal void InsertChildBeforeUnsafe(AstNode, AstNode, CSharpSlotInfo)` -- insert
// before the next sibling into the collection for `kind`, or fill the single slot.
void AstNode::InsertChildBeforeUnsafe(AstNode* nextSibling, AstNode* child,
                                      const CSharpSlotInfo* kind) {
    AstNodeCollection* collection = GetCollectionByKind(kind);
    if (collection != nullptr)
        collection->InsertNodeBefore(nextSibling, child);
    else
        SetChildByKindUntyped(kind, child);
}

// The `InsertChildAfter` counterpart to `InsertChildBeforeUnsafe` -- insert after the
// previous sibling into the collection for `kind`, or fill the single slot. (The C# inlines
// this in `InsertChildAfter<T>`; this port routes the template through this helper so the
// template body need not dereference `AstNodeCollection*`.)
void AstNode::InsertChildAfterUnsafe(AstNode* prevSibling, AstNode* child,
                                     const CSharpSlotInfo* kind) {
    AstNodeCollection* collection = GetCollectionByKind(kind);
    if (collection != nullptr)
        collection->InsertNodeAfter(prevSibling, child);
    else
        SetChildByKindUntyped(kind, child);
}

// The C# `public void Remove()` -- remove this node from its parent. A no-op when
// unparented. (The Trivia branch is dropped with trivia itself.) The parent's slot kind
// locates the collection holding this node (a collection slot -> `RemoveNode`; a single
// slot -> clear it via `SetChild(index, null)`).
void AstNode::Remove() {
    if (parent_ == nullptr)
        return;
    parent_->EnsureChildIndices();
    const CSharpSlotInfo* kind = parent_->GetChildSlotInfo(ChildIndex)->Kind();
    AstNodeCollection* collection = parent_->GetCollectionByKind(kind);
    if (collection != nullptr)
        collection->RemoveNode(this);
    else
        parent_->SetChild(ChildIndex, nullptr);
}

// ---- File-static trivia helpers -------------------------------------------
// The C# `static void ReindexTrivia(AstNode parent, List<Trivia> list, int start)` --
// reassign each trivia in `list` from `start` onward its parent/list/index state after an
// insert or remove. The list is the `NodeTrivia` holder's `Leading`/`Trailing` vector; the
// holder owns the trivia, so this only re-points their back-pointers.
static void ReindexTrivia(AstNode* parent, std::vector<std::unique_ptr<Trivia>>* list,
                          int start) {
    for (int i = start; i < static_cast<int>(list->size()); i++)
        (*list)[static_cast<std::size_t>(i)]->SetTriviaParent(parent, list, i);
}

// The C# `void ValidateNewTrivia(Trivia trivia)` -- the null, self-reference, and
// already-parented guards shared by the trivia attachers.
static void ValidateNewTrivia(Trivia* trivia, AstNode* self) {
    if (trivia == nullptr)
        throw std::invalid_argument("trivia is null");
    if (trivia == self)
        throw std::invalid_argument("Cannot add a node to itself as trivia.");
    if (trivia->Parent() != nullptr)
        throw std::invalid_argument("Node is already used in another tree.");
}

// The C# `void InsertTrivia(List<Trivia> list, int index, Trivia trivia)` -- the single
// mutation path for attaching trivia: validate, insert (taking ownership), then reindex from
// the insertion point so every trivia's parent/list/index state is consistent.
static void InsertTrivia(AstNode* self, std::vector<std::unique_ptr<Trivia>>& list,
                        int index, Trivia* trivia) {
    ValidateNewTrivia(trivia, self);
    list.insert(list.begin() + index, std::unique_ptr<Trivia>(trivia));
    ReindexTrivia(self, &list, index);
}

// ---- Trivia mutation path (AstNode members) -------------------------------

// The C# `private NodeTrivia GetOrCreateTrivia()` -- the holder for this node's trivia,
// creating it (and adding it as an annotation) on first use. The raw pointer is valid for the
// node's lifetime (the node holds the owning `shared_ptr` in its annotation channel).
NodeTrivia* AstNode::GetOrCreateTrivia() {
    NodeTrivia* holder = Annotation<NodeTrivia>();
    if (holder == nullptr) {
        auto owned = std::make_shared<NodeTrivia>();
        holder = owned.get();
        AddAnnotation(std::move(owned));
    }
    return holder;
}

// The C# `public IEnumerable<Trivia> LeadingTrivia`/`TrailingTrivia` -- the trivia in
// insertion order (empty when none). Returned by value as a non-owning pointer view (the
// holder retains ownership).
std::vector<Trivia*> AstNode::LeadingTrivia() const {
    std::vector<Trivia*> result;
    if (NodeTrivia* holder = Annotation<NodeTrivia>()) {
        result.reserve(holder->Leading.size());
        for (const auto& t : holder->Leading)
            result.push_back(t.get());
    }
    return result;
}

std::vector<Trivia*> AstNode::TrailingTrivia() const {
    std::vector<Trivia*> result;
    if (NodeTrivia* holder = Annotation<NodeTrivia>()) {
        result.reserve(holder->Trailing.size());
        for (const auto& t : holder->Trailing)
            result.push_back(t.get());
    }
    return result;
}

// The C# `public void AddLeadingTrivia(Trivia)` -- append to the leading list (lazily
// created on first use).
void AstNode::AddLeadingTrivia(Trivia* trivia) {
    NodeTrivia* holder = GetOrCreateTrivia();
    InsertTrivia(this, holder->Leading, static_cast<int>(holder->Leading.size()), trivia);
}

// The C# `public void PrependLeadingTrivia(Trivia)` -- insert at the start of the leading
// list (used for a file header comment that must precede generated leading directives).
void AstNode::PrependLeadingTrivia(Trivia* trivia) {
    NodeTrivia* holder = GetOrCreateTrivia();
    InsertTrivia(this, holder->Leading, 0, trivia);
}

// The C# `public void AddTrailingTrivia(Trivia)` -- append to the trailing list.
void AstNode::AddTrailingTrivia(Trivia* trivia) {
    NodeTrivia* holder = GetOrCreateTrivia();
    InsertTrivia(this, holder->Trailing, static_cast<int>(holder->Trailing.size()), trivia);
}

// The C# `internal void CopyTriviaFrom(AstNode)` -- deep-copy another node's trivia onto
// this node, appending to any trivia already present (the `NodeTrivia` holder must never be
// shared between nodes: each trivia's Parent points at its single owning node, so a copy
// clones the trivia instead).
void AstNode::CopyTriviaFrom(const AstNode& other) {
    NodeTrivia* otherHolder = other.Annotation<NodeTrivia>();
    if (otherHolder == nullptr)
        return;
    for (const auto& t : otherHolder->Leading)
        AddLeadingTrivia(static_cast<Trivia*>(t->Clone()));
    for (const auto& t : otherHolder->Trailing)
        AddTrailingTrivia(static_cast<Trivia*>(t->Clone()));
}

// The C# `void ReparentTrivia()` -- re-point the trivia held in this node's `NodeTrivia`
// annotation at this node. The concrete node's `Clone` calls this after `CloneAnnotationsFrom`
// (the cloned holder's deep-copied trivia still carry the source owner's parent state), so
// the cloned trivia point at the clone.
void AstNode::ReparentTrivia() {
    NodeTrivia* holder = Annotation<NodeTrivia>();
    if (holder == nullptr)
        return;
    if (!holder->Leading.empty())
        ReindexTrivia(this, &holder->Leading, 0);
    if (!holder->Trailing.empty())
        ReindexTrivia(this, &holder->Trailing, 0);
}

// ---- CheckInvariant / CheckTriviaInvariant (debug machinery) ----------------
// The C# `[Conditional("DEBUG")] void CheckTriviaInvariant(List<Trivia>? list)` -- verify
// each trivia in `list` is parented to `owner` and carries the list/index that holds it. A
// no-op in NDEBUG (mirrors the IL AST `CheckInvariant`).
static void CheckTriviaInvariant(const AstNode* owner,
                                 const std::vector<std::unique_ptr<Trivia>>& list) {
#ifndef NDEBUG
    (void)owner;
    for (int i = 0; i < static_cast<int>(list.size()); i++) {
        Trivia* t = list[static_cast<std::size_t>(i)].get();
        assert(t->Parent() == owner && "trivia's Parent must point back to its owning node");
        assert(t->TriviaSiblings() == &list && "trivia's sibling list must be the list that holds it");
        assert(t->ChildIndex == i && "trivia's index must match its position in the trivia list");
    }
#endif
}

// The C# `[Conditional("DEBUG")] internal virtual void CheckInvariant()` -- recursively
// verify the slot structure of this subtree (every required single slot filled, each child's
// Parent/ChildIndex/type consistent) and the trivia invariants. A no-op in NDEBUG (mirrors the
// IL AST `CheckInvariant`). Concrete nodes override this (calling `base`) to assert their own
// scalar invariants.
void AstNode::CheckInvariant() {
#ifndef NDEBUG
    EnsureChildIndices();
    int count = GetChildCount();
    for (int i = 0; i < count; i++) {
        const CSharpSlotInfo* slot = GetChildSlotInfo(i);
        AstNode* child = GetChild(i);
        if (child == nullptr) {
            // A single slot reads null only when empty; valid only if the slot is optional.
            // (Collection slots never yield a null index, so this branch is always a single slot.)
            assert(slot->IsOptional() && "required slot must not be empty");
            continue;
        }
        assert(child->Parent() == this && "child's Parent must point back to this node");
        assert(child->ChildIndex == i && "child's flattened index must match its slot position");
        assert(slot->IsInstanceOfType(child) && "child's type must be valid in its slot");
        child->CheckInvariant();
    }
    if (NodeTrivia* trivia = Annotation<NodeTrivia>()) {
        CheckTriviaInvariant(this, trivia->Leading);
        CheckTriviaInvariant(this, trivia->Trailing);
    }
#endif
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
