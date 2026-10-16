// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution.ConsiderIfNewCandidateIsBest` (OverloadResolution.cs line 978) ported
// as a `Detail::` free function taking the `OverloadResolution` instance state by reference (the D536
// `CheckApplicabilityPassingModeAndConversions` precedent -- `bestCandidate`/`bestCandidateWasValidated`/
// `bestCandidateAmbiguousWith` are the instance fields, threaded as parameters since the free function
// has no instance state). The decision:
//   * `bestCandidate == null` -> the candidate becomes the new best, `bestCandidateWasValidated` resets
//     to false; `bestCandidateAmbiguousWith` is left untouched.
//   * `BetterFunctionMember(candidate, bestCandidate)`:
//       0 (neither better) -> overwrite `bestCandidateAmbiguousWith` with the candidate (the best stays);
//       1 (the new candidate is better) -> promote it to best, reset `bestCandidateWasValidated`, clear
//         `bestCandidateAmbiguousWith`;
//       2 (the existing best stays best) -> change nothing.
//
// The `BetterFunctionMember` return value is driven deterministically: case 1/2 via the "prefer
// applicable members" heuristic (`ErrorCount == 0` vs `> 0`, which short-circuits before the
// better-conversion loop), and case 0 via two identical applicable candidates with an
// identity-matching argument (the tie-break block reduces to 0 when everything is equal).
//
// The `TestParameter` keep-alives are held in the test scope (via the `MakeApplicableCandidate`
// out-params) -- the candidate's method stores a NON-OWNING `const IParameter*`, and case 0 reaches
// `MoreSpecificFormalParameters` which dereferences `Parameters()`, so the parameter must outlive the
// candidate (the D510/D536/D549 `TestMethod`/`TestParameter` lifetime convention).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ConsiderIfNewCandidateIsBest;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A minimal by-value `IParameter` over a configured type (the D536/D549 `TestParameter` precedent,
// pruned to the fields `OverloadResolutionCandidate` reads -- `Type()` for `ParamsCollectionType`).
class TestParameter : public IParameter {
public:
	explicit TestParameter(ITypePtr type, std::string name = "p")
		: name_(std::move(name)), type_(std::move(type)) {}
	::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
	{ return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
	std::string Name() const override { return name_; }
	const IType& Type() const override { return *type_; }
	bool IsConst() const override { return false; }
	std::any GetConstantValue(bool) const override { return std::any{}; }
	std::vector<const IAttribute*> GetAttributes() const override { return {}; }
	::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
	{ return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
	bool IsParams() const override { return false; }
	bool IsOptional() const override { return false; }
	bool HasConstantValueInSignature() const override { return false; }
	const IParameterizedMember* Owner() const override { return nullptr; }
	LifetimeAnnotation Lifetime() const override { return {}; }
private:
	std::string name_;
	ITypePtr type_;
};

// A `LookupMethod` whose `Parameters()` and `MemberDefinition()` return the configured list and
// `this` (so the `OverloadResolutionCandidate` ctor reads `MemberDefinition()->Parameters()` to
// populate the candidate's `Parameters()`, the D510/D536/D549 `TestMethod` precedent).
class TestMethod : public LookupMethod {
public:
	explicit TestMethod(std::vector<const IParameter*> params, std::string name = "M")
		: LookupMethod(std::move(name), Compilation()), params_(std::move(params)) {}
	std::vector<const IParameter*> Parameters() const override { return params_; }
	const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
private:
	std::vector<const IParameter*> params_;
};

// A primitive element definition (`Int32`) -- a struct with the `KnownTypeCode` so `GetTypeCode`
// resolves and `IdentityConversion`/`BetterConversion` resolve (the D514/D549 `Prim`/`MakeDef`
// precedent).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc) {
	std::string name = "T" + std::to_string(static_cast<int>(ktc));
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
}

// Function-local static singleton so the SAME `IType` instance is shared across a candidate's
// `ParameterTypes`, the other candidate's `ParameterTypes`, and the argument's type --
// `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so two distinct
// `Def(Int32)` instances are NOT identity-convertible (which would falsely set `parameterTypesEqual =
// false` and skip the tie-break block, making `BetterFunctionMember` return 0 for the wrong reason).
// A real compilation caches the `int` type as a single instance; the static faithfully models that
// (the D547/D549 precedent).
ITypePtr Int() { static auto t = MakeDef(KnownTypeCode::Int32); return t; }

// A by-value `ResolveResult` over the supplied type (the D536/D549 `Arg` precedent; a plain
// `ResolveResult` -- `IsCompileTimeConstant` is false, so `BetterConversion` uses the IType-based
// identity / `BetterConversionTarget` arms).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
	return std::make_shared<ResolveResult>(std::move(type));
}

std::shared_ptr<TestParameter> P(ITypePtr type = Int()) {
	return std::make_shared<TestParameter>(std::move(type));
}

// Builds a non-generic, non-lifted `TestMethod` from the supplied parameter list. The
// `shared_ptr<TestParameter>` keep-alives must outlive the returned method (the method holds
// non-owning `const IParameter*`); the test scopes hold them.
std::shared_ptr<TestMethod> Method(std::vector<const IParameter*> params) {
	return std::make_shared<TestMethod>(std::move(params));
}

// Builds a candidate with a single by-value `int` parameter, `ParameterTypes`/`ArgumentToParameterMap`
// filled for a single identity-matching `int` argument, and a configurable `ErrorCount`. With
// `ErrorCount == 0` this is an applicable candidate; the identity-matching argument makes
// `BetterFunctionMember` reduce to 0 against an identical sibling (the ambiguous case). The
// `paramOut`/`methodOut` shared handles are written back to the caller's scope so the parameter and
// method outlive the returned candidate (the candidate's method stores a non-owning
// `const IParameter*`, and case 0 reaches `MoreSpecificFormalParameters` which dereferences
// `Parameters()` -- the D549 lifetime convention).
std::shared_ptr<OverloadResolutionCandidate> MakeApplicableCandidate(
	std::shared_ptr<TestParameter>& paramOut,
	std::shared_ptr<TestMethod>& methodOut,
	int errorCount) {
	paramOut = P(Int());
	methodOut = Method({paramOut.get()});
	auto c = std::make_shared<OverloadResolutionCandidate>(methodOut.get(), /*isExpanded=*/false);
	c->ParameterTypes() = {Int()};
	c->ArgumentToParameterMap() = {0};
	c->ErrorCount() = errorCount;
	return c;
}

// The shared arguments vector + conversions used by every test (a single identity-matching `int`
// argument); case 1/2 short-circuit on `ErrorCount` before touching arguments, and case 0 needs the
// identity-matching argument so the better-conversion loop sets neither `c1IsBetter`/`c2IsBetter`.
const std::vector<std::shared_ptr<ResolveResult>>& Args() {
	static auto a = std::vector<std::shared_ptr<ResolveResult>>{Arg(Int())};
	return a;
}
CSharpConversions& Conversions() {
	static CSharpConversions c(Compilation());
	return c;
}

} // namespace

// ===========================================================================
// `bestCandidate == null` -> the candidate becomes the new best.
// ===========================================================================

// The first candidate becomes the best when no best exists yet.
TEST(ConsiderIfNewCandidateIsBestTest, FirstCandidateBecomesBestWhenBestIsNull) {
	auto p = P(Int());
	auto m = Method({p.get()});
	auto candidate = std::make_shared<OverloadResolutionCandidate>(m.get(), /*isExpanded=*/false);
	std::shared_ptr<OverloadResolutionCandidate> best;
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), candidate.get());
	EXPECT_FALSE(validated);
	EXPECT_EQ(ambiguous.get(), nullptr);
}

// The first-candidate path resets `bestCandidateWasValidated` to false even if it was true.
TEST(ConsiderIfNewCandidateIsBestTest, FirstCandidateResetsWasValidatedToFalse) {
	auto p = P(Int());
	auto m = Method({p.get()});
	auto candidate = std::make_shared<OverloadResolutionCandidate>(m.get(), /*isExpanded=*/false);
	std::shared_ptr<OverloadResolutionCandidate> best;
	bool validated = true;  // a prior validation flag that must reset
	std::shared_ptr<OverloadResolutionCandidate> ambiguous;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_FALSE(validated);
}

// ===========================================================================
// `BetterFunctionMember` returns 0 (neither better) -> record the candidate as ambiguous WITH the
// existing best; the best itself stays, and the validation flag is untouched.
// ===========================================================================

// Two identical applicable candidates -> `BetterFunctionMember` returns 0 -> the new candidate is
// recorded as ambiguous with the existing best, and the best is unchanged.
TEST(ConsiderIfNewCandidateIsBestTest, AmbiguousOverwritesAmbiguousWithWhenReturns0) {
	std::shared_ptr<TestParameter> pBest, pNew;
	std::shared_ptr<TestMethod> mBest, mNew;
	auto best = MakeApplicableCandidate(pBest, mBest, /*errorCount=*/0);
	auto candidate = MakeApplicableCandidate(pNew, mNew, /*errorCount=*/0);
	auto* bestBefore = best.get();
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), bestBefore);  // best unchanged
	EXPECT_EQ(ambiguous.get(), candidate.get());  // the new candidate recorded as ambiguous
	EXPECT_TRUE(validated);  // untouched (the ambiguous arm does not reset the flag)
}

// The ambiguous case OVERWRITES a pre-existing `bestCandidateAmbiguousWith` (the C# comment: "so
// that API users can detect the set of all ambiguous methods if they look at bestCandidateAmbiguousWith
// after each step").
TEST(ConsiderIfNewCandidateIsBestTest, AmbiguousOverwritesPreExistingAmbiguousWith) {
	std::shared_ptr<TestParameter> pBest, pNew, pPrior;
	std::shared_ptr<TestMethod> mBest, mNew, mPrior;
	auto best = MakeApplicableCandidate(pBest, mBest, /*errorCount=*/0);
	auto candidate = MakeApplicableCandidate(pNew, mNew, /*errorCount=*/0);
	auto prior = MakeApplicableCandidate(pPrior, mPrior, /*errorCount=*/0);
	auto* bestBefore = best.get();
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous = prior;  // a prior ambiguous partner
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), bestBefore);  // best unchanged
	EXPECT_EQ(ambiguous.get(), candidate.get());  // overwritten, not appended
	EXPECT_NE(ambiguous.get(), prior.get());
}

// ===========================================================================
// `BetterFunctionMember` returns 1 (the new candidate is better) -> promote it to best.
// ===========================================================================

// The new candidate is applicable (ErrorCount 0), the existing best is not (ErrorCount 1) ->
// `BetterFunctionMember` returns 1 -> the new candidate becomes the best.
TEST(ConsiderIfNewCandidateIsBestTest, NewCandidateBecomesBestWhenReturns1) {
	std::shared_ptr<TestParameter> pBest, pNew;
	std::shared_ptr<TestMethod> mBest, mNew;
	auto best = MakeApplicableCandidate(pBest, mBest, /*errorCount=*/1);  // existing best inapplicable
	auto candidate = MakeApplicableCandidate(pNew, mNew, /*errorCount=*/0);  // new candidate applicable
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), candidate.get());  // promoted
	EXPECT_FALSE(validated);  // reset
	EXPECT_EQ(ambiguous.get(), nullptr);  // cleared
}

// Promoting a new best clears a pre-existing `bestCandidateAmbiguousWith`.
TEST(ConsiderIfNewCandidateIsBestTest, PromotionClearsPreExistingAmbiguousWith) {
	std::shared_ptr<TestParameter> pBest, pNew, pPrior;
	std::shared_ptr<TestMethod> mBest, mNew, mPrior;
	auto best = MakeApplicableCandidate(pBest, mBest, /*errorCount=*/1);
	auto candidate = MakeApplicableCandidate(pNew, mNew, /*errorCount=*/0);
	auto prior = MakeApplicableCandidate(pPrior, mPrior, /*errorCount=*/0);
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous = prior;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), candidate.get());
	EXPECT_EQ(ambiguous.get(), nullptr);  // cleared
}

// ===========================================================================
// `BetterFunctionMember` returns 2 (the existing best stays best) -> change nothing.
// ===========================================================================

// The new candidate is inapplicable (ErrorCount 1), the existing best is applicable (ErrorCount 0)
// -> `BetterFunctionMember` returns 2 -> the best stays, the validation flag and ambiguous partner
// are untouched.
TEST(ConsiderIfNewCandidateIsBestTest, BestStaysBestWhenReturns2) {
	std::shared_ptr<TestParameter> pBest, pNew;
	std::shared_ptr<TestMethod> mBest, mNew;
	auto best = MakeApplicableCandidate(pBest, mBest, /*errorCount=*/0);  // existing best applicable
	auto candidate = MakeApplicableCandidate(pNew, mNew, /*errorCount=*/1);  // new candidate inapplicable
	auto* bestBefore = best.get();
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), bestBefore);  // best unchanged
	EXPECT_TRUE(validated);  // untouched
	EXPECT_EQ(ambiguous.get(), nullptr);  // untouched (was null)
}

// The "stays best" case preserves a pre-existing `bestCandidateAmbiguousWith`.
TEST(ConsiderIfNewCandidateIsBestTest, BestStaysBestPreservesPreExistingAmbiguousWith) {
	std::shared_ptr<TestParameter> pBest, pNew, pPrior;
	std::shared_ptr<TestMethod> mBest, mNew, mPrior;
	auto best = MakeApplicableCandidate(pBest, mBest, /*errorCount=*/0);
	auto candidate = MakeApplicableCandidate(pNew, mNew, /*errorCount=*/1);
	auto prior = MakeApplicableCandidate(pPrior, mPrior, /*errorCount=*/0);
	auto* bestBefore = best.get();
	bool validated = true;
	std::shared_ptr<OverloadResolutionCandidate> ambiguous = prior;
	ConsiderIfNewCandidateIsBest(Conversions(), Args(), best, validated, ambiguous, candidate);
	EXPECT_EQ(best.get(), bestBefore);  // best unchanged
	EXPECT_EQ(ambiguous.get(), prior.get());  // preserved
	EXPECT_TRUE(validated);  // untouched
}
