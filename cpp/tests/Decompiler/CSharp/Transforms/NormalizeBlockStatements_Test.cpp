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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the AST transform `NormalizeBlockStatements`: the embedded-statement brace
// normalization (a legal brace-less arm stays bare, an illegal one is wrapped in a block,
// a redundant single-statement block under an `if`/`using` is unwrapped, and
// `AlwaysUseBraces` wraps every non-else arm), the empty-statement drop, the file-scoped
// namespace marking, and the calculated getter-only property/indexer expression-body
// rewrite. The transform rewrites the tree structurally, so the tests assert node
// identities and counts rather than rendered text.

#include "Decompiler/CSharp/Transforms/NormalizeBlockStatements.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/IndexerDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
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

// The TransformContext fixture (the FlattenSwitchBlocks suite shape).
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

Syntax::Expression* Ref(std::string name) {
    return new Syntax::IdentifierExpression(std::move(name));
}

Syntax::ExpressionStatement* Stmt() {
    return new Syntax::ExpressionStatement(Ref("x"));
}

Syntax::BlockStatement* MakeBlock(std::vector<Syntax::Statement*> statements) {
    auto* block = new Syntax::BlockStatement();
    for (Syntax::Statement* statement : statements)
        block->Statements().Add(statement);
    return block;
}

Syntax::PropertyDeclaration* MakeCalculatedProperty(Syntax::Expression* expression,
                                                    Syntax::Modifiers getterModifiers = Syntax::Modifiers::None) {
    auto* property = new Syntax::PropertyDeclaration();
    property->Name("P");
    auto* getter = new Syntax::Accessor(Syntax::AccessorKind::Getter);
    getter->Modifiers(getterModifiers);
    getter->Body(MakeBlock({new Syntax::ReturnStatement(expression)}));
    property->Getter(getter);
    return property;
}

Syntax::IndexerDeclaration* MakeCalculatedIndexer(Syntax::Expression* expression) {
    auto* indexer = new Syntax::IndexerDeclaration();
    auto* getter = new Syntax::Accessor(Syntax::AccessorKind::Getter);
    getter->Body(MakeBlock({new Syntax::ReturnStatement(expression)}));
    indexer->Getter(getter);
    return indexer;
}

} // namespace

// A bare single-statement `if` true arm is a legal embedded statement, so it stays bare
// (the default arm allows it because the `if` is not itself an `if` arm).
TEST(NormalizeBlockStatementsTest, KeepsLegalBareIfTrueArm)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* ifStatement = new Syntax::IfElseStatement(Ref("c"), statement);
    tree.Members().Add(ifStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(ifStatement->TrueStatement(), statement);
}

// A redundant single-statement block as the `if` true arm is unwrapped: the inner statement
// replaces the block directly.
TEST(NormalizeBlockStatementsTest, UnwrapsRedundantBlockUnderIfTrueArm)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* ifStatement = new Syntax::IfElseStatement(Ref("c"), MakeBlock({statement}));
    tree.Members().Add(ifStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(ifStatement->TrueStatement(), statement);
}

// A `while` body that is not a block is always wrapped, even when a bare embedded
// statement would be legal.
TEST(NormalizeBlockStatementsTest, WrapsBareWhileBody)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* whileStatement = new Syntax::WhileStatement(Ref("c"), statement);
    tree.Members().Add(whileStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(whileStatement->EmbeddedStatement());
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), statement);
}

// A `do`/`while` body that is not a block is wrapped the same way.
TEST(NormalizeBlockStatementsTest, WrapsBareDoWhileBody)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* doWhile = new Syntax::DoWhileStatement(Ref("c"), statement);
    tree.Members().Add(doWhile);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    ASSERT_NE(dynamic_cast<Syntax::BlockStatement*>(doWhile->EmbeddedStatement()), nullptr);
}

// An existing body block is left as-is.
TEST(NormalizeBlockStatementsTest, KeepsExistingLoopBodyBlock)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* block = MakeBlock({Stmt()});
    auto* whileStatement = new Syntax::WhileStatement(Ref("c"), block);
    tree.Members().Add(whileStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(whileStatement->EmbeddedStatement(), block);
}

// A childless `;` body is dropped and the new block stays empty (the empty statement is
// not added).
TEST(NormalizeBlockStatementsTest, DropsChildlessEmptyStatementBody)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* empty = new Syntax::EmptyStatement();
    auto* whileStatement = new Syntax::WhileStatement(Ref("c"), empty);
    tree.Members().Add(whileStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(whileStatement->EmbeddedStatement());
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->Statements().Count(), 0);
}

// A `lock` body that is not a block is wrapped.
TEST(NormalizeBlockStatementsTest, WrapsBareLockBody)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* lockStatement = new Syntax::LockStatement(Ref("c"), statement);
    tree.Members().Add(lockStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(lockStatement->EmbeddedStatement());
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->Statements().At(0), statement);
}

// A variable declaration statement is never legal as an embedded statement: an `if` arm
// carrying one is wrapped.
TEST(NormalizeBlockStatementsTest, WrapsVariableDeclarationIfArm)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::SimpleType("int"), "x");
    auto* ifStatement = new Syntax::IfElseStatement(Ref("c"), declaration);
    tree.Members().Add(ifStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(ifStatement->TrueStatement());
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->Statements().At(0), declaration);
}

// An `if` directly nested as another `if`'s true arm is wrapped (it cannot stay brace-less
// there without changing the dangling-else binding).
TEST(NormalizeBlockStatementsTest, WrapsNestedIfAsTrueArm)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* inner = new Syntax::IfElseStatement(Ref("b"), Stmt());
    auto* outer = new Syntax::IfElseStatement(Ref("a"), inner);
    tree.Members().Add(outer);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(outer->TrueStatement());
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->Statements().At(0), inner);
}

// The dangling-else shape: the inner `if`'s else arm is wrapped because the inner `if` is
// itself an arm of an outer `if` (the default arm's `parent.Parent is IfElseStatement`).
TEST(NormalizeBlockStatementsTest, WrapsDanglingElseInnerFalseArm)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* elseStatement = Stmt();
    auto* inner = new Syntax::IfElseStatement(Ref("b"), Stmt(), elseStatement);
    auto* outer = new Syntax::IfElseStatement(Ref("a"), inner);
    tree.Members().Add(outer);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    // The inner if is wrapped as the outer true arm.
    auto* outerBlock = dynamic_cast<Syntax::BlockStatement*>(outer->TrueStatement());
    ASSERT_NE(outerBlock, nullptr);
    EXPECT_EQ(outerBlock->Statements().At(0), inner);
    // The else arm is wrapped in turn.
    auto* elseBlock = dynamic_cast<Syntax::BlockStatement*>(inner->FalseStatement());
    ASSERT_NE(elseBlock, nullptr);
    EXPECT_EQ(elseBlock->Statements().At(0), elseStatement);
}

// With `AlwaysUseBraces` on, a bare true arm is wrapped (`IsElseIf` is false for it).
TEST(NormalizeBlockStatementsTest, AlwaysUseBracesWrapsTrueArm)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* ifStatement = new Syntax::IfElseStatement(Ref("c"), statement);
    tree.Members().Add(ifStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    auto* block = dynamic_cast<Syntax::BlockStatement*>(ifStatement->TrueStatement());
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->Statements().At(0), statement);
}

// With `AlwaysUseBraces` on, a non-if else arm is NOT wrapped (`IsElseIf` is true for any
// statement sitting in the false slot), matching the C# -- only `if`/`else if` chains stay
// brace-less there.
TEST(NormalizeBlockStatementsTest, AlwaysUseBracesKeepsElseArmBare)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* elseStatement = Stmt();
    auto* ifStatement = new Syntax::IfElseStatement(Ref("c"), Stmt(), elseStatement);
    tree.Members().Add(ifStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(ifStatement->FalseStatement(), elseStatement);
}

// A `using` body block with a single legal statement is unwrapped.
TEST(NormalizeBlockStatementsTest, UnwrapsUsingBodyBlock)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* statement = Stmt();
    auto* usingStatement = new Syntax::UsingStatement(Ref("r"), MakeBlock({statement}));
    tree.Members().Add(usingStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(usingStatement->EmbeddedStatement(), statement);
}

// A `using` body block whose inner statement needs braces (a `while`) is kept.
TEST(NormalizeBlockStatementsTest, KeepsUsingBodyBlockWithLoop)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysUseBraces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* whileStatement = new Syntax::WhileStatement(Ref("c"), Stmt());
    auto* block = MakeBlock({whileStatement});
    auto* usingStatement = new Syntax::UsingStatement(Ref("r"), block);
    tree.Members().Add(usingStatement);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(usingStatement->EmbeddedStatement(), block);
    EXPECT_EQ(whileStatement->EmbeddedStatement() != nullptr, true);
}

// With `FileScopedNamespaces` on, a tree holding a single namespace marks it file-scoped.
TEST(NormalizeBlockStatementsTest, MarksSingleNamespaceFileScoped)
{
    TransformFixture fixture;
    fixture.settings.SetFileScopedNamespaces(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* namespaceDeclaration = new Syntax::NamespaceDeclaration("N");
    tree.Members().Add(namespaceDeclaration);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_TRUE(namespaceDeclaration->IsFileScoped());
}

// With the setting off, the single namespace stays block-scoped.
TEST(NormalizeBlockStatementsTest, DoesNotMarkFileScopedWhenSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetFileScopedNamespaces(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* namespaceDeclaration = new Syntax::NamespaceDeclaration("N");
    tree.Members().Add(namespaceDeclaration);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_FALSE(namespaceDeclaration->IsFileScoped());
}

// A tree with two namespaces marks neither (only the first-seen is a candidate, and the
// second visit clears it).
TEST(NormalizeBlockStatementsTest, DoesNotMarkFileScopedWithMultipleNamespaces)
{
    TransformFixture fixture;
    fixture.settings.SetFileScopedNamespaces(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* first = new Syntax::NamespaceDeclaration("A");
    auto* second = new Syntax::NamespaceDeclaration("B");
    tree.Members().Add(first);
    tree.Members().Add(second);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_FALSE(first->IsFileScoped());
    EXPECT_FALSE(second->IsFileScoped());
}

// A calculated getter-only property is rewritten to an expression body: the return
// expression is detached onto the property and the getter is removed.
TEST(NormalizeBlockStatementsTest, SimplifiesCalculatedGetterOnlyProperty)
{
    TransformFixture fixture;
    fixture.settings.SetUseExpressionBodyForCalculatedGetterOnlyProperties(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* expression = Ref("value");
    auto* property = MakeCalculatedProperty(expression);
    tree.Members().Add(property);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(property->ExpressionBody(), expression);
    EXPECT_EQ(property->Getter(), nullptr);
}

// A `readonly` accessor modifier is hoisted onto the property declaration.
TEST(NormalizeBlockStatementsTest, HoistsReadonlyAccessorModifier)
{
    TransformFixture fixture;
    fixture.settings.SetUseExpressionBodyForCalculatedGetterOnlyProperties(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* property = MakeCalculatedProperty(Ref("value"), Syntax::Modifiers::Readonly);
    tree.Members().Add(property);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(property->Modifiers(), Syntax::Modifiers::Readonly);
    EXPECT_EQ(property->Getter(), nullptr);
}

// A getter with any modifier other than `readonly` is left alone.
TEST(NormalizeBlockStatementsTest, KeepsGetterWithExtraModifier)
{
    TransformFixture fixture;
    fixture.settings.SetUseExpressionBodyForCalculatedGetterOnlyProperties(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* property = MakeCalculatedProperty(Ref("value"),
                                            Syntax::Modifiers::Readonly | Syntax::Modifiers::Static);
    tree.Members().Add(property);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(property->ExpressionBody(), nullptr);
    EXPECT_NE(property->Getter(), nullptr);
}

// The setting off leaves the property untouched.
TEST(NormalizeBlockStatementsTest, DoesNotSimplifyPropertyWhenSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetUseExpressionBodyForCalculatedGetterOnlyProperties(false);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* property = MakeCalculatedProperty(Ref("value"));
    tree.Members().Add(property);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(property->ExpressionBody(), nullptr);
    EXPECT_NE(property->Getter(), nullptr);
}

// A getter whose body carries more than one statement is not a calculated getter-only
// property and stays a block body.
TEST(NormalizeBlockStatementsTest, KeepsGetterWithMultipleStatements)
{
    TransformFixture fixture;
    fixture.settings.SetUseExpressionBodyForCalculatedGetterOnlyProperties(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* property = new Syntax::PropertyDeclaration();
    property->Name("P");
    auto* getter = new Syntax::Accessor(Syntax::AccessorKind::Getter);
    getter->Body(MakeBlock({Stmt(), new Syntax::ReturnStatement(Ref("value"))}));
    property->Getter(getter);
    tree.Members().Add(property);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(property->ExpressionBody(), nullptr);
    EXPECT_NE(property->Getter(), nullptr);
}

// A calculated getter-only indexer is rewritten to an expression body the same way.
TEST(NormalizeBlockStatementsTest, SimplifiesCalculatedGetterOnlyIndexer)
{
    TransformFixture fixture;
    fixture.settings.SetUseExpressionBodyForCalculatedGetterOnlyProperties(true);
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* expression = Ref("value");
    auto* indexer = MakeCalculatedIndexer(expression);
    tree.Members().Add(indexer);

    Transforms::NormalizeBlockStatements transform;
    transform.Run(tree, context);

    EXPECT_EQ(indexer->ExpressionBody(), expression);
    EXPECT_EQ(indexer->Getter(), nullptr);
}
