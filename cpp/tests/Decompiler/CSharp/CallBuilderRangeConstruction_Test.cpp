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

// The CallBuilder range-construction render (CallBuilder.cs lines 2245-2310) and
// the SyntheticRangeIndexAccessor wrapper (TypeSystem/Implementation/
// SyntheticRangeIndexer.cs) it slices through: the Range constructor and
// get_All/StartAt/EndAt arms (the `..`/`x..`/`..y` operators), the Index
// from-end constructor (`^x`), the named-argument gate, and the slicing arm.
// The synthetic-wrapper tests pin the IMethod surface the C# explicit
// interface implementations pin (the unconditional negatives, the re-built
// parameter list, and the wrapper equality).

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/Implementation/SyntheticRangeIndexAccessor.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <cstddef>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
using CSharp::CallBuilder;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;

struct RangeFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function_;
    std::vector<std::shared_ptr<Impl::DefaultParameter>> parameters_;

    RangeFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          scopelessContext(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharp::TypeSystem::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})),
          context_(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope)),
          settings(), run(&settings, usingScope)
    {
    }

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    CSharp::ExpressionBuilder MakeBuilder()
    {
        return CSharp::ExpressionBuilder(nullptr, compilation, *context_, &function_,
                                         &settings, &run);
    }

    // A static method named `name` on the Range/Index type with the given
    // parameter types (the KnownTypeCode lookups through the minimal corlib).
    std::shared_ptr<Impl::FakeMethod> MakeStaticMethod(
        const char* name, const TS::IType* declaringType,
        std::vector<TS::ITypePtr> paramTypes)
    {
        auto method = std::make_shared<Impl::FakeMethod>(
            compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(true);
        method->SetDeclaringType(std::const_pointer_cast<TS::IType>(
            declaringType->shared_from_this()));
        method->SetReturnType(TypePtr(TS::KnownTypeCode::Void));
        std::vector<std::shared_ptr<const TS::IParameter>> parameters;
        for (TS::ITypePtr& type : paramTypes)
            parameters.push_back(std::make_shared<Impl::DefaultParameter>(
                std::move(type), "arg"));
        method->SetParameters(parameters);
        return method;
    }

    CSharp::TranslatedExpression MakeArg(TS::KnownTypeCode code, const char* name)
    {
        auto* ident = new Syntax::IdentifierExpression(name);
        return CSharp::WithRR(
            CSharp::WithoutILInstruction(*ident),
            std::make_shared<Sem::ResolveResult>(TypePtr(code)));
    }
};

} // namespace

TEST(CallBuilderRangeConstructionTest, RangeConstructorRendersRangeOperator)
{
    RangeFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // The `System.Range(..start, ..end)` constructor shape: a static method
    // named ".ctor" on the Range type with two parameters resolves the
    // NewObj arm, rendering `start..end`.
    auto start = fixture.TypePtr(TS::KnownTypeCode::Index);
    auto end = fixture.TypePtr(TS::KnownTypeCode::Index);
    auto method =
        fixture.MakeStaticMethod(".ctor", fixture.TypePtr(TS::KnownTypeCode::Range)->GetDefinition(),
                                 {start, end});

    CallBuilder::ArgumentList argumentList;
    argumentList.Arguments.push_back(fixture.MakeArg(TS::KnownTypeCode::Index, "start"));
    argumentList.Arguments.push_back(fixture.MakeArg(TS::KnownTypeCode::Index, "end"));
    argumentList.ExpectedParameters.push_back(nullptr);
    argumentList.ExpectedParameters.push_back(nullptr);

    CSharp::ExpressionWithResolveResult result;
    CSharp::TranslatedExpression target;
    bool handled = callBuilder.HandleRangeConstruction(
        result, IL::OpCode::NewObj, *method, target, argumentList);

    ASSERT_TRUE(handled);
    auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(result.Expression());
    ASSERT_TRUE(binary != nullptr);
    EXPECT_EQ(binary->Operator(), Syntax::BinaryOperatorType::Range);
    auto* left = dynamic_cast<Syntax::IdentifierExpression*>(binary->Left());
    ASSERT_TRUE(left != nullptr);
    EXPECT_EQ(left->Identifier(), "start");
    auto* right = dynamic_cast<Syntax::IdentifierExpression*>(binary->Right());
    ASSERT_TRUE(right != nullptr);
    EXPECT_EQ(right->Identifier(), "end");
    const auto* rr =
        dynamic_cast<const Sem::MemberResolveResult*>(CSharp::GetResolveResult(*binary));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(),
              static_cast<const TS::IMember*>(
                  static_cast<const TS::IParameterizedMember*>(method.get())));
}

TEST(CallBuilderRangeConstructionTest, IndexFromEndRendersUnaryOperator)
{
    RangeFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // The `System.Index(value, fromEnd: true)` constructor shape: the second
    // argument is the constant `true` (the from-end flag), rendering `^value`.
    auto method = fixture.MakeStaticMethod(
        ".ctor", fixture.TypePtr(TS::KnownTypeCode::Index)->GetDefinition(),
        {fixture.TypePtr(TS::KnownTypeCode::Int32),
         fixture.TypePtr(TS::KnownTypeCode::Boolean)});

    CallBuilder::ArgumentList argumentList;
    argumentList.Arguments.push_back(
        fixture.MakeArg(TS::KnownTypeCode::Int32, "value"));
    argumentList.Arguments.push_back(
        CSharp::WithRR(CSharp::WithoutILInstruction(*new Syntax::PrimitiveExpression(true)),
                       std::make_shared<Sem::ResolveResult>(
                           fixture.TypePtr(TS::KnownTypeCode::Boolean))));
    argumentList.ExpectedParameters.push_back(nullptr);
    argumentList.ExpectedParameters.push_back(nullptr);

    CSharp::ExpressionWithResolveResult result;
    CSharp::TranslatedExpression target;
    bool handled = callBuilder.HandleRangeConstruction(
        result, IL::OpCode::NewObj, *method, target, argumentList);

    ASSERT_TRUE(handled);
    auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(result.Expression());
    ASSERT_TRUE(unary != nullptr);
    EXPECT_EQ(unary->Operator(), Syntax::UnaryOperatorType::IndexFromEnd);
    auto* operand = dynamic_cast<Syntax::IdentifierExpression*>(unary->Expression());
    ASSERT_TRUE(operand != nullptr);
    EXPECT_EQ(operand->Identifier(), "value");
}

TEST(CallBuilderRangeConstructionTest, IndexWithFromEndFalseIsNotHandled)
{
    RangeFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // The from-end flag is a compile-time constant; `false` does not select
    // the `^` render (the C# `pe.Value is true` gate).
    auto method = fixture.MakeStaticMethod(
        ".ctor", fixture.TypePtr(TS::KnownTypeCode::Index)->GetDefinition(),
        {fixture.TypePtr(TS::KnownTypeCode::Int32),
         fixture.TypePtr(TS::KnownTypeCode::Boolean)});

    CallBuilder::ArgumentList argumentList;
    argumentList.Arguments.push_back(
        fixture.MakeArg(TS::KnownTypeCode::Int32, "value"));
    argumentList.Arguments.push_back(
        CSharp::WithRR(CSharp::WithoutILInstruction(*new Syntax::PrimitiveExpression(false)),
                       std::make_shared<Sem::ResolveResult>(
                           fixture.TypePtr(TS::KnownTypeCode::Boolean))));
    argumentList.ExpectedParameters.push_back(nullptr);
    argumentList.ExpectedParameters.push_back(nullptr);

    CSharp::ExpressionWithResolveResult result;
    CSharp::TranslatedExpression target;
    EXPECT_FALSE(callBuilder.HandleRangeConstruction(
        result, IL::OpCode::NewObj, *method, target, argumentList));
}

TEST(CallBuilderRangeConstructionTest, NamedArgumentsGateBlocksRangeArms)
{
    RangeFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // Range syntax does not support named arguments: any named argument list
    // bails out of every range arm.
    auto method = fixture.MakeStaticMethod(
        ".ctor", fixture.TypePtr(TS::KnownTypeCode::Range)->GetDefinition(),
        {fixture.TypePtr(TS::KnownTypeCode::Index),
         fixture.TypePtr(TS::KnownTypeCode::Index)});

    CallBuilder::ArgumentList argumentList;
    argumentList.Arguments.push_back(fixture.MakeArg(TS::KnownTypeCode::Index, "start"));
    argumentList.Arguments.push_back(fixture.MakeArg(TS::KnownTypeCode::Index, "end"));
    argumentList.ExpectedParameters.push_back(nullptr);
    argumentList.ExpectedParameters.push_back(nullptr);
    argumentList.ArgumentNames = std::vector<std::string>{"start", "end"};

    CSharp::ExpressionWithResolveResult result;
    CSharp::TranslatedExpression target;
    EXPECT_FALSE(callBuilder.HandleRangeConstruction(
        result, IL::OpCode::NewObj, *method, target, argumentList));
}

TEST(CallBuilderRangeConstructionTest, SyntheticRangeAccessorSlicingArm)
{
    RangeFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // The compiler-generated range indexer: `Slice(Index, int)` re-signed as
    // `get_Item(Range)` with the slicing flag set renders `target[args]`
    // (the range-based slicing special case).
    auto underlying = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    underlying->SetName("Slice");
    underlying->SetIsStatic(false);
    underlying->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    underlying->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    underlying->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::Int32), "start"),
         std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::Int32), "length")});

    auto accessor = std::make_shared<Impl::SyntheticRangeIndexAccessor>(
        std::static_pointer_cast<const TS::IMethod>(underlying),
        fixture.TypePtr(TS::KnownTypeCode::Range), /*slicing=*/true);
    EXPECT_TRUE(accessor->IsSlicing());

    CSharp::TranslatedExpression target =
        fixture.MakeArg(TS::KnownTypeCode::String, "this");
    CallBuilder::ArgumentList argumentList;
    argumentList.Arguments.push_back(
        fixture.MakeArg(TS::KnownTypeCode::Range, "range"));
    argumentList.ExpectedParameters.push_back(nullptr);

    CSharp::ExpressionWithResolveResult result;
    bool handled = callBuilder.HandleRangeConstruction(
        result, IL::OpCode::CallVirt, *accessor, target, argumentList);

    ASSERT_TRUE(handled);
    auto* indexer = dynamic_cast<Syntax::IndexerExpression*>(result.Expression());
    ASSERT_TRUE(indexer != nullptr);
    auto* targetIdent = dynamic_cast<Syntax::IdentifierExpression*>(indexer->Target());
    ASSERT_TRUE(targetIdent != nullptr);
    EXPECT_EQ(targetIdent->Identifier(), "this");
    ASSERT_EQ(indexer->Arguments().Count(), 1);
    auto* arg = dynamic_cast<Syntax::IdentifierExpression*>(indexer->Arguments().At(0));
    ASSERT_TRUE(arg != nullptr);
    EXPECT_EQ(arg->Identifier(), "range");
    const auto* rr =
        dynamic_cast<const Sem::MemberResolveResult*>(CSharp::GetResolveResult(*indexer));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(), accessor.get());
}

TEST(SyntheticRangeIndexAccessorTest, WrapperSurface)
{
    RangeFixture fixture;
    // A non-slicing wrapper over `get_Item(int, string)`: the parameter list
    // keeps the underlying's tail parameters and prepends the
    // Index-or-Range-typed synthetic parameter; the negative IMethod surface is
    // unconditional (convention (d) of the header comment).
    auto underlying = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Accessor);
    underlying->SetName("get_Item");
    underlying->SetIsStatic(false);
    underlying->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    underlying->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    underlying->SetParameters(
        {std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::Int32), "index"),
         std::make_shared<Impl::DefaultParameter>(
             fixture.TypePtr(TS::KnownTypeCode::String), "value")});

    auto accessor = std::make_shared<Impl::SyntheticRangeIndexAccessor>(
        std::static_pointer_cast<const TS::IMethod>(underlying),
        fixture.TypePtr(TS::KnownTypeCode::Range), /*slicing=*/false);
    EXPECT_FALSE(accessor->IsSlicing());
    EXPECT_EQ(accessor->Name(), "get_Item");
    EXPECT_EQ(accessor->SymbolKind(), TS::SymbolKind::Method);

    // The non-slicing wrapper keeps the underlying's parameters AFTER the
    // first (the `this`-shaped slot) and prepends the synthetic
    // Index-or-Range-typed parameter: (Range, "value") over `get_Item(int,
    // string)`.
    auto parameters = accessor->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    EXPECT_EQ(parameters[0]->Type().ReflectionName(),
              fixture.TypePtr(TS::KnownTypeCode::Range)->ReflectionName());
    EXPECT_EQ(parameters[1]->Name(), "value");

    // The negative surface: never an extension method, local function,
    // constructor, destructor, operator, or explicit interface implementation,
    // and the type-parameter/type-argument lists are empty.
    EXPECT_FALSE(accessor->IsExtensionMethod());
    EXPECT_FALSE(accessor->IsLocalFunction());
    EXPECT_FALSE(accessor->IsConstructor());
    EXPECT_FALSE(accessor->IsDestructor());
    EXPECT_FALSE(accessor->IsOperator());
    EXPECT_FALSE(accessor->IsExplicitInterfaceImplementation());
    EXPECT_TRUE(accessor->TypeParameters().empty());
    EXPECT_TRUE(accessor->TypeArguments().empty());
    EXPECT_TRUE(accessor->ExplicitlyImplementedInterfaceMembers().empty());

    // The wrapper equality: two wrappers over the same underlying method, the
    // same index-or-range type, and the same slicing flag compare equal; a
    // different slicing flag does not.
    auto same = std::make_shared<Impl::SyntheticRangeIndexAccessor>(
        std::static_pointer_cast<const TS::IMethod>(underlying),
        fixture.TypePtr(TS::KnownTypeCode::Range), /*slicing=*/false);
    EXPECT_TRUE(accessor->Equals(same.get(), nullptr));
    auto slicingTwin = std::make_shared<Impl::SyntheticRangeIndexAccessor>(
        std::static_pointer_cast<const TS::IMethod>(underlying),
        fixture.TypePtr(TS::KnownTypeCode::Range), /*slicing=*/true);
    EXPECT_FALSE(accessor->Equals(slicingTwin.get(), nullptr));
}

} // namespace ILSpy::Tests