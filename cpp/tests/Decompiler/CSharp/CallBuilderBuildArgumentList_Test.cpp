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

// The CallBuilder::BuildArgumentList suite (CallBuilder.cs lines 941-1052): the
// positional argument translation, the instance-call `this` skip, the
// boolean-constant primitive-value flag, the optional-argument index
// bookkeeping (and its setting gate), and the loud deferral of the named-argument
// and params-expansion paths -- over a real ExpressionBuilder + FakeMethod.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::ExpressionBuilder;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;
namespace CSharp = ::ILSpy::Decompiler::CSharp;

// A parameter specification for the FakeMethod builder.
struct ParamSpec {
    std::string name;
    TS::KnownTypeCode type;
    bool isOptional = false;
    bool isParams = false;
    std::any defaultValue;
};

// The CallBuilder fixture: a MinimalCorlib compilation, a working
// ExpressionBuilder, and the FakeMethod builder.
struct CallBuilderFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    DecompilerSettings settings;
    DecompileRun run;

    CallBuilderFixture()
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

    std::shared_ptr<IL::ILVariable> MakeLocal(TS::KnownTypeCode code, const char* name)
    {
        auto local = std::make_shared<IL::ILVariable>(IL::VariableKind::Local,
                                                      TypePtr(code));
        local->Name = name;
        return local;
    }

    std::shared_ptr<Impl::FakeMethod> MakeMethod(
        const std::string& name, bool isStatic, TS::KnownTypeCode declaringCode,
        TS::KnownTypeCode returnCode, std::vector<ParamSpec> params)
    {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        method->SetDeclaringType(TypePtr(declaringCode));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (const ParamSpec& spec : params)
        {
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                TypePtr(spec.type), spec.name, nullptr,
                std::vector<const TS::IAttribute*>{}, TS::ReferenceKind::None,
                spec.isParams, spec.isOptional, spec.defaultValue));
        }
        method->SetParameters(parameters);
        method->SetReturnType(TypePtr(returnCode));
        return method;
    }

private:
    IL::ILFunction function_;
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

TEST(CallBuilderBuildArgumentListTest, PositionalArgumentsFillTheList)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "Add", true, TS::KnownTypeCode::Int32, TS::KnownTypeCode::Int32,
        {{"a", TS::KnownTypeCode::Int32}, {"b", TS::KnownTypeCode::Int32}});
    auto a = fixture.MakeLocal(TS::KnownTypeCode::Int32, "a");
    auto b = fixture.MakeLocal(TS::KnownTypeCode::Int32, "b");
    IL::LdLoc aLoad(a), bLoad(b);
    std::vector<IL::ILInstruction*> args{&aLoad, &bLoad};

    auto list = callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, std::nullopt);

    EXPECT_EQ(list.Length(), 2);
    ASSERT_EQ(list.ExpectedParameters.size(), std::size_t{2});
    EXPECT_EQ(list.ExpectedParameters[0]->Name(), "a");
    EXPECT_EQ(list.ExpectedParameters[1]->Name(), "b");
    ASSERT_EQ(list.ParameterNames.size(), std::size_t{2});
    EXPECT_EQ(list.ParameterNames[0], "a");
    EXPECT_EQ(list.ParameterNames[1], "b");
    EXPECT_FALSE(list.ArgumentNames.has_value());
    EXPECT_FALSE(list.ArgumentToParameterMap.has_value());
    EXPECT_FALSE(list.IsExpandedForm);
    EXPECT_FALSE(list.IsPrimitiveValue.Any());
    EXPECT_EQ(list.FirstOptionalArgumentIndex, -2);
    EXPECT_TRUE(list.UseImplicitlyTypedOut);
    EXPECT_TRUE(list.AddNamesToPrimitiveValues);
}

TEST(CallBuilderBuildArgumentListTest, InstanceCallSkipsTheThisArgument)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "Get", false, TS::KnownTypeCode::String, TS::KnownTypeCode::Int32,
        {{"index", TS::KnownTypeCode::Int32}});
    auto self = fixture.MakeLocal(TS::KnownTypeCode::String, "self");
    auto index = fixture.MakeLocal(TS::KnownTypeCode::Int32, "index");
    IL::LdLoc selfLoad(self), indexLoad(index);
    std::vector<IL::ILInstruction*> args{&selfLoad, &indexLoad};

    auto list = callBuilder.BuildArgumentList({}, nullptr, *method, 1, args, std::nullopt);

    EXPECT_EQ(list.Length(), 1);
    ASSERT_EQ(list.ExpectedParameters.size(), std::size_t{1});
    EXPECT_EQ(list.ExpectedParameters[0]->Name(), "index");
}

TEST(CallBuilderBuildArgumentListTest, BooleanConstantIsFlaggedPrimitive)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "Set", true, TS::KnownTypeCode::String, TS::KnownTypeCode::Void,
        {{"flag", TS::KnownTypeCode::Boolean}});
    IL::LdcI4 one(1);
    std::vector<IL::ILInstruction*> args{&one};

    auto list = callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, std::nullopt);

    EXPECT_TRUE(list.IsPrimitiveValue.Any());
}

TEST(CallBuilderBuildArgumentListTest, OptionalArgumentIndexIsTracked)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "M", true, TS::KnownTypeCode::String, TS::KnownTypeCode::Void,
        {{"a", TS::KnownTypeCode::Int32, /*isOptional=*/true, /*isParams=*/false,
          std::any(std::int32_t(5))}});
    IL::LdcI4 five(5);
    std::vector<IL::ILInstruction*> args{&five};

    auto list = callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, std::nullopt);

    EXPECT_EQ(list.FirstOptionalArgumentIndex, 0);
}

TEST(CallBuilderBuildArgumentListTest, NonMatchingOptionalArgumentResetsTheIndex)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "M", true, TS::KnownTypeCode::String, TS::KnownTypeCode::Void,
        {{"a", TS::KnownTypeCode::Int32, /*isOptional=*/true, /*isParams=*/false,
          std::any(std::int32_t(5))}});
    IL::LdcI4 six(6);
    std::vector<IL::ILInstruction*> args{&six};

    auto list = callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, std::nullopt);

    EXPECT_EQ(list.FirstOptionalArgumentIndex, -2);
}

TEST(CallBuilderBuildArgumentListTest, OptionalArgumentsDisabledForbidsTheIndex)
{
    CallBuilderFixture fixture;
    fixture.settings.SetOptionalArguments(false);
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "M", true, TS::KnownTypeCode::String, TS::KnownTypeCode::Void,
        {{"a", TS::KnownTypeCode::Int32, /*isOptional=*/true, /*isParams=*/false,
          std::any(std::int32_t(5))}});
    IL::LdcI4 five(5);
    std::vector<IL::ILInstruction*> args{&five};

    auto list = callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, std::nullopt);

    EXPECT_EQ(list.FirstOptionalArgumentIndex, -1);
}

TEST(CallBuilderBuildArgumentListTest, NamedArgumentMapPathThrows)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod("M", true, TS::KnownTypeCode::String,
                                     TS::KnownTypeCode::Void, {});
    std::vector<IL::ILInstruction*> args;
    std::vector<int> map;

    EXPECT_THROW(
        callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, map),
        std::logic_error);
}

TEST(CallBuilderBuildArgumentListTest, ParamsExpansionPathThrows)
{
    CallBuilderFixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod(
        "M", true, TS::KnownTypeCode::String, TS::KnownTypeCode::Void,
        {{"values", TS::KnownTypeCode::Int32, /*isOptional=*/false, /*isParams=*/true}});
    IL::LdcI4 one(1);
    std::vector<IL::ILInstruction*> args{&one};

    EXPECT_THROW(
        callBuilder.BuildArgumentList({}, nullptr, *method, 0, args, std::nullopt),
        std::logic_error);
}

} // namespace ILSpy::Tests
