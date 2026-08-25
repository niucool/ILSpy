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

// Port of the `MemberLookup` Lookup-region private helpers (ICSharpCode.Decompiler/CSharp/Resolver/
// MemberLookup.cs). The C# `MemberLookup` exposes the accessibility surface (`IsAccessible` etc.,
// already ported in `MemberLookup.hpp`) and the Lookup region (`GetAccessibleMembers` /
// `LookupType` / `Lookup` / `LookupIndexers`, all deferred) which is built from a set of private
// helpers operating on `LookupGroup` (D495). The C# nests these as `private` / `private static`
// methods of `MemberLookup`; since they are PURE transformations over their arguments (the
// `AddNestedTypes` / `AddMembers` helpers do NOT reference `MemberLookup` instance state -- only
// `IsAccessible` does, which `AddMembers` takes as a bool arg), the port lifts them to free functions
// in this `Detail` namespace so they are individually unit-testable (TDD) ahead of the public Lookup
// methods that will compose them. `MemberLookup::Lookup` / `LookupType` / `GetAccessibleMembers` /
// `LookupIndexers` (a later leaf) will call these `Detail::` helpers.
//
// `InnerTypeParameterCount` (the C# `static int InnerTypeParameterCount(IType type)`) computes
// `type.TypeParameterCount - (type.DeclaringType?.TypeParameterCount ?? 0)`. The C# `IType.DeclaringType`
// is abstract (the `AbstractType` default is null); the port's `IType` (D271) does NOT declare
// `DeclaringType` (the D388 decision -- avoids the C# `IType.DeclaringType`-vs-`IEntity.DeclaringType`
// ambiguity; `ITypeDefinition` inherits `IEntity::DeclaringType()`). `InnerTypeParameterCount` therefore
// reads the declaring type via `type.GetDefinition()->DeclaringType()` -- the definition's declaring
// type (unspecialized). For a `ParameterizedType` nested type `Outer<int>.Inner`, `GetDefinition()` is
// the `Outer.Inner` definition, whose `DeclaringType()` is `Outer` (unspecialized) -- the SAME
// `TypeParameterCount` as the parameterized declaring type `Outer<int>` (both carry the outer's count),
// so the inner count is correct for both the unspecialized and the parameterized case (no need to port
// `ParameterizedType.DeclaringType`). A type with no definition / no declaring type returns its full
// `TypeParameterCount` (the C# `type.DeclaringType != null ? ... : 0` -> `total - 0`).

#pragma once

#include "Decompiler/CSharp/Resolver/LookupGroup.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <optional>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

// The C# `static int InnerTypeParameterCount(IType type)`:
//   return type.TypeParameterCount - (type.DeclaringType != null ? type.DeclaringType.TypeParameterCount : 0);
// The "inner types contain the type parameters of outer types; therefore this count has to be adjusted."
// See the file header for the `GetDefinition()->DeclaringType()` approach (the `IType`-has-no-
// `DeclaringType` D388 resolution).
int InnerTypeParameterCount(const ILSpy::Decompiler::TypeSystem::IType& type);

// The C# `void AddNestedTypes(IType type, IEnumerable<IType> nestedTypes, int typeArgumentCount,
// List<LookupGroup> lookupGroups, ref IEnumerable<IType> typeBaseTypes, ref List<IType> newNestedTypes)`.
// Adds the `nestedTypes` to `newNestedTypes`, and -- for each existing lookup group whose
// `DeclaringType` is a base of `type` (i.e. in `type.GetNonInterfaceBaseTypes()`) -- hides the group's
// methods + non-method and removes its same-`InnerTypeParameterCount` nested types (the base's nested
// types are hidden by the derived `type`'s nested types). `AllHidden` groups are skipped. The C# `ref`
// lazily-initialized `typeBaseTypes` (filled on demand from `GetNonInterfaceBaseTypes`) and
// `newNestedTypes` (allocated on the first nested type) map to `std::optional<std::vector<...>>&`
// (`std::nullopt` = the C# `null`).
void AddNestedTypes(const ILSpy::Decompiler::TypeSystem::IType& type,
                    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& nestedTypes,
                    int typeArgumentCount,
                    std::vector<LookupGroup>& lookupGroups,
                    std::optional<std::vector<const ILSpy::Decompiler::TypeSystem::IType*>>& typeBaseTypes,
                    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& newNestedTypes);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
