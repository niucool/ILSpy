// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation, rights to use, copy, modify, merge,
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

// Tests for the `StackAllocExpression` concrete node (cpp/.../Expressions/
// StackAllocExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.cs) -- the
// `stackalloc_expression ::= 'stackalloc' type '[' expression ']' | 'stackalloc' type? '['
// expression? ']' array_initializer` shape (C# grammar 12.8.22): a sealed `Expression` with
// three single, NULLABLE `[Slot]` children (`Type` `AstType?` at index 0, `CountExpression`
// `Expression?` at index 1, `Initializer` `ArrayInitializerExpression?` at index 2), reusing
// the already-ported `Slots::Type`/`Slots::Expression`/`Slots::Initializer` kinds. Exercises
// the slot-storage contract (`GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` over
// three nullable single slots at const indices), the `Type`/`CountExpression`/`Initializer`
// accessors (NO name-shadowing crux -- the `CountExpression` property name differs from its
// `Expression` type, so no elaborated `class Expression` specifier is exercised here, unlike
// `CastExpression`/`WithInitializerExpression`), the generated `DoMatch` over THREE NULLABLE
// recursive children (the `MatchOptional` dispatch -- both absent matches, present-vs-absent
// rejects), the `Clone` deep-copy + re-parent of three children (each skipped if absent), the
// `AcceptVisitor` dispatch, and the depth-first walk. There is no scalar enum, so `DoMatch`
// has no `Any`-wildcard term. All three slots are nullable, so `CheckInvariant` PASSES on an
// empty node (no required slot is empty) -- the key difference from the required-slot nodes.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitStackAllocExpression` (and the nodes it
// holds), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order (document order). The `Type` slot is
// exercised with a `SimpleType` (the simplest leaf `AstType` -- its backing `Identifier` token
// is a visited child, so a `SimpleType("int")` records `simple:int` then `id:int`), the
// `CountExpression` slot with a `NullReferenceExpression`/`ThisReferenceExpression` (the
// simplest leaf `Expression`s), and the `Initializer` slot with an `ArrayInitializerExpression`
// (the `Elements` collection of which is empty in the walk tests, so
// `VisitArrayInitializerExpression` records just the `arrinit` tag).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitStackAllocExpression(StackAllocExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-stackalloc>");
            return;
        }
        trace.push_back("stackalloc");
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) {
            trace.push_back("<null-st>");
            return;
        }
        trace.push_back("simple:" + node->Identifier().value_or("<anon>"));
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) {
            trace.push_back("<null-id>");
            return;
        }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-nre>");
            return;
        }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-tre>");
            return;
        }
        trace.push_back("thisref");
        VisitChildren(node);
    }
    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null-arrinit>");
            return;
        }
        trace.push_back("arrinit");
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

// ---- is-a --------------------------------------------------------------

// `StackAllocExpression` is an `AstNode` and an `Expression` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, not `AstType`).
TEST(CSharp_StackAllocExpression, IsAstNodeAndExpressionNotAstType) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&e), nullptr);
    EXPECT_NE(dynamic_cast<Expression*>(&e), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&e), nullptr);
}

// ---- Const string ------------------------------------------------------

// The `StackallocKeyword` const string is the literal "stackalloc" (the output-visitor token
// the node emits).
TEST(CSharp_StackAllocExpression, StackallocKeywordConst) {
    EXPECT_STREQ(StackAllocExpression::StackallocKeyword, "stackalloc");
}

// ---- Construction -----------------------------------------------------

// The default ctor: all three slots are null (no type, no count, no initializer). All three
// slots are nullable, so a default-constructed node is invariant-valid. The three slots still
// occupy their flattened indices even when empty, so `GetChildCount` is 3.
TEST(CSharp_StackAllocExpression, DefaultCtor) {
    StackAllocExpression e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.CountExpression(), nullptr);
    EXPECT_EQ(e.Initializer(), nullptr);
    EXPECT_EQ(e.GetChildCount(), 3);
}

// The all-params ctor sets all three slots, parents each child, and assigns the flat indices
// 0/1/2 directly (the const-index `SetChildNode` path for each single slot).
TEST(CSharp_StackAllocExpression, AllParamsCtorSetsAndParents) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    SimpleType* typePtr = type.get();
    NullReferenceExpression* countPtr = count.get();
    ArrayInitializerExpression* initPtr = init.get();
    StackAllocExpression e(type.get(), count.get(), init.get());
    EXPECT_EQ(e.Type(), typePtr);
    EXPECT_EQ(e.CountExpression(), countPtr);
    EXPECT_EQ(e.Initializer(), initPtr);
    EXPECT_EQ(typePtr->Parent(), &e);
    EXPECT_EQ(typePtr->ChildIndex, 0);
    EXPECT_EQ(countPtr->Parent(), &e);
    EXPECT_EQ(countPtr->ChildIndex, 1);
    EXPECT_EQ(initPtr->Parent(), &e);
    EXPECT_EQ(initPtr->ChildIndex, 2);
}

// ---- Accessors + slot storage -----------------------------------------

// The `Type` setter fills the slot, parents the child, and assigns the flat index 0 directly.
TEST(CSharp_StackAllocExpression, TypeSetterFillsAndParents) {
    StackAllocExpression e;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    e.Type(type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(type->ChildIndex, 0);
}

// The `CountExpression` setter fills the slot, parents the child, and assigns the flat index 1
// directly.
TEST(CSharp_StackAllocExpression, CountExpressionSetterFillsAndParents) {
    StackAllocExpression e;
    auto count = std::make_unique<NullReferenceExpression>();
    e.CountExpression(count.get());
    EXPECT_EQ(e.CountExpression(), count.get());
    EXPECT_EQ(count->Parent(), &e);
    EXPECT_EQ(count->ChildIndex, 1);
}

// The `Initializer` setter fills the slot, parents the child, and assigns the flat index 2
// directly.
TEST(CSharp_StackAllocExpression, InitializerSetterFillsAndParents) {
    StackAllocExpression e;
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.Initializer(init.get());
    EXPECT_EQ(e.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &e);
    EXPECT_EQ(init->ChildIndex, 2);
}

// A null `Type` setter clears the slot and detaches the old child (the slot is nullable, so a
// null is invariant-valid).
TEST(CSharp_StackAllocExpression, NullTypeSetterClearsSlot) {
    StackAllocExpression e;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    e.Type(type.get());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(type->Parent(), nullptr);
    EXPECT_EQ(type->ChildIndex, -1);
}

// A null `CountExpression` setter clears the slot and detaches the old child.
TEST(CSharp_StackAllocExpression, NullCountExpressionSetterClearsSlot) {
    StackAllocExpression e;
    auto count = std::make_unique<NullReferenceExpression>();
    e.CountExpression(count.get());
    e.CountExpression(nullptr);
    EXPECT_EQ(e.CountExpression(), nullptr);
    EXPECT_EQ(count->Parent(), nullptr);
    EXPECT_EQ(count->ChildIndex, -1);
}

// A null `Initializer` setter clears the slot and detaches the old child.
TEST(CSharp_StackAllocExpression, NullInitializerSetterClearsSlot) {
    StackAllocExpression e;
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.Initializer(init.get());
    e.Initializer(nullptr);
    EXPECT_EQ(e.Initializer(), nullptr);
    EXPECT_EQ(init->Parent(), nullptr);
    EXPECT_EQ(init->ChildIndex, -1);
}

// `GetChildCount` is the constant 3 (three single slots, no collection).
TEST(CSharp_StackAllocExpression, GetChildCountIsThree) {
    StackAllocExpression e;
    EXPECT_EQ(e.GetChildCount(), 3);
}

// `GetChild` returns each slot at its flat index and throws out of range otherwise.
TEST(CSharp_StackAllocExpression, GetChildReturnsSlots) {
    StackAllocExpression e;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.Type(type.get());
    e.CountExpression(count.get());
    e.Initializer(init.get());
    EXPECT_EQ(e.GetChild(0), type.get());
    EXPECT_EQ(e.GetChild(1), count.get());
    EXPECT_EQ(e.GetChild(2), init.get());
    EXPECT_THROW(e.GetChild(3), std::out_of_range);
    EXPECT_THROW(e.GetChild(-1), std::out_of_range);
}

// `SetChild` routes to the matching slot setter.
TEST(CSharp_StackAllocExpression, SetChildRoutesToSlots) {
    StackAllocExpression e;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    e.SetChild(0, type.get());
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(type->Parent(), &e);
    e.SetChild(1, count.get());
    EXPECT_EQ(e.CountExpression(), count.get());
    EXPECT_EQ(count->Parent(), &e);
    e.SetChild(2, init.get());
    EXPECT_EQ(e.Initializer(), init.get());
    EXPECT_THROW(e.SetChild(3, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics.
TEST(CSharp_StackAllocExpression, GetChildSlotInfoReturnsSlotStatics) {
    StackAllocExpression e;
    EXPECT_EQ(e.GetChildSlotInfo(0), &StackAllocExpression::TypeSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &StackAllocExpression::CountExpressionSlot);
    EXPECT_EQ(e.GetChildSlotInfo(2), &StackAllocExpression::InitializerSlot);
    EXPECT_THROW(e.GetChildSlotInfo(3), std::out_of_range);
}

// Each per-node slot's `Kind` points at the shared `Slots` constant (the polymorphic position
// identity `node.Slot.Kind == Slots.X` relies on). `Type` reuses `Slots::Type` (ported by
// `Attribute`); `CountExpression` reuses `Slots::Expression` (ported by
// `UnaryOperatorExpression`); `Initializer` reuses `Slots::Initializer` (ported by
// `ObjectCreateExpression`/`ArrayCreateExpression` as the cycle-broken kind in
// `ArrayInitializerExpression.hpp`).
TEST(CSharp_StackAllocExpression, SlotKindsPointAtSharedSlotsConstants) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    ASSERT_EQ(type->Slot(), &StackAllocExpression::TypeSlot);
    EXPECT_EQ(type->Slot()->Kind(), &Slots::Type);
    ASSERT_EQ(count->Slot(), &StackAllocExpression::CountExpressionSlot);
    EXPECT_EQ(count->Slot()->Kind(), &Slots::Expression);
    ASSERT_EQ(init->Slot(), &StackAllocExpression::InitializerSlot);
    EXPECT_EQ(init->Slot()->Kind(), &Slots::Initializer);
}

// The slot statics are typed: `TypeSlot` accepts an `AstType` (a `SimpleType` is, a
// `NullReferenceExpression` is not -- it derives from `Expression`, not `AstType`);
// `CountExpressionSlot` accepts an `Expression` (a `NullReferenceExpression` is, a `SimpleType`
// is not); `InitializerSlot` accepts an `ArrayInitializerExpression` (a `NullReferenceExpression`
// is not, even though both are `Expression`s; a `SimpleType` is not). This cross-checks that
// the three slots' element types are distinct and faithful to the C#. A null is correctly
// rejected (faithful to C# `Type.IsInstanceOfType(null) == false`).
TEST(CSharp_StackAllocExpression, SlotsAreInstanceOfTheirElementTypes) {
    SimpleType st(std::string("int"));
    NullReferenceExpression nre;
    ArrayInitializerExpression aie;
    EXPECT_TRUE(StackAllocExpression::TypeSlot.IsInstanceOfType(&st));
    EXPECT_FALSE(StackAllocExpression::TypeSlot.IsInstanceOfType(&nre));
    EXPECT_TRUE(StackAllocExpression::CountExpressionSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(StackAllocExpression::CountExpressionSlot.IsInstanceOfType(&st));
    EXPECT_TRUE(StackAllocExpression::InitializerSlot.IsInstanceOfType(&aie));
    EXPECT_FALSE(StackAllocExpression::InitializerSlot.IsInstanceOfType(&nre));
    EXPECT_FALSE(StackAllocExpression::InitializerSlot.IsInstanceOfType(&st));
    EXPECT_FALSE(StackAllocExpression::TypeSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(StackAllocExpression::CountExpressionSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
    EXPECT_FALSE(StackAllocExpression::InitializerSlot.IsInstanceOfType(static_cast<AstNode*>(nullptr)));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitStackAllocExpression`.
TEST(CSharp_StackAllocExpression, AcceptVisitorDispatchesToVisit) {
    StackAllocExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "stackalloc");
}

// `AcceptVisitor` is virtual: calling through an `AstNode*`/`Expression*` dispatches to the
// concrete override.
TEST(CSharp_StackAllocExpression, AcceptVisitorIsVirtual) {
    StackAllocExpression e;
    AstNode* asNode = &e;
    Expression* asExpr = &e;
    RecordingVisitor v;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "stackalloc");
    RecordingVisitor v2;
    asExpr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "stackalloc");
}

// ---- Depth-first walk -------------------------------------------------

// The depth-first walk visits the `stackalloc` node, then its three children in document order
// (the `Type` `SimpleType` -- which recurses into its backing `Identifier` token -- then the
// `CountExpression` `NullReferenceExpression`, then the `Initializer`
// `ArrayInitializerExpression`).
TEST(CSharp_StackAllocExpression, DepthFirstWalksChildrenInOrder) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 5u);
    EXPECT_EQ(v.trace[0], "stackalloc");
    EXPECT_EQ(v.trace[1], "simple:int");
    EXPECT_EQ(v.trace[2], "id:int");
    EXPECT_EQ(v.trace[3], "nullref");
    EXPECT_EQ(v.trace[4], "arrinit");
}

// A node with no children records just the `stackalloc` node.
TEST(CSharp_StackAllocExpression, DepthFirstOnEmptyNodeRecordsJustSelf) {
    StackAllocExpression e;
    RecordingVisitor v;
    e.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "stackalloc");
}

// ---- DoMatch (the generated nullable-recursive match, three terms) ---

// Two nodes with the same-type children in all three slots match (both `Initializer`s empty --
// the collection-DoMatch on `Elements` matches two empty collections).
TEST(CSharp_StackAllocExpression, DoMatchMatchesSameChildren) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bType = std::make_unique<SimpleType>(std::string("int"));
    auto bCount = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression b(bType.get(), bCount.get(), bInit.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two empty nodes (all three slots null on both sides) match: each `MatchOptional` term returns
// true when both children are absent (the nullable-recursive match -- the key difference from
// the required-slot `MatchRequired` nodes, which would reject on a null child).
TEST(CSharp_StackAllocExpression, DoMatchMatchesTwoEmpties) {
    StackAllocExpression a;
    StackAllocExpression b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Type` slot (the first term) rejects (a `SimpleType("int")` pattern vs a
// `SimpleType("byte")` candidate rejects on the child's `MatchString` `DoMatch`).
TEST(CSharp_StackAllocExpression, DoMatchRejectsDifferentType) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bType = std::make_unique<SimpleType>(std::string("byte")); // different type name
    auto bCount = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression b(bType.get(), bCount.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `CountExpression` slot (the second term) rejects (a
// `NullReferenceExpression` pattern vs a `ThisReferenceExpression` candidate rejects on the
// child's type-only `DoMatch`).
TEST(CSharp_StackAllocExpression, DoMatchRejectsDifferentCountExpression) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bType = std::make_unique<SimpleType>(std::string("int"));
    auto bCount = std::make_unique<ThisReferenceExpression>(); // different count type
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression b(bType.get(), bCount.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A mismatch in the `Initializer` slot (the third term) rejects: the `Initializer`'s `DoMatch`
// is a collection-DoMatch on `Elements`, so an empty `Initializer` vs one with a single element
// rejects on the count mismatch.
TEST(CSharp_StackAllocExpression, DoMatchRejectsDifferentInitializer) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bType = std::make_unique<SimpleType>(std::string("int"));
    auto bCount = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    auto bElem = std::make_unique<NullReferenceExpression>();
    bInit->Elements().Add(bElem.get()); // one element -- different from the empty pattern
    StackAllocExpression b(bType.get(), bCount.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A present-vs-absent `Type` rejects: the pattern carries a `Type` but the candidate does not
// (`MatchOptional` returns false when only one side carries a child).
TEST(CSharp_StackAllocExpression, DoMatchRejectsPresentTypeVsAbsent) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bCount = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression b(nullptr, bCount.get(), bInit.get()); // no Type
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A present-vs-absent `CountExpression` rejects.
TEST(CSharp_StackAllocExpression, DoMatchRejectsPresentCountExpressionVsAbsent) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bType = std::make_unique<SimpleType>(std::string("int"));
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression b(bType.get(), nullptr, bInit.get()); // no CountExpression
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A present-vs-absent `Initializer` rejects.
TEST(CSharp_StackAllocExpression, DoMatchRejectsPresentInitializerVsAbsent) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());

    auto bType = std::make_unique<SimpleType>(std::string("int"));
    auto bCount = std::make_unique<NullReferenceExpression>();
    StackAllocExpression b(bType.get(), bCount.get(), nullptr); // no Initializer
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// An absent-vs-present `Type` rejects (the symmetric case: the candidate carries a `Type` the
// pattern does not).
TEST(CSharp_StackAllocExpression, DoMatchRejectsAbsentTypeVsPresent) {
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(nullptr, aCount.get(), aInit.get()); // no Type

    auto bType = std::make_unique<SimpleType>(std::string("int"));
    auto bCount = std::make_unique<NullReferenceExpression>();
    auto bInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression b(bType.get(), bCount.get(), bInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different-type candidate (not a `StackAllocExpression`) rejects -- a cross-structural-twin
// rejection against `ObjectCreateExpression` (which also carries a `Type` + an `Initializer`
// but a collection `Arguments` instead of a `CountExpression` single slot): the two share a
// `Type` + `Initializer` shape but are distinct concrete types, so the pattern matcher's
// type-check gate rejects.
TEST(CSharp_StackAllocExpression, DoMatchRejectsNonStackAllocCandidate) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());
    ObjectCreateExpression oce; // an empty ObjectCreateExpression (a distinct concrete type)
    EXPECT_FALSE(DoMatchAgainst(&a, &oce));
}

// A null candidate rejects (the `INode` delegation: a null candidate is not an `AstNode`).
TEST(CSharp_StackAllocExpression, DoMatchRejectsNullCandidate) {
    auto aType = std::make_unique<SimpleType>(std::string("int"));
    auto aCount = std::make_unique<NullReferenceExpression>();
    auto aInit = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression a(aType.get(), aCount.get(), aInit.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ------------------------------------------------------------

// `Clone` deep-clones all three children (distinct objects, not shared), re-parents each to
// the copy, and assigns the flat indices.
TEST(CSharp_StackAllocExpression, CloneDeepClonesAllChildren) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    std::unique_ptr<StackAllocExpression> copy(static_cast<StackAllocExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(copy->Type()->ChildIndex, 0);
    ASSERT_NE(copy->CountExpression(), nullptr);
    EXPECT_NE(copy->CountExpression(), count.get());
    EXPECT_EQ(copy->CountExpression()->Parent(), copy.get());
    EXPECT_EQ(copy->CountExpression()->ChildIndex, 1);
    ASSERT_NE(copy->Initializer(), nullptr);
    EXPECT_NE(copy->Initializer(), init.get());
    EXPECT_EQ(copy->Initializer()->Parent(), copy.get());
    EXPECT_EQ(copy->Initializer()->ChildIndex, 2);
}

// `Clone` of a node with no children yields a node with all three slots null (the `Clone` skips
// a child clone when the child is absent).
TEST(CSharp_StackAllocExpression, CloneEmptyNodeHasNullSlots) {
    StackAllocExpression e;
    std::unique_ptr<StackAllocExpression> copy(static_cast<StackAllocExpression*>(e.Clone()));
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->CountExpression(), nullptr);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `Expression*`.
TEST(CSharp_StackAllocExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    AstNode* asNode = &e;
    std::unique_ptr<AstNode> nodeCopy(asNode->Clone());
    EXPECT_NE(nodeCopy, nullptr);
    Expression* asExpr = &e;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    EXPECT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<StackAllocExpression*>(nodeCopy.get()), nullptr);
}

// The source is unaffected by `Clone` (the clone's children are distinct and re-parented to
// the copy, not stolen from the source).
TEST(CSharp_StackAllocExpression, CloneDoesNotDetachSource) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    std::unique_ptr<StackAllocExpression> copy(static_cast<StackAllocExpression*>(e.Clone()));
    EXPECT_EQ(type->Parent(), &e);
    EXPECT_EQ(e.Type(), type.get());
    EXPECT_EQ(e.CountExpression(), count.get());
    EXPECT_EQ(e.Initializer(), init.get());
}

// `Clone` deep-copies the `Type`'s name (the cloned `Type` is an `AstType*`, so a
// `dynamic_cast<SimpleType*>` downcast reaches the `Identifier` accessor -- the recurring
// abstract-base-slot-accessor downcast gotcha).
TEST(CSharp_StackAllocExpression, CloneDeepCopiesTypeName) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    std::unique_ptr<StackAllocExpression> copy(static_cast<StackAllocExpression*>(e.Clone()));
    ASSERT_NE(copy->Type(), nullptr);
    auto* clonedType = dynamic_cast<SimpleType*>(copy->Type());
    ASSERT_NE(clonedType, nullptr);
    EXPECT_EQ(clonedType->Identifier().value_or(""), "int");
}

// ---- CheckInvariant ---------------------------------------------------

// The inherited `CheckInvariant` passes on a node with all three slots filled.
TEST(CSharp_StackAllocExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto count = std::make_unique<NullReferenceExpression>();
    auto init = std::make_unique<ArrayInitializerExpression>();
    StackAllocExpression e(type.get(), count.get(), init.get());
    e.CheckInvariant();
    SUCCEED();
}

// The inherited `CheckInvariant` PASSES on an empty node: all three slots are NULLABLE, so no
// required slot is empty (the key difference from the required-slot nodes like
// `WithInitializerExpression` whose empty node would assert "required slot must not be
// empty"). The empty `stackalloc` (no type, no count, no initializer) is invariant-valid.
TEST(CSharp_StackAllocExpression, CheckInvariantPassesOnEmptyNode) {
    StackAllocExpression e;
    e.CheckInvariant();
    SUCCEED();
}
