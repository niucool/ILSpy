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

// Tests for FlattenSwitchBlocks (the port of
// ICSharpCode.Decompiler/CSharp/Transforms/FlattenSwitchBlocks.cs): a switch
// section whose only statement is a plain block (no local declarations inside)
// loses the block wrapper; a block containing (directly or through nested
// blocks) a VariableDeclarationStatement / LocalFunctionDeclarationStatement /
// OutVarDeclarationExpression keeps it, and a multi-statement section is not
// touched.

#include "Decompiler/CSharp/Transforms/FlattenSwitchBlocks.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace ILSpy::Decompiler::CSharp::Syntax;

// A `VariableDeclarationStatement` (the declaration shape the flatten
// detector aborts on).
std::unique_ptr<VariableDeclarationStatement> MakeLocalInt() {
    return std::make_unique<VariableDeclarationStatement>(
        Modifiers::None, new PrimitiveType("int"));
}

} // namespace

// The full rewrite: a section whose single statement is a declaration-free
// block loses the wrapper (the block's statements move into the section).
TEST(FlattenSwitchBlocksTest, PlainBlockIsFlattened)
{
    auto switchStatement = std::make_unique<SwitchStatement>();
    switchStatement->Expression(new NullReferenceExpression());
    SwitchSection* section = new SwitchSection();
    switchStatement->SwitchSections().Add(section);
    section->CaseLabels().Add(new CaseLabel());
    auto block = std::make_unique<BlockStatement>();
    block->Statements().Add(new ExpressionStatement(new IdentifierExpression("x")));
    section->Statements().Add(block.release());

    ILSpy::Decompiler::CSharp::Transforms::TransformContext context;
    ILSpy::Decompiler::CSharp::Transforms::FlattenSwitchBlocks transform;
    transform.Run(*switchStatement, context);

    // The block wrapper is gone: the section directly holds the expression
    // statement.
    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_NE(dynamic_cast<ExpressionStatement*>(section->Statements().At(0)), nullptr)
        << "the wrapper block is replaced by its single statement";
}

// A block containing a local declaration keeps the wrapper.
TEST(FlattenSwitchBlocksTest, BlockWithLocalDeclarationKeepsWrapper)
{
    auto switchStatement = std::make_unique<SwitchStatement>();
    switchStatement->Expression(new NullReferenceExpression());
    SwitchSection* section = new SwitchSection();
    switchStatement->SwitchSections().Add(section);
    auto block = std::make_unique<BlockStatement>();
    block->Statements().Add(MakeLocalInt().release());
    section->Statements().Add(block.release());

    ILSpy::Decompiler::CSharp::Transforms::TransformContext context;
    ILSpy::Decompiler::CSharp::Transforms::FlattenSwitchBlocks transform;
    transform.Run(*switchStatement, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_NE(dynamic_cast<BlockStatement*>(section->Statements().At(0)), nullptr)
        << "a declaration inside the block aborts the flatten";
}

// A nested block is transparent: a declaration two levels deep still aborts
// the flatten (the C# ContainsLocalDeclaration recursion).
TEST(FlattenSwitchBlocksTest, NestedDeclarationThroughTransparentBlockAborts)
{
    auto switchStatement = std::make_unique<SwitchStatement>();
    switchStatement->Expression(new NullReferenceExpression());
    SwitchSection* section = new SwitchSection();
    switchStatement->SwitchSections().Add(section);
    auto outer = std::make_unique<BlockStatement>();
    auto inner = std::make_unique<BlockStatement>();
    inner->Statements().Add(MakeLocalInt().release());
    outer->Statements().Add(inner.release());
    section->Statements().Add(outer.release());

    ILSpy::Decompiler::CSharp::Transforms::TransformContext context;
    ILSpy::Decompiler::CSharp::Transforms::FlattenSwitchBlocks transform;
    transform.Run(*switchStatement, context);

    ASSERT_EQ(section->Statements().Count(), 1);
    EXPECT_NE(dynamic_cast<BlockStatement*>(section->Statements().At(0)), nullptr);
}

// A multi-statement section is not touched.
TEST(FlattenSwitchBlocksTest, MultiStatementSectionIsUntouched)
{
    auto switchStatement = std::make_unique<SwitchStatement>();
    switchStatement->Expression(new NullReferenceExpression());
    SwitchSection* section = new SwitchSection();
    switchStatement->SwitchSections().Add(section);
    auto block = std::make_unique<BlockStatement>();
    block->Statements().Add(new ExpressionStatement(new IdentifierExpression("x")));
    section->Statements().Add(block.release());
    section->Statements().Add(new ExpressionStatement(new IdentifierExpression("y")));

    ILSpy::Decompiler::CSharp::Transforms::TransformContext context;
    ILSpy::Decompiler::CSharp::Transforms::FlattenSwitchBlocks transform;
    transform.Run(*switchStatement, context);

    ASSERT_EQ(section->Statements().Count(), 2);
    EXPECT_NE(dynamic_cast<BlockStatement*>(section->Statements().At(0)), nullptr);
}

// The step hook fires for the flattened block.
TEST(FlattenSwitchBlocksTest, StepHookReportsTheFlatten)
{
    auto switchStatement = std::make_unique<SwitchStatement>();
    switchStatement->Expression(new NullReferenceExpression());
    SwitchSection* section = new SwitchSection();
    switchStatement->SwitchSections().Add(section);
    auto block = std::make_unique<BlockStatement>();
    block->Statements().Add(new ExpressionStatement(new IdentifierExpression("x")));
    section->Statements().Add(block.release());

    std::vector<std::string> steps;
    ILSpy::Decompiler::CSharp::Transforms::TransformContext context;
    context.Step = [&steps](const std::string& what, const void*) {
        steps.push_back(what);
    };
    ILSpy::Decompiler::CSharp::Transforms::FlattenSwitchBlocks transform;
    transform.Run(*switchStatement, context);

    ASSERT_EQ(steps.size(), 1u);
    EXPECT_EQ(steps[0], "Flatten switch section block");
}