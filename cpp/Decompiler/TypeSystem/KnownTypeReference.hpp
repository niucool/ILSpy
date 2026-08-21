// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `KnownTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/KnownTypeReference.cs -- the `sealed class
// KnownTypeReference : ITypeReference` that represents a reference to one of the
// framework "known types" (the primitives, System.Object, System.String,
// System.Void, the collection interfaces, ...). A `KnownTypeReference` resolves
// itself to an `IType` against an `ITypeResolveContext` via
// `context.Compilation.FindType(knownTypeCode)`. `TypeSystemAstBuilder` reaches it
// through `KnownTypeReference.Get` / `GetCSharpNameByTypeCode` to look up the
// metadata name (and the C# primitive keyword) of a known type.
//
// This is the first concrete `ITypeReference` (the D408 base); it is unblocked on
// the base, the `ITypeResolveContext` parameter (D409), the default
// `SimpleTypeResolveContext` (D410) the resolution paths construct, and
// `ICompilation.FindType` (D399). It is a leaf TypeSystem dependency toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`).
//
// KEY PORT CONVENTIONS:
// (a) The C# `sealed class KnownTypeReference : ITypeReference` ports to a C++
// `final` concrete class deriving from `ITypeReference` (D408). It carries the
// six fields from the C# (`knownTypeCode` / `namespaceName` / `name` /
// `typeParameterCount` / `baseType` / `typeKind`); the four string/code scalars
// use `std::string_view` (pointing into compile-time string literals) matching the
// D271 `KnownTypeReferenceEntry` table-row convention, so a `KnownTypeReference`
// is trivially copyable and the static table is cheap to build.
// (b) The C# `private KnownTypeReference(...)` ctor (called only by the static
// table initializer) ports to a PRIVATE ctor; the static table is built by a
// private static member function `Table()` (a member can access the private
// ctor, the C# static-initializer-can-access-private-members counterpart). The
// ctor body applies the C# `if (typeKind == Struct && baseType == Object)
// baseType = ValueType` adjustment (structs implicitly derive from
// `System.ValueType`, not `System.Object`).
// (c) The C# `static readonly KnownTypeReference?[] knownTypeReferences` table
// (runtime-initialized, 60 slots, `null` at `None`) ports to a function-local
// `static const std::array<KnownTypeReference, 60>` (the Meyers-singleton
// pattern, the `KnownAttributeTypeNames` D378 / `StringComparer::Ordinal` D398
// precedent) built at first use and thread-safe-initialized under C++11+. The
// class is polymorphic (it derives from `ITypeReference`, which has a virtual
// destructor), so it is NOT a literal type and the table cannot be `constexpr`
// -- the function-local static is the faithful counterpart of the C#
// `static readonly` runtime-initialized field. The C++ enum is sequential
// (`String == 17`, no gap, matching the D271 `KnownTypeCode` minimal port), so
// the table has 60 entries indexed 0..59 with no gap and `None` at index 0
// (distinct from the C# array which has a `null` gap at the unused value 17);
// `Get` returns `nullptr` for `None` (the C# `null` at index 0).
// (d) The accessor names follow the established C++ minimal-port convention
// rather than the C# property names: `Code()` (the C# `KnownTypeCode` property)
// matches `KnownType::Code()` in `IType.hpp`, and `Kind()` (the C# `internal
// TypeKind typeKind` field) matches `KnownType::Kind()`. This avoids the
// member-function-named-after-its-enum-return-type name-hiding crux (the D372
// `SymbolKind` precedent) that a `KnownTypeCode()` accessor would trigger
// (the C# `KnownTypeCode` property collides with the `KnownTypeCode` enum in
// C++ but not in C#). `BaseType()` (the C# `internal KnownTypeCode baseType`
// field) returns `KnownTypeCode` with no collision (no member function named
// `KnownTypeCode`).
// (e) The C# `IType Resolve(ITypeResolveContext context)` (non-null, returns
// `context.Compilation.FindType(knownTypeCode)`) ports to a `const` override
// `const IType& Resolve(const ITypeResolveContext&) const` (the D408
// non-null-reference + const-read precedent); the body is out-of-line in the
// `.cpp` (needs `ITypeResolveContext` + `ICompilation` complete to call
// `Compilation().FindType(...)`).
// (f) The C# `TopLevelTypeName TypeName => new TopLevelTypeName(namespaceName,
// name, typeParameterCount)` ports to `TopLevelTypeName TypeName() const`
// returning by value (the `TopLevelTypeName` owns its strings); out-of-line to
// keep `<string>` out of this header.
// (g) The C# `static string? GetCSharpNameByTypeCode(KnownTypeCode)` (the 16-case
// primitive-keyword switch, `null` for non-primitives) ports to
// `static std::optional<std::string_view> GetCSharpNameByTypeCode(KnownTypeCode)`
// -- `std::optional<std::string_view>` is the faithful `string?` representation
// (a view into a compile-time literal, or `std::nullopt`), with no allocation.
// (h) The C# `IEnumerable<KnownTypeReference> AllKnownTypes` (yielding every
// non-`null` table entry) ports to `static std::vector<const
// KnownTypeReference*> AllKnownTypes()` (a snapshot of the 59 non-`None`
// entries, the `GetAttributes` / `ExplicitlyImplementedInterfaceMembers`
// non-owning-snapshot precedent).

#pragma once

#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// The number of `KnownTypeCode` values (None..Range), the C#
// `KnownTypeCodeCount = (int)KnownTypeCode.Range + 1`. The static table has one
// entry per code (index 0 is the `None` sentinel, never returned by `Get`).
constexpr std::size_t KnownTypeCodeCount =
    static_cast<std::size_t>(KnownTypeCode::Range) + 1;

// A reference to one of the framework "known types", resolved against an
// `ITypeResolveContext` via `context.Compilation.FindType(KnownTypeCode)`. A
// `KnownTypeReference` is the C# `sealed class` counterpart; it is `final` here.
// Instances are obtained from the static table via `Get` / `AllKnownTypes`
// (the private ctor is for the table only).
class KnownTypeReference final : public ITypeReference {
public:
    // The C# `static KnownTypeReference? Get(KnownTypeCode typeCode)` -- the
    // table entry for `typeCode`, or `nullptr` for `KnownTypeCode.None` (the C#
    // `null` at index 0).
    static const KnownTypeReference* Get(KnownTypeCode typeCode);

    // The C# `static IEnumerable<KnownTypeReference> AllKnownTypes` -- every
    // non-`None` table entry (the 59 known types), as a non-owning pointer
    // snapshot.
    static std::vector<const KnownTypeReference*> AllKnownTypes();

    // The C# `static string? GetCSharpNameByTypeCode(KnownTypeCode)` -- the C#
    // primitive keyword for a primitive known type (`object` / `bool` / `int`
    // / ...), or `std::nullopt` for a non-primitive (the C# `null`).
    static std::optional<std::string_view> GetCSharpNameByTypeCode(
        KnownTypeCode knownTypeCode);

    // The C# `KnownTypeCode KnownTypeCode { get; }` -- the known-type code.
    // Named `Code()` to match the `KnownType::Code()` minimal-port convention
    // (avoids the member-named-after-enum name-hiding crux).
    KnownTypeCode Code() const noexcept { return knownTypeCode_; }

    // The C# `internal TypeKind typeKind` -- the type kind (Class / Struct /
    // Interface / Void). Named `Kind()` to match `KnownType::Kind()`.
    TypeKind Kind() const noexcept { return typeKind_; }

    // The C# `string Namespace { get; }` -- the metadata namespace
    // (e.g. "System", "System.Collections.Generic").
    std::string_view Namespace() const noexcept { return namespaceName_; }

    // The C# `string Name { get; }` -- the metadata name (e.g. "Object",
    // "IEnumerable").
    std::string_view Name() const noexcept { return name_; }

    // The C# `int TypeParameterCount { get; }` -- the arity (0 for non-generic,
    // 1 for `IEnumerable<T>`, ...).
    int TypeParameterCount() const noexcept { return typeParameterCount_; }

    // The C# `internal KnownTypeCode baseType` -- the known-type code of the
    // direct base type (e.g. `ValueType` for a struct, `Delegate` for
    // `MulticastDelegate`, `Task` for `Task<T>`). The struct->`ValueType`
    // adjustment is applied by the ctor.
    KnownTypeCode BaseType() const noexcept { return baseType_; }

    // The C# `TopLevelTypeName TypeName => new TopLevelTypeName(namespaceName,
    // name, typeParameterCount)` -- the full top-level type name. Returns by
    // value (the `TopLevelTypeName` owns its strings); out-of-line.
    TopLevelTypeName TypeName() const;

    // The C# `IType Resolve(ITypeResolveContext context)` -- resolves this
    // reference to the `IType` the compilation holds for `knownTypeCode`. Never
    // null (the C# doc comment); a non-null reference return. `const` because
    // `Resolve` reads the context without modifying `this`.
    const IType& Resolve(const ITypeResolveContext& context) const override;

    // The C# `override string ToString()` -- the C# primitive keyword for a
    // primitive, otherwise `Namespace + "." + Name` (the metadata full name).
    std::string ToString() const;

private:
    // The C# `private KnownTypeReference(...)` ctor (table-only). Applies the
    // struct->`ValueType` base-type adjustment.
    KnownTypeReference(KnownTypeCode knownTypeCode, TypeKind typeKind,
                       std::string_view namespaceName, std::string_view name,
                       int typeParameterCount = 0,
                       KnownTypeCode baseType = KnownTypeCode::Object);

    // The C# `static readonly KnownTypeReference?[] knownTypeReferences` table
    // (60 slots, indexed by `KnownTypeCode`). A function-local `static const`
    // array (Meyers singleton) built at first use; index 0 (`None`) is a
    // sentinel that `Get` never returns.
    static const std::array<KnownTypeReference, KnownTypeCodeCount>& Table();

    KnownTypeCode knownTypeCode_;
    std::string_view namespaceName_;
    std::string_view name_;
    int typeParameterCount_;
    KnownTypeCode baseType_;
    TypeKind typeKind_;
};

} // namespace ILSpy::Decompiler::TypeSystem
