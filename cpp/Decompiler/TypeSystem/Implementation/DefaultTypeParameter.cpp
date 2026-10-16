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
// (see DefaultTypeParameter.hpp for the class contract).

#include "Decompiler/TypeSystem/Implementation/DefaultTypeParameter.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// A local helper shared by the two ctor bodies (the C# chain: the base ctor,
// then the field seeds, then `TypeConstraints = MakeConstraints(constraints)`
// -- two verbatim ctor bodies the port factors into one helper).
static std::vector<TypeConstraint> ComputeConstraints(
    bool hasValueTypeConstraint, const std::vector<ITypePtr>& constraints,
    const ICompilation& compilation);

DefaultTypeParameter::DefaultTypeParameter(
    const IEntity* owner, int index, const std::string& name,
    VarianceModifier variance,
    const std::vector<const IAttribute*>& attributes,
    bool hasValueTypeConstraint, bool hasReferenceTypeConstraint,
    bool hasDefaultConstructorConstraint,
    const std::vector<ITypePtr>& constraints,
    ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint)
    : AbstractTypeParameter(owner, index, name, variance),
      hasValueTypeConstraint_(hasValueTypeConstraint),
      hasReferenceTypeConstraint_(hasReferenceTypeConstraint),
      hasDefaultConstructorConstraint_(hasDefaultConstructorConstraint),
      nullabilityConstraint_(nullabilityConstraint),
      attributes_(attributes)
{
    // The C# reads `this.HasValueTypeConstraint` inside MakeConstraints -- the
    // member seed must be complete before the computation runs.
    typeConstraints_ =
        ComputeConstraints(hasValueTypeConstraint_, constraints,
            Compilation());
}

// The C# `DefaultTypeParameter(ICompilation compilation, SymbolKind ownerType,
// int index, ...)` -- the compilation+ownerType form (a null owner, e.g. the
// free-standing type parameter a C# syntax API builds).
DefaultTypeParameter::DefaultTypeParameter(
    const ICompilation& compilation,
    ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
    int index, const std::string& name,
    VarianceModifier variance,
    const std::vector<const IAttribute*>& attributes,
    bool hasValueTypeConstraint, bool hasReferenceTypeConstraint,
    bool hasDefaultConstructorConstraint,
    const std::vector<ITypePtr>& constraints,
    ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint)
    : AbstractTypeParameter(compilation, ownerType, index, name, variance),
      hasValueTypeConstraint_(hasValueTypeConstraint),
      hasReferenceTypeConstraint_(hasReferenceTypeConstraint),
      hasDefaultConstructorConstraint_(hasDefaultConstructorConstraint),
      nullabilityConstraint_(nullabilityConstraint),
      attributes_(attributes)
{
    typeConstraints_ =
        ComputeConstraints(hasValueTypeConstraint_, constraints,
            Compilation());
}

// The C# `IReadOnlyList<TypeConstraint> MakeConstraints(IReadOnlyList<IType>
// constraints)`: each explicit constraint, then -- when the parameter carries
// the value-type constraint or NO explicit non-interface constraint -- the
// ValueType-or-Object fallback constraint.
static std::vector<TypeConstraint> ComputeConstraints(
    bool hasValueTypeConstraint, const std::vector<ITypePtr>& constraints,
    const ICompilation& compilation)
{
    std::vector<TypeConstraint> result;
    bool hasNonInterfaceConstraint = false;
    for (const ITypePtr& c : constraints) {
        if (!c)
            continue;  // the C# NREs on a null element; the port skips it
        result.emplace_back(c);
        if (c->Kind() != TypeKind::Interface)
            hasNonInterfaceConstraint = true;
    }
    // The C# `if (this.HasValueTypeConstraint || !hasNonInterfaceConstraint)`
    // -- the fallback constraint is the ValueType known type for a
    // struct-constrained parameter, Object otherwise.
    if (hasValueTypeConstraint || !hasNonInterfaceConstraint) {
        const IType& fallback = compilation.FindType(
            hasValueTypeConstraint
                ? KnownTypeCode::ValueType
                : KnownTypeCode::Object);
        // The C# passes the FindType result (the compilation's cached
        // known type) by reference; the port's TypeConstraint takes the
        // owned-type handle -- a no-op-deleter alias (the KnownTypeCache
        // owns the known types).
        result.emplace_back(ITypePtr(const_cast<IType*>(&fallback),
            [](IType*) { /* no-op: the compilation owns it */ }));
    }
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation

