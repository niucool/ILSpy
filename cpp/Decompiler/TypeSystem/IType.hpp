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
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

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

// Convenience: the UnknownType null object.
inline ITypePtr UnknownType() { return std::make_shared<SpecialType>(TypeKind::Unknown); }

} // namespace ILSpy::Decompiler::TypeSystem
