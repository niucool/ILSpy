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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the `VariableDeclarationStatement` concrete node
// (cpp/.../Syntax/Statements/VariableDeclarationStatement.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.cs) -- the next
// in-order Phase-5 piece per the D269 plan (now unblocked -- it needs the `VariableInitializer`
// already ported by D266 for its `Variables` collection, plus a `Modifiers` `[Flags]` enum scalar).
// The `local_variable_declaration ::= type variable_initializer+` (C# grammar 13.6.2.1): a sealed
// `Statement` with a single REQUIRED `AstType Type` `[Slot]` at flattened index 0, a `Variables
// AstNodeCollection<VariableInitializer>` collection `[Slot("Variable")]` at slot 1 (INCREMENTAL --
// the node's only collection and its last slot), and a `Modifiers` scalar (a plain settable
// `Modifiers`-typed property, NOT a `[Slot]` -- the first ported `[Flags]` enum scalar). The suite
// shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `VisitVariableDeclarationStatement` under test
// (plus the nodes its `Type`/`Variables` slots hold -- the `SimpleType`/`Identifier` of its `Type`,
// the `VariableInitializer`/`Identifier` of its `Variables` collection, the `PrimitiveExpression`
// of a variable's initializer -- and the structural-twin `FixedStatement` and `WhileStatement`
// for the cross-structural-twin DoMatch rejection), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitVariableDeclarationStatement(VariableDeclarationStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-vds>"); return; }
        trace.push_back("vds");
        VisitChildren(node);
    }
    void VisitFixedStatement(FixedStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-fixed>"); return; }
        trace.push_back("fixed");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
    void VisitBreakStatement(BreakStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-break>"); return; }
        trace.push_back("break");
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
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ==========================================================================
// VariableDeclarationStatement (the local variable declaration)
// ==========================================================================

// ---- is-a ----------------------------------------------------------------

// `VariableDeclarationStatement` derives from `Statement`; it is disjoint from `Expression` (the
// D254 disjoint-hierarchy discriminator: a variable declaration is a `Statement` and an `AstNode`
// but NOT an `Expression`).
TEST(CSharp_VariableDeclarationStatement, IsStatementAndAstNodeNotExpression) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(type.get(), std::string("x"));
    EXPECT_NE(dynamic_cast<Statement*>(&vds), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&vds), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&vds), nullptr);
}

// `VariableDeclarationStatement` is `final` (the C# `sealed`).
TEST(CSharp_VariableDeclarationStatement, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<VariableDeclarationStatement>);
}

// ---- Construction --------------------------------------------------------

// The empty ctor has no type, no variables, and `Modifiers::None` (the enum's zero value -- a
// declaration with no modifiers). `GetChildCount` is 1 (the empty `Type` single slot) + 0
// variables.
TEST(CSharp_VariableDeclarationStatement, EmptyCtorHasNoTypeOrVariablesAndNoneModifiers) {
    VariableDeclarationStatement vds;
    EXPECT_EQ(vds.Type(), nullptr);
    EXPECT_EQ(vds.Variables().Count(), 0);
    EXPECT_EQ(vds.Modifiers(), Modifiers::None);
    EXPECT_EQ(vds.GetChildCount(), 1);  // the single Type slot (empty) + 0 variables
    EXPECT_EQ(vds.StartLocation(), TextLocation::Empty);
}

// The `(Modifiers, AstType)` generated required-prefix ctor sets both the `Modifiers` and the
// `Type` (parented at index 0).
TEST(CSharp_VariableDeclarationStatement, ModifiersAstTypeCtorSetsModifiersAndType) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(Modifiers::Const, type.get());
    EXPECT_EQ(vds.Modifiers(), Modifiers::Const);
    EXPECT_EQ(vds.Type(), type.get());
    EXPECT_EQ(type->Parent(), &vds);
    EXPECT_EQ(type->ChildIndex, 0);
}

// The hand-written `(AstType, string, Expression*)` convenience ctor sets the `Type` and adds a
// single `VariableInitializer` (created from the name + initializer) to the `Variables`
// collection.
TEST(CSharp_VariableDeclarationStatement, ConvenienceCtorSetsTypeAndAddsVariable) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto init = std::make_unique<PrimitiveExpression>(int32_t(42));
    VariableDeclarationStatement vds(type.get(), std::string("x"), init.get());
    EXPECT_EQ(vds.Type(), type.get());
    EXPECT_EQ(vds.Variables().Count(), 1);
    EXPECT_EQ(vds.Modifiers(), Modifiers::None);  // the convenience ctor does not set modifiers
    auto* vi = vds.Variables().At(0);
    ASSERT_NE(vi, nullptr);
    EXPECT_EQ(vi->Name(), "x");
    EXPECT_EQ(vi->Initializer(), init.get());
    EXPECT_EQ(vi->Parent(), &vds);
    EXPECT_EQ(type->Parent(), &vds);
    EXPECT_EQ(type->ChildIndex, 0);
}

// The 2-arg convenience-ctor form (no initializer) defaults the initializer to nullptr (the bare
// `type name;` form -- the C# `Expression? initializer = null` default).
TEST(CSharp_VariableDeclarationStatement, ConvenienceCtorDefaultInitializerIsNull) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(type.get(), std::string("x"));
    EXPECT_EQ(vds.Variables().Count(), 1);
    auto* vi = vds.Variables().At(0);
    ASSERT_NE(vi, nullptr);
    EXPECT_EQ(vi->Name(), "x");
    EXPECT_EQ(vi->Initializer(), nullptr);  // no initializer
}

// ---- The `Modifiers` scalar (a `[Flags]` enum, NOT a `[Slot]`) ------------

// The `Modifiers` setter stores the value; the getter returns it. A combined mask
// (`Static | Public`) round-trips (the `[Flags]` bitwise operators are defined on the enum).
TEST(CSharp_VariableDeclarationStatement, ModifiersSetterStoresValue) {
    VariableDeclarationStatement vds;
    vds.Modifiers(Modifiers::Static | Modifiers::Public);
    EXPECT_EQ(vds.Modifiers(), Modifiers::Static | Modifiers::Public);
}

// The `Modifiers` default is `None` (the enum's zero value).
TEST(CSharp_VariableDeclarationStatement, ModifiersDefaultsToNone) {
    VariableDeclarationStatement vds;
    EXPECT_EQ(vds.Modifiers(), Modifiers::None);
}

// ---- The `Type` slot (a single REQUIRED `AstType` child) ------------------

// The `Type` setter re-parents the new type and detaches the old one.
TEST(CSharp_VariableDeclarationStatement, TypeSetterReparentsAndDetaches) {
    VariableDeclarationStatement vds;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("byte"));
    vds.Type(a.get());
    EXPECT_EQ(a->Parent(), &vds);
    EXPECT_EQ(vds.Type(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    vds.Type(b.get());
    EXPECT_EQ(b->Parent(), &vds);
    EXPECT_EQ(vds.Type(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Type` setter clears and detaches the old type.
TEST(CSharp_VariableDeclarationStatement, TypeSetterClearsWithNull) {
    VariableDeclarationStatement vds;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    vds.Type(a.get());
    vds.Type(nullptr);
    EXPECT_EQ(vds.Type(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Variables` collection (INCREMENTAL) ---------------------------

// The collection starts empty (0 count); the node's child count is the single `Type` slot (1) +
// 0 variables.
TEST(CSharp_VariableDeclarationStatement, VariablesEmptyByDefault) {
    VariableDeclarationStatement vds;
    EXPECT_EQ(vds.Variables().Count(), 0);
    EXPECT_EQ(vds.GetChildCount(), 1);
}

// `Add` appends an element, parents it, and maintains its flattened `ChildIndex` INCREMENTALLY
// (the collection is the node's only collection and its last slot, so an element's
// `ChildIndex` is exactly `1 + its local position`, unlike `FixedStatement` whose trailing
// `EmbeddedStatement` makes its `Variables` non-incremental).
TEST(CSharp_VariableDeclarationStatement, VariablesAddAppendsAndParentsAndIsIncremental) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(Modifiers::None, type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    vds.Variables().Add(x.get());
    vds.Variables().Add(y.get());
    EXPECT_EQ(vds.Variables().Count(), 2);
    EXPECT_EQ(x->Parent(), &vds);
    EXPECT_EQ(y->Parent(), &vds);
    EXPECT_EQ(x->ChildIndex, 1);  // incremental: 1 + 0
    EXPECT_EQ(y->ChildIndex, 2);  // incremental: 1 + 1
    EXPECT_EQ(vds.GetChildCount(), 3);  // type + 2 variables
}

// `GetCollectionByKind` returns the `Variables` collection for the `Variable` kind and null for
// any other kind (the `Type` kind is a single slot).
TEST(CSharp_VariableDeclarationStatement, GetCollectionByKindReturnsVariablesForVariableKind) {
    VariableDeclarationStatement vds;
    EXPECT_NE(vds.GetCollectionByKind(&Slots::Variable), nullptr);
    EXPECT_EQ(vds.GetCollectionByKind(&Slots::Variable), &vds.Variables());
    EXPECT_EQ(vds.GetCollectionByKind(&Slots::Type), nullptr);       // a single slot
    EXPECT_EQ(vds.GetCollectionByKind(&Slots::Argument), nullptr);   // an unrelated kind
    EXPECT_EQ(vds.GetCollectionByKind(nullptr), nullptr);
}

// The `Variable` kind is the SAME `Slots::Variable` that `FixedStatement` registered (shared
// across both `Variables` collections -- the D267 `FixedStatement` precedent).
TEST(CSharp_VariableDeclarationStatement, VariablesKindSharedWithFixedStatement) {
    EXPECT_EQ(VariableDeclarationStatement::VariablesSlot.Kind(), &Slots::Variable);
    EXPECT_EQ(FixedStatement::VariablesSlot.Kind(), &Slots::Variable);
    EXPECT_EQ(VariableDeclarationStatement::VariablesSlot.Kind(),
              FixedStatement::VariablesSlot.Kind());
}

// ---- Slot storage (single -> collection, incremental) -------------------

// `GetChild` returns the `Type` at index 0 and the `Variables` elements at index 1..1+Count;
// out-of-range throws.
TEST(CSharp_VariableDeclarationStatement, GetChildDispatchesTypeThenVariables) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(Modifiers::None, type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    vds.Variables().Add(x.get());
    vds.Variables().Add(y.get());
    EXPECT_EQ(vds.GetChild(0), type.get());
    EXPECT_EQ(vds.GetChild(1), x.get());
    EXPECT_EQ(vds.GetChild(2), y.get());
    EXPECT_THROW(vds.GetChild(3), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TypeSlot` at index 0 and the `VariablesSlot` at index 1..1+Count.
TEST(CSharp_VariableDeclarationStatement, GetChildSlotInfoDispatchesTypeThenVariables) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(Modifiers::None, type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    vds.Variables().Add(x.get());
    EXPECT_EQ(vds.GetChildSlotInfo(0), &VariableDeclarationStatement::TypeSlot);
    EXPECT_EQ(vds.GetChildSlotInfo(1), &VariableDeclarationStatement::VariablesSlot);
    EXPECT_THROW(vds.GetChildSlotInfo(2), std::out_of_range);
}

// `SetChild` replaces an existing `Type` (index 0) or a `Variables` element (index 1..1+Count)
// in place; it does NOT add (the element must already exist at the flattened index).
TEST(CSharp_VariableDeclarationStatement, SetChildReplacesTypeAndVariablesInPlace) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(Modifiers::None, type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    vds.Variables().Add(x.get());
    auto newtype = std::make_unique<SimpleType>(std::string("byte"));
    vds.SetChild(0, newtype.get());  // replace the type
    EXPECT_EQ(vds.Type(), newtype.get());
    EXPECT_EQ(type->Parent(), nullptr);  // detached
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    vds.SetChild(1, y.get());  // replace the first variable
    EXPECT_EQ(vds.Variables().At(0), y.get());
    EXPECT_EQ(x->Parent(), nullptr);  // detached
}

// ---- The per-node slot statics (shared Slots kinds) ----------------------

// The `TypeSlot`'s kind is the SAME `Slots::Type` that `Attribute`/`FixedStatement` registered.
TEST(CSharp_VariableDeclarationStatement, TypeSlotKindSharedWithFixedStatement) {
    EXPECT_EQ(VariableDeclarationStatement::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(FixedStatement::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(VariableDeclarationStatement::TypeSlot.Kind(), FixedStatement::TypeSlot.Kind());
}

// ---- IsInstanceOfType is-a cross-check --------------------------------

// The `TypeSlot` accepts an `AstType` (e.g. a `SimpleType`) and rejects a `Statement` (e.g. a
// `BreakStatement`) -- the slot's declared child type is `AstType`.
TEST(CSharp_VariableDeclarationStatement, TypeSlotAcceptsAstTypeNotStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    auto body = std::make_unique<BreakStatement>();
    EXPECT_TRUE(VariableDeclarationStatement::TypeSlot.IsInstanceOfType(type.get()));
    EXPECT_FALSE(VariableDeclarationStatement::TypeSlot.IsInstanceOfType(body.get()));
}

// The `VariablesSlot` accepts a `VariableInitializer` and rejects an `Expression` (e.g. a
// `NullReferenceExpression`) and a `Statement` (e.g. a `BreakStatement`) -- the slot's declared
// child type is `VariableInitializer`.
TEST(CSharp_VariableDeclarationStatement, VariablesSlotAcceptsVariableInitializerNotExpressionNotStatement) {
    auto v = std::make_unique<VariableInitializer>(std::string("x"));
    auto expr = std::make_unique<NullReferenceExpression>();
    auto body = std::make_unique<BreakStatement>();
    EXPECT_TRUE(VariableDeclarationStatement::VariablesSlot.IsInstanceOfType(v.get()));
    EXPECT_FALSE(VariableDeclarationStatement::VariablesSlot.IsInstanceOfType(expr.get()));
    EXPECT_FALSE(VariableDeclarationStatement::VariablesSlot.IsInstanceOfType(body.get()));
}

// ---- AcceptVisitor dispatch -------------------------------------------

// `AcceptVisitor` dispatches to `VisitVariableDeclarationStatement` (the visitor-pattern
// round-trip); the depth-first walk then visits the `Type` child (a `VisitSimpleType` -> its
// `Identifier`), then the `Variables` (a `VisitVariableInitializer` per element -> each one's
// `NameToken` `Identifier` -> its `Initializer` when present).
TEST(CSharp_VariableDeclarationStatement, AcceptVisitorDispatchesToVisitVariableDeclarationStatement) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(type.get(), std::string("x"));
    RecordingVisitor v;
    vds.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"vds", "simple:int", "id:int", "varinit:x", "id:x"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / a `Statement*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_VariableDeclarationStatement, AcceptVisitorIsVirtualThroughBases) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(type.get(), std::string("x"));
    AstNode* asAst = &vds;
    Statement* asStmt = &vds;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asStmt->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"vds", "simple:int", "id:int", "varinit:x", "id:x"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"vds", "simple:int", "id:int", "varinit:x", "id:x"}));
}

// ---- Depth-first walk --------------------------------------------------

// The depth-first walk visits the `Type` (a `VisitSimpleType` -> its `Identifier`), then the
// `Variables` (a `VisitVariableInitializer` per element -> its `NameToken` `Identifier` -> its
// `Initializer`) in document order.
TEST(CSharp_VariableDeclarationStatement, DepthFirstWalkVisitsTypeVariablesInOrder) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(Modifiers::None, type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    auto init = std::make_unique<PrimitiveExpression>(int32_t(1));
    x->Initializer(init.get());
    vds.Variables().Add(x.get());
    auto y = std::make_unique<VariableInitializer>(std::string("y"));
    vds.Variables().Add(y.get());  // no initializer
    RecordingVisitor v;
    vds.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "vds", "simple:int", "id:int",
        "varinit:x", "id:x", "prim",
        "varinit:y", "id:y"}));
}

// ---- DoMatch (the generated Any-wildcard + recursive + collection match) -

// Two `VariableDeclarationStatement`s with the same modifiers, the same type, and the same
// (single) variable match.
TEST(CSharp_VariableDeclarationStatement, DoMatchMatchesSameModifiersTypeVariables) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Const, ta.get());
    VariableDeclarationStatement b(Modifiers::Const, tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A pattern with `Modifiers::Any` matches any candidate modifiers (the `Any`-wildcard term).
TEST(CSharp_VariableDeclarationStatement, DoMatchAnyModifiersMatchesAny) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());  // pattern: Any
    VariableDeclarationStatement b(Modifiers::Static, tb.get());  // candidate: Static
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A real (non-`Any`) pattern modifier matches ONLY the exact same candidate modifier (the
// `[Flags]` `==` is value equality, NOT a bitmask test -- `Const` does not match `Static`).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsDifferentModifiers) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Const, ta.get());
    VariableDeclarationStatement b(Modifiers::Static, tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // Const != Static
}

// A combined-modifier pattern matches only the exact same combined-modifier candidate (the `==`
// is value equality: `Static | Public` does not match `Static` alone, and does match
// `Static | Public`).
TEST(CSharp_VariableDeclarationStatement, DoMatchModifiersIsExactValueNotBitmask) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    auto tc = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Static | Modifiers::Public, ta.get());
    VariableDeclarationStatement b(Modifiers::Static, tb.get());  // missing Public
    VariableDeclarationStatement c(Modifiers::Static | Modifiers::Public, tc.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("x"));
    auto cx = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    c.Variables().Add(cx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // Static|Public != Static
    EXPECT_TRUE(DoMatchAgainst(&a, &c));    // Static|Public == Static|Public
}

// A different `Type` rejects the match (the `MatchRequired` on `Type` -- the second term).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsDifferentType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("byte"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());
    VariableDeclarationStatement b(Modifiers::Any, tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // type names differ ("int" vs "byte")
}

// A null pattern `Type` rejects (the `MatchRequired` guard -- the second term).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsNullPatternType) {
    VariableDeclarationStatement a;  // no type
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement b(Modifiers::Any, tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Type` rejects (the `MatchRequired` flows the null child through the child's
// `DoMatch(nullptr)`, which returns false).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsNullCandidateType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());
    VariableDeclarationStatement b;  // no type
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two declarations with the same type but different variable COUNTS do not match (the
// collection `DoMatch` rejects on a count mismatch).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsDifferentVariablesCount) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());
    VariableDeclarationStatement b(Modifiers::Any, tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    // b has no variables
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two declarations with the same type and count but a different variable NAME do not match (the
// collection `DoMatch` recurses into each `VariableInitializer`, whose `MatchString` on `Name`
// rejects on a name mismatch).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsDifferentVariablesName) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());
    VariableDeclarationStatement b(Modifiers::Any, tb.get());
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("y"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // "x" != "y"
}

// A cross-structural-twin `FixedStatement` candidate rejects (the two share the `Variable`
// collection kind but are distinct concrete types -- the `other is VariableDeclarationStatement`
// gate rejects a `FixedStatement`).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsFixedStatementCandidate) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());
    FixedStatement b(tb.get());  // a FixedStatement, not a VariableDeclarationStatement
    auto ax = std::make_unique<VariableInitializer>(std::string("x"));
    auto bx = std::make_unique<VariableInitializer>(std::string("x"));
    a.Variables().Add(ax.get());
    b.Variables().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `null` candidate rejects (the `dynamic_cast` to `VariableDeclarationStatement*` yields null).
TEST(CSharp_VariableDeclarationStatement, DoMatchRejectsNullCandidate) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement a(Modifiers::Any, ta.get());
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone (the concrete override) -------------------------------------

// `Clone` deep-copies the `Modifiers`, the `Type`, and the `Variables`, re-parents the clones,
// and does NOT detach the source.
TEST(CSharp_VariableDeclarationStatement, CloneDeepCopiesModifiersTypeVariables) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement original(Modifiers::Const, type.get());
    auto x = std::make_unique<VariableInitializer>(std::string("x"));
    original.Variables().Add(x.get());
    std::unique_ptr<VariableDeclarationStatement> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Modifiers(), Modifiers::Const);
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());              // deep-copied, not aliased
    EXPECT_EQ(copy->Type()->Parent(), copy.get());    // re-parented to the clone
    EXPECT_EQ(type->Parent(), &original);            // source NOT detached
    EXPECT_EQ(copy->Variables().Count(), 1);
    auto* cvi = copy->Variables().At(0);
    ASSERT_NE(cvi, nullptr);
    EXPECT_NE(cvi, x.get());                           // deep-copied, not aliased
    EXPECT_EQ(cvi->Name(), "x");
    EXPECT_EQ(cvi->Parent(), copy.get());             // re-parented to the clone
    EXPECT_EQ(x->Parent(), &original);                // source NOT detached
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through a `Statement*` (returns a `Statement*`, the covariant override).
TEST(CSharp_VariableDeclarationStatement, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement original(Modifiers::None, type.get());
    AstNode* asAst = &original;
    Statement* asStmt = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    std::unique_ptr<Statement> stmtCopy(asStmt->Clone());
    EXPECT_NE(dynamic_cast<VariableDeclarationStatement*>(astCopy.get()), nullptr);
    EXPECT_NE(dynamic_cast<VariableDeclarationStatement*>(stmtCopy.get()), nullptr);
}

// `Clone` of a declaration with no variables yields a clone with no variables (the empty
// collection deep-clones to an empty collection).
TEST(CSharp_VariableDeclarationStatement, CloneOfNoVariablesHasNoVariables) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement original(Modifiers::None, type.get());
    std::unique_ptr<VariableDeclarationStatement> copy(original.Clone());
    EXPECT_EQ(copy->Variables().Count(), 0);
    EXPECT_EQ(copy->Modifiers(), Modifiers::None);
}

// ---- CheckInvariant ----------------------------------------------------

// A filled `VariableDeclarationStatement` (with a `Type` and at least one variable) passes
// `CheckInvariant` (the required `Type` slot is filled and the collection is never a required
// slot).
TEST(CSharp_VariableDeclarationStatement, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    VariableDeclarationStatement vds(type.get(), std::string("x"));
    vds.CheckInvariant();  // should not assert
    SUCCEED();
}

// An empty `VariableDeclarationStatement` (no `Type`) is REJECTED by `CheckInvariant` (the
// `Type` required-slot invariant fires -- a declaration without a type is half-constructed).
TEST(CSharp_VariableDeclarationStatement, CheckInvariantRejectsEmptyNode) {
    VariableDeclarationStatement vds;  // no type
#ifndef NDEBUG
    EXPECT_DEATH(vds.CheckInvariant(), "");
#endif
}

// ---- Slot-static distinctness (the cross-element-type EXPECT_NE crux) --

// The `TypeSlot` (a `CSharpSlotInfoT<AstType>`) and the `VariablesSlot` (a
// `CSharpSlotInfoT<VariableInitializer>`) are distinct slot statics of distinct element types;
// the cross-element-type `EXPECT_NE` must cast both to the common `const CSharpSlotInfo*` base
// (the D251/D252 precedent -- `gtest`'s `CmpHelperNE` requires comparable pointer types, and the
// distinct-element-type `CSharpSlotInfoT<T>` instantiations are unrelated pointer types).
TEST(CSharp_VariableDeclarationStatement, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* typeSlot = &VariableDeclarationStatement::TypeSlot;
    const CSharpSlotInfo* varsSlot = &VariableDeclarationStatement::VariablesSlot;
    EXPECT_NE(typeSlot, varsSlot);
}
