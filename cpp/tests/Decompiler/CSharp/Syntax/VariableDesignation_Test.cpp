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

// Tests for the `VariableDesignation` hierarchy (cpp/.../Syntax/VariableDesignation.hpp +
// SingleVariableDesignation.hpp + ParenthesizedVariableDesignation.hpp, the ports of
// ICSharpCode.Decompiler/CSharp/Syntax/VariableDesignation.cs) -- the next in-order Phase-5 piece
// per the D263 plan (`ForeachStatement` needs the hierarchy). `VariableDesignation` is the
// abstract base of the C# 7 deconstruction designations; `SingleVariableDesignation` is the
// `single_variable_designation ::= identifier` leaf (a single REQUIRED `string Identifier`
// string-name `[Slot("Identifier")]` over a backing `IdentifierToken` -- the `LabelStatement` D259
// shape with a `VariableDesignation` base, plus the `SimpleType`/`IdentifierExpression`
// `Identifier` name-shadowing crux); `ParenthesizedVariableDesignation` is the
// `tuple_designation ::= '(' designations? ')'` node (a sealed `VariableDesignation` whose sole
// child slot is the `VariableDesignations` `AstNodeCollection<VariableDesignation>` collection --
// the `ArrayInitializerExpression` D250 / `BlockStatement` D256 collection-only shape with a
// `VariableDesignation` base, plus the new `Slots::VariableDesignation` kind). The suite shares a
// `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 multi-suite pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/ParenthesizedVariableDesignation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete designation types (and the backing `Identifier` token of the string-name leaf),
// recursing via the inherited `VisitChildren` (the document-order walk). The nodes these tests
// build are `SingleVariableDesignation`/`ParenthesizedVariableDesignation` and the backing
// `Identifier` token the leaf carries.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitSingleVariableDesignation(SingleVariableDesignation* node) override {
        if (node == nullptr) { trace.push_back("<null-single>"); return; }
        trace.push_back("single:" + node->Identifier());
        VisitChildren(node);
    }
    void VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation* node) override {
        if (node == nullptr) { trace.push_back("<null-paren>"); return; }
        trace.push_back("paren");
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
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
// VariableDesignation (the abstract base)
// ==========================================================================

// `VariableDesignation` is abstract (the C# `public abstract partial class`); a
// default-constructed `VariableDesignation` does not compile (the pure-virtual `Clone` plus the
// inherited pure-virtual `DoMatch`/`AcceptVisitor` keep it uninstantiable).
TEST(CSharp_VariableDesignation, IsAbstract) {
    EXPECT_TRUE(std::is_abstract_v<VariableDesignation>);
}

// `VariableDesignation` derives directly from `AstNode`; it is disjoint from both `Expression`
// and `Statement` (the D254 disjoint-hierarchy discriminator applied to the designation base:
// a designation is an `AstNode` but NOT an `Expression` nor a `Statement`).
TEST(CSharp_VariableDesignation, IsAstNodeNotExpressionNotStatement) {
    SingleVariableDesignation svd;
    EXPECT_NE(dynamic_cast<VariableDesignation*>(&svd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&svd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&svd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&svd), nullptr);

    ParenthesizedVariableDesignation pvd;
    EXPECT_NE(dynamic_cast<VariableDesignation*>(&pvd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&pvd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&pvd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&pvd), nullptr);
}

// ==========================================================================
// SingleVariableDesignation (the single-variable-designation leaf)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_SingleVariableDesignation, IsVariableDesignationAndAstNode) {
    SingleVariableDesignation svd;
    EXPECT_NE(dynamic_cast<VariableDesignation*>(&svd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&svd), nullptr);
}

// `SingleVariableDesignation` is a concrete (non-abstract) class and `final` (the C# `sealed`):
// no further derivation. It is constructible directly.
TEST(CSharp_SingleVariableDesignation, IsConcreteAndFinal) {
    auto svd = std::make_unique<SingleVariableDesignation>();
    ASSERT_NE(svd, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(svd.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<SingleVariableDesignation>);
}

// ---- Construction -------------------------------------------------------

// The empty ctor leaves the token absent (no name); the token is a REQUIRED slot, so the node is
// only valid until the name is set (a half-constructed node -- the `IdentifierExpression` D246 /
// `LabelStatement` D259 required-slot behavior). `GetChildCount` is 1 (the slot counts even when
// the token is absent -- no collection, no single-slot count term).
TEST(CSharp_SingleVariableDesignation, EmptyCtorLeavesTokenAbsent) {
    SingleVariableDesignation svd;
    EXPECT_EQ(svd.IdentifierToken(), nullptr);
    EXPECT_EQ(svd.GetChildCount(), 1);
    EXPECT_EQ(svd.StartLocation(), TextLocation::Empty);
}

// The (string) all-params ctor sets the name via the string setter, which creates the token via
// `Identifier::Create` (a non-nullable name -- the token carries the name even for an empty
// string, NOT a null token -- the `MemberType.MemberName` D238 / `LabelStatement.Label` D259
// precedent).
TEST(CSharp_SingleVariableDesignation, StringCtorSetsIdentifier) {
    SingleVariableDesignation svd(std::string("x"));
    EXPECT_EQ(svd.Identifier(), "x");
    EXPECT_NE(svd.IdentifierToken(), nullptr);
    EXPECT_EQ(svd.GetChildCount(), 1);
}

// ---- The Identifier / IdentifierToken accessors --------------------------

// `Identifier()` returns the token's `Name` (deref -- a null token would be UB); the string
// accessor is the NON-nullable `string` (the C# `string`, not `string?`), so the return is
// `std::string` (NOT `std::optional<std::string>` -- the `MemberType.MemberName` D238 /
// `LabelStatement.Label` D259 precedent).
TEST(CSharp_SingleVariableDesignation, IdentifierReturnsTokenName) {
    SingleVariableDesignation svd(std::string("Qux"));
    EXPECT_EQ(svd.Identifier(), "Qux");
}

// The `Identifier(string)` setter creates the token via `Identifier::Create` (NOT
// `CreateIfNotEmpty`), so an empty name yields a token with an empty `Name` (NOT a null token --
// the non-nullable behaviour, faithful to the C# `string`).
TEST(CSharp_SingleVariableDesignation, IdentifierSetterCreatesTokenForEmptyName) {
    SingleVariableDesignation svd;
    svd.Identifier("");
    EXPECT_NE(svd.IdentifierToken(), nullptr);
    EXPECT_EQ(svd.Identifier(), "");
}

// The `IdentifierToken(Identifier*)` setter sets the backing token directly (the token IS the
// construction value); the `Identifier::Create` factory is used because the 2-arg
// `(name, location)` ctor is private (the `AttributeSection` D241 precedent).
TEST(CSharp_SingleVariableDesignation, IdentifierTokenSetterSetsTokenDirectly) {
    SingleVariableDesignation svd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("Foo", TextLocation(1, 1)));
    svd.IdentifierToken(tok.get());
    EXPECT_EQ(svd.IdentifierToken(), tok.get());
    EXPECT_EQ(svd.Identifier(), "Foo");
    EXPECT_EQ(tok->Parent(), &svd);
}

// ---- Slot storage -------------------------------------------------------

// `GetChildCount` is the constant 1 (the single `IdentifierToken` slot counts even when the
// token is absent); `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.
TEST(CSharp_SingleVariableDesignation, SlotStorageFlatSwitch) {
    SingleVariableDesignation svd(std::string("Bar"));
    EXPECT_EQ(svd.GetChildCount(), 1);
    EXPECT_EQ(svd.GetChild(0), svd.IdentifierToken());
    EXPECT_EQ(svd.GetChildSlotInfo(0), &svd.IdentifierTokenSlot);
    EXPECT_THROW(svd.GetChild(1), std::out_of_range);
    EXPECT_THROW(svd.GetChildSlotInfo(1), std::out_of_range);
    EXPECT_THROW(svd.SetChild(1, nullptr), std::out_of_range);
}

// `SetChild(0, ...)` replaces the token in place; `SetChild` with a non-`Identifier` node is a
// downcast (the slot routes by slot kind, so the element type is guaranteed).
TEST(CSharp_SingleVariableDesignation, SetChildReplacesToken) {
    SingleVariableDesignation svd(std::string("A"));
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("B"));
    svd.SetChild(0, tok.get());
    EXPECT_EQ(svd.IdentifierToken(), tok.get());
    EXPECT_EQ(svd.Identifier(), "B");
    EXPECT_EQ(tok->Parent(), &svd);
}

// ---- The shared Slots::Identifier kind identity -------------------------

// The `IdentifierTokenSlot` points at the shared `Slots::Identifier` kind (already ported by
// `SimpleType` D237 -- no new `Slots` constant); the per-node slot is required
// (`IsOptional=false`, the name is non-nullable).
TEST(CSharp_SingleVariableDesignation, SlotKindIsSharedIdentifier) {
    SingleVariableDesignation svd;
    EXPECT_EQ(svd.IdentifierTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_FALSE(svd.IdentifierTokenSlot.IsOptional());
    EXPECT_FALSE(svd.IdentifierTokenSlot.IsCollection());
}

// The `IsInstanceOfType` is-a cross-check: the `IdentifierTokenSlot` accepts an `Identifier`
// (the slot's element type) and rejects a non-`Identifier` (a `VariableDesignation` is not an
// `Identifier`).
TEST(CSharp_SingleVariableDesignation, IsInstanceOfTypeAcceptsIdentifier) {
    SingleVariableDesignation svd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("x"));
    EXPECT_TRUE(svd.IdentifierTokenSlot.IsInstanceOfType(tok.get()));
    SingleVariableDesignation other;
    EXPECT_FALSE(svd.IdentifierTokenSlot.IsInstanceOfType(&other));
}

// ---- AcceptVisitor dispatch --------------------------------------------

TEST(CSharp_SingleVariableDesignation, AcceptVisitorDispatch) {
    SingleVariableDesignation svd(std::string("y"));
    RecordingVisitor v;
    svd.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "single:y");
    EXPECT_EQ(v.trace[1], "id:y");  // the backing IdentifierToken is a visited child
}

// `AcceptVisitor` is virtual (dispatches through an `AstNode*`/`VariableDesignation*` too).
// A filled node recurses into its backing `IdentifierToken` (a real `[Slot]` child), so the
// trace is the designation then the token (2 entries).
TEST(CSharp_SingleVariableDesignation, AcceptVisitorVirtualThroughBase) {
    SingleVariableDesignation svd(std::string("z"));
    AstNode* node = &svd;
    VariableDesignation* vd = &svd;
    RecordingVisitor v1, v2;
    node->AcceptVisitor(v1);
    vd->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace.size(), 2u);
    EXPECT_EQ(v1.trace[0], "single:z");
    EXPECT_EQ(v1.trace[1], "id:z");
    EXPECT_EQ(v2.trace.size(), 2u);
    EXPECT_EQ(v2.trace[0], "single:z");
    EXPECT_EQ(v2.trace[1], "id:z");
}

// ---- Depth-first walk ---------------------------------------------------

// The backing `IdentifierToken` is a real `[Slot]` child at flattened index 0, so the depth-first
// walk visits it (the `MemberType` D238 / `LabelStatement` D259 backing-token-is-a-visited-child
// precedent -- a non-nullable string-name `[Slot]` node cannot use a token-less node for a
// visitor test since the visitor derefs the token).
TEST(CSharp_SingleVariableDesignation, DepthFirstWalkVisitsToken) {
    SingleVariableDesignation svd(std::string("Foo"));
    RecordingVisitor v;
    svd.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "single:Foo");
    EXPECT_EQ(v.trace[1], "id:Foo");
}

// ---- DoMatch ------------------------------------------------------------

TEST(CSharp_SingleVariableDesignation, DoMatchSameName) {
    SingleVariableDesignation a(std::string("x")), b(std::string("x"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_SingleVariableDesignation, DoMatchRejectsDifferentName) {
    SingleVariableDesignation a(std::string("x")), b(std::string("y"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The empty-name case is a REAL "" match (two empty names match; empty does not match non-empty)
// -- the non-nullable behaviour, NOT a nameless/nullopt case (a nameless node is a
// half-constructed node that would NRE in C#).
TEST(CSharp_SingleVariableDesignation, DoMatchEmptyNameIsRealEmptyMatch) {
    SingleVariableDesignation a(std::string("")), b(std::string(""));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));

    SingleVariableDesignation c(std::string("")), d(std::string("z"));
    EXPECT_FALSE(DoMatchAgainst(&c, &d));
    EXPECT_FALSE(DoMatchAgainst(&d, &c));
}

// The `$any$` wildcard in the pattern's `Identifier` matches any candidate name (the
// `MatchString` wildcard path -- the `Identifier` D227 / `LabelStatement` D259 precedent).
TEST(CSharp_SingleVariableDesignation, DoMatchAnyStringWildcard) {
    SingleVariableDesignation pattern;
    pattern.Identifier(std::string(Pattern::AnyString));
    SingleVariableDesignation candidate(std::string("anything"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candidate));
}

// A type-only mismatch (not a `SingleVariableDesignation`) rejects early (the `other is
// SingleVariableDesignation` gate), generalizing the D234 cross-sibling rejection to the
// designation hierarchy.
TEST(CSharp_SingleVariableDesignation, DoMatchRejectsParenthesizedCandidate) {
    SingleVariableDesignation a(std::string("x"));
    ParenthesizedVariableDesignation b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

TEST(CSharp_SingleVariableDesignation, DoMatchRejectsNullCandidate) {
    SingleVariableDesignation a(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

// `Clone` deep-copies the token (distinct from source) and re-parents it; the cloned token
// carries its own `Name`.
TEST(CSharp_SingleVariableDesignation, CloneDeepCopiesToken) {
    SingleVariableDesignation svd(std::string("Clonable"));
    auto copy = std::unique_ptr<SingleVariableDesignation>(svd.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->IdentifierToken(), svd.IdentifierToken());
    EXPECT_EQ(copy->Identifier(), "Clonable");
    EXPECT_EQ(copy->IdentifierToken()->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*`/`VariableDesignation*` and covariant through
// `SingleVariableDesignation*`/`VariableDesignation*`; it does not detach the source.
TEST(CSharp_SingleVariableDesignation, CloneVirtualAndCovariant) {
    SingleVariableDesignation svd(std::string("x"));
    AstNode* node = &svd;
    VariableDesignation* vd = &svd;
    auto copy1 = std::unique_ptr<AstNode>(node->Clone());
    auto copy2 = std::unique_ptr<VariableDesignation>(vd->Clone());
    EXPECT_NE(copy1, nullptr);
    EXPECT_NE(copy2, nullptr);
    EXPECT_NE(dynamic_cast<SingleVariableDesignation*>(copy1.get()), nullptr);
    EXPECT_NE(dynamic_cast<SingleVariableDesignation*>(copy2.get()), nullptr);
    // The source is unchanged (the clone does not detach it).
    EXPECT_NE(svd.IdentifierToken(), nullptr);
    EXPECT_EQ(svd.Identifier(), "x");
}

// ---- CheckInvariant -----------------------------------------------------

// The token is a REQUIRED slot, so `CheckInvariant` passes only on a FILLED node (the
// `IdentifierExpression` D246 / `LabelStatement` D259 required-slot behavior -- unlike
// `SimpleType` whose optional token passes on a nameless node). The assert is a debug no-op in
// `NDEBUG`, so the test just confirms the filled node does not fire.
TEST(CSharp_SingleVariableDesignation, CheckInvariantPassesOnFilledNode) {
    SingleVariableDesignation svd(std::string("ok"));
    svd.CheckInvariant();  // should not assert
}

// ==========================================================================
// ParenthesizedVariableDesignation (the tuple-designation collection node)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

TEST(CSharp_ParenthesizedVariableDesignation, IsVariableDesignationAndAstNode) {
    ParenthesizedVariableDesignation pvd;
    EXPECT_NE(dynamic_cast<VariableDesignation*>(&pvd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&pvd), nullptr);
}

// `ParenthesizedVariableDesignation` is concrete (non-abstract) and `final` (the C# `sealed`).
TEST(CSharp_ParenthesizedVariableDesignation, IsConcreteAndFinal) {
    auto pvd = std::make_unique<ParenthesizedVariableDesignation>();
    ASSERT_NE(pvd, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(pvd.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<ParenthesizedVariableDesignation>);
}

// ---- Construction -------------------------------------------------------

// The empty ctor has no designations. `GetChildCount` is 0 (the node has no single slots -- the
// only slot is the `VariableDesignations` collection, which is empty -- the collection-only
// shape, like `ArrayInitializerExpression`/`BlockStatement`).
TEST(CSharp_ParenthesizedVariableDesignation, EmptyCtorHasNoDesignations) {
    ParenthesizedVariableDesignation pvd;
    EXPECT_EQ(pvd.VariableDesignations().Count(), 0);
    EXPECT_EQ(pvd.GetChildCount(), 0);
    EXPECT_EQ(pvd.StartLocation(), TextLocation::Empty);
}

// ---- The VariableDesignations collection ---------------------------------

// `Add` appends a designation, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last/only slot, so an element's index is
// its local position -- baseIndex 0).
TEST(CSharp_ParenthesizedVariableDesignation, AddAppendsAndParentsIncremental) {
    ParenthesizedVariableDesignation pvd;
    auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
    auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
    pvd.VariableDesignations().Add(a.get());
    pvd.VariableDesignations().Add(b.get());
    EXPECT_EQ(pvd.VariableDesignations().Count(), 2);
    EXPECT_EQ(a->Parent(), &pvd);
    EXPECT_EQ(b->Parent(), &pvd);
    EXPECT_EQ(a->ChildIndex, 0);  // baseIndex 0 + 0
    EXPECT_EQ(b->ChildIndex, 1);  // baseIndex 0 + 1
    EXPECT_EQ(pvd.GetChildCount(), 2);
    EXPECT_TRUE(pvd.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `VariableDesignations` collection for the
// `VariableDesignation` kind (the new shared `Slots::VariableDesignation`) and null for any
// other kind.
TEST(CSharp_ParenthesizedVariableDesignation, GetCollectionByKindReturnsForVariableDesignationKind) {
    ParenthesizedVariableDesignation pvd;
    EXPECT_NE(pvd.GetCollectionByKind(&Slots::VariableDesignation), nullptr);
    EXPECT_EQ(pvd.GetCollectionByKind(&Slots::VariableDesignation), &pvd.VariableDesignations());
    EXPECT_EQ(pvd.GetCollectionByKind(&Slots::Expression), nullptr);  // a different kind
    EXPECT_EQ(pvd.GetCollectionByKind(&Slots::Statement), nullptr);   // a different kind
    EXPECT_EQ(pvd.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot storage -------------------------------------------------------

// The collection-only slot-storage: `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single
// collection slot with no single step; an empty node reports `GetChildCount` 0 and `GetChild`
// throws out-of-range on any index.
TEST(CSharp_ParenthesizedVariableDesignation, SlotStorageCollectionOnly) {
    ParenthesizedVariableDesignation pvd;
    EXPECT_EQ(pvd.GetChildCount(), 0);
    EXPECT_THROW(pvd.GetChild(0), std::out_of_range);
    EXPECT_THROW(pvd.GetChildSlotInfo(0), std::out_of_range);
    EXPECT_THROW(pvd.SetChild(0, nullptr), std::out_of_range);
}

// `GetChild`/`GetChildSlotInfo` over a filled node walk the collection.
TEST(CSharp_ParenthesizedVariableDesignation, GetChildWalksCollection) {
    ParenthesizedVariableDesignation pvd;
    auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
    auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
    pvd.VariableDesignations().Add(a.get());
    pvd.VariableDesignations().Add(b.get());
    EXPECT_EQ(pvd.GetChild(0), a.get());
    EXPECT_EQ(pvd.GetChild(1), b.get());
    EXPECT_EQ(pvd.GetChildSlotInfo(0), &pvd.VariableDesignationsSlot);
    EXPECT_EQ(pvd.GetChildSlotInfo(1), &pvd.VariableDesignationsSlot);
    EXPECT_THROW(pvd.GetChild(2), std::out_of_range);
}

// `SetChild` with a collection node REPLACES an existing element in place (the collection must
// already have an element at the flattened index -- the `ArrayCreateExpression` D252 /
// `ForStatement` D263 precedent: it does NOT add a new one).
TEST(CSharp_ParenthesizedVariableDesignation, SetChildReplacesElementInPlace) {
    ParenthesizedVariableDesignation pvd;
    auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
    pvd.VariableDesignations().Add(a.get());
    auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
    pvd.SetChild(0, b.get());
    EXPECT_EQ(pvd.GetChild(0), b.get());
    EXPECT_EQ(b->Parent(), &pvd);
    EXPECT_EQ(pvd.VariableDesignations().Count(), 1);  // not grown
}

// ---- The shared Slots::VariableDesignation kind identity ----------------

// The `VariableDesignationsSlot` points at the shared `Slots::VariableDesignation` kind (added
// this iteration); the per-node slot is a collection.
TEST(CSharp_ParenthesizedVariableDesignation, SlotKindIsSharedVariableDesignation) {
    ParenthesizedVariableDesignation pvd;
    EXPECT_EQ(pvd.VariableDesignationsSlot.Kind(), &Slots::VariableDesignation);
    EXPECT_TRUE(pvd.VariableDesignationsSlot.IsCollection());
}

// The `IsInstanceOfType` is-a cross-check: the `VariableDesignationsSlot` accepts a
// `VariableDesignation` (the slot's element type, the abstract base -- accepts any
// `VariableDesignation`-derived node) and rejects a non-`VariableDesignation` (an `Identifier`
// is not a `VariableDesignation`).
TEST(CSharp_ParenthesizedVariableDesignation, IsInstanceOfTypeAcceptsVariableDesignation) {
    ParenthesizedVariableDesignation pvd;
    auto svd = std::make_unique<SingleVariableDesignation>(std::string("x"));
    EXPECT_TRUE(pvd.VariableDesignationsSlot.IsInstanceOfType(svd.get()));
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("y"));
    EXPECT_FALSE(pvd.VariableDesignationsSlot.IsInstanceOfType(tok.get()));
}

// ---- AcceptVisitor dispatch --------------------------------------------

TEST(CSharp_ParenthesizedVariableDesignation, AcceptVisitorDispatch) {
    ParenthesizedVariableDesignation pvd;
    RecordingVisitor v;
    pvd.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "paren");
}

// `AcceptVisitor` is virtual (dispatches through an `AstNode*`/`VariableDesignation*` too).
TEST(CSharp_ParenthesizedVariableDesignation, AcceptVisitorVirtualThroughBase) {
    ParenthesizedVariableDesignation pvd;
    AstNode* node = &pvd;
    VariableDesignation* vd = &pvd;
    RecordingVisitor v1, v2;
    node->AcceptVisitor(v1);
    vd->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace.size(), 1u);
    EXPECT_EQ(v1.trace[0], "paren");
    EXPECT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "paren");
}

// ---- Depth-first walk ---------------------------------------------------

// The depth-first walk recurses into the nested designations in document order (the
// collection-only shape -- the node then each element).
TEST(CSharp_ParenthesizedVariableDesignation, DepthFirstWalkRecursesIntoDesignations) {
    ParenthesizedVariableDesignation pvd;
    auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
    auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
    pvd.VariableDesignations().Add(a.get());
    pvd.VariableDesignations().Add(b.get());
    RecordingVisitor v;
    pvd.AcceptVisitor(v);
    // paren, then each single designation (which itself recurses into its token).
    ASSERT_EQ(v.trace.size(), 5u);
    EXPECT_EQ(v.trace[0], "paren");
    EXPECT_EQ(v.trace[1], "single:a");
    EXPECT_EQ(v.trace[2], "id:a");
    EXPECT_EQ(v.trace[3], "single:b");
    EXPECT_EQ(v.trace[4], "id:b");
}

// The empty node records just itself (no children to walk).
TEST(CSharp_ParenthesizedVariableDesignation, DepthFirstWalkEmptyNode) {
    ParenthesizedVariableDesignation pvd;
    RecordingVisitor v;
    pvd.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "paren");
}

// ---- DoMatch ------------------------------------------------------------

TEST(CSharp_ParenthesizedVariableDesignation, DoMatchSameDesignations) {
    ParenthesizedVariableDesignation a, b;
    auto a1 = std::make_unique<SingleVariableDesignation>(std::string("x"));
    auto b1 = std::make_unique<SingleVariableDesignation>(std::string("x"));
    a.VariableDesignations().Add(a1.get());
    b.VariableDesignations().Add(b1.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ParenthesizedVariableDesignation, DoMatchEmptyMatchesEmpty) {
    ParenthesizedVariableDesignation a, b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_ParenthesizedVariableDesignation, DoMatchRejectsDifferentCount) {
    ParenthesizedVariableDesignation a, b;
    auto a1 = std::make_unique<SingleVariableDesignation>(std::string("x"));
    a.VariableDesignations().Add(a1.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

TEST(CSharp_ParenthesizedVariableDesignation, DoMatchRejectsDifferentDesignation) {
    ParenthesizedVariableDesignation a, b;
    auto a1 = std::make_unique<SingleVariableDesignation>(std::string("x"));
    auto b1 = std::make_unique<SingleVariableDesignation>(std::string("y"));
    a.VariableDesignations().Add(a1.get());
    b.VariableDesignations().Add(b1.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A type-only mismatch (not a `ParenthesizedVariableDesignation`) rejects early -- the
// cross-sibling rejection generalized to the designation hierarchy (a `SingleVariableDesignation`
// pattern vs a `ParenthesizedVariableDesignation` candidate and vice versa).
TEST(CSharp_ParenthesizedVariableDesignation, DoMatchRejectsSingleCandidate) {
    ParenthesizedVariableDesignation pvd;
    SingleVariableDesignation svd(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&pvd, &svd));
    EXPECT_FALSE(DoMatchAgainst(&svd, &pvd));
}

TEST(CSharp_ParenthesizedVariableDesignation, DoMatchRejectsNullCandidate) {
    ParenthesizedVariableDesignation pvd;
    EXPECT_FALSE(DoMatchAgainst(&pvd, nullptr));
}

// ---- Clone --------------------------------------------------------------

// `Clone` deep-copies the collection elements (distinct from source) and re-parents them; the
// cloned elements are distinct nodes.
TEST(CSharp_ParenthesizedVariableDesignation, CloneDeepCopiesDesignations) {
    ParenthesizedVariableDesignation pvd;
    auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
    auto b = std::make_unique<SingleVariableDesignation>(std::string("b"));
    pvd.VariableDesignations().Add(a.get());
    pvd.VariableDesignations().Add(b.get());
    auto copy = std::unique_ptr<ParenthesizedVariableDesignation>(pvd.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->VariableDesignations().Count(), 2);
    // The cloned elements are distinct from the source's.
    EXPECT_NE(copy->VariableDesignations().At(0), a.get());
    EXPECT_NE(copy->VariableDesignations().At(1), b.get());
    // The cloned elements carry the names and are re-parented to the clone.
    auto* c0 = dynamic_cast<SingleVariableDesignation*>(copy->VariableDesignations().At(0));
    auto* c1 = dynamic_cast<SingleVariableDesignation*>(copy->VariableDesignations().At(1));
    ASSERT_NE(c0, nullptr);
    ASSERT_NE(c1, nullptr);
    EXPECT_EQ(c0->Identifier(), "a");
    EXPECT_EQ(c1->Identifier(), "b");
    EXPECT_EQ(c0->Parent(), copy.get());
    EXPECT_EQ(c1->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*`/`VariableDesignation*` and covariant through
// `ParenthesizedVariableDesignation*`/`VariableDesignation*`; the empty clone has no children.
TEST(CSharp_ParenthesizedVariableDesignation, CloneVirtualCovariantAndEmpty) {
    ParenthesizedVariableDesignation pvd;
    AstNode* node = &pvd;
    VariableDesignation* vd = &pvd;
    auto copy1 = std::unique_ptr<AstNode>(node->Clone());
    auto copy2 = std::unique_ptr<VariableDesignation>(vd->Clone());
    EXPECT_NE(copy1, nullptr);
    EXPECT_NE(copy2, nullptr);
    EXPECT_NE(dynamic_cast<ParenthesizedVariableDesignation*>(copy1.get()), nullptr);
    EXPECT_NE(dynamic_cast<ParenthesizedVariableDesignation*>(copy2.get()), nullptr);
    auto* empty = dynamic_cast<ParenthesizedVariableDesignation*>(copy1.get());
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->VariableDesignations().Count(), 0);
}

// ---- CheckInvariant -----------------------------------------------------

// There are no required single slots (the only slot is the collection), so `CheckInvariant`
// passes on both the filled and the empty node (the `ArrayInitializerExpression` D250 /
// `BlockStatement` D256 collection-only precedent -- the empty node is invariant-valid).
TEST(CSharp_ParenthesizedVariableDesignation, CheckInvariantPassesOnEmptyAndFilled) {
    ParenthesizedVariableDesignation empty;
    empty.CheckInvariant();  // should not assert

    ParenthesizedVariableDesignation filled;
    auto a = std::make_unique<SingleVariableDesignation>(std::string("a"));
    filled.VariableDesignations().Add(a.get());
    filled.CheckInvariant();  // should not assert
}
