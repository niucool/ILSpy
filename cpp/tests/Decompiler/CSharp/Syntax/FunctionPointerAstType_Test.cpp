// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `FunctionPointerAstType` concrete node (cpp/.../Syntax/FunctionPointerAstType.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/FunctionPointerAstType.cs) -- the
// `funcptr_type ::= 'delegate' '*' calling_convention_specifier? funcptr_parameter_list
// funcptr_return_type` (C# grammar 24.3.3). The next in-order Phase-5 piece per the D313 plan (the
// remaining GeneralScope `AstType`-bearing nodes). The `ArrayCreateExpression` D252 two-collection
// shape with the trailing single REQUIRED and no leading single -- the first ported `AstType` with
// two collections both followed by a required single. The suite shares a `RecordingVisitor`, a
// `DoMatchAgainst` helper, and a `makeFuncPtr` holder (the D234 pattern).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/FunctionPointerAstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/InvocationAstType.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;

namespace {

// A recording depth-first visitor: overrides the `FunctionPointerAstType` under test (plus the
// `SimpleType`/`Identifier`/`PrimitiveType`/`ParameterDeclaration` of its children, and the
// `InvocationAstType` used for the cross-structural-twin DoMatch rejection), recording a tag and
// recursing via `VisitChildren` (the inherited depth-first default). The trace is the visited
// nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitFunctionPointerType(FunctionPointerAstType* node) override {
        if (node == nullptr) { trace.push_back("<null-fptr>"); return; }
        trace.push_back("fptr");
        VisitChildren(node);
    }
    void VisitSimpleType(SimpleType* node) override {
        if (node == nullptr) { trace.push_back("<null-simple>"); return; }
        trace.push_back("simple:" + node->Identifier().value_or(""));
        VisitChildren(node);
    }
    void VisitIdentifier(Identifier* node) override {
        if (node == nullptr) { trace.push_back("<null-id>"); return; }
        trace.push_back("id:" + node->Name());
        VisitChildren(node);
    }
    void VisitPrimitiveType(PrimitiveType* node) override {
        if (node == nullptr) { trace.push_back("<null-prim>"); return; }
        trace.push_back("prim:" + node->Keyword());
        VisitChildren(node);
    }
    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-pd>"); return; }
        trace.push_back("pd");
        VisitChildren(node);
    }
    void VisitInvocationType(InvocationAstType* node) override {
        if (node == nullptr) { trace.push_back("<null-inv>"); return; }
        trace.push_back("inv");
        VisitChildren(node);
    }
};

// A `DoMatch` helper: invokes the pattern-matcher dispatch through the `INode` interface (the
// C# `((INode)pattern).DoMatch(candidate, new Match())` -- the port's `DoMatch(INode*, Match)`
// overload delegates to the concrete `DoMatch(AstNode*, Match)`). `Match::CreateNew()` allocates
// the capture vector (a default-constructed `Match` holds a null shared_ptr and is a FAILED
// match -- the D219 `CreateNew`-vs-default-ctor distinction; the collection `DoMatch`'s
// `CheckPoint`/`RestoreCheckPoint` deref the vector).
bool DoMatchAgainst(AstNode* pattern, AstNode* candidate) {
    return static_cast<INode*>(pattern)->DoMatch(candidate, Match::CreateNew());
}

// A holder keeping a `FunctionPointerAstType` and its `SimpleType` `ReturnType` + its
// `CallingConventions` `AstType` elements + its `Parameters` `ParameterDeclaration` elements
// alive in the test scope (the D241 non-owning-raw-pointer model -- the parent does not own its
// children; the holder's `unique_ptr`s do).
struct FuncPtrHolder {
    std::unique_ptr<SimpleType> returnType;
    std::vector<std::unique_ptr<PrimitiveType>> callingConventions;
    std::vector<std::unique_ptr<ParameterDeclaration>> parameters;
    std::unique_ptr<FunctionPointerAstType> fptr;
};

// Builds a `FunctionPointerAstType` with a `SimpleType` `ReturnType` of the given name, the given
// number of `PrimitiveType` calling conventions, and the given number of `ParameterDeclaration`
// parameters (a fresh element per slot -- the D235 single-parent guard: a child can have only one
// parent, so each element is its own `unique_ptr`).
FuncPtrHolder makeFuncPtr(std::string returnName, int ccCount, int paramCount) {
    FuncPtrHolder h;
    h.fptr = std::make_unique<FunctionPointerAstType>();
    h.returnType = std::make_unique<SimpleType>(returnName);
    h.fptr->ReturnType(h.returnType.get());
    for (int i = 0; i < ccCount; i++) {
        h.callingConventions.push_back(std::make_unique<PrimitiveType>("unmanaged"));
        h.fptr->CallingConventions().Add(h.callingConventions.back().get());
    }
    for (int i = 0; i < paramCount; i++) {
        h.parameters.push_back(std::make_unique<ParameterDeclaration>());
        h.fptr->Parameters().Add(h.parameters.back().get());
    }
    return h;
}

} // namespace

// ===========================================================================
// FunctionPointerAstType
// ===========================================================================

// `FunctionPointerAstType` is an `AstType` and an `AstNode`; it is disjoint from `Expression`/
// `EntityDeclaration` (the `SimpleType` D237 / `PrimitiveType` D236 / `InvocationAstType` D313
// concrete-`AstType` precedent).
TEST(CSharp_FunctionPointerAstType, IsAstTypeAndAstNodeNotExpression) {
    FunctionPointerAstType fptr;
    EXPECT_TRUE(dynamic_cast<AstType*>(&fptr) != nullptr);
    EXPECT_TRUE(dynamic_cast<AstNode*>(&fptr) != nullptr);
    EXPECT_FALSE(dynamic_cast<Expression*>(&fptr) != nullptr);
    EXPECT_FALSE(dynamic_cast<EntityDeclaration*>(&fptr) != nullptr);
}

// `FunctionPointerAstType` is `final` (the C# `sealed`).
TEST(CSharp_FunctionPointerAstType, IsConcreteAndFinal) {
    EXPECT_TRUE(std::is_final_v<FunctionPointerAstType>);
    EXPECT_FALSE(std::is_abstract_v<FunctionPointerAstType>);
}

// The `PointerToken` const is part of the node's public API (the output-visitor token literal).
TEST(CSharp_FunctionPointerAstType, PointerTokenConst) {
    EXPECT_STREQ(FunctionPointerAstType::PointerToken, "*");
}

// ---- the `HasUnmanagedCallingConvention` scalar (a NON-`[Slot]` bool) ------

// The bool defaults to `false` (the C# default) and round-trips through the public
// getter/setter.
TEST(CSharp_FunctionPointerAstType, HasUnmanagedCallingConventionScalar) {
    FunctionPointerAstType fptr;
    EXPECT_FALSE(fptr.HasUnmanagedCallingConvention());
    fptr.HasUnmanagedCallingConvention(true);
    EXPECT_TRUE(fptr.HasUnmanagedCallingConvention());
    fptr.HasUnmanagedCallingConvention(false);
    EXPECT_FALSE(fptr.HasUnmanagedCallingConvention());
}

// ---- the generated empty ctor ----------------------------------------------

// The empty ctor leaves `CallingConventions` and `Parameters` empty and `ReturnType` null (a
// REQUIRED slot -- `CheckInvariant` asserts it is filled). `GetChildCount` is 1 (the single
// `ReturnType` slot contributes 1 to the flattened count even when null -- the `Accessor` D274 /
// `InvocationAstType` D313 single-slot-counts-even-when-null precedent).
TEST(CSharp_FunctionPointerAstType, EmptyCtorHasEmptyCollectionsAndNullReturnType) {
    FunctionPointerAstType fptr;
    EXPECT_EQ(fptr.CallingConventions().Count(), 0);
    EXPECT_EQ(fptr.Parameters().Count(), 0);
    EXPECT_EQ(fptr.ReturnType(), nullptr);
    EXPECT_EQ(fptr.GetChildCount(), 1);
}

// ---- the `CallingConventions` collection (NON-incremental) ------------------

// `Add` appends and re-parents. The collection is the node's FIRST collection but NOT the last
// slot (`Parameters` and `ReturnType` trail it), so `supportsIncremental` is FALSE -- a child's
// `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices`. A test asserting `ChildIndex`
// immediately after `Add` sees a stale value; the reindex is triggered by `Slot()` (which calls
// `EnsureChildIndices`), after which each child's `ChildIndex` is its correct flattened index (the
// `ComposedType` D242 / `ArrayCreateExpression` D252 two-collection precedent).
TEST(CSharp_FunctionPointerAstType, CallingConventionsAddAppendsParentsAndReindexes) {
    FunctionPointerAstType fptr;
    auto cc0 = std::make_unique<PrimitiveType>("unmanaged");
    auto cc1 = std::make_unique<PrimitiveType>("managed");
    fptr.CallingConventions().Add(cc0.get());
    fptr.CallingConventions().Add(cc1.get());
    EXPECT_EQ(fptr.CallingConventions().Count(), 2);
    EXPECT_EQ(cc0->Parent(), &fptr);
    EXPECT_EQ(cc1->Parent(), &fptr);
    // Trigger the lazy reindex (GetChild does NOT call EnsureChildIndices; Slot() does).
    (void)cc0->Slot();
    (void)cc1->Slot();
    EXPECT_EQ(cc0->ChildIndex, 0);  // first CallingConventions element -> flattened index 0
    EXPECT_EQ(cc1->ChildIndex, 1);  // second CallingConventions element -> flattened index 1
    EXPECT_EQ(fptr.GetChildCount(), 3);  // 2 calling conventions + 1 (the null ReturnType slot)
}

// `GetCollectionByKind` returns the `CallingConventions` collection for the `CallingConvention`
// kind, the `Parameters` collection for the `Parameter` kind, and null for the `Type` single-slot
// kind and an unrelated collection kind.
TEST(CSharp_FunctionPointerAstType, GetCollectionByKindReturnsCorrectCollections) {
    FunctionPointerAstType fptr;
    EXPECT_EQ(fptr.GetCollectionByKind(&Slots::CallingConvention), &fptr.CallingConventions());
    EXPECT_EQ(fptr.GetCollectionByKind(&Slots::Parameter), &fptr.Parameters());
    EXPECT_EQ(fptr.GetCollectionByKind(&Slots::Type), nullptr);       // a single-slot kind
    EXPECT_EQ(fptr.GetCollectionByKind(&Slots::Argument), nullptr);   // an unrelated collection kind
    EXPECT_EQ(fptr.GetCollectionByKind(nullptr), nullptr);
}

// ---- the `Parameters` collection (NON-incremental) -------------------------

// `Add` appends and re-parents; the flattened `ChildIndex` of a `Parameters` element follows the
// `CallingConventions` range (the dynamic layout `[0, ccCount) + [ccCount, ccCount + paramCount)`
// + the `ReturnType` at `ccCount + paramCount`).
TEST(CSharp_FunctionPointerAstType, ParametersAddAppendsParentsAndReindexes) {
    auto h = makeFuncPtr("Ret", 2, 2);
    EXPECT_EQ(h.fptr->Parameters().Count(), 2);
    EXPECT_EQ(h.parameters[0]->Parent(), &*h.fptr);
    EXPECT_EQ(h.parameters[1]->Parent(), &*h.fptr);
    // Trigger the lazy reindex (Slot() calls EnsureChildIndices on the parent).
    (void)h.callingConventions[0]->Slot();
    (void)h.callingConventions[1]->Slot();
    (void)h.parameters[0]->Slot();
    (void)h.parameters[1]->Slot();
    EXPECT_EQ(h.callingConventions[0]->ChildIndex, 0);
    EXPECT_EQ(h.callingConventions[1]->ChildIndex, 1);
    EXPECT_EQ(h.parameters[0]->ChildIndex, 2);  // after 2 CallingConventions
    EXPECT_EQ(h.parameters[1]->ChildIndex, 3);
    EXPECT_EQ(h.fptr->GetChildCount(), 5);  // 2 + 2 + 1 (ReturnType)
}

// ---- the `ReturnType` slot (a REQUIRED single `AstType`) --------------------

// The index-less setter re-parents and detaches the previous child (the dynamic flattened index
// after two collections).
TEST(CSharp_FunctionPointerAstType, ReturnTypeSetterReparentsAndDetaches) {
    FunctionPointerAstType fptr;
    auto a = std::make_unique<SimpleType>("A");
    fptr.ReturnType(a.get());
    EXPECT_EQ(fptr.ReturnType(), a.get());
    EXPECT_EQ(a->Parent(), &fptr);
    auto b = std::make_unique<SimpleType>("B");
    fptr.ReturnType(b.get());
    EXPECT_EQ(fptr.ReturnType(), b.get());
    EXPECT_EQ(b->Parent(), &fptr);
    EXPECT_EQ(a->Parent(), nullptr);  // detached by SetChildNode
}

// The index-less setter clears with null.
TEST(CSharp_FunctionPointerAstType, ReturnTypeSetterClearsWithNull) {
    FunctionPointerAstType fptr;
    auto t = std::make_unique<SimpleType>("T");
    fptr.ReturnType(t.get());
    fptr.ReturnType(nullptr);
    EXPECT_EQ(fptr.ReturnType(), nullptr);
    EXPECT_EQ(t->Parent(), nullptr);
}

// ---- collection-aware slot storage ----------------------------------------

// `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
// running index: a collection step (the `CallingConventions` range `[0, ccCount)`), a collection
// step (the `Parameters` range `[ccCount, ccCount + paramCount)`), then a single step (the
// `ReturnType` at index `ccCount + paramCount`). `GetChildCount` is `2 + ccCount + paramCount` (the
// single slot contributes 1 even when null).
TEST(CSharp_FunctionPointerAstType, CollectionAwareSlotStorage) {
    auto h = makeFuncPtr("Ret", 2, 1);
    EXPECT_EQ(h.fptr->GetChildCount(), 4);  // 2 CallingConventions + 1 Parameter + 1 ReturnType
    EXPECT_EQ(h.fptr->GetChild(0), h.callingConventions[0].get());
    EXPECT_EQ(h.fptr->GetChild(1), h.callingConventions[1].get());
    EXPECT_EQ(h.fptr->GetChild(2), h.parameters[0].get());
    EXPECT_EQ(h.fptr->GetChild(3), h.returnType.get());
    EXPECT_EQ(h.fptr->GetChildSlotInfo(0), &h.fptr->CallingConventionsSlot);
    EXPECT_EQ(h.fptr->GetChildSlotInfo(1), &h.fptr->CallingConventionsSlot);
    EXPECT_EQ(h.fptr->GetChildSlotInfo(2), &h.fptr->ParametersSlot);
    EXPECT_EQ(h.fptr->GetChildSlotInfo(3), &h.fptr->ReturnTypeSlot);
    EXPECT_THROW(h.fptr->GetChild(4), std::out_of_range);
    EXPECT_THROW(h.fptr->GetChildSlotInfo(4), std::out_of_range);
}

// ---- shared `Slots` kind identity -----------------------------------------

TEST(CSharp_FunctionPointerAstType, SlotStaticsPointAtSharedSlotsKinds) {
    EXPECT_EQ(FunctionPointerAstType::CallingConventionsSlot.Kind(), &Slots::CallingConvention);
    EXPECT_EQ(FunctionPointerAstType::ParametersSlot.Kind(), &Slots::Parameter);
    EXPECT_EQ(FunctionPointerAstType::ReturnTypeSlot.Kind(), &Slots::Type);
}

// `IsInstanceOfType` is-a cross-check: the `CallingConventionsSlot`/`ReturnTypeSlot` (both
// `CSharpSlotInfoT<AstType>`) accept an `AstType` (a `SimpleType`) but not an `Expression`-typed
// node; the `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>`) accepts a
// `ParameterDeclaration` but not an `AstType`.
TEST(CSharp_FunctionPointerAstType, SlotKindIsInstanceOfTypeCrossCheck) {
    auto st = std::make_unique<SimpleType>("T");
    auto pd = std::make_unique<ParameterDeclaration>();
    EXPECT_TRUE(FunctionPointerAstType::CallingConventionsSlot.IsInstanceOfType(st.get()));
    EXPECT_FALSE(FunctionPointerAstType::CallingConventionsSlot.IsInstanceOfType(pd.get()));
    EXPECT_FALSE(FunctionPointerAstType::ParametersSlot.IsInstanceOfType(st.get()));
    EXPECT_TRUE(FunctionPointerAstType::ParametersSlot.IsInstanceOfType(pd.get()));
    EXPECT_TRUE(FunctionPointerAstType::ReturnTypeSlot.IsInstanceOfType(st.get()));
    EXPECT_FALSE(FunctionPointerAstType::ReturnTypeSlot.IsInstanceOfType(pd.get()));
}

// ---- AcceptVisitor dispatch + virtuality ----------------------------------

// A default-constructed (empty) node records just itself (an empty node's `Children()` is empty
// since `FirstChild`/`NextSibling` skip the empty collections and the null `ReturnType`).
TEST(CSharp_FunctionPointerAstType, AcceptVisitorDispatchRecordsTag) {
    FunctionPointerAstType fptr;
    RecordingVisitor v;
    fptr.AcceptVisitor(v);
    EXPECT_EQ(v.trace, std::vector<std::string>({"fptr"}));
}

TEST(CSharp_FunctionPointerAstType, AcceptVisitorVirtualThroughAstType) {
    auto h = makeFuncPtr("Ret", 1, 1);
    RecordingVisitor v;
    AstType* node = h.fptr.get();
    node->AcceptVisitor(v);
    // fptr -> prim:unmanaged (CallingConvention 0) -> pd (Parameter 0) -> simple:Ret (ReturnType)
    // -> id:Ret (the SimpleType recurses into its IdentifierToken). The collections are visited
    // first (slot 0, then slot 1), then the single (slot 2) -- the source declaration order.
    EXPECT_EQ(v.trace, std::vector<std::string>({"fptr", "prim:unmanaged", "pd", "simple:Ret", "id:Ret"}));
}

// ---- DoMatch ---------------------------------------------------------------

TEST(CSharp_FunctionPointerAstType, DoMatchSame) {
    auto a = makeFuncPtr("Ret", 2, 1);
    auto b = makeFuncPtr("Ret", 2, 1);
    EXPECT_TRUE(DoMatchAgainst(a.fptr.get(), b.fptr.get()));
}

// Different `CallingConventions` count rejects (the collection-`DoMatch` term runs and rejects on
// a length mismatch).
TEST(CSharp_FunctionPointerAstType, DoMatchDifferentCallingConventionsCountRejects) {
    auto a = makeFuncPtr("Ret", 2, 1);
    auto b = makeFuncPtr("Ret", 1, 1);
    EXPECT_FALSE(DoMatchAgainst(a.fptr.get(), b.fptr.get()));
}

// Different `Parameters` count rejects.
TEST(CSharp_FunctionPointerAstType, DoMatchDifferentParametersCountRejects) {
    auto a = makeFuncPtr("Ret", 1, 2);
    auto b = makeFuncPtr("Ret", 1, 1);
    EXPECT_FALSE(DoMatchAgainst(a.fptr.get(), b.fptr.get()));
}

// Different `ReturnType` name rejects (the `MatchRequired` term delegates to
// `SimpleType::DoMatch`, which compares the names via `MatchString`).
TEST(CSharp_FunctionPointerAstType, DoMatchDifferentReturnTypeRejects) {
    auto a = makeFuncPtr("Foo", 1, 1);
    auto b = makeFuncPtr("Bar", 1, 1);
    EXPECT_FALSE(DoMatchAgainst(a.fptr.get(), b.fptr.get()));
}

// Different `HasUnmanagedCallingConvention` rejects (the plain-bool term is FIRST in the
// `DoMatch`, so a mismatch rejects before any collection/`ReturnType` term is reached).
TEST(CSharp_FunctionPointerAstType, DoMatchDifferentHasUnmanagedCallingConventionRejects) {
    auto a = makeFuncPtr("Ret", 1, 1);
    auto b = makeFuncPtr("Ret", 1, 1);
    a.fptr->HasUnmanagedCallingConvention(true);
    EXPECT_FALSE(DoMatchAgainst(a.fptr.get(), b.fptr.get()));
    EXPECT_FALSE(DoMatchAgainst(b.fptr.get(), a.fptr.get()));
}

// A cross-structural-twin rejection (vs an `InvocationAstType`, the
// collection-plus-required-trailing-single structural twin across disjoint concrete types --
// `FunctionPointerAstType` has two collections, `InvocationAstType` has one).
TEST(CSharp_FunctionPointerAstType, DoMatchCrossStructuralTwinRejects) {
    auto h = makeFuncPtr("Ret", 1, 1);
    auto inv = std::make_unique<InvocationAstType>();
    auto invBase = std::make_unique<SimpleType>("Foo");
    inv->BaseType(invBase.get());
    EXPECT_FALSE(DoMatchAgainst(h.fptr.get(), inv.get()));
    EXPECT_FALSE(DoMatchAgainst(inv.get(), h.fptr.get()));
}

// A cross-hierarchy rejection (vs a `SimpleType`, a leaf `AstType`).
TEST(CSharp_FunctionPointerAstType, DoMatchCrossTypeRejects) {
    auto h = makeFuncPtr("Ret", 1, 1);
    auto st = std::make_unique<SimpleType>("Ret");
    EXPECT_FALSE(DoMatchAgainst(h.fptr.get(), st.get()));
    EXPECT_FALSE(DoMatchAgainst(st.get(), h.fptr.get()));
}

TEST(CSharp_FunctionPointerAstType, DoMatchNullRejects) {
    auto h = makeFuncPtr("Ret", 1, 1);
    EXPECT_FALSE(DoMatchAgainst(h.fptr.get(), nullptr));
}

// ---- Clone ----------------------------------------------------------------

TEST(CSharp_FunctionPointerAstType, CloneDeepCopyAndVirtuality) {
    auto h = makeFuncPtr("Ret", 2, 1);
    h.fptr->HasUnmanagedCallingConvention(true);
    auto* clone = h.fptr->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone, h.fptr.get());
    EXPECT_EQ(clone->HasUnmanagedCallingConvention(), true);  // scalar copied
    EXPECT_EQ(clone->CallingConventions().Count(), 2);
    EXPECT_NE(clone->CallingConventions().At(0), h.callingConventions[0].get());  // deep-copy
    EXPECT_NE(clone->CallingConventions().At(1), h.callingConventions[1].get());
    EXPECT_EQ(clone->CallingConventions().At(0)->Parent(), clone);
    EXPECT_EQ(clone->Parameters().Count(), 1);
    EXPECT_NE(clone->Parameters().At(0), h.parameters[0].get());  // deep-copy
    EXPECT_EQ(clone->Parameters().At(0)->Parent(), clone);
    EXPECT_NE(clone->ReturnType(), h.returnType.get());  // deep-copy, not aliased
    auto* clonedRet = dynamic_cast<SimpleType*>(clone->ReturnType());
    ASSERT_NE(clonedRet, nullptr);
    ASSERT_TRUE(clonedRet->Identifier().has_value());
    EXPECT_EQ(*clonedRet->Identifier(), "Ret");
    EXPECT_EQ(clone->ReturnType()->Parent(), clone);
    auto* astClone = static_cast<AstType*>(h.fptr.get())->Clone();  // covariant through AstType*
    EXPECT_NE(astClone, nullptr);
    auto* typedClone = dynamic_cast<FunctionPointerAstType*>(astClone);
    ASSERT_NE(typedClone, nullptr);
    EXPECT_EQ(typedClone->CallingConventions().Count(), 2);
    delete astClone;
    delete clone;
}

TEST(CSharp_FunctionPointerAstType, CloneSkipsAbsentReturnType) {
    auto fptr = std::make_unique<FunctionPointerAstType>();
    auto cc = std::make_unique<PrimitiveType>("unmanaged");
    fptr->CallingConventions().Add(cc.get());
    auto* clone = fptr->Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->CallingConventions().Count(), 1);
    EXPECT_EQ(clone->ReturnType(), nullptr);  // absent ReturnType is skipped (Clone tolerates it)
    delete clone;
}

// ---- CheckInvariant --------------------------------------------------------

// `CheckInvariant` PASSES on a filled node (the `ReturnType` is required and filled; the
// collections are optional).
TEST(CSharp_FunctionPointerAstType, CheckInvariantPassesOnFilled) {
    auto h = makeFuncPtr("Ret", 1, 1);
    h.fptr->CheckInvariant();  // should not assert
}

// `CheckInvariant` REJECTS an empty node (the `ReturnType` is a REQUIRED slot -- the empty node
// violates the required-slot invariant, the `InvocationAstType` D313 / `CastExpression` D243
// precedent).
TEST(CSharp_FunctionPointerAstType, CheckInvariantRejectsEmpty) {
    FunctionPointerAstType fptr;
    EXPECT_DEATH(fptr.CheckInvariant(), "");
}

// ---- slot-static distinctness ----------------------------------------------

// The three slot statics of distinct element types are unrelated pointer types; compare through
// the common `const CSharpSlotInfo*` base (the D251/D252/D262 cross-element-type `EXPECT_NE`
// precedent).
TEST(CSharp_FunctionPointerAstType, SlotStaticsAreDistinct) {
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&FunctionPointerAstType::CallingConventionsSlot),
              static_cast<const CSharpSlotInfo*>(&FunctionPointerAstType::ParametersSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&FunctionPointerAstType::CallingConventionsSlot),
              static_cast<const CSharpSlotInfo*>(&FunctionPointerAstType::ReturnTypeSlot));
    EXPECT_NE(static_cast<const CSharpSlotInfo*>(&FunctionPointerAstType::ParametersSlot),
              static_cast<const CSharpSlotInfo*>(&FunctionPointerAstType::ReturnTypeSlot));
}
