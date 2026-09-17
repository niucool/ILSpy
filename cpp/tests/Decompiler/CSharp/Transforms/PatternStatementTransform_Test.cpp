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

// Tests for the `PatternStatementTransform` skeleton and the two purely-structural
// sub-transforms landed so far: the conditional-logic reassociation (`a && (b && c)` ->
// `(a && b) && c`) and the negated-equality rewrite (`!(a == b)` -> `a != b`). The
// pattern-based sub-transforms are not ported yet, so this suite drives the structure the
// `VisitChildren` replace-and-revisit walk depends on. The transform rewrites the tree
// structurally, so the tests assert node identity and shape rather than rendered text.

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the NormalizeBlockStatements suite shape).
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

Syntax::Expression* Ref(const char* name) {
    return new Syntax::IdentifierExpression(name);
}

Syntax::BinaryOperatorExpression* Bin(Syntax::Expression* left, Syntax::BinaryOperatorType op,
                                      Syntax::Expression* right) {
    return new Syntax::BinaryOperatorExpression(left, op, right);
}

// Runs the transform over a fresh block holding one expression statement and returns the
// (possibly replaced) statement expression afterward.
Syntax::Expression* RunOnExpression(TransformFixture& fixture, Syntax::Expression* expression) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    auto* statement = new Syntax::ExpressionStatement(expression);
    block->Statements().Add(statement);
    Transforms::PatternStatementTransform transform;
    transform.Run(*block, context);
    return statement->Expression();
}

} // namespace

// `!(a == b)` becomes `a != b`: the unary is replaced by the inner binary with the operator
// flipped to inequality.
TEST(PatternStatementTransformTest, RewritesNegatedEquality)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* equality = Bin(a, Syntax::BinaryOperatorType::Equality, b);
    auto* negated = new Syntax::UnaryOperatorExpression(equality, Syntax::UnaryOperatorType::Not);

    Syntax::Expression* result = RunOnExpression(fixture, negated);

    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary, equality);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::InEquality);
    EXPECT_EQ(binary->Left(), a);
    EXPECT_EQ(binary->Right(), b);
}

// `!(a != b)` is left alone (the inner operator is not equality).
TEST(PatternStatementTransformTest, KeepsNegatedInequality)
{
    TransformFixture fixture;
    auto* inequality = Bin(Ref("a"), Syntax::BinaryOperatorType::InEquality, Ref("b"));
    auto* negated = new Syntax::UnaryOperatorExpression(inequality, Syntax::UnaryOperatorType::Not);

    Syntax::Expression* result = RunOnExpression(fixture, negated);

    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(result);
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary, negated);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
    EXPECT_EQ(unary->Expression(), inequality);
}

// `!(a < b)` is left alone (the inner operator is a relational operator).
TEST(PatternStatementTransformTest, KeepsNegatedRelational)
{
    TransformFixture fixture;
    auto* lessThan = Bin(Ref("a"), Syntax::BinaryOperatorType::LessThan, Ref("b"));
    auto* negated = new Syntax::UnaryOperatorExpression(lessThan, Syntax::UnaryOperatorType::Not);

    Syntax::Expression* result = RunOnExpression(fixture, negated);

    ASSERT_NE(dynamic_cast<Syntax::UnaryOperatorExpression*>(result), nullptr);
    EXPECT_EQ(result, negated);
}

// A non-negated equality is not touched.
TEST(PatternStatementTransformTest, KeepsPlainEquality)
{
    TransformFixture fixture;
    auto* equality = Bin(Ref("a"), Syntax::BinaryOperatorType::Equality, Ref("b"));

    Syntax::Expression* result = RunOnExpression(fixture, equality);

    EXPECT_EQ(result, equality);
    EXPECT_EQ(equality->Operator(), Syntax::BinaryOperatorType::Equality);
}

// `a && (b && c)` becomes `(a && b) && c`: the right operand becomes the parent and the
// visited expression its left operand.
TEST(PatternStatementTransformTest, ReassociatesConditionalAnd)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* c = Ref("c");
    auto* inner = Bin(b, Syntax::BinaryOperatorType::ConditionalAnd, c);
    auto* outer = Bin(a, Syntax::BinaryOperatorType::ConditionalAnd, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, inner);
    ASSERT_NE(top->Left(), nullptr);
    ASSERT_NE(top->Right(), nullptr);
    EXPECT_EQ(top->Right(), c);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left, outer);
    EXPECT_EQ(left->Left(), a);
    EXPECT_EQ(left->Right(), b);
}

// `a || (b || c)` becomes `(a || b) || c` the same way.
TEST(PatternStatementTransformTest, ReassociatesConditionalOr)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* c = Ref("c");
    auto* inner = Bin(b, Syntax::BinaryOperatorType::ConditionalOr, c);
    auto* outer = Bin(a, Syntax::BinaryOperatorType::ConditionalOr, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, inner);
    EXPECT_EQ(top->Right(), c);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left, outer);
    EXPECT_EQ(left->Left(), a);
    EXPECT_EQ(left->Right(), b);
}

// A mixed pair (`a && (b || c)`) is not associative, so it stays as-is.
TEST(PatternStatementTransformTest, KeepsMixedConditionalOperators)
{
    TransformFixture fixture;
    auto* inner = Bin(Ref("b"), Syntax::BinaryOperatorType::ConditionalOr, Ref("c"));
    auto* outer = Bin(Ref("a"), Syntax::BinaryOperatorType::ConditionalAnd, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    EXPECT_EQ(outer->Right(), inner);
}

// A non-conditional binary operator with a nested conditional right operand is not
// reassociated.
TEST(PatternStatementTransformTest, KeepsNonConditionalWithNestedConditional)
{
    TransformFixture fixture;
    auto* inner = Bin(Ref("b"), Syntax::BinaryOperatorType::ConditionalAnd, Ref("c"));
    auto* outer = Bin(Ref("a"), Syntax::BinaryOperatorType::LessThan, inner);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    EXPECT_EQ(outer->Right(), inner);
}

// The `VisitChildren` walk descends: a negated equality nested as the left operand of a
// conditional-and is rewritten while the outer node is left in place.
TEST(PatternStatementTransformTest, RewritesNestedNegatedEquality)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* equality = Bin(a, Syntax::BinaryOperatorType::Equality, b);
    auto* negated = new Syntax::UnaryOperatorExpression(equality, Syntax::UnaryOperatorType::Not);
    auto* outer = Bin(negated, Syntax::BinaryOperatorType::ConditionalAnd, Ref("c"));

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(outer->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left, equality);
    EXPECT_EQ(left->Operator(), Syntax::BinaryOperatorType::InEquality);
}

// The `VisitChildren` replace-and-revisit walk keeps revisiting a replaced node until the
// visit returns the same node: `a && (b && (c && d))` is flattened fully left-associative to
// `((a && b) && c) && d`.
TEST(PatternStatementTransformTest, FlattensFullyLeftAssociative)
{
    TransformFixture fixture;
    auto* a = Ref("a");
    auto* b = Ref("b");
    auto* c = Ref("c");
    auto* d = Ref("d");
    auto* cAndD = Bin(c, Syntax::BinaryOperatorType::ConditionalAnd, d);
    auto* bAndCAndD = Bin(b, Syntax::BinaryOperatorType::ConditionalAnd, cAndD);
    auto* outer = Bin(a, Syntax::BinaryOperatorType::ConditionalAnd, bAndCAndD);

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    // Final shape: ((a && b) && c) && d -- the original nodes are reused at each level.
    auto* top = dynamic_cast<Syntax::BinaryOperatorExpression*>(result);
    ASSERT_NE(top, nullptr);
    EXPECT_EQ(top, cAndD);
    EXPECT_EQ(top->Right(), d);
    auto* second = dynamic_cast<Syntax::BinaryOperatorExpression*>(top->Left());
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second, bAndCAndD);
    EXPECT_EQ(second->Right(), c);
    auto* third = dynamic_cast<Syntax::BinaryOperatorExpression*>(second->Left());
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(third, outer);
    EXPECT_EQ(third->Left(), a);
    EXPECT_EQ(third->Right(), b);
}

// A tree with no matching shape is left structurally untouched (every node identity is
// preserved by the walk).
TEST(PatternStatementTransformTest, LeavesUnrelatedTreeUntouched)
{
    TransformFixture fixture;
    auto* inner = Bin(Ref("a"), Syntax::BinaryOperatorType::Add, Ref("b"));
    auto* outer = Bin(inner, Syntax::BinaryOperatorType::Multiply, Ref("c"));

    Syntax::Expression* result = RunOnExpression(fixture, outer);

    EXPECT_EQ(result, outer);
    EXPECT_EQ(outer->Left(), inner);
    EXPECT_EQ(inner->Operator(), Syntax::BinaryOperatorType::Add);
}

// ---- pattern-based statement sub-transforms ----------------------------------------

namespace {

// A minimal named `IType` for the resolve-result return slots.
class DestructorStubType : public TS::IType {
public:
    explicit DestructorStubType(std::string name) : name_(std::move(name)) {}
    TS::TypeKind Kind() const override { return TS::TypeKind::Unknown; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    bool StructuralEquals(const TS::IType& other) const override { return &other == this; }

private:
    std::string name_;
};

std::shared_ptr<TestSupport::LookupTypeDefinition> MakeDestructorTypeDef(
    const TS::ICompilation& compilation, const std::string& name) {
    return std::make_shared<TestSupport::LookupTypeDefinition>(
        name, "", TS::FullTypeName(TS::TopLevelTypeName("", name)), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr);
}

// A `TypeDeclaration` named `name` whose resolved symbol is `definition`.
Syntax::TypeDeclaration* MakeDestructorTypeDecl(
    const std::string& name, const std::shared_ptr<TestSupport::LookupTypeDefinition>& definition) {
    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name(name);
    typeDecl->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
        std::static_pointer_cast<TS::IType>(definition)));
    return typeDecl;
}

// A `MethodDeclaration` named `name` whose resolved symbol is `method`.
Syntax::MethodDeclaration* MakeDestructorMethod(const std::string& name, const TS::IMethod* method) {
    auto* methodDecl = new Syntax::MethodDeclaration();
    methodDecl->Name(name);
    methodDecl->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, method, std::make_shared<DestructorStubType>("T")));
    return methodDecl;
}

// The `base.Finalize()` invocation the destructor-body pattern pins.
Syntax::Expression* FinalizeCall() {
    return new Syntax::InvocationExpression(new Syntax::MemberReferenceExpression(
        new Syntax::BaseReferenceExpression(), std::string("Finalize")));
}

// The `base.Finalize();` statement the destructor-body pattern pins.
Syntax::ExpressionStatement* FinalizeStatement() {
    return new Syntax::ExpressionStatement(FinalizeCall());
}

// Runs the transform over a block holding `statement`.
void RunOnStatement(TransformFixture& fixture, Syntax::Statement* statement) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(statement);
    Transforms::PatternStatementTransform transform;
    transform.Run(*block, context);
}

// Runs the transform with `node` as the root.
void RunTransform(TransformFixture& fixture, Syntax::AstNode& node) {
    Transforms::TransformContext context = fixture.MakeContext();
    Transforms::PatternStatementTransform transform;
    transform.Run(node, context);
}

// Builds `try { <tryBody> } finally { base.Finalize(); }`, creating an empty try body when
// none is supplied.
Syntax::TryCatchStatement* MakeDestructorBodyTry(Syntax::BlockStatement* tryBody = nullptr) {
    if (tryBody == nullptr)
        tryBody = new Syntax::BlockStatement();
    auto* finallyBlock = new Syntax::BlockStatement();
    finallyBlock->Statements().Add(FinalizeStatement());
    auto* innerTry = new Syntax::TryCatchStatement(tryBody);
    innerTry->FinallyBlock(finallyBlock);
    return innerTry;
}

} // namespace

// ---- cascading if-else --------------------------------------------------------------

// `if (a) X; else { if (b) Y; else Z; }` becomes `if (a) X; else if (b) Y; else Z;`.
TEST(PatternStatementTransformTest, SimplifiesCascadingIfElse)
{
    TransformFixture fixture;
    auto* outer = new Syntax::IfElseStatement();
    outer->Condition(Ref("a"));
    outer->TrueStatement(new Syntax::ExpressionStatement(Ref("x")));
    auto* nestedIf = new Syntax::IfElseStatement();
    nestedIf->Condition(Ref("b"));
    nestedIf->TrueStatement(new Syntax::ExpressionStatement(Ref("y")));
    nestedIf->FalseStatement(new Syntax::ExpressionStatement(Ref("z")));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(nestedIf);
    outer->FalseStatement(elseBlock);

    RunOnStatement(fixture, outer);

    EXPECT_EQ(outer->FalseStatement(), static_cast<Syntax::Statement*>(nestedIf));
    EXPECT_EQ(elseBlock->Statements().Count(), 0);
}

// An `else` block holding a non-`if` statement is not simplified.
TEST(PatternStatementTransformTest, KeepsElseBlockWithNonIfStatement)
{
    TransformFixture fixture;
    auto* outer = new Syntax::IfElseStatement();
    outer->Condition(Ref("a"));
    outer->TrueStatement(new Syntax::ExpressionStatement(Ref("x")));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(new Syntax::ExpressionStatement(Ref("y")));
    outer->FalseStatement(elseBlock);

    RunOnStatement(fixture, outer);

    EXPECT_EQ(outer->FalseStatement(), static_cast<Syntax::Statement*>(elseBlock));
}

// An `else` block holding two statements (not the single-nested-if shape) is not simplified.
TEST(PatternStatementTransformTest, KeepsElseBlockWithTwoStatements)
{
    TransformFixture fixture;
    auto* outer = new Syntax::IfElseStatement();
    outer->Condition(Ref("a"));
    outer->TrueStatement(new Syntax::ExpressionStatement(Ref("x")));
    auto* nestedIf = new Syntax::IfElseStatement();
    nestedIf->Condition(Ref("b"));
    nestedIf->TrueStatement(new Syntax::ExpressionStatement(Ref("y")));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(nestedIf);
    elseBlock->Statements().Add(new Syntax::ExpressionStatement(Ref("z")));
    outer->FalseStatement(elseBlock);

    RunOnStatement(fixture, outer);

    EXPECT_EQ(outer->FalseStatement(), static_cast<Syntax::Statement*>(elseBlock));
    EXPECT_EQ(elseBlock->Statements().Count(), 2);
}

// ---- try-catch-finally --------------------------------------------------------------

// `try { try { body } catch { ... } } finally { f }` merges into a single try-catch-finally.
TEST(PatternStatementTransformTest, MergesNestedTryCatchFinally)
{
    TransformFixture fixture;
    auto* innerTryBlock = new Syntax::BlockStatement();
    innerTryBlock->Statements().Add(new Syntax::ExpressionStatement(Ref("body")));
    auto* innerTry = new Syntax::TryCatchStatement(innerTryBlock);
    auto* catchClause = new Syntax::CatchClause();
    innerTry->CatchClauses().Add(catchClause);
    auto* outerTryBlock = new Syntax::BlockStatement();
    outerTryBlock->Statements().Add(innerTry);
    auto* finallyBlock = new Syntax::BlockStatement();
    finallyBlock->Statements().Add(new Syntax::ExpressionStatement(Ref("f")));
    auto* outerTry = new Syntax::TryCatchStatement(outerTryBlock);
    outerTry->FinallyBlock(finallyBlock);

    RunOnStatement(fixture, outerTry);

    EXPECT_EQ(outerTry->TryBlock(), innerTryBlock);
    ASSERT_EQ(outerTry->CatchClauses().Count(), 1);
    EXPECT_EQ(outerTry->CatchClauses()[0], static_cast<Syntax::CatchClause*>(catchClause));
    EXPECT_EQ(outerTry->FinallyBlock(), finallyBlock);
}

// A `try`/`catch` without a `finally` block is not the merge shape.
TEST(PatternStatementTransformTest, KeepsTryCatchWithoutFinally)
{
    TransformFixture fixture;
    auto* innerTryBlock = new Syntax::BlockStatement();
    auto* innerTry = new Syntax::TryCatchStatement(innerTryBlock);
    innerTry->CatchClauses().Add(new Syntax::CatchClause());
    auto* outerTryBlock = new Syntax::BlockStatement();
    outerTryBlock->Statements().Add(innerTry);
    auto* outerTry = new Syntax::TryCatchStatement(outerTryBlock);

    RunOnStatement(fixture, outerTry);

    EXPECT_EQ(outerTry->TryBlock(), outerTryBlock);
    EXPECT_EQ(outerTry->CatchClauses().Count(), 0);
}

// A `finally` block whose try body is not a single nested try-catch is not the merge shape.
TEST(PatternStatementTransformTest, KeepsTryFinallyWithNonNestedBody)
{
    TransformFixture fixture;
    auto* outerTryBlock = new Syntax::BlockStatement();
    outerTryBlock->Statements().Add(new Syntax::ExpressionStatement(Ref("body")));
    auto* finallyBlock = new Syntax::BlockStatement();
    auto* outerTry = new Syntax::TryCatchStatement(outerTryBlock);
    outerTry->FinallyBlock(finallyBlock);

    RunOnStatement(fixture, outerTry);

    EXPECT_EQ(outerTry->TryBlock(), outerTryBlock);
}

// ---- destructor --------------------------------------------------------------------

// A `void Finalize()` method with the compiler's destructor body becomes a destructor named
// after the declaring type.
TEST(PatternStatementTransformTest, ConvertsFinalizeMethodToDestructor)
{
    TransformFixture fixture;
    auto typeDef = MakeDestructorTypeDef(fixture.compilation, "MyClass");
    auto method = std::make_shared<TestSupport::LookupMethod>("Finalize", fixture.compilation);

    auto* typeDecl = MakeDestructorTypeDecl("MyClass", typeDef);
    auto* methodDecl = MakeDestructorMethod("Finalize", method.get());
    methodDecl->ReturnType(new Syntax::PrimitiveType("void"));
    auto* innerTry = MakeDestructorBodyTry();
    auto* innerTryBody = innerTry->TryBlock();
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(innerTry);
    methodDecl->Body(body);
    typeDecl->Members().Add(methodDecl);

    RunTransform(fixture, *typeDecl);

    ASSERT_EQ(typeDecl->Members().Count(), 1);
    auto* destructor = dynamic_cast<Syntax::DestructorDeclaration*>(typeDecl->Members()[0]);
    ASSERT_NE(destructor, nullptr);
    EXPECT_EQ(destructor->Name(), "MyClass");
    // The destructor takes the inner try body (the `finally { base.Finalize(); }` is consumed).
    EXPECT_EQ(destructor->Body(), innerTryBody);
}

// A method named `Finalize` whose body does not match the destructor shape is kept.
TEST(PatternStatementTransformTest, KeepsFinalizeMethodWithOtherBody)
{
    TransformFixture fixture;
    auto typeDef = MakeDestructorTypeDef(fixture.compilation, "MyClass");
    auto method = std::make_shared<TestSupport::LookupMethod>("Finalize", fixture.compilation);

    auto* typeDecl = MakeDestructorTypeDecl("MyClass", typeDef);
    auto* methodDecl = MakeDestructorMethod("Finalize", method.get());
    methodDecl->ReturnType(new Syntax::PrimitiveType("void"));
    methodDecl->Body(new Syntax::BlockStatement());
    typeDecl->Members().Add(methodDecl);

    RunTransform(fixture, *typeDecl);

    ASSERT_EQ(typeDecl->Members().Count(), 1);
    EXPECT_NE(dynamic_cast<Syntax::MethodDeclaration*>(typeDecl->Members()[0]), nullptr);
}

// A method with the destructor body shape but a different name is kept.
TEST(PatternStatementTransformTest, KeepsNonFinalizeMethodWithDestructorShape)
{
    TransformFixture fixture;
    auto typeDef = MakeDestructorTypeDef(fixture.compilation, "MyClass");
    auto method = std::make_shared<TestSupport::LookupMethod>("Cleanup", fixture.compilation);

    auto* typeDecl = MakeDestructorTypeDecl("MyClass", typeDef);
    auto* methodDecl = MakeDestructorMethod("Cleanup", method.get());
    methodDecl->ReturnType(new Syntax::PrimitiveType("void"));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(MakeDestructorBodyTry());
    methodDecl->Body(body);
    typeDecl->Members().Add(methodDecl);

    RunTransform(fixture, *typeDecl);

    ASSERT_EQ(typeDecl->Members().Count(), 1);
    EXPECT_NE(dynamic_cast<Syntax::MethodDeclaration*>(typeDecl->Members()[0]), nullptr);
}

// A destructor whose body is `try { ... } finally { base.Finalize(); }` has the try body
// hoisted out.
TEST(PatternStatementTransformTest, SimplifiesDestructorBody)
{
    TransformFixture fixture;
    auto* tryBody = new Syntax::BlockStatement();
    tryBody->Statements().Add(new Syntax::ExpressionStatement(Ref("body")));
    auto* innerTry = MakeDestructorBodyTry(/*tryBody=*/tryBody);
    auto* destructor = new Syntax::DestructorDeclaration();
    destructor->Name("MyClass");
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(innerTry);
    destructor->Body(body);

    RunTransform(fixture, *destructor);

    EXPECT_EQ(destructor->Body(), tryBody);
}

// ---- pattern-based fixed ------------------------------------------------------------

namespace {

// A value-type `IType` stub whose `IsReferenceType()` is `false` (the pattern-based-`fixed`
// gate's positive input; the base `IType` default is a null optional).
class ValueStubType : public TS::IType {
public:
    TS::TypeKind Kind() const override { return TS::TypeKind::Struct; }
    std::string Name() const override { return "S"; }
    std::string ReflectionName() const override { return "S"; }
    int TypeParameterCount() const override { return 0; }
    bool StructuralEquals(const TS::IType& other) const override { return &other == this; }
    std::optional<bool> IsReferenceType() const override { return std::optional<bool>(false); }
};

// `&<target>.GetPinnableReference()` -- the initializer shape the pattern-based-`fixed`
// transform matches.
Syntax::Expression* GetPinnableReferenceAddress(Syntax::Expression* target) {
    auto* memberReference = new Syntax::MemberReferenceExpression(
        target, std::string("GetPinnableReference"));
    auto* invocation = new Syntax::InvocationExpression(memberReference);
    return new Syntax::UnaryOperatorExpression(invocation, Syntax::UnaryOperatorType::AddressOf);
}

} // namespace

// A `fixed (int p = &buffer.GetPinnableReference())` over a value-typed `buffer` becomes
// `fixed (int p = buffer)`.
TEST(PatternStatementTransformTest, RewritesPatternBasedFixedForValueType)
{
    TransformFixture fixture;
    fixture.settings.SetPatternBasedFixedStatement(true);
    auto* buffer = Ref("buffer");
    buffer->AddAnnotation(std::make_shared<Sem::ResolveResult>(
        std::static_pointer_cast<TS::IType>(std::make_shared<ValueStubType>())));
    auto* initializer = new Syntax::VariableInitializer(
        "p", GetPinnableReferenceAddress(buffer));
    auto* fixedStmt = new Syntax::FixedStatement(new Syntax::PrimitiveType("int"));
    fixedStmt->Variables().Add(initializer);
    fixedStmt->EmbeddedStatement(new Syntax::BlockStatement());

    RunOnStatement(fixture, fixedStmt);

    EXPECT_EQ(initializer->Initializer(), static_cast<Syntax::Expression*>(buffer));
}

// A reference-typed `buffer` keeps the `&buffer.GetPinnableReference()` initializer (the
// pinned-region detection handles reference types instead).
TEST(PatternStatementTransformTest, KeepsPatternBasedFixedForReferenceType)
{
    TransformFixture fixture;
    fixture.settings.SetPatternBasedFixedStatement(true);
    auto* buffer = Ref("buffer");
    buffer->AddAnnotation(std::make_shared<Sem::ResolveResult>(
        std::static_pointer_cast<TS::IType>(std::make_shared<DestructorStubType>("object"))));
    auto* addressOf = GetPinnableReferenceAddress(buffer);
    auto* initializer = new Syntax::VariableInitializer("p", addressOf);
    auto* fixedStmt = new Syntax::FixedStatement(new Syntax::PrimitiveType("int"));
    fixedStmt->Variables().Add(initializer);
    fixedStmt->EmbeddedStatement(new Syntax::BlockStatement());

    RunOnStatement(fixture, fixedStmt);

    EXPECT_EQ(initializer->Initializer(), addressOf);
}

// A different initializer shape is left alone even with the setting on.
TEST(PatternStatementTransformTest, KeepsNonPinnableReferenceInitializer)
{
    TransformFixture fixture;
    fixture.settings.SetPatternBasedFixedStatement(true);
    auto* initializer = new Syntax::VariableInitializer("p", Ref("buffer"));
    auto* fixedStmt = new Syntax::FixedStatement(new Syntax::PrimitiveType("int"));
    fixedStmt->Variables().Add(initializer);
    fixedStmt->EmbeddedStatement(new Syntax::BlockStatement());

    RunOnStatement(fixture, fixedStmt);

    EXPECT_NE(dynamic_cast<Syntax::IdentifierExpression*>(initializer->Initializer()), nullptr);
}

// With the setting off the value-typed shape is kept.
TEST(PatternStatementTransformTest, KeepsPatternBasedFixedWhenSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetPatternBasedFixedStatement(false);
    auto* buffer = Ref("buffer");
    buffer->AddAnnotation(std::make_shared<Sem::ResolveResult>(
        std::static_pointer_cast<TS::IType>(std::make_shared<ValueStubType>())));
    auto* addressOf = GetPinnableReferenceAddress(buffer);
    auto* initializer = new Syntax::VariableInitializer("p", addressOf);
    auto* fixedStmt = new Syntax::FixedStatement(new Syntax::PrimitiveType("int"));
    fixedStmt->Variables().Add(initializer);
    fixedStmt->EmbeddedStatement(new Syntax::BlockStatement());

    RunOnStatement(fixture, fixedStmt);

    EXPECT_EQ(initializer->Initializer(), addressOf);
}

// ---- enhanced using ----------------------------------------------------------------

// The last statement of a block whose resource acquisition is a variable declaration is
// flagged as the enhanced using declaration.
TEST(PatternStatementTransformTest, FlagsEnhancedUsingVariable)
{
    TransformFixture fixture;
    fixture.settings.SetUseEnhancedUsing(true);
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::PrimitiveType("int"), "x");
    auto* usingStatement = new Syntax::UsingStatement(
        declaration, new Syntax::BlockStatement());
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(usingStatement);

    RunTransform(fixture, *block);

    EXPECT_TRUE(usingStatement->IsEnhanced());
}

// A using statement that is not the last statement of its block stays a using block.
TEST(PatternStatementTransformTest, KeepsEnhancedUsingWhenFollowedByStatement)
{
    TransformFixture fixture;
    fixture.settings.SetUseEnhancedUsing(true);
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::PrimitiveType("int"), "x");
    auto* usingStatement = new Syntax::UsingStatement(
        declaration, new Syntax::BlockStatement());
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(usingStatement);
    block->Statements().Add(new Syntax::ExpressionStatement(Ref("after")));

    RunTransform(fixture, *block);

    EXPECT_FALSE(usingStatement->IsEnhanced());
}

// A using statement whose resource acquisition is an expression (not a variable
// declaration) stays a using block.
TEST(PatternStatementTransformTest, KeepsEnhancedUsingWithExpressionResource)
{
    TransformFixture fixture;
    fixture.settings.SetUseEnhancedUsing(true);
    auto* usingStatement = new Syntax::UsingStatement(
        Ref("resource"), new Syntax::BlockStatement());
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(usingStatement);

    RunTransform(fixture, *block);

    EXPECT_FALSE(usingStatement->IsEnhanced());
}

// With the setting off the last-statement variable-declaration using is not flagged.
TEST(PatternStatementTransformTest, KeepsEnhancedUsingWhenSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetUseEnhancedUsing(false);
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::PrimitiveType("int"), "x");
    auto* usingStatement = new Syntax::UsingStatement(
        declaration, new Syntax::BlockStatement());
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(usingStatement);

    RunTransform(fixture, *block);

    EXPECT_FALSE(usingStatement->IsEnhanced());
}

// A using statement not directly inside a block (here the root) is not flagged.
TEST(PatternStatementTransformTest, KeepsEnhancedUsingOutsideBlock)
{
    TransformFixture fixture;
    fixture.settings.SetUseEnhancedUsing(true);
    auto* declaration = new Syntax::VariableDeclarationStatement(
        new Syntax::PrimitiveType("int"), "x");
    auto* usingStatement = new Syntax::UsingStatement(
        declaration, new Syntax::BlockStatement());

    RunTransform(fixture, *usingStatement);

    EXPECT_FALSE(usingStatement->IsEnhanced());
}
