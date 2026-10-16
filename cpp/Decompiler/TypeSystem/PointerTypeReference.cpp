// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies of substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `PointerTypeReference` (see PointerTypeReference.hpp). The
// structural twin of `ByReferenceTypeReference.cpp`: the `Resolve` body constructs
// a `PointerType` wrapping the element reference's resolved `IType`, sharing
// ownership of the resolved element (the C# GC-owned reference wrap). The
// destructor is out-of-line because the `resolved_` member holds an incomplete
// `PointerType` in the header.

#include "Decompiler/TypeSystem/PointerTypeReference.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `PointerTypeReference(ITypeReference elementType)` -- asserts the
// element is non-null (the C# `ArgumentNullException` on null).
PointerTypeReference::PointerTypeReference(
    std::shared_ptr<const ITypeReference> elementType)
    : elementType_(std::move(elementType))
{
    assert(elementType_ != nullptr);
}

// The C# `IType Resolve(ITypeResolveContext context) => new
// PointerType(elementType.Resolve(context))`. The C# creates a NEW `PointerType`
// (GC-owned) each call; the C++ port returns `const IType&` (the D408
// non-null reference), so the constructed `PointerType` is cached as a
// `mutable` member, built on the first `Resolve`. The element's resolved `IType`
// is `shared_ptr`-owned (the D406 `enable_shared_from_this<IType>` bridge) -- the
// `PointerType` then shares ownership of the element `IType` rather than copying
// it (the faithful C# reference-wrap). `const_pointer_cast` rebinds the
// `const IType&` (the `Resolve` return) to the `ITypePtr` the `PointerType` ctor
// takes; the element is only read, never mutated through the pointer, so the cast
// is safe.
const IType& PointerTypeReference::Resolve(
    const ITypeResolveContext& context) const
{
    if (!resolved_)
    {
        const IType& elementType = elementType_->Resolve(context);
        ITypePtr elementPtr =
            std::const_pointer_cast<IType>(elementType.shared_from_this());
        resolved_ = std::make_shared<PointerType>(std::move(elementPtr));
    }
    return *resolved_;
}

// Out-of-line destructor (the `resolved_` member holds an incomplete
// `PointerType` in the header).
PointerTypeReference::~PointerTypeReference() = default;

} // namespace ILSpy::Decompiler::TypeSystem
