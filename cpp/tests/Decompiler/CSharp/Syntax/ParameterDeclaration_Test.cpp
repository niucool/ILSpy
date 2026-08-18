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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the `ParameterDeclaration` concrete node (cpp/.../Syntax/ParameterDeclaration.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ParameterDeclaration.cs) -- the
// `fixed_parameter`/`parameter_array` node: an `Attributes` `AttributeSection` collection + a
// nullable `AstType?` `Type` + a nullable `string?` `Name` (over a backing `NameToken` `Identifier?`)
// + a nullable `Expression?` `DefaultExpression`, plus four scalars (`HasThisModifier`/`IsParams`/
// `IsScopedRef` bools + `ParameterModifier` `ReferenceKind`). The next in-order Phase-5 piece per
// the D277 plan. The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234
// pattern). Also tests the new `ReferenceKind` `TypeSystem` enum (the `SymbolKind` D271 precedent).

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
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;

namespace {

// A recording depth-first visitor: overrides the `VisitParameterDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier` of its `Attributes` collection and
// `Type`/`NameToken`, the `PrimitiveExpression` of its `DefaultExpression`, and the
// `WhileStatement` used for the cross-type DoMatch rejection), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-param>"); return; }
        auto name = node->Name();
        trace.push_back("param:" + (name ? *name : std::string("")));
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

// A holder keeping a `ParameterDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes).
struct ParamHolder {
    std::unique_ptr<ParameterDeclaration> param;
    std::unique_ptr<SimpleType> type;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<PrimitiveExpression> defaultExpr;
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    ParameterDeclaration* get() const { return param.get(); }
    ParameterDeclaration* operator->() const { return param.get(); }
};

// Build a `ParameterDeclaration` with a `Type` `SimpleType` `int`, a `Name` `x`, and a
// `DefaultExpression` `PrimitiveExpression` `0` (no attributes). The holder keeps every node
// alive.
ParamHolder make_ParameterDeclaration() {
    ParamHolder h;
    h.param = std::make_unique<ParameterDeclaration>();
    h.type = std::make_unique<SimpleType>(std::string("int"));
    h.param->Type(h.type.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param->NameToken(h.nameToken.get());
    h.defaultExpr = std::make_unique<PrimitiveExpression>(int32_t(0));
    h.param->DefaultExpression(h.defaultExpr.get());
    return h;
}

// Build a `ParameterDeclaration` with a `Type` `int`, a `Name` `x`, an `Attributes`
// `AttributeSection` holding an `Attribute` whose `Type` is a `SimpleType` `Foo`, and a
// `DefaultExpression` `0`.
ParamHolder make_ParameterDeclarationWithAttribute() {
    ParamHolder h;
    h.param = std::make_unique<ParameterDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.param->Attributes().Add(h.attrSec.get());
    h.type = std::make_unique<SimpleType>(std::string("int"));
    h.param->Type(h.type.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param->NameToken(h.nameToken.get());
    h.defaultExpr = std::make_unique<PrimitiveExpression>(int32_t(0));
    h.param->DefaultExpression(h.defaultExpr.get());
    return h;
}

} // namespace

// ==========================================================================
// ReferenceKind (the TypeSystem enum, the first ported TypeSystem enum
// consumed by a CSharp.Syntax scalar -- the SymbolKind D271 precedent)
// ==========================================================================

// `None` is the zero value (the C# default for an uninitialized `ParameterModifier` property).
TEST(CSharp_ReferenceKind, NoneIsZero) {
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::None), 0);
}

// The 5 members (None .. RefReadOnly) mirror the C# declaration order; the last member
// `RefReadOnly` is value 4.
TEST(CSharp_ReferenceKind, RefReadOnlyIsLast) {
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::RefReadOnly), 4);
}

// The C# underlying type is `byte`; the port keeps `std::uint8_t`.
TEST(CSharp_ReferenceKind, IsByteSized) {
    EXPECT_EQ(sizeof(ReferenceKind), 1u);
    static_assert(std::is_enum_v<ReferenceKind>);
    static_assert(std::is_same_v<std::underlying_type_t<ReferenceKind>, std::uint8_t>);
}

// The members are distinct and in C# declaration order (None=0, Out=1, Ref=2, In=3, RefReadOnly=4).
TEST(CSharp_ReferenceKind, MembersAreDistinctAndOrdered) {
    EXPECT_NE(static_cast<std::uint8_t>(ReferenceKind::Out),
        static_cast<std::uint8_t>(ReferenceKind::Ref));
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::None), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::Out), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::Ref), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::In), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(ReferenceKind::RefReadOnly), 4);
}

// `ReferenceKind` declares NO `Any` member (unlike the `BinaryOperatorType`/`AssignmentOperatorType`
// enums), so the generator's `hasAny` path does NOT fire for `ParameterDeclaration.ParameterModifier`
// -- the `DoMatch` term is the plain `==`, NOT a wildcard.
TEST(CSharp_ReferenceKind, HasNoAnyMember) {
    // There is no `ReferenceKind::Any` -- this test documents that fact (the absence is what makes
    // the `ParameterModifier` DoMatch term a plain equality).
    SUCCEED();
}

// ==========================================================================
// ParameterDeclaration (the parameter node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `ParameterDeclaration` is NOT `final` (the C# is not `sealed`; `hasPatternPlaceholder: true`
// emits a sealed `PatternPlaceholder` deriving from it -- the deferred placeholder).
TEST(CSharp_ParameterDeclaration, IsConcreteAndNotFinal) {
    EXPECT_FALSE(std::is_final_v<ParameterDeclaration>);
    EXPECT_FALSE(std::is_abstract_v<ParameterDeclaration>);
}

// `ParameterDeclaration` derives directly from `AstNode` (a parameter is a structural node owned
// by a declaration's `Parameters` collection, NOT a member declaration); it is an `AstNode` but NOT
// a `Statement`/`Expression`/`AstType`/`EntityDeclaration`.
TEST(CSharp_ParameterDeclaration, IsAstNodeNotStatementNotExpressionNotAstTypeNotEntityDeclaration) {
    ParameterDeclaration pd;
    EXPECT_NE(dynamic_cast<AstNode*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&pd), nullptr);
    EXPECT_EQ(dynamic_cast<EntityDeclaration*>(&pd), nullptr);
}

// ---- The const keyword tokens --------------------------------------------

// The seven const-string keyword tokens are part of the public API (the output visitor reads
// them) and port as `static constexpr const char*`.
TEST(CSharp_ParameterDeclaration, ConstKeywordTokens) {
    EXPECT_STREQ(ParameterDeclaration::ThisModifier, "this");
    EXPECT_STREQ(ParameterDeclaration::ScopedRefKeyword, "scoped");
    EXPECT_STREQ(ParameterDeclaration::RefModifier, "ref");
    EXPECT_STREQ(ParameterDeclaration::OutModifier, "out");
    EXPECT_STREQ(ParameterDeclaration::InModifier, "in");
    EXPECT_STREQ(ParameterDeclaration::ParamsModifier, "params");
}

// `ReadonlyModifier` is aliased to `ComposedType::ReadonlyKeyword` (the single source of truth for
// the "readonly" literal).
TEST(CSharp_ParameterDeclaration, ReadonlyModifierAliasesComposedTypeReadonlyKeyword) {
    EXPECT_STREQ(ParameterDeclaration::ReadonlyModifier, "readonly");
    EXPECT_EQ(ParameterDeclaration::ReadonlyModifier, ComposedType::ReadonlyKeyword);
}

// ---- The scalar properties (NOT [Slot]s, not child slots) ----------------

// The three bool scalars default to `false` (the C# defaults) and round-trip through the setters.
TEST(CSharp_ParameterDeclaration, BoolScalars) {
    ParameterDeclaration pd;
    EXPECT_FALSE(pd.HasThisModifier());
    EXPECT_FALSE(pd.IsParams());
    EXPECT_FALSE(pd.IsScopedRef());
    pd.HasThisModifier(true);
    pd.IsParams(true);
    pd.IsScopedRef(true);
    EXPECT_TRUE(pd.HasThisModifier());
    EXPECT_TRUE(pd.IsParams());
    EXPECT_TRUE(pd.IsScopedRef());
    pd.HasThisModifier(false);
    EXPECT_FALSE(pd.HasThisModifier());
}

// `ParameterModifier` defaults to `ReferenceKind::None` (the C# default) and round-trips. NO name
// shadowing (the accessor name differs from the enum name, so no elaborated specifier is needed).
TEST(CSharp_ParameterDeclaration, ParameterModifierScalar) {
    ParameterDeclaration pd;
    EXPECT_EQ(pd.ParameterModifier(), ReferenceKind::None);
    pd.ParameterModifier(ReferenceKind::Out);
    EXPECT_EQ(pd.ParameterModifier(), ReferenceKind::Out);
    pd.ParameterModifier(ReferenceKind::RefReadOnly);
    EXPECT_EQ(pd.ParameterModifier(), ReferenceKind::RefReadOnly);
}

// ---- The Attributes collection --------------------------------------------

// The `Attributes` collection is empty by default (an empty node reports `GetChildCount` 3 -- the
// three nullable singles each contribute 1 to the flattened count even when absent). Adding an
// `AttributeSection` re-parents it and grows the collection.
TEST(CSharp_ParameterDeclaration, AttributesCollection) {
    ParameterDeclaration pd;
    EXPECT_EQ(pd.Attributes().Count(), 0);
    EXPECT_EQ(pd.GetChildCount(), 3);
    auto attrType = std::make_unique<SimpleType>(std::string("Foo"));
    auto attr = std::make_unique<Attribute>(attrType.get());
    auto attrSec = std::make_unique<AttributeSection>(attr.get());
    pd.Attributes().Add(attrSec.get());
    EXPECT_EQ(pd.Attributes().Count(), 1);
    EXPECT_EQ(pd.GetChildCount(), 4);
    EXPECT_EQ(attrSec->Parent(), &pd);
}

// ---- The Type slot (a nullable single AstType) ----------------------------

// `Type` defaults to null (the slot is nullable) and re-parents/re-indexes on set. Clearing with
// null detaches the old child.
TEST(CSharp_ParameterDeclaration, TypeSlot) {
    ParameterDeclaration pd;
    EXPECT_EQ(pd.Type(), nullptr);
    auto st = std::make_unique<SimpleType>(std::string("int"));
    pd.Type(st.get());
    EXPECT_EQ(pd.Type(), st.get());
    EXPECT_EQ(st->Parent(), &pd);
    pd.Type(nullptr);
    EXPECT_EQ(pd.Type(), nullptr);
    EXPECT_EQ(st->Parent(), nullptr);
}

// ---- The NameToken slot + Name string accessor ----------------------------

// `NameToken` defaults to null (the name is nullable `string?`). `Name()` returns `nullopt` when
// the token is absent (the faithful `string?` null). Setting a non-empty name creates the token;
// `Name()` returns it.
TEST(CSharp_ParameterDeclaration, NameAndNameToken) {
    ParameterDeclaration pd;
    EXPECT_EQ(pd.NameToken(), nullptr);
    EXPECT_EQ(pd.Name(), std::nullopt);
    pd.Name(std::string("x"));
    EXPECT_NE(pd.NameToken(), nullptr);
    ASSERT_TRUE(pd.Name().has_value());
    EXPECT_EQ(*pd.Name(), "x");
    EXPECT_EQ(pd.NameToken()->Parent(), &pd);
}

// Setting an empty/null name clears the token (the `Identifier::CreateIfNotEmpty` behaviour --
// faithful to the `string?` optionality, the `GotoStatement.Label` D257 precedent).
TEST(CSharp_ParameterDeclaration, EmptyNameClearsToken) {
    ParameterDeclaration pd;
    pd.Name(std::string("x"));
    EXPECT_NE(pd.NameToken(), nullptr);
    pd.Name(std::string(""));
    EXPECT_EQ(pd.NameToken(), nullptr);
    EXPECT_EQ(pd.Name(), std::nullopt);
}

// A nameless parameter (set via the `NameToken` slot directly with null) reports `nullopt` for
// `Name()` and still passes `CheckInvariant` (the token is nullable -- a nameless parameter such
// as `__argList` may carry no token).
TEST(CSharp_ParameterDeclaration, NamelessParameterIsInvariantValid) {
    ParameterDeclaration pd;
    EXPECT_EQ(pd.Name(), std::nullopt);
    EXPECT_EQ(pd.NameToken(), nullptr);
#ifndef NDEBUG
    ASSERT_NO_FATAL_FAILURE(pd.CheckInvariant());
#endif
}

// ---- The DefaultExpression slot (a nullable single Expression) ------------

// `DefaultExpression` defaults to null (the slot is nullable) and re-parents on set.
TEST(CSharp_ParameterDeclaration, DefaultExpressionSlot) {
    ParameterDeclaration pd;
    EXPECT_EQ(pd.DefaultExpression(), nullptr);
    auto pe = std::make_unique<PrimitiveExpression>(int32_t(0));
    pd.DefaultExpression(pe.get());
    EXPECT_EQ(pd.DefaultExpression(), pe.get());
    EXPECT_EQ(pe->Parent(), &pd);
    pd.DefaultExpression(nullptr);
    EXPECT_EQ(pd.DefaultExpression(), nullptr);
    EXPECT_EQ(pe->Parent(), nullptr);
}

// ---- Slot storage (the collection -> single -> single -> single dispatch) --

// `GetChild`/`GetChildSlotInfo` walk the collection then the three singles; the valid indices for
// an empty-collection node are 0=`Type`, 1=`NameToken`, 2=`DefaultExpression`; out-of-range throws.
TEST(CSharp_ParameterDeclaration, SlotStorageGetChildAndGetChildSlotInfo) {
    ParamHolder h = make_ParameterDeclaration();
    EXPECT_EQ(h->GetChildCount(), 3);
    EXPECT_EQ(h->GetChild(0), h.type.get());
    EXPECT_EQ(h->GetChild(1), h.nameToken.get());
    EXPECT_EQ(h->GetChild(2), h.defaultExpr.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &ParameterDeclaration::TypeSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &ParameterDeclaration::NameTokenSlot);
    EXPECT_EQ(h->GetChildSlotInfo(2), &ParameterDeclaration::DefaultExpressionSlot);
    EXPECT_THROW(h->GetChild(3), std::out_of_range);
    EXPECT_THROW(h->GetChildSlotInfo(3), std::out_of_range);
}

// With an attribute, the flattened layout is `Attributes [0,1)`, `Type` at 1, `NameToken` at 2,
// `DefaultExpression` at 3; `GetChildSlotInfo` returns the `AttributesSlot` for the collection
// range.
TEST(CSharp_ParameterDeclaration, SlotStorageWithAttribute) {
    ParamHolder h = make_ParameterDeclarationWithAttribute();
    EXPECT_EQ(h->GetChildCount(), 4);
    EXPECT_EQ(h->GetChild(0), h.attrSec.get());
    EXPECT_EQ(h->GetChild(1), h.type.get());
    EXPECT_EQ(h->GetChild(2), h.nameToken.get());
    EXPECT_EQ(h->GetChild(3), h.defaultExpr.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &ParameterDeclaration::AttributesSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &ParameterDeclaration::TypeSlot);
    EXPECT_EQ(h->GetChildSlotInfo(2), &ParameterDeclaration::NameTokenSlot);
    EXPECT_EQ(h->GetChildSlotInfo(3), &ParameterDeclaration::DefaultExpressionSlot);
}

// `SetChild` replaces a single slot in place (the index computed by walking the slots). Replacing
// the `Type` at the dynamic flattened index re-parents the new child and detaches the old.
TEST(CSharp_ParameterDeclaration, SetChildReplacesSingleSlot) {
    ParamHolder h = make_ParameterDeclaration();
    auto newType = std::make_unique<SimpleType>(std::string("string"));
    auto* oldType = h.type.get();
    h->SetChild(0, newType.get());
    EXPECT_EQ(h->Type(), newType.get());
    EXPECT_EQ(newType->Parent(), h.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

// `SetChild` on a collection index replaces the collection element in place.
TEST(CSharp_ParameterDeclaration, SetChildReplacesCollectionElement) {
    ParamHolder h = make_ParameterDeclarationWithAttribute();
    auto newAttrType = std::make_unique<SimpleType>(std::string("Bar"));
    auto newAttr = std::make_unique<Attribute>(newAttrType.get());
    auto newAttrSec = std::make_unique<AttributeSection>(newAttr.get());
    auto* oldAttrSec = h.attrSec.get();
    h->SetChild(0, newAttrSec.get());
    EXPECT_EQ(h->Attributes().At(0), newAttrSec.get());
    EXPECT_EQ(newAttrSec->Parent(), h.get());
    EXPECT_EQ(oldAttrSec->Parent(), nullptr);
}

// `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind and
// null for other kinds.
TEST(CSharp_ParameterDeclaration, GetCollectionByKind) {
    ParamHolder h = make_ParameterDeclarationWithAttribute();
    EXPECT_EQ(h->GetCollectionByKind(&Slots::AttributeSection), &h->Attributes());
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Identifier), nullptr);
    EXPECT_EQ(h->GetCollectionByKind(&Slots::Expression), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

// The slot statics point at the already-ported shared `Slots` kinds; the cross-element-type
// distinctness check casts both operands to the common `const CSharpSlotInfo*` base (the D251/D252
// EXPECT_NE cross-element-type crux).
TEST(CSharp_ParameterDeclaration, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(ParameterDeclaration::AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(ParameterDeclaration::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(ParameterDeclaration::NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(ParameterDeclaration::DefaultExpressionSlot.Kind(), &Slots::Expression);
    EXPECT_TRUE(ParameterDeclaration::AttributesSlot.IsCollection());
    EXPECT_FALSE(ParameterDeclaration::TypeSlot.IsCollection());
    EXPECT_TRUE(ParameterDeclaration::TypeSlot.IsOptional());
    EXPECT_TRUE(ParameterDeclaration::NameTokenSlot.IsOptional());
    EXPECT_TRUE(ParameterDeclaration::DefaultExpressionSlot.IsOptional());

    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ParameterDeclaration::AttributesSlot),
        static_cast<const CSharpSlotInfo*>(&ParameterDeclaration::TypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ParameterDeclaration::TypeSlot),
        static_cast<const CSharpSlotInfo*>(&ParameterDeclaration::NameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ParameterDeclaration::NameTokenSlot),
        static_cast<const CSharpSlotInfo*>(&ParameterDeclaration::DefaultExpressionSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

// `AcceptVisitor` dispatches to `VisitParameterDeclaration` (the recording visitor records the
// tag); an empty node's `Children()` is empty (the nullable singles are all null), so
// `VisitChildren` yields just the node tag.
TEST(CSharp_ParameterDeclaration, AcceptVisitorDispatch) {
    ParameterDeclaration pd;
    RecordingVisitor v;
    pd.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "param:");
}

// The dispatch is virtual through `AstNode*` (a `ParameterDeclaration*` upcast to `AstNode*` still
// routes to `VisitParameterDeclaration`).
TEST(CSharp_ParameterDeclaration, AcceptVisitorIsVirtual) {
    ParameterDeclaration pd;
    AstNode* node = &pd;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "param:");
}

// ---- The depth-first walk ------------------------------------------------

// The depth-first walk over a filled parameter (no attributes) visits the `Type` `SimpleType`
// (which recurses into its `Identifier` `int`), the `NameToken` `Identifier` `x`, then the
// `DefaultExpression` `PrimitiveExpression`, in declaration order.
TEST(CSharp_ParameterDeclaration, DepthFirstWalkNoAttributes) {
    ParamHolder h = make_ParameterDeclaration();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 5u);
    EXPECT_EQ(v.trace[0], "param:x");
    EXPECT_EQ(v.trace[1], "simple:int");
    EXPECT_EQ(v.trace[2], "id:int");
    EXPECT_EQ(v.trace[3], "id:x");
    EXPECT_EQ(v.trace[4], "prim");
}

// The depth-first walk over a parameter with an attribute visits the `AttributeSection` (which
// recurses into its `Attribute` -> `SimpleType` `Foo` -> `Identifier` `Foo`) first, then the
// `Type`/`NameToken`/`DefaultExpression` as above.
TEST(CSharp_ParameterDeclaration, DepthFirstWalkWithAttribute) {
    ParamHolder h = make_ParameterDeclarationWithAttribute();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 9u);
    EXPECT_EQ(v.trace[0], "param:x");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
    EXPECT_EQ(v.trace[7], "id:x");
    // The `DefaultExpression` `PrimitiveExpression` is the 9th visit.
    EXPECT_EQ(v.trace[8], "prim");
}

// ---- DoMatch ---------------------------------------------------------------

// A parameter matches an identical parameter (all terms pass).
TEST(CSharp_ParameterDeclaration, DoMatchSame) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `HasThisModifier` mismatch rejects.
TEST(CSharp_ParameterDeclaration, DoMatchHasThisModifierMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    b->HasThisModifier(true);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `IsParams` mismatch rejects.
TEST(CSharp_ParameterDeclaration, DoMatchIsParamsMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    b->IsParams(true);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `IsScopedRef` mismatch rejects.
TEST(CSharp_ParameterDeclaration, DoMatchIsScopedRefMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    b->IsScopedRef(true);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `ParameterModifier` mismatch rejects (the plain-equality term, NOT a wildcard -- `ReferenceKind`
// has no `Any` member, so a `None` pattern matches only a `None` candidate).
TEST(CSharp_ParameterDeclaration, DoMatchParameterModifierMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    b->ParameterModifier(ReferenceKind::Ref);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Type` mismatch rejects (the `MatchOptional` term delegates to the `Type`'s `DoMatch`).
TEST(CSharp_ParameterDeclaration, DoMatchTypeMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    auto otherType = std::make_unique<SimpleType>(std::string("string"));
    b->Type(otherType.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Name` mismatch rejects (the `MatchString` term).
TEST(CSharp_ParameterDeclaration, DoMatchNameMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    b->Name(std::string("y"));
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `DefaultExpression` asymmetry rejects (one has a default, the other does not).
TEST(CSharp_ParameterDeclaration, DoMatchDefaultExpressionAsymmetry) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    b->DefaultExpression(nullptr);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both `DefaultExpression`s absent match (the `MatchOptional` both-absent path).
TEST(CSharp_ParameterDeclaration, DoMatchBothDefaultExpressionsAbsent) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    a->DefaultExpression(nullptr);
    b->DefaultExpression(nullptr);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both `Type`s absent match (the `MatchOptional` both-absent path).
TEST(CSharp_ParameterDeclaration, DoMatchBothTypesAbsent) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    a->Type(nullptr);
    b->Type(nullptr);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Both names absent match (the `MatchString(null, null)` path -- a nameless parameter matches
// another nameless parameter).
TEST(CSharp_ParameterDeclaration, DoMatchBothNamesAbsent) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclaration();
    a->Name(std::string(""));
    b->Name(std::string(""));
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// The `$any$` wildcard in the pattern's `Name` matches any candidate name (the `AnyString`
// wildcard, the `GotoStatement` D257 precedent). The pattern must otherwise match the candidate
// structurally (a `Type` asymmetry would reject at the earlier `MatchOptional` term before the
// `Name` `MatchString` runs), so the pattern is a full-structured holder with the `Name` overridden
// to the wildcard.
TEST(CSharp_ParameterDeclaration, DoMatchAnyStringWildcard) {
    ParamHolder pattern = make_ParameterDeclaration();
    pattern->Name(std::string(Pattern::AnyString));
    ParamHolder b = make_ParameterDeclaration();
    EXPECT_TRUE(DoMatchAgainst(pattern.get(), b.get()));
}

// An `Attributes` mismatch rejects (the collection `DoMatch` term).
TEST(CSharp_ParameterDeclaration, DoMatchAttributesMismatch) {
    ParamHolder a = make_ParameterDeclaration();
    ParamHolder b = make_ParameterDeclarationWithAttribute();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`ParameterDeclaration` candidate rejects early (the type-check gate). A `WhileStatement`
// is an unrelated `Statement` node.
TEST(CSharp_ParameterDeclaration, DoMatchRejectsNonParameterCandidate) {
    ParamHolder a = make_ParameterDeclaration();
    WhileStatement ws;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &ws));
}

// A null candidate rejects (the type-check gate, `dynamic_cast` yields null).
TEST(CSharp_ParameterDeclaration, DoMatchRejectsNull) {
    ParamHolder a = make_ParameterDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ----------------------------------------------------------------

// `Clone` deep-copies the slots, copies the scalars, and does not detach the source's children.
TEST(CSharp_ParameterDeclaration, CloneDeepCopies) {
    ParamHolder h = make_ParameterDeclarationWithAttribute();
    h->HasThisModifier(true);
    h->IsParams(true);
    h->ParameterModifier(ReferenceKind::Ref);
    std::unique_ptr<ParameterDeclaration> clone(h->Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), h.get());
    // Scalars copied.
    EXPECT_TRUE(clone->HasThisModifier());
    EXPECT_TRUE(clone->IsParams());
    EXPECT_EQ(clone->ParameterModifier(), ReferenceKind::Ref);
    // Slots deep-copied (distinct pointers, same structural content).
    ASSERT_NE(clone->Type(), nullptr);
    EXPECT_NE(clone->Type(), h.type.get());
    ASSERT_NE(clone->NameToken(), nullptr);
    EXPECT_NE(clone->NameToken(), h.nameToken.get());
    ASSERT_TRUE(clone->Name().has_value());
    EXPECT_EQ(*clone->Name(), "x");
    ASSERT_NE(clone->DefaultExpression(), nullptr);
    EXPECT_NE(clone->DefaultExpression(), h.defaultExpr.get());
    EXPECT_EQ(clone->Attributes().Count(), 1);
    EXPECT_NE(clone->Attributes().At(0), h.attrSec.get());
    // The source's children are NOT detached (still parented to the source).
    EXPECT_EQ(h.type->Parent(), h.get());
    EXPECT_EQ(h.nameToken->Parent(), h.get());
    EXPECT_EQ(h.defaultExpr->Parent(), h.get());
    EXPECT_EQ(h.attrSec->Parent(), h.get());
    // The clone's children are parented to the clone.
    EXPECT_EQ(clone->Type()->Parent(), clone.get());
    EXPECT_EQ(clone->NameToken()->Parent(), clone.get());
    EXPECT_EQ(clone->DefaultExpression()->Parent(), clone.get());
    EXPECT_EQ(clone->Attributes().At(0)->Parent(), clone.get());
}

// `Clone` is virtual through `AstNode*` and returns a `ParameterDeclaration*` (covariant).
TEST(CSharp_ParameterDeclaration, CloneIsVirtualAndCovariant) {
    ParamHolder h = make_ParameterDeclaration();
    AstNode* node = h.get();
    AstNode* clone = node->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ParameterDeclaration*>(clone), nullptr);
    delete clone;
}

// `Clone` of an empty node copies the default scalars and yields null slots.
TEST(CSharp_ParameterDeclaration, CloneEmpty) {
    ParameterDeclaration pd;
    std::unique_ptr<ParameterDeclaration> clone(pd.Clone());
    EXPECT_EQ(clone->Type(), nullptr);
    EXPECT_EQ(clone->NameToken(), nullptr);
    EXPECT_EQ(clone->DefaultExpression(), nullptr);
    EXPECT_EQ(clone->Attributes().Count(), 0);
    EXPECT_FALSE(clone->HasThisModifier());
    EXPECT_EQ(clone->ParameterModifier(), ReferenceKind::None);
}

// ---- CheckInvariant -------------------------------------------------------

// An empty `ParameterDeclaration` is invariant-valid (all three single slots are nullable, and the
// `Attributes` collection is never a required slot -- the `ReturnStatement` D255 nullable-slot
// precedent).
TEST(CSharp_ParameterDeclaration, CheckInvariantEmptyPasses) {
    ParameterDeclaration pd;
#ifndef NDEBUG
    ASSERT_NO_FATAL_FAILURE(pd.CheckInvariant());
#endif
}

// A filled `ParameterDeclaration` (all slots set) passes `CheckInvariant`.
TEST(CSharp_ParameterDeclaration, CheckInvariantFilledPasses) {
    ParamHolder h = make_ParameterDeclarationWithAttribute();
#ifndef NDEBUG
    ASSERT_NO_FATAL_FAILURE(h->CheckInvariant());
#endif
}
