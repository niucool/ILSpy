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

// Tests for the `PropertyDeclaration` concrete node (cpp/.../Syntax/PropertyDeclaration.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/PropertyDeclaration.cs) -- the
// fifth concrete `TypeMember` and the first `EntityDeclaration` with seven single slots trailing
// the `Attributes` collection (the next in-order Phase-5 piece per the D275 plan, now unblocked by
// `Accessor` D274 for the `Getter`/`Setter` slots). The suite shares a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
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

// A recording depth-first visitor: overrides the `VisitPropertyDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`PrimitiveExpression`/`Accessor`/
// `BlockStatement`/`ReturnStatement` of its slots, and the `DestructorDeclaration`/
// `WhileStatement` used for the cross-type DoMatch rejections), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitPropertyDeclaration(PropertyDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-prop>"); return; }
        trace.push_back("property");
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
    void VisitAccessor(Accessor* node) override {
        if (node == nullptr) { trace.push_back("<null-accessor>"); return; }
        trace.push_back("accessor:" + std::to_string(static_cast<int>(node->Kind())));
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

// A holder keeping a `PropertyDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes).
struct PropertyHolder {
    std::unique_ptr<PropertyDeclaration> pd;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<SimpleType> privateImplType;
    std::unique_ptr<Accessor> getter;
    std::unique_ptr<BlockStatement> getterBody;
    std::unique_ptr<ReturnStatement> getterReturn;
    std::unique_ptr<PrimitiveExpression> getterReturnExpr;
    std::unique_ptr<Accessor> setter;
    std::unique_ptr<BlockStatement> setterBody;
    std::unique_ptr<PrimitiveExpression> initializer;
    std::unique_ptr<PrimitiveExpression> expressionBody;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    PropertyDeclaration* get() const { return pd.get(); }
    PropertyDeclaration* operator->() const { return pd.get(); }
};

// Build a `PropertyDeclaration` with a `ReturnType` `SimpleType` `int` and a `NameToken` `P` (no
// attributes, no accessors, no initializer, no expression body). The holder keeps every node
// alive.
PropertyHolder make_PropertyDeclaration() {
    PropertyHolder h;
    h.pd = std::make_unique<PropertyDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.pd->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("P")));
    h.pd->NameToken(h.nameToken.get());
    return h;
}

// Build a `PropertyDeclaration` with a `ReturnType` `int`, a `NameToken` `P`, and an `Attributes`
// `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
PropertyHolder make_PropertyDeclarationWithAttribute() {
    PropertyHolder h;
    h.pd = std::make_unique<PropertyDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.pd->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.pd->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("P")));
    h.pd->NameToken(h.nameToken.get());
    return h;
}

// Build a `PropertyDeclaration` with a `ReturnType` `int`, a `NameToken` `P`, a `Getter` accessor
// (whose `Body` is a `BlockStatement` `{ return 5; }`), a `Setter` accessor (whose `Body` is an
// empty `BlockStatement`), a `PrivateImplementationType` `IFoo`, an `Initializer`
// `PrimitiveExpression` `10`, and an `ExpressionBody` `PrimitiveExpression` `42` -- every slot
// filled.
PropertyHolder make_PropertyDeclarationFull() {
    PropertyHolder h;
    h.pd = std::make_unique<PropertyDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.pd->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("P")));
    h.pd->NameToken(h.nameToken.get());
    h.privateImplType = std::make_unique<SimpleType>(std::string("IFoo"));
    h.pd->PrivateImplementationType(h.privateImplType.get());
    // Getter accessor with a body { return 5; }
    h.getterReturnExpr = std::make_unique<PrimitiveExpression>(int32_t(5));
    h.getterReturn = std::make_unique<ReturnStatement>(h.getterReturnExpr.get());
    h.getterBody = std::make_unique<BlockStatement>();
    h.getterBody->Statements().Add(h.getterReturn.get());
    h.getter = std::make_unique<Accessor>(AccessorKind::Getter);
    h.getter->Body(h.getterBody.get());
    h.pd->Getter(h.getter.get());
    // Setter accessor with an empty body
    h.setterBody = std::make_unique<BlockStatement>();
    h.setter = std::make_unique<Accessor>(AccessorKind::Setter);
    h.setter->Body(h.setterBody.get());
    h.pd->Setter(h.setter.get());
    h.initializer = std::make_unique<PrimitiveExpression>(int32_t(10));
    h.pd->Initializer(h.initializer.get());
    h.expressionBody = std::make_unique<PrimitiveExpression>(int32_t(42));
    h.pd->ExpressionBody(h.expressionBody.get());
    return h;
}

} // namespace

// ==========================================================================
// PropertyDeclaration (the property_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `PropertyDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_PropertyDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<PropertyDeclaration>);
}

// `PropertyDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_PropertyDeclaration, IsEntityDeclarationAndAstNode) {
    PropertyDeclaration pd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&pd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&pd), nullptr);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `ReturnType`/`NameToken`, empty `Attributes`, all five nullable slots null.
// `GetChildCount` is `0 + 7 = 7` (the empty `Attributes` collection plus the seven single slots --
// each single slot contributes 1 to the flattened count even when absent). The `Modifiers`
// defaults to `None`; `SymbolKind` is `Property`.
TEST(CSharp_PropertyDeclaration, EmptyCtor) {
    PropertyDeclaration pd;
    EXPECT_EQ(pd.SymbolKind(), SymbolKind::Property);
    EXPECT_EQ(pd.Modifiers(), Modifiers::None);
    EXPECT_EQ(pd.ReturnType(), nullptr);
    EXPECT_EQ(pd.NameToken(), nullptr);
    EXPECT_EQ(pd.PrivateImplementationType(), nullptr);
    EXPECT_EQ(pd.Getter(), nullptr);
    EXPECT_EQ(pd.Setter(), nullptr);
    EXPECT_EQ(pd.Initializer(), nullptr);
    EXPECT_EQ(pd.ExpressionBody(), nullptr);
    EXPECT_EQ(pd.Attributes().Count(), 0);
    EXPECT_EQ(pd.GetChildCount(), 7);  // 0 attrs + 7 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_PropertyDeclaration, SymbolKindOverrideReturnsProperty) {
    PropertyDeclaration pd;
    EXPECT_EQ(pd.SymbolKind(), SymbolKind::Property);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_PropertyDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    PropertyDeclaration pd;
    EXPECT_EQ(pd.Modifiers(), Modifiers::None);
    pd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(pd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_PropertyDeclaration, HasModifierIsBitmaskTest) {
    PropertyDeclaration pd;
    pd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(pd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(pd.HasModifier(Modifiers::Public));
    EXPECT_FALSE(pd.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken (Name uses the inherited virtual; NameToken is overridden) ----

// `Name` returns the `NameToken`'s name (the inherited base `Name()` kind-walks for the
// `Identifier` kind -- `PropertyDeclaration` does NOT override `Name`, the real-name case).
TEST(CSharp_PropertyDeclaration, NameReturnsTheNameTokenName) {
    auto pd = make_PropertyDeclaration();  // NameToken P
    EXPECT_EQ(pd->Name(), "P");
}

// `NameToken` is the `Identifier` slot itself (the override returns the backing field).
TEST(CSharp_PropertyDeclaration, NameTokenIsTheIdentifierSlot) {
    auto pd = make_PropertyDeclaration();
    EXPECT_EQ(pd->NameToken(), pd.nameToken.get());
    EXPECT_EQ(pd->NameToken()->Name(), "P");
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

TEST(CSharp_PropertyDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    PropertyDeclaration pd;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    pd.ReturnType(type.get());
    EXPECT_EQ(pd.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &pd);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);  // attrCount (0) -- ReturnType at flattened index 0
}

TEST(CSharp_PropertyDeclaration, ReturnTypeSetterDetachesAndClears) {
    PropertyDeclaration pd;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    pd.ReturnType(a.get());
    EXPECT_EQ(a->Parent(), &pd);
    pd.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(pd.ReturnType(), b.get());
    pd.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(pd.ReturnType(), nullptr);
}

// ---- The NameToken slot (a single REQUIRED Identifier, index-less) --------

TEST(CSharp_PropertyDeclaration, NameTokenSetterReparents) {
    PropertyDeclaration pd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("P")));
    pd.NameToken(tok.get());
    EXPECT_EQ(pd.NameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &pd);
}

TEST(CSharp_PropertyDeclaration, NameTokenSetterDetachesAndClears) {
    PropertyDeclaration pd;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("P")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("Q")));
    pd.NameToken(a.get());
    EXPECT_EQ(a->Parent(), &pd);
    pd.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(pd.NameToken(), b.get());
    pd.NameToken(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(pd.NameToken(), nullptr);
}

// ---- The nullable slots (PrivateImplementationType/Getter/Setter/Initializer/ExpressionBody) -

TEST(CSharp_PropertyDeclaration, PrivateImplementationTypeSetterReparentsAndDetaches) {
    PropertyDeclaration pd;
    auto a = std::make_unique<SimpleType>(std::string("IFoo"));
    pd.PrivateImplementationType(a.get());
    EXPECT_EQ(pd.PrivateImplementationType(), a.get());
    EXPECT_EQ(a->Parent(), &pd);
    pd.PrivateImplementationType(nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(pd.PrivateImplementationType(), nullptr);
}

TEST(CSharp_PropertyDeclaration, GetterSetterReparentsAndDetaches) {
    PropertyDeclaration pd;
    auto g = std::make_unique<Accessor>(AccessorKind::Getter);
    pd.Getter(g.get());
    EXPECT_EQ(pd.Getter(), g.get());
    EXPECT_EQ(g->Parent(), &pd);
    pd.Getter(nullptr);
    EXPECT_EQ(g->Parent(), nullptr);
    EXPECT_EQ(pd.Getter(), nullptr);
}

TEST(CSharp_PropertyDeclaration, SetterSetterReparentsAndDetaches) {
    PropertyDeclaration pd;
    auto s = std::make_unique<Accessor>(AccessorKind::Setter);
    pd.Setter(s.get());
    EXPECT_EQ(pd.Setter(), s.get());
    EXPECT_EQ(s->Parent(), &pd);
    pd.Setter(nullptr);
    EXPECT_EQ(s->Parent(), nullptr);
    EXPECT_EQ(pd.Setter(), nullptr);
}

TEST(CSharp_PropertyDeclaration, InitializerSetterReparentsAndDetaches) {
    PropertyDeclaration pd;
    auto e = std::make_unique<PrimitiveExpression>(int32_t(10));
    pd.Initializer(e.get());
    EXPECT_EQ(pd.Initializer(), e.get());
    EXPECT_EQ(e->Parent(), &pd);
    pd.Initializer(nullptr);
    EXPECT_EQ(e->Parent(), nullptr);
    EXPECT_EQ(pd.Initializer(), nullptr);
}

TEST(CSharp_PropertyDeclaration, ExpressionBodySetterReparentsAndDetaches) {
    PropertyDeclaration pd;
    auto e = std::make_unique<PrimitiveExpression>(int32_t(42));
    pd.ExpressionBody(e.get());
    EXPECT_EQ(pd.ExpressionBody(), e.get());
    EXPECT_EQ(e->Parent(), &pd);
    pd.ExpressionBody(nullptr);
    EXPECT_EQ(e->Parent(), nullptr);
    EXPECT_EQ(pd.ExpressionBody(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_PropertyDeclaration, AttributesCollectionAddReparents) {
    PropertyDeclaration pd;
    auto attrSec = std::make_unique<AttributeSection>();
    pd.Attributes().Add(attrSec.get());
    EXPECT_EQ(pd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &pd);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, then the seven singles
// `ReturnType`/`NameToken`/`PrivateImplementationType`/`Getter`/`Setter`/`Initializer`/
// `ExpressionBody` at `attrCount` .. `attrCount + 6`.
TEST(CSharp_PropertyDeclaration, GetChildWalksSlots) {
    auto pd = make_PropertyDeclarationFull();
    // 0 attrs + 7 singles
    EXPECT_EQ(pd->GetChildCount(), 7);
    EXPECT_EQ(pd->GetChild(0), pd->ReturnType());
    EXPECT_EQ(pd->GetChild(1), pd->NameToken());
    EXPECT_EQ(pd->GetChild(2), pd->PrivateImplementationType());
    EXPECT_EQ(pd->GetChild(3), pd->Getter());
    EXPECT_EQ(pd->GetChild(4), pd->Setter());
    EXPECT_EQ(pd->GetChild(5), pd->Initializer());
    EXPECT_EQ(pd->GetChild(6), pd->ExpressionBody());
    EXPECT_THROW(pd->GetChild(7), std::out_of_range);
}

TEST(CSharp_PropertyDeclaration, GetChildSlotInfoWalksSlots) {
    auto pd = make_PropertyDeclarationFull();
    EXPECT_EQ(pd->GetChildSlotInfo(0), &pd->ReturnTypeSlot);
    EXPECT_EQ(pd->GetChildSlotInfo(1), &pd->NameTokenSlot);
    EXPECT_EQ(pd->GetChildSlotInfo(2), &pd->PrivateImplementationTypeSlot);
    EXPECT_EQ(pd->GetChildSlotInfo(3), &pd->GetterSlot);
    EXPECT_EQ(pd->GetChildSlotInfo(4), &pd->SetterSlot);
    EXPECT_EQ(pd->GetChildSlotInfo(5), &pd->InitializerSlot);
    EXPECT_EQ(pd->GetChildSlotInfo(6), &pd->ExpressionBodySlot);
    EXPECT_THROW(pd->GetChildSlotInfo(7), std::out_of_range);
}

TEST(CSharp_PropertyDeclaration, GetCollectionByKindReturnsAttributes) {
    PropertyDeclaration pd;
    EXPECT_EQ(pd.GetCollectionByKind(&Slots::AttributeSection), &pd.Attributes());
    EXPECT_EQ(pd.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(pd.GetCollectionByKind(&Slots::Identifier), nullptr);
}

// `SetChild` replaces the `ReturnType` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_PropertyDeclaration, SetChildReplacesReturnType) {
    auto pd = make_PropertyDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    auto* oldType = pd->ReturnType();
    pd->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(pd->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

// `SetChild` replaces the `NameToken` in place.
TEST(CSharp_PropertyDeclaration, SetChildReplacesNameToken) {
    auto pd = make_PropertyDeclaration();
    auto tok2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Q")));
    auto* oldTok = pd->NameToken();
    pd->SetChild(1, tok2.get());  // NameToken at flattened index 1 (0 attrs)
    EXPECT_EQ(pd->NameToken(), tok2.get());
    EXPECT_EQ(oldTok->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_PropertyDeclaration, SlotStaticsPointAtSharedKinds) {
    PropertyDeclaration pd;
    EXPECT_EQ(pd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(pd.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(pd.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(pd.PrivateImplementationTypeSlot.Kind(), &Slots::PrivateImplementationType);
    EXPECT_EQ(pd.GetterSlot.Kind(), &Slots::Getter);
    EXPECT_EQ(pd.SetterSlot.Kind(), &Slots::Setter);
    EXPECT_EQ(pd.InitializerSlot.Kind(), &Slots::Expression);
    EXPECT_EQ(pd.ExpressionBodySlot.Kind(), &Slots::ExpressionBody);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_PropertyDeclaration, SlotStaticsAreDistinct) {
    PropertyDeclaration pd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&pd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&pd.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&pd.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&pd.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&pd.GetterSlot),
              static_cast<const CSharpSlotInfo*>(&pd.SetterSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&pd.InitializerSlot),
              static_cast<const CSharpSlotInfo*>(&pd.ExpressionBodySlot));
}

// The `Getter`/`Setter` kinds (cycle-broken into `Accessor.hpp` this iteration) are distinct from
// each other and from the `Accessor` collection-agnostic kinds.
TEST(CSharp_PropertyDeclaration, GetterAndSetterKindsAreDistinct) {
    EXPECT_NE(&Slots::Getter, &Slots::Setter);
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Getter),
              static_cast<const CSharpSlotInfo*>(&Slots::Setter));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_PropertyDeclaration, AcceptVisitorDispatches) {
    PropertyDeclaration pd;
    RecordingVisitor v;
    pd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"property"}));
}

TEST(CSharp_PropertyDeclaration, AcceptVisitorVirtualThroughBase) {
    PropertyDeclaration pd;
    AstNode* node = &pd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"property"}));
}

TEST(CSharp_PropertyDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    PropertyDeclaration pd;
    EntityDeclaration* node = &pd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"property"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int`) and the `NameToken`
// (an `Identifier` `P`) in document order (Attributes -> ReturnType -> NameToken -> the five
// nullable singles, all absent here).
TEST(CSharp_PropertyDeclaration, DepthFirstWalk) {
    auto pd = make_PropertyDeclaration();
    RecordingVisitor v;
    pd->AcceptVisitor(v);
    // property -> simple:int (ReturnType) -> id:int -> id:P (NameToken)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "property", "simple:int", "id:int", "id:P",
    }));
}

// A `PropertyDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` and `NameToken` (the
// `Attributes` collection is the first slot).
TEST(CSharp_PropertyDeclaration, DepthFirstWalkWithAttribute) {
    auto pd = make_PropertyDeclarationWithAttribute();
    RecordingVisitor v;
    pd->AcceptVisitor(v);
    // property -> attrsec (Attributes[0]) -> attr -> simple:Foo (the Attribute's Type) -> id:Foo
    //      -> simple:int (ReturnType) -> id:int -> id:P (NameToken)
    ASSERT_EQ(v.trace.size(), 8u);
    EXPECT_EQ(v.trace[0], "property");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "id:P");
}

// A `PropertyDeclaration` with accessors recurses into the `Getter` accessor's body and the
// `Setter` accessor's body after the `PrivateImplementationType`, in slot order.
TEST(CSharp_PropertyDeclaration, DepthFirstWalkWithAccessors) {
    auto pd = make_PropertyDeclarationFull();
    RecordingVisitor v;
    pd->AcceptVisitor(v);
    // property -> simple:int (ReturnType) -> id:int -> id:P (NameToken)
    //      -> simple:IFoo (PrivateImplementationType) -> id:IFoo
    //      -> accessor:1 (Getter, AccessorKind::Getter==1) -> block -> return -> prim (return 5)
    //      -> accessor:2 (Setter, AccessorKind::Setter==2) -> block (empty)
    //      -> prim (Initializer 10) -> prim (ExpressionBody 42)
    ASSERT_EQ(v.trace.size(), 14u);
    EXPECT_EQ(v.trace[0], "property");
    EXPECT_EQ(v.trace[1], "simple:int");
    EXPECT_EQ(v.trace[2], "id:int");
    EXPECT_EQ(v.trace[3], "id:P");
    EXPECT_EQ(v.trace[4], "simple:IFoo");
    EXPECT_EQ(v.trace[5], "id:IFoo");
    EXPECT_EQ(v.trace[6], "accessor:1");  // Getter
    EXPECT_EQ(v.trace[7], "block");
    EXPECT_EQ(v.trace[8], "return");
    EXPECT_EQ(v.trace[9], "prim");
    EXPECT_EQ(v.trace[10], "accessor:2");  // Setter
    EXPECT_EQ(v.trace[11], "block");
    EXPECT_EQ(v.trace[12], "prim");  // Initializer
    EXPECT_EQ(v.trace[13], "prim");  // ExpressionBody
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_PropertyDeclaration, DoMatchSameNode) {
    auto a = make_PropertyDeclaration();
    auto b = make_PropertyDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask; `Static` matches only `Static`, not `Static|Public`).
TEST(CSharp_PropertyDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_PropertyDeclaration();
    auto b = make_PropertyDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_PropertyDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_PropertyDeclaration();
    auto b = make_PropertyDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `Name` `MatchString` term -- the property's name is part of the
// structural match).
TEST(CSharp_PropertyDeclaration, DoMatchNameMismatchRejects) {
    auto a = make_PropertyDeclaration();  // NameToken P
    PropertyHolder b;
    b.pd = std::make_unique<PropertyDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.pd->ReturnType(b.returnType.get());
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Q")));  // different name
    b.pd->NameToken(b.nameToken.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` `ReturnType` term).
TEST(CSharp_PropertyDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_PropertyDeclaration();  // ReturnType int
    PropertyHolder b;
    b.pd = std::make_unique<PropertyDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("byte"));  // different name
    b.pd->ReturnType(b.returnType.get());
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("P")));
    b.pd->NameToken(b.nameToken.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Getter` asymmetry rejects: a pattern with a `Getter` vs a candidate without one.
TEST(CSharp_PropertyDeclaration, DoMatchGetterAsymmetryRejects) {
    auto a = make_PropertyDeclaration();  // no Getter
    auto g = std::make_unique<Accessor>(AccessorKind::Getter);
    a->Getter(g.get());  // a has a Getter
    auto b = make_PropertyDeclaration();  // no Getter
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Setter` asymmetry rejects: a pattern without a `Setter` vs a candidate with one.
TEST(CSharp_PropertyDeclaration, DoMatchSetterAsymmetryRejects) {
    auto a = make_PropertyDeclaration();  // no Setter
    auto b = make_PropertyDeclaration();  // no Setter
    auto s = std::make_unique<Accessor>(AccessorKind::Setter);
    b->Setter(s.get());  // b has a Setter
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same accessor (same `Kind`) match.
TEST(CSharp_PropertyDeclaration, DoMatchSameGetterMatches) {
    auto a = make_PropertyDeclaration();
    auto ga = std::make_unique<Accessor>(AccessorKind::Getter);
    a->Getter(ga.get());
    auto b = make_PropertyDeclaration();
    auto gb = std::make_unique<Accessor>(AccessorKind::Getter);
    b->Getter(gb.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Getter` `Kind` mismatch rejects (the `Accessor.DoMatch` `Any`-wildcard on `Kind`).
TEST(CSharp_PropertyDeclaration, DoMatchGetterKindMismatchRejects) {
    auto a = make_PropertyDeclaration();
    auto ga = std::make_unique<Accessor>(AccessorKind::Getter);
    a->Getter(ga.get());
    auto b = make_PropertyDeclaration();
    auto gb = std::make_unique<Accessor>(AccessorKind::Setter);  // different Kind
    b->Getter(gb.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `Initializer` asymmetry rejects: a pattern with an `Initializer` vs a candidate without one.
TEST(CSharp_PropertyDeclaration, DoMatchInitializerAsymmetryRejects) {
    auto a = make_PropertyDeclaration();
    auto e = std::make_unique<PrimitiveExpression>(int32_t(10));
    a->Initializer(e.get());  // a has an Initializer
    auto b = make_PropertyDeclaration();  // no Initializer
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`PropertyDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not a `PropertyDeclaration`).
TEST(CSharp_PropertyDeclaration, DoMatchRejectsNonPropertyEntityDeclaration) {
    auto a = make_PropertyDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_PropertyDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_PropertyDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_PropertyDeclaration, DoMatchRejectsNull) {
    auto a = make_PropertyDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`NameToken`/`Attributes` and the five nullable slots, copies
// the `Modifiers` scalar, and does not detach the source.
TEST(CSharp_PropertyDeclaration, CloneDeepCopies) {
    auto a = make_PropertyDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<PropertyDeclaration>(
        static_cast<PropertyDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Property);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    EXPECT_EQ(clone->NameToken()->Name(), "P");
    ASSERT_NE(clone->PrivateImplementationType(), nullptr);
    EXPECT_NE(clone->PrivateImplementationType(), a->PrivateImplementationType());
    ASSERT_NE(clone->Getter(), nullptr);
    EXPECT_NE(clone->Getter(), a->Getter());
    ASSERT_NE(clone->Setter(), nullptr);
    EXPECT_NE(clone->Setter(), a->Setter());
    ASSERT_NE(clone->Initializer(), nullptr);
    EXPECT_NE(clone->Initializer(), a->Initializer());
    ASSERT_NE(clone->ExpressionBody(), nullptr);
    EXPECT_NE(clone->ExpressionBody(), a->ExpressionBody());
    // The source is not detached.
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
    EXPECT_EQ(a->Getter()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `PropertyDeclaration*`.
TEST(CSharp_PropertyDeclaration, CloneVirtualAndCovariant) {
    auto a = make_PropertyDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<PropertyDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<PropertyDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_PropertyDeclaration, CloneCopiesAttributes) {
    auto a = make_PropertyDeclarationWithAttribute();
    auto clone = std::unique_ptr<PropertyDeclaration>(
        static_cast<PropertyDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// `Clone` skips absent nullable slots (a property with no accessors/initializer/expression body
// clones without them).
TEST(CSharp_PropertyDeclaration, CloneSkipsAbsentNullableSlots) {
    auto a = make_PropertyDeclaration();  // no accessors/initializer/expression body
    auto clone = std::unique_ptr<PropertyDeclaration>(
        static_cast<PropertyDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->PrivateImplementationType(), nullptr);
    EXPECT_EQ(clone->Getter(), nullptr);
    EXPECT_EQ(clone->Setter(), nullptr);
    EXPECT_EQ(clone->Initializer(), nullptr);
    EXPECT_EQ(clone->ExpressionBody(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
    ASSERT_NE(clone->NameToken(), nullptr);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `ReturnType`/`NameToken` required slots are
// filled).
TEST(CSharp_PropertyDeclaration, CheckInvariantPassesOnFilled) {
    auto pd = make_PropertyDeclaration();  // ReturnType + NameToken filled
    pd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_PropertyDeclaration, CheckInvariantPassesOnFull) {
    auto pd = make_PropertyDeclarationFull();
    pd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `ReturnType`/`NameToken` are REQUIRED slots,
// so a default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_PropertyDeclaration, CheckInvariantRejectsEmpty) {
    PropertyDeclaration pd;  // no ReturnType, no NameToken
    EXPECT_DEATH(pd.CheckInvariant(), "");
}
#endif
