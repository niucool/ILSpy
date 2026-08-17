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

// Tests for the `AttributeSection` concrete node (cpp/.../Syntax/AttributeSection.hpp, the port
// of ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/AttributeSection.cs) -- the second
// `GeneralScope`-sub-namespace concrete node, a sealed `AstNode` (not an `AstType`, not an
// `Expression`) with an optional `Identifier` `AttributeTargetToken` slot and an `Attribute`
// `Attributes` collection. Exercises the optional `AttributeTargetToken` slot, the hand-written
// `AttributeTarget` string convenience accessor, the `Attributes` collection, the
// collection-aware slot-storage contract, the `AcceptVisitor` dispatch, the generated `DoMatch`
// (a `MatchString` on `AttributeTarget` plus a collection `DoMatch` on `Attributes`), the
// per-concrete-node `Clone`, and the inherited `CheckInvariant`.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
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

// A recording depth-first visitor: records the per-node `Visit` calls with a tag distinguishing
// the concrete types, recursing via the inherited `VisitChildren` (the document-order walk).
// `VisitAttributeSection`/`VisitAttribute`/`VisitSimpleType`/`VisitIdentifier`/
// `VisitNullReferenceExpression`/`VisitThisReferenceExpression` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

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

// Build an `Attribute` carrying a `SimpleType` `Foo` (no arguments) -- a minimal attribute for
// the `Attributes` collection. Returns a holder that keeps BOTH the `SimpleType` `Type` and the
// `Attribute` alive: the `Attribute`'s `Type` slot is a non-owning raw pointer (the parent does not
// take ownership of the child in the port's test model -- the test's `unique_ptr`s own the nodes),
// so the `SimpleType` must outlive the `Attribute`. `get()`/`operator->` expose the `Attribute*`.
struct FooAttr {
    std::unique_ptr<SimpleType> type;
    std::unique_ptr<Attribute> attr;
    Attribute* get() const { return attr.get(); }
    Attribute* operator->() const { return attr.get(); }
};
FooAttr MakeFooAttribute() {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    auto attr = std::make_unique<Attribute>(type.get());
    return FooAttr{std::move(type), std::move(attr)};
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `AttributeSection` is an `AstNode`; it is NOT an `Expression` and NOT an `AstType` (it
// derives directly from `AstNode` -- a structural container, not a type reference or an
// expression).
TEST(CSharp_AttributeSection, IsAstNodeNotExpressionOrAstType) {
    AttributeSection sec;
    EXPECT_NE(dynamic_cast<AstNode*>(&sec), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&sec), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&sec), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no target token (empty `AttributeTarget`) and no attributes. The optional
// `AttributeTargetToken` slot still occupies a flattened index even when empty, so
// `GetChildCount` is 1 (the collection adds 0).
TEST(CSharp_AttributeSection, EmptyCtorHasNoTargetOrAttributes) {
    AttributeSection sec;
    EXPECT_EQ(sec.AttributeTargetToken(), nullptr);
    EXPECT_EQ(sec.AttributeTarget(), "");
    EXPECT_EQ(sec.Attributes().Count(), 0);
    EXPECT_EQ(sec.GetChildCount(), 1);  // AttributeTargetToken slot (empty) + 0 attributes
}

// The hand-written `(Attribute)` ctor adds the attribute to the `Attributes` collection (parented,
// flattened `ChildIndex` 1 -- baseIndex 1 after the `AttributeTargetToken` single slot).
TEST(CSharp_AttributeSection, CtorWithAttrAddsToCollection) {
    auto attr = MakeFooAttribute();
    Attribute* attrPtr = attr.get();
    AttributeSection sec(attrPtr);
    EXPECT_EQ(sec.Attributes().Count(), 1);
    EXPECT_EQ(sec.Attributes().At(0), attrPtr);
    EXPECT_EQ(attrPtr->Parent(), &sec);
    EXPECT_EQ(attrPtr->ChildIndex, 1);  // baseIndex 1 + 0
    EXPECT_EQ(sec.AttributeTargetToken(), nullptr);  // no target
    EXPECT_EQ(sec.AttributeTarget(), "");
}

// ---- The `AttributeTargetToken` slot (an optional `Identifier` child) -------

// The `AttributeTargetToken` setter re-parents the new token and detaches the old one.
TEST(CSharp_AttributeSection, AttributeTargetTokenSetterReparentsAndDetaches) {
    AttributeSection sec;
    std::unique_ptr<Identifier> a(Identifier::Create("return"));
    std::unique_ptr<Identifier> b(Identifier::Create("assembly"));
    sec.AttributeTargetToken(a.get());
    EXPECT_EQ(sec.AttributeTargetToken(), a.get());
    EXPECT_EQ(a->Parent(), &sec);
    EXPECT_EQ(a->ChildIndex, 0);
    sec.AttributeTargetToken(b.get());
    EXPECT_EQ(sec.AttributeTargetToken(), b.get());
    EXPECT_EQ(b->Parent(), &sec);
    EXPECT_EQ(b->ChildIndex, 0);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `AttributeTargetToken` clears the slot and detaches the old token (the slot is
// optional, so a null is a valid empty).
TEST(CSharp_AttributeSection, AttributeTargetTokenClearsOnNull) {
    AttributeSection sec;
    std::unique_ptr<Identifier> a(Identifier::Create("return"));
    sec.AttributeTargetToken(a.get());
    ASSERT_EQ(sec.AttributeTargetToken(), a.get());
    sec.AttributeTargetToken(nullptr);
    EXPECT_EQ(sec.AttributeTargetToken(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `AttributeTarget` string convenience accessor -----------------------

// The `AttributeTarget` string accessor round-trips through the backing token: a non-empty
// value creates a token carrying that `Name`; the getter returns the token's `Name`.
TEST(CSharp_AttributeSection, AttributeTargetRoundTripsViaToken) {
    AttributeSection sec;
    sec.AttributeTarget("return");
    EXPECT_EQ(sec.AttributeTarget(), "return");
    ASSERT_NE(sec.AttributeTargetToken(), nullptr);
    EXPECT_EQ(sec.AttributeTargetToken()->Name(), "return");
    EXPECT_EQ(sec.AttributeTargetToken()->Parent(), &sec);
    EXPECT_EQ(sec.AttributeTargetToken()->ChildIndex, 0);
}

// Setting an empty `AttributeTarget` creates a token with an empty `Name` (the C# uses
// `Identifier.Create`, NOT `CreateIfNotEmpty` -- an empty target is a real token, not a null
// token).
TEST(CSharp_AttributeSection, AttributeTargetEmptyCreatesEmptyNameToken) {
    AttributeSection sec;
    sec.AttributeTarget("");
    ASSERT_NE(sec.AttributeTargetToken(), nullptr);
    EXPECT_EQ(sec.AttributeTargetToken()->Name(), "");
    EXPECT_EQ(sec.AttributeTarget(), "");
}

// Setting a different `AttributeTarget` replaces the token (detaches the old one).
TEST(CSharp_AttributeSection, AttributeTargetReplacesToken) {
    AttributeSection sec;
    sec.AttributeTarget("return");
    auto* first = sec.AttributeTargetToken();
    sec.AttributeTarget("assembly");
    EXPECT_EQ(sec.AttributeTarget(), "assembly");
    EXPECT_NE(sec.AttributeTargetToken(), first);
    EXPECT_EQ(first->Parent(), nullptr);  // the old token is detached
}

// ---- The `Attributes` collection --------------------------------------

// The collection starts empty (0 count); the node's child count is the one single slot (1) + 0.
TEST(CSharp_AttributeSection, AttributesEmptyByDefault) {
    AttributeSection sec;
    EXPECT_EQ(sec.Attributes().Count(), 0);
    EXPECT_EQ(sec.GetChildCount(), 1);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last slot, so an element's index is
// `1 + its local position` -- baseIndex 1, after the `AttributeTargetToken` single slot).
TEST(CSharp_AttributeSection, AttributesAddAppendsAndParentsIncremental) {
    AttributeSection sec;
    auto a0 = MakeFooAttribute();
    auto a1 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    sec.Attributes().Add(a1.get());
    EXPECT_EQ(sec.Attributes().Count(), 2);
    EXPECT_EQ(a0->Parent(), &sec);
    EXPECT_EQ(a1->Parent(), &sec);
    EXPECT_EQ(a0->ChildIndex, 1);  // baseIndex 1 + 0
    EXPECT_EQ(a1->ChildIndex, 2);  // baseIndex 1 + 1
    EXPECT_EQ(sec.GetChildCount(), 3);  // token + 2 attributes
    EXPECT_TRUE(sec.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Attributes` collection for the `Attribute` kind and null for
// any other kind (the `Identifier` kind is a single slot, not a collection; the `Type`/`Argument`
// kinds are not declared by `AttributeSection`).
TEST(CSharp_AttributeSection, GetCollectionByKindReturnsAttributesForAttributeKind) {
    AttributeSection sec;
    EXPECT_NE(sec.GetCollectionByKind(&Slots::Attribute), nullptr);
    EXPECT_EQ(sec.GetCollectionByKind(&Slots::Attribute), &sec.Attributes());
    EXPECT_EQ(sec.GetCollectionByKind(&Slots::Identifier), nullptr);  // a single slot
    EXPECT_EQ(sec.GetCollectionByKind(&Slots::Type), nullptr);  // not declared
    EXPECT_EQ(sec.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the token at index 0 and the attributes at index 1+; the collection occupies
// the contiguous range [1, 1 + Count).
TEST(CSharp_AttributeSection, GetChildDispatchesTokenAndAttributes) {
    AttributeSection sec;
    std::unique_ptr<Identifier> tok(Identifier::Create("return"));
    sec.AttributeTargetToken(tok.get());
    auto a0 = MakeFooAttribute();
    auto a1 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    sec.Attributes().Add(a1.get());
    EXPECT_EQ(sec.GetChild(0), tok.get());
    EXPECT_EQ(sec.GetChild(1), a0.get());
    EXPECT_EQ(sec.GetChild(2), a1.get());
    EXPECT_THROW(sec.GetChild(3), std::out_of_range);
    EXPECT_THROW(sec.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `AttributeTargetTokenSlot` at index 0 and the `AttributesSlot`
// at index 1+ (the slot identity the slot system compares by address).
TEST(CSharp_AttributeSection, GetChildSlotInfoDispatchesTokenAndAttributes) {
    AttributeSection sec;
    auto a0 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    EXPECT_EQ(sec.GetChildSlotInfo(0), &AttributeSection::AttributeTargetTokenSlot);
    EXPECT_EQ(sec.GetChildSlotInfo(1), &AttributeSection::AttributesSlot);
    EXPECT_THROW(sec.GetChildSlotInfo(2), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Identifier` KIND (the shared `Slots.Identifier`).
TEST(CSharp_AttributeSection, AttributeTargetTokenSlotPointsAtIdentifierKind) {
    AttributeSection sec;
    EXPECT_EQ(sec.GetChildSlotInfo(0)->Kind(), &Slots::Identifier);
}

// `GetChildSlotInfo(1)` points at the `Attribute` KIND (the shared `Slots.Attribute`).
TEST(CSharp_AttributeSection, AttributesSlotPointsAtAttributeKind) {
    AttributeSection sec;
    auto a0 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    EXPECT_EQ(sec.GetChildSlotInfo(1)->Kind(), &Slots::Attribute);
}

// `SetChild` writes the token at index 0 and replaces an attribute in place at index 1+ (the
// collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_AttributeSection, SetChildDispatchesTokenAndAttributes) {
    AttributeSection sec;
    // Replace the token at index 0.
    std::unique_ptr<Identifier> tok(Identifier::Create("return"));
    sec.SetChild(0, tok.get());
    EXPECT_EQ(sec.AttributeTargetToken(), tok.get());
    EXPECT_EQ(tok->Parent(), &sec);
    EXPECT_EQ(tok->ChildIndex, 0);
    // Replace the attribute at index 1.
    auto a0 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    auto a1 = MakeFooAttribute();
    sec.SetChild(1, a1.get());
    EXPECT_EQ(sec.Attributes().At(0), a1.get());
    EXPECT_EQ(a1->Parent(), &sec);
    EXPECT_THROW(sec.SetChild(2, nullptr), std::out_of_range);  // no element at index 2
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitAttributeSection` (the visitor-pattern round-trip); the
// depth-first walk then visits the `AttributeTargetToken` child (a `VisitIdentifier`) and the
// `Attributes` (a `VisitAttribute` per element, each recursing into the attribute's `Type`
// `SimpleType` + its `Identifier` token) in document order.
TEST(CSharp_AttributeSection, AcceptVisitorDispatchesToVisitAttributeSection) {
    AttributeSection sec;
    sec.AttributeTarget("return");
    auto a0 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    RecordingVisitor v;
    sec.AcceptVisitor(v);
    // sec (target "return") -> id:return (the target token) -> attr -> simple:Foo -> id:Foo
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "sec:\"return\"", "id:return", "attr", "simple:Foo", "id:Foo"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` (the dynamic dispatch the output visitor
// relies on).
TEST(CSharp_AttributeSection, AcceptVisitorIsVirtualThroughAstNode) {
    AttributeSection sec;
    sec.AttributeTarget("return");
    AstNode* asAst = &sec;
    RecordingVisitor v;
    asAst->AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"sec:\"return\"", "id:return"}));
}

// ---- DoMatch (the generated MatchString + collection match) --

// Two `AttributeSection`s with the same `AttributeTarget` and the same `Attributes` match.
TEST(CSharp_AttributeSection, DoMatchMatchesSameAll) {
    AttributeSection a;
    AttributeSection b;
    a.AttributeTarget("return");
    b.AttributeTarget("return");
    auto a0 = MakeFooAttribute();
    auto b0 = MakeFooAttribute();
    a.Attributes().Add(a0.get());
    b.Attributes().Add(b0.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `AttributeTarget` (the `MatchString` term) rejects.
TEST(CSharp_AttributeSection, DoMatchRejectsDifferentAttributeTarget) {
    AttributeSection a;
    AttributeSection b;
    a.AttributeTarget("return");
    b.AttributeTarget("assembly");
    auto a0 = MakeFooAttribute();
    auto b0 = MakeFooAttribute();
    a.Attributes().Add(a0.get());
    b.Attributes().Add(b0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A pattern with the `$any$` wildcard `AttributeTarget` matches any candidate's target (the
// `MatchString` short-circuit when `pattern == AnyString`).
TEST(CSharp_AttributeSection, DoMatchAttributeTargetAnyStringWildcard) {
    AttributeSection pattern;
    AttributeSection candidate;
    pattern.AttributeTarget("$any$");
    candidate.AttributeTarget("assembly");
    auto p0 = MakeFooAttribute();
    auto c0 = MakeFooAttribute();
    pattern.Attributes().Add(p0.get());
    candidate.Attributes().Add(c0.get());
    EXPECT_TRUE(DoMatchAgainst(&pattern, &candidate));
    // Asymmetric: the wildcard is on the pattern side, not the candidate side.
    EXPECT_FALSE(DoMatchAgainst(&candidate, &pattern));
}

// Different `Attributes` COUNTS reject (the collection match fails when the counts differ).
TEST(CSharp_AttributeSection, DoMatchRejectsDifferentAttributesCount) {
    AttributeSection a;
    AttributeSection b;
    a.AttributeTarget("return");
    b.AttributeTarget("return");
    auto a0 = MakeFooAttribute();
    a.Attributes().Add(a0.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 attr, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Different `Attributes` VALUES reject (the element `DoMatch` -- a recursive match on the
// `Attribute` whose `Type` is a `SimpleType`; a `SimpleType` `Foo` vs `Bar` rejects the
// `MatchString` on `Identifier`).
TEST(CSharp_AttributeSection, DoMatchRejectsDifferentAttributesValue) {
    AttributeSection a;
    AttributeSection b;
    a.AttributeTarget("return");
    b.AttributeTarget("return");
    auto ta = std::make_unique<SimpleType>(std::string("Foo"));
    auto tb = std::make_unique<SimpleType>(std::string("Bar"));
    Attribute aAttr(ta.get());
    Attribute bAttr(tb.get());
    a.Attributes().Add(&aAttr);
    b.Attributes().Add(&bAttr);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// An `AttributeSection` does not match a different concrete type (the `other is AttributeSection`
// gate); an `Attribute` is not an `AttributeSection`.
TEST(CSharp_AttributeSection, DoMatchRejectsDifferentType) {
    auto type = std::make_unique<SimpleType>(std::string("Foo"));
    Attribute attr(type.get());
    AttributeSection sec;
    EXPECT_FALSE(DoMatchAgainst(&sec, &attr));
    EXPECT_FALSE(DoMatchAgainst(&attr, &sec));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_AttributeSection, DoMatchRejectsNullCandidate) {
    AttributeSection sec;
    EXPECT_FALSE(DoMatchAgainst(&sec, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `AttributeTargetToken` and the `Attributes`, re-parents the clones,
// and detaches from the source.
TEST(CSharp_AttributeSection, CloneDeepCopiesTokenAndAttributes) {
    AttributeSection original;
    original.AttributeTarget("return");
    auto a0 = MakeFooAttribute();
    original.Attributes().Add(a0.get());

    std::unique_ptr<AttributeSection> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_EQ(copy->AttributeTarget(), "return");
    ASSERT_NE(copy->AttributeTargetToken(), nullptr);
    EXPECT_NE(copy->AttributeTargetToken(), original.AttributeTargetToken());  // a fresh token
    EXPECT_EQ(copy->AttributeTargetToken()->Name(), "return");
    EXPECT_EQ(copy->AttributeTargetToken()->Parent(), copy.get());  // re-parented to the clone
    EXPECT_EQ(copy->Attributes().Count(), 1);
    ASSERT_NE(copy->Attributes().At(0), nullptr);
    EXPECT_NE(copy->Attributes().At(0), a0.get());  // a fresh attribute
    EXPECT_EQ(copy->Attributes().At(0)->Parent(), copy.get());  // re-parented
    // The original is unchanged.
    EXPECT_EQ(original.Attributes().Count(), 1);
    EXPECT_EQ(a0->Parent(), &original);
    EXPECT_EQ(original.AttributeTarget(), "return");
}

// `Clone` on an empty `AttributeSection` (no token, no attributes) yields an empty clone.
TEST(CSharp_AttributeSection, CloneEmpty) {
    AttributeSection original;
    std::unique_ptr<AttributeSection> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->AttributeTargetToken(), nullptr);
    EXPECT_EQ(copy->AttributeTarget(), "");
    EXPECT_EQ(copy->Attributes().Count(), 0);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override).
TEST(CSharp_AttributeSection, CloneIsVirtualThroughAstNode) {
    AttributeSection original;
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<AttributeSection*>(astCopy.get()), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An empty `AttributeSection` (the optional `AttributeTargetToken` absent and `Attributes` empty)
// passes the inherited `CheckInvariant` -- the token is OPTIONAL, so an absent required-slot
// assertion does NOT fire (unlike `Attribute` whose `Type` is required). Runs in debug builds (a
// no-op in NDEBUG).
TEST(CSharp_AttributeSection, CheckInvariantPassesOnEmpty) {
    AttributeSection sec;
    sec.CheckInvariant();
}

// A filled `AttributeSection` (a target token and attributes) passes the inherited
// `CheckInvariant` (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type
// consistent).
TEST(CSharp_AttributeSection, CheckInvariantPassesOnFilled) {
    AttributeSection sec;
    sec.AttributeTarget("return");
    auto a0 = MakeFooAttribute();
    sec.Attributes().Add(a0.get());
    sec.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `AttributeTargetTokenSlot` and `AttributesSlot` are distinct slot statics (compared by
// address); they have different `CSharpSlotInfoT<T>` element types (`Identifier` vs `Attribute`),
// so they are compared through the common `CSharpSlotInfo` base (the pointer-identity comparison
// the slot system uses).
TEST(CSharp_AttributeSection, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&AttributeSection::AttributeTargetTokenSlot),
              static_cast<const CSharpSlotInfo*>(&AttributeSection::AttributesSlot));
}
