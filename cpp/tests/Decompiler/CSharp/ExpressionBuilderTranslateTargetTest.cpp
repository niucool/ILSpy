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

// Tests for the ExpressionBuilder TranslateTarget arm (ExpressionBuilder.cs
// lines 2734-2844): the static type-reference arm, the base-reference arm over
// the current type definition's base types, the instance arm over the
// `this`/local receiver, and the value-type constrained machinery (the
// pointer/ref type-hint walk, the issue-#1333 reference-of-the-correct-type
// conversion, and the DirectionExpression unwrap). The fixture mirrors the
// ExpressionBuilderSkeleton shape over a real MinimalCorlib compilation, with
// the decompilation context's current type definition configurable through the
// CSharpTypeResolveContext With* clone factories.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

using namespace ILSpy::Decompiler;
using CSharp::ExpressionBuilder;
using ILSpy::Decompiler::IL::ILVariable;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::VariableKind;
namespace IL = ILSpy::Decompiler::IL;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ILSpy::Decompiler::TypeSystem;

using CSharp::TypeSystem::CSharpTypeResolveContext;
using CSharp::TypeSystem::UsingScope;

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

// A compilation over MinimalCorlib (the all-known-types module) with a
// configurable decompilation context -- the ExpressionBuilderSkeleton fixture
// shape plus the current-type-definition slot the base-reference arm reads.
struct TranslateTargetFixture {
    TS::SimpleCompilation compilation;
    std::shared_ptr<CSharpTypeResolveContext> baseContext;
    DecompilerSettings settings;
    DecompileRun run;
    IL::ILFunction function;

    TranslateTargetFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          baseContext(MakeBaseContext()), run(&settings, baseContext->CurrentUsingScope())
    {
    }

    // The root using scope over the compilation's global namespace (the
    // CSharpTypeResolveContext_Test fixture shape), wrapped in the bare context.
    std::shared_ptr<CSharpTypeResolveContext> MakeBaseContext()
    {
        auto scopeContext = std::make_shared<CSharpTypeResolveContext>(
            compilation.MainModule());
        auto usingScope = std::make_shared<UsingScope>(
            scopeContext, compilation.RootNamespace(),
            std::vector<const TS::INamespace*>{});
        return std::make_shared<CSharpTypeResolveContext>(
            compilation.MainModule(), usingScope);
    }

    // The module type definition for a top-level corlib known type.
    const TS::ITypeDefinition* Definition(TS::KnownTypeCode code) const
    {
        return compilation.FindType(code).GetDefinition();
    }

    // A builder whose decompilation context carries `currentTypeDefinition`
    // (null = the bare context shape).
    ExpressionBuilder MakeBuilder(const TS::ITypeDefinition* currentTypeDefinition)
    {
        std::unique_ptr<TS::ITypeResolveContext> context =
            currentTypeDefinition != nullptr
                ? baseContext->WithCurrentTypeDefinition(currentTypeDefinition)
                : baseContext->WithCurrentTypeDefinition(nullptr);
        return ExpressionBuilder(nullptr, compilation, *context, &function, &settings,
                                 &run);
    }

    // The synthetic `this` parameter variable over the given type.
    ILVariablePtr ThisVariable(TS::ITypePtr type) const
    {
        auto variable = std::make_shared<ILVariable>(VariableKind::Parameter,
                                                     std::move(type), -1);
        variable->Name = "this";
        return variable;
    }

    // A named local variable over the given type.
    ILVariablePtr LocalVariable(TS::ITypePtr type, const char* name) const
    {
        auto variable = std::make_shared<ILVariable>(VariableKind::Local,
                                                     std::move(type), 0);
        variable->Name = name;
        return variable;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The static arm

TEST(ExpressionBuilderTranslateTargetTest, StaticTargetRendersTypeReference)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto expr = builder.TranslateTarget(nullptr, /*nonVirtualInvocation=*/false,
                                        /*memberStatic=*/true, int32);
    auto* typeReference = dynamic_cast<Syntax::TypeReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(typeReference != nullptr);
    // No IL-instruction annotation on the static arm.
    EXPECT_TRUE(expr.ILInstructions().empty());
    // The resolve result is the TypeResolveResult over the declaring type.
    const auto* typeResult = dynamic_cast<const Sem::TypeResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(typeResult != nullptr);
    EXPECT_TRUE(typeResult->Type().Equals(int32));
}

TEST(ExpressionBuilderTranslateTargetTest, StaticTargetPrefersConstrainedTo)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    const TS::IType& str = fixture.compilation.FindType(TS::KnownTypeCode::String);
    auto expr = builder.TranslateTarget(nullptr, false, true, int32, &str);
    const auto* typeResult = dynamic_cast<const Sem::TypeResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(typeResult != nullptr);
    EXPECT_TRUE(typeResult->Type().Equals(str));
    auto* typeReference = dynamic_cast<Syntax::TypeReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(typeReference != nullptr);
}

// A null target over an instance member takes the same type-reference arm
// (the C# `!memberStatic && target != null` gate falls to the else).
TEST(ExpressionBuilderTranslateTargetTest, NullTargetWithInstanceMemberRendersTypeReference)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto expr = builder.TranslateTarget(nullptr, false, false, int32);
    EXPECT_TRUE(dynamic_cast<Syntax::TypeReferenceExpression*>(expr.Expression()) != nullptr);
}

// ---------------------------------------------------------------------------
// The instance arm over the this receiver

TEST(ExpressionBuilderTranslateTargetTest, ReferenceTypeThisRendersThisReference)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& str = fixture.compilation.FindType(TS::KnownTypeCode::String);
    auto variable = fixture.ThisVariable(
        std::const_pointer_cast<TS::IType>(str.shared_from_this()));
    IL::LdLoc ldloc(variable);
    auto expr = builder.TranslateTarget(&ldloc, /*nonVirtualInvocation=*/false,
                                        /*memberStatic=*/false, str);
    // The expected-this-pointer stack type is O (a reference type) -- no hint
    // change, and the translated receiver is the `this` reference.
    EXPECT_TRUE(dynamic_cast<Syntax::ThisReferenceExpression*>(expr.Expression()) != nullptr);
    // The IL-instruction annotation carries the LdLoc.
    ASSERT_EQ(expr.ILInstructions().size(), 1u);
    EXPECT_EQ(expr.ILInstructions().front(), &ldloc);
    EXPECT_TRUE(expr.Type().Equals(str));
}

// ---------------------------------------------------------------------------
// The base-reference arm

TEST(ExpressionBuilderTranslateTargetTest, BaseReferenceArmOverDerivedCurrentType)
{
    TranslateTargetFixture fixture;
    const TS::ITypeDefinition* idisposable = fixture.Definition(TS::KnownTypeCode::IDisposable);
    ASSERT_TRUE(idisposable != nullptr);
    const TS::IType& str = fixture.compilation.FindType(TS::KnownTypeCode::String);
    auto builder = fixture.MakeBuilder(idisposable);
    auto variable = fixture.ThisVariable(
        std::const_pointer_cast<TS::IType>(str.shared_from_this()));
    IL::LdLoc ldloc(variable);
    // A non-virtual invocation of a String member from an IDisposable-derived
    // current type: the `ldloc this` receiver renders `base` (String's
    // definition != the current type definition).
    auto expr = builder.TranslateTarget(&ldloc, /*nonVirtualInvocation=*/true,
                                        /*memberStatic=*/false, str);
    auto* baseReference = dynamic_cast<Syntax::BaseReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(baseReference != nullptr);
    // The resolve result carries the first non-interface base type of the
    // current type definition (IDisposable's DirectBaseTypes = [Object]).
    const auto* thisResult = dynamic_cast<const Sem::ThisResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(thisResult != nullptr);
    EXPECT_TRUE(thisResult->CausesNonVirtualInvocation());
    EXPECT_TRUE(thisResult->Type().ReflectionName() == "System.Object");
}

TEST(ExpressionBuilderTranslateTargetTest, BaseReferenceArmPrefersNonInterfaceBase)
{
    TranslateTargetFixture fixture;
    // The current type is Object itself (its DirectBaseTypes is empty): the
    // fallback takes the member declaring type. Use Int32 as the declaring type
    // with the current type definition a non-interface type that has no Object
    // base -- the MinimalCorlib Range type (Struct, base ValueType) is the
    // fixture: FirstOrDefault(Kind != Interface) answers ValueType.
    const TS::ITypeDefinition* range = fixture.Definition(TS::KnownTypeCode::Range);
    ASSERT_TRUE(range != nullptr);
    ASSERT_EQ(range->Kind(), TS::TypeKind::Struct);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto builder = fixture.MakeBuilder(range);
    auto variable = fixture.ThisVariable(
        std::const_pointer_cast<TS::IType>(int32.shared_from_this()));
    IL::LdLoc ldloc(variable);
    auto expr = builder.TranslateTarget(&ldloc, true, false, int32);
    auto* baseReference = dynamic_cast<Syntax::BaseReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(baseReference != nullptr);
    const auto* thisResult = dynamic_cast<const Sem::ThisResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(thisResult != nullptr);
    // Int32's definition is not the current Range definition, so the base arm
    // fires; the base-reference type is Range's first non-interface base.
    EXPECT_TRUE(thisResult->Type().ReflectionName() == "System.ValueType");
}

// The same-declaring-type case does NOT take the base arm (the C#
// `(constrainedTo ?? memberDeclaringType).GetDefinition() ==
// resolver.CurrentTypeDefinition` guard).
TEST(ExpressionBuilderTranslateTargetTest, BaseReferenceSuppressedForCurrentType)
{
    TranslateTargetFixture fixture;
    const TS::ITypeDefinition* str = fixture.Definition(TS::KnownTypeCode::String);
    ASSERT_TRUE(str != nullptr);
    const TS::IType& strType = fixture.compilation.FindType(TS::KnownTypeCode::String);
    auto builder = fixture.MakeBuilder(str);
    auto variable = fixture.ThisVariable(
        std::const_pointer_cast<TS::IType>(strType.shared_from_this()));
    IL::LdLoc ldloc(variable);
    auto expr = builder.TranslateTarget(&ldloc, true, false, strType);
    EXPECT_TRUE(dynamic_cast<Syntax::ThisReferenceExpression*>(expr.Expression()) != nullptr);
}

// The base-reference type falls back to the member declaring type when the
// current type definition has NO non-interface base types (the C#
// `baseReferenceType ?? memberDeclaringType` arm; Object is the MinimalCorlib
// type with the empty DirectBaseTypes).
TEST(ExpressionBuilderTranslateTargetTest, BaseReferenceFallbackTakesDeclaringType)
{
    TranslateTargetFixture fixture;
    const TS::ITypeDefinition* obj = fixture.Definition(TS::KnownTypeCode::Object);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_TRUE(obj->DirectBaseTypes().empty());
    const TS::IType& str = fixture.compilation.FindType(TS::KnownTypeCode::String);
    auto builder = fixture.MakeBuilder(obj);
    auto variable = fixture.ThisVariable(
        std::const_pointer_cast<TS::IType>(str.shared_from_this()));
    IL::LdLoc ldloc(variable);
    auto expr = builder.TranslateTarget(&ldloc, true, false, str);
    auto* baseReference = dynamic_cast<Syntax::BaseReferenceExpression*>(expr.Expression());
    ASSERT_TRUE(baseReference != nullptr);
    const auto* thisResult = dynamic_cast<const Sem::ThisResolveResult*>(expr.ResolveResult());
    ASSERT_TRUE(thisResult != nullptr);
    // The fallback: the resolve result type is the member declaring type.
    EXPECT_TRUE(thisResult->Type().Equals(str));
    EXPECT_TRUE(thisResult->CausesNonVirtualInvocation());
}

// ---------------------------------------------------------------------------
// The value-type constrained machinery

TEST(ExpressionBuilderTranslateTargetTest, ValueTypeRefReceiverUnwrapsDirectionExpression)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto variable = fixture.LocalVariable(
        std::const_pointer_cast<TS::IType>(int32.shared_from_this()), "num");
    IL::LdLoca ldloca(variable);
    // constrainedTo set over a value type: the receiver is a managed reference,
    // so the hint is ByReferenceType(int32); the LdLoca already translates to
    // `ref num` whose type IS the correct by-reference type -- the conversion
    // is skipped and the `ref` wrapper is unwrapped.
    auto expr = builder.TranslateTarget(&ldloca, false, false, int32, &int32);
    // (ref x).member => x.member: the DirectionExpression is unwrapped.
    auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(expr.Expression());
    ASSERT_TRUE(identifier != nullptr);
    EXPECT_EQ(identifier->Identifier(), "num");
    EXPECT_TRUE(expr.Type().Equals(int32));
}

TEST(ExpressionBuilderTranslateTargetTest, ValueTypePointerReceiverConvertsToReference)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto variable = fixture.LocalVariable(
        std::const_pointer_cast<TS::IType>(int32.shared_from_this()), "num");
    IL::LdLoc ldloc(variable);
    // constrainedTo set over a value type with a plain value load: the hint is
    // PointerType(int32) (the receiver stack type is I, not Ref), and the
    // translated receiver's type is NOT the correct by-reference type -- the
    // issue-#1333 conversion renders `ref *(int*)x` and the DirectionExpression
    // wrapper is then unwrapped, leaving the dereference.
    auto expr = builder.TranslateTarget(&ldloc, false, false, int32, &int32);
    auto* dereference = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(dereference != nullptr);
    EXPECT_EQ(dereference->Operator(), Syntax::UnaryOperatorType::Dereference);
    // The dereferenced operand is the `(int*)x` pointer cast.
    auto* cast = dynamic_cast<Syntax::CastExpression*>(dereference->Expression());
    ASSERT_TRUE(cast != nullptr);
}

// A value-type receiver WITHOUT a constrained prefix passes the this pointer as
// a managed reference only when the receiver is a Ref; the plain-value local
// falls to the pointer hint (the C# `ExpectedTypeForThisPointer == Ref` branch
// over a value-type declaring type with constrainedTo == null).
TEST(ExpressionBuilderTranslateTargetTest, UnconstrainedValueTypeReceiverTakesPointerHint)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto variable = fixture.LocalVariable(
        std::const_pointer_cast<TS::IType>(int32.shared_from_this()), "num");
    IL::LdLoc ldloc(variable);
    // ExpectedTypeForThisPointer(Int32, null) == Ref (value type); the target's
    // stack type is I, so the hint is PointerType(int32). The Translate renders
    // the identifier with its own type, and the second check converts it to the
    // by-reference form, which unwraps to the dereference of the pointer cast.
    auto expr = builder.TranslateTarget(&ldloc, false, false, int32);
    auto* dereference = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr.Expression());
    ASSERT_TRUE(dereference != nullptr);
    EXPECT_EQ(dereference->Operator(), Syntax::UnaryOperatorType::Dereference);
}

TEST(ExpressionBuilderTranslateTargetTest, CurrentTypeNullWithLocalReceiverThrowsNullReference)
{
    TranslateTargetFixture fixture;
    auto builder = fixture.MakeBuilder(nullptr);
    const TS::IType& int32 = fixture.compilation.FindType(TS::KnownTypeCode::Int32);
    auto variable = fixture.LocalVariable(
        std::const_pointer_cast<TS::IType>(int32.shared_from_this()), "num");
    IL::LdLoc ldloc(variable);
    // The C# local MatchLdThis reaches `resolver.CurrentTypeDefinition.Kind`
    // after the direct `ldloc this` match fails (a non-virtual invocation gets
    // past ShouldUseBaseReference's first guard): with a null current type
    // definition that is the .NET NullReferenceException (the C# decompiler is
    // only constructed with a current type, so the arm is theoretical there).
    EXPECT_THROW(
        builder.TranslateTarget(&ldloc, /*nonVirtualInvocation=*/true, false, int32),
        std::invalid_argument);
}
