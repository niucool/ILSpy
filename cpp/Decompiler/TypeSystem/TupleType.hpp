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
// `IsTupleCompatible` landed with the type-resolution stage (the
// `ApplyAttributeTypeVisitor.VisitParameterizedType` consumer) together with
// the compilation-driven constructor (`CreateTupleType`, the C# ctor that
// builds the underlying `ValueTuple<...>` chain); `FromUnderlyingType`
// (line 148) stays deferred (no in-scope consumer yet).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class ICompilation;
class IModule;

// The C# `public const int RestPosition = 8` (TupleType.cs line 33): the 8-ary
// `ValueTuple<T1..T7,TRest>` nests further elements in the `TRest` (8th) type
// argument. The port's full-fidelity name (the free-function convention); the
// `TupleType.cpp` internals use the same value (`kRestPosition`).
inline constexpr int TupleRestPosition = 8;

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

// The C# `public static IType TupleUnderlyingTypeOrSelf(this IType type)` (TupleType.cs line
// 381, the `TupleTypeExtensions` static class) -- the tuple-unwrap the `TypeInference`
// bound-inference workers apply before their array/parameterized pattern matches (a
// `TypeKind::Tuple` type delegates to its underlying `System.ValueTuple<...>` parameterized
// type, so e.g. a tuple-to-`ValueTuple` comparison reaches the parameterized-type arms):
// `var t = (type as TupleType)?.UnderlyingType ?? type; return t.WithoutNullability();`.
// The `?. ??` combination falls back to the ORIGINAL type both when the input is not a tuple
// and when a degenerate `TupleType` carries a null underlying type (the C# `??` catches both
// shapes -- the port keeps the tuple itself for the degenerate one, never returning null for
// a non-null input). The `WithoutNullability()` tail (TypeSystemExtensions.cs line 799) is
// why the parameter is NON-CONST: `ChangeNullability` is non-const (the `shared_from_this`
// D406 convention), so every caller must pass a shared-managed type.
ITypePtr TupleUnderlyingTypeOrSelf(IType& type);

// The C# `public static bool IsTupleCompatible(IType type, out int tupleCardinality)`
// (TupleType.cs line 111) -- lifted deferral (the type-resolution stage
// `ApplyAttributeTypeVisitor.VisitParameterizedType` consumes): whether the
// type is a valid underlying type for a tuple (also true for tuple types
// themselves). The `TypeKind::Tuple` arm answers the tuple's own cardinality;
// the `TypeKind::Struct` arm requires `System.ValueTuple` BY NAME (the C#
// comment: a class of that name is some other type that happens to share it
// and must not become tuple syntax -- the C# checks `TypeKind.Struct`, NOT
// `Class`), accepts arity 1..7 directly, and arity 8 only when the `TRest`
// (8th) type argument is itself tuple-compatible, adding `RestPosition - 1`
// to the nested cardinality. `tupleCardinality` is 0 on a false return (the
// C# `out` contract -- both are assigned in every path).
bool IsTupleCompatible(const IType& type, int& tupleCardinality);

// The C# `public TupleType(ICompilation compilation, ImmutableArray<IType>
// elementTypes, ImmutableArray<string> elementNames = default,
// IModule valueTupleAssembly = null)` (TupleType.cs line 52) -- the
// compilation-driven constructor that BUILDS the underlying
// `System.ValueTuple<...>` chain (`CreateUnderlyingType`, TupleType.cs line
// 73: the `remainder`-arity `ValueTuple` over the last `remainder` elements,
// wrapped by 8-ary `ValueTuple` levels carrying 7 elements + the nested type,
// each generic resolved through `FindValueTupleType` -- the
// value-tuple-assembly definition first, the compilation-wide `FindType`
// fallback second). The port's `TupleType` class (IType.hpp) takes the
// PRE-BUILT underlying type (the minimal-leaf convention), so this ctor ports
// as the free `CreateTupleType` factory (the static-helper convention).
//
// `elementNames` is `nullopt` for the C# `default(ImmutableArray<string>)`
// (the names filled with nulls -- the port's empty-string mapping); a provided
// vector maps the C# null entries (the `Array.Copy` tail beyond the copied
// names range) to empty strings. `valueTupleAssembly` null falls straight to
// the compilation lookup. The returned `TupleType` owns the built underlying
// chain. (The C# ctor's `elementNames.Length == elementTypes.Length`
// Debug.Assert is compiled out of the release assembly the shipped engine
// runs; the port's TupleType ctor keeps the assert.)
std::shared_ptr<TupleType> CreateTupleType(
    const ICompilation& compilation,
    std::vector<ITypePtr> elementTypes,
    std::optional<std::vector<std::string>> elementNames = std::nullopt,
    const IModule* valueTupleAssembly = nullptr);

}  // namespace ILSpy::Decompiler::TypeSystem
