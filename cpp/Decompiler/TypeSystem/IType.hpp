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
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class IType;
using ITypePtr = std::shared_ptr<IType>;

// The root of the type representation. Equality is structural: two ITypes are
// equal iff they have the same Kind and the same constituent names/types.
class IType {
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
    // Structural equality; derived classes override StructuralEquals.
    bool Equals(const IType& other) const {
        if (Kind() != other.Kind()) return false;
        return StructuralEquals(other);
    }
protected:
    virtual bool StructuralEquals(const IType& other) const = 0;
};

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
    explicit SpecialType(TypeKind kind) : kind_(kind) {}
    TypeKind Kind() const override { return kind_; }
    std::string Name() const override;
    std::string ReflectionName() const override { return Name(); }
    int TypeParameterCount() const override { return 0; }
protected:
    bool StructuralEquals(const IType& other) const override {
        return kind_ == static_cast<const SpecialType&>(other).kind_;
    }
private:
    TypeKind kind_;
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
    NullabilityAnnotatedType(ITypePtr baseType, Nullability nullability)
        : baseType_(std::move(baseType)), nullability_(nullability) {}
    TypeKind Kind() const override { return baseType_ ? baseType_->Kind() : TypeKind::Unknown; }
    std::string Name() const override { return baseType_ ? baseType_->Name() : std::string(); }
    std::string ReflectionName() const override {
        return baseType_ ? baseType_->ReflectionName() : std::string();
    }
    int TypeParameterCount() const override {
        return baseType_ ? baseType_->TypeParameterCount() : 0;
    }
    Nullability Nullability() const noexcept { return nullability_; }
    // The C# `TypeWithoutAnnotation => baseType`: the un-annotated wrapped type.
    const ITypePtr& TypeWithoutAnnotation() const noexcept { return baseType_; }
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
    SignatureCallingConvention CallingConvention() const noexcept { return callingConvention_; }
    const std::vector<ITypePtr>& CustomCallingConventions() const noexcept { return customCallingConventions_; }
    const ITypePtr& ReturnType() const noexcept { return returnType_; }
    bool ReturnIsRefReadOnly() const noexcept { return returnIsRefReadOnly_; }
    const std::vector<ITypePtr>& ParameterTypes() const noexcept { return parameterTypes_; }
    const std::vector<ReferenceKind>& ParameterReferenceKinds() const noexcept { return parameterReferenceKinds_; }
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

// Convenience: the UnknownType null object.
inline ITypePtr UnknownType() { return std::make_shared<SpecialType>(TypeKind::Unknown); }

} // namespace ILSpy::Decompiler::TypeSystem
