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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution`'s `CalculateCandidate` engine step (OverloadResolution.cs
// line 278) -- the composition that wires the whole ported engine pipeline together in the
// C# order: `ResolveParameterTypes` (the expanded-form unpack abort), `MapCorresponding
// Parameters`, `RunTypeInference`, `CheckApplicability` (the argument-count half + the
// passing-mode/conversion half), and `ConsiderIfNewCandidateIsBest` (the best-candidate
// folding). Ported as `Detail::CalculateCandidate` (the instance-state-threading lift:
// `compilation`/`conversions`/`arguments`/`argumentNames`/`explicitlyGivenTypeArguments`/
// the three input flags/the best-candidate state are parameters, the D536/D550/D573
// state-threading convention).
//
// The load-bearing cruxes:
//  (a) the ORDER -- a fresh candidate arrives with sized-but-null `ParameterTypes` (the
//      ctor leaves them unfilled), so `ResolveParameterTypes` must run FIRST; every
//      step's observable effect flows through the composition (the argument-to-parameter
//      map, the inferred types + the substituted parameter types, the applicability
//      errors, the best-candidate state);
//  (b) the EXPANDED-FORM ABORT -- an expanded candidate whose params-collection type is
//      not unpackable (not an array/Span/array-interface) makes `CalculateCandidate`
//      return FALSE (the candidate is removed WITHOUT reporting an error) and NOTHING
//      else runs (the best-candidate state is untouched);
//  (c) the best-candidate folding runs REGARDLESS of applicability -- the C# folds every
//      calculated candidate (an inapplicable candidate still becomes the best; the later
//      `BestCandidateErrors` reports the errors), and the second candidate's
//      `BetterFunctionMember` verdict against the existing best drives the three arms
//      (ambiguous-with / promote / stays-best);
//  (d) the input-flag threading -- `allowOptionalParameters` (the optional-parameter
//      unmapped flag vs. the missing-argument error) and `isExtensionMethodInvocation`
//      (the first-parameter identity/reference/boxing/span restriction rejects a numeric
//      widening) both flow through the composition.
//
// The stubs mirror the RunTypeInference_Test conventions (`TestMethod`/`TestParameter`,
// the `VisitableTypeParameter` AcceptVisitor bridge, the `MakeDef` primitive definitions,
// the shared-instance type cache modeling the real compilation -- `LookupTypeDefinition::
// StructuralEquals` is identity equality, so the ambiguous/promotion/stays-best cruxes
// share ONE `int` instance across both methods' parameter types and the argument).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
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

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::CalculateCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here
// needs a `FindType` registration; the conversions resolve over the stubs' own
// structural equality).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeParameter` overriding `AcceptVisitor` to dispatch to
// `visitor.VisitTypeParameter` (the C# `AbstractTypeParameter.AcceptVisitor` bridge) --
// the plain `LookupTypeParameter` routes to `VisitOtherType`, so the type-parameter
// substitution would never fire for it (the D564 `VisitableTypeParameter` precedent).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A primitive definition (`LookupTypeDefinition`, IS-A `ITypeDefinition`) with the given
// `KnownTypeCode` / `TypeKind` -- the D514 `MakeDef` precedent (distinct names so distinct
// instances never compare equal; `GetTypeCode` resolves through the KnownTypeCode).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A minimal `IParameter` over a configured type with configurable `IsParams`/`IsOptional`
// (the D536/D550 `TestParameter` precedent, extended for the params-array unpack and the
// optional-parameter unmapped flags).
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
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
    bool isOptional_;
};

// A `LookupMethod` with configurable `Parameters` (inherited) and method `TypeParameters`
// (the candidate ctor reads them from the member DEFINITION -- this stub IS its own
// definition, the inherited default). The D573 `TestMethod` precedent.
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::string name = "M")
        : LookupMethod(std::move(name), Compilation()) {}
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
private:
    std::vector<const ITypeParameter*> typeParameters_;
};

// A plain `ResolveResult` over the given type (the base class is concrete; the inference's
// `PhaseOne` reads only `Type()` for a non-lambda/non-method-group argument).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

bool HasError(const OverloadResolutionCandidate& c, OverloadResolutionErrors error) {
    return (c.Errors() & error) != OverloadResolutionErrors::None;
}

// The best-candidate state threaded through the pipeline calls (the `OverloadResolution`
// instance fields `bestCandidate`/`bestCandidateWasValidated`/`bestCandidateAmbiguousWith`).
struct BestState {
    std::shared_ptr<OverloadResolutionCandidate> best;
    bool wasValidated = false;
    std::shared_ptr<OverloadResolutionCandidate> ambiguousWith;
};

// Runs `CalculateCandidate` with the C# `OverloadResolution` input-property defaults
// (`AllowOptionalParameters`/`AllowImplicitIn` true, `IsExtensionMethodInvocation` false)
// and the all-positional argument names (the ctor's `null` normalization), over the given
// best state.
bool RunPipeline(const std::shared_ptr<OverloadResolutionCandidate>& candidate,
         const std::vector<std::shared_ptr<ResolveResult>>& arguments,
         BestState& state,
         const std::vector<std::string>& argumentNames = {},
         bool isExtensionMethodInvocation = false,
         bool allowOptionalParameters = true) {
    CSharpConversions conversions(Compilation());
    std::vector<std::string> names = argumentNames;
    names.resize(arguments.size());
    return CalculateCandidate(candidate, Compilation(), conversions, arguments, names,
                              std::nullopt, allowOptionalParameters, /*allowImplicitIn*/true,
                              isExtensionMethodInvocation,
                              state.best, state.wasValidated, state.ambiguousWith);
}

// ---- The simple applicable pipeline (every step's observable effect) ----

// `M(int)` called with an int argument: the pipeline fills the parameter types from the
// member's formal parameters (the ctor leaves them sized-but-null), maps the argument,
// checks applicability (the identity conversion), and folds the candidate into the empty
// best state (the first candidate becomes best).
TEST(CalculateCandidateTest, SimpleApplicableCandidateBecomesBest)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_TRUE(RunPipeline(c, arguments, state));
    EXPECT_EQ(c->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(c->ErrorCount(), 0);
    // ResolveParameterTypes filled the parameter type from the formal parameter.
    ASSERT_EQ(c->ParameterTypes().size(), 1u);
    ASSERT_NE(c->ParameterTypes()[0], nullptr);
    EXPECT_EQ(c->ParameterTypes()[0].get(), intType.get());
    // MapCorrespondingParameters mapped the argument to the parameter.
    ASSERT_EQ(c->ArgumentToParameterMap().size(), 1u);
    EXPECT_EQ(c->ArgumentToParameterMap()[0], 0);
    // CheckApplicability computed the argument conversion (identity for int -> int).
    ASSERT_EQ(c->ArgumentConversions().size(), 1u);
    EXPECT_EQ(c->ArgumentConversions()[0].get(), Conversions::IdentityConversion().get());
    // ConsiderIfNewCandidateIsBest folded the candidate into the empty best state.
    EXPECT_EQ(state.best.get(), c.get());
    EXPECT_FALSE(state.wasValidated);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// A trailing named argument maps by NAME through the pipeline: `M(int a, string b)`
// called with `(5, b: "...")` maps the second argument to the second parameter.
TEST(CalculateCandidateTest, NamedArgumentMapsToMatchingParameter)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestParameter param0(intType, "a");
    TestParameter param1(strType, "b");
    TestMethod method("M");
    method.SetParameters({&param0, &param1});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(strType)};
    EXPECT_TRUE(RunPipeline(c, arguments, state, {"", "b"}));
    EXPECT_EQ(c->Errors(), OverloadResolutionErrors::None);
    ASSERT_EQ(c->ArgumentToParameterMap().size(), 2u);
    EXPECT_EQ(c->ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c->ArgumentToParameterMap()[1], 1);
    EXPECT_EQ(state.best.get(), c.get());
}

// ---- The expanded-form arms ----

// An expanded candidate whose params-collection type is NOT unpackable (a plain class,
// not an array/Span/array-interface) returns FALSE -- the candidate is removed WITHOUT
// reporting an error -- and NOTHING else runs (the best-candidate state is untouched).
TEST(CalculateCandidateTest, ExpandedFormWithNonUnpackableLastParamReturnsFalse)
{
    auto nonCollection = MakeDef(KnownTypeCode::None, TypeKind::Class);
    TestParameter param(nonCollection, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, /*isExpanded*/true);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(nonCollection), Arg(nonCollection)};
    EXPECT_FALSE(RunPipeline(c, arguments, state));
    // The early-return abort skips every step -- no errors, no folding.
    EXPECT_EQ(c->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(c->ErrorCount(), 0);
    EXPECT_EQ(state.best, nullptr);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// An expanded candidate over `M(params int[])` called with two int arguments: the
// params-array is unpacked to its element type, every argument maps to the params
// parameter, and any count is fine for it (no errors).
TEST(CalculateCandidateTest, ExpandedFormUnpacksParamsArrayAndMapsArguments)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, /*isExpanded*/true);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};
    EXPECT_TRUE(RunPipeline(c, arguments, state));
    EXPECT_EQ(c->Errors(), OverloadResolutionErrors::None);
    // The params-array was unpacked to the ELEMENT type.
    ASSERT_EQ(c->ParameterTypes().size(), 1u);
    ASSERT_NE(c->ParameterTypes()[0], nullptr);
    EXPECT_EQ(c->ParameterTypes()[0].get(), intType.get());
    // Both arguments mapped to the params parameter.
    ASSERT_EQ(c->ArgumentToParameterMap().size(), 2u);
    EXPECT_EQ(c->ArgumentToParameterMap()[0], 0);
    EXPECT_EQ(c->ArgumentToParameterMap()[1], 0);
    EXPECT_EQ(c->ArgumentsPassedToParams(), 2);
    EXPECT_EQ(state.best.get(), c.get());
}

// ---- The applicability errors through the pipeline ----

// `M(int)` called with TWO arguments: TooManyPositionalArguments (the second positional
// argument has no corresponding parameter).
TEST(CalculateCandidateTest, TooManyArgumentsAddsError)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};
    EXPECT_TRUE(RunPipeline(c, arguments, state));
    EXPECT_TRUE(HasError(*c, OverloadResolutionErrors::TooManyPositionalArguments));
    EXPECT_EQ(c->ErrorCount(), 1);
}

// `M(int)` called with NO arguments: MissingArgumentForRequiredParameter -- and the
// INAPPLICABLE candidate still becomes the best (the folding runs regardless of
// applicability; the later BestCandidateErrors reports the errors).
TEST(CalculateCandidateTest, MissingArgumentForRequiredParameterStillBecomesBest)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state;
    EXPECT_TRUE(RunPipeline(c, {}, state));
    EXPECT_TRUE(HasError(*c, OverloadResolutionErrors::MissingArgumentForRequiredParameter));
    EXPECT_EQ(c->ErrorCount(), 1);
    EXPECT_EQ(state.best.get(), c.get());
}

// `M(int optional)` called with no arguments: with `allowOptionalParameters` the unmapped
// optional parameter sets HasUnmappedOptionalParameters (no error); with the flag off it
// is MissingArgumentForRequiredParameter. The flag threads through the composition.
TEST(CalculateCandidateTest, OptionalParameterUnmappedDependsOnAllowOptional)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x", /*isParams*/false, /*isOptional*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    auto c1 = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state1;
    EXPECT_TRUE(RunPipeline(c1, {}, state1, {}, /*isExtension*/false, /*allowOptional*/true));
    EXPECT_EQ(c1->Errors(), OverloadResolutionErrors::None);
    EXPECT_TRUE(c1->HasUnmappedOptionalParameters());

    auto c2 = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state2;
    EXPECT_TRUE(RunPipeline(c2, {}, state2, {}, /*isExtension*/false, /*allowOptional*/false));
    EXPECT_TRUE(HasError(*c2, OverloadResolutionErrors::MissingArgumentForRequiredParameter));
    EXPECT_FALSE(c2->HasUnmappedOptionalParameters());
}

// `M(int)` called with a string argument: ArgumentTypeMismatch (the conversion check
// runs through the pipeline).
TEST(CalculateCandidateTest, ArgumentTypeMismatchThroughPipeline)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(strType)};
    EXPECT_TRUE(RunPipeline(c, arguments, state));
    EXPECT_TRUE(HasError(*c, OverloadResolutionErrors::ArgumentTypeMismatch));
    EXPECT_EQ(c->ErrorCount(), 1);
}

// `M(long)` called with an int argument under `IsExtensionMethodInvocation`: the int ->
// long numeric WIDENING is a valid conversion but NOT one of the four allowed for an
// extension method's first parameter (identity/reference/boxing/span), so
// ArgumentTypeMismatch; with the flag off the same conversion is accepted.
TEST(CalculateCandidateTest, ExtensionMethodFirstParamNumericConversionRejected)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    TestParameter param(longType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    auto c1 = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state1;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_TRUE(RunPipeline(c1, arguments, state1, {}, /*isExtension*/true));
    EXPECT_TRUE(HasError(*c1, OverloadResolutionErrors::ArgumentTypeMismatch));

    auto c2 = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state2;
    EXPECT_TRUE(RunPipeline(c2, arguments, state2, {}, /*isExtension*/false));
    EXPECT_EQ(c2->Errors(), OverloadResolutionErrors::None);
}

// ---- The type-inference step through the pipeline ----

// `M<T>(T x)` called with an int argument: the pipeline infers `T = int` and substitutes
// it into the formal parameter type.
TEST(CalculateCandidateTest, TypeInferenceThroughPipeline)
{
    auto t = std::make_shared<VisitableTypeParameter>("T");
    t->SetIndex(0);
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(t, "x");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param});
    auto c = std::make_shared<OverloadResolutionCandidate>(&method, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_TRUE(RunPipeline(c, arguments, state));
    EXPECT_EQ(c->Errors(), OverloadResolutionErrors::None);
    ASSERT_EQ(c->InferredTypes().size(), 1u);
    ASSERT_NE(c->InferredTypes()[0], nullptr);
    EXPECT_EQ(c->InferredTypes()[0].get(), intType.get());
    ASSERT_EQ(c->ParameterTypes().size(), 1u);
    ASSERT_NE(c->ParameterTypes()[0], nullptr);
    EXPECT_EQ(c->ParameterTypes()[0].get(), intType.get());
    EXPECT_EQ(state.best.get(), c.get());
}

// ---- The best-candidate folding through the pipeline ----

// Two IDENTICAL applicable candidates (`M1(int)` / `M2(int)` sharing the int instance,
// modeling the real compilation's type cache): the first becomes best; the second is
// neither better nor worse, so it becomes bestCandidateAmbiguousWith (the ambiguous arm).
TEST(CalculateCandidateTest, SecondIdenticalCandidateIsAmbiguousWithBest)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param1(intType, "x");
    TestMethod method1("M1");
    method1.SetParameters({&param1});
    TestParameter param2(intType, "x");
    TestMethod method2("M2");
    method2.SetParameters({&param2});
    auto c1 = std::make_shared<OverloadResolutionCandidate>(&method1, false);
    auto c2 = std::make_shared<OverloadResolutionCandidate>(&method2, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_TRUE(RunPipeline(c1, arguments, state));
    EXPECT_EQ(state.best.get(), c1.get());
    EXPECT_TRUE(RunPipeline(c2, arguments, state));
    // The best is unchanged; the new candidate is the ambiguous-with.
    EXPECT_EQ(state.best.get(), c1.get());
    EXPECT_EQ(state.ambiguousWith.get(), c2.get());
}

// An APPLICABLE second candidate promotes over an inapplicable first: `M1(string)` with
// an int argument (ArgumentTypeMismatch) then `M2(int)` (applicable) -- the
// prefer-applicable heuristic in BetterFunctionMember makes the new candidate the best.
TEST(CalculateCandidateTest, ApplicableSecondCandidatePromotesOverInapplicableFirst)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestParameter param1(strType, "x");
    TestMethod method1("M1");
    method1.SetParameters({&param1});
    TestParameter param2(intType, "x");
    TestMethod method2("M2");
    method2.SetParameters({&param2});
    auto c1 = std::make_shared<OverloadResolutionCandidate>(&method1, false);
    auto c2 = std::make_shared<OverloadResolutionCandidate>(&method2, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_TRUE(RunPipeline(c1, arguments, state));
    EXPECT_EQ(state.best.get(), c1.get());
    EXPECT_TRUE(RunPipeline(c2, arguments, state));
    EXPECT_EQ(state.best.get(), c2.get());
    EXPECT_EQ(state.ambiguousWith, nullptr);
    EXPECT_FALSE(state.wasValidated);
}

// An INAPPLICABLE second candidate does not displace the applicable best: `M1(int)` (ok)
// then `M2(string)` with an int argument (ArgumentTypeMismatch) -- the existing best stays
// (the stays-best arm; no ambiguous-with is recorded).
TEST(CalculateCandidateTest, InapplicableSecondCandidateDoesNotDisplaceBest)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestParameter param1(intType, "x");
    TestMethod method1("M1");
    method1.SetParameters({&param1});
    TestParameter param2(strType, "x");
    TestMethod method2("M2");
    method2.SetParameters({&param2});
    auto c1 = std::make_shared<OverloadResolutionCandidate>(&method1, false);
    auto c2 = std::make_shared<OverloadResolutionCandidate>(&method2, false);
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_TRUE(RunPipeline(c1, arguments, state));
    EXPECT_EQ(state.best.get(), c1.get());
    EXPECT_TRUE(RunPipeline(c2, arguments, state));
    EXPECT_EQ(state.best.get(), c1.get());
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

} // namespace
