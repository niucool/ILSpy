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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `OverloadResolution` constraint validation (OverloadResolution.cs line 585, the
// internal static `ValidateConstraints(ITypeParameter, IType, TypeVisitor, CSharpConversions)`)
// ported as a `Detail::` free function:
//   * `ValidateConstraints(const ITypeParameter&, IType&, TypeVisitor*, CSharpConversions&)` --
//     whether the type argument satisfies the type parameter's constraints. The outright
//     rejections (`void`/`null`/pointer type arguments); the `class` constraint (a definite
//     `IsReferenceType == true` -- an indeterminate optional FAILS); the `struct` constraint
//     (`NullableType.IsNonNullableValueType` -- a `Nullable<T>` FAILS despite being a value
//     type); the `new()` constraint (an abstract definition FAILS; a public parameterless
//     constructor is required, looked up with `IgnoreInheritedMembers | ReturnMemberDefinitions`);
//     and the declared base-type constraints (`where T : Base`), each constraint-convertible
//     from the type argument after the optional `TypeVisitor` substitution.
//
// The tests pin every rejecting arm (the kind switch, both `IsReferenceType` non-true
// directions, the nullable/indeterminate/reference `IsNonNullableValueType` failures, the
// abstract/no-ctor/non-public-ctor/parameterized-ctor-only `new()` failures, the inconvertible
// base-type constraint, the null substitution NOT replacing a type-parameter constraint) and
// the accepting paths (each constraint satisfied, the trivial no-constraints case, and the
// substitution REPLACING a type-parameter constraint so a previously-failing constraint
// starts to pass -- the `ConstraintValidatingSubstitution` wiring crux).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ValidateConstraints;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeParameter` whose `AcceptVisitor` dispatches to `visitor.VisitTypeParameter(*this)`
// (the C# `AbstractTypeParameter.AcceptVisitor` bridge) -- needed for the substitution arm: the
// base `IType::AcceptVisitor` default routes to `VisitOtherType`, so a plain `LookupTypeParameter`
// would pass through `TypeParameterSubstitution` unchanged. The D514 `VisitableDefinition` /
// `TestSubstTypeParameter` precedent (kept local so the shared stub's visitor behavior is
// untouched).
class VisitableTypeParameter : public LookupTypeParameter {
public:
	using LookupTypeParameter::LookupTypeParameter;
	ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
		return visitor.VisitTypeParameter(*this);
	}
};

// A `LookupTypeDefinition` that reports itself as a reference type (the D517 `RefDef` precedent).
class RefDef : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	std::optional<bool> IsReferenceType() const override { return true; }
};

// A `LookupTypeDefinition` that reports itself as a value type (a definite
// `IsReferenceType == false`, unlike the inherited `std::nullopt` default).
class ValueTypeDef : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	std::optional<bool> IsReferenceType() const override { return false; }
};

std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
	int n = static_cast<int>(ktc);
	std::string name = "RT" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
	return std::make_shared<RefDef>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

std::shared_ptr<ValueTypeDef> MakeValueDef(KnownTypeCode ktc) {
	int n = static_cast<int>(ktc);
	std::string name = "VT" + std::to_string(n);
	return std::make_shared<ValueTypeDef>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A plain `LookupTypeDefinition` (the inherited `IsReferenceType == nullopt`) -- pins that an
// INDETERMINATE reference-ness fails both the `class` and the `struct` constraint (neither a
// definite `true` nor a definite `false`).
std::shared_ptr<LookupTypeDefinition> MakeIndeterminateDef() {
	return std::make_shared<LookupTypeDefinition>(
		"Indet", "",
		FullTypeName(TopLevelTypeName("", "Indet", 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::Int32);
}

// The `System.Object` definition -- a reference type carrying `KnownTypeCode::Object` (the
// `IsSubtypeOf` short-circuit fires, the D517 precedent).
std::shared_ptr<RefDef> ObjectDef() {
	static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
	return d;
}

// A custom reference type (the type argument / an unrelated base-type constraint).
std::shared_ptr<RefDef> DerivedDef() {
	static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
	return d;
}

// An unrelated interface definition (an inconvertible base-type constraint).
std::shared_ptr<RefDef> InterfaceDef() {
	static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Interface);
	return d;
}

// A minimal `IParameter` for the parameterized-constructor filter (the D536/D548
// `TestParameter` precedent).
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
	const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
	ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
	std::string name_;
	ITypePtr type_;
	::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `LookupTypeDefinition` with a configurable constructor list: `GetConstructors` applies the
// passed filter faithfully (the D529/D533 `MethodHostType` convention) and records the options
// it was called with (the `IgnoreInheritedMembers | ReturnMemberDefinitions` forwarding crux).
// `GetConstructors` is `const`, so the recorder is `mutable` (the D533 call-count precedent).
class CtorHostDef : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	void SetCtors(std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> ctors) { ctors_ = std::move(ctors); }
	GetMemberOptions LastCtorOptions() const { return lastOptions_; }
	std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> GetConstructors(
		std::function<bool(const ILSpy::Decompiler::TypeSystem::IMethod*)> filter,
		GetMemberOptions options) const override {
		lastOptions_ = options;
		std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> result;
		for (const ILSpy::Decompiler::TypeSystem::IMethod* m : ctors_) {
			if (!filter || filter(m))
				result.push_back(m);
		}
		return result;
	}
private:
	std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> ctors_;
	mutable GetMemberOptions lastOptions_ = GetMemberOptions::None;
};

// A `CtorHostDef` whose definition is abstract -- the `new()` constraint's
// `def != null && def.IsAbstract` early rejection.
class AbstractCtorHostDef : public CtorHostDef {
public:
	using CtorHostDef::CtorHostDef;
	bool IsAbstract() const override { return true; }
};

std::shared_ptr<CtorHostDef> MakeCtorHostDef(TypeKind kind = TypeKind::Class) {
	return std::make_shared<CtorHostDef>(
		"CtorHost", "",
		FullTypeName(TopLevelTypeName("", "CtorHost", 0)),
		kind, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// A `LookupMethod` acting as an instance constructor: `Parameters()` empty by default (a
// parameterless ctor), the `Accessibility` configurable (the internal-ctor filter case).
std::shared_ptr<LookupMethod> MakeCtor(Accessibility accessibility = Accessibility::Public) {
	auto m = std::make_shared<LookupMethod>(".ctor", Compilation());
	m->SetAccessibility(accessibility);
	return m;
}

// ---- The outright rejections (void, null, and pointer type arguments) ----

TEST(ValidateConstraintsTest, VoidTypeArgumentIsRejected)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	SpecialType voidType(TypeKind::Void);
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, voidType, nullptr, conversions));
}

TEST(ValidateConstraintsTest, NullTypeArgumentIsRejected)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	SpecialType nullType(TypeKind::Null);
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, nullType, nullptr, conversions));
}

TEST(ValidateConstraintsTest, PointerTypeArgumentIsRejected)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	auto int32 = MakeValueDef(KnownTypeCode::Int32);
	PointerType pointerType(int32);
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, pointerType, nullptr, conversions));
}

// ---- The `class` constraint (`HasReferenceTypeConstraint`) ----

TEST(ValidateConstraintsTest, ClassConstraintAcceptsReferenceType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasReferenceTypeConstraint(true);
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, ClassConstraintRejectsValueType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasReferenceTypeConstraint(true);
	auto arg = MakeValueDef(KnownTypeCode::Int32);
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, ClassConstraintRejectsIndeterminateReferenceNess)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasReferenceTypeConstraint(true);
	auto arg = MakeIndeterminateDef();
	CSharpConversions conversions(Compilation());
	// The `bool? != true` check is true for BOTH a definite `false` and an indeterminate
	// `nullopt` (a lifted `!=` on a null operand is not `false`), so the indeterminate type
	// argument fails the `class` constraint.
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

// ---- The `struct` constraint (`HasValueTypeConstraint`) ----

TEST(ValidateConstraintsTest, StructConstraintAcceptsNonNullableValueType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasValueTypeConstraint(true);
	auto arg = MakeValueDef(KnownTypeCode::Int32);
	CSharpConversions conversions(Compilation());
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, StructConstraintRejectsNullableValueType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasValueTypeConstraint(true);
	// A `Nullable<T>` over a value-type generic: `IsReferenceType` delegates to the generic
	// (definite `false`) but `IsNullable` resolves the `NullableOfT` definition, so
	// `IsNonNullableValueType` is false -- a nullable value type FAILS the `struct` constraint.
	auto nullableOfT = std::make_shared<ValueTypeDef>(
		"Nullable`1", "System",
		FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::NullableOfT);
	auto underlying = MakeValueDef(KnownTypeCode::Int32);
	ParameterizedType nullableArg(nullableOfT, {underlying});
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, nullableArg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, StructConstraintRejectsReferenceType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasValueTypeConstraint(true);
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, StructConstraintRejectsIndeterminateType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasValueTypeConstraint(true);
	auto arg = MakeIndeterminateDef();
	CSharpConversions conversions(Compilation());
	// `IsNonNullableValueType` is `IsReferenceType == false && ...` -- the `bool? == false` is
	// true only for a definite `false`, so an indeterminate type argument fails too.
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

// ---- The `new()` constraint (`HasDefaultConstructorConstraint`) ----

TEST(ValidateConstraintsTest, NewConstraintAcceptsPublicParameterlessCtor)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasDefaultConstructorConstraint(true);
	auto ctor = MakeCtor();  // public, no parameters
	auto arg = MakeCtorHostDef();
	arg->SetCtors({ctor.get()});
	CSharpConversions conversions(Compilation());
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, nullptr, conversions));
	// The lookup forwards the `IgnoreInheritedMembers | ReturnMemberDefinitions` options (the
	// C# only accepts the type's OWN constructors -- base-class parameterless ctors do not
	// satisfy `new()`), the D533 options-recorder crux.
	EXPECT_EQ(arg->LastCtorOptions(),
	          GetMemberOptions::IgnoreInheritedMembers | GetMemberOptions::ReturnMemberDefinitions);
}

TEST(ValidateConstraintsTest, NewConstraintRejectsAbstractDefinition)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasDefaultConstructorConstraint(true);
	auto ctor = MakeCtor();
	auto arg = std::make_shared<AbstractCtorHostDef>(
		"AbstractCtorHost", "",
		FullTypeName(TopLevelTypeName("", "AbstractCtorHost", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
	arg->SetCtors({ctor.get()});
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, NewConstraintRejectsWhenNoConstructors)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasDefaultConstructorConstraint(true);
	// A plain `LookupTypeDefinition` inherits the `IType::GetConstructors` empty default.
	auto arg = MakeCtorHostDef();
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, NewConstraintRejectsNonPublicCtor)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasDefaultConstructorConstraint(true);
	auto ctor = MakeCtor(Accessibility::Internal);
	auto arg = MakeCtorHostDef();
	arg->SetCtors({ctor.get()});
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, NewConstraintRejectsParameterizedCtorOnly)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetHasDefaultConstructorConstraint(true);
	auto int32 = MakeValueDef(KnownTypeCode::Int32);
	TestParameter param(int32);
	auto ctor = MakeCtor();
	ctor->SetParameters({&param});
	auto arg = MakeCtorHostDef();
	arg->SetCtors({ctor.get()});
	CSharpConversions conversions(Compilation());
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

// ---- The trivial no-constraints case ----

TEST(ValidateConstraintsTest, NoConstraintsSatisfiesAnyNormalType)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

// ---- The declared base-type constraints (`where T : Base`) ----

TEST(ValidateConstraintsTest, BaseClassConstraintAcceptsConvertibleArgument)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetDirectBaseTypes({ObjectDef()});
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	// `IsConstraintConvertible(Derived, Object)` fires the implicit-reference arm (the
	// `IsSubtypeOf` Object short-circuit).
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, BaseClassConstraintRejectsInconvertibleArgument)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetDirectBaseTypes({InterfaceDef()});
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	// Neither identity, reference, boxing, nor the type-parameter arm converts a `Derived`
	// class to an unrelated interface.
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

// ---- The optional substitution (constraints that reference type parameters) ----

TEST(ValidateConstraintsTest, NullSubstitutionDoesNotReplaceTypeParameterConstraint)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	auto t2 = std::make_shared<VisitableTypeParameter>("T2");
	tp->SetDirectBaseTypes({t2});
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	// Without the substitution the constraint stays the type parameter `T2`: a `Derived`
	// class is not constraint-convertible to an indeterminate type parameter.
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

TEST(ValidateConstraintsTest, SubstitutionReplacesTypeParameterConstraint)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	auto t2 = std::make_shared<VisitableTypeParameter>("T2");
	tp->SetDirectBaseTypes({t2});
	auto arg = DerivedDef();
	// A method-type-argument substitution mapping `T2` (index 0, method owner -- the
	// `LookupTypeParameter` defaults) to `System.Object`.
	TypeParameterSubstitution substitution(std::nullopt, std::vector<ITypePtr>{ObjectDef()});
	CSharpConversions conversions(Compilation());
	// With the substitution the constraint becomes `System.Object`, which `Derived` IS
	// constraint-convertible to (the implicit-reference arm) -- the same setup that FAILS
	// with a null substitution now passes, pinning that the substitution is applied.
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, &substitution, conversions));
}

TEST(ValidateConstraintsTest, DegenerateNullConstraintYieldsFalse)
{
	auto tp = std::make_shared<LookupTypeParameter>("T");
	tp->SetDirectBaseTypes({ITypePtr()});
	auto arg = DerivedDef();
	CSharpConversions conversions(Compilation());
	// A null `DirectBaseTypes` entry cannot occur in the real type system (the C# would
	// NRE on it); the port's null guard yields false as the safe faithful fallback
	// (the D543 IsExactlyMatching precedent).
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr, conversions));
}

} // namespace
