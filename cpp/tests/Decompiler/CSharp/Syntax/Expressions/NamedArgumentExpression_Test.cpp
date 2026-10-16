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
// OTHERWISE, ARISING FROM, CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the `NamedExpression` + `NamedArgumentExpression` sibling pair (cpp/.../Syntax/
// Expressions/NamedExpression.hpp, NamedArgumentExpression.hpp, the ports of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/NamedExpression.cs,
// NamedArgumentExpression.cs) -- the `FixedVariableInitializer` D286 shape (a required
// non-nullable `string Name` string-name `[Slot("Identifier")]` over a backing `NameToken`
// `Identifier` + a required `Expression` `[Slot("Expression")]` slot) applied to the
// `Expression` hierarchy, with the `Expression`-of-type-`Expression` name-shadowing crux (the
// `CastExpression` D243 precedent). The two are structural twins differing only in the concrete
// class name, the visit-method name, and the grammatical role of the `Expression` slot. Exercises
// the slot accessors/storage, the `AcceptVisitor` dispatch, the generated `DoMatch` (a
// `MatchString` on `Name` + a `MatchRequired` on `Expression`), the per-concrete-node `Clone`,
// and the inherited `CheckInvariant` (which rejects an empty node -- the `NameToken` is a
// required slot).

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
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete types, recursing via the inherited `VisitChildren` (the document-order walk).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitNamedExpression(NamedExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-named>"); return; }
        trace.push_back("named:" + node->Name());
        VisitChildren(node);
    }
    void VisitNamedArgumentExpression(NamedArgumentExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-namedarg>"); return; }
        trace.push_back("namedarg:" + node->Name());
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
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ===========================================================================
// NamedExpression
// ===========================================================================

// ---- Is-a --------------------------------------------------------------

// `NamedExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a); it is NOT an
// `AstType` (it derives from `Expression`, parallel to -- not under -- `AstType`).
TEST(CSharp_NamedExpression, IsExpressionAndAstNodeNotAstType) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    EXPECT_NE(dynamic_cast<Expression*>(&ne), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ne), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ne), nullptr);
}

// `NamedExpression` is a concrete (non-abstract) and `final` class (the C# is `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it).
TEST(CSharp_NamedExpression, IsConcreteAndFinal) {
    auto ne = std::make_unique<NamedExpression>();
    ASSERT_NE(ne, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(ne.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<NamedExpression>);
}

// ---- Construction -----------------------------------------------------

// The empty ctor leaves both slots null. `GetChildCount` is 2 (each slot counts even when its
// child is absent); the `Name` getter would deref a null token (a half-constructed node), so it
// is not called on an empty node.
TEST(CSharp_NamedExpression, EmptyCtorLeavesSlotsNull) {
    NamedExpression ne;
    EXPECT_EQ(ne.NameToken(), nullptr);
    EXPECT_EQ(ne.Expression(), nullptr);
    EXPECT_EQ(ne.GetChildCount(), 2);
    EXPECT_EQ(ne.StartLocation(), TextLocation::Empty);
}

// The all-params ctor sets the name (creating the token) and the expression.
TEST(CSharp_NamedExpression, AllParamsCtorSetsNameAndExpression) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    ASSERT_NE(ne.NameToken(), nullptr);
    EXPECT_EQ(ne.Name(), "x");
    EXPECT_EQ(ne.Expression(), value.get());
    EXPECT_EQ(value->Parent(), &ne);
    EXPECT_EQ(value->ChildIndex, 1);
    EXPECT_EQ(ne.NameToken()->Parent(), &ne);
    EXPECT_EQ(ne.NameToken()->ChildIndex, 0);
}

// ---- The `Name`/`NameToken` string-name accessors ---------------------

// The `Name` getter returns the token's name; the setter creates a fresh token via
// `Identifier::Create` (an empty name yields a token with an empty `Name`, not a null token --
// the non-nullable-string-name-`[Slot]` semantics).
TEST(CSharp_NamedExpression, NameSetterCreatesTokenEvenForEmpty) {
    NamedExpression ne;
    ne.Name(std::string(""));
    ASSERT_NE(ne.NameToken(), nullptr);
    EXPECT_EQ(ne.Name(), "");
    ne.Name(std::string("foo"));
    ASSERT_NE(ne.NameToken(), nullptr);
    EXPECT_EQ(ne.Name(), "foo");
}

// `NameToken` setter re-parents the token to this node at flattened index 0.
TEST(CSharp_NamedExpression, NameTokenSetterReparents) {
    NamedExpression ne;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("bar")));
    ne.NameToken(tok.get());
    EXPECT_EQ(ne.NameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &ne);
    EXPECT_EQ(tok->ChildIndex, 0);
}

// ---- The `Expression` slot --------------------------------------------

// The `Expression` setter re-parents the child to this node at flattened index 1.
TEST(CSharp_NamedExpression, ExpressionSetterReparents) {
    NamedExpression ne;
    ne.Name(std::string("x"));
    auto value = std::make_unique<PrimitiveExpression>(int32_t(2));
    ne.Expression(value.get());
    EXPECT_EQ(ne.Expression(), value.get());
    EXPECT_EQ(value->Parent(), &ne);
    EXPECT_EQ(value->ChildIndex, 1);
}

// ---- Slot-storage contract (the flat two-case switch) ----------------

// `GetChild` dispatches by the flat index: 0 -> `NameToken`, 1 -> `Expression`.
TEST(CSharp_NamedExpression, GetChildDispatchesByFlatIndex) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    EXPECT_EQ(ne.GetChild(0), ne.NameToken());
    EXPECT_EQ(ne.GetChild(1), value.get());
    EXPECT_THROW(ne.GetChild(2), std::out_of_range);
    EXPECT_THROW(ne.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot statics by the flat index.
TEST(CSharp_NamedExpression, GetChildSlotInfoDispatchesByFlatIndex) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    EXPECT_EQ(ne.GetChildSlotInfo(0), &NamedExpression::NameTokenSlot);
    EXPECT_EQ(ne.GetChildSlotInfo(1), &NamedExpression::ExpressionSlot);
    EXPECT_THROW(ne.GetChildSlotInfo(2), std::out_of_range);
}

// `SetChild` replaces each slot in place (re-parenting the new child, detaching the old).
TEST(CSharp_NamedExpression, SetChildReplacesEachSlot) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    auto replacement = std::make_unique<PrimitiveExpression>(int32_t(2));
    ne.SetChild(1, replacement.get());
    EXPECT_EQ(ne.Expression(), replacement.get());
    EXPECT_EQ(replacement->Parent(), &ne);
    EXPECT_EQ(value->Parent(), nullptr);  // detached by SetChildNode
    auto newTok = std::unique_ptr<Identifier>(Identifier::Create(std::string("y")));
    ne.SetChild(0, newTok.get());
    EXPECT_EQ(ne.NameToken(), newTok.get());
    EXPECT_EQ(ne.Name(), "y");
    EXPECT_EQ(newTok->Parent(), &ne);
}

// `GetCollectionByKind` returns null for every kind (the node has no collection slot).
TEST(CSharp_NamedExpression, GetCollectionByKindReturnsNull) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    EXPECT_EQ(ne.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(ne.GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(ne.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot identity ----------------------------------------------------

// The `NameTokenSlot` points at the shared `Slots::Identifier` kind; the `ExpressionSlot`
// points at the shared `Slots::Expression` kind.
TEST(CSharp_NamedExpression, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(NamedExpression::NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(NamedExpression::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(NamedExpression::NameTokenSlot.IsCollection());
    EXPECT_FALSE(NamedExpression::ExpressionSlot.IsCollection());
    EXPECT_FALSE(NamedExpression::NameTokenSlot.IsOptional());
    EXPECT_FALSE(NamedExpression::ExpressionSlot.IsOptional());
}

// The two slot statics are distinct addresses (distinct kinds).
TEST(CSharp_NamedExpression, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&NamedExpression::NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&NamedExpression::ExpressionSlot));
}

// ---- IsInstanceOfType (the slot kind is-a) ----------------------------

// The `NameTokenSlot` accepts an `Identifier`; the `ExpressionSlot` accepts an `Expression`.
TEST(CSharp_NamedExpression, SlotIsInstanceOfType) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    EXPECT_TRUE(NamedExpression::NameTokenSlot.IsInstanceOfType(id.get()));
    PrimitiveExpression prim(int32_t(1));
    EXPECT_TRUE(NamedExpression::ExpressionSlot.IsInstanceOfType(&prim));
    // A non-Expression AstNode is rejected by the ExpressionSlot.
    EXPECT_FALSE(NamedExpression::ExpressionSlot.IsInstanceOfType(id.get()));
    EXPECT_FALSE(NamedExpression::NameTokenSlot.IsInstanceOfType(&prim));
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitNamedExpression`; the depth-first walk visits the backing
// `NameToken` (a real `Identifier` child at index 0) then the `Expression` child (a
// `PrimitiveExpression` leaf), in document order.
TEST(CSharp_NamedExpression, AcceptVisitorDispatchesToVisitNamedExpression) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    RecordingVisitor v;
    ne.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"named:x", "id:x", "prim"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*`.
TEST(CSharp_NamedExpression, AcceptVisitorIsVirtualThroughBases) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    AstNode* asAst = &ne;
    Expression* asExpr = &ne;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"named:x", "id:x", "prim"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"named:x", "id:x", "prim"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits a nested `Expression` (an `IdentifierExpression` with its own
// backing token) after the `NameToken`, in document order.
TEST(CSharp_NamedExpression, DepthFirstWalkVisitsNestedExpression) {
    auto value = std::make_unique<IdentifierExpression>(std::string("y"));
    NamedExpression ne(std::string("x"), value.get());
    RecordingVisitor v;
    ne.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"named:x", "id:x", "idexpr:y", "id:y"}));
}

// ---- DoMatch (the generated pattern match) ----------------------------

// Two `NamedExpression`s with the same name and matching `Expression` match.
TEST(CSharp_NamedExpression, DoMatchMatchesSameNameAndExpression) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression a(std::string("x"), aVal.get());
    NamedExpression b(std::string("x"), bVal.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `NamedExpression`s with different names do not match (the `MatchString` on `Name` rejects).
TEST(CSharp_NamedExpression, DoMatchRejectsDifferentName) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression a(std::string("x"), aVal.get());
    NamedExpression b(std::string("y"), bVal.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `NamedExpression`s with the same name but a mismatched `Expression` do not match (the
// `MatchRequired` on `Expression` rejects).
TEST(CSharp_NamedExpression, DoMatchRejectsDifferentExpression) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(2));
    NamedExpression a(std::string("x"), aVal.get());
    NamedExpression b(std::string("x"), bVal.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The `$any$` wildcard in the pattern's `Name` matches any candidate name.
TEST(CSharp_NamedExpression, DoMatchAnyStringWildcardMatchesAnyName) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression a(std::string(Pattern::AnyString), aVal.get());
    NamedExpression b(std::string("whatever"), bVal.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `NamedExpression` does not match a `NamedArgumentExpression` candidate (the two are structural
// twins sharing the exact same shape but are distinct concrete types, so the pattern matcher's
// `other is NamedExpression` type-check gate rejects -- the cross-sibling DoMatch rejection).
TEST(CSharp_NamedExpression, DoMatchRejectsNamedArgumentExpressionSibling) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression a(std::string("x"), aVal.get());
    NamedArgumentExpression b(std::string("x"), bVal.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `NamedExpression` does not match a non-`NamedExpression` candidate (the type-check gate
// rejects an `IdentifierExpression`).
TEST(CSharp_NamedExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    IdentifierExpression idexpr(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&ne, &idexpr));
    EXPECT_FALSE(DoMatchAgainst(&idexpr, &ne));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_NamedExpression, DoMatchRejectsNullCandidate) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    EXPECT_FALSE(DoMatchAgainst(&ne, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the token and the expression, re-parents the clones, and detaches from
// the source.
TEST(CSharp_NamedExpression, CloneDeepCopiesTokenAndExpression) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression original(std::string("x"), value.get());

    std::unique_ptr<NamedExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    ASSERT_NE(copy->NameToken(), nullptr);
    EXPECT_NE(copy->NameToken(), original.NameToken());
    EXPECT_EQ(copy->Name(), "x");
    EXPECT_EQ(copy->NameToken()->Parent(), copy.get());
    EXPECT_NE(copy->Expression(), value.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Name(), "x");
    EXPECT_EQ(value->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` and covariant through an `Expression*`.
TEST(CSharp_NamedExpression, CloneIsVirtualAndCovariant) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression original(std::string("x"), value.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<NamedExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<NamedExpression*>(exprCopy.get()), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A filled `NamedExpression` passes the inherited `CheckInvariant`.
TEST(CSharp_NamedExpression, CheckInvariantPassesOnFilledNode) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedExpression ne(std::string("x"), value.get());
    ne.CheckInvariant();
}

// An EMPTY `NamedExpression` is REJECTED by `CheckInvariant` (the `NameToken` is a required
// slot, so the missing-token invariant fires in a debug build).
TEST(CSharp_NamedExpression, CheckInvariantRejectsEmptyNode) {
    NamedExpression ne;
    EXPECT_DEBUG_DEATH(ne.CheckInvariant(), "");
}

// ===========================================================================
// NamedArgumentExpression (structural twin of NamedExpression)
// ===========================================================================

// ---- Is-a --------------------------------------------------------------

TEST(CSharp_NamedArgumentExpression, IsExpressionAndAstNodeNotAstType) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    EXPECT_NE(dynamic_cast<Expression*>(&nae), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&nae), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&nae), nullptr);
}

TEST(CSharp_NamedArgumentExpression, IsConcreteAndFinal) {
    auto nae = std::make_unique<NamedArgumentExpression>();
    ASSERT_NE(nae, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(nae.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<NamedArgumentExpression>);
}

// ---- Construction -----------------------------------------------------

TEST(CSharp_NamedArgumentExpression, EmptyCtorLeavesSlotsNull) {
    NamedArgumentExpression nae;
    EXPECT_EQ(nae.NameToken(), nullptr);
    EXPECT_EQ(nae.Expression(), nullptr);
    EXPECT_EQ(nae.GetChildCount(), 2);
    EXPECT_EQ(nae.StartLocation(), TextLocation::Empty);
}

TEST(CSharp_NamedArgumentExpression, AllParamsCtorSetsNameAndExpression) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    ASSERT_NE(nae.NameToken(), nullptr);
    EXPECT_EQ(nae.Name(), "x");
    EXPECT_EQ(nae.Expression(), value.get());
    EXPECT_EQ(value->Parent(), &nae);
    EXPECT_EQ(value->ChildIndex, 1);
    EXPECT_EQ(nae.NameToken()->Parent(), &nae);
    EXPECT_EQ(nae.NameToken()->ChildIndex, 0);
}

// ---- The `Name`/`NameToken` string-name accessors ---------------------

TEST(CSharp_NamedArgumentExpression, NameSetterCreatesTokenEvenForEmpty) {
    NamedArgumentExpression nae;
    nae.Name(std::string(""));
    ASSERT_NE(nae.NameToken(), nullptr);
    EXPECT_EQ(nae.Name(), "");
    nae.Name(std::string("foo"));
    ASSERT_NE(nae.NameToken(), nullptr);
    EXPECT_EQ(nae.Name(), "foo");
}

TEST(CSharp_NamedArgumentExpression, NameTokenSetterReparents) {
    NamedArgumentExpression nae;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("bar")));
    nae.NameToken(tok.get());
    EXPECT_EQ(nae.NameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &nae);
    EXPECT_EQ(tok->ChildIndex, 0);
}

// ---- The `Expression` slot --------------------------------------------

TEST(CSharp_NamedArgumentExpression, ExpressionSetterReparents) {
    NamedArgumentExpression nae;
    nae.Name(std::string("x"));
    auto value = std::make_unique<PrimitiveExpression>(int32_t(2));
    nae.Expression(value.get());
    EXPECT_EQ(nae.Expression(), value.get());
    EXPECT_EQ(value->Parent(), &nae);
    EXPECT_EQ(value->ChildIndex, 1);
}

// ---- Slot-storage contract (the flat two-case switch) ----------------

TEST(CSharp_NamedArgumentExpression, GetChildDispatchesByFlatIndex) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    EXPECT_EQ(nae.GetChild(0), nae.NameToken());
    EXPECT_EQ(nae.GetChild(1), value.get());
    EXPECT_THROW(nae.GetChild(2), std::out_of_range);
}

TEST(CSharp_NamedArgumentExpression, GetChildSlotInfoDispatchesByFlatIndex) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    EXPECT_EQ(nae.GetChildSlotInfo(0), &NamedArgumentExpression::NameTokenSlot);
    EXPECT_EQ(nae.GetChildSlotInfo(1), &NamedArgumentExpression::ExpressionSlot);
    EXPECT_THROW(nae.GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_NamedArgumentExpression, SetChildReplacesEachSlot) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    auto replacement = std::make_unique<PrimitiveExpression>(int32_t(2));
    nae.SetChild(1, replacement.get());
    EXPECT_EQ(nae.Expression(), replacement.get());
    EXPECT_EQ(replacement->Parent(), &nae);
    EXPECT_EQ(value->Parent(), nullptr);
    auto newTok = std::unique_ptr<Identifier>(Identifier::Create(std::string("y")));
    nae.SetChild(0, newTok.get());
    EXPECT_EQ(nae.NameToken(), newTok.get());
    EXPECT_EQ(nae.Name(), "y");
    EXPECT_EQ(newTok->Parent(), &nae);
}

TEST(CSharp_NamedArgumentExpression, GetCollectionByKindReturnsNull) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    EXPECT_EQ(nae.GetCollectionByKind(&Slots::Expression), nullptr);
    EXPECT_EQ(nae.GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(nae.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot identity ----------------------------------------------------

TEST(CSharp_NamedArgumentExpression, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(NamedArgumentExpression::NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(NamedArgumentExpression::ExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(NamedArgumentExpression::NameTokenSlot.IsCollection());
    EXPECT_FALSE(NamedArgumentExpression::ExpressionSlot.IsCollection());
    EXPECT_FALSE(NamedArgumentExpression::NameTokenSlot.IsOptional());
    EXPECT_FALSE(NamedArgumentExpression::ExpressionSlot.IsOptional());
}

TEST(CSharp_NamedArgumentExpression, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&NamedArgumentExpression::NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&NamedArgumentExpression::ExpressionSlot));
}

// ---- IsInstanceOfType (the slot kind is-a) ----------------------------

TEST(CSharp_NamedArgumentExpression, SlotIsInstanceOfType) {
    auto id = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    EXPECT_TRUE(NamedArgumentExpression::NameTokenSlot.IsInstanceOfType(id.get()));
    PrimitiveExpression prim(int32_t(1));
    EXPECT_TRUE(NamedArgumentExpression::ExpressionSlot.IsInstanceOfType(&prim));
    EXPECT_FALSE(NamedArgumentExpression::ExpressionSlot.IsInstanceOfType(id.get()));
    EXPECT_FALSE(NamedArgumentExpression::NameTokenSlot.IsInstanceOfType(&prim));
}

// ---- AcceptVisitor dispatch ------------------------------------------

TEST(CSharp_NamedArgumentExpression, AcceptVisitorDispatchesToVisitNamedArgumentExpression) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    RecordingVisitor v;
    nae.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"namedarg:x", "id:x", "prim"}));
}

TEST(CSharp_NamedArgumentExpression, AcceptVisitorIsVirtualThroughBases) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    AstNode* asAst = &nae;
    Expression* asExpr = &nae;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"namedarg:x", "id:x", "prim"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"namedarg:x", "id:x", "prim"}));
}

// ---- Depth-first walk ------------------------------------------------

TEST(CSharp_NamedArgumentExpression, DepthFirstWalkVisitsNestedExpression) {
    auto value = std::make_unique<IdentifierExpression>(std::string("y"));
    NamedArgumentExpression nae(std::string("x"), value.get());
    RecordingVisitor v;
    nae.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"namedarg:x", "id:x", "idexpr:y", "id:y"}));
}

// ---- DoMatch (the generated pattern match) ----------------------------

TEST(CSharp_NamedArgumentExpression, DoMatchMatchesSameNameAndExpression) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression a(std::string("x"), aVal.get());
    NamedArgumentExpression b(std::string("x"), bVal.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_NamedArgumentExpression, DoMatchRejectsDifferentName) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression a(std::string("x"), aVal.get());
    NamedArgumentExpression b(std::string("y"), bVal.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_NamedArgumentExpression, DoMatchRejectsDifferentExpression) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(2));
    NamedArgumentExpression a(std::string("x"), aVal.get());
    NamedArgumentExpression b(std::string("x"), bVal.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_NamedArgumentExpression, DoMatchAnyStringWildcardMatchesAnyName) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression a(std::string(Pattern::AnyString), aVal.get());
    NamedArgumentExpression b(std::string("whatever"), bVal.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `NamedArgumentExpression` does not match a `NamedExpression` candidate (the cross-sibling
// DoMatch rejection -- the reverse of the `NamedExpression` direction).
TEST(CSharp_NamedArgumentExpression, DoMatchRejectsNamedExpressionSibling) {
    auto aVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto bVal = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression a(std::string("x"), aVal.get());
    NamedExpression b(std::string("x"), bVal.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_NamedArgumentExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    IdentifierExpression idexpr(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&nae, &idexpr));
    EXPECT_FALSE(DoMatchAgainst(&idexpr, &nae));
}

TEST(CSharp_NamedArgumentExpression, DoMatchRejectsNullCandidate) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    EXPECT_FALSE(DoMatchAgainst(&nae, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

TEST(CSharp_NamedArgumentExpression, CloneDeepCopiesTokenAndExpression) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression original(std::string("x"), value.get());

    std::unique_ptr<NamedArgumentExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);
    ASSERT_NE(copy->NameToken(), nullptr);
    EXPECT_NE(copy->NameToken(), original.NameToken());
    EXPECT_EQ(copy->Name(), "x");
    EXPECT_EQ(copy->NameToken()->Parent(), copy.get());
    EXPECT_NE(copy->Expression(), value.get());
    EXPECT_EQ(copy->Expression()->Parent(), copy.get());
    EXPECT_EQ(original.Name(), "x");
    EXPECT_EQ(value->Parent(), &original);
}

TEST(CSharp_NamedArgumentExpression, CloneIsVirtualAndCovariant) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression original(std::string("x"), value.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<NamedArgumentExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<NamedArgumentExpression*>(exprCopy.get()), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

TEST(CSharp_NamedArgumentExpression, CheckInvariantPassesOnFilledNode) {
    auto value = std::make_unique<PrimitiveExpression>(int32_t(1));
    NamedArgumentExpression nae(std::string("x"), value.get());
    nae.CheckInvariant();
}

TEST(CSharp_NamedArgumentExpression, CheckInvariantRejectsEmptyNode) {
    NamedArgumentExpression nae;
    EXPECT_DEBUG_DEATH(nae.CheckInvariant(), "");
}
