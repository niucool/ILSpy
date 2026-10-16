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

// Tests for the `MemberType` concrete node (cpp/.../Syntax/MemberType.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/MemberType.cs) -- the third concrete `AstType`, the
// second with a collection slot (`TypeArguments`) and a string-name `[Slot]` (`MemberName`),
// and the first to combine a REQUIRED `AstType` child (`Target`) with a collection plus a plain
// bool scalar (`IsDoubleColon`). Exercises the `IsDoubleColon` scalar, the required `Target`
// slot, the non-nullable `MemberName` string-name accessor (over the `MemberNameToken`), the
// `TypeArguments` collection, the collection-aware slot-storage contract, the `AcceptVisitor`
// dispatch, the generated `DoMatch` (a plain-equality on `IsDoubleColon` plus a non-nullable
// recursive `MatchRequired` on `Target` plus a `MatchString` on `MemberName` plus a collection
// `DoMatch` on `TypeArguments`), the per-concrete-node `Clone`, and the inherited
// `CheckInvariant`.

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
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
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
// the inherited `VisitChildren` (the document-order walk). `VisitMemberType`/`VisitSimpleType`/
// `VisitPrimitiveType`/`VisitIdentifier` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitMemberType(MemberType* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("member:" + node->MemberName());
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-st>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or("<anon>"));
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-pt>"); return; }
        trace.push_back("prim:" + node->Keyword());
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

// `MemberType` is an `AstType` and an `AstNode`; it is NOT an `Expression` (it derives from
// `AstType`, parallel to -- not under -- `Expression`).
TEST(CSharp_MemberType, IsAstTypeAndAstNodeNotExpression) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_NE(dynamic_cast<AstType*>(&mt), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&mt), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&mt), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no target, no name, no type arguments, and `IsDoubleColon` defaults to
// false. The two required single slots (Target + MemberNameToken) still each occupy a flattened
// index even when empty, so `GetChildCount` is 2 (the collection adds 0).
TEST(CSharp_MemberType, EmptyCtorHasNoTargetNameOrTypeArguments) {
    MemberType mt;
    EXPECT_FALSE(mt.IsDoubleColon());
    EXPECT_EQ(mt.Target(), nullptr);
    EXPECT_EQ(mt.MemberNameToken(), nullptr);
    EXPECT_EQ(mt.TypeArguments().Count(), 0);
    EXPECT_EQ(mt.GetChildCount(), 2);  // Target slot + MemberNameToken slot, both empty, + 0
}

// The `(AstType, string)` required-prefix ctor sets the `Target` (parented, index 0) and the
// `MemberName` (the token created via `Identifier.Create`, parented, index 1); the name reads
// back through the string accessor.
TEST(CSharp_MemberType, CtorWithTargetAndNameSetsBoth) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_EQ(mt.Target(), target.get());
    EXPECT_EQ(target->Parent(), &mt);
    EXPECT_EQ(target->ChildIndex, 0);
    EXPECT_EQ(mt.MemberName(), "Enumerator");
    ASSERT_NE(mt.MemberNameToken(), nullptr);
    EXPECT_EQ(mt.MemberNameToken()->Name(), "Enumerator");
    EXPECT_EQ(mt.MemberNameToken()->Parent(), &mt);
    EXPECT_EQ(mt.MemberNameToken()->ChildIndex, 1);
    EXPECT_FALSE(mt.IsDoubleColon());
}

// ---- The `IsDoubleColon` bool scalar ----------------------------------

// The `IsDoubleColon` getter/setter round-trip; it is plain instance state (not a child slot,
// not a ctor param).
TEST(CSharp_MemberType, IsDoubleColonRoundTrip) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_FALSE(mt.IsDoubleColon());
    mt.IsDoubleColon(true);
    EXPECT_TRUE(mt.IsDoubleColon());
    mt.IsDoubleColon(false);
    EXPECT_FALSE(mt.IsDoubleColon());
}

// ---- The `Target` slot (a required `AstType` child) -------------------

// The `Target` setter re-parents the new target and detaches the old one.
TEST(CSharp_MemberType, TargetSetterReparentsAndDetaches) {
    MemberType mt;
    auto a = std::make_unique<SimpleType>(std::string("List"));
    auto b = std::make_unique<SimpleType>(std::string("Dict"));
    mt.Target(a.get());
    EXPECT_EQ(mt.Target(), a.get());
    EXPECT_EQ(a->Parent(), &mt);
    EXPECT_EQ(a->ChildIndex, 0);
    mt.Target(b.get());
    EXPECT_EQ(mt.Target(), b.get());
    EXPECT_EQ(b->Parent(), &mt);
    EXPECT_EQ(b->ChildIndex, 0);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// A null `Target` clears the slot and detaches the old target.
TEST(CSharp_MemberType, TargetSetterClearsOnNull) {
    MemberType mt;
    auto a = std::make_unique<SimpleType>(std::string("List"));
    mt.Target(a.get());
    ASSERT_EQ(mt.Target(), a.get());
    mt.Target(nullptr);
    EXPECT_EQ(mt.Target(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `MemberName` string accessor (non-nullable) ----------------

// The string accessor reads the token's name (a non-nullable name -- `get => MemberNameToken.Name`).
TEST(CSharp_MemberType, MemberNameReadsTokenName) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_EQ(mt.MemberName(), "Enumerator");
}

// The string setter creates the token via `Identifier.Create` (NOT `CreateIfNotEmpty` -- a
// non-nullable name creates a token even for an empty string, so an empty name yields a token
// with an empty `Name`, not a null token). This is the discriminator vs `SimpleType.Identifier`
// (a nullable `string?` whose setter clears the token on empty via `CreateIfNotEmpty`).
TEST(CSharp_MemberType, MemberNameSetterCreatesTokenForNonEmpty) {
    MemberType mt;
    mt.MemberName("Foo");
    EXPECT_EQ(mt.MemberName(), "Foo");
    ASSERT_NE(mt.MemberNameToken(), nullptr);
    EXPECT_EQ(mt.MemberNameToken()->Name(), "Foo");
    EXPECT_EQ(mt.MemberNameToken()->Parent(), &mt);
    EXPECT_EQ(mt.MemberNameToken()->ChildIndex, 1);
}

// An empty `MemberName` assignment creates an empty-name token (NOT a null token) -- the
// non-nullable-name behaviour.
TEST(CSharp_MemberType, MemberNameSetterCreatesTokenForEmpty) {
    MemberType mt;
    mt.MemberName("");
    ASSERT_NE(mt.MemberNameToken(), nullptr);  // a token with an empty name, not null
    EXPECT_EQ(mt.MemberNameToken()->Name(), "");
}

// ---- The `MemberNameToken` slot --------------------------------------

// The token setter re-parents the new token and detaches the old one.
TEST(CSharp_MemberType, MemberNameTokenSetterReparentsAndDetaches) {
    MemberType mt;
    auto a = std::make_unique<Identifier>();
    a->Name("a");
    auto b = std::make_unique<Identifier>();
    b->Name("b");
    mt.MemberNameToken(a.get());
    EXPECT_EQ(mt.MemberNameToken(), a.get());
    EXPECT_EQ(a->Parent(), &mt);
    EXPECT_EQ(a->ChildIndex, 1);
    mt.MemberNameToken(b.get());
    EXPECT_EQ(mt.MemberNameToken(), b.get());
    EXPECT_EQ(b->Parent(), &mt);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(mt.MemberName(), "b");
}

// ---- The `TypeArguments` collection ----------------------------------

// The collection starts empty (0 count); the node's child count is the two single slots (2) + 0.
TEST(CSharp_MemberType, TypeArgumentsEmptyByDefault) {
    MemberType mt;
    EXPECT_EQ(mt.TypeArguments().Count(), 0);
    EXPECT_EQ(mt.GetChildCount(), 2);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last slot, so an element's index is
// `2 + its local position` -- baseIndex 2, after the Target and MemberNameToken single slots).
TEST(CSharp_MemberType, TypeArgumentsAddAppendsAndParentsIncremental) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 11));
    mt.TypeArguments().Add(i.get());
    mt.TypeArguments().Add(s.get());
    EXPECT_EQ(mt.TypeArguments().Count(), 2);
    EXPECT_EQ(i->Parent(), &mt);
    EXPECT_EQ(s->Parent(), &mt);
    EXPECT_EQ(i->ChildIndex, 2);  // baseIndex 2 + 0
    EXPECT_EQ(s->ChildIndex, 3);  // baseIndex 2 + 1
    EXPECT_EQ(mt.GetChildCount(), 4);  // target + token + 2 type args
    EXPECT_TRUE(mt.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `TypeArguments` collection for the `TypeArgument` kind and
// null for any other kind (the `Target`/`Identifier` kinds are single slots, not collections).
TEST(CSharp_MemberType, GetCollectionByKindReturnsTypeArgumentsForTypeArgumentKind) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_NE(mt.GetCollectionByKind(&Slots::TypeArgument), nullptr);
    EXPECT_EQ(mt.GetCollectionByKind(&Slots::TypeArgument), &mt.TypeArguments());
    EXPECT_EQ(mt.GetCollectionByKind(&Slots::Target), nullptr);  // a single slot, not a collection
    EXPECT_EQ(mt.GetCollectionByKind(&Slots::Identifier), nullptr);  // a single slot, not a collection
    EXPECT_EQ(mt.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the target at index 0, the token at index 1, and the type arguments at
// index 2+; the collection occupies the contiguous range [2, 2 + Count).
TEST(CSharp_MemberType, GetChildDispatchesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    mt.TypeArguments().Add(i.get());
    mt.TypeArguments().Add(s.get());
    EXPECT_EQ(mt.GetChild(0), target.get());
    EXPECT_EQ(mt.GetChild(1), mt.MemberNameToken());
    EXPECT_EQ(mt.GetChild(2), i.get());
    EXPECT_EQ(mt.GetChild(3), s.get());
    EXPECT_THROW(mt.GetChild(4), std::out_of_range);
    EXPECT_THROW(mt.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TargetSlot` at index 0, the `MemberNameTokenSlot` at index 1,
// and the `TypeArgumentsSlot` at index 2+ (the slot identity the slot system compares by
// address).
TEST(CSharp_MemberType, GetChildSlotInfoDispatchesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    mt.TypeArguments().Add(i.get());
    EXPECT_EQ(mt.GetChildSlotInfo(0), &MemberType::TargetSlot);
    EXPECT_EQ(mt.GetChildSlotInfo(1), &MemberType::MemberNameTokenSlot);
    EXPECT_EQ(mt.GetChildSlotInfo(2), &MemberType::TypeArgumentsSlot);
    EXPECT_THROW(mt.GetChildSlotInfo(3), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Target` KIND (the shared `Slots.Target`).
TEST(CSharp_MemberType, TargetSlotPointsAtTargetKind) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_EQ(mt.GetChildSlotInfo(0)->Kind(), &Slots::Target);
}

// `GetChildSlotInfo(1)` points at the `Identifier` KIND (the shared `Slots.Identifier` -- the
// backing-token kind shared by all string-name `[Slot]`s).
TEST(CSharp_MemberType, MemberNameTokenSlotPointsAtIdentifierKind) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_EQ(mt.GetChildSlotInfo(1)->Kind(), &Slots::Identifier);
}

// `GetChildSlotInfo(2)` points at the `TypeArgument` KIND (the shared `Slots.TypeArgument`).
TEST(CSharp_MemberType, TypeArgumentsSlotPointsAtTypeArgumentKind) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    mt.TypeArguments().Add(i.get());
    EXPECT_EQ(mt.GetChildSlotInfo(2)->Kind(), &Slots::TypeArgument);
}

// `SetChild` writes the target at index 0, the token at index 1, and replaces a type argument
// in place at index 2+ (the collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_MemberType, SetChildDispatchesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    mt.TypeArguments().Add(i.get());
    // Replace the target at index 0.
    auto newTarget = std::make_unique<SimpleType>(std::string("Dict"));
    mt.SetChild(0, newTarget.get());
    EXPECT_EQ(mt.Target(), newTarget.get());
    EXPECT_EQ(newTarget->Parent(), &mt);
    EXPECT_EQ(newTarget->ChildIndex, 0);
    // Replace the token at index 1.
    auto id = Identifier::Create("Item");
    mt.SetChild(1, id);
    EXPECT_EQ(mt.MemberNameToken(), id);
    EXPECT_EQ(id->Parent(), &mt);
    EXPECT_EQ(id->ChildIndex, 1);
    // Replace the type argument at index 2.
    mt.SetChild(2, s.get());
    EXPECT_EQ(mt.TypeArguments().At(0), s.get());
    EXPECT_EQ(s->Parent(), &mt);
    EXPECT_THROW(mt.SetChild(3, nullptr), std::out_of_range);  // no element at index 3
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitMemberType` (the visitor-pattern round-trip); the
// depth-first walk then visits the `Target` child (a `VisitSimpleType`), the `MemberNameToken`
// (a `VisitIdentifier`), and the `TypeArguments` (a `VisitPrimitiveType` per element) in
// document order.
TEST(CSharp_MemberType, AcceptVisitorDispatchesToVisitMemberType) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    mt.TypeArguments().Add(i.get());
    RecordingVisitor v;
    mt.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "member:Enumerator", "simple:List", "id:List", "id:Enumerator", "prim:int"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `AstType*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_MemberType, AcceptVisitorIsVirtualThroughBases) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    AstNode* asAst = &mt;
    AstType* asType = &mt;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asType->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"member:Enumerator", "simple:List", "id:List", "id:Enumerator"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"member:Enumerator", "simple:List", "id:List", "id:Enumerator"}));
}

// ---- DoMatch (the generated bool + recursive + string + collection match) --

// Two `MemberType`s with the same `IsDoubleColon`, `Target`, `MemberName`, and `TypeArguments`
// match.
TEST(CSharp_MemberType, DoMatchMatchesSameAll) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("List"));
    MemberType a(ta.get(), std::string("Enumerator"));
    MemberType b(tb.get(), std::string("Enumerator"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Different `IsDoubleColon` (the plain-equality term) rejects -- and short-circuits before the
// recursive `Target` term is even evaluated.
TEST(CSharp_MemberType, DoMatchRejectsDifferentIsDoubleColon) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("List"));
    MemberType a(ta.get(), std::string("Enumerator"));
    MemberType b(tb.get(), std::string("Enumerator"));
    a.IsDoubleColon(true);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A different `Target` (the non-nullable recursive term via `MatchRequired`) rejects.
TEST(CSharp_MemberType, DoMatchRejectsDifferentTarget) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("Dict"));
    MemberType a(ta.get(), std::string("Enumerator"));
    MemberType b(tb.get(), std::string("Enumerator"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different `MemberName` (the `MatchString` term) rejects.
TEST(CSharp_MemberType, DoMatchRejectsDifferentName) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("List"));
    MemberType a(ta.get(), std::string("Enumerator"));
    MemberType b(tb.get(), std::string("Item"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Different type-argument COUNTS reject (the collection match fails when the counts differ).
TEST(CSharp_MemberType, DoMatchRejectsDifferentTypeArgumentCount) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("List"));
    MemberType a(ta.get(), std::string("Enumerator"));
    MemberType b(tb.get(), std::string("Enumerator"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Different type-argument VALUES reject (the element `DoMatch` -- a `MatchString` on the
// `PrimitiveType` keyword -- rejects).
TEST(CSharp_MemberType, DoMatchRejectsDifferentTypeArgumentValue) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("List"));
    MemberType a(ta.get(), std::string("Enumerator"));
    MemberType b(tb.get(), std::string("Enumerator"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("string"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A pattern `MemberType` whose `MemberName` is `$any$` (the `Pattern::AnyString` wildcard)
// matches any candidate `MemberType` regardless of its name (the `MatchString` wildcard path).
TEST(CSharp_MemberType, DoMatchAnyStringWildcardMatchesAnyName) {
    auto ta = std::make_unique<SimpleType>(std::string("List"));
    auto tb = std::make_unique<SimpleType>(std::string("List"));
    MemberType pattern(ta.get(), std::string(Pattern::AnyString));
    MemberType cand1(tb.get(), std::string("Enumerator"));
    auto tc = std::make_unique<SimpleType>(std::string("List"));
    MemberType cand2(tc.get(), std::string("Item"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand1));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand2));
}

// A `MemberType` does not match a different concrete type (the `other is MemberType` gate); a
// `SimpleType` is not a `MemberType`.
TEST(CSharp_MemberType, DoMatchRejectsDifferentType) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    SimpleType st(std::string("Enumerator"));
    EXPECT_FALSE(DoMatchAgainst(&mt, &st));
    EXPECT_FALSE(DoMatchAgainst(&st, &mt));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_MemberType, DoMatchRejectsNullCandidate) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_FALSE(DoMatchAgainst(&mt, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `Target`, the `MemberName` token, the `TypeArguments`, and the
// `IsDoubleColon` scalar, re-parents the clones, and detaches from the source.
TEST(CSharp_MemberType, CloneDeepCopiesTargetTokenTypeArgumentsAndScalar) {
    auto target = std::make_unique<SimpleType>(std::string("List"), TextLocation(1, 1));
    MemberType original(target.get(), std::string("Enumerator"));
    original.IsDoubleColon(true);
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    original.TypeArguments().Add(i.get());

    std::unique_ptr<MemberType> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_TRUE(copy->IsDoubleColon());  // the scalar is copied
    ASSERT_NE(copy->Target(), nullptr);
    EXPECT_NE(copy->Target(), target.get());  // a fresh target
    EXPECT_EQ(copy->Target()->Parent(), copy.get());  // re-parented to the clone
    EXPECT_EQ(copy->MemberName(), "Enumerator");
    ASSERT_NE(copy->MemberNameToken(), nullptr);
    EXPECT_NE(copy->MemberNameToken(), original.MemberNameToken());  // a fresh token
    EXPECT_EQ(copy->MemberNameToken()->Parent(), copy.get());  // re-parented
    EXPECT_EQ(copy->TypeArguments().Count(), 1);
    ASSERT_NE(copy->TypeArguments().At(0), nullptr);
    EXPECT_NE(copy->TypeArguments().At(0), i.get());  // a fresh type argument
    EXPECT_EQ(copy->TypeArguments().At(0)->Parent(), copy.get());  // re-parented
    // The original is unchanged.
    EXPECT_EQ(original.TypeArguments().Count(), 1);
    EXPECT_EQ(i->Parent(), &original);
    EXPECT_TRUE(original.IsDoubleColon());
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `AstType*` (returns an `AstType*`).
TEST(CSharp_MemberType, CloneIsVirtualAndCovariant) {
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType original(target.get(), std::string("Enumerator"));
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<MemberType*>(astCopy.get()), nullptr);
    AstType* asType = &original;
    std::unique_ptr<AstType> typeCopy(asType->Clone());
    ASSERT_NE(typeCopy, nullptr);
    EXPECT_NE(dynamic_cast<MemberType*>(typeCopy.get()), nullptr);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A `MemberType` with the required `Target` and `MemberName` slots filled and type arguments
// passes the inherited `CheckInvariant` (the slot-structure verifier: every slot's
// `Parent`/`ChildIndex`/type consistent). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_MemberType, CheckInvariantPassesOnFilledNode) {
    auto target = std::make_unique<SimpleType>(std::string("List"), TextLocation(1, 1));
    MemberType mt(target.get(), std::string("Enumerator"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    mt.TypeArguments().Add(i.get());
    mt.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `TargetSlot`, `MemberNameTokenSlot`, and `TypeArgumentsSlot` are distinct slot statics
// (compared by address); the node's `Slot()` reports the per-node slot for the target and the
// token. The slot statics have different `CSharpSlotInfoT<T>` element types (`AstType` vs
// `Identifier`), so they are compared through the common `CSharpSlotInfo` base (the
// pointer-identity comparison the slot system uses).
TEST(CSharp_MemberType, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&MemberType::TargetSlot),
              static_cast<const CSharpSlotInfo*>(&MemberType::MemberNameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&MemberType::TargetSlot),
              static_cast<const CSharpSlotInfo*>(&MemberType::TypeArgumentsSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&MemberType::MemberNameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&MemberType::TypeArgumentsSlot));
    auto target = std::make_unique<SimpleType>(std::string("List"));
    MemberType mt(target.get(), std::string("Enumerator"));
    EXPECT_EQ(mt.Target()->Slot(), &MemberType::TargetSlot);
    EXPECT_EQ(mt.Target()->Slot()->Kind(), &Slots::Target);
    EXPECT_EQ(mt.MemberNameToken()->Slot(), &MemberType::MemberNameTokenSlot);
    EXPECT_EQ(mt.MemberNameToken()->Slot()->Kind(), &Slots::Identifier);
}
