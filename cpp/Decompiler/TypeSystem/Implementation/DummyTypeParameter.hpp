// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/DummyTypeParameter.cs --
// the PLACEHOLDER type parameter used when a concrete one is unavailable:
// `NormalizeTypeVisitor` maps method (and optionally class) type parameters to
// `DummyTypeParameter`s by index so two generic signatures can be compared by
// shape (`Method{T}(T)` vs `Method{S}(S)` are equal), and `UnknownType` uses the
// class-parameter list to stand in for its type arguments. A dummy stands for
// exactly one (owner-kind, index) pair -- the static caches guarantee a unique
// instance per pair, so identity comparison is the equality semantics (the C#
// class has no `Equals` override; reference identity suffices because the cache
// is the only source of instances).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `: AbstractType, ITypeParameter` flattens to a single `: ITypeParameter`
//      inheritance (the port has no `AbstractType`; `ITypeParameter : IType` carries
//      the whole type surface) -- one `IType` subobject, so `shared_from_this()` and
//      the `AcceptVisitor` / `ChangeNullability` wrappers work unambiguously.
//  (b) The C# `static ITypeParameter[] methodTypeParameters / classTypeParameters`
//      arrays extended with `Interlocked.CompareExchange` port to function-scope
//      static vectors grown under a mutex (the .cpp) -- the compare-exchange dance
//      is a lock-free publication detail; the observable contract (a stable, unique
//      instance per (owner-kind, index) pair, lazily grown, thread-safe) is kept.
//      The instances live in the cache forever (process lifetime, like the C#
//      static array) and are returned as shared `shared_ptr<ITypeParameter>`s.
//  (c) The C# `private DummyTypeParameter(SymbolKind ownerType, int index)` ctor
//      stays private; the growth path allocates with plain `new` handed straight to
//      a `shared_ptr` (the one place a raw `new` is needed -- `make_shared` cannot
//      call a private ctor, and the allocation is immediately owned).
//  (d) `ChangeNullability` wraps non-Oblivious annotations into the
//      `NullabilityAnnotatedTypeParameter` (the nested class of
//      Implementation/NullabilityAnnotatedType.cs, landed in `ITypeParameter.hpp`).
//  (e) `GetClassTypeParameterList(int)` (the `IReadOnlyList<ITypeParameter>` cache
//      backing `UnknownType.TypeParameters` and `MinimalCorlib`'s
//      `CorlibTypeDefinition.TypeParameters`) IS ported (the first consumer landed with
//      MinimalCorlib); still deferred from the C# file: the `ITypeReference.Resolve` /
//      interner surface.

#pragma once

#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// A placeholder type parameter (see the header comment). All members except the
// accepting/projecting ones are inline; `AcceptVisitor` and `ChangeNullability`
// are out-of-line (they need `TypeVisitor` / `NullabilityAnnotatedTypeParameter`
// contexts the .cpp completes).
class DummyTypeParameter final : public ITypeParameter {
public:
    // The C# `GetMethodTypeParameter(int index)` -- a cached dummy for the method
    // type parameter at `index` (negative indices throw `std::out_of_range`, the
    // C# `IndexOutOfRangeException` arm).
    static std::shared_ptr<ITypeParameter> GetMethodTypeParameter(int index);
    // The C# `GetClassTypeParameter(int index)` -- a cached dummy for the class
    // type parameter at `index`.
    static std::shared_ptr<ITypeParameter> GetClassTypeParameter(int index);
    // The C# `internal static IReadOnlyList<ITypeParameter> GetClassTypeParameterList(
    // int length)` -- the cached list of the first `length` class dummies
    // (`[GetClassTypeParameter(0) .. GetClassTypeParameter(length-1)]`, the empty list
    // for 0), grown lazily entry-by-entry like the per-index caches. The returned
    // snapshot is non-owning (the process-lifetime cache owns the dummies; the same
    // pointers come back for the same `length`).
    static std::vector<const ITypeParameter*> GetClassTypeParameterList(int length);

    // --- IType ---
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    // The C# `override bool? IsReferenceType => null`.
    std::optional<bool> IsReferenceType() const override { return std::nullopt; }
    // The C# `(ownerType == SymbolKind.Method ? "!!" : "!") + index`.
    std::string Name() const override {
        return (ownerType_ == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method ? "!!" : "!") +
               std::to_string(index_);
    }
    // The C# `(ownerType == SymbolKind.Method ? "``" : "`") + index`.
    std::string ReflectionName() const override {
        return (ownerType_ == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method ? "``" : "`") +
               std::to_string(index_);
    }
    int TypeParameterCount() const override { return 0; }
    // The C# `ToString() => ReflectionName + " (dummy)"`. Plain member (C# object
    // method; the port's IType has no virtual ToString).
    std::string ToString() const { return ReflectionName() + " (dummy)"; }
    // The C# `override AcceptVisitor` -> `visitor.VisitTypeParameter(this)`
    // (out-of-line: needs `TypeVisitor` complete).
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    // The C# `override ChangeNullability`: `Oblivious` returns this; any other
    // annotation wraps in a `NullabilityAnnotatedTypeParameter` (out-of-line).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;

    // --- ISymbol ---
    // The return type is qualified: the member name shadows the enum type in
    // MSVC's complete-class lookup (the D372 crux; every `SymbolKind` mention
    // after this declaration is qualified too).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }

    // --- ITypeParameter ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override { return ownerType_; }
    // The C# `ITypeParameter.Owner => null` (a dummy has no owner).
    const IEntity* Owner() const override { return nullptr; }
    int Index() const override { return index_; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    VarianceModifier Variance() const override { return VarianceModifier::Invariant; }
    // The C# `EffectiveBaseClass => SpecialType.UnknownType` (the port's
    // `UnknownType()` factory for that singleton shape).
    ITypePtr EffectiveBaseClass() const override { return UnknownType(); }
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    std::vector<TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    // Reference identity within the per-(owner-kind, index) cache above (the C#
    // has no Equals override -- the cache is the only instance source, so
    // identity comparison is the C# semantics).
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    DummyTypeParameter(::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType, int index)
        : ownerType_(ownerType), index_(index) {}

    // The C# `GetTypeParameter(ref array, symbolKind, index)` -- grows `cache` to
    // `index + 1` entries under the caller's lock and returns `cache[index]`. A
    // static member so it can invoke the private ctor.
    static std::shared_ptr<ITypeParameter> GetTypeParameter(
        std::vector<std::shared_ptr<ITypeParameter>>& cache,
        ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType, int index);

    ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType_;
    int index_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
