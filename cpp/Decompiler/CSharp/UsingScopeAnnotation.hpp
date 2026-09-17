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

// The `UsingScope` AST annotation: the C# `IntroduceUsingDeclarations` attaches a
// `UsingScope` object directly to the syntax-tree root (`rootNode.AddAnnotation(usingScope)`)
// and `IntroduceExtensionMethods` reads it back. The port's annotation channel owns via
// `shared_ptr<AnnotationBase>`, so the scope is wrapped in this holder (the
// `ILFunctionAnnotation` precedent); the holder shares ownership of the scope.
//
// This lives outside Annotations.hpp on purpose: naming the sibling
// `ILSpy::Decompiler::CSharp::TypeSystem` namespace there would make unqualified
// `TypeSystem::` lookups inside `ILSpy::Decompiler::CSharp` resolve to the CSharp one in
// every translation unit that includes the widely-included Annotations.hpp, breaking the
// real `ILSpy::Decompiler::TypeSystem` references. Only the consumers of this annotation
// include this header.

#pragma once

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"

#include <memory>
#include <utility>

namespace ILSpy::Decompiler::CSharp::TypeSystem {
class UsingScope;
} // namespace ILSpy::Decompiler::CSharp::TypeSystem

namespace ILSpy::Decompiler::CSharp {

// The port holder for the C# `rootNode.AddAnnotation(usingScope)` -- the `UsingScope`
// object as an AST annotation.
class UsingScopeAnnotation final : public Syntax::AnnotationBase {
public:
    std::shared_ptr<TypeSystem::UsingScope> Scope;

    explicit UsingScopeAnnotation(std::shared_ptr<TypeSystem::UsingScope> scope)
        : Scope(std::move(scope)) {}
};

// The C# `rootNode.AddAnnotation(usingScope)` over the holder channel.
inline void WithUsingScope(Syntax::AstNode& node,
                           std::shared_ptr<TypeSystem::UsingScope> scope) {
    node.AddAnnotation(std::make_shared<UsingScopeAnnotation>(std::move(scope)));
}

// The C# `rootNode.Annotation<UsingScope>()` -- the scope carried by the node's
// UsingScope annotation, or null.
inline std::shared_ptr<TypeSystem::UsingScope> GetUsingScope(const Syntax::AstNode& node) {
    const auto* annotation = node.Annotation<UsingScopeAnnotation>();
    return annotation ? annotation->Scope : nullptr;
}

} // namespace ILSpy::Decompiler::CSharp
