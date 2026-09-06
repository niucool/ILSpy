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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The out-of-line FakeMember members (see FakeMember.hpp): the IType-surface
// reads the FullName/ReflectionName/Namespace overrides route through, the
// Specialize short-circuits + deferral arms, the per-leaf parameter snapshots,
// and the CreateDummyConstructor factory.

#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The C# `AbstractType.Namespace` for any `IType` (the port's minimal `IType`
// surface carries neither `Namespace` nor `FullName` -- the ILAmbience.cpp
// file-local helper copied next to this its third consumer, convention (f)):
// an entity reads its own namespace, a parameterized type its generic's, an
// `UnknownType` its stored full-name namespace; everything else has the
// `AbstractType` empty default.
std::string NamespaceOf(const IType& type)
{
    if (const auto* entity = dynamic_cast<const IEntity*>(&type))
        return entity->Namespace();
    if (const auto* pt = dynamic_cast<const ParameterizedType*>(&type))
        return pt->GenericType() ? NamespaceOf(*pt->GenericType())
                                 : std::string();
    if (const auto* unknown
        = dynamic_cast<const class UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    return std::string();
}

// The C# `AbstractType.FullName` (`Namespace.IsNullOrEmpty() ? Name :
// Namespace + "." + Name`).
std::string FullNameOf(const IType& type)
{
    const std::string ns = NamespaceOf(type);
    if (ns.empty())
        return type.Name();
    return ns + "." + type.Name();
}

// The C# `DeclaringType.TypeParameterCount` read the `SpecializedX.Create`
// short-circuits perform -- the `NullReferenceException` of the null
// `DeclaringType` (gold-pinned: `D: ffNoDecl specialize EX=
// NullReferenceException`) maps to `std::runtime_error` carrying the .NET
// message (convention (d)).
int DeclaringTypeParameterCountOf(const FakeMember& member)
{
    ITypePtr declaringType = member.DeclaringType();
    if (!declaringType) {
        throw std::runtime_error(
            "Object reference not set to an instance of an object.");
    }
    return declaringType->TypeParameterCount();
}

}  // namespace

// --- FakeMember ---

// The C# `TypeParameterSubstitution IMember.Substitution =>
// TypeParameterSubstitution.Identity`.
const TypeParameterSubstitution* FakeMember::Substitution() const
{
    return &TypeParameterSubstitution::Identity();
}

// The C# `ITypeDefinition IEntity.DeclaringTypeDefinition =>
// DeclaringType?.GetDefinition()`.
const ITypeDefinition* FakeMember::DeclaringTypeDefinition() const
{
    ITypePtr declaringType = DeclaringType();
    return declaringType ? declaringType->GetDefinition() : nullptr;
}

// The C# `IModule IEntity.ParentModule =>
// DeclaringType?.GetDefinition()?.ParentModule`.
const IModule* FakeMember::ParentModule() const
{
    const ITypeDefinition* definition = DeclaringTypeDefinition();
    return definition ? definition->ParentModule() : nullptr;
}

// The C# `string INamedElement.FullName` --
// `DeclaringType != null ? DeclaringType.FullName + "." + Name : Name`.
std::string FakeMember::FullName() const
{
    ITypePtr declaringType = DeclaringType();
    if (declaringType)
        return FullNameOf(*declaringType) + "." + Name();
    return Name();
}

// The C# `string INamedElement.ReflectionName` -- the same conditional over
// `ReflectionName`.
std::string FakeMember::ReflectionName() const
{
    ITypePtr declaringType = DeclaringType();
    if (declaringType)
        return declaringType->ReflectionName() + "." + Name();
    return Name();
}

// The C# `string INamedElement.Namespace => DeclaringType?.Namespace` (the
// null collapsing to "" -- convention (a)).
std::string FakeMember::Namespace() const
{
    ITypePtr declaringType = DeclaringType();
    return declaringType ? NamespaceOf(*declaringType) : std::string();
}

// --- FakeField ---

// The C# `public override IMember Specialize` --
// `SpecializedField.Create(this, substitution)`: the `Identity`-or-declaring-
// tpc-0 short-circuit returns the same instance (gold-pinned); the general
// arm constructs a `SpecializedField` -- DEFERRED with the owning-`Specialize`
// design (convention (d)).
const IMember* FakeField::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    if (TypeParameterSubstitution::Identity().Equals(substitution)
        || DeclaringTypeParameterCountOf(*this) == 0)
    {
        // The unambiguous sub-A conversion (`this` alone is ambiguous: the
        // FakeField carries two IMember subobjects, convention (g)).
        return static_cast<const FakeMember*>(this);
    }
    throw std::logic_error(
        "FakeField::Specialize: the SpecializedField construction is not "
        "yet ported (the owning-Specialize design the SpecializedX::Create "
        "factories are gated on)");
}

// --- FakeMethod ---

// The C# `internal static IMethod CreateDummyConstructor(ICompilation
// compilation, IType declaringType, Accessibility accessibility =
// Accessibility.Public)`.
std::shared_ptr<IMethod> FakeMethod::CreateDummyConstructor(
    const ICompilation& compilation, ITypePtr declaringType,
    ::ILSpy::Decompiler::TypeSystem::Accessibility accessibility)
{
    auto method = std::make_shared<FakeMethod>(
        compilation, ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor);
    method->SetDeclaringType(std::move(declaringType));
    method->SetName(".ctor");
    // The C# `ReturnType = compilation.FindType(KnownTypeCode.Void)` -- the
    // compilation-owned known type aliased with a no-op deleter (the
    // KnownTypeCache convention).
    const IType& voidType = compilation.FindType(
        ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void);
    method->SetReturnType(ITypePtr(
        const_cast<IType*>(&voidType), [](IType*) {
            // no-op: the compilation's type system owns the known type
        }));
    method->SetAccessibility(accessibility);
    return method;
}

// The C# `public IReadOnlyList<ITypeParameter> TypeParameters` -- the
// snapshot over the owning list.
std::vector<const ITypeParameter*> FakeMethod::TypeParameters() const
{
    std::vector<const ITypeParameter*> result;
    result.reserve(typeParameters_.size());
    for (const auto& tp : typeParameters_) {
        result.push_back(tp.get());
    }
    return result;
}

// The C# `public IReadOnlyList<IParameter> Parameters` -- the snapshot over
// the owning list (the C# EMPTY-list default, convention (a)).
std::vector<const IParameter*> FakeMethod::Parameters() const
{
    std::vector<const IParameter*> result;
    result.reserve(parameters_.size());
    for (const auto& p : parameters_) {
        result.push_back(p.get());
    }
    return result;
}

// The C# `IReadOnlyList<IType> IMethod.TypeArguments => TypeParameters` --
// the shared-handle snapshot (the MetadataMethod convention (k)); the fake
// owns its type parameters.
std::vector<ITypePtr> FakeMethod::TypeArguments() const
{
    std::vector<ITypePtr> result;
    result.reserve(typeParameters_.size());
    for (const auto& tp : typeParameters_) {
        result.push_back(ITypePtr(
            const_cast<ITypeParameter*>(tp.get()), [](ITypeParameter*) {
                // no-op: the fake method owns its type parameters
            }));
    }
    return result;
}

// The C# `public override IMember Specialize` --
// `SpecializedMethod.Create(this, substitution)`: the `Identity` short-circuit
// and the own-TypeParameters-empty + declaring-tpc-0 short-circuit return the
// same instance (gold-pinned); the general arm constructs a
// `SpecializedMethod` -- DEFERRED (convention (d)).
const IMethod* FakeMethod::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    if (TypeParameterSubstitution::Identity().Equals(substitution)) {
        // `this` converts to the covariant `const IMethod*` unambiguously
        // (IMethod is a direct base of FakeMethod).
        return this;
    }
    if (TypeParameters().empty()
        && DeclaringTypeParameterCountOf(*this) == 0)
    {
        return this;
    }
    throw std::logic_error(
        "FakeMethod::Specialize: the SpecializedMethod construction is not "
        "yet ported (the owning-Specialize design the SpecializedX::Create "
        "factories are gated on)");
}

// --- FakeProperty ---

// The C# `public IReadOnlyList<IParameter> Parameters` -- the snapshot over
// the owning list (the C# NULL default maps to the empty vector, convention
// (a)).
std::vector<const IParameter*> FakeProperty::Parameters() const
{
    std::vector<const IParameter*> result;
    result.reserve(parameters_.size());
    for (const auto& p : parameters_) {
        result.push_back(p.get());
    }
    return result;
}

// The C# `public override IMember Specialize` --
// `SpecializedProperty.Create(this, substitution)` (the Field form).
const IMember* FakeProperty::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    if (TypeParameterSubstitution::Identity().Equals(substitution)
        || DeclaringTypeParameterCountOf(*this) == 0)
    {
        return static_cast<const FakeMember*>(this);
    }
    throw std::logic_error(
        "FakeProperty::Specialize: the SpecializedProperty construction is "
        "not yet ported (the owning-Specialize design the SpecializedX::"
        "Create factories are gated on)");
}

// --- FakeEvent ---

// The C# `public override IMember Specialize` --
// `SpecializedEvent.Create(this, substitution)` (the Field form).
const IMember* FakeEvent::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    if (TypeParameterSubstitution::Identity().Equals(substitution)
        || DeclaringTypeParameterCountOf(*this) == 0)
    {
        return static_cast<const FakeMember*>(this);
    }
    throw std::logic_error(
        "FakeEvent::Specialize: the SpecializedEvent construction is not "
        "yet ported (the owning-Specialize design the SpecializedX::Create "
        "factories are gated on)");
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
