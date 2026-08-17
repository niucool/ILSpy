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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `Identifier` token node (cpp/Decompiler/CSharp/Syntax/Identifier.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/Identifier.cs) -- the first concrete C# AST
// node that derives directly from `AstNode` (not through `Expression`): a leaf token carrying
// a `Name` string and an `IsVerbatim` flag, the `Create` factories, the generated
// `DoMatch` (a `MatchString` on `Name`, `IsVerbatim` excluded), the `AcceptVisitor` dispatch,
// and the per-concrete-node `Clone`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides only the `Visit` methods under test (the role
// the generated per-node `Visit` overrides play), recording the node's `Name` (or a sentinel
// for the non-`Identifier` leaves) and recursing via `VisitChildren` (the inherited
// depth-first default). The leaves have no children, so the trace is the visited nodes.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr)
            return;
        trace.push_back("null");
        VisitChildren(node);
    }
};

} // namespace

// ---- Default construction ---------------------------------------------

// The default `Identifier()` has an empty `Name` and `IsVerbatim == false` (the C#
// `Identifier()` sets `name = string.Empty`).
TEST(CSharp_Identifier, DefaultCtorHasEmptyNameAndNotVerbatim) {
    Identifier id;
    EXPECT_EQ(id.Name(), "");
    EXPECT_FALSE(id.IsVerbatim());
    EXPECT_EQ(id.StartLocation(), TextLocation::Empty);
}

// ---- Name / IsVerbatim accessors ----------------------------------------

// `Name` gets/sets the underlying string.
TEST(CSharp_Identifier, NameGetterSetter) {
    Identifier id;
    id.Name("foo");
    EXPECT_EQ(id.Name(), "foo");
    id.Name("bar");
    EXPECT_EQ(id.Name(), "bar");
}

// `IsVerbatim` gets/sets the flag.
TEST(CSharp_Identifier, IsVerbatimGetterSetter) {
    Identifier id;
    EXPECT_FALSE(id.IsVerbatim());
    id.IsVerbatim(true);
    EXPECT_TRUE(id.IsVerbatim());
    id.IsVerbatim(false);
    EXPECT_FALSE(id.IsVerbatim());
}

// ---- EndLocation (the name span) ---------------------------------------

// `EndLocation` spans `Name.size()` columns from `StartLocation`.
TEST(CSharp_Identifier, EndLocationSpansNameLength) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation(3, 5)));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->StartLocation(), TextLocation(3, 5));
    EXPECT_EQ(id->EndLocation(), TextLocation(3, 8));  // 3 + 3
}

// `EndLocation` adds one column for the `@`-prefix of a verbatim identifier.
TEST(CSharp_Identifier, EndLocationIncludesVerbatimPrefix) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("@foo", TextLocation(1, 2)));
    ASSERT_NE(id, nullptr);
    EXPECT_TRUE(id->IsVerbatim());
    EXPECT_EQ(id->Name(), "foo");
    EXPECT_EQ(id->StartLocation(), TextLocation(1, 3));  // the @ advances the start one column
    EXPECT_EQ(id->EndLocation(), TextLocation(1, 7));    // 3 + 3 (name) + 1 (the @)
}

// An empty `Identifier` has `EndLocation == StartLocation` (zero-length name).
TEST(CSharp_Identifier, EndLocationOfEmptyNameEqualsStartLocation) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("", TextLocation(2, 4)));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "");
    EXPECT_EQ(id->StartLocation(), TextLocation(2, 4));
    EXPECT_EQ(id->EndLocation(), TextLocation(2, 4));
}

// ---- Create factories ---------------------------------------------------

// `Create("")` yields an empty `Identifier` at the location.
TEST(CSharp_Identifier, CreateEmptyReturnsEmptyIdentifier) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("", TextLocation(7, 1)));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "");
    EXPECT_EQ(id->StartLocation(), TextLocation(7, 1));
}

// `Create("@foo", loc)` strips the `@`, sets `IsVerbatim`, and advances the start one column.
TEST(CSharp_Identifier, CreateAtPrefixStripsAndSetsVerbatim) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("@foo", TextLocation(1, 10)));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "foo");
    EXPECT_TRUE(id->IsVerbatim());
    EXPECT_EQ(id->StartLocation(), TextLocation(1, 11));  // column + 1 for the @
}

// `Create("foo", loc, true)` keeps the name and sets the explicit verbatim flag (no column
// advance -- the `@` is not in the name).
TEST(CSharp_Identifier, CreateWithExplicitVerbatimFlag) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation(4, 6), true));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "foo");
    EXPECT_TRUE(id->IsVerbatim());
    EXPECT_EQ(id->StartLocation(), TextLocation(4, 6));  // no advance (no @ in the name)
    EXPECT_EQ(id->EndLocation(), TextLocation(4, 10));   // 6 + 3 (name) + 1 (verbatim)
}

// `Create("foo", loc, false)` is a plain identifier.
TEST(CSharp_Identifier, CreateWithExplicitNonVerbatim) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation(4, 6), false));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "foo");
    EXPECT_FALSE(id->IsVerbatim());
    EXPECT_EQ(id->StartLocation(), TextLocation(4, 6));
    EXPECT_EQ(id->EndLocation(), TextLocation(4, 9));  // 6 + 3
}

// `Create(name)` (no location) uses `TextLocation.Empty`.
TEST(CSharp_Identifier, CreateWithoutLocationUsesEmpty) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("bar"));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "bar");
    EXPECT_EQ(id->StartLocation(), TextLocation::Empty);
}

// `CreateIfNotEmpty` returns null for an empty name and an `Identifier` for a non-empty one.
TEST(CSharp_Identifier, CreateIfNotEmpty) {
    EXPECT_EQ(Identifier::CreateIfNotEmpty(""), nullptr);
    EXPECT_EQ(Identifier::CreateIfNotEmpty(std::string_view()), nullptr);
    auto id = std::unique_ptr<Identifier>(Identifier::CreateIfNotEmpty("foo"));
    ASSERT_NE(id, nullptr);
    EXPECT_EQ(id->Name(), "foo");
}

// ---- AcceptVisitor dispatch --------------------------------------------

// `AcceptVisitor` dispatches to `VisitIdentifier` (the visitor-pattern round-trip).
TEST(CSharp_Identifier, AcceptVisitorDispatchesToVisitIdentifier) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("x", TextLocation(1, 1)));
    RecordingVisitor v;
    id->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"id:x"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_Identifier, AcceptVisitorIsVirtualThroughAstNode) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("y", TextLocation(2, 3)));
    AstNode* asAst = id.get();
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"id:y"}));
}

// `VisitChildren` walks the (zero) children -- a leaf records just itself.
TEST(CSharp_Identifier, DepthFirstWalkRecordsJustTheLeaf) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("z", TextLocation(5, 5)));
    RecordingVisitor v;
    id->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"id:z"}));
}

// `Identifier` is an `AstNode` but NOT an `Expression` (it derives directly from `AstNode`).
TEST(CSharp_Identifier, IdentifierIsAstNodeButNotExpression) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("w"));
    EXPECT_NE(dynamic_cast<AstNode*>(id.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(id.get()), nullptr);
}

// ---- DoMatch (the generated type + string match) -----------------------

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Two `Identifier`s with the same `Name` match.
TEST(CSharp_Identifier, DoMatchMatchesSameName) {
    auto a = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    auto b = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Two `Identifier`s with different `Name`s do not match.
TEST(CSharp_Identifier, DoMatchRejectsDifferentName) {
    auto a = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    auto b = std::unique_ptr<Identifier>(Identifier::Create("bar"));
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_FALSE(DoMatchAgainst(b.get(), a.get()));
}

// An `Identifier` does not match a different concrete type (the `other is Identifier` gate).
TEST(CSharp_Identifier, DoMatchRejectsDifferentType) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(id.get(), &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, id.get()));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_Identifier, DoMatchRejectsNullCandidate) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    EXPECT_FALSE(DoMatchAgainst(id.get(), nullptr));
}

// A pattern `Identifier` whose `Name` is `$any$` (the `Pattern::AnyString` wildcard) matches
// any candidate `Identifier` regardless of its name (the `MatchString` wildcard path).
TEST(CSharp_Identifier, DoMatchAnyStringWildcardMatchesAnyName) {
    auto pattern = std::unique_ptr<Identifier>(Identifier::Create("anything"));
    pattern->Name(std::string(Pattern::AnyString));
    auto cand1 = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    auto cand2 = std::unique_ptr<Identifier>(Identifier::Create("bar"));
    EXPECT_TRUE(DoMatchAgainst(pattern.get(), cand1.get()));
    EXPECT_TRUE(DoMatchAgainst(pattern.get(), cand2.get()));
}

// `IsVerbatim` is `[ExcludeFromMatch]`: a verbatim and a non-verbatim `Identifier` with the
// same `Name` still match (the flag is not compared).
TEST(CSharp_Identifier, DoMatchExcludesIsVerbatim) {
    auto a = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation(1, 1), true));
    auto b = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation(2, 2), false));
    ASSERT_TRUE(a->IsVerbatim());
    ASSERT_FALSE(b->IsVerbatim());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
    EXPECT_TRUE(DoMatchAgainst(b.get(), a.get()));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` copies `Name`, `IsVerbatim`, and `StartLocation`, and detaches.
TEST(CSharp_Identifier, CloneCopiesNameVerbatimAndLocation) {
    auto original = std::unique_ptr<Identifier>(Identifier::Create("@foo", TextLocation(3, 4)));
    ASSERT_TRUE(original->IsVerbatim());
    std::unique_ptr<Identifier> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());  // a fresh node
    EXPECT_EQ(copy->Parent(), nullptr);     // detached
    EXPECT_EQ(copy->Name(), "foo");
    EXPECT_TRUE(copy->IsVerbatim());
    EXPECT_EQ(copy->StartLocation(), TextLocation(3, 5));  // the @ advanced the start
    EXPECT_EQ(copy->EndLocation(), TextLocation(3, 9));
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`,
// dispatched to the concrete override.
TEST(CSharp_Identifier, CloneIsVirtualThroughAstNode) {
    auto original = std::unique_ptr<Identifier>(Identifier::Create("bar", TextLocation(1, 1)));
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<Identifier*>(copy.get()), nullptr);
    EXPECT_EQ(copy->StartLocation(), TextLocation(1, 1));
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A leaf `Identifier` with no children passes the inherited `CheckInvariant` (the no-child
// case the base handles). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_Identifier, CheckInvariantPassesOnLeaf) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo", TextLocation(1, 1)));
    id->CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) -------------------

// A leaf `Identifier` reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_Identifier, LeafHasNoChildren) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create("foo"));
    EXPECT_EQ(id->GetChildCount(), 0);
    EXPECT_FALSE(id->HasChildren());
    EXPECT_EQ(id->FirstChild(), nullptr);
    EXPECT_EQ(id->LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : id->Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);
}
