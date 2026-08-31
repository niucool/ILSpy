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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/SpecializedParameter.cs --
// the sealed `IParameter` a `SpecializedParameterizedMember` builds its substituted
// parameter list out of. It wraps a base `IParameter` and a new `IType`, delegating
// the whole `IParameter` / `IVariable` / `ISymbol` surface to the base parameter
// EXCEPT `Type` (the new type) and `Owner` (the new owning member).
// `SpecializedParameterizedMember.CreateParameters` constructs one per parameter:
// `new SpecializedParameter(p, p.Type.AcceptVisitor(substitution), this)` -- the
// new type is the base parameter's type run through the member's substitution, and
// the new owner is the specializing member itself.
//
// This is the first of the `Specialized*` member leaves the `GetMembersHelper`
// routing needs (the `MemberLookup.LookupGroup` blocker): `GetMethodsImpl` /
// `GetPropertiesImpl` build `SpecializedMethod` / `SpecializedProperty`, whose
// `SpecializedParameterizedMember` base builds its `Parameters` list out of these.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `sealed class SpecializedParameter : IParameter` ports to a `final
//      class SpecializedParameter : public IParameter` (the interface-to-abstract-base
//      convention; `final` mirrors `sealed`). `IParameter : IVariable : ISymbol` is
//      SINGLE inheritance (no diamond), so the single `Name()` override is the final
//      overrider for all three (the D374 inherited-virtual-covers-the-`new` precedent).
//  (b) The C# `readonly IParameter baseParameter` (a held reference -- the C# does NOT
//      own the base parameter; the `SpecializedParameterizedMember` / base member owns
//      it) ports to `const IParameter* baseParameter_` -- a NON-OWNING raw pointer,
//      non-null by contract. This is the faithful counterpart of the C# non-owning
//      reference: the `SpecializedParameter` does not keep the base alive; the owning
//      chain is `SpecializedParameterizedMember` -> `baseMember_` -> its parameters (the
//      base member owns its parameters; the `SpecializedParameterizedMember` holds
//      `baseMember_` owning, so the base parameters outlive the `SpecializedParameter`
//      instances in the `Parameters` cache). The C# `Debug.Assert(baseParameter != null
//      && newType != null)` is a debug-only contract; the port documents the non-null
//      contract without a runtime assert (the `DummyTypeParameter` no-assert
//      precedent). The D480 first cut held `baseParameter_` as an OWNING `shared_ptr`;
//      that was a DEVIATION from the C# non-owning reference that the D484
//      `SpecializedParameterizedMember.CreateParameters` call site surfaced
//      (`CreateParameters` reads `((IParameterizedMember)baseMember).Parameters`, which
//      returns NON-OWNING `const IParameter*`; an owning `shared_ptr` ctor cannot take a
//      non-owning pointer without an aliasing-`shared_ptr` trick). The non-owning raw
//      pointer is the faithful, clean design; the revision is a follow-up fix to the
//      D480 committed leaf, motivated by the `CreateParameters` call site.
//  (c) The C# `readonly IType newType` ports to `ITypePtr newType_` (an owning
//      `shared_ptr<IType>`). `IVariable::Type()` returns `const IType&`, so `Type()`
//      returns `*newType_` (the managed `IType` is the same object passed in -- the
//      caller's `shared_ptr` shares ownership, so the returned reference is stable for
//      the parameter's lifetime).
//  (d) The C# `readonly IParameterizedMember newOwner` ports to
//      `const IParameterizedMember* newOwner_` -- a NON-OWNING raw pointer, nullable
//      (the `IParameter::Owner` contract: "May return null" for lambda/anonymous-method
//      parameters). The specializing member owns the `SpecializedParameter` (its
//      `Parameters` vector holds it); the back-reference to the owner is non-owning.
//      The `IEntity::ParentModule` / `IParameter::Owner` non-owning-pointer precedent.
//  (e) `SymbolKind()` returns `SymbolKind::Parameter` (the C# `ISymbol.SymbolKind =>
//      SymbolKind.Parameter`); the return type is GLOBALLY QUALIFIED -- the inherited
//      `ISymbol::SymbolKind` (via `IParameter` -> `IVariable` -> `ISymbol`) member name
//      shadows the namespace-scope `SymbolKind` enum in MSVC's complete-class lookup
//      (the D372 crux; `ReferenceKind()` applies the same, via the inherited
//      `IParameter::ReferenceKind`).
//  (f) The C# `public override string ToString() => DefaultParameter.ToString(this)`
//      delegates to the static canonical parameter-signature renderer the
//      `DefaultParameter` leaf carries (its `public static string ToString(IParameter)`;
//      the member was deferred from this leaf until `DefaultParameter` landed -- it
//      now delegates through the included `DefaultParameter.hpp`). The port's
//      `IParameter` / `IVariable` / `ISymbol` carry no virtual `ToString` (the
//      `DummyTypeParameter.ToString` plain-member precedent), so the member is a PLAIN
//      member, not an override.

#pragma once

#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"

#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// A specialized parameter (see the header comment). A sealed `IParameter` wrapping a
// base parameter and a new type, delegating the whole surface to the base except `Type`
// (the new type) and `Owner` (the new owning member). All accessors are inline (simple
// delegations); the header is header-only (no .cpp -- no `TypeVisitor` / complete-type
// requirements beyond `IParameter`, which is included).
class SpecializedParameter final : public IParameter {
public:
    // The C# `SpecializedParameter(IParameter baseParameter, IType newType,
    // IParameterizedMember newOwner)`. `baseParameter` is a NON-OWNING non-null pointer
    // (the C# reference; the caller owns the base parameter); `newType` is moved into an
    // owning `ITypePtr` member (non-null by contract); `newOwner` is a nullable non-owning
    // pointer.
    SpecializedParameter(const IParameter* baseParameter,
                         ITypePtr newType,
                         const IParameterizedMember* newOwner)
        : baseParameter_(baseParameter),
          newType_(std::move(newType)),
          newOwner_(newOwner) {}

    // --- IParameter ---

    // The C# `IEnumerable<IAttribute> IParameter.GetAttributes() => baseParameter.GetAttributes()`.
    std::vector<const IAttribute*> GetAttributes() const override {
        return baseParameter_->GetAttributes();
    }

    // The C# `ReferenceKind IParameter.ReferenceKind => baseParameter.ReferenceKind`.
    // The return type is globally qualified: the inherited `IParameter::ReferenceKind`
    // member name shadows the namespace-scope `ReferenceKind` enum in MSVC's
    // complete-class lookup (the D372 crux; `SymbolKind` below applies the same).
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override {
        return baseParameter_->ReferenceKind();
    }

    // The C# `LifetimeAnnotation IParameter.Lifetime => baseParameter.Lifetime`.
    LifetimeAnnotation Lifetime() const override {
        return baseParameter_->Lifetime();
    }

    // The C# `bool IParameter.IsParams => baseParameter.IsParams`.
    bool IsParams() const override {
        return baseParameter_->IsParams();
    }

    // The C# `bool IParameter.IsOptional => baseParameter.IsOptional`.
    bool IsOptional() const override {
        return baseParameter_->IsOptional();
    }

    // The C# `bool IParameter.HasConstantValueInSignature =>
    // baseParameter.HasConstantValueInSignature`.
    bool HasConstantValueInSignature() const override {
        return baseParameter_->HasConstantValueInSignature();
    }

    // The C# `IParameterizedMember? IParameter.Owner => newOwner` -- the NEW owning
    // member (not the base parameter's owner). Nullable (a null `newOwner` is returned as
    // nullptr, the `IParameter.Owner` "May return null" contract).
    const IParameterizedMember* Owner() const override {
        return newOwner_;
    }

    // --- IVariable ---

    // The C# `IType IVariable.Type => newType` -- the NEW type (not the base's). Returns
    // a reference to the held `newType_` (the managed `IType` is the object passed in).
    const IType& Type() const override {
        return *newType_;
    }

    // The C# `bool IVariable.IsConst => baseParameter.IsConst`.
    bool IsConst() const override {
        return baseParameter_->IsConst();
    }

    // The C# `object? IVariable.GetConstantValue(bool throwOnInvalidMetadata) =>
    // baseParameter.GetConstantValue(throwOnInvalidMetadata)`.
    std::any GetConstantValue(bool throwOnInvalidMetadata) const override {
        return baseParameter_->GetConstantValue(throwOnInvalidMetadata);
    }

    // --- ISymbol ---

    // The C# `string IVariable.Name => baseParameter.Name` (also `ISymbol.Name`; the
    // single override is the final overrider for the single-inheritance chain).
    std::string Name() const override {
        return baseParameter_->Name();
    }

    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Parameter`. The return
    // type is globally qualified: the inherited `ISymbol::SymbolKind` member name
    // shadows the namespace-scope `SymbolKind` enum in MSVC's complete-class lookup
    // (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }

    // The C# `public override string ToString() => DefaultParameter.ToString(this)` --
    // the canonical parameter-signature renderer the `DefaultParameter` leaf carries
    // (convention (f); a PLAIN member -- the port's symbol surface has no virtual
    // `ToString`, the `DummyTypeParameter.ToString` plain-member precedent).
    std::string ToString() const {
        return DefaultParameter::ToString(*this);
    }

private:
    const IParameter* baseParameter_;
    ITypePtr newType_;
    const IParameterizedMember* newOwner_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
