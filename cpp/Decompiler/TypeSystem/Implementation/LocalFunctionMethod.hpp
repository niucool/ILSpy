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
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/LocalFunctionMethod.cs --
// `class LocalFunctionMethod : IMethod` -- "A local function has zero or more
// compiler-generated parameters added at the end." The C# class is the wrapper the
// decompiler puts around the metadata method when it detects a local function
// (`LocalFunctionDecompiler`): it hides the compiler-generated parameter /
// type-parameter tails behind the source-level signature, renames the method to the
// local function's source name, and reports the local-function shape
// (`IsLocalFunction` true, `ReducedFrom` the unwrapped metadata method, `IsStatic`
// unconditionally true). The ported consumers are the `member is LocalFunctionMethod`
// RTTI checks in `TypeSystemAstBuilder.GetMemberModifiers` (the Convert Modifiers
// region, the next TypeSystemAstBuilder slice) and `CSharpAmbience` (the
// `ShowDeclaringType` gate), plus the future `LocalFunctionDecompiler` /
// `AssignVariableNames` / `DelegateConstruction` IL-transform paths.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly IMethod baseMethod` (a GC reference the wrapper keeps alive)
//      ports to an OWNING `std::shared_ptr<IMethod>` (the `SpecializedMember::baseMember_`
//      precedent). The ctor takes the shared handle by value and throws
//      `std::invalid_argument` on a null base (the C# `ArgumentNullException`; the
//      `DefaultParameter` null-type-guard convention).
//  (b) `IsLocalFunction()` returns TRUE unconditionally -- the only ported `IMethod`
//      implementation that does (the C# `public bool IsLocalFunction => true`).
//  (c) `IsStatic()` returns TRUE unconditionally, EVEN when the base method is an
//      instance method and even when the wrapper was constructed with
//      `isStaticLocalFunction == false` -- the C# comment verbatim: "We consider local
//      functions as always static, because they do not have a 'this parameter'. Even
//      local functions in instance methods capture this." This is the load-bearing
//      quirk `GetMemberModifiers` keys on (a local function never emits the `static`
//      modifier from the base method's flags; the `IsStaticLocalFunction` flag decides).
//  (d) `ReducedFrom()` returns THE BASE METHOD ITSELF (`baseMethod_.get()`), NOT
//      `baseMethod->ReducedFrom()` -- the C# `public IMethod ReducedFrom => baseMethod;`
//      (the wrapper IS the reduction of the metadata method, so the reduction source is
//      the base, not whatever the base might itself be reduced from).
//  (e) The C# lazily-memoized `parameters` / `typeParameters` / `typeArguments` locals
//      (`baseMethod.Parameters.SkipLast(N)` etc.) port to compute-per-call `SkipLast`
//      snapshots: the memoization is unobservable through the by-value snapshot
//      contract (and compute-per-call stays correct against a mutated test stub,
//      unlike the C# first-access freeze). `SkipLast` semantics: a non-positive count
//      yields the whole list; a count at or above the size yields the empty list.
//  (f) `Name()` / `FullName()` are the wrapper's OWN name (ctor-set; `SetName` models
//      the C# `Name { get; set; }` setter) -- `FullName => Name`; `ReflectionName` /
//      `Namespace` forward to the base. `Name()` is the single final overrider for the
//      `ISymbol::Name` / `INamedElement::Name` diamond (the D374 precedent).
//  (g) `MemberDefinition()`: the C# `ReferenceEquals(baseMethodDefinition, baseMethod)`
//      this-check ports to the pointer comparison `baseMethod_->MemberDefinition() ==
//      baseMethod_.get()`; the rewrap branch builds a fresh wrapper over the base's
//      definition. The C# `(IMethod)baseMethod.MemberDefinition` HARD cast would throw
//      `InvalidCastException` for a definition that is not an `IMethod` (or null) --
//      impossible for a real method; the port's `dynamic_cast` null result takes the
//      documented safe fallback of passing the degenerate result through unchanged (the
//      D516 convention). The rewrap's base handle uses the ALIASING `shared_ptr`
//      constructor (co-owning this wrapper's own `baseMethod_` control block while
//      pointing at the definition): an unspecialized method IS its definition and a
//      specialized method owns its definition through its own base member, so
//      co-owning the base keeps the definition reachable exactly as the C# GC
//      reference does (the `LiftedUserDefinedOperator` MemberDefinition precedent).
//  (h) `Specialize()` builds a FRESH wrapper over the specialized base method (the C#
//      allocates per call; `IsLocalFunction` / the name / the flags carry over). Because
//      `IMethod::Specialize` returns a NON-OWNING handle ("the type system owns the
//      newly-specialized method", the `IMember::Specialize` contract) while the port has
//      no shared owning-`Specialize` machinery yet, EVERY rewrap this wrapper creates
//      is kept alive for THIS wrapper's lifetime through a mutable owning vector (the
//      C# GC owns each fresh wrapper; here the source wrapper owns its results). Two
//      calls produce two distinct wrappers that are `Equals`-equal -- the C#
//      fresh-per-call identity semantics, with a source-tied lifetime instead of the GC.
//  (i) `Equals(IMember, TypeVisitor)`: the `LocalFunctionMethod`-RTTI gate, then the
//      base-method equality under the normalization AND the two generated-counts AND
//      the `IsStaticLocalFunction` flag. The C# `override bool Equals(object)` is the
//      SAME comparison without a normalization -- the port's single 2-arg override
//      covers both (a caller passes a null normalization for the object.Equals form).
//  (j) `GetHashCode()` is a plain member (the port's `IMember` carries no GetHashCode
//      virtual): the C# `baseMethod.GetHashCode()` is the base's `object.GetHashCode`
//      identity hash, ported as the pointer-identity hash (the `SpecializedMember`
//      GetHashCode precedent).
//  (k) `ToString()` is DEFERRED (the C# custom format renders `ReducedFrom` through the
//      base method's object `ToString`, which the port's `IMethod` surface does not
//      carry -- the `SpecializedMember` ToString deferral precedent); lands with the
//      shared `IType` / `IMember` `ToString` design.
//  (l) The C# `internal` accessors (`IsStaticLocalFunction`,
//      `NumberOfCompilerGeneratedParameters`, `NumberOfCompilerGeneratedTypeParameters`)
//      are widened to public for direct TDD (the internal-widening convention). The C#
//      class is UNSEALED (`class LocalFunctionMethod : IMethod`); the port is not
//      `final`.
//  (m) The name-hiding qualifications: the inherited `SymbolKind()` / `Accessibility()`
//      member names hide the namespace-scope enums in the class body (the D372
//      cross-scope crux), so those two overrides carry the globally-qualified return
//      types. `MethodSemanticsAttributes` / `TypeParameterSubstitution` / `TypeVisitor`
//      do not collide with any member name and need no qualification.

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// Port of the C# `class LocalFunctionMethod : IMethod` -- the local-function wrapper
// (see the header comment). Derives `IMethod` directly (single inheritance; the
// `IEntity` `ISymbol` + `ICompilationProvider` + `INamedElement` diamond is resolved by
// one override per name, the `LookupMethod` stub precedent). Everything is inline (the
// class is header-only; the only non-trivial dependencies -- `TypeParameterSubstitution`
// / `TypeVisitor` -- appear as pointers).
class LocalFunctionMethod : public IMethod {
public:
    // The C# `public LocalFunctionMethod(IMethod baseMethod, string name, bool
    // isStaticLocalFunction, int numberOfCompilerGeneratedParameters, int
    // numberOfCompilerGeneratedTypeParameters)`. The base-method null check throws
    // (convention (a)); the name is the wrapper's OWN (convention (f)).
    LocalFunctionMethod(std::shared_ptr<IMethod> baseMethod, std::string name,
                        bool isStaticLocalFunction,
                        int numberOfCompilerGeneratedParameters,
                        int numberOfCompilerGeneratedTypeParameters)
        : baseMethod_(std::move(baseMethod)),
          name_(std::move(name)),
          isStaticLocalFunction_(isStaticLocalFunction),
          numberOfCompilerGeneratedParameters_(numberOfCompilerGeneratedParameters),
          numberOfCompilerGeneratedTypeParameters_(numberOfCompilerGeneratedTypeParameters)
    {
        if (!baseMethod_) {
            throw std::invalid_argument("baseMethod");
        }
    }

    // --- The C# `internal` own surface (widened to public, convention (l)) ---

    // The C# `internal bool IsStaticLocalFunction { get; }` -- whether the source-level
    // local function was declared `static`.
    bool IsStaticLocalFunction() const { return isStaticLocalFunction_; }

    // The C# `internal int NumberOfCompilerGeneratedParameters { get; }` -- how many
    // compiler-generated parameters were appended to the metadata method's end (the
    // closure-display hoisted variables); the wrapper hides them from the signature.
    int NumberOfCompilerGeneratedParameters() const { return numberOfCompilerGeneratedParameters_; }

    // The C# `internal int NumberOfCompilerGeneratedTypeParameters { get; }` -- how many
    // compiler-generated type parameters were appended (the cached-delegate machinery's
    // extra `T` on generic local functions).
    int NumberOfCompilerGeneratedTypeParameters() const
    {
        return numberOfCompilerGeneratedTypeParameters_;
    }

    // --- ISymbol ---

    // The C# `SymbolKind SymbolKind => baseMethod.SymbolKind`. Globally qualified (the
    // D372 crux -- the inherited `ISymbol::SymbolKind` member shadows the enum).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return baseMethod_->SymbolKind();
    }

    // The C# `public string Name { get; set; }` -- the wrapper's OWN name (the local
    // function's source name). The single final overrider for the
    // `ISymbol::Name` / `INamedElement::Name` diamond.
    std::string Name() const override { return name_; }

    // The C# setter half of `Name { get; set; }`.
    void SetName(std::string name) { name_ = std::move(name); }

    // --- INamedElement ---

    // The C# `public string FullName => Name`.
    std::string FullName() const override { return name_; }

    // The C# `public string ReflectionName => baseMethod.ReflectionName`.
    std::string ReflectionName() const override { return baseMethod_->ReflectionName(); }

    // The C# `public string Namespace => baseMethod.Namespace`.
    std::string Namespace() const override { return baseMethod_->Namespace(); }

    // --- ICompilationProvider ---

    // The C# `public ICompilation Compilation => baseMethod.Compilation`.
    const ICompilation& Compilation() const override { return baseMethod_->Compilation(); }

    // --- IEntity ---

    // The C# `public EntityHandle MetadataToken => baseMethod.MetadataToken`.
    std::uint32_t MetadataToken() const override { return baseMethod_->MetadataToken(); }

    // The C# `public ITypeDefinition DeclaringTypeDefinition =>
    // baseMethod.DeclaringTypeDefinition`.
    const ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return baseMethod_->DeclaringTypeDefinition();
    }

    // The C# `public IType DeclaringType => baseMethod.DeclaringType`.
    ITypePtr DeclaringType() const override { return baseMethod_->DeclaringType(); }

    // The C# `public IModule ParentModule => baseMethod.ParentModule`.
    const IModule* ParentModule() const override { return baseMethod_->ParentModule(); }

    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // baseMethod.GetAttributes()`.
    std::vector<const IAttribute*> GetAttributes() const override
    {
        return baseMethod_->GetAttributes();
    }

    // The C# `bool IEntity.HasAttribute(KnownAttribute) => baseMethod.HasAttribute(...)`.
    bool HasAttribute(KnownAttribute attribute) const override
    {
        return baseMethod_->HasAttribute(attribute);
    }

    // The C# `IAttribute IEntity.GetAttribute(KnownAttribute) =>
    // baseMethod.GetAttribute(...)`.
    const IAttribute* GetAttribute(KnownAttribute attribute) const override
    {
        return baseMethod_->GetAttribute(attribute);
    }

    // The C# `public Accessibility Accessibility => baseMethod.Accessibility`. Globally
    // qualified (the D372 crux -- the inherited `IEntity::Accessibility` member shadows
    // the enum).
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return baseMethod_->Accessibility();
    }

    // The C# `public bool IsStatic => true` -- unconditionally true, the load-bearing
    // quirk (convention (c)): local functions do not have a "this parameter", so even
    // local functions in instance methods are considered static.
    bool IsStatic() const override { return true; }

    // The C# `public bool IsAbstract => baseMethod.IsAbstract`.
    bool IsAbstract() const override { return baseMethod_->IsAbstract(); }

    // The C# `public bool IsSealed => baseMethod.IsSealed`.
    bool IsSealed() const override { return baseMethod_->IsSealed(); }

    // --- IMember ---

    // The C# `public IMember MemberDefinition` -- `this` when the base is its own
    // definition (the `ReferenceEquals` check), else a fresh wrapper over the base's
    // definition (convention (g)).
    const IMember* MemberDefinition() const override
    {
        const IMethod* baseMethodDefinition =
            dynamic_cast<const IMethod*>(baseMethod_->MemberDefinition());
        if (baseMethodDefinition == baseMethod_.get()) {
            return this;
        }
        if (baseMethodDefinition == nullptr) {
            return baseMethod_->MemberDefinition();
        }
        auto rewrap = std::make_shared<LocalFunctionMethod>(
            std::shared_ptr<IMethod>(baseMethod_, const_cast<IMethod*>(baseMethodDefinition)),
            name_, isStaticLocalFunction_, numberOfCompilerGeneratedParameters_,
            numberOfCompilerGeneratedTypeParameters_);
        rewraps_.push_back(std::move(rewrap));
        return rewraps_.back().get();
    }

    // The C# `public IType ReturnType => baseMethod.ReturnType`.
    const IType& ReturnType() const override { return baseMethod_->ReturnType(); }

    // The C# `IEnumerable<IMember> IMember.ExplicitlyImplementedInterfaceMembers =>
    // baseMethod.ExplicitlyImplementedInterfaceMembers`.
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return baseMethod_->ExplicitlyImplementedInterfaceMembers();
    }

    // The C# `bool IMember.IsExplicitInterfaceImplementation =>
    // baseMethod.IsExplicitInterfaceImplementation`.
    bool IsExplicitInterfaceImplementation() const override
    {
        return baseMethod_->IsExplicitInterfaceImplementation();
    }

    // The C# `public bool IsVirtual => baseMethod.IsVirtual`.
    bool IsVirtual() const override { return baseMethod_->IsVirtual(); }

    // The C# `public bool IsOverride => baseMethod.IsOverride`.
    bool IsOverride() const override { return baseMethod_->IsOverride(); }

    // The C# `public bool IsOverridable => baseMethod.IsOverridable`.
    bool IsOverridable() const override { return baseMethod_->IsOverridable(); }

    // The C# `public TypeParameterSubstitution Substitution => baseMethod.Substitution`.
    const TypeParameterSubstitution* Substitution() const override
    {
        return baseMethod_->Substitution();
    }

    // The C# `IMethod Specialize(TypeParameterSubstitution)` -- a FRESH wrapper over
    // the specialized base method (convention (h)). The covariant `const IMethod*`
    // return is the final overrider for the `IMember::Specialize` / `IMethod::Specialize`
    // slots (the `IMethod` header convention (e)).
    const IMethod* Specialize(const TypeParameterSubstitution* substitution) const override
    {
        const IMethod* specializedBase = baseMethod_->Specialize(substitution);
        auto rewrap = std::make_shared<LocalFunctionMethod>(
            std::shared_ptr<IMethod>(baseMethod_, const_cast<IMethod*>(specializedBase)),
            name_, isStaticLocalFunction_, numberOfCompilerGeneratedParameters_,
            numberOfCompilerGeneratedTypeParameters_);
        rewraps_.push_back(std::move(rewrap));
        return rewraps_.back().get();
    }

    // The C# `public bool Equals(IMember obj, TypeVisitor typeNormalization)` (and the
    // `Equals(object)` twin, convention (i)).
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override
    {
        const auto* other = dynamic_cast<const LocalFunctionMethod*>(obj);
        if (other == nullptr) {
            return false;
        }
        return baseMethod_->Equals(other->baseMethod_.get(), typeNormalization)
            && numberOfCompilerGeneratedParameters_ == other->numberOfCompilerGeneratedParameters_
            && numberOfCompilerGeneratedTypeParameters_
                == other->numberOfCompilerGeneratedTypeParameters_
            && isStaticLocalFunction_ == other->isStaticLocalFunction_;
    }

    // The C# `public override int GetHashCode() => baseMethod.GetHashCode()` (convention
    // (j)).
    int GetHashCode() const
    {
        return static_cast<int>(
            std::hash<const void*>{}(static_cast<const void*>(baseMethod_.get())));
    }

    // --- IParameterizedMember ---

    // The C# `public IReadOnlyList<IParameter> Parameters` -- the base method's
    // parameter list with the compiler-generated tail hidden (convention (e)).
    std::vector<const IParameter*> Parameters() const override
    {
        return SkipLast(baseMethod_->Parameters(), numberOfCompilerGeneratedParameters_);
    }

    // --- IMethod ---

    // The C# `IEnumerable<IAttribute> IMethod.GetReturnTypeAttributes() =>
    // baseMethod.GetReturnTypeAttributes()`.
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override
    {
        return baseMethod_->GetReturnTypeAttributes();
    }

    // The C# `bool IMethod.ReturnTypeIsRefReadOnly => baseMethod.ReturnTypeIsRefReadOnly`.
    bool ReturnTypeIsRefReadOnly() const override
    {
        return baseMethod_->ReturnTypeIsRefReadOnly();
    }

    // The C# `bool IMethod.IsInitOnly => baseMethod.IsInitOnly`.
    bool IsInitOnly() const override { return baseMethod_->IsInitOnly(); }

    // The C# `bool IMethod.ThisIsRefReadOnly => baseMethod.ThisIsRefReadOnly`.
    bool ThisIsRefReadOnly() const override { return baseMethod_->ThisIsRefReadOnly(); }

    // The C# `public IReadOnlyList<ITypeParameter> TypeParameters` -- the base method's
    // type parameters with the compiler-generated tail hidden (convention (e)).
    std::vector<const ITypeParameter*> TypeParameters() const override
    {
        return SkipLast(baseMethod_->TypeParameters(),
                        numberOfCompilerGeneratedTypeParameters_);
    }

    // The C# `public IReadOnlyList<IType> TypeArguments` -- the base method's type
    // arguments with the compiler-generated tail hidden (convention (e)).
    std::vector<ITypePtr> TypeArguments() const override
    {
        return SkipLast(baseMethod_->TypeArguments(),
                        numberOfCompilerGeneratedTypeParameters_);
    }

    // The C# `public bool IsExtensionMethod => baseMethod.IsExtensionMethod`.
    bool IsExtensionMethod() const override { return baseMethod_->IsExtensionMethod(); }

    // The C# `public bool IsLocalFunction => true` -- unconditionally (convention (b)).
    bool IsLocalFunction() const override { return true; }

    // The C# `public bool IsConstructor => baseMethod.IsConstructor`.
    bool IsConstructor() const override { return baseMethod_->IsConstructor(); }

    // The C# `public bool IsDestructor => baseMethod.IsDestructor`.
    bool IsDestructor() const override { return baseMethod_->IsDestructor(); }

    // The C# `public bool IsOperator => baseMethod.IsOperator`.
    bool IsOperator() const override { return baseMethod_->IsOperator(); }

    // The C# `public bool HasBody => baseMethod.HasBody`.
    bool HasBody() const override { return baseMethod_->HasBody(); }

    // The C# `public bool IsAccessor => baseMethod.IsAccessor`.
    bool IsAccessor() const override { return baseMethod_->IsAccessor(); }

    // The C# `public IMember AccessorOwner => baseMethod.AccessorOwner`.
    const IMember* AccessorOwner() const override { return baseMethod_->AccessorOwner(); }

    // The C# `public MethodSemanticsAttributes AccessorKind => baseMethod.AccessorKind`.
    MethodSemanticsAttributes AccessorKind() const override
    {
        return baseMethod_->AccessorKind();
    }

    // The C# `public IMethod ReducedFrom => baseMethod` -- THE BASE METHOD ITSELF, not
    // whatever the base is itself reduced from (convention (d)).
    const IMethod* ReducedFrom() const override { return baseMethod_.get(); }

private:
    // The C# `Enumerable.SkipLast` (convention (e)).
    template <typename T>
    static std::vector<T> SkipLast(std::vector<T> list, int count)
    {
        if (count <= 0) {
            return list;
        }
        if (static_cast<size_t>(count) >= list.size()) {
            return {};
        }
        list.resize(list.size() - static_cast<size_t>(count));
        return list;
    }

    // The C# `readonly IMethod baseMethod` (convention (a)) -- the unwrapped metadata
    // method with the compiler-generated tails still present.
    std::shared_ptr<IMethod> baseMethod_;
    // The C# `public string Name { get; set; }` storage.
    std::string name_;
    // The C# `internal bool IsStaticLocalFunction { get; }`.
    bool isStaticLocalFunction_;
    // The C# `internal int NumberOfCompilerGeneratedParameters { get; }`.
    int numberOfCompilerGeneratedParameters_;
    // The C# `internal int NumberOfCompilerGeneratedTypeParameters { get; }`.
    int numberOfCompilerGeneratedTypeParameters_;
    // Every rewrap this wrapper created (`MemberDefinition` / `Specialize`), kept alive
    // for this wrapper's lifetime (convention (h) -- the C# GC owns each fresh wrapper;
    // here the source wrapper owns its results because `IMethod::Specialize` returns a
    // non-owning handle).
    mutable std::vector<std::shared_ptr<LocalFunctionMethod>> rewraps_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
