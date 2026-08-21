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

// Implementation of `NestedTypeReference` (see NestedTypeReference.hpp). The
// `Resolve` body resolves the declaring-type reference, downcasts to
// `ITypeDefinition`, and searches `NestedTypes` for a matching name + total
// `TypeParameterCount`; the fallback constructs an `UnknownType(null, name,
// additionalTypeParameterCount)` (cached). The destructor is out-of-line because
// the `resolved_` member holds an incomplete `IType` in the header.

#include "Decompiler/TypeSystem/NestedTypeReference.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `NestedTypeReference(ITypeReference declaringTypeRef, string name, int
// additionalTypeParameterCount, bool? isReferenceType = null)` -- asserts the
// declaring type is non-null (the C# `ArgumentNullException` on null) and the name
// is non-empty (the C# `ArgumentNullException` on a null name -- a `std::string`
// has no null state, so the empty string stands in as the impossible sentinel).
NestedTypeReference::NestedTypeReference(
    std::shared_ptr<const ITypeReference> declaringTypeRef,
    std::string name,
    int additionalTypeParameterCount,
    std::optional<bool> isReferenceType)
    : declaringTypeRef_(std::move(declaringTypeRef)),
      name_(std::move(name)),
      additionalTypeParameterCount_(additionalTypeParameterCount),
      isReferenceType_(isReferenceType)
{
    assert(declaringTypeRef_ != nullptr);
    assert(!name_.empty());
}

// The C# `IType Resolve(ITypeResolveContext context)`:
//   ITypeDefinition declaringType = declaringTypeRef.Resolve(context) as ITypeDefinition;
//   if (declaringType != null)
//   {
//       int tpc = declaringType.TypeParameterCount;
//       foreach (IType type in declaringType.NestedTypes)
//       {
//           if (type.Name == name && type.TypeParameterCount == tpc + additionalTypeParameterCount)
//               return type;
//       }
//   }
//   return new UnknownType(null, name, additionalTypeParameterCount);
//
// The C# returns the matched nested type by reference (GC-owned by the declaring
// type definition) or constructs a NEW `UnknownType` (GC-owned) each call; the C++
// port returns `const IType&` (the D408 non-null reference). A matched nested type
// is owned by the declaring type definition (it lives in the `NestedTypes`
// snapshot), so the returned reference is valid without caching. The fallback
// `UnknownType` is a new object the port must own, so it is cached as a `mutable`
// `shared_ptr<const IType>` member, built on the first `Resolve` that reaches the
// fallback. The `dynamic_cast<const ITypeDefinition*>` (the C# `as
// ITypeDefinition`) yields null when the resolved declaring type is not a type
// definition (e.g. it is a `ParameterizedType`), skipping the search and falling
// through to the `UnknownType` fallback.
const IType& NestedTypeReference::Resolve(
    const ITypeResolveContext& context) const
{
    const IType& declaringType = declaringTypeRef_->Resolve(context);
    const auto* td = dynamic_cast<const ITypeDefinition*>(&declaringType);
    if (td != nullptr)
    {
        int tpc = td->TypeParameterCount();
        for (const auto* nested : td->NestedTypes())
        {
            if (nested->Name() == name_
                && nested->TypeParameterCount()
                       == tpc + additionalTypeParameterCount_)
            {
                return *nested;
            }
        }
    }
    if (!resolved_)
    {
        // The `inline ITypePtr UnknownType()` convenience function (the
        // SpecialType-based null object) shares the name `UnknownType` with the
        // `class UnknownType` (the minimal-port IType the fallback constructs) in
        // the same scope. `std::make_shared<UnknownType>` would resolve the name to
        // the function (MSVC C2672); an elaborated-type-specifier (`class
        // UnknownType`) in a `new` expression explicitly targets the class, and the
        // resulting `UnknownType*` binds to the `shared_ptr<const IType>` via the
        // derived-to-base pointer conversion (UnknownType IS-A IType).
        resolved_ = std::shared_ptr<const IType>(
            new class UnknownType(std::nullopt, name_, additionalTypeParameterCount_));
    }
    return *resolved_;
}

// Out-of-line destructor (the `resolved_` member holds an incomplete `IType` in
// the header). The `shared_ptr<const IType>` deleter is type-erased, so this is
// the faithful no-op default.
NestedTypeReference::~NestedTypeReference() = default;

} // namespace ILSpy::Decompiler::TypeSystem
