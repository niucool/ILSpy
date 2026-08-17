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

// Port of the `IAnnotatable` interface + `AbstractAnnotatable` base in
// ICSharpCode.Decompiler/CSharp/Syntax/IAnnotatable.cs -- the annotation channel the
// `AstNode` base carries alongside the child-slot mutation. An annotation is a
// type-erased object attached to a node (the `NodeTrivia` trivia holder, the resolver's
// `ResolveResult`s, `ILInstruction` back-references, ...); the C# `IAnnotatable` API
// stores `object`s and queries them by `System.Type` (`as T`/`is T` is-a).
//
// C++ has no single root type and no `System.Type`, so annotations derive from the
// polymorphic `AnnotationBase` marker base (a virtual destructor makes `dynamic_cast<T*>`
// the is-a lookup the C# `as T`/`is T` provide). The `Annotation<T>()`/`RemoveAnnotations<T>`
// templates downcast through `AnnotationBase*`, so an annotation type must derive from
// `AnnotationBase` (a one-line addition when a type becomes an annotation; `NodeTrivia` does
// so here, and the resolver's `ResolveResult`/`ILInstruction` will when they land). The
// `Type`-based overloads (`Annotation(Type)`, `RemoveAnnotations(Type)`) are dropped: they
// would need a `type_info`-to-`dynamic_cast` bridge that C++ does not provide, and the engine
// uses only the generic `Annotation<T>()`/`RemoveAnnotations<T>()` (verified by grep --
// every `Annotation<>`/`RemoveAnnotations<>` call uses a static type). The C# thread-safety
// (`Interlocked`/`lock` on the lazily-promoted single-vs-list) is dropped: the decompiler is
// single-threaded, and a plain lazily-allocated vector matches the C# "no storage when no
// annotations" profile (the overwhelmingly common case for an AST node -- the annotation
// pointer is null until the first `AddAnnotation`).
//
// The C# `CloneAnnotations` (called by the `MemberwiseClone`-based `AstNode.Clone`) clones
// the clonable annotations and shares the rest. The port has no `MemberwiseClone` (each
// concrete node's `Clone` copies its fields and deep-clones its children itself), so the
// concrete node's `Clone` calls `CloneAnnotationsFrom(source)` to copy the source's
// annotations onto the fresh clone (clonable -> deep copy, rest -> shared), then
// `ReparentTrivia()` to point the cloned trivia at the clone. `CloneAnnotationsFrom`
// replaces the C# `MemberwiseClone`+`CloneAnnotations` combo for the annotation channel.
//
// Naming: the marker base is `AnnotationBase`, not `Annotation`, because the C# `IAnnotatable`
// query method is `Annotation<T>()` -- a member named `Annotation` would shadow the
// `AnnotationBase` type inside `AbstractAnnotatable`'s member functions (unqualified name
// lookup finds the member, not the namespace type), so `std::shared_ptr<Annotation>` would
// resolve to the function template, not the type. The `T`-suffix-less base name (`Annotation`)
// is reserved for the query method, matching the C# `IAnnotatable.Annotation<T>()` API.

#pragma once

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The universal base for objects stored as annotations (the C# `object` the `IAnnotatable`
// API stores). C++ has no single root, so annotations derive from this marker base; the
// virtual destructor makes `dynamic_cast<T*>` the is-a lookup the C# `as T`/`is T` provide.
// An annotation may override `Clone()` to return a deep copy (clonable); the default
// returns null (non-clonable -- `CloneAnnotationsFrom` shares it as-is instead).
struct AnnotationBase {
    virtual ~AnnotationBase() = default;

    // Returns a deep copy of this annotation, or null when it is not clonable (the C#
    // `ICloneable` test -- `CloneAnnotationsFrom` shares a non-clonable annotation rather
    // than dropping it). Override to make an annotation clonable.
    virtual std::shared_ptr<AnnotationBase> Clone() const { return nullptr; }
};

// The base class implementing the annotation channel. `AstNode` derives from this; the
// annotation storage is lazily allocated (null until the first `AddAnnotation`) so a node
// without annotations (the overwhelmingly common case) pays only one pointer.
class AbstractAnnotatable {
    std::unique_ptr<std::vector<std::shared_ptr<AnnotationBase>>> annotations_;

public:
    virtual ~AbstractAnnotatable() = default;

    // The C# `void AddAnnotation(object)` -- attach an annotation. A null annotation is
    // rejected (the C# `ArgumentNullException`). The storage is lazily allocated on the
    // first annotation.
    void AddAnnotation(std::shared_ptr<AnnotationBase> annotation) {
        if (!annotation)
            throw std::invalid_argument("AbstractAnnotatable::AddAnnotation: annotation is null");
        if (!annotations_)
            annotations_ = std::make_unique<std::vector<std::shared_ptr<AnnotationBase>>>();
        annotations_->push_back(std::move(annotation));
    }

    // The C# `T? Annotation<T>()` -- the first annotation of type `T` (is-a), or null.
    // `T` must derive from `AnnotationBase` (for the `dynamic_cast` downcast).
    template <class T>
    T* Annotation() const {
        static_assert(std::is_base_of_v<AnnotationBase, T>,
                      "Annotation<T>: T must derive from AnnotationBase");
        if (!annotations_)
            return nullptr;
        for (const auto& a : *annotations_) {
            if (T* t = dynamic_cast<T*>(a.get()))
                return t;
        }
        return nullptr;
    }

    // The C# `void RemoveAnnotations<T>()` -- remove all annotations of type `T` (is-a).
    template <class T>
    void RemoveAnnotations() {
        static_assert(std::is_base_of_v<AnnotationBase, T>,
                      "RemoveAnnotations<T>: T must derive from AnnotationBase");
        if (!annotations_)
            return;
        annotations_->erase(
            std::remove_if(annotations_->begin(), annotations_->end(),
                [](const std::shared_ptr<AnnotationBase>& a) {
                    return dynamic_cast<T*>(a.get()) != nullptr;
                }),
            annotations_->end());
    }

    // The C# `IEnumerable<object> Annotations` -- all annotations on this node, in
    // insertion order. Returned as raw pointers (the node retains ownership).
    std::vector<AnnotationBase*> Annotations() const {
        std::vector<AnnotationBase*> result;
        if (annotations_) {
            result.reserve(annotations_->size());
            for (const auto& a : *annotations_)
                result.push_back(a.get());
        }
        return result;
    }

protected:
    // Replaces the C# `MemberwiseClone` + `protected CloneAnnotations()` combo for the
    // annotation channel. The concrete node's `Clone` calls this on the fresh clone
    // (passing the source) to copy the source's annotations: clonable annotations
    // (`Clone()` returns a copy) are deep-copied onto the clone; non-clonable ones are
    // shared (the clone holds the same `shared_ptr`). A no-op when the source has no
    // annotations.
    void CloneAnnotationsFrom(const AbstractAnnotatable& source) {
        if (!source.annotations_)
            return;
        std::vector<std::shared_ptr<AnnotationBase>> cloned;
        cloned.reserve(source.annotations_->size());
        for (const auto& a : *source.annotations_) {
            if (std::shared_ptr<AnnotationBase> copy = a->Clone())
                cloned.push_back(std::move(copy));
            else
                cloned.push_back(a);  // share (the source's shared_ptr)
        }
        annotations_ =
            std::make_unique<std::vector<std::shared_ptr<AnnotationBase>>>(std::move(cloned));
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax
