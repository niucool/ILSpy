// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/MetadataTypeParameter.cs
// -- the `sealed class MetadataTypeParameter : AbstractTypeParameter`, the
// GenericParam-table-backed type parameter the MetadataTypeDefinition /
// MetadataMethod ctors create. This slice lands the EAGER surface (the three
// `Create` factories the MetadataTypeDefinition ctor drives, the private ctor
// over the raw row, the variance mapping, and the raw-flag constraint
// predicates) plus `HasUnmanagedConstraint` (the option-gated
// `HasKnownAttribute` scan the iteration-65 SRMExtensions predicate family
// backs) and the `Equals` / `GetHashCode` / `ToString` trio.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `ITypeParameter[] Create(...)` factories return ARRAYS the
//      caller (the type/method definition) holds; the port's
//      `std::vector<std::shared_ptr<const ITypeParameter>>` is the owning
//      shape -- the freshly created parameters are OWNED by the returned
//      vector (the C# array is a GC root), and the copy-from-outer arm's
//      entries are NON-OWNING aliases (a no-op-deleter `shared_ptr` over the
//      outer definition's parameter -- the outer type definition, held by
//      the module's entity cache, owns it; the KnownTypeCache convention
//      (d)). The `handles` collection the C# passes is the port's row
//      vector (`MetadataFile::GetGenericParameters` -- `GenericParameterInfo`
//      carries the row's own token, Number, raw Flags, and Name, the fields
//      the C# reads from each row inside the single-handle factory).
//  (b) The C# `copyFromOuter.TypeParameters` deref NREs on a null outer --
//      the port maps the null to the standard NRE message, AFTER the
//      `handles.Count == 0` early exit (the C# returns the empty array
//      before the deref, so an empty generic-parameter list never needs the
//      outer).
//  (c) `HasUnmanagedConstraint` keeps the C#'s `ThreeState` byte caching
//      (False=0 / True=1 / Unknown=2, computed once on first read). The
//      option gate runs FIRST (the C# returns false without scanning when
//      `UnmanagedConstraints` is off), then the `HasKnownAttribute` scan
//      over the GenericParam row's own CustomAttribute rows.
//  (d) DEFERRED from the C# file (each a loud `std::logic_error` naming the
//      gating machinery): `GetAttributes` (the `AttributeListBuilder` +
//      custom-attribute value decoder), `NullabilityConstraint` (the
//      `ShouldDecodeNullableAttributes` / `[Nullable]` byte decode -- the
//      module's `minAccessibilityForNRT` computation and the
//      CustomAttributeDecoder), and `TypeConstraints` (`module.ResolveType`
//      -- the `TypeProvider` itself LANDED with the
//      signature-provider slice, so the gate is now the `ResolveType` +
//      `ApplyAttributeTypeVisitor` composition). The C#'s `DirectBaseTypes` /
//      `EffectiveBaseClass` / `EffectiveInterfaceSet` inherited machinery
//      reads `TypeConstraints`, so those inherit the deferral through the
//      base class unchanged.

#pragma once

#include "Decompiler/TypeSystem/Implementation/AbstractTypeParameter.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Forward declarations: the module (MetadataModule.hpp -- a reference member
// is complete with the pointee incomplete; the .cpp includes the real header)
// and the GenericParam row struct (MetadataFile.hpp -- the `Create` factories
// take the row vector by const reference, and `std::vector<Incomplete>` is
// valid in a declaration; the .cpp includes the full header).
namespace ILSpy::Decompiler::TypeSystem { class MetadataModule; }
namespace ILSpy::Decompiler::Metadata { struct GenericParameterInfo; }

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The GenericParam-table-backed type parameter (see the header comment).
class MetadataTypeParameter final : public AbstractTypeParameter {
public:
    // The C# `public static ITypeParameter[] Create(MetadataModule module,
    // ITypeDefinition copyFromOuter, IEntity owner,
    // GenericParameterHandleCollection handles)` -- the NESTED-TYPE factory:
    // the first `copyFromOuter.TypeParameters.Count` slots alias the outer
    // type's parameters (a nested type's generic parameters continue the
    // outer's numbering), the rest are freshly created from the type's own
    // GenericParam rows.
    static std::vector<std::shared_ptr<const ITypeParameter>> Create(
        const MetadataModule& module,
        const ITypeDefinition* copyFromOuter,
        const IEntity* owner,
        const std::vector<Metadata::GenericParameterInfo>& genericParameters);

    // The C# `public static ITypeParameter[] Create(MetadataModule module,
    // IEntity owner, GenericParameterHandleCollection handles)` -- the
    // top-level-type / method factory (no outer copy).
    static std::vector<std::shared_ptr<const ITypeParameter>> Create(
        const MetadataModule& module,
        const IEntity* owner,
        const std::vector<Metadata::GenericParameterInfo>& genericParameters);

    // The C# `public static MetadataTypeParameter Create(MetadataModule
    // module, IEntity owner, int index, GenericParameterHandle handle)` --
    // the single-row factory (public in the C#; the Debug.Assert on
    // `gp.Index == index` is compiled out of the release assembly).
    static std::shared_ptr<const ITypeParameter> Create(
        const MetadataModule& module, const IEntity* owner, int index,
        const Metadata::GenericParameterInfo& genericParameter);

    // The C# `public GenericParameterHandle MetadataToken => handle;` -- the
    // raw 0x2A...... token (a plain member; the port's ITypeParameter surface
    // has no virtual MetadataToken).
    std::uint32_t MetadataToken() const { return handle_; }

    // The C# `public override int GetHashCode() => 0x51fc5b83 ^
    // module.MetadataFile.GetHashCode() ^ handle.GetHashCode();` -- a plain
    // member (the port has no `object.GetHashCode` virtual; the
    // AbstractTypeParameter plain-member precedent). The C# object hashes are
    // identity hashes; the port hashes the MetadataFile pointer (the module
    // owns one file per module) and the raw token.
    int GetHashCode() const;

    // The C# `public override string ToString() => $"
    // {MetadataTokens.GetToken(handle):X8} {ReflectionName}"` -- the raw token
    // in 8-digit uppercase hex plus the `!N` / `!!N` reflection name.
    std::string ToString() const;

    // --- ITypeParameter ---

    // DEFERRED (convention (d)): the AttributeListBuilder attribute snapshot.
    std::vector<const IAttribute*> GetAttributes() const override;

    // The C# raw-flag predicates over the GenericParam Attributes column
    // (ECMA II.23.1.7: DefaultConstructorConstraint 0x10,
    // ReferenceTypeConstraint 0x4, NotNullableValueTypeConstraint 0x8,
    // AllowByRefLike 0x20 -- the raw bits the SRM GenericParameterAttributes
    // enum carries verbatim).
    bool HasDefaultConstructorConstraint() const override {
        return (attr_ & 0x10u) != 0;
    }
    bool HasReferenceTypeConstraint() const override {
        return (attr_ & 0x04u) != 0;
    }
    bool HasValueTypeConstraint() const override {
        return (attr_ & 0x08u) != 0;
    }
    bool AllowsRefLikeType() const override {
        return (attr_ & 0x20u) != 0;
    }

    // The C# lazy-cached option-gated `IsUnmanaged` attribute scan
    // (convention (c)). Out-of-line (reads the module's options and the
    // MetadataFile surface).
    bool HasUnmanagedConstraint() const override;

    // DEFERRED (convention (d)): the [Nullable] byte decode behind
    // `ShouldDecodeNullableAttributes`.
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint()
        const override;

    // DEFERRED (convention (d)): the `module.ResolveType` constraint decode.
    std::vector<TypeConstraint> TypeConstraints() const override;

protected:
    // The C# `public override bool Equals(object obj)`: `obj is
    // MetadataTypeParameter tp && handle == tp.handle && module.MetadataFile
    // == tp.module.MetadataFile`. The port's `IType::Equals` dispatches to
    // this protected `StructuralEquals` (the AbstractTypeParameter
    // reference-equality override replaced).
    bool StructuralEquals(const IType& other) const override;

private:
    // The C# `private MetadataTypeParameter(MetadataModule module, IEntity
    // owner, int index, string name, GenericParameterHandle handle,
    // GenericParameterAttributes attr) : base(owner, index, name,
    // GetVariance(attr))`.
    MetadataTypeParameter(const MetadataModule& module, const IEntity* owner,
                          int index, const std::string& name,
                          std::uint32_t handle, std::uint16_t attr);

    // The C# `private static VarianceModifier GetVariance(
    // GenericParameterAttributes attr)` -- the VarianceMask (0x3) switch.
    static VarianceModifier GetVariance(std::uint16_t attr);

    const MetadataModule& module_;
    std::uint32_t handle_;  // the raw 0x2A...... token
    std::uint16_t attr_;    // the raw GenericParam Attributes column

    // The C# `byte unmanagedConstraint = ThreeState.Unknown;` (False=0 /
    // True=1 / Unknown=2; convention (c)).
    mutable std::uint8_t unmanagedConstraint_ = 2;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
