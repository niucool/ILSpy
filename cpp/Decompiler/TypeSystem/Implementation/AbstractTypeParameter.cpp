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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Out-of-line members of `AbstractTypeParameter` (the header comment in
// `AbstractTypeParameter.hpp` documents the port conventions + deferrals).

#include "Decompiler/TypeSystem/Implementation/AbstractTypeParameter.hpp"

#include "Decompiler/TypeSystem/IType.hpp"  // UnknownType()
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `name ?? ((OwnerType == Method ? "!!" : "!") + index)` default-name computation.
// The port's `std::string` has no null; an empty `name` is the sentinel for the default.
namespace {
std::string DefaultName(::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType, int index)
{
    return (ownerType == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method ? "!!" : "!") +
           std::to_string(index);
}
} // namespace

// The owner-based ctor. The C# `ArgumentNullException` on a null `owner` -> `std::invalid_argument`.
AbstractTypeParameter::AbstractTypeParameter(const IEntity* owner, int index, std::string name,
                                             VarianceModifier variance)
    : compilation_(owner ? &owner->Compilation() : nullptr),
      ownerType_(owner ? owner->SymbolKind()
                       : ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition),
      owner_(owner), index_(index),
      name_(name.empty() ? DefaultName(ownerType_, index) : std::move(name)),
      variance_(variance)
{
    if (!owner) {
        throw std::invalid_argument("AbstractTypeParameter: owner must not be null");
    }
}

// The compilation-based ctor. `owner` is null; `ownerType` is taken directly. The C#
// `ArgumentNullException` on a null `compilation` is structurally impossible (the port's
// `const ICompilation&` reference param cannot be null), so no check.
AbstractTypeParameter::AbstractTypeParameter(const ICompilation& compilation,
                                            ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
                                            int index, std::string name, VarianceModifier variance)
    : compilation_(&compilation), ownerType_(ownerType), owner_(nullptr), index_(index),
      name_(name.empty() ? DefaultName(ownerType, index) : std::move(name)), variance_(variance)
{}

// The C# `IType EffectiveBaseClass` -- DEFERRED to `UnknownType()` (the `DummyTypeParameter`
// precedent). The faithful `CalculateEffectiveBaseClass` (with `BusyManager` cyclic-protection +
// `ICompilation.FindType` + `IsDerivedFrom`) lands with `BusyManager`.
ITypePtr AbstractTypeParameter::EffectiveBaseClass() const {
    return UnknownType();
}

// The C# `IType AcceptVisitor(TypeVisitor) => visitor.VisitTypeParameter(this)`.
ITypePtr AbstractTypeParameter::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitTypeParameter(*this);
}

// The C# `IType ChangeNullability(Nullability)`: `Oblivious` -> this; else wrap in
// `NullabilityAnnotatedTypeParameter`. Mirrors `DummyTypeParameter::ChangeNullability`.
ITypePtr AbstractTypeParameter::ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    if (nullability == ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious) {
        return shared_from_this();
    }
    // `shared_from_this()` yields the single `IType` handle; up-cast to `ITypeParameter`.
    // The wrapper is returned through `ITypeParameter` (the direct `IType` conversion is
    // ambiguous -- the wrapper carries TWO non-virtual `IType` subobjects).
    auto wrapped = std::make_shared<NullabilityAnnotatedTypeParameter>(
        std::static_pointer_cast<ITypeParameter>(shared_from_this()), nullability);
    return std::static_pointer_cast<ITypeParameter>(wrapped);
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
