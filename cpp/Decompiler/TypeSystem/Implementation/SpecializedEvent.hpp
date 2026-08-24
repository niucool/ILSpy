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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedEvent.cs -- the
// concrete `IEvent` a `GetMembersHelper.GetEventsImpl` builds for an event on a
// parameterized type (`new SpecializedEvent(ev, pt.GetSubstitution())`). It derives
// `SpecializedMember, IEvent`: the `SpecializedMember` base supplies the substituted
// `ReturnType` / `DeclaringType` / `Substitution` + the delegated `IMember` surface; this
// class adds the `IEvent`-own surface (`CanAdd` / `CanRemove` / `CanInvoke` + the
// `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` accessors), delegating to the wrapped
// `eventDefinition`.
//
// KEY PORT CONVENTIONS:
//  (a) THE TWO-IMember-SUBOBJECT DIAMOND (the `SpecializedField` D483 precedent applied to
//      `IEvent`): `SpecializedEvent : SpecializedMember, IEvent` where
//      `SpecializedMember : IMember` and `IEvent : IMember` -- TWO `IMember` subobjects
//      (transitively two `ISymbol` subobjects; `IEvent` does NOT derive `IVariable`, so NO
//      third `ISymbol` -- simpler than `IField`). Every `IMember` / `IEntity` / `ISymbol` /
//      `INamedElement` / `ICompilationProvider` pure-virtual is inherited via two paths; a
//      single `SpecializedEvent::Name()` (etc.) override is the final overrider for BOTH
//      (the standard C++ rule), so ONE override per method name covers every subobject.
//      Without these the class stays ABSTRACT (sub B's pure-virtuals unresolved); the
//      `SpecializedMember` overrides cover sub A ONLY. Each delegates to the
//      `SpecializedMember::` qualified call (sub A's already-implemented override) -- a
//      static, qualified dispatch, NOT a virtual re-dispatch.
//  (b) The C# `readonly IEvent eventDefinition` (held alongside the `baseMember` in
//      `SpecializedMember`) ports to an OWNING `std::shared_ptr<IEvent> eventDefinition_`
//      (shared ownership with `baseMember_` -- both point to the same event; the
//      `SpecializedField::fieldDefinition_` precedent). The ctor takes a
//      `shared_ptr<IEvent>` and passes it to `SpecializedMember` (upcast to
//      `shared_ptr<IMember>`) AND stores it as `eventDefinition_`.
//  (c) The D372 name-shadowing crux applies to `SymbolKind()` / `Accessibility()` (the
//      inherited `ISymbol::SymbolKind` / `IEntity::Accessibility` member names shadow the
//      namespace-scope enums in MSVC's complete-class lookup), so both return types are
//      GLOBALLY QUALIFIED (the `SpecializedField` precedent).
//  (d) The `IEvent`-own bools (`CanAdd` / `CanRemove` / `CanInvoke`) delegate to
//      `eventDefinition_` verbatim (the C# `=> eventDefinition.CanAdd` etc.).
//  (e) The three accessor properties (`AddAccessor` / `RemoveAccessor` / `InvokeAccessor`)
//      are DEFERRED to return the BASE accessor UNSPECIALIZED. The C# `WrapAccessor(ref
//      cachingField, eventDefinition.AddAccessor)` lazily builds a `SpecializedMethod` for
//      each accessor via `accessorDefinition.Specialize(substitution)` and caches it (an
//      OWNING `LazyInit` cache). The port's `IMember::Specialize` returns a NON-OWNING
//      `const IMember*` (the "type system owns" convention), so the owning-`Specialize` /
//      owning-cache design `WrapAccessor` needs is not yet in place (lands with the
//      owning-`Specialize` refactor). The deferred override delegates to
//      `eventDefinition_->AddAccessor()` (the base accessor, unspecialized) -- a documented
//      divergence: the accessor's `DeclaringType` / `ReturnType` are NOT substituted in
//      this minimal leaf. The `GetMembersHelper` / `MemberLookup.LookupGroup` routing (the
//      blocker) does NOT use the accessors (it builds the `SpecializedEvent` for the member
//      list, not for accessor dispatch), so the divergence is benign for the routing; the
//      faithful specialized accessors land with the owning-`Specialize` design + `WrapAccessor`.
//  (f) The C# `internal static IEvent Create(...)` factory (the `Identity`-or-
//      `TypeParameterCount == 0` short-circuit + the `MethodTypeArguments`-stripping) is
//      DEFERRED -- it needs the owning-`Specialize` design; lands with the `GetMembersHelper`
//      routing that calls it. The ctor-based construction (what `GetMembersHelper` uses
//      directly) is the faithful surface ported here.
//  (g) HEADER-ONLY (all simple delegations + the deferred accessors; the complex lazy
//      `ReturnType` / `DeclaringType` are inherited, not re-implemented); NOT added to the
//      ilspy `CMakeLists.txt` (compiles into each TU that includes it, the `SpecializedField`
//      precedent) -- only the test `.cpp` is wired.

#pragma once

#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMember.hpp"

#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// A specialized event (see the header comment). Derives `SpecializedMember, IEvent`; the
// two-`IMember`-subobject diamond is resolved by one override per method name delegating to
// the `SpecializedMember::` qualified call (sub A's override). Header-only.
class SpecializedEvent final : public SpecializedMember, public IEvent {
public:
    // The C# `SpecializedEvent(IEvent eventDefinition, TypeParameterSubstitution substitution)`.
    // `eventDefinition` is shared with the `SpecializedMember` base (its `baseMember_`,
    // upcast to `IMember`) AND stored as the typed `eventDefinition_`.
    SpecializedEvent(std::shared_ptr<IEvent> eventDefinition,
                     TypeParameterSubstitution substitution)
        : SpecializedMember(eventDefinition),  // upcast shared_ptr<IEvent> -> shared_ptr<IMember>
          eventDefinition_(std::move(eventDefinition)) {
        AddSubstitution(std::move(substitution));
    }

    // --- ISymbol (the single override is the final overrider for both IMember subobjects;
    //     IEvent does NOT redeclare Name/SymbolKind, but the two-IMember-subobject diamond
    //     makes them ambiguous without these) ---

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

    // --- IEvent (the C# `bool CanAdd / CanRemove / CanInvoke => eventDefinition.*`) ---

    bool CanAdd() const override { return eventDefinition_->CanAdd(); }
    bool CanRemove() const override { return eventDefinition_->CanRemove(); }
    bool CanInvoke() const override { return eventDefinition_->CanInvoke(); }

    // The C# `IMethod AddAccessor => WrapAccessor(ref addAccessor, eventDefinition.AddAccessor)`
    // / `RemoveAccessor` / `InvokeAccessor`. DEFERRED: the owning-`Specialize` / owning-cache
    // design `WrapAccessor` needs is not yet in place; the override returns the BASE accessor
    // UNSPECIALIZED (a documented divergence -- the accessor's `DeclaringType` / `ReturnType`
    // are not substituted; the `GetMembersHelper` / `LookupGroup` routing does not use the
    // accessors). See the header comment (e).
    const IMethod* AddAccessor() const override { return eventDefinition_->AddAccessor(); }
    const IMethod* RemoveAccessor() const override { return eventDefinition_->RemoveAccessor(); }
    const IMethod* InvokeAccessor() const override { return eventDefinition_->InvokeAccessor(); }

private:
    std::shared_ptr<IEvent> eventDefinition_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
