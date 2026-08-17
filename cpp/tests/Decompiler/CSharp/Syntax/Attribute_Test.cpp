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

// Tests for the `Attribute` concrete node (cpp/.../Syntax/Attribute.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/Attribute.cs) -- the first
// `GeneralScope`-sub-namespace concrete node, a sealed `AstNode` (not an `AstType`, not an
// `Expression`) with a required `AstType` `Type` slot, an `Expression` `Arguments`
// collection, and a plain `HasArgumentList` bool scalar. Exercises the `HasArgumentList`
// scalar, the required `Type` slot, the `Arguments` collection, the collection-aware
// slot-storage contract, the `AcceptVisitor` dispatch, the generated `DoMatch` (a
// non-nullable recursive `MatchRequired` on `Type` plus a collection `DoMatch` on `Arguments`
// plus a plain-equality on `HasArgumentList`), the per-concrete-node `Clone`, and the
// inherited `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls (the role the generated
// per-node `Visit` overrides play) with a tag distinguishing the concrete types, recursing via
// the inherited `VisitChildren` (the document-order walk). `VisitAttribute`/`VisitSimpleType`/
// `VisitIdentifier`/`VisitNullReferenceExpression`/`VisitThisReferenceExpression` are the nodes
// these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitAttribute(Attribute* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("attr");
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-st>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or("<anon>"));
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-nre>"); return; }
        trace.push_back("nullref");
        VisitChildren(node);
    }
    void VisitThisReferenceExpression(ThisReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-tre>"); return; }
        trace.push_back("thisref");
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

// `Attribute` is an `AstNode`; it is NOT an `Expression` and NOT an `AstType` (it derives
// directly from `AstNode` -- a structural node on an `AttributeSection`, not a type reference or
// an expression).
TEST(CSharp_Attribute, IsAstNodeNotExpressionOrAstType) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_NE(dynamic_cast<AstNode*>(&attr), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&attr), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&attr), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no type, no arguments, and `HasArgumentList` defaults to false. The
// required `Type` slot still occupies a flattened index even when empty, so `GetChildCount`
// is 1 (the collection adds 0).
TEST(CSharp_Attribute, EmptyCtorHasNoTypeOrArguments) {
    Attribute attr;
    EXPECT_FALSE(attr.HasArgumentList());
    EXPECT_EQ(attr.Type(), nullptr);
    EXPECT_EQ(attr.Arguments().Count(), 0);
    EXPECT_EQ(attr.GetChildCount(), 1);  // Type slot (empty) + 0 arguments
}

// The `(AstType)` required-prefix ctor sets the `Type` (parented, index 0); `HasArgumentList`
// is still false and `Arguments` is empty.
TEST(CSharp_Attribute, CtorWithTypeSetsType) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_EQ(attr.Type(), type.get());
    EXPECT_EQ(type->Parent(), &attr);
    EXPECT_EQ(type->ChildIndex, 0);
    EXPECT_FALSE(attr.HasArgumentList());
    EXPECT_EQ(attr.Arguments().Count(), 0);
}

// ---- The `HasArgumentList` bool scalar --------------------------------

// The `HasArgumentList` getter/setter round-trip; it is plain instance state (not a child slot,
// not a ctor param).
TEST(CSharp_Attribute, HasArgumentListRoundTrip) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_FALSE(attr.HasArgumentList());
    attr.HasArgumentList(true);
    EXPECT_TRUE(attr.HasArgumentList());
    attr.HasArgumentList(false);
    EXPECT_FALSE(attr.HasArgumentList());
}

// ---- The `Type` slot (a required `AstType` child) -------------------

// The `Type` setter re-parents the new type and detaches the old one.
TEST(CSharp_Attribute, TypeSetterReparentsAndDetaches) {
    Attribute attr;
    auto a = std::make_unique<SimpleType>(std::string("Foo"));
    auto b = std::make_unique<SimpleType>(std::string("Bar"));
    attr.Type(a.get());
    EXPECT_EQ(attr.Type(), a.get());
    EXPECT_EQ(a->Parent(), &attr);
    EXPECT_EQ(a->ChildIndex, 0);
    attr.Type(b.get());
    EXPECT_EQ(attr.Type(), b.get());
    EXPECT_EQ(b->Parent(), &attr);
    EXPECT_EQ(b->ChildIndex, 0);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `Type` clears the slot and detaches the old type.
TEST(CSharp_Attribute, TypeSetterClearsOnNull) {
    Attribute attr;
    auto a = std::make_unique<SimpleType>(std::string("Foo"));
    attr.Type(a.get());
    ASSERT_EQ(attr.Type(), a.get());
    attr.Type(nullptr);
    EXPECT_EQ(attr.Type(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Arguments` collection --------------------------------------

// The collection starts empty (0 count); the node's child count is the one single slot (1) + 0.
TEST(CSharp_Attribute, ArgumentsEmptyByDefault) {
    Attribute attr;
    EXPECT_EQ(attr.Arguments().Count(), 0);
    EXPECT_EQ(attr.GetChildCount(), 1);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last slot, so an element's index is
// `1 + its local position` -- baseIndex 1, after the `Type` single slot).
TEST(CSharp_Attribute, ArgumentsAddAppendsAndParentsIncremental) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    auto a1 = std::make_unique<ThisReferenceExpression>();
    attr.Arguments().Add(a0.get());
    attr.Arguments().Add(a1.get());
    EXPECT_EQ(attr.Arguments().Count(), 2);
    EXPECT_EQ(a0->Parent(), &attr);
    EXPECT_EQ(a1->Parent(), &attr);
    EXPECT_EQ(a0->ChildIndex, 1);  // baseIndex 1 + 0
    EXPECT_EQ(a1->ChildIndex, 2);  // baseIndex 1 + 1
    EXPECT_EQ(attr.GetChildCount(), 3);  // type + 2 arguments
    EXPECT_TRUE(attr.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind and null for
// any other kind (the `Type` kind is a single slot, not a collection).
TEST(CSharp_Attribute, GetCollectionByKindReturnsArgumentsForArgumentKind) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_NE(attr.GetCollectionByKind(&Slots::Argument), nullptr);
    EXPECT_EQ(attr.GetCollectionByKind(&Slots::Argument), &attr.Arguments());
    EXPECT_EQ(attr.GetCollectionByKind(&Slots::Type), nullptr);  // a single slot, not a collection
    EXPECT_EQ(attr.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the type at index 0 and the arguments at index 1+; the collection occupies
// the contiguous range [1, 1 + Count).
TEST(CSharp_Attribute, GetChildDispatchesTypeAndArguments) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    auto a1 = std::make_unique<ThisReferenceExpression>();
    attr.Arguments().Add(a0.get());
    attr.Arguments().Add(a1.get());
    EXPECT_EQ(attr.GetChild(0), type.get());
    EXPECT_EQ(attr.GetChild(1), a0.get());
    EXPECT_EQ(attr.GetChild(2), a1.get());
    EXPECT_THROW(attr.GetChild(3), std::out_of_range);
    EXPECT_THROW(attr.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TypeSlot` at index 0 and the `ArgumentsSlot` at index 1+ (the
// slot identity the slot system compares by address).
TEST(CSharp_Attribute, GetChildSlotInfoDispatchesTypeAndArguments) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    attr.Arguments().Add(a0.get());
    EXPECT_EQ(attr.GetChildSlotInfo(0), &Attribute::TypeSlot);
    EXPECT_EQ(attr.GetChildSlotInfo(1), &Attribute::ArgumentsSlot);
    EXPECT_THROW(attr.GetChildSlotInfo(2), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Type` KIND (the shared `Slots.Type`).
TEST(CSharp_Attribute, TypeSlotPointsAtTypeKind) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_EQ(attr.GetChildSlotInfo(0)->Kind(), &Slots::Type);
}

// `GetChildSlotInfo(1)` points at the `Argument` KIND (the shared `Slots.Argument`).
TEST(CSharp_Attribute, ArgumentsSlotPointsAtArgumentKind) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    attr.Arguments().Add(a0.get());
    EXPECT_EQ(attr.GetChildSlotInfo(1)->Kind(), &Slots::Argument);
}

// `SetChild` writes the type at index 0 and replaces an argument in place at index 1+ (the
// collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_Attribute, SetChildDispatchesTypeAndArguments) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    attr.Arguments().Add(a0.get());
    // Replace the type at index 0.
    auto newType = std::make_unique<SimpleType>(std::string("Bar"));
    attr.SetChild(0, newType.get());
    EXPECT_EQ(attr.Type(), newType.get());
    EXPECT_EQ(newType->Parent(), &attr);
    EXPECT_EQ(newType->ChildIndex, 0);
    // Replace the argument at index 1.
    auto a1 = std::make_unique<ThisReferenceExpression>();
    attr.SetChild(1, a1.get());
    EXPECT_EQ(attr.Arguments().At(0), a1.get());
    EXPECT_EQ(a1->Parent(), &attr);
    EXPECT_THROW(attr.SetChild(2, nullptr), std::out_of_range);  // no element at index 2
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitAttribute` (the visitor-pattern round-trip); the
// depth-first walk then visits the `Type` child (a `VisitSimpleType` + its `VisitIdentifier`
// token) and the `Arguments` (a `VisitNullReferenceExpression`/`VisitThisReferenceExpression`
// per element) in document order.
TEST(CSharp_Attribute, AcceptVisitorDispatchesToVisitAttribute) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    auto a1 = std::make_unique<ThisReferenceExpression>();
    attr.Arguments().Add(a0.get());
    attr.Arguments().Add(a1.get());
    RecordingVisitor v;
    attr.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "attr", "simple:Foo", "id:Foo", "nullref", "thisref"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` (the dynamic dispatch the output visitor
// relies on).
TEST(CSharp_Attribute, AcceptVisitorIsVirtualThroughAstNode) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    AstNode* asAst = &attr;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"attr", "simple:Foo", "id:Foo"}));
}

// ---- DoMatch (the generated recursive + collection + plain-equality match) --

// Two `Attribute`s with the same `Type`, `Arguments`, and `HasArgumentList` match.
TEST(CSharp_Attribute, DoMatchMatchesSameAll) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute a(ta.get());
    Attribute b(tb.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    auto b0 = std::make_unique<NullReferenceExpression>();
    a.Arguments().Add(a0.get());
    b.Arguments().Add(b0.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Type` (the non-nullable recursive term via `MatchRequired`) rejects.
TEST(CSharp_Attribute, DoMatchRejectsDifferentType) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Bar"));
    Attribute a(ta.get());
    Attribute b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different `Arguments` COUNTS reject (the collection match fails when the counts differ).
TEST(CSharp_Attribute, DoMatchRejectsDifferentArgumentsCount) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute a(ta.get());
    Attribute b(tb.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    a.Arguments().Add(a0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Different `Arguments` VALUES reject (the element `DoMatch` -- a type-only match on the
// `NullReferenceExpression`/`ThisReferenceExpression` -- rejects).
TEST(CSharp_Attribute, DoMatchRejectsDifferentArgumentsValue) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute a(ta.get());
    Attribute b(tb.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    auto b0 = std::make_unique<ThisReferenceExpression>();
    a.Arguments().Add(a0.get());
    b.Arguments().Add(b0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different `HasArgumentList` (the plain-equality term) rejects -- and short-circuits after
// the recursive `Type` and `Arguments` terms already matched.
TEST(CSharp_Attribute, DoMatchRejectsDifferentHasArgumentList) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute a(ta.get());
    Attribute b(tb.get());
    a.HasArgumentList(true);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// An `Attribute` does not match a different concrete type (the `other is Attribute` gate); a
// `SimpleType` is not an `Attribute`.
TEST(CSharp_Attribute, DoMatchRejectsDifferentType2) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    SimpleType st(std::string("Foo"));
    EXPECT_FALSE(DoMatchAgainst(&attr, &st));
    EXPECT_FALSE(DoMatchAgainst(&st, &attr));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_Attribute, DoMatchRejectsNullCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_FALSE(DoMatchAgainst(&attr, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `Type`, the `Arguments`, and the `HasArgumentList` scalar, re-parents
// the clones, and detaches from the source.
TEST(CSharp_Attribute, CloneDeepCopiesTypeArgumentsAndScalar) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"), TextLocation(1, 1));
    Attribute original(type.get());
    original.HasArgumentList(true);
    auto a0 = std::make_unique<NullReferenceExpression>();
    original.Arguments().Add(a0.get());

    std::unique_ptr<Attribute> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_TRUE(copy->HasArgumentList());  // the scalar is copied
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());  // a fresh type
    EXPECT_EQ(copy->Type()->Parent(), copy.get());  // re-parented to the clone
    EXPECT_EQ(copy->Arguments().Count(), 1);
    ASSERT_NE(copy->Arguments().At(0), nullptr);
    EXPECT_NE(copy->Arguments().At(0), a0.get());  // a fresh argument
    EXPECT_EQ(copy->Arguments().At(0)->Parent(), copy.get());  // re-parented
    // The original is unchanged.
    EXPECT_EQ(original.Arguments().Count(), 1);
    EXPECT_EQ(a0->Parent(), &original);
    EXPECT_TRUE(original.HasArgumentList());
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override).
TEST(CSharp_Attribute, CloneIsVirtualThroughAstNode) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute original(type.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<Attribute*>(astCopy.get()), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `Attribute` with the required `Type` slot filled and arguments passes the inherited
// `CheckInvariant` (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type
// consistent). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_Attribute, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"), TextLocation(1, 1));
    Attribute attr(type.get());
    auto a0 = std::make_unique<NullReferenceExpression>();
    attr.Arguments().Add(a0.get());
    attr.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `TypeSlot` and `ArgumentsSlot` are distinct slot statics (compared by address); the
// node's `Slot()` reports the per-node slot for the type. The slot statics have different
// `CSharpSlotInfoT<T>` element types (`AstType` vs `Expression`), so they are compared through
// the common `CSharpSlotInfo` base (the pointer-identity comparison the slot system uses).
TEST(CSharp_Attribute, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&Attribute::TypeSlot),
              static_cast<const CSharpSlotInfo*>(&Attribute::ArgumentsSlot));
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    EXPECT_EQ(attr.Type()->Slot(), &Attribute::TypeSlot);
    EXPECT_EQ(attr.Type()->Slot()->Kind(), &Slots::Type);
}
