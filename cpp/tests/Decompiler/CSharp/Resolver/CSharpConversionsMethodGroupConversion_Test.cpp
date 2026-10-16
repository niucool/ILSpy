// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `Detail::MethodGroupConversion` (CSharpConversions.cs line 1363, C# 9.0 spec
// section 10.8 "Method group conversions") -- the method-group -> delegate-type conversion, the
// last of the CSharpConversions conversion arms to land. It composes the whole ported engine:
// `GetDelegateInvokeMethod` (D533) resolves the target delegate's Invoke, the already-ported
// `MethodGroupConversionArguments` (D535) builds the synthetic delegate-invoke arguments,
// `MethodGroupResolveResult::PerformOverloadResolution` resolves the group with the three flags
// pinned false, `GetBestCandidateWithSubstitutedTypeArguments` yields the chosen method, and
// `IsDelegateCompatible` (D531) decides the valid/invalid factory pair.
//
// The load-bearing cruxes:
//  (a) the DELEGATE-INVOKE RESOLUTION GATE -- a non-delegate target (or a delegate with no
//      Invoke) resolves no invoke method and yields `None`;
//  (b) the VALID CONVERSION -- an applicable, unambiguous, delegate-compatible resolution
//      returns the `MethodGroupConv` with `IsValid`/`IsMethodGroupConversion`/`IsImplicit` and
//      `Method` == the CHOSEN method (the overload selection: `M(int)` beats `M(long)` for an
//      `int` delegate parameter);
//  (c) the INVALID TWIN -- an AMBIGUOUS resolution (two identical signatures tie 0/0) and a
//      NOT-DELEGATE-COMPATIBLE resolution (applicable by the numeric widening `int`->`long`,
//      but delegate compatibility accepts only identity or implicit reference conversions)
//      both return `InvalidMethodGroupConversion` -- still a `MethodGroupConv` carrying the
//      chosen method, but `IsValid` false;
//  (d) the isVirtual FLAG -- `method.IsOverridable && !(target is ThisResolveResult {
//      CausesNonVirtualInvocation: true })`: an overridable method with a null/plain-`this`
//      target is a virtual lookup, a `base.M()`-style target (the `ThisResolveResult` with
//      `CausesNonVirtualInvocation`) is not, and a non-overridable method never is;
//  (e) the delegateCapturesFirstArgument FLAG -- `IsExtensionMethodInvocation ||
//      !method.IsStatic`: an instance method captures the delegate's first argument, a static
//      method does not;
//  (f) the THREE FLAG PINS -- `allowExpandingParams`/`allowOptionalParameters`/
//      `allowImplicitIn` are all false: a params-collection method is not expanded (a
//      two-parameter delegate against `M(params int[])` finds no applicable candidate), and an
//      optional parameter is not filled (a zero-parameter delegate against `M(int x = 5)`
//      neither);
//  (g) the REF ARM ROUND TRIP -- a `ref` delegate parameter builds a `ByReferenceResolveResult`
//      synthetic argument, resolves the `ref` method, and the identity-on-types delegate
//      compatibility accepts it;
//  (h) the DYNAMIC-ERASURE ARM -- a `dynamic` delegate parameter erases to `object` for the
//      method-group lookup AND the delegate-compatibility identity check folds `dynamic` back
//      to `object` through the TypeErasure, so `M(object)` converts a `D(dynamic)` delegate;
//  (i) the DISPATCH WIRING -- the public `CSharpConversions::ImplicitConversion(ResolveResult,
//      IType)` entry routes a method group to the arm (a non-generic shape: the dispatch threads
//      the `CSharpConversions::Get` singleton, whose `ImplicitConversion(IType, IType)` cache
//      only a generic method's Fix path populates -- the D540 cache-dangling caveat), a method
//      group against a non-delegate target falls through the arm to `None`, and a
//      non-method-group resolve result skips the arm (the dispatch-owned RTTI).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions (the public dispatch entry + the local conversions threaded to the helper)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"  // Detail::MethodGroupConversion (the helper under test)
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"  // MethodGroupResolveResult, MethodListWithDeclaringType (the group + buckets)
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions (the None singleton)
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (the plain synthetic arguments)
#include "Decompiler/Semantics/ThisResolveResult.hpp"  // ThisResolveResult (the isVirtual target-result property pattern)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"  // ByReferenceType, SpecialType, ArrayType, GetMemberOptions, TypeVisitor (via IType.hpp)
#include "Decompiler/TypeSystem/TypeVisitor.hpp"  // TypeVisitor (the VisitableDefinition AcceptVisitor bridge)
#include "Decompiler/TypeSystem/LookupStubs.hpp"  // LookupCompilation, LookupMethod, LookupTypeDefinition
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::MethodGroupConversion;
using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::ThisResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` overriding `AcceptVisitor` to dispatch to
// `visitor.VisitTypeDefinition` (the C# `TypeDefinition.AcceptVisitor` bridge) -- the plain
// `LookupTypeDefinition` routes to `VisitOtherType`, so the TypeErasure object->dynamic fold
// never fires for it (the D514 `VisitableDefinition` precedent).
class VisitableDefinition : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
		return visitor.VisitTypeDefinition(*this);
	}
};

// The shared compilation with `Object` registered (the dynamic-erasure arm's `FindType(Object)`
// target; the D535 MethodGroupArgs precedent). The registered `Def(Object)` shared_ptr is kept
// alive in a static so `shared_from_this` is valid. The registered definition is a
// `VisitableDefinition` (below) so the TypeErasure object->dynamic fold fires for it -- the
// delegate-compatibility identity check on `(dynamic, object)` erases the object side to
// `dynamic` through `VisitTypeDefinition` (the plain `LookupTypeDefinition` routes to
// `VisitOtherType` and stays unfolded, the D514 learning).
LookupCompilation& Compilation()
{
	static LookupCompilation c;
	static const ITypePtr objectDef = std::make_shared<VisitableDefinition>(
		"Object", "System",
		FullTypeName(TopLevelTypeName("System", "Object", 0)),
		TypeKind::Class, Accessibility::Public, c, nullptr, KnownTypeCode::Object);
	static const bool registered = [&] {
		c.RegisterKnownType(KnownTypeCode::Object, objectDef.get());
		return true;
	}();
	(void)registered;
	return c;
}

const ITypePtr& ObjectDef()
{
	static const ITypePtr objectDef = [] {
		// The SAME instance the compilation's FindType(Object) resolves (registered above), so
		// the identity conversions on it compare equal (identity-equality stub).
		const IType& t = Compilation().FindType(KnownTypeCode::Object);
		return std::const_pointer_cast<IType>(t.shared_from_this());
	}();
	return objectDef;
}

// A shared-managed `LookupTypeDefinition` stub with a configurable `KnownTypeCode` / `TypeKind`
// (the D514 `MakeDef` precedent). The type-cache model: one shared instance per primitive used
// across the delegate's parameter, the method's parameter, and the argument.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct)
{
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// A `dynamic`-typed stub (a `SpecialType(Dynamic, /*isReferenceType=*/true)`), the D528
// dynamic-arm precedent.
ITypePtr DynamicType() { return std::make_shared<SpecialType>(TypeKind::Dynamic, true); }

// A shared-managed return type for the delegate's Invoke and the group's methods (the D578
// `CreateResolveResult` learning: the `LookupMethod` stub's DEFAULT return type is an inline
// non-shared-managed `KnownType` member, and the delegate-compatibility return check visits it
// through the TypeErasure -> `AcceptVisitor` -> `VisitChildren` -> `shared_from_this` chain --
// without a `SetReturnType` make_shared'd type the visit throws `bad_weak_ptr`). The single
// shared instance keeps both methods' return types identical (the value-based
// `KnownType::StructuralEquals` would also accept distinct instances).
ITypePtr RetType()
{
	static const ITypePtr ret = std::make_shared<KnownType>(KnownTypeCode::Object);
	return ret;
}

// A `ByReferenceType` wrapping the element (the ref/out/in parameter's type).
ITypePtr ByRef(ITypePtr element) { return std::make_shared<ByReferenceType>(std::move(element)); }

// The `IParameter` stub with a configurable type / reference kind / IsParams / IsOptional (the
// D524/D531/D575 `TestParameter` precedents). The `SymbolKind()` / `ReferenceKind()` accessors
// hide the namespace-scope enums of the same name for the rest of the class body (the D402
// crux), so the enum references are fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
class TestParameter : public IParameter {
public:
	explicit TestParameter(ITypePtr type,
	                       ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
	                       std::string name = "p",
	                       bool isParams = false, bool isOptional = false)
		: name_(std::move(name)), type_(std::move(type)), refKind_(refKind),
		  isParams_(isParams), isOptional_(isOptional) {}
	::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
	{ return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
	std::string Name() const override { return name_; }
	const IType& Type() const override { return *type_; }
	bool IsConst() const override { return false; }
	std::any GetConstantValue(bool) const override { return std::any{}; }
	std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
	::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
	bool IsParams() const override { return isParams_; }
	bool IsOptional() const override { return isOptional_; }
	bool HasConstantValueInSignature() const override { return false; }
	const IParameterizedMember* Owner() const override { return nullptr; }
	::ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
	std::string name_;
	ITypePtr type_;
	::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
	bool isParams_;
	bool isOptional_;
};

// A `LookupTypeDefinition` subclass overriding `GetMethods` to return a configured method table
// (the D533 `MethodHostType` precedent). Used for the delegate type (TypeKind::Delegate with
// the `Invoke` method the `GetDelegateInvokeMethod` filter selects) and for the non-delegate
// sentinel (a Class kind).
class MethodHostType : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

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
};

std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
	return std::make_shared<MethodHostType>(
		std::move(name), "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr);
}

// Builds a method group over one declaring-type bucket holding the given methods, with the
// given target resolve result (null for the plain untargeted group).
std::shared_ptr<MethodGroupResolveResult> Group(
	std::shared_ptr<ResolveResult> targetResult,
	std::vector<const IParameterizedMember*> methods)
{
	static const ITypePtr bucketDeclaring = [] {
		auto t = std::make_shared<LookupTypeDefinition>(
			"C", "",
			FullTypeName(TopLevelTypeName("", "C", 0)),
			TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
		return t;
	}();
	return std::make_shared<MethodGroupResolveResult>(
		std::move(targetResult), "M",
		std::vector<MethodListWithDeclaringType>{
			MethodListWithDeclaringType(bucketDeclaring, std::move(methods))},
		std::vector<ITypePtr>{});
}

// Runs the helper under test with a LOCAL `CSharpConversions` threaded through (the D540
// cache-dangling caveat: a local instance's `ImplicitConversion(IType, IType)` cache dies with
// the call, so test-local types never dangle in the shared `CSharpConversions::Get` singleton).
std::shared_ptr<Conversion> Convert(const MethodGroupResolveResult& group, const IType& toType)
{
	CSharpConversions localConversions(Compilation());
	return MethodGroupConversion(Compilation(), &localConversions, group, toType);
}

} // namespace

// ---- (a) the delegate-invoke resolution gate ----

// A non-delegate target resolves no invoke method: the conversion yields `None`.
TEST(CSharpConversionsMethodGroupConversionTest, NonDelegateTargetYieldsNone)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto param = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({param.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});
	auto nonDelegate = MakeHost("NotADelegate", TypeKind::Class);
	auto c = Convert(*group, *nonDelegate);
	EXPECT_EQ(c.get(), Conversions::None().get());
}

// A delegate-kind type with an EMPTY method table resolves no invoke method either.
TEST(CSharpConversionsMethodGroupConversionTest, DelegateWithoutInvokeYieldsNone)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto param = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({param.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});
	auto delegateType = MakeHost("D");  // no methods configured
	auto c = Convert(*group, *delegateType);
	EXPECT_EQ(c.get(), Conversions::None().get());
}

// ---- (b) the valid conversion ----

// A single applicable, unambiguous, delegate-compatible method yields the VALID method-group
// conversion carrying the chosen method. The default `LookupMethod` shape is non-overridable
// (`IsVirtualMethodLookup` false) and an instance method (`DelegateCapturesFirstArgument` true).
TEST(CSharpConversionsMethodGroupConversionTest, ApplicableMethodYieldsValidConversion)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_NE(c.get(), Conversions::None().get());
	EXPECT_TRUE(c->IsValid());
	EXPECT_TRUE(c->IsMethodGroupConversion());
	EXPECT_TRUE(c->IsImplicit());
	EXPECT_FALSE(c->IsExplicit());
	EXPECT_EQ(c->Method(), static_cast<const IMethod*>(method.get()));
	EXPECT_FALSE(c->IsVirtualMethodLookup());
	EXPECT_TRUE(c->DelegateCapturesFirstArgument());
}

// The overload selection flows through: `M(int)` beats `M(long)` for an `int` delegate parameter
// (the identity conversion is better than the numeric widening), so the conversion carries the
// `M(int)` method.
TEST(CSharpConversionsMethodGroupConversionTest, OverloadSelectionPicksMatchingMethod)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto longType = Def(KnownTypeCode::Int64);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto intParam = std::make_shared<TestParameter>(intType);
	auto intMethod = std::make_shared<LookupMethod>("M", Compilation());
	intMethod->SetParameters({intParam.get()});
	intMethod->SetReturnType(RetType());
	auto longParam = std::make_shared<TestParameter>(longType);
	auto longMethod = std::make_shared<LookupMethod>("M", Compilation());
	longMethod->SetParameters({longParam.get()});
	longMethod->SetReturnType(RetType());
	auto group = Group(nullptr, {intMethod.get(), longMethod.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_EQ(c->Method(), static_cast<const IMethod*>(intMethod.get()));
}

// ---- (c) the invalid twin ----

// Two IDENTICAL signatures tie 0/0 in BetterFunctionMember: the resolution is ambiguous, so the
// conversion is the INVALID twin -- still a method-group conversion carrying the chosen method.
TEST(CSharpConversionsMethodGroupConversionTest, AmbiguousGroupYieldsInvalidConversion)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto param1 = std::make_shared<TestParameter>(intType);
	auto method1 = std::make_shared<LookupMethod>("M", Compilation());
	method1->SetParameters({param1.get()});
	method1->SetReturnType(RetType());
	auto param2 = std::make_shared<TestParameter>(intType);  // the SAME shared int instance
	auto method2 = std::make_shared<LookupMethod>("M", Compilation());
	method2->SetParameters({param2.get()});
	method2->SetReturnType(RetType());
	auto group = Group(nullptr, {method1.get(), method2.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_NE(c.get(), Conversions::None().get());
	EXPECT_FALSE(c->IsValid());
	EXPECT_TRUE(c->IsMethodGroupConversion());
	EXPECT_TRUE(c->IsImplicit());
	EXPECT_NE(c->Method(), nullptr);
}

// `M(long)` is APPLICABLE to a `D(int)` delegate (the synthetic int argument converts by the
// numeric widening), but delegate compatibility accepts only identity or implicit reference
// conversions on by-value parameters -- so the conversion is the INVALID twin.
TEST(CSharpConversionsMethodGroupConversionTest, WidenedApplicabilityButNotDelegateCompatibleYieldsInvalid)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto longType = Def(KnownTypeCode::Int64);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(longType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_NE(c.get(), Conversions::None().get());
	EXPECT_FALSE(c->IsValid());
	EXPECT_TRUE(c->IsMethodGroupConversion());
	EXPECT_EQ(c->Method(), static_cast<const IMethod*>(method.get()));
}

// ---- (d) the isVirtual flag ----

// An overridable method with a null target is a virtual lookup.
TEST(CSharpConversionsMethodGroupConversionTest, OverridableMethodWithNullTargetIsVirtual)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	method->SetIsOverridable(true);
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_TRUE(c->IsVirtualMethodLookup());
}

// A `base.M()`-style target (a `ThisResolveResult` with `CausesNonVirtualInvocation`) forces the
// non-virtual lookup even for an overridable method -- the property pattern requires BOTH the
// runtime type and the flag.
TEST(CSharpConversionsMethodGroupConversionTest, OverridableMethodWithCausesNonVirtualTargetIsNotVirtual)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	method->SetIsOverridable(true);
	auto target = std::make_shared<ThisResolveResult>(intType, /*causesNonVirtualInvocation*/ true);
	auto group = Group(target, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_FALSE(c->IsVirtualMethodLookup());
}

// A plain `this` target (a `ThisResolveResult` WITHOUT the flag) keeps the virtual lookup.
TEST(CSharpConversionsMethodGroupConversionTest, OverridableMethodWithPlainThisTargetIsVirtual)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	method->SetIsOverridable(true);
	auto target = std::make_shared<ThisResolveResult>(intType, /*causesNonVirtualInvocation*/ false);
	auto group = Group(target, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_TRUE(c->IsVirtualMethodLookup());
}

// A NON-overridable method is never a virtual lookup, whatever the target.
TEST(CSharpConversionsMethodGroupConversionTest, NonOverridableMethodIsNotVirtual)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto target = std::make_shared<ThisResolveResult>(intType, /*causesNonVirtualInvocation*/ false);
	auto group = Group(target, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_FALSE(c->IsVirtualMethodLookup());
}

// ---- (e) the delegateCapturesFirstArgument flag ----

// A static method does not capture the delegate's first argument.
TEST(CSharpConversionsMethodGroupConversionTest, StaticMethodDoesNotCaptureFirstArgument)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	method->SetStatic(true);
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_FALSE(c->DelegateCapturesFirstArgument());
}

// ---- (f) the three flag pins ----

// `allowExpandingParams` is pinned false: a params-collection method is not expanded, so a
// two-parameter delegate finds no applicable candidate (the normal form takes one array
// parameter -- TooManyPositionalArguments).
TEST(CSharpConversionsMethodGroupConversionTest, ParamsExpansionDisabledYieldsNone)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto intArrayType = std::make_shared<ArrayType>(intType);
	auto invokeParamA = std::make_shared<TestParameter>(intType);
	auto invokeParamB = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParamA.get(), invokeParamB.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intArrayType, /*refKind*/ ReferenceKind::None,
	                                                   /*name*/ "p", /*isParams*/ true);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	EXPECT_EQ(c.get(), Conversions::None().get());
}

// `allowOptionalParameters` is pinned false: a zero-parameter delegate against
// `M(int x = 5)` finds no applicable candidate (the optional parameter is not filled).
TEST(CSharpConversionsMethodGroupConversionTest, OptionalParametersDisabledYieldsNone)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({});  // a zero-parameter delegate
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType, /*refKind*/ ReferenceKind::None,
	                                                   /*name*/ "x", /*isParams*/ false,
	                                                   /*isOptional*/ true);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	EXPECT_EQ(c.get(), Conversions::None().get());
}

// ---- (g) the ref arm round trip ----

// A `ref` delegate parameter builds a `ByReferenceResolveResult` synthetic argument, the
// resolution maps it to the `ref` method (the reference-kind match + the identity conversion on
// the by-reference types), and the delegate compatibility's ref arm (the identity on the types)
// accepts it: the VALID conversion.
TEST(CSharpConversionsMethodGroupConversionTest, RefParameterDelegateRoundTrip)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(ByRef(intType), ReferenceKind::Ref);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(ByRef(intType), ReferenceKind::Ref);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_TRUE(c->IsMethodGroupConversion());
	EXPECT_EQ(c->Method(), static_cast<const IMethod*>(method.get()));
}

// ---- (h) the dynamic-erasure arm ----

// A `dynamic` delegate parameter erases to `object` for the method-group lookup (the synthetic
// argument is a plain `ResolveResult` over `FindType(Object)`), and the delegate-compatibility
// identity check folds `dynamic` back to `object` through the TypeErasure -- so `M(object)`
// converts a `D(dynamic)` delegate.
TEST(CSharpConversionsMethodGroupConversionTest, DynamicDelegateParameterErasesToObject)
{
	auto invokeParam = std::make_shared<TestParameter>(DynamicType());
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(ObjectDef());
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	auto c = Convert(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_TRUE(c->IsValid());
	EXPECT_EQ(c->Method(), static_cast<const IMethod*>(method.get()));
}

// ---- (i) the dispatch wiring ----

// The public `ImplicitConversion(ResolveResult, IType)` entry routes a method group to the arm
// (a NON-GENERIC shape: the dispatch threads the `CSharpConversions::Get` singleton, whose
// `ImplicitConversion(IType, IType)` cache only a generic method's Fix path populates -- the
// D540 cache-dangling caveat).
TEST(CSharpConversionsMethodGroupConversionTest, DispatchWiringResolvesMethodGroup)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});

	CSharpConversions conversions(Compilation());
	auto c = conversions.ImplicitConversion(*group, *delegateType);
	ASSERT_NE(c.get(), nullptr);
	EXPECT_NE(c.get(), Conversions::None().get());
	EXPECT_TRUE(c->IsMethodGroupConversion());
	EXPECT_TRUE(c->IsValid());
	EXPECT_EQ(c->Method(), static_cast<const IMethod*>(method.get()));
}

// A method group against a NON-delegate target: the arm fires (the dispatch's RTTI) but the
// helper yields `None` (no invoke method), and the dispatch falls through to the IType fallback
// whose `NoType` converts to nothing -- the final `None`.
TEST(CSharpConversionsMethodGroupConversionTest, DispatchNonDelegateFallsThrough)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto methodParam = std::make_shared<TestParameter>(intType);
	auto method = std::make_shared<LookupMethod>("M", Compilation());
	method->SetParameters({methodParam.get()});
	method->SetReturnType(RetType());
	auto group = Group(nullptr, {method.get()});
	auto nonDelegate = MakeHost("NotADelegate", TypeKind::Class);

	CSharpConversions conversions(Compilation());
	auto c = conversions.ImplicitConversion(*group, *nonDelegate);
	EXPECT_EQ(c.get(), Conversions::None().get());
}

// A non-method-group resolve result skips the arm entirely (the dispatch-owned RTTI): a plain
// `ResolveResult(int)` against a delegate type converts by none of the arms -- `None`.
TEST(CSharpConversionsMethodGroupConversionTest, DispatchNonMethodGroupSkipsArm)
{
	auto intType = Def(KnownTypeCode::Int32);
	auto invokeParam = std::make_shared<TestParameter>(intType);
	auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetParameters({invokeParam.get()});
	invoke->SetReturnType(RetType());
	auto delegateType = MakeHost("D");
	delegateType->SetMethods({invoke.get()});

	auto arg = std::make_shared<ResolveResult>(intType);
	CSharpConversions conversions(Compilation());
	auto c = conversions.ImplicitConversion(*arg, *delegateType);
	EXPECT_EQ(c.get(), Conversions::None().get());
}
