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

// Tests for the `SimpleType` concrete node (cpp/.../Syntax/SimpleType.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/SimpleType.cs) -- the second concrete `AstType` and the
// first ported node with a COLLECTION slot (`TypeArguments`: `AstNodeCollection<AstType>`) and
// a string-name `[Slot]` (`Identifier`, over a backing `IdentifierToken`). Exercises the
// string-name accessor, the token slot, the collection slot, the collection-aware slot-storage
// contract, the `AcceptVisitor` dispatch, the generated `DoMatch` (a `MatchString` on
// `Identifier` plus a collection `DoMatch` on `TypeArguments`), the per-concrete-node `Clone`,
// and the inherited `CheckInvariant`. Also exercises the `AstNodeCollection` collection-DoMatch
// surface (`NodeCount`/`NodeAt`/`AsNodeList`/`DoMatch`) through `SimpleType`'s generated
// `DoMatch` (the first node whose recursive term is a collection match).

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
// the inherited `VisitChildren` (the document-order walk). `VisitSimpleType`/`VisitIdentifier`/
// `VisitPrimitiveType` are the nodes these tests build.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or("<anon>"));
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

// `SimpleType` is an `AstType` and an `AstNode` (the `dynamic_cast` is-a the slot system and
// the annotation channel use); it is NOT an `Expression` (it derives from `AstType`, parallel
// to -- not under -- `Expression`).
TEST(CSharp_SimpleType, IsAstTypeAndAstNodeNotExpression) {
    SimpleType st(std::string("Foo"));
    EXPECT_NE(dynamic_cast<AstType*>(&st), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&st), nullptr);
    EXPECT_EQ(dynamic_cast<Expression*>(&st), nullptr);
}

// ---- Construction -----------------------------------------------------

// The empty ctor has no name (the `Identifier` is nullopt) and no type arguments.
TEST(CSharp_SimpleType, EmptyCtorHasNoNameAndNoTypeArguments) {
    SimpleType st;
    EXPECT_FALSE(st.Identifier().has_value());
    EXPECT_EQ(st.TypeArguments().Count(), 0);
    EXPECT_EQ(st.GetChildCount(), 1);  // the token slot (empty) + 0 type args
    EXPECT_EQ(st.StartLocation(), TextLocation::Empty);
}

// The `(Identifier*)` ctor sets the token directly; the name reads back through the string
// accessor.
TEST(CSharp_SimpleType, CtorWithIdentifierTokenSetsName) {
    auto id = std::make_unique<Identifier>();
    id->Name("Bar");
    id->SetStartLocation(TextLocation(2, 3));
    SimpleType st(id.get());
    ASSERT_TRUE(st.Identifier().has_value());
    EXPECT_EQ(*st.Identifier(), "Bar");
    EXPECT_EQ(st.IdentifierToken(), id.get());
    EXPECT_EQ(id->Parent(), &st);
    EXPECT_EQ(id->ChildIndex, 0);
}

// The `(string, TextLocation)` ctor creates the token via the factory and sets it through the
// kind-based `SetChildByKindUntyped`; the name and the token's start location read back.
TEST(CSharp_SimpleType, CtorWithStringAndLocationCreatesToken) {
    SimpleType st(std::string("Baz"), TextLocation(4, 5));
    ASSERT_TRUE(st.Identifier().has_value());
    EXPECT_EQ(*st.Identifier(), "Baz");
    ASSERT_NE(st.IdentifierToken(), nullptr);
    EXPECT_EQ(st.IdentifierToken()->Name(), "Baz");
    EXPECT_EQ(st.IdentifierToken()->StartLocation(), TextLocation(4, 5));
    EXPECT_EQ(st.IdentifierToken()->Parent(), &st);
    EXPECT_EQ(st.IdentifierToken()->ChildIndex, 0);
}

// The `(string)` ctor (the generated required-prefix ctor) creates the token via
// `Identifier.CreateIfNotEmpty`; a non-empty name yields a token.
TEST(CSharp_SimpleType, CtorWithStringCreatesTokenForNonEmpty) {
    SimpleType st(std::string("Qux"));
    ASSERT_TRUE(st.Identifier().has_value());
    EXPECT_EQ(*st.Identifier(), "Qux");
    ASSERT_NE(st.IdentifierToken(), nullptr);
}

// An empty name in the `(string)` ctor leaves the token null (`CreateIfNotEmpty` returns null
// for empty); the `Identifier` reads nullopt.
TEST(CSharp_SimpleType, CtorWithEmptyStringLeavesTokenNull) {
    SimpleType st(std::string(""));
    EXPECT_FALSE(st.Identifier().has_value());
    EXPECT_EQ(st.IdentifierToken(), nullptr);
}

// ---- The `Identifier` string accessor --------------------------------

// The string accessor returns nullopt when the token is absent (an optional name).
TEST(CSharp_SimpleType, IdentifierIsNulloptWhenNoToken) {
    SimpleType st;
    EXPECT_FALSE(st.Identifier().has_value());
}

// The string setter creates the token for a non-empty name and reads it back.
TEST(CSharp_SimpleType, IdentifierSetterCreatesTokenForNonEmpty) {
    SimpleType st;
    st.Identifier("Foo");
    ASSERT_TRUE(st.Identifier().has_value());
    EXPECT_EQ(*st.Identifier(), "Foo");
    ASSERT_NE(st.IdentifierToken(), nullptr);
    EXPECT_EQ(st.IdentifierToken()->Parent(), &st);
    EXPECT_EQ(st.IdentifierToken()->ChildIndex, 0);
}

// The string setter CLEARS the token on an empty name (`CreateIfNotEmpty` returns null), so
// an empty `Identifier` assignment removes the name.
TEST(CSharp_SimpleType, IdentifierSetterClearsTokenOnEmpty) {
    SimpleType st(std::string("Foo"));
    ASSERT_TRUE(st.Identifier().has_value());
    st.Identifier("");  // clears the token
    EXPECT_FALSE(st.Identifier().has_value());
    EXPECT_EQ(st.IdentifierToken(), nullptr);
}

// ---- The `IdentifierToken` slot --------------------------------------

// The token setter re-parents the new token and detaches the old one.
TEST(CSharp_SimpleType, IdentifierTokenSetterReparentsAndDetaches) {
    SimpleType st;
    auto a = std::make_unique<Identifier>();
    a->Name("a");
    auto b = std::make_unique<Identifier>();
    b->Name("b");
    st.IdentifierToken(a.get());
    EXPECT_EQ(a->Parent(), &st);
    EXPECT_EQ(st.IdentifierToken(), a.get());
    st.IdentifierToken(b.get());
    EXPECT_EQ(b->Parent(), &st);
    EXPECT_EQ(st.IdentifierToken(), b.get());
    EXPECT_EQ(a->Parent(), nullptr);  // detached
    EXPECT_EQ(st.IdentifierToken()->Name(), "b");
}

// ---- The `TypeArguments` collection ----------------------------------

// The collection starts empty (0 count); the node's child count is the token slot (1) + 0.
TEST(CSharp_SimpleType, TypeArgumentsEmptyByDefault) {
    SimpleType st;
    EXPECT_EQ(st.TypeArguments().Count(), 0);
    EXPECT_EQ(st.GetChildCount(), 1);
}

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex`
// incrementally (the collection is the node's only collection at its last slot, so an
// element's index is `1 + its local position`).
TEST(CSharp_SimpleType, TypeArgumentsAddAppendsAndParentsIncremental) {
    SimpleType st(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 11));
    st.TypeArguments().Add(i.get());
    st.TypeArguments().Add(s.get());
    EXPECT_EQ(st.TypeArguments().Count(), 2);
    EXPECT_EQ(i->Parent(), &st);
    EXPECT_EQ(s->Parent(), &st);
    EXPECT_EQ(i->ChildIndex, 1);  // baseIndex 1 + 0
    EXPECT_EQ(s->ChildIndex, 2);  // baseIndex 1 + 1
    EXPECT_EQ(st.GetChildCount(), 3);  // token + 2 type args
    EXPECT_TRUE(st.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `TypeArguments` collection for the `TypeArgument` kind
// and null for any other kind (the `Identifier` kind is a single slot, not a collection).
TEST(CSharp_SimpleType, GetCollectionByKindReturnsTypeArgumentsForTypeArgumentKind) {
    SimpleType st(std::string("List"));
    EXPECT_NE(st.GetCollectionByKind(&Slots::TypeArgument), nullptr);
    EXPECT_EQ(st.GetCollectionByKind(&Slots::TypeArgument), &st.TypeArguments());
    EXPECT_EQ(st.GetCollectionByKind(&Slots::Identifier), nullptr);  // a single slot, not a collection
    EXPECT_EQ(st.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-aware dispatch) -----------

// `GetChild` returns the token at index 0 and the type arguments at index 1+; the collection
// occupies the contiguous range [1, 1 + Count).
TEST(CSharp_SimpleType, GetChildDispatchesTokenAndTypeArguments) {
    SimpleType st(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    st.TypeArguments().Add(i.get());
    st.TypeArguments().Add(s.get());
    EXPECT_EQ(st.GetChild(0), st.IdentifierToken());
    EXPECT_EQ(st.GetChild(1), i.get());
    EXPECT_EQ(st.GetChild(2), s.get());
    EXPECT_THROW(st.GetChild(3), std::out_of_range);
    EXPECT_THROW(st.GetChild(-1), std::out_of_range);
}

// `GetChildSlotInfo` returns the `IdentifierTokenSlot` at index 0 and the `TypeArgumentsSlot`
// at index 1+ (the slot identity the slot system compares by address).
TEST(CSharp_SimpleType, GetChildSlotInfoDispatchesTokenAndTypeArguments) {
    SimpleType st(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    st.TypeArguments().Add(i.get());
    EXPECT_EQ(st.GetChildSlotInfo(0), &SimpleType::IdentifierTokenSlot);
    EXPECT_EQ(st.GetChildSlotInfo(1), &SimpleType::TypeArgumentsSlot);
    EXPECT_THROW(st.GetChildSlotInfo(2), std::out_of_range);
}

// `GetChildSlotInfo(0)` points at the `Identifier` KIND (the shared `Slots.Identifier`).
TEST(CSharp_SimpleType, IdentifierTokenSlotPointsAtIdentifierKind) {
    SimpleType st(std::string("Foo"));
    EXPECT_EQ(st.GetChildSlotInfo(0)->Kind(), &Slots::Identifier);
}

// `GetChildSlotInfo(1)` points at the `TypeArgument` KIND (the shared `Slots.TypeArgument`).
TEST(CSharp_SimpleType, TypeArgumentsSlotPointsAtTypeArgumentKind) {
    SimpleType st(std::string("Foo"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    st.TypeArguments().Add(i.get());
    EXPECT_EQ(st.GetChildSlotInfo(1)->Kind(), &Slots::TypeArgument);
}

// `SetChild` writes the token at index 0 and replaces a type argument in place at index 1+
// (the collection's `SetAt` re-parents and carries the old index).
TEST(CSharp_SimpleType, SetChildDispatchesTokenAndTypeArguments) {
    SimpleType st(std::string("List"));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    auto s = std::make_unique<PrimitiveType>(std::string("string"));
    st.TypeArguments().Add(i.get());
    // Replace the token at index 0.
    auto id = Identifier::Create("Dict");
    st.SetChild(0, id);
    EXPECT_EQ(st.IdentifierToken(), id);
    EXPECT_EQ(id->Parent(), &st);
    EXPECT_EQ(id->ChildIndex, 0);
    // Replace the type argument at index 1.
    st.SetChild(1, s.get());
    EXPECT_EQ(st.TypeArguments().At(0), s.get());
    EXPECT_EQ(s->Parent(), &st);
    EXPECT_THROW(st.SetChild(2, nullptr), std::out_of_range);  // no element at index 2
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitSimpleType` (the visitor-pattern round-trip); the
// depth-first walk then visits the `IdentifierToken` child (a `VisitIdentifier`).
TEST(CSharp_SimpleType, AcceptVisitorDispatchesToVisitSimpleType) {
    SimpleType st(std::string("Foo"));
    RecordingVisitor v;
    st.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"simple:Foo", "id:Foo"}));
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `AstType*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_SimpleType, AcceptVisitorIsVirtualThroughBases) {
    SimpleType st(std::string("Foo"));
    AstNode* asAst = &st;
    AstType* asType = &st;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asType->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"simple:Foo", "id:Foo"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"simple:Foo", "id:Foo"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits the `IdentifierToken` (a `VisitIdentifier`) then the
// `TypeArguments` (a `VisitPrimitiveType` per element) in document order.
TEST(CSharp_SimpleType, DepthFirstWalkVisitsTokenThenTypeArgumentsInOrder) {
    SimpleType st(std::string("List"));
    st.IdentifierToken(Identifier::Create("List", TextLocation(1, 1)));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    auto s = std::make_unique<PrimitiveType>(std::string("string"), TextLocation(1, 11));
    st.TypeArguments().Add(i.get());
    st.TypeArguments().Add(s.get());
    RecordingVisitor v;
    st.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"simple:List", "id:List", "prim:int", "prim:string"}));
}

// ---- DoMatch (the generated string + collection match) --------------

// Two `SimpleType`s with the same name and the same (single) type argument match.
TEST(CSharp_SimpleType, DoMatchMatchesSameNameAndTypeArguments) {
    SimpleType a(std::string("List"));
    SimpleType b(std::string("List"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// Two `SimpleType`s with the same name but different type-argument COUNTS do not match (the
// collection match fails when the counts differ).
TEST(CSharp_SimpleType, DoMatchRejectsDifferentTypeArgumentCount) {
    SimpleType a(std::string("List"));
    SimpleType b(std::string("List"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    a.TypeArguments().Add(ai.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));  // a has 1 arg, b has 0
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// Two `SimpleType`s with the same name and count but different type-argument VALUES do not
// match (the element `DoMatch` -- a `MatchString` on the `PrimitiveType` keyword -- rejects).
TEST(CSharp_SimpleType, DoMatchRejectsDifferentTypeArgumentValue) {
    SimpleType a(std::string("List"));
    SimpleType b(std::string("List"));
    auto ai = std::make_unique<PrimitiveType>(std::string("int"));
    auto bi = std::make_unique<PrimitiveType>(std::string("string"));
    a.TypeArguments().Add(ai.get());
    b.TypeArguments().Add(bi.get());
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two `SimpleType`s with different NAMES do not match (the `MatchString` on `Identifier`
// rejects), even with the same (empty) type arguments.
TEST(CSharp_SimpleType, DoMatchRejectsDifferentName) {
    SimpleType a(std::string("List"));
    SimpleType b(std::string("Dict"));
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// Two nameless `SimpleType`s (both `nullopt`) match on the `Identifier` term (`MatchString`
// of two nulls is true) with no type arguments.
TEST(CSharp_SimpleType, DoMatchMatchesTwoNameless) {
    SimpleType a;
    SimpleType b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A named pattern does not match a nameless candidate (a null `Identifier` matches only a null
// `Identifier`).
TEST(CSharp_SimpleType, DoMatchNamedDoesNotMatchNameless) {
    SimpleType a(std::string("Foo"));
    SimpleType b;
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A pattern `SimpleType` whose `Identifier` is `$any$` (the `Pattern::AnyString` wildcard)
// matches any candidate `SimpleType` regardless of its name (the `MatchString` wildcard path).
TEST(CSharp_SimpleType, DoMatchAnyStringWildcardMatchesAnyName) {
    SimpleType pattern;
    pattern.Identifier(std::string(Pattern::AnyString));
    SimpleType cand1(std::string("List"));
    SimpleType cand2(std::string("Dict"));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand1));
    EXPECT_TRUE(DoMatchAgainst(&pattern, &cand2));
}

// A `SimpleType` does not match a different concrete type (the `other is SimpleType` gate); a
// `PrimitiveType` is not a `SimpleType`.
TEST(CSharp_SimpleType, DoMatchRejectsDifferentType) {
    SimpleType st(std::string("Foo"));
    PrimitiveType pt(std::string("int"));
    EXPECT_FALSE(DoMatchAgainst(&st, &pt));
    EXPECT_FALSE(DoMatchAgainst(&pt, &st));
}

// A null candidate is rejected (`dynamic_cast` of null is null).
TEST(CSharp_SimpleType, DoMatchRejectsNullCandidate) {
    SimpleType st(std::string("Foo"));
    EXPECT_FALSE(DoMatchAgainst(&st, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the name (via the token) and the type arguments, re-parents the
// clones, and detaches from the source.
TEST(CSharp_SimpleType, CloneDeepCopiesTokenAndTypeArguments) {
    SimpleType original(std::string("List"), TextLocation(1, 1));
    auto i = std::make_unique<PrimitiveType>(std::string("int"), TextLocation(1, 6));
    original.TypeArguments().Add(i.get());

    std::unique_ptr<SimpleType> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(copy.get(), &original);
    EXPECT_EQ(copy->Parent(), nullptr);  // detached
    ASSERT_TRUE(copy->Identifier().has_value());
    EXPECT_EQ(*copy->Identifier(), "List");
    ASSERT_NE(copy->IdentifierToken(), nullptr);
    EXPECT_NE(copy->IdentifierToken(), original.IdentifierToken());  // a fresh token
    EXPECT_EQ(copy->IdentifierToken()->Parent(), copy.get());  // re-parented to the clone
    EXPECT_EQ(copy->TypeArguments().Count(), 1);
    ASSERT_NE(copy->TypeArguments().At(0), nullptr);
    EXPECT_NE(copy->TypeArguments().At(0), i.get());  // a fresh type argument
    EXPECT_EQ(copy->TypeArguments().At(0)->Parent(), copy.get());  // re-parented
    // The original is unchanged.
    EXPECT_EQ(original.TypeArguments().Count(), 1);
    EXPECT_EQ(i->Parent(), &original);
}

// `Clone` is virtual through an `AstNode*` (returns an `AstNode*`, dispatched to the concrete
// override) and covariant through an `AstType*` (returns an `AstType*`).
TEST(CSharp_SimpleType, CloneIsVirtualAndCovariant) {
    SimpleType original(std::string("Foo"));
    AstNode* asAst = &original;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<SimpleType*>(astCopy.get()), nullptr);
    AstType* asType = &original;
    std::unique_ptr<AstType> typeCopy(asType->Clone());
    ASSERT_NE(typeCopy, nullptr);
    EXPECT_NE(dynamic_cast<SimpleType*>(typeCopy.get()), nullptr);
}

// `Clone` of a nameless `SimpleType` with no type arguments yields a nameless clone with no
// type arguments (the token is null, the collection is empty).
TEST(CSharp_SimpleType, CloneOfNamelessEmptyIsNamelessEmpty) {
    SimpleType original;
    std::unique_ptr<SimpleType> copy(original.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_FALSE(copy->Identifier().has_value());
    EXPECT_EQ(copy->IdentifierToken(), nullptr);
    EXPECT_EQ(copy->TypeArguments().Count(), 0);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// A `SimpleType` with the token set and type arguments passes the inherited `CheckInvariant`
// (the slot-structure verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in
// debug builds (a no-op in NDEBUG).
TEST(CSharp_SimpleType, CheckInvariantPassesOnFilledNode) {
    SimpleType st(std::string("List"), TextLocation(1, 1));
    auto i = std::make_unique<PrimitiveType>(std::string("int"));
    st.TypeArguments().Add(i.get());
    st.CheckInvariant();
}

// A `SimpleType` with an OPTIONAL (absent) name still passes `CheckInvariant` (the token slot
// is nullable, so an absent name is not a required-slot violation).
TEST(CSharp_SimpleType, CheckInvariantPassesOnNamelessNode) {
    SimpleType st;  // no token, no type arguments
    st.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `IdentifierTokenSlot` and `TypeArgumentsSlot` are distinct slot statics (compared by
// address); the node's `Slot()` reports the `IdentifierTokenSlot` for the token. The two slot
// statics have different `CSharpSlotInfoT<T>` element types, so they are compared through the
// common `CSharpSlotInfo` base (the pointer-identity comparison the slot system uses).
TEST(CSharp_SimpleType, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&SimpleType::IdentifierTokenSlot),
              static_cast<const CSharpSlotInfo*>(&SimpleType::TypeArgumentsSlot));
    SimpleType st(std::string("Foo"));
    EXPECT_EQ(st.IdentifierToken()->Slot(), &SimpleType::IdentifierTokenSlot);
    EXPECT_EQ(st.IdentifierToken()->Slot()->Kind(), &Slots::Identifier);
}
