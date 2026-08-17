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

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"

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

} // namespace ILSpy::Decompiler::CSharp::Syntax
