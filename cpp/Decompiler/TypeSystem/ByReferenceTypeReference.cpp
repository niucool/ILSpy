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
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `ByReferenceTypeReference` (see ByReferenceTypeReference.hpp).
// The `Resolve` body constructs a `ByReferenceType` wrapping the element
// reference's resolved `IType`, sharing ownership of the resolved element `IType`
// (the C# GC-owned reference wrap, not a deep copy). The destructor is
// out-of-line because the `resolved_` member holds an incomplete `ByReferenceType`
// in the header.

#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `ByReferenceTypeReference(ITypeReference elementType)` -- asserts the
// element is non-null (the C# `ArgumentNullException` on null).
ByReferenceTypeReference::ByReferenceTypeReference(
    std::shared_ptr<const ITypeReference> elementType)
    : elementType_(std::move(elementType))
{
    assert(elementType_ != nullptr);
}

// The C# `IType Resolve(ITypeResolveContext context) => new
// ByReferenceType(elementType.Resolve(context))`. The C# creates a NEW
// `ByReferenceType` (GC-owned) each call; the C++ port returns `const IType&`
// (the D408 non-null reference), so the constructed `ByReferenceType` is cached
// as a `mutable` member, built on the first `Resolve`. The element's resolved
// `IType` is `shared_ptr`-owned (the C# GC-owned reference), so an `ITypePtr` to
// it is obtained via `shared_from_this()` (the D406
// `enable_shared_from_this<IType>` bridge) -- the `ByReferenceType` then shares
// ownership of the element `IType` rather than copying it (the faithful C#
// reference-wrap). `const_pointer_cast` rebinds the `const IType&` (the `Resolve`
// return) to the `ITypePtr` (`shared_ptr<IType>`) the `ByReferenceType` ctor
// takes; the element is only read, never mutated through the by-reference, so
// the cast is safe.
const IType& ByReferenceTypeReference::Resolve(
    const ITypeResolveContext& context) const
{
    if (!resolved_)
    {
        const IType& elementType = elementType_->Resolve(context);
        ITypePtr elementPtr =
            std::const_pointer_cast<IType>(elementType.shared_from_this());
        resolved_ = std::make_shared<ByReferenceType>(std::move(elementPtr));
    }
    return *resolved_;
}

// Out-of-line destructor (the `resolved_` member holds an incomplete
// `ByReferenceType` in the header). The `shared_ptr<ByReferenceType>` deleter
// is type-erased, so this is the faithful no-op default.
ByReferenceTypeReference::~ByReferenceTypeReference() = default;

} // namespace ILSpy::Decompiler::TypeSystem
