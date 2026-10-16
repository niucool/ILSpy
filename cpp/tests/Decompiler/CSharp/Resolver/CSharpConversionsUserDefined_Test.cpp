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

// Tests for the `CSharpConversions.UserDefinedImplicitConversion` /
// `UserDefinedExplicitConversion` helpers (CSharpConversions.cs lines 1030 / 1079, C# spec
// draft-v11 sections 10.5.4 / 10.5.5) -- the user-defined conversion resolution entry points
// that reduce the applicable operators (built by `GetApplicableConversionOperators`) to the
// most-specific source/target (`FindMostEncompassedType` / `FindMostEncompassingType`) and
// select the operator (`SelectOperator`).
//
// CRUX STUB CONVENTIONS (carried from the D529 `GetApplicableConversionOperators` test):
//  * `Host(ktc)` is an `OperatorMethodHost` -- a `LookupTypeDefinition` subclass whose
//    `GetMethods(filter)` returns a configured list (applying the filter faithfully) so the
//    operator scan finds the configured operators. `IsReferenceType` is configurable.
//  * `Def(ktc)` is a plain `LookupTypeDefinition` (no `GetMethods` override -- returns `{}`)
//    for types that declare no operators.
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetStatic` /
//    `SetIsOperator` / `SetReturnType` / `SetParameters`); kept alive in a static vector so
//    the raw `const IMethod*` the `OperatorInfo` holds outlives the call.
//  * `TestParameter(type, refKind)` is the `IParameter` stub with a configurable type.
//  * The `Nullable<T>` tests use a compilation with `System.Nullable`1` registered for
//    `FindType` (the D529 precedent), so `NullableType.Create` resolves the same definition.
//  * `StructuralEquals` is IDENTITY equality on `LookupTypeDefinition` (inherited), so the
//    encompassment identity checks match only on the SAME instance -- the tests reuse the
//    same `ITypePtr` for the operator's source/target and the from/to types.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::UserDefinedImplicit/ExplicitConversion
#include "Decompiler/Semantics/ConstantResolveResult.hpp"           // ConstantResolveResult (a non-constant sentinel)
#include "Decompiler/Semantics/ConversionFactories.hpp"             // Conversions (None / UserDefinedConversion)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"          // KnownType, ParameterizedType, GetMemberOptions
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"   // IsNullable, GetUnderlyingType
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::OperatorInfo;
using ILSpy::Decompiler::CSharp::Resolver::Detail::UserDefinedExplicitConversion;
using ILSpy::Decompiler::CSharp::Resolver::Detail::UserDefinedImplicitConversion;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

// A `LookupCompilation` with the `System.Nullable`1` definition registered for `FindType`
// (the D529 precedent -- the lifted-operator forms call `NullableType.Create`, which resolves
// the nullable definition through `compilation.FindType(KnownTypeCode.NullableOfT)`).
struct RegisteredNullable {
	LookupCompilation compilation;
	std::shared_ptr<LookupTypeDefinition> def;
};
RegisteredNullable& Registry() {
	static auto reg = [](){
		auto rp = std::make_unique<RegisteredNullable>();
		rp->def = std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
			FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
			TypeKind::Struct, Accessibility::Public, rp->compilation, nullptr,
			KnownTypeCode::NullableOfT);
		rp->compilation.RegisterKnownType(KnownTypeCode::NullableOfT, rp->def.get());
		return rp;
	}();
	return *reg;
}
LookupCompilation& Compilation() { return Registry().compilation; }
std::shared_ptr<LookupTypeDefinition> NullableDef() { return Registry().def; }

// A `LookupTypeDefinition` whose `GetMethods(filter)` returns a configured list of operators,
// applying the filter faithfully (the D529 precedent). `IsReferenceType` is configurable
// (default `nullopt`, the `LookupTypeDefinition` default); the nullable-target tests set it to
// `false` so the operator's source type is a non-nullable value type.
class OperatorMethodHost : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
	void SetIsReferenceType(std::optional<bool> v) { isRef_ = v; }
	std::optional<bool> IsReferenceType() const override { return isRef_; }
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
	std::optional<bool> isRef_;
};

// A plain `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind` (struct by default). The D514 `MakeDef` precedent;
// `GetTypeCode` resolves the `KnownTypeCode` so the encompassment numeric arms fire for
// primitive integral definitions. Used for the to-side types that declare no operators.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// An `OperatorMethodHost` (a definition whose `GetMethods` returns the configured operators).
// `IsReferenceType` defaults to `nullopt`; pass `false` for the nullable-target tests.
std::shared_ptr<OperatorMethodHost> MakeHost(KnownTypeCode ktc,
                                              std::optional<bool> isRef = std::nullopt) {
	int n = static_cast<int>(ktc);
	std::string name = "H" + std::to_string(n);
	auto h = std::make_shared<OperatorMethodHost>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
	h->SetIsReferenceType(isRef);
	return h;
}

// `Nullable<T>` over the supplied element type, built over the registered `System.Nullable`1`
// definition (the D522 `NullableOf` precedent -- the same instance `NullableType.Create`
// resolves via `FindType`, so the lifted `IdentityConversion` matches by structure).
ITypePtr NullableOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A minimal `IParameter` with a configurable type and reference kind (the D529 precedent). The
// `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same name
// for the rest of the class body (the D402 cross-scope name-hiding crux), so the enum
// references are fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
class TestParameter : public IParameter {
public:
	explicit TestParameter(ITypePtr type,
	                       ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
	                       std::string name = "p")
		: name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
	::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
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

// A `LookupMethod` kept alive in a static vector (the D529 precedent) so the raw
// `const IMethod*` the `OperatorInfo` holds outlives the call.
const IMethod* MakeMethod(std::string name) {
	static std::vector<std::shared_ptr<LookupMethod>> keep;
	auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
	keep.push_back(m);
	return m.get();
}

// A fully-configured conversion operator: static, an operator, the given name, a single
// parameter of `sourceType` (reference kind `None`), and `targetType` as the return type.
const IMethod* MakeOperator(std::string name, ITypePtr sourceType, ITypePtr targetType,
                            std::vector<std::shared_ptr<TestParameter>>* keepParams) {
	const IMethod* m = MakeMethod(std::move(name));
	auto* method = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(m));
	method->SetStatic(true);
	method->SetIsOperator(true);
	auto p = std::make_shared<TestParameter>(std::move(sourceType), ReferenceKind::None);
	keepParams->push_back(p);
	method->SetParameters({p.get()});
	method->SetReturnType(std::move(targetType));
	return m;
}

// A non-constant `ResolveResult` (the default `IsCompileTimeConstant` is false on the base
// `ResolveResult`; `ConstantResolveResult` overrides it to true, so this helper constructs a
// `ConstantResolveResult` with an empty `any` -- the tests that pass a from-result but do not
// exercise the constant-expression fallback use it; the `IsCompileTimeConstant` value is not
// load-bearing for these tests' resolution paths).
std::shared_ptr<ConstantResolveResult> NonConstResult(ITypePtr type) {
	return std::make_shared<ConstantResolveResult>(std::move(type), std::any());
}

} // namespace

// ===========================================================================
// UserDefinedImplicitConversion (CSharpConversions.cs line 1030).
// ===========================================================================

// The interface guard: user-defined conversions are not supported with interfaces. A
// `TypeKind::Interface` on EITHER side returns `None` without scanning any operators.
TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitInterfaceOnFromTypeReturnsNone) {
	auto fromType = Def(KnownTypeCode::None, TypeKind::Interface);
	auto toType = Def(KnownTypeCode::Int32);
	auto rr = NonConstResult(Def(KnownTypeCode::None, TypeKind::Interface));
	auto result = UserDefinedImplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitInterfaceOnToTypeReturnsNone) {
	auto fromType = Def(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::None, TypeKind::Interface);
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedImplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// No operators on either type -> `None` (the `operators.empty()` early-out).
TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitNoOperatorsReturnsNone) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedImplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// A single applicable op_Implicit operator whose source equals fromType and target equals
// toType: the `operators.Any(op.SourceType.Equals(fromType))` arm takes fromType directly, the
// target arm takes toType directly, `SelectOperator` finds the one match -> a valid
// `UserDefinedConversion` (not None, IsUserDefined, IsImplicit, not ambiguous).
TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitSingleMatchingOperatorResolves) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedImplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_FALSE(result->IsExplicit());
	EXPECT_TRUE(result->IsUserDefined());
	EXPECT_TRUE(result->IsValid());
}

// A null `fromResult` (the `ImplicitConversion(IType, IType)` entry has no `ResolveResult`
// context) does NOT block the identity-path resolution: the null guard in
// `GetApplicableConversionOperators` only short-circuits the constant-expression fallback,
// not the encompassment arm, so a single matching operator still resolves.
TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitNullFromResultResolvesIdentityOperator) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto result = UserDefinedImplicitConversion(Compilation(), /*fromResult*/ nullptr, *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
}

// Two applicable operators whose sources (Int64 / UInt64) neither encompass nor are
// encompassed by each other (no implicit signed<->unsigned conversion), reached from a
// `Byte` from-type (Byte converts implicitly to both Int64 and UInt64 by numeric widening).
// `operators.Any(op.SourceType.Equals(fromType=Byte))` is false (neither source is Byte), so
// `FindMostEncompassedType([Int64, UInt64])` runs -> null (ambiguous) -> the ambiguous
// `UserDefinedConversion` return (IsValid false, IsAmbiguous via the isValid computation).
TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitAmbiguousSourceReturnsAmbiguousConversion) {
	auto fromType = MakeHost(KnownTypeCode::Byte);
	auto target = Def(KnownTypeCode::Int64);
	auto sourceA = Def(KnownTypeCode::Int64);
	auto sourceB = Def(KnownTypeCode::UInt64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* opA = MakeOperator("op_Implicit", sourceA, target, &keepParams);
	const IMethod* opB = MakeOperator("op_Implicit", sourceB, target, &keepParams);
	fromType->SetMethods({opA, opB});
	auto rr = NonConstResult(Def(KnownTypeCode::Byte));
	auto result = UserDefinedImplicitConversion(Compilation(), rr.get(), *fromType, *target);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_FALSE(result->IsValid());  // the ambiguous conversion has isValid == false
}

// A `Nullable<T>` target with an operator whose target is the underlying `T`: the operator is
// applicable to `Nullable<T>` (the lifted-identity `IsEncompassedBy(T, Nullable<T>)` is true),
// `mostSpecificTarget` reduces to `T`, `SelectOperator` finds the match -> a valid
// `UserDefinedConversion`. Exercises the nullable-target path WITHOUT the recursion (the
// operator resolves directly).
TEST(CSharpConversionsUserDefinedTest, UserDefinedImplicitNullableTargetResolvesViaLiftedIdentity) {
	auto fromType = MakeHost(KnownTypeCode::Int32, /*isRef*/ std::optional<bool>(false));
	auto underlyingTarget = MakeHost(KnownTypeCode::Int64, /*isRef*/ std::optional<bool>(false));
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", fromType, underlyingTarget, &keepParams);
	fromType->SetMethods({op});
	auto toType = NullableOf(underlyingTarget);  // Nullable<long>
	auto rr = NonConstResult(NullableOf(fromType));
	auto result = UserDefinedImplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
}

// ===========================================================================
// UserDefinedExplicitConversion (CSharpConversions.cs line 1079).
// ===========================================================================

// The interface guard: a `TypeKind::Interface` on the from-side returns `None`.
TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitInterfaceOnFromTypeReturnsNone) {
	auto fromType = Def(KnownTypeCode::None, TypeKind::Interface);
	auto toType = Def(KnownTypeCode::Int32);
	auto rr = NonConstResult(Def(KnownTypeCode::None, TypeKind::Interface));
	auto result = UserDefinedExplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitInterfaceOnToTypeReturnsNone) {
	auto fromType = Def(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::None, TypeKind::Interface);
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedExplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// No operators on either type -> `None`.
TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitNoOperatorsReturnsNone) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedExplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	EXPECT_EQ(result.get(), Conversions::None().get());
}

// A single applicable op_Explicit operator whose source equals fromType and target equals
// toType: resolves to a valid `UserDefinedConversion` (IsExplicit, not ambiguous).
TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitSingleMatchingOperatorResolves) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Explicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedExplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_FALSE(result->IsImplicit());
	EXPECT_TRUE(result->IsValid());
}

// A null `fromResult` does NOT block the identity-path resolution (the null guard in
// `GetApplicableConversionOperators` only short-circuits the constant-expression fallback).
TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitNullFromResultResolvesIdentityOperator) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Explicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto result = UserDefinedExplicitConversion(Compilation(), /*fromResult*/ nullptr, *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
}

// The source-encompassing filter arm: an operator whose source (Int64) is NOT the from-type
// (Int32) but `IsEncompassedBy(Int32, Int64)` is true (numeric widening). The
// `operators.Any(op.SourceType.Equals(fromType=Int32))` check is false, so the else-arm runs;
// the filter `IsEncompassedBy(fromType, op.SourceType)` is true -> `FindMostEncompassedType`
// over the filtered set -> Int64 -> `mostSpecificSource = Int64`. The operator then resolves
// via `SelectOperator`.
TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitSourceEncompassingFilterResolves) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	auto operatorSource = Def(KnownTypeCode::Int64);  // Int32 -> Int64 implicit widening
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Explicit", operatorSource, toType, &keepParams);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = UserDefinedExplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
}

// The `A? -> A -> B` recursion: a `Nullable<T>` from-type with an operator on the underlying
// `T`'s method table (source=T, target=B). `GetApplicableConversionOperators` finds the
// operator (applicable via `IsEncompassingOrEncompassedBy(Nullable<T>, T)` = true); the
// source-reduction's `operators.Any(op.SourceType.Equals(Nullable<T>))` is false, the filter
// `IsEncompassedBy(Nullable<T>, T)` is false (Nullable<T>->T is explicit), so the fallback
// `FindMostEncompassingType([T])` -> T -> `mostSpecificSource = T`; the target arm takes `toType`
// directly; `SelectOperator(T, toType, ...)` finds the match -> resolves. Exercises the
// `Nullable<T>` from-type path (the `A? -> A -> B` recursion fires only when `SelectOperator`
// returns None; here it resolves directly, but the `Nullable<T>`-from-type source-reduction
// path is exercised).
TEST(CSharpConversionsUserDefinedTest, UserDefinedExplicitNullableFromTypeResolvesViaUnderlyingSource) {
	auto underlyingFrom = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Explicit", underlyingFrom, toType, &keepParams);
	underlyingFrom->SetMethods({op});
	auto fromType = NullableOf(underlyingFrom);  // Nullable<int>
	auto rr = NonConstResult(NullableOf(underlyingFrom));
	auto result = UserDefinedExplicitConversion(Compilation(), rr.get(), *fromType, *toType);
	ASSERT_NE(result.get(), Conversions::None().get());
	EXPECT_TRUE(result->IsExplicit());
	EXPECT_TRUE(result->IsValid());
}
