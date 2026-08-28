// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of `ICSharpCode.Decompiler.CSharp.Resolver.CSharpConversions` -- the C# conversion controller
// (~1757 lines, ~20 regions). This header ports the class SKELETON: the constructor (holds the
// `ICompilation`), the `Get(ICompilation)` factory (per-compilation singleton via `CacheManager`), the
// `TypePair` caching key struct, and the `implicitConversionCache`/`explicitConversionCache` fields.
// The conversion methods (`IdentityConversion`/`ImplicitConversion`/`ExplicitConversion`/
// `StandardImplicitConversion`/`BetterConversion`/the ~16 `Is*Conversion`/`*Conversion` helpers) are
// deferred -- they need `NormalizeTypeVisitor.TypeErasure` (unported), `ReflectionHelper.GetTypeCode`
// (unported), and the `Conversion` result type's full surface.
//
// `CSharpConversions` unblocks: the `OverloadResolution` engine steps (`CheckApplicability`'s
// passing-mode+conversion half, `RunTypeInference`, `BetterFunctionMember`), `LambdaResolveResult.IsValid`,
// and the `MemberLookup`/`CSharpResolver` conversion queries. This skeleton (D512) gives those callers
// a forward-declared-but-now-defined `CSharpConversions` to reference (the ctor's nullable-pointer slot
// in `OverloadResolution` can now be a real instance).
//
// Field ownership (the C# is a `sealed class` with GC-owned reference fields):
//  * `compilation` -> non-owning `const ICompilation*` (the caller owns the compilation, which outlives
//    the conversions instance -- it is cached ON the compilation's `CacheManager`).
//  * `implicitConversionCache`/`explicitConversionCache` -> owning `std::unordered_map<TypePair,
//    std::shared_ptr<Conversion>>` (the C# `ConcurrentDictionary<TypePair, Conversion>`). The cache is
//    populated by the (deferred) conversion methods; the skeleton declares it empty. The value is an
//    owning `shared_ptr<Conversion>` because `Conversion` is polymorphic (the C# `Conversion` is a class
//    hierarchy; a plain `Conversion` value would slice). `Conversion` is forward-declared (unported).
//
// `TypePair` hashing: the C# `TypePair.GetHashCode` uses `IType.GetHashCode` (structural). The port's
// `IType` deliberately defers `GetHashCode` (a Phase-2 leaf), so `TypePair` hashes via `IType::Kind()` +
// `IType::ReflectionName()` + `IType::TypeParameterCount()` -- deterministic `IType` accessors present on
// every subtype, hash-consistent with `IType::Equals` (which is `Kind() == other.Kind() &&
// StructuralEquals`, and `StructuralEquals` compares the same core fields that `ReflectionName`
// encodes). This is a documented simplification: a hash that collides more than `IType.GetHashCode` would
// (perf only, never correctness -- `Equals` is the authority). It upgrades to a structural hash when
// `IType::GetHashCode` lands.

#pragma once

#include "Decompiler/Semantics/Conversion.hpp"  // Conversion (forward-declared; unported full surface)
#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (compilation) + CacheManager (Get factory)
#include "Decompiler/TypeSystem/IType.hpp"  // IType (TypePair)

#include <cstddef>
#include <functional>  // std::hash
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `struct TypePair : IEquatable<TypePair>` -- the conversion-cache key. A value type holding two
// non-owning `const IType*` (the C# `IType` references; the cached conversions outlive the key -- the
// types are owned by the compilation/type-system, not by the `TypePair`).
struct TypePair {
    const ILSpy::Decompiler::TypeSystem::IType* FromType = nullptr;
    const ILSpy::Decompiler::TypeSystem::IType* ToType = nullptr;

    TypePair() = default;
    TypePair(const ILSpy::Decompiler::TypeSystem::IType* fromType,
             const ILSpy::Decompiler::TypeSystem::IType* toType)
        : FromType(fromType), ToType(toType) {}

    // The C# `bool Equals(TypePair other)` -- structural via `object.Equals(FromType, other.FromType) &&
    // object.Equals(ToType, other.ToType)` (the `IType` `Equals` override). The port uses `IType::Equals`
    // (the structural `Kind() == other.Kind() && StructuralEquals`).
    bool Equals(const TypePair& other) const {
        if (FromType == other.FromType && ToType == other.ToType) return true;  // fast pointer-identity
        if (FromType == nullptr || ToType == nullptr || other.FromType == nullptr || other.ToType == nullptr)
            return false;
        return FromType->Equals(*other.FromType) && ToType->Equals(*other.ToType);
    }

    // The C# `int GetHashCode()` -- `1000000007 * FromType.GetHashCode() + 1000000009 * ToType.GetHashCode()`.
    // The port's `IType` has no `GetHashCode` (deferred), so this combines `Kind()` + `ReflectionName()`
    // + `TypeParameterCount()` of each type via the same prime multipliers. See the header note.
    std::size_t GetHashCode() const {
        using ILSpy::Decompiler::TypeSystem::IType;
        auto typeHash = [](const IType* t) -> std::size_t {
            if (t == nullptr) return 0;
            std::size_t h = std::hash<int>{}(static_cast<int>(t->Kind()));
            h ^= std::hash<std::string>{}(t->ReflectionName()) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<int>{}(t->TypeParameterCount()) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        };
        return 1000000007u * typeHash(FromType) + 1000000009u * typeHash(ToType);
    }
};

// `std::hash<TypePair>` so `TypePair` keys an `unordered_map` (the conversion cache).
struct TypePairHash {
    std::size_t operator()(const TypePair& p) const noexcept { return p.GetHashCode(); }
};

// `TypePair` equality predicate for `unordered_map` (delegates to `TypePair::Equals`).
struct TypePairEq {
    bool operator()(const TypePair& a, const TypePair& b) const noexcept { return a.Equals(b); }
};

class CSharpConversions {
public:
    // The C# `public CSharpConversions(ICompilation compilation)` -- holds the compilation. The C#
    // `ArgumentNullException` on null `compilation` is compiled out (a `const` reference cannot bind to
    // null, the D374 reference-not-null convention).
    explicit CSharpConversions(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(&compilation) {}

    // The C# `public static CSharpConversions Get(ICompilation compilation)` -- the per-compilation
    // singleton, cached on the compilation's `CacheManager`. The C# `ArgumentNullException` on null
    // `compilation` is compiled out (const reference). The cache key is `typeid(CSharpConversions)`
    // (the C# `typeof(CSharpConversions)`); the port uses the address of a local static as a stable
    // `const void*` key (the `CacheManager` keys on `const void*`).
    static CSharpConversions& Get(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation);

    // The C# `public ICompilation Compilation` (not public in C#, but `Get` and the conversion methods
    // read it; exposed for the deferred engine steps).
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const { return *compilation_; }

    // The C# `public Conversion StandardImplicitConversion(IType fromType, IType toType)`
    // (CSharpConversions.cs line 201, C# 9.0 spec section 10.4.2) -- the standard implicit conversion
    // dispatch entry point. Delegates to `Detail::StandardImplicitConversion(*compilation_, ...)`.
    // The C# `ArgumentNullException` on null args compiles out (the `IType&` references cannot bind to
    // null, the D374 convention). The C# calls the private `StandardImplicitConversion(fromType,
    // toType, allowTupleConversion: true)` overload; the port collapses the overload into the Detail
    // function (the tuple arm is deferred, so `allowTupleConversion` is effectively always true for
    // the ported arms).
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
    StandardImplicitConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                              ILSpy::Decompiler::TypeSystem::IType& toType);

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation* compilation_;

    // The C# `readonly ConcurrentDictionary<TypePair, Conversion> implicitConversionCache` /
    // `explicitConversionCache`. Populated by the (deferred) `ImplicitConversion`/`ExplicitConversion`
    // methods. The skeleton declares them empty (no conversion method writes them yet). The value is an
    // owning `shared_ptr<Conversion>` (`Conversion` is polymorphic).
    std::unordered_map<TypePair, std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>,
                        TypePairHash, TypePairEq> implicitConversionCache_;
    std::unordered_map<TypePair, std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>,
                        TypePairHash, TypePairEq> explicitConversionCache_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
