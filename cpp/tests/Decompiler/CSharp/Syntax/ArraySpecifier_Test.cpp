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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `ArraySpecifier` concrete node (cpp/Decompiler/CSharp/Syntax/ArraySpecifier.hpp,
// the port of the `ArraySpecifier` declared in ICSharpCode.Decompiler/CSharp/Syntax/ComposedType.cs)
// -- the rank-specifier leaf of an array type (the `[...]`/`[,...]` of a `ComposedType`): a leaf
// `AstNode` carrying a single `Dimensions` count, the generated `DoMatch` (a plain-equality term
// on the `int` `Dimensions`), the `AcceptVisitor` dispatch, the per-concrete-node `Clone`, and the
// hand-written `CheckInvariant` override (the first ported node to add its own scalar invariant).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides only `VisitArraySpecifier` (the role the
// generated per-node `Visit` override plays), recording the node's `Dimensions` and recursing
// via `VisitChildren` (the inherited depth-first default). The leaf has no children, so the
// trace is the visited node.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitArraySpecifier(ArraySpecifier* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("arr:" + std::to_string(node->Dimensions()));
        VisitChildren(node);
    }
};

} // namespace

// ---- Construction ------------------------------------------------------

// The default `ArraySpecifier()` has `Dimensions == 1` (the C# `= 1` initializer).
TEST(CSharp_ArraySpecifier, DefaultCtorHasDimensionOne) {
    ArraySpecifier spec;
    EXPECT_EQ(spec.Dimensions(), 1);
    EXPECT_EQ(spec.StartLocation(), TextLocation::Empty);
}

// The `ArraySpecifier(int dimensions)` ctor sets `Dimensions`.
TEST(CSharp_ArraySpecifier, IntCtorSetsDimensions) {
    ArraySpecifier spec(3);
    EXPECT_EQ(spec.Dimensions(), 3);
}

// ---- Dimensions accessor ----------------------------------------------

// `Dimensions` gets/sets the rank.
TEST(CSharp_ArraySpecifier, DimensionsGetterSetter) {
    ArraySpecifier spec;
    EXPECT_EQ(spec.Dimensions(), 1);
    spec.Dimensions(2);
    EXPECT_EQ(spec.Dimensions(), 2);
    spec.Dimensions(5);
    EXPECT_EQ(spec.Dimensions(), 5);
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitArraySpecifier` (the visitor-pattern round-trip).
TEST(CSharp_ArraySpecifier, AcceptVisitorDispatchesToVisitArraySpecifier) {
    ArraySpecifier spec(2);
    RecordingVisitor v;
    spec.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"arr:2"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_ArraySpecifier, AcceptVisitorIsVirtualThroughAstNode) {
    auto spec = std::make_unique<ArraySpecifier>(4);
    AstNode* asAst = spec.get();
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"arr:4"}));
}

// `VisitChildren` walks the (zero) children -- a leaf records just itself.
TEST(CSharp_ArraySpecifier, DepthFirstWalkRecordsJustTheLeaf) {
    auto spec = std::make_unique<ArraySpecifier>(1);
    RecordingVisitor v;
    spec->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"arr:1"}));
}

// ---- is-a (AstNode, not Expression, not AstType) ----------------------

// `ArraySpecifier` is an `AstNode` but NOT an `Expression` and NOT an `AstType` (it derives
// directly from `AstNode` -- the rank specifier is a structural leaf, not a type reference).
TEST(CSharp_ArraySpecifier, IsAstNodeButNotExpressionOrAstType) {
    auto spec = std::make_unique<ArraySpecifier>(1);
    EXPECT_NE(dynamic_cast<AstNode*>(spec.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(spec.get()), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(spec.get()), nullptr);
}

// ---- DoMatch (the generated type + int-equality match) ----------------

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Two `ArraySpecifier`s with the same `Dimensions` match.
TEST(CSharp_ArraySpecifier, DoMatchMatchesSameDimensions) {
    ArraySpecifier a(2);
    ArraySpecifier b(2);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Two `ArraySpecifier`s with different `Dimensions` do not match (the plain-equality term).
TEST(CSharp_ArraySpecifier, DoMatchRejectsDifferentDimensions) {
    ArraySpecifier a(1);
    ArraySpecifier b(3);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// The default-constructed `ArraySpecifier` (`Dimensions == 1`) matches another rank-1
// specifier but not a rank-2 one (the zero value `1` is a real rank, NOT a wildcard -- an
// `int` has no `Any` member, unlike the enum-with-`Any` operator nodes).
TEST(CSharp_ArraySpecifier, DoMatchDefaultIsNotWildcard) {
    ArraySpecifier def;  // Dimensions == 1
    ArraySpecifier one(1);
    ArraySpecifier two(2);
    EXPECT_TRUE(DoMatchAgainst(&def, &one));
    EXPECT_FALSE(DoMatchAgainst(&def, &two));
}

// An `ArraySpecifier` does not match a different concrete type (the `other is ArraySpecifier`
// gate).
TEST(CSharp_ArraySpecifier, DoMatchRejectsDifferentType) {
    ArraySpecifier spec(1);
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&spec, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &spec));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_ArraySpecifier, DoMatchRejectsNullCandidate) {
    ArraySpecifier spec(1);
    EXPECT_FALSE(DoMatchAgainst(&spec, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` copies `Dimensions` and detaches (a fresh node with no parent). The print-time
// locations are not derived, so they are not copied (the `ConditionalExpression`/`SimpleType`
// precedent for nodes without derived locations).
TEST(CSharp_ArraySpecifier, CloneCopiesDimensionsAndDetaches) {
    auto original = std::make_unique<ArraySpecifier>(3);
    std::unique_ptr<ArraySpecifier> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());  // a fresh node
    EXPECT_EQ(copy->Parent(), nullptr);     // detached
    EXPECT_EQ(copy->Dimensions(), 3);
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`,
// dispatched to the concrete override.
TEST(CSharp_ArraySpecifier, CloneIsVirtualThroughAstNode) {
    auto original = std::make_unique<ArraySpecifier>(2);
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<ArraySpecifier*>(copy.get()), nullptr);
    EXPECT_EQ(static_cast<ArraySpecifier*>(copy.get())->Dimensions(), 2);
}

// ---- CheckInvariant (the hand-written scalar-invariant override) ------

// A valid `ArraySpecifier` (`Dimensions >= 1`) passes `CheckInvariant` (the inherited base
// check on the zero-child leaf plus the hand-written `Dimensions >= 1` assertion). Runs in
// debug builds (a no-op in `NDEBUG`).
TEST(CSharp_ArraySpecifier, CheckInvariantPassesOnValidDimension) {
    auto spec = std::make_unique<ArraySpecifier>(1);
    spec->CheckInvariant();
    spec->Dimensions(4);
    spec->CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) -----------------

// A leaf `ArraySpecifier` reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_ArraySpecifier, LeafHasNoChildren) {
    auto spec = std::make_unique<ArraySpecifier>(2);
    EXPECT_EQ(spec->GetChildCount(), 0);
    EXPECT_FALSE(spec->HasChildren());
    EXPECT_EQ(spec->FirstChild(), nullptr);
    EXPECT_EQ(spec->LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : spec->Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);
}
