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

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"

#include <gtest/gtest.h>

#include <string_view>

using namespace ILSpy::Decompiler::CSharp::Syntax;

namespace {
// Two distinct concrete node types deriving from the (polymorphic) AstNode base, so
// the slot's is-a test (`dynamic_cast<const T*>`) has both a match and a mismatch to
// distinguish. The DoMatch overrides are unused here but AstNode is abstract.
class StubExpr : public AstNode {
public:
    bool DoMatch(AstNode* /*other*/, PatternMatching::Match /*match*/) override { return false; }
};

class StubStmt : public AstNode {
public:
    bool DoMatch(AstNode* /*other*/, PatternMatching::Match /*match*/) override { return false; }
};

// A subtype of StubExpr, so the is-a test also accepts a subtype (the CLR
// `IsInstanceOfType` is-a semantics the `dynamic_cast` reproduces).
class StubDerivedExpr : public StubExpr {
public:
    bool DoMatch(AstNode* /*other*/, PatternMatching::Match /*match*/) override { return false; }
};

// Slot constants at namespace scope (single-TU test, so `const` internal linkage
// gives the one address the pointer-identity comparison relies on; the real slot
// statics in node headers use `inline` for cross-TU identity).
const CSharpSlotInfoT<StubExpr> SlotsLeft{"Left", false, nullptr, false};
const CSharpSlotInfoT<StubExpr> SlotsRight{"Right", false, nullptr, false};
const CSharpSlotInfoT<StubExpr> SlotsParameters{"Parameters", true, nullptr, false};
const CSharpSlotInfoT<StubExpr> BinaryLeftSlot{"Left", false, &SlotsLeft, false};
} // namespace

// ---- Construction / fields -------------------------------------------------

TEST(CSharp_CSharpSlotInfo, SingleSlotRecordsNameAndFlags) {
    CSharpSlotInfoT<StubExpr> slot("Left", false, nullptr, false);
    EXPECT_EQ(slot.Name(), "Left");
    EXPECT_FALSE(slot.IsCollection());
    EXPECT_EQ(slot.Kind(), nullptr);
    EXPECT_FALSE(slot.IsOptional());
    EXPECT_EQ(slot.ToString(), "Left");
}

TEST(CSharp_CSharpSlotInfo, CollectionSlotIsOptional) {
    // A collection slot points at a canonical kind and is optional (it may hold zero
    // children).
    CSharpSlotInfoT<StubExpr> slot("Parameters", true, &SlotsParameters, true);
    EXPECT_TRUE(slot.IsCollection());
    EXPECT_TRUE(slot.IsOptional());
    EXPECT_EQ(slot.Kind(), &SlotsParameters);
}

TEST(CSharp_CSharpSlotInfo, OptionalSingleSlot) {
    CSharpSlotInfoT<StubExpr> slot("Condition", false, nullptr, true);
    EXPECT_FALSE(slot.IsCollection());
    EXPECT_TRUE(slot.IsOptional());
}

// ---- IsInstanceOfType (the dynamic_cast is-a check) ------------------------

TEST(CSharp_CSharpSlotInfo, IsInstanceOfTypeAcceptsExactType) {
    CSharpSlotInfoT<StubExpr> slot("Left", false, nullptr, false);
    StubExpr e;
    EXPECT_TRUE(slot.IsInstanceOfType(&e));
}

TEST(CSharp_CSharpSlotInfo, IsInstanceOfTypeAcceptsSubtype) {
    // The C# `IsInstanceOfType` is an is-a test; `dynamic_cast` reproduces it, so a
    // slot declared for `StubExpr` accepts a `StubDerivedExpr`.
    CSharpSlotInfoT<StubExpr> slot("Left", false, nullptr, false);
    StubDerivedExpr e;
    EXPECT_TRUE(slot.IsInstanceOfType(&e));
}

TEST(CSharp_CSharpSlotInfo, IsInstanceOfTypeRejectsUnrelatedType) {
    CSharpSlotInfoT<StubExpr> slot("Left", false, nullptr, false);
    StubStmt s;
    EXPECT_FALSE(slot.IsInstanceOfType(&s));
}

TEST(CSharp_CSharpSlotInfo, IsInstanceOfTypeRejectsNull) {
    // The C# `Type.IsInstanceOfType(null) == false`.
    CSharpSlotInfoT<StubExpr> slot("Left", false, nullptr, false);
    EXPECT_FALSE(slot.IsInstanceOfType(nullptr));
}

// ---- Kind identity (the pointer comparison GetCollectionByKind uses) -------

TEST(CSharp_CSharpSlotInfo, PerNodeSlotKindPointsAtSlotsConstant) {
    // A Slots constant is its own canonical kind (Kind null); a per-node slot points
    // at it, and `node.Slot.Kind == &Slots::X` identifies a position polymorphically.
    EXPECT_EQ(SlotsLeft.Kind(), nullptr);
    ASSERT_NE(BinaryLeftSlot.Kind(), nullptr);
    EXPECT_EQ(BinaryLeftSlot.Kind(), &SlotsLeft);            // pointer identity
    EXPECT_EQ(BinaryLeftSlot.Kind()->Name(), "Left");
}

TEST(CSharp_CSharpSlotInfo, DistinctSlotsHaveDistinctAddresses) {
    // Slot identity is by address; two distinct slot statics are not the same slot.
    EXPECT_NE(&SlotsLeft, &SlotsRight);
    EXPECT_EQ(SlotsLeft.Name(), "Left");
    EXPECT_EQ(SlotsRight.Name(), "Right");
}
