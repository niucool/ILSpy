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

// Port of ICSharpCode.Decompiler/TypeSystem/GenericContext.cs -- the
// `readonly struct GenericContext` that scopes VAR/MVAR decoding: the
// type-system signature provider (`TypeProvider`) resolves
// ELEMENT_TYPE_VAR/`!N` and ELEMENT_TYPE_MVAR/`!!N` indices through the
// class/method type-parameter lists it carries.
//
// KEY PORT CONVENTIONS:
//  (a) The C# fields are `IReadOnlyList<ITypeParameter>` (GC references the
//      caller owns); the port holds NON-OWNING `const ITypeParameter*`
//      snapshots by value (`std::vector<const ITypeParameter*>`). The C#
//      `MethodTypeParameters` can be null; every consumer treats null and
//      empty identically (`index < ClassTypeParameters?.Count` is false for
//      both, and `ToSubstitution`'s `?.Count > 0` collapses both to the null
//      substitution), so the port carries ONE plain vector per slot and the
//      null/empty distinction is not observable -- a documented collapse.
//  (b) `GetClassTypeParameter`/`GetMethodTypeParameter` return the indexed
//      parameter, falling back to `DummyTypeParameter.GetClassTypeParameter`/
//      `GetMethodTypeParameter` (the cached per-index dummies) out of range.
//      The port returns `std::shared_ptr<ITypeParameter>`: an owning handle
//      for the cached dummy, and a NON-OWNING alias (a no-op deleter, the
//      ReflectionHelper::SnapshotDefinition convention) for the in-range
//      parameter -- the entity's `TypeParameters()` list owns it for the
//      compilation's lifetime.
//  (c) `ToSubstitution` ports `ClassTypeParameters?.Count > 0 ?
//      ClassTypeParameters : null` to the `TypeParameterSubstitution`
//      optional-vector convention (`std::nullopt` = the C# null "keep
//      unmodified"; a non-empty vector converts each `const ITypeParameter*`
//      to a non-owning `ITypePtr` alias -- `ITypeParameter` IS-A `IType`, the
//      C# `IReadOnlyList<ITypeParameter>`-to-`IReadOnlyList<IType>` covariance
//      the port spells as the alias conversion).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"          // IType, ITypePtr
#include "Decompiler/TypeSystem/IEntity.hpp"        // IEntity
#include "Decompiler/TypeSystem/IMethod.hpp"        // IMethod
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The VAR/MVAR scope (see the header comment). A value type -- copied freely,
// like the C# `readonly struct`.
class GenericContext {
public:
    // The C# `new GenericContext()` -- the `readonly struct`'s implicit
    // parameterless ctor (both fields null). The AttributeListBuilder
    // attribute-ctor resolution (AttributeListBuilder.cs lines 194/299) drives
    // it: "Attribute types shouldn't be open generic, so we don't need a
    // generic context". The C# null lists and the port's empty vectors are
    // equivalent everywhere (`?.Count` guards, `ToSubstitution`'s
    // null-over-empty collapse).
    GenericContext() = default;

    // The C# `GenericContext(IReadOnlyList<ITypeParameter> classTypeParameters)`
    // -- no method parameters.
    explicit GenericContext(
        std::vector<const ITypeParameter*> classTypeParameters)
        : classTypeParameters_(std::move(classTypeParameters)) {}

    // The C# `GenericContext(IReadOnlyList<ITypeParameter> classTypeParameters,
    // IReadOnlyList<ITypeParameter> methodTypeParameters)`.
    GenericContext(std::vector<const ITypeParameter*> classTypeParameters,
                   std::vector<const ITypeParameter*> methodTypeParameters)
        : classTypeParameters_(std::move(classTypeParameters)),
          methodTypeParameters_(std::move(methodTypeParameters)) {}

    // The C# `internal GenericContext(ITypeResolveContext context)`:
    // `ClassTypeParameters = context.CurrentTypeDefinition?.TypeParameters;
    // MethodTypeParameters = (context.CurrentMember as IMethod)?.TypeParameters`.
    explicit GenericContext(const ITypeResolveContext& context) {
        if (const ITypeDefinition* td = context.CurrentTypeDefinition())
            classTypeParameters_ = td->TypeParameters();
        if (const IMethod* method =
                dynamic_cast<const IMethod*>(context.CurrentMember()))
            methodTypeParameters_ = method->TypeParameters();
    }

    // The C# `internal GenericContext(IEntity context)`: an ITypeDefinition
    // scopes its own type parameters; any other entity scopes its declaring
    // type's parameters plus its own method parameters (the C# `(context as
    // IMethod)?.TypeParameters`).
    explicit GenericContext(const IEntity& context) {
        if (const ITypeDefinition* td =
                dynamic_cast<const ITypeDefinition*>(&context)) {
            classTypeParameters_ = td->TypeParameters();
        } else {
            if (const ITypeDefinition* declaring =
                    context.DeclaringTypeDefinition())
                classTypeParameters_ = declaring->TypeParameters();
            if (const IMethod* method =
                    dynamic_cast<const IMethod*>(&context))
                methodTypeParameters_ = method->TypeParameters();
        }
    }

    // The C# `public ITypeParameter GetClassTypeParameter(int index)` (the
    // convention-(b) shared_ptr return).
    std::shared_ptr<ITypeParameter> GetClassTypeParameter(int index) const {
        if (index >= 0 &&
            static_cast<std::size_t>(index) < classTypeParameters_.size()) {
            // Non-owning alias: the entity's TypeParameters() list owns it.
            return std::shared_ptr<ITypeParameter>(
                std::shared_ptr<ITypeParameter>(),
                const_cast<ITypeParameter*>(
                    classTypeParameters_[static_cast<std::size_t>(index)]));
        }
        return Implementation::DummyTypeParameter::GetClassTypeParameter(index);
    }

    // The C# `public ITypeParameter GetMethodTypeParameter(int index)`.
    std::shared_ptr<ITypeParameter> GetMethodTypeParameter(int index) const {
        if (index >= 0 &&
            static_cast<std::size_t>(index) < methodTypeParameters_.size()) {
            return std::shared_ptr<ITypeParameter>(
                std::shared_ptr<ITypeParameter>(),
                const_cast<ITypeParameter*>(
                    methodTypeParameters_[static_cast<std::size_t>(index)]));
        }
        return Implementation::DummyTypeParameter::GetMethodTypeParameter(index);
    }

    // The read-only projections (the C# public fields). By-value snapshots.
    const std::vector<const ITypeParameter*>& ClassTypeParameters() const {
        return classTypeParameters_;
    }
    const std::vector<const ITypeParameter*>& MethodTypeParameters() const {
        return methodTypeParameters_;
    }

    // The C# `internal TypeParameterSubstitution ToSubstitution()`. The C#
    // prefers `null` over empty lists in substitutions ("we need our
    // substitution to compare equal to the ones created by the TS"), so a
    // count of 0 maps to `std::nullopt` (convention (c)).
    TypeParameterSubstitution ToSubstitution() const {
        return TypeParameterSubstitution(
            ToSubstitutionList(classTypeParameters_),
            ToSubstitutionList(methodTypeParameters_));
    }

private:
    static std::optional<std::vector<ITypePtr>> ToSubstitutionList(
        const std::vector<const ITypeParameter*>& parameters) {
        if (parameters.empty()) return std::nullopt;
        std::vector<ITypePtr> arguments;
        arguments.reserve(parameters.size());
        for (const ITypeParameter* p : parameters) {
            // Non-owning alias (convention (c)): the parameter's owner keeps
            // it alive for the substitution's lifetime.
            arguments.push_back(ITypePtr(
                std::shared_ptr<IType>(), const_cast<ITypeParameter*>(p)));
        }
        return arguments;
    }

    std::vector<const ITypeParameter*> classTypeParameters_;
    std::vector<const ITypeParameter*> methodTypeParameters_;
};

} // namespace ILSpy::Decompiler::TypeSystem
