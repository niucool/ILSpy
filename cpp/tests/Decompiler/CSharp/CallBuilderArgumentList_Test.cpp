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

// The CallBuilder::ArgumentList suite -- the CallBuilder prerequisite data holder:
// the length / actual-count bookkeeping, the primitive-value argument-name fill,
// the out-parameter OutVarResolveResult substitution, the named-argument expression
// wrapping with the implicit-out annotation, the anonymous-type name inference, and
// the no-named-or-optional assert -- over the MinimalCorlib fixture.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

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
namespace Util = ::ILSpy::Decompiler::Util;
namespace CSharp = ::ILSpy::Decompiler::CSharp;
using ArgumentList = ::ILSpy::Decompiler::CSharp::CallBuilder::ArgumentList;

// A translated identifier carrying a resolve result over `type`.
CSharp::TranslatedExpression MakeArg(TS::ITypePtr type, const std::string& name)
{
    auto* ident = new Syntax::IdentifierExpression(name);
    return CSharp::WithRR(CSharp::WithoutILInstruction(*ident),
                          std::make_shared<Sem::ResolveResult>(std::move(type)));
}

} // namespace

TEST(CallBuilderArgumentListTest, GetActualArgumentCountHonorsOptionalIndex)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    list.Arguments.push_back(MakeArg(intType, "a"));
    list.Arguments.push_back(MakeArg(intType, "b"));
    list.ExpectedParameters.push_back(new Impl::DefaultParameter(intType, "a"));
    list.ExpectedParameters.push_back(new Impl::DefaultParameter(intType, "b"));
    EXPECT_EQ(list.Length(), 2);
    EXPECT_EQ(list.GetArgumentResolveResults().size(), std::size_t{2});

    // A non-negative FirstOptionalArgumentIndex truncates the actual count.
    list.FirstOptionalArgumentIndex = 1;
    EXPECT_EQ(list.GetArgumentResolveResults().size(), std::size_t{1});
}

TEST(CallBuilderArgumentListTest, GetArgumentNamesFillsPrimitiveValueNames)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    list.Arguments.push_back(MakeArg(intType, "a"));
    list.Arguments.push_back(MakeArg(intType, "b"));
    list.ParameterNames = {"first", "second"};
    list.AddNamesToPrimitiveValues = true;
    list.IsPrimitiveValue = Util::BitSet(2);
    list.IsPrimitiveValue.Set(0);

    auto names = list.GetArgumentNames();
    ASSERT_TRUE(names.has_value());
    ASSERT_EQ(names->size(), std::size_t{2});
    EXPECT_EQ((*names)[0], "first");
    // The non-primitive argument keeps the null (empty) name.
    EXPECT_EQ((*names)[1], "");
}

TEST(CallBuilderArgumentListTest, GetArgumentResolveResultsSubstitutesOutVar)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    // An out parameter whose argument type is a managed reference.
    auto* ident = new Syntax::IdentifierExpression("a");
    list.Arguments.push_back(CSharp::WithRR(
        CSharp::WithoutILInstruction(*ident),
        std::make_shared<Sem::ByReferenceResolveResult>(
            intType, TS::ReferenceKind::Out)));
    list.ExpectedParameters.push_back(
        new Impl::DefaultParameter(intType, "a", nullptr, {},
                                   TS::ReferenceKind::Out));
    list.UseImplicitlyTypedOut = true;

    auto rrs = list.GetArgumentResolveResults();
    ASSERT_EQ(rrs.size(), std::size_t{1});
    EXPECT_NE(dynamic_cast<const Sem::OutVarResolveResult*>(rrs[0].get()), nullptr);
}

TEST(CallBuilderArgumentListTest, GetArgumentResolveResultsDirectReturnsTheArgumentRr)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    auto arg = MakeArg(intType, "a");
    const Sem::ResolveResult* expected = arg.ResolveResult();
    list.Arguments.push_back(arg);

    auto rrs = list.GetArgumentResolveResultsDirect();
    ASSERT_EQ(rrs.size(), std::size_t{1});
    EXPECT_EQ(rrs[0].get(), expected);
}

TEST(CallBuilderArgumentListTest, GetArgumentExpressionsWrapsNamedArguments)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    list.Arguments.push_back(MakeArg(intType, "a"));
    list.Arguments.push_back(MakeArg(intType, "b"));
    list.ArgumentNames = std::vector<std::string>{"x", ""};

    auto exprs = list.GetArgumentExpressions();
    ASSERT_EQ(exprs.size(), std::size_t{2});
    auto* named = dynamic_cast<Syntax::NamedArgumentExpression*>(exprs[0]);
    ASSERT_TRUE(named != nullptr);
    EXPECT_EQ(named->Name(), "x");
    // The empty name keeps the bare identifier.
    EXPECT_NE(dynamic_cast<Syntax::IdentifierExpression*>(exprs[1]), nullptr);
}

TEST(CallBuilderArgumentListTest, GetArgumentExpressionsAddsImplicitOutAnnotation)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    auto* ident = new Syntax::IdentifierExpression("a");
    CSharp::TranslatedExpression arg = CSharp::WithRR(
        CSharp::WithoutILInstruction(*ident),
        std::make_shared<Sem::ByReferenceResolveResult>(
            intType, TS::ReferenceKind::Out));
    list.Arguments.push_back(arg);
    list.UseImplicitlyTypedOut = true;

    auto exprs = list.GetArgumentExpressions();
    ASSERT_EQ(exprs.size(), std::size_t{1});
    EXPECT_NE(exprs[0]->Annotation<CSharp::UseImplicitlyTypedOutAnnotation>(), nullptr);
}

TEST(CallBuilderArgumentListTest, CanInferAnonymousTypePropertyNames)
{
    TS::SimpleCompilation compilation(Impl::MinimalCorlib::Instance(), {});
    auto intType = std::const_pointer_cast<TS::IType>(
        compilation.FindType(TS::KnownTypeCode::Int32).shared_from_this());

    ArgumentList list;
    list.Arguments.push_back(MakeArg(intType, "first"));
    list.Arguments.push_back(MakeArg(intType, "second"));
    list.ExpectedParameters.push_back(new Impl::DefaultParameter(intType, "first"));
    list.ExpectedParameters.push_back(new Impl::DefaultParameter(intType, "second"));
    EXPECT_TRUE(list.CanInferAnonymousTypePropertyNamesFromArguments());

    // A name mismatch breaks the inference.
    list.ExpectedParameters[1] = new Impl::DefaultParameter(intType, "other");
    EXPECT_FALSE(list.CanInferAnonymousTypePropertyNamesFromArguments());
}

TEST(CallBuilderArgumentListTest, CheckNoNamedOrOptionalArgumentsPassesForPlainList)
{
    ArgumentList list;
    EXPECT_NO_THROW(list.CheckNoNamedOrOptionalArguments());
}

} // namespace ILSpy::Tests
