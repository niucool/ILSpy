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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `CSharpConversions` numeric-conversion helpers (the C# private instance methods
// `IsNumericType` / `AnyNumericConversion` / `ImplicitNumericConversion`, CSharpConversions.cs
// lines 391-455). Lifted to `Detail::` free functions for TDD testability (the D508
// `OverloadResolutionHelpers` precedent): the three helpers are pure -- they read no
// `CSharpConversions` instance state (no compilation, no conversion cache), only their `IType`
// arguments -- so the faithful free-function port preserves behavior exactly. They consume
// `ReflectionHelper.GetTypeCode` (D513) and the `TypeCode`/`TypeKind` enums; the
// `implicitNumericConversionLookup` table (C# line 377) is a file-local `constexpr` in the .cpp.
//
// These are the first `CSharpConversions` conversion helpers to land (the D512 skeleton deferred
// the conversion methods pending `NormalizeTypeVisitor.TypeErasure` and `ReflectionHelper.GetTypeCode`,
// both now ported). The numeric helpers themselves need only `GetTypeCode` + `Kind`, so they land
// ahead of the `IdentityConversion` (needs `NormalizeTypeVisitor.TypeErasure`) and the
// reference/boxing/nullable conversion helpers.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem { class IType; }
namespace ILSpy::Decompiler::TypeSystem { class ICompilation; }
namespace ILSpy::Decompiler::TypeSystem { class IMethod; }
namespace ILSpy::Decompiler::TypeSystem { enum class TypeCode : std::uint8_t; }

namespace ILSpy::Decompiler::Semantics { class Conversion; }
namespace ILSpy::Decompiler::Semantics { class ResolveResult; }
namespace ILSpy::Decompiler::Semantics { class TupleResolveResult; }

// `CSharpConversions` (the conversion controller the anonymous-function conversion threads to
// `LambdaResolveResult::IsValid`) and `LambdaResolveResult` (the RTTI target the anonymous-function
// arm dynamic_casts the resolve result to) are both defined in this directory; forward-declared
// here so the new helpers' signatures can reference them without pulling in the full headers
// (keeping the include graph minimal -- the .cpp includes the full headers for the bodies).
namespace ILSpy::Decompiler::CSharp::Resolver { class CSharpConversions; }
namespace ILSpy::Decompiler::CSharp::Resolver { class LambdaResolveResult; }

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

// The C# `bool IsNumericType(IType type)` (CSharpConversions.cs line 439) -- true if the type is a
// numeric primitive (char, the signed/unsigned integrals, the floating-point types, decimal) or a
// native integer (nint/nuint). The native integers have no `KnownTypeCode` (they are synthetic
// `TypeKind.NInt`/`NUInt` types, not `ITypeDefinition`s), so they are recognized via `Kind` before
// the `GetTypeCode` range check; the rest are the `[Char..Decimal]` range in the `TypeCode` domain.
bool IsNumericType(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `bool AnyNumericConversion(IType fromType, IType toType)` (line 451, C# spec draft-v11
// section 10.3.2 "numeric conversions" precondition) -- true iff both types are numeric. The
// actual conversion direction (implicit/explicit) is decided by `ImplicitNumericConversion` /
// the (deferred) explicit-numeric helpers.
bool AnyNumericConversion(const ILSpy::Decompiler::TypeSystem::IType& fromType,
                         const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool ImplicitNumericConversion(IType fromType, IType toType)` (line 391, C# 9.0 spec
// section 10.2.3) -- true if there is an implicit numeric conversion from `fromType` to `toType`.
// Native integers are treated as 64-bit when converting FROM (the full range fits in a 64-bit
// destination) and 32-bit when converting TO (only 32 bits store safely on a 32-bit platform):
// nint maps to `Int64` on the from-side and `Int32` on the to-side, nuint to `UInt64`/`UInt32`.
// Conversions to float/double/decimal exist from all integral types (`Char`..`UInt64`) plus
// `Single`->`Double`; integral-to-integral uses the `implicitNumericConversionLookup` table.
bool ImplicitNumericConversion(const ILSpy::Decompiler::TypeSystem::IType& fromType,
                               const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool ExplicitEnumerationConversion(IType fromType, IType toType)` (CSharpConversions.cs
// line 474, C# spec draft-v11 section 10.3.3 "explicit enumeration conversions") -- true iff an
// explicit enumeration conversion exists from `fromType` to `toType`: enum<->enum, enum<->any
// numeric primitive. A type with `Kind == Enum` converts to any other enum or to any numeric type;
// a numeric type converts to any enum. (The literal-0-to-enum *implicit* enumeration conversion is
// the separate `ImplicitEnumerationConversion(ResolveResult, IType)`, deferred with `ResolveResult`.)
// Pure like the numeric helpers -- reads only `IType.Kind` and `IsNumericType` (no `CSharpConversions`
// instance state) -- so it lands as a `Detail::` free function taking `const IType&`.
bool ExplicitEnumerationConversion(const ILSpy::Decompiler::TypeSystem::IType& fromType,
                                   const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion ImplicitEnumerationConversion(ResolveResult rr, IType toType)`
// (CSharpConversions.cs line 459, C# 9.0 spec section 10.2.4 "implicit enumeration conversions"
// + the enum part of section 10.2.6) -- the constant-0-to-enum implicit enumeration conversion.
// A compile-time constant whose type is a numeric primitive (`TypeCode` in [SByte, Decimal]) and
// whose value is zero (`Convert.ToDouble(ConstantValue) == 0`) converts implicitly to any enum
// type (the to-side is stripped of its nullable wrapper first, so `0 -> E?` is the lifted form).
// Returns `EnumerationConversion(true, IsNullable(toType))` (a factory -- a fresh per-call
// `NumericOrEnumerationConversion`) when the conversion fires, else `Conversions::None()`.
//
// Pure like the other conversion helpers (no `CSharpConversions` instance state -- reads only
// `ResolveResult.IsCompileTimeConstant` / `ResolveResult.Type` / `ResolveResult.ConstantValue`,
// `ReflectionHelper.GetTypeCode`, `NullableType.GetUnderlyingType` / `IsNullable`, `IType.Kind`),
// so it lands as a `Detail::` free function. Takes `const ResolveResult&` (every `ResolveResult`
// member it reads is `const`) and `const IType&` (the const `GetUnderlyingType` overload returns
// `const IType&`, and `GetTypeCode` takes `const IType&`) -- like `ImplicitConstantExpressionConversion`
// (D521), unlike the reference/boxing helpers that take non-const `IType&`. The
// `Convert.ToDouble(ConstantValue)` ports to a file-local `ConvertToDouble` helper that unboxes the
// `std::any` constant via the pointer-form `std::any_cast` (returns `nullopt` on a type mismatch --
// the safe faithful fallback for a divergent state the C# would `InvalidCastException` on; the guard
// returns `None` when the value cannot be read as a `double`).
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ImplicitEnumerationConversion(const ILSpy::Decompiler::Semantics::ResolveResult& rr,
                              const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `public bool IdentityConversion(IType fromType, IType toType)` (CSharpConversions.cs line 367,
// C# spec draft-v11 section 10.2.2 "identity conversion") -- true if `fromType` and `toType` are the
// same type after type erasure. Erasure (the `NormalizeTypeVisitor.TypeErasure` singleton) folds the
// differences that must not distinguish identity: object<->dynamic, IntPtr/UIntPtr<->nint/nuint,
// nullability annotations, custom modifiers, and tuple-vs-underlying-`ValueTuple`. Type parameters are
// NOT replaced (the `TypeErasure` configuration leaves `ReplaceClassTypeParametersWithDummy` /
// `ReplaceMethodTypeParametersWithDummy` false), so a `T` stays a `T` (the C# spec: a type parameter
// has an identity conversion only to itself).
//
// Lifted to a `Detail::` free function like the numeric helpers (the D508 precedent): `IdentityConversion`
// is pure -- it reads no `CSharpConversions` instance state (no compilation, no conversion cache), only
// the `TypeErasure` static singleton and its `IType` arguments. The signature takes `IType&` (non-const,
// unlike the numeric helpers' `const IType&`) because `IType::AcceptVisitor` is non-const (the D406
// convention -- a visitor may reconstruct the type), mirroring `NormalizeTypeVisitor::EquivalentTypes`.
bool IdentityConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                        ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion ImplicitNullableConversion(IType fromType, IType toType)` (CSharpConversions.cs
// line 490, C# 9.0 spec section 10.2.6) -- the implicit (lifted) nullable conversion. Acts ONLY when
// `toType` is `Nullable<T>`: it strips both types to their underlying types (`GetUnderlyingType`),
// then an identity conversion on the underlying types yields `Conversion.ImplicitNullableConversion`
// (the lifted identity), and an implicit numeric conversion on the underlying types yields
// `Conversion.ImplicitLiftedNumericConversion`. Otherwise `Conversion.None`. (Note `toType` must be
// nullable -- there is no implicit nullable conversion TO a non-nullable type.)
//
// Pure like the other conversion helpers (no `CSharpConversions` instance state), so it lands as a
// `Detail::` free function. Returns `std::shared_ptr<Conversion>` (the C# `Conversion` reference
// modeled as a shared handle; the two singleton returns come from `Conversions::ImplicitNullableConversion`
// / `Conversions::ImplicitLiftedNumericConversion`, the `None` from `Conversions::None`). The
// signature takes `IType&` (non-const) like `IdentityConversion` because it feeds the underlying
// types to `IdentityConversion(IType&, IType&)` (the non-const `AcceptVisitor`); it calls the
// already-ported `NullableType::IsNullable` / `NullableType::GetUnderlyingType` (D515),
// `IdentityConversion` (D514) and `ImplicitNumericConversion` (D514).
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ImplicitNullableConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                           ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion ExplicitNullableConversion(IType fromType, IType toType)` (CSharpConversions.cs
// line 505, C# spec draft-v11 section 10.3.4) -- the explicit (lifted) nullable conversion. Acts when
// EITHER operand is `Nullable<T>`: it strips both types to their underlying types, then an identity
// conversion yields `Conversion.ExplicitNullableConversion`, an any-numeric conversion yields
// `Conversion.ExplicitLiftedNumericConversion`, and an explicit enumeration conversion yields
// `Conversion.EnumerationConversion(false, true)` (explicit, lifted). Otherwise `Conversion.None`.
//
// Pure like the other conversion helpers, so it lands as a `Detail::` free function. Returns
// `std::shared_ptr<Conversion>`; the three singleton returns come from `Conversions::ExplicitNullableConversion`
// / `Conversions::ExplicitLiftedNumericConversion` / `Conversions::None`, the enumeration return from
// the `Conversions::EnumerationConversion(false, true)` FACTORY (a fresh per-call instance). Takes
// `IType&` (non-const) like `ImplicitNullableConversion` (feeds `IdentityConversion`); calls the
// already-ported `NullableType` helpers (D515), `IdentityConversion` (D514), `AnyNumericConversion`
// (D514) and `ExplicitEnumerationConversion` (D514).
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ExplicitNullableConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                           ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool NullLiteralConversion(IType fromType, IType toType)` (CSharpConversions.cs line 524,
// C# 9.0 spec section 10.2.7) -- true iff the null literal (`TypeKind.Null`) converts to `toType`:
// `Nullable<T>` (any nullable) or any reference type (`IsReferenceType == true`). A non-nullable
// value type (e.g. `int`) does NOT accept the null literal. Reads only `IType.Kind` / `IType.IsReferenceType`
// / `NullableType.IsNullable` (all const), so it takes `const IType&` like the numeric helpers.
bool NullLiteralConversion(const ILSpy::Decompiler::TypeSystem::IType& fromType,
                           const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `IType UnpackGenericArrayInterface(IType interfaceType)` (CSharpConversions.cs line 586)
// -- for `IList<T>` / `ICollection<T>` / `IEnumerable<T>` / `IReadOnlyList<T>` /
// `IReadOnlyCollection<T>`, returns the type argument `T`; otherwise null. A pure helper (no
// `CSharpConversions` instance state) consumed by the (deferred) `ImplicitReferenceConversion` /
// `ExplicitReferenceConversion` arms that unpack a single-dimensional array's generic-interface base.
// Reads only `ParameterizedType` / `GetDefinition` / `KnownTypeCode` / `GetTypeArgument` (all
// TypeSystem primitives), so it lands as a `Detail::` free function taking `const IType&` like the
// numeric helpers. Returns `const IType*` (nullable, non-owning -- the managed `IType` is owned by
// the `ParameterizedType`'s `typeArgs_`, outliving the call; the C# `IType` nullable reference ports
// to a nullable raw pointer, the `SkipModifiers` / `GetDefinition` convention).
const ILSpy::Decompiler::TypeSystem::IType*
UnpackGenericArrayInterface(const ILSpy::Decompiler::TypeSystem::IType& interfaceType);

// The C# `public bool IsImplicitReferenceConversion(IType fromType, IType toType)` (CSharpConversions.cs
// line 534, C# 9.0 spec section 10.2.8) -- true if there is an implicit reference conversion from
// `fromType` to `toType`. The public entry; delegates to `ImplicitReferenceConversion(fromType, toType, 0)`.
// Pure given a compilation (reads no `CSharpConversions` instance state beyond the compilation, which the
// recursion threads through for `compilation.FindType(KnownTypeCode.Array)`), so it lands as a `Detail::`
// free function taking `const ICompilation&` (the D508 precedent) like the other conversion helpers.
// Takes `IType&` (non-const) like `IdentityConversion` because the recursion feeds element/argument types
// to `IdentityConversion(IType&, IType&)` (the non-const `AcceptVisitor`, the D406 convention).
bool IsImplicitReferenceConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                    ILSpy::Decompiler::TypeSystem::IType& fromType,
                                    ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool ImplicitReferenceConversion(IType fromType, IType toType, int subtypeCheckNestingDepth)`
// (CSharpConversions.cs line 540, C# 9.0 spec section 10.2.8) -- the recursive worker behind
// `IsImplicitReferenceConversion`. Reference conversions are possible only when both types are known
// reference types (the `IsReferenceType == true` guard); then: array-to-array covariance (same dimensions
// + a recursive reference conversion on the element types), single-dimensional array to `IList<T>` /
// `ICollection<T>` / `IEnumerable<T>` / `IReadOnlyList<T>` / `IReadOnlyCollection<T>` (an identity or
// recursive reference conversion on the element vs the unpacked type argument), any array to
// `System.Array` (`compilation.FindType(KnownTypeCode.Array)` + recursion), or the inheritance-chain
// `IsSubtypeOf` arm. The `subtypeCheckNestingDepth` bounds the variance recursion (C# subtyping is
// undecidable, see Kennedy & Pierce; a depth > 10 short-circuits to false in `IsSubtypeOf`).
bool ImplicitReferenceConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                 ILSpy::Decompiler::TypeSystem::IType& fromType,
                                 ILSpy::Decompiler::TypeSystem::IType& toType,
                                 int subtypeCheckNestingDepth);

// The C# `bool IsSubtypeOf(IType s, IType t, int subtypeCheckNestingDepth)` (CSharpConversions.cs line
// 607) -- whether `s` is a subtype of `t`, used by `ImplicitReferenceConversion`, `BoxingConversion` and
// `ImplicitTypeParameterConversion`. A conversion to `dynamic` or `object` is always possible; otherwise
// the depth guard bounds the (undecidable) variance recursion, and `GetAllBaseTypes(s)` is traversed:
// if any base type has an `IdentityOrVarianceConversion` to `t`, `s` is a subtype of `t`.
bool IsSubtypeOf(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                 ILSpy::Decompiler::TypeSystem::IType& s,
                 ILSpy::Decompiler::TypeSystem::IType& t,
                 int subtypeCheckNestingDepth);

// The C# `bool IdentityOrVarianceConversion(IType s, IType t, int subtypeCheckNestingDepth)`
// (CSharpConversions.cs line 635) -- the per-base-type check `IsSubtypeOf` applies. When `s` has a
// definition: it must be the same definition as `t`, and (if both are parameterized) the type arguments
// must match by identity or by a variance-direction reference conversion (`Covariant` ->
// `ImplicitReferenceConversion(si, ti)`, `Contravariant` -> `ImplicitReferenceConversion(ti, si)`;
// an `Invariant` parameter or a count mismatch yields false). When `s` has no definition: a structural
// `s.Equals(t)` (e.g. two equal type parameters).
bool IdentityOrVarianceConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                  ILSpy::Decompiler::TypeSystem::IType& s,
                                  ILSpy::Decompiler::TypeSystem::IType& t,
                                  int subtypeCheckNestingDepth);

// The C# `bool IsSealedReferenceType(IType type)` (CSharpConversions.cs line 784) -- true if `type` is
// a sealed class or any delegate (delegates are implicitly sealed). A pure helper (reads only
// `IType.Kind` + `GetDefinition()->IsSealed`, no `CSharpConversions` instance state), so it lands as
// a `Detail::` free function taking `const IType&` like the numeric helpers. The C# short-circuits the
// `GetDefinition().IsSealed` deref behind `kind == TypeKind.Class` (so a delegate returns true without a
// definition); the port adds a `def != nullptr` guard before `def->IsSealed()` for the class arm (a
// class-kind type whose definition is unresolved, e.g. a `KnownType` placeholder, would NRE in the C# --
// the guard returns false, the safe faithful fallback, the D516 `def != nullptr` precedent).
bool IsSealedReferenceType(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `bool ExplicitReferenceConversion(IType fromType, IType toType)` (CSharpConversions.cs line
// 669, C# spec draft-v11 section 10.3.5) -- the explicit reference conversion. Both operands must be
// reference types (the `IsReferenceType == true` guard on both sides), with a type-parameter special
// case on the from-side (converting from `F` to `T` where `T : class, F` recurses `IsSubtypeOf(toType,
// fromType, 0)`). Then: array-to-array covariance (same dimensions + a recursive explicit reference
// conversion on the elements), array<->generic-interface unpacking, delegate variance (same definition
// + a per-type-argument identity-or-explicit-reference/contravariant-reference check), sealed-source /
// sealed-target fallbacks to the implicit reference conversion, and the unsealed-unsealed arm (an
// interface on either side is always convertible; otherwise an implicit reference conversion in either
// direction suffices).
//
// Pure given a compilation (reads no `CSharpConversions` instance state beyond the compilation, which
// the `IsSubtypeOf` / `IsImplicitReferenceConversion` calls thread through), so it lands as a `Detail::`
// free function taking `const ICompilation&` like the implicit-reference cluster. Recurses on itself
// (the array/delegate arms) WITHOUT a nesting-depth parameter (the C# recurses at the top level; the
// depth guard lives only inside `IsSubtypeOf`), so the signature mirrors `IsImplicitReferenceConversion`
// minus the depth. Takes `IType&` (non-const) like the implicit cluster because it feeds element/
// argument types to `IdentityConversion(IType&, IType&)` (the non-const `AcceptVisitor`) and to the
// non-const `IsSubtypeOf` / `IsImplicitReferenceConversion` recursion.
bool ExplicitReferenceConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                 ILSpy::Decompiler::TypeSystem::IType& fromType,
                                 ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool IsBoxingConversion(IType fromType, IType toType)` (CSharpConversions.cs line 791,
// C# 9.0 spec section 10.2.9) -- true if the conversion from `fromType` to `toType` is a boxing
// conversion. Strips the nullable wrapper from the from-side first (a `Nullable<T>` boxes as its
// underlying `T`), then requires: the from-side is a non-nullable value type
// (`IsReferenceType == false`), the from-side is not by-ref-like (ref structs cannot be boxed),
// and the to-side is a reference type (`IsReferenceType == true`); with the guard satisfied, the
// boxing conversion is a subtype relation (`IsSubtypeOf(fromType, toType, 0)` -- the value type
// is a subtype of the reference type it implements, e.g. `int` -> `object`).
//
// Pure given a compilation (reads no `CSharpConversions` instance state beyond the compilation,
// which the `IsSubtypeOf` call threads through), so it lands as a `Detail::` free function taking
// `const ICompilation&` (the D517 reference-cluster precedent). Takes `IType&` (non-const) like
// the reference cluster because `GetUnderlyingType` returns `IType&` (the D515 non-const overload)
// and `IsSubtypeOf` takes `IType&` (the non-const `AcceptVisitor`, D406).
bool IsBoxingConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                        ILSpy::Decompiler::TypeSystem::IType& fromType,
                        ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool UnboxingConversion(IType fromType, IType toType)` (CSharpConversions.cs line 812,
// C# spec draft-v11 section 10.3.7) -- true if the conversion from `fromType` to `toType` is an
// unboxing conversion. Strips the nullable wrapper from the TO-side first (unboxing to a
// `Nullable<T>` unboxes the underlying `T`), then requires: the from-side is a reference type
// (`IsReferenceType == true`) and the to-side (after the nullable strip) is a value type
// (`IsReferenceType == false`); with the guard satisfied, the unboxing conversion is a subtype
// relation with the arguments SWAPPED (`IsSubtypeOf(toType, fromType, 0)` -- the value type is a
// subtype of the boxed reference type it was boxed from).
//
// Pure given a compilation (the `IsSubtypeOf` call), so it lands as a `Detail::` free function
// taking `const ICompilation&` like `IsBoxingConversion`. Takes `IType&` (non-const) for the same
// reason (`GetUnderlyingType` non-const overload + `IsSubtypeOf` non-const).
bool UnboxingConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                        ILSpy::Decompiler::TypeSystem::IType& fromType,
                        ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool ImplicitTypeParameterConversion(IType fromType, IType toType)` (CSharpConversions.cs
// line 870, C# 9.0 spec section 10.2.12) -- the implicit conversion involving a type parameter.
// Only a type parameter (`Kind == TypeParameter`) reaches the `IsSubtypeOf` arm; a type parameter
// whose `IsReferenceType` has a definite value (`true` or `false`) is already handled by
// `ImplicitReferenceConversion` / `IsBoxingConversion`, so only an INDETERMINATE (`std::nullopt`)
// type parameter proceeds to `IsSubtypeOf(fromType, toType, 0)`.
//
// Pure given a compilation (the `IsSubtypeOf` call), so it lands as a `Detail::` free function
// taking `const ICompilation&` like the boxing helpers. Takes `IType&` (non-const) because
// `IsSubtypeOf` takes `IType&`. Consumed by `IsBoxingConversionOrInvolvingTypeParameter` (the
// public boxing entry point) and the (deferred) `StandardImplicitConversion`.
bool ImplicitTypeParameterConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                     ILSpy::Decompiler::TypeSystem::IType& fromType,
                                     ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `public bool IsBoxingConversionOrInvolvingTypeParameter(IType fromType, IType toType)`
// (CSharpConversions.cs line 806) -- true if the conversion is a boxing conversion OR an implicit
// conversion involving a type parameter that might be a boxing conversion when instantiated with
// a value type. The public entry point; delegates to `IsBoxingConversion` /
// `ImplicitTypeParameterConversion`.
//
// Pure given a compilation (both callees thread it through), so it lands as a `Detail::` free
// function taking `const ICompilation&` like its callees. Takes `IType&` (non-const) for the same
// reason.
bool IsBoxingConversionOrInvolvingTypeParameter(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                                ILSpy::Decompiler::TypeSystem::IType& fromType,
                                                ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool IsIntegerType(IType type)` (CSharpConversions.cs line 927) -- true for the native
// integers (`nint`/`nuint`, recognized via `Kind`) and the integral primitives (`sbyte`..`ulong`,
// the `[SByte..UInt64]` range in the `TypeCode` domain). A pure helper (reads only `IType.Kind` +
// `ReflectionHelper.GetTypeCode`, no `CSharpConversions` instance state), so it lands as a `Detail::`
// free function taking `const IType&` like the numeric helpers. Consumed by `ExplicitPointerConversion`.
bool IsIntegerType(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `bool ImplicitPointerConversion(IType fromType, IType toType)` (CSharpConversions.cs line
// 898, C# spec draft-v11 section 24.5) -- the implicit pointer conversion. Any pointer (`PointerType`
// or `FunctionPointer` -- `IsAnyPointer(Kind)`) converts to `void*` (a `PointerType` whose `ReflectionName`
// is `"System.Void*"`); the null literal (`TypeKind.Null`) converts to any pointer; and a function pointer
// converts to a function pointer with the same calling convention, the same parameter count, a return
// type convertible by identity or implicit reference conversion, and (contravariantly) parameter types
// convertible by identity or implicit reference conversion in the REVERSE direction (the body's
// `IdentityConversion(toPT, fromPT)` / `ImplicitReferenceConversion(toPT, fromPT)` -- swapped order).
//
// Pure given a compilation (the function-pointer variance arm calls `ImplicitReferenceConversion` which
// threads the compilation), so it lands as a `Detail::` free function taking `const ICompilation&` like
// the reference cluster. Takes `IType&` (non-const) like the reference cluster because the function-pointer
// return/parameter types (extracted via `ReturnType()` / `ParameterTypes()`, which return `IType&` through
// the `shared_ptr<IType>` deref) feed `IdentityConversion(IType&, IType&)` (the non-const `AcceptVisitor`,
// D406) and the non-const `ImplicitReferenceConversion`.
bool ImplicitPointerConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                               ILSpy::Decompiler::TypeSystem::IType& fromType,
                               ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool ExplicitPointerConversion(IType fromType, IType toType)` (CSharpConversions.cs line
// 917, C# spec draft-v11 section 24.5) -- the explicit pointer conversion. A pointer (any kind,
// `IsAnyPointer(Kind)`) converts to any other pointer or to any integer type; conversely any integer
// type converts to a pointer. Pure (reads only `IType.Kind` via `IsAnyPointer` + `IsIntegerType`, no
// `CSharpConversions` instance state), so it lands as a `Detail::` free function taking `const IType&`
// like the numeric helpers.
bool ExplicitPointerConversion(const ILSpy::Decompiler::TypeSystem::IType& fromType,
                               const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion ExplicitTypeParameterConversion(IType fromType, IType toType)` (CSharpConversions.cs
// line 880, C# spec draft-v11 section 10.3.6) -- the explicit conversion involving a type parameter.
// When the TO-side is a type parameter: an explicit conversion from an interface OR from a type the
// type parameter is a subtype of (`IsSubtypeOf(toType, fromType, 0)`) is an unboxing conversion; otherwise
// `None`. When the to-side is NOT a type parameter: a conversion from a type parameter to an interface is
// a boxing conversion; otherwise `None`.
//
// Pure given a compilation (the `IsSubtypeOf` call threads it through), so it lands as a `Detail::`
// free function taking `const ICompilation&` like the boxing helpers. Takes `IType&` (non-const)
// because `IsSubtypeOf` takes `IType&` (the non-const `AcceptVisitor`, D406). Returns
// `std::shared_ptr<Conversion>` (the C# `Conversion` reference modeled as a shared handle; the
// `UnboxingConversion` / `BoxingConversion` singleton returns come from `Conversions::UnboxingConversion`
// / `Conversions::BoxingConversion`, the `None` from `Conversions::None`).
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ExplicitTypeParameterConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                 ILSpy::Decompiler::TypeSystem::IType& fromType,
                                 ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool ImplicitConstantExpressionConversion(ResolveResult rr, IType toType)`
// (CSharpConversions.cs line 823, C# 9.0 spec section 10.2.11) -- the implicit constant-expression
// conversion. A compile-time constant (`rr.IsCompileTimeConstant`) of type `int` (`Int32`) or `long`
// (`Int64`) converts implicitly to an integral type whose range contains the constant value: a
// non-negative `long` converts to `ulong`; an `int` converts to `sbyte`/`byte`/`short`/`ushort`/
// `uint`/`ulong` when the value fits the target's range (and is non-negative for the unsigned
// targets). The to-side is stripped of its nullable wrapper first (`GetUnderlyingType`), and a
// `nuint` to-side is treated as `UInt32` (only 32 bits store safely on a 32-bit platform).
//
// Pure like the other conversion helpers (no `CSharpConversions` instance state -- reads only
// `ResolveResult.IsCompileTimeConstant` / `ResolveResult.Type` / `ResolveResult.ConstantValue`,
// `ReflectionHelper.GetTypeCode`, `NullableType.GetUnderlyingType`, `IType.Kind`), so it lands as a
// `Detail::` free function. Takes `const ResolveResult&` (every `ResolveResult` member it reads --
// `IsCompileTimeConstant` / `Type` / `ConstantValue` -- is `const`) and `const IType&` (the const
// `GetUnderlyingType` overload returns `const IType&`, and `GetTypeCode` takes `const IType&`) --
// unlike the reference/boxing helpers that take non-const `IType&` for the non-const `AcceptVisitor`
// and the non-const `GetUnderlyingType` overload. The `ConstantValue` (`std::any`, the D424/D374
// `object?` model) unbox ports via the pointer-form `std::any_cast` (returns `nullptr` on a type
// mismatch rather than throwing -- the safe faithful fallback for a divergent state the C# would
// `InvalidCastException` on).
bool ImplicitConstantExpressionConversion(const ILSpy::Decompiler::Semantics::ResolveResult& rr,
                                          const ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `public Conversion StandardImplicitConversion(IType fromType, IType toType)`
// (CSharpConversions.cs line 201, C# 9.0 spec section 10.4.2) -- the standard implicit conversion
// dispatch entry point. Checks the already-ported conversion helpers in spec order: identity,
// numeric, nullable (returns a Conversion, checked via pointer-identity against
// `Conversions::None()`), null-literal, reference, boxing, type-parameter (yields a boxing
// conversion when not also a reference conversion), pointer; the tuple/inline-array/span arms are
// deferred (need TupleResolveResult/IsInlineArrayType/Span machinery) and yield `None` for those
// shapes until ported. Returns the first matching Conversion singleton, else `Conversions::None()`.
//
// The C# `StandardImplicitConversion(fromType, toType)` calls the private
// `StandardImplicitConversion(fromType, toType, allowTupleConversion: true)` overload; the port
// collapses the overload into this Detail function (the tuple arm is deferred, so
// `allowTupleConversion` is effectively always true for the ported arms). Takes `const ICompilation&`
// (threaded to the reference/boxing/type-parameter/pointer helpers that need `FindType`/
// `IsSubtypeOf`) and `IType&` non-const (the helpers take non-const `IType&` for the non-const
// `AcceptVisitor`, D406). Returns `std::shared_ptr<Conversion>` -- the first matching singleton,
// else `Conversions::None()`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
StandardImplicitConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                          ILSpy::Decompiler::TypeSystem::IType& fromType,
                          ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion ExplicitConversionImpl(IType fromType, IType toType)` (CSharpConversions.cs
// line 308, C# spec draft-v11 section 10.4.3) -- the standard explicit conversion dispatch entry
// point. Called by the public `ExplicitConversion` methods AFTER the implicit conversions have
// been checked, so any remaining conversion must be explicit. Checks the already-ported helpers in
// spec order: `AnyNumericConversion` -> `ExplicitNumericConversion`, `ExplicitEnumerationConversion`
// -> `EnumerationConversion(false, false)` (explicit, not lifted), `ExplicitNullableConversion` (returns
// a Conversion, checked via pointer-identity against `Conversions::None()`), `ExplicitReferenceConversion`
// -> `ExplicitReferenceConversion`, `UnboxingConversion` -> `UnboxingConversion`,
// `ExplicitTypeParameterConversion` (returns a Conversion, checked against None), `ExplicitPointerConversion`
// -> `ExplicitPointerConversion`; the tuple arm (`TupleConversion(isExplicit: true)`) is deferred (needs
// `TupleResolveResult` machinery) and yields `None` until ported. Returns the first matching Conversion
// singleton, else `Conversions::None()`.
//
// The C# `ExplicitConversionImpl` is a private instance method that reads `this.compilation` (threaded to
// `ExplicitReferenceConversion` / `UnboxingConversion` / `ExplicitTypeParameterConversion` which need
// `IsSubtypeOf` / `FindType`). The port lifts it to a `Detail::` free function taking `const ICompilation&`
// (the D508 precedent) like `StandardImplicitConversion`. Takes `IType&` non-const (the helpers take
// non-const `IType&` for the non-const `AcceptVisitor`, D406). Returns `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ExplicitConversionImpl(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                        ILSpy::Decompiler::TypeSystem::IType& fromType,
                        ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion ExplicitConversionNotUserDefined(IType fromType, IType toType)`
// (CSharpConversions.cs line 331) -- the explicit conversion MINUS the user-defined-conversion
// fallback. This is the helper the user-defined-conversion resolution (`SelectOperator` /
// `UserDefinedImplicitConversion` / `UserDefinedExplicitConversion`) uses to compute the
// conversion before and after the user-defined operator: it returns the standard (implicit OR
// explicit) conversion between `fromType` and `toType`, never reaching the user-defined branch.
//
// The C# body is: `Conversion c = ImplicitConversion(fromType, toType, allowUserDefined: false,
// allowTuple: false); if (c != Conversion.None) return c; return ExplicitConversionImpl(fromType,
// toType);` -- it checks the standard implicit conversion FIRST, and only if no implicit
// conversion exists does it fall back to the standard explicit conversion. The private
// `ImplicitConversion(IType, IType, bool allowUserDefined, bool allowTuple)` overload (line 166)
// it calls is `StandardImplicitConversion(fromType, toType, allowTuple)` followed by an
// `allowUserDefined`-gated `UserDefinedImplicitConversion`; with `allowUserDefined: false` the
// user-defined branch is skipped, and the tuple arm is deferred (yields `None` for tuple shapes
// until ported), so `allowTuple` is effectively `true` for the ported arms. The faithful port
// reduces to the already-ported `StandardImplicitConversion` (D523) then `ExplicitConversionImpl`
// (D524).
//
// The load-bearing crux is the IMPLICIT-CHECK-FIRST ordering: an implicit conversion (e.g.
// `int` -> `long` numeric widening, `int` -> `object` boxing, `string` -> `object` reference
// widening, `null` -> reference-type null-literal, `int` -> `Nullable<int>` lifted identity) is
// returned even though the name says "ExplicitConversion" -- this is what distinguishes
// `ExplicitConversionNotUserDefined` from `ExplicitConversionImpl` (which would return the
// explicit conversion directly, e.g. `ExplicitNumericConversion` for `int` -> `long` or
// `ExplicitReferenceConversion` for `string` -> `object`).
//
// Pure given a compilation (delegates entirely to the already-ported `StandardImplicitConversion`
// and `ExplicitConversionImpl`, both of which take `const ICompilation&`), so it lands as a
// `Detail::` free function with the same signature: `const ICompilation&` + `IType&` non-const (the
// helpers take non-const `IType&` for the non-const `AcceptVisitor`, D406). Returns
// `std::shared_ptr<Conversion>` (the C# `Conversion` reference modeled as a shared handle).
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ExplicitConversionNotUserDefined(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                 ILSpy::Decompiler::TypeSystem::IType& fromType,
                                 ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool IsEncompassedBy(IType a, IType b)` (CSharpConversions.cs line 960, C# spec draft-v11
// section 10.5.4 "user-defined implicit conversions" -- the encompassment helper) -- true iff type `a`
// is encompassed by type `b`, i.e. there is a standard implicit conversion from `a` to `b`. This is
// the `FindMostEncompassedType` / `FindMostEncompassingType` primitive (the most-encompassed type
// is the one every other candidate converts to; the most-encompassing type is the one that converts
// to every other candidate). Delegates to `StandardImplicitConversion(a, b).IsValid` -- a `None`
// return (the `InvalidConversion` singleton, `IsValid` false) means `a` is NOT encompassed by `b`;
// any other return (a `BuiltinConversion` / `NumericOrEnumerationConversion` singleton, `IsValid`
// inherited true) means `a` IS encompassed by `b`.
//
// Pure like the other dispatch helpers (no `CSharpConversions` instance state -- delegates entirely
// to the already-ported `StandardImplicitConversion` D523 which threads the compilation), so it lands
// as a `Detail::` free function taking `const ICompilation&` + `IType&` non-const (the same signature
// `StandardImplicitConversion` takes, the D523 precedent). Returns `bool` -- the `.IsValid` fold of the
// `shared_ptr<Conversion>` return.
bool IsEncompassedBy(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                     ILSpy::Decompiler::TypeSystem::IType& a,
                     ILSpy::Decompiler::TypeSystem::IType& b);

// The C# `bool IsEncompassingOrEncompassedBy(IType a, IType b)` (CSharpConversions.cs line 965) --
// true iff type `a` encompasses or is encompassed by type `b`, i.e. there is a standard implicit
// conversion in EITHER direction (`a` -> `b` OR `b` -> `a`). Used by `UserDefinedExplicitConversion`
// to filter the applicable operators whose source type encompasses or is encompassed by the from-type.
// Delegates to `StandardImplicitConversion(a, b).IsValid || StandardImplicitConversion(b, a).IsValid`.
//
// Pure like `IsEncompassedBy` (delegates entirely to `StandardImplicitConversion` D523), so it lands
// as a `Detail::` free function with the same signature. Takes `const ICompilation&` + `IType&`
// non-const (the `StandardImplicitConversion` signature). Returns `bool`.
bool IsEncompassingOrEncompassedBy(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                  ILSpy::Decompiler::TypeSystem::IType& a,
                                  ILSpy::Decompiler::TypeSystem::IType& b);

// The C# `IType FindMostEncompassedType(IEnumerable<IType> candidates)` (CSharpConversions.cs line
// 971, C# spec draft-v11 section 10.5.4 "most encompassed type") -- the most-encompassed type
// among the candidates, i.e. the one that is encompassed by every other candidate (a standard
// implicit conversion exists from it to every other candidate). Returns null (a null `ITypePtr`)
// when the candidates are empty OR ambiguous (no single candidate is encompassed by all the others
// -- two candidates that neither encompass nor are encompassed by each other make the set
// ambiguous).
//
// The algorithm: `best` tracks the running most-encompassed candidate. For each `current`: if
// `best` is null (first iteration) or `current` is encompassed by `best` (`current` is "smaller"
// than `best`), `best` becomes `current`; else if `best` is NOT encompassed by `current` (neither
// encompasses the other), the set is ambiguous -> return null; otherwise (`best` is encompassed by
// `current`, so `current` is "bigger") `best` stays. The result is the candidate every other
// candidate converts to (the "smallest" in the implicit-conversion partial order).
//
// Pure like `IsEncompassedBy` (delegates entirely to `IsEncompassedBy` D524 which threads the
// compilation), so it lands as a `Detail::` free function taking `const ICompilation&` +
// `const std::vector<ITypePtr>&` (the `IEnumerable<IType>` port -- the ILSpy collection convention,
// `std::vector<ITypePtr>` as used by `DirectBaseTypes` / `TypeArguments`). Returns `ITypePtr`
// (nullable -- a null `shared_ptr<IType>` means empty/ambiguous, faithfully matching the C# null `IType`
// return).
std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType>
FindMostEncompassedType(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                        const std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType>>& candidates);

// The C# `IType FindMostEncompassingType(IEnumerable<IType> candidates)` (CSharpConversions.cs line
// 982, C# spec draft-v11 section 10.5.4 "most encompassing type") -- the most-encompassing type
// among the candidates, i.e. the one that encompasses every other candidate (a standard implicit
// conversion exists from every other candidate to it). Returns null when the candidates are empty
// OR ambiguous.
//
// The algorithm mirrors `FindMostEncompassedType` with the direction swapped: `best` tracks the
// running most-encompassing candidate. For each `current`: if `best` is null or `best` is
// encompassed by `current` (`current` is "bigger"), `best` becomes `current`; else if `current` is
// NOT encompassed by `best` (neither encompasses the other), the set is ambiguous -> return null;
// otherwise `best` stays. The result is the candidate every other candidate converts from (the
// "biggest" in the implicit-conversion partial order).
//
// Pure like `FindMostEncompassedType`, so it lands as a `Detail::` free function with the same
// signature. Returns `ITypePtr` (nullable -- empty/ambiguous).
std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType>
FindMostEncompassingType(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                         const std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType>>& candidates);

// The C# private nested `class OperatorInfo` (CSharpConversions.cs lines 1131-1148) -- a pure data
// holder for the user-defined-conversion resolution. Each entry captures one applicable conversion
// operator: the `IMethod Method` handle (non-owning; the method is owned by the type system, the
// `Conversions::UserDefinedConversion` `const IMethod*` convention), the `SourceType` / `TargetType`
// (the operator's parameter type / return type, possibly lifted to `Nullable<T>`), and the `IsLifted`
// flag (true when the operator applies in its lifted form -- a non-nullable value-type operator
// lifted so its source/target are `Nullable<T>`). Built by `GetApplicableConversionOperators`
// (deferred -- needs `IType.GetMethods` + the operator filter) and consumed by `SelectOperator`.
//
// The C# `readonly IType` reference fields port to `ITypePtr` (owning shared handles -- the D526/D527
// `FindMostEncompassedType`/`FindMostEncompassingType` `std::vector<ITypePtr>` convention); this keeps
// the stub types alive in tests where the `OperatorInfo` is constructed by hand from `shared_ptr` stubs.
// The `IMethod` ports to a non-owning `const IMethod*` raw pointer (the type system owns the method;
// the `Conversions::UserDefinedConversion` factory takes the same `const IMethod*`).
struct OperatorInfo {
    const ILSpy::Decompiler::TypeSystem::IMethod* Method;
    std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType> SourceType;
    std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType> TargetType;
    bool IsLifted;

    OperatorInfo(const ILSpy::Decompiler::TypeSystem::IMethod* method,
                 std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType> sourceType,
                 std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType> targetType,
                 bool isLifted)
        : Method(method),
          SourceType(std::move(sourceType)),
          TargetType(std::move(targetType)),
          IsLifted(isLifted) {}
};

// The C# `Conversion SelectOperator(IType mostSpecificSource, IType mostSpecificTarget,
// IList<OperatorInfo> operators, bool isImplicit, IType source, IType target)`
// (CSharpConversions.cs line 993) -- the user-defined-conversion operator selection. From the
// applicable operators (the `operators` list built by `GetApplicableConversionOperators`), selects
// the ones whose `SourceType` equals `mostSpecificSource` AND whose `TargetType` equals
// `mostSpecificTarget` (the most-specific source/target the user-defined-conversion resolution
// computed via `FindMostEncompassedType`/`FindMostEncompassingType`). If none match, returns `None`.
// If exactly one matches, returns a `UserDefinedConversion` over that operator. If more than one
// matches and exactly one is non-lifted, returns the non-lifted one (a non-lifted operator is
// preferred over the lifted forms). Otherwise (ambiguous -- multiple matches with zero or
// more-than-one non-lifted), returns a `UserDefinedConversion` over `selected[0]` flagged
// `isAmbiguous`.
//
// The `conversionBeforeUserDefinedOperator` / `conversionAfterUserDefinedOperator` the
// `UserDefinedConversion` carries are the standard (implicit-or-explicit) conversions before and
// after the user-defined operator: `ExplicitConversionNotUserDefined(source, mostSpecificSource)`
// (the conversion from the original source to the operator's source) and
// `ExplicitConversionNotUserDefined(mostSpecificTarget, target)` (the conversion from the operator's
// target to the original target). Both come from the already-ported
// `Detail::ExplicitConversionNotUserDefined` (D525), so `SelectOperator` threads the compilation
// through to it.
//
// Pure given a compilation (delegates to `IType::Equals` and `Detail::ExplicitConversionNotUserDefined`,
// both of which read no `CSharpConversions` instance state beyond the compilation), so it lands as a
// `Detail::` free function. Takes `const ICompilation&` (threaded to `ExplicitConversionNotUserDefined`)
// and `IType&` non-const (the `mostSpecificSource`/`mostSpecificTarget`/`source`/`target` feed
// `ExplicitConversionNotUserDefined`'s non-const `IType&` parameters, the non-const `AcceptVisitor`,
// D406). The C# `IList<OperatorInfo>` ports to `const std::vector<OperatorInfo>&` (the ILSpy
// collection convention). Returns `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
SelectOperator(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
               ILSpy::Decompiler::TypeSystem::IType& mostSpecificSource,
               ILSpy::Decompiler::TypeSystem::IType& mostSpecificTarget,
               const std::vector<OperatorInfo>& operators,
               bool isImplicit,
               ILSpy::Decompiler::TypeSystem::IType& source,
               ILSpy::Decompiler::TypeSystem::IType& target);

// The C# `static IType UnderlyingTypeForConversion(IType type)` (CSharpConversions.cs line 1164)
// -- the type to use for looking up user-defined conversion operators: if `type` is a
// `ByReferenceType` (a `ref` parameter/local), unwrap to the element type first; then strip the
// `Nullable<T>` wrapper (`NullableType.GetUnderlyingType`). The result is the type whose method
// table `GetApplicableConversionOperators` scans for `op_Implicit` / `op_Explicit` operators --
// a `ref` parameter does not carry its own operators, and a `Nullable<T>` does not define its own
// conversion operators (the underlying `T` does).
//
// Pure (a `static` method -- reads no `CSharpConversions` instance state, only `IType.Kind`, the
// `ByReferenceType` element, and `NullableType.GetUnderlyingType`), so it lands as a `Detail::`
// free function taking `const IType&` (the const `Kind()` / const `GetUnderlyingType` overload --
// the consumer `GetApplicableConversionOperators` calls the const `GetMethods` on the result).
// Returns `const IType&` -- the returned reference is valid for the lifetime of the input `type`:
// the `ByReferenceType` element is owned by the `ByReferenceType`'s `element_` (reachable through
// `type`), and the `GetUnderlyingType` return (either the `Nullable<T>` type argument owned by the
// `ParameterizedType`'s `typeArgs_`, or the original `type` itself) is likewise reachable through
// the input `type`. A degenerate `ByReferenceType` with a null element falls through to
// `GetUnderlyingType(type)` as the safe faithful fallback (the D516 null-guard-before-deref
// precedent -- the C# would deref the null element and NRE, but a null element does not occur in
// practice and the guard avoids UB).
const ILSpy::Decompiler::TypeSystem::IType&
UnderlyingTypeForConversion(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `List<OperatorInfo> GetApplicableConversionOperators(ResolveResult fromResult, IType
// fromType, IType toType, bool isExplicit)` (CSharpConversions.cs line 1167) -- the heavy helper
// that builds the list of applicable user-defined conversion operators. Scans the method tables
// of `UnderlyingTypeForConversion(fromType)` and `UnderlyingTypeForConversion(toType)` for the
// static single-parameter conversion operators (op_Implicit for implicit, op_Implicit OR
// op_Explicit for explicit) -- the C# `.Concat(...).Distinct()` ports to a concat-then-dedup by
// `IMethod` pointer identity (the C# default equality comparer for the reference type `IMethod`).
// For each candidate operator, computes its `sourceType` (the parameter type, with a `ref In`
// parameter unwrapped to its element when the from-side is not itself by-ref) and `targetType`
// (the return type), then determines applicability:
//   - explicit: `(IsEncompassingOrEncompassedBy(fromType, sourceType) ||
//     ImplicitConstantExpressionConversion(fromResult, sourceType)) &&
//     IsEncompassingOrEncompassedBy(targetType, toType)`;
//   - implicit: `(IsEncompassedBy(fromType, sourceType) ||
//     ImplicitConstantExpressionConversion(fromResult, sourceType)) &&
//     IsEncompassedBy(targetType, toType)`.
// A non-nullable value-type operator additionally gets a LIFTED form (`Nullable<T>` source and --
// when the target is also a non-nullable value type -- `Nullable<T>` target), checked by the
// same encompassment pair.
//
// Reads the `CSharpConversions` instance `compilation` (for `NullableType.Create` on the lifted
// forms), so the port threads `const ICompilation&` (the D517 reference-cluster convention). The
// `fromType` / `toType` feed `IsEncompassedBy` / `IsEncompassingOrEncompassedBy` which take
// `IType&` non-const, so they are `IType&` (non-const); the operator's `sourceType` / `targetType`
// come from the const `IParameter::Type()` / `IMember::ReturnType()` accessors, so they are held
// as `const IType*` and `const_cast` to `IType&` when feeding the non-const encompassment helpers
// (the underlying type-system objects are mutable -- the const is the accessor contract -- the
// D515 const-overload-pair precedent). The `OperatorInfo`'s `SourceType` / `TargetType` are
// owning `ITypePtr` handles, so the port obtains them from the const `IType&` via
// `IType::shared_from_this()` + `std::const_pointer_cast` (the D529 `NullableType.Create`
// precedent); the lifted forms are already owning `ITypePtr` from `NullableType.Create`.
std::vector<OperatorInfo>
GetApplicableConversionOperators(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                                 const ILSpy::Decompiler::Semantics::ResolveResult* fromResult,
                                 ILSpy::Decompiler::TypeSystem::IType& fromType,
                                 ILSpy::Decompiler::TypeSystem::IType& toType,
                                 bool isExplicit);

// The C# `Conversion UserDefinedImplicitConversion(ResolveResult fromResult, IType fromType,
// IType toType)` (CSharpConversions.cs line 1030, C# spec draft-v11 section 10.5.4 "user-defined
// implicit conversions") -- the user-defined implicit conversion resolution. User-defined
// conversions are not supported with interfaces (the `Kind == Interface` guard on both sides).
// Scans the applicable operators (`GetApplicableConversionOperators(..., isExplicit: false)`),
// then reduces them to the most-specific source/target (`FindMostEncompassedType` /
// `FindMostEncompassingType`) and selects the operator (`SelectOperator`). When the selected
// operator is lifted and the target is `Nullable<T>`, prefers the `A -> B -> B?` path over
// `A -> A? -> B?` by recursing on the underlying target. When no operator matches and the
// target is `Nullable<T>`, recurses on the underlying target. Returns `None` when no applicable
// operator resolves.
//
// The C# `fromResult` is a nullable reference -- the public `ImplicitConversion(IType, IType)`
// entry (no `ResolveResult` context) passes `null`, and the `A? -> A -> B` recursion in
// `UserDefinedExplicitConversion` passes `null` too -- so the port takes `const ResolveResult*`
// (nullable pointer) faithfully. The null `fromResult` flows only to
// `GetApplicableConversionOperators` (which guards its `ImplicitConstantExpressionConversion`
// call); `UserDefinedImplicitConversion` itself never dereferences `fromResult` directly, so
// no additional null guard is needed here.
//
// Delegates to the already-ported `GetApplicableConversionOperators` (D529) /
// `FindMostEncompassedType` (D526) / `FindMostEncompassingType` (D526) / `SelectOperator`
// (D525) / `NullableType.IsNullable` + `GetUnderlyingType` (D515), all of which take
// `const ICompilation&`, so it lands as a `Detail::` free function with the same compilation
// parameter. Takes `IType&` (non-const) because `SelectOperator` takes `IType&` (the non-const
// `AcceptVisitor`, D406) and `GetUnderlyingType`'s non-const overload returns `IType&` for the
// recursive calls. Returns `std::shared_ptr<Conversion>` -- the selected `UserDefinedConversion`
// (a fresh per-call instance) or `Conversions::None()`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
UserDefinedImplicitConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                             const ILSpy::Decompiler::Semantics::ResolveResult* fromResult,
                             ILSpy::Decompiler::TypeSystem::IType& fromType,
                             ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `Conversion UserDefinedExplicitConversion(ResolveResult fromResult, IType fromType,
// IType toType)` (CSharpConversions.cs line 1079, C# spec draft-v11 section 10.5.5 "user-defined
// explicit conversions") -- the user-defined explicit conversion resolution. Mirrors
// `UserDefinedImplicitConversion` (the interface guard, the operator scan, the most-specific
// reduction, the lifted-nullable preference) with two divergences: (1) the most-specific SOURCE
// reduction's first else-arm filters the operators whose source type encompasses or is
// encompassed by the from-type (`IsEncompassedBy(fromType, op.SourceType)` or the
// constant-expression fallback `ImplicitConstantExpressionConversion(fromResult,
// GetUnderlyingType(op.SourceType))`), then `FindMostEncompassedType` over the filtered set;
// when no operator's source encompasses the from-type, falls back to `FindMostEncompassingType`
// over ALL operators' sources; (2) the most-specific TARGET reduction's middle arm filters the
// operators whose target type is encompassed by the to-type (`IsEncompassedBy(op.TargetType,
// toType)`), then `FindMostEncompassingType` over the filtered set. The `A? -> A -> B`
// recursion (no operator matches, `fromType` is `Nullable<T>`) recurses with a `null` fromResult
// on the underlying from-type.
//
// The C# `fromResult` is a nullable reference (see `UserDefinedImplicitConversion`), so the
// port takes `const ResolveResult*` (nullable pointer). Unlike `UserDefinedImplicitConversion`,
// this helper calls `ImplicitConstantExpressionConversion(*fromResult, ...)` DIRECTLY (in the
// source-encompassing filter), so the port guards that call with `fromResult != nullptr` -- a
// null `fromResult` makes the constant-expression fallback false (the safe faithful port of the
// C# `||` short-circuit: the C# would NRE on `ImplicitConstantExpressionConversion(null, ...)`
// if reached, but the `||` short-circuits whenever `IsEncompassedBy` is true, which is the only
// path the null-fromResult recursions reach in practice; the guard avoids UB by returning
// false).
//
// Delegates to `GetApplicableConversionOperators` (D529) / `FindMostEncompassedType` (D526) /
// `FindMostEncompassingType` (D526) / `SelectOperator` (D525) / `IsEncompassedBy` (D524) /
// `ImplicitConstantExpressionConversion` (D521) / `NullableType` (D515) and recurses on itself
// and `UserDefinedImplicitConversion` (the lifted-nullable preference). Takes `IType&`
// (non-const) for the same reasons as `UserDefinedImplicitConversion`. Returns
// `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
UserDefinedExplicitConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                             const ILSpy::Decompiler::Semantics::ResolveResult* fromResult,
                             ILSpy::Decompiler::TypeSystem::IType& fromType,
                             ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool IsDelegateCompatible(IMethod m, IMethod d, bool isExtensionMethodInvocation)`
// (CSharpConversions.cs line 1457, C# spec draft-v11 section 21.4 "delegate compatibility") --
// whether the method `m` is compatible with the delegate whose invoke method is `d`. The
// private 3-arg overload the public `IsDelegateCompatible(IMethod, IType)` (which resolves the
// delegate's invoke method via `GetDelegateInvokeMethod`, the TypeSystemExtensions free function
// D533) and the `MethodGroupConversion` helper both call. Tests a method against a delegate invoke method:
// the parameter count must match (skipping `m`'s first parameter when `isExtensionMethodInvocation`
// -- the `this` the extension syntax supplies), each corresponding parameter's `ReferenceKind` must
// match, a ref/out/in parameter must have an identity conversion on the types (Roslyn relaxes the
// spec's same-type requirement to identity), a by-value parameter must have an identity OR implicit
// reference conversion from `d`'s parameter type to `m`'s, the `ReturnTypeIsRefReadOnly` flags must
// match, and the return type must have an identity OR implicit reference conversion from `m`'s to
// `d`'s. Returns `bool` (the C# `bool`, not a `Conversion`).
//
// Pure given a compilation (delegates only to the already-ported `IdentityConversion` D514 and
// `IsImplicitReferenceConversion` D517, both of which take `const ICompilation&`; reads no
// `CSharpConversions` instance state), so it lands as a `Detail::` free function (the D508
// precedent). Takes `const IMethod&` for `m` and `d` (every `IMethod`/`IParameter` member it reads
// -- `Parameters` / `ReturnType` / `ReturnTypeIsRefReadOnly` / `ReferenceKind` / `Type` -- is `const`).
// The parameter/return `Type()` accessors return `const IType&`, but `IdentityConversion` and
// `IsImplicitReferenceConversion` take `IType&` non-const (the non-const `AcceptVisitor`, D406), so
// the port `const_cast`s the const references -- the underlying type-system objects are mutable
// (the accessor's `const` is the contract, not a guarantee), the D515/D517 `const_cast` precedent.
// The C# `throw new ArgumentNullException` for a null `m`/`d` is N/A: C++ references are non-null by
// contract. This is a tested-but-not-yet-wired foundation (the D63/D66/D68/D70/D74 precedent): no
// `CSharpConversions` caller invokes it yet (the public `IsDelegateCompatible(IMethod, IType)`
// and `MethodGroupConversion` are still deferred), so it is dead in the CLI call graph.
bool IsDelegateCompatible(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                           const ILSpy::Decompiler::TypeSystem::IMethod& m,
                           const ILSpy::Decompiler::TypeSystem::IMethod& d,
                           bool isExtensionMethodInvocation);

// The C# `private Conversion ImplicitConversion(IType fromType, IType toType, bool allowUserDefined,
// bool allowTuple)` (CSharpConversions.cs line 166, C# spec draft-v11 section 10.2 "implicit
// conversions") -- the private IType-based implicit-conversion dispatch. The standard implicit
// conversion first (`StandardImplicitConversion(fromType, toType, allowTuple)`), then -- only when
// no standard implicit conversion exists (`c == Conversion.None`) AND `allowUserDefined` is true --
// the user-defined implicit conversion (`UserDefinedImplicitConversion(null, fromType, toType)`).
// The public `ImplicitConversion(IType, IType)` entry (the cached overload) calls this with
// `allowUserDefined: true, allowTuple: true`; the public `ExplicitConversion(IType, IType)` calls
// this with `allowUserDefined: false, allowTuple: false` (the implicit check before the explicit
// dispatch). `ExplicitConversionNotUserDefined` (D525) inlines this same logic with
// `allowUserDefined: false` (the standard implicit then `ExplicitConversionImpl`).
//
// The `allowTuple` parameter threads to `StandardImplicitConversion`'s tuple arm, which is
// deferred (yields `None` for tuple shapes until the `TupleConversion` machinery lands), so it is
// effectively ignored for the ported arms -- the faithful port does not thread it to the
// already-ported `Detail::StandardImplicitConversion` (D523, which has no `allowTuple` parameter;
// the tuple arm is deferred inside it). Pure given a compilation (delegates entirely to the
// already-ported `StandardImplicitConversion` D523 + `UserDefinedImplicitConversion` D530, both
// of which take `const ICompilation&`), so it lands as a `Detail::` free function with the same
// compilation parameter. Takes `IType&` non-const (the helpers take non-const `IType&` for the
// non-const `AcceptVisitor`, D406). Returns `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ImplicitConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                   ILSpy::Decompiler::TypeSystem::IType& fromType,
                   ILSpy::Decompiler::TypeSystem::IType& toType,
                   bool allowUserDefined,
                   bool allowTuple);

// The C# `private Conversion ImplicitConversion(ResolveResult resolveResult, IType toType,
// bool allowUserDefined, bool allowTuple)` (CSharpConversions.cs line 101, C# spec draft-v11
// section 10.2 "implicit conversions") -- the private ResolveResult-based implicit-conversion
// dispatch, the core the public `ImplicitConversion(ResolveResult, IType)` entry calls with
// `allowUserDefined: true, allowTuple: true` and the public `ExplicitConversion(ResolveResult,
// IType)` calls with `allowUserDefined: false, allowTuple: false` (the implicit check before the
// explicit dispatch). The dispatch checks the already-ported helpers in spec order: the
// compile-time-constant arms (`ImplicitEnumerationConversion` D527, then
// `ImplicitConstantExpressionConversion` D521), the interpolated-string arm (an RTTI check on
// `InterpolatedStringResolveResult` plus `IsKnownType(IFormattable/FormattableString)`), the
// dynamic arm (`resolveResult.Type.Kind == TypeKind.Dynamic`), the anonymous-function arm
// (`AnonymousFunctionConversion`, an RTTI check on `LambdaResolveResult` -- the dispatch owns the
// RTTI, the helper owns the body), the (deferred) method-group arm, the compile-time-constant
// fallback (`StandardImplicitConversion` D523 + `UserDefinedImplicitConversion` D530), and the
// non-constant fallback (the deferred tuple arm, the `ThrowResolveResult` arm, then the IType-
// based `ImplicitConversion` D531).
//
// The still-deferred arms (`MethodGroupConversion`, `TupleConversion`) yield
// `Conversions::None()` until their machinery (`MethodGroupResolveResult.PerformOverloadResolution`;
// the `TupleConversion` tuple machinery) lands -- a non-matching `ResolveResult` falls through
// to the IType-based fallback exactly as the C# does when those arms return `None`.
//
// `resolveResult.Type()` returns `const IType&` (the D374 non-null-reference convention), but
// `StandardImplicitConversion` / `UserDefinedImplicitConversion` / the IType-based
// `ImplicitConversion` take `IType&` non-const (the non-const `AcceptVisitor`, D406), so the port
// `const_cast`s `resolveResult.Type()` to `IType&` -- the underlying type-system objects are
// mutable (the accessor's `const` is the contract, not a guarantee), the D515 const-overload-pair
// / D517 `const_cast` precedent. Takes `const ResolveResult&` (every `ResolveResult` member it
// reads -- `IsCompileTimeConstant` / `Type` -- is `const`) and `IType& toType` non-const (the
// fallback helpers take non-const `IType&`). Threads `const ICompilation&` (the D523/D530/D531
// convention). Returns `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
ImplicitConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                   const ILSpy::Decompiler::Semantics::ResolveResult& resolveResult,
                   ILSpy::Decompiler::TypeSystem::IType& toType,
                   bool allowUserDefined,
                   bool allowTuple);

// The C# `static IType UnpackExpressionTreeType(IType type)` (CSharpConversions.cs line 1348) --
// the helper the anonymous-function conversion strips the `Expression<T>` wrapper with: a
// `ParameterizedType` over the `System.Linq.Expressions.Expression`1` generic definition (arity
// 1) unpacks to its single type argument; any other type passes through unchanged. The C# is a
// `static` method (no `CSharpConversions` instance state), so it lands as a `Detail::` free
// function taking `const IType&` and returning `const IType&` (both the unpacked type argument --
// owned by the `ParameterizedType`'s `typeArgs_` reachable through the input `type`, outliving the
// call -- and the passthrough -- the input `type` itself, owned by the caller -- outlive the call;
// the D516/D529 non-owning-reference-return precedent).
//
// The C# `pt.Name == "Expression"` reads the generic definition's `Name` (the `ParameterizedType`
// delegates `Name` to `genericType.Name`); the C# `pt.Namespace == "System.Linq.Expressions"`
// reads the generic definition's `Namespace`. The port's `IType` interface does NOT carry
// `Namespace()` (only `Name()` / `ReflectionName()` / `TypeParameterCount()`); `Namespace()` lives
// on `ITypeDefinition` (via `IEntity` -> `INamedElement`). The faithful port reads the namespace
// via `pt->GetDefinition()` (the `ParameterizedType::GetDefinition()` delegates to
// `genericType->GetDefinition()`, which for the real `Expression`1` definition is the definition
// itself), with a `nullptr` guard -- a `ParameterizedType` over a non-definition generic (a
// degenerate shape that does not occur for `Expression<T>`) yields `nullptr`, so the namespace
// check fails and the type passes through, faithfully matching the C# (the generic's `Namespace`
// would be empty for a non-definition, not `"System.Linq.Expressions"`). `pt->Name()` is available
// on the `IType` interface directly (`ParameterizedType::Name()` delegates to `genericType->Name()`).
const ILSpy::Decompiler::TypeSystem::IType&
UnpackExpressionTreeType(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `Conversion AnonymousFunctionConversion(ResolveResult resolveResult, IType toType)`
// (CSharpConversions.cs line 1280, C# 9.0 spec section 10.7 "anonymous function conversions") --
// the anonymous-function (lambda / anonymous-method) -> delegate-type conversion. Resolves the
// delegate's `Invoke` method (`GetDelegateInvokeMethod` D533), builds the delegate's parameter
// types / return type, checks the parameter-list compatibility (the `HasParameterList` /
// `IsImplicitlyTyped` / explicit-typed `ReferenceKind` + identity guards, or the no-parameter-list
// `out` rejection), then delegates the body-validity verdict to `LambdaResolveResult.IsValid`
// (the abstract `IsValid` the lambda subclass implements). The C# method is private; the port
// lifts it to a `Detail::` free function (the D508 precedent).
//
// The C# takes `ResolveResult` and does the `resolveResult as LambdaResolveResult` + null check
// internally; the port takes the already-verified `const LambdaResolveResult& f` (the dispatch
// does the `dynamic_cast` and only calls this helper for an actual lambda -- the D528 interpolated-
// string / throw-arm precedent, where the dispatch owns the RTTI and the helper owns the body;
// the C# `as` + null check is a private method called once, so moving its RTTI to the single
// caller is a faithful reorganization). The `!f.IsAnonymousMethod` expression-tree unwrap
// (`UnpackExpressionTreeType`) runs only for lambdas (C# 3.0+); anonymous methods (C# 2.0
// `delegate { }`) cannot convert to expression trees, so the toType is left as-is for them.
//
// The `CSharpConversions& conversions` parameter threads the conversion controller to
// `f.IsValid(...)` (the C# passes `this`; the port's `IsValid` signature takes `CSharpConversions&`,
// the D473 abstract-base port). The delegate's parameter/return `Type()` accessors return `const
// IType&`, but `IsValid` takes `const std::vector<ITypePtr>&` / `const ITypePtr&`, so the port
// builds owning `ITypePtr` handles from the const references via `shared_from_this()` +
// `const_pointer_cast` (the D529 precedent -- the underlying type-system objects are shared-
// managed; the `const` is the accessor contract, not a guarantee). The explicit-typed-parameter
// identity check `IdentityConversion(dParamTypes[i], pF.Type)` takes `IType&` non-const (the
// non-const `AcceptVisitor`, D406), so the `pF->Type()` `const IType&` is `const_cast` to `IType&`
// (the D515/D517 precedent). Returns `std::shared_ptr<Conversion>` (the `LambdaConversion` the
// `IsValid` returns on success, or `Conversions::None()` on any guard failure).
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
AnonymousFunctionConversion(ILSpy::Decompiler::CSharp::Resolver::CSharpConversions& conversions,
                            const ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult& f,
                            const ILSpy::Decompiler::TypeSystem::IType& toType);

// The synthetic-arguments construction for the method-group conversion (CSharpConversions.cs
// line 1362, the local `args` construction inside `MethodGroupConversion`). The method-group
// conversion resolves the delegate's `Invoke` method (`GetDelegateInvokeMethod` D533) and then
// builds one synthetic `ResolveResult` per `Invoke` parameter -- the arguments fed to
// `MethodGroupResolveResult.PerformOverloadResolution` (deferred -- needs the `OverloadResolution`
// engine) to select the matching method in the group. Each parameter maps to:
//   * a `ByReferenceResolveResult(elementType, param.ReferenceKind)` when the parameter is a
//     ref/out/in parameter (`ReferenceKind != None`) AND its type is a `ByReferenceType` -- the
//     element type is unwrapped (`((ByReferenceType)parameterType).ElementType`) so the overload
//     resolver sees the underlying ref-able type; the `ByReferenceResolveResult` ctor builds the
//     base `ResolveResult` from a fresh `ByReferenceType(elementType)` (faithful to the C#
//     `internal ByReferenceResolveResult(IType, ReferenceKind)` ctor).
//   * a plain `ResolveResult(compilation.FindType(KnownTypeCode.Object))` when the parameter's
//     type is `dynamic` (the dynamic-erasure arm -- `dynamic` erases to `object` for the method
//     group lookup; the C# `param.Type.Kind == TypeKind.Dynamic` check).
//   * a plain `ResolveResult(parameterType)` otherwise.
//
// Pure given a compilation (the only `CSharpConversions` instance state the body reads is
// `compilation` -- for `FindType(Object)` in the dynamic arm; the C# `MethodGroupConversion` is
// an instance method on `CSharpConversions` reading `this.compilation`), so it lands as a
// `Detail::` free function taking `const ICompilation&` + `const IMethod& invoke` (every
// `IMethod`/`IParameter` member the body reads -- `Parameters`, each parameter's `Type` /
// `ReferenceKind` -- is `const`). A tested-but-not-yet-wired foundation ahead of the
// `MethodGroupConversion` body (which needs `PerformOverloadResolution`); `MethodGroupConversion`
// will call this helper then feed the result to the (deferred) overload-resolution engine.
//
// The owning `std::shared_ptr<ResolveResult>` handles the returned vector carries model the C#
// `ResolveResult[]` (the C# GC-shared array; the port's shared handles keep the constructed
// `ByReferenceResolveResult` / `ResolveResult` instances alive). The `ByReferenceType`'s element
// is obtained directly via `Element()` (the `ByReferenceType` already owns its element as an
// `ITypePtr`); the plain/dynamic-`object` types are obtained from the `const IType&` accessors
// via `shared_from_this()` + `const_pointer_cast` (the D529 precedent -- the type-system objects
// are shared-managed; the `const` is the accessor contract, not a guarantee).
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
MethodGroupConversionArguments(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                               const ILSpy::Decompiler::TypeSystem::IMethod& invoke);

// The C# `Conversion TupleConversion(TupleResolveResult fromRR, IType toType, bool isExplicit)`
// (CSharpConversions.cs line 1480, C# 9.0 spec sections 10.2.13 + 10.3.6) -- the tuple-literal
// (a `TupleResolveResult`) -> tuple-type conversion. Flattens the source via `fromRR.Elements()`
// (the per-element `ResolveResult`s) and the target via `TupleType.GetTupleElementTypes(toType)`
// (the now-ported D539 prerequisite); if the target is not a tuple (`IsDefault`) or the element
// counts differ, returns `None`. Otherwise converts each element pair via the public
// `ImplicitConversion(IType, IType)` (isExplicit false) or `ExplicitConversion(IType, IType)`
// (isExplicit true) -- the C# `this.ImplicitConversion` / `this.ExplicitConversion` -- and returns
// `Conversion.TupleConversion(elementConversions)` only when every element conversion is valid;
// any invalid element conversion short-circuits to `None`.
//
// The C# is a private instance method reading `this.compilation` (threaded via the per-element
// `ImplicitConversion` / `ExplicitConversion` calls), so the port threads `const ICompilation&`.
// The per-element dispatch uses the UNCACHED `Detail::` free-function equivalents (`ImplicitConversion`,
// `ExplicitConversionImpl`, `UserDefinedExplicitConversion`) rather than the cached
// `CSharpConversions::Get(compilation).ImplicitConversion(...)` public method -- the cache's
// `TypePair` keys are non-owning `const IType*` that would dangle across test-local types (the
// `CSharpConversions.hpp` `TypePair` convention), and the uncached dispatch is functionally
// identical for the result (the D531 `BetterConversionTarget` precedent). `fromRR.Elements()`
// yields `shared_ptr<ResolveResult>` whose `Type()` returns `const IType&`, but the per-element
// dispatch takes `IType&` non-const (the non-const `AcceptVisitor`, D406), so the port `const_cast`s
// the const reference -- the underlying type-system objects are mutable (the accessor's `const` is
// the contract), the D515/D517/D528 `const_cast` precedent. `toType` is `IType&` non-const for the
// same reason. Returns `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
TupleConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                const ILSpy::Decompiler::Semantics::TupleResolveResult& fromRR,
                ILSpy::Decompiler::TypeSystem::IType& toType,
                bool isExplicit);

// The C# `Conversion TupleConversion(IType fromType, IType toType, bool isExplicit)`
// (CSharpConversions.cs line 1506, C# 9.0 spec sections 10.2.13 + 10.3.6) -- the tuple-type ->
// tuple-type conversion (the IType overload, consumed by `StandardImplicitConversion`'s tuple
// arm, `ExplicitConversionImpl`'s tail, and the public `ExplicitConversion(IType, IType)` via
// `ExplicitConversionImpl`). Flattens both sides via `TupleType.GetTupleElementTypes` (D539);
// if the source is not a tuple (`IsDefaultOrEmpty`) or the target is not a tuple (`IsDefault`) or
// the element counts differ, returns `None`. Otherwise converts each element pair via the public
// `ImplicitConversion` / `ExplicitConversion` (the C# `this.ImplicitConversion` /
// `this.ExplicitConversion`) and returns `Conversion.TupleConversion(elementConversions)` only
// when every element conversion is valid; any invalid element conversion short-circuits to
// `None`. Mirrors the `TupleResolveResult` overload with the source flattened via
// `GetTupleElementTypes(fromType)` instead of `fromRR.Elements()`.
//
// The C# `IsDefaultOrEmpty` / `IsDefault` checks port to `!has_value() || value.empty()` /
// `!has_value()` (the D539 `std::optional<std::vector<ITypePtr>>` convention). The per-element
// `GetTupleElementTypes` results are `ITypePtr` whose deref yields `IType&` non-const directly
// (no `const_cast` needed, unlike the `TupleResolveResult` overload whose `Elements()` yields
// `const IType&`). Threads `const ICompilation&` (the per-element conversion calls) and takes
// `IType&` non-const (the `GetTupleElementTypes` const-overload returns `const IType&`, but the
// per-element `ImplicitConversion` / `ExplicitConversion` take `IType&` non-const -- the element
// `ITypePtr` deref already yields non-const `IType&`). Returns `std::shared_ptr<Conversion>`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
TupleConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                ILSpy::Decompiler::TypeSystem::IType& fromType,
                ILSpy::Decompiler::TypeSystem::IType& toType,
                bool isExplicit);

// The C# `bool IsBetterIntegralType(TypeCode t1, TypeCode t2)` (CSharpConversions.cs line 1697,
// C# 9.0 spec section 12.6.4.7 "better conversion target" -- the integral-type tiebreak) -- true
// iff a signed integral type `t1` is a better conversion target than the unsigned integral type
// `t2`: `SByte` beats `Byte`/`UInt16`/`UInt32`/`UInt64`; `Int16` beats `UInt16`/`UInt32`/`UInt64`;
// `Int32` beats `UInt32`/`UInt64`; `Int64` beats `UInt64`. The C# spec rule: signed types are
// better than unsigned types when the smaller-range signed type's range fully overlaps the
// unsigned type's. A pure helper (reads only the two `TypeCode` values, no `CSharpConversions`
// instance state, no `IType`), so it lands as a `Detail::` free function. Consumed by
// `BetterConversionTarget`'s tail (the integral-type tiebreak after the implicit-convertibility
// and Task checks) and a tested-but-not-yet-wired foundation ahead of the full
// `BetterConversionTarget` / `BetterConversion` resolution.
bool IsBetterIntegralType(ILSpy::Decompiler::TypeSystem::TypeCode t1,
                          ILSpy::Decompiler::TypeSystem::TypeCode t2);

// The C# `static IType UnpackTask(IType type)` (CSharpConversions.cs line 1654) -- the
// `Task<T>` unpacker the `BetterConversionTarget` recursion uses: returns `type.TypeArguments[0]`
// when `type` is a generic task-like type (`TaskType.IsTask(type) || TaskType.IsCustomTask(type,
// out _)`) with exactly one type parameter, else a null `IType`. This is a STRICTER filter than
// `TaskType.UnpackTask` (which returns `void` for the non-generic `Task` and the type itself for a
// non-task): `CSharpConversions.UnpackTask` returns null for a non-generic task (0 type params)
// and for any non-task, so the `BetterConversionTarget` `s1 != null && s2 != null` recursion fires
// only when BOTH targets are `Task<T>`-shaped (a 1-type-param task-like), faithfully matching the
// C# `BetterConversionTarget` recursion.
//
// Pure given a type (delegates only to `TaskType.IsTask` / `TaskType.IsCustomTask` and reads
// `IType::TypeParameterCount` -- no `CSharpConversions` instance state), so it lands as a
// `Detail::` free function taking `const IType&` (like the pure numeric/identity helpers, not the
// reference/boxing helpers that take non-const `IType&` for the non-const `AcceptVisitor`).
// `TaskType.IsTask` / `TaskType.IsCustomTask` take `const IType&`; `TypeParameterCount()` is a
// const `IType` virtual; the `TypeArguments[0]` read is `ParameterizedType`-specific (not on the
// `IType` surface), so the port `dynamic_cast`s to `const ParameterizedType*` + guards before
// `GetTypeArgument(0)` (a 1-type-param task-like is always parameterized in practice --
// `IsTask`'s `TaskOfT` arm requires a `ParameterizedType`, and a custom task-like is built as a
// `ParameterizedType` over its generic definition; the guard is the defensive null-check
// convention, the D516 precedent). `GetTypeArgument(0)` returns a co-owning `ITypePtr` copy from
// the `ParameterizedType`'s `typeArgs_`, so the returned handle outlives the call (the
// `TaskType.UnpackTask` / `UnpackAnyTask` ownership precedent). The `TaskType.IsCustomTask` call
// discards the builder type via a local `ITypePtr` (the C# `out _`). Returns `ITypePtr` (nullable --
// a null `shared_ptr<IType>` for a non-task or a non-generic task).
std::shared_ptr<ILSpy::Decompiler::TypeSystem::IType>
UnpackTask(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `int BetterConversionTarget(IType t1, IType t2)` (CSharpConversions.cs line 1660, C# 9.0
// spec section 12.6.4.7 "better conversion target") -- which of two target types `t1` / `t2` is
// the better conversion target: `0` = neither is better, `1` = `t1` is better, `2` = `t2` is
// better. The C# `if (t1.IsKnownType(ReadOnlySpanOfT)) { ... } if (t2.IsKnownType(ReadOnlySpanOfT))
// { ... }` ReadOnlySpan/Span tiebreak arms (the C# 9 `ref struct` preference: a `ReadOnlySpan<T>`
// is preferred over a `Span<T>` when the element types match by identity, and a `ReadOnlySpan<T>`
// is preferred over another `ReadOnlySpan<U>` when `T` converts implicitly to `U` but not back);
// the core implicit-convertibility check (`ImplicitConversion(t1, t2).IsValid &&
// !ImplicitConversion(t2, t1).IsValid` -> `t1` is better, and the mirror); the `UnpackTask`
// recursion (when both targets are `Task<T>`, recurse on the inner types); and the integral-type
// tiebreak (`IsBetterIntegralType`).
//
// The `ReadOnlySpan`/`Span` tiebreak arms read `t1.TypeArguments[0]` / `t2.TypeArguments[0]` --
// the C# `IType.TypeArguments` is on the `IType` interface; the port's `TypeArguments()` is
// `ParameterizedType`-specific (not on the `IType` surface), so the port `dynamic_cast`s to
// `ParameterizedType` and guards before reading `TypeArguments()[0]` (a non-`ParameterizedType`
// with `IsKnownType(ReadOnlySpanOfT)` true does not occur in practice -- a `ReadOnlySpan<T>` is a
// parameterized type -- but the guard avoids UB on a degenerate stub, the D516 null-guard
// precedent). The element types feed `IdentityConversion` / `Detail::ImplicitConversion` which
// take `IType&` non-const (the non-const `AcceptVisitor`, D406); the `TypeArguments()[0]`
// `ITypePtr` dereferences to `IType&` (the shared `IType` is mutable).
//
// The `ImplicitConversion(t1, t2)` calls are the C# cached public `ImplicitConversion(IType,
// IType)`; the port uses `Detail::ImplicitConversion(*compilation, t1, t2, true, true)` (the
// uncached IType-based dispatch, D531 -- the cache is an instance-level optimization on
// `CSharpConversions`, not on the `Detail::` free function; the result is identical). The
// `UnpackTask` recursion (the `UnpackTask` helper above) recurses on the inner types when both
// targets are `Task<T>`-shaped; the `s1 != null && s2 != null` guard skips the recursion for
// non-`Task` targets (the faithful fallback for the common case). Threads `const ICompilation&`
// (the D523/D531 convention) and takes `IType&` non-const (the helpers take non-const `IType&`).
// Returns `int` (the `0`/`1`/`2` verdict).
int BetterConversionTarget(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                           ILSpy::Decompiler::TypeSystem::IType& t1,
                           ILSpy::Decompiler::TypeSystem::IType& t2);

// The C# `bool IsExactlyMatching(ResolveResult e, IType t)` (CSharpConversions.cs line 1615,
// C# 8.0 spec section 12.6.4.6 "exactly matching expression") -- whether the expression `e`
// exactly matches the type `t`. An expression exactly matches a type when there is an identity
// conversion from the expression's type to `t`; a lambda expression additionally exactly matches
// a delegate type when, after unpacking the `Expression<T>` wrapper (for non-anonymous-method
// lambdas) and resolving the delegate's `Invoke` method, the lambda's inferred return type
// exactly matches the delegate's return type (recursively, with the `Task<T>` wrapper unpacked
// for async lambdas).
//
// The C# body: `var s = e.Type; if (IdentityConversion(s, t)) return true; if (e is
// LambdaResolveResult lambda) { ... } else return false;`. The lambda arm unpacks the
// expression-tree wrapper (`UnpackExpressionTreeType` D534), resolves the delegate `Invoke`
// (`GetDelegateInvokeMethod` D533), builds the delegate's parameter types (fed to
// `GetInferredReturnType`), then checks `IdentityConversion(x, y)` (the inferred return vs the
// delegate return); for an async lambda, unpacks the `Task<T>` wrapper from both (`UnpackTask`
// D542) and recurses on the unpacked types (`IsExactlyMatching(new ResolveResult(x), y)`).
//
// Pure (every callee -- `IdentityConversion` D514, `UnpackExpressionTreeType` D534,
// `GetDelegateInvokeMethod` D533, `LambdaResolveResult::GetInferredReturnType`, `UnpackTask` D542 --
// is pure; reads no `CSharpConversions` instance state, no compilation), so it lands as a
// `Detail::` free function taking `const ResolveResult&` (every `ResolveResult` member it reads
// -- `Type()`, the `dynamic_cast` to `LambdaResolveResult`, `IsAnonymousMethod` / `IsAsync` /
// `GetInferredReturnType` -- is `const`) and `const IType&` (the first `IdentityConversion` call
// `const_cast`s both sides to `IType&` non-const -- the underlying type-system objects are
// mutable, the accessor's `const` is the contract, the D515/D517 `const_cast` precedent; the
// lambda arm's `GetDelegateInvokeMethod` / `UnpackExpressionTreeType` take `const IType&`). The
// C# `t = UnpackExpressionTreeType(t)` rebind ports to a `const IType*` pointer rebound through
// the unwrap (a C++ reference cannot be rebound). Recurses on itself. Returns `bool`.
bool IsExactlyMatching(const ILSpy::Decompiler::Semantics::ResolveResult& e,
                        const ILSpy::Decompiler::TypeSystem::IType& t);

// The C# `public bool IsConstraintConvertible(IType fromType, IType toType)`
// (CSharpConversions.cs line 261, C# spec section 8.4.5 "satisfying constraints") -- whether
// `fromType` is convertible to `toType` using one of the conversions allowed when satisfying type
// parameter constraints. The allowed conversions are a strict subset of the implicit conversions:
// identity, implicit reference, boxing (for a non-nullable from-type), the nullable-value-type-to-
// `object` special case (an `object` constraint still allows nullable value types -- `object`
// constraints don't exist in C# but are inserted by `DefaultResolvedTypeParameter.DirectBaseTypes`),
// and implicit type-parameter conversion. NOT allowed: numeric, nullable-lifted, pointer, constant-
// expression, user-defined, or tuple conversions.
//
// The C# body checks the already-ported helpers in order: `IdentityConversion(fromType, toType)`;
// `ImplicitReferenceConversion(fromType, toType, 0)` (the private recursive worker at depth 0, NOT
// the public `IsImplicitReferenceConversion` -- both produce the same result, the public delegates to
// the worker at depth 0); the nullable branch -- `NullableType.IsNullable(fromType)` gates: if
// nullable, `toType.IsKnownType(KnownTypeCode.Object)` (the nullable-to-object special case); else
// `IsBoxingConversion(fromType, toType)` (the boxing arm); then `ImplicitTypeParameterConversion(
// fromType, toType)`; else `false`.
//
// Pure given a compilation (delegates entirely to the already-ported `IdentityConversion` D514,
// `ImplicitReferenceConversion` D517, `NullableType.IsNullable` D515, `IsKnownType`,
// `IsBoxingConversion` D519, `ImplicitTypeParameterConversion` D519 -- all of which take
// `const ICompilation&` or are pure), so it lands as a `Detail::` free function taking
// `const ICompilation&` (the D517 reference-cluster convention). Takes `IType&` non-const (the
// callees `IdentityConversion` / `ImplicitReferenceConversion` / `IsBoxingConversion` /
// `ImplicitTypeParameterConversion` take non-const `IType&` for the non-const `AcceptVisitor`,
// D406; `NullableType.IsNullable` and `IsKnownType` take `const IType&` but a non-const `IType&`
// binds to a `const IType&` parameter trivially). The C# `throw new ArgumentNullException` on
// null `fromType`/`toType` compiles out (the `IType&` references cannot bind to null, the D374
// convention). Returns `bool`.
bool IsConstraintConvertible(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                            ILSpy::Decompiler::TypeSystem::IType& fromType,
                            ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `bool IsImplicitSpanConversion(IType fromType, IType toType)` (CSharpConversions.cs line
// 1238, the C# 14.0 first-class-span-types proposal) -- true iff an implicit span conversion
// exists from `fromType` to `toType`. The proposal permits conversions between single-dimensional
// arrays, `System.Span<T>`, `System.ReadOnlySpan<T>`, and `string`: a 1-D array converts to
// `Span<T>` by element identity, and to `ReadOnlySpan<T>` by element identity OR an implicit
// reference conversion (covariance); `Span<T>` / `ReadOnlySpan<T>` convert to `ReadOnlySpan<T>`
// by element identity OR an implicit reference conversion; `string` converts to
// `ReadOnlySpan<char>` by the fixed element-type match.
//
// The conversion is gated on `compilation.TypeSystemOptions.HasFlag(TypeSystemOptions.
// FirstClassSpanTypes)` -- it does not exist unless the type system materializes first-class
// spans. `TypeSystemOptions` (D376, with `FirstClassSpanTypes = 0x40000`) and the `ICompilation::
// TypeSystemOptions()` accessor are both ported, so the flag check lands now. The helper then
// delegates to the already-ported `IdentityConversion` (D514), `IsImplicitReferenceConversion`
// (D517), and `IsKnownType` (TypeSystemExtensions).
//
// The C# pattern-match arms read `toType.TypeArguments[0]` (the C# `IType.TypeArguments` is on
// the interface); the port's `TypeArguments()` is `ParameterizedType`-specific (not on the `IType`
// surface), so each access `dynamic_cast`s to `ParameterizedType*` + guards (an empty/missing
// argument yields a null pointer and the arm returns false -- faithful to the C#, where a real
// `Span<T>` / `ReadOnlySpan<T>` always carries the argument; the guard avoids UB on a degenerate
// stub, the D516 / BetterConversionTarget `firstTypeArg` precedent). The `ArrayType` arm reads
// `Dimensions: 1` (the rank) -- the port checks `arr->Rank() == 1` (`ArrayType::Rank`, the
// single-dimensional-array condition, faithful to the C# `Dimensions` property); `ElementType`
// is `arr->Element()` whose `shared_ptr` deref yields `IType&` non-const.
//
// Pure given a compilation (delegates only to the already-ported helpers; reads no
// `CSharpConversions` instance state beyond the compilation threaded to
// `IsImplicitReferenceConversion`), so it lands as a `Detail::` free function taking
// `const ICompilation&` (the D517 reference-cluster convention). Takes `IType&` non-const (the
// callees `IdentityConversion` / `IsImplicitReferenceConversion` take non-const `IType&` for the
// non-const `AcceptVisitor`, D406; `IsKnownType` takes `const IType&` but a non-const `IType&`
// binds to a `const IType&` parameter trivially). Returns `bool`.
bool IsImplicitSpanConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                               ILSpy::Decompiler::TypeSystem::IType& fromType,
                               ILSpy::Decompiler::TypeSystem::IType& toType);

// The C# `public int BetterConversion(IType s, IType t1, IType t2)` (CSharpConversions.cs line
// 1620, C# 4.0 spec section 7.5.3.4 "better conversion from type"; the current standard folds it
// into section 12.6.4.5-12.6.4.7) -- the "better conversion from type" dispatch: an identity
// conversion from the source `s` to a target beats a non-identity conversion; when neither (or
// both) is identity, the verdict falls to `BetterConversionTarget`. Returns `0` = neither is
// better, `1` = `t1` is better, `2` = `t2` is better.
//
// The C# `public` method body is `bool ident1 = IdentityConversion(s, t1); bool ident2 =
// IdentityConversion(s, t2); if (ident1 && !ident2) return 1; if (ident2 && !ident1) return 2;
// return BetterConversionTarget(t1, t2);`. The port lifts that body to this `Detail::` free
// function so the public `CSharpConversions::BetterConversion(IType, IType, IType)` method can
// delegate to it AND the ResolveResult-based `Detail::BetterConversion(ResolveResult, IType,
// IType)` overload (below) can call it for its recursion (the C# `BetterConversion(ResolveResult,
// ...)` recursion calls the public IType overload `BetterConversion(inferredRet, ret1, ret2)` /
// `BetterConversion(resolveResult.Type, t1, t2)`; the port's Detail dispatch uses the uncached
// `Detail::` free function rather than the cached public method -- the `BetterConversionTarget`
// precedent, the cache being an instance-level optimization that is immaterial to the result).
//
// Pure given a compilation (delegates only to the already-ported `IdentityConversion` D514 and
// `BetterConversionTarget` D535, both of which take `const ICompilation&`), so it lands as a
// `Detail::` free function with the same compilation parameter. Takes `IType&` non-const (the
// callees take non-const `IType&` for the non-const `AcceptVisitor`, D406). Returns `int`.
int BetterConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                     ILSpy::Decompiler::TypeSystem::IType& s,
                     ILSpy::Decompiler::TypeSystem::IType& t1,
                     ILSpy::Decompiler::TypeSystem::IType& t2);

// The C# `public int BetterConversion(ResolveResult resolveResult, IType t1, IType t2)`
// (CSharpConversions.cs line 1540, C# 8.0 spec section 12.6.4.5 "better conversion from
// expression") -- the better conversion from an EXPRESSION `resolveResult` to two candidate
// target types `t1` / `t2`: `0` = neither is better, `1` = `t1` is better, `2` = `t2` is better.
// An expression that EXACTLY MATCHES a target (`IsExactlyMatching` D543) beats one that does not;
// when neither exactly matches, an implicit span conversion (`IsImplicitSpanConversion` D538)
// from the expression's type to a target breaks the tie; when both (or neither) exactly match,
// the verdict falls to `BetterConversionTarget` (D535); and for a LAMBDA expression the delegate
// `Invoke` signatures are compared (the inferred return type's better conversion to the two
// delegate return types, with the `Task<T>` wrapper unpacked for async lambdas).
//
// The C# body: `bool t1Exact = IsExactlyMatching(resolveResult, t1); bool t2Exact =
// IsExactlyMatching(resolveResult, t2); if (t1Exact && !t2Exact) return 1; if (t2Exact &&
// !t1Exact) return 2; if (!t1Exact && !t2Exact) { ... IsImplicitSpanConversion(resolveResult.Type,
// t1/t2) ... } if (t1Exact == t2Exact) { int r = BetterConversionTarget(t1, t2); if (r != 0)
// return r; } if (resolveResult is LambdaResolveResult lambda) { ...delegate Invoke comparison,
// async Task<T> unpack, recurse BetterConversion(inferredRet, ret1, ret2)... } else return
// BetterConversion(resolveResult.Type, t1, t2);`. The two `BetterConversion(...)` recursion calls
// are the IType overload (the C# `inferredRet` / `resolveResult.Type` are `IType`, not
// `ResolveResult`), so they delegate to `Detail::BetterConversion(IType, IType, IType)` above --
// no recursion back to this ResolveResult overload, so the dispatch terminates.
//
// Pure given a compilation (every callee -- `IsExactlyMatching` D543, `IsImplicitSpanConversion`
// D538, `BetterConversionTarget` D535, the IType `BetterConversion` above, `UnpackExpressionTreeType`
// D534, `GetDelegateInvokeMethod` D533, `LambdaResolveResult::GetInferredReturnType` /
// `IsAnonymousMethod` / `HasParameterList` / `Parameters` / `IsAsync`, `UnpackTask` D42 -- is pure or
// takes `const ICompilation&`; reads no `CSharpConversions` instance state beyond the compilation),
// so it lands as a `Detail::` free function. Takes `const ResolveResult&` (every `ResolveResult`
// member it reads -- `Type()`, the `dynamic_cast` to `LambdaResolveResult`, the lambda accessors --
// is `const`) and `IType& t1` / `IType& t2` non-const (the callees `IsImplicitSpanConversion` /
// `BetterConversionTarget` / the IType `BetterConversion` take non-const `IType&` for the non-const
// `AcceptVisitor`, D406). The `resolveResult.Type()` accessor returns `const IType&`, but the
// span-helper / recursion calls take `IType&` non-const, so the port `const_cast`s it -- the
// underlying type-system object is mutable (the accessor's `const` is the contract), the
// D515/D517/D528 `const_cast` precedent. The lambda arm's `t1 = UnpackExpressionTreeType(t1)` /
// `t2 = UnpackExpressionTreeType(t2)` rebind ports to `const IType*` pointers (a C++ reference
// cannot be rebound); `GetDelegateInvokeMethod` takes `const IType&`, so the rebound pointers
// feed it directly. The `ret1` / `ret2` / `inferredRet` locals (reassigned to `UnpackTask` results)
// are `ITypePtr` (the D529 `shared_from_this` + `const_pointer_cast` ownership convention for
// the `m->ReturnType()` const references; `GetInferredReturnType` returns `ITypePtr` directly).
// Returns `int`.
int BetterConversion(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                     const ILSpy::Decompiler::Semantics::ResolveResult& resolveResult,
                     ILSpy::Decompiler::TypeSystem::IType& t1,
                     ILSpy::Decompiler::TypeSystem::IType& t2);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
