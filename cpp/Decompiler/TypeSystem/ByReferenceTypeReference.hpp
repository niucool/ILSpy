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

// Port of the `ByReferenceTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/ByReferenceType.cs (the `sealed class
// ByReferenceTypeReference : ITypeReference, ISupportsInterning` half of that
// file). A `ByReferenceTypeReference` is a reference that, when resolved against
// an `ITypeResolveContext`, produces a `ByReferenceType` wrapping the element
// reference's resolved `IType` -- the `ref T` / `out T` managed-pointer shape the
// signature decoder builds for by-ref parameters.
//
// It is the second concrete `ITypeReference` (after `KnownTypeReference` D411),
// the first to derive from BOTH `ITypeReference` (D408) and `ISupportsInterning`
// (D412), and the structural twin of `PointerTypeReference` (the `*` vs `&`
// managed-vs-unmanaged-pointer pair). It is a leaf TypeSystem dependency toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`); the `InterningProvider` (not yet ported) deduplicates
// structurally-equal references via the `ISupportsInterning` accessors.
//
// KEY PORT CONVENTIONS:
// (a) The C# `sealed class ByReferenceTypeReference : ITypeReference,
// ISupportsInterning` ports to a C++ `final` class multiply-inheriting
// `ITypeReference` (D408) and `ISupportsInterning` (D412). The two bases share no
// common base (neither derives from the other, and neither derives from a shared
// third), so there is NO diamond and NO `Name`/`SymbolKind` redeclaration -- the
// cleanest multiple-inheritance shape (structurally unlike `IField` (D391) whose
// two bases both derived from `ISymbol`).
// (b) The C# `readonly ITypeReference elementType` (a reference-type field,
// non-null per the `ArgumentNullException` ctor guard) ports to a
// `std::shared_ptr<const ITypeReference>` member -- the shared-ownership model
// for a polymorphic reference type (the `ITypePtr` (D271) convention extended to
// `ITypeReference`). The ctor asserts non-null (the C# `ArgumentNullException`
// port, the D383 `TypeConstraint` non-null-`ITypePtr`-ctor-assert precedent).
// (c) The C# `IType Resolve(ITypeResolveContext context) => new
// ByReferenceType(elementType.Resolve(context))` creates a NEW `ByReferenceType`
// each call (GC-owned; the caller holds the returned reference). The C++ port
// returns `const IType&` (the D408 non-null-reference return), so the constructed
// `ByReferenceType` must outlive the call: it is cached as a `mutable
// std::shared_ptr<ByReferenceType>` member, built on the first `Resolve`. The
// element's resolved `IType` is shared_ptr-owned (the C# GC-owned reference), so
// the port obtains an `ITypePtr` to it via `shared_from_this()` (the D406
// `enable_shared_from_this<IType>` bridge) -- the resolved `ByReferenceType`
// then shares ownership of the element `IType` rather than copying it (the
// faithful C# reference-wrap, not a deep copy). `ByReferenceType` is
// forward-declared in this header and the destructor is out-of-line (the
// `resolved_` member holds an incomplete type, the shared_ptr-deleter-is-
// type-erased pattern); `Resolve` is out-of-line (needs `ByReferenceType` +
// `IType` + `ITypeResolveContext` complete).
// (d) The C# `int ISupportsInterning.GetHashCodeForInterning() => elementType.GetHashCode()
// ^ 91725814` uses the IDENTITY hash of the reference-type `elementType` (C#
// default `GetHashCode` for a reference type without an override is
// `RuntimeHelpers.GetHashCode`, the identity hash). The C++ port XORs the
// shared_ptr identity hash (`std::hash<std::shared_ptr<const ITypeReference>>`
// hashes the stored pointer = the identity hash) with the C# salt `91725814`.
// (e) The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other) =>
// (other as ByReferenceTypeReference) is ByReferenceTypeReference brt && this.elementType
// == brt.elementType` uses REFERENCE equality on the `elementType` (C# reference
// equality on reference types). The C++ port downcasts via `dynamic_cast<const
// ByReferenceTypeReference*>(&other)` (the C# `as`-returns-null case, the D412
// precedent) and compares `elementType_ == brt->elementType_` (shared_ptr `==`
// compares stored pointers = reference identity, the faithful C# reference
// equality).
// (f) The C# `override string ToString() => elementType.ToString() + "&"` is
// DEFERRED: `ITypeReference` (the C# interface) does not declare `ToString`
// (it inherits `Object.ToString`), so the C++ `ITypeReference` interface has no
// polymorphic `ToString` for the element, and adding one is an interface
// extension outside this leaf's scope. The port lands the `ITypeReference` +
// `ISupportsInterning` contract (`Resolve` + the two interning accessors) and
// defers `ToString` with the not-yet-ported polymorphic-element-`ToString`
// infrastructure.

#pragma once

#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IType` (the D271 minimal port, the `Resolve` return
// type), `ITypeResolveContext` (the D409 paired context interface, the `Resolve`
// parameter type), and `ByReferenceType` (the D271 minimal concrete `IType` the
// resolved reference produces). `ByReferenceType` is incomplete here -- the
// `resolved_` member's `shared_ptr<ByReferenceType>` deleter is type-erased, so
// the member can hold an incomplete type, and the destructor is out-of-line so
// the header does not need `ByReferenceType` complete.
class IType;
class ITypeResolveContext;
class ByReferenceType;

// A reference that resolves to a `ByReferenceType` wrapping the element
// reference's resolved `IType` -- the `ref T` / `out T` managed-pointer shape.
// The C# `sealed class` counterpart; it is `final` here, deriving from both
// `ITypeReference` (the resolve contract) and `ISupportsInterning` (the
// interning-deduplication contract).
class ByReferenceTypeReference final : public ITypeReference,
                                       public ISupportsInterning {
public:
    // The C# `ByReferenceTypeReference(ITypeReference elementType)` -- the
    // non-null element reference the by-reference wraps. The C# ctor throws
    // `ArgumentNullException` on null; the C++ port asserts non-null (the D383
    // `TypeConstraint` non-null-ctor-assert precedent).
    explicit ByReferenceTypeReference(
        std::shared_ptr<const ITypeReference> elementType);

    // The C# `ITypeReference ElementType { get; }` -- the wrapped element
    // reference (non-null).
    const std::shared_ptr<const ITypeReference>& ElementType() const noexcept
    {
        return elementType_;
    }

    // The C# `IType Resolve(ITypeResolveContext context) => new
    // ByReferenceType(elementType.Resolve(context))` -- resolves this reference
    // to a `ByReferenceType` wrapping the element's resolved `IType`. Never null
    // (a non-null reference return, the D408 precedent); cached on first call.
    const IType& Resolve(const ITypeResolveContext& context) const override;

    // The C# `int ISupportsInterning.GetHashCodeForInterning() =>
    // elementType.GetHashCode() ^ 91725814` -- the identity hash of the element
    // reference XORed with the by-reference salt. `std::hash<shared_ptr<T>>`
    // hashes the stored pointer (the identity hash), faithful to the C# default
    // reference-type `GetHashCode`.
    int GetHashCodeForInterning() const override
    {
        return static_cast<int>(
                   std::hash<std::shared_ptr<const ITypeReference>>{}(
                       elementType_))
               ^ 91725814;
    }

    // The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)
    // => (other as ByReferenceTypeReference) is { } brt && this.elementType ==
    // brt.elementType` -- downcast via `dynamic_cast` (returns null on a type
    // mismatch) and reference-equality on the element (shared_ptr `==` compares
    // stored pointers = reference identity).
    bool EqualsForInterning(const ISupportsInterning& other) const override
    {
        const auto* brt = dynamic_cast<const ByReferenceTypeReference*>(&other);
        return brt != nullptr && elementType_ == brt->elementType_;
    }

    // Out-of-line destructor: the `resolved_` member holds an incomplete
    // `ByReferenceType` (forward-declared above), so the destructor is defined
    // in the `.cpp` where `ByReferenceType` is complete.
    ~ByReferenceTypeReference() override;

private:
    std::shared_ptr<const ITypeReference> elementType_;
    // The resolved `ByReferenceType`, cached on the first `Resolve` call so the
    // returned `const IType&` outlives the call. `mutable` because `Resolve` is
    // `const` (it reads the element reference without mutating, the D408
    // const-`Resolve` precedent) but lazily constructs its result.
    mutable std::shared_ptr<ByReferenceType> resolved_;
};

} // namespace ILSpy::Decompiler::TypeSystem
