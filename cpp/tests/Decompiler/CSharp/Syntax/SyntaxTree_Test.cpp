// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the Software
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
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the `SyntaxTree` concrete node (cpp/.../Syntax/SyntaxTree.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/SyntaxTree.cs) -- the root `compilation_unit` node and
// the last remaining GeneralScope Phase-5 piece per the D314 plan. The
// `ArrayInitializerExpression` D250 / `AnonymousTypeCreateExpression` D304 collection-only shape
// applied to a direct-`AstNode` root: a sealed `AstNode` whose sole child slot is the `Members`
// `AstNodeCollection<AstNode>` collection (the compilation-unit body), reusing the already-ported
// `Slots::Member` kind. Exercises the `Members` collection, the collection-only slot-storage
// contract (an empty node reports `GetChildCount` 0), the `AcceptVisitor` dispatch, the generated
// `DoMatch` (a single collection `DoMatch` term), the per-concrete-node `Clone` (deep-copying
// `AstNode`-root-typed elements with no `static_cast`), and the inherited `CheckInvariant`
// (which passes on the empty node -- there are no required single slots).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/ExternAliasDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousTypeCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls (the role the generated
// per-node `Visit` overrides play) with a tag distinguishing the concrete types, recursing via
// the inherited `VisitChildren` (the document-order walk). Only the types appearing in the
// walk traces below are overridden; the rest inherit the `DepthFirstAstVisitor` `VisitChildren`
// defaults.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitSyntaxTree(SyntaxTree* node) override {
        if (node == nullptr) { trace.push_back("<null-tree>"); return; }
        trace.push_back("tree");
        VisitChildren(node);
    }
    void VisitExternAliasDeclaration(ExternAliasDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-externalias>"); return; }
        trace.push_back("externalias");
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `SyntaxTree` is an `AstNode` (the `dynamic_cast` is-a the slot system and the annotation
// channel use); it derives DIRECTLY from the `AstNode` root, so it is NOT an `Expression`, a
// `Statement`, or an `AstType` (it is a root container, not a member declaration, expression,
// statement, or type -- the `NamespaceDeclaration` D293 / `Constraint` D283 disjoint-hierarchy
// precedent applied to the compilation-unit root).
TEST(CSharp_SyntaxTree, IsAstNodeNotExpressionStatementAstType) {
    SyntaxTree st;
    EXPECT_NE(dynamic_cast<AstNode*>(&st), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&st), nullptr);
    EXPECT_EQ(dynamic_cast<Statement*>(&st), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&st), nullptr);
}

// `SyntaxTree` is a concrete (non-abstract) and `final` class (the C# is `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it). It is constructible directly.
TEST(CSharp_SyntaxTree, IsConcreteAndFinal) {
    auto st = std::make_unique<SyntaxTree>();
    ASSERT_NE(st, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(st.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<SyntaxTree>);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no initializers. `GetChildCount` is 0 (the node has no single slots -- the
// only slot is the `Members` collection, which is empty). `StartLocation` is the default empty
// location (the output visitor sets the print-time span later).
TEST(CSharp_SyntaxTree, EmptyCtorHasNoMembers) {
    SyntaxTree st;
    EXPECT_EQ(st.Members().Count(), 0);
    EXPECT_EQ(st.GetChildCount(), 0);  // no single slots + 0 members
    EXPECT_EQ(st.StartLocation(), TextLocation::Empty);
}

// ---- The `Members` collection ----------------------------------------

// The collection starts empty (0 count); the node's child count is 0 (no single slots).
TEST(CSharp_SyntaxTree, MembersEmptyByDefault) {
    SyntaxTree st;
    EXPECT_EQ(st.Members().Count(), 0);
    EXPECT_EQ(st.GetChildCount(), 0);
}

// `Add` appends a member, parents it, and assigns its flattened `ChildIndex` incrementally (the
// collection is the node's only collection at its last/only slot, so an element's index is its
// local position -- baseIndex 0). The members are `AstNode`-root-typed, so any `AstNode`-derived
// top-level directive/declaration is accepted (here an `ExternAliasDeclaration` -- a real
// `extern alias` directive, a valid top-level compilation-unit member).
TEST(CSharp_SyntaxTree, MembersAddAppendsAndParentsIncremental) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    auto b = std::make_unique<ExternAliasDeclaration>(std::string("b"));
    st.Members().Add(a.get());
    st.Members().Add(b.get());
    EXPECT_EQ(st.Members().Count(), 2);
    EXPECT_EQ(a->Parent(), &st);
    EXPECT_EQ(b->Parent(), &st);
    EXPECT_EQ(a->ChildIndex, 0);  // baseIndex 0 + 0
    EXPECT_EQ(b->ChildIndex, 1);  // baseIndex 0 + 1
    EXPECT_EQ(st.GetChildCount(), 2);  // 2 members, no single slots
    EXPECT_TRUE(st.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Members` collection for the `Member` kind (the shared
// `Slots::Member`, reused as the collection kind -- the second reuse after `NamespaceDeclaration`
// D293) and null for any other kind.
TEST(CSharp_SyntaxTree, GetCollectionByKindReturnsMembersForMemberKind) {
    SyntaxTree st;
    EXPECT_NE(st.GetCollectionByKind(&Slots::Member), nullptr);
    EXPECT_EQ(st.GetCollectionByKind(&Slots::Member), &st.Members());
    EXPECT_EQ(st.GetCollectionByKind(&Slots::Statement), nullptr);  // a different kind
    EXPECT_EQ(st.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-only dispatch) -----------

// `GetChild` returns the members at index 0..Count; the collection occupies the contiguous
// range [0, Count). An empty node throws for any index (there are no slots at all).
TEST(CSharp_SyntaxTree, GetChildDispatchesMembers) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    auto b = std::make_unique<ExternAliasDeclaration>(std::string("b"));
    st.Members().Add(a.get());
    st.Members().Add(b.get());
    EXPECT_EQ(st.GetChild(0), a.get());
    EXPECT_EQ(st.GetChild(1), b.get());
    EXPECT_THROW(st.GetChild(2), std::out_of_range);
    EXPECT_THROW(st.GetChild(-1), std::out_of_range);
}

// An empty node throws for every index (no single slots, no members).
TEST(CSharp_SyntaxTree, GetChildThrowsOnEmptyNode) {
    SyntaxTree st;
    EXPECT_THROW(st.GetChild(0), std::out_of_range);
}

// `GetChildSlotInfo` returns the `MembersSlot` at index 0..Count (the slot identity the slot
// system compares by address).
TEST(CSharp_SyntaxTree, GetChildSlotInfoDispatchesMembers) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    st.Members().Add(a.get());
    EXPECT_EQ(st.GetChildSlotInfo(0), &SyntaxTree::MembersSlot);
    EXPECT_THROW(st.GetChildSlotInfo(1), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Member` KIND (the shared `Slots.Member`, reused as the
// collection kind).
TEST(CSharp_SyntaxTree, MembersSlotPointsAtMemberKind) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    st.Members().Add(a.get());
    EXPECT_EQ(st.GetChildSlotInfo(0)->Kind(), &Slots::Member);
}

// `SetChild` replaces a member in place at index 0..Count (the collection's `SetAt` re-parents
// and carries the old index).
TEST(CSharp_SyntaxTree, SetChildDispatchesMembers) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    st.Members().Add(a.get());
    // Replace the member at index 0.
    auto b = std::make_unique<ExternAliasDeclaration>(std::string("b"));
    st.SetChild(0, b.get());
    EXPECT_EQ(st.Members().At(0), b.get());
    EXPECT_EQ(b->Parent(), &st);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetAt
    EXPECT_THROW(st.SetChild(1, nullptr), std::out_of_range);  // no element at index 1
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitSyntaxTree` (the visitor-pattern round-trip); the
// depth-first walk then visits the `Members` children -- each `ExternAliasDeclaration` and its
// backing `NameToken` `Identifier` (the recurring "string-name [Slot] backing token is a visited
// child" gotcha from D259/D266).
TEST(CSharp_SyntaxTree, AcceptVisitorDispatchesToVisitSyntaxTree) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    st.Members().Add(a.get());
    RecordingVisitor v;
    st.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"tree", "externalias", "id:x"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` (the dynamic dispatch the output visitor
// relies on). `SyntaxTree` derives directly from `AstNode` (no `Expression`/`Statement`
// intermediate), so the only base tested is `AstNode*`.
TEST(CSharp_SyntaxTree, AcceptVisitorIsVirtualThroughAstNode) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    st.Members().Add(a.get());
    AstNode* asAst = &st;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"tree", "externalias", "id:x"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits each `Members` child (a `VisitExternAliasDeclaration` -> its
// backing `NameToken` `Identifier`) in document order. An empty node records just the node
// itself.
TEST(CSharp_SyntaxTree, DepthFirstWalkVisitsMembersInOrder) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    auto b = std::make_unique<ExternAliasDeclaration>(std::string("b"));
    st.Members().Add(a.get());
    st.Members().Add(b.get());
    RecordingVisitor v;
    st.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "tree", "externalias", "id:a", "externalias", "id:b"}));
}

// The depth-first walk of an empty node records just the node itself (no children).
TEST(CSharp_SyntaxTree, DepthFirstWalkOfEmptyRecordsJustNode) {
    SyntaxTree st;
    RecordingVisitor v;
    st.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"tree"}));
}

// ---- DoMatch (the generated collection match) ------------------------

// Two `SyntaxTree`s with the same (single) member match.
TEST(CSharp_SyntaxTree, DoMatchMatchesSameMembers) {
    SyntaxTree a;
    SyntaxTree b;
    auto am = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    auto bm = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    a.Members().Add(am.get());
    b.Members().Add(bm.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two empty `SyntaxTree`s match (the collection match succeeds when both counts are 0).
TEST(CSharp_SyntaxTree, DoMatchMatchesTwoEmpties) {
    SyntaxTree a;
    SyntaxTree b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `SyntaxTree`s with different member COUNTS do not match (the collection match fails when
// the counts differ).
TEST(CSharp_SyntaxTree, DoMatchRejectsDifferentMemberCount) {
    SyntaxTree a;
    SyntaxTree b;
    auto am = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    a.Members().Add(am.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 member, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `SyntaxTree`s with the same count but different member VALUES do not match (the element
// `DoMatch` -- a `MatchString` on the `ExternAliasDeclaration` name -- rejects).
TEST(CSharp_SyntaxTree, DoMatchRejectsDifferentMemberValue) {
    SyntaxTree a;
    SyntaxTree b;
    auto am = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    auto bm = std::make_unique<ExternAliasDeclaration>(std::string("y"));
    a.Members().Add(am.get());
    b.Members().Add(bm.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `SyntaxTree` does not match a `NamespaceDeclaration` candidate (the two share the EXACT same
// `Members` `AstNodeCollection<AstNode>` collection kind -- `Slots::Member` -- but are distinct
// concrete types with different shapes, so the pattern matcher's `other is SyntaxTree`/
// `other is NamespaceDeclaration` type-check gate rejects; the cross-structural-twin DoMatch
// rejection, the D249/D296 precedent applied to a pair sharing a collection kind).
TEST(CSharp_SyntaxTree, DoMatchRejectsNamespaceDeclarationTwin) {
    SyntaxTree st;
    auto m = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    st.Members().Add(m.get());
    auto nsName = std::make_unique<SimpleType>(std::string("Foo"));
    NamespaceDeclaration ns(nsName.get());
    EXPECT_FALSE(DoMatchAgainst(&st, &ns));
    EXPECT_FALSE(DoMatchAgainst(&ns, &st));
}

// A `SyntaxTree` does not match a `BlockStatement` candidate (the two share the collection-only
// shape -- a sole collection slot -- but are distinct concrete types in disjoint hierarchies
// (`AstNode` root vs `Statement`), so the type-check gate rejects; the cross-hierarchy
// collection-only DoMatch rejection, the D256 `BlockStatement`-vs-`ArrayInitializerExpression`
// precedent).
TEST(CSharp_SyntaxTree, DoMatchRejectsBlockStatementCandidate) {
    SyntaxTree st;
    BlockStatement bs;
    EXPECT_FALSE(DoMatchAgainst(&st, &bs));
    EXPECT_FALSE(DoMatchAgainst(&bs, &st));
}

// A `SyntaxTree` does not match an `AnonymousTypeCreateExpression` candidate (the two share the
// collection-only shape but are distinct concrete types in disjoint hierarchies (`AstNode` root
// vs `Expression`), so the type-check gate rejects).
TEST(CSharp_SyntaxTree, DoMatchRejectsAnonymousTypeCreateExpressionCandidate) {
    SyntaxTree st;
    AnonymousTypeCreateExpression atce;
    EXPECT_FALSE(DoMatchAgainst(&st, &atce));
    EXPECT_FALSE(DoMatchAgainst(&atce, &st));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_SyntaxTree, DoMatchRejectsNullCandidate) {
    SyntaxTree st;
    EXPECT_FALSE(DoMatchAgainst(&st, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the members, re-parents the clones, and detaches from the source. The
// element type is the abstract `AstNode` root, which inherits `AstNode::Clone` returning
// `AstNode*`, so each element's `Clone()` returns `AstNode*` which `Add(AstNode*)` accepts
// directly -- NO `static_cast` (the `NamespaceDeclaration.Members` D293 `AstNode`-IS-the-root
// precedent).
TEST(CSharp_SyntaxTree, CloneDeepCopiesMembers) {
    SyntaxTree original;
    auto x = std::make_unique<ExternAliasDeclaration>(std::string("x"));
    original.Members().Add(x.get());

    std::unique_ptr<SyntaxTree> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The member is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Members().Count(), 1);
    ASSERT_NE(copy->Members().At(0), nullptr);
    EXPECT_NE(copy->Members().At(0), x.get());
    EXPECT_EQ(dynamic_cast<ExternAliasDeclaration*>(copy->Members().At(0))->Name(), "x");
    EXPECT_EQ(copy->Members().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Members().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override). `SyntaxTree` derives directly from `AstNode`, so there is no `Expression`/`Statement`
// covariant intermediate to test -- the only base is `AstNode*`.
TEST(CSharp_SyntaxTree, CloneIsVirtual) {
    SyntaxTree original;
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<SyntaxTree*>(astCopy.get()), nullptr);
}

// `Clone` of an empty `SyntaxTree` yields an empty clone.
TEST(CSharp_SyntaxTree, CloneOfEmptyIsEmpty) {
    SyntaxTree original;  // no members
    std::unique_ptr<SyntaxTree> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Members().Count(), 0);
    EXPECT_EQ(copy->GetChildCount(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A `SyntaxTree` with members passes the inherited `CheckInvariant` (the slot-structure
// verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in debug builds (a no-op
// in NDEBUG).
TEST(CSharp_SyntaxTree, CheckInvariantPassesOnFilledNode) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    auto b = std::make_unique<ExternAliasDeclaration>(std::string("b"));
    st.Members().Add(a.get());
    st.Members().Add(b.get());
    st.CheckInvariant();
}

// An EMPTY `SyntaxTree` passes `CheckInvariant` (the node has no required single slots -- the
// only slot is the `Members` collection, which has no required-slot invariant), the
// collection-only precedent where the empty node is invariant-valid.
TEST(CSharp_SyntaxTree, CheckInvariantPassesOnEmptyNode) {
    SyntaxTree st;
    st.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `MembersSlot` is the node's only slot static; the node's `Slot()` reports it for each
// child. The slot's `Kind` is the shared `Slots::Member` (reused as the collection kind).
TEST(CSharp_SyntaxTree, MembersSlotIsMemberKind) {
    SyntaxTree st;
    auto a = std::make_unique<ExternAliasDeclaration>(std::string("a"));
    st.Members().Add(a.get());
    EXPECT_EQ(st.Members().At(0)->Slot(), &SyntaxTree::MembersSlot);
    EXPECT_EQ(st.Members().At(0)->Slot()->Kind(), &Slots::Member);
    EXPECT_TRUE(st.Members().At(0)->Slot()->IsCollection());
}
