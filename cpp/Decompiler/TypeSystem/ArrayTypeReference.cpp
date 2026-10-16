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

// Implementation of `ArrayTypeReference` (see ArrayTypeReference.hpp). The
// `Resolve` body constructs an `ArrayType` of the given rank wrapping the
// element reference's resolved `IType`, sharing ownership of the resolved
// element `IType` (the C# GC-owned reference wrap, not a deep copy). The
// destructor is out-of-line because the `resolved_` member holds an incomplete
// `ArrayType` in the header. Structurally the D413/D414 reference siblings plus a
// `dimensions` int field; the compilation argument the C# `ArrayType` ctor takes
// is dropped (the minimal-port `ArrayType` does not store it).

#include "Decompiler/TypeSystem/ArrayTypeReference.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `ArrayTypeReference(ITypeReference elementType, int dimensions = 1)` --
// asserts the element is non-null (the C# `ArgumentNullException` on null) and
// the dimensions are positive (the C# `ArgumentOutOfRangeException` on
// `dimensions <= 0`).
ArrayTypeReference::ArrayTypeReference(
    std::shared_ptr<const ITypeReference> elementType, int dimensions)
    : elementType_(std::move(elementType)), dimensions_(dimensions)
{
    assert(elementType_ != nullptr);
    assert(dimensions_ > 0);
}

// The C# `IType Resolve(ITypeResolveContext context) => new
// ArrayType(context.Compilation, elementType.Resolve(context), dimensions)`. The
// C# creates a NEW `ArrayType` (GC-owned) each call; the C++ port returns
// `const IType&` (the D408 non-null reference), so the constructed `ArrayType` is
// cached as a `mutable` member, built on the first `Resolve`. The element's
// resolved `IType` is `shared_ptr`-owned (the C# GC-owned reference), so an
// `ITypePtr` to it is obtained via `shared_from_this()` (the D406
// `enable_shared_from_this<IType>` bridge) -- the `ArrayType` then shares
// ownership of the element `IType` rather than copying it (the faithful C#
// reference-wrap). `const_pointer_cast` rebinds the `const IType&` (the `Resolve`
// return) to the `ITypePtr` the `ArrayType` ctor takes; the element is only read,
// never mutated through the array, so the cast is safe. The compilation argument
// the C# passes is dropped (the minimal-port `ArrayType` does not store it); the
// multi-dim ctor `ArrayType(ITypePtr, int rank)` is used (NOT the SZArray ctor,
// because the C# `ArrayType(compilation, element, 1)` is a 1-D array with
// `dimensions=1`, not an SZArray -- the SZArray shape is a distinct minimal-port
// ctor the signature decoder uses elsewhere).
const IType& ArrayTypeReference::Resolve(
    const ITypeResolveContext& context) const
{
    if (!resolved_)
    {
        const IType& elementType = elementType_->Resolve(context);
        ITypePtr elementPtr =
            std::const_pointer_cast<IType>(elementType.shared_from_this());
        resolved_ = std::make_shared<ArrayType>(std::move(elementPtr), dimensions_);
    }
    return *resolved_;
}

// Out-of-line destructor (the `resolved_` member holds an incomplete
// `ArrayType` in the header). The `shared_ptr<ArrayType>` deleter is
// type-erased, so this is the faithful no-op default.
ArrayTypeReference::~ArrayTypeReference() = default;

} // namespace ILSpy::Decompiler::TypeSystem
