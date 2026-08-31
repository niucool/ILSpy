// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software") to deal in the
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
// PURPOSE NONINFRINGEMENT. CAUSED BY ON THE WHICHEVER THEORY OF LIABILITY, WHETHER IN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IntersectionType.cs -- the intersection of
// several types (the synthetic type the `TypeInference` `ImprovedReturnAllResults` fixing
// algorithm produces for a type parameter fixed to multiple candidates: "a value that is
// all of these types at once"). The class is a leaf of the `AbstractType` hierarchy; the
// minimal port has no `AbstractType` (flattened onto `IType`, the D406 convention), so it
// derives from `IType` directly (the `TupleType` / `KnownType` precedent).
//
// The private ctor + `Create` factory pair is faithful to the C#: `Create` dedups the
// input under `IType::Equals` (the C# `Distinct()` with `EqualityComparer<IType>.Default`,
// which calls `IEquatable<IType>.Equals` -- the `TP` bounds containers'
// `HashSet<IType>.Add`-under-`IType.Equals` analogue), maps an empty list to
// `SpecialType.UnknownType`, maps a singleton to the single type itself, and otherwise
// builds the intersection.
//
// KEY C#-vs-C++ FINDINGS (the load-bearing cruxes the tests pin):
//  (a) THE `GetMembersHelper` RE-ENTRY LANDMINE: the C# `GetMembersHelper` header
//      explicitly warns "GetMembersHelper will recursively call back into
//      IType.GetMembers(), but only with both IgnoreInheritedMembers and
//      ReturnMemberDefinitions set ... Ensure that your IType implementation does not
//      use the GetMembersHelper if both flags are set, otherwise you'll get a
//      StackOverflowException!". The C# `IntersectionType` violates that warning: every
//      member-family override unconditionally routes through `GetMembersHelper`, so a
//      call with `IgnoreInheritedMembers` set re-enters `GetMethodsImpl`'s
//      `baseType.GetMethods(filter, options | declaredMembers)` callback, which re-enters
//      the override with the same flags -- an unconditional infinite recursion
//      (`StackOverflowException` in C#, a process-fatal stack overflow in C++). The port
//      adds the documented safe fallback: an `IgnoreInheritedMembers`-bearing call
//      returns the EMPTY set (the D516 null-guard / documented-safe-fallback convention
//      for a C# crash shape). The empty set is also the semantically-faithful answer:
//      the helper's re-entry asks the type for its DECLARED members, an intersection
//      type declares none of its own (its members flow from its constituents, which the
//      non-ignoring path enumerates), and the C# control flow would reduce to exactly
//      that empty set if the recursion terminated.
//  (b) The C# `Create` dedups FIRST and throws on a null entry SECOND; the port checks
//      the nulls first (the dedup would dereference a null `shared_ptr` -- UB, the C#
//      `Distinct` handles nulls because `EqualityComparer<IType>.Default` is null-aware).
//      The reorder is observationally identical: the throw is unconditional whenever a
//      null is present, so no input reaches the dedup outcome either way.
//  (c) The C# `ArgumentNullException` on a null entry ports to `std::invalid_argument`
//      (the `TokenWriter::Create` / `TextWriterTokenWriter` factory precedent).
//  (d) The C# `GetHashCode` override participates only in .NET hashtable lookups; the
//      port's `IType` carries no hash surface (hashes live on the interning reference
//      classes), so it is omitted with this note rather than inventing one.
//  (e) `TypeParameterCount` is NOT overridden in the C# (the `AbstractType` default 0);
//      the port's `IType` declares it pure-virtual (no flattened default), so the
//      override returns the faithful `0`.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class IntersectionType : public IType {
public:
    // The C# `public static IType Create(IEnumerable<IType> types)` -- the factory the
    // `TypeInference` fixing algorithm calls. Dedups under `IType::Equals` (keeps the
    // FIRST occurrence, the C# `Distinct()` semantics), throws `std::invalid_argument`
    // on a null entry (the C# `ArgumentNullException`), maps empty to `UnknownType()` and
    // a singleton to the type itself, else builds the intersection.
    static ITypePtr Create(const std::vector<ITypePtr>& types);

    TypeKind Kind() const override { return TypeKind::Intersection; }

    // The C# `Name` / `ReflectionName`: the constituent names joined with " & ".
    std::string Name() const override;
    std::string ReflectionName() const override;

    // The C# `AbstractType` default (IntersectionType does not override it).
    int TypeParameterCount() const override { return 0; }

    // The C# `ReadOnlyCollection<IType> Types` property (the ctor-asserted >= 2
    // constituents, deduped).
    const std::vector<ITypePtr>& Types() const noexcept { return types_; }

    // The C# `bool? IsReferenceType`: the FIRST constituent with a definite value wins
    // (an indeterminate constituent is skipped); all-indeterminate yields `nullopt`.
    std::optional<bool> IsReferenceType() const override;

    // The C# `IEnumerable<IType> DirectBaseTypes => types` -- the constituents ARE the
    // direct base types (everything a value of all these types is, is one of each).
    std::vector<ITypePtr> DirectBaseTypes() const override;

    // The member-enumeration overrides route through `GetMembersHelper` over the
    // non-interface base types (the constituents), with the non-static filter composed
    // into the caller's filter (`FilterNonStatic`: the C# composes `!member.IsStatic`).
    // The `IgnoreInheritedMembers`-bearing shapes return the empty set -- the documented
    // safe fallback for the C# re-entry landmine (finding (a) above). Defined in the
    // .cpp (the `GetMembersHelper.hpp` include lives there to keep this header's include
    // graph minimal, the `ParameterizedType` family-override precedent).
    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;
    std::vector<const IMethod*> GetMethods(
        const std::vector<ITypePtr>& typeArguments,
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;
    std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;
    std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;
    std::vector<const IMember*> GetMembers(
        std::function<bool(const IMember*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;
    std::vector<const IMethod*> GetAccessors(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override;

protected:
    // The C# `Equals(IType)`: same count + element-wise `Equals` (the `IType::Equals`
    // Kind guard has already matched `TypeKind::Intersection`, the `static_cast`
    // `TupleType` / `KnownType` precedent).
    bool StructuralEquals(const IType& other) const override;

private:
    // The C# private ctor (`Debug.Assert(types.Length >= 2)`).
    explicit IntersectionType(std::vector<ITypePtr> types) : types_(std::move(types)) {
        assert(types_.size() >= 2);
    }

    std::vector<ITypePtr> types_;
};

} // namespace ILSpy::Decompiler::TypeSystem
