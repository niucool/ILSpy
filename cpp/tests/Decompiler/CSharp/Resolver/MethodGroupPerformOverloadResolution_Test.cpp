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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `MethodGroupResolveResult::PerformOverloadResolution` (MethodGroupResolveResult.cs
// lines 248-307) -- the method group's overload-resolution entry point composing the ported
// `OverloadResolution` engine: it builds an `OverloadResolution` over the given arguments (with
// the group's `TypeArguments` as the given type arguments), sets the four input properties from
// its flags, adds the group's own method lists (the derived-type-hides-base-methods scan), and
// returns the resolution whose output properties observe the folded state. Also covers the two
// public `OverloadResolution` methods the entry point composes with (and that were previously
// only reachable through the `Detail::` free functions): `LogCandidateAddingResult` and
// `GetBestCandidateWithSubstitutedTypeArguments`.
//
// The load-bearing cruxes:
//  (a) the METHOD-LIST WIRING -- the group's own buckets flow into `AddMethodLists`: a single
//      applicable method is the best candidate, and the DERIVED-TYPE HIDING (Base+Derived buckets
//      with `Derived : Base`) leaves no ambiguity (both-folded would tie 0/0);
//  (b) the OVERLOAD SELECTION -- `M(int)` beats `M(long)` for an int argument (the identity
//      conversion is better than the widening), and two IDENTICAL signatures tie 0/0
//      (`IsAmbiguous`, `BestCandidateAmbiguousWith`);
//  (c) the TYPE-ARGUMENT THREADING -- the group's `TypeArguments` become the resolution's
//      explicitly given type arguments (the generic `M<T>(T x)` resolves with the given int
//      instead of inferring), and an EMPTY group `TypeArguments` leaves them unset (the
//      inference path);
//  (d) the INPUT-FLAG THREADING -- `allowExpandingParams` false rejects a params-collection call
//      (the normal-form TooManyPositionalArguments) while the default true resolves the
//      expanded form (`BestCandidateIsExpandedForm`);
//  (e) the named-argument threading -- `GetArgumentToParameterMap` maps the named call;
//  (f) the public `GetBestCandidateWithSubstitutedTypeArguments` delegation -- a non-generic
//      best returns the member as-is (pointer identity), a generic best is re-specialized, and
//      an empty group yields null;
//  (g) the public `LogCandidateAddingResult` -- a state-preserving no-op (logging disabled).
//
// The stubs mirror the AddMethodLists_Test / CalculateCandidate_Test conventions
// (`TestMethod`/`TestParameter`, the `VisitableTypeParameter` AcceptVisitor bridge, the
// `MakeDef` primitive definitions, the shared-instance type-cache modeling). A LOCAL
// `CSharpConversions` is threaded through every GENERIC-method test (its cache dies with the
// call; only the null-conversions test exercises the lazy `CSharpConversions::Get` fallback,
// and it uses the non-generic shape that never populates the shared instance's
// `ImplicitConversion(IType, IType)` cache -- the D540 cache-dangling caveat).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here needs a
// `FindType` registration).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeParameter` overriding `AcceptVisitor` to dispatch to
// `visitor.VisitTypeParameter` (the C# `AbstractTypeParameter.AcceptVisitor` bridge) -- the
// plain `LookupTypeParameter` routes to `VisitOtherType`, so the type-parameter substitution
// would never fire for it (the D564 `VisitableTypeParameter` precedent).
// The `AcceptVisitor -> visitor.VisitTypeParameter(*this)` bridge (the plain
// `LookupTypeParameter` routes to `VisitOtherType`, so the type-parameter substitution would
// never fire for it -- the D564 precedent) PLUS a configurable `Owner` (the real-method-
// type-parameter shape: the lazy constraint validation behind `BestCandidateErrors` resolves
// conversions from the owner's compilation; a null `Owner` is the dummy shape whose documented
// fallback yields `MethodConstraintsNotSatisfied` -- the OutputProperties_Test
// `OwnedTypeParameter` precedent, merged into one stub since the generic tests need both).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    void SetOwner(const ILSpy::Decompiler::TypeSystem::IEntity* owner) { owner_ = owner; }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return owner_; }
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
private:
    const ILSpy::Decompiler::TypeSystem::IEntity* owner_ = nullptr;
};

// The owning entity for the type parameters -- a real `IEntity` whose `Compilation()` is the
// static test compilation (the OutputProperties_Test `OwnerEntity` precedent).
const ILSpy::Decompiler::TypeSystem::IEntity* OwnerEntity() {
    static auto d = std::make_shared<LookupTypeDefinition>(
        "OwnerType", "",
        FullTypeName(TopLevelTypeName("", "OwnerType", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    return d.get();
}

// A primitive definition (`LookupTypeDefinition`, IS-A `ITypeDefinition`) with the given
// `KnownTypeCode` / `TypeKind` -- the D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A named class definition for a method-list bucket's `DeclaringType`, with optional direct
// base types (the `LookupTypeDefinition::AddDirectBaseType` graph; the SAME shared instance
// must be used as the bucket's `DeclaringType` and in the derived type's base list -- the
// stub's `StructuralEquals` is IDENTITY equality).
std::shared_ptr<LookupTypeDefinition> MakeTypeDef(std::string name,
                                                   std::initializer_list<ITypePtr> bases = {}) {
    auto type = std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    for (const ITypePtr& base : bases)
        type->AddDirectBaseType(base);
    return type;
}

// A minimal `IParameter` over a configured type with configurable `IsParams`/`IsOptional` (the
// D550 `TestParameter` precedent).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p",
                           bool isParams = false, bool isOptional = false)
        : name_(std::move(name)), type_(std::move(type)),
          isParams_(isParams), isOptional_(isOptional) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
    bool isOptional_;
};

// A `LookupMethod` with configurable `Parameters` (inherited) and method `TypeParameters`, plus
// a configurable `Specialize` result for the re-specialization tests (the D577 `TestMethod`
// precedent extended with the D578 recording `Specialize`).
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::string name = "M")
        : LookupMethod(std::move(name), Compilation()) {}
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
    void SetSpecialized(const ILSpy::Decompiler::TypeSystem::IMethod* m) { specialized_ = m; }
    int SpecializeCallCount() const { return specializeCallCount_; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
    const IMethod* Specialize(const TypeParameterSubstitution*) const override
    {
        specializeCallCount_++;
        return specialized_ != nullptr ? specialized_ : this;
    }
private:
    std::vector<const ITypeParameter*> typeParameters_;
    const ILSpy::Decompiler::TypeSystem::IMethod* specialized_ = nullptr;
    mutable int specializeCallCount_ = 0;
};

// A plain `ResolveResult` over the given type (the base class is concrete).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// Builds a method group over the given buckets (a null target; the group's own `TypeArguments`
// default to empty).
std::unique_ptr<MethodGroupResolveResult> Group(
    std::vector<MethodListWithDeclaringType> methodLists,
    std::vector<ITypePtr> typeArguments = {}) {
    return std::make_unique<MethodGroupResolveResult>(
        nullptr, "M", std::move(methodLists), std::move(typeArguments));
}

// Runs `PerformOverloadResolution` with the C# flag defaults and a LOCAL `CSharpConversions`
// threaded through (the D540 cache-dangling caveat: a local instance's cache dies with the
// call, so test-local types never dangle in the shared `CSharpConversions::Get` instance).
std::unique_ptr<OverloadResolution> Resolve(
    const MethodGroupResolveResult& group,
    const std::vector<std::shared_ptr<ResolveResult>>& arguments,
    const std::optional<std::vector<std::string>>& argumentNames = std::nullopt,
    bool allowExpandingParams = true,
    bool allowOptionalParameters = true) {
    CSharpConversions localConversions(Compilation());
    return group.PerformOverloadResolution(Compilation(), arguments, argumentNames,
                                           /*allowExtensionMethods*/true,
                                           allowExpandingParams,
                                           allowOptionalParameters,
                                           /*allowImplicitIn*/true,
                                           /*checkForOverflow*/false,
                                           &localConversions);
}

// ---- The method-list wiring ----

// A single applicable method in the group: the returned resolution's best candidate IS that
// method (the group's buckets flow into `AddMethodLists`).
TEST(PerformOverloadResolutionTest, SingleApplicableMethodIsBest)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_TRUE(resolution->FoundApplicableCandidate());
    EXPECT_FALSE(resolution->IsAmbiguous());
    EXPECT_EQ(resolution->BestCandidateErrors(), OverloadResolutionErrors::None);
}

// The derived-type hiding flows through the group's buckets: `[Base { M(int) }, Derived {
// M(int) }]` with `Derived : Base` -- both-folded would tie 0/0 (ambiguous); the hiding keeps
// the Base list out, so the derived method is the sole best.
TEST(PerformOverloadResolutionTest, DerivedTypeHidingThroughGroup)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived", {base});
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(base, {&baseMethod}),
                          MethodListWithDeclaringType(derived, {&derivedMethod})}),
                  {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &derivedMethod);
    EXPECT_EQ(resolution->BestCandidateAmbiguousWith(), nullptr);
    EXPECT_FALSE(resolution->IsAmbiguous());
    EXPECT_TRUE(resolution->FoundApplicableCandidate());
}

// ---- The overload selection ----

// `M(int)` beats `M(long)` for an int argument: the identity conversion is better than the
// numeric widening (the better-conversion loop in `BetterFunctionMember`).
TEST(PerformOverloadResolutionTest, BetterConversionPicksMatchingOverload)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    auto declaring = MakeTypeDef("C");
    TestParameter intParam(intType, "x");
    TestMethod intMethod("M");
    intMethod.SetParameters({&intParam});
    TestParameter longParam(longType, "y");
    TestMethod longMethod("M");
    longMethod.SetParameters({&longParam});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&intMethod, &longMethod})}),
                  {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &intMethod);
    EXPECT_TRUE(resolution->FoundApplicableCandidate());
}

// Two IDENTICAL signatures (sharing the ONE int instance, the type-cache model) tie 0/0: the
// second becomes `BestCandidateAmbiguousWith` and `IsAmbiguous()` is true.
TEST(PerformOverloadResolutionTest, IdenticalSignaturesAreAmbiguous)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(intType, "x");
    TestMethod method1("M");
    method1.SetParameters({&param});
    TestMethod method2("M");
    method2.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method1, &method2})}),
                  {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method1);
    EXPECT_EQ(resolution->BestCandidateAmbiguousWith(), &method2);
    EXPECT_TRUE(resolution->IsAmbiguous());
    EXPECT_TRUE(resolution->FoundApplicableCandidate());
}

// ---- The type-argument threading ----

// The generic `M<T>(T x)` called with an int argument and NO group type arguments: the
// resolution INFERS `T = int` (the inference path -- `InferredTypeArguments` reports the
// inferred type).
TEST(PerformOverloadResolutionTest, GenericMethodInfersTypeArguments)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    auto t = std::make_shared<VisitableTypeParameter>("T");
    t->SetIndex(0);
    t->SetOwner(OwnerEntity());
    TestParameter param(t, "x");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    ASSERT_EQ(resolution->InferredTypeArguments().size(), 1u);
    ASSERT_NE(resolution->InferredTypeArguments()[0], nullptr);
    EXPECT_EQ(resolution->InferredTypeArguments()[0].get(), intType.get());
}

// The generic `M<T>(T x)` with the group's `TypeArguments = [int]`: the group's type
// arguments become the resolution's EXPLICITLY GIVEN type arguments (the ctor threading),
// and the candidate resolves with the given type (no inference needed).
TEST(PerformOverloadResolutionTest, GroupTypeArgumentsBecomeExplicitlyGiven)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    auto t = std::make_shared<VisitableTypeParameter>("T");
    t->SetIndex(0);
    t->SetOwner(OwnerEntity());
    TestParameter param(t, "x");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}, {intType}),
                  {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    ASSERT_TRUE(resolution->ExplicitlyGivenTypeArguments().has_value());
    ASSERT_EQ(resolution->ExplicitlyGivenTypeArguments()->size(), 1u);
    EXPECT_EQ((*resolution->ExplicitlyGivenTypeArguments())[0].get(), intType.get());
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_EQ(resolution->BestCandidateErrors(), OverloadResolutionErrors::None);
    // The given type is the resolved method type argument (the explicit arm of
    // `RunTypeInference` uses it as-is).
    ASSERT_EQ(resolution->InferredTypeArguments().size(), 1u);
    EXPECT_EQ(resolution->InferredTypeArguments()[0].get(), intType.get());
}

// An EMPTY group `TypeArguments` list ports to no explicitly given type arguments (the C#
// `ToArray()` on an empty list yields an empty array, which the ctor's `Length > 0` guard
// treats as null).
TEST(PerformOverloadResolutionTest, EmptyTypeArgumentsLeaveExplicitArgumentsUnset)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_FALSE(resolution->ExplicitlyGivenTypeArguments().has_value());
}

// ---- The input-flag threading ----

// `M(params int[])` called with two int arguments: the DEFAULT (allowExpandingParams true)
// resolves the EXPANDED form (`BestCandidateIsExpandedForm`, no errors).
TEST(PerformOverloadResolutionTest, ParamsCallResolvesExpandedFormByDefault)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(std::make_shared<ArrayType>(intType, 1), "p", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}),
                  {Arg(intType), Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_TRUE(resolution->BestCandidateIsExpandedForm());
    EXPECT_EQ(resolution->BestCandidateErrors(), OverloadResolutionErrors::None);
}

// The same call with `allowExpandingParams = false`: the expanded form is never built, so the
// normal form is inapplicable with TooManyPositionalArguments (the flag threads through to
// `AddCandidate`).
TEST(PerformOverloadResolutionTest, AllowExpandingParamsFalseRejectsParamsCall)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(std::make_shared<ArrayType>(intType, 1), "p", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}),
                  {Arg(intType), Arg(intType)},
                  std::nullopt, /*allowExpandingParams*/false);
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_FALSE(resolution->BestCandidateIsExpandedForm());
    EXPECT_NE(resolution->BestCandidateErrors() & OverloadResolutionErrors::TooManyPositionalArguments,
              OverloadResolutionErrors::None);
    EXPECT_FALSE(resolution->FoundApplicableCandidate());
}

// `M(int x, int y = 2)` called with ONE argument: the default (allowOptionalParameters true)
// resolves (the optional parameter stays at its default); `false` rejects with
// MissingArgumentForRequiredParameter.
TEST(PerformOverloadResolutionTest, OptionalParameterDependsOnAllowOptionalParameters)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter x(intType, "x");
    TestParameter y(intType, "y", /*isParams*/false, /*isOptional*/true);
    TestMethod method("M");
    method.SetParameters({&x, &y});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_EQ(resolution->BestCandidateErrors(), OverloadResolutionErrors::None);

    auto resolution2 = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)},
                  std::nullopt, /*allowExpandingParams*/true, /*allowOptionalParameters*/false);
    ASSERT_NE(resolution2, nullptr);
    EXPECT_EQ(resolution2->BestCandidate(), &method);
    EXPECT_NE(resolution2->BestCandidateErrors() &
              OverloadResolutionErrors::MissingArgumentForRequiredParameter,
              OverloadResolutionErrors::None);
    EXPECT_FALSE(resolution2->FoundApplicableCandidate());
}

// ---- The named-argument threading ----

// `M(int x, int y)` called as `(a, b: y-named...)` -- the arguments named ("y", "x") map to
// the parameters in that order: `GetArgumentToParameterMap()` yields {1, 0}.
TEST(PerformOverloadResolutionTest, ArgumentNamesMapToParameters)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter x(intType, "x");
    TestParameter y(intType, "y");
    TestMethod method("M");
    method.SetParameters({&x, &y});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}),
                  {Arg(intType), Arg(intType)},
                  std::optional<std::vector<std::string>>(std::vector<std::string>{"y", "x"}));
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    auto map = resolution->GetArgumentToParameterMap();
    ASSERT_TRUE(map.has_value());
    ASSERT_EQ(map->size(), 2u);
    EXPECT_EQ((*map)[0], 1);
    EXPECT_EQ((*map)[1], 0);
}

// ---- The public GetBestCandidateWithSubstitutedTypeArguments delegation ----

// A non-generic best candidate returns the member AS-IS (pointer identity); the method group
// surface composes with the public wrapper.
TEST(PerformOverloadResolutionTest, NonGenericBestReturnsMemberAsIs)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->GetBestCandidateWithSubstitutedTypeArguments(), &method);
}

// A GENERIC best candidate is re-specialized: `Specialize` is invoked on the member definition
// with the merged substitution and its result is returned (NOT the original member).
TEST(PerformOverloadResolutionTest, GenericBestIsRespecialized)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    auto t = std::make_shared<VisitableTypeParameter>("T");
    t->SetIndex(0);
    t->SetOwner(OwnerEntity());
    TestParameter param(t, "x");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param});
    TestMethod specialized("MSpecialized");
    specialized.SetTypeParameters({t.get()});
    specialized.SetParameters({&param});
    method.SetSpecialized(&specialized);
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_EQ(resolution->GetBestCandidateWithSubstitutedTypeArguments(), &specialized);
    EXPECT_EQ(method.SpecializeCallCount(), 1);
}

// An EMPTY group (no methods) yields no best candidate, and the public wrapper returns null
// without dereferencing.
TEST(PerformOverloadResolutionTest, EmptyGroupYieldsNoBestCandidate)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto resolution = Resolve(*Group({}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), nullptr);
    EXPECT_FALSE(resolution->FoundApplicableCandidate());
    EXPECT_EQ(resolution->GetBestCandidateWithSubstitutedTypeArguments(), nullptr);
    EXPECT_FALSE(resolution->GetArgumentToParameterMap().has_value());
}

// ---- The conversions threading ----

// The null-conversions shape: the lazy `CSharpConversions::Get` fallback resolves at the
// first engine call. The shape is deliberately NON-GENERIC (only the generic `Fix` path calls
// the cached `ImplicitConversion(IType, IType)` on the shared instance, so no test-local
// type ever lands in its cache -- the D540 dangling caveat).
TEST(PerformOverloadResolutionTest, NullConversionsUsesLazyGetFallback)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Group({MethodListWithDeclaringType(declaring, {&method})})
                  ->PerformOverloadResolution(Compilation(), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_TRUE(resolution->FoundApplicableCandidate());
}

// ---- The public LogCandidateAddingResult ----

// `LogCandidateAddingResult` is a state-preserving no-op while logging is disabled (the C#
// `#if DEBUG` body reads only the best-candidate state for the log suffixes).
TEST(PerformOverloadResolutionTest, LogCandidateAddingResultPreservesState)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaring = MakeTypeDef("C");
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto resolution = Resolve(*Group({MethodListWithDeclaringType(declaring, {&method})}), {Arg(intType)});
    ASSERT_NE(resolution, nullptr);
    resolution->LogCandidateAddingResult("  Test", method, OverloadResolutionErrors::None);
    EXPECT_EQ(resolution->BestCandidate(), &method);
    EXPECT_TRUE(resolution->FoundApplicableCandidate());
    EXPECT_EQ(resolution->BestCandidateErrors(), OverloadResolutionErrors::None);
}

} // namespace
