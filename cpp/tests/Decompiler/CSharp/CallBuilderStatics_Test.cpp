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

// The CallBuilder static call-shape helpers the Build core consults:
// IsStringToReadOnlySpanCharImplicitConversion, IsInterpolatedStringCreation,
// IsNullConditional, IsDelegateEqualityComparison, and
// HandleDelegateEqualityComparison -- over the MinimalCorlib fixture and
// test-local stub types.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace CSharp = ::ILSpy::Decompiler::CSharp;

// A test-local delegate type (the minimal corlib models System.Delegate as a
// Class; only a concrete delegate has TypeKind::Delegate).
class DelegateStubType : public TS::IType {
public:
    explicit DelegateStubType(std::string name) : name_(std::move(name)) {}

    TS::TypeKind Kind() const override { return TS::TypeKind::Delegate; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    std::string name_;
};

// The shared compilation plus the type handles the tests build methods from.
struct StaticCallFixture {
    TS::SimpleCompilation compilation;
    TS::ITypePtr stringType;
    TS::ITypePtr charType;
    TS::ITypePtr intType;
    TS::ITypePtr spanDefinition;
    TS::ITypePtr delegateType;

    StaticCallFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          stringType(TypePtr(TS::KnownTypeCode::String)),
          charType(TypePtr(TS::KnownTypeCode::Char)),
          intType(TypePtr(TS::KnownTypeCode::Int32)),
          spanDefinition(TypePtr(TS::KnownTypeCode::ReadOnlySpanOfT)),
          delegateType(TypePtr(TS::KnownTypeCode::Delegate))
    {
    }

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    TS::ITypePtr SpanOfChar() const
    {
        return std::make_shared<TS::ParameterizedType>(
            spanDefinition, std::vector<TS::ITypePtr>{charType});
    }

    TS::ITypePtr SpanOf(TS::ITypePtr element) const
    {
        return std::make_shared<TS::ParameterizedType>(
            spanDefinition, std::vector<TS::ITypePtr>{std::move(element)});
    }

    std::shared_ptr<Impl::FakeMethod> MakeMethod(
        TS::SymbolKind kind, const std::string& name, TS::ITypePtr declaringType,
        std::vector<TS::ITypePtr> paramTypes, TS::ITypePtr returnType,
        bool isStatic = true)
    {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, kind);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        method->SetDeclaringType(std::move(declaringType));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (TS::ITypePtr& type : paramTypes)
            parameters.push_back(
                std::make_shared<Impl::DefaultParameter>(type, std::string("arg")));
        method->SetParameters(parameters);
        method->SetReturnType(std::move(returnType));
        return method;
    }

    static CSharp::TranslatedExpression MakeArg(TS::ITypePtr type)
    {
        auto* ident = new Syntax::IdentifierExpression("x");
        return CSharp::WithRR(CSharp::WithoutILInstruction(*ident),
                              std::make_shared<Sem::ResolveResult>(std::move(type)));
    }
};

} // namespace

TEST(CallBuilderStaticsTest, IsStringToReadOnlySpanCharImplicitConversionMatrix)
{
    StaticCallFixture fixture;
    // The string -> ReadOnlySpan<char> op_Implicit: recognized.
    auto conversion = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Implicit", fixture.stringType,
        {fixture.stringType}, fixture.SpanOfChar());
    EXPECT_TRUE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *conversion));

    // A span over a non-char element: not recognized.
    auto spanOfInt = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Implicit", fixture.intType,
        {fixture.intType}, fixture.SpanOf(fixture.intType));
    EXPECT_FALSE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *spanOfInt));

    // A non-span return: not recognized.
    auto plain = fixture.MakeMethod(TS::SymbolKind::Operator, "op_Implicit",
                                    fixture.stringType, {fixture.stringType},
                                    fixture.stringType);
    EXPECT_FALSE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *plain));

    // A non-string parameter: not recognized.
    auto fromInt = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Implicit", fixture.intType,
        {fixture.intType}, fixture.SpanOfChar());
    EXPECT_FALSE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *fromInt));

    // op_Explicit, two parameters, and a non-operator method: not recognized.
    auto explicitConv = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Explicit", fixture.stringType,
        {fixture.stringType}, fixture.SpanOfChar());
    EXPECT_FALSE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *explicitConv));
    auto twoParams = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Implicit", fixture.stringType,
        {fixture.stringType, fixture.stringType}, fixture.SpanOfChar());
    EXPECT_FALSE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *twoParams));
    auto notOperator = fixture.MakeMethod(
        TS::SymbolKind::Method, "op_Implicit", fixture.stringType,
        {fixture.stringType}, fixture.SpanOfChar());
    EXPECT_FALSE(CSharp::CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
        *notOperator));
}

TEST(CallBuilderStaticsTest, IsNullConditionalMatrix)
{
    auto* ident = new Syntax::IdentifierExpression("x");
    auto* conditional = new Syntax::UnaryOperatorExpression(
        ident, Syntax::UnaryOperatorType::NullConditional);
    EXPECT_TRUE(CSharp::CallBuilder::IsNullConditional(conditional));

    auto* otherIdent = new Syntax::IdentifierExpression("x");
    auto* not = new Syntax::UnaryOperatorExpression(otherIdent,
                                                    Syntax::UnaryOperatorType::Not);
    EXPECT_FALSE(CSharp::CallBuilder::IsNullConditional(not));

    auto* plain = new Syntax::IdentifierExpression("x");
    EXPECT_FALSE(CSharp::CallBuilder::IsNullConditional(plain));
    EXPECT_FALSE(CSharp::CallBuilder::IsNullConditional(nullptr));
}

TEST(CallBuilderStaticsTest, IsDelegateEqualityComparisonAndRender)
{
    StaticCallFixture fixture;
    auto delegateA = std::make_shared<DelegateStubType>("System.Action");
    auto delegateB = std::make_shared<DelegateStubType>("System.Func");

    auto equality = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Equality", fixture.delegateType,
        {delegateA, delegateA}, fixture.stringType);
    std::vector<CSharp::TranslatedExpression> sameType{
        StaticCallFixture::MakeArg(delegateA), StaticCallFixture::MakeArg(delegateA)};
    EXPECT_TRUE(
        CSharp::CallBuilder::IsDelegateEqualityComparison(*equality, sameType));

    // The render reparents the argument nodes, so each call gets a fresh pair.
    std::vector<CSharp::TranslatedExpression> renderEquality{
        StaticCallFixture::MakeArg(delegateA), StaticCallFixture::MakeArg(delegateA)};
    auto* rendered = CSharp::CallBuilder::HandleDelegateEqualityComparison(
        *equality, renderEquality);
    auto* bin = dynamic_cast<Syntax::BinaryOperatorExpression*>(rendered);
    ASSERT_TRUE(bin != nullptr);
    EXPECT_EQ(bin->Operator(), Syntax::BinaryOperatorType::Equality);

    auto inequality = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Inequality", fixture.delegateType,
        {delegateA, delegateA}, fixture.stringType);
    EXPECT_TRUE(
        CSharp::CallBuilder::IsDelegateEqualityComparison(*inequality, sameType));
    std::vector<CSharp::TranslatedExpression> renderInequality{
        StaticCallFixture::MakeArg(delegateA), StaticCallFixture::MakeArg(delegateA)};
    auto* renderedInequality = CSharp::CallBuilder::HandleDelegateEqualityComparison(
        *inequality, renderInequality);
    auto* renderedInequalityBin =
        dynamic_cast<Syntax::BinaryOperatorExpression*>(renderedInequality);
    ASSERT_TRUE(renderedInequalityBin != nullptr);
    EXPECT_EQ(renderedInequalityBin->Operator(),
              Syntax::BinaryOperatorType::InEquality);

    // Mismatched delegate types: not a comparison.
    std::vector<CSharp::TranslatedExpression> mixed{
        StaticCallFixture::MakeArg(delegateA), StaticCallFixture::MakeArg(delegateB)};
    EXPECT_FALSE(CSharp::CallBuilder::IsDelegateEqualityComparison(*equality, mixed));

    // Non-delegate argument types: not a comparison.
    std::vector<CSharp::TranslatedExpression> strings{
        StaticCallFixture::MakeArg(fixture.stringType),
        StaticCallFixture::MakeArg(fixture.stringType)};
    EXPECT_FALSE(CSharp::CallBuilder::IsDelegateEqualityComparison(*equality, strings));

    // A wrong name, a wrong declaring type, a non-operator, and the wrong
    // argument count all fail.
    auto addition = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Addition", fixture.delegateType,
        {delegateA, delegateA}, fixture.stringType);
    EXPECT_FALSE(CSharp::CallBuilder::IsDelegateEqualityComparison(*addition, sameType));
    auto stringEquality = fixture.MakeMethod(
        TS::SymbolKind::Operator, "op_Equality", fixture.stringType,
        {delegateA, delegateA}, fixture.stringType);
    EXPECT_FALSE(
        CSharp::CallBuilder::IsDelegateEqualityComparison(*stringEquality, sameType));
    auto notOperator = fixture.MakeMethod(
        TS::SymbolKind::Method, "op_Equality", fixture.delegateType,
        {delegateA, delegateA}, fixture.stringType);
    EXPECT_FALSE(
        CSharp::CallBuilder::IsDelegateEqualityComparison(*notOperator, sameType));
    std::vector<CSharp::TranslatedExpression> one{
        StaticCallFixture::MakeArg(delegateA)};
    EXPECT_FALSE(CSharp::CallBuilder::IsDelegateEqualityComparison(*equality, one));
}

} // namespace ILSpy::Tests
