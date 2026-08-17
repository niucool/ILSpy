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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the `PointerReferenceExpression` concrete node (cpp/.../Syntax/Expressions/
// PointerReferenceExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.cs) -- the
// structural twin of `MemberReferenceExpression` (D247): the same `MemberType` (D238) shape (a
// `Target` single `Expression` slot + a `MemberName` string-name `[Slot]` over a backing
// `MemberNameToken` + a `TypeArguments` `AstNodeCollection<AstType>` collection) but for the
// `pointer_member_access ::= expression '->' identifier ( '<' type ( ',' type )* '>' )?`
// production, plus the `ArrowToken` const string `"->"`. Exercises the `ArrowToken` const, the
// `Target`/`MemberNameToken`/`TypeArguments` slots, the collection-aware slot-storage contract,
// the `AcceptVisitor` dispatch, the generated `DoMatch` (a `MatchRequired` on `Target` + a
// `MatchString` on `MemberName` + a collection `DoMatch` on `TypeArguments`), the
// per-concrete-node `Clone`, the inherited `CheckInvariant`, and a cross-structural-twin
// `DoMatch` rejection against `MemberReferenceExpression` (the two share the exact same shape
// but are distinct concrete types so the type-check gate rejects).

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
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
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
// the inherited `VisitChildren` (the document-order walk). `VisitPointerReferenceExpression`/
// `VisitMemberReferenceExpression`/`VisitIdentifier`/`VisitPrimitiveType`/`VisitIdentifierExpression`
// /`VisitNullReferenceExpression` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitPointerReferenceExpression(PointerReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("pref:" + node->MemberName());
        VisitChildren(node);
    }
    void VisitMemberReferenceExpression(MemberReferenceExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-mref>"); return; }
        trace.push_back("mref:" + node->MemberName());
        VisitChildren(node);
    }
    void VisitIdentifierExpression(IdentifierExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-idexpr>"); return; }
        trace.push_back("idexpr:" + node->Identifier());
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-pt>"); return; }
        trace.push_back("prim:" + node->Keyword());
        VisitChildren(node);
    }
    void VisitNullReferenceExpression(NullReferenceExpression* node) override {
        if (node == nullptr) return;
        trace.push_back("null");
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

// `PointerReferenceExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the
// slot system and the annotation channel use); it is NOT an `AstType` (it derives from
// `Expression`, parallel to -- not under -- `AstType`).
TEST(CSharp_PointerReferenceExpression, IsExpressionAndAstNodeNotAstType) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    EXPECT_NE(dynamic_cast<Expression*>(&pre), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&pre), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&pre), nullptr);
}

// ---- The `ArrowToken` const --------------------------------------------

// The `ArrowToken` is the `->` separator the output visitor emits (the C#
// `public const string ArrowToken = "->"`). A static constexpr field, not instance state.
TEST(CSharp_PointerReferenceExpression, ArrowTokenIsArrowSeparator) {
    EXPECT_STREQ(PointerReferenceExpression::ArrowToken, "->");
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no target, no name token, and no type arguments. `GetChildCount` is 2
// (the two empty single slots) + 0 type args.
TEST(CSharp_PointerReferenceExpression, EmptyCtorHasNoTargetNameOrTypeArguments) {
    PointerReferenceExpression pre;
    EXPECT_EQ(pre.Target(), nullptr);
    EXPECT_EQ(pre.MemberNameToken(), nullptr);
    EXPECT_EQ(pre.TypeArguments().Count(), 0);
    EXPECT_EQ(pre.GetChildCount(), 2);  // the two single slots (empty) + 0 type args
    EXPECT_EQ(pre.StartLocation(), TextLocation::Empty);
}

// The `(Expression, string)` ctor (the generated required-prefix ctor) sets the `Target` and
// creates the `MemberNameToken` via `Identifier.Create` (NOT `CreateIfNotEmpty`).
TEST(CSharp_PointerReferenceExpression, CtorSetsTargetAndCreatesNameToken) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    EXPECT_EQ(pre.Target(), target.get());
    EXPECT_EQ(pre.MemberName(), "Member");
    ASSERT_NE(pre.MemberNameToken(), nullptr);
    EXPECT_EQ(pre.MemberNameToken()->Name(), "Member");
    EXPECT_EQ(pre.MemberNameToken()->Parent(), &pre);
    EXPECT_EQ(pre.MemberNameToken()->ChildIndex, 1);
    EXPECT_EQ(target->Parent(), &pre);
    EXPECT_EQ(target->ChildIndex, 0);
}

// An empty member name in the `(Expression, string)` ctor creates a token with an EMPTY `Name`
// (NOT a null token -- the non-nullable `MemberName` uses `Identifier.Create`, so an empty
// name yields a token whose `Name` is "", unlike a nullable name which would use
// `CreateIfNotEmpty` and leave the token null on empty).
TEST(CSharp_PointerReferenceExpression, CtorWithEmptyStringCreatesTokenWithEmptyName) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string(""));
    ASSERT_NE(pre.MemberNameToken(), nullptr);
    EXPECT_EQ(pre.MemberNameToken()->Name(), "");
    EXPECT_EQ(pre.MemberName(), "");
}

// ---- The `Target` slot ------------------------------------------------

// The `Target` setter re-parents the new target and detaches the old one.
TEST(CSharp_PointerReferenceExpression, TargetSetterReparentsAndDetaches) {
    PointerReferenceExpression pre;
    auto a = std::make_unique<IdentifierExpression>(std::string("a"));
    auto b = std::make_unique<IdentifierExpression>(std::string("b"));
    pre.Target(a.get());
    EXPECT_EQ(a->Parent(), &pre);
    EXPECT_EQ(pre.Target(), a.get());
    EXPECT_EQ(a->ChildIndex, 0);
    pre.Target(b.get());
    EXPECT_EQ(b->Parent(), &pre);
    EXPECT_EQ(pre.Target(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(b->ChildIndex, 0);
}

// A null `Target` setter clears and detaches the old target.
TEST(CSharp_PointerReferenceExpression, TargetSetterClearsWithNull) {
    PointerReferenceExpression pre;
    auto a = std::make_unique<IdentifierExpression>(std::string("a"));
    pre.Target(a.get());
    pre.Target(nullptr);
    EXPECT_EQ(pre.Target(), nullptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
}

// ---- The `MemberNameToken` slot --------------------------------------

// The token setter re-parents the new token and detaches the old one.
TEST(CSharp_PointerReferenceExpression, MemberNameTokenSetterReparentsAndDetaches) {
    PointerReferenceExpression pre;
    auto a = Identifier::Create("a");
    auto b = Identifier::Create("b");
    pre.MemberNameToken(a);
    EXPECT_EQ(a->Parent(), &pre);
    EXPECT_EQ(pre.MemberNameToken(), a);
    EXPECT_EQ(a->ChildIndex, 1);
    pre.MemberNameToken(b);
    EXPECT_EQ(b->Parent(), &pre);
    EXPECT_EQ(pre.MemberNameToken(), b);
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(pre.MemberNameToken()->Name(), "b");
}

// ---- The `MemberName` string accessor --------------------------------

// The string accessor derefs the token and returns its `Name` (a `std::string` copy); a
// non-nullable name has no "absent" state (the token is required, so the getter derefs
// unconditionally -- a null token is a half-constructed node).
TEST(CSharp_PointerReferenceExpression, MemberNameReturnsTokenName) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Foo"));
    EXPECT_EQ(pre.MemberName(), "Foo");
}

// The string setter creates the token for a non-empty name and reads it back.
TEST(CSharp_PointerReferenceExpression, MemberNameSetterCreatesTokenForNonEmpty) {
    PointerReferenceExpression pre;
    pre.MemberName("Bar");
    EXPECT_EQ(pre.MemberName(), "Bar");
    ASSERT_NE(pre.MemberNameToken(), nullptr);
    EXPECT_EQ(pre.MemberNameToken()->Parent(), &pre);
    EXPECT_EQ(pre.MemberNameToken()->ChildIndex, 1);
}

// The string setter creates a token EVEN for an empty name (the non-nullable `MemberName` uses
// `Identifier.Create`, not `CreateIfNotEmpty`), so an empty `MemberName` assignment yields a
// token with an empty `Name`, not a null token.
TEST(CSharp_PointerReferenceExpression, MemberNameSetterCreatesTokenEvenForEmpty) {
    PointerReferenceExpression pre;
    pre.MemberName("");
    ASSERT_NE(pre.MemberNameToken(), nullptr);
    EXPECT_EQ(pre.MemberNameToken()->Name(), "");
    EXPECT_EQ(pre.MemberName(), "");
}

// ---- The `TypeArguments` collection ----------------------------------

// The collection starts empty (0 count); the node's child count is the two single slots (2) + 0.
TEST(CSharp_PointerReferenceExpression, TypeArgumentsEmptyByDefault) {
    PointerReferenceExpression pre;
    EXPECT_EQ(pre.TypeArguments().Count(), 0);
    EXPECT_EQ(pre.GetChildCount(), 2);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last slot, so an element's index is
// `2 + its local position`).
TEST(CSharp_PointerReferenceExpression, TypeArgumentsAddAppendsAndParentsIncremental) {
    PointerReferenceExpression pre;
    pre.MemberName("List");
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 12));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 17));
    pre.TypeArguments().Add(i.get());
    pre.TypeArguments().Add(s.get());
    EXPECT_EQ(pre.TypeArguments().Count(), 2);
    EXPECT_EQ(i->Parent(), &pre);
    EXPECT_EQ(s->Parent(), &pre);
    EXPECT_EQ(i->ChildIndex, 2);  // baseIndex 2 + 0
    EXPECT_EQ(s->ChildIndex, 3);  // baseIndex 2 + 1
    EXPECT_EQ(pre.GetChildCount(), 4);  // target + token + 2 type args
    EXPECT_TRUE(pre.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `TypeArguments` collection for the `TypeArgument` kind
// and null for any other kind (the `TargetExpression`/`Identifier` kinds are single slots).
TEST(CSharp_PointerReferenceExpression, GetCollectionByKindReturnsTypeArgumentsForTypeArgumentKind) {
    PointerReferenceExpression pre;
    pre.MemberName("Foo");
    EXPECT_NE(pre.GetCollectionByKind(&Slots::TypeArgument), nullptr);
    EXPECT_EQ(pre.GetCollectionByKind(&Slots::TypeArgument), &pre.TypeArguments());
    EXPECT_EQ(pre.GetCollectionByKind(&Slots::TargetExpression), nullptr);  // a single slot
    EXPECT_EQ(pre.GetCollectionByKind(&Slots::Identifier), nullptr);  // a single slot
    EXPECT_EQ(pre.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the target at index 0, the name token at index 1, and the type arguments
// at index 2+; the collection occupies the contiguous range [2, 2 + Count).
TEST(CSharp_PointerReferenceExpression, GetChildDispatchesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("M"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    pre.TypeArguments().Add(i.get());
    pre.TypeArguments().Add(s.get());
    EXPECT_EQ(pre.GetChild(0), target.get());
    EXPECT_EQ(pre.GetChild(1), pre.MemberNameToken());
    EXPECT_EQ(pre.GetChild(2), i.get());
    EXPECT_EQ(pre.GetChild(3), s.get());
    EXPECT_THROW(pre.GetChild(4), std::out_of_range);
    EXPECT_THROW(pre.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `TargetSlot` at index 0, the `MemberNameTokenSlot` at index 1,
// and the `TypeArgumentsSlot` at index 2+ (the slot identity the slot system compares by
// address).
TEST(CSharp_PointerReferenceExpression, GetChildSlotInfoDispatchesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("M"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    pre.TypeArguments().Add(i.get());
    EXPECT_EQ(pre.GetChildSlotInfo(0), &PointerReferenceExpression::TargetSlot);
    EXPECT_EQ(pre.GetChildSlotInfo(1), &PointerReferenceExpression::MemberNameTokenSlot);
    EXPECT_EQ(pre.GetChildSlotInfo(2), &PointerReferenceExpression::TypeArgumentsSlot);
    EXPECT_THROW(pre.GetChildSlotInfo(3), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `TargetExpression` KIND (the shared `Slots.TargetExpression`).
TEST(CSharp_PointerReferenceExpression, TargetSlotPointsAtTargetExpressionKind) {
    PointerReferenceExpression pre;
    EXPECT_EQ(pre.GetChildSlotInfo(0)->Kind(), &Slots::TargetExpression);
}

// `GetChildSlotInfo(1)` points at the `Identifier` KIND (the shared `Slots.Identifier`).
TEST(CSharp_PointerReferenceExpression, MemberNameTokenSlotPointsAtIdentifierKind) {
    PointerReferenceExpression pre;
    pre.MemberName("Foo");
    EXPECT_EQ(pre.GetChildSlotInfo(1)->Kind(), &Slots::Identifier);
}

// `GetChildSlotInfo(2)` points at the `TypeArgument` KIND (the shared `Slots.TypeArgument`).
TEST(CSharp_PointerReferenceExpression, TypeArgumentsSlotPointsAtTypeArgumentKind) {
    PointerReferenceExpression pre;
    pre.MemberName("Foo");
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    pre.TypeArguments().Add(i.get());
    EXPECT_EQ(pre.GetChildSlotInfo(2)->Kind(), &Slots::TypeArgument);
}

// `SetChild` writes the target at index 0, the name token at index 1, and replaces a type
// argument in place at index 2+ (the collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_PointerReferenceExpression, SetChildDispatchesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("M"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    pre.TypeArguments().Add(i.get());
    // Replace the name token at index 1.
    auto id = Identifier::Create("N");
    pre.SetChild(1, id);
    EXPECT_EQ(pre.MemberNameToken(), id);
    EXPECT_EQ(id->Parent(), &pre);
    EXPECT_EQ(id->ChildIndex, 1);
    // Replace the type argument at index 2.
    pre.SetChild(2, s.get());
    EXPECT_EQ(pre.TypeArguments().At(0), s.get());
    EXPECT_EQ(s->Parent(), &pre);
    EXPECT_THROW(pre.SetChild(3, nullptr), std::out_of_range);  // no element at index 3
}

// `SetChild` writes the target at index 0.
TEST(CSharp_PointerReferenceExpression, SetChildWritesTarget) {
    PointerReferenceExpression pre;
    pre.MemberName("M");
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    pre.SetChild(0, target.get());
    EXPECT_EQ(pre.Target(), target.get());
    EXPECT_EQ(target->Parent(), &pre);
    EXPECT_EQ(target->ChildIndex, 0);
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitPointerReferenceExpression` (the visitor-pattern
// round-trip); the depth-first walk then visits the `Target` child (a `VisitIdentifierExpression`)
// and the `MemberNameToken` child (a `VisitIdentifier`).
TEST(CSharp_PointerReferenceExpression, AcceptVisitorDispatchesToVisitPointerReferenceExpression) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    RecordingVisitor v;
    pre.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"pref:Member", "idexpr:obj", "id:obj", "id:Member"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_PointerReferenceExpression, AcceptVisitorIsVirtualThroughBases) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    AstNode* asAst = &pre;
    Expression* asExpr = &pre;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"pref:Member", "idexpr:obj", "id:obj", "id:Member"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"pref:Member", "idexpr:obj", "id:obj", "id:Member"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits the `Target` (a `VisitIdentifierExpression` -> its `Identifier`),
// then the `MemberNameToken` (a `VisitIdentifier`), then the `TypeArguments` (a
// `VisitPrimitiveType` per element) in document order.
TEST(CSharp_PointerReferenceExpression, DepthFirstWalkVisitsTargetTokenThenTypeArgumentsInOrder) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("M"));
    pre.MemberNameToken(Identifier::Create("M", TextLocation(1, 4)));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 7));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 12));
    pre.TypeArguments().Add(i.get());
    pre.TypeArguments().Add(s.get());
    RecordingVisitor v;
    pre.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "pref:M", "idexpr:obj", "id:obj", "id:M", "prim:int", "prim:string"}));
}

// ---- DoMatch (the generated recursive + string + collection match) ---

// Two `PointerReferenceExpression`s with the same target, member name, and the same (single)
// type argument match.
TEST(CSharp_PointerReferenceExpression, DoMatchMatchesSameTargetNameAndTypeArguments) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression a(ta.get(), std::string("Member"));
    PointerReferenceExpression b(tb.get(), std::string("Member"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A different `Target` rejects the match (the `MatchRequired` on `Target` -- the first term --
// rejects via the child's `DoMatch`).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsDifferentTarget) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("other"));
    PointerReferenceExpression a(ta.get(), std::string("Member"));
    PointerReferenceExpression b(tb.get(), std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // target names differ ("obj" vs "other")
}

// A null pattern `Target` rejects (the `MatchRequired` guard -- the first term).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsNullPatternTarget) {
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression a;  // no target
    a.MemberName("Member");
    PointerReferenceExpression b(tb.get(), std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A null candidate `Target` rejects (the `MatchRequired` flows the null child through the child's
// `DoMatch(nullptr)`, which returns false).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsNullCandidateTarget) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression a(ta.get(), std::string("Member"));
    PointerReferenceExpression b;  // no target
    b.MemberName("Member");
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A different `MemberName` rejects (the `MatchString` on `MemberName` -- the second term).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsDifferentName) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression a(ta.get(), std::string("Foo"));
    PointerReferenceExpression b(tb.get(), std::string("Bar"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `PointerReferenceExpression`s with the same target and name but different type-argument
// COUNTS do not match (the collection match fails when the counts differ).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsDifferentTypeArgumentCount) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression a(ta.get(), std::string("Member"));
    PointerReferenceExpression b(tb.get(), std::string("Member"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `PointerReferenceExpression`s with the same target, name, and count but different
// type-argument VALUES do not match (the element `DoMatch` -- a `MatchString` on the
// `PrimitiveType` keyword -- rejects).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsDifferentTypeArgumentValue) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression a(ta.get(), std::string("Member"));
    PointerReferenceExpression b(tb.get(), std::string("Member"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("string"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// A pattern `PointerReferenceExpression` whose `MemberName` is `$any$` (the `Pattern::AnyString`
// wildcard) matches any candidate `PointerReferenceExpression` with the same target and type
// arguments, regardless of the candidate's member name (the `MatchString` wildcard path). Each
// candidate has its OWN target (a child can have only one parent -- the D235 single-parent
// guard).
TEST(CSharp_PointerReferenceExpression, DoMatchAnyStringWildcardMatchesAnyName) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tc = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pattern(ta.get(), std::string(Pattern::AnyString));
    PointerReferenceExpression cand1(tb.get(), std::string("Foo"));
    PointerReferenceExpression cand2(tc.get(), std::string("Bar"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand1));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand2));
}

// A `PointerReferenceExpression` does not match a different concrete type (the `other is
// PointerReferenceExpression` gate); an `IdentifierExpression` is not a
// `PointerReferenceExpression`.
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    IdentifierExpression ie(std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&pre, &ie));
    EXPECT_FALSE(DoMatchAgainst(&ie, &pre));
}

// A `PointerReferenceExpression` does not match a `MemberReferenceExpression`, and vice versa --
// the two are STRUCTURAL TWINS (the exact same `Target` single slot + `MemberName` string-name
// `[Slot]` + `TypeArguments` collection shape) but distinct concrete types, so the `other is
// PointerReferenceExpression` / `other is MemberReferenceExpression` type-check gate rejects.
// This generalizes the D234 cross-sibling `DoMatch` rejection test (As vs Is) to a structural
// twin pair sharing an identical shape.
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsMemberReferenceExpressionCandidate) {
    auto ta = std::make_unique<IdentifierExpression>(std::string("obj"));
    auto tb = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(ta.get(), std::string("Member"));
    MemberReferenceExpression mre(tb.get(), std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&pre, &mre));
    EXPECT_FALSE(DoMatchAgainst(&mre, &pre));
}

// A `PointerReferenceExpression` (an `Expression`) does not match a `MemberType` (an `AstType`),
// and vice versa -- the two share the `MemberType` structural shape (a `Target` single slot + a
// `MemberName` string-name `[Slot]` + a `TypeArguments` collection) but are in disjoint
// hierarchies, so the type-check gate rejects.
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsMemberTypeCandidate) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    auto mtTarget = std::make_unique<SimpleType>(std::string("obj"));
    MemberType mt(mtTarget.get(), std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&pre, &mt));
    EXPECT_FALSE(DoMatchAgainst(&mt, &pre));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_PointerReferenceExpression, DoMatchRejectsNullCandidate) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    EXPECT_FALSE(DoMatchAgainst(&pre, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the target, the name token, and the type arguments, re-parents the
// clones, and detaches from the source.
TEST(CSharp_PointerReferenceExpression, CloneDeepCopiesTargetTokenAndTypeArguments) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"), TextLocation(1, 1));
    PointerReferenceExpression original(target.get(), std::string("Member"));
    original.MemberNameToken(Identifier::Create("Member", TextLocation(1, 5)));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 12));
    original.TypeArguments().Add(i.get());

    std::unique_ptr<PointerReferenceExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    // The target is a fresh clone, re-parented to the copy.
    ASSERT_NE(copy->Target(), nullptr);
    EXPECT_NE(copy->Target(), target.get());
    EXPECT_EQ(dynamic_cast<IdentifierExpression*>(copy->Target())->Identifier(), "obj");
    EXPECT_EQ(copy->Target()->Parent(), copy.get());
    // The name token is a fresh clone, re-parented to the copy, carrying its name and location.
    ASSERT_NE(copy->MemberNameToken(), nullptr);
    EXPECT_NE(copy->MemberNameToken(), original.MemberNameToken());
    EXPECT_EQ(copy->MemberName(), "Member");
    EXPECT_EQ(copy->MemberNameToken()->Parent(), copy.get());
    EXPECT_EQ(copy->MemberNameToken()->StartLocation(), TextLocation(1, 5));
    // The type argument is a fresh clone, re-parented to the copy.
    EXPECT_EQ(copy->TypeArguments().Count(), 1);
    ASSERT_NE(copy->TypeArguments().At(0), nullptr);
    EXPECT_NE(copy->TypeArguments().At(0), i.get());
    EXPECT_EQ(copy->TypeArguments().At(0)->Parent(), copy.get());
    // The original is unchanged.
    EXPECT_EQ(original.TypeArguments().Count(), 1);
    EXPECT_EQ(i->Parent(), &original);
    EXPECT_EQ(target->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_PointerReferenceExpression, CloneIsVirtualAndCovariant) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression original(target.get(), std::string("Member"));
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<PointerReferenceExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<PointerReferenceExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of a target-less `PointerReferenceExpression` (the empty ctor) with no name token and
// no type arguments yields a target-less, name-less clone with no type arguments.
TEST(CSharp_PointerReferenceExpression, CloneOfEmptyIsEmpty) {
    PointerReferenceExpression original;  // no target, no name token, no type arguments
    std::unique_ptr<PointerReferenceExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Target(), nullptr);
    EXPECT_EQ(copy->MemberNameToken(), nullptr);
    EXPECT_EQ(copy->TypeArguments().Count(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A `PointerReferenceExpression` with the target and name token set and type arguments passes
// the inherited `CheckInvariant` (the slot-structure verifier: every slot's
// `Parent`/`ChildIndex`/type consistent). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_PointerReferenceExpression, CheckInvariantPassesOnFilledNode) {
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Member"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    pre.TypeArguments().Add(i.get());
    pre.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `TargetSlot`, `MemberNameTokenSlot`, and `TypeArgumentsSlot` are distinct slot statics
// (compared by address); the node's `Slot()` reports the per-node slot for each child. The
// three slot statics have different `CSharpSlotInfoT<T>` element types, so they are compared
// through the common `CSharpSlotInfo` base (the pointer-identity comparison the slot system
// uses).
TEST(CSharp_PointerReferenceExpression, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&PointerReferenceExpression::TargetSlot),
              static_cast<const CSharpSlotInfo*>(&PointerReferenceExpression::MemberNameTokenSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&PointerReferenceExpression::TargetSlot),
              static_cast<const CSharpSlotInfo*>(&PointerReferenceExpression::TypeArgumentsSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&PointerReferenceExpression::MemberNameTokenSlot),
              static_cast<const CSharpSlotInfo*>(&PointerReferenceExpression::TypeArgumentsSlot));
    auto target = std::make_unique<IdentifierExpression>(std::string("obj"));
    PointerReferenceExpression pre(target.get(), std::string("Foo"));
    EXPECT_EQ(pre.MemberNameToken()->Slot(), &PointerReferenceExpression::MemberNameTokenSlot);
    EXPECT_EQ(pre.MemberNameToken()->Slot()->Kind(), &Slots::Identifier);
    EXPECT_EQ(pre.Target()->Slot(), &PointerReferenceExpression::TargetSlot);
    EXPECT_EQ(pre.Target()->Slot()->Kind(), &Slots::TargetExpression);
}
