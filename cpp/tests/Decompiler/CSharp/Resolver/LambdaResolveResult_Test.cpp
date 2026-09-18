// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
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

// Tests for `LambdaResolveResult` + `LambdaConversion` (the ninth/tenth
// `cpp/Decompiler/CSharp/Resolver/` leaves, the port of
// ICSharpCode.Decompiler/CSharp/Resolver/LambdaResolveResult.cs). `LambdaResolveResult` is
// the abstract `ResolveResult` subclass for an anonymous method / lambda expression: it
// forwards `SpecialType.NoType` to the base (a lambda has NO type; the delegate type comes
// from the anonymous-function conversion), declares the pure-virtual lambda surface
// (`HasParameterList` / `IsAnonymousMethod` / `IsImplicitlyTyped` / `IsAsync` /
// `GetInferredReturnType(IType[])` / `Parameters` / `ReturnType` / `IsValid(...)` /
// `Body`), and provides the concrete `GetChildResults()` override yielding `{ Body }`.
// `LambdaConversion` is the internal singleton `Conversion` subclass with
// `IsAnonymousFunctionConversion` + `IsImplicit` both true. The C# `DecompiledLambdaResolveResult`
// concrete subclass (the third class in the .cs file) projects `IsAsync` / `Parameters` /
// `ReturnType` over the held `ILFunction` and answers `IsValid` through `CSharpConversions`
// (both prerequisites, `ILFunction.Parameters` and the conversion controller, are landed).
//
// The tests exercise `LambdaResolveResult` through a local concrete test subclass (the
// class is abstract). They pin the `SpecialType.NoType` base-type crux
// (`Type().Kind() == TypeKind::None`), the eight pure-virtual dispatches, the
// `GetChildResults()` single-element snapshot (direct + through a `ResolveResult*` base),
// the inherited `ToString` bracket form (with the test subclass's `ClassName()` override),
// the `ShallowClone` runtime-type preservation + shared `Body`, and the class-shape
// static-asserts. `DecompiledLambdaResolveResult` is exercised directly (the ctor /
// projections / `GetInferredReturnType` / `ToString` / `ShallowClone` and the full `IsValid`
// arm matrix). For `LambdaConversion` the tests pin the two overridden flags, the inherited
// defaults (`IsValid` true, `IsExplicit` / `IsIdentityConversion` false), the `Instance`
// singleton reference-identity, the inherited reference-equality `Equals`, the polymorphic
// dispatch through a `Conversion*` base, and the class-shape static-asserts.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;
namespace IL = ILSpy::Decompiler::IL;

namespace {

// A minimal concrete `IParameter` for testing the `Parameters` list: only pointer-identity
// (the address) is exercised by the tests, so every accessor returns a trivial default.
class TestLambdaParameter : public TS::IParameter {
public:
    explicit TestLambdaParameter(std::string name)
        : name_(std::move(name)),
          type_(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32)) {}

    TestLambdaParameter(std::string name, TS::ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    // --- IVariable ---
    const TS::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool = false) const override { return {}; }
    // --- IParameter ---
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::ReferenceKind ReferenceKind() const override { return TS::ReferenceKind::None; }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const TS::IParameterizedMember* Owner() const override { return nullptr; }
private:
    std::string name_;
    TS::ITypePtr type_;
};

// The concrete test subclass driving the abstract `LambdaResolveResult`: the C# class is
// abstract (the real back-end subclass is `DecompiledLambdaResolveResult`, tested below), so
// the tests define a concrete stand-in that stores canned answers for every pure-virtual
// member. `IsValid` stores nothing and returns the `LambdaConversion` singleton (the canned
// success conversion -- the C# `IsValid` success result).
class TestLambdaResolveResult : public Res::LambdaResolveResult {
public:
    bool hasParameterList = true;
    bool isAnonymousMethod = false;
    bool isImplicitlyTyped = true;
    bool isAsync = false;
    TS::ITypePtr inferredReturnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TS::ITypePtr returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    std::vector<const TS::IParameter*> parameters;
    std::shared_ptr<Sem::ResolveResult> body =
        std::make_shared<Sem::TypeResolveResult>(
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void));
    // Records the parameterTypes the last GetInferredReturnType call received (the C#
    // IType[] argument pass-through).
    mutable std::vector<TS::ITypePtr> lastParameterTypes;

    bool HasParameterList() const override { return hasParameterList; }
    bool IsAnonymousMethod() const override { return isAnonymousMethod; }
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped; }
    bool IsAsync() const override { return isAsync; }
    TS::ITypePtr GetInferredReturnType(const std::vector<TS::ITypePtr>& parameterTypes) const override
    {
        lastParameterTypes = parameterTypes;
        return inferredReturnType;
    }
    std::vector<const TS::IParameter*> Parameters() const override { return parameters; }
    const TS::IType& ReturnType() const override { return *returnType; }
    std::shared_ptr<Sem::Conversion> IsValid(const std::vector<TS::ITypePtr>&,
                                             const TS::ITypePtr&,
                                             Res::CSharpConversions&) const override
    {
        return Res::LambdaConversion::InstancePtr();
    }
    Sem::ResolveResult& Body() const override { return *body; }

    // The D424 slicing-prevention convention: the abstract base CANNOT override
    // `ShallowClone` (it is abstract), so each concrete subclass overrides it with its own
    // copy (the C# `MemberwiseClone` runtime-type preservation). The default copy ctor
    // shares the `body` shared_ptr and the ITypePtr fields, faithfully mirroring the C#
    // reference-copy.
    std::unique_ptr<Sem::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<TestLambdaResolveResult>(*this);
    }
protected:
    // The C# `GetType().Name` on a lambda resolve result yields the CONCRETE class name
    // (the abstract base never instantiates); the test subclass reports its own name.
    std::string ClassName() const override { return "TestLambdaResolveResult"; }
};

} // namespace

// ===========================================================================
// LambdaResolveResult -- the `SpecialType.NoType` base-type crux.
// ===========================================================================

TEST(LambdaResolveResultTest, BaseTypeIsNoType)
{
    // The C# ctor forwards `SpecialType.NoType` (the TypeKind::None null object): a lambda
    // has NO type (the delegate type comes from the anonymous-function conversion, not the
    // resolve result's Type).
    TestLambdaResolveResult rr;
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::None);
}

TEST(LambdaResolveResultTest, IsErrorDefaultsFalse)
{
    // No IsError override: the inherited ResolveResult default (false) applies.
    TestLambdaResolveResult rr;
    EXPECT_FALSE(rr.IsError());
}

// ===========================================================================
// LambdaResolveResult -- the pure-virtual property dispatches.
// ===========================================================================

TEST(LambdaResolveResultTest, HasParameterListDispatchesToOverride)
{
    TestLambdaResolveResult rr;
    EXPECT_TRUE(rr.HasParameterList());
    rr.hasParameterList = false;
    EXPECT_FALSE(rr.HasParameterList());
}

TEST(LambdaResolveResultTest, IsAnonymousMethodDispatchesToOverride)
{
    TestLambdaResolveResult rr;
    EXPECT_FALSE(rr.IsAnonymousMethod());
    rr.isAnonymousMethod = true;
    EXPECT_TRUE(rr.IsAnonymousMethod());
}

TEST(LambdaResolveResultTest, IsImplicitlyTypedDispatchesToOverride)
{
    TestLambdaResolveResult rr;
    EXPECT_TRUE(rr.IsImplicitlyTyped());
    rr.isImplicitlyTyped = false;
    EXPECT_FALSE(rr.IsImplicitlyTyped());
}

TEST(LambdaResolveResultTest, IsAsyncDispatchesToOverride)
{
    TestLambdaResolveResult rr;
    EXPECT_FALSE(rr.IsAsync());
    rr.isAsync = true;
    EXPECT_TRUE(rr.IsAsync());
}

TEST(LambdaResolveResultTest, GetInferredReturnTypeReturnsStubbedTypeAndForwardsParameters)
{
    TestLambdaResolveResult rr;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    rr.inferredReturnType = stringType;
    auto result = rr.GetInferredReturnType({ intType, stringType });
    EXPECT_EQ(result.get(), stringType.get());
    ASSERT_EQ(rr.lastParameterTypes.size(), 2u);
    EXPECT_EQ(rr.lastParameterTypes[0].get(), intType.get());
    EXPECT_EQ(rr.lastParameterTypes[1].get(), stringType.get());
}

TEST(LambdaResolveResultTest, ParametersReturnsStubbedList)
{
    TestLambdaResolveResult rr;
    TestLambdaParameter p1("x");
    TestLambdaParameter p2("y");
    rr.parameters = { &p1, &p2 };
    auto params = rr.Parameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0], &p1);
    EXPECT_EQ(params[1], &p2);
}

TEST(LambdaResolveResultTest, ParametersDefaultsEmpty)
{
    TestLambdaResolveResult rr;
    EXPECT_TRUE(rr.Parameters().empty());
}

TEST(LambdaResolveResultTest, ReturnTypeReturnsStubbedTypeByReference)
{
    TestLambdaResolveResult rr;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    rr.returnType = intType;
    EXPECT_EQ(&rr.ReturnType(), intType.get());
}

TEST(LambdaResolveResultTest, BodyReturnsStubbedBodyByReference)
{
    TestLambdaResolveResult rr;
    EXPECT_EQ(&rr.Body(), rr.body.get());
}

// ===========================================================================
// LambdaResolveResult -- the concrete `GetChildResults()` override (`{ Body }`).
// ===========================================================================

TEST(LambdaResolveResultTest, GetChildResultsReturnsSingleElementBodySnapshot)
{
    TestLambdaResolveResult rr;
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], rr.body.get());
}

TEST(LambdaResolveResultTest, GetChildResultsDispatchesThroughResolveResultBasePointer)
{
    TestLambdaResolveResult rr;
    Sem::ResolveResult* base = &rr;
    auto children = base->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], rr.body.get());
}

// ===========================================================================
// LambdaResolveResult -- the inherited ToString bracket form (the C# does NOT override
// ToString), yielding "[<concrete ClassName> ?]" (NoType's ReflectionName is
// the C# `SpecialType.NoType` singleton's name "?").
// ===========================================================================

TEST(LambdaResolveResultTest, ToStringUsesInheritedBracketForm)
{
    TestLambdaResolveResult rr;
    EXPECT_EQ(rr.ToString(), "[TestLambdaResolveResult ?]");
}

// ===========================================================================
// LambdaResolveResult -- ShallowClone (overridden by the concrete subclass, the D424
// convention): preserves the runtime type, shares the Body, is a distinct instance.
// ===========================================================================

TEST(LambdaResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestLambdaResolveResult rr;
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<TestLambdaResolveResult*>(clone.get()), nullptr);
}

TEST(LambdaResolveResultTest, ShallowCloneIsDistinctInstanceSharingBody)
{
    TestLambdaResolveResult rr;
    auto clone = rr.ShallowClone();
    EXPECT_NE(static_cast<Sem::ResolveResult*>(&rr), clone.get());
    auto* cloned = dynamic_cast<TestLambdaResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(&cloned->Body(), rr.body.get());
}

// ===========================================================================
// LambdaConversion -- the internal singleton Conversion subclass.
// ===========================================================================

TEST(LambdaConversionTest, IsAnonymousFunctionConversionIsTrue)
{
    EXPECT_TRUE(Res::LambdaConversion::Instance().IsAnonymousFunctionConversion());
}

TEST(LambdaConversionTest, IsImplicitIsTrue)
{
    EXPECT_TRUE(Res::LambdaConversion::Instance().IsImplicit());
}

TEST(LambdaConversionTest, InheritedDefaultsApply)
{
    // LambdaConversion overrides exactly two members; the rest are the Conversion base
    // defaults (IsValid true, every other flag false).
    const Sem::Conversion& conv = Res::LambdaConversion::Instance();
    EXPECT_TRUE(conv.IsValid());
    EXPECT_FALSE(conv.IsExplicit());
    EXPECT_FALSE(conv.IsIdentityConversion());
    EXPECT_FALSE(conv.IsMethodGroupConversion());
    EXPECT_FALSE(conv.IsTupleConversion());
}

TEST(LambdaConversionTest, InstanceIsSingletonReferenceEqual)
{
    // The C# `public static readonly LambdaConversion Instance` is a single object.
    EXPECT_EQ(&Res::LambdaConversion::Instance(), &Res::LambdaConversion::Instance());
}

TEST(LambdaConversionTest, EqualsIsInheritedReferenceEquality)
{
    // LambdaConversion does NOT override Equals/GetHashCode: the base reference-equality
    // applies (the Instance singleton equals itself, never a distinct instance).
    Res::LambdaConversion other;
    EXPECT_TRUE(Res::LambdaConversion::Instance().Equals(Res::LambdaConversion::Instance()));
    EXPECT_FALSE(Res::LambdaConversion::Instance().Equals(other));
}

TEST(LambdaConversionTest, DispatchesThroughConversionBasePointer)
{
    const Sem::Conversion* base = &Res::LambdaConversion::Instance();
    EXPECT_TRUE(base->IsAnonymousFunctionConversion());
    EXPECT_TRUE(base->IsImplicit());
    EXPECT_TRUE(base->IsValid());
}

// ===========================================================================
// DecompiledLambdaResolveResult -- the concrete back-end subclass.
// ===========================================================================

namespace {

using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;

// An `ILFunction` with a two-parameter list, an Int32 return type, and the compilation the
// `CSharpConversions` controller runs over. The parameters are owned by the fixture (the
// `ILFunction.Parameters` field is non-owning, faithful to the C# type-system ownership).
struct DecompiledLambdaFixture {
    LookupCompilation compilation;
    IL::ILFunction function;
    std::shared_ptr<TestLambdaParameter> firstParam;
    std::shared_ptr<TestLambdaParameter> secondParam;

    DecompiledLambdaFixture()
    {
        firstParam = std::make_shared<TestLambdaParameter>(
            "sender", std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
        secondParam = std::make_shared<TestLambdaParameter>(
            "e", std::make_shared<TS::KnownType>(TS::KnownTypeCode::String));
        function.Parameters.push_back(firstParam.get());
        function.Parameters.push_back(secondParam.get());
        function.ReturnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    }
};

} // namespace

TEST(DecompiledLambdaResolveResultTest, CtorCapturesFlagsAndProjectsFunction)
{
    DecompiledLambdaFixture f;
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          /*hasParameterList*/ true,
                                          /*isAnonymousMethod*/ false,
                                          /*isImplicitlyTyped*/ true);
    EXPECT_TRUE(rr.HasParameterList());
    EXPECT_FALSE(rr.IsAnonymousMethod());
    EXPECT_TRUE(rr.IsImplicitlyTyped());
    EXPECT_FALSE(rr.IsAsync());
    ASSERT_EQ(rr.Parameters().size(), 2u);
    EXPECT_EQ(rr.Parameters()[0], f.firstParam.get());
    EXPECT_EQ(rr.Parameters()[1], f.secondParam.get());
    EXPECT_EQ(&rr.ReturnType(), f.function.ReturnType.get());
    EXPECT_EQ(rr.DelegateType.get(), delegateType.get());
    EXPECT_EQ(rr.InferredReturnType.get(), inferred.get());
    // The lambda has NO type (the anonymous-function conversion carries the delegate type).
    EXPECT_EQ(rr.Type().Kind(), TS::TypeKind::None);
    EXPECT_FALSE(rr.IsError());
    // The Body is a fresh ResolveResult(SpecialType.UnknownType).
    EXPECT_EQ(rr.Body().Type().Kind(), TS::TypeKind::Unknown);
}

TEST(DecompiledLambdaResolveResultTest, IsAsyncProjectsFunctionAsyncReturnType)
{
    DecompiledLambdaFixture f;
    f.function.AsyncReturnType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred, false, true, false);
    EXPECT_TRUE(rr.IsAsync());
}

TEST(DecompiledLambdaResolveResultTest, GetInferredReturnTypeReturnsStoredField)
{
    DecompiledLambdaFixture f;
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred, false, false, false);
    // The parameter types are ignored (the C# "we don't know how to compute ... " comment).
    EXPECT_EQ(rr.GetInferredReturnType({}).get(), inferred.get());
    EXPECT_EQ(rr.GetInferredReturnType(
                  {std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean)})
                  .get(),
              inferred.get());
    // `InferredReturnType` is a public mutable field (`ModifyReturnTypeOfLambda` assigns it).
    auto replacement = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean);
    rr.InferredReturnType = replacement;
    EXPECT_EQ(rr.GetInferredReturnType({}).get(), replacement.get());
}

TEST(DecompiledLambdaResolveResultTest, ToStringUsesConcreteClassName)
{
    DecompiledLambdaFixture f;
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred, false, false, false);
    // The inherited ResolveResult bracket form uses the concrete ClassName; a lambda has NoType
    // (its ReflectionName is "?").
    EXPECT_EQ(rr.ToString(), "[DecompiledLambdaResolveResult ?]");
}

TEST(DecompiledLambdaResolveResultTest, ShallowClonePreservesTypeAndSharesBody)
{
    DecompiledLambdaFixture f;
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred, true, false, false);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->ToString(), "[DecompiledLambdaResolveResult ?]");
    auto* cloneLambda = dynamic_cast<Res::LambdaResolveResult*>(clone.get());
    ASSERT_NE(cloneLambda, nullptr);
    // The Body shared_ptr / the ITypePtr fields are shared; the held function pointer is copied.
    EXPECT_EQ(&cloneLambda->Body(), &rr.Body());
}

TEST(DecompiledLambdaResolveResultTest, IsValidIdentityParameterAndReturnTypes)
{
    DecompiledLambdaFixture f;
    Res::CSharpConversions conversions(f.compilation);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          true, false, false);
    std::vector<TS::ITypePtr> parameterTypes{
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object),
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String)};
    auto returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto result = rr.IsValid(parameterTypes, returnType, conversions);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->IsValid());
    EXPECT_TRUE(result->IsAnonymousFunctionConversion());
}

TEST(DecompiledLambdaResolveResultTest, IsValidParameterCountMismatchIsNone)
{
    DecompiledLambdaFixture f;
    Res::CSharpConversions conversions(f.compilation);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          true, false, false);
    std::vector<TS::ITypePtr> parameterTypes{
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object)};
    auto returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto result = rr.IsValid(parameterTypes, returnType, conversions);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->IsValid());
    EXPECT_FALSE(result->IsAnonymousFunctionConversion());
}

TEST(DecompiledLambdaResolveResultTest, IsValidImplicitlyTypedParameterMismatchIsValid)
{
    DecompiledLambdaFixture f;
    Res::CSharpConversions conversions(f.compilation);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    // Neither the declared return type (String) nor the inferred return type (String) converts
    // to the target (Int32); only the parameter-list early return can make this valid.
    f.function.ReturnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          true, false, /*isImplicitlyTyped*/ true);
    std::vector<TS::ITypePtr> parameterTypes{
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32),
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean)};
    auto returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto result = rr.IsValid(parameterTypes, returnType, conversions);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->IsValid());
    EXPECT_TRUE(result->IsAnonymousFunctionConversion());
}

TEST(DecompiledLambdaResolveResultTest, IsValidExplicitlyTypedParameterMismatchIsNone)
{
    DecompiledLambdaFixture f;
    Res::CSharpConversions conversions(f.compilation);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    // Neither the declared return type (String) nor the inferred return type (String) converts
    // to the target (Int32); the explicitly typed parameter mismatch yields None.
    f.function.ReturnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          true, false, /*isImplicitlyTyped*/ false);
    std::vector<TS::ITypePtr> parameterTypes{
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32),
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Boolean)};
    auto returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto result = rr.IsValid(parameterTypes, returnType, conversions);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->IsValid());
    EXPECT_FALSE(result->IsAnonymousFunctionConversion());
}

TEST(DecompiledLambdaResolveResultTest, IsValidFallsBackToInferredReturnType)
{
    DecompiledLambdaFixture f;
    Res::CSharpConversions conversions(f.compilation);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    // The declared return type (Int32) does not identity-convert to the target (Object), but
    // the inferred return type (Object) does.
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          false, false, false);
    auto returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto result = rr.IsValid({}, returnType, conversions);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->IsValid());
    EXPECT_TRUE(result->IsAnonymousFunctionConversion());
}

TEST(DecompiledLambdaResolveResultTest, IsValidNoConversionIsNone)
{
    DecompiledLambdaFixture f;
    Res::CSharpConversions conversions(f.compilation);
    auto delegateType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    // Neither the declared return type (String) nor the inferred return type (Object) converts
    // implicitly to the target (Int32): boxing is not implicit, unboxing is explicit.
    f.function.ReturnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    auto inferred = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    Res::DecompiledLambdaResolveResult rr(&f.function, delegateType, inferred,
                                          false, false, false);
    auto returnType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto result = rr.IsValid({}, returnType, conversions);
    ASSERT_NE(result, nullptr);
    EXPECT_FALSE(result->IsValid());
    EXPECT_FALSE(result->IsAnonymousFunctionConversion());
}

// ===========================================================================
// Class shape -- base / abstract / not-final / polymorphic.
// ===========================================================================

static_assert(std::is_base_of<Sem::ResolveResult, Res::LambdaResolveResult>::value,
              "LambdaResolveResult derives from ResolveResult");
static_assert(std::is_abstract<Res::LambdaResolveResult>::value,
              "LambdaResolveResult is abstract (the C# keyword; pure-virtual lambda surface)");
static_assert(!std::is_final<Res::LambdaResolveResult>::value,
              "LambdaResolveResult is not final (the C# class is unsealed)");
static_assert(std::is_polymorphic<Res::LambdaResolveResult>::value,
              "LambdaResolveResult is polymorphic");

static_assert(std::is_base_of<Sem::Conversion, Res::LambdaConversion>::value,
              "LambdaConversion derives from Conversion");
static_assert(!std::is_final<Res::LambdaConversion>::value,
              "LambdaConversion is not final (the C# class is unsealed)");
static_assert(std::is_polymorphic<Res::LambdaConversion>::value,
              "LambdaConversion is polymorphic");

static_assert(std::is_base_of<Res::LambdaResolveResult, Res::DecompiledLambdaResolveResult>::value,
              "DecompiledLambdaResolveResult derives from LambdaResolveResult");
static_assert(std::is_final<Res::DecompiledLambdaResolveResult>::value,
              "DecompiledLambdaResolveResult is final (the C# class is sealed)");
static_assert(std::is_polymorphic<Res::DecompiledLambdaResolveResult>::value,
              "DecompiledLambdaResolveResult is polymorphic");

