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

// Tests for the `NamespaceDeclaration` concrete node (cpp/.../Syntax/NamespaceDeclaration.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/NamespaceDeclaration.cs) -- the
// `namespace_declaration` node (C# grammar 14.3): an `IsFileScoped` bool scalar + a REQUIRED
// `AstType` `NamespaceName` single slot + an `AstNodeCollection<AstNode>` `Members` collection
// (the namespace body, the first `AstNode`-root-typed collection). The next in-order Phase-5 piece
// per the D292 plan. The suite shares a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234
// pattern). The node is structurally the `Constraint` D283 shape (single required child at index 0 +
// incremental collection at index 1) with a direct-`AstNode` base, an `IsFileScoped` bool scalar
// declared before the slots, the child `AstType`, and the collection element the abstract `AstNode`
// root.

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
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `VisitNamespaceDeclaration` under test (plus the
// `SimpleType`/`Identifier` of its `NamespaceName` and `Members`, and the `WhileStatement` used for
// the cross-type is-a check), recording a tag and recursing via `VisitChildren` (the inherited
// depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitNamespaceDeclaration(NamespaceDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-ns>"); return; }
        trace.push_back("namespace");
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

// `NamespaceDeclaration` derives directly from `AstNode`; it is disjoint from
// `Expression`/`Statement`/`AstType`/`EntityDeclaration` (the `VariableInitializer` D266 /
// `CatchClause` D269 / `Constraint` D283 / `TypeParameterDeclaration` D282 direct-`AstNode`
// precedent).
TEST(CSharp_NamespaceDeclaration, IsAstNodeNotExpressionNotStatementNotAstType) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    EXPECT_TRUE(dynamic_cast<AstNode*>(&ns) != nullptr);
    EXPECT_FALSE(dynamic_cast<Expression*>(&ns) != nullptr);
    EXPECT_FALSE(dynamic_cast<AstType*>(&ns) != nullptr);
    EXPECT_FALSE(dynamic_cast<EntityDeclaration*>(&ns) != nullptr);
}

// `NamespaceDeclaration` is `final` (the C# `sealed`).
TEST(CSharp_NamespaceDeclaration, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<NamespaceDeclaration>);
    EXPECT_FALSE(std::is_abstract_v<NamespaceDeclaration>);
}

// ---- construction -------------------------------------------------------

// The generated empty ctor leaves `NamespaceName` null, `Members` empty, and `IsFileScoped` false.
TEST(CSharp_NamespaceDeclaration, EmptyCtorHasNullNameEmptyMembersFalseFileScoped) {
    NamespaceDeclaration ns;
    EXPECT_EQ(ns.NamespaceName(), nullptr);
    EXPECT_EQ(ns.Members().Count(), 0);
    EXPECT_FALSE(ns.IsFileScoped());
    EXPECT_EQ(ns.GetChildCount(), 1);  // 1 single slot (null) + 0 collection elements
}

// The generated required-prefix ctor sets `NamespaceName`.
TEST(CSharp_NamespaceDeclaration, CtorSetsNamespaceName) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    EXPECT_EQ(ns.NamespaceName(), name.get());
    EXPECT_EQ(name->Parent(), &ns);
    EXPECT_EQ(name->ChildIndex, 0);
}

// ---- the `IsFileScoped` scalar (a plain bool, not a `[Slot]`) ------------

// `IsFileScoped` round-trips (the C# 10 file-scoped `namespace Foo.Bar;` form).
TEST(CSharp_NamespaceDeclaration, IsFileScopedRoundTrips) {
    NamespaceDeclaration ns;
    EXPECT_FALSE(ns.IsFileScoped());
    ns.IsFileScoped(true);
    EXPECT_TRUE(ns.IsFileScoped());
    ns.IsFileScoped(false);
    EXPECT_FALSE(ns.IsFileScoped());
}

// ---- the `NamespaceName` slot (a single REQUIRED `AstType` child) -------

// The setter re-parents and detaches the previous child.
TEST(CSharp_NamespaceDeclaration, NamespaceNameSetterReparentsAndDetaches) {
    auto a = std::make_unique<SimpleType>("A");
    NamespaceDeclaration ns(a.get());
    EXPECT_EQ(a->Parent(), &ns);
    auto b = std::make_unique<SimpleType>("B");
    ns.NamespaceName(b.get());
    EXPECT_EQ(ns.NamespaceName(), b.get());
    EXPECT_EQ(b->Parent(), &ns);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetChildNode
}

// The setter clears with null.
TEST(CSharp_NamespaceDeclaration, NamespaceNameSetterClearsWithNull) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    ns.NamespaceName(nullptr);
    EXPECT_EQ(ns.NamespaceName(), nullptr);
    EXPECT_EQ(name->Parent(), nullptr);
}

// ---- the `Members` collection (incremental) ----------------------------

// `Add` appends, re-parents, and maintains `ChildIndex` incrementally (the collection is the
// node's only collection and its last slot, so `supportsIncremental` is true).
TEST(CSharp_NamespaceDeclaration, MembersAddAppendsParentsAndMaintainsChildIndex) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    auto m0 = std::make_unique<SimpleType>("A");
    auto m1 = std::make_unique<SimpleType>("B");
    ns.Members().Add(m0.get());
    ns.Members().Add(m1.get());
    EXPECT_EQ(ns.Members().Count(), 2);
    EXPECT_EQ(m0->Parent(), &ns);
    EXPECT_EQ(m1->Parent(), &ns);
    EXPECT_EQ(m0->ChildIndex, 1);  // incremental: 1 + 0
    EXPECT_EQ(m1->ChildIndex, 2);  // incremental: 1 + 1
    EXPECT_EQ(ns.GetChildCount(), 3);  // 1 NamespaceName + 2 members
}

// `AddMember` (the hand-written convenience) appends via `AddChild(child, Slots.Member)`.
TEST(CSharp_NamespaceDeclaration, AddMemberAppendsViaSlotsMember) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    auto m = std::make_unique<SimpleType>("A");
    ns.AddMember(m.get());
    EXPECT_EQ(ns.Members().Count(), 1);
    EXPECT_EQ(ns.Members().At(0), m.get());
    EXPECT_EQ(m->Parent(), &ns);
    EXPECT_EQ(m->ChildIndex, 1);
}

// `AddMember` is a no-op on null (the `AddChild` null guard).
TEST(CSharp_NamespaceDeclaration, AddMemberNullIsNoOp) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    ns.AddMember(nullptr);
    EXPECT_EQ(ns.Members().Count(), 0);
}

// `GetCollectionByKind` returns the `Members` collection for the `Member` kind.
TEST(CSharp_NamespaceDeclaration, GetCollectionByKindReturnsMembersForMemberKind) {
    NamespaceDeclaration ns;
    EXPECT_EQ(ns.GetCollectionByKind(&Slots::Member), &ns.Members());
    EXPECT_EQ(ns.GetCollectionByKind(&Slots::NamespaceName), nullptr);  // a single slot
    EXPECT_EQ(ns.GetCollectionByKind(&Slots::TypeArgument), nullptr);  // an unrelated kind
    EXPECT_EQ(ns.GetCollectionByKind(nullptr), nullptr);
}

// ---- slot storage (single -> collection) --------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single slot then the collection. A namespace
// with a name and two members reports `GetChildCount` 3.
TEST(CSharp_NamespaceDeclaration, CollectionAwareSlotStorage) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    auto m0 = std::make_unique<SimpleType>("A");
    auto m1 = std::make_unique<SimpleType>("B");
    ns.Members().Add(m0.get());
    ns.Members().Add(m1.get());
    EXPECT_EQ(ns.GetChildCount(), 3);
    EXPECT_EQ(ns.GetChild(0), name.get());
    EXPECT_EQ(ns.GetChild(1), m0.get());
    EXPECT_EQ(ns.GetChild(2), m1.get());
    EXPECT_EQ(ns.GetChildSlotInfo(0), &NamespaceDeclaration::NamespaceNameSlot);
    EXPECT_EQ(ns.GetChildSlotInfo(1), &NamespaceDeclaration::MembersSlot);
    EXPECT_EQ(ns.GetChildSlotInfo(2), &NamespaceDeclaration::MembersSlot);
    EXPECT_THROW(ns.GetChild(3), std::out_of_range);
}

// `SetChild` replaces the `NamespaceName` (index 0) or a collection element (index >= 1).
TEST(CSharp_NamespaceDeclaration, SetChildReplacesNamespaceNameAndMember) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    auto m0 = std::make_unique<SimpleType>("A");
    ns.Members().Add(m0.get());
    auto newName = std::make_unique<SimpleType>("Bar");
    ns.SetChild(0, newName.get());
    EXPECT_EQ(ns.NamespaceName(), newName.get());
    EXPECT_EQ(newName->Parent(), &ns);
    EXPECT_EQ(name->Parent(), nullptr);  // detached by SetChildNode
    auto newM = std::make_unique<SimpleType>("C");
    ns.SetChild(1, newM.get());
    EXPECT_EQ(ns.Members().At(0), newM.get());
    EXPECT_EQ(newM->Parent(), &ns);
}

// `GetChildSlotInfo` throws past the end.
TEST(CSharp_NamespaceDeclaration, GetChildSlotInfoThrowsOutOfRange) {
    NamespaceDeclaration ns;
    EXPECT_THROW(ns.GetChildSlotInfo(1), std::out_of_range);  // empty: only index 0 valid
}

// ---- slot statics (pointing at the shared `Slots` kinds) ----------------

// `NamespaceNameSlot` points at `Slots::NamespaceName`; `MembersSlot` points at `Slots::Member`.
TEST(CSharp_NamespaceDeclaration, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(NamespaceDeclaration::NamespaceNameSlot.Kind(), &Slots::NamespaceName);
    EXPECT_EQ(NamespaceDeclaration::MembersSlot.Kind(), &Slots::Member);
}

// `NamespaceNameSlot` accepts any `AstType`; `MembersSlot` accepts any `AstNode` (both a
// `SimpleType` and -- via the `AstNode` root -- a `SimpleType`, the first `AstNode`-typed
// collection kind).
TEST(CSharp_NamespaceDeclaration, SlotIsInstanceOfTypeCrossCheck) {
    auto simple = std::make_unique<SimpleType>("Foo");
    EXPECT_TRUE(NamespaceDeclaration::NamespaceNameSlot.IsInstanceOfType(simple.get()));
    EXPECT_TRUE(NamespaceDeclaration::MembersSlot.IsInstanceOfType(simple.get()));
}

// The two slot statics are distinct (cross-element-type comparison via the common
// `CSharpSlotInfo*` base -- the D251/D252 EXPECT_NE precedent).
TEST(CSharp_NamespaceDeclaration, SlotStaticDistinctFromUnrelatedKinds) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&NamespaceDeclaration::NamespaceNameSlot),
              static_cast<const CSharpSlotInfo*>(&NamespaceDeclaration::MembersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&NamespaceDeclaration::NamespaceNameSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::Type));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&NamespaceDeclaration::MembersSlot),
              static_cast<const CSharpSlotInfo*>(&Slots::ResourceAcquisition));
}

// ---- AcceptVisitor (the dispatch) ---------------------------------------

// `AcceptVisitor` dispatches to `VisitNamespaceDeclaration`; the depth-first walk visits the
// `NamespaceName` (single slot, recursing into the `SimpleType`'s backing `Identifier`) then the
// `Members` (each `AstNode` recursing as appropriate).
TEST(CSharp_NamespaceDeclaration, AcceptVisitorDispatchesAndWalks) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    auto m0 = std::make_unique<SimpleType>("A");
    auto m1 = std::make_unique<SimpleType>("B");
    ns.Members().Add(m0.get());
    ns.Members().Add(m1.get());
    RecordingVisitor v;
    ns.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "namespace",
        "simple:Foo", "id:Foo",            // the NamespaceName SimpleType (single slot) -> its NameToken
        "simple:A", "id:A",                 // Members[0] (a SimpleType) -> its NameToken
        "simple:B", "id:B"}));              // Members[1] (a SimpleType) -> its NameToken
}

// `AcceptVisitor` is virtual through an `AstNode*`.
TEST(CSharp_NamespaceDeclaration, AcceptVisitorIsVirtualThroughAstNode) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    AstNode* asAst = &ns;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"namespace", "simple:Foo", "id:Foo"}));
}

// ---- DoMatch (the generated plain-bool + required-recursive + collection match) ---------

// Two `NamespaceDeclaration`s with the same `IsFileScoped`, same `NamespaceName`, and empty
// `Members` match.
TEST(CSharp_NamespaceDeclaration, DoMatchMatchesSame) {
    auto na = std::make_unique<SimpleType>("Foo");
    auto nb = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration a(na.get());
    NamespaceDeclaration b(nb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `NamespaceDeclaration`s with a different `IsFileScoped` reject (the plain-`==` is the FIRST
// term, so it rejects before the `NamespaceName` is observed).
TEST(CSharp_NamespaceDeclaration, DoMatchRejectsDifferentIsFileScoped) {
    auto na = std::make_unique<SimpleType>("Foo");
    auto nb = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration a(na.get());
    NamespaceDeclaration b(nb.get());
    a.IsFileScoped(true);
    b.IsFileScoped(false);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `NamespaceDeclaration`s with a different `NamespaceName` reject.
TEST(CSharp_NamespaceDeclaration, DoMatchRejectsDifferentNamespaceName) {
    auto na = std::make_unique<SimpleType>("Foo");
    auto nb = std::make_unique<SimpleType>("Bar");
    NamespaceDeclaration a(na.get());
    NamespaceDeclaration b(nb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `NamespaceDeclaration`s with matching `IsFileScoped`/`NamespaceName` but a `Members` count
// mismatch reject.
TEST(CSharp_NamespaceDeclaration, DoMatchRejectsMembersCountMismatch) {
    auto na = std::make_unique<SimpleType>("Foo");
    auto nb = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration a(na.get());
    NamespaceDeclaration b(nb.get());
    auto m = std::make_unique<SimpleType>("A");
    a.Members().Add(m.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `NamespaceDeclaration`s with matching `IsFileScoped`/`NamespaceName` and matching `Members`
// match.
TEST(CSharp_NamespaceDeclaration, DoMatchMatchesWithMatchingMembers) {
    auto na = std::make_unique<SimpleType>("Foo");
    auto nb = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration a(na.get());
    NamespaceDeclaration b(nb.get());
    auto a0 = std::make_unique<SimpleType>("A");
    auto b0 = std::make_unique<SimpleType>("A");
    a.Members().Add(a0.get());
    b.Members().Add(b0.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `NamespaceDeclaration`s with matching `IsFileScoped`/`NamespaceName` but a `Members` value
// mismatch reject.
TEST(CSharp_NamespaceDeclaration, DoMatchRejectsMembersValueMismatch) {
    auto na = std::make_unique<SimpleType>("Foo");
    auto nb = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration a(na.get());
    NamespaceDeclaration b(nb.get());
    auto a0 = std::make_unique<SimpleType>("A");
    auto b0 = std::make_unique<SimpleType>("B");
    a.Members().Add(a0.get());
    b.Members().Add(b0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `NamespaceDeclaration` pattern rejects a non-`NamespaceDeclaration` candidate.
TEST(CSharp_NamespaceDeclaration, DoMatchRejectsNonNamespaceCandidate) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration pattern(name.get());
    auto other = std::make_unique<SimpleType>("Foo");  // an AstType, not a NamespaceDeclaration
    EXPECT_FALSE(DoMatchAgainst(&pattern, other.get()));
}

// A `NamespaceDeclaration` pattern rejects a `null` candidate.
TEST(CSharp_NamespaceDeclaration, DoMatchRejectsNullCandidate) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration pattern(name.get());
    EXPECT_FALSE(DoMatchAgainst(&pattern, nullptr));
}

// ---- Clone ---------------------------------------------------------------

// `Clone` deep-copies the `IsFileScoped` scalar, the `NamespaceName`, and the `Members` collection.
TEST(CSharp_NamespaceDeclaration, CloneDeepCopiesScalarNameAndMembers) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    ns.IsFileScoped(true);
    auto m0 = std::make_unique<SimpleType>("A");
    auto m1 = std::make_unique<SimpleType>("B");
    ns.Members().Add(m0.get());
    ns.Members().Add(m1.get());

    std::unique_ptr<NamespaceDeclaration> copy(ns.Clone());
    EXPECT_TRUE(copy->IsFileScoped());
    ASSERT_NE(copy->NamespaceName(), nullptr);
    ASSERT_NE(copy->NamespaceName(), name.get());  // deep copy, not the same node
    // `NamespaceName()` returns the abstract `AstType*`; downcast to `SimpleType*` to read the
    // name (the clone is a `SimpleType` since the source `NamespaceName` was a `SimpleType`).
    EXPECT_EQ(*dynamic_cast<SimpleType*>(copy->NamespaceName())->Identifier(), "Foo");
    EXPECT_EQ(copy->Members().Count(), 2);
    EXPECT_NE(copy->Members().At(0), m0.get());
    EXPECT_NE(copy->Members().At(1), m1.get());
    // `Members().At(i)` returns the abstract `AstNode*`; downcast to `SimpleType*` to read the
    // name (the clone elements are `SimpleType`s since the source members were `SimpleType`s).
    EXPECT_EQ(*dynamic_cast<SimpleType*>(copy->Members().At(0))->Identifier(), "A");
    EXPECT_EQ(*dynamic_cast<SimpleType*>(copy->Members().At(1))->Identifier(), "B");
    // The cloned children are re-parented to the copy.
    EXPECT_EQ(copy->NamespaceName()->Parent(), copy.get());
    EXPECT_EQ(copy->Members().At(0)->Parent(), copy.get());
    EXPECT_EQ(copy->Members().At(1)->Parent(), copy.get());
}

// `Clone` is virtual through `AstNode*` and returns a covariant `NamespaceDeclaration*`.
TEST(CSharp_NamespaceDeclaration, CloneIsVirtualAndCovariant) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    AstNode* asAst = &ns;
    std::unique_ptr<AstNode> copy(asAst->Clone());
    EXPECT_NE(dynamic_cast<NamespaceDeclaration*>(copy.get()), nullptr);
}

// `Clone` does not detach the source's children.
TEST(CSharp_NamespaceDeclaration, CloneDoesNotDetachSource) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    auto m0 = std::make_unique<SimpleType>("A");
    ns.Members().Add(m0.get());
    std::unique_ptr<NamespaceDeclaration> copy(ns.Clone());
    EXPECT_EQ(name->Parent(), &ns);      // source intact
    EXPECT_EQ(m0->Parent(), &ns);
}

// ---- CheckInvariant -------------------------------------------------------

// `CheckInvariant` passes on a filled node (the `NamespaceName` required slot is filled).
TEST(CSharp_NamespaceDeclaration, CheckInvariantPassesOnFilledNode) {
    auto name = std::make_unique<SimpleType>("Foo");
    NamespaceDeclaration ns(name.get());
    ns.CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (the `NamespaceName` is a REQUIRED slot, so a
// default-constructed node violates the required-slot invariant -- the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_NamespaceDeclaration, CheckInvariantRejectsEmpty) {
    NamespaceDeclaration ns;  // no NamespaceName
    EXPECT_DEATH(ns.CheckInvariant(), "");
}
#endif

} // namespace
