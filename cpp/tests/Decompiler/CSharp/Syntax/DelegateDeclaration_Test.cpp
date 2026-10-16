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

// Tests for the `DelegateDeclaration` concrete node (cpp/.../Syntax/DelegateDeclaration.hpp,
// the port of the `DelegateDeclaration` in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/DelegateDeclaration.cs) -- the next
// in-order Phase-5 piece per the D286 plan (the `MethodDeclaration` D284 four-collection shape
// with TWO trailing singles `ReturnType`/`NameToken` instead of four -- no
// `PrivateImplementationType`, no `Body` -- now unblocked since every dependency is ported:
// `TypeParameterDeclaration` D282 + `ParameterDeclaration` D278 + `Constraint` D283). The file
// carries one suite (CSharp_DelegateDeclaration) with a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 shared-RecordingVisitor/DoMatchAgainst pattern, with
// `DelegateHolder` structs keeping all children alive in the test scope -- the D223 non-owning
// raw-pointer model).

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
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/DelegateDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitDelegateDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`TypeParameterDeclaration`/
// `ParameterDeclaration`/`Constraint` of the slots, and the `DestructorDeclaration`/
// `WhileStatement` used for the cross-type DoMatch rejections), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitDelegateDeclaration(DelegateDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-delegate>"); return; }
        trace.push_back("delegate:" + node->Name());
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
    void VisitTypeParameterDeclaration(TypeParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-tpp>"); return; }
        trace.push_back("tpp:" + node->Name());
        VisitChildren(node);
    }
    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-param>"); return; }
        trace.push_back("param");
        VisitChildren(node);
    }
    void VisitConstraint(Constraint* node) override {
        if (node == nullptr) { trace.push_back("<null-constraint>"); return; }
        trace.push_back("constraint");
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
// pattern). Uses `Match::CreateNew()` (a default-constructed `Match` holds a null capture vector
// -- the D219 distinction; the collection `DoMatch` derefs it).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping a `DelegateDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design).
struct DelegateHolder {
    std::unique_ptr<DelegateDeclaration> dd;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<Identifier> nameToken;
    // An Attributes AttributeSection holding an Attribute whose Type is a SimpleType.
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    // One TypeParameter "T" (kept alive with its own NameToken).
    std::unique_ptr<TypeParameterDeclaration> tp0;
    std::unique_ptr<Identifier> tp0NameToken;
    // One Parameter "int x" (kept alive with its own Type + NameToken).
    std::unique_ptr<ParameterDeclaration> param0;
    std::unique_ptr<SimpleType> param0Type;
    std::unique_ptr<Identifier> param0NameToken;
    // One Constraint "where T :" with no base types (kept alive with its own TypeParameter SimpleType).
    std::unique_ptr<Constraint> constraint0;
    std::unique_ptr<SimpleType> constraint0TypeParam;
    DelegateDeclaration* get() const { return dd.get(); }
    DelegateDeclaration* operator->() const { return dd.get(); }
};

// Build a `DelegateDeclaration` with a `NameToken` `Foo` and a `ReturnType` `int` (both required
// slots filled), no attributes, no `TypeParameters`, no `Parameters`, no `Constraints`. The
// holder keeps every node alive.
DelegateHolder make_DelegateDeclaration() {
    DelegateHolder h;
    h.dd = std::make_unique<DelegateDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.dd->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.dd->NameToken(h.nameToken.get());
    return h;
}

// Build a `DelegateDeclaration` with a `NameToken` `Foo`, a `ReturnType` `int`, and an
// `Attributes` `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
DelegateHolder make_DelegateDeclarationWithAttribute() {
    DelegateHolder h;
    h.dd = std::make_unique<DelegateDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.dd->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.dd->NameToken(h.nameToken.get());
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.dd->Attributes().Add(h.attrSec.get());
    return h;
}

// Build a `DelegateDeclaration` with every slot filled: a `NameToken` `Foo`, a `ReturnType` `int`,
// an `Attributes` `AttributeSection` with an `Attribute` `Foo`, a `TypeParameters` collection
// holding one `TypeParameterDeclaration` `T`, a `Parameters` collection holding one
// `ParameterDeclaration` `int x`, and a `Constraints` collection holding one `Constraint`
// `where T :` (no base types) -- every slot filled.
DelegateHolder make_DelegateDeclarationFull() {
    DelegateHolder h;
    h.dd = std::make_unique<DelegateDeclaration>();
    // ReturnType int
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.dd->ReturnType(h.returnType.get());
    // NameToken Foo
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.dd->NameToken(h.nameToken.get());
    // Attribute
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.dd->Attributes().Add(h.attrSec.get());
    // One type parameter T
    h.tp0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("T")));
    h.tp0 = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    h.tp0->NameToken(h.tp0NameToken.get());
    h.dd->TypeParameters().Add(h.tp0.get());
    // One parameter int x
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.param0Type = std::make_unique<SimpleType>(std::string("int"));
    h.param0->Type(h.param0Type.get());
    h.param0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param0->NameToken(h.param0NameToken.get());
    h.dd->Parameters().Add(h.param0.get());
    // One constraint where T : (no base types)
    h.constraint0TypeParam = std::make_unique<SimpleType>(std::string("T"));
    h.constraint0 = std::make_unique<Constraint>(h.constraint0TypeParam.get());
    h.dd->Constraints().Add(h.constraint0.get());
    return h;
}

} // namespace

// ==========================================================================
// DelegateDeclaration (the delegate_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `DelegateDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_DelegateDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<DelegateDeclaration>);
}

// `DelegateDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_DelegateDeclaration, IsEntityDeclarationAndAstNode) {
    DelegateDeclaration dd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&dd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&dd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&dd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&dd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&dd), nullptr);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `NameToken`, no `ReturnType`, empty `Attributes`/`TypeParameters`/
// `Parameters`/`Constraints`. `GetChildCount` is `0 + 0 + 0 + 0 + 2 = 2` (the four empty
// collections plus the two single slots -- each single slot contributes 1 to the flattened count
// even when absent). `Modifiers` defaults to `None`; `SymbolKind` is `TypeDefinition`.
TEST(CSharp_DelegateDeclaration, EmptyCtor) {
    DelegateDeclaration dd;
    EXPECT_EQ(dd.SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(dd.Modifiers(), Modifiers::None);
    EXPECT_EQ(dd.ReturnType(), nullptr);
    EXPECT_EQ(dd.NameToken(), nullptr);
    EXPECT_EQ(dd.TypeParameters().Count(), 0);
    EXPECT_EQ(dd.Parameters().Count(), 0);
    EXPECT_EQ(dd.Constraints().Count(), 0);
    EXPECT_EQ(dd.Attributes().Count(), 0);
    EXPECT_EQ(dd.GetChildCount(), 2);  // 0 attrs + 0 tps + 0 params + 0 constraints + 2 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_DelegateDeclaration, SymbolKindOverrideReturnsTypeDefinition) {
    DelegateDeclaration dd;
    EXPECT_EQ(dd.SymbolKind(), SymbolKind::TypeDefinition);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_DelegateDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    DelegateDeclaration dd;
    EXPECT_EQ(dd.Modifiers(), Modifiers::None);
    dd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(dd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_DelegateDeclaration, HasModifierIsBitmaskTest) {
    DelegateDeclaration dd;
    dd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(dd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(dd.HasModifier(Modifiers::Public));
    EXPECT_FALSE(dd.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken (NameToken is a real [Slot], NOT [ExcludeFromMatch]) ----

// `Name` returns the `NameToken`'s name (the inherited base `Name()` kind-walks for the
// `Identifier` kind and returns the token's `Name`). A delegate's name is a real name.
TEST(CSharp_DelegateDeclaration, NameReturnsNameTokenName) {
    auto dd = make_DelegateDeclaration();
    EXPECT_EQ(dd->Name(), "Foo");
}

// `NameToken` returns the backing field directly (the generated `get => field!`).
TEST(CSharp_DelegateDeclaration, NameTokenReturnsBackingField) {
    auto dd = make_DelegateDeclaration();
    EXPECT_EQ(dd->NameToken(), dd->NameToken());
    EXPECT_NE(dd->NameToken(), nullptr);
    EXPECT_EQ(dd->NameToken()->Name(), "Foo");
}

// `NameToken` setter re-parents and re-indexes (the index-less setter following the `Attributes`
// collection).
TEST(CSharp_DelegateDeclaration, NameTokenSetterReparentsAndDetaches) {
    DelegateDeclaration dd;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    dd.NameToken(a.get());
    EXPECT_EQ(dd.NameToken(), a.get());
    EXPECT_EQ(a->Parent(), &dd);
    dd.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(dd.NameToken(), b.get());
    dd.NameToken(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(dd.NameToken(), nullptr);
}

// ---- The ReturnType slot (override, required, index-less) ----------------

TEST(CSharp_DelegateDeclaration, ReturnTypeSetterReparentsAndDetaches) {
    DelegateDeclaration dd;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("string"));
    dd.ReturnType(a.get());
    EXPECT_EQ(dd.ReturnType(), a.get());
    EXPECT_EQ(a->Parent(), &dd);
    dd.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(dd.ReturnType(), b.get());
    dd.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(dd.ReturnType(), nullptr);
}

// ---- The TypeParameters collection (non-incremental) ---------------------

TEST(CSharp_DelegateDeclaration, TypeParametersCollectionAddReparents) {
    DelegateDeclaration dd;
    auto tp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    dd.TypeParameters().Add(tp.get());
    EXPECT_EQ(dd.TypeParameters().Count(), 1);
    EXPECT_EQ(tp->Parent(), &dd);
}

// ---- The Parameters collection (non-incremental) -------------------------

TEST(CSharp_DelegateDeclaration, ParametersCollectionAddReparents) {
    DelegateDeclaration dd;
    auto p = std::make_unique<ParameterDeclaration>();
    dd.Parameters().Add(p.get());
    EXPECT_EQ(dd.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &dd);
}

// ---- The Constraints collection (non-incremental) ------------------------

TEST(CSharp_DelegateDeclaration, ConstraintsCollectionAddReparents) {
    DelegateDeclaration dd;
    auto tp = std::make_unique<SimpleType>(std::string("T"));
    auto c = std::make_unique<Constraint>(tp.get());
    dd.Constraints().Add(c.get());
    EXPECT_EQ(dd.Constraints().Count(), 1);
    EXPECT_EQ(c->Parent(), &dd);
}

// ---- The Attributes collection (non-incremental) -------------------------

TEST(CSharp_DelegateDeclaration, AttributesCollectionAddReparents) {
    DelegateDeclaration dd;
    auto attrSec = std::make_unique<AttributeSection>();
    dd.Attributes().Add(attrSec.get());
    EXPECT_EQ(dd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &dd);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `ReturnType`
// single at `attrCount`, the `NameToken` single at `attrCount + 1`, the `TypeParameters`
// collection `[attrCount + 2, ...)`, the `Parameters` collection, and the `Constraints` collection.
TEST(CSharp_DelegateDeclaration, GetChildWalksSlots) {
    auto dd = make_DelegateDeclarationFull();
    // 1 attr + 1 tpp + 1 param + 1 constraint + 2 singles = 6
    EXPECT_EQ(dd->GetChildCount(), 6);
    EXPECT_EQ(dd->GetChild(0), dd->Attributes().At(0));        // the AttributeSection
    EXPECT_EQ(dd->GetChild(1), dd->ReturnType());             // 1 (attrCount + ReturnType)
    EXPECT_EQ(dd->GetChild(2), dd->NameToken());              // 2
    EXPECT_EQ(dd->GetChild(3), dd->TypeParameters().At(0));    // 3 (TypeParameters[0])
    EXPECT_EQ(dd->GetChild(4), dd->Parameters().At(0));       // 4 (Parameters[0])
    EXPECT_EQ(dd->GetChild(5), dd->Constraints().At(0));      // 5 (Constraints[0])
    EXPECT_THROW(dd->GetChild(6), std::out_of_range);
}

TEST(CSharp_DelegateDeclaration, GetChildSlotInfoWalksSlots) {
    auto dd = make_DelegateDeclarationFull();
    EXPECT_EQ(dd->GetChildSlotInfo(0), &dd->AttributesSlot);
    EXPECT_EQ(dd->GetChildSlotInfo(1), &dd->ReturnTypeSlot);
    EXPECT_EQ(dd->GetChildSlotInfo(2), &dd->NameTokenSlot);
    EXPECT_EQ(dd->GetChildSlotInfo(3), &dd->TypeParametersSlot);
    EXPECT_EQ(dd->GetChildSlotInfo(4), &dd->ParametersSlot);
    EXPECT_EQ(dd->GetChildSlotInfo(5), &dd->ConstraintsSlot);
    EXPECT_THROW(dd->GetChildSlotInfo(6), std::out_of_range);
}

TEST(CSharp_DelegateDeclaration, GetCollectionByKindReturnsAllFourCollections) {
    DelegateDeclaration dd;
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::AttributeSection), &dd.Attributes());
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::TypeParameter), &dd.TypeParameters());
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::Parameter), &dd.Parameters());
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::Constraint), &dd.Constraints());
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(dd.GetCollectionByKind(&Slots::Identifier), nullptr);
}

// `SetChild` replaces the `ReturnType` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_DelegateDeclaration, SetChildReplacesReturnType) {
    auto dd = make_DelegateDeclaration();
    auto rt2 = std::make_unique<SimpleType>(std::string("string"));
    auto* oldRt = dd->ReturnType();
    dd->SetChild(0, rt2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(dd->ReturnType(), rt2.get());
    EXPECT_EQ(oldRt->Parent(), nullptr);
}

// `SetChild` replaces a `Parameters` element in place.
TEST(CSharp_DelegateDeclaration, SetChildReplacesParameter) {
    auto dd = make_DelegateDeclarationFull();
    auto p2 = std::make_unique<ParameterDeclaration>();
    auto* oldParam = dd->Parameters().At(0);
    dd->SetChild(4, p2.get());  // Parameters[0] at flattened index 4 (1 attr + ReturnType + NameToken + 1 tpp)
    EXPECT_EQ(dd->Parameters().At(0), p2.get());
    EXPECT_EQ(oldParam->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_DelegateDeclaration, SlotStaticsPointAtSharedKinds) {
    DelegateDeclaration dd;
    EXPECT_EQ(dd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(dd.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(dd.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(dd.TypeParametersSlot.Kind(), &Slots::TypeParameter);
    EXPECT_EQ(dd.ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(dd.ConstraintsSlot.Kind(), &Slots::Constraint);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_DelegateDeclaration, SlotStaticsAreDistinct) {
    DelegateDeclaration dd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&dd.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&dd.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&dd.TypeParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.TypeParametersSlot),
              static_cast<const CSharpSlotInfo*>(&dd.ParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&dd.ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&dd.ConstraintsSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_DelegateDeclaration, AcceptVisitorDispatches) {
    DelegateDeclaration dd;  // no NameToken -- the empty node records just the node tag
    RecordingVisitor v;
    dd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"delegate:"}));
}

TEST(CSharp_DelegateDeclaration, AcceptVisitorVirtualThroughBase) {
    DelegateDeclaration dd;
    AstNode* node = &dd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"delegate:"}));
}

TEST(CSharp_DelegateDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    DelegateDeclaration dd;
    EntityDeclaration* node = &dd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"delegate:"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int` -> `Identifier` `int`)
// and the `NameToken` (`Identifier` `Foo`) in document order (no attributes, no
// `TypeParameters`/`Parameters`/`Constraints`).
TEST(CSharp_DelegateDeclaration, DepthFirstWalk) {
    auto dd = make_DelegateDeclaration();
    RecordingVisitor v;
    dd->AcceptVisitor(v);
    // delegate:Foo -> simple:int -> id:int (ReturnType) -> id:Foo (NameToken)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "delegate:Foo", "simple:int", "id:int", "id:Foo",
    }));
}

// A `DelegateDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute` ->
// its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` (the `Attributes` collection
// is the first slot).
TEST(CSharp_DelegateDeclaration, DepthFirstWalkWithAttribute) {
    auto dd = make_DelegateDeclarationWithAttribute();
    RecordingVisitor v;
    dd->AcceptVisitor(v);
    // delegate:Foo -> attrsec (Attributes[0]) -> attr -> simple:Foo -> id:Foo
    //      -> simple:int -> id:int (ReturnType)
    //      -> id:Foo (NameToken)
    ASSERT_EQ(v.trace.size(), 8u);
    EXPECT_EQ(v.trace[0], "delegate:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "id:Foo");  // the NameToken
}

// A `DelegateDeclaration` with every slot filled recurses into the `Attributes`, `ReturnType`,
// `NameToken`, each `TypeParameter` (and its `NameToken`), each `Parameter` (and its `Type`/
// `NameToken`), and each `Constraint` (and its `TypeParameter`), in slot order.
TEST(CSharp_DelegateDeclaration, DepthFirstWalkFull) {
    auto dd = make_DelegateDeclarationFull();
    RecordingVisitor v;
    dd->AcceptVisitor(v);
    // delegate:Foo -> attrsec -> attr -> simple:Foo -> id:Foo (Attribute)
    //      -> simple:int -> id:int (ReturnType)
    //      -> id:Foo (NameToken)
    //      -> tpp:T -> id:T (TypeParameters[0])
    //      -> param (Parameters[0]) -> simple:int -> id:int -> id:x
    //      -> constraint (Constraints[0]) -> simple:T -> id:T
    ASSERT_EQ(v.trace.size(), 17u);
    EXPECT_EQ(v.trace[0], "delegate:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "id:Foo");  // NameToken
    EXPECT_EQ(v.trace[8], "tpp:T");
    EXPECT_EQ(v.trace[9], "id:T");
    EXPECT_EQ(v.trace[10], "param");
    EXPECT_EQ(v.trace[11], "simple:int");
    EXPECT_EQ(v.trace[12], "id:int");
    EXPECT_EQ(v.trace[13], "id:x");
    EXPECT_EQ(v.trace[14], "constraint");
    EXPECT_EQ(v.trace[15], "simple:T");
    EXPECT_EQ(v.trace[16], "id:T");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_DelegateDeclaration, DoMatchSameNode) {
    auto a = make_DelegateDeclaration();
    auto b = make_DelegateDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]`).
TEST(CSharp_DelegateDeclaration, DoMatchNameMismatchRejects) {
    auto a = make_DelegateDeclaration();
    auto b = make_DelegateDeclaration();
    auto bName = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    b->NameToken(bName.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_DelegateDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_DelegateDeclaration();
    auto b = make_DelegateDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_DelegateDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_DelegateDeclaration();
    auto b = make_DelegateDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` term over the required `ReturnType` slot).
TEST(CSharp_DelegateDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_DelegateDeclaration();
    auto b = make_DelegateDeclaration();
    auto aRt = std::make_unique<SimpleType>(std::string("int"));
    a->ReturnType(aRt.get());
    auto bRt = std::make_unique<SimpleType>(std::string("string"));
    b->ReturnType(bRt.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `TypeParameters` mismatch rejects (the collection recursive `DoMatch` -- a pattern with a
// type parameter vs a candidate without one).
TEST(CSharp_DelegateDeclaration, DoMatchTypeParametersMismatchRejects) {
    auto a = make_DelegateDeclarationFull();  // one type parameter
    auto b = make_DelegateDeclaration();  // no type parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Parameters` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_DelegateDeclaration, DoMatchParametersMismatchRejects) {
    auto a = make_DelegateDeclarationFull();  // one parameter
    auto b = make_DelegateDeclaration();  // no parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Constraints` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_DelegateDeclaration, DoMatchConstraintsMismatchRejects) {
    auto a = make_DelegateDeclarationFull();  // one constraint
    auto b = make_DelegateDeclaration();  // no constraints
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single type parameter match (the collection recursive `DoMatch` accepts
// equal-length collections whose elements match).
TEST(CSharp_DelegateDeclaration, DoMatchSameTypeParametersMatch) {
    auto a = make_DelegateDeclaration();
    auto aTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    a->TypeParameters().Add(aTp.get());
    auto b = make_DelegateDeclaration();
    auto bTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    b->TypeParameters().Add(bTp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single constraint match.
TEST(CSharp_DelegateDeclaration, DoMatchSameConstraintsMatch) {
    auto a = make_DelegateDeclaration();
    auto aTpSimple = std::make_unique<SimpleType>(std::string("T"));
    auto aC = std::make_unique<Constraint>(aTpSimple.get());
    a->Constraints().Add(aC.get());
    auto b = make_DelegateDeclaration();
    auto bTpSimple = std::make_unique<SimpleType>(std::string("T"));
    auto bC = std::make_unique<Constraint>(bTpSimple.get());
    b->Constraints().Add(bC.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`DelegateDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not a `DelegateDeclaration`).
TEST(CSharp_DelegateDeclaration, DoMatchRejectsNonDelegateEntityDeclaration) {
    auto a = make_DelegateDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_DelegateDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_DelegateDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_DelegateDeclaration, DoMatchRejectsNull) {
    auto a = make_DelegateDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`NameToken`, copies the `Modifiers` scalar, and does not
// detach the source.
TEST(CSharp_DelegateDeclaration, CloneDeepCopies) {
    auto a = make_DelegateDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<DelegateDeclaration>(
        static_cast<DelegateDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->Name(), "Foo");
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    // The source is not detached.
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `DelegateDeclaration*`.
TEST(CSharp_DelegateDeclaration, CloneVirtualAndCovariant) {
    auto a = make_DelegateDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<DelegateDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<DelegateDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_DelegateDeclaration, CloneCopiesAttributes) {
    auto a = make_DelegateDeclarationWithAttribute();
    auto clone = std::unique_ptr<DelegateDeclaration>(
        static_cast<DelegateDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->NameToken()->Parent(), clone.get());
}

// `Clone` copies the `TypeParameters` collection (each element deep-cloned).
TEST(CSharp_DelegateDeclaration, CloneCopiesTypeParameters) {
    auto a = make_DelegateDeclarationFull();
    auto clone = std::unique_ptr<DelegateDeclaration>(
        static_cast<DelegateDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->TypeParameters().Count(), 1u);
    auto* clonedTp = clone->TypeParameters().At(0);
    EXPECT_NE(clonedTp, a->TypeParameters().At(0));
    ASSERT_NE(clonedTp->NameToken(), nullptr);
    EXPECT_NE(clonedTp->NameToken(), a->TypeParameters().At(0)->NameToken());
    EXPECT_EQ(clonedTp->NameToken()->Name(), "T");
}

// `Clone` copies the `Parameters` collection (each element deep-cloned, including its `Type`/
// `NameToken`).
TEST(CSharp_DelegateDeclaration, CloneCopiesParameters) {
    auto a = make_DelegateDeclarationFull();
    auto clone = std::unique_ptr<DelegateDeclaration>(
        static_cast<DelegateDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Parameters().Count(), 1u);
    auto* clonedParam = clone->Parameters().At(0);
    EXPECT_NE(clonedParam, a->Parameters().At(0));
    ASSERT_NE(clonedParam->Type(), nullptr);
    EXPECT_NE(clonedParam->Type(), a->Parameters().At(0)->Type());
    ASSERT_NE(clonedParam->NameToken(), nullptr);
    EXPECT_NE(clonedParam->NameToken(), a->Parameters().At(0)->NameToken());
    EXPECT_EQ(clonedParam->NameToken()->Name(), "x");
}

// `Clone` copies the `Constraints` collection (each element deep-cloned, including its
// `TypeParameter` `SimpleType`).
TEST(CSharp_DelegateDeclaration, CloneCopiesConstraints) {
    auto a = make_DelegateDeclarationFull();
    auto clone = std::unique_ptr<DelegateDeclaration>(
        static_cast<DelegateDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Constraints().Count(), 1u);
    auto* clonedConstraint = clone->Constraints().At(0);
    EXPECT_NE(clonedConstraint, a->Constraints().At(0));
    ASSERT_NE(clonedConstraint->TypeParameter(), nullptr);
    EXPECT_NE(clonedConstraint->TypeParameter(), a->Constraints().At(0)->TypeParameter());
}

// `Clone` of a node with no collections clones with empty collections (a delegate with no
// `TypeParameters`/`Parameters`/`Constraints`).
TEST(CSharp_DelegateDeclaration, CloneEmptyCollections) {
    auto a = make_DelegateDeclaration();  // no collections
    auto clone = std::unique_ptr<DelegateDeclaration>(
        static_cast<DelegateDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->TypeParameters().Count(), 0u);
    EXPECT_EQ(clone->Parameters().Count(), 0u);
    EXPECT_EQ(clone->Constraints().Count(), 0u);
    EXPECT_EQ(clone->Attributes().Count(), 0u);
    ASSERT_NE(clone->NameToken(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `ReturnType` + `NameToken` required slots filled).
TEST(CSharp_DelegateDeclaration, CheckInvariantPassesOnFilled) {
    auto dd = make_DelegateDeclaration();  // ReturnType + NameToken filled
    dd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_DelegateDeclaration, CheckInvariantPassesOnFull) {
    auto dd = make_DelegateDeclarationFull();
    dd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `ReturnType` and `NameToken` are REQUIRED
// slots, so a default-constructed node violates the required-slot invariant -- the assert fires
// in debug).
#ifndef NDEBUG
TEST(CSharp_DelegateDeclaration, CheckInvariantRejectsEmpty) {
    DelegateDeclaration dd;  // no ReturnType, no NameToken
    EXPECT_DEATH(dd.CheckInvariant(), "");
}
#endif
