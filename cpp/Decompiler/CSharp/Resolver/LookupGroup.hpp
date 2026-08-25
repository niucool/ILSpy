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

// Port of the `MemberLookup.LookupGroup` nested class (ICSharpCode.Decompiler/CSharp/Resolver/
// MemberLookup.cs, the `#region class LookupGroup`). The C# `sealed class LookupGroup` is the value
// type the `MemberLookup` Lookup region (`GetAccessibleMembers` / `LookupType` / `Lookup` /
// `LookupIndexers`) builds per (declaring-type, member-name) group and mutates as it traverses the
// base types -- hiding members shadowed by a derived class and substituting an override for the
// virtual it replaces.
//
// The C# nests `LookupGroup` inside `MemberLookup`; the port makes it a separate class in the same
// `CSharp::Resolver` namespace (the C++-nested-class-in-header-only convention is awkward, and the
// Lookup regions -- a later leaf -- include this header). It is NOT final (the C# is `sealed` but the
// port does not seal value types; no derivation is intended).
//
// FIELD OWNING MODEL (the C# `List<T>`-GC model maps to):
//  - `DeclaringType` (the C# `readonly IType DeclaringType`): a NON-OWNING `const IType*` -- the
//    `MemberLookup` regions receive `const IType*` raw pointers from `GetNonInterfaceBaseTypes` /
//    the loop variable `IType type`; the group observes the type (the caller -- the
//    `GetNonInterfaceBaseTypes` snapshot's owners -- keeps it alive for the lookup's duration).
//    `AddMembers` / `AddNestedTypes` compare `typeBaseTypes.Contains(lookupGroup.DeclaringType)` --
//    pointer-identity comparison, faithful to the C# `IEnumerable<IType>.Contains` (reference
//    equality on `IType`).
//  - `NestedTypes` (the C# `List<IType> NestedTypes` -- mutable, cleared-when-hidden): an OWNING
//    `std::vector<ITypePtr>` (shared_ptr) -- `GetNestedTypes` returns OWNING `ITypePtr` (D494), so
//    the group owns the nested-type instances it holds. Mutable (the Lookup region hides nested
//    types by clearing it; the C# sets it to `null`, the port clears the vector -- `AllHidden`'s
//    `.Count > 0` check maps to `!empty()`).
//  - `Methods` (the C# `readonly List<IParameterizedMember> Methods`): an OWNING
//    `std::vector<const IParameterizedMember*>` (the vector is owned, the ELEMENTS are non-owning --
//    the type system owns the methods, the D477 "type system owns" convention). Mutable for the
//    override-replacement (`AddMembers` does `Methods[j] = method`).
//  - `NonMethod` (the C# `IMember NonMethod` -- mutable): a NON-OWNING `const IMember*` (the type
//    system owns the member). Mutable (`AddMembers` replaces it with the override).
//  - `MethodsAreHidden` / `NonMethodIsHidden` (the C# mutable bools): plain `bool` members, exposed
//    via non-const ref accessors (the Lookup region sets them directly).
//
// The ctor (the C# `LookupGroup(IType, List<IType>, List<IParameterizedMember>, IMember)`) sets
// `MethodsAreHidden = (methods == null || methods.Count == 0)` and `NonMethodIsHidden = (nonMethod
// == null)`. The nullable `methods` / `nestedTypes` parameters map to nullable `const
// std::vector<T>*` (a null pointer is the C# `null`); the ctor moves a non-null vector in (an empty
// vector stays empty -- `MethodsAreHidden` is true either way).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"

#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The `MemberLookup.LookupGroup` nested class -- the per-(declaring-type, member-name) group the
// Lookup region builds and mutates. See the file header for the owning model.
class LookupGroup {
public:
    // The C# `LookupGroup(IType declaringType, List<IType> nestedTypes, List<IParameterizedMember>
    // methods, IMember nonMethod)`. `nestedTypes` / `methods` are nullable (a null pointer is the
    // C# `null`); a non-null vector is moved in. `DeclaringType` is a non-owning raw pointer.
    LookupGroup(const ILSpy::Decompiler::TypeSystem::IType* declaringType,
                std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>* nestedTypes,
                std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>* methods,
                const ILSpy::Decompiler::TypeSystem::IMember* nonMethod)
        : declaringType_(declaringType),
          nonMethod_(nonMethod),
          methodsAreHidden_(methods == nullptr || methods->empty()),
          nonMethodIsHidden_(nonMethod == nullptr)
    {
        if (nestedTypes != nullptr) {
            nestedTypes_ = std::move(*nestedTypes);
        }
        if (methods != nullptr) {
            methods_ = std::move(*methods);
        }
    }

    // The C# `public readonly IType DeclaringType`.
    const ILSpy::Decompiler::TypeSystem::IType* DeclaringType() const { return declaringType_; }

    // The C# `public List<IType> NestedTypes` (mutable). Returns a non-const ref so the Lookup region
    // can clear it (the C# sets it to `null`; the port clears the vector).
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& NestedTypes() { return nestedTypes_; }
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& NestedTypes() const { return nestedTypes_; }

    // The C# `public readonly List<IParameterizedMember> Methods`. Returns a non-const ref so
    // `AddMembers` can replace an element (`Methods[j] = method`).
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>& Methods() { return methods_; }
    const std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>& Methods() const { return methods_; }

    // The C# `public bool MethodsAreHidden` (mutable). Returns a non-const ref so the Lookup region
    // can set it (`lookupGroup.MethodsAreHidden = true`).
    bool& MethodsAreHidden() { return methodsAreHidden_; }
    bool MethodsAreHidden() const { return methodsAreHidden_; }

    // The C# `public IMember NonMethod` (mutable). Returns a non-const ref so `AddMembers` can replace
    // it (`lookupGroup.NonMethod = member`).
    const ILSpy::Decompiler::TypeSystem::IMember*& NonMethod() { return nonMethod_; }
    const ILSpy::Decompiler::TypeSystem::IMember* NonMethod() const { return nonMethod_; }

    // The C# `public bool NonMethodIsHidden` (mutable). Returns a non-const ref.
    bool& NonMethodIsHidden() { return nonMethodIsHidden_; }
    bool NonMethodIsHidden() const { return nonMethodIsHidden_; }

    // The C# `public bool AllHidden { get; }`:
    //   if (NestedTypes != null && NestedTypes.Count > 0) return false;
    //   return NonMethodIsHidden && MethodsAreHidden;
    // The port's `NestedTypes` is never null (an empty vector is the C# `null`/empty), so
    // `NestedTypes != null && .Count > 0` maps to `!nestedTypes_.empty()`.
    bool AllHidden() const {
        if (!nestedTypes_.empty()) return false;
        return nonMethodIsHidden_ && methodsAreHidden_;
    }

private:
    const ILSpy::Decompiler::TypeSystem::IType* declaringType_;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> nestedTypes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*> methods_;
    const ILSpy::Decompiler::TypeSystem::IMember* nonMethod_;
    bool methodsAreHidden_;
    bool nonMethodIsHidden_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
