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

#include "Decompiler/TypeSystem/Implementation/MetadataTypeParameter.hpp"

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <cstdio>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `public static ITypeParameter[] Create(MetadataModule module,
// ITypeDefinition copyFromOuter, IEntity owner,
// GenericParameterHandleCollection handles)` (conventions (a) and (b)).
std::vector<std::shared_ptr<const ITypeParameter>> MetadataTypeParameter::Create(
    const MetadataModule& module,
    const ITypeDefinition* copyFromOuter,
    const IEntity* owner,
    const std::vector<Metadata::GenericParameterInfo>& genericParameters)
{
    // The C# `if (handles.Count == 0) return Empty<ITypeParameter>.Array;`
    // runs BEFORE the outer deref, so an empty list never needs the outer.
    if (genericParameters.empty())
        return {};
    // The C# `var outerTps = copyFromOuter.TypeParameters;` -- the null outer
    // NREs at the deref (the standard .NET message, the port's NRE mapping).
    if (copyFromOuter == nullptr)
        throw std::runtime_error(
            "Object reference not set to an instance of an object.");
    std::vector<const ITypeParameter*> outerTps =
        copyFromOuter->TypeParameters();
    std::vector<std::shared_ptr<const ITypeParameter>> tps;
    tps.reserve(genericParameters.size());
    for (std::size_t i = 0; i < genericParameters.size(); i++)
    {
        if (i < outerTps.size())
        {
            // The C# `tps[i] = outerTps[i]` -- the SAME ITypeParameter
            // object continues the outer's numbering. The port aliases the
            // outer definition's parameter with a no-op deleter (the outer
            // type definition, held by the module's entity cache, owns it --
            // the KnownTypeCache convention (d)).
            tps.push_back(std::shared_ptr<const ITypeParameter>(
                outerTps[i],
                [](const ITypeParameter*) {
                    // no-op: the outer type definition owns the parameter
                }));
        }
        else
        {
            tps.push_back(
                Create(module, owner, static_cast<int>(i),
                       genericParameters[i]));
        }
    }
    return tps;
}

// The C# `public static ITypeParameter[] Create(MetadataModule module,
// IEntity owner, GenericParameterHandleCollection handles)`.
std::vector<std::shared_ptr<const ITypeParameter>> MetadataTypeParameter::Create(
    const MetadataModule& module,
    const IEntity* owner,
    const std::vector<Metadata::GenericParameterInfo>& genericParameters)
{
    if (genericParameters.empty())
        return {};
    std::vector<std::shared_ptr<const ITypeParameter>> tps;
    tps.reserve(genericParameters.size());
    for (std::size_t i = 0; i < genericParameters.size(); i++)
        tps.push_back(Create(module, owner, static_cast<int>(i),
                             genericParameters[i]));
    return tps;
}

// The C# `public static MetadataTypeParameter Create(MetadataModule module,
// IEntity owner, int index, GenericParameterHandle handle)`. The raw
// `std::shared_ptr<T>(new T(...))` (not `make_shared`): the private ctor is
// access-checked at the new-expression's point of use (the iteration-39
// make_shared-cannot-reach-private-ctors trap).
std::shared_ptr<const ITypeParameter> MetadataTypeParameter::Create(
    const MetadataModule& module, const IEntity* owner, int index,
    const Metadata::GenericParameterInfo& genericParameter)
{
    return std::shared_ptr<const ITypeParameter>(
        new MetadataTypeParameter(module, owner, index, genericParameter.Name,
                                  genericParameter.Token, genericParameter.Flags));
}

// The C# `private MetadataTypeParameter(...) : base(owner, index, name,
// GetVariance(attr))`.
MetadataTypeParameter::MetadataTypeParameter(const MetadataModule& module,
                                             const IEntity* owner, int index,
                                             const std::string& name,
                                             std::uint32_t handle,
                                             std::uint16_t attr)
    : AbstractTypeParameter(owner, index, name, GetVariance(attr)),
      module_(module),
      handle_(handle),
      attr_(attr)
{
}

// The C# `public override int GetHashCode()` (out-of-line: the header
// declaration keeps `MetadataModule` incomplete).
int MetadataTypeParameter::GetHashCode() const {
    return static_cast<int>(0x51fc5b83u
        ^ static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(module_.MetadataFile()))
        ^ handle_);
}

// The C# `private static VarianceModifier GetVariance(
// GenericParameterAttributes attr)` -- the VarianceMask (0x3) switch over
// the raw ECMA II.23.1.7 bits.
VarianceModifier MetadataTypeParameter::GetVariance(std::uint16_t attr)
{
    switch (attr & 0x3u)
    {
        case 2:  // GenericParameterAttributes.Contravariant
            return VarianceModifier::Contravariant;
        case 1:  // GenericParameterAttributes.Covariant
            return VarianceModifier::Covariant;
        default:
            return VarianceModifier::Invariant;
    }
}

// The C# `public override string ToString()`.
std::string MetadataTypeParameter::ToString() const
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", handle_);
    return std::string(buffer) + " " + ReflectionName();
}

// DEFERRED (convention (d)): the AttributeListBuilder + custom-attribute
// value decoder.
std::vector<const IAttribute*> MetadataTypeParameter::GetAttributes() const
{
    throw std::logic_error(
        "MetadataTypeParameter::GetAttributes: AttributeListBuilder is not "
        "yet ported");
}

// The C# `public override bool HasUnmanagedConstraint` (convention (c)): the
// option gate first (no scan when UnmanagedConstraints is off), then the
// IsUnmanaged classification over the row's own CustomAttribute rows, cached
// in the ThreeState byte.
bool MetadataTypeParameter::HasUnmanagedConstraint() const
{
    if (unmanagedConstraint_ == 2)  // ThreeState.Unknown
    {
        bool result = false;
        if ((module_.TypeSystemOptions()
             & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                 UnmanagedConstraints)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None)
        {
            result = Metadata::HasKnownAttribute(
                *module_.MetadataFile(), handle_,
                ::ILSpy::Decompiler::TypeSystem::KnownAttribute::IsUnmanaged);
        }
        unmanagedConstraint_ = result ? 1 : 0;
    }
    return unmanagedConstraint_ == 1;
}

// DEFERRED (convention (d)): the [Nullable] byte decode behind
// ShouldDecodeNullableAttributes (the module's minAccessibilityForNRT
// computation and the CustomAttributeDecoder).
::ILSpy::Decompiler::TypeSystem::Nullability
MetadataTypeParameter::NullabilityConstraint() const
{
    throw std::logic_error(
        "MetadataTypeParameter::NullabilityConstraint: the NullableAttribute "
        "value decode is not yet ported (gated on "
        "ShouldDecodeNullableAttributes / the custom-attribute value "
        "decoder)");
}

// DEFERRED (convention (d)): the module.ResolveType constraint decode (the
// TypeProvider slice).
std::vector<TypeConstraint> MetadataTypeParameter::TypeConstraints() const
{
    throw std::logic_error(
        "MetadataTypeParameter::TypeConstraints: ResolveType is not yet "
        "ported (gated on the TypeProvider slice)");
}

// The C# `public override bool Equals(object obj)`.
bool MetadataTypeParameter::StructuralEquals(const IType& other) const
{
    auto parameter = dynamic_cast<const MetadataTypeParameter*>(&other);
    return parameter != nullptr && handle_ == parameter->handle_
        && module_.MetadataFile() == parameter->module_.MetadataFile();
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
