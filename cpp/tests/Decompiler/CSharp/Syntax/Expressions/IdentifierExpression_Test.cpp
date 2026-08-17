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

// Tests for the `IdentifierExpression` concrete node (cpp/.../Syntax/Expressions/
// IdentifierExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.cs) -- the first
// AstType-bearing `Expression` with a COLLECTION slot (`TypeArguments`:
// `AstNodeCollection<AstType>`) and a string-name `[Slot]` (the non-nullable `Identifier`,
// over a backing `IdentifierToken`). Structurally identical to `SimpleType` but deriving from
// `Expression` (not `AstType`) and with a NON-nullable `Identifier` (a required token slot, a
// derefing getter, and a `Create`-not-`CreateIfNotEmpty` setter). Exercises the string-name
// accessor, the required token slot, the collection slot, the collection-aware slot-storage
// contract, the `AcceptVisitor` dispatch, the generated `DoMatch` (a `MatchString` on
// `Identifier` plus a collection `DoMatch` on `TypeArguments`), the per-concrete-node `Clone`,
// and the inherited `CheckInvariant`.

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
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
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
// the inherited `VisitChildren` (the document-order walk). `VisitIdentifierExpression`/
// `VisitIdentifier`/`VisitPrimitiveType` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitIdentifierExpression(IdentifierExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
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
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-st>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or("<anon>"));
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

// `IdentifierExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_IdentifierExpression, IsExpressionAndAstNodeNotAstType) {
    IdentifierExpression ie(std::string("Foo"));
    EXPECT_NE(dynamic_cast<Expression*>(&ie), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ie), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ie), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no token (the name is unset -- a required slot, so the node is only valid
// until the name is set) and no type arguments. `GetChildCount` is 1 (the empty token slot) +
// 0 type args.
TEST(CSharp_IdentifierExpression, EmptyCtorHasNoTokenAndNoTypeArguments) {
    IdentifierExpression ie;
    EXPECT_EQ(ie.IdentifierToken(), nullptr);
    EXPECT_EQ(ie.TypeArguments().Count(), 0);
    EXPECT_EQ(ie.GetChildCount(), 1);  // the token slot (empty) + 0 type args
    EXPECT_EQ(ie.StartLocation(), TextLocation::Empty);
}

// The `(string)` ctor (the generated required-prefix ctor) creates the token via
// `Identifier.Create` (NOT `CreateIfNotEmpty`); a non-empty name yields a token.
TEST(CSharp_IdentifierExpression, CtorWithStringCreatesToken) {
    IdentifierExpression ie(std::string("Qux"));
    EXPECT_EQ(ie.Identifier(), "Qux");
    ASSERT_NE(ie.IdentifierToken(), nullptr);
    EXPECT_EQ(ie.IdentifierToken()->Name(), "Qux");
    EXPECT_EQ(ie.IdentifierToken()->Parent(), &ie);
    EXPECT_EQ(ie.IdentifierToken()->ChildIndex, 0);
}

// An empty name in the `(string)` ctor creates a token with an EMPTY `Name` (NOT a null token --
// the non-nullable `Identifier` uses `Identifier.Create`, so an empty name yields a token whose
// `Name` is "", unlike `SimpleType` whose nullable name uses `CreateIfNotEmpty` and leaves the
// token null on empty).
TEST(CSharp_IdentifierExpression, CtorWithEmptyStringCreatesTokenWithEmptyName) {
    IdentifierExpression ie(std::string(""));
    ASSERT_NE(ie.IdentifierToken(), nullptr);
    EXPECT_EQ(ie.IdentifierToken()->Name(), "");
    EXPECT_EQ(ie.Identifier(), "");
}

// The `(string, TextLocation)` ctor (the hand-written ctor) creates the token via the factory
// and sets it through the kind-based `SetChildByKindUntyped`; the name and the token's start
// location read back.
TEST(CSharp_IdentifierExpression, CtorWithStringAndLocationCreatesToken) {
    IdentifierExpression ie(std::string("Baz"), TextLocation(4, 5));
    EXPECT_EQ(ie.Identifier(), "Baz");
    ASSERT_NE(ie.IdentifierToken(), nullptr);
    EXPECT_EQ(ie.IdentifierToken()->Name(), "Baz");
    EXPECT_EQ(ie.IdentifierToken()->StartLocation(), TextLocation(4, 5));
    EXPECT_EQ(ie.IdentifierToken()->Parent(), &ie);
    EXPECT_EQ(ie.IdentifierToken()->ChildIndex, 0);
}

// ---- The `Identifier` string accessor --------------------------------

// The string accessor derefs the token and returns its `Name` (a `std::string` copy); a
// non-nullable name has no "absent" state (the token is required, so the getter derefs
// unconditionally -- a null token is a half-constructed node).
TEST(CSharp_IdentifierExpression, IdentifierReturnsTokenName) {
    IdentifierExpression ie(std::string("Foo"));
    EXPECT_EQ(ie.Identifier(), "Foo");
}

// The string setter creates the token for a non-empty name and reads it back.
TEST(CSharp_IdentifierExpression, IdentifierSetterCreatesTokenForNonEmpty) {
    IdentifierExpression ie;
    ie.Identifier("Foo");
    EXPECT_EQ(ie.Identifier(), "Foo");
    ASSERT_NE(ie.IdentifierToken(), nullptr);
    EXPECT_EQ(ie.IdentifierToken()->Parent(), &ie);
    EXPECT_EQ(ie.IdentifierToken()->ChildIndex, 0);
}

// The string setter creates a token EVEN for an empty name (the non-nullable `Identifier` uses
// `Identifier.Create`, not `CreateIfNotEmpty`), so an empty `Identifier` assignment yields a
// token with an empty `Name`, not a null token -- unlike `SimpleType` whose setter clears the
// token on empty.
TEST(CSharp_IdentifierExpression, IdentifierSetterCreatesTokenEvenForEmpty) {
    IdentifierExpression ie;
    ie.Identifier("");
    ASSERT_NE(ie.IdentifierToken(), nullptr);
    EXPECT_EQ(ie.IdentifierToken()->Name(), "");
    EXPECT_EQ(ie.Identifier(), "");
}

// ---- The `IdentifierToken` slot --------------------------------------

// The token setter re-parents the new token and detaches the old one.
TEST(CSharp_IdentifierExpression, IdentifierTokenSetterReparentsAndDetaches) {
    IdentifierExpression ie;
    auto a = std::make_unique<Identifier>();
    a->Name("a");
    auto b = std::make_unique<Identifier>();
    b->Name("b");
    ie.IdentifierToken(a.get());
    EXPECT_EQ(a->Parent(), &ie);
    EXPECT_EQ(ie.IdentifierToken(), a.get());
    ie.IdentifierToken(b.get());
    EXPECT_EQ(b->Parent(), &ie);
    EXPECT_EQ(ie.IdentifierToken(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(ie.IdentifierToken()->Name(), "b");
}

// ---- The `TypeArguments` collection ----------------------------------

// The collection starts empty (0 count); the node's child count is the token slot (1) + 0.
TEST(CSharp_IdentifierExpression, TypeArgumentsEmptyByDefault) {
    IdentifierExpression ie;
    EXPECT_EQ(ie.TypeArguments().Count(), 0);
    EXPECT_EQ(ie.GetChildCount(), 1);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex`
// incrementally (the collection is the node's only collection at its last slot, so an
// element's index is `1 + its local position`).
TEST(CSharp_IdentifierExpression, TypeArgumentsAddAppendsAndParentsIncremental) {
    IdentifierExpression ie(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 11));
    ie.TypeArguments().Add(i.get());
    ie.TypeArguments().Add(s.get());
    EXPECT_EQ(ie.TypeArguments().Count(), 2);
    EXPECT_EQ(i->Parent(), &ie);
    EXPECT_EQ(s->Parent(), &ie);
    EXPECT_EQ(i->ChildIndex, 1);  // baseIndex 1 + 0
    EXPECT_EQ(s->ChildIndex, 2);  // baseIndex 1 + 1
    EXPECT_EQ(ie.GetChildCount(), 3);  // token + 2 type args
    EXPECT_TRUE(ie.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `TypeArguments` collection for the `TypeArgument` kind
// and null for any other kind (the `Identifier` kind is a single slot, not a collection).
TEST(CSharp_IdentifierExpression, GetCollectionByKindReturnsTypeArgumentsForTypeArgumentKind) {
    IdentifierExpression ie(std::string("List"));
    EXPECT_NE(ie.GetCollectionByKind(&Slots::TypeArgument), nullptr);
    EXPECT_EQ(ie.GetCollectionByKind(&Slots::TypeArgument), &ie.TypeArguments());
    EXPECT_EQ(ie.GetCollectionByKind(&Slots::Identifier), nullptr);  // a single slot
    EXPECT_EQ(ie.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the token at index 0 and the type arguments at index 1+; the collection
// occupies the contiguous range [1, 1 + Count).
TEST(CSharp_IdentifierExpression, GetChildDispatchesTokenAndTypeArguments) {
    IdentifierExpression ie(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    ie.TypeArguments().Add(i.get());
    ie.TypeArguments().Add(s.get());
    EXPECT_EQ(ie.GetChild(0), ie.IdentifierToken());
    EXPECT_EQ(ie.GetChild(1), i.get());
    EXPECT_EQ(ie.GetChild(2), s.get());
    EXPECT_THROW(ie.GetChild(3), std::out_of_range);
    EXPECT_THROW(ie.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `IdentifierTokenSlot` at index 0 and the `TypeArgumentsSlot`
// at index 1+ (the slot identity the slot system compares by address).
TEST(CSharp_IdentifierExpression, GetChildSlotInfoDispatchesTokenAndTypeArguments) {
    IdentifierExpression ie(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    ie.TypeArguments().Add(i.get());
    EXPECT_EQ(ie.GetChildSlotInfo(0), &IdentifierExpression::IdentifierTokenSlot);
    EXPECT_EQ(ie.GetChildSlotInfo(1), &IdentifierExpression::TypeArgumentsSlot);
    EXPECT_THROW(ie.GetChildSlotInfo(2), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Identifier` KIND (the shared `Slots.Identifier`).
TEST(CSharp_IdentifierExpression, IdentifierTokenSlotPointsAtIdentifierKind) {
    IdentifierExpression ie(std::string("Foo"));
    EXPECT_EQ(ie.GetChildSlotInfo(0)->Kind(), &Slots::Identifier);
}

// `GetChildSlotInfo(1)` points at the `TypeArgument` KIND (the shared `Slots.TypeArgument`).
TEST(CSharp_IdentifierExpression, TypeArgumentsSlotPointsAtTypeArgumentKind) {
    IdentifierExpression ie(std::string("Foo"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    ie.TypeArguments().Add(i.get());
    EXPECT_EQ(ie.GetChildSlotInfo(1)->Kind(), &Slots::TypeArgument);
}

// `SetChild` writes the token at index 0 and replaces a type argument in place at index 1+
// (the collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_IdentifierExpression, SetChildDispatchesTokenAndTypeArguments) {
    IdentifierExpression ie(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    ie.TypeArguments().Add(i.get());
    // Replace the token at index 0.
    auto id = Identifier::Create("Dict");
    ie.SetChild(0, id);
    EXPECT_EQ(ie.IdentifierToken(), id);
    EXPECT_EQ(id->Parent(), &ie);
    EXPECT_EQ(id->ChildIndex, 0);
    // Replace the type argument at index 1.
    ie.SetChild(1, s.get());
    EXPECT_EQ(ie.TypeArguments().At(0), s.get());
    EXPECT_EQ(s->Parent(), &ie);
    EXPECT_THROW(ie.SetChild(2, nullptr), std::out_of_range);  // no element at index 2
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitIdentifierExpression` (the visitor-pattern round-trip);
// the depth-first walk then visits the `IdentifierToken` child (a `VisitIdentifier`).
TEST(CSharp_IdentifierExpression, AcceptVisitorDispatchesToVisitIdentifierExpression) {
    IdentifierExpression ie(std::string("Foo"));
    RecordingVisitor v;
    ie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"idexpr:Foo", "id:Foo"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_IdentifierExpression, AcceptVisitorIsVirtualThroughBases) {
    IdentifierExpression ie(std::string("Foo"));
    AstNode* asAst = &ie;
    Expression* asExpr = &ie;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"idexpr:Foo", "id:Foo"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"idexpr:Foo", "id:Foo"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits the `IdentifierToken` (a `VisitIdentifier`) then the
// `TypeArguments` (a `VisitPrimitiveType` per element) in document order.
TEST(CSharp_IdentifierExpression, DepthFirstWalkVisitsTokenThenTypeArgumentsInOrder) {
    IdentifierExpression ie(std::string("List"));
    ie.IdentifierToken(Identifier::Create("List", TextLocation(1, 1)));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 11));
    ie.TypeArguments().Add(i.get());
    ie.TypeArguments().Add(s.get());
    RecordingVisitor v;
    ie.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"idexpr:List", "id:List", "prim:int", "prim:string"}));
}

// ---- DoMatch (the generated string + collection match) --------------

// Two `IdentifierExpression`s with the same name and the same (single) type argument match.
TEST(CSharp_IdentifierExpression, DoMatchMatchesSameNameAndTypeArguments) {
    IdentifierExpression a(std::string("List"));
    IdentifierExpression b(std::string("List"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `IdentifierExpression`s with the same name but different type-argument COUNTS do not
// match (the collection match fails when the counts differ).
TEST(CSharp_IdentifierExpression, DoMatchRejectsDifferentTypeArgumentCount) {
    IdentifierExpression a(std::string("List"));
    IdentifierExpression b(std::string("List"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `IdentifierExpression`s with the same name and count but different type-argument VALUES do
// not match (the element `DoMatch` -- a `MatchString` on the `PrimitiveType` keyword -- rejects).
TEST(CSharp_IdentifierExpression, DoMatchRejectsDifferentTypeArgumentValue) {
    IdentifierExpression a(std::string("List"));
    IdentifierExpression b(std::string("List"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("string"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `IdentifierExpression`s with different NAMES do not match (the `MatchString` on
// `Identifier` rejects), even with the same (empty) type arguments.
TEST(CSharp_IdentifierExpression, DoMatchRejectsDifferentName) {
    IdentifierExpression a(std::string("List"));
    IdentifierExpression b(std::string("Dict"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `IdentifierExpression`s both with an EMPTY name (tokens with `Name` "") match on the
// `Identifier` term (`MatchString` of two empty strings is true) with no type arguments. The
// non-nullable name always yields a token (never null), so an empty name is a real "" match,
// not a null match -- unlike `SimpleType` whose nullable name has a "nameless" (nullopt) state.
TEST(CSharp_IdentifierExpression, DoMatchMatchesTwoEmptyNames) {
    IdentifierExpression a(std::string(""));
    IdentifierExpression b(std::string(""));
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// An empty-name pattern does not match a non-empty candidate (an "" `Identifier` matches only
// an "" `Identifier`); and vice versa.
TEST(CSharp_IdentifierExpression, DoMatchEmptyNameDoesNotMatchNonEmpty) {
    IdentifierExpression a(std::string(""));
    IdentifierExpression b(std::string("List"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A pattern `IdentifierExpression` whose `Identifier` is `$any$` (the `Pattern::AnyString`
// wildcard) matches any candidate `IdentifierExpression` regardless of its name (the
// `MatchString` wildcard path).
TEST(CSharp_IdentifierExpression, DoMatchAnyStringWildcardMatchesAnyName) {
    IdentifierExpression pattern;
    pattern.Identifier(std::string(Pattern::AnyString));
    IdentifierExpression cand1(std::string("List"));
    IdentifierExpression cand2(std::string("Dict"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand1));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand2));
}

// An `IdentifierExpression` does not match a different concrete type (the `other is
// IdentifierExpression` gate); a `PrimitiveType` is not an `IdentifierExpression`.
TEST(CSharp_IdentifierExpression, DoMatchRejectsPrimitiveTypeCandidate) {
    IdentifierExpression ie(std::string("Foo"));
    PrimitiveType pt(std::string("int"));
    EXPECT_FALSE(DoMatchAgainst(&ie, &pt));
    EXPECT_FALSE(DoMatchAgainst(&pt, &ie));
}

// An `IdentifierExpression` (an `Expression`) does not match a `SimpleType` (an `AstType`),
// and vice versa -- the two share the `SimpleType` structural shape but are in disjoint
// hierarchies, so the type-check gate rejects.
TEST(CSharp_IdentifierExpression, DoMatchRejectsSimpleTypeCandidate) {
    IdentifierExpression ie(std::string("Foo"));
    SimpleType st(std::string("Foo"));
    EXPECT_FALSE(DoMatchAgainst(&ie, &st));
    EXPECT_FALSE(DoMatchAgainst(&st, &ie));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_IdentifierExpression, DoMatchRejectsNullCandidate) {
    IdentifierExpression ie(std::string("Foo"));
    EXPECT_FALSE(DoMatchAgainst(&ie, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the name (via the token) and the type arguments, re-parents the
// clones, and detaches from the source.
TEST(CSharp_IdentifierExpression, CloneDeepCopiesTokenAndTypeArguments) {
    IdentifierExpression original(std::string("List"), TextLocation(1, 1));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    original.TypeArguments().Add(i.get());

    std::unique_ptr<IdentifierExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    EXPECT_EQ(copy->Identifier(), "List");
    ASSERT_NE(copy->IdentifierToken(), nullptr);
    EXPECT_NE(copy->IdentifierToken(), original.IdentifierToken());  // a fresh token
    EXPECT_EQ(copy->IdentifierToken()->Parent(), copy.get());  // re-parented to the clone
    EXPECT_EQ(copy->IdentifierToken()->StartLocation(), TextLocation(1, 1));  // location carried
    EXPECT_EQ(copy->TypeArguments().Count(), 1);
    ASSERT_NE(copy->TypeArguments().At(0), nullptr);
    EXPECT_NE(copy->TypeArguments().At(0), i.get());  // a fresh type argument
    EXPECT_EQ(copy->TypeArguments().At(0)->Parent(), copy.get());  // re-parented
    // The original is unchanged.
    EXPECT_EQ(original.TypeArguments().Count(), 1);
    EXPECT_EQ(i->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `Expression*` (returns an `Expression*`).
TEST(CSharp_IdentifierExpression, CloneIsVirtualAndCovariant) {
    IdentifierExpression original(std::string("Foo"));
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<IdentifierExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &original;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<IdentifierExpression*>(exprCopy.get()), nullptr);
}

// `Clone` of a token-less `IdentifierExpression` (the empty ctor) with no type arguments yields
// a token-less clone with no type arguments (the token is null, the collection is empty).
TEST(CSharp_IdentifierExpression, CloneOfTokenlessEmptyIsTokenlessEmpty) {
    IdentifierExpression original;  // no token, no type arguments
    std::unique_ptr<IdentifierExpression> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->IdentifierToken(), nullptr);
    EXPECT_EQ(copy->TypeArguments().Count(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `IdentifierExpression` with the token set and type arguments passes the inherited
// `CheckInvariant` (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type
// consistent). Runs in debug builds (a no-op in NDEBUG).
TEST(CSharp_IdentifierExpression, CheckInvariantPassesOnFilledNode) {
    IdentifierExpression ie(std::string("List"), TextLocation(1, 1));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    ie.TypeArguments().Add(i.get());
    ie.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `IdentifierTokenSlot` and `TypeArgumentsSlot` are distinct slot statics (compared by
// address); the node's `Slot()` reports the `IdentifierTokenSlot` for the token. The two slot
// statics have different `CSharpSlotInfoT<T>` element types, so they are compared through the
// common `CSharpSlotInfo` base (the pointer-identity comparison the slot system uses).
TEST(CSharp_IdentifierExpression, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&IdentifierExpression::IdentifierTokenSlot),
              static_cast<const CSharpSlotInfo*>(&IdentifierExpression::TypeArgumentsSlot));
    IdentifierExpression ie(std::string("Foo"));
    EXPECT_EQ(ie.IdentifierToken()->Slot(), &IdentifierExpression::IdentifierTokenSlot);
    EXPECT_EQ(ie.IdentifierToken()->Slot()->Kind(), &Slots::Identifier);
}
