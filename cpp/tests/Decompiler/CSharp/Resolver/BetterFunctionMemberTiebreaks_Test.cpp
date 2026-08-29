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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the two pure `Candidate`-taking tiebreaks in the `OverloadResolution`
// `BetterFunctionMember` region (OverloadResolution.cs) ported as `Detail::` free functions:
//   * `BetterParameterPassingChoice(Candidate, Candidate)` -- the C# 7.2 "prefer by-value
//     parameters over in-parameters" tiebreak: a by-value (`ReferenceKind::None`) parameter beats
//     an `in` (`ReferenceKind::In`) parameter at the same position; the direction-exclusive
//     reduction returns 1 / 2 / 0.
//   * `MoreSpecificFormalParameters(Candidate, Candidate)` -- the Candidate-taking entry that
//     first prefers the member with MORE formal parameters, then delegates to the type-sequence
//     `MoreSpecificFormalParameters` overload over the two candidates' parameter types.
//
// Both helpers are pure (they read only the candidates' `Parameters()`); the tests pin the
// by-value-vs-in crux, the mixed-verdict reduction, the non-triggering reference kinds
// (`Out`/`Ref`/`RefReadOnly`), the parameter-count tiebreak, and the delegation to the
// type-parameter-less-specific rule.

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::BetterParameterPassingChoice;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MoreSpecificFormalParameters;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A minimal `IParameter` with a configurable type and `ReferenceKind` (the D524/D536 `TestParameter`
// precedent; the cross-scope name-hiding crux means the enum references are fully-qualified with
// `::ILSpy::Decompiler::TypeSystem::`).
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
	std::vector<const IAttribute*> GetAttributes() const override { return {}; }
	::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
	bool IsParams() const override { return false; }
	bool IsOptional() const override { return false; }
	bool HasConstantValueInSignature() const override { return false; }
	const IParameterizedMember* Owner() const override { return nullptr; }
	LifetimeAnnotation Lifetime() const override { return {}; }
private:
	std::string name_;
	ITypePtr type_;
	::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `LookupMethod` whose `Parameters()` and `MemberDefinition()` return the configured parameter
// list and `this` (so the `OverloadResolutionCandidate` ctor reads `MemberDefinition()->Parameters()`
// to populate the candidate's `Parameters()`, the D510/D536 `TestMethod` precedent).
class TestMethod : public LookupMethod {
public:
	explicit TestMethod(std::vector<const IParameter*> params, std::string name = "M")
		: LookupMethod(std::move(name), Compilation()), params_(std::move(params)) {}
	std::vector<const IParameter*> Parameters() const override { return params_; }
	const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
private:
	std::vector<const IParameter*> params_;
};

// A primitive element definition (`Int32`) -- a struct with the `KnownTypeCode` (the D546
// `Prim` precedent). Used as the non-type-parameter side in the type-parameter-less-specific crux.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind, int typeParamCount) {
	std::string name = "T" + std::to_string(static_cast<int>(ktc));
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, typeParamCount)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Prim(KnownTypeCode ktc) { return MakeDef(ktc, TypeKind::Struct, 0); }
ITypePtr Int() { return Prim(KnownTypeCode::Int32); }

// A non-generic class definition -- the non-type-parameter side for the
// `MoreSpecificFormalParameters` type-parameter crux (the D546 `ClassDef` precedent).
std::shared_ptr<LookupTypeDefinition> ClassDef() {
	return MakeDef(KnownTypeCode::None, TypeKind::Class, 0);
}

// A type parameter stub named "T" (the D546 `TParam` precedent).
ITypePtr TParam() { return std::make_shared<LookupTypeParameter>("T"); }

// A by-value `TestParameter` over `int` (the common case; the `ReferenceKind` is the load-bearing
// input for `BetterParameterPassingChoice`, the `Type` for `MoreSpecificFormalParameters`).
std::shared_ptr<TestParameter> P(ITypePtr type = Int(),
                                 ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None) {
	return std::make_shared<TestParameter>(std::move(type), refKind);
}

// Builds a `TestMethod` (and the underlying `OverloadResolutionCandidate`) from the supplied
// parameter list. The `shared_ptr<TestParameter>` keep-alives must outlive the returned candidate
// (the candidate holds non-owning `const IParameter*`); the test scopes hold them.
std::shared_ptr<TestMethod> Method(std::vector<const IParameter*> params) {
	return std::make_shared<TestMethod>(std::move(params));
}

} // namespace

// ===========================================================================
// BetterParameterPassingChoice -- the by-value-vs-in tiebreak.
// ===========================================================================

// c1 by-value, c2 `in` at the same position -> c1 is better -> returns 1.
TEST(BetterFunctionMemberTiebreaksTest, ByValueBeatsInReturns1) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 1);
}

// c1 `in`, c2 by-value -> c2 is better -> returns 2.
TEST(BetterFunctionMemberTiebreaksTest, InWorseThanByValueReturns2) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 2);
}

// Both by-value -> neither side favoured -> returns 0.
TEST(BetterFunctionMemberTiebreaksTest, BothByValueReturns0) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 0);
}

// Both `in` -> neither side favoured -> returns 0.
TEST(BetterFunctionMemberTiebreaksTest, BothInReturns0) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 0);
}

// A mixed verdict (one position favours c1, another favours c2) -> both flags set -> returns 0.
TEST(BetterFunctionMemberTiebreaksTest, MixedVerdictReturns0) {
	auto c1a = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto c1b = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto c2a = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto c2b = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto m1 = Method({c1a.get(), c1b.get()});
	auto m2 = Method({c2a.get(), c2b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 0);
}

// `Out` vs `in` does not trigger the by-value-vs-in tiebreak (only `None` vs `In` matters) -> 0.
TEST(BetterFunctionMemberTiebreaksTest, OutVersusInDoesNotTriggerReturns0) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::Out);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 0);
}

// `Ref` vs by-value does not trigger (only `None` vs `In` matters) -> 0.
TEST(BetterFunctionMemberTiebreaksTest, RefVersusByValueDoesNotTriggerReturns0) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 0);
}

// `RefReadOnly` vs by-value does not trigger -> 0.
TEST(BetterFunctionMemberTiebreaksTest, RefReadOnlyVersusByValueDoesNotTriggerReturns0) {
	auto p1 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::RefReadOnly);
	auto p2 = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 0);
}

// Two by-value params where the second position is c1 by-value / c2 `in` -> c1 wins -> returns 1.
TEST(BetterFunctionMemberTiebreaksTest, OneByValueVsInPositionFavoursC1Returns1) {
	auto c1a = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto c1b = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto c2a = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto c2b = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto m1 = Method({c1a.get(), c1b.get()});
	auto m2 = Method({c2a.get(), c2b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 1);
}

// Two params where the first position is c1 `in` / c2 by-value -> c2 wins -> returns 2.
TEST(BetterFunctionMemberTiebreaksTest, OneInVsByValuePositionFavoursC2Returns2) {
	auto c1a = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto c1b = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto c2a = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
	auto c2b = P(Int(), ::ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
	auto m1 = Method({c1a.get(), c1b.get()});
	auto m2 = Method({c2a.get(), c2b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(BetterParameterPassingChoice(c1, c2), 2);
}

// ===========================================================================
// MoreSpecificFormalParameters(Candidate, Candidate) -- the count tiebreak then the
// type-sequence delegation.
// ===========================================================================

// c1 has MORE formal parameters -> returns 1 (the "prefer the member with more formal parameters"
// heuristic, in case both have different numbers of optional parameters).
TEST(BetterFunctionMemberTiebreaksTest, MoreFormalParametersReturns1) {
	auto i1 = P(Int());
	auto i2a = P(Int());
	auto i2b = P(Int());
	auto m1 = Method({i1.get(), i2a.get()});
	auto m2 = Method({i2b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(MoreSpecificFormalParameters(c1, c2), 1);
}

// c1 has FEWER formal parameters (c2 has more) -> returns 2.
TEST(BetterFunctionMemberTiebreaksTest, FewerFormalParametersReturns2) {
	auto i1 = P(Int());
	auto i2a = P(Int());
	auto i2b = P(Int());
	auto m1 = Method({i1.get()});
	auto m2 = Method({i2a.get(), i2b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(MoreSpecificFormalParameters(c1, c2), 2);
}

// Equal count; c1's param is a non-type-parameter, c2's is a type parameter -> the non-type-
// parameter is more specific -> the type-sequence overload returns 1 -> c1 wins.
TEST(BetterFunctionMemberTiebreaksTest, EqualCountNonTypeParamBeatsTypeParamReturns1) {
	auto cls = ClassDef();
	auto t = TParam();
	auto p1 = P(cls);
	auto p2 = P(t);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(MoreSpecificFormalParameters(c1, c2), 1);
}

// Equal count; c1's param is a type parameter, c2's is a non-type-parameter -> the non-type-
// parameter (c2) is more specific -> returns 2.
TEST(BetterFunctionMemberTiebreaksTest, EqualCountTypeParamLosesToNonTypeParamReturns2) {
	auto t = TParam();
	auto cls = ClassDef();
	auto p1 = P(t);
	auto p2 = P(cls);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(MoreSpecificFormalParameters(c1, c2), 2);
}

// Equal count; same types (same instance) -> the type-sequence overload's per-pair verdict is 0
// (neither type parameter, neither ParameterizedType, neither TypeWithElementType) -> returns 0.
TEST(BetterFunctionMemberTiebreaksTest, EqualCountSameTypesReturns0) {
	auto i = P(Int());
	auto j = P(Int());
	auto m1 = Method({i.get()});
	auto m2 = Method({j.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(MoreSpecificFormalParameters(c1, c2), 0);
}

// Equal count; a mixed verdict (one pair favours c1, the other c2) -> the type-sequence overload
// returns 0 (both flags set).
TEST(BetterFunctionMemberTiebreaksTest, EqualCountMixedVerdictReturns0) {
	auto cls = ClassDef();
	auto t = TParam();
	auto c1a = P(cls);
	auto c1b = P(t);
	auto c2a = P(t);
	auto c2b = P(cls);
	auto m1 = Method({c1a.get(), c1b.get()});
	auto m2 = Method({c2a.get(), c2b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	EXPECT_EQ(MoreSpecificFormalParameters(c1, c2), 0);
}
