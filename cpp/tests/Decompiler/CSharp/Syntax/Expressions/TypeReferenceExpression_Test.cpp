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

// Tests for the single-`[Slot("Type")]`-`AstType` `Expression` node group
// (cpp/.../Expressions/TypeReferenceExpression.hpp + TypeOfExpression.hpp +
// DefaultValueExpression.hpp + SizeOfExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/{TypeReferenceExpression,TypeOfExpression,
// DefaultValueExpression,SizeOfExpression}.cs) -- the next in-order Phase-5 piece per the
// D244 plan ("the remaining single-[Slot('Type')]-AstType Expression nodes: TypeReferenceExpression
// -- a single required AstType Type slot, no const keyword; TypeOfExpression/DefaultValueExpression/
// SizeOfExpression -- a single required AstType Type slot + a typeof/default/sizeof const keyword
// string; all reusing the already-ported Slots::Type kind -- the simplest slot-bearing AstType-bearing
// Expression shape, one required single slot"). All four share the EXACT same shape -- one required
// (non-nullable) `AstType` `Type` slot at flattened index 0, no scalar, reusing `Slots::Type` -- so they
// form a natural quartet; `TypeReferenceExpression` carries no const keyword, the other three each carry
// a `typeof`/`default`/`sizeof` keyword const string. The four suites are kept in one file (the D234
// "natural pair" framing extended to a quartet): they share the recording visitor and the `DoMatch`
// helper, and each suite is independently selectable (`--gtest_filter=CSharp_TypeReferenceExpression.*`
// / `CSharp_TypeOfExpression.*` / `CSharp_DefaultValueExpression.*` / `CSharp_SizeOfExpression.*`).
//
// Each suite exercises the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` over one required single slot at const index 0), the `Type` accessor (NO
// `class Expression`/`class Type` elaborated-type-specifier name-shadowing crux -- the `Type()`
// accessor does not collide with the `AstType` base type, the Attribute D240 lesson), the const-keyword
// string (the three keyword nodes), the generated `DoMatch` over ONE NON-nullable recursive child (the
// direct dispatch via `MatchRequired`, with the defensive null guard), the `Clone` deep-copy +
// re-parent of the one child, the `AcceptVisitor` dispatch, and the depth-first walk (the node, then
// the `Type` `PrimitiveType` child). There is no scalar enum, so `DoMatch` has no `Any`-wildcard term.
// The required-slot invariant means `CheckInvariant` is only exercised on a filled node (an empty node
// would assert "required slot must not be empty").

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the four single-Type-slot nodes (and the
// cross-sibling `AsExpression`/`IsExpression` for the cross-type tests) and the nodes they
// hold, recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order (document order). The `Type` slot is
// exercised with a `PrimitiveType` (the simplest leaf `AstType`), so `VisitPrimitiveType`
// records the keyword.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitTypeReferenceExpression(TypeReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-typeref>"); return; }
        trace.push_back("typeref");
        VisitChildren(node);
    }
    void VisitTypeOfExpression(TypeOfExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-typeof>"); return; }
        trace.push_back("typeof");
        VisitChildren(node);
    }
    void VisitDefaultValueExpression(DefaultValueExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-default>"); return; }
        trace.push_back("default");
        VisitChildren(node);
    }
    void VisitSizeOfExpression(SizeOfExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-sizeof>"); return; }
        trace.push_back("sizeof");
        VisitChildren(node);
    }
    void VisitAsExpression(AsExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-as>"); return; }
        trace.push_back("as");
        VisitChildren(node);
    }
    void VisitIsExpression(IsExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-is>"); return; }
        trace.push_back("is");
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-pt>"); return; }
        trace.push_back("prim:" + node->Keyword());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nre>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the
// D220 pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// =====================================================================
// TypeReferenceExpression (no const keyword -- the canonical single-Type-slot node)
// =====================================================================

// ---- is-a --------------------------------------------------------------

// `TypeReferenceExpression` is an `AstNode` and an `Expression`; it is NOT an `AstType`.
TEST(CSharp_TypeReferenceExpression, IsAstNodeAndExpressionNotAstType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

// ---- Construction -----------------------------------------------------

TEST(CSharp_TypeReferenceExpression, DefaultCtor) {
    TypeReferenceExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_TypeReferenceExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    PrimitiveType* typePtr = type.get();
    TypeReferenceExpression e(type.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
}

// ---- Accessors + slot storage -----------------------------------------

TEST(CSharp_TypeReferenceExpression, TypeSetterFillsAndParents) {
    TypeReferenceExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

TEST(CSharp_TypeReferenceExpression, NullTypeSetterClearsSlot) {
    TypeReferenceExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

TEST(CSharp_TypeReferenceExpression, GetChildCountIsOne) {
    TypeReferenceExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_TypeReferenceExpression, GetChildReturnsSlot) {
    TypeReferenceExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

TEST(CSharp_TypeReferenceExpression, SetChildRoutesToSlot) {
    TypeReferenceExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_TypeReferenceExpression, GetChildSlotInfoReturnsSlotStatic) {
    TypeReferenceExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &TypeReferenceExpression::TypeSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_TypeReferenceExpression, SlotKindPointsAtSharedSlotsType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    ASSERT_EQ(type->Slot(), &TypeReferenceExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
}

// The `TypeSlot` is typed: it accepts an `AstType` (a `PrimitiveType` is) but not an
// `Expression` (a `NullReferenceExpression` is not an `AstType`).
TEST(CSharp_TypeReferenceExpression, TypeSlotAcceptsAstTypeNotExpression) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(TypeReferenceExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(TypeReferenceExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(TypeReferenceExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

TEST(CSharp_TypeReferenceExpression, AcceptVisitorDispatchesToVisit) {
    TypeReferenceExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "typeref");
}

TEST(CSharp_TypeReferenceExpression, AcceptVisitorIsVirtual) {
    TypeReferenceExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "typeref");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "typeref");
}

// ---- Depth-first walk -------------------------------------------------

TEST(CSharp_TypeReferenceExpression, DepthFirstWalksChildInOrder) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "typeref");
    EXPECT_EQ(v.trace[1], "prim:int");
}

TEST(CSharp_TypeReferenceExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    TypeReferenceExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "typeref");
}

// ---- DoMatch (the generated non-nullable-recursive match, one term) --

TEST(CSharp_TypeReferenceExpression, DoMatchMatchesSameChild) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression b(bType.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeReferenceExpression, DoMatchRejectsDifferentType) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    TypeReferenceExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeReferenceExpression, DoMatchNullPatternTypeRejects) {
    TypeReferenceExpression a;
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeReferenceExpression, DoMatchNullCandidateTypeRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    TypeReferenceExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeReferenceExpression, DoMatchAllChildrenNullRejects) {
    TypeReferenceExpression a;
    TypeReferenceExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeReferenceExpression, DoMatchDelegatesToChildDoMatch) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    TypeReferenceExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeReferenceExpression, DoMatchRejectsNonTypeRefCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_TypeReferenceExpression, DoMatchRejectsNullCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

TEST(CSharp_TypeReferenceExpression, DoMatchRejectsTypeOfSibling) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// ---- Clone ------------------------------------------------------------

TEST(CSharp_TypeReferenceExpression, CloneDeepClonesChild) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    std::unique_ptr<TypeReferenceExpression> copy(static_cast<TypeReferenceExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

TEST(CSharp_TypeReferenceExpression, CloneEmptyNodeHasNullSlot) {
    TypeReferenceExpression e;
    std::unique_ptr<TypeReferenceExpression> copy(static_cast<TypeReferenceExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
}

TEST(CSharp_TypeReferenceExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<TypeReferenceExpression*>(nodeCopy.get()), nullptr);
}

TEST(CSharp_TypeReferenceExpression, CloneDoesNotDetachSource) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    std::unique_ptr<TypeReferenceExpression> copy(static_cast<TypeReferenceExpression*>(e.Clone()));
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(e.Type(), type.get());
}

// ---- CheckInvariant ---------------------------------------------------

TEST(CSharp_TypeReferenceExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression e(type.get());
    e.CheckInvariant();
    SUCCEED();
}

// =====================================================================
// TypeOfExpression (TypeofKeyword = "typeof")
// =====================================================================

TEST(CSharp_TypeOfExpression, IsAstNodeAndExpressionNotAstType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

TEST(CSharp_TypeOfExpression, TypeofKeywordConst) {
    EXPECT_STREQ(TypeOfExpression::TypeofKeyword, "typeof");
}

TEST(CSharp_TypeOfExpression, DefaultCtor) {
    TypeOfExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_TypeOfExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    PrimitiveType* typePtr = type.get();
    TypeOfExpression e(type.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
}

TEST(CSharp_TypeOfExpression, TypeSetterFillsAndParents) {
    TypeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

TEST(CSharp_TypeOfExpression, NullTypeSetterClearsSlot) {
    TypeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

TEST(CSharp_TypeOfExpression, GetChildCountIsOne) {
    TypeOfExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_TypeOfExpression, GetChildReturnsSlot) {
    TypeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

TEST(CSharp_TypeOfExpression, SetChildRoutesToSlot) {
    TypeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_TypeOfExpression, GetChildSlotInfoReturnsSlotStatic) {
    TypeOfExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &TypeOfExpression::TypeSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_TypeOfExpression, SlotKindPointsAtSharedSlotsType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    ASSERT_EQ(type->Slot(), &TypeOfExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
}

TEST(CSharp_TypeOfExpression, TypeSlotAcceptsAstTypeNotExpression) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(TypeOfExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(TypeOfExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(TypeOfExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

TEST(CSharp_TypeOfExpression, AcceptVisitorDispatchesToVisit) {
    TypeOfExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "typeof");
}

TEST(CSharp_TypeOfExpression, AcceptVisitorIsVirtual) {
    TypeOfExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "typeof");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "typeof");
}

TEST(CSharp_TypeOfExpression, DepthFirstWalksChildInOrder) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "typeof");
    EXPECT_EQ(v.trace[1], "prim:int");
}

TEST(CSharp_TypeOfExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    TypeOfExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "typeof");
}

TEST(CSharp_TypeOfExpression, DoMatchMatchesSameChild) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression b(bType.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeOfExpression, DoMatchRejectsDifferentType) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    TypeOfExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeOfExpression, DoMatchNullPatternTypeRejects) {
    TypeOfExpression a;
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeOfExpression, DoMatchNullCandidateTypeRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression a(aType.get());
    TypeOfExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeOfExpression, DoMatchAllChildrenNullRejects) {
    TypeOfExpression a;
    TypeOfExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeOfExpression, DoMatchRejectsNonTypeOfCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression a(aType.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_TypeOfExpression, DoMatchRejectsNullCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression a(aType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

TEST(CSharp_TypeOfExpression, DoMatchRejectsTypeRefSibling) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    TypeReferenceExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TypeOfExpression, CloneDeepClonesChild) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    std::unique_ptr<TypeOfExpression> copy(static_cast<TypeOfExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

TEST(CSharp_TypeOfExpression, CloneEmptyNodeHasNullSlot) {
    TypeOfExpression e;
    std::unique_ptr<TypeOfExpression> copy(static_cast<TypeOfExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
}

TEST(CSharp_TypeOfExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<TypeOfExpression*>(nodeCopy.get()), nullptr);
}

TEST(CSharp_TypeOfExpression, CloneDoesNotDetachSource) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    std::unique_ptr<TypeOfExpression> copy(static_cast<TypeOfExpression*>(e.Clone()));
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(e.Type(), type.get());
}

TEST(CSharp_TypeOfExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    TypeOfExpression e(type.get());
    e.CheckInvariant();
    SUCCEED();
}

// =====================================================================
// DefaultValueExpression (DefaultKeyword = "default")
// =====================================================================

TEST(CSharp_DefaultValueExpression, IsAstNodeAndExpressionNotAstType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

TEST(CSharp_DefaultValueExpression, DefaultKeywordConst) {
    EXPECT_STREQ(DefaultValueExpression::DefaultKeyword, "default");
}

TEST(CSharp_DefaultValueExpression, DefaultCtor) {
    DefaultValueExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_DefaultValueExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    PrimitiveType* typePtr = type.get();
    DefaultValueExpression e(type.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
}

TEST(CSharp_DefaultValueExpression, TypeSetterFillsAndParents) {
    DefaultValueExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

TEST(CSharp_DefaultValueExpression, NullTypeSetterClearsSlot) {
    DefaultValueExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

TEST(CSharp_DefaultValueExpression, GetChildCountIsOne) {
    DefaultValueExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_DefaultValueExpression, GetChildReturnsSlot) {
    DefaultValueExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

TEST(CSharp_DefaultValueExpression, SetChildRoutesToSlot) {
    DefaultValueExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_DefaultValueExpression, GetChildSlotInfoReturnsSlotStatic) {
    DefaultValueExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &DefaultValueExpression::TypeSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_DefaultValueExpression, SlotKindPointsAtSharedSlotsType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    ASSERT_EQ(type->Slot(), &DefaultValueExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
}

TEST(CSharp_DefaultValueExpression, TypeSlotAcceptsAstTypeNotExpression) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(DefaultValueExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(DefaultValueExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(DefaultValueExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

TEST(CSharp_DefaultValueExpression, AcceptVisitorDispatchesToVisit) {
    DefaultValueExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "default");
}

TEST(CSharp_DefaultValueExpression, AcceptVisitorIsVirtual) {
    DefaultValueExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "default");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "default");
}

TEST(CSharp_DefaultValueExpression, DepthFirstWalksChildInOrder) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "default");
    EXPECT_EQ(v.trace[1], "prim:int");
}

TEST(CSharp_DefaultValueExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    DefaultValueExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "default");
}

TEST(CSharp_DefaultValueExpression, DoMatchMatchesSameChild) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression b(bType.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DefaultValueExpression, DoMatchRejectsDifferentType) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    DefaultValueExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DefaultValueExpression, DoMatchNullPatternTypeRejects) {
    DefaultValueExpression a;
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DefaultValueExpression, DoMatchNullCandidateTypeRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression a(aType.get());
    DefaultValueExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DefaultValueExpression, DoMatchAllChildrenNullRejects) {
    DefaultValueExpression a;
    DefaultValueExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DefaultValueExpression, DoMatchRejectsNonDefaultCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression a(aType.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_DefaultValueExpression, DoMatchRejectsNullCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression a(aType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

TEST(CSharp_DefaultValueExpression, DoMatchRejectsSizeOfSibling) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_DefaultValueExpression, CloneDeepClonesChild) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    std::unique_ptr<DefaultValueExpression> copy(static_cast<DefaultValueExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

TEST(CSharp_DefaultValueExpression, CloneEmptyNodeHasNullSlot) {
    DefaultValueExpression e;
    std::unique_ptr<DefaultValueExpression> copy(static_cast<DefaultValueExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
}

TEST(CSharp_DefaultValueExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<DefaultValueExpression*>(nodeCopy.get()), nullptr);
}

TEST(CSharp_DefaultValueExpression, CloneDoesNotDetachSource) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    std::unique_ptr<DefaultValueExpression> copy(static_cast<DefaultValueExpression*>(e.Clone()));
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(e.Type(), type.get());
}

TEST(CSharp_DefaultValueExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression e(type.get());
    e.CheckInvariant();
    SUCCEED();
}

// =====================================================================
// SizeOfExpression (SizeofKeyword = "sizeof")
// =====================================================================

TEST(CSharp_SizeOfExpression, IsAstNodeAndExpressionNotAstType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

TEST(CSharp_SizeOfExpression, SizeofKeywordConst) {
    EXPECT_STREQ(SizeOfExpression::SizeofKeyword, "sizeof");
}

TEST(CSharp_SizeOfExpression, DefaultCtor) {
    SizeOfExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_SizeOfExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    PrimitiveType* typePtr = type.get();
    SizeOfExpression e(type.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
}

TEST(CSharp_SizeOfExpression, TypeSetterFillsAndParents) {
    SizeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

TEST(CSharp_SizeOfExpression, NullTypeSetterClearsSlot) {
    SizeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

TEST(CSharp_SizeOfExpression, GetChildCountIsOne) {
    SizeOfExpression e;
    EXPECT_EQ(e.GetChildCount(), 1);
}

TEST(CSharp_SizeOfExpression, GetChildReturnsSlot) {
    SizeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_THROW(e.GetChild(1), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

TEST(CSharp_SizeOfExpression, SetChildRoutesToSlot) {
    SizeOfExpression e;
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_THROW(e.SetChild(1, nullptr), std::out_of_range);
}

TEST(CSharp_SizeOfExpression, GetChildSlotInfoReturnsSlotStatic) {
    SizeOfExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &SizeOfExpression::TypeSlot);
    EXPECT_THROW(e.GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_SizeOfExpression, SlotKindPointsAtSharedSlotsType) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    ASSERT_EQ(type->Slot(), &SizeOfExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
}

TEST(CSharp_SizeOfExpression, TypeSlotAcceptsAstTypeNotExpression) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_TRUE(SizeOfExpression::TypeSlot.IsInstanceOfType(&pt));
    EXPECT_FALSE(SizeOfExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(SizeOfExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

TEST(CSharp_SizeOfExpression, AcceptVisitorDispatchesToVisit) {
    SizeOfExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "sizeof");
}

TEST(CSharp_SizeOfExpression, AcceptVisitorIsVirtual) {
    SizeOfExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "sizeof");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "sizeof");
}

TEST(CSharp_SizeOfExpression, DepthFirstWalksChildInOrder) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "sizeof");
    EXPECT_EQ(v.trace[1], "prim:int");
}

TEST(CSharp_SizeOfExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    SizeOfExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "sizeof");
}

TEST(CSharp_SizeOfExpression, DoMatchMatchesSameChild) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression b(bType.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SizeOfExpression, DoMatchRejectsDifferentType) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("long"));
    SizeOfExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SizeOfExpression, DoMatchNullPatternTypeRejects) {
    SizeOfExpression a;
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SizeOfExpression, DoMatchNullCandidateTypeRejects) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression a(aType.get());
    SizeOfExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SizeOfExpression, DoMatchAllChildrenNullRejects) {
    SizeOfExpression a;
    SizeOfExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SizeOfExpression, DoMatchRejectsNonSizeOfCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression a(aType.get());
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&a, &nre));
}

TEST(CSharp_SizeOfExpression, DoMatchRejectsNullCandidate) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression a(aType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

TEST(CSharp_SizeOfExpression, DoMatchRejectsDefaultSibling) {
    auto aType = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression a(aType.get());
    auto bType = std::make_unique<PrimitiveType>(std::string("int"));
    DefaultValueExpression b(bType.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SizeOfExpression, CloneDeepClonesChild) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    std::unique_ptr<SizeOfExpression> copy(static_cast<SizeOfExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    auto* clonedType = dynamic_cast<PrimitiveType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Keyword(), "int");
}

TEST(CSharp_SizeOfExpression, CloneEmptyNodeHasNullSlot) {
    SizeOfExpression e;
    std::unique_ptr<SizeOfExpression> copy(static_cast<SizeOfExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
}

TEST(CSharp_SizeOfExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<SizeOfExpression*>(nodeCopy.get()), nullptr);
}

TEST(CSharp_SizeOfExpression, CloneDoesNotDetachSource) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    std::unique_ptr<SizeOfExpression> copy(static_cast<SizeOfExpression*>(e.Clone()));
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(e.Type(), type.get());
}

TEST(CSharp_SizeOfExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<PrimitiveType>(std::string("int"));
    SizeOfExpression e(type.get());
    e.CheckInvariant();
    SUCCEED();
}
