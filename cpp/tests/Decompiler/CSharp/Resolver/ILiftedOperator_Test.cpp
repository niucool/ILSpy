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

// Tests for the `ILiftedOperator` marker interface (CSharpOperators.cs line 1170) ported as a
// standalone abstract base (the `: IParameterizedMember` deviation -- see the header). The
// interface tags a lifted operator's method so `OverloadResolution.BetterFunctionMember` can
// prefer a non-lifted operator (a `dynamic_cast<const ILiftedOperator*>` from an
// `IParameterizedMember*` -- the C# `member as ILiftedOperator`). The tests pin:
//   * the standalone-base design: a `LiftedStub : LookupMethod, ILiftedOperator` constructs
//     cleanly (no `IParameterizedMember` diamond -- `&stub` -> `const IParameterizedMember*` is
//     unambiguous through the `LookupMethod` base);
//   * the `dynamic_cast` cross-cast from `IParameterizedMember*` to `ILiftedOperator*` succeeds
//     for a lifted stub and returns null for a plain non-lifted method (the C# `is`/`as` check);
//   * the two `NonLifted*` properties round-trip.

#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A minimal `IParameter` with a configurable type (the D524/D536/D548 `TestParameter` precedent;
// the cross-scope name-hiding crux means the enum references are fully-qualified).
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

// A `LookupMethod` that also implements `ILiftedOperator` -- the faithful shape of a concrete
// lifted-operator method (a `LiftedXOperatorMethod : UnaryOperatorMethod, ILiftedOperator`): the
// `IParameterizedMember`-ness comes from the `LookupMethod` (=`IMethod`) base, and the
// `ILiftedOperator` marker is an additional standalone base (no `IParameterizedMember` diamond,
// the standalone-base deviation). The two `NonLifted*` properties are configurable so the tests
// can pin their round-trip.
class LiftedMethodStub : public LookupMethod, public ILiftedOperator {
public:
	LiftedMethodStub(ITypePtr nonLiftedReturnType,
	                 std::vector<const IParameter*> nonLiftedParameters,
	                 std::string name = "op_Lifted")
		: LookupMethod(std::move(name), Compilation()),
		  nonLiftedReturnType_(std::move(nonLiftedReturnType)),
		  nonLiftedParameters_(std::move(nonLiftedParameters)) {}

	// --- ILiftedOperator ---
	const IType& NonLiftedReturnType() const override { return *nonLiftedReturnType_; }
	std::vector<const IParameter*> NonLiftedParameters() const override { return nonLiftedParameters_; }

private:
	ITypePtr nonLiftedReturnType_;
	std::vector<const IParameter*> nonLiftedParameters_;
};

// A plain non-lifted `LookupMethod` (no `ILiftedOperator` base) -- the FALSE side of the
// `dynamic_cast` type check.
class PlainMethodStub : public LookupMethod {
public:
	PlainMethodStub() : LookupMethod("M", Compilation()) {}
};

ITypePtr Int() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr Long() { return std::make_shared<KnownType>(KnownTypeCode::Int64); }

} // namespace

// ---------------------------------------------------------------------------
// A `LiftedMethodStub : LookupMethod, ILiftedOperator` IS-A `ILiftedOperator` (the marker base is
// reachable), and IS-A `IParameterizedMember` (through the `LookupMethod` base, unambiguous --
// the standalone-base design avoids the `IParameterizedMember` diamond).
// ---------------------------------------------------------------------------
TEST(ILiftedOperatorTest, LiftedStubIsALiftedOperatorAndIParameterizedMember) {
	auto rt = Int();
	auto p = std::make_shared<TestParameter>(Int());
	LiftedMethodStub stub(rt, {p.get()});
	const IParameterizedMember* asParam = &stub;  // unambiguous through LookupMethod
	EXPECT_NE(asParam, nullptr);
	const ILiftedOperator* asLifted = &stub;  // the ILiftedOperator base
	EXPECT_NE(asLifted, nullptr);
}

// ---------------------------------------------------------------------------
// The `dynamic_cast` cross-cast from an `IParameterizedMember*` to an `ILiftedOperator*` (the C#
// `member as ILiftedOperator` in `BetterFunctionMember`) succeeds for a lifted stub -- the
// runtime finds the `ILiftedOperator` base of the most-derived `LiftedMethodStub` object.
// ---------------------------------------------------------------------------
TEST(ILiftedOperatorTest, DynamicCastFromIParameterizedMemberSucceedsForLiftedStub) {
	auto rt = Int();
	auto p = std::make_shared<TestParameter>(Int());
	auto stub = std::make_shared<LiftedMethodStub>(rt, std::vector<const IParameter*>{p.get()});
	const IParameterizedMember* asParam = stub.get();
	auto* lifted = dynamic_cast<const ILiftedOperator*>(asParam);
	EXPECT_NE(lifted, nullptr);
}

// ---------------------------------------------------------------------------
// The `dynamic_cast` cross-cast returns null for a plain non-lifted `LookupMethod` -- the C#
// `member as ILiftedOperator` yields null when the member is not a lifted operator.
// ---------------------------------------------------------------------------
TEST(ILiftedOperatorTest, DynamicCastFromIParameterizedMemberIsNullForPlainMethod) {
	auto stub = std::make_shared<PlainMethodStub>();
	const IParameterizedMember* asParam = stub.get();
	auto* lifted = dynamic_cast<const ILiftedOperator*>(asParam);
	EXPECT_EQ(lifted, nullptr);
}

// ---------------------------------------------------------------------------
// `NonLiftedReturnType` round-trips the configured return type (the IL layer reads it for the
// lifted call's stack type).
// ---------------------------------------------------------------------------
TEST(ILiftedOperatorTest, NonLiftedReturnTypeRoundTrips) {
	auto rt = Long();
	auto p = std::make_shared<TestParameter>(Int());
	auto stub = std::make_shared<LiftedMethodStub>(rt, std::vector<const IParameter*>{p.get()});
	const ILiftedOperator* lifted = stub.get();
	EXPECT_EQ(&lifted->NonLiftedReturnType(), rt.get());
}

// ---------------------------------------------------------------------------
// `NonLiftedParameters` round-trips the configured parameter list (snapshot of non-owning
// pointers, the IParameterizedMember::Parameters convention).
// ---------------------------------------------------------------------------
TEST(ILiftedOperatorTest, NonLiftedParametersRoundTrips) {
	auto rt = Int();
	auto p1 = std::make_shared<TestParameter>(Int());
	auto p2 = std::make_shared<TestParameter>(Long());
	auto stub = std::make_shared<LiftedMethodStub>(
		rt, std::vector<const IParameter*>{p1.get(), p2.get()});
	const ILiftedOperator* lifted = stub.get();
	auto params = lifted->NonLiftedParameters();
	ASSERT_EQ(params.size(), 2u);
	EXPECT_EQ(params[0], p1.get());
	EXPECT_EQ(params[1], p2.get());
}

// ---------------------------------------------------------------------------
// Two distinct lifted stubs each `dynamic_cast` to a distinct non-null `ILiftedOperator*` (the
// `BetterFunctionMember` non-lifted-vs-lifted tiebreak compares two candidates' members; the
// test pins that two separate lifted operators are distinguishable).
// ---------------------------------------------------------------------------
TEST(ILiftedOperatorTest, TwoDistinctLiftedStubsCastToDistinctNonNull) {
	auto rt = Int();
	auto p = std::make_shared<TestParameter>(Int());
	auto stub1 = std::make_shared<LiftedMethodStub>(rt, std::vector<const IParameter*>{p.get()});
	auto stub2 = std::make_shared<LiftedMethodStub>(rt, std::vector<const IParameter*>{p.get()});
	const IParameterizedMember* asParam1 = stub1.get();
	const IParameterizedMember* asParam2 = stub2.get();
	auto* lifted1 = dynamic_cast<const ILiftedOperator*>(asParam1);
	auto* lifted2 = dynamic_cast<const ILiftedOperator*>(asParam2);
	ASSERT_NE(lifted1, nullptr);
	ASSERT_NE(lifted2, nullptr);
	EXPECT_NE(lifted1, lifted2);
}
