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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/FakeMember.cs -- the
// base class for fake members (`abstract class FakeMember : IMember`) plus its
// four concrete leaves (`FakeField : FakeMember, IField`, `FakeMethod :
// FakeMember, IMethod`, `FakeProperty : FakeMember, IProperty`, `FakeEvent :
// FakeMember, IEvent`): the placeholder members the resolver and the
// `MetadataModule` reference-resolution fallbacks construct when metadata
// names a member no module can resolve, and `FakeMethod.CreateDummyConstructor`
// -- the public parameterless constructor `MetadataTypeDefinition.Methods`
// adds to structs and enums that declare none (this slice's consumer).
//
// KEY PORT CONVENTIONS:
//  (a) The C# settable auto-properties (`Name` / `DeclaringType` / `ReturnType`
//      / `Accessibility` / `IsStatic`, plus the per-leaf `TypeParameters` /
//      `Parameters` / `IsIndexer` / `Getter` / `Setter` / the event accessors /
//      `AccessorOwner` / `AccessorKind`) port to getter-override + `Set*`
//      method pairs (the LocalFunctionMethod `SetName` convention). The C#
//      `Name { get; set; }` NULL default maps to the empty string (the
//      null=="" equivalence; the C# null is observable only through the C#
//      null itself -- a `FullName` of a nameless, undeclared fake renders
//      identically). `FakeProperty.Parameters`' C# NULL default (distinct from
//      `FakeMethod`'s empty-list default!) maps to the empty vector -- the
//      one C# reader that distinguishes them, `FakeProperty.ToString()`, is
//      deferred (convention (h)) so the null state is unobservable in the
//      port.
//  (b) The C# `readonly ICompilation compilation` ports to a REFERENCE member
//      (`const ICompilation& compilation_`, the AbstractTypeParameter
//      convention); the ctor's `ArgumentNullException` maps to
//      `std::invalid_argument`.
//  (c) The accessor slots (`Getter` / `Setter` / the event trio /
//      `AccessorOwner`) are NON-OWNING raw pointers (the C# references; the
//      pointed-at methods are module- or test-owned -- the C# GC reference the
//      port models as a non-owning handle).
//  (d) `Specialize` routes through the `SpecializedX.Create` factories
//      faithfully (LANDED with the owning-Specialize design): the `Identity`
//      substitution and the declaring-type-`TypeParameterCount == 0` (plus,
//      for `FakeMethod`, the own-`TypeParameters`-empty) cases return THE SAME
//      instance (gold-pinned: `ReferenceEquals(spec, fake)` is true); the
//      general arm constructs a fresh owning `SpecializedX` kept alive in the
//      per-fake keep-alive registry (the C# GC root; the
//      `VarArgInstanceMethod` rewrap-registry precedent). The returned
//      `IMember*` view for the field/property/event forms is the fake's own
//      concrete-interface subobject (the `IField` / `IProperty` / `IEvent`
//      view) -- BOTH arms take the same view, so fresh-vs-same-instance
//      pointer comparisons stay correct; the C#
//      `NullReferenceException` of a null `DeclaringType` in the Create arm
//      chain maps to `std::runtime_error` carrying the .NET message, the
//      XamlContext NRE convention).
//  (e) `Equals` is the C# DEFAULT reference equality (`bool IMember.Equals(IMember,
//      TypeVisitor) => Equals(obj)` -- `FakeMember` overrides no `Equals`):
//      the port compares pointer identity (`obj == this`). `GetHashCode` is a
//      plain member (the pointer-identity hash, the SpecializedMember
//      precedent).
//  (f) The `FullNameOf` / `NamespaceOf` IType-surface reads (`DeclaringType.
//      FullName` / `.Namespace` in the `FullName` / `ReflectionName` /
//      `Namespace` overrides) are the ILAmbience.cpp file-local helpers copied
//      next to this their third consumer (the port's minimal `IType` carries
//      neither member -- the documented copy-along convention).
//  (g) The two-`IMember`-subobject diamond (`FakeField : FakeMember, IField`
//      etc.) is resolved by one override per shared name delegating to the
//      `FakeMember::` qualified call (sub A's override -- the SpecializedField
//      / SpecializedMethod precedent); the `IMethod`-covariant `Specialize`
//      override is the final overrider for both slots.
//  (h) `FakeProperty.ToString()` is DEFERRED (the C# render goes through the
//      concrete `IType`'s own `ToString` -- "020000FB System.Int32" for a
//      definition -- and the `IParameter` `ToString` chain, none of which the
//      port's `IType` surface carries; the LocalFunctionMethod `ToString`
//      deferral precedent; lands with the shared `IType`/`IMember` `ToString`
//      design). The C# NULL-default-`Parameters` NRE inside it (convention
//      (a)) is therefore unreachable in the port.

#pragma once

#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The abstract base class for fake members (see the header comment). Pure
// virtual only in `SymbolKind()` and `Specialize` (the two abstract members
// each leaf owns).
class FakeMember : public IMember {
public:
    // The C# `protected FakeMember(ICompilation compilation)`; the null check
    // is the ctor's `ArgumentNullException` (convention (b)).
    explicit FakeMember(const ICompilation& compilation)
        : compilation_(compilation)
    {
    }

    // --- The C# settable auto-properties (convention (a)) ---

    // The C# `public string Name { get; set; }` (the null default maps to
    // "").
    std::string Name() const override { return name_; }
    void SetName(std::string name) { name_ = std::move(name); }

    // The C# `public IType DeclaringType { get; set; }`.
    ITypePtr DeclaringType() const override { return declaringType_; }
    void SetDeclaringType(ITypePtr declaringType) {
        declaringType_ = std::move(declaringType);
    }

    // The C# `public IType ReturnType { get; set; } = SpecialType.UnknownType`.
    const IType& ReturnType() const override { return *returnType_; }
    void SetReturnType(ITypePtr returnType) { returnType_ = std::move(returnType); }

    // The C# `public Accessibility Accessibility { get; set; } =
    // Accessibility.Public`. Globally qualified (the D372 crux -- the
    // inherited `IEntity::Accessibility` member name hides the enum).
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return accessibility_;
    }
    void SetAccessibility(::ILSpy::Decompiler::TypeSystem::Accessibility value) {
        accessibility_ = value;
    }

    // The C# `public bool IsStatic { get; set; }`.
    bool IsStatic() const override { return isStatic_; }
    void SetIsStatic(bool value) { isStatic_ = value; }

    // --- The delegated/fixed IMember surface ---

    // The C# `IMember IMember.MemberDefinition => this`.
    const IMember* MemberDefinition() const override { return this; }
    // The C# `IEnumerable<IMember> IMember.ExplicitlyImplementedInterfaceMembers
    // => EmptyList<IMember>.Instance`.
    std::vector<const IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    // The C# `bool IMember.IsExplicitInterfaceImplementation => false`.
    bool IsExplicitInterfaceImplementation() const override { return false; }
    // The C# `bool IMember.IsVirtual => false`.
    bool IsVirtual() const override { return isVirtual_; }
    // Configurable `IsVirtual` for the call-builder tests (the
    // BaseReferenceExpression arm of the C# GetRequiredTransformationsForCall
    // requireTarget block reads the resolved method's IsVirtual). The default
    // false preserves the prior hardcoded behavior (the additive-setter
    // convention).
    void SetIsVirtual(bool value) { isVirtual_ = value; }
    // The C# `bool IMember.IsOverride => false`.
    bool IsOverride() const override { return false; }
    // The C# `bool IMember.IsOverridable => false`.
    bool IsOverridable() const override { return false; }
    // The C# `TypeParameterSubstitution IMember.Substitution =>
    // TypeParameterSubstitution.Identity`.
    const TypeParameterSubstitution* Substitution() const override;

    // The C# `EntityHandle IEntity.MetadataToken => default` (the nil handle:
    // raw token 0).
    std::uint32_t MetadataToken() const override { return 0; }
    // The C# `ITypeDefinition IEntity.DeclaringTypeDefinition =>
    // DeclaringType?.GetDefinition()`.
    const ITypeDefinition* DeclaringTypeDefinition() const override;
    // The C# `IModule IEntity.ParentModule =>
    // DeclaringType?.GetDefinition()?.ParentModule`.
    const IModule* ParentModule() const override;
    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // EmptyList<IAttribute>.Instance`.
    std::vector<const IAttribute*> GetAttributes() const override {
        return {};
    }
    // The C# `bool IEntity.HasAttribute(KnownAttribute) => false`.
    bool HasAttribute(KnownAttribute) const override { return false; }
    // The C# `IAttribute IEntity.GetAttribute(KnownAttribute) => null`.
    const IAttribute* GetAttribute(KnownAttribute) const override {
        return nullptr;
    }
    // The C# `bool IEntity.IsAbstract => false`.
    bool IsAbstract() const override { return false; }
    // The C# `bool IEntity.IsSealed => false`.
    bool IsSealed() const override { return false; }

    // The C# `ICompilation ICompilationProvider.Compilation => compilation`.
    const ICompilation& Compilation() const override { return compilation_; }

    // The C# `string INamedElement.FullName` -- the `DeclaringType != null ?
    // DeclaringType.FullName + "." + Name : Name` conditional. Out-of-line
    // (the FullNameOf helper, convention (f)).
    std::string FullName() const override;
    // The C# `string INamedElement.ReflectionName` -- the same conditional
    // over `ReflectionName`.
    std::string ReflectionName() const override;
    // The C# `string INamedElement.Namespace => DeclaringType?.Namespace`
    // (the null collapsing to "" -- the null=="" equivalence, convention
    // (a)). Out-of-line (the NamespaceOf helper, convention (f)).
    std::string Namespace() const override;

    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)
    // => Equals(obj)` -- the DEFAULT reference equality (convention (e)). The
    // C# reference identity is path-independent; the C++ subobject pointers
    // are not (the FakeMethod/FakeEvent/... diamonds give every
    // `IMember`-converting path a different address), so the comparison runs
    // on the most-derived object address (the `dynamic_cast<void*>` identity)
    // instead of the incoming subobject pointer.
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization)
        const override {
        (void)typeNormalization;
        return dynamic_cast<const void*>(obj) == dynamic_cast<const void*>(this);
    }

    // The C# `public override int GetHashCode()` (convention (e)): the
    // pointer-identity hash.
    int GetHashCode() const {
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(this)
                                & 0x7fffffff);
    }

protected:
    const ICompilation& compilation_;
    std::string name_;  // the C# null default maps to ""
    ITypePtr declaringType_;
    // The C# `= SpecialType.UnknownType` initializer -- the port's
    // `UnknownType()` null-object free function (a SpecialType of Kind
    // Unknown rendering "?").
    ITypePtr returnType_ = UnknownType();
    ::ILSpy::Decompiler::TypeSystem::Accessibility accessibility_
        = ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    bool isStatic_ = false;
    bool isVirtual_ = false;
};

// The `abstract` members each leaf provides:
//   SymbolKind()  -- the leaf's own kind
//   Specialize() -- the SpecializedX::Create short-circuits + the deferred
//       general arm (convention (d))

} // namespace ILSpy::Decompiler::TypeSystem::Implementation

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// Port of the C# `class FakeField : FakeMember, IField` -- the fake field.
// The two-`IMember`-subobject diamond is resolved by one override per shared
// name delegating to the `FakeMember::` call (convention (g)); the `IVariable` /
// `IField`-only members carry the C# fixed values.
class FakeField : public FakeMember, public IField {
public:
    // The C# `public FakeField(ICompilation compilation) : base(compilation)`.
    explicit FakeField(const ICompilation& compilation)
        : FakeMember(compilation) {}

    // --- ISymbol / INamedElement (delegated to sub A) ---

    std::string Name() const override { return FakeMember::Name(); }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
    }
    std::string FullName() const override { return FakeMember::FullName(); }
    std::string ReflectionName() const override {
        return FakeMember::ReflectionName();
    }
    std::string Namespace() const override { return FakeMember::Namespace(); }
    const ICompilation& Compilation() const override {
        return FakeMember::Compilation();
    }

    // --- IEntity (delegated to sub A) ---

    std::uint32_t MetadataToken() const override {
        return FakeMember::MetadataToken();
    }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return FakeMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override {
        return FakeMember::DeclaringType();
    }
    const IModule* ParentModule() const override {
        return FakeMember::ParentModule();
    }
    std::vector<const IAttribute*> GetAttributes() const override {
        return FakeMember::GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return FakeMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return FakeMember::GetAttribute(attribute);
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return FakeMember::Accessibility();
    }
    bool IsStatic() const override { return FakeMember::IsStatic(); }
    bool IsAbstract() const override { return FakeMember::IsAbstract(); }
    bool IsSealed() const override { return FakeMember::IsSealed(); }

    // --- IMember (delegated to sub A) ---

    const IMember* MemberDefinition() const override {
        return FakeMember::MemberDefinition();
    }
    const IType& ReturnType() const override {
        return FakeMember::ReturnType();
    }
    std::vector<const IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return FakeMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return FakeMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return FakeMember::IsVirtual(); }
    bool IsOverride() const override { return FakeMember::IsOverride(); }
    bool IsOverridable() const override { return FakeMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return FakeMember::Substitution();
    }
    // The C# `public override IMember Specialize` --
    // `SpecializedField.Create(this, substitution)` (convention (d)). REAL: routes
    // through the landed `SpecializedField::Create` factory with the no-op-deleter alias
    // over `this`; every fresh result is kept alive in the registry below (the
    // `MetadataField::Specialize` convention).
    const IMember* Specialize(const TypeParameterSubstitution* substitution)
        const override;
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization)
        const override {
        return FakeMember::Equals(obj, typeNormalization);
    }

    // --- IVariable (the C# explicit-interface delegations) ---

    // The C# `IType IVariable.Type => this.ReturnType`.
    const IType& Type() const override { return FakeMember::ReturnType(); }
    // The C# `bool IVariable.IsConst => false`.
    bool IsConst() const override { return false; }
    // The C# `object IVariable.GetConstantValue(bool) => null`.
    std::any GetConstantValue(bool) const override { return {}; }

    // --- IField (the C# fixed values) ---

    // The C# `bool IField.IsReadOnly => false`.
    bool IsReadOnly() const override { return false; }
    // The C# `bool IField.ReturnTypeIsRefReadOnly => false`.
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    // The C# `bool IField.IsVolatile => false`.
    bool IsVolatile() const override { return false; }

private:
    // The keep-alive registry for the `Specialize`-created instances (the C#
    // GC roots them; the returned `const IMember*` must stay valid while this
    // fake is alive -- the `VarArgInstanceMethod` rewrap-registry precedent).
    // `mutable` (`Specialize` is const).
    mutable std::vector<std::shared_ptr<IField>> specializedFields_;
};

// Port of the C# `class FakeMethod : FakeMember, IMethod` -- the fake method
// (the dummy-construction factory and the `CreateFakeMethod` fallback product).
class FakeMethod : public FakeMember, public IMethod {
public:
    // The C# `public FakeMethod(ICompilation compilation, SymbolKind symbolKind)
    // : base(compilation)`.
    FakeMethod(const ICompilation& compilation,
               ::ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind)
        : FakeMember(compilation), symbolKind_(symbolKind) {}

    // The C# `internal static IMethod CreateDummyConstructor(ICompilation
    // compilation, IType declaringType, Accessibility accessibility =
    // Accessibility.Public)` -- the parameterless public constructor the
    // `Methods` enumeration adds to structs/enums without one. Returns an
    // OWNING handle (the C# `IMethod` GC reference; the caller keeps it
    // alive -- the `MetadataTypeDefinition::Methods` keep-alive slot).
    static std::shared_ptr<IMethod> CreateDummyConstructor(
        const ICompilation& compilation, ITypePtr declaringType,
        ::ILSpy::Decompiler::TypeSystem::Accessibility accessibility
        = ::ILSpy::Decompiler::TypeSystem::Accessibility::Public);

    // --- The C# settable per-leaf members (convention (a)) ---

    // The C# `public IReadOnlyList<ITypeParameter> TypeParameters { get; set; }
    // = EmptyList<ITypeParameter>.Instance` -- the snapshot over the owning
    // list.
    std::vector<const ITypeParameter*> TypeParameters() const override;
    void SetTypeParameters(
        std::vector<std::shared_ptr<const ITypeParameter>> typeParameters) {
        typeParameters_ = std::move(typeParameters);
    }

    // The C# `public IReadOnlyList<IParameter> Parameters { get; set; } =
    // Empty<IParameter>.Array` (the EMPTY default -- distinct from
    // FakeProperty's null, convention (a)).
    std::vector<const IParameter*> Parameters() const override;
    void SetParameters(
        std::vector<std::shared_ptr<const IParameter>> parameters) {
        parameters_ = std::move(parameters);
    }

    // The C# `public IMember AccessorOwner { get; set; }` (convention (c)).
    const IMember* AccessorOwner() const override { return accessorOwner_; }
    void SetAccessorOwner(const IMember* value) { accessorOwner_ = value; }

    // The C# `public MethodSemanticsAttributes AccessorKind { get; set; }`.
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind()
        const override {
        return accessorKind_;
    }
    void SetAccessorKind(
        ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes value) {
        accessorKind_ = value;
    }

    // --- ISymbol / INamedElement (delegated to sub A) ---

    std::string Name() const override { return FakeMember::Name(); }
    // The C# `public override SymbolKind SymbolKind => symbolKind`.
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return symbolKind_;
    }
    std::string FullName() const override { return FakeMember::FullName(); }
    std::string ReflectionName() const override {
        return FakeMember::ReflectionName();
    }
    std::string Namespace() const override { return FakeMember::Namespace(); }
    const ICompilation& Compilation() const override {
        return FakeMember::Compilation();
    }

    // --- IEntity (delegated to sub A) ---

    std::uint32_t MetadataToken() const override {
        return FakeMember::MetadataToken();
    }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return FakeMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override {
        return FakeMember::DeclaringType();
    }
    const IModule* ParentModule() const override {
        return FakeMember::ParentModule();
    }
    std::vector<const IAttribute*> GetAttributes() const override {
        return FakeMember::GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return FakeMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return FakeMember::GetAttribute(attribute);
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return FakeMember::Accessibility();
    }
    bool IsStatic() const override { return FakeMember::IsStatic(); }
    bool IsAbstract() const override { return FakeMember::IsAbstract(); }
    bool IsSealed() const override { return FakeMember::IsSealed(); }

    // --- IMember (delegated to sub A) ---

    const IMember* MemberDefinition() const override {
        return FakeMember::MemberDefinition();
    }
    const IType& ReturnType() const override {
        return FakeMember::ReturnType();
    }
    std::vector<const IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return FakeMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return FakeMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return FakeMember::IsVirtual(); }
    bool IsOverride() const override { return FakeMember::IsOverride(); }
    bool IsOverridable() const override { return FakeMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return FakeMember::Substitution();
    }
    // The C# `public override IMember Specialize` --
    // `SpecializedMethod.Create(this, substitution)` (convention (d)). The
    // COVARIANT `IMethod` form is the final overrider for both `Specialize`
    // slots (convention (g)).
    const IMethod* Specialize(const TypeParameterSubstitution* substitution)
        const override;
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization)
        const override {
        return FakeMember::Equals(obj, typeNormalization);
    }

    // --- IMethod (the C# explicit-interface delegations) ---

    // The C# `IEnumerable<IAttribute> IMethod.GetReturnTypeAttributes() =>
    // EmptyList<IAttribute>.Instance`.
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    // The C# `bool IMethod.ReturnTypeIsRefReadOnly => false`.
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    // The C# `bool IMethod.ThisIsRefReadOnly => false`.
    bool ThisIsRefReadOnly() const override { return false; }
    // The C# `bool IMethod.IsInitOnly => false`.
    bool IsInitOnly() const override { return false; }
    // The C# `IReadOnlyList<IType> IMethod.TypeArguments => TypeParameters` --
    // the shared-handle snapshot (the MetadataMethod convention (k)).
    std::vector<ITypePtr> TypeArguments() const override;
    // The C# `bool IMethod.IsExtensionMethod => false`. A settable field so
    // a test fixture can construct the extension-method shape the
    // delegate-reference family's CanUseDelegateConstruction matrix drives
    // (the real MetadataMethod reads the attribute; the default false
    // preserves the prior hardcoded behavior, the additive-setter convention).
    bool IsExtensionMethod() const override { return isExtensionMethod_; }
    void SetIsExtensionMethod(bool value) { isExtensionMethod_ = value; }
    // The C# `bool IMethod.IsLocalFunction => false`.
    bool IsLocalFunction() const override { return false; }
    // The C# `bool IMethod.IsConstructor => symbolKind == SymbolKind.Constructor`.
    bool IsConstructor() const override {
        return symbolKind_
            == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor;
    }
    // The C# `bool IMethod.IsDestructor => symbolKind == SymbolKind.Destructor`.
    bool IsDestructor() const override {
        return symbolKind_
            == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Destructor;
    }
    // The C# `bool IMethod.IsOperator => symbolKind == SymbolKind.Operator`.
    bool IsOperator() const override {
        return symbolKind_
            == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Operator;
    }
    // The C# `bool IMethod.HasBody => false`.
    bool HasBody() const override { return false; }
    // The C# `bool IsAccessor => AccessorOwner is not null`.
    bool IsAccessor() const override { return accessorOwner_ != nullptr; }
    // The C# `IMethod IMethod.ReducedFrom => null`.
    const IMethod* ReducedFrom() const override { return nullptr; }

private:
    ::ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind_;
    std::vector<std::shared_ptr<const ITypeParameter>> typeParameters_;
    std::vector<std::shared_ptr<const IParameter>> parameters_;
    const IMember* accessorOwner_ = nullptr;
    bool isExtensionMethod_ = false;
    ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes accessorKind_
        = ::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes::None;

    // The keep-alive registry for the `Specialize`-created instances (the
    // `FakeField::specializedFields_` precedent).
    mutable std::vector<std::shared_ptr<IMethod>> specializedMethods_;
};

// Port of the C# `sealed class FakeProperty : FakeMember, IProperty`.
class FakeProperty final : public FakeMember, public IProperty {
public:
    // The C# `public FakeProperty(ICompilation compilation) : base(compilation)`.
    explicit FakeProperty(const ICompilation& compilation)
        : FakeMember(compilation) {}

    // --- The C# settable per-leaf members ---

    // The C# `public IMethod Getter { get; set; }` (convention (c)).
    const IMethod* Getter() const override { return getter_; }
    void SetGetter(const IMethod* value) { getter_ = value; }
    // The C# `public IMethod Setter { get; set; }`.
    const IMethod* Setter() const override { return setter_; }
    void SetSetter(const IMethod* value) { setter_ = value; }
    // The C# `public bool IsIndexer { get; set; }`.
    bool IsIndexer() const override { return isIndexer_; }
    void SetIsIndexer(bool value) { isIndexer_ = value; }
    // The C# `public IReadOnlyList<IParameter> Parameters { get; set; }` --
    // the C# NULL default maps to the empty vector (convention (a)); the
    // owning list is the port's stand-in for the C# array reference.
    std::vector<const IParameter*> Parameters() const override;
    void SetParameters(
        std::vector<std::shared_ptr<const IParameter>> parameters) {
        parameters_ = std::move(parameters);
    }

    // The C# `public override SymbolKind SymbolKind => IsIndexer ?
    // SymbolKind.Indexer : SymbolKind.Property`.
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return isIndexer_
            ? ::ILSpy::Decompiler::TypeSystem::SymbolKind::Indexer
            : ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }

    // --- ISymbol / INamedElement / IEntity / IMember (delegated to sub A) ---

    std::string Name() const override { return FakeMember::Name(); }
    std::string FullName() const override { return FakeMember::FullName(); }
    std::string ReflectionName() const override {
        return FakeMember::ReflectionName();
    }
    std::string Namespace() const override { return FakeMember::Namespace(); }
    const ICompilation& Compilation() const override {
        return FakeMember::Compilation();
    }
    std::uint32_t MetadataToken() const override {
        return FakeMember::MetadataToken();
    }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return FakeMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override {
        return FakeMember::DeclaringType();
    }
    const IModule* ParentModule() const override {
        return FakeMember::ParentModule();
    }
    std::vector<const IAttribute*> GetAttributes() const override {
        return FakeMember::GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return FakeMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return FakeMember::GetAttribute(attribute);
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return FakeMember::Accessibility();
    }
    bool IsStatic() const override { return FakeMember::IsStatic(); }
    bool IsAbstract() const override { return FakeMember::IsAbstract(); }
    bool IsSealed() const override { return FakeMember::IsSealed(); }
    const IMember* MemberDefinition() const override {
        return FakeMember::MemberDefinition();
    }
    const IType& ReturnType() const override {
        return FakeMember::ReturnType();
    }
    std::vector<const IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return FakeMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return FakeMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return FakeMember::IsVirtual(); }
    bool IsOverride() const override { return FakeMember::IsOverride(); }
    bool IsOverridable() const override { return FakeMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return FakeMember::Substitution();
    }
    // The C# `public override IMember Specialize` --
    // `SpecializedProperty.Create(this, substitution)` (convention (d)).
    const IMember* Specialize(const TypeParameterSubstitution* substitution)
        const override;
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization)
        const override {
        return FakeMember::Equals(obj, typeNormalization);
    }

    // --- IProperty (the C# fixed values) ---

    // The C# `bool CanGet => Getter is not null`.
    bool CanGet() const override { return getter_ != nullptr; }
    // The C# `bool CanSet => Setter is not null`.
    bool CanSet() const override { return setter_ != nullptr; }
    // The C# `bool ReturnTypeIsRefReadOnly => false`.
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    const IMethod* getter_ = nullptr;
    const IMethod* setter_ = nullptr;
    bool isIndexer_ = false;
    std::vector<std::shared_ptr<const IParameter>> parameters_;

    // The keep-alive registry for the `Specialize`-created instances (the
    // `FakeField::specializedFields_` precedent).
    mutable std::vector<std::shared_ptr<IProperty>> specializedProperties_;
};

// Port of the C# `sealed class FakeEvent : FakeMember, IEvent`.
class FakeEvent final : public FakeMember, public IEvent {
public:
    // The C# `public FakeEvent(ICompilation compilation) : base(compilation)`.
    explicit FakeEvent(const ICompilation& compilation)
        : FakeMember(compilation) {}

    // --- The C# settable per-leaf members (convention (c)) ---

    // The C# `public IMethod AddAccessor { get; set; }`.
    const IMethod* AddAccessor() const override { return addAccessor_; }
    void SetAddAccessor(const IMethod* value) { addAccessor_ = value; }
    // The C# `public IMethod RemoveAccessor { get; set; }`.
    const IMethod* RemoveAccessor() const override { return removeAccessor_; }
    void SetRemoveAccessor(const IMethod* value) { removeAccessor_ = value; }
    // The C# `public IMethod InvokeAccessor { get; set; }`.
    const IMethod* InvokeAccessor() const override { return invokeAccessor_; }
    void SetInvokeAccessor(const IMethod* value) { invokeAccessor_ = value; }

    // The C# `public override SymbolKind SymbolKind => SymbolKind.Event`.
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Event;
    }

    // --- ISymbol / INamedElement / IEntity / IMember (delegated to sub A) ---

    std::string Name() const override { return FakeMember::Name(); }
    std::string FullName() const override { return FakeMember::FullName(); }
    std::string ReflectionName() const override {
        return FakeMember::ReflectionName();
    }
    std::string Namespace() const override { return FakeMember::Namespace(); }
    const ICompilation& Compilation() const override {
        return FakeMember::Compilation();
    }
    std::uint32_t MetadataToken() const override {
        return FakeMember::MetadataToken();
    }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return FakeMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override {
        return FakeMember::DeclaringType();
    }
    const IModule* ParentModule() const override {
        return FakeMember::ParentModule();
    }
    std::vector<const IAttribute*> GetAttributes() const override {
        return FakeMember::GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return FakeMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return FakeMember::GetAttribute(attribute);
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility()
        const override {
        return FakeMember::Accessibility();
    }
    bool IsStatic() const override { return FakeMember::IsStatic(); }
    bool IsAbstract() const override { return FakeMember::IsAbstract(); }
    bool IsSealed() const override { return FakeMember::IsSealed(); }
    const IMember* MemberDefinition() const override {
        return FakeMember::MemberDefinition();
    }
    const IType& ReturnType() const override {
        return FakeMember::ReturnType();
    }
    std::vector<const IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return FakeMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return FakeMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return FakeMember::IsVirtual(); }
    bool IsOverride() const override { return FakeMember::IsOverride(); }
    bool IsOverridable() const override { return FakeMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return FakeMember::Substitution();
    }
    // The C# `public override IMember Specialize` --
    // `SpecializedEvent.Create(this, substitution)` (convention (d)).
    const IMember* Specialize(const TypeParameterSubstitution* substitution)
        const override;
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization)
        const override {
        return FakeMember::Equals(obj, typeNormalization);
    }

    // --- IEvent (the C# `bool CanAdd => AddAccessor is not null` family) ---

    // The C# `bool CanAdd => AddAccessor is not null`.
    bool CanAdd() const override { return addAccessor_ != nullptr; }
    // The C# `bool CanRemove => RemoveAccessor is not null`.
    bool CanRemove() const override { return removeAccessor_ != nullptr; }
    // The C# `bool CanInvoke => InvokeAccessor is not null`.
    bool CanInvoke() const override { return invokeAccessor_ != nullptr; }

private:
    const IMethod* addAccessor_ = nullptr;
    const IMethod* removeAccessor_ = nullptr;
    const IMethod* invokeAccessor_ = nullptr;

    // The keep-alive registry for the `Specialize`-created instances (the
    // `FakeField::specializedFields_` precedent).
    mutable std::vector<std::shared_ptr<IEvent>> specializedEvents_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
