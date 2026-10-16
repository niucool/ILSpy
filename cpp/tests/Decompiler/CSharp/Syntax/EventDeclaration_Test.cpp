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

// Tests for the `EventDeclaration` and `CustomEventDeclaration` concrete nodes
// (cpp/.../Syntax/EventDeclaration.hpp + CustomEventDeclaration.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/EventDeclaration.cs) -- the sixth and seventh
// concrete `TypeMember` nodes, the next in-order Phase-5 piece per the D276 plan (now unblocked by
// `Accessor` D274 for the `AddAccessor`/`RemoveAccessor` slots). `EventDeclaration` is the
// field-like event (`Attributes` collection + `ReturnType` single + `Variables` collection, the
// `FieldDeclaration` D273 shape); `CustomEventDeclaration` is the property-like event
// (`Attributes` collection + `ReturnType` + `PrivateImplementationType` + `NameToken` +
// `AddAccessor` + `RemoveAccessor`, the `PropertyDeclaration` D276 collection + trailing-singles
// shape). The two suites share a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234
// multi-suite pattern).

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
#include "Decompiler/CSharp/Syntax/CustomEventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitEventDeclaration`/
// `VisitCustomEventDeclaration` under test (plus the `AttributeSection`/`Attribute`/`SimpleType`/
// `Identifier`/`VariableInitializer`/`PrimitiveExpression`/`Accessor`/`BlockStatement`/
// `ReturnStatement` of their slots, and the `DestructorDeclaration`/`WhileStatement` used for the
// cross-type DoMatch rejections), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitEventDeclaration(EventDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-event>"); return; }
        trace.push_back("event");
        VisitChildren(node);
    }
    void VisitCustomEventDeclaration(CustomEventDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-customevent>"); return; }
        trace.push_back("customevent:" + node->Name());
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
    void VisitVariableInitializer(VariableInitializer* node) override {
        if (node == nullptr) { trace.push_back("<null-varinit>"); return; }
        trace.push_back("varinit:" + node->Name());
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

// ---- EventDeclaration holders ---------------------------------------------

// A holder keeping an `EventDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes). An `EventDeclaration` with a `ReturnType`
// `SimpleType` `int` and a `Variables` collection of one `VariableInitializer` `x` (and, for the
// attribute variant, an `Attributes` `AttributeSection` whose `Attributes` hold an `Attribute`
// whose `Type` is a `SimpleType` `Foo`).
struct EventHolder {
    std::unique_ptr<EventDeclaration> ed;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<VariableInitializer> varX;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    EventDeclaration* get() const { return ed.get(); }
    EventDeclaration* operator->() const { return ed.get(); }
};

// Build an `EventDeclaration` with a `ReturnType` `SimpleType` `int` and a single `Variable` `x`
// (no initializer, no attributes). The holder keeps every node alive.
EventHolder make_EventDeclaration() {
    EventHolder h;
    h.ed = std::make_unique<EventDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.ed->ReturnType(h.returnType.get());
    h.varX = std::make_unique<VariableInitializer>(std::string("x"));
    h.ed->Variables().Add(h.varX.get());
    return h;
}

// Build an `EventDeclaration` with a `ReturnType` `SimpleType` `int`, a single `Variable` `x`, and
// an `Attributes` `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
EventHolder make_EventDeclarationWithAttribute() {
    EventHolder h;
    h.ed = std::make_unique<EventDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.ed->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.ed->ReturnType(h.returnType.get());
    h.varX = std::make_unique<VariableInitializer>(std::string("x"));
    h.ed->Variables().Add(h.varX.get());
    return h;
}

// ---- CustomEventDeclaration holders --------------------------------------

// A holder keeping a `CustomEventDeclaration` and all its children alive in the test scope.
struct CustomEventHolder {
    std::unique_ptr<CustomEventDeclaration> ced;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<SimpleType> privateImplType;
    std::unique_ptr<Accessor> addAccessor;
    std::unique_ptr<BlockStatement> addBody;
    std::unique_ptr<ReturnStatement> addReturn;
    std::unique_ptr<PrimitiveExpression> addReturnExpr;
    std::unique_ptr<Accessor> removeAccessor;
    std::unique_ptr<BlockStatement> removeBody;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    CustomEventDeclaration* get() const { return ced.get(); }
    CustomEventDeclaration* operator->() const { return ced.get(); }
};

// Build a `CustomEventDeclaration` with a `ReturnType` `SimpleType` `EventHandler` and a
// `NameToken` `E` (no attributes, no accessors, no private implementation type). The holder keeps
// every node alive.
CustomEventHolder make_CustomEventDeclaration() {
    CustomEventHolder h;
    h.ced = std::make_unique<CustomEventDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("EventHandler"));
    h.ced->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    h.ced->NameToken(h.nameToken.get());
    return h;
}

// Build a `CustomEventDeclaration` with a `ReturnType` `EventHandler`, a `NameToken` `E`, and an
// `Attributes` `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
CustomEventHolder make_CustomEventDeclarationWithAttribute() {
    CustomEventHolder h;
    h.ced = std::make_unique<CustomEventDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.ced->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("EventHandler"));
    h.ced->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    h.ced->NameToken(h.nameToken.get());
    return h;
}

// Build a `CustomEventDeclaration` with a `ReturnType` `EventHandler`, a `NameToken` `E`, a
// `PrivateImplementationType` `IFoo`, an `AddAccessor` accessor (whose `Body` is a
// `BlockStatement` `{ return; }`), and a `RemoveAccessor` accessor (whose `Body` is an empty
// `BlockStatement`) -- every slot filled.
CustomEventHolder make_CustomEventDeclarationFull() {
    CustomEventHolder h;
    h.ced = std::make_unique<CustomEventDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("EventHandler"));
    h.ced->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    h.ced->NameToken(h.nameToken.get());
    h.privateImplType = std::make_unique<SimpleType>(std::string("IFoo"));
    h.ced->PrivateImplementationType(h.privateImplType.get());
    // AddAccessor with a body { return; }
    h.addReturnExpr = std::make_unique<PrimitiveExpression>(int32_t(0));
    h.addReturn = std::make_unique<ReturnStatement>(h.addReturnExpr.get());
    h.addBody = std::make_unique<BlockStatement>();
    h.addBody->Statements().Add(h.addReturn.get());
    h.addAccessor = std::make_unique<Accessor>(AccessorKind::Adder);
    h.addAccessor->Body(h.addBody.get());
    h.ced->AddAccessor(h.addAccessor.get());
    // RemoveAccessor with an empty body
    h.removeBody = std::make_unique<BlockStatement>();
    h.removeAccessor = std::make_unique<Accessor>(AccessorKind::Remover);
    h.removeAccessor->Body(h.removeBody.get());
    h.ced->RemoveAccessor(h.removeAccessor.get());
    return h;
}

} // namespace

// ==========================================================================
// EventDeclaration (the field-like event -- the FieldDeclaration D273 shape)
// ==========================================================================

// ---- is-a + final ---------------------------------------------------------

TEST(CSharp_EventDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<EventDeclaration>);
}

TEST(CSharp_EventDeclaration, IsEntityDeclarationAndAstNode) {
    EventDeclaration ed;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&ed), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ed), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ed), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ed), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ed), nullptr);
}

// ---- The EventKeyword const -----------------------------------------------

TEST(CSharp_EventDeclaration, EventKeywordConst) {
    EXPECT_STREQ(EventKeyword, "event");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_EventDeclaration, EmptyCtor) {
    EventDeclaration ed;
    EXPECT_EQ(ed.SymbolKind(), SymbolKind::Event);
    EXPECT_EQ(ed.Modifiers(), Modifiers::None);
    EXPECT_EQ(ed.ReturnType(), nullptr);
    EXPECT_EQ(ed.Attributes().Count(), 0);
    EXPECT_EQ(ed.Variables().Count(), 0);
    EXPECT_EQ(ed.GetChildCount(), 1);  // 0 attrs + 1 ReturnType + 0 variables
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_EventDeclaration, SymbolKindOverrideReturnsEvent) {
    EventDeclaration ed;
    EXPECT_EQ(ed.SymbolKind(), SymbolKind::Event);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_EventDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    EventDeclaration ed;
    EXPECT_EQ(ed.Modifiers(), Modifiers::None);
    ed.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(ed.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_EventDeclaration, HasModifierIsBitmaskTest) {
    EventDeclaration ed;
    ed.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(ed.HasModifier(Modifiers::Static));
    EXPECT_TRUE(ed.HasModifier(Modifiers::Public));
    EXPECT_FALSE(ed.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken overrides (hidden; the names live in VariableInitializer) ----

TEST(CSharp_EventDeclaration, NameAndNameTokenAreHidden) {
    EventDeclaration ed;
    EXPECT_EQ(ed.Name(), "");
    EXPECT_EQ(ed.NameToken(), nullptr);
    EXPECT_THROW(ed.Name(std::string("Foo")), std::logic_error);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    EXPECT_THROW(ed.NameToken(tok.get()), std::logic_error);
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

TEST(CSharp_EventDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    EventDeclaration ed;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ed.ReturnType(type.get());
    EXPECT_EQ(ed.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &ed);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);  // attrCount (0) -- ReturnType at flattened index 0
}

TEST(CSharp_EventDeclaration, ReturnTypeSetterDetachesAndClears) {
    EventDeclaration ed;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    ed.ReturnType(a.get());
    EXPECT_EQ(a->Parent(), &ed);
    ed.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(ed.ReturnType(), b.get());
    ed.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ed.ReturnType(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_EventDeclaration, AttributesCollectionAddReparents) {
    EventDeclaration ed;
    auto attrSec = std::make_unique<AttributeSection>();
    ed.Attributes().Add(attrSec.get());
    EXPECT_EQ(ed.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &ed);
}

// ---- The Variables collection (non-incremental) --------------------------

TEST(CSharp_EventDeclaration, VariablesCollectionAddReparentsAndReindexes) {
    EventDeclaration ed;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ed.ReturnType(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    ed.Variables().Add(x.get());
    EXPECT_EQ(ed.Variables().Count(), 1);
    EXPECT_EQ(x->Parent(), &ed);
    (void)x->Slot();  // trigger the lazy reindex
    EXPECT_EQ(x->ChildIndex, 1);  // attrCount (0) + 1 ReturnType -- Variables[0] at index 1
}

// ---- Slot storage (the generated overrides) ------------------------------

TEST(CSharp_EventDeclaration, GetChildWalksSlots) {
    auto ed = make_EventDeclaration();
    EXPECT_EQ(ed->GetChildCount(), 2);  // 0 attrs + ReturnType + 1 variable
    EXPECT_EQ(ed->GetChild(0), ed->ReturnType());
    EXPECT_EQ(ed->GetChild(1), ed->Variables().At(0));
    EXPECT_THROW(ed->GetChild(2), std::out_of_range);
}

TEST(CSharp_EventDeclaration, GetChildSlotInfoWalksSlots) {
    auto ed = make_EventDeclaration();
    EXPECT_EQ(ed->GetChildSlotInfo(0), &ed->ReturnTypeSlot);
    EXPECT_EQ(ed->GetChildSlotInfo(1), &ed->VariablesSlot);
    EXPECT_THROW(ed->GetChildSlotInfo(2), std::out_of_range);
}

TEST(CSharp_EventDeclaration, GetCollectionByKindReturnsCollections) {
    EventDeclaration ed;
    EXPECT_EQ(ed.GetCollectionByKind(&Slots::AttributeSection), &ed.Attributes());
    EXPECT_EQ(ed.GetCollectionByKind(&Slots::Variable), &ed.Variables());
    EXPECT_EQ(ed.GetCollectionByKind(&Slots::Type), nullptr);
}

TEST(CSharp_EventDeclaration, SetChildReplacesReturnType) {
    auto ed = make_EventDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    auto* oldType = ed->ReturnType();
    ed->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(ed->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

TEST(CSharp_EventDeclaration, SetChildReplacesVariable) {
    auto ed = make_EventDeclaration();
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    auto* oldVar = ed->Variables().At(0);
    ed->SetChild(1, y.get());  // Variables[0] at flattened index 1 (0 attrs + 1 ReturnType)
    EXPECT_EQ(ed->Variables().At(0), y.get());
    EXPECT_EQ(oldVar->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_EventDeclaration, SlotStaticsPointAtSharedKinds) {
    EventDeclaration ed;
    EXPECT_EQ(ed.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(ed.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(ed.VariablesSlot.Kind(), &Slots::Variable);
}

TEST(CSharp_EventDeclaration, SlotStaticsAreDistinct) {
    EventDeclaration ed;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ed.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ed.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ed.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ed.VariablesSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ed.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&ed.VariablesSlot));
}

TEST(CSharp_EventDeclaration, VariablesSlotSharesVariableKindWithFixedStatement) {
    EventDeclaration ed;
    EXPECT_EQ(ed.VariablesSlot.Kind(), &Slots::Variable);
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_EventDeclaration, AcceptVisitorDispatches) {
    EventDeclaration ed;
    RecordingVisitor v;
    ed.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"event"}));
}

TEST(CSharp_EventDeclaration, AcceptVisitorVirtualThroughBase) {
    EventDeclaration ed;
    AstNode* node = &ed;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"event"}));
}

TEST(CSharp_EventDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    EventDeclaration ed;
    EntityDeclaration* node = &ed;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"event"}));
}

TEST(CSharp_EventDeclaration, DepthFirstWalk) {
    auto ed = make_EventDeclaration();
    RecordingVisitor v;
    ed->AcceptVisitor(v);
    // event -> simple:int (ReturnType) -> id:int -> varinit:x (Variables[0]) -> id:x
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "event", "simple:int", "id:int", "varinit:x", "id:x",
    }));
}

TEST(CSharp_EventDeclaration, DepthFirstWalkWithAttribute) {
    auto ed = make_EventDeclarationWithAttribute();
    RecordingVisitor v;
    ed->AcceptVisitor(v);
    // event -> attrsec -> attr -> simple:Foo -> id:Foo -> simple:int -> id:int -> varinit:x -> id:x
    ASSERT_EQ(v.trace.size(), 9u);
    EXPECT_EQ(v.trace[0], "event");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "varinit:x");
    EXPECT_EQ(v.trace[8], "id:x");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_EventDeclaration, DoMatchSameNode) {
    auto a = make_EventDeclaration();
    auto b = make_EventDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_EventDeclaration();
    auto b = make_EventDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_EventDeclaration();
    auto b = make_EventDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_EventDeclaration();  // ReturnType SimpleType int
    EventHolder b;
    b.ed = std::make_unique<EventDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("byte"));  // different name
    b.ed->ReturnType(b.returnType.get());
    b.varX = std::make_unique<VariableInitializer>(std::string("x"));
    b.ed->Variables().Add(b.varX.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchReturnTypeAsymmetryRejects) {
    auto a = make_EventDeclaration();  // has ReturnType
    EventDeclaration b;  // no ReturnType (empty ctor)
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    b.Variables().Add(x.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

TEST(CSharp_EventDeclaration, DoMatchVariablesCountMismatchRejects) {
    auto a = make_EventDeclaration();  // 1 variable x
    EventHolder b;
    b.ed = std::make_unique<EventDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.ed->ReturnType(b.returnType.get());
    // no variables
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchVariablesNameMismatchRejects) {
    auto a = make_EventDeclaration();  // variable x
    EventHolder b;
    b.ed = std::make_unique<EventDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.ed->ReturnType(b.returnType.get());
    b.varX = std::make_unique<VariableInitializer>(std::string("y"));  // different name
    b.ed->Variables().Add(b.varX.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchRejectsNonEventEntityDeclaration) {
    auto a = make_EventDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_EventDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_EventDeclaration, DoMatchRejectsNull) {
    auto a = make_EventDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

TEST(CSharp_EventDeclaration, CloneDeepCopies) {
    auto a = make_EventDeclaration();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<EventDeclaration>(
        static_cast<EventDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Event);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    EXPECT_EQ(clone->Variables().Count(), 1u);
    EXPECT_NE(clone->Variables().At(0), a->Variables().At(0));
    EXPECT_EQ(clone->Variables().At(0)->Name(), "x");
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
    EXPECT_EQ(a->Variables().At(0)->Parent(), a.get());
}

TEST(CSharp_EventDeclaration, CloneVirtualAndCovariant) {
    auto a = make_EventDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<EventDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<EventDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

TEST(CSharp_EventDeclaration, CloneCopiesAttributes) {
    auto a = make_EventDeclarationWithAttribute();
    auto clone = std::unique_ptr<EventDeclaration>(
        static_cast<EventDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// ---- CheckInvariant -------------------------------------------------------

TEST(CSharp_EventDeclaration, CheckInvariantPassesOnFilled) {
    auto ed = make_EventDeclaration();  // ReturnType filled
    ed->CheckInvariant();  // should not assert
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_EventDeclaration, CheckInvariantRejectsEmpty) {
    EventDeclaration ed;  // no ReturnType
    EXPECT_DEATH(ed.CheckInvariant(), "");
}
#endif

// ==========================================================================
// CustomEventDeclaration (the property-like event -- the PropertyDeclaration
// D276 collection + trailing-singles shape with Accessor add/remove slots)
// ==========================================================================

// ---- is-a + final ---------------------------------------------------------

TEST(CSharp_CustomEventDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<CustomEventDeclaration>);
}

TEST(CSharp_CustomEventDeclaration, IsEntityDeclarationAndAstNode) {
    CustomEventDeclaration ced;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&ced), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ced), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ced), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ced), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ced), nullptr);
}

// ---- The EventKeyword const -----------------------------------------------

TEST(CSharp_CustomEventDeclaration, EventKeywordConst) {
    EXPECT_STREQ(EventKeyword, "event");
}

// ---- Construction --------------------------------------------------------

TEST(CSharp_CustomEventDeclaration, EmptyCtor) {
    CustomEventDeclaration ced;
    EXPECT_EQ(ced.SymbolKind(), SymbolKind::Event);
    EXPECT_EQ(ced.Modifiers(), Modifiers::None);
    EXPECT_EQ(ced.ReturnType(), nullptr);
    EXPECT_EQ(ced.PrivateImplementationType(), nullptr);
    EXPECT_EQ(ced.NameToken(), nullptr);
    EXPECT_EQ(ced.AddAccessor(), nullptr);
    EXPECT_EQ(ced.RemoveAccessor(), nullptr);
    EXPECT_EQ(ced.Attributes().Count(), 0);
    EXPECT_EQ(ced.GetChildCount(), 5);  // 0 attrs + 5 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_CustomEventDeclaration, SymbolKindOverrideReturnsEvent) {
    CustomEventDeclaration ced;
    EXPECT_EQ(ced.SymbolKind(), SymbolKind::Event);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_CustomEventDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    CustomEventDeclaration ced;
    EXPECT_EQ(ced.Modifiers(), Modifiers::None);
    ced.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(ced.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_CustomEventDeclaration, HasModifierIsBitmaskTest) {
    CustomEventDeclaration ced;
    ced.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(ced.HasModifier(Modifiers::Static));
    EXPECT_TRUE(ced.HasModifier(Modifiers::Public));
    EXPECT_FALSE(ced.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken (the real-name case -- the inherited virtual returns
// the NameToken's Name) ----------------------------------------------------------------

TEST(CSharp_CustomEventDeclaration, NameReturnsNameTokenName) {
    auto ced = make_CustomEventDeclaration();
    EXPECT_EQ(ced->Name(), "E");
    EXPECT_EQ(ced->NameToken()->Name(), "E");
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

TEST(CSharp_CustomEventDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    CustomEventDeclaration ced;
    auto type = std::make_unique<SimpleType>(std::string("EventHandler"));
    ced.ReturnType(type.get());
    EXPECT_EQ(ced.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &ced);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);  // attrCount (0) -- ReturnType at flattened index 0
}

TEST(CSharp_CustomEventDeclaration, ReturnTypeSetterDetachesAndClears) {
    CustomEventDeclaration ced;
    auto a = std::make_unique<SimpleType>(std::string("EventHandler"));
    auto b = std::make_unique<SimpleType>(std::string("Action"));
    ced.ReturnType(a.get());
    EXPECT_EQ(a->Parent(), &ced);
    ced.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(ced.ReturnType(), b.get());
    ced.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ced.ReturnType(), nullptr);
}

// ---- The PrivateImplementationType slot (a NULLABLE single AstType) -------

TEST(CSharp_CustomEventDeclaration, PrivateImplementationTypeSetterReparents) {
    CustomEventDeclaration ced;
    auto type = std::make_unique<SimpleType>(std::string("IFoo"));
    ced.PrivateImplementationType(type.get());
    EXPECT_EQ(ced.PrivateImplementationType(), type.get());
    EXPECT_EQ(type->Parent(), &ced);
}

TEST(CSharp_CustomEventDeclaration, PrivateImplementationTypeSetterDetachesAndClears) {
    CustomEventDeclaration ced;
    auto a = std::make_unique<SimpleType>(std::string("IFoo"));
    auto b = std::make_unique<SimpleType>(std::string("IBar"));
    ced.PrivateImplementationType(a.get());
    ced.PrivateImplementationType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(ced.PrivateImplementationType(), b.get());
    ced.PrivateImplementationType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ced.PrivateImplementationType(), nullptr);
}

// ---- The NameToken slot (a single REQUIRED Identifier, index-less) --------

TEST(CSharp_CustomEventDeclaration, NameTokenSetterReparentsAndReindexes) {
    CustomEventDeclaration ced;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    ced.NameToken(tok.get());
    EXPECT_EQ(ced.NameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &ced);
    (void)tok->Slot();  // trigger the lazy reindex
    // NameToken is at flattened index 2 (0 attrs + ReturnType at 0 + PrivateImplType at 1)
    EXPECT_EQ(tok->ChildIndex, 2);
}

TEST(CSharp_CustomEventDeclaration, NameTokenSetterDetachesAndClears) {
    CustomEventDeclaration ced;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("F")));
    ced.NameToken(a.get());
    EXPECT_EQ(a->Parent(), &ced);
    ced.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(ced.NameToken(), b.get());
    ced.NameToken(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ced.NameToken(), nullptr);
}

// ---- The AddAccessor slot (a NULLABLE single Accessor) -------------------

TEST(CSharp_CustomEventDeclaration, AddAccessorSetterReparents) {
    CustomEventDeclaration ced;
    auto acc = std::make_unique<Accessor>(AccessorKind::Adder);
    ced.AddAccessor(acc.get());
    EXPECT_EQ(ced.AddAccessor(), acc.get());
    EXPECT_EQ(acc->Parent(), &ced);
}

TEST(CSharp_CustomEventDeclaration, AddAccessorSetterDetachesAndClears) {
    CustomEventDeclaration ced;
    auto a = std::make_unique<Accessor>(AccessorKind::Adder);
    auto b = std::make_unique<Accessor>(AccessorKind::Adder);
    ced.AddAccessor(a.get());
    ced.AddAccessor(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(ced.AddAccessor(), b.get());
    ced.AddAccessor(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ced.AddAccessor(), nullptr);
}

// ---- The RemoveAccessor slot (a NULLABLE single Accessor) ----------------

TEST(CSharp_CustomEventDeclaration, RemoveAccessorSetterReparents) {
    CustomEventDeclaration ced;
    auto acc = std::make_unique<Accessor>(AccessorKind::Remover);
    ced.RemoveAccessor(acc.get());
    EXPECT_EQ(ced.RemoveAccessor(), acc.get());
    EXPECT_EQ(acc->Parent(), &ced);
}

TEST(CSharp_CustomEventDeclaration, RemoveAccessorSetterDetachesAndClears) {
    CustomEventDeclaration ced;
    auto a = std::make_unique<Accessor>(AccessorKind::Remover);
    auto b = std::make_unique<Accessor>(AccessorKind::Remover);
    ced.RemoveAccessor(a.get());
    ced.RemoveAccessor(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(ced.RemoveAccessor(), b.get());
    ced.RemoveAccessor(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(ced.RemoveAccessor(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_CustomEventDeclaration, AttributesCollectionAddReparents) {
    CustomEventDeclaration ced;
    auto attrSec = std::make_unique<AttributeSection>();
    ced.Attributes().Add(attrSec.get());
    EXPECT_EQ(ced.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &ced);
}

// ---- Slot storage (the generated overrides) ------------------------------

TEST(CSharp_CustomEventDeclaration, GetChildWalksSlots) {
    auto ced = make_CustomEventDeclarationFull();
    // 0 attrs + 5 singles (ReturnType, PrivateImplType, NameToken, AddAccessor, RemoveAccessor)
    EXPECT_EQ(ced->GetChildCount(), 5);
    EXPECT_EQ(ced->GetChild(0), ced->ReturnType());
    EXPECT_EQ(ced->GetChild(1), ced->PrivateImplementationType());
    EXPECT_EQ(ced->GetChild(2), ced->NameToken());
    EXPECT_EQ(ced->GetChild(3), ced->AddAccessor());
    EXPECT_EQ(ced->GetChild(4), ced->RemoveAccessor());
    EXPECT_THROW(ced->GetChild(5), std::out_of_range);
}

TEST(CSharp_CustomEventDeclaration, GetChildSlotInfoWalksSlots) {
    auto ced = make_CustomEventDeclarationFull();
    EXPECT_EQ(ced->GetChildSlotInfo(0), &ced->ReturnTypeSlot);
    EXPECT_EQ(ced->GetChildSlotInfo(1), &ced->PrivateImplementationTypeSlot);
    EXPECT_EQ(ced->GetChildSlotInfo(2), &ced->NameTokenSlot);
    EXPECT_EQ(ced->GetChildSlotInfo(3), &ced->AddAccessorSlot);
    EXPECT_EQ(ced->GetChildSlotInfo(4), &ced->RemoveAccessorSlot);
    EXPECT_THROW(ced->GetChildSlotInfo(5), std::out_of_range);
}

TEST(CSharp_CustomEventDeclaration, GetCollectionByKindReturnsAttributes) {
    CustomEventDeclaration ced;
    EXPECT_EQ(ced.GetCollectionByKind(&Slots::AttributeSection), &ced.Attributes());
    EXPECT_EQ(ced.GetCollectionByKind(&Slots::Type), nullptr);
}

TEST(CSharp_CustomEventDeclaration, SetChildReplacesReturnType) {
    auto ced = make_CustomEventDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("Action"));
    auto* oldType = ced->ReturnType();
    ced->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(ced->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

TEST(CSharp_CustomEventDeclaration, SetChildReplacesNameToken) {
    auto ced = make_CustomEventDeclaration();
    auto tok2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("F")));
    auto* oldTok = ced->NameToken();
    ced->SetChild(2, tok2.get());  // NameToken at flattened index 2 (0 attrs)
    EXPECT_EQ(ced->NameToken(), tok2.get());
    EXPECT_EQ(oldTok->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_CustomEventDeclaration, SlotStaticsPointAtSharedKinds) {
    CustomEventDeclaration ced;
    EXPECT_EQ(ced.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(ced.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(ced.PrivateImplementationTypeSlot.Kind(), &Slots::PrivateImplementationType);
    EXPECT_EQ(ced.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(ced.AddAccessorSlot.Kind(), &Slots::AddAccessor);
    EXPECT_EQ(ced.RemoveAccessorSlot.Kind(), &Slots::RemoveAccessor);
}

TEST(CSharp_CustomEventDeclaration, SlotStaticsAreDistinct) {
    CustomEventDeclaration ced;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ced.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&ced.PrivateImplementationTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ced.AddAccessorSlot),
              static_cast<const CSharpSlotInfo*>(&ced.RemoveAccessorSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ced.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&ced.AddAccessorSlot));
}

TEST(CSharp_CustomEventDeclaration, AddRemoveAccessorKindsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::AddAccessor),
              static_cast<const CSharpSlotInfo*>(&Slots::RemoveAccessor));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_CustomEventDeclaration, AcceptVisitorDispatches) {
    CustomEventDeclaration ced;
    RecordingVisitor v;
    ced.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"customevent:"}));
}

TEST(CSharp_CustomEventDeclaration, AcceptVisitorVirtualThroughBase) {
    CustomEventDeclaration ced;
    AstNode* node = &ced;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"customevent:"}));
}

TEST(CSharp_CustomEventDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    CustomEventDeclaration ced;
    EntityDeclaration* node = &ced;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"customevent:"}));
}

TEST(CSharp_CustomEventDeclaration, DepthFirstWalk) {
    auto ced = make_CustomEventDeclaration();
    RecordingVisitor v;
    ced->AcceptVisitor(v);
    // customevent:E -> simple:EventHandler (ReturnType) -> id:EventHandler -> id:E (NameToken)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "customevent:E", "simple:EventHandler", "id:EventHandler", "id:E",
    }));
}

TEST(CSharp_CustomEventDeclaration, DepthFirstWalkWithAttribute) {
    auto ced = make_CustomEventDeclarationWithAttribute();
    RecordingVisitor v;
    ced->AcceptVisitor(v);
    // customevent:E -> attrsec -> attr -> simple:Foo -> id:Foo
    //               -> simple:EventHandler -> id:EventHandler -> id:E
    ASSERT_EQ(v.trace.size(), 8u);
    EXPECT_EQ(v.trace[0], "customevent:E");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:EventHandler");
    EXPECT_EQ(v.trace[6], "id:EventHandler");
    EXPECT_EQ(v.trace[7], "id:E");
}

TEST(CSharp_CustomEventDeclaration, DepthFirstWalkFull) {
    auto ced = make_CustomEventDeclarationFull();
    RecordingVisitor v;
    ced->AcceptVisitor(v);
    // customevent:E -> simple:EventHandler -> id:EventHandler -> simple:IFoo -> id:IFoo
    //               -> id:E (NameToken) -> accessor:Adder -> block -> return -> prim
    //               -> accessor:Remover -> block
    ASSERT_EQ(v.trace.size(), 12u);
    EXPECT_EQ(v.trace[0], "customevent:E");
    EXPECT_EQ(v.trace[1], "simple:EventHandler");
    EXPECT_EQ(v.trace[2], "id:EventHandler");
    EXPECT_EQ(v.trace[3], "simple:IFoo");
    EXPECT_EQ(v.trace[4], "id:IFoo");
    EXPECT_EQ(v.trace[5], "id:E");
    EXPECT_EQ(v.trace[6], "accessor:" + std::to_string(static_cast<int>(AccessorKind::Adder)));
    EXPECT_EQ(v.trace[7], "block");
    EXPECT_EQ(v.trace[8], "return");
    EXPECT_EQ(v.trace[9], "prim");
    EXPECT_EQ(v.trace[10], "accessor:" + std::to_string(static_cast<int>(AccessorKind::Remover)));
    EXPECT_EQ(v.trace[11], "block");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_CustomEventDeclaration, DoMatchSameNode) {
    auto a = make_CustomEventDeclaration();
    auto b = make_CustomEventDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_CustomEventDeclaration();
    auto b = make_CustomEventDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_CustomEventDeclaration();
    auto b = make_CustomEventDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchNameMismatchRejects) {
    auto a = make_CustomEventDeclaration();  // Name E
    CustomEventHolder b;
    b.ced = std::make_unique<CustomEventDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("EventHandler"));
    b.ced->ReturnType(b.returnType.get());
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("F")));  // different
    b.ced->NameToken(b.nameToken.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_CustomEventDeclaration();  // ReturnType EventHandler
    CustomEventHolder b;
    b.ced = std::make_unique<CustomEventDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("Action"));  // different name
    b.ced->ReturnType(b.returnType.get());
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    b.ced->NameToken(b.nameToken.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchAddAccessorAsymmetryRejects) {
    auto a = make_CustomEventDeclarationFull();  // has AddAccessor
    auto b = make_CustomEventDeclaration();  // no AddAccessor
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchRemoveAccessorAsymmetryRejects) {
    auto a = make_CustomEventDeclarationFull();  // has RemoveAccessor
    CustomEventHolder b;
    b.ced = std::make_unique<CustomEventDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("EventHandler"));
    b.ced->ReturnType(b.returnType.get());
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("E")));
    b.ced->NameToken(b.nameToken.get());
    // same AddAccessor but no RemoveAccessor -- build a matching AddAccessor for b
    b.addBody = std::make_unique<BlockStatement>();
    b.addReturn = std::make_unique<ReturnStatement>();
    b.addBody->Statements().Add(b.addReturn.get());
    b.addAccessor = std::make_unique<Accessor>(AccessorKind::Adder);
    b.addAccessor->Body(b.addBody.get());
    b.ced->AddAccessor(b.addAccessor.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));  // a has RemoveAccessor, b does not
}

TEST(CSharp_CustomEventDeclaration, DoMatchBothAccessorsAbsentMatches) {
    auto a = make_CustomEventDeclaration();  // no AddAccessor, no RemoveAccessor
    auto b = make_CustomEventDeclaration();  // no AddAccessor, no RemoveAccessor
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchPrivateImplementationTypeAsymmetryRejects) {
    auto a = make_CustomEventDeclarationFull();  // has PrivateImplementationType IFoo
    auto b = make_CustomEventDeclaration();  // no PrivateImplementationType
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchRejectsNonCustomEventEntityDeclaration) {
    auto a = make_CustomEventDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_CustomEventDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

TEST(CSharp_CustomEventDeclaration, DoMatchRejectsNull) {
    auto a = make_CustomEventDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

TEST(CSharp_CustomEventDeclaration, CloneDeepCopies) {
    auto a = make_CustomEventDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<CustomEventDeclaration>(
        static_cast<CustomEventDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Event);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->Name(), "E");
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    ASSERT_NE(clone->PrivateImplementationType(), nullptr);
    EXPECT_NE(clone->PrivateImplementationType(), a->PrivateImplementationType());
    ASSERT_NE(clone->AddAccessor(), nullptr);
    EXPECT_NE(clone->AddAccessor(), a->AddAccessor());
    ASSERT_NE(clone->RemoveAccessor(), nullptr);
    EXPECT_NE(clone->RemoveAccessor(), a->RemoveAccessor());
    // The source is not detached.
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
}

TEST(CSharp_CustomEventDeclaration, CloneVirtualAndCovariant) {
    auto a = make_CustomEventDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<CustomEventDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<CustomEventDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

TEST(CSharp_CustomEventDeclaration, CloneSkipsAbsentNullableSlots) {
    auto a = make_CustomEventDeclaration();  // no PrivateImplType, no AddAccessor, no RemoveAccessor
    auto clone = std::unique_ptr<CustomEventDeclaration>(
        static_cast<CustomEventDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->PrivateImplementationType(), nullptr);
    EXPECT_EQ(clone->AddAccessor(), nullptr);
    EXPECT_EQ(clone->RemoveAccessor(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
    ASSERT_NE(clone->NameToken(), nullptr);
}

TEST(CSharp_CustomEventDeclaration, CloneCopiesAttributes) {
    auto a = make_CustomEventDeclarationWithAttribute();
    auto clone = std::unique_ptr<CustomEventDeclaration>(
        static_cast<CustomEventDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// ---- CheckInvariant -------------------------------------------------------

TEST(CSharp_CustomEventDeclaration, CheckInvariantPassesOnFilled) {
    auto ced = make_CustomEventDeclaration();  // ReturnType + NameToken filled
    ced->CheckInvariant();  // should not assert
    SUCCEED();
}

TEST(CSharp_CustomEventDeclaration, CheckInvariantPassesOnFull) {
    auto ced = make_CustomEventDeclarationFull();
    ced->CheckInvariant();  // should not assert
    SUCCEED();
}

#ifndef NDEBUG
TEST(CSharp_CustomEventDeclaration, CheckInvariantRejectsEmpty) {
    CustomEventDeclaration ced;  // no ReturnType, no NameToken
    EXPECT_DEATH(ced.CheckInvariant(), "");
}
#endif
