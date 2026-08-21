// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including, without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Implementation of `ParameterizedTypeReference` (see ParameterizedTypeReference.hpp).
// The `Resolve` body resolves the generic-type reference, reads the resolved base
// type's `TypeParameterCount`, and either returns the bare base type (`tpc == 0`,
// the early-return crux) or constructs a `ParameterizedType` of the base type with
// the resolved type arguments, filling any missing slots with `UnknownType` (the
// fill crux). The constructed `ParameterizedType` shares ownership of the base
// type and each resolved type argument (the C# GC-owned reference wrap, not deep
// copies). The destructor is out-of-line because the `resolved_` member holds an
// incomplete `IType` in the header.

#include "Decompiler/TypeSystem/ParameterizedTypeReference.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `ParameterizedTypeReference(ITypeReference genericType,
// IEnumerable<ITypeReference> typeArguments)` -- asserts the generic type is
// non-null (the C# `ArgumentNullException` on null) and each type-argument element
// is non-null (the C# per-index `ArgumentNullException`).
ParameterizedTypeReference::ParameterizedTypeReference(
    std::shared_ptr<const ITypeReference> genericType,
    std::vector<std::shared_ptr<const ITypeReference>> typeArguments)
    : genericType_(std::move(genericType)), typeArguments_(std::move(typeArguments))
{
    assert(genericType_ != nullptr);
    for (const auto& t : typeArguments_)
    {
        (void)t;
        assert(t != nullptr);
    }
}

// The C# `IType Resolve(ITypeResolveContext context)`:
//   IType baseType = genericType.Resolve(context);
//   int tpc = baseType.TypeParameterCount;
//   if (tpc == 0) return baseType;
//   IType[] resolvedTypes = new IType[tpc];
//   for (int i = 0; i < resolvedTypes.Length; i++)
//       resolvedTypes[i] = (i < typeArguments.Length) ? typeArguments[i].Resolve(context)
//                                                    : SpecialType.UnknownType;
//   return new ParameterizedType(baseType, resolvedTypes);
//
// The C# returns the base type by reference (GC-owned) or constructs a new
// `ParameterizedType` (GC-owned) each call; the C++ port returns `const IType&`
// (the D408 non-null reference), so the result is cached as a `mutable` member,
// built on the first `Resolve`. For `tpc == 0` the bare base type is cached
// directly (its `shared_from_this()` keeps it alive); for `tpc > 0` a
// `ParameterizedType` is constructed sharing ownership of the base type and each
// resolved type argument via `shared_from_this()` (the D406 bridge) +
// `const_pointer_cast` (the resolved `IType` instances are shared, not copied --
// the faithful C# reference-wrap). Missing slots (`typeArguments_.size() < tpc`)
// are filled with `UnknownType()` (the C# `SpecialType.UnknownType`); extra
// arguments past `tpc` are dropped (`resolvedTypes` is `tpc` long, the loop stops
// at `tpc`, faithful to the C#).
const IType& ParameterizedTypeReference::Resolve(
    const ITypeResolveContext& context) const
{
    if (!resolved_)
    {
        const IType& baseType = genericType_->Resolve(context);
        int tpc = baseType.TypeParameterCount();
        if (tpc == 0)
        {
            // The `tpc == 0` early-return crux: a non-generic base type is
            // returned unchanged (NOT wrapped). Cache the base type directly so
            // the returned `const IType&` outlives the call.
            resolved_ = baseType.shared_from_this();
        }
        else
        {
            std::vector<ITypePtr> resolvedTypes;
            resolvedTypes.reserve(static_cast<std::size_t>(tpc));
            for (int i = 0; i < tpc; ++i)
            {
                if (i < static_cast<int>(typeArguments_.size()))
                {
                    const IType& ta = typeArguments_[static_cast<std::size_t>(i)]
                                          ->Resolve(context);
                    resolvedTypes.push_back(
                        std::const_pointer_cast<IType>(ta.shared_from_this()));
                }
                else
                {
                    // The missing-argument fill crux: a slot past the
                    // reference's own argument list is filled with
                    // `UnknownType` (the C# `SpecialType.UnknownType`).
                    resolvedTypes.push_back(UnknownType());
                }
            }
            ITypePtr basePtr =
                std::const_pointer_cast<IType>(baseType.shared_from_this());
            resolved_ = std::make_shared<ParameterizedType>(
                std::move(basePtr), std::move(resolvedTypes));
        }
    }
    return *resolved_;
}

// Out-of-line destructor (the `resolved_` member holds an incomplete `IType` in
// the header). The `shared_ptr<const IType>` deleter is type-erased, so this is
// the faithful no-op default.
ParameterizedTypeReference::~ParameterizedTypeReference() = default;

} // namespace ILSpy::Decompiler::TypeSystem
