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

// The out-of-line `IntersectionType` members: the `Create` factory and the
// member-enumeration overrides (the latter live here because they call the
// `GetMembersHelper` free functions, whose header stays out of `IntersectionType.hpp`
// to keep that header's include graph minimal -- the `ParameterizedType`
// family-override precedent in `IType.cpp`).

#include "Decompiler/TypeSystem/IntersectionType.hpp"

#include "Decompiler/TypeSystem/Implementation/GetMembersHelper.hpp"

#include <functional>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// `(options & IgnoreInheritedMembers) == IgnoreInheritedMembers` -- the re-entry landmine
// guard (finding (a) in the IntersectionType.hpp header comment): a call bearing this flag
// would, in the C#, re-enter `GetMembersHelper`'s `*Impl` bodies' `baseType.Get*(filter,
// options | declaredMembers)` callback unconditionally -- an infinite recursion
// (`StackOverflowException`). The port returns the empty set: the semantically-faithful
// DECLARED-members answer (the intersection declares no members of its own) that the C#
// control flow reduces to if the recursion terminates.
inline bool ignoringInherited(GetMemberOptions options) {
    return (static_cast<std::int32_t>(options) &
            static_cast<std::int32_t>(GetMemberOptions::IgnoreInheritedMembers)) != 0;
}

// The C# `static Predicate<T> FilterNonStatic<T>(Predicate<T> filter) where T : class,
// IMember`: compose `!member.IsStatic` with the caller's filter (a null filter is "no
// filter" -- passes everything except static members).
template <typename T>
std::function<bool(const T*)> FilterNonStatic(std::function<bool(const T*)> filter) {
    if (!filter)
        return [](const T* m) { return !m->IsStatic(); };
    return [filter](const T* m) { return !m->IsStatic() && filter(m); };
}

// Convert the helper's OWNING `std::vector<std::shared_ptr<const T>>` snapshots to the
// non-owning `const T*` snapshots the `IType` family virtuals promise (the D477
// "type system owns the entities" convention; the `ParameterizedType` routing-arm
// conversion).
template <typename T>
std::vector<const T*> ToNonOwning(const std::vector<std::shared_ptr<const T>>& owned) {
    std::vector<const T*> result;
    result.reserve(owned.size());
    for (const auto& m : owned)
        result.push_back(m.get());
    return result;
}

} // namespace

// The C# `public static IType Create(IEnumerable<IType> types)`.
ITypePtr IntersectionType::Create(const std::vector<ITypePtr>& types)
{
    // The C# throws `ArgumentNullException` on a null entry; the port throws
    // `std::invalid_argument` (the `TokenWriter::Create` factory precedent). The C#
    // dedups first and checks nulls second; the port checks FIRST so the dedup never
    // dereferences a null `shared_ptr` (the C# `Distinct` is null-aware). The reorder is
    // observationally identical: the throw is unconditional whenever a null is present.
    for (const auto& t : types) {
        if (!t)
            throw std::invalid_argument("IntersectionType.Create: null type");
    }
    // The C# `types.Distinct()`: dedup under `IType::Equals` (the
    // `EqualityComparer<IType>.Default` -> `IEquatable<IType>.Equals` route), keeping the
    // first occurrence.
    std::vector<ITypePtr> arr;
    arr.reserve(types.size());
    for (const auto& t : types) {
        bool alreadyPresent = false;
        for (const auto& existing : arr) {
            if (existing->Equals(*t)) {
                alreadyPresent = true;
                break;
            }
        }
        if (!alreadyPresent)
            arr.push_back(t);
    }
    if (arr.empty())
        return UnknownType();
    if (arr.size() == 1)
        return arr[0];
    // The `shared_ptr(new ...)` form (not `make_shared`): the private ctor is only
    // callable from this static member (the C# private-ctor + factory pattern), and
    // `make_shared` cannot construct through a private ctor from outside the class.
    return ITypePtr(new IntersectionType(std::move(arr)));
}

std::string IntersectionType::Name() const
{
    std::string result;
    for (const auto& t : types_) {
        if (!result.empty())
            result += " & ";
        result += t->Name();
    }
    return result;
}

std::string IntersectionType::ReflectionName() const
{
    std::string result;
    for (const auto& t : types_) {
        if (!result.empty())
            result += " & ";
        result += t->ReflectionName();
    }
    return result;
}

std::optional<bool> IntersectionType::IsReferenceType() const
{
    // The C# returns the FIRST constituent's definite value; an indeterminate
    // constituent is skipped; none definite yields `null` (nullopt).
    for (const auto& t : types_) {
        std::optional<bool> isReferenceType = t->IsReferenceType();
        if (isReferenceType.has_value())
            return isReferenceType;
    }
    return std::nullopt;
}

std::vector<ITypePtr> IntersectionType::DirectBaseTypes() const
{
    return types_;
}

bool IntersectionType::StructuralEquals(const IType& other) const
{
    const auto& o = static_cast<const IntersectionType&>(other);
    if (types_.size() != o.types_.size())
        return false;
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (!types_[i]->Equals(*o.types_[i]))
            return false;
    }
    return true;
}

// ---- The member-enumeration overrides ----
// Each routes through `GetMembersHelper` exactly as the C# does (`GetMembersHelper.Get*(this,
// FilterNonStatic(filter), options)`), which (with `IgnoreInheritedMembers` unset -- the guard
// below) enumerates the NON-INTERFACE base types (the constituents, via
// `GetNonInterfaceBaseTypes` -> `DirectBaseTypes`) and aggregates each one's members. An
// `IgnoreInheritedMembers`-bearing call returns the empty set (the re-entry landmine guard).

std::vector<const IMethod*> IntersectionType::GetMethods(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetMethods(
        this, FilterNonStatic(filter), options));
}

std::vector<const IMethod*> IntersectionType::GetMethods(
    const std::vector<ITypePtr>& typeArguments,
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetMethods(
        this, &typeArguments, FilterNonStatic(filter), options));
}

std::vector<const IProperty*> IntersectionType::GetProperties(
    std::function<bool(const IProperty*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetProperties(
        this, FilterNonStatic(filter), options));
}

std::vector<const IField*> IntersectionType::GetFields(
    std::function<bool(const IField*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetFields(
        this, FilterNonStatic(filter), options));
}

std::vector<const IEvent*> IntersectionType::GetEvents(
    std::function<bool(const IEvent*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetEvents(
        this, FilterNonStatic(filter), options));
}

std::vector<const IMember*> IntersectionType::GetMembers(
    std::function<bool(const IMember*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetMembers(
        this, FilterNonStatic(filter), options));
}

std::vector<const IMethod*> IntersectionType::GetAccessors(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const
{
    if (ignoringInherited(options))
        return {};
    return ToNonOwning(Implementation::GetMembersHelper::GetAccessors(
        this, FilterNonStatic(filter), options));
}

} // namespace ILSpy::Decompiler::TypeSystem
