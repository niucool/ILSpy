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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the minimal-port `NullabilityAnnotatedType` concrete `IType` -- a type
// annotated with a C# 8 nullable-reference annotation (one of Oblivious / NotNullable /
// Nullable). It is one of the four "concrete IType VisitChildren types" (ModifiedType /
// TupleType / NullabilityAnnotatedType / FunctionPointerType) that the not-yet-ported
// TypeVisitor / TypeParameterSubstitution need; this port lands it as a self-contained
// leaf toward TypeVisitor / TypeSystemAstBuilder / CSharpAmbience. It follows the
// existing IType.hpp minimal-port convention (the D401 ModifiedType flatten-
// TypeWithElementType precedent applied to DecoratedType): Name() / ReflectionName()
// delegate to the wrapped base type verbatim -- the `?` / `!` / `~` annotation the C#
// surfaces only in ToString(), NOT in Name / ReflectionName; the full IType surface
// (ChangeNullability, AcceptVisitor, VisitChildren -- the TypeVisitor dispatch, plus
// the NullabilityAnnotatedTypeParameter nested subclass that implements ITypeParameter)
// lands with the rest of Phase 2.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

namespace {

ITypePtr ObjectType() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr StringType() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
// A generic definition with a non-zero type-parameter count, to exercise TPC delegation.
ITypePtr GenericDictionaryType() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System", "Dictionary", 2));
}

}  // namespace

TEST(NullabilityAnnotatedTypeTest, KindDelegatesToBaseType) {
    // Object is a Class; the annotation does not change the kind.
    NullabilityAnnotatedType t(ObjectType(), Nullability::Nullable);
    EXPECT_EQ(t.Kind(), TypeKind::Class);
    // Int32 is a Struct.
    NullabilityAnnotatedType s(Int32Type(), Nullability::NotNullable);
    EXPECT_EQ(s.Kind(), TypeKind::Struct);
}

TEST(NullabilityAnnotatedTypeTest, NameDelegatesToBaseType) {
    NullabilityAnnotatedType t(StringType(), Nullability::Nullable);
    EXPECT_EQ(t.Name(), "String");
}

TEST(NullabilityAnnotatedTypeTest, ReflectionNameDelegatesToBaseTypeWithoutAnnotation) {
    // The C# DecoratedType delegates ReflectionName to baseType; the `?` annotation
    // surfaces only in ToString(), NOT in ReflectionName -- so the faithful minimal
    // port returns the base type's reflection name verbatim, with no suffix appended.
    NullabilityAnnotatedType t(ObjectType(), Nullability::Nullable);
    EXPECT_EQ(t.ReflectionName(), "System.Object");
    NullabilityAnnotatedType s(Int32Type(), Nullability::NotNullable);
    EXPECT_EQ(s.ReflectionName(), "System.Int32");
}

TEST(NullabilityAnnotatedTypeTest, TypeParameterCountDelegatesToBaseType) {
    // A bare Object has TPC 0; a generic Dictionary`2 has TPC 2, propagated through the
    // annotation wrapper (the C# DecoratedType delegates TypeParameterCount to baseType).
    NullabilityAnnotatedType bare(ObjectType(), Nullability::Nullable);
    EXPECT_EQ(bare.TypeParameterCount(), 0);
    NullabilityAnnotatedType generic(GenericDictionaryType(), Nullability::Nullable);
    EXPECT_EQ(generic.TypeParameterCount(), 2);
}

TEST(NullabilityAnnotatedTypeTest, NullabilityAccessorReturnsConfiguredValue) {
    NullabilityAnnotatedType nullable(ObjectType(), Nullability::Nullable);
    NullabilityAnnotatedType notNull(ObjectType(), Nullability::NotNullable);
    NullabilityAnnotatedType oblivious(ObjectType(), Nullability::Oblivious);
    EXPECT_EQ(nullable.Nullability(), Nullability::Nullable);
    EXPECT_EQ(notNull.Nullability(), Nullability::NotNullable);
    EXPECT_EQ(oblivious.Nullability(), Nullability::Oblivious);
}

TEST(NullabilityAnnotatedTypeTest, TypeWithoutAnnotationReturnsBaseType) {
    ITypePtr base = ObjectType();
    NullabilityAnnotatedType t(base, Nullability::Nullable);
    EXPECT_EQ(t.TypeWithoutAnnotation().get(), base.get());
}

TEST(NullabilityAnnotatedTypeTest, EqualsComparesNullabilityAndBaseType) {
    NullabilityAnnotatedType a(ObjectType(), Nullability::Nullable);
    NullabilityAnnotatedType same(ObjectType(), Nullability::Nullable);
    EXPECT_TRUE(a.Equals(same));

    // Different nullability => not equal.
    NullabilityAnnotatedType differentNullability(ObjectType(), Nullability::NotNullable);
    EXPECT_FALSE(a.Equals(differentNullability));

    // Different base type (same kind, Class) => not equal.
    NullabilityAnnotatedType differentBase(StringType(), Nullability::Nullable);
    EXPECT_FALSE(a.Equals(differentBase));

    // A type of a different kind (Struct vs Class) is never equal (Kind differs).
    EXPECT_FALSE(a.Equals(*Int32Type()));
}

TEST(NullabilityAnnotatedTypeTest, EqualsIsReflexiveAndSymmetric) {
    NullabilityAnnotatedType a(ObjectType(), Nullability::Nullable);
    NullabilityAnnotatedType b(ObjectType(), Nullability::Nullable);
    EXPECT_TRUE(a.Equals(a));
    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(b.Equals(a));
}

TEST(NullabilityAnnotatedTypeTest, DispatchesPolymorphicallyThroughITypeReference) {
    NullabilityAnnotatedType t(GenericDictionaryType(), Nullability::NotNullable);
    const IType& asBase = t;
    EXPECT_EQ(asBase.Kind(), TypeKind::Class);
    EXPECT_EQ(asBase.Name(), "Dictionary");
    EXPECT_EQ(asBase.ReflectionName(), "System.Dictionary`2");
    EXPECT_EQ(asBase.TypeParameterCount(), 2);
    // The Nullability() accessor is NullabilityAnnotatedType-own (not an IType virtual
    // in the minimal port -- the full IType surface lands with the rest of Phase 2),
    // so it is reached only through the concrete type, not through the IType base.
    EXPECT_EQ(t.Nullability(), Nullability::NotNullable);
    NullabilityAnnotatedType same(GenericDictionaryType(), Nullability::NotNullable);
    EXPECT_TRUE(asBase.Equals(same));
}
