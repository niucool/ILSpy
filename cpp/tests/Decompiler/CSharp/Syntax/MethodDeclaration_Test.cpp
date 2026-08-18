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
// OTHERWISE, ARISING, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `MethodDeclaration` concrete node (cpp/.../Syntax/MethodDeclaration.hpp, the port
// of the `MethodDeclaration` in ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/MethodDeclaration.cs)
// -- the next in-order Phase-5 piece per the D283 plan (the first `EntityDeclaration` with more
// than two collections: four collections `Attributes`/`TypeParameters`/`Parameters`/`Constraints`
// plus four singles `ReturnType`/`PrivateImplementationType`/`NameToken`/`Body`, now unblocked by
// `TypeParameterDeclaration` D282 + `ParameterDeclaration` D278 + `Constraint` D283). The file
// carries one suite (CSharp_MethodDeclaration) with a `RecordingVisitor` and a `DoMatchAgainst`
// helper (the D234 shared-RecordingVisitor/DoMatchAgainst pattern, with `MethodHolder` structs
// keeping all children alive in the test scope -- the D223 non-owning raw-pointer model).

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
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
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
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitMethodDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`PrimitiveExpression`/
// `TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint`/`BlockStatement`/`ReturnStatement`
// of the slots, and the `DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch
// rejections), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitMethodDeclaration(MethodDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-method>"); return; }
        trace.push_back("method:" + node->Name());
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
// pattern). Uses `Match::CreateNew()` (a default-constructed `Match` holds a null capture vector
// -- the D219 distinction; the collection `DoMatch` derefs it).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping a `MethodDeclaration` and all its children alive in the test scope (the port's
// non-owning raw-pointer child slots -- the D223 design).
struct MethodHolder {
    std::unique_ptr<MethodDeclaration> md;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<SimpleType> privateImplType;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<BlockStatement> body;
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
    MethodDeclaration* get() const { return md.get(); }
    MethodDeclaration* operator->() const { return md.get(); }
};

// Build a `MethodDeclaration` with a `NameToken` `Foo` and a `ReturnType` `int` (both required
// slots filled), no attributes, no `PrivateImplementationType`, no `TypeParameters`, no
// `Parameters`, no `Constraints`, no `Body`. The holder keeps every node alive.
MethodHolder make_MethodDeclaration() {
    MethodHolder h;
    h.md = std::make_unique<MethodDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.md->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.md->NameToken(h.nameToken.get());
    return h;
}

// Build a `MethodDeclaration` with a `NameToken` `Foo`, a `ReturnType` `int`, and an `Attributes`
// `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
MethodHolder make_MethodDeclarationWithAttribute() {
    MethodHolder h;
    h.md = std::make_unique<MethodDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.md->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.md->NameToken(h.nameToken.get());
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.md->Attributes().Add(h.attrSec.get());
    return h;
}

// Build a `MethodDeclaration` with every slot filled: a `NameToken` `Foo`, a `ReturnType` `int`, a
// `PrivateImplementationType` `I`, an `Attributes` `AttributeSection` with an `Attribute` `Foo`, a
// `TypeParameters` collection holding one `TypeParameterDeclaration` `T`, a `Parameters`
// collection holding one `ParameterDeclaration` `int x`, a `Constraints` collection holding one
// `Constraint` `where T :` (no base types), and a `Body` `BlockStatement` -- every slot filled.
MethodHolder make_MethodDeclarationFull() {
    MethodHolder h;
    h.md = std::make_unique<MethodDeclaration>();
    // ReturnType int
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.md->ReturnType(h.returnType.get());
    // PrivateImplementationType I
    h.privateImplType = std::make_unique<SimpleType>(std::string("I"));
    h.md->PrivateImplementationType(h.privateImplType.get());
    // NameToken Foo
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.md->NameToken(h.nameToken.get());
    // Attribute
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.md->Attributes().Add(h.attrSec.get());
    // One type parameter T
    h.tp0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("T")));
    h.tp0 = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    h.tp0->NameToken(h.tp0NameToken.get());
    h.md->TypeParameters().Add(h.tp0.get());
    // One parameter int x
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.param0Type = std::make_unique<SimpleType>(std::string("int"));
    h.param0->Type(h.param0Type.get());
    h.param0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param0->NameToken(h.param0NameToken.get());
    h.md->Parameters().Add(h.param0.get());
    // One constraint where T : (no base types)
    h.constraint0TypeParam = std::make_unique<SimpleType>(std::string("T"));
    h.constraint0 = std::make_unique<Constraint>(h.constraint0TypeParam.get());
    h.md->Constraints().Add(h.constraint0.get());
    // Body block
    h.body = std::make_unique<BlockStatement>();
    h.md->Body(h.body.get());
    return h;
}

} // namespace

// ==========================================================================
// MethodDeclaration (the method_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `MethodDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_MethodDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<MethodDeclaration>);
}

// `MethodDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_MethodDeclaration, IsEntityDeclarationAndAstNode) {
    MethodDeclaration md;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&md), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&md), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&md), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&md), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&md), nullptr);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `NameToken`, no `ReturnType`, empty `Attributes`/`TypeParameters`/
// `Parameters`/`Constraints`, `PrivateImplementationType`/`Body` null. `GetChildCount` is
// `0 + 0 + 0 + 0 + 4 = 4` (the four empty collections plus the four single slots -- each single
// slot contributes 1 to the flattened count even when absent). `Modifiers` defaults to `None`;
// `SymbolKind` is `Method`.
TEST(CSharp_MethodDeclaration, EmptyCtor) {
    MethodDeclaration md;
    EXPECT_EQ(md.SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(md.Modifiers(), Modifiers::None);
    EXPECT_EQ(md.ReturnType(), nullptr);
    EXPECT_EQ(md.PrivateImplementationType(), nullptr);
    EXPECT_EQ(md.NameToken(), nullptr);
    EXPECT_EQ(md.TypeParameters().Count(), 0);
    EXPECT_EQ(md.Parameters().Count(), 0);
    EXPECT_EQ(md.Constraints().Count(), 0);
    EXPECT_EQ(md.Body(), nullptr);
    EXPECT_EQ(md.Attributes().Count(), 0);
    EXPECT_EQ(md.GetChildCount(), 4);  // 0 attrs + 0 tps + 0 params + 0 constraints + 4 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_MethodDeclaration, SymbolKindOverrideReturnsMethod) {
    MethodDeclaration md;
    EXPECT_EQ(md.SymbolKind(), SymbolKind::Method);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_MethodDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    MethodDeclaration md;
    EXPECT_EQ(md.Modifiers(), Modifiers::None);
    md.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(md.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_MethodDeclaration, HasModifierIsBitmaskTest) {
    MethodDeclaration md;
    md.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(md.HasModifier(Modifiers::Static));
    EXPECT_TRUE(md.HasModifier(Modifiers::Public));
    EXPECT_FALSE(md.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken (NameToken is a real [Slot], NOT [ExcludeFromMatch]) ----

// `Name` returns the `NameToken`'s name (the inherited base `Name()` kind-walks for the
// `Identifier` kind and returns the token's `Name`). A method's name is a real name.
TEST(CSharp_MethodDeclaration, NameReturnsNameTokenName) {
    auto md = make_MethodDeclaration();
    EXPECT_EQ(md->Name(), "Foo");
}

// `NameToken` returns the backing field directly (the generated `get => field!`).
TEST(CSharp_MethodDeclaration, NameTokenReturnsBackingField) {
    auto md = make_MethodDeclaration();
    EXPECT_EQ(md->NameToken(), md->NameToken());
    EXPECT_NE(md->NameToken(), nullptr);
    EXPECT_EQ(md->NameToken()->Name(), "Foo");
}

// `NameToken` setter re-parents and re-indexes (the index-less setter following the `Attributes`
// collection).
TEST(CSharp_MethodDeclaration, NameTokenSetterReparentsAndDetaches) {
    MethodDeclaration md;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    md.NameToken(a.get());
    EXPECT_EQ(md.NameToken(), a.get());
    EXPECT_EQ(a->Parent(), &md);
    md.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(md.NameToken(), b.get());
    md.NameToken(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(md.NameToken(), nullptr);
}

// ---- The ReturnType slot (override, required, index-less) ----------------

TEST(CSharp_MethodDeclaration, ReturnTypeSetterReparentsAndDetaches) {
    MethodDeclaration md;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("string"));
    md.ReturnType(a.get());
    EXPECT_EQ(md.ReturnType(), a.get());
    EXPECT_EQ(a->Parent(), &md);
    md.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(md.ReturnType(), b.get());
    md.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(md.ReturnType(), nullptr);
}

// ---- The PrivateImplementationType slot (nullable, index-less) ----------

TEST(CSharp_MethodDeclaration, PrivateImplementationTypeSetterReparentsAndDetaches) {
    MethodDeclaration md;
    auto a = std::make_unique<SimpleType>(std::string("I"));
    md.PrivateImplementationType(a.get());
    EXPECT_EQ(md.PrivateImplementationType(), a.get());
    EXPECT_EQ(a->Parent(), &md);
    md.PrivateImplementationType(nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(md.PrivateImplementationType(), nullptr);
}

// ---- The TypeParameters collection (non-incremental) ---------------------

TEST(CSharp_MethodDeclaration, TypeParametersCollectionAddReparents) {
    MethodDeclaration md;
    auto tp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    md.TypeParameters().Add(tp.get());
    EXPECT_EQ(md.TypeParameters().Count(), 1);
    EXPECT_EQ(tp->Parent(), &md);
}

// ---- The Parameters collection (non-incremental) -------------------------

TEST(CSharp_MethodDeclaration, ParametersCollectionAddReparents) {
    MethodDeclaration md;
    auto p = std::make_unique<ParameterDeclaration>();
    md.Parameters().Add(p.get());
    EXPECT_EQ(md.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &md);
}

// ---- The Constraints collection (non-incremental) ------------------------

TEST(CSharp_MethodDeclaration, ConstraintsCollectionAddReparents) {
    MethodDeclaration md;
    auto tp = std::make_unique<SimpleType>(std::string("T"));
    auto c = std::make_unique<Constraint>(tp.get());
    md.Constraints().Add(c.get());
    EXPECT_EQ(md.Constraints().Count(), 1);
    EXPECT_EQ(c->Parent(), &md);
}

// ---- The Attributes collection (non-incremental) -------------------------

TEST(CSharp_MethodDeclaration, AttributesCollectionAddReparents) {
    MethodDeclaration md;
    auto attrSec = std::make_unique<AttributeSection>();
    md.Attributes().Add(attrSec.get());
    EXPECT_EQ(md.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &md);
}

// ---- The Body slot (a NULLABLE single, index-less) ----------------------

TEST(CSharp_MethodDeclaration, BodySetterReparentsAndDetaches) {
    MethodDeclaration md;
    auto b = std::make_unique<BlockStatement>();
    md.Body(b.get());
    EXPECT_EQ(md.Body(), b.get());
    EXPECT_EQ(b->Parent(), &md);
    md.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(md.Body(), nullptr);
}

// ---- The IsExtensionMethod computed property -----------------------------

// A method with no parameters is NOT an extension method (`GetChildByKind<ParameterDeclaration>`
// returns null).
TEST(CSharp_MethodDeclaration, IsExtensionMethodFalseNoParameters) {
    auto md = make_MethodDeclaration();
    EXPECT_FALSE(md->IsExtensionMethod());
}

// A method with a parameter whose `HasThisModifier` is false is NOT an extension method.
TEST(CSharp_MethodDeclaration, IsExtensionMethodFalseNoThisModifier) {
    auto md = make_MethodDeclaration();
    auto p = std::make_unique<ParameterDeclaration>();
    p->HasThisModifier(false);
    md->Parameters().Add(p.get());
    EXPECT_FALSE(md->IsExtensionMethod());
}

// A method with a parameter whose `HasThisModifier` is true IS an extension method.
TEST(CSharp_MethodDeclaration, IsExtensionMethodTrueWithThisModifier) {
    auto md = make_MethodDeclaration();
    auto p = std::make_unique<ParameterDeclaration>();
    p->HasThisModifier(true);
    md->Parameters().Add(p.get());
    EXPECT_TRUE(md->IsExtensionMethod());
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `ReturnType`
// single at `attrCount`, the `PrivateImplementationType` single at `attrCount + 1`, the
// `NameToken` single at `attrCount + 2`, the `TypeParameters` collection
// `[attrCount + 3, ...)`, the `Parameters` collection, the `Constraints` collection, and the
// `Body` single at the end.
TEST(CSharp_MethodDeclaration, GetChildWalksSlots) {
    auto md = make_MethodDeclarationFull();
    // 1 attr + 1 tpp + 1 param + 1 constraint + 4 singles = 8
    EXPECT_EQ(md->GetChildCount(), 8);
    EXPECT_EQ(md->GetChild(0), md->Attributes().At(0));        // the AttributeSection
    EXPECT_EQ(md->GetChild(1), md->ReturnType());              // 1 (attrCount + ReturnType)
    EXPECT_EQ(md->GetChild(2), md->PrivateImplementationType());  // 2
    EXPECT_EQ(md->GetChild(3), md->NameToken());               // 3
    EXPECT_EQ(md->GetChild(4), md->TypeParameters().At(0));    // 4 (TypeParameters[0])
    EXPECT_EQ(md->GetChild(5), md->Parameters().At(0));        // 5 (Parameters[0])
    EXPECT_EQ(md->GetChild(6), md->Constraints().At(0));      // 6 (Constraints[0])
    EXPECT_EQ(md->GetChild(7), md->Body());                    // 7 (Body)
    EXPECT_THROW(md->GetChild(8), std::out_of_range);
}

TEST(CSharp_MethodDeclaration, GetChildSlotInfoWalksSlots) {
    auto md = make_MethodDeclarationFull();
    EXPECT_EQ(md->GetChildSlotInfo(0), &md->AttributesSlot);
    EXPECT_EQ(md->GetChildSlotInfo(1), &md->ReturnTypeSlot);
    EXPECT_EQ(md->GetChildSlotInfo(2), &md->PrivateImplementationTypeSlot);
    EXPECT_EQ(md->GetChildSlotInfo(3), &md->NameTokenSlot);
    EXPECT_EQ(md->GetChildSlotInfo(4), &md->TypeParametersSlot);
    EXPECT_EQ(md->GetChildSlotInfo(5), &md->ParametersSlot);
    EXPECT_EQ(md->GetChildSlotInfo(6), &md->ConstraintsSlot);
    EXPECT_EQ(md->GetChildSlotInfo(7), &md->BodySlot);
    EXPECT_THROW(md->GetChildSlotInfo(8), std::out_of_range);
}

TEST(CSharp_MethodDeclaration, GetCollectionByKindReturnsAllFourCollections) {
    MethodDeclaration md;
    EXPECT_EQ(md.GetCollectionByKind(&Slots::AttributeSection), &md.Attributes());
    EXPECT_EQ(md.GetCollectionByKind(&Slots::TypeParameter), &md.TypeParameters());
    EXPECT_EQ(md.GetCollectionByKind(&Slots::Parameter), &md.Parameters());
    EXPECT_EQ(md.GetCollectionByKind(&Slots::Constraint), &md.Constraints());
    EXPECT_EQ(md.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(md.GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(md.GetCollectionByKind(&Slots::Body), nullptr);
}

// `SetChild` replaces the `ReturnType` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_MethodDeclaration, SetChildReplacesReturnType) {
    auto md = make_MethodDeclaration();
    auto rt2 = std::make_unique<SimpleType>(std::string("string"));
    auto* oldRt = md->ReturnType();
    md->SetChild(0, rt2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(md->ReturnType(), rt2.get());
    EXPECT_EQ(oldRt->Parent(), nullptr);
}

// `SetChild` replaces a `Parameters` element in place.
TEST(CSharp_MethodDeclaration, SetChildReplacesParameter) {
    auto md = make_MethodDeclarationFull();
    auto p2 = std::make_unique<ParameterDeclaration>();
    auto* oldParam = md->Parameters().At(0);
    md->SetChild(5, p2.get());  // Parameters[0] at flattened index 5
    EXPECT_EQ(md->Parameters().At(0), p2.get());
    EXPECT_EQ(oldParam->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_MethodDeclaration, SlotStaticsPointAtSharedKinds) {
    MethodDeclaration md;
    EXPECT_EQ(md.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(md.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(md.PrivateImplementationTypeSlot.Kind(), &Slots::PrivateImplementationType);
    EXPECT_EQ(md.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(md.TypeParametersSlot.Kind(), &Slots::TypeParameter);
    EXPECT_EQ(md.ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(md.ConstraintsSlot.Kind(), &Slots::Constraint);
    EXPECT_EQ(md.BodySlot.Kind(), &Slots::Body);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_MethodDeclaration, SlotStaticsAreDistinct) {
    MethodDeclaration md;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&md.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&md.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&md.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&md.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&md.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&md.TypeParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&md.TypeParametersSlot),
              static_cast<const CSharpSlotInfo*>(&md.ParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&md.ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&md.ConstraintsSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&md.ConstraintsSlot),
              static_cast<const CSharpSlotInfo*>(&md.BodySlot));
}

// The two NEW cycle-broken `Slots` kinds (`Slots::TypeParameter` in `TypeParameterDeclaration.hpp`,
// `Slots::Constraint` in `Constraint.hpp`) are distinct from the other collection/single kinds
// (cast both to the common `CSharpSlotInfo*` base for the cross-element-type `EXPECT_NE`, the
// D251/D252/D262 precedent).
TEST(CSharp_MethodDeclaration, NewSlotKindsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeParameter),
              static_cast<const CSharpSlotInfo*>(&Slots::Constraint));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeParameter),
              static_cast<const CSharpSlotInfo*>(&Slots::AttributeSection));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeParameter),
              static_cast<const CSharpSlotInfo*>(&Slots::Parameter));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Constraint),
              static_cast<const CSharpSlotInfo*>(&Slots::Parameter));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::Constraint),
              static_cast<const CSharpSlotInfo*>(&Slots::Body));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_MethodDeclaration, AcceptVisitorDispatches) {
    MethodDeclaration md;  // no NameToken -- the empty node records just the node tag
    RecordingVisitor v;
    md.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"method:"}));
}

TEST(CSharp_MethodDeclaration, AcceptVisitorVirtualThroughBase) {
    MethodDeclaration md;
    AstNode* node = &md;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"method:"}));
}

TEST(CSharp_MethodDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    MethodDeclaration md;
    EntityDeclaration* node = &md;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"method:"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int` -> `Identifier` `int`)
// and the `NameToken` (`Identifier` `Foo`) in document order (no attributes, no
// `PrivateImplementationType`, no `TypeParameters`/`Parameters`/`Constraints`, no `Body`).
TEST(CSharp_MethodDeclaration, DepthFirstWalk) {
    auto md = make_MethodDeclaration();
    RecordingVisitor v;
    md->AcceptVisitor(v);
    // method:Foo -> simple:int -> id:int (ReturnType) -> id:Foo (NameToken)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "method:Foo", "simple:int", "id:int", "id:Foo",
    }));
}

// A `MethodDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute` ->
// its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` (the `Attributes` collection
// is the first slot).
TEST(CSharp_MethodDeclaration, DepthFirstWalkWithAttribute) {
    auto md = make_MethodDeclarationWithAttribute();
    RecordingVisitor v;
    md->AcceptVisitor(v);
    // method:Foo -> attrsec (Attributes[0]) -> attr -> simple:Foo -> id:Foo
    //      -> simple:int -> id:int (ReturnType)
    //      -> id:Foo (NameToken)
    ASSERT_EQ(v.trace.size(), 8u);
    EXPECT_EQ(v.trace[0], "method:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "id:Foo");  // the NameToken
}

// A `MethodDeclaration` with every slot filled recurses into the `Attributes`, `ReturnType`,
// `PrivateImplementationType`, `NameToken`, each `TypeParameter` (and its `NameToken`), each
// `Parameter` (and its `Type`/`NameToken`), each `Constraint` (and its `TypeParameter`), and the
// `Body`, in slot order.
TEST(CSharp_MethodDeclaration, DepthFirstWalkFull) {
    auto md = make_MethodDeclarationFull();
    RecordingVisitor v;
    md->AcceptVisitor(v);
    // method:Foo -> attrsec -> attr -> simple:Foo -> id:Foo (Attribute)
    //      -> simple:int -> id:int (ReturnType)
    //      -> simple:I -> id:I (PrivateImplementationType)
    //      -> id:Foo (NameToken)
    //      -> tpp:T -> id:T (TypeParameters[0])
    //      -> param (Parameters[0]) -> simple:int -> id:int -> id:x
    //      -> constraint (Constraints[0]) -> simple:T -> id:T
    //      -> block (Body, empty)
    ASSERT_EQ(v.trace.size(), 20u);
    EXPECT_EQ(v.trace[0], "method:Foo");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "simple:I");
    EXPECT_EQ(v.trace[8], "id:I");
    EXPECT_EQ(v.trace[9], "id:Foo");  // NameToken
    EXPECT_EQ(v.trace[10], "tpp:T");
    EXPECT_EQ(v.trace[11], "id:T");
    EXPECT_EQ(v.trace[12], "param");
    EXPECT_EQ(v.trace[13], "simple:int");
    EXPECT_EQ(v.trace[14], "id:int");
    EXPECT_EQ(v.trace[15], "id:x");
    EXPECT_EQ(v.trace[16], "constraint");
    EXPECT_EQ(v.trace[17], "simple:T");
    EXPECT_EQ(v.trace[18], "id:T");
    EXPECT_EQ(v.trace[19], "block");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_MethodDeclaration, DoMatchSameNode) {
    auto a = make_MethodDeclaration();
    auto b = make_MethodDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]`).
TEST(CSharp_MethodDeclaration, DoMatchNameMismatchRejects) {
    auto a = make_MethodDeclaration();
    auto b = make_MethodDeclaration();
    auto bName = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    b->NameToken(bName.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_MethodDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_MethodDeclaration();
    auto b = make_MethodDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_MethodDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_MethodDeclaration();
    auto b = make_MethodDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` term over the required `ReturnType` slot).
TEST(CSharp_MethodDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_MethodDeclaration();
    auto b = make_MethodDeclaration();
    auto aRt = std::make_unique<SimpleType>(std::string("int"));
    a->ReturnType(aRt.get());
    auto bRt = std::make_unique<SimpleType>(std::string("string"));
    b->ReturnType(bRt.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `PrivateImplementationType` asymmetry rejects: a pattern with a `PrivateImplementationType`
// vs a candidate without one.
TEST(CSharp_MethodDeclaration, DoMatchPrivateImplTypeAsymmetryRejects) {
    auto a = make_MethodDeclaration();
    auto pit = std::make_unique<SimpleType>(std::string("I"));
    a->PrivateImplementationType(pit.get());
    auto b = make_MethodDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `TypeParameters` mismatch rejects (the collection recursive `DoMatch` -- a pattern with a
// type parameter vs a candidate without one).
TEST(CSharp_MethodDeclaration, DoMatchTypeParametersMismatchRejects) {
    auto a = make_MethodDeclarationFull();  // one type parameter
    auto b = make_MethodDeclaration();  // no type parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Parameters` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_MethodDeclaration, DoMatchParametersMismatchRejects) {
    auto a = make_MethodDeclarationFull();  // one parameter
    auto b = make_MethodDeclaration();  // no parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Constraints` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_MethodDeclaration, DoMatchConstraintsMismatchRejects) {
    auto a = make_MethodDeclarationFull();  // one constraint
    auto b = make_MethodDeclaration();  // no constraints
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` asymmetry rejects: a pattern without a `Body` vs a candidate with one.
TEST(CSharp_MethodDeclaration, DoMatchBodyAsymmetryRejects) {
    auto a = make_MethodDeclaration();
    auto b = make_MethodDeclaration();
    auto body = std::make_unique<BlockStatement>();
    b->Body(body.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single type parameter match (the collection recursive `DoMatch` accepts
// equal-length collections whose elements match).
TEST(CSharp_MethodDeclaration, DoMatchSameTypeParametersMatch) {
    auto a = make_MethodDeclaration();
    auto aTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    a->TypeParameters().Add(aTp.get());
    auto b = make_MethodDeclaration();
    auto bTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    b->TypeParameters().Add(bTp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single constraint match.
TEST(CSharp_MethodDeclaration, DoMatchSameConstraintsMatch) {
    auto a = make_MethodDeclaration();
    auto aTpSimple = std::make_unique<SimpleType>(std::string("T"));
    auto aC = std::make_unique<Constraint>(aTpSimple.get());
    a->Constraints().Add(aC.get());
    auto b = make_MethodDeclaration();
    auto bTpSimple = std::make_unique<SimpleType>(std::string("T"));
    auto bC = std::make_unique<Constraint>(bTpSimple.get());
    b->Constraints().Add(bC.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`MethodDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not a `MethodDeclaration`).
TEST(CSharp_MethodDeclaration, DoMatchRejectsNonMethodEntityDeclaration) {
    auto a = make_MethodDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_MethodDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_MethodDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_MethodDeclaration, DoMatchRejectsNull) {
    auto a = make_MethodDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`PrivateImplementationType`/`NameToken`/`Body`, copies the
// `Modifiers` scalar, and does not detach the source.
TEST(CSharp_MethodDeclaration, CloneDeepCopies) {
    auto a = make_MethodDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<MethodDeclaration>(
        static_cast<MethodDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->Name(), "Foo");
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    ASSERT_NE(clone->PrivateImplementationType(), nullptr);
    EXPECT_NE(clone->PrivateImplementationType(), a->PrivateImplementationType());
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), a->Body());
    // The source is not detached.
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
    EXPECT_EQ(a->Body()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `MethodDeclaration*`.
TEST(CSharp_MethodDeclaration, CloneVirtualAndCovariant) {
    auto a = make_MethodDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<MethodDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<MethodDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_MethodDeclaration, CloneCopiesAttributes) {
    auto a = make_MethodDeclarationWithAttribute();
    auto clone = std::unique_ptr<MethodDeclaration>(
        static_cast<MethodDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->NameToken()->Parent(), clone.get());
}

// `Clone` copies the `TypeParameters` collection (each element deep-cloned).
TEST(CSharp_MethodDeclaration, CloneCopiesTypeParameters) {
    auto a = make_MethodDeclarationFull();
    auto clone = std::unique_ptr<MethodDeclaration>(
        static_cast<MethodDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->TypeParameters().Count(), 1u);
    auto* clonedTp = clone->TypeParameters().At(0);
    EXPECT_NE(clonedTp, a->TypeParameters().At(0));
    ASSERT_NE(clonedTp->NameToken(), nullptr);
    EXPECT_NE(clonedTp->NameToken(), a->TypeParameters().At(0)->NameToken());
    EXPECT_EQ(clonedTp->NameToken()->Name(), "T");
}

// `Clone` copies the `Parameters` collection (each element deep-cloned, including its `Type`/
// `NameToken`).
TEST(CSharp_MethodDeclaration, CloneCopiesParameters) {
    auto a = make_MethodDeclarationFull();
    auto clone = std::unique_ptr<MethodDeclaration>(
        static_cast<MethodDeclaration*>(a->Clone()));
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
TEST(CSharp_MethodDeclaration, CloneCopiesConstraints) {
    auto a = make_MethodDeclarationFull();
    auto clone = std::unique_ptr<MethodDeclaration>(
        static_cast<MethodDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Constraints().Count(), 1u);
    auto* clonedConstraint = clone->Constraints().At(0);
    EXPECT_NE(clonedConstraint, a->Constraints().At(0));
    ASSERT_NE(clonedConstraint->TypeParameter(), nullptr);
    EXPECT_NE(clonedConstraint->TypeParameter(), a->Constraints().At(0)->TypeParameter());
}

// `Clone` skips absent nullable slots (a method with no `PrivateImplementationType`/`Body` clones
// without them).
TEST(CSharp_MethodDeclaration, CloneSkipsAbsentNullableSlots) {
    auto a = make_MethodDeclaration();  // no PrivateImplementationType/Body
    auto clone = std::unique_ptr<MethodDeclaration>(
        static_cast<MethodDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->PrivateImplementationType(), nullptr);
    EXPECT_EQ(clone->Body(), nullptr);
    EXPECT_EQ(clone->TypeParameters().Count(), 0u);
    EXPECT_EQ(clone->Parameters().Count(), 0u);
    EXPECT_EQ(clone->Constraints().Count(), 0u);
    ASSERT_NE(clone->NameToken(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `ReturnType` + `NameToken` required slots filled).
TEST(CSharp_MethodDeclaration, CheckInvariantPassesOnFilled) {
    auto md = make_MethodDeclaration();  // ReturnType + NameToken filled
    md->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_MethodDeclaration, CheckInvariantPassesOnFull) {
    auto md = make_MethodDeclarationFull();
    md->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `ReturnType` and `NameToken` are REQUIRED
// slots, so a default-constructed node violates the required-slot invariant -- the assert fires
// in debug).
#ifndef NDEBUG
TEST(CSharp_MethodDeclaration, CheckInvariantRejectsEmpty) {
    MethodDeclaration md;  // no ReturnType, no NameToken
    EXPECT_DEATH(md.CheckInvariant(), "");
}
#endif
