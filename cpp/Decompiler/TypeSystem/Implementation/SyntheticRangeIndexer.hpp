// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SyntheticRangeIndexer.cs --
// `SyntheticRangeIndexAccessor`, the synthetic IMethod for a compiler-generated
// indexer with the signature 'get_Item(System.Index)' / 'get_Item(System.Range)'
// (or a setter), the C# 8 implicit-index/range support. Consumed by the
// CallBuilder range-construction arm (HandleRangeConstruction's slicing case).
// Header-only (the VarArgInstanceMethod precedent): every member delegates to
// the underlying method except the synthetic parameter list (the index-or-range
// type as the first parameter) and the slicing flag.

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class SyntheticRangeIndexAccessor final : public IMethod {
public:
    SyntheticRangeIndexAccessor(std::shared_ptr<IMethod> underlyingMethod,
                                ITypePtr indexOrRangeType, bool slicing)
        : underlyingMethod_(std::move(underlyingMethod)),
          indexOrRangeType_(std::move(indexOrRangeType)),
          slicing_(slicing)
    {
        // The C# Debug.Asserts (the assert convention).
        assert(underlyingMethod_ != nullptr);
        assert(indexOrRangeType_ != nullptr);
        parameters_.push_back(
            std::static_pointer_cast<const IParameter>(
                std::make_shared<Implementation::DefaultParameter>(
                    indexOrRangeType_, std::string(), this)));
        if (slicing_)
        {
            assert(underlyingMethod_->Parameters().size() == 2);
        }
        else
        {
            std::vector<const IParameter*> baseParams = underlyingMethod_->Parameters();
            for (std::size_t i = 1; i < baseParams.size(); i++)
            {
                parameters_.push_back(std::shared_ptr<const IParameter>(
                    std::shared_ptr<const IParameter>(), baseParams[i]));
            }
        }
    }

    // The C# `public bool IsSlicing => slicing`.
    bool IsSlicing() const { return slicing_; }

    // --- IParameterizedMember ---

    // The C# `IReadOnlyList<IParameter> IParameterizedMember.Parameters => parameters`.
    std::vector<const IParameter*> Parameters() const override
    {
        std::vector<const IParameter*> snapshot;
        snapshot.reserve(parameters_.size());
        for (const std::shared_ptr<const IParameter>& p : parameters_)
        {
            snapshot.push_back(p.get());
        }
        return snapshot;
    }

    // The C# `IMember IMember.MemberDefinition => underlyingMethod.MemberDefinition`.
    const IMember* MemberDefinition() const override {
        return underlyingMethod_->MemberDefinition();
    }
    // The C# `IType IMember.ReturnType => underlyingMethod.ReturnType`.
    const IType& ReturnType() const override { return underlyingMethod_->ReturnType(); }
    // The C# `IEnumerable<IMember> IMember.ExplicitlyImplementedInterfaceMembers =>
    // EmptyList<IMember>.Instance`.
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    // The C# `bool IMember.IsExplicitInterfaceImplementation => false`.
    bool IsExplicitInterfaceImplementation() const override { return false; }
    // The C# `bool IMember.IsVirtual => underlyingMethod.IsVirtual`.
    bool IsVirtual() const override { return underlyingMethod_->IsVirtual(); }
    // The C# `bool IMember.IsOverride => underlyingMethod.IsOverride`.
    bool IsOverride() const override { return underlyingMethod_->IsOverride(); }
    // The C# `bool IMember.IsOverridable => underlyingMethod.IsOverridable`.
    bool IsOverridable() const override { return underlyingMethod_->IsOverridable(); }
    // The C# `TypeParameterSubstitution IMember.Substitution =>
    // underlyingMethod.Substitution`.
    const TypeParameterSubstitution* Substitution() const override {
        return underlyingMethod_->Substitution();
    }
    // The C# `EntityHandle IEntity.MetadataToken => underlyingMethod.MetadataToken`.
    std::uint32_t MetadataToken() const override { return underlyingMethod_->MetadataToken(); }
    // The C# `public string Name => underlyingMethod.Name`.
    std::string Name() const override { return underlyingMethod_->Name(); }
    // The C# `public IType DeclaringType => underlyingMethod.DeclaringType`.
    ITypePtr DeclaringType() const override { return underlyingMethod_->DeclaringType(); }
    // The C# `ITypeDefinition IEntity.DeclaringTypeDefinition =>
    // underlyingMethod.DeclaringTypeDefinition`.
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return underlyingMethod_->DeclaringTypeDefinition();
    }
    // The C# `IModule IEntity.ParentModule => underlyingMethod.ParentModule`.
    const IModule* ParentModule() const override { return underlyingMethod_->ParentModule(); }
    // The C# `Accessibility IEntity.Accessibility => underlyingMethod.Accessibility`.
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return underlyingMethod_->Accessibility();
    }
    // The C# `bool IEntity.IsStatic => underlyingMethod.IsStatic`.
    bool IsStatic() const override { return underlyingMethod_->IsStatic(); }
    // The C# `bool IEntity.IsAbstract => underlyingMethod.IsAbstract`.
    bool IsAbstract() const override { return underlyingMethod_->IsAbstract(); }
    // The C# `bool IEntity.IsSealed => underlyingMethod.IsSealed`.
    bool IsSealed() const override { return underlyingMethod_->IsSealed(); }
    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Method` (the globally
    // qualified return type -- the D372 name-hiding crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    // The C# `ICompilation ICompilationProvider.Compilation =>
    // underlyingMethod.Compilation`.
    const ICompilation& Compilation() const override {
        return underlyingMethod_->Compilation();
    }
    // The C# `string INamedElement.FullName => underlyingMethod.FullName`.
    std::string FullName() const override { return underlyingMethod_->FullName(); }
    // The C# `string INamedElement.ReflectionName => underlyingMethod.ReflectionName`.
    std::string ReflectionName() const override {
        return underlyingMethod_->ReflectionName();
    }
    // The C# `string INamedElement.Namespace => underlyingMethod.Namespace`.
    std::string Namespace() const override { return underlyingMethod_->Namespace(); }

    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // underlyingMethod.GetAttributes()`.
    std::vector<const IAttribute*> GetAttributes() const override {
        return underlyingMethod_->GetAttributes();
    }
    // The C# `bool IEntity.HasAttribute(KnownAttribute) =>
    // underlyingMethod.HasAttribute(attribute)`.
    bool HasAttribute(KnownAttribute attribute) const override {
        return underlyingMethod_->HasAttribute(attribute);
    }
    // The C# `IAttribute IEntity.GetAttribute(KnownAttribute) =>
    // underlyingMethod.GetAttribute(attribute)`.
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return underlyingMethod_->GetAttribute(attribute);
    }

    // The C# `public override bool Equals(object obj)` / `public override int
    // GetHashCode()` are plain members with no port virtual (the VarArgInstanceMethod
    // convention (c)): object identity through the member chain is the observable
    // surface, and the C# hash is unreachable through the port's API.

    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)`.
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override {
        const auto* g = dynamic_cast<const SyntheticRangeIndexAccessor*>(obj);
        if (g == nullptr)
            return false;
        if (!underlyingMethod_->Equals(g->underlyingMethod_.get(), typeNormalization))
            return false;
        ITypePtr left = indexOrRangeType_->AcceptVisitor(
            *const_cast<TypeVisitor*>(typeNormalization));
        ITypePtr right = g->indexOrRangeType_->AcceptVisitor(
            *const_cast<TypeVisitor*>(typeNormalization));
        return left != nullptr && right != nullptr && left->Equals(*right);
    }

    // --- IMethod ---

    // The C# `IEnumerable<IAttribute> IMethod.GetReturnTypeAttributes() =>
    // underlyingMethod.GetReturnTypeAttributes()`.
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return underlyingMethod_->GetReturnTypeAttributes();
    }
    // The C# `bool IMethod.ReturnTypeIsRefReadOnly =>
    // underlyingMethod.ReturnTypeIsRefReadOnly`.
    bool ReturnTypeIsRefReadOnly() const override {
        return underlyingMethod_->ReturnTypeIsRefReadOnly();
    }
    // The C# `bool IMethod.ThisIsRefReadOnly => underlyingMethod.ThisIsRefReadOnly`.
    bool ThisIsRefReadOnly() const override {
        return underlyingMethod_->ThisIsRefReadOnly();
    }
    // The C# `bool IMethod.IsInitOnly => underlyingMethod.IsInitOnly`.
    bool IsInitOnly() const override { return underlyingMethod_->IsInitOnly(); }
    // The C# `IReadOnlyList<ITypeParameter> IMethod.TypeParameters =>
    // EmptyList<ITypeParameter>.Instance`.
    std::vector<const ITypeParameter*> TypeParameters() const override { return {}; }
    // The C# `IReadOnlyList<IType> IMethod.TypeArguments =>
    // EmptyList<IType>.Instance`.
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    // The C# `bool IMethod.IsExtensionMethod => false`.
    bool IsExtensionMethod() const override { return false; }
    // The C# `bool IMethod.IsLocalFunction => false`.
    bool IsLocalFunction() const override { return false; }
    // The C# `bool IMethod.IsConstructor => false`.
    bool IsConstructor() const override { return false; }
    // The C# `bool IMethod.IsDestructor => false`.
    bool IsDestructor() const override { return false; }
    // The C# `bool IMethod.IsOperator => false`.
    bool IsOperator() const override { return false; }
    // The C# `bool IMethod.HasBody => underlyingMethod.HasBody`.
    bool HasBody() const override { return underlyingMethod_->HasBody(); }
    // The C# `bool IMethod.IsAccessor => underlyingMethod.IsAccessor`.
    bool IsAccessor() const override { return underlyingMethod_->IsAccessor(); }
    // The C# `IMember IMethod.AccessorOwner => underlyingMethod.AccessorOwner`.
    const IMember* AccessorOwner() const override {
        return underlyingMethod_->AccessorOwner();
    }
    // The C# `MethodSemanticsAttributes IMethod.AccessorKind =>
    // underlyingMethod.AccessorKind`.
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind()
        const override {
        return underlyingMethod_->AccessorKind();
    }
    // The C# `IMethod IMethod.ReducedFrom => underlyingMethod.ReducedFrom`.
    const IMethod* ReducedFrom() const override { return underlyingMethod_->ReducedFrom(); }

    // The C# `IMethod IMethod.Specialize(TypeParameterSubstitution substitution)` /
    // `IMember IMember.Specialize(...)` -- a FRESH wrapper over the specialized
    // underlying method (the C# both interfaces implement identically; the port's
    // single covariant `const IMethod*` final overrider covers both slots).
    const IMethod* Specialize(const TypeParameterSubstitution* substitution)
        const override
    {
        return new SyntheticRangeIndexAccessor(
            std::shared_ptr<IMethod>(
                std::shared_ptr<IMethod>(),
                const_cast<IMethod*>(underlyingMethod_->Specialize(substitution))),
            indexOrRangeType_, slicing_);
    }

private:
    // The C# `readonly IMethod underlyingMethod` -- the GC reference is a shared
    // handle (the ctor parameter owns the underlying method's lifetime for the
    // CallBuilder's use sites; the port's shared_ptr keeps it).
    std::shared_ptr<IMethod> underlyingMethod_;
    // The C# `readonly IType indexOrRangeType`.
    ITypePtr indexOrRangeType_;
    // The C# `readonly IReadOnlyList<IParameter> parameters`.
    std::vector<std::shared_ptr<const IParameter>> parameters_;
    // The C# `readonly bool slicing`.
    bool slicing_;
};

} // namespace ILSpy::Decompiler::TypeSystem
