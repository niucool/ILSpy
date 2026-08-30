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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `OverloadResolution` Output Properties region (OverloadResolution.cs lines
// 1002-1085) beyond the first batch landed with `AddMethodLists`:
//  * `BestCandidateErrors` (line 1015) -- "the errors that apply to the best candidate.
//    This includes additional errors that do not affect applicability (e.g. AmbiguousMatch,
//    MethodConstraintsNotSatisfied)" -- ported as `Detail::BestCandidateErrors` (the lazy
//    `ValidateMethodConstraints` memoization) plus the public non-const
//    `OverloadResolution::BestCandidateErrors()` delegating to it;
//  * `BestCandidateIsExpandedForm` / `InferredTypeArguments` / `ArgumentConversions` /
//    `GetArgumentToParameterMap` -- the trivially state-derived getters on the public class.
//
// The load-bearing cruxes:
//  (a) the MEMOIZATION -- `wasValidated == false` runs a FRESH `ValidateMethodConstraints` and
//      overwrites a stale `validationResult` (a non-generic candidate's fresh validation yields
//      `None`, so a stale `MethodConstraintsNotSatisfied` must vanish from the returned mask);
//  (b) the MEMO REUSE -- `wasValidated == true` SKIPS the validation and returns the memoized
//      result verbatim (the stale `MethodConstraintsNotSatisfied` STAYS in the mask even though
//      a fresh validation would yield `None`);
//  (c) the NULL-BEST EARLY RETURN leaves the memo state untouched (the C# early return before
//      the memoization block -- load-bearing: a flag set on this path would wrongly skip the
//      validation of a later, actually-added best candidate);
//  (d) the AMBIGUOUS fold -- `bestCandidateAmbiguousWith != null` OR-s `AmbiguousMatch` into
//      the mask (an overall, not per-candidate, error);
//  (e) the public delegation -- the whole resolution pipeline folds the candidates, the public
//      getters observe the folded state: a params-expanded best (`BestCandidateIsExpandedForm`),
//      the explicit type arguments (`InferredTypeArguments`), the per-argument identity
//      conversion (`ArgumentConversions`), and the argument->parameter map with the -1 unmapped
//      sentinel (`GetArgumentToParameterMap`).
//
// The stubs mirror the AddMethodLists_Test / ValidateMethodConstraints_Test conventions: the
// `TestParameter`/`TestMethod` pair (the stub IS its own member definition), the
// `OwnedTypeParameter` (a `LookupTypeParameter` with a configurable `Owner` so the public
// constraint-validation path resolves conversions from the owner's compilation), and one
// SHARED int instance across the method parameter type and the argument (the real compilation's
// type-cache model, required for the identity applicability conversion).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
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
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::CSharp::Resolver::Detail::BestCandidateErrors;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

bool HasError(OverloadResolutionErrors mask, OverloadResolutionErrors error) {
    return (mask & error) != OverloadResolutionErrors::None;
}

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here needs a
// `FindType` registration; the conversions resolve over the stubs' own structural equality).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
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

// A named class definition (a method-list bucket's `DeclaringType`), the AddMethodLists_Test
// `MakeTypeDef` precedent (no base types needed here -- the unrelated-lists ambiguity shape).
std::shared_ptr<LookupTypeDefinition> MakeTypeDef(std::string name) {
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// A minimal `IParameter` over a configured type with configurable `IsParams` (the
// AddMethodLists_Test `TestParameter` precedent).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p", bool isParams = false)
        : name_(std::move(name)), type_(std::move(type)), isParams_(isParams) {}
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
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
};

// A `LookupMethod` with configurable `Parameters` (inherited) and method `TypeParameters` (the
// candidate ctor reads them from the member DEFINITION -- this stub IS its own definition); the
// RunTypeInference_Test `TestMethod` precedent.
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::string name = "M")
        : LookupMethod(std::move(name), Compilation()) {}
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }

    // --- IMethod ---
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }

private:
    std::vector<const ITypeParameter*> typeParameters_;
};

// A `LookupTypeParameter` with a configurable `Owner` -- the real-method-type-parameter shape
// (the public 3-arg `ValidateConstraints` overload resolves the conversions from the owner's
// compilation; a plain `LookupTypeParameter` returns a null `Owner`, the dummy shape whose
// documented fallback also yields `MethodConstraintsNotSatisfied`). The
// ValidateMethodConstraints_Test `OwnedTypeParameter` precedent, EXTENDED with the
// `AcceptVisitor -> visitor.VisitTypeParameter(*this)` bridge (the `VisitableTypeParameter`
// precedent): the public-pipeline tests below substitute the FORMAL PARAMETER TYPE `T` with
// the explicit type argument inside `RunTypeInference`, and the base `IType::AcceptVisitor`
// default routes to `VisitOtherType` so `TypeParameterSubstitution::VisitTypeParameter` would
// never fire for a plain `LookupTypeParameter` -- leaving the parameter type unsubstituted and
// failing the applicability conversion.
class OwnedTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    void SetOwner(const TS::IEntity* owner) { owner_ = owner; }
    const TS::IEntity* Owner() const override { return owner_; }
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
private:
    const TS::IEntity* owner_ = nullptr;
};

// The owning entity for the type parameters -- a real `IEntity` whose `Compilation()` is the
// static test compilation, so the public 3-arg overload's `CSharpConversions.Get` resolves.
const TS::IEntity* OwnerEntity() {
    static auto d = std::make_shared<LookupTypeDefinition>(
        "OwnerType", "",
        FullTypeName(TopLevelTypeName("", "OwnerType", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    return d.get();
}

// A plain `ResolveResult` over the given type (the base class is concrete).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// The memoization-state bundle threaded through `Detail::BestCandidateErrors` (the
// `OverloadResolution` instance fields `bestCandidate`/`bestCandidateWasValidated`/
// `bestCandidateValidationResult`/`bestCandidateAmbiguousWith`).
struct BestState {
    std::shared_ptr<OverloadResolutionCandidate> best;
    bool wasValidated = false;
    OverloadResolutionErrors validationResult = OverloadResolutionErrors::None;
    std::shared_ptr<OverloadResolutionCandidate> ambiguousWith;
};

// ---- Detail::BestCandidateErrors: the null-best early return ----

// A null best candidate yields `None` and leaves the memoization state UNTOUCHED -- the C#
// early return fires before the memoization block, so a `wasValidated` flag set here would
// wrongly skip the validation of a later, actually-added best candidate (the load-bearing
// untouched-flag crux).
TEST(OutputPropertiesTest, NullBestCandidateYieldsNoneAndLeavesMemoStateUntouched)
{
    BestState state;
    state.validationResult = OverloadResolutionErrors::MethodConstraintsNotSatisfied;
    EXPECT_EQ(BestCandidateErrors(state.best, state.wasValidated, state.validationResult,
                                  state.ambiguousWith),
              OverloadResolutionErrors::None);
    EXPECT_FALSE(state.wasValidated);
    EXPECT_EQ(state.validationResult, OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

// ---- Detail::BestCandidateErrors: the lazy-memoization crux pair ----

// An UNVALIDATED best candidate runs a FRESH `ValidateMethodConstraints` whose result
// OVERWRITES the stale memo: a non-generic candidate's fresh validation yields `None`, so a
// stale `MethodConstraintsNotSatisfied` must VANISH from the returned mask, and the flag ends
// up set with the fresh result memoized.
TEST(OutputPropertiesTest, UnvalidatedBestRunsFreshValidationOverwritingStaleResult)
{
    auto method = std::make_shared<TestMethod>("M");
    OverloadResolutionCandidate candidate(method.get(), false);
    BestState state;
    state.best = std::make_shared<OverloadResolutionCandidate>(method.get(), false);
    state.validationResult = OverloadResolutionErrors::MethodConstraintsNotSatisfied;

    EXPECT_EQ(BestCandidateErrors(state.best, state.wasValidated, state.validationResult,
                                  state.ambiguousWith),
              OverloadResolutionErrors::None);
    // The flag is now set and the stale result was overwritten by the fresh validation.
    EXPECT_TRUE(state.wasValidated);
    EXPECT_EQ(state.validationResult, OverloadResolutionErrors::None);
}

// A VALIDATED best candidate REUSES the memoized result and SKIPS the validation: the memoized
// `MethodConstraintsNotSatisfied` STAYS in the returned mask even though a fresh validation of
// this non-generic candidate would yield `None` (the mirror of the fresh-validation crux).
TEST(OutputPropertiesTest, ValidatedBestReusesMemoizedResult)
{
    auto method = std::make_shared<TestMethod>("M");
    BestState state;
    state.best = std::make_shared<OverloadResolutionCandidate>(method.get(), false);
    state.wasValidated = true;
    state.validationResult = OverloadResolutionErrors::MethodConstraintsNotSatisfied;

    EXPECT_EQ(BestCandidateErrors(state.best, state.wasValidated, state.validationResult,
                                  state.ambiguousWith),
              OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

// ---- Detail::BestCandidateErrors: the fresh-validation verdicts ----

// A generic candidate whose inferred type argument VIOLATES the class constraint reports
// `MethodConstraintsNotSatisfied` through the fresh validation (the real
// `ValidateMethodConstraints` path wired into the output property).
TEST(OutputPropertiesTest, FreshValidationReportsViolatedConstraint)
{
    auto method = std::make_shared<TestMethod>("M");
    auto tp = std::make_shared<OwnedTypeParameter>("T");
    tp->SetOwner(OwnerEntity());
    tp->SetHasReferenceTypeConstraint(true);
    method->SetTypeParameters({tp.get()});
    BestState state;
    state.best = std::make_shared<OverloadResolutionCandidate>(method.get(), false);
    // A value-type argument fails the `class` constraint.
    state.best->InferredTypes() = {MakeDef(KnownTypeCode::Int32)};

    EXPECT_EQ(BestCandidateErrors(state.best, state.wasValidated, state.validationResult,
                                  state.ambiguousWith),
              OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

// A candidate whose type inference already failed skips the constraint check entirely (the
// `TypeInferenceFailed` guard inside `ValidateMethodConstraints`): the returned mask carries
// ONLY the candidate's own errors, not `MethodConstraintsNotSatisfied`.
TEST(OutputPropertiesTest, TypeInferenceFailedSkipsConstraintCheck)
{
    auto method = std::make_shared<TestMethod>("M");
    auto tp = std::make_shared<OwnedTypeParameter>("T");
    tp->SetOwner(OwnerEntity());
    tp->SetHasReferenceTypeConstraint(true);
    method->SetTypeParameters({tp.get()});
    BestState state;
    state.best = std::make_shared<OverloadResolutionCandidate>(method.get(), false);
    state.best->InferredTypes() = {MakeDef(KnownTypeCode::Int32)};
    state.best->AddError(OverloadResolutionErrors::TypeInferenceFailed);

    OverloadResolutionErrors result = BestCandidateErrors(
        state.best, state.wasValidated, state.validationResult, state.ambiguousWith);
    EXPECT_TRUE(HasError(result, OverloadResolutionErrors::TypeInferenceFailed));
    EXPECT_FALSE(HasError(result, OverloadResolutionErrors::MethodConstraintsNotSatisfied));
}

// ---- Detail::BestCandidateErrors: the mask composition ----

// The best candidate's accumulated applicability errors are OR-ed into the mask alongside the
// validation result.
TEST(OutputPropertiesTest, CandidateErrorsAreORedWithValidationResult)
{
    auto method = std::make_shared<TestMethod>("M");
    BestState state;
    state.best = std::make_shared<OverloadResolutionCandidate>(method.get(), false);
    state.best->AddError(OverloadResolutionErrors::ArgumentTypeMismatch);
    state.wasValidated = true;
    state.validationResult = OverloadResolutionErrors::None;

    OverloadResolutionErrors result = BestCandidateErrors(
        state.best, state.wasValidated, state.validationResult, state.ambiguousWith);
    EXPECT_TRUE(HasError(result, OverloadResolutionErrors::ArgumentTypeMismatch));
}

// A non-null `bestCandidateAmbiguousWith` OR-s `AmbiguousMatch` into the mask -- the overall
// (not per-candidate) error "that does not matter for applicability" surfacing in the output
// property.
TEST(OutputPropertiesTest, AmbiguousWithORsAmbiguousMatch)
{
    auto method = std::make_shared<TestMethod>("M");
    BestState state;
    state.best = std::make_shared<OverloadResolutionCandidate>(method.get(), false);
    state.ambiguousWith = std::make_shared<OverloadResolutionCandidate>(method.get(), false);

    OverloadResolutionErrors result = BestCandidateErrors(
        state.best, state.wasValidated, state.validationResult, state.ambiguousWith);
    EXPECT_TRUE(HasError(result, OverloadResolutionErrors::AmbiguousMatch));
}

// ---- The public delegation: BestCandidateErrors ----

// An empty resolution (no candidate added) yields `None`.
TEST(OutputPropertiesTest, PublicBestCandidateErrorsEmptyResolutionYieldsNone)
{
    OverloadResolution resolution(Compilation(), {});
    EXPECT_EQ(resolution.BestCandidateErrors(), OverloadResolutionErrors::None);
}

// The full public pipeline: a generic `M<T>(T x)` with an explicit type argument that VIOLATES
// the `class` constraint resolves APPLICABLY (the constructed-type constraint check fires only
// for parameterized formal-parameter types, and `T` is a bare type parameter; the int argument
// identity-converts to the substituted parameter type), and `BestCandidateErrors` surfaces the
// lazily-validated `MethodConstraintsNotSatisfied` -- the additional error that does not
// affect applicability.
TEST(OutputPropertiesTest, PublicBestCandidateErrorsReportsViolatedConstraint)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto tp = std::make_shared<OwnedTypeParameter>("T");
    tp->SetOwner(OwnerEntity());
    tp->SetHasReferenceTypeConstraint(true);
    TestParameter param(tp, "x");
    TestMethod method("M");
    method.SetTypeParameters({tp.get()});
    method.SetParameters({&param});

    OverloadResolution resolution(
        Compilation(), {Arg(intType)}, std::nullopt,
        std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{intType}});
    resolution.AddCandidate(method);
    // The candidate IS applicable -- the constraint violation does not affect applicability.
    EXPECT_TRUE(resolution.FoundApplicableCandidate());
    OverloadResolutionErrors result = resolution.BestCandidateErrors();
    EXPECT_TRUE(HasError(result, OverloadResolutionErrors::MethodConstraintsNotSatisfied));
    // The lazily-validated result: the CANDIDATE's own error mask is clean.
    EXPECT_FALSE(HasError(result, OverloadResolutionErrors::ArgumentTypeMismatch));
}

// The satisfied-constraint twin: an UNCONSTRAINED generic method with a matching explicit type
// argument resolves with NO errors at all (the fresh validation yields `None`).
TEST(OutputPropertiesTest, PublicBestCandidateErrorsSatisfiedConstraintYieldsNone)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto tp = std::make_shared<OwnedTypeParameter>("T");
    tp->SetOwner(OwnerEntity());
    TestParameter param(tp, "x");
    TestMethod method("M");
    method.SetTypeParameters({tp.get()});
    method.SetParameters({&param});

    OverloadResolution resolution(
        Compilation(), {Arg(intType)}, std::nullopt,
        std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{intType}});
    resolution.AddCandidate(method);
    EXPECT_EQ(resolution.BestCandidateErrors(), OverloadResolutionErrors::None);
}

// Two unrelated declaring types with IDENTICAL signatures tie 0/0 in `BetterFunctionMember`, so
// the resolution ends AMBIGUOUS -- `BestCandidateErrors` OR-s `AmbiguousMatch` into the
// otherwise-clean mask.
TEST(OutputPropertiesTest, PublicBestCandidateErrorsAmbiguousMatch)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto left = MakeTypeDef("Left");
    auto right = MakeTypeDef("Right");
    TestParameter param(intType, "x");
    TestMethod leftMethod("M");
    leftMethod.SetParameters({&param});
    TestMethod rightMethod("M");
    rightMethod.SetParameters({&param});
    MethodListWithDeclaringType leftBucket(left, {&leftMethod});
    MethodListWithDeclaringType rightBucket(right, {&rightMethod});

    OverloadResolution resolution(Compilation(), {Arg(intType)});
    resolution.AddMethodLists({leftBucket, rightBucket});
    EXPECT_TRUE(resolution.IsAmbiguous());
    OverloadResolutionErrors result = resolution.BestCandidateErrors();
    EXPECT_TRUE(HasError(result, OverloadResolutionErrors::AmbiguousMatch));
}

// ---- The public delegation: the trivially state-derived getters ----

// `M(params int[])` called with two int arguments: the EXPANDED form wins (its error count is
// strictly lower), so `BestCandidateIsExpandedForm()` is true.
TEST(OutputPropertiesTest, PublicBestCandidateIsExpandedForm)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "p", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});

    OverloadResolution resolution(Compilation(), {Arg(intType), Arg(intType)});
    resolution.AddCandidate(method);
    EXPECT_TRUE(resolution.BestCandidateIsExpandedForm());
}

// The non-params shape: only the normal form exists, so `BestCandidateIsExpandedForm()` is
// false; an empty resolution is false as well (the null-best default).
TEST(OutputPropertiesTest, PublicBestCandidateIsExpandedFormFalseDirections)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});

    OverloadResolution normalForm(Compilation(), {Arg(intType)});
    normalForm.AddCandidate(method);
    EXPECT_FALSE(normalForm.BestCandidateIsExpandedForm());

    OverloadResolution empty(Compilation(), {});
    EXPECT_FALSE(empty.BestCandidateIsExpandedForm());
}

// A generic method with MATCHING explicit type arguments: `InferredTypeArguments()` returns the
// given type arguments (the `RunTypeInference` explicit-args arm stores them as the candidate's
// `InferredTypes`).
TEST(OutputPropertiesTest, PublicInferredTypeArgumentsReturnsExplicitTypeArguments)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto tp = std::make_shared<OwnedTypeParameter>("T");
    tp->SetOwner(OwnerEntity());
    TestParameter param(tp, "x");
    TestMethod method("M");
    method.SetTypeParameters({tp.get()});
    method.SetParameters({&param});

    OverloadResolution resolution(
        Compilation(), {Arg(intType)}, std::nullopt,
        std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{intType}});
    resolution.AddCandidate(method);
    const std::vector<ITypePtr>& inferred = resolution.InferredTypeArguments();
    ASSERT_EQ(inferred.size(), 1u);
    EXPECT_EQ(inferred[0].get(), intType.get());
}

// A NON-GENERIC method never populates `InferredTypes` (the C# array stays null), so
// `InferredTypeArguments()` is the empty list; an empty resolution is empty too.
TEST(OutputPropertiesTest, PublicInferredTypeArgumentsEmptyDirections)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});

    OverloadResolution nonGeneric(Compilation(), {Arg(intType)});
    nonGeneric.AddCandidate(method);
    EXPECT_TRUE(nonGeneric.InferredTypeArguments().empty());

    OverloadResolution empty(Compilation(), {});
    EXPECT_TRUE(empty.InferredTypeArguments().empty());
}

// A resolved best candidate exposes its per-argument conversions: the int argument
// identity-converts to the int parameter, so `ArgumentConversions()[0]` is the
// `IdentityConversion` singleton.
TEST(OutputPropertiesTest, PublicArgumentConversionsReturnsCandidateConversions)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});

    OverloadResolution resolution(Compilation(), {Arg(intType)});
    resolution.AddCandidate(method);
    auto conversions = resolution.ArgumentConversions();
    ASSERT_EQ(conversions.size(), 1u);
    ASSERT_NE(conversions[0], nullptr);
    EXPECT_EQ(conversions[0].get(), Conversions::IdentityConversion().get());
}

// With NO best candidate, `ArgumentConversions()` falls back to the `None` singleton repeated
// once per argument (the C# `Enumerable.Repeat(Conversion.None, arguments.Length).ToList()`).
TEST(OutputPropertiesTest, PublicArgumentConversionsEmptyResolutionRepeatsNone)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    OverloadResolution resolution(Compilation(), {Arg(intType), Arg(intType)});
    auto conversions = resolution.ArgumentConversions();
    ASSERT_EQ(conversions.size(), 2u);
    ASSERT_NE(conversions[0], nullptr);
    ASSERT_NE(conversions[1], nullptr);
    EXPECT_EQ(conversions[0].get(), Conversions::None().get());
    EXPECT_EQ(conversions[1].get(), Conversions::None().get());
}

// A resolved best candidate exposes its argument->parameter map: the single argument maps to
// parameter 0.
TEST(OutputPropertiesTest, PublicGetArgumentToParameterMap)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});

    OverloadResolution resolution(Compilation(), {Arg(intType)});
    resolution.AddCandidate(method);
    std::optional<std::vector<int>> map = resolution.GetArgumentToParameterMap();
    ASSERT_TRUE(map.has_value());
    ASSERT_EQ(map->size(), 1u);
    EXPECT_EQ((*map)[0], 0);
}

// A TOO-MANY-ARGUMENTS resolution maps the overflowing argument to the -1 unmapped sentinel;
// an empty resolution yields `nullopt` (the C# `null`).
TEST(OutputPropertiesTest, PublicGetArgumentToParameterMapUnmappedSentinel)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});

    OverloadResolution overflow(Compilation(), {Arg(intType), Arg(intType)});
    overflow.AddCandidate(method);
    std::optional<std::vector<int>> map = overflow.GetArgumentToParameterMap();
    ASSERT_TRUE(map.has_value());
    ASSERT_EQ(map->size(), 2u);
    EXPECT_EQ((*map)[0], 0);
    EXPECT_EQ((*map)[1], -1);

    OverloadResolution empty(Compilation(), {});
    EXPECT_FALSE(empty.GetArgumentToParameterMap().has_value());
}

} // namespace
