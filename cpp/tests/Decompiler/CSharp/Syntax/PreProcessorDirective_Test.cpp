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

// Tests for the `PreProcessorDirective` family (cpp/Decompiler/CSharp/Syntax/
// PreProcessorDirective.hpp + LinePreprocessorDirective.hpp + PragmaWarningPreprocessorDirective.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/PreProcessorDirective.cs) -- the
// concrete `Trivia` family for preprocessor directives: the NOT-sealed `PreProcessorDirective` base
// (a `PreProcessorDirectiveType` enum + a nullable `Argument` string, HAND-WRITTEN `DoMatch`),
// the sealed `LinePreprocessorDirective` (`#line`, no slots), and the sealed
// `PragmaWarningPreprocessorDirective` (`#pragma warning`, a `Warnings` collection + an
// `EndLocation` override). Three suites (one per node) in one shared test file (the D234
// multi-suite pattern), covering the ctors, scalars, the inherited `AcceptVisitor` dispatch, the
// depth-first walk, the hand-written/inherited `DoMatch` (incl. the subtype-accepting base match
// and the `Warnings`-not-matched behavior), the per-concrete `Clone` (the not-sealed-hierarchy
// no-slicing crux), and `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/LinePreprocessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PragmaWarningPreprocessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides `VisitPreProcessorDirective` (the one `Visit`
// method the whole family dispatches through -- the two sealed derived classes inherit the
// base `AcceptVisitor` calling it) and `VisitPrimitiveExpression` (so the depth-first walk over
// a `PragmaWarningPreprocessorDirective`'s `Warnings` collection is observable). Records the
// visited node and recurses via `VisitChildren` (the inherited depth-first default).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitPreProcessorDirective(PreProcessorDirective* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("ppd");
        VisitChildren(node);
    }

    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) {
            trace.push_back("<null>");
            return;
        }
        trace.push_back("prim");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`). The
// inherited hand-written `DoMatch` is subtype-accepting (`other as PreProcessorDirective`), so a
// base pattern matches a derived candidate.
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ============================================================================
// PreProcessorDirective (the base)
// ============================================================================

// ---- Construction ------------------------------------------------------

// The default `PreProcessorDirective()` has `Type == Invalid` (the enum's zero value) and
// `Argument == nullopt` (the C# `null`), with empty `Trivia` locations.
TEST(CSharp_PreProcessorDirective, DefaultCtorHasDefaults) {
    PreProcessorDirective ppd;
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Invalid);
    EXPECT_FALSE(ppd.Argument().has_value());
    EXPECT_EQ(ppd.StartLocation(), TextLocation::Empty);
    EXPECT_EQ(ppd.EndLocation(), TextLocation::Empty);
}

// The `(type, span, span)` ctor sets `Type` and records the `Trivia` source span.
TEST(CSharp_PreProcessorDirective, SpanCtorSetsTypeAndSpan) {
    PreProcessorDirective ppd(PreProcessorDirectiveType::If, TextLocation(1, 1), TextLocation(1, 10));
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::If);
    EXPECT_FALSE(ppd.Argument().has_value());
    EXPECT_EQ(ppd.StartLocation(), TextLocation(1, 1));
    EXPECT_EQ(ppd.EndLocation(), TextLocation(1, 10));
}

// The `(type, argument)` ctor sets `Type` and `Argument` (default `nullopt`).
TEST(CSharp_PreProcessorDirective, ArgumentCtorSetsTypeAndArgument) {
    PreProcessorDirective ppd(PreProcessorDirectiveType::Define, std::optional<std::string>("DEBUG"));
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Define);
    ASSERT_TRUE(ppd.Argument().has_value());
    EXPECT_EQ(*ppd.Argument(), "DEBUG");
    // Default argument is nullopt.
    PreProcessorDirective ppd2(PreProcessorDirectiveType::Endif);
    EXPECT_EQ(ppd2.Type(), PreProcessorDirectiveType::Endif);
    EXPECT_FALSE(ppd2.Argument().has_value());
}

// ---- Type / Argument accessors ----------------------------------------

// `Type` gets/sets the directive kind (round-trips a few values).
TEST(CSharp_PreProcessorDirective, TypeGetterSetter) {
    PreProcessorDirective ppd;
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Invalid);
    ppd.Type(PreProcessorDirectiveType::Region);
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Region);
    ppd.Type(PreProcessorDirectiveType::Pragma);
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Pragma);
}

// `Argument` gets/sets the optional rest-of-line text (nullopt round-trips).
TEST(CSharp_PreProcessorDirective, ArgumentGetterSetter) {
    PreProcessorDirective ppd;
    EXPECT_FALSE(ppd.Argument().has_value());
    ppd.Argument(std::optional<std::string>("#DEBUG"));
    ASSERT_TRUE(ppd.Argument().has_value());
    EXPECT_EQ(*ppd.Argument(), "#DEBUG");
    ppd.Argument(std::nullopt);
    EXPECT_FALSE(ppd.Argument().has_value());
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitPreProcessorDirective` (the visitor-pattern round-trip).
TEST(CSharp_PreProcessorDirective, AcceptVisitorDispatchesToVisitPreProcessorDirective) {
    PreProcessorDirective ppd(PreProcessorDirectiveType::If);
    RecordingVisitor v;
    ppd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"ppd"}));
}

// `AcceptVisitor` is virtual through `AstNode*` and `Trivia*`.
TEST(CSharp_PreProcessorDirective, AcceptVisitorIsVirtualThroughBases) {
    auto ppd = std::make_unique<PreProcessorDirective>(PreProcessorDirectiveType::If);
    AstNode* asAst = ppd.get();
    Trivia* asTrivia = ppd.get();
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asTrivia->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"ppd"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"ppd"}));
}

// `VisitChildren` walks the (zero) children -- a leaf records just itself.
TEST(CSharp_PreProcessorDirective, DepthFirstWalkRecordsJustTheLeaf) {
    auto ppd = std::make_unique<PreProcessorDirective>(PreProcessorDirectiveType::Else);
    RecordingVisitor v;
    ppd->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"ppd"}));
}

// ---- is-a (Trivia + AstNode, not Expression/Statement/AstType; NOT final) -

// `PreProcessorDirective` is a `Trivia` and an `AstNode` but NOT an `Expression`/`Statement`/
// `AstType`. It is NOT `final` (the C# is not sealed; the two sealed derived classes derive from
// it).
TEST(CSharp_PreProcessorDirective, IsTriviaAndAstNodeButNotFinal) {
    auto ppd = std::make_unique<PreProcessorDirective>();
    EXPECT_NE(dynamic_cast<Trivia*>(ppd.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(ppd.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(ppd.get()), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(ppd.get()), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(ppd.get()), nullptr);
    EXPECT_FALSE(std::is_final_v<PreProcessorDirective>);
}

// ---- DoMatch (the HAND-WRITTEN type + enum-equality + MatchString match) -

// Two `PreProcessorDirective`s with the same `Type` and `Argument` match.
TEST(CSharp_PreProcessorDirective, DoMatchMatchesSameTypeAndArgument) {
    PreProcessorDirective a(PreProcessorDirectiveType::If, std::optional<std::string>("DEBUG"));
    PreProcessorDirective b(PreProcessorDirectiveType::If, std::optional<std::string>("DEBUG"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// Different `Type` rejects (the plain enum-equality term -- no `Any`-wildcard).
TEST(CSharp_PreProcessorDirective, DoMatchRejectsDifferentType) {
    PreProcessorDirective a(PreProcessorDirectiveType::If, std::optional<std::string>("x"));
    PreProcessorDirective b(PreProcessorDirectiveType::Endif, std::optional<std::string>("x"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Different `Argument` rejects (the `MatchString` term).
TEST(CSharp_PreProcessorDirective, DoMatchRejectsDifferentArgument) {
    PreProcessorDirective a(PreProcessorDirectiveType::If, std::optional<std::string>("DEBUG"));
    PreProcessorDirective b(PreProcessorDirectiveType::If, std::optional<std::string>("RELEASE"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `nullopt` `Argument` matches a `nullopt` `Argument` (the C# null == null).
TEST(CSharp_PreProcessorDirective, DoMatchNullArgumentMatchesNull) {
    PreProcessorDirective a(PreProcessorDirectiveType::Endif);
    PreProcessorDirective b(PreProcessorDirectiveType::Endif);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `nullopt` `Argument` does NOT match a non-null `Argument` (the nullable distinction).
TEST(CSharp_PreProcessorDirective, DoMatchNullArgumentDoesNotMatchNonNull) {
    PreProcessorDirective a(PreProcessorDirectiveType::Endif);
    PreProcessorDirective b(PreProcessorDirectiveType::Endif, std::optional<std::string>("x"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// The `$any$` wildcard (`Pattern::AnyString`) in the pattern's `Argument` matches any candidate
// argument (the `MatchString` wildcard path) -- but `Type` must still match.
TEST(CSharp_PreProcessorDirective, DoMatchAnyStringWildcardMatchesAnyArgument) {
    PreProcessorDirective pattern(PreProcessorDirectiveType::If,
                                  std::optional<std::string>(PatternMatching::Pattern::AnyString));
    PreProcessorDirective candA(PreProcessorDirectiveType::If, std::optional<std::string>("anything"));
    PreProcessorDirective candB(PreProcessorDirectiveType::If);  // nullopt
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candA));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candB));
    // The wildcard is on Argument only; a Type mismatch still rejects.
    PreProcessorDirective candC(PreProcessorDirectiveType::Endif, std::optional<std::string>("anything"));
    EXPECT_FALSE(DoMatchAgainst(&pattern, &candC));
}

// A `PreProcessorDirective` does not match a different concrete type.
TEST(CSharp_PreProcessorDirective, DoMatchRejectsDifferentTypeNode) {
    PreProcessorDirective ppd(PreProcessorDirectiveType::If);
    NullReferenceExpression nre;
    EXPECT_FALSE(DoMatchAgainst(&ppd, &nre));
    EXPECT_FALSE(DoMatchAgainst(&nre, &ppd));
}

// A null candidate is rejected.
TEST(CSharp_PreProcessorDirective, DoMatchRejectsNullCandidate) {
    PreProcessorDirective ppd(PreProcessorDirectiveType::If);
    EXPECT_FALSE(DoMatchAgainst(&ppd, nullptr));
}

// The inherited `DoMatch` is SUBTYPE-ACCEPTING (`other as PreProcessorDirective`): a base
// `PreProcessorDirective` pattern with `Type=Line` matches a `LinePreprocessorDirective`
// candidate (which is-a `PreProcessorDirective` and has `Type=Line`).
TEST(CSharp_PreProcessorDirective, DoMatchIsSubtypeAccepting) {
    PreProcessorDirective pattern(PreProcessorDirectiveType::Line);
    LinePreprocessorDirective candidate(TextLocation(1, 1), TextLocation(1, 10));
    EXPECT_EQ(candidate.Type(), PreProcessorDirectiveType::Line);
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candidate));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` copies `Type` + `Argument` + the `Trivia` source span, and detaches.
TEST(CSharp_PreProcessorDirective, CloneCopiesScalarsAndSpanAndDetaches) {
    auto original = std::make_unique<PreProcessorDirective>(PreProcessorDirectiveType::If,
                                                             TextLocation(2, 1), TextLocation(2, 8));
    original->Argument(std::optional<std::string>("DEBUG"));
    std::unique_ptr<PreProcessorDirective> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Type(), PreProcessorDirectiveType::If);
    ASSERT_TRUE(copy->Argument().has_value());
    EXPECT_EQ(*copy->Argument(), "DEBUG");
    EXPECT_EQ(copy->StartLocation(), TextLocation(2, 1));
    EXPECT_EQ(copy->EndLocation(), TextLocation(2, 8));
}

// `Clone` is virtual through `AstNode*`.
TEST(CSharp_PreProcessorDirective, CloneIsVirtualThroughAstNode) {
    auto original = std::make_unique<PreProcessorDirective>(PreProcessorDirectiveType::Region);
    AstNode* node = original.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PreProcessorDirective*>(copy.get()), nullptr);
    EXPECT_EQ(static_cast<PreProcessorDirective*>(copy.get())->Type(), PreProcessorDirectiveType::Region);
}

// `Clone` is a deep copy (mutating the copy does not affect the original).
TEST(CSharp_PreProcessorDirective, CloneIsDeepCopy) {
    auto original = std::make_unique<PreProcessorDirective>(PreProcessorDirectiveType::If,
                                                            std::optional<std::string>("orig"));
    std::unique_ptr<PreProcessorDirective> copy(original->Clone());
    copy->Argument(std::optional<std::string>("copy"));
    ASSERT_TRUE(original->Argument().has_value());
    EXPECT_EQ(*original->Argument(), "orig");
    ASSERT_TRUE(copy->Argument().has_value());
    EXPECT_EQ(*copy->Argument(), "copy");
}

// ---- CheckInvariant + slot-storage (zero-child leaf) ------------------

TEST(CSharp_PreProcessorDirective, CheckInvariantPassesAndLeafHasNoChildren) {
    auto ppd = std::make_unique<PreProcessorDirective>(PreProcessorDirectiveType::If);
    ppd->CheckInvariant();
    EXPECT_EQ(ppd->GetChildCount(), 0);
    EXPECT_FALSE(ppd->HasChildren());
    EXPECT_EQ(ppd->FirstChild(), nullptr);
    EXPECT_EQ(ppd->LastChild(), nullptr);
}

// ============================================================================
// LinePreprocessorDirective (the sealed #line derived class)
// ============================================================================

// ---- Construction ------------------------------------------------------

// The `(span, span)` ctor delegates to the base with `Type=Line` and records the span.
TEST(CSharp_LinePreprocessorDirective, SpanCtorSetsTypeLineAndSpan) {
    LinePreprocessorDirective lpd(TextLocation(3, 1), TextLocation(3, 12));
    EXPECT_EQ(lpd.Type(), PreProcessorDirectiveType::Line);
    EXPECT_FALSE(lpd.Argument().has_value());
    EXPECT_EQ(lpd.StartLocation(), TextLocation(3, 1));
    EXPECT_EQ(lpd.EndLocation(), TextLocation(3, 12));
}

// The `(argument)` ctor delegates to the base with `Type=Line` and sets the argument.
TEST(CSharp_LinePreprocessorDirective, ArgumentCtorSetsTypeLineAndArgument) {
    LinePreprocessorDirective lpd(std::optional<std::string>("10 \"file.cs\""));
    EXPECT_EQ(lpd.Type(), PreProcessorDirectiveType::Line);
    ASSERT_TRUE(lpd.Argument().has_value());
    EXPECT_EQ(*lpd.Argument(), "10 \"file.cs\"");
    // Default argument is nullopt.
    LinePreprocessorDirective lpd2;
    EXPECT_EQ(lpd2.Type(), PreProcessorDirectiveType::Line);
    EXPECT_FALSE(lpd2.Argument().has_value());
}

// ---- AcceptVisitor (inherited -> VisitPreProcessorDirective) ----------

// `AcceptVisitor` dispatches to `VisitPreProcessorDirective` (inherited from the base; there is
// no `VisitLinePreprocessorDirective`).
TEST(CSharp_LinePreprocessorDirective, AcceptVisitorDispatchesToVisitPreProcessorDirective) {
    LinePreprocessorDirective lpd(std::optional<std::string>("default"));
    RecordingVisitor v;
    lpd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"ppd"}));
}

// `AcceptVisitor` is virtual through `PreProcessorDirective*` and `AstNode*`.
TEST(CSharp_LinePreprocessorDirective, AcceptVisitorIsVirtualThroughBases) {
    auto lpd = std::make_unique<LinePreprocessorDirective>(TextLocation(1, 1), TextLocation(1, 5));
    PreProcessorDirective* asPpd = lpd.get();
    AstNode* asAst = lpd.get();
    RecordingVisitor v1, v2;
    asPpd->AcceptVisitor(v1);
    asAst->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"ppd"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"ppd"}));
}

// ---- is-a (PreProcessorDirective + Trivia + AstNode; final) -----------

TEST(CSharp_LinePreprocessorDirective, IsPreProcessorDirectiveAndFinal) {
    auto lpd = std::make_unique<LinePreprocessorDirective>();
    EXPECT_NE(dynamic_cast<PreProcessorDirective*>(lpd.get()), nullptr);
    EXPECT_NE(dynamic_cast<Trivia*>(lpd.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(lpd.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<LinePreprocessorDirective>);
}

// ---- DoMatch (inherited hand-written; subtype-accepting) --------------

// Two `LinePreprocessorDirective`s with the same (default-nullopt) `Argument` match (both
// `Type=Line`).
TEST(CSharp_LinePreprocessorDirective, DoMatchMatchesSibling) {
    LinePreprocessorDirective a;
    LinePreprocessorDirective b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `LinePreprocessorDirective` (Type=Line) does NOT match a `PragmaWarningPreprocessorDirective`
// (Type=Pragma) -- the `Type` term rejects (cross-sibling rejection).
TEST(CSharp_LinePreprocessorDirective, DoMatchRejectsPragmaSiblingOnType) {
    LinePreprocessorDirective lpd;
    PragmaWarningPreprocessorDirective ppd;
    EXPECT_EQ(lpd.Type(), PreProcessorDirectiveType::Line);
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Pragma);
    EXPECT_FALSE(DoMatchAgainst(&lpd, &ppd));
    EXPECT_FALSE(DoMatchAgainst(&ppd, &lpd));
}

// A base `PreProcessorDirective` pattern (Type=Line, matching Argument) matches a
// `LinePreprocessorDirective` candidate (subtype-accepting, matching Type + Argument).
TEST(CSharp_LinePreprocessorDirective, DoMatchBasePatternMatchesDerived) {
    PreProcessorDirective pattern(PreProcessorDirectiveType::Line, std::optional<std::string>("hidden"));
    LinePreprocessorDirective candidate(std::optional<std::string>("hidden"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candidate));
}

// ---- Clone (per-concrete; no slicing) ---------------------------------

// `Clone` creates a `LinePreprocessorDirective` (NOT a sliced `PreProcessorDirective`), copies
// `Type=Line` + `Argument` + the span, and detaches.
TEST(CSharp_LinePreprocessorDirective, CloneCreatesConcreteTypeNoSlicing) {
    auto original = std::make_unique<LinePreprocessorDirective>(TextLocation(1, 1), TextLocation(1, 9));
    original->Argument(std::optional<std::string>("hidden"));
    std::unique_ptr<LinePreprocessorDirective> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Type(), PreProcessorDirectiveType::Line);
    ASSERT_TRUE(copy->Argument().has_value());
    EXPECT_EQ(*copy->Argument(), "hidden");
    EXPECT_EQ(copy->StartLocation(), TextLocation(1, 1));
    EXPECT_EQ(copy->EndLocation(), TextLocation(1, 9));
    // The clone is a LinePreprocessorDirective, not a sliced base.
    EXPECT_NE(dynamic_cast<LinePreprocessorDirective*>(copy.get()), nullptr);
}

// `Clone` is virtual through `PreProcessorDirective*`: a call through the base pointer returns a
// `LinePreprocessorDirective` (the per-concrete override, no slicing).
TEST(CSharp_LinePreprocessorDirective, CloneIsVirtualThroughBaseNoSlicing) {
    auto original = std::make_unique<LinePreprocessorDirective>(std::optional<std::string>("default"));
    PreProcessorDirective* asPpd = original.get();
    std::unique_ptr<PreProcessorDirective> copy(asPpd->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<LinePreprocessorDirective*>(copy.get()), nullptr);
    EXPECT_EQ(copy->Type(), PreProcessorDirectiveType::Line);
}

// ---- CheckInvariant + slot-storage (zero-child leaf) ------------------

TEST(CSharp_LinePreprocessorDirective, CheckInvariantPassesAndLeafHasNoChildren) {
    auto lpd = std::make_unique<LinePreprocessorDirective>();
    lpd->CheckInvariant();
    EXPECT_EQ(lpd->GetChildCount(), 0);
    EXPECT_FALSE(lpd->HasChildren());
}

// ============================================================================
// PragmaWarningPreprocessorDirective (the sealed #pragma warning derived class)
// ============================================================================

// ---- Construction ------------------------------------------------------

// The `(span, span)` ctor delegates to the base with `Type=Pragma` and records the span; the
// `Warnings` collection starts empty.
TEST(CSharp_PragmaWarningPreprocessorDirective, SpanCtorSetsTypePragmaAndSpan) {
    PragmaWarningPreprocessorDirective ppd(TextLocation(1, 1), TextLocation(1, 30));
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Pragma);
    EXPECT_FALSE(ppd.Argument().has_value());
    EXPECT_EQ(ppd.StartLocation(), TextLocation(1, 1));
    EXPECT_EQ(ppd.EndLocation(), TextLocation(1, 30));  // no warnings -> stored end
    EXPECT_EQ(ppd.Warnings().Count(), 0);
    EXPECT_EQ(ppd.GetChildCount(), 0);
}

// The `(argument)` ctor delegates to the base with `Type=Pragma` and sets the argument.
TEST(CSharp_PragmaWarningPreprocessorDirective, ArgumentCtorSetsTypePragmaAndArgument) {
    PragmaWarningPreprocessorDirective ppd(std::optional<std::string>("disable"));
    EXPECT_EQ(ppd.Type(), PreProcessorDirectiveType::Pragma);
    ASSERT_TRUE(ppd.Argument().has_value());
    EXPECT_EQ(*ppd.Argument(), "disable");
    EXPECT_EQ(ppd.Warnings().Count(), 0);
}

// ---- Warnings collection ----------------------------------------------

// `Warnings().Add` appends a `PrimitiveExpression` and re-parents it; `GetChildCount` tracks the
// collection length (the collection-only slot-storage shape).
TEST(CSharp_PragmaWarningPreprocessorDirective, WarningsAddReparentsAndTracksCount) {
    PragmaWarningPreprocessorDirective ppd;
    auto w1 = std::make_unique<PrimitiveExpression>(int32_t(42));
    auto w2 = std::make_unique<PrimitiveExpression>(int32_t(103));
    ppd.Warnings().Add(w1.get());
    ppd.Warnings().Add(w2.get());
    EXPECT_EQ(ppd.Warnings().Count(), 2);
    EXPECT_EQ(ppd.GetChildCount(), 2);
    EXPECT_EQ(ppd.Warnings().At(0), w1.get());
    EXPECT_EQ(ppd.Warnings().At(1), w2.get());
    EXPECT_EQ(w1->Parent(), &ppd);
    EXPECT_EQ(w2->Parent(), &ppd);
    // Incremental collection: ChildIndex == local position.
    ASSERT_TRUE(w1->Slot() != nullptr);
    ASSERT_TRUE(w2->Slot() != nullptr);
    EXPECT_EQ(w1->ChildIndex, 0);
    EXPECT_EQ(w2->ChildIndex, 1);
}

// `GetCollectionByKind` returns the `Warnings` collection for the `Warning` kind, null otherwise.
TEST(CSharp_PragmaWarningPreprocessorDirective, GetCollectionByKindReturnsWarnings) {
    PragmaWarningPreprocessorDirective ppd;
    EXPECT_NE(ppd.GetCollectionByKind(&Slots::Warning), nullptr);
    EXPECT_EQ(ppd.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(ppd.GetCollectionByKind(&Slots::Argument), nullptr);
}

// `GetChild`/`GetChildSlotInfo` walk the single collection; out-of-range throws.
TEST(CSharp_PragmaWarningPreprocessorDirective, GetChildAndSlotInfoWalkCollection) {
    PragmaWarningPreprocessorDirective ppd;
    auto w = std::make_unique<PrimitiveExpression>(int32_t(1));
    ppd.Warnings().Add(w.get());
    EXPECT_EQ(ppd.GetChild(0), w.get());
    EXPECT_EQ(ppd.GetChildSlotInfo(0), &PragmaWarningPreprocessorDirective::WarningsSlot);
    EXPECT_THROW(ppd.GetChild(1), std::out_of_range);
    EXPECT_THROW(ppd.GetChildSlotInfo(1), std::out_of_range);
}

// ---- EndLocation override (computed from LastChild) ------------------

// With no warnings, `EndLocation` falls back to the stored `Trivia` span end.
TEST(CSharp_PragmaWarningPreprocessorDirective, EndLocationFallsBackToStoredWhenNoWarnings) {
    PragmaWarningPreprocessorDirective ppd(TextLocation(1, 1), TextLocation(1, 20));
    EXPECT_EQ(ppd.EndLocation(), TextLocation(1, 20));  // LastChild null -> stored end
}

// With warnings, `EndLocation` is the last warning's `EndLocation` (the computed override).
TEST(CSharp_PragmaWarningPreprocessorDirective, EndLocationIsLastWarningEnd) {
    PragmaWarningPreprocessorDirective ppd(TextLocation(1, 1), TextLocation(1, 30));
    auto w1 = std::make_unique<PrimitiveExpression>(int32_t(42));
    w1->SetLocation(TextLocation(1, 15), TextLocation(1, 17));
    auto w2 = std::make_unique<PrimitiveExpression>(int32_t(103));
    w2->SetLocation(TextLocation(1, 19), TextLocation(1, 22));  // last warning
    ppd.Warnings().Add(w1.get());
    ppd.Warnings().Add(w2.get());
    EXPECT_EQ(ppd.LastChild(), w2.get());
    EXPECT_EQ(ppd.EndLocation(), TextLocation(1, 22));  // last warning's end
}

// ---- AcceptVisitor (inherited -> VisitPreProcessorDirective) ----------

// `AcceptVisitor` dispatches to `VisitPreProcessorDirective` (inherited; no
// `VisitPragmaWarningPreprocessorDirective`).
TEST(CSharp_PragmaWarningPreprocessorDirective, AcceptVisitorDispatchesToVisitPreProcessorDirective) {
    PragmaWarningPreprocessorDirective ppd;
    RecordingVisitor v;
    ppd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"ppd"}));  // no warnings -> just the node
}

// The depth-first walk recurses into the `Warnings` collection (each warning is a visited child).
TEST(CSharp_PragmaWarningPreprocessorDirective, DepthFirstWalkRecursesIntoWarnings) {
    PragmaWarningPreprocessorDirective ppd;
    auto w1 = std::make_unique<PrimitiveExpression>(int32_t(42));
    auto w2 = std::make_unique<PrimitiveExpression>(int32_t(103));
    ppd.Warnings().Add(w1.get());
    ppd.Warnings().Add(w2.get());
    RecordingVisitor v;
    ppd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"ppd", "prim", "prim"}));
}

// ---- is-a (PreProcessorDirective + Trivia + AstNode; final) -----------

TEST(CSharp_PragmaWarningPreprocessorDirective, IsPreProcessorDirectiveAndFinal) {
    auto ppd = std::make_unique<PragmaWarningPreprocessorDirective>();
    EXPECT_NE(dynamic_cast<PreProcessorDirective*>(ppd.get()), nullptr);
    EXPECT_NE(dynamic_cast<Trivia*>(ppd.get()), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(ppd.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<PragmaWarningPreprocessorDirective>);
}

// ---- DoMatch (inherited; Warnings NOT matched) -----------------------

// Two `PragmaWarningPreprocessorDirective`s with the same `Type` + `Argument` match even with
// DIFFERENT `Warnings` (the inherited hand-written `DoMatch` does NOT match `Warnings`).
TEST(CSharp_PragmaWarningPreprocessorDirective, DoMatchIgnoresWarnings) {
    PragmaWarningPreprocessorDirective a(std::optional<std::string>("disable"));
    PragmaWarningPreprocessorDirective b(std::optional<std::string>("disable"));
    auto aw = std::make_unique<PrimitiveExpression>(int32_t(42));
    a.Warnings().Add(aw.get());  // a has a warning, b has none
    EXPECT_EQ(a.Warnings().Count(), 1);
    EXPECT_EQ(b.Warnings().Count(), 0);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
    EXPECT_TRUE(DoMatchAgainst(&b, &a));
}

// A `PragmaWarningPreprocessorDirective` (Type=Pragma) rejects a `LinePreprocessorDirective`
// (Type=Line) on the `Type` term (cross-sibling rejection).
TEST(CSharp_PragmaWarningPreprocessorDirective, DoMatchRejectsLineSiblingOnType) {
    PragmaWarningPreprocessorDirective ppd;
    LinePreprocessorDirective lpd;
    EXPECT_FALSE(DoMatchAgainst(&ppd, &lpd));
}

// ---- Clone (per-concrete; no slicing; deep-copies Warnings; stored span) -

// `Clone` creates a `PragmaWarningPreprocessorDirective` (no slicing), copies `Type=Pragma` +
// `Argument` + the STORED `Trivia` span, deep-copies the `Warnings`, and detaches.
TEST(CSharp_PragmaWarningPreprocessorDirective, CloneCreatesConcreteTypeDeepCopiesWarnings) {
    auto original = std::make_unique<PragmaWarningPreprocessorDirective>(TextLocation(1, 1), TextLocation(1, 30));
    original->Argument(std::optional<std::string>("disable"));
    auto w1 = std::make_unique<PrimitiveExpression>(int32_t(42));
    w1->SetLocation(TextLocation(1, 15), TextLocation(1, 17));
    auto w2 = std::make_unique<PrimitiveExpression>(int32_t(103));
    w2->SetLocation(TextLocation(1, 19), TextLocation(1, 22));
    original->Warnings().Add(w1.get());
    original->Warnings().Add(w2.get());

    std::unique_ptr<PragmaWarningPreprocessorDirective> copy(original->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), original.get());
    EXPECT_EQ(copy->Parent(), nullptr);
    EXPECT_EQ(copy->Type(), PreProcessorDirectiveType::Pragma);
    ASSERT_TRUE(copy->Argument().has_value());
    EXPECT_EQ(*copy->Argument(), "disable");
    // The stored span is copied (NOT the computed EndLocation).
    EXPECT_EQ(copy->StartLocation(), TextLocation(1, 1));
    EXPECT_EQ(copy->Trivia::EndLocation(), TextLocation(1, 30));  // stored end (explicit base call)
    // The Warnings are deep-copied (count matches, distinct nodes, re-parented to the clone).
    EXPECT_EQ(copy->Warnings().Count(), 2);
    EXPECT_NE(copy->Warnings().At(0), w1.get());
    EXPECT_NE(copy->Warnings().At(1), w2.get());
    EXPECT_EQ(copy->Warnings().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->Warnings().At(1)->Parent(), copy.get());
    // The computed EndLocation on the clone is the last warning's end (Warnings deep-copied).
    EXPECT_EQ(copy->EndLocation(), TextLocation(1, 22));
}

// `Clone` is virtual through `PreProcessorDirective*`: a call through the base pointer returns a
// `PragmaWarningPreprocessorDirective` (the per-concrete override, no slicing).
TEST(CSharp_PragmaWarningPreprocessorDirective, CloneIsVirtualThroughBaseNoSlicing) {
    auto original = std::make_unique<PragmaWarningPreprocessorDirective>(std::optional<std::string>("restore"));
    PreProcessorDirective* asPpd = original.get();
    std::unique_ptr<PreProcessorDirective> copy(asPpd->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<PragmaWarningPreprocessorDirective*>(copy.get()), nullptr);
    EXPECT_EQ(copy->Type(), PreProcessorDirectiveType::Pragma);
}

// ---- CheckInvariant (collection-only; no required single slots) -------

// A `PragmaWarningPreprocessorDirective` (collection-only; no required single slots) passes
// `CheckInvariant` on both the empty and the filled node.
TEST(CSharp_PragmaWarningPreprocessorDirective, CheckInvariantPassesEmptyAndFilled) {
    auto ppd = std::make_unique<PragmaWarningPreprocessorDirective>();
    ppd->CheckInvariant();  // empty -> passes (no required slots)
    auto w = std::make_unique<PrimitiveExpression>(int32_t(42));
    ppd->Warnings().Add(w.get());
    ppd->CheckInvariant();  // filled -> passes
}
