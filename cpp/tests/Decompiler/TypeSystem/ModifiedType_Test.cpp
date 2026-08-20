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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the minimal-port `ModifiedType` concrete `IType` -- a type decorated
// with an ECMA-335 II.23.2.7 custom modifier (an optional `modopt` or required
// `modreq`). It is the smallest of the four "concrete IType VisitChildren types"
// (`ModifiedType` / `TupleType` / `NullabilityAnnotatedType` / `FunctionPointerType`)
// that the not-yet-ported `TypeVisitor` / `TypeParameterSubstitution` need; this
// port lands it as a self-contained leaf toward `TypeVisitor` / `TypeSystemAstBuilder`
// / `CSharpAmbience`. It follows the existing `IType.hpp` minimal-port convention
// (`ByReferenceType` / `PointerType` / `ArrayType`): `Name()` is the element's name
// (the `modopt`/`modreq` annotation is carried only by `ReflectionName`, matching
// the sibling minimal-port types which keep the suffix in `ReflectionName` only);
// the full `IType` surface (`ChangeNullability`, member access, `AcceptVisitor`,
// `VisitChildren`) lands with the rest of Phase 2.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

namespace {

// A typical custom modifier: System.Runtime.CompilerServices.IsConst.
ITypePtr IsConstModifier() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System.Runtime.CompilerServices", "IsConst"));
}

ITypePtr Int32Element() {
    return std::make_shared<KnownType>(KnownTypeCode::Int32);
}

ITypePtr StringElement() {
    return std::make_shared<KnownType>(KnownTypeCode::String);
}

}  // namespace

TEST(ModifiedTypeTest, KindIsModOptForOptionalModifier) {
    ModifiedType t(IsConstModifier(), Int32Element(), /*isRequired=*/false);
    EXPECT_EQ(t.Kind(), TypeKind::ModOpt);
}

TEST(ModifiedTypeTest, KindIsModReqForRequiredModifier) {
    ModifiedType t(IsConstModifier(), Int32Element(), /*isRequired=*/true);
    EXPECT_EQ(t.Kind(), TypeKind::ModReq);
}

TEST(ModifiedTypeTest, ModifierAccessorReturnsConfiguredType) {
    ITypePtr modifier = IsConstModifier();
    ModifiedType t(modifier, Int32Element(), false);
    EXPECT_EQ(t.Modifier().get(), modifier.get());
}

TEST(ModifiedTypeTest, ElementAccessorReturnsConfiguredType) {
    ITypePtr element = Int32Element();
    ModifiedType t(IsConstModifier(), element, false);
    EXPECT_EQ(t.Element().get(), element.get());
}

TEST(ModifiedTypeTest, IsRequiredAccessorReturnsConfiguredFlag) {
    ModifiedType opt(IsConstModifier(), Int32Element(), false);
    ModifiedType req(IsConstModifier(), Int32Element(), true);
    EXPECT_FALSE(opt.IsRequired());
    EXPECT_TRUE(req.IsRequired());
}

TEST(ModifiedTypeTest, ReflectionNameAppendsModOptAnnotationWithModifierReflectionName) {
    ModifiedType t(IsConstModifier(), Int32Element(), false);
    EXPECT_EQ(t.ReflectionName(),
              "System.Int32 modopt(System.Runtime.CompilerServices.IsConst)");
}

TEST(ModifiedTypeTest, ReflectionNameAppendsModReqAnnotationWithModifierReflectionName) {
    ModifiedType t(IsConstModifier(), Int32Element(), true);
    EXPECT_EQ(t.ReflectionName(),
              "System.Int32 modreq(System.Runtime.CompilerServices.IsConst)");
}

TEST(ModifiedTypeTest, NameIsElementNameWithoutSuffix) {
    // The minimal-port convention (ByReferenceType / PointerType / ArrayType): the
    // decoration suffix lives only in ReflectionName; Name is the element's short name.
    ModifiedType t(IsConstModifier(), Int32Element(), false);
    EXPECT_EQ(t.Name(), "Int32");
}

TEST(ModifiedTypeTest, TypeParameterCountIsZero) {
    ModifiedType t(IsConstModifier(), Int32Element(), false);
    EXPECT_EQ(t.TypeParameterCount(), 0);
}

TEST(ModifiedTypeTest, EqualsComparesKindModifierAndElement) {
    ModifiedType a(IsConstModifier(), Int32Element(), false);
    ModifiedType same(IsConstModifier(), Int32Element(), false);
    EXPECT_TRUE(a.Equals(same));

    // Different required-ness => different kind (ModOpt vs ModReq) => not equal.
    ModifiedType req(IsConstModifier(), Int32Element(), true);
    EXPECT_FALSE(a.Equals(req));

    // Different element => not equal.
    ModifiedType differentElement(IsConstModifier(), StringElement(), false);
    EXPECT_FALSE(a.Equals(differentElement));

    // A non-ModifiedType is never equal (Kind differs).
    EXPECT_FALSE(a.Equals(*Int32Element()));
}

TEST(ModifiedTypeTest, EqualsIsReflexiveAndSymmetric) {
    ModifiedType a(IsConstModifier(), Int32Element(), false);
    ModifiedType b(IsConstModifier(), Int32Element(), false);
    EXPECT_TRUE(a.Equals(a));
    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(b.Equals(a));
}

TEST(ModifiedTypeTest, DispatchesPolymorphicallyThroughITypePointer) {
    ModifiedType t(IsConstModifier(), Int32Element(), false);
    const IType& asBase = t;
    EXPECT_EQ(asBase.Kind(), TypeKind::ModOpt);
    EXPECT_EQ(asBase.Name(), "Int32");
    EXPECT_EQ(asBase.ReflectionName(),
              "System.Int32 modopt(System.Runtime.CompilerServices.IsConst)");
    EXPECT_EQ(asBase.TypeParameterCount(), 0);
    ModifiedType same(IsConstModifier(), Int32Element(), false);
    EXPECT_TRUE(asBase.Equals(same));
}
