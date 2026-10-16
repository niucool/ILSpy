// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the tuple type family (cpp/.../Syntax/TupleTypeElement.hpp +
// TupleAstType.hpp, the port of ICSharpCode.Decompiler/CSharp/Syntax/TupleAstType.cs) --
// the `tuple_type_element ::= type identifier?` (C# grammar 8.3.1) and the
// `tuple_type ::= '(' tuple_type_element (',' tuple_type_element)+ ')'`. The next in-order
// Phase-5 piece per the D288 plan (the remaining GeneralScope `AstType`-bearing nodes).
// `TupleTypeElement` combines the `CastExpression` D243 required-`AstType`-slot shape with the
// `SimpleType` D237 nullable-string-name-`[Slot]` shape; `TupleAstType` is the
// `ArrayInitializerExpression` D250 collection-only shape applied to the `AstType` hierarchy.
// The suite shares a `RecordingVisitor`, a `DoMatchAgainst` helper, and a `makeTupleType` helper
// (the D234 pattern).

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
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TupleAstType.hpp"
#include "Decompiler/CSharp/Syntax/TupleTypeElement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `TupleTypeElement`/`TupleAstType` under test
// (plus the `SimpleType`/`Identifier`/`PrimitiveType` of their children, the `Constraint` used
// for the cross-type DoMatch rejection, and the `ArrayInitializerExpression` used for the
// cross-structural-twin TupleAstType-vs-ArrayInitializerExpression rejection), recording a tag
// and recursing via `VisitChildren` (the inherited depth-first default). The trace is the
// visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitTupleTypeElement(TupleTypeElement* node) override {
        if (node == nullptr) { trace.push_back("<null-tte>"); return; }
        trace.push_back("tte");
        VisitChildren(node);
    }
    void VisitTupleType(TupleAstType* node) override {
        if (node == nullptr) { trace.push_back("<null-tt>"); return; }
        trace.push_back("tt");
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
    void VisitConstraint(Constraint* node) override {
        if (node == nullptr) { trace.push_back("<null-constraint>"); return; }
        trace.push_back("constraint");
        VisitChildren(node);
    }
    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-aie>"); return; }
        trace.push_back("aie");
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

// A holder keeping a `TupleAstType` and its `TupleTypeElement`/`SimpleType` children alive in
// the test scope (the D241 non-owning-raw-pointer model -- the parent does not own its
// children; the holder's `unique_ptr`s do). Builds a tuple type from a list of `PrimitiveType`
// keyword names (each element is a bare `PrimitiveType` with no name).
struct TupleTypeHolder {
    std::vector<std::unique_ptr<PrimitiveType>> types;
    std::vector<std::unique_ptr<TupleTypeElement>> elements;
    std::unique_ptr<TupleAstType> tuple;
};

TupleTypeHolder makeTupleType(std::vector<std::string> keywords) {
    TupleTypeHolder h;
    h.tuple = std::make_unique<TupleAstType>();
    for (auto& kw : keywords) {
        auto t = std::make_unique<PrimitiveType>(kw);
        auto e = std::make_unique<TupleTypeElement>(t.get(), std::string());
        h.tuple->Elements().Add(e.get());
        h.types.push_back(std::move(t));
        h.elements.push_back(std::move(e));
    }
    return h;
}

} // namespace

// ===========================================================================
// TupleTypeElement
// ===========================================================================

// `TupleTypeElement` derives directly from `AstNode`; it is disjoint from `Expression`/
// `Statement`/`AstType`/`EntityDeclaration` (the `VariableInitializer` D266 / `CatchClause` D269 /
// `Constraint` D283 direct-`AstNode` precedent -- a tuple element is a structural node, not a
// type itself).
TEST(CSharp_TupleTypeElement, IsAstNodeNotExpressionNotStatementNotAstType) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string("X"));
    EXPECT_TRUE(dynamic_cast<AstNode*>(&e) != nullptr);
    EXPECT_FALSE(dynamic_cast<Expression*>(&e) != nullptr);
    EXPECT_FALSE(dynamic_cast<AstType*>(&e) != nullptr);
    EXPECT_FALSE(dynamic_cast<EntityDeclaration*>(&e) != nullptr);
}

// `TupleTypeElement` is `final` (the C# `sealed`).
TEST(CSharp_TupleTypeElement, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<TupleTypeElement>);
    EXPECT_FALSE(std::is_abstract_v<TupleTypeElement>);
}

// The generated empty ctor leaves `Type` null and `NameToken` null (no name).
TEST(CSharp_TupleTypeElement, EmptyCtorHasNullTypeAndNullName) {
    TupleTypeElement e;
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(e.NameToken(), nullptr);
    EXPECT_FALSE(e.Name().has_value());
    EXPECT_EQ(e.GetChildCount(), 2);  // two single slots (both null), no collection
}

// The generated all-params ctor sets `Type` (required) and `Name` (a string [Slot] required
// regardless of optionality; the setter uses `CreateIfNotEmpty`, so a non-empty name creates a
// token).
TEST(CSharp_TupleTypeElement, AllParamsCtorSetsTypeAndName) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string("X"));
    EXPECT_EQ(e.Type(), t.get());
    EXPECT_EQ(t->Parent(), &e);
    EXPECT_EQ(t->ChildIndex, 0);
    ASSERT_TRUE(e.Name().has_value());
    EXPECT_EQ(*e.Name(), "X");
    EXPECT_NE(e.NameToken(), nullptr);
    EXPECT_EQ(e.NameToken()->Parent(), &e);
    EXPECT_EQ(e.NameToken()->ChildIndex, 1);
}

// The single-arg `(AstType)` ctor is NOT generated (the required prefix IS the full set -- both
// slots are required, so `RequiredConstructorPrefixLength` is 2). `std::is_constructible` on the
// one-arg form must be false.
TEST(CSharp_TupleTypeElement, NoSingleArgAstTypeCtor) {
    EXPECT_FALSE((std::is_constructible_v<TupleTypeElement, AstType*>));
    EXPECT_TRUE((std::is_constructible_v<TupleTypeElement, AstType*, std::string>));
}

// ---- the `Type` slot (a single REQUIRED `AstType` child) ------------------

// The setter re-parents and detaches the previous child.
TEST(CSharp_TupleTypeElement, TypeSetterReparentsAndDetaches) {
    auto a = std::make_unique<SimpleType>("A");
    TupleTypeElement e(a.get(), std::string());
    EXPECT_EQ(a->Parent(), &e);
    auto b = std::make_unique<SimpleType>("B");
    e.Type(b.get());
    EXPECT_EQ(e.Type(), b.get());
    EXPECT_EQ(b->Parent(), &e);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetChildNode
}

// The setter clears with null.
TEST(CSharp_TupleTypeElement, TypeSetterClearsWithNull) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string());
    e.Type(nullptr);
    EXPECT_EQ(e.Type(), nullptr);
    EXPECT_EQ(t->Parent(), nullptr);
}

// ---- the `NameToken` slot (the backing `Identifier` token of the optional name) ----

// The setter re-parents and detaches the previous token.
TEST(CSharp_TupleTypeElement, NameTokenSetterReparentsAndDetaches) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string("A"));
    auto oldTok = e.NameToken();
    auto newTok = std::unique_ptr<Identifier>(Identifier::Create("B"));
    e.NameToken(newTok.get());
    EXPECT_EQ(e.NameToken(), newTok.get());
    EXPECT_EQ(newTok->Parent(), &e);
    EXPECT_EQ(oldTok->Parent(), nullptr);  // detached by SetChildNode
}

// `Name()` returns `std::optional<std::string>` (nullopt when the token is absent -- the faithful
// `string?`); the setter creates the token via `Identifier::CreateIfNotEmpty`, so an empty name
// clears the token.
TEST(CSharp_TupleTypeElement, NameSetterCreateIfNotEmptyClearsOnEmpty) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string("X"));
    ASSERT_NE(e.NameToken(), nullptr);
    e.Name(std::string());  // empty -> CreateIfNotEmpty returns null
    EXPECT_EQ(e.NameToken(), nullptr);
    EXPECT_FALSE(e.Name().has_value());
}

// ---- slot storage (two single slots, flat switch) -------------------------

TEST(CSharp_TupleTypeElement, SlotStorageFlatTwoCases) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string("X"));
    EXPECT_EQ(e.GetChildCount(), 2);
    EXPECT_EQ(e.GetChild(0), t.get());
    EXPECT_EQ(e.GetChild(1), e.NameToken());
    EXPECT_EQ(e.GetChildSlotInfo(0), &e.TypeSlot);
    EXPECT_EQ(e.GetChildSlotInfo(1), &e.NameTokenSlot);
    EXPECT_THROW(e.GetChild(2), std::out_of_range);
    EXPECT_THROW(e.GetChildSlotInfo(2), std::out_of_range);
}

// `SetChild` replaces each slot in place.
TEST(CSharp_TupleTypeElement, SetChildReplacesEachSlot) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string("X"));
    auto t2 = std::make_unique<SimpleType>("U");
    e.SetChild(0, t2.get());
    EXPECT_EQ(e.Type(), t2.get());
    EXPECT_EQ(t2->Parent(), &e);
    auto id = std::unique_ptr<Identifier>(Identifier::Create("Y"));
    e.SetChild(1, id.get());
    EXPECT_EQ(e.NameToken(), id.get());
    EXPECT_EQ(id->Parent(), &e);
    EXPECT_THROW(e.SetChild(2, nullptr), std::out_of_range);
}

// ---- shared `Slots` kind identity -----------------------------------------

TEST(CSharp_TupleTypeElement, SlotStaticsPointAtSharedSlotsKinds) {
    EXPECT_EQ(TupleTypeElement::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(TupleTypeElement::NameTokenSlot.Kind(), &Slots::Identifier);
}

// `IsInstanceOfType` is-a cross-check: the `TypeSlot` (a `CSharpSlotInfoT<AstType>`) accepts an
// `AstType` (a `SimpleType`) but not an `Expression`; the `NameTokenSlot` (a
// `CSharpSlotInfoT<Identifier>`) accepts an `Identifier` but not a `SimpleType`.
TEST(CSharp_TupleTypeElement, SlotKindIsInstanceOfTypeCrossCheck) {
    auto t = std::make_unique<SimpleType>("T");
    auto id = std::unique_ptr<Identifier>(Identifier::Create("X"));
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(TupleTypeElement::TypeSlot.IsInstanceOfType(t.get()));
    EXPECT_FALSE(TupleTypeElement::TypeSlot.IsInstanceOfType(expr.get()));
    EXPECT_TRUE(TupleTypeElement::NameTokenSlot.IsInstanceOfType(id.get()));
    EXPECT_FALSE(TupleTypeElement::NameTokenSlot.IsInstanceOfType(t.get()));
}

// ---- AcceptVisitor dispatch + virtuality ----------------------------------

TEST(CSharp_TupleTypeElement, AcceptVisitorDispatchRecordsTag) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string());
    RecordingVisitor v;
    e.AcceptVisitor(v);
    // A bare-type element (no name) walks into the SimpleType (which recurses into its
    // IdentifierToken): tte, simple:T, id:T.
    EXPECT_EQ(v.trace, std::vector<std::string>({"tte", "simple:T", "id:T"}));
}

TEST(CSharp_TupleTypeElement, AcceptVisitorVirtualThroughAstNode) {
    auto t = std::make_unique<SimpleType>("T");
    auto e = std::make_unique<TupleTypeElement>(t.get(), std::string("X"));
    RecordingVisitor v;
    AstNode* node = e.get();
    node->AcceptVisitor(v);
    // A named element walks into the SimpleType then the NameToken: tte, simple:T, id:T, id:X.
    EXPECT_EQ(v.trace, std::vector<std::string>({"tte", "simple:T", "id:T", "id:X"}));
}

// ---- DoMatch ---------------------------------------------------------------

TEST(CSharp_TupleTypeElement, DoMatchSame) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string("X"));
    auto t2 = std::make_unique<SimpleType>("T");
    TupleTypeElement b(t2.get(), std::string("X"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TupleTypeElement, DoMatchTypeMismatchRejects) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string("X"));
    auto t2 = std::make_unique<PrimitiveType>("int");
    TupleTypeElement b(t2.get(), std::string("X"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

TEST(CSharp_TupleTypeElement, DoMatchNameMismatchRejects) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string("X"));
    auto t2 = std::make_unique<SimpleType>("T");
    TupleTypeElement b(t2.get(), std::string("Y"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// The `$any$` wildcard in the pattern's `Name` matches any candidate name (but the `Type` term
// still runs first and must match).
TEST(CSharp_TupleTypeElement, DoMatchAnyStringWildcardOnName) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string(Pattern::AnyString));
    auto t2 = std::make_unique<SimpleType>("T");
    TupleTypeElement b(t2.get(), std::string("Whatever"));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Both names absent (nullopt vs nullopt) matches -- the `string?` optional absence.
TEST(CSharp_TupleTypeElement, DoMatchBothNamesAbsentMatches) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string());  // empty -> null token
    auto t2 = std::make_unique<SimpleType>("T");
    TupleTypeElement b(t2.get(), std::string());  // empty -> null token
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A name asymmetry (one absent, one present) rejects.
TEST(CSharp_TupleTypeElement, DoMatchNameAsymmetryRejects) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string());  // empty -> null token
    auto t2 = std::make_unique<SimpleType>("T");
    TupleTypeElement b(t2.get(), std::string("X"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A cross-type rejection (vs a `Constraint`, a direct-`AstNode` node with a `SimpleType` first
// slot -- a near-identical shape but a distinct concrete type).
TEST(CSharp_TupleTypeElement, DoMatchCrossTypeRejects) {
    auto t1 = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t1.get(), std::string("X"));
    auto t2 = std::make_unique<SimpleType>("T");
    Constraint c(t2.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &c));
}

TEST(CSharp_TupleTypeElement, DoMatchNullRejects) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement a(t.get(), std::string("X"));
    EXPECT_FALSE(DoMatchAgainst(&a, nullptr));
}

// ---- Clone ----------------------------------------------------------------

TEST(CSharp_TupleTypeElement, CloneDeepCopyAndVirtuality) {
    auto t = std::make_unique<SimpleType>("T");
    auto e = std::make_unique<TupleTypeElement>(t.get(), std::string("X"));
    auto* clone = e->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone, e.get());
    EXPECT_NE(clone->Type(), t.get());  // deep-copy, not aliased
    EXPECT_NE(clone->NameToken(), e->NameToken());  // deep-copy, not aliased
    ASSERT_TRUE(clone->Name().has_value());
    EXPECT_EQ(*clone->Name(), "X");
    EXPECT_EQ(clone->Type()->Parent(), clone);
    EXPECT_EQ(clone->NameToken()->Parent(), clone);
    auto* astClone = static_cast<AstNode*>(e.get())->Clone();  // virtual through AstNode*
    EXPECT_NE(astClone, nullptr);
    delete astClone;
    delete clone;
}

TEST(CSharp_TupleTypeElement, CloneSkipsAbsentNameToken) {
    auto t = std::make_unique<SimpleType>("T");
    auto e = std::make_unique<TupleTypeElement>(t.get(), std::string());  // no name
    auto* clone = e->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->NameToken(), nullptr);
    EXPECT_FALSE(clone->Name().has_value());
    EXPECT_NE(clone->Type(), nullptr);
    delete clone;
}

// ---- CheckInvariant --------------------------------------------------------

TEST(CSharp_TupleTypeElement, CheckInvariantPassesOnFilledType) {
    auto t = std::make_unique<SimpleType>("T");
    TupleTypeElement e(t.get(), std::string());  // Type filled, Name optional (absent)
    e.CheckInvariant();  // should not assert -- Type is required and filled, Name is optional
}

TEST(CSharp_TupleTypeElement, CheckInvariantRejectsEmpty) {
    TupleTypeElement e;  // Type is a REQUIRED slot -- the empty node violates the invariant
    EXPECT_DEATH(e.CheckInvariant(), "");
}

// ---- slot-static distinctness ----------------------------------------------

// The two slot statics of distinct element types are unrelated pointer types; compare through
// the common `const CSharpSlotInfo*` base (the D251/D252/D262 cross-element-type `EXPECT_NE`
// precedent).
TEST(CSharp_TupleTypeElement, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&TupleTypeElement::TypeSlot),
              static_cast<const CSharpSlotInfo*>(&TupleTypeElement::NameTokenSlot));
}

// ===========================================================================
// TupleAstType
// ===========================================================================

// `TupleAstType` is an `AstType` and an `AstNode`; it is disjoint from `Expression`/`Statement`/
// `EntityDeclaration` (the `SimpleType` D237 / `PrimitiveType` D236 concrete-`AstType`
// precedent).
TEST(CSharp_TupleAstType, IsAstTypeAndAstNodeNotExpression) {
    TupleAstType tt;
    EXPECT_TRUE(dynamic_cast<AstType*>(&tt) != nullptr);
    EXPECT_TRUE(dynamic_cast<AstNode*>(&tt) != nullptr);
    EXPECT_FALSE(dynamic_cast<Expression*>(&tt) != nullptr);
    EXPECT_FALSE(dynamic_cast<EntityDeclaration*>(&tt) != nullptr);
}

// `TupleAstType` is `final` (the C# `sealed`).
TEST(CSharp_TupleAstType, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<TupleAstType>);
    EXPECT_FALSE(std::is_abstract_v<TupleAstType>);
}

// The generated empty ctor leaves `Elements` empty (an empty node reports `GetChildCount` 0 --
// no single-slot count term, the collection-only `ArrayInitializerExpression` D250 precedent).
TEST(CSharp_TupleAstType, EmptyCtorHasEmptyElements) {
    TupleAstType tt;
    EXPECT_EQ(tt.Elements().Count(), 0);
    EXPECT_EQ(tt.GetChildCount(), 0);
    EXPECT_THROW(tt.GetChild(0), std::out_of_range);
}

// ---- the `Elements` collection (incremental) ------------------------------

// `Add` appends, re-parents, and maintains `ChildIndex` incrementally (the collection is the
// node's only collection and its last slot, so `supportsIncremental` is true).
TEST(CSharp_TupleAstType, ElementsAddAppendsParentsAndMaintainsChildIndex) {
    TupleAstType tt;
    auto t0 = std::make_unique<SimpleType>("int");
    auto e0 = std::make_unique<TupleTypeElement>(t0.get(), std::string("X"));
    auto t1 = std::make_unique<SimpleType>("string");
    auto e1 = std::make_unique<TupleTypeElement>(t1.get(), std::string());
    tt.Elements().Add(e0.get());
    tt.Elements().Add(e1.get());
    EXPECT_EQ(tt.Elements().Count(), 2);
    EXPECT_EQ(e0->Parent(), &tt);
    EXPECT_EQ(e1->Parent(), &tt);
    EXPECT_EQ(e0->ChildIndex, 0);  // incremental: 0 + 0
    EXPECT_EQ(e1->ChildIndex, 1);  // incremental: 0 + 1
    EXPECT_EQ(tt.GetChildCount(), 2);
}

// `GetCollectionByKind` returns the `Elements` collection for the `Element` kind.
TEST(CSharp_TupleAstType, GetCollectionByKindReturnsElementsForElementKind) {
    TupleAstType tt;
    EXPECT_EQ(tt.GetCollectionByKind(&Slots::Element), &tt.Elements());
    EXPECT_EQ(tt.GetCollectionByKind(&Slots::Type), nullptr);  // a single-slot kind
    EXPECT_EQ(tt.GetCollectionByKind(&Slots::TypeArgument), nullptr);  // an unrelated collection kind
    EXPECT_EQ(tt.GetCollectionByKind(nullptr), nullptr);
}

// ---- collection-only slot storage ----------------------------------------

TEST(CSharp_TupleAstType, CollectionOnlySlotStorage) {
    TupleAstType tt;
    auto t = std::make_unique<SimpleType>("int");
    auto e = std::make_unique<TupleTypeElement>(t.get(), std::string());
    tt.Elements().Add(e.get());
    EXPECT_EQ(tt.GetChildCount(), 1);
    EXPECT_EQ(tt.GetChild(0), e.get());
    EXPECT_EQ(tt.GetChildSlotInfo(0), &tt.ElementsSlot);
    EXPECT_THROW(tt.GetChild(1), std::out_of_range);
    EXPECT_THROW(tt.GetChildSlotInfo(1), std::out_of_range);
}

// ---- shared `Slots` kind identity -----------------------------------------

TEST(CSharp_TupleAstType, ElementsSlotPointsAtSharedElementKind) {
    EXPECT_EQ(TupleAstType::ElementsSlot.Kind(), &Slots::Element);
}

// ---- AcceptVisitor dispatch + virtuality ----------------------------------

TEST(CSharp_TupleAstType, AcceptVisitorDispatchRecordsTag) {
    TupleAstType tt;
    RecordingVisitor v;
    tt.AcceptVisitor(v);
    // An empty tuple type records just itself.
    EXPECT_EQ(v.trace, std::vector<std::string>({"tt"}));
}

TEST(CSharp_TupleAstType, AcceptVisitorVirtualThroughAstType) {
    auto tt = std::make_unique<TupleAstType>();
    auto t = std::make_unique<SimpleType>("int");
    auto e = std::make_unique<TupleTypeElement>(t.get(), std::string());
    tt->Elements().Add(e.get());
    RecordingVisitor v;
    AstType* node = tt.get();
    node->AcceptVisitor(v);
    // tt -> tte -> simple:int -> id:int (the SimpleType recurses into its IdentifierToken).
    EXPECT_EQ(v.trace, std::vector<std::string>({"tt", "tte", "simple:int", "id:int"}));
}

// ---- DoMatch ---------------------------------------------------------------

TEST(CSharp_TupleAstType, DoMatchSame) {
    auto tt1 = makeTupleType({"int", "string"});
    auto tt2 = makeTupleType({"int", "string"});
    EXPECT_TRUE(DoMatchAgainst(tt1.tuple.get(), tt2.tuple.get()));
}

TEST(CSharp_TupleAstType, DoMatchDifferentCountRejects) {
    auto tt1 = makeTupleType({"int", "string"});
    auto tt2 = makeTupleType({"int"});
    EXPECT_FALSE(DoMatchAgainst(tt1.tuple.get(), tt2.tuple.get()));
}

TEST(CSharp_TupleAstType, DoMatchDifferentElementValueRejects) {
    auto tt1 = makeTupleType({"int"});
    auto tt2 = makeTupleType({"string"});
    EXPECT_FALSE(DoMatchAgainst(tt1.tuple.get(), tt2.tuple.get()));
}

// A cross-structural-twin rejection (vs an `ArrayInitializerExpression`, the collection-only
// structural twin across disjoint hierarchies -- `TupleAstType` is an `AstType`,
// `ArrayInitializerExpression` is an `Expression`).
TEST(CSharp_TupleAstType, DoMatchCrossStructuralTwinRejects) {
    auto tt = makeTupleType({"int"});
    auto aie = std::make_unique<ArrayInitializerExpression>();
    EXPECT_FALSE(DoMatchAgainst(tt.tuple.get(), aie.get()));
    EXPECT_FALSE(DoMatchAgainst(aie.get(), tt.tuple.get()));
}

TEST(CSharp_TupleAstType, DoMatchNullRejects) {
    auto tt = makeTupleType({"int"});
    EXPECT_FALSE(DoMatchAgainst(tt.tuple.get(), nullptr));
}

// ---- Clone ----------------------------------------------------------------

TEST(CSharp_TupleAstType, CloneDeepCopyAndVirtuality) {
    auto tt = makeTupleType({"int", "string"});
    auto* clone = tt.tuple->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone, tt.tuple.get());
    EXPECT_EQ(clone->Elements().Count(), 2);
    EXPECT_NE(clone->Elements().At(0), tt.tuple->Elements().At(0));  // deep-copy, not aliased
    EXPECT_NE(clone->Elements().At(1), tt.tuple->Elements().At(1));
    EXPECT_EQ(clone->Elements().At(0)->Parent(), clone);
    auto* astClone = static_cast<AstType*>(tt.tuple.get())->Clone();  // covariant through AstType*
    EXPECT_NE(astClone, nullptr);
    auto* typedClone = dynamic_cast<TupleAstType*>(astClone);
    ASSERT_NE(typedClone, nullptr);
    EXPECT_EQ(typedClone->Elements().Count(), 2);
    delete astClone;
    delete clone;
}

TEST(CSharp_TupleAstType, CloneEmpty) {
    auto tt = std::make_unique<TupleAstType>();
    auto* clone = tt->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->Elements().Count(), 0);
    delete clone;
}

// ---- CheckInvariant --------------------------------------------------------

// `CheckInvariant` PASSES on an empty node (the collection-only shape has no required single
// slots -- the `ArrayInitializerExpression` D250 / `BlockStatement` D256 collection-only
// precedent).
TEST(CSharp_TupleAstType, CheckInvariantPassesOnEmpty) {
    TupleAstType tt;
    tt.CheckInvariant();  // should not assert
}

// ---- slot-static distinctness ----------------------------------------------

TEST(CSharp_TupleAstType, ElementsSlotIsDistinctFromOtherKinds) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&TupleAstType::ElementsSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::TypeArgument));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&TupleAstType::ElementsSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::Type));
}
