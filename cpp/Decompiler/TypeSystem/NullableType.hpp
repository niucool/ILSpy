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

// Port of ICSharpCode.Decompiler/TypeSystem/NullableType.cs -- the static helper for
// working with `Nullable<T>` types. The C# is a static class of static methods; per
// the extension-method convention they port as free functions in the TypeSystem
// namespace (`ILSpy::Decompiler::TypeSystem`).
//
// PORTED here (the members the CSharpConversions conversion helpers consume -- the
// next prerequisite flagged by the D514 ExplicitEnumerationConversion landing):
//   - IsNullable            (NullableType.cs line 30)
//   - IsNonNullableValueType (NullableType.cs line 40)
//   - GetUnderlyingType     (NullableType.cs line 46)
//   - Create(ICompilation, IType) (NullableType.cs line 56) -- the `Nullable<T>` constructor
//     the user-defined-conversion lifted-operator forms build (`GetApplicableConversionOperators`
//     calls it to lift a non-nullable-value-type operator source/target into `Nullable<T>`).
//
// The `Create(ITypeReference)` overload (line 68) builds a nullable type REFERENCE and needs the
// `ITypeReference` / `ParameterizedTypeReference` construction surface (not yet ported); no
// in-scope consumer constructs a nullable reference here yet, so it stays deferred with that
// surface.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::TypeSystem {

class ICompilation;

// The C# `public static bool NullableType.IsNullable(IType type)` (line 30) -- true iff
// `type` is `Nullable<T>`: a `ParameterizedType` with exactly 1 type argument whose generic
// type is the known `NullableOfT`. Custom modifiers are unwrapped first (`type.SkipModifiers()`,
// so `modopt(Nullable<int>)` is still nullable). The C# `ArgumentNullException` on null compiles
// out (a `const IType&` cannot bind to null).
bool IsNullable(const IType& type);

// The C# `public static bool NullableType.IsNonNullableValueType(IType type)` (line 40) -- true
// iff `type` is a value type that is not nullable: `type.IsReferenceType == false && !IsNullable(type)`.
// The C# `bool? == false` is true only when `IsReferenceType` holds `false` (not `null`, not
// `true`), so an indeterminate reference-ness (`std::nullopt`) yields false.
bool IsNonNullableValueType(const IType& type);

// The C# `public static IType NullableType.GetUnderlyingType(IType type)` (line 46) -- if `type`
// is `Nullable<T>`, returns `T` (the type argument); otherwise returns `type` itself (custom
// modifiers preserved -- the C# `else` returns the original `type`, NOT `SkipModifiers(type)`).
// The returned reference is valid for the lifetime of `type`: the type argument is owned by the
// `ParameterizedType`'s `typeArgs_` member (the `ParameterizedType` reachable through `type`), so
// it outlives the call. The C# `ArgumentNullException` on null compiles out.
const IType& GetUnderlyingType(const IType& type);

// The non-const overload -- the C# has no `const`, so a caller holding a mutable `IType&` (e.g.
// the `CSharpConversions` nullable-conversion helpers, which feed the result to
// `IdentityConversion(IType&, IType&)` -- a NON-const signature because `IType::AcceptVisitor` is
// non-const, the D406 convention) needs a non-const reference back. The underlying object IS
// mutable (the type argument is owned by the `ParameterizedType`'s `typeArgs_` shared handle, or
// the overload returns the caller's own non-const input), so the `const_cast` delegating to the
// const overload is safe (the object was not const-qualified at the call site). The textbook
// const-overload-pair: non-const input prefers this overload; const input falls back to the
// const overload above.
IType& GetUnderlyingType(IType& type);

// The C# `public static IType NullableType.Create(ICompilation compilation, IType elementType)`
// (NullableType.cs line 56) -- builds a `Nullable<T>` over `elementType`: resolves the known
// `Nullable`1` definition via `compilation.FindType(KnownTypeCode.NullableOfT)`, and if that
// resolves to a definition (the normal case), returns `new ParameterizedType(nullableTypeDef,
// { elementType })`; if the definition cannot be resolved (`GetDefinition() == null`), returns the
// `FindType` result itself (the un-parameterized nullable type, a defensive fallback for a type
// system without `System.Nullable`1`).
//
// Returns `ITypePtr` (an OWNING shared handle): the constructed `ParameterizedType` is freshly
// allocated, so the caller must hold the returned handle to keep it alive (the C# returns an
// `IType` reference owned by the GC; the C++ port models that ownership as a `shared_ptr`). The
// `ParameterizedType` constructor takes owning `ITypePtr` constituents, so the port obtains owning
// handles to both the generic type (the resolved definition) and the element via
// `IType::shared_from_this()` (the `enable_shared_from_this<IType>` bridge, D406): both objects are
// shared-managed in the real type system (the definition is owned by the compilation, the element
// by its declaring parameter/return slot), so `shared_from_this` yields a co-owning handle without
// copying. The `else` branch likewise returns `nullableType.shared_from_this()` (the same `FindType`
// result, viewed as an owning handle).
ITypePtr Create(const ICompilation& compilation, const IType& elementType);

} // namespace ILSpy::Decompiler::TypeSystem
