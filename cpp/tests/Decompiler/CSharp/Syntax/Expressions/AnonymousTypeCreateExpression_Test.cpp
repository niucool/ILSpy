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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the `AnonymousTypeCreateExpression` concrete node (cpp/.../Syntax/Expressions/
// AnonymousTypeCreateExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.cs) -- the
// `ArrayInitializerExpression` D250 / `TupleExpression` D296 collection-only shape applied
// to the `anonymous_object_creation_expression ::= 'new' '{' expression* '}'` production
// (C# grammar 12.8.17.4). A sealed `Expression` whose sole child slot is the `Initializers`
// `AstNodeCollection<Expression>` collection, plus the `NewKeyword` const string. Exercises
// the `Initializers` collection, the collection-only slot-storage contract (an empty node
// reports `GetChildCount` 0), the `NewKeyword` const, the `AcceptVisitor` dispatch, the
// generated `DoMatch` (a single collection `DoMatch` term), the per-concrete-node `Clone`,
// and the inherited `CheckInvariant` (which passes on the empty node -- there are no required
// single slots).

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
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
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
// the inherited `VisitChildren` (the document-order walk).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitAnonymousTypeCreateExpression(AnonymousTypeCreateExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("anonnew");
        VisitChildren(node);
    }
    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-arrinit>"); return; }
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

// `AnonymousTypeCreateExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a
// the slot system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, parallel to -- not under -- `AstType`).
TEST(CSharp_AnonymousTypeCreateExpression, IsExpressionAndAstNodeNotAstType) {
    AnonymousTypeCreateExpression atce;
    EXPECT_NE(dynamic_cast<Expression*>(&atce), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&atce), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&atce), nullptr);
}

// `AnonymousTypeCreateExpression` is a concrete (non-abstract) and `final` class (the C# is
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it -- unlike `ArrayInitializerExpression` which is
// not sealed). It is constructible directly.
TEST(CSharp_AnonymousTypeCreateExpression, IsConcreteAndFinal) {
    auto atce = std::make_unique<AnonymousTypeCreateExpression>();
    ASSERT_NE(atce, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(atce.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<AnonymousTypeCreateExpression>);
}

// ---- The `NewKeyword` const -------------------------------------------

// The `NewKeyword` const string is "new" (the output visitor emits it before the initializer
// list). It is a static field, not instance state.
TEST(CSharp_AnonymousTypeCreateExpression, NewKeywordIsNew) {
    EXPECT_STREQ(AnonymousTypeCreateExpression::NewKeyword, "new");
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no initializers. `GetChildCount` is 0 (the node has no single slots -- the
// only slot is the `Initializers` collection, which is empty).
TEST(CSharp_AnonymousTypeCreateExpression, EmptyCtorHasNoInitializers) {
    AnonymousTypeCreateExpression atce;
    EXPECT_EQ(atce.Initializers().Count(), 0);
    EXPECT_EQ(atce.GetChildCount(), 0);  // no single slots + 0 initializers
    EXPECT_EQ(atce.StartLocation(), TextLocation::Empty);
}

// ---- The `Initializers` collection ----------------------------------------

// The collection starts empty (0 count); the node's child count is 0 (no single slots).
TEST(CSharp_AnonymousTypeCreateExpression, InitializersEmptyByDefault) {
    AnonymousTypeCreateExpression atce;
    EXPECT_EQ(atce.Initializers().Count(), 0);
    EXPECT_EQ(atce.GetChildCount(), 0);
}

// `Add` appends an initializer, parents it, and assigns its flattened `ChildIndex`
// incrementally (the collection is the node's only collection at its last/only slot, so an
// element's index is its local position -- baseIndex 0).
TEST(CSharp_AnonymousTypeCreateExpression, InitializersAddAppendsAndParentsIncremental) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    atce.Initializers().Add(x.get());
    atce.Initializers().Add(y.get());
    EXPECT_EQ(atce.Initializers().Count(), 2);
    EXPECT_EQ(x->Parent(), &atce);
    EXPECT_EQ(y->Parent(), &atce);
    EXPECT_EQ(x->ChildIndex, 0);  // baseIndex 0 + 0
    EXPECT_EQ(y->ChildIndex, 1);  // baseIndex 0 + 1
    EXPECT_EQ(atce.GetChildCount(), 2);  // 2 initializers, no single slots
    EXPECT_TRUE(atce.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Initializers` collection for the `Expression` kind (the
// shared `Slots::Expression`, reused as the collection kind) and null for any other kind.
TEST(CSharp_AnonymousTypeCreateExpression, GetCollectionByKindReturnsInitializersForExpressionKind) {
    AnonymousTypeCreateExpression atce;
    EXPECT_NE(atce.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(atce.GetCollectionByKind(&Slots::Expression), &atce.Initializers());
    EXPECT_EQ(atce.GetCollectionByKind(&Slots::Argument), nullptr);  // a different kind
    EXPECT_EQ(atce.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-only dispatch) -----------

// `GetChild` returns the initializers at index 0..Count; the collection occupies the contiguous
// range [0, Count). An empty node throws for any index (there are no slots at all).
TEST(CSharp_AnonymousTypeCreateExpression, GetChildDispatchesInitializers) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    atce.Initializers().Add(x.get());
    atce.Initializers().Add(y.get());
    EXPECT_EQ(atce.GetChild(0), x.get());
    EXPECT_EQ(atce.GetChild(1), y.get());
    EXPECT_THROW(atce.GetChild(2), std::out_of_range);
    EXPECT_THROW(atce.GetChild(-1), std::out_of_range);
}

// An empty node throws for every index (no single slots, no initializers).
TEST(CSharp_AnonymousTypeCreateExpression, GetChildThrowsOnEmptyNode) {
    AnonymousTypeCreateExpression atce;
    EXPECT_THROW(atce.GetChild(0), std::out_of_range);
}

// `GetChildSlotInfo` returns the `InitializersSlot` at index 0..Count (the slot identity the
// slot system compares by address).
TEST(CSharp_AnonymousTypeCreateExpression, GetChildSlotInfoDispatchesInitializers) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    EXPECT_EQ(atce.GetChildSlotInfo(0), &AnonymousTypeCreateExpression::InitializersSlot);
    EXPECT_THROW(atce.GetChildSlotInfo(1), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Expression` KIND (the shared `Slots.Expression`, reused
// as the collection kind).
TEST(CSharp_AnonymousTypeCreateExpression, InitializersSlotPointsAtExpressionKind) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    EXPECT_EQ(atce.GetChildSlotInfo(0)->Kind(), &Slots::Expression);
}

// `SetChild` replaces an initializer in place at index 0..Count (the collection's `SetAt`
// re-parents and carries the old index).
TEST(CSharp_AnonymousTypeCreateExpression, SetChildDispatchesInitializers) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    // Replace the initializer at index 0.
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    atce.SetChild(0, y.get());
    EXPECT_EQ(atce.Initializers().At(0), y.get());
    EXPECT_EQ(y->Parent(), &atce);
    EXPECT_EQ(x->Parent(), nullptr);  // detached by SetAt
    EXPECT_THROW(atce.SetChild(1, nullptr), std::out_of_range);  // no element at index 1
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitAnonymousTypeCreateExpression` (the visitor-pattern
// round-trip); the depth-first walk then visits the `Initializers` children (a
// `VisitIdentifierExpression` per element).
TEST(CSharp_AnonymousTypeCreateExpression, AcceptVisitorDispatchesToVisitAnonymousTypeCreateExpression) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    RecordingVisitor v;
    atce.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"anonnew", "idexpr:x", "id:x"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_AnonymousTypeCreateExpression, AcceptVisitorIsVirtualThroughBases) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    AstNode* asAst = &atce;
    Expression* asExpr = &atce;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"anonnew", "idexpr:x", "id:x"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"anonnew", "idexpr:x", "id:x"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits each `Initializers` child (a `VisitIdentifierExpression` -> its
// `Identifier`) in document order. An empty node records just the node itself.
TEST(CSharp_AnonymousTypeCreateExpression, DepthFirstWalkVisitsInitializersInOrder) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"), TextLocation(1, 8));
    atce.Initializers().Add(x.get());
    atce.Initializers().Add(y.get());
    RecordingVisitor v;
    atce.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "anonnew", "idexpr:x", "id:x", "idexpr:y", "id:y"}));
}

// The depth-first walk of an empty node records just the node itself (no children).
TEST(CSharp_AnonymousTypeCreateExpression, DepthFirstWalkOfEmptyRecordsJustNode) {
    AnonymousTypeCreateExpression atce;
    RecordingVisitor v;
    atce.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"anonnew"}));
}

// ---- DoMatch (the generated collection match) ------------------------

// Two `AnonymousTypeCreateExpression`s with the same (single) initializer match.
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchMatchesSameInitializers) {
    AnonymousTypeCreateExpression a;
    AnonymousTypeCreateExpression b;
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Initializers().Add(ax.get());
    b.Initializers().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two empty `AnonymousTypeCreateExpression`s match (the collection match succeeds when both
// counts are 0).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchMatchesTwoEmpties) {
    AnonymousTypeCreateExpression a;
    AnonymousTypeCreateExpression b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `AnonymousTypeCreateExpression`s with different initializer COUNTS do not match (the
// collection match fails when the counts differ).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchRejectsDifferentInitializerCount) {
    AnonymousTypeCreateExpression a;
    AnonymousTypeCreateExpression b;
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Initializers().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 initializer, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `AnonymousTypeCreateExpression`s with the same count but different initializer VALUES do
// not match (the element `DoMatch` -- a `MatchString` on the `IdentifierExpression` identifier
// -- rejects).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchRejectsDifferentInitializerValue) {
    AnonymousTypeCreateExpression a;
    AnonymousTypeCreateExpression b;
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("y"));
    a.Initializers().Add(ax.get());
    b.Initializers().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// An `AnonymousTypeCreateExpression` does not match an `ArrayInitializerExpression` candidate
// (the two share the EXACT same collection-only shape -- a sole `Expression` collection -- but
// are distinct concrete types, so the pattern matcher's `other is AnonymousTypeCreateExpression`
// gate rejects; the cross-structural-twin DoMatch rejection, the D250/D249/D296 precedent
// applied to the collection-only pair).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchRejectsArrayInitializerExpressionTwin) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    ArrayInitializerExpression aie;
    auto y = std::make_unique<IdentifierExpression>(std::string("x"));
    aie.Elements().Add(y.get());
    EXPECT_FALSE(DoMatchAgainst(&atce, &aie));
    EXPECT_FALSE(DoMatchAgainst(&aie, &atce));
}

// An `AnonymousTypeCreateExpression` does not match an `InvocationExpression` candidate (the two
// both hold an `Expression` collection but are distinct concrete types, so the type-check gate
// rejects).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchRejectsInvocationExpressionCandidate) {
    AnonymousTypeCreateExpression atce;
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_FALSE(DoMatchAgainst(&atce, &ie));
    EXPECT_FALSE(DoMatchAgainst(&ie, &atce));
}

// An `AnonymousTypeCreateExpression` does not match an `IdentifierExpression` candidate (the
// type-check gate rejects).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    AnonymousTypeCreateExpression atce;
    IdentifierExpression idexpr(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&atce, &idexpr));
    EXPECT_FALSE(DoMatchAgainst(&idexpr, &atce));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_AnonymousTypeCreateExpression, DoMatchRejectsNullCandidate) {
    AnonymousTypeCreateExpression atce;
    EXPECT_FALSE(DoMatchAgainst(&atce, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the initializers, re-parents the clones, and detaches from the source.
TEST(CSharp_AnonymousTypeCreateExpression, CloneDeepCopiesInitializers) {
    AnonymousTypeCreateExpression original;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    original.Initializers().Add(x.get());

    std::unique_ptr<AnonymousTypeCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The initializer is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Initializers().Count(), 1);
    ASSERT_NE(copy->Initializers().At(0), nullptr);
    EXPECT_NE(copy->Initializers().At(0), x.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Initializers().At(0))->Identifier(), "x");
    EXPECT_EQ(copy->Initializers().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Initializers().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_AnonymousTypeCreateExpression, CloneIsVirtualAndCovariant) {
    AnonymousTypeCreateExpression original;
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<AnonymousTypeCreateExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<AnonymousTypeCreateExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of an empty `AnonymousTypeCreateExpression` yields an empty clone.
TEST(CSharp_AnonymousTypeCreateExpression, CloneOfEmptyIsEmpty) {
    AnonymousTypeCreateExpression original;  // no initializers
    std::unique_ptr<AnonymousTypeCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Initializers().Count(), 0);
    EXPECT_EQ(copy->GetChildCount(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `AnonymousTypeCreateExpression` with initializers passes the inherited `CheckInvariant`
// (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in
// debug builds (a no-op in NDEBUG).
TEST(CSharp_AnonymousTypeCreateExpression, CheckInvariantPassesOnFilledNode) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    atce.Initializers().Add(x.get());
    atce.Initializers().Add(y.get());
    atce.CheckInvariant();
}

// An EMPTY `AnonymousTypeCreateExpression` passes `CheckInvariant` (the node has no required
// single slots -- the only slot is the `Initializers` collection, which has no required-slot
// invariant), the collection-only precedent where the empty node is invariant-valid.
TEST(CSharp_AnonymousTypeCreateExpression, CheckInvariantPassesOnEmptyNode) {
    AnonymousTypeCreateExpression atce;
    atce.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `InitializersSlot` is the node's only slot static; the node's `Slot()` reports it for each
// child. The slot's `Kind` is the shared `Slots::Expression` (reused as the collection kind).
TEST(CSharp_AnonymousTypeCreateExpression, InitializersSlotIsExpressionKind) {
    AnonymousTypeCreateExpression atce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    atce.Initializers().Add(x.get());
    EXPECT_EQ(atce.Initializers().At(0)->Slot(), &AnonymousTypeCreateExpression::InitializersSlot);
    EXPECT_EQ(atce.Initializers().At(0)->Slot()->Kind(), &Slots::Expression);
    EXPECT_TRUE(atce.Initializers().At(0)->Slot()->IsCollection());
}
