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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedField.cs -- the
// concrete `IField` a `GetMembersHelper.GetFieldsImpl` builds for a field on a
// parameterized type (`new SpecializedField(m, pt.GetSubstitution()) { DeclaringType = pt }`).
// It derives `SpecializedMember, IField`: the `SpecializedMember` base supplies the
// substituted `ReturnType` / `DeclaringType` / `Substitution` + the delegated `IMember`
// surface; this class adds the `IField`-own surface (`IsReadOnly` / `ReturnTypeIsRefReadOnly` /
// `IsVolatile`) and the `IVariable` surface (`Type` = `this.ReturnType`, `IsConst`,
// `GetConstantValue`), all delegating to the wrapped `fieldDefinition`.
//
// KEY PORT CONVENTIONS:
//  (a) THE TWO-IMember-SUBOBJECT DIAMOND (the key structural crux, the
//      `NullabilityAnnotatedTypeParameter` D402 precedent applied to `IMember`):
//      `SpecializedField : SpecializedMember, IField` where `SpecializedMember : IMember`
//      and `IField : IMember, IVariable`. With the port's NON-VIRTUAL inheritance, the
//      `SpecializedMember` subobject contributes ONE `IMember` (sub A) and the `IField`
//      subobject contributes ANOTHER `IMember` (sub B, inside `IField`) -- TWO `IMember`
//      subobjects. Transitively there are THREE `ISymbol` subobjects (sub A's via `IEntity`,
//      sub B's via `IEntity`, and the `IVariable`'s own `ISymbol`). Every `IMember` /
//      `IEntity` / `ISymbol` / `INamedElement` / `ICompilationProvider` pure-virtual is
//      inherited via multiple paths; a single `SpecializedField::Name()` (etc.) override is
//      the final overrider for ALL of them (the standard C++ rule: one derived function
//      overrides every matching base virtual across all base subobjects), so ONE override
//      per method name covers every subobject. Without these the class stays ABSTRACT (sub
//      B's pure-virtuals unresolved); the `SpecializedMember` overrides cover sub A ONLY.
//      Each delegates to the `SpecializedMember::` qualified call (sub A's already-implemented
//      override, which itself delegates to `baseMember_`) -- `return SpecializedMember::Name();`
//      is a static, qualified dispatch to sub A's override, NOT a virtual re-dispatch.
//  (b) The C# `readonly IField fieldDefinition` (held alongside the `baseMember` in
//      `SpecializedMember`) ports to an OWNING `std::shared_ptr<IField> fieldDefinition_`
//      (shared ownership with `baseMember_` -- both point to the same field; the
//      `NullabilityAnnotatedTypeParameter::typeParameter_` precedent). The ctor takes a
//      `shared_ptr<IField>` and passes it to `SpecializedMember` (upcast to
//      `shared_ptr<IMember>`) AND stores it as `fieldDefinition_`.
//  (c) The C# `IType IVariable.Type => this.ReturnType` (an explicit-interface
//      implementation) ports to `IVariable::Type()` returning `SpecializedMember::ReturnType()`
//      -- both are `const IType&`; the field's type IS the substituted return type. The
//      `IMember::ReturnType()` and `IVariable::Type()` are DIFFERENT virtuals (different
//      names) that both denote the field's type; this override makes `IVariable::Type()`
//      return the same substituted `IType` the `SpecializedMember::ReturnType()` accessor
//      computes (the C# `MetadataField` implements both to return the same `IType`).
//  (d) The D372 name-shadowing crux applies to `SymbolKind()` / `Accessibility()` (the
//      inherited `ISymbol::SymbolKind` / `IEntity::Accessibility` member names shadow the
//      namespace-scope enums in MSVC's complete-class lookup), so both return types are
//      GLOBALLY QUALIFIED (the `DummyTypeParameter::SymbolKind` / `SpecializedMember` precedent).
//  (e) `GetConstantValue`'s default arg (`= false`) lives on the `IVariable` interface ONLY
//      (the port convention: defaults are not repeated in overrides); the override takes the
//      explicit `bool`.
//  (f) The C# `internal static IField Create(...)` factory (the `Identity`-or-`TypeParameterCount
//      == 0` short-circuit + the `MethodTypeArguments`-stripping) LANDED with the owning-
//      `Specialize` design: it returns an OWNING `std::shared_ptr<IField>` (the short-circuit
//      arms hand the caller-supplied handle straight back -- the caller passes the
//      no-op-deleter alias over its own instance; the general arm a fresh owning
//      `SpecializedField`), and every caller keeps the result alive in its keep-alive
//      registry (the C# GC root). The `GetMembersHelper` construction path keeps using
//      the ctor directly.
//  (g) HEADER-ONLY (all simple delegations + the `mutable`-free surface; the complex
//      `SpecializedMember` lazy `ReturnType` / `DeclaringType` are inherited, not
//      re-implemented); NOT added to the ilspy `CMakeLists.txt` (compiles into each TU that
//      includes it, the `SpecializedParameter.hpp` precedent) -- only the test `.cpp` is wired.

#pragma once

#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMember.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// A specialized field (see the header comment). Derives `SpecializedMember, IField`; the
// two-`IMember`-subobject diamond is resolved by one override per method name delegating to
// the `SpecializedMember::` qualified call (sub A's override). Header-only.
class SpecializedField final : public SpecializedMember, public IField {
public:
    // The C# `SpecializedField(IField fieldDefinition, TypeParameterSubstitution substitution)`.
    // `fieldDefinition` is shared with the `SpecializedMember` base (its `baseMember_`,
    // upcast to `IMember`) AND stored as the typed `fieldDefinition_`.
    SpecializedField(std::shared_ptr<IField> fieldDefinition,
                     TypeParameterSubstitution substitution)
        : SpecializedMember(fieldDefinition),  // upcast shared_ptr<IField> -> shared_ptr<IMember>
          fieldDefinition_(std::move(fieldDefinition)) {
        AddSubstitution(std::move(substitution));
    }

    // The C# `internal static IField Create(IField fieldDefinition,
    // TypeParameterSubstitution substitution)` (SpecializedField.cs lines 28-37) -- the
    // field factory (the `SpecializedMethod::Create` ownership convention: the Identity /
    // declaring-tpc-0 arms return the caller-supplied handle, the general arm a fresh owning
    // `SpecializedField`; the caller keeps the result alive in its keep-alive registry).
    // NOTE the differences from the method factory: NO ArrayType arm, and the tpc-0 test
    // fires regardless of the field's own type parameters (a field is never generic). A
    // null `DeclaringType` maps the C# `NullReferenceException` to `std::runtime_error`
    // carrying the .NET message (checked AFTER the Identity arm).
    static std::shared_ptr<IField> Create(
        std::shared_ptr<IField> fieldDefinition,
        TypeParameterSubstitution substitution)
    {
        if (TypeParameterSubstitution::Identity().Equals(&substitution)) {
            return fieldDefinition;
        }
        if (fieldDefinition->DeclaringType() == nullptr) {
            throw std::runtime_error(
                "Object reference not set to an instance of an object.");
        }
        if (fieldDefinition->DeclaringType()->TypeParameterCount() == 0) {
            return fieldDefinition;
        }
        const auto& methodArgs = substitution.MethodTypeArguments();
        if (methodArgs.has_value() && !methodArgs->empty()) {
            substitution = TypeParameterSubstitution(substitution.ClassTypeArguments(),
                                                     std::vector<ITypePtr>{});
        }
        return std::make_shared<SpecializedField>(std::move(fieldDefinition),
                                                   std::move(substitution));
    }

    // --- ISymbol (redeclared by IField to disambiguate the shared-ISymbol-base diamond;
    //     the single override is the final overrider for all three ISymbol subobjects) ---

    // The C# `string Name => baseMember.Name`. Delegates to sub A's `SpecializedMember::Name()`.
    std::string Name() const override { return SpecializedMember::Name(); }
    // The C# `SymbolKind SymbolKind => baseMember.SymbolKind`. Globally qualified (D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return SpecializedMember::SymbolKind();
    }

    // --- INamedElement (delegated to sub A) ---

    std::string FullName() const override { return SpecializedMember::FullName(); }
    std::string ReflectionName() const override { return SpecializedMember::ReflectionName(); }
    std::string Namespace() const override { return SpecializedMember::Namespace(); }

    // --- ICompilationProvider (delegated to sub A) ---

    const ICompilation& Compilation() const override { return SpecializedMember::Compilation(); }

    // --- IEntity (delegated to sub A) ---

    std::uint32_t MetadataToken() const override { return SpecializedMember::MetadataToken(); }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return SpecializedMember::DeclaringTypeDefinition();
    }
    ITypePtr DeclaringType() const override { return SpecializedMember::DeclaringType(); }
    const IModule* ParentModule() const override { return SpecializedMember::ParentModule(); }
    std::vector<const IAttribute*> GetAttributes() const override {
        return SpecializedMember::GetAttributes();
    }
    bool HasAttribute(KnownAttribute attribute) const override {
        return SpecializedMember::HasAttribute(attribute);
    }
    const IAttribute* GetAttribute(KnownAttribute attribute) const override {
        return SpecializedMember::GetAttribute(attribute);
    }
    // Globally qualified (D372 crux -- the inherited `IEntity::Accessibility` shadows the enum).
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override {
        return SpecializedMember::Accessibility();
    }
    bool IsStatic() const override { return SpecializedMember::IsStatic(); }
    bool IsAbstract() const override { return SpecializedMember::IsAbstract(); }
    bool IsSealed() const override { return SpecializedMember::IsSealed(); }

    // --- IMember (delegated to sub A) ---

    const IMember* MemberDefinition() const override { return SpecializedMember::MemberDefinition(); }
    const IType& ReturnType() const override { return SpecializedMember::ReturnType(); }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return SpecializedMember::ExplicitlyImplementedInterfaceMembers();
    }
    bool IsExplicitInterfaceImplementation() const override {
        return SpecializedMember::IsExplicitInterfaceImplementation();
    }
    bool IsVirtual() const override { return SpecializedMember::IsVirtual(); }
    bool IsOverride() const override { return SpecializedMember::IsOverride(); }
    bool IsOverridable() const override { return SpecializedMember::IsOverridable(); }
    const TypeParameterSubstitution* Substitution() const override {
        return SpecializedMember::Substitution();
    }
    const IMember* Specialize(const TypeParameterSubstitution* newSubstitution) const override {
        return SpecializedMember::Specialize(newSubstitution);
    }
    bool Equals(const IMember* obj, const TypeVisitor* typeNormalization) const override {
        return SpecializedMember::Equals(obj, typeNormalization);
    }

    // --- IVariable (the C# explicit-interface delegations) ---

    // The C# `IType IVariable.Type => this.ReturnType` -- the field's type IS the
    // substituted return type. Returns the same `const IType&` as `ReturnType()`.
    const IType& Type() const override { return SpecializedMember::ReturnType(); }
    // The C# `bool IsConst => fieldDefinition.IsConst`.
    bool IsConst() const override { return fieldDefinition_->IsConst(); }
    // The C# `object GetConstantValue(bool throwOnInvalidMetadata) =>
    // fieldDefinition.GetConstantValue(throwOnInvalidMetadata)`. Default arg lives on
    // `IVariable` only (the port convention); the override takes the explicit `bool`.
    std::any GetConstantValue(bool throwOnInvalidMetadata) const override {
        return fieldDefinition_->GetConstantValue(throwOnInvalidMetadata);
    }

    // --- IField (the C# `bool IsReadOnly / ReturnTypeIsRefReadOnly / IsVolatile =>
    //     fieldDefinition.*`) ---

    bool IsReadOnly() const override { return fieldDefinition_->IsReadOnly(); }
    bool ReturnTypeIsRefReadOnly() const override { return fieldDefinition_->ReturnTypeIsRefReadOnly(); }
    bool IsVolatile() const override { return fieldDefinition_->IsVolatile(); }

private:
    std::shared_ptr<IField> fieldDefinition_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
