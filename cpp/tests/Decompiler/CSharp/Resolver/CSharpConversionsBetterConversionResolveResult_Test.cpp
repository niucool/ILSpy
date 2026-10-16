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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// TDD for `Detail::BetterConversion(ResolveResult, IType, IType)` (CSharpConversions.cs line 1540,
// the "better conversion from expression", C# 8.0 spec section 12.6.4.5) and the public
// `CSharpConversions::BetterConversion(ResolveResult, IType, IType)` entry that delegates to it.
//
// The dispatch arms (in order): the exactly-matching tiebreak (`IsExactlyMatching` D543), the
// implicit-span tiebreak (`IsImplicitSpanConversion` D538, fired only when neither target exactly
// matches), the `BetterConversionTarget` (D535) fallback (when both or neither exactly match),
// and the lambda arm (delegate `Invoke` signature comparison -- the parameter-count / per-
// parameter-type / HasParameterList-count guards, the void-return tiebreak, the inferred-return
// `BetterConversion` recursion, and the async `Task<T>` unpack+recompute).
//
// Test stubs (carried from the D534 `AnonymousFunctionConversion` / D543 `IsExactlyMatching` /
// D538 `IsImplicitSpanConversion` tests, each test file carries its own anonymous-namespace
// stubs -- the established convention):
//   * `SpanCompilation` is a `LookupCompilation` subclass overriding `TypeSystemOptions()` to
//    return `FirstClassSpanTypes` (the D538 precedent). It is used for ALL tests: the span arm
//    fires only for span-shaped fromTypes (array/Span/ReadOnlySpan/string), so the non-span tests
//    (int/long/byte/NoType fromTypes) are unaffected by the flag -- `IsImplicitSpanConversion`
//    returns false for them. Using one compilation for all tests keeps the stubs consistent.
//   * `MethodHostType` is a `LookupTypeDefinition` subclass whose `GetMethods(filter, options)`
//    returns a configured list, applying the filter faithfully (the D533 precedent). The delegate
//    type is a `MethodHostType` with `TypeKind::Delegate`.
//   * `TestParameter` is a minimal `IParameter` with a configurable type and reference kind (the
//    D524/D531 precedent; the `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope
//    enums of the same name, so the enum references are fully-qualified).
//   * `TestLambda` is a `LambdaResolveResult` subclass with a configurable `GetInferredReturnType`
//    (returns the `returnType` field) and configurable `IsAnonymousMethod` / `IsAsync` /
//    `HasParameterList` / `Parameters` flags -- the D543 `TestLambda` precedent.
//   * `TaskOf(element, name)` builds `Task<T>` as a `ParameterizedType` over a `Task`1`
//    `LookupTypeDefinition` (`KnownTypeCode::TaskOfT`, arity 1). The `name` parameter allows
//    constructing DISTINCT `Task`1` definitions (the D543 async-unpack precedent) so the Task-
//    level identity fails (distinct defs) while the unwrapped element identity may succeed.
//   * `ExpressionOf(element)` builds `Expression<T>` (the D534 precedent).
//   * `SpanOf` / `ReadOnlySpanOf` / `MakeArray` build the span/array types (the D538 precedent).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public BetterConversion)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::BetterConversion (ResolveResult + IType overloads)
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"         // LambdaResolveResult (the TestLambda base) + LambdaConversion
#include "Decompiler/Semantics/Conversion.hpp"                        // Conversion (the TestLambda IsValid return base)
#include "Decompiler/Semantics/ResolveResult.hpp"                     // ResolveResult (the dispatch parameter)
#include "Decompiler/Semantics/TypeResolveResult.hpp"                 // TypeResolveResult (the non-lambda resolve-result stub)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::LambdaConversion;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupCompilation` whose `TypeSystemOptions()` carries `FirstClassSpanTypes` (the base
// returns `None`, which fails the flag gate). The member function `TypeSystemOptions()` shares
// its name with the `TypeSystemOptions` enum type (the C# idiom the D472 port hit); the member
// hides the enum in the class body, so the return type and the body are fully-qualified with
// `TS::` (the D538 precedent). Used for ALL tests: the span arm fires only for span-shaped
// fromTypes, so the non-span tests are unaffected.
class SpanCompilation : public LookupCompilation {
public:
	TS::TypeSystemOptions TypeSystemOptions() const override {
		return TS::TypeSystemOptions::FirstClassSpanTypes;
	}
};

SpanCompilation& Compilation() {
	static SpanCompilation c;
	return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind`. The D514 `MakeDef` precedent; `StructuralEquals` is
// IDENTITY equality, so the identity-conversion crux cases reuse the SAME instance.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// A `LookupTypeDefinition` subclass whose `GetMethods(filter, options)` returns a configured list,
// applying the filter faithfully (the D533 `MethodHostType` precedent).
class MethodHostType : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

	std::vector<const IMethod*> GetMethods(
		std::function<bool(const IMethod*)> filter = nullptr,
		GetMemberOptions options = GetMemberOptions::None) const override
	{
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

// A `MethodHostType` with the supplied `TypeKind` (Delegate by default). The D533 `MakeHost`
// precedent.
std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
	return std::make_shared<MethodHostType>(
		std::move(name), "",
		FullTypeName(TopLevelTypeName("", std::move(name), 0)),
		kind, Accessibility::Public, Compilation(), nullptr);
}

// A minimal `IParameter` with a configurable type and reference kind. The
// `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same name
// (the D402 cross-scope name-hiding crux), so the enum references are fully-qualified. The
// D524/D531/D534 `TestParameter` precedent.
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
	const IParameterizedMember* Owner() const override { return nullptr; }
	LifetimeAnnotation Lifetime() const override { return {}; }
private:
	std::string name_;
	ITypePtr type_;
	::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `LookupMethod` kept alive in a static vector so the raw `const IMethod*` outlives the call.
const IMethod* MakeMethod(std::string name = "M") {
	static std::vector<std::shared_ptr<LookupMethod>> keep;
	auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
	keep.push_back(m);
	return m.get();
}

// A `TestParameter` kept alive in a static vector so the raw `const IParameter*` the method holds
// outlives the call.
std::shared_ptr<TestParameter> MakeParam(ITypePtr type, ReferenceKind rk = ReferenceKind::None) {
	static std::vector<std::shared_ptr<TestParameter>> keep;
	auto p = std::make_shared<TestParameter>(std::move(type), rk);
	keep.push_back(p);
	return p;
}

// Configure a method: set its parameters (from the kept `TestParameter` shared_ptrs) and its
// return type. Returns the same raw `const IMethod*` for chaining. The D531/D534 `Configure`
// precedent.
const IMethod* Configure(const IMethod* method, const std::vector<std::shared_ptr<TestParameter>>& params,
                         ITypePtr returnType) {
	auto* m = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(method));
	std::vector<const IParameter*> paramPtrs;
	for (const auto& p : params)
		paramPtrs.push_back(p.get());
	m->SetParameters(std::move(paramPtrs));
	m->SetReturnType(std::move(returnType));
	return method;
}

// A `LookupTypeDefinition` for the `System.Linq.Expressions.Expression`1` generic definition (the
// expression-tree wrapper). The D534 `ExpressionDef` precedent.
std::shared_ptr<LookupTypeDefinition> ExpressionDef() {
	return std::make_shared<LookupTypeDefinition>(
		"Expression", "System.Linq.Expressions",
		FullTypeName(TopLevelTypeName("System.Linq.Expressions", "Expression", 1)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
}

// `Expression<T>` over the supplied element type (the D534 `ExpressionOf` precedent).
ITypePtr ExpressionOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(ExpressionDef(), std::vector<ITypePtr>{std::move(element)});
}

// A `LookupTypeDefinition` for the `System.Threading.Tasks.Task`1` generic definition (the async
// wrapper). The `name` parameter allows constructing DISTINCT `Task`1` definitions (the D543
// async-unpack precedent) so the Task-level identity fails (distinct defs) while the unwrapped
// element identity may succeed. Arity 1, `KnownTypeCode::TaskOfT`.
std::shared_ptr<LookupTypeDefinition> TaskOfTDef(const std::string& name = "Task`1") {
	return std::make_shared<LookupTypeDefinition>(
		name, "System.Threading.Tasks",
		FullTypeName(TopLevelTypeName("System.Threading.Tasks", name, 1)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::TaskOfT);
}

// `Task<T>` over the supplied element type, using a `Task`1` definition with the supplied name
// (the D543 precedent).
ITypePtr TaskOf(ITypePtr element, const std::string& name = "Task`1") {
	return std::make_shared<ParameterizedType>(TaskOfTDef(name), std::vector<ITypePtr>{std::move(element)});
}

// The `System.Span`1` generic definition (a struct, `KnownTypeCode::SpanOfT`). The D538 precedent.
std::shared_ptr<LookupTypeDefinition> SpanDef() {
	static auto d = std::make_shared<LookupTypeDefinition>("Span`1", "System",
		FullTypeName(TopLevelTypeName("System", "Span`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::SpanOfT);
	return d;
}

// The `System.ReadOnlySpan`1` generic definition (a struct, `KnownTypeCode::ReadOnlySpanOfT`).
std::shared_ptr<LookupTypeDefinition> ReadOnlySpanDef() {
	static auto d = std::make_shared<LookupTypeDefinition>("ReadOnlySpan`1", "System",
		FullTypeName(TopLevelTypeName("System", "ReadOnlySpan`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::ReadOnlySpanOfT);
	return d;
}

// `Span<T>` over the supplied element type (the D538 precedent).
ITypePtr SpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(SpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// `ReadOnlySpan<T>` over the supplied element type (the D538 precedent).
ITypePtr ReadOnlySpanOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(ReadOnlySpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// A single-dimensional array (`ArrayType` SZArray ctor, rank 1) over the supplied element type.
ITypePtr MakeArray(ITypePtr element) {
	return std::make_shared<ArrayType>(std::move(element));
}

// The concrete `LambdaResolveResult` test subclass with a CONFIGURABLE `GetInferredReturnType`
// (returns the `returnType` field) and configurable `IsAnonymousMethod` / `IsAsync` /
// `HasParameterList` / `Parameters` flags. The D543 `TestLambda` precedent; `IsValid` is unused by
// `BetterConversion` but the abstract base requires an override, so it returns a fresh
// `LambdaConversion`.
class TestLambda : public LambdaResolveResult {
public:
	bool hasParameterList = true;
	bool isAnonymousMethod = false;
	bool isImplicitlyTyped = true;
	bool isAsync = false;
	ITypePtr returnType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
	std::vector<const IParameter*> parameters;
	std::shared_ptr<ResolveResult> body =
		std::make_shared<Sem::TypeResolveResult>(
			std::make_shared<TS::KnownType>(KnownTypeCode::Void));

	bool HasParameterList() const override { return hasParameterList; }
	bool IsAnonymousMethod() const override { return isAnonymousMethod; }
	bool IsImplicitlyTyped() const override { return isImplicitlyTyped; }
	bool IsAsync() const override { return isAsync; }
	ITypePtr GetInferredReturnType(const std::vector<ITypePtr>&) const override { return returnType; }
	std::vector<const IParameter*> Parameters() const override { return parameters; }
	const IType& ReturnType() const override { return *returnType; }
	std::shared_ptr<Conversion> IsValid(const std::vector<ITypePtr>&,
		const ITypePtr&,
		CSharpConversions&) const override
	{
		return std::make_shared<LambdaConversion>();
	}
	ResolveResult& Body() const override { return *body; }
	std::unique_ptr<ResolveResult> ShallowClone() const override
	{
		return std::make_unique<TestLambda>(*this);
	}
protected:
	std::string ClassName() const override { return "TestLambda"; }
};

// Build a delegate type (a `MethodHostType` with `TypeKind::Delegate`) whose `Invoke` method has
// the supplied parameter types and return type. Returns the delegate type (the `Invoke` method
// is kept alive in the `MakeMethod` static vector). The D534 `MakeDelegate` precedent.
std::shared_ptr<MethodHostType> MakeDelegate(ITypePtr returnType,
	const std::vector<std::shared_ptr<TestParameter>>& params = {}) {
	const IMethod* invoke = Configure(MakeMethod("Invoke"), params, std::move(returnType));
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke});
	return delegateType;
}

using ILSpy::Decompiler::CSharp::Resolver::Detail::BetterConversion;

} // namespace

// ===========================================================================
// Exactly-matching tiebreak (CSharpConversions.cs lines 1542-1545).
// ===========================================================================

// A non-lambda expression whose type exactly matches `t1` (identity, same instance) but not `t2`
// -> `t1` is the better target (return 1).
TEST(CSharpConversionsBetterConversionResolveResultTest, T1ExactBeatsT2NonExact)
{
	auto intT = Def(KnownTypeCode::Int32);
	auto longT = Def(KnownTypeCode::Int64);
	auto rr = std::make_shared<Sem::TypeResolveResult>(intT);
	EXPECT_EQ(BetterConversion(Compilation(), *rr, *intT, *longT), 1);
}

// The mirror: the expression exactly matches `t2` but not `t1` -> `t2` is better (return 2).
TEST(CSharpConversionsBetterConversionResolveResultTest, T2ExactBeatsT1NonExact)
{
	auto intT = Def(KnownTypeCode::Int32);
	auto longT = Def(KnownTypeCode::Int64);
	auto rr = std::make_shared<Sem::TypeResolveResult>(intT);
	EXPECT_EQ(BetterConversion(Compilation(), *rr, *longT, *intT), 2);
}

// Both targets exactly match (the same instance) -> falls to `BetterConversionTarget(int, int)`
// (0) and the non-lambda else branch (`BetterConversion(int, int, int)` = 0) -> return 0.
TEST(CSharpConversionsBetterConversionResolveResultTest, BothExactNonLambdaReturnsZero)
{
	auto intT = Def(KnownTypeCode::Int32);
	auto rr = std::make_shared<Sem::TypeResolveResult>(intT);
	EXPECT_EQ(BetterConversion(Compilation(), *rr, *intT, *intT), 0);
}

// Neither target exactly matches -> the implicit-span tiebreak does not fire (the fromType `byte`
// is not array/span/string) -> `BetterConversionTarget(int, uint)` fires (the signed integral
// tiebreak: `Int32` beats `UInt32`) -> return 1. The CRUX: the non-lambda dispatch delegates to
// `BetterConversionTarget` when neither target exactly matches.
TEST(CSharpConversionsBetterConversionResolveResultTest, NeitherExactNonLambdaDelegatesToBetterConversionTarget)
{
	auto byteT = Def(KnownTypeCode::Byte);
	auto intT = Def(KnownTypeCode::Int32);
	auto uintT = Def(KnownTypeCode::UInt32);
	auto rr = std::make_shared<Sem::TypeResolveResult>(byteT);
	EXPECT_EQ(BetterConversion(Compilation(), *rr, *intT, *uintT), 1);
}

// ===========================================================================
// Implicit-span tiebreak (CSharpConversions.cs lines 1550-1557).
// ===========================================================================

// An `int[]` expression (neither target exactly matches): `int[] -> Span<int>` is an implicit span
// conversion (array-to-Span by element identity, `FirstClassSpanTypes`), but `int[] -> long` is
// not -> `t1` is better (return 1). The CRUX: the span tiebreak fires only when neither target
// exactly matches and the `FirstClassSpanTypes` flag is set.
TEST(CSharpConversionsBetterConversionResolveResultTest, NeitherExactSpanT1BeatsT2)
{
	auto intT = Def(KnownTypeCode::Int32);
	auto longT = Def(KnownTypeCode::Int64);
	auto intArray = MakeArray(ITypePtr(intT));
	auto rr = std::make_shared<Sem::TypeResolveResult>(intArray);
	auto span = SpanOf(ITypePtr(intT));
	EXPECT_EQ(BetterConversion(Compilation(), *rr, *span, *longT), 1);
}

// The mirror: `t2` is the span conversion, `t1` is not -> `t2` is better (return 2).
TEST(CSharpConversionsBetterConversionResolveResultTest, NeitherExactSpanT2BeatsT1)
{
	auto intT = Def(KnownTypeCode::Int32);
	auto longT = Def(KnownTypeCode::Int64);
	auto intArray = MakeArray(ITypePtr(intT));
	auto rr = std::make_shared<Sem::TypeResolveResult>(intArray);
	auto span = SpanOf(ITypePtr(intT));
	EXPECT_EQ(BetterConversion(Compilation(), *rr, *longT, *span), 2);
}

// ===========================================================================
// Lambda arm -- the delegate `Invoke` resolution guards.
// ===========================================================================

// A lambda whose targets are NOT delegates (a `Class`, no `Invoke`) -> `GetDelegateInvokeMethod`
// yields null -> return 0. (Neither target exactly matches: the lambda's `NoType` does not
// identity-match a `Class`; `BetterConversionTarget(Class, Class)` is 0; the lambda arm then
// resolves null invoke methods.)
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaNonDelegateTargetReturns0)
{
	auto lambda = std::make_shared<TestLambda>();
	auto t1 = MakeHost("C1", TypeKind::Class);
	auto t2 = MakeHost("C2", TypeKind::Class);
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *t1, *t2), 0);
}

// A lambda whose delegate targets take a different number of parameters -> return 0.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaParamCountMismatchReturns0)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto param = MakeParam(ITypePtr(intA));
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intB;  // GetInferredReturnType -> intB (distinct from the delegates' returns)
	auto d1 = MakeDelegate(ITypePtr(intB), {param});   // Invoke: 1 param
	auto d2 = MakeDelegate(ITypePtr(intB), {});        // Invoke: 0 params
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 0);
}

// A lambda whose delegate targets take the same number of parameters but the parameter types
// differ -> the per-parameter `.Equals` check fails -> return 0.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaParamTypeMismatchReturns0)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto longB = Def(KnownTypeCode::Int64);
	auto pInt = MakeParam(ITypePtr(intA));
	auto pLong = MakeParam(ITypePtr(longB));
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intB;
	auto d1 = MakeDelegate(ITypePtr(intB), {pInt});
	auto d2 = MakeDelegate(ITypePtr(intB), {pLong});
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 0);
}

// A lambda with an explicit parameter list whose length differs from the delegate's `Invoke`
// parameter count -> return 0.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaHasParameterListCountMismatchReturns0)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto lambdaParam = MakeParam(ITypePtr(intA));
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intB;
	lambda->hasParameterList = true;
	lambda->parameters = {lambdaParam.get()};  // lambda lists 1 parameter
	auto d1 = MakeDelegate(ITypePtr(intB), {});  // Invoke: 0 params
	auto d2 = MakeDelegate(ITypePtr(intB), {});
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 0);
}

// ===========================================================================
// Lambda arm -- the expression-tree unwrap (`!IsAnonymousMethod` guard).
// ===========================================================================

// A non-anonymous-method lambda converting to `Expression<D1>` / `Expression<D2>` unwraps the
// `Expression<T>` wrapper before resolving the `Invoke` methods. With the unwrap: `m1` / `m2`
// resolve, `ret1=int` / `ret2=long`, the inferred return `int` converts better to `int` (the
// `BetterConversionTarget` core check: `int -> long` is implicit, `long -> int` is not) -> return
// 1. Without the unwrap, `GetDelegateInvokeMethod(Expression<D>)` would yield null -> return 0.
// The CRUX: the expression-tree unwrap is load-bearing.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaUnwrapsExpressionTreeBeforeInvokeResolution)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto longB = Def(KnownTypeCode::Int64);
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intA;  // GetInferredReturnType -> intA (distinct from the delegates' returns)
	lambda->isAnonymousMethod = false;
	auto d1 = MakeDelegate(ITypePtr(intB), {});   // Invoke returns intB
	auto d2 = MakeDelegate(ITypePtr(longB), {});  // Invoke returns longB
	auto exprD1 = ExpressionOf(ITypePtr(d1));
	auto exprD2 = ExpressionOf(ITypePtr(d2));
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *exprD1, *exprD2), 1);
}

// ===========================================================================
// Lambda arm -- the void-return tiebreak (CSharpConversions.cs lines 1591-1594).
// ===========================================================================

// `ret1` is void and `ret2` is non-void -> the non-void target is better -> return 2.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaRet1VoidRet2NonVoidReturns2)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto voidT = std::make_shared<TS::KnownType>(KnownTypeCode::Void);
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intA;
	auto d1 = MakeDelegate(ITypePtr(voidT), {});  // Invoke returns void
	auto d2 = MakeDelegate(ITypePtr(intB), {});   // Invoke returns int
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 2);
}

// `ret1` is non-void and `ret2` is void -> `t1` is better -> return 1.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaRet1NonVoidRet2VoidReturns1)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto voidT = std::make_shared<TS::KnownType>(KnownTypeCode::Void);
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intA;
	auto d1 = MakeDelegate(ITypePtr(intB), {});   // Invoke returns int
	auto d2 = MakeDelegate(ITypePtr(voidT), {});  // Invoke returns void
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 1);
}

// ===========================================================================
// Lambda arm -- the inferred-return `BetterConversion` recursion (line 1595).
// ===========================================================================

// The lambda's inferred return type (`intA`) does not exactly match either delegate return
// (`intB` / `longB`, both distinct instances), so neither exactly-matching arm fires; the lambda
// arm then computes `r = BetterConversion(intA, intB, longB)` -- `int` -> `int` is identity but
// `int` -> `long` is not... wait, `intB` is distinct from `intA` so identity is false on both
// sides; the verdict falls to `BetterConversionTarget(intB, longB)` = 1 (`int -> long` implicit,
// `long -> int` not) -> return 1. The CRUX: the lambda arm delegates to the IType
// `BetterConversion` overload for the inferred-return comparison.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaInferredReturnBetterConversionReturns1)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto longB = Def(KnownTypeCode::Int64);
	auto lambda = std::make_shared<TestLambda>();
	lambda->returnType = intA;  // GetInferredReturnType -> intA (distinct from intB / longB)
	auto d1 = MakeDelegate(ITypePtr(intB), {});   // Invoke returns intB
	auto d2 = MakeDelegate(ITypePtr(longB), {});  // Invoke returns longB
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 1);
}

// ===========================================================================
// Lambda arm -- the async `Task<T>` unpack+recompute (lines 1597-1607).
// ===========================================================================

// An async lambda whose inferred-return `BetterConversion` is already non-zero (1): the async
// unpack is SKIPPED (the `r == 0` guard), so the result is the first verdict (1). The CRUX: the
// async unpack is guarded on `r == 0`.
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaAsyncSkipsUnpackWhenFirstResultNonZero)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto intB = Def(KnownTypeCode::Int32);
	auto longB = Def(KnownTypeCode::Int64);
	auto lambda = std::make_shared<TestLambda>();
	lambda->isAsync = true;
	lambda->returnType = intA;  // GetInferredReturnType -> intA (not a Task -> the async unpack yields null)
	auto d1 = MakeDelegate(ITypePtr(intB), {});   // Invoke returns intB
	auto d2 = MakeDelegate(ITypePtr(longB), {});  // Invoke returns longB
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 1);
}

// An async lambda whose inferred-return `BetterConversion` is 0 (all three are `Task<T>` with
// distinct defs so the Task-level identity fails, and the unwrapped elements `int` / `bool` /
// `long` do not convert): the async unpack fires (all three unpack to non-null), the recompute
// runs, and the result stays 0. This exercises the async unpack code path (the `UnpackTask` calls,
// the null guard, the recompute) without crashing. (The async unpack is defensive: the exactly-
// matching arm already captures the unpacked identity via `IsExactlyMatching`'s own async unpack,
// so the unpack cannot change a 0 to a non-zero identity verdict here.)
TEST(CSharpConversionsBetterConversionResolveResultTest, LambdaAsyncUnpacksAndRecomputesWhenFirstResultZero)
{
	auto intA = Def(KnownTypeCode::Int32);
	auto longA = Def(KnownTypeCode::Int64);
	auto boolB = Def(KnownTypeCode::Boolean);
	auto taskLongA = TaskOf(ITypePtr(longA), "TaskA`1");
	auto taskIntB = TaskOf(ITypePtr(intA), "TaskB`1");
	auto taskBoolB = TaskOf(ITypePtr(boolB), "TaskB`1");
	auto lambda = std::make_shared<TestLambda>();
	lambda->isAsync = true;
	lambda->returnType = taskLongA;  // GetInferredReturnType -> Task<long>(A)
	auto d1 = MakeDelegate(ITypePtr(taskIntB), {});   // Invoke returns Task<int>(B)
	auto d2 = MakeDelegate(ITypePtr(taskBoolB), {});   // Invoke returns Task<bool>(B)
	EXPECT_EQ(BetterConversion(Compilation(), *lambda, *d1, *d2), 0);
}

// ===========================================================================
// Public method delegation.
// ===========================================================================

// The public `CSharpConversions::BetterConversion(ResolveResult, IType, IType)` delegates to the
// `Detail::` free function (threading the instance's compilation). The exactly-matching crux
// (`T1ExactBeatsT2NonExact`) reproduced via the public method -> return 1.
TEST(CSharpConversionsBetterConversionResolveResultTest, PublicMethodDelegatesToDetail)
{
	CSharpConversions conversions(Compilation());
	auto intT = Def(KnownTypeCode::Int32);
	auto longT = Def(KnownTypeCode::Int64);
	auto rr = std::make_shared<Sem::TypeResolveResult>(intT);
	EXPECT_EQ(conversions.BetterConversion(*rr, *intT, *longT), 1);
}
