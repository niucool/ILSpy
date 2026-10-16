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
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the `InvocationExpression` concrete node (cpp/.../Syntax/Expressions/
// InvocationExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/InvocationExpression.cs) -- the second
// `Expression` with both a single `Expression` child slot (`Target`) and a collection slot
// (`Arguments`): the `MemberReferenceExpression` (D247) shape but with NO string-name `[Slot]`
// and NO scalar (the `Attribute` (D240) shape with an `Expression` target and the base class
// `Expression`). Exercises the `Target`/`Arguments` slots, the collection-aware slot-storage
// contract, the `AcceptVisitor` dispatch, the generated `DoMatch` (a `MatchRequired` on `Target`
// + a collection `DoMatch` on `Arguments`), the per-concrete-node `Clone`, and the inherited
// `CheckInvariant`.

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
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
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
// the inherited `VisitChildren` (the document-order walk). `VisitInvocationExpression`/
// `VisitIdentifierExpression`/`VisitNullReferenceExpression` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitInvocationExpression(InvocationExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("invoke");
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
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) return;
        trace.push_back("null");
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

// `InvocationExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_InvocationExpression, IsExpressionAndAstNodeNotAstType) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_NE(dynamic_cast<Expression*>(&ie), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ie), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ie), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no target and no arguments. `GetChildCount` is 1 (the empty `Target` single
// slot) + 0 arguments.
TEST(CSharp_InvocationExpression, EmptyCtorHasNoTargetOrArguments) {
    InvocationExpression ie;
    EXPECT_EQ(ie.Target(), nullptr);
    EXPECT_EQ(ie.Arguments().Count(), 0);
    EXPECT_EQ(ie.GetChildCount(), 1);  // the single slot (empty) + 0 arguments
    EXPECT_EQ(ie.StartLocation(), TextLocation::Empty);
}

// The `(Expression)` ctor (the generated required-prefix ctor) sets the `Target` and parents it
// at index 0.
TEST(CSharp_InvocationExpression, CtorSetsTarget) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_EQ(ie.Target(), target.get());
    EXPECT_EQ(target->Parent(), &ie);
    EXPECT_EQ(target->ChildIndex, 0);
}

// ---- The `Target` slot ------------------------------------------------

// The `Target` setter re-parents the new target and detaches the old one.
TEST(CSharp_InvocationExpression, TargetSetterReparentsAndDetaches) {
    InvocationExpression ie;
    auto a = std::make_unique<IdentifierExpression>(std::string("a"));
    auto b = std::make_unique<IdentifierExpression>(std::string("b"));
    ie.Target(a.get());
    EXPECT_EQ(a->Parent(), &ie);
    EXPECT_EQ(ie.Target(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    ie.Target(b.get());
    EXPECT_EQ(b->Parent(), &ie);
    EXPECT_EQ(ie.Target(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Target` setter clears and detaches the old target.
TEST(CSharp_InvocationExpression, TargetSetterClearsWithNull) {
    InvocationExpression ie;
    auto a = std::make_unique<IdentifierExpression>(std::string("a"));
    ie.Target(a.get());
    ie.Target(nullptr);
    EXPECT_EQ(ie.Target(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Arguments` collection ----------------------------------------

// The collection starts empty (0 count); the node's child count is the single slot (1) + 0.
TEST(CSharp_InvocationExpression, ArgumentsEmptyByDefault) {
    InvocationExpression ie;
    EXPECT_EQ(ie.Arguments().Count(), 0);
    EXPECT_EQ(ie.GetChildCount(), 1);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last slot, so an element's index is
// `1 + its local position`).
TEST(CSharp_InvocationExpression, ArgumentsAddAppendsAndParentsIncremental) {
    InvocationExpression ie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ie.Arguments().Add(x.get());
    ie.Arguments().Add(y.get());
    EXPECT_EQ(ie.Arguments().Count(), 2);
    EXPECT_EQ(x->Parent(), &ie);
    EXPECT_EQ(y->Parent(), &ie);
    EXPECT_EQ(x->ChildIndex, 1);  // baseIndex 1 + 0
    EXPECT_EQ(y->ChildIndex, 2);  // baseIndex 1 + 1
    EXPECT_EQ(ie.GetChildCount(), 3);  // target + 2 arguments
    EXPECT_TRUE(ie.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind and null for
// any other kind (the `TargetExpression` kind is a single slot).
TEST(CSharp_InvocationExpression, GetCollectionByKindReturnsArgumentsForArgumentKind) {
    InvocationExpression ie;
    EXPECT_NE(ie.GetCollectionByKind(&Slots::Argument), nullptr);
    EXPECT_EQ(ie.GetCollectionByKind(&Slots::Argument), &ie.Arguments());
    EXPECT_EQ(ie.GetCollectionByKind(&Slots::TargetExpression), nullptr);  // a single slot
    EXPECT_EQ(ie.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the target at index 0 and the arguments at index 1+; the collection
// occupies the contiguous range [1, 1 + Count).
TEST(CSharp_InvocationExpression, GetChildDispatchesTargetAndArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ie.Arguments().Add(x.get());
    ie.Arguments().Add(y.get());
    EXPECT_EQ(ie.GetChild(0), target.get());
    EXPECT_EQ(ie.GetChild(1), x.get());
    EXPECT_EQ(ie.GetChild(2), y.get());
    EXPECT_THROW(ie.GetChild(3), std::out_of_range);
    EXPECT_THROW(ie.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TargetSlot` at index 0 and the `ArgumentsSlot` at index 1+
// (the slot identity the slot system compares by address).
TEST(CSharp_InvocationExpression, GetChildSlotInfoDispatchesTargetAndArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ie.Arguments().Add(x.get());
    EXPECT_EQ(ie.GetChildSlotInfo(0), &InvocationExpression::TargetSlot);
    EXPECT_EQ(ie.GetChildSlotInfo(1), &InvocationExpression::ArgumentsSlot);
    EXPECT_THROW(ie.GetChildSlotInfo(2), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `TargetExpression` KIND (the shared `Slots.TargetExpression`).
TEST(CSharp_InvocationExpression, TargetSlotPointsAtTargetExpressionKind) {
    InvocationExpression ie;
    EXPECT_EQ(ie.GetChildSlotInfo(0)->Kind(), &Slots::TargetExpression);
}

// `GetChildSlotInfo(1)` points at the `Argument` KIND (the shared `Slots.Argument`).
TEST(CSharp_InvocationExpression, ArgumentsSlotPointsAtArgumentKind) {
    InvocationExpression ie;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ie.Arguments().Add(x.get());
    EXPECT_EQ(ie.GetChildSlotInfo(1)->Kind(), &Slots::Argument);
}

// `SetChild` writes the target at index 0 and replaces an argument in place at index 1+ (the
// collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_InvocationExpression, SetChildDispatchesTargetAndArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ie.Arguments().Add(x.get());
    // Replace the target at index 0.
    auto target2 = std::make_unique<IdentifierExpression>(std::string("other"));
    ie.SetChild(0, target2.get());
    EXPECT_EQ(ie.Target(), target2.get());
    EXPECT_EQ(target2->Parent(), &ie);
    EXPECT_EQ(target2->ChildIndex, 0);
    // Replace the argument at index 1.
    ie.SetChild(1, y.get());
    EXPECT_EQ(ie.Arguments().At(0), y.get());
    EXPECT_EQ(y->Parent(), &ie);
    EXPECT_THROW(ie.SetChild(2, nullptr), std::out_of_range);  // no element at index 2
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitInvocationExpression` (the visitor-pattern round-trip);
// the depth-first walk then visits the `Target` child (a `VisitIdentifierExpression`).
TEST(CSharp_InvocationExpression, AcceptVisitorDispatchesToVisitInvocationExpression) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    RecordingVisitor v;
    ie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"invoke", "idexpr:obj", "id:obj"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_InvocationExpression, AcceptVisitorIsVirtualThroughBases) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    AstNode* asAst = &ie;
    Expression* asExpr = &ie;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"invoke", "idexpr:obj", "id:obj"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"invoke", "idexpr:obj", "id:obj"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits the `Target` (a `VisitIdentifierExpression` -> its `Identifier`),
// then the `Arguments` (a `VisitIdentifierExpression` per element) in document order.
TEST(CSharp_InvocationExpression, DepthFirstWalkVisitsTargetThenArgumentsInOrder) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"), TextLocation(1, 8));
    ie.Arguments().Add(x.get());
    ie.Arguments().Add(y.get());
    RecordingVisitor v;
    ie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "invoke", "idexpr:obj", "id:obj", "idexpr:x", "id:x", "idexpr:y", "id:y"}));
}

// ---- DoMatch (the generated recursive + collection match) -------------

// Two `InvocationExpression`s with the same target and the same (single) argument match.
TEST(CSharp_InvocationExpression, DoMatchMatchesSameTargetAndArguments) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression a(ta.get());
    InvocationExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Target` rejects the match (the `MatchRequired` on `Target` -- the first term --
// rejects via the child's `DoMatch`).
TEST(CSharp_InvocationExpression, DoMatchRejectsDifferentTarget) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("other"));
    InvocationExpression a(ta.get());
    InvocationExpression b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // target names differ ("obj" vs "other")
}

// A null pattern `Target` rejects (the `MatchRequired` guard -- the first term).
TEST(CSharp_InvocationExpression, DoMatchRejectsNullPatternTarget) {
    InvocationExpression a;  // no target
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Target` rejects (the `MatchRequired` flows the null child through the
// child's `DoMatch(nullptr)`, which returns false).
TEST(CSharp_InvocationExpression, DoMatchRejectsNullCandidateTarget) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression a(ta.get());
    InvocationExpression b;  // no target
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `InvocationExpression`s with the same target but different argument COUNTS do not match
// (the collection match fails when the counts differ).
TEST(CSharp_InvocationExpression, DoMatchRejectsDifferentArgumentCount) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression a(ta.get());
    InvocationExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `InvocationExpression`s with the same target and count but different argument VALUES do
// not match (the element `DoMatch` -- a `MatchString` on the `IdentifierExpression` identifier --
// rejects).
TEST(CSharp_InvocationExpression, DoMatchRejectsDifferentArgumentValue) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression a(ta.get());
    InvocationExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("y"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// An `InvocationExpression` does not match a different concrete type (the `other is
// InvocationExpression` gate); an `IdentifierExpression` is not an `InvocationExpression`.
TEST(CSharp_InvocationExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    IdentifierExpression idexpr(std::string("obj"));
    EXPECT_FALSE(DoMatchAgainst(&ie, &idexpr));
    EXPECT_FALSE(DoMatchAgainst(&idexpr, &ie));
}

// An `InvocationExpression` does not match a `MemberReferenceExpression`, and vice versa -- the
// two share the `Target`-`Expression`-single-slot-plus-a-collection shape but are distinct
// concrete types, so the type-check gate rejects.
TEST(CSharp_InvocationExpression, DoMatchRejectsMemberReferenceExpressionCandidate) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(ta.get());
    MemberReferenceExpression mre(tb.get(), std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&ie, &mre));
    EXPECT_FALSE(DoMatchAgainst(&mre, &ie));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_InvocationExpression, DoMatchRejectsNullCandidate) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_FALSE(DoMatchAgainst(&ie, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the target and the arguments, re-parents the clones, and detaches from
// the source.
TEST(CSharp_InvocationExpression, CloneDeepCopiesTargetAndArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"), TextLocation(1, 1));
    InvocationExpression original(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    original.Arguments().Add(x.get());

    std::unique_ptr<InvocationExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The target is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Target(), nullptr);
    EXPECT_NE(copy->Target(), target.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Target())->Identifier(), "obj");
    EXPECT_EQ(copy->Target()->Parent(), copy.get());
    // The argument is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Arguments().Count(), 1);
    ASSERT_NE(copy->Arguments().At(0), nullptr);
    EXPECT_NE(copy->Arguments().At(0), x.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Arguments().At(0))->Identifier(), "x");
    EXPECT_EQ(copy->Arguments().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Arguments().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
    EXPECT_EQ(target->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_InvocationExpression, CloneIsVirtualAndCovariant) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression original(target.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<InvocationExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<InvocationExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of a target-less `InvocationExpression` (the empty ctor) with no arguments yields a
// target-less clone with no arguments.
TEST(CSharp_InvocationExpression, CloneOfEmptyIsEmpty) {
    InvocationExpression original;  // no target, no arguments
    std::unique_ptr<InvocationExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Target(), nullptr);
    EXPECT_EQ(copy->Arguments().Count(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `InvocationExpression` with the target set and arguments passes the inherited
// `CheckInvariant` (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type
// consistent). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_InvocationExpression, CheckInvariantPassesOnFilledNode) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ie.Arguments().Add(x.get());
    ie.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `TargetSlot` and `ArgumentsSlot` are distinct slot statics (compared by address); the
// node's `Slot()` reports the per-node slot for each child. Both slot statics have the same
// `CSharpSlotInfoT<Expression>` element type, so they are compared by address directly.
TEST(CSharp_InvocationExpression, SlotStaticsAreDistinct) {
    EXPECT_NE(&InvocationExpression::TargetSlot, &InvocationExpression::ArgumentsSlot);
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ie.Arguments().Add(x.get());
    EXPECT_EQ(ie.Target()->Slot(), &InvocationExpression::TargetSlot);
    EXPECT_EQ(ie.Target()->Slot()->Kind(), &Slots::TargetExpression);
    EXPECT_EQ(ie.Arguments().At(0)->Slot(), &InvocationExpression::ArgumentsSlot);
    EXPECT_EQ(ie.Arguments().At(0)->Slot()->Kind(), &Slots::Argument);
}
