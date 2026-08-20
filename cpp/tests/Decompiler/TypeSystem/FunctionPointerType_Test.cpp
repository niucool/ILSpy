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
// PURPOSE NONINFRINGEMENT. HOWEVER CAUSED AND ON WHICHEVER THEORY OF LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the minimal-port `FunctionPointerType` concrete `IType` -- the C# 9
// `delegate*` function-pointer type, the third of the four "concrete IType
// VisitChildren types" (ModifiedType / NullabilityAnnotatedType / FunctionPointerType
// / TupleType) that the not-yet-ported TypeVisitor / TypeParameterSubstitution need;
// this port lands it as a self-contained leaf toward TypeVisitor / TypeSystemAstBuilder
// / CSharpAmbience. It follows the existing IType.hpp minimal-port convention (the
// D401/D402 flatten-AbstractType precedent): Name() / ReflectionName() are the literal
// "delegate*" (the C# `Name` is literally "delegate*" and `ReflectionName` falls
// through to `AbstractType.FullName` = `Name`, since `Namespace` is the empty default);
// the full IType surface (the cross-layer `MetadataModule module` field, the
// `TypeSystemOptions.FunctionPointers`-gated `Kind()` UIntPtr-alias fallback,
// `GetDefinition`, `FromSignature`, `WithSignature`, `AcceptVisitor`, `VisitChildren` --
// the TypeVisitor dispatch) lands with the rest of Phase 2.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

namespace {

ITypePtr ObjectType() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr StringType() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// A `delegate*<int>(string)` -- default calling convention, one by-value string parameter,
// int return. The simplest complete function-pointer shape.
FunctionPointerType DefaultOneParam() {
    return FunctionPointerType(
        SignatureCallingConvention::Default, {}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
}

}  // namespace

TEST(FunctionPointerTypeTest, KindIsFunctionPointer) {
    FunctionPointerType t = DefaultOneParam();
    EXPECT_EQ(t.Kind(), TypeKind::FunctionPointer);
}

TEST(FunctionPointerTypeTest, NameIsDelegateStar) {
    // The C# `Name` is literally "delegate*" (FunctionPointerType.cs).
    FunctionPointerType t = DefaultOneParam();
    EXPECT_EQ(t.Name(), "delegate*");
}

TEST(FunctionPointerTypeTest, ReflectionNameIsDelegateStar) {
    // The C# `ReflectionName` falls through to `AbstractType.FullName` = `Name`
    // (Namespace is the empty default), so every function-pointer type shares the
    // same reflection name regardless of its signature.
    FunctionPointerType t = DefaultOneParam();
    EXPECT_EQ(t.ReflectionName(), "delegate*");
    // A different signature still renders the same reflection name.
    FunctionPointerType u(
        SignatureCallingConvention::Unmanaged, {}, ObjectType(), true,
        {Int32Type(), StringType()}, {ReferenceKind::Ref, ReferenceKind::In});
    EXPECT_EQ(u.ReflectionName(), "delegate*");
}

TEST(FunctionPointerTypeTest, TypeParameterCountIsZero) {
    FunctionPointerType t = DefaultOneParam();
    EXPECT_EQ(t.TypeParameterCount(), 0);
}

TEST(FunctionPointerTypeTest, AccessorsReturnConfiguredValues) {
    ITypePtr ret = Int32Type();
    ITypePtr param = StringType();
    std::vector<ITypePtr> params = {param};
    std::vector<ReferenceKind> kinds = {ReferenceKind::None};
    FunctionPointerType t(
        SignatureCallingConvention::CDecl, {}, ret, true, params, kinds);
    EXPECT_EQ(t.CallingConvention(), SignatureCallingConvention::CDecl);
    EXPECT_TRUE(t.CustomCallingConventions().empty());
    EXPECT_EQ(t.ReturnType().get(), ret.get());
    EXPECT_TRUE(t.ReturnIsRefReadOnly());
    ASSERT_EQ(t.ParameterTypes().size(), 1u);
    EXPECT_EQ(t.ParameterTypes()[0].get(), param.get());
    ASSERT_EQ(t.ParameterReferenceKinds().size(), 1u);
    EXPECT_EQ(t.ParameterReferenceKinds()[0], ReferenceKind::None);
}

TEST(FunctionPointerTypeTest, CustomCallingConventionsAreCarried) {
    // A custom CallConv* modifier type is carried verbatim in CustomCallingConventions
    // (the C# `CustomCallingConventions` field, surfaced by TypeSystemAstBuilder).
    ITypePtr callConv = std::make_shared<SimpleType>(
        TopLevelTypeName("System.Runtime.CompilerServices", "CallConvCdecl"));
    FunctionPointerType t(
        SignatureCallingConvention::Unmanaged, {callConv}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
    ASSERT_EQ(t.CustomCallingConventions().size(), 1u);
    EXPECT_EQ(t.CustomCallingConventions()[0].get(), callConv.get());
}

TEST(FunctionPointerTypeTest, EqualsComparesAllSixCoreFields) {
    FunctionPointerType a = DefaultOneParam();
    FunctionPointerType same = DefaultOneParam();
    EXPECT_TRUE(a.Equals(same));

    // Different calling convention => not equal.
    FunctionPointerType diffCallingConv(
        SignatureCallingConvention::Unmanaged, {}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
    EXPECT_FALSE(a.Equals(diffCallingConv));

    // Different return type => not equal.
    FunctionPointerType diffReturn(
        SignatureCallingConvention::Default, {}, ObjectType(), false,
        {StringType()}, {ReferenceKind::None});
    EXPECT_FALSE(a.Equals(diffReturn));

    // Different return-ref-readonly flag => not equal.
    FunctionPointerType diffRefReadOnly(
        SignatureCallingConvention::Default, {}, Int32Type(), true,
        {StringType()}, {ReferenceKind::None});
    EXPECT_FALSE(a.Equals(diffRefReadOnly));

    // Different parameter type => not equal.
    FunctionPointerType diffParam(
        SignatureCallingConvention::Default, {}, Int32Type(), false,
        {ObjectType()}, {ReferenceKind::None});
    EXPECT_FALSE(a.Equals(diffParam));

    // Different parameter reference kind => not equal.
    FunctionPointerType diffRefKind(
        SignatureCallingConvention::Default, {}, Int32Type(), false,
        {StringType()}, {ReferenceKind::Ref});
    EXPECT_FALSE(a.Equals(diffRefKind));

    // Different custom calling conventions => not equal.
    ITypePtr callConv = std::make_shared<SimpleType>(
        TopLevelTypeName("System.Runtime.CompilerServices", "CallConvCdecl"));
    FunctionPointerType diffCustomConv(
        SignatureCallingConvention::Unmanaged, {callConv}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
    FunctionPointerType baselineUnmanaged(
        SignatureCallingConvention::Unmanaged, {}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
    EXPECT_FALSE(baselineUnmanaged.Equals(diffCustomConv));
}

TEST(FunctionPointerTypeTest, EqualsIsReflexiveAndSymmetric) {
    FunctionPointerType a = DefaultOneParam();
    FunctionPointerType b = DefaultOneParam();
    EXPECT_TRUE(a.Equals(a));
    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(b.Equals(a));
}

TEST(FunctionPointerTypeTest, EqualsShortCircuitsOnDifferentKind) {
    // A function-pointer type is never equal to a type of a different kind (Kind
    // differs, so IType::Equals short-circuits before StructuralEquals).
    FunctionPointerType t = DefaultOneParam();
    EXPECT_FALSE(t.Equals(*ObjectType()));
    EXPECT_FALSE(t.Equals(*Int32Type()));
}

TEST(FunctionPointerTypeTest, DispatchesPolymorphicallyThroughITypeReference) {
    FunctionPointerType t(
        SignatureCallingConvention::StdCall, {}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
    const IType& asBase = t;
    EXPECT_EQ(asBase.Kind(), TypeKind::FunctionPointer);
    EXPECT_EQ(asBase.Name(), "delegate*");
    EXPECT_EQ(asBase.ReflectionName(), "delegate*");
    EXPECT_EQ(asBase.TypeParameterCount(), 0);
    // The CallingConvention / ReturnType / ParameterTypes / ParameterReferenceKinds /
    // CustomCallingConventions / ReturnIsRefReadOnly accessors are FunctionPointerType-
    // own (not IType virtuals in the minimal port -- the full IType surface lands with
    // the rest of Phase 2), so they are reached only through the concrete type.
    EXPECT_EQ(t.CallingConvention(), SignatureCallingConvention::StdCall);
    FunctionPointerType same(
        SignatureCallingConvention::StdCall, {}, Int32Type(), false,
        {StringType()}, {ReferenceKind::None});
    EXPECT_TRUE(asBase.Equals(same));
}
