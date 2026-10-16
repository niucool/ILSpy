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

// Port of the `NestedTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/Implementation/NestedTypeReference.cs (the
// `sealed class NestedTypeReference : ITypeReference, ISupportsInterning`). A
// `NestedTypeReference` is a reference to a nested type identified by its
// declaring-type reference, the nested name, and the additional type-parameter
// count (the inner class's own type parameters, without the declaring type's).
// When resolved against an `ITypeResolveContext`, it resolves the declaring-type
// reference, downcasts to `ITypeDefinition`, and searches `NestedTypes` for a
// nested type whose `Name` matches and whose `TypeParameterCount` equals the
// declaring type's `TypeParameterCount` plus the `additionalTypeParameterCount`.
// If no match is found (or the declaring type is not an `ITypeDefinition`), it
// falls back to an `UnknownType(null, name, additionalTypeParameterCount)` -- an
// unknown type with the known name but unknown namespace.
//
// It is the sixth concrete `ITypeReference` (after `KnownTypeReference` D411,
// `ByReferenceTypeReference` D413, `PointerTypeReference` D414,
// `ArrayTypeReference` D415, `ParameterizedTypeReference` D416) and the fifth to
// derive from both `ITypeReference` (D408) and `ISupportsInterning` (D412). It is
// a leaf TypeSystem dependency toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker of `CSharpAmbience`); the `InterningProvider`
// (not yet ported) deduplicates structurally-equal references via the
// `ISupportsInterning` accessors.
//
// KEY PORT CONVENTIONS:
// (a) The C# `sealed class NestedTypeReference : ITypeReference,
// ISupportsInterning` ports to a C++ `final` class multiply-inheriting
// `ITypeReference` (D408) and `ISupportsInterning` (D412). The two bases share no
// common base, so there is NO diamond and NO `Name`/`SymbolKind` redeclaration --
// the cleanest multiple-inheritance shape (the D415 precedent).
// (b) The C# `readonly ITypeReference declaringTypeRef` (non-null per the
// `ArgumentNullException` ctor guard) ports to a
// `std::shared_ptr<const ITypeReference>` member (the `ITypePtr` convention
// extended to `ITypeReference`, the D413 precedent). The ctor asserts non-null
// (the C# `ArgumentNullException` port, the D383 non-null-ctor-assert precedent).
// (c) The C# `readonly string name` (non-null per the `ArgumentNullException` ctor
// guard) ports to a `std::string` member; the ctor asserts non-empty (a faithful
// mirror -- the C# `name == null` throw has no C++ `std::string` null state, so
// the empty string stands in as the impossible sentinel, the D383 convention).
// (d) The C# `readonly bool? isReferenceType` ports to `std::optional<bool>`
// (nullopt = the C# null, the faithful nullable-value-type representation). It
// participates in `EqualsForInterning` (the C# `isReferenceType == o.isReferenceType`
// comparison).
// (e) The C# `IType Resolve(ITypeResolveContext context)` resolves the declaring
// type reference, downcasts to `ITypeDefinition` (the C# `as ITypeDefinition`,
// ports to `dynamic_cast`), and searches `NestedTypes` for a matching name + total
// `TypeParameterCount`. If found, the matched `IType` (an `ITypeDefinition` IS-A
// `IType`) is returned -- the matched type is owned by the declaring type
// definition, so the returned `const IType&` is valid without caching. If NOT
// found (or the declaring type is not an `ITypeDefinition`), the fallback
// `new UnknownType(null, name, additionalTypeParameterCount)` is cached as a
// `mutable std::shared_ptr<const IType>` member (the D416 polymorphic-cache
// precedent -- the result is either the declaring-type-owned match OR a
// newly-constructed `UnknownType`, so the cache is `shared_ptr<const IType>`).
// `IType`, `ITypeResolveContext`, `ITypeDefinition`, and `UnknownType` are
// forward-declared and the destructor is out-of-line (the `resolved_` member
// holds an incomplete `IType`); `Resolve` is out-of-line (needs `IType` +
// `ITypeResolveContext` + `ITypeDefinition` + `UnknownType` complete).
// (f) The C# `int ISupportsInterning.GetHashCodeForInterning() =>
// declaringTypeRef.GetHashCode() ^ name.GetHashCode() ^ additionalTypeParameterCount`
// uses the IDENTITY hash of the reference-type `declaringTypeRef` XORed with the
// `string.GetHashCode()` of the name and the `additionalTypeParameterCount`. The
// C++ port XORs the shared_ptr identity hash with `std::hash<std::string>{}(name_)`
// and `additionalTypeParameterCount_` (`static_cast<int>`-ed on the hash to avoid
// C4267, the D415 precedent). The `isReferenceType` is NOT in the C# hash and is
// NOT in the C++ hash (faithful).
// (g) The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)`
// downcasts via `as`, checks `declaringTypeRef == o.declaringTypeRef` (reference
// equality), `name == o.name`, `additionalTypeParameterCount ==
// o.additionalTypeParameterCount`, and `isReferenceType == o.isReferenceType`. The
// C++ port downcasts via `dynamic_cast` (the C# `as`-returns-null case, the D412
// precedent) and mirrors all four comparisons (reference equality on the declaring
// type, value equality on the name/count, optional equality on `isReferenceType`).
// (h) The C# `override string ToString()` is DEFERRED for the same reason as the
// D413-D416 references: `ITypeReference` (the C# interface) does not declare
// `ToString` (it inherits `Object.ToString`), so the C++ `ITypeReference`
// interface has no polymorphic `ToString` for the declaring type, and adding one
// is an interface extension outside this leaf's scope.

#pragma once

#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IType` (the D271 minimal port, the `Resolve` return
// type), `ITypeResolveContext` (the D409 paired context interface, the `Resolve`
// parameter type), `ITypeDefinition` (the D393 resolved type-definition interface
// the `dynamic_cast` targets), and `UnknownType` (the minimal-port IType the
// fallback constructs). `IType` is incomplete here -- the `resolved_` member's
// `shared_ptr<const IType>` deleter is type-erased, so the member can hold an
// incomplete type, and the destructor is out-of-line.
class IType;
class ITypeResolveContext;
class ITypeDefinition;
class UnknownType;

// A reference to a nested type identified by its declaring-type reference, the
// nested name, and the additional type-parameter count. The C# `sealed class`
// counterpart; it is `final` here, deriving from both `ITypeReference` (the
// resolve contract) and `ISupportsInterning` (the interning-deduplication
// contract).
class NestedTypeReference final : public ITypeReference,
                                  public ISupportsInterning {
public:
    // The C# `NestedTypeReference(ITypeReference declaringTypeRef, string name,
    // int additionalTypeParameterCount, bool? isReferenceType = null)` -- the
    // non-null declaring-type reference, the non-null nested name, the additional
    // type-parameter count, and the optional `isReferenceType` hint. The C# ctor
    // throws `ArgumentNullException` on a null declaring type or name; the C++ port
    // asserts both (the D383 non-null-ctor-assert precedent -- the empty string
    // stands in for the impossible C# null `std::string`).
    NestedTypeReference(std::shared_ptr<const ITypeReference> declaringTypeRef,
                       std::string name,
                       int additionalTypeParameterCount,
                       std::optional<bool> isReferenceType = std::nullopt);

    // The C# `ITypeReference DeclaringTypeReference { get; }` -- the wrapped
    // declaring-type reference (non-null).
    const std::shared_ptr<const ITypeReference>& DeclaringTypeReference() const noexcept
    {
        return declaringTypeRef_;
    }

    // The C# `string Name { get; }` -- the nested type's name (non-null).
    const std::string& Name() const noexcept { return name_; }

    // The C# `int AdditionalTypeParameterCount { get; }` -- the inner class's own
    // type-parameter count (without the declaring type's).
    int AdditionalTypeParameterCount() const noexcept
    {
        return additionalTypeParameterCount_;
    }

    // The C# `IType Resolve(ITypeResolveContext context)` -- resolves this
    // reference: resolves the declaring type, downcasts to `ITypeDefinition`, and
    // searches `NestedTypes` for a nested type whose `Name` matches and whose
    // `TypeParameterCount` equals the declaring type's `TypeParameterCount` plus
    // `additionalTypeParameterCount_`. Returns the match (owned by the declaring
    // type definition) or falls back to an `UnknownType(null, name,
    // additionalTypeParameterCount)`. Never null (a non-null reference return, the
    // D408 precedent); the fallback is cached on first call.
    const IType& Resolve(const ITypeResolveContext& context) const override;

    // The C# `int ISupportsInterning.GetHashCodeForInterning() =>
    // declaringTypeRef.GetHashCode() ^ name.GetHashCode() ^
    // additionalTypeParameterCount` -- the identity hash of the declaring-type
    // reference XORed with the string hash of the name and the
    // additionalTypeParameterCount. The `isReferenceType` is NOT in the C# hash
    // (faithful).
    int GetHashCodeForInterning() const override
    {
        return static_cast<int>(
                   std::hash<std::shared_ptr<const ITypeReference>>{}(
                       declaringTypeRef_))
               ^ static_cast<int>(std::hash<std::string>{}(name_))
               ^ additionalTypeParameterCount_;
    }

    // The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)`
    // -- downcast via `dynamic_cast` (returns null on a type mismatch),
    // reference-equality on the declaring type (shared_ptr `==` = reference
    // identity), value-equality on the name and the additionalTypeParameterCount,
    // and optional-equality on `isReferenceType` (the C# `bool? ==`).
    bool EqualsForInterning(const ISupportsInterning& other) const override
    {
        const auto* o = dynamic_cast<const NestedTypeReference*>(&other);
        return o != nullptr
               && declaringTypeRef_ == o->declaringTypeRef_
               && name_ == o->name_
               && additionalTypeParameterCount_ == o->additionalTypeParameterCount_
               && isReferenceType_ == o->isReferenceType_;
    }

    // Out-of-line destructor: the `resolved_` member holds an incomplete `IType`
    // (forward-declared above), so the destructor is defined in the `.cpp` where
    // `IType` is complete.
    ~NestedTypeReference() override;

private:
    std::shared_ptr<const ITypeReference> declaringTypeRef_;
    std::string name_;
    int additionalTypeParameterCount_;
    std::optional<bool> isReferenceType_;
    // The resolved `IType` (the `UnknownType` fallback), cached on the first
    // `Resolve` call so the returned `const IType&` outlives the call. `mutable`
    // because `Resolve` is `const` (the D408 const-`Resolve` precedent) but
    // lazily constructs the fallback. The cache holds the fallback only -- a
    // matched nested type is owned by the declaring type definition and returned
    // by reference without caching; the `shared_ptr<const IType>` (not a
    // monomorphic `shared_ptr<UnknownType>`) is used because the cache may be
    // empty (no fallback yet) and the matched-type path does not populate it.
    mutable std::shared_ptr<const IType> resolved_;
};

} // namespace ILSpy::Decompiler::TypeSystem
