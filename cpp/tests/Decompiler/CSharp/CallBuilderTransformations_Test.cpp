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

// The CallBuilder overload-resolution front end (CallBuilder.cs lines
// 1138-1556): the CallTransformation flags, the IsPossibleExtensionMethodCall
// OnNull / WrapInAsRefReadOnly / EnforceExplicitIn / CastArguments /
// CanInferTypeArgumentsFromArguments helpers, the IsUnambiguousCall driver,
// and the GetRequiredTransformationsForCall fallback cascade -- over a real
// ExpressionBuilder + FakeMethod. The cascade scenarios drive a FakeMethod the
// resolver cannot resolve (the simple-name lookup finds nothing), so the loop
// exhausts its fallbacks and gives up -- the state the tests pin is the
// cascade's observable mutations (the optional-argument reset, the argument
// casts, the AsRefReadOnly wrap) and the handed-back flags.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
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

    // A plain static method with the given parameter types (the resolver
    // cannot resolve the FakeMethod by simple name, so the cascade scenarios
    // deterministically exhaust their fallbacks).
    std::shared_ptr<Impl::FakeMethod> MakeMethod(const char* name,
                                                 std::vector<TS::ITypePtr> paramTypes)
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(true);
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::String));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (TS::ITypePtr& type : paramTypes)
            parameters.push_back(
                std::make_shared<Impl::DefaultParameter>(std::move(type), "arg"));
        method->SetParameters(parameters);
        method->SetReturnType(TypePtr(TS::KnownTypeCode::String));
        return method;
    }

    CSharp::TranslatedExpression MakeArg(TS::KnownTypeCode code, const char* name)
    {
        auto* ident = new Syntax::IdentifierExpression(name);
        return CSharp::WithRR(
            CSharp::WithoutILInstruction(*ident),
            std::make_shared<Sem::ResolveResult>(TypePtr(code)));
    }

    static CSharp::TranslatedExpression MakeNullArg()
    {
        auto* ident = new Syntax::NullReferenceExpression();
        return CSharp::WithRR(
            CSharp::WithoutILInstruction(*ident),
            std::make_shared<Sem::ResolveResult>(TS::UnknownType()));
    }

    // Pushes `argument` and the matching (null) ExpectedParameters slot: a
    // real BuildArgumentList-produced list keeps the two arrays parallel, and
    // GetArgumentResolveResults indexes both.
    static void AddArgument(CallBuilder::ArgumentList& list,
                            CSharp::TranslatedExpression argument)
    {
        list.Arguments.push_back(std::move(argument));
        list.ExpectedParameters.push_back(nullptr);
    }

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
};

} // namespace

TEST(CallBuilderTransformationsTest, IsPossibleExtensionMethodCallOnNullMatrix)
{
    Fixture fixture;
    // The extension method: a LookupMethod with the extension flag set (the
    // FakeMethod hardcodes IsExtensionMethod false; the LookupStubs stub
    // carries the settable flag).
    auto extension = std::make_shared<TS::TestSupport::LookupMethod>(
        "M", fixture.compilation);
    extension->SetIsExtensionMethod(true);
    auto nonExtension = fixture.MakeMethod("M", {fixture.TypePtr(TS::KnownTypeCode::String)});

    std::vector<CSharp::TranslatedExpression> nullFirst{Fixture::MakeNullArg(),
                                                        fixture.MakeArg(TS::KnownTypeCode::String, "b")};
    std::vector<CSharp::TranslatedExpression> plainFirst{fixture.MakeArg(TS::KnownTypeCode::String, "a"),
                                                         fixture.MakeArg(TS::KnownTypeCode::String, "b")};

    EXPECT_TRUE(CallBuilder::IsPossibleExtensionMethodCallOnNull(*extension, nullFirst));
    EXPECT_FALSE(CallBuilder::IsPossibleExtensionMethodCallOnNull(*nonExtension, nullFirst))
        << "a non-extension method is never a null-target extension call";
    EXPECT_FALSE(CallBuilder::IsPossibleExtensionMethodCallOnNull(*extension, plainFirst))
        << "a non-null first argument is not a null-target extension call";
}

TEST(CallBuilderTransformationsTest, WrapInAsRefReadOnlyShape)
{
    Fixture fixture;
    auto argument = fixture.MakeArg(TS::KnownTypeCode::Int32, "value");

    auto wrapped = CallBuilder::WrapInAsRefReadOnly(std::move(argument));

    auto* direction =
        dynamic_cast<Syntax::DirectionExpression*>(wrapped.Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::In);
    auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(direction->Expression());
    ASSERT_TRUE(invocation != nullptr);
    auto* target = dynamic_cast<Syntax::IdentifierExpression*>(invocation->Target());
    ASSERT_TRUE(target != nullptr);
    EXPECT_EQ(target->Identifier(), "ILSpyHelper_AsRefReadOnly");
    ASSERT_EQ(invocation->Arguments().Count(), 1);
    EXPECT_NE(dynamic_cast<Syntax::IdentifierExpression*>(invocation->Arguments().At(0)),
              nullptr);
    const auto* byRef =
        dynamic_cast<const Sem::ByReferenceResolveResult*>(wrapped.ResolveResult());
    ASSERT_TRUE(byRef != nullptr);
    (void)byRef;
}

TEST(CallBuilderTransformationsTest, EnforceExplicitInWrapsOnlyInParameters)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::TranslatedExpression target;
    CSharp::CallBuilder callBuilder(builder, fixture.settings);

    // The `in` parameter: a ByReferenceType-wrapped Int32 (the C# `in T`
    // parameter shape) plus a plain Int32 twin.
    auto inParameter = std::make_shared<Impl::DefaultParameter>(
        std::make_shared<TS::ByReferenceType>(fixture.TypePtr(TS::KnownTypeCode::Int32)),
        "inValue", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::In);
    std::vector<CSharp::TranslatedExpression> arguments{fixture.MakeArg(TS::KnownTypeCode::Int32, "a")};
    std::vector<const TS::IParameter*> expected{inParameter.get()};
    callBuilder.EnforceExplicitIn(arguments, expected);

    ASSERT_EQ(arguments.size(), 1u);
    auto* direction =
        dynamic_cast<Syntax::DirectionExpression*>(arguments[0].Expression());
    ASSERT_TRUE(direction != nullptr);
    EXPECT_EQ(direction->FieldDirection(), Syntax::FieldDirection::In);
    EXPECT_NE(dynamic_cast<Syntax::InvocationExpression*>(direction->Expression()),
              nullptr);

    // A plain (non-in) parameter leaves the argument alone.
    auto plainParameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Int32), "value");
    std::vector<CSharp::TranslatedExpression> untouched{fixture.MakeArg(TS::KnownTypeCode::Int32, "a")};
    std::vector<const TS::IParameter*> plainExpected{plainParameter.get()};
    callBuilder.EnforceExplicitIn(untouched, plainExpected);
    EXPECT_EQ(dynamic_cast<Syntax::DirectionExpression*>(untouched[0].Expression()),
              nullptr);
}

TEST(CallBuilderTransformationsTest, CastArgumentsConvertsToParameterTypes)
{
    Fixture fixture;
    // An Int32 argument against a Long parameter: the cascade's cast arm.
    auto longParameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Int64), "value");
    std::vector<CSharp::TranslatedExpression> arguments{fixture.MakeArg(TS::KnownTypeCode::Int32, "a")};
    std::vector<const TS::IParameter*> expected{longParameter.get()};
    auto builder = fixture.MakeBuilder();
    CSharp::CallBuilder callBuilder(builder, fixture.settings);
    callBuilder.CastArguments(arguments, expected);

    ASSERT_EQ(arguments.size(), 1u);
    EXPECT_EQ(&arguments[0].Type(), &longParameter->Type());

    // The `in long` parameter: the cast target is the ByReferenceType's
    // element (the C# `brt.ElementType` unwrap).
    auto inLongParameter = std::make_shared<Impl::DefaultParameter>(
        std::make_shared<TS::ByReferenceType>(fixture.TypePtr(TS::KnownTypeCode::Int64)),
        "inValue", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::In);
    std::vector<CSharp::TranslatedExpression> inArguments{fixture.MakeArg(TS::KnownTypeCode::Int32, "a")};
    std::vector<const TS::IParameter*> inExpected{inLongParameter.get()};
    callBuilder.CastArguments(inArguments, inExpected);
    EXPECT_EQ(&inArguments[0].Type(),
              fixture.TypePtr(TS::KnownTypeCode::Int64).get());
}

TEST(CallBuilderTransformationsTest, CanInferTypeArgumentsFromArguments)
{
    Fixture fixture;
    // M<T>(T): an Int32 argument infers T.
    auto tParam = std::make_shared<TS::TestSupport::LookupTypeParameter>("T");
    tParam->SetIndex(0);
    auto genericMethod = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    genericMethod->SetName("M");
    genericMethod->SetIsStatic(true);
    genericMethod->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    genericMethod->SetTypeParameters({tParam});
    genericMethod->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(tParam, "x")});
    genericMethod->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));

    CallBuilder::ArgumentList inferable;
    Fixture::AddArgument(inferable, fixture.MakeArg(TS::KnownTypeCode::Int32, "a"));
    EXPECT_TRUE(CallBuilder::CanInferTypeArgumentsFromArguments(
        *genericMethod, inferable, fixture.MakeBuilder()));

    // M<T>(object): no method type parameter appears in the parameters, so
    // inference fails.
    auto nonInferableMethod = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    nonInferableMethod->SetName("M");
    nonInferableMethod->SetIsStatic(true);
    nonInferableMethod->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    nonInferableMethod->SetTypeParameters({tParam});
    nonInferableMethod->SetParameters({std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Object), "x")});
    nonInferableMethod->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));
    CallBuilder::ArgumentList notInferable;
    Fixture::AddArgument(notInferable, fixture.MakeArg(TS::KnownTypeCode::Int32, "a"));
    EXPECT_FALSE(CallBuilder::CanInferTypeArgumentsFromArguments(
        *nonInferableMethod, notInferable, fixture.MakeBuilder()));

    // A non-generic method short-circuits to true.
    auto nonGeneric = fixture.MakeMethod("M", {fixture.TypePtr(TS::KnownTypeCode::Int32)});
    CallBuilder::ArgumentList plain;
    Fixture::AddArgument(plain, fixture.MakeArg(TS::KnownTypeCode::Int32, "a"));
    EXPECT_TRUE(CallBuilder::CanInferTypeArgumentsFromArguments(
        *nonGeneric, plain, fixture.MakeBuilder()));
}

TEST(CallBuilderTransformationsTest,
     GetRequiredTransformationsResetsOptionalIndexAndGivesUp)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::TranslatedExpression target;
    CallBuilder callBuilder(builder, fixture.settings);
    auto longParameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Int64), "value");
    auto method = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("M");
    method->SetIsStatic(true);
    method->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    method->SetParameters({longParameter, longParameter});
    method->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));

    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int64, "a"));
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int64, "b"));
    list.ExpectedParameters[0] = longParameter.get();
    list.ExpectedParameters[1] = longParameter.get();
    list.FirstOptionalArgumentIndex = 1;

    const TS::IParameterizedMember* foundMethod = nullptr;
    CallBuilder::CallTransformation transform =
        callBuilder.GetRequiredTransformationsForCall(
            CallBuilder::ExpectedTargetDetails{}, *method,
            /*target=*/target, list,
            CallBuilder::CallTransformation::None, foundMethod);

    EXPECT_EQ(foundMethod, method.get());
    EXPECT_EQ(list.FirstOptionalArgumentIndex, -1)
        << "the cascade's optional-argument reset fires before giving up";
    EXPECT_EQ(static_cast<std::uint32_t>(transform),
              static_cast<std::uint32_t>(
                  CallBuilder::CallTransformation::NoOptionalArgumentAllowed
                  | CallBuilder::CallTransformation::NoNamedArgsForPrettiness));
}

TEST(CallBuilderTransformationsTest,
     GetRequiredTransformationsCastsArgumentsBeforeGivingUp)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::TranslatedExpression target;
    CallBuilder callBuilder(builder, fixture.settings);
    auto longParameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Int64), "value");
    auto method = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("M");
    method->SetIsStatic(true);
    method->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    method->SetParameters({longParameter});
    method->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));

    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int32, "a"));
    list.ExpectedParameters[0] = longParameter.get();

    const TS::IParameterizedMember* foundMethod = nullptr;
    callBuilder.GetRequiredTransformationsForCall(
        CallBuilder::ExpectedTargetDetails{}, *method,
        /*target=*/target, list,
        CallBuilder::CallTransformation::None, foundMethod);

    EXPECT_EQ(foundMethod, method.get());
    EXPECT_EQ(&list.Arguments[0].Type(), &longParameter->Type())
        << "the cascade's CastArguments arm converts the argument to the "
           "expected parameter type";
}

TEST(CallBuilderTransformationsTest,
     GetRequiredTransformationsRequireTargetForInstanceMethod)
{
    Fixture fixture;
    CSharp::TranslatedExpression target =
        fixture.MakeArg(TS::KnownTypeCode::String, "this");
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    auto method = fixture.MakeMethod("M", {fixture.TypePtr(TS::KnownTypeCode::Int64)});

    // An instance method over a ThisReferenceExpression target: the C#
    // `target.Expression is not ThisReferenceExpression -> false` init arm
    // leaves requireTarget false; the cascade's RequireTarget fallback arm
    // flips it back on (the C# `!requireTarget -> requireTarget = true`)
    // once the simple-name resolution has failed and the arguments are cast.
    // The expected parameters mirror the method's own (a real BuildArgumentList
    // always fills them; the C# CastArguments dereferences each entry).
    auto longParameter = std::make_shared<Impl::DefaultParameter>(
        fixture.TypePtr(TS::KnownTypeCode::Int64), "a");
    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int64, "a"));
    list.ExpectedParameters[0] = longParameter.get();

    const TS::IParameterizedMember* foundMethod = nullptr;
    CallBuilder::CallTransformation transform =
        callBuilder.GetRequiredTransformationsForCall(
            CallBuilder::ExpectedTargetDetails{}, *method,
            /*target=*/target, list,
            CallBuilder::CallTransformation::RequireTarget, foundMethod);

    EXPECT_EQ(foundMethod, method.get());
    EXPECT_EQ(
        static_cast<std::uint32_t>(transform & CallBuilder::CallTransformation::RequireTarget),
        static_cast<std::uint32_t>(CallBuilder::CallTransformation::RequireTarget));
}

TEST(CallBuilderTransformationsTest,
     GetRequiredTransformationsEnforceExplicitInArmFires)
{
    Fixture fixture;
    auto builder = fixture.MakeBuilder();
    CSharp::TranslatedExpression target;
    CallBuilder callBuilder(builder, fixture.settings);
    auto inParameter = std::make_shared<Impl::DefaultParameter>(
        std::make_shared<TS::ByReferenceType>(fixture.TypePtr(TS::KnownTypeCode::Int32)),
        "inValue", nullptr, std::vector<const TS::IAttribute*>{},
        TS::ReferenceKind::In);
    auto method = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    method->SetName("M");
    method->SetIsStatic(true);
    method->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    method->SetParameters({inParameter});
    method->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::String));

    CallBuilder::ArgumentList list;
    Fixture::AddArgument(list, fixture.MakeArg(TS::KnownTypeCode::Int32, "a"));
    list.ExpectedParameters[0] = inParameter.get();

    const TS::IParameterizedMember* foundMethod = nullptr;
    callBuilder.GetRequiredTransformationsForCall(
        CallBuilder::ExpectedTargetDetails{}, *method,
        /*target=*/target, list,
        CallBuilder::CallTransformation::EnforceExplicitIn, foundMethod);

    ASSERT_EQ(list.Arguments.size(), 1u);
    auto* direction =
        dynamic_cast<Syntax::DirectionExpression*>(list.Arguments[0].Expression());
    EXPECT_TRUE(direction != nullptr)
        << "the cascade's EnforceExplicitIn arm wraps the `in` argument";
}

} // namespace ILSpy::Tests