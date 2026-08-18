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

// Tests for the `LambdaExpression` concrete node (cpp/.../Syntax/Expressions/LambdaExpression.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/Expressions/LambdaExpression.cs) -- the
// `lambda_expression ::= attribute_section* 'async'? parameter* '=>' ( block | expression )`
// production (C# grammar 12.22.1). A sealed `Expression` with two collections (`Attributes`
// `AttributeSection` + `Parameters` `ParameterDeclaration`, both non-incremental) plus a
// trailing single REQUIRED `AstNode` `Body` (the `UsingStatement` `ResourceAcquisition` D262
// `AstNode`-typed-slot precedent) plus an `IsAsync` bool scalar, plus the `AsyncModifier` const.
// Exercises the two-collection + trailing-single slot-storage dispatch (the `ConstructorDeclaration`
// D281 shape with one trailing single and no `NameToken`), the `IsAsync` bool scalar, the
// `AsyncModifier` const, the `AcceptVisitor` dispatch, the four-term generated `DoMatch`
// (collection-DoMatch + plain-bool + collection-DoMatch + MatchRequired), the per-concrete-node
// `Clone`, and the inherited `CheckInvariant` (which rejects the empty node -- the `Body` slot is
// required).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `VisitLambdaExpression` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`ParameterDeclaration`/
// `BlockStatement`/`ReturnStatement`/`IdentifierExpression`/`PrimitiveExpression` of the slots,
// and the `WhileStatement` used for the cross-type DoMatch rejection), recording a tag and
// recursing via `VisitChildren` (the inherited depth-first default). The trace is the visited
// nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitLambdaExpression(LambdaExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-lambda>"); return; }
        trace.push_back("lambda");
        VisitChildren(node);
    }
    void VisitAttributeSection(AttributeSection* node) override {
        if (node == nullptr) { trace.push_back("<null-attrsec>"); return; }
        trace.push_back("attrsec:" + node->AttributeTarget());
        VisitChildren(node);
    }
    void VisitAttribute(Attribute* node) override {
        if (node == nullptr) { trace.push_back("<null-attr>"); return; }
        trace.push_back("attr");
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-simple>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or(""));
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
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
    void VisitIdentifierExpression(IdentifierExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-idexpr>"); return; }
        trace.push_back("idexpr:" + node->Identifier());
        VisitChildren(node);
    }
    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim");
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

// A holder keeping a `LambdaExpression` and all its children alive in the test scope (the port's
// non-owning raw-pointer child slots -- the D223 design).
struct LambdaHolder {
    std::unique_ptr<LambdaExpression> lambda;
    std::unique_ptr<BlockStatement> body;
    std::unique_ptr<ReturnStatement> bodyReturn;
    // Optional attributes (one AttributeSection holding an Attribute with a SimpleType Type).
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    // Optional parameters (one ParameterDeclaration kept alive with its own children).
    std::unique_ptr<ParameterDeclaration> param0;
    LambdaExpression* get() const { return lambda.get(); }
    LambdaExpression* operator->() const { return lambda.get(); }
};

// Build a `LambdaExpression` with a `Body` `BlockStatement` (holding a bare `ReturnStatement`) and
// no attributes, no parameters, `IsAsync` false. The holder keeps every node alive.
LambdaHolder make_Lambda() {
    LambdaHolder h;
    h.lambda = std::make_unique<LambdaExpression>();
    h.body = std::make_unique<BlockStatement>();
    h.bodyReturn = std::make_unique<ReturnStatement>();
    h.body->Statements().Add(h.bodyReturn.get());
    h.lambda->Body(h.body.get());
    return h;
}

// Build an `async` `LambdaExpression` with a `Body` `BlockStatement` (holding a bare
// `ReturnStatement`), `IsAsync` true, no attributes, no parameters.
LambdaHolder make_LambdaAsync() {
    LambdaHolder h = make_Lambda();
    h.lambda->IsAsync(true);
    return h;
}

// Build a `LambdaExpression` with a `Body` `BlockStatement`, one `AttributeSection` (holding an
// `Attribute` whose `Type` is a `SimpleType` `Foo`), no parameters.
LambdaHolder make_LambdaWithAttribute() {
    LambdaHolder h = make_Lambda();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.lambda->Attributes().Add(h.attrSec.get());
    return h;
}

// Build a `LambdaExpression` with a `Body` `BlockStatement`, one `ParameterDeclaration` (kept
// alive), no attributes.
LambdaHolder make_LambdaWithParameter() {
    LambdaHolder h = make_Lambda();
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.lambda->Parameters().Add(h.param0.get());
    return h;
}

// Build a `LambdaExpression` with every slot filled: a `Body` `BlockStatement` (holding a
// `ReturnStatement`), an `Attributes` `AttributeSection` (holding an `Attribute` `Foo`), a
// `Parameters` collection holding one `ParameterDeclaration`, `IsAsync` true.
LambdaHolder make_LambdaFull() {
    LambdaHolder h = make_Lambda();
    h.lambda->IsAsync(true);
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.lambda->Attributes().Add(h.attrSec.get());
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.lambda->Parameters().Add(h.param0.get());
    return h;
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `LambdaExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot system
// and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_LambdaExpression, IsExpressionAndAstNodeNotAstType) {
    LambdaExpression le;
    EXPECT_NE(dynamic_cast<Expression*>(&le), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&le), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&le), nullptr);
}

// `LambdaExpression` is a concrete (non-abstract) and `final` class (the C# is `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it). It is constructible directly.
TEST(CSharp_LambdaExpression, IsConcreteAndFinal) {
    auto le = std::make_unique<LambdaExpression>();
    ASSERT_NE(le, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(le.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<LambdaExpression>);
}

// ---- The `AsyncModifier` const -----------------------------------------

// The `AsyncModifier` const string is "async" (the leading `async` modifier token the output
// visitor emits). It is a static field, not instance state. This is the canonical source of the
// `async` literal (aliased by the not-yet-ported `AnonymousMethodExpression.AsyncModifier`).
TEST(CSharp_LambdaExpression, AsyncModifierIsAsync) {
    EXPECT_STREQ(LambdaExpression::AsyncModifier, "async");
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no initializers. `GetChildCount` is 1 (the two collections are empty, plus
// the one `Body` single slot which contributes 1 to the flattened count even though it is null --
// the `Accessor` D274 precedent). The `Body` is null (a half-constructed node until `Body` is
// set -- the required-slot invariant).
TEST(CSharp_LambdaExpression, EmptyCtorHasEmptyCollectionsAndNullBody) {
    LambdaExpression le;
    EXPECT_EQ(le.Attributes().Count(), 0);
    EXPECT_EQ(le.Parameters().Count(), 0);
    EXPECT_EQ(le.Body(), nullptr);
    EXPECT_EQ(le.GetChildCount(), 1);  // 0 attrs + 0 params + 1 single (Body counts even when null)
    EXPECT_FALSE(le.IsAsync());
    EXPECT_EQ(le.StartLocation(), TextLocation::Empty);
}

// ---- The `IsAsync` bool scalar ----------------------------------------

// `IsAsync` defaults to `false` and round-trips through the setter (the `async` modifier flag).
// It is a plain bool field, set via the property setter (not a ctor param -- the generator adds
// only settable ENUM-typed scalars to `CtorParams`).
TEST(CSharp_LambdaExpression, IsAsyncRoundTrips) {
    LambdaExpression le;
    EXPECT_FALSE(le.IsAsync());
    le.IsAsync(true);
    EXPECT_TRUE(le.IsAsync());
    le.IsAsync(false);
    EXPECT_FALSE(le.IsAsync());
}

// ---- The `Attributes` collection (non-incremental) ------------------------

// `Attributes` is the node's FIRST of TWO collections, so it is non-incremental. `Add` re-parents
// the element; the flattened `ChildIndex` is dynamic (the reindex is triggered by `Slot()`).
TEST(CSharp_LambdaExpression, AttributesCollectionAddReparentsAndReindexesDynamically) {
    LambdaExpression le;
    auto attrSec = std::make_unique<AttributeSection>();
    le.Attributes().Add(attrSec.get());
    EXPECT_EQ(le.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &le);
    // The collection is non-incremental (the node has two collections), so the flattened
    // `ChildIndex` is dynamic -- trigger the reindex via `Slot()`.
    (void)attrSec->Slot();
    EXPECT_EQ(attrSec->ChildIndex, 0);  // attrCount (0) + 0
}

// ---- The `Parameters` collection (non-incremental) ------------------------

// `Parameters` is the node's SECOND of TWO collections, so it is non-incremental. `Add` re-parents
// the element; the flattened `ChildIndex` is dynamic (the reindex is triggered by `Slot()`).
TEST(CSharp_LambdaExpression, ParametersCollectionAddReparentsAndReindexesDynamically) {
    LambdaExpression le;
    auto p = std::make_unique<ParameterDeclaration>();
    le.Parameters().Add(p.get());
    EXPECT_EQ(le.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &le);
    (void)p->Slot();
    // The `Body` single slot trails the `Parameters` collection at the dynamic flattened index
    // `attrCount + paramCount` -- here 0 + 1 = 1, so the first parameter is at index 0.
    EXPECT_EQ(p->ChildIndex, 0);
}

// ---- The `Body` slot (a REQUIRED single, index-less) ------------------

// The `Body` setter re-parents the child and detaches the previous. The slot is typed the
// abstract `AstNode` base (the production takes EITHER a `BlockStatement` OR an `Expression`), so
// the setter accepts either.
TEST(CSharp_LambdaExpression, BodySetterReparentsAndDetaches) {
    LambdaExpression le;
    auto b = std::make_unique<BlockStatement>();
    le.Body(b.get());
    EXPECT_EQ(le.Body(), b.get());
    EXPECT_EQ(b->Parent(), &le);
    le.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(le.Body(), nullptr);
}

// The `Body` slot accepts an `Expression` too (the expression-bodied lambda form -- the slot is
// typed the abstract `AstNode` base so it accepts either a `BlockStatement` or an `Expression`).
TEST(CSharp_LambdaExpression, BodySlotAcceptsAnExpression) {
    LambdaExpression le;
    auto e = std::make_unique<IdentifierExpression>(std::string("x"));
    le.Body(e.get());
    EXPECT_EQ(le.Body(), e.get());
    EXPECT_EQ(e->Parent(), &le);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `Parameters`
// collection `[attrCount, attrCount + paramCount)`, the `Body` single at
// `attrCount + paramCount`.
TEST(CSharp_LambdaExpression, GetChildWalksSlots) {
    auto h = make_LambdaFull();
    // 1 attr + 1 param + 1 single (Body)
    EXPECT_EQ(h->GetChildCount(), 3);
    EXPECT_EQ(h->GetChild(0), h->Attributes().At(0));   // the AttributeSection
    EXPECT_EQ(h->GetChild(1), h->Parameters().At(0));   // 1 (attrCount + param[0])
    EXPECT_EQ(h->GetChild(2), h->Body());               // 2 (attrCount + paramCount + 0)
    EXPECT_THROW(h->GetChild(3), std::out_of_range);
}

// `GetChildSlotInfo` walks the slots the same way, returning the per-node slot static.
TEST(CSharp_LambdaExpression, GetChildSlotInfoWalksSlots) {
    auto h = make_LambdaFull();
    EXPECT_EQ(h->GetChildSlotInfo(0), &h->AttributesSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &h->ParametersSlot);
    EXPECT_EQ(h->GetChildSlotInfo(2), &h->BodySlot);
    EXPECT_THROW(h->GetChildSlotInfo(3), std::out_of_range);
}

// `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind, the
// `Parameters` collection for the `Parameter` kind, and null for any other kind.
TEST(CSharp_LambdaExpression, GetCollectionByKindRoutesCollections) {
    auto h = make_LambdaFull();
    EXPECT_EQ(h->GetCollectionByKind(&Slots::AttributeSection), &h->Attributes());
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Parameter), &h->Parameters());
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Expression), nullptr);  // a different kind
    EXPECT_EQ(h->GetCollectionByKind(nullptr), nullptr);
}

// `SetChild` replaces an `Attributes` element in place at index 0..attrCount (the collection's
// `SetAt` re-parents and carries the old index).
TEST(CSharp_LambdaExpression, SetChildReplacesAttributeInPlace) {
    auto h = make_LambdaWithAttribute();
    auto attrType2 = std::make_unique<SimpleType>(std::string("Bar"));
    auto attr2 = std::make_unique<Attribute>(attrType2.get());
    auto attrSec2 = std::make_unique<AttributeSection>(attr2.get());
    // Replace the AttributeSection at index 0.
    h->SetChild(0, attrSec2.get());
    EXPECT_EQ(h->Attributes().At(0), attrSec2.get());
    EXPECT_EQ(attrSec2->Parent(), h.get());
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitLambdaExpression` (the visitor-pattern round-trip); the
// depth-first walk then visits the `Body` child (a `BlockStatement` -> its `ReturnStatement`).
TEST(CSharp_LambdaExpression, AcceptVisitorDispatchesToVisitLambdaExpression) {
    auto h = make_Lambda();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"lambda", "block", "return"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_LambdaExpression, AcceptVisitorIsVirtualThroughBases) {
    auto h = make_Lambda();
    AstNode* asAst = h.get();
    Expression* asExpr = h.get();
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"lambda", "block", "return"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"lambda", "block", "return"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk over a full `LambdaExpression` visits the `Attributes` (the
// `AttributeSection` -> its `Attribute` -> its `SimpleType` `Foo` -> the `Identifier` `Foo`), the
// `Parameters` (the `ParameterDeclaration`), and the `Body` (`BlockStatement` ->
// `ReturnStatement`) in document order.
TEST(CSharp_LambdaExpression, DepthFirstWalkVisitsAllSlotsInOrder) {
    auto h = make_LambdaFull();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "lambda",
        "attrsec:", "attr", "simple:Foo", "id:Foo",   // the Attributes collection (1 section)
        "param",                                          // the Parameters collection (1 param)
        "block", "return"}));                            // the Body (a block with a return)
}

// The depth-first walk over an empty `LambdaExpression` (no `Body`) records just the node itself
// -- `Children()` skips null slots, so an absent `Body` contributes no children, and the two
// empty collections contribute none either.
TEST(CSharp_LambdaExpression, DepthFirstWalkOfEmptyRecordsJustNode) {
    LambdaExpression le;  // no Body, no attributes, no parameters
    RecordingVisitor v;
    le.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"lambda"}));
}

// ---- DoMatch (the generated four-term match) ------------------------

// Two `LambdaExpression`s with the same `Body` (an empty `BlockStatement`), no attributes, no
// parameters, `IsAsync` false, match.
TEST(CSharp_LambdaExpression, DoMatchMatchesSameBody) {
    auto a = make_Lambda();
    auto b = make_Lambda();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Two `async` `LambdaExpression`s match (the `IsAsync` bools are both true).
TEST(CSharp_LambdaExpression, DoMatchMatchesTwoAsyncs) {
    auto a = make_LambdaAsync();
    auto b = make_LambdaAsync();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// An `async` lambda does not match a non-`async` lambda (the `IsAsync` plain-equality term
// rejects).
TEST(CSharp_LambdaExpression, DoMatchRejectsIsAsyncMismatch) {
    auto a = make_LambdaAsync();
    auto b = make_Lambda();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_FALSE(DoMatchAgainst(b.get(), a.get()));
}

// Two `LambdaExpression`s with different `Attributes` counts do not match (the collection
// `DoMatch` rejects on the count mismatch).
TEST(CSharp_LambdaExpression, DoMatchRejectsDifferentAttributesCount) {
    auto a = make_LambdaWithAttribute();
    auto b = make_Lambda();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_FALSE(DoMatchAgainst(b.get(), a.get()));
}

// Two `LambdaExpression`s with different `Parameters` counts do not match (the collection
// `DoMatch` rejects on the count mismatch).
TEST(CSharp_LambdaExpression, DoMatchRejectsDifferentParametersCount) {
    auto a = make_LambdaWithParameter();
    auto b = make_Lambda();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_FALSE(DoMatchAgainst(b.get(), a.get()));
}

// Two `LambdaExpression`s with different `Body` values do not match (the `MatchRequired` term
// delegates to the `Body`'s own `DoMatch`, which rejects on a type/value mismatch -- here a
// `BlockStatement` body vs an `IdentifierExpression` body).
TEST(CSharp_LambdaExpression, DoMatchRejectsDifferentBody) {
    auto a = make_Lambda();  // Body = BlockStatement
    auto b = std::make_unique<LambdaExpression>();
    auto bBody = std::make_unique<IdentifierExpression>(std::string("x"));
    b->Body(bBody.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `LambdaExpression` does not match a `WhileStatement` candidate (the type-check gate rejects --
// a `WhileStatement` is a `Statement`, not an `Expression`/`LambdaExpression`).
TEST(CSharp_LambdaExpression, DoMatchRejectsWhileStatementCandidate) {
    auto h = make_Lambda();
    WhileStatement ws;
    EXPECT_FALSE(DoMatchAgainst(h.get(), &ws));
    EXPECT_FALSE(DoMatchAgainst(&ws, h.get()));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_LambdaExpression, DoMatchRejectsNullCandidate) {
    auto h = make_Lambda();
    EXPECT_FALSE(DoMatchAgainst(h.get(), nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `Body`, re-parents the clone, and detaches from the source. The
// `IsAsync` scalar is copied.
TEST(CSharp_LambdaExpression, CloneDeepCopiesBodyAndScalar) {
    auto h = make_LambdaAsync();
    std::unique_ptr<LambdaExpression> copy(h->Clone());
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

// `Clone` deep-copies the `Attributes` and `Parameters` collections (every element is a fresh
// clone re-parented to the copy).
TEST(CSharp_LambdaExpression, CloneDeepCopiesCollections) {
    auto h = make_LambdaFull();
    std::unique_ptr<LambdaExpression> copy(h->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Attributes().Count(), 1);
    EXPECT_NE(copy->Attributes().At(0), h->Attributes().At(0));  // a fresh clone
    EXPECT_EQ(copy->Attributes().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->Parameters().Count(), 1);
    EXPECT_NE(copy->Parameters().At(0), h->Parameters().At(0));  // a fresh clone
    EXPECT_EQ(copy->Parameters().At(0)->Parent(), copy.get());
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_LambdaExpression, CloneIsVirtualAndCovariant) {
    auto h = make_Lambda();
    AstNode* asAst = h.get();
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<LambdaExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = h.get();
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<LambdaExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of a `LambdaExpression` with an absent `Body` (a half-constructed node) yields a clone
// with an absent `Body` -- `Clone` tolerates a missing `Body` even though the slot is required
// (the invariant is enforced by `CheckInvariant`, not by `Clone`).
TEST(CSharp_LambdaExpression, CloneOfEmptyBodySkipsBody) {
    LambdaExpression original;  // no Body
    std::unique_ptr<LambdaExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Body(), nullptr);
    EXPECT_EQ(copy->Attributes().Count(), 0);
    EXPECT_EQ(copy->Parameters().Count(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A `LambdaExpression` with every slot filled passes the inherited `CheckInvariant` (the
// slot-structure verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in debug
// builds (a no-op in NDEBUG).
TEST(CSharp_LambdaExpression, CheckInvariantPassesOnFilledNode) {
    auto h = make_LambdaFull();
    h->CheckInvariant();
}

// An EMPTY `LambdaExpression` (no `Body`) is REJECTED by `CheckInvariant` -- the `Body` slot is
// REQUIRED (non-nullable), so the missing required slot trips the inherited slot-structure
// verifier (the `UnaryOperatorExpression` D231 / `CastExpression` D243 required-slot precedent).
// `EXPECT_DEATH` (a debug-only assertion; a no-op in NDEBUG).
TEST(CSharp_LambdaExpression, CheckInvariantRejectsEmptyNode) {
    LambdaExpression le;  // no Body -- the required slot is empty
    EXPECT_DEATH(le.CheckInvariant(), ".*");
}

// ---- Slot identity ----------------------------------------------------

// The per-node slot statics point at the shared `Slots` kinds: `AttributesSlot` at
// `Slots::AttributeSection`, `ParametersSlot` at `Slots::Parameter`, `BodySlot` at
// `Slots::LambdaBody`. The slot statics are distinct (the cross-element-type EXPECT_NE crux --
// cast to the common `const CSharpSlotInfo*` base to compare distinct element types).
TEST(CSharp_LambdaExpression, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(LambdaExpression::AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(LambdaExpression::ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(LambdaExpression::BodySlot.Kind(), &Slots::LambdaBody);
    EXPECT_TRUE(LambdaExpression::AttributesSlot.IsCollection());
    EXPECT_TRUE(LambdaExpression::ParametersSlot.IsCollection());
    EXPECT_FALSE(LambdaExpression::BodySlot.IsCollection());
    EXPECT_FALSE(LambdaExpression::BodySlot.IsOptional());  // Body is required
    // The three slot statics are distinct (distinct element types -- compare through the base).
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&LambdaExpression::AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&LambdaExpression::ParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&LambdaExpression::ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&LambdaExpression::BodySlot));
}

// The `Attributes`/`Parameters` collection elements' `Slot()` reports the per-node slot static
// (the slot system's identity check by address).
TEST(CSharp_LambdaExpression, CollectionElementsSlotReportsAttributesSlot) {
    auto h = make_LambdaFull();
    EXPECT_EQ(h->Attributes().At(0)->Slot(), &h->AttributesSlot);
    EXPECT_EQ(h->Parameters().At(0)->Slot(), &h->ParametersSlot);
    EXPECT_EQ(h->Body()->Slot(), &h->BodySlot);
}
