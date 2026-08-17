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

// Tests for the `ObjectCreateExpression` concrete node (cpp/.../Syntax/Expressions/
// ObjectCreateExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.cs) -- the first
// ported node to combine a collection (`Arguments`) with a nullable single child slot
// (`Initializer`): the `object_create_expression ::= 'new' type '(' expression* ')'
// array_initializer?` production. Exercises the `Type` (required `AstType`) / `Arguments`
// (NON-incremental `AstNodeCollection<Expression>`) / `Initializer` (nullable single
// `ArrayInitializerExpression`) slots, the collection-aware slot-storage contract (a single ->
// collection -> single dispatch), the `AcceptVisitor` dispatch, the generated `DoMatch` (a
// `MatchRequired` on `Type` + a collection `DoMatch` on `Arguments` + a `MatchOptional` on
// `Initializer`), the per-concrete-node `Clone`, and the inherited `CheckInvariant`.

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
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: records the per-node `Visit` calls (the role the generated
// per-node `Visit` overrides play) with a tag distinguishing the concrete types, recursing via
// the inherited `VisitChildren` (the document-order walk).
// `VisitObjectCreateExpression`/`VisitSimpleType`/`VisitIdentifier`/`VisitIdentifierExpression`/
// `VisitArrayInitializerExpression` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitObjectCreateExpression(ObjectCreateExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("objectcreate");
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
    void VisitIdentifierExpression(IdentifierExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-idexpr>"); return; }
        trace.push_back("idexpr:" + node->Identifier());
        VisitChildren(node);
    }
    void VisitArrayInitializerExpression(ArrayInitializerExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-arrinit>"); return; }
        trace.push_back("arrinit");
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

// `ObjectCreateExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_ObjectCreateExpression, IsExpressionAndAstNodeNotAstType) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    EXPECT_NE(dynamic_cast<Expression*>(&oce), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&oce), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&oce), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no type, no arguments, and no initializer. `GetChildCount` is 2 (the two
// empty single slots `Type` + `Initializer`) + 0 arguments.
TEST(CSharp_ObjectCreateExpression, EmptyCtorHasNoTypeOrArgumentsOrInitializer) {
    ObjectCreateExpression oce;
    EXPECT_EQ(oce.Type(), nullptr);
    EXPECT_EQ(oce.Arguments().Count(), 0);
    EXPECT_EQ(oce.Initializer(), nullptr);
    EXPECT_EQ(oce.GetChildCount(), 2);  // the two single slots (empty) + 0 arguments
    EXPECT_EQ(oce.StartLocation(), TextLocation::Empty);
    EXPECT_STREQ(oce.NewKeyword, "new");
}

// The `(AstType)` ctor (the generated required-prefix ctor) sets the `Type` and parents it at
// index 0.
TEST(CSharp_ObjectCreateExpression, CtorSetsType) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    EXPECT_EQ(oce.Type(), type.get());
    EXPECT_EQ(type->Parent(), &oce);
    EXPECT_EQ(type->ChildIndex, 0);
}

// ---- The `Type` slot ------------------------------------------------

// The `Type` setter re-parents the new type and detaches the old one.
TEST(CSharp_ObjectCreateExpression, TypeSetterReparentsAndDetaches) {
    ObjectCreateExpression oce;
    auto a = std::make_unique<SimpleType>(std::string("a"));
    auto b = std::make_unique<SimpleType>(std::string("b"));
    oce.Type(a.get());
    EXPECT_EQ(a->Parent(), &oce);
    EXPECT_EQ(oce.Type(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    oce.Type(b.get());
    EXPECT_EQ(b->Parent(), &oce);
    EXPECT_EQ(oce.Type(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Type` setter clears and detaches the old type.
TEST(CSharp_ObjectCreateExpression, TypeSetterClearsWithNull) {
    ObjectCreateExpression oce;
    auto a = std::make_unique<SimpleType>(std::string("a"));
    oce.Type(a.get());
    oce.Type(nullptr);
    EXPECT_EQ(oce.Type(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Arguments` collection ----------------------------------------

// The collection starts empty (0 count); the node's child count is the two single slots (2) + 0.
TEST(CSharp_ObjectCreateExpression, ArgumentsEmptyByDefault) {
    ObjectCreateExpression oce;
    EXPECT_EQ(oce.Arguments().Count(), 0);
    EXPECT_EQ(oce.GetChildCount(), 2);
}

// `Add` appends an element and parents it; the collection is NON-incremental (it is the node's
// only collection but NOT its last slot -- the `Initializer` single slot trails it), so `Add`
// INVALIDATES the parent's indices (unlike `Attribute`/`InvocationExpression` whose only-and-last
// collection maintains the index incrementally). The element's `ChildIndex` is stale until a
// reindex is triggered.
TEST(CSharp_ObjectCreateExpression, ArgumentsAddAppendsAndParentsAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    oce.Arguments().Add(x.get());
    oce.Arguments().Add(y.get());
    EXPECT_EQ(oce.Arguments().Count(), 2);
    EXPECT_EQ(x->Parent(), &oce);
    EXPECT_EQ(y->Parent(), &oce);
    EXPECT_FALSE(oce.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(oce.GetChildCount(), 4);  // type + 2 arguments + initializer slot
}

// `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind and null for
// any other kind (the `Type`/`Initializer` kinds are single slots).
TEST(CSharp_ObjectCreateExpression, GetCollectionByKindReturnsArgumentsForArgumentKind) {
    ObjectCreateExpression oce;
    EXPECT_NE(oce.GetCollectionByKind(&Slots::Argument), nullptr);
    EXPECT_EQ(oce.GetCollectionByKind(&Slots::Argument), &oce.Arguments());
    EXPECT_EQ(oce.GetCollectionByKind(&Slots::Type), nullptr);        // a single slot
    EXPECT_EQ(oce.GetCollectionByKind(&Slots::Initializer), nullptr);  // a single slot
    EXPECT_EQ(oce.GetCollectionByKind(nullptr), nullptr);
}

// ---- The `Initializer` slot (a nullable single child after a collection) --

// The `Initializer` defaults to null (an optional slot).
TEST(CSharp_ObjectCreateExpression, InitializerNullByDefault) {
    ObjectCreateExpression oce;
    EXPECT_EQ(oce.Initializer(), nullptr);
}

// The `Initializer` setter re-parents the new initializer and detaches the old one; the setter is
// index-less (a collection precedes the slot), so a set INVALIDATES the parent's indices.
TEST(CSharp_ObjectCreateExpression, InitializerSetterReparentsAndDetachesAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto a = std::make_unique<ArrayInitializerExpression>();
    auto b = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(a.get());
    EXPECT_EQ(a->Parent(), &oce);
    EXPECT_EQ(oce.Initializer(), a.get());
    EXPECT_FALSE(oce.ChildIndicesValid());  // index-less setter: set invalidates
    oce.Initializer(b.get());
    EXPECT_EQ(b->Parent(), &oce);
    EXPECT_EQ(oce.Initializer(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `Initializer` setter clears and detaches the old initializer.
TEST(CSharp_ObjectCreateExpression, InitializerSetterClearsWithNull) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto a = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(a.get());
    oce.Initializer(nullptr);
    EXPECT_EQ(oce.Initializer(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the type at index 0, the arguments at index 1+, and the initializer at
// index `1 + Count`; the collection occupies the contiguous range `[1, 1 + Count)`.
TEST(CSharp_ObjectCreateExpression, GetChildDispatchesTypeArgumentsInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    oce.Arguments().Add(x.get());
    oce.Arguments().Add(y.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    EXPECT_EQ(oce.GetChild(0), type.get());
    EXPECT_EQ(oce.GetChild(1), x.get());
    EXPECT_EQ(oce.GetChild(2), y.get());
    EXPECT_EQ(oce.GetChild(3), init.get());
    EXPECT_THROW(oce.GetChild(4), std::out_of_range);
    EXPECT_THROW(oce.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TypeSlot` at index 0, the `ArgumentsSlot` at index 1+, and the
// `InitializerSlot` at index `1 + Count` (the slot identity the slot system compares by address).
TEST(CSharp_ObjectCreateExpression, GetChildSlotInfoDispatchesTypeArgumentsInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    oce.Arguments().Add(x.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    EXPECT_EQ(oce.GetChildSlotInfo(0), &ObjectCreateExpression::TypeSlot);
    EXPECT_EQ(oce.GetChildSlotInfo(1), &ObjectCreateExpression::ArgumentsSlot);
    EXPECT_EQ(oce.GetChildSlotInfo(2), &ObjectCreateExpression::InitializerSlot);
    EXPECT_THROW(oce.GetChildSlotInfo(3), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Type` KIND (the shared `Slots.Type`).
TEST(CSharp_ObjectCreateExpression, TypeSlotPointsAtTypeKind) {
    ObjectCreateExpression oce;
    EXPECT_EQ(oce.GetChildSlotInfo(0)->Kind(), &Slots::Type);
}

// `GetChildSlotInfo(1)` points at the `Argument` KIND (the shared `Slots.Argument`).
TEST(CSharp_ObjectCreateExpression, ArgumentsSlotPointsAtArgumentKind) {
    ObjectCreateExpression oce;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    oce.Arguments().Add(x.get());
    EXPECT_EQ(oce.GetChildSlotInfo(1)->Kind(), &Slots::Argument);
}

// `GetChildSlotInfo(1 + Count)` points at the `Initializer` KIND (the new `Slots.Initializer`,
// cycle-broken into ArrayInitializerExpression.hpp).
TEST(CSharp_ObjectCreateExpression, InitializerSlotPointsAtInitializerKind) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    // The Initializer is at flattened index 1 (no arguments); trigger a reindex so GetChildSlotInfo
    // routes to the trailing single slot.
    (void)type->Slot();
    EXPECT_EQ(oce.GetChildSlotInfo(1)->Kind(), &Slots::Initializer);
}

// `SetChild` writes the type at index 0, replaces an argument in place at index 1+, and writes
// the initializer at index `1 + Count` (the collection's `SetAt` re-parents and carries the old
// index; the single-slot `SetChildNode` re-parents).
TEST(CSharp_ObjectCreateExpression, SetChildDispatchesTypeArgumentsInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    oce.Arguments().Add(x.get());
    // Replace the type at index 0.
    auto type2 = std::make_unique<SimpleType>(std::string("Bar"));
    oce.SetChild(0, type2.get());
    EXPECT_EQ(oce.Type(), type2.get());
    EXPECT_EQ(type2->Parent(), &oce);
    // Replace the argument at index 1.
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    oce.SetChild(1, y.get());
    EXPECT_EQ(oce.Arguments().At(0), y.get());
    EXPECT_EQ(y->Parent(), &oce);
    // Write the initializer at index 2 (1 argument).
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.SetChild(2, init.get());
    EXPECT_EQ(oce.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &oce);
    EXPECT_THROW(oce.SetChild(3, nullptr), std::out_of_range);  // no slot at index 3
}

// ---- The dynamic flattened-index layout (non-incremental) -----------

// After `Add`/set the parent's indices are invalid; the reindex is triggered by `Slot()` (which
// calls `EnsureChildIndices` on the parent), after which each child's `ChildIndex` is its correct
// flattened index: `Type` at 0, `Arguments` at `[1, 1 + Count)`, `Initializer` at `1 + Count`.
TEST(CSharp_ObjectCreateExpression, ChildIndicesRebuiltAfterReindex) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    oce.Arguments().Add(x.get());
    oce.Arguments().Add(y.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    ASSERT_FALSE(oce.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)type->Slot();
    ASSERT_TRUE(oce.ChildIndicesValid());
    EXPECT_EQ(type->ChildIndex, 0);       // Type at 0
    EXPECT_EQ(x->ChildIndex, 1);          // Arguments[0] at 1
    EXPECT_EQ(y->ChildIndex, 2);          // Arguments[1] at 2
    EXPECT_EQ(init->ChildIndex, 3);       // Initializer at 1 + Count (1 + 2)
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitObjectCreateExpression` (the visitor-pattern round-trip);
// the depth-first walk then visits the `Type` child (a `VisitSimpleType` -> its `Identifier`).
TEST(CSharp_ObjectCreateExpression, AcceptVisitorDispatchesToVisitObjectCreateExpression) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    RecordingVisitor v;
    oce.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"objectcreate", "simple:Foo", "id:Foo"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_ObjectCreateExpression, AcceptVisitorIsVirtualThroughBases) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    AstNode* asAst = &oce;
    Expression* asExpr = &oce;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"objectcreate", "simple:Foo", "id:Foo"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"objectcreate", "simple:Foo", "id:Foo"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits the `Type` (a `VisitSimpleType` -> its `Identifier`), then the
// `Arguments` (a `VisitIdentifierExpression` per element), then the `Initializer` (a
// `VisitArrayInitializerExpression`) in document order.
TEST(CSharp_ObjectCreateExpression, DepthFirstWalkVisitsTypeArgumentsInitializerInOrder) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    oce.Arguments().Add(x.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    RecordingVisitor v;
    oce.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "objectcreate", "simple:Foo", "id:Foo", "idexpr:x", "id:x", "arrinit"}));
}

// ---- DoMatch (the generated recursive + collection + nullable match) --

// Two `ObjectCreateExpression`s with the same type, the same (single) argument, and both with no
// initializer match.
TEST(CSharp_ObjectCreateExpression, DoMatchMatchesSameTypeAndArgumentsNoInitializer) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Type` rejects the match (the `MatchRequired` on `Type` -- the first term --
// rejects via the child's `DoMatch`).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsDifferentType) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Bar"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // type names differ ("Foo" vs "Bar")
}

// A null pattern `Type` rejects (the `MatchRequired` guard -- the first term).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsNullPatternType) {
    ObjectCreateExpression a;  // no type
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Type` rejects (the `MatchRequired` flows the null child through the child's
// `DoMatch(nullptr)`, which returns false).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsNullCandidateType) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b;  // no type
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `ObjectCreateExpression`s with the same type but different argument COUNTS do not match
// (the collection match fails when the counts differ).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsDifferentArgumentCount) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `ObjectCreateExpression`s with the same type and count but different argument VALUES do
// not match (the element `DoMatch` -- a `MatchString` on the `IdentifierExpression` identifier --
// rejects).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsDifferentArgumentValue) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("y"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `ObjectCreateExpression`s with the same type and arguments where BOTH have no initializer
// match (the `MatchOptional` returns true when both are absent).
TEST(CSharp_ObjectCreateExpression, DoMatchMatchesWhenBothInitializersAbsent) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));  // neither has an initializer
}

// A pattern with an `Initializer` does not match a candidate without one (the `MatchOptional`
// rejects when the pattern is present but the candidate is absent).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsPatternInitializerPresentCandidateAbsent) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    a.Initializer(init.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A pattern without an `Initializer` does not match a candidate with one (the `MatchOptional`
// rejects when the pattern is absent but the candidate is present).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsPatternInitializerAbsentCandidatePresent) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    b.Initializer(init.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `ObjectCreateExpression`s with the same type, arguments, and both with an (empty)
// initializer match (the `MatchOptional` delegates to the pattern's `DoMatch`, and two empty
// `ArrayInitializerExpression`s match).
TEST(CSharp_ObjectCreateExpression, DoMatchMatchesWhenBothInitializersPresent) {
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression a(ta.get());
    ObjectCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    auto ia = std::make_unique<ArrayInitializerExpression>();
    auto ib = std::make_unique<ArrayInitializerExpression>();
    a.Initializer(ia.get());
    b.Initializer(ib.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// An `ObjectCreateExpression` does not match a different concrete type (the `other is
// ObjectCreateExpression` gate); an `InvocationExpression` is not an `ObjectCreateExpression`.
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsInvocationExpressionCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_FALSE(DoMatchAgainst(&oce, &ie));
    EXPECT_FALSE(DoMatchAgainst(&ie, &oce));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_ObjectCreateExpression, DoMatchRejectsNullCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    EXPECT_FALSE(DoMatchAgainst(&oce, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the type, the arguments, and the initializer, re-parents the clones, and
// detaches from the source.
TEST(CSharp_ObjectCreateExpression, CloneDeepCopiesTypeArgumentsInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"), TextLocation(1, 1));
    ObjectCreateExpression original(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    original.Arguments().Add(x.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    original.Initializer(init.get());

    std::unique_ptr<ObjectCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The type is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(dynamic_cast<SimpleType*>(copy->Type())->Identifier().value_or(""), "Foo");
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    // The argument is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Arguments().Count(), 1);
    ASSERT_NE(copy->Arguments().At(0), nullptr);
    EXPECT_NE(copy->Arguments().At(0), x.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Arguments().At(0))->Identifier(), "x");
    EXPECT_EQ(copy->Arguments().At(0)->Parent(), copy.get());
    // The initializer is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Initializer(), nullptr);
    EXPECT_NE(copy->Initializer(), init.get());
    EXPECT_EQ(copy->Initializer()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Arguments().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
    EXPECT_EQ(type->Parent(), &original);
    EXPECT_EQ(init->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_ObjectCreateExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression original(type.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<ObjectCreateExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<ObjectCreateExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of an empty `ObjectCreateExpression` (no type, no arguments, no initializer) yields an
// empty clone.
TEST(CSharp_ObjectCreateExpression, CloneOfEmptyIsEmpty) {
    ObjectCreateExpression original;  // no type, no arguments, no initializer
    std::unique_ptr<ObjectCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->Arguments().Count(), 0);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// `Clone` of a node with a type and arguments but NO initializer yields a clone with no
// initializer (the optional slot is skipped when absent).
TEST(CSharp_ObjectCreateExpression, CloneWithoutInitializerHasNoInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression original(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    original.Arguments().Add(x.get());
    std::unique_ptr<ObjectCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_EQ(copy->Arguments().Count(), 1);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `ObjectCreateExpression` with the type set and arguments passes the inherited
// `CheckInvariant` (the `Type` is a required slot; the `Initializer` is optional, so its absence
// does not violate the invariant). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_ObjectCreateExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    oce.Arguments().Add(x.get());
    oce.CheckInvariant();  // no initializer -- still valid (optional slot)
}

// An `ObjectCreateExpression` with the type, arguments, AND initializer set passes the inherited
// `CheckInvariant`.
TEST(CSharp_ObjectCreateExpression, CheckInvariantPassesOnFilledNodeWithInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    oce.Arguments().Add(x.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    oce.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `TypeSlot`, `ArgumentsSlot`, and `InitializerSlot` are distinct slot statics (compared by
// address); the node's `Slot()` reports the per-node slot for each child. The three slot statics
// have DIFFERENT `CSharpSlotInfoT<T>` element types (`AstType`/`Expression`/
// `ArrayInitializerExpression`), so they are unrelated pointer types and must be compared by
// address through the common `CSharpSlotInfo*` base (the D222 precedent).
TEST(CSharp_ObjectCreateExpression, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&ObjectCreateExpression::TypeSlot,
              (const CSharpSlotInfo*)&ObjectCreateExpression::ArgumentsSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ObjectCreateExpression::TypeSlot,
              (const CSharpSlotInfo*)&ObjectCreateExpression::InitializerSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ObjectCreateExpression::ArgumentsSlot,
              (const CSharpSlotInfo*)&ObjectCreateExpression::InitializerSlot);
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    ObjectCreateExpression oce(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    oce.Arguments().Add(x.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    oce.Initializer(init.get());
    EXPECT_EQ(oce.Type()->Slot(), &ObjectCreateExpression::TypeSlot);
    EXPECT_EQ(oce.Type()->Slot()->Kind(), &Slots::Type);
    EXPECT_EQ(oce.Arguments().At(0)->Slot(), &ObjectCreateExpression::ArgumentsSlot);
    EXPECT_EQ(oce.Arguments().At(0)->Slot()->Kind(), &Slots::Argument);
    EXPECT_EQ(oce.Initializer()->Slot(), &ObjectCreateExpression::InitializerSlot);
    EXPECT_EQ(oce.Initializer()->Slot()->Kind(), &Slots::Initializer);
}
