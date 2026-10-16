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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SyntheticRangeIndexer.cs --
// `class SyntheticRangeIndexAccessor : IMethod` -- "Synthetic method representing a
// compiler-generated indexer with the signature 'get_Item(System.Index)' or
// 'get_Item(System.Range)'. Can also be a setter. Used for the 'Implicit Index
// support'/'Implicit Range support' for the C# 8 ranges feature." The ported consumers
// are the CallBuilder range-construction render (`HandleRangeConstruction`'s slicing
// arm) and the ILTransforms that create the wrapper (the C# `ExpressionTransforms` /
// `StatementTransform` code synthesizes the accessor from the ILAst's range-based
// indexer pattern).
//
// KEY PORT CONVENTIONS (the LocalFunctionMethod.hpp wrapper conventions, adapted):
//  (a) The C# `readonly IMethod underlyingMethod` / `readonly IType indexOrRangeType`
//      (GC references) port to OWNING `std::shared_ptr` handles (the
//      `LocalFunctionMethod::baseMethod_` precedent). The ctor takes them by value and
//      throws `std::invalid_argument` on a null underlying method or null type (the C#
//      `Debug.Assert` pair; the null guard is the DefaultParameter convention).
//  (b) The parameter list the ctor builds ports to a compute-per-call snapshot over an
//      owned storage vector: `DefaultParameter(indexOrRangeType, "")` first, then
//      (non-slicing only) `underlyingMethod.Parameters.Skip(1)` -- the C# `Skip`
//      dropping the `this`-shaped first parameter (the C# asserts
//      `underlyingMethod.Parameters.Count == 2` when slicing; the port asserts the
//      same in the ctor).
//  (c) `SymbolKind()` is `SymbolKind::Method` UNCONDITIONALLY (the C#
//      `ISymbol.SymbolKind => SymbolKind.Method`) -- not the underlying method's kind
//      (a `get_Item` accessor carries `SymbolKind.Accessor`; the wrapper presents as a
//      plain method).
//  (d) The negative delegations are unconditional: `IsExtensionMethod` /
//      `IsLocalFunction` / `IsConstructor` / `IsDestructor` / `IsOperator` /
//      `IsExplicitInterfaceImplementation` are FALSE and
//      `ExplicitlyImplementedInterfaceMembers` / `TypeParameters` / `TypeArguments`
//      are EMPTY, even when the underlying method differs (the C# explicit
//      interface-implementation bodies).
//  (e) `Equals(IMember, TypeVisitor)`: the `SyntheticRangeIndexAccessor`-RTTI gate,
//      then the underlying-method equality under the normalization AND the
//      index-or-range type equality under the normalization AND the slicing flag. The
//      C# `override bool Equals(object)` is the SAME comparison without a
//      normalization -- the port's single 2-arg override covers both (a caller passes
//      a null normalization for the object.Equals form).
//  (f) `Specialize()` builds a FRESH wrapper over the specialized underlying method
//      (both the `IMethod` and the `IMember` specializations; the C# explicit
//      interface implementations are one and the same). The fresh wrapper is kept
//      alive for THIS wrapper's lifetime through a mutable owning vector (the
//      `LocalFunctionMethod` rewraps convention) because the port's `Specialize`
//      returns a non-owning handle.
//  (g) `IsSlicing` is the ctor flag, widened to public for direct TDD (the
//      internal-widening convention). The C# class is `internal` (unsealed); the port
//      widens it to public and does not mark it `final`.
//  (h) The name-hiding qualifications: the inherited `SymbolKind()` /
//      `Accessibility()` member names hide the namespace-scope enums in the class body
//      (the D372 cross-scope crux), so those two overrides carry the
//      globally-qualified return types.

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include <cassert>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// Port of the C# `class SyntheticRangeIndexAccessor : IMethod` (see the header
// comment). Derives `IMethod` directly (single inheritance; the `IEntity` /
// `ISymbol` / `ICompilationProvider` / `INamedElement` diamond is resolved by one
// override per name, the `LocalFunctionMethod` precedent). Everything is inline (the
// class is header-only; the only non-trivial dependency -- `TypeVisitor` -- appears as
// a pointer/reference).
class SyntheticRangeIndexAccessor : public IMethod {
public:
    // The C# `public SyntheticRangeIndexAccessor(IMethod underlyingMethod, IType
    // indexOrRangeType, bool slicing)`: asserts both non-null, stores them, and
    // builds the wrapper parameter list (convention (b)).
    SyntheticRangeIndexAccessor(std::shared_ptr<const IMethod> underlyingMethod,
                                ITypePtr indexOrRangeType, bool slicing)
        : underlyingMethod_(std::move(underlyingMethod)),
          indexOrRangeType_(std::move(indexOrRangeType)),
          slicing_(slicing)
    {
        if (underlyingMethod_ == nullptr) {
            throw std::invalid_argument("underlyingMethod");
        }
        if (indexOrRangeType_ == nullptr) {
            throw std::invalid_argument("indexOrRangeType");
        }
        if (slicing_) {
            assert(underlyingMethod_->Parameters().size() == 2
                   && "SyntheticRangeIndexAccessor: a slicing accessor's underlying "
                      "method must have exactly two parameters");
        } else {
            parameters_.push_back(std::make_shared<DefaultParameter>(
                indexOrRangeType_, std::string()));
            const std::vector<const IParameter*> underlyingParameters =
                underlyingMethod_->Parameters();
            for (std::size_t i = 1; i < underlyingParameters.size(); i++) {
                parameters_.push_back(std::shared_ptr<const IParameter>(
                    underlyingParameters[i], [](const IParameter*) {
                        // no-op: the underlying method owns its parameters
                    }));
            }
        }
    }

    // The C# `public bool IsSlicing => slicing` -- whether the accessor wraps a
    // `Slice(int, int)` (the range-slicing shape) rather than a `get_Item(Index)`
    // (convention (g)).
    bool IsSlicing() const { return slicing_; }

    // --- ISymbol ---

    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Method` (convention (c)).
    // Globally qualified (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }

    // The C# `public string Name => underlyingMethod.Name`.
    std::string Name() const override { return underlyingMethod_->Name(); }

    // --- INamedElement ---

    // The C# `string INamedElement.FullName => underlyingMethod.FullName`.
    std::string FullName() const override { return underlyingMethod_->FullName(); }

    // The C# `string INamedElement.ReflectionName => underlyingMethod.ReflectionName`.
    std::string ReflectionName() const override
    {
        return underlyingMethod_->ReflectionName();
    }

    // The C# `string INamedElement.Namespace => underlyingMethod.Namespace`.
    std::string Namespace() const override { return underlyingMethod_->Namespace(); }

    // --- ICompilationProvider ---

    // The C# `ICompilation ICompilationProvider.Compilation =>
    // underlyingMethod.Compilation`.
    const ICompilation& Compilation() const override
    {
        return underlyingMethod_->Compilation();
    }

    // --- IEntity ---

    // The C# `EntityHandle IEntity.MetadataToken => underlyingMethod.MetadataToken`.
    std::uint32_t MetadataToken() const override
    {
        return underlyingMethod_->MetadataToken();
    }

    // The C# `ITypeDefinition IEntity.DeclaringTypeDefinition =>
    // underlyingMethod.DeclaringTypeDefinition`.
    const ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return underlyingMethod_->DeclaringTypeDefinition();
    }

    // The C# `IType IEntity.DeclaringType => underlyingMethod.DeclaringType`.
    ITypePtr DeclaringType() const override { return underlyingMethod_->DeclaringType(); }

    // The C# `IModule IEntity.ParentModule => underlyingMethod.ParentModule`.
    const IModule* ParentModule() const override
    {
        return underlyingMethod_->ParentModule();
    }

    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // underlyingMethod.GetAttributes()`.
    std::vector<const IAttribute*> GetAttributes() const override
    {
        return underlyingMethod_->GetAttributes();
    }

    // The C# `bool IEntity.HasAttribute(KnownAttribute) =>
    // underlyingMethod.HasAttribute(...)`.
    bool HasAttribute(KnownAttribute attribute) const override
    {
        return underlyingMethod_->HasAttribute(attribute);
    }

    // The C# `IAttribute IEntity.GetAttribute(KnownAttribute) =>
    // underlyingMethod.GetAttribute(...)`.
    const IAttribute* GetAttribute(KnownAttribute attribute) const override
    {
        return underlyingMethod_->GetAttribute(attribute);
    }

    // The C# `Accessibility IEntity.Accessibility => underlyingMethod.Accessibility`.
    // Globally qualified (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return underlyingMethod_->Accessibility();
    }

    // The C# `bool IEntity.IsStatic => underlyingMethod.IsStatic`.
    bool IsStatic() const override { return underlyingMethod_->IsStatic(); }

    // The C# `bool IEntity.IsAbstract => underlyingMethod.IsAbstract`.
    bool IsAbstract() const override { return underlyingMethod_->IsAbstract(); }

    // The C# `bool IEntity.IsSealed => underlyingMethod.IsSealed`.
    bool IsSealed() const override { return underlyingMethod_->IsSealed(); }

    // --- IMember ---

    // The C# `IMember IMember.MemberDefinition => underlyingMethod.MemberDefinition`.
    const IMember* MemberDefinition() const override
    {
        return underlyingMethod_->MemberDefinition();
    }

    // The C# `IType IMember.ReturnType => underlyingMethod.ReturnType`.
    const IType& ReturnType() const override { return underlyingMethod_->ReturnType(); }

    // The C# `IEnumerable<IMember> IMember.ExplicitlyImplementedInterfaceMembers =>
    // EmptyList<IMember>.Instance` (convention (d)).
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }

    // The C# `bool IMember.IsExplicitInterfaceImplementation => false` (convention
    // (d)).
    bool IsExplicitInterfaceImplementation() const override { return false; }

    // The C# `bool IMember.IsVirtual => underlyingMethod.IsVirtual`.
    bool IsVirtual() const override { return underlyingMethod_->IsVirtual(); }

    // The C# `bool IMember.IsOverride => underlyingMethod.IsOverride`.
    bool IsOverride() const override { return underlyingMethod_->IsOverride(); }

    // The C# `bool IMember.IsOverridable => underlyingMethod.IsOverridable`.
    bool IsOverridable() const override { return underlyingMethod_->IsOverridable(); }

    // The C# `TypeParameterSubstitution IMember.Substitution =>
    // underlyingMethod.Substitution`.
    const TypeParameterSubstitution* Substitution() const override
    {
        return underlyingMethod_->Substitution();
    }

    // The C# `IMethod IMethod.Specialize(TypeParameterSubstitution substitution)
    // => new SyntheticRangeIndexAccessor(underlyingMethod.Specialize(substitution),
    // indexOrRangeType, slicing)` (the IMethod/IMember pair collapses to one
    // override; convention (f)).
    const IMethod* Specialize(const TypeParameterSubstitution* substitution) const override
    {
        const IMethod* specializedUnderlying = underlyingMethod_->Specialize(substitution);
        auto rewrap = std::make_shared<SyntheticRangeIndexAccessor>(
            std::shared_ptr<const IMethod>(underlyingMethod_, specializedUnderlying),
            indexOrRangeType_, slicing_);
        rewraps_.push_back(std::move(rewrap));
        return rewraps_.back().get();
    }

    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)`
    // (convention (e) -- and the `Equals(object)` twin via the null normalization).
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override
    {
        const auto* other = dynamic_cast<const SyntheticRangeIndexAccessor*>(obj);
        if (other == nullptr) {
            return false;
        }
        bool typeEquals;
        if (typeNormalization != nullptr) {
            // The C# `Equals(IMember, TypeVisitor)` form: normalize both
            // index-or-range types through the visitor before comparing.
            ITypePtr thisNormalized = indexOrRangeType_->AcceptVisitor(
                const_cast<TypeVisitor&>(*typeNormalization));
            ITypePtr otherNormalized = other->indexOrRangeType_->AcceptVisitor(
                const_cast<TypeVisitor&>(*typeNormalization));
            typeEquals = thisNormalized != nullptr && otherNormalized != nullptr
                && thisNormalized->Equals(*otherNormalized);
        } else {
            // The C# `override bool Equals(object)` form (the null
            // normalization): compare the unnormalized types.
            typeEquals = indexOrRangeType_->Equals(*other->indexOrRangeType_);
        }
        return underlyingMethod_->Equals(other->underlyingMethod_.get(), typeNormalization)
            && typeEquals
            && slicing_ == other->slicing_;
    }

    // The C# `public override int GetHashCode() =>
    // underlyingMethod.GetHashCode() ^ indexOrRangeType.GetHashCode()` (the
    // LocalFunctionMethod pointer-identity-hash precedent).
    int GetHashCode() const
    {
        return static_cast<int>(std::hash<const void*>{}(
                   static_cast<const void*>(underlyingMethod_.get())))
            ^ static_cast<int>(std::hash<const void*>{}(
                static_cast<const void*>(indexOrRangeType_.get())));
    }

    // --- IParameterizedMember ---

    // The C# `IReadOnlyList<IParameter> IParameterizedMember.Parameters =>
    // parameters` -- the ctor-built list (convention (b)).
    std::vector<const IParameter*> Parameters() const override
    {
        std::vector<const IParameter*> result;
        result.reserve(parameters_.size());
        for (const auto& p : parameters_) {
            result.push_back(p.get());
        }
        return result;
    }

    // --- IMethod ---

    // The C# `IEnumerable<IAttribute> IMethod.GetReturnTypeAttributes() =>
    // underlyingMethod.GetReturnTypeAttributes()`.
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override
    {
        return underlyingMethod_->GetReturnTypeAttributes();
    }

    // The C# `bool IMethod.ReturnTypeIsRefReadOnly =>
    // underlyingMethod.ReturnTypeIsRefReadOnly`.
    bool ReturnTypeIsRefReadOnly() const override
    {
        return underlyingMethod_->ReturnTypeIsRefReadOnly();
    }

    // The C# `bool IMethod.ThisIsRefReadOnly => underlyingMethod.ThisIsRefReadOnly`.
    bool ThisIsRefReadOnly() const override
    {
        return underlyingMethod_->ThisIsRefReadOnly();
    }

    // The C# `bool IMethod.IsInitOnly => underlyingMethod.IsInitOnly`.
    bool IsInitOnly() const override { return underlyingMethod_->IsInitOnly(); }

    // The C# `IReadOnlyList<ITypeParameter> IMethod.TypeParameters =>
    // EmptyList<ITypeParameter>.Instance` (convention (d)).
    std::vector<const ITypeParameter*> TypeParameters() const override { return {}; }

    // The C# `IReadOnlyList<IType> IMethod.TypeArguments =>
    // EmptyList<IType>.Instance` (convention (d)).
    std::vector<ITypePtr> TypeArguments() const override { return {}; }

    // The C# `bool IMethod.IsExtensionMethod => false` (convention (d)).
    bool IsExtensionMethod() const override { return false; }

    // The C# `bool IMethod.IsLocalFunction => false` (convention (d)).
    bool IsLocalFunction() const override { return false; }

    // The C# `bool IMethod.IsConstructor => false` (convention (d)).
    bool IsConstructor() const override { return false; }

    // The C# `bool IMethod.IsDestructor => false` (convention (d)).
    bool IsDestructor() const override { return false; }

    // The C# `bool IMethod.IsOperator => false` (convention (d)).
    bool IsOperator() const override { return false; }

    // The C# `bool IMethod.HasBody => underlyingMethod.HasBody`.
    bool HasBody() const override { return underlyingMethod_->HasBody(); }

    // The C# `bool IMethod.IsAccessor => underlyingMethod.IsAccessor`.
    bool IsAccessor() const override { return underlyingMethod_->IsAccessor(); }

    // The C# `IMethod.IMember AccessorOwner => underlyingMethod.AccessorOwner`.
    const IMember* AccessorOwner() const override
    {
        return underlyingMethod_->AccessorOwner();
    }

    // The C# `MethodSemanticsAttributes IMethod.AccessorKind =>
    // underlyingMethod.AccessorKind`.
    MethodSemanticsAttributes AccessorKind() const override
    {
        return underlyingMethod_->AccessorKind();
    }

    // The C# `IMethod IMethod.ReducedFrom => underlyingMethod.ReducedFrom`.
    const IMethod* ReducedFrom() const override
    {
        return underlyingMethod_->ReducedFrom();
    }

private:
    // The C# `readonly IMethod underlyingMethod` (convention (a)) -- the
    // `get_Item`/`set_Item`/`Slice` method the wrapper re-signatures.
    std::shared_ptr<const IMethod> underlyingMethod_;
    // The C# `readonly IType indexOrRangeType` (convention (a)) -- the
    // `System.Index`/`System.Range` parameter type of the synthetic accessor.
    ITypePtr indexOrRangeType_;
    // The C# `readonly bool slicing`.
    bool slicing_;
    // The ctor-built parameter list (convention (b)); the non-slicing tail
    // entries alias the underlying method's own parameter objects (the
    // no-op-deleter alias, convention (b)).
    std::vector<std::shared_ptr<const IParameter>> parameters_;
    // Every rewrap this wrapper created (`Specialize`), kept alive for this
    // wrapper's lifetime (convention (f)).
    mutable std::vector<std::shared_ptr<SyntheticRangeIndexAccessor>> rewraps_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation