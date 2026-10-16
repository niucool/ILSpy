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

// Port of ICSharpCode.Decompiler/TypeSystem/VarArgInstanceMethod.cs -- the
// `public class VarArgInstanceMethod : IMethod` wrapper a vararg CALL SITE
// resolves to (`MetadataModule.ResolveMethodDefinition`'s `expandVarArgs` arm
// and `ResolveMethodReference`'s trailing `VarArgs` arm construct it): the
// wrapper stores the actual parameter types being passed, replacing the
// sentinel `__arglist` parameter of the underlying method.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly IMethod baseMethod` ports to an OWNING
//      `std::shared_ptr<IMethod> baseMethod_` (the LocalFunctionMethod
//      convention): the wrapper is constructed standalone (the C# GC roots the
//      base method; the port's wrapper owns it), and the delegation surface
//      reads through the handle.
//  (b) The C# ctor's `IEnumerable<IType> varArgTypes` ports to a
//      `std::vector<ITypePtr>`; the `Debug.Assert(paramList.Last().Type.Kind ==
//      TypeKind.ArgList)` is compiled out of the release assembly the shipped
//      tool runs (the Debug.Assert release-strip convention -- the port does
//      not carry it). Each vararg type becomes a `DefaultParameter` with the
//      empty name and THIS wrapper as the owner.
//  (c) The C# `Equals(object)` / `GetHashCode()` are plain members (the port's
//      `IMethod` carries no `GetHashCode` virtual -- the SpecializedMember
//      precedent): the C# default-object-`GetHashCode` on the wrapper is
//      ported as the base method's hash; `Equals(object)` and
//      `Equals(IMember, TypeVisitor)` are the SAME comparison (the
//      VarArgInstanceMethod-RTTI gate, then `baseMethod.Equals`), covered by
//      the single 2-arg override (a caller passes a null normalization for the
//      object form).
//  (d) `Specialize` builds a FRESH wrapper over the specialized base method
//      (the C# allocates per call). Because `IMethod::Specialize` returns a
//      NON-OWNING handle while the port has no shared owning-`Specialize`
//      machinery, every rewrap this wrapper creates is kept alive for THIS
//      wrapper's lifetime through a mutable owning vector (the C# GC owns
//      each fresh wrapper; here the source wrapper owns its results -- the
//      LocalFunctionMethod rewraps precedent). Two calls produce two distinct
//      wrappers that are `Equals`-equal -- the C# fresh-per-call identity
//      semantics (gold-pinned: `ReferenceEquals(spec, two)` is false while
//      `Equals` and the `ToString` render agree).
//  (e) `ToString()` renders through `ReflectionName` (NOT the deferred
//      IType-`ToString` chain): the kind spelling is a local switch carrying
//      the .NET enum-`ToString` semantics (the member name or the decimal
//      fallback, the TypeKindName/DeclarativeSecurityAction convention).
//  (f) The name-hiding qualifications: the inherited `SymbolKind()` /
//      `Accessibility()` member names hide the namespace-scope enums in the
//      class body (the D372 crux), so those two overrides carry the
//      globally-qualified return types.

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The vararg call-site wrapper (see the header comment). Derives `IMethod`
// directly (single inheritance; the diamond is resolved by one override per
// name, the LocalFunctionMethod precedent). Everything inline (header-only; the
// class is not added to the ilspy CMakeLists -- it compiles into each TU that
// includes it, the SpecializedParameter.hpp precedent).
class VarArgInstanceMethod : public IMethod {
public:
    // The C# `public VarArgInstanceMethod(IMethod baseMethod, IEnumerable<IType>
    // varArgTypes)`: replaces the base's trailing `__arglist` sentinel with one
    // `DefaultParameter` per vararg type (convention (b)). The base-method null
    // check is the C# `NullReferenceException` at the first `baseMethod`
    // dereference -- ported as `std::invalid_argument` (the DefaultParameter
    // convention).
    VarArgInstanceMethod(std::shared_ptr<IMethod> baseMethod,
                         std::vector<ITypePtr> varArgTypes)
        : baseMethod_(std::move(baseMethod))
    {
        if (!baseMethod_) {
            throw std::invalid_argument("baseMethod");
        }
        std::vector<const IParameter*> baseParams = baseMethod_->Parameters();
        if (!baseParams.empty()) {
            baseParams.pop_back();  // the trailing `__arglist` sentinel
        }
        // The regular parameters first (each an ALIASING shared_ptr over the
        // base method's own parameter object -- the C# copies the IParameter
        // reference into the new array; `baseMethod_` keeps the objects
        // alive), then one freshly created DefaultParameter per vararg type
        // (owned here, convention (b)).
        parameters_.reserve(baseParams.size() + varArgTypes.size());
        for (const IParameter* p : baseParams) {
            parameters_.push_back(std::shared_ptr<const IParameter>(
                std::shared_ptr<const IParameter>(), p));
        }
        for (ITypePtr& varArg : varArgTypes) {
            parameters_.push_back(std::make_shared<Implementation::DefaultParameter>(
                std::move(varArg), std::string(), this));
        }
    }

    // The C# `public IMethod BaseMethod => baseMethod`.
    const IMethod* BaseMethod() const { return baseMethod_.get(); }

    // The C# `public int RegularParameterCount =>
    // baseMethod.Parameters.Count - 1`.
    int RegularParameterCount() const {
        return static_cast<int>(baseMethod_->Parameters().size()) - 1;
    }

    // --- IParameterizedMember ---

    // The C# `public IReadOnlyList<IParameter> Parameters => parameters` -- the
    // wrapper's own list (the base's sentinel replaced by the vararg types).
    std::vector<const IParameter*> Parameters() const override {
        std::vector<const IParameter*> snapshot;
        snapshot.reserve(parameters_.size());
        for (const std::shared_ptr<const IParameter>& p : parameters_) {
            snapshot.push_back(p.get());
        }
        return snapshot;
    }

    // --- ISymbol ---

    // The C# `SymbolKind SymbolKind => baseMethod.SymbolKind`. Globally
    // qualified (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return baseMethod_->SymbolKind();
    }
    // The C# `string Name => baseMethod.Name`.
    std::string Name() const override { return baseMethod_->Name(); }

    // --- INamedElement ---

    // The C# `string FullName => baseMethod.FullName`.
    std::string FullName() const override { return baseMethod_->FullName(); }
    // The C# `string ReflectionName => baseMethod.ReflectionName`.
    std::string ReflectionName() const override {
        return baseMethod_->ReflectionName();
    }
    // The C# `string Namespace => baseMethod.Namespace`.
    std::string Namespace() const override { return baseMethod_->Namespace(); }

    // --- ICompilationProvider ---

    // The C# `ICompilation Compilation => baseMethod.Compilation`.
    const ICompilation& Compilation() const override {
        return baseMethod_->Compilation();
    }

    // --- IEntity ---

    // The C# `EntityHandle MetadataToken => baseMethod.MetadataToken`.
    std::uint32_t MetadataToken() const override {
        return baseMethod_->MetadataToken();
    }
    // The C# `ITypeDefinition DeclaringTypeDefinition =>
    // baseMethod.DeclaringTypeDefinition`.
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return baseMethod_->DeclaringTypeDefinition();
    }
    // The C# `IType DeclaringType => baseMethod.DeclaringType`.
    ITypePtr DeclaringType() const override {
        return baseMethod_->DeclaringType();
    }
    // The C# `IModule ParentModule => baseMethod.ParentModule`.
    const IModule* ParentModule() const override {
        return baseMethod_->ParentModule();
    }
    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // baseMethod.GetAttributes()`.
    std::vector<const IAttribute*> GetAttributes() const override {
        return baseMethod_->GetAttributes();
    }
    // The C# `bool IEntity.HasAttribute(KnownAttribute) =>
    // baseMethod.HasAttribute(...)`.
    bool HasAttribute(KnownAttribute attribute) const override {
        return baseMethod_->HasAttribute(attribute);
    }
    // The C# `IAttribute IEntity.GetAttribute(KnownAttribute) =>
    // baseMethod.GetAttribute(...)`.
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return baseMethod_->GetAttribute(attribute);
    }
    // The C# `Accessibility Accessibility => baseMethod.Accessibility`.
    // Globally qualified (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return baseMethod_->Accessibility();
    }
    // The C# `bool IsStatic => baseMethod.IsStatic`.
    bool IsStatic() const override { return baseMethod_->IsStatic(); }
    // The C# `bool IsAbstract => baseMethod.IsAbstract`.
    bool IsAbstract() const override { return baseMethod_->IsAbstract(); }
    // The C# `bool IsSealed => baseMethod.IsSealed`.
    bool IsSealed() const override { return baseMethod_->IsSealed(); }

    // --- IMember ---

    // The C# `IMember MemberDefinition => baseMethod.MemberDefinition`.
    const IMember* MemberDefinition() const override {
        return baseMethod_->MemberDefinition();
    }
    // The C# `IType ReturnType => baseMethod.ReturnType`.
    const IType& ReturnType() const override { return baseMethod_->ReturnType(); }
    // The C# `IEnumerable<IMember> ExplicitlyImplementedInterfaceMembers =>
    // baseMethod.ExplicitlyImplementedInterfaceMembers`.
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers()
        const override {
        return baseMethod_->ExplicitlyImplementedInterfaceMembers();
    }
    // The C# `bool IsExplicitInterfaceImplementation =>
    // baseMethod.IsExplicitInterfaceImplementation`.
    bool IsExplicitInterfaceImplementation() const override {
        return baseMethod_->IsExplicitInterfaceImplementation();
    }
    // The C# `bool IsVirtual => baseMethod.IsVirtual`.
    bool IsVirtual() const override { return baseMethod_->IsVirtual(); }
    // The C# `bool IsOverride => baseMethod.IsOverride`.
    bool IsOverride() const override { return baseMethod_->IsOverride(); }
    // The C# `bool IsOverridable => baseMethod.IsOverridable`.
    bool IsOverridable() const override { return baseMethod_->IsOverridable(); }
    // The C# `TypeParameterSubstitution Substitution =>
    // baseMethod.Substitution`.
    const TypeParameterSubstitution* Substitution() const override {
        return baseMethod_->Substitution();
    }
    // The C# `IMethod Specialize(TypeParameterSubstitution)` -- a FRESH
    // wrapper over the specialized base (convention (d)): the vararg types are
    // the wrapper's own trailing parameters, substituted through the same
    // substitution (the C# `parameters.Skip(baseMethod.Parameters.Count - 1)
    // .Select(p => p.Type.AcceptVisitor(substitution))`). The COVARIANT
    // `const IMethod*` form is the final overrider for the single inherited
    // `Specialize` slot (single inheritance -- no diamond here).
    const IMethod* Specialize(const TypeParameterSubstitution* substitution)
        const override;

    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)`
    // -- the VarArgInstanceMethod-RTTI gate, then the base-method equality
    // under the normalization (convention (c)).
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization)
        const override {
        const auto* other = dynamic_cast<const VarArgInstanceMethod*>(obj);
        return other != nullptr
            && baseMethod_->Equals(other->baseMethod_.get(), typeNormalization);
    }

    // --- IMethod ---

    // The C# `IEnumerable<IAttribute> IMethod.GetReturnTypeAttributes() =>
    // baseMethod.GetReturnTypeAttributes()`.
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return baseMethod_->GetReturnTypeAttributes();
    }
    // The C# `bool IMethod.ReturnTypeIsRefReadOnly =>
    // baseMethod.ReturnTypeIsRefReadOnly`.
    bool ReturnTypeIsRefReadOnly() const override {
        return baseMethod_->ReturnTypeIsRefReadOnly();
    }
    // The C# `bool IMethod.ThisIsRefReadOnly => baseMethod.ThisIsRefReadOnly`.
    bool ThisIsRefReadOnly() const override {
        return baseMethod_->ThisIsRefReadOnly();
    }
    // The C# `bool IMethod.IsInitOnly => baseMethod.IsInitOnly`.
    bool IsInitOnly() const override { return baseMethod_->IsInitOnly(); }
    // The C# `IReadOnlyList<ITypeParameter> TypeParameters =>
    // baseMethod.TypeParameters`.
    std::vector<const ITypeParameter*> TypeParameters() const override {
        return baseMethod_->TypeParameters();
    }
    // The C# `IReadOnlyList<IType> TypeArguments => baseMethod.TypeArguments`.
    std::vector<ITypePtr> TypeArguments() const override {
        return baseMethod_->TypeArguments();
    }
    // The C# `bool IsExtensionMethod => baseMethod.IsExtensionMethod`.
    bool IsExtensionMethod() const override {
        return baseMethod_->IsExtensionMethod();
    }
    // The C# `bool IMethod.IsLocalFunction => baseMethod.IsLocalFunction`.
    bool IsLocalFunction() const override {
        return baseMethod_->IsLocalFunction();
    }
    // The C# `bool IsConstructor => baseMethod.IsConstructor`.
    bool IsConstructor() const override { return baseMethod_->IsConstructor(); }
    // The C# `bool IsDestructor => baseMethod.IsDestructor`.
    bool IsDestructor() const override { return baseMethod_->IsDestructor(); }
    // The C# `bool IsOperator => baseMethod.IsOperator`.
    bool IsOperator() const override { return baseMethod_->IsOperator(); }
    // The C# `bool HasBody => baseMethod.HasBody`.
    bool HasBody() const override { return baseMethod_->HasBody(); }
    // The C# `bool IsAccessor => baseMethod.IsAccessor`.
    bool IsAccessor() const override { return baseMethod_->IsAccessor(); }
    // The C# `IMember AccessorOwner => baseMethod.AccessorOwner`.
    const IMember* AccessorOwner() const override {
        return baseMethod_->AccessorOwner();
    }
    // The C# `MethodSemanticsAttributes AccessorKind =>
    // baseMethod.AccessorKind`.
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind()
        const override {
        return baseMethod_->AccessorKind();
    }
    // The C# `IMethod ReducedFrom => baseMethod.ReducedFrom`.
    const IMethod* ReducedFrom() const override {
        return baseMethod_->ReducedFrom();
    }

    // The C# `public override string ToString()` -- the `[KindDeclaringType.
    // Name(``N(params):ReturnType]` render (convention (e)).
    std::string ToString() const;

    // The C# `public override int GetHashCode()` (convention (c)): the base
    // method's hash (the pointer-identity hash over the shared base handle).
    int GetHashCode() const {
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(
                                    baseMethod_.get())
                                & 0x7fffffff);
    }

private:
    std::shared_ptr<IMethod> baseMethod_;
    // The wrapper's own parameter list: the base's regular parameters (aliased
    // non-owning) followed by the freshly created vararg DefaultParameters
    // (owned). The regular entries keep the BASE method alive through
    // `baseMethod_` exactly as the C# array-of-references does.
    std::vector<std::shared_ptr<const IParameter>> parameters_;
    // The keep-alive registry for the `Specialize`-created rewraps (convention
    // (d)).
    mutable std::vector<std::shared_ptr<VarArgInstanceMethod>> rewraps_;
};

} // namespace ILSpy::Decompiler::TypeSystem
