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

// Tests for the `FixedVariableInitializer` and `FixedFieldDeclaration` concrete nodes
// (cpp/.../Syntax/FixedVariableInitializer.hpp + cpp/.../Syntax/FixedFieldDeclaration.hpp, the
// ports of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/FixedVariableInitializer.cs and
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/FixedFieldDeclaration.cs) -- the next in-order
// Phase-5 piece per the D285 plan ("the remaining TypeMember hierarchy: FixedFieldDeclaration,
// FixedVariableInitializer; then the OutputVisitor"). The `FixedVariableInitializer` is the
// `fixed_size_buffer_declarator ::= identifier '[' expression ']'` element of a
// `FixedFieldDeclaration.Variables` collection (a direct-`AstNode` node with a required
// `NameToken` `Identifier` + a REQUIRED `CountExpression` `Expression`); the `FixedFieldDeclaration`
// is the `fixed_size_buffer_declaration` (a sealed `EntityDeclaration` with an `Attributes`
// collection + a required `ReturnType` + a `Variables` `FixedVariableInitializer` collection -- the
// `FieldDeclaration` D273 two-collection shape with the `Variables` element type changed). The two
// suites share a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/FixedFieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/FixedVariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the per-node `Visit` calls under test (plus the
// `Identifier`/`SimpleType`/`PrimitiveExpression`/`NullReferenceExpression` of the children, and
// the `AttributeSection`/`Attribute`/`WhileStatement`/`VariableInitializer` used for the
// depth-first walks and the cross-type DoMatch rejections), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitFixedVariableInitializer(FixedVariableInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-fvi>"); return; }
        trace.push_back("fvi:" + node->Name());
        VisitChildren(node);
    }
    void VisitFixedFieldDeclaration(FixedFieldDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-ffd>"); return; }
        trace.push_back("ffd");
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-simple>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or(""));
        VisitChildren(node);
    }
    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim");
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitAttributeSection(AttributeSection* node) override {
        if (node == nullptr) { trace.push_back("<null-attrsec>"); return; }
        trace.push_back("attrsec:" + node->AttributeTarget());
        VisitChildren(node);
    }
    void VisitAttribute(Attribute* node) override {
        if (node == nullptr) { trace.push_back("<null-attr>"); return; }
        trace.push_back("attr");
        VisitChildren(node);
    }
    void VisitVariableInitializer(VariableInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-varinit>"); return; }
        trace.push_back("varinit:" + node->Name());
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
// pattern).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping a `FixedVariableInitializer` and its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design). A `FixedVariableInitializer`
// with a `Name` `x` and a `CountExpression` `PrimitiveExpression` (the `int count` literal).
struct FviHolder {
    std::unique_ptr<FixedVariableInitializer> fvi;
    std::unique_ptr<PrimitiveExpression> count;
    FixedVariableInitializer* get() const { return fvi.get(); }
    FixedVariableInitializer* operator->() const { return fvi.get(); }
};

// Build a `FixedVariableInitializer` with a `Name` `x` and a `CountExpression`
// `PrimitiveExpression` holding int32_t 10 (the `fixed (int x[10])` form).
FviHolder make_FixedVariableInitializer() {
    FviHolder h;
    h.count = std::make_unique<PrimitiveExpression>(int32_t(10));
    h.fvi = std::make_unique<FixedVariableInitializer>(std::string("x"), h.count.get());
    return h;
}

// A holder keeping a `FixedFieldDeclaration` and all its children alive in the test scope. A
// `FixedFieldDeclaration` with a `ReturnType` `SimpleType` `int` and a single `Variables`
// `FixedVariableInitializer` `x` (and, for the attribute variant, an `Attributes` `AttributeSection`
// whose `Attributes` hold an `Attribute` whose `Type` is a `SimpleType` `Foo`).
struct FfdHolder {
    std::unique_ptr<FixedFieldDeclaration> ffd;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<FixedVariableInitializer> varX;
    std::unique_ptr<PrimitiveExpression> varCount;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    FixedFieldDeclaration* get() const { return ffd.get(); }
    FixedFieldDeclaration* operator->() const { return ffd.get(); }
};

// Build a `FixedFieldDeclaration` with a `ReturnType` `SimpleType` `int` and a single `Variable`
// `FixedVariableInitializer` `x[10]` (no attributes).
FfdHolder make_FixedFieldDeclaration() {
    FfdHolder h;
    h.ffd = std::make_unique<FixedFieldDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.ffd->ReturnType(h.returnType.get());
    h.varCount = std::make_unique<PrimitiveExpression>(int32_t(10));
    h.varX = std::make_unique<FixedVariableInitializer>(std::string("x"), h.varCount.get());
    h.ffd->Variables().Add(h.varX.get());
    return h;
}

// Build a `FixedFieldDeclaration` with a `ReturnType` `SimpleType` `int`, a single `Variable`
// `FixedVariableInitializer` `x[10]`, and an `Attributes` `AttributeSection` holding an
// `Attribute` whose `Type` is a `SimpleType` `Foo`.
FfdHolder make_FixedFieldDeclarationWithAttribute() {
    FfdHolder h;
    h.ffd = std::make_unique<FixedFieldDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.ffd->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.ffd->ReturnType(h.returnType.get());
    h.varCount = std::make_unique<PrimitiveExpression>(int32_t(10));
    h.varX = std::make_unique<FixedVariableInitializer>(std::string("x"), h.varCount.get());
    h.ffd->Variables().Add(h.varX.get());
    return h;
}

} // namespace

// ==========================================================================
// FixedVariableInitializer (the fixed_size_buffer_declarator node)
// ==========================================================================

// ---- is-a + final ---------------------------------------------------------

// `FixedVariableInitializer` derives directly from `AstNode`; it is disjoint from `Expression`,
// `Statement`, `AstType`, and `EntityDeclaration`.
TEST(CSharp_FixedVariableInitializer, IsAstNodeNotExpressionNotStatementNotAstType) {
    FixedVariableInitializer fvi;
    EXPECT_NE(dynamic_cast<AstNode*>(&fvi), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&fvi), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&fvi), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&fvi), nullptr);
    EXPECT_EQ(dynamic_cast<EntityDeclaration*>(&fvi), nullptr);
}

// `FixedVariableInitializer` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_FixedVariableInitializer, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<FixedVariableInitializer>);
}

// ---- Construction --------------------------------------------------------

// The empty ctor leaves both `NameToken` and `CountExpression` absent; both are REQUIRED slots,
// so the node is only valid until the name and count are set (the `UnaryOperatorExpression` D231
// required-slot behavior). `GetChildCount` is 2 (both slots count even when their children are
// absent).
TEST(CSharp_FixedVariableInitializer, EmptyCtorLeavesTokenAndCountAbsent) {
    FixedVariableInitializer fvi;
    EXPECT_EQ(fvi.NameToken(), nullptr);
    EXPECT_EQ(fvi.CountExpression(), nullptr);
    EXPECT_EQ(fvi.GetChildCount(), 2);
    EXPECT_EQ(fvi.StartLocation(), TextLocation::Empty);
}

// The (string, Expression*) all-params ctor sets the name via the string setter (which creates
// the token via `Identifier::Create`) and the `CountExpression`; both are set and parented.
TEST(CSharp_FixedVariableInitializer, AllParamsCtorSetsNameAndCount) {
    auto count = std::make_unique<PrimitiveExpression>(int32_t(10));
    FixedVariableInitializer fvi(std::string("x"), count.get());
    EXPECT_EQ(fvi.Name(), "x");
    EXPECT_NE(fvi.NameToken(), nullptr);
    EXPECT_EQ(fvi.CountExpression(), count.get());
    EXPECT_EQ(count->Parent(), &fvi);
    EXPECT_EQ(fvi.GetChildCount(), 2);
}

// ---- The Name / NameToken accessors --------------------------

// `Name()` returns the token's `Name` (deref); the string accessor is NON-nullable (`std::string`).
TEST(CSharp_FixedVariableInitializer, NameReturnsTokenName) {
    FixedVariableInitializer fvi;
    fvi.Name(std::string("Buf"));
    EXPECT_EQ(fvi.Name(), "Buf");
}

// The `Name(string)` setter creates the token via `Identifier::Create` (NOT `CreateIfNotEmpty`),
// so an empty name yields a token with an empty `Name` (NOT a null token).
TEST(CSharp_FixedVariableInitializer, NameSetterCreatesTokenForEmptyName) {
    FixedVariableInitializer fvi;
    fvi.Name("");
    EXPECT_NE(fvi.NameToken(), nullptr);
    EXPECT_EQ(fvi.Name(), "");
}

// The `NameToken(Identifier*)` setter sets the backing token directly.
TEST(CSharp_FixedVariableInitializer, NameTokenSetterSetsTokenDirectly) {
    FixedVariableInitializer fvi;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("Foo", TextLocation(1, 1)));
    fvi.NameToken(tok.get());
    EXPECT_EQ(fvi.NameToken(), tok.get());
    EXPECT_EQ(fvi.Name(), "Foo");
    EXPECT_EQ(tok->Parent(), &fvi);
}

// ---- The CountExpression accessor ----------------------------

// The `CountExpression()` accessor returns the required `Expression` (null when absent -- a
// half-constructed node); the `CountExpression(Expression*)` setter parents and re-indexes the
// child.
TEST(CSharp_FixedVariableInitializer, CountExpressionAccessor) {
    FixedVariableInitializer fvi;
    fvi.Name(std::string("x"));
    EXPECT_EQ(fvi.CountExpression(), nullptr);
    auto count = std::make_unique<PrimitiveExpression>(int32_t(10));
    fvi.CountExpression(count.get());
    EXPECT_EQ(fvi.CountExpression(), count.get());
    EXPECT_EQ(count->Parent(), &fvi);
}

// ---- Slot storage -------------------------------------------------------

// `GetChildCount` is the constant 2; `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index
// switch.
TEST(CSharp_FixedVariableInitializer, SlotStorageFlatSwitch) {
    FixedVariableInitializer fvi;
    fvi.Name(std::string("Bar"));
    EXPECT_EQ(fvi.GetChildCount(), 2);
    EXPECT_EQ(fvi.GetChild(0), fvi.NameToken());
    EXPECT_EQ(fvi.GetChild(1), fvi.CountExpression());
    EXPECT_EQ(fvi.GetChildSlotInfo(0), &fvi.NameTokenSlot);
    EXPECT_EQ(fvi.GetChildSlotInfo(1), &fvi.CountExpressionSlot);
    EXPECT_THROW(fvi.GetChild(2), std::out_of_range);
    EXPECT_THROW(fvi.GetChildSlotInfo(2), std::out_of_range);
    EXPECT_THROW(fvi.SetChild(2, nullptr), std::out_of_range);
}

// `SetChild(0, ...)` replaces the `NameToken`; `SetChild(1, ...)` replaces the `CountExpression`.
TEST(CSharp_FixedVariableInitializer, SetChildReplacesEachSlot) {
    FixedVariableInitializer fvi;
    fvi.Name(std::string("A"));
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("B"));
    fvi.SetChild(0, tok.get());
    EXPECT_EQ(fvi.NameToken(), tok.get());
    EXPECT_EQ(fvi.Name(), "B");
    EXPECT_EQ(tok->Parent(), &fvi);

    auto count = std::make_unique<PrimitiveExpression>(int32_t(5));
    fvi.SetChild(1, count.get());
    EXPECT_EQ(fvi.CountExpression(), count.get());
    EXPECT_EQ(count->Parent(), &fvi);
}

// ---- The shared Slots kind identity -------------------------

// The `NameTokenSlot` points at the shared `Slots::Identifier` kind; the `CountExpressionSlot`
// points at the shared `Slots::Expression` kind. Both per-node slots are required
// (`IsOptional=false`).
TEST(CSharp_FixedVariableInitializer, SlotKindsAreShared) {
    FixedVariableInitializer fvi;
    EXPECT_EQ(fvi.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_FALSE(fvi.NameTokenSlot.IsOptional());
    EXPECT_FALSE(fvi.NameTokenSlot.IsCollection());
    EXPECT_EQ(fvi.CountExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_FALSE(fvi.CountExpressionSlot.IsOptional());
    EXPECT_FALSE(fvi.CountExpressionSlot.IsCollection());
}

// The slot statics are distinct (compared through the common `CSharpSlotInfo*` base).
TEST(CSharp_FixedVariableInitializer, SlotStaticsAreDistinct) {
    FixedVariableInitializer fvi;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&fvi.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&fvi.CountExpressionSlot));
}

// The `IsInstanceOfType` is-a cross-check: the `NameTokenSlot` accepts an `Identifier` and rejects
// an `Expression`; the `CountExpressionSlot` accepts an `Expression` and rejects an `Identifier`.
TEST(CSharp_FixedVariableInitializer, IsInstanceOfTypeAcceptsElementTypes) {
    FixedVariableInitializer fvi;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create("x"));
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(fvi.NameTokenSlot.IsInstanceOfType(tok.get()));
    EXPECT_FALSE(fvi.NameTokenSlot.IsInstanceOfType(expr.get()));
    EXPECT_TRUE(fvi.CountExpressionSlot.IsInstanceOfType(expr.get()));
    EXPECT_FALSE(fvi.CountExpressionSlot.IsInstanceOfType(tok.get()));
}

// ---- AcceptVisitor dispatch --------------------------------------------

TEST(CSharp_FixedVariableInitializer, AcceptVisitorDispatch) {
    auto fvi = make_FixedVariableInitializer();
    RecordingVisitor v;
    fvi->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "fvi:x");
    EXPECT_EQ(v.trace[1], "id:x");  // the backing NameToken is a visited child
    EXPECT_EQ(v.trace[2], "prim");  // the CountExpression PrimitiveExpression (a leaf)
}

// `AcceptVisitor` is virtual (dispatches through an `AstNode*` too).
TEST(CSharp_FixedVariableInitializer, AcceptVisitorVirtualThroughBase) {
    auto fvi = make_FixedVariableInitializer();
    AstNode* node = fvi.get();
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "fvi:x");
}

// ---- Depth-first walk ---------------------------------------------------

// The depth-first walk visits the `NameToken` then the `CountExpression` in document order over a
// filled node (the backing `IdentifierToken` is a real `[Slot]` child at flattened index 0 -- the
// `MemberType` D238 / `LabelStatement` D259 / `VariableInitializer` D266
// backing-token-is-a-visited-child precedent).
TEST(CSharp_FixedVariableInitializer, DepthFirstWalkVisitsTokenThenCount) {
    auto fvi = make_FixedVariableInitializer();
    RecordingVisitor v;
    fvi->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 3u);
    EXPECT_EQ(v.trace[0], "fvi:x");
    EXPECT_EQ(v.trace[1], "id:x");
    EXPECT_EQ(v.trace[2], "prim");
}

// ---- DoMatch ------------------------------------------------------------

TEST(CSharp_FixedVariableInitializer, DoMatchSameNameAndCount) {
    auto a = make_FixedVariableInitializer();
    auto b = make_FixedVariableInitializer();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_FixedVariableInitializer, DoMatchRejectsDifferentName) {
    auto a = make_FixedVariableInitializer();  // name x
    FviHolder b;
    b.count = std::make_unique<PrimitiveExpression>(int32_t(10));
    b.fvi = std::make_unique<FixedVariableInitializer>(std::string("y"), b.count.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `CountExpression` mismatch rejects (the `MatchRequired` term -- both present and the
// `CountExpression.DoMatch` rejects on a value mismatch).
TEST(CSharp_FixedVariableInitializer, DoMatchCountMismatchRejects) {
    auto a = make_FixedVariableInitializer();  // count 10
    FviHolder b;
    b.count = std::make_unique<PrimitiveExpression>(int32_t(20));  // different count value
    b.fvi = std::make_unique<FixedVariableInitializer>(std::string("x"), b.count.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// The empty-name case is a REAL "" match (two empty names match; empty does not match non-empty).
TEST(CSharp_FixedVariableInitializer, DoMatchEmptyNameIsRealEmptyMatch) {
    auto countA = std::make_unique<PrimitiveExpression>(int32_t(10));
    auto countB = std::make_unique<PrimitiveExpression>(int32_t(10));
    FixedVariableInitializer a(std::string(""), countA.get());
    FixedVariableInitializer b(std::string(""), countB.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));

    auto countC = std::make_unique<PrimitiveExpression>(int32_t(10));
    auto countD = std::make_unique<PrimitiveExpression>(int32_t(10));
    FixedVariableInitializer c(std::string(""), countC.get());
    FixedVariableInitializer d(std::string("z"), countD.get());
    EXPECT_FALSE(DoMatchAgainst(&c, &d));
    EXPECT_FALSE(DoMatchAgainst(&d, &c));
}

// The `$any$` wildcard in the pattern's `Name` matches any candidate name (the `MatchString`
// wildcard path).
TEST(CSharp_FixedVariableInitializer, DoMatchAnyStringWildcard) {
    FixedVariableInitializer pattern;
    pattern.Name(std::string(Pattern::AnyString));
    // The CountExpression is required -- the pattern's CountExpression must be set too for the
    // MatchRequired term to accept (a null pattern CountExpression does not match a non-null
    // candidate -- the MatchRequired defensive guard).
    auto count = std::make_unique<PrimitiveExpression>(int32_t(10));
    pattern.CountExpression(count.get());
    auto candidate = make_FixedVariableInitializer();
    EXPECT_TRUE(DoMatchAgainst(&pattern, candidate.get()));
}

// A type-only mismatch (not a `FixedVariableInitializer`) rejects early (the `other is
// FixedVariableInitializer` gate).
TEST(CSharp_FixedVariableInitializer, DoMatchRejectsNonFixedVariableInitializer) {
    auto a = make_FixedVariableInitializer();
    NullReferenceExpression b;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
    EXPECT_FALSE(DoMatchAgainst(&b, a.get()));
}

// A cross-structural-twin rejection: a `FixedVariableInitializer` pattern vs a `VariableInitializer`
// candidate (and vice versa) -- the two share the non-nullable-string-name-`[Slot]` shape but are
// distinct concrete types (the `FixedVariableInitializer` has a required `CountExpression`, the
// `VariableInitializer` has a nullable `Initializer`), so the type-check gate rejects.
TEST(CSharp_FixedVariableInitializer, DoMatchRejectsVariableInitializer) {
    auto a = make_FixedVariableInitializer();
    VariableInitializer b(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
    EXPECT_FALSE(DoMatchAgainst(&b, a.get()));
}

TEST(CSharp_FixedVariableInitializer, DoMatchRejectsNullCandidate) {
    auto a = make_FixedVariableInitializer();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone --------------------------------------------------------------

// `Clone` deep-copies the `NameToken` and `CountExpression` (distinct from source, re-parented);
// it does not detach the source.
TEST(CSharp_FixedVariableInitializer, CloneDeepCopiesTokenAndCount) {
    auto fvi = make_FixedVariableInitializer();
    auto copy = std::unique_ptr<FixedVariableInitializer>(fvi->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy->NameToken(), fvi->NameToken());
    EXPECT_EQ(copy->Name(), "x");
    EXPECT_EQ(copy->NameToken()->Parent(), copy.get());
    EXPECT_NE(copy->CountExpression(), fvi->CountExpression());
    EXPECT_NE(dynamic_cast<PrimitiveExpression*>(copy->CountExpression()), nullptr);
    EXPECT_EQ(copy->CountExpression()->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `FixedVariableInitializer*`.
TEST(CSharp_FixedVariableInitializer, CloneVirtualAndCovariant) {
    auto fvi = make_FixedVariableInitializer();
    AstNode* node = fvi.get();
    auto copy1 = std::unique_ptr<AstNode>(node->Clone());
    auto copy2 = std::unique_ptr<FixedVariableInitializer>(fvi->Clone());
    EXPECT_NE(copy1, nullptr);
    EXPECT_NE(copy2, nullptr);
    EXPECT_NE(dynamic_cast<FixedVariableInitializer*>(copy1.get()), nullptr);
    // The source is not detached.
    EXPECT_NE(fvi->NameToken(), nullptr);
    EXPECT_NE(fvi->CountExpression(), nullptr);
}

// ---- CheckInvariant -----------------------------------------------------

// `CheckInvariant` passes on a filled node (both required slots filled).
TEST(CSharp_FixedVariableInitializer, CheckInvariantPassesOnFilledNode) {
    auto fvi = make_FixedVariableInitializer();
    fvi->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NameToken` is a REQUIRED slot -- the assert
// fires in debug).
#ifndef NDEBUG
TEST(CSharp_FixedVariableInitializer, CheckInvariantRejectsEmpty) {
    FixedVariableInitializer fvi;  // no NameToken, no CountExpression
    EXPECT_DEATH(fvi.CheckInvariant(), "");
}
#endif

// ==========================================================================
// FixedFieldDeclaration (the fixed_size_buffer_declaration node)
// ==========================================================================

// ---- is-a + final ---------------------------------------------------------

TEST(CSharp_FixedFieldDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<FixedFieldDeclaration>);
}

// `FixedFieldDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_FixedFieldDeclaration, IsEntityDeclarationAndAstNode) {
    FixedFieldDeclaration ffd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&ffd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ffd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ffd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ffd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ffd), nullptr);
}

// ---- Construction + const + SymbolKind -----------------------------------

// The empty ctor: no `ReturnType`, empty `Attributes`/`Variables`. `GetChildCount` is 1 (the one
// `ReturnType` single slot; both collections are empty). `Modifiers` defaults to `None`;
// `SymbolKind` is `Field` (a fixed buffer is a field-kind member).
TEST(CSharp_FixedFieldDeclaration, EmptyCtor) {
    FixedFieldDeclaration ffd;
    EXPECT_EQ(ffd.SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(ffd.Modifiers(), Modifiers::None);
    EXPECT_EQ(ffd.ReturnType(), nullptr);
    EXPECT_EQ(ffd.Attributes().Count(), 0);
    EXPECT_EQ(ffd.Variables().Count(), 0);
    EXPECT_EQ(ffd.GetChildCount(), 1);  // 0 attrs + 1 ReturnType + 0 variables
}

// The `FixedKeyword` const string is "fixed" (the output-visitor keyword token).
TEST(CSharp_FixedFieldDeclaration, FixedKeywordConst) {
    EXPECT_STREQ(FixedFieldDeclaration::FixedKeyword(), "fixed");
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_FixedFieldDeclaration, SymbolKindOverrideReturnsField) {
    FixedFieldDeclaration ffd;
    EXPECT_EQ(ffd.SymbolKind(), SymbolKind::Field);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_FixedFieldDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    FixedFieldDeclaration ffd;
    EXPECT_EQ(ffd.Modifiers(), Modifiers::None);
    ffd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(ffd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_FixedFieldDeclaration, HasModifierIsBitmaskTest) {
    FixedFieldDeclaration ffd;
    ffd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(ffd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(ffd.HasModifier(Modifiers::Public));
    EXPECT_FALSE(ffd.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken (NOT overridden; inherited base returns empty/null) ----

// `FixedFieldDeclaration` does NOT override `Name`/`NameToken` (unlike `FieldDeclaration` D273
// which throws); the inherited base kind-walk finds no `NameToken` slot, so `Name()` returns the
// empty string and `NameToken()` returns null.
TEST(CSharp_FixedFieldDeclaration, NameAndNameTokenAreInheritedEmpty) {
    FixedFieldDeclaration ffd;
    EXPECT_EQ(ffd.Name(), "");
    EXPECT_EQ(ffd.NameToken(), nullptr);
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

// The `ReturnType` setter re-parents the type. After the reindex (triggered by `Slot()`) the
// `ReturnType`'s flattened `ChildIndex` is `attrCount` (0 with no attributes).
TEST(CSharp_FixedFieldDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    FixedFieldDeclaration ffd;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ffd.ReturnType(type.get());
    EXPECT_EQ(ffd.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &ffd);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_FixedFieldDeclaration, AttributesCollectionAddReparents) {
    FixedFieldDeclaration ffd;
    auto attrSec = std::make_unique<AttributeSection>();
    ffd.Attributes().Add(attrSec.get());
    EXPECT_EQ(ffd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &ffd);
}

// ---- The Variables collection (non-incremental) --------------------------

// `Variables().Add(...)` appends and parents; the collection is NON-incremental (two collections),
// so `Add` invalidates the parent's indices. After the reindex the `Variables[0]`'s flattened
// `ChildIndex` is `attrCount + 1` (1 with no attributes).
TEST(CSharp_FixedFieldDeclaration, VariablesCollectionAddReparentsAndReindexes) {
    FixedFieldDeclaration ffd;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ffd.ReturnType(type.get());
    auto count = std::make_unique<PrimitiveExpression>(int32_t(10));
    auto x = std::make_unique<FixedVariableInitializer>(std::string("x"), count.get());
    ffd.Variables().Add(x.get());
    EXPECT_EQ(ffd.Variables().Count(), 1);
    EXPECT_EQ(x->Parent(), &ffd);
    (void)x->Slot();  // trigger the lazy reindex
    EXPECT_EQ(x->ChildIndex, 1);  // attrCount (0) + 1 ReturnType -- Variables[0] at index 1
}

// ---- Slot storage (the generated overrides) ------------------------------

TEST(CSharp_FixedFieldDeclaration, GetChildWalksSlots) {
    auto ffd = make_FixedFieldDeclaration();
    EXPECT_EQ(ffd->GetChildCount(), 2);  // 0 attrs + ReturnType + 1 variable
    EXPECT_EQ(ffd->GetChild(0), ffd->ReturnType());
    EXPECT_EQ(ffd->GetChild(1), ffd->Variables().At(0));
    EXPECT_THROW(ffd->GetChild(2), std::out_of_range);
}

TEST(CSharp_FixedFieldDeclaration, GetChildSlotInfoWalksSlots) {
    auto ffd = make_FixedFieldDeclaration();
    EXPECT_EQ(ffd->GetChildSlotInfo(0), &ffd->ReturnTypeSlot);
    EXPECT_EQ(ffd->GetChildSlotInfo(1), &ffd->VariablesSlot);
    EXPECT_THROW(ffd->GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_FixedFieldDeclaration, GetCollectionByKindReturnsCollections) {
    FixedFieldDeclaration ffd;
    EXPECT_EQ(ffd.GetCollectionByKind(&Slots::AttributeSection), &ffd.Attributes());
    EXPECT_EQ(ffd.GetCollectionByKind(&Slots::FixedVariable), &ffd.Variables());
    EXPECT_EQ(ffd.GetCollectionByKind(&Slots::Type), nullptr);
    // The FixedVariable kind is DISTINCT from the Variable kind (VariableInitializer is a different
    // element type -- a FixedFieldDeclaration has no VariableInitializer collection).
    EXPECT_EQ(ffd.GetCollectionByKind(&Slots::Variable), nullptr);
}

TEST(CSharp_FixedFieldDeclaration, SetChildReplacesReturnType) {
    auto ffd = make_FixedFieldDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    auto* oldType = ffd->ReturnType();
    ffd->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(ffd->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

TEST(CSharp_FixedFieldDeclaration, SetChildReplacesVariable) {
    auto ffd = make_FixedFieldDeclaration();
    auto count2 = std::make_unique<PrimitiveExpression>(int32_t(20));
    auto y = std::make_unique<FixedVariableInitializer>(std::string("y"), count2.get());
    auto* oldVar = ffd->Variables().At(0);
    ffd->SetChild(1, y.get());  // Variables[0] at flattened index 1 (0 attrs + 1 ReturnType)
    EXPECT_EQ(ffd->Variables().At(0), y.get());
    EXPECT_EQ(oldVar->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_FixedFieldDeclaration, SlotStaticsPointAtSharedKinds) {
    FixedFieldDeclaration ffd;
    EXPECT_EQ(ffd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(ffd.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(ffd.VariablesSlot.Kind(), &Slots::FixedVariable);
}

TEST(CSharp_FixedFieldDeclaration, SlotStaticsAreDistinct) {
    FixedFieldDeclaration ffd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ffd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ffd.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ffd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ffd.VariablesSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ffd.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&ffd.VariablesSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_FixedFieldDeclaration, AcceptVisitorDispatches) {
    FixedFieldDeclaration ffd;
    RecordingVisitor v;
    ffd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ffd"}));
}

TEST(CSharp_FixedFieldDeclaration, AcceptVisitorVirtualThroughBase) {
    FixedFieldDeclaration ffd;
    AstNode* node = &ffd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ffd"}));
}

TEST(CSharp_FixedFieldDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    FixedFieldDeclaration ffd;
    EntityDeclaration* node = &ffd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ffd"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int`) and the `Variables`
// collection (a `FixedVariableInitializer` `x` whose `CountExpression` is a `PrimitiveExpression`)
// in document order (Attributes -> ReturnType -> Variables, with no attributes here).
TEST(CSharp_FixedFieldDeclaration, DepthFirstWalk) {
    auto ffd = make_FixedFieldDeclaration();
    RecordingVisitor v;
    ffd->AcceptVisitor(v);
    // ffd -> simple:int (ReturnType) -> id:int -> fvi:x (Variables[0]) -> id:x -> prim (CountExpression)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "ffd", "simple:int", "id:int", "fvi:x", "id:x", "prim",
    }));
}

// A `FixedFieldDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` and `Variables`.
TEST(CSharp_FixedFieldDeclaration, DepthFirstWalkWithAttribute) {
    auto ffd = make_FixedFieldDeclarationWithAttribute();
    RecordingVisitor v;
    ffd->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 10u);
    EXPECT_EQ(v.trace[0], "ffd");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "fvi:x");
    EXPECT_EQ(v.trace[8], "id:x");
    EXPECT_EQ(v.trace[9], "prim");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_FixedFieldDeclaration, DoMatchSameNode) {
    auto a = make_FixedFieldDeclaration();
    auto b = make_FixedFieldDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_FixedFieldDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_FixedFieldDeclaration();
    auto b = make_FixedFieldDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_FixedFieldDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_FixedFieldDeclaration();
    auto b = make_FixedFieldDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` `ReturnType` term).
TEST(CSharp_FixedFieldDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_FixedFieldDeclaration();  // ReturnType int
    FfdHolder b;
    b.ffd = std::make_unique<FixedFieldDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("byte"));  // different name
    b.ffd->ReturnType(b.returnType.get());
    b.varCount = std::make_unique<PrimitiveExpression>(int32_t(10));
    b.varX = std::make_unique<FixedVariableInitializer>(std::string("x"), b.varCount.get());
    b.ffd->Variables().Add(b.varX.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Variables` mismatch rejects (the collection recursive `DoMatch` -- different count rejects).
TEST(CSharp_FixedFieldDeclaration, DoMatchVariablesCountMismatchRejects) {
    auto a = make_FixedFieldDeclaration();  // 1 variable
    FfdHolder b;
    b.ffd = std::make_unique<FixedFieldDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.ffd->ReturnType(b.returnType.get());
    // no variables
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Variables` name mismatch rejects (the collection recursive `DoMatch` -- a
// `FixedVariableInitializer` `x` vs a `FixedVariableInitializer` `y` rejects via the `MatchString`
// on `Name`).
TEST(CSharp_FixedFieldDeclaration, DoMatchVariablesNameMismatchRejects) {
    auto a = make_FixedFieldDeclaration();  // variable x
    FfdHolder b;
    b.ffd = std::make_unique<FixedFieldDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.ffd->ReturnType(b.returnType.get());
    b.varCount = std::make_unique<PrimitiveExpression>(int32_t(10));
    b.varX = std::make_unique<FixedVariableInitializer>(std::string("y"), b.varCount.get());
    b.ffd->Variables().Add(b.varX.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A cross-structural-twin rejection: a `FixedFieldDeclaration` pattern vs a `FieldDeclaration`
// candidate (and vice versa) -- the two share the two-collection `EntityDeclaration` shape but are
// distinct concrete types (the `Variables` element type differs), so the type-check gate rejects.
TEST(CSharp_FixedFieldDeclaration, DoMatchRejectsFieldDeclaration) {
    auto a = make_FixedFieldDeclaration();
    // Build a FieldDeclaration with a ReturnType and a VariableInitializer.
    auto fd = std::make_unique<FieldDeclaration>();
    auto rt = std::make_unique<SimpleType>(std::string("int"));
    fd->ReturnType(rt.get());
    auto vi = std::make_unique<VariableInitializer>(std::string("x"));
    fd->Variables().Add(vi.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), fd.get()));
    EXPECT_FALSE(DoMatchAgainst(fd.get(), a.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`).
TEST(CSharp_FixedFieldDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_FixedFieldDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_FixedFieldDeclaration, DoMatchRejectsNull) {
    auto a = make_FixedFieldDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`Attributes`/`Variables`, copies the `Modifiers` scalar,
// and does not detach the source.
TEST(CSharp_FixedFieldDeclaration, CloneDeepCopies) {
    auto a = make_FixedFieldDeclaration();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<FixedFieldDeclaration>(
        static_cast<FixedFieldDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    EXPECT_EQ(clone->Variables().Count(), 1u);
    EXPECT_NE(clone->Variables().At(0), a->Variables().At(0));
    EXPECT_EQ(clone->Variables().At(0)->Name(), "x");
    // The source is not detached.
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
    EXPECT_EQ(a->Variables().At(0)->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `FixedFieldDeclaration*`.
TEST(CSharp_FixedFieldDeclaration, CloneVirtualAndCovariant) {
    auto a = make_FixedFieldDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<FixedFieldDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<FixedFieldDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_FixedFieldDeclaration, CloneCopiesAttributes) {
    auto a = make_FixedFieldDeclarationWithAttribute();
    auto clone = std::unique_ptr<FixedFieldDeclaration>(
        static_cast<FixedFieldDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// `Clone` deep-copies the `Variables`' `FixedVariableInitializer` children including their
// `CountExpression` (the nested `PrimitiveExpression` is distinct from the source's).
TEST(CSharp_FixedFieldDeclaration, CloneDeepCopiesVariableCountExpressions) {
    auto a = make_FixedFieldDeclaration();
    auto clone = std::unique_ptr<FixedFieldDeclaration>(
        static_cast<FixedFieldDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Variables().Count(), 1u);
    auto* clonedVar = clone->Variables().At(0);
    EXPECT_NE(clonedVar, a->Variables().At(0));
    ASSERT_NE(clonedVar->CountExpression(), nullptr);
    EXPECT_NE(clonedVar->CountExpression(), a->Variables().At(0)->CountExpression());
    EXPECT_EQ(clonedVar->CountExpression()->Parent(), clonedVar);
}

// ---- CheckInvariant -------------------------------------------------------

TEST(CSharp_FixedFieldDeclaration, CheckInvariantPassesOnFilled) {
    auto ffd = make_FixedFieldDeclaration();  // ReturnType filled
    ffd->CheckInvariant();  // should not assert
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_FixedFieldDeclaration, CheckInvariantRejectsEmpty) {
    FixedFieldDeclaration ffd;  // no ReturnType
    EXPECT_DEATH(ffd.CheckInvariant(), "");
}
#endif
