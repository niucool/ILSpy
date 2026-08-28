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

namespace ILSpy::Decompiler::TypeSystem { class IType; }

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
