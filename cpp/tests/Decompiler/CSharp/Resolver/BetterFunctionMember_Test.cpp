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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution.BetterFunctionMember` (OverloadResolution.cs line 730, C# spec
// draft-v11 section 12.6.4.3 "better function member") ported as a `Detail::` free function taking
// `CSharpConversions&` + the arguments vector + two candidates (the D536
// `CheckApplicabilityPassingModeAndConversions` precedent -- the `arguments`/`conversions` are the
// `OverloadResolution` instance fields, passed as parameters since the free function has no
// instance state). The full decision:
//   * the "prefer applicable members" heuristic (`ErrorCount == 0` vs `> 0`);
//   * the per-argument better-conversion loop (the argument-to-parameter map +
//     `Detail::IdentityConversion` over the formal parameter types + the public
//     `BetterConversion(ResolveResult, IType, IType)` of each argument to the two target types --
//     the direction-exclusive `c1IsBetter`/`c2IsBetter` reduction, plus the `parameterTypesEqual`
//     gate for the tie-breaking rules);
//   * the "prefer members with less errors" heuristic;
//   * the tie-breaking rules (only when neither is better AND parameter types are all equal):
//     non-generic beats generic, non-expanded beats expanded, fewer arguments-to-params,
//     no-unmapped-optional-parameters, `MoreSpecificFormalParameters`, non-lifted operators (a
//     `dynamic_cast` to `ILiftedOperator`), `BetterParameterPassingChoice`, and
//     `BetterParamsCollectionType` (when both are expanded).
//
// The tests pin each decision arm in isolation (constructing candidates with filled
// `ParameterTypes`/`ArgumentToParameterMap`/`ErrorCount`/`HasUnmappedOptionalParameters` and a
// matching arguments vector) plus the tiebreak-block gate (`parameterTypesEqual == false` skips the
// tie-breaking rules).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
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

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::BetterFunctionMember;
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
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

// A minimal `IParameter` with a configurable type, `ReferenceKind`, and `IsParams` flag (the
// D524/D536/D548 `TestParameter` precedent extended with an `IsParams` flag for the expanded-form
// candidates; the cross-scope name-hiding crux means the enum references are fully-qualified).
class TestParameter : public IParameter {
public:
	explicit TestParameter(ITypePtr type,
	                       ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
	                       std::string name = "p",
	                       bool isParams = false)
		: name_(std::move(name)), type_(std::move(type)), refKind_(refKind), isParams_(isParams) {}
	::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
	{ return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
	std::string Name() const override { return name_; }
	const IType& Type() const override { return *type_; }
	bool IsConst() const override { return false; }
	std::any GetConstantValue(bool) const override { return std::any{}; }
	std::vector<const IAttribute*> GetAttributes() const override { return {}; }
	::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
	bool IsParams() const override { return isParams_; }
	bool IsOptional() const override { return false; }
	bool HasConstantValueInSignature() const override { return false; }
	const IParameterizedMember* Owner() const override { return nullptr; }
	LifetimeAnnotation Lifetime() const override { return {}; }
private:
	std::string name_;
	ITypePtr type_;
	::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
	bool isParams_;
};

// A `LookupMethod` whose `Parameters()` and `MemberDefinition()` return the configured parameter
// list and `this` (so the `OverloadResolutionCandidate` ctor reads `MemberDefinition()->Parameters()`
// to populate the candidate's `Parameters()`, the D510/D536/D548 `TestMethod` precedent).
class TestMethod : public LookupMethod {
public:
	explicit TestMethod(std::vector<const IParameter*> params, std::string name = "M")
		: LookupMethod(std::move(name), Compilation()), params_(std::move(params)) {}
	std::vector<const IParameter*> Parameters() const override { return params_; }
	const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
private:
	std::vector<const IParameter*> params_;
};

// A `TestMethod` that reports itself as generic (a non-empty `TypeParameters()` list) -- the
// `IsGenericMethod` candidate property derives from `dynamic_cast<IMethod*> && !TypeParameters().
// empty()` (the `LookupMethod` IS-A `IMethod`). Used for the non-generic-beats-generic tiebreak.
class GenericTestMethod : public TestMethod {
public:
	explicit GenericTestMethod(std::vector<const IParameter*> params, std::string name = "M")
		: TestMethod(std::move(params), std::move(name)) {}
	std::vector<const ITypeParameter*> TypeParameters() const override { return {TParam()}; }
private:
	// A type-parameter stub named "T" kept alive for the program lifetime (the candidate and
	// `IsGenericMethod` only read the count, not the pointee). Returned as a non-owning pointer.
	static const ITypeParameter* TParam() {
		static auto t = std::make_shared<LookupTypeParameter>("T");
		return t.get();
	}
};

// A `LookupMethod` that also implements `ILiftedOperator` -- the faithful shape of a concrete
// lifted-operator method (the D549 standalone-base design). The `IParameterizedMember`-ness comes
// from the `LookupMethod` base; the `ILiftedOperator` marker is an additional standalone base (no
// `IParameterizedMember` diamond). The two `NonLifted*` properties return the same data as the
// `IParameterizedMember::Parameters()` / return type (a real lifted operator's non-lifted signature
// IS the underlying method's signature); only the `dynamic_cast` to `ILiftedOperator` matters here.
class LiftedMethodStub : public LookupMethod, public ILiftedOperator {
public:
	explicit LiftedMethodStub(std::vector<const IParameter*> params, ITypePtr returnType,
	                          std::string name = "op_Lifted")
		: LookupMethod(std::move(name), Compilation()), params_(std::move(params)),
		  returnType_(std::move(returnType)) {}
	// `LookupMethod::Parameters()` is non-virtual-overridden: this override returns the configured
	// list (the candidate reads `MemberDefinition()->Parameters()` = this).
	std::vector<const IParameter*> Parameters() const override { return params_; }
	const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
	// --- ILiftedOperator ---
	const IType& NonLiftedReturnType() const override { return *returnType_; }
	std::vector<const IParameter*> NonLiftedParameters() const override { return params_; }
private:
	std::vector<const IParameter*> params_;
	ITypePtr returnType_;
};

// A primitive element definition (`Int32` etc.) -- a struct with the `KnownTypeCode` (the D514/D548
// `Prim`/`MakeDef` precedent; `GetTypeCode` resolves the `KnownTypeCode` so `IdentityConversion`
// and `BetterConversion` resolve).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind, int typeParamCount) {
	std::string name = "T" + std::to_string(static_cast<int>(ktc));
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, typeParamCount)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	return MakeDef(ktc, kind, 0);
}
// Function-local static singletons so the SAME `IType` instance is shared across a candidate's
// `ParameterTypes`, the other candidate's `ParameterTypes`, and the argument's type --
// `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so two
// distinct `Def(Int32)` instances are NOT identity-convertible (which would falsely set
// `parameterTypesEqual = false` and skip the tie-break block). A real compilation caches the
// `int` type as a single instance; the statics faithfully model that (the D547
// `ReadOnlySpanDef`/`SpanDef` precedent).
ITypePtr Int() { static auto t = Def(KnownTypeCode::Int32); return t; }
ITypePtr Long() { static auto t = Def(KnownTypeCode::Int64); return t; }
ITypePtr String() { static auto t = Def(KnownTypeCode::String, TypeKind::Class); return t; }

// A type-parameter stub named "T" (the D546 `TParam` precedent).
ITypePtr TParam() { return std::make_shared<LookupTypeParameter>("T"); }

// The `System.ReadOnlySpan`1` / `System.Span`1` generic definitions (the D547 precedent).
std::shared_ptr<LookupTypeDefinition> ReadOnlySpanDef() {
	static auto d = std::make_shared<LookupTypeDefinition>("ReadOnlySpan`1", "System",
		FullTypeName(TopLevelTypeName("System", "ReadOnlySpan`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::ReadOnlySpanOfT);
	return d;
}
std::shared_ptr<LookupTypeDefinition> SpanDef() {
	static auto d = std::make_shared<LookupTypeDefinition>("Span`1", "System",
		FullTypeName(TopLevelTypeName("System", "Span`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::SpanOfT);
	return d;
}
ITypePtr ReadOnlySpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(ReadOnlySpanDef(), std::vector<ITypePtr>{std::move(element)});
}
ITypePtr SpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(SpanDef(), std::vector<ITypePtr>{std::move(element)});
}
ITypePtr ArrayOf(ITypePtr element) {
	return std::make_shared<ArrayType>(std::move(element));
}

// A by-value `ResolveResult` over the supplied type (the D536 `Arg` precedent; a plain
// `ResolveResult` -- `IsCompileTimeConstant` is false, so `BetterConversion` uses the IType-based
// identity / `BetterConversionTarget` arms).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
	return std::make_shared<ResolveResult>(std::move(type));
}

// A by-value `TestParameter` over a type (the common case).
std::shared_ptr<TestParameter> P(ITypePtr type = Int(),
                                 ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                                 bool isParams = false) {
	return std::make_shared<TestParameter>(std::move(type), refKind, "p", isParams);
}

// Builds a `TestMethod` (non-generic, non-lifted) from the supplied parameter list. The
// `shared_ptr<TestParameter>` keep-alives must outlive the returned method (the method holds
// non-owning `const IParameter*`); the test scopes hold them.
std::shared_ptr<TestMethod> Method(std::vector<const IParameter*> params) {
	return std::make_shared<TestMethod>(std::move(params));
}
std::shared_ptr<GenericTestMethod> GenericMethod(std::vector<const IParameter*> params) {
	return std::make_shared<GenericTestMethod>(std::move(params));
}
std::shared_ptr<LiftedMethodStub> LiftedMethod(std::vector<const IParameter*> params,
                                                ITypePtr returnType = Int()) {
	return std::make_shared<LiftedMethodStub>(std::move(params), std::move(returnType));
}

} // namespace

// ===========================================================================
// "Prefer applicable members" heuristic (ErrorCount == 0 vs > 0).
// ===========================================================================

// c1 has no errors, c2 has errors -> c1 is the better function member -> returns 1 (this fires
// before the better-conversion loop).
TEST(BetterFunctionMemberTest, PreferApplicableC1Returns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c2.ErrorCount() = 1;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// c2 has no errors, c1 has errors -> c2 is better -> returns 2.
TEST(BetterFunctionMemberTest, PreferApplicableC2Returns2) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ErrorCount() = 1;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// Better-conversion loop -- `BetterConversion(ResolveResult, IType, IType)`.
// ===========================================================================

// The argument exactly matches c1's parameter type (int) but not c2's (long) -> `BetterConversion`
// returns 1 -> c1IsBetter -> returns 1 (before the tiebreak block; `parameterTypesEqual` is false
// but the `c1IsBetter && !c2IsBetter` short-circuit fires first).
TEST(BetterFunctionMemberTest, BetterConversionFavoursC1Returns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Long()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// The argument exactly matches c2's parameter type (int) but not c1's (long) -> `BetterConversion`
// returns 2 -> c2IsBetter -> returns 2.
TEST(BetterFunctionMemberTest, BetterConversionFavoursC2Returns2) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Long()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// c1 maps an argument that c2 leaves unmapped (p1 >= 0, p2 < 0) -> c1IsBetter -> returns 1.
TEST(BetterFunctionMemberTest, UnmappedArgFavoursC1Returns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {-1};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// c2 maps an argument that c1 leaves unmapped (p1 < 0, p2 >= 0) -> c2IsBetter -> returns 2.
TEST(BetterFunctionMemberTest, UnmappedArgFavoursC2Returns2) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {-1};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// "Prefer members with less errors" heuristic (reached when both have > 0 errors but differ).
// ===========================================================================

// Both have errors (so the top applicability arm does not fire), neither is better in the
// conversion loop, c1 has fewer errors -> returns 1.
TEST(BetterFunctionMemberTest, LessErrorsC1Returns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	c1.ErrorCount() = 1;
	c2.ErrorCount() = 2;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// The mirror: c2 has fewer errors -> returns 2.
TEST(BetterFunctionMemberTest, LessErrorsC2Returns2) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	c1.ErrorCount() = 2;
	c2.ErrorCount() = 1;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// Tiebreak-block gate: `parameterTypesEqual == false` skips the tie-breaking rules.
// ===========================================================================

// The formal parameter types at a mapped position are not identity-convertible (int vs string),
// the argument matches neither (a `long` arg: identity to neither int nor string), and
// `BetterConversionTarget(int, string)` is 0 (no implicit conversion either way) -> neither is
// better, errors are equal, but `parameterTypesEqual` is false -> the tie-break block is skipped
// -> returns 0.
TEST(BetterFunctionMemberTest, ParameterTypesNotEqualSkipsTiebreaksReturns0) {
	auto pa = P(Int());
	auto pb = P(String());
	auto m1 = Method({pa.get(), pb.get()});
	auto m2 = Method({pa.get(), pb.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	// Two args: the first maps to int on both (identity, parameterTypesEqual unchanged); the
	// second maps to int (c1) vs string (c2) -- not identity -> parameterTypesEqual = false, and
	// the `long` arg matches neither -> BetterConversion 0.
	c1.ParameterTypes() = {Int(), Int()};
	c2.ParameterTypes() = {Int(), String()};
	c1.ArgumentToParameterMap() = {0, 1};
	c2.ArgumentToParameterMap() = {0, 1};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int()), Arg(Long())}, c1, c2), 0);
}

// ===========================================================================
// Tiebreak 1: non-generic methods are better.
// ===========================================================================

// c1 is non-generic, c2 is generic, both otherwise equal -> returns 1.
TEST(BetterFunctionMemberTest, NonGenericBeatsGenericReturns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = GenericMethod({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// The mirror: c1 is generic, c2 is non-generic -> returns 2.
TEST(BetterFunctionMemberTest, GenericLosesToNonGenericReturns2) {
	auto p = P(Int());
	auto m1 = GenericMethod({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// Tiebreak 2: non-expanded members are better.
// ===========================================================================

// c1 is non-expanded, c2 is expanded, both otherwise equal (the single argument maps to the
// regular `int` parameter on both) -> returns 1.
TEST(BetterFunctionMemberTest, NonExpandedBeatsExpandedReturns1) {
	auto a = P(Int());
	auto b = P(ArrayOf(Int()), ReferenceKind::None, /*isParams=*/true);
	auto m1 = Method({a.get(), b.get()});
	auto m2 = Method({a.get(), b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/true);
	// Only the first argument maps (to the `int` param on both); the params parameter is not
	// reached by an argument. `ParameterTypes` carry the unpacked element (`int`) for the
	// expanded c2's params position, but that index is never read here.
	c1.ParameterTypes() = {Int(), ArrayOf(Int())};
	c2.ParameterTypes() = {Int(), Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// The mirror: c1 is expanded, c2 is non-expanded -> returns 2.
TEST(BetterFunctionMemberTest, ExpandedLosesToNonExpandedReturns2) {
	auto a = P(Int());
	auto b = P(ArrayOf(Int()), ReferenceKind::None, /*isParams=*/true);
	auto m1 = Method({a.get(), b.get()});
	auto m2 = Method({a.get(), b.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/true);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int(), Int()};
	c2.ParameterTypes() = {Int(), ArrayOf(Int())};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// Tiebreak 3: prefer the member with FEWER arguments mapped to the params-collection.
// ===========================================================================

// Both expanded; c1 has fewer arguments mapped to its params parameter (1) than c2 (2) -> the
// formal types at every mapped position are `int` (identity) so the tie-break block runs, and the
// fewer-args-to-params rule returns 1. c1 has a 3-param list `[int, int, params int[]]` (the params
// parameter at index 2, one argument maps to it); c2 has a 2-param list `[int, params int[]]`
// (the params parameter at index 1, two arguments map to it).
TEST(BetterFunctionMemberTest, FewerArgsToParamsFavoursC1Returns1) {
	auto a = P(Int());
	auto c = P(Int());
	auto b1 = P(ArrayOf(Int()), ReferenceKind::None, /*isParams=*/true);
	auto b2 = P(ArrayOf(Int()), ReferenceKind::None, /*isParams=*/true);
	auto m1 = Method({a.get(), c.get(), b1.get()});  // 3 params, params at index 2
	auto m2 = Method({a.get(), b2.get()});           // 2 params, params at index 1
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/true);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/true);
	// Three `int` arguments; the unpacked `ParameterTypes` are all `int` (the params position's
	// element type), so every mapped pair is identity -> `parameterTypesEqual` stays true.
	c1.ParameterTypes() = {Int(), Int(), Int()};
	c2.ParameterTypes() = {Int(), Int()};
	// c1: arg0->a(0), arg1->c(1), arg2->params(2) -- one arg to the params parameter.
	// c2: arg0->a(0), arg1->params(1), arg2->params(1) -- two args to the params parameter.
	c1.ArgumentToParameterMap() = {0, 1, 2};
	c2.ArgumentToParameterMap() = {0, 1, 1};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int()), Arg(Int()), Arg(Int())}, c1, c2), 1);
}

// ===========================================================================
// Tiebreak 4: prefer the member where no default values need to be substituted.
// ===========================================================================

// c1 has no unmapped optional parameters, c2 has unmapped optional parameters, both otherwise
// equal -> returns 1.
TEST(BetterFunctionMemberTest, NoUnmappedOptionalBeatsUnmappedReturns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	c2.HasUnmappedOptionalParameters() = true;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// The mirror: c1 has unmapped optional, c2 does not -> returns 2.
TEST(BetterFunctionMemberTest, UnmappedOptionalLosesToNoUnmappedReturns2) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	c1.HasUnmappedOptionalParameters() = true;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// Tiebreak 5: `MoreSpecificFormalParameters` -- the count tiebreak (prefer the member with MORE
// formal parameters), reached when both have unmapped optional parameters (so tiebreak 4 is
// neutral) but different total parameter counts.
// ===========================================================================

// Both have unmapped optional parameters (tiebreak 4 neutral); c1 has MORE formal parameters
// (3) than c2 (2); the single argument maps to the first `int` parameter on both (identity) ->
// `MoreSpecificFormalParameters` count tiebreak returns 1.
TEST(BetterFunctionMemberTest, MoreFormalParametersFavoursC1Returns1) {
	auto a = P(Int());
	auto b = P(Int());
	auto c = P(Int());
	auto m1 = Method({a.get(), b.get(), c.get()});  // 3 params
	auto m2 = Method({a.get(), b.get()});          // 2 params
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int(), Int(), Int()};
	c2.ParameterTypes() = {Int(), Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	// Both have unmapped optional parameters (the extra params are unmapped) -> tiebreak 4 neutral.
	c1.HasUnmappedOptionalParameters() = true;
	c2.HasUnmappedOptionalParameters() = true;
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// ===========================================================================
// Tiebreak 6: prefer non-lifted operators (a `dynamic_cast` to `ILiftedOperator`).
// ===========================================================================

// c1's member is a plain method (not a lifted operator), c2's member is a lifted operator
// (`LiftedMethodStub`), both otherwise equal -> the `dynamic_cast` to `ILiftedOperator` is null for
// c1 and non-null for c2 -> returns 1.
TEST(BetterFunctionMemberTest, NonLiftedBeatsLiftedReturns1) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = LiftedMethod({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// The mirror: c1 is a lifted operator, c2 is a plain method -> returns 2.
TEST(BetterFunctionMemberTest, LiftedLosesToNonLiftedReturns2) {
	auto p = P(Int());
	auto m1 = LiftedMethod({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 2);
}

// ===========================================================================
// Tiebreak 7: `BetterParameterPassingChoice` -- prefer by-value parameters over in-parameters.
// ===========================================================================

// c1's parameter is by-value (`None`), c2's is `in`, both otherwise equal -> the by-value-over-in
// tiebreak returns 1.
TEST(BetterFunctionMemberTest, ByValueBeatsInReturns1) {
	auto p1 = P(Int(), ReferenceKind::None);
	auto p2 = P(Int(), ReferenceKind::In);
	auto m1 = Method({p1.get()});
	auto m2 = Method({p2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// ===========================================================================
// Tiebreak 8: `BetterParamsCollectionType` (both expanded) -- prefer `ReadOnlySpan<T>` over
// `Span<T>` when the element types identity-match.
// ===========================================================================

// Both expanded; c1's params-collection type is `ReadOnlySpan<int>`, c2's is `Span<int>`, both
// otherwise equal (the single argument maps to the regular `int` parameter; the params parameter
// carries the span type and is not reached by an argument) -> `BetterParamsCollectionType` returns
// 1 (ReadOnlySpan beats Span by element identity).
TEST(BetterFunctionMemberTest, ReadOnlySpanBeatsSpanReturns1) {
	auto a = P(Int());
	auto b1 = P(ReadOnlySpanOf(Int()), ReferenceKind::None, /*isParams=*/true);
	auto b2 = P(SpanOf(Int()), ReferenceKind::None, /*isParams=*/true);
	auto m1 = Method({a.get(), b1.get()});
	auto m2 = Method({a.get(), b2.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/true);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/true);
	// The first argument maps to the `int` parameter on both (identity); the params position's
	// unpacked `ParameterTypes` is `int` (the span element) on both.
	c1.ParameterTypes() = {Int(), Int()};
	c2.ParameterTypes() = {Int(), Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 1);
}

// ===========================================================================
// All tiebreaks neutral -> returns 0.
// ===========================================================================

// Two identical non-generic, non-expanded, by-value candidates with identity parameter types and an
// identity argument conversion -> neither is better, errors equal, every tiebreak neutral ->
// returns 0.
TEST(BetterFunctionMemberTest, AllEqualReturns0) {
	auto p = P(Int());
	auto m1 = Method({p.get()});
	auto m2 = Method({p.get()});
	OverloadResolutionCandidate c1(m1.get(), /*isExpanded=*/false);
	OverloadResolutionCandidate c2(m2.get(), /*isExpanded=*/false);
	c1.ParameterTypes() = {Int()};
	c2.ParameterTypes() = {Int()};
	c1.ArgumentToParameterMap() = {0};
	c2.ArgumentToParameterMap() = {0};
	CSharpConversions conversions(Compilation());
	EXPECT_EQ(BetterFunctionMember(conversions, {Arg(Int())}, c1, c2), 0);
}
