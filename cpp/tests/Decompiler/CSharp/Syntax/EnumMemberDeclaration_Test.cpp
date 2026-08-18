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

// Tests for the `EnumMemberDeclaration` concrete node (cpp/.../Syntax/EnumMemberDeclaration.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/EnumMemberDeclaration.cs) -- the
// `enum_member_declaration` node: an `Attributes` `AttributeSection` collection + a required
// `NameToken` `Identifier` + a nullable `Expression?` `Initializer` (the optional `= expression`
// of an enum member, e.g. the `1` in `enum E { A = 1 }`), plus the new `Slots::EnumMemberInitializer`
// `Expression`-typed kind. The next in-order Phase-5 piece per the D274 plan. The suite shares a
// `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EnumMemberDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
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
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitEnumMemberDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier` of its `Attributes` collection, the
// `Identifier` of its `NameToken`, the `PrimitiveExpression` of its `Initializer`, and the
// `DestructorDeclaration`/`WhileStatement` used for the cross-type DoMatch rejections), recording a
// tag and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitEnumMemberDeclaration(EnumMemberDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-enummember>"); return; }
        trace.push_back("enummember:" + node->Name());
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

// A holder keeping an `EnumMemberDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes). An `EnumMemberDeclaration` with a
// `NameToken` `Identifier` `A`, an `Initializer` `PrimitiveExpression` `1` (and, for the attribute
// variant, an `Attributes` `AttributeSection` whose `Attributes` hold an `Attribute` whose `Type`
// is a `SimpleType` `Foo`).
struct EnumMemberHolder {
    std::unique_ptr<EnumMemberDeclaration> emd;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<PrimitiveExpression> initializer;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    EnumMemberDeclaration* get() const { return emd.get(); }
    EnumMemberDeclaration* operator->() const { return emd.get(); }
};

// Build an `EnumMemberDeclaration` with a `NameToken` `A` and an `Initializer` `PrimitiveExpression`
// `1` (no attributes). The holder keeps every node alive.
EnumMemberHolder make_EnumMemberDeclaration() {
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    h.emd->NameToken(h.nameToken.get());
    h.initializer = std::make_unique<PrimitiveExpression>(int32_t(1));
    h.emd->Initializer(h.initializer.get());
    return h;
}

// Build an `EnumMemberDeclaration` with a `NameToken` `A`, an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`, and an `Initializer`
// `PrimitiveExpression` `1`.
EnumMemberHolder make_EnumMemberDeclarationWithAttribute() {
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    h.emd->NameToken(h.nameToken.get());
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.emd->Attributes().Add(h.attrSec.get());
    h.initializer = std::make_unique<PrimitiveExpression>(int32_t(1));
    h.emd->Initializer(h.initializer.get());
    return h;
}

} // namespace

// ==========================================================================
// EnumMemberDeclaration (the enum_member_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `EnumMemberDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_EnumMemberDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<EnumMemberDeclaration>);
}

// `EnumMemberDeclaration` derives from `EntityDeclaration` (a type member is a structural
// container, not a `Statement`/`Expression`/`AstType`); it is an `EntityDeclaration` and an
// `AstNode` but NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_EnumMemberDeclaration, IsAstNodeNotStatementNotExpressionNotAstType) {
    EnumMemberDeclaration emd;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&emd), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&emd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&emd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&emd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&emd), nullptr);
}

// ---- The SymbolKind override ---------------------------------------------

// The abstract `SymbolKind` is overridden by `EnumMemberDeclaration` to return `SymbolKind::Field`
// (an enum member is field-like: a named constant in an enum, reported as `SymbolKind.Field` by
// the C# source).
TEST(CSharp_EnumMemberDeclaration, SymbolKindOverrideReturnsField) {
    EnumMemberDeclaration emd;
    EXPECT_EQ(emd.SymbolKind(), SymbolKind::Field);
}

// ---- The Modifiers scalar (a settable [Flags] enum, NOT a [Slot]) --------

// `Modifiers` defaults to `None` (the enum's zero value -- a declaration with no modifiers). The
// setter round-trips a combined mask (the `[Flags]` bitwise operators).
TEST(CSharp_EnumMemberDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    EnumMemberDeclaration emd;
    EXPECT_EQ(emd.Modifiers(), Modifiers::None);
    emd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(emd.Modifiers(), Modifiers::Static | Modifiers::Public);
}

// `HasModifier(mod)` is a bitmask test (all of `mod`'s bits set), NOT the pattern-match `==`.
TEST(CSharp_EnumMemberDeclaration, HasModifierIsBitmaskTest) {
    EnumMemberDeclaration emd;
    emd.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(emd.HasModifier(Modifiers::Static));
    EXPECT_TRUE(emd.HasModifier(Modifiers::Public));
    EXPECT_FALSE(emd.HasModifier(Modifiers::Virtual));
}

// ---- The virtual Name/NameToken (over the Identifier slot) ----------------

// `NameToken` (the concrete override) returns the backing field. The `Name` virtual (inherited
// base kind-walk) returns the `NameToken`'s `Name` (the real name -- `EnumMemberDeclaration` does
// NOT override `Name`, unlike `Accessor` whose no-op returns empty), empty when the token is
// absent.
TEST(CSharp_EnumMemberDeclaration, NameAndNameTokenOverIdentifierSlot) {
    EnumMemberDeclaration emd;
    // No NameToken yet: the inherited base `Name()` kind-walks for the `Identifier` kind and
    // returns empty (the token is absent).
    EXPECT_EQ(emd.Name(), "");
    EXPECT_EQ(emd.NameToken(), nullptr);
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    emd.NameToken(tok.get());
    EXPECT_EQ(emd.NameToken(), tok.get());
    EXPECT_EQ(emd.Name(), "A");
    EXPECT_EQ(tok->Parent(), &emd);
}

// The `Name(string)` setter (the inherited base virtual) creates an `Identifier` token via
// `Identifier::Create` and sets the `Identifier`-kind slot.
TEST(CSharp_EnumMemberDeclaration, NameStringSetterCreatesToken) {
    EnumMemberDeclaration emd;
    emd.Name(std::string("B"));
    ASSERT_NE(emd.NameToken(), nullptr);
    EXPECT_EQ(emd.NameToken()->Name(), "B");
    EXPECT_EQ(emd.Name(), "B");
}

// ---- The virtual ReturnType (over the Type slot; nullable) --------------

// `ReturnType` (the inherited base virtual) kind-walks for the `Type` kind. An
// `EnumMemberDeclaration` declares no `Type` slot, so it returns null (an enum member has no
// return type).
TEST(CSharp_EnumMemberDeclaration, ReturnTypeIsNull) {
    EnumMemberDeclaration emd;
    EXPECT_EQ(emd.ReturnType(), nullptr);
}

// ---- GetChildren<T> (the kind-based collection read) ----------------------

// `GetChildren<T>` returns the real collection for a kind the node declares a collection of (the
// `Attributes` `AttributeSection` collection of an `EnumMemberDeclaration`).
TEST(CSharp_EnumMemberDeclaration, GetChildrenReturnsRealCollectionForDeclaredKind) {
    EnumMemberDeclaration emd;
    auto& attrs = emd.GetChildren<AttributeSection>(&Slots::AttributeSection);
    EXPECT_EQ(attrs.Count(), 0);
    EXPECT_EQ(&attrs, &emd.Attributes());
}

// `GetChildren<T>` returns a detached EMPTY collection for a kind the node declares NO collection
// of (an `EnumMemberDeclaration` has no `Statement` collection). The detached empty is distinct
// from the real `Attributes` collection (a stable per-`<T>` singleton).
TEST(CSharp_EnumMemberDeclaration, GetChildrenReturnsDetachedEmptyForAbsentKind) {
    EnumMemberDeclaration emd;
    auto& stmts = emd.GetChildren<Statement>(&Slots::Statement);
    EXPECT_EQ(stmts.Count(), 0);
    auto& attrs = emd.GetChildren<AttributeSection>(&Slots::AttributeSection);
    // The two collections span distinct element types (`Statement` vs `AttributeSection`), so
    // `AstNodeCollectionT<Statement>*` and `AstNodeCollectionT<AttributeSection>*` are unrelated
    // pointer types -- cast to the common `AstNodeCollection*` base for the `EXPECT_NE` (the
    // D251/D252 cross-element-type crux).
    EXPECT_NE(static_cast<AstNodeCollection*>(&stmts),
              static_cast<AstNodeCollection*>(&attrs));
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `NameToken`, no `Initializer`, empty `Attributes`. `GetChildCount` is
// `0 + 2 = 2` (the empty `Attributes` collection plus the two single slots `NameToken` +
// `Initializer`).
TEST(CSharp_EnumMemberDeclaration, EmptyCtorHasNoSlots) {
    EnumMemberDeclaration emd;
    EXPECT_EQ(emd.NameToken(), nullptr);
    EXPECT_EQ(emd.Initializer(), nullptr);
    EXPECT_EQ(emd.Attributes().Count(), 0);
    EXPECT_EQ(emd.GetChildCount(), 2);
    EXPECT_EQ(emd.SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(emd.Modifiers(), Modifiers::None);
}

// ---- The NameToken slot (a single REQUIRED Identifier) ------------------

// The `NameToken` setter re-parents the token. The slot follows the `Attributes` collection, so
// the index-less `SetChildNode` invalidates the parent's indices; the reindex is triggered by
// `Slot()` (the `EnsureChildIndices` call). After the reindex the `NameToken`'s flattened
// `ChildIndex` is `attrCount` (0 with no attributes -- the `NameToken` is the first non-empty
// slot).
TEST(CSharp_EnumMemberDeclaration, NameTokenSetterReparentsAndReindexes) {
    EnumMemberDeclaration emd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    emd.NameToken(tok.get());
    EXPECT_EQ(emd.NameToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &emd);
    (void)tok->Slot();  // trigger the lazy reindex
    EXPECT_EQ(tok->ChildIndex, 0);  // attrCount (0) -- NameToken at flattened index 0
}

// The `NameToken` setter detaches the old token.
TEST(CSharp_EnumMemberDeclaration, NameTokenSetterDetachesOld) {
    EnumMemberDeclaration emd;
    auto a = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    auto b = std::unique_ptr<Identifier>(Identifier::Create(std::string("B")));
    emd.NameToken(a.get());
    EXPECT_EQ(a->Parent(), &emd);
    emd.NameToken(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(emd.NameToken(), b.get());
}

// ---- The Initializer slot (a single NULLABLE Expression) ------------------

// The `Initializer` setter re-parents the expression.
TEST(CSharp_EnumMemberDeclaration, InitializerSetterReparents) {
    EnumMemberDeclaration emd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    emd.NameToken(tok.get());
    auto init = std::make_unique<PrimitiveExpression>(int32_t(1));
    emd.Initializer(init.get());
    EXPECT_EQ(emd.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &emd);
}

// The `Initializer` setter detaches the old expression and clears with null.
TEST(CSharp_EnumMemberDeclaration, InitializerSetterDetachesAndClears) {
    EnumMemberDeclaration emd;
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    emd.NameToken(tok.get());
    auto a = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto b = std::make_unique<PrimitiveExpression>(int32_t(2));
    emd.Initializer(a.get());
    emd.Initializer(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(emd.Initializer(), b.get());
    emd.Initializer(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(emd.Initializer(), nullptr);
}

// ---- The Attributes collection (non-incremental) ------------------------

// `Attributes().Add(...)` appends and parents; the collection is NON-incremental (the `NameToken`
// and `Initializer` singles trail it), so `Add` invalidates the parent's indices for a lazy reindex.
TEST(CSharp_EnumMemberDeclaration, AttributesCollectionAddReparents) {
    EnumMemberDeclaration emd;
    auto attrSec = std::make_unique<AttributeSection>();  // an empty AttributeSection
    emd.Attributes().Add(attrSec.get());
    EXPECT_EQ(emd.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &emd);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, then `NameToken` at
// `attrCount`, then `Initializer` at `attrCount + 1`.
TEST(CSharp_EnumMemberDeclaration, GetChildWalksSlots) {
    auto emd = make_EnumMemberDeclaration();
    EXPECT_EQ(emd->GetChildCount(), 2);  // 0 attrs + NameToken + Initializer
    EXPECT_EQ(emd->GetChild(0), emd->NameToken());
    EXPECT_EQ(emd->GetChild(1), emd->Initializer());
    EXPECT_THROW(emd->GetChild(2), std::out_of_range);
}

// `GetChildSlotInfo` returns the per-node slot for each flattened index.
TEST(CSharp_EnumMemberDeclaration, GetChildSlotInfoWalksSlots) {
    auto emd = make_EnumMemberDeclaration();
    EXPECT_EQ(emd->GetChildSlotInfo(0), &emd->NameTokenSlot);   // NameToken at attrCount (0)
    EXPECT_EQ(emd->GetChildSlotInfo(1), &emd->InitializerSlot);  // Initializer at attrCount + 1
    EXPECT_THROW(emd->GetChildSlotInfo(2), std::out_of_range);
}

// `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind, null
// for other kinds (the single-slot kinds `Identifier`/`EnumMemberInitializer` and unrelated kinds
// all fall back).
TEST(CSharp_EnumMemberDeclaration, GetCollectionByKindReturnsAttributes) {
    EnumMemberDeclaration emd;
    EXPECT_EQ(emd.GetCollectionByKind(&Slots::AttributeSection), &emd.Attributes());
    EXPECT_EQ(emd.GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(emd.GetCollectionByKind(&Slots::EnumMemberInitializer), nullptr);
}

// `SetChild` replaces the `NameToken` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_EnumMemberDeclaration, SetChildReplacesNameToken) {
    auto emd = make_EnumMemberDeclaration();
    auto tok1 = emd->NameToken();  // keep the original alive via the holder
    auto tok2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("B")));
    emd->SetChild(0, tok2.get());  // NameToken at flattened index 0 (0 attrs)
    EXPECT_EQ(emd->NameToken(), tok2.get());
    EXPECT_EQ(tok1->Parent(), nullptr);
}

// `SetChild` replaces the `Initializer` in place (the slot must already exist at the flattened
// index `attrCount + 1`).
TEST(CSharp_EnumMemberDeclaration, SetChildReplacesInitializer) {
    auto emd = make_EnumMemberDeclaration();
    auto init1 = emd->Initializer();  // keep the original alive via the holder
    auto init2 = std::make_unique<PrimitiveExpression>(int32_t(2));
    emd->SetChild(1, init2.get());  // Initializer at flattened index 1 (0 attrs)
    EXPECT_EQ(emd->Initializer(), init2.get());
    EXPECT_EQ(init1->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

// The slot statics point at the shared `Slots` kinds.
TEST(CSharp_EnumMemberDeclaration, SlotStaticsPointAtSharedKinds) {
    EnumMemberDeclaration emd;
    EXPECT_EQ(emd.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(emd.NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(emd.InitializerSlot.Kind(), &Slots::EnumMemberInitializer);
}

// The slot statics are distinct (the three kinds are distinct -- cast to the common
// `CSharpSlotInfo*` base for the cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_EnumMemberDeclaration, SlotStaticsAreDistinct) {
    EnumMemberDeclaration emd;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&emd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&emd.NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&emd.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&emd.InitializerSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&emd.NameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&emd.InitializerSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

// `AcceptVisitor` dispatches to `VisitEnumMemberDeclaration` (the concrete `Visit`).
TEST(CSharp_EnumMemberDeclaration, AcceptVisitorDispatches) {
    EnumMemberDeclaration emd;
    RecordingVisitor v;
    emd.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"enummember:"}));
}

// `AcceptVisitor` is virtual: a call through `AstNode*` dispatches to the concrete override.
TEST(CSharp_EnumMemberDeclaration, AcceptVisitorVirtualThroughBase) {
    EnumMemberDeclaration emd;
    AstNode* node = &emd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"enummember:"}));
}

// `AcceptVisitor` is virtual: a call through `EntityDeclaration*` dispatches to the concrete
// override.
TEST(CSharp_EnumMemberDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    EnumMemberDeclaration emd;
    EntityDeclaration* node = &emd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"enummember:"}));
}

// The depth-first walk recurses into the `NameToken` (a real `Identifier` child) and the
// `Initializer` (a `PrimitiveExpression`) in document order.
TEST(CSharp_EnumMemberDeclaration, DepthFirstWalk) {
    auto emd = make_EnumMemberDeclaration();
    RecordingVisitor v;
    emd->AcceptVisitor(v);
    // enummember:A -> id:A (the NameToken) -> prim (the Initializer)
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "enummember:A", "id:A", "prim",
    }));
}

// A `EnumMemberDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` in document order (the `Attributes` collection is
// the first slot, before `NameToken` and `Initializer`).
TEST(CSharp_EnumMemberDeclaration, DepthFirstWalkWithAttribute) {
    auto emd = make_EnumMemberDeclarationWithAttribute();
    RecordingVisitor v;
    emd->AcceptVisitor(v);
    // enummember:A -> attrsec (Attributes[0]) -> attr -> simple:Foo (the Attribute's Type)
    //      -> id:Foo -> id:A (the NameToken) -> prim (the Initializer)
    ASSERT_EQ(v.trace.size(), 7u);
    EXPECT_EQ(v.trace[0], "enummember:A");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "id:A");
    EXPECT_EQ(v.trace[6], "prim");
}

// A `EnumMemberDeclaration` with no `Initializer` (a plain `enum E { A }` with no explicit value)
// recurses into just the `NameToken`.
TEST(CSharp_EnumMemberDeclaration, DepthFirstWalkNoInitializer) {
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    h.emd->NameToken(h.nameToken.get());
    RecordingVisitor v;
    h.emd->AcceptVisitor(v);
    // enummember:A -> id:A (the NameToken); no Initializer child
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "enummember:A", "id:A",
    }));
}

// ---- DoMatch (the generated pattern match) --------------------------------

// A `EnumMemberDeclaration` matches itself (same `Name`, same `Modifiers`, both `Initializer`
// present).
TEST(CSharp_EnumMemberDeclaration, DoMatchSameNode) {
    auto a = make_EnumMemberDeclaration();
    auto b = make_EnumMemberDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `MatchString` `Name` term -- the enum member's name IS part of
// the structural match, unlike `DestructorDeclaration` whose `NameToken` is `[ExcludeFromMatch]`).
TEST(CSharp_EnumMemberDeclaration, DoMatchNameMismatchRejects) {
    auto a = make_EnumMemberDeclaration();  // name "A"
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("B")));
    h.emd->NameToken(h.nameToken.get());
    h.initializer = std::make_unique<PrimitiveExpression>(int32_t(1));
    h.emd->Initializer(h.initializer.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), h.get()));  // "A" vs "B"
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask; `Static` matches only `Static`, not `Static|Public`).
TEST(CSharp_EnumMemberDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_EnumMemberDeclaration();
    auto b = make_EnumMemberDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_EnumMemberDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_EnumMemberDeclaration();
    auto b = make_EnumMemberDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// An `Initializer` asymmetry rejects: a pattern with an `Initializer` vs a candidate without one.
TEST(CSharp_EnumMemberDeclaration, DoMatchInitializerAsymmetryRejects) {
    auto a = make_EnumMemberDeclaration();  // has Initializer
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    h.emd->NameToken(h.nameToken.get());
    // no Initializer
    EXPECT_FALSE(DoMatchAgainst(a.get(), h.get()));
}

// Both `Initializer` absent matches (the `MatchOptional` `Initializer` term -- both absent
// matches, unlike the one-present + one-absent asymmetry).
TEST(CSharp_EnumMemberDeclaration, DoMatchBothInitializerAbsentMatches) {
    EnumMemberHolder a;
    a.emd = std::make_unique<EnumMemberDeclaration>();
    a.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    a.emd->NameToken(a.nameToken.get());
    EnumMemberHolder b;
    b.emd = std::make_unique<EnumMemberDeclaration>();
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    b.emd->NameToken(b.nameToken.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// The `Any` `String` wildcard in a pattern `Name` matches any candidate name (the `Pattern::AnyString`
// -- the `$any$` group). A pattern node whose `Name` is the `$any$` wildcard matches any candidate.
TEST(CSharp_EnumMemberDeclaration, DoMatchAnyStringWildcardInName) {
    // A pattern Identifier whose Name is the AnyString wildcard, set as the NameToken of a
    // pattern EnumMemberDeclaration, matches a candidate with any name.
    EnumMemberHolder a;
    a.emd = std::make_unique<EnumMemberDeclaration>();
    a.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string(Pattern::AnyString)));
    a.emd->NameToken(a.nameToken.get());
    EnumMemberHolder b;
    b.emd = std::make_unique<EnumMemberDeclaration>();
    b.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("B")));
    b.emd->NameToken(b.nameToken.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EnumMemberDeclaration` candidate rejects (the type-check gate). A `DestructorDeclaration`
// is an `EntityDeclaration` but not an `EnumMemberDeclaration`.
TEST(CSharp_EnumMemberDeclaration, DoMatchRejectsNonEnumMember) {
    auto a = make_EnumMemberDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    auto tok = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    b->NameToken(tok.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_EnumMemberDeclaration, DoMatchRejectsNull) {
    auto a = make_EnumMemberDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `NameToken`/`Initializer`/`Attributes`, copies the `Modifiers` scalar,
// and does not detach the source.
TEST(CSharp_EnumMemberDeclaration, CloneDeepCopies) {
    auto a = make_EnumMemberDeclaration();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    auto clone = std::unique_ptr<EnumMemberDeclaration>(
        static_cast<EnumMemberDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), a->NameToken());
    EXPECT_EQ(clone->NameToken()->Name(), "A");
    ASSERT_NE(clone->Initializer(), nullptr);
    EXPECT_NE(clone->Initializer(), a->Initializer());
    // The source is not detached.
    EXPECT_EQ(a->NameToken()->Parent(), a.get());
    EXPECT_EQ(a->Initializer()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `EnumMemberDeclaration*`.
TEST(CSharp_EnumMemberDeclaration, CloneVirtualAndCovariant) {
    auto a = make_EnumMemberDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<EnumMemberDeclaration*>(clone.get()), nullptr);
    // Covariant: `EnumMemberDeclaration::Clone` returns `EnumMemberDeclaration*` through the
    // `AstNode::Clone` virtual.
    auto cov = std::unique_ptr<EnumMemberDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_EnumMemberDeclaration, CloneCopiesAttributes) {
    auto a = make_EnumMemberDeclarationWithAttribute();
    auto clone = std::unique_ptr<EnumMemberDeclaration>(
        static_cast<EnumMemberDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
}

// `Clone` skips the absent `Initializer` (a plain `enum E { A }` with no explicit value).
TEST(CSharp_EnumMemberDeclaration, CloneSkipsAbsentInitializer) {
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    h.emd->NameToken(h.nameToken.get());
    auto clone = std::unique_ptr<EnumMemberDeclaration>(
        static_cast<EnumMemberDeclaration*>(h.emd->Clone()));
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_EQ(clone->NameToken()->Name(), "A");
    EXPECT_EQ(clone->Initializer(), nullptr);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `NameToken` required slot is filled, the
// `Initializer` is nullable so its absence is invariant-valid).
TEST(CSharp_EnumMemberDeclaration, CheckInvariantPassesOnFilled) {
    auto emd = make_EnumMemberDeclaration();  // NameToken filled, Initializer present
    emd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on a node with no `Initializer` (the `Initializer` is nullable, so its
// absence is invariant-valid -- unlike the `NameToken` which is required).
TEST(CSharp_EnumMemberDeclaration, CheckInvariantPassesWithoutInitializer) {
    EnumMemberHolder h;
    h.emd = std::make_unique<EnumMemberDeclaration>();
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("A")));
    h.emd->NameToken(h.nameToken.get());
    h.emd->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NameToken` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_EnumMemberDeclaration, CheckInvariantRejectsEmpty) {
    EnumMemberDeclaration emd;  // no NameToken
    EXPECT_DEATH(emd.CheckInvariant(), "");
}
#endif
