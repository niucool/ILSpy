// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpResolver ResolveArrayCreation region
// (cpp/Decompiler/CSharp/Resolver/CSharpResolver.{hpp,cpp}, the port of
// CSharpResolver.cs lines 2880-2935 -- the LAST CSharpResolver region).
//
// The load-bearing cruxes:
//  (a) SIZE OVERLOAD: non-negative sizes materialize as ConstantResolveResults over the
//      REGISTERED Int32 (type-cache identity), a negative size is the ErrorResolveResult
//      UNKNOWN-ERROR SINGLETON (pointer identity), and the core overload runs over the
//      synthesized list;
//  (b) CORE: zero size arguments is the C# ArgumentException; a NULL element type infers
//      the best common type of the initializers through the TypeInference GetBestCommonType
//      free function (the resolver's OWN conversions instance, CSharp4 default), with the
//      C# `GetBestCommonType(null)` ArgumentNullException on the no-initializer shape;
//  (c) the array type is a fresh multi-dimensional ArrayType over the element type with
//      rank == dimensions -- a RANK-1 array is the port's SZ-array shape (the
//      ExpressionBuilder `newArr` convention);
//  (d) the size arguments are adjusted IN PLACE (the int32/uint32/int64/uint64
//      TryConvert chain; a non-convertible size is the Convert-under-None wrap over
//      Int32) and each initializer element re-binds through Convert (a convertible
//      non-constant initializer becomes a ConversionResolveResult; a non-convertible one
//      the None-conversion wrap; an IDENTITY match returns the SAME instance).

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace SU = ILSpy::Decompiler::Semantics;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::Semantics::ArrayCreateResolveResult;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

// The per-test fixture: a FRESH LookupCompilation (the lifetime discipline) with the
// registered known-type definitions the paths resolve through FindType (the type-cache
// model: one shared-managed instance per code).
struct Fixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupTypeDefinition> uint32;
    std::shared_ptr<LookupTypeDefinition> int64;
    std::shared_ptr<LookupTypeDefinition> uint64;
    std::shared_ptr<LookupTypeDefinition> stringDef;
    std::shared_ptr<LookupTypeDefinition> objectDef;
    std::shared_ptr<LookupTypeDefinition> nullableOfT;

    Fixture()
        : int32(MakeDef("Int32", KnownTypeCode::Int32, TypeKind::Struct)),
          uint32(MakeDef("UInt32", KnownTypeCode::UInt32, TypeKind::Struct)),
          int64(MakeDef("Int64", KnownTypeCode::Int64, TypeKind::Struct)),
          uint64(MakeDef("UInt64", KnownTypeCode::UInt64, TypeKind::Struct)),
          stringDef(MakeDef("String", KnownTypeCode::String, TypeKind::Class)),
          objectDef(MakeDef("Object", KnownTypeCode::Object, TypeKind::Class)),
          nullableOfT(MakeDef("Nullable", KnownTypeCode::NullableOfT, TypeKind::Struct))
    {
        // Every code the conversion machinery resolves through FindType must be
        // registered with a shared-managed definition (the bad_weak_ptr trap: an
        // unregistered code falls back to the compilation's non-shared unknownType_
        // stub whose shared_from_this throws -- the CSharpResolverInvocation/
        // CSharpResolverConvert fixture comments). The four size-adjust codes are the
        // CSharpResolverIndexer fixture set; Object is the user-defined-scan /
        // dynamic-erasure lookup; NullableOfT is the NullableType.Create lookup the
        // conversion helpers reach.
        compilation.RegisterKnownType(KnownTypeCode::Int32, int32.get());
        compilation.RegisterKnownType(KnownTypeCode::UInt32, uint32.get());
        compilation.RegisterKnownType(KnownTypeCode::Int64, int64.get());
        compilation.RegisterKnownType(KnownTypeCode::UInt64, uint64.get());
        compilation.RegisterKnownType(KnownTypeCode::String, stringDef.get());
        compilation.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
        compilation.RegisterKnownType(KnownTypeCode::NullableOfT, nullableOfT.get());
    }

    std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                                  KnownTypeCode code, TypeKind kind) const
    {
        return std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)), kind,
            Accessibility::Public, compilation, nullptr, code);
    }
};

// A plain (non-constant) expression over the given type -- the C# `new ResolveResult(type)`
// initializer shape whose conversion wraps are observable through the result.
std::shared_ptr<ResolveResult> MakeExpression(ITypePtr type)
{
    return std::make_shared<ResolveResult>(std::move(type));
}

// A compile-time constant over the given type.
std::shared_ptr<ResolveResult> MakeConstant(ITypePtr type, int value)
{
    return std::make_shared<SU::ConstantResolveResult>(std::move(type), std::any(value));
}

// The array type the result carries, down-cast (the ArrayCreateResolveResult's own type
// is always the fresh ArrayType the region built).
const ArrayType* ArrayTypeOf(const std::shared_ptr<ArrayCreateResolveResult>& result)
{
    return dynamic_cast<const ArrayType*>(&result->Type());
}

// ===========================================================================
// The int[] size convenience overload (CSharpResolver.cs line 2881)
// ===========================================================================

// Non-negative sizes materialize as ConstantResolveResults over the REGISTERED Int32
// (identity, not a fresh type), and the core overload runs over the synthesized list:
// a rank-2 array over the given element type (identity).
TEST(CSharpResolverArrayCreationTest, SizeOverloadFoldsNonNegativeSizesToConstants)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto result = resolver.ResolveArrayCreation(f.int32, {2, 3});

    ASSERT_NE(result, nullptr);
    const ArrayType* arrayType = ArrayTypeOf(result);
    ASSERT_NE(arrayType, nullptr);
    EXPECT_EQ(arrayType->Rank(), 2);
    EXPECT_FALSE(arrayType->IsSzArray());
    EXPECT_EQ(arrayType->Element().get(), f.int32.get());
    EXPECT_EQ(&result->Type(), arrayType);

    const auto& sizes = result->SizeArguments();
    ASSERT_EQ(sizes.size(), 2u);
    for (int i = 0; i < 2; i++) {
        auto* constant = dynamic_cast<const SU::ConstantResolveResult*>(sizes[i].get());
        ASSERT_NE(constant, nullptr) << "size argument " << i << " should be a constant";
        EXPECT_EQ(&constant->Type(), f.int32.get());
        ASSERT_TRUE(constant->ConstantValue().has_value());
        EXPECT_EQ(std::any_cast<int>(constant->ConstantValue()), i + 2);
    }
    EXPECT_FALSE(result->InitializerElements().has_value());
}

// A negative size is the ErrorResolveResult.UnknownError SINGLETON (pointer identity with
// the static the non-overloadable arms return), not a constant and not a fresh error.
TEST(CSharpResolverArrayCreationTest, SizeOverloadNegativeSizeIsTheUnknownErrorSingleton)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto result = resolver.ResolveArrayCreation(f.int64, {-1, 4});

    ASSERT_NE(result, nullptr);
    const auto& sizes = result->SizeArguments();
    ASSERT_EQ(sizes.size(), 2u);
    // The error size slot: the AdjustArrayAccessArguments conversion-failure arm wraps
    // the UnknownError singleton into a ConversionResolveResult over Int32 (the C#
    // `Convert(arguments[i], Int32, Conversion.None)` wrap -- the singleton is carried
    // as the wrap's Input by pointer identity).
    auto* wrapped = dynamic_cast<const ConversionResolveResult*>(sizes[0].get());
    ASSERT_NE(wrapped, nullptr);
    EXPECT_EQ(&wrapped->Type(), f.int32.get());
    EXPECT_EQ(wrapped->ConversionProperty(), Conversions::None().get());
    EXPECT_EQ(wrapped->Input(), &ErrorResolveResult::UnknownError());
    auto* constant = dynamic_cast<const SU::ConstantResolveResult*>(sizes[1].get());
    ASSERT_NE(constant, nullptr);
    EXPECT_EQ(std::any_cast<int>(constant->ConstantValue()), 4);
    // The element type flows through untouched (the synthesized list drives the core).
    EXPECT_EQ(ArrayTypeOf(result)->Element().get(), f.int64.get());
}

// A rank-1 size list builds the port's SZ-array shape (the ExpressionBuilder `newArr`
// convention: one dimension is the SZArray ctor; the C# rank-1 ArrayType's `[` + 0
// commas + `]` suffix is observationally identical).
TEST(CSharpResolverArrayCreationTest, SizeOverloadRankOneIsSzArrayShape)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    auto result = resolver.ResolveArrayCreation(f.stringDef, {5});

    const ArrayType* arrayType = ArrayTypeOf(result);
    ASSERT_NE(arrayType, nullptr);
    EXPECT_TRUE(arrayType->IsSzArray());
    EXPECT_EQ(arrayType->Rank(), 1);
    EXPECT_EQ(arrayType->Element().get(), f.stringDef.get());
}

// ===========================================================================
// The core overload (CSharpResolver.cs line 2910)
// ===========================================================================

// Zero size arguments is the C# `ArgumentException("sizeArguments.Length must not be 0")`.
TEST(CSharpResolverArrayCreationTest, CoreZeroSizeArgumentsThrows)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    EXPECT_THROW(
        resolver.ResolveArrayCreation(f.int32,
                                      std::vector<std::shared_ptr<ResolveResult>>{}),
        std::invalid_argument);
}

// Each initializer re-binds through Convert to the element type: a convertible
// non-constant initializer becomes a ConversionResolveResult over the element type
// carrying a VALID implicit conversion.
TEST(CSharpResolverArrayCreationTest, CoreConvertsInitializersToElementType)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto initializer = MakeExpression(f.int32);

    std::vector<std::shared_ptr<ResolveResult>> initializers{initializer};
    auto result =
        resolver.ResolveArrayCreation(f.int64, {MakeConstant(f.int32, 1)}, initializers);

    ASSERT_NE(result, nullptr);
    const ArrayType* arrayType = ArrayTypeOf(result);
    ASSERT_NE(arrayType, nullptr);
    EXPECT_EQ(arrayType->Element().get(), f.int64.get());

    // The size argument stayed the Int32 constant (the TryConvert int32 identity arm).
    ASSERT_EQ(result->SizeArguments().size(), 1u);
    EXPECT_EQ(&result->SizeArguments()[0]->Type(), f.int32.get());

    ASSERT_TRUE(result->InitializerElements().has_value());
    const auto& converted = *result->InitializerElements();
    ASSERT_EQ(converted.size(), 1u);
    auto* conversion = dynamic_cast<const ConversionResolveResult*>(converted[0].get());
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(&conversion->Type(), f.int64.get());
    EXPECT_TRUE(conversion->ConversionProperty()->IsValid());
    // The input is carried as the conversion's Input (the wrap, not a re-resolve).
    EXPECT_EQ(&conversion->Input()->Type(), f.int32.get());
}

// A non-convertible initializer is the Convert-under-None wrap (the C# `Convert` call
// does not check -- the None conversion carries the failure through the result).
TEST(CSharpResolverArrayCreationTest, CoreNoneConversionWrapsNonConvertibleInitializer)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto initializer = MakeExpression(f.int32);

    std::vector<std::shared_ptr<ResolveResult>> initializers{initializer};
    auto result =
        resolver.ResolveArrayCreation(f.stringDef, {MakeConstant(f.int32, 1)}, initializers);

    ASSERT_TRUE(result->InitializerElements().has_value());
    const auto& converted = *result->InitializerElements();
    ASSERT_EQ(converted.size(), 1u);
    auto* conversion = dynamic_cast<const ConversionResolveResult*>(converted[0].get());
    ASSERT_NE(conversion, nullptr);
    EXPECT_EQ(&conversion->Type(), f.stringDef.get());
    EXPECT_EQ(conversion->ConversionProperty(), Conversions::None().get());
}

// The size arguments are adjusted IN PLACE: a size over Int64 survives through the
// TryConvert int64 arm (the identity unwrap returns the SAME instance), a size over a
// reference type exhausts all four arms and is the Convert-under-None wrap over Int32.
TEST(CSharpResolverArrayCreationTest, CoreAdjustsSizesThroughTheTryConvertChain)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto longSize = MakeExpression(f.int64);
    auto stringSize = MakeExpression(f.stringDef);

    auto result = resolver.ResolveArrayCreation(
        f.int32, {longSize, stringSize}, std::nullopt);

    ASSERT_EQ(result->SizeArguments().size(), 2u);
    // The Int64 size: the int64 arm converted it (identity unwrap keeps the instance).
    EXPECT_EQ(&result->SizeArguments()[0]->Type(), f.int64.get());
    EXPECT_EQ(result->SizeArguments()[0].get(), longSize.get());
    // The String size: the conversion-failure arm wrapped it over Int32 with None.
    auto* wrapped = dynamic_cast<const ConversionResolveResult*>(result->SizeArguments()[1].get());
    ASSERT_NE(wrapped, nullptr);
    EXPECT_EQ(&wrapped->Type(), f.int32.get());
    EXPECT_EQ(wrapped->ConversionProperty(), Conversions::None().get());
}

// A NULL element type with a single initializer short-circuits the inference to that
// initializer's own type (the GetBestCommonType one-expression path) -- the array is an
// SZ array over the initializer's type and the initializer converts identically (the
// SAME instance, the Convert identity unwrap).
TEST(CSharpResolverArrayCreationTest, ImplicitElementTypeSingleInitializerShortCircuits)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto initializer = MakeExpression(f.int64);

    std::vector<std::shared_ptr<ResolveResult>> initializers{initializer};
    auto result = resolver.ResolveArrayCreation(nullptr, {MakeConstant(f.int32, 1)},
                                                initializers);

    const ArrayType* arrayType = ArrayTypeOf(result);
    ASSERT_NE(arrayType, nullptr);
    EXPECT_TRUE(arrayType->IsSzArray());
    EXPECT_EQ(arrayType->Element().get(), f.int64.get());

    ASSERT_TRUE(result->InitializerElements().has_value());
    const auto& converted = *result->InitializerElements();
    ASSERT_EQ(converted.size(), 1u);
    EXPECT_EQ(converted[0].get(), initializer.get());
}

// A NULL element type with mixed initializers infers the best common type through the
// dummy-TP inference: int + long lower-bound the dummy and the fix picks long (the
// registered Int64, identity).
TEST(CSharpResolverArrayCreationTest, ImplicitElementTypeTwoInitializersInferInt64)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);
    auto first = MakeExpression(f.int32);
    auto second = MakeExpression(f.int64);

    std::vector<std::shared_ptr<ResolveResult>> initializers{first, second};
    auto result = resolver.ResolveArrayCreation(nullptr, {MakeConstant(f.int32, 1)},
                                                initializers);

    const ArrayType* arrayType = ArrayTypeOf(result);
    ASSERT_NE(arrayType, nullptr);
    EXPECT_EQ(arrayType->Element().get(), f.int64.get());

    ASSERT_TRUE(result->InitializerElements().has_value());
    const auto& converted = *result->InitializerElements();
    ASSERT_EQ(converted.size(), 2u);
    // The Int32 initializer converts (the implicit numeric conversion wrap over the
    // inferred Int64); the Int64 initializer matches the inference EXACTLY, so its
    // Convert is the identity unwrap -- the SAME instance, no wrap.
    auto* firstConversion = dynamic_cast<const ConversionResolveResult*>(converted[0].get());
    ASSERT_NE(firstConversion, nullptr);
    EXPECT_EQ(&firstConversion->Type(), f.int64.get());
    EXPECT_EQ(converted[1].get(), second.get());
    EXPECT_EQ(&converted[1]->Type(), f.int64.get());
}

// The C# `GetBestCommonType(null)` on a no-initializer implicitly-typed creation throws
// ArgumentNullException -- the port's std::invalid_argument (the guard convention).
TEST(CSharpResolverArrayCreationTest, ImplicitElementTypeNullInitializersThrows)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    EXPECT_THROW(resolver.ResolveArrayCreation(nullptr, {MakeConstant(f.int32, 1)},
                                               std::nullopt),
                 std::invalid_argument);
}

// An EMPTY (present) initializer list infers the UnknownType null object (the boundless
// dummy fix fails) -- the array is an SZ array over an Unknown-kind type; the empty
// initializer list stays empty. The present-empty vs nullopt distinction is load-bearing.
TEST(CSharpResolverArrayCreationTest, ImplicitElementTypeEmptyInitializerListInfersUnknownType)
{
    Fixture f;
    CSharpResolver resolver(f.compilation);

    // A PRESENT empty initializer list (the C# non-null empty array) -- NOT nullopt
    // (the nullopt shape is the no-initializer state, which throws instead).
    std::vector<std::shared_ptr<ResolveResult>> emptyInitializers;
    auto result = resolver.ResolveArrayCreation(
        nullptr, {MakeConstant(f.int32, 1)},
        std::optional<std::vector<std::shared_ptr<ResolveResult>>>(
            std::move(emptyInitializers)));

    const ArrayType* arrayType = ArrayTypeOf(result);
    ASSERT_NE(arrayType, nullptr);
    EXPECT_EQ(arrayType->Element()->Kind(), TypeKind::Unknown);
    EXPECT_TRUE(result->InitializerElements().has_value());
    EXPECT_EQ(result->InitializerElements()->size(), 0u);
}

} // namespace
