// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the DeclareVariables analysis-half tests
// (ICSharpCode.Decompiler/CSharp/Transforms/DeclareVariables.cs): the
// VariableNeedsDeclaration kind table, the insertion-point computation
// (first-use statement, common-parent merge over sibling blocks and nested
// uses, the for-initializer special case, and the UsesInitialValue hoist out
// of loops), the same-block name-collision merge, and ClearAnalysisResults.

#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;
using CS::Transforms::DeclareVariables;

// A `name`-named local of unknown type (the annotation the analysis reads is
// attached to the identifier expressions, not the variable).
IL::ILVariablePtr Local(const std::string& name) {
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                     TS::UnknownType());
    variable->Name = name;
    return variable;
}

// A `name` identifier carrying the variable annotation.
Syntax::IdentifierExpression* Var(const std::string& name,
                                  const IL::ILVariablePtr& variable) {
    auto* expression = new Syntax::IdentifierExpression(name);
    expression->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(variable));
    return expression;
}

// A bare `name();` statement using the variable.
Syntax::ExpressionStatement* Use(const std::string& name,
                                 const IL::ILVariablePtr& variable) {
    return new Syntax::ExpressionStatement(Var(name, variable));
}

// The kind table: the kinds that carry their own declaration are excluded.
TEST(DeclareVariablesTest, VariableNeedsDeclarationKindTable)
{
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::PinnedRegionLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::Parameter));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::ExceptionLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::ExceptionStackSlot));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::UsingLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::ForeachLocal));
    EXPECT_FALSE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::PatternLocal));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::Local));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::StackSlot));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::PinnedLocal));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::InitializerTarget));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::NamedArgument));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::DisplayClassLocal));
    EXPECT_TRUE(DeclareVariables::VariableNeedsDeclaration(IL::VariableKind::DeconstructionInitTemporary));
}

// A variable used twice in one block is declared before its first use's
// statement.
TEST(DeclareVariablesTest, AnalyzeFindsFirstUseStatement)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("v", v);
    block->Statements().Add(stmt1);
    block->Statements().Add(Use("v", v));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), stmt1);
    EXPECT_FALSE(declareVariables.WasMerged(v.get()));
}

// A variable used in both arms of an if is declared before the whole if
// statement (the common parent of the two uses).
TEST(DeclareVariablesTest, AnalyzeMergesSiblingBlocksToCommonParent)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* thenBlock = new Syntax::BlockStatement();
    thenBlock->Statements().Add(Use("v", v));
    auto* elseBlock = new Syntax::BlockStatement();
    elseBlock->Statements().Add(Use("v", v));
    auto* ifStatement = new Syntax::IfElseStatement(
        new Syntax::IdentifierExpression("c"), thenBlock);
    ifStatement->FalseStatement(elseBlock);
    block->Statements().Add(ifStatement);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), ifStatement)
        << "the declaration moves before the if statement";
}

// A variable used before and inside a nested block is declared before the
// first (outer) use.
TEST(DeclareVariablesTest, AnalyzeMergesNestedUseToOuterStatement)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("v", v);
    block->Statements().Add(stmt1);
    auto* nestedBlock = new Syntax::BlockStatement();
    nestedBlock->Statements().Add(Use("v", v));
    block->Statements().Add(
        new Syntax::IfElseStatement(new Syntax::IdentifierExpression("c"), nestedBlock));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), stmt1);
}

// Two same-named variables used in the same block cannot both be declared
// there: ResolveCollisions merges them into one declaration before the first
// use.
TEST(DeclareVariablesTest, SameBlockUsesOfSameNamedVariablesMerge)
{
    IL::ILVariablePtr i1 = Local("i");
    IL::ILVariablePtr i2 = Local("i");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("i", i1);
    block->Statements().Add(stmt1);
    block->Statements().Add(Use("i", i2));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_TRUE(declareVariables.WasMerged(i1.get()));
    EXPECT_TRUE(declareVariables.WasMerged(i2.get()));
    EXPECT_EQ(declareVariables.GetDeclarationPoint(i1.get()), stmt1);
    EXPECT_EQ(declareVariables.GetDeclarationPoint(i2.get()), stmt1)
        << "the merged declaration sits before the first use";
}

// Distinctly-named variables in the same block do not collide.
TEST(DeclareVariablesTest, DistinctNamedVariablesDoNotMerge)
{
    IL::ILVariablePtr a = Local("a");
    IL::ILVariablePtr b = Local("b");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* stmt1 = Use("a", a);
    Syntax::Statement* stmt2 = Use("b", b);
    block->Statements().Add(stmt1);
    block->Statements().Add(stmt2);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_FALSE(declareVariables.WasMerged(a.get()));
    EXPECT_FALSE(declareVariables.WasMerged(b.get()));
    EXPECT_EQ(declareVariables.GetDeclarationPoint(a.get()), stmt1);
    EXPECT_EQ(declareVariables.GetDeclarationPoint(b.get()), stmt2);
}

// The initializer of a for statement can itself declare the variable (with
// scope local to the loop), so a matching `i = ...` initializer keeps the
// insertion point inside the for's initializer list instead of walking up to
// the enclosing block.
TEST(DeclareVariablesTest, ForInitializerKeepsLocalInsertionPoint)
{
    IL::ILVariablePtr i = Local("i");
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* forStatement = new Syntax::ForStatement();
    auto* initializer = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("i", i),
                                         Syntax::AssignmentOperatorType::Assign,
                                         new Syntax::IdentifierExpression("zero")));
    forStatement->Initializers().Add(initializer);
    forStatement->Condition(new Syntax::BinaryOperatorExpression(
        Var("i", i), Syntax::BinaryOperatorType::LessThan,
        new Syntax::IdentifierExpression("n")));
    forStatement->Iterators().Add(new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("i", i),
                                         Syntax::AssignmentOperatorType::Assign,
                                         new Syntax::IdentifierExpression("one"))));
    forStatement->EmbeddedStatement(new Syntax::BlockStatement());
    block->Statements().Add(forStatement);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(i.get()), initializer)
        << "the for-initializer special case keeps the local insertion point";
}

// A variable whose initial value is used (a possible loop-carried dependency)
// is declared OUTSIDE any loop whose scope annotation marks it as a loop (the
// entry point has more than one incoming edge).
TEST(DeclareVariablesTest, UsesInitialValueHoistsOutOfLoop)
{
    IL::ILVariablePtr v = Local("v");
    v->UsesInitialValue = true;
    // The loop's scope annotation: a BlockContainer whose entry point is a
    // multi-predecessor block (the ILAst loop shape).
    auto container = std::make_shared<IL::BlockContainer>();
    container->Blocks.push_back(std::make_unique<IL::Block>());
    container->Blocks.front()->IncomingEdgeCount = 2;

    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* before = new Syntax::ExpressionStatement(
        new Syntax::IdentifierExpression("work0"));
    block->Statements().Add(before);
    auto* body = new Syntax::BlockStatement();
    body->Statements().Add(Use("v", v));
    auto* loop = new Syntax::WhileStatement(
        new Syntax::IdentifierExpression("cond"), body);
    loop->AddAnnotation(std::make_shared<CS::ILInstructionAnnotation>(container.get()));
    block->Statements().Add(loop);

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);

    EXPECT_EQ(declareVariables.GetDeclarationPoint(v.get()), loop)
        << "the loop-carried dependency is declared before the loop";
}

// ClearAnalysisResults drops the analysis state (a later GetDeclarationPoint
// is the unknown-variable throw).
TEST(DeclareVariablesTest, ClearAnalysisResultsDropsTheDict)
{
    IL::ILVariablePtr v = Local("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    block->Statements().Add(Use("v", v));

    DeclareVariables declareVariables;
    declareVariables.Analyze(*block);
    declareVariables.ClearAnalysisResults();

    EXPECT_THROW(declareVariables.GetDeclarationPoint(v.get()), std::out_of_range);
    EXPECT_THROW(declareVariables.WasMerged(v.get()), std::out_of_range);
}

// ---- The mutation half (Run) --------------------------------------------------------------

namespace {

// The Run fixture: the settings + the using scope the DecompileRun requires
// plus the type renderer the insertion arms consult (the
// NormalizeBlockStatements_Test pattern + the TransformContext ast-builder
// slot).
struct RunFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CSharpTS::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharpTS::UsingScope> usingScope;
    DecompilerSettings settings;
    Syntax::TypeSystemAstBuilder astBuilder;

    RunFixture()
        : scopelessContext(std::make_shared<CSharpTS::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharpTS::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})) {}
};

// An Int32-typed local (a type ConvertType renders).
IL::ILVariablePtr LocalInt32(const std::string& name) {
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32));
    variable->Name = name;
    return variable;
}

// A C#-anonymous-type stub (the PatternStatementTransform_Test rig shape):
// the compiler-generated, empty-namespace <>f__AnonymousType shape with a
// read-only property set.
class AnonTypeStub : public TS::TestSupport::LookupTypeDefinition {
public:
    AnonTypeStub(const TS::ICompilation& compilation)
        : TS::TestSupport::LookupTypeDefinition(
              "<>f__AnonymousType0", std::string(),
              TS::FullTypeName("<>f__AnonymousType0"), TS::TypeKind::Class,
              TS::Accessibility::Public, compilation, nullptr),
          getter_(compilation), property_(compilation) {
        property_.SetName("A");
        property_.SetGetter(getter_.Member());
    }
    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::CompilerGenerated;
    }
    std::vector<const TS::IProperty*> GetProperties(
        std::function<bool(const TS::IProperty*)> filter = nullptr,
        TS::GetMemberOptions options = TS::GetMemberOptions::None)
        const override {
        (void)filter;
        (void)options;
        return {&property_};
    }

private:
    class MethodStub : public Impl::FakeMethod {
    public:
        MethodStub(const TS::ICompilation& compilation)
            : Impl::FakeMethod(compilation, TS::SymbolKind::Method) {}
        const TS::IMethod* Member() const {
            return static_cast<const TS::IMethod*>(this);
        }
    };
    MethodStub getter_;
    Impl::FakeProperty property_;
};

// Runs the transform's IAstTransform entry over the tree.
void RunPipeline(Syntax::AstNode& root, RunFixture& fx) {
    DecompileRun runStorage(&fx.settings, fx.usingScope);
    CS::Transforms::TransformContext context;
    context.DecompileRun = &runStorage;
    context.TypeSystemAstBuilder = &fx.astBuilder;
    // The C# TransformContext ctor's third parameter (the compilation the
    // SkipInit arm's FindType consults; the MinimalCorlib reference in the
    // fixture's SimpleCompilation answers Unsafe).
    context.TypeSystem = &fx.compilation;
    CS::Transforms::DeclareVariables declareVariables;
    declareVariables.Run(root, context);
}

// A valid statement using the variable: `target = v;` (an assignment is a
// valid statement expression, so the Run fixup leaves it alone; the
// annotated right side is the use).
Syntax::ExpressionStatement* AssignUse(const std::string& target,
                                       const IL::ILVariablePtr& variable) {
    return new Syntax::ExpressionStatement(new Syntax::AssignmentExpression(
        new Syntax::IdentifierExpression(target),
        Syntax::AssignmentOperatorType::Assign,
        Var(variable->Name, variable)));
}

} // namespace

// `v = a; use(v);` becomes `int v = a; use(v);`: the assignment statement is
// replaced by a variable declaration carrying the assignment's right side as
// the initializer.
TEST(DeclareVariablesTest, RunCombinesDeclarationAndInitializer)
{
    RunFixture fx;
    IL::ILVariablePtr v = LocalInt32("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Expression* right = new Syntax::IdentifierExpression("a");
    auto* assignment = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          right));
    Syntax::Statement* use = AssignUse("w", v);
    block->Statements().Add(assignment);
    block->Statements().Add(use);

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    auto* declaration = dynamic_cast<Syntax::VariableDeclarationStatement*>(
        block->Statements().At(0));
    ASSERT_NE(declaration, nullptr)
        << "the assignment becomes the declaration";
    EXPECT_EQ(block->Statements().At(1), use);
    ASSERT_NE(declaration->Type(), nullptr);
    ASSERT_EQ(declaration->Variables().Count(), 1);
    Syntax::VariableInitializer* initializer = declaration->Variables().At(0);
    EXPECT_EQ(initializer->Name(), "v");
    EXPECT_EQ(initializer->Initializer(), right)
        << "the initializer is the assignment's right side";
    const auto* annotation = initializer->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Variable(), v.get())
        << "the resolve-result annotation moves onto the initializer";
}

// With SeparateLocalVariableDeclarations the declaration stays separate
// (no initializer) and precedes the assignment.
TEST(DeclareVariablesTest, RunKeepsDeclarationSeparateUnderTheSetting)
{
    RunFixture fx;
    fx.settings.SetSeparateLocalVariableDeclarations(true);
    IL::ILVariablePtr v = LocalInt32("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* assignment = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          new Syntax::IdentifierExpression("a")));
    block->Statements().Add(assignment);
    block->Statements().Add(AssignUse("w", v));

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 3);
    auto* declaration = dynamic_cast<Syntax::VariableDeclarationStatement*>(
        block->Statements().At(0));
    ASSERT_NE(declaration, nullptr)
        << "a separate declaration is inserted before the assignment";
    EXPECT_EQ(block->Statements().At(1), assignment);
    ASSERT_EQ(declaration->Variables().Count(), 1);
    Syntax::VariableInitializer* initializer = declaration->Variables().At(0);
    EXPECT_EQ(initializer->Name(), "v");
    EXPECT_EQ(initializer->Initializer(), nullptr)
        << "the separate declaration carries no initializer";
    const auto* annotation = initializer->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Variable(), v.get());
}

// The C# NeedsSkipInit forms (DeclareVariables.cs lines 717-770): a local
// whose initial value is read before any store gets the
// System.Runtime.CompilerServices.Unsafe.SkipInit call -- with the
// out-variables setting, the declaration folds into the call's argument
// (`Unsafe.SkipInit(out int v);`) and no separate declaration renders.
TEST(DeclareVariablesTest, RunInsertsSkipInitCallForUninitializedLocals)
{
    RunFixture fx;
    IL::ILVariablePtr v = LocalInt32("v");
    v->UsesInitialValue = true;
    v->InitialValueIsInitialized = false;
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* use = AssignUse("w", v);
    block->Statements().Add(use);

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 2);
    auto* skipInit = dynamic_cast<Syntax::ExpressionStatement*>(
        block->Statements().At(0));
    ASSERT_NE(skipInit, nullptr) << "the first statement is the SkipInit call";
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(
        skipInit->Expression());
    ASSERT_NE(invocation, nullptr);
    auto* target = dynamic_cast<Syntax::MemberReferenceExpression*>(
        invocation->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->MemberName(), "SkipInit");
    ASSERT_NE(dynamic_cast<Syntax::TypeReferenceExpression*>(target->Target()),
              nullptr)
        << "the target is the Unsafe type reference";
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    auto* outVar = dynamic_cast<Syntax::OutVarDeclarationExpression*>(
        invocation->Arguments().At(0));
    ASSERT_NE(outVar, nullptr)
        << "the declaration folds into the call's argument";
    ASSERT_NE(outVar->Variable(), nullptr);
    EXPECT_EQ(outVar->Variable()->Name(), "v");
    const auto* annotation =
        outVar->Variable()->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Variable(), v.get());
    EXPECT_EQ(block->Statements().At(1), use)
        << "the SkipInit call is inserted before the first use";
}

// Without the out-variables setting the plain form: the separate
// declaration is inserted, then the SkipInit call whose argument is the
// out-direction identifier of the variable.
TEST(DeclareVariablesTest, RunInsertsSkipInitCallWithDirectionExpression)
{
    RunFixture fx;
    fx.settings.SetOutVariables(false);
    IL::ILVariablePtr v = LocalInt32("v");
    v->UsesInitialValue = true;
    v->InitialValueIsInitialized = false;
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Statement* use = AssignUse("w", v);
    block->Statements().Add(use);

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 3);
    auto* declaration = dynamic_cast<Syntax::VariableDeclarationStatement*>(
        block->Statements().At(0));
    ASSERT_NE(declaration, nullptr)
        << "the separate declaration leads";
    auto* skipInit = dynamic_cast<Syntax::ExpressionStatement*>(
        block->Statements().At(1));
    ASSERT_NE(skipInit, nullptr) << "the SkipInit call follows it";
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(
        skipInit->Expression());
    ASSERT_NE(invocation, nullptr);
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(
        invocation->Arguments().At(0));
    ASSERT_NE(direction, nullptr)
        << "the argument is the out direction over the variable";
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Out);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(
        direction->Expression());
    ASSERT_NE(identifier, nullptr);
    EXPECT_EQ(identifier->Identifier(), "v");
    const auto* annotation =
        identifier->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Variable(), v.get());
    EXPECT_EQ(block->Statements().At(2), use);
}

// A variable whose type is a C# anonymous type combines into `var v =
// a;` (the unwritable mangled form is never rendered).
TEST(DeclareVariablesTest, RunCombinesDeclarationWithVarForAnonymousType)
{
    RunFixture fx;
    auto anon = std::make_shared<AnonTypeStub>(fx.compilation);
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, anon);
    v->Name = "v";
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Expression* right = new Syntax::IdentifierExpression("a");
    auto* assignment = new Syntax::ExpressionStatement(
        new Syntax::AssignmentExpression(Var("v", v),
                                          Syntax::AssignmentOperatorType::Assign,
                                          right));
    block->Statements().Add(assignment);
    block->Statements().Add(AssignUse("w", v));

    RunPipeline(*block, fx);

    auto* declaration = dynamic_cast<Syntax::VariableDeclarationStatement*>(
        block->Statements().At(0));
    ASSERT_NE(declaration, nullptr);
    ASSERT_NE(declaration->Type(), nullptr);
    EXPECT_EQ(declaration->Type()->ToString(nullptr), "var")
        << "the anonymous-typed variable declares as var";
}

// The out-var form over an anonymous type declares `out var v` and carries
// the OutVarResolveResult re-annotation.
TEST(DeclareVariablesTest, RunDeclaresOutVariableWithVarForAnonymousType)
{
    RunFixture fx;
    auto anon = std::make_shared<AnonTypeStub>(fx.compilation);
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, anon);
    v->Name = "v";
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* direction = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Out, Var("v", v));
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(new Syntax::IdentifierExpression("M"), "M"));
    invocation->Arguments().Add(direction);
    auto* call = new Syntax::ExpressionStatement(invocation);
    block->Statements().Add(call);

    RunPipeline(*block, fx);

    auto* outVar = dynamic_cast<Syntax::OutVarDeclarationExpression*>(
        invocation->Arguments().At(0));
    ASSERT_NE(outVar, nullptr);
    ASSERT_NE(outVar->Type(), nullptr);
    EXPECT_EQ(outVar->Type()->ToString(nullptr), "var")
        << "the anonymous-typed out variable declares as var";
    EXPECT_NE(outVar->Annotation<Sem::OutVarResolveResult>(), nullptr)
        << "the implicitly-typed out var carries the OutVarResolveResult";
}

// A variable whose only use is an `out` argument of a call is declared at the
// call: `M(out v);` becomes `M(out int v);`.
TEST(DeclareVariablesTest, RunDeclaresOutVariable)
{
    RunFixture fx;
    IL::ILVariablePtr v = LocalInt32("v");
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* direction = new Syntax::DirectionExpression(
        Syntax::FieldDirection::Out, Var("v", v));
    auto* invocation = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(new Syntax::IdentifierExpression("M"), "M"));
    invocation->Arguments().Add(direction);
    auto* call = new Syntax::ExpressionStatement(invocation);
    block->Statements().Add(call);

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 1);
    EXPECT_EQ(block->Statements().At(0), call);
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    auto* outVar = dynamic_cast<Syntax::OutVarDeclarationExpression*>(
        invocation->Arguments().At(0));
    ASSERT_NE(outVar, nullptr) << "the direction becomes an out-var declaration";
    ASSERT_NE(outVar->Variable(), nullptr);
    EXPECT_EQ(outVar->Variable()->Name(), "v");
    const auto* annotation =
        outVar->Variable()->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Variable(), v.get());
}

// A direction-expression statement over a valid inner expression unwraps to
// the inner expression.
TEST(DeclareVariablesTest, RunUnwrapsDirectionExpressionStatement)
{
    RunFixture fx;
    auto block = std::make_unique<Syntax::BlockStatement>();
    auto* inner = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(new Syntax::IdentifierExpression("M"), "M"));
    auto* statement = new Syntax::ExpressionStatement(
        new Syntax::DirectionExpression(Syntax::FieldDirection::Out, inner));
    block->Statements().Add(statement);

    RunPipeline(*block, fx);

    EXPECT_EQ(statement->Expression(), inner)
        << "the direction wrapper is unwrapped";
}

// An invalid expression statement is assigned to the C# 7.0 discard (the
// method's ILFunction annotation rides on the root).
TEST(DeclareVariablesTest, RunAssignsInvalidStatementToDiscard)
{
    RunFixture fx;
    auto function = std::make_shared<IL::ILFunction>();
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::Expression* invalid = new Syntax::IdentifierExpression("a");
    auto* statement = new Syntax::ExpressionStatement(invalid);
    block->Statements().Add(statement);
    block->AddAnnotation(
        std::make_shared<CS::ILInstructionAnnotation>(function.get()));

    RunPipeline(*block, fx);

    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(
        statement->Expression());
    ASSERT_NE(assignment, nullptr) << "the statement becomes a discard assignment";
    auto* discard = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_NE(discard, nullptr);
    EXPECT_EQ(discard->Identifier(), "_");
    EXPECT_EQ(assignment->Right(), invalid);
}

// Two same-named variables merged by ResolveCollisions end up with one
// declaration, and the removed variable's use sites are re-annotated to the
// surviving variable.
TEST(DeclareVariablesTest, RunUpdatesMergedAnnotations)
{
    RunFixture fx;
    IL::ILVariablePtr i1 = LocalInt32("i");
    IL::ILVariablePtr i2 = LocalInt32("i");
    auto block = std::make_unique<Syntax::BlockStatement>();
    Syntax::ExpressionStatement* use1 = AssignUse("x", i1);
    Syntax::ExpressionStatement* use2 = AssignUse("y", i2);
    block->Statements().Add(use1);
    block->Statements().Add(use2);

    RunPipeline(*block, fx);

    ASSERT_EQ(block->Statements().Count(), 3);
    auto* declaration = dynamic_cast<Syntax::VariableDeclarationStatement*>(
        block->Statements().At(0));
    ASSERT_NE(declaration, nullptr)
        << "one declaration covers the merged pair";
    EXPECT_EQ(block->Statements().At(1), use1);
    EXPECT_EQ(block->Statements().At(2), use2);
    const auto* annotation =
        declaration->Variables().At(0)->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->Variable(), i2.get())
        << "the merged declaration belongs to the surviving variable";
    // The removed variable's use site is re-annotated to the survivor.
    auto* use1Right = dynamic_cast<Syntax::IdentifierExpression*>(
        dynamic_cast<Syntax::AssignmentExpression*>(use1->Expression())->Right());
    ASSERT_NE(use1Right, nullptr);
    EXPECT_EQ(CS::GetILVariable(*use1Right), i2.get());
    auto* use2Right = dynamic_cast<Syntax::IdentifierExpression*>(
        dynamic_cast<Syntax::AssignmentExpression*>(use2->Expression())->Right());
    ASSERT_NE(use2Right, nullptr);
    EXPECT_EQ(CS::GetILVariable(*use2Right), i2.get());
}

// The transform occupies its C# GetAstTransforms slot (after
// PatternStatementTransform, ReplaceMethodCallsWithOperators, and the
// deferred IntroduceUnsafeModifier / AddCheckedBlocks, before the deferred
// TransformFieldAndConstructorInitializers).
TEST(DeclareVariablesTest, PipelineCarriesDeclareVariables)
{
    auto transforms = CS::CSharpDecompiler::GetAstTransforms();
    ASSERT_GT(transforms.size(), 2u);
    EXPECT_NE(dynamic_cast<CS::Transforms::DeclareVariables*>(
                  transforms[2].get()),
              nullptr)
        << "DeclareVariables is the third AST transform (after "
           "ReplaceMethodCallsWithOperators)";
}

} // namespace
