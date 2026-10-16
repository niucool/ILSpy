// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `IsReferenceType` accessor on the minimal-port `IType` hierarchy
// (faithful port of IType.cs `bool? IsReferenceType`, modeled as
// `std::optional<bool>`: `true` = reference type, `false` = value type, `nullopt`
// = not known). The C# interface declares this `abstract`; the minimal port makes
// it virtual-WITH-DEFAULT `std::nullopt` (the D406 flattened-AbstractType
// convention) and overrides it in the concrete types with a clear C# value. This
// is the prerequisite leaf the D428 next-in-order analysis flagged for
// `SizeOfResolveResult` (whose `IsError` override is `referencedType.IsReferenceType
// != false` -- a value type is not an error, a reference type or an unknown type
// is). The consumer-pattern tests at the bottom pin that `optional<bool> !=
// false` mirrors the C# `bool? != bool` lifted-comparison semantics the
// `SizeOfResolveResult` port will rely on.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameter;
using ILSpy::Decompiler::TypeSystem::UnknownType;

namespace {

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr Boolean() { return std::make_shared<KnownType>(KnownTypeCode::Boolean); }
ITypePtr Void() { return std::make_shared<KnownType>(KnownTypeCode::Void); }

// A custom-modifier type (System.Runtime.CompilerServices.IsConst), used as the
// `modifier` arg of `ModifiedType` (its `IsReferenceType` delegates to the
// *element*, not the modifier).
ITypePtr IsConstModifier() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System.Runtime.CompilerServices", "IsConst"));
}

} // namespace

// ---- KnownType: derived from Kind() (the MetadataTypeDefinition switch) ----

TEST(IsReferenceTypeTest, KnownTypeValueTypesAreFalse) {
    EXPECT_EQ(Int32()->IsReferenceType(), std::optional<bool>(false));
    EXPECT_EQ(Boolean()->IsReferenceType(), std::optional<bool>(false));
    EXPECT_EQ(Void()->IsReferenceType(), std::optional<bool>(false));
}

TEST(IsReferenceTypeTest, KnownTypeReferenceTypesAreTrue) {
    EXPECT_EQ(Object()->IsReferenceType(), std::optional<bool>(true));
    EXPECT_EQ(String()->IsReferenceType(), std::optional<bool>(true));
}

// ---- ArrayType: always true (an array is a reference type) ----

TEST(IsReferenceTypeTest, ArrayTypeIsAlwaysReferenceType) {
    auto sz = std::make_shared<ArrayType>(Int32());
    EXPECT_EQ(sz->IsReferenceType(), std::optional<bool>(true));
    auto multi = std::make_shared<ArrayType>(Int32(), 2);
    EXPECT_EQ(multi->IsReferenceType(), std::optional<bool>(true));
    // The element's reference-ness does not change the array's: an array of
    // value types is still a reference type.
    auto arrayOfRef = std::make_shared<ArrayType>(Object());
    EXPECT_EQ(arrayOfRef->IsReferenceType(), std::optional<bool>(true));
}

// ---- FunctionPointerType: always false (a function pointer is a value type) ----

TEST(IsReferenceTypeTest, FunctionPointerTypeIsAlwaysValueType) {
    // Direct construction (not `std::make_shared`): the `{}` braced-init-lists for
    // the vector ctor params cannot be deduced by `make_shared`'s perfect-forwarding
    // template, so the ctor is invoked directly where the param types pin them.
    FunctionPointerType fpt(
        SignatureCallingConvention::Default, /*customCallingConventions*/ {},
        Int32(), /*returnIsRefReadOnly*/ false,
        /*parameterTypes*/ {}, /*parameterReferenceKinds*/ {});
    EXPECT_EQ(fpt.IsReferenceType(), std::optional<bool>(false));
}

// ---- ModifiedType: delegates to the decorated element type ----

TEST(IsReferenceTypeTest, ModifiedTypeDelegatesToElement) {
    auto valElem = std::make_shared<ModifiedType>(IsConstModifier(), Int32(), /*isRequired*/ false);
    EXPECT_EQ(valElem->IsReferenceType(), std::optional<bool>(false));
    auto refElem = std::make_shared<ModifiedType>(IsConstModifier(), Object(), /*isRequired*/ true);
    EXPECT_EQ(refElem->IsReferenceType(), std::optional<bool>(true));
    // The modifier's own reference-ness is irrelevant; the element drives it.
    auto modRefModifier = std::make_shared<ModifiedType>(Object(), Int32(), /*isRequired*/ false);
    EXPECT_EQ(modRefModifier->IsReferenceType(), std::optional<bool>(false));
}

// ---- ParameterizedType: delegates to the generic definition ----

TEST(IsReferenceTypeTest, ParameterizedTypeDelegatesToGenericType) {
    // A reference-type generic (Object) -> the instantiation is a reference type.
    auto refInst = std::make_shared<ParameterizedType>(Object(), std::vector<ITypePtr>{Int32()});
    EXPECT_EQ(refInst->IsReferenceType(), std::optional<bool>(true));
    // A value-type generic (Int32, used here as a stand-in generic definition)
    // -> the instantiation is a value type.
    auto valInst = std::make_shared<ParameterizedType>(Int32(), std::vector<ITypePtr>{Object()});
    EXPECT_EQ(valInst->IsReferenceType(), std::optional<bool>(false));
}

// ---- TupleType: delegates to the underlying ValueTuple<...> parameterized type ----

TEST(IsReferenceTypeTest, TupleTypeDelegatesToUnderlying) {
    // A real ValueTuple is a struct; stand it in with a value-type generic
    // definition so the delegation resolves to false.
    auto underlying = std::make_shared<ParameterizedType>(Int32(), std::vector<ITypePtr>{Int32(), String()});
    auto tup = std::make_shared<TupleType>(underlying,
                                           std::vector<ITypePtr>{Int32(), String()},
                                           std::vector<std::string>{"a", "b"});
    EXPECT_EQ(tup->IsReferenceType(), std::optional<bool>(false));
    // An underlying that is a reference type propagates true.
    auto refUnderlying = std::make_shared<ParameterizedType>(Object(), std::vector<ITypePtr>{Int32()});
    auto refTup = std::make_shared<TupleType>(refUnderlying,
                                              std::vector<ITypePtr>{Int32()},
                                              std::vector<std::string>{});
    EXPECT_EQ(refTup->IsReferenceType(), std::optional<bool>(true));
}

// ---- NullabilityAnnotatedType: delegates to the wrapped base type ----

TEST(IsReferenceTypeTest, NullabilityAnnotatedTypeDelegatesToBase) {
    auto ref = std::make_shared<NullabilityAnnotatedType>(Object(), Nullability::Nullable);
    EXPECT_EQ(ref->IsReferenceType(), std::optional<bool>(true));
    auto val = std::make_shared<NullabilityAnnotatedType>(Int32(), Nullability::NotNullable);
    EXPECT_EQ(val->IsReferenceType(), std::optional<bool>(false));
    // The nullability annotation does not change reference-ness: a NotNullable
    // reference type is still a reference type.
    auto refNotNull = std::make_shared<NullabilityAnnotatedType>(Object(), Nullability::NotNullable);
    EXPECT_EQ(refNotNull->IsReferenceType(), std::optional<bool>(true));
}

// ---- SpecialType: the stored ctor bool? (default nullopt) ----

TEST(IsReferenceTypeTest, SpecialTypeDefaultsToUnknown) {
    auto unknown = std::make_shared<SpecialType>(TypeKind::Unknown);
    EXPECT_EQ(unknown->IsReferenceType(), std::nullopt);
}

TEST(IsReferenceTypeTest, SpecialTypeStoresConfiguredBool) {
    auto asRef = std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true));
    EXPECT_EQ(asRef->IsReferenceType(), std::optional<bool>(true));
    auto asVal = std::make_shared<SpecialType>(TypeKind::NInt, std::optional<bool>(false));
    EXPECT_EQ(asVal->IsReferenceType(), std::optional<bool>(false));
}

TEST(IsReferenceTypeTest, UnknownTypeConvenienceIsUnknown) {
    // The `UnknownType()` convenience constructs `SpecialType(TypeKind::Unknown)`
    // with the default nullopt, faithful to the C# `SpecialType.UnknownType`
    // singleton (isReferenceType: null).
    EXPECT_EQ(ILSpy::Decompiler::TypeSystem::UnknownType()->IsReferenceType(), std::nullopt);
}

// ---- UnknownType (the class): the stored ctor bool? (default nullopt) ----

TEST(IsReferenceTypeTest, UnknownTypeClassDefaultsToUnknown) {
    // `class UnknownType` (the elaborated-type-specifier) targets the CLASS, not
    // the `UnknownType()` convenience function that shares the name (the D417
    // tag-vs-ordinary-namespace crux); a plain `std::make_shared<UnknownType>`
    // would resolve the template type-arg to the function.
    class UnknownType u(std::nullopt, "Foo", 0);
    EXPECT_EQ(u.IsReferenceType(), std::nullopt);
}

TEST(IsReferenceTypeTest, UnknownTypeClassStoresConfiguredBool) {
    class UnknownType asRef(std::optional<std::string>("System"), "Foo", 0,
                           std::optional<bool>(true));
    EXPECT_EQ(asRef.IsReferenceType(), std::optional<bool>(true));
    class UnknownType asVal(std::optional<std::string>("System"), "Bar", 1,
                            std::optional<bool>(false));
    EXPECT_EQ(asVal.IsReferenceType(), std::optional<bool>(false));
}

TEST(IsReferenceTypeTest, UnknownTypeEqualsComparesIsReferenceType) {
    // The C# `UnknownType.Equals` compares `isReferenceType`; two UnknownTypes
    // that differ only in `isReferenceType` are NOT equal.
    class UnknownType a(std::nullopt, "Foo", 0, std::optional<bool>(true));
    class UnknownType b(std::nullopt, "Foo", 0, std::optional<bool>(false));
    EXPECT_FALSE(a.Equals(b));
    class UnknownType c(std::nullopt, "Foo", 0, std::optional<bool>(true));
    EXPECT_TRUE(a.Equals(c));
}

// ---- ByReferenceType / PointerType: inherit the nullopt default (C# `return null`) ----

TEST(IsReferenceTypeTest, ByReferenceTypeIsUnknown) {
    auto br = std::make_shared<ByReferenceType>(Int32());
    EXPECT_EQ(br->IsReferenceType(), std::nullopt);
}

TEST(IsReferenceTypeTest, PointerTypeIsUnknown) {
    auto ptr = std::make_shared<PointerType>(Int32());
    EXPECT_EQ(ptr->IsReferenceType(), std::nullopt);
}

// ---- SimpleType / TypeParameter: inherit the nullopt default (not known) ----

TEST(IsReferenceTypeTest, SimpleTypeIsUnknown) {
    // A SimpleType is a name reference; the actual reference-ness is known only
    // after resolution, so the default nullopt ("not known") holds.
    auto s = std::make_shared<SimpleType>(TopLevelTypeName("System", "Foo"));
    EXPECT_EQ(s->IsReferenceType(), std::nullopt);
}

TEST(IsReferenceTypeTest, TypeParameterIsUnknown) {
    // An unconstrained type parameter's reference-ness is not known (the C#
    // AbstractTypeParameter derives it from constraints / effective base class,
    // which the minimal TypeParameter lacks).
    auto tp = std::make_shared<TypeParameter>(0, TypeParameter::OwnerKind::Class, "T");
    EXPECT_EQ(tp->IsReferenceType(), std::nullopt);
}

// ---- Consumer pattern: the SizeOfResolveResult `IsError => IsReferenceType != false` ----
// A value type (false) is NOT an error; a reference type (true) or an unknown
// (nullopt) IS an error. This mirrors the C# `bool? != bool` lifted comparison,
// which returns `bool` where `null != false` is `true`.

TEST(IsReferenceTypeTest, ConsumerPatternValueTypeIsNotError) {
    auto rt = Int32()->IsReferenceType();
    bool isError = rt != false;
    EXPECT_FALSE(isError);
}

TEST(IsReferenceTypeTest, ConsumerPatternReferenceTypeIsError) {
    auto rt = Object()->IsReferenceType();
    bool isError = rt != false;
    EXPECT_TRUE(isError);
}

TEST(IsReferenceTypeTest, ConsumerPatternUnknownIsError) {
    auto rt = ILSpy::Decompiler::TypeSystem::UnknownType()->IsReferenceType();
    bool isError = rt != false;
    EXPECT_TRUE(isError);
}
