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

// Tests for the `FixedStatement` concrete node (cpp/.../Syntax/Statements/FixedStatement.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/Statements/FixedStatement.cs) -- the next in-order
// Phase-5 piece per the D266 plan (now unblocked -- it needs the `VariableInitializer` just ported
// plus a NEW `Slots::Variable` kind for its `Variables AstNodeCollection<VariableInitializer>`
// collection, plus the already-ported `Slots::Type`/`Slots::EmbeddedStatement` kinds). The
// `fixed_statement ::= 'fixed' '(' type variable_initializer* ')' statement` (C# grammar 24.7):
// a sealed `Statement` with a single REQUIRED `AstType Type` slot at flattened index 0, a
// `Variables AstNodeCollection<VariableInitializer>` collection at index 1 (non-incremental
// since the `EmbeddedStatement` single slot follows), and a single REQUIRED `Statement
// EmbeddedStatement` at index 2 (the index-less `SetChildNode` setter, following a collection) --
// the `ObjectCreateExpression` D251 shape (single + collection + trailing single) with a
// `Statement` base and a REQUIRED trailing single. The suite shares a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `VisitFixedStatement` under test (plus the leaf
// statements it holds, the structural-twin `WhileStatement`, the `SimpleType`/`Identifier` of its
// `Type` slot, and the `VariableInitializer`/`Identifier` of its `Variables` collection),
// recording a tag and recursing via `VisitChildren` (the inherited depth-first default). The
// trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitFixedStatement(FixedStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-fixed>"); return; }
        trace.push_back("fixed");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
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
    void VisitVariableInitializer(VariableInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-varinit>"); return; }
        trace.push_back("varinit:" + node->Name());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
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
// FixedStatement (the fixed pin statement)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `FixedStatement` derives from `Statement`; it is disjoint from `Expression` (the D254
// disjoint-hierarchy discriminator applied to the slot-bearing statements: a fixed statement is
// a `Statement` and an `AstNode` but NOT an `Expression`).
TEST(CSharp_FixedStatement, IsStatementAndAstNodeNotExpression) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    EXPECT_NE(dynamic_cast<Statement*>(&fs), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&fs), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&fs), nullptr);
}

// `FixedStatement` is `final` (the C# `sealed`).
TEST(CSharp_FixedStatement, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<FixedStatement>);
}

// ---- Construction --------------------------------------------------------

// The empty ctor has no type, no variables, and no body. `GetChildCount` is 2 (the two empty
// single slots `Type` + `EmbeddedStatement`) + 0 variables.
TEST(CSharp_FixedStatement, EmptyCtorHasNoTypeOrVariablesOrEmbeddedStatement) {
    FixedStatement fs;
    EXPECT_EQ(fs.Type(), nullptr);
    EXPECT_EQ(fs.Variables().Count(), 0);
    EXPECT_EQ(fs.EmbeddedStatement(), nullptr);
    EXPECT_EQ(fs.GetChildCount(), 2);  // the two single slots (empty) + 0 variables
    EXPECT_EQ(fs.StartLocation(), TextLocation::Empty);
    EXPECT_STREQ(fs.FixedKeyword, "fixed");
}

// The `(AstType)` ctor (the generated required-prefix ctor) sets the `Type` and parents it at
// index 0.
TEST(CSharp_FixedStatement, CtorSetsType) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    EXPECT_EQ(fs.Type(), type.get());
    EXPECT_EQ(type->Parent(), &fs);
    EXPECT_EQ(type->ChildIndex, 0);
}

// ---- The `Type` slot ---------------------------------------------------

// The `Type` setter re-parents the new type and detaches the old one.
TEST(CSharp_FixedStatement, TypeSetterReparentsAndDetaches) {
    FixedStatement fs;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    fs.Type(a.get());
    EXPECT_EQ(a->Parent(), &fs);
    EXPECT_EQ(fs.Type(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    fs.Type(b.get());
    EXPECT_EQ(b->Parent(), &fs);
    EXPECT_EQ(fs.Type(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Type` setter clears and detaches the old type.
TEST(CSharp_FixedStatement, TypeSetterClearsWithNull) {
    FixedStatement fs;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    fs.Type(a.get());
    fs.Type(nullptr);
    EXPECT_EQ(fs.Type(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Variables` collection -----------------------------------------

// The collection starts empty (0 count); the node's child count is the two single slots (2) + 0.
TEST(CSharp_FixedStatement, VariablesEmptyByDefault) {
    FixedStatement fs;
    EXPECT_EQ(fs.Variables().Count(), 0);
    EXPECT_EQ(fs.GetChildCount(), 2);
}

// `Add` appends an element and parents it; the collection is NON-incremental (it is the node's
// only collection but NOT its last slot -- the `EmbeddedStatement` single slot trails it), so
// `Add` INVALIDATES the parent's indices (unlike `Attribute`/`InvocationExpression` whose
// only-and-last collection maintains the index incrementally). The element's `ChildIndex` is
// stale until a reindex is triggered.
TEST(CSharp_FixedStatement, VariablesAddAppendsAndParentsAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    fs.Variables().Add(x.get());
    fs.Variables().Add(y.get());
    EXPECT_EQ(fs.Variables().Count(), 2);
    EXPECT_EQ(x->Parent(), &fs);
    EXPECT_EQ(y->Parent(), &fs);
    EXPECT_FALSE(fs.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(fs.GetChildCount(), 4);  // type + 2 variables + embedded-statement slot
}

// `GetCollectionByKind` returns the `Variables` collection for the `Variable` kind and null for
// any other kind (the `Type`/`EmbeddedStatement` kinds are single slots).
TEST(CSharp_FixedStatement, GetCollectionByKindReturnsVariablesForVariableKind) {
    FixedStatement fs;
    EXPECT_NE(fs.GetCollectionByKind(&Slots::Variable), nullptr);
    EXPECT_EQ(fs.GetCollectionByKind(&Slots::Variable), &fs.Variables());
    EXPECT_EQ(fs.GetCollectionByKind(&Slots::Type), nullptr);              // a single slot
    EXPECT_EQ(fs.GetCollectionByKind(&Slots::EmbeddedStatement), nullptr); // a single slot
    EXPECT_EQ(fs.GetCollectionByKind(&Slots::Argument), nullptr);         // an unrelated kind
    EXPECT_EQ(fs.GetCollectionByKind(nullptr), nullptr);
}

// ---- The `EmbeddedStatement` slot (a required single child after a collection) --

// The `EmbeddedStatement` defaults to null (unfilled -- a required slot, so a node without a body
// violates the invariant; `CheckInvariant` rejects it).
TEST(CSharp_FixedStatement, EmbeddedStatementNullByDefault) {
    FixedStatement fs;
    EXPECT_EQ(fs.EmbeddedStatement(), nullptr);
}

// The `EmbeddedStatement` setter re-parents the new body and detaches the old one; the setter is
// index-less (a collection precedes the slot), so a set INVALIDATES the parent's indices.
TEST(CSharp_FixedStatement, EmbeddedStatementSetterReparentsAndDetachesAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto a = std::make_unique<BreakStatement>();
    auto b = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(a.get());
    EXPECT_EQ(a->Parent(), &fs);
    EXPECT_EQ(fs.EmbeddedStatement(), a.get());
    EXPECT_FALSE(fs.ChildIndicesValid());  // index-less setter: set invalidates
    fs.EmbeddedStatement(b.get());
    EXPECT_EQ(b->Parent(), &fs);
    EXPECT_EQ(fs.EmbeddedStatement(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `EmbeddedStatement` setter clears and detaches the old body.
TEST(CSharp_FixedStatement, EmbeddedStatementSetterClearsWithNull) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto a = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(a.get());
    fs.EmbeddedStatement(nullptr);
    EXPECT_EQ(fs.EmbeddedStatement(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- Slot-storage contract (the collection-aware dispatch) -------------

// `GetChild` returns the type at index 0, the variables at index 1+, and the body at
// index `1 + Count`; the collection occupies the contiguous range `[1, 1 + Count)`.
TEST(CSharp_FixedStatement, GetChildDispatchesTypeVariablesEmbeddedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    fs.Variables().Add(x.get());
    fs.Variables().Add(y.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    EXPECT_EQ(fs.GetChild(0), type.get());
    EXPECT_EQ(fs.GetChild(1), x.get());
    EXPECT_EQ(fs.GetChild(2), y.get());
    EXPECT_EQ(fs.GetChild(3), body.get());
    EXPECT_THROW(fs.GetChild(4), std::out_of_range);
    EXPECT_THROW(fs.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TypeSlot` at index 0, the `VariablesSlot` at index 1+, and the
// `EmbeddedStatementSlot` at index `1 + Count` (the slot identity the slot system compares by
// address).
TEST(CSharp_FixedStatement, GetChildSlotInfoDispatchesTypeVariablesEmbeddedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fs.Variables().Add(x.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    EXPECT_EQ(fs.GetChildSlotInfo(0), &FixedStatement::TypeSlot);
    EXPECT_EQ(fs.GetChildSlotInfo(1), &FixedStatement::VariablesSlot);
    EXPECT_EQ(fs.GetChildSlotInfo(2), &FixedStatement::EmbeddedStatementSlot);
    EXPECT_THROW(fs.GetChildSlotInfo(3), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Type` KIND (the shared `Slots.Type`, ported by `Attribute`).
TEST(CSharp_FixedStatement, TypeSlotPointsAtTypeKind) {
    FixedStatement fs;
    EXPECT_EQ(fs.GetChildSlotInfo(0)->Kind(), &Slots::Type);
}

// `GetChildSlotInfo(1)` points at the `Variable` KIND (the new `Slots.Variable`, cycle-broken
// into VariableInitializer.hpp).
TEST(CSharp_FixedStatement, VariablesSlotPointsAtVariableKind) {
    FixedStatement fs;
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fs.Variables().Add(x.get());
    EXPECT_EQ(fs.GetChildSlotInfo(1)->Kind(), &Slots::Variable);
}

// `GetChildSlotInfo(1 + Count)` points at the `EmbeddedStatement` KIND (the shared
// `Slots.EmbeddedStatement`, ported by `WhileStatement`).
TEST(CSharp_FixedStatement, EmbeddedStatementSlotPointsAtEmbeddedStatementKind) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    // The EmbeddedStatement is at flattened index 1 (no variables); trigger a reindex so
    // GetChildSlotInfo routes to the trailing single slot.
    (void)type->Slot();
    EXPECT_EQ(fs.GetChildSlotInfo(1)->Kind(), &Slots::EmbeddedStatement);
}

// `SetChild` writes the type at index 0, replaces a variable in place at index 1+, and writes
// the body at index `1 + Count` (the collection's `SetAt` re-parents and carries the old index;
// the single-slot `SetChildNode` re-parents).
TEST(CSharp_FixedStatement, SetChildDispatchesTypeVariablesEmbeddedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fs.Variables().Add(x.get());
    // Replace the type at index 0.
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    fs.SetChild(0, type2.get());
    EXPECT_EQ(fs.Type(), type2.get());
    EXPECT_EQ(type2->Parent(), &fs);
    // Replace the variable at index 1.
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    fs.SetChild(1, y.get());
    EXPECT_EQ(fs.Variables().At(0), y.get());
    EXPECT_EQ(y->Parent(), &fs);
    // Write the body at index 2 (1 variable).
    auto body = std::make_unique<BreakStatement>();
    fs.SetChild(2, body.get());
    EXPECT_EQ(fs.EmbeddedStatement(), body.get());
    EXPECT_EQ(body->Parent(), &fs);
    EXPECT_THROW(fs.SetChild(3, nullptr), std::out_of_range);  // no slot at index 3
}

// ---- The dynamic flattened-index layout (non-incremental) --------------

// After `Add`/set the parent's indices are invalid; the reindex is triggered by `Slot()` (which
// calls `EnsureChildIndices` on the parent), after which each child's `ChildIndex` is its
// correct flattened index: `Type` at 0, `Variables` at `[1, 1 + Count)`, `EmbeddedStatement` at
// `1 + Count`.
TEST(CSharp_FixedStatement, ChildIndicesRebuiltAfterReindex) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    fs.Variables().Add(x.get());
    fs.Variables().Add(y.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    ASSERT_FALSE(fs.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)type->Slot();
    ASSERT_TRUE(fs.ChildIndicesValid());
    EXPECT_EQ(type->ChildIndex, 0);       // Type at 0
    EXPECT_EQ(x->ChildIndex, 1);          // Variables[0] at 1
    EXPECT_EQ(y->ChildIndex, 2);          // Variables[1] at 2
    EXPECT_EQ(body->ChildIndex, 3);       // EmbeddedStatement at 1 + Count (1 + 2)
}

// ---- Shared Slots kind identity ----------------------------------------

// The `TypeSlot`'s kind is the SAME `Slots::Type` that `Attribute` (the first `[Slot("Type")]`
// node) registered -- the kind-collapsing design (one kind per `[Slot]` name).
TEST(CSharp_FixedStatement, TypeSlotKindSharedWithAttribute) {
    EXPECT_EQ(FixedStatement::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(Attribute::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(FixedStatement::TypeSlot.Kind(), Attribute::TypeSlot.Kind());
}

// The `EmbeddedStatementSlot`'s kind is the SAME `Slots::EmbeddedStatement` that `WhileStatement`
// (the first `[Slot("EmbeddedStatement")]` node) registered.
TEST(CSharp_FixedStatement, EmbeddedStatementSlotKindSharedWithWhileStatement) {
    EXPECT_EQ(FixedStatement::EmbeddedStatementSlot.Kind(), &Slots::EmbeddedStatement);
    EXPECT_EQ(WhileStatement::EmbeddedStatementSlot.Kind(), &Slots::EmbeddedStatement);
    EXPECT_EQ(FixedStatement::EmbeddedStatementSlot.Kind(), WhileStatement::EmbeddedStatementSlot.Kind());
}

// ---- IsInstanceOfType is-a cross-check --------------------------------

// The `TypeSlot` accepts an `AstType` (e.g. a `SimpleType`) and rejects a `Statement` (e.g. a
// `BreakStatement`) -- the slot's declared child type is `AstType`.
TEST(CSharp_FixedStatement, TypeSlotAcceptsAstTypeNotStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto body = std::make_unique<BreakStatement>();
    EXPECT_TRUE(FixedStatement::TypeSlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(FixedStatement::TypeSlot.IsInstanceOfType(body.get()));
}

// The `VariablesSlot` accepts a `VariableInitializer` and rejects an `Expression` (e.g. a
// `NullReferenceExpression`) and a `Statement` (e.g. a `BreakStatement`) -- the slot's declared
// child type is `VariableInitializer`.
TEST(CSharp_FixedStatement, VariablesSlotAcceptsVariableInitializerNotExpressionNotStatement) {
    auto v = std::make_unique<VariableInitializer>(std::string("x"));
    auto expr = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BreakStatement>();
    EXPECT_TRUE(FixedStatement::VariablesSlot.IsInstanceOfType(v.get()));
    EXPECT_FALSE(FixedStatement::VariablesSlot.IsInstanceOfType(expr.get()));
    EXPECT_FALSE(FixedStatement::VariablesSlot.IsInstanceOfType(body.get()));
}

// The `EmbeddedStatementSlot` accepts a `Statement` (e.g. a `BreakStatement`) and rejects an
// `AstType` (e.g. a `SimpleType`) and an `Expression` (e.g. a `NullReferenceExpression`) -- the
// slot's declared child type is `Statement`.
TEST(CSharp_FixedStatement, EmbeddedStatementSlotAcceptsStatementNotAstTypeNotExpression) {
    auto body = std::make_unique<BreakStatement>();
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(FixedStatement::EmbeddedStatementSlot.IsInstanceOfType(body.get()));
    EXPECT_FALSE(FixedStatement::EmbeddedStatementSlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(FixedStatement::EmbeddedStatementSlot.IsInstanceOfType(expr.get()));
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitFixedStatement` (the visitor-pattern round-trip); the
// depth-first walk then visits the `Type` child (a `VisitSimpleType` -> its `Identifier`).
TEST(CSharp_FixedStatement, AcceptVisitorDispatchesToVisitFixedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    RecordingVisitor v;
    fs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"fixed", "simple:int", "id:int"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / a `Statement*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_FixedStatement, AcceptVisitorIsVirtualThroughBases) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    AstNode* asAst = &fs;
    Statement* asStmt = &fs;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asStmt->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"fixed", "simple:int", "id:int"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"fixed", "simple:int", "id:int"}));
}

// ---- Depth-first walk --------------------------------------------------

// The depth-first walk visits the `Type` (a `VisitSimpleType` -> its `Identifier`), then the
// `Variables` (a `VisitVariableInitializer` per element -> each one's `NameToken` `Identifier`),
// then the `EmbeddedStatement` (a `VisitBreakStatement`) in document order.
TEST(CSharp_FixedStatement, DepthFirstWalkVisitsTypeVariablesEmbeddedStatementInOrder) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fs.Variables().Add(x.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    RecordingVisitor v;
    fs.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "fixed", "simple:int", "id:int", "varinit:x", "id:x", "break"}));
}

// ---- DoMatch (the generated recursive + collection + recursive match) ---

// Two `FixedStatement`s with the same type, the same (single) variable, and the same body match.
TEST(CSharp_FixedStatement, DoMatchMatchesSameTypeVariablesEmbeddedStatement) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    auto abody = std::make_unique<BreakStatement>();
    auto bbody = std::make_unique<BreakStatement>();
    a.EmbeddedStatement(abody.get());
    b.EmbeddedStatement(bbody.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Type` rejects the match (the `MatchRequired` on `Type` -- the first term --
// rejects via the child's `DoMatch`).
TEST(CSharp_FixedStatement, DoMatchRejectsDifferentType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("byte"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // type names differ ("int" vs "byte")
}

// A null pattern `Type` rejects (the `MatchRequired` guard -- the first term).
TEST(CSharp_FixedStatement, DoMatchRejectsNullPatternType) {
    FixedStatement a;  // no type
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Type` rejects (the `MatchRequired` flows the null child through the child's
// `DoMatch(nullptr)`, which returns false).
TEST(CSharp_FixedStatement, DoMatchRejectsNullCandidateType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b;  // no type
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `FixedStatement`s with the same type but different variable COUNTS do not match (the
// collection match fails when the counts differ).
TEST(CSharp_FixedStatement, DoMatchRejectsDifferentVariableCount) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 variable, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `FixedStatement`s with the same type and count but different variable VALUES do not match
// (the element `DoMatch` -- a `MatchString` on the `VariableInitializer` name -- rejects).
TEST(CSharp_FixedStatement, DoMatchRejectsDifferentVariableValue) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("y"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different `EmbeddedStatement` rejects the match (the `MatchRequired` on `EmbeddedStatement` --
// the last term -- rejects via the child's `DoMatch`).
TEST(CSharp_FixedStatement, DoMatchRejectsDifferentEmbeddedStatement) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    auto abody = std::make_unique<BreakStatement>();
    auto bbody = std::make_unique<ContinueStatement>();
    a.EmbeddedStatement(abody.get());
    b.EmbeddedStatement(bbody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // break vs continue -- different concrete types
}

// A null pattern `EmbeddedStatement` rejects (the `MatchRequired` guard -- the last term).
TEST(CSharp_FixedStatement, DoMatchRejectsNullPatternEmbeddedStatement) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    auto bbody = std::make_unique<BreakStatement>();
    b.EmbeddedStatement(bbody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has no body
}

// A null candidate `EmbeddedStatement` rejects (the `MatchRequired` flows the null child through
// the child's `DoMatch(nullptr)`, which returns false).
TEST(CSharp_FixedStatement, DoMatchRejectsNullCandidateEmbeddedStatement) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement a(ta.get());
    FixedStatement b(tb.get());
    auto abody = std::make_unique<BreakStatement>();
    a.EmbeddedStatement(abody.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // b has no body
}

// A `FixedStatement` does not match a different concrete type (the `other is FixedStatement`
// gate); a `WhileStatement` is a fellow `Statement` but not a `FixedStatement`.
TEST(CSharp_FixedStatement, DoMatchRejectsWhileStatementCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    WhileStatement ws;
    EXPECT_FALSE(DoMatchAgainst(&fs, &ws));
    EXPECT_FALSE(DoMatchAgainst(&ws, &fs));
}

// A `FixedStatement` does not match a structural-twin `Expression` across the disjoint
// `Statement`/`Expression` hierarchies: an `ObjectCreateExpression` shares the single + collection
// + trailing-single shape but is an `Expression`, so the `other is FixedStatement` gate rejects.
// Each candidate gets its own fresh `Type` (a child can have only one parent -- the D235
// single-parent-guard gotcha).
TEST(CSharp_FixedStatement, DoMatchRejectsObjectCreateExpressionCandidate) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(ta.get());
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ObjectCreateExpression oce(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&fs, &oce));
    EXPECT_FALSE(DoMatchAgainst(&oce, &fs));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_FixedStatement, DoMatchRejectsNullCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    EXPECT_FALSE(DoMatchAgainst(&fs, nullptr));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` deep-copies the type, the variables, and the body, re-parents the clones, and detaches
// from the source.
TEST(CSharp_FixedStatement, CloneDeepCopiesTypeVariablesEmbeddedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"), TextLocation(1, 1));
    FixedStatement original(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"), (Expression*)nullptr);
    original.Variables().Add(x.get());
    auto body = std::make_unique<BreakStatement>();
    original.EmbeddedStatement(body.get());

    std::unique_ptr<FixedStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The type is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(dynamic_cast<SimpleType*>(copy->Type())->Identifier().value_or(""), "int");
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    // The variable is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Variables().Count(), 1);
    ASSERT_NE(copy->Variables().At(0), nullptr);
    EXPECT_NE(copy->Variables().At(0), x.get());
    EXPECT_EQ(copy->Variables().At(0)->Name(), "x");
    EXPECT_EQ(copy->Variables().At(0)->Parent(), copy.get());
    // The body is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->EmbeddedStatement(), nullptr);
    EXPECT_NE(copy->EmbeddedStatement(), body.get());
    EXPECT_EQ(copy->EmbeddedStatement()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Variables().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
    EXPECT_EQ(type->Parent(), &original);
    EXPECT_EQ(body->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through a `Statement*` (returns a `Statement*`).
TEST(CSharp_FixedStatement, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement original(type.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<FixedStatement*>(astCopy.get()), nullptr);
    Statement* asStmt = &original;
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    ASSERT_NE(stmtCopy, nullptr);
    EXPECT_NE(dynamic_cast<FixedStatement*>(stmtCopy.get()), nullptr);
}

// `Clone` of an empty `FixedStatement` (no type, no variables, no body) yields an empty clone.
TEST(CSharp_FixedStatement, CloneOfEmptyIsEmpty) {
    FixedStatement original;  // no type, no variables, no body
    std::unique_ptr<FixedStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->Variables().Count(), 0);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
}

// `Clone` of a node with a type and variables but NO body yields a clone with no body (the
// required slot is skipped when absent -- `Clone` tolerates a missing child even though the slot
// is required; the invariant is enforced by `CheckInvariant`, not by `Clone`).
TEST(CSharp_FixedStatement, CloneWithoutEmbeddedStatementHasNoEmbeddedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement original(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    original.Variables().Add(x.get());
    std::unique_ptr<FixedStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_EQ(copy->Variables().Count(), 1);
    EXPECT_EQ(copy->EmbeddedStatement(), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A `FixedStatement` with the type and body set (the variables may be empty -- a collection is
// never a required slot) passes the inherited `CheckInvariant`. Runs in debug builds (a no-op in
// NDEBUG).
TEST(CSharp_FixedStatement, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    fs.CheckInvariant();  // no variables -- still valid (a collection is never required)
}

// A `FixedStatement` with the type, a variable, AND the body set passes the inherited
// `CheckInvariant`.
TEST(CSharp_FixedStatement, CheckInvariantPassesOnFilledNodeWithVariables) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fs.Variables().Add(x.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    fs.CheckInvariant();
}

// A default-constructed (empty) node violates the required-`Type` invariant (the assert fires in
// debug -- the `LabelStatement` D259 / `ForeachStatement` D265 precedent).
TEST(CSharp_FixedStatement, CheckInvariantRejectsEmptyNode) {
    FixedStatement fs;
    EXPECT_DEBUG_DEATH(fs.CheckInvariant(), "");
}

// A node with the type set but NO body violates the required-`EmbeddedStatement` invariant (the
// assert fires in debug).
TEST(CSharp_FixedStatement, CheckInvariantRejectsNodeMissingEmbeddedStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    EXPECT_DEBUG_DEATH(fs.CheckInvariant(), "");
}

// ---- Slot identity -----------------------------------------------------

// The `TypeSlot`, `VariablesSlot`, and `EmbeddedStatementSlot` are distinct slot statics
// (compared by address); the node's `Slot()` reports the per-node slot for each child. The three
// slot statics have DIFFERENT `CSharpSlotInfoT<T>` element types
// (`AstType`/`VariableInitializer`/`Statement`), so they are unrelated pointer types and must be
// compared by address through the common `CSharpSlotInfo*` base (the D222 precedent).
TEST(CSharp_FixedStatement, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&FixedStatement::TypeSlot,
              (const CSharpSlotInfo*)&FixedStatement::VariablesSlot);
    EXPECT_NE((const CSharpSlotInfo*)&FixedStatement::TypeSlot,
              (const CSharpSlotInfo*)&FixedStatement::EmbeddedStatementSlot);
    EXPECT_NE((const CSharpSlotInfo*)&FixedStatement::VariablesSlot,
              (const CSharpSlotInfo*)&FixedStatement::EmbeddedStatementSlot);
    auto type = std::make_unique<SimpleType>(std::string("int"));
    FixedStatement fs(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fs.Variables().Add(x.get());
    auto body = std::make_unique<BreakStatement>();
    fs.EmbeddedStatement(body.get());
    EXPECT_EQ(fs.Type()->Slot(), &FixedStatement::TypeSlot);
    EXPECT_EQ(fs.Type()->Slot()->Kind(), &Slots::Type);
    EXPECT_EQ(fs.Variables().At(0)->Slot(), &FixedStatement::VariablesSlot);
    EXPECT_EQ(fs.Variables().At(0)->Slot()->Kind(), &Slots::Variable);
    EXPECT_EQ(fs.EmbeddedStatement()->Slot(), &FixedStatement::EmbeddedStatementSlot);
    EXPECT_EQ(fs.EmbeddedStatement()->Slot()->Kind(), &Slots::EmbeddedStatement);
}
