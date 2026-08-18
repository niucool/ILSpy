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

// Tests for the `OperatorDeclaration` concrete node (cpp/.../Syntax/OperatorDeclaration.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/OperatorDeclaration.cs) -- the tenth
// concrete `TypeMember` and the second `EntityDeclaration` with a `Parameters` collection (the
// next in-order Phase-5 piece per the D279 plan, now unblocked by `ParameterDeclaration` D278 for
// the `Parameters` slot). The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the
// D234 pattern).

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
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
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

// A recording depth-first visitor: overrides the `VisitOperatorDeclaration` under test (plus the
// `AttributeSection`/`Attribute`/`SimpleType`/`Identifier`/`PrimitiveExpression`/`BlockStatement`/
// `ReturnStatement`/`ParameterDeclaration` of its slots, and the `DestructorDeclaration`/
// `WhileStatement` used for the cross-type DoMatch rejections), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitOperatorDeclaration(OperatorDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-operator>"); return; }
        trace.push_back("operator");
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

// A holder keeping an `OperatorDeclaration` and all its children alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design: the parent does not take
// ownership; the holder's `unique_ptr`s own the nodes).
struct OperatorHolder {
    std::unique_ptr<OperatorDeclaration> od;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<SimpleType> privateImplType;
    // One parameter "int x" (kept alive with its own Type + NameToken).
    std::unique_ptr<ParameterDeclaration> param0;
    std::unique_ptr<SimpleType> param0Type;
    std::unique_ptr<Identifier> param0NameToken;
    // A body { return 5; }.
    std::unique_ptr<BlockStatement> body;
    std::unique_ptr<ReturnStatement> bodyReturn;
    std::unique_ptr<PrimitiveExpression> bodyReturnExpr;
    // An attribute [Foo].
    std::unique_ptr<AttributeSection> attrSec;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<SimpleType> attrType;
    OperatorDeclaration* get() const { return od.get(); }
    OperatorDeclaration* operator->() const { return od.get(); }
};

// Build an `OperatorDeclaration` with a `ReturnType` `SimpleType` `int` (no attributes, no
// `PrivateImplementationType`, no `Parameters`, no `Body`). The `OperatorType` defaults to
// `LogicalNot` (the enum's zero value). The holder keeps every node alive.
OperatorHolder make_OperatorDeclaration() {
    OperatorHolder h;
    h.od = std::make_unique<OperatorDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.od->ReturnType(h.returnType.get());
    return h;
}

// Build an `OperatorDeclaration` with a `ReturnType` `int` and an `Attributes` `AttributeSection`
// holding an `Attribute` whose `Type` is a `SimpleType` `Foo`.
OperatorHolder make_OperatorDeclarationWithAttribute() {
    OperatorHolder h;
    h.od = std::make_unique<OperatorDeclaration>();
    h.attrType = std::make_unique<SimpleType>(std::string("Foo"));
    h.attr = std::make_unique<Attribute>(h.attrType.get());
    h.attrSec = std::make_unique<AttributeSection>(h.attr.get());
    h.od->Attributes().Add(h.attrSec.get());
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.od->ReturnType(h.returnType.get());
    return h;
}

// Build an `OperatorDeclaration` with a `ReturnType` `int`, a `PrivateImplementationType` `IFoo`,
// a `Parameters` collection holding one `ParameterDeclaration` "int x", and a `Body`
// `BlockStatement` `{ return 5; }` -- every slot filled. The `OperatorType` defaults to
// `LogicalNot`.
OperatorHolder make_OperatorDeclarationFull() {
    OperatorHolder h;
    h.od = std::make_unique<OperatorDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.od->ReturnType(h.returnType.get());
    h.privateImplType = std::make_unique<SimpleType>(std::string("IFoo"));
    h.od->PrivateImplementationType(h.privateImplType.get());
    // One parameter "int x"
    h.param0 = std::make_unique<ParameterDeclaration>();
    h.param0Type = std::make_unique<SimpleType>(std::string("int"));
    h.param0->Type(h.param0Type.get());
    h.param0NameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("x")));
    h.param0->NameToken(h.param0NameToken.get());
    h.od->Parameters().Add(h.param0.get());
    // Body { return 5; }
    h.bodyReturnExpr = std::make_unique<PrimitiveExpression>(int32_t(5));
    h.bodyReturn = std::make_unique<ReturnStatement>(h.bodyReturnExpr.get());
    h.body = std::make_unique<BlockStatement>();
    h.body->Statements().Add(h.bodyReturn.get());
    h.od->Body(h.body.get());
    return h;
}

} // namespace

// ==========================================================================
// OperatorDeclaration (the operator_declaration node)
// ==========================================================================

// ---- is-a + final -------------------------------------------------------

// `OperatorDeclaration` is `final` (the C# `sealed`; `hasPatternPlaceholder` default false).
TEST(CSharp_OperatorDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<OperatorDeclaration>);
}

// `OperatorDeclaration` derives from `EntityDeclaration` (the `TypeMember` base), which derives
// from `AstNode`; it is NOT a `Statement`/`Expression`/`AstType`.
TEST(CSharp_OperatorDeclaration, IsEntityDeclarationAndAstNode) {
    OperatorDeclaration od;
    EXPECT_NE(dynamic_cast<EntityDeclaration*>(&od), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&od), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&od), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&od), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&od), nullptr);
}

// ---- The const keyword tokens -------------------------------------------

TEST(CSharp_OperatorDeclaration, ConstKeywordTokens) {
    EXPECT_STREQ(OperatorDeclaration::OperatorKeyword, "operator");
    EXPECT_STREQ(OperatorDeclaration::CheckedKeyword, "checked");
    EXPECT_STREQ(OperatorDeclaration::ExplicitKeyword, "explicit");
    EXPECT_STREQ(OperatorDeclaration::ImplicitKeyword, "implicit");
}

// ---- Construction --------------------------------------------------------

// The empty ctor: no `ReturnType`, empty `Attributes`/`Parameters`, both nullable slots null.
// `GetChildCount` is `0 + 0 + 3 = 3` (the two empty collections plus the three single slots --
// each single slot contributes 1 to the flattened count even when absent). The `Modifiers`
// defaults to `None`; `OperatorType` defaults to `LogicalNot` (the enum's zero value);
// `SymbolKind` is `Operator`.
TEST(CSharp_OperatorDeclaration, EmptyCtor) {
    OperatorDeclaration od;
    EXPECT_EQ(od.SymbolKind(), SymbolKind::Operator);
    EXPECT_EQ(od.Modifiers(), Modifiers::None);
    EXPECT_EQ(od.OperatorType(), OperatorType::LogicalNot);
    EXPECT_EQ(od.ReturnType(), nullptr);
    EXPECT_EQ(od.NameToken(), nullptr);
    EXPECT_EQ(od.PrivateImplementationType(), nullptr);
    EXPECT_EQ(od.Parameters().Count(), 0);
    EXPECT_EQ(od.Body(), nullptr);
    EXPECT_EQ(od.Attributes().Count(), 0);
    EXPECT_EQ(od.GetChildCount(), 3);  // 0 attrs + 0 params + 3 singles
}

// ---- The SymbolKind override ---------------------------------------------

TEST(CSharp_OperatorDeclaration, SymbolKindOverrideReturnsOperator) {
    OperatorDeclaration od;
    EXPECT_EQ(od.SymbolKind(), SymbolKind::Operator);
}

// ---- The Modifiers scalar (inherited from EntityDeclaration) -------------

TEST(CSharp_OperatorDeclaration, ModifiersDefaultsToNoneAndRoundTrips) {
    OperatorDeclaration od;
    EXPECT_EQ(od.Modifiers(), Modifiers::None);
    od.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(od.Modifiers(), Modifiers::Static | Modifiers::Public);
}

TEST(CSharp_OperatorDeclaration, HasModifierIsBitmaskTest) {
    OperatorDeclaration od;
    od.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(od.HasModifier(Modifiers::Static));
    EXPECT_TRUE(od.HasModifier(Modifiers::Public));
    EXPECT_FALSE(od.HasModifier(Modifiers::Virtual));
}

// ---- The OperatorType scalar (a settable enum, NOT a [Slot]) --------------

// `OperatorType` defaults to `LogicalNot` (the enum's zero value) and round-trips. It has NO
// `Any` member, so the generated `DoMatch` term is the plain `==` (not a wildcard).
TEST(CSharp_OperatorDeclaration, OperatorTypeDefaultsToLogicalNotAndRoundTrips) {
    OperatorDeclaration od;
    EXPECT_EQ(od.OperatorType(), OperatorType::LogicalNot);
    od.OperatorType(OperatorType::Addition);
    EXPECT_EQ(od.OperatorType(), OperatorType::Addition);
    od.OperatorType(OperatorType::CheckedExplicit);
    EXPECT_EQ(od.OperatorType(), OperatorType::CheckedExplicit);
}

// The 35 `OperatorType` values are distinct and ordered.
TEST(CSharp_OperatorDeclaration, OperatorTypeValuesAreDistinctAndOrdered) {
    EXPECT_EQ(static_cast<int>(OperatorType::LogicalNot), 0);
    EXPECT_EQ(static_cast<int>(OperatorType::CheckedExplicit), 34);
    EXPECT_NE(OperatorType::Addition, OperatorType::Subtraction);
    EXPECT_NE(OperatorType::Implicit, OperatorType::Explicit);
}

// ---- The GetName static helper (the operator method-name lookup) ---------

// `GetName` returns the method name for each `OperatorType` value (the C# `names` table's
// method-name column). The expected names parallel the `kMethodNames` array in the port; this
// catches any ordering mistake.
TEST(CSharp_OperatorDeclaration, GetNameReturnsMethodNameForEachOperatorType) {
    static constexpr const char* const kExpected[] = {
        "op_LogicalNot", "op_OnesComplement", "op_Increment", "op_CheckedIncrement",
        "op_Decrement", "op_CheckedDecrement", "op_True", "op_False",
        "op_UnaryPlus", "op_UnaryNegation", "op_CheckedUnaryNegation",
        "op_Addition", "op_CheckedAddition", "op_Subtraction", "op_CheckedSubtraction",
        "op_Multiply", "op_CheckedMultiply", "op_Division", "op_CheckedDivision",
        "op_Modulus", "op_BitwiseAnd", "op_BitwiseOr", "op_ExclusiveOr",
        "op_LeftShift", "op_RightShift", "op_UnsignedRightShift",
        "op_Equality", "op_Inequality", "op_GreaterThan", "op_LessThan",
        "op_GreaterThanOrEqual", "op_LessThanOrEqual",
        "op_Implicit", "op_Explicit", "op_CheckedExplicit"
    };
    constexpr int kCount = static_cast<int>(sizeof(kExpected) / sizeof(kExpected[0]));
    ASSERT_EQ(kCount, 35);
    for (int i = 0; i < kCount; i++) {
        auto name = OperatorDeclaration::GetName(static_cast<OperatorType>(i));
        ASSERT_TRUE(name.has_value()) << "i=" << i;
        EXPECT_EQ(*name, std::string(kExpected[i])) << "i=" << i;
    }
}

// `GetName` returns `nullopt` for a null `OperatorType?` input (the C# returns null for a null
// type).
TEST(CSharp_OperatorDeclaration, GetNameReturnsNulloptForNullInput) {
    EXPECT_FALSE(OperatorDeclaration::GetName(std::optional<OperatorType>()).has_value());
}

// ---- The Name/NameToken overrides (an operator has no name token) ---------

// `Name` returns the method name for the default `OperatorType` (`LogicalNot` ->
// `"op_LogicalNot"`).
TEST(CSharp_OperatorDeclaration, NameReturnsMethodNameForDefaultOperatorType) {
    OperatorDeclaration od;
    EXPECT_EQ(od.OperatorType(), OperatorType::LogicalNot);
    EXPECT_EQ(od.Name(), "op_LogicalNot");
}

// `Name` reflects the `OperatorType` (the override returns `GetName(this.OperatorType)`).
TEST(CSharp_OperatorDeclaration, NameReflectsOperatorType) {
    OperatorDeclaration od;
    od.OperatorType(OperatorType::Addition);
    EXPECT_EQ(od.Name(), "op_Addition");
    od.OperatorType(OperatorType::Implicit);
    EXPECT_EQ(od.Name(), "op_Implicit");
    od.OperatorType(OperatorType::CheckedExplicit);
    EXPECT_EQ(od.Name(), "op_CheckedExplicit");
}

// `Name` setter throws (`NotSupportedException` ports as `std::logic_error`).
TEST(CSharp_OperatorDeclaration, NameSetterThrows) {
    OperatorDeclaration od;
    EXPECT_THROW(od.Name(std::string_view("x")), std::logic_error);
}

// `NameToken` returns null (an operator has no name token).
TEST(CSharp_OperatorDeclaration, NameTokenReturnsNull) {
    OperatorDeclaration od;
    EXPECT_EQ(od.NameToken(), nullptr);
}

// `NameToken` setter throws.
TEST(CSharp_OperatorDeclaration, NameTokenSetterThrows) {
    OperatorDeclaration od;
    EXPECT_THROW(od.NameToken(nullptr), std::logic_error);
}

// ---- The ReturnType slot (a single REQUIRED AstType, index-less) ----------

TEST(CSharp_OperatorDeclaration, ReturnTypeSetterReparentsAndReindexes) {
    OperatorDeclaration od;
    auto type = std::make_unique<SimpleType>(std::string("int"));
    od.ReturnType(type.get());
    EXPECT_EQ(od.ReturnType(), type.get());
    EXPECT_EQ(type->Parent(), &od);
    (void)type->Slot();  // trigger the lazy reindex
    EXPECT_EQ(type->ChildIndex, 0);  // attrCount (0) -- ReturnType at flattened index 0
}

TEST(CSharp_OperatorDeclaration, ReturnTypeSetterDetachesAndClears) {
    OperatorDeclaration od;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    od.ReturnType(a.get());
    EXPECT_EQ(a->Parent(), &od);
    od.ReturnType(b.get());
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(od.ReturnType(), b.get());
    od.ReturnType(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(od.ReturnType(), nullptr);
}

// ---- The nullable slots (PrivateImplementationType/Body) ------------------

TEST(CSharp_OperatorDeclaration, PrivateImplementationTypeSetterReparentsAndDetaches) {
    OperatorDeclaration od;
    auto a = std::make_unique<SimpleType>(std::string("IFoo"));
    od.PrivateImplementationType(a.get());
    EXPECT_EQ(od.PrivateImplementationType(), a.get());
    EXPECT_EQ(a->Parent(), &od);
    od.PrivateImplementationType(nullptr);
    EXPECT_EQ(a->Parent(), nullptr);
    EXPECT_EQ(od.PrivateImplementationType(), nullptr);
}

TEST(CSharp_OperatorDeclaration, BodySetterReparentsAndDetaches) {
    OperatorDeclaration od;
    auto b = std::make_unique<BlockStatement>();
    od.Body(b.get());
    EXPECT_EQ(od.Body(), b.get());
    EXPECT_EQ(b->Parent(), &od);
    od.Body(nullptr);
    EXPECT_EQ(b->Parent(), nullptr);
    EXPECT_EQ(od.Body(), nullptr);
}

// ---- The Parameters collection (non-incremental) ------------------------

TEST(CSharp_OperatorDeclaration, ParametersCollectionAddReparents) {
    OperatorDeclaration od;
    auto p = std::make_unique<ParameterDeclaration>();
    od.Parameters().Add(p.get());
    EXPECT_EQ(od.Parameters().Count(), 1);
    EXPECT_EQ(p->Parent(), &od);
}

TEST(CSharp_OperatorDeclaration, ParametersCollectionAddReindexesDynamically) {
    OperatorDeclaration od;
    auto p0 = std::make_unique<ParameterDeclaration>();
    auto p1 = std::make_unique<ParameterDeclaration>();
    od.Parameters().Add(p0.get());
    od.Parameters().Add(p1.get());
    // The collection is non-incremental (the node has two collections), so the flattened
    // `ChildIndex` is dynamic -- trigger the reindex via `Slot()`.
    (void)p0->Slot();
    EXPECT_EQ(p0->ChildIndex, 2);  // attrCount (0) + 2 singles (ReturnType, PrivateImpl) + 0
    EXPECT_EQ(p1->ChildIndex, 3);
}

// ---- The Attributes collection (non-incremental) ------------------------

TEST(CSharp_OperatorDeclaration, AttributesCollectionAddReparents) {
    OperatorDeclaration od;
    auto attrSec = std::make_unique<AttributeSection>();
    od.Attributes().Add(attrSec.get());
    EXPECT_EQ(od.Attributes().Count(), 1);
    EXPECT_EQ(attrSec->Parent(), &od);
}

// ---- Slot storage (the generated overrides) ------------------------------

// `GetChild` walks the slots: the `Attributes` collection `[0, attrCount)`, the `ReturnType`
// single at `attrCount`, the `PrivateImplementationType` single at `attrCount + 1`, the
// `Parameters` collection `[attrCount + 2, attrCount + 2 + paramCount)`, the `Body` single at
// `attrCount + 2 + paramCount`.
TEST(CSharp_OperatorDeclaration, GetChildWalksSlots) {
    auto od = make_OperatorDeclarationFull();
    // 0 attrs + 1 param + 3 singles
    EXPECT_EQ(od->GetChildCount(), 4);
    EXPECT_EQ(od->GetChild(0), od->ReturnType());               // 0
    EXPECT_EQ(od->GetChild(1), od->PrivateImplementationType());  // 1
    EXPECT_EQ(od->GetChild(2), od->Parameters().At(0));         // 2 (the param)
    EXPECT_EQ(od->GetChild(3), od->Body());                    // 3
    EXPECT_THROW(od->GetChild(4), std::out_of_range);
}

TEST(CSharp_OperatorDeclaration, GetChildSlotInfoWalksSlots) {
    auto od = make_OperatorDeclarationFull();
    EXPECT_EQ(od->GetChildSlotInfo(0), &od->ReturnTypeSlot);
    EXPECT_EQ(od->GetChildSlotInfo(1), &od->PrivateImplementationTypeSlot);
    EXPECT_EQ(od->GetChildSlotInfo(2), &od->ParametersSlot);
    EXPECT_EQ(od->GetChildSlotInfo(3), &od->BodySlot);
    EXPECT_THROW(od->GetChildSlotInfo(4), std::out_of_range);
}

TEST(CSharp_OperatorDeclaration, GetCollectionByKindReturnsAttributesAndParameters) {
    OperatorDeclaration od;
    EXPECT_EQ(od.GetCollectionByKind(&Slots::AttributeSection), &od.Attributes());
    EXPECT_EQ(od.GetCollectionByKind(&Slots::Parameter), &od.Parameters());
    EXPECT_EQ(od.GetCollectionByKind(&Slots::Type), nullptr);
    EXPECT_EQ(od.GetCollectionByKind(&Slots::Identifier), nullptr);
}

// `SetChild` replaces the `ReturnType` in place (the slot must already exist at the flattened
// index).
TEST(CSharp_OperatorDeclaration, SetChildReplacesReturnType) {
    auto od = make_OperatorDeclaration();
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    auto* oldType = od->ReturnType();
    od->SetChild(0, type2.get());  // ReturnType at flattened index 0 (0 attrs)
    EXPECT_EQ(od->ReturnType(), type2.get());
    EXPECT_EQ(oldType->Parent(), nullptr);
}

// `SetChild` replaces a `Parameters` element in place.
TEST(CSharp_OperatorDeclaration, SetChildReplacesParameter) {
    auto od = make_OperatorDeclarationFull();
    auto p2 = std::make_unique<ParameterDeclaration>();
    auto* oldParam = od->Parameters().At(0);
    od->SetChild(2, p2.get());  // Parameters[0] at flattened index 2 (0 attrs + 2 singles)
    EXPECT_EQ(od->Parameters().At(0), p2.get());
    EXPECT_EQ(oldParam->Parent(), nullptr);
}

// ---- The per-node slot statics (pointing at the shared Slots kinds) ------

TEST(CSharp_OperatorDeclaration, SlotStaticsPointAtSharedKinds) {
    OperatorDeclaration od;
    EXPECT_EQ(od.AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(od.ReturnTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(od.PrivateImplementationTypeSlot.Kind(), &Slots::PrivateImplementationType);
    EXPECT_EQ(od.ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(od.BodySlot.Kind(), &Slots::Body);
}

// The slot statics are distinct (cast to the common `CSharpSlotInfo*` base for the
// cross-element-type `EXPECT_NE`, the D251/D252 precedent).
TEST(CSharp_OperatorDeclaration, SlotStaticsAreDistinct) {
    OperatorDeclaration od;
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&od.AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&od.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&od.ReturnTypeSlot),
              static_cast<const CSharpSlotInfo*>(&od.PrivateImplementationTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&od.ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&od.ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&od.BodySlot),
              static_cast<const CSharpSlotInfo*>(&od.ReturnTypeSlot));
}

// ---- AcceptVisitor dispatch + virtuality ---------------------------------

TEST(CSharp_OperatorDeclaration, AcceptVisitorDispatches) {
    OperatorDeclaration od;
    RecordingVisitor v;
    od.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"operator"}));
}

TEST(CSharp_OperatorDeclaration, AcceptVisitorVirtualThroughBase) {
    OperatorDeclaration od;
    AstNode* node = &od;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"operator"}));
}

TEST(CSharp_OperatorDeclaration, AcceptVisitorVirtualThroughEntityDeclaration) {
    OperatorDeclaration od;
    EntityDeclaration* node = &od;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"operator"}));
}

// The depth-first walk recurses into the `ReturnType` (a `SimpleType` `int`) in document order (no
// attributes, no `PrivateImplementationType`, no `Parameters`, no `Body`). An operator has no
// `NameToken`, so the walk does NOT recurse into one (unlike `PropertyDeclaration`).
TEST(CSharp_OperatorDeclaration, DepthFirstWalk) {
    auto od = make_OperatorDeclaration();
    RecordingVisitor v;
    od->AcceptVisitor(v);
    // operator -> simple:int (ReturnType) -> id:int
    EXPECT_EQ(v.trace, std::vector<std::string>({
        "operator", "simple:int", "id:int",
    }));
}

// An `OperatorDeclaration` with an attribute recurses into the `AttributeSection` -> `Attribute`
// -> its `SimpleType` `Type` -> its `Identifier` BEFORE the `ReturnType` (the `Attributes`
// collection is the first slot).
TEST(CSharp_OperatorDeclaration, DepthFirstWalkWithAttribute) {
    auto od = make_OperatorDeclarationWithAttribute();
    RecordingVisitor v;
    od->AcceptVisitor(v);
    // operator -> attrsec (Attributes[0]) -> attr -> simple:Foo -> id:Foo
    //      -> simple:int (ReturnType) -> id:int
    ASSERT_EQ(v.trace.size(), 7u);
    EXPECT_EQ(v.trace[0], "operator");
    EXPECT_EQ(v.trace[1], "attrsec:");
    EXPECT_EQ(v.trace[2], "attr");
    EXPECT_EQ(v.trace[3], "simple:Foo");
    EXPECT_EQ(v.trace[4], "id:Foo");
    EXPECT_EQ(v.trace[5], "simple:int");
    EXPECT_EQ(v.trace[6], "id:int");
}

// An `OperatorDeclaration` with every slot filled recurses into the `ReturnType`, the
// `PrivateImplementationType`, each `Parameter` (and its `Type`/`NameToken`), and the `Body`'s
// statements, in slot order.
TEST(CSharp_OperatorDeclaration, DepthFirstWalkFull) {
    auto od = make_OperatorDeclarationFull();
    RecordingVisitor v;
    od->AcceptVisitor(v);
    // operator -> simple:int (ReturnType) -> id:int
    //      -> simple:IFoo (PrivateImplementationType) -> id:IFoo
    //      -> param (Parameters[0]) -> simple:int (param's Type) -> id:int -> id:x (NameToken)
    //      -> block (Body) -> return -> prim (return 5)
    ASSERT_EQ(v.trace.size(), 12u);
    EXPECT_EQ(v.trace[0], "operator");
    EXPECT_EQ(v.trace[1], "simple:int");
    EXPECT_EQ(v.trace[2], "id:int");
    EXPECT_EQ(v.trace[3], "simple:IFoo");
    EXPECT_EQ(v.trace[4], "id:IFoo");
    EXPECT_EQ(v.trace[5], "param");
    EXPECT_EQ(v.trace[6], "simple:int");
    EXPECT_EQ(v.trace[7], "id:int");
    EXPECT_EQ(v.trace[8], "id:x");
    EXPECT_EQ(v.trace[9], "block");
    EXPECT_EQ(v.trace[10], "return");
    EXPECT_EQ(v.trace[11], "prim");
}

// ---- DoMatch (the generated pattern match) --------------------------------

TEST(CSharp_OperatorDeclaration, DoMatchSameNode) {
    auto a = make_OperatorDeclaration();
    auto b = make_OperatorDeclaration();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Modifiers` mismatch rejects (the `MatchAttributesAndModifiers` `Any`-wildcard is NOT a
// bitmask).
TEST(CSharp_OperatorDeclaration, DoMatchModifiersMismatchRejects) {
    auto a = make_OperatorDeclaration();
    auto b = make_OperatorDeclaration();
    a->Modifiers(Modifiers::Static);
    b->Modifiers(Modifiers::Public);
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// `Modifiers::Any` matches any candidate (the wildcard).
TEST(CSharp_OperatorDeclaration, DoMatchModifiersAnyWildcard) {
    auto a = make_OperatorDeclaration();
    auto b = make_OperatorDeclaration();
    a->Modifiers(Modifiers::Any);
    b->Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// The `Name` term is a `MatchString` over the operator's method name (the override returns
// `GetName(this.OperatorType)`), so two operators with DIFFERENT `OperatorType` reject on the
// name mismatch -- the `IndexerDeclaration` D279 fixed-`Name` precedent generalized to a `Name`
// that depends on the scalar. The `Name` term is the FIRST term, so it rejects before the later
// `OperatorType ==` term is even reached.
TEST(CSharp_OperatorDeclaration, DoMatchNameMismatchViaDifferentOperatorTypeRejects) {
    auto a = make_OperatorDeclaration();
    auto b = make_OperatorDeclaration();
    a->OperatorType(OperatorType::Addition);      // Name "op_Addition"
    b->OperatorType(OperatorType::Subtraction);  // Name "op_Subtraction"
    EXPECT_NE(a->Name(), b->Name());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Two operators with the SAME `OperatorType` match on both the `Name` and `OperatorType` terms.
TEST(CSharp_OperatorDeclaration, DoMatchSameOperatorTypeMatchesOnNameAndOperatorType) {
    auto a = make_OperatorDeclaration();
    auto b = make_OperatorDeclaration();
    a->OperatorType(OperatorType::Implicit);
    b->OperatorType(OperatorType::Implicit);
    EXPECT_EQ(a->Name(), b->Name());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `ReturnType` mismatch rejects (the `MatchOptional` `ReturnType` term).
TEST(CSharp_OperatorDeclaration, DoMatchReturnTypeMismatchRejects) {
    auto a = make_OperatorDeclaration();  // ReturnType int
    OperatorHolder b;
    b.od = std::make_unique<OperatorDeclaration>();
    b.returnType = std::make_unique<SimpleType>(std::string("byte"));  // different type
    b.od->ReturnType(b.returnType.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `PrivateImplementationType` asymmetry rejects: a pattern with one vs a candidate without.
TEST(CSharp_OperatorDeclaration, DoMatchPrivateImplementationTypeAsymmetryRejects) {
    auto a = make_OperatorDeclaration();
    auto pit = std::make_unique<SimpleType>(std::string("IFoo"));
    a->PrivateImplementationType(pit.get());  // a has a PrivateImplementationType
    auto b = make_OperatorDeclaration();  // no PrivateImplementationType
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Parameters` mismatch rejects (the collection recursive `DoMatch` -- a pattern with a
// parameter vs a candidate without one). Both sides share the same `OperatorType` (the default
// `LogicalNot`) so the `Name` term matches and the `Parameters` term is the one that rejects.
TEST(CSharp_OperatorDeclaration, DoMatchParametersMismatchRejects) {
    auto a = make_OperatorDeclarationFull();  // one parameter
    auto b = make_OperatorDeclaration();  // no parameters
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Both sides with the same single parameter match (the collection recursive `DoMatch` accepts
// equal-length collections whose elements match).
TEST(CSharp_OperatorDeclaration, DoMatchSameParametersMatch) {
    auto a = make_OperatorDeclaration();
    auto ap = std::make_unique<ParameterDeclaration>();
    auto apt = std::make_unique<SimpleType>(std::string("int"));
    ap->Type(apt.get());
    a->Parameters().Add(ap.get());
    auto b = make_OperatorDeclaration();
    auto bp = std::make_unique<ParameterDeclaration>();
    auto bpt = std::make_unique<SimpleType>(std::string("int"));
    bp->Type(bpt.get());
    b->Parameters().Add(bp.get());
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Body` asymmetry rejects: a pattern with a `Body` vs a candidate without one.
TEST(CSharp_OperatorDeclaration, DoMatchBodyAsymmetryRejects) {
    auto a = make_OperatorDeclaration();
    auto body = std::make_unique<BlockStatement>();
    a->Body(body.get());
    auto b = make_OperatorDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`OperatorDeclaration` `EntityDeclaration` candidate rejects (the type-check gate -- a
// `DestructorDeclaration` is an `EntityDeclaration` but not an `OperatorDeclaration`).
TEST(CSharp_OperatorDeclaration, DoMatchRejectsNonOperatorEntityDeclaration) {
    auto a = make_OperatorDeclaration();
    auto b = std::make_unique<DestructorDeclaration>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A non-`EntityDeclaration` candidate rejects (a `WhileStatement` is a `Statement`, not an
// `EntityDeclaration`).
TEST(CSharp_OperatorDeclaration, DoMatchRejectsNonEntityDeclaration) {
    auto a = make_OperatorDeclaration();
    auto b = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A null candidate rejects.
TEST(CSharp_OperatorDeclaration, DoMatchRejectsNull) {
    auto a = make_OperatorDeclaration();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `ReturnType`/`PrivateImplementationType`/`Parameters`/`Body`, copies
// the `Modifiers` and `OperatorType` scalars, and does not detach the source.
TEST(CSharp_OperatorDeclaration, CloneDeepCopies) {
    auto a = make_OperatorDeclarationFull();
    a->Modifiers(Modifiers::Static | Modifiers::Public);
    a->OperatorType(OperatorType::Addition);
    auto clone = std::unique_ptr<OperatorDeclaration>(
        static_cast<OperatorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->SymbolKind(), SymbolKind::Operator);
    EXPECT_EQ(clone->Modifiers(), Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(clone->OperatorType(), OperatorType::Addition);
    EXPECT_EQ(clone->Name(), "op_Addition");
    EXPECT_EQ(clone->NameToken(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
    EXPECT_NE(clone->ReturnType(), a->ReturnType());
    ASSERT_NE(clone->PrivateImplementationType(), nullptr);
    EXPECT_NE(clone->PrivateImplementationType(), a->PrivateImplementationType());
    ASSERT_EQ(clone->Parameters().Count(), 1u);
    EXPECT_NE(clone->Parameters().At(0), a->Parameters().At(0));
    ASSERT_NE(clone->Body(), nullptr);
    EXPECT_NE(clone->Body(), a->Body());
    // The source is not detached.
    EXPECT_EQ(a->ReturnType()->Parent(), a.get());
    EXPECT_EQ(a->Body()->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` and covariant through `OperatorDeclaration*`.
TEST(CSharp_OperatorDeclaration, CloneVirtualAndCovariant) {
    auto a = make_OperatorDeclaration();
    AstNode* node = a.get();
    auto clone = std::unique_ptr<AstNode>(node->Clone());
    EXPECT_NE(dynamic_cast<OperatorDeclaration*>(clone.get()), nullptr);
    auto cov = std::unique_ptr<OperatorDeclaration>(a->Clone());
    EXPECT_NE(cov, nullptr);
}

// `Clone` copies the `Attributes` collection (each element deep-cloned).
TEST(CSharp_OperatorDeclaration, CloneCopiesAttributes) {
    auto a = make_OperatorDeclarationWithAttribute();
    auto clone = std::unique_ptr<OperatorDeclaration>(
        static_cast<OperatorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->Attributes().Count(), 1u);
    EXPECT_NE(clone->Attributes().At(0), a->Attributes().At(0));
    EXPECT_EQ(clone->ReturnType()->Parent(), clone.get());
}

// `Clone` skips absent nullable slots (an operator with no `PrivateImplementationType`/
// `Parameters`/`Body` clones without them).
TEST(CSharp_OperatorDeclaration, CloneSkipsAbsentNullableSlots) {
    auto a = make_OperatorDeclaration();  // no PrivateImpl/Parameters/Body
    auto clone = std::unique_ptr<OperatorDeclaration>(
        static_cast<OperatorDeclaration*>(a->Clone()));
    EXPECT_EQ(clone->PrivateImplementationType(), nullptr);
    EXPECT_EQ(clone->Parameters().Count(), 0u);
    EXPECT_EQ(clone->Body(), nullptr);
    ASSERT_NE(clone->ReturnType(), nullptr);
}

// `Clone` copies the `Parameters` collection (each element deep-cloned).
TEST(CSharp_OperatorDeclaration, CloneCopiesParameters) {
    auto a = make_OperatorDeclarationFull();
    auto clone = std::unique_ptr<OperatorDeclaration>(
        static_cast<OperatorDeclaration*>(a->Clone()));
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
TEST(CSharp_OperatorDeclaration, CheckInvariantPassesOnFilled) {
    auto od = make_OperatorDeclaration();  // ReturnType filled
    od->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` passes on the full node (every slot filled).
TEST(CSharp_OperatorDeclaration, CheckInvariantPassesOnFull) {
    auto od = make_OperatorDeclarationFull();
    od->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `ReturnType` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_OperatorDeclaration, CheckInvariantRejectsEmpty) {
    OperatorDeclaration od;  // no ReturnType
    EXPECT_DEATH(od.CheckInvariant(), "");
}
#endif
