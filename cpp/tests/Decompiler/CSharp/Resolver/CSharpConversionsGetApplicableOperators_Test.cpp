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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for the `CSharpConversions.GetApplicableConversionOperators` helper
// (CSharpConversions.cs line 1167) -- the heavy helper that builds the list of applicable
// user-defined conversion operators. It scans the method tables of
// `UnderlyingTypeForConversion(fromType)` and `UnderlyingTypeForConversion(toType)` for the
// static single-parameter conversion operators (op_Implicit for implicit, op_Implicit OR
// op_Explicit for explicit), dedups by `IMethod` pointer identity, then for each candidate
// determines applicability via the encompassment helpers (`IsEncompassedBy` for implicit,
// `IsEncompassingOrEncompassedBy` for explicit) with a constant-expression fallback, and
// additionally builds a LIFTED form (`Nullable<T>` source / target) for non-nullable value-type
// operators.
//
// CRUX STUB CONVENTIONS (carried from the D514-D528 tests):
//  * `Host(ktc)` is an `OperatorMethodHost` -- a `LookupTypeDefinition` subclass whose
//    `GetMethods(filter)` returns a configured list (applying the filter, faithful to
//    `GetMethodsImpl`) so the operator scan finds the configured operators. Its
//    `IsReferenceType` is configurable (default `nullopt`, the `LookupTypeDefinition`
//    default); the lifted tests set it to `false` so `IsNonNullableValueType` recognizes the
//    operator's source type as a non-nullable value type. `StructuralEquals` is IDENTITY
//    equality (inherited), so the encompassment identity checks match only on the SAME
//    instance -- the tests reuse the same `ITypePtr` for the operator's source/target and
//    the from/to types.
//  * `Def(ktc)` is a plain `LookupTypeDefinition` (no `GetMethods` override -- returns `{}`,
//    the `AbstractType` default) for types that declare no operators (the to-side in most
//    tests).
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetStatic` /
//    `SetIsOperator` / `SetReturnType` / `SetParameters`); kept alive in a static vector so
//    the raw `const IMethod*` the `OperatorInfo` holds outlives the call.
//  * `TestParameter(type, refKind)` is the `IParameter` stub with a configurable type and
//    reference kind (the `ref In` unwrap crux).
//  * The `Nullable<T>` tests use a compilation with `System.Nullable`1` registered for
//    `FindType` (the D529 `NullableType.Create` precedent), so `Create` resolves the same
//    definition the test's `Nullable<T>` is built over -- the lifted `IdentityConversion`
//    then matches by structure.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::GetApplicableConversionOperators, Detail::OperatorInfo
#include "Decompiler/Semantics/ConstantResolveResult.hpp"           // ConstantResolveResult (the constant-expression fallback)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"          // KnownType, ParameterizedType, ByReferenceType, GetMemberOptions
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

using ILSpy::Decompiler::CSharp::Resolver::Detail::GetApplicableConversionOperators;
using ILSpy::Decompiler::CSharp::Resolver::Detail::OperatorInfo;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
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

// A `LookupCompilation` with the `System.Nullable`1` definition registered for `FindType`, paired
// with the owning shared_ptr. The lifted-operator forms call `NullableType.Create`, which
// resolves the nullable definition through `compilation.FindType(KnownTypeCode.NullableOfT)`, so
// the compilation must have the definition registered (the D529 precedent). `LookupCompilation`
// is non-copyable / non-movable (its `LookupModule` member holds a `const ICompilation&`
// reference to `*this`), so the struct is heap-allocated via `make_unique` and kept alive for the
// whole program (a function-local static): the pointee is never moved, so the `mainModule_`
// reference stays valid.
struct RegisteredNullable {
	LookupCompilation compilation;
	std::shared_ptr<LookupTypeDefinition> def;
};
RegisteredNullable& Registry() {
	static auto reg = []{
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
// applying the filter faithfully (the C# `GetMethodsImpl` runs the predicate over the method
// table). `IsReferenceType` is configurable (default `nullopt`, the `LookupTypeDefinition`
// default); the lifted tests set it to `false` so the operator's source type is a non-nullable
// value type. The `using LookupTypeDefinition::LookupTypeDefinition;` inherits the base ctor.
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

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind` (struct by default). The D514 `MakeDef` precedent;
// `GetTypeCode` resolves the `KnownTypeCode` so the encompassment numeric arms fire for
// primitive integral definitions. Used for the to-side types that declare no operators (the
// `AbstractType` default `GetMethods` returns `{}`).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// An `OperatorMethodHost` (a value-type definition whose `GetMethods` returns the configured
// operators). `IsReferenceType` defaults to `nullopt`; pass `false` for the lifted tests.
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
// definition (the same instance `NullableType.Create` resolves via `FindType`, so the lifted
// `IdentityConversion` matches by structure). The D522 `NullableOf` precedent.
ITypePtr NullableOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// A minimal `IParameter` with a configurable type and reference kind (the `ref In` unwrap crux).
// The `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same
// name for the rest of the class body (the D402 / CheckApplicability_Test cross-scope name-hiding
// crux), so the enum references are fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
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

// A `LookupMethod` kept alive in a static vector (the `AddMembers_Test` precedent) so the raw
// `const IMethod*` the `OperatorInfo` holds outlives the call. The `IsStatic` / `IsOperator` /
// `ReturnType` / `Parameters` are configured via the additive setters.
const IMethod* MakeMethod(std::string name) {
	static std::vector<std::shared_ptr<LookupMethod>> keep;
	auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
	keep.push_back(m);
	return m.get();
}

// A fully-configured conversion operator: static, an operator, the given name, a single
// parameter of `sourceType` (reference kind `None`), and `targetType` as the return type.
const IMethod* MakeOperator(std::string name, ITypePtr sourceType, ITypePtr targetType,
                            std::vector<std::shared_ptr<TestParameter>>* keepParams,
                            ReferenceKind refKind = ReferenceKind::None) {
	const IMethod* m = MakeMethod(std::move(name));
	auto* method = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(m));
	method->SetStatic(true);
	method->SetIsOperator(true);
	auto p = std::make_shared<TestParameter>(std::move(sourceType), refKind);
	keepParams->push_back(p);
	method->SetParameters({p.get()});
	method->SetReturnType(std::move(targetType));
	return m;
}

// A non-constant `ResolveResult` (the default `IsCompileTimeConstant` is false) over `int`, for
// the tests that pass a from-result but do not exercise the constant-expression fallback.
std::shared_ptr<ConstantResolveResult> NonConstResult(ITypePtr type) {
	return std::make_shared<ConstantResolveResult>(std::move(type), std::any());
}

} // namespace

// ===========================================================================
// GetApplicableConversionOperators (CSharpConversions.cs line 1167).
// ===========================================================================

// No operators on either type -> empty result.
TEST(CSharpConversionsGetApplicableOperatorsTest, EmptyWhenNoOperators) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	EXPECT_TRUE(result.empty());
}

// A single applicable op_Implicit operator (implicit) yields one non-lifted OperatorInfo.
TEST(CSharpConversionsGetApplicableOperatorsTest, ReturnsSingleNonLiftedImplicitOperator) {
	auto fromType = MakeHost(KnownTypeCode::Int32);            // the operator's source
	auto toType = Def(KnownTypeCode::Int64);                   // the operator's target
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
	EXPECT_FALSE(result[0].IsLifted);
	EXPECT_EQ(result[0].SourceType.get(), static_cast<const IType*>(fromType.get()));
	EXPECT_EQ(result[0].TargetType.get(), toType.get());
}

// A single applicable op_Explicit operator (explicit) yields one non-lifted OperatorInfo.
TEST(CSharpConversionsGetApplicableOperatorsTest, ReturnsSingleNonLiftedExplicitOperator) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Explicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ true);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
	EXPECT_FALSE(result[0].IsLifted);
}

// The implicit filter accepts ONLY op_Implicit, so an op_Explicit operator is filtered out.
TEST(CSharpConversionsGetApplicableOperatorsTest, ImplicitFilterRejectsOpExplicit) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Explicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	EXPECT_TRUE(result.empty());
}

// The explicit filter accepts BOTH op_Implicit and op_Explicit, so two distinct applicable
// operators yield two OperatorInfo entries.
TEST(CSharpConversionsGetApplicableOperatorsTest, ExplicitFilterAcceptsBothOpImplicitAndOpExplicit) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = Def(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* opImp = MakeOperator("op_Implicit", fromType, toType, &keepParams);
	const IMethod* opExp = MakeOperator("op_Explicit", fromType, toType, &keepParams);
	fromType->SetMethods({opImp, opExp});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ true);
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result[0].Method, opImp);
	EXPECT_EQ(result[1].Method, opExp);
}

// The same operator method appearing in BOTH the from-type and the to-type method tables is
// deduplicated (pointer identity) -- one OperatorInfo, not two.
TEST(CSharpConversionsGetApplicableOperatorsTest, DeduplicatesOperatorAppearingOnBothHosts) {
	auto fromType = MakeHost(KnownTypeCode::Int32);
	auto toType = MakeHost(KnownTypeCode::Int64);
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", fromType, toType, &keepParams);
	fromType->SetMethods({op});
	toType->SetMethods({op});   // the SAME method on the to-side too
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
}

// A `ref In` parameter unwraps to its element type when the from-side is not itself by-ref: the
// operator's source type is the element (NOT the by-ref wrapper).
TEST(CSharpConversionsGetApplicableOperatorsTest, RefInParameterUnwrappedToElement) {
	auto fromType = MakeHost(KnownTypeCode::Int32);            // the operator's source element
	auto toType = Def(KnownTypeCode::Int64);
	auto byRefSource = std::make_shared<ByReferenceType>(fromType);  // `ref In int`
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", byRefSource, toType, &keepParams, ReferenceKind::In);
	fromType->SetMethods({op});
	auto rr = NonConstResult(Def(KnownTypeCode::Int32));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
	EXPECT_FALSE(result[0].IsLifted);
	// The source type is the unwrapped element (the from-type instance), NOT the by-ref wrapper.
	EXPECT_EQ(result[0].SourceType.get(), static_cast<const IType*>(fromType.get()));
	EXPECT_NE(result[0].SourceType.get(), static_cast<const IType*>(byRefSource.get()));
}

// The constant-expression fallback: when `IsEncompassedBy(fromType, sourceType)` is false but
// `ImplicitConstantExpressionConversion(fromResult, sourceType)` is true (a constant `5` of
// type `int` converts to `byte`), the operator is still applicable.
TEST(CSharpConversionsGetApplicableOperatorsTest, ConstantExpressionFallbackMakesOperatorApplicable) {
	auto fromType = MakeHost(KnownTypeCode::Int32);            // int -- not implicitly convertible to byte
	auto sourceType = Def(KnownTypeCode::Byte);                // byte -- the operator's source
	auto toType = Def(KnownTypeCode::Int64);                   // the operator's target
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", sourceType, toType, &keepParams);
	fromType->SetMethods({op});
	// A compile-time constant `int` 5 -- converts to `byte` via the constant-expression conversion.
	auto rr = std::make_shared<ConstantResolveResult>(Def(KnownTypeCode::Int32), std::any(std::int32_t(5)));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
	EXPECT_FALSE(result[0].IsLifted);
	EXPECT_EQ(result[0].SourceType.get(), sourceType.get());
}

// A non-nullable value-type operator gets a LIFTED form: the source AND target (both non-nullable
// value types) become `Nullable<T>`, applicable when the from/to types are the matching
// `Nullable<T>`. The non-lifted form is NOT applicable (no implicit `Nullable<T>` -> `T`).
TEST(CSharpConversionsGetApplicableOperatorsTest, LiftedFormForNonNullableValueTypeSourceAndTarget) {
	auto host = MakeHost(KnownTypeCode::Int32, /*isRef*/ std::optional<bool>(false));  // value-type source
	auto target = MakeHost(KnownTypeCode::Int64, /*isRef*/ std::optional<bool>(false));  // value-type target
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", host, target, &keepParams);
	host->SetMethods({op});
	auto fromType = NullableOf(host);    // Nullable<int>
	auto toType = NullableOf(target);     // Nullable<long>
	auto rr = NonConstResult(NullableOf(host));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
	EXPECT_TRUE(result[0].IsLifted);
	// The lifted source/target are Nullable<T> over the operator's source/target.
	EXPECT_TRUE(IsNullable(*result[0].SourceType));
	EXPECT_TRUE(IsNullable(*result[0].TargetType));
	EXPECT_EQ(static_cast<const IType*>(&GetUnderlyingType(*result[0].SourceType)),
	          static_cast<const IType*>(host.get()));
	EXPECT_EQ(static_cast<const IType*>(&GetUnderlyingType(*result[0].TargetType)),
	          static_cast<const IType*>(target.get()));
}

// The lifted form keeps a non-value-type target as-is (NOT wrapped in Nullable<T>): the source
// is lifted to `Nullable<T>`, the reference-type target stays the original target type.
TEST(CSharpConversionsGetApplicableOperatorsTest, LiftedFormKeepsNonValueTypeTarget) {
	auto host = MakeHost(KnownTypeCode::Int32, /*isRef*/ std::optional<bool>(false));  // value-type source
	auto target = std::make_shared<KnownType>(KnownTypeCode::String);  // reference-type target
	std::vector<std::shared_ptr<TestParameter>> keepParams;
	const IMethod* op = MakeOperator("op_Implicit", host, target, &keepParams);
	host->SetMethods({op});
	auto fromType = NullableOf(host);    // Nullable<int>
	auto toType = target;                // string
	auto rr = NonConstResult(NullableOf(host));
	auto result = GetApplicableConversionOperators(Compilation(), *rr, *fromType, *toType, /*isExplicit*/ false);
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0].Method, op);
	EXPECT_TRUE(result[0].IsLifted);
	EXPECT_TRUE(IsNullable(*result[0].SourceType));   // source lifted
	EXPECT_FALSE(IsNullable(*result[0].TargetType));  // target kept
	EXPECT_EQ(result[0].TargetType.get(), static_cast<const IType*>(target.get()));
}
