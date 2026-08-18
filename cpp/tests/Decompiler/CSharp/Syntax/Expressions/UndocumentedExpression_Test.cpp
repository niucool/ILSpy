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

// Tests for the `UndocumentedExpression` concrete node (cpp/.../Syntax/Expressions/
// UndocumentedExpression.hpp, the port of
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.cs) -- the
// `ConstructorInitializer` D281 collection-only-plus-enum-scalar shape applied to the
// `Expression` hierarchy (the `undocumented_expression ::= '__arglist' | '__arglist' '('
// expression* ')' | '__refvalue' '(' expression ',' type ')' | '__reftype' '(' expression ')' |
// '__makeref' '(' expression ')'` production). A sealed `Expression` carrying a
// `UndocumentedExpressionType` scalar (the kind of undocumented expression, no `Any` member so
// the `DoMatch` term is the plain `==`) and an `Arguments` `AstNodeCollection<Expression>`
// collection. Exercises the enum scalar, the const keyword strings, the `Arguments` collection,
// the collection-only slot-storage contract (an empty node reports `GetChildCount` 0), the
// `AcceptVisitor` dispatch, the generated `DoMatch` (a plain-`==` term + a collection `DoMatch`
// term), the per-concrete-node `Clone`, and the inherited `CheckInvariant` (which passes on the
// empty node -- there are no required single slots).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TupleExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
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
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitUndocumentedExpression(UndocumentedExpression* node) override {
        if (node == nullptr) { trace.push_back("<null>"); return; }
        trace.push_back("undoc:" + std::to_string(static_cast<int>(node->UndocumentedExpressionType())));
        VisitChildren(node);
    }
    void VisitPrimitiveExpression(PrimitiveExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim");
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
    void VisitTupleExpression(TupleExpression* node) override {
        if (node == nullptr) { trace.push_back("<null-tuple>"); return; }
        trace.push_back("tuple");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping an `UndocumentedExpression` and its `Arguments` alive in the test scope (the
// port's non-owning raw-pointer child slots -- the D223 design).
struct UdeHolder {
    std::unique_ptr<UndocumentedExpression> ude;
    std::vector<std::unique_ptr<PrimitiveExpression>> args;
    UndocumentedExpression* get() const { return ude.get(); }
    UndocumentedExpression* operator->() const { return ude.get(); }
};

// Build an `UndocumentedExpression` with `UndocumentedExpressionType::ArgList` and one argument
// `PrimitiveExpression` `42` (the `__arglist(42)` form).
UdeHolder make_UndocumentedArgListWithArg() {
    UdeHolder h;
    h.ude = std::make_unique<UndocumentedExpression>(UndocumentedExpressionType::ArgList);
    h.args.push_back(std::make_unique<PrimitiveExpression>(int32_t(42)));
    h.ude->Arguments().Add(h.args[0].get());
    return h;
}

} // namespace

// ---- Is-a --------------------------------------------------------------

// `UndocumentedExpression` is an `Expression` and an `AstNode` (the `dynamic_cast` is-a the slot
// system and the annotation channel use); it is NOT an `AstType` (it derives from `Expression`,
// parallel to -- not under -- `AstType`).
TEST(CSharp_UndocumentedExpression, IsExpressionAndAstNodeNotAstType) {
    UndocumentedExpression ude;
    EXPECT_NE(dynamic_cast<Expression*>(&ude), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&ude), nullptr);
    EXPECT_EQ(dynamic_cast<AstType*>(&ude), nullptr);
}

// `UndocumentedExpression` is a concrete (non-abstract) and `final` class (the C# is `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it). It is constructible directly.
TEST(CSharp_UndocumentedExpression, IsConcreteAndFinal) {
    auto ude = std::make_unique<UndocumentedExpression>();
    ASSERT_NE(ude, nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(ude.get()), nullptr);
    EXPECT_TRUE(std::is_final_v<UndocumentedExpression>);
}

// ---- The `UndocumentedExpressionType` enum ---------------------------------

// The enum values are in C# declaration order; `ArgListAccess` is the zero value (the C# default
// -- the bare `__arglist` form). The underlying type is the default (the C# default `int`).
TEST(CSharp_UndocumentedExpression, UndocumentedExpressionTypeEnumValues) {
    EXPECT_EQ(static_cast<int>(UndocumentedExpressionType::ArgListAccess), 0);
    EXPECT_EQ(static_cast<int>(UndocumentedExpressionType::ArgList), 1);
    EXPECT_EQ(static_cast<int>(UndocumentedExpressionType::RefValue), 2);
    EXPECT_EQ(static_cast<int>(UndocumentedExpressionType::RefType), 3);
    EXPECT_EQ(static_cast<int>(UndocumentedExpressionType::MakeRef), 4);
}

// ---- The const keyword strings ------------------------------------------

// The four keyword token literals the output visitor emits are part of the node's public API.
TEST(CSharp_UndocumentedExpression, KeywordConsts) {
    EXPECT_STREQ(UndocumentedExpression::ArglistKeyword, "__arglist");
    EXPECT_STREQ(UndocumentedExpression::RefvalueKeyword, "__refvalue");
    EXPECT_STREQ(UndocumentedExpression::ReftypeKeyword, "__reftype");
    EXPECT_STREQ(UndocumentedExpression::MakerefKeyword, "__makeref");
}

// ---- Construction -----------------------------------------------------

// The empty ctor defaults `UndocumentedExpressionType` to `ArgListAccess` (the enum's zero
// value, the bare `__arglist` form) and has no arguments. `GetChildCount` is 0 (the node has no
// single slots -- the only slot is the `Arguments` collection, which is empty).
TEST(CSharp_UndocumentedExpression, EmptyCtor) {
    UndocumentedExpression ude;
    EXPECT_EQ(ude.UndocumentedExpressionType(), UndocumentedExpressionType::ArgListAccess);
    EXPECT_EQ(ude.Arguments().Count(), 0);
    EXPECT_EQ(ude.GetChildCount(), 0);
}

// The explicit `(UndocumentedExpressionType)` ctor sets the scalar.
TEST(CSharp_UndocumentedExpression, ExplicitCtorSetsScalar) {
    UndocumentedExpression ude(UndocumentedExpressionType::RefValue);
    EXPECT_EQ(ude.UndocumentedExpressionType(), UndocumentedExpressionType::RefValue);
}

// ---- The `UndocumentedExpressionType` scalar --------------------------------

// The scalar setter round-trips each value.
TEST(CSharp_UndocumentedExpression, ScalarRoundTrips) {
    UndocumentedExpression ude;
    ude.UndocumentedExpressionType(UndocumentedExpressionType::MakeRef);
    EXPECT_EQ(ude.UndocumentedExpressionType(), UndocumentedExpressionType::MakeRef);
    ude.UndocumentedExpressionType(UndocumentedExpressionType::ArgListAccess);
    EXPECT_EQ(ude.UndocumentedExpressionType(), UndocumentedExpressionType::ArgListAccess);
    ude.UndocumentedExpressionType(UndocumentedExpressionType::RefType);
    EXPECT_EQ(ude.UndocumentedExpressionType(), UndocumentedExpressionType::RefType);
}

// ---- The `Arguments` collection ----------------------------------------

// `Add` appends an element, parents it, and assigns its flattened `ChildIndex` incrementally
// (the collection is the node's only collection at its last/only slot, so an element's index is
// its local position -- baseIndex 0).
TEST(CSharp_UndocumentedExpression, ArgumentsAddAppendsAndParentsIncremental) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto y = std::make_unique<PrimitiveExpression>(int32_t(2));
    ude.Arguments().Add(x.get());
    ude.Arguments().Add(y.get());
    EXPECT_EQ(ude.Arguments().Count(), 2);
    EXPECT_EQ(x->Parent(), &ude);
    EXPECT_EQ(y->Parent(), &ude);
    EXPECT_EQ(x->ChildIndex, 0);  // baseIndex 0 + 0
    EXPECT_EQ(y->ChildIndex, 1);  // baseIndex 0 + 1
    EXPECT_EQ(ude.GetChildCount(), 2);  // 2 elements, no single slots
    EXPECT_TRUE(ude.ChildIndicesValid());
}

// `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind (the shared
// `Slots::Argument`, reused as the collection kind) and null for any other kind.
TEST(CSharp_UndocumentedExpression, GetCollectionByKindReturnsArgumentsForArgumentKind) {
    UndocumentedExpression ude;
    EXPECT_NE(ude.GetCollectionByKind(&Slots::Argument), nullptr);
    EXPECT_EQ(ude.GetCollectionByKind(&Slots::Argument), &ude.Arguments());
    EXPECT_EQ(ude.GetCollectionByKind(&Slots::Expression), nullptr);  // a different kind
    EXPECT_EQ(ude.GetCollectionByKind(nullptr), nullptr);
}

// ---- Slot-storage contract (the collection-only dispatch) -----------

// `GetChild` returns the arguments at index 0..Count; the collection occupies the contiguous range
// [0, Count). An empty node throws for any index (there are no slots at all).
TEST(CSharp_UndocumentedExpression, GetChildDispatchesArguments) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto y = std::make_unique<PrimitiveExpression>(int32_t(2));
    ude.Arguments().Add(x.get());
    ude.Arguments().Add(y.get());
    EXPECT_EQ(ude.GetChild(0), x.get());
    EXPECT_EQ(ude.GetChild(1), y.get());
    EXPECT_THROW(ude.GetChild(2), std::out_of_range);
    EXPECT_THROW(ude.GetChild(-1), std::out_of_range);
}

// An empty node throws for every index (no single slots, no elements).
TEST(CSharp_UndocumentedExpression, GetChildThrowsOnEmptyNode) {
    UndocumentedExpression ude;
    EXPECT_THROW(ude.GetChild(0), std::out_of_range);
}

// `GetChildSlotInfo` returns the `ArgumentsSlot` at index 0..Count (the slot identity the slot
// system compares by address). `GetChildSlotInfo(0)` points at the `Argument` KIND (the shared
// `Slots.Argument`, reused as the collection kind).
TEST(CSharp_UndocumentedExpression, GetChildSlotInfoDispatchesArguments) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    ude.Arguments().Add(x.get());
    EXPECT_EQ(ude.GetChildSlotInfo(0), &UndocumentedExpression::ArgumentsSlot);
    EXPECT_EQ(ude.GetChildSlotInfo(0)->Kind(), &Slots::Argument);
    EXPECT_THROW(ude.GetChildSlotInfo(1), std::out_of_range);
}

// `SetChild` replaces an argument in place at index 0..Count (the collection's `SetAt` re-parents
// and carries the old index).
TEST(CSharp_UndocumentedExpression, SetChildReplacesArgument) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    ude.Arguments().Add(x.get());
    auto y = std::make_unique<PrimitiveExpression>(int32_t(2));
    ude.SetChild(0, y.get());
    EXPECT_EQ(ude.Arguments().At(0), y.get());
    EXPECT_EQ(y->Parent(), &ude);
    EXPECT_EQ(x->Parent(), nullptr);  // detached by SetAt
    EXPECT_THROW(ude.SetChild(1, nullptr), std::out_of_range);  // no element at index 1
}

// ---- AcceptVisitor dispatch ------------------------------------------

// `AcceptVisitor` dispatches to `VisitUndocumentedExpression` (the visitor-pattern round-trip);
// the depth-first walk then visits the `Arguments` children (a `VisitPrimitiveExpression` per
// element).
TEST(CSharp_UndocumentedExpression, AcceptVisitorDispatchesToVisitUndocumentedExpression) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    ude.Arguments().Add(x.get());
    RecordingVisitor v;
    ude.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"undoc:0", "prim"}));  // ArgListAccess = 0
}

// `AcceptVisitor` is virtual through an `AstNode*` / an `Expression*` (the dynamic dispatch the
// output visitor relies on).
TEST(CSharp_UndocumentedExpression, AcceptVisitorIsVirtualThroughBases) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    ude.Arguments().Add(x.get());
    AstNode* asAst = &ude;
    Expression* asExpr = &ude;
    RecordingVisitor v1, v2;
    asAst->AcceptVisitor(v1);
    asExpr->AcceptVisitor(v2);
    EXPECT_EQ(v1.trace, (std::vector<std::string>{"undoc:0", "prim"}));
    EXPECT_EQ(v2.trace, (std::vector<std::string>{"undoc:0", "prim"}));
}

// ---- Depth-first walk ------------------------------------------------

// The depth-first walk visits each `Arguments` child (a `VisitPrimitiveExpression`, a leaf) in
// document order. An empty node records just the node itself.
TEST(CSharp_UndocumentedExpression, DepthFirstWalkVisitsArgumentsInOrder) {
    UndocumentedExpression ude(UndocumentedExpressionType::MakeRef);
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto y = std::make_unique<PrimitiveExpression>(int32_t(2));
    ude.Arguments().Add(x.get());
    ude.Arguments().Add(y.get());
    RecordingVisitor v;
    ude.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{
        "undoc:4", "prim", "prim"}));  // MakeRef = 4
}

// The depth-first walk of an empty node records just the node itself (no children).
TEST(CSharp_UndocumentedExpression, DepthFirstWalkOfEmptyRecordsJustNode) {
    UndocumentedExpression ude;
    RecordingVisitor v;
    ude.AcceptVisitor(v);
    EXPECT_EQ(v.trace, (std::vector<std::string>{"undoc:0"}));
}

// ---- DoMatch (the generated plain-== + collection match) ------------------------

// Two `UndocumentedExpression`s with the same scalar and same (single) argument match.
TEST(CSharp_UndocumentedExpression, DoMatchSameNode) {
    auto a = make_UndocumentedArgListWithArg();
    auto b = make_UndocumentedArgListWithArg();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// Two empty `UndocumentedExpression`s with the same default scalar match (the collection match
// succeeds when both counts are 0, and the plain-`==` on the default `ArgListAccess` is true).
TEST(CSharp_UndocumentedExpression, DoMatchMatchesTwoEmpties) {
    UndocumentedExpression a;
    UndocumentedExpression b;
    EXPECT_TRUE(DoMatchAgainst(&a, &b));
}

// A `UndocumentedExpressionType` mismatch rejects (the plain `==` term -- NO `Any` wildcard, so
// a different scalar value rejects, unlike `ConstructorInitializer` whose `Any` is the wildcard).
TEST(CSharp_UndocumentedExpression, DoMatchScalarMismatchRejects) {
    UndocumentedExpression a(UndocumentedExpressionType::ArgList);
    UndocumentedExpression b(UndocumentedExpressionType::RefValue);
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
    EXPECT_FALSE(DoMatchAgainst(&b, &a));
}

// A `UndocumentedExpressionType::ArgListAccess` does NOT match a `RefValue` candidate (the plain
// `==` -- no wildcard). Confirms the enum has NO `Any` member (unlike the `ConstructorInitializer`
// D281 `Any`-wildcard); the zero value `ArgListAccess` is a real kind, not a wildcard.
TEST(CSharp_UndocumentedExpression, DoMatchZeroValueIsNotWildcard) {
    UndocumentedExpression a;  // ArgListAccess (zero value), no args
    UndocumentedExpression b(UndocumentedExpressionType::MakeRef);  // MakeRef, no args
    EXPECT_FALSE(DoMatchAgainst(&a, &b));
}

// An `Arguments` mismatch rejects (the collection recursive `DoMatch` -- a length mismatch).
TEST(CSharp_UndocumentedExpression, DoMatchArgumentsMismatchRejects) {
    auto a = make_UndocumentedArgListWithArg();  // one arg
    UndocumentedExpression b(UndocumentedExpressionType::ArgList);  // no args
    EXPECT_FALSE(DoMatchAgainst(a.get(), &b));
    EXPECT_FALSE(DoMatchAgainst(&b, a.get()));
}

// Both sides with the same scalar and same single argument match (the collection recursive
// `DoMatch` accepts equal-length collections whose elements match).
TEST(CSharp_UndocumentedExpression, DoMatchSameArgumentsMatch) {
    auto a = make_UndocumentedArgListWithArg();
    auto b = make_UndocumentedArgListWithArg();
    EXPECT_TRUE(DoMatchAgainst(a.get(), b.get()));
}

// A `UndocumentedExpression` does not match a `TupleExpression` candidate (the two both hold an
// `Expression` collection but are distinct concrete types, so the type-check gate rejects).
TEST(CSharp_UndocumentedExpression, DoMatchRejectsTupleExpressionCandidate) {
    UndocumentedExpression ude;
    TupleExpression te;
    EXPECT_FALSE(DoMatchAgainst(&ude, &te));
    EXPECT_FALSE(DoMatchAgainst(&te, &ude));
}

// A `UndocumentedExpression` does not match an `IdentifierExpression` candidate (the type-check
// gate rejects).
TEST(CSharp_UndocumentedExpression, DoMatchRejectsIdentifierExpressionCandidate) {
    UndocumentedExpression ude;
    IdentifierExpression idexpr(std::string("x"));
    EXPECT_FALSE(DoMatchAgainst(&ude, &idexpr));
    EXPECT_FALSE(DoMatchAgainst(&idexpr, &ude));
}

// A null candidate is rejected (`dynamic_cast` of null is null). The null-pattern case is a
// caller error (a null `this` derefs in C++/NREs in C#), so it is not tested (the D298 precedent).
TEST(CSharp_UndocumentedExpression, DoMatchRejectsNullCandidate) {
    UndocumentedExpression ude;
    EXPECT_FALSE(DoMatchAgainst(&ude, nullptr));
}

// ---- Clone (the concrete override) -----------------------------------

// `Clone` deep-copies the `Arguments`, copies the `UndocumentedExpressionType` scalar, and does
// not detach the source.
TEST(CSharp_UndocumentedExpression, CloneDeepCopies) {
    auto a = make_UndocumentedArgListWithArg();
    auto clone = std::unique_ptr<UndocumentedExpression>(
        static_cast<UndocumentedExpression*>(a->Clone()));
    EXPECT_EQ(clone->UndocumentedExpressionType(), UndocumentedExpressionType::ArgList);
    ASSERT_EQ(clone->Arguments().Count(), 1u);
    EXPECT_NE(clone->Arguments().At(0), a->Arguments().At(0));
    EXPECT_EQ(clone->Arguments().At(0)->Parent(), clone.get());
    // The source is not detached.
    EXPECT_EQ(a->Arguments().At(0)->Parent(), a.get());
}

// `Clone` is virtual through `AstNode*` / `Expression*` and covariant through
// `UndocumentedExpression*`.
TEST(CSharp_UndocumentedExpression, CloneVirtualAndCovariant) {
    UndocumentedExpression a;
    AstNode* asAst = &a;
    std::unique_ptr<AstNode> astCopy(asAst->Clone());
    ASSERT_NE(astCopy, nullptr);
    EXPECT_NE(dynamic_cast<UndocumentedExpression*>(astCopy.get()), nullptr);
    Expression* asExpr = &a;
    std::unique_ptr<Expression> exprCopy(asExpr->Clone());
    ASSERT_NE(exprCopy, nullptr);
    EXPECT_NE(dynamic_cast<UndocumentedExpression*>(exprCopy.get()), nullptr);
    auto cov = std::unique_ptr<UndocumentedExpression>(a.Clone());
    ASSERT_NE(cov, nullptr);
}

// `Clone` copies the scalar (the `UndocumentedExpressionType`).
TEST(CSharp_UndocumentedExpression, CloneCopiesScalar) {
    UndocumentedExpression a(UndocumentedExpressionType::RefType);
    auto clone = std::unique_ptr<UndocumentedExpression>(
        static_cast<UndocumentedExpression*>(a.Clone()));
    EXPECT_EQ(clone->UndocumentedExpressionType(), UndocumentedExpressionType::RefType);
}

// `Clone` of an empty `UndocumentedExpression` yields an empty clone (the default scalar copied).
TEST(CSharp_UndocumentedExpression, CloneOfEmptyIsEmpty) {
    UndocumentedExpression a;
    std::unique_ptr<UndocumentedExpression> copy(a.Clone());
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->Arguments().Count(), 0);
    EXPECT_EQ(copy->GetChildCount(), 0);
    EXPECT_EQ(copy->UndocumentedExpressionType(), UndocumentedExpressionType::ArgListAccess);
}

// ---- CheckInvariant (inherited from AstNode) ------------------------

// An `UndocumentedExpression` with arguments passes the inherited `CheckInvariant` (the
// slot-structure verifier: every slot's `Parent`/`ChildIndex`/type consistent). Runs in debug
// builds (a no-op in NDEBUG).
TEST(CSharp_UndocumentedExpression, CheckInvariantPassesOnFilledNode) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    auto y = std::make_unique<PrimitiveExpression>(int32_t(2));
    ude.Arguments().Add(x.get());
    ude.Arguments().Add(y.get());
    ude.CheckInvariant();
}

// An EMPTY `UndocumentedExpression` passes `CheckInvariant` (the node has no required single
// slots -- the only slot is the `Arguments` collection, which has no required-slot invariant),
// the collection-only precedent where the empty node is invariant-valid.
TEST(CSharp_UndocumentedExpression, CheckInvariantPassesOnEmptyNode) {
    UndocumentedExpression ude;
    ude.CheckInvariant();
}

// ---- Slot identity ----------------------------------------------------

// The `ArgumentsSlot` is the node's only slot static; the node's `Slot()` reports it for each
// child. The slot's `Kind` is the shared `Slots::Argument` (reused as the collection kind).
TEST(CSharp_UndocumentedExpression, ArgumentsSlotIsArgumentKind) {
    UndocumentedExpression ude;
    auto x = std::make_unique<PrimitiveExpression>(int32_t(1));
    ude.Arguments().Add(x.get());
    EXPECT_EQ(ude.Arguments().At(0)->Slot(), &UndocumentedExpression::ArgumentsSlot);
    EXPECT_EQ(ude.Arguments().At(0)->Slot()->Kind(), &Slots::Argument);
    EXPECT_TRUE(ude.Arguments().At(0)->Slot()->IsCollection());
}
