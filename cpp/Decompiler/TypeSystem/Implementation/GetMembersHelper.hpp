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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/GetMembersHelper.cs -- the static
// helper that routes member enumeration for an `IType` implementation. The C# `static class
// GetMembersHelper` (a namespace of free functions in the C++ port) provides the member-
// enumeration logic an `IType` delegate to from its `GetMethods` / `GetProperties` / `GetFields`
// / `GetEvents` / `GetConstructors` / `GetAccessors` / `GetMembers` overrides (the
// `ParameterizedType` routing arm, a later leaf, calls these). It applies the caller's
// `Delegate<Predicate>` filter and `GetMemberOptions` flags, traverses the non-interface base
// types when `IgnoreInheritedMembers` is unset, and -- for a `ParameterizedType` base (or a
// call supplying method type arguments) -- builds the `Specialized*` instances that substitute
// the type parameters with the type arguments.
//
// OWNING RETURN MODEL (the port divergence the C# `IEnumerable<T>`-GC model maps to):
// The C# returns `IEnumerable<IMethod>` etc. of freshly-`new`-allocated `Specialized*` objects
// (GC-owned; the `IEnumerable` is lazy). The C++ port returns OWNING `std::vector<std::shared_ptr<
// const T>>` snapshots -- the `Specialized*` instances are `std::make_shared`-allocated and owned
// by the returned vector (the caller -- eventually the `ParameterizedType` routing arm's cache --
// holds the `shared_ptr`s alive); the unspecialized definitions in the definitions arm are ALIASED
// to the base type via `baseType->shared_from_this()` (the base type transitively owns its declared
// members -- a `ParameterizedType` owns its `genericType_`, which owns the `ITypeDefinition`, which
// owns the methods; the aliasing `shared_ptr` keeps the base alive, keeping the members alive). The
// `const_cast` in the aliasing reconciles the port's const-correct `const IMethod*` return from
// `IType::GetMethods` (D477) with the non-const `std::shared_ptr<IMethod>` the `Specialized*` ctors
// take (the C# `IMethod methodDefinition` is non-const). A later leaf (the `ParameterizedType`
// routing arm) caches these owning vectors in a `mutable` member and returns the non-owning
// `const T*` snapshots `IType::GetMethods` promises.
//
// MUTUAL RECURSION (bounded by `ReturnMemberDefinitions`): `GetMethodsImpl` calls
// `baseType->GetMethods(filter, options | declaredMembers)` (`declaredMembers =
// IgnoreInheritedMembers | ReturnMemberDefinitions`). For a `ParameterizedType` base, that call
// hits the D489 `ReturnMemberDefinitions` arm (delegates to `genericType->GetMethods(...)` -- the
// generic definition's declared methods); the `ReturnMemberDefinitions` flag is the bound (the C#
// header comment's "both IgnoreInheritedMembers and ReturnMemberDefinitions set" invariant -- no
// `StackOverflowException`). This leaf (D489) put that arm in place; this leaf (D490) ports the
// routing that consumes it.
//
// DEFERRED: `GetNestedTypes` (the most complex family -- builds parameterized nested types with a
// mix of outer-type arguments and nested-type arguments; not used by `MemberLookup.LookupGroup`).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"  // IType + GetMemberOptions + the family Get* decls
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `static class GetMembersHelper` -- a namespace of free functions (the C#-static-class
// convention). Each public entry mirrors a `ParameterizedType.cs` member-enumeration override's
// routing arm: apply the filter + flags, traverse the base types if not `IgnoreInheritedMembers`,
// and build `Specialized*` for a `ParameterizedType` base / supplied method type arguments.
namespace GetMembersHelper {

// The C# `GetMethods(IType type, Predicate<IMethod> filter, GetMemberOptions options)` -- all
// methods callable on `type` (not ctors or accessors). The simple overload delegates to the
// typeArguments overload with `null` (the C# `return GetMethods(type, null, filter, options)`).
std::vector<std::shared_ptr<const IMethod>> GetMethods(
    const IType* type,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options);

// The C# `GetMethods(IType type, IReadOnlyList<IType> typeArguments, Predicate<IMethod> filter,
// GetMemberOptions options)` -- the generic-method overload: `typeArguments` (nullable; a null
// `const std::vector<ITypePtr>*` is the C# `null`) supplies method type arguments and filters the
// methods to those whose `TypeParameters.Count` matches (`FilterTypeParameterCount`).
std::vector<std::shared_ptr<const IMethod>> GetMethods(
    const IType* type,
    const std::vector<ITypePtr>* typeArguments,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options);

// The C# `GetConstructors(IType type, Predicate<IMethod> filter, GetMemberOptions options =
// IgnoreInheritedMembers)` -- the instance constructors.
std::vector<std::shared_ptr<const IMethod>> GetConstructors(
    const IType* type,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options);

// The C# `GetAccessors(IType type, Predicate<IMethod> filter, GetMemberOptions options)` -- the
// accessors of the properties / events on `type`.
std::vector<std::shared_ptr<const IMethod>> GetAccessors(
    const IType* type,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options);

// The C# `GetProperties(IType type, Predicate<IProperty> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IProperty>> GetProperties(
    const IType* type,
    std::function<bool(const IProperty*)> filter,
    GetMemberOptions options);

// The C# `GetFields(IType type, Predicate<IField> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IField>> GetFields(
    const IType* type,
    std::function<bool(const IField*)> filter,
    GetMemberOptions options);

// The C# `GetEvents(IType type, Predicate<IEvent> filter, GetMemberOptions options)`.
std::vector<std::shared_ptr<const IEvent>> GetEvents(
    const IType* type,
    std::function<bool(const IEvent*)> filter,
    GetMemberOptions options);

// The C# `GetMembers(IType type, Predicate<IMember> filter, GetMemberOptions options)` -- all
// members (methods + properties + fields + events; NOT ctors). Composes the four families (the
// `GetMembersImpl` composition; the `IMember` filter is passed to each family `*Impl` via the
// `std::function`-invocable-with-derived-arg conversion -- the C# `Predicate<in T>` contravariance
// analogue).
std::vector<std::shared_ptr<const IMember>> GetMembers(
    const IType* type,
    std::function<bool(const IMember*)> filter,
    GetMemberOptions options);

} // namespace GetMembersHelper

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
