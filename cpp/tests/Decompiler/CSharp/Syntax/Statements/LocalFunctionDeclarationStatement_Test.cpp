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

// Tests for `LocalFunctionDeclarationStatement` -- the `local_function_declaration ::=
// method_declaration` node (C# grammar 13.6.4), the last remaining concrete statement and the
// next in-order Phase-5 piece per the D281 plan ("MethodDeclaration ... unblocks
// LocalFunctionDeclarationStatement"). A sealed `Statement` wrapping a single REQUIRED
// `MethodDeclaration` `Declaration` child (the `CheckedStatement` D260 single-required-slot
// shape with a `MethodDeclaration` child), reusing the new cycle-broken `Slots::MethodDeclaration`
// kind (in `MethodDeclaration.hpp`).

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/CheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

using namespace ILSpy::Decompiler::CSharp::Syntax;
using PatternMatching::INode;
using PatternMatching::Match;

namespace {

// A recording depth-first visitor: overrides the new `VisitLocalFunctionDeclarationStatement`
// under test plus the `MethodDeclaration`/`SimpleType`/`Identifier`/`BlockStatement`/
// `EmptyStatement` the wrapped method holds, recording a tag and recursing via `VisitChildren`
// (the inherited depth-first default). The trace is the visited nodes in pre-order.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    std::vector<std::string> trace;

    void VisitLocalFunctionDeclarationStatement(LocalFunctionDeclarationStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-lfds>"); return; }
        trace.push_back("lfds");
        VisitChildren(node);
    }
    void VisitMethodDeclaration(MethodDeclaration* node) override {
        if (node == nullptr) { trace.push_back("<null-method>"); return; }
        trace.push_back("method:" + node->Name());
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
    void VisitBlockStatement(BlockStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-block>"); return; }
        trace.push_back("block");
        VisitChildren(node);
    }
    void VisitEmptyStatement(EmptyStatement* node) override {
        if (node == nullptr) { trace.push_back("<null-empty>"); return; }
        trace.push_back("empty");
        VisitChildren(node);
    }
};

// The `DoMatch` helper: invokes the typed `DoMatch` via the public `INode` delegation (the
// protected typed `DoMatch` is hidden in the derived class, so call through `INode*`, the D220
// pattern).
static bool DoMatchAgainst(INode* pattern, INode* candidate) {
    Match m = Match::CreateNew();
    return pattern->DoMatch(candidate, m);
}

// A holder keeping a `LocalFunctionDeclarationStatement` and its wrapped `MethodDeclaration`
// (plus the method's `ReturnType` `SimpleType` and `NameToken` `Identifier`) alive in the test
// scope -- the non-owning raw-pointer child-slot model means the test must own every node (the
// D241 use-after-free precedent).
struct LfdsHolder {
    std::unique_ptr<LocalFunctionDeclarationStatement> lfds;
    std::unique_ptr<MethodDeclaration> md;
    std::unique_ptr<SimpleType> returnType;
    std::unique_ptr<Identifier> nameToken;
    LocalFunctionDeclarationStatement* get() const { return lfds.get(); }
    LocalFunctionDeclarationStatement* operator->() const { return lfds.get(); }
};

// Build a `LocalFunctionDeclarationStatement` wrapping a `MethodDeclaration` with a `NameToken`
// `Foo` and a `ReturnType` `int` (the method's two required slots filled, no collections, no body).
LfdsHolder make_Lfds() {
    LfdsHolder h;
    h.md = std::make_unique<MethodDeclaration>();
    h.returnType = std::make_unique<SimpleType>(std::string("int"));
    h.md->ReturnType(h.returnType.get());
    h.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    h.md->NameToken(h.nameToken.get());
    h.lfds = std::make_unique<LocalFunctionDeclarationStatement>(h.md.get());
    return h;
}

} // namespace

// ==========================================================================
// is-a
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, IsStatementAndAstNodeNotExpression) {
    LocalFunctionDeclarationStatement s;
    EXPECT_NE(dynamic_cast<Statement*>(&s), nullptr);
    EXPECT_NE(dynamic_cast<AstNode*>(&s), nullptr);
    // The Statement and Expression hierarchies are disjoint (both derive directly from AstNode):
    // a concrete statement is NOT an Expression.
    EXPECT_EQ(dynamic_cast<Expression*>(&s), nullptr);
}

TEST(CSharp_LocalFunctionDeclarationStatement, IsFinal) {
    EXPECT_TRUE(std::is_final_v<LocalFunctionDeclarationStatement>);
}

// ==========================================================================
// construction
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, DefaultCtorHasNullDeclaration) {
    LocalFunctionDeclarationStatement s;
    EXPECT_EQ(s.Declaration(), nullptr);
    EXPECT_EQ(s.GetChildCount(), 1);
}

TEST(CSharp_LocalFunctionDeclarationStatement, AllParamsCtorSetsDeclaration) {
    auto md = std::make_unique<MethodDeclaration>();
    auto returnType = std::make_unique<SimpleType>(std::string("int"));
    md->ReturnType(returnType.get());
    auto nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    md->NameToken(nameToken.get());
    MethodDeclaration* mdPtr = md.get();

    LocalFunctionDeclarationStatement s(mdPtr);
    EXPECT_EQ(s.Declaration(), mdPtr);
    // The ctor transferred ownership of the child to the parent.
    EXPECT_EQ(md->Parent(), &s);
    md.release();  // The parent now owns it; release the unique_ptr without deleting.
    returnType.release();
    nameToken.release();
    EXPECT_EQ(s.Declaration()->Parent(), &s);
    EXPECT_EQ(s.Declaration()->ChildIndex, 0);
}

// ==========================================================================
// Declaration slot accessors
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, DeclarationSetterReparentsAndIndexes) {
    LocalFunctionDeclarationStatement s;
    auto md = std::make_unique<MethodDeclaration>();
    auto returnType = std::make_unique<SimpleType>(std::string("int"));
    md->ReturnType(returnType.get());
    auto nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    md->NameToken(nameToken.get());
    MethodDeclaration* mdPtr = md.get();
    s.Declaration(mdPtr);
    md.release();
    returnType.release();
    nameToken.release();
    EXPECT_EQ(s.Declaration(), mdPtr);
    EXPECT_EQ(s.Declaration()->Parent(), &s);
    EXPECT_EQ(s.Declaration()->ChildIndex, 0);
}

TEST(CSharp_LocalFunctionDeclarationStatement, DeclarationSetterRejectsAlreadyParented) {
    LocalFunctionDeclarationStatement a;
    LocalFunctionDeclarationStatement b;
    auto md = std::make_unique<MethodDeclaration>();
    a.Declaration(md.get());
    EXPECT_THROW({ b.Declaration(md.get()); }, std::logic_error);
}

// ==========================================================================
// slot storage
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, SlotStorageContract) {
    auto h = make_Lfds();
    LocalFunctionDeclarationStatement* s = h.get();
    MethodDeclaration* md = h.md.get();

    EXPECT_EQ(s->GetChildCount(), 1);
    EXPECT_EQ(s->GetChild(0), md);
    EXPECT_EQ(s->GetChildSlotInfo(0), &LocalFunctionDeclarationStatement::DeclarationSlot);
    EXPECT_THROW({ (void)s->GetChild(1); }, std::out_of_range);
    EXPECT_THROW({ (void)s->GetChildSlotInfo(1); }, std::out_of_range);
}

TEST(CSharp_LocalFunctionDeclarationStatement, SetChildAtSlotIndexZero) {
    LocalFunctionDeclarationStatement s;
    auto md1 = std::make_unique<MethodDeclaration>();
    auto rt1 = std::make_unique<SimpleType>(std::string("int"));
    md1->ReturnType(rt1.get());
    auto nt1 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Foo")));
    md1->NameToken(nt1.get());
    auto md2 = std::make_unique<MethodDeclaration>();
    auto rt2 = std::make_unique<SimpleType>(std::string("int"));
    md2->ReturnType(rt2.get());
    auto nt2 = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    md2->NameToken(nt2.get());
    MethodDeclaration* m1 = md1.get();
    MethodDeclaration* m2 = md2.get();
    s.Declaration(m1);
    md1.release();
    rt1.release();
    nt1.release();
    s.SetChild(0, m2);
    md2.release();
    rt2.release();
    nt2.release();
    EXPECT_EQ(s.Declaration(), m2);
    EXPECT_EQ(m2->Parent(), &s);
    EXPECT_EQ(m2->ChildIndex, 0);
}

// ==========================================================================
// slot kind identity
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, DeclarationSlotPointsAtSharedSlotsMethodDeclaration) {
    EXPECT_EQ(LocalFunctionDeclarationStatement::DeclarationSlot.Kind(), &Slots::MethodDeclaration);
    EXPECT_EQ(LocalFunctionDeclarationStatement::DeclarationSlot.IsCollection(), false);
    EXPECT_EQ(LocalFunctionDeclarationStatement::DeclarationSlot.IsOptional(), false);
}

// ==========================================================================
// IsInstanceOfType is-a (the slot's dynamic_cast checker)
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, DeclarationSlotAcceptsMethodDeclaration) {
    MethodDeclaration md;
    EXPECT_TRUE(LocalFunctionDeclarationStatement::DeclarationSlot.IsInstanceOfType(&md));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DeclarationSlotRejectsNonMethodDeclaration) {
    LocalFunctionDeclarationStatement s;  // a Statement but NOT a MethodDeclaration
    EXPECT_FALSE(LocalFunctionDeclarationStatement::DeclarationSlot.IsInstanceOfType(&s));
}

// ==========================================================================
// AcceptVisitor dispatch
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, AcceptVisitorDispatches) {
    LocalFunctionDeclarationStatement s;
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lfds");
}

TEST(CSharp_LocalFunctionDeclarationStatement, AcceptVisitorVirtualThroughAstNode) {
    LocalFunctionDeclarationStatement s;
    AstNode* node = &s;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lfds");
}

TEST(CSharp_LocalFunctionDeclarationStatement, AcceptVisitorVirtualThroughStatement) {
    LocalFunctionDeclarationStatement s;
    Statement* node = &s;
    RecordingVisitor v;
    node->AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lfds");
}

// ==========================================================================
// depth-first walk
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, DepthFirstWalkRecurseIntoMethodDeclaration) {
    auto h = make_Lfds();
    RecordingVisitor v;
    h->AcceptVisitor(v);
    // lfds -> method:Foo -> simple:int (ReturnType) -> id:int (the SimpleType's backing
    // IdentifierToken) -> id:Foo (the method's NameToken). The basic method has no body and no
    // collection elements, so its only filled children are the ReturnType and the NameToken.
    ASSERT_EQ(v.trace.size(), 5u);
    EXPECT_EQ(v.trace[0], "lfds");
    EXPECT_EQ(v.trace[1], "method:Foo");
    EXPECT_EQ(v.trace[2], "simple:int");
    EXPECT_EQ(v.trace[3], "id:int");
    EXPECT_EQ(v.trace[4], "id:Foo");
}

TEST(CSharp_LocalFunctionDeclarationStatement, DepthFirstWalkEmptyNodeRecordsJustSelf) {
    LocalFunctionDeclarationStatement s;  // no Declaration
    RecordingVisitor v;
    s.AcceptVisitor(v);
    ASSERT_EQ(v.trace.size(), 1u);
    EXPECT_EQ(v.trace[0], "lfds");
}

// ==========================================================================
// DoMatch
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchSameDeclaration) {
    auto p = make_Lfds();
    auto c = make_Lfds();
    EXPECT_TRUE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchDifferentDeclaration) {
    auto p = make_Lfds();  // method "Foo"
    // A candidate wrapping a method "Bar" -- the MethodDeclaration DoMatch rejects on the Name
    // MatchString term (Foo != Bar), so the wrapper's MatchRequired delegation rejects.
    LfdsHolder c;
    c.md = std::make_unique<MethodDeclaration>();
    c.returnType = std::make_unique<SimpleType>(std::string("int"));
    c.md->ReturnType(c.returnType.get());
    c.nameToken = std::unique_ptr<Identifier>(Identifier::Create(std::string("Bar")));
    c.md->NameToken(c.nameToken.get());
    c.lfds = std::make_unique<LocalFunctionDeclarationStatement>(c.md.get());
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchNullPatternDeclarationDoesNotMatch) {
    auto p = std::make_unique<LocalFunctionDeclarationStatement>();  // null Declaration
    auto c = make_Lfds();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchNullCandidateDeclarationDoesNotMatch) {
    auto p = make_Lfds();
    auto c = std::make_unique<LocalFunctionDeclarationStatement>();  // null Declaration
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchRejectsCheckedStatementCandidate) {
    // A cross-type rejection: a single-required-slot statement sibling (CheckedStatement wraps a
    // BlockStatement, not a MethodDeclaration) -- the 'other is LocalFunctionDeclarationStatement'
    // type check rejects.
    auto p = make_Lfds();
    auto c = std::make_unique<CheckedStatement>();
    auto body = std::make_unique<BlockStatement>();
    c->Body(body.get());
    body.release();
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchRejectsNull) {
    auto p = make_Lfds();
    EXPECT_FALSE(DoMatchAgainst(p.get(), nullptr));
}

TEST(CSharp_LocalFunctionDeclarationStatement, DoMatchRejectsNonStatementCandidate) {
    auto p = make_Lfds();
    auto c = std::make_unique<NullReferenceExpression>();  // an Expression, not a Statement
    EXPECT_FALSE(DoMatchAgainst(p.get(), c.get()));
}

// ==========================================================================
// Clone
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, CloneDeepCopiesDeclaration) {
    auto h = make_Lfds();
    std::unique_ptr<LocalFunctionDeclarationStatement> clone(h->Clone());
    ASSERT_NE(clone->Declaration(), nullptr);
    EXPECT_NE(clone->Declaration(), h.md.get());  // deep copy, not the same node
    EXPECT_EQ(clone->Declaration()->Parent(), clone.get());
    EXPECT_EQ(clone->Declaration()->ChildIndex, 0);
    // The cloned method has the same structure: a ReturnType SimpleType "int" + a NameToken "Foo".
    EXPECT_EQ(clone->Declaration()->Name(), "Foo");
    EXPECT_NE(clone->Declaration()->ReturnType(), nullptr);
}

TEST(CSharp_LocalFunctionDeclarationStatement, CloneNullDeclaration) {
    LocalFunctionDeclarationStatement s;  // no Declaration
    std::unique_ptr<LocalFunctionDeclarationStatement> clone(s.Clone());
    EXPECT_EQ(clone->Declaration(), nullptr);
}

TEST(CSharp_LocalFunctionDeclarationStatement, CloneVirtualThroughAstNode) {
    auto h = make_Lfds();
    AstNode* node = h.get();
    std::unique_ptr<AstNode> clone(node->Clone());
    EXPECT_NE(dynamic_cast<LocalFunctionDeclarationStatement*>(clone.get()), nullptr);
}

TEST(CSharp_LocalFunctionDeclarationStatement, CloneCovariantThroughStatement) {
    auto h = make_Lfds();
    Statement* node = h.get();
    std::unique_ptr<Statement> clone(node->Clone());
    EXPECT_NE(dynamic_cast<LocalFunctionDeclarationStatement*>(clone.get()), nullptr);
}

// ==========================================================================
// CheckInvariant
// ==========================================================================

TEST(CSharp_LocalFunctionDeclarationStatement, CheckInvariantPassesOnFilledNode) {
    auto h = make_Lfds();
#ifndef NDEBUG
    EXPECT_NO_FATAL_FAILURE(h->CheckInvariant());
#endif
}
