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

// Tests for the `ComposedType` concrete node (cpp/.../Syntax/ComposedType.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/ComposedType.cs) -- the third concrete `AstType` with a
// collection slot and the FIRST ported node with TWO collection slots (`Attributes` + `ArraySpecifiers`),
// with a required `BaseType` `AstType` single slot between them, plus `HasRefSpecifier`/
// `HasReadOnlySpecifier`/`HasNullableSpecifier` bool scalars and a `PointerRank` int scalar.
// Exercises the const keyword tokens, the four scalars, the required `BaseType` slot (the first
// single slot that FOLLOWS a collection, so its setter uses the index-less path that invalidates),
// the two non-incremental collections, the three-slot collection-aware slot-storage contract
// (collection -> single -> collection), the `AcceptVisitor` dispatch + the two-collection
// depth-first walk, the generated seven-term `DoMatch`, the per-concrete-node `Clone`, and the
// `PointerRank >= 0` `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
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

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete types, recursing via the inherited `VisitChildren` (the document-order walk).
// `VisitComposedType`/`VisitAttributeSection`/`VisitAttribute`/`VisitSimpleType`/
// `VisitIdentifier`/`VisitArraySpecifier` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitComposedType(ComposedType* node) override {
        if (node == nullptr) { trace.push_back("<null-ct>"); return; }
        trace.push_back("composed");
        VisitChildren(node);
    }
    void VisitAttributeSection(AttributeSection* node) override {
        if (node == nullptr) { trace.push_back("<null-sec>"); return; }
        trace.push_back("sec:\"" + node->AttributeTarget() + "\"");
        VisitChildren(node);
    }
    void VisitAttribute(Attribute* node) override {
        if (node == nullptr) { trace.push_back("<null-attr>"); return; }
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
    void VisitArraySpecifier(ArraySpecifier* node) override {
        if (node == nullptr) { trace.push_back("<null-arr>"); return; }
        trace.push_back("arrayspec:" + std::to_string(node->Dimensions()));
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// Build an `AttributeSection` carrying one `Attribute` whose `Type` is a `SimpleType` `Foo` (no
// target token, no arguments) -- a minimal attribute section for the `Attributes` collection.
// Returns a holder that keeps the `SimpleType` (the attribute's `Type`), the `Attribute`, and
// the `AttributeSection` alive: the slots are non-owning raw pointers (the parent does not take
// ownership of a child in the port's test model -- the test's `unique_ptr`s own the nodes), so
// the `SimpleType` and `Attribute` must outlive the `AttributeSection` that references them.
// `get()`/`operator->` expose the `AttributeSection*`.
struct FooAttrSection {
    std::unique_ptr<SimpleType> type;
    std::unique_ptr<Attribute> attr;
    std::unique_ptr<AttributeSection> sec;
    AttributeSection* get() const { return sec.get(); }
    AttributeSection* operator->() const { return sec.get(); }
};
FooAttrSection MakeFooAttributeSection() {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    auto attr = std::make_unique<Attribute>(type.get());
    auto sec = std::make_unique<AttributeSection>(attr.get());
    return FooAttrSection{std::move(type), std::move(attr), std::move(sec)};
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `ComposedType` is an `AstType` and an `AstNode`; it is NOT an `Expression` (it derives from
// `AstType` -> `AstNode`, not from `Expression`).
TEST(CSharp_ComposedType, IsAstTypeAndAstNodeNotExpression) {
    ComposedType ct;
    EXPECT_NE(dynamic_cast<AstNode*>(&ct), nullptr);
    EXPECT_NE(dynamic_cast<AstType*>(&ct), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&ct), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no base type, no attributes, and no array specifiers; the four scalars
// default to false/false/false/0. Both collections still occupy their (empty) slots, and the
// required `BaseType` slot occupies one flattened index even when empty, so `GetChildCount` is
// 1 (the single `BaseType` slot) + 0 + 0.
TEST(CSharp_ComposedType, EmptyCtorHasNoChildren) {
    ComposedType ct;
    EXPECT_EQ(ct.BaseType(), nullptr);
    EXPECT_EQ(ct.Attributes().Count(), 0);
    EXPECT_EQ(ct.ArraySpecifiers().Count(), 0);
    EXPECT_FALSE(ct.HasRefSpecifier());
    EXPECT_FALSE(ct.HasReadOnlySpecifier());
    EXPECT_FALSE(ct.HasNullableSpecifier());
    EXPECT_EQ(ct.PointerRank(), 0);
    EXPECT_EQ(ct.GetChildCount(), 1);  // BaseType slot (empty) + 0 + 0
}

// ---- The const keyword tokens ----------------------------------------

// The four const keyword token literals (the output-visitor tokens) port as `static constexpr
// const char*`.
TEST(CSharp_ComposedType, ConstKeywordTokens) {
    EXPECT_STREQ(ComposedType::RefKeyword, "ref");
    EXPECT_STREQ(ComposedType::ReadonlyKeyword, "readonly");
    EXPECT_STREQ(ComposedType::NullableToken, "?");
    EXPECT_STREQ(ComposedType::PointerToken, "*");
}

// ---- The four scalars --------------------------------------------------

// `HasRefSpecifier`/`HasReadOnlySpecifier`/`HasNullableSpecifier` (bools) and `PointerRank`
// (int) round-trip through their getters/setters; they are plain instance state (not child
// slots, not ctor params).
TEST(CSharp_ComposedType, ScalarsRoundTrip) {
    ComposedType ct;
    ct.HasRefSpecifier(true);
    EXPECT_TRUE(ct.HasRefSpecifier());
    ct.HasReadOnlySpecifier(true);
    EXPECT_TRUE(ct.HasReadOnlySpecifier());
    ct.HasNullableSpecifier(true);
    EXPECT_TRUE(ct.HasNullableSpecifier());
    ct.PointerRank(3);
    EXPECT_EQ(ct.PointerRank(), 3);
}

// ---- The `BaseType` slot (a required `AstType` child after a collection) ---

// The `BaseType` setter re-parents the new type and detaches the old one. Because the
// `Attributes` collection PRECEDES the single `BaseType` slot, the setter uses the index-less
// `SetChildNode` (which invalidates the parent's indices on a set, since the flattened index is
// dynamic), unlike the const-index setters of the prior single-slot nodes.
TEST(CSharp_ComposedType, BaseTypeSetterReparentsAndDetaches) {
    ComposedType ct;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    auto b = std::make_unique<SimpleType>(std::string("Foo"));
    ct.BaseType(a.get());
    EXPECT_EQ(ct.BaseType(), a.get());
    EXPECT_EQ(a->Parent(), &ct);
    ct.BaseType(b.get());
    EXPECT_EQ(ct.BaseType(), b.get());
    EXPECT_EQ(b->Parent(), &ct);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `BaseType` clears the slot and detaches the old type.
TEST(CSharp_ComposedType, BaseTypeSetterClearsOnNull) {
    ComposedType ct;
    auto a = std::make_unique<SimpleType>(std::string("int"));
    ct.BaseType(a.get());
    ASSERT_EQ(ct.BaseType(), a.get());
    ct.BaseType(nullptr);
    EXPECT_EQ(ct.BaseType(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `Attributes` collection --------------------------------------

// The collection starts empty; the node's child count is the one single slot (1) + 0 + 0.
TEST(CSharp_ComposedType, AttributesEmptyByDefault) {
    ComposedType ct;
    EXPECT_EQ(ct.Attributes().Count(), 0);
    EXPECT_EQ(ct.GetChildCount(), 1);
}

// `Add` appends an element and parents it; the collection is NON-incremental (it is not the
// node's sole collection), so `Add` INVALIDATES the parent's indices (unlike `Attribute`.
// `Arguments` whose only-and-last collection maintains the index incrementally). The element's
// `ChildIndex` is stale until a reindex is triggered.
TEST(CSharp_ComposedType, AttributesAddAppendsAndParentsAndInvalidates) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    auto sec = MakeFooAttributeSection();
    ct.Attributes().Add(sec.get());
    EXPECT_EQ(ct.Attributes().Count(), 1);
    EXPECT_EQ(sec->Parent(), &ct);
    EXPECT_FALSE(ct.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(ct.GetChildCount(), 2);  // BaseType + 1 attribute section
}

// `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind and
// null for any other kind (the `Type`/`ArraySpecifier` kinds are not this collection).
TEST(CSharp_ComposedType, GetCollectionByKindReturnsAttributesForAttributeSectionKind) {
    ComposedType ct;
    EXPECT_EQ(ct.GetCollectionByKind(&Slots::AttributeSection), &ct.Attributes());
    EXPECT_EQ(ct.GetCollectionByKind(&Slots::Type), nullptr);        // a single slot
    EXPECT_EQ(ct.GetCollectionByKind(&Slots::ArraySpecifier), &ct.ArraySpecifiers());  // the other collection
    EXPECT_EQ(ct.GetCollectionByKind(nullptr), nullptr);
}

// ---- The `ArraySpecifiers` collection ----------------------------------

// The collection starts empty.
TEST(CSharp_ComposedType, ArraySpecifiersEmptyByDefault) {
    ComposedType ct;
    EXPECT_EQ(ct.ArraySpecifiers().Count(), 0);
}

// `Add` appends an element and parents it; the collection is NON-incremental (it is not the
// node's sole collection), so `Add` INVALIDATES the parent's indices.
TEST(CSharp_ComposedType, ArraySpecifiersAddAppendsAndParentsAndInvalidates) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    auto a0 = std::make_unique<ArraySpecifier>(1);
    auto a1 = std::make_unique<ArraySpecifier>(2);
    ct.ArraySpecifiers().Add(a0.get());
    ct.ArraySpecifiers().Add(a1.get());
    EXPECT_EQ(ct.ArraySpecifiers().Count(), 2);
    EXPECT_EQ(a0->Parent(), &ct);
    EXPECT_EQ(a1->Parent(), &ct);
    EXPECT_FALSE(ct.ChildIndicesValid());  // non-incremental: Add invalidates
    EXPECT_EQ(ct.GetChildCount(), 3);  // BaseType + 0 attributes + 2 array specifiers
}

// `GetCollectionByKind` returns the `ArraySpecifiers` collection for the `ArraySpecifier` kind.
TEST(CSharp_ComposedType, GetCollectionByKindReturnsArraySpecifiersForArraySpecifierKind) {
    ComposedType ct;
    EXPECT_EQ(ct.GetCollectionByKind(&Slots::ArraySpecifier), &ct.ArraySpecifiers());
}

// ---- The dynamic flattened-index layout (two collections) ------------

// The two collections + the single slot between them produce a dynamic flattened layout: the
// `Attributes` collection occupies `[0, attrCount)`, the `BaseType` single slot is at index
// `attrCount`, and the `ArraySpecifiers` collection occupies `[attrCount + 1, attrCount + 1 +
// arrCount)`. With both collections non-incremental, `Add`/set leave the indices invalid until a
// reindex is triggered (here by `Slot()`), after which each child's `ChildIndex` is the correct
// flattened index.
TEST(CSharp_ComposedType, DynamicFlattenedIndexLayout) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    auto sec = MakeFooAttributeSection();
    auto arr = std::make_unique<ArraySpecifier>(2);
    ComposedType ct;
    ct.Attributes().Add(sec.get());       // Attributes at [0, 1)
    ct.BaseType(baseType.get());          // BaseType at index 1
    ct.ArraySpecifiers().Add(arr.get());  // ArraySpecifiers at [2, 3)
    ASSERT_FALSE(ct.ChildIndicesValid());
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)sec->Slot();
    ASSERT_TRUE(ct.ChildIndicesValid());
    EXPECT_EQ(sec->ChildIndex, 0);          // Attributes[0]
    EXPECT_EQ(baseType->ChildIndex, 1);    // the single slot at attrCount (1)
    EXPECT_EQ(arr->ChildIndex, 2);         // ArraySpecifiers[0] at attrCount + 1 (2)
}

// ---- Slot-storage contract (the collection -> single -> collection dispatch) --

// `GetChild` returns the attribute section at index 0, the base type at index `attrCount`, and
// the array specifiers at index `attrCount + 1`+; the two collections occupy their contiguous
// ranges with the single slot between them.
TEST(CSharp_ComposedType, GetChildDispatchesThreeSlots) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    auto sec0 = MakeFooAttributeSection();
    auto sec1 = MakeFooAttributeSection();
    auto arr0 = std::make_unique<ArraySpecifier>(1);
    auto arr1 = std::make_unique<ArraySpecifier>(2);
    ComposedType ct;
    ct.Attributes().Add(sec0.get());
    ct.Attributes().Add(sec1.get());
    ct.BaseType(baseType.get());
    ct.ArraySpecifiers().Add(arr0.get());
    ct.ArraySpecifiers().Add(arr1.get());
    // Layout: sec0 (0), sec1 (1), baseType (2), arr0 (3), arr1 (4).
    EXPECT_EQ(ct.GetChild(0), sec0.get());
    EXPECT_EQ(ct.GetChild(1), sec1.get());
    EXPECT_EQ(ct.GetChild(2), baseType.get());
    EXPECT_EQ(ct.GetChild(3), arr0.get());
    EXPECT_EQ(ct.GetChild(4), arr1.get());
    EXPECT_THROW(ct.GetChild(5), std::out_of_range);
    EXPECT_THROW(ct.GetChild(-1), std::out_of_range);
    EXPECT_EQ(ct.GetChildCount(), 5);
}

// `GetChildSlotInfo` returns the per-node slot static for each slot's range.
TEST(CSharp_ComposedType, GetChildSlotInfoDispatchesThreeSlots) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    auto sec = MakeFooAttributeSection();
    auto arr = std::make_unique<ArraySpecifier>(1);
    ComposedType ct;
    ct.Attributes().Add(sec.get());
    ct.BaseType(baseType.get());
    ct.ArraySpecifiers().Add(arr.get());
    EXPECT_EQ(ct.GetChildSlotInfo(0), &ComposedType::AttributesSlot);
    EXPECT_EQ(ct.GetChildSlotInfo(1), &ComposedType::BaseTypeSlot);
    EXPECT_EQ(ct.GetChildSlotInfo(2), &ComposedType::ArraySpecifiersSlot);
    EXPECT_THROW(ct.GetChildSlotInfo(3), std::out_of_range);
}

// The slot statics point at their shared kinds.
TEST(CSharp_ComposedType, SlotStaticsPointAtKinds) {
    EXPECT_EQ(ComposedType::AttributesSlot.Kind(), &Slots::AttributeSection);
    EXPECT_EQ(ComposedType::BaseTypeSlot.Kind(), &Slots::Type);
    EXPECT_EQ(ComposedType::ArraySpecifiersSlot.Kind(), &Slots::ArraySpecifier);
}

// `SetChild` writes the base type at its index and replaces a collection element in place (the
// collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_ComposedType, SetChildDispatchesThreeSlots) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    auto sec = MakeFooAttributeSection();
    auto arr = std::make_unique<ArraySpecifier>(1);
    ComposedType ct;
    ct.Attributes().Add(sec.get());
    ct.BaseType(baseType.get());
    ct.ArraySpecifiers().Add(arr.get());
    // Replace the base type at index 1.
    auto newBase = std::make_unique<SimpleType>(std::string("Foo"));
    ct.SetChild(1, newBase.get());
    EXPECT_EQ(ct.BaseType(), newBase.get());
    EXPECT_EQ(newBase->Parent(), &ct);
    // Replace the attribute section at index 0.
    auto sec2 = MakeFooAttributeSection();
    ct.SetChild(0, sec2.get());
    EXPECT_EQ(ct.Attributes().At(0), sec2.get());
    EXPECT_EQ(sec2->Parent(), &ct);
    // Replace the array specifier at index 2.
    auto arr2 = std::make_unique<ArraySpecifier>(3);
    ct.SetChild(2, arr2.get());
    EXPECT_EQ(ct.ArraySpecifiers().At(0), arr2.get());
    EXPECT_EQ(arr2->Parent(), &ct);
    EXPECT_THROW(ct.SetChild(3, nullptr), std::out_of_range);  // no element at index 3
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitComposedType`; the depth-first walk visits the two
// collections interleaved with the single slot in document order: the `Attributes` (each
// `AttributeSection` recursing into its `Attribute` and the attribute's `Type` `SimpleType`),
// then the `BaseType`, then the `ArraySpecifiers`.
TEST(CSharp_ComposedType, AcceptVisitorDispatchesAndWalksTwoCollections) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    auto sec = MakeFooAttributeSection();  // section with one Attribute (Type = SimpleType "Foo")
    auto arr = std::make_unique<ArraySpecifier>(2);
    ComposedType ct;
    ct.Attributes().Add(sec.get());
    ct.BaseType(baseType.get());
    ct.ArraySpecifiers().Add(arr.get());
    RecordingVisitor v;
    ct.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "composed",
        "sec:\"\"", "attr", "simple:Foo", "id:Foo",  // the Attributes collection (section + attribute + its type)
        "simple:int", "id:int",                        // the BaseType single slot
        "arrayspec:2"}));                              // the ArraySpecifiers collection
}

// `AcceptVisitor` is virtual through an `AstNode*` (the dynamic dispatch the output visitor
// relies on).
TEST(CSharp_ComposedType, AcceptVisitorIsVirtualThroughAstNode) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    AstNode* asAst = &ct;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"composed", "simple:int", "id:int"}));
}

// `AcceptVisitor` is virtual through an `AstType*` too (the covariant base).
TEST(CSharp_ComposedType, AcceptVisitorIsVirtualThroughAstType) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    AstType* asAstType = &ct;
    RecordingVisitor v;
    asAstType->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"composed", "simple:int", "id:int"}));
}

// ---- DoMatch (the generated seven-term match) --

// Two `ComposedType`s with the same `BaseType`, `ArraySpecifiers`, and default scalars match
// (the empty `Attributes` collections match as empty==empty).
TEST(CSharp_ComposedType, DoMatchMatchesSameAll) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    auto aa = std::make_unique<ArraySpecifier>(2);
    auto bb = std::make_unique<ArraySpecifier>(2);
    a.ArraySpecifiers().Add(aa.get());
    b.ArraySpecifiers().Add(bb.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `BaseType` (the non-nullable recursive `MatchRequired` term) rejects.
TEST(CSharp_ComposedType, DoMatchRejectsDifferentBaseType) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("Foo"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different `ArraySpecifiers` COUNTS reject (the collection match fails when the counts differ).
TEST(CSharp_ComposedType, DoMatchRejectsDifferentArraySpecifiersCount) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    auto aa = std::make_unique<ArraySpecifier>(2);
    a.ArraySpecifiers().Add(aa.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Different `ArraySpecifiers` VALUES reject (the `ArraySpecifier` `DoMatch` compares `Dimensions`).
TEST(CSharp_ComposedType, DoMatchRejectsDifferentArraySpecifiersValue) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    auto aa = std::make_unique<ArraySpecifier>(2);
    auto bb = std::make_unique<ArraySpecifier>(1);
    a.ArraySpecifiers().Add(aa.get());
    b.ArraySpecifiers().Add(bb.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different `Attributes` COUNTS reject (the collection match fails when the counts differ).
TEST(CSharp_ComposedType, DoMatchRejectsDifferentAttributesCount) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    auto sec = MakeFooAttributeSection();
    a.Attributes().Add(sec.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 attribute section, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A different `HasRefSpecifier` (a plain-equality term) rejects.
TEST(CSharp_ComposedType, DoMatchRejectsDifferentHasRefSpecifier) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    a.HasRefSpecifier(true);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A different `PointerRank` (the int plain-equality term) rejects.
TEST(CSharp_ComposedType, DoMatchRejectsDifferentPointerRank) {
    auto ta = std::make_unique<SimpleType>(std::string("int"));
    auto tb = std::make_unique<SimpleType>(std::string("int"));
    ComposedType a;
    ComposedType b;
    a.BaseType(ta.get());
    b.BaseType(tb.get());
    a.PointerRank(1);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A `ComposedType` does not match a different concrete type (the `other is ComposedType` gate).
TEST(CSharp_ComposedType, DoMatchRejectsDifferentType) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    SimpleType st(std::string("int"));
    EXPECT_FALSE(DoMatchAgainst(&ct, &st));
    EXPECT_FALSE(DoMatchAgainst(&st, &ct));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_ComposedType, DoMatchRejectsNullCandidate) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    EXPECT_FALSE(DoMatchAgainst(&ct, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `BaseType`, the two collections, and the four scalars, re-parents the
// clones, and detaches from the source.
TEST(CSharp_ComposedType, CloneDeepCopiesEverything) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"), TextLocation(1, 1));
    auto sec = MakeFooAttributeSection();
    auto arr = std::make_unique<ArraySpecifier>(2);
    ComposedType original;
    original.BaseType(baseType.get());
    original.Attributes().Add(sec.get());
    original.ArraySpecifiers().Add(arr.get());
    original.HasRefSpecifier(true);
    original.HasReadOnlySpecifier(true);
    original.HasNullableSpecifier(true);
    original.PointerRank(3);

    std::unique_ptr<ComposedType> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The scalars are copied.
    EXPECT_TRUE(copy->HasRefSpecifier());
    EXPECT_TRUE(copy->HasReadOnlySpecifier());
    EXPECT_TRUE(copy->HasNullableSpecifier());
    EXPECT_EQ(copy->PointerRank(), 3);
    // The base type is a fresh, re-parented clone.
    ASSERT_NE(copy->BaseType(), nullptr);
    EXPECT_NE(copy->BaseType(), baseType.get());
    EXPECT_EQ(copy->BaseType()->Parent(), copy.get());
    // The attributes collection is a fresh, re-parented clone.
    EXPECT_EQ(copy->Attributes().Count(), 1);
    ASSERT_NE(copy->Attributes().At(0), nullptr);
    EXPECT_NE(copy->Attributes().At(0), sec.get());
    EXPECT_EQ(copy->Attributes().At(0)->Parent(), copy.get());
    // The array specifiers collection is a fresh, re-parented clone.
    EXPECT_EQ(copy->ArraySpecifiers().Count(), 1);
    ASSERT_NE(copy->ArraySpecifiers().At(0), nullptr);
    EXPECT_NE(copy->ArraySpecifiers().At(0), arr.get());
    EXPECT_EQ(copy->ArraySpecifiers().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.Attributes().Count(), 1);
    EXPECT_EQ(original.ArraySpecifiers().Count(), 1);
    EXPECT_EQ(sec->Parent(), &original);
    EXPECT_EQ(arr->Parent(), &original);
    EXPECT_TRUE(original.HasRefSpecifier());
    EXPECT_EQ(original.PointerRank(), 3);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and through an `AstType*` (the covariant return).
TEST(CSharp_ComposedType, CloneIsVirtualThroughAstNodeAndAstType) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType original;
    original.BaseType(baseType.get());
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<ComposedType*>(astCopy.get()), nullptr);
    AstType* asAstType = &original;
    std::unique_ptr<AstType> astTypeCopy(asAstType->Clone());
    ASSERT_NE(astTypeCopy, nullptr);
    EXPECT_NE(dynamic_cast<ComposedType*>(astTypeCopy.get()), nullptr);
}

// ---- CheckInvariant (the PointerRank scalar invariant) ----------------

// A `ComposedType` with the required `BaseType` slot filled passes the inherited `CheckInvariant`
// (the slot-structure verifier) plus the `PointerRank >= 0` scalar assertion. Runs in debug
// builds (a no-op in NDEBUG).
TEST(CSharp_ComposedType, CheckInvariantPassesOnFilledNode) {
    auto baseType = std::make_unique<SimpleType>(std::string("int"), TextLocation(1, 1));
    ComposedType ct;
    ct.BaseType(baseType.get());
    ct.PointerRank(2);
    ct.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The three per-node slot statics are distinct (compared by address). They have different
// `CSharpSlotInfoT<T>` element types, so they are compared through the common `CSharpSlotInfo`
// base (the pointer-identity comparison the slot system uses).
TEST(CSharp_ComposedType, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ComposedType::AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ComposedType::BaseTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ComposedType::AttributesSlot),
              static_cast<const CSharpSlotInfo*>(&ComposedType::ArraySpecifiersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&ComposedType::BaseTypeSlot),
              static_cast<const CSharpSlotInfo*>(&ComposedType::ArraySpecifiersSlot));
    auto baseType = std::make_unique<SimpleType>(std::string("int"));
    ComposedType ct;
    ct.BaseType(baseType.get());
    EXPECT_EQ(ct.BaseType()->Slot(), &ComposedType::BaseTypeSlot);
    EXPECT_EQ(ct.BaseType()->Slot()->Kind(), &Slots::Type);
}
