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
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <any>
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
