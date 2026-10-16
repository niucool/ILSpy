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

// Tests for the `ArrayCreateExpression` concrete node (cpp/.../Syntax/Expressions/
// ArrayCreateExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.cs) -- the first
// ported node with TWO collections FOLLOWED BY a single slot:
// `array_creation_expression ::= 'new' type '[' expression* ']' array_specifier*
// array_initializer?` (C# grammar 12.8.17.5). Exercises the `Type` (required `AstType`) /
// `Arguments` (NON-incremental `AstNodeCollection<Expression>`) / `AdditionalArraySpecifiers`
// (NON-incremental `AstNodeCollection<ArraySpecifier>`) / `Initializer` (nullable single
// `ArrayInitializerExpression`) slots, the collection-aware slot-storage contract (a single ->
// collection -> collection -> single dispatch), the `AcceptVisitor` dispatch, the generated
// `DoMatch` (a `MatchRequired` on `Type` + a collection `DoMatch` on `Arguments` + a collection
// `DoMatch` on `AdditionalArraySpecifiers` + a `MatchOptional` on `Initializer`), the
// per-concrete-node `Clone`, and the inherited `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
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
// `VisitArrayCreateExpression`/`VisitSimpleType`/`VisitIdentifier`/`VisitIdentifierExpression`/
// `VisitArraySpecifier`/`VisitArrayInitializerExpression` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitArrayCreateExpression(ArrayCreateExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("arraycreate");
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
    void VisitArraySpecifier(ArraySpecifier* node) override {
        if (node == nullptr) { trace.push_back("<null-arrspec>"); return; }
        trace.push_back("arrspec:" + std::to_string(node->Dimensions()));
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

// `ArrayCreateExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_ArrayCreateExpression, IsExpressionAndAstNodeNotAstType) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    EXPECT_NE(dynamic_cast<Expression*>(&ace), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ace), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ace), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no type, no arguments, no additional array specifiers, and no initializer.
// `GetChildCount` is 2 (the two empty single slots `Type` + `Initializer`) + 0 arguments + 0
// additional specifiers.
TEST(CSharp_ArrayCreateExpression, EmptyCtorHasNoSlots) {
    ArrayCreateExpression ace;
    EXPECT_EQ(ace.Type(), nullptr);
    EXPECT_EQ(ace.Arguments().Count(), 0);
    EXPECT_EQ(ace.AdditionalArraySpecifiers().Count(), 0);
    EXPECT_EQ(ace.Initializer(), nullptr);
    EXPECT_EQ(ace.GetChildCount(), 2);  // the two single slots (empty) + 0 arguments + 0 specifiers
    EXPECT_EQ(ace.StartLocation(), TextLocation::Empty);
    EXPECT_STREQ(ace.NewKeyword, "new");
}

// The `(AstType)` ctor (the generated required-prefix ctor) sets the `Type` and parents it at
// index 0.
TEST(CSharp_ArrayCreateExpression, CtorSetsType) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    EXPECT_EQ(ace.Type(), type.get());
    EXPECT_EQ(type->Parent(), &ace);
    EXPECT_EQ(type->ChildIndex, 0);
}

// ---- The `Type` slot ------------------------------------------------

// The `Type` setter re-parents the new type and detaches the old one.
TEST(CSharp_ArrayCreateExpression, TypeSetterReparentsAndDetaches) {
    ArrayCreateExpression ace;
    auto a = std::make_unique<SimpleType>(std::string("a"));
    auto b = std::make_unique<SimpleType>(std::string("b"));
    ace.Type(a.get());
    EXPECT_EQ(a->Parent(), &ace);
    EXPECT_EQ(ace.Type(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    ace.Type(b.get());
    EXPECT_EQ(b->Parent(), &ace);
    EXPECT_EQ(ace.Type(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Type` setter clears and detaches the old type.
TEST(CSharp_ArrayCreateExpression, TypeSetterClearsWithNull) {
    ArrayCreateExpression ace;
    auto a = std::make_unique<SimpleType>(std::string("a"));
    ace.Type(a.get());
    ace.Type(nullptr);
    EXPECT_EQ(ace.Type(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Arguments` collection ----------------------------------------

// The collection starts empty (0 count); the node's child count is the two single slots (2) +
// 0 arguments + 0 specifiers.
TEST(CSharp_ArrayCreateExpression, ArgumentsEmptyByDefault) {
    ArrayCreateExpression ace;
    EXPECT_EQ(ace.Arguments().Count(), 0);
    EXPECT_EQ(ace.GetChildCount(), 2);
}

// `Add` appends an element and parents it; the collection is NON-incremental (the node has TWO
// collections, so neither owns the contiguous trailing range), so `Add` INVALIDATES the
// parent's indices. The element's `ChildIndex` is stale until a reindex is triggered.
TEST(CSharp_ArrayCreateExpression, ArgumentsAddAppendsAndParentsAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ace.Arguments().Add(x.get());
    ace.Arguments().Add(y.get());
    EXPECT_EQ(ace.Arguments().Count(), 2);
    EXPECT_EQ(x->Parent(), &ace);
    EXPECT_EQ(y->Parent(), &ace);
    EXPECT_FALSE(ace.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(ace.GetChildCount(), 4);  // type + 2 arguments + initializer slot
}

// `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind and null for
// the single-slot kinds. (`Slots::AdditionalArraySpecifier` is NOT null here -- it is the kind
// of this node's own `AdditionalArraySpecifiers` collection, exercised separately below.)
TEST(CSharp_ArrayCreateExpression, GetCollectionByKindReturnsArgumentsForArgumentKind) {
    ArrayCreateExpression ace;
    EXPECT_NE(ace.GetCollectionByKind(&Slots::Argument), nullptr);
    EXPECT_EQ(ace.GetCollectionByKind(&Slots::Argument), &ace.Arguments());
    EXPECT_EQ(ace.GetCollectionByKind(&Slots::Type), nullptr);                  // a single slot
    EXPECT_EQ(ace.GetCollectionByKind(&Slots::Initializer), nullptr);           // a single slot
    EXPECT_EQ(ace.GetCollectionByKind(nullptr), nullptr);
}

// ---- The `AdditionalArraySpecifiers` collection ------------------------

// The collection starts empty (0 count).
TEST(CSharp_ArrayCreateExpression, AdditionalArraySpecifiersEmptyByDefault) {
    ArrayCreateExpression ace;
    EXPECT_EQ(ace.AdditionalArraySpecifiers().Count(), 0);
}

// `Add` appends an `ArraySpecifier` and parents it; the collection is NON-incremental (two
// collections), so `Add` INVALIDATES the parent's indices.
TEST(CSharp_ArrayCreateExpression, AdditionalArraySpecifiersAddAppendsAndParentsAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    auto a1 = std::make_unique<ArraySpecifier>(2);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    ace.AdditionalArraySpecifiers().Add(a1.get());
    EXPECT_EQ(ace.AdditionalArraySpecifiers().Count(), 2);
    EXPECT_EQ(a0->Parent(), &ace);
    EXPECT_EQ(a1->Parent(), &ace);
    EXPECT_FALSE(ace.ChildIndicesValid());  // non-incremental: Add invalidates
}

// `GetCollectionByKind` returns the `AdditionalArraySpecifiers` collection for the
// `AdditionalArraySpecifier` kind (distinct from the `ArraySpecifier` kind used by
// `ComposedType.ArraySpecifiers`).
TEST(CSharp_ArrayCreateExpression, GetCollectionByKindReturnsAdditionalArraySpecifiersForKind) {
    ArrayCreateExpression ace;
    EXPECT_NE(ace.GetCollectionByKind(&Slots::AdditionalArraySpecifier), nullptr);
    EXPECT_EQ(ace.GetCollectionByKind(&Slots::AdditionalArraySpecifier),
              &ace.AdditionalArraySpecifiers());
    EXPECT_EQ(ace.GetCollectionByKind(&Slots::ArraySpecifier), nullptr);  // ComposedType's kind
}

// ---- The `Initializer` slot (a nullable single child after TWO collections) --

// The `Initializer` defaults to null (an optional slot).
TEST(CSharp_ArrayCreateExpression, InitializerNullByDefault) {
    ArrayCreateExpression ace;
    EXPECT_EQ(ace.Initializer(), nullptr);
}

// The `Initializer` setter re-parents the new initializer and detaches the old one; the setter is
// index-less (two collections precede the slot), so a set INVALIDATES the parent's indices.
TEST(CSharp_ArrayCreateExpression, InitializerSetterReparentsAndDetachesAndInvalidates) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto a = std::make_unique<ArrayInitializerExpression>();
    auto b = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(a.get());
    EXPECT_EQ(a->Parent(), &ace);
    EXPECT_EQ(ace.Initializer(), a.get());
    EXPECT_FALSE(ace.ChildIndicesValid());  // index-less setter: set invalidates
    ace.Initializer(b.get());
    EXPECT_EQ(b->Parent(), &ace);
    EXPECT_EQ(ace.Initializer(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `Initializer` setter clears and detaches the old initializer.
TEST(CSharp_ArrayCreateExpression, InitializerSetterClearsWithNull) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto a = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(a.get());
    ace.Initializer(nullptr);
    EXPECT_EQ(ace.Initializer(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the type at index 0, the arguments at index 1+, the additional array
// specifiers at index `1 + argCount +`, and the initializer at index `1 + argCount + arrCount`.
TEST(CSharp_ArrayCreateExpression, GetChildDispatchesAllSlots) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ace.Arguments().Add(x.get());
    ace.Arguments().Add(y.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    auto a1 = std::make_unique<ArraySpecifier>(2);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    ace.AdditionalArraySpecifiers().Add(a1.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    EXPECT_EQ(ace.GetChild(0), type.get());
    EXPECT_EQ(ace.GetChild(1), x.get());
    EXPECT_EQ(ace.GetChild(2), y.get());
    EXPECT_EQ(ace.GetChild(3), a0.get());
    EXPECT_EQ(ace.GetChild(4), a1.get());
    EXPECT_EQ(ace.GetChild(5), init.get());
    EXPECT_THROW(ace.GetChild(6), std::out_of_range);
    EXPECT_THROW(ace.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the matching per-node slot static for each child index (the slot
// identity the slot system compares by address).
TEST(CSharp_ArrayCreateExpression, GetChildSlotInfoDispatchesAllSlots) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ace.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    EXPECT_EQ(ace.GetChildSlotInfo(0), &ArrayCreateExpression::TypeSlot);
    EXPECT_EQ(ace.GetChildSlotInfo(1), &ArrayCreateExpression::ArgumentsSlot);
    EXPECT_EQ(ace.GetChildSlotInfo(2), &ArrayCreateExpression::AdditionalArraySpecifiersSlot);
    EXPECT_EQ(ace.GetChildSlotInfo(3), &ArrayCreateExpression::InitializerSlot);
    EXPECT_THROW(ace.GetChildSlotInfo(4), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Type` KIND (the shared `Slots.Type`).
TEST(CSharp_ArrayCreateExpression, TypeSlotPointsAtTypeKind) {
    ArrayCreateExpression ace;
    EXPECT_EQ(ace.GetChildSlotInfo(0)->Kind(), &Slots::Type);
}

// `GetChildSlotInfo(1)` points at the `Argument` KIND (the shared `Slots.Argument`).
TEST(CSharp_ArrayCreateExpression, ArgumentsSlotPointsAtArgumentKind) {
    ArrayCreateExpression ace;
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ace.Arguments().Add(x.get());
    EXPECT_EQ(ace.GetChildSlotInfo(1)->Kind(), &Slots::Argument);
}

// `GetChildSlotInfo(1 + argCount)` points at the `AdditionalArraySpecifier` KIND (the new shared
// `Slots.AdditionalArraySpecifier`, distinct from `ComposedType`'s `Slots.ArraySpecifier`).
TEST(CSharp_ArrayCreateExpression, AdditionalArraySpecifiersSlotPointsAtKind) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    // The AdditionalArraySpecifiers occupy flattened index 1 (no arguments); trigger a reindex so
    // GetChildSlotInfo routes to the collection.
    (void)type->Slot();
    EXPECT_EQ(ace.GetChildSlotInfo(1)->Kind(), &Slots::AdditionalArraySpecifier);
    EXPECT_NE(ace.GetChildSlotInfo(1)->Kind(), &Slots::ArraySpecifier);  // distinct from ComposedType
}

// `GetChildSlotInfo(1 + argCount + arrCount)` points at the `Initializer` KIND (the
// cycle-broken `Slots.Initializer`).
TEST(CSharp_ArrayCreateExpression, InitializerSlotPointsAtInitializerKind) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    // The Initializer is at flattened index 2 (no arguments, 1 additional specifier); trigger a
    // reindex so GetChildSlotInfo routes to the trailing single slot.
    (void)type->Slot();
    EXPECT_EQ(ace.GetChildSlotInfo(2)->Kind(), &Slots::Initializer);
}

// `SetChild` writes the type at index 0, replaces an argument in place at index 1+, replaces an
// additional array specifier in place at index `1 + argCount +`, and writes the initializer at
// index `1 + argCount + arrCount` (the collection's `SetAt` re-parents and carries the old index;
// the single-slot `SetChildNode` re-parents).
TEST(CSharp_ArrayCreateExpression, SetChildDispatchesAllSlots) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ace.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    // Replace the type at index 0.
    auto type2 = std::make_unique<SimpleType>(std::string("byte"));
    ace.SetChild(0, type2.get());
    EXPECT_EQ(ace.Type(), type2.get());
    EXPECT_EQ(type2->Parent(), &ace);
    // Replace the argument at index 1.
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ace.SetChild(1, y.get());
    EXPECT_EQ(ace.Arguments().At(0), y.get());
    EXPECT_EQ(y->Parent(), &ace);
    // Replace the additional array specifier at index 2 (1 argument).
    auto a1 = std::make_unique<ArraySpecifier>(3);
    ace.SetChild(2, a1.get());
    EXPECT_EQ(ace.AdditionalArraySpecifiers().At(0), a1.get());
    EXPECT_EQ(a1->Parent(), &ace);
    // Write the initializer at index 3 (1 argument, 1 additional specifier).
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.SetChild(3, init.get());
    EXPECT_EQ(ace.Initializer(), init.get());
    EXPECT_EQ(init->Parent(), &ace);
    EXPECT_THROW(ace.SetChild(4, nullptr), std::out_of_range);  // no slot at index 4
}

// ---- The dynamic flattened-index layout (non-incremental) -----------

// After `Add`/set the parent's indices are invalid; the reindex is triggered by `Slot()` (which
// calls `EnsureChildIndices` on the parent), after which each child's `ChildIndex` is its
// correct flattened index: `Type` at 0, `Arguments` at `[1, 1 + argCount)`,
// `AdditionalArraySpecifiers` at `[1 + argCount, 1 + argCount + arrCount)`, `Initializer` at
// `1 + argCount + arrCount`.
TEST(CSharp_ArrayCreateExpression, ChildIndicesRebuiltAfterReindex) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    auto y = std::make_unique<IdentifierExpression>(std::string("y"));
    ace.Arguments().Add(x.get());
    ace.Arguments().Add(y.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    auto a1 = std::make_unique<ArraySpecifier>(2);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    ace.AdditionalArraySpecifiers().Add(a1.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    ASSERT_FALSE(ace.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)type->Slot();
    ASSERT_TRUE(ace.ChildIndicesValid());
    EXPECT_EQ(type->ChildIndex, 0);  // Type at 0
    EXPECT_EQ(x->ChildIndex, 1);     // Arguments[0] at 1
    EXPECT_EQ(y->ChildIndex, 2);     // Arguments[1] at 2
    EXPECT_EQ(a0->ChildIndex, 3);    // AdditionalArraySpecifiers[0] at 1 + argCount (1 + 2)
    EXPECT_EQ(a1->ChildIndex, 4);    // AdditionalArraySpecifiers[1] at 4
    EXPECT_EQ(init->ChildIndex, 5);  // Initializer at 1 + argCount + arrCount (1 + 2 + 2)
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitArrayCreateExpression` (the visitor-pattern round-trip);
// the depth-first walk then visits the `Type` child (a `VisitSimpleType` -> its `Identifier`).
TEST(CSharp_ArrayCreateExpression, AcceptVisitorDispatchesToVisitArrayCreateExpression) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    RecordingVisitor v;
    ace.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"arraycreate", "simple:int", "id:int"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_ArrayCreateExpression, AcceptVisitorIsVirtualThroughBases) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    AstNode* asAst = &ace;
    Expression* asExpr = &ace;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"arraycreate", "simple:int", "id:int"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"arraycreate", "simple:int", "id:int"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits the `Type` (a `VisitSimpleType` -> its `Identifier`), then the
// `Arguments` (a `VisitIdentifierExpression` per element -> its `Identifier`), then the
// `AdditionalArraySpecifiers` (a `VisitArraySpecifier` per element), then the `Initializer` (a
// `VisitArrayInitializerExpression`) in document order.
TEST(CSharp_ArrayCreateExpression, DepthFirstWalkVisitsAllSlotsInOrder) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    ace.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    RecordingVisitor v;
    ace.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "arraycreate", "simple:int", "id:int",
        "idexpr:x", "id:x",
        "arrspec:1",
        "arrinit"}));
}

// ---- DoMatch (the generated recursive + collection + nullable match) --

// Two `ArrayCreateExpression`s with the same type, the same (single) argument, the same
// (single) additional array specifier, and both with no initializer match.
TEST(CSharp_ArrayCreateExpression, DoMatchMatchesSameSlotsNoInitializer) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    auto aa = std::make_unique<ArraySpecifier>(1);
    auto ab = std::make_unique<ArraySpecifier>(1);
    a.AdditionalArraySpecifiers().Add(aa.get());
    b.AdditionalArraySpecifiers().Add(ab.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Type` rejects the match (the `MatchRequired` on `Type` -- the first term --
// rejects via the child's `DoMatch`).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsDifferentType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("byte"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // type names differ ("int" vs "byte")
}

// A null pattern `Type` rejects (the `MatchRequired` guard -- the first term).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsNullPatternType) {
    ArrayCreateExpression a;  // no type
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression b(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Type` rejects (the `MatchRequired` flows the null child through the child's
// `DoMatch(nullptr)`, which returns false).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsNullCandidateType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b;  // no type
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `ArrayCreateExpression`s with the same type but different argument COUNTS do not match
// (the `Arguments` collection match fails when the counts differ).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsDifferentArgumentCount) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `ArrayCreateExpression`s with the same type and argument count but different argument
// VALUES do not match (the element `DoMatch` -- a `MatchString` on the `IdentifierExpression`
// identifier -- rejects).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsDifferentArgumentValue) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("y"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `ArrayCreateExpression`s with the same type and arguments but different
// `AdditionalArraySpecifiers` COUNTS do not match (the `AdditionalArraySpecifiers` collection
// match fails when the counts differ).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsDifferentAdditionalArraySpecifierCount) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto aa = std::make_unique<ArraySpecifier>(1);
    a.AdditionalArraySpecifiers().Add(aa.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 specifier, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `ArrayCreateExpression`s with the same type, arguments, and additional array specifier
// count but different additional array specifier VALUES (different `Dimensions`) do not match
// (the element `DoMatch` -- a plain-equality on the `ArraySpecifier.Dimensions` int -- rejects).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsDifferentAdditionalArraySpecifierValue) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto aa = std::make_unique<ArraySpecifier>(1);
    auto ab = std::make_unique<ArraySpecifier>(2);
    a.AdditionalArraySpecifiers().Add(aa.get());
    b.AdditionalArraySpecifiers().Add(ab.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // Dimensions 1 vs 2
}

// Two `ArrayCreateExpression`s with the same type, arguments, and additional array specifiers
// where BOTH have no initializer match (the `MatchOptional` returns true when both are absent).
TEST(CSharp_ArrayCreateExpression, DoMatchMatchesWhenBothInitializersAbsent) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));  // neither has an initializer
}

// A pattern with an `Initializer` does not match a candidate without one (the `MatchOptional`
// rejects when the pattern is present but the candidate is absent).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsPatternInitializerPresentCandidateAbsent) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
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
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsPatternInitializerAbsentCandidatePresent) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    b.Initializer(init.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `ArrayCreateExpression`s with the same type, arguments, additional array specifiers, and
// both with an (empty) initializer match (the `MatchOptional` delegates to the pattern's
// `DoMatch`, and two empty `ArrayInitializerExpression`s match).
TEST(CSharp_ArrayCreateExpression, DoMatchMatchesWhenBothInitializersPresent) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression a(ta.get());
    ArrayCreateExpression b(tb.get());
    auto ax = std::make_unique<IdentifierExpression>(std::string("x"));
    auto bx = std::make_unique<IdentifierExpression>(std::string("x"));
    a.Arguments().Add(ax.get());
    b.Arguments().Add(bx.get());
    auto aa = std::make_unique<ArraySpecifier>(1);
    auto ab = std::make_unique<ArraySpecifier>(1);
    a.AdditionalArraySpecifiers().Add(aa.get());
    b.AdditionalArraySpecifiers().Add(ab.get());
    auto ia = std::make_unique<ArrayInitializerExpression>();
    auto ib = std::make_unique<ArrayInitializerExpression>();
    a.Initializer(ia.get());
    b.Initializer(ib.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// An `ArrayCreateExpression` does not match a different concrete type (the `other is
// ArrayCreateExpression` gate); an `InvocationExpression` is not an `ArrayCreateExpression`.
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsInvocationExpressionCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    InvocationExpression ie(target.get());
    EXPECT_FALSE(DoMatchAgainst(&ace, &ie));
    EXPECT_FALSE(DoMatchAgainst(&ie, &ace));
}

// An `ArrayCreateExpression` does not match an `ObjectCreateExpression` (the two share the
// `Type` + `Arguments` + `Initializer` shape but `ArrayCreateExpression` adds the
// `AdditionalArraySpecifiers` collection, so the type-check gate rejects the structural-twin
// across the disjoint concrete types).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsObjectCreateExpressionCandidate) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(ta.get());
    ObjectCreateExpression oce(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&ace, &oce));
    EXPECT_FALSE(DoMatchAgainst(&oce, &ace));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_ArrayCreateExpression, DoMatchRejectsNullCandidate) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    EXPECT_FALSE(DoMatchAgainst(&ace, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the type, the arguments, the additional array specifiers, and the
// initializer, re-parents the clones, and detaches from the source.
TEST(CSharp_ArrayCreateExpression, CloneDeepCopiesAllSlots) {
    auto type = std::make_unique<SimpleType>(std::string("int"), TextLocation(1, 1));
    ArrayCreateExpression original(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"), TextLocation(1, 5));
    original.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    original.AdditionalArraySpecifiers().Add(a0.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    original.Initializer(init.get());

    std::unique_ptr<ArrayCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The type is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_NE(copy->Type(), type.get());
    EXPECT_EQ(dynamic_cast<SimpleType*>(copy->Type())->Identifier().value_or(""), "int");
    EXPECT_EQ(copy->Type()->Parent(), copy.get());
    // The argument is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->Arguments().Count(), 1);
    ASSERT_NE(copy->Arguments().At(0), nullptr);
    EXPECT_NE(copy->Arguments().At(0), x.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Arguments().At(0))->Identifier(), "x");
    EXPECT_EQ(copy->Arguments().At(0)->Parent(), copy.get());
    // The additional array specifier is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->AdditionalArraySpecifiers().Count(), 1);
    ASSERT_NE(copy->AdditionalArraySpecifiers().At(0), nullptr);
    EXPECT_NE(copy->AdditionalArraySpecifiers().At(0), a0.get());
    EXPECT_EQ(copy->AdditionalArraySpecifiers().At(0)->Dimensions(), 1);
    EXPECT_EQ(copy->AdditionalArraySpecifiers().At(0)->Parent(), copy.get());
    // The initializer is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Initializer(), nullptr);
    EXPECT_NE(copy->Initializer(), init.get());
    EXPECT_EQ(copy->Initializer()->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Arguments().Count(), 1);
    EXPECT_EQ(original.AdditionalArraySpecifiers().Count(), 1);
    EXPECT_EQ(x->Parent(), &original);
    EXPECT_EQ(a0->Parent(), &original);
    EXPECT_EQ(type->Parent(), &original);
    EXPECT_EQ(init->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_ArrayCreateExpression, CloneIsVirtualAndCovariant) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression original(type.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<ArrayCreateExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<ArrayCreateExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of an empty `ArrayCreateExpression` (no type, no arguments, no additional array
// specifiers, no initializer) yields an empty clone.
TEST(CSharp_ArrayCreateExpression, CloneOfEmptyIsEmpty) {
    ArrayCreateExpression original;  // no slots
    std::unique_ptr<ArrayCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Type(), nullptr);
    EXPECT_EQ(copy->Arguments().Count(), 0);
    EXPECT_EQ(copy->AdditionalArraySpecifiers().Count(), 0);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// `Clone` of a node with a type, arguments, and additional array specifiers but NO initializer
// yields a clone with no initializer (the optional slot is skipped when absent).
TEST(CSharp_ArrayCreateExpression, CloneWithoutInitializerHasNoInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression original(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    original.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    original.AdditionalArraySpecifiers().Add(a0.get());
    std::unique_ptr<ArrayCreateExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(copy->Type(), nullptr);
    EXPECT_EQ(copy->Arguments().Count(), 1);
    EXPECT_EQ(copy->AdditionalArraySpecifiers().Count(), 1);
    EXPECT_EQ(copy->Initializer(), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `ArrayCreateExpression` with the type set, arguments, and additional array specifiers
// passes the inherited `CheckInvariant` (the `Type` is a required slot; the `Initializer` is
// optional, so its absence does not violate the invariant). Runs in debug builds (a no-op in
// NDEBUG).
TEST(CSharp_ArrayCreateExpression, CheckInvariantPassesOnFilledNode) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ace.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    ace.CheckInvariant();  // no initializer -- still valid (optional slot)
}

// An `ArrayCreateExpression` with the type, arguments, additional array specifiers, AND
// initializer set passes the inherited `CheckInvariant`.
TEST(CSharp_ArrayCreateExpression, CheckInvariantPassesOnFilledNodeWithInitializer) {
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ace.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    ace.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The four slot statics are distinct (compared by address); the node's `Slot()` reports the
// per-node slot for each child. The four slot statics have DIFFERENT `CSharpSlotInfoT<T>`
// element types (`AstType`/`Expression`/`ArraySpecifier`/`ArrayInitializerExpression`), so they
// are unrelated pointer types and must be compared by address through the common
// `CSharpSlotInfo*` base (the D222/D251 precedent).
TEST(CSharp_ArrayCreateExpression, SlotStaticsAreDistinct) {
    EXPECT_NE((const CSharpSlotInfo*)&ArrayCreateExpression::TypeSlot,
              (const CSharpSlotInfo*)&ArrayCreateExpression::ArgumentsSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ArrayCreateExpression::TypeSlot,
              (const CSharpSlotInfo*)&ArrayCreateExpression::AdditionalArraySpecifiersSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ArrayCreateExpression::TypeSlot,
              (const CSharpSlotInfo*)&ArrayCreateExpression::InitializerSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ArrayCreateExpression::ArgumentsSlot,
              (const CSharpSlotInfo*)&ArrayCreateExpression::AdditionalArraySpecifiersSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ArrayCreateExpression::ArgumentsSlot,
              (const CSharpSlotInfo*)&ArrayCreateExpression::InitializerSlot);
    EXPECT_NE((const CSharpSlotInfo*)&ArrayCreateExpression::AdditionalArraySpecifiersSlot,
              (const CSharpSlotInfo*)&ArrayCreateExpression::InitializerSlot);
    auto type = std::make_unique<SimpleType>(std::string("int"));
    ArrayCreateExpression ace(type.get());
    auto x = std::make_unique<IdentifierExpression>(std::string("x"));
    ace.Arguments().Add(x.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    ace.AdditionalArraySpecifiers().Add(a0.get());
    auto init = std::make_unique<ArrayInitializerExpression>();
    ace.Initializer(init.get());
    EXPECT_EQ(ace.Type()->Slot(), &ArrayCreateExpression::TypeSlot);
    EXPECT_EQ(ace.Type()->Slot()->Kind(), &Slots::Type);
    EXPECT_EQ(ace.Arguments().At(0)->Slot(), &ArrayCreateExpression::ArgumentsSlot);
    EXPECT_EQ(ace.Arguments().At(0)->Slot()->Kind(), &Slots::Argument);
    EXPECT_EQ(ace.AdditionalArraySpecifiers().At(0)->Slot(),
              &ArrayCreateExpression::AdditionalArraySpecifiersSlot);
    EXPECT_EQ(ace.AdditionalArraySpecifiers().At(0)->Slot()->Kind(),
              &Slots::AdditionalArraySpecifier);
    EXPECT_EQ(ace.Initializer()->Slot(), &ArrayCreateExpression::InitializerSlot);
    EXPECT_EQ(ace.Initializer()->Slot()->Kind(), &Slots::Initializer);
}
