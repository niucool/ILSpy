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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `InvocationAstType` concrete node (cpp/.../Syntax/InvocationAstType.hpp, the
// port of ICSharpCode.Decompiler/CSharp/Syntax/InvocationAstType.cs) -- the
// `invocation_ast_type ::= type '(' argument_list? ')'` (no spec grammar production -- an
// ILSpy-internal type form used when a type appears applied to arguments). The next in-order
// Phase-5 piece per the D312 plan (the remaining GeneralScope `AstType`-bearing nodes). The
// `AnonymousMethodExpression` D306 one-non-incremental-collection-plus-a-required-trailing-single
// shape applied to the `AstType` hierarchy. The suite shares a `RecordingVisitor`, a
// `DoMatchAgainst` helper, and a `makeInvocation` holder (the D234 pattern).

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
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/InvocationAstType.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/TupleAstType.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `InvocationAstType` under test (plus the
// `SimpleType`/`Identifier`/`PrimitiveExpression`/`NullReferenceExpression` of its children, the
// `AnonymousMethodExpression`/`ArrayInitializerExpression` used for the cross-structural-twin
// DoMatch rejections, and the `TupleAstType` used for the cross-hierarchy DoMatch rejection),
// recording a tag and recursing via `VisitChildren` (the inherited depth-first default). The
// trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitInvocationType(InvocationAstType* node) override {
        if (node == nullptr) { trace.push_back("<null-inv>"); return; }
        trace.push_back("inv");
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
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nullref>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitAnonymousMethodExpression(AnonymousMethodExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-anon>"); return; }
        trace.push_back("anon");
        VisitChildren(node);
    }
    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-aie>"); return; }
        trace.push_back("aie");
        VisitChildren(node);
    }
    void VisitTupleType(TupleAstType* node) override {
        if (node == nullptr) { trace.push_back("<null-tt>"); return; }
        trace.push_back("tt");
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

// A holder keeping an `InvocationAstType` and its `SimpleType` `BaseType` + its
// `Expression` arguments alive in the test scope (the D241 non-owning-raw-pointer model -- the
// parent does not own its children; the holder's `unique_ptr`s do).
struct InvocationHolder {
    std::unique_ptr<SimpleType> baseType;
    std::vector<std::unique_ptr<Expression>> args;
    std::unique_ptr<InvocationAstType> inv;
};

// Builds an `InvocationAstType` with a `SimpleType` `BaseType` of the given name and the given
// number of `NullReferenceExpression` arguments (a fresh argument per slot -- the D235
// single-parent guard: a child can have only one parent, so each argument is its own
// `unique_ptr`).
InvocationHolder makeInvocation(std::string baseName, int argCount) {
    InvocationHolder h;
    h.inv = std::make_unique<InvocationAstType>();
    h.baseType = std::make_unique<SimpleType>(baseName);
    h.inv->BaseType(h.baseType.get());
    for (int i = 0; i < argCount; i++) {
        h.args.push_back(std::make_unique<NullReferenceExpression>());
        h.inv->Arguments().Add(h.args.back().get());
    }
    return h;
}

} // namespace

// ===========================================================================
// InvocationAstType
// ===========================================================================

// `InvocationAstType` is an `AstType` and an `AstNode`; it is disjoint from `Expression`/
// `Statement`/`EntityDeclaration` (the `SimpleType` D237 / `PrimitiveType` D236 / `TupleAstType`
// D290 concrete-`AstType` precedent).
TEST(CSharp_InvocationAstType, IsAstTypeAndAstNodeNotExpression) {
    InvocationAstType inv;
    EXPECT_TRUE(dynamic_cast<AstType*>(&inv) != nullptr);
    EXPECT_TRUE(dynamic_cast<AstNode*>(&inv) != nullptr);
    EXPECT_FALSE(dynamic_cast<Expression*>(&inv) != nullptr);
    EXPECT_FALSE(dynamic_cast<EntityDeclaration*>(&inv) != nullptr);
}

// `InvocationAstType` is `final` (the C# `sealed`).
TEST(CSharp_InvocationAstType, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<InvocationAstType>);
    EXPECT_FALSE(std::is_abstract_v<InvocationAstType>);
}

// The generated empty ctor leaves `Arguments` empty and `BaseType` null (a REQUIRED slot --
// `CheckInvariant` asserts it is filled). `GetChildCount` is 1 (the single `BaseType` slot
// contributes 1 to the flattened count even when null -- the `Accessor` D274 / `AnonymousMethodExpression`
// D306 single-slot-counts-even-when-null precedent).
TEST(CSharp_InvocationAstType, EmptyCtorHasEmptyArgumentsAndNullBaseType) {
    InvocationAstType inv;
    EXPECT_EQ(inv.Arguments().Count(), 0);
    EXPECT_EQ(inv.BaseType(), nullptr);
    EXPECT_EQ(inv.GetChildCount(), 1);
}

// ---- the `Arguments` collection (NON-incremental) --------------------------

// `Add` appends and re-parents. The collection is the node's ONLY collection but NOT the last
// slot (`BaseType` trails it), so `supportsIncremental` is FALSE -- a child's `ChildIndex` is
// dynamic, rebuilt lazily by `EnsureChildIndices`. A test asserting `ChildIndex` immediately
// after `Add` sees a stale value; the reindex is triggered by `Slot()` (which calls
// `EnsureChildIndices`), after which each child's `ChildIndex` is its correct flattened index
// (the `Accessor` D274 / `ComposedType` D242 / `AnonymousMethodExpression` D306 non-incremental
// precedent).
TEST(CSharp_InvocationAstType, ArgumentsAddAppendsParentsAndReindexes) {
    InvocationAstType inv;
    auto a0 = std::make_unique<NullReferenceExpression>();
    auto a1 = std::make_unique<NullReferenceExpression>();
    inv.Arguments().Add(a0.get());
    inv.Arguments().Add(a1.get());
    EXPECT_EQ(inv.Arguments().Count(), 2);
    EXPECT_EQ(a0->Parent(), &inv);
    EXPECT_EQ(a1->Parent(), &inv);
    // Trigger the lazy reindex (GetChild does NOT call EnsureChildIndices; Slot() does).
    (void)a0->Slot();
    (void)a1->Slot();
    EXPECT_EQ(a0->ChildIndex, 0);  // first collection element -> flattened index 0
    EXPECT_EQ(a1->ChildIndex, 1);  // second collection element -> flattened index 1
    EXPECT_EQ(inv.GetChildCount(), 3);  // 2 arguments + 1 (the null BaseType slot)
}

// `GetCollectionByKind` returns the `Arguments` collection for the `Expression` kind (the
// `[Slot("Expression")]` argument names the slot kind -- the kind-collapsing-by-`[Slot]`-name
// design reusing the single-`Expression` operand `Slots::Expression` as a collection kind, the
// `ArrayInitializerExpression` D250 / `TupleExpression` D296 / `AnonymousTypeCreateExpression`
// D304 precedent).
TEST(CSharp_InvocationAstType, GetCollectionByKindReturnsArgumentsForExpressionKind) {
    InvocationAstType inv;
    EXPECT_EQ(inv.GetCollectionByKind(&Slots::Expression), &inv.Arguments());
    EXPECT_EQ(inv.GetCollectionByKind(&Slots::Type), nullptr);  // a single-slot kind
    EXPECT_EQ(inv.GetCollectionByKind(&Slots::Argument), nullptr);  // an unrelated collection kind
    EXPECT_EQ(inv.GetCollectionByKind(nullptr), nullptr);
}

// ---- the `BaseType` slot (a REQUIRED single `AstType`) ---------------------

// The setter re-parents and detaches the previous child.
TEST(CSharp_InvocationAstType, BaseTypeSetterReparentsAndDetaches) {
    InvocationAstType inv;
    auto a = std::make_unique<SimpleType>("A");
    inv.BaseType(a.get());
    EXPECT_EQ(inv.BaseType(), a.get());
    EXPECT_EQ(a->Parent(), &inv);
    auto b = std::make_unique<SimpleType>("B");
    inv.BaseType(b.get());
    EXPECT_EQ(inv.BaseType(), b.get());
    EXPECT_EQ(b->Parent(), &inv);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetChildNode
}

// The setter clears with null.
TEST(CSharp_InvocationAstType, BaseTypeSetterClearsWithNull) {
    InvocationAstType inv;
    auto t = std::make_unique<SimpleType>("T");
    inv.BaseType(t.get());
    inv.BaseType(nullptr);
    EXPECT_EQ(inv.BaseType(), nullptr);
    EXPECT_EQ(t->Parent(), nullptr);
}

// ---- collection-aware slot storage ----------------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
// running index: a collection step (the `Arguments` range `[0, argCount)`), then a single step
// (the `BaseType` at index `argCount`). `GetChildCount` is `argCount + 1` (the single slot
// contributes 1 even when null).
TEST(CSharp_InvocationAstType, CollectionAwareSlotStorage) {
    auto h = makeInvocation("Foo", 2);
    EXPECT_EQ(h.inv->GetChildCount(), 3);  // 2 arguments + 1 (BaseType)
    EXPECT_EQ(h.inv->GetChild(0), h.args[0].get());
    EXPECT_EQ(h.inv->GetChild(1), h.args[1].get());
    EXPECT_EQ(h.inv->GetChild(2), h.baseType.get());
    EXPECT_EQ(h.inv->GetChildSlotInfo(0), &h.inv->ArgumentsSlot);
    EXPECT_EQ(h.inv->GetChildSlotInfo(1), &h.inv->ArgumentsSlot);
    EXPECT_EQ(h.inv->GetChildSlotInfo(2), &h.inv->BaseTypeSlot);
    EXPECT_THROW(h.inv->GetChild(3), std::out_of_range);
    EXPECT_THROW(h.inv->GetChildSlotInfo(3), std::out_of_range);
}

// `SetChild` replaces a collection element in place (the element must already exist at the
// flattened index) and the single slot via the index-less `SetChildNode`.
TEST(CSharp_InvocationAstType, SetChildReplacesCollectionElementAndSingle) {
    auto h = makeInvocation("Foo", 1);
    auto newArg = std::make_unique<NullReferenceExpression>();
    h.inv->SetChild(0, newArg.get());  // replace the first argument in place
    EXPECT_EQ(h.inv->Arguments().At(0), newArg.get());
    EXPECT_EQ(newArg->Parent(), &*h.inv);
    auto newBase = std::make_unique<SimpleType>("Bar");
    h.inv->SetChild(1, newBase.get());  // replace the BaseType
    EXPECT_EQ(h.inv->BaseType(), newBase.get());
    EXPECT_EQ(newBase->Parent(), &*h.inv);
    EXPECT_THROW(h.inv->SetChild(2, nullptr), std::out_of_range);
}

// ---- shared `Slots` kind identity -----------------------------------------

TEST(CSharp_InvocationAstType, SlotStaticsPointAtSharedSlotsKinds) {
    EXPECT_EQ(InvocationAstType::ArgumentsSlot.Kind(), &Slots::Expression);
    EXPECT_EQ(InvocationAstType::BaseTypeSlot.Kind(), &Slots::Type);
}

// `IsInstanceOfType` is-a cross-check: the `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>`)
// accepts an `Expression` (a `NullReferenceExpression`) but not an `AstType` (a `SimpleType`);
// the `BaseTypeSlot` (a `CSharpSlotInfoT<AstType>`) accepts an `AstType` (a `SimpleType`) but
// not an `Expression` (a `NullReferenceExpression`).
TEST(CSharp_InvocationAstType, SlotKindIsInstanceOfTypeCrossCheck) {
    auto st = std::make_unique<SimpleType>("T");
    auto expr = std::make_unique<NullReferenceExpression>();
    EXPECT_TRUE(InvocationAstType::ArgumentsSlot.IsInstanceOfType(expr.get()));
    EXPECT_FALSE(InvocationAstType::ArgumentsSlot.IsInstanceOfType(st.get()));
    EXPECT_TRUE(InvocationAstType::BaseTypeSlot.IsInstanceOfType(st.get()));
    EXPECT_FALSE(InvocationAstType::BaseTypeSlot.IsInstanceOfType(expr.get()));
}

// ---- AcceptVisitor dispatch + virtuality ----------------------------------

// A default-constructed (empty) node records just itself (an empty node's `Children()` is empty
// since `FirstChild`/`NextSibling` skip the empty collection and the null `BaseType`).
TEST(CSharp_InvocationAstType, AcceptVisitorDispatchRecordsTag) {
    InvocationAstType inv;
    RecordingVisitor v;
    inv.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"inv"}));
}

TEST(CSharp_InvocationAstType, AcceptVisitorVirtualThroughAstType) {
    auto h = makeInvocation("Foo", 1);
    RecordingVisitor v;
    AstType* node = h.inv.get();
    node->AcceptVisitor(v);
    // inv -> nullref (argument 0) -> simple:Foo (BaseType) -> id:Foo (the SimpleType recurses
    // into its IdentifierToken). The collection is visited first (slot 0), then the single
    // (slot 1) -- the source declaration order.
    EXPECT_EQ(v.trace, std::vector<std::string>({"inv", "nullref", "simple:Foo", "id:Foo"}));
}

// ---- DoMatch ---------------------------------------------------------------

TEST(CSharp_InvocationAstType, DoMatchSame) {
    auto a = makeInvocation("Foo", 2);
    auto b = makeInvocation("Foo", 2);
    EXPECT_TRUE(DoMatchAgainst(a.inv.get(), b.inv.get()));
}

// Different argument count rejects (the collection-`DoMatch` term runs first and rejects on a
// length mismatch).
TEST(CSharp_InvocationAstType, DoMatchDifferentArgumentCountRejects) {
    auto a = makeInvocation("Foo", 2);
    auto b = makeInvocation("Foo", 1);
    EXPECT_FALSE(DoMatchAgainst(a.inv.get(), b.inv.get()));
}

// Different `BaseType` name rejects (the `MatchRequired` term delegates to `SimpleType::DoMatch`,
// which compares the names via `MatchString`).
TEST(CSharp_InvocationAstType, DoMatchDifferentBaseTypeRejects) {
    auto a = makeInvocation("Foo", 1);
    auto b = makeInvocation("Bar", 1);
    EXPECT_FALSE(DoMatchAgainst(a.inv.get(), b.inv.get()));
}

// An empty-vs-non-empty `Arguments` asymmetry rejects (both sides must agree on the argument
// list presence/length).
TEST(CSharp_InvocationAstType, DoMatchArgumentsAsymmetryRejects) {
    auto a = makeInvocation("Foo", 0);
    auto b = makeInvocation("Foo", 1);
    EXPECT_FALSE(DoMatchAgainst(a.inv.get(), b.inv.get()));
    EXPECT_FALSE(DoMatchAgainst(b.inv.get(), a.inv.get()));
}

// A cross-structural-twin rejection (vs an `AnonymousMethodExpression`, the
// collection-plus-required-trailing-single structural twin across disjoint hierarchies --
// `InvocationAstType` is an `AstType`, `AnonymousMethodExpression` is an `Expression`).
TEST(CSharp_InvocationAstType, DoMatchCrossStructuralTwinRejects) {
    auto h = makeInvocation("Foo", 1);
    auto body = std::make_unique<BlockStatement>();
    auto anon = std::make_unique<AnonymousMethodExpression>();
    anon->Body(body.get());
    EXPECT_FALSE(DoMatchAgainst(h.inv.get(), anon.get()));
    EXPECT_FALSE(DoMatchAgainst(anon.get(), h.inv.get()));
}

// A cross-hierarchy rejection (vs a `TupleAstType`, a collection-only `AstType` -- both are
// `AstType`s but distinct concrete types).
TEST(CSharp_InvocationAstType, DoMatchCrossTypeRejects) {
    auto h = makeInvocation("Foo", 1);
    auto tt = std::make_unique<TupleAstType>();
    EXPECT_FALSE(DoMatchAgainst(h.inv.get(), tt.get()));
    EXPECT_FALSE(DoMatchAgainst(tt.get(), h.inv.get()));
}

TEST(CSharp_InvocationAstType, DoMatchNullRejects) {
    auto h = makeInvocation("Foo", 1);
    EXPECT_FALSE(DoMatchAgainst(h.inv.get(), nullptr));
}

// ---- Clone ----------------------------------------------------------------

TEST(CSharp_InvocationAstType, CloneDeepCopyAndVirtuality) {
    auto h = makeInvocation("Foo", 2);
    auto* clone = h.inv->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone, h.inv.get());
    EXPECT_EQ(clone->Arguments().Count(), 2);
    EXPECT_NE(clone->Arguments().At(0), h.args[0].get());  // deep-copy, not aliased
    EXPECT_NE(clone->Arguments().At(1), h.args[1].get());
    EXPECT_EQ(clone->Arguments().At(0)->Parent(), clone);
    EXPECT_NE(clone->BaseType(), h.baseType.get());  // deep-copy, not aliased
    // The cloned `BaseType` is an abstract `AstType*`; downcast to `SimpleType` to read the name.
    auto* clonedBase = dynamic_cast<SimpleType*>(clone->BaseType());
    ASSERT_NE(clonedBase, nullptr);
    ASSERT_TRUE(clonedBase->Identifier().has_value());
    EXPECT_EQ(*clonedBase->Identifier(), "Foo");
    EXPECT_EQ(clone->BaseType()->Parent(), clone);
    auto* astClone = static_cast<AstType*>(h.inv.get())->Clone();  // covariant through AstType*
    EXPECT_NE(astClone, nullptr);
    auto* typedClone = dynamic_cast<InvocationAstType*>(astClone);
    ASSERT_NE(typedClone, nullptr);
    EXPECT_EQ(typedClone->Arguments().Count(), 2);
    delete astClone;
    delete clone;
}

TEST(CSharp_InvocationAstType, CloneSkipsAbsentBaseType) {
    auto inv = std::make_unique<InvocationAstType>();
    auto arg = std::make_unique<NullReferenceExpression>();
    inv->Arguments().Add(arg.get());
    auto* clone = inv->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->Arguments().Count(), 1);
    EXPECT_EQ(clone->BaseType(), nullptr);  // absent BaseType is skipped (Clone tolerates it)
    delete clone;
}

// ---- CheckInvariant --------------------------------------------------------

// `CheckInvariant` PASSES on a filled node (the `Arguments` collection is optional, the
// `BaseType` is required and filled).
TEST(CSharp_InvocationAstType, CheckInvariantPassesOnFilled) {
    auto h = makeInvocation("Foo", 1);
    h.inv->CheckInvariant();  // should not assert
}

// `CheckInvariant` REJECTS an empty node (the `BaseType` is a REQUIRED slot -- the empty node
// violates the required-slot invariant, the `AnonymousMethodExpression` D306 / `CastExpression`
// D243 precedent).
TEST(CSharp_InvocationAstType, CheckInvariantRejectsEmpty) {
    InvocationAstType inv;
    EXPECT_DEATH(inv.CheckInvariant(), "");
}

// ---- slot-static distinctness ----------------------------------------------

// The two slot statics of distinct element types are unrelated pointer types; compare through
// the common `const CSharpSlotInfo*` base (the D251/D252/D262 cross-element-type `EXPECT_NE`
// precedent).
TEST(CSharp_InvocationAstType, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&InvocationAstType::ArgumentsSlot),
              static_cast<const CSharpSlotInfo*>(&InvocationAstType::BaseTypeSlot));
}
