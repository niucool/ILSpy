// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The ExpressionBuilder skeleton suite: the self-contained statics (the operator-name
// tables, the overflow checks, the boxing-unwrap / direction-change / error-expression
// helpers), the DecompileRun prerequisite, and the ctor + entry-family drives over a
// real MinimalCorlib compilation (the Translate leaf arms: LdNull, DefaultValue, LdStr,
// LdcI4/I8 with the type-hint adjustment, LdLoc/LdLoca through ConvertVariable).

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <stdexcept>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::ExpressionBuilder;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace IL = ::ILSpy::Decompiler::IL;

// The default settings bag (the C# `new DecompilerSettings()`).
DecompilerSettings DefaultSettings()
{
    return DecompilerSettings{};
}

// A compilation over MinimalCorlib (the all-known-types module) -- the fixture shape
// the MinimalCorlib tests use. Every KnownTypeCode resolves.
struct BuilderFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;

    BuilderFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(DefaultSettings()), run(&settings, usingScope)
    {
    }

    // The root using scope over the compilation's global namespace (the
    // CSharpTypeResolveContext_Test fixture shape).
    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope()
    {
        auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    IL::ILFunction MakeFunction()
    {
        IL::ILFunction function;
        return function;
    }

    // A Translate over the fixture's builder.
    CSharp::TranslatedExpression Translate(IL::ILInstruction* inst,
                                           const TS::IType* hint = nullptr)
    {
        ExpressionBuilder builder(nullptr, compilation, FixtureContext(), &function_, &settings,
                                  &run);
        return builder.Translate(inst, hint);
    }

    ExpressionBuilder MakeBuilder()
    {
        return ExpressionBuilder(nullptr, compilation, FixtureContext(), &function_, &settings,
                                 &run);
    }

private:
    IL::ILFunction function_;
    // The decompilation context over the compilation (a CSharpTypeResolveContext --
    // the C# `decompilationContext` is a CSharpTypeResolveContext in practice).
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;

    const CSharp::TypeSystem::CSharpTypeResolveContext& FixtureContext()
    {
        if (!context_)
        {
            context_ = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
                compilation.MainModule(), usingScope);
        }
        return *context_;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// DecompileRun

TEST(DecompileRunTest, CtorGuardsRejectNullArguments)
{
    DecompilerSettings settings;
    auto scope = std::shared_ptr<CSharp::TypeSystem::UsingScope>();
    EXPECT_THROW(DecompileRun(nullptr, scope), std::invalid_argument);
    EXPECT_THROW(DecompileRun(&settings, scope), std::invalid_argument);
}

TEST(DecompileRunTest, CtorStoresSettingsAndUsingScope)
{
    DecompilerSettings settings;
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
        compilation.MainModule());
    auto scope = std::make_shared<CSharp::TypeSystem::UsingScope>(
        context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    DecompileRun run(&settings, scope);
    EXPECT_EQ(&run.Settings(), &settings);
    EXPECT_EQ(run.UsingScope(), scope);
    EXPECT_FALSE(run.DefinedSymbols().count("DEBUG"));
    EXPECT_FALSE(run.Namespaces().has_value());
    run.DefinedSymbols().insert("DEBUG");
    EXPECT_TRUE(run.DefinedSymbols().count("DEBUG"));
    run.SetNamespaces(std::unordered_set<std::string>{"System"});
    ASSERT_TRUE(run.Namespaces().has_value());
    EXPECT_TRUE(run.Namespaces()->count("System"));
}

TEST(EnumValueDisplayModeTest, MemberOrder)
{
    EXPECT_EQ(static_cast<int>(EnumValueDisplayMode::None), 0);
    EXPECT_EQ(static_cast<int>(EnumValueDisplayMode::All), 1);
    EXPECT_EQ(static_cast<int>(EnumValueDisplayMode::AllHex), 2);
    EXPECT_EQ(static_cast<int>(EnumValueDisplayMode::FirstOnly), 3);
}

// ---------------------------------------------------------------------------
// The operator-name tables

TEST(ExpressionBuilderStaticsTest, GetAssignmentOperatorTypeFromMetadataName)
{
    DecompilerSettings settings;
    using T = Syntax::AssignmentOperatorType;
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_Addition", settings),
              T::Add);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_Subtraction", settings),
              T::Subtract);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_Multiply", settings),
              T::Multiply);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_Division", settings),
              T::Divide);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_Modulus", settings),
              T::Modulus);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_BitwiseAnd", settings),
              T::BitwiseAnd);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_BitwiseOr", settings),
              T::BitwiseOr);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_ExclusiveOr", settings),
              T::ExclusiveOr);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_LeftShift", settings),
              T::ShiftLeft);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_RightShift", settings),
              T::ShiftRight);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_UnsignedRightShift", settings),
              T::UnsignedShiftRight);
    // Unmatched names.
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("op_Equality", settings),
              std::nullopt);
    EXPECT_EQ(ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName("", settings),
              std::nullopt);
}

TEST(ExpressionBuilderStaticsTest, GetUnaryOperatorTypeFromMetadataName)
{
    using T = Syntax::UnaryOperatorType;
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_Increment", false),
              T::Increment);
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_Increment", true),
              T::PostIncrement);
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_Decrement", false),
              T::Decrement);
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_Decrement", true),
              T::PostDecrement);
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_CheckedIncrement", false),
              T::Increment);
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_CheckedDecrement", true),
              T::PostDecrement);
    EXPECT_EQ(ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName("op_UnaryPlus", false),
              std::nullopt);
}

TEST(ExpressionBuilderStaticsTest, BinaryOperatorMightCheckForOverflow)
{
    using T = Syntax::BinaryOperatorType;
    EXPECT_FALSE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::BitwiseAnd));
    EXPECT_FALSE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::BitwiseOr));
    EXPECT_FALSE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::ExclusiveOr));
    EXPECT_FALSE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::ShiftLeft));
    EXPECT_FALSE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::ShiftRight));
    EXPECT_FALSE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::UnsignedShiftRight));
    EXPECT_TRUE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::Add));
    EXPECT_TRUE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::Subtract));
    EXPECT_TRUE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::Multiply));
    EXPECT_TRUE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::Divide));
    EXPECT_TRUE(ExpressionBuilder::BinaryOperatorMightCheckForOverflow(T::Modulus));
}

TEST(ExpressionBuilderStaticsTest, AssignmentOperatorMightCheckForOverflow)
{
    using T = Syntax::AssignmentOperatorType;
    EXPECT_FALSE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::BitwiseAnd));
    EXPECT_FALSE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::BitwiseOr));
    EXPECT_FALSE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::ExclusiveOr));
    EXPECT_FALSE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::ShiftLeft));
    EXPECT_FALSE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::ShiftRight));
    EXPECT_TRUE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::Add));
    EXPECT_TRUE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::Subtract));
    EXPECT_TRUE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::Multiply));
    EXPECT_TRUE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::Divide));
    EXPECT_TRUE(ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(T::Modulus));
}

TEST(ExpressionBuilderStaticsTest, IsCompatibleWithSign)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    EXPECT_TRUE(ExpressionBuilder::IsCompatibleWithSign(
        compilation.FindType(TS::KnownTypeCode::Int32), TS::Sign::Signed));
    EXPECT_TRUE(ExpressionBuilder::IsCompatibleWithSign(
        compilation.FindType(TS::KnownTypeCode::Int32), TS::Sign::None));
    EXPECT_FALSE(ExpressionBuilder::IsCompatibleWithSign(
        compilation.FindType(TS::KnownTypeCode::Int32), TS::Sign::Unsigned));
    EXPECT_TRUE(ExpressionBuilder::IsCompatibleWithSign(
        compilation.FindType(TS::KnownTypeCode::UInt32), TS::Sign::Unsigned));
}

// ---------------------------------------------------------------------------
// The ctor + entry-family drives over the MinimalCorlib compilation

TEST(ExpressionBuilderTranslateTest, LdNullRendersNullLiteral)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdNull ldnull;
    auto expr = builder.Translate(&ldnull);
    EXPECT_TRUE(dynamic_cast<Syntax::NullReferenceExpression*>(expr.Expression()) != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Null);
}

TEST(ExpressionBuilderTranslateTest, LdStrRendersStringLiteral)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdStr ldstr("hello");
    auto expr = builder.Translate(&ldstr);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<std::string>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "hello");
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Class);
}

TEST(ExpressionBuilderTranslateTest, LdcI4RendersIntLiteral)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(42);
    auto expr = builder.Translate(&ldc);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<std::int32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    // The default hint is UnknownType; the constant keeps its Int32 type.
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Struct);
}

TEST(ExpressionBuilderTranslateTest, LdcI4UnsignedHintRendersUint)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(-1);
    const TS::IType& uintHint = fixture.compilation.FindType(TS::KnownTypeCode::UInt32);
    auto expr = builder.Translate(&ldc, &uintHint);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<std::uint32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 0xFFFFFFFFu);
}

TEST(ExpressionBuilderTranslateTest, LdcI4BooleanHintRendersBool)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(1);
    const TS::IType& boolHint = fixture.compilation.FindType(TS::KnownTypeCode::Boolean);
    auto expr = builder.Translate(&ldc, &boolHint);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<bool>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_TRUE(*value);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Struct);
}

TEST(ExpressionBuilderTranslateTest, LdcI8RendersLongLiteral)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI8 ldc(1234567890123LL);
    auto expr = builder.Translate(&ldc);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<std::int64_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 1234567890123LL);
}

TEST(ExpressionBuilderTranslateTest, DefaultValueRendersDefaultExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::DefaultValue defaultValue(
        std::const_pointer_cast<TS::IType>(
            std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this())));
    auto expr = builder.Translate(&defaultValue);
    EXPECT_TRUE(dynamic_cast<Syntax::DefaultValueExpression*>(expr.Expression()) != nullptr);
}

TEST(ExpressionBuilderTranslateTest, LdLocRendersIdentifier)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
        0);
    variable->Name = "num";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(identifier->Identifier(), "num");
}

TEST(ExpressionBuilderTranslateTest, LdLocThisParameterRendersThis)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Parameter, std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this()),
        -1);
    variable->Name = "this";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    EXPECT_TRUE(dynamic_cast<Syntax::ThisReferenceExpression*>(expr.Expression()) != nullptr);
}

TEST(ExpressionBuilderTranslateTest, LdLocaWrapsRefDirection)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
        0);
    variable->Name = "num";
    IL::LdLoca ldloca(variable);
    auto expr = builder.Translate(&ldloca);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Ref);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(direction->Expression()) != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::ByReference);
}

TEST(ExpressionBuilderTranslateTest, LdLocByRefVariableWrapsRef)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto elementType = std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto byRefType = std::make_shared<TS::ByReferenceType>(elementType);
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Parameter, byRefType, 0);
    variable->Name = "arg";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    // The C# ConvertVariable wraps a by-ref load in a ref DirectionExpression so the
    // 'ref' can be stripped when dereferencing.
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Ref);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::ByReference);
    auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    EXPECT_TRUE(brrr != nullptr);
    EXPECT_EQ(brrr->ReferenceKind(), TS::ReferenceKind::Ref);
}

TEST(ExpressionBuilderTranslateTest, UnportedOpCodeFallsToErrorExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // An opcode whose C# Visit method has not been ported yet: the port's Default
    // renders the 'OpCode not supported' error expression.
    IL::Nop nop;
    auto error = builder.Translate(&nop);
    auto* errorExpr = dynamic_cast<Syntax::ErrorExpression*>(error.Expression());
    ASSERT_TRUE(errorExpr != nullptr);
}

TEST(ExpressionBuilderTranslateTest, TranslateConditionDropsBooleanIdentity)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // An I4 condition already matching the Boolean hint skips the width convert.
    IL::LdcI4 condition(1);
    auto expr = builder.TranslateCondition(&condition, false);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<bool>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_TRUE(*value);
}

TEST(ExpressionBuilderTranslateTest, TranslateConditionNegatesConstant)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A constant condition with the Boolean hint folds to the Boolean constant; the
    // negate path wraps it in the LogicNot '!' node (the C# LogicNot shape).
    IL::LdcI4 condition(0);
    auto expr = builder.TranslateCondition(&condition, true);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(unary->Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<bool>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_FALSE(*value);
}

// ---------------------------------------------------------------------------
// ConvertToBoolean

TEST(ExpressionBuilderConvertTest, ConvertToBooleanConstantInt)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(7);
    auto expr = builder.Translate(&ldc);
    auto converted = expr.ConvertToBoolean(builder, false);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(converted.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<bool>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_TRUE(*value);
}

TEST(ExpressionBuilderConvertTest, ConvertToBooleanNegate)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(3);
    auto expr = builder.Translate(&ldc);
    auto converted = expr.ConvertToBoolean(builder, true);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(converted.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<bool>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_FALSE(*value);
}

TEST(ExpressionBuilderConvertTest, ConvertToBooleanNonConstantRendersComparison)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
        0);
    variable->Name = "num";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    auto converted = expr.ConvertToBoolean(builder, false);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(converted.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::InEquality);
    auto* rr = dynamic_cast<const Sem::OperatorResolveResult*>(converted.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->OperatorType(), TS::ExpressionType::NotEqual);
}

TEST(ExpressionBuilderConvertTest, ConvertToBooleanPointerArmRendersNullComparison)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A pointer-typed expression converts through the != null comparison (the C#
    // Pointer arm of ConvertToBoolean).
    auto voidType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Void).shared_from_this());
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::make_shared<TS::PointerType>(voidType), 0);
    variable->Name = "ptr";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    auto converted = expr.ConvertToBoolean(builder, false);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(converted.Expression());
    ASSERT_TRUE(binary != nullptr);
    auto* rr = dynamic_cast<const Sem::OperatorResolveResult*>(converted.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->OperatorType(), TS::ExpressionType::NotEqual);
}

// ---------------------------------------------------------------------------
// ConvertTo (the cast-insertion machinery)

TEST(ExpressionBuilderConvertTest, ConvertToIdentitySkipsCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(42);
    auto expr = builder.Translate(&ldc);
    auto& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto converted = expr.ConvertTo(const_cast<TS::IType&>(intType), builder);
    EXPECT_EQ(converted.Expression(), expr.Expression());
}

TEST(ExpressionBuilderConvertTest, ConvertToVoidTargetSkipsCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(42);
    auto expr = builder.Translate(&ldc);
    auto& voidType = fixture.compilation.FindType(TS::KnownTypeCode::Void);
    auto converted = expr.ConvertTo(const_cast<TS::IType&>(voidType), builder);
    EXPECT_EQ(converted.Expression(), expr.Expression());
}

TEST(ExpressionBuilderConvertTest, ConvertToInt64FoldsConstantThroughResolverCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The C# path for a compile-time constant: the resolver's ResolveCast folds the
    // constant and ConvertConstantValue re-renders it (no CastExpression wrapper).
    IL::LdcI4 ldc(42);
    auto expr = builder.Translate(&ldc);
    auto& longType = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    auto converted = expr.ConvertTo(const_cast<TS::IType&>(longType), builder);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(converted.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<std::int64_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42LL);
}

TEST(ExpressionBuilderConvertTest, ConvertToInt64InsertsCastForNonConstant)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A non-constant input inserts the explicit cast expression.
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
        0);
    variable->Name = "num";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    auto& longType = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    auto converted = expr.ConvertTo(const_cast<TS::IType&>(longType), builder);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(converted.Expression());
    ASSERT_TRUE(cast != nullptr);
    // The wrapped expression stays the identifier (the cast carries the new type).
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression()) != nullptr);
}

TEST(ExpressionBuilderConvertTest, ConvertToBooleanTargetGoesThroughByte)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdcI4 ldc(1);
    auto expr = builder.Translate(&ldc);
    auto& boolType = fixture.compilation.FindType(TS::KnownTypeCode::Boolean);
    auto converted = expr.ConvertTo(const_cast<TS::IType&>(boolType), builder);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(converted.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<bool>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_TRUE(*value);
}

TEST(ExpressionBuilderConvertTest, ConvertToBooleanFromIntegerRendersConditional)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::const_pointer_cast<TS::IType>(fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
        0);
    variable->Name = "num";
    IL::LdLoc ldloc(variable);
    auto expr = builder.Translate(&ldloc);
    auto& boolType = fixture.compilation.FindType(TS::KnownTypeCode::Boolean);
    auto converted = expr.ConvertTo(const_cast<TS::IType&>(boolType), builder);
    // The non-constant int -> bool path renders the `!= 0` comparison shape.
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(converted.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::InEquality);
}

// ---------------------------------------------------------------------------
// The error-expression shape

TEST(ExpressionBuilderErrorTest, ErrorExpressionCarriesCommentTrivia)
{
    auto error = ExpressionBuilder::ErrorExpression("Test failure message");
    auto* errorExpr = dynamic_cast<Syntax::ErrorExpression*>(error.Expression());
    ASSERT_TRUE(errorExpr != nullptr);
    EXPECT_EQ(error.ResolveResult()->IsError(), true);
}

TEST(ExpressionBuilderDefaultTest, DefaultRendersOpCodeNotSupported)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Nop nop;
    auto error = builder.Translate(&nop);
    auto* errorExpr = dynamic_cast<Syntax::ErrorExpression*>(error.Expression());
    ASSERT_TRUE(errorExpr != nullptr);
    // The C# Default message: "OpCode not supported: Nop".
    EXPECT_TRUE(error.ResolveResult()->IsError());
}

// ---------------------------------------------------------------------------
// UnwrapBoxingConversion / ChangeDirectionExpressionTo

TEST(ExpressionBuilderStaticsTest, UnwrapBoxingConversionNoOpWithoutCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdNull ldnull;
    auto expr = builder.Translate(&ldnull);
    auto unwrapped = ExpressionBuilder::UnwrapBoxingConversion(expr);
    EXPECT_EQ(unwrapped.Expression(), expr.Expression());
}

} // namespace ILSpy::Tests
