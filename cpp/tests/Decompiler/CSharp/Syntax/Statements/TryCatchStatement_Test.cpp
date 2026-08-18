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

// Tests for the try/catch family of C# AST nodes -- `CatchClause` (a NON-SEALED `AstNode`
// deriving directly from the `AstNode` root, an element of a `TryCatchStatement`'s `CatchClauses`
// collection, with four single slots -- a NULLABLE `AstType?` `Type`, a NULLABLE `string?`
// `VariableName` string-name `[Slot]` over a backing `VariableNameToken` `Identifier`, a NULLABLE
// `Expression?` `Condition`, and a REQUIRED `BlockStatement` `Body` -- plus `CatchKeyword`/
// `WhenKeyword`/`CondLPar`/`CondRPar` const strings; `hasPatternPlaceholder: true` so non-`final`;
// reusing the already-ported `Slots::Type`/`Slots::Identifier`/`Slots::Condition`/`Slots::Body`
// kinds; plus the NEW cycle-broken `Slots::CatchClause` kind) and `TryCatchStatement` (a sealed
// `Statement` structurally the `ObjectCreateExpression` D251 shape -- a single REQUIRED
// `BlockStatement` `TryBlock` + a NON-INCREMENTAL `CatchClauses AstNodeCollection<CatchClause>`
// collection + a NULLABLE `BlockStatement?` `FinallyBlock` trailing single -- plus
// `TryKeyword`/`FinallyKeyword` const strings; the NEW cycle-broken `Slots::TryBlock`/
// `Slots::FinallyBlock` kinds in `BlockStatement.hpp`, reusing `Slots::CatchClause`) -- the next
// in-order Phase-5 piece per the D268 plan ("TryCatchStatement, LocalFunctionDeclarationStatement,
// VariableDeclarationStatement ..."). The two suites share a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 multi-suite pattern); each is independently
// `--gtest_filter`-selectable.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the two try/catch-family `Visit` methods under test
// (plus the leaf statements the bodies hold, the leaf expressions the conditions hold, and the
// `SimpleType`/`Identifier` of the `Type`/`VariableName` slots), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitCatchClause(CatchClause* node) override {
        if (node == nullptr) { trace.push_back("<null-catch>"); return; }
        trace.push_back("catch");
        VisitChildren(node);
    }
    void VisitTryCatchStatement(TryCatchStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-try>"); return; }
        trace.push_back("try");
        VisitChildren(node);
    }
    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-block>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-break>"); return; }
        trace.push_back("break");
        VisitChildren(node);
    }
    void VisitContinueStatement(ContinueStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-continue>"); return; }
        trace.push_back("continue");
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
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
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

} // namespace

// ==========================================================================
// CatchClause (the catch clause)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `CatchClause` derives DIRECTLY from `AstNode` (not `Expression`/`Statement`/`AstType`); it is
// disjoint from both the `Expression` and `Statement` hierarchies (the D254 disjoint-hierarchy
// discriminator applied to a direct-`AstNode`-root node: a catch clause is an `AstNode` but NOT an
// `Expression` and NOT a `Statement`).
TEST(CSharp_CatchClause, IsAstNodeNotExpressionNotStatement) {
    CatchClause cc;
    EXPECT_NE(dynamic_cast<AstNode*>(&cc), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&cc), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&cc), nullptr);
}

// `CatchClause` is NOT `final` (the C# is non-sealed -- `hasPatternPlaceholder: true`, so the
// generated `PatternPlaceholder` derives from it; the placeholder is deferred but the class stays
// non-`final` -- the `ArrayInitializerExpression` D250 / `SwitchSection` D268 non-sealed
// precedent).
TEST(CSharp_CatchClause, IsConcreteAndNotFinal) {
    EXPECT_FALSE(std::is_final_v<CatchClause>);
}

// ---- const keyword tokens ----------------------------------------------

TEST(CSharp_CatchClause, ConstKeywords) {
    EXPECT_STREQ(CatchClause::CatchKeyword, "catch");
    EXPECT_STREQ(CatchClause::WhenKeyword, "when");
    EXPECT_STREQ(CatchClause::CondLPar, "(");
    EXPECT_STREQ(CatchClause::CondRPar, ")");
}

// ---- Construction --------------------------------------------------------

// The empty ctor leaves all four slots null (a bare `catch {}` with no type, no variable, no
// condition). `GetChildCount` is 4 (the four single slots, each counting even when empty).
TEST(CSharp_CatchClause, EmptyCtorHasNoSlots) {
    CatchClause cc;
    EXPECT_EQ(cc.Type(), nullptr);
    EXPECT_EQ(cc.VariableNameToken(), nullptr);
    EXPECT_FALSE(cc.VariableName().has_value());
    EXPECT_EQ(cc.Condition(), nullptr);
    EXPECT_EQ(cc.Body(), nullptr);
    EXPECT_EQ(cc.GetChildCount(), 4);
    EXPECT_EQ(cc.StartLocation(), TextLocation::Empty);
}

// The all-params ctor sets all four slots in declaration order (Type, VariableName, Condition,
// Body) and re-parents them.
TEST(CSharp_CatchClause, AllParamsCtorSetsSlots) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto body = std::make_unique<BlockStatement>();
    auto cond = std::make_unique<NullReferenceExpression>();
    CatchClause cc(type.get(), std::string("e"), cond.get(), body.get());
    EXPECT_EQ(cc.Type(), type.get());
    EXPECT_EQ(cc.VariableName().value_or(""), "e");
    EXPECT_NE(cc.VariableNameToken(), nullptr);
    EXPECT_EQ(cc.Condition(), cond.get());
    EXPECT_EQ(cc.Body(), body.get());
    EXPECT_EQ(type->Parent(), &cc);
    EXPECT_EQ(cc.VariableNameToken()->Parent(), &cc);
    EXPECT_EQ(cond->Parent(), &cc);
    EXPECT_EQ(body->Parent(), &cc);
    EXPECT_EQ(type->ChildIndex, 0);
    EXPECT_EQ(cc.VariableNameToken()->ChildIndex, 1);
    EXPECT_EQ(cond->ChildIndex, 2);
    EXPECT_EQ(body->ChildIndex, 3);
}

// ---- The `Type` slot ---------------------------------------------------

// The `Type` setter re-parents the new type and detaches the old one (the const-index setter at
// flattened index 0).
TEST(CSharp_CatchClause, TypeSetterReparentsAndDetaches) {
    CatchClause cc;
    auto a = std::make_unique<SimpleType>(std::string("Exception"));
    auto b = std::make_unique<SimpleType>(std::string("SystemException"));
    cc.Type(a.get());
    EXPECT_EQ(a->Parent(), &cc);
    EXPECT_EQ(cc.Type(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    cc.Type(b.get());
    EXPECT_EQ(b->Parent(), &cc);
    EXPECT_EQ(cc.Type(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Type` setter clears and detaches the old type (the bare `catch` form).
TEST(CSharp_CatchClause, TypeSetterClearsWithNull) {
    CatchClause cc;
    auto a = std::make_unique<SimpleType>(std::string("Exception"));
    cc.Type(a.get());
    cc.Type(nullptr);
    EXPECT_EQ(cc.Type(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `VariableName` string-name [Slot] -----------------------------

// The `VariableName` string setter creates the backing token via `Identifier::CreateIfNotEmpty`:
// a non-empty name creates a token with that `Name`, an empty/null name clears the token (the C#
// `string?` optionality -- the `GotoStatement.Label` D257 nullable-string-name-`[Slot]` precedent).
TEST(CSharp_CatchClause, VariableNameSetterCreatesOrClearsToken) {
    CatchClause cc;
    cc.VariableName(std::string("e"));
    ASSERT_NE(cc.VariableNameToken(), nullptr);
    EXPECT_EQ(cc.VariableNameToken()->Name(), "e");
    EXPECT_EQ(cc.VariableName().value_or(""), "e");
    EXPECT_EQ(cc.VariableNameToken()->Parent(), &cc);
    EXPECT_EQ(cc.VariableNameToken()->ChildIndex, 1);
    // An empty name clears the token.
    cc.VariableName(std::string());
    EXPECT_EQ(cc.VariableNameToken(), nullptr);
    EXPECT_FALSE(cc.VariableName().has_value());
}

// The `VariableNameToken` slot setter sets the token directly (the const-index setter at flattened
// index 1).
TEST(CSharp_CatchClause, VariableNameTokenSetterReparentsAndDetaches) {
    CatchClause cc;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("e")));
    cc.VariableNameToken(tok.get());
    EXPECT_EQ(cc.VariableNameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &cc);
    EXPECT_EQ(tok->ChildIndex, 1);
    cc.VariableNameToken(nullptr);
    EXPECT_EQ(cc.VariableNameToken(), nullptr);
    EXPECT_EQ(tok->Parent(), nullptr);  // detached
}

// ---- The `Condition` slot ---------------------------------------------

// The `Condition` setter re-parents the new condition and detaches the old one (the const-index
// setter at flattened index 2).
TEST(CSharp_CatchClause, ConditionSetterReparentsAndDetaches) {
    CatchClause cc;
    auto a = std::make_unique<NullReferenceExpression>();
    auto b = std::make_unique<NullReferenceExpression>();
    cc.Condition(a.get());
    EXPECT_EQ(a->Parent(), &cc);
    EXPECT_EQ(cc.Condition(), a.get());
    EXPECT_EQ(a->ChildIndex, 2);
    cc.Condition(b.get());
    EXPECT_EQ(b->Parent(), &cc);
    EXPECT_EQ(cc.Condition(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 2);
}

// A null `Condition` setter clears and detaches the old condition (the unfiltered `catch` form).
TEST(CSharp_CatchClause, ConditionSetterClearsWithNull) {
    CatchClause cc;
    auto a = std::make_unique<NullReferenceExpression>();
    cc.Condition(a.get());
    cc.Condition(nullptr);
    EXPECT_EQ(cc.Condition(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Body` slot (the required catch body block) -----------------

// The `Body` setter re-parents the new body and detaches the old one (the const-index setter at
// flattened index 3).
TEST(CSharp_CatchClause, BodySetterReparentsAndDetaches) {
    CatchClause cc;
    auto a = std::make_unique<BlockStatement>();
    auto b = std::make_unique<BlockStatement>();
    cc.Body(a.get());
    EXPECT_EQ(a->Parent(), &cc);
    EXPECT_EQ(cc.Body(), a.get());
    EXPECT_EQ(a->ChildIndex, 3);
    cc.Body(b.get());
    EXPECT_EQ(b->Parent(), &cc);
    EXPECT_EQ(cc.Body(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 3);
}

// A null `Body` setter clears and detaches the old body (a half-constructed node -- the
// `CheckInvariant` required-slot invariant rejects it).
TEST(CSharp_CatchClause, BodySetterClearsWithNull) {
    CatchClause cc;
    auto a = std::make_unique<BlockStatement>();
    cc.Body(a.get());
    cc.Body(nullptr);
    EXPECT_EQ(cc.Body(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- Slot-storage contract (the flat four-case switch) ---------------

// `GetChild` returns the type at index 0, the token at index 1, the condition at index 2, and
// the body at index 3.
TEST(CSharp_CatchClause, GetChildDispatchesFourSlots) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(type.get(), std::string("e"), cond.get(), body.get());
    EXPECT_EQ(cc.GetChild(0), type.get());
    EXPECT_EQ(cc.GetChild(1), cc.VariableNameToken());
    EXPECT_EQ(cc.GetChild(2), cond.get());
    EXPECT_EQ(cc.GetChild(3), body.get());
    EXPECT_THROW(cc.GetChild(4), std::out_of_range);
    EXPECT_THROW(cc.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot static for each index.
TEST(CSharp_CatchClause, GetChildSlotInfoDispatchesFourSlots) {
    CatchClause cc;
    EXPECT_EQ(cc.GetChildSlotInfo(0), &CatchClause::TypeSlot);
    EXPECT_EQ(cc.GetChildSlotInfo(1), &CatchClause::VariableNameTokenSlot);
    EXPECT_EQ(cc.GetChildSlotInfo(2), &CatchClause::ConditionSlot);
    EXPECT_EQ(cc.GetChildSlotInfo(3), &CatchClause::BodySlot);
    EXPECT_THROW(cc.GetChildSlotInfo(4), std::out_of_range);
}

// `SetChild` writes each slot in place (replacing the existing child).
TEST(CSharp_CatchClause, SetChildDispatchesFourSlots) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(type.get(), std::string("e"), cond.get(), body.get());
    auto type2 = std::make_unique<SimpleType>(std::string("SystemException"));
    cc.SetChild(0, type2.get());
    EXPECT_EQ(cc.Type(), type2.get());
    EXPECT_EQ(type2->Parent(), &cc);
    auto cond2 = std::make_unique<NullReferenceExpression>();
    cc.SetChild(2, cond2.get());
    EXPECT_EQ(cc.Condition(), cond2.get());
    EXPECT_EQ(cond2->Parent(), &cc);
    auto body2 = std::make_unique<BlockStatement>();
    cc.SetChild(3, body2.get());
    EXPECT_EQ(cc.Body(), body2.get());
    EXPECT_EQ(body2->Parent(), &cc);
    EXPECT_THROW(cc.SetChild(4, nullptr), std::out_of_range);
}

// ---- Shared Slots kind identity ----------------------------------------

TEST(CSharp_CatchClause, SlotsPointAtSharedKinds) {
    EXPECT_EQ(CatchClause::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(CatchClause::VariableNameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(CatchClause::ConditionSlot.Kind(), &Slots::Condition);
    EXPECT_EQ(CatchClause::BodySlot.Kind(), &Slots::Body);
}

// ---- IsInstanceOfType is-a cross-check --------------------------------

TEST(CSharp_CatchClause, TypeSlotAcceptsAstTypeNotStatementNotExpression) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto body = std::make_unique<BlockStatement>();
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(CatchClause::TypeSlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(CatchClause::TypeSlot.IsInstanceOfType(body.get()));
    EXPECT_FALSE(CatchClause::TypeSlot.IsInstanceOfType(expr.get()));
}

TEST(CSharp_CatchClause, VariableNameTokenSlotAcceptsIdentifierNotAstTypeNotExpression) {
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("e")));
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(CatchClause::VariableNameTokenSlot.IsInstanceOfType(tok.get()));
    EXPECT_FALSE(CatchClause::VariableNameTokenSlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(CatchClause::VariableNameTokenSlot.IsInstanceOfType(expr.get()));
}

TEST(CSharp_CatchClause, ConditionSlotAcceptsExpressionNotAstTypeNotStatement) {
    auto expr = std::make_unique<NullReferenceExpression>();
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto body = std::make_unique<BlockStatement>();
    EXPECT_TRUE(CatchClause::ConditionSlot.IsInstanceOfType(expr.get()));
    EXPECT_FALSE(CatchClause::ConditionSlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(CatchClause::ConditionSlot.IsInstanceOfType(body.get()));
}

TEST(CSharp_CatchClause, BodySlotAcceptsBlockStatementNotAstTypeNotExpression) {
    auto body = std::make_unique<BlockStatement>();
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(CatchClause::BodySlot.IsInstanceOfType(body.get()));
    EXPECT_FALSE(CatchClause::BodySlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(CatchClause::BodySlot.IsInstanceOfType(expr.get()));
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitCatchClause`; the depth-first walk then visits the `Type`
// child (a `VisitSimpleType` -> its `Identifier`), the `VariableNameToken` (a `VisitIdentifier`),
// the `Condition` (a `VisitNullReferenceExpression`), and the `Body` (a `VisitBlockStatement`) in
// document order.
TEST(CSharp_CatchClause, AcceptVisitorDispatchesToVisitCatchClause) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(type.get(), std::string("e"), cond.get(), body.get());
    RecordingVisitor v;
    cc.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "catch", "simple:Exception", "id:Exception", "id:e", "nullref", "block"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` (the dynamic dispatch the output visitor relies
// on).
TEST(CSharp_CatchClause, AcceptVisitorIsVirtualThroughAstNode) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(type.get(), std::string("e"), nullptr, body.get());
    AstNode* asAst = &cc;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "catch", "simple:Exception", "id:Exception", "id:e", "block"}));
}

// ---- Depth-first walk --------------------------------------------------

// A bare `catch {}` (no `Type`, no `VariableName`, no `Condition`, just the `Body`) records just
// "catch" then the body (the null single-slot children are skipped -- the `CaseLabel` D268
// default: label precedent).
TEST(CSharp_CatchClause, DepthFirstWalkBareCatchRecordsCatchAndBody) {
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(nullptr, std::string(), nullptr, body.get());
    RecordingVisitor v;
    cc.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"catch", "block"}));
}

// ---- DoMatch (the nullable + string + nullable + required match) -------

// Two bare `catch {}` clauses (no `Type`, no `VariableName`, no `Condition`, same `Body`) match
// (all three nullable terms both-absent, the `Body` matches).
TEST(CSharp_CatchClause, DoMatchMatchesTwoBareCatches) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(nullptr, std::string(), nullptr, ba.get());
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Type` rejects the match (the `MatchOptional` on `Type` -- the first term).
TEST(CSharp_CatchClause, DoMatchRejectsDifferentType) {
    auto ta = std::make_unique<SimpleType>(std::string("Exception"));
    auto tb = std::make_unique<SimpleType>(std::string("SystemException"));
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(ta.get(), std::string(), nullptr, ba.get());
    CatchClause b(tb.get(), std::string(), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // type names differ
}

// A `Type` present on one side but absent on the other rejects (the `MatchOptional` asymmetry).
TEST(CSharp_CatchClause, DoMatchRejectsTypeAsymmetry) {
    auto ta = std::make_unique<SimpleType>(std::string("Exception"));
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(ta.get(), std::string(), nullptr, ba.get());
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A different `VariableName` rejects the match (the `MatchString` on `VariableName`).
TEST(CSharp_CatchClause, DoMatchRejectsDifferentVariableName) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(nullptr, std::string("e"), nullptr, ba.get());
    CatchClause b(nullptr, std::string("ex"), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two binding-less `catch (T)` clauses (both `VariableName` absent) match on the `VariableName`
// term (both nullopt).
TEST(CSharp_CatchClause, DoMatchMatchesTwoBindinglessCatches) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(nullptr, std::string(), nullptr, ba.get());
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `VariableName` present on one side but absent on the other rejects (the `MatchString` on
// nullopt-vs-value).
TEST(CSharp_CatchClause, DoMatchRejectsVariableNameAsymmetry) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(nullptr, std::string("e"), nullptr, ba.get());
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `Condition` present on one side but absent on the other rejects (the `MatchOptional` on
// `Condition`).
TEST(CSharp_CatchClause, DoMatchRejectsConditionAsymmetry) {
    auto cond = std::make_unique<NullReferenceExpression>();
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a(nullptr, std::string(), cond.get(), ba.get());
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A different `Body` rejects the match (the `MatchRequired` on `Body` -- the last term).
TEST(CSharp_CatchClause, DoMatchRejectsDifferentBody) {
    auto ba = std::make_unique<BlockStatement>();  // empty body
    auto bb = std::make_unique<BlockStatement>();  // body with a break
    auto brk = std::make_unique<BreakStatement>();
    bb->Statements().Add(brk.get());
    CatchClause a(nullptr, std::string(), nullptr, ba.get());
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a body empty, b body has a break -- different
}

// A null pattern `Body` rejects (the `MatchRequired` guard -- the last term).
TEST(CSharp_CatchClause, DoMatchRejectsNullPatternBody) {
    auto bb = std::make_unique<BlockStatement>();
    CatchClause a;  // no body
    CatchClause b(nullptr, std::string(), nullptr, bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `CatchClause` does not match a different concrete type (the `other is CatchClause` gate); a
// `TryCatchStatement` is a fellow `AstNode` but not a `CatchClause` -- the cross-family rejection.
// Each candidate gets its own fresh body (a child can have only one parent -- the D235
// single-parent-guard gotcha).
TEST(CSharp_CatchClause, DoMatchRejectsTryCatchStatementCandidate) {
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(nullptr, std::string(), nullptr, body.get());
    auto tryBody = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(tryBody.get());
    EXPECT_FALSE(DoMatchAgainst(&cc, &tcs));
    EXPECT_FALSE(DoMatchAgainst(&tcs, &cc));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_CatchClause, DoMatchRejectsNullCandidate) {
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(nullptr, std::string(), nullptr, body.get());
    EXPECT_FALSE(DoMatchAgainst(&cc, nullptr));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` deep-copies all four children (the present ones), re-parents the clones, and detaches
// from the source; the absent nullable children stay absent.
TEST(CSharp_CatchClause, CloneDeepCopiesAllSlots) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BlockStatement>();
    CatchClause original(type.get(), std::string("e"), cond.get(), body.get());

    std::unique_ptr<CatchClause> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(dynamic_cast<SimpleType*>(copy->Type())->Identifier().value_or(""), "Exception");
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    ASSERT_NE(copy->VariableNameToken(), nullptr);
    EXPECT_NE(copy->VariableNameToken(), original.VariableNameToken());
    EXPECT_EQ(copy->VariableName().value_or(""), "e");
    EXPECT_EQ(copy->VariableNameToken()->Parent(), copy.get());
    ASSERT_NE(copy->Condition(), nullptr);
    EXPECT_NE(copy->Condition(), cond.get());
    EXPECT_EQ(copy->Condition()->Parent(), copy.get());
    ASSERT_NE(copy->Body(), nullptr);
    EXPECT_NE(copy->Body(), body.get());
    EXPECT_EQ(copy->Body()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(type->Parent(), &original);
    EXPECT_EQ(original.VariableNameToken()->Parent(), &original);
    EXPECT_EQ(cond->Parent(), &original);
    EXPECT_EQ(body->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override).
TEST(CSharp_CatchClause, CloneIsVirtualThroughAstNode) {
    auto body = std::make_unique<BlockStatement>();
    CatchClause original(nullptr, std::string(), nullptr, body.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<CatchClause*>(astCopy.get()), nullptr);
}

// `Clone` of a bare `catch {}` (only the `Body`) yields a bare clone (the absent nullable children
// stay absent, the `Body` is deep-cloned).
TEST(CSharp_CatchClause, CloneOfBareCatchStaysBare) {
    auto body = std::make_unique<BlockStatement>();
    CatchClause original(nullptr, std::string(), nullptr, body.get());
    std::unique_ptr<CatchClause> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->VariableNameToken(), nullptr);
    EXPECT_EQ(copy->Condition(), nullptr);
    ASSERT_NE(copy->Body(), nullptr);
    EXPECT_NE(copy->Body(), body.get());
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A `CatchClause` with the `Body` set (the `Type`/`VariableName`/`Condition` may be absent -- they
// are nullable) passes the inherited `CheckInvariant`. Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_CatchClause, CheckInvariantPassesOnFilledBody) {
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(nullptr, std::string(), nullptr, body.get());
    cc.CheckInvariant();
}

// A `CatchClause` with the `Type`, `VariableName`, `Condition`, AND `Body` set passes the
// inherited `CheckInvariant`.
TEST(CSharp_CatchClause, CheckInvariantPassesOnFullyFilled) {
    auto type = std::make_unique<SimpleType>(std::string("Exception"));
    auto cond = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BlockStatement>();
    CatchClause cc(type.get(), std::string("e"), cond.get(), body.get());
    cc.CheckInvariant();
}

// A default-constructed (empty) node violates the required-`Body` invariant (the assert fires in
// debug -- the `FixedStatement` D267 / `ForeachStatement` D265 precedent).
TEST(CSharp_CatchClause, CheckInvariantRejectsEmptyNode) {
    CatchClause cc;
    EXPECT_DEBUG_DEATH(cc.CheckInvariant(), "");
}

// ---- Slot identity -----------------------------------------------------

// The four slot statics are distinct (compared by address); they have DIFFERENT
// `CSharpSlotInfoT<T>` element types (`AstType`/`Identifier`/`Expression`/`BlockStatement`), so
// they are unrelated pointer types and must be compared by address through the common
// `CSharpSlotInfo*` base (the D222/D251 precedent).
TEST(CSharp_CatchClause, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&CatchClause::TypeSlot,
              (const CSharpSlotInfo*)&CatchClause::VariableNameTokenSlot);
    EXPECT_NE((const CSharpSlotInfo*)&CatchClause::TypeSlot,
              (const CSharpSlotInfo*)&CatchClause::ConditionSlot);
    EXPECT_NE((const CSharpSlotInfo*)&CatchClause::TypeSlot,
              (const CSharpSlotInfo*)&CatchClause::BodySlot);
    EXPECT_NE((const CSharpSlotInfo*)&CatchClause::VariableNameTokenSlot,
              (const CSharpSlotInfo*)&CatchClause::ConditionSlot);
    EXPECT_NE((const CSharpSlotInfo*)&CatchClause::VariableNameTokenSlot,
              (const CSharpSlotInfo*)&CatchClause::BodySlot);
    EXPECT_NE((const CSharpSlotInfo*)&CatchClause::ConditionSlot,
              (const CSharpSlotInfo*)&CatchClause::BodySlot);
}

// ==========================================================================
// TryCatchStatement (the try/catch/finally statement)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `TryCatchStatement` derives from `Statement`; it is disjoint from `Expression` (the D254
// disjoint-hierarchy discriminator: a try/catch statement is a `Statement` and an `AstNode` but
// NOT an `Expression`).
TEST(CSharp_TryCatchStatement, IsStatementAndAstNodeNotExpression) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    EXPECT_NE(dynamic_cast<Statement*>(&tcs), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&tcs), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&tcs), nullptr);
}

// `TryCatchStatement` is `final` (the C# `sealed`).
TEST(CSharp_TryCatchStatement, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<TryCatchStatement>);
}

// ---- const keyword tokens ----------------------------------------------

TEST(CSharp_TryCatchStatement, ConstKeywords) {
    EXPECT_STREQ(TryCatchStatement::TryKeyword, "try");
    EXPECT_STREQ(TryCatchStatement::FinallyKeyword, "finally");
}

// ---- Construction --------------------------------------------------------

// The empty ctor has no try block, no catch clauses, and no finally block. `GetChildCount` is 2
// (the two single slots `TryBlock` + `FinallyBlock`) + 0 catch clauses.
TEST(CSharp_TryCatchStatement, EmptyCtorHasNoSlots) {
    TryCatchStatement tcs;
    EXPECT_EQ(tcs.TryBlock(), nullptr);
    EXPECT_EQ(tcs.CatchClauses().Count(), 0);
    EXPECT_EQ(tcs.FinallyBlock(), nullptr);
    EXPECT_EQ(tcs.GetChildCount(), 2);  // the two single slots (empty) + 0 catch clauses
    EXPECT_EQ(tcs.StartLocation(), TextLocation::Empty);
}

// The `(BlockStatement)` ctor (the generated required-prefix ctor) sets the `TryBlock` and
// parents it at index 0.
TEST(CSharp_TryCatchStatement, CtorSetsTryBlock) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    EXPECT_EQ(tcs.TryBlock(), body.get());
    EXPECT_EQ(body->Parent(), &tcs);
    EXPECT_EQ(body->ChildIndex, 0);
}

// ---- The `TryBlock` slot ------------------------------------------------

// The `TryBlock` setter re-parents the new body and detaches the old one (the const-index setter
// at flattened index 0).
TEST(CSharp_TryCatchStatement, TryBlockSetterReparentsAndDetaches) {
    TryCatchStatement tcs;
    auto a = std::make_unique<BlockStatement>();
    auto b = std::make_unique<BlockStatement>();
    tcs.TryBlock(a.get());
    EXPECT_EQ(a->Parent(), &tcs);
    EXPECT_EQ(tcs.TryBlock(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    tcs.TryBlock(b.get());
    EXPECT_EQ(b->Parent(), &tcs);
    EXPECT_EQ(tcs.TryBlock(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `TryBlock` setter clears and detaches the old try body.
TEST(CSharp_TryCatchStatement, TryBlockSetterClearsWithNull) {
    TryCatchStatement tcs;
    auto a = std::make_unique<BlockStatement>();
    tcs.TryBlock(a.get());
    tcs.TryBlock(nullptr);
    EXPECT_EQ(tcs.TryBlock(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `CatchClauses` collection --------------------------------------

// The collection starts empty (0 count); the node's child count is the two single slots (2) + 0.
TEST(CSharp_TryCatchStatement, CatchClausesEmptyByDefault) {
    TryCatchStatement tcs;
    EXPECT_EQ(tcs.CatchClauses().Count(), 0);
    EXPECT_EQ(tcs.GetChildCount(), 2);
}

// `Add` appends an element and parents it; the collection is NON-incremental (it is the node's
// only collection but NOT its last slot -- the `FinallyBlock` single slot trails it), so `Add`
// INVALIDATES the parent's indices (the `ObjectCreateExpression.Arguments` D251 precedent). The
// element's `ChildIndex` is stale until a reindex is triggered.
TEST(CSharp_TryCatchStatement, CatchClausesAddAppendsAndParentsAndInvalidates) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody1 = std::make_unique<BlockStatement>();
    auto c1 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody1.get());
    auto cbody2 = std::make_unique<BlockStatement>();
    auto c2 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody2.get());
    tcs.CatchClauses().Add(c1.get());
    tcs.CatchClauses().Add(c2.get());
    EXPECT_EQ(tcs.CatchClauses().Count(), 2);
    EXPECT_EQ(c1->Parent(), &tcs);
    EXPECT_EQ(c2->Parent(), &tcs);
    EXPECT_FALSE(tcs.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(tcs.GetChildCount(), 4);  // tryBlock + 2 catch clauses + finallyBlock slot
}

// `GetCollectionByKind` returns the `CatchClauses` collection for the `CatchClause` kind and null
// for any other kind (the `TryBlock`/`FinallyBlock` kinds are single slots).
TEST(CSharp_TryCatchStatement, GetCollectionByKindReturnsCatchClausesForCatchClauseKind) {
    TryCatchStatement tcs;
    EXPECT_NE(tcs.GetCollectionByKind(&Slots::CatchClause), nullptr);
    EXPECT_EQ(tcs.GetCollectionByKind(&Slots::CatchClause), &tcs.CatchClauses());
    EXPECT_EQ(tcs.GetCollectionByKind(&Slots::TryBlock), nullptr);       // a single slot
    EXPECT_EQ(tcs.GetCollectionByKind(&Slots::FinallyBlock), nullptr);   // a single slot
    EXPECT_EQ(tcs.GetCollectionByKind(&Slots::Argument), nullptr);       // an unrelated kind
    EXPECT_EQ(tcs.GetCollectionByKind(nullptr), nullptr);
}

// ---- The `FinallyBlock` slot (a nullable single child after a collection) --

// The `FinallyBlock` defaults to null (absent for a `try`/`catch` without `finally`).
TEST(CSharp_TryCatchStatement, FinallyBlockNullByDefault) {
    TryCatchStatement tcs;
    EXPECT_EQ(tcs.FinallyBlock(), nullptr);
}

// The `FinallyBlock` setter re-parents the new body and detaches the old one; the setter is
// index-less (a collection precedes the slot), so a set INVALIDATES the parent's indices.
TEST(CSharp_TryCatchStatement, FinallyBlockSetterReparentsAndDetachesAndInvalidates) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto a = std::make_unique<BlockStatement>();
    auto b = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(a.get());
    EXPECT_EQ(a->Parent(), &tcs);
    EXPECT_EQ(tcs.FinallyBlock(), a.get());
    EXPECT_FALSE(tcs.ChildIndicesValid());  // index-less setter: set invalidates
    tcs.FinallyBlock(b.get());
    EXPECT_EQ(b->Parent(), &tcs);
    EXPECT_EQ(tcs.FinallyBlock(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `FinallyBlock` setter clears and detaches the old finally body.
TEST(CSharp_TryCatchStatement, FinallyBlockSetterClearsWithNull) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto a = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(a.get());
    tcs.FinallyBlock(nullptr);
    EXPECT_EQ(tcs.FinallyBlock(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- Slot-storage contract (the collection-aware dispatch) -------------

// `GetChild` returns the try block at index 0, the catch clauses at index 1+, and the finally
// block at index `1 + Count`; the collection occupies the contiguous range `[1, 1 + Count)`.
TEST(CSharp_TryCatchStatement, GetChildDispatchesTryBlockCatchClausesFinallyBlock) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody1 = std::make_unique<BlockStatement>();
    auto c1 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody1.get());
    auto cbody2 = std::make_unique<BlockStatement>();
    auto c2 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody2.get());
    tcs.CatchClauses().Add(c1.get());
    tcs.CatchClauses().Add(c2.get());
    auto fin = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(fin.get());
    EXPECT_EQ(tcs.GetChild(0), body.get());
    EXPECT_EQ(tcs.GetChild(1), c1.get());
    EXPECT_EQ(tcs.GetChild(2), c2.get());
    EXPECT_EQ(tcs.GetChild(3), fin.get());
    EXPECT_THROW(tcs.GetChild(4), std::out_of_range);
    EXPECT_THROW(tcs.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TryBlockSlot` at index 0, the `CatchClausesSlot` at index 1+,
// and the `FinallyBlockSlot` at index `1 + Count`.
TEST(CSharp_TryCatchStatement, GetChildSlotInfoDispatchesSlots) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody1 = std::make_unique<BlockStatement>();
    auto c1 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody1.get());
    tcs.CatchClauses().Add(c1.get());
    auto fin = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(fin.get());
    EXPECT_EQ(tcs.GetChildSlotInfo(0), &TryCatchStatement::TryBlockSlot);
    EXPECT_EQ(tcs.GetChildSlotInfo(1), &TryCatchStatement::CatchClausesSlot);
    EXPECT_EQ(tcs.GetChildSlotInfo(2), &TryCatchStatement::FinallyBlockSlot);
    EXPECT_THROW(tcs.GetChildSlotInfo(3), std::out_of_range);
}

TEST(CSharp_TryCatchStatement, SlotsPointAtSharedKinds) {
    EXPECT_EQ(TryCatchStatement::TryBlockSlot.Kind(), &Slots::TryBlock);
    EXPECT_EQ(TryCatchStatement::CatchClausesSlot.Kind(), &Slots::CatchClause);
    EXPECT_EQ(TryCatchStatement::FinallyBlockSlot.Kind(), &Slots::FinallyBlock);
}

// `SetChild` writes the try block at index 0, replaces a catch clause in place at index 1+, and
// writes the finally block at index `1 + Count`.
TEST(CSharp_TryCatchStatement, SetChildDispatchesSlots) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody1 = std::make_unique<BlockStatement>();
    auto c1 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody1.get());
    tcs.CatchClauses().Add(c1.get());
    // Replace the try block at index 0.
    auto body2 = std::make_unique<BlockStatement>();
    tcs.SetChild(0, body2.get());
    EXPECT_EQ(tcs.TryBlock(), body2.get());
    EXPECT_EQ(body2->Parent(), &tcs);
    // Replace the catch clause at index 1.
    auto cbody2 = std::make_unique<BlockStatement>();
    auto c2 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody2.get());
    tcs.SetChild(1, c2.get());
    EXPECT_EQ(tcs.CatchClauses().At(0), c2.get());
    EXPECT_EQ(c2->Parent(), &tcs);
    // Write the finally block at index 2 (1 catch clause).
    auto fin = std::make_unique<BlockStatement>();
    tcs.SetChild(2, fin.get());
    EXPECT_EQ(tcs.FinallyBlock(), fin.get());
    EXPECT_EQ(fin->Parent(), &tcs);
    EXPECT_THROW(tcs.SetChild(3, nullptr), std::out_of_range);  // no slot at index 3
}

// ---- The dynamic flattened-index layout (non-incremental) --------------

// After `Add`/set the parent's indices are invalid; the reindex is triggered by `Slot()` (which
// calls `EnsureChildIndices` on the parent), after which each child's `ChildIndex` is its correct
// flattened index: `TryBlock` at 0, `CatchClauses` at `[1, 1 + Count)`, `FinallyBlock` at
// `1 + Count`.
TEST(CSharp_TryCatchStatement, ChildIndicesRebuiltAfterReindex) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody1 = std::make_unique<BlockStatement>();
    auto c1 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody1.get());
    auto cbody2 = std::make_unique<BlockStatement>();
    auto c2 = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody2.get());
    tcs.CatchClauses().Add(c1.get());
    tcs.CatchClauses().Add(c2.get());
    auto fin = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(fin.get());
    ASSERT_FALSE(tcs.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)body->Slot();
    ASSERT_TRUE(tcs.ChildIndicesValid());
    EXPECT_EQ(body->ChildIndex, 0);    // TryBlock at 0
    EXPECT_EQ(c1->ChildIndex, 1);      // CatchClauses[0] at 1
    EXPECT_EQ(c2->ChildIndex, 2);      // CatchClauses[1] at 2
    EXPECT_EQ(fin->ChildIndex, 3);     // FinallyBlock at 1 + Count (1 + 2)
}

// ---- IsInstanceOfType is-a cross-check --------------------------------

TEST(CSharp_TryCatchStatement, TryBlockSlotAcceptsBlockStatementNotCatchClause) {
    auto body = std::make_unique<BlockStatement>();
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    EXPECT_TRUE(TryCatchStatement::TryBlockSlot.IsInstanceOfType(body.get()));
    EXPECT_FALSE(TryCatchStatement::TryBlockSlot.IsInstanceOfType(cc.get()));
}

TEST(CSharp_TryCatchStatement, CatchClausesSlotAcceptsCatchClauseNotBlockStatement) {
    auto body = std::make_unique<BlockStatement>();
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    EXPECT_TRUE(TryCatchStatement::CatchClausesSlot.IsInstanceOfType(cc.get()));
    EXPECT_FALSE(TryCatchStatement::CatchClausesSlot.IsInstanceOfType(body.get()));
}

TEST(CSharp_TryCatchStatement, FinallyBlockSlotAcceptsBlockStatementNotCatchClause) {
    auto body = std::make_unique<BlockStatement>();
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    EXPECT_TRUE(TryCatchStatement::FinallyBlockSlot.IsInstanceOfType(body.get()));
    EXPECT_FALSE(TryCatchStatement::FinallyBlockSlot.IsInstanceOfType(cc.get()));
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitTryCatchStatement`; the depth-first walk then visits the
// `TryBlock` (a `VisitBlockStatement`), the `CatchClauses` (a `VisitCatchClause` per element ->
// each one's `Body`), and the `FinallyBlock` (a `VisitBlockStatement`) in document order.
TEST(CSharp_TryCatchStatement, AcceptVisitorDispatchesToVisitTryCatchStatement) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    tcs.CatchClauses().Add(cc.get());
    auto fin = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(fin.get());
    RecordingVisitor v;
    tcs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "try", "block", "catch", "block", "block"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / a `Statement*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_TryCatchStatement, AcceptVisitorIsVirtualThroughBases) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    AstNode* asAst = &tcs;
    Statement* asStmt = &tcs;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asStmt->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"try", "block"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"try", "block"}));
}

// ---- Depth-first walk --------------------------------------------------

// A `try`/`catch` with no `FinallyBlock` records the try block then the catch clauses (no finally
// block child to recurse into -- the nullable single slot is absent).
TEST(CSharp_TryCatchStatement, DepthFirstWalkNoFinallyRecordsTryAndCatches) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    tcs.CatchClauses().Add(cc.get());
    RecordingVisitor v;
    tcs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"try", "block", "catch", "block"}));
}

// ---- DoMatch (the required + collection + nullable match) --------------

// Two `TryCatchStatement`s with the same try block, the same (single) catch clause, and the same
// finally block match.
TEST(CSharp_TryCatchStatement, DoMatchMatchesSameTryCatchesFinally) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    auto cbodya = std::make_unique<BlockStatement>();
    auto cbodyb = std::make_unique<BlockStatement>();
    auto ca = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbodya.get());
    auto cb = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbodyb.get());
    a.CatchClauses().Add(ca.get());
    b.CatchClauses().Add(cb.get());
    auto fa = std::make_unique<BlockStatement>();
    auto fb = std::make_unique<BlockStatement>();
    a.FinallyBlock(fa.get());
    b.FinallyBlock(fb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `TryBlock` rejects the match (the `MatchRequired` on `TryBlock` -- the first
// term). One try body is empty, the other has a break -- different content.
TEST(CSharp_TryCatchStatement, DoMatchRejectsDifferentTryBlock) {
    auto ba = std::make_unique<BlockStatement>();  // empty try body
    auto bb = std::make_unique<BlockStatement>();  // try body with a break
    auto brk = std::make_unique<BreakStatement>();
    bb->Statements().Add(brk.get());
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a try body empty, b try body has a break
}

// A null pattern `TryBlock` rejects (the `MatchRequired` guard -- the first term).
TEST(CSharp_TryCatchStatement, DoMatchRejectsNullPatternTryBlock) {
    TryCatchStatement a;  // no try block
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement b(bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `TryBlock` rejects (the `MatchRequired` flows the null child through the
// child's `DoMatch(nullptr)`, which returns false).
TEST(CSharp_TryCatchStatement, DoMatchRejectsNullCandidateTryBlock) {
    auto ba = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b;  // no try block
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `TryCatchStatement`s with the same try block but different catch-clause COUNTS do not match
// (the collection match fails when the counts differ).
TEST(CSharp_TryCatchStatement, DoMatchRejectsDifferentCatchClauseCount) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    auto cbodya = std::make_unique<BlockStatement>();
    auto ca = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbodya.get());
    a.CatchClauses().Add(ca.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 catch, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `TryCatchStatement`s with the same try block and count but different catch-clause VALUES do
// not match (the element `DoMatch` rejects -- here a `Type` on one, absent on the other).
TEST(CSharp_TryCatchStatement, DoMatchRejectsDifferentCatchClauseValue) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    auto ta = std::make_unique<SimpleType>(std::string("Exception"));
    auto cbodya = std::make_unique<BlockStatement>();
    auto ca = std::make_unique<CatchClause>(ta.get(), std::string(), nullptr, cbodya.get());
    auto cbodyb = std::make_unique<BlockStatement>();
    auto cb = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbodyb.get());
    a.CatchClauses().Add(ca.get());
    b.CatchClauses().Add(cb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // ca has a Type, cb does not
}

// Two `TryCatchStatement`s with no `FinallyBlock` match on the `FinallyBlock` term (both absent
// -- the `MatchOptional` both-absent path).
TEST(CSharp_TryCatchStatement, DoMatchMatchesBothFinallyAbsent) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));  // both no catch, no finally
}

// A `FinallyBlock` present on the pattern but absent on the candidate rejects (the `MatchOptional`
// asymmetry).
TEST(CSharp_TryCatchStatement, DoMatchRejectsFinallyBlockPresentPatternAbsentCandidate) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    auto fa = std::make_unique<BlockStatement>();
    a.FinallyBlock(fa.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `FinallyBlock` absent on the pattern but present on the candidate rejects (the `MatchOptional`
// asymmetry the other way).
TEST(CSharp_TryCatchStatement, DoMatchRejectsFinallyBlockAbsentPatternPresentCandidate) {
    auto ba = std::make_unique<BlockStatement>();
    auto bb = std::make_unique<BlockStatement>();
    TryCatchStatement a(ba.get());
    TryCatchStatement b(bb.get());
    auto fb = std::make_unique<BlockStatement>();
    b.FinallyBlock(fb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `TryCatchStatement` does not match a different concrete type (the `other is TryCatchStatement`
// gate); a `WhileStatement` is a fellow `Statement` but not a `TryCatchStatement`.
TEST(CSharp_TryCatchStatement, DoMatchRejectsWhileStatementCandidate) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    WhileStatement ws;
    EXPECT_FALSE(DoMatchAgainst(&tcs, &ws));
    EXPECT_FALSE(DoMatchAgainst(&ws, &tcs));
}

// A `TryCatchStatement` does not match a structural-twin `Expression` across the disjoint
// `Statement`/`Expression` hierarchies: an `ObjectCreateExpression` shares the single + collection
// + trailing-single shape but is an `Expression`, so the `other is TryCatchStatement` gate
// rejects. Each candidate gets its own fresh try block / type (a child can have only one parent
// -- the D235 single-parent-guard gotcha).
TEST(CSharp_TryCatchStatement, DoMatchRejectsObjectCreateExpressionCandidate) {
    auto ba = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(ba.get());
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ObjectCreateExpression oce(type.get());
    EXPECT_FALSE(DoMatchAgainst(&tcs, &oce));
    EXPECT_FALSE(DoMatchAgainst(&oce, &tcs));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_TryCatchStatement, DoMatchRejectsNullCandidate) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    EXPECT_FALSE(DoMatchAgainst(&tcs, nullptr));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` deep-copies the try block, the catch clauses, and the finally block, re-parents the
// clones, and detaches from the source.
TEST(CSharp_TryCatchStatement, CloneDeepCopiesTryCatchesFinally) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement original(body.get());
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string("e"), nullptr, cbody.get());
    original.CatchClauses().Add(cc.get());
    auto fin = std::make_unique<BlockStatement>();
    original.FinallyBlock(fin.get());

    std::unique_ptr<TryCatchStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    ASSERT_NE(copy->TryBlock(), nullptr);
    EXPECT_NE(copy->TryBlock(), body.get());
    EXPECT_EQ(copy->TryBlock()->Parent(), copy.get());
    EXPECT_EQ(copy->CatchClauses().Count(), 1);
    ASSERT_NE(copy->CatchClauses().At(0), nullptr);
    EXPECT_NE(copy->CatchClauses().At(0), cc.get());
    EXPECT_EQ(copy->CatchClauses().At(0)->VariableName().value_or(""), "e");
    EXPECT_EQ(copy->CatchClauses().At(0)->Parent(), copy.get());
    ASSERT_NE(copy->FinallyBlock(), nullptr);
    EXPECT_NE(copy->FinallyBlock(), fin.get());
    EXPECT_EQ(copy->FinallyBlock()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(body->Parent(), &original);
    EXPECT_EQ(cc->Parent(), &original);
    EXPECT_EQ(fin->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through a `Statement*` (returns a `Statement*`).
TEST(CSharp_TryCatchStatement, CloneIsVirtualAndCovariant) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement original(body.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<TryCatchStatement*>(astCopy.get()), nullptr);
    Statement* asStmt = &original;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    ASSERT_NE(stmtCopy, nullptr);
    EXPECT_NE(dynamic_cast<TryCatchStatement*>(stmtCopy.get()), nullptr);
}

// `Clone` of an empty `TryCatchStatement` (no try block, no catch clauses, no finally block)
// yields an empty clone.
TEST(CSharp_TryCatchStatement, CloneOfEmptyIsEmpty) {
    TryCatchStatement original;
    std::unique_ptr<TryCatchStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->TryBlock(), nullptr);
    EXPECT_EQ(copy->CatchClauses().Count(), 0);
    EXPECT_EQ(copy->FinallyBlock(), nullptr);
}

// `Clone` of a node with a try block and catch clauses but NO finally block yields a clone with
// no finally block (the nullable slot is skipped when absent).
TEST(CSharp_TryCatchStatement, CloneWithoutFinallyBlockHasNoFinallyBlock) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement original(body.get());
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    original.CatchClauses().Add(cc.get());
    std::unique_ptr<TryCatchStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy->TryBlock(), nullptr);
    EXPECT_EQ(copy->CatchClauses().Count(), 1);
    EXPECT_EQ(copy->FinallyBlock(), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A `TryCatchStatement` with the try block set (the `CatchClauses` may be empty -- a collection is
// never a required slot, and the `FinallyBlock` may be absent -- it is nullable) passes the
// inherited `CheckInvariant`. Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_TryCatchStatement, CheckInvariantPassesOnFilledTryBlock) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    tcs.CheckInvariant();  // no catches, no finally -- still valid
}

// A `TryCatchStatement` with the try block, a catch clause, AND the finally block set passes the
// inherited `CheckInvariant`.
TEST(CSharp_TryCatchStatement, CheckInvariantPassesOnFullyFilled) {
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    tcs.CatchClauses().Add(cc.get());
    auto fin = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(fin.get());
    tcs.CheckInvariant();
}

// A default-constructed (empty) node violates the required-`TryBlock` invariant (the assert fires
// in debug -- the `FixedStatement` D267 precedent).
TEST(CSharp_TryCatchStatement, CheckInvariantRejectsEmptyNode) {
    TryCatchStatement tcs;
    EXPECT_DEBUG_DEATH(tcs.CheckInvariant(), "");
}

// ---- Slot identity -----------------------------------------------------

// The three slot statics are distinct (compared by address); the `CatchClausesSlot` has a
// DIFFERENT `CSharpSlotInfoT<T>` element type (`CatchClause`) from the `TryBlockSlot`/
// `FinallyBlockSlot` (both `BlockStatement`), so the cross-element-type comparisons go through the
// common `CSharpSlotInfo*` base (the D222/D251 precedent). The `TryBlockSlot`/`FinallyBlockSlot`
// share the `BlockStatement` element type, so they compare directly.
TEST(CSharp_TryCatchStatement, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&TryCatchStatement::TryBlockSlot,
              (const CSharpSlotInfo*)&TryCatchStatement::CatchClausesSlot);
    EXPECT_NE((const CSharpSlotInfo*)&TryCatchStatement::TryBlockSlot,
              (const CSharpSlotInfo*)&TryCatchStatement::FinallyBlockSlot);
    EXPECT_NE((const CSharpSlotInfo*)&TryCatchStatement::CatchClausesSlot,
              (const CSharpSlotInfo*)&TryCatchStatement::FinallyBlockSlot);
    EXPECT_NE(&TryCatchStatement::TryBlockSlot, &TryCatchStatement::FinallyBlockSlot);
    auto body = std::make_unique<BlockStatement>();
    TryCatchStatement tcs(body.get());
    auto cbody = std::make_unique<BlockStatement>();
    auto cc = std::make_unique<CatchClause>(nullptr, std::string(), nullptr, cbody.get());
    tcs.CatchClauses().Add(cc.get());
    auto fin = std::make_unique<BlockStatement>();
    tcs.FinallyBlock(fin.get());
    EXPECT_EQ(tcs.TryBlock()->Slot(), &TryCatchStatement::TryBlockSlot);
    EXPECT_EQ(tcs.TryBlock()->Slot()->Kind(), &Slots::TryBlock);
    EXPECT_EQ(tcs.CatchClauses().At(0)->Slot(), &TryCatchStatement::CatchClausesSlot);
    EXPECT_EQ(tcs.CatchClauses().At(0)->Slot()->Kind(), &Slots::CatchClause);
    EXPECT_EQ(tcs.FinallyBlock()->Slot(), &TryCatchStatement::FinallyBlockSlot);
    EXPECT_EQ(tcs.FinallyBlock()->Slot()->Kind(), &Slots::FinallyBlock);
}
