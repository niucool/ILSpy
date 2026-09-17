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
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Arglist.hpp"
#include "Decompiler/IL/Instructions/RefAnyType.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/TupleTransform.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/Unbox.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/ILTypeExtensions.hpp"
#include "Decompiler/IL/PointerArithmeticOffset.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/Semantics/ThrowResolveResult.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/SizeOfResolveResult.hpp"
#include "Decompiler/Semantics/TypeIsResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"

#include <gtest/gtest.h>

#include <any>
#include <map>
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
// The TypeSystem test stubs (the LookupTypeDefinition enum fixture shape) -- the
// BamlDecompiler suites' TestSupport convention.
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;

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

    // The fixture's own function (the one MakeBuilder/Translate point the builder
    // at) -- the VisitStLoc store-scan walks its live body.
    IL::ILFunction& Function()
    {
        return function_;
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

    // A builder whose decompilation context carries the given current type
    // definition (the field-address arms build a MemberLookup / resolve simple
    // names against it). The typed context is owned by the fixture and outlives
    // the returned builder (the ExpressionBuilder stores it by pointer).
    ExpressionBuilder MakeBuilderForType(const TS::ITypeDefinition* currentTypeDefinition)
    {
        typedContext_ = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule(), usingScope, currentTypeDefinition, nullptr);
        return ExpressionBuilder(nullptr, compilation, *typedContext_, &function_, &settings,
                                 &run);
    }

private:
    IL::ILFunction function_;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> typedContext_;
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


// ---------------------------------------------------------------------------
// The boxing/cast conversion arms (VisitUnboxAny / VisitBox / VisitCastClass):
// the conversion leaves that need no CallBuilder, no statement machinery, and
// no control-flow helpers. Expectations derived from the C# bodies
// (ExpressionBuilder.cs lines 3285-3358) over the MinimalCorlib fixture.

// The fixture's object-typed local (the unboxing/cast source).
std::shared_ptr<IL::ILVariable> ObjectLocal(const BuilderFixture& fixture)
{
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this()),
        0);
    variable->Name = "o";
    return variable;
}

TEST(ExpressionBuilderCastTest, UnboxAnyInt32RendersUnboxingCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto objectVar = ObjectLocal(fixture);
    // unbox.any int32(object) is not the isinst shortcut (int32 is a value type),
    // so it falls through the object conversion to the plain unboxing cast.
    IL::UnboxAny unboxAny(intType, std::make_unique<IL::LdLoc>(objectVar));
    auto expr = builder.Translate(&unboxAny);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    const auto* crr = dynamic_cast<const Sem::ConversionResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(crr != nullptr);
    EXPECT_TRUE(crr->ConversionProperty()->IsUnboxingConversion());
}

TEST(ExpressionBuilderCastTest, UnboxAnyWithIsInstRendersAsExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto objectVar = ObjectLocal(fixture);
    // unbox.any string(isinst string(object)) is the nullable/reference shortcut:
    // "object as string" rather than a cast.
    IL::UnboxAny unboxAny(
        stringType, std::make_unique<IL::IsInst>(stringType, std::make_unique<IL::LdLoc>(objectVar)));
    auto expr = builder.Translate(&unboxAny);
    auto* asExpr = dynamic_cast<Syntax::AsExpression*>(expr.Expression());
    ASSERT_TRUE(asExpr != nullptr);
    const auto* crr = dynamic_cast<const Sem::ConversionResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(crr != nullptr);
    EXPECT_TRUE(crr->ConversionProperty()->IsTryCast());
}

TEST(ExpressionBuilderCastTest, UnboxInt32RendersRefCastDirection)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto objectVar = ObjectLocal(fixture);
    // `unbox int32(object)` yields a managed pointer: `ref (int32)object` with
    // the UnboxingConversion on the cast and a ByReferenceResolveResult on the
    // ref direction (unlike unbox.any, which yields the value directly).
    IL::Unbox unbox(intType, std::make_unique<IL::LdLoc>(objectVar));
    auto expr = builder.Translate(&unbox);
    auto* dir = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(dir != nullptr);
    EXPECT_EQ(dir->FieldDirection(), Syntax::FieldDirection::Ref);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(dir->Expression());
    ASSERT_TRUE(cast != nullptr);
    const auto* crr = dynamic_cast<const Sem::ConversionResolveResult*>(
        CSharp::GetResolveResult(*cast));
    ASSERT_TRUE(crr != nullptr);
    EXPECT_TRUE(crr->ConversionProperty()->IsUnboxingConversion());
    const auto* brr = dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(brr != nullptr);
}

TEST(ExpressionBuilderCastTest, BoxInt32RendersBoxingCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    IL::Box box(intType, std::make_unique<IL::LdcI4>(42));
    auto expr = builder.Translate(&box);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    const auto* crr = dynamic_cast<const Sem::ConversionResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(crr != nullptr);
    EXPECT_TRUE(crr->ConversionProperty()->IsBoxingConversion());
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Object));
}

TEST(ExpressionBuilderCastTest, CastClassStringRendersExplicitCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto objectVar = ObjectLocal(fixture);
    IL::CastClass castClass(stringType, std::make_unique<IL::LdLoc>(objectVar));
    auto expr = builder.Translate(&castClass);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::String));
}

// ---------------------------------------------------------------------------
// The memory-access load/store arms (VisitLdObj / VisitStObj): the typed
// managed/raw load and store, the `unaligned.` Unsafe intrinsic rewrites, and
// the node prefix fields (ISupportsVolatilePrefix / ISupportsUnalignedPrefix).
// Expectations derived from the C# bodies (ExpressionBuilder.cs lines 2857-3125)
// over the MinimalCorlib fixture.

// A local whose type is a raw pointer to `elementType` (the LdObj/StObj address).
std::shared_ptr<IL::ILVariable> PointerLocal(const BuilderFixture& fixture,
                                             const TS::ITypePtr& elementType)
{
    (void)fixture;
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, std::make_shared<TS::PointerType>(elementType), 0);
    variable->Name = "p";
    return variable;
}

TEST(ExpressionBuilderMemoryTest, LdObjOverPointerRendersDereference)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto pointerVar = PointerLocal(fixture, intType);
    // ldobj int32(*p): the pointer is incompatible as a value, so the LdObj helper
    // renders the `*p` dereference.
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(pointerVar), intType);
    auto expr = builder.Translate(&ldObj);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Dereference);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderMemoryTest, LdObjOverRefStripsTheRef)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto intVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    intVar->Name = "x";
    // ldobj int32(ref x): the byref target is compatible, so the managed reference
    // is dereferenced by stripping the `ref` and keeping the identifier.
    IL::LdObj ldObj(std::make_unique<IL::LdLoca>(intVar), intType);
    auto expr = builder.Translate(&ldObj);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression()) != nullptr);
}

TEST(ExpressionBuilderMemoryTest, LdObjUnalignedRendersReadUnaligned)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto pointerVar = PointerLocal(fixture, intType);
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(pointerVar), intType);
    ldObj.UnalignedPrefix = 1;
    auto expr = builder.Translate(&ldObj);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* target = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->MemberName(), "ReadUnaligned");
    EXPECT_EQ(target->TypeArguments().Count(), 1);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderMemoryTest, LdObjTypeHintPrefersCompatibleHint)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto uintType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::UInt32).shared_from_this());
    TS::ITypePtr objectType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto pointerVar = PointerLocal(fixture, objectType);
    // ldobj int32(*p) with an uint32 hint: the type hint is compatible (equal size
    // integer) so the load type is replaced by the hint.
    IL::LdObj ldObj(std::make_unique<IL::LdLoc>(pointerVar), intType);
    auto expr = builder.Translate(&ldObj, uintType.get());
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::UInt32));
}

TEST(ExpressionBuilderMemoryTest, StObjOverPointerRendersAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto pointerVar = PointerLocal(fixture, intType);
    // stobj int32(*p, 5): a raw-pointer store to an unmanaged type is a plain
    // dereference assignment.
    IL::StObj stObj(std::make_unique<IL::LdLoc>(pointerVar),
                    std::make_unique<IL::LdcI4>(5), intType);
    auto expr = builder.Translate(&stObj);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::UnaryOperatorExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Operator(), Syntax::UnaryOperatorType::Dereference);
}

TEST(ExpressionBuilderMemoryTest, StObjOverRefRendersPlainAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto intVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    intVar->Name = "x";
    // stobj int32(ref x, 5): the byref target dereferences to the identifier.
    IL::StObj stObj(std::make_unique<IL::LdLoca>(intVar),
                    std::make_unique<IL::LdcI4>(5), intType);
    auto expr = builder.Translate(&stObj);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assign != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(assign->Left()) != nullptr);
}

TEST(ExpressionBuilderMemoryTest, StObjUnalignedRendersWriteUnaligned)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto pointerVar = PointerLocal(fixture, intType);
    IL::StObj stObj(std::make_unique<IL::LdLoc>(pointerVar),
                    std::make_unique<IL::LdcI4>(5), intType);
    stObj.UnalignedPrefix = 4;
    auto expr = builder.Translate(&stObj);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* target = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->MemberName(), "WriteUnaligned");
    EXPECT_EQ(invocation->Arguments().Count(), 2);
}

TEST(ExpressionBuilderMemoryTest, StObjManagedTypeRendersWrite)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto pointerVar = PointerLocal(fixture, stringType);
    // stobj string(*p, "x"): a managed-typed store to a raw pointer routes through
    // Unsafe.Write<T>.
    IL::StObj stObj(std::make_unique<IL::LdLoc>(pointerVar),
                    std::make_unique<IL::LdStr>("x"), stringType);
    auto expr = builder.Translate(&stObj);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* target = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->MemberName(), "Write");
}

TEST(ExpressionBuilderMemoryTest, LdObjAndStObjDumpRendersPrefixes)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    IL::LdObj ldObj(std::make_unique<IL::LdNull>(), intType);
    ldObj.IsVolatile = true;
    ldObj.UnalignedPrefix = 2;
    std::string dump;
    ldObj.WriteTo(dump);
    EXPECT_EQ(dump, "volatile.unaligned(2).ldobj(System.Int32, ldnull)");

    IL::StObj stObj(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdcI4>(1), intType);
    stObj.UnalignedPrefix = 1;
    std::string stDump;
    stObj.WriteTo(stDump);
    EXPECT_EQ(stDump, "unaligned(1).stobj(System.Int32, ldnull, ldc.i4(1))");
}

TEST(ExpressionBuilderMemoryTest, LdObjAndStObjCloneCarriesPrefixes)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    IL::LdObj ldObj(std::make_unique<IL::LdNull>(), intType);
    ldObj.IsVolatile = true;
    ldObj.UnalignedPrefix = 3;
    auto ldClone = ldObj.Clone();
    auto* ldCloneTyped = dynamic_cast<IL::LdObj*>(ldClone.get());
    ASSERT_TRUE(ldCloneTyped != nullptr);
    EXPECT_TRUE(ldCloneTyped->IsVolatile);
    EXPECT_EQ(ldCloneTyped->UnalignedPrefix, 3);

    IL::StObj stObj(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdcI4>(1), intType);
    stObj.IsVolatile = true;
    stObj.UnalignedPrefix = 4;
    auto stClone = stObj.Clone();
    auto* stCloneTyped = dynamic_cast<IL::StObj*>(stClone.get());
    ASSERT_TRUE(stCloneTyped != nullptr);
    EXPECT_TRUE(stCloneTyped->IsVolatile);
    EXPECT_EQ(stCloneTyped->UnalignedPrefix, 4);
}


// ---------------------------------------------------------------------------
// The array-length arm (VisitLdLen): the `ldlen.i4` / `ldlen.i8` renders over an
// array-typed local. The StackType selects the `Length` (Int32) / `LongLength`
// (Int64) member name, and the array operand is translated against the
// System.Array type hint. Expectations derived from the C# VisitLdLen
// (ExpressionBuilder.cs lines 3088-3116) body over the MinimalCorlib fixture
// (whose System.Array exposes no properties, so the resolve result degrades to the
// plain Int32/Int64 result).

TEST(ExpressionBuilderLdLenTest, LdLenI4RendersLength)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto arrayType = std::make_shared<TS::ArrayType>(intType);
    auto arrayVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, arrayType, 0);
    arrayVar->Name = "a";
    IL::LdLen ldLen(IL::StackType::I4, std::make_unique<IL::LdLoc>(arrayVar));
    auto expr = builder.Translate(&ldLen);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Length");
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(memberRef->Target()) != nullptr);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderLdLenTest, LdLenI8RendersLongLength)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto arrayType = std::make_shared<TS::ArrayType>(intType);
    auto arrayVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, arrayType, 0);
    arrayVar->Name = "a";
    IL::LdLen ldLen(IL::StackType::I8, std::make_unique<IL::LdLoc>(arrayVar));
    auto expr = builder.Translate(&ldLen);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "LongLength");
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Int64));
}

TEST(ExpressionBuilderLdLenTest, LdLenRawIRendersLongLength)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto arrayType = std::make_shared<TS::ArrayType>(intType);
    auto arrayVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, arrayType, 0);
    arrayVar->Name = "a";
    // The raw `ldlen` pushes a native int (StackType::I): every non-I4 stack type
    // selects LongLength.
    IL::LdLen ldLen(IL::StackType::I, std::make_unique<IL::LdLoc>(arrayVar));
    auto expr = builder.Translate(&ldLen);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "LongLength");
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Int64));
}

// The array-element-address arm (VisitLdElema): the `ldelema <T>(array, index)`
// render -- the array indexer wrapped in a `ref` DirectionExpression carrying a
// ByReferenceResolveResult over the element type (the C# VisitLdElema,
// ExpressionBuilder.cs lines 3203-3229). The `withsystemindex` prefix drives the
// System.Index conversion for the index, and a mismatched element type converts the
// array expression to a fresh array of the access type.

TEST(ExpressionBuilderLdElemaTest, LdElemaOverArrayRendersRefIndexer)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto arrayType = std::make_shared<TS::ArrayType>(intType);
    auto arrayVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, arrayType, 0);
    arrayVar->Name = "a";
    auto idxVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 1);
    idxVar->Name = "i";
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(idxVar));
    IL::LdElema ldElema(intType, std::make_unique<IL::LdLoc>(arrayVar), std::move(indices));
    auto expr = builder.Translate(&ldElema);
    auto* dir = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(dir != nullptr);
    EXPECT_EQ(dir->FieldDirection(), Syntax::FieldDirection::Ref);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(dir->Expression());
    ASSERT_TRUE(indexer != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(indexer->Target()) != nullptr);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(brrr != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::ByReference);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(brrr->ElementType()), TS::KnownTypeCode::Int32));
    // The outer DirectionExpression carries no IL annotation (the C#
    // WithoutILInstruction), but the inner indexer keeps it.
    EXPECT_TRUE(expr.ILInstructions().empty());
    std::vector<IL::ILInstruction*> indexerIL = CSharp::GetILInstructions(*indexer);
    ASSERT_EQ(indexerIL.size(), std::size_t(1));
    EXPECT_EQ(indexerIL[0], &ldElema);
}

TEST(ExpressionBuilderLdElemaTest, MismatchedElementTypeConvertsArray)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto longType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    // A scalar int local indexed as if it were an int[]: the translated type is not
    // an array, so the arm builds a fresh array of the access type and converts.
    auto valueVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    valueVar->Name = "v";
    auto idxVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 1);
    idxVar->Name = "i";
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(idxVar));
    IL::LdElema ldElema(longType, std::make_unique<IL::LdLoc>(valueVar), std::move(indices));
    auto expr = builder.Translate(&ldElema);
    auto* dir = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(dir != nullptr);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(dir->Expression());
    ASSERT_TRUE(indexer != nullptr);
    // The array operand was converted to the fresh array type (an explicit cast).
    EXPECT_TRUE(dynamic_cast<Syntax::CastExpression*>(indexer->Target()) != nullptr);
    auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(brrr != nullptr);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(brrr->ElementType()), TS::KnownTypeCode::Int64));
}

TEST(ExpressionBuilderLdElemaTest, WithSystemIndexConvertsIndexToSystemIndex)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto arrayType = std::make_shared<TS::ArrayType>(intType);
    auto arrayVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, arrayType, 0);
    arrayVar->Name = "a";
    auto idxVar = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 1);
    idxVar->Name = "i";
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(idxVar));
    IL::LdElema ldElema(intType, std::make_unique<IL::LdLoc>(arrayVar), std::move(indices));
    ldElema.WithSystemIndex = true;
    auto expr = builder.Translate(&ldElema);
    auto* dir = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(dir != nullptr);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(dir->Expression());
    ASSERT_TRUE(indexer != nullptr);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    const Sem::ResolveResult* argRR = indexer->Arguments().At(0)->Annotation<Sem::ResolveResult>();
    ASSERT_TRUE(argRR != nullptr);
    const TS::IType& systemIndex = fixture.compilation.FindType(TS::KnownTypeCode::Index);
    EXPECT_TRUE(TS::NormalizeTypeVisitor::IgnoreNullabilityAndTuples().EquivalentTypes(
        const_cast<TS::IType&>(argRR->Type()), const_cast<TS::IType&>(systemIndex)));
}

TEST(ExpressionBuilderLdElemaTest, NodeCloneCarriesWithSystemIndex)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(0));
    IL::LdElema ldElema(intType, std::make_unique<IL::LdcI4>(0), std::move(indices));
    ldElema.WithSystemIndex = true;
    std::string dump;
    ldElema.WriteTo(dump);
    EXPECT_EQ(dump.rfind("withsystemindex.ldelema(", 0), 0u);
    auto clone = ldElema.Clone();
    auto* cloneTyped = static_cast<IL::LdElema*>(clone.get());
    EXPECT_TRUE(cloneTyped->WithSystemIndex);
}

// The field-address arms (ConvertField + VisitLdsFlda, the C# lines 302-398 and
// 3196-3201): the field reference through the requires-qualifier / ambiguous-access
// machinery and the `ref` DirectionExpression wrap of the static field address. The
// fixture's IField stub fills the C# IField surface the arms read; the port's IL
// reader leaves the nodes' resolved `Field` unset, so it is wired by hand here.

// A resolved custom attribute stub for the fixed-buffer fixtures: the
// `[FixedBuffer(typeof(T), N)]` decode IsFixedField reads.
class AttributeStub : public TS::IAttribute {
public:
    AttributeStub(TS::ITypePtr type, std::vector<TS::CustomAttributeTypedArgument> args)
        : type_(std::move(type)), args_(std::move(args)) {}
    const TS::IType& AttributeType() const override { return *type_; }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<TS::CustomAttributeTypedArgument> FixedArguments() const override {
        return args_;
    }
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override {
        return {};
    }

private:
    TS::ITypePtr type_;
    std::vector<TS::CustomAttributeTypedArgument> args_;
};

class FieldStub : public TS::IField {
public:
    FieldStub(std::string name, TS::ITypePtr fieldType, const TS::ICompilation& compilation)
        : name_(std::move(name)), fieldType_(std::move(fieldType)), compilation_(&compilation) {}

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const TS::ICompilation& Compilation() const override { return *compilation_; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return declaringTypeDefinition_;
    }
    TS::ITypePtr DeclaringType() const override { return declaringType_; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasAttribute(TS::KnownAttribute kind) const override {
        return GetAttribute(kind) != nullptr;
    }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute kind) const override {
        auto it = knownAttributes_.find(kind);
        return it == knownAttributes_.end() ? nullptr : it->second;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *fieldType_; }
    std::vector<const TS::IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const TS::IMember* Specialize(const TS::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override {
        return obj == this;
    }
    const TS::IType& Type() const override { return *fieldType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool = false) const override { return {}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

    void SetStatic(bool value) { isStatic_ = value; }
    void SetDeclaringType(TS::ITypePtr value) { declaringType_ = std::move(value); }
    void SetDeclaringTypeDefinition(const TS::ITypeDefinition* value) {
        declaringTypeDefinition_ = value;
    }
    void SetKnownAttribute(TS::KnownAttribute kind, const TS::IAttribute* attribute) {
        knownAttributes_[kind] = attribute;
        attributes_.push_back(attribute);
    }

private:
    std::string name_;
    TS::ITypePtr fieldType_;
    const TS::ICompilation* compilation_;
    TS::ITypePtr declaringType_;
    const TS::ITypeDefinition* declaringTypeDefinition_ = nullptr;
    bool isStatic_ = false;
    std::vector<const TS::IAttribute*> attributes_;
    std::map<TS::KnownAttribute, const TS::IAttribute*> knownAttributes_;
};

TEST(ExpressionBuilderFieldTest, ConvertFieldStaticFieldRendersMemberReference)
{
    BuilderFixture fixture;
    auto objectType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    const TS::ITypeDefinition* objectDef =
        fixture.compilation.FindType(TS::KnownTypeCode::Object).GetDefinition();
    auto builder = fixture.MakeBuilderForType(objectDef);

    auto field = std::make_shared<FieldStub>("Value", objectType, fixture.compilation);
    field->SetStatic(true);
    field->SetDeclaringType(objectType);
    field->SetDeclaringTypeDefinition(objectDef);

    auto result = builder.ConvertField(*field);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(result.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Value");
    EXPECT_TRUE(dynamic_cast<Syntax::TypeReferenceExpression*>(memberRef->Target()) != nullptr);
    const auto* mrr = dynamic_cast<const Sem::MemberResolveResult*>(result.ResolveResult());
    ASSERT_TRUE(mrr != nullptr);
    EXPECT_EQ(mrr->Member(), field.get());
}

TEST(ExpressionBuilderFieldTest, ConvertFieldByReferenceTypeWrapsInRefDirection)
{
    BuilderFixture fixture;
    auto objectType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    const TS::ITypeDefinition* objectDef =
        fixture.compilation.FindType(TS::KnownTypeCode::Object).GetDefinition();
    auto builder = fixture.MakeBuilderForType(objectDef);

    auto byRefInt = std::make_shared<TS::ByReferenceType>(intType);
    auto field = std::make_shared<FieldStub>("RefField", byRefInt, fixture.compilation);
    field->SetStatic(true);
    field->SetDeclaringType(objectType);
    field->SetDeclaringTypeDefinition(objectDef);

    auto result = builder.ConvertField(*field);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(result.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Ref);
    auto* byRef = dynamic_cast<const Sem::ByReferenceResolveResult*>(result.ResolveResult());
    ASSERT_TRUE(byRef != nullptr);
    EXPECT_EQ(byRef->ReferenceKind(), TS::ReferenceKind::Ref);
}

TEST(ExpressionBuilderFieldTest, LdsFldaRendersRefDirectionOverFieldReference)
{
    BuilderFixture fixture;
    auto objectType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    const TS::ITypeDefinition* objectDef =
        fixture.compilation.FindType(TS::KnownTypeCode::Object).GetDefinition();
    auto builder = fixture.MakeBuilderForType(objectDef);

    auto field = std::make_shared<FieldStub>("Value", objectType, fixture.compilation);
    field->SetStatic(true);
    field->SetDeclaringType(objectType);
    field->SetDeclaringTypeDefinition(objectDef);

    IL::LdsFlda ldsFlda("System.Object::Value");
    ldsFlda.Field = field;
    auto expr = builder.Translate(&ldsFlda);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Ref);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(direction->Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Value");
    auto* byRef = dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(byRef != nullptr);
    EXPECT_EQ(byRef->ReferenceKind(), TS::ReferenceKind::Ref);
    // The outer DirectionExpression carries no IL-instruction annotation (the C#
    // `.WithoutILInstruction()`); the inner field access keeps the single one.
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*memberRef);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &ldsFlda);
}

TEST(ExpressionBuilderFieldTest, LdsFldaUnresolvedFieldRendersDefaultError)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdsFlda ldsFlda("System.Object::Missing");
    auto expr = builder.Translate(&ldsFlda);
    EXPECT_TRUE(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()) != nullptr);
}

TEST(ExpressionBuilderFieldTest, LdsFldaCloneCarriesResolvedField)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto objectType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    IL::LdsFlda ldsFlda("System.Object::Value");
    ldsFlda.Field = std::make_shared<FieldStub>("Value", objectType, compilation);
    auto clone = ldsFlda.Clone();
    auto* cloneTyped = static_cast<IL::LdsFlda*>(clone.get());
    ASSERT_TRUE(cloneTyped->Field != nullptr);
    EXPECT_EQ(cloneTyped->Field.get(), ldsFlda.Field.get());
}

TEST(ExpressionBuilderFieldTest, IsFixedFieldDecodesFixedBufferAttribute)
{
    BuilderFixture fixture;
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto bufferType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());

    std::vector<TS::CustomAttributeTypedArgument> args;
    args.emplace_back(intType, std::any(intType));
    args.emplace_back(intType, std::any(std::int32_t(16)));
    AttributeStub attribute(bufferType, std::move(args));

    FieldStub field("FixedBuffer", intType, fixture.compilation);
    field.SetKnownAttribute(TS::KnownAttribute::FixedBuffer, &attribute);

    TS::ITypePtr type;
    int count = -1;
    EXPECT_TRUE(CSharp::IsFixedField(field, type, count));
    EXPECT_EQ(type.get(), intType.get());
    EXPECT_EQ(count, 16);

    FieldStub plain("Plain", intType, fixture.compilation);
    TS::ITypePtr noType;
    int noCount = 99;
    EXPECT_FALSE(CSharp::IsFixedField(plain, noType, noCount));
    EXPECT_TRUE(noType == nullptr);
    EXPECT_EQ(noCount, 0);
}

TEST(ExpressionBuilderFieldTest, TupleTransformMatchesItemAndRestChain)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto objectType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    auto tupleType = std::make_shared<TS::TupleType>(
        objectType, std::vector<TS::ITypePtr>{objectType, intType},
        std::vector<std::string>{"", "Second"});

    auto field = std::make_shared<FieldStub>("Item1", objectType, compilation);
    field->SetDeclaringType(tupleType);
    auto target = std::make_unique<IL::LdLoc>(
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::ITypePtr(tupleType)));
    IL::LdFlda inst(std::move(target), "Item1");
    inst.Field = field;

    TS::ITypePtr matchedType;
    IL::ILInstruction* matchedTarget = nullptr;
    int position = 0;
    EXPECT_TRUE(IL::TupleTransform::MatchTupleFieldAccess(inst, matchedType, matchedTarget,
                                                          position));
    EXPECT_EQ(position, 1);
    EXPECT_EQ(matchedType.get(), tupleType.get());
    EXPECT_EQ(matchedTarget, inst.Target.get());

    auto nonItem = std::make_shared<FieldStub>("Value", objectType, compilation);
    nonItem->SetDeclaringType(tupleType);
    IL::LdFlda nonItemInst(
        std::make_unique<IL::LdLoc>(
            std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::ITypePtr(tupleType))),
        "Value");
    nonItemInst.Field = nonItem;
    TS::ITypePtr unusedType;
    IL::ILInstruction* unusedTarget = nullptr;
    int unusedPosition = 7;
    EXPECT_FALSE(IL::TupleTransform::MatchTupleFieldAccess(nonItemInst, unusedType,
                                                           unusedTarget, unusedPosition));
    EXPECT_EQ(unusedPosition, 0);
}

TEST(ExpressionBuilderFieldTest, LdFldaTupleElementRendersNamedMemberReference)
{
    BuilderFixture fixture;
    auto objectType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    const TS::ITypeDefinition* objectDef =
        fixture.compilation.FindType(TS::KnownTypeCode::Object).GetDefinition();
    auto builder = fixture.MakeBuilderForType(objectDef);

    auto tupleType = std::make_shared<TS::TupleType>(
        objectType, std::vector<TS::ITypePtr>{objectType, intType},
        std::vector<std::string>{"", "Second"});
    auto field = std::make_shared<FieldStub>("Item2", intType, fixture.compilation);
    field->SetDeclaringType(tupleType);
    field->SetDeclaringTypeDefinition(objectDef);

    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, TS::ITypePtr(tupleType));
    IL::LdFlda inst(std::make_unique<IL::LdLoca>(variable), "Item2");
    inst.Field = field;

    auto expr = builder.Translate(&inst);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(direction->Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Second");
    const auto* mrr =
        dynamic_cast<const Sem::MemberResolveResult*>(CSharp::GetResolveResult(*memberRef));
    ASSERT_TRUE(mrr != nullptr);
    EXPECT_EQ(mrr->Member(), field.get());
    const auto* byRef = dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(byRef != nullptr);
    EXPECT_EQ(byRef->ReferenceKind(), TS::ReferenceKind::Ref);
}

TEST(ExpressionBuilderFieldTest, LdFldaUnresolvedFieldRendersDefaultError)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::LdFlda ldFlda(std::make_unique<IL::LdLoc>(
                          std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullptr)),
                      "System.Object::Missing");
    auto expr = builder.Translate(&ldFlda);
    EXPECT_TRUE(dynamic_cast<Syntax::ErrorExpression*>(expr.Expression()) != nullptr);
}

TEST(ExpressionBuilderFieldTest, LdFldaFixedBufferRendersFieldIndexer)
{
    BuilderFixture fixture;
    auto objectType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this());
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    const TS::ITypeDefinition* objectDef =
        fixture.compilation.FindType(TS::KnownTypeCode::Object).GetDefinition();
    auto builder = fixture.MakeBuilderForType(objectDef);

    std::vector<TS::CustomAttributeTypedArgument> args;
    args.emplace_back(intType, std::any(intType));
    args.emplace_back(intType, std::any(std::int32_t(4)));
    AttributeStub attribute(objectType, std::move(args));

    auto nestedField = std::make_shared<FieldStub>(
        "Buffer", std::make_shared<TS::PointerType>(intType), fixture.compilation);
    nestedField->SetStatic(true);
    nestedField->SetDeclaringType(objectType);
    nestedField->SetDeclaringTypeDefinition(objectDef);
    nestedField->SetKnownAttribute(TS::KnownAttribute::FixedBuffer, &attribute);

    auto elementField = std::make_shared<FieldStub>("FixedElementField", intType,
                                                    fixture.compilation);
    elementField->SetStatic(true);
    elementField->SetDeclaringType(objectType);
    elementField->SetDeclaringTypeDefinition(objectDef);

    auto nested = std::make_unique<IL::LdFlda>(
        std::make_unique<IL::LdLoc>(
            std::make_shared<IL::ILVariable>(IL::VariableKind::Local, objectType)),
        "Buffer");
    nested->Field = nestedField;
    IL::LdFlda inst(std::move(nested), "FixedElementField");
    inst.Field = elementField;

    auto expr = builder.Translate(&inst);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(direction->Expression());
    ASSERT_TRUE(indexer != nullptr);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    auto* zero = dynamic_cast<Syntax::PrimitiveExpression*>(
        indexer->Arguments().At(0));
    ASSERT_TRUE(zero != nullptr);
}


// The null-conditional arms (VisitNullableRewrap / VisitNullableUnwrap, the C#
// lines 4298-4321): the `?.` join point lifts a non-nullable value-type operand
// into `Nullable<T>` and renders a NullConditionalRewrap; the `?.` dereference
// strips a ref DirectionExpression for a RefInput argument and renders a
// NullConditional over the underlying type. Expectations derived from the C#
// bodies over the MinimalCorlib fixture.

TEST(ExpressionBuilderNullableTest, RewrapLiftsNonNullableValueTypeToNullable)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto var = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    var->Name = "x";
    IL::NullableRewrap rewrap(std::make_unique<IL::LdLoc>(var));
    auto expr = builder.Translate(&rewrap);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::NullConditionalRewrap);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression()) != nullptr);
    // The int operand is a non-nullable value type, so the result is Nullable<int>.
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*unary);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &rewrap);
}

TEST(ExpressionBuilderNullableTest, RewrapKeepsReferenceTypeUnlifted)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    const TS::IType& stringType = fixture.compilation.FindType(TS::KnownTypeCode::String);
    auto stringPtr = std::const_pointer_cast<TS::IType>(stringType.shared_from_this());
    auto var = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, stringPtr, 0);
    var->Name = "s";
    IL::NullableRewrap rewrap(std::make_unique<IL::LdLoc>(var));
    auto expr = builder.Translate(&rewrap);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::NullConditionalRewrap);
    // A reference type is not lifted: the result stays the reference type itself.
    EXPECT_FALSE(TS::IsNullable(expr.Type()));
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::String));
}

TEST(ExpressionBuilderNullableTest, UnwrapRendersNullConditionalOverUnderlyingType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    TS::ITypePtr nullableInt = TS::Create(fixture.compilation, *intType);
    auto var = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 0);
    var->Name = "n";
    IL::NullableUnwrap unwrap(IL::StackType::I4, std::make_unique<IL::LdLoc>(var));
    auto expr = builder.Translate(&unwrap);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::NullConditional);
    // GetUnderlyingType(Nullable<int>) is int (not the nullable itself).
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()), TS::KnownTypeCode::Int32));
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*unary);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &unwrap);
}

TEST(ExpressionBuilderNullableTest, UnwrapRefInputStripsDirectionExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto var = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    var->Name = "x";
    // LdLoca translates to a ref DirectionExpression; the RefInput arm strips it.
    IL::NullableUnwrap unwrap(IL::StackType::I4, std::make_unique<IL::LdLoca>(var),
                              /*refInput=*/true);
    auto expr = builder.Translate(&unwrap);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::NullConditional);
    EXPECT_TRUE(dynamic_cast<Syntax::DirectionExpression*>(unary->Expression()) == nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression()) != nullptr);
}

TEST(ExpressionBuilderNullableTest, NodeDumpsAndClones)
{
    IL::NullableRewrap rewrap(std::make_unique<IL::LdcI4>(1));
    std::string dump;
    rewrap.WriteTo(dump);
    EXPECT_EQ(dump.rfind("nullable.rewrap(", 0), 0u);
    auto rewrapClone = rewrap.Clone();
    EXPECT_EQ(rewrapClone->Op, IL::OpCode::NullableRewrap);

    IL::NullableUnwrap unwrap(IL::StackType::I4, std::make_unique<IL::LdcI4>(1),
                              /*refInput=*/true);
    std::string dump2;
    unwrap.WriteTo(dump2);
    EXPECT_EQ(dump2.rfind("nullable.unwrap.refinput.I4(", 0), 0u);
    auto unwrapClone = unwrap.Clone();
    auto* cloneTyped = static_cast<IL::NullableUnwrap*>(unwrapClone.get());
    EXPECT_TRUE(cloneTyped->RefInput);
    EXPECT_EQ(cloneTyped->ResultTypeField, IL::StackType::I4);
}


// The null-coalescing arm (VisitNullCoalescingInstruction, the C# lines
// 3912-3953): translate both operands, constant-adjust the fallback to the
// value's type, resolve the `??` operator; on an error recover the target type
// (a throw fallback over NoType uses the value's underlying type, two differing
// non-null types fall back to inst.UnderlyingResultType, else the non-null
// operand's type) and convert the operands. Expectations derived from the C#
// body over the MinimalCorlib fixture.

TEST(ExpressionBuilderNullCoalescingTest, RefKindRendersNullCoalescingOverStrings)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto a = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, stringType, 0);
    a->Name = "a";
    auto b = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, stringType, 1);
    b->Name = "b";
    IL::NullCoalescingInstruction coalescing(IL::NullCoalescingKind::Ref,
                                             std::make_unique<IL::LdLoc>(a),
                                             std::make_unique<IL::LdLoc>(b));
    auto expr = builder.Translate(&coalescing);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()),
                                TS::KnownTypeCode::String));
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*binary);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &coalescing);
}

TEST(ExpressionBuilderNullCoalescingTest, NullableKindKeepsNullableResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    TS::ITypePtr nullableInt = TS::Create(fixture.compilation, *intType);
    auto a = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 0);
    a->Name = "a";
    auto b = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 1);
    b->Name = "b";
    IL::NullCoalescingInstruction coalescing(IL::NullCoalescingKind::Nullable,
                                             std::make_unique<IL::LdLoc>(a),
                                             std::make_unique<IL::LdLoc>(b));
    coalescing.UnderlyingResultType = IL::StackType::I4;
    auto expr = builder.Translate(&coalescing);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
    EXPECT_TRUE(TS::IsKnownType(
        const_cast<TS::IType&>(TS::GetUnderlyingType(expr.Type())), TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderNullCoalescingTest, ValueFallbackKindRecoversIntFromUnderlyingResultType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    TS::ITypePtr nullableInt = TS::Create(fixture.compilation, *intType);
    auto a = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 0);
    a->Name = "a";
    auto b = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 1);
    b->Name = "b";
    IL::NullCoalescingInstruction coalescing(
        IL::NullCoalescingKind::NullableWithValueFallback, std::make_unique<IL::LdLoc>(a),
        std::make_unique<IL::LdLoc>(b));
    coalescing.UnderlyingResultType = IL::StackType::I4;
    auto expr = builder.Translate(&coalescing);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()),
                                TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderNullCoalescingTest, ThrowFallbackOverNoTypeUsesValueUnderlyingType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    TS::ITypePtr nullableInt = TS::Create(fixture.compilation, *intType);
    auto a = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 0);
    a->Name = "a";
    IL::NullCoalescingInstruction coalescing(
        IL::NullCoalescingKind::Nullable, std::make_unique<IL::LdLoc>(a),
        std::make_unique<IL::Throw>(std::make_unique<IL::LdNull>()));
    coalescing.UnderlyingResultType = IL::StackType::I4;
    auto expr = builder.Translate(&coalescing);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::NullCoalescing);
    // The throw fallback has NoType, so the recovered target type is the value's
    // underlying type (int), not the nullable itself.
    EXPECT_TRUE(TS::IsKnownType(const_cast<TS::IType&>(expr.Type()),
                                TS::KnownTypeCode::Int32));
}

// The AddressOf arm (VisitAddressOf, the C# lines 4231-4266): classify the
// wrapped value, translate + convert it to the address type, insert a redundant
// cast for a mutable lvalue whose parent chain is not an ldobj (so a mutating C#
// call cannot modify the original), and render a ref DirectionExpression
// carrying a ByReferenceResolveResult. Expectations derived from the C# body over
// the MinimalCorlib fixture.

TEST(ExpressionBuilderAddressOfTest, MutableLocalInsertsCastAndRendersRefDirection)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    v->Name = "v";
    IL::AddressOf addressOf(std::make_unique<IL::LdLoc>(v), intType);
    auto expr = builder.Translate(&addressOf);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Ref);
    // The mutable lvalue gets the redundant cast (the C# copy-forcing shape).
    ASSERT_TRUE(dynamic_cast<Syntax::CastExpression*>(direction->Expression()) != nullptr);
    // The outer direction carries the AddressOf IL annotation.
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*direction);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &addressOf);
    ASSERT_TRUE(dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult())
                != nullptr);
}

TEST(ExpressionBuilderAddressOfTest, ReadonlyLocalSkipsCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    v->Name = "v";
    v->IsRefReadOnly = true;
    IL::AddressOf addressOf(std::make_unique<IL::LdLoc>(v), intType);
    auto expr = builder.Translate(&addressOf);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    // A readonly lvalue is not a MutableLValue, so no copy-forcing cast is inserted.
    EXPECT_EQ(dynamic_cast<Syntax::CastExpression*>(direction->Expression()), nullptr);
}

TEST(ExpressionBuilderAddressOfTest, ConstantRValueSkipsCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    IL::AddressOf addressOf(std::make_unique<IL::LdcI4>(5), intType);
    auto expr = builder.Translate(&addressOf);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    // An rvalue is never cast.
    EXPECT_EQ(dynamic_cast<Syntax::CastExpression*>(direction->Expression()), nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(direction->Expression()) != nullptr);
}

TEST(ExpressionBuilderAddressOfTest, LdObjParentSkipsCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    v->Name = "v";
    auto addressOf = std::make_unique<IL::AddressOf>(std::make_unique<IL::LdLoc>(v), intType);
    auto* addressOfPtr = addressOf.get();
    // The parent ldobj makes the address a pure load (CanIgnoreCopy), so no cast.
    IL::LdObj ldObj(std::move(addressOf), intType);
    auto expr = builder.Translate(addressOfPtr);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(dynamic_cast<Syntax::CastExpression*>(direction->Expression()), nullptr);
}

// The RefAnyType arm (VisitRefAnyType, the C# lines 3386-3394): the
// `__reftype(typedReference).TypeHandle` render -- the RefType
// UndocumentedExpression over the translated argument, the `TypeHandle` member
// reference, and the System.RuntimeTypeHandle resolve result.

TEST(ExpressionBuilderRefAnyTypeTest, RendersRefTypeTypeHandleWithRuntimeTypeHandleResolveResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto v = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    v->Name = "v";
    IL::RefAnyType refAnyType(std::make_unique<IL::LdLoc>(v));
    auto expr = builder.Translate(&refAnyType);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "TypeHandle");
    auto* doc = dynamic_cast<Syntax::UndocumentedExpression*>(memberRef->Target());
    ASSERT_TRUE(doc != nullptr);
    EXPECT_EQ(doc->UndocumentedExpressionType(), Syntax::UndocumentedExpressionType::RefType);
    ASSERT_EQ(doc->Arguments().Count(), 1);
    // The translated argument (the local load) is the UndocumentedExpression argument.
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(doc->Arguments().At(0)) != nullptr);
    // The IL annotation sits on the member reference (the C# WithILInstruction).
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*memberRef);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &refAnyType);
    // The resolve result is the RuntimeTypeHandle type resolve result.
    ASSERT_TRUE(dynamic_cast<const Sem::TypeResolveResult*>(expr.ResolveResult()) != nullptr);
}

// The Arglist arm (VisitArglist, the C# lines 3274-3280): the `__arglist`
// render -- the ArgListAccess UndocumentedExpression with no arguments and a
// System.RuntimeArgumentHandle resolve result, annotated with the IL node.

TEST(ExpressionBuilderArglistTest, RendersArgListAccessWithRuntimeArgumentHandleResolveResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Arglist arglist;
    auto expr = builder.Translate(&arglist);
    auto* doc = dynamic_cast<Syntax::UndocumentedExpression*>(expr.Expression());
    ASSERT_TRUE(doc != nullptr);
    EXPECT_EQ(doc->UndocumentedExpressionType(), Syntax::UndocumentedExpressionType::ArgListAccess);
    EXPECT_EQ(doc->Arguments().Count(), 0);
    // The IL annotation sits on the UndocumentedExpression (the C# WithILInstruction).
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*doc);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &arglist);
    // The C# `new TypeResolveResult(...)` over System.RuntimeArgumentHandle.
    ASSERT_TRUE(dynamic_cast<const Sem::TypeResolveResult*>(expr.ResolveResult()) != nullptr);
}

// The ILInlining.ClassifyExpression / IsReadonlyReference helpers (the C#
// ILInlining.cs lines 557-645) the AddressOf arm composes.

TEST(ILInliningClassifyExpressionTest, LocalKindsClassify)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto local = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    IL::LdLoc localLoad(local);
    EXPECT_EQ(IL::ClassifyExpression(&localLoad), IL::ExpressionClassification::MutableLValue);

    auto foreachVar = std::make_shared<IL::ILVariable>(IL::VariableKind::ForeachLocal, intType, 1);
    IL::LdLoc foreachLoad(foreachVar);
    EXPECT_EQ(IL::ClassifyExpression(&foreachLoad),
              IL::ExpressionClassification::ReadonlyLValue);

    auto usingVar = std::make_shared<IL::ILVariable>(IL::VariableKind::UsingLocal, intType, 2);
    IL::LdLoc usingLoad(usingVar);
    EXPECT_EQ(IL::ClassifyExpression(&usingLoad), IL::ExpressionClassification::ReadonlyLValue);

    local->IsRefReadOnly = true;
    EXPECT_EQ(IL::ClassifyExpression(&localLoad), IL::ExpressionClassification::ReadonlyLValue);
}

TEST(ILInliningClassifyExpressionTest, LdObjStObjOverReadonlyFieldClassifyReadonly)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto target = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    auto readonlyField = std::make_unique<IL::LdFlda>(std::make_unique<IL::LdLoc>(target),
                                                      "ns::T::F");
    readonlyField->FieldIsReadOnly = true;
    IL::LdObj load(std::move(readonlyField), intType);
    EXPECT_EQ(IL::ClassifyExpression(&load), IL::ExpressionClassification::ReadonlyLValue);

    auto mutableField = std::make_unique<IL::LdFlda>(std::make_unique<IL::LdLoc>(target),
                                                     "ns::T::G");
    IL::LdObj mutableLoad(std::move(mutableField), intType);
    EXPECT_EQ(IL::ClassifyExpression(&mutableLoad),
              IL::ExpressionClassification::MutableLValue);

    auto storeField = std::make_unique<IL::LdFlda>(std::make_unique<IL::LdLoc>(target),
                                                   "ns::T::F");
    storeField->FieldIsReadOnly = true;
    IL::StObj store(std::move(storeField), std::make_unique<IL::LdcI4>(1), intType);
    EXPECT_EQ(IL::ClassifyExpression(&store), IL::ExpressionClassification::ReadonlyLValue);
}

TEST(ILInliningClassifyExpressionTest, CallOverArrayClassifiesMutable)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    IL::Call arrayCall("ns::T::Get");
    arrayCall.DeclaringType = std::make_shared<TS::ArrayType>(intType);
    EXPECT_EQ(IL::ClassifyExpression(&arrayCall),
              IL::ExpressionClassification::MutableLValue);

    IL::Call plainCall("ns::T::M");
    plainCall.DeclaringType = intType;
    EXPECT_EQ(IL::ClassifyExpression(&plainCall), IL::ExpressionClassification::RValue);
}

TEST(ILInliningClassifyExpressionTest, DefaultAndConstantClassifyRValue)
{
    IL::LdcI4 constant(1);
    EXPECT_EQ(IL::ClassifyExpression(&constant), IL::ExpressionClassification::RValue);
}

TEST(ILInliningClassifyExpressionTest, IsReadonlyReferenceArms)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto local = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    IL::LdLoc localLoad(local);
    EXPECT_FALSE(IL::IsReadonlyReference(&localLoad));
    local->IsRefReadOnly = true;
    EXPECT_TRUE(IL::IsReadonlyReference(&localLoad));

    IL::AddressOf addressOf(std::make_unique<IL::LdLoc>(local), intType);
    EXPECT_TRUE(IL::IsReadonlyReference(&addressOf));

    IL::LdsFlda staticReadonly("ns::T::F");
    staticReadonly.FieldIsReadOnly = true;
    EXPECT_TRUE(IL::IsReadonlyReference(&staticReadonly));
    IL::LdsFlda staticMutable("ns::T::G");
    EXPECT_FALSE(IL::IsReadonlyReference(&staticMutable));

    IL::Call call("ns::T::M");
    EXPECT_FALSE(IL::IsReadonlyReference(&call));
}


// ---------------------------------------------------------------------------
// The operator-expression arms (VisitBitNot / VisitThrow / the three-valued
// logic arms): the next ExpressionBuilder slice after the leaf loads -- the
// arms that need no CallBuilder and no statement machinery. Expectations
// derived from the C# VisitBitNot (ExpressionBuilder.cs lines 746-777),
// VisitThrow (1226-1231), and HandleThreeValuedLogic (1197-1224) bodies over
// the MinimalCorlib fixture.

TEST(ExpressionBuilderOperatorTest, BitNotOverIntConstantFoldsResolveResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::BitNot bitNot(std::make_unique<IL::LdcI4>(42));
    auto expr = builder.Translate(&bitNot);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::BitNot);
    // GetSize(Int32) == GetSize(I4): no extension cast is inserted.
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(unary->Expression());
    ASSERT_TRUE(primitive != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    // The resolver folds the constant operand into the resolve result (~42).
    const auto* constantRR = dynamic_cast<const Sem::ConstantResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(constantRR != nullptr);
    auto folded = std::any_cast<std::int32_t>(constantRR->ConstantValue());
    EXPECT_EQ(folded, -43);
}

TEST(ExpressionBuilderOperatorTest, BitNotOverIntLocalNoConversion)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
        0);
    variable->Name = "num";
    IL::BitNot bitNot(std::make_unique<IL::LdLoc>(variable));
    auto expr = builder.Translate(&bitNot);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::BitNot);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression()) != nullptr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::OnesComplement);
    EXPECT_FALSE(opRR->IsLiftedOperator());
    EXPECT_FALSE(TS::IsNullable(expr.Type()));
}

TEST(ExpressionBuilderOperatorTest, BitNotOverByteLocalPromotesToInt32)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Byte).shared_from_this()),
        0);
    variable->Name = "b";
    IL::BitNot bitNot(std::make_unique<IL::LdLoc>(variable));
    auto expr = builder.Translate(&bitNot);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    // GetSize(GetStackType(Byte)) == GetSize(I4): no extension cast. The
    // resolver's unary numeric promotion (char..uint16 -> int32) answers the
    // built-in `~` over the PROMOTED type -- `~b` is valid C#.
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression()) != nullptr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::OnesComplement);
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderOperatorTest, BitNotOverBoolLocalExtendsToUnsigned)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Boolean).shared_from_this()),
        0);
    variable->Name = "flag";
    IL::BitNot bitNot(std::make_unique<IL::LdLoc>(variable));
    auto expr = builder.Translate(&bitNot);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    // bool does not support ~ in C#: the ConvertTo Boolean->integer arm renders
    // the argument as the ternary `flag ? 1 : 0` over the arithmetic type.
    auto* ternary = dynamic_cast<Syntax::ConditionalExpression*>(unary->Expression());
    ASSERT_TRUE(ternary != nullptr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::OnesComplement);
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::UInt32));
}

TEST(ExpressionBuilderOperatorTest, BitNotOverCharLocalExtendsToUnsigned)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Char).shared_from_this()),
        0);
    variable->Name = "c";
    IL::BitNot bitNot(std::make_unique<IL::LdLoc>(variable));
    auto expr = builder.Translate(&bitNot);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::CastExpression*>(unary->Expression()) != nullptr);
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::UInt32));
}

TEST(ExpressionBuilderOperatorTest, BitNotTypeHintSignDrivesExtension)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Char).shared_from_this()),
        0);
    variable->Name = "c";
    IL::BitNot bitNot(std::make_unique<IL::LdLoc>(variable));
    // A SIGNED type hint drives the extension sign before the argument type's
    // own sign is consulted: the hint's Int64 sign picks int32, not uint32.
    const TS::IType& int64Type = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    auto expr = builder.Translate(&bitNot, &int64Type);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(unary->Expression());
    ASSERT_TRUE(cast != nullptr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::OnesComplement);
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderOperatorTest, BitNotLiftedOverNullableResolvesLiftedOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto int32Type =
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto nullableInt = TS::Create(fixture.compilation, *int32Type);
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 0);
    variable->Name = "maybeNum";
    // The lifted form: the argument stays the Nullable<T> load; the underlying
    // result type is the original I4.
    IL::BitNot bitNot(std::make_unique<IL::LdLoc>(variable), true, IL::StackType::I4);
    auto expr = builder.Translate(&bitNot);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::BitNot);
    // GetStackType(Int32) == GetSize(I4): no extension cast; the lifted
    // operator table answers over Nullable<int>.
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression()) != nullptr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::OnesComplement);
    EXPECT_TRUE(opRR->IsLiftedOperator());
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
}

TEST(ExpressionBuilderOperatorTest, ThrowRendersThrowExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::Throw throwInst(std::make_unique<IL::LdStr>("boom"));
    auto expr = builder.Translate(&throwInst);
    auto* throwExpr = dynamic_cast<Syntax::ThrowExpression*>(expr.Expression());
    ASSERT_TRUE(throwExpr != nullptr);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(throwExpr->Expression());
    ASSERT_TRUE(primitive != nullptr);
    const std::string* value = std::get_if<std::string>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, "boom");
    EXPECT_TRUE(dynamic_cast<const Sem::ThrowResolveResult*>(expr.ResolveResult()) != nullptr);
}

TEST(ExpressionBuilderOperatorTest, ThreeValuedBoolAndBuildsLiftedBitwiseAnd)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto boolType =
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Boolean).shared_from_this());
    auto var1 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, boolType, 0);
    var1->Name = "flag1";
    auto var2 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, boolType, 1);
    var2->Name = "flag2";
    IL::ThreeValuedBoolAnd and3vl(std::make_unique<IL::LdLoc>(var1),
                                  std::make_unique<IL::LdLoc>(var2));
    auto expr = builder.Translate(&and3vl);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::BitwiseAnd);
    // The left operand converts to plain bool (identity); the right operand
    // converts to Nullable<bool> -- the TypeErasure equivalence ignores only
    // REFERENCE-type nullability, so the value-type wrap is a real cast.
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Left()) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::CastExpression*>(binary->Right()) != nullptr);
    // The resolve result is the LIFTED bitwise-and over Nullable<bool>.
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::And);
    EXPECT_TRUE(opRR->IsLiftedOperator());
    EXPECT_EQ(opRR->Operands().size(), 2u);
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
}

TEST(ExpressionBuilderOperatorTest, ThreeValuedBoolOrBuildsLiftedBitwiseOr)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto boolType =
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Boolean).shared_from_this());
    auto var1 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, boolType, 0);
    var1->Name = "flag1";
    auto var2 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, boolType, 1);
    var2->Name = "flag2";
    IL::ThreeValuedBoolOr or3vl(std::make_unique<IL::LdLoc>(var1),
                                std::make_unique<IL::LdLoc>(var2));
    auto expr = builder.Translate(&or3vl);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::BitwiseOr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::Or);
    EXPECT_TRUE(opRR->IsLiftedOperator());
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
}

TEST(ExpressionBuilderOperatorTest, ThreeValuedBoolAndNullableLeftConvertsNullableRight)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto int32Type =
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto nullableInt = TS::Create(fixture.compilation, *int32Type);
    auto var1 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 0);
    var1->Name = "maybe1";
    auto var2 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt, 1);
    var2->Name = "maybe2";
    IL::ThreeValuedBoolAnd and3vl(std::make_unique<IL::LdLoc>(var1),
                                  std::make_unique<IL::LdLoc>(var2));
    auto expr = builder.Translate(&and3vl);
    // A nullable LEFT operand takes the IsNullable branch: both operands
    // convert to Nullable<bool> -- over the int? loads both sides are the
    // explicit (bool?) casts.
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::BitwiseAnd);
    EXPECT_TRUE(dynamic_cast<Syntax::CastExpression*>(binary->Left()) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::CastExpression*>(binary->Right()) != nullptr);
    const auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::And);
    EXPECT_TRUE(opRR->IsLiftedOperator());
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
}

// ---------------------------------------------------------------------------
// The type-operand expression arms (VisitIsInst / VisitSizeOf / VisitLdTypeToken):
// the next ExpressionBuilder slice after the operator arms. Expectations derived
// from the C# IsType helper (ExpressionBuilder.cs lines 425-432), VisitIsInst
// (434-483), VisitSizeOf (712-734), and VisitLdTypeToken (736-744) bodies over
// the MinimalCorlib fixture.
// ---------------------------------------------------------------------------

TEST(ExpressionBuilderIsInstTest, IsInstOverReferenceTypeRendersAsExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, stringType, 0);
    variable->Name = "s";
    IL::IsInst isInst(std::const_pointer_cast<TS::IType>(
                          fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this()),
                      std::make_unique<IL::LdLoc>(variable));
    auto expr = builder.Translate(&isInst);
    auto* asExpr = dynamic_cast<Syntax::AsExpression*>(expr.Expression());
    ASSERT_TRUE(asExpr != nullptr);
    EXPECT_TRUE(asExpr->Type() != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(asExpr->Expression()) != nullptr);
    // The resolve result is the TryCast conversion over the operand.
    const auto* convRR = dynamic_cast<const Sem::ConversionResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(convRR != nullptr);
    EXPECT_TRUE(convRR->ConversionProperty()->IsTryCast());
    EXPECT_EQ(&convRR->Type(), stringType.get());
}

TEST(ExpressionBuilderIsInstTest, IsInstOverValueTypeWithPureArgumentRendersConditional)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, intType, 0);
    variable->Name = "num";
    IL::IsInst isInst(std::const_pointer_cast<TS::IType>(
                          fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
                      std::make_unique<IL::LdLoc>(variable));
    // The value-type arm never flows through Translate (its DEBUG post-condition
    // assert would fire: IsInst.ResultType is O but the conditional's type is the
    // argument type -- the C# keeps this arm for the consumers that special-case
    // value-type isinsts before Translate ever runs), so the drive is the direct
    // Visit call over the default context.
    auto expr = builder.Visit(&isInst, CSharp::TranslationContext{});
    auto* cond = dynamic_cast<Syntax::ConditionalExpression*>(expr.Expression());
    ASSERT_TRUE(cond != nullptr);
    // The condition is the `expr is T` expression carrying the isinst's annotation.
    auto* isExpr = dynamic_cast<Syntax::IsExpression*>(cond->Condition());
    ASSERT_TRUE(isExpr != nullptr);
    EXPECT_TRUE(isExpr->Type() != nullptr);
    EXPECT_EQ(expr.ILInstructions().size(), std::size_t{0});
    // ... while the IS expression itself carries the isinst.
    EXPECT_EQ(CSharp::GetILInstructions(*isExpr).size(), std::size_t{1});
    // The true arm is a CLONE of the operand (a distinct identifier), the false
    // arm the null literal.
    auto* trueIdent = dynamic_cast<Syntax::IdentifierExpression*>(cond->TrueExpression());
    ASSERT_TRUE(trueIdent != nullptr);
    auto* operandIdent = dynamic_cast<Syntax::IdentifierExpression*>(isExpr->Expression());
    ASSERT_TRUE(operandIdent != nullptr);
    EXPECT_NE(operandIdent, trueIdent);
    EXPECT_TRUE(dynamic_cast<Syntax::NullReferenceExpression*>(cond->FalseExpression()) != nullptr);
    // The conditional carries a plain ResolveResult over the ARGUMENT's type
    // (isinst over a value type yields the boxed value, so the conditional's
    // type is the unboxed argument type).
    const Sem::ResolveResult* rr = expr.ResolveResult();
    EXPECT_TRUE(dynamic_cast<const Sem::ConversionResolveResult*>(rr) == nullptr);
    EXPECT_EQ(&rr->Type(), intType.get());
}

TEST(ExpressionBuilderIsInstTest, IsInstOverValueTypeWithImpureArgumentIsError)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A Call has SideEffect|MayThrow, so the pure check fails and the C# error
    // expression fires.
    IL::IsInst isInst(std::const_pointer_cast<TS::IType>(
                          fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this()),
                      std::make_unique<IL::Call>("Impure"));
    // The direct Visit drive: the same value-type isinst never flows through
    // Translate (see the conditional test above).
    auto expr = builder.Visit(&isInst, CSharp::TranslationContext{});
    auto* errorExpr = dynamic_cast<Syntax::ErrorExpression*>(expr.Expression());
    ASSERT_TRUE(errorExpr != nullptr);
    EXPECT_TRUE(expr.ResolveResult()->IsError());
}

TEST(ExpressionBuilderIsInstTest, IsTypeHelperRendersIsExpressionWithTypeIsResolveResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, stringType, 0);
    variable->Name = "s";
    IL::IsInst isInst(std::const_pointer_cast<TS::IType>(
                          fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this()),
                      std::make_unique<IL::LdLoc>(variable));
    auto expr = builder.IsType(isInst);
    auto* isExpr = dynamic_cast<Syntax::IsExpression*>(expr.Expression());
    ASSERT_TRUE(isExpr != nullptr);
    // The resolve result is the TypeIsResolveResult over the operand.
    const auto* typeIs = dynamic_cast<const Sem::TypeIsResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(typeIs != nullptr);
    EXPECT_EQ(&typeIs->TargetType(), stringType.get());
    ASSERT_TRUE(typeIs->Input() != nullptr);
    EXPECT_EQ(&typeIs->Input()->Type(), stringType.get());
    // The expression's own type is the forwarded boolean type.
    EXPECT_EQ(&expr.Type(), &fixture.compilation.FindType(TS::KnownTypeCode::Boolean));
}

TEST(ExpressionBuilderSizeOfTest, SizeOfOverUnmanagedTypeRendersSizeOfExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    IL::SizeOf sizeOf(intType, "System.Int32");
    auto expr = builder.Translate(&sizeOf);
    auto* sizeOfExpr = dynamic_cast<Syntax::SizeOfExpression*>(expr.Expression());
    ASSERT_TRUE(sizeOfExpr != nullptr);
    EXPECT_TRUE(sizeOfExpr->Type() != nullptr);
    const auto* rr = dynamic_cast<const Sem::SizeOfResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(&rr->ReferencedType(), intType.get());
    // The expression's own type is the forwarded Int32.
    EXPECT_EQ(&expr.Type(), intType.get());
}

TEST(ExpressionBuilderSizeOfTest, SizeOfOverManagedTypeCallsUnsafeIntrinsic)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    IL::SizeOf sizeOf(stringType, "System.String");
    auto expr = builder.Translate(&sizeOf);
    // A managed type is not `sizeof`-able in C#, so the arm renders the
    // System.Unsafe.SizeOf<String>() intrinsic.
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* target = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->MemberName(), "SizeOf");
    EXPECT_TRUE(dynamic_cast<Syntax::TypeReferenceExpression*>(target->Target()) != nullptr);
    EXPECT_EQ(target->TypeArguments().Count(), 1);
    // The intrinsic's own type is Int32.
    EXPECT_EQ(&expr.Type(), &fixture.compilation.FindType(TS::KnownTypeCode::Int32));
}

TEST(ExpressionBuilderLdTypeTokenTest, LdTypeTokenRendersTypeofTypeHandle)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    IL::LdTypeToken token(stringType, "System.String");
    auto expr = builder.Translate(&token);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "TypeHandle");
    // The typeof expression carries its own TypeOfResolveResult over the String type.
    auto* typeofExpr = dynamic_cast<Syntax::TypeOfExpression*>(memberRef->Target());
    ASSERT_TRUE(typeofExpr != nullptr);
    const auto* innerRR = dynamic_cast<const Sem::TypeOfResolveResult*>(
        CSharp::GetResolveResult(*typeofExpr));
    ASSERT_TRUE(innerRR != nullptr);
    EXPECT_EQ(&innerRR->ReferencedType(), stringType.get());
    EXPECT_EQ(&innerRR->Type(), &fixture.compilation.FindType(TS::KnownTypeCode::Type));
    // The outer resolve result is the TypeOfResolveResult over the resolved
    // System.RuntimeTypeHandle (over MinimalCorlib the UnknownType fallback
    // carrying the requested full name).
    const auto* typeOfRR = dynamic_cast<const Sem::TypeOfResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(typeOfRR != nullptr);
    EXPECT_EQ(&typeOfRR->ReferencedType(), stringType.get());
    EXPECT_EQ(expr.Type().ReflectionName(), "System.RuntimeTypeHandle");
}
// ---------------------------------------------------------------------------
// The VisitStLoc assignment arm (the C# ExpressionBuilder.cs lines 809-870):
// the stack-slot type refinement, the by-ref re-assignment `ref (a = ref b)`
// shape, and the plain Assignment. Expectations derived from the C# body over
// the MinimalCorlib fixture.

namespace {

// The Object-typed stack slot the refinement fixtures reuse.
std::shared_ptr<IL::ILVariable> MakeStackSlot(const BuilderFixture& fixture, int storeCount)
{
    auto slot = std::make_shared<IL::ILVariable>(
        IL::VariableKind::StackSlot,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Object).shared_from_this()));
    slot->Name = "S_0";
    slot->StoreCount = storeCount;
    return slot;
}

std::shared_ptr<IL::ILVariable> MakeLocal(const BuilderFixture& fixture,
                                          TS::KnownTypeCode code, const char* name)
{
    auto local = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(code).shared_from_this()));
    local->Name = name;
    return local;
}

} // namespace

TEST(ExpressionBuilderStLocTest, StLocRendersAssignmentWithOperatorResolveResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::StLoc stLoc(variable, std::make_unique<IL::LdcI4>(42));
    auto expr = builder.Translate(&stLoc);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "num");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assign->Right());
    ASSERT_TRUE(right != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&right->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    // The resolve result is the Assign OperatorResolveResult over the variable's
    // type with both operands, and the node carries the StLoc IL annotation.
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::Assign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(2));
    const auto ilInstructions = expr.ILInstructions();
    ASSERT_EQ(ilInstructions.size(), std::size_t(1));
    EXPECT_EQ(ilInstructions[0], &stLoc);
}

TEST(ExpressionBuilderStLocTest, StLocTranslatesTheValueWithTheVariableTypeHint)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = MakeLocal(fixture, TS::KnownTypeCode::Int64, "num");
    IL::StLoc stLoc(variable, std::make_unique<IL::LdcI4>(42));
    auto expr = builder.Translate(&stLoc);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(expr.Expression());
    ASSERT_TRUE(assign != nullptr);
    // The int32 constant was re-typed to the hint (Int64) inside
    // AdjustConstantToType, so the assignment needs no cast.
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int64");
    const auto* rightRR =
        dynamic_cast<const Sem::ResolveResult*>(operatorRR->Operands()[1].get());
    ASSERT_TRUE(rightRR != nullptr);
    EXPECT_EQ(rightRR->Type().ReflectionName(), "System.Int64");
}

TEST(ExpressionBuilderStLocTest, StLocByRefVariableRendersRefReAssignment)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto target = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(
            std::const_pointer_cast<TS::IType>(intType.shared_from_this())));
    target->Name = "refLocal";
    auto source = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(
            std::const_pointer_cast<TS::IType>(intType.shared_from_this())));
    source->Name = "other";
    IL::StLoc stLoc(target, std::make_unique<IL::LdLoca>(source));
    auto expr = builder.Translate(&stLoc);
    // ref (refLocal = ref other)
    auto* outer = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(outer != nullptr);
    EXPECT_EQ(outer->FieldDirection(), Syntax::FieldDirection::Ref);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(outer->Expression());
    ASSERT_TRUE(assign != nullptr);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assign->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "refLocal");
    auto* right = dynamic_cast<Syntax::DirectionExpression*>(assign->Right());
    ASSERT_TRUE(right != nullptr);
    // The SAME ByReferenceResolveResult object rides the outer node (the C#
    // `.WithRR(lhsRefRR)` re-attachment).
    const auto* outerRR =
        dynamic_cast<const Sem::ByReferenceResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(outerRR != nullptr);
    EXPECT_EQ(outerRR->Type().ReflectionName(), "System.Int32&");
}

TEST(ExpressionBuilderStLocTest, StLocRefinesSingleDefinitionStackSlotType)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto slot = MakeStackSlot(fixture, /*storeCount=*/1);
    auto strLocal = MakeLocal(fixture, TS::KnownTypeCode::String, "str");
    IL::StLoc stLoc(slot, std::make_unique<IL::LdLoc>(strLocal));
    builder.Translate(&stLoc);
    // The single-definition slot adopts the value's type (String).
    EXPECT_EQ(slot->Type->ReflectionName(), "System.String");
}

TEST(ExpressionBuilderStLocTest, StLocSkipsRefinementWhenTheSlotWasLoaded)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto slot = MakeStackSlot(fixture, /*storeCount=*/1);
    auto strLocal = MakeLocal(fixture, TS::KnownTypeCode::String, "str");
    IL::LdLoc load(slot);
    // Loading the stack slot first registers it in the loadedVariablesSet.
    builder.Visit(&load, CSharp::TranslationContext{});
    IL::StLoc stLoc(slot, std::make_unique<IL::LdLoc>(strLocal));
    builder.Translate(&stLoc);
    // The load marks the slot as read (its inaccurate Object type is in use).
    EXPECT_EQ(slot->Type->ReflectionName(), "System.Object");
}

TEST(ExpressionBuilderStLocTest,
     StLocRefinesSlotToTheDefaultValueTypeWhenStoresAreInconsistent)
{
    BuilderFixture fixture;
    auto slot = MakeStackSlot(fixture, /*storeCount=*/2);
    auto intLocal = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    auto& fn = fixture.Function();
    fn.Body = std::make_unique<IL::BlockContainer>();
    auto block = std::make_unique<IL::Block>();
    block->Add(std::make_unique<IL::StLoc>(
        slot, std::make_unique<IL::DefaultValue>(
                  std::const_pointer_cast<TS::IType>(
                      fixture.compilation.FindType(TS::KnownTypeCode::Decimal)
                          .shared_from_this()))));
    block->Add(std::make_unique<IL::StLoc>(slot, std::make_unique<IL::LdLoc>(intLocal)));
    fn.Body->AddBlock(std::move(block));
    auto builder = fixture.MakeBuilder();
    IL::StLoc* first = static_cast<IL::StLoc*>(
        static_cast<IL::Block*>(fn.Body->Blocks[0].get())->Instructions[0].get());
    builder.Translate(first);
    // The stores disagree, so the consistent-type arm fails; the
    // default-value arm still adopts the Decimal value type.
    EXPECT_EQ(slot->Type->ReflectionName(), "System.Decimal");
}

TEST(ExpressionBuilderStLocTest, StLocRefinesMultiStoreSlotWhenAllStoresAgree)
{
    BuilderFixture fixture;
    auto slot = MakeStackSlot(fixture, /*storeCount=*/2);
    auto strLocal = MakeLocal(fixture, TS::KnownTypeCode::String, "str");
    auto strLocal2 = MakeLocal(fixture, TS::KnownTypeCode::String, "str2");
    auto& fn = fixture.Function();
    fn.Body = std::make_unique<IL::BlockContainer>();
    auto block = std::make_unique<IL::Block>();
    block->Add(std::make_unique<IL::StLoc>(slot, std::make_unique<IL::LdLoc>(strLocal)));
    block->Add(std::make_unique<IL::StLoc>(slot, std::make_unique<IL::LdLoc>(strLocal2)));
    fn.Body->AddBlock(std::move(block));
    auto builder = fixture.MakeBuilder();
    IL::StLoc* first = static_cast<IL::StLoc*>(
        static_cast<IL::Block*>(fn.Body->Blocks[0].get())->Instructions[0].get());
    builder.Translate(first);
    // Every store infers String, so the consistent-type arm refines the slot.
    EXPECT_EQ(slot->Type->ReflectionName(), "System.String");
}

TEST(ExpressionBuilderStLocTest, StLocKeepsWidenedTypeWhenStoresDisagree)
{
    BuilderFixture fixture;
    auto slot = MakeStackSlot(fixture, /*storeCount=*/2);
    auto strLocal = MakeLocal(fixture, TS::KnownTypeCode::String, "str");
    auto intLocal = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    auto& fn = fixture.Function();
    fn.Body = std::make_unique<IL::BlockContainer>();
    auto block = std::make_unique<IL::Block>();
    block->Add(std::make_unique<IL::StLoc>(slot, std::make_unique<IL::LdLoc>(strLocal)));
    block->Add(std::make_unique<IL::StLoc>(slot, std::make_unique<IL::LdLoc>(intLocal)));
    fn.Body->AddBlock(std::move(block));
    auto builder = fixture.MakeBuilder();
    IL::StLoc* first = static_cast<IL::StLoc*>(
        static_cast<IL::Block*>(fn.Body->Blocks[0].get())->Instructions[0].get());
    builder.Translate(first);
    // Inconsistent stores and no default-value value keep the widened type.
    EXPECT_EQ(slot->Type->ReflectionName(), "System.Object");
}

TEST(ExpressionBuilderStLocTest, StLocNonStLocStoreBlocksTheConsistencyRefinement)
{
    BuilderFixture fixture;
    auto slot = MakeStackSlot(fixture, /*storeCount=*/2);
    auto strLocal = MakeLocal(fixture, TS::KnownTypeCode::String, "str");
    auto& fn = fixture.Function();
    fn.Body = std::make_unique<IL::BlockContainer>();
    auto block = std::make_unique<IL::Block>();
    block->Add(std::make_unique<IL::StLoc>(slot, std::make_unique<IL::LdLoc>(strLocal)));
    // A MatchInstruction is an IStoreInstruction that is not an StLoc -- the C#
    // AllStoresUseConsistentType rejects the list outright.
    block->Add(std::make_unique<IL::MatchInstruction>(slot, std::make_unique<IL::LdNull>()));
    fn.Body->AddBlock(std::move(block));
    auto builder = fixture.MakeBuilder();
    IL::StLoc* first = static_cast<IL::StLoc*>(
        static_cast<IL::Block*>(fn.Body->Blocks[0].get())->Instructions[0].get());
    builder.Translate(first);
    EXPECT_EQ(slot->Type->ReflectionName(), "System.Object");
}

TEST(ExpressionBuilderStLocTest, AssignmentHelperBuildsAssignOperatorResolveResult)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto variable = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::LdcI4 ldc(42);
    auto value = builder.Translate(&ldc);
    // The C# `Assignment(TranslatedExpression, TranslatedExpression)` helper over
    // a variable-typed left side.
    IL::LdLoc ldLoc(variable);
    auto left = builder.Translate(&ldLoc);
    auto result = builder.Assignment(left, value);
    auto* assign = dynamic_cast<Syntax::AssignmentExpression*>(result.Expression());
    ASSERT_TRUE(assign != nullptr);
    const auto* operatorRR =
        dynamic_cast<const Sem::OperatorResolveResult*>(result.ResolveResult());
    ASSERT_TRUE(operatorRR != nullptr);
    EXPECT_EQ(operatorRR->OperatorType(), TS::ExpressionType::Assign);
    EXPECT_EQ(operatorRR->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(operatorRR->Operands().size(), std::size_t(2));
}

// ---------------------------------------------------------------------------
// The ILTypeExtensions port (ILTypeExtensions.cs): the InferType extension the
// AllStoresUseConsistentType helper consumes, the MatchDefaultValue bare match,
// the ILVariable.StackType derived read, and the TypeUtils.
// IsCompatibleTypeForMemoryAccess port the LdElema arm composes.

TEST(ILTypeExtensionsTest, InferTypeLoadsReadTheVariableType)
{
    BuilderFixture fixture;
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::LdLoc ldLoc(local);
    auto type = IL::InferType(ldLoc, &fixture.compilation);
    EXPECT_EQ(type->ReflectionName(), "System.Int32");
    IL::StLoc stLoc(local, std::make_unique<IL::LdcI4>(1));
    type = IL::InferType(stLoc, &fixture.compilation);
    EXPECT_EQ(type->ReflectionName(), "System.Int32");
}

TEST(ILTypeExtensionsTest, InferTypeLdLocaWrapsByReferenceType)
{
    BuilderFixture fixture;
    // ldloca loads the ADDRESS of the variable, so the inferred type is the
    // variable's type wrapped in a ByReferenceType (int& over the int local).
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::LdLoca ldLoca(local);
    auto type = IL::InferType(ldLoca, &fixture.compilation);
    ASSERT_EQ(type->Kind(), TS::TypeKind::ByReference);
    auto* byRef = static_cast<TS::ByReferenceType*>(type.get());
    EXPECT_EQ(byRef->Element()->ReflectionName(), "System.Int32");
}

TEST(ILTypeExtensionsTest, InferTypeDefaultValueReturnsItsType)
{
    BuilderFixture fixture;
    auto decimalType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Decimal).shared_from_this());
    IL::DefaultValue defaultValue(decimalType);
    TS::ITypePtr matched;
    ASSERT_TRUE(IL::MatchDefaultValue(&defaultValue, matched));
    EXPECT_TRUE(matched->Equals(*decimalType));
    auto type = IL::InferType(defaultValue, &fixture.compilation);
    EXPECT_TRUE(type->Equals(*decimalType));
    // The bare match rejects other instructions.
    IL::LdcI4 ldc(1);
    EXPECT_FALSE(IL::MatchDefaultValue(&ldc, matched));
}

TEST(ILTypeExtensionsTest, InferTypeCallUsesTheResolvedReturnType)
{
    BuilderFixture fixture;
    IL::Call call("Test::Method");
    call.ReturnIType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    auto type = IL::InferType(call, &fixture.compilation);
    EXPECT_EQ(type->ReflectionName(), "System.Int64");
    // The newobj shape answers the DECLARING type (the constructed object).
    IL::Call newObj("System.Decimal::.ctor");
    newObj.IsNewObj = true;
    newObj.DeclaringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Decimal).shared_from_this());
    type = IL::InferType(newObj, &fixture.compilation);
    EXPECT_EQ(type->ReflectionName(), "System.Decimal");
}

TEST(ILTypeExtensionsTest, InferTypeNewArrBuildsArrayType)
{
    BuilderFixture fixture;
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(1));
    indices.push_back(std::make_unique<IL::LdcI4>(2));
    IL::NewArr newArr(stringType, std::move(indices));
    auto type = IL::InferType(newArr, &fixture.compilation);
    auto* arrayType = dynamic_cast<TS::ArrayType*>(type.get());
    ASSERT_TRUE(arrayType != nullptr);
    EXPECT_EQ(arrayType->Rank(), 2);
    EXPECT_EQ(arrayType->Element()->ReflectionName(), "System.String");
}

TEST(ILTypeExtensionsTest, InferTypeCompByLiftingKind)
{
    BuilderFixture fixture;
    IL::Comp comp(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(2));
    auto type = IL::InferType(comp, &fixture.compilation);
    EXPECT_EQ(type->ReflectionName(), "System.Boolean");
    IL::Comp lifted(std::make_unique<IL::LdNull>(), std::make_unique<IL::LdNull>(),
                    IL::ComparisonKind::Equality, IL::ComparisonLiftingKind::ThreeValuedLogic,
                    IL::StackType::O);
    type = IL::InferType(lifted, &fixture.compilation);
    EXPECT_EQ(TS::GetUnderlyingType(*type).ReflectionName(), "System.Boolean");
}

TEST(ILTypeExtensionsTest, InferTypeBitAndOfEqualPrimitivesFoldsToTheType)
{
    BuilderFixture fixture;
    auto intLocal = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::BinaryNumericInstruction sameTypes(
        std::make_unique<IL::LdLoc>(intLocal), std::make_unique<IL::LdLoc>(intLocal),
        IL::BinaryNumericOperator::BitAnd);
    auto type = IL::InferType(sameTypes, &fixture.compilation);
    EXPECT_EQ(type->ReflectionName(), "System.Int32");
    auto int64Local = MakeLocal(fixture, TS::KnownTypeCode::Int64, "big");
    IL::BinaryNumericInstruction mixedTypes(
        std::make_unique<IL::LdLoc>(intLocal), std::make_unique<IL::LdLoc>(int64Local),
        IL::BinaryNumericOperator::BitAnd);
    type = IL::InferType(mixedTypes, &fixture.compilation);
    EXPECT_EQ(type->Kind(), TS::TypeKind::Unknown);
    // Non-bitwise operators have no type inference rule.
    IL::BinaryNumericInstruction addInst(std::make_unique<IL::LdLoc>(intLocal),
                                         std::make_unique<IL::LdLoc>(intLocal),
                                         IL::BinaryNumericOperator::Add);
    type = IL::InferType(addInst, &fixture.compilation);
    EXPECT_EQ(type->Kind(), TS::TypeKind::Unknown);
}

TEST(ILTypeExtensionsTest, InferTypeUnsupportedFallsToUnknownType)
{
    BuilderFixture fixture;
    IL::Nop nop;
    auto type = IL::InferType(nop, &fixture.compilation);
    EXPECT_EQ(type->Kind(), TS::TypeKind::Unknown);
    IL::LdStr ldStr("text");
    type = IL::InferType(ldStr, &fixture.compilation);
    EXPECT_EQ(type->Kind(), TS::TypeKind::Unknown);
}

TEST(ILTypeExtensionsTest, ILVariableStackTypeFollowsTheDeclaredType)
{
    BuilderFixture fixture;
    auto intLocal = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    EXPECT_EQ(intLocal->StackType(), IL::StackType::I4);
    auto strLocal = MakeLocal(fixture, TS::KnownTypeCode::String, "str");
    EXPECT_EQ(strLocal->StackType(), IL::StackType::O);
    auto& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto refLocal = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local,
        std::make_shared<TS::ByReferenceType>(
            std::const_pointer_cast<TS::IType>(intType.shared_from_this())));
    EXPECT_EQ(refLocal->StackType(), IL::StackType::Ref);
    // A variable with no type reads Unknown (the default-constructed shape).
    IL::ILVariable empty;
    EXPECT_EQ(empty.StackType(), IL::StackType::Unknown);
    // The value is derived on read, so re-assigning the type updates it (the
    // port's Type is a plain field the reader re-assigns after construction).
    empty.Type = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    EXPECT_EQ(empty.StackType(), IL::StackType::I8);
}

TEST(ILTypeExtensionsTest, IsCompatibleTypeForMemoryAccessMatrix)
{
    BuilderFixture fixture;
    auto& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto& uintType = fixture.compilation.FindType(TS::KnownTypeCode::UInt32);
    auto& longType = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    auto& objectType = fixture.compilation.FindType(TS::KnownTypeCode::Object);
    auto& stringType = fixture.compilation.FindType(TS::KnownTypeCode::String);
    // Equal types.
    EXPECT_TRUE(TS::IsCompatibleTypeForMemoryAccess(
        const_cast<TS::IType&>(intType), const_cast<TS::IType&>(intType)));
    // Same integer stack type and size (int32 vs uint32).
    EXPECT_TRUE(TS::IsCompatibleTypeForMemoryAccess(
        const_cast<TS::IType&>(intType), const_cast<TS::IType&>(uintType)));
    // Different integer sizes.
    EXPECT_FALSE(TS::IsCompatibleTypeForMemoryAccess(
        const_cast<TS::IType&>(intType), const_cast<TS::IType&>(longType)));
    // Both reference types.
    EXPECT_TRUE(TS::IsCompatibleTypeForMemoryAccess(
        const_cast<TS::IType&>(objectType), const_cast<TS::IType&>(stringType)));
    // Unknown types are compatible with everything.
    auto unknown = TS::UnknownType();
    EXPECT_TRUE(
        TS::IsCompatibleTypeForMemoryAccess(const_cast<TS::IType&>(intType), *unknown));
}

// ---------------------------------------------------------------------------
// The VisitNewArr array-creation family (ExpressionBuilder.cs lines 502-516 +
// the TranslateArrayIndex/ConvertArrayIndex helpers at 3248-3277). The renders are
// pinned against the real ilspycmd 11.0 decompiler's --csharp output over a
// csc-compiled fixture (new int[5], new int[n], new long[n], new int[(uint)c],
// new string[n], new int[n][], new int[5, 6]).

TEST(ExpressionBuilderNewArrTest, ConstantSizeRendersNewInt5)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(5));
    IL::NewArr newArr(intType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    auto* type = dynamic_cast<Syntax::PrimitiveType*>(create->Type());
    ASSERT_TRUE(type != nullptr);
    EXPECT_EQ(type->Keyword(), "int");
    ASSERT_EQ(create->Arguments().Count(), 1);
    auto* size = dynamic_cast<Syntax::PrimitiveExpression*>(create->Arguments().At(0));
    ASSERT_TRUE(size != nullptr);
    const std::int32_t* value = std::get_if<std::int32_t>(&size->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 5);
    EXPECT_EQ(create->AdditionalArraySpecifiers().Count(), 0);
    EXPECT_EQ(create->ToString(), "new int[5]");
    // The resolve result is the ArrayCreateResolveResult over the reconstructed
    // one-dimensional array type, with the present-but-empty initializer list
    // (the C# `Empty<ResolveResult>.Array` non-null-empty state).
    const auto* rr = dynamic_cast<const Sem::ArrayCreateResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    auto* arrayType = dynamic_cast<const TS::ArrayType*>(&rr->Type());
    ASSERT_TRUE(arrayType != nullptr);
    EXPECT_TRUE(arrayType->IsSzArray());
    EXPECT_EQ(arrayType->Element()->ReflectionName(), "System.Int32");
    ASSERT_EQ(rr->SizeArguments().size(), std::size_t(1));
    ASSERT_TRUE(rr->InitializerElements().has_value());
    EXPECT_TRUE(rr->InitializerElements()->empty());
}

TEST(ExpressionBuilderNewArrTest, VariableIndexRendersNewIntN)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(local));
    IL::NewArr newArr(intType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    ASSERT_EQ(create->Arguments().Count(), 1);
    auto* size = dynamic_cast<Syntax::IdentifierExpression*>(create->Arguments().At(0));
    ASSERT_TRUE(size != nullptr);
    EXPECT_EQ(size->Identifier(), "n");
    EXPECT_EQ(create->ToString(), "new int[n]");
    const auto* rr = dynamic_cast<const Sem::ArrayCreateResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    ASSERT_EQ(rr->SizeArguments().size(), std::size_t(1));
    ASSERT_TRUE(rr->SizeArguments()[0] != nullptr);
    EXPECT_EQ(rr->SizeArguments()[0]->Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderNewArrTest, LongIndexLongElementRendersNewLong)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int64, "n");
    auto longType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int64).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(local));
    IL::NewArr newArr(longType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    auto* type = dynamic_cast<Syntax::PrimitiveType*>(create->Type());
    ASSERT_TRUE(type != nullptr);
    EXPECT_EQ(type->Keyword(), "long");
    EXPECT_EQ(create->ToString(), "new long[n]");
}

TEST(ExpressionBuilderNewArrTest, CharIndexConvertsToUint)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Char, "c");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(local));
    IL::NewArr newArr(intType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    ASSERT_EQ(create->Arguments().Count(), 1);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(create->Arguments().At(0));
    ASSERT_TRUE(cast != nullptr);
    auto* castType = dynamic_cast<Syntax::PrimitiveType*>(cast->Type());
    ASSERT_TRUE(castType != nullptr);
    EXPECT_EQ(castType->Keyword(), "uint");
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression()) != nullptr);
    EXPECT_EQ(create->ToString(), "new int[(uint)c]");
    // The size argument's own type is the converted uint32.
    const auto* rr = dynamic_cast<const Sem::ArrayCreateResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    ASSERT_EQ(rr->SizeArguments().size(), std::size_t(1));
    ASSERT_TRUE(rr->SizeArguments()[0] != nullptr);
    EXPECT_EQ(rr->SizeArguments()[0]->Type().ReflectionName(), "System.UInt32");
}

TEST(ExpressionBuilderNewArrTest, UIntIndexRendersNewString)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::UInt32, "n");
    auto stringType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::String).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(local));
    IL::NewArr newArr(stringType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    auto* type = dynamic_cast<Syntax::PrimitiveType*>(create->Type());
    ASSERT_TRUE(type != nullptr);
    EXPECT_EQ(type->Keyword(), "string");
    ASSERT_EQ(create->Arguments().Count(), 1);
    // The uint index is a C# primitive integer type and passes through as-is.
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(create->Arguments().At(0)) != nullptr);
    EXPECT_EQ(create->ToString(), "new string[n]");
}

TEST(ExpressionBuilderNewArrTest, JaggedElementMovesSpecifierToAdditional)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto jaggedType = std::make_shared<TS::ArrayType>(intType);
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdLoc>(local));
    IL::NewArr newArr(jaggedType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    // "new (int[])[n]" becomes "new int[n][]": the ConvertType of the jagged
    // element type is a ComposedType over the "int" base with one array specifier;
    // the specifier moves out of the Type slot into AdditionalArraySpecifiers (the
    // ComposedType itself stays the Type slot and renders as its bare base).
    auto* type = dynamic_cast<Syntax::ComposedType*>(create->Type());
    ASSERT_TRUE(type != nullptr);
    auto* base = dynamic_cast<Syntax::PrimitiveType*>(type->BaseType());
    ASSERT_TRUE(base != nullptr);
    EXPECT_EQ(base->Keyword(), "int");
    EXPECT_EQ(type->ArraySpecifiers().Count(), 0);
    ASSERT_EQ(create->AdditionalArraySpecifiers().Count(), 1);
    auto* specifier = create->AdditionalArraySpecifiers().At(0);
    ASSERT_TRUE(specifier != nullptr);
    EXPECT_EQ(specifier->Dimensions(), 1);
    EXPECT_EQ(create->ToString(), "new int[n][]");
}

TEST(ExpressionBuilderNewArrTest, TwoIndicesBuildRank2)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(5));
    indices.push_back(std::make_unique<IL::LdcI4>(6));
    IL::NewArr newArr(intType, std::move(indices));
    auto expr = builder.Translate(&newArr);
    auto* create = dynamic_cast<Syntax::ArrayCreateExpression*>(expr.Expression());
    ASSERT_TRUE(create != nullptr);
    ASSERT_EQ(create->Arguments().Count(), 2);
    EXPECT_EQ(create->ToString(), "new int[5, 6]");
    // Two indices reconstruct a rank-2 (non-SZ) array type in the resolve result.
    const auto* rr = dynamic_cast<const Sem::ArrayCreateResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    auto* arrayType = dynamic_cast<const TS::ArrayType*>(&rr->Type());
    ASSERT_TRUE(arrayType != nullptr);
    EXPECT_FALSE(arrayType->IsSzArray());
    EXPECT_EQ(arrayType->Rank(), 2);
    ASSERT_EQ(rr->SizeArguments().size(), std::size_t(2));
}

// The direct ConvertArrayIndex decision-tree arms (the C# private helper at
// ExpressionBuilder.cs line 3253; the port exposes every member).

TEST(ExpressionBuilderNewArrTest, ConvertArrayIndexTruncatesOversizedInput)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int64, "num");
    IL::LdLoc ldloc(local);
    auto input = builder.Translate(&ldloc);
    // A long-typed input against an I4 stack type is oversized: it truncates to
    // the stack type (Int32), rendering the explicit cast.
    auto converted = builder.ConvertArrayIndex(input, IL::StackType::I4, false);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(converted.Expression());
    ASSERT_TRUE(cast != nullptr);
    auto* castType = dynamic_cast<Syntax::PrimitiveType*>(cast->Type());
    ASSERT_TRUE(castType != nullptr);
    EXPECT_EQ(castType->Keyword(), "int");
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression()) != nullptr);
    EXPECT_EQ(converted.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderNewArrTest, ConvertArrayIndexPrefersIntOverI8)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Char, "c");
    IL::LdLoc ldloc(local);
    auto input = builder.Translate(&ldloc);
    // A char input against an I8 stack type prefers casting to int's stack size:
    // without the I4-preference branch the target would be the I8 arithmetic type
    // (UInt64 for the unsigned char sign).
    auto converted = builder.ConvertArrayIndex(input, IL::StackType::I8, false);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(converted.Expression());
    ASSERT_TRUE(cast != nullptr);
    auto* castType = dynamic_cast<Syntax::PrimitiveType*>(cast->Type());
    ASSERT_TRUE(castType != nullptr);
    EXPECT_EQ(castType->Keyword(), "uint");
    EXPECT_EQ(converted.Type().ReflectionName(), "System.UInt32");
}

TEST(ExpressionBuilderNewArrTest, ConvertArrayIndexPassesIntPtrWhenAllowed)
{
    BuilderFixture fixture;
    // Native integers off: the not-allowed path falls to the I8 arithmetic type.
    fixture.settings.SetNativeIntegers(false);
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::IntPtr, "p");
    IL::LdLoc ldloc(local);
    auto input = builder.Translate(&ldloc);
    // With allowIntPtr the IntPtr input passes through unchanged.
    auto kept = builder.ConvertArrayIndex(input, IL::StackType::I, true);
    EXPECT_EQ(kept.Expression(), input.Expression());
    EXPECT_EQ(&kept.Type(), &input.Type());
    // Without it the input converts to the I8 arithmetic type (Int64).
    auto converted = builder.ConvertArrayIndex(input, IL::StackType::I, false);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(converted.Expression());
    ASSERT_TRUE(cast != nullptr);
    auto* castType = dynamic_cast<Syntax::PrimitiveType*>(cast->Type());
    ASSERT_TRUE(castType != nullptr);
    EXPECT_EQ(castType->Keyword(), "long");
    EXPECT_EQ(converted.Type().ReflectionName(), "System.Int64");
}


// ---------------------------------------------------------------------------
// VisitConv (the numeric-conversion arm)

TEST(ExpressionBuilderConvTest, NopConversionPassesThroughWithAnnotation)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I4, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    // The nop conversion adds no cast; the conv annotation rides on the node after
    // the argument's own LdLoc annotation (the WithILInstruction add semantics).
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{2});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdLoc);
    EXPECT_EQ(expr.ILInstructions()[1], &conv);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Struct);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderConvTest, SignExtendKeepsSignedInputWithoutCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I8, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // The input type is already signed and not oversized -> return the argument as-is
    // (the caller handles the sign extension through the post-condition).
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{2});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdLoc);
    EXPECT_EQ(expr.ILInstructions()[1], &conv);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderConvTest, SignExtendNormalizesUnsignedInputToInt64)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::UInt32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I8, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // The unsigned input is normalized to the SIGNED INPUT STACK TYPE (Int32) -- the
    // sign extension to the target type is left to the caller through the
    // post-condition, so the result type is Int32, not the conv target.
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
    // The ConvertTo cast node does not carry the argument's own annotation
    // (WithoutILInstruction in the ConvertTo tail), so only the conv rides on it.
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, ZeroExtendNormalizesSignedInputToUInt64)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::U8, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.UInt32");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, IntToFloatConvertsToDouble)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    // conv.r8 carries the Signed input sign (the ILReader passes Sign.Signed).
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::R8, false,
                  TS::Sign::Signed);
    auto expr = builder.Translate(&conv);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Double");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, CheckedArmNormalizesSignBeforeCheckedCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::UInt32, "num");
    // conv.ovf.i8 over an unsigned input: the checked arm normalizes the input to the
    // conv's Signed sign first, then casts checked to Int64.
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I8, true,
                  TS::Sign::Signed);
    auto expr = builder.Translate(&conv);
    auto* outer = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(outer != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int64");
    auto* inner = dynamic_cast<Syntax::CastExpression*>(outer->Expression());
    ASSERT_TRUE(inner != nullptr);
    auto* innerType = dynamic_cast<Syntax::PrimitiveType*>(inner->Type());
    ASSERT_TRUE(innerType != nullptr);
    EXPECT_EQ(innerType->Keyword(), "int");
    auto* innermost = dynamic_cast<Syntax::IdentifierExpression*>(inner->Expression());
    ASSERT_TRUE(innermost != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, CheckedArmSkipsSignNormalizeWhenSignMatches)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    // conv.ovf.u8 over a signed input with the conv's sign Unsigned: the input is
    // normalized to the Unsigned form first.
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::U8, true,
                  TS::Sign::Unsigned);
    auto expr = builder.Translate(&conv);
    auto* outer = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(outer != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.UInt64");
    // The input was already Int32-sized with the matching Unsigned input sign
    // after the normalization... the pre-cast goes to the Unsigned I4 type.
    auto* inner = dynamic_cast<Syntax::CastExpression*>(outer->Expression());
    ASSERT_TRUE(inner != nullptr);
    auto* innerType = dynamic_cast<Syntax::PrimitiveType*>(inner->Type());
    ASSERT_TRUE(innerType != nullptr);
    EXPECT_EQ(innerType->Keyword(), "uint");
    auto* innermost = dynamic_cast<Syntax::IdentifierExpression*>(inner->Expression());
    ASSERT_TRUE(innermost != nullptr);
}

TEST(ExpressionBuilderConvTest, TruncateToSmallIntegerEmitsCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I1, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // Truncation to a small integer type: the input is larger than the target and the
    // sign differs -> the default arm emits the simple cast.
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    auto* castType = dynamic_cast<Syntax::PrimitiveType*>(cast->Type());
    ASSERT_TRUE(castType != nullptr);
    EXPECT_EQ(castType->Keyword(), "sbyte");
    EXPECT_EQ(expr.Type().ReflectionName(), "System.SByte");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
}

TEST(ExpressionBuilderConvTest, TruncateToSameSizeSameSignPassesThrough)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::SByte, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I1, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // No actual truncation involved, and the result extends the same way -> the
    // argument is returned directly.
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{2});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdLoc);
    EXPECT_EQ(expr.ILInstructions()[1], &conv);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.SByte");
}

TEST(ExpressionBuilderConvTest, NonSmallTruncatePassesThroughToCaller)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int64, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::U4, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // Truncation to U4 (not a small integer type): the whole unchecked truncation is
    // handled by the caller through the post-condition.
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{2});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdLoc);
    EXPECT_EQ(expr.ILInstructions()[1], &conv);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int64");
}

TEST(ExpressionBuilderConvTest, StopGCTrackingFixedAddressCastsToCorrespondingPointer)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoca>(local), IL::PrimitiveType::I8, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // An uncaptured local's address is fixed -> the reference-to-pointer conversion
    // renders the address-of operator over the identifier (the ConvertTo
    // reference-to-pointer arm), and the pointer type needs no further cast.
    auto* addressOf = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(addressOf != nullptr);
    EXPECT_EQ(addressOf->Operator(), Syntax::UnaryOperatorType::AddressOf);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(addressOf->Expression()) != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Pointer);
    const auto* pointerType = dynamic_cast<const TS::PointerType*>(&expr.Type());
    ASSERT_TRUE(pointerType != nullptr);
    EXPECT_EQ(pointerType->Element()->ReflectionName(), "System.Int32");
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, StopGCTrackingUnfixedAddressCallsAsPointerIntrinsic)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto elementType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto byRefType = std::make_shared<TS::ByReferenceType>(elementType);
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Parameter, byRefType, 0);
    variable->Name = "arg";
    IL::Conv conv(std::make_unique<IL::LdLoc>(variable), IL::PrimitiveType::I8, false,
                  TS::Sign::None);
    auto expr = builder.Translate(&conv);
    // A moveable (non-fixed) address emits the Unsafe.AsPointer() intrinsic; the
    // return type is the void* pointer type.
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "AsPointer");
    EXPECT_TRUE(dynamic_cast<Syntax::TypeReferenceExpression*>(memberRef->Target()) != nullptr);
    EXPECT_EQ(invocation->Arguments().Count(), 1);
    EXPECT_TRUE(dynamic_cast<Syntax::DirectionExpression*>(invocation->Arguments().FirstOrNull())
                != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Pointer);
    const auto* pointerType = dynamic_cast<const TS::PointerType*>(&expr.Type());
    ASSERT_TRUE(pointerType != nullptr);
    EXPECT_EQ(pointerType->Element()->ReflectionName(), "System.Void");
    // The invocation is a fresh node: only the conv annotation rides on it (the
    // argument's own annotation stays on the DirectionExpression argument).
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, StopGCTrackingOverIntegerPassthrough)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A stop-tracking over something that was just tracked (an integer value the
    // start-gc-tracking arm put on the stack): the argument passes through.
    IL::Conv conv(std::make_unique<IL::LdcI4>(42), IL::StackType::Ref, TS::Sign::None,
                  IL::PrimitiveType::I8, false, /*isLifted=*/false);
    ASSERT_EQ(conv.Kind, IL::ConversionKind::StopGCTracking);
    auto expr = builder.Translate(&conv);
    auto* primitive = dynamic_cast<Syntax::PrimitiveExpression*>(expr.Expression());
    ASSERT_TRUE(primitive != nullptr);
    const auto* value = std::get_if<std::int32_t>(&primitive->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 42);
    // The C# `return arg` adds NO conv annotation here (the caller handles the
    // stop-tracking passthrough through the post-condition), so only the
    // argument's own LdcI4 annotation is present.
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdcI4);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderConvTest, StopGCTrackingNonIntegerTargetFallsToDefaultCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::String, "s");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I8, false,
                  TS::Sign::None);
    ASSERT_EQ(conv.Kind, IL::ConversionKind::StopGCTracking);
    auto expr = builder.Translate(&conv);
    // The argument's C# type is not a managed reference and not an integer -> the
    // default arm casts to the target type.
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int64");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
    EXPECT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, NIntPreferenceOverIntPtrWithoutHint)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    // conv.ovf.i over an int input: the checked arm casts to nint (NativeIntegers is
    // on by default and the type hint does not equal IntPtr).
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I, true,
                  TS::Sign::Signed);
    auto expr = builder.Translate(&conv);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::NInt);
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
}

TEST(ExpressionBuilderConvTest, IntPtrWithoutNativeIntegers)
{
    BuilderFixture fixture;
    fixture.settings.SetNativeIntegers(false);
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I, true,
                  TS::Sign::Signed);
    auto expr = builder.Translate(&conv);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    // Without the nint setting the target type stays System.IntPtr.
    EXPECT_EQ(expr.Type().ReflectionName(), "System.IntPtr");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
}

TEST(ExpressionBuilderConvTest, IntPtrHintMatchesStayIntPtr)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "num");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I, true,
                  TS::Sign::Signed);
    // With the IntPtr type as the hint the GetType preference does not fire (the
    // hint equals the found type), so the raw System.IntPtr is used.
    const TS::IType& hint = fixture.compilation.FindType(TS::KnownTypeCode::IntPtr);
    auto expr = builder.Visit(&conv, CSharp::TranslationContext{&hint});
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.IntPtr");
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Struct);
}

TEST(ExpressionBuilderConvTest, LiftedCheckedConvWrapsNullableDouble)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A lifted conv.nop.ovf-style conv over a boxed Nullable<T> argument: the target
    // type is wrapped in Nullable<T> by the GetType local function.
    IL::Conv conv(std::make_unique<IL::LdcI4>(42), IL::StackType::I4, TS::Sign::Signed,
                  IL::PrimitiveType::R8, true, /*isLifted=*/true);
    auto expr = builder.Translate(&conv);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Struct);
    EXPECT_TRUE(TS::IsNullable(expr.Type()));
    const auto* nullable = dynamic_cast<const TS::ParameterizedType*>(&expr.Type());
    ASSERT_TRUE(nullable != nullptr);
    EXPECT_EQ(nullable->TypeArguments()[0]->ReflectionName(), "System.Double");
}

TEST(ExpressionBuilderConvTest, InvalidUnknownToObjectPassthrough)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A variable typed with the Unknown null object loads with the Unknown stack type
    // -> the Invalid arm's Unknown->O conversion passes through without the (object)
    // cast (we're likely to cast back to the same unknown type).
    auto unknownVariable = std::make_shared<IL::ILVariable>(
        IL::VariableKind::Local, TS::UnknownType(), 0);
    unknownVariable->Name = "u";
    IL::Conv conv(std::make_unique<IL::LdLoc>(unknownVariable), IL::PrimitiveType::None,
                  false, TS::Sign::None);
    ASSERT_EQ(conv.Kind, IL::ConversionKind::Invalid);
    auto expr = builder.Translate(&conv);
    // The argument's own shape (the identifier over the unknown-typed variable) is
    // returned unchanged with the conv annotation added.
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    EXPECT_EQ(ident->Identifier(), "u");
    EXPECT_EQ(expr.Type().Kind(), TS::TypeKind::Unknown);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{2});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdLoc);
    EXPECT_EQ(expr.ILInstructions()[1], &conv);
}

TEST(ExpressionBuilderConvTest, InvalidWithKnownArgumentFallsToObjectCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::String, "s");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::None, false,
                  TS::Sign::None);
    ASSERT_EQ(conv.Kind, IL::ConversionKind::Invalid);
    auto expr = builder.Translate(&conv);
    // The argument type is not Unknown -> the default arm casts to object.
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Object");
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &conv);
}

TEST(ExpressionBuilderConvTest, DefaultArmUsesHintWhenItMatches)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::String, "s");
    IL::Conv conv(std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::I8, false,
                  TS::Sign::None);
    // With the Int64 type as the hint the default arm picks the hint (the target type
    // matches the hint's primitive form and the hint is not nullable).
    const TS::IType& hint = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    auto expr = builder.Visit(&conv, CSharp::TranslationContext{&hint});
    auto* cast = dynamic_cast<Syntax::CastExpression*>(expr.Expression());
    ASSERT_TRUE(cast != nullptr);
    EXPECT_EQ(&expr.Type(), &hint);
    auto* inner = dynamic_cast<Syntax::IdentifierExpression*>(cast->Expression());
    ASSERT_TRUE(inner != nullptr);
}

TEST(ExpressionBuilderConvTest, ValueMightBeOversizedMatrix)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // A smaller-or-equal input type cannot be oversized.
    const TS::IType& int32Type = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    Sem::ConstantResolveResult smallRr(
        std::const_pointer_cast<TS::IType>(int32Type.shared_from_this()),
        std::any{std::int32_t{1}});
    EXPECT_FALSE(builder.ValueMightBeOversized(smallRr, IL::StackType::I8));
    // A larger input type without any operator information might be oversized.
    const TS::IType& int64Type = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    Sem::ConstantResolveResult largeRr(
        std::const_pointer_cast<TS::IType>(int64Type.shared_from_this()),
        std::any{std::int64_t{1}});
    EXPECT_TRUE(builder.ValueMightBeOversized(largeRr, IL::StackType::I4));
    // A pointer subtraction is known to fit in a native int: with the result type
    // oversized relative to StackType.I (Int64 is 8 bytes over the 6-byte native
    // int), the operator arm is the only path that answers false.
    auto pointerType = std::make_shared<TS::PointerType>(
        std::const_pointer_cast<TS::IType>(int32Type.shared_from_this()));
    Sem::OperatorResolveResult pointerSubtraction(
        std::const_pointer_cast<TS::IType>(int64Type.shared_from_this()),
        TS::ExpressionType::Subtract,
        std::vector<std::shared_ptr<Sem::ResolveResult>>{
            std::make_shared<Sem::ResolveResult>(pointerType),
            std::make_shared<Sem::ResolveResult>(pointerType)});
    EXPECT_FALSE(builder.ValueMightBeOversized(pointerSubtraction, IL::StackType::I));
    // The same oversized value without the pointer-subtraction shape is oversized.
    Sem::OperatorResolveResult nonSubtraction(
        std::const_pointer_cast<TS::IType>(int64Type.shared_from_this()),
        TS::ExpressionType::Add,
        std::vector<std::shared_ptr<Sem::ResolveResult>>{
            std::make_shared<Sem::ResolveResult>(pointerType),
            std::make_shared<Sem::ResolveResult>(pointerType)});
    EXPECT_TRUE(builder.ValueMightBeOversized(nonSubtraction, IL::StackType::I));
    // Two non-pointer operands do not answer false either.
    Sem::OperatorResolveResult nonPointerSubtraction(
        std::const_pointer_cast<TS::IType>(int64Type.shared_from_this()),
        TS::ExpressionType::Subtract,
        std::vector<std::shared_ptr<Sem::ResolveResult>>{
            std::make_shared<Sem::ResolveResult>(
                std::const_pointer_cast<TS::IType>(int32Type.shared_from_this())),
            std::make_shared<Sem::ResolveResult>(
                std::const_pointer_cast<TS::IType>(int32Type.shared_from_this()))});
    EXPECT_TRUE(builder.ValueMightBeOversized(nonPointerSubtraction, IL::StackType::I));
}
// ---------------------------------------------------------------------------
// The PointerArithmeticOffset unit tests (IL/PointerArithmeticOffset.cs -- the
// port landing with the stackalloc arms).

TEST(PointerArithmeticOffsetTest, ComputeSizeOfMatrix)
{
    BuilderFixture fixture;
    EXPECT_EQ(1, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Byte)));
    EXPECT_EQ(1, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::SByte)));
    EXPECT_EQ(1, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Boolean)));
    EXPECT_EQ(2, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Char)));
    EXPECT_EQ(2, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Int16)));
    EXPECT_EQ(2, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::UInt16)));
    EXPECT_EQ(4, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Int32)));
    EXPECT_EQ(4, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::UInt32)));
    EXPECT_EQ(4, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Single)));
    EXPECT_EQ(8, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Int64)));
    EXPECT_EQ(8, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::UInt64)));
    EXPECT_EQ(8, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Double)));
    EXPECT_EQ(16, IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Decimal)));
    // Non-primitive element types answer nullopt (IntPtr is not in the C#
    // ComputeSizeOf table either).
    EXPECT_FALSE(IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::Object)).has_value());
    EXPECT_FALSE(IL::PointerArithmeticOffset::ComputeSizeOf(
        &fixture.compilation.FindType(TS::KnownTypeCode::IntPtr)).has_value());
}

TEST(PointerArithmeticOffsetTest, DetectSizeOneReturnsInput)
{
    BuilderFixture fixture;
    IL::LdcI4 constant(40);
    const TS::IType& byteType = fixture.compilation.FindType(TS::KnownTypeCode::Byte);
    auto outcome = IL::PointerArithmeticOffset::Detect(&constant, &byteType, false);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome.Inst, &constant);
    EXPECT_TRUE(outcome.Owned == nullptr);
}

TEST(PointerArithmeticOffsetTest, DetectConstantFoldDivision)
{
    BuilderFixture fixture;
    IL::LdcI4 constant(40);
    constant.SetILRange(0x10, 0x12);
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto outcome = IL::PointerArithmeticOffset::Detect(&constant, &intType, false);
    ASSERT_TRUE(outcome);
    ASSERT_TRUE(outcome.Owned != nullptr);
    auto* folded = dynamic_cast<IL::LdcI4*>(const_cast<IL::ILInstruction*>(outcome.Inst));
    ASSERT_TRUE(folded != nullptr);
    EXPECT_EQ(folded->Value, 10);
    // The fresh constant carries the input's IL byte range.
    EXPECT_EQ(folded->StartILOffset, 0x10);
    EXPECT_EQ(folded->EndILOffset, 0x12);
}

TEST(PointerArithmeticOffsetTest, DetectConstantNotDivisibleOrZero)
{
    BuilderFixture fixture;
    IL::LdcI4 seven(7);
    IL::LdcI4 zero(0);
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&seven, &intType, false));
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&zero, &intType, false));
}

TEST(PointerArithmeticOffsetTest, DetectConstantOverflowAfterDivide)
{
    BuilderFixture fixture;
    // 10000000000 / 4 = 2500000000 does not fit in int32 -> no match;
    // 8000000000 / 4 = 2000000000 fits -> a fresh LdcI4.
    IL::LdcI8 tooLarge(10000000000LL);
    IL::LdcI8 fits(8000000000LL);
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&tooLarge, &intType, false));
    auto outcome = IL::PointerArithmeticOffset::Detect(&fits, &intType, false);
    ASSERT_TRUE(outcome);
    auto* folded = dynamic_cast<IL::LdcI4*>(const_cast<IL::ILInstruction*>(outcome.Inst));
    ASSERT_TRUE(folded != nullptr);
    EXPECT_EQ(folded->Value, 2000000000);
}

TEST(PointerArithmeticOffsetTest, DetectMulWithConstantSize)
{
    BuilderFixture fixture;
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    IL::BinaryNumericInstruction mul(
        std::make_unique<IL::LdLoc>(local), std::make_unique<IL::LdcI4>(4),
        IL::BinaryNumericOperator::Mul, true, TS::Sign::Unsigned);
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto outcome = IL::PointerArithmeticOffset::Detect(&mul, &intType, true);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome.Inst, mul.Left.get());
    // The overflow-checking flag must match: a non-checking mul never matches a
    // checking query and vice versa.
    IL::BinaryNumericInstruction uncheckedMul(
        std::make_unique<IL::LdLoc>(local), std::make_unique<IL::LdcI4>(4),
        IL::BinaryNumericOperator::Mul, false, TS::Sign::Unsigned);
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&uncheckedMul, &intType, true));
    EXPECT_TRUE(IL::PointerArithmeticOffset::Detect(&uncheckedMul, &intType, false));
    // A lifted mul never matches.
    IL::BinaryNumericInstruction liftedMul(
        std::make_unique<IL::LdLoc>(local), std::make_unique<IL::LdcI4>(4),
        IL::BinaryNumericOperator::Mul, true, TS::Sign::Unsigned);
    liftedMul.IsLifted = true;
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&liftedMul, &intType, true));
}

TEST(PointerArithmeticOffsetTest, DetectMulWithSizeOfRightOperand)
{
    BuilderFixture fixture;
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    IL::SizeOf sizeOf(
        std::const_pointer_cast<TS::IType>(intType.shared_from_this()), "int");
    IL::BinaryNumericInstruction mul(
        std::make_unique<IL::LdLoc>(local), std::make_unique<IL::SizeOf>(sizeOf),
        IL::BinaryNumericOperator::Mul, false, TS::Sign::None);
    auto outcome = IL::PointerArithmeticOffset::Detect(&mul, &intType, false);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(outcome.Inst, mul.Left.get());
    // A sizeof of a DIFFERENT element type does not match.
    const TS::IType& byteType = fixture.compilation.FindType(TS::KnownTypeCode::Byte);
    IL::SizeOf byteSizeOf(
        std::const_pointer_cast<TS::IType>(byteType.shared_from_this()), "byte");
    IL::BinaryNumericInstruction byteMul(
        std::make_unique<IL::LdLoc>(local), std::make_unique<IL::SizeOf>(byteSizeOf),
        IL::BinaryNumericOperator::Mul, false, TS::Sign::None);
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&byteMul, &intType, false));
}

TEST(PointerArithmeticOffsetTest, DetectUnwrapZeroExtension)
{
    BuilderFixture fixture;
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    // conv.u over the int32 local (target U from I4 with a None input sign is a
    // zero extension) around the count.
    IL::BinaryNumericInstruction mul(
        std::make_unique<IL::Conv>(
            std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::U, false,
            TS::Sign::None),
        std::make_unique<IL::LdcI4>(4), IL::BinaryNumericOperator::Mul, true,
        TS::Sign::Unsigned);
    auto withoutUnwrap = IL::PointerArithmeticOffset::Detect(&mul, &intType, true, false);
    ASSERT_TRUE(withoutUnwrap);
    EXPECT_EQ(withoutUnwrap.Inst, mul.Left.get());
    auto withUnwrap = IL::PointerArithmeticOffset::Detect(&mul, &intType, true, true);
    ASSERT_TRUE(withUnwrap);
    auto* mulConv = dynamic_cast<IL::Conv*>(mul.Left.get());
    ASSERT_TRUE(mulConv != nullptr);
    EXPECT_EQ(withUnwrap.Inst, mulConv->Argument.get());
}

TEST(PointerArithmeticOffsetTest, DetectBareSizeOfAnswersOneElement)
{
    BuilderFixture fixture;
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    IL::SizeOf sizeOf(
        std::const_pointer_cast<TS::IType>(intType.shared_from_this()), "int");
    auto outcome = IL::PointerArithmeticOffset::Detect(&sizeOf, &intType, false);
    ASSERT_TRUE(outcome);
    auto* folded = dynamic_cast<IL::LdcI4*>(const_cast<IL::ILInstruction*>(outcome.Inst));
    ASSERT_TRUE(folded != nullptr);
    EXPECT_EQ(folded->Value, 1);
    // A different element type never matches (an int64 element never equals an
    // int sizeof; a byte element would take the size-1 arm first and return
    // the input itself).
    const TS::IType& int64Type = fixture.compilation.FindType(TS::KnownTypeCode::Int64);
    EXPECT_FALSE(IL::PointerArithmeticOffset::Detect(&sizeOf, &int64Type, false));
}

TEST(PointerArithmeticOffsetTest, DetectUnwrapsConvI8ToI)
{
    BuilderFixture fixture;
    const TS::IType& intType = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    // The reader's wide-to-native conv (InputType I8, ResultType I) around a
    // constant byte count unwraps before the constant folding.
    IL::Conv conv(std::make_unique<IL::LdcI8>(40), IL::PrimitiveType::I, false,
                  TS::Sign::None);
    ASSERT_EQ(conv.ResultType(), IL::StackType::I);
    ASSERT_EQ(conv.InputType, IL::StackType::I8);
    auto outcome = IL::PointerArithmeticOffset::Detect(&conv, &intType, false);
    ASSERT_TRUE(outcome);
    auto* folded = dynamic_cast<IL::LdcI4*>(const_cast<IL::ILInstruction*>(outcome.Inst));
    ASSERT_TRUE(folded != nullptr);
    EXPECT_EQ(folded->Value, 10);
}

TEST(PointerArithmeticOffsetTest, IsFixedVariableMatrix)
{
    BuilderFixture fixture;
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    // ldloca of an uncaptured local is fixed (the port reader never sets a
    // capture scope).
    IL::LdLoca locala(local);
    EXPECT_TRUE(IL::PointerArithmeticOffset::IsFixedVariable(&locala));
    // A field address is fixed if its target is fixed.
    IL::LdFlda fielda(std::make_unique<IL::LdLoca>(local), "T::f");
    EXPECT_TRUE(IL::PointerArithmeticOffset::IsFixedVariable(&fielda));
    // Everything else answers on the stack type: an ldloc is not a native int.
    IL::LdLoc localLoad(local);
    EXPECT_FALSE(IL::PointerArithmeticOffset::IsFixedVariable(&localLoad));
}

// ---------------------------------------------------------------------------
// The stackalloc arms (ExpressionBuilder.cs lines 517-579 + the
// GetPointerArithmeticOffset/EnsureIntegerType helpers at 1506-1531). The
// renders are pinned against the real ilspycmd 11.0 --csharp output over a
// csc-compiled stackalloc fixture: stackalloc int[n], stackalloc byte[(int)n],
// stackalloc int[(int)(uint)c] and the Span<int> arm stackalloc int[n].

TEST(ExpressionBuilderStackAllocTest, SizeOfArmRendersStackAllocIntN)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto intTypePtr = std::const_pointer_cast<TS::IType>(intType);
    IL::LocAlloc locAlloc(std::make_unique<IL::BinaryNumericInstruction>(
        std::make_unique<IL::LdLoc>(local),
        std::make_unique<IL::SizeOf>(intTypePtr, "int"),
        IL::BinaryNumericOperator::Mul, false, TS::Sign::None));
    auto expr = builder.Translate(&locAlloc);
    auto* stackAlloc = dynamic_cast<Syntax::StackAllocExpression*>(expr.Expression());
    ASSERT_TRUE(stackAlloc != nullptr);
    EXPECT_EQ(stackAlloc->ToString(), "stackalloc int[n]");
    // The resolve result is a plain ResolveResult over the element pointer type.
    const auto* rr = dynamic_cast<const Sem::ResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Type().Kind(), TS::TypeKind::Pointer);
    EXPECT_EQ(rr->Type().ReflectionName(), "System.Int32*");
}

TEST(ExpressionBuilderStackAllocTest, ByteHintRendersIntCastOfByteCount)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Byte, "n");
    auto byteType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Byte).shared_from_this());
    // The compiler shape: ldarg.0; conv.u; localloc -- the conv.u is a zero
    // extension that Detect's size-1 arm leaves in place.
    IL::LocAlloc locAlloc(std::make_unique<IL::Conv>(
        std::make_unique<IL::LdLoc>(local), IL::PrimitiveType::U, false,
        TS::Sign::None));
    TS::PointerType bytePointer(
        std::const_pointer_cast<TS::IType>(byteType));
    auto expr = builder.Translate(&locAlloc, &bytePointer);
    auto* stackAlloc = dynamic_cast<Syntax::StackAllocExpression*>(expr.Expression());
    ASSERT_TRUE(stackAlloc != nullptr);
    EXPECT_EQ(stackAlloc->ToString(), "stackalloc byte[(int)n]");
}

TEST(ExpressionBuilderStackAllocTest, IntHintUnwrapsZeroExtension)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    // The compiler shape for stackalloc int[n]: ldarg.0; conv.u; ldc.i4.4;
    // mul.ovf.un; localloc -- the conv.u unwraps with unwrapZeroExtension.
    IL::LocAlloc locAlloc(std::make_unique<IL::BinaryNumericInstruction>(
        std::make_unique<IL::Conv>(std::make_unique<IL::LdLoc>(local),
                                   IL::PrimitiveType::U, false, TS::Sign::None),
        std::make_unique<IL::LdcI4>(4), IL::BinaryNumericOperator::Mul, true,
        TS::Sign::Unsigned));
    TS::PointerType intPointer(intType);
    auto expr = builder.Translate(&locAlloc, &intPointer);
    auto* stackAlloc = dynamic_cast<Syntax::StackAllocExpression*>(expr.Expression());
    ASSERT_TRUE(stackAlloc != nullptr);
    EXPECT_EQ(stackAlloc->ToString(), "stackalloc int[n]");
}

TEST(ExpressionBuilderStackAllocTest, NoHintFallsBackToByteElement)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    IL::LocAlloc locAlloc(std::make_unique<IL::LdLoc>(local));
    auto expr = builder.Translate(&locAlloc);
    auto* stackAlloc = dynamic_cast<Syntax::StackAllocExpression*>(expr.Expression());
    ASSERT_TRUE(stackAlloc != nullptr);
    EXPECT_EQ(stackAlloc->ToString(), "stackalloc byte[n]");
}

TEST(ExpressionBuilderStackAllocTest, CharCountRendersUintIntermediate)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Char, "c");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    // The compiler shape for stackalloc int[c]: ldarg.0; conv.u; ldc.i4.4;
    // mul.ovf.un; localloc. The char survives the zero-extension unwrap and the
    // EnsureIntegerType conversion renders (uint)c; the int32 count conversion
    // then wraps the (int).
    IL::LocAlloc locAlloc(std::make_unique<IL::BinaryNumericInstruction>(
        std::make_unique<IL::Conv>(std::make_unique<IL::LdLoc>(local),
                                   IL::PrimitiveType::U, false, TS::Sign::None),
        std::make_unique<IL::LdcI4>(4), IL::BinaryNumericOperator::Mul, true,
        TS::Sign::Unsigned));
    TS::PointerType intPointer(intType);
    auto expr = builder.Translate(&locAlloc, &intPointer);
    auto* stackAlloc = dynamic_cast<Syntax::StackAllocExpression*>(expr.Expression());
    ASSERT_TRUE(stackAlloc != nullptr);
    EXPECT_EQ(stackAlloc->ToString(), "stackalloc int[(int)(uint)c]");
}

TEST(ExpressionBuilderStackAllocTest, SpanArmRendersStackAllocIntN)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto spanType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::SpanOfT).shared_from_this());
    auto spanOfInt = std::make_shared<TS::ParameterizedType>(spanType,
                                                             std::vector<TS::ITypePtr>{intType});
    IL::LocAllocSpan locAllocSpan(std::make_unique<IL::LdLoc>(local), spanOfInt);
    auto expr = builder.Translate(&locAllocSpan);
    auto* stackAlloc = dynamic_cast<Syntax::StackAllocExpression*>(expr.Expression());
    ASSERT_TRUE(stackAlloc != nullptr);
    EXPECT_EQ(stackAlloc->ToString(), "stackalloc int[n]");
    // The resolve result type is the span type itself.
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Span`1[[System.Int32]]");
}

TEST(ExpressionBuilderStackAllocTest, LocAllocCloneCopiesTypeAndArgument)
{
    BuilderFixture fixture;
    auto local = MakeLocal(fixture, TS::KnownTypeCode::Int32, "n");
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    IL::LocAllocSpan span(std::make_unique<IL::LdLoc>(local),
                          std::make_shared<TS::ParameterizedType>(
                              std::const_pointer_cast<TS::IType>(
                                  fixture.compilation.FindType(TS::KnownTypeCode::SpanOfT)
                                      .shared_from_this()),
                              std::vector<TS::ITypePtr>{intType}));
    auto spanClone = span.Clone();
    auto* spanCloneTyped = dynamic_cast<IL::LocAllocSpan*>(spanClone.get());
    ASSERT_TRUE(spanCloneTyped != nullptr);
    EXPECT_EQ(spanCloneTyped->Type, span.Type);
    ASSERT_TRUE(spanCloneTyped->Argument != nullptr);
    EXPECT_EQ(spanCloneTyped->Argument->Op, IL::OpCode::LdLoc);
    IL::LocAlloc alloc(std::make_unique<IL::LdLoc>(local));
    auto allocClone = alloc.Clone();
    auto* allocCloneTyped = dynamic_cast<IL::LocAlloc*>(allocClone.get());
    ASSERT_TRUE(allocCloneTyped != nullptr);
    EXPECT_EQ(allocCloneTyped->Op, IL::OpCode::LocAlloc);
}

// The VisitComp comparison family (ExpressionBuilder.cs lines 871-1187 plus the
// shared AdjustConstantExpressionToType/CreateBuiltinBinaryOperator helpers at
// 3861-1120). The renders are pinned against the real ilspycmd 11.0 --csharp
// output over a csc-compiled comparison fixture (C:/temp-probe/CmpTest):
// a < b, x == false -> !x, s == null, p == null, x == y over int?, the
// three-valued-logic lifted-not arm and the Unsafe.AreSame ref intrinsics.

TEST(ExpressionBuilderCompTest, RelationalRendersBinaryOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::Comp comp(std::make_unique<IL::LdLoc>(a), std::make_unique<IL::LdLoc>(b),
                  IL::ComparisonKind::LessThan, TS::Sign::Signed);
    auto expr = builder.Translate(&comp);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::LessThan);
    EXPECT_EQ(bin->ToString(), "a < b");
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &comp);
}

TEST(ExpressionBuilderCompTest, EqualityRendersBinaryOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::Comp comp(std::make_unique<IL::LdLoc>(a), std::make_unique<IL::LdLoc>(b),
                  IL::ComparisonKind::Equality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::Equality);
    EXPECT_EQ(bin->ToString(), "a == b");
}

TEST(ExpressionBuilderCompTest, BoolEqualsZeroRendersNot)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto x = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "x");
    IL::Comp comp(std::make_unique<IL::LdLoc>(x), std::make_unique<IL::LdcI4>(0),
                  IL::ComparisonKind::Equality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    // 'x == false' renders '!x': the redundant-bool-comparison arm returns the
    // translated bool operand with negateOutput=true.
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
    EXPECT_EQ(unary->ToString(), "!x");
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &comp);
}

TEST(ExpressionBuilderCompTest, BoolEqualsOneRendersIdentity)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto x = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "x");
    IL::Comp comp(std::make_unique<IL::LdLoc>(x), std::make_unique<IL::LdcI4>(1),
                  IL::ComparisonKind::Equality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    // 'x == true' renders 'x': the identity arm returns the operand unmodified.
    auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(ident != nullptr);
    EXPECT_EQ(ident->Identifier(), "x");
    // The identity arm returns the translated operand directly (the C# `return
    // left` before the WithILInstruction(inst) wrap), so the IL annotation is
    // the LdLoc's.
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0]->Op, IL::OpCode::LdLoc);
}

TEST(ExpressionBuilderCompTest, BoolNotEqualsOneRendersNot)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto x = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "x");
    IL::Comp comp(std::make_unique<IL::LdLoc>(x), std::make_unique<IL::LdcI4>(1),
                  IL::ComparisonKind::Inequality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    // 'x != true' renders '!x' (the negate-output arm).
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->ToString(), "!x");
}

TEST(ExpressionBuilderCompTest, BoolConstantLeftOperands)
{
    BuilderFixture fixture;
    {
        auto builder = fixture.MakeBuilder();
        auto b = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "b");
        IL::Comp comp(std::make_unique<IL::LdcI4>(0), std::make_unique<IL::LdLoc>(b),
                      IL::ComparisonKind::Equality, TS::Sign::None);
        auto expr = builder.Translate(&comp);
        // '0 == b' renders '!b' (the right-operand bool arm).
        auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
        ASSERT_TRUE(unary != nullptr);
        EXPECT_EQ(unary->ToString(), "!b");
    }
    {
        auto builder = fixture.MakeBuilder();
        auto b = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "b");
        IL::Comp comp(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdLoc>(b),
                      IL::ComparisonKind::Equality, TS::Sign::None);
        auto expr = builder.Translate(&comp);
        // '1 == b' renders 'b'.
        auto* ident = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
        ASSERT_TRUE(ident != nullptr);
        EXPECT_EQ(ident->Identifier(), "b");
    }
}

TEST(ExpressionBuilderCompTest, IntZeroConstantsRenderPlain)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    {
        IL::Comp comp(std::make_unique<IL::LdLoc>(a), std::make_unique<IL::LdcI4>(0),
                      IL::ComparisonKind::Equality, TS::Sign::None);
        auto expr = builder.Translate(&comp);
        auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
        ASSERT_TRUE(bin != nullptr);
        EXPECT_EQ(bin->ToString(), "a == 0");
    }
    {
        IL::Comp comp(std::make_unique<IL::LdLoc>(a), std::make_unique<IL::LdcI4>(0),
                      IL::ComparisonKind::Inequality, TS::Sign::None);
        auto expr = builder.Translate(&comp);
        auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
        ASSERT_TRUE(bin != nullptr);
        EXPECT_EQ(bin->ToString(), "a != 0");
    }
}

TEST(ExpressionBuilderCompTest, StringNullRendersReferenceComparison)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto s = MakeLocal(fixture, TS::KnownTypeCode::String, "s");
    IL::Comp comp(std::make_unique<IL::LdLoc>(s), std::make_unique<IL::LdNull>(),
                  IL::ComparisonKind::Equality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    // When comparing a string with null, the C# compiler generates a reference
    // comparison -- the special case renders the builtin operator directly.
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->ToString(), "s == null");
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &comp);
}

TEST(ExpressionBuilderCompTest, PointerNullComparisonRendersNullLiteral)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto ptrType = std::make_shared<TS::PointerType>(intType);
    auto p = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    p->Name = "p";
    // The compiler shape for 'p == null': ldarg.0; ldc.i4.0; conv.u; ceq --
    // the conv.u is the zero extension MatchLdcI unwraps.
    IL::Comp comp(
        std::make_unique<IL::LdLoc>(p),
        std::make_unique<IL::Conv>(std::make_unique<IL::LdcI4>(0), IL::PrimitiveType::U,
                                   false, TS::Sign::None),
        IL::ComparisonKind::Equality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->ToString(), "p == null");
}

TEST(ExpressionBuilderCompTest, PointerIdentityRendersPlain)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto ptrType = std::make_shared<TS::PointerType>(intType);
    auto p = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    p->Name = "p";
    auto q = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    q->Name = "q";
    IL::Comp comp(std::make_unique<IL::LdLoc>(p), std::make_unique<IL::LdLoc>(q),
                  IL::ComparisonKind::Equality, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->ToString(), "p == q");
}

TEST(ExpressionBuilderCompTest, LiftedCSharpEqualityOverNullableLocals)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto nullableInt = TS::Create(fixture.compilation, *intType);
    auto x = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt);
    x->Name = "x";
    auto y = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt);
    y->Name = "y";
    // The nullable-lifting shape: comp(Equality, CSharp lift, InputType I4,
    // Sign None) over the nullable-typed locals; the resolver resolves the lifted
    // operator and the render stays 'x == y'.
    IL::Comp comp(std::make_unique<IL::LdLoc>(x), std::make_unique<IL::LdLoc>(y),
                  IL::ComparisonKind::Equality, IL::ComparisonLiftingKind::CSharp,
                  IL::StackType::I4, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->ToString(), "x == y");
    auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_TRUE(opRR->IsLiftedOperator());
}

TEST(ExpressionBuilderCompTest, LiftedCSharpRelationalOverNullableLocals)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto nullableInt = TS::Create(fixture.compilation, *intType);
    auto x = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt);
    x->Name = "x";
    auto y = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableInt);
    y->Name = "y";
    IL::Comp comp(std::make_unique<IL::LdLoc>(x), std::make_unique<IL::LdLoc>(y),
                  IL::ComparisonKind::LessThan, IL::ComparisonLiftingKind::CSharp,
                  IL::StackType::I4, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->ToString(), "x < y");
    // The resolve result is a Boolean-typed operator over the two operand
    // resolve results (the render is pinned against the real ilspycmd N2
    // render; the resolver's lifted-RELATIONAL shape is a resolver-side detail
    // -- the lifted-equality sibling test pins the IsLiftedOperator flag).
    auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->Type().ReflectionName(), "System.Boolean");
}

TEST(ExpressionBuilderCompTest, ThreeValuedLogicEqualityZeroRendersNot)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto boolType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Boolean).shared_from_this());
    auto nullableBool = TS::Create(fixture.compilation, *boolType);
    auto b = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableBool);
    b->Name = "b";
    // The lifted logic.not arm: comp(Equality, ThreeValuedLogic lift, I4, None,
    // ldloc b, ldc.i4 0) renders '!b' with the lifted Not operator resolve result.
    IL::Comp comp(std::make_unique<IL::LdLoc>(b), std::make_unique<IL::LdcI4>(0),
                  IL::ComparisonKind::Equality, IL::ComparisonLiftingKind::ThreeValuedLogic,
                  IL::StackType::I4, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->ToString(), "!b");
    auto* opRR = dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opRR != nullptr);
    EXPECT_EQ(opRR->OperatorType(), TS::ExpressionType::Not);
    EXPECT_EQ(opRR->Type().ReflectionName(),
              "System.Nullable`1[[System.Boolean]]");
}

TEST(ExpressionBuilderCompTest, ThreeValuedLogicRelationalRendersError)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto boolType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Boolean).shared_from_this());
    auto nullableBool = TS::Create(fixture.compilation, *boolType);
    auto b = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, nullableBool);
    b->Name = "b";
    // Any non-equality three-valued-logic comparison is not expressible in C#:
    // the error expression with the exact message.
    IL::Comp comp(std::make_unique<IL::LdLoc>(b), std::make_unique<IL::LdcI4>(0),
                  IL::ComparisonKind::LessThan, IL::ComparisonLiftingKind::ThreeValuedLogic,
                  IL::StackType::I4, TS::Sign::None);
    auto expr = builder.Translate(&comp);
    auto* error = dynamic_cast<Syntax::ErrorExpression*>(expr.Expression());
    ASSERT_TRUE(error != nullptr);
    ASSERT_EQ(error->TrailingTrivia().size(), std::size_t{1});
    auto* comment = dynamic_cast<Syntax::Comment*>(error->TrailingTrivia()[0]);
    ASSERT_TRUE(comment != nullptr);
    EXPECT_EQ(comment->Content(),
              "Nullable comparisons with three-valued-logic not supported in C#");
}

// The Ref-arm drives assert STRUCTURE (the existing CallUnsafeIntrinsic convention --
// the AsPointer test's member-name/target/arguments shape) because the port's
// InvocationExpression::ToString renders with the " (" spacing while the real output
// visitor writes none.
namespace {
// Drives a Ref-type comp over two int32 locals and asserts the intrinsic shape.
void DriveRefComp(IL::ComparisonKind kind, const char* expectedMethod, bool negated)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::Comp comp(std::make_unique<IL::LdLoca>(a), std::make_unique<IL::LdLoca>(b),
                  kind, TS::Sign::None);
    comp.InputType = IL::StackType::Ref;
    auto expr = builder.Translate(&comp);
    const Syntax::Expression* node = expr.Expression();
    if (negated)
    {
        // The negate arm wraps WITHOUT carrying the IL instruction (the C#
        // `.WithoutILInstruction()`), so the outer node has no IL annotations.
        EXPECT_EQ(expr.ILInstructions().size(), std::size_t{0});
        auto* unary = dynamic_cast<const Syntax::UnaryOperatorExpression*>(node);
        ASSERT_TRUE(unary != nullptr);
        EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Not);
        node = unary->Expression();
    }
    auto* invocation = dynamic_cast<const Syntax::InvocationExpression*>(node);
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef =
        dynamic_cast<const Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), expectedMethod);
    // The target is the Unsafe type reference.
    EXPECT_TRUE(dynamic_cast<Syntax::TypeReferenceExpression*>(memberRef->Target()) != nullptr);
    // Two ref-direction arguments over the two locals.
    EXPECT_EQ(invocation->Arguments().Count(), 2);
    auto* first = dynamic_cast<Syntax::DirectionExpression*>(invocation->Arguments().FirstOrNull());
    ASSERT_TRUE(first != nullptr);
    auto* second =
        dynamic_cast<Syntax::DirectionExpression*>(invocation->Arguments().NodeAt(1));
    ASSERT_TRUE(second != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Boolean");
    EXPECT_TRUE(expr.ResolveResult()->IsError() == false);
}
} // namespace

TEST(ExpressionBuilderCompTest, RefArmRendersAreSame)
{
    DriveRefComp(IL::ComparisonKind::Equality, "AreSame", false);
}

TEST(ExpressionBuilderCompTest, RefInequalityNegatesAreSame)
{
    DriveRefComp(IL::ComparisonKind::Inequality, "AreSame", true);
}

TEST(ExpressionBuilderCompTest, RefLessThanRendersIsAddressLessThan)
{
    DriveRefComp(IL::ComparisonKind::LessThan, "IsAddressLessThan", false);
}

TEST(ExpressionBuilderCompTest, RefGreaterThanOrEqualNegatesIsAddressLessThan)
{
    DriveRefComp(IL::ComparisonKind::GreaterThanOrEqual, "IsAddressLessThan", true);
}

TEST(ExpressionBuilderCompTest, RefGreaterThanRendersIsAddressGreaterThan)
{
    DriveRefComp(IL::ComparisonKind::GreaterThan, "IsAddressGreaterThan", false);
}

TEST(ExpressionBuilderCompTest, RefLessThanOrEqualNegatesIsAddressGreaterThan)
{
    DriveRefComp(IL::ComparisonKind::LessThanOrEqual, "IsAddressGreaterThan", true);
}

TEST(ExpressionBuilderCompTest, ToBinaryOperatorTypeMatrix)
{
    EXPECT_EQ(CSharp::ToBinaryOperatorType(IL::ComparisonKind::Equality),
              Syntax::BinaryOperatorType::Equality);
    EXPECT_EQ(CSharp::ToBinaryOperatorType(IL::ComparisonKind::Inequality),
              Syntax::BinaryOperatorType::InEquality);
    EXPECT_EQ(CSharp::ToBinaryOperatorType(IL::ComparisonKind::LessThan),
              Syntax::BinaryOperatorType::LessThan);
    EXPECT_EQ(CSharp::ToBinaryOperatorType(IL::ComparisonKind::LessThanOrEqual),
              Syntax::BinaryOperatorType::LessThanOrEqual);
    EXPECT_EQ(CSharp::ToBinaryOperatorType(IL::ComparisonKind::GreaterThan),
              Syntax::BinaryOperatorType::GreaterThan);
    EXPECT_EQ(CSharp::ToBinaryOperatorType(IL::ComparisonKind::GreaterThanOrEqual),
              Syntax::BinaryOperatorType::GreaterThanOrEqual);
    EXPECT_THROW(CSharp::ToBinaryOperatorType(static_cast<IL::ComparisonKind>(99)),
                 std::out_of_range);
}

TEST(ExpressionBuilderCompTest, CompCloneCarriesSign)
{
    IL::Comp comp(std::make_unique<IL::LdLoc>(nullptr), std::make_unique<IL::LdLoc>(nullptr),
                  IL::ComparisonKind::GreaterThan, TS::Sign::Unsigned);
    EXPECT_TRUE(comp.Unsigned);
    EXPECT_EQ(comp.Sign, TS::Sign::Unsigned);
    EXPECT_EQ(comp.InputType, IL::StackType::Unknown);
    auto clone = comp.Clone();
    auto* compClone = dynamic_cast<IL::Comp*>(clone.get());
    ASSERT_TRUE(compClone != nullptr);
    EXPECT_EQ(compClone->Sign, TS::Sign::Unsigned);
    EXPECT_TRUE(compClone->Unsigned);
    // The equality shape carries Sign.None with the false Unsigned view.
    IL::Comp eqComp(nullptr, nullptr, IL::ComparisonKind::Equality, TS::Sign::None);
    EXPECT_FALSE(eqComp.Unsigned);
    EXPECT_EQ(eqComp.Sign, TS::Sign::None);
    // The bool ctor maps false to Signed (the reader's relational mapping).
    IL::Comp relComp(nullptr, nullptr, IL::ComparisonKind::LessThan, false);
    EXPECT_FALSE(relComp.Unsigned);
    EXPECT_EQ(relComp.Sign, TS::Sign::Signed);
}

TEST(ExpressionBuilderCompTest, AdjustConstantToTypeNullableHintStaysUnchanged)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto nullableInt = TS::Create(fixture.compilation, *intType);
    // The C# `NullableType.GetUnderlyingType(typeHint)` unwraps the Nullable<T>
    // hint to T: an Int32 constant over a Nullable<Int32> hint compares equal
    // after the unwrap and is returned unchanged.
    auto rr = std::make_shared<Sem::ConstantResolveResult>(intType, 42);
    auto adjusted = builder.AdjustConstantToType(rr, *nullableInt);
    EXPECT_EQ(adjusted.get(), rr.get());
    // The lifted form: a Nullable<Int32> constant over the same hint also stays.
    auto liftedRR = std::make_shared<Sem::ConstantResolveResult>(nullableInt, 42);
    auto liftedAdjusted = builder.AdjustConstantToType(liftedRR, *nullableInt);
    EXPECT_EQ(liftedAdjusted.get(), liftedRR.get());
}

TEST(ExpressionBuilderCompTest, AdjustConstantToTypeEnumHintRetypesConstant)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    // A stub enum (the LookupTypeDefinition fixture shape): Kind Enum with the
    // Int32 underlying type -- the resolver's enum cast arm folds the constant
    // through the underlying type.
    auto enumDef = std::make_shared<TestSupport::LookupTypeDefinition>(
        "E", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "E")),
        TS::TypeKind::Enum, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    enumDef->SetEnumUnderlyingType(intType);
    // The Int32 constant over the enum hint re-types to the enum (the value is
    // in range so the checked cast folds).
    auto rr = std::make_shared<Sem::ConstantResolveResult>(intType, 1);
    auto adjusted = builder.AdjustConstantToType(rr, *enumDef);
    EXPECT_TRUE(adjusted->IsCompileTimeConstant());
    EXPECT_EQ(adjusted->Type().ReflectionName(), "Ns.E");
}


// ---------------------------------------------------------------------------
// The binary-numeric family (VisitBinaryNumericInstruction + the Handle* helpers)

// The translation drives through Visit (the OpCode switch), so every test uses the
// public Translate entry over a hand-built BinaryNumericInstruction (the
// ExpressionBuilderConvTest convention).

TEST(ExpressionBuilderBinaryNumericTest, AddOverIntLocalsRendersAdd)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::Add);
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Add);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
    // The arithmetic operators carry the checked/unchecked annotation; over a
    // non-constant (implicitly unchecked) add the unchecked annotation is added.
    EXPECT_TRUE(binary->Annotation<CSharp::Transforms::CheckedUncheckedAnnotation>()
                != nullptr);
    // The fresh BinaryOperatorExpression node carries only the bni annotation
    // (the operands' own annotations ride on their identifier nodes).
    EXPECT_EQ(expr.ILInstructions().size(), std::size_t{1});
    EXPECT_EQ(expr.ILInstructions()[0], &bni);
}

TEST(ExpressionBuilderBinaryNumericTest, ZeroMinusXRenderUnaryMinus)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdcI4>(0),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::Sub);
    auto expr = builder.Translate(&bni);
    // `0 - x` over a signed int is the unary-minus rewrite.
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::Minus);
    EXPECT_TRUE(unary->Annotation<CSharp::Transforms::CheckedUncheckedAnnotation>()
                != nullptr);
    // The resolve result is the negate operator over Int32.
    const auto* opResult =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opResult != nullptr);
    EXPECT_EQ(opResult->OperatorType(), TS::ExpressionType::Negate);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderBinaryNumericTest, ZeroMinusUncheckedCheckedAnnotationChoosesChecked)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdcI4>(0),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::Sub);
    bni.CheckForOverflow = true;
    auto expr = builder.Translate(&bni);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(unary != nullptr);
    const auto* annotation =
        unary->Annotation<CSharp::Transforms::CheckedUncheckedAnnotation>();
    ASSERT_TRUE(annotation != nullptr);
    EXPECT_TRUE(annotation->IsChecked);
    const auto* opResult =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opResult != nullptr);
    EXPECT_EQ(opResult->OperatorType(), TS::ExpressionType::NegateChecked);
}

TEST(ExpressionBuilderBinaryNumericTest, LongOperandTruncatesToInt32)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int64, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int64, "b");
    // An add whose IL input type is I4 over 8-byte long locals: the operands are
    // oversized and truncate to the I4 arithmetic type (int32) first.
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::Add);
    bni.LeftInputType = IL::StackType::I4;
    bni.RightInputType = IL::StackType::I4;
    bni.ResultStackType = IL::StackType::I4;
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    auto* leftCast = dynamic_cast<Syntax::CastExpression*>(binary->Left());
    ASSERT_TRUE(leftCast != nullptr);
    auto* rightCast = dynamic_cast<Syntax::CastExpression*>(binary->Right());
    ASSERT_TRUE(rightCast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderBinaryNumericTest, IntPtrInputConvertsToNativeInt)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::IntPtr, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::IntPtr, "b");
    // StackType.I-sized IntPtr inputs: none of the operators are supported on
    // IntPtr, so both inputs convert to the native-integer arithmetic type
    // (NativeIntegers on -> nint).
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::Add);
    bni.LeftInputType = IL::StackType::I;
    bni.RightInputType = IL::StackType::I;
    bni.ResultStackType = IL::StackType::I;
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    auto* leftCast = dynamic_cast<Syntax::CastExpression*>(binary->Left());
    ASSERT_TRUE(leftCast != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "nint");
}

TEST(ExpressionBuilderBinaryNumericTest, AddOverMixedWidthOperandsTruncatesOnlyTheOversizedOne)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int64, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    // An add whose IL input type is I4 over a long and an int32: only the long
    // operand is oversized and truncates; the int32 operand keeps its identity
    // (the common-type fallback would have converted BOTH operands).
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::Add);
    bni.LeftInputType = IL::StackType::I4;
    bni.RightInputType = IL::StackType::I4;
    bni.ResultStackType = IL::StackType::I4;
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    auto* leftCast = dynamic_cast<Syntax::CastExpression*>(binary->Left());
    ASSERT_TRUE(leftCast != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Right()) != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderBinaryNumericTest, BitwiseOrConstantLeftStaysConstant)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto b = MakeLocal(fixture, TS::KnownTypeCode::UInt32, "b");
    // A bitwise op with a constant operand re-renders the constant through
    // ConvertConstantValue (the hex-gate arm); the value itself is unchanged.
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdcI4>(0xFF),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::BitOr);
    bni.LeftInputType = IL::StackType::I4;
    bni.RightInputType = IL::StackType::I4;
    bni.ResultStackType = IL::StackType::I4;
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::BitwiseOr);
    auto* constant = dynamic_cast<Syntax::PrimitiveExpression*>(binary->Left());
    ASSERT_TRUE(constant != nullptr);
    const auto* value = std::get_if<std::int32_t>(&constant->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 0xFF);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.UInt32");
    // Bitwise operators never carry a checked/unchecked annotation
    // (BinaryOperatorMightCheckForOverflow is false for them).
    EXPECT_EQ(binary->Annotation<CSharp::Transforms::CheckedUncheckedAnnotation>(),
              nullptr);
}

TEST(ExpressionBuilderBinaryNumericTest, SmallIntegerShiftLeftPromotesWithoutCast)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int16, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    // With small integer types, C# promotes to int and performs signed shifts -- no
    // cast is needed on the left operand.
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::ShiftLeft);
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::ShiftLeft);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Left()) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::CastExpression*>(binary->Left()) == nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int32");
}

TEST(ExpressionBuilderBinaryNumericTest, UnsignedShiftRightOverSignedIntUsesUnsignedOperator)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    // An unsigned right shift over a same-size signed type with an unsigned
    // instruction sign uses the C# 11 >>> operator (the UnsignedRightShift
    // setting is on).
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::ShiftRight);
    bni.Sign = TS::Sign::Unsigned;
    bni.Signed = false;
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::UnsignedShiftRight);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Left()) != nullptr);
}

TEST(ExpressionBuilderBinaryNumericTest, UnsignedShiftRightOverUnsignedIntStaysShiftRight)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::UInt32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    // An unsigned shift over an already-unsigned type: casting to unsigned anyway,
    // so the plain >> operator is kept (the couldUseUnsignedRightShift else-if's
    // sign check fails).
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::ShiftRight);
    bni.Sign = TS::Sign::Unsigned;
    bni.Signed = false;
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::ShiftRight);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Left()) != nullptr);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.UInt32");
}

TEST(ExpressionBuilderBinaryNumericTest, ManagedRefPlusIntRendersAddByteOffset)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto elementType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto byRefType = std::make_shared<TS::ByReferenceType>(elementType);
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, byRefType);
    variable->Name = "a";
    // `ref a + 4` over an int reference: the LdLoca's ByReferenceResolveResult
    // nests the managed reference (int& &), so brt.ElementType is itself a
    // managed reference and PointerArithmeticOffset.Detect answers no match --
    // the raw byte offset falls through to the byte-offset intrinsic.
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoca>(variable),
                                     std::make_unique<IL::LdcI4>(4),
                                     IL::BinaryNumericOperator::Add, false,
                                     TS::Sign::None);
    auto expr = builder.Translate(&bni);
    // The reference-returning invocation is wrapped in a ref DirectionExpression.
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::Ref);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(direction->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "AddByteOffset");
    EXPECT_EQ(invocation->Arguments().Count(), 2);
    auto* refArg = dynamic_cast<Syntax::DirectionExpression*>(invocation->Arguments().FirstOrNull());
    ASSERT_TRUE(refArg != nullptr);
    auto* offset = dynamic_cast<Syntax::PrimitiveExpression*>(invocation->Arguments().NodeAt(1));
    ASSERT_TRUE(offset != nullptr);
    // The raw byte offset stays (the element type is a managed reference, so
    // Detect's element-size read answers null).
    const auto* value = std::get_if<std::int32_t>(&offset->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 4);
}

TEST(ExpressionBuilderBinaryNumericTest, ManagedRefPlusElementOffsetRendersAdd)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto elementType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto byRefType = std::make_shared<TS::ByReferenceType>(elementType);
    auto variable = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, byRefType);
    variable->Name = "a";
    // `ref a + 4 * sizeof(int)`: the mul-by-sizeof pattern detects the element count
    // (4), so the plain Add intrinsic renders the element offset.
    auto mul = std::make_unique<IL::BinaryNumericInstruction>(
        std::make_unique<IL::LdcI4>(4),
        std::make_unique<IL::SizeOf>(elementType, "System.Int32"),
        IL::BinaryNumericOperator::Mul);
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoca>(variable),
                                     std::move(mul),
                                     IL::BinaryNumericOperator::Add, false,
                                     TS::Sign::None);
    auto expr = builder.Translate(&bni);
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression());
    ASSERT_TRUE(direction != nullptr);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(direction->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    // The mul is not unwrapped (the element type is a managed reference) --
    // the raw byte offset renders as the `4 * sizeof(int)` binary expression.
    EXPECT_EQ(memberRef->MemberName(), "AddByteOffset");
    auto* offset = dynamic_cast<Syntax::BinaryOperatorExpression*>(invocation->Arguments().NodeAt(1));
    ASSERT_TRUE(offset != nullptr);
    EXPECT_EQ(offset->Operator(), Syntax::BinaryOperatorType::Multiply);
    auto* constant = dynamic_cast<Syntax::PrimitiveExpression*>(offset->Left());
    ASSERT_TRUE(constant != nullptr);
    const auto* value = std::get_if<std::int32_t>(&constant->Value());
    ASSERT_TRUE(value != nullptr);
    EXPECT_EQ(*value, 4);
}

TEST(ExpressionBuilderBinaryNumericTest, ManagedRefMinusRefRendersByteOffsetNamedArgs)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto elementType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto byRefType = std::make_shared<TS::ByReferenceType>(elementType);
    auto a = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, byRefType);
    a->Name = "a";
    auto b = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, byRefType);
    b->Name = "b";
    // `ref a - ref b` => Unsafe.ByteOffset(target: ..., origin: ...) -- the named
    // arguments order the operands the wrong way around.
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoca>(a),
                                     std::make_unique<IL::LdLoca>(b),
                                     IL::BinaryNumericOperator::Sub, false,
                                     TS::Sign::None);
    auto expr = builder.Translate(&bni);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr.Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "ByteOffset");
    EXPECT_EQ(invocation->Arguments().Count(), 2);
    auto* namedTarget =
        dynamic_cast<Syntax::NamedArgumentExpression*>(invocation->Arguments().FirstOrNull());
    ASSERT_TRUE(namedTarget != nullptr);
    EXPECT_EQ(namedTarget->Name(), "target");
    auto* namedOrigin =
        dynamic_cast<Syntax::NamedArgumentExpression*>(invocation->Arguments().NodeAt(1));
    ASSERT_TRUE(namedOrigin != nullptr);
    EXPECT_EQ(namedOrigin->Name(), "origin");
    // The result type is the IntPtr integer the C# infers for the byte offset.
    EXPECT_EQ(expr.Type().ReflectionName(), "System.IntPtr");
}

TEST(ExpressionBuilderBinaryNumericTest, PointerSubtractionRendersSubtractOverLong)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto ptrType = std::make_shared<TS::PointerType>(intType);
    auto p1 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    p1->Name = "p1";
    auto p2 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    p2->Name = "p2";
    // div(sub(p1, p2), 4) with matching int* pointer types: the pointer-subtraction
    // render is the pointer difference with an Int64 resolve result.
    // The sub's own stack type is I (the reader's pointer-arithmetic shape).
    auto sub = std::make_unique<IL::BinaryNumericInstruction>(
        std::make_unique<IL::LdLoc>(p1), std::make_unique<IL::LdLoc>(p2),
        IL::BinaryNumericOperator::Sub, IL::StackType::I);
    IL::BinaryNumericInstruction bni(std::move(sub), std::make_unique<IL::LdcI4>(4),
                                     IL::BinaryNumericOperator::Div);
    auto expr = builder.Translate(&bni);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Subtract);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Left()) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::IdentifierExpression*>(binary->Right()) != nullptr);
    const auto* opResult =
        dynamic_cast<const Sem::OperatorResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(opResult != nullptr);
    EXPECT_EQ(opResult->OperatorType(), TS::ExpressionType::Subtract);
    EXPECT_EQ(expr.Type().ReflectionName(), "System.Int64");
    // The C# `.WithILInstruction(new[] { inst, sub })` -- two IL annotations.
    ASSERT_EQ(expr.ILInstructions().size(), std::size_t{2});
    EXPECT_EQ(expr.ILInstructions()[0], &bni);
}

TEST(ExpressionBuilderBinaryNumericTest, PointerSubtractionGatesRejectNonMatchingShapes)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto intType = std::const_pointer_cast<TS::IType>(
        fixture.compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());
    auto ptrType = std::make_shared<TS::PointerType>(intType);
    auto p1 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    auto p2 = std::make_shared<IL::ILVariable>(IL::VariableKind::Local, ptrType);
    // Overflow-checked divisions are never pointer subtractions.
    {
        auto sub = std::make_unique<IL::BinaryNumericInstruction>(
            std::make_unique<IL::LdLoc>(p1), std::make_unique<IL::LdLoc>(p2),
            IL::BinaryNumericOperator::Sub, IL::StackType::I);
        IL::BinaryNumericInstruction bni(std::move(sub), std::make_unique<IL::LdcI4>(4),
                                         IL::BinaryNumericOperator::Div);
        bni.CheckForOverflow = true;
        EXPECT_FALSE(builder.HandlePointerSubtraction(bni).has_value());
    }
    // A div whose left input type is not StackType.I is not a pointer subtraction.
    {
        auto sub = std::make_unique<IL::BinaryNumericInstruction>(
            std::make_unique<IL::LdLoc>(p1), std::make_unique<IL::LdLoc>(p2),
            IL::BinaryNumericOperator::Sub, IL::StackType::I);
        IL::BinaryNumericInstruction bni(std::move(sub), std::make_unique<IL::LdcI4>(4),
                                         IL::BinaryNumericOperator::Div);
        bni.LeftInputType = IL::StackType::I8;
        EXPECT_FALSE(builder.HandlePointerSubtraction(bni).has_value());
    }
    // A div whose left operand is not a subtraction is not a pointer subtraction.
    {
        IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(p1),
                                         std::make_unique<IL::LdcI4>(4),
                                         IL::BinaryNumericOperator::Div);
        EXPECT_FALSE(builder.HandlePointerSubtraction(bni).has_value());
    }
}

TEST(ExpressionBuilderBinaryNumericTest, UnknownOperatorThrows)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto a = MakeLocal(fixture, TS::KnownTypeCode::Int32, "a");
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Int32, "b");
    IL::BinaryNumericInstruction bni(std::make_unique<IL::LdLoc>(a),
                                     std::make_unique<IL::LdLoc>(b),
                                     IL::BinaryNumericOperator::None);
    EXPECT_THROW(builder.Translate(&bni), std::out_of_range);
}

TEST(ExpressionBuilderBinaryNumericTest, IsCSharpSmallIntegerTypeReadsTheDefinition)
{
    BuilderFixture fixture;
    // A real corlib definition (Int16): the definition-based dispatch answers true.
    const TS::IType& int16Def = fixture.compilation.FindType(TS::KnownTypeCode::Int16);
    EXPECT_TRUE(TS::IsCSharpSmallIntegerType(&int16Def));
    // The minimal KnownType wrapper over Int16 (no definition): the wrapper
    // fallback answers true as well.
    auto wrapper = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int16);
    EXPECT_TRUE(TS::IsCSharpSmallIntegerType(wrapper.get()));
    // Int32 is not a small integer.
    const TS::IType& int32Def = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(&int32Def));
    // An enum with a small underlying type is NOT a C# small integer (the C#
    // reads the definition's KnownTypeCode, which is None for enums).
    auto enumDef = std::make_shared<TestSupport::LookupTypeDefinition>(
        "E", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "E")),
        TS::TypeKind::Enum, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    enumDef->SetEnumUnderlyingType(
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Int16).shared_from_this()));
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(enumDef.get()));
}

// The VisitIfInstruction arm (ExpressionBuilder.cs lines 3956-4049) and the
// IfInstruction.IsInConditionSlot predicate it composes.

TEST(IfInstructionConditionSlotTest, ChildRolesClassify)
{
    // A comparison against the constant 0: the non-constant operand sits in a
    // condition slot; the 0 constant itself does not (the C# checks the OTHER
    // operand).
    IL::Comp zeroComp(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(0),
                      IL::ComparisonKind::Inequality, TS::Sign::None);
    EXPECT_TRUE(IL::IfInstruction::IsInConditionSlot(zeroComp.Left.get()));
    EXPECT_FALSE(IL::IfInstruction::IsInConditionSlot(zeroComp.Right.get()));
    // A comparison against a non-zero constant: neither operand qualifies.
    IL::Comp nonzeroComp(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(2),
                         IL::ComparisonKind::Inequality, TS::Sign::None);
    EXPECT_FALSE(IL::IfInstruction::IsInConditionSlot(nonzeroComp.Left.get()));
    EXPECT_FALSE(IL::IfInstruction::IsInConditionSlot(nonzeroComp.Right.get()));
    // Of a root if, only the condition child is in a condition slot; the arms
    // recurse to the (root) if and come back false.
    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(2),
                             std::make_unique<IL::LdcI4>(3));
    EXPECT_TRUE(IL::IfInstruction::IsInConditionSlot(ifInst.Condition.get()));
    EXPECT_FALSE(IL::IfInstruction::IsInConditionSlot(ifInst.TrueInst.get()));
    EXPECT_FALSE(IL::IfInstruction::IsInConditionSlot(ifInst.FalseInst.get()));
    // A disconnected node has no slot.
    IL::LdcI4 standalone(9);
    EXPECT_FALSE(IL::IfInstruction::IsInConditionSlot(&standalone));
}

TEST(IfInstructionConditionSlotTest, NestedArmReachesConditionSlot)
{
    // outer's condition is an inner if; outer is itself the condition of the
    // root if, so the inner's arms transitively sit in a condition slot.
    auto inner = std::make_unique<IL::IfInstruction>(
        std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(2),
        std::make_unique<IL::LdcI4>(0));
    auto* innerPtr = inner.get();
    auto outer = std::make_unique<IL::IfInstruction>(
        std::move(inner), std::make_unique<IL::LdcI4>(3), std::make_unique<IL::LdcI4>(4));
    auto* outerPtr = outer.get();
    IL::IfInstruction root(std::move(outer), std::make_unique<IL::LdcI4>(5),
                           std::make_unique<IL::LdcI4>(6));
    EXPECT_TRUE(IL::IfInstruction::IsInConditionSlot(root.Condition.get()));
    EXPECT_TRUE(IL::IfInstruction::IsInConditionSlot(innerPtr));
    EXPECT_TRUE(IL::IfInstruction::IsInConditionSlot(innerPtr->Condition.get()));
    EXPECT_TRUE(IL::IfInstruction::IsInConditionSlot(innerPtr->TrueInst.get()));
    (void)outerPtr;
}

TEST(ExpressionBuilderIfInstructionTest, PlainIfRendersConditionalExpression)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(2),
                             std::make_unique<IL::LdcI4>(3));
    auto expr = builder.Translate(&ifInst);
    auto* cond = dynamic_cast<Syntax::ConditionalExpression*>(expr.Expression());
    ASSERT_TRUE(cond != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(cond->Condition()) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(cond->TrueExpression()) != nullptr);
    EXPECT_TRUE(dynamic_cast<Syntax::PrimitiveExpression*>(cond->FalseExpression()) != nullptr);
    // The IL annotation sits on the conditional expression.
    std::vector<IL::ILInstruction*> il = CSharp::GetILInstructions(*cond);
    ASSERT_EQ(il.size(), 1u);
    EXPECT_EQ(il[0], &ifInst);
}

TEST(ExpressionBuilderIfInstructionTest, BooleanLogicAndRendersConditionalAnd)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "b");
    auto c = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "c");
    IL::IfInstruction ifInst(std::make_unique<IL::LdLoc>(b), std::make_unique<IL::LdLoc>(c),
                             std::make_unique<IL::LdcI4>(0));
    auto expr = builder.Translate(&ifInst);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::ConditionalAnd);
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::Boolean));
}

TEST(ExpressionBuilderIfInstructionTest, BooleanLogicOrRendersConditionalOr)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    auto b = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "b");
    auto c = MakeLocal(fixture, TS::KnownTypeCode::Boolean, "c");
    IL::IfInstruction ifInst(std::make_unique<IL::LdLoc>(b), std::make_unique<IL::LdcI4>(1),
                             std::make_unique<IL::LdLoc>(c));
    auto expr = builder.Translate(&ifInst);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::ConditionalOr);
    EXPECT_TRUE(TS::IsKnownType(expr.Type(), TS::KnownTypeCode::Boolean));
}

TEST(ExpressionBuilderIfInstructionTest, NonBooleanLogicAndFallsBackToConditional)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The logic-and shape with a non-boolean rhs, at the root (not in a
    // condition slot), cannot be rendered as `&&`; it degrades to `?:`.
    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(5),
                             std::make_unique<IL::LdcI4>(0));
    auto expr = builder.Translate(&ifInst);
    EXPECT_TRUE(dynamic_cast<Syntax::ConditionalExpression*>(expr.Expression()) != nullptr);
}

TEST(ExpressionBuilderIfInstructionTest, MissingFalseArmRendersNopError)
{
    BuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    // The reader's fall-through if has a null FalseInst; the missing arm
    // translates as the Nop error expression (the C# Nop default).
    IL::IfInstruction ifInst(std::make_unique<IL::LdcI4>(1), std::make_unique<IL::LdcI4>(2));
    auto expr = builder.Translate(&ifInst);
    auto* cond = dynamic_cast<Syntax::ConditionalExpression*>(expr.Expression());
    ASSERT_TRUE(cond != nullptr);
    EXPECT_TRUE(cond->FalseExpression() != nullptr);
}

} // namespace ILSpy::Tests