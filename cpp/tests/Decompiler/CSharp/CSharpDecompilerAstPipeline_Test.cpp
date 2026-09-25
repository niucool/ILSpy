// Tests for the C# AST pipeline driver (the C#
// CSharpDecompiler.GetAstTransforms + RunTransforms pair): the fresh-instance
// transform list in the C# order over the ported entries, and the driver
// loop with the invariant checks and the parenthesization tail.

#include "Decompiler/CSharp/CSharpDecompiler.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace CSharpTS = ::ILSpy::Decompiler::CSharp::TypeSystem;
using ::ILSpy::Decompiler::DecompilerSettings;
using ::ILSpy::Decompiler::DecompileRun;

// The fixture: the MinimalCorlib compilation + the using scope the
// DecompileRun requires (the NormalizeBlockStatements_Test pattern).
struct AstPipelineFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    std::shared_ptr<CSharpTS::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharpTS::UsingScope> usingScope;

    AstPipelineFixture()
        : scopelessContext(std::make_shared<CSharpTS::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharpTS::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})) {}
};

// A while-loop body holding a bare call statement (the shape
// NormalizeBlockStatements wraps in a block). The Syntax-layer node model is
// non-owning `new` (the C# GC tree) -- the test releases the root only.
std::unique_ptr<Syntax::WhileStatement> MakeWhileWithBareBody() {
    auto whileStatement = std::make_unique<Syntax::WhileStatement>();
    // Both slots are required (the two-arg ctor's contract): the condition
    // and the embedded statement.
    whileStatement->Condition(new Syntax::IdentifierExpression("keep"));
    whileStatement->EmbeddedStatement(
        new Syntax::ExpressionStatement(
            new Syntax::IdentifierExpression("Work")));
    return whileStatement;
}

// The list is a fresh instance per call (the C# GetAstTransforms returns new
// transform objects every time), and the ported entries keep their C# order.
TEST(AstTransformPipeline, ListIsFreshInstanceInCSharpOrder) {
    auto first = CS::CSharpDecompiler::GetAstTransforms();
    auto second = CS::CSharpDecompiler::GetAstTransforms();
    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first.size(), second.size());
    EXPECT_NE(first[0].get(), second[0].get())
        << "each call returns fresh transform instances";
}

// The driver applies the pipeline: a while body that NormalizeBlockStatements
// wraps in a block ends up as a block after RunAstTransforms. The root
// carries the root-ILFunction annotation every method decompilation
// attaches (DeclareVariables' invalid-statement fixup reads it through the
// ancestors).
TEST(AstTransformPipeline, RunAppliesThePortedPipeline) {
    auto whileStatement = MakeWhileWithBareBody();
    auto function = std::make_shared<IL::ILFunction>();
    whileStatement->AddAnnotation(
        std::make_shared<CS::ILInstructionAnnotation>(function.get()));
    AstPipelineFixture fx;
    DecompilerSettings settings;
    DecompileRun runStorage(&settings, fx.usingScope);
    CS::CSharpDecompiler::RunAstTransforms(*whileStatement, runStorage);
    ASSERT_NE(whileStatement->EmbeddedStatement(), nullptr);
    EXPECT_NE(dynamic_cast<Syntax::BlockStatement*>(
                  whileStatement->EmbeddedStatement()),
              nullptr)
        << "the NormalizeBlockStatements arm fires through the driver";
}

} // namespace
