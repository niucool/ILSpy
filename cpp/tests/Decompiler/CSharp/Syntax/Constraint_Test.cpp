// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished
// to do so, subject to the following conditions:
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

// Tests for the `Constraint` concrete node (cpp/.../Syntax/Constraint.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/Constraint.cs) -- the
// `type_parameter_constraints_clause ::= 'where' type ':' type+` node (C# grammar 15.2.5): a
// REQUIRED `SimpleType` `TypeParameter` single slot (the constrained type parameter) + a
// `BaseTypes` `AstNodeCollection<AstType>` collection (the base-type constraint list). The
// next in-order Phase-5 piece per the D282 plan (the dependency of `MethodDeclaration.Constraints`).
// The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 pattern). The
// node is structurally the `SwitchStatement` D268 shape (single required child at index 0 +
// incremental collection at index 1) with a direct-`AstNode` base, the child `SimpleType`, and the
// collection element `AstType`.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `VisitConstraint` under test (plus the
// `SimpleType`/`Identifier`/`PrimitiveType` of its `TypeParameter` and `BaseTypes`, and the
// `WhileStatement` used for the cross-type DoMatch rejection), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in
// pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitConstraint(Constraint* node) override {
        if (node == nullptr) { trace.push_back("<null-constraint>"); return; }
        trace.push_back("constraint");
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
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-pt>"); return; }
        trace.push_back("prim:" + node->Keyword());
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
        VisitChildren(node);
    }
};

// A `DoMatch` helper: invokes the pattern-matcher dispatch through the `INode` interface (the
// C# `((INode)pattern).DoMatch(candidate, new Match())` -- the port's `DoMatch(INode*, Match)`
// overload delegates to the concrete `DoMatch(AstNode*, Match)`). `Match::CreateNew()` allocates
// the capture vector (a default-constructed `Match` holds a null shared_ptr and is a FAILED
// match -- the D219 `CreateNew`-vs-default-ctor distinction; the collection `DoMatch`'s
// `CheckPoint`/`RestoreCheckPoint` deref the vector).
bool DoMatchAgainst(AstNode* pattern, AstNode* candidate) {
    return static_cast<INode*>(pattern)->DoMatch(candidate, Match::CreateNew());
}

// ---- is-a (the abstract hierarchy) --------------------------------------

// `Constraint` derives directly from `AstNode`; it is disjoint from `Expression`/`Statement`/
// `AstType`/`EntityDeclaration` (the `VariableInitializer` D266 / `CatchClause` D269 /
// `ParameterDeclaration` D278 / `TypeParameterDeclaration` D282 direct-`AstNode` precedent).
TEST(CSharp_Constraint, IsAstNodeNotExpressionNotStatementNotAstType) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    EXPECT_TRUE(dynamic_cast<AstNode*>(&c) != nullptr);
    EXPECT_FALSE(dynamic_cast<Expression*>(&c) != nullptr);
    EXPECT_FALSE(dynamic_cast<AstType*>(&c) != nullptr);
    EXPECT_FALSE(dynamic_cast<EntityDeclaration*>(&c) != nullptr);
}

// `Constraint` is `final` (the C# `sealed`).
TEST(CSharp_Constraint, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<Constraint>);
    EXPECT_FALSE(std::is_abstract_v<Constraint>);
}

// ---- construction -------------------------------------------------------

// The generated empty ctor leaves `TypeParameter` null and `BaseTypes` empty.
TEST(CSharp_Constraint, EmptyCtorHasNullTypeParameterAndEmptyBaseTypes) {
    Constraint c;
    EXPECT_EQ(c.TypeParameter(), nullptr);
    EXPECT_EQ(c.BaseTypes().Count(), 0);
    EXPECT_EQ(c.GetChildCount(), 1);  // 1 single slot (null) + 0 collection elements
}

// The generated required-prefix ctor sets `TypeParameter`.
TEST(CSharp_Constraint, CtorSetsTypeParameter) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    EXPECT_EQ(c.TypeParameter(), t.get());
    EXPECT_EQ(t->Parent(), &c);
    EXPECT_EQ(t->ChildIndex, 0);
}

// ---- the `TypeParameter` slot (a single REQUIRED `SimpleType` child) ----

// The setter re-parents and detaches the previous child.
TEST(CSharp_Constraint, TypeParameterSetterReparentsAndDetaches) {
    auto a = std::make_unique<SimpleType>("A");
    Constraint c(a.get());
    EXPECT_EQ(a->Parent(), &c);
    auto b = std::make_unique<SimpleType>("B");
    c.TypeParameter(b.get());
    EXPECT_EQ(c.TypeParameter(), b.get());
    EXPECT_EQ(b->Parent(), &c);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetChildNode
}

// The setter clears with null.
TEST(CSharp_Constraint, TypeParameterSetterClearsWithNull) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    c.TypeParameter(nullptr);
    EXPECT_EQ(c.TypeParameter(), nullptr);
    EXPECT_EQ(t->Parent(), nullptr);
}

// ---- the `BaseTypes` collection (incremental) ---------------------------

// `Add` appends, re-parents, and maintains `ChildIndex` incrementally (the collection is the
// node's only collection and its last slot, so `supportsIncremental` is true).
TEST(CSharp_Constraint, BaseTypesAddAppendsParentsAndMaintainsChildIndex) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    auto b0 = std::make_unique<PrimitiveType>("struct");
    auto b1 = std::make_unique<PrimitiveType>("new");
    c.BaseTypes().Add(b0.get());
    c.BaseTypes().Add(b1.get());
    EXPECT_EQ(c.BaseTypes().Count(), 2);
    EXPECT_EQ(b0->Parent(), &c);
    EXPECT_EQ(b1->Parent(), &c);
    EXPECT_EQ(b0->ChildIndex, 1);  // incremental: 1 + 0
    EXPECT_EQ(b1->ChildIndex, 2);  // incremental: 1 + 1
    EXPECT_EQ(c.GetChildCount(), 3);  // 1 TypeParameter + 2 base types
}

// `GetCollectionByKind` returns the `BaseTypes` collection for the `BaseType` kind.
TEST(CSharp_Constraint, GetCollectionByKindReturnsBaseTypesForBaseTypeKind) {
    Constraint c;
    EXPECT_EQ(c.GetCollectionByKind(&Slots::BaseType), &c.BaseTypes());
    EXPECT_EQ(c.GetCollectionByKind(&Slots::ConstraintTypeParameter), nullptr);  // a single slot
    EXPECT_EQ(c.GetCollectionByKind(&Slots::TypeArgument), nullptr);  // an unrelated collection kind
    EXPECT_EQ(c.GetCollectionByKind(nullptr), nullptr);
}

// ---- slot storage (single -> collection) --------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single slot then the collection. A
// constraint with a type parameter and two base types reports `GetChildCount` 3.
TEST(CSharp_Constraint, CollectionAwareSlotStorage) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    auto b0 = std::make_unique<PrimitiveType>("struct");
    auto b1 = std::make_unique<PrimitiveType>("new");
    c.BaseTypes().Add(b0.get());
    c.BaseTypes().Add(b1.get());
    EXPECT_EQ(c.GetChildCount(), 3);
    EXPECT_EQ(c.GetChild(0), t.get());
    EXPECT_EQ(c.GetChild(1), b0.get());
    EXPECT_EQ(c.GetChild(2), b1.get());
    EXPECT_EQ(c.GetChildSlotInfo(0), &Constraint::TypeParameterSlot);
    EXPECT_EQ(c.GetChildSlotInfo(1), &Constraint::BaseTypesSlot);
    EXPECT_EQ(c.GetChildSlotInfo(2), &Constraint::BaseTypesSlot);
    EXPECT_THROW(c.GetChild(3), std::out_of_range);
}

// `SetChild` replaces the `TypeParameter` (index 0) or a collection element (index >= 1).
TEST(CSharp_Constraint, SetChildReplacesTypeParameterAndBaseType) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    auto b0 = std::make_unique<PrimitiveType>("struct");
    c.BaseTypes().Add(b0.get());
    auto newT = std::make_unique<SimpleType>("U");
    c.SetChild(0, newT.get());
    EXPECT_EQ(c.TypeParameter(), newT.get());
    EXPECT_EQ(newT->Parent(), &c);
    EXPECT_EQ(t->Parent(), nullptr);  // detached by SetChildNode
    auto newB = std::make_unique<PrimitiveType>("class");
    c.SetChild(1, newB.get());
    EXPECT_EQ(c.BaseTypes().At(0), newB.get());
    EXPECT_EQ(newB->Parent(), &c);
}

// `GetChildSlotInfo` throws past the end.
TEST(CSharp_Constraint, GetChildSlotInfoThrowsOutOfRange) {
    Constraint c;
    EXPECT_THROW(c.GetChildSlotInfo(1), std::out_of_range);  // empty: only index 0 valid
}

// ---- slot statics (pointing at the shared `Slots` kinds) ----------------

// `TypeParameterSlot` points at the cycle-broken `Slots::ConstraintTypeParameter`; `BaseTypesSlot`
// points at `Slots::BaseType`.
TEST(CSharp_Constraint, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(Constraint::TypeParameterSlot.Kind(), &Slots::ConstraintTypeParameter);
    EXPECT_EQ(Constraint::BaseTypesSlot.Kind(), &Slots::BaseType);
}

// `TypeParameterSlot` accepts a `SimpleType` not a `PrimitiveType`; `BaseTypesSlot` accepts any
// `AstType` (both a `SimpleType` and a `PrimitiveType`).
TEST(CSharp_Constraint, SlotIsInstanceOfTypeCrossCheck) {
    auto simple = std::make_unique<SimpleType>("T");
    auto prim = std::make_unique<PrimitiveType>("struct");
    EXPECT_TRUE(Constraint::TypeParameterSlot.IsInstanceOfType(simple.get()));
    EXPECT_FALSE(Constraint::TypeParameterSlot.IsInstanceOfType(prim.get()));
    EXPECT_TRUE(Constraint::BaseTypesSlot.IsInstanceOfType(simple.get()));
    EXPECT_TRUE(Constraint::BaseTypesSlot.IsInstanceOfType(prim.get()));
}

// The two slot statics are distinct (cross-element-type comparison via the common
// `CSharpSlotInfo*` base -- the D251/D252 EXPECT_NE precedent).
TEST(CSharp_Constraint, SlotStaticDistinctFromUnrelatedKinds) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Constraint::TypeParameterSlot),
              static_cast<const CSharpSlotInfo*>(&Constraint::BaseTypesSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Constraint::TypeParameterSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::Type));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Constraint::BaseTypesSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::TypeArgument));
}

// ---- AcceptVisitor (the dispatch) ---------------------------------------

// `AcceptVisitor` dispatches to `VisitConstraint`; the depth-first walk visits the `TypeParameter`
// (single slot, recursing into the `SimpleType`'s backing `Identifier`) then the `BaseTypes`
// (each `AstType` recursing as appropriate).
TEST(CSharp_Constraint, AcceptVisitorDispatchesAndWalks) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    auto b0 = std::make_unique<PrimitiveType>("struct");       // a leaf AstType
    auto b1 = std::make_unique<SimpleType>("IComparable");      // recurses into its NameToken
    c.BaseTypes().Add(b0.get());
    c.BaseTypes().Add(b1.get());
    RecordingVisitor v;
    c.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "constraint",
        "simple:T", "id:T",            // the TypeParameter SimpleType (single slot) -> its NameToken
        "prim:struct",                 // BaseTypes[0] (a leaf PrimitiveType)
        "simple:IComparable", "id:IComparable"}));  // BaseTypes[1] (a SimpleType) -> its NameToken
}

// `AcceptVisitor` is virtual through an `AstNode*`.
TEST(CSharp_Constraint, AcceptVisitorIsVirtualThroughAstNode) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    AstNode* asAst = &c;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"constraint", "simple:T", "id:T"}));
}

// ---- DoMatch (the generated required-recursive + collection match) ------

// Two `Constraint`s with the same `TypeParameter` name and empty `BaseTypes` match.
TEST(CSharp_Constraint, DoMatchMatchesSame) {
    auto ta = std::make_unique<SimpleType>("T");
    auto tb = std::make_unique<SimpleType>("T");
    Constraint a(ta.get());
    Constraint b(tb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `Constraint`s with different `TypeParameter` names reject.
TEST(CSharp_Constraint, DoMatchRejectsDifferentTypeParameter) {
    auto ta = std::make_unique<SimpleType>("T");
    auto tb = std::make_unique<SimpleType>("U");
    Constraint a(ta.get());
    Constraint b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `Constraint`s with matching `TypeParameter` but a `BaseTypes` count mismatch reject.
TEST(CSharp_Constraint, DoMatchRejectsBaseTypesCountMismatch) {
    auto ta = std::make_unique<SimpleType>("T");
    auto tb = std::make_unique<SimpleType>("T");
    Constraint a(ta.get());
    Constraint b(tb.get());
    auto a0 = std::make_unique<PrimitiveType>("struct");
    a.BaseTypes().Add(a0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `Constraint`s with matching `TypeParameter` and matching `BaseTypes` match.
TEST(CSharp_Constraint, DoMatchMatchesWithMatchingBaseTypes) {
    auto ta = std::make_unique<SimpleType>("T");
    auto tb = std::make_unique<SimpleType>("T");
    Constraint a(ta.get());
    Constraint b(tb.get());
    auto a0 = std::make_unique<PrimitiveType>("struct");
    auto b0 = std::make_unique<PrimitiveType>("struct");
    a.BaseTypes().Add(a0.get());
    b.BaseTypes().Add(b0.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `Constraint`s with matching `TypeParameter` but a `BaseTypes` value mismatch reject.
TEST(CSharp_Constraint, DoMatchRejectsBaseTypesValueMismatch) {
    auto ta = std::make_unique<SimpleType>("T");
    auto tb = std::make_unique<SimpleType>("T");
    Constraint a(ta.get());
    Constraint b(tb.get());
    auto a0 = std::make_unique<PrimitiveType>("struct");
    auto b0 = std::make_unique<PrimitiveType>("class");
    a.BaseTypes().Add(a0.get());
    b.BaseTypes().Add(b0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `Constraint` pattern rejects a non-`Constraint` candidate.
TEST(CSharp_Constraint, DoMatchRejectsNonConstraintCandidate) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint pattern(t.get());
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_FALSE(DoMatchAgainst(&pattern, expr.get()));
}

// A `Constraint` pattern rejects a `null` candidate.
TEST(CSharp_Constraint, DoMatchRejectsNullCandidate) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint pattern(t.get());
    EXPECT_FALSE(DoMatchAgainst(&pattern, nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `TypeParameter` and the `BaseTypes` collection.
TEST(CSharp_Constraint, CloneDeepCopiesTypeParameterAndBaseTypes) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    auto b0 = std::make_unique<PrimitiveType>("struct");
    auto b1 = std::make_unique<SimpleType>("IComparable");
    c.BaseTypes().Add(b0.get());
    c.BaseTypes().Add(b1.get());

    std::unique_ptr<Constraint> copy(c.Clone());
    ASSERT_NE(copy->TypeParameter(), nullptr);
    ASSERT_NE(copy->TypeParameter(), t.get());  // deep copy, not the same node
    EXPECT_EQ(*copy->TypeParameter()->Identifier(), "T");
    EXPECT_EQ(copy->BaseTypes().Count(), 2);
    EXPECT_NE(copy->BaseTypes().At(0), b0.get());
    EXPECT_NE(copy->BaseTypes().At(1), b1.get());
    // The cloned children are re-parented to the copy.
    EXPECT_EQ(copy->TypeParameter()->Parent(), copy.get());
    EXPECT_EQ(copy->BaseTypes().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->BaseTypes().At(1)->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*` and returns a covariant `Constraint*`.
TEST(CSharp_Constraint, CloneIsVirtualAndCovariant) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    AstNode* asAst = &c;
    std::unique_ptr<AstNode> copy(asAst->Clone());
    EXPECT_NE(dynamic_cast<Constraint*>(copy.get()), nullptr);
}

// `Clone` does not detach the source's children.
TEST(CSharp_Constraint, CloneDoesNotDetachSource) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    auto b0 = std::make_unique<PrimitiveType>("struct");
    c.BaseTypes().Add(b0.get());
    std::unique_ptr<Constraint> copy(c.Clone());
    EXPECT_EQ(t->Parent(), &c);      // source intact
    EXPECT_EQ(b0->Parent(), &c);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `TypeParameter` required slot is filled).
TEST(CSharp_Constraint, CheckInvariantPassesOnFilledNode) {
    auto t = std::make_unique<SimpleType>("T");
    Constraint c(t.get());
    c.CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `TypeParameter` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_Constraint, CheckInvariantRejectsEmpty) {
    Constraint c;  // no TypeParameter
    EXPECT_DEATH(c.CheckInvariant(), "");
}
#endif

} // namespace
