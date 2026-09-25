// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the AST transform `FlattenSwitchBlocks`: a switch section whose sole
// statement is a block with no local declaration has the block unwrapped (its statements
// move up into the section), while a section with multiple statements, a non-block single
// statement, or a block carrying a local declaration (variable declaration statement,
// local function declaration statement, or out-var declaration expression, including one
// nested in an expression statement) is left untouched. The transform only rewrites the
// tree structurally, so the tests assert statement counts and node identity rather than
// rendered text.

#include "Decompiler/CSharp/Transforms/FlattenSwitchBlocks.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the RemoveCompilerGeneratedAssemblyAttributes suite shape).
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A `switch (x) { section }` tree: the switch holds one section carrying the given
// statements (the transform walks SwitchSection descendants, so the switch merely hosts
// them in a realistic position).
Syntax::SwitchStatement* MakeSwitchWithSection(std::vector<Syntax::Statement*> statements) {
    auto* section = new Syntax::SwitchSection();
    for (Syntax::Statement* statement : statements)
        section->Statements().Add(statement);
    auto* switchStatement = new Syntax::SwitchStatement(new Syntax::IdentifierExpression("x"));
    switchStatement->SwitchSections().Add(section);
    return switchStatement;
}

Syntax::BlockStatement* MakeBlock(std::vector<Syntax::Statement*> statements) {
    auto* block = new Syntax::BlockStatement();
    for (Syntax::Statement* statement : statements)
        block->Statements().Add(statement);
    return block;
}

Syntax::SwitchSection* SectionOf(Syntax::SwitchStatement* switchStatement) {
    return switchStatement->SwitchSections().At(0);
}

} // namespace

// A section whose sole statement is a block of two statements has the block unwrapped and
// its statements moved up into the section, in order, as the same node instances.
TEST(FlattenSwitchBlocksTest, FlattensSingleBlockSection)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* first = new Syntax::BreakStatement();
    auto* second = new Syntax::EmptyStatement();
    auto* block = MakeBlock({first, second});
    auto* switchStatement = MakeSwitchWithSection({block});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 2);
    EXPECT_EQ(section->Statements().At(0), first);
    EXPECT_EQ(section->Statements().At(1), second);
}

// A section with more than one statement is never flattened, even when one of them is a
// block (the C# `Statements.Count != 1` guard).
TEST(FlattenSwitchBlocksTest, KeepsSectionWithMultipleStatements)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* block = MakeBlock({new Syntax::BreakStatement()});
    auto* switchStatement = MakeSwitchWithSection({block, new Syntax::EmptyStatement()});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 2);
    EXPECT_EQ(section->Statements().At(0), block);
}

// A section whose single statement is not a block is left untouched.
TEST(FlattenSwitchBlocksTest, KeepsNonBlockSingleStatement)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* breakStatement = new Syntax::BreakStatement();
    auto* switchStatement = MakeSwitchWithSection({breakStatement});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_EQ(section->Statements().At(0), breakStatement);
}

// A block carrying a variable declaration statement is kept (flattening would change the
// declaration's scope).
TEST(FlattenSwitchBlocksTest, KeepsBlockWithVariableDeclaration)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::SimpleType("int"), "value");
    auto* block = MakeBlock({declaration});
    auto* switchStatement = MakeSwitchWithSection({block});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_EQ(section->Statements().At(0), block);
}

// A block carrying a local function declaration statement is kept.
TEST(FlattenSwitchBlocksTest, KeepsBlockWithLocalFunctionDeclaration)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* localFunction = new Syntax::LocalFunctionDeclarationStatement();
    auto* block = MakeBlock({localFunction});
    auto* switchStatement = MakeSwitchWithSection({block});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_EQ(section->Statements().At(0), block);
}

// A block carrying an out-var declaration expression (directly as the single statement) is
// kept -- the `node is OutVarDeclarationExpression` arm.
TEST(FlattenSwitchBlocksTest, KeepsBlockWithOutVarDeclaration)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* outVar = new Syntax::OutVarDeclarationExpression(new Syntax::SimpleType("int"), "value");
    auto* block = MakeBlock({new Syntax::ExpressionStatement(outVar)});
    auto* switchStatement = MakeSwitchWithSection({block});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_EQ(section->Statements().At(0), block);
}

// A block whose single statement is a nested block carrying a declaration IS flattened:
// `ContainsLocalDeclaration` stops at the nested block, whose own scope keeps the
// declaration valid, so the outer braces are still redundant.
TEST(FlattenSwitchBlocksTest, FlattensOuterBlockWithNestedDeclarationBlock)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::SimpleType("int"), "value");
    auto* inner = MakeBlock({declaration});
    auto* outer = MakeBlock({inner});
    auto* switchStatement = MakeSwitchWithSection({outer});
    tree.Members().Add(switchStatement);
    auto* section = SectionOf(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_EQ(section->Statements().At(0), inner);
}

// Two sections are each processed independently: the flattenable one is unwrapped and the
// one carrying a declaration is kept.
TEST(FlattenSwitchBlocksTest, ProcessesSectionsIndependently)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* breakStatement = new Syntax::BreakStatement();
    auto* plainBlock = MakeBlock({breakStatement});
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::SimpleType("int"), "value");
    auto* declarationBlock = MakeBlock({declaration});

    auto* firstSection = new Syntax::SwitchSection();
    firstSection->Statements().Add(plainBlock);
    auto* secondSection = new Syntax::SwitchSection();
    secondSection->Statements().Add(declarationBlock);
    auto* switchStatement = new Syntax::SwitchStatement(new Syntax::IdentifierExpression("x"));
    switchStatement->SwitchSections().Add(firstSection);
    switchStatement->SwitchSections().Add(secondSection);
    tree.Members().Add(switchStatement);

    Transforms::FlattenSwitchBlocks transform;
    transform.Run(tree, context);

    ASSERT_EQ(firstSection->Statements().Count(), 1);
    EXPECT_EQ(firstSection->Statements().At(0), breakStatement);
    ASSERT_EQ(secondSection->Statements().Count(), 1);
    EXPECT_EQ(secondSection->Statements().At(0), declarationBlock);
}
