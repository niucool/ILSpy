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

// The CallBuilder span-based string-concat shape (CallBuilder.cs lines
// 252-328): the `IsReadOnlySpanCharCtor` helper matrix, the
// `IsSpanBasedStringConcat(CallInstruction, out operands)` extraction (the
// op_Implicit and newobj-ReadOnlySpan<char> argument arms plus the
// first-string-index gate), and the `BuildStringConcat` left-associative
// `+` render -- over a real ExpressionBuilder + FakeMethod.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::CallBuilder;
using CSharp::ExpressionBuilder;
namespace IL = ::ILSpy::Decompiler::IL;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace CSharp = ::ILSpy::Decompiler::CSharp;

struct Fixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function_;

    Fixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}), usingScope(MakeScope()),
          settings(), run(&settings, usingScope)
    {
    }

    std::shared_ptr<CSharp::TypeSystem::UsingScope> MakeScope()
    {
        auto context = std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<CSharp::TypeSystem::UsingScope>(
            context, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    ExpressionBuilder MakeBuilder()
    {
        return ExpressionBuilder(nullptr, compilation, FixtureContext(), &function_,
                                 &settings, &run);
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
    // The `string.Concat(ReadOnlySpan<char>, ReadOnlySpan<char>)` method the
    // span-based shapes name.
    std::shared_ptr<Impl::FakeMethod> MakeConcatMethod()
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        method->SetName("Concat");
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::String));
        method->SetParameters(
            {std::make_shared<Impl::DefaultParameter>(SpanOfChar(), "left"),
             std::make_shared<Impl::DefaultParameter>(SpanOfChar(), "right")});
        method->SetReturnType(TypePtr(TS::KnownTypeCode::String));
        return method;
    }

    // The `op_Implicit(string -> ReadOnlySpan<char>)` conversion call the
    // compiler lowers `s + "literal"` through.
    std::shared_ptr<Impl::FakeMethod> MakeStringToSpanImplicit()
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Operator);
        method->SetName("op_Implicit");
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::ReadOnlySpanOfT));
        method->SetParameters({std::make_shared<Impl::DefaultParameter>(
            TypePtr(TS::KnownTypeCode::String), "value")});
        method->SetReturnType(SpanOfChar());
        return method;
    }

    // The `newobj ReadOnlySpan<char>(addressof(charValue))` constructor.
    std::shared_ptr<Impl::FakeMethod> MakeReadOnlySpanCharCtor()
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Constructor);
        method->SetName(".ctor");
        method->SetIsStatic(false);
        method->SetDeclaringType(SpanOfChar());
        method->SetParameters(
            {std::make_shared<Impl::DefaultParameter>(
                std::make_shared<TS::ByReferenceType>(charType), "value")});
        method->SetReturnType(TypePtr(TS::KnownTypeCode::Void));
        return method;
    }

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(
            IL::VariableKind::Local, TypePtr(code));
        local->Name = name;
        return local;
    }

    TS::ITypePtr spanDefinition = TypePtr(TS::KnownTypeCode::ReadOnlySpanOfT);
    TS::ITypePtr charType = TypePtr(TS::KnownTypeCode::Char);

private:
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
};} // namespace

TEST(CallBuilderStringConcatTest, IsReadOnlySpanCharCtorMatrix)
{
    Fixture fixture;
    auto ctor = fixture.MakeReadOnlySpanCharCtor();
    EXPECT_TRUE(IL::IsReadOnlySpanCharCtor(*ctor));

    // A non-constructor symbol kind fails the gate.
    auto notCtor = fixture.MakeStringToSpanImplicit();
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(*notCtor));

    // A ReadOnlySpan<int> declaring type fails the element check.
    auto spanOfInt = std::make_shared<TS::ParameterizedType>(
        fixture.spanDefinition, std::vector<TS::ITypePtr>{fixture.TypePtr(TS::KnownTypeCode::Int32)});
    auto intCtor = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    intCtor->SetName(".ctor");
    intCtor->SetIsStatic(false);
    intCtor->SetDeclaringType(spanOfInt);
    intCtor->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(
            std::make_shared<TS::ByReferenceType>(fixture.TypePtr(TS::KnownTypeCode::Int32)),
            "value")});
    intCtor->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(*intCtor));

    // A plain char parameter (not a by-reference wrapper) fails the check.
    auto plainParam = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Constructor);
    plainParam->SetName(".ctor");
    plainParam->SetIsStatic(false);
    plainParam->SetDeclaringType(fixture.SpanOfChar());
    plainParam->SetParameters({std::make_shared<Impl::DefaultParameter>(
        fixture.charType, "value")});
    plainParam->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    EXPECT_FALSE(IL::IsReadOnlySpanCharCtor(*plainParam));
}

TEST(CallBuilderStringConcatTest, ExtractionAcceptsTwoOpImplicitArguments)
{
    Fixture fixture;
    auto concat = fixture.MakeConcatMethod();
    auto implicitConversion = fixture.MakeStringToSpanImplicit();
    auto outer = std::make_unique<IL::Call>(concat);
    auto first = std::make_unique<IL::Call>(implicitConversion);
    first->AddArg(std::make_unique<IL::LdStr>("a"));
    auto second = std::make_unique<IL::Call>(implicitConversion);
    second->AddArg(std::make_unique<IL::LdStr>("b"));
    outer->AddArg(std::move(first));
    outer->AddArg(std::move(second));

    std::vector<CallBuilder::SpanConcatOperand> operands;
    ASSERT_TRUE(CallBuilder::IsSpanBasedStringConcat(*outer, operands));
    ASSERT_EQ(operands.size(), 2u);
    EXPECT_EQ(operands[0].TypeCode, TS::KnownTypeCode::String);
    EXPECT_EQ(operands[1].TypeCode, TS::KnownTypeCode::String);
}

TEST(CallBuilderStringConcatTest, ExtractionAcceptsTheNewObjReadOnlySpanArm)
{
    Fixture fixture;
    auto concat = fixture.MakeConcatMethod();
    auto implicitConversion = fixture.MakeStringToSpanImplicit();
    auto spanCtor = fixture.MakeReadOnlySpanCharCtor();
    auto chLocal = fixture.MakeLocal(TS::KnownTypeCode::Char, "ch");

    auto outer = std::make_unique<IL::Call>(concat);
    auto opImplicit = std::make_unique<IL::Call>(implicitConversion);
    opImplicit->AddArg(std::make_unique<IL::LdStr>("a"));
    auto newObj = std::make_unique<IL::Call>(spanCtor);
    newObj->IsNewObj = true;
    newObj->AddArg(std::make_unique<IL::AddressOf>(
        std::make_unique<IL::LdLoc>(chLocal), fixture.charType));
    outer->AddArg(std::move(opImplicit));
    outer->AddArg(std::move(newObj));

    std::vector<CallBuilder::SpanConcatOperand> operands;
    ASSERT_TRUE(CallBuilder::IsSpanBasedStringConcat(*outer, operands));
    ASSERT_EQ(operands.size(), 2u);
    EXPECT_EQ(operands[0].TypeCode, TS::KnownTypeCode::String);
    EXPECT_EQ(operands[1].TypeCode, TS::KnownTypeCode::Char);
}

TEST(CallBuilderStringConcatTest, ExtractionRejectsFirstStringArgumentPastIndexOne)
{
    Fixture fixture;
    auto concat = fixture.MakeConcatMethod();
    auto implicitConversion = fixture.MakeStringToSpanImplicit();
    auto spanCtor = fixture.MakeReadOnlySpanCharCtor();
    auto chLocal = fixture.MakeLocal(TS::KnownTypeCode::Char, "ch");

    auto outer = std::make_unique<IL::Call>(concat);
    auto firstNewObj = std::make_unique<IL::Call>(spanCtor);
    firstNewObj->IsNewObj = true;
    firstNewObj->AddArg(std::make_unique<IL::AddressOf>(
        std::make_unique<IL::LdLoc>(chLocal), fixture.charType));
    auto secondNewObj = std::make_unique<IL::Call>(spanCtor);
    secondNewObj->IsNewObj = true;
    secondNewObj->AddArg(std::make_unique<IL::AddressOf>(
        std::make_unique<IL::LdLoc>(chLocal), fixture.charType));
    auto opImplicit = std::make_unique<IL::Call>(implicitConversion);
    opImplicit->AddArg(std::make_unique<IL::LdStr>("a"));
    outer->AddArg(std::move(firstNewObj));
    outer->AddArg(std::move(secondNewObj));
    outer->AddArg(std::move(opImplicit));

    std::vector<CallBuilder::SpanConcatOperand> operands;
    EXPECT_FALSE(CallBuilder::IsSpanBasedStringConcat(*outer, operands))
        << "the first string operand sits at index 2, past the C# "
           "`firstStringArgumentIndex <= 1` gate";
}

TEST(CallBuilderStringConcatTest, ExtractionRejectsForeignArguments)
{
    Fixture fixture;
    auto concat = fixture.MakeConcatMethod();
    auto chLocal = fixture.MakeLocal(TS::KnownTypeCode::Char, "ch");

    auto outer = std::make_unique<IL::Call>(concat);
    outer->AddArg(std::make_unique<IL::LdLoc>(chLocal));

    std::vector<CallBuilder::SpanConcatOperand> operands;
    EXPECT_FALSE(CallBuilder::IsSpanBasedStringConcat(*outer, operands));
}

TEST(CallBuilderStringConcatTest, ExtractionRejectsNonSpanConcatMethods)
{
    Fixture fixture;
    // A string.Concat(string, string) shape (not the span-based overload):
    // the method-only gate fails before the argument arms run.
    auto stringConcat = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    stringConcat->SetName("Concat");
    stringConcat->SetIsStatic(true);
    stringConcat->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    stringConcat->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::String), "left"),
         std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::String), "right")});
    stringConcat->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));
    auto implicitConversion = fixture.MakeStringToSpanImplicit();

    auto outer = std::make_unique<IL::Call>(stringConcat);
    auto opImplicit = std::make_unique<IL::Call>(implicitConversion);
    opImplicit->AddArg(std::make_unique<IL::LdStr>("a"));
    outer->AddArg(std::move(opImplicit));
    outer->AddArg(std::make_unique<IL::LdStr>("b"));

    std::vector<CallBuilder::SpanConcatOperand> operands;
    EXPECT_FALSE(CallBuilder::IsSpanBasedStringConcat(*outer, operands));
}

TEST(CallBuilderStringConcatTest, BuildStringConcatRendersLeftAssociativeAdds)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto concat = fixture.MakeConcatMethod();
    auto chLocal = fixture.MakeLocal(TS::KnownTypeCode::Char, "ch");

    std::vector<CallBuilder::SpanConcatOperand> operands;
    auto firstStr = std::make_unique<IL::LdStr>("a");
    auto chLoad = std::make_unique<IL::LdLoc>(chLocal);
    auto lastStr = std::make_unique<IL::LdStr>("c");
    operands.push_back({firstStr.get(), TS::KnownTypeCode::String});
    operands.push_back({chLoad.get(), TS::KnownTypeCode::Char});
    operands.push_back({lastStr.get(), TS::KnownTypeCode::String});

    auto result = callBuilder.BuildStringConcat(*concat, operands);

    // The outer node folds (a + ch) + "c": a left-associative chain with the
    // shared MemberResolveResult on the Concat method.
    auto* outer = dynamic_cast<Syntax::BinaryOperatorExpression*>(result.Expression());
    ASSERT_TRUE(outer != nullptr);
    EXPECT_EQ(outer->Operator(), Syntax::BinaryOperatorType::Add);
    auto* left = dynamic_cast<Syntax::BinaryOperatorExpression*>(outer->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Operator(), Syntax::BinaryOperatorType::Add);
    auto* first = dynamic_cast<Syntax::PrimitiveExpression*>(left->Left());
    ASSERT_TRUE(first != nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(first->Value()));
    EXPECT_EQ(std::get<std::string>(first->Value()), "a");
    auto* middle = dynamic_cast<Syntax::IdentifierExpression*>(left->Right());
    ASSERT_TRUE(middle != nullptr);
    EXPECT_EQ(middle->Identifier(), "ch");
    auto* right = dynamic_cast<Syntax::PrimitiveExpression*>(outer->Right());
    ASSERT_TRUE(right != nullptr);
    ASSERT_TRUE(std::holds_alternative<std::string>(right->Value()));
    EXPECT_EQ(std::get<std::string>(right->Value()), "c");

    // Both Add nodes carry the same MemberResolveResult (the C# binds every
    // BinaryOperatorExpression to the shared `rr`).
    const auto* outerRR =
        dynamic_cast<const Sem::MemberResolveResult*>(CSharp::GetResolveResult(*outer));
    const auto* leftRR =
        dynamic_cast<const Sem::MemberResolveResult*>(CSharp::GetResolveResult(*left));
    ASSERT_TRUE(outerRR != nullptr);
    ASSERT_TRUE(leftRR != nullptr);
    EXPECT_EQ(outerRR, leftRR);
    EXPECT_EQ(outerRR->Member()->Name(), "Concat");
}

} // namespace ILSpy::Tests