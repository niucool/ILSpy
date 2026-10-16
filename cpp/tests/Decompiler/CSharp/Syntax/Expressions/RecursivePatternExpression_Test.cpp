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

// Tests for the `RecursivePatternExpression` concrete node
// (cpp/.../Syntax/Expressions/RecursivePatternExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/RecursivePatternExpression.cs). The
// `recursive_pattern ::= type? '{' pattern* '}' variable_designation?` /
// `type? '(' pattern* ')' variable_designation?` node (C# grammar 11.2.5/11.2.6).
//
// It is the `ObjectCreateExpression` D251 shape (a single + a non-incremental collection + a
// trailing nullable single) with the leading `Type` single NULLABLE (not required), the
// trailing `Designation` a `VariableDesignation?` (not an `ArrayInitializerExpression?`), and a
// non-`[Slot]` `IsPositional` bool scalar added. The four-term `DoMatch` combines two
// `MatchOptional` terms (the leading `Type` and the trailing `Designation`), a collection-
// `DoMatch` term (`SubPatterns`), and a plain-bool term (`IsPositional`) -- the first ported
// node to combine two `MatchOptional` terms with a collection-`DoMatch` and a plain-bool term.

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
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/RecursivePatternExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the node under test (plus the `SimpleType`/`Identifier`
// `Type` subtree, the leaf `Expression`s used as `SubPatterns` elements, the
// `SingleVariableDesignation`/`Identifier` `Designation` subtree, and the `ObjectCreateExpression`
// used for the cross-structural-twin DoMatch rejection), recording a tag and recursing via
// `VisitChildren` (the inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitRecursivePatternExpression(RecursivePatternExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-rp>"); return; }
        trace.push_back("rp");
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
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-this>"); return; }
        trace.push_back("this");
        VisitChildren(node);
    }
    void VisitSingleVariableDesignation(SingleVariableDesignation* node) override {
        if (node == nullptr) { trace.push_back("<null-single>"); return; }
        trace.push_back("single:" + node->Identifier());
        VisitChildren(node);
    }
    void VisitObjectCreateExpression(ObjectCreateExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-oce>"); return; }
        trace.push_back("oce");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern). Uses `Match::CreateNew()` (a default-constructed `Match()` holds a NULL capture
// vector -- the D283 crux).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping a `RecursivePatternExpression` and all its children alive in the test scope
// (the port's non-owning raw-pointer child slots -- the D223 design).
struct RpHolder {
    std::unique_ptr<RecursivePatternExpression> rp;
    std::unique_ptr<SimpleType> type;
    std::unique_ptr<NullReferenceExpression> sub0;
    std::unique_ptr<ThisReferenceExpression> sub1;
    std::unique_ptr<SingleVariableDesignation> designation;
    RecursivePatternExpression* get() const { return rp.get(); }
    RecursivePatternExpression* operator->() const { return rp.get(); }
};

// Build a full `RecursivePatternExpression`: `Type` = `SimpleType("Point")`, `SubPatterns` =
// `[NullReferenceExpression, ThisReferenceExpression]`, `Designation` =
// `SingleVariableDesignation("x")`, `IsPositional` = true. The holder keeps every node alive.
RpHolder make_RpFull() {
    RpHolder h;
    h.rp = std::make_unique<RecursivePatternExpression>();
    h.type = std::make_unique<SimpleType>(std::string("Point"));
    h.rp->Type(h.type.get());
    h.sub0 = std::make_unique<NullReferenceExpression>();
    h.sub1 = std::make_unique<ThisReferenceExpression>();
    h.rp->SubPatterns().Add(h.sub0.get());
    h.rp->SubPatterns().Add(h.sub1.get());
    h.designation = std::make_unique<SingleVariableDesignation>(std::string("x"));
    h.rp->Designation(h.designation.get());
    h.rp->IsPositional(true);
    return h;
}

} // namespace

// =====================================================================
// Is-a
// =====================================================================

// `RecursivePatternExpression` is an `Expression` and an `AstNode`; it is NOT an `AstType` (it
// derives from `Expression`, parallel to -- not under -- `AstType`).
TEST(CSharp_RecursivePatternExpression, IsExpressionAndAstNodeNotAstType) {
    RecursivePatternExpression rp;
    EXPECT_NE(dynamic_cast<Expression*>(&rp), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&rp), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&rp), nullptr);
}

// `RecursivePatternExpression` is a concrete (non-abstract) and `final` class (the C# is
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to `false`).
TEST(CSharp_RecursivePatternExpression, IsConcreteAndFinal) {
    auto rp = std::make_unique<RecursivePatternExpression>();
    ASSERT_NE(rp, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(rp.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<RecursivePatternExpression>);
}

// =====================================================================
// Construction
// =====================================================================

// The empty ctor yields a node with a null `Type` (optional), an empty `SubPatterns` collection,
// a null `Designation` (optional), and `IsPositional` false.
TEST(CSharp_RecursivePatternExpression, EmptyCtorYieldsNullSlotsAndFalseIsPositional) {
    RecursivePatternExpression rp;
    EXPECT_EQ(rp.Type(), nullptr);
    EXPECT_EQ(rp.SubPatterns().Count(), 0);
    EXPECT_EQ(rp.Designation(), nullptr);
    EXPECT_FALSE(rp.IsPositional());
    EXPECT_EQ(rp.GetChildCount(), 2);  // the two single slots (empty) + 0 sub-patterns
}

// =====================================================================
// The Type slot (a nullable single child before the collection)
// =====================================================================

// The `Type` getter returns the backing field; the setter re-parents the child and records the
// flattened index 0 (the const-index `SetChildNode(ref field, value, 0)`). `Type` is OPTIONAL,
// so it defaults to null and `CheckInvariant` passes with it null.
TEST(CSharp_RecursivePatternExpression, TypeAccessorAndSetter) {
    RecursivePatternExpression rp;
    auto type = std::make_unique<SimpleType>(std::string("Point"));
    rp.Type(type.get());
    EXPECT_EQ(rp.Type(), type.get());
    EXPECT_EQ(type->Parent(), &rp);
    EXPECT_EQ(type->ChildIndex, 0);
}

// Setting `Type` to a second child re-parents the new child and clears the old child's parent.
TEST(CSharp_RecursivePatternExpression, TypeSetterReparentsAndClearsOld) {
    RecursivePatternExpression rp;
    auto t1 = std::make_unique<SimpleType>(std::string("A"));
    auto t2 = std::make_unique<SimpleType>(std::string("B"));
    rp.Type(t1.get());
    EXPECT_EQ(t1->Parent(), &rp);
    rp.Type(t2.get());
    EXPECT_EQ(rp.Type(), t2.get());
    EXPECT_EQ(t2->Parent(), &rp);
    EXPECT_EQ(t1->Parent(), nullptr);
}

// =====================================================================
// The SubPatterns collection (a non-incremental collection)
// =====================================================================

// `Add` appends an element and parents it; the collection is NON-incremental (it is the node's
// only collection but NOT its last slot -- the `Designation` single slot trails it), so `Add`
// INVALIDATES the parent's indices (unlike `Attribute`/`InvocationExpression` whose only-and-last
// collection maintains the index incrementally). The element's `ChildIndex` is stale until a
// reindex is triggered.
TEST(CSharp_RecursivePatternExpression, SubPatternsAddAppendsAndParentsAndInvalidates) {
    RecursivePatternExpression rp;
    auto s0 = std::make_unique<NullReferenceExpression>();
    auto s1 = std::make_unique<ThisReferenceExpression>();
    rp.SubPatterns().Add(s0.get());
    rp.SubPatterns().Add(s1.get());
    EXPECT_EQ(rp.SubPatterns().Count(), 2);
    EXPECT_EQ(s0->Parent(), &rp);
    EXPECT_EQ(s1->Parent(), &rp);
    EXPECT_FALSE(rp.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(rp.GetChildCount(), 4);  // type slot + 2 sub-patterns + designation slot
}

// After `Add` the parent's indices are invalid; the reindex is triggered by `Slot()` (which
// calls `EnsureChildIndices` on the parent), after which each child's `ChildIndex` is its
// correct flattened index. The `Type` single slot at index 0 counts even when null, so with no
// `Type` set: `SubPatterns[0]` at 1, `[1]` at 2 (the `Designation` slot at `1 + Count` = 3 is
// null here but still occupies its flattened position).
TEST(CSharp_RecursivePatternExpression, SubPatternsChildIndexAfterReindex) {
    RecursivePatternExpression rp;
    auto s0 = std::make_unique<NullReferenceExpression>();
    auto s1 = std::make_unique<ThisReferenceExpression>();
    rp.SubPatterns().Add(s0.get());
    rp.SubPatterns().Add(s1.get());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)s0->Slot();
    EXPECT_EQ(s0->ChildIndex, 1);  // SubPatterns[0] at 1 (the Type slot at 0 counts even when null)
    EXPECT_EQ(s1->ChildIndex, 2);  // SubPatterns[1] at 2
}

// `GetCollectionByKind` returns the `SubPatterns` collection for the `SubPattern` kind and null
// for any other kind (the `Type`/`Designation` kinds are single slots).
TEST(CSharp_RecursivePatternExpression, GetCollectionByKindReturnsSubPatternsForSubPatternKind) {
    RecursivePatternExpression rp;
    EXPECT_NE(rp.GetCollectionByKind(&Slots::SubPattern), nullptr);
    EXPECT_EQ(rp.GetCollectionByKind(&Slots::SubPattern), &rp.SubPatterns());
    EXPECT_EQ(rp.GetCollectionByKind(&Slots::Type), nullptr);              // a single slot
    EXPECT_EQ(rp.GetCollectionByKind(&Slots::VariableDesignation), nullptr);  // a single slot
    EXPECT_EQ(rp.GetCollectionByKind(nullptr), nullptr);
}

// =====================================================================
// The Designation slot (a nullable single child after the collection)
// =====================================================================

// The `Designation` defaults to null (an optional slot).
TEST(CSharp_RecursivePatternExpression, DesignationNullByDefault) {
    RecursivePatternExpression rp;
    EXPECT_EQ(rp.Designation(), nullptr);
}

// The `Designation` getter returns the backing field; the setter re-parents the child. A
// COLLECTION precedes it (`SubPatterns`), so the setter uses the index-less
// `SetChildNode(ref field, value)` (which invalidates on a set/clear, since the flattened index
// is dynamic). `Designation` is OPTIONAL.
TEST(CSharp_RecursivePatternExpression, DesignationAccessorAndSetter) {
    RecursivePatternExpression rp;
    auto desig = std::make_unique<SingleVariableDesignation>(std::string("x"));
    rp.Designation(desig.get());
    EXPECT_EQ(rp.Designation(), desig.get());
    EXPECT_EQ(desig->Parent(), &rp);
}

// Setting `Designation` to a second child re-parents the new child and clears the old child's
// parent.
TEST(CSharp_RecursivePatternExpression, DesignationSetterReparentsAndClearsOld) {
    RecursivePatternExpression rp;
    auto d1 = std::make_unique<SingleVariableDesignation>(std::string("a"));
    auto d2 = std::make_unique<SingleVariableDesignation>(std::string("b"));
    rp.Designation(d1.get());
    EXPECT_EQ(d1->Parent(), &rp);
    rp.Designation(d2.get());
    EXPECT_EQ(rp.Designation(), d2.get());
    EXPECT_EQ(d2->Parent(), &rp);
    EXPECT_EQ(d1->Parent(), nullptr);
}

// =====================================================================
// The IsPositional scalar (a non-[Slot] bool)
// =====================================================================

// The `IsPositional` scalar round-trips (the `(...)` positional form is true, the `{...}`
// property form is false).
TEST(CSharp_RecursivePatternExpression, IsPositionalScalarRoundTrip) {
    RecursivePatternExpression rp;
    EXPECT_FALSE(rp.IsPositional());
    rp.IsPositional(true);
    EXPECT_TRUE(rp.IsPositional());
    rp.IsPositional(false);
    EXPECT_FALSE(rp.IsPositional());
}

// =====================================================================
// Slot storage
// =====================================================================

// `GetChildCount` is `2 + Count` (the two single slots -- `Type` and `Designation` -- each
// count as 1 even when null, plus the `SubPatterns` collection's current length; the
// `IsPositional` bool scalar is not a slot); `GetChild`/`GetChildSlotInfo` walk the single
// `Type` slot, the `SubPatterns` collection, then the single `Designation` slot.
TEST(CSharp_RecursivePatternExpression, SlotStorageContract) {
    auto h = make_RpFull();
    EXPECT_EQ(h->GetChildCount(), 4);  // type + 2 sub-patterns + designation (the bool is not a slot)
    EXPECT_EQ(h->GetChild(0), h.type.get());
    EXPECT_EQ(h->GetChild(1), h.sub0.get());
    EXPECT_EQ(h->GetChild(2), h.sub1.get());
    EXPECT_EQ(h->GetChild(3), h.designation.get());
    EXPECT_EQ(h->GetChildSlotInfo(0), &RecursivePatternExpression::TypeSlot);
    EXPECT_EQ(h->GetChildSlotInfo(1), &RecursivePatternExpression::SubPatternsSlot);
    EXPECT_EQ(h->GetChildSlotInfo(2), &RecursivePatternExpression::SubPatternsSlot);
    EXPECT_EQ(h->GetChildSlotInfo(3), &RecursivePatternExpression::DesignationSlot);
    EXPECT_THROW((void)h->GetChild(4), std::out_of_range);
    EXPECT_THROW((void)h->GetChildSlotInfo(4), std::out_of_range);
}

// `SetChild` replaces the `Type` single slot, a `SubPatterns` collection element, and the
// `Designation` single slot in place.
TEST(CSharp_RecursivePatternExpression, SetChildReplacesSlots) {
    auto h = make_RpFull();
    auto newType = std::make_unique<SimpleType>(std::string("NewType"));
    auto newSub = std::make_unique<NullReferenceExpression>();
    auto newDesig = std::make_unique<SingleVariableDesignation>(std::string("y"));
    h->SetChild(0, newType.get());
    h->SetChild(1, newSub.get());        // SubPatterns[0]
    h->SetChild(3, newDesig.get());      // Designation (at 1 + Count = 1 + 2 = 3)
    EXPECT_EQ(h->Type(), newType.get());
    EXPECT_EQ(h->SubPatterns().At(0), newSub.get());
    EXPECT_EQ(h->Designation(), newDesig.get());
}

// =====================================================================
// Slot statics
// =====================================================================

// The per-node slot statics point at the shared `Slots` kinds (`TypeSlot` at `Slots::Type`,
// `SubPatternsSlot` at `Slots::SubPattern`, `DesignationSlot` at `Slots::VariableDesignation`).
TEST(CSharp_RecursivePatternExpression, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(RecursivePatternExpression::TypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(RecursivePatternExpression::SubPatternsSlot.Kind(), &Slots::SubPattern);
    EXPECT_EQ(RecursivePatternExpression::DesignationSlot.Kind(), &Slots::VariableDesignation);
    EXPECT_FALSE(RecursivePatternExpression::TypeSlot.IsCollection());
    EXPECT_TRUE(RecursivePatternExpression::TypeSlot.IsOptional());       // nullable Type
    EXPECT_TRUE(RecursivePatternExpression::SubPatternsSlot.IsCollection());
    EXPECT_TRUE(RecursivePatternExpression::SubPatternsSlot.IsOptional());  // collection
    EXPECT_FALSE(RecursivePatternExpression::DesignationSlot.IsCollection());
    EXPECT_TRUE(RecursivePatternExpression::DesignationSlot.IsOptional());  // nullable Designation
    // The three slot statics are distinct (distinct element types -- compare through the base).
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&RecursivePatternExpression::TypeSlot),
              static_cast<const CSharpSlotInfo*>(&RecursivePatternExpression::SubPatternsSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&RecursivePatternExpression::TypeSlot),
              static_cast<const CSharpSlotInfo*>(&RecursivePatternExpression::DesignationSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&RecursivePatternExpression::SubPatternsSlot),
              static_cast<const CSharpSlotInfo*>(&RecursivePatternExpression::DesignationSlot));
}

// The `IsInstanceOfType` is-a cross-check: `TypeSlot` accepts an `AstType` (a `SimpleType`), the
// `SubPatternsSlot` accepts an `Expression` (a `NullReferenceExpression`), the `DesignationSlot`
// accepts a `VariableDesignation` (a `SingleVariableDesignation`); a disjoint type is rejected.
TEST(CSharp_RecursivePatternExpression, SlotIsInstanceOfTypeCrossCheck) {
    auto type = std::make_unique<SimpleType>(std::string("T"));
    EXPECT_TRUE(RecursivePatternExpression::TypeSlot.IsInstanceOfType(type.get()));
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(RecursivePatternExpression::SubPatternsSlot.IsInstanceOfType(expr.get()));
    auto desig = std::make_unique<SingleVariableDesignation>(std::string("d"));
    EXPECT_TRUE(RecursivePatternExpression::DesignationSlot.IsInstanceOfType(desig.get()));
    // A `VariableDesignation` is NOT an `Expression`, so the `SubPatternsSlot` (Expression-typed)
    // rejects it.
    EXPECT_FALSE(RecursivePatternExpression::SubPatternsSlot.IsInstanceOfType(desig.get()));
}

// The children's `Slot()` reports the per-node slot static.
TEST(CSharp_RecursivePatternExpression, ChildrenSlotReportsSlotStatic) {
    auto h = make_RpFull();
    EXPECT_EQ(h->Type()->Slot(), &RecursivePatternExpression::TypeSlot);
    EXPECT_EQ(h->SubPatterns().At(0)->Slot(), &RecursivePatternExpression::SubPatternsSlot);
    EXPECT_EQ(h->SubPatterns().At(1)->Slot(), &RecursivePatternExpression::SubPatternsSlot);
    EXPECT_EQ(h->Designation()->Slot(), &RecursivePatternExpression::DesignationSlot);
}

// =====================================================================
// AcceptVisitor dispatch
// =====================================================================

// `AcceptVisitor` dispatches to `VisitRecursivePatternExpression` (through the concrete type and
// through the abstract `AstNode*`/`Expression*` bases -- the virtual dispatch). Uses a
// default-constructed (empty) node so `VisitChildren` yields just the node tag (an empty node's
// `Children()` is empty -- the D276/D288 dispatch-test precedent; a filled node would recurse
// into its slots).
TEST(CSharp_RecursivePatternExpression, AcceptVisitorDispatch) {
    RecursivePatternExpression rp;
    RecordingVisitor v;
    rp.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "rp");
}

TEST(CSharp_RecursivePatternExpression, AcceptVisitorVirtualThroughAstNodeAndExpression) {
    RecursivePatternExpression rp;
    AstNode* node = &rp;
    Expression* expr = &rp;
    RecordingVisitor v1;
    node->AcceptVisitor(v1);
    ASSERT_EQ(v1.trace.size(), 1u);
    EXPECT_EQ(v1.trace[0], "rp");
    RecordingVisitor v2;
    expr->AcceptVisitor(v2);
    ASSERT_EQ(v2.trace.size(), 1u);
    EXPECT_EQ(v2.trace[0], "rp");
}

// =====================================================================
// Depth-first walk
// =====================================================================

// The depth-first walk visits the `Type` (a `SimpleType` recursing into its backing
// `IdentifierToken`), the `SubPatterns` in document order, then the `Designation` (a
// `SingleVariableDesignation` recursing into its backing `IdentifierToken`).
TEST(CSharp_RecursivePatternExpression, DepthFirstWalkFull) {
    auto h = make_RpFull();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // rp, simple:Point, id:Point (Type), nullref (SubPatterns[0]), this (SubPatterns[1]),
    // single:x, id:x (Designation)
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "rp", "simple:Point", "id:Point", "nullref", "this", "single:x", "id:x"}));
}

// An empty `RecursivePatternExpression` (no `Type`, no `SubPatterns`, no `Designation`) records
// just the node tag.
TEST(CSharp_RecursivePatternExpression, DepthFirstWalkEmpty) {
    RecursivePatternExpression rp;
    RecordingVisitor v;
    rp.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "rp");
}

// =====================================================================
// DoMatch
// =====================================================================

// Two `RecursivePatternExpression`s with equal `Type`/`SubPatterns`/`Designation`/`IsPositional`
// match.
TEST(CSharp_RecursivePatternExpression, DoMatchMatchesSame) {
    auto a = make_RpFull();
    auto b = make_RpFull();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `Type` asymmetry rejects (the pattern has a `Type`, the candidate does not -- the
// `MatchOptional` `Type` term).
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsTypeAsymmetry) {
    auto a = make_RpFull();  // Type = SimpleType("Point")
    auto b = make_RpFull();
    b.rp->Type(nullptr);  // candidate has no Type
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Type` value mismatch rejects (the recursive term delegates to the `Type`'s `DoMatch`).
// Set the new `Type` first (which detaches the old), then transfer ownership to the holder -- the
// old `Type` is detached before its unique_ptr is reset, so the parent holds no dangling pointer.
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsTypeMismatch) {
    auto a = make_RpFull();  // Type = SimpleType("Point")
    auto b = make_RpFull();
    auto newType = std::make_unique<SimpleType>(std::string("Other"));
    b.rp->Type(newType.get());  // detaches old b.type (Point), attaches Other
    b.type = std::move(newType);  // transfer ownership; old detached Point destroyed safely
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `SubPatterns` count mismatch rejects (the collection recursive `DoMatch`).
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsSubPatternsCountMismatch) {
    auto a = make_RpFull();  // 2 sub-patterns
    auto b = make_RpFull();
    b.rp->SubPatterns().Clear();  // 0 sub-patterns
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `SubPatterns` element mismatch rejects (the recursive term delegates to the element's
// `DoMatch`). The different-type element is held in its own local unique_ptr (a
// `unique_ptr<ThisReferenceExpression>` holder member cannot be assigned a
// `BaseReferenceExpression` -- the D265 sibling-type unique_ptr crux).
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsSubPatternsElementMismatch) {
    auto a = make_RpFull();  // SubPatterns = [NullRef, This]
    auto b = make_RpFull();
    auto diffSub = std::make_unique<BaseReferenceExpression>();  // differs from a.sub1 (ThisRef)
    b.rp->SubPatterns().Clear();          // detaches b.sub0, b.sub1
    b.rp->SubPatterns().Add(b.sub0.get());  // re-add NullRef (matches a.sub0)
    b.rp->SubPatterns().Add(diffSub.get()); // add BaseRef (differs from a.sub1)
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Designation` asymmetry rejects (the pattern has a `Designation`, the candidate does not --
// the `MatchOptional` `Designation` term).
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsDesignationAsymmetry) {
    auto a = make_RpFull();  // Designation = SingleVariableDesignation("x")
    auto b = make_RpFull();
    b.rp->Designation(nullptr);  // candidate has no Designation
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// A `Designation` value mismatch rejects (the recursive term delegates to the `Designation`'s
// `DoMatch`). Set the new `Designation` first (which detaches the old), then transfer ownership
// to the holder -- the old `Designation` is detached before its unique_ptr is reset.
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsDesignationMismatch) {
    auto a = make_RpFull();  // Designation = SingleVariableDesignation("x")
    auto b = make_RpFull();
    auto newDesig = std::make_unique<SingleVariableDesignation>(std::string("y"));  // different name
    b.rp->Designation(newDesig.get());  // detaches old b.designation (x), attaches y
    b.designation = std::move(newDesig);  // transfer ownership; old detached x destroyed safely
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// An `IsPositional` mismatch rejects (the plain-bool term).
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsIsPositionalMismatch) {
    auto a = make_RpFull();  // IsPositional = true
    auto b = make_RpFull();
    b.rp->IsPositional(false);  // different
    EXPECT_FALSE(DoMatchAgainst(a.get(), b.get()));
}

// Two bare recursive patterns (no `Type`, no `SubPatterns`, no `Designation`, `IsPositional`
// false) match: both `MatchOptional` terms return true when both sides are absent, the empty
// collection recursive `DoMatch` matches, and the plain-bool matches.
TEST(CSharp_RecursivePatternExpression, DoMatchBarePatternsMatch) {
    RecursivePatternExpression a, b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A non-`RecursivePatternExpression` candidate rejects (the type-check gate). Uses a
// `NullReferenceExpression` (a different `Expression`).
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsNonRecursiveCandidate) {
    auto a = make_RpFull();
    NullReferenceExpression other;
    EXPECT_FALSE(DoMatchAgainst(a.get(), &other));
}

// A cross-structural-twin `ObjectCreateExpression` candidate rejects (the type-check gate) --
// the two share the single + non-incremental collection + trailing-nullable-single shape but are
// distinct concrete types.
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsObjectCreateCandidate) {
    auto a = make_RpFull();
    auto oceType = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression other(oceType.get());
    EXPECT_FALSE(DoMatchAgainst(a.get(), &other));
}

// A null candidate rejects.
TEST(CSharp_RecursivePatternExpression, DoMatchRejectsNullCandidate) {
    auto a = make_RpFull();
    EXPECT_FALSE(DoMatchAgainst(a.get(), nullptr));
}

// =====================================================================
// Clone
// =====================================================================

// `Clone` deep-copies the `Type`, every `SubPatterns` element, and the `Designation` (distinct
// re-parented nodes), copies the `IsPositional` scalar, is virtual through `AstNode*`/
// `Expression*`, and returns `RecursivePatternExpression*` covariantly.
TEST(CSharp_RecursivePatternExpression, CloneDeepCopiesAllSlotsAndScalar) {
    auto h = make_RpFull();
    std::unique_ptr<RecursivePatternExpression> copy(h->Clone());
    ASSERT_NE(copy, nullptr);
    // Type deep-copied.
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), h->Type());
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    EXPECT_EQ(dynamic_cast<SimpleType*>(copy->Type())->Identifier().value_or(""), "Point");
    // SubPatterns deep-copied (2 distinct re-parented elements).
    EXPECT_EQ(copy->SubPatterns().Count(), 2);
    EXPECT_NE(copy->SubPatterns().At(0), h.sub0.get());
    EXPECT_NE(copy->SubPatterns().At(1), h.sub1.get());
    EXPECT_EQ(copy->SubPatterns().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->SubPatterns().At(1)->Parent(), copy.get());
    // Designation deep-copied.
    ASSERT_NE(copy->Designation(), nullptr);
    EXPECT_NE(copy->Designation(), h->Designation());
    EXPECT_EQ(copy->Designation()->Parent(), copy.get());
    EXPECT_EQ(dynamic_cast<SingleVariableDesignation*>(copy->Designation())->Identifier(), "x");
    // IsPositional scalar copied.
    EXPECT_TRUE(copy->IsPositional());
    // The source children keep their original parent.
    EXPECT_EQ(h->Type()->Parent(), h.get());
    EXPECT_EQ(h.sub0->Parent(), h.get());
    EXPECT_EQ(h.sub1->Parent(), h.get());
    EXPECT_EQ(h->Designation()->Parent(), h.get());
}

TEST(CSharp_RecursivePatternExpression, CloneVirtualAndCovariant) {
    auto h = make_RpFull();
    AstNode* node = h.get();
    std::unique_ptr<AstNode> copy(node->Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(dynamic_cast<RecursivePatternExpression*>(copy.get()), nullptr);
    Expression* exprCopy = dynamic_cast<Expression*>(copy.get());
    ASSERT_NE(exprCopy, nullptr);
    // The covariant Clone through Expression* returns RecursivePatternExpression* (the
    // Expression::Clone pure-virtual).
    std::unique_ptr<RecursivePatternExpression> typed(
        dynamic_cast<RecursivePatternExpression*>(exprCopy->Clone()));
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->SubPatterns().Count(), 2);
    EXPECT_TRUE(typed->IsPositional());
}

// A `Clone` of a bare pattern (no `Type`/`SubPatterns`/`Designation`) yields a bare clone.
TEST(CSharp_RecursivePatternExpression, CloneBarePattern) {
    RecursivePatternExpression rp;
    std::unique_ptr<RecursivePatternExpression> copy(rp.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->SubPatterns().Count(), 0);
    EXPECT_EQ(copy->Designation(), nullptr);
    EXPECT_FALSE(copy->IsPositional());
}

// =====================================================================
// CheckInvariant
// =====================================================================

// A full `RecursivePatternExpression` passes the inherited `CheckInvariant`.
TEST(CSharp_RecursivePatternExpression, CheckInvariantPassesOnFullNode) {
    auto h = make_RpFull();
    h->CheckInvariant();
}

// An EMPTY `RecursivePatternExpression` passes `CheckInvariant` -- all the single slots are
// NULLABLE (`Type`/`Designation`), and a collection is never a required slot, so no required
// slot is empty. (Distinct from `ObjectCreateExpression` whose required `Type` rejects an empty
// node -- here `Type` is optional.)
TEST(CSharp_RecursivePatternExpression, CheckInvariantPassesOnEmptyNode) {
    RecursivePatternExpression rp;  // all slots null/empty
    rp.CheckInvariant();
}
