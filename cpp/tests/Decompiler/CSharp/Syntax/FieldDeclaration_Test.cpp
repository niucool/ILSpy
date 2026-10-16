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

// Tests for the `FieldDeclaration` concrete node (cpp/.../Syntax/FieldDeclaration.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/FieldDeclaration.cs) -- the second concrete
// `TypeMember` and the first `EntityDeclaration` with TWO collection slots (`Attributes` +
// `Variables`) with the required `ReturnType` single slot between them (the `ComposedType` D242
// collection -> single -> collection shape applied to the `TypeMember` hierarchy). The next
// in-order Phase-5 piece per the D272 plan. The suite shares a `RecordingVisitor` and a
// `DoMatchAgainst` helper (the D234 pattern).

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
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitFieldDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier` of its `Attributes` collection and
// `ReturnType`, the `VariableInitializer`/`Identifier`/`PrimitiveExpression` of its `Variables`
// collection, and the `DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch
// rejections), recording a tag and recursing via `VisitChildren` (the inherited depth-first
// default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitFieldDeclaration(FieldDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-field>"); return; }
        trace.push_back("field");
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

// A holder keeping a `FieldDeclaration` and all its children alive in the test scope (the port's
// non-owning raw-pointer child slots -- the D223 design: the parent does not take ownership; the
// holder's `unique_ptr`s own the nodes). A `FieldDeclaration` with a `ReturnType` `SimpleType`
// and a `Variables` collection of one `VariableInitializer` `x` (and, for the attribute variant,
// an `Attributes` `AttributeSection` whose `Attributes` hold an `Attribute` whose `Type` is a
// `SimpleType` `Foo`).
struct FieldHolder {
    std::unique_ptr<FieldDeclaration> fd;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<VariableInitializer> varX;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    FieldDeclaration* get() const { return fd.get(); }
    FieldDeclaration* operator->() const { return fd.get(); }
};

// Build a `FieldDeclaration` with a `ReturnType` `SimpleType` `int` and a single `Variable` `x`
// (no initializer, no attributes). The holder keeps every node alive.
FieldHolder make_FieldDeclaration() {
    FieldHolder h;
    h.fd = std::make_unique<FieldDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.fd->ReturnType(h.returnType.get());
    h.varX = std::make_unique<VariableInitializer>(std::string("x"));
    h.fd->Variables().Add(h.varX.get());
    return h;
}

// Build a `FieldDeclaration` with a `ReturnType` `SimpleType` `int`, a single `Variable` `x`, and
// an `Attributes` `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
FieldHolder make_FieldDeclarationWithAttribute() {
    FieldHolder h;
    h.fd = std::make_unique<FieldDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.fd->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.fd->ReturnType(h.returnType.get());
    h.varX = std::make_unique<VariableInitializer>(std::string("x"));
    h.fd->Variables().Add(h.varX.get());
    return h;
}

} // namespace

// ==========================================================================
// FieldDeclaration (the second concrete TypeMember)
// ==========================================================================

// ---- is-a + final ---------------------------------------------------------

// `FieldDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_FieldDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<FieldDeclaration>);
}

// `FieldDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives from
// `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_FieldDeclaration, IsEntityDeclarationAndAstNode) {
    FieldDeclaration fd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&fd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&fd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&fd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&fd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&fd), nullptr);
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `ReturnType`, empty `Attributes`/`Variables`. `GetChildCount` is
// `0 + 1 + 0 = 1` (the empty `Attributes` collection plus the one `ReturnType` single slot plus
// the empty `Variables` collection -- the `ReturnType` single slot contributes 1 to the flattened
// count even when absent). The `Modifiers` defaults to `None`; `SymbolKind` is `Field`.
TEST(CSharp_FieldDeclaration, EmptyCtor) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(fd.Modifiers(), Modifiers::None);
    EXPECT_EQ(fd.ReturnType(), nullptr);
    EXPECT_EQ(fd.Attributes().Count(), 0);
    EXPECT_EQ(fd.Variables().Count(), 0);
    EXPECT_EQ(fd.GetChildCount(), 1);  // 0 attrs + 1 ReturnType + 0 variables
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_FieldDeclaration, SymbolKindOverrideReturnsField) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.SymbolKind(), SymbolKind::Field);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_FieldDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.Modifiers(), Modifiers::None);
    fd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(fd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_FieldDeclaration, HasModifierIsBitmaskTest) {
    FieldDeclaration fd;
    fd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(fd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(fd.HasModifier(Modifiers::Public));
    EXPECT_FALSE(fd.HasModifier(Modifiers::Virtual));
}

// ---- The Name/NameToken overrides (hidden; the names live in VariableInitializer) ----

// `Name` returns the empty string; `NameToken` returns null. The setters throw
// `std::logic_error` (the C# `NotSupportedException`).
TEST(CSharp_FieldDeclaration, NameAndNameTokenAreHidden) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.Name(), "");
    EXPECT_EQ(fd.NameToken(), nullptr);
    EXPECT_THROW(fd.Name(std::string("Foo")), std::logic_error);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    EXPECT_THROW(fd.NameToken(tok.get()), std::logic_error);
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

// The `ReturnType` setter re-parents the type. The slot follows the `Attributes` collection, so
// the index-less `SetChildNode` invalidates the parent's indices; the reindex is triggered by
// `Slot()` (the `EnsureChildIndices` call). After the reindex the `ReturnType`'s flattened
// `ChildIndex` is `attrCount` (0 with no attributes -- the `ReturnType` is the first non-empty
// slot).
TEST(CSharp_FieldDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    FieldDeclaration fd;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    fd.ReturnType(type.get());
    EXPECT_EQ(fd.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &fd);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);  // attrCount (0) -- ReturnType at flattened index 0
}

// The `ReturnType` setter detaches the old type and clears with null.
TEST(CSharp_FieldDeclaration, ReturnTypeSetterDetachesAndClears) {
    FieldDeclaration fd;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    fd.ReturnType(a.get());
    EXPECT_EQ(a->Parent(), &fd);
    fd.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(fd.ReturnType(), b.get());
    fd.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(fd.ReturnType(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

// `Attributes().Add(...)` appends and parents; the collection is NON-incremental (the node has
// two collections), so `Add` invalidates the parent's indices for a lazy reindex.
TEST(CSharp_FieldDeclaration, AttributesCollectionAddReparents) {
    FieldDeclaration fd;
    auto attrSec = std::make_unique<AttributeSection>();
    fd.Attributes().Add(attrSec.get());
    EXPECT_EQ(fd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &fd);
}

// ---- The Variables collection (non-incremental) --------------------------

// `Variables().Add(...)` appends and parents; the collection is NON-incremental (the node has
// two collections), so `Add` invalidates the parent's indices for a lazy reindex. After the
// reindex (triggered by `Slot()`) the `Variables[0]`'s flattened `ChildIndex` is
// `attrCount + 1` (1 with no attributes -- the `ReturnType` single slot at index 0, the
// `Variables[0]` at index 1).
TEST(CSharp_FieldDeclaration, VariablesCollectionAddReparentsAndReindexes) {
    FieldDeclaration fd;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    fd.ReturnType(type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    fd.Variables().Add(x.get());
    EXPECT_EQ(fd.Variables().Count(), 1);
    EXPECT_EQ(x->Parent(), &fd);
    (void)x->Slot();  // trigger the lazy reindex
    EXPECT_EQ(x->ChildIndex, 1);  // attrCount (0) + 1 ReturnType -- Variables[0] at index 1
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, then `ReturnType` at
// `attrCount`, then the `Variables` collection `[attrCount + 1, attrCount + 1 + varCount)`.
TEST(CSharp_FieldDeclaration, GetChildWalksSlots) {
    auto fd = make_FieldDeclaration();
    EXPECT_EQ(fd->GetChildCount(), 2);  // 0 attrs + ReturnType + 1 variable
    EXPECT_EQ(fd->GetChild(0), fd->ReturnType());
    EXPECT_EQ(fd->GetChild(1), fd->Variables().At(0));
    EXPECT_THROW(fd->GetChild(2), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot for each flattened index.
TEST(CSharp_FieldDeclaration, GetChildSlotInfoWalksSlots) {
    auto fd = make_FieldDeclaration();
    EXPECT_EQ(fd->GetChildSlotInfo(0), &fd->ReturnTypeSlot);      // ReturnType at attrCount (0)
    EXPECT_EQ(fd->GetChildSlotInfo(1), &fd->VariablesSlot);       // Variables[0] at attrCount + 1
    EXPECT_THROW(fd->GetChildSlotInfo(2), std::out_of_range);
}

// `GetCollectionByKind` returns the `Attributes`/`Variables` collections for their kinds, null
// for other kinds (the single-slot kind `Type` falls back).
TEST(CSharp_FieldDeclaration, GetCollectionByKindReturnsCollections) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.GetCollectionByKind(&Slots::AttributeSection), &fd.Attributes());
    EXPECT_EQ(fd.GetCollectionByKind(&Slots::Variable), &fd.Variables());
    EXPECT_EQ(fd.GetCollectionByKind(&Slots::Type), nullptr);
}

// `SetChild` replaces the `ReturnType` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_FieldDeclaration, SetChildReplacesReturnType) {
    auto fd = make_FieldDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    auto* oldType = fd->ReturnType();
    fd->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(fd->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

// `SetChild` replaces a `Variables` element in place (the element must already exist at the
// flattened index).
TEST(CSharp_FieldDeclaration, SetChildReplacesVariable) {
    auto fd = make_FieldDeclaration();
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    auto* oldVar = fd->Variables().At(0);
    fd->SetChild(1, y.get());  // Variables[0] at flattened index 1 (0 attrs + 1 ReturnType)
    EXPECT_EQ(fd->Variables().At(0), y.get());
    EXPECT_EQ(oldVar->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

// The slot statics point at the shared `Slots` kinds.
TEST(CSharp_FieldDeclaration, SlotStaticsPointAtSharedKinds) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(fd.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(fd.VariablesSlot.Kind(), &Slots::Variable);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_FieldDeclaration, SlotStaticsAreDistinct) {
    FieldDeclaration fd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&fd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&fd.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&fd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&fd.VariablesSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&fd.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&fd.VariablesSlot));
}

// The `Variables` collection reuses the SAME `Slots::Variable` kind as `FixedStatement` (the
// D267 cycle-broken kind), confirming the kind is shared across the `TypeMember`/`Statement`
// hierarchies by `[Slot]` name.
TEST(CSharp_FieldDeclaration, VariablesSlotSharesVariableKindWithFixedStatement) {
    FieldDeclaration fd;
    EXPECT_EQ(fd.VariablesSlot.Kind(), &Slots::Variable);
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

// `AcceptVisitor` dispatches to `VisitFieldDeclaration` (the concrete `Visit`).
TEST(CSharp_FieldDeclaration, AcceptVisitorDispatches) {
    FieldDeclaration fd;
    RecordingVisitor v;
    fd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"field"}));
}

// `AcceptVisitor` is virtual: a call through `AstNode*` dispatches to the concrete override.
TEST(CSharp_FieldDeclaration, AcceptVisitorVirtualThroughBase) {
    FieldDeclaration fd;
    AstNode* node = &fd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"field"}));
}

// `AcceptVisitor` is virtual: a call through `EntityDeclaration*` dispatches to the concrete
// override (the `EntityDeclaration` base declares the abstract `AcceptVisitor`; the concrete
// `FieldDeclaration` overrides it).
TEST(CSharp_FieldDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    FieldDeclaration fd;
    EntityDeclaration* node = &fd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"field"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int`) and the `Variables`
// collection (a `VariableInitializer` `x`) in document order (Attributes -> ReturnType ->
// Variables, with no attributes here).
TEST(CSharp_FieldDeclaration, DepthFirstWalk) {
    auto fd = make_FieldDeclaration();
    RecordingVisitor v;
    fd->AcceptVisitor(v);
    // field -> simple:int (ReturnType) -> id:int -> varinit:x (Variables[0]) -> id:x
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "field", "simple:int", "id:int", "varinit:x", "id:x",
    }));
}

// A `FieldDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` and `Variables` (the
// `Attributes` collection is the first slot).
TEST(CSharp_FieldDeclaration, DepthFirstWalkWithAttribute) {
    auto fd = make_FieldDeclarationWithAttribute();
    RecordingVisitor v;
    fd->AcceptVisitor(v);
    // field -> attrsec (Attributes[0]) -> attr -> simple:Foo (the Attribute's Type) -> id:Foo
    //      -> simple:int (ReturnType) -> id:int -> varinit:x (Variables[0]) -> id:x (its NameToken)
    ASSERT_EQ(v.trace.size(), 9u);
    EXPECT_EQ(v.trace[0], "field");
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

// A `FieldDeclaration` matches itself (same `Modifiers`, same `ReturnType`, same `Variables`).
TEST(CSharp_FieldDeclaration, DoMatchSameNode) {
    auto a = make_FieldDeclaration();
    auto b = make_FieldDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask; `Static` matches only `Static`, not `Static|Public`).
TEST(CSharp_FieldDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_FieldDeclaration();
    auto b = make_FieldDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_FieldDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_FieldDeclaration();
    auto b = make_FieldDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` `ReturnType` term -- both present and the
// `ReturnType.DoMatch` rejects on a name mismatch).
TEST(CSharp_FieldDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_FieldDeclaration();  // ReturnType SimpleType int
    FieldHolder b;
    b.fd = std::make_unique<FieldDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("byte"));  // different name
    b.fd->ReturnType(b.returnType.get());
    b.varX = std::make_unique<VariableInitializer>(std::string("x"));
    b.fd->Variables().Add(b.varX.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` asymmetry rejects: a pattern with a `ReturnType` vs a candidate without one.
TEST(CSharp_FieldDeclaration, DoMatchReturnTypeAsymmetryRejects) {
    auto a = make_FieldDeclaration();  // has ReturnType
    FieldDeclaration b;  // no ReturnType (empty ctor)
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    b.Variables().Add(x.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
}

// A `Variables` mismatch rejects (the collection recursive `DoMatch` -- different count rejects).
TEST(CSharp_FieldDeclaration, DoMatchVariablesCountMismatchRejects) {
    auto a = make_FieldDeclaration();  // 1 variable x
    FieldHolder b;
    b.fd = std::make_unique<FieldDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.fd->ReturnType(b.returnType.get());
    // no variables
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Variables` name mismatch rejects (the collection recursive `DoMatch` -- a `VariableInitializer`
// `x` vs a `VariableInitializer` `y` rejects via the `MatchString` on `Name`).
TEST(CSharp_FieldDeclaration, DoMatchVariablesNameMismatchRejects) {
    auto a = make_FieldDeclaration();  // variable x
    FieldHolder b;
    b.fd = std::make_unique<FieldDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("int"));
    b.fd->ReturnType(b.returnType.get());
    b.varX = std::make_unique<VariableInitializer>(std::string("y"));  // different name
    b.fd->Variables().Add(b.varX.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`FieldDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not a `FieldDeclaration`).
TEST(CSharp_FieldDeclaration, DoMatchRejectsNonFieldEntityDeclaration) {
    auto a = make_FieldDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_FieldDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_FieldDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_FieldDeclaration, DoMatchRejectsNull) {
    auto a = make_FieldDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`Attributes`/`Variables`, copies the `Modifiers` scalar,
// and does not detach the source.
TEST(CSharp_FieldDeclaration, CloneDeepCopies) {
    auto a = make_FieldDeclaration();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<FieldDeclaration>(
        static_cast<FieldDeclaration*>(a->Clone()));
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

// `Clone` is virtual through `AstNode*` and covariant through `FieldDeclaration*`.
TEST(CSharp_FieldDeclaration, CloneVirtualAndCovariant) {
    auto a = make_FieldDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<FieldDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<FieldDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_FieldDeclaration, CloneCopiesAttributes) {
    auto a = make_FieldDeclarationWithAttribute();
    auto clone = std::unique_ptr<FieldDeclaration>(
        static_cast<FieldDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `ReturnType` required slot is filled).
TEST(CSharp_FieldDeclaration, CheckInvariantPassesOnFilled) {
    auto fd = make_FieldDeclaration();  // ReturnType filled
    fd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `ReturnType` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_FieldDeclaration, CheckInvariantRejectsEmpty) {
    FieldDeclaration fd;  // no ReturnType
    EXPECT_DEATH(fd.CheckInvariant(), "");
}
#endif
