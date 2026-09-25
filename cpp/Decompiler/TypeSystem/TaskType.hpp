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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/TaskType.cs -- the static helper for working
// with `System.Threading.Tasks.Task` and Task-like types. The C# is a static class of static
// methods; per the extension-method convention they port as free functions in the TypeSystem
// namespace (`ILSpy::Decompiler::TypeSystem`), like the `NullableType` helpers.
//
// PORTED here (the members the deferred `CSharpConversions` arms consume -- the prerequisite
// flagged by the D538 `TupleConversion` landing, the next natural increment toward the
// `BetterConversion(ResolveResult, IType, IType)` / `IsExactlyMatching` arms):
//   - IsTask              (TaskType.cs line 37) -- `Task` / `Task<T>` recognition.
//   - IsCustomTask        (TaskType.cs line 47) -- a Task-like type (any type carrying an
//     `[AsyncMethodBuilder]` attribute with a single `System.Type` fixed argument); reports the
//     builder type via the out-parameter.
//   - UnpackTask          (TaskType.cs line 19) -- the `T` in `Task<T>` (`void` for the non-generic
//     `Task`, the type itself for a non-task).
//   - UnpackAnyTask       (TaskType.cs line 29) -- the element type of any Task-like type
//     (`Task`, `Task<T>`, `ValueTask`, `ValueTask<T>`, or any `[AsyncMethodBuilder]` custom type).
//
// DEFERRED (the builder-type-name helpers need `FullTypeName` construction from the builder type's
// `ReflectionName` plus the `TopLevelTypeName` constant for the built-in builders -- the
// `IsNonGenericTaskType` / `IsGenericTaskType` arms -- and `Create` needs the `Task`/`Task`1`
// `FindType` resolution + `ParameterizedType` construction; no in-scope consumer needs them
// before the deferred `CSharpConversions` arms land, so they stay deferred with that surface):
//   - IsNonGenericTaskType (TaskType.cs line 73)
//   - IsGenericTaskType    (TaskType.cs line 90)
//   - Create               (TaskType.cs line 101)

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::TypeSystem {

class ICompilation;

// The C# `public static bool TaskType.IsTask(IType type)` (TaskType.cs line 37) -- whether the
// type is `Task` or `Task<T>`: a type whose definition's `KnownTypeCode` is `Task` (the
// non-generic `System.Threading.Tasks.Task`), or `TaskOfT` AND the type is a `ParameterizedType`
// (a `Task<T>`, NOT the bare `Task`1` definition). The C# `ArgumentNullException` on null compiles
// out (a `const IType&` cannot bind to null).
bool IsTask(const IType& type);

// The C# `public static bool TaskType.IsCustomTask(IType type, out IType builderType)`
// (TaskType.cs line 47) -- whether the type is a Task-like type (any type, other than `Task`/
// `Task<T>`, carrying an `[AsyncMethodBuilder]` attribute whose single fixed argument is a
// `System.Type`). On success `builderType` is the builder type (the `[AsyncMethodBuilder(T)]`
// argument); on every failure path it is reset to a null `ITypePtr` (faithful to the C#
// `builderType = null` at the top). A type with more than one type parameter is rejected before
// the attribute lookup (the C# `def.TypeParameterCount > 1` early-out). The attribute must have
// exactly one fixed argument whose decoded `Type` is `System.Type` (`KnownTypeCode::Type`); the
// boxed `Value` is unboxed as an `ITypePtr` (the C# `(IType)arg.Value` -- the decoder stores the
// `IType` for a `System.Type`-typed argument). The pointer-form `std::any_cast` returns null on a
// type mismatch (the safe faithful fallback for a divergent state the C# would
// `InvalidCastException` on); the helper then returns false with `builderType` already null.
bool IsCustomTask(const IType& type, ITypePtr& builderType);

// The C# `public static bool TaskType.IsNonGenericTaskType(IType task, out
// FullTypeName builderTypeName)` (TaskType.cs lines 73-84): whether the type
// is a non-generic Task-like -- `Task` itself, or a custom Task-like whose
// `[AsyncMethodBuilder]` builder type has no type parameters. On success
// `builderTypeName` receives the builder's full name.
bool IsNonGenericTaskType(const IType& task, FullTypeName& builderTypeName);

// The C# `public static bool TaskType.IsGenericTaskType(IType task, out
// FullTypeName builderTypeName)` (TaskType.cs lines 90-104): whether the
// type is a generic Task-like -- `Task<T>`, or a custom Task-like whose
// builder type has exactly one type parameter. On success `builderTypeName`
// receives the builder's full name.
bool IsGenericTaskType(const IType& task, FullTypeName& builderTypeName);

// The C# `public static IType TaskType.UnpackTask(ICompilation compilation, IType type)`
// (TaskType.cs line 19) -- gets the `T` in `Task<T>`: returns `void` for the non-generic `Task`,
// the type argument for `Task<T>`, and the type itself unmodified for any non-task type. Returns
// an OWNING `ITypePtr` (the C# returns an `IType` reference owned by the GC; the C++ port models
// that ownership as a `shared_ptr`). For the non-task passthrough and the non-generic-`Task`-void
// cases the owning handle is obtained via `IType::shared_from_this()` (the
// `enable_shared_from_this<IType>` bridge, D406) with `const_pointer_cast` (the `FindType` /
// input-type accessors return `const` references, but the underlying type-system objects are
// mutable); for the `Task<T>` case the type argument is a co-owning copy of the
// `ParameterizedType`'s stored `ITypePtr` (`GetTypeArgument(0)` returns by value).
ITypePtr UnpackTask(const ICompilation& compilation, const IType& type);

// The C# `public static IType TaskType.UnpackAnyTask(ICompilation compilation, IType type)`
// (TaskType.cs line 29) -- gets the element type of any Task-like type (`Task`, `Task<T>`,
// `ValueTask`, `ValueTask<T>`, or any `[AsyncMethodBuilder]` custom type): returns `void` for a
// non-generic task-like, the type argument for a generic one, and the type itself unmodified for
// any non-task-like type. Returns an OWNING `ITypePtr` (the same ownership convention as
// `UnpackTask`). The generic-task-like branch reads `TypeArguments[0]` via a
// `dynamic_cast<const ParameterizedType*>` + null guard (the port's `TypeArguments()` is
// `ParameterizedType`-specific, NOT on the `IType` interface -- the `BetterConversionTarget` /
// `SpanConversion` precedent); a degenerate 1-type-param non-parameterized custom task falls back
// to the input-type passthrough (the safe faithful fallback, since such a shape does not occur
// in practice -- a 1-type-param task-like is always parameterized).
ITypePtr UnpackAnyTask(const ICompilation& compilation, const IType& type);

} // namespace ILSpy::Decompiler::TypeSystem
