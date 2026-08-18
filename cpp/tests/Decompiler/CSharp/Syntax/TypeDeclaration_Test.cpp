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

// Tests for the `TypeDeclaration` concrete node (cpp/.../Syntax/TypeDeclaration.hpp, the port of
// the `TypeDeclaration` in ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/TypeDeclaration.cs)
// -- the central type-declaration node, the next in-order Phase-5 piece per the D291 plan (the
// `DelegateDeclaration` D291 four-collection shape with two more collections
// (`PrimaryConstructorParameters`/`Members`) inserted, the `ReturnType` single removed, plus two
// scalars (`ClassType`/`HasPrimaryConstructor`) interleaved -- the first ported node with SIX
// collections). The file carries one suite (CSharp_TypeDeclaration) with a `RecordingVisitor` and
// a `DoMatchAgainst` helper (the D234 shared-RecordingVisitor/DoMatchAgainst pattern, with
// `TypeHolder` structs keeping all children alive in the test scope -- the D223 non-owning
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
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DelegateDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitTypeDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`TypeParameterDeclaration`/
// `ParameterDeclaration`/`Constraint`/`MethodDeclaration` of the slots, and the
// `DelegateDeclaration`/`DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch
// rejections), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitTypeDeclaration(TypeDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-type>"); return; }
        trace.push_back("type:" + node->Name());
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
    void VisitMethodDeclaration(MethodDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-method>"); return; }
        trace.push_back("method:" + node->Name());
        VisitChildren(node);
    }
    void VisitDelegateDeclaration(DelegateDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-delegate>"); return; }
        trace.push_back("delegate:" + node->Name());
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

// A holder keeping a `TypeDeclaration` and all its children alive in the test scope (the port's
// non-owning raw-pointer child slots -- the D223 design).
struct TypeHolder {
    std::unique_ptr<TypeDeclaration> td;
    std::unique_ptr<Identifier> nameToken;
    // An Attributes AttributeSection holding an Attribute whose Type is a SimpleType.
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    // One TypeParameter "T" (kept alive with its own NameToken).
    std::unique_ptr<TypeParameterDeclaration> tp0;
    std::unique_ptr<Identifier> tp0NameToken;
    // One PrimaryConstructorParameter "int x" (kept alive with its own Type + NameToken).
    std::unique_ptr<ParameterDeclaration> pcp0;
    std::unique_ptr<SimpleType> pcp0Type;
    std::unique_ptr<Identifier> pcp0NameToken;
    // One BaseType "Base" (a SimpleType).
    std::unique_ptr<SimpleType> bt0;
    // One Constraint "where T :" (kept alive with its own TypeParameter SimpleType).
    std::unique_ptr<Constraint> constraint0;
    std::unique_ptr<SimpleType> constraint0TypeParam;
    // One Member: a MethodDeclaration "int Foo()" (kept alive with its own ReturnType + NameToken).
    std::unique_ptr<MethodDeclaration> member0;
    std::unique_ptr<SimpleType> member0ReturnType;
    std::unique_ptr<Identifier> member0NameToken;
    TypeDeclaration* get() const { return td.get(); }
    TypeDeclaration* operator->() const { return td.get(); }
};

// Build a `TypeDeclaration` with a `NameToken` `Foo` (the required slot filled), no attributes, no
// collections, `ClassType` defaults to `Class`, `HasPrimaryConstructor` defaults to false. The
// holder keeps every node alive.
TypeHolder make_TypeDeclaration() {
    TypeHolder h;
    h.td = std::make_unique<TypeDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.td->NameToken(h.nameToken.get());
    return h;
}

// Build a `TypeDeclaration` with a `NameToken` `Foo` and an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
TypeHolder make_TypeDeclarationWithAttribute() {
    TypeHolder h;
    h.td = std::make_unique<TypeDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.td->NameToken(h.nameToken.get());
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.td->Attributes().Add(h.attrSec.get());
    return h;
}

// Build a `TypeDeclaration` with every slot filled: a `NameToken` `Foo`, an `Attributes`
// `AttributeSection` with an `Attribute` `Foo`, a `TypeParameters` collection holding one
// `TypeParameterDeclaration` `T`, a `PrimaryConstructorParameters` collection holding one
// `ParameterDeclaration` `int x`, a `BaseTypes` collection holding one `SimpleType` `Base`, a
// `Constraints` collection holding one `Constraint` `where T :` (no base types), and a `Members`
// collection holding one `MethodDeclaration` `int Foo()` -- every slot filled. `ClassType` is set
// to `Class` and `HasPrimaryConstructor` to true (to exercise the primary-constructor path).
TypeHolder make_TypeDeclarationFull() {
    TypeHolder h;
    h.td = std::make_unique<TypeDeclaration>();
    // NameToken Foo
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.td->NameToken(h.nameToken.get());
    // ClassType + HasPrimaryConstructor scalars
    h.td->ClassType(ClassType::Class);
    h.td->HasPrimaryConstructor(true);
    // Attribute
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.td->Attributes().Add(h.attrSec.get());
    // One type parameter T
    h.tp0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("T")));
    h.tp0 = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    h.tp0->NameToken(h.tp0NameToken.get());
    h.td->TypeParameters().Add(h.tp0.get());
    // One primary-constructor parameter int x
    h.pcp0 = std::make_unique<ParameterDeclaration>();
    h.pcp0Type = std::make_unique<SimpleType>(std::string("int"));
    h.pcp0->Type(h.pcp0Type.get());
    h.pcp0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.pcp0->NameToken(h.pcp0NameToken.get());
    h.td->PrimaryConstructorParameters().Add(h.pcp0.get());
    // One base type Base
    h.bt0 = std::make_unique<SimpleType>(std::string("Base"));
    h.td->BaseTypes().Add(h.bt0.get());
    // One constraint where T : (no base types)
    h.constraint0TypeParam = std::make_unique<SimpleType>(std::string("T"));
    h.constraint0 = std::make_unique<Constraint>(h.constraint0TypeParam.get());
    h.td->Constraints().Add(h.constraint0.get());
    // One member: a method int Foo()
    h.member0 = std::make_unique<MethodDeclaration>();
    h.member0ReturnType = std::make_unique<SimpleType>(std::string("int"));
    h.member0->ReturnType(h.member0ReturnType.get());
    h.member0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.member0->NameToken(h.member0NameToken.get());
    h.td->Members().Add(h.member0.get());
    return h;
}

} // namespace

// ==========================================================================
// TypeDeclaration (the type_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `TypeDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_TypeDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<TypeDeclaration>);
}

// `TypeDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives from
// `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_TypeDeclaration, IsEntityDeclarationAndAstNode) {
    TypeDeclaration td;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&td), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&td), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&td), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&td), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&td), nullptr);
}

// ---- The ClassType enum ---------------------------------------------------

TEST(CSharp_TypeDeclaration, ClassTypeEnumValues) {
    EXPECT_EQ(static_cast<int>(ClassType::Class), 0);
    EXPECT_EQ(static_cast<int>(ClassType::Struct), 1);
    EXPECT_EQ(static_cast<int>(ClassType::Interface), 2);
    EXPECT_EQ(static_cast<int>(ClassType::Enum), 3);
    EXPECT_EQ(static_cast<int>(ClassType::RecordClass), 4);
    EXPECT_EQ(static_cast<int>(ClassType::RecordStruct), 5);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `NameToken`, empty `Attributes`/`TypeParameters`/
// `PrimaryConstructorParameters`/`BaseTypes`/`Constraints`/`Members`. `GetChildCount` is
// `0 + 0 + 0 + 0 + 0 + 0 + 1 = 1` (the six empty collections plus the one single slot -- each
// single slot contributes 1 to the flattened count even when absent). `Modifiers` defaults to
// `None`; `SymbolKind` is `TypeDefinition`; `ClassType` defaults to `Class`;
// `HasPrimaryConstructor` defaults to false.
TEST(CSharp_TypeDeclaration, EmptyCtor) {
    TypeDeclaration td;
    EXPECT_EQ(td.SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(td.Modifiers(), Modifiers::None);
    EXPECT_EQ(td.ClassType(), ClassType::Class);
    EXPECT_FALSE(td.HasPrimaryConstructor());
    EXPECT_EQ(td.NameToken(), nullptr);
    EXPECT_EQ(td.ReturnType(), nullptr);  // no Type slot -- the inherited base kind-walk
    EXPECT_EQ(td.TypeParameters().Count(), 0);
    EXPECT_EQ(td.PrimaryConstructorParameters().Count(), 0);
    EXPECT_EQ(td.BaseTypes().Count(), 0);
    EXPECT_EQ(td.Constraints().Count(), 0);
    EXPECT_EQ(td.Members().Count(), 0);
    EXPECT_EQ(td.Attributes().Count(), 0);
    EXPECT_EQ(td.GetChildCount(), 1);  // 0 attrs + 0 tps + 0 pcp + 0 bt + 0 c + 0 m + 1 single
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_TypeDeclaration, SymbolKindOverrideReturnsTypeDefinition) {
    TypeDeclaration td;
    EXPECT_EQ(td.SymbolKind(), SymbolKind::TypeDefinition);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_TypeDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    TypeDeclaration td;
    EXPECT_EQ(td.Modifiers(), Modifiers::None);
    td.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(td.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_TypeDeclaration, HasModifierIsBitmaskTest) {
    TypeDeclaration td;
    td.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(td.HasModifier(Modifiers::Static));
    EXPECT_TRUE(td.HasModifier(Modifiers::Public));
    EXPECT_FALSE(td.HasModifier(Modifiers::Virtual));
}

// ---- The ClassType scalar (a settable enum, no Any) ----------------------

TEST(CSharp_TypeDeclaration, ClassTypeRoundTrips) {
    TypeDeclaration td;
    EXPECT_EQ(td.ClassType(), ClassType::Class);  // default zero value
    td.ClassType(ClassType::Struct);
    EXPECT_EQ(td.ClassType(), ClassType::Struct);
    td.ClassType(ClassType::RecordClass);
    EXPECT_EQ(td.ClassType(), ClassType::RecordClass);
}

// ---- The HasPrimaryConstructor scalar (a settable bool) ------------------

TEST(CSharp_TypeDeclaration, HasPrimaryConstructorRoundTrips) {
    TypeDeclaration td;
    EXPECT_FALSE(td.HasPrimaryConstructor());  // default false
    td.HasPrimaryConstructor(true);
    EXPECT_TRUE(td.HasPrimaryConstructor());
    td.HasPrimaryConstructor(false);
    EXPECT_FALSE(td.HasPrimaryConstructor());
}

// ---- The Name/NameToken (NameToken is a real [Slot], NOT [ExcludeFromMatch]) ----

// `Name` returns the `NameToken`'s name (the inherited base `Name()` kind-walks for the
// `Identifier` kind and returns the token's `Name`). A type's name is a real name.
TEST(CSharp_TypeDeclaration, NameReturnsNameTokenName) {
    auto td = make_TypeDeclaration();
    EXPECT_EQ(td->Name(), "Foo");
}

// `NameToken` returns the backing field directly (the generated `get => field!`).
TEST(CSharp_TypeDeclaration, NameTokenReturnsBackingField) {
    auto td = make_TypeDeclaration();
    EXPECT_EQ(td->NameToken(), td->NameToken());
    EXPECT_NE(td->NameToken(), nullptr);
    EXPECT_EQ(td->NameToken()->Name(), "Foo");
}

// `NameToken` setter re-parents and re-indexes (the index-less setter following the `Attributes`
// collection).
TEST(CSharp_TypeDeclaration, NameTokenSetterReparentsAndDetaches) {
    TypeDeclaration td;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    td.NameToken(a.get());
    EXPECT_EQ(td.NameToken(), a.get());
    EXPECT_EQ(a->Parent(), &td);
    td.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(td.NameToken(), b.get());
    td.NameToken(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(td.NameToken(), nullptr);
}

// ---- The ReturnType (no Type slot -- the inherited base kind-walk) --------

// `ReturnType` returns null (the node declares no `[Slot("Type")]` ReturnType -- the inherited
// base `EntityDeclaration::ReturnType()` kind-walks for the `Type` kind and finds none).
TEST(CSharp_TypeDeclaration, ReturnTypeIsNull) {
    auto td = make_TypeDeclaration();
    EXPECT_EQ(td->ReturnType(), nullptr);
}

// ---- The collections (non-incremental) ------------------------------------

TEST(CSharp_TypeDeclaration, TypeParametersCollectionAddReparents) {
    TypeDeclaration td;
    auto tp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    td.TypeParameters().Add(tp.get());
    EXPECT_EQ(td.TypeParameters().Count(), 1);
    EXPECT_EQ(tp->Parent(), &td);
}

TEST(CSharp_TypeDeclaration, PrimaryConstructorParametersCollectionAddReparents) {
    TypeDeclaration td;
    auto p = std::make_unique<ParameterDeclaration>();
    td.PrimaryConstructorParameters().Add(p.get());
    EXPECT_EQ(td.PrimaryConstructorParameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &td);
}

TEST(CSharp_TypeDeclaration, BaseTypesCollectionAddReparents) {
    TypeDeclaration td;
    auto bt = std::make_unique<SimpleType>(std::string("Base"));
    td.BaseTypes().Add(bt.get());
    EXPECT_EQ(td.BaseTypes().Count(), 1);
    EXPECT_EQ(bt->Parent(), &td);
}

TEST(CSharp_TypeDeclaration, ConstraintsCollectionAddReparents) {
    TypeDeclaration td;
    auto tp = std::make_unique<SimpleType>(std::string("T"));
    auto c = std::make_unique<Constraint>(tp.get());
    td.Constraints().Add(c.get());
    EXPECT_EQ(td.Constraints().Count(), 1);
    EXPECT_EQ(c->Parent(), &td);
}

TEST(CSharp_TypeDeclaration, MembersCollectionAddReparents) {
    TypeDeclaration td;
    auto m = std::make_unique<MethodDeclaration>();
    td.Members().Add(m.get());
    EXPECT_EQ(td.Members().Count(), 1);
    EXPECT_EQ(m->Parent(), &td);
}

TEST(CSharp_TypeDeclaration, AttributesCollectionAddReparents) {
    TypeDeclaration td;
    auto attrSec = std::make_unique<AttributeSection>();
    td.Attributes().Add(attrSec.get());
    EXPECT_EQ(td.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &td);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `NameToken`
// single at `attrCount`, the `TypeParameters` collection, the `PrimaryConstructorParameters`
// collection, the `BaseTypes` collection, the `Constraints` collection, and the `Members`
// collection.
TEST(CSharp_TypeDeclaration, GetChildWalksSlots) {
    auto td = make_TypeDeclarationFull();
    // 1 attr + 1 tpp + 1 pcp + 1 bt + 1 constraint + 1 member + 1 single = 7
    EXPECT_EQ(td->GetChildCount(), 7);
    EXPECT_EQ(td->GetChild(0), td->Attributes().At(0));                    // Attributes[0]
    EXPECT_EQ(td->GetChild(1), td->NameToken());                          // NameToken
    EXPECT_EQ(td->GetChild(2), td->TypeParameters().At(0));               // TypeParameters[0]
    EXPECT_EQ(td->GetChild(3), td->PrimaryConstructorParameters().At(0)); // PrimaryConstructorParameters[0]
    EXPECT_EQ(td->GetChild(4), td->BaseTypes().At(0));                    // BaseTypes[0]
    EXPECT_EQ(td->GetChild(5), td->Constraints().At(0));                  // Constraints[0]
    EXPECT_EQ(td->GetChild(6), td->Members().At(0));                      // Members[0]
    EXPECT_THROW(td->GetChild(7), std::out_of_range);
}

TEST(CSharp_TypeDeclaration, GetChildSlotInfoWalksSlots) {
    auto td = make_TypeDeclarationFull();
    EXPECT_EQ(td->GetChildSlotInfo(0), &td->AttributesSlot);
    EXPECT_EQ(td->GetChildSlotInfo(1), &td->NameTokenSlot);
    EXPECT_EQ(td->GetChildSlotInfo(2), &td->TypeParametersSlot);
    EXPECT_EQ(td->GetChildSlotInfo(3), &td->PrimaryConstructorParametersSlot);
    EXPECT_EQ(td->GetChildSlotInfo(4), &td->BaseTypesSlot);
    EXPECT_EQ(td->GetChildSlotInfo(5), &td->ConstraintsSlot);
    EXPECT_EQ(td->GetChildSlotInfo(6), &td->MembersSlot);
    EXPECT_THROW(td->GetChildSlotInfo(7), std::out_of_range);
}

TEST(CSharp_TypeDeclaration, GetCollectionByKindReturnsAllSixCollections) {
    TypeDeclaration td;
    EXPECT_EQ(td.GetCollectionByKind(&Slots::AttributeSection), &td.Attributes());
    EXPECT_EQ(td.GetCollectionByKind(&Slots::TypeParameter), &td.TypeParameters());
    EXPECT_EQ(td.GetCollectionByKind(&Slots::Parameter), &td.PrimaryConstructorParameters());
    EXPECT_EQ(td.GetCollectionByKind(&Slots::BaseType), &td.BaseTypes());
    EXPECT_EQ(td.GetCollectionByKind(&Slots::Constraint), &td.Constraints());
    EXPECT_EQ(td.GetCollectionByKind(&Slots::TypeMember), &td.Members());
    EXPECT_EQ(td.GetCollectionByKind(&Slots::Type), nullptr);  // no single-slot kinds
    EXPECT_EQ(td.GetCollectionByKind(&Slots::Identifier), nullptr);
}

// `SetChild` replaces the `NameToken` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_TypeDeclaration, SetChildReplacesNameToken) {
    auto td = make_TypeDeclaration();
    auto nt2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    auto* oldNt = td->NameToken();
    td->SetChild(0, nt2.get());  // NameToken at flattened index 0 (0 attrs)
    EXPECT_EQ(td->NameToken(), nt2.get());
    EXPECT_EQ(oldNt->Parent(), nullptr);
}

// `SetChild` replaces a `Members` element in place (a collection element at the flattened index).
TEST(CSharp_TypeDeclaration, SetChildReplacesMember) {
    auto td = make_TypeDeclarationFull();
    auto m2 = std::make_unique<MethodDeclaration>();
    auto* oldMember = td->Members().At(0);
    td->SetChild(6, m2.get());  // Members[0] at flattened index 6
    EXPECT_EQ(td->Members().At(0), m2.get());
    EXPECT_EQ(oldMember->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_TypeDeclaration, SlotStaticsPointAtSharedKinds) {
    TypeDeclaration td;
    EXPECT_EQ(td.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(td.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(td.TypeParametersSlot.Kind(), &Slots::TypeParameter);
    EXPECT_EQ(td.PrimaryConstructorParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(td.BaseTypesSlot.Kind(), &Slots::BaseType);
    EXPECT_EQ(td.ConstraintsSlot.Kind(), &Slots::Constraint);
    EXPECT_EQ(td.MembersSlot.Kind(), &Slots::TypeMember);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_TypeDeclaration, SlotStaticsAreDistinct) {
    TypeDeclaration td;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&td.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&td.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&td.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&td.TypeParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&td.TypeParametersSlot),
              static_cast<const CSharpSlotInfo*>(&td.PrimaryConstructorParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&td.PrimaryConstructorParametersSlot),
              static_cast<const CSharpSlotInfo*>(&td.BaseTypesSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&td.BaseTypesSlot),
              static_cast<const CSharpSlotInfo*>(&td.ConstraintsSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&td.ConstraintsSlot),
              static_cast<const CSharpSlotInfo*>(&td.MembersSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_TypeDeclaration, AcceptVisitorDispatches) {
    auto td = make_TypeDeclaration();
    // Name() derefs the NameToken; a filled node records the type tag then recurses into the
    // NameToken (a visited child).
    RecordingVisitor v;
    td->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"type:Foo", "id:Foo"}));
}

TEST(CSharp_TypeDeclaration, AcceptVisitorVirtualThroughBase) {
    auto td = make_TypeDeclaration();
    AstNode* node = td.get();
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"type:Foo", "id:Foo"}));
}

TEST(CSharp_TypeDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    auto td = make_TypeDeclaration();
    EntityDeclaration* node = td.get();
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"type:Foo", "id:Foo"}));
}

// The depth-first walk recurses into the `NameToken` (`Identifier` `Foo`) in document order (no
// attributes, no collections).
TEST(CSharp_TypeDeclaration, DepthFirstWalk) {
    auto td = make_TypeDeclaration();
    RecordingVisitor v;
    td->AcceptVisitor(v);
    // type:Foo -> id:Foo (NameToken)
    EXPECT_EQ(v.trace, std::vector<std::string>({"type:Foo", "id:Foo"}));
}

// A `TypeDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute` ->
// its `SimpleType` `Type` -> its `Identifier` BEFORE the `NameToken` (the `Attributes` collection
// is the first slot).
TEST(CSharp_TypeDeclaration, DepthFirstWalkWithAttribute) {
    auto td = make_TypeDeclarationWithAttribute();
    RecordingVisitor v;
    td->AcceptVisitor(v);
    // type:Foo -> attrsec (Attributes[0]) -> attr -> simple:Foo -> id:Foo
    //      -> id:Foo (NameToken)
    ASSERT_EQ(v.trace.size(), 6u);
    EXPECT_EQ(v.trace[0], "type:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:Foo");  // the NameToken
}

// A `TypeDeclaration` with every slot filled recurses into the `Attributes`, `NameToken`, each
// `TypeParameter` (and its `NameToken`), each `PrimaryConstructorParameter` (and its `Type`/
// `NameToken`), each `BaseType` (and its `Identifier`), each `Constraint` (and its `TypeParameter`),
// and each `Member` (the `MethodDeclaration` and its `ReturnType`/`NameToken`), in slot order.
TEST(CSharp_TypeDeclaration, DepthFirstWalkFull) {
    auto td = make_TypeDeclarationFull();
    RecordingVisitor v;
    td->AcceptVisitor(v);
    // type:Foo -> attrsec -> attr -> simple:Foo -> id:Foo (Attribute)
    //      -> id:Foo (NameToken)
    //      -> tpp:T -> id:T (TypeParameters[0])
    //      -> param (PrimaryConstructorParameters[0]) -> simple:int -> id:int -> id:x
    //      -> simple:Base -> id:Base (BaseTypes[0])
    //      -> constraint (Constraints[0]) -> simple:T -> id:T
    //      -> method:Foo (Members[0]) -> simple:int -> id:int -> id:Foo
    ASSERT_EQ(v.trace.size(), 21u);
    EXPECT_EQ(v.trace[0], "type:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:Foo");  // NameToken
    EXPECT_EQ(v.trace[6], "tpp:T");
    EXPECT_EQ(v.trace[7], "id:T");
    EXPECT_EQ(v.trace[8], "param");
    EXPECT_EQ(v.trace[9], "simple:int");
    EXPECT_EQ(v.trace[10], "id:int");
    EXPECT_EQ(v.trace[11], "id:x");
    EXPECT_EQ(v.trace[12], "simple:Base");
    EXPECT_EQ(v.trace[13], "id:Base");
    EXPECT_EQ(v.trace[14], "constraint");
    EXPECT_EQ(v.trace[15], "simple:T");
    EXPECT_EQ(v.trace[16], "id:T");
    EXPECT_EQ(v.trace[17], "method:Foo");
    EXPECT_EQ(v.trace[18], "simple:int");
    EXPECT_EQ(v.trace[19], "id:int");
    EXPECT_EQ(v.trace[20], "id:Foo");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_TypeDeclaration, DoMatchSameNode) {
    auto a = make_TypeDeclaration();
    auto b = make_TypeDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]`).
TEST(CSharp_TypeDeclaration, DoMatchNameMismatchRejects) {
    auto a = make_TypeDeclaration();
    auto b = make_TypeDeclaration();
    auto bName = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    b->NameToken(bName.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_TypeDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_TypeDeclaration();
    auto b = make_TypeDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_TypeDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_TypeDeclaration();
    auto b = make_TypeDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ClassType` mismatch rejects (the plain `==` term -- `ClassType` has no `Any` member).
TEST(CSharp_TypeDeclaration, DoMatchClassTypeMismatchRejects) {
    auto a = make_TypeDeclaration();
    auto b = make_TypeDeclaration();
    a->ClassType(ClassType::Class);
    b->ClassType(ClassType::Struct);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `HasPrimaryConstructor` mismatch rejects (the plain `==` term).
TEST(CSharp_TypeDeclaration, DoMatchHasPrimaryConstructorMismatchRejects) {
    auto a = make_TypeDeclaration();
    auto b = make_TypeDeclaration();
    a->HasPrimaryConstructor(true);
    b->HasPrimaryConstructor(false);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `TypeParameters` mismatch rejects (the collection recursive `DoMatch` -- a pattern with a
// type parameter vs a candidate without one).
TEST(CSharp_TypeDeclaration, DoMatchTypeParametersMismatchRejects) {
    auto a = make_TypeDeclarationFull();  // one type parameter
    auto b = make_TypeDeclaration();  // no type parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `PrimaryConstructorParameters` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_TypeDeclaration, DoMatchPrimaryConstructorParametersMismatchRejects) {
    auto a = make_TypeDeclarationFull();  // one primary-constructor parameter
    auto b = make_TypeDeclaration();  // no primary-constructor parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `BaseTypes` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_TypeDeclaration, DoMatchBaseTypesMismatchRejects) {
    auto a = make_TypeDeclarationFull();  // one base type
    auto b = make_TypeDeclaration();  // no base types
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Constraints` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_TypeDeclaration, DoMatchConstraintsMismatchRejects) {
    auto a = make_TypeDeclarationFull();  // one constraint
    auto b = make_TypeDeclaration();  // no constraints
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Members` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_TypeDeclaration, DoMatchMembersMismatchRejects) {
    auto a = make_TypeDeclarationFull();  // one member
    auto b = make_TypeDeclaration();  // no members
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single type parameter match (the collection recursive `DoMatch`
// accepts equal-length collections whose elements match).
TEST(CSharp_TypeDeclaration, DoMatchSameTypeParametersMatch) {
    auto a = make_TypeDeclaration();
    auto aTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    a->TypeParameters().Add(aTp.get());
    auto b = make_TypeDeclaration();
    auto bTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    b->TypeParameters().Add(bTp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single base type match.
TEST(CSharp_TypeDeclaration, DoMatchSameBaseTypesMatch) {
    auto a = make_TypeDeclaration();
    auto aBt = std::make_unique<SimpleType>(std::string("Base"));
    a->BaseTypes().Add(aBt.get());
    auto b = make_TypeDeclaration();
    auto bBt = std::make_unique<SimpleType>(std::string("Base"));
    b->BaseTypes().Add(bBt.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single member match.
TEST(CSharp_TypeDeclaration, DoMatchSameMembersMatch) {
    auto a = make_TypeDeclaration();
    auto aM = std::make_unique<MethodDeclaration>();
    a->Members().Add(aM.get());
    auto b = make_TypeDeclaration();
    auto bM = std::make_unique<MethodDeclaration>();
    b->Members().Add(bM.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A full node matches a structurally-identical full node (every slot + scalar matched).
TEST(CSharp_TypeDeclaration, DoMatchFullMatchesFull) {
    auto a = make_TypeDeclarationFull();
    auto b = make_TypeDeclarationFull();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`TypeDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DelegateDeclaration` is an `EntityDeclaration` but not a `TypeDeclaration`).
TEST(CSharp_TypeDeclaration, DoMatchRejectsNonTypeEntityDeclaration) {
    auto a = make_TypeDeclaration();
    auto b = std::make_unique<DelegateDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_TypeDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_TypeDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_TypeDeclaration, DoMatchRejectsNull) {
    auto a = make_TypeDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `NameToken`, copies the `Modifiers`/`ClassType`/`HasPrimaryConstructor`
// scalars, and does not detach the source.
TEST(CSharp_TypeDeclaration, CloneDeepCopies) {
    auto a = make_TypeDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->ClassType(), ClassType::Class);
    EXPECT_TRUE(clone->HasPrimaryConstructor());
    EXPECT_EQ(clone->Name(), "Foo");
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    // The source is not detached.
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `TypeDeclaration*`.
TEST(CSharp_TypeDeclaration, CloneVirtualAndCovariant) {
    auto a = make_TypeDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<TypeDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<TypeDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_TypeDeclaration, CloneCopiesAttributes) {
    auto a = make_TypeDeclarationWithAttribute();
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->NameToken()->Parent(), clone.get());
}

// `Clone` copies the `TypeParameters` collection (each element deep-cloned).
TEST(CSharp_TypeDeclaration, CloneCopiesTypeParameters) {
    auto a = make_TypeDeclarationFull();
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->TypeParameters().Count(), 1u);
    auto* clonedTp = clone->TypeParameters().At(0);
    EXPECT_NE(clonedTp, a->TypeParameters().At(0));
    ASSERT_NE(clonedTp->NameToken(), nullptr);
    EXPECT_NE(clonedTp->NameToken(), a->TypeParameters().At(0)->NameToken());
    EXPECT_EQ(clonedTp->NameToken()->Name(), "T");
}

// `Clone` copies the `PrimaryConstructorParameters` collection (each element deep-cloned, including
// its `Type`/`NameToken`).
TEST(CSharp_TypeDeclaration, CloneCopiesPrimaryConstructorParameters) {
    auto a = make_TypeDeclarationFull();
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->PrimaryConstructorParameters().Count(), 1u);
    auto* clonedParam = clone->PrimaryConstructorParameters().At(0);
    EXPECT_NE(clonedParam, a->PrimaryConstructorParameters().At(0));
    ASSERT_NE(clonedParam->Type(), nullptr);
    EXPECT_NE(clonedParam->Type(), a->PrimaryConstructorParameters().At(0)->Type());
    ASSERT_NE(clonedParam->NameToken(), nullptr);
    EXPECT_NE(clonedParam->NameToken(), a->PrimaryConstructorParameters().At(0)->NameToken());
    EXPECT_EQ(clonedParam->NameToken()->Name(), "x");
}

// `Clone` copies the `BaseTypes` collection (each element deep-cloned).
TEST(CSharp_TypeDeclaration, CloneCopiesBaseTypes) {
    auto a = make_TypeDeclarationFull();
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->BaseTypes().Count(), 1u);
    auto* clonedBt = clone->BaseTypes().At(0);
    EXPECT_NE(clonedBt, a->BaseTypes().At(0));
    ASSERT_NE(dynamic_cast<SimpleType*>(clonedBt), nullptr);
    EXPECT_EQ(dynamic_cast<SimpleType*>(clonedBt)->Identifier().value_or(""), "Base");
}

// `Clone` copies the `Constraints` collection (each element deep-cloned, including its
// `TypeParameter` `SimpleType`).
TEST(CSharp_TypeDeclaration, CloneCopiesConstraints) {
    auto a = make_TypeDeclarationFull();
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Constraints().Count(), 1u);
    auto* clonedConstraint = clone->Constraints().At(0);
    EXPECT_NE(clonedConstraint, a->Constraints().At(0));
    ASSERT_NE(clonedConstraint->TypeParameter(), nullptr);
    EXPECT_NE(clonedConstraint->TypeParameter(), a->Constraints().At(0)->TypeParameter());
}

// `Clone` copies the `Members` collection (each element deep-cloned via the abstract-base
// `static_cast<EntityDeclaration*>(elem->Clone())` path).
TEST(CSharp_TypeDeclaration, CloneCopiesMembers) {
    auto a = make_TypeDeclarationFull();
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Members().Count(), 1u);
    auto* clonedMember = clone->Members().At(0);
    EXPECT_NE(clonedMember, a->Members().At(0));
    auto* clonedMethod = dynamic_cast<MethodDeclaration*>(clonedMember);
    ASSERT_NE(clonedMethod, nullptr);
    ASSERT_NE(clonedMethod->ReturnType(), nullptr);
    EXPECT_NE(clonedMethod->ReturnType(), a->Members().At(0)->ReturnType());
    ASSERT_NE(clonedMethod->NameToken(), nullptr);
    EXPECT_NE(clonedMethod->NameToken(), a->Members().At(0)->NameToken());
    EXPECT_EQ(clonedMethod->Name(), "Foo");
}

// `Clone` of a node with no collections clones with empty collections.
TEST(CSharp_TypeDeclaration, CloneEmptyCollections) {
    auto a = make_TypeDeclaration();  // no collections
    auto clone = std::unique_ptr<TypeDeclaration>(
        static_cast<TypeDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->TypeParameters().Count(), 0u);
    EXPECT_EQ(clone->PrimaryConstructorParameters().Count(), 0u);
    EXPECT_EQ(clone->BaseTypes().Count(), 0u);
    EXPECT_EQ(clone->Constraints().Count(), 0u);
    EXPECT_EQ(clone->Members().Count(), 0u);
    EXPECT_EQ(clone->Attributes().Count(), 0u);
    ASSERT_NE(clone->NameToken(), nullptr);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `NameToken` required slot filled).
TEST(CSharp_TypeDeclaration, CheckInvariantPassesOnFilled) {
    auto td = make_TypeDeclaration();  // NameToken filled
    td->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_TypeDeclaration, CheckInvariantPassesOnFull) {
    auto td = make_TypeDeclarationFull();
    td->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NameToken` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_TypeDeclaration, CheckInvariantRejectsEmpty) {
    TypeDeclaration td;  // no NameToken
    EXPECT_DEATH(td.CheckInvariant(), "");
}
#endif
