// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `AnonymousMethodExpression` concrete node
// (cpp/.../Syntax/Expressions/AnonymousMethodExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.cs) -- the
// `anonymous_method_expression ::= 'async'? 'delegate' parameter* block` production (C# grammar
// 12.22.1). A sealed `Expression` with one collection (`Parameters` `ParameterDeclaration`,
// non-incremental -- the node's only collection but NOT the last slot) plus a trailing single
// REQUIRED `BlockStatement` `Body` (typed the concrete `BlockStatement` because the production
// takes ONLY a `block`, unlike `LambdaExpression` whose `Body` is the abstract `AstNode` base)
// plus an `IsAsync` bool scalar, plus the `DelegateKeyword`/`AsyncModifier` const strings (the
// latter ALIASED to the canonical `LambdaExpression.AsyncModifier` source). Exercises the
// collection-then-required-single-`BlockStatement`-body slot-storage dispatch (the `Accessor` D274
// shape with the collection FIRST, the `Body` REQUIRED, and no `EntityDeclaration` base
// machinery), the `IsAsync` bool scalar, the two consts, the `AcceptVisitor` dispatch, the
// three-term generated `DoMatch` (plain-bool + collection-DoMatch + MatchRequired), the
// per-concrete-node `Clone`, and the inherited `CheckInvariant` (which rejects the empty node --
// the `Body` slot is required).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the `VisitAnonymousMethodExpression` under test
// (plus the `ParameterDeclaration`/`BlockStatement`/`ReturnStatement` of the slots, the
// `LambdaExpression` used for the cross-structural-twin DoMatch rejection, and the
// `WhileStatement` used for the cross-type DoMatch rejection), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitAnonymousMethodExpression(AnonymousMethodExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-anon>"); return; }
        trace.push_back("anon");
        VisitChildren(node);
    }
    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-param>"); return; }
        trace.push_back("param");
        VisitChildren(node);
    }
    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-block>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitReturnStatement(ReturnStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-return>"); return; }
        trace.push_back("return");
        VisitChildren(node);
    }
    void VisitLambdaExpression(LambdaExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-lambda>"); return; }
        trace.push_back("lambda");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping an `AnonymousMethodExpression` and all its children alive in the test scope
// (the port's non-owning raw-pointer child slots -- the D223 design).
struct AnonHolder {
    std::unique_ptr<AnonymousMethodExpression> anon;
    std::unique_ptr<BlockStatement> body;
    std::unique_ptr<ReturnStatement> bodyReturn;
    // Optional parameters (one ParameterDeclaration kept alive).
    std::unique_ptr<ParameterDeclaration> param0;
    AnonymousMethodExpression* get() const { return anon.get(); }
    AnonymousMethodExpression* operator->() const { return anon.get(); }
};

// Build an `AnonymousMethodExpression` with a `Body` `BlockStatement` (holding a bare
// `ReturnStatement`) and no parameters, `IsAsync` false. The holder keeps every node alive.
AnonHolder make_AnonMethod() {
    AnonHolder h;
    h.anon = std::make_unique<AnonymousMethodExpression>();
    h.body = std::make_unique<BlockStatement>();
    h.bodyReturn = std::make_unique<ReturnStatement>();
    h.body->Statements().Add(h.bodyReturn.get());
    h.anon->Body(h.body.get());
    return h;
}

// Build an `async` `AnonymousMethodExpression` with a `Body` `BlockStatement` (holding a bare
// `ReturnStatement`), `IsAsync` true, no parameters.
AnonHolder make_AnonMethodAsync() {
    AnonHolder h = make_AnonMethod();
    h.anon->IsAsync(true);
    return h;
}

// Build an `AnonymousMethodExpression` with a `Body` `BlockStatement`, one `ParameterDeclaration`
// (kept alive), `IsAsync` false.
AnonHolder make_AnonMethodWithParameter() {
    AnonHolder h = make_AnonMethod();
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.anon->Parameters().Add(h.param0.get());
    return h;
}

// Build an `AnonymousMethodExpression` with every slot filled: a `Body` `BlockStatement` (holding
// a `ReturnStatement`), a `Parameters` collection holding one `ParameterDeclaration`, `IsAsync`
// true.
AnonHolder make_AnonMethodFull() {
    AnonHolder h = make_AnonMethod();
    h.anon->IsAsync(true);
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.anon->Parameters().Add(h.param0.get());
    return h;
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `AnonymousMethodExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_AnonymousMethodExpression, IsExpressionAndAstNodeNotAstType) {
    AnonymousMethodExpression ame;
    EXPECT_NE(dynamic_cast<Expression*>(&ame), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ame), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ame), nullptr);
}

// `AnonymousMethodExpression` is a concrete (non-abstract) and `final` class (the C# is `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it). It is constructible directly.
TEST(CSharp_AnonymousMethodExpression, IsConcreteAndFinal) {
    auto ame = std::make_unique<AnonymousMethodExpression>();
    ASSERT_NE(ame, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(ame.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<AnonymousMethodExpression>);
}

// ---- The const strings ------------------------------------------------

// The `DelegateKeyword` const string is "delegate" (the `delegate` keyword token the output
// visitor emits). It is a static field, not instance state.
TEST(CSharp_AnonymousMethodExpression, DelegateKeywordIsDelegate) {
    EXPECT_STREQ(AnonymousMethodExpression::DelegateKeyword, "delegate");
}

// The `AsyncModifier` const string is ALIASED to the canonical `LambdaExpression.AsyncModifier`
// (preserving the single source of truth -- the C# `public const string AsyncModifier =
// LambdaExpression.AsyncModifier`). Both resolve to "async".
TEST(CSharp_AnonymousMethodExpression, AsyncModifierAliasesLambdaExpression) {
    EXPECT_STREQ(AnonymousMethodExpression::AsyncModifier, "async");
    EXPECT_EQ(AnonymousMethodExpression::AsyncModifier, LambdaExpression::AsyncModifier);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no initializers. `GetChildCount` is 1 (the `Parameters` collection is empty,
// plus the one `Body` single slot which contributes 1 to the flattened count even though it is null
// -- the `Accessor` D274 precedent). The `Body` is null (a half-constructed node until `Body` is
// set -- the required-slot invariant).
TEST(CSharp_AnonymousMethodExpression, EmptyCtorHasEmptyParametersAndNullBody) {
    AnonymousMethodExpression ame;
    EXPECT_EQ(ame.Parameters().Count(), 0);
    EXPECT_EQ(ame.Body(), nullptr);
    EXPECT_EQ(ame.GetChildCount(), 1);  // 0 params + 1 single (Body counts even when null)
    EXPECT_FALSE(ame.IsAsync());
    EXPECT_EQ(ame.StartLocation(), TextLocation::Empty);
}

// ---- The `IsAsync` bool scalar ----------------------------------------

// `IsAsync` defaults to `false` and round-trips through the setter (the `async` modifier flag).
// It is a plain bool field, set via the property setter (not a ctor param -- the generator adds
// only settable ENUM-typed scalars to `CtorParams`).
TEST(CSharp_AnonymousMethodExpression, IsAsyncRoundTrips) {
    AnonymousMethodExpression ame;
    EXPECT_FALSE(ame.IsAsync());
    ame.IsAsync(true);
    EXPECT_TRUE(ame.IsAsync());
    ame.IsAsync(false);
    EXPECT_FALSE(ame.IsAsync());
}

// ---- The `Parameters` collection (non-incremental) ------------------------

// `Parameters` is the node's ONLY collection, but it is NOT the last slot (`Body` trails it), so it
// is non-incremental. `Add` re-parents the element; the flattened `ChildIndex` is dynamic (the
// reindex is triggered by `Slot()`).
TEST(CSharp_AnonymousMethodExpression, ParametersCollectionAddReparentsAndReindexesDynamically) {
    AnonymousMethodExpression ame;
    auto p = std::make_unique<ParameterDeclaration>();
    ame.Parameters().Add(p.get());
    EXPECT_EQ(ame.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &ame);
    // The collection is non-incremental (NOT the node's last slot), so the flattened `ChildIndex`
    // is dynamic -- trigger the reindex via `Slot()`.
    (void)p->Slot();
    EXPECT_EQ(p->ChildIndex, 0);  // paramCount 0 + 0 -- the first parameter is at index 0
}

// ---- The `Body` slot (a REQUIRED single, index-less) ------------------

// The `Body` setter re-parents the child and detaches the previous. The slot is typed the
// concrete `BlockStatement` (the production takes ONLY a `block`, unlike `LambdaExpression` whose
// `Body` is the abstract `AstNode` base).
TEST(CSharp_AnonymousMethodExpression, BodySetterReparentsAndDetaches) {
    AnonymousMethodExpression ame;
    auto b = std::make_unique<BlockStatement>();
    ame.Body(b.get());
    EXPECT_EQ(ame.Body(), b.get());
    EXPECT_EQ(b->Parent(), &ame);
    ame.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ame.Body(), nullptr);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Parameters` collection `[0, paramCount)`, the `Body` single
// at `paramCount`.
TEST(CSharp_AnonymousMethodExpression, GetChildWalksSlots) {
    auto h = make_AnonMethodFull();
    // 1 param + 1 single (Body)
    EXPECT_EQ(h->GetChildCount(), 2);
    EXPECT_EQ(h->GetChild(0), h->Parameters().At(0));   // the ParameterDeclaration
    EXPECT_EQ(h->GetChild(1), h->Body());               // 1 (paramCount + 0)
    EXPECT_THROW(h->GetChild(2), std::out_of_range);
}

// `GetChildSlotInfo` walks the slots the same way, returning the per-node slot static.
TEST(CSharp_AnonymousMethodExpression, GetChildSlotInfoWalksSlots) {
    auto h = make_AnonMethodFull();
    EXPECT_EQ(h->GetChildSlotInfo(0), &h->ParametersSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &h->BodySlot);
    EXPECT_THROW(h->GetChildSlotInfo(2), std::out_of_range);
}

// `GetCollectionByKind` returns the `Parameters` collection for the `Parameter` kind, and null
// for any other kind.
TEST(CSharp_AnonymousMethodExpression, GetCollectionByKindRoutesCollections) {
    auto h = make_AnonMethodFull();
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Parameter), &h->Parameters());
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Expression), nullptr);  // a different kind
    EXPECT_EQ(h->GetCollectionByKind(nullptr), nullptr);
}

// `SetChild` replaces a `Parameters` element in place at index 0..paramCount (the collection's
// `SetAt` re-parents and carries the old index).
TEST(CSharp_AnonymousMethodExpression, SetChildReplacesParameterInPlace) {
    auto h = make_AnonMethodWithParameter();
    auto param2 = std::make_unique<ParameterDeclaration>();
    // Replace the ParameterDeclaration at index 0.
    h->SetChild(0, param2.get());
    EXPECT_EQ(h->Parameters().At(0), param2.get());
    EXPECT_EQ(param2->Parent(), h.get());
}

// `SetChild` replaces the `Body` single at index `paramCount` (the index-less `SetChildNode` with
// the known flattened index -- the `Accessor` D274 precedent).
TEST(CSharp_AnonymousMethodExpression, SetChildReplacesBody) {
    auto h = make_AnonMethod();
    auto body2 = std::make_unique<BlockStatement>();
    // The Body is at index paramCount = 0 (no parameters).
    h->SetChild(0, body2.get());
    EXPECT_EQ(h->Body(), body2.get());
    EXPECT_EQ(body2->Parent(), h.get());
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitAnonymousMethodExpression` (the visitor-pattern
// round-trip); the depth-first walk then visits the `Body` child (a `BlockStatement` -> its
// `ReturnStatement`).
TEST(CSharp_AnonymousMethodExpression, AcceptVisitorDispatchesToVisitAnonymousMethodExpression) {
    auto h = make_AnonMethod();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"anon", "block", "return"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_AnonymousMethodExpression, AcceptVisitorIsVirtualThroughBases) {
    auto h = make_AnonMethod();
    AstNode* asAst = h.get();
    Expression* asExpr = h.get();
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"anon", "block", "return"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"anon", "block", "return"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk over a full `AnonymousMethodExpression` visits the `Parameters` (the
// `ParameterDeclaration`) and the `Body` (`BlockStatement` -> `ReturnStatement`) in document order.
TEST(CSharp_AnonymousMethodExpression, DepthFirstWalkVisitsAllSlotsInOrder) {
    auto h = make_AnonMethodFull();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "anon",
        "param",                 // the Parameters collection (1 param)
        "block", "return"}));    // the Body (a block with a return)
}

// The depth-first walk over an empty `AnonymousMethodExpression` (no `Body`) records just the
// node itself -- `Children()` skips null slots, so an absent `Body` contributes no children, and
// the empty `Parameters` collection contributes none either.
TEST(CSharp_AnonymousMethodExpression, DepthFirstWalkOfEmptyRecordsJustNode) {
    AnonymousMethodExpression ame;  // no Body, no parameters
    RecordingVisitor v;
    ame.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"anon"}));
}

// ---- DoMatch (the generated three-term match) ------------------------

// Two `AnonymousMethodExpression`s with the same `Body` (an empty `BlockStatement`), no
// parameters, `IsAsync` false, match.
TEST(CSharp_AnonymousMethodExpression, DoMatchMatchesSameBody) {
    auto a = make_AnonMethod();
    auto b = make_AnonMethod();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Two `async` `AnonymousMethodExpression`s match (the `IsAsync` bools are both true).
TEST(CSharp_AnonymousMethodExpression, DoMatchMatchesTwoAsyncs) {
    auto a = make_AnonMethodAsync();
    auto b = make_AnonMethodAsync();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// An `async` anonymous method does not match a non-`async` anonymous method (the `IsAsync`
// plain-equality term rejects).
TEST(CSharp_AnonymousMethodExpression, DoMatchRejectsIsAsyncMismatch) {
    auto a = make_AnonMethodAsync();
    auto b = make_AnonMethod();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_FALSE(DoMatchAgainst(b.get(), a.get()));
}

// Two `AnonymousMethodExpression`s with different `Parameters` counts do not match (the collection
// `DoMatch` rejects on the count mismatch).
TEST(CSharp_AnonymousMethodExpression, DoMatchRejectsDifferentParametersCount) {
    auto a = make_AnonMethodWithParameter();
    auto b = make_AnonMethod();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_FALSE(DoMatchAgainst(b.get(), a.get()));
}

// Two `AnonymousMethodExpression`s with different `Body` values do not match (the `MatchRequired`
// term delegates to the `Body`'s own `DoMatch`, which rejects on a type/value mismatch -- here two
// `BlockStatement`s with different statement counts: one with a `ReturnStatement`, one empty).
TEST(CSharp_AnonymousMethodExpression, DoMatchRejectsDifferentBody) {
    auto a = make_AnonMethod();  // Body = BlockStatement with a ReturnStatement
    auto b = std::make_unique<AnonymousMethodExpression>();
    auto bBody = std::make_unique<BlockStatement>();  // an empty block
    b->Body(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `AnonymousMethodExpression` does not match a `LambdaExpression` candidate (the
// cross-structural-twin rejection -- both are `Expression`s with a `Parameters` collection + a
// `Body`, but the two are distinct concrete types: the anonymous method has no `Attributes`
// collection and a concrete `BlockStatement` body, the lambda has an `Attributes` collection and
// an abstract `AstNode` body; the type-check gate rejects).
TEST(CSharp_AnonymousMethodExpression, DoMatchRejectsLambdaExpressionCandidate) {
    auto h = make_AnonMethod();
    auto lambda = std::make_unique<LambdaExpression>();
    auto lambdaBody = std::make_unique<BlockStatement>();
    lambda->Body(lambdaBody.get());
    EXPECT_FALSE(DoMatchAgainst(h.get(), lambda.get()));
    EXPECT_FALSE(DoMatchAgainst(lambda.get(), h.get()));
}

// An `AnonymousMethodExpression` does not match a `WhileStatement` candidate (the type-check gate
// rejects -- a `WhileStatement` is a `Statement`, not an `Expression`/`AnonymousMethodExpression`).
TEST(CSharp_AnonymousMethodExpression, DoMatchRejectsWhileStatementCandidate) {
    auto h = make_AnonMethod();
    WhileStatement ws;
    EXPECT_FALSE(DoMatchAgainst(h.get(), &ws));
    EXPECT_FALSE(DoMatchAgainst(&ws, h.get()));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_AnonymousMethodExpression, DoMatchRejectsNullCandidate) {
    auto h = make_AnonMethod();
    EXPECT_FALSE(DoMatchAgainst(h.get(), nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `Body`, re-parents the clone, and detaches from the source. The
// `IsAsync` scalar is copied.
TEST(CSharp_AnonymousMethodExpression, CloneDeepCopiesBodyAndScalar) {
    auto h = make_AnonMethodAsync();
    std::unique_ptr<AnonymousMethodExpression> copy(h->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), h.get());
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_TRUE(copy->IsAsync());         // the IsAsync scalar is copied
    ASSERT_NE(copy->Body(), nullptr);
    EXPECT_NE(copy->Body(), h->Body());  // a fresh clone
    EXPECT_EQ(copy->Body()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(h->Body()->Parent(), h.get());
}

// `Clone` deep-copies the `Parameters` collection (every element is a fresh clone re-parented to
// the copy).
TEST(CSharp_AnonymousMethodExpression, CloneDeepCopiesParametersCollection) {
    auto h = make_AnonMethodFull();
    std::unique_ptr<AnonymousMethodExpression> copy(h->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Parameters().Count(), 1);
    EXPECT_NE(copy->Parameters().At(0), h->Parameters().At(0));  // a fresh clone
    EXPECT_EQ(copy->Parameters().At(0)->Parent(), copy.get());
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_AnonymousMethodExpression, CloneIsVirtualAndCovariant) {
    auto h = make_AnonMethod();
    AstNode* asAst = h.get();
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<AnonymousMethodExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = h.get();
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<AnonymousMethodExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of an `AnonymousMethodExpression` with an absent `Body` (a half-constructed node)
// yields a clone with an absent `Body` -- `Clone` tolerates a missing `Body` even though the slot
// is required (the invariant is enforced by `CheckInvariant`, not by `Clone`).
TEST(CSharp_AnonymousMethodExpression, CloneOfEmptyBodySkipsBody) {
    AnonymousMethodExpression original;  // no Body
    std::unique_ptr<AnonymousMethodExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Body(), nullptr);
    EXPECT_EQ(copy->Parameters().Count(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `AnonymousMethodExpression` with every slot filled passes the inherited `CheckInvariant`
// (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in
// debug builds (a no-op in NDEBUG).
TEST(CSharp_AnonymousMethodExpression, CheckInvariantPassesOnFilledNode) {
    auto h = make_AnonMethodFull();
    h->CheckInvariant();
}

// An EMPTY `AnonymousMethodExpression` (no `Body`) is REJECTED by `CheckInvariant` -- the `Body`
// slot is REQUIRED (non-nullable), so the missing required slot trips the inherited
// slot-structure verifier (the `UnaryOperatorExpression` D231 / `CastExpression` D243 /
// `LambdaExpression` D305 required-slot precedent). `EXPECT_DEATH` (a debug-only assertion; a
// no-op in NDEBUG).
TEST(CSharp_AnonymousMethodExpression, CheckInvariantRejectsEmptyNode) {
    AnonymousMethodExpression ame;  // no Body -- the required slot is empty
    EXPECT_DEATH(ame.CheckInvariant(), ".*");
}

// ---- Slot identity ----------------------------------------------------

// The per-node slot statics point at the shared `Slots` kinds: `ParametersSlot` at
// `Slots::Parameter`, `BodySlot` at `Slots::Body`. The slot statics are distinct (the
// cross-element-type EXPECT_NE crux -- cast to the common `const CSharpSlotInfo*` base to compare
// distinct element types).
TEST(CSharp_AnonymousMethodExpression, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(AnonymousMethodExpression::ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(AnonymousMethodExpression::BodySlot.Kind(), &Slots::Body);
    EXPECT_TRUE(AnonymousMethodExpression::ParametersSlot.IsCollection());
    EXPECT_FALSE(AnonymousMethodExpression::BodySlot.IsCollection());
    EXPECT_FALSE(AnonymousMethodExpression::BodySlot.IsOptional());  // Body is required
    // The two slot statics are distinct (distinct element types -- compare through the base).
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&AnonymousMethodExpression::ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&AnonymousMethodExpression::BodySlot));
}

// The `Parameters` collection elements' and the `Body`'s `Slot()` reports the per-node slot static
// (the slot system's identity check by address).
TEST(CSharp_AnonymousMethodExpression, CollectionAndBodySlotReportsSlotStatic) {
    auto h = make_AnonMethodFull();
    EXPECT_EQ(h->Parameters().At(0)->Slot(), &h->ParametersSlot);
    EXPECT_EQ(h->Body()->Slot(), &h->BodySlot);
}
