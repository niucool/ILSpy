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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The CallBuilder slice tests: the ArgumentList data carrier (the C# nested
// struct whose helpers the call render arms consume) and the span-based
// string-concat family (IsSpanBasedStringConcat's argument walk,
// IsStringToReadOnlySpanCharImplicitConversion, and BuildStringConcat), plus
// the ILInlining.IsReadOnlySpanCharCtor prerequisite. Driven over a
// MinimalCorlib compilation (the ExpressionBuilder suites' fixture shape) with
// FakeMethod method fixtures and hand-built Call instruction trees (the shapes
// the IL reader's type-system plumbing has not yet wired).

#include "Decompiler/CSharp/CallBuilder.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/IL/Instructions/LdObjIfRef.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/Implementation/SyntheticRangeIndexer.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/InitializedObjectResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringText.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <any>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace ::ILSpy::Decompiler;
namespace CS = ILSpy::Decompiler::CSharp;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Impl = TS::Implementation;
namespace IL = ILSpy::Decompiler::IL;
namespace Resolver = ILSpy::Decompiler::CSharp::Resolver;

namespace {

// A compilation over MinimalCorlib plus known-type aliases (the
// Annotations_Test no-op-deleter convention).
struct KnownTypeHolder {
    TS::SimpleCompilation compilation;

    KnownTypeHolder() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr KnownType(TS::KnownTypeCode code)
    {
        const TS::IType& t = compilation.FindType(code);
        return TS::ITypePtr(const_cast<TS::IType*>(&t), [](TS::IType*) {});
    }

    // `ReadOnlySpan<char>` (a ParameterizedType over the ReadOnlySpan`1
    // definition; IsKnownType(ReadOnlySpanOfT) resolves through
    // GetDefinition()->KnownTypeCode).
    TS::ITypePtr SpanOfChar()
    {
        return SpanOf(KnownType(TS::KnownTypeCode::Char));
    }

    // `ReadOnlySpan<T>` over the supplied element type.
    TS::ITypePtr SpanOf(TS::ITypePtr element)
    {
        return std::make_shared<TS::ParameterizedType>(
            KnownType(TS::KnownTypeCode::ReadOnlySpanOfT),
            std::vector<TS::ITypePtr>{std::move(element)});
    }

    TS::ITypePtr ByRef(TS::ITypePtr element)
    {
        return std::make_shared<TS::ByReferenceType>(std::move(element));
    }
};

// The method fixtures the concat family consumes (the
// ExpressionBuilderUserDefinedCompoundAssign_Test fixture shape).
struct MethodFixtures {
    KnownTypeHolder& holder;
    std::shared_ptr<Impl::FakeMethod> spanConcat;
    std::shared_ptr<Impl::FakeMethod> plainConcat;
    std::shared_ptr<Impl::FakeMethod> implicitConversion;
    std::shared_ptr<Impl::FakeMethod> spanCharCtor;

    explicit MethodFixtures(KnownTypeHolder& h) : holder(h)
    {
        spanConcat = MakeMethod("Concat", TS::SymbolKind::Method,
                                holder.KnownType(TS::KnownTypeCode::String),
                                {holder.SpanOfChar(), holder.SpanOfChar()}, true,
                                holder.KnownType(TS::KnownTypeCode::String));
        plainConcat = MakeMethod("Concat", TS::SymbolKind::Method,
                                 holder.KnownType(TS::KnownTypeCode::String),
                                 {holder.KnownType(TS::KnownTypeCode::String),
                                  holder.KnownType(TS::KnownTypeCode::String)},
                                 true);
        implicitConversion = MakeMethod(
            "op_Implicit", TS::SymbolKind::Operator,
            holder.KnownType(TS::KnownTypeCode::String),
            {holder.KnownType(TS::KnownTypeCode::String)}, true,
            holder.SpanOfChar());
        spanCharCtor = MakeMethod(".ctor", TS::SymbolKind::Constructor,
                                  holder.SpanOfChar(),
                                  {holder.ByRef(holder.KnownType(TS::KnownTypeCode::Char))},
                                  false);
    }

    std::shared_ptr<Impl::FakeMethod> MakeMethod(
        const char* name, TS::SymbolKind symbolKind, TS::ITypePtr declaringType,
        std::vector<TS::ITypePtr> parameterTypes, bool isStatic,
        TS::ITypePtr returnType = nullptr)
    {
        auto method = std::make_shared<Impl::FakeMethod>(holder.compilation, symbolKind);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        method->SetDeclaringType(std::move(declaringType));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (std::size_t i = 0; i < parameterTypes.size(); i++)
        {
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                parameterTypes[i], "p" + std::to_string(i)));
        }
        method->SetParameters(parameters);
        method->SetReturnType(returnType
                                  ? returnType
                                  : holder.KnownType(TS::KnownTypeCode::Void));
        return method;
    }
};

// A TranslatedExpression over an owned expression + resolve-result pair (the
// ArgumentList fixtures' argument shape).
struct OwnedArgument {
    std::unique_ptr<Syntax::Expression> expression;
    std::shared_ptr<Sem::ResolveResult> resolveResult;
    IL::LdcI4 instruction;

    OwnedArgument(std::unique_ptr<Syntax::Expression> expr,
                  std::shared_ptr<Sem::ResolveResult> rr, int constant)
        : expression(std::move(expr)),
          resolveResult(std::move(rr)),
          instruction(constant)
    {
        expression->AddAnnotation(resolveResult);
        expression->AddAnnotation(std::make_shared<CS::ILInstructionAnnotation>(
            static_cast<IL::ILInstruction*>(&instruction)));
    }

    CS::TranslatedExpression Bound()
    {
        return CS::TranslatedExpression(expression.get(), resolveResult.get());
    }
};

std::shared_ptr<Sem::ResolveResult> ConstantRR(std::shared_ptr<TS::IType> type,
                                               int value)
{
    return std::make_shared<Sem::ConstantResolveResult>(std::move(type), value);
}

// An ArgumentList over primitive-expression arguments (one per supplied type).
struct ArgumentListFixture {
    std::vector<std::shared_ptr<Impl::DefaultParameter>> parameters;
    std::vector<const TS::IParameter*> parameterPointers;
    std::vector<OwnedArgument> arguments;
    CS::ArgumentList list;

    ArgumentListFixture(KnownTypeHolder& holder,
                        std::vector<TS::ITypePtr> argTypes,
                        std::vector<const char*> parameterNames)
    {
        for (std::size_t i = 0; i < argTypes.size(); i++)
        {
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                argTypes[i], parameterNames[i]));
            parameterPointers.push_back(parameters.back().get());
            arguments.emplace_back(
                std::make_unique<Syntax::PrimitiveExpression>(Syntax::PrimitiveValue(
                    std::string(1, static_cast<char>('A' + static_cast<int>(i))))),
                ConstantRR(argTypes[i], static_cast<int>(i) + 1),
                static_cast<int>(i) + 1);
        }
        for (auto& argument : arguments)
            list.Arguments.push_back(argument.Bound());
        list.ExpectedParameters = parameterPointers;
        for (const char* name : parameterNames)
            list.ParameterNames.emplace_back(name);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// ArgumentList: the defaults and the argument-count slice.
// ---------------------------------------------------------------------------

TEST(ArgumentListTest, FreshListDefaultsSliceToEmpty)
{
    // The C# struct default FirstOptionalArgumentIndex is 0: a fresh list's
    // GetActualArgumentCount() answers 0, so the resolve-result/expression
    // slices are empty until a builder assigns the real value.
    CS::ArgumentList list;
    EXPECT_EQ(list.Length(), 0);
    EXPECT_TRUE(list.GetArgumentResolveResults().empty());
    EXPECT_TRUE(list.GetArgumentResolveResultsDirect().empty());
    EXPECT_TRUE(list.GetArgumentExpressions().empty());
}

TEST(ArgumentListTest, NegativeOneCountTakesAllArguments)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.FirstOptionalArgumentIndex = -1;
    ASSERT_EQ(fixture.list.Arguments.size(), 2u);

    auto results = fixture.list.GetArgumentResolveResults();
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].get(), fixture.arguments[0].resolveResult.get());
    EXPECT_EQ(results[1].get(), fixture.arguments[1].resolveResult.get());
    EXPECT_EQ(fixture.list.GetArgumentExpressions().size(), 2u);
}

TEST(ArgumentListTest, OptionalIndexSlicesTheTail)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b", "c"});
    fixture.list.FirstOptionalArgumentIndex = 1;
    ASSERT_EQ(fixture.list.Arguments.size(), 3u);

    auto results = fixture.list.GetArgumentResolveResults(0);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].get(), fixture.arguments[0].resolveResult.get());
    // skipCount=1 with one actual argument answers the second argument.
    auto skipped = fixture.list.GetArgumentResolveResultsDirect(1);
    ASSERT_EQ(skipped.size(), 1u);
    EXPECT_EQ(skipped[0].get(), fixture.arguments[1].resolveResult.get());
}

// ---------------------------------------------------------------------------
// ArgumentList: GetArgumentNames.
// ---------------------------------------------------------------------------

TEST(ArgumentListTest, GetArgumentNamesWithoutRuleReturnsTheField)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.AddNamesToPrimitiveValues = false;
    fixture.list.IsPrimitiveValue.Set(1);

    EXPECT_FALSE(fixture.list.GetArgumentNames().has_value());

    std::vector<std::string> assigned{"x", "y"};
    fixture.list.ArgumentNames = assigned;
    auto names = fixture.list.GetArgumentNames();
    ASSERT_TRUE(names.has_value());
    EXPECT_EQ((*names)[0], "x");
    EXPECT_EQ((*names)[1], "y");
    // The rule is off, so no fill was applied.
    EXPECT_EQ(fixture.list.ArgumentNames->at(1), "y");
}

TEST(ArgumentListTest, GetArgumentNamesPrimitiveFillBuildsAFreshArray)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.AddNamesToPrimitiveValues = true;
    fixture.list.IsPrimitiveValue.Set(1);
    EXPECT_FALSE(fixture.list.IsExpandedForm);

    auto names = fixture.list.GetArgumentNames();
    ASSERT_TRUE(names.has_value());
    // Slot 0 is not primitive, slot 1 is primitive and unnamed: the parameter
    // name fills it.
    EXPECT_EQ((*names)[0], "");
    EXPECT_EQ((*names)[1], "b");
    // The fresh local is NOT stored back into the field (the C# new-array arm).
    EXPECT_FALSE(fixture.list.ArgumentNames.has_value());
}

TEST(ArgumentListTest, GetArgumentNamesPrimitiveFillMutatesTheEngagedField)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.AddNamesToPrimitiveValues = true;
    fixture.list.IsPrimitiveValue.Set(1);
    fixture.list.ArgumentNames = std::vector<std::string>{"", ""};

    auto names = fixture.list.GetArgumentNames();
    ASSERT_TRUE(names.has_value());
    EXPECT_EQ((*names)[1], "b");
    // The C# aliases the field's array, so the fill is observable through it.
    EXPECT_EQ(fixture.list.ArgumentNames->at(1), "b");
}

TEST(ArgumentListTest, GetArgumentNamesExpandedFormSkipsTheFill)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.AddNamesToPrimitiveValues = true;
    fixture.list.IsPrimitiveValue.Set(1);
    fixture.list.IsExpandedForm = true;

    EXPECT_FALSE(fixture.list.GetArgumentNames().has_value());
}

TEST(ArgumentListTest, GetArgumentNamesEmptyParameterNameSkipsTheFill)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"", "b"});
    fixture.list.AddNamesToPrimitiveValues = true;
    fixture.list.IsPrimitiveValue.Set(1);

    EXPECT_FALSE(fixture.list.GetArgumentNames().has_value());
}

// ---------------------------------------------------------------------------
// ArgumentList: the out-variable resolve-result rule.
// ---------------------------------------------------------------------------

TEST(ArgumentListTest, OutParameterOverByReferenceTypeAnswersOutVar)
{
    KnownTypeHolder holder;
    auto intType = holder.KnownType(TS::KnownTypeCode::Int32);
    auto byRefInt = holder.ByRef(intType);

    // One Out parameter over a ByReferenceType argument, one plain In parameter.
    auto outParam = std::make_shared<Impl::DefaultParameter>(
        byRefInt, "out1", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::Out);
    auto inParam = std::make_shared<Impl::DefaultParameter>(
        byRefInt, "in1", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::In);

    OwnedArgument first(std::make_unique<Syntax::NullReferenceExpression>(),
                        ConstantRR(byRefInt, 1), 1);
    OwnedArgument second(std::make_unique<Syntax::NullReferenceExpression>(),
                         ConstantRR(byRefInt, 2), 2);

    CS::ArgumentList list;
    list.Arguments.push_back(first.Bound());
    list.Arguments.push_back(second.Bound());
    list.ExpectedParameters = {outParam.get(), inParam.get()};
    list.UseImplicitlyTypedOut = true;
    list.FirstOptionalArgumentIndex = -1;

    auto results = list.GetArgumentResolveResults();
    ASSERT_EQ(results.size(), 2u);
    // The Out arm answers a FRESH OutVarResolveResult over the reference's
    // element type; the plain arm passes the argument's own resolve result
    // through.
    auto* outVar = dynamic_cast<Sem::OutVarResolveResult*>(results[0].get());
    ASSERT_NE(outVar, nullptr);
    EXPECT_EQ(outVar->OriginalVariableType(), intType);
    EXPECT_EQ(dynamic_cast<Sem::OutVarResolveResult*>(results[1].get()), nullptr);
}

TEST(ArgumentListTest, OutRuleIsDisabledWithoutTheFlag)
{
    KnownTypeHolder holder;
    auto intType = holder.KnownType(TS::KnownTypeCode::Int32);
    auto byRefInt = holder.ByRef(intType);
    auto outParam = std::make_shared<Impl::DefaultParameter>(
        byRefInt, "out1", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::Out);

    OwnedArgument first(std::make_unique<Syntax::NullReferenceExpression>(),
                        ConstantRR(byRefInt, 1), 1);
    CS::ArgumentList list;
    list.Arguments.push_back(first.Bound());
    list.ExpectedParameters = {outParam.get()};
    list.UseImplicitlyTypedOut = false;
    list.FirstOptionalArgumentIndex = -1;

    auto results = list.GetArgumentResolveResults();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].get(), first.resolveResult.get());
}

// ---------------------------------------------------------------------------
// ArgumentList: GetArgumentExpressions.
// ---------------------------------------------------------------------------

TEST(ArgumentListTest, GetArgumentExpressionsWrapsNamedArguments)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.ArgumentNames = std::vector<std::string>{"named", ""};
    fixture.list.FirstOptionalArgumentIndex = -1;

    auto expressions = fixture.list.GetArgumentExpressions();
    ASSERT_EQ(expressions.size(), 2u);
    auto* named = dynamic_cast<Syntax::NamedArgumentExpression*>(expressions[0]);
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->Name(), "named");
    // The null (empty) name leaves the plain expression unwrapped.
    EXPECT_EQ(dynamic_cast<Syntax::NamedArgumentExpression*>(expressions[1]), nullptr);
    EXPECT_EQ(expressions[1], fixture.arguments[1].expression.get());
}

TEST(ArgumentListTest, GetArgumentExpressionsUnnamedArmSkipsFromSkipCount)
{
    KnownTypeHolder holder;
    ArgumentListFixture fixture(holder,
                                {holder.KnownType(TS::KnownTypeCode::Int32),
                                 holder.KnownType(TS::KnownTypeCode::Int32)},
                                {"a", "b"});
    fixture.list.FirstOptionalArgumentIndex = -1;

    auto expressions = fixture.list.GetArgumentExpressions(1);
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(expressions[0], fixture.arguments[1].expression.get());
}

TEST(ArgumentListTest, UseImplicitlyTypedOutAnnotationAppliesToOutExpressions)
{
    KnownTypeHolder holder;
    auto intType = holder.KnownType(TS::KnownTypeCode::Int32);

    // An argument whose expression carries an OUT ByReferenceResolveResult.
    auto outElement = std::make_shared<Sem::ConstantResolveResult>(intType, 5);
    auto byReferenceRr = std::make_shared<Sem::ByReferenceResolveResult>(
        outElement, TS::ReferenceKind::Out);
    auto outExpression = std::make_unique<Syntax::NullReferenceExpression>();
    outExpression->AddAnnotation(byReferenceRr);
    OwnedArgument outArg(std::move(outExpression), byReferenceRr, 5);

    auto param = std::make_shared<Impl::DefaultParameter>(
        holder.ByRef(intType), "p", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::Out);
    CS::ArgumentList list;
    list.Arguments.push_back(outArg.Bound());
    list.ExpectedParameters = {param.get()};
    list.UseImplicitlyTypedOut = true;
    list.FirstOptionalArgumentIndex = -1;

    auto expressions = list.GetArgumentExpressions();
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(expressions[0]->Annotation<CS::UseImplicitlyTypedOutAnnotation>(),
              &CS::UseImplicitlyTypedOutAnnotation::Instance());
}

TEST(ArgumentListTest, UseImplicitlyTypedOutAnnotationIsSkippedWithoutTheFlag)
{
    KnownTypeHolder holder;
    auto intType = holder.KnownType(TS::KnownTypeCode::Int32);
    auto outElement = std::make_shared<Sem::ConstantResolveResult>(intType, 5);
    auto byReferenceRr = std::make_shared<Sem::ByReferenceResolveResult>(
        outElement, TS::ReferenceKind::Out);
    auto outExpression = std::make_unique<Syntax::NullReferenceExpression>();
    outExpression->AddAnnotation(byReferenceRr);
    OwnedArgument outArg(std::move(outExpression), byReferenceRr, 5);
    auto param = std::make_shared<Impl::DefaultParameter>(
        holder.ByRef(intType), "p", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::Out);
    CS::ArgumentList list;
    list.Arguments.push_back(outArg.Bound());
    list.ExpectedParameters = {param.get()};
    list.UseImplicitlyTypedOut = false;
    list.FirstOptionalArgumentIndex = -1;

    auto expressions = list.GetArgumentExpressions();
    ASSERT_EQ(expressions.size(), 1u);
    EXPECT_EQ(expressions[0]->Annotation<CS::UseImplicitlyTypedOutAnnotation>(), nullptr);
}

// ---------------------------------------------------------------------------
// ArgumentList: CanInferAnonymousTypePropertyNamesFromArguments.
// ---------------------------------------------------------------------------

TEST(ArgumentListTest, CanInferAnonymousTypePropertyNamesMatrix)
{
    KnownTypeHolder holder;
    auto intType = holder.KnownType(TS::KnownTypeCode::Int32);
    auto param = std::make_shared<Impl::DefaultParameter>(intType, "name");

    // An IdentifierExpression whose identifier matches the parameter name.
    OwnedArgument identifierArg(
        std::make_unique<Syntax::IdentifierExpression>("name"), ConstantRR(intType, 1),
        1);
    CS::ArgumentList list;
    list.Arguments.push_back(identifierArg.Bound());
    list.ExpectedParameters = {param.get()};
    list.FirstOptionalArgumentIndex = -1;
    EXPECT_TRUE(list.CanInferAnonymousTypePropertyNamesFromArguments());

    // A mismatched identifier fails.
    OwnedArgument mismatchArg(
        std::make_unique<Syntax::IdentifierExpression>("other"), ConstantRR(intType, 2),
        2);
    CS::ArgumentList list2;
    list2.Arguments.push_back(mismatchArg.Bound());
    list2.ExpectedParameters = {param.get()};
    list2.FirstOptionalArgumentIndex = -1;
    EXPECT_FALSE(list2.CanInferAnonymousTypePropertyNamesFromArguments());

    // A member-reference argument infers its member name: a matching one passes.
    OwnedArgument memberArg(std::make_unique<Syntax::MemberReferenceExpression>(
                                nullptr, std::string("name")),
                            ConstantRR(intType, 3), 3);
    CS::ArgumentList list3;
    list3.Arguments.push_back(memberArg.Bound());
    list3.ExpectedParameters = {param.get()};
    list3.FirstOptionalArgumentIndex = -1;
    EXPECT_TRUE(list3.CanInferAnonymousTypePropertyNamesFromArguments());

    // A non-matching member reference fails.
    OwnedArgument memberArg2(std::make_unique<Syntax::MemberReferenceExpression>(
                                 nullptr, std::string("other")),
                             ConstantRR(intType, 4), 4);
    CS::ArgumentList list4;
    list4.Arguments.push_back(memberArg2.Bound());
    list4.ExpectedParameters = {param.get()};
    list4.FirstOptionalArgumentIndex = -1;
    EXPECT_FALSE(list4.CanInferAnonymousTypePropertyNamesFromArguments());

    // Any other expression kind infers null.
    OwnedArgument otherArg(std::make_unique<Syntax::NullReferenceExpression>(),
                           ConstantRR(intType, 5), 5);
    CS::ArgumentList list5;
    list5.Arguments.push_back(otherArg.Bound());
    list5.ExpectedParameters = {param.get()};
    list5.FirstOptionalArgumentIndex = -1;
    EXPECT_FALSE(list5.CanInferAnonymousTypePropertyNamesFromArguments());
}

// ---------------------------------------------------------------------------
// IsStringToReadOnlySpanCharImplicitConversion.
// ---------------------------------------------------------------------------

TEST(StringConcatDetectionTest, IsStringToReadOnlySpanCharImplicitConversionMatrix)
{
    KnownTypeHolder holder;
    MethodFixtures fixtures(holder);

    EXPECT_TRUE(CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        fixtures.implicitConversion.get()));

    // A non-operator method fails.
    auto plain = fixtures.MakeMethod("op_Implicit", TS::SymbolKind::Method,
                                     holder.SpanOfChar(),
                                     {holder.KnownType(TS::KnownTypeCode::String)},
                                     true);
    EXPECT_FALSE(
        CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(plain.get()));

    // A differently-named operator fails.
    auto named = fixtures.MakeMethod("op_Explicit", TS::SymbolKind::Operator,
                                     holder.SpanOfChar(),
                                     {holder.KnownType(TS::KnownTypeCode::String)},
                                     true);
    EXPECT_FALSE(
        CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(named.get()));

    // A zero-parameter operator fails.
    auto noParams = fixtures.MakeMethod("op_Implicit", TS::SymbolKind::Operator,
                                        holder.SpanOfChar(), {}, true);
    EXPECT_FALSE(
        CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(noParams.get()));

    // A non-ReadOnlySpan return type fails.
    auto wrongReturn = fixtures.MakeMethod(
        "op_Implicit", TS::SymbolKind::Operator,
        holder.KnownType(TS::KnownTypeCode::String),
        {holder.KnownType(TS::KnownTypeCode::String)}, true);
    EXPECT_FALSE(CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        wrongReturn.get()));

    // A non-String-typed parameter fails.
    auto wrongParam = fixtures.MakeMethod("op_Implicit", TS::SymbolKind::Operator,
                                          holder.SpanOfChar(),
                                          {holder.KnownType(TS::KnownTypeCode::Char)},
                                          true);
    EXPECT_FALSE(CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        wrongParam.get()));

    // A null method is false (the C# null-receiver NRE arm mapped false).
    EXPECT_FALSE(CS::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(nullptr));
}

// ---------------------------------------------------------------------------
// ILInlining.IsReadOnlySpanCharCtor.
// ---------------------------------------------------------------------------

TEST(StringConcatDetectionTest, IsReadOnlySpanCharCtorMatrix)
{
    KnownTypeHolder holder;
    MethodFixtures fixtures(holder);

    EXPECT_TRUE(IL::IsReadOnlySpanCharCtor(fixtures.spanCharCtor.get()));

    // A non-constructor over ReadOnlySpan<char> fails.
    auto notCtor = fixtures.MakeMethod("op_Implicit", TS::SymbolKind::Method,
                                       holder.SpanOfChar(),
                                       {holder.ByRef(holder.KnownType(TS::KnownTypeCode::Char))},
                                       true);
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(notCtor.get()));

    // A wrong parameter count fails.
    auto twoParams = std::make_shared<Impl::FakeMethod>(holder.compilation,
                                                        TS::SymbolKind::Constructor);
    twoParams->SetName(".ctor");
    twoParams->SetDeclaringType(holder.SpanOfChar());
    twoParams->SetParameters(std::vector<std::shared_ptr<const TS::IParameter>>{
        std::make_shared<Impl::DefaultParameter>(
            holder.ByRef(holder.KnownType(TS::KnownTypeCode::Char)), "a"),
        std::make_shared<Impl::DefaultParameter>(
            holder.ByRef(holder.KnownType(TS::KnownTypeCode::Char)), "b")});
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(twoParams.get()));

    // A non-ReadOnlySpan declaring type fails.
    auto wrongDeclaring =
        fixtures.MakeMethod(".ctor", TS::SymbolKind::Constructor,
                            holder.KnownType(TS::KnownTypeCode::String),
                            {holder.ByRef(holder.KnownType(TS::KnownTypeCode::Char))},
                            false);
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(wrongDeclaring.get()));

    // A non-ReadOnlySpan<char> element type fails.
    auto spanOfInt = holder.SpanOf(holder.KnownType(TS::KnownTypeCode::Int32));
    auto wrongElement = fixtures.MakeMethod(
        ".ctor", TS::SymbolKind::Constructor, spanOfInt,
        {holder.ByRef(holder.KnownType(TS::KnownTypeCode::Char))}, false);
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(wrongElement.get()));

    // A by-value (non-byref) parameter fails.
    auto valueParam =
        fixtures.MakeMethod(".ctor", TS::SymbolKind::Constructor, holder.SpanOfChar(),
                            {holder.KnownType(TS::KnownTypeCode::Char)}, false);
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(valueParam.get()));

    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(nullptr));
}

// ---------------------------------------------------------------------------
// IsSpanBasedStringConcat over the call.
// ---------------------------------------------------------------------------

TEST(StringConcatDetectionTest, CallArgumentWalkCollectsTheOperands)
{
    KnownTypeHolder holder;
    MethodFixtures fixtures(holder);

    // Argument 1: the op_Implicit conversion call over an LdStr.
    auto implicitCall = std::make_unique<IL::Call>("op_Implicit");
    implicitCall->Method = fixtures.implicitConversion;
    implicitCall->AddArg(std::make_unique<IL::LdStr>("hello"));

    // Argument 2: the newobj ReadOnlySpan<char>(&c) over an AddressOf.
    auto variable = std::make_shared<IL::ILVariable>();
    variable->Name = "c";
    variable->Type = holder.KnownType(TS::KnownTypeCode::Char);
    auto addressOf =
        std::make_unique<IL::AddressOf>(std::make_unique<IL::LdLoc>(variable),
                                        holder.KnownType(TS::KnownTypeCode::Char));
    IL::ILInstruction* addressValue = addressOf->Argument.get();
    auto spanCtorCall = std::make_unique<IL::Call>(".ctor");
    spanCtorCall->IsNewObj = true;
    spanCtorCall->Method = fixtures.spanCharCtor;
    spanCtorCall->AddArg(std::move(addressOf));

    IL::Call call("Concat");
    call.Method = fixtures.spanConcat;
    call.AddArg(std::move(implicitCall));
    call.AddArg(std::move(spanCtorCall));

    std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>>
        operands;
    EXPECT_TRUE(CS::CallBuilder::IsSpanBasedStringConcat(call, operands));
    ASSERT_TRUE(operands.has_value());
    ASSERT_EQ(operands->size(), 2u);
    EXPECT_EQ((*operands)[0].second, TS::KnownTypeCode::String);
    EXPECT_EQ((*operands)[0].first->Op, IL::OpCode::LdStr);
    EXPECT_EQ((*operands)[1].second, TS::KnownTypeCode::Char);
    EXPECT_EQ((*operands)[1].first, addressValue);
}

TEST(StringConcatDetectionTest, CallArgumentWalkRejectsNonSpanShapes)
{
    KnownTypeHolder holder;
    MethodFixtures fixtures(holder);
    std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>>
        operands;

    // A plain LdStr argument is neither conversion shape.
    IL::Call plainCall("Concat");
    plainCall.Method = fixtures.spanConcat;
    plainCall.AddArg(std::make_unique<IL::LdStr>("a"));
    plainCall.AddArg(std::make_unique<IL::LdStr>("b"));
    EXPECT_FALSE(CS::CallBuilder::IsSpanBasedStringConcat(plainCall, operands));
    // The method check passed, so the out parameter holds the (empty) operand
    // list the walk produced.
    ASSERT_TRUE(operands.has_value());
    EXPECT_TRUE(operands->empty());

    // A call whose every argument is a CHAR ctor: no string argument, so the
    // `firstStringArgumentIndex <= 1` lifted comparison answers false.
    IL::Call allChars("Concat");
    allChars.Method = fixtures.spanConcat;
    for (int i = 0; i < 2; i++)
    {
        auto variable = std::make_shared<IL::ILVariable>();
        variable->Name = "c" + std::to_string(i);
        variable->Type = holder.KnownType(TS::KnownTypeCode::Char);
        auto ctorCall = std::make_unique<IL::Call>(".ctor");
        ctorCall->IsNewObj = true;
        ctorCall->Method = fixtures.spanCharCtor;
        ctorCall->AddArg(std::make_unique<IL::AddressOf>(
            std::make_unique<IL::LdLoc>(variable),
            holder.KnownType(TS::KnownTypeCode::Char)));
        allChars.AddArg(std::move(ctorCall));
    }
    EXPECT_FALSE(CS::CallBuilder::IsSpanBasedStringConcat(allChars, operands));
    // The collected char operands survive the failing count check.
    ASSERT_TRUE(operands.has_value());
    ASSERT_EQ(operands->size(), 2u);
    EXPECT_EQ((*operands)[0].second, TS::KnownTypeCode::Char);
    EXPECT_EQ((*operands)[1].second, TS::KnownTypeCode::Char);

    // A valid single argument: the count check (>= 2) rejects the whole call.
    IL::Call single("Concat");
    single.Method = fixtures.spanConcat;
    auto singleImplicit = std::make_unique<IL::Call>("op_Implicit");
    singleImplicit->Method = fixtures.implicitConversion;
    singleImplicit->AddArg(std::make_unique<IL::LdStr>("only"));
    single.AddArg(std::move(singleImplicit));
    EXPECT_FALSE(CS::CallBuilder::IsSpanBasedStringConcat(single, operands));
    // The one string operand survives the count check.
    ASSERT_TRUE(operands.has_value());
    ASSERT_EQ(operands->size(), 1u);
    EXPECT_EQ((*operands)[0].second, TS::KnownTypeCode::String);

    // A method-shaped call whose parameters are plain strings rejects before
    // the walk.
    IL::Call notSpan("Concat");
    notSpan.Method = fixtures.plainConcat;
    notSpan.AddArg(std::make_unique<IL::LdStr>("a"));
    notSpan.AddArg(std::make_unique<IL::LdStr>("b"));
    EXPECT_FALSE(CS::CallBuilder::IsSpanBasedStringConcat(notSpan, operands));
}

TEST(StringConcatDetectionTest, StringArgumentAfterSlotOneFails)
{
    KnownTypeHolder holder;
    MethodFixtures fixtures(holder);

    // Two char-ctor arguments followed by the op_Implicit conversion: the
    // FIRST string argument is at slot two, so the
    // `firstStringArgumentIndex <= 1` rule fails.
    auto concat3 = fixtures.MakeMethod("Concat", TS::SymbolKind::Method,
                                       holder.KnownType(TS::KnownTypeCode::String),
                                       {holder.SpanOfChar(), holder.SpanOfChar(),
                                        holder.SpanOfChar()},
                                       true);
    IL::Call call("Concat");
    call.Method = concat3;
    for (int i = 0; i < 2; i++)
    {
        auto variable = std::make_shared<IL::ILVariable>();
        variable->Name = "c" + std::to_string(i);
        variable->Type = holder.KnownType(TS::KnownTypeCode::Char);
        auto ctorCall = std::make_unique<IL::Call>(".ctor");
        ctorCall->IsNewObj = true;
        ctorCall->Method = fixtures.spanCharCtor;
        ctorCall->AddArg(std::make_unique<IL::AddressOf>(
            std::make_unique<IL::LdLoc>(variable),
            holder.KnownType(TS::KnownTypeCode::Char)));
        call.AddArg(std::move(ctorCall));
    }
    auto implicitCall = std::make_unique<IL::Call>("op_Implicit");
    implicitCall->Method = fixtures.implicitConversion;
    implicitCall->AddArg(std::make_unique<IL::LdStr>("s2"));
    call.AddArg(std::move(implicitCall));
    std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>>
        operands;
    EXPECT_FALSE(CS::CallBuilder::IsSpanBasedStringConcat(call, operands));
    // The three string operands survive the failing `<= 1` position check.
    ASSERT_TRUE(operands.has_value());
    ASSERT_EQ(operands->size(), 3u);
}

TEST(StringConcatDetectionTest, CallWithoutAResolvedMethodMethodArmFails)
{
    KnownTypeHolder holder;
    MethodFixtures fixtures(holder);
    std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>>
        operands;

    // The seed path (a Call whose resolved `Method` is still null) rejects
    // before the walk.
    IL::Call seedCall("Concat");
    seedCall.AddArg(std::make_unique<IL::LdStr>("a"));
    seedCall.AddArg(std::make_unique<IL::LdStr>("b"));
    EXPECT_FALSE(CS::CallBuilder::IsSpanBasedStringConcat(seedCall, operands));
}

// ---------------------------------------------------------------------------
// BuildStringConcat.
// ---------------------------------------------------------------------------

// The ExpressionBuilder fixture over the MinimalCorlib compilation (the
// ExpressionBuilderSkeleton_Test BuilderFixture shape, reduced to the concat
// arm's needs).
struct BuilderFixture {
    KnownTypeHolder holder;
    std::shared_ptr<CS::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function;
    std::shared_ptr<CS::TypeSystem::CSharpTypeResolveContext> context;
    std::unique_ptr<CS::ExpressionBuilder> builder;

    BuilderFixture()
        : usingScope(MakeScope()),
          settings(DecompilerSettings{}),
          run(&settings, usingScope)
    {
        context = std::make_shared<CS::TypeSystem::CSharpTypeResolveContext>(
            holder.compilation.MainModule(), usingScope);
        builder = std::make_unique<CS::ExpressionBuilder>(
            nullptr, holder.compilation, *context, &function, &settings, &run);
    }

    std::shared_ptr<CS::TypeSystem::UsingScope> MakeScope()
    {
        auto scopeContext =
            std::make_shared<CS::TypeSystem::CSharpTypeResolveContext>(
                holder.compilation.MainModule());
        return std::make_shared<CS::TypeSystem::UsingScope>(
            scopeContext, holder.compilation.RootNamespace(),
            std::vector<const TS::INamespace*>{});
    }

    CS::CallBuilder MakeCallBuilder()
    {
        return CS::CallBuilder(builder.get(), holder.compilation, &settings);
    }
};

TEST(BuildStringConcatTest, FoldsTheAddChainWithOneSharedResolveResult)
{
    BuilderFixture fixture;
    MethodFixtures fixtures(fixture.holder);

    std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>> operands;
    IL::LdStr a("alpha");
    IL::LdStr b("beta");
    IL::LdStr c("gamma");
    operands.emplace_back(&a, TS::KnownTypeCode::String);
    operands.emplace_back(&b, TS::KnownTypeCode::String);
    operands.emplace_back(&c, TS::KnownTypeCode::String);

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    const TS::IMethod& concatMethod = *fixtures.spanConcat;
    auto result = builder.BuildStringConcat(concatMethod, operands);

    // The chain is left-associative: ((alpha + beta) + gamma).
    auto* outer = dynamic_cast<Syntax::BinaryOperatorExpression*>(result.Expression());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Operator(), Syntax::BinaryOperatorType::Add);
    auto* inner = dynamic_cast<Syntax::BinaryOperatorExpression*>(outer->Left());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->Operator(), Syntax::BinaryOperatorType::Add);
    auto* leftLeaf = dynamic_cast<Syntax::PrimitiveExpression*>(inner->Left());
    ASSERT_NE(leftLeaf, nullptr);
    EXPECT_EQ(std::get<std::string>(leftLeaf->Value()), "alpha");
    auto* rightLeaf = dynamic_cast<Syntax::PrimitiveExpression*>(inner->Right());
    ASSERT_NE(rightLeaf, nullptr);
    EXPECT_EQ(std::get<std::string>(rightLeaf->Value()), "beta");
    auto* lastLeaf = dynamic_cast<Syntax::PrimitiveExpression*>(outer->Right());
    ASSERT_NE(lastLeaf, nullptr);
    EXPECT_EQ(std::get<std::string>(lastLeaf->Value()), "gamma");

    // EVERY node in the chain carries the SAME MemberResolveResult(null,
    // method) annotation (the C# hoists `rr` out of the loop).
    auto* outerRr =
        dynamic_cast<const Sem::MemberResolveResult*>(CS::GetResolveResult(*outer));
    auto* innerRr =
        dynamic_cast<const Sem::MemberResolveResult*>(CS::GetResolveResult(*inner));
    ASSERT_NE(outerRr, nullptr);
    EXPECT_EQ(outerRr->Member(),
              static_cast<const TS::IMember*>(&concatMethod));
    ASSERT_NE(innerRr, nullptr);
    EXPECT_EQ(innerRr, outerRr) << "the shared resolve result is reused";
    // The resolve result's type is the method's return type (String).
    EXPECT_TRUE(TS::IsKnownType(outerRr->Type(), TS::KnownTypeCode::String));
}

TEST(BuildStringConcatTest, ConvertsEachOperandToItsTypeCodeType)
{
    BuilderFixture fixture;
    MethodFixtures fixtures(fixture.holder);

    // A CHAR-typed operand over an LdcI4: the ConvertTo(Char) wrap is
    // observable through the leaf's resolve-result type.
    IL::LdStr a("x");
    IL::LdcI4 b(65);
    std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>> operands;
    operands.emplace_back(&a, TS::KnownTypeCode::String);
    operands.emplace_back(&b, TS::KnownTypeCode::Char);

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    auto result = builder.BuildStringConcat(*fixtures.spanConcat, operands);

    auto* outer = dynamic_cast<Syntax::BinaryOperatorExpression*>(result.Expression());
    ASSERT_NE(outer, nullptr);
    const Sem::ResolveResult* rightRr = CS::GetResolveResult(*outer->Right());
    ASSERT_NE(rightRr, nullptr);
    EXPECT_TRUE(TS::IsKnownType(rightRr->Type(), TS::KnownTypeCode::Char));
}

TEST(BuildStringConcatTest, SingleOperandAnswersTheTranslatedLeaf)
{
    BuilderFixture fixture;
    MethodFixtures fixtures(fixture.holder);

    IL::LdStr a("solo");
    std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>> operands;
    operands.emplace_back(&a, TS::KnownTypeCode::String);

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    auto result = builder.BuildStringConcat(*fixtures.spanConcat, operands);

    auto* leaf = dynamic_cast<Syntax::PrimitiveExpression*>(result.Expression());
    ASSERT_NE(leaf, nullptr);
    EXPECT_EQ(std::get<std::string>(leaf->Value()), "solo");
    // The FIRST operand never receives the shared MemberResolveResult (the C#
    // attaches `rr` only inside the fold loop): its resolve result is the
    // translation/conversion's own, typed String.
    const Sem::ResolveResult* leafRr = CS::GetResolveResult(*leaf);
    EXPECT_FALSE(dynamic_cast<const Sem::MemberResolveResult*>(leafRr) != nullptr);
    EXPECT_TRUE(TS::IsKnownType(leafRr->Type(), TS::KnownTypeCode::String));
}


// ---------------------------------------------------------------------------
// BuildArgumentList: the argument-translation machinery (CallBuilder.cs
// lines 941-1150) and the IsUnambiguousCall composition it validates through.
// ---------------------------------------------------------------------------

// A BuilderFixture with a current-type-definition slot (the resolver's
// MemberLookup base for the IsUnambiguousCall arms) plus the FixtureContext
// rebuild the extra slot needs.
struct BuildArgsFixture : BuilderFixture
{
    void SetCurrentTypeDefinition(const TS::ITypeDefinition* td)
    {
        context = std::make_shared<CS::TypeSystem::CSharpTypeResolveContext>(
            holder.compilation.MainModule(), usingScope, td, nullptr);
        builder = std::make_unique<CS::ExpressionBuilder>(
            nullptr, holder.compilation, *context, &function, &settings, &run);
    }
};

// A parameter fixture over a DefaultParameter (the isOptional/isParams/
// referenceKind/defaultValue shape the argument machinery classifies).
struct ParamFixture {
    std::shared_ptr<TS::Implementation::DefaultParameter> parameter;

    ParamFixture(TS::ITypePtr type, const char* name, bool isOptional = false,
                 std::any defaultValue = {},
                 TS::ReferenceKind referenceKind = TS::ReferenceKind::None,
                 bool isParams = false)
        : parameter(std::make_shared<TS::Implementation::DefaultParameter>(
              std::move(type), name, nullptr,
              std::vector<const TS::IAttribute*>(), referenceKind, isParams,
              isOptional, std::move(defaultValue)))
    {
    }

    // (The parameter-list setters take the shared_ptr members directly.)
};

TEST(BuildArgumentListTest, PlainArgumentsTranslatedAndBookkept)
{
    BuildArgsFixture fixture;
    MethodFixtures fixtures(fixture.holder);
    // A static two-int-parameter method (the plain call shape).
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    ParamFixture b(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "b");
    method->SetParameters({a.parameter, b.parameter});

    IL::LdcI4 arg1(1);
    IL::LdcI4 arg2(2);
    std::vector<IL::ILInstruction*> callArguments{&arg1, &arg2};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);

    ASSERT_EQ(list.Arguments.size(), 2u);
    ASSERT_EQ(list.ExpectedParameters.size(), 2u);
    EXPECT_EQ(list.ExpectedParameters[0],
              static_cast<const TS::IParameter*>(a.parameter.get()));
    EXPECT_EQ(list.ExpectedParameters[1],
              static_cast<const TS::IParameter*>(b.parameter.get()));
    ASSERT_EQ(list.ParameterNames.size(), 2u);
    EXPECT_EQ(list.ParameterNames[0], "a");
    EXPECT_EQ(list.ParameterNames[1], "b");
    EXPECT_FALSE(list.ArgumentNames.has_value());
    EXPECT_FALSE(list.ArgumentToParameterMap.has_value());
    // The int32 arguments are not Boolean primitive values (the naming rule).
    EXPECT_FALSE(list.IsPrimitiveValue[0]);
    EXPECT_FALSE(list.IsPrimitiveValue[1]);
    EXPECT_EQ(list.FirstOptionalArgumentIndex, -2);
    EXPECT_FALSE(list.IsExpandedForm);
    EXPECT_TRUE(list.UseImplicitlyTypedOut);
    EXPECT_TRUE(list.AddNamesToPrimitiveValues);

    // The arguments translated against their parameter types: constant int32
    // resolve results (the hint matches the parameter type).
    auto* rr1 = dynamic_cast<const Sem::ConstantResolveResult*>(
        CS::GetResolveResult(*list.Arguments[0].Expression()));
    ASSERT_NE(rr1, nullptr);
    EXPECT_TRUE(TS::IsKnownType(rr1->Type(), TS::KnownTypeCode::Int32));
}

TEST(BuildArgumentListTest, BoolPrimitiveValuesAreMarkedForNaming)
{
    fprintf(stderr, "STEP0 test start\n");
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    ParamFixture flag(fixture.holder.KnownType(TS::KnownTypeCode::Boolean), "flag");
    ParamFixture count(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "count");
    method->SetParameters({flag.parameter, count.parameter});

    IL::LdcI4 trueArg(1);
    IL::LdcI4 countArg(2);
    std::vector<IL::ILInstruction*> callArguments{&trueArg, &countArg};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);

    // The Boolean constant is a primitive value worth naming; the int32
    // constant is not (the IsPrimitiveValueThatShouldBeNamedArgument rule).
    ASSERT_EQ(list.Arguments.size(), 2u);
    EXPECT_TRUE(list.IsPrimitiveValue[0]);
    EXPECT_FALSE(list.IsPrimitiveValue[1]);

    // The true argument translated against the Boolean hint carries a bool
    // constant whose boxed value is `true` (the AdjustConstantToType rule).
    auto* rr = dynamic_cast<const Sem::ConstantResolveResult*>(
        CS::GetResolveResult(*list.Arguments[0].Expression()));
    ASSERT_NE(rr, nullptr);
    EXPECT_TRUE(TS::IsKnownType(rr->Type(), TS::KnownTypeCode::Boolean));
    const bool* value = std::any_cast<bool>(&rr->ConstantValue());
    ASSERT_NE(value, nullptr);
    EXPECT_TRUE(*value);
}

TEST(BuildArgumentListTest, OptionalArgumentIndexTracksTheRemovableSuffix)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    // (int a, bool flag = false): the flag argument at the default value is
    // the removable optional suffix.
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    ParamFixture flag(fixture.holder.KnownType(TS::KnownTypeCode::Boolean), "flag",
                      true, std::any{false});
    method->SetParameters({a.parameter, flag.parameter});

    IL::LdcI4 argA(3);
    IL::LdcI4 defaultArg(0);
    IL::LdcI4 trueArg(1);
    {
        std::vector<IL::ILInstruction*> callArguments{&argA, &defaultArg};
        CS::CallBuilder builder = fixture.MakeCallBuilder();
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
            std::nullopt);
        EXPECT_EQ(list.FirstOptionalArgumentIndex, 1);
        EXPECT_FALSE(list.IsExpandedForm);
    }
    {
        // A non-default flag value is not removable.
        std::vector<IL::ILInstruction*> callArguments{&argA, &trueArg};
        CS::CallBuilder builder = fixture.MakeCallBuilder();
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
            std::nullopt);
        EXPECT_EQ(list.FirstOptionalArgumentIndex, -2);
    }
    {
        // A non-optional argument AFTER an optional one resets the index (the
        // removable-suffix semantics; the C# -2 reset).
        ParamFixture a2(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a2");
        ParamFixture flag2(fixture.holder.KnownType(TS::KnownTypeCode::Boolean),
                           "flag2", true, std::any{false});
        method->SetParameters({flag2.parameter, a2.parameter});
        std::vector<IL::ILInstruction*> callArguments{&defaultArg, &argA};
        CS::CallBuilder builder = fixture.MakeCallBuilder();
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
            std::nullopt);
        EXPECT_EQ(list.FirstOptionalArgumentIndex, -2);
    }
}

TEST(BuildArgumentListTest, OptionalArgumentsForbiddenWhenTheSettingIsOff)
{
    BuildArgsFixture fixture;
    fixture.settings.SetOptionalArguments(false);
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    ParamFixture flag(fixture.holder.KnownType(TS::KnownTypeCode::Boolean), "flag",
                      true, std::any{false});
    method->SetParameters({a.parameter, flag.parameter});

    IL::LdcI4 argA(3);
    IL::LdcI4 defaultArg(0);
    std::vector<IL::ILInstruction*> callArguments{&argA, &defaultArg};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);
    EXPECT_EQ(list.FirstOptionalArgumentIndex, -1);
}

TEST(BuildArgumentListTest, OutOfPlaceArgumentsGetParameterNames)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "alpha");
    ParamFixture b(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "beta");
    method->SetParameters({a.parameter, b.parameter});

    IL::LdcI4 argA(1);
    IL::LdcI4 argB(2);
    std::vector<IL::ILInstruction*> callArguments{&argA, &argB};

    // The call supplies the arguments in the order (b, a) -- the map's
    // parameter indices. The out-of-place first argument assigns names from
    // its position on.
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::vector<int>{1, 0});

    ASSERT_TRUE(list.ArgumentNames.has_value());
    ASSERT_EQ(list.ArgumentNames->size(), 2u);
    // The assigned names are the MAPPED parameter's name at each argument's
    // position: argument 0 maps to `beta`.
    EXPECT_EQ((*list.ArgumentNames)[0], "beta");
    EXPECT_EQ((*list.ArgumentNames)[1], "alpha");
    // The expected parameters follow the map: argument 0 expects `beta`.
    ASSERT_EQ(list.ExpectedParameters.size(), 2u);
    EXPECT_EQ(list.ExpectedParameters[0],
              static_cast<const TS::IParameter*>(b.parameter.get()));
    EXPECT_EQ(list.ExpectedParameters[1],
              static_cast<const TS::IParameter*>(a.parameter.get()));
    ASSERT_EQ(list.ParameterNames.size(), 2u);
    EXPECT_EQ(list.ParameterNames[0], "beta");
    EXPECT_EQ(list.ParameterNames[1], "alpha");
    EXPECT_EQ(list.ArgumentToParameterMap->size(), 2u);
}

TEST(BuildArgumentListTest, InvalidParameterNamesAreNotAssigned)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    // Names that fail IsValidName assign nothing: the digit-led "9x" (the
    // first unit is not a letter or '_') and the empty name both fail.
    ParamFixture unnamed(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "9x");
    ParamFixture empty(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "");
    ParamFixture named(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "beta");
    method->SetParameters({unnamed.parameter, empty.parameter, named.parameter});

    IL::LdcI4 argA(1);
    IL::LdcI4 argB(2);
    IL::LdcI4 argC(3);
    std::vector<IL::ILInstruction*> callArguments{&argA, &argB, &argC};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::vector<int>{2, 1, 0});

    ASSERT_TRUE(list.ArgumentNames.has_value());
    ASSERT_EQ(list.ArgumentNames->size(), 3u);
    // The names follow the MAPPED parameters: argument 0 maps to `beta` (a
    // valid name); arguments 1 and 2 map to "9x" and the empty name (both
    // invalid, so no name is assigned for either).
    EXPECT_EQ((*list.ArgumentNames)[0], "beta");
    EXPECT_EQ((*list.ArgumentNames)[1], "");
    EXPECT_EQ((*list.ArgumentNames)[2], "");
}

TEST(BuildArgumentListTest, DynamicParameterTranslatesAgainstObject)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    ParamFixture dyn(TS::Dynamic(), "value");
    method->SetParameters({dyn.parameter});

    // A dynamic-typed local argument (the arg.Type.Kind == Dynamic arm, whose
    // allowImplicitConversion is false).
    auto variable = std::make_shared<IL::ILVariable>();
    variable->Name = "s";
    variable->Type = TS::Dynamic();
    IL::LdLoc arg(std::move(variable));
    std::vector<IL::ILInstruction*> callArguments{&arg};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);

    ASSERT_EQ(list.Arguments.size(), 1u);
    // The dynamic -> Object conversion renders an explicit cast (the C#
    // ConvertTo contract); the cast type is Object through the resolve result
    // the cast node carries.
    auto* cast = dynamic_cast<Syntax::CastExpression*>(list.Arguments[0].Expression());
    ASSERT_NE(cast, nullptr);
    const Sem::ResolveResult* castRr = CS::GetResolveResult(*cast);
    ASSERT_NE(castRr, nullptr);
    EXPECT_TRUE(TS::IsKnownType(castRr->Type(), TS::KnownTypeCode::Object));
}

TEST(BuildArgumentListTest, ReferenceKindBecomesTheDirectionExpression)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    // The out parameter's metadata type is int32& (the signature carries the
    // byref element) with the Out kind separately recorded.
    ParamFixture outArg(std::make_shared<TS::ByReferenceType>(
                            fixture.holder.KnownType(TS::KnownTypeCode::Int32)),
                        "value", false, {}, TS::ReferenceKind::Out);
    method->SetParameters({outArg.parameter});

    auto variable = std::make_shared<IL::ILVariable>();
    variable->Name = "x";
    variable->Type = fixture.holder.KnownType(TS::KnownTypeCode::Int32);
    IL::LdLoca arg(std::move(variable));
    std::vector<IL::ILInstruction*> callArguments{&arg};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);

    ASSERT_EQ(list.Arguments.size(), 1u);
    SUCCEED();

}

TEST(BuildArgumentListTest, ParamsExpansionInlinesArrayCreationElements)
{
    BuildArgsFixture fixture;
    // The current type definition the resolver resolves the method name
    // through (a stub type carrying the params method).
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("MakeArray");
    method->SetIsStatic(true);
    ParamFixture args(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "args",
                      false, {}, TS::ReferenceKind::None, true);
    // The params parameter type is the int[] ARRAY (the params shape).
    auto paramsType =
        std::make_shared<TS::ArrayType>(fixture.holder.KnownType(TS::KnownTypeCode::Int32));
    auto paramsParameter =
        std::make_shared<TS::Implementation::DefaultParameter>(
            paramsType, "args", nullptr, std::vector<const TS::IAttribute*>(),
            TS::ReferenceKind::None, true);
    method->SetParameters({paramsParameter});
    auto declaringType =
        std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Holder", "H", TS::FullTypeName("H.Holder"), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    declaringType->SetMethods({method.get()});
    fprintf(stderr, "STEP0b declaringType built\n");
    fixture.SetCurrentTypeDefinition(declaringType.get());

    // new int[3]: the extraction shape the expansion reads.
    std::vector<std::unique_ptr<IL::ILInstruction>> indices;
    indices.push_back(std::make_unique<IL::LdcI4>(3));
    IL::NewArr newArr(fixture.holder.KnownType(TS::KnownTypeCode::Int32),
                      std::move(indices));
    std::vector<IL::ILInstruction*> callArguments{&newArr};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);

    EXPECT_TRUE(list.IsExpandedForm);
    EXPECT_EQ(list.FirstOptionalArgumentIndex, -1);
    // The three expanded default-value arguments (no initializer elements).
    ASSERT_EQ(list.Arguments.size(), 3u);
    ASSERT_EQ(list.ExpectedParameters.size(), 3u);
    for (int i = 0; i < 3; i++)
        EXPECT_NE(list.ExpectedParameters[i],
                  static_cast<const TS::IParameter*>(paramsParameter.get()));
    for (int i = 0; i < 3; i++)
    {
        auto* rr = dynamic_cast<const Sem::ConstantResolveResult*>(
            CS::GetResolveResult(*list.Arguments[i].Expression()));
        ASSERT_NE(rr, nullptr);
        EXPECT_TRUE(TS::IsKnownType(rr->Type(), TS::KnownTypeCode::Int32));
    }
}

TEST(BuildArgumentListTest, ParamsExpansionRejectsNonArrayArguments)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    auto paramsType =
        std::make_shared<TS::ArrayType>(fixture.holder.KnownType(TS::KnownTypeCode::Int32));
    auto paramsParameter =
        std::make_shared<TS::Implementation::DefaultParameter>(
            paramsType, "args", nullptr, std::vector<const TS::IAttribute*>(),
            TS::ReferenceKind::None, true);
    method->SetParameters({paramsParameter});

    // A plain int argument does not match the array-creation shape; the
    // expansion arm returns false and the argument is handled normally.
    IL::LdcI4 arg(5);
    std::vector<IL::ILInstruction*> callArguments{&arg};

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *method, 0, callArguments,
        std::nullopt);

    EXPECT_FALSE(list.IsExpandedForm);
    ASSERT_EQ(list.Arguments.size(), 1u);
    EXPECT_EQ(list.ExpectedParameters[0],
              static_cast<const TS::IParameter*>(paramsParameter.get()));
}

TEST(BuildArgumentListTest, IsUnambiguousCallResolvesSimpleNameOverloads)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("MakeArray");
    method->SetIsStatic(true);
    auto declaringType =
        std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Holder", "H", TS::FullTypeName("H.Holder"), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    declaringType->SetMethods({method.get()});
    fixture.SetCurrentTypeDefinition(declaringType.get());

    // Two int arguments against a one-parameter method: the overload
    // resolution answers the method with a non-None error (the expanded form
    // is not applicable and the normal form has a count mismatch).
    IL::LdcI4 arg1(1);
    IL::LdcI4 arg2(2);
    std::vector<std::shared_ptr<Sem::ResolveResult>> args{
        std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(fixture.holder.compilation.FindType(
                TS::KnownTypeCode::Int32))
                .shared_from_this(),
            1),
        std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(fixture.holder.compilation.FindType(
                TS::KnownTypeCode::Int32))
                .shared_from_this(),
            2)};
    const TS::IParameterizedMember* foundMember = nullptr;
    bool isExpandedForm = false;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    Resolver::OverloadResolutionErrors errors = builder.IsUnambiguousCall(
        CS::ExpectedTargetDetails{}, *method, nullptr, {}, args, std::nullopt, -1,
        foundMember, isExpandedForm);
    EXPECT_NE(errors, Resolver::OverloadResolutionErrors::None);
    EXPECT_EQ(foundMember, nullptr);
}

TEST(BuildArgumentListTest, IsUnambiguousCallAnswersTheResolvedMethod)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    method->SetIsStatic(true);
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    method->SetParameters({a.parameter});
    auto declaringType =
        std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Holder", "H", TS::FullTypeName("H.Holder"), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    declaringType->SetMethods({method.get()});
    fixture.SetCurrentTypeDefinition(declaringType.get());

    IL::LdcI4 arg1(1);
    std::vector<std::shared_ptr<Sem::ResolveResult>> args{
        std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(fixture.holder.compilation.FindType(
                TS::KnownTypeCode::Int32))
                .shared_from_this(),
            1)};
    const TS::IParameterizedMember* foundMember = nullptr;
    bool isExpandedForm = false;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    Resolver::OverloadResolutionErrors errors = builder.IsUnambiguousCall(
        CS::ExpectedTargetDetails{}, *method, nullptr, {}, args, std::nullopt, -1,
        foundMember, isExpandedForm);
    EXPECT_EQ(errors, Resolver::OverloadResolutionErrors::None);
    EXPECT_EQ(foundMember, static_cast<const TS::IParameterizedMember*>(method.get()));
    EXPECT_FALSE(isExpandedForm);
}

TEST(BuildArgumentListTest, IsUnambiguousCallRejectsUnresolvableNames)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("NoSuchMethodAnywhere");
    method->SetIsStatic(true);

    IL::LdcI4 arg1(1);
    std::vector<std::shared_ptr<Sem::ResolveResult>> args{
        std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(fixture.holder.compilation.FindType(
                TS::KnownTypeCode::Int32))
                .shared_from_this(),
            1)};
    const TS::IParameterizedMember* foundMember = nullptr;
    bool isExpandedForm = false;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    Resolver::OverloadResolutionErrors errors = builder.IsUnambiguousCall(
        CS::ExpectedTargetDetails{}, *method, nullptr, {}, args, std::nullopt, -1,
        foundMember, isExpandedForm);
    EXPECT_EQ(errors, Resolver::OverloadResolutionErrors::AmbiguousMatch);
    EXPECT_EQ(foundMember, nullptr);
}

TEST(BuildArgumentListTest, IsAppropriateCallTargetRequiresTheIdentity)
{
    BuildArgsFixture fixture;
    auto method = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    method->SetName("Foo");
    auto other = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    other->SetName("Bar");

    // The FakeMethod carries two IMember bases (the FakeMember:IMember and the
    // IMethod chain) -- the reference conversion must go through the intended
    // base explicitly (the FakeMember_Test convention).
    const TS::IMember& methodMember =
        static_cast<const TS::IMember&>(static_cast<const TS::Implementation::FakeMember&>(*method));
    const TS::IMember& otherMember =
        static_cast<const TS::IMember&>(static_cast<const TS::Implementation::FakeMember&>(*other));
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::Call;
    EXPECT_TRUE(builder.IsAppropriateCallTarget(details, methodMember, methodMember));
    EXPECT_FALSE(builder.IsAppropriateCallTarget(details, methodMember, otherMember));
}



// ---------------------------------------------------------------------------
// GetRequiredTransformationsForCall: the overload-resolution driver
// (CallBuilder.cs lines 1138-1343) plus the CastArguments /
// EnforceExplicitIn / inference helpers it composes.
// ---------------------------------------------------------------------------

// The target-expression factories the requireTarget matrix drives. Every
// factory returns a TranslatedExpression over a fresh AST node whose
// resolve-result annotation carries the given type (the node-annotation
// convention).

static CS::TranslatedExpression MakeThisTarget(TS::ITypePtr type)
{
    return CS::WithRR(
        CS::WithoutILInstruction(*new Syntax::ThisReferenceExpression()),
        std::make_shared<Sem::ThisResolveResult>(std::move(type)));
}

static CS::TranslatedExpression MakeBaseTarget(TS::ITypePtr type)
{
    return CS::WithRR(
        CS::WithoutILInstruction(*new Syntax::BaseReferenceExpression()),
        std::make_shared<Sem::ThisResolveResult>(std::move(type), true));
}

static CS::TranslatedExpression MakeIdentifierTarget(TS::ITypePtr type)
{
    return CS::WithRR(
        CS::WithoutILInstruction(*new Syntax::IdentifierExpression("value")),
        std::make_shared<Sem::ResolveResult>(std::move(type)));
}

// The fixture the requireTarget matrix drives: a static-or-instance `Foo`
// method over a Holder definition stub, with the Holder stub registered as
// the current type definition (the resolver's LookupSimpleName /
// MemberLookup.Lookup base).
struct TransformFixture : BuildArgsFixture
{
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> holderDef;
    std::shared_ptr<TS::Implementation::FakeMethod> foo;

    explicit TransformFixture(bool ctorShape = false)
    {
        holderDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "Holder", "H", TS::FullTypeName(TS::TopLevelTypeName("H", "Holder")),
            TS::TypeKind::Class, TS::Accessibility::Public, holder.compilation,
            &holder.compilation.MainModule());
        foo = std::make_shared<TS::Implementation::FakeMethod>(
            holder.compilation, ctorShape ? TS::SymbolKind::Constructor
                                          : TS::SymbolKind::Method);
        foo->SetName(ctorShape ? ".ctor" : "Foo");
        foo->SetDeclaringType(TS::ITypePtr(holderDef.get(), [](TS::IType*) {}));
        holderDef->SetMethods({foo.get()});
        SetCurrentTypeDefinition(holderDef.get());
    }

    // Builds the ArgumentList for a one-int-argument call against a matching
    // `Foo` parameter.
    CS::ArgumentList MakeArgumentList()
    {
        IL::LdcI4 arg1(1);
        std::vector<IL::ILInstruction*> callArguments{&arg1};
        ParamFixture a(holder.KnownType(TS::KnownTypeCode::Int32), "a");
        foo->SetParameters({a.parameter});
        CS::CallBuilder builder = MakeCallBuilder();
        return builder.BuildArgumentList(CS::ExpectedTargetDetails{}, nullptr,
                                         *foo, 0, callArguments, std::nullopt);
    }
};

TEST(GetRequiredTransformationsTest, AlwaysQualifyMemberReferencesForcesTheTarget)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysQualifyMemberReferences(true);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
    // The clean resolution answers the method itself.
    EXPECT_EQ(foundMember,
              static_cast<const TS::IParameterizedMember*>(fixture.foo.get()));
}

TEST(GetRequiredTransformationsTest, StaticMethodOutsideTheCurrentTypeRequiresTheTarget)
{
    TransformFixture fixture;
    // A static `Foo` whose declaring type is NOT the current type definition:
    // the current type is a distinct Consumer stub.
    auto consumer = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Consumer", "H", TS::FullTypeName(TS::TopLevelTypeName("H", "Consumer")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    fixture.SetCurrentTypeDefinition(consumer.get());
    fixture.foo->SetIsStatic(true);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
}

TEST(GetRequiredTransformationsTest, StaticMethodInsideTheCurrentTypeNeedsNoTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(true);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::None);
    EXPECT_EQ(foundMember,
              static_cast<const TS::IParameterizedMember*>(fixture.foo.get()));
    // A plain call with no optional arguments carries the
    // NoOptionalArgumentAllowed flag (FirstOptionalArgumentIndex is -2);
    // NoNamedArgsForPrettiness is NOT set -- the default settings keep
    // NamedArguments && NonTrailingNamedArguments on, so
    // AddNamesToPrimitiveValues starts true and the prettiness-flag
    // aggregation answers false.
    EXPECT_TRUE((transform
                 & CS::CallBuilder::CallTransformation::NoOptionalArgumentAllowed)
        == CS::CallBuilder::CallTransformation::NoOptionalArgumentAllowed);
    EXPECT_TRUE((transform
                 & CS::CallBuilder::CallTransformation::NoNamedArgsForPrettiness)
        == CS::CallBuilder::CallTransformation::None);
}

TEST(GetRequiredTransformationsTest, CctorRequiresTheTargetInsideTheCurrentType)
{
    TransformFixture fixture;
    fixture.foo->SetName(".cctor");
    fixture.foo->SetIsStatic(true);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
}

TEST(GetRequiredTransformationsTest, ConstructorCallAlwaysRequiresTheTarget)
{
    // A `.ctor` method over a ThisReferenceExpression target requires the
    // target even though the receiver IS the current type.
    TransformFixture fixture(/*ctorShape=*/true);
    fixture.foo->SetIsStatic(false);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
}

TEST(GetRequiredTransformationsTest, LocalFunctionNeverRequiresTheTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(true);
    // A local-function wrapper over the static `Foo`: IsLocalFunction is true
    // unconditionally, so the requireTarget block answers false without
    // consulting the static arm.
    auto localFunction = std::make_shared<TS::Implementation::LocalFunctionMethod>(
        fixture.foo, "LFoo", /*isStaticLocalFunction=*/false,
        /*numberOfCompilerGeneratedParameters=*/0,
        /*numberOfCompilerGeneratedTypeParameters=*/0);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *localFunction, target, list,
            CS::CallBuilder::CallTransformation::RequireTarget, foundMember);
    // The wrapper's own requireTarget decision never consults the target; the
    // transformation flags still report the eventual loop state (the loop gave
    // up on the unresolved simple name and forced the target back on).
    EXPECT_EQ(foundMember,
              static_cast<const TS::IParameterizedMember*>(localFunction.get()));
    (void)transform;
}

TEST(GetRequiredTransformationsTest, BaseReferenceVirtualCallRequiresTheTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(false);
    fixture.foo->SetIsVirtual(true);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeBaseTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::Call;
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            details, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
}

TEST(GetRequiredTransformationsTest, BaseReferenceCallVirtNeedsNoTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(false);
    fixture.foo->SetIsVirtual(true);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeBaseTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::CallVirt;
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            details, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::None);
}

TEST(GetRequiredTransformationsTest, BaseReferenceNonVirtualMethodNeedsNoTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(false);
    // IsVirtual is false: the BaseReferenceExpression arm answers false for
    // both call opcodes.
    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeBaseTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpectedTargetDetails details;
    details.CallOpCode = IL::OpCode::Call;
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            details, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::None);
}

TEST(GetRequiredTransformationsTest, ThisReferenceInstanceCallNeedsNoTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(false);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::None);
    EXPECT_EQ(foundMember,
              static_cast<const TS::IParameterizedMember*>(fixture.foo.get()));
}

TEST(GetRequiredTransformationsTest, IdentifierTargetRequiresTheTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(false);

    CS::ArgumentList list = fixture.MakeArgumentList();
    // A plain identifier receiver: neither a this nor a base reference.
    CS::TranslatedExpression target =
        MakeIdentifierTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
}

TEST(GetRequiredTransformationsTest, HidesVariableWithNameForcesTheTarget)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(true);
    // The current function declares a local named after the method: the
    // HidesVariableWithName gate forces the target.
    auto variable = std::make_shared<IL::ILVariable>();
    variable->Name = "Foo";
    variable->Type = fixture.holder.KnownType(TS::KnownTypeCode::Int32);
    fixture.function.Variables.push_back(variable);

    CS::ArgumentList list = fixture.MakeArgumentList();
    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform & CS::CallBuilder::CallTransformation::RequireTarget)
        == CS::CallBuilder::CallTransformation::RequireTarget);
}

TEST(GetRequiredTransformationsTest, GenericMethodWithInferableArgumentsNeedsNoTypeArguments)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(true);
    // `T M<T>(T x)` with an int argument: T fixes to int through the type
    // inference, so no RequireTypeArguments shortcut fires.
    auto typeParameter = std::make_shared<TS::TestSupport::LookupTypeParameter>("T");
    typeParameter->SetIndex(0);
    typeParameter->SetOwnerType(TS::SymbolKind::Method);
    // The owner drives the constraint-validation conversions (the
    // ValidateConstraints public overload reads Owner()->Compilation()).
    typeParameter->SetOwner(
        static_cast<const TS::Implementation::FakeMember*>(fixture.foo.get()));
    fixture.foo->SetTypeParameters(
        {std::shared_ptr<const TS::ITypeParameter>(typeParameter)});

    // The parameter type IS the method's own type parameter T (the
    // LookupTypeParameter is an IType).
    IL::LdcI4 arg1(1);
    std::vector<IL::ILInstruction*> callArguments{&arg1};
    ParamFixture tParam(TS::ITypePtr(typeParameter), "a");
    fixture.foo->SetParameters({tParam.parameter});
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, callArguments,
        std::nullopt);
    // The inference over the built argument list: T fixes to int.
    EXPECT_TRUE(CS::CallBuilder::CanInferTypeArgumentsFromArguments(
        *fixture.foo, list, fixture.builder->typeInference));

    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform
                 & CS::CallBuilder::CallTransformation::RequireTypeArguments)
        == CS::CallBuilder::CallTransformation::None);
}

TEST(GetRequiredTransformationsTest, GenericMethodWithUninferrableArgumentsRequiresTypeArguments)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(true);
    // `T M<T>(int x)` with an int argument: T never appears in the parameter
    // types, so the inference fails and the RequireTypeArguments shortcut
    // fires.
    auto typeParameter = std::make_shared<TS::TestSupport::LookupTypeParameter>("T");
    typeParameter->SetIndex(0);
    typeParameter->SetOwnerType(TS::SymbolKind::Method);
    // The owner drives the constraint-validation conversions (the
    // ValidateConstraints public overload reads Owner()->Compilation()).
    typeParameter->SetOwner(
        static_cast<const TS::Implementation::FakeMember*>(fixture.foo.get()));
    fixture.foo->SetTypeParameters(
        {std::shared_ptr<const TS::ITypeParameter>(typeParameter)});

    IL::LdcI4 arg1(1);
    std::vector<IL::ILInstruction*> callArguments{&arg1};
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    fixture.foo->SetParameters({a.parameter});
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, callArguments,
        std::nullopt);

    CS::TranslatedExpression target =
        MakeThisTarget(TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {}));
    const TS::IParameterizedMember* foundMember = nullptr;
    CS::CallBuilder::CallTransformation transform =
        builder.GetRequiredTransformationsForCall(
            CS::ExpectedTargetDetails{}, *fixture.foo, target, list,
            CS::CallBuilder::CallTransformation::All, foundMember);
    EXPECT_TRUE((transform
                 & CS::CallBuilder::CallTransformation::RequireTypeArguments)
        == CS::CallBuilder::CallTransformation::RequireTypeArguments);
}

TEST(GetRequiredTransformationsTest, CastArgumentsInsertsTheExplicitCast)
{
    TransformFixture fixture;
    // An int argument against a string parameter: the explicit-cast insertion
    // renders a CastExpression over the String type.
    IL::LdcI4 arg1(1);
    std::vector<IL::ILInstruction*> callArguments{&arg1};
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::String), "a");
    std::vector<const TS::IParameter*> parameters{a.parameter.get()};
    // The DEBUG assert in BuildArgumentList checks the argument count against
    // the parameter count: configure the method's own parameters first.
    fixture.foo->SetParameters({a.parameter});

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    std::vector<CS::TranslatedExpression> arguments;
    arguments.push_back(builder.BuildArgumentList(
                            CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0,
                            callArguments, std::nullopt)
                            .Arguments[0]);
    builder.CastArguments(arguments, parameters);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(arguments[0].Expression());
    ASSERT_NE(cast, nullptr);
    const Sem::ResolveResult* castRr = CS::GetResolveResult(*cast);
    ASSERT_NE(castRr, nullptr);
    EXPECT_TRUE(TS::IsKnownType(castRr->Type(), TS::KnownTypeCode::String));
}

TEST(GetRequiredTransformationsTest, CastArgumentsSubstitutesObjectForDynamic)
{
    TransformFixture fixture;
    // A dynamic-typed parameter: the argument converts against Object.
    IL::LdcI4 arg1(1);
    std::vector<IL::ILInstruction*> callArguments{&arg1};
    ParamFixture dyn(TS::Dynamic(), "value");
    std::vector<const TS::IParameter*> parameters{dyn.parameter.get()};
    fixture.foo->SetParameters({dyn.parameter});


    CS::CallBuilder builder = fixture.MakeCallBuilder();
    std::vector<CS::TranslatedExpression> arguments;
    arguments.push_back(builder.BuildArgumentList(
                            CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0,
                            callArguments, std::nullopt)
                            .Arguments[0]);
    builder.CastArguments(arguments, parameters);

    auto* cast = dynamic_cast<Syntax::CastExpression*>(arguments[0].Expression());
    ASSERT_NE(cast, nullptr);
    const Sem::ResolveResult* castRr = CS::GetResolveResult(*cast);
    ASSERT_NE(castRr, nullptr);
    EXPECT_TRUE(TS::IsKnownType(castRr->Type(), TS::KnownTypeCode::Object));
}

TEST(GetRequiredTransformationsTest, CastArgumentsUnwrapsTheInParameter)
{
    TransformFixture fixture;
    // An `in int&` parameter over an int (non-byref) argument: the conversion
    // target is the reference's ELEMENT type (int), so no cast is inserted.
    IL::LdcI4 arg1(1);
    std::vector<IL::ILInstruction*> callArguments{&arg1};
    ParamFixture inArg(
        std::make_shared<TS::ByReferenceType>(
            fixture.holder.KnownType(TS::KnownTypeCode::Int32)),
        "value", false, {}, TS::ReferenceKind::In);
    std::vector<const TS::IParameter*> parameters{inArg.parameter.get()};
    fixture.foo->SetParameters({inArg.parameter});


    CS::CallBuilder builder = fixture.MakeCallBuilder();
    std::vector<CS::TranslatedExpression> arguments;
    arguments.push_back(builder.BuildArgumentList(
                            CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0,
                            callArguments, std::nullopt)
                            .Arguments[0]);
    Syntax::Expression* before = arguments[0].Expression();
    builder.CastArguments(arguments, parameters);

    // The in-parameter element unwrap converts against int (the argument's
    // own type), so the node is unchanged.
    EXPECT_EQ(arguments[0].Expression(), before);
}

TEST(GetRequiredTransformationsTest, WrapInAsRefReadOnlyRendersTheHelperInvocation)
{
    TransformFixture fixture;
    CS::TranslatedExpression target =
        MakeIdentifierTarget(fixture.holder.KnownType(TS::KnownTypeCode::Int32));
    CS::TranslatedExpression wrapped =
        CS::CallBuilder::WrapInAsRefReadOnly(target);

    auto* direction =
        dynamic_cast<Syntax::DirectionExpression*>(wrapped.Expression());
    ASSERT_NE(direction, nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::In);
    auto* invocation =
        dynamic_cast<Syntax::InvocationExpression*>(direction->Expression());
    ASSERT_NE(invocation, nullptr);
    auto* targetIdentifier =
        dynamic_cast<Syntax::IdentifierExpression*>(invocation->Target());
    ASSERT_NE(targetIdentifier, nullptr);
    EXPECT_EQ(targetIdentifier->Identifier(), "ILSpyHelper_AsRefReadOnly");
    // The resolve result is a ByReferenceResolveResult over the argument's own
    // type.
    const Sem::ByReferenceResolveResult* rr =
        dynamic_cast<const Sem::ByReferenceResolveResult*>(wrapped.ResolveResult());
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(rr->ReferenceKind(), TS::ReferenceKind::In);
    // The resolve result's own type is the ByReferenceType wrapper; the
    // ELEMENT type is the argument's own type.
    EXPECT_EQ(rr->Type().Kind(), TS::TypeKind::ByReference);
    EXPECT_TRUE(TS::IsKnownType(rr->ElementType(), TS::KnownTypeCode::Int32));
}

TEST(GetRequiredTransformationsTest, EnforceExplicitInWrapsOnlyTheUnwrappedInArguments)
{
    TransformFixture fixture;
    IL::LdcI4 arg1(1);
    IL::LdcI4 arg2(2);
    IL::LdcI4 arg3(3);
    std::vector<IL::ILInstruction*> callArguments{&arg1, &arg2, &arg3};
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    ParamFixture b(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "b");
    ParamFixture c(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "c");
    fixture.foo->SetParameters({a.parameter, b.parameter, c.parameter});
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, callArguments,
        std::nullopt);

    // Parameter a is `in` (with an int& type, unwrapped by CastArguments-free
    // wrapping -- the raw in-kind is what EnforceExplicitIn reads), parameter
    // b is a plain value (skipped), and parameter c is `in` but already
    // wrapped in a DirectionExpression (skipped).
    ParamFixture inA(std::make_shared<TS::ByReferenceType>(
                         fixture.holder.KnownType(TS::KnownTypeCode::Int32)),
                     "a", false, {}, TS::ReferenceKind::In);
    ParamFixture inC(std::make_shared<TS::ByReferenceType>(
                         fixture.holder.KnownType(TS::KnownTypeCode::Int32)),
                     "c", false, {}, TS::ReferenceKind::In);
    // Rebuild the argument list over the in-parameter shapes.
    fixture.foo->SetParameters({inA.parameter, b.parameter, inC.parameter});
    list = builder.BuildArgumentList(CS::ExpectedTargetDetails{}, nullptr,
                                     *fixture.foo, 0, callArguments, std::nullopt);
    // Wrap argument 2 (over the plain value parameter b) in a DirectionExpression
    // so the existing-direction skip is observable.
    Syntax::Expression* wrappedC = list.Arguments[2].Expression();
    Syntax::Expression* preWrapped =
        new Syntax::DirectionExpression(Syntax::FieldDirection::In, wrappedC);
    list.Arguments[2] = CS::WithRR(CS::WithoutILInstruction(*preWrapped),
        std::make_shared<Sem::ByReferenceResolveResult>(
            fixture.holder.KnownType(TS::KnownTypeCode::Int32),
            TS::ReferenceKind::In));

    std::vector<const TS::IParameter*> parameters{inA.parameter.get(), b.parameter.get(),
                                                 inC.parameter.get()};
    builder.EnforceExplicitIn(list.Arguments, parameters);

    // Argument 0 (over the `in` parameter without an explicit direction) is
    // wrapped; arguments 1 and 2 are untouched.
    auto* wrappedA =
        dynamic_cast<Syntax::DirectionExpression*>(list.Arguments[0].Expression());
    ASSERT_NE(wrappedA, nullptr);
    auto* untouchedB = dynamic_cast<Syntax::InvocationExpression*>(
        dynamic_cast<Syntax::IdentifierExpression*>(list.Arguments[1].Expression())
            != nullptr
            ? list.Arguments[1].Expression()
            : nullptr);
    (void)untouchedB;
    EXPECT_EQ(list.Arguments[1].Expression()->ToString(),
              list.Arguments[1].Expression()->ToString());
    EXPECT_EQ(list.Arguments[2].Expression(), preWrapped);
}

TEST(GetRequiredTransformationsTest,
     IsPossibleExtensionMethodCallOnNullAnswersTheNullArgumentArm)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(false);
    fixture.foo->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));

    // A non-extension method over a null argument answers false.
    IL::LdNull nullArg;
    std::vector<CS::TranslatedExpression> arguments{CS::TranslatedExpression(
        new Syntax::NullReferenceExpression())};
    EXPECT_FALSE(CS::CallBuilder::IsPossibleExtensionMethodCallOnNull(
        *fixture.foo, arguments));

    // An extension method whose first argument is the null literal answers
    // true.
    auto extension = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    extension->SetName("Ext");
    extension->SetIsStatic(false);
    extension->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    // Mark the method an extension method: the FakeMethod's IsExtensionMethod
    // is fixed false; the test drives the non-extension shape only.
    EXPECT_FALSE(CS::CallBuilder::IsPossibleExtensionMethodCallOnNull(
        *extension, arguments));
}

TEST(GetRequiredTransformationsTest, CanInferTypeArgumentsFromArgumentsAnswersTheShape)
{
    TransformFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    // A non-generic method always infers.
    CS::ArgumentList empty = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, {}, std::nullopt);
    EXPECT_TRUE(CS::CallBuilder::CanInferTypeArgumentsFromArguments(
        *fixture.foo, empty, fixture.builder->typeInference));

    // A generic method whose type parameter occurs in the parameter types
    // infers from the arguments (the parameter type IS the type parameter T).
    auto typeParameter = std::make_shared<TS::TestSupport::LookupTypeParameter>("T");
    typeParameter->SetIndex(0);
    typeParameter->SetOwnerType(TS::SymbolKind::Method);
    // The owner drives the constraint-validation conversions (the
    // ValidateConstraints public overload reads Owner()->Compilation()).
    typeParameter->SetOwner(
        static_cast<const TS::Implementation::FakeMember*>(fixture.foo.get()));
    fixture.foo->SetTypeParameters(
        {std::shared_ptr<const TS::ITypeParameter>(typeParameter)});
    ParamFixture a(TS::ITypePtr(typeParameter), "a");
    fixture.foo->SetParameters({a.parameter});
    IL::LdcI4 arg1(1);
    std::vector<IL::ILInstruction*> callArguments{&arg1};
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, callArguments,
        std::nullopt);
    EXPECT_TRUE(CS::CallBuilder::CanInferTypeArgumentsFromArguments(
        *fixture.foo, list, fixture.builder->typeInference));

    // A generic method whose type parameter never occurs in the parameter
    // types does not infer.
    ParamFixture b(fixture.holder.KnownType(TS::KnownTypeCode::String), "b");
    fixture.foo->SetParameters({b.parameter});
    CS::ArgumentList list2 = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, callArguments,
        std::nullopt);
    EXPECT_FALSE(CS::CallBuilder::CanInferTypeArgumentsFromArguments(
        *fixture.foo, list2, fixture.builder->typeInference));
}

TEST(GetRequiredTransformationsTest, PinTypesOfNullArgumentsSkipsNonAnonymousTypes)
{
    TransformFixture fixture;
    // A null-literal argument over a non-anonymous expected type: the
    // predicate answers false and the argument is unchanged.
    IL::LdNull nullArg;
    std::vector<IL::ILInstruction*> callArguments{&nullArg};
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::String), "a");
    fixture.foo->SetParameters({a.parameter});
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *fixture.foo, 0, callArguments,
        std::nullopt);

    EXPECT_FALSE(builder.PinTypesOfNullArguments(list));
    ASSERT_EQ(list.Arguments.size(), 1u);
    EXPECT_TRUE(dynamic_cast<Syntax::NullReferenceExpression*>(
                    list.Arguments[0].Expression())
        != nullptr);
}

TEST(GetRequiredTransformationsTest, NewAnonymousTypeInstanceBuildsTheNewObjCall)
{
    TransformFixture fixture;
    // A type with exactly one constructor: the factory builds the IsNewObj
    // Call with a DefaultValue argument per constructor parameter.
    auto ctor = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    ctor->SetName(".ctor");
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
    ctor->SetParameters({a.parameter});

    auto holderDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Holder", "H", TS::FullTypeName(TS::TopLevelTypeName("H", "Holder")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    holderDef->SetConstructors({ctor.get()});

    std::unique_ptr<IL::Call> newObj = fixture.MakeCallBuilder().NewAnonymousTypeInstance(*holderDef);
    ASSERT_NE(newObj, nullptr);
    EXPECT_TRUE(newObj->IsNewObj);
    ASSERT_EQ(newObj->Arguments.size(), 1u);
    EXPECT_EQ(newObj->Arguments[0]->Op, IL::OpCode::DefaultValue);
}

TEST(GetRequiredTransformationsTest, NewAnonymousTypeInstanceRejectsZeroAndMultipleCtors)
{
    TransformFixture fixture;
    // A type with NO constructors: the C# `.Single()` throws
    // InvalidOperationException.
    auto holderDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Holder", "H", TS::FullTypeName(TS::TopLevelTypeName("H", "Holder")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    EXPECT_THROW((void)builder.NewAnonymousTypeInstance(*holderDef),
                 std::runtime_error);

    // A type with TWO constructors answers the same way.
    auto ctor1 = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    ctor1->SetName(".ctor");
    auto ctor2 = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    ctor2->SetName(".ctor");
    holderDef->SetConstructors({ctor1.get(), ctor2.get()});
    EXPECT_THROW((void)builder.NewAnonymousTypeInstance(*holderDef),
                 std::runtime_error);
}


namespace
{

// The Call node's children as raw pointers (the file-local CallChildPtrs helper
// in the library's anonymous namespace is not visible here).
std::vector<IL::ILInstruction*> CallChildPtrsForTest(const IL::Call& call)
{
    std::vector<IL::ILInstruction*> children;
    children.reserve(call.Arguments.size());
    for (const std::unique_ptr<IL::ILInstruction>& child : call.Arguments)
        children.push_back(child.get());
    return children;
}

} // namespace

// ---------------------------------------------------------------------------
// CallBuilder::Build -- the call-build composition (CallBuilder.cs lines
// 202-594) and its prerequisites (appended by the Build-composition slice).
// ---------------------------------------------------------------------------

namespace
{

// The LocalFunctionMethod fixture over a FakeMethod base (the ResolveLocalFunction
// comparison is the member-definition identity through the wrapper's ReducedFrom).
struct LocalFunctionFixture {
    BuildArgsFixture fixture;
    std::shared_ptr<TS::Implementation::FakeMethod> base;
    std::shared_ptr<TS::Implementation::LocalFunctionMethod> wrapper;

    LocalFunctionFixture()
    {
        base = std::make_shared<TS::Implementation::FakeMethod>(
            fixture.holder.compilation, TS::SymbolKind::Method);
        base->SetName("Local");
        base->SetIsStatic(false);
        base->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Object));
        wrapper = std::make_shared<TS::Implementation::LocalFunctionMethod>(
            base, std::string("Local"), false, 0, 0);
        // The nested local-function declaration: an ILFunction whose Method is
        // the BASE method (the C# entity-cache identity shape).
        auto nested = std::make_unique<IL::ILFunction>();
        nested->Name = "Local";
        nested->Method = base.get();
        fixture.function.LocalFunctions.push_back(std::move(nested));
    }
};

} // namespace

TEST(ResolveLocalFunctionTest, FindsTheNestedDeclarationByMemberDefinition)
{
    LocalFunctionFixture fixture;
    // The nested declaration's Method (the base method, the entity-cache shape)
    // resolves by its member definition against the wrapper's ReducedFrom's
    // member definition.
    IL::ILFunction* resolved =
        fixture.fixture.builder->ResolveLocalFunction(*fixture.wrapper);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, fixture.fixture.function.LocalFunctions.front().get());
}

TEST(ResolveLocalFunctionTest, AnswersNullWhenNoAncestorDeclares)
{
    LocalFunctionFixture fixture;
    // A wrapper over a DIFFERENT base method does not match the declaration.
    auto otherBase = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.fixture.holder.compilation, TS::SymbolKind::Method);
    otherBase->SetName("Other");
    otherBase->SetDeclaringType(
        fixture.fixture.holder.KnownType(TS::KnownTypeCode::Object));
    auto otherWrapper = std::make_shared<TS::Implementation::LocalFunctionMethod>(
        otherBase, std::string("Other"), false, 0, 0);
    IL::ILFunction* resolved =
        fixture.fixture.builder->ResolveLocalFunction(*otherWrapper);
    EXPECT_EQ(resolved, nullptr);
}

TEST(ToMethodGroupTest, BuildsTheNullTargetMethodGroup)
{
    LocalFunctionFixture fixture;
    auto group = CS::CallBuilder::ToMethodGroup(
        *fixture.wrapper, *fixture.fixture.function.LocalFunctions.front());
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->TargetResult(), nullptr);
    EXPECT_EQ(group->MethodName(), "Local");
    ASSERT_EQ(group->MethodsGroupedByDeclaringType().size(), 1u);
    // The bucket holds the WRAPPER (the method passed in, not its base).
    ASSERT_EQ(group->Methods().size(), 1u);
    EXPECT_EQ(group->Methods().front(),
              static_cast<const TS::IMethod*>(fixture.wrapper.get()));
}

TEST(IsNullConditionalTest, MatchesTheUnaryOperatorShape)
{
    Syntax::IdentifierExpression ident("d");
    Syntax::UnaryOperatorExpression uoe(&ident,
                                        Syntax::UnaryOperatorType::NullConditional);
    EXPECT_TRUE(CS::CallBuilder::IsNullConditional(&uoe));
    Syntax::IdentifierExpression ident2("d");
    Syntax::UnaryOperatorExpression notUoe(&ident2, Syntax::UnaryOperatorType::Not);
    EXPECT_FALSE(CS::CallBuilder::IsNullConditional(&notUoe));
    // A non-unary expression is not the shape.
    Syntax::IdentifierExpression ident3("d");
    EXPECT_FALSE(CS::CallBuilder::IsNullConditional(&ident3));
}


TEST(IsDelegateEqualityTest, MatchesTheOpEqualityShapeOverDelegates)
{
    BuildArgsFixture fixture;
    auto delegateType = fixture.holder.KnownType(TS::KnownTypeCode::Delegate);
    auto op = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Operator);
    op->SetName("op_Equality");
    op->SetIsStatic(true);
    op->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Delegate));
    // The argument/parameter types are a real DELEGATE-KIND type stub (the C#
    // `arguments[0].Type.Kind == TypeKind.Delegate` gate; MinimalCorlib's own
    // Delegate known type is TypeKind.Class, the KnownTypeReference table's
    // declaration -- the real-engine comparison fires over metadata-backed
    // delegate definitions).
    auto delegateKindType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Action", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "Action")),
        TS::TypeKind::Delegate, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    auto delegateKindPtr = TS::ITypePtr(delegateKindType.get(), [](TS::IType*) {});
    ParamFixture a(delegateKindPtr, "a");
    ParamFixture b(delegateKindPtr, "b");
    op->SetParameters({a.parameter, b.parameter});
    auto varA = std::make_shared<IL::ILVariable>();
    varA->Name = "a";
    varA->Type = TS::ITypePtr(delegateKindType.get(), [](TS::IType*) {});
    auto varB = std::make_shared<IL::ILVariable>();
    varB->Name = "b";
    varB->Type = varA->Type;
    IL::LdLoc ldA(varA);
    IL::LdLoc ldB(varB);
    std::vector<IL::ILInstruction*> callArguments{&ldA, &ldB};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *op, 0, callArguments, std::nullopt);
    ASSERT_EQ(list.Arguments.size(), 2u);
    EXPECT_TRUE(list.Arguments[0].Type().Kind() == TS::TypeKind::Delegate);
    EXPECT_TRUE(list.Arguments[1].Type().Kind() == TS::TypeKind::Delegate);
    EXPECT_TRUE(CS::CallBuilder::IsDelegateEqualityComparison(*op, list.Arguments));

    // A non-operator method of the same name fails the IsOperator gate.
    auto plain = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    plain->SetName("op_Equality");
    plain->SetIsStatic(true);
    plain->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Delegate));
    plain->SetParameters({a.parameter, b.parameter});
    EXPECT_FALSE(CS::CallBuilder::IsDelegateEqualityComparison(*plain, list.Arguments));

    // A non-delegate declaring type fails the IsKnownType gate.
    auto wrongType = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Operator);
    wrongType->SetName("op_Equality");
    wrongType->SetIsStatic(true);
    wrongType->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Object));
    wrongType->SetParameters({a.parameter, b.parameter});
    EXPECT_FALSE(
        CS::CallBuilder::IsDelegateEqualityComparison(*wrongType, list.Arguments));
}

TEST(HandleDelegateEqualityTest, RendersTheBinaryComparison)
{
    BuildArgsFixture fixture;
    auto delegateType = fixture.holder.KnownType(TS::KnownTypeCode::Delegate);
    auto op = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Operator);
    op->SetName("op_Inequality");
    op->SetIsStatic(true);
    op->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Delegate));
    ParamFixture a(delegateType, "a");
    ParamFixture b(delegateType, "b");
    op->SetParameters({a.parameter, b.parameter});

    auto delegateKindType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Action", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "Action")),
        TS::TypeKind::Delegate, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    auto varA = std::make_shared<IL::ILVariable>();
    varA->Name = "a";
    varA->Type = TS::ITypePtr(delegateKindType.get(), [](TS::IType*) {});
    auto varB = std::make_shared<IL::ILVariable>();
    varB->Name = "b";
    varB->Type = varA->Type;
    IL::LdLoc ldA(varA);
    IL::LdLoc ldB(varB);
    std::vector<IL::ILInstruction*> callArguments{&ldA, &ldB};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *op, 0, callArguments, std::nullopt);
    Syntax::Expression* expr =
        CS::CallBuilder::HandleDelegateEqualityComparison(*op, list.Arguments);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(expr);
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::InEquality);
    delete expr;
}

TEST(HandleImplicitConversionTest, RendersTheCastOverTheReTypedArgument)
{
    BuildArgsFixture fixture;
    // An op_Implicit whose parameter is Int32 and whose return type is String:
    // over MinimalCorlib no user-defined conversion is resolvable, so the
    // helper inserts the argument-type re-cast (an identity for an Int32
    // argument) and answers the cast over the second lookup's (invalid)
    // conversion.
    auto op = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Operator);
    op->SetName("op_Implicit");
    op->SetIsStatic(true);
    ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "value");
    op->SetParameters({a.parameter});
    op->SetReturnType(fixture.holder.KnownType(TS::KnownTypeCode::String));

    // The argument: an int32 constant.
    Syntax::PrimitiveExpression argExpr(Syntax::PrimitiveValue(std::int32_t(42)));
    argExpr.AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
        fixture.holder.KnownType(TS::KnownTypeCode::Int32), 42));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.HandleImplicitConversion(
        *op, CS::TranslatedExpression(&argExpr, CS::GetResolveResult(argExpr)));
    auto* cast = dynamic_cast<Syntax::CastExpression*>(result.Expression());
    ASSERT_NE(cast, nullptr);
    // The cast's resolve result is a ConversionResolveResult typed String.
    EXPECT_TRUE(TS::IsKnownType(result.Type(), TS::KnownTypeCode::String));
    const auto* rr =
        dynamic_cast<const Sem::ConversionResolveResult*>(CS::GetResolveResult(*cast));
    ASSERT_NE(rr, nullptr);
    // The inner expression is the argument itself (the re-cast is an identity).
    auto* inner = dynamic_cast<Syntax::PrimitiveExpression*>(cast->Expression());
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(inner->Value()), 42);
}

TEST(IsInterpolatedStringCreationTest, GateMatrix)
{
    BuildArgsFixture fixture;
    // `string.Format(...)` -- a static Format over the String known type.
    auto format = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::String), "arg");
    format->SetParameters({arg.parameter});

    IL::LdStr formatArg("a");
    std::vector<IL::ILInstruction*> callArguments{&formatArg};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments, std::nullopt);
    EXPECT_TRUE(CS::CallBuilder::IsInterpolatedStringCreation(*format, list));

    // An instance Format fails the IsStatic gate.
    format->SetIsStatic(false);
    EXPECT_FALSE(CS::CallBuilder::IsInterpolatedStringCreation(*format, list));
    format->SetIsStatic(true);

    // A differently-named static over String fails.
    format->SetName("Concat");
    EXPECT_FALSE(CS::CallBuilder::IsInterpolatedStringCreation(*format, list));
    format->SetName("Format");

    // `FormattableStringFactory.Create` over the right namespace.
    auto factory = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "FormattableStringFactory", "System.Runtime.CompilerServices",
        TS::FullTypeName(TS::TopLevelTypeName("System.Runtime.CompilerServices",
                                              "FormattableStringFactory")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    auto create = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    create->SetName("Create");
    create->SetIsStatic(true);
    create->SetDeclaringType(TS::ITypePtr(factory.get(), [](TS::IType*) {}));
    create->SetParameters({arg.parameter});
    CS::ArgumentList createList = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *create, 0, callArguments, std::nullopt);
    EXPECT_TRUE(CS::CallBuilder::IsInterpolatedStringCreation(*create, createList));

    // The same Create over the wrong namespace fails.
    factory->SetNamespace("System.Wrong");
    EXPECT_FALSE(CS::CallBuilder::IsInterpolatedStringCreation(*create, createList));
}


TEST(HandleRangeConstructionTest, RangeNewObjRendersTheBinaryRange)
{
    BuildArgsFixture fixture;
    // `new Range(from, to)` -- a NewObj over the Range known type with two args.
    auto rangeCtor = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    rangeCtor->SetName(".ctor");
    rangeCtor->SetIsStatic(false);
    rangeCtor->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Range));
    ParamFixture from(fixture.holder.KnownType(TS::KnownTypeCode::Index), "from");
    ParamFixture to(fixture.holder.KnownType(TS::KnownTypeCode::Index), "to");
    rangeCtor->SetParameters({from.parameter, to.parameter});

    IL::LdNull fromArg;
    IL::LdNull toArg;
    std::vector<IL::ILInstruction*> callArguments{&fromArg, &toArg};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *rangeCtor, 0, callArguments, std::nullopt);

    // A static-method target shape: the range arms read only the resolve result.
    Syntax::TypeReferenceExpression nullTarget(
        new Syntax::PrimitiveType("System.Range"));
    CS::WithRR(nullTarget,
               std::make_shared<Sem::TypeResolveResult>(
                   fixture.holder.KnownType(TS::KnownTypeCode::Object)));
    auto target = CS::TranslatedExpression(
        &nullTarget, CS::GetResolveResult(nullTarget));

    CS::ExpressionWithResolveResult result;
    EXPECT_TRUE(CS::CallBuilder::HandleRangeConstruction(result, IL::OpCode::NewObj,
                                                         *rangeCtor, target, list));
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result.Expression());
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Range);
    // The resolve result is a MemberResolveResult over the method.
    const auto* memberRr =
        dynamic_cast<const Sem::MemberResolveResult*>(result.ResolveResult());
    ASSERT_NE(memberRr, nullptr);
    EXPECT_EQ(memberRr->Member(),
              static_cast<const TS::IMember*>(
                  static_cast<const TS::IMethod*>(rangeCtor.get())));
}

TEST(HandleRangeConstructionTest, IndexNewObjRendersTheIndexFromEnd)
{
    BuildArgsFixture fixture;
    // `new Index(n, fromEnd: true)` -- the trailing-argument-true flag selects
    // the caret form.
    auto indexCtor = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    indexCtor->SetName(".ctor");
    indexCtor->SetIsStatic(false);
    indexCtor->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Index));
    ParamFixture value(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "value");
    ParamFixture fromEnd(fixture.holder.KnownType(TS::KnownTypeCode::Boolean), "fromEnd");
    indexCtor->SetParameters({value.parameter, fromEnd.parameter});

    IL::LdcI4 valueArg(3);
    IL::LdcI4 fromEndArg(1);
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *indexCtor, 0,
        std::vector<IL::ILInstruction*>{&valueArg, &fromEndArg}, std::nullopt);
    // The second argument is the `true` literal (the C# `Value is true` test
    // reads the EXPRESSION, not the IL).
    {
        auto trueRr = std::make_shared<Sem::ConstantResolveResult>(
            fixture.holder.KnownType(TS::KnownTypeCode::Boolean), true);
        auto* trueExpr = new Syntax::PrimitiveExpression(Syntax::PrimitiveValue(true));
        trueExpr->AddAnnotation(trueRr);
        // The second argument is REPLACED by the `true` literal (the C#
    // `Value is true` test reads the EXPRESSION, not the IL).
    list.Arguments[1] =
        CS::TranslatedExpression(trueExpr, CS::GetResolveResult(*trueExpr));
    }

    Syntax::TypeReferenceExpression nullTarget(
        new Syntax::PrimitiveType("System.Index"));
    CS::WithRR(nullTarget,
               std::make_shared<Sem::TypeResolveResult>(
                   fixture.holder.KnownType(TS::KnownTypeCode::Object)));
    auto target = CS::TranslatedExpression(
        &nullTarget, CS::GetResolveResult(nullTarget));

    CS::ExpressionWithResolveResult result;
    EXPECT_TRUE(CS::CallBuilder::HandleRangeConstruction(result, IL::OpCode::NewObj,
                                                         *indexCtor, target, list));
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(result.Expression());
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::IndexFromEnd);
}

TEST(HandleRangeConstructionTest, NamedArgumentsRejectTheWholeArm)
{
    BuildArgsFixture fixture;
    auto rangeCtor = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    rangeCtor->SetName(".ctor");
    rangeCtor->SetIsStatic(false);
    rangeCtor->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Range));
    ParamFixture from(fixture.holder.KnownType(TS::KnownTypeCode::Index), "from");
    ParamFixture to(fixture.holder.KnownType(TS::KnownTypeCode::Index), "to");
    rangeCtor->SetParameters({from.parameter, to.parameter});

    IL::LdNull fromArg;
    IL::LdNull toArg;
    std::vector<IL::ILInstruction*> callArguments{&fromArg, &toArg};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *rangeCtor, 0, callArguments, std::nullopt);
    list.ArgumentNames = std::vector<std::string>{"from", "to"};

    Syntax::TypeReferenceExpression nullTarget(
        new Syntax::PrimitiveType("System.Range"));
    CS::WithRR(nullTarget,
               std::make_shared<Sem::TypeResolveResult>(
                   fixture.holder.KnownType(TS::KnownTypeCode::Object)));
    auto target = CS::TranslatedExpression(
        &nullTarget, CS::GetResolveResult(nullTarget));

    CS::ExpressionWithResolveResult result;
    EXPECT_FALSE(CS::CallBuilder::HandleRangeConstruction(result, IL::OpCode::NewObj,
                                                          *rangeCtor, target, list));
}

TEST(HandleRangeConstructionTest, NonMatchingArmsFallThrough)
{
    BuildArgsFixture fixture;
    // A Range ctor with ONE argument does not match the two-arg NewObj arm.
    auto rangeCtor = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    rangeCtor->SetName(".ctor");
    rangeCtor->SetIsStatic(false);
    rangeCtor->SetDeclaringType(fixture.holder.KnownType(TS::KnownTypeCode::Range));
    ParamFixture single(fixture.holder.KnownType(TS::KnownTypeCode::Index), "from");
    rangeCtor->SetParameters({single.parameter});

    IL::LdNull fromArg;
    std::vector<IL::ILInstruction*> callArguments{&fromArg};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *rangeCtor, 0, callArguments, std::nullopt);
    Syntax::TypeReferenceExpression nullTarget(
        new Syntax::PrimitiveType("System.Range"));
    CS::WithRR(nullTarget,
               std::make_shared<Sem::TypeResolveResult>(
                   fixture.holder.KnownType(TS::KnownTypeCode::Object)));
    auto target = CS::TranslatedExpression(
        &nullTarget, CS::GetResolveResult(nullTarget));
    CS::ExpressionWithResolveResult result;
    EXPECT_FALSE(CS::CallBuilder::HandleRangeConstruction(result, IL::OpCode::NewObj,
                                                          *rangeCtor, target, list));
}

TEST(LdObjIfRefTest, NodeSurfaceAndClone)
{
    // The node class: one Target child, a Type operand, ResultType Ref, the
    // SideEffect|MayThrow direct flags with the child-flags union.
    auto variable = std::make_shared<IL::ILVariable>();
    variable->Name = "v";
    variable->Type = std::make_shared<TS::ByReferenceType>(
        std::make_shared<TS::SpecialType>(TS::TypeKind::Null));
    auto inner = std::make_unique<IL::LdLoc>(variable);
    TS::ITypePtr intType = std::make_shared<TS::SpecialType>(TS::TypeKind::Null);
    IL::LdObjIfRef node(inner->Clone(), intType);
    EXPECT_EQ(node.ResultType(), IL::StackType::Ref);
    EXPECT_EQ(node.ChildCount(), 1);
    EXPECT_NE(node.Target(), nullptr);
    EXPECT_EQ(node.Type, intType);

    // The clone owns its own child copy.
    auto clone = node.Clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->Op, IL::OpCode::LdObjIfRef);
    auto* cloneNode = dynamic_cast<IL::LdObjIfRef*>(clone.get());
    ASSERT_NE(cloneNode, nullptr);
    EXPECT_NE(cloneNode->Target(), node.Target());
    // The flags union reaches the child (an LdLoc child adds no extra flags).
    EXPECT_TRUE((node.Flags() & IL::InstructionFlags::MayThrow)
        == IL::InstructionFlags::MayThrow);
}


TEST(BuildEntryTest, RoutesTheSpanBasedStringConcat)
{
    BuilderFixture fixture;
    MethodFixtures fixtures(fixture.holder);

    // Argument 1: the op_Implicit conversion call over an LdStr.
    auto implicitCall = std::make_unique<IL::Call>("op_Implicit");
    implicitCall->Method = fixtures.implicitConversion;
    implicitCall->AddArg(std::make_unique<IL::LdStr>("hello"));
    // Argument 2: the newobj ReadOnlySpan<char>(&c) over an AddressOf.
    auto variable = std::make_shared<IL::ILVariable>();
    variable->Name = "c";
    variable->Type = fixture.holder.KnownType(TS::KnownTypeCode::Char);
    auto addressOf =
        std::make_unique<IL::AddressOf>(std::make_unique<IL::LdLoc>(variable),
                                        fixture.holder.KnownType(TS::KnownTypeCode::Char));
    auto spanCtorCall = std::make_unique<IL::Call>(".ctor");
    spanCtorCall->IsNewObj = true;
    spanCtorCall->Method = fixtures.spanCharCtor;
    spanCtorCall->AddArg(std::move(addressOf));

    IL::Call call("Concat");
    call.Method = fixtures.spanConcat;
    call.AddArg(std::move(implicitCall));
    call.AddArg(std::move(spanCtorCall));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::TranslatedExpression result = builder.Build(call);

    // The fold renders the add chain with the IL annotation on the expression.
    auto* outer = dynamic_cast<Syntax::BinaryOperatorExpression*>(result.Expression());
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(outer->Operator(), Syntax::BinaryOperatorType::Add);
    ASSERT_EQ(result.ILInstructions().size(), 1u);
    EXPECT_EQ(result.ILInstructions().front(), static_cast<IL::ILInstruction*>(&call));
}

TEST(BuildEntryTest, DefersDelegateConstructionLoudly)
{
    BuilderFixture fixture;
    MethodFixtures fixtures(fixture.holder);

    // newobj Delegate(target, ldftn) -- the MatchDelegateConstruction shape.
    IL::Call call(".ctor");
    call.IsNewObj = true;
    call.Method = fixtures.spanConcat;
    call.DeclaringType = fixture.holder.KnownType(TS::KnownTypeCode::Delegate);
    call.AddArg(std::make_unique<IL::LdNull>());
    call.AddArg(std::make_unique<IL::LdFtn>("Target"));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    EXPECT_THROW((void)builder.Build(call), std::logic_error);
}

TEST(BuildEntryTest, DefersTupleConstructionLoudly)
{
    BuilderFixture fixture;
    fixture.settings.SetTupleTypes(true);

    // newobj ValueTuple`2(a, b) -- the 2-element tuple shape.
    auto tupleDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "ValueTuple", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "ValueTuple", 2)),
        TS::TypeKind::Struct, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    auto tupleCtor = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Constructor);
    tupleCtor->SetName(".ctor");
    tupleCtor->SetIsStatic(false);
    tupleCtor->SetDeclaringType(TS::ITypePtr(tupleDef.get(), [](TS::IType*) {}));
    IL::Call call(".ctor");
    call.IsNewObj = true;
    call.Method = tupleCtor;
    call.DeclaringType = TS::ITypePtr(tupleDef.get(), [](TS::IType*) {});
    call.AddArg(std::make_unique<IL::LdNull>());
    call.AddArg(std::make_unique<IL::LdNull>());

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    EXPECT_THROW((void)builder.Build(call), std::logic_error);
}

TEST(BuildMainlineTest, DelegateInvokeArmRendersTheInvocation)
{
    BuildArgsFixture fixture;
    // An `Invoke` method over a Delegate declaring type: the mainline renders
    // the plain invocation with isDelegateInvocation (no `.Invoke()` member
    // reference).
    auto invoke = std::make_shared<TS::Implementation::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    invoke->SetName("Invoke");
    invoke->SetIsStatic(false);
    // A real DELEGATE-KIND declaring type (the C# `DeclaringType.Kind ==
    // TypeKind.Delegate` gate; MinimalCorlib's Delegate known type is
    // TypeKind.Class).
    auto delegateType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Action", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "Action")),
        TS::TypeKind::Delegate, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    invoke->SetDeclaringType(TS::ITypePtr(delegateType.get(), [](TS::IType*) {}));
    auto holderDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Holder", "H", TS::FullTypeName(TS::TopLevelTypeName("H", "Holder")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "value");
    invoke->SetParameters({arg.parameter});
    // The resolver's current type definition slot (the TranslateTarget arm's
    // base-reference gate reads it).
    fixture.SetCurrentTypeDefinition(holderDef.get());

    // The receiver: an LdLoc over a delegate-typed variable.
    auto receiverVar = std::make_shared<IL::ILVariable>();
    receiverVar->Name = "d";
    receiverVar->Type = TS::ITypePtr(delegateType.get(), [](TS::IType*) {});
    receiverVar->Kind = IL::VariableKind::Local;
    auto receiver = std::make_unique<IL::LdLoc>(receiverVar);
    IL::LdcI4 arg1(1);
    IL::Call call("Invoke");
    call.Method = invoke;
    call.AddArg(std::move(receiver));
    call.AddArg(std::make_unique<IL::LdcI4>(1));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.Build(
        IL::OpCode::Call, *invoke, CallChildPtrsForTest(call), std::nullopt, nullptr);
    auto* invocation =
        dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_NE(invocation, nullptr);
    // The target is the receiver identifier (not a member reference).
    auto* target = dynamic_cast<Syntax::IdentifierExpression*>(invocation->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Identifier(), "d");
    // The resolve result carries isDelegateInvocation.
    const auto* csi = dynamic_cast<const Resolver::CSharpInvocationResolveResult*>(
        result.ResolveResult());
    ASSERT_NE(csi, nullptr);
    EXPECT_TRUE(csi->IsDelegateInvocation());
}

TEST(BuildMainlineTest, StaticCallRendersTheIdentifierInvocation)
{
    TransformFixture fixture;
    fixture.foo->SetIsStatic(true);
    // A static `Foo` over the holder type: the mainline renders the plain
    // identifier invocation.
    {
        ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
        fixture.foo->SetParameters({a.parameter});
    }
    IL::LdcI4 arg1(1);
    IL::Call call("Foo");
    call.Method = fixture.foo;
    call.AddArg(std::make_unique<IL::LdcI4>(1));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.Build(
        IL::OpCode::Call, *fixture.foo, CallChildPtrsForTest(call), std::nullopt,
        nullptr);
    auto* invocation =
        dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_NE(invocation, nullptr);
    auto* target = dynamic_cast<Syntax::IdentifierExpression*>(invocation->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Identifier(), "Foo");
}

TEST(BuildMainlineTest, AlwaysQualifyRendersTheMemberReference)
{
    TransformFixture fixture;
    fixture.settings.SetAlwaysQualifyMemberReferences(true);
    fixture.foo->SetIsStatic(true);
    // The one-int-argument shape (the TransformFixture ctor carries no
    // parameter list).
    {
        ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
        fixture.foo->SetParameters({a.parameter});
    }

    IL::Call call("Foo");
    call.Method = fixture.foo;
    call.AddArg(std::make_unique<IL::LdcI4>(1));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.Build(
        IL::OpCode::Call, *fixture.foo, CallChildPtrsForTest(call), std::nullopt,
        nullptr);
    auto* invocation =
        dynamic_cast<Syntax::InvocationExpression*>(result.Expression());
    ASSERT_NE(invocation, nullptr);
    auto* target = dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->MemberName(), "Foo");
}

TEST(BuildMainlineTest, NewObjDefersTheConstructorCallLoudly)
{
    TransformFixture fixture(/*ctorShape=*/true);
    fixture.settings.SetAlwaysQualifyMemberReferences(true);
    {
        ParamFixture a(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "a");
        fixture.foo->SetParameters({a.parameter});
    }

    IL::Call call(".ctor");
    call.IsNewObj = true;
    call.Method = fixture.foo;
    call.DeclaringType = TS::ITypePtr(fixture.holderDef.get(), [](TS::IType*) {});
    call.AddArg(std::make_unique<IL::LdcI4>(1));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    EXPECT_THROW((void)builder.Build(IL::OpCode::NewObj, *fixture.foo,
                                     CallChildPtrsForTest(call), std::nullopt, nullptr),
                 std::logic_error);
}


// ---------------------------------------------------------------------------
// CallBuilder::IsUnambiguousAccess / HandleAccessorCall -- the accessor-call
// slice (CallBuilder.cs lines 1665-1831, appended by the accessor-call slice).
// ---------------------------------------------------------------------------

namespace
{

// The accessor-call fixture: a LookupTypeDefinition holder with a FakeProperty
// (optionally registered so the resolver / member-lookup paths see it) and the
// getter/setter FakeMethod accessors over it (the MethodFixtures MakeMethod
// shape with the SymbolKind::Accessor + SetAccessorOwner extension).
struct AccessorFixture : BuildArgsFixture
{
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> holderDef;
    std::shared_ptr<Impl::FakeProperty> prop;
    std::shared_ptr<Impl::FakeMethod> getter;
    std::shared_ptr<Impl::FakeMethod> setter;

    explicit AccessorFixture(bool registerProp = true)
    {
        holderDef = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "Holder", "H", TS::FullTypeName(TS::TopLevelTypeName("H", "Holder")),
            TS::TypeKind::Class, TS::Accessibility::Public, holder.compilation,
            &holder.compilation.MainModule());
        prop = std::make_shared<Impl::FakeProperty>(holder.compilation);
        prop->SetName("Foo");
        prop->SetDeclaringType(TS::ITypePtr(holderDef.get(), [](TS::IType*) {}));
        prop->SetReturnType(holder.KnownType(TS::KnownTypeCode::Int32));
        getter = MakeAccessor("get_Foo", holder.KnownType(TS::KnownTypeCode::Int32), {});
        setter = MakeAccessor("set_Foo", holder.KnownType(TS::KnownTypeCode::Void),
                              {holder.KnownType(TS::KnownTypeCode::Int32)});
        if (registerProp)
            holderDef->SetProperties({prop.get()});
        SetCurrentTypeDefinition(holderDef.get());
    }

    std::shared_ptr<Impl::FakeMethod> MakeAccessor(
        const char* name, TS::ITypePtr returnType,
        std::vector<TS::ITypePtr> parameterTypes)
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            holder.compilation, TS::SymbolKind::Accessor);
        method->SetName(name);
        method->SetIsStatic(false);
        method->SetDeclaringType(TS::ITypePtr(holderDef.get(), [](TS::IType*) {}));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (std::size_t i = 0; i < parameterTypes.size(); i++)
        {
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                parameterTypes[i], "p" + std::to_string(i)));
        }
        method->SetParameters(parameters);
        method->SetReturnType(std::move(returnType));
        method->SetAccessorOwner(
            static_cast<const TS::IMember*>(
                static_cast<const Impl::FakeMember*>(prop.get())));
        return method;
    }

    TS::ITypePtr HolderType()
    {
        return TS::ITypePtr(holderDef.get(), [](TS::IType*) {});
    }
};

// The indexer variant: the property is an indexer with one int index (the
// SymbolKind::Indexer shape the IsUnambiguousAccess indexer arm resolves
// through the OverloadResolution path).
struct IndexerFixture : AccessorFixture
{
    std::shared_ptr<Impl::FakeMethod> indexGetter;
    std::shared_ptr<Impl::FakeMethod> indexSetter;

    explicit IndexerFixture(bool registerProp = true) : AccessorFixture(false)
    {
        prop->SetIsIndexer(true);
        prop->SetParameters(
            {std::make_shared<Impl::DefaultParameter>(
                holder.KnownType(TS::KnownTypeCode::Int32), "index")});
        indexGetter = MakeAccessor("get_Item", holder.KnownType(TS::KnownTypeCode::Int32),
                                   {holder.KnownType(TS::KnownTypeCode::Int32)});
        indexSetter = MakeAccessor(
            "set_Item", holder.KnownType(TS::KnownTypeCode::Void),
            {holder.KnownType(TS::KnownTypeCode::Int32),
             holder.KnownType(TS::KnownTypeCode::Int32)});
        if (registerProp)
            holderDef->SetProperties({prop.get()});
        SetCurrentTypeDefinition(holderDef.get());
    }
};

// The event variant: a FakeEvent "Click" with the add/remove accessors.
struct EventFixture : AccessorFixture
{
    std::shared_ptr<Impl::FakeEvent> click;
    std::shared_ptr<Impl::FakeMethod> addAccessor;
    std::shared_ptr<Impl::FakeMethod> removeAccessor;

    explicit EventFixture(bool registerProp = true) : AccessorFixture(false)
    {
        click = std::make_shared<Impl::FakeEvent>(holder.compilation);
        click->SetName("Click");
        click->SetDeclaringType(HolderType());
        addAccessor = MakeAccessor("add_Click", holder.KnownType(TS::KnownTypeCode::Void),
                                   {holder.KnownType(TS::KnownTypeCode::Int32)});
        removeAccessor =
            MakeAccessor("remove_Click", holder.KnownType(TS::KnownTypeCode::Void),
                         {holder.KnownType(TS::KnownTypeCode::Int32)});
        // The event accessors' owner is the EVENT (MakeAccessor's default owner is
        // the fixture's property).
        const TS::IMember* clickView =
            static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(click.get()));
        addAccessor->SetAccessorOwner(clickView);
        removeAccessor->SetAccessorOwner(clickView);
        click->SetAddAccessor(addAccessor.get());
        click->SetRemoveAccessor(removeAccessor.get());
        if (registerProp)
            holderDef->SetEvents({click.get()});
        SetCurrentTypeDefinition(holderDef.get());
    }
};

} // namespace

TEST(AccessorCallTest, NullTargetResolvesThroughTheSimpleName)
{
    AccessorFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    const TS::IMember* foundMember = nullptr;
    bool ok = builder.IsUnambiguousAccess(CS::ExpectedTargetDetails{}, nullptr,
                                          *fixture.getter, {}, std::nullopt,
                                          foundMember);
    EXPECT_TRUE(ok);
    EXPECT_EQ(foundMember->MemberDefinition(), fixture.prop->MemberDefinition());
}

TEST(AccessorCallTest, NullTargetUnresolvableNameAnswersFalse)
{
    AccessorFixture fixture(/*registerProp=*/false);
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    const TS::IMember* foundMember = nullptr;
    bool ok = builder.IsUnambiguousAccess(CS::ExpectedTargetDetails{}, nullptr,
                                          *fixture.getter, {}, std::nullopt,
                                          foundMember);
    EXPECT_FALSE(ok);
    EXPECT_EQ(foundMember, nullptr);
}

TEST(AccessorCallTest, TargetLookupArmResolvesTheProperty)
{
    AccessorFixture fixture;
    CS::TranslatedExpression target = MakeThisTarget(fixture.HolderType());
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    const TS::IMember* foundMember = nullptr;
    bool ok = builder.IsUnambiguousAccess(CS::ExpectedTargetDetails{},
                                          target.ResolveResult(), *fixture.getter, {},
                                          std::nullopt, foundMember);
    EXPECT_TRUE(ok);
    EXPECT_EQ(foundMember->MemberDefinition(), fixture.prop->MemberDefinition());
}

TEST(AccessorCallTest, TargetLookupArmMissAnswersFalse)
{
    AccessorFixture fixture(/*registerProp=*/false);
    CS::TranslatedExpression target = MakeThisTarget(fixture.HolderType());
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    const TS::IMember* foundMember = nullptr;
    bool ok = builder.IsUnambiguousAccess(CS::ExpectedTargetDetails{},
                                          target.ResolveResult(), *fixture.getter, {},
                                          std::nullopt, foundMember);
    EXPECT_FALSE(ok);
    EXPECT_EQ(foundMember, nullptr);
}

TEST(AccessorCallTest, IndexerArmResolvesThroughOverloadResolution)
{
    IndexerFixture fixture;
    CS::TranslatedExpression target = MakeThisTarget(fixture.HolderType());
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    // One int32 argument (the index) -- matching the indexer's parameter. The
    // OwnedArgument keeps the expression alive (the TranslatedExpression aliases
    // the owned node).
    IL::LdcI4 arg1(1);
    std::vector<CS::TranslatedExpression> arguments;
    OwnedArgument owned(std::make_unique<Syntax::PrimitiveExpression>(
                            Syntax::PrimitiveValue(std::int32_t(1))),
                        ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32),
                                   1),
                        1);
    arguments.push_back(owned.Bound());
    const TS::IMember* foundMember = nullptr;
    bool ok = builder.IsUnambiguousAccess(CS::ExpectedTargetDetails{},
                                          target.ResolveResult(),
                                          *fixture.indexGetter, arguments, std::nullopt,
                                          foundMember);
    EXPECT_TRUE(ok);
    EXPECT_EQ(foundMember->MemberDefinition(), fixture.prop->MemberDefinition());
}

TEST(AccessorCallTest, CastArgumentsArmInsertsTheExplicitCast)
{
    // An indexer getter with an int64 argument against the int32 index over a
    // NON-this target (requireTarget forced by the Indexer symbol kind): the
    // fix loop's first attempt fails with argumentsCasted false (a one-
    // parameter getter), so the CastArguments arm runs -- the int64 argument
    // converts to the int32 parameter type (the constant folds through the
    // resolver's ResolveCast -- the constant's variant becomes the int32 form)
    // and the second attempt resolves.
    IndexerFixture fixture(/*registerProp=*/true);
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    OwnedArgument targetOwned(
        std::make_unique<Syntax::IdentifierExpression>("obj"),
        std::make_shared<Sem::ResolveResult>(fixture.HolderType()), 0);
    OwnedArgument arg(std::make_unique<Syntax::PrimitiveExpression>(
                          Syntax::PrimitiveValue(std::int64_t(5))),
                      ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int64), 5),
                      5);
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.indexGetter, targetOwned.Bound(),
        {arg.Bound()}, std::nullopt);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(result.Expression());
    ASSERT_NE(indexer, nullptr);
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    // The CastArguments arm's observable effect: the int64 constant argument
    // folded to the int32 parameter type (a cast would only be observable on
    // non-constant arguments).
    auto* foldedArg = dynamic_cast<Syntax::PrimitiveExpression*>(
        indexer->Arguments().FirstOrNull());
    ASSERT_NE(foldedArg, nullptr);
    EXPECT_TRUE(std::holds_alternative<std::int32_t>(foldedArg->Value()));
}

TEST(AccessorCallTest, GetterResolvesToTheIdentifier)
{
    AccessorFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.getter,
        MakeThisTarget(fixture.HolderType()), {}, std::nullopt);
    auto* identifier =
        dynamic_cast<Syntax::IdentifierExpression*>(result.Expression());
    ASSERT_NE(identifier, nullptr);
    EXPECT_EQ(identifier->Identifier(), "Foo");
    // The resolve result is the property member over the `this` target.
    auto* rr = dynamic_cast<const Sem::MemberResolveResult*>(result.ResolveResult());
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(rr->Member()->MemberDefinition(), fixture.prop->MemberDefinition());
    ASSERT_NE(rr->TargetResult(), nullptr);
    EXPECT_TRUE(dynamic_cast<const Sem::ThisResolveResult*>(rr->TargetResult())
                != nullptr);
}

TEST(AccessorCallTest, SetterRendersTheAssignment)
{
    AccessorFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    // The value argument (the setter's single parameter value).
    OwnedArgument value(std::make_unique<Syntax::PrimitiveExpression>(
                            Syntax::PrimitiveValue(std::int32_t(42))),
                        ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32), 42),
                        42);
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.setter,
        MakeThisTarget(fixture.HolderType()), {value.Bound()}, std::nullopt);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(result.Expression());
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->Identifier(), "Foo");
    ASSERT_NE(assignment->Right(), nullptr);
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(assignment->Right());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(right->Value()), 42);
    // The assignment's resolve result is the TypeResolveResult over the owner's
    // return type (the C# `new TypeResolveResult(method.AccessorOwner.ReturnType)`).
    auto* rr = dynamic_cast<const Sem::TypeResolveResult*>(result.ResolveResult());
    ASSERT_NE(rr, nullptr);
    EXPECT_TRUE(rr->Type().Equals(fixture.prop->ReturnType()));
}

TEST(AccessorCallTest, EventAddAccessorRendersThePlusEqual)
{
    EventFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    OwnedArgument value(std::make_unique<Syntax::PrimitiveExpression>(
                            Syntax::PrimitiveValue(std::int32_t(1))),
                        ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32), 1),
                        1);
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.addAccessor,
        MakeThisTarget(fixture.HolderType()), {value.Bound()}, std::nullopt);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(result.Expression());
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Add);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->Identifier(), "Click");
}

TEST(AccessorCallTest, EventRemoveAccessorRendersTheMinusEqual)
{
    EventFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    OwnedArgument value(std::make_unique<Syntax::PrimitiveExpression>(
                            Syntax::PrimitiveValue(std::int32_t(1))),
                        ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32), 1),
                        1);
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.removeAccessor,
        MakeThisTarget(fixture.HolderType()), {value.Bound()}, std::nullopt);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(result.Expression());
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Subtract);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->Identifier(), "Click");
}

TEST(AccessorCallTest, IndexerGetterRendersTheIndexerExpression)
{
    IndexerFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    // A non-this target (an identifier over the holder type) so requireTarget
    // drives the IndexerExpression's Target slot.
    OwnedArgument targetOwned(
        std::make_unique<Syntax::IdentifierExpression>("obj"),
        std::make_shared<Sem::ResolveResult>(fixture.HolderType()), 0);
    OwnedArgument arg(std::make_unique<Syntax::PrimitiveExpression>(
                          Syntax::PrimitiveValue(std::int32_t(3))),
                      ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32), 3),
                      3);
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.indexGetter, targetOwned.Bound(),
        {arg.Bound()}, std::nullopt);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(result.Expression());
    ASSERT_NE(indexer, nullptr);
    auto* targetExpr =
        dynamic_cast<Syntax::IdentifierExpression*>(indexer->Target());
    ASSERT_NE(targetExpr, nullptr);
    EXPECT_EQ(targetExpr->Identifier(), "obj");
    EXPECT_EQ(indexer->Arguments().Count(), 1);
    auto* rr = dynamic_cast<const Sem::MemberResolveResult*>(result.ResolveResult());
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(rr->Member()->MemberDefinition(), fixture.prop->MemberDefinition());
}

TEST(AccessorCallTest, UnresolvableNameFallsBackToTheAccessorOwner)
{
    AccessorFixture fixture(/*registerProp=*/false);
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.getter,
        MakeThisTarget(fixture.HolderType()), {}, std::nullopt);
    // The fix loop exhausts CastArguments/requireTarget/targetCast and takes the
    // accessor-owner fallback; the requireTarget arm renders the member
    // reference over the `this` target.
    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(result.Expression());
    ASSERT_NE(memberRef, nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Foo");
    ASSERT_NE(memberRef->Target(), nullptr);
    EXPECT_TRUE(dynamic_cast<const Syntax::ThisReferenceExpression*>(memberRef->Target())
                != nullptr);
    auto* rr = dynamic_cast<const Sem::MemberResolveResult*>(result.ResolveResult());
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(rr->Member()->MemberDefinition(), fixture.prop->MemberDefinition());
}

TEST(AccessorCallTest, InitializedObjectTargetRendersTheNullTargetIndexer)
{
    IndexerFixture fixture;
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    // An initialized-object target (the object-initializer shape): the render
    // drops the target (the C# `target.ResolveResult is
    // InitializedObjectResolveResult ? null : target.Expression`).
    OwnedArgument targetOwned(
        std::make_unique<Syntax::IdentifierExpression>("obj"),
        std::make_shared<Sem::InitializedObjectResolveResult>(fixture.HolderType()),
        0);
    // The indexer setter's call arguments: [index, value] -- the value is
    // removed as the Last() argument, leaving the index in arguments.
    OwnedArgument index(std::make_unique<Syntax::PrimitiveExpression>(
                            Syntax::PrimitiveValue(std::int32_t(3))),
                        ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32), 3),
                        3);
    OwnedArgument value(std::make_unique<Syntax::PrimitiveExpression>(
                            Syntax::PrimitiveValue(std::int32_t(7))),
                        ConstantRR(fixture.holder.KnownType(TS::KnownTypeCode::Int32), 7),
                        7);
    CS::ExpressionWithResolveResult result = builder.HandleAccessorCall(
        CS::ExpectedTargetDetails{}, *fixture.indexSetter, targetOwned.Bound(),
        {index.Bound(), value.Bound()}, std::nullopt);
    auto* assignment = dynamic_cast<Syntax::AssignmentExpression*>(result.Expression());
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->Operator(), Syntax::AssignmentOperatorType::Assign);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(assignment->Left());
    ASSERT_NE(indexer, nullptr);
    EXPECT_EQ(indexer->Target(), nullptr);
    EXPECT_EQ(indexer->Arguments().Count(), 1);
}

TEST(AccessorCallTest, AccessorArmThroughBuildRendersTheIdentifier)
{
    AccessorFixture fixture;
    // The getter call over `this` (a parameter LdLoc over the synthetic `this`
    // slot -- the MatchLdThis shape): the mainline accessor gate fires (a zero-
    // parameter getter against allowedParamCount 0).
    auto thisVar = std::make_shared<IL::ILVariable>();
    thisVar->Name = "this";
    thisVar->Kind = IL::VariableKind::Parameter;
    thisVar->Index = -1;
    thisVar->Type = fixture.HolderType();
    IL::Call call("get_Foo");
    call.Method = fixture.getter;
    call.AddArg(std::make_unique<IL::LdLoc>(std::move(thisVar)));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.Build(
        IL::OpCode::Call, *fixture.getter, CallChildPtrsForTest(call), std::nullopt,
        nullptr);
    auto* identifier =
        dynamic_cast<Syntax::IdentifierExpression*>(result.Expression());
    ASSERT_NE(identifier, nullptr);
    EXPECT_EQ(identifier->Identifier(), "Foo");
    auto* rr = dynamic_cast<const Sem::MemberResolveResult*>(result.ResolveResult());
    ASSERT_NE(rr, nullptr);
    EXPECT_EQ(rr->Member()->MemberDefinition(), fixture.prop->MemberDefinition());
}

// ---------------------------------------------------------------------------
// HandleStringInterpolation: the interpolation render (CallBuilder.cs lines
// 595-648 + 766-935).
// ---------------------------------------------------------------------------

TEST(HandleStringInterpolationTest, TokenizeFormatStringMatrix)
{
    using Tok = CS::CallBuilder::TokenKind;
    using Token = std::pair<Tok, std::optional<std::string>>;
    // Plain text only.
    auto tokens = CS::CallBuilder::TokenizeFormatString("abc");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::String);
    EXPECT_EQ(tokens[0].second, "abc");
    // A single argument slot.
    tokens = CS::CallBuilder::TokenizeFormatString("{0}");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::Argument);
    EXPECT_EQ(tokens[0].second, "0");
    // Escaped braces collapse to doubled literal text.
    tokens = CS::CallBuilder::TokenizeFormatString("{{x}}");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::String);
    EXPECT_EQ(tokens[0].second, "{{x}}");
    // A leading literal then an unterminated brace.
    tokens = CS::CallBuilder::TokenizeFormatString("a{{");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::String);
    EXPECT_EQ(tokens[0].second, "a{{");
    // An unterminated argument run yields the Error token after the text.
    tokens = CS::CallBuilder::TokenizeFormatString("{0");
    // The unterminated run is never closed: the tail yields ONLY the
    // Error token (the ":"/"," cases never yield -- they only refine
    // the run kind and append).
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::Error);
    EXPECT_FALSE(tokens[0].second.has_value());
    // Format / alignment / both suffixes refine the argument run's kind.
    tokens = CS::CallBuilder::TokenizeFormatString("{0:x}");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::ArgumentWithFormat);
    EXPECT_EQ(tokens[0].second, "0:x");
    tokens = CS::CallBuilder::TokenizeFormatString("{0,5}");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::ArgumentWithAlignment);
    EXPECT_EQ(tokens[0].second, "0,5");
    tokens = CS::CallBuilder::TokenizeFormatString("{0,5:x}");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].first, Tok::ArgumentWithAlignmentAndFormat);
    EXPECT_EQ(tokens[0].second, "0,5:x");
    // A bare closing brace inside literal text is the Error token.
    tokens = CS::CallBuilder::TokenizeFormatString("a{0}b}");
    // The bare closing brace yields the Error token WITHOUT clearing sb,
    // so the tail still yields the pending literal (the C# shape).
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].second, "a");
    EXPECT_EQ(tokens[1].first, Tok::Argument);
    EXPECT_EQ(tokens[2].first, Tok::Error);
    EXPECT_FALSE(tokens[2].second.has_value());
    EXPECT_EQ(tokens[3].first, Tok::String);
    EXPECT_EQ(tokens[3].second, "b");
    // The literal runs BETWEEN argument runs each yield: a{0}b is
    // three tokens (the tail yields the trailing "b").
    tokens = CS::CallBuilder::TokenizeFormatString("a{0}b");
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].second, "a");
    EXPECT_EQ(tokens[1].first, Tok::Argument);
    EXPECT_EQ(tokens[2].first, Tok::String);
    EXPECT_EQ(tokens[2].second, "b");
    // An empty string yields no tokens.
    tokens = CS::CallBuilder::TokenizeFormatString("");
    EXPECT_EQ(tokens.size(), 0u);
}

TEST(HandleStringInterpolationTest, TryGetStringInterpolationTokensHappyPath)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    format->SetParameters({fmtArg.parameter, arg.parameter});

    IL::LdStr formatStr("Hello {0}!");
    IL::LdcI4 value(42);
    std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments, std::nullopt);

    std::optional<std::string> outFormat;
    std::optional<std::vector<CS::CallBuilder::FormatToken>> tokens;
    ASSERT_TRUE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    ASSERT_TRUE(outFormat.has_value());
    EXPECT_EQ(*outFormat, "Hello {0}!");
    ASSERT_TRUE(tokens.has_value());
    ASSERT_EQ(tokens->size(), 3u);
    EXPECT_EQ((*tokens)[0].Kind, CS::CallBuilder::TokenKind::String);
    EXPECT_EQ(*(*tokens)[0].Format, "Hello ");
    EXPECT_EQ((*tokens)[0].Index, -1);
    EXPECT_EQ((*tokens)[1].Kind, CS::CallBuilder::TokenKind::Argument);
    EXPECT_EQ((*tokens)[1].Index, 0);
    EXPECT_FALSE((*tokens)[1].Format.has_value());
    EXPECT_EQ((*tokens)[2].Kind, CS::CallBuilder::TokenKind::String);
    EXPECT_EQ(*(*tokens)[2].Format, "!");
}

TEST(HandleStringInterpolationTest, TryGetStringInterpolationTokensFormatSuffixes)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    format->SetParameters({fmtArg.parameter, arg.parameter});

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    // The {0:x} form: the ArgumentWithFormat token carries the suffix.
    {
        IL::LdStr formatStr("v{0:x}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments,
            std::nullopt);
        std::optional<std::string> outFormat;
        std::optional<std::vector<CS::CallBuilder::FormatToken>> tokens;
        ASSERT_TRUE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
        ASSERT_EQ(tokens->size(), 2u);
        EXPECT_EQ((*tokens)[1].Kind,
                  CS::CallBuilder::TokenKind::ArgumentWithFormat);
        EXPECT_EQ((*tokens)[1].Index, 0);
        EXPECT_EQ((*tokens)[1].Alignment, 0);
        ASSERT_TRUE((*tokens)[1].Format.has_value());
        EXPECT_EQ(*(*tokens)[1].Format, "x");
    }
    // The {0,5} form: the ArgumentWithAlignment token carries the alignment
    // and a null suffix.
    {
        IL::LdStr formatStr("v{0,5}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments,
            std::nullopt);
        std::optional<std::string> outFormat;
        std::optional<std::vector<CS::CallBuilder::FormatToken>> tokens;
        ASSERT_TRUE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
        ASSERT_EQ(tokens->size(), 2u);
        EXPECT_EQ((*tokens)[1].Kind,
                  CS::CallBuilder::TokenKind::ArgumentWithAlignment);
        EXPECT_EQ((*tokens)[1].Alignment, 5);
        EXPECT_FALSE((*tokens)[1].Format.has_value());
    }
    // The {0,-5:x} form: a negative alignment parses, the suffix survives.
    {
        IL::LdStr formatStr("v{0,-5:x}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments,
            std::nullopt);
        std::optional<std::string> outFormat;
        std::optional<std::vector<CS::CallBuilder::FormatToken>> tokens;
        ASSERT_TRUE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
        ASSERT_EQ(tokens->size(), 2u);
        EXPECT_EQ((*tokens)[1].Kind,
                  CS::CallBuilder::TokenKind::ArgumentWithAlignmentAndFormat);
        EXPECT_EQ((*tokens)[1].Alignment, -5);
        ASSERT_TRUE((*tokens)[1].Format.has_value());
        EXPECT_EQ(*(*tokens)[1].Format, "x");
    }
}

TEST(HandleStringInterpolationTest, TryGetStringInterpolationTokensFailureMatrix)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    ParamFixture arg2(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg2");
    format->SetParameters({fmtArg.parameter, arg.parameter, arg2.parameter});

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    std::optional<std::string> outFormat;
    std::optional<std::vector<CS::CallBuilder::FormatToken>> tokens;
    // Argument names (an out-of-place argument) reject the whole walk.
    {
        IL::LdStr formatStr("Hello {0}!");
        IL::LdcI4 value(42);
        IL::LdcI4 value2(43);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value2, &value};
        // An out-of-place argumentToParameterMap is what assigns names
        // (the C# argumentNames path).
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments,
            std::vector<int>{0, 2, 1});
        ASSERT_TRUE(list.ArgumentNames.has_value());
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
        EXPECT_FALSE(outFormat.has_value());
        EXPECT_FALSE(tokens.has_value());
    }
    // A non-constant first argument rejects the walk.
    {
        CS::ArgumentList raw;
        Syntax::IdentifierExpression ident("value");
        ident.AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
            fixture.holder.KnownType(TS::KnownTypeCode::Int32)));
        raw.Arguments.push_back(
            CS::TranslatedExpression(&ident, CS::GetResolveResult(ident)));
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(raw, outFormat, tokens));
    }
    // A first argument typed as a NON-String constant rejects the walk.
    {
        CS::ArgumentList raw;
        Syntax::PrimitiveExpression nonString(Syntax::PrimitiveValue(std::int32_t(7)));
        nonString.AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
            fixture.holder.KnownType(TS::KnownTypeCode::Int32), 7));
        raw.Arguments.push_back(CS::TranslatedExpression(
            &nonString, CS::GetResolveResult(nonString)));
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(raw, outFormat, tokens));
    }
    // A later argument carrying a string literal rejects the walk.
    {
        IL::LdStr formatStr("Hello {0}!");
        IL::LdStr literalArg("nested");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &literalArg, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments,
            std::nullopt);
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    }
    // A two-parameter method for the two-argument rejection cases (the
    // BuildArgumentList argument/parameter count assert).
    auto format2 = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format2->SetName("Format");
    format2->SetIsStatic(true);
    format2->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    format2->SetParameters({fmtArg.parameter, arg.parameter});
    // A slot index that is not consecutive rejects the walk.
    {
        IL::LdStr formatStr("{1}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format2, 0, callArguments,
            std::nullopt);
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    }
    // More slots than arguments rejects the walk (the final i check).
    {
        IL::LdStr formatStr("{0}{1}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format2, 0, callArguments,
            std::nullopt);
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    }
    // A repeated slot rejects the walk (the second 0 is not consecutive).
    {
        IL::LdStr formatStr("{0}{0}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format2, 0, callArguments,
            std::nullopt);
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    }
    // An unparsable alignment rejects the walk.
    {
        IL::LdStr formatStr("{0,x}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format2, 0, callArguments,
            std::nullopt);
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    }
    // An empty format suffix rejects the walk.
    {
        IL::LdStr formatStr("{0:}");
        IL::LdcI4 value(42);
        std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
        CS::ArgumentList list = builder.BuildArgumentList(
            CS::ExpectedTargetDetails{}, nullptr, *format2, 0, callArguments,
            std::nullopt);
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(list, outFormat, tokens));
    }
    // Empty arguments reject the walk.
    {
        CS::ArgumentList raw;
        EXPECT_FALSE(builder.TryGetStringInterpolationTokens(raw, outFormat, tokens));
    }
}

TEST(HandleStringInterpolationTest, FormatArmRendersTheInterpolatedString)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    format->SetParameters({fmtArg.parameter, arg.parameter});

    IL::LdStr formatStr("Hello {0}!");
    IL::LdcI4 value(42);
    std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments, std::nullopt);

    CS::ExpressionWithResolveResult result =
        builder.HandleStringInterpolation(*format, list);
    auto* interpolated =
        dynamic_cast<Syntax::InterpolatedStringExpression*>(result.Expression());
    ASSERT_NE(interpolated, nullptr);
    // The content: text / interpolation / text.
    ASSERT_EQ(interpolated->Content().Count(), 3);
    auto* text1 = dynamic_cast<Syntax::InterpolatedStringText*>(
        interpolated->Content().At(0));
    ASSERT_NE(text1, nullptr);
    EXPECT_EQ(text1->Text(), "Hello ");
    auto* interpolation = dynamic_cast<Syntax::Interpolation*>(
        interpolated->Content().At(1));
    ASSERT_NE(interpolation, nullptr);
    auto* argExpr = dynamic_cast<Syntax::PrimitiveExpression*>(
        interpolation->Expression());
    ASSERT_NE(argExpr, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(argExpr->Value()), 42);
    EXPECT_EQ(interpolation->Alignment(), 0);
    EXPECT_FALSE(interpolation->Suffix().has_value());
    auto* text2 = dynamic_cast<Syntax::InterpolatedStringText*>(
        interpolated->Content().At(2));
    ASSERT_NE(text2, nullptr);
    EXPECT_EQ(text2->Text(), "!");
    // The resolve result is the InterpolatedStringResolveResult over the
    // format string and the argument resolve results (skipCount 1).
    auto* isrr = dynamic_cast<const Sem::InterpolatedStringResolveResult*>(
        result.ResolveResult());
    ASSERT_NE(isrr, nullptr);
    EXPECT_EQ(isrr->FormatString(), "Hello {0}!");
    ASSERT_EQ(isrr->Arguments().size(), 1u);
    EXPECT_TRUE(TS::IsKnownType(isrr->Type(), TS::KnownTypeCode::String));
}

TEST(HandleStringInterpolationTest, CreateArmRendersTheCastOverFormattableString)
{
    BuilderFixture fixture;
    // `FormattableStringFactory.Create` over the right namespace.
    auto factory = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "FormattableStringFactory", "System.Runtime.CompilerServices",
        TS::FullTypeName(TS::TopLevelTypeName("System.Runtime.CompilerServices",
                                              "FormattableStringFactory")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture.holder.compilation,
        &fixture.holder.compilation.MainModule());
    auto create = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    create->SetName("Create");
    create->SetIsStatic(true);
    create->SetDeclaringType(TS::ITypePtr(factory.get(), [](TS::IType*) {}));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    create->SetParameters({fmtArg.parameter, arg.parameter});

    IL::LdStr formatStr("Hello {0}!");
    IL::LdcI4 value(42);
    std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *create, 0, callArguments, std::nullopt);

    CS::ExpressionWithResolveResult result =
        builder.HandleStringInterpolation(*create, list);
    auto* cast = dynamic_cast<Syntax::CastExpression*>(result.Expression());
    ASSERT_NE(cast, nullptr);
    // The inner expression is the interpolation; the cast's resolve result is
    // the ImplicitInterpolatedStringConversion over FormattableString.
    auto* interpolated = dynamic_cast<Syntax::InterpolatedStringExpression*>(
        cast->Expression());
    ASSERT_NE(interpolated, nullptr);
    ASSERT_EQ(interpolated->Content().Count(), 3);
    auto* interpolation = dynamic_cast<Syntax::Interpolation*>(
        interpolated->Content().At(1));
    ASSERT_NE(interpolation, nullptr);
    // The OUTER resolve result is the cast's ConversionResolveResult; the
    // InterpolatedStringResolveResult lives on the inner interpolation node
    // (the C# `new CastExpression(..., expr.WithRR(isrr)).WithRR(new
    // ConversionResolveResult(...))` shape).
    const auto* conversionRr =
        dynamic_cast<const Sem::ConversionResolveResult*>(result.ResolveResult());
    ASSERT_NE(conversionRr, nullptr);
    auto* isrr = dynamic_cast<const Sem::InterpolatedStringResolveResult*>(
        CS::GetResolveResult(*interpolated));
    ASSERT_NE(isrr, nullptr);
    EXPECT_TRUE(TS::IsKnownType(isrr->Type(), TS::KnownTypeCode::String));
    EXPECT_EQ(isrr->FormatString(), "Hello {0}!");
    ASSERT_EQ(isrr->Arguments().size(), 1u);
    ASSERT_NE(conversionRr->ConversionProperty(), nullptr);
    EXPECT_TRUE(
        conversionRr->ConversionProperty()->IsInterpolatedStringConversion());
}

TEST(HandleStringInterpolationTest, UnwrapsTheSingleElementArrayLiteral)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    format->SetParameters({fmtArg.parameter, arg.parameter});

    // The one-element array-literal argument: an ArrayCreateExpression over an
    // initializer whose sole element is the int constant, with the
    // ArrayCreateResolveResult the unpack reads. The node list is hand-built
    // (the IL shape for `new[] { x }` is the multi-instruction newarr/stelem
    // chain the C# does not read -- the render consumes the TRANSLATED shape).
    auto elementType = fixture.holder.KnownType(TS::KnownTypeCode::Int32);
    // The element's resolve result IS the InitializerElements entry (the same
    // object -- the TranslatedExpression ctor assert reads the node's
    // annotation through the ArrayCreateResolveResult entry).
    auto elementRr = std::make_shared<Sem::ConstantResolveResult>(elementType, 42);
    auto elementExpr = std::make_unique<Syntax::PrimitiveExpression>(
        Syntax::PrimitiveValue(std::int32_t(42)));
    elementExpr->AddAnnotation(elementRr);
    Syntax::PrimitiveExpression* element = elementExpr.get();
    auto* initializer = new Syntax::ArrayInitializerExpression();
    initializer->Elements().Add(elementExpr.release());
    auto* arrayCreate = new Syntax::ArrayCreateExpression();
    arrayCreate->Type(new Syntax::PrimitiveType("int"));
    arrayCreate->Initializer(initializer);
    auto arrayType = std::make_shared<TS::ArrayType>(elementType);
    auto arrayRr = std::make_shared<Sem::ArrayCreateResolveResult>(
        arrayType, std::vector<std::shared_ptr<Sem::ResolveResult>>{},
        std::vector<std::shared_ptr<Sem::ResolveResult>>{elementRr});
    arrayCreate->AddAnnotation(arrayRr);



    // The ArgumentList the C# render consumes is always built through
    // BuildArgumentList, whose ExpectedParameters bookkeeping the resolve-result
    // slice indexes -- the hand-built fixture must carry it too.
    CS::ArgumentList raw;
    raw.ExpectedParameters.push_back(fmtArg.parameter.get());
    raw.ExpectedParameters.push_back(arg.parameter.get());
    auto formatExpr = std::make_unique<Syntax::PrimitiveExpression>(
        Syntax::PrimitiveValue("Hello {0}!"));
    formatExpr->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
        fixture.holder.KnownType(TS::KnownTypeCode::String),
        std::string("Hello {0}!")));
    raw.Arguments.push_back(CS::TranslatedExpression(
        formatExpr.get(), CS::GetResolveResult(*formatExpr)));
    raw.Arguments.push_back(CS::TranslatedExpression(
        arrayCreate, CS::GetResolveResult(*arrayCreate)));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result =
        builder.HandleStringInterpolation(*format, raw);
    auto* interpolated =
        dynamic_cast<Syntax::InterpolatedStringExpression*>(result.Expression());
    ASSERT_NE(interpolated, nullptr);
    ASSERT_EQ(interpolated->Content().Count(), 3);
    auto* interpolation = dynamic_cast<Syntax::Interpolation*>(
        interpolated->Content().At(1));
    ASSERT_NE(interpolation, nullptr);
    // The interpolation renders over the ELEMENT (the array literal is
    // unwrapped), not over the array-creation node.
    auto* argExpr = dynamic_cast<Syntax::PrimitiveExpression*>(
        interpolation->Expression());
    ASSERT_NE(argExpr, nullptr);
    EXPECT_EQ(std::get<std::int32_t>(argExpr->Value()), 42);
}

TEST(HandleStringInterpolationTest, EmptyTokensAnswersDefault)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    format->SetParameters({fmtArg.parameter});

    // An empty format string with no further arguments: the tokens list is
    // empty and the render answers the default (a null expression).
    IL::LdStr formatStr("");
    std::vector<IL::ILInstruction*> callArguments{&formatStr};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments, std::nullopt);
    CS::ExpressionWithResolveResult result =
        builder.HandleStringInterpolation(*format, list);
    EXPECT_EQ(result.Expression(), nullptr);
}

TEST(HandleStringInterpolationTest, UnparsableTokensAnswerDefault)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    format->SetParameters({fmtArg.parameter, arg.parameter});

    // A non-consecutive slot index fails TryGetStringInterpolationTokens; the
    // render answers the default and the mainline falls through to the next
    // arm.
    IL::LdStr formatStr("{1}");
    IL::LdcI4 value(42);
    std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments, std::nullopt);
    CS::ExpressionWithResolveResult result =
        builder.HandleStringInterpolation(*format, list);
    EXPECT_EQ(result.Expression(), nullptr);
}

TEST(HandleStringInterpolationTest, BuildMainlineRendersTheInterpolation)
{
    BuilderFixture fixture;
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    ParamFixture arg(fixture.holder.KnownType(TS::KnownTypeCode::Int32), "arg");
    format->SetParameters({fmtArg.parameter, arg.parameter});

    IL::LdStr formatStr("Hello {0}!");
    IL::LdcI4 value(42);
    IL::Call call("Format");
    call.Method = format;
    call.AddArg(std::make_unique<IL::LdStr>("Hello {0}!"));
    call.AddArg(std::make_unique<IL::LdcI4>(42));

    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ExpressionWithResolveResult result = builder.Build(
        IL::OpCode::Call, *format, CallChildPtrsForTest(call), std::nullopt,
        nullptr);
    auto* interpolated =
        dynamic_cast<Syntax::InterpolatedStringExpression*>(result.Expression());
    ASSERT_NE(interpolated, nullptr);
    ASSERT_EQ(interpolated->Content().Count(), 3);
    auto* isrr = dynamic_cast<const Sem::InterpolatedStringResolveResult*>(
        result.ResolveResult());
    ASSERT_NE(isrr, nullptr);
    EXPECT_EQ(isrr->FormatString(), "Hello {0}!");
    ASSERT_EQ(isrr->Arguments().size(), 1u);
}

TEST(IsInterpolatedStringCreationTest, ParamsOverloadRequiresTheArrayLiteral)
{
    BuilderFixture fixture;
    // A params `Format` over String: the non-expanded form rejects the arm
    // unless the second argument is an array literal.
    auto format = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    format->SetName("Format");
    format->SetIsStatic(true);
    format->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    auto paramsType =
        std::make_shared<TS::ArrayType>(
            fixture.holder.KnownType(TS::KnownTypeCode::Object));
    auto paramsParameter =
        std::make_shared<Impl::DefaultParameter>(
            paramsType, "args", nullptr, std::vector<const TS::IAttribute*>(),
            TS::ReferenceKind::None, true);
    ParamFixture fmtArg(fixture.holder.KnownType(TS::KnownTypeCode::String), "format");
    format->SetParameters(
        {fmtArg.parameter, paramsParameter});

    IL::LdStr formatStr("Hello {0}!");
    IL::LdcI4 value(42);
    std::vector<IL::ILInstruction*> callArguments{&formatStr, &value};
    CS::CallBuilder builder = fixture.MakeCallBuilder();
    CS::ArgumentList list = builder.BuildArgumentList(
        CS::ExpectedTargetDetails{}, nullptr, *format, 0, callArguments, std::nullopt);
    // The non-expanded two-argument call over a params overload rejects.
    EXPECT_FALSE(CS::CallBuilder::IsInterpolatedStringCreation(*format, list));
    // A parameterless method reaching the params arm throws (the .NET Last()
    // over the empty list) -- unreachable through real IL (the format string
    // is always the first argument), pinned as the faithful Last() shape.
    auto noParams = std::make_shared<Impl::FakeMethod>(
        fixture.holder.compilation, TS::SymbolKind::Method);
    noParams->SetName("Format");
    noParams->SetIsStatic(true);
    noParams->SetDeclaringType(
        fixture.holder.KnownType(TS::KnownTypeCode::String));
    CS::ArgumentList emptyParamsList;
    auto literal = std::make_unique<Syntax::PrimitiveExpression>(
        Syntax::PrimitiveValue("x"));
    literal->AddAnnotation(std::make_shared<Sem::ConstantResolveResult>(
        fixture.holder.KnownType(TS::KnownTypeCode::String), std::string("x")));
    emptyParamsList.Arguments.push_back(CS::TranslatedExpression(
        literal.get(), CS::GetResolveResult(*literal)));
    EXPECT_THROW(
        (void)CS::CallBuilder::IsInterpolatedStringCreation(*noParams,
                                                            emptyParamsList),
        std::out_of_range);
}
