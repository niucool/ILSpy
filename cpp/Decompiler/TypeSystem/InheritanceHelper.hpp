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

// Port of ICSharpCode.Decompiler/TypeSystem/InheritanceHelper.cs -- the `public static class
// InheritanceHelper` (a namespace of free functions in the C++ port, the C#-static-class convention):
// "Provides helper methods for inheritance." The base-member lookup (`GetBaseMember` /
// `GetBaseMembers`) walks the base types of a member's declaring type and yields the same-signature
// base members (specialized with the original substitution); the derived-member lookup
// (`GetDerivedMember`) walks a derived type's members for the one that overrides a base member; and
// the attribute helpers (`GetAttributes` / `GetAttribute`) collect attributes up the base-type /
// override chain. All deps are ported: `SignatureComparer.Ordinal` (D478), `GetNonInterfaceBaseTypes` /
// `GetAllBaseTypes` (`TypeSystemExtensions`), `IType::GetMembers`/`GetAccessors` (D477), the `IMember`
// surface (`MemberDefinition` / `Substitution` / `Specialize` / `DeclaringTypeDefinition` /
// `IsOverride` / `IsExplicitInterfaceImplementation` / `ExplicitlyImplementedInterfaceMembers`),
// `ITypeDefinition::Methods`/`Properties`/`Events`/`Fields`, and `IEntity::GetAttributes`/
// `GetAttribute(KnownAttribute)` / `HasAttribute`.

#pragma once

#include "Decompiler/TypeSystem/IMember.hpp"  // IMember (the lookup subject + result)
#include "Decompiler/TypeSystem/IType.hpp"  // GetMemberOptions (the family Get* options)

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `public static class InheritanceHelper` -- a namespace of free functions.
namespace InheritanceHelper {

// The C# `public static IMember? GetBaseMember(IMember member)` -- "Gets the base member that has the
// same signature." Returns the first of `GetBaseMembers(member, false)` (the derived-most base member),
// or nullptr if there is none. A non-owning `const IMember*` (the C# `IMember?`; the base member is
// owned by the base type's graph -- `Specialize` returns a non-owning handle, "the type system owns it").
const IMember* GetBaseMember(const IMember& member);

// The C# `public static IEnumerable<IMember> GetBaseMembers(IMember member, bool includeImplemented
// Interfaces)` -- "Gets all base members that have the same signature. The member from the derived-most
// base class is returned first."
//   - if `includeImplementedInterfaces` and the member is an explicit interface impl with exactly one
//     explicitly-implemented member, switch to that member (the C#-style explicit interface impl);
//   - strip the generic specialization (`member = member.MemberDefinition`);
//   - if no `DeclaringTypeDefinition` (a global method), yield empty (the SharpDevelop UDC crash 4524
//     guard);
//   - for each base type (in reverse -- derived-last), if it's not the member's own declaring type,
//     fetch the base type's `GetMembers` (or `GetAccessors` for an `Accessor` SymbolKind) filtered by
//     name + `Accessibility > Private`, and yield the `SignatureComparer.Ordinal.Equals` matches
//     specialized with the original `substitution`.
// Returns non-owning `const IMember*` snapshots (the C# `IEnumerable<IMember>` materialized; the base
// members are owned by the base types, kept alive via the member's `DeclaringTypeDefinition` graph).
std::vector<const IMember*> GetBaseMembers(const IMember& member, bool includeImplementedInterfaces);

} // namespace InheritanceHelper

} // namespace ILSpy::Decompiler::TypeSystem
