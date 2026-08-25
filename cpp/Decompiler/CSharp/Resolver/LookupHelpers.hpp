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
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"

#include <optional>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward declaration: `AddMembers` takes a `const MemberLookup&` (for the `IsAccessible` check);
// `MemberLookup` is defined in `MemberLookup.hpp` (full definition not needed here -- the helper
// only references it through a reference parameter).
class MemberLookup;

namespace Detail {

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

// The C# `void AddMembers(IType type, IEnumerable<IMember> members, bool allowProtectedAccess,
// List<LookupGroup> lookupGroups, bool treatAllParameterizedMembersAsMethods, ref IEnumerable<IType>
// typeBaseTypes, ref List<IParameterizedMember> newMethods, ref IMember newNonMethod)`.
// Adds the `members` to `newMethods` (for parameterized members / methods) / `newNonMethod` (for
// non-methods), removing hidden members from the existing lookup groups and substituting an override
// for the virtual it replaces. `AddMembers` uses `IsAccessible` (a `MemberLookup` instance method), so
// the free function takes a `const MemberLookup&` (the lookup context for the accessibility check).
// `treatAllParameterizedMembersAsMethods` makes a property/indexer count as a "method" (the
// `LookupIndexers` arm -- it casts `member as IParameterizedMember` instead of `as IMethod`); the
// `typeArguments.Count != 0` caller (`Lookup`) only fetches methods, so a method's `SymbolKind` is
// `Method` either way. The C# `ref` lazily-initialized `typeBaseTypes` / `newMethods` map to
// `std::optional<std::vector<...>>&`; `newNonMethod` is a plain `const IMember*&` (the C# `ref IMember`).
void AddMembers(const MemberLookup& lookup,
                const ILSpy::Decompiler::TypeSystem::IType& type,
                const std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>& members,
                bool allowProtectedAccess,
                std::vector<LookupGroup>& lookupGroups,
                bool treatAllParameterizedMembersAsMethods,
                std::optional<std::vector<const ILSpy::Decompiler::TypeSystem::IType*>>& typeBaseTypes,
                std::optional<std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>>& newMethods,
                const ILSpy::Decompiler::TypeSystem::IMember*& newNonMethod);

// The C# `void RemoveInterfaceMembersHiddenByClassMembers(List<LookupGroup> lookupGroups)`.
// Walks the lookup groups: a CLASS group (NOT interface/Object) with nested types OR a visible
// non-method hides ALL interface groups' members (methods + non-method + nested types); a class group
// with visible methods (no nested, non-method hidden) removes the same-signature methods from interface
// groups (`SignatureComparer.Ordinal.Equals`) + hides interface non-methods + nested types. An
// interface/Object group is skipped (not treated as a "class" group). A pure transformation over
// `lookupGroups` (no instance state), so it lifts to a free function.
void RemoveInterfaceMembersHiddenByClassMembers(std::vector<LookupGroup>& lookupGroups);

// The C# `static bool IsInterfaceOrSystemObject(IType type)` -- "return true if type is an interface or
// System.Object": `type.Kind == Interface || type.GetDefinition()?.KnownTypeCode == Object`.
bool IsInterfaceOrSystemObject(const ILSpy::Decompiler::TypeSystem::IType& type);

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail

} // namespace ILSpy::Decompiler::CSharp::Resolver
