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
//   * PORTED: the binary operator region (CSharpOperators.cs lines 411-481 + 622-655): the
//     `BinaryOperatorMethod` base, `LambdaBinaryOperatorMethod<T1,T2>` (the CHECKED/UNCHECKED
//     func PAIR -- the C# `checked`/`unchecked` arithmetic semantics the resolver's
//     `CheckForOverflow` selects between -- with the types resolved from
//     `Type.GetTypeCode(typeof(T1))`/`(T2)`), `LiftedBinaryOperatorMethod` (the `Nullable<T>`
//     lifted form implementing `ILiftedOperator`), `StringConcatenation` (the built-in
//     `string + string` / `string + object` / `object + string` operators), and the eight
//     lazy arithmetic operator-table properties (`MultiplicationOperators` /
//     `DivisionOperators` / `RemainderOperators` / `AdditionOperators` /
//     `SubtractionOperators` / `ShiftLeftOperators` / `ShiftRightOperators` /
//     `UnsignedShiftRightOperators` -- the C# 4.0 spec sections 7.8.1-7.8.5).
//
//   * PORTED: the equality operator region (CSharpOperators.cs lines 700-786 + 789-862): the
//     `EqualityOperatorMethod` (a built-in `==`/`!=` operator over one TypeCode's operands
//     -- both parameters the TypeCode's shared normal-table instances, the return type
//     always Boolean, the `Type`/`Negate` fields, the `CanEvaluateAtCompileTime =>
//     Type != TypeCode.Object` flag, and the `Lift` guard that keeps the reference-typed
//     Object/String forms unlifted), the `LiftedEqualityOperatorMethod` (the `Nullable<T>`
//     form: BOTH parameters lifted but the return type STAYS the base's plain Boolean, the
//     same shared nullable parameter instance added twice), and the four lazy equality
//     operator-table properties (`ValueEqualityOperators` / `ValueInequalityOperators` /
//     `ReferenceEqualityOperators` / `ReferenceInequalityOperators`).
//
// The remaining derived operator-method families (the relational / bitwise /
// user-defined regions, lines 865-1168) and the lazy operator-table properties built on
// them are DEFERRED to later slices.
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
#include <stdexcept>
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

    // --- The C# lazy binary operator-table properties (lines 484-695, convention (m)) ---

    // The C# `OperatorMethod[] MultiplicationOperators` (the C# 4.0 spec 7.8.1
    // multiplication operator): the seven numeric originals (int, uint, long, ulong,
    // float, double, decimal -- each the checked/unchecked multiply pair), followed by
    // their lifted `Nullable<T>` forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& MultiplicationOperators() const;

    // The C# `OperatorMethod[] DivisionOperators` (the C# 4.0 spec 7.8.2 division
    // operator): the same seven originals with the division bodies, then their lifted
    // forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& DivisionOperators() const;

    // The C# `OperatorMethod[] RemainderOperators` (the C# 4.0 spec 7.8.3 remainder
    // operator): the same seven originals with the remainder bodies, then their lifted
    // forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& RemainderOperators() const;

    // The C# `OperatorMethod[] AdditionOperators` (the C# 4.0 spec 7.8.3 addition
    // operator): the seven numeric originals, then the three built-in string
    // concatenations (`string + string`, `string + object`, `object + string` -- the
    // StringConcatenation class, NOT lifted), then the seven lifted numeric forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& AdditionOperators() const;

    // The C# `OperatorMethod[] SubtractionOperators` (the C# 4.0 spec 7.8.4 subtraction
    // operator): the seven numeric originals, then their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& SubtractionOperators() const;

    // The C# `OperatorMethod[] ShiftLeftOperators` (the C# 4.0 spec 7.8.5 shift
    // operators): the four originals (int, uint, long, ulong -- each shifting by an int
    // count, the single-func ctor: a shift never overflows, so there is no
    // checked/unchecked distinction), followed by their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& ShiftLeftOperators() const;

    // The C# `OperatorMethod[] ShiftRightOperators`: the same four originals with the
    // right-shift bodies, then their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& ShiftRightOperators() const;

    // The C# `OperatorMethod[] UnsignedShiftRightOperators` (the C# 11 `>>>` operator):
    // the same four originals with the zero-filling right-shift bodies, then their
    // lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& UnsignedShiftRightOperators() const;

    // --- The C# lazy equality operator-table properties (lines 789-862, convention (m)) ---

    // The C# `OperatorMethod[] ValueEqualityOperators` (the C# 4.0 spec 7.10 value
    // equality operator `==`): the eight value-type originals (int, uint, long, ulong,
    // float, double, decimal, bool -- `valueEqualityOperatorsFor`, negate=false), followed
    // by their lifted `Nullable<T>` forms via `Lift`.
    const std::vector<std::shared_ptr<OperatorMethod>>& ValueEqualityOperators() const;

    // The C# `OperatorMethod[] ValueInequalityOperators` (the C# 4.0 spec 7.10 value
    // inequality operator `!=`): the same eight originals with negate=true, followed by
    // their lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& ValueInequalityOperators() const;

    // The C# `OperatorMethod[] ReferenceEqualityOperators` (the C# 4.0 spec 7.10
    // reference equality operator `==`): the Object and String originals (negate=false) --
    // reference-typed operands do not lift, so no lifted forms are appended.
    const std::vector<std::shared_ptr<OperatorMethod>>& ReferenceEqualityOperators() const;

    // The C# `OperatorMethod[] ReferenceInequalityOperators`: the Object and String
    // originals with negate=true, no lifted forms.
    const std::vector<std::shared_ptr<OperatorMethod>>& ReferenceInequalityOperators() const;

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

    // The C# `OperatorMethod[]? multiplicationOperators` (and the seven siblings) -- the
    // binary lazy memo fields (convention (m)).
    mutable std::vector<std::shared_ptr<OperatorMethod>> multiplicationOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> divisionOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> remainderOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> additionOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> subtractionOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> shiftLeftOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> shiftRightOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> unsignedShiftRightOperators_;

    // The C# `OperatorMethod[]? valueEqualityOperators` (and the three siblings) -- the
    // equality lazy memo fields (convention (m)).
    mutable std::vector<std::shared_ptr<OperatorMethod>> valueEqualityOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> valueInequalityOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> referenceEqualityOperators_;
    mutable std::vector<std::shared_ptr<OperatorMethod>> referenceInequalityOperators_;
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

// The binary `decimal` stand-in operators (the * / / / % / + / - bodies the arithmetic
// operator tables below store, convention (l)): the System.Decimal SHAPE is a scaled
// magnitude with a separate sign, so the multiplicative operators combine the
// magnitudes/scales/signs and the additive operators align the scales first (the
// smaller-scale operand scaled up by 10^diff -- the System.Decimal addition alignment).
// The 64-bit stand-in magnitude wraps where the real 96-bit one would not (the
// uint64 -> int64 narrowing of a wrapped magnitude is implementation-defined before
// C++20; MSVC defines the two's-complement wrap), and the division scale handling is the
// direct approximation (the real System.Decimal raises the quotient scale to keep the
// precision); the full arithmetic fidelity arrives with the deferred constant-evaluation
// path (convention (j)). Division/remainder by a zero divisor throws -- the C# decimal
// DivideByZeroException in BOTH the checked and unchecked contexts.

// Normalizes a stand-in value to the (non-negative magnitude, authoritative sign flag)
// pair the binary operators combine: the flag is the sign the unary operators flip, and a
// negative mantissa (never produced in intended use -- the default is 0) folds into the
// flag so the magnitude stays non-negative.
inline Decimal NormalizeDecimal(Decimal value)
{
    if (value.mantissa < 0)
    {
        value.mantissa = static_cast<std::int64_t>(
            0ull - static_cast<std::uint64_t>(value.mantissa));
        value.isNegative = !value.isNegative;
    }
    return value;
}

// 10^power (unsigned wrap past 10^19 -- the stand-in's 64-bit magnitude standing in for
// the real 96-bit one).
inline std::uint64_t DecimalPow10(std::uint8_t power)
{
    std::uint64_t result = 1;
    for (std::uint8_t i = 0; i < power; i++)
        result *= 10ull;
    return result;
}

// The C# binary `a * b` (the same body for the checked/unchecked pair -- the stand-in
// wraps where the real System.Decimal would throw OverflowException).
inline Decimal operator*(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    Decimal result;
    result.mantissa = static_cast<std::int64_t>(static_cast<std::uint64_t>(a.mantissa)
                                                * static_cast<std::uint64_t>(b.mantissa));
    result.scale = static_cast<std::uint8_t>(a.scale + b.scale);
    result.isNegative = a.isNegative != b.isNegative;
    return result;
}

// The C# binary `a / b`.
inline Decimal operator/(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    if (b.mantissa == 0)
        throw std::runtime_error("DivideByZeroException");
    Decimal result;
    result.mantissa = static_cast<std::int64_t>(static_cast<std::uint64_t>(a.mantissa)
                                                / static_cast<std::uint64_t>(b.mantissa));
    // The stand-in approximates the result scale with the operand scale difference
    // (floored at 0); the real System.Decimal raises the scale to keep the quotient's
    // precision (the deferred constant-evaluation fidelity).
    result.scale = a.scale > b.scale ? static_cast<std::uint8_t>(a.scale - b.scale) : 0;
    result.isNegative = a.isNegative != b.isNegative;
    return result;
}

// The C# binary `a % b`: both operands aligned to the larger scale, the remainder taking
// the dividend's sign.
inline Decimal operator%(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    if (b.mantissa == 0)
        throw std::runtime_error("DivideByZeroException");
    const std::uint8_t scale = a.scale > b.scale ? a.scale : b.scale;
    std::uint64_t ma = static_cast<std::uint64_t>(a.mantissa);
    std::uint64_t mb = static_cast<std::uint64_t>(b.mantissa);
    if (a.scale < scale)
        ma *= DecimalPow10(static_cast<std::uint8_t>(scale - a.scale));
    if (b.scale < scale)
        mb *= DecimalPow10(static_cast<std::uint8_t>(scale - b.scale));
    Decimal result;
    const std::uint64_t remainder = ma % mb;
    result.mantissa = static_cast<std::int64_t>(remainder);
    result.scale = scale;
    result.isNegative = a.isNegative && remainder != 0;
    return result;
}

// The C# binary `a + b`: both magnitudes scaled to the larger scale, then combined by
// sign (the larger magnitude wins a sign mismatch; equal magnitudes cancel to +0).
inline Decimal operator+(Decimal a, Decimal b)
{
    a = NormalizeDecimal(a);
    b = NormalizeDecimal(b);
    const std::uint8_t scale = a.scale > b.scale ? a.scale : b.scale;
    std::uint64_t ma = static_cast<std::uint64_t>(a.mantissa);
    std::uint64_t mb = static_cast<std::uint64_t>(b.mantissa);
    if (a.scale < scale)
        ma *= DecimalPow10(static_cast<std::uint8_t>(scale - a.scale));
    if (b.scale < scale)
        mb *= DecimalPow10(static_cast<std::uint8_t>(scale - b.scale));
    Decimal result;
    std::uint64_t sum;
    bool negative;
    if (a.isNegative == b.isNegative)
    {
        sum = ma + mb;  // unsigned wrap
        negative = a.isNegative;
    }
    else
    {
        if (ma >= mb)
        {
            sum = ma - mb;
            negative = a.isNegative;
        }
        else
        {
            sum = mb - ma;
            negative = b.isNegative;
        }
    }
    result.mantissa = static_cast<std::int64_t>(sum);
    result.scale = scale;
    result.isNegative = negative && sum != 0;  // zero is sign-neutral
    return result;
}

// The C# binary `a - b` -- the addition of the negated subtrahend (the unary sign flip;
// a zero subtrahend's flipped sign cancels back out through the sum != 0 guard).
inline Decimal operator-(Decimal a, Decimal b)
{
    b.isNegative = !b.isNegative;
    return a + b;
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

// ---------------------------------------------------------------------------
// The binary operator region (CSharpOperators.cs lines 411-481 + 622-655)
// ---------------------------------------------------------------------------

// The C# `internal class BinaryOperatorMethod : OperatorMethod` (lines 412-421) -- the
// base of every binary built-in operator method: the constant-evaluation contract, the
// mirror of `UnaryOperatorMethod`. The C# `object? Invoke(CSharpResolver resolver,
// object? lhs, object? rhs)` virtual pair (throwing NotSupportedException here,
// overridden by the lambda-backed operators) is deferred as a whole (convention (j): the
// resolver is the parameter type, the only caller, and the only dependency).
// Unsealed (the Lambda/Lifted/StringConcatenation classes derive it).
class BinaryOperatorMethod : public OperatorMethod {
public:
    // The C# `public BinaryOperatorMethod(ICompilation compilation) : base(compilation)`.
    explicit BinaryOperatorMethod(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : OperatorMethod(compilation)
    {
    }

    // The C# `public virtual bool CanEvaluateAtCompileTime => false` -- the base default
    // (the lambda-backed operators override it to true).
    virtual bool CanEvaluateAtCompileTime() const { return false; }
};

// The C# `sealed class LiftedBinaryOperatorMethod : BinaryOperatorMethod, ILiftedOperator`
// (lines 463-481) -- the `Nullable<T>` form of a binary operator: the return type and
// both parameters lifted to their `Nullable<T>` counterparts (the shared nullable
// parameter-table instances), the `ILiftedOperator` surface (the D549 standalone base)
// exposing the pre-lifting signature. `final` (the C# sealed). The `Lift`/
// `CanEvaluateAtCompileTime` inherited defaults are faithful: a lifted operator is not
// lifted again (the OperatorMethod default returns null) and is not itself
// constant-evaluable (the C# does not override the flag -- the resolver lifts null
// operands itself; only the non-lifted lambda operators carry the flag).
class LiftedBinaryOperatorMethod final : public BinaryOperatorMethod, public ILiftedOperator {
public:
    // The C# `public LiftedBinaryOperatorMethod(CSharpOperators operators,
    // BinaryOperatorMethod baseMethod) : base(operators.compilation)` -- out-of-line in the
    // .cpp (convention (a): the body reads the CSharpOperators parameter tables). The
    // parameter is `const&`: the only callers (the `Lift` overrides) are const (the base
    // `Lift` contract).
    LiftedBinaryOperatorMethod(const CSharpOperators& operators,
                               const BinaryOperatorMethod& baseMethod);

    // --- ILiftedOperator ---

    // The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` --
    // a by-value snapshot of non-owning pointers (the ILiftedOperator convention (b)).
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> NonLiftedParameters()
        const override;

    // The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
    const ILSpy::Decompiler::TypeSystem::IType& NonLiftedReturnType() const override;

private:
    // The C# `BinaryOperatorMethod baseMethod` reference field -- the non-owning back-
    // pointer, convention (n).
    const BinaryOperatorMethod* baseMethod_;
};

// The C# `sealed class LambdaBinaryOperatorMethod<T1, T2> : BinaryOperatorMethod` (lines
// 423-461) -- the lambda-backed binary operator: the CHECKED/UNCHECKED func PAIR (the C#
// `Func<T1,T2,T1>` fields, stored for the deferred `Invoke`, convention (j) -- the funcs
// encode the C# `checked`/`unchecked` arithmetic semantics the resolver's
// `CheckForOverflow` selects between), the return type and the FIRST parameter resolved
// from `Type.GetTypeCode(typeof(T1))` and the second parameter from
// `Type.GetTypeCode(typeof(T2))` (the `TypeCodeFor<T>` trait, convention (k)),
// `CanEvaluateAtCompileTime => true`, and the `Lift` override building the `Nullable<T1>`
// form. `final` (the C# sealed).
template <typename T1, typename T2>
class LambdaBinaryOperatorMethod final : public BinaryOperatorMethod {
public:
    // The C# `public LambdaBinaryOperatorMethod(CSharpOperators operators, Func<T1,T2,T1>
    // func) : this(operators, func, func)` -- the single-func ctor (the shift tables: a
    // shift never overflows, so there is no checked/unchecked distinction).
    LambdaBinaryOperatorMethod(const CSharpOperators& operators, std::function<T1(T1, T2)> func)
        : LambdaBinaryOperatorMethod(operators, func, func)
    {
    }

    // The C# `public LambdaBinaryOperatorMethod(CSharpOperators operators,
    // Func<T1,T2,T1> checkedFunc, Func<T1,T2,T1> uncheckedFunc)`: `TypeCode t1 =
    // Type.GetTypeCode(typeof(T1)); this.ReturnType = operators.compilation.FindType(t1);
    // parameters.Add(operators.MakeParameter(t1)); parameters.Add(
    // operators.MakeParameter(Type.GetTypeCode(typeof(T2)))); this.checkedFunc =
    // checkedFunc; this.uncheckedFunc = uncheckedFunc;`.
    LambdaBinaryOperatorMethod(const CSharpOperators& operators,
                               std::function<T1(T1, T2)> checkedFunc,
                               std::function<T1(T1, T2)> uncheckedFunc)
        : BinaryOperatorMethod(operators.Compilation())
    {
        const ILSpy::Decompiler::TypeSystem::TypeCode t1 = TypeCodeFor<T1>::value;
        // The C# `operators.compilation.FindType(t1)` -- the ReflectionHelper TypeCode
        // lookup (the fully-qualified call: the sibling TypeSystem namespace is not
        // searched from inside the class body, the iteration-64 learning). The owning
        // handle is recovered through `shared_from_this()` + `const_pointer_cast` (the
        // D529 convention).
        const ILSpy::Decompiler::TypeSystem::IType& type =
            ILSpy::Decompiler::TypeSystem::FindType(operators.Compilation(), t1);
        returnType_ = std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
            type.shared_from_this());
        parameters_.push_back(operators.MakeParameter(t1));
        parameters_.push_back(operators.MakeParameter(TypeCodeFor<T2>::value));
        checkedFunc_ = std::move(checkedFunc);
        uncheckedFunc_ = std::move(uncheckedFunc);
    }

    // The C# `public override bool CanEvaluateAtCompileTime => true` -- the lambda-backed
    // operator is compile-time evaluable (the deferred `Invoke` applies the
    // checked/unchecked func the resolver's CheckForOverflow selects).
    bool CanEvaluateAtCompileTime() const override { return true; }

    // The C# `public override OperatorMethod Lift(CSharpOperators operators) => new
    // LiftedBinaryOperatorMethod(operators, this)`.
    std::shared_ptr<OperatorMethod> Lift(const CSharpOperators& operators) const override
    {
        return std::make_shared<LiftedBinaryOperatorMethod>(operators, *this);
    }

private:
    // The C# `readonly Func<T1,T2,T1> checkedFunc` / `uncheckedFunc` -- stored for the
    // deferred `Invoke` (convention (j)); the operator tables pass the C# lambda bodies
    // (the checked/unchecked arithmetic pairs, the shift bodies) through these members.
    std::function<T1(T1, T2)> checkedFunc_;
    std::function<T1(T1, T2)> uncheckedFunc_;
};

// The C# `sealed class StringConcatenation : BinaryOperatorMethod` (lines 622-655) -- the
// built-in `string + string` / `string + object` / `object + string` operators of the
// addition table: the return type is String, the parameters come from the two TypeCodes,
// and ONLY the `string + string` form is constant-evaluable (the C#
// `canEvaluateAtCompileTime = p1 == TypeCode.String && p2 == TypeCode.String` -- a
// `string + object` may invoke ToString at run time). NOT lifted (the inherited `Lift`
// returns null): the addition table's lifted forms come from the numeric lambdas only.
// The C# `Invoke` (`string.Concat(lhs, rhs)`) is deferred (convention (j)).
class StringConcatenation final : public BinaryOperatorMethod {
public:
    // The C# `public StringConcatenation(CSharpOperators operators, TypeCode p1,
    // TypeCode p2)`: `this.canEvaluateAtCompileTime = p1 == TypeCode.String &&
    // p2 == TypeCode.String; this.ReturnType = operators.compilation.FindType(
    // KnownTypeCode.String); parameters.Add(operators.MakeParameter(p1));
    // parameters.Add(operators.MakeParameter(p2));`.
    StringConcatenation(const CSharpOperators& operators,
                        ILSpy::Decompiler::TypeSystem::TypeCode p1,
                        ILSpy::Decompiler::TypeSystem::TypeCode p2)
        : BinaryOperatorMethod(operators.Compilation()),
          canEvaluateAtCompileTime_(
              p1 == ILSpy::Decompiler::TypeSystem::TypeCode::String
              && p2 == ILSpy::Decompiler::TypeSystem::TypeCode::String)
    {
        const ILSpy::Decompiler::TypeSystem::IType& stringType =
            operators.Compilation().FindType(
                ILSpy::Decompiler::TypeSystem::KnownTypeCode::String);
        returnType_ = std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
            stringType.shared_from_this());
        parameters_.push_back(operators.MakeParameter(p1));
        parameters_.push_back(operators.MakeParameter(p2));
    }

    // The C# `public override bool CanEvaluateAtCompileTime => canEvaluateAtCompileTime`.
    bool CanEvaluateAtCompileTime() const override { return canEvaluateAtCompileTime_; }

private:
    // The C# `bool canEvaluateAtCompileTime`.
    bool canEvaluateAtCompileTime_;
};

// ---------------------------------------------------------------------------
// The equality operator region (CSharpOperators.cs lines 700-786)
// ---------------------------------------------------------------------------

// Forward-declared (the LiftedEqualityOperatorMethod ctor parameter and back-pointer
// member need only declarations, not the full definition; the ctor is out-of-line).
class EqualityOperatorMethod;

// The C# `sealed class LiftedEqualityOperatorMethod : BinaryOperatorMethod,
// ILiftedOperator` (lines 753-786) -- the `Nullable<T>` form of a value equality/
// inequality operator: BOTH parameters are lifted to their shared `Nullable<T>`
// counterparts (the SAME instance added twice), but the return type STAYS the base's
// plain Boolean (a lifted comparison of possibly-null operands still produces a definite
// bool -- `null == null` is true, not null). Declared BEFORE EqualityOperatorMethod (the
// LiftedUnaryOperatorMethod / LiftedBinaryOperatorMethod reorder precedent): the base's
// inline `Lift` body constructs this class through `std::make_shared`, which needs the
// complete type. `final` (the C# sealed).
class LiftedEqualityOperatorMethod final : public BinaryOperatorMethod, public ILiftedOperator {
public:
    // The C# `public LiftedEqualityOperatorMethod(CSharpOperators operators,
    // EqualityOperatorMethod baseMethod) : base(operators.compilation)` -- out-of-line in
    // the .cpp (convention (a): the body reads the CSharpOperators parameter tables). The
    // parameter is `const&`: the only caller, the base's `Lift` override, is const (the
    // base `Lift` contract).
    LiftedEqualityOperatorMethod(const CSharpOperators& operators,
                                 const EqualityOperatorMethod& baseMethod);

    // The C# `public override bool CanEvaluateAtCompileTime =>
    // baseMethod.CanEvaluateAtCompileTime` -- delegates to the base method's flag
    // (out-of-line: the body dereferences the baseMethod_ back-pointer).
    bool CanEvaluateAtCompileTime() const override;

    // --- ILiftedOperator ---

    // The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` --
    // a by-value snapshot of non-owning pointers (the ILiftedOperator convention (b)).
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> NonLiftedParameters()
        const override;

    // The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
    const ILSpy::Decompiler::TypeSystem::IType& NonLiftedReturnType() const override;

private:
    // The C# `EqualityOperatorMethod baseMethod` reference field -- the non-owning back-
    // pointer, convention (n).
    const EqualityOperatorMethod* baseMethod_;
};

// The C# `sealed class EqualityOperatorMethod : BinaryOperatorMethod` (lines 701-751) --
// a built-in `==` / `!=` operator over one TypeCode's operands: both parameters are the
// TypeCode's shared normal-table instances (the diagonal `T == T` shape -- the SAME
// instance added twice) and the return type is always Boolean. The `Type`/`Negate` fields
// distinguish the value tables (`==` negate=false / `!=` negate=true) and drive the
// deferred `Invoke` and the `Lift` guard. The C# `object Invoke(CSharpResolver resolver,
// object? lhs, object? rhs)` (the null-operand short-circuits, the `CSharpPrimitiveCast`
// conversions, and the Single/Double/object.Equals comparison) is deferred as a whole
// (convention (j): the resolver is the parameter type, the only caller, and the only
// dependency). `final` (the C# sealed).
class EqualityOperatorMethod final : public BinaryOperatorMethod {
public:
    // The C# `public EqualityOperatorMethod(CSharpOperators operators, TypeCode type, bool
    // negate) : base(operators.compilation)`: `this.Negate = negate; this.Type = type;
    // this.ReturnType = operators.compilation.FindType(KnownTypeCode.Boolean);
    // parameters.Add(operators.MakeParameter(type)); parameters.Add(
    // operators.MakeParameter(type));` -- the SAME shared parameter instance is added
    // twice. The TypeCode is fully qualified: the sibling TypeSystem namespace is not
    // searched from inside the class body (the iteration-64 learning); the owning return-
    // type handle is recovered through `shared_from_this()` + `const_pointer_cast` (the
    // D529 convention).
    EqualityOperatorMethod(const CSharpOperators& operators,
                           ILSpy::Decompiler::TypeSystem::TypeCode type, bool negate)
        : BinaryOperatorMethod(operators.Compilation()), type_(type), negate_(negate)
    {
        const ILSpy::Decompiler::TypeSystem::IType& booleanType =
            operators.Compilation().FindType(
                ILSpy::Decompiler::TypeSystem::KnownTypeCode::Boolean);
        returnType_ = std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
            booleanType.shared_from_this());
        parameters_.push_back(operators.MakeParameter(type));
        parameters_.push_back(operators.MakeParameter(type));
    }

    // The C# `public readonly TypeCode Type` -- the operand type the operator compares.
    // (No name clash: the OperatorMethod hierarchy carries no `Type` member -- only
    // `IVariable::Type()`, which IParameter -- not these members -- derives.)
    ILSpy::Decompiler::TypeSystem::TypeCode Type() const { return type_; }

    // The C# `public readonly bool Negate` -- true for the `!=` operators.
    bool Negate() const { return negate_; }

    // The C# `public override bool CanEvaluateAtCompileTime => Type != TypeCode.Object` --
    // only the Object form is not constant-evaluable (a reference comparison is not
    // foldable; even the String form folds).
    bool CanEvaluateAtCompileTime() const override
    {
        return type_ != ILSpy::Decompiler::TypeSystem::TypeCode::Object;
    }

    // The C# `public override OperatorMethod? Lift(CSharpOperators operators)`: the Object
    // and String forms have reference-typed operands -- they do not lift (a lifted
    // operator needs `Nullable<T>` value-type operands); every other TypeCode builds the
    // LiftedEqualityOperatorMethod.
    std::shared_ptr<OperatorMethod> Lift(const CSharpOperators& operators) const override
    {
        if (type_ == ILSpy::Decompiler::TypeSystem::TypeCode::Object
            || type_ == ILSpy::Decompiler::TypeSystem::TypeCode::String)
        {
            return nullptr;
        }
        return std::make_shared<LiftedEqualityOperatorMethod>(operators, *this);
    }

private:
    // The C# `public readonly TypeCode Type` / `public readonly bool Negate`.
    const ILSpy::Decompiler::TypeSystem::TypeCode type_;
    const bool negate_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
