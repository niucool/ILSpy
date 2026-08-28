// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. CAUSED IN ON THE WHICHEVER THEORY OF LIABILITY, WHETHER IN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/TupleType.cs -- the static helpers for working
// with C# 7 tuple types. The C# `TupleType` class itself (the `TypeKind::Tuple` concrete
// `IType` built atop an underlying `System.ValueTuple<...>` parameterized type) is already
// ported as a header-only leaf in `IType.hpp` (the minimal-port `TupleType` class); these are
// the static helpers that were DEFERRED when that class landed (per the `IType.hpp` comment at
// the `TupleType` class). Per the extension-method / static-class convention they port as free
// functions in the TypeSystem namespace (`ILSpy::Decompiler::TypeSystem`) -- the
// `NullableType.{hpp,cpp}` / `TypeSystemExtensions.{hpp,cpp}` precedent.
//
// PORTED here (the helper the CSharpConversions `TupleConversion` conversion consumes -- the
// prerequisite flagged by the IsImplicitSpanConversion landing as the next unblocked piece):
//   - GetTupleElementTypes (TupleType.cs line 168) -- flattens a tuple type (a `TypeKind::Tuple`
//     `TupleType` or an underlying `System.ValueTuple<...>` parameterized type) into the list
//     of its element types, threading the 8-ary `ValueTuple<T1..T7,TRest>` nesting through the
//     `Rest` (the 8th type argument, itself a `ValueTuple<...>`).
//
// The `IsTupleCompatible` (line 111) / `FromUnderlyingType` (line 148) helpers stay deferred:
// `IsTupleCompatible` is consumed only by `FromUnderlyingType` (the type-resolution stage that
// builds a `TupleType` from an underlying `ValueTuple<...>`, which the minimal-port `TupleType`
// ctor accepts pre-built), and `TupleConversion` consumes only `GetTupleElementTypes`; no
// in-scope consumer needs `IsTupleCompatible` yet, so it lands with the type-resolution stage.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <optional>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `public static ImmutableArray<IType> GetTupleElementTypes(IType tupleType)`
// (TupleType.cs line 168) -- flattens a tuple type into its element types. Returns
// `default(ImmutableArray<IType>)` (the C# `IsDefault` sentinel) when `tupleType` is not a
// tuple; otherwise a non-empty array of the element types.
//
// The C# `default(ImmutableArray<IType>)` (a null/empty sentinel distinct from a present
// zero-element array) ports to `std::optional<std::vector<ITypePtr>>`: `std::nullopt` is the
// `IsDefault` sentinel (not a tuple), and a populated `std::vector<ITypePtr>` is the element
// types (the helper only ever returns a non-empty vector when it succeeds -- it adds at least
// one element before returning `true`). The `IsDefault` / `IsDefaultOrEmpty` checks the
// `TupleConversion` caller performs map to `!has_value()` / `!has_value() || value.empty()`.
//
// The returned `ITypePtr` handles share ownership of the element types with the input type (a
// `TupleType`'s `ElementTypes` or a `ParameterizedType`'s `TypeArguments`), so they outlive the
// call regardless of the input's lifetime.
std::optional<std::vector<ITypePtr>> GetTupleElementTypes(const IType& tupleType);

}  // namespace ILSpy::Decompiler::TypeSystem
