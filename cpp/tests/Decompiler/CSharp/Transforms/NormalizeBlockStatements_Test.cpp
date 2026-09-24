// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for NormalizeBlockStatements (the port of
// ICSharpCode.Decompiler/CSharp/Transforms/NormalizeBlockStatements.cs): with
// AlwaysUseBraces every embedded statement (except an `else if`) gains a block
// wrapper; the loop/fixed/lock bodies are always braced; a redundant
// single-statement block is unwrapped when the inner statement may appear as
// an embedded statement; and the single top-level namespace becomes
// file-scoped under FileScopedNamespaces.

#include "Decompiler/CSharp/Transforms/NormalizeBlockStatements.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;

// The fixture: the compilation + the using scope the DecompileRun requires.
struct NormalizeFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CSharpTS::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharpTS::UsingScope> usingScope;

    NormalizeFixture()
        : scopelessContext(std::make_shared<CSharpTS::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharpTS::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})) {}
};

    Syntax::ExpressionStatement* MakeCallStatement(const std::string& name) {
        return new Syntax::ExpressionStatement(
            new Syntax::IdentifierExpression(name));
    }
};

// A while body that is a bare expression statement: the loop arm wraps it in a
// block unconditionally.
TEST(NormalizeBlockStatementsTest, WhileBodyAlwaysGetsABlock)
{
    NormalizeFixture fx;
    auto whileStatement = std::make_unique<Syntax::WhileStatement>();
    whileStatement->EmbeddedStatement(MakeCallStatement("Work"));

    DecompilerSettings settings;
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*whileStatement, context);

    ASSERT_NE(whileStatement->EmbeddedStatement(), nullptr);
    EXPECT_NE(dynamic_cast<Syntax::BlockStatement*>(whileStatement->EmbeddedStatement()),
              nullptr)
        << "the loop body is always a block";
}

// An else-if chain (the FalseStatement slot holding an IfElseStatement) is NOT
// wrapped even under AlwaysUseBraces.
TEST(NormalizeBlockStatementsTest, ElseIfChainIsNotWrapped)
{
    NormalizeFixture fx;
    auto outer = std::make_unique<Syntax::IfElseStatement>();
    auto inner = new Syntax::IfElseStatement();
    outer->TrueStatement(new Syntax::BlockStatement());
    outer->FalseStatement(inner);

    DecompilerSettings settings;
    settings.SetAlwaysUseBraces(true);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*outer, context);

    // The inner if-else is still the direct else branch (not wrapped).
    ASSERT_NE(outer->FalseStatement(), nullptr);
    EXPECT_EQ(outer->FalseStatement(), inner)
        << "an else-if chain keeps its shape under AlwaysUseBraces";
}

// A non-block embedded statement inside an if under AlwaysUseBraces gains a
// wrapper block.
TEST(NormalizeBlockStatementsTest, AlwaysUseBracesWrapsIfBody)
{
    NormalizeFixture fx;
    auto ifStatement = std::make_unique<Syntax::IfElseStatement>();
    ifStatement->TrueStatement(MakeCallStatement("Work"));

    DecompilerSettings settings;
    settings.SetAlwaysUseBraces(true);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*ifStatement, context);

    ASSERT_NE(ifStatement->TrueStatement(), nullptr);
    EXPECT_NE(dynamic_cast<Syntax::BlockStatement*>(ifStatement->TrueStatement()),
              nullptr)
        << "AlwaysUseBraces wraps the if body";
}

// Without AlwaysUseBraces a redundant single-statement block is unwrapped.
TEST(NormalizeBlockStatementsTest, RedundantBlockIsUnwrapped)
{
    NormalizeFixture fx;
    auto ifStatement = std::make_unique<Syntax::IfElseStatement>();
    auto block = new Syntax::BlockStatement();
    block->Statements().Add(MakeCallStatement("Work"));
    ifStatement->TrueStatement(block);

    DecompilerSettings settings;
    settings.SetAlwaysUseBraces(false);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*ifStatement, context);

    ASSERT_NE(ifStatement->TrueStatement(), nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::BlockStatement*>(ifStatement->TrueStatement()),
              nullptr)
        << "the redundant block is removed";
}

// A block whose single statement is a local declaration keeps the wrapper
// (the C# IsAllowedAsEmbeddedStatement rejects a VariableDeclarationStatement).
TEST(NormalizeBlockStatementsTest, DeclarationBlockIsKept)
{
    NormalizeFixture fx;
    auto ifStatement = std::make_unique<Syntax::IfElseStatement>();
    auto block = new Syntax::BlockStatement();
    block->Statements().Add(
        new Syntax::VariableDeclarationStatement(Syntax::Modifiers::None,
                                                 new Syntax::PrimitiveType("int")));
    ifStatement->TrueStatement(block);

    DecompilerSettings settings;
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*ifStatement, context);

    ASSERT_NE(ifStatement->TrueStatement(), nullptr);
    EXPECT_NE(dynamic_cast<Syntax::BlockStatement*>(ifStatement->TrueStatement()),
              nullptr);
}

// The file-scoped namespace arm: a single top-level namespace becomes
// file-scoped under FileScopedNamespaces.
TEST(NormalizeBlockStatementsTest, SingleNamespaceBecomesFileScoped)
{
    NormalizeFixture fx;
    auto syntaxTree = std::make_unique<Syntax::SyntaxTree>();
    auto ns = std::make_unique<Syntax::NamespaceDeclaration>();
    Syntax::NamespaceDeclaration* nsPtr = ns.get();
    syntaxTree->Members().Add(ns.release());

    DecompilerSettings settings;
    settings.SetFileScopedNamespaces(true);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*syntaxTree, context);

    EXPECT_TRUE(nsPtr->IsFileScoped());
}

// Two sibling namespaces: none becomes file-scoped (the C# single-namespace
// gate).
TEST(NormalizeBlockStatementsTest, TwoNamespacesStayBraced)
{
    NormalizeFixture fx;
    auto syntaxTree = std::make_unique<Syntax::SyntaxTree>();
    Syntax::NamespaceDeclaration* ns1 = new Syntax::NamespaceDeclaration();
    Syntax::NamespaceDeclaration* ns2 = new Syntax::NamespaceDeclaration();
    syntaxTree->Members().Add(ns1);
    syntaxTree->Members().Add(ns2);

    DecompilerSettings settings;
    settings.SetFileScopedNamespaces(true);
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    CS::Transforms::NormalizeBlockStatements transform;
    transform.Run(*syntaxTree, context);

    EXPECT_FALSE(ns1->IsFileScoped());
    EXPECT_FALSE(ns2->IsFileScoped());
}