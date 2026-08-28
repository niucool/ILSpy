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

namespace ILSpy::Decompiler::Semantics { class Conversion; }

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
