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

// Port of the `Trivia` abstract base (ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/
// Trivia.cs) and the `AstNode.NodeTrivia` holder (the nested sealed class in `AstNode.cs`)
// -- the trivia half of the annotation channel (the `AbstractAnnotatable` half is in
// `AbstractAnnotatable.hpp`). Trivia -- comments and preprocessor directives -- is content
// the output visitor emits verbatim but that is not a semantic child of a node; it is
// attached to the node it annotates as leading or trailing trivia (`AddLeadingTrivia`/
// `AddTrailingTrivia` on `AstNode`) rather than occupying a child slot, so it stays off the
// child-index space. The trivia mutation path itself lives on `AstNode` (declared in
// `AstNode.hpp`, defined out-of-line in `AstNode.cpp`); this header supplies the node type
// the path stores and the holder the path consults.
//
// `Trivia : AstNode` -- the abstract base of `Comment`/`PreProcessorDirective` (the concrete
// trivia land with the generated node hierarchy, port-the-output per D1). A trivia carries
// its own source-location pair (overriding the base's, which the output visitor records via
// `SetStartLocation`/`SetEndLocation` rather than `StorePrintStart`/`StorePrintEnd`) and a
// back-pointer to the `Leading`/`Trailing` list of its owning node (`triviaSiblings_`, null
// while detached). The sibling/slot/remove navigation branches the C# base does via
// `this is Trivia { triviaSiblings: ... }` are deferred (kept dropped, as D223): trivia is
// reached through the `LeadingTrivia`/`TrailingTrivia` lists, not the sibling/slot space,
// and the engine never calls `Remove()` on trivia (verified by grep), so the navigation
// branches are not needed by the add/copy/reparent path or the debug invariant.
//
// `NodeTrivia` -- the holder stored as an `Annotation` on the owning node (the C# nested
// `sealed class NodeTrivia : ICloneable`), carrying the `Leading`/`Trailing` trivia lists.
// The holder lives in the annotation channel so a node without trivia (the overwhelmingly
// common case) costs nothing extra, and cloning the annotation channel deep-copies it for
// free. The C# `List<Trivia>?` (null until the first trivia) ports to an always-present
// `std::vector<std::unique_ptr<Trivia>>` (empty until the first add -- the D222 precedent);
// the holder owns its trivia (the C# GC owns them; the port transfers ownership on
// `AddLeadingTrivia`/`AddTrailingTrivia` and `CopyTriviaFrom`, which clone the source
// trivia onto the target). `Clone()` deep-copies the trivia lists (each trivia is
// `Clone()`d, the C# `NodeTrivia.Clone` shape), returning a `shared_ptr<AnnotationBase>` so
// `CloneAnnotationsFrom` can store the copy on the cloned node.

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The abstract base of trivia nodes (comments and preprocessor directives). Derives from
// `AstNode` (trivia is a node, addressable by the output visitor), but occupies no child
// slot -- it is attached to its owning node via the `NodeTrivia` annotation, not the slot
// storage contract, so the zero-child slot-storage defaults apply. Concrete trivia
// (`Comment`/`PreProcessorDirective`) override `Clone()`/`DoMatch`/`AcceptVisitor`; this
// base keeps `DoMatch`/`Clone` abstract (inherited from `AstNode`).
class Trivia : public AstNode {
    // The `Leading`/`Trailing` list of the owning node that currently holds this trivia
    // (null while detached). Raw, non-owning: the `NodeTrivia` holder owns the vector; this
    // pointer is valid for the trivia's lifetime (the trivia is an element of that vector,
    // destroyed with it). The C# `internal List<Trivia>? triviaSiblings` (mirrored here);
    // public-read via `TriviaSiblings()` (the internal-to-public convention) so the
    // `CheckTriviaInvariant` debug check can verify it.
    std::vector<std::unique_ptr<Trivia>>* triviaSiblings_ = nullptr;

    TextLocation startLocation_ = TextLocation::Empty;
    TextLocation endLocation_ = TextLocation::Empty;

public:
    Trivia() = default;

    // The C# `protected Trivia(TextLocation, TextLocation)` -- a concrete trivia (e.g.
    // `Comment(CommentType, start, end)`) records its source span at construction.
    Trivia(TextLocation startLocation, TextLocation endLocation)
        : startLocation_(startLocation), endLocation_(endLocation) {}

    // The C# `override StartLocation`/`EndLocation` -- a trivia carries its own span
    // (the output visitor records it via `SetStartLocation`/`SetEndLocation`), separate
    // from the base's print-time locations.
    TextLocation StartLocation() const override { return startLocation_; }
    TextLocation EndLocation() const override { return endLocation_; }

    // The C# `internal void SetStartLocation`/`SetEndLocation` -- the output visitor
    // records the trivia's span. Public (the internal-to-public convention).
    void SetStartLocation(TextLocation value) { startLocation_ = value; }
    void SetEndLocation(TextLocation value) { endLocation_ = value; }

    // The C# `internal void SetTriviaParent(AstNode, List<Trivia>, int)` -- point this
    // trivia at its owning node and the list/index holding it (the `ReindexTrivia`
    // helper calls this after an insert/remove to keep every trivia's parent/list/index
    // state consistent). Public (the internal-to-public convention) so `AstNode`'s
    // trivia mutation path can call it through `ReindexTrivia`.
    void SetTriviaParent(AstNode* newParent, std::vector<std::unique_ptr<Trivia>>* siblings,
                         int index) {
        SetParent(newParent);
        triviaSiblings_ = siblings;
        ChildIndex = index;
    }

    // The `internal List<Trivia>? triviaSiblings` (read) -- the list holding this trivia,
    // or null when detached. Used by the `CheckTriviaInvariant` debug check to verify a
    // trivia's sibling list is the list that holds it.
    const std::vector<std::unique_ptr<Trivia>>* TriviaSiblings() const { return triviaSiblings_; }
};

// The holder for a node's leading/trailing trivia, stored as an `Annotation` on the node
// (the C# `AstNode.NodeTrivia`). A node without trivia (the common case) has no holder
// (the annotation is absent); the first `AddLeadingTrivia`/`AddTrailingTrivia` creates it
// via `AstNode::GetOrCreateTrivia`. `Clone()` deep-copies the trivia lists so cloning the
// annotation channel (via `CloneAnnotationsFrom`) gives the clone its own trivia.
class NodeTrivia : public AnnotationBase {
public:
    // The leading/trailing trivia in insertion order (empty until the first add). Public
    // so `AstNode`'s trivia mutation path (`AddLeadingTrivia`/`InsertTrivia`/`ReparentTrivia`
    // /`CopyTriviaFrom`/`CheckTriviaInvariant`) can read and modify them (the C# holder is a
    // private nested class whose fields are in-scope to `AstNode`'s methods; the port's
    // free-class holder exposes them, the internal-to-public convention).
    std::vector<std::unique_ptr<Trivia>> Leading;
    std::vector<std::unique_ptr<Trivia>> Trailing;

    // The C# `public object Clone()` -- deep-copy the trivia lists (each trivia is
    // `Clone()`d). The cloned trivia are detached (`Clone()` resets parent/index/siblings),
    // and the owning node's `ReparentTrivia` reindexes them onto the clone. Returns a
    // `shared_ptr<AnnotationBase>` so `CloneAnnotationsFrom` stores the copy on the cloned node.
    std::shared_ptr<AnnotationBase> Clone() const override {
        auto copy = std::make_shared<NodeTrivia>();
        copy->Leading.reserve(Leading.size());
        for (const auto& t : Leading)
            copy->Leading.emplace_back(static_cast<Trivia*>(t->Clone()));
        copy->Trailing.reserve(Trailing.size());
        for (const auto& t : Trailing)
            copy->Trailing.emplace_back(static_cast<Trivia*>(t->Clone()));
        return copy;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax
