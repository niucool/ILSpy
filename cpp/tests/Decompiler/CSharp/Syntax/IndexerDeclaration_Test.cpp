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

// Tests for the `IndexerDeclaration` concrete node (cpp/.../Syntax/IndexerDeclaration.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/IndexerDeclaration.cs) -- the ninth
// concrete `TypeMember` and the first `EntityDeclaration` with a `Parameters` collection (the next
// in-order Phase-5 piece per the D278 plan, now unblocked by `ParameterDeclaration` D278 for the
// `Parameters` slot). The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234
// pattern).

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
#include "Decompiler/CSharp/Syntax/IndexerDeclaration.hpp"
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

// A recording depth-first visitor: overrides the `VisitIndexerDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`PrimitiveExpression`/`Accessor`/
// `BlockStatement`/`ReturnStatement`/`ParameterDeclaration` of its slots, and the
// `DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch rejections), recording
// a tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitIndexerDeclaration(IndexerDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-indexer>"); return; }
        trace.push_back("indexer");
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
    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-param>"); return; }
        trace.push_back("param");
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

// A holder keeping an `IndexerDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes).
struct IndexerHolder {
    std::unique_ptr<IndexerDeclaration> id;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<SimpleType> privateImplType;
    // One parameter "int x" (kept alive with its own Type + NameToken).
    std::unique_ptr<ParameterDeclaration> param0;
    std::unique_ptr<SimpleType> param0Type;
    std::unique_ptr<Identifier> param0NameToken;
    std::unique_ptr<Accessor> getter;
    std::unique_ptr<BlockStatement> getterBody;
    std::unique_ptr<ReturnStatement> getterReturn;
    std::unique_ptr<PrimitiveExpression> getterReturnExpr;
    std::unique_ptr<Accessor> setter;
    std::unique_ptr<BlockStatement> setterBody;
    std::unique_ptr<PrimitiveExpression> expressionBody;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    IndexerDeclaration* get() const { return id.get(); }
    IndexerDeclaration* operator->() const { return id.get(); }
};

// Build an `IndexerDeclaration` with a `ReturnType` `SimpleType` `int` (no attributes, no
// `PrivateImplementationType`, no `Parameters`, no accessors, no expression body). The holder
// keeps every node alive.
IndexerHolder make_IndexerDeclaration() {
    IndexerHolder h;
    h.id = std::make_unique<IndexerDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.id->ReturnType(h.returnType.get());
    return h;
}

// Build an `IndexerDeclaration` with a `ReturnType` `int` and an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
IndexerHolder make_IndexerDeclarationWithAttribute() {
    IndexerHolder h;
    h.id = std::make_unique<IndexerDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.id->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.id->ReturnType(h.returnType.get());
    return h;
}

// Build an `IndexerDeclaration` with a `ReturnType` `int`, a `PrivateImplementationType` `IFoo`,
// a `Parameters` collection holding one `ParameterDeclaration` "int x", a `Getter` accessor
// (whose `Body` is a `BlockStatement` `{ return x; }`), a `Setter` accessor (whose `Body` is an
// empty `BlockStatement`), and an `ExpressionBody` `PrimitiveExpression` `42` -- every slot
// filled.
IndexerHolder make_IndexerDeclarationFull() {
    IndexerHolder h;
    h.id = std::make_unique<IndexerDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.id->ReturnType(h.returnType.get());
    h.privateImplType = std::make_unique<SimpleType>(std::string("IFoo"));
    h.id->PrivateImplementationType(h.privateImplType.get());
    // One parameter "int x"
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.param0Type = std::make_unique<SimpleType>(std::string("int"));
    h.param0->Type(h.param0Type.get());
    h.param0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param0->NameToken(h.param0NameToken.get());
    h.id->Parameters().Add(h.param0.get());
    // Getter accessor with a body { return x; }
    h.getterReturnExpr = std::make_unique<PrimitiveExpression>(int32_t(5));
    h.getterReturn = std::make_unique<ReturnStatement>(h.getterReturnExpr.get());
    h.getterBody = std::make_unique<BlockStatement>();
    h.getterBody->Statements().Add(h.getterReturn.get());
    h.getter = std::make_unique<Accessor>(AccessorKind::Getter);
    h.getter->Body(h.getterBody.get());
    h.id->Getter(h.getter.get());
    // Setter accessor with an empty body
    h.setterBody = std::make_unique<BlockStatement>();
    h.setter = std::make_unique<Accessor>(AccessorKind::Setter);
    h.setter->Body(h.setterBody.get());
    h.id->Setter(h.setter.get());
    h.expressionBody = std::make_unique<PrimitiveExpression>(int32_t(42));
    h.id->ExpressionBody(h.expressionBody.get());
    return h;
}

} // namespace

// ==========================================================================
// IndexerDeclaration (the indexer_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `IndexerDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_IndexerDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<IndexerDeclaration>);
}

// `IndexerDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_IndexerDeclaration, IsEntityDeclarationAndAstNode) {
    IndexerDeclaration id;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&id), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&id), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&id), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&id), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&id), nullptr);
}

// ---- The ThisKeyword const ----------------------------------------------

TEST(CSharp_IndexerDeclaration, ThisKeywordConstIsThis) {
    EXPECT_STREQ(IndexerDeclaration::ThisKeyword, "this");
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `ReturnType`, empty `Attributes`/`Parameters`, all four nullable slots null.
// `GetChildCount` is `0 + 0 + 5 = 5` (the two empty collections plus the five single slots -- each
// single slot contributes 1 to the flattened count even when absent). The `Modifiers` defaults to
// `None`; `SymbolKind` is `Indexer`.
TEST(CSharp_IndexerDeclaration, EmptyCtor) {
    IndexerDeclaration id;
    EXPECT_EQ(id.SymbolKind(), SymbolKind::Indexer);
    EXPECT_EQ(id.Modifiers(), Modifiers::None);
    EXPECT_EQ(id.ReturnType(), nullptr);
    EXPECT_EQ(id.NameToken(), nullptr);
    EXPECT_EQ(id.PrivateImplementationType(), nullptr);
    EXPECT_EQ(id.Parameters().Count(), 0);
    EXPECT_EQ(id.Getter(), nullptr);
    EXPECT_EQ(id.Setter(), nullptr);
    EXPECT_EQ(id.ExpressionBody(), nullptr);
    EXPECT_EQ(id.Attributes().Count(), 0);
    EXPECT_EQ(id.GetChildCount(), 5);  // 0 attrs + 0 params + 5 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_IndexerDeclaration, SymbolKindOverrideReturnsIndexer) {
    IndexerDeclaration id;
    EXPECT_EQ(id.SymbolKind(), SymbolKind::Indexer);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_IndexerDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    IndexerDeclaration id;
    EXPECT_EQ(id.Modifiers(), Modifiers::None);
    id.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(id.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_IndexerDeclaration, HasModifierIsBitmaskTest) {
    IndexerDeclaration id;
    id.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(id.HasModifier(Modifiers::Static));
    EXPECT_TRUE(id.HasModifier(Modifiers::Public));
    EXPECT_FALSE(id.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken overrides (an indexer has no name token) ----------

// `Name` returns the fixed literal `"Item"` (the conventional indexer name).
TEST(CSharp_IndexerDeclaration, NameReturnsItem) {
    IndexerDeclaration id;
    EXPECT_EQ(id.Name(), "Item");
}

// `Name` setter throws (`NotSupportedException` ports as `std::logic_error`).
TEST(CSharp_IndexerDeclaration, NameSetterThrows) {
    IndexerDeclaration id;
    EXPECT_THROW(id.Name(std::string_view("x")), std::logic_error);
}

// `NameToken` returns null (an indexer has no name token).
TEST(CSharp_IndexerDeclaration, NameTokenReturnsNull) {
    IndexerDeclaration id;
    EXPECT_EQ(id.NameToken(), nullptr);
}

// `NameToken` setter throws.
TEST(CSharp_IndexerDeclaration, NameTokenSetterThrows) {
    IndexerDeclaration id;
    EXPECT_THROW(id.NameToken(nullptr), std::logic_error);
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

TEST(CSharp_IndexerDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    IndexerDeclaration id;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    id.ReturnType(type.get());
    EXPECT_EQ(id.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &id);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);  // attrCount (0) -- ReturnType at flattened index 0
}

TEST(CSharp_IndexerDeclaration, ReturnTypeSetterDetachesAndClears) {
    IndexerDeclaration id;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    id.ReturnType(a.get());
    EXPECT_EQ(a->Parent(), &id);
    id.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(id.ReturnType(), b.get());
    id.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(id.ReturnType(), nullptr);
}

// ---- The nullable slots (PrivateImplementationType/Getter/Setter/ExpressionBody) -

TEST(CSharp_IndexerDeclaration, PrivateImplementationTypeSetterReparentsAndDetaches) {
    IndexerDeclaration id;
    auto a = std::make_unique<SimpleType>(std::string("IFoo"));
    id.PrivateImplementationType(a.get());
    EXPECT_EQ(id.PrivateImplementationType(), a.get());
    EXPECT_EQ(a->Parent(), &id);
    id.PrivateImplementationType(nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(id.PrivateImplementationType(), nullptr);
}

TEST(CSharp_IndexerDeclaration, GetterSetterReparentsAndDetaches) {
    IndexerDeclaration id;
    auto g = std::make_unique<Accessor>(AccessorKind::Getter);
    id.Getter(g.get());
    EXPECT_EQ(id.Getter(), g.get());
    EXPECT_EQ(g->Parent(), &id);
    id.Getter(nullptr);
    EXPECT_EQ(g->Parent(), nullptr);
    EXPECT_EQ(id.Getter(), nullptr);
}

TEST(CSharp_IndexerDeclaration, SetterSetterReparentsAndDetaches) {
    IndexerDeclaration id;
    auto s = std::make_unique<Accessor>(AccessorKind::Setter);
    id.Setter(s.get());
    EXPECT_EQ(id.Setter(), s.get());
    EXPECT_EQ(s->Parent(), &id);
    id.Setter(nullptr);
    EXPECT_EQ(s->Parent(), nullptr);
    EXPECT_EQ(id.Setter(), nullptr);
}

TEST(CSharp_IndexerDeclaration, ExpressionBodySetterReparentsAndDetaches) {
    IndexerDeclaration id;
    auto e = std::make_unique<PrimitiveExpression>(int32_t(42));
    id.ExpressionBody(e.get());
    EXPECT_EQ(id.ExpressionBody(), e.get());
    EXPECT_EQ(e->Parent(), &id);
    id.ExpressionBody(nullptr);
    EXPECT_EQ(e->Parent(), nullptr);
    EXPECT_EQ(id.ExpressionBody(), nullptr);
}

// ---- The Parameters collection (non-incremental) ------------------------

TEST(CSharp_IndexerDeclaration, ParametersCollectionAddReparents) {
    IndexerDeclaration id;
    auto p = std::make_unique<ParameterDeclaration>();
    id.Parameters().Add(p.get());
    EXPECT_EQ(id.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &id);
}

TEST(CSharp_IndexerDeclaration, ParametersCollectionAddReindexesDynamically) {
    IndexerDeclaration id;
    auto p0 = std::make_unique<ParameterDeclaration>();
    auto p1 = std::make_unique<ParameterDeclaration>();
    id.Parameters().Add(p0.get());
    id.Parameters().Add(p1.get());
    // The collection is non-incremental (the node has two collections), so the flattened
    // `ChildIndex` is dynamic -- trigger the reindex via `Slot()`.
    (void)p0->Slot();
    EXPECT_EQ(p0->ChildIndex, 2);  // attrCount (0) + 2 singles (ReturnType, PrivateImpl) + 0
    EXPECT_EQ(p1->ChildIndex, 3);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_IndexerDeclaration, AttributesCollectionAddReparents) {
    IndexerDeclaration id;
    auto attrSec = std::make_unique<AttributeSection>();
    id.Attributes().Add(attrSec.get());
    EXPECT_EQ(id.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &id);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `ReturnType` single
// at `attrCount`, the `PrivateImplementationType` single at `attrCount + 1`, the `Parameters`
// collection `[attrCount + 2, attrCount + 2 + paramCount)`, the `Getter` single at
// `attrCount + 2 + paramCount`, the `Setter` single at `attrCount + 3 + paramCount`, the
// `ExpressionBody` single at `attrCount + 4 + paramCount`.
TEST(CSharp_IndexerDeclaration, GetChildWalksSlots) {
    auto id = make_IndexerDeclarationFull();
    // 0 attrs + 1 param + 5 singles
    EXPECT_EQ(id->GetChildCount(), 6);
    EXPECT_EQ(id->GetChild(0), id->ReturnType());               // 0
    EXPECT_EQ(id->GetChild(1), id->PrivateImplementationType());  // 1
    EXPECT_EQ(id->GetChild(2), id->Parameters().At(0));         // 2 (the param)
    EXPECT_EQ(id->GetChild(3), id->Getter());                   // 3
    EXPECT_EQ(id->GetChild(4), id->Setter());                   // 4
    EXPECT_EQ(id->GetChild(5), id->ExpressionBody());           // 5
    EXPECT_THROW(id->GetChild(6), std::out_of_range);
}

TEST(CSharp_IndexerDeclaration, GetChildSlotInfoWalksSlots) {
    auto id = make_IndexerDeclarationFull();
    EXPECT_EQ(id->GetChildSlotInfo(0), &id->ReturnTypeSlot);
    EXPECT_EQ(id->GetChildSlotInfo(1), &id->PrivateImplementationTypeSlot);
    EXPECT_EQ(id->GetChildSlotInfo(2), &id->ParametersSlot);
    EXPECT_EQ(id->GetChildSlotInfo(3), &id->GetterSlot);
    EXPECT_EQ(id->GetChildSlotInfo(4), &id->SetterSlot);
    EXPECT_EQ(id->GetChildSlotInfo(5), &id->ExpressionBodySlot);
    EXPECT_THROW(id->GetChildSlotInfo(6), std::out_of_range);
}

TEST(CSharp_IndexerDeclaration, GetCollectionByKindReturnsAttributesAndParameters) {
    IndexerDeclaration id;
    EXPECT_EQ(id.GetCollectionByKind(&Slots::AttributeSection), &id.Attributes());
    EXPECT_EQ(id.GetCollectionByKind(&Slots::Parameter), &id.Parameters());
    EXPECT_EQ(id.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(id.GetCollectionByKind(&Slots::Identifier), nullptr);
}

// `SetChild` replaces the `ReturnType` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_IndexerDeclaration, SetChildReplacesReturnType) {
    auto id = make_IndexerDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    auto* oldType = id->ReturnType();
    id->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(id->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

// `SetChild` replaces a `Parameters` element in place.
TEST(CSharp_IndexerDeclaration, SetChildReplacesParameter) {
    auto id = make_IndexerDeclarationFull();
    auto p2 = std::make_unique<ParameterDeclaration>();
    auto* oldParam = id->Parameters().At(0);
    id->SetChild(2, p2.get());  // Parameters[0] at flattened index 2 (0 attrs + 2 singles)
    EXPECT_EQ(id->Parameters().At(0), p2.get());
    EXPECT_EQ(oldParam->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_IndexerDeclaration, SlotStaticsPointAtSharedKinds) {
    IndexerDeclaration id;
    EXPECT_EQ(id.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(id.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(id.PrivateImplementationTypeSlot.Kind(), &Slots::PrivateImplementationType);
    EXPECT_EQ(id.ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(id.GetterSlot.Kind(), &Slots::Getter);
    EXPECT_EQ(id.SetterSlot.Kind(), &Slots::Setter);
    EXPECT_EQ(id.ExpressionBodySlot.Kind(), &Slots::ExpressionBody);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_IndexerDeclaration, SlotStaticsAreDistinct) {
    IndexerDeclaration id;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&id.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&id.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&id.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&id.PrivateImplementationTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&id.ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&id.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&id.GetterSlot),
              static_cast<const CSharpSlotInfo*>(&id.SetterSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&id.SetterSlot),
              static_cast<const CSharpSlotInfo*>(&id.ExpressionBodySlot));
}

// The new `Slots::Parameter` kind (cycle-broken into `ParameterDeclaration.hpp` this iteration) is
// distinct from the other collection kinds (cast both to the common `CSharpSlotInfo*` base for
// the cross-element-type `EXPECT_NE`, the D251/D252/D262 precedent).
TEST(CSharp_IndexerDeclaration, ParameterKindIsDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Parameter),
              static_cast<const CSharpSlotInfo*>(&Slots::AttributeSection));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Parameter),
              static_cast<const CSharpSlotInfo*>(&Slots::Type));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Parameter),
              static_cast<const CSharpSlotInfo*>(&Slots::Variable));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_IndexerDeclaration, AcceptVisitorDispatches) {
    IndexerDeclaration id;
    RecordingVisitor v;
    id.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"indexer"}));
}

TEST(CSharp_IndexerDeclaration, AcceptVisitorVirtualThroughBase) {
    IndexerDeclaration id;
    AstNode* node = &id;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"indexer"}));
}

TEST(CSharp_IndexerDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    IndexerDeclaration id;
    EntityDeclaration* node = &id;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"indexer"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int`) in document order
// (no attributes, no `PrivateImplementationType`, no `Parameters`, no accessors, no expression
// body). An indexer has no `NameToken`, so the walk does NOT recurse into one (unlike
// `PropertyDeclaration`).
TEST(CSharp_IndexerDeclaration, DepthFirstWalk) {
    auto id = make_IndexerDeclaration();
    RecordingVisitor v;
    id->AcceptVisitor(v);
    // indexer -> simple:int (ReturnType) -> id:int
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "indexer", "simple:int", "id:int",
    }));
}

// An `IndexerDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` (the `Attributes`
// collection is the first slot).
TEST(CSharp_IndexerDeclaration, DepthFirstWalkWithAttribute) {
    auto id = make_IndexerDeclarationWithAttribute();
    RecordingVisitor v;
    id->AcceptVisitor(v);
    // indexer -> attrsec (Attributes[0]) -> attr -> simple:Foo -> id:Foo
    //      -> simple:int (ReturnType) -> id:int
    ASSERT_EQ(v.trace.size(), 7u);
    EXPECT_EQ(v.trace[0], "indexer");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
}

// An `IndexerDeclaration` with every slot filled recurses into the `ReturnType`, the
// `PrivateImplementationType`, each `Parameter` (and its `Type`/`NameToken`), the `Getter`/`Setter`
// accessors' bodies, and the `ExpressionBody`, in slot order.
TEST(CSharp_IndexerDeclaration, DepthFirstWalkFull) {
    auto id = make_IndexerDeclarationFull();
    RecordingVisitor v;
    id->AcceptVisitor(v);
    // indexer -> simple:int (ReturnType) -> id:int
    //      -> simple:IFoo (PrivateImplementationType) -> id:IFoo
    //      -> param (Parameters[0]) -> simple:int (param's Type) -> id:int -> id:x (NameToken)
    //      -> accessor:1 (Getter) -> block -> return -> prim (return 5)
    //      -> accessor:2 (Setter) -> block (empty)
    //      -> prim (ExpressionBody 42)
    ASSERT_EQ(v.trace.size(), 16u);
    EXPECT_EQ(v.trace[0], "indexer");
    EXPECT_EQ(v.trace[1], "simple:int");
    EXPECT_EQ(v.trace[2], "id:int");
    EXPECT_EQ(v.trace[3], "simple:IFoo");
    EXPECT_EQ(v.trace[4], "id:IFoo");
    EXPECT_EQ(v.trace[5], "param");
    EXPECT_EQ(v.trace[6], "simple:int");
    EXPECT_EQ(v.trace[7], "id:int");
    EXPECT_EQ(v.trace[8], "id:x");
    EXPECT_EQ(v.trace[9], "accessor:1");  // Getter
    EXPECT_EQ(v.trace[10], "block");
    EXPECT_EQ(v.trace[11], "return");
    EXPECT_EQ(v.trace[12], "prim");
    EXPECT_EQ(v.trace[13], "accessor:2");  // Setter
    EXPECT_EQ(v.trace[14], "block");
    EXPECT_EQ(v.trace[15], "prim");  // ExpressionBody
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_IndexerDeclaration, DoMatchSameNode) {
    auto a = make_IndexerDeclaration();
    auto b = make_IndexerDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_IndexerDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_IndexerDeclaration();
    auto b = make_IndexerDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_IndexerDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_IndexerDeclaration();
    auto b = make_IndexerDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// The `Name` term is a `MatchString` over the always-`"Item"` `Name` (the override returns the
// fixed literal), so it vacuously matches `"Item"` vs `"Item"` for every pair of indexers -- the
// `Name` term never rejects (the `FieldDeclaration` D273 always-empty-`Name` precedent applied to
// a fixed-const `Name`).
TEST(CSharp_IndexerDeclaration, DoMatchNameIsAlwaysItemAndVacuous) {
    auto a = make_IndexerDeclaration();
    auto b = make_IndexerDeclaration();
    EXPECT_EQ(a->Name(), "Item");
    EXPECT_EQ(b->Name(), "Item");
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` `ReturnType` term).
TEST(CSharp_IndexerDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_IndexerDeclaration();  // ReturnType int
    IndexerHolder b;
    b.id = std::make_unique<IndexerDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("byte"));  // different type
    b.id->ReturnType(b.returnType.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `PrivateImplementationType` asymmetry rejects: a pattern with one vs a candidate without.
TEST(CSharp_IndexerDeclaration, DoMatchPrivateImplementationTypeAsymmetryRejects) {
    auto a = make_IndexerDeclaration();
    auto pit = std::make_unique<SimpleType>(std::string("IFoo"));
    a->PrivateImplementationType(pit.get());  // a has a PrivateImplementationType
    auto b = make_IndexerDeclaration();  // no PrivateImplementationType
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Parameters` mismatch rejects (the collection recursive `DoMatch` -- a pattern with a
// parameter vs a candidate without one).
TEST(CSharp_IndexerDeclaration, DoMatchParametersMismatchRejects) {
    auto a = make_IndexerDeclarationFull();  // one parameter
    auto b = make_IndexerDeclaration();  // no parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single parameter match (the collection recursive `DoMatch` accepts
// equal-length collections whose elements match).
TEST(CSharp_IndexerDeclaration, DoMatchSameParametersMatch) {
    auto a = make_IndexerDeclaration();
    auto ap = std::make_unique<ParameterDeclaration>();
    auto apt = std::make_unique<SimpleType>(std::string("int"));
    ap->Type(apt.get());
    a->Parameters().Add(ap.get());
    auto b = make_IndexerDeclaration();
    auto bp = std::make_unique<ParameterDeclaration>();
    auto bpt = std::make_unique<SimpleType>(std::string("int"));
    bp->Type(bpt.get());
    b->Parameters().Add(bp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Getter` asymmetry rejects: a pattern with a `Getter` vs a candidate without one.
TEST(CSharp_IndexerDeclaration, DoMatchGetterAsymmetryRejects) {
    auto a = make_IndexerDeclaration();
    auto g = std::make_unique<Accessor>(AccessorKind::Getter);
    a->Getter(g.get());
    auto b = make_IndexerDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Setter` asymmetry rejects: a pattern without a `Setter` vs a candidate with one.
TEST(CSharp_IndexerDeclaration, DoMatchSetterAsymmetryRejects) {
    auto a = make_IndexerDeclaration();
    auto b = make_IndexerDeclaration();
    auto s = std::make_unique<Accessor>(AccessorKind::Setter);
    b->Setter(s.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `ExpressionBody` asymmetry rejects: a pattern with an `ExpressionBody` vs a candidate without.
TEST(CSharp_IndexerDeclaration, DoMatchExpressionBodyAsymmetryRejects) {
    auto a = make_IndexerDeclaration();
    auto e = std::make_unique<PrimitiveExpression>(int32_t(42));
    a->ExpressionBody(e.get());
    auto b = make_IndexerDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`IndexerDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not an `IndexerDeclaration`).
TEST(CSharp_IndexerDeclaration, DoMatchRejectsNonIndexerEntityDeclaration) {
    auto a = make_IndexerDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_IndexerDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_IndexerDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_IndexerDeclaration, DoMatchRejectsNull) {
    auto a = make_IndexerDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`PrivateImplementationType`/`Parameters`/`Getter`/`Setter`/
// `ExpressionBody`, copies the `Modifiers` scalar, and does not detach the source.
TEST(CSharp_IndexerDeclaration, CloneDeepCopies) {
    auto a = make_IndexerDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<IndexerDeclaration>(
        static_cast<IndexerDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Indexer);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->Name(), "Item");
    EXPECT_EQ(clone->NameToken(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    ASSERT_NE(clone->PrivateImplementationType(), nullptr);
    EXPECT_NE(clone->PrivateImplementationType(), a->PrivateImplementationType());
    ASSERT_EQ(clone->Parameters().Count(), 1u);
    EXPECT_NE(clone->Parameters().At(0), a->Parameters().At(0));
    ASSERT_NE(clone->Getter(), nullptr);
    EXPECT_NE(clone->Getter(), a->Getter());
    ASSERT_NE(clone->Setter(), nullptr);
    EXPECT_NE(clone->Setter(), a->Setter());
    ASSERT_NE(clone->ExpressionBody(), nullptr);
    EXPECT_NE(clone->ExpressionBody(), a->ExpressionBody());
    // The source is not detached.
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
    EXPECT_EQ(a->Getter()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `IndexerDeclaration*`.
TEST(CSharp_IndexerDeclaration, CloneVirtualAndCovariant) {
    auto a = make_IndexerDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<IndexerDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<IndexerDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_IndexerDeclaration, CloneCopiesAttributes) {
    auto a = make_IndexerDeclarationWithAttribute();
    auto clone = std::unique_ptr<IndexerDeclaration>(
        static_cast<IndexerDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// `Clone` skips absent nullable slots (an indexer with no `PrivateImplementationType`/accessors/
// expression body clones without them).
TEST(CSharp_IndexerDeclaration, CloneSkipsAbsentNullableSlots) {
    auto a = make_IndexerDeclaration();  // no PrivateImplementationType/Parameters/accessors/exbody
    auto clone = std::unique_ptr<IndexerDeclaration>(
        static_cast<IndexerDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->PrivateImplementationType(), nullptr);
    EXPECT_EQ(clone->Parameters().Count(), 0u);
    EXPECT_EQ(clone->Getter(), nullptr);
    EXPECT_EQ(clone->Setter(), nullptr);
    EXPECT_EQ(clone->ExpressionBody(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
}

// `Clone` copies the `Parameters` collection (each element deep-cloned).
TEST(CSharp_IndexerDeclaration, CloneCopiesParameters) {
    auto a = make_IndexerDeclarationFull();
    auto clone = std::unique_ptr<IndexerDeclaration>(
        static_cast<IndexerDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Parameters().Count(), 1u);
    auto* clonedParam = clone->Parameters().At(0);
    EXPECT_NE(clonedParam, a->Parameters().At(0));
    ASSERT_NE(clonedParam->Type(), nullptr);
    EXPECT_NE(clonedParam->Type(), a->Parameters().At(0)->Type());
    ASSERT_NE(clonedParam->NameToken(), nullptr);
    EXPECT_NE(clonedParam->NameToken(), a->Parameters().At(0)->NameToken());
    EXPECT_EQ(clonedParam->NameToken()->Name(), "x");
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `ReturnType` required slot is filled).
TEST(CSharp_IndexerDeclaration, CheckInvariantPassesOnFilled) {
    auto id = make_IndexerDeclaration();  // ReturnType filled
    id->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_IndexerDeclaration, CheckInvariantPassesOnFull) {
    auto id = make_IndexerDeclarationFull();
    id->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `ReturnType` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_IndexerDeclaration, CheckInvariantRejectsEmpty) {
    IndexerDeclaration id;  // no ReturnType
    EXPECT_DEATH(id.CheckInvariant(), "");
}
#endif
