// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including without limitation the rights to use, copy, modify, merge,
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

// Port of the `ArrayTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/ArrayType.cs (the `sealed class
// ArrayTypeReference : ITypeReference, ISupportsInterning` half of that file).
// An `ArrayTypeReference` is a reference that, when resolved against an
// `ITypeResolveContext`, produces an `ArrayType` of the given rank wrapping the
// element reference's resolved `IType` -- the `T[]` / `T[,]` / `T[,,]` array shape
// the signature decoder builds for array types.
//
// It is the fourth concrete `ITypeReference` (after `KnownTypeReference` D411,
// `ByReferenceTypeReference` D413, `PointerTypeReference` D414) and the third to
// derive from both `ITypeReference` (D408) and `ISupportsInterning` (D412). It is
// structurally a `ByReferenceTypeReference`/`PointerTypeReference` sibling that
// adds a `dimensions` (rank) int field: the interning hash XORs the element's
// identity hash with the dimensions (NOT a fixed salt), and the interning
// equality compares both the element and the dimensions. It is a leaf TypeSystem
// dependency toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`); the `InterningProvider` (not yet
// ported) deduplicates structurally-equal references via the `ISupportsInterning`
// accessors.
//
// KEY PORT CONVENTIONS:
// (a) The C# `sealed class ArrayTypeReference : ITypeReference,
// ISupportsInterning` ports to a C++ `final` class multiply-inheriting
// `ITypeReference` (D408) and `ISupportsInterning` (D412). The two bases share no
// common base, so there is NO diamond and NO `Name`/`SymbolKind` redeclaration --
// the cleanest multiple-inheritance shape (structurally unlike `IField` (D391)
// whose two bases both derived from `ISymbol`).
// (b) The C# `readonly ITypeReference elementType` (non-null per the
// `ArgumentNullException` ctor guard) ports to a
// `std::shared_ptr<const ITypeReference>` member (the `ITypePtr` convention
// extended to `ITypeReference`, the D413 precedent). The ctor asserts non-null
// (the C# `ArgumentNullException` port, the D383 non-null-ctor-assert precedent).
// (c) The C# `readonly int dimensions` (positive per the `ArgumentOutOfRangeException`
// ctor guard, default 1) ports to an `int` member with a default ctor argument of
// 1; the ctor asserts `dimensions > 0` (the C# `dimensions <= 0` throw port).
// (d) The C# `IType Resolve(ITypeResolveContext context) => new
// ArrayType(context.Compilation, elementType.Resolve(context), dimensions)`.
// The C# `ArrayType` ctor takes the compilation (used only for `DirectBaseTypes`/
// `GetMethods`, surface not in the minimal port); the minimal-port `ArrayType`
// (D271) ctor is `ArrayType(ITypePtr element, int rank)` (it does NOT store the
// compilation). The port therefore drops the compilation argument (the
// minimal-port convention -- the `ArrayType` was ported at D271 without the
// compilation member) and constructs `ArrayType(elementPtr, dimensions)` -- the
// multi-dim ctor (NOT the SZArray ctor: the C# `ArrayType(compilation, element,
// 1)` is a 1-D array with `dimensions=1`, not an SZArray; the minimal-port SZArray
// ctor `ArrayType(ITypePtr)` is a distinct shape the signature decoder uses
// elsewhere). As with the D413/D414 references, the constructed `ArrayType` is
// cached as a `mutable std::shared_ptr<ArrayType>` member, built on the first
// `Resolve`, because the C++ `Resolve` returns `const IType&` (the D408
// non-null-reference return) and the result must outlive the call. The element's
// resolved `IType` is shared_ptr-owned (the C# GC-owned reference), obtained via
// `shared_from_this()` (the D406 `enable_shared_from_this<IType>` bridge) +
// `const_pointer_cast` (the resolved `ByReferenceType`/`PointerType` shares
// ownership of the element `IType`, not a deep copy -- the faithful C# reference
// wrap). `ArrayType` is forward-declared and the destructor is out-of-line (the
// `resolved_` member holds an incomplete type, the shared_ptr-deleter-is-
// type-erased pattern); `Resolve` is out-of-line (needs `ArrayType` + `IType` +
// `ITypeResolveContext` complete).
// (e) The C# `int ISupportsInterning.GetHashCodeForInterning() =>
// elementType.GetHashCode() ^ dimensions` uses the IDENTITY hash of the
// reference-type `elementType` XORed with the dimensions (an int, NOT a fixed
// salt -- the structural distinction from the D413/D414 twins). The C++ port
// XORs the shared_ptr identity hash with `dimensions_` (`static_cast<int>`-ed to
// avoid C4267, the D413 precedent).
// (f) The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)
// => (other as ArrayTypeReference) != null && elementType == o.elementType &&
// dimensions == o.dimensions` uses REFERENCE equality on the `elementType` and
// value equality on the dimensions. The C++ port downcasts via `dynamic_cast`
// (the C# `as`-returns-null case, the D412 precedent) and compares
// `elementType_ == o->elementType_` (shared_ptr `==` = reference identity) AND
// `dimensions_ == o->dimensions_`.
// (g) The C# `override string ToString() => elementType.ToString() + "[" +
// new string(',', dimensions - 1) + "]"` is DEFERRED for the same reason as the
// D413/D414 twins: `ITypeReference` (the C# interface) does not declare
// `ToString` (it inherits `Object.ToString`), so the C++ `ITypeReference`
// interface has no polymorphic `ToString` for the element, and adding one is an
// interface extension outside this leaf's scope.

#pragma once

#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IType` (the D271 minimal port, the `Resolve` return
// type), `ITypeResolveContext` (the D409 paired context interface, the `Resolve`
// parameter type), and `ArrayType` (the D271 minimal concrete `IType` the
// resolved reference produces). `ArrayType` is incomplete here -- the
// `resolved_` member's `shared_ptr<ArrayType>` deleter is type-erased, so the
// member can hold an incomplete type, and the destructor is out-of-line.
class IType;
class ITypeResolveContext;
class ArrayType;

// A reference that resolves to an `ArrayType` of the given rank wrapping the
// element reference's resolved `IType` -- the `T[]` / `T[,]` / `T[,,]` array
// shape. The C# `sealed class` counterpart; it is `final` here, deriving from
// both `ITypeReference` (the resolve contract) and `ISupportsInterning` (the
// interning-deduplication contract).
class ArrayTypeReference final : public ITypeReference,
                                 public ISupportsInterning {
public:
    // The C# `ArrayTypeReference(ITypeReference elementType, int dimensions = 1)`
    // -- the non-null element reference the array wraps and the positive rank.
    // The C# ctor throws `ArgumentNullException` on a null element and
    // `ArgumentOutOfRangeException` on `dimensions <= 0`; the C++ port asserts
    // both (the D383 non-null-ctor-assert precedent).
    ArrayTypeReference(std::shared_ptr<const ITypeReference> elementType,
                       int dimensions = 1);

    // The C# `ITypeReference ElementType { get; }` -- the wrapped element
    // reference (non-null).
    const std::shared_ptr<const ITypeReference>& ElementType() const noexcept
    {
        return elementType_;
    }

    // The C# `int Dimensions { get; }` -- the array rank (positive).
    int Dimensions() const noexcept { return dimensions_; }

    // The C# `IType Resolve(ITypeResolveContext context) => new
    // ArrayType(context.Compilation, elementType.Resolve(context), dimensions)`
    // -- resolves this reference to an `ArrayType` of rank `dimensions_` wrapping
    // the element's resolved `IType`. Never null (a non-null reference return,
    // the D408 precedent); cached on first call. The compilation argument the C#
    // passes is dropped (the minimal-port `ArrayType` does not store it).
    const IType& Resolve(const ITypeResolveContext& context) const override;

    // The C# `int ISupportsInterning.GetHashCodeForInterning() =>
    // elementType.GetHashCode() ^ dimensions` -- the identity hash of the
    // element reference XORed with the dimensions (NOT a fixed salt, the
    // structural distinction from the D413/D414 twins).
    int GetHashCodeForInterning() const override
    {
        return static_cast<int>(
                   std::hash<std::shared_ptr<const ITypeReference>>{}(
                       elementType_))
               ^ dimensions_;
    }

    // The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)
    // => (other as ArrayTypeReference) != null && elementType == o.elementType &&
    // dimensions == o.dimensions` -- downcast via `dynamic_cast` (returns null on
    // a type mismatch), reference-equality on the element (shared_ptr `==` =
    // reference identity), and value-equality on the dimensions.
    bool EqualsForInterning(const ISupportsInterning& other) const override
    {
        const auto* o = dynamic_cast<const ArrayTypeReference*>(&other);
        return o != nullptr && elementType_ == o->elementType_
               && dimensions_ == o->dimensions_;
    }

    // Out-of-line destructor: the `resolved_` member holds an incomplete
    // `ArrayType` (forward-declared above), so the destructor is defined in the
    // `.cpp` where `ArrayType` is complete.
    ~ArrayTypeReference() override;

private:
    std::shared_ptr<const ITypeReference> elementType_;
    int dimensions_;
    // The resolved `ArrayType`, cached on the first `Resolve` call so the
    // returned `const IType&` outlives the call. `mutable` because `Resolve` is
    // `const` (it reads the element reference without mutating, the D408
    // const-`Resolve` precedent) but lazily constructs its result.
    mutable std::shared_ptr<ArrayType> resolved_;
};

} // namespace ILSpy::Decompiler::TypeSystem
