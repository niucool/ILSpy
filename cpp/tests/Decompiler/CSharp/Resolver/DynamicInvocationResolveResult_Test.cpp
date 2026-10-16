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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `DynamicInvocationResolveResult` (the seventh `cpp/Decompiler/CSharp/Resolver/`
// leaf, the port of ICSharpCode.Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.cs
// -- the result of an invocation of a member of a `dynamic` object). The class derives from
// `ResolveResult` (D424) with a `SpecialType.Dynamic` base type, carries a `ResolveResult
// Target` (a held child NOT exposed via `GetChildResults`), a `DynamicInvocationType`
// InvocationType (the Invocation/Indexing/ObjectCreation enum), an `Arguments` list, an
// `InitializerStatements` list, and a nullable `IMember Symbol`. The C# does NOT override
// `GetChildResults` (the inherited base default returns empty) or `IsError` (the inherited
// base default returns false), but DOES override `ToString` with the custom format
// "[Dynamic invocation ]". The tests pin the `DynamicInvocationType` enum values, the
// ctor-stores-fields + forwards-dynamic-type contract, the `Type()` is `TypeKind::Dynamic`
// with `IsReferenceType() == true`, the custom `ToString` format, the inherited
// `GetChildResults` (empty) and `IsError` (false) base defaults, the `ShallowClone`
// runtime-type preservation + shared/copied fields, the polymorphic dispatch, and the
// class-shape static-asserts.

#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

// Convenience: a `KnownType(Object)` (a reference type) for use as a target's type.
TS::ITypePtr MakeObjectType()
{
    return std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
}

// A non-error `Target` (a `TypeResolveResult` over a known type).
std::shared_ptr<Sem::ResolveResult> MakeObjectTarget()
{
    return std::make_shared<Sem::TypeResolveResult>(MakeObjectType());
}

} // namespace

// ===========================================================================
// DynamicInvocationType -- the three invocation kinds are the declaration-order 0..2.
// ===========================================================================

TEST(DynamicInvocationTypeTest, MembersMatchCSharpDeclarationOrderValues)
{
    EXPECT_EQ(static_cast<std::int32_t>(Res::DynamicInvocationType::Invocation), 0);
    EXPECT_EQ(static_cast<std::int32_t>(Res::DynamicInvocationType::Indexing), 1);
    EXPECT_EQ(static_cast<std::int32_t>(Res::DynamicInvocationType::ObjectCreation), 2);
}

TEST(DynamicInvocationTypeTest, MembersAreDistinct)
{
    EXPECT_NE(Res::DynamicInvocationType::Invocation, Res::DynamicInvocationType::Indexing);
    EXPECT_NE(Res::DynamicInvocationType::Invocation, Res::DynamicInvocationType::ObjectCreation);
    EXPECT_NE(Res::DynamicInvocationType::Indexing, Res::DynamicInvocationType::ObjectCreation);
}

// ===========================================================================
// ctor -- stores the target / invocationType / arguments / initializerStatements /
// symbol and forwards SpecialType.Dynamic.
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, ConstructorStoresAllFields)
{
    auto target = MakeObjectTarget();
    auto arg1 = MakeObjectTarget();
    auto init1 = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(
        target, Res::DynamicInvocationType::ObjectCreation,
        { arg1 }, { init1 });
    EXPECT_EQ(rr.Target(), target.get());
    EXPECT_EQ(rr.InvocationType(), Res::DynamicInvocationType::ObjectCreation);
    ASSERT_EQ(rr.Arguments().size(), 1u);
    EXPECT_EQ(rr.Arguments()[0].get(), arg1.get());
    ASSERT_EQ(rr.InitializerStatements().size(), 1u);
    EXPECT_EQ(rr.InitializerStatements()[0].get(), init1.get());
    EXPECT_EQ(rr.Symbol(), nullptr);
}

TEST(DynamicInvocationResolveResultTest, ConstructorDefaultsArgumentsAndInitializerStatementsToEmpty)
{
    auto target = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(target, Res::DynamicInvocationType::Invocation);
    EXPECT_TRUE(rr.Arguments().empty());
    EXPECT_TRUE(rr.InitializerStatements().empty());
}

TEST(DynamicInvocationResolveResultTest, ConstructorAcceptsNullTarget)
{
    // The C# does NOT guard `target` -- it may be null.
    Res::DynamicInvocationResolveResult rr(nullptr, Res::DynamicInvocationType::Invocation);
    EXPECT_EQ(rr.Target(), nullptr);
}

TEST(DynamicInvocationResolveResultTest, ConstructorAcceptsEachInvocationType)
{
    for (auto type : { Res::DynamicInvocationType::Invocation,
                       Res::DynamicInvocationType::Indexing,
                       Res::DynamicInvocationType::ObjectCreation }) {
        Res::DynamicInvocationResolveResult rr(MakeObjectTarget(), type);
        EXPECT_EQ(rr.InvocationType(), type);
    }
}

// ===========================================================================
// Type -- the base SpecialType.Dynamic (TypeKind::Dynamic, isReferenceType: true).
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, TypeIsSpecialTypeDynamic)
{
    Res::DynamicInvocationResolveResult rr(MakeObjectTarget(), Res::DynamicInvocationType::Invocation);
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::Dynamic);
}

TEST(DynamicInvocationResolveResultTest, TypeIsReferenceType)
{
    // The C# SpecialType.Dynamic singleton carries isReferenceType: true; the C++ port
    // forwards std::make_shared<SpecialType>(TypeKind::Dynamic, true) to the base.
    Res::DynamicInvocationResolveResult rr(MakeObjectTarget(), Res::DynamicInvocationType::Invocation);
    ASSERT_TRUE(rr.Type().IsReferenceType().has_value());
    EXPECT_TRUE(*rr.Type().IsReferenceType());
}

// ===========================================================================
// ToString -- the custom "[Dynamic invocation ]" format (NOT the inherited bracket).
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, ToStringUsesDynamicInvocationFormat)
{
    Res::DynamicInvocationResolveResult rr(MakeObjectTarget(), Res::DynamicInvocationType::Invocation);
    EXPECT_EQ(rr.ToString(), "[Dynamic invocation ]");
}

TEST(DynamicInvocationResolveResultTest, ToStringIsConstantRegardlessOfInvocationType)
{
    // The C# format string "[Dynamic invocation ]" is a fixed literal -- it does not
    // substitute the InvocationType, the target, or the arguments; all three invocation
    // kinds yield the same string.
    Res::DynamicInvocationResolveResult rr1(nullptr, Res::DynamicInvocationType::Invocation);
    Res::DynamicInvocationResolveResult rr2(nullptr, Res::DynamicInvocationType::Indexing);
    Res::DynamicInvocationResolveResult rr3(nullptr, Res::DynamicInvocationType::ObjectCreation);
    EXPECT_EQ(rr1.ToString(), "[Dynamic invocation ]");
    EXPECT_EQ(rr2.ToString(), "[Dynamic invocation ]");
    EXPECT_EQ(rr3.ToString(), "[Dynamic invocation ]");
}

// ===========================================================================
// GetChildResults -- the inherited base default (empty; the C# does NOT override it).
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, GetChildResultsIsEmptyByDefault)
{
    // The C# does NOT override GetChildResults -- the inherited ResolveResult base default
    // returns empty. The Target is held but NOT exposed as a child.
    auto target = MakeObjectTarget();
    auto arg = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(target, Res::DynamicInvocationType::Invocation, { arg });
    EXPECT_TRUE(rr.GetChildResults().empty());
}

// ===========================================================================
// IsError -- the inherited base default (false; the C# does NOT override it).
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, IsErrorIsFalseByDefault)
{
    Res::DynamicInvocationResolveResult rr(MakeObjectTarget(), Res::DynamicInvocationType::Invocation);
    EXPECT_FALSE(rr.IsError());
}

// ===========================================================================
// ShallowClone -- preserves the runtime type, shares the Target, copies the
// InvocationType, shares the Arguments/InitializerStatements, copies the Symbol.
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, ShallowClonePreservesRuntimeType)
{
    auto target = MakeObjectTarget();
    auto arg = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(
        target, Res::DynamicInvocationType::ObjectCreation, { arg });
    auto clone = rr.ShallowClone();
    EXPECT_NE(clone, nullptr);
    EXPECT_EQ(dynamic_cast<Res::DynamicInvocationResolveResult*>(clone.get()), clone.get());
}

TEST(DynamicInvocationResolveResultTest, ShallowCloneSharesTargetAndCopiesInvocationType)
{
    auto target = MakeObjectTarget();
    auto arg = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(
        target, Res::DynamicInvocationType::Indexing, { arg });
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::DynamicInvocationResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->Target(), target.get());
    EXPECT_EQ(cloned->InvocationType(), Res::DynamicInvocationType::Indexing);
}

TEST(DynamicInvocationResolveResultTest, ShallowCloneSharedArgumentsAndInitializerStatements)
{
    auto target = MakeObjectTarget();
    auto arg = MakeObjectTarget();
    auto init = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(
        target, Res::DynamicInvocationType::ObjectCreation, { arg }, { init });
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<Res::DynamicInvocationResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    ASSERT_EQ(cloned->Arguments().size(), 1u);
    EXPECT_EQ(cloned->Arguments()[0].get(), arg.get());
    ASSERT_EQ(cloned->InitializerStatements().size(), 1u);
    EXPECT_EQ(cloned->InitializerStatements()[0].get(), init.get());
}

TEST(DynamicInvocationResolveResultTest, ShallowCloneIsDistinctInstance)
{
    Res::DynamicInvocationResolveResult rr(MakeObjectTarget(), Res::DynamicInvocationType::Invocation);
    auto clone = rr.ShallowClone();
    EXPECT_NE(static_cast<Sem::ResolveResult*>(&rr), clone.get());
}

// ===========================================================================
// Polymorphic dispatch through a ResolveResult* base pointer.
// ===========================================================================

TEST(DynamicInvocationResolveResultTest, DispatchesThroughResolveResultBasePointer)
{
    auto target = MakeObjectTarget();
    Res::DynamicInvocationResolveResult rr(target, Res::DynamicInvocationType::Invocation);
    Sem::ResolveResult* base = &rr;
    EXPECT_EQ(base->Type().Kind(), TS::TypeKind::Dynamic);
    EXPECT_EQ(base->ToString(), "[Dynamic invocation ]");
    EXPECT_TRUE(base->GetChildResults().empty());
    EXPECT_FALSE(base->IsError());
}

// ===========================================================================
// Class shape -- is_base_of / not-final / has-virtual-destructor / is-polymorphic.
// ===========================================================================

static_assert(std::is_base_of<Sem::ResolveResult, Res::DynamicInvocationResolveResult>::value,
              "DynamicInvocationResolveResult derives from ResolveResult");
static_assert(!std::is_final<Res::DynamicInvocationResolveResult>::value,
              "DynamicInvocationResolveResult is not final (the C# class is unsealed)");
static_assert(std::has_virtual_destructor<Sem::ResolveResult>::value,
              "ResolveResult has a virtual destructor (deletion through base is safe)");
static_assert(std::is_polymorphic<Res::DynamicInvocationResolveResult>::value,
              "DynamicInvocationResolveResult is polymorphic (supports dynamic_cast)");
