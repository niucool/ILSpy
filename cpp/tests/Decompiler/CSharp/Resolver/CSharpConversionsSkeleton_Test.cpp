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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` class skeleton (D512) -- the constructor (holds the compilation),
// the `Get(ICompilation)` per-compilation singleton factory (cached on the compilation's `CacheManager`),
// and the `TypePair` caching key struct (equality + hashing). The conversion methods are deferred.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::TypePair;
using ILSpy::Decompiler::CSharp::Resolver::TypePairEq;
using ILSpy::Decompiler::CSharp::Resolver::TypePairHash;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;

const ICompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

const IType& Int32Type() {
    static auto t = std::make_shared<KnownType>(KnownTypeCode::Int32);
    return *t;
}

const IType& StringType() {
    static auto t = std::make_shared<KnownType>(KnownTypeCode::String);
    return *t;
}

} // namespace

// ---------------------------------------------------------------------------
// Ctor holds the compilation -- `Compilation()` returns the ctor's compilation.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsTest, CtorHoldsCompilation) {
    CSharpConversions c(Compilation());
    EXPECT_EQ(&c.Compilation(), &Compilation());
}

// ---------------------------------------------------------------------------
// `Get` returns the same instance for the same compilation (the per-compilation singleton).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsTest, GetReturnsSameInstance) {
    CSharpConversions& a = CSharpConversions::Get(Compilation());
    CSharpConversions& b = CSharpConversions::Get(Compilation());
    EXPECT_EQ(&a, &b);
}

// ---------------------------------------------------------------------------
// `Get` returns a reference whose `Compilation()` is the ctor's compilation.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsTest, GetCompilationIsConsistent) {
    CSharpConversions& c = CSharpConversions::Get(Compilation());
    EXPECT_EQ(&c.Compilation(), &Compilation());
}

// ---------------------------------------------------------------------------
// TypePair: pointer-identity equality -- the same two pointers are equal.
// ---------------------------------------------------------------------------
TEST(TypePairTest, PointerIdentityEquals) {
    const IType& i = Int32Type();
    const IType& s = StringType();
    TypePair a(&i, &s);
    TypePair b(&i, &s);
    EXPECT_TRUE(a.Equals(b));
}

// ---------------------------------------------------------------------------
// TypePair: structural equality -- two distinct `KnownType(Int32)` instances are equal (the
// `IType::Equals` structural path), and a `TypePair(Int32, Int32)` differs from `(String, Int32)`.
// ---------------------------------------------------------------------------
TEST(TypePairTest, StructuralEquals) {
    // Two distinct KnownType instances for the same KnownTypeCode (Int32) -- structural-equal.
    auto intA = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto intB = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ASSERT_TRUE(intA->Equals(*intB));  // structural via Kind + KnownTypeCode

    TypePair pAB(intA.get(), intB.get());
    // Since intA and intB are structural-equal (both Int32), (intA,intB) and (intB,intA) are equal
    // (the slot roles don't matter when both types are equal).
    TypePair pBA(intB.get(), intA.get());
    EXPECT_TRUE(pAB.Equals(pBA));

    TypePair pAA(intA.get(), intA.get());
    TypePair pBB(intB.get(), intB.get());  // both slots structural-equal Int32
    EXPECT_TRUE(pAA.Equals(pBB));

    // Int32 vs String differ.
    auto str = std::make_shared<KnownType>(KnownTypeCode::String);
    EXPECT_FALSE(intA->Equals(*str));
    TypePair pIntStr(intA.get(), str.get());
    TypePair pIntInt(intA.get(), intB.get());
    EXPECT_FALSE(pIntStr.Equals(pIntInt));
}

// ---------------------------------------------------------------------------
// TypePair: null handling -- a null slot is unequal to a non-null slot.
// ---------------------------------------------------------------------------
TEST(TypePairTest, NullSlotsUnequalToNonNull) {
    const IType& i = Int32Type();
    TypePair pNull(&i, nullptr);
    TypePair pBoth(&i, &i);
    EXPECT_FALSE(pNull.Equals(pBoth));
    TypePair pNullNull(nullptr, nullptr);
    EXPECT_TRUE(pNullNull.Equals(TypePair{}));  // both default-null
}

// ---------------------------------------------------------------------------
// TypePair: hashing is consistent -- equal pairs hash the same (the hash==equals contract).
// ---------------------------------------------------------------------------
TEST(TypePairTest, HashConsistentWithEquals) {
    auto intA = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto intB = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ASSERT_TRUE(intA->Equals(*intB));
    TypePair pA(intA.get(), intA.get());
    TypePair pB(intB.get(), intB.get());
    ASSERT_TRUE(pA.Equals(pB));
    EXPECT_EQ(pA.GetHashCode(), pB.GetHashCode());
}

// ---------------------------------------------------------------------------
// TypePair: usable as an unordered_map key (TypePairHash + TypePairEq).
// ---------------------------------------------------------------------------
TEST(TypePairTest, UsableAsUnorderedMapKey) {
    const IType& i = Int32Type();
    const IType& s = StringType();
    std::unordered_map<TypePair, int, TypePairHash, TypePairEq> m;
    m[TypePair(&i, &s)] = 42;
    m[TypePair(&i, &i)] = 7;
    ASSERT_EQ(m.size(), 2u);
    EXPECT_EQ(m[TypePair(&i, &s)], 42);
    EXPECT_EQ(m[TypePair(&i, &i)], 7);
    // A structurally-equal key (distinct Int32 instance) finds the same slot.
    auto intB = std::make_shared<KnownType>(KnownTypeCode::Int32);
    ASSERT_TRUE(intB->Equals(i));
    EXPECT_EQ(m[TypePair(intB.get(), intB.get())], 7);
}
