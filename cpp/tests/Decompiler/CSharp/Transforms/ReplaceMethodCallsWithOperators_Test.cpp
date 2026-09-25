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

#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace TSImpl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// The Run fixture (the DeclareVariables_Test RunFixture pattern): the
// compilation the ast-builder's ConvertType consults (the op_Explicit cast)
// plus the using scope the DecompileRun requires.
struct RunFixture {
    TS::SimpleCompilation compilation{TSImpl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CS::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CS::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    Syntax::TypeSystemAstBuilder astBuilder;

    RunFixture()
        : scopelessContext(
              std::make_shared<CS::TypeSystem::CSharpTypeResolveContext>(
                  compilation.MainModule())),
          usingScope(std::make_shared<CS::TypeSystem::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})) {}
};

// An operator-method stub: a FakeMethod named like the operator's metadata
// name, its Int32 return type, a named declaring type.
std::shared_ptr<TSImpl::FakeMethod> OperatorMethod(
    const RunFixture& fx, const std::string& name) {
    auto method = std::make_shared<TSImpl::FakeMethod>(
        fx.compilation, TS::SymbolKind::Operator);
    method->SetName(name);
    method->SetDeclaringType(std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(std::string(), "MyType")));
    // The return type is a real type definition carrying the known-type
    // code (the ConvertType keyword arm reads it off the definition).
    auto returnType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Int32", "System", TS::FullTypeName("System.Int32"),
        TS::TypeKind::Struct, TS::Accessibility::Public, fx.compilation,
        nullptr, TS::KnownTypeCode::Int32);
    method->SetReturnType(TS::ITypePtr(returnType));
    return method;
}

// Keeps the stub methods alive for the program's lifetime (the
// resolve-result annotations store raw pointers -- the PatternStatement
// transform-test keep-alive convention).
std::vector<std::shared_ptr<TSImpl::FakeMethod>>& KeepAlive() {
    static std::vector<std::shared_ptr<TSImpl::FakeMethod>> methods;
    return methods;
}

// An annotated `M.op(...)` invocation over the operator method (the symbol
// the transform reads through the ResolveResult annotation).
Syntax::InvocationExpression* OperatorCall(
    const std::shared_ptr<TSImpl::FakeMethod>& method,
    std::initializer_list<Syntax::Expression*> arguments) {
    KeepAlive().push_back(method);
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(
            new Syntax::IdentifierExpression("M"), "op"));
    for (Syntax::Expression* argument : arguments)
        invocation->Arguments().Add(argument);
    auto target = std::make_shared<Sem::TypeResolveResult>(
        method->DeclaringType());
    invocation->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        target,
        // FakeMethod derives IMember twice (FakeMember and IMethod); pick
        // the FakeMember subobject's IMember base.
        static_cast<TSImpl::FakeMember*>(method.get())));
    return invocation;
}

// Runs the transform's IAstTransform entry over the tree.
void RunPipeline(Syntax::AstNode& root, RunFixture& fx) {
    DecompileRun runStorage(&fx.settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    context.TypeSystemAstBuilder = &fx.astBuilder;
    context.TypeSystem = &fx.compilation;
    CS::Transforms::ReplaceMethodCallsWithOperators transform;
    transform.Run(root, context);
}

} // namespace

// The user-defined-operator core (ReplaceMethodCallsWithOperators.cs lines
// 177-250): `op_Addition(a, b)` over a 2-ary invocation becomes the `a + b`
// binary operator expression, the arguments detached from the invocation
// and the annotations carried over.
TEST(ReplaceMethodCallsWithOperatorsTest, UserDefinedAdditionBecomesBinaryOperator)
{
    RunFixture fx;
    auto* a = new Syntax::IdentifierExpression("a");
    auto* b = new Syntax::IdentifierExpression("b");
    auto* invocation = OperatorCall(OperatorMethod(fx, "op_Addition"), {a, b});
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(invocation));

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(
        block->Statements().At(0)->Children().At(0));
    ASSERT_NE(binary, nullptr)
        << "the invocation becomes a binary operator expression";
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Add);
    EXPECT_EQ(binary->Left(), a) << "the first argument detaches as the left";
    EXPECT_EQ(binary->Right(), b) << "the second argument detaches as the right";
}

// `op_LogicalNot(a)` becomes the `!a` unary operator expression.
TEST(ReplaceMethodCallsWithOperatorsTest, UserDefinedLogicalNotBecomesUnaryOperator)
{
    RunFixture fx;
    auto* a = new Syntax::IdentifierExpression("a");
    auto* invocation = OperatorCall(OperatorMethod(fx, "op_LogicalNot"), {a});
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(invocation));

    RunPipeline(*block, fx);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(
        block->Statements().At(0)->Children().At(0));
    ASSERT_NE(unary, nullptr)
        << "the invocation becomes a unary operator expression";
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
    EXPECT_EQ(unary->Expression(), a);
}

// `op_Explicit(a)` becomes the cast to the operator's return type.
TEST(ReplaceMethodCallsWithOperatorsTest, ConversionOperatorBecomesCast)
{
    RunFixture fx;
    auto* a = new Syntax::IdentifierExpression("a");
    auto* invocation = OperatorCall(OperatorMethod(fx, "op_Explicit"), {a});
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(invocation));

    RunPipeline(*block, fx);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(
        block->Statements().At(0)->Children().At(0));
    ASSERT_NE(cast, nullptr) << "the invocation becomes a cast expression";
    EXPECT_EQ(cast->Expression(), a);
    ASSERT_NE(cast->Type(), nullptr);
    EXPECT_EQ(cast->Type()->ToString(nullptr), "int")
        << "the cast type is the operator's return type";
}

// `op_True(a)` in a condition slot is removed (the condition keeps the
// bare operand).
TEST(ReplaceMethodCallsWithOperatorsTest, OpTrueInConditionIsRemoved)
{
    RunFixture fx;
    auto* a = new Syntax::IdentifierExpression("a");
    auto* invocation = OperatorCall(OperatorMethod(fx, "op_True"), {a});
    auto* ifStatement = new Syntax::IfElseStatement();
    ifStatement->Condition(invocation);
    ifStatement->TrueStatement(new Syntax::BlockStatement());
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(ifStatement);

    RunPipeline(*block, fx);

    auto* ifAfter = dynamic_cast<Syntax::IfElseStatement*>(
        block->Statements().At(0));
    ASSERT_NE(ifAfter, nullptr);
    EXPECT_EQ(ifAfter->Condition(), a)
        << "the op_True call unwraps to its operand";
}

// A direction-wrapped argument (`in`-direction, the compiler's by-ref
// operator-argument form) unwraps into the operator operand.
TEST(ReplaceMethodCallsWithOperatorsTest, DirectionWrappedArgumentUnwraps)
{
    RunFixture fx;
    auto* a = new Syntax::IdentifierExpression("a");
    auto* b = new Syntax::IdentifierExpression("b");
    auto* wrapped = new Syntax::DirectionExpression(
        Syntax::FieldDirection::In, a);
    auto* invocation =
        OperatorCall(OperatorMethod(fx, "op_Addition"), {wrapped, b});
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(invocation));

    RunPipeline(*block, fx);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(
        block->Statements().At(0)->Children().At(0));
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Left(), a)
        << "the in-direction wrapper unwraps off the operand";
}

// A method name outside the operator table leaves the invocation alone.
TEST(ReplaceMethodCallsWithOperatorsTest, PlainMethodNameIsUntouched)
{
    RunFixture fx;
    auto* a = new Syntax::IdentifierExpression("a");
    auto* b = new Syntax::IdentifierExpression("b");
    auto* invocation = OperatorCall(OperatorMethod(fx, "SomeMethod"), {a, b});
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(new Syntax::ExpressionStatement(invocation));

    RunPipeline(*block, fx);

    EXPECT_EQ(block->Statements().At(0)->Children().At(0), invocation)
        << "a non-operator method invocation is unchanged";
}
