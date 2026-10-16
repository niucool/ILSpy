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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the remaining "Validate Constraints" region of `OverloadResolution`
// (OverloadResolution.cs), completing the region after the internal static landed (the
// `ValidateConstraints_Test.cpp` sibling):
//   * `Detail::ValidateConstraints(const ITypeParameter&, IType&, TypeVisitor*)` -- the public
//     static 3-arg overload (C# line 576): resolves the `CSharpConversions` from the type
//     parameter's OWN compilation (`typeParameter.Owner.Compilation` via `CSharpConversions.Get`)
//     and delegates to the internal static. The null-`Owner` safe fallback (the dummy type
//     parameters have no owning entity -- the C# would NRE) yields `false`.
//   * `Detail::GetSubstitution(const OverloadResolutionCandidate&)` -- the merged substitution
//     (C# line 1168): the member's CLASS type arguments + the candidate's INFERRED method type
//     arguments. The merge-not-compose crux: the member substitution's METHOD type arguments are
//     deliberately discarded.
//   * `Detail::ValidateMethodConstraints(const OverloadResolutionCandidate&)` -- the engine step
//     (C# line 548): the `TypeInferenceFailed` skip, the non-generic skip, then per-type-parameter
//     validation via the public 3-arg overload with the merged substitution applied to constraints
//     that reference type parameters; the first violation yields the soft
//     `MethodConstraintsNotSatisfied`.
//
// The tests pin the substitution wiring at the candidate level (the constraint's type parameter is
// replaced by the corresponding INFERRED type argument, so `where T1 : T0` with
// `InferredTypes[0] = Object` checks the argument against `Object`), the merge-not-compose
// direction, the null-`Owner` fallback flowing through the candidate-level check, and the
// degenerate pre-inference candidate states (empty/short/null-entry inferred types -- the C#
// indexes the array unconditionally and would throw).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::GetSubstitution;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ValidateConstraints;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ValidateMethodConstraints;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` that reports itself as a reference type (the `RefDef` precedent).
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

// The `System.Object` definition -- a reference type carrying `KnownTypeCode::Object` (the
// `IsSubtypeOf` short-circuit fires, so `DerivedDef` is implicitly reference-convertible to it).
std::shared_ptr<RefDef> ObjectDef() {
	static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
	return d;
}

// A custom reference type (the inferred type argument / a class-constrained argument).
std::shared_ptr<RefDef> DerivedDef() {
	static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Class);
	return d;
}

// An unrelated interface definition (an inconvertible substituted constraint).
std::shared_ptr<RefDef> InterfaceDef() {
	static auto d = MakeRefDef(KnownTypeCode::None, TypeKind::Interface);
	return d;
}

// An `Int32` value type definition (a definite `IsReferenceType == false` argument -- the
// `class`-constraint violation).
std::shared_ptr<ValueTypeDef> Int32ValueDef() {
	static auto d = MakeValueDef(KnownTypeCode::Int32);
	return d;
}

// The owning entity for the type parameters -- a real `IEntity` whose `Compilation()` is the
// static test compilation, so the public 3-arg overload's `CSharpConversions.Get` resolves (a
// plain `LookupTypeParameter` returns a null `Owner`, the dummy-type-parameter shape).
const TS::IEntity* OwnerEntity() {
	static auto d = std::make_shared<LookupTypeDefinition>(
		"OwnerType", "",
		FullTypeName(TopLevelTypeName("", "OwnerType", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
	return d.get();
}

// A `LookupTypeParameter` with a configurable `Owner` -- the real-method-type-parameter shape
// (the public 3-arg overload resolves the conversions from the owner's compilation).
class OwnedTypeParameter : public LookupTypeParameter {
public:
	using LookupTypeParameter::LookupTypeParameter;
	void SetOwner(const TS::IEntity* owner) { owner_ = owner; }
	const TS::IEntity* Owner() const override { return owner_; }
private:
	const TS::IEntity* owner_ = nullptr;
};

// A `LookupTypeParameter` whose `AcceptVisitor` dispatches to `visitor.VisitTypeParameter(*this)`
// (the substitution arm needs the visitor dispatch; the base `IType::AcceptVisitor` default
// routes to `VisitOtherType`, so a plain `LookupTypeParameter` would pass through
// `TypeParameterSubstitution` unchanged). The `VisitableTypeParameter` precedent.
class VisitableTypeParameter : public LookupTypeParameter {
public:
	using LookupTypeParameter::LookupTypeParameter;
	ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
		return visitor.VisitTypeParameter(*this);
	}
};

// A `LookupMethod` with a configurable type-parameter list (the generic-method shape the
// candidate ctor reads via `MemberDefinition`/`TypeParameters`) and a configurable
// `Substitution` (the member's own class-type-argument substitution that `GetSubstitution`
// merges). The defaults preserve the shared stub's behavior (empty type parameters, a null
// `Substitution`).
class TestMethod : public LookupMethod {
public:
	using LookupMethod::LookupMethod;
	void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
	void SetSubstitution(const TypeParameterSubstitution* s) { substitution_ = s; }
	std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
	const TypeParameterSubstitution* Substitution() const override { return substitution_; }
private:
	std::vector<const ITypeParameter*> typeParameters_;
	const TypeParameterSubstitution* substitution_ = nullptr;
};

// ---- GetSubstitution: the merge (member class arguments + inferred method arguments) ----

TEST(ValidateMethodConstraintsTest, GetSubstitutionMergesClassArgumentsWithInferredTypes)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	// The member's own substitution carries CLASS type arguments (the specialized-method shape).
	TypeParameterSubstitution memberSubstitution(std::vector<ITypePtr>{ObjectDef()}, std::nullopt);
	method->SetSubstitution(&memberSubstitution);
	OverloadResolutionCandidate candidate(method.get(), false);
	candidate.InferredTypes() = {DerivedDef()};

	TypeParameterSubstitution result = GetSubstitution(candidate);

	// The class list comes from the member substitution...
	ASSERT_TRUE(result.ClassTypeArguments().has_value());
	ASSERT_EQ(result.ClassTypeArguments()->size(), 1u);
	EXPECT_EQ(result.ClassTypeArguments()->at(0).get(), ObjectDef().get());
	// ...and the method list from the candidate's inferred types.
	ASSERT_TRUE(result.MethodTypeArguments().has_value());
	ASSERT_EQ(result.MethodTypeArguments()->size(), 1u);
	EXPECT_EQ(result.MethodTypeArguments()->at(0).get(), DerivedDef().get());
}

TEST(ValidateMethodConstraintsTest, GetSubstitutionDiscardsMemberMethodArguments)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	// The member's own substitution also carries METHOD type arguments -- which the merge
	// deliberately DISCARDS (composing would double-substitute; only the class list is taken
	// from the member). The `InvocationTests.SubstituteClassAndMethodTypeParametersAtOnce` crux.
	TypeParameterSubstitution memberSubstitution(std::vector<ITypePtr>{ObjectDef()},
	                                             std::vector<ITypePtr>{ObjectDef()});
	method->SetSubstitution(&memberSubstitution);
	OverloadResolutionCandidate candidate(method.get(), false);
	candidate.InferredTypes() = {DerivedDef()};

	TypeParameterSubstitution result = GetSubstitution(candidate);

	ASSERT_TRUE(result.MethodTypeArguments().has_value());
	ASSERT_EQ(result.MethodTypeArguments()->size(), 1u);
	// The method arguments are the INFERRED types, not the member's own method arguments.
	EXPECT_EQ(result.MethodTypeArguments()->at(0).get(), DerivedDef().get());
	EXPECT_NE(result.MethodTypeArguments()->at(0).get(), ObjectDef().get());
}

TEST(ValidateMethodConstraintsTest, GetSubstitutionNullMemberSubstitutionYieldsIdentityClassArguments)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	// The default `Substitution()` is null (the shared-stub shape). The C# contract is "never
	// null" ("Returns Identity for not specialized"), so the fallback is the Identity singleton's
	// `nullopt` class list ("keep the class type parameters unmodified").
	OverloadResolutionCandidate candidate(method.get(), false);
	candidate.InferredTypes() = {DerivedDef()};

	TypeParameterSubstitution result = GetSubstitution(candidate);

	EXPECT_FALSE(result.ClassTypeArguments().has_value());
	ASSERT_TRUE(result.MethodTypeArguments().has_value());
	EXPECT_EQ(result.MethodTypeArguments()->at(0).get(), DerivedDef().get());
}

TEST(ValidateMethodConstraintsTest, GetSubstitutionEmptyInferredTypesYieldsPresentEmptyMethodArguments)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	OverloadResolutionCandidate candidate(method.get(), false);
	// The C# `InferredTypes` is null before inference runs; the port's candidate carries a
	// never-null vector, so the merged method list is PRESENT-and-empty (distinct from
	// `nullopt` -- an empty list substitutes every index out of range, per the
	// `TypeParameterSubstitution` semantics).
	TypeParameterSubstitution result = GetSubstitution(candidate);

	EXPECT_FALSE(result.ClassTypeArguments().has_value());
	ASSERT_TRUE(result.MethodTypeArguments().has_value());
	EXPECT_TRUE(result.MethodTypeArguments()->empty());
}

// ---- The public 3-arg ValidateConstraints overload (the conversions-from-the-owner wiring) ----

TEST(ValidateMethodConstraintsTest, PublicOverloadResolvesConversionsFromOwnerCompilation)
{
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	tp->SetDirectBaseTypes({ObjectDef()});
	auto arg = DerivedDef();
	// The `where T : object` constraint needs the implicit-reference arm of
	// `IsConstraintConvertible`, which needs the `CSharpConversions` the public overload
	// resolves via `CSharpConversions.Get(owner->Compilation())`.
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, nullptr));
}

TEST(ValidateMethodConstraintsTest, PublicOverloadRejectsVoidArgument)
{
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	ILSpy::Decompiler::TypeSystem::SpecialType voidType(TypeKind::Void);
	auto arg = DerivedDef();
	// The internal static's outright `void` rejection delegates through the public overload.
	EXPECT_FALSE(ValidateConstraints(*tp, voidType, nullptr));
}

TEST(ValidateMethodConstraintsTest, PublicOverloadNullOwnerYieldsFalse)
{
	// A plain `LookupTypeParameter` returns a null `Owner` -- the dummy-type-parameter shape.
	// The C# dereferences `typeParameter.Owner.Compilation` unconditionally (an NRE); the port's
	// documented safe fallback returns `false` (the constraints cannot be validated without a
	// compilation).
	auto tp = std::make_shared<LookupTypeParameter>("T");
	auto arg = DerivedDef();
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr));
}

TEST(ValidateMethodConstraintsTest, PublicOverloadAppliesSubstitutionToConstraints)
{
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	// A constraint that references another METHOD type parameter: `where T : U`.
	auto u = std::make_shared<VisitableTypeParameter>("U");
	tp->SetDirectBaseTypes({u});
	auto arg = DerivedDef();
	// Without the substitution the constraint stays the type parameter `U`: a `Derived` class is
	// not constraint-convertible to a type parameter (the type-parameter arm requires the
	// FROM type to be the parameter).
	EXPECT_FALSE(ValidateConstraints(*tp, *arg, nullptr));
	// With the substitution mapping `U` (index 0, method owner -- the `LookupTypeParameter`
	// defaults) to `System.Object`, the constraint becomes `System.Object`, which `Derived` IS
	// constraint-convertible to -- the substitution flows through the public overload.
	TypeParameterSubstitution substitution(std::nullopt, std::vector<ITypePtr>{ObjectDef()});
	EXPECT_TRUE(ValidateConstraints(*tp, *arg, &substitution));
}

// ---- ValidateMethodConstraints: the guards ----

TEST(ValidateMethodConstraintsTest, TypeInferenceFailedSkipsConstraintCheck)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	tp->SetHasReferenceTypeConstraint(true); // a constraint that WOULD fail below
	method->SetTypeParameters({tp.get()});
	OverloadResolutionCandidate candidate(method.get(), false);
	// The degenerate post-failure shape: the inferred types were never populated.
	candidate.InferredTypes() = {};
	candidate.Errors() = OverloadResolutionErrors::TypeInferenceFailed;

	// The constraints are NOT checked after a failed inference (the soft `None`, not an error).
	EXPECT_EQ(ValidateMethodConstraints(candidate), OverloadResolutionErrors::None);
}

TEST(ValidateMethodConstraintsTest, NonGenericCandidateReturnsNone)
{
	// No method type parameters (the shared `TestMethod` default) -- the method isn't generic,
	// so there is nothing to validate (even with a populated `InferredTypes`).
	auto method = std::make_shared<TestMethod>("M", Compilation());
	OverloadResolutionCandidate candidate(method.get(), false);
	candidate.InferredTypes() = {DerivedDef()};

	EXPECT_EQ(ValidateMethodConstraints(candidate), OverloadResolutionErrors::None);
}

// ---- ValidateMethodConstraints: the per-parameter constraint check ----

TEST(ValidateMethodConstraintsTest, UnconstrainedGenericCandidateSatisfiesConstraints)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	method->SetTypeParameters({tp.get()});
	OverloadResolutionCandidate candidate(method.get(), false);
	candidate.InferredTypes() = {DerivedDef()};

	EXPECT_EQ(ValidateMethodConstraints(candidate), OverloadResolutionErrors::None);
}

TEST(ValidateMethodConstraintsTest, ClassConstraintViolationYieldsMethodConstraintsNotSatisfied)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	tp->SetHasReferenceTypeConstraint(true);
	method->SetTypeParameters({tp.get()});
	OverloadResolutionCandidate candidate(method.get(), false);
	// A definite value-type argument fails the `class` constraint.
	candidate.InferredTypes() = {Int32ValueDef()};

	EXPECT_EQ(ValidateMethodConstraints(candidate),
	          OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

TEST(ValidateMethodConstraintsTest, NullOwnerTypeParameterYieldsMethodConstraintsNotSatisfied)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	// A plain `LookupTypeParameter` (a null `Owner`, the dummy shape) -- the public overload's
	// documented `false` fallback flows through the candidate-level check as the soft
	// `MethodConstraintsNotSatisfied`.
	auto tp = std::make_shared<LookupTypeParameter>("T");
	method->SetTypeParameters({tp.get()});
	OverloadResolutionCandidate candidate(method.get(), false);
	candidate.InferredTypes() = {DerivedDef()};

	EXPECT_EQ(ValidateMethodConstraints(candidate),
	          OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

TEST(ValidateMethodConstraintsTest, DegeneratePreInferenceCandidateYieldsMethodConstraintsNotSatisfied)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	method->SetTypeParameters({tp.get()});
	OverloadResolutionCandidate candidate(method.get(), false);
	// The pre-inference shape: the type parameters exist but the inferred types were never
	// populated (the C# indexes the null `InferredTypes` array unconditionally and NREs; the
	// port's documented fallback yields the soft unverifiable verdict).
	candidate.InferredTypes() = {};

	EXPECT_EQ(ValidateMethodConstraints(candidate),
	          OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

TEST(ValidateMethodConstraintsTest, NullInferredTypeEntryYieldsMethodConstraintsNotSatisfied)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	auto tp = std::make_shared<OwnedTypeParameter>("T");
	tp->SetOwner(OwnerEntity());
	method->SetTypeParameters({tp.get()});
	OverloadResolutionCandidate candidate(method.get(), false);
	// A null inferred-type entry (the C# would pass the null `IType` to `ValidateConstraints`,
	// which throws `ArgumentNullException`; the port yields the soft unverifiable verdict).
	candidate.InferredTypes() = {ITypePtr()};

	EXPECT_EQ(ValidateMethodConstraints(candidate),
	          OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

// ---- ValidateMethodConstraints: the substitution wiring at the candidate level ----

namespace {
// Builds a two-parameter generic candidate: `T0` unconstrained, `T1` constrained by
// `where T1 : U` (a constraint referencing another method type parameter). The substituted
// constraint maps `U` (index 0, method owner -- the `LookupTypeParameter` defaults) to
// `InferredTypes[0]`, so the check for `T1` is `IsConstraintConvertible(inferred1, inferred0)`
// -- the crux that pins the merged substitution wiring.
std::shared_ptr<TestMethod> MakeTwoParameterMethod(
    std::shared_ptr<OwnedTypeParameter>& tp0Out,
    std::shared_ptr<OwnedTypeParameter>& tp1Out) {
	auto method = std::make_shared<TestMethod>("M", Compilation());
	static auto u = std::make_shared<VisitableTypeParameter>("U"); // index 0, method owner
	auto tp0 = std::make_shared<OwnedTypeParameter>("T0");
	tp0->SetOwner(OwnerEntity());
	auto tp1 = std::make_shared<OwnedTypeParameter>("T1");
	tp1->SetOwner(OwnerEntity());
	tp1->SetDirectBaseTypes({u});
	method->SetTypeParameters({tp0.get(), tp1.get()});
	// The candidate's `TypeParameters()` entries are NON-OWNING raw pointers (the candidate
	// stores `const ITypeParameter*`); the out-params hand the owning handles to the test scope
	// so the type parameters outlive the `ValidateMethodConstraints` call (the dangling-pointer
	// lifetime convention).
	tp0Out = tp0;
	tp1Out = tp1;
	return method;
}
} // namespace

TEST(ValidateMethodConstraintsTest, SubstitutionMapsConstraintTypeParameterToInferredArgument)
{
	std::shared_ptr<OwnedTypeParameter> tp0, tp1; // keep the type parameters alive
	auto method = MakeTwoParameterMethod(tp0, tp1);
	OverloadResolutionCandidate candidate(method.get(), false);
	// `InferredTypes[0] = Object` (so the `where T1 : U` constraint becomes `where T1 : object`)
	// and `InferredTypes[1] = Derived` (which IS constraint-convertible to `object` via the
	// implicit-reference arm). Without the substitution the constraint would stay the type
	// parameter `U` and the check would FAIL.
	candidate.InferredTypes() = {ObjectDef(), DerivedDef()};

	EXPECT_EQ(ValidateMethodConstraints(candidate), OverloadResolutionErrors::None);
}

TEST(ValidateMethodConstraintsTest, SubstitutionMappedConstraintStillRejectsInconvertible)
{
	std::shared_ptr<OwnedTypeParameter> tp0, tp1; // keep the type parameters alive
	auto method = MakeTwoParameterMethod(tp0, tp1);
	OverloadResolutionCandidate candidate(method.get(), false);
	// `InferredTypes[0] = IUnrelated` (the `where T1 : U` constraint becomes
	// `where T1 : IUnrelated`) and `InferredTypes[1] = Derived` -- which is NOT
	// constraint-convertible to the unrelated interface. The loop scans PAST the satisfied
	// first parameter and reports the second parameter's violation.
	candidate.InferredTypes() = {InterfaceDef(), DerivedDef()};

	EXPECT_EQ(ValidateMethodConstraints(candidate),
	          OverloadResolutionErrors::MethodConstraintsNotSatisfied);
}

} // namespace
