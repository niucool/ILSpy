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

// Tests for the `IAstVisitor<bool>` / `AcceptVisitorBool` dispatch (IAstVisitorBool.hpp +
// DepthFirstAstVisitorBool.hpp + the abstract `AstNode::AcceptVisitorBool`) -- the
// `<bool>`-instantiation of the C# generic `IAstVisitor<out S>` / `DepthFirstAstVisitor<T>`
// visitor pair, the foundation that unblocks `GenericGrammarAmbiguityVisitor
// : DepthFirstAstVisitor<bool>` (the next in-order output-stage file per PORT_PLAN.md
// decision D369). Mirrors the void `IAstVisitor` / `DepthFirstAstVisitor` dispatch
// (IAstVisitor_Test.cpp) but each per-node `Visit` returns `bool` (the stop/continue
// signal the ambiguity resolver consumes) and the dispatch entry is `AcceptVisitorBool`.
// The concrete C# AST node hierarchy is ported, so the dispatch is exercised with real
// concrete nodes (an `IdentifierExpression`, a `BinaryOperatorExpression` over
// `NullReferenceExpression`/`ThisReferenceExpression` leaves) rather than stubs.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::Match;

namespace {

// A recording `DepthFirstAstVisitor<bool>`: overrides the four node kinds the
// `GenericGrammarAmbiguityVisitor` itself overrides (the binary, the identifier, and the
// two reference-expression leaves used here) plus records each visited node, recursing via
// the inherited `VisitChildren` (the depth-first default). A `stopOnBinary` flag mirrors
// the C# stop signal: when set, `VisitBinaryOperatorExpression` returns `true` (stop
// visiting) without recursing, and the `true` propagates back through `AcceptVisitorBool`.
class RecordingBoolVisitor : public DepthFirstAstVisitorBool {
public:
    std::vector<std::string> trace;
    bool stopOnBinary = false;

    bool VisitBinaryOperatorExpression(BinaryOperatorExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return true;
        }
        trace.push_back("bin");
        if (stopOnBinary)
            return true; // stop visiting, no ambiguity found
        return VisitChildren(node); // keep walking (returns false = default(bool))
    }

    bool VisitIdentifierExpression(IdentifierExpression* node) override {
        if (node == nullptr)
            return false;
        trace.push_back("ident:" + node->Identifier());
        return VisitChildren(node);
    }

    bool VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr)
            return false;
        trace.push_back("null");
        return VisitChildren(node);
    }

    bool VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr)
            return false;
        trace.push_back("this");
        return VisitChildren(node);
    }
};

} // namespace

// A concrete node's `AcceptVisitorBool` dispatches to the matching `Visit` method on the
// visitor (the `<bool>`-variant visitor-pattern round-trip), returning its `bool` result.
TEST(CSharp_DepthFirstAstVisitorBool, AcceptVisitorBoolDispatchesToOverride) {
    IdentifierExpression id("x");
    RecordingBoolVisitor v;
    EXPECT_EQ(id.AcceptVisitorBool(v), false); // default VisitChildren returns default(bool)
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "ident:x");
}

// `AcceptVisitorBool` is virtual: calling it through an `AstNode*` dispatches to the
// concrete override (the dynamic dispatch the `DepthFirstAstVisitor<bool>` walk relies on
// -- it holds `AstNode*` and calls `node->AcceptVisitorBool(*this)`).
TEST(CSharp_DepthFirstAstVisitorBool, AcceptVisitorBoolIsVirtual) {
    IdentifierExpression id("x");
    AstNode* node = &id;
    RecordingBoolVisitor v;
    EXPECT_EQ(node->AcceptVisitorBool(v), false);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "ident:x");
}

// `DepthFirstAstVisitorBool::VisitChildren` walks the node's children in document order,
// calling `AcceptVisitorBool` on each (which dispatches back to the matching `Visit`
// method), recursing depth-first and returning `false` (default(bool)): the recorded trace
// is the pre-order walk of the binary-over-two-leaves tree.
TEST(CSharp_DepthFirstAstVisitorBool, VisitChildrenWalksDepthFirstInDocumentOrder) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    BinaryOperatorExpression bin(left.get(), BinaryOperatorType::Add, right.get());

    RecordingBoolVisitor v;
    EXPECT_EQ(bin.AcceptVisitorBool(v), false);
    ASSERT_EQ(v.trace, (std::vector<std::string>{"bin", "null", "this"}));
}

// An override that returns `true` (the stop signal) short-circuits the walk (no children
// visited) and the `true` propagates back through `AcceptVisitorBool` -- the
// stop-visiting contract the `GenericGrammarAmbiguityVisitor` relies on (its
// `VisitBinaryOperatorExpression` returns `true` once the ambiguity is resolved or ruled
// out).
TEST(CSharp_DepthFirstAstVisitorBool, OverrideReturnTrueStopsAndPropagates) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    BinaryOperatorExpression bin(left.get(), BinaryOperatorType::Add, right.get());

    RecordingBoolVisitor v;
    v.stopOnBinary = true;
    EXPECT_EQ(bin.AcceptVisitorBool(v), true);
    // The override returned `true` before recursing, so the leaves were not visited.
    ASSERT_EQ(v.trace, (std::vector<std::string>{"bin"}));
}

// A node kind the visitor does NOT override falls through to the `DepthFirstAstVisitorBool`
// base's default `Visit<NodeName>`, which delegates to `VisitChildren` (the depth-first
// walk) and returns `false` (default(bool)). `ThisReferenceExpression` is overridden here,
// so use a deeper unoverridden kind: the binary's default walk still visits the
// (unoverridden) `ThisReferenceExpression`? -- instead exercise the default directly with a
// node whose `Visit` this visitor does not override. The `Expression`-derived leaves
// `NullReferenceExpression`/`ThisReferenceExpression` ARE overridden above; an
// `IdentifierExpression` is overridden too. So verify the default path via a
// `BinaryOperatorExpression` whose `VisitBinaryOperatorExpression` is overridden -- instead
// check the default on a node kind left to the base: a `ParenthesizedExpression` wrapping
// the binary (the visitor does not override `VisitParenthesizedExpression`).
TEST(CSharp_DepthFirstAstVisitorBool, UnhandledNodeFallsThroughToDefaultVisitChildren) {
    auto left = std::make_unique<NullReferenceExpression>();
    auto right = std::make_unique<ThisReferenceExpression>();
    auto bin = std::make_unique<BinaryOperatorExpression>(
        left.get(), BinaryOperatorType::Add, right.get());
    ParenthesizedExpression paren(bin.get());

    RecordingBoolVisitor v;
    // `VisitParenthesizedExpression` is NOT overridden: the base default runs
    // `VisitChildren(paren)` (walking the binary child, which IS overridden) and returns
    // `false` (default(bool)).
    EXPECT_EQ(paren.AcceptVisitorBool(v), false);
    // The default `VisitChildren` walked the one child (the binary), whose override then
    // walked its two leaves.
    ASSERT_EQ(v.trace, (std::vector<std::string>{"bin", "null", "this"}));
}
