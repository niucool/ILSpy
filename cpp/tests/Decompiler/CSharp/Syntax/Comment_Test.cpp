// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including unless required by applicable law or agreed to in writing,
// software distributed under the License, distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `Comment` concrete node (cpp/Decompiler/CSharp/Syntax/Comment.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/Comment.cs) -- the first concrete
// `Trivia`-derived node: a sealed `Trivia` leaf carrying a `CommentType` enum scalar (no `Any`)
// and a `Content` string, the `AcceptVisitor` dispatch, the generated `DoMatch` (a type check
// plus a plain-enum equality plus a `MatchString` on `Content`), and the per-concrete-node
// `Clone`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides only `VisitComment` (the role the generated
// per-node `Visit` override plays), recording the node's `Content`/`CommentType` and recursing
// via `VisitChildren` (the inherited depth-first default). The leaf has no children, so the
// trace is the visited node.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitComment(Comment* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("comment:" + node->Content());
        VisitChildren(node);
    }
};

} // namespace

// ---- Construction ------------------------------------------------------

// The default `Comment()` has `CommentType == SingleLine` (the enum's zero value) and
// `Content == ""` (the `std::string` default), with empty `Trivia` locations.
TEST(CSharp_Comment, DefaultCtorHasDefaults) {
    Comment c;
    EXPECT_EQ(c.CommentType(), CommentType::SingleLine);
    EXPECT_EQ(c.Content(), "");
    EXPECT_EQ(c.StartLocation(), TextLocation::Empty);
    EXPECT_EQ(c.EndLocation(), TextLocation::Empty);
}

// The generated `(CommentType)` ctor sets the `CommentType` scalar.
TEST(CSharp_Comment, CommentTypeCtorSetsCommentType) {
    Comment c(CommentType::MultiLine);
    EXPECT_EQ(c.CommentType(), CommentType::MultiLine);
    EXPECT_EQ(c.Content(), "");
}

// The hand-written `(string, CommentType = SingleLine)` ctor sets `Content` and defaults the
// `CommentType` to `SingleLine`.
TEST(CSharp_Comment, StringCtorDefaultsCommentTypeToSingleLine) {
    Comment c(std::string("hello"));
    EXPECT_EQ(c.Content(), "hello");
    EXPECT_EQ(c.CommentType(), CommentType::SingleLine);
}

// The hand-written `(string, CommentType)` ctor sets both `Content` and `CommentType`.
TEST(CSharp_Comment, StringCommentTypeCtorSetsBoth) {
    Comment c(std::string("doc"), CommentType::Documentation);
    EXPECT_EQ(c.Content(), "doc");
    EXPECT_EQ(c.CommentType(), CommentType::Documentation);
}

// The hand-written `(CommentType, TextLocation, TextLocation)` ctor sets `CommentType` and
// records the `Trivia` source span.
TEST(CSharp_Comment, LocationCtorSetsCommentTypeAndSpan) {
    Comment c(CommentType::InactiveCode, TextLocation(2, 5), TextLocation(2, 20));
    EXPECT_EQ(c.CommentType(), CommentType::InactiveCode);
    EXPECT_EQ(c.StartLocation(), TextLocation(2, 5));
    EXPECT_EQ(c.EndLocation(), TextLocation(2, 20));
}

// ---- CommentType accessor ---------------------------------------------

// `CommentType` gets/sets the comment style (all five values round-trip).
TEST(CSharp_Comment, CommentTypeGetterSetter) {
    Comment c;
    EXPECT_EQ(c.CommentType(), CommentType::SingleLine);
    c.CommentType(CommentType::MultiLine);
    EXPECT_EQ(c.CommentType(), CommentType::MultiLine);
    c.CommentType(CommentType::Documentation);
    EXPECT_EQ(c.CommentType(), CommentType::Documentation);
    c.CommentType(CommentType::InactiveCode);
    EXPECT_EQ(c.CommentType(), CommentType::InactiveCode);
    c.CommentType(CommentType::MultiLineDocumentation);
    EXPECT_EQ(c.CommentType(), CommentType::MultiLineDocumentation);
}

// ---- Content accessor --------------------------------------------------

// `Content` gets/sets the comment text (defaults to the empty string).
TEST(CSharp_Comment, ContentGetterSetter) {
    Comment c;
    EXPECT_EQ(c.Content(), "");
    c.Content(std::string("a comment"));
    EXPECT_EQ(c.Content(), "a comment");
    c.Content(std::string(""));
    EXPECT_EQ(c.Content(), "");
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitComment` (the visitor-pattern round-trip).
TEST(CSharp_Comment, AcceptVisitorDispatchesToVisitComment) {
    Comment c(std::string("hi"));
    RecordingVisitor v;
    c.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"comment:hi"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_Comment, AcceptVisitorIsVirtualThroughAstNode) {
    auto c = std::make_unique<Comment>(std::string("x"));
    AstNode* asAst = c.get();
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"comment:x"}));
}

// `AcceptVisitor` is virtual through a `Trivia*` too (the `Trivia` base redeclares no
// `AcceptVisitor`, so the `AstNode` virtual dispatches to the `Comment` override).
TEST(CSharp_Comment, AcceptVisitorIsVirtualThroughTrivia) {
    auto c = std::make_unique<Comment>(std::string("t"));
    Trivia* asTrivia = c.get();
    RecordingVisitor v;
    asTrivia->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"comment:t"}));
}

// `VisitChildren` walks the (zero) children -- a leaf records just itself.
TEST(CSharp_Comment, DepthFirstWalkRecordsJustTheLeaf) {
    auto c = std::make_unique<Comment>(std::string("leaf"));
    RecordingVisitor v;
    c->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"comment:leaf"}));
}

// ---- is-a (Trivia + AstNode, not Expression/Statement/AstType) -------

// `Comment` is a `Trivia` and an `AstNode` but NOT an `Expression`, `Statement`, or `AstType`
// (it derives from `Trivia` -> `AstNode`, a disjoint hierarchy from the Expression/Statement/
// AstType bases). It is also `final` (the C# `sealed`).
TEST(CSharp_Comment, IsTriviaAndAstNodeButNotExpressionStatementOrAstType) {
    auto c = std::make_unique<Comment>();
    EXPECT_NE(dynamic_cast<Trivia*>(c.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(c.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(c.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(c.get()), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(c.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<Comment>);
}

// ---- DoMatch (the generated type + enum-equality + MatchString match) -

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Two `Comment`s with the same `CommentType` and `Content` match.
TEST(CSharp_Comment, DoMatchMatchesSameTypeAndContent) {
    Comment a(std::string("hi"), CommentType::SingleLine);
    Comment b(std::string("hi"), CommentType::SingleLine);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Two `Comment`s with the same `Content` but different `CommentType` do not match (the plain
// enum-equality term rejects -- `CommentType` has no `Any` member, so the zero value
// `SingleLine` is a real type, NOT a wildcard).
TEST(CSharp_Comment, DoMatchRejectsDifferentCommentType) {
    Comment a(std::string("hi"), CommentType::SingleLine);
    Comment b(std::string("hi"), CommentType::MultiLine);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `Comment`s with the same `CommentType` but different `Content` do not match (the
// `MatchString` term rejects).
TEST(CSharp_Comment, DoMatchRejectsDifferentContent) {
    Comment a(std::string("hi"), CommentType::SingleLine);
    Comment b(std::string("bye"), CommentType::SingleLine);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// The `$any$` wildcard (`Pattern::AnyString`) in the pattern's `Content` matches any candidate
// content (the `MatchString` wildcard path) -- but the `CommentType` must still match (it is a
// plain enum, not wildcarded).
TEST(CSharp_Comment, DoMatchAnyStringWildcardMatchesAnyContent) {
    Comment pattern(std::string(PatternMatching::Pattern::AnyString), CommentType::SingleLine);
    Comment candA(std::string("anything"), CommentType::SingleLine);
    Comment candB(std::string(""), CommentType::SingleLine);
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candA));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candB));
    // The wildcard is on Content only; a CommentType mismatch still rejects.
    Comment candC(std::string("anything"), CommentType::MultiLine);
    EXPECT_FALSE(DoMatchAgainst(&pattern, &candC));
}

// The default-constructed `Comment` (`SingleLine` + empty `Content`) matches another
// `SingleLine` empty-content `Comment` (both terms are plain equality / `MatchString`).
TEST(CSharp_Comment, DoMatchDefaultMatchesSameDefaults) {
    Comment def;
    Comment other(std::string(""), CommentType::SingleLine);
    EXPECT_TRUE(DoMatchAgainst(&def, &other));
    EXPECT_TRUE(DoMatchAgainst(&other, &def));
}

// A `Comment` does not match a different concrete type (the `other is Comment` gate).
TEST(CSharp_Comment, DoMatchRejectsDifferentType) {
    Comment c(std::string("hi"));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&c, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &c));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_Comment, DoMatchRejectsNullCandidate) {
    Comment c(std::string("hi"));
    EXPECT_FALSE(DoMatchAgainst(&c, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` copies `CommentType` + `Content` + the `Trivia` source span, and detaches (a fresh
// node with no parent).
TEST(CSharp_Comment, CloneCopiesScalarsAndLocationsAndDetaches) {
    auto original = std::make_unique<Comment>(CommentType::MultiLine, TextLocation(1, 2), TextLocation(1, 10));
    original->Content(std::string("a /* */ comment"));
    std::unique_ptr<Comment> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());   // a fresh node
    EXPECT_EQ(copy->Parent(), nullptr);      // detached
    EXPECT_EQ(copy->CommentType(), CommentType::MultiLine);
    EXPECT_EQ(copy->Content(), "a /* */ comment");
    EXPECT_EQ(copy->StartLocation(), TextLocation(1, 2));
    EXPECT_EQ(copy->EndLocation(), TextLocation(1, 10));
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`,
// dispatched to the concrete override (which downcasts to `Comment`).
TEST(CSharp_Comment, CloneIsVirtualThroughAstNode) {
    auto original = std::make_unique<Comment>(std::string("x"), CommentType::Documentation);
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<Comment*>(copy.get()), nullptr);
    EXPECT_EQ(static_cast<Comment*>(copy.get())->Content(), "x");
    EXPECT_EQ(static_cast<Comment*>(copy.get())->CommentType(), CommentType::Documentation);
}

// `Clone` produces a node distinct from the original (a deep copy, not a shared reference);
// mutating the copy does not affect the original.
TEST(CSharp_Comment, CloneIsDeepCopy) {
    auto original = std::make_unique<Comment>(std::string("orig"));
    std::unique_ptr<Comment> copy(original->Clone());
    copy->Content(std::string("copy"));
    EXPECT_EQ(original->Content(), "orig");
    EXPECT_EQ(copy->Content(), "copy");
}

// ---- CheckInvariant (the inherited zero-child leaf check) -------------

// A `Comment` (a leaf with no `[Slot]` children) passes `CheckInvariant` -- there are no
// required slots, so even a default-constructed node is invariant-valid. Runs in debug builds
// (a no-op in `NDEBUG`).
TEST(CSharp_Comment, CheckInvariantPassesOnDefault) {
    auto c = std::make_unique<Comment>();
    c->CheckInvariant();
    c->CommentType(CommentType::MultiLineDocumentation);
    c->Content(std::string("doc"));
    c->CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) -----------------

// A leaf `Comment` reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_Comment, LeafHasNoChildren) {
    auto c = std::make_unique<Comment>(std::string("hi"));
    EXPECT_EQ(c->GetChildCount(), 0);
    EXPECT_FALSE(c->HasChildren());
    EXPECT_EQ(c->FirstChild(), nullptr);
    EXPECT_EQ(c->LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : c->Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);
}
