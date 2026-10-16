// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the translated-value layer (Decompiler/CSharp): Annotations.hpp (the
// annotation-holder classes + the AnnotationExtensions surface), TranslatedExpression
// (the wrapper-struct family), TranslatedStatement, and TranslationContext -- the
// foundation slice of the C# back end builders (ExpressionBuilder / StatementBuilder /
// CallBuilder consume exactly these shapes).

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/TranslationContext.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/CSharp/TranslatedStatement.hpp"

#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicMemberResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/LocalResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CS = ILSpy::Decompiler::CSharp;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Resolver = ILSpy::Decompiler::CSharp::Resolver;
namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Impl = TS::Implementation;
namespace IL = ILSpy::Decompiler::IL;

namespace {

// A minimal compilation over the MinimalCorlib singleton reference (the
// MinimalCorlib_Test fixture shape): supplies FakeMethod/FakeMember construction and
// FindType(KnownTypeCode) definitions without loading a real module.
struct CorlibFixture {
    TS::SimpleCompilation compilation;

    CorlibFixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr KnownType(TS::KnownTypeCode code) {
        const TS::IType& t = compilation.FindType(code);
        return TS::ITypePtr(const_cast<TS::IType*>(&t), [](TS::IType*) {
            // no-op: the compilation's type system owns the known type
        });
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// The WithILInstruction / WithoutILInstruction binding factories.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, WithILInstructionOnExpressionStoresTheInstruction)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    IL::LdcI4 inst(42);

    CS::ExpressionWithILInstruction bound = CS::WithILInstruction(*expr, &inst);

    EXPECT_EQ(bound.Expression(), expr.get());
    // The annotation IS the non-owning holder (the port's channel stand-in for the
    // C# bare-instruction annotation); the bare instruction is not.
    const CS::ILInstructionAnnotation* holder =
        expr->Annotation<CS::ILInstructionAnnotation>();
    ASSERT_NE(holder, nullptr);
    EXPECT_EQ(holder->Instruction, &inst);
    std::vector<IL::ILInstruction*> instructions = bound.ILInstructions();
    ASSERT_EQ(instructions.size(), 1u);
    EXPECT_EQ(instructions[0], &inst);
}

TEST(AnnotationsTest, WithILInstructionListOverloadKeepsInsertionOrder)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    IL::LdcI4 first(1);
    IL::Nop second;
    IL::LdcI4 third(3);

    CS::ExpressionWithILInstruction bound = CS::WithILInstruction(
        *expr, std::vector<IL::ILInstruction*>{&first, &second, &third});

    std::vector<IL::ILInstruction*> instructions = bound.ILInstructions();
    ASSERT_EQ(instructions.size(), 3u);
    EXPECT_EQ(instructions[0], &first);
    EXPECT_EQ(instructions[1], &second);
    EXPECT_EQ(instructions[2], &third);
    // The C# `Annotations.OfType<ILInstruction>()` reads the same list off the node.
    std::vector<IL::ILInstruction*> fromNode = CS::GetILInstructions(*expr);
    ASSERT_EQ(fromNode.size(), 3u);
    EXPECT_EQ(fromNode[0], &first);
}

TEST(AnnotationsTest, WithoutILInstructionWrapsWithoutAdding)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();

    CS::ExpressionWithILInstruction bound = CS::WithoutILInstruction(*expr);

    EXPECT_EQ(bound.Expression(), expr.get());
    EXPECT_TRUE(bound.ILInstructions().empty());
    EXPECT_TRUE(expr->Annotations().empty());
}

TEST(AnnotationsTest, WithoutILInstructionKeepsExistingAnnotations)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    IL::Nop inst;
    CS::WithILInstruction(*expr, &inst);

    CS::ExpressionWithILInstruction wrapped = CS::WithoutILInstruction(*expr);

    // The wrap-only factory does not duplicate or remove anything.
    ASSERT_EQ(expr->Annotations().size(), 1u);
    ASSERT_EQ(wrapped.ILInstructions().size(), 1u);
    EXPECT_EQ(wrapped.ILInstructions()[0], &inst);
}

TEST(AnnotationsTest, TranslatedStatementBinding)
{
    auto stmt = std::make_unique<Syntax::EmptyStatement>();
    IL::Nop inst;

    CS::TranslatedStatement bound = CS::WithILInstruction(*stmt, &inst);

    EXPECT_EQ(bound.Statement(), stmt.get());
    ASSERT_EQ(bound.ILInstructions().size(), 1u);
    EXPECT_EQ(bound.ILInstructions()[0], &inst);
    // The statement-less factory wraps without adding.
    CS::TranslatedStatement wrapped = CS::WithoutILInstruction(*stmt);
    EXPECT_EQ(wrapped.Statement(), stmt.get());
    EXPECT_EQ(wrapped.ILInstructions().size(), 1u);
}

// ---------------------------------------------------------------------------
// The WithRR binding factories and the UnknownError fallbacks.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, WithRRBindsAnnotationAndWrapper)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    TS::ITypePtr intType = TS::UnknownType();
    auto rr = std::make_shared<Sem::ConstantResolveResult>(intType,
                                                           std::any(std::int32_t(5)));

    CS::ExpressionWithResolveResult bound = CS::WithRR(*expr, rr);

    EXPECT_EQ(bound.Expression(), expr.get());
    EXPECT_EQ(bound.ResolveResult(), rr.get());
    // The C# `ExpressionWithResolveResult` ctor asserts the resolve result IS the
    // node's annotation.
    EXPECT_EQ(expr->Annotation<Sem::ResolveResult>(), rr.get());
    // The C# `IType Type => ResolveResult.Type` property.
    EXPECT_EQ(&bound.Type(), intType.get());
}

TEST(AnnotationsTest, WithRRTranslationFormPreservesInstructions)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    IL::Nop inst;
    CS::ExpressionWithILInstruction withInstruction =
        CS::WithILInstruction(*expr, &inst);
    auto rr = std::make_shared<Sem::ConstantResolveResult>(TS::UnknownType(),
                                                           std::any(std::int32_t(7)));

    CS::TranslatedExpression bound = CS::WithRR(withInstruction, rr);

    EXPECT_EQ(bound.Expression(), expr.get());
    EXPECT_EQ(bound.ResolveResult(), rr.get());
    ASSERT_EQ(bound.ILInstructions().size(), 1u);
    EXPECT_EQ(bound.ILInstructions()[0], &inst);
}

TEST(AnnotationsTest, ExpressionWithResolveResultDefaultsToUnknownError)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();

    CS::ExpressionWithResolveResult bound(expr.get());

    EXPECT_EQ(bound.ResolveResult(), &Sem::ErrorResolveResult::UnknownError());
    // The fallback's type is the SpecialType::UnknownType null object.
    EXPECT_EQ(&bound.Type(), Sem::ErrorResolveResult::UnknownError().TypePtr().get());
}

TEST(AnnotationsTest, TranslatedExpressionDefaultsToUnknownError)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();

    CS::TranslatedExpression bound(expr.get());

    EXPECT_EQ(bound.ResolveResult(), &Sem::ErrorResolveResult::UnknownError());
}

TEST(AnnotationsTest, TranslatedExpressionTypeFollowsResolveResult)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    TS::ITypePtr intType = TS::UnknownType();
    auto rr = std::make_shared<Sem::ConstantResolveResult>(intType,
                                                           std::any(std::int32_t(1)));

    CS::TranslatedExpression bound = CS::WithRR(CS::WithoutILInstruction(*expr), rr);

    EXPECT_EQ(&bound.Type(), intType.get());
}

TEST(AnnotationsTest, TranslatedExpressionHoldsWithoutILInstructionForm)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    auto rr = std::make_shared<Sem::ConstantResolveResult>(TS::UnknownType(),
                                                           std::any(std::int32_t(2)));
    CS::ExpressionWithResolveResult withRR = CS::WithRR(*expr, rr);

    // The C# `WithoutILInstruction(this ExpressionWithResolveResult)` re-wraps the
    // expression's own resolve result without touching the annotations.
    CS::TranslatedExpression bound = CS::WithoutILInstruction(withRR);

    EXPECT_EQ(bound.Expression(), expr.get());
    EXPECT_EQ(bound.ResolveResult(), rr.get());
}

// ---------------------------------------------------------------------------
// UnwrapChild.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, UnwrapChildSameNodeReturnsSelf)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    auto rr = std::make_shared<Sem::ConstantResolveResult>(TS::UnknownType(),
                                                           std::any(std::int32_t(3)));
    CS::TranslatedExpression bound = CS::WithRR(CS::WithoutILInstruction(*expr), rr);

    CS::TranslatedExpression unwrapped = bound.UnwrapChild(expr.get());

    EXPECT_EQ(unwrapped.Expression(), bound.Expression());
    EXPECT_EQ(unwrapped.ResolveResult(), bound.ResolveResult());
}

TEST(AnnotationsTest, UnwrapChildCopiesInstructionsAlongTheChainAndDetaches)
{
    // A two-level tree: cast -> inner. The instruction is annotated on the ROOT; the
    // unwrap walks the ancestor chain and copies the IL-instruction annotations onto
    // the descendant, then detaches it.
    auto type = std::make_unique<Syntax::PrimitiveType>("int");
    auto inner = std::make_unique<Syntax::NullReferenceExpression>();
    auto cast = std::make_unique<Syntax::CastExpression>();
    cast->Type(type.get());
    cast->Expression(inner.get());
    IL::Nop inst;
    CS::WithILInstruction(*cast, &inst);
    auto rootRR = std::make_shared<Sem::ConstantResolveResult>(
        TS::UnknownType(), std::any(std::int32_t(4)));
    CS::TranslatedExpression bound = CS::WithRR(CS::WithoutILInstruction(*cast), rootRR);

    CS::TranslatedExpression unwrapped = bound.UnwrapChild(inner.get());

    EXPECT_EQ(unwrapped.Expression(), inner.get());
    // The descendant is detached from the AST (the C# `descendant.Detach()`).
    EXPECT_EQ(inner->Parent(), nullptr);
    // All IL-instruction annotations from the ancestor chain landed on the
    // descendant.
    std::vector<IL::ILInstruction*> instructions = unwrapped.ILInstructions();
    ASSERT_EQ(instructions.size(), 1u);
    EXPECT_EQ(instructions[0], &inst);
    // The descendant keeps its own resolve result (no root annotation copy -- only
    // the IL-instruction annotations are copied along the chain).
    EXPECT_TRUE(inner->Annotation<Sem::ConstantResolveResult>() == nullptr);
}

TEST(AnnotationsTest, UnwrapChildThrowsForNonDescendant)
{
    auto unrelated = std::make_unique<Syntax::NullReferenceExpression>();
    auto root = std::make_unique<Syntax::NullReferenceExpression>();
    CS::TranslatedExpression bound(root.get());

    EXPECT_THROW((void)bound.UnwrapChild(unrelated.get()), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// GetResolveResult / GetSymbol.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, GetResolveResultFallsBackToUnknownError)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();

    const Sem::ResolveResult* rr = CS::GetResolveResult(*expr);

    EXPECT_EQ(rr, &Sem::ErrorResolveResult::UnknownError());
    EXPECT_TRUE(rr->IsError());
}

TEST(AnnotationsTest, GetResolveResultReturnsTheAnnotation)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    auto rr = std::make_shared<Sem::ConstantResolveResult>(TS::UnknownType(),
                                                           std::any(std::int32_t(8)));
    expr->AddAnnotation(rr);

    EXPECT_EQ(CS::GetResolveResult(*expr), rr.get());
}

TEST(AnnotationsTest, GetSymbolOverLocalResolveResultReturnsTheVariable)
{
    auto variable =
        std::make_shared<Impl::DefaultParameter>(TS::UnknownType(), std::string("local"));
    auto local = std::make_shared<Sem::LocalResolveResult>(variable.get());
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(local);

    const TS::ISymbol* symbol = CS::GetSymbol(*expr);

    EXPECT_EQ(symbol, static_cast<const TS::ISymbol*>(variable.get()));
}

TEST(AnnotationsTest, GetSymbolOverMemberResolveResultReturnsTheMember)
{
    CorlibFixture fx;
    Impl::FakeMethod method(fx.compilation, TS::SymbolKind::Method);
    const TS::IMember* memberAsMember =
        static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(&method));
    auto target =
        std::make_shared<Sem::TypeResolveResult>(fx.KnownType(TS::KnownTypeCode::Int32));
    auto rr = std::make_shared<Sem::MemberResolveResult>(target, memberAsMember);
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(rr);

    const TS::ISymbol* symbol = CS::GetSymbol(*expr);

    EXPECT_EQ(symbol, static_cast<const TS::ISymbol*>(memberAsMember));
}

TEST(AnnotationsTest, GetSymbolOverTypeResolveResultReturnsTheDefinition)
{
    CorlibFixture fx;
    auto rr = std::make_shared<Sem::TypeResolveResult>(fx.KnownType(TS::KnownTypeCode::Int32));
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(rr);

    const TS::ISymbol* symbol = CS::GetSymbol(*expr);

    EXPECT_EQ(symbol, fx.KnownType(TS::KnownTypeCode::Int32).get()->GetDefinition());
}

TEST(AnnotationsTest, GetSymbolOverConversionResolveResultRecursesIntoInput)
{
    auto variable =
        std::make_shared<Impl::DefaultParameter>(TS::UnknownType(), std::string("input"));
    auto input = std::make_shared<Sem::LocalResolveResult>(variable.get());
    auto rr = std::make_shared<Sem::ConversionResolveResult>(
        TS::UnknownType(), input, Sem::Conversions::IdentityConversion());
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(rr);

    const TS::ISymbol* symbol = CS::GetSymbol(*expr);

    EXPECT_EQ(symbol, static_cast<const TS::ISymbol*>(variable.get()));
}

TEST(AnnotationsTest, GetSymbolOverMethodGroupReturnsTheChosenMethod)
{
    CorlibFixture fx;
    Impl::FakeMethod method(fx.compilation, TS::SymbolKind::Method);
    auto mg = std::make_shared<Resolver::MethodGroupResolveResult>(
        nullptr, std::string("Choose"), std::vector<Resolver::MethodListWithDeclaringType>{});
    // The C# `WithChosenMethod` is a ShallowClone with the method set (the original
    // is untouched).
    std::unique_ptr<Resolver::MethodGroupResolveResult> clone = mg->WithChosenMethod(&method);
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(std::shared_ptr<Resolver::MethodGroupResolveResult>(clone.release()));

    const TS::ISymbol* symbol = CS::GetSymbol(*expr);

    EXPECT_EQ(symbol,
              static_cast<const TS::ISymbol*>(static_cast<const TS::IMethod*>(&method)));
    // The original group's chosen method stays unset.
    EXPECT_EQ(mg->ChosenMethod(), nullptr);
}

TEST(AnnotationsTest, GetSymbolOverDynamicResultsReturnsTheSymbol)
{
    CorlibFixture fx;
    Impl::FakeMethod method(fx.compilation, TS::SymbolKind::Method);
    const TS::IMember* memberAsMember =
        static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(&method));
    auto target =
        std::make_shared<Sem::ConstantResolveResult>(TS::UnknownType(), std::any(std::int32_t(0)));
    auto dynamicMember = std::make_shared<Resolver::DynamicMemberResolveResult>(
        target, std::string("Name"), memberAsMember);
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(dynamicMember);

    const TS::ISymbol* symbol = CS::GetSymbol(*expr);

    EXPECT_EQ(symbol, static_cast<const TS::ISymbol*>(memberAsMember));

    // The DynamicInvocationResolveResult arm reads the same Symbol surface.
    Resolver::DynamicInvocationResolveResult dynamicInvocation(
        target, Resolver::DynamicInvocationType::Invocation, {}, {}, memberAsMember);
    const auto* probeRR =
        dynamic_cast<const Resolver::DynamicInvocationResolveResult*>(
            static_cast<const Sem::ResolveResult*>(&dynamicInvocation));
    ASSERT_NE(probeRR, nullptr);
    EXPECT_EQ(probeRR->Symbol(), static_cast<const TS::ISymbol*>(memberAsMember));
}

TEST(AnnotationsTest, GetSymbolReturnsNullForOtherResults)
{
    auto expr = std::make_unique<Syntax::NullReferenceExpression>();
    expr->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
        TS::UnknownType(), std::any(std::int32_t(0))));

    EXPECT_EQ(CS::GetSymbol(*expr), nullptr);
    // An unannotated node has no symbol either.
    auto bare = std::make_unique<Syntax::ThisReferenceExpression>();
    EXPECT_EQ(CS::GetSymbol(*bare), nullptr);
}

// ---------------------------------------------------------------------------
// GetILVariable / WithILVariable.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, GetILVariableReadsIdentifierExpressionAnnotation)
{
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::UnknownType());
    auto expr = std::make_unique<Syntax::IdentifierExpression>();
    expr->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(variable));

    IL::ILVariable* found = CS::GetILVariable(*expr);

    EXPECT_EQ(found, variable.get());
}

TEST(AnnotationsTest, GetILVariableReadsInitializerAndForeachAnnotations)
{
    auto variable =
        std::make_shared<IL::ILVariable>(IL::VariableKind::ForeachLocal, TS::UnknownType());

    auto initializer = std::make_unique<Syntax::VariableInitializer>();
    EXPECT_EQ(CS::GetILVariable(*initializer), nullptr);
    CS::WithILVariable(*initializer, variable);
    EXPECT_EQ(CS::GetILVariable(*initializer), variable.get());

    auto loop = std::make_unique<Syntax::ForeachStatement>();
    EXPECT_EQ(CS::GetILVariable(*loop), nullptr);
    Syntax::ForeachStatement* loopResult = CS::WithILVariable(*loop, variable);
    EXPECT_EQ(loopResult, loop.get());
    EXPECT_EQ(CS::GetILVariable(*loop), variable.get());
    // The WithILVariable-written annotation is an ILVariableResolveResult carrying
    // the variable's own type.
    const CS::ILVariableResolveResult* rr = loop->Annotation<CS::ILVariableResolveResult>();
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(&rr->Type(), variable->Type.get());
}

TEST(AnnotationsTest, ILVariableResolveResultConstructorForms)
{
    TS::ITypePtr explicitType = TS::NoType();
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::UnknownType());

    // The (v) form: the variable's own type is the result type.
    CS::ILVariableResolveResult implicitType(variable);
    EXPECT_EQ(implicitType.Variable(), variable.get());
    EXPECT_EQ(&implicitType.Type(), variable->Type.get());

    // The (v, type) form: the explicit type wins.
    CS::ILVariableResolveResult explicitForm(variable, explicitType);
    EXPECT_EQ(&explicitForm.Type(), explicitType.get());

    // The (v, type) form throws the ArgumentNullException-mapped invalid_argument
    // for a null variable.
    EXPECT_THROW((void)CS::ILVariableResolveResult(nullptr, explicitType),
                 std::invalid_argument);
}

// ---------------------------------------------------------------------------
// CopyAnnotationsFrom / CopyInstructionsFrom.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, CopyAnnotationsFromSharesNonTriviaAnnotations)
{
    auto source = std::make_unique<Syntax::NullReferenceExpression>();
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::UnknownType());
    auto rr = std::make_shared<CS::ILVariableResolveResult>(variable);
    source->AddAnnotation(rr);

    auto target = std::make_unique<Syntax::NullReferenceExpression>();

    CS::CopyAnnotationsFrom(target.get(), *source);

    // The C# reference-sharing: the target holds the SAME annotation object.
    EXPECT_EQ(target->Annotation<CS::ILVariableResolveResult>(), rr.get());
    EXPECT_EQ(source->Annotations().size(), 1u);
}

TEST(AnnotationsTest, CopyAnnotationsFromSkipsTheTriviaHolderAndCopiesTrivia)
{
    auto source = std::make_unique<Syntax::NullReferenceExpression>();
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::UnknownType());
    source->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(variable));
    // AddLeadingTrivia TAKES OWNERSHIP of the trivia (the holder owns it), so the
    // test hands over the raw pointer.
    Syntax::Comment* comment = new Syntax::Comment("hello");
    source->AddLeadingTrivia(comment);

    auto target = std::make_unique<Syntax::NullReferenceExpression>();

    CS::CopyAnnotationsFrom(target.get(), *source);

    // The trivia holder is NOT shared (each holder owns its own deep-copied trivia).
    const Syntax::NodeTrivia* sourceTrivia = source->Annotation<Syntax::NodeTrivia>();
    const Syntax::NodeTrivia* targetTrivia = target->Annotation<Syntax::NodeTrivia>();
    ASSERT_NE(sourceTrivia, nullptr);
    ASSERT_NE(targetTrivia, nullptr);
    EXPECT_NE(sourceTrivia, targetTrivia);
    ASSERT_EQ(targetTrivia->Leading.size(), 1u);
    EXPECT_NE(targetTrivia->Leading.front().get(), comment)
        << "the deep copy is a new trivia object, not the source's";
    EXPECT_EQ(static_cast<Syntax::Comment*>(targetTrivia->Leading.front().get())->Content(),
              "hello");
    // The non-trivia annotations still share.
    EXPECT_NE(target->Annotation<CS::ILVariableResolveResult>(), nullptr);
}

TEST(AnnotationsTest, CopyInstructionsFromCopiesOnlyILInstructions)
{
    auto source = std::make_unique<Syntax::NullReferenceExpression>();
    IL::Nop inst;
    source->AddAnnotation(std::make_shared<CS::ILInstructionAnnotation>(&inst));
    source->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::UnknownType())));

    auto target = std::make_unique<Syntax::NullReferenceExpression>();

    CS::CopyInstructionsFrom(target.get(), *source);

    // Only the IL-instruction annotations are copied (the resolve-result annotation
    // is not).
    std::vector<IL::ILInstruction*> instructions = CS::GetILInstructions(*target);
    ASSERT_EQ(instructions.size(), 1u);
    EXPECT_EQ(instructions[0], &inst);
    EXPECT_TRUE(target->Annotation<CS::ILVariableResolveResult>() == nullptr);
}

// ---------------------------------------------------------------------------
// The holder classes.
// ---------------------------------------------------------------------------

TEST(AnnotationsTest, ForeachAnnotationCarriesTheThreeCalls)
{
    IL::Nop getEnumerator;
    IL::LdcI4 moveNext(1);
    IL::Nop getCurrent;
    CS::ForeachAnnotation annotation(&getEnumerator, &moveNext, &getCurrent);

    EXPECT_EQ(annotation.GetEnumeratorCall, &getEnumerator);
    EXPECT_EQ(annotation.MoveNextCall, &moveNext);
    EXPECT_EQ(annotation.GetCurrentCall, &getCurrent);
}

TEST(AnnotationsTest, ImplicitReturnAnnotationCarriesTheLeave)
{
    IL::Leave leave;
    CS::ImplicitReturnAnnotation annotation(&leave);
    EXPECT_EQ(annotation.Leave, &leave);
}

TEST(AnnotationsTest, ImplicitConversionAnnotationCarriesTheConversionAndTargetType)
{
    TS::ITypePtr targetType = TS::NoType();
    auto input =
        std::make_shared<Sem::ConstantResolveResult>(TS::UnknownType(), std::any(std::int32_t(0)));
    auto conversionRR = std::make_shared<Sem::ConversionResolveResult>(
        targetType, input, Sem::Conversions::IdentityConversion());
    CS::ImplicitConversionAnnotation annotation(conversionRR);

    EXPECT_EQ(annotation.ConversionResolveResult, conversionRR);
    EXPECT_EQ(&annotation.TargetType(), targetType.get());
}

TEST(AnnotationsTest, MemberInitializerAnnotationOwnsTheCopies)
{
    std::vector<std::unique_ptr<Syntax::Expression>> initializers;
    std::unique_ptr<Syntax::Expression> first =
        std::make_unique<Syntax::NullReferenceExpression>();
    std::unique_ptr<Syntax::Expression> second =
        std::make_unique<Syntax::ThisReferenceExpression>();
    Syntax::Expression* firstPtr = first.get();
    Syntax::Expression* secondPtr = second.get();
    initializers.push_back(std::move(first));
    initializers.push_back(std::move(second));

    CS::MemberInitializerInOtherConstructorsAnnotation annotation(std::move(initializers));

    std::vector<Syntax::Expression*> view = annotation.Initializers();
    ASSERT_EQ(view.size(), 2u);
    EXPECT_EQ(view[0], firstPtr);
    EXPECT_EQ(view[1], secondPtr);
}

TEST(AnnotationsTest, QueryClauseAnnotationsCarryTheLambdas)
{
    IL::ILFunction keyBody;
    IL::ILFunction projectionBody;
    CS::QueryGroupClauseAnnotation group(&keyBody, &projectionBody);
    EXPECT_EQ(group.KeyLambda, &keyBody);
    EXPECT_EQ(group.ProjectionLambda, &projectionBody);

    CS::QueryJoinClauseAnnotation join(&keyBody, &projectionBody);
    EXPECT_EQ(join.OnLambda, &keyBody);
    EXPECT_EQ(join.EqualsLambda, &projectionBody);
}

TEST(AnnotationsTest, UseImplicitlyTypedOutAnnotationInstanceIsASharedSingleton)
{
    const CS::UseImplicitlyTypedOutAnnotation& a =
        CS::UseImplicitlyTypedOutAnnotation::Instance();
    const CS::UseImplicitlyTypedOutAnnotation& b =
        CS::UseImplicitlyTypedOutAnnotation::Instance();
    EXPECT_EQ(&a, &b);
}

TEST(AnnotationsTest, LdTokenAnnotationIsAnEmptyMarker)
{
    CS::LdTokenAnnotation annotation;
    (void)annotation;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// TranslationContext.
// ---------------------------------------------------------------------------

TEST(TranslationContextTest, TypeHintDefaultsToNull)
{
    CS::TranslationContext context;
    EXPECT_EQ(context.TypeHint, nullptr);
}

TEST(TranslationContextTest, TypeHintCarriesTheUnknownSentinel)
{
    CS::TranslationContext context;
    TS::ITypePtr unknown = TS::UnknownType();
    context.TypeHint = unknown.get();
    EXPECT_EQ(context.TypeHint, unknown.get());
}
