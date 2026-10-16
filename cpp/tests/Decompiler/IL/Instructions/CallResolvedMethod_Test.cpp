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

// The Call resolved-method construction form (the `Call(IMethod, bool)` ctor):
// the stand-in fields derived from a real IMethod -- the dump name, the
// declaring/return/parameter type handles, the instance-call and newobj flags,
// the operator flag, the method-spec type-argument count, and the ResultType
// (the CallBuilder prerequisite: the resolved IMethod the Build path consumes).
// The seed string form is covered too (Method stays null).

#include "Decompiler/IL/Instructions/Call.hpp"

#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
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
namespace IL = ::ILSpy::Decompiler::IL;

// A FakeMethod builder over the MinimalCorlib compilation (the
// ExpressionBuilderUserDefinedLogic_Test shape).
struct CallFixture {
    TS::SimpleCompilation compilation;
    CallFixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    std::shared_ptr<Impl::FakeMethod> MakeMethod(
        TS::SymbolKind symbolKind, const std::string& name,
        TS::KnownTypeCode declaringCode, bool isStatic,
        TS::KnownTypeCode returnCode,
        std::vector<TS::KnownTypeCode> parameterCodes)
    {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, symbolKind);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        method->SetDeclaringType(TypePtr(declaringCode));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (TS::KnownTypeCode code : parameterCodes)
            parameters.push_back(
                std::make_shared<Impl::DefaultParameter>(TypePtr(code), "arg"));
        method->SetParameters(parameters);
        method->SetReturnType(TypePtr(returnCode));
        return method;
    }
};

} // namespace

TEST(CallResolvedMethodTest, InstanceMethodDerivesEveryStandInField)
{
    CallFixture fixture;
    auto method = fixture.MakeMethod(TS::SymbolKind::Method, "Substring",
                                     TS::KnownTypeCode::String, false,
                                     TS::KnownTypeCode::String,
                                     {TS::KnownTypeCode::Int32});
    IL::Call call(method);

    EXPECT_EQ(call.Method.get(), method.get());
    EXPECT_EQ(call.MethodName, "System.String::Substring");
    ASSERT_TRUE(call.DeclaringType != nullptr);
    EXPECT_EQ(call.DeclaringType->ReflectionName(), "System.String");
    ASSERT_TRUE(call.ReturnIType != nullptr);
    EXPECT_EQ(call.ReturnIType->ReflectionName(), "System.String");
    ASSERT_EQ(call.ParameterIType.size(), std::size_t{1});
    ASSERT_TRUE(call.ParameterIType[0] != nullptr);
    EXPECT_EQ(call.ParameterIType[0]->ReflectionName(), "System.Int32");
    EXPECT_TRUE(call.IsInstanceCall);
    EXPECT_FALSE(call.IsNewObj);
    EXPECT_FALSE(call.IsOperator);
    EXPECT_EQ(call.TypeArgumentsCount, 0);
    EXPECT_EQ(call.ReturnType, IL::StackType::O);
    EXPECT_EQ(call.ResultType(), IL::StackType::O);
}

TEST(CallResolvedMethodTest, StaticMethodIsNotAnInstanceCall)
{
    CallFixture fixture;
    auto method = fixture.MakeMethod(TS::SymbolKind::Method, "Concat",
                                     TS::KnownTypeCode::String, true,
                                     TS::KnownTypeCode::String,
                                     {TS::KnownTypeCode::String,
                                      TS::KnownTypeCode::String});
    IL::Call call(method);

    EXPECT_FALSE(call.IsInstanceCall);
    EXPECT_FALSE(call.IsNewObj);
    EXPECT_EQ(call.ParameterIType.size(), std::size_t{2});
    EXPECT_EQ(call.ReturnType, IL::StackType::O);
}

TEST(CallResolvedMethodTest, NewObjUsesTheDeclaringTypeStackType)
{
    CallFixture fixture;
    // A value-type constructor: the C# ResultType for NewObj is
    // DeclaringType.GetStackType(), not the declared void return.
    auto ctor = fixture.MakeMethod(TS::SymbolKind::Constructor, ".ctor",
                                   TS::KnownTypeCode::Int32, false,
                                   TS::KnownTypeCode::Void,
                                   {TS::KnownTypeCode::Int32});
    IL::Call call(ctor, /*isNewObj=*/true);

    EXPECT_TRUE(call.IsNewObj);
    EXPECT_FALSE(call.IsInstanceCall);
    EXPECT_TRUE(ctor->IsConstructor());
    EXPECT_EQ(call.ReturnType, IL::StackType::I4);
    EXPECT_EQ(call.ResultType(), IL::StackType::I4);
}

TEST(CallResolvedMethodTest, OperatorMethodSetsTheOperatorFlag)
{
    CallFixture fixture;
    auto method = fixture.MakeMethod(TS::SymbolKind::Operator, "op_Addition",
                                     TS::KnownTypeCode::Int32, true,
                                     TS::KnownTypeCode::Int32,
                                     {TS::KnownTypeCode::Int32,
                                      TS::KnownTypeCode::Int32});
    IL::Call call(method);

    EXPECT_TRUE(call.IsOperator);
    EXPECT_EQ(call.MethodName, "System.Int32::op_Addition");
    ASSERT_TRUE(call.Method->IsOperator());
}

TEST(CallResolvedMethodTest, TypeArgumentsCountFollowsTheMethod)
{
    CallFixture fixture;
    auto method = fixture.MakeMethod(TS::SymbolKind::Method, "CreateInstance",
                                     TS::KnownTypeCode::Object, true,
                                     TS::KnownTypeCode::Object, {});
    method->SetTypeParameters(
        {Impl::DummyTypeParameter::GetMethodTypeParameter(0)});
    IL::Call call(method);

    EXPECT_EQ(call.TypeArgumentsCount, 1);
}

TEST(CallResolvedMethodTest, SeedStringFormKeepsTheStandInShape)
{
    IL::Call call("Foo.Bar::Baz");

    EXPECT_EQ(call.Method, nullptr);
    EXPECT_EQ(call.MethodName, "Foo.Bar::Baz");
    EXPECT_FALSE(call.IsInstanceCall);
    EXPECT_FALSE(call.IsNewObj);
    EXPECT_FALSE(call.IsOperator);
    EXPECT_EQ(call.TypeArgumentsCount, 0);
    EXPECT_EQ(call.ReturnType, IL::StackType::Unknown);
}

} // namespace ILSpy::Tests
