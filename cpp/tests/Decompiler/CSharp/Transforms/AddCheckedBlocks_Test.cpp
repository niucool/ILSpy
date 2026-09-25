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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the AST transform `AddCheckedBlocks`: the cost-based placement of
// `checked(...)`/`unchecked(...)` expressions and `checked { ... }`/`unchecked { ... }`
// blocks driven by the `CheckedUncheckedAnnotation` the expression builder attaches to
// casts and assignments. With `CheckForOverflowUnderflow` on the default context is
// checked (so `unchecked` annotations require an explicit wrapper); with it off the
// default is unchecked (the `DecompilerSettings` default), so `checked` annotations require
// one. The tests assert the rewritten tree structure (node identity and the wrapper kinds)
// rather than rendered text.

#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UncheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/CheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UncheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
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

Syntax::PrimitiveExpression* Prim(std::int32_t value) {
    return new Syntax::PrimitiveExpression(value);
}

// A `left + right` expression statement carrying the given operands.
Syntax::ExpressionStatement* MakeAddStatement(Syntax::Expression* left, Syntax::Expression* right) {
    return new Syntax::ExpressionStatement(
        new Syntax::BinaryOperatorExpression(left, Syntax::BinaryOperatorType::Add, right));
}

// The `BinaryOperatorExpression` inside an `ExpressionStatement(x + y)`.
Syntax::BinaryOperatorExpression* AddOf(Syntax::ExpressionStatement* statement) {
    return dynamic_cast<Syntax::BinaryOperatorExpression*>(statement->Expression());
}

} // namespace

// With `CheckForOverflowUnderflow` on the default context is checked, so a nested expression
// annotated `unchecked` is wrapped in an `unchecked(...)` expression. The wrapper wins over a
// block because the tie-break prefers expressions over blocks.
TEST(AddCheckedBlocksTest, WrapsUncheckedAnnotatedNestedExpressionInCheckedContext)
{
    TransformFixture fixture;
    fixture.settings.SetCheckForOverflowUnderflow(true);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* left = Prim(1);
    left->AddAnnotation(Transforms::UncheckedAnnotationHandle());
    auto* right = Prim(2);
    auto* block = new Syntax::BlockStatement();
    auto* statement = MakeAddStatement(left, right);
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), statement);
    auto* wrapper = dynamic_cast<Syntax::UncheckedExpression*>(AddOf(statement)->Left());
    ASSERT_NE(wrapper, nullptr);
    EXPECT_EQ(wrapper->Expression(), left);
}

// With the default (unchecked) context a nested expression annotated `checked` is wrapped in a
// `checked(...)` expression.
TEST(AddCheckedBlocksTest, WrapsCheckedAnnotatedNestedExpressionInUncheckedContext)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* left = Prim(1);
    left->AddAnnotation(Transforms::CheckedAnnotationHandle());
    auto* right = Prim(2);
    auto* block = new Syntax::BlockStatement();
    auto* statement = MakeAddStatement(left, right);
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* wrapper = dynamic_cast<Syntax::CheckedExpression*>(AddOf(statement)->Left());
    ASSERT_NE(wrapper, nullptr);
    EXPECT_EQ(wrapper->Expression(), left);
}

// An annotation matching the ambient context needs no wrapper: with the default unchecked
// context an `unchecked`-annotated nested expression is left alone.
TEST(AddCheckedBlocksTest, MatchingAnnotationIsNotWrapped)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* left = Prim(1);
    left->AddAnnotation(Transforms::UncheckedAnnotationHandle());
    auto* right = Prim(2);
    auto* block = new Syntax::BlockStatement();
    auto* statement = MakeAddStatement(left, right);
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    EXPECT_EQ(AddOf(statement)->Left(), left);
    EXPECT_EQ(AddOf(statement)->Right(), right);
}

// An explicit `unchecked` annotation always forces an `unchecked(...)` expression, even in an
// unchecked context where a plain (non-explicit) `unchecked` annotation would not.
TEST(AddCheckedBlocksTest, ExplicitUncheckedAnnotationAlwaysWraps)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* left = Prim(1);
    left->AddAnnotation(Transforms::ExplicitUncheckedAnnotationHandle());
    auto* right = Prim(2);
    auto* block = new Syntax::BlockStatement();
    auto* statement = MakeAddStatement(left, right);
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    auto* wrapper = dynamic_cast<Syntax::UncheckedExpression*>(AddOf(statement)->Left());
    ASSERT_NE(wrapper, nullptr);
    EXPECT_EQ(wrapper->Expression(), left);
}

// A top-level expression (a direct child of an `ExpressionStatement`) cannot use a
// checked/unchecked expression, so consecutive statements requiring the other context are
// grouped into a single `checked { ... }` block (the default context is unchecked and both
// primitives are annotated `checked`, so the block wraps both statements).
TEST(AddCheckedBlocksTest, GroupsOppositeAnnotatedStatementsIntoBlock)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* firstPrim = Prim(1);
    firstPrim->AddAnnotation(Transforms::CheckedAnnotationHandle());
    auto* secondPrim = Prim(2);
    secondPrim->AddAnnotation(Transforms::CheckedAnnotationHandle());
    auto* block = new Syntax::BlockStatement();
    auto* first = new Syntax::ExpressionStatement(firstPrim);
    auto* second = new Syntax::ExpressionStatement(secondPrim);
    block->Statements().Add(first);
    block->Statements().Add(second);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* checkedStatement = dynamic_cast<Syntax::CheckedStatement*>(block->Statements().At(0));
    ASSERT_NE(checkedStatement, nullptr);
    ASSERT_NE(checkedStatement->Body(), nullptr);
    ASSERT_EQ(checkedStatement->Body()->Statements().Count(), 2);
    EXPECT_EQ(checkedStatement->Body()->Statements().At(0), first);
    EXPECT_EQ(checkedStatement->Body()->Statements().At(1), second);
}

// A label blocks the free movement of statements, so the checked block starts after the label
// (`checkedBlockStart` is reset when the label is processed) and the label stays outside.
TEST(AddCheckedBlocksTest, LabelStaysOutsideCheckedBlock)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* label = new Syntax::LabelStatement("Label");
    auto* prim = Prim(1);
    prim->AddAnnotation(Transforms::CheckedAnnotationHandle());
    auto* statement = new Syntax::ExpressionStatement(prim);
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(label);
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(0), label);
    auto* checkedStatement = dynamic_cast<Syntax::CheckedStatement*>(block->Statements().At(1));
    ASSERT_NE(checkedStatement, nullptr);
    ASSERT_NE(checkedStatement->Body(), nullptr);
    ASSERT_EQ(checkedStatement->Body()->Statements().Count(), 1);
    EXPECT_EQ(checkedStatement->Body()->Statements().At(0), statement);
}

// A local function declaration likewise stays outside the checked block (moving it would put
// it out of scope from its call sites).
TEST(AddCheckedBlocksTest, LocalFunctionStaysOutsideCheckedBlock)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* localFunction = new Syntax::LocalFunctionDeclarationStatement();
    auto* prim = Prim(1);
    prim->AddAnnotation(Transforms::CheckedAnnotationHandle());
    auto* statement = new Syntax::ExpressionStatement(prim);
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(localFunction);
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements().At(0), localFunction);
    auto* checkedStatement = dynamic_cast<Syntax::CheckedStatement*>(block->Statements().At(1));
    ASSERT_NE(checkedStatement, nullptr);
    ASSERT_NE(checkedStatement->Body(), nullptr);
    EXPECT_EQ(checkedStatement->Body()->Statements().At(0), statement);
}

// A nested block's annotation need is subsumed by an enclosing block of the same context:
// the inner `checked`-annotated statement makes the inner block want a checked block, but
// the outer block can cover the inner block itself for the same cost, so a single checked
// block wraps the inner block and the inner statement stays directly in it.
TEST(AddCheckedBlocksTest, NestedBlockIsSubsumedByOuterBlock)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* prim = Prim(1);
    prim->AddAnnotation(Transforms::CheckedAnnotationHandle());
    auto* statement = new Syntax::ExpressionStatement(prim);
    auto* innerBlock = new Syntax::BlockStatement();
    innerBlock->Statements().Add(statement);
    auto* outerBlock = new Syntax::BlockStatement();
    outerBlock->Statements().Add(innerBlock);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*outerBlock, context);

    ASSERT_EQ(outerBlock->Statements().Count(), 1);
    auto* checkedStatement = dynamic_cast<Syntax::CheckedStatement*>(outerBlock->Statements().At(0));
    ASSERT_NE(checkedStatement, nullptr);
    ASSERT_NE(checkedStatement->Body(), nullptr);
    ASSERT_EQ(checkedStatement->Body()->Statements().Count(), 1);
    EXPECT_EQ(checkedStatement->Body()->Statements().At(0), innerBlock);
    ASSERT_EQ(innerBlock->Statements().Count(), 1);
    EXPECT_EQ(innerBlock->Statements().At(0), statement);
}

// A top-level expression whose annotation matches the ambient context needs neither an
// expression wrapper (disallowed for top-level expressions) nor a block, so the statement is
// left untouched.
TEST(AddCheckedBlocksTest, MatchingTopLevelExpressionIsNotWrapped)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* prim = Prim(1);
    prim->AddAnnotation(Transforms::UncheckedAnnotationHandle());
    auto* statement = new Syntax::ExpressionStatement(prim);
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(statement);

    Transforms::AddCheckedBlocks transform;
    transform.Run(*block, context);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), statement);
    EXPECT_EQ(statement->Expression(), prim);
}
