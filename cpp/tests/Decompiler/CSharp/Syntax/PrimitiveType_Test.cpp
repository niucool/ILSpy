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

// Tests for the `AstType` abstract base (cpp/.../Syntax/AstType.hpp) and the `PrimitiveType`
// concrete node (cpp/.../Syntax/PrimitiveType.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/PrimitiveType.cs) -- the first slice of the `AstType`
// hierarchy. `AstType` is the common base of every C# type-reference node (an abstract base
// with a covariant pure-virtual `Clone`); `PrimitiveType` is the simplest concrete `AstType`
// (a leaf with no `[Slot]` children carrying a single `Keyword` string). Exercises the
// abstract base, the `Keyword` accessors, the derived `EndLocation`, the `AcceptVisitor`
// dispatch, the generated `DoMatch` (a `MatchString` on `Keyword`), and the per-concrete-node
// `Clone`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides `VisitPrimitiveType` (the role the generated
// per-node `Visit` override plays), recording the node's `Keyword` (or a sentinel for a null
// node) and recursing via `VisitChildren` (the inherited depth-first default). The leaf has
// no children, so the trace is the visited nodes.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("prim:" + node->Keyword());
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

// ---- AstType abstract base ----------------------------------------------

// `AstType` is abstract: it cannot be instantiated directly (the covariant `Clone` is
// pure-virtual), matching the C# `public abstract class AstType`. A concrete type node is NOT
// abstract (it overrides `Clone`). The `is_abstract` trait pins this at compile time.
TEST(CSharp_PrimitiveType, AstTypeIsAbstractViaPureVirtualClone) {
    static_assert(std::is_abstract_v<AstType>);
    static_assert(!std::is_abstract_v<PrimitiveType>);
    // The only way to get an `AstType` is via a concrete subclass; a concrete node constructs
    // and upcasts.
    auto pt = std::make_unique<PrimitiveType>(std::string("int"));
    AstType* type = pt.get();
    EXPECT_NE(type, nullptr);
}

// A concrete type node IS an `AstType` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use).
TEST(CSharp_PrimitiveType, PrimitiveTypeIsAstTypeAndAstNode) {
    auto pt = std::make_unique<PrimitiveType>(std::string("int"));
    EXPECT_NE(dynamic_cast<AstType*>(pt.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(pt.get()), nullptr);
}

// `PrimitiveType` is NOT an `Expression` (it derives from `AstType`, which derives from
// `AstNode`, parallel to -- not under -- `Expression`).
TEST(CSharp_PrimitiveType, PrimitiveTypeIsNotExpression) {
    auto pt = std::make_unique<PrimitiveType>(std::string("int"));
    EXPECT_EQ(dynamic_cast<Expression*>(pt.get()), nullptr);
}

// ---- Construction ------------------------------------------------------

// The default `PrimitiveType()` has an empty `Keyword` (the C# `string keyword = string.Empty`
// field initializer; a `std::string` is empty by default).
TEST(CSharp_PrimitiveType, DefaultCtorHasEmptyKeyword) {
    PrimitiveType pt;
    EXPECT_EQ(pt.Keyword(), "");
    EXPECT_EQ(pt.StartLocation(), TextLocation::Empty);
    EXPECT_EQ(pt.EndLocation(), TextLocation::Empty);  // Empty + 0
}

// The single-arg ctor stores the keyword.
TEST(CSharp_PrimitiveType, CtorWithKeywordStoresIt) {
    PrimitiveType pt(std::string("int"));
    EXPECT_EQ(pt.Keyword(), "int");
}

// The (keyword, location) ctor stores the keyword and records the start location.
TEST(CSharp_PrimitiveType, CtorWithKeywordAndLocationStoresBoth) {
    PrimitiveType pt(std::string("string"), TextLocation(3, 5));
    EXPECT_EQ(pt.Keyword(), "string");
    EXPECT_EQ(pt.StartLocation(), TextLocation(3, 5));
}

// ---- Keyword accessor --------------------------------------------------

// `Keyword` gets/sets the underlying string.
TEST(CSharp_PrimitiveType, KeywordGetterSetter) {
    PrimitiveType pt;
    pt.Keyword("bool");
    EXPECT_EQ(pt.Keyword(), "bool");
    pt.Keyword("object");
    EXPECT_EQ(pt.Keyword(), "object");
}

// ---- EndLocation (the keyword span) -----------------------------------

// `EndLocation` spans `Keyword.size()` columns from `StartLocation` (the keyword's lexical
// width).
TEST(CSharp_PrimitiveType, EndLocationSpansKeywordLength) {
    PrimitiveType pt(std::string("double"), TextLocation(2, 4));
    EXPECT_EQ(pt.StartLocation(), TextLocation(2, 4));
    EXPECT_EQ(pt.EndLocation(), TextLocation(2, 10));  // 4 + 6
}

// An empty `PrimitiveType` has `EndLocation == StartLocation` (zero-length keyword).
TEST(CSharp_PrimitiveType, EndLocationOfEmptyKeywordEqualsStartLocation) {
    PrimitiveType pt(std::string(""), TextLocation(7, 1));
    EXPECT_EQ(pt.Keyword(), "");
    EXPECT_EQ(pt.StartLocation(), TextLocation(7, 1));
    EXPECT_EQ(pt.EndLocation(), TextLocation(7, 1));
}

// `EndLocation` tracks `Keyword` after a setter change (it is always derived, not stored).
TEST(CSharp_PrimitiveType, EndLocationTracksKeywordAfterSet) {
    PrimitiveType pt(std::string("int"), TextLocation(1, 1));
    EXPECT_EQ(pt.EndLocation(), TextLocation(1, 4));  // 1 + 3
    pt.Keyword("long");
    EXPECT_EQ(pt.EndLocation(), TextLocation(1, 5));  // 1 + 4
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitPrimitiveType` (the visitor-pattern round-trip).
TEST(CSharp_PrimitiveType, AcceptVisitorDispatchesToVisitPrimitiveType) {
    PrimitiveType pt(std::string("int"), TextLocation(1, 1));
    RecordingVisitor v;
    pt.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim:int"}));
}

// `AcceptVisitor` is virtual: calling it through an `AstNode*` dispatches to the concrete
// override (the dynamic dispatch the output visitor relies on).
TEST(CSharp_PrimitiveType, AcceptVisitorIsVirtualThroughAstNode) {
    PrimitiveType pt(std::string("int"), TextLocation(2, 3));
    AstNode* asAst = &pt;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim:int"}));
}

// `AcceptVisitor` is virtual through an `AstType*` too (the abstract base the concrete type
// node derives from).
TEST(CSharp_PrimitiveType, AcceptVisitorIsVirtualThroughAstType) {
    PrimitiveType pt(std::string("int"), TextLocation(4, 5));
    AstType* asType = &pt;
    RecordingVisitor v;
    asType->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim:int"}));
}

// `VisitChildren` walks the (zero) children -- a leaf records just itself.
TEST(CSharp_PrimitiveType, DepthFirstWalkRecordsJustTheLeaf) {
    PrimitiveType pt(std::string("int"), TextLocation(5, 5));
    RecordingVisitor v;
    pt.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"prim:int"}));
}

// ---- DoMatch (the generated type + string match) -----------------------

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Two `PrimitiveType`s with the same `Keyword` match.
TEST(CSharp_PrimitiveType, DoMatchMatchesSameKeyword) {
    PrimitiveType a(std::string("int"));
    PrimitiveType b(std::string("int"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `PrimitiveType`s with different `Keyword`s do not match.
TEST(CSharp_PrimitiveType, DoMatchRejectsDifferentKeyword) {
    PrimitiveType a(std::string("int"));
    PrimitiveType b(std::string("string"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `PrimitiveType` does not match a different concrete type (the `other is PrimitiveType`
// gate); a `NullReferenceExpression` (an `Expression`, not an `AstType`) is not a
// `PrimitiveType`.
TEST(CSharp_PrimitiveType, DoMatchRejectsDifferentType) {
    PrimitiveType pt(std::string("int"));
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&pt, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &pt));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_PrimitiveType, DoMatchRejectsNullCandidate) {
    PrimitiveType pt(std::string("int"));
    EXPECT_FALSE(DoMatchAgainst(&pt, nullptr));
}

// A pattern `PrimitiveType` whose `Keyword` is `$any$` (the `Pattern::AnyString` wildcard)
// matches any candidate `PrimitiveType` regardless of its keyword (the `MatchString` wildcard
// path).
TEST(CSharp_PrimitiveType, DoMatchAnyStringWildcardMatchesAnyKeyword) {
    PrimitiveType pattern(std::string("anything"));
    pattern.Keyword(std::string(Pattern::AnyString));
    PrimitiveType cand1(std::string("int"));
    PrimitiveType cand2(std::string("string"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand1));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand2));
}

// A non-wildcard pattern whose `Keyword` is the empty string matches only a candidate whose
// `Keyword` is also empty (the empty string is a real keyword, not a wildcard).
TEST(CSharp_PrimitiveType, DoMatchEmptyKeywordIsNotWildcard) {
    PrimitiveType pattern;          // empty keyword (default ctor)
    PrimitiveType candEmpty;        // empty keyword (default ctor)
    PrimitiveType candInt(std::string("int"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candEmpty));
    EXPECT_FALSE(DoMatchAgainst(&pattern, &candInt));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` copies `Keyword` and `StartLocation`, and detaches.
TEST(CSharp_PrimitiveType, CloneCopiesKeywordAndLocation) {
    PrimitiveType original(std::string("int"), TextLocation(3, 4));
    std::unique_ptr<PrimitiveType> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);  // a fresh node
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_EQ(copy->Keyword(), "int");
    EXPECT_EQ(copy->StartLocation(), TextLocation(3, 4));
    EXPECT_EQ(copy->EndLocation(), TextLocation(3, 7));  // 4 + 3
}

// `Clone` is virtual through `AstNode*`: a call through an `AstNode*` returns an `AstNode*`,
// dispatched to the concrete override.
TEST(CSharp_PrimitiveType, CloneIsVirtualThroughAstNode) {
    PrimitiveType original(std::string("int"), TextLocation(1, 1));
    AstNode* node = &original;
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PrimitiveType*>(copy.get()), nullptr);
    EXPECT_EQ(copy->StartLocation(), TextLocation(1, 1));
}

// `Clone` is covariant through `AstType*`: a call through an `AstType*` returns an `AstType*`
// (the typed return the C# `new AstType Clone()` gives), and the result is the concrete type.
TEST(CSharp_PrimitiveType, CloneIsCovariantThroughAstType) {
    PrimitiveType original(std::string("int"), TextLocation(2, 3));
    AstType* type = &original;
    std::unique_ptr<AstType> copy(type->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PrimitiveType*>(copy.get()), nullptr);
    EXPECT_EQ(copy->StartLocation(), TextLocation(2, 3));
}

// `Clone` does not detach the source (the original keeps its parent and children); a leaf
// with no children and no parent still has `Parent() == nullptr` after a clone.
TEST(CSharp_PrimitiveType, CloneDoesNotDetachSource) {
    PrimitiveType original(std::string("int"), TextLocation(1, 1));
    std::unique_ptr<PrimitiveType> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(original.Parent(), nullptr);
    EXPECT_EQ(original.Keyword(), "int");
}

// ---- CheckInvariant (inherited from AstNode) --------------------------

// A leaf `PrimitiveType` with no children passes the inherited `CheckInvariant` (the no-child
// case the base handles). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_PrimitiveType, CheckInvariantPassesOnLeaf) {
    PrimitiveType pt(std::string("int"), TextLocation(1, 1));
    pt.CheckInvariant();
    PrimitiveType empty;
    empty.CheckInvariant();
}

// ---- Slot-storage contract (the zero-child defaults) -------------------

// A leaf `PrimitiveType` reports zero children (the `AstNode` zero-child defaults).
TEST(CSharp_PrimitiveType, LeafHasNoChildren) {
    PrimitiveType pt(std::string("int"));
    EXPECT_EQ(pt.GetChildCount(), 0);
    EXPECT_FALSE(pt.HasChildren());
    EXPECT_EQ(pt.FirstChild(), nullptr);
    EXPECT_EQ(pt.LastChild(), nullptr);
    int count = 0;
    for (AstNode* child : pt.Children())
        (void)child, ++count;
    EXPECT_EQ(count, 0);
}

// `GetChild` on a leaf throws (no slot to read).
TEST(CSharp_PrimitiveType, GetChildThrowsOutOfRange) {
    PrimitiveType pt(std::string("int"));
    EXPECT_THROW(pt.GetChild(0), std::out_of_range);
}

// `SetChild` on a leaf throws (no slot to write).
TEST(CSharp_PrimitiveType, SetChildThrowsOutOfRange) {
    PrimitiveType pt(std::string("int"));
    EXPECT_THROW(pt.SetChild(0, nullptr), std::out_of_range);
}

// `GetChildSlotInfo` on a leaf throws (no slots).
TEST(CSharp_PrimitiveType, GetChildSlotInfoThrowsOutOfRange) {
    PrimitiveType pt(std::string("int"));
    EXPECT_THROW(pt.GetChildSlotInfo(0), std::out_of_range);
}

// `Slot()` on an unparented leaf is null (it occupies no slot in any parent).
TEST(CSharp_PrimitiveType, SlotIsNullWhenUnparented) {
    PrimitiveType pt(std::string("int"));
    EXPECT_EQ(pt.Slot(), nullptr);
}
