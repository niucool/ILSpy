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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Port of `ICSharpCode.Decompiler.CSharp.Resolver.CSharpOperators` (CSharpOperators.cs) --
// the first slice of the built-in C# operator-method tables the `CSharpResolver` consumes
// for overload resolution of the built-in operators (unary +/-/!/~, the arithmetic/
// shift/equality/relational/bitwise binaries, ...). This header ports the CLASS SKELETON
// (CSharpOperators.cs lines 33-56: the per-compilation cached instance + the private ctor)
// and the nested `OperatorMethod` base class (lines 102-235, lifted to namespace scope
// here -- see below), plus the members that build on it:
//   * PORTED: `public static CSharpOperators Get(ICompilation)` (lines 40-54) -- the
//     per-compilation instance cached on the compilation's `CacheManager` (the
//     `CSharpConversions::Get` precedent).
//   * PORTED: `OperatorMethod[] Lift(params OperatorMethod[] methods)` (lines 60-70) --
//     the lifted-form list builder: the original methods followed by each method's
//     lifted form (`method.Lift(this)`), in iteration order.
//   * PORTED: `void InitParameterArrays()` (lines 72-87) + `IParameter MakeParameter(
//     TypeCode)` (line 89) + `IParameter MakeNullableParameter(IParameter)` (lines 91-100)
//     -- the precomputed parameter tables: one `DefaultParameter` per built-in type
//     (`TypeCode.Object`..`TypeCode.String`, resolved through `ReflectionHelper.FindType`
//     -- the D513 leaf this port lands alongside) and one `Nullable<T>`-typed parameter
//     per numeric primitive (`TypeCode.Boolean`..`TypeCode.Decimal`, built through
//     `NullableType.Create` D529), so the operator tables share parameter instances.
//   * PORTED: `internal class OperatorMethod : IParameterizedMember` (lines 102-235) --
//     the common base of every built-in operator method: a compilation-bound,
//     parameterized member with the fixed built-in-operator surface (Name
//     "operator", SymbolKind.Operator, static/public, no declaring type, the identity
//     substitution, reference equality) plus the `virtual Lift` hook and the
//     `parameters`/`ReturnType` initialization surface the derived operator-method
//     ctors write through.
//
// The derived operator-method classes (`UnaryOperatorMethod` / `LambdaUnaryOperatorMethod<T>`
// / `LiftedUnaryOperatorMethod`, the binary/equality/relational families, ...) and the lazy
// operator-table properties built on them (lines 237-1101) are DEFERRED to later slices.
//
// KEY PORT CONVENTIONS:
//  (a) The C# nested classes (`OperatorMethod` and, later, the *OperatorMethod families)
//      are LIFTED TO NAMESPACE SCOPE in this header -- C# nested classes see the enclosing
//      class's private members, but C++ namespace-scope classes cannot, so the port widens
//      the CSharpOperators members they need (`Lift`/`MakeParameter`/`MakeNullableParameter`/
//      `Compilation()`) from C# private to public (the `CSharpConversions` Detail::-free-
//      function visibility-widening precedent). The TDD tests exercise them directly.
//  (b) The C# `IParameter[] normalParameters` / `IParameter[] nullableParameters` (the
//      GC-owned arrays, `new IParameter[(int)(TypeCode.String + 1 - TypeCode.Object)]` /
//      `new IParameter[(int)(TypeCode.Decimal + 1 - TypeCode.Boolean)]`) port to owning
//      `std::vector<std::shared_ptr<const IParameter>>` members (17 / 13 entries; the C#
//      reference semantics -- the tables are the single owner and every operator table
//      SHARES the parameter instances -- is carried by the shared_ptr ownership).
//  (c) The C# `IParameter MakeParameter(TypeCode)` / `IParameter MakeNullableParameter(
//      IParameter)` return a GC reference; the port returns `std::shared_ptr<const
//      IParameter>` (an owning handle mirroring the reference -- the derived operator
//      methods keep the parameter alive in their own parameter lists). The nullable
//      lookup's `normalParameter == normalParameters[i - TypeCode.Object]` is C#
//      REFERENCE equality; the port compares addresses (`&normalParameter == ...get()`).
//  (d) The C# `MakeNullableParameter` throws `ArgumentException` for a parameter not in
//      the table; the port throws `std::invalid_argument` (the DefaultParameter
//      ArgumentNullException convention).
//  (e) The C# `OperatorMethod` field `internal readonly List<IParameter> parameters`
//      (mutated by the derived ctors' `parameters.Add(...)`) and property `IType
//      ReturnType { get; internal set; } = null!` (assigned by the derived ctors) port to
//      PROTECTED members `parameters_` / `returnType_` the derived classes write directly.
//      Reading `ReturnType` before a derived ctor assigned it would NRE in the C#
//      (`null!`); the port returns the `UnknownType()` null object as the documented safe
//      faithful fallback (the D516 null-guard precedent).
//  (f) The C# `public virtual OperatorMethod? Lift(CSharpOperators operators) => null`
//      ports to `virtual std::shared_ptr<OperatorMethod> Lift(const CSharpOperators&)
//      const` returning nullptr by default; the derived lifted classes (later slices)
//      override it. `OperatorMethod` is NOT `final` (the C# is unsealed).
//  (g) The C# `sealed class CSharpOperators` ports to a `final` C++ class, pinned by
//      static_assert in the tests.
//  (h) `ToString` is a PLAIN member (the port's ISymbol/IMember surface carries no
//      virtual `ToString`; the SpecializedParameter / DummyTypeParameter plain-member
//      precedent). It renders the C# signature format
//      `"<ReturnType> operator(<paramType, ...>)"` where each type renders as its
//      ReflectionName (the C# `AbstractType.ToString() => ReflectionName`).
//  (i) The C# explicit-interface members with fixed values (`SymbolKind ISymbol.SymbolKind
//      => SymbolKind.Operator`, `bool IEntity.IsStatic => true`, `Accessibility
//      IEntity.Accessibility => Accessibility.Public`, ...) port to plain overrides with
//      the same fixed values; the inherited member names that share a namespace-scope
//      enum name (`SymbolKind`/`Accessibility`) are globally qualified per the D372
//      name-hiding crux.

#pragma once

#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (MainModule -- the ParentModule inline body)
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // OperatorMethod's base (brings IType.hpp/IEntity.hpp)
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // TypeCode (MakeParameter's parameter type)

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
// Forward-declared (the declarations below need only shared_ptr of it; the .cpp includes
// the full header).
class IParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (the return type of `Lift` below; declared in this header).
class CSharpOperators;

// The C# `internal class OperatorMethod : IParameterizedMember` (nested inside
// `CSharpOperators`, lines 102-235; lifted to namespace scope, convention (a)) -- the
// common base of every built-in operator method. It carries the compilation, the
// parameter list, and the return type, and fixes the built-in-operator member surface:
// the name is always "operator", the symbol kind is always Operator, the member is
// always static and public, it has no declaring type (UnknownType), the substitution is
// the identity, and equality is reference equality. The `Lift` virtual is the hook the
// derived lifted-operator classes override to build the Nullable<T> form.
class OperatorMethod : public ILSpy::Decompiler::TypeSystem::IParameterizedMember {
public:
    // The C# `protected OperatorMethod(ICompilation compilation)` -- the compilation is
    // bound at construction (the derived operator-method ctors forward the CSharpOperators
    // instance's compilation).
    explicit OperatorMethod(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(compilation) {}

    // The C# `public virtual OperatorMethod? Lift(CSharpOperators operators) => null` --
    // the default: a non-liftable operator. The derived classes (the Lifted*OperatorMethod
    // families, later slices) override this to build their Nullable<T> form.
    virtual std::shared_ptr<OperatorMethod> Lift(const CSharpOperators& operators) const
    {
        (void)operators;
        return nullptr;
    }

    // --- IParameterizedMember ---

    // The C# `IReadOnlyList<IParameter> Parameters` -- a by-value snapshot of non-owning
    // pointers over the internal parameter list (the IParameterizedMember::Parameters
    // convention).
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override;

    // --- ICompilationProvider ---

    // The C# `public ICompilation Compilation => compilation`.
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IMember ---

    // The C# `public IType ReturnType { get; internal set; } = null!` -- never read before
    // a derived ctor assigned it in the C#; the port's `returnType_` starts empty and the
    // accessor returns the `UnknownType()` null object until then (convention (e)).
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override;

    // The C# `IMember IMember.MemberDefinition => this`.
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override
    {
        return this;
    }

    // The C# `IEnumerable<IMember> IMember.ExplicitlyImplementedInterfaceMembers =>
    // EmptyList<IMember>.Instance` -- empty.
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }

    // The C# `bool IMember.IsExplicitInterfaceImplementation => false`.
    bool IsExplicitInterfaceImplementation() const override { return false; }

    // The C# `bool IMember.IsVirtual => false`.
    bool IsVirtual() const override { return false; }

    // The C# `bool IMember.IsOverride => false`.
    bool IsOverride() const override { return false; }

    // The C# `bool IMember.IsOverridable => false`.
    bool IsOverridable() const override { return false; }

    // The C# `TypeParameterSubstitution IMember.Substitution =>
    // TypeParameterSubstitution.Identity` -- the never-null identity singleton (a
    // non-owning pointer, the IMember::Substitution convention).
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override;

    // The C# `IMember IMember.Specialize(TypeParameterSubstitution substitution)`: the
    // identity substitution returns `this`; anything else throws NotSupportedException
    // (the built-in operator methods are never specialized). Out-of-line (the .cpp).
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* substitution)
        const override;

    // The C# `bool IMember.Equals(IMember? obj, TypeVisitor? typeNormalization) =>
    // this == obj` -- REFERENCE equality (no structural comparison).
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor* typeNormalization)
        const override
    {
        (void)typeNormalization;
        return obj == this;
    }

    // --- IEntity ---

    // The C# `string Name => "operator"` (also covers the shared INamedElement/ISymbol
    // `Name`).
    std::string Name() const override { return "operator"; }

    // The C# `System.Reflection.Metadata.EntityHandle MetadataToken => default` -- the
    // nil handle is 0 in the port's uint32 token surface.
    std::uint32_t MetadataToken() const override { return 0; }

    // The C# `ITypeDefinition? IEntity.DeclaringTypeDefinition => null`.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition()
        const override
    {
        return nullptr;
    }

    // The C# `public IType DeclaringType => SpecialType.UnknownType` -- a fresh
    // `UnknownType()` per call (the SpecialType null object; value-equal instances).
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override
    {
        return ILSpy::Decompiler::TypeSystem::UnknownType();
    }

    // The C# `IModule IEntity.ParentModule => compilation.MainModule`.
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override
    {
        return &compilation_.MainModule();
    }

    // The C# `IEnumerable<IAttribute> IEntity.GetAttributes() =>
    // EmptyList<IAttribute>.Instance` -- empty.
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes()
        const override
    {
        return {};
    }

    // The C# `bool IEntity.HasAttribute(KnownAttribute attribute) => false`.
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute /*attribute*/)
        const override
    {
        return false;
    }

    // The C# `IAttribute? IEntity.GetAttribute(KnownAttribute attribute) => null`.
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute /*attribute*/) const override
    {
        return nullptr;
    }

    // The C# `Accessibility IEntity.Accessibility => Accessibility.Public`. The return
    // type is globally qualified: the inherited member name shadows the namespace-scope
    // enum in the class body (the D372 crux).
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }

    // The C# `bool IEntity.IsStatic => true`.
    bool IsStatic() const override { return true; }

    // The C# `bool IEntity.IsAbstract => false`.
    bool IsAbstract() const override { return false; }

    // The C# `bool IEntity.IsSealed => false`.
    bool IsSealed() const override { return false; }

    // --- INamedElement ---

    // The C# `string INamedElement.FullName => "operator"`.
    std::string FullName() const override { return "operator"; }

    // The C# `string INamedElement.ReflectionName => "operator"`.
    std::string ReflectionName() const override { return "operator"; }

    // The C# `string INamedElement.Namespace => string.Empty`.
    std::string Namespace() const override { return {}; }

    // --- ISymbol ---

    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Operator`. The return type is
    // globally qualified: the inherited member name shadows the namespace-scope enum in
    // the class body (the D372 crux).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Operator;
    }

    // --- ToString ---

    // The C# `public override string ToString()` -- the signature renderer:
    // `"<ReturnType> operator(<paramType, ...>)"` where each type renders as its
    // ReflectionName (the C# AbstractType.ToString). A PLAIN member (convention (h)).
    // Out-of-line (the .cpp; the body dereferences the parameters).
    std::string ToString() const;

protected:
    // The C# `internal readonly List<IParameter> parameters` -- mutated by the derived
    // operator-method ctors (`parameters.Add(...)`); the shared ownership mirrors the C#
    // GC reference (the parameters come from the CSharpOperators tables).
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>> parameters_;

    // The C# `public IType ReturnType { get; internal set; } = null!` -- assigned by the
    // derived operator-method ctors (convention (e)).
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;

private:
    // The C# `readonly ICompilation compilation` -- non-owning (the compilation outlives
    // the per-compilation CSharpOperators instance).
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
};

// The C# `sealed class CSharpOperators` (lines 33-56 + 60-100) -- the per-compilation
// built-in operator-method tables. `Get` resolves the instance through the compilation's
// `CacheManager` (the CSharpConversions::Get precedent); the private ctor precomputes the
// parameter tables (`InitParameterArrays`), and the lazy operator-table properties (later
// slices) build their method lists through `Lift` with `MakeParameter`/`MakeNullableParameter`.
class CSharpOperators final {
public:
    // The C# `public static CSharpOperators Get(ICompilation compilation)` (lines 40-54)
    // -- the per-compilation instance cached on the compilation's `CacheManager` (the
    // CSharpConversions::Get precedent; implemented out-of-line in the .cpp).
    static CSharpOperators& Get(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation);

    // --- The C# private members (public in the port: the lifted-to-namespace-scope
    // --- OperatorMethod subclasses and the tests need them, convention (a)) ---

    // The C# `OperatorMethod[] Lift(params OperatorMethod[] methods)` (lines 60-70) -- the
    // result starts as a copy of ALL the original methods, then each method's lifted form
    // (`method.Lift(this)`, when non-null) is APPENDED: the lifted forms come after all
    // the originals, in iteration order. Taking/returning shared handles mirrors the C#
    // reference arrays (the arrays own the methods).
    std::vector<std::shared_ptr<OperatorMethod>> Lift(
        const std::vector<std::shared_ptr<OperatorMethod>>& methods) const;

    // The C# `IParameter MakeParameter(TypeCode code)` (line 89) -- the shared normal
    // (non-nullable) parameter for the built-in type:
    // `normalParameters[code - TypeCode.Object]`. The range is TypeCode.Object..String
    // (17 entries).
    std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter> MakeParameter(
        ILSpy::Decompiler::TypeSystem::TypeCode code) const;

    // The C# `IParameter MakeNullableParameter(IParameter normalParameter)` (lines 91-100)
    // -- the shared `Nullable<T>` counterpart of one of the normal parameters, matched by
    // REFERENCE equality. Note the C#'s index asymmetry: the NORMAL table is indexed
    // relative to TypeCode.Object, the NULLABLE table relative to TypeCode.Boolean (the
    // loop covers TypeCode.Boolean..Decimal, the 13 numeric primitives + Boolean). A
    // parameter outside the table throws (convention (d)).
    std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter> MakeNullableParameter(
        const ILSpy::Decompiler::TypeSystem::IParameter& normalParameter) const;

    // The C# `readonly ICompilation compilation` -- read by the derived operator-method
    // ctors (`base(operators.compilation)`); widened to an accessor for the
    // namespace-scope subclasses (convention (a)).
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const
    {
        return compilation_;
    }

private:
    // The C# `private CSharpOperators(ICompilation compilation)` -- private; `Get`
    // (a static member, which has private access) builds the instance. NOTE:
    // `std::make_shared` cannot construct through a private ctor, so `Get` builds the
    // shared handle via `new` (the IntersectionType::Create precedent).
    explicit CSharpOperators(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation);

    // The C# `void InitParameterArrays()` (lines 72-87) -- the precomputed parameter
    // tables: `DefaultParameter(compilation.FindType(i), string.Empty)` per
    // TypeCode.Object..String, and `DefaultParameter(NullableType.Create(compilation,
    // compilation.FindType(i)), string.Empty)` per TypeCode.Boolean..Decimal (implemented
    // out-of-line in the .cpp).
    void InitParameterArrays();

    // The C# `readonly ICompilation compilation`.
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;

    // The C# `IParameter[] normalParameters` / `IParameter[] nullableParameters`
    // (convention (b)): 17 / 13 entries, owning (the tables are the single owner; the
    // operator tables SHARE these parameter instances through the returned handles).
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>>
        normalParameters_;
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>>
        nullableParameters_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
