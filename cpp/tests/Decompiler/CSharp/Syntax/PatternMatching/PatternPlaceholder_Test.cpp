// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the generated pattern-placeholder machinery
// (Syntax/PatternPlaceholder.hpp + the PatternExtensions factory shims): the
// `PatternPlaceholderNode<TNode>` template's matching delegation, visitor dispatch
// (void and bool), clone, and the `ToType`/`ToExpression`/`ToStatement`/`WithName`
// factories, plus the formerly-final `hasPatternPlaceholder` bases now being
// inheritable.

#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"

#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::CSharp::Syntax;
namespace PM = ILSpy::Decompiler::CSharp::Syntax::PatternMatching;

namespace {

// A void visitor recording the `VisitPatternPlaceholder` dispatch.
class RecordingVisitor : public DepthFirstAstVisitor {
public:
    int calls = 0;
    AstNode* placeholder = nullptr;
    PM::Pattern* pattern = nullptr;

    void VisitPatternPlaceholder(AstNode* p, PM::Pattern& pat) override {
        calls++;
        placeholder = p;
        pattern = &pat;
    }
};

// A bool visitor recording the `VisitPatternPlaceholder` dispatch.
class RecordingBoolVisitor : public DepthFirstAstVisitorBool {
public:
    int calls = 0;
    AstNode* placeholder = nullptr;
    PM::Pattern* pattern = nullptr;

    bool VisitPatternPlaceholder(AstNode* p, PM::Pattern& pat) override {
        calls++;
        placeholder = p;
        pattern = &pat;
        return true;
    }
};

} // namespace

// ---- ToExpression -----------------------------------------------------------

TEST(CSharp_PatternPlaceholder, ToExpressionWrapsAndMatches) {
    auto any = std::make_shared<PM::AnyNode>("g");
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(any));
    ASSERT_NE(placeholder, nullptr);

    IdentifierExpression id("x");
    PM::Match m = PM::PatternExtensions::Match(*placeholder, &id);
    ASSERT_TRUE(m.Has("g"));
    EXPECT_EQ(m.Get("g")[0], &id);
}

TEST(CSharp_PatternPlaceholder, ToExpressionNullPatternIsNull) {
    EXPECT_EQ(PM::PatternExtensions::ToExpression(nullptr), nullptr);
}

TEST(CSharp_PatternPlaceholder, ToTypeWrapsAndMatches) {
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<AstType> placeholder(PM::PatternExtensions::ToType(any));
    ASSERT_NE(placeholder, nullptr);

    SimpleType type("System.Int32");
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(*placeholder, &type));
    EXPECT_EQ(PM::PatternExtensions::ToType(nullptr), nullptr);
}

TEST(CSharp_PatternPlaceholder, ToStatementWrapsAndMatches) {
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<Statement> placeholder(PM::PatternExtensions::ToStatement(any));
    ASSERT_NE(placeholder, nullptr);

    BlockStatement stmt;
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(*placeholder, &stmt));
    EXPECT_EQ(PM::PatternExtensions::ToStatement(nullptr), nullptr);
}

TEST(CSharp_PatternPlaceholder, NamedPatternRejectsCandidate) {
    // A NamedNode wrapping a concrete pattern: the placeholder must delegate to it.
    auto named = std::make_shared<PM::NamedNode>(
        "ident", new IdentifierExpression(std::string(PM::Pattern::AnyString)));
    // NamedNode keeps non-owning children; a leak is acceptable in the test-local pattern.
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(named));

    IdentifierExpression match("x");
    PM::Match ok = PM::PatternExtensions::Match(*placeholder, &match);
    ASSERT_TRUE(ok.Has("ident"));
    EXPECT_EQ(ok.Get("ident")[0], &match);

    // A non-IdentifierExpression candidate is rejected by the wrapped IdentifierExpression
    // pattern (the AST node's structural match).
    PrimitiveExpression primitive(1);
    EXPECT_FALSE(PM::PatternExtensions::IsMatch(*placeholder, &primitive));
}

// ---- WithName ---------------------------------------------------------------

TEST(CSharp_PatternPlaceholder, WithNameCapturesExpression) {
    IdentifierExpression id("x");
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::WithName(id, "var"));
    ASSERT_NE(placeholder, nullptr);

    IdentifierExpression same("x");
    PM::Match m = PM::PatternExtensions::Match(*placeholder, &same);
    ASSERT_TRUE(m.Has("var"));
    EXPECT_EQ(m.Get("var")[0], &same);

    IdentifierExpression different("y");
    EXPECT_FALSE(PM::PatternExtensions::IsMatch(*placeholder, &different));
}

TEST(CSharp_PatternPlaceholder, WithNameCapturesStatement) {
    BlockStatement block;
    std::unique_ptr<Statement> placeholder(PM::PatternExtensions::WithName(block, "body"));
    ASSERT_NE(placeholder, nullptr);

    BlockStatement candidate;
    PM::Match m = PM::PatternExtensions::Match(*placeholder, &candidate);
    ASSERT_TRUE(m.Has("body"));
    EXPECT_EQ(m.Get("body")[0], &candidate);
}

// ---- Visitor dispatch -------------------------------------------------------

TEST(CSharp_PatternPlaceholder, AcceptVisitorRoutesToVisitPatternPlaceholder) {
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(any));

    RecordingVisitor visitor;
    placeholder->AcceptVisitor(visitor);
    EXPECT_EQ(visitor.calls, 1);
    EXPECT_EQ(visitor.placeholder, placeholder.get());
    EXPECT_EQ(visitor.pattern, any.get());
}

TEST(CSharp_PatternPlaceholder, AcceptVisitorBoolRoutesToVisitPatternPlaceholder) {
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(any));

    RecordingBoolVisitor visitor;
    EXPECT_TRUE(placeholder->AcceptVisitorBool(visitor));
    EXPECT_EQ(visitor.calls, 1);
    EXPECT_EQ(visitor.placeholder, placeholder.get());
    EXPECT_EQ(visitor.pattern, any.get());
}

TEST(CSharp_PatternPlaceholder, DefaultDepthFirstVisitorWalkIsSafe) {
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(any));
    // `DepthFirstAstVisitor`'s default `VisitPatternPlaceholder` walks the placeholder's (empty)
    // AST children; it must not crash or touch the wrapped pattern.
    class NoOpVisitor : public DepthFirstAstVisitor {};
    NoOpVisitor visitor;
    placeholder->AcceptVisitor(visitor);
}

// ---- DoMatchCollection ------------------------------------------------------

TEST(CSharp_PatternPlaceholder, DoMatchCollectionDelegatesToWrappedPattern) {
    // A Repeat wrapped in a placeholder must keep the collection (backtracking) behaviour
    // rather than the single-element `AstNode::DoMatchCollection` default.
    PM::AnyNode any;
    auto repeat = std::make_shared<PM::Repeat>(&any);
    PatternPlaceholderNode<Expression> placeholder(repeat);

    IdentifierExpression a("a");
    IdentifierExpression b("b");
    std::vector<PM::INode*> others{&a, &b};
    PM::Match m = PM::Match::CreateNew();
    PM::BacktrackingInfo bt;

    EXPECT_FALSE(placeholder.DoMatchCollection(others, 0, m, bt));
    // Counts 0, 1, 2 pushed (a match for each prefix length).
    EXPECT_EQ(bt.BacktrackingStack.size(), 3u);
}

// ---- Clone ------------------------------------------------------------------

TEST(CSharp_PatternPlaceholder, CloneSharesThePattern) {
    auto any = std::make_shared<PM::AnyNode>("g");
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(any));

    std::unique_ptr<Expression> clone(placeholder->Clone());
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), placeholder.get());
    auto* originalPh = dynamic_cast<PatternPlaceholderNode<Expression>*>(placeholder.get());
    auto* clonePh = dynamic_cast<PatternPlaceholderNode<Expression>*>(clone.get());
    ASSERT_NE(originalPh, nullptr);
    ASSERT_NE(clonePh, nullptr);
    EXPECT_EQ(&clonePh->Child(), &originalPh->Child());

    IdentifierExpression id("x");
    PM::Match m = PM::PatternExtensions::Match(*clone, &id);
    EXPECT_TRUE(m.Has("g"));
}

TEST(CSharp_PatternPlaceholder, ImplementsPatternPlaceholderMarker) {
    auto any = std::make_shared<PM::AnyNode>();
    std::unique_ptr<Expression> placeholder(PM::PatternExtensions::ToExpression(any));
    EXPECT_NE(dynamic_cast<PM::IPatternPlaceholder*>(placeholder.get()), nullptr);
}

// ---- Every hasPatternPlaceholder base is inheritable ------------------------

TEST(CSharp_PatternPlaceholder, OtherBasesCanHostPlaceholders) {
    PM::AnyNode any;
    BlockStatement block;
    SwitchStatement switchStmt;
    TryCatchStatement tryCatch;
    AttributeSection attributeSection;
    ParameterDeclaration parameter;
    VariableInitializer initializer;
    ArrayInitializerExpression arrayInitializer;

    PatternPlaceholderNode<BlockStatement> blockPh(std::make_shared<PM::AnyNode>());
    PatternPlaceholderNode<SwitchStatement> switchPh(std::make_shared<PM::AnyNode>());
    PatternPlaceholderNode<TryCatchStatement> tryPh(std::make_shared<PM::AnyNode>());
    PatternPlaceholderNode<AttributeSection> attrPh(std::make_shared<PM::AnyNode>());
    PatternPlaceholderNode<ParameterDeclaration> paramPh(std::make_shared<PM::AnyNode>());
    PatternPlaceholderNode<VariableInitializer> initPh(std::make_shared<PM::AnyNode>());
    PatternPlaceholderNode<ArrayInitializerExpression> arrayPh(std::make_shared<PM::AnyNode>());

    EXPECT_TRUE(PM::PatternExtensions::IsMatch(blockPh, &block));
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(switchPh, &switchStmt));
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(tryPh, &tryCatch));
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(attrPh, &attributeSection));
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(paramPh, &parameter));
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(initPh, &initializer));
    EXPECT_TRUE(PM::PatternExtensions::IsMatch(arrayPh, &arrayInitializer));

    // The covariant `Clone` returns the base type in each case.
    std::unique_ptr<BlockStatement> blockClone(blockPh.Clone());
    EXPECT_NE(blockClone, nullptr);
    std::unique_ptr<SwitchStatement> switchClone(switchPh.Clone());
    EXPECT_NE(switchClone, nullptr);
}
