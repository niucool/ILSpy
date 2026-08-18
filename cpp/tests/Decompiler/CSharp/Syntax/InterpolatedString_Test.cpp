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

// Tests for the `InterpolatedStringContent` abstract base and the `Interpolation`,
// `InterpolatedStringText`, and `InterpolatedStringExpression` concrete nodes (the ports of
// the four classes in ICSharpCode.Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.cs).
// The `interpolated_string_content ::= interpolation | interpolated_string_text` (C# grammar
// 12.8.3) common base and its two concrete subclasses, plus the
// `interpolated_string_expression ::= interpolated_string_content*` (C# grammar 12.8.3)
// collection-only `Expression`.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringText.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the three concrete nodes under test (plus the
// leaf `Expression`s used as operands and the `WhileStatement` used for the cross-type
// DoMatch rejection), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitInterpolation(Interpolation* node) override {
        if (node == nullptr) { trace.push_back("<null-interp>"); return; }
        trace.push_back("interp");
        VisitChildren(node);
    }
    void VisitInterpolatedStringText(InterpolatedStringText* node) override {
        if (node == nullptr) { trace.push_back("<null-text>"); return; }
        trace.push_back("text");
        VisitChildren(node);
    }
    void VisitInterpolatedStringExpression(InterpolatedStringExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-istr>"); return; }
        trace.push_back("istr");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-this>"); return; }
        trace.push_back("this");
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
// pattern). Uses `Match::CreateNew()` (a default-constructed `Match()` holds a NULL capture
// vector -- the D283 crux).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping an `Interpolation` and its `Expression` child alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design).
struct InterpHolder {
    std::unique_ptr<Interpolation> interp;
    std::unique_ptr<Expression> expr;
    Interpolation* get() const { return interp.get(); }
    Interpolation* operator->() const { return interp.get(); }
};

// Build an `Interpolation` with a `ThisReferenceExpression` `Expression`, alignment 0, no
// suffix (the bare `{expr}` form). The holder keeps every node alive.
InterpHolder make_Interp() {
    InterpHolder h;
    h.expr = std::make_unique<ThisReferenceExpression>();
    h.interp = std::make_unique<Interpolation>(h.expr.get());
    return h;
}

// A holder keeping an `InterpolatedStringExpression` and its `Content` elements alive.
struct IStrHolder {
    std::unique_ptr<InterpolatedStringExpression> istr;
    std::unique_ptr<InterpolatedStringText> text0;
    std::unique_ptr<ThisReferenceExpression> interpExpr;
    std::unique_ptr<Interpolation> interp;
    std::unique_ptr<InterpolatedStringText> text1;
    InterpolatedStringExpression* get() const { return istr.get(); }
    InterpolatedStringExpression* operator->() const { return istr.get(); }
};

// Build an `InterpolatedStringExpression` with two `InterpolatedStringText` runs bracketing
// one `Interpolation` (`$"Hello {this}!"`). The holder keeps every node alive.
IStrHolder make_IStr() {
    IStrHolder h;
    h.istr = std::make_unique<InterpolatedStringExpression>();
    h.text0 = std::make_unique<InterpolatedStringText>(std::string("Hello "));
    h.istr->Content().Add(h.text0.get());
    h.interpExpr = std::make_unique<ThisReferenceExpression>();
    h.interp = std::make_unique<Interpolation>(h.interpExpr.get());
    h.istr->Content().Add(h.interp.get());
    h.text1 = std::make_unique<InterpolatedStringText>(std::string("!"));
    h.istr->Content().Add(h.text1.get());
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// InterpolatedStringContent (abstract base)
// ---------------------------------------------------------------------------
TEST(CSharp_InterpolatedStringContent, IsAbstractAndAstNode) {
    EXPECT_TRUE(std::is_abstract_v<InterpolatedStringContent>);
    EXPECT_TRUE((std::is_base_of_v<AstNode, InterpolatedStringContent>));
    // Disjoint from Expression/Statement (an InterpolatedStringContent is an AstNode only).
    EXPECT_FALSE((std::is_base_of_v<Expression, InterpolatedStringContent>));
}

TEST(CSharp_InterpolatedStringContent, ConcreteSubclassesAreInterpolatedStringContent) {
    EXPECT_TRUE((std::is_base_of_v<InterpolatedStringContent, Interpolation>));
    EXPECT_TRUE((std::is_base_of_v<InterpolatedStringContent, InterpolatedStringText>));
}

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------
TEST(CSharp_Interpolation, IsFinalAndIsA) {
    EXPECT_TRUE(std::is_final_v<Interpolation>);
    EXPECT_TRUE((std::is_base_of_v<InterpolatedStringContent, Interpolation>));
    EXPECT_TRUE((std::is_base_of_v<AstNode, Interpolation>));
    // An Interpolation is NOT an Expression (disjoint hierarchy).
    EXPECT_FALSE((std::is_base_of_v<Expression, Interpolation>));
}

TEST(CSharp_Interpolation, ConstructionDefaults) {
    Interpolation i;
    EXPECT_EQ(i.Expression(), nullptr);
    EXPECT_EQ(i.Alignment(), 0);
    EXPECT_FALSE(i.Suffix().has_value());
}

TEST(CSharp_Interpolation, HandWrittenCtorSetsAllThree) {
    auto expr = std::make_unique<ThisReferenceExpression>();
    Interpolation i(expr.get(), 5, std::string("F2"));
    EXPECT_EQ(i.Expression(), expr.get());
    EXPECT_EQ(i.Alignment(), 5);
    ASSERT_TRUE(i.Suffix().has_value());
    EXPECT_EQ(*i.Suffix(), "F2");
}

TEST(CSharp_Interpolation, HandWrittenCtorDefaultsAlignmentAndSuffix) {
    auto expr = std::make_unique<ThisReferenceExpression>();
    Interpolation i(expr.get());  // alignment=0, suffix=nullopt defaults
    EXPECT_EQ(i.Expression(), expr.get());
    EXPECT_EQ(i.Alignment(), 0);
    EXPECT_FALSE(i.Suffix().has_value());
}

TEST(CSharp_Interpolation, GetOnlyScalarsAreImmutable) {
    // Alignment/Suffix are get-only (no setter) -- the only way to set them is the ctor.
    auto expr = std::make_unique<ThisReferenceExpression>();
    Interpolation i(expr.get(), -3, std::string(":G"));
    EXPECT_EQ(i.Alignment(), -3);
    ASSERT_TRUE(i.Suffix().has_value());
    EXPECT_EQ(*i.Suffix(), ":G");
}

TEST(CSharp_Interpolation, ExpressionSlotSetterReparentsAndClearsOld) {
    auto e1 = std::make_unique<ThisReferenceExpression>();
    Interpolation i(e1.get());
    EXPECT_EQ(i.Expression(), e1.get());
    EXPECT_EQ(e1->Parent(), &i);
    EXPECT_EQ(e1->ChildIndex, 0);
    auto e2 = std::make_unique<NullReferenceExpression>();
    i.Expression(e2.get());
    EXPECT_EQ(i.Expression(), e2.get());
    EXPECT_EQ(e2->Parent(), &i);
    EXPECT_EQ(e2->ChildIndex, 0);
    EXPECT_EQ(e1->Parent(), nullptr);
}

TEST(CSharp_Interpolation, SlotStorageContract) {
    auto h = make_Interp();
    EXPECT_EQ(h->GetChildCount(), 1);
    EXPECT_EQ(h->GetChild(0), h.expr.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &Interpolation::ExpressionSlot);
    EXPECT_THROW(h->GetChild(1), std::out_of_range);
    EXPECT_THROW(h->SetChild(1, nullptr), std::out_of_range);
    EXPECT_THROW(h->GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_Interpolation, SlotStaticIdentityAndKind) {
    EXPECT_EQ(Interpolation::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(Interpolation::ExpressionSlot.IsCollection());
    EXPECT_FALSE(Interpolation::ExpressionSlot.IsOptional());
    EXPECT_TRUE(Interpolation::ExpressionSlot.IsInstanceOfType(std::make_unique<ThisReferenceExpression>().get()));
    EXPECT_FALSE(Interpolation::ExpressionSlot.IsInstanceOfType(std::make_unique<WhileStatement>().get()));
}

TEST(CSharp_Interpolation, AcceptVisitorDispatchAndVirtuality) {
    RecordingVisitor v;
    // Use a default-constructed (empty) Interpolation so VisitChildren yields just the tag
    // (the D276/D288 dispatch-test precedent -- a filled node recurses into its child).
    Interpolation empty;
    empty.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "interp");
    // Virtual dispatch through AstNode* / InterpolatedStringContent*.
    AstNode* asNode = &empty;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[1], "interp");
    InterpolatedStringContent* asBase = &empty;
    asBase->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[2], "interp");
}

TEST(CSharp_Interpolation, DepthFirstWalk) {
    auto h = make_Interp();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // interp then the ThisReferenceExpression operand.
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "interp");
    EXPECT_EQ(v.trace[1], "this");
}

TEST(CSharp_Interpolation, DoMatchMatchesSame) {
    auto a = make_Interp();
    auto b = make_Interp();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_Interpolation, DoMatchRejectsDifferentExpression) {
    auto a = make_Interp();  // ThisReferenceExpression
    InterpHolder b;
    b.expr = std::make_unique<NullReferenceExpression>();
    b.interp = std::make_unique<Interpolation>(b.expr.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_Interpolation, DoMatchRejectsAlignmentMismatch) {
    auto e1 = std::make_unique<ThisReferenceExpression>();
    Interpolation a(e1.get(), 0);
    auto e2 = std::make_unique<ThisReferenceExpression>();
    Interpolation b(e2.get(), 5);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_Interpolation, DoMatchRejectsSuffixMismatch) {
    auto e1 = std::make_unique<ThisReferenceExpression>();
    Interpolation a(e1.get(), 0, std::string("F"));
    auto e2 = std::make_unique<ThisReferenceExpression>();
    Interpolation b(e2.get(), 0, std::string("G"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_Interpolation, DoMatchSuffixNullVsEmptyAreDistinct) {
    auto e1 = std::make_unique<ThisReferenceExpression>();
    Interpolation a(e1.get(), 0, std::string(""));  // empty string
    auto e2 = std::make_unique<ThisReferenceExpression>();
    Interpolation b(e2.get(), 0, std::nullopt);  // null
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_Interpolation, DoMatchMatchesBothNullSuffix) {
    auto e1 = std::make_unique<ThisReferenceExpression>();
    Interpolation a(e1.get(), 0, std::nullopt);
    auto e2 = std::make_unique<ThisReferenceExpression>();
    Interpolation b(e2.get(), 0, std::nullopt);
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_Interpolation, DoMatchRejectsDifferentType) {
    auto h = make_Interp();
    auto txt = std::make_unique<InterpolatedStringText>(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(h.get(), txt.get()));
}

TEST(CSharp_Interpolation, DoMatchRejectsNullCandidate) {
    auto h = make_Interp();
    EXPECT_FALSE(DoMatchAgainst(h.get(), nullptr));
}

TEST(CSharp_Interpolation, CloneDeepCopiesScalarAndChild) {
    auto e1 = std::make_unique<ThisReferenceExpression>();
    Interpolation original(e1.get(), 7, std::string("X"));
    Interpolation* copy = original.Clone();
    EXPECT_EQ(copy->Alignment(), 7);
    ASSERT_TRUE(copy->Suffix().has_value());
    EXPECT_EQ(*copy->Suffix(), "X");
    ASSERT_NE(copy->Expression(), nullptr);
    EXPECT_NE(copy->Expression(), original.Expression());  // deep copy, distinct node
    EXPECT_EQ(copy->Expression()->Parent(), copy);
    delete copy;
}

TEST(CSharp_Interpolation, CloneVirtualAndCovariant) {
    auto h = make_Interp();
    InterpolatedStringContent* asBase = h.get();
    InterpolatedStringContent* copy = asBase->Clone();
    EXPECT_NE(copy, nullptr);
    auto* typedCopy = dynamic_cast<Interpolation*>(copy);
    EXPECT_NE(typedCopy, nullptr);
    delete copy;
}

TEST(CSharp_Interpolation, CheckInvariantEmptyRejected) {
    Interpolation empty;  // required Expression slot is null
#ifndef NDEBUG
    EXPECT_DEATH(empty.CheckInvariant(), "");
#endif
}

TEST(CSharp_Interpolation, CheckInvariantFilledPasses) {
    auto h = make_Interp();
    // A filled node (required Expression slot present) passes the inherited CheckInvariant
    // (a no-op in NDEBUG, asserts the required slot in debug).
    h->CheckInvariant();
}

// ---------------------------------------------------------------------------
// InterpolatedStringText
// ---------------------------------------------------------------------------
TEST(CSharp_InterpolatedStringText, IsFinalAndIsA) {
    EXPECT_TRUE(std::is_final_v<InterpolatedStringText>);
    EXPECT_TRUE((std::is_base_of_v<InterpolatedStringContent, InterpolatedStringText>));
    EXPECT_TRUE((std::is_base_of_v<AstNode, InterpolatedStringText>));
    EXPECT_FALSE((std::is_base_of_v<Expression, InterpolatedStringText>));
}

TEST(CSharp_InterpolatedStringText, ConstructionDefaultsEmpty) {
    InterpolatedStringText t;
    EXPECT_EQ(t.Text(), "");
}

TEST(CSharp_InterpolatedStringText, StringCtorSetsText) {
    InterpolatedStringText t(std::string("Hello"));
    EXPECT_EQ(t.Text(), "Hello");
}

TEST(CSharp_InterpolatedStringText, TextSetter) {
    InterpolatedStringText t;
    t.Text(std::string("World"));
    EXPECT_EQ(t.Text(), "World");
    t.Text(std::string(""));
    EXPECT_EQ(t.Text(), "");
}

TEST(CSharp_InterpolatedStringText, ZeroChildSlotStorage) {
    InterpolatedStringText t;
    EXPECT_EQ(t.GetChildCount(), 0);
    EXPECT_THROW(t.GetChild(0), std::out_of_range);
    EXPECT_THROW(t.SetChild(0, nullptr), std::out_of_range);
    EXPECT_THROW(t.GetChildSlotInfo(0), std::out_of_range);
}

TEST(CSharp_InterpolatedStringText, AcceptVisitorDispatchAndVirtuality) {
    RecordingVisitor v;
    InterpolatedStringText t;
    t.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "text");
    AstNode* asNode = &t;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[1], "text");
    InterpolatedStringContent* asBase = &t;
    asBase->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[2], "text");
}

TEST(CSharp_InterpolatedStringText, DepthFirstWalkIsLeaf) {
    InterpolatedStringText t(std::string("hi"));
    RecordingVisitor v;
    t.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "text");
}

TEST(CSharp_InterpolatedStringText, DoMatchMatchesSame) {
    InterpolatedStringText a(std::string("Hello"));
    InterpolatedStringText b(std::string("Hello"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_InterpolatedStringText, DoMatchRejectsDifferentText) {
    InterpolatedStringText a(std::string("Hello"));
    InterpolatedStringText b(std::string("World"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_InterpolatedStringText, DoMatchEmptyMatchesEmpty) {
    InterpolatedStringText a;
    InterpolatedStringText b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_InterpolatedStringText, DoMatchAnyStringWildcard) {
    InterpolatedStringText a(std::string("anything"));
    // Braced-init avoids the most-vexing-parse (a bare `InterpolatedStringText
    // pattern(std::string(...))` declares a function, the D236 precedent).
    InterpolatedStringText pattern{std::string(PatternMatching::Pattern::AnyString)};
    EXPECT_TRUE(DoMatchAgainst(&pattern, &a));
}

TEST(CSharp_InterpolatedStringText, DoMatchRejectsDifferentType) {
    InterpolatedStringText a(std::string("x"));
    auto expr = std::make_unique<ThisReferenceExpression>();
    Interpolation b(expr.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_InterpolatedStringText, DoMatchRejectsNullCandidate) {
    InterpolatedStringText a(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

TEST(CSharp_InterpolatedStringText, CloneDeepCopiesText) {
    InterpolatedStringText original(std::string("Hello"));
    InterpolatedStringText* copy = original.Clone();
    EXPECT_EQ(copy->Text(), "Hello");
    // Mutating the clone does not affect the original.
    copy->Text(std::string("World"));
    EXPECT_EQ(original.Text(), "Hello");
    EXPECT_EQ(copy->Text(), "World");
    delete copy;
}

TEST(CSharp_InterpolatedStringText, CloneVirtualAndCovariant) {
    InterpolatedStringText original(std::string("x"));
    InterpolatedStringContent* asBase = &original;
    InterpolatedStringContent* copy = asBase->Clone();
    EXPECT_NE(dynamic_cast<InterpolatedStringText*>(copy), nullptr);
    delete copy;
}

TEST(CSharp_InterpolatedStringText, CheckInvariantPasses) {
    InterpolatedStringText t(std::string("hi"));
    t.CheckInvariant();  // leaf with no required slots: passes (no-op in NDEBUG)
    InterpolatedStringText empty;
    empty.CheckInvariant();
}

// ---------------------------------------------------------------------------
// InterpolatedStringExpression
// ---------------------------------------------------------------------------
TEST(CSharp_InterpolatedStringExpression, IsFinalAndIsA) {
    EXPECT_TRUE(std::is_final_v<InterpolatedStringExpression>);
    EXPECT_TRUE((std::is_base_of_v<Expression, InterpolatedStringExpression>));
    EXPECT_TRUE((std::is_base_of_v<AstNode, InterpolatedStringExpression>));
    EXPECT_FALSE((std::is_base_of_v<InterpolatedStringContent, InterpolatedStringExpression>));
}

TEST(CSharp_InterpolatedStringExpression, ConstStrings) {
    EXPECT_STREQ(InterpolatedStringExpression::OpenQuote, "$\"");
    EXPECT_STREQ(InterpolatedStringExpression::CloseQuote, "\"");
}

TEST(CSharp_InterpolatedStringExpression, ConstructionEmpty) {
    InterpolatedStringExpression istr;
    EXPECT_EQ(istr.GetChildCount(), 0);
    EXPECT_EQ(istr.Content().Count(), 0);
}

TEST(CSharp_InterpolatedStringExpression, ContentCollectionAddReparentsAndIndexes) {
    InterpolatedStringExpression istr;
    auto text = std::make_unique<InterpolatedStringText>(std::string("Hello"));
    istr.Content().Add(text.get());
    ASSERT_EQ(istr.Content().Count(), 1);
    EXPECT_EQ(istr.Content().At(0), text.get());
    EXPECT_EQ(text->Parent(), &istr);
    // Incremental collection (sole collection, last slot): ChildIndex == local position.
    EXPECT_EQ(text->ChildIndex, 0);
    EXPECT_EQ(istr.GetChildCount(), 1);
    EXPECT_EQ(istr.GetChild(0), text.get());
    EXPECT_EQ(istr.GetChildSlotInfo(0), &InterpolatedStringExpression::ContentSlot);
}

TEST(CSharp_InterpolatedStringExpression, GetCollectionByKind) {
    InterpolatedStringExpression istr;
    EXPECT_EQ(istr.GetCollectionByKind(&Slots::Content), &istr.Content());
    EXPECT_EQ(istr.GetCollectionByKind(&Slots::Expression), nullptr);
}

TEST(CSharp_InterpolatedStringExpression, SlotStaticIdentity) {
    EXPECT_EQ(InterpolatedStringExpression::ContentSlot.Kind(), &Slots::Content);
    EXPECT_TRUE(InterpolatedStringExpression::ContentSlot.IsCollection());
    EXPECT_TRUE(InterpolatedStringExpression::ContentSlot.IsOptional());
    EXPECT_TRUE(InterpolatedStringExpression::ContentSlot.IsInstanceOfType(std::make_unique<InterpolatedStringText>().get()));
    EXPECT_FALSE(InterpolatedStringExpression::ContentSlot.IsInstanceOfType(std::make_unique<WhileStatement>().get()));
}

TEST(CSharp_InterpolatedStringExpression, SlotStaticDistinctFromOtherKinds) {
    // The Content kind is distinct from prior Slots kinds (cast to the common base for the
    // cross-element-type EXPECT_NE, the D222 precedent).
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&InterpolatedStringExpression::ContentSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::Expression));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&InterpolatedStringExpression::ContentSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::SubPattern));
}

TEST(CSharp_InterpolatedStringExpression, AcceptVisitorDispatchAndVirtuality) {
    RecordingVisitor v;
    // Use an empty node so VisitChildren yields just the tag (the D276/D288 precedent).
    InterpolatedStringExpression istr;
    istr.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "istr");
    AstNode* asNode = &istr;
    asNode->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[1], "istr");
    Expression* asExpr = &istr;
    asExpr->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[2], "istr");
}

TEST(CSharp_InterpolatedStringExpression, DepthFirstWalk) {
    auto h = make_IStr();  // $"Hello {this}!"
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // istr, text:Hello , interp, this, text:!
    ASSERT_EQ(v.trace.size(), 5u);
    EXPECT_EQ(v.trace[0], "istr");
    EXPECT_EQ(v.trace[1], "text");
    EXPECT_EQ(v.trace[2], "interp");
    EXPECT_EQ(v.trace[3], "this");
    EXPECT_EQ(v.trace[4], "text");
}

TEST(CSharp_InterpolatedStringExpression, DepthFirstWalkEmptyIsJustNode) {
    InterpolatedStringExpression istr;
    RecordingVisitor v;
    istr.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "istr");
}

TEST(CSharp_InterpolatedStringExpression, DoMatchMatchesSame) {
    auto a = make_IStr();
    auto b = make_IStr();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_InterpolatedStringExpression, DoMatchRejectsDifferentCount) {
    auto a = make_IStr();  // 3 content elements
    InterpolatedStringExpression b;
    auto text = std::make_unique<InterpolatedStringText>(std::string("Hello "));
    b.Content().Add(text.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_InterpolatedStringExpression, DoMatchRejectsDifferentElementValue) {
    auto a = make_IStr();
    auto b = make_IStr();
    // Mutate b's trailing text to differ.
    b.text1->Text(std::string("?"));
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_InterpolatedStringExpression, DoMatchRejectsDifferentElementType) {
    auto a = make_IStr();  // text, interp, text
    auto b = make_IStr();
    // Replace b's middle Interpolation with an InterpolatedStringText (a different content
    // element type at the same position).
    auto replacement = std::make_unique<InterpolatedStringText>(std::string("mid"));
    b->Content().SetAt(1, replacement.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_InterpolatedStringExpression, DoMatchEmptyMatchesEmpty) {
    InterpolatedStringExpression a;
    InterpolatedStringExpression b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_InterpolatedStringExpression, DoMatchRejectsDifferentType) {
    auto h = make_IStr();
    auto txt = std::make_unique<InterpolatedStringText>(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(h.get(), txt.get()));
}

TEST(CSharp_InterpolatedStringExpression, DoMatchRejectsNullCandidate) {
    auto h = make_IStr();
    EXPECT_FALSE(DoMatchAgainst(h.get(), nullptr));
}

TEST(CSharp_InterpolatedStringExpression, CloneDeepCopiesContent) {
    auto h = make_IStr();
    InterpolatedStringExpression* copy = h->Clone();
    EXPECT_EQ(copy->Content().Count(), h->Content().Count());
    ASSERT_EQ(copy->Content().Count(), 3);
    // Each element is a distinct deep copy (not the same pointer).
    for (int i = 0; i < 3; i++) {
        EXPECT_NE(copy->Content().At(i), h->Content().At(i));
        EXPECT_EQ(copy->Content().At(i)->Parent(), copy);
    }
    // The cloned Interpolation's Expression is itself deep-copied (distinct from the original's).
    auto* origInterp = dynamic_cast<Interpolation*>(h->Content().At(1));
    auto* copyInterp = dynamic_cast<Interpolation*>(copy->Content().At(1));
    ASSERT_NE(origInterp, nullptr);
    ASSERT_NE(copyInterp, nullptr);
    EXPECT_NE(copyInterp->Expression(), origInterp->Expression());
    EXPECT_EQ(copyInterp->Expression()->Parent(), copyInterp);
    delete copy;
}

TEST(CSharp_InterpolatedStringExpression, CloneEmpty) {
    InterpolatedStringExpression istr;
    InterpolatedStringExpression* copy = istr.Clone();
    EXPECT_EQ(copy->Content().Count(), 0);
    delete copy;
}

TEST(CSharp_InterpolatedStringExpression, CloneVirtualAndCovariant) {
    auto h = make_IStr();
    AstNode* asNode = h.get();
    AstNode* copy = asNode->Clone();
    EXPECT_NE(dynamic_cast<InterpolatedStringExpression*>(copy), nullptr);
    delete copy;
}

TEST(CSharp_InterpolatedStringExpression, CheckInvariantFilledAndEmptyPass) {
    auto h = make_IStr();
    h->CheckInvariant();  // collection-only: passes (no-op in NDEBUG)
    InterpolatedStringExpression empty;
    empty.CheckInvariant();
}
