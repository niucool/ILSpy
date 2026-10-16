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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the `ArrayInitializerExpression` concrete node (cpp/.../Syntax/Expressions/
// ArrayInitializerExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.cs) -- the
// simplest collection-slot node: a non-sealed `Expression` whose sole child slot is the
// `Elements` `AstNodeCollection<Expression>` collection (the `array_initializer ::=
// '{' expression* '}'` production). It is the first ported node with a collection slot and NO
// single child slot, and the first non-sealed concrete node (its `PatternPlaceholder` subclass
// is deferred). Exercises the `Elements` collection, the collection-only slot-storage contract
// (an empty node reports `GetChildCount` 0), the `AcceptVisitor` dispatch, the generated
// `DoMatch` (a single collection `DoMatch` term), the per-concrete-node `Clone`, and the
// inherited `CheckInvariant` (which passes on the empty node -- there are no required single
// slots).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls (the role the generated
// per-node `Visit` overrides play) with a tag distinguishing the concrete types, recursing via
// the inherited `VisitChildren` (the document-order walk). `VisitArrayInitializerExpression`/
// `VisitIdentifierExpression`/`VisitIdentifier` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("arrinit");
        VisitChildren(node);
    }
    void VisitIdentifierExpression(IdentifierExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-idexpr>"); return; }
        trace.push_back("idexpr:" + node->Identifier());
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `ArrayInitializerExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the
// slot system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, parallel to -- not under -- `AstType`).
TEST(CSharp_ArrayInitializerExpression, IsExpressionAndAstNodeNotAstType) {
    ArrayInitializerExpression aie;
    EXPECT_NE(dynamic_cast<Expression*>(&aie), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&aie), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&aie), nullptr);
}

// `ArrayInitializerExpression` is a concrete (non-abstract) class but NOT `final` (the C# is not
// sealed -- the generated `PatternPlaceholder` derives from it; the placeholder is deferred, but
// the class stays open for derivation). It is constructible directly.
TEST(CSharp_ArrayInitializerExpression, IsConcreteAndNotFinal) {
    auto aie = std::make_unique<ArrayInitializerExpression>();
    ASSERT_NE(aie, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(aie.get()), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no elements. `GetChildCount` is 0 (the node has no single slots -- the only
// slot is the `Elements` collection, which is empty -- the first ported node where an empty node
// reports `GetChildCount` 0).
TEST(CSharp_ArrayInitializerExpression, EmptyCtorHasNoElements) {
    ArrayInitializerExpression aie;
    EXPECT_EQ(aie.Elements().Count(), 0);
    EXPECT_EQ(aie.GetChildCount(), 0);  // no single slots + 0 elements
    EXPECT_EQ(aie.StartLocation(), TextLocation::Empty);
}

// ---- The `Elements` collection ----------------------------------------

// The collection starts empty (0 count); the node's child count is 0 (no single slots).
TEST(CSharp_ArrayInitializerExpression, ElementsEmptyByDefault) {
    ArrayInitializerExpression aie;
    EXPECT_EQ(aie.Elements().Count(), 0);
    EXPECT_EQ(aie.GetChildCount(), 0);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last/only slot, so an element's index is
// its local position -- baseIndex 0).
TEST(CSharp_ArrayInitializerExpression, ElementsAddAppendsAndParentsIncremental) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    aie.Elements().Add(x.get());
    aie.Elements().Add(y.get());
    EXPECT_EQ(aie.Elements().Count(), 2);
    EXPECT_EQ(x->Parent(), &aie);
    EXPECT_EQ(y->Parent(), &aie);
    EXPECT_EQ(x->ChildIndex, 0);  // baseIndex 0 + 0
    EXPECT_EQ(y->ChildIndex, 1);  // baseIndex 0 + 1
    EXPECT_EQ(aie.GetChildCount(), 2);  // 2 elements, no single slots
    EXPECT_TRUE(aie.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Elements` collection for the `Expression` kind (the shared
// `Slots::Expression`, reused as the collection kind) and null for any other kind.
TEST(CSharp_ArrayInitializerExpression, GetCollectionByKindReturnsElementsForExpressionKind) {
    ArrayInitializerExpression aie;
    EXPECT_NE(aie.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(aie.GetCollectionByKind(&Slots::Expression), &aie.Elements());
    EXPECT_EQ(aie.GetCollectionByKind(&Slots::Argument), nullptr);  // a different kind
    EXPECT_EQ(aie.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-only dispatch) -----------

// `GetChild` returns the elements at index 0..Count; the collection occupies the contiguous range
// [0, Count). An empty node throws for any index (there are no slots at all).
TEST(CSharp_ArrayInitializerExpression, GetChildDispatchesElements) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    aie.Elements().Add(x.get());
    aie.Elements().Add(y.get());
    EXPECT_EQ(aie.GetChild(0), x.get());
    EXPECT_EQ(aie.GetChild(1), y.get());
    EXPECT_THROW(aie.GetChild(2), std::out_of_range);
    EXPECT_THROW(aie.GetChild(-1), std::out_of_range);
}

// An empty node throws for every index (no single slots, no elements).
TEST(CSharp_ArrayInitializerExpression, GetChildThrowsOnEmptyNode) {
    ArrayInitializerExpression aie;
    EXPECT_THROW(aie.GetChild(0), std::out_of_range);
}

// `GetChildSlotInfo` returns the `ElementsSlot` at index 0..Count (the slot identity the slot
// system compares by address).
TEST(CSharp_ArrayInitializerExpression, GetChildSlotInfoDispatchesElements) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(x.get());
    EXPECT_EQ(aie.GetChildSlotInfo(0), &ArrayInitializerExpression::ElementsSlot);
    EXPECT_THROW(aie.GetChildSlotInfo(1), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Expression` KIND (the shared `Slots.Expression`, reused as
// the collection kind -- the first collection reuse of `Slots::Expression`).
TEST(CSharp_ArrayInitializerExpression, ElementsSlotPointsAtExpressionKind) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(x.get());
    EXPECT_EQ(aie.GetChildSlotInfo(0)->Kind(), &Slots::Expression);
}

// `SetChild` replaces an element in place at index 0..Count (the collection's `SetAt` re-parents
// and carries the old index).
TEST(CSharp_ArrayInitializerExpression, SetChildDispatchesElements) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(x.get());
    // Replace the element at index 0.
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    aie.SetChild(0, y.get());
    EXPECT_EQ(aie.Elements().At(0), y.get());
    EXPECT_EQ(y->Parent(), &aie);
    EXPECT_EQ(x->Parent(), nullptr);  // detached by SetAt
    EXPECT_THROW(aie.SetChild(1, nullptr), std::out_of_range);  // no element at index 1
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitArrayInitializerExpression` (the visitor-pattern
// round-trip); the depth-first walk then visits the `Elements` children (a
// `VisitIdentifierExpression` per element).
TEST(CSharp_ArrayInitializerExpression, AcceptVisitorDispatchesToVisitArrayInitializerExpression) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(x.get());
    RecordingVisitor v;
    aie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"arrinit", "idexpr:x", "id:x"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_ArrayInitializerExpression, AcceptVisitorIsVirtualThroughBases) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(x.get());
    AstNode* asAst = &aie;
    Expression* asExpr = &aie;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"arrinit", "idexpr:x", "id:x"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"arrinit", "idexpr:x", "id:x"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits each `Elements` child (a `VisitIdentifierExpression` -> its
// `Identifier`) in document order. An empty initializer records just the node itself.
TEST(CSharp_ArrayInitializerExpression, DepthFirstWalkVisitsElementsInOrder) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"), TextLocation(1, 8));
    aie.Elements().Add(x.get());
    aie.Elements().Add(y.get());
    RecordingVisitor v;
    aie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "arrinit", "idexpr:x", "id:x", "idexpr:y", "id:y"}));
}

// The depth-first walk of an empty initializer records just the node itself (no children).
TEST(CSharp_ArrayInitializerExpression, DepthFirstWalkOfEmptyRecordsJustNode) {
    ArrayInitializerExpression aie;
    RecordingVisitor v;
    aie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"arrinit"}));
}

// ---- DoMatch (the generated collection match) ------------------------

// Two `ArrayInitializerExpression`s with the same (single) element match.
TEST(CSharp_ArrayInitializerExpression, DoMatchMatchesSameElements) {
    ArrayInitializerExpression a;
    ArrayInitializerExpression b;
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Elements().Add(ax.get());
    b.Elements().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two empty `ArrayInitializerExpression`s match (the collection match succeeds when both counts
// are 0).
TEST(CSharp_ArrayInitializerExpression, DoMatchMatchesTwoEmpties) {
    ArrayInitializerExpression a;
    ArrayInitializerExpression b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `ArrayInitializerExpression`s with different element COUNTS do not match (the collection
// match fails when the counts differ).
TEST(CSharp_ArrayInitializerExpression, DoMatchRejectsDifferentElementCount) {
    ArrayInitializerExpression a;
    ArrayInitializerExpression b;
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Elements().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 element, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `ArrayInitializerExpression`s with the same count but different element VALUES do not match
// (the element `DoMatch` -- a `MatchString` on the `IdentifierExpression` identifier -- rejects).
TEST(CSharp_ArrayInitializerExpression, DoMatchRejectsDifferentElementValue) {
    ArrayInitializerExpression a;
    ArrayInitializerExpression b;
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("y"));
    a.Elements().Add(ax.get());
    b.Elements().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// An `ArrayInitializerExpression` does not match a different concrete type (the `other is
// ArrayInitializerExpression` gate); an `InvocationExpression` is not an
// `ArrayInitializerExpression` (the two both hold an `Expression` collection but are distinct
// concrete types, so the type-check gate rejects).
TEST(CSharp_ArrayInitializerExpression, DoMatchRejectsInvocationExpressionCandidate) {
    ArrayInitializerExpression aie;
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_FALSE(DoMatchAgainst(&aie, &ie));
    EXPECT_FALSE(DoMatchAgainst(&ie, &aie));
}

// An `ArrayInitializerExpression` does not match an `IdentifierExpression` candidate (the
// type-check gate rejects).
TEST(CSharp_ArrayInitializerExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    ArrayInitializerExpression aie;
    IdentifierExpression idexpr(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&aie, &idexpr));
    EXPECT_FALSE(DoMatchAgainst(&idexpr, &aie));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_ArrayInitializerExpression, DoMatchRejectsNullCandidate) {
    ArrayInitializerExpression aie;
    EXPECT_FALSE(DoMatchAgainst(&aie, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the elements, re-parents the clones, and detaches from the source.
TEST(CSharp_ArrayInitializerExpression, CloneDeepCopiesElements) {
    ArrayInitializerExpression original;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    original.Elements().Add(x.get());

    std::unique_ptr<ArrayInitializerExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The element is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Elements().Count(), 1);
    ASSERT_NE(copy->Elements().At(0), nullptr);
    EXPECT_NE(copy->Elements().At(0), x.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Elements().At(0))->Identifier(), "x");
    EXPECT_EQ(copy->Elements().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Elements().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_ArrayInitializerExpression, CloneIsVirtualAndCovariant) {
    ArrayInitializerExpression original;
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<ArrayInitializerExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<ArrayInitializerExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of an empty `ArrayInitializerExpression` yields an empty clone.
TEST(CSharp_ArrayInitializerExpression, CloneOfEmptyIsEmpty) {
    ArrayInitializerExpression original;  // no elements
    std::unique_ptr<ArrayInitializerExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Elements().Count(), 0);
    EXPECT_EQ(copy->GetChildCount(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `ArrayInitializerExpression` with elements passes the inherited `CheckInvariant` (the
// slot-structure verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in debug
// builds (a no-op in NDEBUG).
TEST(CSharp_ArrayInitializerExpression, CheckInvariantPassesOnFilledNode) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    aie.Elements().Add(x.get());
    aie.Elements().Add(y.get());
    aie.CheckInvariant();
}

// An EMPTY `ArrayInitializerExpression` passes `CheckInvariant` (the node has no required single
// slots -- the only slot is the `Elements` collection, which has no required-slot invariant), the
// first ported collection-only node where the empty node is invariant-valid.
TEST(CSharp_ArrayInitializerExpression, CheckInvariantPassesOnEmptyNode) {
    ArrayInitializerExpression aie;
    aie.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `ElementsSlot` is the node's only slot static; the node's `Slot()` reports it for each
// child. The slot's `Kind` is the shared `Slots::Expression` (reused as the collection kind).
TEST(CSharp_ArrayInitializerExpression, ElementsSlotIsExpressionKind) {
    ArrayInitializerExpression aie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(x.get());
    EXPECT_EQ(aie.Elements().At(0)->Slot(), &ArrayInitializerExpression::ElementsSlot);
    EXPECT_EQ(aie.Elements().At(0)->Slot()->Kind(), &Slots::Expression);
    EXPECT_TRUE(aie.Elements().At(0)->Slot()->IsCollection());
}
