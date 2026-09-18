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

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <memory>
#include <initializer_list>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
namespace IL = ::ILSpy::Decompiler::IL;
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

    TS::ITypePtr FindType(TS::KnownTypeCode code) {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
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

// ---- for ---------------------------------------------------------------------------

namespace {

IL::ILVariablePtr Var(const std::string& name) {
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::UnknownType());
    variable->Name = name;
    return variable;
}

// An `IdentifierExpression` named after the variable and carrying its `ILVariableResolveResult`
// (the annotation the `for` rewrite reads through `GetILVariable`).
Syntax::IdentifierExpression* Use(const IL::ILVariablePtr& variable) {
    auto* identifier = new Syntax::IdentifierExpression(variable->Name.c_str());
    identifier->AddAnnotation(
        std::make_shared<::ILSpy::Decompiler::CSharp::ILVariableResolveResult>(variable));
    return identifier;
}

Syntax::PrimitiveExpression* Int(int value) {
    return new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(std::int32_t(value)));
}

// Runs the transform over a block holding `statements` and returns the block.
Syntax::BlockStatement* RunOnBlock(
    TransformFixture& fixture, std::initializer_list<Syntax::Statement*> statements) {
    Transforms::TransformContext context = fixture.MakeContext();
    auto* block = new Syntax::BlockStatement();
    for (Syntax::Statement* statement : statements)
        block->Statements().Add(statement);
    Transforms::PatternStatementTransform transform;
    transform.Run(*block, context);
    return block;
}

} // namespace

// `i = 0; while (i < n) { body; i = i + 1; }` becomes
// `for (i = 0; i < n; i = i + 1) { body; }`.
TEST(PatternStatementTransformTest, TransformsWhileLoopToFor)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* condition = Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(Use(n)));
    auto* iteratorStmt = new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(i), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(i), Syntax::BinaryOperatorType::Add, Int(1))));
    body->Statements().Add(iteratorStmt);
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(condition);
    whileStmt->EmbeddedStatement(body);

    auto* block = RunOnBlock(fixture, {initStmt, whileStmt});

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* forStatement = dynamic_cast<Syntax::ForStatement*>(block->Statements()[0]);
    ASSERT_NE(forStatement, nullptr);
    ASSERT_EQ(forStatement->Initializers().Count(), 1);
    EXPECT_EQ(forStatement->Initializers()[0], static_cast<Syntax::Statement*>(initStmt));
    EXPECT_EQ(forStatement->Condition(), condition);
    ASSERT_EQ(forStatement->Iterators().Count(), 1);
    EXPECT_EQ(forStatement->Iterators()[0], static_cast<Syntax::Statement*>(iteratorStmt));
    auto* newBody = dynamic_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    ASSERT_NE(newBody, nullptr);
    EXPECT_EQ(newBody->Statements().Count(), 1);
}

// With `ForStatement` off the while loop is left in place.
TEST(PatternStatementTransformTest, KeepsWhileLoopWhenSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(false);
    auto i = Var("i");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n)));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(i), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(i), Syntax::BinaryOperatorType::Add, Int(1)))));
    whileStmt->EmbeddedStatement(body);

    auto* block = RunOnBlock(fixture, {initStmt, whileStmt});

    EXPECT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements()[1], static_cast<Syntax::Statement*>(whileStmt));
}

// The declaration variable and the condition variable must be the same; otherwise the while
// loop is kept.
TEST(PatternStatementTransformTest, KeepsWhileLoopWhenVariableDiffers)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto j = Var("j");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(Bin(Use(j), Syntax::BinaryOperatorType::LessThan, Use(n)));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(j), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(j), Syntax::BinaryOperatorType::Add, Int(1)))));
    whileStmt->EmbeddedStatement(body);

    auto* block = RunOnBlock(fixture, {initStmt, whileStmt});

    EXPECT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements()[1], static_cast<Syntax::Statement*>(whileStmt));
}

// A `continue` in the loop body blocks the rewrite (in a while it jumps to the condition,
// whereas in a for it jumps to the increment).
TEST(PatternStatementTransformTest, KeepsWhileLoopWithContinue)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n)));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ContinueStatement());
    body->Statements().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(i), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(i), Syntax::BinaryOperatorType::Add, Int(1)))));
    whileStmt->EmbeddedStatement(body);

    auto* block = RunOnBlock(fixture, {initStmt, whileStmt});

    EXPECT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements()[1], static_cast<Syntax::Statement*>(whileStmt));
}

// A variable referenced by the iterator that would be declared inside the loop body blocks the
// rewrite (the iterator cannot be split from the declaration).
TEST(PatternStatementTransformTest, KeepsWhileLoopWhenIteratorVariableDeclaredInside)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto k = Var("k");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n)));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(k), Int(5))));
    body->Statements().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(i), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(k), Syntax::BinaryOperatorType::Add, Int(1)))));
    whileStmt->EmbeddedStatement(body);

    auto* block = RunOnBlock(fixture, {initStmt, whileStmt});

    EXPECT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements()[1], static_cast<Syntax::Statement*>(whileStmt));
}

// A by-ref local used after the loop keeps the while loop (the hoisted declaration cannot be
// split into a for initializer).
TEST(PatternStatementTransformTest, KeepsWhileLoopForByRefVariableUsedAfter)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    i->Type = std::make_shared<TS::ByReferenceType>(TS::UnknownType());
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n)));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(i), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(i), Syntax::BinaryOperatorType::Add, Int(1)))));
    whileStmt->EmbeddedStatement(body);
    auto* afterStmt = new Syntax::ExpressionStatement(Use(i));

    auto* block = RunOnBlock(fixture, {initStmt, whileStmt, afterStmt});

    EXPECT_EQ(block->Statements().Count(), 3);
    EXPECT_EQ(block->Statements()[1], static_cast<Syntax::Statement*>(whileStmt));
}

// A first statement that is not a `$var = $init` assignment is not a for declaration.
TEST(PatternStatementTransformTest, KeepsFirstNonAssignmentStatement)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto n = Var("n");
    auto* firstStmt = new Syntax::ExpressionStatement(new Syntax::InvocationExpression(Ref("Foo")));
    auto* whileStmt = new Syntax::WhileStatement();
    whileStmt->Condition(Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n)));
    whileStmt->EmbeddedStatement(new Syntax::BlockStatement());

    auto* block = RunOnBlock(fixture, {firstStmt, whileStmt});

    EXPECT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements()[1], static_cast<Syntax::Statement*>(whileStmt));
}

// `i = 0; for (; i < n; i = i + 1) {}` moves the declaration into the for initializer.
TEST(PatternStatementTransformTest, MovesDeclarationIntoForInitializer)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Condition(Bin(Use(i), Syntax::BinaryOperatorType::LessThan, Use(n)));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(i), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(i), Syntax::BinaryOperatorType::Add, Int(1)))));
    forStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto* block = RunOnBlock(fixture, {initStmt, forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
    ASSERT_EQ(forStatement->Initializers().Count(), 1);
    EXPECT_EQ(forStatement->Initializers()[0], static_cast<Syntax::Statement*>(initStmt));
}

// A for loop whose condition and iterators do not use the variable keeps the declaration as a
// separate statement.
TEST(PatternStatementTransformTest, KeepsDeclarationWhenForDoesNotUseVariable)
{
    TransformFixture fixture;
    fixture.settings.SetForStatement(true);
    auto i = Var("i");
    auto j = Var("j");
    auto n = Var("n");
    auto* initStmt = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(i), Int(0)));
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Condition(Bin(Use(j), Syntax::BinaryOperatorType::LessThan, Use(n)));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        Use(j), Syntax::AssignmentOperatorType::Assign,
        Bin(Use(j), Syntax::BinaryOperatorType::Add, Int(1)))));
    forStatement->EmbeddedStatement(new Syntax::BlockStatement());

    auto* block = RunOnBlock(fixture, {initStmt, forStatement});

    ASSERT_EQ(block->Statements().Count(), 2);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(initStmt));
    EXPECT_EQ(forStatement->Initializers().Count(), 0);
}

// ---- foreach over array ------------------------------------------------------------

namespace {

// Builds the compiler's array index loop
// `for (index = 0; index < array.Length; index = index + 1) { item = array[index]; <extra> }`.
Syntax::ForStatement* MakeArrayForLoop(
    const IL::ILVariablePtr& index, const IL::ILVariablePtr& array,
    const IL::ILVariablePtr& item,
    std::initializer_list<Syntax::Statement*> extraStatements = {}) {
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Initializers().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(index), Int(0))));
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Use(index), Syntax::BinaryOperatorType::LessThan,
        new Syntax::MemberReferenceExpression(Use(array), std::string("Length"))));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Use(index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Use(index), Syntax::BinaryOperatorType::Add, Int(1)))));
    auto* body = new Syntax::BlockStatement();
    auto* indexer = new Syntax::IndexerExpression(Use(array));
    indexer->Arguments().Add(Use(index));
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(item), indexer)));
    for (Syntax::Statement* statement : extraStatements)
        body->Statements().Add(statement);
    forStatement->EmbeddedStatement(body);
    return forStatement;
}

} // namespace

// `for (i = 0; i < array.Length; i = i + 1) { item = array[i]; body; }` becomes
// `foreach (var item in array) { body; }`.
TEST(PatternStatementTransformTest, TransformsArrayForLoopToForeach)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* bodyStatement = new Syntax::ExpressionStatement(Ref("Foo"));
    auto* forStatement = MakeArrayForLoop(index, array, item, {bodyStatement});

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* foreachStmt = dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]);
    ASSERT_NE(foreachStmt, nullptr);
    auto* inExpression = dynamic_cast<Syntax::IdentifierExpression*>(foreachStmt->InExpression());
    ASSERT_NE(inExpression, nullptr);
    EXPECT_EQ(inExpression->Identifier(), "array");
    auto* designation = dynamic_cast<Syntax::SingleVariableDesignation*>(
        foreachStmt->VariableDesignation());
    ASSERT_NE(designation, nullptr);
    EXPECT_EQ(designation->Identifier(), "item");
    EXPECT_EQ(static_cast<int>(item->Kind), static_cast<int>(IL::VariableKind::ForeachLocal));
    auto* newBody = dynamic_cast<Syntax::BlockStatement*>(foreachStmt->EmbeddedStatement());
    ASSERT_NE(newBody, nullptr);
    ASSERT_EQ(newBody->Statements().Count(), 1);
    EXPECT_EQ(newBody->Statements()[0], static_cast<Syntax::Statement*>(bodyStatement));
}

// A `string` looped by index also rewrites to `foreach` (the `Length` member plus the string
// indexer read as a `char`).
TEST(PatternStatementTransformTest, TransformsStringForLoopToForeach)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto text = Var("text");
    text->Type = fixture.FindType(TS::KnownTypeCode::String);
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* forStatement = MakeArrayForLoop(index, text, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_NE(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// With `ForEachStatement` off the index loop is left alone.
TEST(PatternStatementTransformTest, KeepsForLoopWhenForEachSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(false);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* forStatement = MakeArrayForLoop(index, array, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// The index variable must be a pure counter (stored twice, loaded three times, never
// addressed); a different profile keeps the loop.
TEST(PatternStatementTransformTest, KeepsForLoopWhenIndexCountsDiffer)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    index->StoreCount = 3;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* forStatement = MakeArrayForLoop(index, array, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// An item variable that is not single-definition (and whose address is not used for a single
// call) cannot become a foreach local.
TEST(PatternStatementTransformTest, KeepsForLoopWhenItemNotSingleDefinition)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 2;
    auto* forStatement = MakeArrayForLoop(index, array, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// The looped collection must be an array or a string; any other type keeps the loop.
TEST(PatternStatementTransformTest, KeepsForLoopWhenCollectionIsNotArrayOrString)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto scalar = Var("scalar");
    scalar->Type = fixture.FindType(TS::KnownTypeCode::Int32);
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* forStatement = MakeArrayForLoop(index, scalar, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// Only locals/stack slots can become the foreach local; a parameter keeps the loop.
TEST(PatternStatementTransformTest, KeepsForLoopWhenItemIsParameter)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    item->Kind = IL::VariableKind::Parameter;
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* forStatement = MakeArrayForLoop(index, array, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// A variable captured outside the loop keeps the loop (it cannot be declared in the loop).
TEST(PatternStatementTransformTest, KeepsForLoopWhenItemCapturedOutsideLoop)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    IL::BlockContainer captureScope;
    item->CaptureScope = &captureScope;
    auto* forStatement = MakeArrayForLoop(index, array, item);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// A condition that is not `<` (here `<=`) does not match the array pattern.
TEST(PatternStatementTransformTest, KeepsForLoopWhenConditionIsLessThanOrEqual)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    auto index = Var("i");
    auto array = Var("array");
    array->Type = std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32));
    auto item = Var("item");
    index->StoreCount = 2;
    index->LoadCount = 3;
    item->StoreCount = 1;
    auto* forStatement = MakeArrayForLoop(index, array, item);
    static_cast<Syntax::BinaryOperatorExpression*>(forStatement->Condition())
        ->Operator(Syntax::BinaryOperatorType::LessThanOrEqual);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// ---- foreach over a multidimensional array -----------------------------------------

namespace {

// `$target = $collection.GetUpperBound/$GetLowerBound($dim);`.
Syntax::ExpressionStatement* BoundCall(const char* method, const IL::ILVariablePtr& target,
                                        const IL::ILVariablePtr& collection, int dim) {
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(Use(collection), std::string(method)));
    invocation->Arguments().Add(Int(dim));
    return new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(target), invocation));
}

// `for (; $index <= $upper; $index = $index + 1) { $lowerBoundAssign; <rest> }` (no
// initializer -- the index is already set by the preceding lower-bound assignment).
Syntax::ForStatement* MultiDimFor(const IL::ILVariablePtr& index,
                                  const IL::ILVariablePtr& upper,
                                  Syntax::Statement* lowerBoundAssign,
                                  std::initializer_list<Syntax::Statement*> rest = {}) {
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Use(index), Syntax::BinaryOperatorType::LessThanOrEqual, Use(upper)));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Use(index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Use(index), Syntax::BinaryOperatorType::Add, Int(1)))));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(lowerBoundAssign);
    for (Syntax::Statement* statement : rest)
        body->Statements().Add(statement);
    forStatement->EmbeddedStatement(body);
    return forStatement;
}

// `$item = $collection[$index0, $index1, ...];`.
Syntax::ExpressionStatement* ElementAssignment(
    const IL::ILVariablePtr& item, const IL::ILVariablePtr& collection,
    std::initializer_list<IL::ILVariablePtr> indices) {
    auto* indexer = new Syntax::IndexerExpression(Use(collection));
    for (const IL::ILVariablePtr& index : indices)
        indexer->Arguments().Add(Use(index));
    return new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(item), indexer));
}

// The compiler's rank-2 multidimensional index nest:
//   $u0 = array.GetUpperBound(0);
//   $u1 = array.GetUpperBound(1);
//   $i0 = array.GetLowerBound(0);
//   for (; $i0 <= $u0; $i0 = $i0 + 1) {
//       $i1 = array.GetLowerBound(1);
//       for (; $i1 <= $u1; $i1 = $i1 + 1) {
//           $item = array[$i0, $i1];
//           <bodyStatements>
//       }
//   }
std::vector<Syntax::Statement*> MakeMultiDimNest(
    const IL::ILVariablePtr& collection, const IL::ILVariablePtr& u0,
    const IL::ILVariablePtr& u1, const IL::ILVariablePtr& i0, const IL::ILVariablePtr& i1,
    const IL::ILVariablePtr& item,
    std::initializer_list<Syntax::Statement*> bodyStatements = {},
    int firstUpperBoundIndex = 0) {
    auto* innerFor = MultiDimFor(
        i1, u1, ElementAssignment(item, collection, {i0, i1}), bodyStatements);
    auto* outerFor = MultiDimFor(i0, u0, BoundCall("GetLowerBound", i1, collection, 1),
                                 {innerFor});
    return {
        BoundCall("GetUpperBound", u0, collection, firstUpperBoundIndex),
        BoundCall("GetUpperBound", u1, collection, 1),
        BoundCall("GetLowerBound", i0, collection, 0),
        outerFor,
    };
}

// The shared rank-2 setup (a rank-2 int array and one variable per bound/index/item role).
struct MultiDimFixture {
    IL::ILVariablePtr collection = Var("array");
    IL::ILVariablePtr u0 = Var("u0");
    IL::ILVariablePtr u1 = Var("u1");
    IL::ILVariablePtr i0 = Var("i0");
    IL::ILVariablePtr i1 = Var("i1");
    IL::ILVariablePtr item = Var("item");

    explicit MultiDimFixture(TransformFixture& fixture) {
        collection->Type =
            std::make_shared<TS::ArrayType>(fixture.FindType(TS::KnownTypeCode::Int32), 2);
        u0->StoreCount = 1;
        u0->LoadCount = 1;
        u1->StoreCount = 1;
        u1->LoadCount = 1;
        i0->StoreCount = 2;
        i0->LoadCount = 3;
        i1->StoreCount = 2;
        i1->LoadCount = 3;
        item->StoreCount = 1;
    }

    std::vector<Syntax::Statement*> Make(
        std::initializer_list<Syntax::Statement*> bodyStatements = {},
        int firstUpperBoundIndex = 0) {
        return MakeMultiDimNest(collection, u0, u1, i0, i1, item, bodyStatements,
                                firstUpperBoundIndex);
    }
};

} // namespace

// The nested `GetUpperBound`/`GetLowerBound` index loops over a rank-2 array become
// `foreach (var item in array) { body; }`.
TEST(PatternStatementTransformTest, TransformsMultiDimArrayForLoopToForeach)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    MultiDimFixture dim(fixture);
    auto* bodyStatement = new Syntax::ExpressionStatement(Ref("Foo"));
    std::vector<Syntax::Statement*> statements = dim.Make({bodyStatement});

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* foreachStmt = dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]);
    ASSERT_NE(foreachStmt, nullptr);
    auto* inExpression = dynamic_cast<Syntax::IdentifierExpression*>(foreachStmt->InExpression());
    ASSERT_NE(inExpression, nullptr);
    EXPECT_EQ(inExpression->Identifier(), "array");
    auto* designation =
        dynamic_cast<Syntax::SingleVariableDesignation*>(foreachStmt->VariableDesignation());
    ASSERT_NE(designation, nullptr);
    EXPECT_EQ(designation->Identifier(), "item");
    EXPECT_EQ(static_cast<int>(dim.item->Kind),
              static_cast<int>(IL::VariableKind::ForeachLocal));
    auto* newBody = dynamic_cast<Syntax::BlockStatement*>(foreachStmt->EmbeddedStatement());
    ASSERT_NE(newBody, nullptr);
    ASSERT_EQ(newBody->Statements().Count(), 1);
    EXPECT_EQ(newBody->Statements()[0], static_cast<Syntax::Statement*>(bodyStatement));
}

// With `ForEachStatement` off the multidimensional index nest is left alone.
TEST(PatternStatementTransformTest, KeepsMultiDimArrayNestWhenForEachSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(false);
    MultiDimFixture dim(fixture);
    std::vector<Syntax::Statement*> statements = dim.Make();

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    // The `TransformFor` declaration move may still fold `$i0 = ...` into the `for`
    // initializer, but the multidim nest itself is not rewritten.
    EXPECT_NE(block->Statements()[0], nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// The collection must be an array type; a scalar keeps the index nest.
TEST(PatternStatementTransformTest, KeepsMultiDimArrayNestWhenCollectionIsNotArray)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    MultiDimFixture dim(fixture);
    dim.collection->Type = fixture.FindType(TS::KnownTypeCode::Int32);
    std::vector<Syntax::Statement*> statements = dim.Make();

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    EXPECT_EQ(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// The upper-bound initializations must be numbered from 0; a `GetUpperBound(1)` first keeps the
// nest.
TEST(PatternStatementTransformTest, KeepsMultiDimArrayNestWhenBoundIndexIsWrong)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    MultiDimFixture dim(fixture);
    std::vector<Syntax::Statement*> statements = dim.Make({}, 1);

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    EXPECT_EQ(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// An upper-bound variable that is loaded more than once (so not a pure bound) keeps the nest.
TEST(PatternStatementTransformTest, KeepsMultiDimArrayNestWhenUpperBoundNotSingleLoad)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    MultiDimFixture dim(fixture);
    dim.u0->LoadCount = 2;
    std::vector<Syntax::Statement*> statements = dim.Make();

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    EXPECT_EQ(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// An index variable that is not a pure counter (stored twice, loaded three times, never
// addressed) keeps the nest.
TEST(PatternStatementTransformTest, KeepsMultiDimArrayNestWhenIndexCountsDiffer)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    MultiDimFixture dim(fixture);
    dim.i0->StoreCount = 3;
    std::vector<Syntax::Statement*> statements = dim.Make();

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    EXPECT_EQ(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// An item variable that is not single-definition cannot become the foreach local.
TEST(PatternStatementTransformTest, KeepsMultiDimArrayNestWhenItemNotSingleDefinition)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    MultiDimFixture dim(fixture);
    dim.item->StoreCount = 2;
    std::vector<Syntax::Statement*> statements = dim.Make();

    auto* block = RunOnBlock(fixture, {statements[0], statements[1], statements[2],
                                       statements[3]});

    EXPECT_EQ(dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]), nullptr);
}

// ---- foreach over an inline array ---------------------------------------------------

namespace {

// The `[InlineArray(N)]` attribute stub carrying the positional length argument.
class TestInlineArrayAttribute : public TS::IAttribute {
public:
    TestInlineArrayAttribute(const TS::ITypePtr& intType, int length)
        : args_({TS::CustomAttributeTypedArgument(intType, std::any(length))}) {}

    const TS::IType& AttributeType() const override {
        static auto attrType = std::make_shared<TS::SimpleType>(
            TS::TopLevelTypeName("System.Runtime.CompilerServices", "InlineArrayAttribute"));
        return *attrType;
    }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override
    {
        return args_;
    }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    std::vector<TS::CustomAttributeTypedArgument> args_;
};

// A struct definition carrying an `[InlineArray(N)]` attribute (the helper's buffer type).
class TestInlineArrayDefinition : public TestSupport::LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetInlineArrayAttribute(const TS::IAttribute* attr) { attr_ = attr; }
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::InlineArray && attr_ != nullptr;
    }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::InlineArray ? attr_ : nullptr;
    }

private:
    const TS::IAttribute* attr_ = nullptr;
};

// The `<PrivateImplementationDetails>` type (the helper method's declaring type).
std::shared_ptr<TestSupport::LookupTypeDefinition> MakePrivateImplementationDetails(
    TransformFixture& fixture) {
    return std::make_shared<TestSupport::LookupTypeDefinition>(
        "<PrivateImplementationDetails>", "",
        TS::FullTypeName(TS::TopLevelTypeName("", "<PrivateImplementationDetails>", 0)),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation, nullptr,
        TS::KnownTypeCode::None);
}

// The `[InlineArray(N)]` buffer type (leaks the attribute, test scope).
TS::ITypePtr MakeInlineArrayBuffer(TransformFixture& fixture, int length) {
    auto def = std::make_shared<TestInlineArrayDefinition>(
        "Buffer", "Test", TS::FullTypeName(TS::TopLevelTypeName("Test", "Buffer", 0)),
        TS::TypeKind::Struct, TS::Accessibility::Public, fixture.compilation, nullptr,
        TS::KnownTypeCode::None);
    def->SetInlineArrayAttribute(
        new TestInlineArrayAttribute(fixture.FindType(TS::KnownTypeCode::Int32), length));
    return def;
}

// Builds the compiler's inline-array loop
// `for (index = 0; index < length; index = index + 1) { item = <helper>(ref buffer, index);
// <extra> }`. `helper` may be null (then the element access carries no symbol).
Syntax::ForStatement* MakeInlineArrayForLoop(
    const IL::ILVariablePtr& index, const IL::ILVariablePtr& item,
    const IL::ILVariablePtr& buffer, const TS::IMethod* helper,
    const TS::ITypePtr& resultType, int length,
    std::initializer_list<Syntax::Statement*> extraStatements = {}) {
    auto* forStatement = new Syntax::ForStatement();
    forStatement->Initializers().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(index), Int(0))));
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Use(index), Syntax::BinaryOperatorType::LessThan, Int(length)));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(
            Use(index), Syntax::AssignmentOperatorType::Assign,
            new Syntax::BinaryOperatorExpression(
                Use(index), Syntax::BinaryOperatorType::Add, Int(1)))));
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::IdentifierExpression("InlineArrayElementRef"));
    invocation->Arguments().Add(new Syntax::DirectionExpression(
        Syntax::FieldDirection::Ref, Use(buffer)));
    invocation->Arguments().Add(Use(index));
    if (helper != nullptr) {
        invocation->AddAnnotation(
            std::make_shared<Sem::MemberResolveResult>(nullptr, helper, resultType));
    }
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(item), invocation)));
    for (Syntax::Statement* statement : extraStatements)
        body->Statements().Add(statement);
    forStatement->EmbeddedStatement(body);
    return forStatement;
}

// The shared inline-array setup (a `[InlineArray(length)]` buffer, the
// `<PrivateImplementationDetails>.InlineArrayElementRef` helper, and the loop variables).
struct InlineArrayFixture {
    std::shared_ptr<TestSupport::LookupTypeDefinition> implementationDetails;
    TS::ITypePtr bufferType;
    std::shared_ptr<TestSupport::LookupMethod> helper;
    IL::ILVariablePtr index = Var("i");
    IL::ILVariablePtr buffer = Var("buffer");
    IL::ILVariablePtr item = Var("item");
    int length = 4;

    explicit InlineArrayFixture(TransformFixture& fixture) {
        implementationDetails = MakePrivateImplementationDetails(fixture);
        bufferType = MakeInlineArrayBuffer(fixture, length);
        buffer->Type = bufferType;
        helper = std::make_shared<TestSupport::LookupMethod>(
            "InlineArrayElementRef", fixture.compilation);
        helper->SetDeclaringType(implementationDetails);
        index->StoreCount = 2;
        index->LoadCount = 3;
        item->StoreCount = 1;
    }

    Syntax::ForStatement* Make(
        std::initializer_list<Syntax::Statement*> extraStatements = {}) {
        return MakeInlineArrayForLoop(index, item, buffer, helper.get(), bufferType, length,
                                      extraStatements);
    }
};

} // namespace

// The compiler's inline-array index loop becomes `foreach (var item in buffer) { body; }`.
TEST(PatternStatementTransformTest, TransformsInlineArrayForLoopToForeach)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    auto* bodyStatement = new Syntax::ExpressionStatement(Ref("Foo"));
    auto* forStatement = inlineArray.Make({bodyStatement});

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    auto* foreachStmt = dynamic_cast<Syntax::ForeachStatement*>(block->Statements()[0]);
    ASSERT_NE(foreachStmt, nullptr);
    auto* inExpression = dynamic_cast<Syntax::IdentifierExpression*>(foreachStmt->InExpression());
    ASSERT_NE(inExpression, nullptr);
    EXPECT_EQ(inExpression->Identifier(), "buffer");
    auto* designation =
        dynamic_cast<Syntax::SingleVariableDesignation*>(foreachStmt->VariableDesignation());
    ASSERT_NE(designation, nullptr);
    EXPECT_EQ(designation->Identifier(), "item");
    EXPECT_EQ(static_cast<int>(inlineArray.item->Kind),
              static_cast<int>(IL::VariableKind::ForeachLocal));
    auto* newBody = dynamic_cast<Syntax::BlockStatement*>(foreachStmt->EmbeddedStatement());
    ASSERT_NE(newBody, nullptr);
    ASSERT_EQ(newBody->Statements().Count(), 1);
    EXPECT_EQ(newBody->Statements()[0], static_cast<Syntax::Statement*>(bodyStatement));
}

// With `InlineArrays` off the inline-array index loop is left alone.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenInlineArraysSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(false);
    InlineArrayFixture inlineArray(fixture);
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// With `ForEachStatement` off the inline-array index loop is left alone.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenForEachSettingOff)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(false);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// An element access that is not an invocation keeps the loop.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenElementAccessNotInvocation)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    auto* forStatement = inlineArray.Make();
    auto* body = static_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    body->Statements().SetAt(0, new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Use(inlineArray.item), Ref("element"))));

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// A helper whose declaring type is not `<PrivateImplementationDetails>` keeps the loop.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenHelperNotPrivateImplementationDetails)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    auto otherType = std::make_shared<TestSupport::LookupTypeDefinition>(
        "Other", "", TS::FullTypeName(TS::TopLevelTypeName("", "Other", 0)),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.compilation, nullptr,
        TS::KnownTypeCode::None);
    inlineArray.helper->SetDeclaringType(otherType);
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// A helper with the wrong name keeps the loop.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenHelperNameDiffers)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    inlineArray.helper = std::make_shared<TestSupport::LookupMethod>(
        "InlineArrayOther", fixture.compilation);
    inlineArray.helper->SetDeclaringType(inlineArray.implementationDetails);
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// A loop bound that does not equal the inline array length keeps the loop (the index would
// not be provably in range).
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenBoundDiffersFromLength)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    auto* forStatement = MakeInlineArrayForLoop(
        inlineArray.index, inlineArray.item, inlineArray.buffer, inlineArray.helper.get(),
        inlineArray.bufferType, inlineArray.length + 1);

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// An index argument that is not the loop's index variable keeps the loop.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenIndexArgumentDiffers)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    auto* forStatement = inlineArray.Make();
    auto* body = static_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    auto* assignment = static_cast<Syntax::ExpressionStatement*>(body->Statements()[0]);
    auto* assignExpr = dynamic_cast<Syntax::AssignmentExpression*>(assignment->Expression());
    ASSERT_NE(assignExpr, nullptr);
    auto* invocation =
        dynamic_cast<Syntax::InvocationExpression*>(assignExpr->Right());
    ASSERT_NE(invocation, nullptr);
    invocation->Arguments().SetAt(1, Use(Var("other")));

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// A buffer whose type is not an inline array keeps the loop.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenBufferTypeNotInlineArray)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    inlineArray.buffer->Type = fixture.FindType(TS::KnownTypeCode::Int32);
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// An index variable that is not a pure counter keeps the loop.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenIndexCountsDiffer)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    inlineArray.index->StoreCount = 3;
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// An item variable that is not single-definition cannot become the foreach local.
TEST(PatternStatementTransformTest, KeepsInlineArrayLoopWhenItemNotSingleDefinition)
{
    TransformFixture fixture;
    fixture.settings.SetForEachStatement(true);
    fixture.settings.SetInlineArrays(true);
    InlineArrayFixture inlineArray(fixture);
    inlineArray.item->StoreCount = 2;
    auto* forStatement = inlineArray.Make();

    auto* block = RunOnBlock(fixture, {forStatement});

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements()[0], static_cast<Syntax::Statement*>(forStatement));
}

// ---- automatic properties -----------------------------------------------------------

namespace {

// A `FakeField` reporting `[CompilerGenerated]`.
class CompilerGeneratedField : public Impl::FakeField {
public:
    explicit CompilerGeneratedField(const TS::ICompilation& compilation)
        : Impl::FakeField(compilation) {}
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::CompilerGenerated
            || Impl::FakeField::HasAttribute(attribute);
    }
};

// A `FakeMethod` reporting `[CompilerGenerated]`.
class CompilerGeneratedMethod : public Impl::FakeMethod {
public:
    explicit CompilerGeneratedMethod(const TS::ICompilation& compilation)
        : Impl::FakeMethod(compilation, TS::SymbolKind::Method) {}
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::CompilerGenerated
            || Impl::FakeMethod::HasAttribute(attribute);
    }
};

// A type definition whose `FullName` is `<ns>.<name>` and whose `FullTypeName` is the
// matching top-level name (the attribute-type fixture the `IsKnownType` classification and
// the full-name attribute removal compare against).
std::shared_ptr<TestSupport::LookupTypeDefinition> MakeNamedTypeDef(
    const TS::ICompilation& compilation, const std::string& ns, const std::string& name) {
    const std::string fullName = ns.empty() ? name : ns + "." + name;
    return std::make_shared<TestSupport::LookupTypeDefinition>(
        fullName, ns, TS::FullTypeName(TS::TopLevelTypeName(ns, name)), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr);
}

// `[<resolved type>]` -- an attribute section whose type node resolves to `typeDef`.
Syntax::AttributeSection* MakeAttributeSection(
    const std::shared_ptr<TestSupport::LookupTypeDefinition>& typeDef) {
    auto* simpleType = new Syntax::SimpleType(typeDef->Name());
    simpleType->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
        std::static_pointer_cast<TS::IType>(typeDef)));
    return new Syntax::AttributeSection(new Syntax::Attribute(simpleType));
}

// The `IField`-typed `IMember` handle for a fake field: the `FakeField` diamond shares its
// `IMember` base with `FakeMember`, so the annotation is built through the `FakeMember`
// subobject, while the identity comparison goes through the unambiguous `IField` subobject.
const TS::IMember* AsMember(const Impl::FakeMember& member) {
    return static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(&member));
}
const TS::IMember* AsMember(const Impl::FakeMethod& member) {
    return static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(&member));
}

// The canonical `IMember` view of a fake property. The port's metadata members derive only from
// their interface (so `static_cast<IMember*>(IProperty*)` is the identity the transforms and the
// annotations use), while the `FakeProperty` diamond also has a `FakeMember`-path `IMember`
// subobject. Tests that compare against a property returned by `GetProperties` or stored as an
// `AccessorOwner` must use this view.
const TS::IMember* AsPropertyMember(const Impl::FakeProperty& property) {
    return static_cast<const TS::IMember*>(
        static_cast<const TS::IProperty*>(&property));
}

// A fully built `class C { <field>; int P { get { return <field>; } set { <field> = value; } } }`
// with resolved symbols, the compiler backing-field name, and the attribute shapes the
// automatic-property rewrite consumes.
class AutoPropertyFixture {
public:
    explicit AutoPropertyFixture(TransformFixture& f)
        : fixture(f),
          typeDef(MakeNamedTypeDef(f.compilation, "N", "C")),
          compilerGeneratedAttr(MakeNamedTypeDef(
              f.compilation, "System.Runtime.CompilerServices", "CompilerGeneratedAttribute")),
          debuggerBrowsableAttr(
              MakeNamedTypeDef(f.compilation, "System.Diagnostics", "DebuggerBrowsableAttribute")),
          markerAttr(MakeNamedTypeDef(f.compilation, "N", "MarkerAttribute")),
          field(std::make_shared<CompilerGeneratedField>(f.compilation)),
          getter(std::make_shared<CompilerGeneratedMethod>(f.compilation)),
          setter(std::make_shared<CompilerGeneratedMethod>(f.compilation)),
          property(std::make_shared<Impl::FakeProperty>(f.compilation)) {
        field->SetName("<P>k__BackingField");
        field->SetDeclaringType(typeDef);
        field->SetReturnType(f.FindType(TS::KnownTypeCode::Int32));
        getter->SetName("get_P");
        getter->SetDeclaringType(typeDef);
        setter->SetName("set_P");
        setter->SetDeclaringType(typeDef);
        property->SetName("P");
        property->SetDeclaringType(typeDef);
        property->SetGetter(getter.get());
        property->SetSetter(setter.get());
        property->SetReturnType(f.FindType(TS::KnownTypeCode::Int32));
    }

    // The `IField*` identity the field declaration annotation must resolve to.
    TS::IField* FieldPointer() { return static_cast<TS::IField*>(field.get()); }

    // Builds `int <P>k__BackingField;` (with the compiler-generated, debugger-browsable and a
    // marker attribute) plus `int P { get { return <P>k__BackingField; } set { ... } }` inside a
    // `class C` and returns the type declaration. `withSetter` drops the setter; `useDerivedField`
    // / `useDerivedAccessors` swap in the non-compiler-generated fakes.
    Syntax::TypeDeclaration* Make(bool withSetter = true, bool useDerivedField = true,
                                  bool useDerivedAccessors = true) {
        if (!useDerivedField) {
            field = std::make_shared<Impl::FakeField>(fixture.compilation);
            field->SetName("<P>k__BackingField");
            field->SetDeclaringType(typeDef);
            field->SetReturnType(fixture.FindType(TS::KnownTypeCode::Int32));
        }
        if (!useDerivedAccessors) {
            getter = std::make_shared<Impl::FakeMethod>(fixture.compilation, TS::SymbolKind::Method);
            getter->SetName("get_P");
            getter->SetDeclaringType(typeDef);
            setter = std::make_shared<Impl::FakeMethod>(fixture.compilation, TS::SymbolKind::Method);
            setter->SetName("set_P");
            setter->SetDeclaringType(typeDef);
        }
        property->SetGetter(getter.get());
        property->SetSetter(withSetter ? setter.get() : nullptr);

        const TS::ITypePtr intType = fixture.FindType(TS::KnownTypeCode::Int32);

        fieldDecl = new Syntax::FieldDeclaration();
        fieldDecl->ReturnType(new Syntax::PrimitiveType("int"));
        fieldDecl->Variables().Add(new Syntax::VariableInitializer("<P>k__BackingField"));
        fieldDecl->AddAnnotation(
            std::make_shared<Sem::MemberResolveResult>(nullptr, AsMember(*field), intType));
        fieldDecl->Attributes().Add(MakeAttributeSection(compilerGeneratedAttr));
        fieldDecl->Attributes().Add(MakeAttributeSection(debuggerBrowsableAttr));
        fieldDecl->Attributes().Add(MakeAttributeSection(markerAttr));

        prop = new Syntax::PropertyDeclaration();
        prop->ReturnType(new Syntax::PrimitiveType("int"));
        prop->Name("P");
        prop->AddAnnotation(
            std::make_shared<Sem::MemberResolveResult>(nullptr, AsMember(*property), intType));

        auto* getterAccessor = new Syntax::Accessor(Syntax::AccessorKind::Getter);
        getterAccessor->Attributes().Add(MakeAttributeSection(compilerGeneratedAttr));
        auto* getterBody = new Syntax::BlockStatement();
        getterRef = new Syntax::IdentifierExpression("<P>k__BackingField");
        getterRef->AddAnnotation(
            std::make_shared<Sem::MemberResolveResult>(nullptr, AsMember(*field), intType));
        getterBody->Statements().Add(new Syntax::ReturnStatement(getterRef));
        getterAccessor->Body(getterBody);
        prop->Getter(getterAccessor);

        if (withSetter) {
            auto* setterAccessor = new Syntax::Accessor(Syntax::AccessorKind::Setter);
            setterAccessor->Attributes().Add(MakeAttributeSection(compilerGeneratedAttr));
            auto* setterBody = new Syntax::BlockStatement();
            auto* assignment = new Syntax::AssignmentExpression(
                new Syntax::IdentifierExpression("<P>k__BackingField"),
                new Syntax::IdentifierExpression("value"));
            setterBody->Statements().Add(new Syntax::ExpressionStatement(assignment));
            setterAccessor->Body(setterBody);
            prop->Setter(setterAccessor);
        }

        typeDecl = new Syntax::TypeDeclaration();
        typeDecl->Name("C");
        typeDecl->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
            std::static_pointer_cast<TS::IType>(typeDef)));
        typeDecl->Members().Add(fieldDecl);
        typeDecl->Members().Add(prop);
        return typeDecl;
    }

    TransformFixture& fixture;
    std::shared_ptr<TestSupport::LookupTypeDefinition> typeDef;
    std::shared_ptr<TestSupport::LookupTypeDefinition> compilerGeneratedAttr;
    std::shared_ptr<TestSupport::LookupTypeDefinition> debuggerBrowsableAttr;
    std::shared_ptr<TestSupport::LookupTypeDefinition> markerAttr;
    std::shared_ptr<Impl::FakeField> field;
    std::shared_ptr<Impl::FakeMethod> getter;
    std::shared_ptr<Impl::FakeMethod> setter;
    std::shared_ptr<Impl::FakeProperty> property;
    Syntax::TypeDeclaration* typeDecl = nullptr;
    Syntax::FieldDeclaration* fieldDecl = nullptr;
    Syntax::PropertyDeclaration* prop = nullptr;
    Syntax::IdentifierExpression* getterRef = nullptr;
};

} // namespace

// A getter/setter pair over a compiler-generated backing field becomes an auto-property: the
// accessor bodies are cleared, the backing field declaration is removed, and its remaining
// attribute moves onto the property with the `field` target.
TEST(PatternStatementTransformTest, ConvertsGetterSetterPairToAutoProperty)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make();

    RunTransform(fixture, *typeDecl);

    EXPECT_TRUE(model.prop->IsAutomaticProperty());
    EXPECT_EQ(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(model.prop->Setter()->Body(), nullptr);
    EXPECT_EQ(model.prop->Getter()->Attributes().Count(), 0);
    EXPECT_EQ(model.prop->Setter()->Attributes().Count(), 0);
    EXPECT_EQ(typeDecl->Members().Count(), 1);
    EXPECT_EQ(typeDecl->Members()[0], static_cast<Syntax::EntityDeclaration*>(model.prop));
    ASSERT_EQ(model.prop->Attributes().Count(), 1);
    EXPECT_EQ(model.prop->Attributes()[0]->AttributeTarget(), "field");
    EXPECT_NE(model.prop->Attributes()[0]->Attributes()[0]
                  ->Type(),
              nullptr);
}

// A get-only property over a compiler-generated backing field becomes an auto-property (the
// read-only pattern).
TEST(PatternStatementTransformTest, ConvertsGetterOnlyPropertyToAutoProperty)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make(/*withSetter=*/false);

    RunTransform(fixture, *typeDecl);

    EXPECT_TRUE(model.prop->IsAutomaticProperty());
    EXPECT_EQ(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(model.prop->Setter(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 1);
}

// With `AutomaticProperties` off the property is left alone.
TEST(PatternStatementTransformTest, KeepsPropertyWhenAutomaticPropertiesDisabled)
{
    TransformFixture fixture;
    fixture.settings.SetAutomaticProperties(false);
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make();

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// A get-only property with `GetterOnlyAutomaticProperties` off is left alone.
TEST(PatternStatementTransformTest, KeepsGetterOnlyPropertyWhenGetterOnlyDisabled)
{
    TransformFixture fixture;
    fixture.settings.SetGetterOnlyAutomaticProperties(false);
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make(/*withSetter=*/false);

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// Accessors that are not compiler-generated block the rewrite (no VB-style `_P` field here).
TEST(PatternStatementTransformTest, KeepsPropertyWhenAccessorsNotCompilerGenerated)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make(/*withSetter=*/true, /*useDerivedField=*/true,
                                /*useDerivedAccessors=*/false);

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// A backing field that is not compiler-generated is not hidden.
TEST(PatternStatementTransformTest, KeepsPropertyWhenFieldNotCompilerGenerated)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make(/*withSetter=*/true, /*useDerivedField=*/false);

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// A property without a resolved symbol is left alone.
TEST(PatternStatementTransformTest, KeepsPropertyWithoutSymbol)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make();
    model.prop->RemoveAnnotations<Sem::ResolveResult>();

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// A `readonly set` accessor blocks the rewrite.
TEST(PatternStatementTransformTest, KeepsPropertyWithReadonlySetter)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make();
    model.prop->Setter()->Modifiers(model.prop->Setter()->Modifiers()
                                    | Syntax::Modifiers::Readonly);

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// A backing field whose declaring type differs from the property's is not hidden.
TEST(PatternStatementTransformTest, KeepsPropertyWhenFieldDeclaringTypeDiffers)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make();
    auto otherType = MakeNamedTypeDef(fixture.compilation, "N", "D");
    model.field->SetDeclaringType(otherType);

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// A backing-field name the regex does not recognize blocks the rewrite.
TEST(PatternStatementTransformTest, KeepsPropertyWhenFieldNameNotBackingField)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto* typeDecl = model.Make();
    model.field->SetName("otherField");

    RunTransform(fixture, *typeDecl);

    EXPECT_FALSE(model.prop->IsAutomaticProperty());
    EXPECT_NE(model.prop->Getter()->Body(), nullptr);
    EXPECT_EQ(typeDecl->Members().Count(), 2);
}

// ---- Backing-field reference replacement (VisitIdentifier) --------------------------

// A reference to an auto-property's compiler backing field is rewritten to the property name and
// the parent expression is re-annotated with a `MemberResolveResult` over the property.
TEST(PatternStatementTransformTest, ReplacesBackingFieldReferenceWithProperty)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "P");
    const auto* mrr = ref->Annotation<Sem::MemberResolveResult>();
    ASSERT_NE(mrr, nullptr);
    EXPECT_EQ(mrr->Member(), AsPropertyMember(*model.property));
}

// The VB-style `_P` backing field is replaced too.
TEST(PatternStatementTransformTest, ReplacesVbBackingFieldReferenceWithProperty)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.field->SetName("_P");
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("_P");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "P");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(),
              AsPropertyMember(*model.property));
}

// With `AutomaticProperties` off the reference is left untouched.
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceWhenAutomaticPropertiesDisabled)
{
    TransformFixture fixture;
    fixture.settings.SetAutomaticProperties(false);
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*model.field));
}

// A name that is not a backing-field name is left untouched.
TEST(PatternStatementTransformTest, KeepsNonBackingFieldReference)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("otherField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "otherField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*model.field));
}

// A backing-field-named identifier whose parent carries no resolve result is left untouched.
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceWithoutResolveResult)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>(), nullptr);
}

// A resolve result whose member is not a field is left untouched.
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceWhenMemberNotField)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.getter), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*model.getter));
}

// A non-compiler-generated backing field is left untouched.
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceWhenFieldNotCompilerGenerated)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto plainField = std::make_shared<Impl::FakeField>(fixture.compilation);
    plainField->SetName("<P>k__BackingField");
    plainField->SetDeclaringType(model.typeDef);
    plainField->SetReturnType(fixture.FindType(TS::KnownTypeCode::Int32));
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*plainField), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*plainField));
}

// A non-compiler-generated accessor blocks the rewrite.
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceWhenAccessorNotCompilerGenerated)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    auto plainGetter = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    plainGetter->SetName("get_P");
    plainGetter->SetDeclaringType(model.typeDef);
    model.property->SetGetter(plainGetter.get());
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*model.field));
}

// A get-only property with `GetterOnlyAutomaticProperties` off blocks the rewrite.
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceWhenGetterOnlyDisabled)
{
    TransformFixture fixture;
    fixture.settings.SetGetterOnlyAutomaticProperties(false);
    AutoPropertyFixture model(fixture);
    model.property->SetSetter(nullptr);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(new Syntax::ExpressionStatement(ref));

    RunTransform(fixture, *block);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*model.field));
}

// A reference inside the property's own accessor is left untouched (rewriting it would create
// a recursive property reference).
TEST(PatternStatementTransformTest, KeepsBackingFieldReferenceInsideOwnAccessor)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});
    model.getter->SetAccessorOwner(AsPropertyMember(*model.property));
    auto* ref = new Syntax::IdentifierExpression("<P>k__BackingField");
    ref->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.field), fixture.FindType(TS::KnownTypeCode::Int32)));
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(new Syntax::ExpressionStatement(ref));
    auto* method = new Syntax::MethodDeclaration();
    method->Name("get_P");
    method->ReturnType(new Syntax::PrimitiveType("int"));
    method->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
        nullptr, AsMember(*model.getter), fixture.FindType(TS::KnownTypeCode::Int32)));
    method->Body(body);

    RunTransform(fixture, *method);

    EXPECT_EQ(ref->Identifier(), "<P>k__BackingField");
    EXPECT_EQ(ref->Annotation<Sem::MemberResolveResult>()->Member(), AsMember(*model.field));
}

// `IsBackingFieldOfAutomaticProperty` recognizes a compiler-generated backing field with a
// matching property and rejects the non-field/non-backing-field/foreign-type shapes.
TEST(PatternStatementTransformTest, IsBackingFieldOfAutomaticPropertyRecognizesShapes)
{
    TransformFixture fixture;
    AutoPropertyFixture model(fixture);
    model.typeDef->SetProperties({static_cast<const TS::IProperty*>(model.property.get())});

    const TS::IProperty* property = nullptr;
    EXPECT_TRUE(Transforms::PatternStatementTransform::IsBackingFieldOfAutomaticProperty(
        *model.field, property));
    EXPECT_EQ(property, static_cast<const TS::IProperty*>(model.property.get()));

    const TS::IProperty* noProperty = nullptr;
    auto plainField = std::make_shared<Impl::FakeField>(fixture.compilation);
    plainField->SetName("<P>k__BackingField");
    plainField->SetDeclaringType(model.typeDef);
    EXPECT_FALSE(Transforms::PatternStatementTransform::IsBackingFieldOfAutomaticProperty(
        *plainField, noProperty));

    const TS::IProperty* wrongName = nullptr;
    model.field->SetName("otherField");
    EXPECT_FALSE(Transforms::PatternStatementTransform::IsBackingFieldOfAutomaticProperty(
        *model.field, wrongName));
}
