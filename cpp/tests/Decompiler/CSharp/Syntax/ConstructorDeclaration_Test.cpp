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
// OTHERWISE, ARISING, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// DEALINGS IN THE SOFTWARE.

// Tests for the `ConstructorInitializer` concrete node (cpp/.../Syntax/ConstructorInitializer.hpp,
// the port of the `ConstructorInitializer` in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ConstructorDeclaration.cs) and the
// `ConstructorDeclaration` concrete node (cpp/.../Syntax/ConstructorDeclaration.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ConstructorDeclaration.cs) -- the next
// in-order Phase-5 piece per the D280 plan. `ConstructorInitializer` is the dependency of
// `ConstructorDeclaration.Initializer` (a sealed `AstNode` with a `ConstructorInitializerType`
// scalar and an `Arguments` `Expression` collection); `ConstructorDeclaration` is the second
// `EntityDeclaration` with a `Parameters` collection (the `OperatorDeclaration` D280 two-collection
// shape with two trailing nullable singles instead of the `OperatorType` scalar, plus the
// `DestructorDeclaration` D272 `[ExcludeFromMatch]` `NameToken`). The file carries two suites
// (CSharp_ConstructorInitializer + CSharp_ConstructorDeclaration) sharing a `RecordingVisitor` and
// a `DoMatchAgainst` helper (the D234 multi-suite pattern).

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
#include "Decompiler/CSharp/Syntax/ConstructorInitializer.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitConstructorInitializer`/`VisitConstructorDeclaration`
// under test (plus the `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`PrimitiveExpression`/
// `ParameterDeclaration`/`BlockStatement`/`ReturnStatement` of the slots, and the
// `DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch rejections), recording a
// tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the visited
// nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitConstructorInitializer(ConstructorInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-ctorinit>"); return; }
        trace.push_back("ctorinit:" + std::to_string(static_cast<int>(node->ConstructorInitializerType())));
        VisitChildren(node);
    }
    void VisitConstructorDeclaration(ConstructorDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-ctor>"); return; }
        trace.push_back("ctor:" + node->Name());
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
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-simple>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or(""));
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim");
        VisitChildren(node);
    }
    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-param>"); return; }
        trace.push_back("param");
        VisitChildren(node);
    }
    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-block>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitReturnStatement(ReturnStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-return>"); return; }
        trace.push_back("return");
        VisitChildren(node);
    }
    void VisitDestructorDeclaration(DestructorDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-dtor>"); return; }
        trace.push_back("dtor");
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

// A holder keeping a `ConstructorInitializer` and its `Arguments` alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design).
struct CtorInitHolder {
    std::unique_ptr<ConstructorInitializer> ci;
    std::vector<std::unique_ptr<PrimitiveExpression>> args;
    ConstructorInitializer* get() const { return ci.get(); }
    ConstructorInitializer* operator->() const { return ci.get(); }
};

// Build a `ConstructorInitializer` with `ConstructorInitializerType::Base` and no arguments.
CtorInitHolder make_ConstructorInitializerBase() {
    CtorInitHolder h;
    h.ci = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::Base);
    return h;
}

// Build a `ConstructorInitializer` with `ConstructorInitializerType::This` and one argument
// `PrimitiveExpression` `42`.
CtorInitHolder make_ConstructorInitializerThisWithArg() {
    CtorInitHolder h;
    h.ci = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::This);
    h.args.push_back(std::make_unique<PrimitiveExpression>(int32_t(42)));
    h.ci->Arguments().Add(h.args[0].get());
    return h;
}

// A holder keeping a `ConstructorDeclaration` and all its children alive in the test scope.
struct CtorHolder {
    std::unique_ptr<ConstructorDeclaration> cd;
    std::unique_ptr<Identifier> nameToken;
    // One parameter "int x" (kept alive with its own Type + NameToken).
    std::unique_ptr<ParameterDeclaration> param0;
    std::unique_ptr<SimpleType> param0Type;
    std::unique_ptr<Identifier> param0NameToken;
    std::unique_ptr<ConstructorInitializer> initializer;
    std::unique_ptr<PrimitiveExpression> initializerArg;
    std::unique_ptr<BlockStatement> body;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    ConstructorDeclaration* get() const { return cd.get(); }
    ConstructorDeclaration* operator->() const { return cd.get(); }
};

// Build a `ConstructorDeclaration` with a `NameToken` `Identifier` `Foo` (no attributes, no
// `Parameters`, no `Initializer`, no `Body`). The holder keeps every node alive.
CtorHolder make_ConstructorDeclaration() {
    CtorHolder h;
    h.cd = std::make_unique<ConstructorDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.cd->NameToken(h.nameToken.get());
    return h;
}

// Build a `ConstructorDeclaration` with a `NameToken` `Foo` and an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
CtorHolder make_ConstructorDeclarationWithAttribute() {
    CtorHolder h;
    h.cd = std::make_unique<ConstructorDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.cd->NameToken(h.nameToken.get());
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.cd->Attributes().Add(h.attrSec.get());
    return h;
}

// Build a `ConstructorDeclaration` with every slot filled: a `NameToken` `Foo`, an `Attributes`
// `AttributeSection` with an `Attribute` `Foo`, a `Parameters` collection holding one
// `ParameterDeclaration` "int x", an `Initializer` `ConstructorInitializer` `: this(42)` (with one
// argument), and a `Body` `BlockStatement` -- every slot filled.
CtorHolder make_ConstructorDeclarationFull() {
    CtorHolder h;
    h.cd = std::make_unique<ConstructorDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.cd->NameToken(h.nameToken.get());
    // Attribute
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.cd->Attributes().Add(h.attrSec.get());
    // One parameter "int x"
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.param0Type = std::make_unique<SimpleType>(std::string("int"));
    h.param0->Type(h.param0Type.get());
    h.param0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param0->NameToken(h.param0NameToken.get());
    h.cd->Parameters().Add(h.param0.get());
    // Initializer : this(42)
    h.initializer = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::This);
    h.initializerArg = std::make_unique<PrimitiveExpression>(int32_t(42));
    h.initializer->Arguments().Add(h.initializerArg.get());
    h.cd->Initializer(h.initializer.get());
    // Body block
    h.body = std::make_unique<BlockStatement>();
    h.cd->Body(h.body.get());
    return h;
}

} // namespace

// ==========================================================================
// ConstructorInitializer (the constructor_initializer node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `ConstructorInitializer` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_ConstructorInitializer, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<ConstructorInitializer>);
}

// `ConstructorInitializer` derives DIRECTLY from `AstNode` (NOT `EntityDeclaration`/
// `Statement`/`Expression`/`AstType`): an initializer is a structural node owned by a
// `ConstructorDeclaration`, not a member declaration.
TEST(CSharp_ConstructorInitializer, IsAstNodeNotEntityDeclaration) {
    ConstructorInitializer ci;
    EXPECT_NE(dynamic_cast<AstNode*>(&ci), nullptr);
    EXPECT_EQ(dynamic_cast<EntityDeclaration*>(&ci), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ci), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ci), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ci), nullptr);
}

// ---- The ConstructorInitializerType enum --------------------------------

TEST(CSharp_ConstructorInitializer, ConstructorInitializerTypeEnumValues) {
    EXPECT_EQ(static_cast<int>(ConstructorInitializerType::Any), 0);
    EXPECT_EQ(static_cast<int>(ConstructorInitializerType::Base), 1);
    EXPECT_EQ(static_cast<int>(ConstructorInitializerType::This), 2);
}

// ---- The BaseKeyword/ThisKeyword const strings ---------------------------

TEST(CSharp_ConstructorInitializer, BaseKeywordConstIsBase) {
    EXPECT_STREQ(ConstructorInitializer::BaseKeyword, "base");
}

TEST(CSharp_ConstructorInitializer, ThisKeywordConstIsThis) {
    EXPECT_STREQ(ConstructorInitializer::ThisKeyword, "this");
}

// ---- Construction --------------------------------------------------------

// The empty ctor: `ConstructorInitializerType` defaults to `Any` (the enum's zero value), the
// `Arguments` collection is empty, `GetChildCount` is 0 (the collection-only shape -- an empty node
// reports 0).
TEST(CSharp_ConstructorInitializer, EmptyCtor) {
    ConstructorInitializer ci;
    EXPECT_EQ(ci.ConstructorInitializerType(), ConstructorInitializerType::Any);
    EXPECT_EQ(ci.Arguments().Count(), 0);
    EXPECT_EQ(ci.GetChildCount(), 0);
}

// The explicit `(ConstructorInitializerType)` ctor sets the scalar.
TEST(CSharp_ConstructorInitializer, ExplicitCtorSetsScalar) {
    ConstructorInitializer ci(ConstructorInitializerType::Base);
    EXPECT_EQ(ci.ConstructorInitializerType(), ConstructorInitializerType::Base);
}

TEST(CSharp_ConstructorInitializer, ExplicitCtorThis) {
    ConstructorInitializer ci(ConstructorInitializerType::This);
    EXPECT_EQ(ci.ConstructorInitializerType(), ConstructorInitializerType::This);
}

// ---- The ConstructorInitializerType scalar -------------------------------

TEST(CSharp_ConstructorInitializer, ConstructorInitializerTypeScalarRoundTrips) {
    ConstructorInitializer ci;
    ci.ConstructorInitializerType(ConstructorInitializerType::This);
    EXPECT_EQ(ci.ConstructorInitializerType(), ConstructorInitializerType::This);
    ci.ConstructorInitializerType(ConstructorInitializerType::Base);
    EXPECT_EQ(ci.ConstructorInitializerType(), ConstructorInitializerType::Base);
}

// ---- The Arguments collection (incremental, the only slot) ---------------

TEST(CSharp_ConstructorInitializer, ArgumentsCollectionAddReparents) {
    ConstructorInitializer ci;
    auto arg = std::make_unique<PrimitiveExpression>(int32_t(42));
    ci.Arguments().Add(arg.get());
    EXPECT_EQ(ci.Arguments().Count(), 1);
    EXPECT_EQ(arg->Parent(), &ci);
}

// The `Arguments` collection is the node's only collection and last slot, so it is INCREMENTAL
// (an element's flattened `ChildIndex` is exactly its local position).
TEST(CSharp_ConstructorInitializer, ArgumentsCollectionIsIncremental) {
    ConstructorInitializer ci;
    auto a0 = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto a1 = std::make_unique<PrimitiveExpression>(int32_t(2));
    ci.Arguments().Add(a0.get());
    ci.Arguments().Add(a1.get());
    EXPECT_EQ(a0->ChildIndex, 0);
    EXPECT_EQ(a1->ChildIndex, 1);
}

// ---- Slot storage (the collection-only shape) ----------------------------

TEST(CSharp_ConstructorInitializer, GetChildWalksCollection) {
    auto ci = make_ConstructorInitializerThisWithArg();
    EXPECT_EQ(ci->GetChildCount(), 1);
    EXPECT_EQ(ci->GetChild(0), ci->Arguments().At(0));
    EXPECT_THROW(ci->GetChild(1), std::out_of_range);
}

TEST(CSharp_ConstructorInitializer, GetChildSlotInfoWalksCollection) {
    auto ci = make_ConstructorInitializerThisWithArg();
    EXPECT_EQ(ci->GetChildSlotInfo(0), &ci->ArgumentsSlot);
    EXPECT_THROW(ci->GetChildSlotInfo(1), std::out_of_range);
}

TEST(CSharp_ConstructorInitializer, GetCollectionByKindReturnsArguments) {
    ConstructorInitializer ci;
    EXPECT_EQ(ci.GetCollectionByKind(&Slots::Argument), &ci.Arguments());
    EXPECT_EQ(ci.GetCollectionByKind(&Slots::Type), nullptr);
}

// `SetChild` replaces an `Arguments` element in place.
TEST(CSharp_ConstructorInitializer, SetChildReplacesArgument) {
    auto ci = make_ConstructorInitializerThisWithArg();
    auto arg2 = std::make_unique<PrimitiveExpression>(int32_t(99));
    auto* oldArg = ci->Arguments().At(0);
    ci->SetChild(0, arg2.get());
    EXPECT_EQ(ci->Arguments().At(0), arg2.get());
    EXPECT_EQ(oldArg->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kind) -------

TEST(CSharp_ConstructorInitializer, ArgumentsSlotPointsAtSharedKind) {
    ConstructorInitializer ci;
    EXPECT_EQ(ci.ArgumentsSlot.Kind(), &Slots::Argument);
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_ConstructorInitializer, AcceptVisitorDispatches) {
    ConstructorInitializer ci;
    RecordingVisitor v;
    ci.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctorinit:0"}));  // Any = 0
}

TEST(CSharp_ConstructorInitializer, AcceptVisitorVirtualThroughBase) {
    ConstructorInitializer ci;
    AstNode* node = &ci;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctorinit:0"}));
}

// The depth-first walk recurses into the `Arguments` collection (each `PrimitiveExpression` in
// document order).
TEST(CSharp_ConstructorInitializer, DepthFirstWalkEmpty) {
    ConstructorInitializer ci(ConstructorInitializerType::Base);
    RecordingVisitor v;
    ci.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctorinit:1"}));  // Base = 1, no args
}

TEST(CSharp_ConstructorInitializer, DepthFirstWalkWithArg) {
    auto ci = make_ConstructorInitializerThisWithArg();
    RecordingVisitor v;
    ci->AcceptVisitor(v);
    // ctorinit:2 (This) -> prim (the argument)
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctorinit:2", "prim"}));
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_ConstructorInitializer, DoMatchSameNode) {
    auto a = make_ConstructorInitializerBase();
    auto b = make_ConstructorInitializerBase();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ConstructorInitializerType` mismatch rejects (the plain `==` term).
TEST(CSharp_ConstructorInitializer, DoMatchTypeMismatchRejects) {
    auto a = make_ConstructorInitializerBase();  // Base
    auto b = make_ConstructorInitializerThisWithArg();  // This (plus an arg, but the type rejects first)
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `ConstructorInitializerType::Any` matches any candidate (the wildcard). Both sides must
// have matching `Arguments` (or none) -- the `Any`-wildcard is on the TYPE only, the `Arguments`
// collection-DoMatch still runs.
TEST(CSharp_ConstructorInitializer, DoMatchAnyWildcard) {
    ConstructorInitializer a;  // Any (default), no args
    ConstructorInitializer b(ConstructorInitializerType::This);  // This, no args
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// An `Arguments` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_ConstructorInitializer, DoMatchArgumentsMismatchRejects) {
    auto a = make_ConstructorInitializerThisWithArg();  // one arg
    CtorInitHolder b;
    b.ci = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::This);  // no args
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single argument match (the collection recursive `DoMatch` accepts
// equal-length collections whose elements match).
TEST(CSharp_ConstructorInitializer, DoMatchSameArgumentsMatch) {
    auto a = make_ConstructorInitializerThisWithArg();
    auto b = make_ConstructorInitializerThisWithArg();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`ConstructorInitializer` candidate rejects (the type-check gate -- a `DestructorDeclaration`
// is an `AstNode` but not a `ConstructorInitializer`).
TEST(CSharp_ConstructorInitializer, DoMatchRejectsNonConstructorInitializer) {
    auto a = make_ConstructorInitializerBase();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_ConstructorInitializer, DoMatchRejectsNull) {
    auto a = make_ConstructorInitializerBase();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `Arguments`, copies the `ConstructorInitializerType` scalar, and does
// not detach the source.
TEST(CSharp_ConstructorInitializer, CloneDeepCopies) {
    auto a = make_ConstructorInitializerThisWithArg();
    auto clone = std::unique_ptr<ConstructorInitializer>(
        static_cast<ConstructorInitializer*>(a->Clone()));
    EXPECT_EQ(clone->ConstructorInitializerType(), ConstructorInitializerType::This);
    ASSERT_EQ(clone->Arguments().Count(), 1u);
    EXPECT_NE(clone->Arguments().At(0), a->Arguments().At(0));
    EXPECT_EQ(clone->Arguments().At(0)->Parent(), clone.get());
    // The source is not detached.
    EXPECT_EQ(a->Arguments().At(0)->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `ConstructorInitializer*`.
TEST(CSharp_ConstructorInitializer, CloneVirtualAndCovariant) {
    auto a = make_ConstructorInitializerBase();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<ConstructorInitializer*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<ConstructorInitializer>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the scalar (the `ConstructorInitializerType`).
TEST(CSharp_ConstructorInitializer, CloneCopiesScalar) {
    auto a = make_ConstructorInitializerBase();
    auto clone = std::unique_ptr<ConstructorInitializer>(
        static_cast<ConstructorInitializer*>(a->Clone()));
    EXPECT_EQ(clone->ConstructorInitializerType(), ConstructorInitializerType::Base);
}

// `Clone` on an empty node (no arguments) clones without them.
TEST(CSharp_ConstructorInitializer, CloneEmpty) {
    auto a = make_ConstructorInitializerBase();
    auto clone = std::unique_ptr<ConstructorInitializer>(
        static_cast<ConstructorInitializer*>(a->Clone()));
    EXPECT_EQ(clone->Arguments().Count(), 0u);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on an empty node (the only slot is the `Arguments` collection, which is
// never a required slot -- an empty `ConstructorInitializer` is invariant-valid, the
// `ArrayInitializerExpression` D250 collection-only precedent).
TEST(CSharp_ConstructorInitializer, CheckInvariantPassesOnEmpty) {
    ConstructorInitializer ci;
    ci.CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on a filled node.
TEST(CSharp_ConstructorInitializer, CheckInvariantPassesOnFilled) {
    auto ci = make_ConstructorInitializerThisWithArg();
    ci->CheckInvariant();  // should not assert
    SUCCEED();
}

// ==========================================================================
// ConstructorDeclaration (the constructor_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `ConstructorDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_ConstructorDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<ConstructorDeclaration>);
}

// `ConstructorDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_ConstructorDeclaration, IsEntityDeclarationAndAstNode) {
    ConstructorDeclaration cd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&cd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&cd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&cd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&cd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&cd), nullptr);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `NameToken`, empty `Attributes`/`Parameters`, `Initializer`/`Body` null.
// `GetChildCount` is `0 + 0 + 3 = 3` (the two empty collections plus the three single slots -- each
// single slot contributes 1 to the flattened count even when absent). `Modifiers` defaults to
// `None`; `SymbolKind` is `Constructor`.
TEST(CSharp_ConstructorDeclaration, EmptyCtor) {
    ConstructorDeclaration cd;
    EXPECT_EQ(cd.SymbolKind(), SymbolKind::Constructor);
    EXPECT_EQ(cd.Modifiers(), Modifiers::None);
    EXPECT_EQ(cd.NameToken(), nullptr);
    EXPECT_EQ(cd.Parameters().Count(), 0);
    EXPECT_EQ(cd.Initializer(), nullptr);
    EXPECT_EQ(cd.Body(), nullptr);
    EXPECT_EQ(cd.Attributes().Count(), 0);
    EXPECT_EQ(cd.GetChildCount(), 3);  // 0 attrs + 0 params + 3 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_ConstructorDeclaration, SymbolKindOverrideReturnsConstructor) {
    ConstructorDeclaration cd;
    EXPECT_EQ(cd.SymbolKind(), SymbolKind::Constructor);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_ConstructorDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    ConstructorDeclaration cd;
    EXPECT_EQ(cd.Modifiers(), Modifiers::None);
    cd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(cd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_ConstructorDeclaration, HasModifierIsBitmaskTest) {
    ConstructorDeclaration cd;
    cd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(cd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(cd.HasModifier(Modifiers::Public));
    EXPECT_FALSE(cd.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken (NameToken is [ExcludeFromMatch], a real [Slot]) ----

// `Name` returns the `NameToken`'s name (the inherited base `Name()` kind-walks for the `Identifier`
// kind and returns the token's `Name`). A constructor's name is just the declaring type name.
TEST(CSharp_ConstructorDeclaration, NameReturnsNameTokenName) {
    auto cd = make_ConstructorDeclaration();
    EXPECT_EQ(cd->Name(), "Foo");
}

// `NameToken` returns the backing field directly (the generated `get => field!`).
TEST(CSharp_ConstructorDeclaration, NameTokenReturnsBackingField) {
    auto cd = make_ConstructorDeclaration();
    EXPECT_EQ(cd->NameToken(), cd->NameToken());
    EXPECT_NE(cd->NameToken(), nullptr);
    EXPECT_EQ(cd->NameToken()->Name(), "Foo");
}

// `NameToken` setter re-parents and re-indexes (the index-less setter following the `Attributes`
// collection).
TEST(CSharp_ConstructorDeclaration, NameTokenSetterReparentsAndDetaches) {
    ConstructorDeclaration cd;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    cd.NameToken(a.get());
    EXPECT_EQ(cd.NameToken(), a.get());
    EXPECT_EQ(a->Parent(), &cd);
    cd.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(cd.NameToken(), b.get());
    cd.NameToken(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(cd.NameToken(), nullptr);
}

// ---- The ReturnType (inherited base, null for a constructor) --------------

// A `ConstructorDeclaration` has no `ReturnType` slot, so the inherited base `ReturnType()`
// kind-walk returns null (the `DestructorDeclaration` D272 precedent).
TEST(CSharp_ConstructorDeclaration, ReturnTypeIsNullNoSlot) {
    auto cd = make_ConstructorDeclaration();
    EXPECT_EQ(cd->ReturnType(), nullptr);
}

// ---- The Parameters collection (non-incremental) ------------------------

TEST(CSharp_ConstructorDeclaration, ParametersCollectionAddReparents) {
    ConstructorDeclaration cd;
    auto p = std::make_unique<ParameterDeclaration>();
    cd.Parameters().Add(p.get());
    EXPECT_EQ(cd.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &cd);
}

TEST(CSharp_ConstructorDeclaration, ParametersCollectionAddReindexesDynamically) {
    ConstructorDeclaration cd;
    auto p0 = std::make_unique<ParameterDeclaration>();
    auto p1 = std::make_unique<ParameterDeclaration>();
    cd.Parameters().Add(p0.get());
    cd.Parameters().Add(p1.get());
    // The collection is non-incremental (the node has two collections), so the flattened
    // `ChildIndex` is dynamic -- trigger the reindex via `Slot()`.
    (void)p0->Slot();
    EXPECT_EQ(p0->ChildIndex, 1);  // attrCount (0) + 1 single (NameToken) + 0
    EXPECT_EQ(p1->ChildIndex, 2);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_ConstructorDeclaration, AttributesCollectionAddReparents) {
    ConstructorDeclaration cd;
    auto attrSec = std::make_unique<AttributeSection>();
    cd.Attributes().Add(attrSec.get());
    EXPECT_EQ(cd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &cd);
}

// ---- The Initializer slot (a NULLABLE single, index-less) ----------------

TEST(CSharp_ConstructorDeclaration, InitializerSetterReparentsAndDetaches) {
    ConstructorDeclaration cd;
    auto ci = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::Base);
    cd.Initializer(ci.get());
    EXPECT_EQ(cd.Initializer(), ci.get());
    EXPECT_EQ(ci->Parent(), &cd);
    cd.Initializer(nullptr);
    EXPECT_EQ(ci->Parent(), nullptr);
    EXPECT_EQ(cd.Initializer(), nullptr);
}

// ---- The Body slot (a NULLABLE single, index-less) ----------------------

TEST(CSharp_ConstructorDeclaration, BodySetterReparentsAndDetaches) {
    ConstructorDeclaration cd;
    auto b = std::make_unique<BlockStatement>();
    cd.Body(b.get());
    EXPECT_EQ(cd.Body(), b.get());
    EXPECT_EQ(b->Parent(), &cd);
    cd.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(cd.Body(), nullptr);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `NameToken` single
// at `attrCount`, the `Parameters` collection `[attrCount + 1, attrCount + 1 + paramCount)`, the
// `Initializer` single at `attrCount + 1 + paramCount`, the `Body` single at
// `attrCount + 2 + paramCount`.
TEST(CSharp_ConstructorDeclaration, GetChildWalksSlots) {
    auto cd = make_ConstructorDeclarationFull();
    // 1 attr + 1 param + 3 singles
    EXPECT_EQ(cd->GetChildCount(), 5);
    EXPECT_EQ(cd->GetChild(0), cd->Attributes().At(0));  // the AttributeSection
    EXPECT_EQ(cd->GetChild(1), cd->NameToken());          // 1 (attrCount + NameToken)
    EXPECT_EQ(cd->GetChild(2), cd->Parameters().At(0));  // 2 (attrCount + 1 + param[0])
    EXPECT_EQ(cd->GetChild(3), cd->Initializer());        // 3
    EXPECT_EQ(cd->GetChild(4), cd->Body());               // 4
    EXPECT_THROW(cd->GetChild(5), std::out_of_range);
}

TEST(CSharp_ConstructorDeclaration, GetChildSlotInfoWalksSlots) {
    auto cd = make_ConstructorDeclarationFull();
    EXPECT_EQ(cd->GetChildSlotInfo(0), &cd->AttributesSlot);
    EXPECT_EQ(cd->GetChildSlotInfo(1), &cd->NameTokenSlot);
    EXPECT_EQ(cd->GetChildSlotInfo(2), &cd->ParametersSlot);
    EXPECT_EQ(cd->GetChildSlotInfo(3), &cd->InitializerSlot);
    EXPECT_EQ(cd->GetChildSlotInfo(4), &cd->BodySlot);
    EXPECT_THROW(cd->GetChildSlotInfo(5), std::out_of_range);
}

TEST(CSharp_ConstructorDeclaration, GetCollectionByKindReturnsAttributesAndParameters) {
    ConstructorDeclaration cd;
    EXPECT_EQ(cd.GetCollectionByKind(&Slots::AttributeSection), &cd.Attributes());
    EXPECT_EQ(cd.GetCollectionByKind(&Slots::Parameter), &cd.Parameters());
    EXPECT_EQ(cd.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(cd.GetCollectionByKind(&Slots::Identifier), nullptr);
}

// `SetChild` replaces the `NameToken` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_ConstructorDeclaration, SetChildReplacesNameToken) {
    auto cd = make_ConstructorDeclaration();
    auto name2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    auto* oldName = cd->NameToken();
    cd->SetChild(0, name2.get());  // NameToken at flattened index 0 (0 attrs)
    EXPECT_EQ(cd->NameToken(), name2.get());
    EXPECT_EQ(oldName->Parent(), nullptr);
}

// `SetChild` replaces a `Parameters` element in place.
TEST(CSharp_ConstructorDeclaration, SetChildReplacesParameter) {
    auto cd = make_ConstructorDeclarationFull();
    auto p2 = std::make_unique<ParameterDeclaration>();
    auto* oldParam = cd->Parameters().At(0);
    cd->SetChild(2, p2.get());  // Parameters[0] at flattened index 2 (1 attr + 1 single)
    EXPECT_EQ(cd->Parameters().At(0), p2.get());
    EXPECT_EQ(oldParam->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_ConstructorDeclaration, SlotStaticsPointAtSharedKinds) {
    ConstructorDeclaration cd;
    EXPECT_EQ(cd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(cd.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(cd.ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(cd.InitializerSlot.Kind(), &Slots::ConstructorInitializer);
    EXPECT_EQ(cd.BodySlot.Kind(), &Slots::Body);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_ConstructorDeclaration, SlotStaticsAreDistinct) {
    ConstructorDeclaration cd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&cd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&cd.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&cd.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&cd.ParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&cd.ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&cd.InitializerSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&cd.InitializerSlot),
              static_cast<const CSharpSlotInfo*>(&cd.BodySlot));
}

// The new `Slots::ConstructorInitializer` kind (cycle-broken into `ConstructorInitializer.hpp`
// this iteration) is distinct from the other collection/single kinds (cast both to the common
// `CSharpSlotInfo*` base for the cross-element-type `EXPECT_NE`, the D251/D252/D262 precedent).
TEST(CSharp_ConstructorDeclaration, ConstructorInitializerKindIsDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::ConstructorInitializer),
              static_cast<const CSharpSlotInfo*>(&Slots::AttributeSection));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::ConstructorInitializer),
              static_cast<const CSharpSlotInfo*>(&Slots::Parameter));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::ConstructorInitializer),
              static_cast<const CSharpSlotInfo*>(&Slots::Body));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_ConstructorDeclaration, AcceptVisitorDispatches) {
    ConstructorDeclaration cd;  // no NameToken -- the empty node records just the node tag
    RecordingVisitor v;
    cd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctor:"}));
}

TEST(CSharp_ConstructorDeclaration, AcceptVisitorVirtualThroughBase) {
    ConstructorDeclaration cd;
    AstNode* node = &cd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctor:"}));
}

TEST(CSharp_ConstructorDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    ConstructorDeclaration cd;
    EntityDeclaration* node = &cd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ctor:"}));
}

// The depth-first walk recurses into the `NameToken` (an `Identifier` `Foo`) in document order
// (no attributes, no `Parameters`, no `Initializer`, no `Body`). A constructor HAS a `NameToken`
// (unlike `IndexerDeclaration`), so the walk recurses into it.
TEST(CSharp_ConstructorDeclaration, DepthFirstWalk) {
    auto cd = make_ConstructorDeclaration();
    RecordingVisitor v;
    cd->AcceptVisitor(v);
    // ctor:Foo -> id:Foo (NameToken)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "ctor:Foo", "id:Foo",
    }));
}

// A `ConstructorDeclaration` with an attribute recurses into the `AttributeSection` ->
// `Attribute` -> its `SimpleType` `Type` -> its `Identifier` BEFORE the `NameToken` (the
// `Attributes` collection is the first slot).
TEST(CSharp_ConstructorDeclaration, DepthFirstWalkWithAttribute) {
    auto cd = make_ConstructorDeclarationWithAttribute();
    RecordingVisitor v;
    cd->AcceptVisitor(v);
    // ctor:Foo -> attrsec (Attributes[0]) -> attr -> simple:Foo -> id:Foo
    //      -> id:Foo (NameToken)
    ASSERT_EQ(v.trace.size(), 6u);
    EXPECT_EQ(v.trace[0], "ctor:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:Foo");  // the NameToken
}

// A `ConstructorDeclaration` with every slot filled recurses into the `Attributes`, `NameToken`,
// each `Parameter` (and its `Type`/`NameToken`), the `Initializer` (and its `Arguments`), and the
// `Body`, in slot order.
TEST(CSharp_ConstructorDeclaration, DepthFirstWalkFull) {
    auto cd = make_ConstructorDeclarationFull();
    RecordingVisitor v;
    cd->AcceptVisitor(v);
    // ctor:Foo -> attrsec -> attr -> simple:Foo -> id:Foo (Attribute)
    //      -> id:Foo (NameToken)
    //      -> param (Parameters[0]) -> simple:int (param's Type) -> id:int -> id:x (NameToken)
    //      -> ctorinit:2 (Initializer This) -> prim (the argument 42)
    //      -> block (Body, empty)
    ASSERT_EQ(v.trace.size(), 13u);
    EXPECT_EQ(v.trace[0], "ctor:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:Foo");  // NameToken
    EXPECT_EQ(v.trace[6], "param");
    EXPECT_EQ(v.trace[7], "simple:int");
    EXPECT_EQ(v.trace[8], "id:int");
    EXPECT_EQ(v.trace[9], "id:x");
    EXPECT_EQ(v.trace[10], "ctorinit:2");
    EXPECT_EQ(v.trace[11], "prim");  // the Initializer's argument
    EXPECT_EQ(v.trace[12], "block");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_ConstructorDeclaration, DoMatchSameNode) {
    auto a = make_ConstructorDeclaration();
    auto b = make_ConstructorDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_ConstructorDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_ConstructorDeclaration();
    auto b = make_ConstructorDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_ConstructorDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_ConstructorDeclaration();
    auto b = make_ConstructorDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Parameters` mismatch rejects (the collection recursive `DoMatch` -- a pattern with a
// parameter vs a candidate without one).
TEST(CSharp_ConstructorDeclaration, DoMatchParametersMismatchRejects) {
    auto a = make_ConstructorDeclarationFull();  // one parameter
    auto b = make_ConstructorDeclaration();  // no parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single parameter match (the collection recursive `DoMatch` accepts
// equal-length collections whose elements match).
TEST(CSharp_ConstructorDeclaration, DoMatchSameParametersMatch) {
    auto a = make_ConstructorDeclaration();
    auto ap = std::make_unique<ParameterDeclaration>();
    auto apt = std::make_unique<SimpleType>(std::string("int"));
    ap->Type(apt.get());
    a->Parameters().Add(ap.get());
    auto b = make_ConstructorDeclaration();
    auto bp = std::make_unique<ParameterDeclaration>();
    auto bpt = std::make_unique<SimpleType>(std::string("int"));
    bp->Type(bpt.get());
    b->Parameters().Add(bp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// An `Initializer` asymmetry rejects: a pattern with an `Initializer` vs a candidate without one.
TEST(CSharp_ConstructorDeclaration, DoMatchInitializerAsymmetryRejects) {
    auto a = make_ConstructorDeclaration();
    auto ci = std::make_unique<ConstructorInitializer>(ConstructorInitializerType::Base);
    a->Initializer(ci.get());
    auto b = make_ConstructorDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` asymmetry rejects: a pattern without a `Body` vs a candidate with one.
TEST(CSharp_ConstructorDeclaration, DoMatchBodyAsymmetryRejects) {
    auto a = make_ConstructorDeclaration();
    auto b = make_ConstructorDeclaration();
    auto body = std::make_unique<BlockStatement>();
    b->Body(body.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`ConstructorDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not a `ConstructorDeclaration`).
TEST(CSharp_ConstructorDeclaration, DoMatchRejectsNonConstructorEntityDeclaration) {
    auto a = make_ConstructorDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_ConstructorDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_ConstructorDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_ConstructorDeclaration, DoMatchRejectsNull) {
    auto a = make_ConstructorDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `NameToken`/`Parameters`/`Initializer`/`Body`, copies the `Modifiers`
// scalar, and does not detach the source.
TEST(CSharp_ConstructorDeclaration, CloneDeepCopies) {
    auto a = make_ConstructorDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<ConstructorDeclaration>(
        static_cast<ConstructorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Constructor);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->Name(), "Foo");
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    ASSERT_EQ(clone->Parameters().Count(), 1u);
    EXPECT_NE(clone->Parameters().At(0), a->Parameters().At(0));
    ASSERT_NE(clone->Initializer(), nullptr);
    EXPECT_NE(clone->Initializer(), a->Initializer());
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), a->Body());
    // The source is not detached.
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
    EXPECT_EQ(a->Body()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `ConstructorDeclaration*`.
TEST(CSharp_ConstructorDeclaration, CloneVirtualAndCovariant) {
    auto a = make_ConstructorDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<ConstructorDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<ConstructorDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_ConstructorDeclaration, CloneCopiesAttributes) {
    auto a = make_ConstructorDeclarationWithAttribute();
    auto clone = std::unique_ptr<ConstructorDeclaration>(
        static_cast<ConstructorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->NameToken()->Parent(), clone.get());
}

// `Clone` copies the `Parameters` collection (each element deep-cloned).
TEST(CSharp_ConstructorDeclaration, CloneCopiesParameters) {
    auto a = make_ConstructorDeclarationFull();
    auto clone = std::unique_ptr<ConstructorDeclaration>(
        static_cast<ConstructorDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Parameters().Count(), 1u);
    auto* clonedParam = clone->Parameters().At(0);
    EXPECT_NE(clonedParam, a->Parameters().At(0));
    ASSERT_NE(clonedParam->Type(), nullptr);
    EXPECT_NE(clonedParam->Type(), a->Parameters().At(0)->Type());
    ASSERT_NE(clonedParam->NameToken(), nullptr);
    EXPECT_NE(clonedParam->NameToken(), a->Parameters().At(0)->NameToken());
    EXPECT_EQ(clonedParam->NameToken()->Name(), "x");
}

// `Clone` skips absent nullable slots (a constructor with no `Initializer`/`Body` clones without
// them).
TEST(CSharp_ConstructorDeclaration, CloneSkipsAbsentNullableSlots) {
    auto a = make_ConstructorDeclaration();  // no Parameters/Initializer/Body
    auto clone = std::unique_ptr<ConstructorDeclaration>(
        static_cast<ConstructorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Parameters().Count(), 0u);
    EXPECT_EQ(clone->Initializer(), nullptr);
    EXPECT_EQ(clone->Body(), nullptr);
    ASSERT_NE(clone->NameToken(), nullptr);
}

// `Clone` copies the `Initializer`'s `Arguments` (the `ConstructorInitializer` deep-cloned with
// its arguments).
TEST(CSharp_ConstructorDeclaration, CloneCopiesInitializerArguments) {
    auto a = make_ConstructorDeclarationFull();
    auto clone = std::unique_ptr<ConstructorDeclaration>(
        static_cast<ConstructorDeclaration*>(a->Clone()));
    ASSERT_NE(clone->Initializer(), nullptr);
    EXPECT_EQ(clone->Initializer()->ConstructorInitializerType(), ConstructorInitializerType::This);
    ASSERT_EQ(clone->Initializer()->Arguments().Count(), 1u);
    EXPECT_NE(clone->Initializer()->Arguments().At(0), a->Initializer()->Arguments().At(0));
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `NameToken` required slot is filled).
TEST(CSharp_ConstructorDeclaration, CheckInvariantPassesOnFilled) {
    auto cd = make_ConstructorDeclaration();  // NameToken filled
    cd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_ConstructorDeclaration, CheckInvariantPassesOnFull) {
    auto cd = make_ConstructorDeclarationFull();
    cd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NameToken` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_ConstructorDeclaration, CheckInvariantRejectsEmpty) {
    ConstructorDeclaration cd;  // no NameToken
    EXPECT_DEATH(cd.CheckInvariant(), "");
}
#endif
