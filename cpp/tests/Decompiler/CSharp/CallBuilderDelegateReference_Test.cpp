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

// The CallBuilder delegate-reference family (CallBuilder.cs lines 1936-2203):
// CanUseDelegateConstruction, the BuildDelegateReference /
// DisambiguateDelegateReference / IsUnambiguousMethodReference cluster,
// HandleDelegateConstruction, and the BuildLdVirtDelegate /
// BuildMethodReference entry points. The resolvable scenarios drive a
// LookupTypeDefinition whose method table the MemberLookup can walk (the
// target's own type), so the DisambiguateDelegateReference loop exits on its
// first IsUnambiguousMethodReference iteration; the BuildMethodReference
// scenario drives a method the resolver cannot resolve, pinning the
// re-wrap-with-MemberResolveResult tail.

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

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

// Builds an ExpressionBuilder over the minimal corlib whose current type
// definition is the String definition, so MemberLookup has a host scope.
struct DelegateFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> scopelessContext;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope;
    std::shared_ptr<CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function_;
    // The parameter objects the LookupMethod stubs point into (the C# GC keeps
    // them alive; the port's stub parameters are raw, so the fixture owns them).
    std::vector<std::shared_ptr<Impl::DefaultParameter>> parameters_;

    DelegateFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          scopelessContext(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule())),
          usingScope(std::make_shared<CSharp::TypeSystem::UsingScope>(
              scopelessContext, compilation.RootNamespace(),
              std::vector<const TS::INamespace*>{})),
          context_(std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope, StringDefinition())),
          settings(), run(&settings, usingScope)
    {
    }

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }

    const TS::ITypeDefinition* StringDefinition()
    {
        return compilation.FindType(TS::KnownTypeCode::String).GetDefinition();
    }

    CSharp::ExpressionBuilder MakeBuilder()
    {
        return CSharp::ExpressionBuilder(nullptr, compilation, *context_, &function_,
                                         &settings, &run);
    }

    // A method `name` declared on the String type with `parameterCount` int
    // parameters (the void-return shape the delegate family reads through
    // `Parameters()`).
    std::shared_ptr<TestSupport::LookupMethod> MakeMethod(const char* name,
                                                          bool isStatic,
                                                          int parameterCount)
    {
        auto method = std::make_shared<TestSupport::LookupMethod>(name, compilation);
        method->SetStatic(isStatic);
        method->SetDeclaringTypeDefinition(StringDefinition());
        method->SetDeclaringType(TypePtr(TS::KnownTypeCode::String));
        method->SetReturnType(TypePtr(TS::KnownTypeCode::Void));
        std::vector<const TS::IParameter*> parameters;
        for (int i = 0; i < parameterCount; i++) {
            parameters_.push_back(std::make_shared<Impl::DefaultParameter>(
                TypePtr(TS::KnownTypeCode::Int32), "arg"));
            parameters.push_back(parameters_.back().get());
        }
        method->SetParameters(std::move(parameters));
        return method;
    }
};

} // namespace

TEST(CallBuilderDelegateReferenceTest, CanUseDelegateConstructionMatrix)
{
    DelegateFixture fixture;
    // Instance method + a matching invoke method: the delegate binds the
    // instance, so the method group renders regardless of the this-arg shape.
    auto instanceMethod = fixture.MakeMethod("Method", /*isStatic=*/false, 1);
    auto invokeMatch = fixture.MakeMethod("Invoke", /*isStatic=*/false, 1);

    IL::LdNull nullArg;
    // A non-ldnull this-arg probe (a constant; only the OpCode matters for
    // MatchLdNull).
    IL::LdcI4 otherArg(0);
    EXPECT_TRUE(CallBuilder::CanUseDelegateConstruction(*instanceMethod, &nullArg,
                                                        invokeMatch.get()));

    // A mismatched invoke parameter count means the delegate cannot be built
    // from an instance method.
    auto invokeMismatch = fixture.MakeMethod("Invoke", /*isStatic=*/false, 2);
    EXPECT_FALSE(CallBuilder::CanUseDelegateConstruction(*instanceMethod, &nullArg,
                                                         invokeMismatch.get()));

    // Accessors cannot be referenced as a method group in C# (issue #1741).
    auto accessor = std::make_shared<Impl::FakeMethod>(
        fixture.compilation, TS::SymbolKind::Method);
    accessor->SetName("get_Item");
    accessor->SetIsStatic(false);
    accessor->SetDeclaringType(fixture.TypePtr(TS::KnownTypeCode::String));
    accessor->SetReturnType(fixture.TypePtr(TS::KnownTypeCode::Void));
    accessor->SetAccessorOwner(static_cast<const TS::IMember*>(
        static_cast<const TS::IParameterizedMember*>(accessor.get())));
    EXPECT_TRUE(accessor->IsAccessor());
    EXPECT_FALSE(CallBuilder::CanUseDelegateConstruction(*accessor, &nullArg,
                                                         invokeMatch.get()));
}

TEST(CallBuilderDelegateReferenceTest, CanUseDelegateConstructionStaticBranches)
{
    DelegateFixture fixture;
    auto staticMethod = fixture.MakeMethod("Method", /*isStatic=*/true, 2);

    // Static method + matching invoke count + an ldnull this-arg: the delegate
    // is static, so ldnull works.
    auto invokeMatch = fixture.MakeMethod("Invoke", /*isStatic=*/false, 2);
    IL::LdNull nullArg;
    IL::LdcI4 otherArg(0);
    EXPECT_TRUE(CallBuilder::CanUseDelegateConstruction(*staticMethod, &nullArg,
                                                        invokeMatch.get()));

    // ... but a non-null this-arg on a static method means the delegate binds
    // a receiver the static method does not have.
    EXPECT_FALSE(CallBuilder::CanUseDelegateConstruction(*staticMethod, &otherArg,
                                                         invokeMatch.get()));

    // Unknown invoke method: fall back to ldnull / extension-method.
    EXPECT_TRUE(CallBuilder::CanUseDelegateConstruction(*staticMethod, &nullArg,
                                                        nullptr));
    EXPECT_FALSE(CallBuilder::CanUseDelegateConstruction(*staticMethod, &otherArg,
                                                         nullptr));

    // An extension method with one fewer invoke parameter is usable without a
    // receiver.
    auto extension = fixture.MakeMethod("Method", /*isStatic=*/true, 2);
    extension->SetIsExtensionMethod(true);
    auto invokeOneArg = fixture.MakeMethod("Invoke", /*isStatic=*/false, 1);
    EXPECT_TRUE(CallBuilder::CanUseDelegateConstruction(*extension, &otherArg,
                                                        invokeOneArg.get()));
}

TEST(CallBuilderDelegateReferenceTest, BuildMethodReferenceRendersMethodGroup)
{
    DelegateFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // A static method the resolver cannot resolve by simple name: the
    // DisambiguateDelegateReference loop adds the type arguments, then the
    // target (the declaring-type reference TranslateTarget produced for the
    // static member), then casts, and gives up with a null method-group
    // result -- so the render is `DeclaringType.MethodName` (a
    // MemberReferenceExpression over the type reference). BuildMethodReference
    // then strips the resolve-result annotations and re-wraps with a
    // target-less MemberResolveResult.
    auto method = fixture.MakeMethod("UnresolvableMethod", /*isStatic=*/true, 0);

    auto result = callBuilder.BuildMethodReference(*method, /*isVirtual=*/false);

    auto* memberRef =
        dynamic_cast<Syntax::MemberReferenceExpression*>(result.Expression());
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "UnresolvableMethod");
    const auto* rr = dynamic_cast<const Sem::MemberResolveResult*>(
        CSharp::GetResolveResult(*memberRef));
    ASSERT_TRUE(rr != nullptr);
    EXPECT_EQ(rr->Member(), method.get());
}

TEST(CallBuilderDelegateReferenceTest, BuildLdVirtDelegateRendersDelegateCreation)
{
    DelegateFixture fixture;
    auto builder = fixture.MakeBuilder();
    CallBuilder callBuilder(builder, fixture.settings);
    // The delegate type `Invoke(int)` and the instance method `Method(int)`:
    // the lookup finds `Method` on the declaring type and the overload
    // resolution accepts it, so the loop exits on the first iteration with the
    // target + method group.
    auto instanceMethod = fixture.MakeMethod("Method", /*isStatic=*/false, 1);
    auto invokeMethod = fixture.MakeMethod("Invoke", /*isStatic=*/false, 1);
    auto delegateType = std::make_shared<TestSupport::LookupTypeDefinition>(
        "MyDelegate", "Test",
        TS::FullTypeName(TS::TopLevelTypeName("Test", "MyDelegate", 0)),
        TS::TypeKind::Delegate, TS::Accessibility::Public, fixture.compilation,
        &fixture.compilation.MainModule());
    delegateType->SetMethods({invokeMethod.get()});

    IL::LdNull nullArg;
    IL::LdVirtDelegate ldVirtDelegate(
        std::make_unique<IL::LdNull>(), fixture.TypePtr(TS::KnownTypeCode::String),
        std::string("Method"),
        std::static_pointer_cast<const TS::IMethod>(instanceMethod));

    auto result = callBuilder.BuildLdVirtDelegate(ldVirtDelegate);

    auto* objectCreate =
        dynamic_cast<Syntax::ObjectCreateExpression*>(result.Expression());
    ASSERT_TRUE(objectCreate != nullptr);
    ASSERT_EQ(objectCreate->Arguments().Count(), 1);
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(
        objectCreate->Arguments().At(0));
    ASSERT_TRUE(memberRef != nullptr);
    EXPECT_EQ(memberRef->MemberName(), "Method");
    const auto* conversionRR = dynamic_cast<const Sem::ConversionResolveResult*>(
        CSharp::GetResolveResult(*objectCreate));
    ASSERT_TRUE(conversionRR != nullptr);
    EXPECT_TRUE(conversionRR->ConversionProperty()->IsMethodGroupConversion());
}

} // namespace ILSpy::Tests