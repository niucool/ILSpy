// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
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

// Tests for the `TypeInference` MakeOutputTypeInference region (the fourth slice of the
// TypeInference long pole): `Detail::IsValidType` (TypeInference.cs lines 314-317),
// `Detail::GetSubstitutionForFixedTPs` (lines 602-611), and `Detail::
// MakeOutputTypeInference` (lines 522-600, C# 4.0 spec section 7.5.2.6 "Output type
// inferences").
//
// The load-bearing cruxes:
//  (a) the lambda arm threads the fixed-TP substitution through the delegate signature's
//      parameter types into `GetInferredReturnType` -- a FIXED type parameter substitutes
//      to its `FixedTo`, an unfixed one to `SpecialType.UnknownType` -- and the inferred
//      return type lower-bounds the delegate return type's unfixed TP;
//  (b) a parameter-count mismatch between the delegate signature and the lambda returns
//      WITHOUT calling `GetInferredReturnType` ("cannot infer due to mismatched parameter
//      lists");
//  (c) an EXPLICITLY-typed lambda receives the C# null parameter array (the empty
//      vector) and its inferred return type still lower-bounds;
//  (d) the plain-expression arm lower-bounds `e.Type` into `t`'s unfixed TP, gated on
//      `IsValidType` (the Unknown / null-literal / NoType null-object kinds make no
//      inference);
//  (e) a method-group argument makes NO inference (the deferred `PerformOverloadResolution`
//      arm) and never falls through to the plain-expression arm.
//
// The stubs mirror the TypeInferenceMakeInference_Test conventions (`Def`, `MakeState`,
// `MethodHostType`/`MakeMethod`/`ConfigureMethod`, `TestParameter`, `MakeHost`) plus the
// `RecordingLambda` (a `TestLambda` whose `GetInferredReturnType` records the received
// parameter types) and the `VisitableTypeParameter` (a `LookupTypeParameter` overriding
// `AcceptVisitor` -> `visitor.VisitTypeParameter`, the C# `AbstractTypeParameter.
// AcceptVisitor` bridge -- the plain `LookupTypeParameter` routes to `VisitOtherType`, so
// `TypeParameterSubstitution.VisitTypeParameter` would never fire for it).

#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

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

using ILSpy::Decompiler::CSharp::Resolver::Detail::GetSubstitutionForFixedTPs;
using ILSpy::Decompiler::CSharp::Resolver::Detail::IsValidType;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MakeOutputTypeInference;
using ILSpy::Decompiler::CSharp::Resolver::Detail::TP;
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
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here is
// span-shaped, so the `FirstClassSpanTypes` gate is irrelevant).
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
// the plain `LookupTypeParameter` routes to `VisitOtherType`, so the substitution's
// `VisitTypeParameter` would never fire for it (the D564 `VisitableTypeParameter`
// precedent). The inherited `OwnerType()` returns `SymbolKind::Method`, so the
// substitution reads the METHOD type-argument list (the fixed types).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// Build the `TP` state vector over the supplied type parameters (the vector order IS the
// index contract).
std::vector<TP> MakeState(const std::vector<std::shared_ptr<VisitableTypeParameter>>& tps) {
    std::vector<TP> state;
    state.reserve(tps.size());
    for (const auto& t : tps)
        state.emplace_back(*t);
    return state;
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

// A concrete `LambdaResolveResult` stub whose `GetInferredReturnType` RECORDS the received
// parameter types (the substitution output) and returns the configured return type (the
// D534 `TestLambda` precedent; the recorder members are mutable because
// `GetInferredReturnType` is const).
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

} // namespace

// ===========================================================================
// IsValidType (TypeInference.cs lines 314-317).
// ===========================================================================

// A real type (an `int` definition) is valid -- the plain-expression arm's gate passes.
TEST(TypeInferenceMakeOutputTypeInferenceTest, ValidTypeIsAccepted)
{
    EXPECT_TRUE(IsValidType(*Def(KnownTypeCode::Int32)));
}

// The error type (`SpecialType.UnknownType`) is rejected -- an unresolved expression makes
// no inference.
TEST(TypeInferenceMakeOutputTypeInferenceTest, UnknownTypeIsRejected)
{
    EXPECT_FALSE(IsValidType(*TS::UnknownType()));
}

// The `null` literal's type (`TypeKind::Null`) is rejected.
TEST(TypeInferenceMakeOutputTypeInferenceTest, NullTypeIsRejected)
{
    EXPECT_FALSE(IsValidType(*std::make_shared<TS::SpecialType>(TS::TypeKind::Null)));
}

// `NoType` (`TypeKind::None` -- a lambda's / method group's own type) is rejected.
TEST(TypeInferenceMakeOutputTypeInferenceTest, NoTypeIsRejected)
{
    EXPECT_FALSE(IsValidType(*TS::NoType()));
}

// ===========================================================================
// GetSubstitutionForFixedTPs (TypeInference.cs lines 602-611).
// ===========================================================================

// An UNFIXED type parameter substitutes to `SpecialType.UnknownType` (the C# `FixedTo ??
// SpecialType.UnknownType` fallback) -- the still-unresolved positions are marked.
TEST(TypeInferenceMakeOutputTypeInferenceTest, UnfixedTypeParameterSubstitutesToUnknownType)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    TypeParameterSubstitution subst = GetSubstitutionForFixedTPs(state, std::nullopt);
    ITypePtr r = t0->AcceptVisitor(subst);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->Kind(), TypeKind::Unknown);
}

// A FIXED type parameter substitutes to its `FixedTo` instance (pointer identity -- the
// very instance the inference fixed it to).
TEST(TypeInferenceMakeOutputTypeInferenceTest, FixedTypeParameterSubstitutesToItsFixedType)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    auto longT = Def(KnownTypeCode::Int64);
    std::vector<TP> state = MakeState({t0});
    state[0].FixedTo = longT;
    TypeParameterSubstitution subst = GetSubstitutionForFixedTPs(state, std::nullopt);
    ITypePtr r = t0->AcceptVisitor(subst);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r.get(), longT.get());
}

// The fixed types form the METHOD type-argument list (always PRESENT, even for an empty
// state); an absent `classTypeArguments` (the C# `null`) keeps the class parameters
// unmodified.
TEST(TypeInferenceMakeOutputTypeInferenceTest, FixedTypesFormTheMethodTypeArgumentList)
{
    std::vector<TP> state;
    TypeParameterSubstitution subst = GetSubstitutionForFixedTPs(state, std::nullopt);
    ASSERT_TRUE(subst.MethodTypeArguments().has_value());
    EXPECT_TRUE(subst.MethodTypeArguments()->empty());
    EXPECT_FALSE(subst.ClassTypeArguments().has_value());
}

// The class type arguments thread from the `InferTypeArguments` caller into the
// substitution's class list (the same instance).
TEST(TypeInferenceMakeOutputTypeInferenceTest, ClassTypeArgumentsThreadThrough)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto stringT = Def(KnownTypeCode::String, TypeKind::Class);
    std::optional<std::vector<ITypePtr>> classArgs{std::vector<ITypePtr>{stringT}};
    TypeParameterSubstitution subst = GetSubstitutionForFixedTPs(state, classArgs);
    ASSERT_TRUE(subst.ClassTypeArguments().has_value());
    ASSERT_EQ(subst.ClassTypeArguments()->size(), 1u);
    EXPECT_EQ((*subst.ClassTypeArguments())[0].get(), stringT.get());
}

// ===========================================================================
// MakeOutputTypeInference (TypeInference.cs lines 522-600, spec section 7.5.2.6).
// ===========================================================================

// The plain-expression arm: a valid-typed expression lower-bounds its type into the
// parameter type's unfixed TP (`int` against the tracked `T`).
TEST(TypeInferenceMakeOutputTypeInferenceTest, PlainExpressionLowerBoundsItsTypeIntoTheParameterType)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto intT = Def(KnownTypeCode::Int32);
    auto rr = std::make_shared<TypeResolveResult>(intT);
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *rr, *t0);
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), intT.get());
}

// The `IsValidType` gate: an expression of the error type (`UnknownType`) makes no
// inference.
TEST(TypeInferenceMakeOutputTypeInferenceTest, UnknownTypeExpressionMakesNoInference)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto rr = std::make_shared<TypeResolveResult>(TS::UnknownType());
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *rr, *t0);
    EXPECT_FALSE(state[0].HasBounds());
}

// The `IsValidType` gate: the `null` literal's type (`TypeKind::Null`) makes no inference.
TEST(TypeInferenceMakeOutputTypeInferenceTest, NullLiteralExpressionMakesNoInference)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto rr = std::make_shared<TypeResolveResult>(
        std::make_shared<TS::SpecialType>(TS::TypeKind::Null));
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *rr, *t0);
    EXPECT_FALSE(state[0].HasBounds());
}

// The lambda arm, implicitly typed: the delegate signature's parameter types flow through
// the fixed-TP substitution (an unfixed tracked parameter substitutes to `UnknownType`)
// into `GetInferredReturnType`, whose result lower-bounds the delegate return type's
// unfixed TP.
TEST(TypeInferenceMakeOutputTypeInferenceTest, LambdaImplicitlyTypedThreadsSubstitutedParameterTypes)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto intT = Def(KnownTypeCode::Int32);
    // The delegate `D`: `R Invoke(T p)` -- both positions carry the tracked parameter.
    auto delegateType = MakeDelegate({t0}, t0);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};  // the count matches Invoke's 1 parameter
    lambda->returnType = intT;
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *lambda, *delegateType);
    // The delegate parameter type (the tracked T) was substituted with the unfixed fallback
    // before reaching `GetInferredReturnType`.
    ASSERT_EQ(lambda->inferredCallCount, 1);
    ASSERT_EQ(lambda->lastParameterTypes.size(), 1u);
    EXPECT_EQ(lambda->lastParameterTypes[0]->Kind(), TypeKind::Unknown);
    // The inferred return type lower-bounds the delegate return type (the tracked T).
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), intT.get());
}

// The flagship crux: a delegate `T1 Invoke(T0 p)` over TWO tracked parameters with `T0`
// FIXED to `long` -- the fixed decision threads into the nested lambda's parameter types
// (`GetInferredReturnType` receives `long`), while the unfixed `T1` (the delegate return
// type) takes the inferred return type as its lower bound and the fixed `T0` takes none.
TEST(TypeInferenceMakeOutputTypeInferenceTest, FixedTypeParameterThreadsIntoNestedLambda)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T0");
    t0->SetIndex(0);
    auto t1 = std::make_shared<VisitableTypeParameter>("T1");
    t1->SetIndex(1);
    std::vector<TP> state = MakeState({t0, t1});
    auto longT = Def(KnownTypeCode::Int64);
    auto intT = Def(KnownTypeCode::Int32);
    state[0].FixedTo = longT;
    auto delegateType = MakeDelegate({t0}, t1);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *lambda, *delegateType);
    // The FIXED `T0` substituted to its `FixedTo` inside the parameter types fed to the
    // nested lambda.
    ASSERT_EQ(lambda->inferredCallCount, 1);
    ASSERT_EQ(lambda->lastParameterTypes.size(), 1u);
    EXPECT_EQ(lambda->lastParameterTypes[0].get(), longT.get());
    // The unfixed `T1` (the delegate return type) takes the inferred return as a lower
    // bound; the fixed `T0` takes none.
    ASSERT_EQ(state[1].LowerBounds.size(), 1u);
    EXPECT_EQ(state[1].LowerBounds[0].get(), intT.get());
    EXPECT_TRUE(state[0].LowerBounds.empty());
}

// The parameter-count mismatch: the delegate signature has 1 parameter but the lambda has
// 2 -- the arm returns WITHOUT calling `GetInferredReturnType` ("cannot infer due to
// mismatched parameter lists").
TEST(TypeInferenceMakeOutputTypeInferenceTest, LambdaParameterCountMismatchSkipsInference)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto intT = Def(KnownTypeCode::Int32);
    auto delegateType = MakeDelegate({t0}, t0);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get(), MakeParam(intT).get()};
    lambda->returnType = intT;
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *lambda, *delegateType);
    EXPECT_EQ(lambda->inferredCallCount, 0);
    EXPECT_FALSE(state[0].HasBounds());
}

// The explicitly-typed lambda: `GetInferredReturnType` receives the C# null parameter
// array (the empty vector -- the lambda already knows its parameter types), and its
// inferred return type still lower-bounds the delegate return type.
TEST(TypeInferenceMakeOutputTypeInferenceTest, ExplicitlyTypedLambdaReceivesNoParameterTypes)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto intT = Def(KnownTypeCode::Int32);
    auto delegateType = MakeDelegate({t0}, t0);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->isImplicitlyTyped = false;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *lambda, *delegateType);
    ASSERT_EQ(lambda->inferredCallCount, 1);
    EXPECT_TRUE(lambda->lastParameterTypes.empty());
    ASSERT_EQ(state[0].LowerBounds.size(), 1u);
    EXPECT_EQ(state[0].LowerBounds[0].get(), intT.get());
}

// A lambda against a NON-delegate target (a `Class`, no `Invoke`): the delegate signature
// never resolves, the lambda falls through to the plain-expression arm, and its own type
// (`NoType`) fails the `IsValidType` gate -- no inference.
TEST(TypeInferenceMakeOutputTypeInferenceTest, LambdaWithNonDelegateTargetMakesNoInference)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto intT = Def(KnownTypeCode::Int32);
    auto classType = MakeHost("C", TypeKind::Class);
    auto lambda = std::make_shared<RecordingLambda>();
    lambda->isImplicitlyTyped = true;
    lambda->parameters = {MakeParam(intT).get()};
    lambda->returnType = intT;
    MakeOutputTypeInference(Compilation(), state, std::nullopt, *lambda, *classType);
    EXPECT_EQ(lambda->inferredCallCount, 0);
    EXPECT_FALSE(state[0].HasBounds());
}

// A method-group argument makes NO inference (the deferred `PerformOverloadResolution`
// arm) and never falls through to the plain-expression arm (the C# unconditional
// `return` inside the `mgrr` block).
TEST(TypeInferenceMakeOutputTypeInferenceTest, MethodGroupArgumentMakesNoInference)
{
    auto t0 = std::make_shared<VisitableTypeParameter>("T");
    t0->SetIndex(0);
    std::vector<TP> state = MakeState({t0});
    auto delegateType = MakeDelegate({t0}, t0);
    Res::MethodGroupResolveResult mgrr(nullptr, "M", {}, {});
    MakeOutputTypeInference(Compilation(), state, std::nullopt, mgrr, *delegateType);
    EXPECT_FALSE(state[0].HasBounds());
}
