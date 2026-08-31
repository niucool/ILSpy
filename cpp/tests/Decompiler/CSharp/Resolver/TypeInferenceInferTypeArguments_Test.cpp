// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `TypeInference` InferTypeArguments region (the final slice of the
// TypeInference long pole): `Detail::PhaseOne` (TypeInference.cs lines 277-311, C# 4.0
// spec section 7.5.2.1), `Detail::PhaseTwo` (lines 321-380, spec draft-v11 section
// 12.6.3.3), `Detail::InferTypeArguments` (lines 116-170, the main entry), and
// `Detail::InferTypeArgumentsFromBounds` (lines 179-208).
//
// The load-bearing cruxes:
//  (a) the PhaseOne dispatch order -- an explicitly-typed lambda exact-infers its
//      declared parameter types, an (implicitly-typed lambda OR method group) argument
//      whose output types mention an unfixed parameter while its input types do not
//      takes an output-type inference, and a plain expression's own type bounds the
//      parameter type EXACT against a by-ref parameter shape and LOWER otherwise
//      (gated on IsValidType);
//  (b) the PhaseTwo fix rounds -- the parameters that depend on no unfixed parameter
//      are fixed first, and the output-type-inference-then-repeat loop unblocks a
//      dependent parameter AFTER its dependency is fixed (the flagship
//      Func<T1,T2> + T1 shape);
//  (c) the PhaseTwo output-type inference threads the FIXED decisions into the nested
//      lambda's GetInferredReturnType through the fixed-TP substitution (the recorded
//      parameter types of the PhaseTwo call are the substituted ones);
//  (d) the main entry's soft-failure fallbacks -- the wrong-index / non-method-owner /
//      null-entry contract violations (C# ArgumentException / ArgumentNullException)
//      report success=false with the all-UnknownType result;
//  (e) an unfixable parameter reports UnknownType and fails the inference, while the
//      fixable positions still report their fixed types (the per-position reporting
//      and the non-short-circuit `success &=` accumulation of
//      InferTypeArgumentsFromBounds).
//
// The stubs mirror the TypeInferenceMakeInference_Test / TypeInferenceMakeOutputType-
// Inference_Test conventions (`Def`, `MakeTypeParam`, `MakeState`, `MethodHostType`/
// `MakeMethod`/`TestParameter`/`MakeParam`/`ConfigureMethod`, `RecordingLambda`) plus
// the `ClassOwnedTypeParameter` (the OwnerType contract-violation stub) and the
// `VisitableTypeParameter` AcceptVisitor bridge (the TypeParameterSubstitution
// dispatch over the delegate signature's parameter types).

#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::InferTypeArguments;
using ILSpy::Decompiler::CSharp::Resolver::Detail::InferTypeArgumentsFromBounds;
using ILSpy::Decompiler::CSharp::Resolver::Detail::PhaseOne;
using ILSpy::Decompiler::CSharp::Resolver::Detail::PhaseTwo;
using ILSpy::Decompiler::CSharp::Resolver::Detail::TP;
using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here
// is span-shaped or needs a FindType registration; the identity conversions resolve over
// the stubs' own structural equality).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind`. The D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// A `LookupTypeParameter` overriding `AcceptVisitor` to dispatch to
// `visitor.VisitTypeParameter` (the C# `AbstractTypeParameter.AcceptVisitor` bridge) --
// the plain `LookupTypeParameter` routes to `VisitOtherType`, so the fixed-TP
// substitution's `VisitTypeParameter` would never fire for it (the D564/D570
// `VisitableTypeParameter` precedent). The inherited `OwnerType()` returns
// `SymbolKind::Method` and `Index()` the configured value (the real
// `InferTypeArguments` contract `typeParameters[i].Index == i`).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A `LookupTypeParameter` owned by a TYPE DEFINITION (not a method) -- the main entry's
// `OwnerType != SymbolKind.Method` contract-violation stub.
class ClassOwnedTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    TS::SymbolKind OwnerType() const override { return TS::SymbolKind::TypeDefinition; }
};

// A named method type parameter with the given index (the real `InferTypeArguments`
// contract is `typeParameters[i].Index == i`).
std::shared_ptr<VisitableTypeParameter> MakeTypeParam(std::string name, int index) {
    auto t = std::make_shared<VisitableTypeParameter>(std::move(name));
    t->SetIndex(index);
    return t;
}

// Build the `TP` state vector over the supplied type parameters (the vector order IS
// the index contract).
std::vector<TP> MakeState(const std::vector<std::shared_ptr<VisitableTypeParameter>>& tps) {
    std::vector<TP> state;
    state.reserve(tps.size());
    for (const auto& t : tps)
        state.emplace_back(*t);
    return state;
}

// The raw-pointer list the main entry takes (the `IMethod::TypeParameters()` /
// `OverloadResolutionCandidate::TypeParameters()` convention -- non-owning raw pointers).
std::vector<const ITypeParameter*> RawParams(
    const std::vector<std::shared_ptr<VisitableTypeParameter>>& tps) {
    std::vector<const ITypeParameter*> r;
    for (const auto& t : tps)
        r.push_back(t.get());
    return r;
}

// A `LookupTypeDefinition` subclass whose `GetMethods(filter, options)` returns a
// configured list, applying the filter faithfully (the D533 `MethodHostType` precedent).
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        (void)options;
        if (!filter)
            return methods_;
        std::vector<const IMethod*> r;
        for (const IMethod* m : methods_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const IMethod*> methods_;
};

std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
    return std::make_shared<MethodHostType>(
        std::move(name), "",
        FullTypeName(TopLevelTypeName("", std::move(name), 0)),
        kind, Accessibility::Public, Compilation(), nullptr);
}

// A `LookupMethod` kept alive in a static vector so the raw `const IMethod*` the host
// holds outlives the call (the D533 precedent).
const IMethod* MakeMethod(std::string name = "Invoke") {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A minimal `IParameter` with a configurable type and reference kind (the D531/D534
// `TestParameter` precedent; the enum references are fully qualified against the
// cross-scope name-hiding crux).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type,
                           ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                           std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `TestParameter` kept alive in a static vector so the raw `const IParameter*` the
// method holds outlives the call (the D534 precedent).
std::shared_ptr<TestParameter> MakeParam(ITypePtr type,
                                          ::ILSpy::Decompiler::TypeSystem::ReferenceKind rk
                                          = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None) {
    static std::vector<std::shared_ptr<TestParameter>> keep;
    auto p = std::make_shared<TestParameter>(std::move(type), rk);
    keep.push_back(p);
    return p;
}

const IMethod* ConfigureMethod(const IMethod* method,
                               const std::vector<std::shared_ptr<TestParameter>>& params,
                               ITypePtr returnType) {
    auto* m = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(method));
    std::vector<const IParameter*> paramPtrs;
    for (const auto& p : params)
        paramPtrs.push_back(p.get());
    m->SetParameters(std::move(paramPtrs));
    m->SetReturnType(std::move(returnType));
    return method;
}

// A delegate type whose `Invoke` takes the supplied parameter types and returns the
// supplied return type (the `Func`-like shape `GetDelegateOrExpressionTreeSignature`
// resolves).
std::shared_ptr<MethodHostType> MakeDelegate(const std::vector<ITypePtr>& paramTypes,
                                              ITypePtr returnType) {
    std::vector<std::shared_ptr<TestParameter>> params;
    for (const ITypePtr& t : paramTypes)
        params.push_back(MakeParam(t));
    auto host = MakeHost("D");
    const IMethod* invoke = MakeMethod("Invoke");
    ConfigureMethod(invoke, params, std::move(returnType));
    host->SetMethods({invoke});
    return host;
}

// A concrete `LambdaResolveResult` stub whose `GetInferredReturnType` RECORDS the
// received parameter types (the substitution output) and returns the configured return
// type (the D534/D570 `RecordingLambda` precedent; the recorder members are mutable
// because `GetInferredReturnType` is const).
class RecordingLambda : public LambdaResolveResult {
public:
    bool hasParameterList = true;
    bool isAnonymousMethod = false;
    bool isImplicitlyTyped = true;
    bool isAsync = false;
    ITypePtr returnType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    std::vector<const IParameter*> parameters;
    std::shared_ptr<ResolveResult> body =
        std::make_shared<TypeResolveResult>(
            std::make_shared<TS::KnownType>(KnownTypeCode::Void));
    mutable int inferredCallCount = 0;
    mutable std::vector<ITypePtr> lastParameterTypes;

    bool HasParameterList() const override { return hasParameterList; }
    bool IsAnonymousMethod() const override { return isAnonymousMethod; }
    bool IsImplicitlyTyped() const override { return isImplicitlyTyped; }
    bool IsAsync() const override { return isAsync; }
    ITypePtr GetInferredReturnType(const std::vector<ITypePtr>& parameterTypes) const override {
        ++inferredCallCount;
        lastParameterTypes = parameterTypes;
        return returnType;
    }
    std::vector<const IParameter*> Parameters() const override { return parameters; }
    const IType& ReturnType() const override { return *returnType; }
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> IsValid(
        const std::vector<ITypePtr>&, const ITypePtr&,
        ILSpy::Decompiler::CSharp::Resolver::CSharpConversions&) const override
    {
        return std::make_shared<ILSpy::Decompiler::CSharp::Resolver::LambdaConversion>();
    }
    ResolveResult& Body() const override { return *body; }
    std::unique_ptr<ResolveResult> ShallowClone() const override
    {
        return std::make_unique<RecordingLambda>(*this);
    }
protected:
    std::string ClassName() const override { return "RecordingLambda"; }
};

// A `TypeResolveResult` expression over the supplied type (the plain-expression shape
// the PhaseOne plain arm consumes).
std::shared_ptr<ResolveResult> Expr(ITypePtr type) {
    return std::make_shared<TypeResolveResult>(std::move(type));
}

// The default algorithm for every call (the C# default field initializer).
constexpr Res::TypeInferenceAlgorithm kCSharp4 = Res::TypeInferenceAlgorithm::CSharp4;

} // namespace

// ===========================================================================
// InferTypeArguments (the main entry, TypeInference.cs lines 116-170).
// ===========================================================================

// `M<T>(T x)` with an `int` argument: the plain-expression lower-bound inference fixes
// T to the argument's own type.
TEST(TypeInferenceInferTypeArgumentsTest, SingleArgumentLowerBoundInfersAndFixes)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {Expr(int32)}, {t0}, success,
        std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int32.get());
}

// `M<T1, T2>(T1 a, T2 b)` with `(int, string)` arguments: the two parameters infer and
// fix independently.
TEST(TypeInferenceInferTypeArgumentsTest, TwoParametersInferIndependently)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T1", 0);
    auto t1 = MakeTypeParam("T2", 1);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr str = Def(KnownTypeCode::String, TypeKind::Class);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0, t1}),
        {Expr(int32), Expr(str)}, {t0, t1}, success, std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), int32.get());
    EXPECT_EQ(result[1].get(), str.get());
}

// A non-generic method shape (no type parameters): the inference trivially succeeds
// with the empty result.
TEST(TypeInferenceInferTypeArgumentsTest, EmptyTypeParameterListSucceedsTrivially)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = false;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, {}, {Expr(int32)}, {int32}, success,
        std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    EXPECT_TRUE(result.empty());
}

// `M<T>(int x)`: the parameter type does not mention T, so T accumulates no bounds and
// the fix fails -- success=false and the unfixed position reports the UnknownType null
// object.
TEST(TypeInferenceInferTypeArgumentsTest, UnmentionedParameterFailsWithUnknownTypeReport)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {Expr(int32)}, {int32}, success,
        std::nullopt, kCSharp4);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// A `null`-literal-typed argument fails the `IsValidType` gate -- no inference runs and
// the inference fails.
TEST(TypeInferenceInferTypeArgumentsTest, NullLiteralArgumentMakesNoInference)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}),
        {Expr(std::make_shared<SpecialType>(TypeKind::Null))}, {t0}, success,
        std::nullopt, kCSharp4);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// `M<T>(ref T x)` with a by-ref `int` argument: the parameter type is a by-reference
// shape, so the argument's (by-reference) type exact-infers -- both by-ref shapes
// recurse on their elements and T's EXACT bound is the int element.
TEST(TypeInferenceInferTypeArgumentsTest, ByRefParameterShapeTakesExactInference)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr argType = std::make_shared<TS::ByReferenceType>(int32);
    ITypePtr paramType = std::make_shared<TS::ByReferenceType>(t0);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {Expr(argType)}, {paramType},
        success, std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int32.get());
}

// `M<T>(Func<int, T> f)` with an implicitly-typed lambda whose inferred return type is
// string: the lambda's output type (the delegate return type T) mentions the unfixed T
// while its input types (the delegate's int parameter) do not, so the output-type
// inference fires in PhaseOne and T fixes to the lambda's inferred return type.
TEST(TypeInferenceInferTypeArgumentsTest, LambdaArgumentInfersReturnTypeViaOutputInference)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr str = Def(KnownTypeCode::String, TypeKind::Class);
    auto host = MakeDelegate({int32}, t0);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->returnType = str;
    lambda->parameters = {MakeParam(int32).get()};
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {lambda}, {host}, success,
        std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), str.get());
    // The output-type inference resolved the delegate signature and threaded its
    // parameter types (with the unfixed-TP substitution, an identity here) into the
    // lambda's return-type inference.
    EXPECT_EQ(lambda->inferredCallCount, 1);
    ASSERT_EQ(lambda->lastParameterTypes.size(), 1u);
    EXPECT_EQ(lambda->lastParameterTypes[0].get(), int32.get());
}

// `M<T>(Func<T, T> f)` with an EXPLICITLY-typed lambda whose declared parameter type is
// string (and whose inferred return type is string too): the explicit-parameter-type
// inference takes the declared parameter type as T's EXACT bound.
TEST(TypeInferenceInferTypeArgumentsTest, ExplicitlyTypedLambdaExactInfersParameterType)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr str = Def(KnownTypeCode::String, TypeKind::Class);
    auto host = MakeDelegate({t0}, t0);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->isImplicitlyTyped = false;
    lambda->returnType = str;
    lambda->parameters = {MakeParam(str).get()};
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {lambda}, {host}, success,
        std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), str.get());
}

// The flagship dependency shape `M<T1, T2>(Func<T1, T2> f, T1 x)` with `(lambda, int)`
// arguments: T2 DEPENDS ON T1 (T1 occurs in the lambda argument's input types, T2 in
// its output types), so PhaseTwo fixes T1 first (the int argument's lower bound), then
// the repeat round's output-type inference unblocks T2 (the lambda's inferred return
// type string lower-bounds it). The PhaseTwo output-type inference threads T1's FIXED
// decision (int) into the nested lambda through the fixed-TP substitution.
TEST(TypeInferenceInferTypeArgumentsTest, DependentParametersFixInDependencyOrder)
{
    CSharpConversions conversions(Compilation());
    auto t1 = MakeTypeParam("T1", 0);
    auto t2 = MakeTypeParam("T2", 1);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr str = Def(KnownTypeCode::String, TypeKind::Class);
    auto host = MakeDelegate({t1}, t2);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->returnType = str;
    lambda->parameters = {MakeParam(int32).get()};
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t1, t2}),
        {lambda, Expr(int32)}, {host, t1}, success, std::nullopt, kCSharp4);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), int32.get());
    EXPECT_EQ(result[1].get(), str.get());
    // The PhaseTwo output-type inference ran once (PhaseOne skipped it: the lambda's
    // input types ALSO mentioned the then-unfixed T1) and threaded the FIXED T1 (int)
    // into the lambda's return-type inference through the fixed-TP substitution.
    EXPECT_EQ(lambda->inferredCallCount, 1);
    ASSERT_EQ(lambda->lastParameterTypes.size(), 1u);
    EXPECT_EQ(lambda->lastParameterTypes[0].get(), int32.get());
}

// A type parameter with the WRONG INDEX (the C# ArgumentException contract violation)
// takes the documented soft failure: success=false and the all-UnknownType report.
TEST(TypeInferenceInferTypeArgumentsTest, WrongIndexSoftFailsWithUnknownReport)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 1);  // index 1 in position 0
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {Expr(int32)}, {t0}, success,
        std::nullopt, kCSharp4);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// A type parameter owned by a TYPE DEFINITION (the C# ArgumentException contract
// violation -- the inference requires method-owned parameters) takes the soft failure.
TEST(TypeInferenceInferTypeArgumentsTest, ClassOwnedTypeParameterSoftFails)
{
    CSharpConversions conversions(Compilation());
    auto classOwned = std::make_shared<ClassOwnedTypeParameter>("T");
    classOwned->SetIndex(0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, {classOwned.get()}, {Expr(int32)},
        {std::static_pointer_cast<IType>(classOwned)}, success, std::nullopt, kCSharp4);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// A NULL argument entry (the C# ArgumentNullException contract violation) takes the
// soft failure.
TEST(TypeInferenceInferTypeArgumentsTest, NullArgumentEntrySoftFails)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}),
        {std::shared_ptr<ResolveResult>()}, {t0}, success, std::nullopt, kCSharp4);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// A NULL parameter-type entry (the C# ArgumentNullException contract violation) takes
// the soft failure.
TEST(TypeInferenceInferTypeArgumentsTest, NullParameterTypeEntrySoftFails)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArguments(
        Compilation(), conversions, RawParams({t0}), {Expr(int32)}, {ITypePtr()},
        success, std::nullopt, kCSharp4);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// ===========================================================================
// PhaseOne / PhaseTwo (the direct private-worker tests).
// ===========================================================================

// PhaseOne accumulates the plain-expression lower bound without fixing anything.
TEST(TypeInferenceInferTypeArgumentsTest, PhaseOneAccumulatesLowerBoundDirectly)
{
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    PhaseOne(Compilation(), state, {Expr(int32)}, {t0}, std::nullopt);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), int32.get());
    EXPECT_FALSE(state[0].IsFixed());
}

// PhaseTwo fixes a bounded parameter that depends on nothing.
TEST(TypeInferenceInferTypeArgumentsTest, PhaseTwoFixesUnfixedWithNoDependenciesDirectly)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddLowerBound(int32);
    const std::vector<std::vector<bool>> matrix{{false}};
    EXPECT_TRUE(PhaseTwo(Compilation(), conversions, state, matrix, {}, {},
                         std::nullopt, kCSharp4));
    EXPECT_TRUE(state[0].IsFixed());
    EXPECT_EQ(state[0].FixedTo.get(), int32.get());
}

// PhaseTwo fails when the selected parameter has no bounds to fix from (the fix error).
// NOTE the faithful fix semantics: even the FAILED fix assigns `FixedTo` (the empty
// candidate list reduces to the `UnknownType` null object) before returning false --
// the C# `tp.FixedTo = GetFirstTypePreferNonInterfaces(types)` runs unconditionally
// (the D571 learning), so the parameter reports fixed-to-UnknownType on the failure.
TEST(TypeInferenceInferTypeArgumentsTest, PhaseTwoFailsWhenNothingFixableDirectly)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    std::vector<TP> state = MakeState({t0});
    const std::vector<std::vector<bool>> matrix{{false}};
    EXPECT_FALSE(PhaseTwo(Compilation(), conversions, state, matrix, {}, {},
                          std::nullopt, kCSharp4));
    EXPECT_TRUE(state[0].IsFixed());
    EXPECT_EQ(state[0].FixedTo->Kind(), TypeKind::Unknown);
}

// ===========================================================================
// InferTypeArgumentsFromBounds (the bounds-based entry, TypeInference.cs lines
// 179-208).
// ===========================================================================

// A single lower bound against the target type parameter fixes it to the bound.
TEST(TypeInferenceInferTypeArgumentsTest, FromBoundsLowerBoundFixes)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArgumentsFromBounds(
        Compilation(), conversions, RawParams({t0}), *t0, {int32}, {}, success,
        kCSharp4, /*nestingLevel*/ 0);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int32.get());
}

// A single upper bound against the target type parameter fixes it to the bound (the
// upper-bound inference).
TEST(TypeInferenceInferTypeArgumentsTest, FromBoundsUpperBoundFixes)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    ITypePtr str = Def(KnownTypeCode::String, TypeKind::Class);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArgumentsFromBounds(
        Compilation(), conversions, RawParams({t0}), *t0, {}, {str}, success,
        kCSharp4, /*nestingLevel*/ 0);
    EXPECT_TRUE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), str.get());
}

// No bounds at all: the fix fails and the parameter reports the UnknownType null
// object.
TEST(TypeInferenceInferTypeArgumentsTest, FromBoundsNoBoundsFails)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 0);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArgumentsFromBounds(
        Compilation(), conversions, RawParams({t0}), *t0, {}, {}, success,
        kCSharp4, /*nestingLevel*/ 0);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}

// Two parameters, only one of them mentioned in the target: the fix runs for BOTH (the
// C# non-short-circuit `success &=`), so the inference fails overall while the
// mentioned parameter still reports its fixed type and the unmentioned one the
// UnknownType null object.
TEST(TypeInferenceInferTypeArgumentsTest, FromBoundsAccumulatesAcrossParameters)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T1", 0);
    auto t1 = MakeTypeParam("T2", 1);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArgumentsFromBounds(
        Compilation(), conversions, RawParams({t0, t1}), *t0, {int32}, {}, success,
        kCSharp4, /*nestingLevel*/ 0);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), int32.get());
    EXPECT_EQ(result[1]->Kind(), TypeKind::Unknown);
}

// The wrong-index contract violation takes the soft failure here too (the C#
// ArgumentException).
TEST(TypeInferenceInferTypeArgumentsTest, FromBoundsIndexViolationSoftFails)
{
    CSharpConversions conversions(Compilation());
    auto t0 = MakeTypeParam("T", 2);  // index 2 in position 0
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    std::vector<ITypePtr> result = InferTypeArgumentsFromBounds(
        Compilation(), conversions, RawParams({t0}), *t0, {int32}, {}, success,
        kCSharp4, /*nestingLevel*/ 0);
    EXPECT_FALSE(success);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->Kind(), TypeKind::Unknown);
}
