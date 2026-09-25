// Copyright (c) 2026 Jun Cai
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

#include "Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace TSImpl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;

using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;

// The Run fixture (the DeclareVariables_Test RunFixture pattern).
struct RunFixture {
    TS::SimpleCompilation compilation{TSImpl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CS::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CS::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;

    RunFixture()
        : scopelessContext(
              std::make_shared<CS::TypeSystem::CSharpTypeResolveContext>(
                  compilation.MainModule())),
          usingScope(std::make_shared<CS::TypeSystem::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})) {}
};

// A pointer-kind type stub (the IsUnsafeType Kind check).
TS::ITypePtr PointerType(const RunFixture& fx) {
    static std::vector<
        std::shared_ptr<TS::TestSupport::LookupTypeDefinition>> keepAlive;
    auto type = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Void*", "System", TS::FullTypeName("System.Void*"),
        TS::TypeKind::Pointer, TS::Accessibility::Public, fx.compilation,
        nullptr);
    keepAlive.push_back(type);
    return type;
}

// A method declaration with a block body.
Syntax::MethodDeclaration* MethodWithBody(Syntax::Statement* statement) {
    auto* method = new Syntax::MethodDeclaration();
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(statement);
    method->Body(body);
    return method;
}

void RunPipeline(Syntax::AstNode& root, RunFixture& fx) {
    DecompileRun runStorage(&fx.settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    context.TypeSystem = &fx.compilation;
    CS::Transforms::IntroduceUnsafeModifier transform;
    transform.Run(root, context);
}

} // namespace

// The static IsUnsafe probe: a pointer-reference expression in the tree.
TEST(IntroduceUnsafeModifierTest, IsUnsafeDetectsPointerReference)
{
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* use = new Syntax::ExpressionStatement(
        new Syntax::PointerReferenceExpression(
            new Syntax::IdentifierExpression("p"), "M"));
    block->Statements().Add(use);

    EXPECT_TRUE(CS::Transforms::IntroduceUnsafeModifier::IsUnsafe(
        block.get()))
        << "the pointer-reference usage marks the tree unsafe";
}

// A plain statement tree stays safe.
TEST(IntroduceUnsafeModifierTest, IsUnsafeRejectsPlainTree)
{
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::IdentifierExpression("x")));

    EXPECT_FALSE(CS::Transforms::IntroduceUnsafeModifier::IsUnsafe(
        block.get()))
        << "no pointer usage: the tree stays safe";
}

// A pointer-typed composed type (the `T*` AstType) marks the tree unsafe.
TEST(IntroduceUnsafeModifierTest, IsUnsafeDetectsPointerRankType)
{
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* composedType = new Syntax::ComposedType();
    composedType->PointerRank(1);
    composedType->BaseType(new Syntax::SimpleType("Byte"));
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::SizeOfExpression(composedType)));

    EXPECT_TRUE(CS::Transforms::IntroduceUnsafeModifier::IsUnsafe(
        block.get()))
        << "the pointer-rank type reference marks the tree unsafe";
}

// A sizeof expression marks the tree unsafe (sizeof(MyStruct) requires
// unsafe).
TEST(IntroduceUnsafeModifierTest, IsUnsafeDetectsSizeOf)
{
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::SizeOfExpression()));

    EXPECT_TRUE(CS::Transforms::IntroduceUnsafeModifier::IsUnsafe(
        block.get()))
        << "the sizeof usage marks the tree unsafe";
}

// The member declaration containing the unsafe usage gets the `unsafe`
// modifier.
TEST(IntroduceUnsafeModifierTest, UnsafeMemberGetsTheModifier)
{
    RunFixture fx;
    auto* method = MethodWithBody(new Syntax::ExpressionStatement(
        new Syntax::PointerReferenceExpression(
            new Syntax::IdentifierExpression("p"), "M")));

    RunPipeline(*method, fx);

    EXPECT_TRUE(method->HasModifier(Syntax::Modifiers::Unsafe))
        << "the method containing the pointer usage is marked unsafe";
}

// A safe member stays unmarked.
TEST(IntroduceUnsafeModifierTest, SafeMemberStaysUnmarked)
{
    RunFixture fx;
    auto* method = MethodWithBody(new Syntax::ExpressionStatement(
        new Syntax::IdentifierExpression("x")));

    RunPipeline(*method, fx);

    EXPECT_FALSE(method->HasModifier(Syntax::Modifiers::Unsafe))
        << "a method without pointer usage is not marked";
}

// `*(ptr + i)` becomes `ptr[i]` (the pointer-addition indexer form), with
// the annotations carried from both the dereference and the addition.
TEST(IntroduceUnsafeModifierTest, PointerAdditionBecomesIndexer)
{
    RunFixture fx;
    auto* ptr = new Syntax::IdentifierExpression("ptr");
    auto* index = new Syntax::IdentifierExpression("i");
    auto* addition = new Syntax::BinaryOperatorExpression(
        ptr, Syntax::BinaryOperatorType::Add, index);
    // The operator resolve result: the first operand's type is a pointer.
    addition->AddAnnotation(std::make_shared<Sem::OperatorResolveResult>(
        PointerType(fx), TS::ExpressionType::Add,
        std::vector<std::shared_ptr<Sem::ResolveResult>>{
            std::make_shared<Sem::TypeResolveResult>(PointerType(fx))}));
    auto* dereference = new Syntax::UnaryOperatorExpression(
        addition, Syntax::UnaryOperatorType::Dereference);
    auto* method = MethodWithBody(
        new Syntax::ExpressionStatement(dereference));

    RunPipeline(*method, fx);

    EXPECT_TRUE(method->HasModifier(Syntax::Modifiers::Unsafe));
    auto* statement = dynamic_cast<Syntax::ExpressionStatement*>(
        method->Body()->Statements().At(0));
    ASSERT_NE(statement, nullptr);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(
        statement->Expression());
    ASSERT_NE(indexer, nullptr)
        << "the dereferenced pointer addition becomes the indexer";
    EXPECT_EQ(indexer->Target(), ptr);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    EXPECT_EQ(indexer->Arguments().At(0), index);
}

// `(*ptr).Member` becomes `ptr->Member` (the pointer member access).
TEST(IntroduceUnsafeModifierTest, DereferenceMemberAccessBecomesPointerReference)
{
    RunFixture fx;
    auto* ptr = new Syntax::IdentifierExpression("ptr");
    auto* dereference = new Syntax::UnaryOperatorExpression(
        ptr, Syntax::UnaryOperatorType::Dereference);
    auto* member = new Syntax::MemberReferenceExpression(dereference,
                                                         "Member");
    auto* method = MethodWithBody(
        new Syntax::ExpressionStatement(member));

    RunPipeline(*method, fx);

    EXPECT_TRUE(method->HasModifier(Syntax::Modifiers::Unsafe));
    auto* statement = dynamic_cast<Syntax::ExpressionStatement*>(
        method->Body()->Statements().At(0));
    ASSERT_NE(statement, nullptr);
    auto* pointerReference =
        dynamic_cast<Syntax::PointerReferenceExpression*>(
            statement->Expression());
    ASSERT_NE(pointerReference, nullptr)
        << "the member access over a dereference becomes the pointer "
           "member access";
    EXPECT_EQ(pointerReference->Target(), ptr);
    EXPECT_EQ(pointerReference->MemberName(), "Member");
}
