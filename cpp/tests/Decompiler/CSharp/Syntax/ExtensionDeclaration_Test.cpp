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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `ExtensionDeclaration` concrete C# AST node (the C# 14 extension block, the first
// `EntityDeclaration` with NO single slots -- five collections `Attributes`/`TypeParameters`/
// `ReceiverParameters`/`Constraints`/`Members`). Mirrors the `MethodDeclaration_Test.cpp` structure
// (the D234 shared-`RecordingVisitor`/`DoMatchAgainst` pattern, with `ExtensionHolder` structs
// keeping every node alive in the test scope).

#include "Decompiler/CSharp/Syntax/ExtensionDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitExtensionDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`TypeParameterDeclaration`/
// `ParameterDeclaration`/`Constraint`/`MethodDeclaration` of the slots, and the
// `DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch rejections), recording a
// tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitExtensionDeclaration(ExtensionDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-ext>"); return; }
        trace.push_back(std::string("ext:") + node->ExtensionKeyword);
        VisitChildren(node);
    }
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
// pattern). Uses `Match::CreateNew()` (a default-constructed `Match` holds a null capture vector --
// the D219 distinction; the collection `DoMatch` derefs it).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping an `ExtensionDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design).
struct ExtensionHolder {
    std::unique_ptr<ExtensionDeclaration> ext;
    // An Attributes AttributeSection holding an Attribute whose Type is a SimpleType.
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    // One TypeParameter "T" (kept alive with its own NameToken).
    std::unique_ptr<TypeParameterDeclaration> tp0;
    std::unique_ptr<Identifier> tp0NameToken;
    // One ReceiverParameter "int x" (kept alive with its own Type + NameToken).
    std::unique_ptr<ParameterDeclaration> param0;
    std::unique_ptr<SimpleType> param0Type;
    std::unique_ptr<Identifier> param0NameToken;
    // One Constraint "where T :" with no base types (kept alive with its own TypeParameter SimpleType).
    std::unique_ptr<Constraint> constraint0;
    std::unique_ptr<SimpleType> constraint0TypeParam;
    // One Member: a MethodDeclaration "int Foo()" (kept alive with its own ReturnType + NameToken).
    std::unique_ptr<MethodDeclaration> member0;
    std::unique_ptr<SimpleType> member0ReturnType;
    std::unique_ptr<Identifier> member0NameToken;
    ExtensionDeclaration* get() const { return ext.get(); }
    ExtensionDeclaration* operator->() const { return ext.get(); }
};

// Build an `ExtensionDeclaration` with no collections filled (the empty node). `GetChildCount` is
// 0 (there are NO single slots, so an empty node reports 0 -- the collection-only precedent).
ExtensionHolder make_ExtensionDeclaration() {
    ExtensionHolder h;
    h.ext = std::make_unique<ExtensionDeclaration>();
    return h;
}

// Build an `ExtensionDeclaration` with every collection holding one element: an `Attributes`
// `AttributeSection` with an `Attribute` `Foo`, a `TypeParameters` collection holding one
// `TypeParameterDeclaration` `T`, a `ReceiverParameters` collection holding one
// `ParameterDeclaration` `int x`, a `Constraints` collection holding one `Constraint` `where T :`
// (no base types), and a `Members` collection holding one `MethodDeclaration` `int Foo()` (a
// concrete `EntityDeclaration` -- every collection filled).
ExtensionHolder make_ExtensionDeclarationFull() {
    ExtensionHolder h;
    h.ext = std::make_unique<ExtensionDeclaration>();
    // Attribute
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.ext->Attributes().Add(h.attrSec.get());
    // One type parameter T
    h.tp0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("T")));
    h.tp0 = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    h.tp0->NameToken(h.tp0NameToken.get());
    h.ext->TypeParameters().Add(h.tp0.get());
    // One receiver parameter int x
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.param0Type = std::make_unique<SimpleType>(std::string("int"));
    h.param0->Type(h.param0Type.get());
    h.param0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param0->NameToken(h.param0NameToken.get());
    h.ext->ReceiverParameters().Add(h.param0.get());
    // One constraint where T : (no base types)
    h.constraint0TypeParam = std::make_unique<SimpleType>(std::string("T"));
    h.constraint0 = std::make_unique<Constraint>(h.constraint0TypeParam.get());
    h.ext->Constraints().Add(h.constraint0.get());
    // One member: a MethodDeclaration int Foo()
    h.member0 = std::make_unique<MethodDeclaration>();
    h.member0ReturnType = std::make_unique<SimpleType>(std::string("int"));
    h.member0->ReturnType(h.member0ReturnType.get());
    h.member0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.member0->NameToken(h.member0NameToken.get());
    h.ext->Members().Add(h.member0.get());
    return h;
}

} // namespace

// ==========================================================================
// ExtensionDeclaration (the C# 14 extension block)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `ExtensionDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_ExtensionDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<ExtensionDeclaration>);
}

// `ExtensionDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_ExtensionDeclaration, IsEntityDeclarationAndAstNode) {
    ExtensionDeclaration ext;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&ext), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ext), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&ext), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ext), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ext), nullptr);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: all five collections empty, NO single slots. `GetChildCount` is 0 (there are NO
// single slots, so an empty node reports 0 -- the collection-only `ArrayInitializerExpression` D250
// / `BlockStatement` D256 precedent). `Modifiers` defaults to `None`; `SymbolKind` is
// `TypeDefinition`; the inherited `Name`/`NameToken`/`ReturnType` are all null/empty (the node
// declares no `Identifier`/`Type` slot, so the base kind-walks return null).
TEST(CSharp_ExtensionDeclaration, EmptyCtor) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(ext.Modifiers(), Modifiers::None);
    EXPECT_EQ(ext.Name(), "");
    EXPECT_EQ(ext.NameToken(), nullptr);
    EXPECT_EQ(ext.ReturnType(), nullptr);
    EXPECT_EQ(ext.Attributes().Count(), 0);
    EXPECT_EQ(ext.TypeParameters().Count(), 0);
    EXPECT_EQ(ext.ReceiverParameters().Count(), 0);
    EXPECT_EQ(ext.Constraints().Count(), 0);
    EXPECT_EQ(ext.Members().Count(), 0);
    EXPECT_EQ(ext.GetChildCount(), 0);  // 0 attrs + 0 tps + 0 params + 0 constraints + 0 members
}

// ---- The ExtensionKeyword const string ----------------------------------

TEST(CSharp_ExtensionDeclaration, ExtensionKeywordConst) {
    EXPECT_STREQ(ExtensionDeclaration::ExtensionKeyword, "extension");
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_ExtensionDeclaration, SymbolKindOverrideReturnsTypeDefinition) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.SymbolKind(), SymbolKind::TypeDefinition);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_ExtensionDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.Modifiers(), Modifiers::None);
    ext.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(ext.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_ExtensionDeclaration, HasModifierIsBitmaskTest) {
    ExtensionDeclaration ext;
    ext.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(ext.HasModifier(Modifiers::Static));
    EXPECT_TRUE(ext.HasModifier(Modifiers::Public));
    EXPECT_FALSE(ext.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken/ReturnType (inherited base, no [Slot] declared) --

// `Name` returns the empty string (the node declares no `Identifier` slot; the inherited base
// `Name()` kind-walks for the `Identifier` kind and returns null -> empty string).
TEST(CSharp_ExtensionDeclaration, NameReturnsEmpty) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.Name(), "");
}

// `NameToken` returns null (no `Identifier` slot).
TEST(CSharp_ExtensionDeclaration, NameTokenReturnsNull) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.NameToken(), nullptr);
}

// `ReturnType` returns null (no `Type` slot).
TEST(CSharp_ExtensionDeclaration, ReturnTypeReturnsNull) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.ReturnType(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_ExtensionDeclaration, AttributesCollectionAddReparents) {
    ExtensionDeclaration ext;
    auto attrSec = std::make_unique<AttributeSection>();
    ext.Attributes().Add(attrSec.get());
    EXPECT_EQ(ext.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &ext);
}

// ---- The TypeParameters collection (non-incremental) ---------------------

TEST(CSharp_ExtensionDeclaration, TypeParametersCollectionAddReparents) {
    ExtensionDeclaration ext;
    auto tp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    ext.TypeParameters().Add(tp.get());
    EXPECT_EQ(ext.TypeParameters().Count(), 1);
    EXPECT_EQ(tp->Parent(), &ext);
}

// ---- The ReceiverParameters collection (non-incremental) -----------------

TEST(CSharp_ExtensionDeclaration, ReceiverParametersCollectionAddReparents) {
    ExtensionDeclaration ext;
    auto p = std::make_unique<ParameterDeclaration>();
    ext.ReceiverParameters().Add(p.get());
    EXPECT_EQ(ext.ReceiverParameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &ext);
}

// ---- The Constraints collection (non-incremental) ------------------------

TEST(CSharp_ExtensionDeclaration, ConstraintsCollectionAddReparents) {
    ExtensionDeclaration ext;
    auto tp = std::make_unique<SimpleType>(std::string("T"));
    auto c = std::make_unique<Constraint>(tp.get());
    ext.Constraints().Add(c.get());
    EXPECT_EQ(ext.Constraints().Count(), 1);
    EXPECT_EQ(c->Parent(), &ext);
}

// ---- The Members collection (non-incremental) ----------------------------

TEST(CSharp_ExtensionDeclaration, MembersCollectionAddReparents) {
    ExtensionDeclaration ext;
    auto m = std::make_unique<MethodDeclaration>();
    ext.Members().Add(m.get());
    EXPECT_EQ(ext.Members().Count(), 1);
    EXPECT_EQ(m->Parent(), &ext);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the five collections in declaration order (no single slots): the `Attributes`
// collection `[0, attrCount)`, the `TypeParameters` collection, the `ReceiverParameters`
// collection, the `Constraints` collection, and the `Members` collection at the end.
TEST(CSharp_ExtensionDeclaration, GetChildWalksSlots) {
    auto ext = make_ExtensionDeclarationFull();
    // 1 attr + 1 tpp + 1 param + 1 constraint + 1 member = 5 (no single slots)
    EXPECT_EQ(ext->GetChildCount(), 5);
    EXPECT_EQ(ext->GetChild(0), ext->Attributes().At(0));            // the AttributeSection
    EXPECT_EQ(ext->GetChild(1), ext->TypeParameters().At(0));        // TypeParameters[0]
    EXPECT_EQ(ext->GetChild(2), ext->ReceiverParameters().At(0));   // ReceiverParameters[0]
    EXPECT_EQ(ext->GetChild(3), ext->Constraints().At(0));           // Constraints[0]
    EXPECT_EQ(ext->GetChild(4), ext->Members().At(0));               // Members[0]
    EXPECT_THROW(ext->GetChild(5), std::out_of_range);
}

TEST(CSharp_ExtensionDeclaration, GetChildSlotInfoWalksSlots) {
    auto ext = make_ExtensionDeclarationFull();
    EXPECT_EQ(ext->GetChildSlotInfo(0), &ext->AttributesSlot);
    EXPECT_EQ(ext->GetChildSlotInfo(1), &ext->TypeParametersSlot);
    EXPECT_EQ(ext->GetChildSlotInfo(2), &ext->ReceiverParametersSlot);
    EXPECT_EQ(ext->GetChildSlotInfo(3), &ext->ConstraintsSlot);
    EXPECT_EQ(ext->GetChildSlotInfo(4), &ext->MembersSlot);
    EXPECT_THROW(ext->GetChildSlotInfo(5), std::out_of_range);
}

TEST(CSharp_ExtensionDeclaration, GetCollectionByKindReturnsAllFiveCollections) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::AttributeSection), &ext.Attributes());
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::TypeParameter), &ext.TypeParameters());
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::Parameter), &ext.ReceiverParameters());
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::Constraint), &ext.Constraints());
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::TypeMember), &ext.Members());
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(ext.GetCollectionByKind(&Slots::Body), nullptr);
}

// `SetChild` replaces a `Members` element in place (a collection element at the flattened index).
TEST(CSharp_ExtensionDeclaration, SetChildReplacesMember) {
    auto ext = make_ExtensionDeclarationFull();
    auto m2 = std::make_unique<MethodDeclaration>();
    auto* oldMember = ext->Members().At(0);
    ext->SetChild(4, m2.get());  // Members[0] at flattened index 4
    EXPECT_EQ(ext->Members().At(0), m2.get());
    EXPECT_EQ(oldMember->Parent(), nullptr);
}

// `SetChild` replaces a `ReceiverParameters` element in place.
TEST(CSharp_ExtensionDeclaration, SetChildReplacesReceiverParameter) {
    auto ext = make_ExtensionDeclarationFull();
    auto p2 = std::make_unique<ParameterDeclaration>();
    auto* oldParam = ext->ReceiverParameters().At(0);
    ext->SetChild(2, p2.get());  // ReceiverParameters[0] at flattened index 2
    EXPECT_EQ(ext->ReceiverParameters().At(0), p2.get());
    EXPECT_EQ(oldParam->Parent(), nullptr);
}

// `GetChild` throws on an empty node (no children -- the collection-only precedent).
TEST(CSharp_ExtensionDeclaration, GetChildThrowsOnEmpty) {
    auto ext = make_ExtensionDeclaration();
    EXPECT_THROW(ext->GetChild(0), std::out_of_range);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_ExtensionDeclaration, SlotStaticsPointAtSharedKinds) {
    ExtensionDeclaration ext;
    EXPECT_EQ(ext.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(ext.TypeParametersSlot.Kind(), &Slots::TypeParameter);
    EXPECT_EQ(ext.ReceiverParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(ext.ConstraintsSlot.Kind(), &Slots::Constraint);
    EXPECT_EQ(ext.MembersSlot.Kind(), &Slots::TypeMember);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_ExtensionDeclaration, SlotStaticsAreDistinct) {
    ExtensionDeclaration ext;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ext.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ext.TypeParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ext.TypeParametersSlot),
              static_cast<const CSharpSlotInfo*>(&ext.ReceiverParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ext.ReceiverParametersSlot),
              static_cast<const CSharpSlotInfo*>(&ext.ConstraintsSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ext.ConstraintsSlot),
              static_cast<const CSharpSlotInfo*>(&ext.MembersSlot));
}

// The NEW cycle-broken `Slots::TypeMember` kind (in `EntityDeclaration.hpp`) is distinct from the
// other collection kinds (cast both to the common `CSharpSlotInfo*` base for the cross-element-type
// `EXPECT_NE`, the D251/D252/D262 precedent).
TEST(CSharp_ExtensionDeclaration, NewSlotKindIsDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeMember),
              static_cast<const CSharpSlotInfo*>(&Slots::AttributeSection));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeMember),
              static_cast<const CSharpSlotInfo*>(&Slots::TypeParameter));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeMember),
              static_cast<const CSharpSlotInfo*>(&Slots::Parameter));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeMember),
              static_cast<const CSharpSlotInfo*>(&Slots::Constraint));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Slots::TypeMember),
              static_cast<const CSharpSlotInfo*>(&Slots::Type));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_ExtensionDeclaration, AcceptVisitorDispatches) {
    ExtensionDeclaration ext;  // empty -- records just the node tag
    RecordingVisitor v;
    ext.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ext:extension"}));
}

TEST(CSharp_ExtensionDeclaration, AcceptVisitorVirtualThroughBase) {
    ExtensionDeclaration ext;
    AstNode* node = &ext;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ext:extension"}));
}

TEST(CSharp_ExtensionDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    ExtensionDeclaration ext;
    EntityDeclaration* node = &ext;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ext:extension"}));
}

// The depth-first walk over an empty node records just the node tag (no children).
TEST(CSharp_ExtensionDeclaration, DepthFirstWalkEmpty) {
    auto ext = make_ExtensionDeclaration();
    RecordingVisitor v;
    ext->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"ext:extension"}));
}

// A `ExtensionDeclaration` with every collection holding one element recurses into the
// `Attributes` (AttributeSection -> Attribute -> SimpleType -> Identifier), the `TypeParameters`
// (TypeParameterDeclaration -> its NameToken), the `ReceiverParameters` (ParameterDeclaration ->
// its Type SimpleType/Identifier + its NameToken), the `Constraints` (Constraint -> its
// TypeParameter SimpleType/Identifier), and the `Members` (a MethodDeclaration -> its ReturnType
// SimpleType/Identifier + its NameToken), in slot order.
TEST(CSharp_ExtensionDeclaration, DepthFirstWalkFull) {
    auto ext = make_ExtensionDeclarationFull();
    RecordingVisitor v;
    ext->AcceptVisitor(v);
    // ext:extension -> attrsec: -> attr -> simple:Foo -> id:Foo (Attributes)
    //      -> tpp:T -> id:T (TypeParameters[0])
    //      -> param (ReceiverParameters[0]) -> simple:int -> id:int -> id:x
    //      -> constraint (Constraints[0]) -> simple:T -> id:T
    //      -> method:Foo (Members[0]) -> simple:int -> id:int (ReturnType) -> id:Foo (NameToken)
    ASSERT_EQ(v.trace.size(), 18u);
    EXPECT_EQ(v.trace[0], "ext:extension");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "tpp:T");
    EXPECT_EQ(v.trace[6], "id:T");
    EXPECT_EQ(v.trace[7], "param");
    EXPECT_EQ(v.trace[8], "simple:int");
    EXPECT_EQ(v.trace[9], "id:int");
    EXPECT_EQ(v.trace[10], "id:x");
    EXPECT_EQ(v.trace[11], "constraint");
    EXPECT_EQ(v.trace[12], "simple:T");
    EXPECT_EQ(v.trace[13], "id:T");
    EXPECT_EQ(v.trace[14], "method:Foo");
    EXPECT_EQ(v.trace[15], "simple:int");
    EXPECT_EQ(v.trace[16], "id:int");
    EXPECT_EQ(v.trace[17], "id:Foo");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_ExtensionDeclaration, DoMatchSameNode) {
    auto a = make_ExtensionDeclaration();
    auto b = make_ExtensionDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_ExtensionDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_ExtensionDeclaration();
    auto b = make_ExtensionDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_ExtensionDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_ExtensionDeclaration();
    auto b = make_ExtensionDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// An `Attributes` mismatch rejects (the `MatchAttributesAndModifiers` collection `DoMatch` -- a
// pattern with an attribute vs a candidate without one).
TEST(CSharp_ExtensionDeclaration, DoMatchAttributesMismatchRejects) {
    auto a = make_ExtensionDeclarationFull();  // one attribute
    auto b = make_ExtensionDeclaration();  // no attributes
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `TypeParameters` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_ExtensionDeclaration, DoMatchTypeParametersMismatchRejects) {
    auto a = make_ExtensionDeclarationFull();  // one type parameter
    auto b = make_ExtensionDeclaration();  // no type parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReceiverParameters` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_ExtensionDeclaration, DoMatchReceiverParametersMismatchRejects) {
    auto a = make_ExtensionDeclarationFull();  // one receiver parameter
    auto b = make_ExtensionDeclaration();  // no receiver parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Constraints` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_ExtensionDeclaration, DoMatchConstraintsMismatchRejects) {
    auto a = make_ExtensionDeclarationFull();  // one constraint
    auto b = make_ExtensionDeclaration();  // no constraints
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Members` mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_ExtensionDeclaration, DoMatchMembersMismatchRejects) {
    auto a = make_ExtensionDeclarationFull();  // one member
    auto b = make_ExtensionDeclaration();  // no members
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single type parameter match.
TEST(CSharp_ExtensionDeclaration, DoMatchSameTypeParametersMatch) {
    auto a = make_ExtensionDeclaration();
    auto aTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    a->TypeParameters().Add(aTp.get());
    auto b = make_ExtensionDeclaration();
    auto bTp = std::make_unique<TypeParameterDeclaration>(std::string("T"));
    b->TypeParameters().Add(bTp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single constraint match.
TEST(CSharp_ExtensionDeclaration, DoMatchSameConstraintsMatch) {
    auto a = make_ExtensionDeclaration();
    auto aTpSimple = std::make_unique<SimpleType>(std::string("T"));
    auto aC = std::make_unique<Constraint>(aTpSimple.get());
    a->Constraints().Add(aC.get());
    auto b = make_ExtensionDeclaration();
    auto bTpSimple = std::make_unique<SimpleType>(std::string("T"));
    auto bC = std::make_unique<Constraint>(bTpSimple.get());
    b->Constraints().Add(bC.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single receiver parameter match.
TEST(CSharp_ExtensionDeclaration, DoMatchSameReceiverParametersMatch) {
    auto a = make_ExtensionDeclaration();
    auto aP = std::make_unique<ParameterDeclaration>();
    a->ReceiverParameters().Add(aP.get());
    auto b = make_ExtensionDeclaration();
    auto bP = std::make_unique<ParameterDeclaration>();
    b->ReceiverParameters().Add(bP.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single Member (a `MethodDeclaration` `int Foo()`) match (the collection
// recursive `DoMatch` accepts equal-length collections whose elements match).
TEST(CSharp_ExtensionDeclaration, DoMatchSameMembersMatch) {
    auto a = make_ExtensionDeclaration();
    auto aM = std::make_unique<MethodDeclaration>();
    auto aRt = std::make_unique<SimpleType>(std::string("int"));
    aM->ReturnType(aRt.get());
    auto aName = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    aM->NameToken(aName.get());
    a->Members().Add(aM.get());
    auto b = make_ExtensionDeclaration();
    auto bM = std::make_unique<MethodDeclaration>();
    auto bRt = std::make_unique<SimpleType>(std::string("int"));
    bM->ReturnType(bRt.get());
    auto bName = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    bM->NameToken(bName.get());
    b->Members().Add(bM.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`ExtensionDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not an `ExtensionDeclaration`).
TEST(CSharp_ExtensionDeclaration, DoMatchRejectsNonExtensionEntityDeclaration) {
    auto a = make_ExtensionDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_ExtensionDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_ExtensionDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_ExtensionDeclaration, DoMatchRejectsNull) {
    auto a = make_ExtensionDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies all five collections, copies the `Modifiers` scalar, and does not detach the
// source.
TEST(CSharp_ExtensionDeclaration, CloneDeepCopies) {
    auto a = make_ExtensionDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->Name(), "");
    EXPECT_EQ(clone->NameToken(), nullptr);
    EXPECT_EQ(clone->ReturnType(), nullptr);
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_EQ(clone->TypeParameters().Count(), 1u);
    EXPECT_EQ(clone->ReceiverParameters().Count(), 1u);
    EXPECT_EQ(clone->Constraints().Count(), 1u);
    EXPECT_EQ(clone->Members().Count(), 1u);
    // The source is not detached.
    EXPECT_EQ(a->Members().At(0)->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `ExtensionDeclaration*`.
TEST(CSharp_ExtensionDeclaration, CloneVirtualAndCovariant) {
    auto a = make_ExtensionDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<ExtensionDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<ExtensionDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_ExtensionDeclaration, CloneCopiesAttributes) {
    auto a = make_ExtensionDeclarationFull();
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->Attributes().At(0)->Parent(), clone.get());
}

// `Clone` copies the `TypeParameters` collection (each element deep-cloned, including its
// `NameToken`).
TEST(CSharp_ExtensionDeclaration, CloneCopiesTypeParameters) {
    auto a = make_ExtensionDeclarationFull();
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->TypeParameters().Count(), 1u);
    auto* clonedTp = clone->TypeParameters().At(0);
    EXPECT_NE(clonedTp, a->TypeParameters().At(0));
    ASSERT_NE(clonedTp->NameToken(), nullptr);
    EXPECT_NE(clonedTp->NameToken(), a->TypeParameters().At(0)->NameToken());
    EXPECT_EQ(clonedTp->NameToken()->Name(), "T");
}

// `Clone` copies the `ReceiverParameters` collection (each element deep-cloned, including its
// `Type`/`NameToken`).
TEST(CSharp_ExtensionDeclaration, CloneCopiesReceiverParameters) {
    auto a = make_ExtensionDeclarationFull();
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->ReceiverParameters().Count(), 1u);
    auto* clonedParam = clone->ReceiverParameters().At(0);
    EXPECT_NE(clonedParam, a->ReceiverParameters().At(0));
    ASSERT_NE(clonedParam->Type(), nullptr);
    EXPECT_NE(clonedParam->Type(), a->ReceiverParameters().At(0)->Type());
    ASSERT_NE(clonedParam->NameToken(), nullptr);
    EXPECT_NE(clonedParam->NameToken(), a->ReceiverParameters().At(0)->NameToken());
    EXPECT_EQ(clonedParam->NameToken()->Name(), "x");
}

// `Clone` copies the `Constraints` collection (each element deep-cloned, including its
// `TypeParameter` `SimpleType`).
TEST(CSharp_ExtensionDeclaration, CloneCopiesConstraints) {
    auto a = make_ExtensionDeclarationFull();
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Constraints().Count(), 1u);
    auto* clonedConstraint = clone->Constraints().At(0);
    EXPECT_NE(clonedConstraint, a->Constraints().At(0));
    ASSERT_NE(clonedConstraint->TypeParameter(), nullptr);
    EXPECT_NE(clonedConstraint->TypeParameter(), a->Constraints().At(0)->TypeParameter());
}

// `Clone` copies the `Members` collection (each element deep-cloned via the abstract
// `EntityDeclaration::Clone` -- the `static_cast` downcast of the `AstNode*` returned by the
// covariant concrete `Clone`).
TEST(CSharp_ExtensionDeclaration, CloneCopiesMembers) {
    auto a = make_ExtensionDeclarationFull();
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    ASSERT_EQ(clone->Members().Count(), 1u);
    auto* clonedMember = clone->Members().At(0);
    EXPECT_NE(clonedMember, a->Members().At(0));
    // The cloned Member is a MethodDeclaration int Foo() (its ReturnType + NameToken deep-cloned).
    auto* clonedMethod = dynamic_cast<MethodDeclaration*>(clonedMember);
    ASSERT_NE(clonedMethod, nullptr);
    ASSERT_NE(clonedMethod->ReturnType(), nullptr);
    EXPECT_NE(clonedMethod->ReturnType(), a->Members().At(0)->ReturnType());
    ASSERT_NE(clonedMethod->NameToken(), nullptr);
    EXPECT_NE(clonedMethod->NameToken(), a->Members().At(0)->NameToken());
    EXPECT_EQ(clonedMethod->NameToken()->Name(), "Foo");
}

// `Clone` of an empty node yields an empty node (all five collections empty).
TEST(CSharp_ExtensionDeclaration, CloneEmpty) {
    auto a = make_ExtensionDeclaration();
    auto clone = std::unique_ptr<ExtensionDeclaration>(
        static_cast<ExtensionDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 0u);
    EXPECT_EQ(clone->TypeParameters().Count(), 0u);
    EXPECT_EQ(clone->ReceiverParameters().Count(), 0u);
    EXPECT_EQ(clone->Constraints().Count(), 0u);
    EXPECT_EQ(clone->Members().Count(), 0u);
    EXPECT_EQ(clone->GetChildCount(), 0);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on an EMPTY node (there are NO required single slots -- the collection-
// only precedent, unlike `MethodDeclaration` whose `ReturnType`/`NameToken` are required).
TEST(CSharp_ExtensionDeclaration, CheckInvariantPassesOnEmpty) {
    auto ext = make_ExtensionDeclaration();
    ext->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every collection filled).
TEST(CSharp_ExtensionDeclaration, CheckInvariantPassesOnFull) {
    auto ext = make_ExtensionDeclarationFull();
    ext->CheckInvariant();  // should not assert
    SUCCEED();
}
