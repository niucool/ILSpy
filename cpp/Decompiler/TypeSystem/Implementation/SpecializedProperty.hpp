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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedProperty.cs -- the
// concrete `IProperty` a `GetMembersHelper.GetPropertiesImpl` builds for a property on a
// parameterized type (`new SpecializedProperty(m, pt.GetSubstitution())`). It derives
// `SpecializedParameterizedMember, IProperty`: the `SpecializedParameterizedMember` base
// supplies the substituted `Parameters` list (lazily, via `CreateParameters`) + the
// substituted `ReturnType` / `DeclaringType` / `Substitution` + the delegated `IMember`
// surface; this class adds the `IProperty`-own surface (`CanGet` / `CanSet` + the `Getter` /
// `Setter` accessors + `IsIndexer` / `ReturnTypeIsRefReadOnly`), delegating to the wrapped
// `propertyDefinition`.
//
// KEY PORT CONVENTIONS:
//  (a) THE THREE-IMember-SUBOBJECT DIAMOND (the `SpecializedField` D483 / `SpecializedEvent`
//      D485 pattern extended): `SpecializedProperty : SpecializedParameterizedMember,
//      IProperty` where `SpecializedParameterizedMember : SpecializedMember,
//      IParameterizedMember` and `IProperty : IParameterizedMember`. Transitively there are
//      THREE `IMember` subobjects: `SpecializedMember`'s (sub A), the
//      `IParameterizedMember` inside `SpecializedParameterizedMember` (sub B), and the
//      `IParameterizedMember` inside `IProperty` (sub C -- `IProperty`'s OWN
//      `IParameterizedMember` base). Every `IMember` / `IEntity` / `ISymbol` /
//      `INamedElement` / `ICompilationProvider` / `IParameterizedMember` pure-virtual is
//      inherited via multiple paths; a single `SpecializedProperty::Name()` (etc.) override
//      is the final overrider for ALL THREE (the standard C++ rule), so ONE override per
//      method name covers every subobject. Without these the class stays ABSTRACT (sub B/C
//      pure-virtuals unresolved); the `SpecializedMember` overrides cover sub A ONLY, and
//      `SpecializedParameterizedMember::Parameters()` covers the `IParameterizedMember` surface for
//      sub B ONLY -- sub C (`IProperty`'s OWN `IParameterizedMember`) is a separate subobject
//      with its own unresolved `Parameters()` pure-virtual, so `Parameters()` IS redeclared
//      here, delegating to the inherited `SpecializedParameterizedMember::Parameters()` (sub
//      B's override, a static qualified dispatch resolving sub C). Each diamond override delegates to the `SpecializedMember::` qualified
//      call (sub A's already-implemented override) -- a static, qualified dispatch, NOT a
//      virtual re-dispatch.
//  (b) The C# `readonly IProperty propertyDefinition` (held alongside the `baseMember` in
//      `SpecializedMember`) ports to an OWNING `std::shared_ptr<IProperty> propertyDefinition_`
//      (shared ownership with `baseMember_`; the `SpecializedEvent::eventDefinition_`
//      precedent). The ctor takes a `shared_ptr<IProperty>` and passes it to
//      `SpecializedParameterizedMember` (upcast to `shared_ptr<IParameterizedMember>`) AND
//      stores it as `propertyDefinition_`.
//  (c) The D372 name-shadowing crux applies to `SymbolKind()` / `Accessibility()` (the
//      inherited `ISymbol::SymbolKind` / `IEntity::Accessibility` member names shadow the
//      namespace-scope enums in MSVC's complete-class lookup), so both return types are
//      GLOBALLY QUALIFIED (the `SpecializedEvent` precedent).
//  (d) The `IProperty`-own bools (`CanGet` / `CanSet` / `IsIndexer` /
//      `ReturnTypeIsRefReadOnly`) delegate to `propertyDefinition_` verbatim (the C#
//      `=> propertyDefinition.CanGet` etc.).
//  (e) The two accessor properties (`Getter` / `Setter`) are DEFERRED to return the BASE
//      accessor UNSPECIALIZED. The C# `WrapAccessor(ref cachingField, propertyDefinition.
//      Getter)` lazily builds a `SpecializedMethod` for each accessor via
//      `accessorDefinition.Specialize(substitution)` and caches it (an OWNING `LazyInit`
//      cache). The port's `IMember::Specialize` returns a NON-OWNING `const IMember*` (the
//      "type system owns" convention), so the owning-`Specialize` / owning-cache design
//      `WrapAccessor` needs is not yet in place (lands with the owning-`Specialize` refactor).
//      The deferred override delegates to `propertyDefinition_->Getter()` (the base
//      accessor, unspecialized) -- a documented divergence (the accessor's `DeclaringType` /
//      `ReturnType` / `Parameters` are not substituted). The `GetMembersHelper` /
//      `MemberLookup.LookupGroup` routing (the blocker) does NOT use the accessors (it
//      builds the `SpecializedProperty` for the member list, not for accessor dispatch), so
//      the divergence is benign for the routing; the faithful specialized accessors land
//      with the owning-`Specialize` design + `WrapAccessor`.
//  (f) The C# `internal static IProperty Create(...)` factory (the `Identity`-or-
//      `TypeParameterCount == 0` short-circuit + the `MethodTypeArguments`-stripping) is
//      DEFERRED -- it needs the owning-`Specialize` design; lands with the `GetMembersHelper`
//      routing that calls it. The ctor-based construction (what `GetMembersHelper` uses
//      directly) is the faithful surface ported here.
//  (g) HEADER-ONLY (all simple delegations + the deferred accessors; the complex lazy
//      `ReturnType` / `DeclaringType` / `Parameters` are inherited, not re-implemented); NOT
//      added to the ilspy `CMakeLists.txt` (compiles into each TU that includes it, the
//      `SpecializedEvent` precedent) -- only the test `.cpp` is wired.

#pragma once

#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedParameterizedMember.hpp"

#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// A specialized property (see the header comment). Derives `SpecializedParameterizedMember,
// IProperty`; the three-`IMember`-subobject diamond is resolved by one override per method
// name delegating to the `SpecializedMember::` qualified call (sub A's override).
// `Parameters()` is inherited from `SpecializedParameterizedMember` (the final overrider for
// all three `IMember` subobjects). Header-only.
class SpecializedProperty final : public SpecializedParameterizedMember, public IProperty {
public:
    // The C# `SpecializedProperty(IProperty propertyDefinition, TypeParameterSubstitution
    // substitution)`. `propertyDefinition` is shared with the `SpecializedParameterizedMember`
    // base (its `baseMember_`, upcast to `IParameterizedMember`) AND stored as the typed
    // `propertyDefinition_`.
    SpecializedProperty(std::shared_ptr<IProperty> propertyDefinition,
                        TypeParameterSubstitution substitution)
        : SpecializedParameterizedMember(propertyDefinition),  // upcast -> IParameterizedMember
          propertyDefinition_(std::move(propertyDefinition)) {
        AddSubstitution(std::move(substitution));
    }

    // --- ISymbol (the single override is the final overrider for all three IMember
    //     subobjects; IProperty does NOT redeclare Name/SymbolKind, but the three-IMember
    //     diamond makes them ambiguous without these) ---

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

    // --- IParameterizedMember (Parameters() is INHERITED from SpecializedParameterizedMember
    //     for sub B, but the IProperty subobject's OWN IParameterizedMember (sub C) is a
    //     SEPARATE subobject with its own unresolved `Parameters()` pure-virtual in the
    //     three-IMember-subobject diamond. Redeclare `Parameters()` here delegating to the
    //     inherited `SpecializedParameterizedMember::Parameters()` (sub B's override, which is
    //     the final overrider for sub B) -- a static, qualified dispatch resolving sub C.) ---

    std::vector<const IParameter*> Parameters() const override {
        return SpecializedParameterizedMember::Parameters();
    }

    // --- IProperty (the C# `bool CanGet / CanSet => propertyDefinition.*`,
    //     `IsIndexer => propertyDefinition.IsIndexer`, `ReturnTypeIsRefReadOnly =>
    //     propertyDefinition.ReturnTypeIsRefReadOnly`) ---

    bool CanGet() const override { return propertyDefinition_->CanGet(); }
    bool CanSet() const override { return propertyDefinition_->CanSet(); }
    bool IsIndexer() const override { return propertyDefinition_->IsIndexer(); }
    bool ReturnTypeIsRefReadOnly() const override { return propertyDefinition_->ReturnTypeIsRefReadOnly(); }

    // The C# `IMethod Getter => WrapAccessor(ref getter, propertyDefinition.Getter)` /
    // `Setter`. DEFERRED: the owning-`Specialize` / owning-cache design `WrapAccessor` needs
    // is not yet in place; the override returns the BASE accessor UNSPECIALIZED (a documented
    // divergence -- the accessor's `DeclaringType` / `ReturnType` / `Parameters` are not
    // substituted; the `GetMembersHelper` / `LookupGroup` routing does not use the
    // accessors). See the header comment (e).
    const IMethod* Getter() const override { return propertyDefinition_->Getter(); }
    const IMethod* Setter() const override { return propertyDefinition_->Setter(); }

private:
    std::shared_ptr<IProperty> propertyDefinition_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
