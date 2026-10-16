// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
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

// Tests for the `OutVarDeclarationExpression` concrete node (cpp/.../Syntax/Expressions/
// OutVarDeclarationExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.cs) -- the
// `out_var_declaration_expression ::= type variable_initializer` shape (C# grammar section
// 12.20): a sealed `Expression` with two single, REQUIRED (non-nullable) `[Slot]` children --
// a `Type` `AstType` at flattened index 0 and a `Variable` `VariableInitializer` at flattened
// index 1 -- plus the `OutKeyword` const string aliased to `DirectionExpression.OutKeyword`,
// the generated empty + all-params ctors, and a hand-written `(AstType, string)` convenience
// ctor that creates a `new VariableInitializer(name)`. Exercises the `OutKeyword` const alias,
// both slot accessors/setters, the slot-storage contract, the shared-`Slots`-kind identity, the
// `AcceptVisitor` dispatch + virtuality, the depth-first walk (with the `VariableInitializer`
// recursing into its backing `NameToken` `Identifier`), the two-term `MatchRequired` `DoMatch`,
// the per-concrete-node `Clone`, and `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete node types, recursing via the inherited `VisitChildren` (the document-order walk of
// the slot-storage children). The `Type` slot is exercised with a `PrimitiveType` (the simplest
// leaf `AstType`); the `Variable` slot with a `VariableInitializer` whose `Name` is "x" (its
// backing `NameToken` `Identifier` is a visited child, so the walk recurses into `id:x`).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitOutVarDeclarationExpression(OutVarDeclarationExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-outvar>"); return; }
        trace.push_back("outvar");
        VisitChildren(node);
    }
    void VisitAsExpression(AsExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-as>"); return; }
        trace.push_back("as");
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-pt>"); return; }
        trace.push_back("prim:" + node->Keyword());
        VisitChildren(node);
    }
    void VisitVariableInitializer(VariableInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-varinit>"); return; }
        trace.push_back("varinit:" + node->Name());
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nre>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern). Uses `Match::CreateNew()` (NOT the default `Match()` -- the default holds a null
// capture vector and crashes the collection `DoMatch`'s checkpoint/restore; the D283 precedent).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// =========================================================================
// is-a
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, IsExpressionAndAstNodeNotStatementOrAstType) {
    OutVarDeclarationExpression e;
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

TEST(CSharp_OutVarDeclarationExpression, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<OutVarDeclarationExpression>);
    EXPECT_TRUE(std::is_final_v<OutVarDeclarationExpression>);
}

// =========================================================================
// Construction
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, DefaultCtorLeavesBothSlotsNull) {
    OutVarDeclarationExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.Variable(), nullptr);
    // The two required slots still occupy their flattened indices even when empty.
    EXPECT_EQ(e.GetChildCount(), 2);
}

TEST(CSharp_OutVarDeclarationExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    PrimitiveType* typePtr = type.get();
    VariableInitializer* varPtr = var.get();
    OutVarDeclarationExpression e(type.get(), var.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(e.Variable(), varPtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
    EXPECT_EQ(varPtr->Parent(), &e);
    EXPECT_EQ(varPtr->ChildIndex, 1);
}

TEST(CSharp_OutVarDeclarationExpression, HandWrittenStringCtorCreatesVariableInitializer) {
    // The hand-written `(AstType, string)` convenience ctor creates a `new VariableInitializer(name)`
    // and sets `Variable` to it. The created `VariableInitializer` is re-parented to this node,
    // carries the name "x" (its backing `NameToken`), and is NOT deleted on the parent's
    // destruction (the non-owning leak profile -- the `VariableDeclarationStatement` D270 precedent).
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    OutVarDeclarationExpression e(type.get(), std::string("x"));
    EXPECT_EQ(e.Type(), type.get());
    ASSERT_NE(e.Variable(), nullptr);
    EXPECT_EQ(e.Variable()->Name(), "x");
    EXPECT_EQ(e.Variable()->Parent(), &e);
    EXPECT_EQ(e.Variable()->ChildIndex, 1);
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

// =========================================================================
// OutKeyword const string (aliased to DirectionExpression.OutKeyword)
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, OutKeywordConstIsOutLiteral) {
    EXPECT_STREQ(OutVarDeclarationExpression::OutKeyword, "out");
}

TEST(CSharp_OutVarDeclarationExpression, OutKeywordAliasesDirectionExpressionOutKeyword) {
    EXPECT_EQ(OutVarDeclarationExpression::OutKeyword, DirectionExpression::OutKeyword);
}

// =========================================================================
// Accessors + slot storage
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, TypeSetterFillsAndParents) {
    OutVarDeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

TEST(CSharp_OutVarDeclarationExpression, VariableSetterFillsAndParents) {
    OutVarDeclarationExpression e;
    auto var = std::make_unique<VariableInitializer>(std::string("y"));
    e.Variable(var.get());
    EXPECT_EQ(e.Variable(), var.get());
    EXPECT_EQ(var->Parent(), &e);
    EXPECT_EQ(var->ChildIndex, 1);
}

TEST(CSharp_OutVarDeclarationExpression, NullTypeSetterClearsSlot) {
    OutVarDeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

TEST(CSharp_OutVarDeclarationExpression, NullVariableSetterClearsSlot) {
    OutVarDeclarationExpression e;
    auto var = std::make_unique<VariableInitializer>(std::string("y"));
    e.Variable(var.get());
    e.Variable(nullptr);
    EXPECT_EQ(e.Variable(), nullptr);
    EXPECT_EQ(var->Parent(), nullptr);
    EXPECT_EQ(var->ChildIndex, -1);
}

TEST(CSharp_OutVarDeclarationExpression, GetChildCountIsTwo) {
    OutVarDeclarationExpression e;
    EXPECT_EQ(e.GetChildCount(), 2);
}

TEST(CSharp_OutVarDeclarationExpression, GetChildReturnsSlots) {
    OutVarDeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    e.Type(type.get());
    e.Variable(var.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_EQ(e.GetChild(1), var.get());
    EXPECT_THROW(static_cast<void>(e.GetChild(2)), std::out_of_range);
    EXPECT_THROW(static_cast<void>(e.GetChild(-1)), std::out_of_range);
}

TEST(CSharp_OutVarDeclarationExpression, SetChildRoutesToSlots) {
    OutVarDeclarationExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    e.SetChild(1, var.get());
    EXPECT_EQ(e.Variable(), var.get());
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

TEST(CSharp_OutVarDeclarationExpression, GetChildSlotInfoReturnsSlotStatics) {
    OutVarDeclarationExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &OutVarDeclarationExpression::TypeSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &OutVarDeclarationExpression::VariableSlot);
    EXPECT_THROW(static_cast<void>(e.GetChildSlotInfo(2)), std::out_of_range);
}

TEST(CSharp_OutVarDeclarationExpression, GetCollectionByKindReturnsNullForEveryKind) {
    OutVarDeclarationExpression e;
    EXPECT_EQ(e.GetCollectionByKind(nullptr), nullptr);
    EXPECT_EQ(e.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(e.GetCollectionByKind(&Slots::Variable), nullptr);
}

// =========================================================================
// Slot statics (shared-`Slots`-kind identity + IsInstanceOfType)
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression e(type.get(), var.get());
    ASSERT_EQ(type->Slot(), &OutVarDeclarationExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
    ASSERT_EQ(var->Slot(), &OutVarDeclarationExpression::VariableSlot);
    EXPECT_EQ(var->Slot()->Kind(), &Slots::Variable);
}

TEST(CSharp_OutVarDeclarationExpression, SlotsAreInstanceOfTheirElementTypes) {
    PrimitiveType pt(std::string("int"));
    VariableInitializer vi(std::string("x"));
    NullReferenceExpression nre;
    EXPECT_TRUE(OutVarDeclarationExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(OutVarDeclarationExpression::TypeSlot.IsInstanceOfType(&vi));
    EXPECT_TRUE(OutVarDeclarationExpression::VariableSlot.IsInstanceOfType(&vi));
    EXPECT_FALSE(OutVarDeclarationExpression::VariableSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(OutVarDeclarationExpression::VariableSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(OutVarDeclarationExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(OutVarDeclarationExpression::VariableSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

TEST(CSharp_OutVarDeclarationExpression, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&OutVarDeclarationExpression::TypeSlot),
              static_cast<const CSharpSlotInfo*>(&OutVarDeclarationExpression::VariableSlot));
}

// =========================================================================
// AcceptVisitor dispatch
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, AcceptVisitorDispatchesToVisit) {
    OutVarDeclarationExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "outvar");
}

TEST(CSharp_OutVarDeclarationExpression, AcceptVisitorIsVirtualThroughAstNode) {
    OutVarDeclarationExpression e;
    AstNode* asNode = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "outvar");
}

TEST(CSharp_OutVarDeclarationExpression, AcceptVisitorIsVirtualThroughExpression) {
    OutVarDeclarationExpression e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asExpr->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "outvar");
}

// =========================================================================
// Depth-first walk
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, DepthFirstWalksChildrenInOrder) {
    // The walk visits the `outvar` node, then its two children in source declaration order:
    // `Type` (the `PrimitiveType` leaf) first, then `Variable` (the `VariableInitializer`, which
    // recurses into its backing `NameToken` `Identifier` -- the recurring string-name-[Slot]
    // backing-token-is-a-visited-child gotcha, the `VariableInitializer` D266 / `MemberType` D238
    // precedent). The trace is `outvar`, `prim:int`, `varinit:x`, `id:x`.
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression e(type.get(), var.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 4u);
    EXPECT_EQ(v.trace[0], "outvar");
    EXPECT_EQ(v.trace[1], "prim:int");
    EXPECT_EQ(v.trace[2], "varinit:x");
    EXPECT_EQ(v.trace[3], "id:x");
}

TEST(CSharp_OutVarDeclarationExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    OutVarDeclarationExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "outvar");
}

// =========================================================================
// DoMatch (two-term MatchRequired, no Any-wildcard)
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, DoMatchMatchesSameShape) {
    auto t1 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v1 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression a(t1.get(), v1.get());
    auto t2 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v2 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression b(t2.get(), v2.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_OutVarDeclarationExpression, DoMatchRejectsDifferentType) {
    auto t1 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v1 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression a(t1.get(), v1.get());
    auto t2 = std::make_unique<PrimitiveType>(std::string("string"));
    auto v2 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression b(t2.get(), v2.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_OutVarDeclarationExpression, DoMatchRejectsDifferentVariable) {
    auto t1 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v1 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression a(t1.get(), v1.get());
    auto t2 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v2 = std::make_unique<VariableInitializer>(std::string("y"));
    OutVarDeclarationExpression b(t2.get(), v2.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_OutVarDeclarationExpression, DoMatchRejectsDifferentVariableNameViaNameToken) {
    // The `Variable` `VariableInitializer`'s `DoMatch` matches the `Name` `MatchString` (the
    // `VariableInitializer` D266 generated `DoMatch` over `Name` + `Initializer`), so two
    // `VariableInitializer`s with different names reject even when both have no `Initializer`.
    auto t1 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v1 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression a(t1.get(), v1.get());
    auto t2 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v2 = std::make_unique<VariableInitializer>(std::string("xx"));
    OutVarDeclarationExpression b(t2.get(), v2.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_OutVarDeclarationExpression, DoMatchRejectsNonOutVarCandidate) {
    // A cross-type rejection: an `OutVarDeclarationExpression` pattern vs an `AsExpression`
    // candidate (a structural twin -- two required single slots in the `Expression` hierarchy --
    // but a distinct concrete type) rejects at the type-check gate.
    auto t1 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v1 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression a(t1.get(), v1.get());
    auto operand = std::make_unique<NullReferenceExpression>();
    auto t2 = std::make_unique<PrimitiveType>(std::string("int"));
    AsExpression b(operand.get(), t2.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_OutVarDeclarationExpression, DoMatchRejectsNullCandidate) {
    auto t1 = std::make_unique<PrimitiveType>(std::string("int"));
    auto v1 = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression a(t1.get(), v1.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// =========================================================================
// Clone
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, CloneDeepCopiesBothChildren) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression e(type.get(), var.get());
    std::unique_ptr<OutVarDeclarationExpression> copy(e.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &e);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->Type(), nullptr);
    ASSERT_NE(copy->Variable(), nullptr);
    // The cloned children are DISTINCT deep copies (not the source's).
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_NE(copy->Variable(), var.get());
    // The cloned `Type` `PrimitiveType` carries the keyword (the `Type()` slot accessor
    // returns the abstract `AstType*`, so a `dynamic_cast<PrimitiveType*>` reaches the
    // `Keyword()` member -- the recurring abstract-base-slot-accessor downcast gotcha, the
    // `NamespaceDeclaration` D293 / `Constraint` D283 precedent).
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
    // The cloned `Variable` `VariableInitializer` carries the name (and a distinct backing token).
    EXPECT_EQ(copy->Variable()->Name(), "x");
    ASSERT_NE(copy->Variable()->NameToken(), nullptr);
    EXPECT_NE(copy->Variable()->NameToken(), var->NameToken());
    // The cloned children are re-parented to the clone.
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    EXPECT_EQ(copy->Variable()->Parent(), copy.get());
    EXPECT_EQ(copy->Variable()->ChildIndex, 1);
    // The source children stay parented to the source (non-detach).
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(var->Parent(), &e);
}

TEST(CSharp_OutVarDeclarationExpression, CloneIsVirtualAndCovariant) {
    OutVarDeclarationExpression e;
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(dynamic_cast<OutVarDeclarationExpression*>(nodeCopy.get()), nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(dynamic_cast<OutVarDeclarationExpression*>(exprCopy.get()), nullptr);
}

TEST(CSharp_OutVarDeclarationExpression, CloneSkipsAbsentNullableChildren) {
    // `Clone` tolerates a missing child even though both slots are required (the invariant is
    // enforced by `CheckInvariant`, not by `Clone`). An empty node clones to an empty node.
    OutVarDeclarationExpression e;
    std::unique_ptr<OutVarDeclarationExpression> copy(e.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->Variable(), nullptr);
}

// =========================================================================
// CheckInvariant
// =========================================================================

TEST(CSharp_OutVarDeclarationExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    auto var = std::make_unique<VariableInitializer>(std::string("x"));
    OutVarDeclarationExpression e(type.get(), var.get());
    e.CheckInvariant();
    SUCCEED();
}

TEST(CSharp_OutVarDeclarationExpression, CheckInvariantPassesOnStringCtorNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    OutVarDeclarationExpression e(type.get(), std::string("x"));
    e.CheckInvariant();
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_OutVarDeclarationExpression, CheckInvariantRejectsEmptyNode) {
    // Both slots are required (non-nullable), so an empty node violates the required-slot
    // invariant (the `UnaryOperatorExpression` D231 required-slot behavior -- the assert fires
    // in debug).
    OutVarDeclarationExpression e;
    EXPECT_DEATH(e.CheckInvariant(), "");
}
#endif
