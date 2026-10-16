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

// Tests for the `VariableInitializer` concrete node (cpp/.../Syntax/VariableInitializer.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/VariableInitializer.cs) -- the next
// in-order Phase-5 piece per the D265 plan (the dependency of `FixedStatement.Variables` and
// `VariableDeclarationStatement.Variables`). The `variable_declarator ::= identifier ( '='
// expression )?` (C# grammar 15.5.1): a non-sealed `AstNode` with a REQUIRED `string Name`
// string-name `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot (the
// `LabelStatement` D259 / `SingleVariableDesignation` D264 non-nullable-string-name-`[Slot]`
// shape) plus a NULLABLE `Expression?` `Initializer` `[Slot("Expression")]` single slot (the
// `ReturnStatement` D255 nullable-`Expression?`-single-slot shape), the first ported `TypeMembers`
// node.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
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
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete node types (and the backing `Identifier` token of the string-name leaf and the
// `Initializer` operand), recursing via the inherited `VisitChildren` (the document-order walk).
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitVariableInitializer(VariableInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-varinit>"); return; }
        trace.push_back("varinit:" + node->Name());
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
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
// VariableInitializer (the variable_declarator node)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `VariableInitializer` derives directly from `AstNode`; it is disjoint from `Expression`,
// `Statement`, and `AstType` (the D254 disjoint-hierarchy discriminator applied to the
// `TypeMembers` base: a variable initializer is an `AstNode` but NOT an `Expression` nor a
// `Statement` nor an `AstType`).
TEST(CSharp_VariableInitializer, IsAstNodeNotExpressionNotStatementNotAstType) {
    VariableInitializer vi;
    EXPECT_NE(dynamic_cast<AstNode*>(&vi), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&vi), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&vi), nullptr);
}

// `VariableInitializer` is a concrete (non-abstract) class and NOT `final` (the C# is NOT
// `sealed` -- the `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a `PatternPlaceholder`
// deriving from it; the pattern placeholder is deferred, but the class stays non-`final` to match
// the C# -- the `ArrayInitializerExpression` D250 precedent). It is constructible directly.
TEST(CSharp_VariableInitializer, IsConcreteAndNotFinal) {
    auto vi = std::make_unique<VariableInitializer>();
    ASSERT_NE(vi, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(vi.get()), nullptr);
    EXPECT_FALSE(std::is_final_v<VariableInitializer>);
}

// ---- Construction -------------------------------------------------------

// The empty ctor leaves the `NameToken` absent (no name) and the `Initializer` null (no
// initializer); the token is a REQUIRED slot, so the node is only valid until the name is set
// (a half-constructed node -- the `IdentifierExpression` D246 / `LabelStatement` D259 required-slot
// behavior). `GetChildCount` is 2 (both slots count even when their children are absent).
TEST(CSharp_VariableInitializer, EmptyCtorLeavesTokenAndInitializerAbsent) {
    VariableInitializer vi;
    EXPECT_EQ(vi.NameToken(), nullptr);
    EXPECT_EQ(vi.Initializer(), nullptr);
    EXPECT_EQ(vi.GetChildCount(), 2);
    EXPECT_EQ(vi.StartLocation(), TextLocation::Empty);
}

// The (string) required-prefix ctor sets the name via the string setter, which creates the token
// via `Identifier::Create` (a non-nullable name -- the token carries the name even for an empty
// string, NOT a null token -- the `MemberType.MemberName` D238 / `LabelStatement.Label` D259
// precedent). The `Initializer` stays null (no initializer -- the bare `name;` form).
TEST(CSharp_VariableInitializer, StringCtorSetsName) {
    VariableInitializer vi(std::string("x"));
    EXPECT_EQ(vi.Name(), "x");
    EXPECT_NE(vi.NameToken(), nullptr);
    EXPECT_EQ(vi.Initializer(), nullptr);
    EXPECT_EQ(vi.GetChildCount(), 2);
}

// The (string, Expression*) all-params ctor chains to the (string) prefix then sets the
// `Initializer`; both the name and the initializer are set.
TEST(CSharp_VariableInitializer, AllParamsCtorSetsNameAndInitializer) {
    auto init = std::make_unique<NullReferenceExpression>();
    VariableInitializer vi(std::string("x"), init.get());
    EXPECT_EQ(vi.Name(), "x");
    EXPECT_NE(vi.NameToken(), nullptr);
    EXPECT_EQ(vi.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &vi);
    EXPECT_EQ(vi.GetChildCount(), 2);
}

// ---- The Name / NameToken accessors --------------------------

// `Name()` returns the token's `Name` (deref -- a null token would be UB); the string accessor is
// the NON-nullable `string` (the C# `string`, not `string?`), so the return is `std::string`
// (NOT `std::optional<std::string>` -- the `MemberType.MemberName` D238 / `LabelStatement.Label`
// D259 precedent).
TEST(CSharp_VariableInitializer, NameReturnsTokenName) {
    VariableInitializer vi(std::string("Qux"));
    EXPECT_EQ(vi.Name(), "Qux");
}

// The `Name(string)` setter creates the token via `Identifier::Create` (NOT `CreateIfNotEmpty`),
// so an empty name yields a token with an empty `Name` (NOT a null token -- the non-nullable
// behaviour, faithful to the C# `string`).
TEST(CSharp_VariableInitializer, NameSetterCreatesTokenForEmptyName) {
    VariableInitializer vi;
    vi.Name("");
    EXPECT_NE(vi.NameToken(), nullptr);
    EXPECT_EQ(vi.Name(), "");
}

// The `NameToken(Identifier*)` setter sets the backing token directly (the token IS the
// construction value); the `Identifier::Create` factory is used because the 2-arg
// `(name, location)` ctor is private (the `AttributeSection` D241 precedent).
TEST(CSharp_VariableInitializer, NameTokenSetterSetsTokenDirectly) {
    VariableInitializer vi;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("Foo", TextLocation(1, 1)));
    vi.NameToken(tok.get());
    EXPECT_EQ(vi.NameToken(), tok.get());
    EXPECT_EQ(vi.Name(), "Foo");
    EXPECT_EQ(tok->Parent(), &vi);
}

// ---- The Initializer accessor -------------------------------

// The `Initializer()` accessor returns the nullable `Expression` (null when absent -- the bare
// `name;` form); the `Initializer(Expression*)` setter parents and re-indexes the child.
TEST(CSharp_VariableInitializer, InitializerAccessor) {
    VariableInitializer vi(std::string("x"));
    EXPECT_EQ(vi.Initializer(), nullptr);
    auto init = std::make_unique<NullReferenceExpression>();
    vi.Initializer(init.get());
    EXPECT_EQ(vi.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &vi);
}

// ---- Slot storage -------------------------------------------------------

// `GetChildCount` is the constant 2 (both single slots count even when their children are absent);
// `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.
TEST(CSharp_VariableInitializer, SlotStorageFlatSwitch) {
    VariableInitializer vi(std::string("Bar"));
    EXPECT_EQ(vi.GetChildCount(), 2);
    EXPECT_EQ(vi.GetChild(0), vi.NameToken());
    EXPECT_EQ(vi.GetChild(1), vi.Initializer());
    EXPECT_EQ(vi.GetChildSlotInfo(0), &vi.NameTokenSlot);
    EXPECT_EQ(vi.GetChildSlotInfo(1), &vi.InitializerSlot);
    EXPECT_THROW(vi.GetChild(2), std::out_of_range);
    EXPECT_THROW(vi.GetChildSlotInfo(2), std::out_of_range);
    EXPECT_THROW(vi.SetChild(2, nullptr), std::out_of_range);
}

// `SetChild(0, ...)` replaces the `NameToken` in place; `SetChild(1, ...)` replaces the
// `Initializer` in place; each downcasts to the slot's element type (the slot routes by slot kind,
// so the element type is guaranteed).
TEST(CSharp_VariableInitializer, SetChildReplacesEachSlot) {
    VariableInitializer vi(std::string("A"));
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("B"));
    vi.SetChild(0, tok.get());
    EXPECT_EQ(vi.NameToken(), tok.get());
    EXPECT_EQ(vi.Name(), "B");
    EXPECT_EQ(tok->Parent(), &vi);

    auto init = std::make_unique<NullReferenceExpression>();
    vi.SetChild(1, init.get());
    EXPECT_EQ(vi.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &vi);
}

// ---- The shared Slots kind identity -------------------------

// The `NameTokenSlot` points at the shared `Slots::Identifier` kind (already ported by `SimpleType`
// D237 -- no new `Slots` constant); the per-node slot is required (`IsOptional=false`, the name is
// non-nullable). The `InitializerSlot` points at the shared `Slots::Expression` kind (already
// ported by `UnaryOperatorExpression` D231); the per-node slot is nullable (`IsOptional=true`).
TEST(CSharp_VariableInitializer, SlotKindsAreShared) {
    VariableInitializer vi;
    EXPECT_EQ(vi.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_FALSE(vi.NameTokenSlot.IsOptional());
    EXPECT_FALSE(vi.NameTokenSlot.IsCollection());
    EXPECT_EQ(vi.InitializerSlot.Kind(), &Slots::Expression);
    EXPECT_TRUE(vi.InitializerSlot.IsOptional());
    EXPECT_FALSE(vi.InitializerSlot.IsCollection());
}

// The slot statics are distinct (the `NameTokenSlot` and `InitializerSlot` are different
// `CSharpSlotInfoT<T>` instantiations of distinct element types, compared through the common
// `CSharpSlotInfo*` base -- the D251/D252 cross-element-type `EXPECT_NE` crux).
TEST(CSharp_VariableInitializer, SlotStaticsAreDistinct) {
    VariableInitializer vi;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&vi.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&vi.InitializerSlot));
}

// The `IsInstanceOfType` is-a cross-check: the `NameTokenSlot` accepts an `Identifier` (its
// element type) and rejects an `Expression`; the `InitializerSlot` accepts an `Expression` and
// rejects an `Identifier`.
TEST(CSharp_VariableInitializer, IsInstanceOfTypeAcceptsElementTypes) {
    VariableInitializer vi;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("x"));
    auto init = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(vi.NameTokenSlot.IsInstanceOfType(tok.get()));
    EXPECT_FALSE(vi.NameTokenSlot.IsInstanceOfType(init.get()));
    EXPECT_TRUE(vi.InitializerSlot.IsInstanceOfType(init.get()));
    EXPECT_FALSE(vi.InitializerSlot.IsInstanceOfType(tok.get()));
}

// ---- AcceptVisitor dispatch --------------------------------------------

TEST(CSharp_VariableInitializer, AcceptVisitorDispatch) {
    VariableInitializer vi(std::string("y"));
    RecordingVisitor v;
    vi.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "varinit:y");
    EXPECT_EQ(v.trace[1], "id:y");  // the backing NameToken is a visited child
}

// `AcceptVisitor` is virtual (dispatches through an `AstNode*` too). A filled node recurses into
// its backing `NameToken` (a real `[Slot]` child), so the trace is the node then the token
// (2 entries); the `Initializer` is absent here so the walk stops after the token.
TEST(CSharp_VariableInitializer, AcceptVisitorVirtualThroughBase) {
    VariableInitializer vi(std::string("z"));
    AstNode* node = &vi;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "varinit:z");
    EXPECT_EQ(v.trace[1], "id:z");
}

// ---- Depth-first walk ---------------------------------------------------

// The depth-first walk visits the `NameToken` then the `Initializer` in document order over a
// filled node (the backing `IdentifierToken` is a real `[Slot]` child at flattened index 0 -- the
// `MemberType` D238 / `LabelStatement` D259 / `SingleVariableDesignation` D264
// backing-token-is-a-visited-child precedent).
TEST(CSharp_VariableInitializer, DepthFirstWalkVisitsTokenThenInitializer) {
    VariableInitializer vi(std::string("Foo"));
    auto init = std::make_unique<NullReferenceExpression>();
    vi.Initializer(init.get());
    RecordingVisitor v;
    vi.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "varinit:Foo");
    EXPECT_EQ(v.trace[1], "id:Foo");
    EXPECT_EQ(v.trace[2], "nullref");
}

// The empty node (no `Initializer`) records the node then its backing `NameToken` (2 entries);
// the `Initializer` is absent so the walk stops after the token.
TEST(CSharp_VariableInitializer, DepthFirstWalkEmptyInitializer) {
    VariableInitializer vi(std::string("Bar"));
    RecordingVisitor v;
    vi.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 2u);
    EXPECT_EQ(v.trace[0], "varinit:Bar");
    EXPECT_EQ(v.trace[1], "id:Bar");
}

// ---- DoMatch ------------------------------------------------------------

TEST(CSharp_VariableInitializer, DoMatchSameName) {
    VariableInitializer a(std::string("x")), b(std::string("x"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_VariableInitializer, DoMatchRejectsDifferentName) {
    VariableInitializer a(std::string("x")), b(std::string("y"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The empty-name case is a REAL "" match (two empty names match; empty does not match non-empty)
// -- the non-nullable behaviour, NOT a nameless/nullopt case (a nameless node is a
// half-constructed node that would NRE in C#).
TEST(CSharp_VariableInitializer, DoMatchEmptyNameIsRealEmptyMatch) {
    VariableInitializer a(std::string("")), b(std::string(""));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));

    VariableInitializer c(std::string("")), d(std::string("z"));
    EXPECT_FALSE(DoMatchAgainst(&c, &d));
    EXPECT_FALSE(DoMatchAgainst(&d, &c));
}

// The `$any$` wildcard in the pattern's `Name` matches any candidate name (the `MatchString`
// wildcard path -- the `Identifier` D227 / `LabelStatement` D259 precedent).
TEST(CSharp_VariableInitializer, DoMatchAnyStringWildcard) {
    VariableInitializer pattern;
    pattern.Name(std::string(Pattern::AnyString));
    VariableInitializer candidate(std::string("anything"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candidate));
}

// The `Initializer` is a NULLABLE recursive child, so `MatchOptional` returns true when BOTH are
// absent (the common `name;` form with no initializer).
TEST(CSharp_VariableInitializer, DoMatchBothInitializerAbsent) {
    VariableInitializer a(std::string("x")), b(std::string("x"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Both `Initializer`s present: the pattern's `Initializer.DoMatch` decides (here both
// `NullReferenceExpression` -- same type, match).
TEST(CSharp_VariableInitializer, DoMatchBothInitializerPresent) {
    VariableInitializer a(std::string("x")), b(std::string("x"));
    auto ia = std::make_unique<NullReferenceExpression>();
    auto ib = std::make_unique<NullReferenceExpression>();
    a.Initializer(ia.get());
    b.Initializer(ib.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// One `Initializer` absent, one present: `MatchOptional` rejects (the asymmetry -- one side has an
// initializer, the other does not).
TEST(CSharp_VariableInitializer, DoMatchInitializerAsymmetryRejects) {
    VariableInitializer a(std::string("x")), b(std::string("x"));
    auto ia = std::make_unique<NullReferenceExpression>();
    a.Initializer(ia.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A type-only mismatch (not a `VariableInitializer`) rejects early (the `other is
// VariableInitializer` gate), generalizing the D234 cross-sibling rejection to the `TypeMembers`
// node.
TEST(CSharp_VariableInitializer, DoMatchRejectsNonVariableInitializerCandidate) {
    VariableInitializer a(std::string("x"));
    NullReferenceExpression b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

TEST(CSharp_VariableInitializer, DoMatchRejectsNullCandidate) {
    VariableInitializer a(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone --------------------------------------------------------------

// `Clone` deep-copies the `NameToken` (distinct from source) and re-parents it; the cloned token
// carries its own `Name`. The `Initializer` is deep-copied too (distinct from source, re-parented)
// when present.
TEST(CSharp_VariableInitializer, CloneDeepCopiesTokenAndInitializer) {
    VariableInitializer vi(std::string("Clonable"));
    auto init = std::make_unique<NullReferenceExpression>();
    vi.Initializer(init.get());
    auto copy = std::unique_ptr<VariableInitializer>(vi.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->NameToken(), vi.NameToken());
    EXPECT_EQ(copy->Name(), "Clonable");
    EXPECT_EQ(copy->NameToken()->Parent(), copy.get());
    EXPECT_NE(copy->Initializer(), init.get());
    EXPECT_NE(dynamic_cast<NullReferenceExpression*>(copy->Initializer()), nullptr);
    EXPECT_EQ(copy->Initializer()->Parent(), copy.get());
}

// `Clone` skips the absent `Initializer` (the bare `name;` form); the cloned node has no
// `Initializer`.
TEST(CSharp_VariableInitializer, CloneSkipsAbsentInitializer) {
    VariableInitializer vi(std::string("x"));
    auto copy = std::unique_ptr<VariableInitializer>(vi.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->NameToken(), nullptr);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// `Clone` is virtual through `AstNode*` and covariant through `VariableInitializer*`; it does not
// detach the source.
TEST(CSharp_VariableInitializer, CloneVirtualAndCovariant) {
    VariableInitializer vi(std::string("x"));
    AstNode* node = &vi;
    auto copy1 = std::unique_ptr<AstNode>(node->Clone());
    auto copy2 = std::unique_ptr<VariableInitializer>(vi.Clone());
    EXPECT_NE(copy1, nullptr);
    EXPECT_NE(copy2, nullptr);
    EXPECT_NE(dynamic_cast<VariableInitializer*>(copy1.get()), nullptr);
    // The source is unchanged (the clone does not detach it).
    EXPECT_NE(vi.NameToken(), nullptr);
    EXPECT_EQ(vi.Name(), "x");
}

// ---- CheckInvariant -----------------------------------------------------

// The `NameToken` is a REQUIRED slot, so `CheckInvariant` passes only on a node whose name is set
// (the `IdentifierExpression` D246 / `LabelStatement` D259 required-slot behavior). The
// `Initializer` is nullable so its absence is invariant-valid.
TEST(CSharp_VariableInitializer, CheckInvariantPassesOnFilledNode) {
    VariableInitializer vi(std::string("ok"));
    vi.CheckInvariant();  // should not assert
}

// A default-constructed (empty) node violates the required-`NameToken` invariant (the assert fires
// in debug -- the `LabelStatement` D259 / `ForeachStatement` D265 precedent).
TEST(CSharp_VariableInitializer, CheckInvariantRejectsEmptyNode) {
    VariableInitializer vi;
    EXPECT_DEBUG_DEATH(vi.CheckInvariant(), "");
}
