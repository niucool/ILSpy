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
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Detail::MethodGroupConversionArguments` (CSharpConversions.cs line 1362, the local
// `args` construction inside `MethodGroupConversion`) -- the synthetic-arguments helper that
// builds one `ResolveResult` per delegate-invoke parameter, ahead of the (deferred)
// `MethodGroupResolveResult.PerformOverloadResolution` engine. Each parameter maps to:
//   * a `ByReferenceResolveResult(elementType, refKind)` for a ref/out/in parameter whose type is
//     a `ByReferenceType` (the element unwrapped);
//   * a plain `ResolveResult(object)` for a `dynamic`-typed parameter (dynamic erases to object);
//   * a plain `ResolveResult(parameterType)` otherwise.
//
// CRUX STUB CONVENTIONS (carried from the D531/D532/D533 delegate-compatible tests):
//  * `Def(ktc)` is the shared-managed `LookupTypeDefinition` stub (the D514 precedent). The plain /
//    dynamic-object owning `ITypePtr` handles come from `shared_from_this()` + `const_pointer_cast`,
//    so the stub types MUST be `make_shared`-managed (the enable_shared_from_this contract); a
//    stack-local type would be UB under `shared_from_this`.
//  * `TestParameter(type, refKind)` is the `IParameter` stub (the D524/D531 precedent); the
//    `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same name
//    for the rest of the class body (the D402 cross-scope name-hiding crux), so the enum references
//    are fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetParameters`); kept
//    alive in a static vector so the raw `const IMethod&` outlives the call.
//  * `LookupCompilation` registers `Def(Object)` as the `KnownTypeCode::Object` known type so the
//    dynamic arm's `compilation.FindType(Object)` resolves it (the registered shared_ptr stays
//    alive for the program's lifetime so `shared_from_this` is valid).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"   // Detail::MethodGroupConversionArguments
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"        // ByReferenceResolveResult (the ref/out/in arm result)
#include "Decompiler/Semantics/ResolveResult.hpp"                   // ResolveResult (the plain-arm result)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"                          // ByReferenceType, SpecialType, KnownType
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::MethodGroupConversionArguments;
using ILSpy::Decompiler::Semantics::ByReferenceResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;

// The shared compilation with `Object` registered (the dynamic arm's `FindType(Object)` target).
// The registered `Def(Object)` shared_ptr is kept alive in a static so `shared_from_this` is valid.
LookupCompilation& Compilation()
{
	static LookupCompilation c;
	static const ITypePtr objectDef = std::make_shared<LookupTypeDefinition>(
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

// A shared-managed `LookupTypeDefinition` stub with a configurable `KnownTypeCode` / `TypeKind`
// (the D514 `MakeDef` precedent). The owning `ITypePtr` handles come from `shared_from_this`, so
// the stub MUST be `make_shared`-managed (kept alive by the caller).
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

// A `dynamic`-typed stub (a `SpecialType(Dynamic, /*isReferenceType=*/true)`), the D528 dynamic-arm
// precedent.
ITypePtr DynamicType() { return std::make_shared<SpecialType>(TypeKind::Dynamic, true); }

// A `ByReferenceType` wrapping the element (the ref/out/in parameter's type).
ITypePtr ByRef(ITypePtr element) { return std::make_shared<ByReferenceType>(std::move(element)); }

// The `IParameter` stub with a configurable type and reference kind (the D524/D531 `TestParameter`
// precedent). The `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of
// the same name for the rest of the class body (the D402 crux), so the enum references are
// fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
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

// Builds an `IMethod` (the delegate's Invoke) with the given parameters. The `LookupMethod` is
// kept alive in a static vector so the returned reference outlives the call.
const IMethod& MakeInvoke(std::vector<const IParameter*> params)
{
	static std::vector<std::shared_ptr<LookupMethod>> keepAlive;
	auto m = std::make_shared<LookupMethod>("Invoke", Compilation());
	m->SetParameters(std::move(params));
	keepAlive.push_back(std::move(m));
	return *keepAlive.back();
}

// Wraps a `TestParameter` in a `shared_ptr` kept alive in a static vector so the raw
// `const IParameter*` outlives the call.
const IParameter* KeepParam(ITypePtr type, ReferenceKind refKind = ReferenceKind::None)
{
	static std::vector<std::shared_ptr<TestParameter>> keepAlive;
	auto p = std::make_shared<TestParameter>(std::move(type), refKind);
	keepAlive.push_back(std::move(p));
	return keepAlive.back().get();
}

} // namespace

// --- Empty / sentinel ---

TEST(CSharpConversionsMethodGroupArgsTest, EmptyInvokeParametersYieldsEmptyArgs)
{
	const IMethod& invoke = MakeInvoke({});
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	EXPECT_EQ(args.size(), 0u);
}

// --- Plain by-value arm ---

TEST(CSharpConversionsMethodGroupArgsTest, PlainByValueParameterYieldsResolveResultOverParameterType)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({ KeepParam(intType, ReferenceKind::None) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	ASSERT_NE(args[0], nullptr);
	// A by-value parameter yields a plain ResolveResult (NOT a ByReferenceResolveResult).
	EXPECT_EQ(dynamic_cast<const ByReferenceResolveResult*>(args[0].get()), nullptr);
	EXPECT_EQ(&args[0]->Type(), intType.get());
}

TEST(CSharpConversionsMethodGroupArgsTest, PlainByValueParameterResolveResultSharesParameterTypeInstance)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({ KeepParam(intType, ReferenceKind::None) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	// The owning ITypePtr comes from shared_from_this, so the result's Type() is the SAME instance
	// as the parameter's type (pointer-identity).
	EXPECT_EQ(&args[0]->Type(), intType.get());
}

// --- Ref/out/in + ByReference-type arm ---

TEST(CSharpConversionsMethodGroupArgsTest, RefParameterWithByReferenceTypeYieldsByReferenceResolveResult)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({ KeepParam(ByRef(intType), ReferenceKind::Ref) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	auto* byRef = dynamic_cast<const ByReferenceResolveResult*>(args[0].get());
	ASSERT_NE(byRef, nullptr);
	EXPECT_EQ(byRef->ReferenceKind(), ReferenceKind::Ref);
	// The element type is unwrapped from the ByReferenceType.
	EXPECT_EQ(&byRef->ElementType(), intType.get());
}

TEST(CSharpConversionsMethodGroupArgsTest, OutParameterWithByReferenceTypeYieldsByReferenceResolveResult)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({ KeepParam(ByRef(intType), ReferenceKind::Out) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	auto* byRef = dynamic_cast<const ByReferenceResolveResult*>(args[0].get());
	ASSERT_NE(byRef, nullptr);
	EXPECT_EQ(byRef->ReferenceKind(), ReferenceKind::Out);
	EXPECT_EQ(&byRef->ElementType(), intType.get());
}

TEST(CSharpConversionsMethodGroupArgsTest, InParameterWithByReferenceTypeYieldsByReferenceResolveResult)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({ KeepParam(ByRef(intType), ReferenceKind::In) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	auto* byRef = dynamic_cast<const ByReferenceResolveResult*>(args[0].get());
	ASSERT_NE(byRef, nullptr);
	EXPECT_EQ(byRef->ReferenceKind(), ReferenceKind::In);
	EXPECT_EQ(&byRef->ElementType(), intType.get());
}

TEST(CSharpConversionsMethodGroupArgsTest, ByReferenceParameterElementTypeIsUnwrappedNotTheByReferenceType)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	ITypePtr byRefType = ByRef(intType);
	const IMethod& invoke = MakeInvoke({ KeepParam(byRefType, ReferenceKind::Ref) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	auto* byRef = dynamic_cast<const ByReferenceResolveResult*>(args[0].get());
	ASSERT_NE(byRef, nullptr);
	// The element type is the underlying int, NOT the ByReferenceType wrapper itself.
	EXPECT_EQ(&byRef->ElementType(), intType.get());
	EXPECT_NE(&byRef->ElementType(), byRefType.get());
	// The result's Type() is a fresh ByReferenceType wrapping the element (the internal ctor).
	EXPECT_EQ(args[0]->Type().Kind(), TypeKind::ByReference);
}

// --- Conjunction crux: both conditions required ---

TEST(CSharpConversionsMethodGroupArgsTest, RefParameterWithNonByReferenceTypeFallsToPlainArm)
{
	// A ref parameter whose type is NOT a ByReferenceType (a plain int, Kind == Struct): the
	// ByReference arm requires BOTH refKind != None AND Kind == ByReference, so this falls through
	// to the dynamic check (Kind != Dynamic) then the plain arm.
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({ KeepParam(intType, ReferenceKind::Ref) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	EXPECT_EQ(dynamic_cast<const ByReferenceResolveResult*>(args[0].get()), nullptr);
	EXPECT_EQ(&args[0]->Type(), intType.get());
}

TEST(CSharpConversionsMethodGroupArgsTest, ByReferenceTypeWithNoneReferenceKindFallsToPlainArm)
{
	// A by-value parameter (ReferenceKind::None) whose type IS a ByReferenceType: the conjunction
	// requires refKind != None, so this falls through to the plain arm. The result's Type() is the
	// ByReferenceType itself (a by-value parameter whose type happens to be a ByReferenceType).
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	ITypePtr byRefType = ByRef(intType);
	const IMethod& invoke = MakeInvoke({ KeepParam(byRefType, ReferenceKind::None) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	EXPECT_EQ(dynamic_cast<const ByReferenceResolveResult*>(args[0].get()), nullptr);
	EXPECT_EQ(&args[0]->Type(), byRefType.get());
	EXPECT_EQ(args[0]->Type().Kind(), TypeKind::ByReference);
}

// --- Dynamic arm ---

TEST(CSharpConversionsMethodGroupArgsTest, DynamicParameterYieldsResolveResultOverObject)
{
	const IMethod& invoke = MakeInvoke({ KeepParam(DynamicType(), ReferenceKind::None) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	ASSERT_NE(args[0], nullptr);
	EXPECT_EQ(dynamic_cast<const ByReferenceResolveResult*>(args[0].get()), nullptr);
	// The dynamic arm erases to object.
	EXPECT_EQ(args[0]->Type().Kind(), TypeKind::Class);
}

TEST(CSharpConversionsMethodGroupArgsTest, DynamicParameterObjectIsCompilationFindTypeObject)
{
	const IMethod& invoke = MakeInvoke({ KeepParam(DynamicType(), ReferenceKind::None) });
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 1u);
	// The object type is exactly compilation.FindType(Object) -- pointer-identity (the owning
	// ITypePtr comes from shared_from_this on the registered Def(Object)).
	EXPECT_EQ(&args[0]->Type(), &Compilation().FindType(KnownTypeCode::Object));
}

// --- Order preservation ---

TEST(CSharpConversionsMethodGroupArgsTest, MultipleParametersBuiltInOrder)
{
	ITypePtr intType = Def(KnownTypeCode::Int32, TypeKind::Struct);
	ITypePtr longType = Def(KnownTypeCode::Int64, TypeKind::Struct);
	const IMethod& invoke = MakeInvoke({
		KeepParam(intType, ReferenceKind::None),       // plain
		KeepParam(ByRef(longType), ReferenceKind::Ref), // ref ByReference
		KeepParam(DynamicType(), ReferenceKind::None),  // dynamic
	});
	auto args = MethodGroupConversionArguments(Compilation(), invoke);
	ASSERT_EQ(args.size(), 3u);
	// [0] plain over int.
	EXPECT_EQ(dynamic_cast<const ByReferenceResolveResult*>(args[0].get()), nullptr);
	EXPECT_EQ(&args[0]->Type(), intType.get());
	// [1] ref ByReference over long.
	auto* byRef = dynamic_cast<const ByReferenceResolveResult*>(args[1].get());
	ASSERT_NE(byRef, nullptr);
	EXPECT_EQ(byRef->ReferenceKind(), ReferenceKind::Ref);
	EXPECT_EQ(&byRef->ElementType(), longType.get());
	// [2] dynamic -> object.
	EXPECT_EQ(dynamic_cast<const ByReferenceResolveResult*>(args[2].get()), nullptr);
	EXPECT_EQ(&args[2]->Type(), &Compilation().FindType(KnownTypeCode::Object));
}
