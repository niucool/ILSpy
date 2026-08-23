// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Minimal port of the ICSharpCode.Decompiler.TypeSystem.IType hierarchy, enough
// for Phase 1/2 signature decoding. IType is a polymorphic base owned via
// shared_ptr (the C# uses reference semantics; types are shared and cached by
// the type system). Concrete kinds: SimpleType (a top-level or known type
// reference), ParameterizedType, ArrayType, ByReferenceType, PointerType,
// TypeParameter, and SpecialType. The full IType surface (Nullability, member
// access, AcceptVisitor, ...) lands with the rest of Phase 2.

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class IType;
class ITypeDefinition;
class IEvent;
class IField;
class IMember;
class IMethod;
class IProperty;
class TypeParameterSubstitution;
class TypeVisitor;
using ITypePtr = std::shared_ptr<IType>;

// The C# `[Flags] enum GetMemberOptions` (IType.cs) -- the bitmask selecting which
// members the member-enumeration functions below return / whether type substitution
// is performed. An `int`-backed `enum class` (the C# has no underlying-type
// annotation). The `|`/`&` bitwise operators the C# `[Flags]` enum has implicitly
// are defined as free functions here (the C++ `enum class` has none), the
// D376 `TypeSystemOptions` / D468 `OverloadResolutionErrors` precedent.
enum class GetMemberOptions : std::int32_t {
	// No options specified (the default): members are specialized, inherited members included.
	None = 0x00,
	// Do not specialize the returned members; return the (generic) member definitions directly.
	ReturnMemberDefinitions = 0x01,
	// Do not list inherited members; only members defined directly on this type.
	IgnoreInheritedMembers = 0x02,
};
inline constexpr GetMemberOptions operator|(GetMemberOptions a, GetMemberOptions b) {
	return static_cast<GetMemberOptions>(
		static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline constexpr GetMemberOptions operator&(GetMemberOptions a, GetMemberOptions b) {
	return static_cast<GetMemberOptions>(
		static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline constexpr GetMemberOptions operator~(GetMemberOptions a) {
	return static_cast<GetMemberOptions>(~static_cast<std::int32_t>(a));
}

// The root of the type representation. Equality is structural: two ITypes are
// equal iff they have the same Kind and the same constituent names/types. IType
// derives from std::enable_shared_from_this so that VisitChildren can hand back
// a shared_ptr to itself when no child changed (the C# `return this` reference-
// identity semantics); the types are shared_ptr-owned throughout the port (the
// signature decoder / IL reader / type system all construct them via make_shared),
// so shared_from_this is valid whenever a Visit is in progress.
class IType : public std::enable_shared_from_this<IType> {
public:
    virtual ~IType() = default;
    virtual TypeKind Kind() const = 0;
    // The short name of the type (e.g. "List" or "Int32"); for special types
    // this is a diagnostic label.
    virtual std::string Name() const = 0;
    // The reflection-name form, e.g. "System.Collections.Generic.List`1" or
    // "System.Int32". Used for tests and diagnostics; the full round-trip
    // name handling lives in FullTypeName for type definitions.
    virtual std::string ReflectionName() const = 0;
    virtual int TypeParameterCount() const = 0;

    // Whether the type is a reference type or value type (faithful port of
    // IType.cs `bool? IsReferenceType`): `std::optional<bool>(true)` = reference
    // type, `std::optional<bool>(false)` = value type, `std::nullopt` = not known
    // (e.g. an unconstrained type parameter or a not-yet-resolved name reference).
    // The C# interface declares this `abstract` (no AbstractType default); the
    // minimal port makes it virtual-WITH-DEFAULT `std::nullopt` (the D406
    // flattened-AbstractType convention) to avoid the D370 big-bang churn a
    // pure-virtual would force on every IType subclass and test stub. The
    // concrete types with a clear C# value override it: ArrayType -> true,
    // FunctionPointerType -> false, the delegating decorators ModifiedType /
    // ParameterizedType / TupleType / NullabilityAnnotatedType forward to their
    // element/generic/underlying/base type, and the stored-field SpecialType /
    // UnknownType return their ctor-supplied bool?. ByReferenceType / PointerType
    // inherit the `std::nullopt` default, faithful to the C# `return null`.
    virtual std::optional<bool> IsReferenceType() const { return std::nullopt; }

    // The C# `Nullability Nullability { get; }` -- the nullability annotation carried by
    // the type itself (`Oblivious` when the type carries no annotation). The C# interface
    // declares it `abstract` and `AbstractType` supplies the `Nullability.Oblivious`
    // default; the port flattens that default here (the D406 convention), and only the
    // `NullabilityAnnotatedType` decorator overrides it. The return type and every later
    // `Nullability` mention in this class are namespace-qualified: the member name
    // shadows the enum type in MSVC's complete-class lookup (the D372/D475 crux).
    virtual ::ILSpy::Decompiler::TypeSystem::Nullability Nullability() const {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }

    // The C# `IType ChangeNullability(Nullability newNullability)` -- returns this type
    // annotated with the requested nullability. `AbstractType`'s default ignores the
    // change and returns `this` (only some types support nullability) -- flattened onto
    // `IType` here per the D406 convention, as a virtual-WITH-DEFAULT returning
    // `shared_from_this()` (the C# `return this` reference identity). The concrete
    // overrides mirror the C# override set the port's concrete types carry:
    // `NullabilityAnnotatedType` (same annotation -> this, else forward to the unwrapped
    // base), `SpecialType` (only `Dynamic` annotates), `ParameterizedType` and
    // `ModifiedType` (rebuild when the substituted child changed), and the `UnknownType`
    // class (non-Oblivious reference-types wrap in `NullabilityAnnotatedType`). The
    // port's `ArrayType` carries no nullability field (the C# ctor's `nullability`
    // argument is deferred), so it inherits the default -- a documented divergence for
    // the non-Oblivious arm. NON-CONST (like `AcceptVisitor` / `VisitChildren`): it may
    // return `shared_from_this()`.
    virtual ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
        (void)nullability;
        return shared_from_this();
    }

    // Gets the underlying type definition (faithful port of IType.cs `ITypeDefinition?
    // GetDefinition()`). Can return null for types which do not have a type definition (for
    // example arrays, pointers, type parameters, the C++-only minimal KnownType / SimpleType
    // / SpecialType). The C# interface declares this `abstract` (no AbstractType default); the
    // minimal port makes it virtual-WITH-DEFAULT `nullptr` (the D406 flattened-AbstractType
    // convention, mirroring the C# AbstractType.GetDefinition() `return null`) so the C++-only
    // minimal types and the not-yet-concrete interfaces inherit it without a big-bang churn.
    // The delegating decorators (ModifiedType / NullabilityAnnotatedType / ParameterizedType /
    // TupleType) override it to forward to their element / generic / underlying / base type
    // (the C# DecoratedType / ModifiedType / ParameterizedType / TupleType overrides);
    // FunctionPointerType inherits the `nullptr` default -- the faithful common case (the C#
    // override returns null when TypeSystemOptions.FunctionPointers is enabled, which the
    // minimal port assumes unconditionally per the D404 module-field deferral; the UIntPtr-alias
    // fallback stays deferred with the module field). The real MetadataTypeDefinition that
    // production `ICompilation.FindType` returns overrides this to return `this` (it IS an
    // ITypeDefinition); the minimal KnownType is NOT an ITypeDefinition and inherits `nullptr`.
    virtual const ITypeDefinition* GetDefinition() const { return nullptr; }

    // The TypeVisitor dispatch (faithful port of IType.cs AcceptVisitor /
    // VisitChildren). The C# interface declares these abstract and AbstractType
    // provides the defaults (VisitOtherType for the no-dedicated-Visit-method
    // types, `return this` for the no-children types); the minimal port has no
    // AbstractType (flattened onto IType), so the defaults live here. Concrete
    // types with a dedicated Visit* method or with children override these
    // (see ParameterizedType / ArrayType / ByReferenceType / PointerType /
    // TupleType / ModifiedType / NullabilityAnnotatedType / FunctionPointerType);
    // the C++-only minimal types (KnownType / SimpleType / SpecialType /
    // TypeParameter) and the not-yet-concrete interfaces (ITypeDefinition /
    // ITypeParameter) inherit the defaults. AcceptVisitor is out-of-line (it
    // calls TypeVisitor::VisitOtherType, which needs TypeVisitor complete).
    virtual ITypePtr AcceptVisitor(TypeVisitor& visitor);
    // The default reconstructs nothing (no children) and returns this; the
    // shared_ptr identity is the C# `return this` reference identity.
    virtual ITypePtr VisitChildren(TypeVisitor& visitor) { return shared_from_this(); }

    // ---- The member-enumeration surface (IType.cs `GetNestedTypes`/`GetConstructors`/
    // `GetMethods`/`GetProperties`/`GetFields`/`GetEvents`/`GetMembers`/`GetAccessors`) ----
    // The C# `interface IType` declares these `abstract`; the C#
    // `Implementation.AbstractType` supplies the defaults (empty for the specific
    // families; `GetMembers` composes `GetMethods.Concat(GetProperties).Concat(GetFields)
    // .Concat(GetEvents)`), and the concrete `IType` implementations route them through
    // `Implementation.GetMembersHelper` (apply the filter + `GetMemberOptions` flags +
    // specialization). This minimal port has no `AbstractType` (the D406 flattened
    // convention), so the `AbstractType` DEFAULTS land here as `virtual`-with-defaults:
    // the specific families default to an empty snapshot, and `GetMembers` defaults to the
    // virtual composition of the four families (so a derived type that overrides only the
    // families sees them aggregated, faithful to the C#). The concrete routing / the
    // specialization machinery (`GetMembersHelper`, `SpecializedMethod`/`SpecializedProperty`/
    // `SpecializedField`/`SpecializedEvent`) is deferred to the next leaf; a real consumer
    // (the `MemberLookup` Lookup region's `type.GetMembers(...)` / `type.GetNestedTypes(...)`)
    // calls these through the `IType&`. The C# `IEnumerable` deferred sequences port to
    // by-value `std::vector<const T*>` snapshots of non-owning pointers (the D271
    // `IParameterizedMember::Parameters` precedent -- the members are owned by the type
    // system / the concrete type definition). The C# `Delegate<Predicate>` filters port to
    // `std::function<bool(const T*)>` AFTER the default (C++ has no `[Nullable]` annotated
    // reference; the filter's by-value position + a default `nullptr` is the faithful
    // "no filter" sentinel the C# `= null` models), and the C# `GetMemberOptions` default
    // values port verbatim.

    // The C# `IEnumerable<IType> GetNestedTypes(Delegate<Predicate{ITypeDefinition}> filter,
    // GetMemberOptions options = None)` -- the inner classes (including inherited inner
    // classes) un-type-argument-constrained. Defaults to the C# `AbstractType` empty list.
    virtual std::vector<ITypePtr> GetNestedTypes(
        std::function<bool(const ITypeDefinition*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IType> GetNestedTypes(IReadOnlyList<IType> typeArguments,
    // Delegate<Predicate{ITypeDefinition}> filter, GetMemberOptions options = None)` -- the
    // inner classes that have `typeArguments.size()` additional type parameters. Defaults to
    // the C# `AbstractType` empty list.
    virtual std::vector<ITypePtr> GetNestedTypes(
        const std::vector<ITypePtr>& typeArguments,
        std::function<bool(const ITypeDefinition*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)typeArguments; (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IMethod> GetConstructors(Delegate<Predicate{IMethod}> filter,
    // GetMemberOptions options = IgnoreInheritedMembers)` -- the instance constructors
    // (NOT static constructors; base-class constructors are not returned by default). Defaults
    // to the C# `AbstractType` empty list.
    virtual std::vector<const IMethod*> GetConstructors(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::IgnoreInheritedMembers) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IMethod> GetMethods(Delegate<Predicate{IMethod}> filter,
    // GetMemberOptions options = None)` -- all methods callable on this type (not ctors or
    // accessors). Defaults to the C# `AbstractType` empty list.
    virtual std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IMethod> GetMethods(IReadOnlyList<IType> typeArguments,
    // Delegate<Predicate{IMethod}> filter, GetMemberOptions options = None)` -- the generic
    // methods callable with the specified type arguments. Defaults to the C# `AbstractType`
    // empty list.
    virtual std::vector<const IMethod*> GetMethods(
        const std::vector<ITypePtr>& typeArguments,
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)typeArguments; (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IProperty> GetProperties(Delegate<Predicate{IProperty}> filter,
    // GetMemberOptions options = None)` -- the properties callable on this type. Defaults to
    // the C# `AbstractType` empty list.
    virtual std::vector<const IProperty*> GetProperties(
        std::function<bool(const IProperty*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IField> GetFields(Delegate<Predicate{IField}> filter,
    // GetMemberOptions options = None)` -- the fields accessible on this type. Defaults to
    // the C# `AbstractType` empty list.
    virtual std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IEvent> GetEvents(Delegate<Predicate{IEvent}> filter,
    // GetMemberOptions options = None)` -- the events accessible on this type. Defaults to
    // the C# `AbstractType` empty list.
    virtual std::vector<const IEvent*> GetEvents(
        std::function<bool(const IEvent*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IMember> GetMembers(Delegate<Predicate{IMember}> filter,
    // GetMemberOptions options = None)` -- all members callable on this type (methods,
    // properties, fields, events; NOT ctors). The `AbstractType` default composes
    // `GetMethods.Concat(GetProperties).Concat(GetFields).Concat(GetEvents)`; the port
    // reproduces that composition over the four virtual families in `IType.cpp` (the
    // composed up-casts to `const IMember*` need the member-family headers complete, and
    // those headers include `IType.hpp` transitively -- a header-side composition would
    // cycle the includes), so a type that overrides only the families sees them
    // aggregated here, faithful to the C#.
    virtual std::vector<const IMember*> GetMembers(
        std::function<bool(const IMember*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const;

    // The C# `IEnumerable<IMethod> GetAccessors(Delegate<Predicate{IMethod}> filter,
    // GetMemberOptions options = None)` -- the accessors of the properties / events on this
    // type (not returned by `GetMembers` / `GetMethods`). Defaults to the C# `AbstractType`
    // empty list.
    virtual std::vector<const IMethod*> GetAccessors(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const { (void)filter; (void)options; return {}; }

    // The C# `IEnumerable<IType> DirectBaseTypes { get; }` -- the direct base types,
    // including interfaces (IType.cs). The C# interface declares it abstract and
    // AbstractType / the concrete types supply the list; the minimal port has no
    // AbstractType (flattened onto IType), so the default lands here as a
    // virtual-WITH-DEFAULT empty list (the GetDefinition / IsReferenceType precedent):
    // concrete types with real base types (the ported MinimalResolveContext types,
    // and later the MetadataTypeDefinition) override this; the C++-only minimal
    // types (KnownType / SimpleType / SpecialType / TypeParameter / ...) carry no
    // recorded base list, so they inherit the empty default. The returned types are
    // non-null shared_ptrs (the C# never yields a null IType); by-value snapshot,
    // mirroring the C# deferred enumeration's materialization point.
    virtual std::vector<ITypePtr> DirectBaseTypes() const { return {}; }

    // Structural equality; derived classes override StructuralEquals.
    bool Equals(const IType& other) const {
        if (Kind() != other.Kind()) return false;
        return StructuralEquals(other);
    }
protected:
    virtual bool StructuralEquals(const IType& other) const = 0;
};

// The `GetMembers` definition lands in `IType.cpp`: the faithful `AbstractType`
// composition `GetMethods.Concat(GetProperties).Concat(GetFields).Concat(GetEvents)`
// up-casts the family `const IMethod*` / `const IProperty*` / `const IField*` /
// `const IEvent*` elements to `const IMember*`, which needs the member-family headers
// (`IMethod.hpp` / `IProperty.hpp` / `IField.hpp` / `IEvent.hpp`) COMPLETE -- and those
// headers transitively include this one (via `IEntity.hpp` / `IVariable.hpp`), so the
// composition cannot live inline in this header without an include cycle. `IType.cpp`
// includes them and defines `IType::GetMembers`.

// A type that is known by code (a primitive or framework type). Cheap to
// construct and compare; the decompiler's type system looks these up by code
// rather than by name.
class KnownType : public IType {
public:
    explicit KnownType(KnownTypeCode code) : code_(code) {}
    TypeKind Kind() const override;
    std::string Name() const override;
    std::string ReflectionName() const override;
    int TypeParameterCount() const override;
    std::optional<bool> IsReferenceType() const override;
    KnownTypeCode Code() const noexcept { return code_; }
protected:
    bool StructuralEquals(const IType& other) const override {
        return code_ == static_cast<const KnownType&>(other).code_;
    }
private:
    KnownTypeCode code_;
};

// A reference to a top-level type definition by name (from a TypeDef or a
// TypeRef). The type system later resolves this to an ITypeDefinition; until
// then it carries just the name and kind.
class SimpleType : public IType {
public:
    explicit SimpleType(TopLevelTypeName name, TypeKind kind = TypeKind::Class)
        : name_(std::move(name)), kind_(kind) {}
    TypeKind Kind() const override { return kind_; }
    std::string Name() const override { return name_.Name(); }
    std::string ReflectionName() const override { return name_.ReflectionName(); }
    int TypeParameterCount() const override { return name_.TypeParameterCount(); }
    const TopLevelTypeName& GetTopLevelTypeName() const noexcept { return name_; }
protected:
    bool StructuralEquals(const IType& other) const override {
        return name_ == static_cast<const SimpleType&>(other).name_ &&
               kind_ == static_cast<const SimpleType&>(other).kind_;
    }
private:
    TopLevelTypeName name_;
    TypeKind kind_;
};

// A parameterized (generic) type: e.g. List<int>. The base type is the
// generic definition (a SimpleType for an unbound generic), with type args.
class ParameterizedType : public IType {
public:
    ParameterizedType(ITypePtr genericType, std::vector<ITypePtr> typeArgs)
        : genericType_(std::move(genericType)), typeArgs_(std::move(typeArgs)) {}
    TypeKind Kind() const override { return genericType_ ? genericType_->Kind() : TypeKind::Class; }
    std::string Name() const override;
    std::string ReflectionName() const override;
    int TypeParameterCount() const override { return static_cast<int>(typeArgs_.size()); }
    const ITypePtr& GenericType() const noexcept { return genericType_; }
    const std::vector<ITypePtr>& TypeArguments() const noexcept { return typeArgs_; }
    // Faithful port of ParameterizedType.cs `bool? IsReferenceType => genericType.IsReferenceType`
    // (delegates to the generic definition).
    std::optional<bool> IsReferenceType() const override {
        return genericType_ ? genericType_->IsReferenceType() : std::nullopt;
    }
    // Faithful port of ParameterizedType.cs `ITypeDefinition GetDefinition() => genericType.GetDefinition()`
    // (delegates to the generic definition; a type-argument substitution carries no definition
    // of its own -- `List<int>.GetDefinition()` is the `List` definition).
    const ITypeDefinition* GetDefinition() const override {
        return genericType_ ? genericType_->GetDefinition() : nullptr;
    }
    // Faithful port of ParameterizedType.cs AcceptVisitor / VisitChildren: dispatch
    // to VisitParameterizedType and reconstruct (genericType + type args) if any
    // child changed, else return this.
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
    // Faithful port of ParameterizedType.cs ChangeNullability: forwards to the
    // generic type and rebuilds only when it changed (defined in IType.cpp).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;

    // Faithful port of ParameterizedType.cs `IType GetTypeArgument(int index)` -- the
    // `index`-th type argument (the C# is literally `typeArguments[index]`). Returns the
    // stored `ITypePtr` BY VALUE (a shared_ptr copy, sharing the managed `IType` with
    // `TypeArguments()[index]`). The C# has no bounds check (an out-of-range index is the
    // caller's contract); the port mirrors that -- defined in IType.cpp alongside the
    // `GetSubstitution` pair (the class body keeps the declaration only). See the header
    // comment on `GetSubstitution` for the include-graph reason the trio is out-of-line.
    ITypePtr GetTypeArgument(int index) const;

    // Faithful port of ParameterizedType.cs `TypeParameterSubstitution GetSubstitution()` --
    // returns `new TypeParameterSubstitution(typeArguments, null)` (the substitution of
    // the generic type's CLASS type parameters with this parameterized type's type
    // arguments; no method type arguments). The port returns the substitution BY VALUE
    // (the C# heap allocation realized as a value, the D407 convention); the class type
    // arguments are a fresh `std::optional<std::vector<ITypePtr>>` holding copies of this
    // type's `typeArgs_` (the managed `IType` objects are shared via the shared_ptrs), and
    // the method list is `std::nullopt` (the C# `null`). The `GetMembersHelper` routing
    // and the `SpecializedMember` constructors bind against this surface.
    //
    // PLACEMENT / INCLUDE GRAPH: `TypeParameterSubstitution` is a concrete `TypeVisitor`
    // whose header includes `TypeVisitor.hpp`, which in turn includes THIS header
    // (`IType.hpp`) -- so `IType.hpp` cannot include `TypeParameterSubstitution.hpp` without
    // a cycle. The by-value return type is only FORWARD-DECLARED here (a member-function
    // declaration permits an incomplete return type); the DEFINITION is out-of-line in
    // `IType.cpp`, which includes `TypeParameterSubstitution.hpp` (the type is complete
    // there). The `GetTypeArgument` member is kept out-of-line too for a single grouping.
    TypeParameterSubstitution GetSubstitution() const;

    // Faithful port of ParameterizedType.cs `TypeParameterSubstitution GetSubstitution(
    // IReadOnlyList<IType> methodTypeArguments)` -- returns `new TypeParameterSubstitution(
    // typeArguments, methodTypeArguments)`: the class type parameters substituted with this
    // type's type arguments AND the method type parameters substituted with the supplied
    // method type arguments. The `methodTypeArguments` parameter is the nullable list (`std::nullopt`
    // = the C# `null`, "keep this kind of type parameter unmodified"); a present (possibly empty)
    // list substitutes the method type parameters. Returns the substitution BY VALUE (the D407
    // convention). Out-of-line in IType.cpp for the include-graph reason above.
    TypeParameterSubstitution GetSubstitution(std::optional<std::vector<ITypePtr>> methodTypeArguments) const;
protected:
    bool StructuralEquals(const IType& other) const override;
private:
    ITypePtr genericType_;
    std::vector<ITypePtr> typeArgs_;
};

// An array type. SZArray (single-dim, zero-lower-bound) is the common case;
// multi-dim arrays carry rank and (optional) sizes/lower bounds.
class ArrayType : public IType {
public:
    // SZArray constructor.
    explicit ArrayType(ITypePtr element) : element_(std::move(element)), rank_(1), isSzArray_(true) {}
    // Multi-dimensional array constructor.
    ArrayType(ITypePtr element, int rank) : element_(std::move(element)), rank_(rank), isSzArray_(false) {}
    TypeKind Kind() const override { return TypeKind::Array; }
    std::string Name() const override;
    std::string ReflectionName() const override;
    int TypeParameterCount() const override { return 0; }
    const ITypePtr& Element() const noexcept { return element_; }
    int Rank() const noexcept { return rank_; }
    bool IsSzArray() const noexcept { return isSzArray_; }
    // Faithful port of ArrayType.cs `bool? IsReferenceType => true` (an array is
    // always a reference type).
    std::optional<bool> IsReferenceType() const override { return std::optional<bool>(true); }
    // Faithful port of ArrayType.cs VisitChildren: reconstruct with the visited
    // element if it changed, else return this.
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
protected:
    bool StructuralEquals(const IType& other) const override;
private:
    ITypePtr element_;
    int rank_;
    bool isSzArray_;
};

class ByReferenceType : public IType {
public:
    explicit ByReferenceType(ITypePtr element) : element_(std::move(element)) {}
    TypeKind Kind() const override { return TypeKind::ByReference; }
    std::string Name() const override { return element_ ? element_->Name() : std::string(); }
    std::string ReflectionName() const override;
    int TypeParameterCount() const override { return element_ ? element_->TypeParameterCount() : 0; }
    const ITypePtr& Element() const noexcept { return element_; }
    // Faithful port of ByReferenceType.cs VisitChildren.
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
protected:
    bool StructuralEquals(const IType& other) const override {
        return element_->Equals(*static_cast<const ByReferenceType&>(other).element_);
    }
private:
    ITypePtr element_;
};

class PointerType : public IType {
public:
    explicit PointerType(ITypePtr element) : element_(std::move(element)) {}
    TypeKind Kind() const override { return TypeKind::Pointer; }
    std::string Name() const override { return element_ ? element_->Name() : std::string(); }
    std::string ReflectionName() const override;
    int TypeParameterCount() const override { return element_ ? element_->TypeParameterCount() : 0; }
    const ITypePtr& Element() const noexcept { return element_; }
    // Faithful port of PointerType.cs VisitChildren.
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
protected:
    bool StructuralEquals(const IType& other) const override {
        return element_->Equals(*static_cast<const PointerType&>(other).element_);
    }
private:
    ITypePtr element_;
};

// A generic type parameter. OwnerKind distinguishes class (Var) from method
// (MVar) parameters, matching ELEMENT_TYPE_VAR / ELEMENT_TYPE_MVAR.
class TypeParameter : public IType {
public:
    enum class OwnerKind { Class, Method };
    TypeParameter(int index, OwnerKind owner, std::string name)
        : index_(index), owner_(owner), name_(std::move(name)) {}
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override;
    int TypeParameterCount() const override { return 0; }
    int Index() const noexcept { return index_; }
    OwnerKind Owner() const noexcept { return owner_; }
protected:
    bool StructuralEquals(const IType& other) const override {
        const auto& o = static_cast<const TypeParameter&>(other);
        return index_ == o.index_ && owner_ == o.owner_;
    }
private:
    int index_;
    OwnerKind owner_;
    std::string name_;
};

// The null-object types (Unknown, Null, None, Dynamic). Matches the C#
// SpecialType usage where UnknownType is the null object for IType.
class SpecialType : public IType {
public:
    explicit SpecialType(TypeKind kind, std::optional<bool> isReferenceType = std::nullopt)
        : kind_(kind), isReferenceType_(isReferenceType) {}
    TypeKind Kind() const override { return kind_; }
    std::string Name() const override;
    std::string ReflectionName() const override { return Name(); }
    int TypeParameterCount() const override { return 0; }
    // Faithful port of SpecialType.cs `bool? IsReferenceType => isReferenceType`
    // (the stored field set per static singleton: NullType/Dynamic -> true,
    // NInt/NUInt -> false, UnknownType/NoType/ArgList/UnboundTypeArgument -> null).
    // The minimal port's `UnknownType()` convenience constructs
    // `SpecialType(TypeKind::Unknown)` (isReferenceType defaults to nullopt),
    // faithful to the C# `SpecialType.UnknownType` singleton (isReferenceType: null).
    std::optional<bool> IsReferenceType() const override { return isReferenceType_; }
    // Faithful port of SpecialType.cs ChangeNullability: only `Dynamic` annotates;
    // `Oblivious` and every non-Dynamic kind return `this` (defined in IType.cpp).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
protected:
    bool StructuralEquals(const IType& other) const override {
        return kind_ == static_cast<const SpecialType&>(other).kind_;
    }
private:
    TypeKind kind_;
    std::optional<bool> isReferenceType_;
};

// A modopt/modreq modified type (ECMA-335 II.23.2.7 custom modifier): a type decorated
// with an optional (modopt) or required (modreq) custom modifier type. Follows the existing
// minimal-port TypeWithElementType-flattened convention (ByReferenceType / PointerType /
// ArrayType): the decoration suffix (" modopt(<modifier>)" / " modreq(<modifier>)") is carried
// only by ReflectionName, and Name() is the element's name -- matching the sibling minimal-port
// types which keep the suffix in ReflectionName only. The C# ModifiedType derives from
// TypeWithElementType (Name = element.Name + NameSuffix); the minimal port flattens that. The
// full IType surface (ChangeNullability, member access, AcceptVisitor, VisitChildren -- the
// TypeVisitor dispatch this type is one of four concrete leaves toward) lands with the rest
// of Phase 2; this minimal leaf lands the concrete type so the not-yet-ported TypeVisitor /
// TypeParameterSubstitution can reference it.
class ModifiedType : public IType {
public:
    ModifiedType(ITypePtr modifier, ITypePtr unmodifiedType, bool isRequired)
        : modifier_(std::move(modifier)), element_(std::move(unmodifiedType)),
          isRequired_(isRequired) {}
    TypeKind Kind() const override { return isRequired_ ? TypeKind::ModReq : TypeKind::ModOpt; }
    std::string Name() const override { return element_ ? element_->Name() : std::string(); }
    std::string ReflectionName() const override;
    int TypeParameterCount() const override { return 0; }
    const ITypePtr& Modifier() const noexcept { return modifier_; }
    const ITypePtr& Element() const noexcept { return element_; }
    bool IsRequired() const noexcept { return isRequired_; }
    // Faithful port of ModifiedType.cs `bool? IsReferenceType => elementType.IsReferenceType`
    // (delegates to the decorated element type).
    std::optional<bool> IsReferenceType() const override {
        return element_ ? element_->IsReferenceType() : std::nullopt;
    }
    // Faithful port of ModifiedType.cs `ITypeDefinition GetDefinition() => elementType.GetDefinition()`
    // (delegates to the decorated element type; a custom modifier carries no definition).
    const ITypeDefinition* GetDefinition() const override {
        return element_ ? element_->GetDefinition() : nullptr;
    }
    // Faithful port of ModifiedType.cs AcceptVisitor (ModReq / ModOpt split) /
    // VisitChildren (element + modifier).
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
    // Faithful port of ModifiedType.cs ChangeNullability: forwards to the element
    // type and rebuilds the modifier wrapper only when it changed (defined in
    // IType.cpp).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
protected:
    bool StructuralEquals(const IType& other) const override;
private:
    ITypePtr modifier_;
    ITypePtr element_;
    bool isRequired_;
};

// A type annotated with a C# 8 nullable-reference annotation: a decorator wrapping a
// base type and carrying one of the three Nullability states (Oblivious / NotNullable /
// Nullable). The C# NullabilityAnnotatedType derives from DecoratedType (an abstract
// delegating base forwarding Name / ReflectionName / Kind / TypeParameterCount / ... to
// its baseType); the minimal port flattens that, deriving directly from IType and
// delegating the same accessors to the wrapped baseType_ -- the D401 ModifiedType
// flatten-TypeWithElementType precedent applied to DecoratedType. Name() /
// ReflectionName() return the base type's names verbatim (the C# DecoratedType delegates
// INamedElement.Name / ReflectionName to baseType; the `?` / `!` / `~` annotation the C#
// surfaces only in ToString(), NOT in Name / ReflectionName -- so the faithful minimal
// port does NOT append the annotation to ReflectionName either). The full IType surface
// (ChangeNullability, AcceptVisitor, VisitChildren -- the TypeVisitor dispatch this type
// is one of four concrete leaves toward, plus the NullabilityAnnotatedTypeParameter
// nested subclass that implements ITypeParameter by delegating to a base ITypeParameter)
// lands with the rest of Phase 2; this minimal leaf lands the concrete type so the
// not-yet-ported TypeVisitor / TypeParameterSubstitution can reference it.
class NullabilityAnnotatedType : public IType {
public:
    // The `Nullability` parameter/return types are namespace-qualified throughout
    // this class: its own `Nullability()` accessor shadows the enum type in MSVC's
    // complete-class lookup (the D372 crux; `ChangeNullability` above applies the
    // same). 
    NullabilityAnnotatedType(ITypePtr baseType, ::ILSpy::Decompiler::TypeSystem::Nullability nullability)
        : baseType_(std::move(baseType)), nullability_(nullability) {}
    TypeKind Kind() const override { return baseType_ ? baseType_->Kind() : TypeKind::Unknown; }
    std::string Name() const override { return baseType_ ? baseType_->Name() : std::string(); }
    std::string ReflectionName() const override {
        return baseType_ ? baseType_->ReflectionName() : std::string();
    }
    int TypeParameterCount() const override {
        return baseType_ ? baseType_->TypeParameterCount() : 0;
    }
    ::ILSpy::Decompiler::TypeSystem::Nullability Nullability() const noexcept { return nullability_; }
    // The C# `TypeWithoutAnnotation => baseType`: the un-annotated wrapped type.
    const ITypePtr& TypeWithoutAnnotation() const noexcept { return baseType_; }
    // Faithful port of the C# DecoratedType `bool? IsReferenceType => baseType.IsReferenceType`
    // (NullabilityAnnotatedType derives from DecoratedType which delegates to the
    // wrapped base type -- the nullability annotation does not change reference-ness).
    std::optional<bool> IsReferenceType() const override {
        return baseType_ ? baseType_->IsReferenceType() : std::nullopt;
    }
    // Faithful port of the C# DecoratedType `ITypeDefinition GetDefinition() => baseType.GetDefinition()`
    // (NullabilityAnnotatedType derives from DecoratedType which delegates to the wrapped base
    // type; a nullability annotation carries no definition of its own).
    const ITypeDefinition* GetDefinition() const override {
        return baseType_ ? baseType_->GetDefinition() : nullptr;
    }
    // Faithful port of NullabilityAnnotatedType.cs AcceptVisitor /
    // VisitChildren (baseType; the C# IsReferenceType / TypeParameter edge
    // cases are deferred to the Phase 2 nullability-lifting stage that consumes
    // TypeVisitor -- the minimal leaf reconstructs the wrapper with the visited
    // base and the same nullability).
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
    // Faithful port of NullabilityAnnotatedType.cs ChangeNullability: identical
    // nullability -> this; otherwise forward to the unwrapped base type (defined
    // in IType.cpp).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
protected:
    bool StructuralEquals(const IType& other) const override {
        const auto& o = static_cast<const NullabilityAnnotatedType&>(other);
        return nullability_ == o.nullability_
            && baseType_->Equals(*o.baseType_);
    }
private:
    ITypePtr baseType_;
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability_;
};

// A function pointer type (C# 9 `delegate*`): `delegate* <returnType>(parameterTypes)`,
// optionally `unmanaged` with a calling convention and/or custom `CallConv*` modifiers.
// The C# FunctionPointerType derives from AbstractType (the full IType base) and holds a
// cross-layer `MetadataModule module` used only by `Kind()` (which returns `TypeKind::Struct`
// when `TypeSystemOptions.FunctionPointers` is disabled, acting as a UIntPtr alias) and
// `GetDefinition()` (resolves the UIntPtr alias). The minimal port derives directly from
// IType (the D401/D402 flatten-AbstractType precedent), drops the deferred `module`, and
// returns `TypeKind::FunctionPointer` unconditionally -- the common case where the type
// system advertises function-pointer support; the module-dependent `Kind()` gate and the
// `GetDefinition()` UIntPtr-alias fallback land with the rest of Phase 2. `Name()` /
// `ReflectionName()` are `"delegate*"` (the C# `Name` is literally `"delegate*"` and
// `ReflectionName` falls through to `AbstractType.FullName` = `Name`, since `Namespace` is
// the empty default) -- every function-pointer type shares the same rendered name; the
// signature (return type + parameters + calling convention) is carried by the dedicated
// accessors and the (deferred) `ToString` / `AcceptVisitor` / `VisitChildren`, NOT by
// `Name` / `ReflectionName`. `StructuralEquals` faithfully compares the six core fields
// the C# `Equals` compares (calling convention + custom calling conventions + return type +
// return-ref-readonly flag + parameter types + parameter reference kinds).
class FunctionPointerType : public IType {
public:
    FunctionPointerType(SignatureCallingConvention callingConvention,
                        std::vector<ITypePtr> customCallingConventions,
                        ITypePtr returnType, bool returnIsRefReadOnly,
                        std::vector<ITypePtr> parameterTypes,
                        std::vector<ReferenceKind> parameterReferenceKinds)
        : callingConvention_(callingConvention),
          customCallingConventions_(std::move(customCallingConventions)),
          returnType_(std::move(returnType)),
          returnIsRefReadOnly_(returnIsRefReadOnly),
          parameterTypes_(std::move(parameterTypes)),
          parameterReferenceKinds_(std::move(parameterReferenceKinds))
    {
        // The C# `Debug.Assert(parameterTypes.Length == parameterReferenceKinds.Length)` --
        // every parameter carries exactly one reference kind.
        assert(parameterTypes_.size() == parameterReferenceKinds_.size());
    }
    TypeKind Kind() const override { return TypeKind::FunctionPointer; }
    std::string Name() const override { return "delegate*"; }
    // The C# `ReflectionName` is `AbstractType.FullName` = `Name` (Namespace is the empty
    // default), so every function-pointer type renders the same reflection name.
    std::string ReflectionName() const override { return "delegate*"; }
    int TypeParameterCount() const override { return 0; }
    // Faithful port of FunctionPointerType.cs `bool? IsReferenceType => false`
    // (a function pointer is a value type).
    std::optional<bool> IsReferenceType() const override { return std::optional<bool>(false); }
    SignatureCallingConvention CallingConvention() const noexcept { return callingConvention_; }
    const std::vector<ITypePtr>& CustomCallingConventions() const noexcept { return customCallingConventions_; }
    const ITypePtr& ReturnType() const noexcept { return returnType_; }
    bool ReturnIsRefReadOnly() const noexcept { return returnIsRefReadOnly_; }
    const std::vector<ITypePtr>& ParameterTypes() const noexcept { return parameterTypes_; }
    const std::vector<ReferenceKind>& ParameterReferenceKinds() const noexcept { return parameterReferenceKinds_; }
    // Faithful port of FunctionPointerType.cs VisitChildren (return type +
    // parameter types; the custom calling conventions are carried over unvisited,
    // matching the C# source).
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
protected:
    bool StructuralEquals(const IType& other) const override {
        const auto& o = static_cast<const FunctionPointerType&>(other);
        if (callingConvention_ != o.callingConvention_) return false;
        if (returnIsRefReadOnly_ != o.returnIsRefReadOnly_) return false;
        if (parameterTypes_.size() != o.parameterTypes_.size()) return false;
        if (parameterReferenceKinds_.size() != o.parameterReferenceKinds_.size()) return false;
        if (customCallingConventions_.size() != o.customCallingConventions_.size()) return false;
        // ReturnType.Equals (the C# `ReturnType.Equals(fpt.ReturnType)`).
        if (!returnType_->Equals(*o.returnType_)) return false;
        // ParameterTypes.SequenceEqual (element-wise Equals).
        for (std::size_t i = 0; i < parameterTypes_.size(); ++i) {
            if (!parameterTypes_[i]->Equals(*o.parameterTypes_[i])) return false;
        }
        // ParameterReferenceKinds.SequenceEqual.
        for (std::size_t i = 0; i < parameterReferenceKinds_.size(); ++i) {
            if (parameterReferenceKinds_[i] != o.parameterReferenceKinds_[i]) return false;
        }
        // CustomCallingConventions.SequenceEqual (element-wise Equals).
        for (std::size_t i = 0; i < customCallingConventions_.size(); ++i) {
            if (!customCallingConventions_[i]->Equals(*o.customCallingConventions_[i])) return false;
        }
        return true;
    }
private:
    SignatureCallingConvention callingConvention_;
    std::vector<ITypePtr> customCallingConventions_;
    ITypePtr returnType_;
    bool returnIsRefReadOnly_;
    std::vector<ITypePtr> parameterTypes_;
    std::vector<ReferenceKind> parameterReferenceKinds_;
};

// A C# 7 tuple type (`(int, string)`, `(int x, string y)`): a tuple built atop an underlying
// `System.ValueTuple<...>` parameterized type. The C# `TupleType : AbstractType,
// ICompilationProvider` holds the `ICompilation` it was built from and constructs its
// `UnderlyingType` (the `System.ValueTuple<...>` chain) at ctor time via the recursive
// `CreateUnderlyingType` / `FindValueTupleType` helpers (which call `ICompilation.FindType` at
// runtime, threading 8-ary `ValueTuple<T1..T7,TRest>` nesting through `RestPosition = 8`).
// The minimal port flattens `AbstractType` to a direct `: IType` derivation (the
// D401/D402/D404 flatten-AbstractType precedent) and DEFERS the `CreateUnderlyingType` /
// `FindValueTupleType` recursion by accepting an ALREADY-BUILT `UnderlyingType` as a ctor
// parameter -- the caller (a future port of the `TupleType` ctor or `FromUnderlyingType` /
// the type-resolution stage) builds the `System.ValueTuple<...>` `ParameterizedType` and
// hands it in, so this minimal leaf needs no `ICompilation` at construction. The `Compilation`
// property (the `ICompilationProvider` surface the C# `TupleType` implements), the static
// `IsTupleCompatible` / `FromUnderlyingType` / `GetTupleElementTypes` helpers, the
// `GetHashCode` / `ToString` / member-access delegations (`GetMethods` / `GetProperties` /
// `GetFields` / ... forward to `UnderlyingType`), and the `AcceptVisitor` / `VisitChildren`
// `TypeVisitor` dispatch (this is the fourth and final concrete leaf toward `TypeVisitor` /
// `TypeParameterSubstitution`) land with the rest of Phase 2; this minimal leaf lands ONLY the
// concrete type so the not-yet-ported `TypeVisitor` / `TypeParameterSubstitution` can reference it.
// `Kind()` is `TypeKind::Tuple` (a unique discriminator, unlike `NullabilityAnnotatedType` D402
// whose `Kind` delegates to its base -- so a `TupleType`-vs-non-`TupleType` comparison
// short-circuits in `IType::Equals` before `StructuralEquals`, no UB). `Name()` / `ReflectionName()`
// delegate to the `UnderlyingType` verbatim (the C# `FullName` / `Name` / `ReflectionName` /
// `Namespace` all delegate to `UnderlyingType`); `TypeParameterCount()` is `0` (the C#
// `TupleType` overrides it to `0`, distinct from the underlying `ValueTuple<...>` which has
// arity 8 -- the tuple's own type-parameter count is 0, the arity lives on the underlying type).
// `Equals` (via `IType::Equals` -> `StructuralEquals`) compares `UnderlyingType.Equals` AND
// `ElementNames` element-wise, faithful to the C# `Equals(IType)` override (de-duplicated: the C#
// source calls `UnderlyingType.Equals(o.UnderlyingType)` twice -- a redundant source quirk; the
// effective condition is `UnderlyingType.Equals(o.UnderlyingType) && ElementNames.SequenceEqual`).
class TupleType : public IType {
public:
    // The ctor takes a PRE-BUILT `underlyingType` (the `System.ValueTuple<...>` parameterized
    // type), the tuple `elementTypes`, and the optional `elementNames` (empty strings for
    // unnamed elements). An empty `elementNames` is the "not provided" sentinel (the C#
    // `default(ImmutableArray<string>)`): it is filled with empty strings matching
    // `elementTypes.size()`, mirroring the C# ctor's `Enumerable.Repeat<string>(null, ...)`.
    TupleType(ITypePtr underlyingType, std::vector<ITypePtr> elementTypes,
              std::vector<std::string> elementNames = {})
        : underlyingType_(std::move(underlyingType)),
          elementTypes_(std::move(elementTypes)),
          elementNames_(std::move(elementNames))
    {
        if (elementNames_.empty() && !elementTypes_.empty()) {
            elementNames_.assign(elementTypes_.size(), std::string());
        }
        // The C# `Debug.Assert(elementNames.Length == elementTypes.Length)` when names are
        // provided -- a non-empty names vector must match the element count.
        assert(elementNames_.empty() || elementNames_.size() == elementTypes_.size());
    }
    TypeKind Kind() const override { return TypeKind::Tuple; }
    // The C# `Name` / `ReflectionName` / `FullName` / `Namespace` delegate to `UnderlyingType`.
    std::string Name() const override {
        return underlyingType_ ? underlyingType_->Name() : std::string();
    }
    std::string ReflectionName() const override {
        return underlyingType_ ? underlyingType_->ReflectionName() : std::string();
    }
    int TypeParameterCount() const override { return 0; }
    // The C# `Cardinality => ElementTypes.Length`.
    int Cardinality() const noexcept { return static_cast<int>(elementTypes_.size()); }
    const std::vector<ITypePtr>& ElementTypes() const noexcept { return elementTypes_; }
    const std::vector<std::string>& ElementNames() const noexcept { return elementNames_; }
    const ITypePtr& UnderlyingType() const noexcept { return underlyingType_; }
    // Faithful port of TupleType.cs `bool? IsReferenceType => UnderlyingType.IsReferenceType`
    // (delegates to the underlying ValueTuple<...> parameterized type).
    std::optional<bool> IsReferenceType() const override {
        return underlyingType_ ? underlyingType_->IsReferenceType() : std::nullopt;
    }
    // Faithful port of TupleType.cs `ITypeDefinition GetDefinition() => UnderlyingType.GetDefinition()`
    // (delegates to the underlying `System.ValueTuple<...>` parameterized type; a tuple carries
    // no definition of its own).
    const ITypeDefinition* GetDefinition() const override {
        return underlyingType_ ? underlyingType_->GetDefinition() : nullptr;
    }
    // Faithful port of TupleType.cs VisitChildren (element types; the underlying
    // ValueTuple<...> and element names are carried over; the C# Compilation /
    // GetDefinition().ParentModule reconstruction inputs are deferred to the
    // Phase 2 type-resolution stage -- the minimal leaf reconstructs with the
    // same underlying type and names).
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override;
    ITypePtr VisitChildren(TypeVisitor& visitor) override;
protected:
    bool StructuralEquals(const IType& other) const override {
        const auto& o = static_cast<const TupleType&>(other);
        // The C# `Equals`: `UnderlyingType.Equals(o.UnderlyingType) && ElementNames.SequenceEqual`.
        if (!underlyingType_ || !o.underlyingType_) return underlyingType_ == o.underlyingType_;
        if (!underlyingType_->Equals(*o.underlyingType_)) return false;
        if (elementNames_.size() != o.elementNames_.size()) return false;
        for (std::size_t i = 0; i < elementNames_.size(); ++i) {
            if (elementNames_[i] != o.elementNames_[i]) return false;
        }
        return true;
    }
private:
    ITypePtr underlyingType_;
    std::vector<ITypePtr> elementTypes_;
    std::vector<std::string> elementNames_;
};

// A minimal port of the C# `UnknownType` (ICSharpCode.Decompiler/TypeSystem/
// Implementation/UnknownType.cs) -- an unknown type where (part of) the name is known.
// The C# `UnknownType` carries a `FullTypeName` (namespace + name + type-parameter-
// count) and a `namespaceKnown` flag (false when the namespace was passed as null).
// `Name` returns the known name, `TypeParameterCount` the known count, `Kind` is
// `TypeKind::Unknown`, and `ReflectionName` is "?" when the namespace is unknown.
// The full `IType` surface (`GetDefinitionOrUnknown`, `Namespace`, `FullName`,
// `TypeParameters`, `TypeArguments`, `IsReferenceType`, `ChangeNullability`,
// `GetHashCode`, `Equals`, `ToString`, `AcceptVisitor`, `VisitChildren`) lands with
// the rest of Phase 2; this minimal leaf lands the concrete type so the
// `NestedTypeReference` (and future consumers) can construct the null-namespace
// `UnknownType(null, name, tpc)` fallback the `Resolve` path produces.
//
// The C# `string? namespaceName` (nullable reference) ports to
// `std::optional<std::string>` (`std::nullopt` = the C# `null` -> `namespaceKnown =
// false` -> `ReflectionName = "?"`; a present string = the C# non-null ->
// `namespaceKnown = true`), faithfully modeling the C# null-vs-non-null distinction
// that a bare `std::string` cannot.
class UnknownType : public IType {
public:
    UnknownType(std::optional<std::string> ns, std::string name,
                int typeParameterCount,
                std::optional<bool> isReferenceType = std::nullopt)
        : fullTypeName_(ns.value_or(""), std::move(name), typeParameterCount),
          namespaceKnown_(ns.has_value()), isReferenceType_(isReferenceType) {}

    TypeKind Kind() const override { return TypeKind::Unknown; }
    std::string Name() const override { return fullTypeName_.Name(); }
    std::string ReflectionName() const override
    {
        return namespaceKnown_ ? fullTypeName_.ReflectionName() : std::string("?");
    }
    int TypeParameterCount() const override
    {
        return fullTypeName_.TypeParameterCount();
    }
    // Faithful port of UnknownType.cs `bool? IsReferenceType => isReferenceType`
    // (the stored field, defaulting to null = "not known", passed by the
    // TypeProvider when it can derive reference-ness from the metadata raw type
    // kind).
    std::optional<bool> IsReferenceType() const override { return isReferenceType_; }
    const TopLevelTypeName& FullTypeName() const noexcept { return fullTypeName_; }
    // Faithful port of UnknownType.cs ChangeNullability: `Oblivious` (and value
    // types) return `this`; a non-`Oblivious` annotation on a reference type wraps
    // in `NullabilityAnnotatedType` (defined in IType.cpp).
    ITypePtr ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) override;
protected:
    bool StructuralEquals(const IType& other) const override
    {
        const auto& o = static_cast<const UnknownType&>(other);
        return namespaceKnown_ == o.namespaceKnown_
               && fullTypeName_ == o.fullTypeName_
               && isReferenceType_ == o.isReferenceType_;
    }
private:
    TopLevelTypeName fullTypeName_;
    bool namespaceKnown_;
    std::optional<bool> isReferenceType_;
};

// Convenience: the UnknownType null object (a `SpecialType(TypeKind::Unknown)`
// with no name). Distinct from the `UnknownType` CLASS above (which carries a
// known name); the class and this function share the name `UnknownType` via the
// C++ tag-vs-ordinary-namespace distinction (a class type and a function can
// coexist in the same scope -- `std::make_shared<UnknownType>(...)` resolves to
// the class, `UnknownType()` to this function).
inline ITypePtr UnknownType() { return std::make_shared<SpecialType>(TypeKind::Unknown); }

// Convenience: the C# `SpecialType.NoType` singleton (a `SpecialType(TypeKind::None)`
// with name "?" and `isReferenceType: null`). Used by `ResolveResult` subclasses for
// expressions without a type (method groups, lambdas, namespaces, throw
// statements) -- the C# `NamespaceResolveResult` / `ThrowResolveResult` pass
// `SpecialType.NoType` to the `ResolveResult` base ctor. The minimal port's
// `SpecialType(TypeKind::None)` has `isReferenceType` defaulting to `nullopt`
// (the C# `null`), faithful to the C# singleton. Distinct from `UnknownType()`
// (which is `TypeKind::Unknown`, the error-type null object) -- `NoType` is
// `TypeKind::None` (no type at all, e.g. a method group or a namespace reference).
inline ITypePtr NoType() { return std::make_shared<SpecialType>(TypeKind::None); }

} // namespace ILSpy::Decompiler::TypeSystem
