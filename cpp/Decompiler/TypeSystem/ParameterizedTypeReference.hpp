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

// Port of the `ParameterizedTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/ParameterizedType.cs (the `sealed class
// ParameterizedTypeReference : ITypeReference, ISupportsInterning` half of that
// file). A `ParameterizedTypeReference` is a reference to a generic type that
// also specifies the type-parameter arguments -- the `List<string>` reference
// shape the signature decoder builds when it reads a `GenericTypeInst` sig. When
// resolved against an `ITypeResolveContext`, it resolves the generic-type
// reference to its `IType`, then builds a `ParameterizedType` of that base type
// with the resolved type arguments (filling any missing arguments past the
// reference's own argument list with `UnknownType`, and returning the bare base
// type when the resolved base type has no type parameters).
//
// It is the fifth concrete `ITypeReference` (after `KnownTypeReference` D411,
// `ByReferenceTypeReference` D413, `PointerTypeReference` D414,
// `ArrayTypeReference` D415) and the fourth to derive from both `ITypeReference`
// (D408) and `ISupportsInterning` (D412). It is a leaf TypeSystem dependency
// toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining
// blocker of `CSharpAmbience`); the `InterningProvider` (not yet ported)
// deduplicates structurally-equal references via the `ISupportsInterning`
// accessors.
//
// KEY PORT CONVENTIONS:
// (a) The C# `sealed class ParameterizedTypeReference : ITypeReference,
// ISupportsInterning` ports to a C++ `final` class multiply-inheriting
// `ITypeReference` (D408) and `ISupportsInterning` (D412). The two bases share no
// common base, so there is NO diamond and NO `Name`/`SymbolKind` redeclaration --
// the cleanest multiple-inheritance shape (the D415 precedent).
// (b) The C# `readonly ITypeReference genericType` (non-null per the
// `ArgumentNullException` ctor guard) ports to a
// `std::shared_ptr<const ITypeReference>` member (the `ITypePtr` convention
// extended to `ITypeReference`, the D413 precedent). The ctor asserts non-null
// (the C# `ArgumentNullException` port, the D383 non-null-ctor-assert precedent).
// (c) The C# `readonly ITypeReference[] typeArguments` (each element non-null per
// the per-index `ArgumentNullException` ctor guard) ports to a
// `std::vector<std::shared_ptr<const ITypeReference>>`; the ctor asserts each
// element is non-null (the C# per-index null-check port).
// (d) The C# `IType Resolve(ITypeResolveContext context)` resolves the generic
// type, reads its `TypeParameterCount`, and: if `tpc == 0` returns the bare base
// type (the `tpc == 0` early-return crux -- a non-generic base type is returned
// unchanged, NOT wrapped); otherwise builds a `resolvedTypes` array of length
// `tpc`, resolving `typeArguments[i]` for `i < typeArguments.Length` and filling
// the remaining slots with `SpecialType.UnknownType` (the missing-argument fill
// crux), then constructs `new ParameterizedType(baseType, resolvedTypes)` (the
// extra arguments past `tpc` are dropped -- `resolvedTypes` is `tpc` long, so the
// loop stops at `tpc`). The C++ port mirrors all three behaviors: the `tpc == 0`
// early return caches the base type directly (the result is polymorphic -- the
// bare base type OR a `ParameterizedType` -- so the cache is a
// `mutable std::shared_ptr<const IType>`, distinct from the D415
// `shared_ptr<ArrayType>` monomorphic cache); the constructed `ParameterizedType`
// shares ownership of the base type and each resolved type argument (the C# GC-
// owned reference wrap), obtained via `shared_from_this()` (the D406
// `enable_shared_from_this<IType>` bridge) + `const_pointer_cast` (the resolved
// `IType` is shared, not copied). `IType`, `ITypeResolveContext`, and
// `ParameterizedType` are forward-declared and the destructor is out-of-line (the
// `resolved_` member holds an incomplete `IType`); `Resolve` is out-of-line (needs
// `ParameterizedType` + `IType` + `ITypeResolveContext` complete).
// (e) The C# `int ISupportsInterning.GetHashCodeForInterning()` uses the IDENTITY
// hash of the reference-type `genericType` seeded, then `hashCode *= 27;
// hashCode += t.GetHashCode()` per type argument (the IDENTITY hash of each
// reference-type `t`). The C++ port seeds with the shared_ptr identity hash of
// `genericType_` (`static_cast<int>`-ed to avoid C4267, the D415 precedent) and
// folds each type argument's identity hash with the `*27 + ` formula.
// (f) The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)`
// downcasts via `as`, checks `genericType == o.genericType` (reference equality)
// AND `typeArguments.Length == o.typeArguments.Length`, then loops
// `typeArguments[i] != o.typeArguments[i]` (reference inequality per element --
// the load-bearing element-wise comparison the D413/D414/D415 single-element
// references do not have). The C++ port downcasts via `dynamic_cast` (the C#
// `as`-returns-null case, the D412 precedent) and mirrors the reference-equality
// generic, the length match, and the per-element reference-inequality loop.
// (g) The C# `override string ToString()` is DEFERRED for the same reason as the
// D413/D414/D415 references: `ITypeReference` (the C# interface) does not declare
// `ToString` (it inherits `Object.ToString`), so the C++ `ITypeReference`
// interface has no polymorphic `ToString` for the element, and adding one is an
// interface extension outside this leaf's scope.

#pragma once

#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IType` (the D271 minimal port, the `Resolve` return
// type), `ITypeResolveContext` (the D409 paired context interface, the `Resolve`
// parameter type), and `ParameterizedType` (the D271 minimal concrete `IType` the
// resolved reference produces when the base type has type parameters). `IType` is
// incomplete here -- the `resolved_` member's `shared_ptr<const IType>` deleter
// is type-erased, so the member can hold an incomplete type, and the destructor is
// out-of-line.
class IType;
class ITypeResolveContext;
class ParameterizedType;

// A reference to a generic type that also specifies the type-parameter arguments
// -- the `List<string>` reference shape. The C# `sealed class` counterpart; it is
// `final` here, deriving from both `ITypeReference` (the resolve contract) and
// `ISupportsInterning` (the interning-deduplication contract).
class ParameterizedTypeReference final : public ITypeReference,
                                         public ISupportsInterning {
public:
    // The C# `ParameterizedTypeReference(ITypeReference genericType,
    // IEnumerable<ITypeReference> typeArguments)` -- the non-null generic-type
    // reference and the non-null-element type-argument references. The C# ctor
    // throws `ArgumentNullException` on a null generic type, a null argument
    // collection, and a null per-index element; the C++ port asserts the generic
    // type and each element are non-null (the D383 non-null-ctor-assert
    // precedent). An empty type-argument vector is allowed (the resolved base
    // type with `tpc > 0` then fills every slot with `UnknownType`).
    ParameterizedTypeReference(std::shared_ptr<const ITypeReference> genericType,
                               std::vector<std::shared_ptr<const ITypeReference>> typeArguments);

    // The C# `ITypeReference GenericType { get; }` -- the wrapped generic-type
    // reference (non-null).
    const std::shared_ptr<const ITypeReference>& GenericType() const noexcept
    {
        return genericType_;
    }

    // The C# `IReadOnlyList<ITypeReference> TypeArguments { get; }` -- the
    // type-argument references (each non-null; possibly empty).
    const std::vector<std::shared_ptr<const ITypeReference>>& TypeArguments() const noexcept
    {
        return typeArguments_;
    }

    // The C# `IType Resolve(ITypeResolveContext context)` -- resolves this
    // reference: resolves the generic type, reads its `TypeParameterCount`, and
    // returns the bare base type when `tpc == 0` (the early-return crux) or
    // constructs a `ParameterizedType` of the base type with the resolved type
    // arguments, filling missing slots with `UnknownType` (the fill crux). Never
    // null (a non-null reference return, the D408 precedent); cached on first
    // call. The result is polymorphic (the bare base type OR a
    // `ParameterizedType`), so the cache is a `shared_ptr<const IType>`.
    const IType& Resolve(const ITypeResolveContext& context) const override;

    // The C# `int ISupportsInterning.GetHashCodeForInterning()` -- the identity
    // hash of the generic-type reference seeded, then `*27 +` each type
    // argument's identity hash (the C# `hashCode *= 27; hashCode +=
    // t.GetHashCode()` formula). The identity hashes are the shared_ptr pointer
    // hashes (`static_cast<int>`-ed to avoid C4267, the D415 precedent).
    int GetHashCodeForInterning() const override
    {
        int hashCode = static_cast<int>(
            std::hash<std::shared_ptr<const ITypeReference>>{}(genericType_));
        for (const auto& t : typeArguments_)
        {
            hashCode *= 27;
            hashCode += static_cast<int>(
                std::hash<std::shared_ptr<const ITypeReference>>{}(t));
        }
        return hashCode;
    }

    // The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)`
    // -- downcast via `dynamic_cast` (returns null on a type mismatch),
    // reference-equality on the generic type (shared_ptr `==` = reference
    // identity), length match on the type-argument vectors, and per-element
    // reference-inequality (shared_ptr `!=` = reference distinctness) -- the
    // load-bearing element-wise comparison the single-element references do not
    // have.
    bool EqualsForInterning(const ISupportsInterning& other) const override
    {
        const auto* o = dynamic_cast<const ParameterizedTypeReference*>(&other);
        if (o != nullptr && genericType_ == o->genericType_
            && typeArguments_.size() == o->typeArguments_.size())
        {
            for (std::size_t i = 0; i < typeArguments_.size(); ++i)
            {
                if (typeArguments_[i] != o->typeArguments_[i])
                    return false;
            }
            return true;
        }
        return false;
    }

    // Out-of-line destructor: the `resolved_` member holds an incomplete `IType`
    // (forward-declared above), so the destructor is defined in the `.cpp` where
    // `IType` is complete.
    ~ParameterizedTypeReference() override;

private:
    std::shared_ptr<const ITypeReference> genericType_;
    std::vector<std::shared_ptr<const ITypeReference>> typeArguments_;
    // The resolved `IType`, cached on the first `Resolve` call so the returned
    // `const IType&` outlives the call. `mutable` because `Resolve` is `const`
    // (the D408 const-`Resolve` precedent) but lazily constructs its result. The
    // result is polymorphic -- the bare base type (`tpc == 0`) OR a constructed
    // `ParameterizedType` (`tpc > 0`) -- so the cache is a `shared_ptr<const
    // IType>`, distinct from the D415 monomorphic `shared_ptr<ArrayType>`.
    mutable std::shared_ptr<const IType> resolved_;
};

} // namespace ILSpy::Decompiler::TypeSystem
