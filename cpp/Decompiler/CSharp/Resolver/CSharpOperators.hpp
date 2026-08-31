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
//   * PORTED: the unary operator region (CSharpOperators.cs lines 237-299 + 300-409): the
//     `UnaryOperatorMethod` base (the `CanEvaluateAtCompileTime` constant-evaluation flag),
//     `LambdaUnaryOperatorMethod<T>` (the lambda-backed compile-time-evaluable operator:
//     the parameter and return types resolved from `Type.GetTypeCode(typeof(T))` through
//     `FindType` + `MakeParameter`, the `Lift` override), `LiftedUnaryOperatorMethod` (the
//     `Nullable<T>` lifted form implementing `ILiftedOperator`), and the five lazy
//     operator-table properties (`UnaryPlusOperators` / `UncheckedUnaryMinusOperators` /
//     `CheckedUnaryMinusOperators` / `LogicalNegationOperators` / `BitwiseComplementOperators`
//     -- each the originals followed by their lifted forms via `Lift`).
//
// The remaining derived operator-method families (the binary / equality / relational
// regions, lines 411-1101) and the lazy operator-table properties built on them are
// DEFERRED to later slices.
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
//  (j) The C# `object? Invoke(CSharpResolver resolver, object? input)` virtual pair (the
//      constant-evaluation entry on `UnaryOperatorMethod`/`LambdaUnaryOperatorMethod<T>`)
//      is DEFERRED until `CSharpResolver` ports: the resolver is the parameter type, the
//      only caller (CSharpResolver.cs lines 511/931, wrapped in `catch (ArithmeticException)`),
//      and the only dependency (the `CSharpPrimitiveCast` the lambda body casts through).
//      `CanEvaluateAtCompileTime` (type-independent) lands now; the `Func<T,T>` is STORED
//      by the ctor (the C# field) and consumed once `Invoke` lands.
//  (k) The C# `Type.GetTypeCode(typeof(T))` (the ctor's parameter/return-type resolution,
//      and the deferred `Invoke`'s cast target) ports to the `TypeCodeFor<T>` compile-time
//      trait below -- the BCL `TypeCode` mapping of the primitive types the operator tables
//      instantiate (the C# `int`/`uint`/`long`/`ulong`/`float`/`double`/`decimal`/`bool`
//      spell the port's `std::int32_t`/`std::uint32_t`/`std::int64_t`/`std::uint64_t`/
//      `float`/`double`/`Decimal`/`bool`).
//  (l) The C# `decimal` language alias (System.Decimal) ports to the minimal `Decimal`
//      stand-in below: the unary operator tables need the TYPE (`TypeCode::Decimal` ->
//      `FindType` -> `MakeParameter`) and the unary `+`/`-` lambda bodies; the full 96-bit
//      scaled-decimal arithmetic fidelity arrives with the deferred constant-evaluation
//      path (the funcs are stored, not yet invoked). The stand-in keeps the scaled-decimal
//      SHAPE (a mantissa + a scale + a separate sign bit), so unary negation is the sign
//      flip -- the faithful System.Decimal negation.
//  (m) The C# `LazyInit.VolatileRead`/`LazyInit.GetOrSet` lazy table properties port to
//      compute-on-first-call memoization: `mutable` members + const getters (the port is
//      single-threaded; the empty vector is the not-yet-built sentinel, sound because
//      every built table holds at least its non-empty originals).
//  (n) The C# `LiftedUnaryOperatorMethod`'s `UnaryOperatorMethod baseMethod` reference
//      field ports to a non-owning `const UnaryOperatorMethod*`: the original is owned by
//      the same operator-table list that owns the lifted form (`CSharpOperators::Lift`
//      copies the originals into the result list alongside the lifted forms), so both
//      share the list's lifetime -- the C# GC-reference semantics. A caller invoking a
//      method's `Lift` directly (not through `Lift(...)`) must keep the original alive
//      itself (the owning handle stays in the caller's scope).

#pragma once

#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"  // LiftedUnaryOperatorMethod's second base
#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (MainModule -- the ParentModule inline body)
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // OperatorMethod's base (brings IType.hpp/IEntity.hpp)
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // TypeCode (MakeParameter's parameter type)

#include <cstdint>
#include <functional>
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

    // --- The C# lazy unary operator-table properties (lines 300-409, convention (m)) ---

    // The C# `OperatorMethod[] UnaryPlusOperators` (the C# 4.0 spec 7.7.1 unary plus
    // operator): the seven numeric originals (int, uint, long, ulong, float, double,
    // decimal -- each `i => +i`), followed by their lifted `Nullable<T>` forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& UnaryPlusOperators() const;

    // The C# `OperatorMethod[] UncheckedUnaryMinusOperators` (the C# 4.0 spec 7.7.2 unary
    // minus operator): the five signed-and-floating originals (int, long, float, double,
    // decimal -- each `i => unchecked(-i)`), followed by their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& UncheckedUnaryMinusOperators() const;

    // The C# `OperatorMethod[] CheckedUnaryMinusOperators`: the same five originals with
    // the `checked(-i)` bodies, followed by their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& CheckedUnaryMinusOperators() const;

    // The C# `OperatorMethod[] LogicalNegationOperators` (the C# spec draft-v11 12.9.4
    // logical negation operator): the single bool original (`b => !b`), followed by its
    // lifted `Nullable<bool>` form.
    const std::vector<std::shared_ptr<OperatorMethod>>& LogicalNegationOperators() const;

    // The C# `OperatorMethod[] BitwiseComplementOperators` (the C# 4.0 spec 7.7.4 bitwise
    // complement operator): the four integer originals (int, uint, long, ulong -- each
    // `i => ~i`), followed by their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& BitwiseComplementOperators() const;

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

    // The C# `OperatorMethod[]? unaryPlusOperators` (and the four siblings) -- the lazy
    // memo fields (convention (m)): `mutable` + const getters, the empty vector being the
    // not-yet-built sentinel.
    mutable std::vector<std::shared_ptr<OperatorMethod>> unaryPlusOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> uncheckedUnaryMinusOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> checkedUnaryMinusOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> logicalNegationOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> bitwiseComplementOperators_;
};

// ---------------------------------------------------------------------------
// The unary operator region (CSharpOperators.cs lines 237-299)
// ---------------------------------------------------------------------------

// The C# `decimal` language alias (System.Decimal) -- the minimal stand-in, convention
// (l). A real System.Decimal is a 96-bit mantissa + a scale (0..28) + a separate sign
// bit; the stand-in keeps that SHAPE (with the mantissa truncated to its low 64 bits) so
// the unary +/- lambda bodies compile faithfully -- negation is the sign flip, the
// System.Decimal negation semantics. The full arithmetic fidelity arrives with the
// deferred constant-evaluation path (convention (j)): the operator tables consume only
// the TYPE (`TypeCodeFor<Decimal>` -> `TypeCode::Decimal`), never a stored value.
struct Decimal {
    std::int64_t mantissa = 0;
    std::uint8_t scale = 0;
    bool isNegative = false;
};

// The C# unary `+d` -- the identity (System.Decimal defines unary plus as the identity).
inline Decimal operator+(Decimal value)
{
    return value;
}

// The C# unary `-d` -- the sign flip (System.Decimal negation never touches the
// mantissa/scale, so no signed-overflow edge exists).
inline Decimal operator-(Decimal value)
{
    value.isNegative = !value.isNegative;
    return value;
}

// The C# `Type.GetTypeCode(typeof(T))` -- convention (k): the compile-time `TypeCode` of
// the primitive types the operator tables instantiate. The C# resolves the parameter and
// return types through this mapping (`operators.compilation.FindType(typeCode)`); the
// specializations cover the BCL primitives `Type.GetTypeCode` distinguishes (a `T` without
// a specialization is a compile error -- the operator tables only instantiate these).
template <typename T>
struct TypeCodeFor;

template <>
struct TypeCodeFor<bool> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Boolean;
};
template <>
struct TypeCodeFor<std::int8_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::SByte;
};
template <>
struct TypeCodeFor<std::uint8_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Byte;
};
template <>
struct TypeCodeFor<std::int16_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Int16;
};
template <>
struct TypeCodeFor<std::uint16_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::UInt16;
};
template <>
struct TypeCodeFor<std::int32_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Int32;
};
template <>
struct TypeCodeFor<std::uint32_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::UInt32;
};
template <>
struct TypeCodeFor<std::int64_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Int64;
};
template <>
struct TypeCodeFor<std::uint64_t> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::UInt64;
};
template <>
struct TypeCodeFor<float> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Single;
};
template <>
struct TypeCodeFor<double> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Double;
};
template <>
struct TypeCodeFor<Decimal> {
    static constexpr ILSpy::Decompiler::TypeSystem::TypeCode value =
        ILSpy::Decompiler::TypeSystem::TypeCode::Decimal;
};

// The C# `internal class UnaryOperatorMethod : OperatorMethod` (lines 237-250) -- the base
// of every unary built-in operator method: the constant-evaluation contract. The C#
// `object? Invoke(CSharpResolver resolver, object? input)` virtual throws
// NotSupportedException here and is overridden by the lambda-backed operators; the port
// defers the whole `Invoke` member (convention (j)) and lands the type-independent
// `CanEvaluateAtCompileTime` flag. Unsealed (the Lambda/Lifted classes derive it).
class UnaryOperatorMethod : public OperatorMethod {
public:
    // The C# `public UnaryOperatorMethod(ICompilation compilation) : base(compilation)`.
    explicit UnaryOperatorMethod(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : OperatorMethod(compilation)
    {
    }

    // The C# `public virtual bool CanEvaluateAtCompileTime => false` -- the base default:
    // a built-in operator that cannot be constant-folded (the lambda-backed operators
    // override it to true; the resolver consults it before invoking -- CSharpResolver.cs
    // line 506/926).
    virtual bool CanEvaluateAtCompileTime() const { return false; }
};

// The C# `sealed class LiftedUnaryOperatorMethod : UnaryOperatorMethod, ILiftedOperator`
// (lines 284-296) -- the `Nullable<T>` form of a unary operator: the return type and the
// single parameter are both lifted to `Nullable<T>`, and the `ILiftedOperator` surface
// (the D549 standalone base) exposes the pre-lifting signature. `final` (the C# sealed).
// The `Lift`/`Invoke` inherited defaults are faithful: a lifted operator is not lifted
// again (the OperatorMethod default returns null) and is not itself constant-evaluable.
class LiftedUnaryOperatorMethod final : public UnaryOperatorMethod, public ILiftedOperator {
public:
    // The C# `public LiftedUnaryOperatorMethod(CSharpOperators operators, UnaryOperatorMethod
    // baseMethod) : base(operators.compilation)` -- implemented out-of-line in the .cpp
    // (convention (a): the body reads the CSharpOperators parameter tables). The parameter
    // is `const&`: the only caller, the lambda's `Lift` override, is const (the base
    // `Lift` contract).
    LiftedUnaryOperatorMethod(const CSharpOperators& operators,
                              const UnaryOperatorMethod& baseMethod);

    // --- ILiftedOperator ---

    // The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` --
    // a by-value snapshot of non-owning pointers (the ILiftedOperator convention (b)).
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> NonLiftedParameters()
        const override;

    // The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
    const ILSpy::Decompiler::TypeSystem::IType& NonLiftedReturnType() const override;

private:
    // The C# `UnaryOperatorMethod baseMethod` reference field -- the non-owning back-
    // pointer, convention (n).
    const UnaryOperatorMethod* baseMethod_;
};

// The C# `sealed class LambdaUnaryOperatorMethod<T> : UnaryOperatorMethod` (lines 252-282)
// -- the lambda-backed unary operator: `Func<T,T>` (stored for the deferred `Invoke`,
// convention (j)), the parameter and return types resolved from
// `Type.GetTypeCode(typeof(T))` (the `TypeCodeFor<T>` trait, convention (k)) through
// `operators.compilation.FindType(typeCode)` + `operators.MakeParameter(typeCode)`,
// `CanEvaluateAtCompileTime => true`, and the `Lift` override building the `Nullable<T>`
// form. `final` (the C# sealed).
template <typename T>
class LambdaUnaryOperatorMethod final : public UnaryOperatorMethod {
public:
    // The C# `public LambdaUnaryOperatorMethod(CSharpOperators operators, Func<T,T> func)`:
    // `TypeCode typeCode = Type.GetTypeCode(typeof(T)); this.ReturnType =
    // operators.compilation.FindType(typeCode); parameters.Add(operators.MakeParameter(
    // typeCode)); this.func = func;`.
    LambdaUnaryOperatorMethod(const CSharpOperators& operators, std::function<T(T)> func)
        : UnaryOperatorMethod(operators.Compilation())
    {
        const ILSpy::Decompiler::TypeSystem::TypeCode typeCode = TypeCodeFor<T>::value;
        // The C# `operators.compilation.FindType(typeCode)` -- the ReflectionHelper TypeCode
        // lookup (the fully-qualified call: the sibling TypeSystem namespace is not
        // searched from inside the class body, the iteration-64 learning). The owning
        // handle is recovered through `shared_from_this()` + `const_pointer_cast` (the
        // D529 convention: the registered types are shared-managed).
        const ILSpy::Decompiler::TypeSystem::IType& type =
            ILSpy::Decompiler::TypeSystem::FindType(operators.Compilation(), typeCode);
        returnType_ = std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
            type.shared_from_this());
        parameters_.push_back(operators.MakeParameter(typeCode));
        func_ = std::move(func);
    }

    // The C# `public override bool CanEvaluateAtCompileTime => true` -- the lambda-backed
    // operator is compile-time evaluable (the deferred `Invoke` applies the func).
    bool CanEvaluateAtCompileTime() const override { return true; }

    // The C# `public override OperatorMethod Lift(CSharpOperators operators) => new
    // LiftedUnaryOperatorMethod(operators, this)`.
    std::shared_ptr<OperatorMethod> Lift(const CSharpOperators& operators) const override
    {
        return std::make_shared<LiftedUnaryOperatorMethod>(operators, *this);
    }

private:
    // The C# `readonly Func<T,T> func` -- stored for the deferred `Invoke` (convention
    // (j)); the operator tables pass the C# lambda bodies (`+i`, `unchecked(-i)`,
    // `checked(-i)`, `!b`, `~i`) through this member.
    std::function<T(T)> func_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
