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

#include <memory>

namespace ILSpy::Decompiler::TypeSystem { class IType; }
namespace ILSpy::Decompiler::TypeSystem { class ICompilation; }

namespace ILSpy::Decompiler::Semantics { class Conversion; }
namespace ILSpy::Decompiler::Semantics { class ResolveResult; }

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
