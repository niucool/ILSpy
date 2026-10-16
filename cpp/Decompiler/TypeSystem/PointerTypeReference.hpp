// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including limitation, without restriction, the rights to use,
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

// Port of the `PointerTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/PointerType.cs (the `sealed class
// PointerTypeReference : ITypeReference, ISupportsInterning` half of that file).
// A `PointerTypeReference` is a reference that, when resolved against an
// `ITypeResolveContext`, produces a `PointerType` wrapping the element
// reference's resolved `IType` -- the unmanaged `T*` pointer shape the signature
// decoder builds for C# `unsafe` pointer types.
//
// It is the structural twin of `ByReferenceTypeReference` (the `*` vs `&`
// unmanaged-vs-managed-pointer pair): the two classes are line-for-line
// identical except for the produced `IType` (`PointerType` vs
// `ByReferenceType`), the salt constant (`91725812` vs `91725814`), and the
// `ToString` suffix (`"*"` vs `"&"`). It is the third concrete `ITypeReference`
// (after `KnownTypeReference` D411 and `ByReferenceTypeReference`), the second
// to derive from both `ITypeReference` (D408) and `ISupportsInterning` (D412).
// It is a leaf TypeSystem dependency toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker of `CSharpAmbience`).
//
// KEY PORT CONVENTIONS: identical to `ByReferenceTypeReference` (the structural
// twin) -- see ByReferenceTypeReference.hpp for the full rationale. The only
// divergences are the produced `IType` (`PointerType`, forward-declared here),
// the interning salt (`91725812`, the C# `elementType.GetHashCode() ^ 91725812`),
// and the deferred `ToString` suffix (`"*"`). The `ToString` is deferred for
// the same reason (no polymorphic `ToString` on the `ITypeReference` interface).

#pragma once

#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IType`, `ITypeResolveContext`, and `PointerType` (the
// D271 minimal concrete `IType` the resolved reference produces). `PointerType`
// is incomplete here -- the `resolved_` member's `shared_ptr<PointerType>`
// deleter is type-erased, so the member can hold an incomplete type, and the
// destructor is out-of-line.
class IType;
class ITypeResolveContext;
class PointerType;

// A reference that resolves to a `PointerType` wrapping the element reference's
// resolved `IType` -- the unmanaged `T*` pointer shape. The C# `sealed class`
// counterpart; it is `final` here, deriving from both `ITypeReference` (the
// resolve contract) and `ISupportsInterning` (the interning-deduplication
// contract).
class PointerTypeReference final : public ITypeReference,
                                   public ISupportsInterning {
public:
    // The C# `PointerTypeReference(ITypeReference elementType)` -- the non-null
    // element reference the pointer wraps. Asserts non-null (the C#
    // `ArgumentNullException` on null, the D383 non-null-ctor-assert precedent).
    explicit PointerTypeReference(
        std::shared_ptr<const ITypeReference> elementType);

    // The C# `ITypeReference ElementType { get; }` -- the wrapped element
    // reference (non-null).
    const std::shared_ptr<const ITypeReference>& ElementType() const noexcept
    {
        return elementType_;
    }

    // The C# `IType Resolve(ITypeResolveContext context) => new
    // PointerType(elementType.Resolve(context))` -- resolves this reference to
    // a `PointerType` wrapping the element's resolved `IType`. Never null (a
    // non-null reference return, the D408 precedent); cached on first call.
    const IType& Resolve(const ITypeResolveContext& context) const override;

    // The C# `int ISupportsInterning.GetHashCodeForInterning() =>
    // elementType.GetHashCode() ^ 91725812` -- the identity hash of the element
    // reference XORed with the pointer salt.
    int GetHashCodeForInterning() const override
    {
        return static_cast<int>(
                   std::hash<std::shared_ptr<const ITypeReference>>{}(
                       elementType_))
               ^ 91725812;
    }

    // The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)
    // => (other as PointerTypeReference) is { } o && this.elementType ==
    // o.elementType` -- downcast via `dynamic_cast` (returns null on a type
    // mismatch) and reference-equality on the element (shared_ptr `==` compares
    // stored pointers = reference identity).
    bool EqualsForInterning(const ISupportsInterning& other) const override
    {
        const auto* o = dynamic_cast<const PointerTypeReference*>(&other);
        return o != nullptr && elementType_ == o->elementType_;
    }

    // Out-of-line destructor: the `resolved_` member holds an incomplete
    // `PointerType` (forward-declared above).
    ~PointerTypeReference() override;

private:
    std::shared_ptr<const ITypeReference> elementType_;
    // The resolved `PointerType`, cached on the first `Resolve` call so the
    // returned `const IType&` outlives the call. `mutable` because `Resolve` is
    // `const` but lazily constructs its result.
    mutable std::shared_ptr<PointerType> resolved_;
};

} // namespace ILSpy::Decompiler::TypeSystem
