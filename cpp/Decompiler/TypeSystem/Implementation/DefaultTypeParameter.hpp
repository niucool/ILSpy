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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/DefaultTypeParameter.cs
// -- the `public class DefaultTypeParameter : AbstractTypeParameter`, the
// caller-configured type parameter the `MetadataModule.CreateFakeMethod` factory
// creates for a generic member-reference method (`new DefaultTypeParameter(m,
// i)`), and the type-parameter shape every synthesized/fake member composes.
// The two C# constructors (the IEntity-owner form and the
// compilation+ownerType form) port with their default arguments; the
// constraint-flags / attribute-list / constraint-list fields seed the eager
// surface, and `MakeConstraints` computes the `TypeConstraints` list exactly
// as the C# does (the explicit constraints, then the ValueType-or-Object
// fallback constraint).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IReadOnlyList<IAttribute> attributes = null` -- the
//      `attributes ?? EmptyList<IAttribute>.Instance` default maps to the
//      EMPTY vector (the empty-list-is-null equivalence: `GetAttributes`
//      never exposes the distinction). The C# `IReadOnlyList<IType>
//      constraints = null` similarly maps to the empty vector (MakeConstraints
//      only distinguishes null by skipping the loop -- an empty list behaves
//      identically).
//  (b) The C# `string name = null` maps to the empty string (the
//      `AbstractTypeParameter` empty-name sentinel -- the base ctor derives
//      the `!`/`!!` default form). The `VarianceModifier variance` default is
//      `Invariant`.
//  (c) `MakeConstraints` is a private instance member (it reads
//      `HasValueTypeConstraint`); the `TypeConstraints` property is set once
//      in the C# ctor -- the port stores the computed list in a member and
//      the `TypeConstraints()` override returns it (a pure-virtual cannot be
//      assigned in a C++ ctor body).
//  (d) `HasUnmanagedConstraint` is `false` and `AllowsRefLikeType` is `false`
//      -- the C# overrides return the constants (no attribute scan, unlike
//      `MetadataTypeParameter`).

#pragma once

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/AbstractTypeParameter.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class IEntity;
class ICompilation;

namespace Implementation {

// The caller-configured type parameter (see the header comment). The
// constraint/attribute surface is eager (set at construction); everything
// else derives from the `AbstractTypeParameter` base.
class DefaultTypeParameter final : public AbstractTypeParameter {
public:
    // The C# `DefaultTypeParameter(IEntity owner, int index, string name =
    // null, VarianceModifier variance = VarianceModifier.Invariant,
    // IReadOnlyList<IAttribute> attributes = null, bool
    // hasValueTypeConstraint = false, bool hasReferenceTypeConstraint =
    // false, bool hasDefaultConstructorConstraint = false,
    // IReadOnlyList<IType> constraints = null, Nullability
    // nullabilityConstraint = Nullability.Oblivious)`. The `constraints` list
    // is taken by reference and copied into `MakeConstraints` (the C#
    // enumerable is lazily consumed once, in the ctor).
    DefaultTypeParameter(
        const IEntity* owner, int index, const std::string& name = "",
        VarianceModifier variance = VarianceModifier::Invariant,
        const std::vector<const IAttribute*>& attributes = {},
        bool hasValueTypeConstraint = false,
        bool hasReferenceTypeConstraint = false,
        bool hasDefaultConstructorConstraint = false,
        const std::vector<ITypePtr>& constraints = {},
        ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint
            = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious);

    // The C# `DefaultTypeParameter(ICompilation compilation, SymbolKind
    // ownerType, int index, ...)` -- the compilation+ownerType form (a null
    // owner, e.g. the free-standing type parameter a C# syntax API builds).
    // GLOBALLY QUALIFIED (the D372 crux: the `SymbolKind()` member inherited
    // from `ISymbol` hides the namespace-scope enum inside the class body).
    DefaultTypeParameter(
        const ICompilation& compilation,
        ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
        int index, const std::string& name = "",
        VarianceModifier variance = VarianceModifier::Invariant,
        const std::vector<const IAttribute*>& attributes = {},
        bool hasValueTypeConstraint = false,
        bool hasReferenceTypeConstraint = false,
        bool hasDefaultConstructorConstraint = false,
        const std::vector<ITypePtr>& constraints = {},
        ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint
            = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious);

    // The C# `public override IEnumerable<IAttribute> GetAttributes()` -- the
    // construction-time snapshot (a non-owning view; the caller of the ctor
    // owns the IAttribute objects).
    std::vector<const IAttribute*> GetAttributes() const override {
        return attributes_;
    }

    // The eager constraint flags (the C# overrides over the readonly
    // fields).
    bool HasValueTypeConstraint() const override {
        return hasValueTypeConstraint_;
    }
    bool HasReferenceTypeConstraint() const override {
        return hasReferenceTypeConstraint_;
    }
    bool HasDefaultConstructorConstraint() const override {
        return hasDefaultConstructorConstraint_;
    }
    // The C# `public override bool HasUnmanagedConstraint => false;`.
    bool HasUnmanagedConstraint() const override { return false; }
    // The C# `public override bool AllowsRefLikeType => false;`.
    bool AllowsRefLikeType() const override { return false; }
    // The C# `public override Nullability NullabilityConstraint`.
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint()
        const override {
        return nullabilityConstraint_;
    }

    // The C# `public override IReadOnlyList<TypeConstraint>
    // TypeConstraints { get; }` -- the ctor-computed `MakeConstraints` list.
    std::vector<TypeConstraint> TypeConstraints() const override {
        return typeConstraints_;
    }

private:
    bool hasValueTypeConstraint_;
    bool hasReferenceTypeConstraint_;
    bool hasDefaultConstructorConstraint_;
    ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint_;
    std::vector<const IAttribute*> attributes_;
    std::vector<TypeConstraint> typeConstraints_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
} // namespace ILSpy::Decompiler::TypeSystem

