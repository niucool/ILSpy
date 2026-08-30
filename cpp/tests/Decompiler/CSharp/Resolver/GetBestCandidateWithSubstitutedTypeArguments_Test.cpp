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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR IN CONNECTION WITH THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `Detail::GetBestCandidateWithSubstitutedTypeArguments` (OverloadResolution.cs line
// 1153) -- the second consumer of the merged `GetSubstitution` substitution (the first is
// `ValidateMethodConstraints`, the sibling `ValidateMethodConstraints_Test.cpp`):
//
//     public IParameterizedMember GetBestCandidateWithSubstitutedTypeArguments()
//     {
//         if (bestCandidate == null)
//             return null;
//         IMethod method = bestCandidate.Member as IMethod;
//         if (method != null && method.TypeParameters.Count > 0)
//         {
//             return ((IMethod)method.MemberDefinition).Specialize(GetSubstitution(bestCandidate));
//         }
//         else
//         {
//             return bestCandidate.Member;
//         }
//     }
//
// The tests pin:
//  (a) a null best candidate -> null (no dereference);
//  (b) a non-`IMethod` member (an indexer property is a parameterized member but not a method)
//      -> the member as-is;
//  (c) a non-generic method (empty `TypeParameters`) -> the member as-is, `Specialize` NOT called;
//  (d) a generic method -> `Specialize` is called on the MEMBER DEFINITION (not on the specialized
//      member) with the merged `GetSubstitution` substitution (the member's class type arguments
//      + the candidate's inferred method type arguments), and the specialized result is returned;
//  (e) an unspecialized generic method (the definition IS the member) -> `Specialize` called on it;
//  (f) the degenerate shapes the C# hard cast would throw on (a definition that is not an
//      `IMethod`, a null `MemberDefinition` -- both impossible for a real method) -> the
//      documented safe fallback returns the member as-is.

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::GetBestCandidateWithSubstitutedTypeArguments;
using ILSpy::Decompiler::CSharp::Resolver::Detail::GetSubstitution;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IProperty;
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

ITypePtr ObjectDef() {
	static auto d = std::make_shared<LookupTypeDefinition>(
		"Object", "",
		FullTypeName(TopLevelTypeName("", "Object", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::Object);
	return d;
}

ITypePtr DerivedDef() {
	static auto d = std::make_shared<LookupTypeDefinition>(
		"Derived", "",
		FullTypeName(TopLevelTypeName("", "Derived", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
	return d;
}

// A `LookupMethod` with a configurable type-parameter list, member definition, own substitution,
// and `Specialize` result. The `Specialize` override RECORDS the call (a counter + a copy of the
// received substitution -- copy-on-call, the real `SpecializedMethod::Specialize` contract: the
// caller's substitution is a stack local valid only during the call) so the tests can pin WHICH
// member `Specialize` is invoked on and WHICH substitution it receives. The defaults preserve the
// shared stub's behavior (`MemberDefinition() == this`, empty `TypeParameters`, a null
// `Substitution`, `Specialize` returning `this`).
class TestMethod : public LookupMethod {
public:
	using LookupMethod::LookupMethod;
	void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
	void SetSubstitution(const TypeParameterSubstitution* s) { substitution_ = s; }
	void SetMemberDefinition(const ILSpy::Decompiler::TypeSystem::IMember* m) { memberDefinition_ = m; }
	void SetSpecialized(const IMethod* m) { specialized_ = m; }
	int SpecializeCallCount() const { return specializeCallCount_; }
	bool HasReceivedSubstitution() const { return receivedSubstitution_.has_value(); }
	const TypeParameterSubstitution& ReceivedSubstitution() const { return *receivedSubstitution_; }
	std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
	const TypeParameterSubstitution* Substitution() const override { return substitution_; }
	const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override
	{
		return memberDefinition_ != nullptr ? memberDefinition_ : this;
	}
	const IMethod* Specialize(const TypeParameterSubstitution* substitution) const override
	{
		specializeCallCount_++;
		// Copy-on-call (the real `SpecializedMethod::Specialize` dereferences only during the
		// call): record the received substitution before the caller's stack local dies.
		receivedSubstitution_ = (substitution != nullptr)
			? std::optional<TypeParameterSubstitution>(*substitution)
			: std::nullopt;
		return specialized_ != nullptr ? specialized_ : this;
	}
private:
	std::vector<const ITypeParameter*> typeParameters_;
	const TypeParameterSubstitution* substitution_ = nullptr;
	const ILSpy::Decompiler::TypeSystem::IMember* memberDefinition_ = nullptr;
	const IMethod* specialized_ = nullptr;
	mutable int specializeCallCount_ = 0;
	mutable std::optional<TypeParameterSubstitution> receivedSubstitution_;
};

// A minimal `IProperty` stub (the `MemberLookupLookupIndexers_Test.cpp` precedent) -- a
// parameterized member that is NOT an `IMethod` (the C# `bestCandidate.Member as IMethod` null
// shape: an indexer is an `IParameterizedMember` but not a method).
class TestProperty : public IProperty {
public:
	TestProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
		: name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation) {}

	::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
	{
		return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
	}
	std::string Name() const override { return name_; }
	std::string FullName() const override { return name_; }
	std::string ReflectionName() const override { return name_; }
	std::string Namespace() const override { return {}; }
	const ICompilation& Compilation() const override { return compilation_; }
	std::uint32_t MetadataToken() const override { return 0; }
	const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
	{
		return nullptr;
	}
	ITypePtr DeclaringType() const override { return {}; }
	const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
	std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
	{
		return {};
	}
	bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
	const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
		ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
	{
		return nullptr;
	}
	::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
	{
		return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
	}
	bool IsStatic() const override { return false; }
	bool IsAbstract() const override { return false; }
	bool IsSealed() const override { return false; }
	const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
	const IType& ReturnType() const override { return *returnType_; }
	std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
	ExplicitlyImplementedInterfaceMembers() const override
	{
		return {};
	}
	bool IsExplicitInterfaceImplementation() const override { return false; }
	bool IsVirtual() const override { return false; }
	bool IsOverride() const override { return false; }
	bool IsOverridable() const override { return false; }
	const TypeParameterSubstitution* Substitution() const override { return &identitySubst_; }
	const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
		const TypeParameterSubstitution*) const override
	{
		return this;
	}
	bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const TypeVisitor*) const override
	{
		return obj == this;
	}
	std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override
	{
		return {};
	}
	bool CanGet() const override { return true; }
	bool CanSet() const override { return false; }
	bool IsIndexer() const override { return true; }
	bool ReturnTypeIsRefReadOnly() const override { return false; }
	const IMethod* Getter() const override { return nullptr; }
	const IMethod* Setter() const override { return nullptr; }

private:
	std::string name_;
	ITypePtr returnType_;
	const ICompilation& compilation_;
	mutable TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// One generic-method type parameter (the `TypeParameters.Count > 0` shape; any non-null
// `ITypeParameter` works -- the helper only reads the count).
std::shared_ptr<LookupTypeParameter> TypeParam() {
	static auto tp = std::make_shared<LookupTypeParameter>("T");
	return tp;
}

// ---- GetBestCandidateWithSubstitutedTypeArguments ----

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, NullBestCandidateYieldsNull)
{
	std::shared_ptr<OverloadResolutionCandidate> bestCandidate;

	EXPECT_EQ(GetBestCandidateWithSubstitutedTypeArguments(bestCandidate), nullptr);
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, NonMethodMemberReturnsMemberAsIs)
{
	auto property = std::make_shared<TestProperty>("Item", ObjectDef(), Compilation());
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(property.get(), false);

	const IParameterizedMember* result = GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	EXPECT_EQ(result, property.get());
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, NonGenericMethodReturnsMemberAsIsWithoutSpecializing)
{
	auto method = std::make_shared<TestMethod>("M", Compilation());
	// The default `TypeParameters()` is empty (the non-generic shape); the shared stub's
	// `Specialize` would return `this`, so the call count distinguishes the as-is return from a
	// `Specialize(this)` return.
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(method.get(), false);

	const IParameterizedMember* result = GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	EXPECT_EQ(result, method.get());
	EXPECT_EQ(method->SpecializeCallCount(), 0);
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, GenericMethodSpecializesTheMemberDefinition)
{
	// The best candidate's member is a SPECIALIZED generic method (it carries its own
	// `Substitution`); the definition (a distinct method) is the member `Specialize` is called on.
	auto definition = std::make_shared<TestMethod>("M", Compilation());
	definition->SetTypeParameters({TypeParam().get()});
	auto specialized = std::make_shared<TestMethod>("M<string>", Compilation());
	specialized->SetTypeParameters({TypeParam().get()});
	specialized->SetMemberDefinition(definition.get());
	definition->SetSpecialized(specialized.get());
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(specialized.get(), false);

	const IParameterizedMember* result = GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	// `Specialize` was called on the DEFINITION (not on the specialized member)...
	EXPECT_EQ(definition->SpecializeCallCount(), 1);
	EXPECT_EQ(specialized->SpecializeCallCount(), 0);
	// ...and the configured specialized result is returned.
	EXPECT_EQ(result, specialized.get());
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, SubstitutionPassedToSpecializeIsTheGetSubstitutionMerge)
{
	// The merged substitution the `Specialize` call receives: the member's own CLASS type
	// arguments + the candidate's INFERRED method type arguments (the `GetSubstitution`
	// merge-not-compose contract, here pinned at the `Specialize` call boundary).
	auto definition = std::make_shared<TestMethod>("M", Compilation());
	definition->SetTypeParameters({TypeParam().get()});
	auto specialized = std::make_shared<TestMethod>("M<string>", Compilation());
	specialized->SetTypeParameters({TypeParam().get()});
	specialized->SetMemberDefinition(definition.get());
	// The member's own substitution carries CLASS type arguments (the specialized-method shape).
	TypeParameterSubstitution memberSubstitution(std::vector<ITypePtr>{ObjectDef()}, std::nullopt);
	specialized->SetSubstitution(&memberSubstitution);
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(specialized.get(), false);
	bestCandidate->InferredTypes() = {DerivedDef()};

	GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	ASSERT_TRUE(definition->HasReceivedSubstitution());
	const TypeParameterSubstitution& received = definition->ReceivedSubstitution();
	// The class list comes from the member substitution...
	ASSERT_TRUE(received.ClassTypeArguments().has_value());
	ASSERT_EQ(received.ClassTypeArguments()->size(), 1u);
	EXPECT_EQ(received.ClassTypeArguments()->at(0).get(), ObjectDef().get());
	// ...and the method list from the candidate's inferred types.
	ASSERT_TRUE(received.MethodTypeArguments().has_value());
	ASSERT_EQ(received.MethodTypeArguments()->size(), 1u);
	EXPECT_EQ(received.MethodTypeArguments()->at(0).get(), DerivedDef().get());
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, UnspecializedGenericMethodSpecializesItself)
{
	// A non-specialized generic method IS its own member definition (the `MemberDefinition() ==
	// this` default), so `Specialize` is called on the member itself.
	auto method = std::make_shared<TestMethod>("M", Compilation());
	method->SetTypeParameters({TypeParam().get()});
	auto resultMethod = std::make_shared<TestMethod>("M<int>", Compilation());
	method->SetSpecialized(resultMethod.get());
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(method.get(), false);

	const IParameterizedMember* result = GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	EXPECT_EQ(method->SpecializeCallCount(), 1);
	EXPECT_EQ(result, resultMethod.get());
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, DefinitionNotIMethodFallsBackToMember)
{
	// A degenerate shape the C# hard cast `((IMethod)method.MemberDefinition)` would throw on
	// (a method's definition is always a method in practice): the documented safe fallback
	// returns the member as-is instead of the UB.
	auto property = std::make_shared<TestProperty>("Item", ObjectDef(), Compilation());
	auto method = std::make_shared<TestMethod>("M", Compilation());
	method->SetTypeParameters({TypeParam().get()});
	method->SetMemberDefinition(property.get()); // a non-IMethod definition (degenerate)
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(method.get(), false);

	const IParameterizedMember* result = GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	EXPECT_EQ(result, method.get());
}

TEST(GetBestCandidateWithSubstitutedTypeArgumentsTest, NullMemberDefinitionFallsBackToMember)
{
	// A degenerate null `MemberDefinition` (the C# contract is "never null" -- "Returns `this` if
	// this is not a specialized member"): the documented safe fallback returns the member as-is.
	auto method = std::make_shared<TestMethod>("M", Compilation());
	method->SetTypeParameters({TypeParam().get()});
	method->SetMemberDefinition(nullptr); // the degenerate null definition
	auto bestCandidate = std::make_shared<OverloadResolutionCandidate>(method.get(), false);

	const IParameterizedMember* result = GetBestCandidateWithSubstitutedTypeArguments(bestCandidate);

	EXPECT_EQ(result, method.get());
}

} // namespace
