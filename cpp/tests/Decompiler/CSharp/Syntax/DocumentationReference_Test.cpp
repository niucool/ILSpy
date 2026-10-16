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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `DocumentationReference` concrete node (cpp/.../Syntax/DocumentationReference.hpp,
// the port of ICSharpCode.Decompiler/CSharp/Syntax/DocumentationReference.cs) -- the XML-
// documentation `cref` reference node (no C# grammar production): a sealed direct-`AstNode` node
// with three non-`[Slot]` scalars (`SymbolKind`/`OperatorType` enums + `HasParameterList` bool),
// three single `[Slot]` children (nullable `DeclaringType` `AstType`, required `NameToken`
// `Identifier`, required `ConversionOperatorReturnType` `AstType`), two collection `[Slot]`
// children (`TypeArguments` `AstType` collection, `Parameters` `ParameterDeclaration` collection),
// a hand-written `MemberName` string accessor over `NameToken`, and a HAND-WRITTEN `SymbolKind`-
// driven conditional `DoMatch`. The next in-order Phase-5 piece per the D294 plan. The suite shares
// a `RecordingVisitor` and a `DoMatchAgainst` helper (the D234 pattern).

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
#include "Decompiler/CSharp/Syntax/DocumentationReference.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;
using PatternMatching::Pattern;
using ILSpy::Decompiler::TypeSystem::SymbolKind;

namespace {

// A recording depth-first visitor: overrides the `VisitDocumentationReference` under test (plus
// the `SimpleType`/`Identifier`/`ParameterDeclaration` of its slots, and the `WhileStatement` used
// for the cross-type DoMatch rejection), recording a tag and recursing via `VisitChildren` (the
// inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitDocumentationReference(DocumentationReference* node) override {
        if (node == nullptr) { trace.push_back("<null-docref>"); return; }
        trace.push_back("docref");
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
    void VisitParameterDeclaration(ParameterDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-param>"); return; }
        trace.push_back("param");
        VisitChildren(node);
    }
    void VisitWhileStatement(WhileStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-while>"); return; }
        trace.push_back("while");
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

// A holder keeping a `DocumentationReference` and its child `unique_ptr`s all alive in the test
// scope (the port's non-owning raw-pointer child model -- a helper that returns only the parent
// would dangle its children, the D241 use-after-free crux). The (len=5) ctor takes the five
// required params; the collections are filled via `Add` after construction.
struct DocRefHolder {
    std::unique_ptr<SimpleType> declaringType;
    std::unique_ptr<Identifier> nameToken;
    std::unique_ptr<SimpleType> conversionReturnType;
    std::unique_ptr<DocumentationReference> node;

    // Build a `None`-branch cref: `SymbolKind::None`, a `DeclaringType` (nullable -- pass a real
    // `SimpleType` when `withDeclaringType`), a `NameToken`, and a dummy `ConversionOperatorReturnType`
    // (the required slot must be filled for `CheckInvariant`, but the `None` branch does not match
    // it). `OperatorType` is `LogicalNot` (unused in the `None` branch).
    static DocRefHolder makeNone(std::string name, bool withDeclaringType = true) {
        DocRefHolder h;
        h.declaringType = withDeclaringType ? std::make_unique<SimpleType>(std::string("Foo")) : nullptr;
        h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::move(name)));
        h.conversionReturnType = std::make_unique<SimpleType>(std::string("Int32"));
        h.node = std::make_unique<DocumentationReference>(
            SymbolKind::None, OperatorType::LogicalNot,
            h.declaringType.get(), h.nameToken.get(), h.conversionReturnType.get());
        return h;
    }

    // Build an `Operator`-branch cref with a non-conversion `OperatorType` (e.g. `Addition`): the
    // `ConversionOperatorReturnType` is NOT matched for non-conversion operators, but the required
    // slot must still be filled.
    static DocRefHolder makeOperator(OperatorType op) {
        DocRefHolder h;
        h.declaringType = nullptr;
        h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("op_Addition")));
        h.conversionReturnType = std::make_unique<SimpleType>(std::string("Int32"));
        h.node = std::make_unique<DocumentationReference>(
            SymbolKind::Operator, op,
            h.declaringType.get(), h.nameToken.get(), h.conversionReturnType.get());
        return h;
    }

    // Build an `Operator`-branch conversion cref (`Implicit`/`Explicit`): the
    // `ConversionOperatorReturnType` IS matched.
    static DocRefHolder makeConversion(OperatorType op, std::string returnType) {
        DocRefHolder h;
        h.declaringType = nullptr;
        h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("op_Implicit")));
        h.conversionReturnType = std::make_unique<SimpleType>(std::move(returnType));
        h.node = std::make_unique<DocumentationReference>(
            SymbolKind::Operator, op,
            h.declaringType.get(), h.nameToken.get(), h.conversionReturnType.get());
        return h;
    }

    DocumentationReference* operator->() { return node.get(); }
};

} // namespace

// ---- is-a / concrete / final -----------------------------------------------------------
TEST(CSharp_DocumentationReference, IsAstNodeNotExpressionStatementAstType) {
    EXPECT_TRUE((std::is_convertible_v<DocumentationReference*, AstNode*>));
    EXPECT_FALSE((std::is_convertible_v<DocumentationReference*, Expression*>));
    EXPECT_FALSE((std::is_convertible_v<DocumentationReference*, Statement*>));
    EXPECT_FALSE((std::is_convertible_v<DocumentationReference*, AstType*>));
}

TEST(CSharp_DocumentationReference, IsConcreteAndFinal) {
    EXPECT_FALSE(std::is_abstract_v<DocumentationReference>);
    EXPECT_TRUE(std::is_final_v<DocumentationReference>);
}

// ---- construction -----------------------------------------------------------------------
TEST(CSharp_DocumentationReference, EmptyCtor) {
    DocumentationReference d;
    EXPECT_EQ(d.SymbolKind(), SymbolKind::None);
    EXPECT_EQ(d.OperatorType(), OperatorType::LogicalNot);
    EXPECT_FALSE(d.HasParameterList());
    EXPECT_EQ(d.DeclaringType(), nullptr);
    EXPECT_EQ(d.NameToken(), nullptr);
    EXPECT_EQ(d.ConversionOperatorReturnType(), nullptr);
    EXPECT_EQ(d.TypeArguments().Count(), 0);
    EXPECT_EQ(d.Parameters().Count(), 0);
    EXPECT_EQ(d.GetChildCount(), 3); // three single slots (each counts even when null) + 0 + 0
}

TEST(CSharp_DocumentationReference, RequiredPrefixCtorSetsSlots) {
    auto st1 = std::make_unique<SimpleType>(std::string("Foo"));
    auto id = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    auto st2 = std::make_unique<SimpleType>(std::string("Int32"));
    DocumentationReference d(SymbolKind::None, OperatorType::LogicalNot,
                             st1.get(), id.get(), st2.get());
    EXPECT_EQ(d.SymbolKind(), SymbolKind::None);
    EXPECT_EQ(d.OperatorType(), OperatorType::LogicalNot);
    EXPECT_EQ(d.DeclaringType(), st1.get());
    EXPECT_EQ(d.NameToken(), id.get());
    EXPECT_EQ(d.ConversionOperatorReturnType(), st2.get());
    // The slot setters re-parented the children.
    EXPECT_EQ(st1->Parent(), &d);
    EXPECT_EQ(id->Parent(), &d);
    EXPECT_EQ(st2->Parent(), &d);
}

// ---- scalars ----------------------------------------------------------------------------
TEST(CSharp_DocumentationReference, SymbolKindScalarRoundTrips) {
    DocumentationReference d;
    d.SymbolKind(SymbolKind::Operator);
    EXPECT_EQ(d.SymbolKind(), SymbolKind::Operator);
    d.SymbolKind(SymbolKind::Indexer);
    EXPECT_EQ(d.SymbolKind(), SymbolKind::Indexer);
    d.SymbolKind(SymbolKind::TypeDefinition);
    EXPECT_EQ(d.SymbolKind(), SymbolKind::TypeDefinition);
}

TEST(CSharp_DocumentationReference, OperatorTypeScalarRoundTrips) {
    DocumentationReference d;
    d.OperatorType(OperatorType::Addition);
    EXPECT_EQ(d.OperatorType(), OperatorType::Addition);
    d.OperatorType(OperatorType::Implicit);
    EXPECT_EQ(d.OperatorType(), OperatorType::Implicit);
    d.OperatorType(OperatorType::Explicit);
    EXPECT_EQ(d.OperatorType(), OperatorType::Explicit);
}

TEST(CSharp_DocumentationReference, HasParameterListScalarRoundTrips) {
    DocumentationReference d;
    EXPECT_FALSE(d.HasParameterList());
    d.HasParameterList(true);
    EXPECT_TRUE(d.HasParameterList());
    d.HasParameterList(false);
    EXPECT_FALSE(d.HasParameterList());
}

// ---- MemberName hand-written string accessor --------------------------------------------
TEST(CSharp_DocumentationReference, MemberNameReadsNameToken) {
    auto h = DocRefHolder::makeNone("Bar");
    EXPECT_EQ(h->MemberName(), "Bar");
}

TEST(CSharp_DocumentationReference, MemberNameSetterCreatesToken) {
    auto h = DocRefHolder::makeNone("Bar");
    h->MemberName("Quux");
    EXPECT_EQ(h->MemberName(), "Quux");
    ASSERT_NE(h->NameToken(), nullptr);
    EXPECT_EQ(h->NameToken()->Name(), "Quux");
}

// ---- single slots -----------------------------------------------------------------------
TEST(CSharp_DocumentationReference, DeclaringTypeNullableSetterReparents) {
    auto h = DocRefHolder::makeNone("Bar", /*withDeclaringType=*/false);
    EXPECT_EQ(h->DeclaringType(), nullptr);
    auto st = std::make_unique<SimpleType>(std::string("Foo"));
    SimpleType* raw = st.get();
    h->DeclaringType(st.get());
    EXPECT_EQ(h->DeclaringType(), raw);
    EXPECT_EQ(raw->Parent(), h.node.get());
    // Clearing the nullable slot detaches the old child.
    h->DeclaringType(nullptr);
    EXPECT_EQ(h->DeclaringType(), nullptr);
    EXPECT_EQ(raw->Parent(), nullptr);
}

TEST(CSharp_DocumentationReference, NameTokenSetterReparentsAndDetaches) {
    auto h = DocRefHolder::makeNone("Bar");
    Identifier* oldTok = h->NameToken();
    EXPECT_EQ(oldTok->Parent(), h.node.get());
    auto id2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Quux")));
    Identifier* raw = id2.get();
    h->NameToken(id2.get());
    EXPECT_EQ(h->NameToken(), raw);
    EXPECT_EQ(raw->Parent(), h.node.get());
    EXPECT_EQ(oldTok->Parent(), nullptr); // the old token is detached
}

TEST(CSharp_DocumentationReference, ConversionOperatorReturnTypeSetterReparents) {
    auto h = DocRefHolder::makeNone("Bar");
    auto st2 = std::make_unique<SimpleType>(std::string("Long"));
    SimpleType* raw = st2.get();
    AstType* old = h->ConversionOperatorReturnType();
    h->ConversionOperatorReturnType(st2.get());
    EXPECT_EQ(h->ConversionOperatorReturnType(), raw);
    EXPECT_EQ(raw->Parent(), h.node.get());
    EXPECT_EQ(old->Parent(), nullptr);
}

// ---- collections ------------------------------------------------------------------------
TEST(CSharp_DocumentationReference, TypeArgumentsAddReparentsAndCounts) {
    auto h = DocRefHolder::makeNone("Bar");
    EXPECT_EQ(h->TypeArguments().Count(), 0);
    auto t1 = std::make_unique<SimpleType>(std::string("T"));
    auto t2 = std::make_unique<SimpleType>(std::string("U"));
    h->TypeArguments().Add(t1.get());
    h->TypeArguments().Add(t2.get());
    EXPECT_EQ(h->TypeArguments().Count(), 2);
    EXPECT_EQ(h->TypeArguments().At(0), t1.get());
    EXPECT_EQ(h->TypeArguments().At(1), t2.get());
    EXPECT_EQ(t1->Parent(), h.node.get());
    EXPECT_EQ(t2->Parent(), h.node.get());
}

TEST(CSharp_DocumentationReference, ParametersAddReparentsAndCounts) {
    auto h = DocRefHolder::makeNone("Bar");
    EXPECT_EQ(h->Parameters().Count(), 0);
    auto p1 = std::make_unique<ParameterDeclaration>();
    p1->NameToken(Identifier::Create(std::string("x")));
    auto p2 = std::make_unique<ParameterDeclaration>();
    p2->NameToken(Identifier::Create(std::string("y")));
    h->Parameters().Add(p1.get());
    h->Parameters().Add(p2.get());
    EXPECT_EQ(h->Parameters().Count(), 2);
    EXPECT_EQ(h->Parameters().At(0), p1.get());
    EXPECT_EQ(h->Parameters().At(1), p2.get());
    EXPECT_EQ(p1->Parent(), h.node.get());
    EXPECT_EQ(p2->Parent(), h.node.get());
}

TEST(CSharp_DocumentationReference, GetCollectionByKind) {
    auto h = DocRefHolder::makeNone("Bar");
    EXPECT_EQ(h.node->GetCollectionByKind(&Slots::TypeArgument), &h->TypeArguments());
    EXPECT_EQ(h.node->GetCollectionByKind(&Slots::Parameter), &h->Parameters());
    EXPECT_EQ(h.node->GetCollectionByKind(&Slots::Type), nullptr); // not a collection kind here
}

// ---- slot storage dispatch --------------------------------------------------------------
TEST(CSharp_DocumentationReference, GetChildWalksSinglesThenCollections) {
    auto h = DocRefHolder::makeNone("Bar");
    auto t1 = std::make_unique<SimpleType>(std::string("T"));
    auto p1 = std::make_unique<ParameterDeclaration>();
    h->TypeArguments().Add(t1.get());
    h->Parameters().Add(p1.get());
    // Index 0..2 are the three singles; 3 is the first TypeArgument; 4 is the first Parameter.
    EXPECT_EQ(h.node->GetChildCount(), 3 + 1 + 1);
    EXPECT_EQ(h.node->GetChild(0), h->DeclaringType());
    EXPECT_EQ(h.node->GetChild(1), h->NameToken());
    EXPECT_EQ(h.node->GetChild(2), h->ConversionOperatorReturnType());
    EXPECT_EQ(h.node->GetChild(3), t1.get());
    EXPECT_EQ(h.node->GetChild(4), p1.get());
    EXPECT_THROW(h.node->GetChild(5), std::out_of_range);
}

TEST(CSharp_DocumentationReference, GetChildSlotInfo) {
    auto h = DocRefHolder::makeNone("Bar");
    auto t1 = std::make_unique<SimpleType>(std::string("T"));
    auto p1 = std::make_unique<ParameterDeclaration>();
    h->TypeArguments().Add(t1.get());
    h->Parameters().Add(p1.get());
    EXPECT_EQ(h.node->GetChildSlotInfo(0), &h.node->DeclaringTypeSlot);
    EXPECT_EQ(h.node->GetChildSlotInfo(1), &h.node->NameTokenSlot);
    EXPECT_EQ(h.node->GetChildSlotInfo(2), &h.node->ConversionOperatorReturnTypeSlot);
    EXPECT_EQ(h.node->GetChildSlotInfo(3), &h.node->TypeArgumentsSlot);
    EXPECT_EQ(h.node->GetChildSlotInfo(4), &h.node->ParametersSlot);
    EXPECT_THROW(h.node->GetChildSlotInfo(5), std::out_of_range);
}

TEST(CSharp_DocumentationReference, SetChildReplacesEachSlot) {
    auto h = DocRefHolder::makeNone("Bar");
    auto newDecl = std::make_unique<SimpleType>(std::string("NewDecl"));
    auto newName = std::unique_ptr<Identifier>(Identifier::Create(std::string("NewName")));
    auto newRet = std::make_unique<SimpleType>(std::string("NewRet"));
    h.node->SetChild(0, newDecl.get());
    h.node->SetChild(1, newName.get());
    h.node->SetChild(2, newRet.get());
    EXPECT_EQ(h->DeclaringType(), newDecl.get());
    EXPECT_EQ(h->NameToken(), newName.get());
    EXPECT_EQ(h->ConversionOperatorReturnType(), newRet.get());
    auto t1 = std::make_unique<SimpleType>(std::string("T"));
    h->TypeArguments().Add(t1.get());
    auto t1b = std::make_unique<SimpleType>(std::string("Tb"));
    h.node->SetChild(3, t1b.get()); // replace the TypeArgument in place
    EXPECT_EQ(h->TypeArguments().At(0), t1b.get());
    EXPECT_THROW(h.node->SetChild(99, nullptr), std::out_of_range);
}

// ---- slot statics / kind identity -------------------------------------------------------
TEST(CSharp_DocumentationReference, SlotStaticsPointAtSharedKinds) {
    EXPECT_EQ(DocumentationReference::DeclaringTypeSlot.Kind(), &Slots::DeclaringType);
    EXPECT_EQ(DocumentationReference::NameTokenSlot.Kind(), &Slots::Identifier);
    EXPECT_EQ(DocumentationReference::ConversionOperatorReturnTypeSlot.Kind(), &Slots::ConversionOperatorReturnType);
    EXPECT_EQ(DocumentationReference::TypeArgumentsSlot.Kind(), &Slots::TypeArgument);
    EXPECT_EQ(DocumentationReference::ParametersSlot.Kind(), &Slots::Parameter);
}

TEST(CSharp_DocumentationReference, SlotStaticsAreDistinct) {
    const CSharpSlotInfo* a = &DocumentationReference::DeclaringTypeSlot;
    const CSharpSlotInfo* b = &DocumentationReference::NameTokenSlot;
    const CSharpSlotInfo* c = &DocumentationReference::ConversionOperatorReturnTypeSlot;
    const CSharpSlotInfo* d = &DocumentationReference::TypeArgumentsSlot;
    const CSharpSlotInfo* e = &DocumentationReference::ParametersSlot;
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(a, e);
    EXPECT_NE(b, c);
    EXPECT_NE(b, d);
    EXPECT_NE(b, e);
    EXPECT_NE(c, d);
    EXPECT_NE(c, e);
    EXPECT_NE(d, e);
}

TEST(CSharp_DocumentationReference, IsInstanceOfTypeAcceptsAstType) {
    auto st = std::make_unique<SimpleType>(std::string("T"));
    EXPECT_TRUE(Slots::DeclaringType.IsInstanceOfType(st.get()));
    EXPECT_TRUE(Slots::ConversionOperatorReturnType.IsInstanceOfType(st.get()));
    // A `Statement` is NOT an `AstType`, so the `dynamic_cast<const AstType*>` rejects it.
    auto stmt = std::make_unique<BreakStatement>();
    EXPECT_FALSE(Slots::DeclaringType.IsInstanceOfType(stmt.get()));
    EXPECT_FALSE(Slots::ConversionOperatorReturnType.IsInstanceOfType(stmt.get()));
}

// ---- AcceptVisitor dispatch -------------------------------------------------------------
TEST(CSharp_DocumentationReference, AcceptVisitorDispatch) {
    // A bare node (no filled slots) so the depth-first walk records just the node tag.
    DocumentationReference d;
    RecordingVisitor v;
    d.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "docref");
}

TEST(CSharp_DocumentationReference, AcceptVisitorVirtualThroughAstNode) {
    DocumentationReference d;
    AstNode* asBase = &d;
    RecordingVisitor v;
    asBase->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "docref");
}

TEST(CSharp_DocumentationReference, DepthFirstWalkOrder) {
    auto h = DocRefHolder::makeNone("Bar");
    auto t1 = std::make_unique<SimpleType>(std::string("T"));
    auto p1 = std::make_unique<ParameterDeclaration>();
    p1->NameToken(Identifier::Create(std::string("x")));
    h->TypeArguments().Add(t1.get());
    h->Parameters().Add(p1.get());
    RecordingVisitor v;
    h.node->AcceptVisitor(v);
    // docref, simple:Foo, id:Foo, id:Bar, simple:Int32, id:Int32, simple:T, id:T, param, id:x
    const std::vector<std::string> expected = {
        "docref", "simple:Foo", "id:Foo", "id:Bar",
        "simple:Int32", "id:Int32", "simple:T", "id:T",
        "param", "id:x"
    };
    EXPECT_EQ(v.trace, expected);
}

// ---- DoMatch (hand-written, SymbolKind-driven) ------------------------------------------
TEST(CSharp_DocumentationReference, DoMatchNoneBranchMatches) {
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNoneBranchRejectsDifferentMemberName) {
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Quux");
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNoneBranchRejectsDifferentSymbolKind) {
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    b->SymbolKind(SymbolKind::Operator); // different SymbolKind rejects at the first gate
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNoneBranchRejectsDifferentHasParameterList) {
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    b->HasParameterList(true);
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNoneBranchRejectsDifferentTypeArguments) {
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    auto t = std::make_unique<SimpleType>(std::string("T"));
    b->TypeArguments().Add(t.get()); // a has no type arguments, b has one -> mismatch
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNoneBranchIgnoresConversionOperatorReturnType) {
    // The None branch does NOT match ConversionOperatorReturnType -- two None-branch crefs with
    // different (dummy) conversion return types still match.
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    auto otherRet = std::make_unique<SimpleType>(std::string("Long"));
    b->ConversionOperatorReturnType(otherRet.get());
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNoneBranchRejectsDifferentParameters) {
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    auto p = std::make_unique<ParameterDeclaration>();
    p->NameToken(Identifier::Create(std::string("x")));
    b->Parameters().Add(p.get()); // a has no parameters, b has one -> mismatch
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchMatchesSameOperatorType) {
    auto a = DocRefHolder::makeOperator(OperatorType::Addition);
    auto b = DocRefHolder::makeOperator(OperatorType::Addition);
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchRejectsDifferentOperatorType) {
    auto a = DocRefHolder::makeOperator(OperatorType::Addition);
    auto b = DocRefHolder::makeOperator(OperatorType::Subtraction);
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchIgnoresMemberNameAndTypeArguments) {
    // The Operator branch does NOT match MemberName or TypeArguments -- two operators with the
    // same OperatorType but different names/type-args still match.
    auto a = DocRefHolder::makeOperator(OperatorType::Addition);
    auto b = DocRefHolder::makeOperator(OperatorType::Addition);
    b->MemberName("op_Different");
    auto t = std::make_unique<SimpleType>(std::string("T"));
    b->TypeArguments().Add(t.get());
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchNonConversionIgnoresReturnType) {
    // A non-conversion operator (Addition) does NOT match ConversionOperatorReturnType.
    auto a = DocRefHolder::makeOperator(OperatorType::Addition);
    auto b = DocRefHolder::makeOperator(OperatorType::Addition);
    auto otherRet = std::make_unique<SimpleType>(std::string("Long"));
    b->ConversionOperatorReturnType(otherRet.get());
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchImplicitMatchesSameReturnType) {
    auto a = DocRefHolder::makeConversion(OperatorType::Implicit, "Int32");
    auto b = DocRefHolder::makeConversion(OperatorType::Implicit, "Int32");
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchImplicitRejectsDifferentReturnType) {
    auto a = DocRefHolder::makeConversion(OperatorType::Implicit, "Int32");
    auto b = DocRefHolder::makeConversion(OperatorType::Implicit, "Long");
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOperatorBranchExplicitRejectsDifferentReturnType) {
    auto a = DocRefHolder::makeConversion(OperatorType::Explicit, "Int32");
    auto b = DocRefHolder::makeConversion(OperatorType::Explicit, "Long");
    EXPECT_FALSE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchOtherSymbolKindBranchMatchesOnParametersOnly) {
    // A SymbolKind that is neither Operator nor None (e.g. TypeDefinition) matches ONLY on the
    // unconditional Parameters term -- MemberName/TypeArguments/OperatorType/ConversionOperatorReturnType
    // are all ignored.
    auto a = DocRefHolder::makeNone("Bar");
    auto b = DocRefHolder::makeNone("Bar");
    a->SymbolKind(SymbolKind::TypeDefinition);
    b->SymbolKind(SymbolKind::TypeDefinition);
    // Different names/type-args/conversion-return-type are ignored in this branch.
    b->MemberName("Different");
    auto t = std::make_unique<SimpleType>(std::string("T"));
    b->TypeArguments().Add(t.get());
    auto otherRet = std::make_unique<SimpleType>(std::string("Long"));
    b->ConversionOperatorReturnType(otherRet.get());
    EXPECT_TRUE(DoMatchAgainst(a.node.get(), b.node.get()));
}

TEST(CSharp_DocumentationReference, DoMatchCrossTypeRejection) {
    auto h = DocRefHolder::makeNone("Bar");
    auto w = std::make_unique<WhileStatement>();
    EXPECT_FALSE(DoMatchAgainst(h.node.get(), w.get()));
}

TEST(CSharp_DocumentationReference, DoMatchNullCandidate) {
    auto h = DocRefHolder::makeNone("Bar");
    EXPECT_FALSE(DoMatchAgainst(h.node.get(), nullptr));
}

// ---- Clone ------------------------------------------------------------------------------
TEST(CSharp_DocumentationReference, CloneDeepCopiesScalarsAndSlots) {
    auto h = DocRefHolder::makeNone("Bar");
    auto t = std::make_unique<SimpleType>(std::string("T"));
    auto p = std::make_unique<ParameterDeclaration>();
    p->NameToken(Identifier::Create(std::string("x")));
    h->TypeArguments().Add(t.get());
    h->Parameters().Add(p.get());
    h->HasParameterList(true);
    h->SymbolKind(SymbolKind::Indexer);

    std::unique_ptr<DocumentationReference> c(h.node->Clone());
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->SymbolKind(), SymbolKind::Indexer);
    EXPECT_EQ(c->OperatorType(), OperatorType::LogicalNot);
    EXPECT_TRUE(c->HasParameterList());
    // The cloned children are distinct deep copies (not the same pointers).
    ASSERT_NE(c->DeclaringType(), nullptr);
    EXPECT_NE(c->DeclaringType(), h->DeclaringType());
    ASSERT_NE(c->NameToken(), nullptr);
    EXPECT_NE(c->NameToken(), h->NameToken());
    EXPECT_EQ(c->NameToken()->Name(), "Bar");
    ASSERT_NE(c->ConversionOperatorReturnType(), nullptr);
    EXPECT_NE(c->ConversionOperatorReturnType(), h->ConversionOperatorReturnType());
    EXPECT_EQ(c->TypeArguments().Count(), 1);
    EXPECT_NE(c->TypeArguments().At(0), t.get());
    EXPECT_EQ(c->Parameters().Count(), 1);
    EXPECT_NE(c->Parameters().At(0), p.get());
    // The clone's children are re-parented to the clone.
    EXPECT_EQ(c->DeclaringType()->Parent(), c.get());
    EXPECT_EQ(c->NameToken()->Parent(), c.get());
    EXPECT_EQ(c->ConversionOperatorReturnType()->Parent(), c.get());
    EXPECT_EQ(c->TypeArguments().At(0)->Parent(), c.get());
    EXPECT_EQ(c->Parameters().At(0)->Parent(), c.get());
    // The source is not detached.
    EXPECT_EQ(h->NameToken()->Parent(), h.node.get());
}

TEST(CSharp_DocumentationReference, CloneVirtualThroughAstNode) {
    auto h = DocRefHolder::makeNone("Bar");
    AstNode* asBase = h.node.get();
    std::unique_ptr<AstNode> c(asBase->Clone());
    ASSERT_NE(c, nullptr);
    auto* typed = dynamic_cast<DocumentationReference*>(c.get());
    ASSERT_NE(typed, nullptr);
    EXPECT_EQ(typed->MemberName(), "Bar");
}

TEST(CSharp_DocumentationReference, CloneCovariantReturn) {
    auto h = DocRefHolder::makeNone("Bar");
    DocumentationReference* c = h.node->Clone();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->MemberName(), "Bar");
    delete c;
}

TEST(CSharp_DocumentationReference, CloneSkipsAbsentDeclaringType) {
    auto h = DocRefHolder::makeNone("Bar", /*withDeclaringType=*/false);
    std::unique_ptr<DocumentationReference> c(h.node->Clone());
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->DeclaringType(), nullptr); // the nullable DeclaringType stays absent
}

// ---- CheckInvariant ---------------------------------------------------------------------
// `CheckInvariant` passes on a filled node (the required `NameToken`/`ConversionOperatorReturnType`
// slots are filled).
TEST(CSharp_DocumentationReference, CheckInvariantPassesOnFilledNode) {
    auto h = DocRefHolder::makeNone("Bar");
    h.node->CheckInvariant();  // should not assert
    SUCCEED();
}

// `CheckInvariant` is REJECTED on an empty node (`NameToken` and `ConversionOperatorReturnType`
// are REQUIRED slots, so a default-constructed node violates their required-slot invariants --
// the assert fires in debug).
#ifndef NDEBUG
TEST(CSharp_DocumentationReference, CheckInvariantRejectsEmpty) {
    DocumentationReference d;  // no NameToken, no ConversionOperatorReturnType
    EXPECT_DEATH(d.CheckInvariant(), "");
}
#endif
