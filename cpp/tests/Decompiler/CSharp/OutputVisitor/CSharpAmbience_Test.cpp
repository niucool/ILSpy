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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `CSharpAmbience` (OutputVisitor/CSharpAmbience.hpp) -- the port of
// CSharpAmbience.cs, the C#-syntax ambience composing TypeSystemAstBuilder with the
// TokenWriter/CSharpOutputVisitor stack under the ConversionFlags mask.
//
// The load-bearing cruxes:
//  (a) the flag-driven rendering matrix: the ShowDefinitionKeyword keywords
//      (class/struct/record struct/delegate/namespace), the return type before vs
//      after the parameter list (PlaceReturnTypeAfterParameterList), the
//      ShowParameterList kinds, the ShowBody `{ get; set; }` property shape (with the
//      `init` upgrade under SupportInitAccessors), and the PrintModifiers keyword
//      chain;
//  (b) the member-name dispatch: the constructor/destructor declaring-type names
//      (WriteQualifiedName), the operator keyword arms (`implicit` / `explicit` /
//      `explicit checked` / the token / the unsupported-checked-operator NameToken
//      fallback quirk), the indexer `this` keyword, and the ShowDeclaringType prefix
//      with the LocalFunctionMethod exclusion;
//  (c) the parameter-list rendering: the node-carried parameters vs the
//      parameterized-property symbol-carried parameters, the modifier/default-value
//      strips under their flags, and the ShowConstantValues-default-TRUE quirk (the
//      default expression renders with `=5`, no spaces -- SpaceAroundAssignment is
//      false in the CreateEmpty policy, and the ShowParameterDefaultValues-off arm
//      strips it via Detach);
//  (d) the string entries: ConvertVariable's TrimEnd (the `;`/line-break strip),
//      ConvertType's UseFullyQualifiedEntityNames flag (NOT UseFullyQualifiedTypeNames
//      -- a C# quirk), ConvertConstantValue's PrintPrimitiveValue delegation, and
//      WrapComment;
//  (e) the deferred arms are pinned at their faithful fallbacks: IsExtension is false
//      for every ported type definition (ExtensionInfo() is always null), and the
//      fixed-field return-type arm renders the plain return type.
//
// The stub shapes: AmbienceMethod/AmbienceProperty/AmbienceParameter/AmbienceVariable
// carry the established per-file stub surfaces (the iteration-128/131 patterns
// extended with the setters this region reads); every method-shaped stub receives a
// make_shared'd return type because AddResolveResultAnnotations is TRUE through the
// ambience (the MemberResolveResult annotation eagerly computes the type through
// shared_from_this over ReturnType -- the bad_weak_ptr trap), and MethodHostType (the
// GetDelegateInvokeMethod_Test pattern) backs the delegate definition with a
// filter-faithful GetMethods.

#include "Decompiler/CSharp/OutputVisitor/CSharpAmbience.hpp"

#include "Decompiler/CSharp/OutputVisitor/CSharpFormattingOptions.hpp"
#include "Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.hpp"
#include "Decompiler/CSharp/OutputVisitor/TextWriterTokenWriter.hpp"

#include "Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace OF = ::ILSpy::Decompiler::Output;
namespace Syn = ::ILSpy::Decompiler::CSharp::Syntax;

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IProperty;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::CSharp::OutputVisitor::CSharpAmbience;
using ILSpy::Decompiler::CSharp::OutputVisitor::CSharpFormattingOptions;
using ILSpy::Decompiler::CSharp::OutputVisitor::TextWriterTokenWriter;
using ILSpy::Decompiler::CSharp::OutputVisitor::TokenWriter;

// ---------------------------------------------------------------------------
// The shared fixture: a per-program LookupCompilation and the shared primitive
// definitions every test builds its stubs over (the type-cache model -- the
// definitions ARE the instances the renderers resolve through).
// ---------------------------------------------------------------------------

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

std::shared_ptr<LookupTypeDefinition> MakeIntDef() {
	static auto def = std::make_shared<LookupTypeDefinition>(
		"Int32", "System", FullTypeName(TopLevelTypeName("System", "Int32", 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::Int32);
	return def;
}

std::shared_ptr<LookupTypeDefinition> MakeClassDef(std::string name, std::string ns = {}) {
	return std::make_shared<LookupTypeDefinition>(
		std::move(name), std::move(ns),
		FullTypeName(TopLevelTypeName(ns, name, 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
}

// A configurable `IMethod` stub (the iteration-131 TestMemberMethod pattern extended
// with the setters the ambience's member-name paths read: the rebindable
// DeclaringType, the type-parameter list, the init-only flag, and the
// explicit-interface pair).
class AmbienceMethod : public IMethod {
public:
	AmbienceMethod(std::string name, ITypePtr returnType, const ICompilation& compilation)
		: name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation) {}

	void SetSymbolKind(TS::SymbolKind k) { kind_ = k; }
	void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
	void SetIsStatic(bool v) { isStatic_ = v; }
	void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }
	void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }
	void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
	void SetIsExplicitInterfaceImplementation(bool v) { isExplicit_ = v; }
	void SetExplicitlyImplementedInterfaceMembers(std::vector<const IMember*> m) {
		explicitlyImplemented_ = std::move(m);
	}
	void SetIsInitOnly(bool v) { isInitOnly_ = v; }

	TS::SymbolKind SymbolKind() const override { return kind_; }
	std::string Name() const override { return name_; }
	std::string FullName() const override { return name_; }
	std::string ReflectionName() const override { return name_; }
	std::string Namespace() const override { return {}; }
	const ICompilation& Compilation() const override { return compilation_; }
	std::uint32_t MetadataToken() const override { return 0; }
	const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
	ITypePtr DeclaringType() const override { return declaringType_; }
	const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
	std::vector<const IAttribute*> GetAttributes() const override { return {}; }
	bool HasAttribute(KnownAttribute) const override { return false; }
	const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
	TS::Accessibility Accessibility() const override { return accessibility_; }
	bool IsStatic() const override { return isStatic_; }
	bool IsAbstract() const override { return false; }
	bool IsSealed() const override { return false; }
	const IMember* MemberDefinition() const override { return this; }
	const IType& ReturnType() const override { return *returnType_; }
	std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
		return explicitlyImplemented_;
	}
	bool IsExplicitInterfaceImplementation() const override { return isExplicit_; }
	bool IsVirtual() const override { return false; }
	bool IsOverride() const override { return false; }
	bool IsOverridable() const override { return false; }
	const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
	const IMethod* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
	bool Equals(const IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
	std::vector<const IParameter*> Parameters() const override { return parameters_; }
	std::vector<const IAttribute*> GetReturnTypeAttributes() const override { return {}; }
	bool ReturnTypeIsRefReadOnly() const override { return false; }
	bool IsInitOnly() const override { return isInitOnly_; }
	bool ThisIsRefReadOnly() const override { return false; }
	std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }
	std::vector<ITypePtr> TypeArguments() const override { return {}; }
	bool IsExtensionMethod() const override { return false; }
	bool IsLocalFunction() const override { return false; }
	bool IsConstructor() const override { return kind_ == TS::SymbolKind::Constructor; }
	bool IsDestructor() const override { return kind_ == TS::SymbolKind::Destructor; }
	bool IsOperator() const override { return kind_ == TS::SymbolKind::Operator; }
	bool HasBody() const override { return false; }
	bool IsAccessor() const override { return false; }
	const IMember* AccessorOwner() const override { return nullptr; }
	TS::MethodSemanticsAttributes AccessorKind() const override { return TS::MethodSemanticsAttributes::None; }
	const IMethod* ReducedFrom() const override { return nullptr; }

private:
	std::string name_;
	ITypePtr returnType_;
	const ICompilation& compilation_;
	ITypePtr declaringType_;
	std::vector<const ITypeParameter*> typeParameters_;
	std::vector<const IParameter*> parameters_;
	std::vector<const IMember*> explicitlyImplemented_;
	TS::SymbolKind kind_ = TS::SymbolKind::Method;
	TS::Accessibility accessibility_ = TS::Accessibility::Public;
	bool isStatic_ = false;
	bool isExplicit_ = false;
	bool isInitOnly_ = false;
};

// A configurable `IParameter` stub (the iteration-131 TestParameter pattern extended
// with the reference-kind / optional / constant-value setters the parameter-list
// rendering reads).
class AmbienceParameter : public IParameter {
public:
	explicit AmbienceParameter(ITypePtr type, std::string name = "p")
		: name_(std::move(name)), type_(std::move(type)) {}

	void SetReferenceKind(TS::ReferenceKind k) { referenceKind_ = k; }
	void SetIsOptional(bool v) { isOptional_ = v; }
	void SetHasConstantValueInSignature(bool v) { hasConstantValueInSignature_ = v; }
	void SetConstantValue(std::any v) { constantValue_ = std::move(v); }

	TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
	std::string Name() const override { return name_; }
	const IType& Type() const override { return *type_; }
	bool IsConst() const override { return false; }
	std::any GetConstantValue(bool) const override { return constantValue_; }
	TS::ReferenceKind ReferenceKind() const override { return referenceKind_; }
	const TS::IParameterizedMember* Owner() const override { return nullptr; }
	bool IsParams() const override { return false; }
	bool IsOptional() const override { return isOptional_; }
	bool HasConstantValueInSignature() const override { return hasConstantValueInSignature_; }
	TS::LifetimeAnnotation Lifetime() const override { return {}; }
	std::vector<const IAttribute*> GetAttributes() const override { return {}; }

private:
	std::string name_;
	ITypePtr type_;
	TS::ReferenceKind referenceKind_ = TS::ReferenceKind::None;
	bool isOptional_ = false;
	bool hasConstantValueInSignature_ = false;
	std::any constantValue_;
};

// A configurable `IProperty` stub (the iteration-128 TestProperty pattern extended with
// the SymbolKind setter -- an indexer property reports SymbolKind::Indexer -- and the
// IsIndexer tracking the C# AbstractProperty derives from it).
class AmbienceProperty : public IProperty {
public:
	AmbienceProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
		: name_(std::move(name)), returnType_(std::move(returnType)), compilation_(compilation) {}

	void SetSymbolKind(TS::SymbolKind k) { kind_ = k; }
	void SetGetter(const IMethod* m) { getter_ = m; }
	void SetSetter(const IMethod* m) { setter_ = m; }
	void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }

	TS::SymbolKind SymbolKind() const override { return kind_; }
	std::string Name() const override { return name_; }
	std::string FullName() const override { return name_; }
	std::string ReflectionName() const override { return name_; }
	std::string Namespace() const override { return {}; }
	const ICompilation& Compilation() const override { return compilation_; }
	std::uint32_t MetadataToken() const override { return 0; }
	const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
	ITypePtr DeclaringType() const override { return {}; }
	const ::ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
	std::vector<const IAttribute*> GetAttributes() const override { return {}; }
	bool HasAttribute(KnownAttribute) const override { return false; }
	const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
	TS::Accessibility Accessibility() const override { return accessibility_; }
	bool IsStatic() const override { return false; }
	bool IsAbstract() const override { return false; }
	bool IsSealed() const override { return false; }
	const IMember* MemberDefinition() const override { return this; }
	const IType& ReturnType() const override { return *returnType_; }
	std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
	bool IsExplicitInterfaceImplementation() const override { return false; }
	bool IsVirtual() const override { return false; }
	bool IsOverride() const override { return false; }
	bool IsOverridable() const override { return false; }
	const TS::TypeParameterSubstitution* Substitution() const override { return nullptr; }
	const IMember* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
	bool Equals(const IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
	std::vector<const IParameter*> Parameters() const override { return parameters_; }
	bool CanGet() const override { return getter_ != nullptr; }
	bool CanSet() const override { return setter_ != nullptr; }
	const IMethod* Getter() const override { return getter_; }
	const IMethod* Setter() const override { return setter_; }
	bool IsIndexer() const override { return kind_ == TS::SymbolKind::Indexer; }
	bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
	std::string name_;
	ITypePtr returnType_;
	const ICompilation& compilation_;
	const IMethod* getter_ = nullptr;
	const IMethod* setter_ = nullptr;
	TS::SymbolKind kind_ = TS::SymbolKind::Property;
	TS::Accessibility accessibility_ = TS::Accessibility::Public;
	std::vector<const IParameter*> parameters_;
};

// A configurable `IVariable` stub (the iteration-123 TestVariable pattern).
class AmbienceVariable : public IVariable {
public:
	AmbienceVariable(std::string name, ITypePtr type)
		: name_(std::move(name)), type_(std::move(type)) {}

	void SetIsConst(bool v) { isConst_ = v; }
	void SetConstantValue(std::any v) { constantValue_ = std::move(v); }

	TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
	std::string Name() const override { return name_; }
	const IType& Type() const override { return *type_; }
	bool IsConst() const override { return isConst_; }
	std::any GetConstantValue(bool) const override { return constantValue_; }

private:
	std::string name_;
	ITypePtr type_;
	bool isConst_ = false;
	std::any constantValue_;
};

// A `LookupTypeDefinition` whose `GetMethods(filter, options)` returns a configured
// list, applying the filter faithfully (the GetDelegateInvokeMethod_Test MethodHostType
// pattern -- the delegate renderer resolves the Invoke through this surface).
class MethodHostType : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;
	void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }

	std::vector<const IMethod*> GetMethods(
		std::function<bool(const IMethod*)> filter = nullptr,
		GetMemberOptions options = GetMemberOptions::None) const override {
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

// The shared parameter the method renderings use (kept alive in a static vector -- the
// stubs hold non-owning pointers).
const IParameter* MakeIntParam(std::string name = "a") {
	static std::vector<std::shared_ptr<AmbienceParameter>> keep;
	auto p = std::make_shared<AmbienceParameter>(MakeIntDef(), std::move(name));
	keep.push_back(p);
	return p.get();
}

// The shared getter/setter accessor methods (a shared make_shared'd return type -- the
// MemberResolveResult annotation over the accessor computes the type through
// shared_from_this over ReturnType).
std::shared_ptr<AmbienceMethod> MakeAccessor(std::string name) {
	return std::make_shared<AmbienceMethod>(std::move(name), MakeIntDef(), Compilation());
}

// A local class definition for the member/namespace-qualified renderings.
std::shared_ptr<LookupTypeDefinition> MakeHostDef(std::string name) {
	static std::vector<std::shared_ptr<LookupTypeDefinition>> keep;
	auto def = std::make_shared<LookupTypeDefinition>(
		std::move(name), "", FullTypeName(TopLevelTypeName("", name, 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
	keep.push_back(def);
	return def;
}

// ---------------------------------------------------------------------------
// The ConversionFlags property (the IAmbience override pair)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceConversionFlagsTest, DefaultsToNone)
{
	CSharpAmbience ambience;
	EXPECT_EQ(ambience.ConversionFlags(), OF::ConversionFlags::None);
}

TEST(CSharpAmbienceConversionFlagsTest, SetAndGetRoundTrip)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowReturnType);
	EXPECT_EQ(ambience.ConversionFlags(),
		OF::ConversionFlags::ShowParameterList | OF::ConversionFlags::ShowReturnType);
}

TEST(CSharpAmbienceConversionFlagsTest, DispatchesThroughTheIAmbienceBase)
{
	CSharpAmbience ambience;
	OF::IAmbience& base = ambience;
	base.ConversionFlags(OF::ConversionFlags::All);
	EXPECT_EQ(base.ConversionFlags(), OF::ConversionFlags::All);
	// The virtual dispatch of the remaining interface members through the base.
	EXPECT_EQ(base.WrapComment("foo"), "// foo");
	EXPECT_EQ(base.ConvertConstantValue(Syn::PrimitiveValue{std::int32_t{7}}), "7");
}

// ---------------------------------------------------------------------------
// IsExtension (the deferred-arm faithful fallback)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceIsExtensionTest, FalseWithoutTheSupportExtensionDeclarationsFlag)
{
	CSharpAmbience ambience;
	auto typeDef = MakeHostDef("C");
	CSharpAmbience::ExtensionGroup group;
	EXPECT_FALSE(ambience.IsExtension(typeDef.get(), group));
}

TEST(CSharpAmbienceIsExtensionTest, FalseForNullTypeDefinition)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::SupportExtensionDeclarations);
	CSharpAmbience::ExtensionGroup group;
	EXPECT_FALSE(ambience.IsExtension(nullptr, group));
	EXPECT_EQ(group.Marker, nullptr);
}

TEST(CSharpAmbienceIsExtensionTest, FalseForEveryPortedTypeDefinition)
{
	// The DEFERRED ExtensionInfo.IsExtensionMarkerType tail: every ported
	// ITypeDefinition returns nullptr from ExtensionInfo(), so the C#
	// `extensionInfo != null &&` short-circuit is false for every port-reachable
	// shape -- IsExtension reports false and leaves the group defaulted.
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::SupportExtensionDeclarations);
	auto typeDef = MakeHostDef("C");
	CSharpAmbience::ExtensionGroup group;
	EXPECT_FALSE(ambience.IsExtension(typeDef.get(), group));
	EXPECT_EQ(group.Marker, nullptr);
	EXPECT_TRUE(group.TypeParameters.empty());
}

// ---------------------------------------------------------------------------
// ShowParameterList (the when-clause switch)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceShowParameterListTest, MethodReturnsTheFlag)
{
	CSharpAmbience ambienceOn;
	ambienceOn.ConversionFlags(OF::ConversionFlags::ShowParameterList);
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	EXPECT_TRUE(ambienceOn.ShowParameterList(*method));

	CSharpAmbience ambienceOff;
	EXPECT_FALSE(ambienceOff.ShowParameterList(*method));
}

TEST(CSharpAmbienceShowParameterListTest, IndexerOperatorConstructorDestructorReturnTheFlag)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList);
	auto indexer = std::make_shared<AmbienceProperty>("Item", MakeIntDef(), Compilation());
	indexer->SetSymbolKind(TS::SymbolKind::Indexer);
	EXPECT_TRUE(ambience.ShowParameterList(*indexer));

	for (TS::SymbolKind kind : {TS::SymbolKind::Operator, TS::SymbolKind::Constructor,
	     TS::SymbolKind::Destructor}) {
		auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
		method->SetSymbolKind(kind);
		EXPECT_TRUE(ambience.ShowParameterList(*method));
	}
}

TEST(CSharpAmbienceShowParameterListTest, DelegateTypeDefinitionReturnsTheFlag)
{
	CSharpAmbience ambienceOn;
	ambienceOn.ConversionFlags(OF::ConversionFlags::ShowParameterList);
	auto host = std::make_shared<MethodHostType>(
		"D", "", FullTypeName(TopLevelTypeName("", "D", 0)),
		TypeKind::Delegate, Accessibility::Public, Compilation(), nullptr);
	EXPECT_TRUE(ambienceOn.ShowParameterList(*host));

	CSharpAmbience ambienceOff;
	EXPECT_FALSE(ambienceOff.ShowParameterList(*host));
}

TEST(CSharpAmbienceShowParameterListTest, NonDelegateTypeDefinitionReturnsFalse)
{
	// A class type definition matches neither TypeDefinition `when` clause and falls
	// to the C# `default: return false` -- even with the flag set.
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::SupportExtensionDeclarations);
	auto typeDef = MakeHostDef("C");
	EXPECT_FALSE(ambience.ShowParameterList(*typeDef));
}

TEST(CSharpAmbienceShowParameterListTest, ParameterizedPropertyReturnsTheFlag)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList);
	auto property = std::make_shared<AmbienceProperty>("P", MakeIntDef(), Compilation());
	property->SetParameters({MakeIntParam()});
	EXPECT_TRUE(ambience.ShowParameterList(*property));
}

TEST(CSharpAmbienceShowParameterListTest, ParameterlessPropertyReturnsFalse)
{
	// The C# `when ((IProperty)e).Parameters.Count > 0` -- a zero-parameter property
	// misses the clause and falls to `default`.
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList);
	auto property = std::make_shared<AmbienceProperty>("P", MakeIntDef(), Compilation());
	EXPECT_FALSE(ambience.ShowParameterList(*property));
}

TEST(CSharpAmbienceShowParameterListTest, VariableReturnsFalse)
{
	// The default arm: a non-parameterized symbol kind never carries a list.
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList);
	auto variable = std::make_shared<AmbienceVariable>("x", MakeIntDef());
	EXPECT_FALSE(ambience.ShowParameterList(*variable));
}

// ---------------------------------------------------------------------------
// ConvertSymbol (the composed renderings)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceConvertSymbolTest, MethodWithStandardFlagsRendersSignature)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowParameterModifiers
		| OF::ConversionFlags::ShowParameterDefaultValues
		| OF::ConversionFlags::ShowReturnType
		| OF::ConversionFlags::ShowModifiers
		| OF::ConversionFlags::ShowAccessibility
		| OF::ConversionFlags::ShowBody);
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	method->SetAccessibility(Accessibility::Public);
	method->SetParameters({MakeIntParam()});

	EXPECT_EQ(ambience.ConvertSymbol(*method), "public int M(int a);");
}

TEST(CSharpAmbienceConvertSymbolTest, MethodWithoutFlagsRendersOnlyTheName)
{
	CSharpAmbience ambience;
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	method->SetParameters({MakeIntParam()});

	EXPECT_EQ(ambience.ConvertSymbol(*method), "M");
}

TEST(CSharpAmbienceConvertSymbolTest, DefinitionKeywordForClass)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDefinitionKeyword);
	auto typeDef = MakeHostDef("C");

	EXPECT_EQ(ambience.ConvertSymbol(*typeDef), "class C");
}

TEST(CSharpAmbienceConvertSymbolTest, DefinitionKeywordForRecordStruct)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDefinitionKeyword
		| OF::ConversionFlags::SupportRecordStructs);
	auto typeDef = std::make_shared<LookupTypeDefinition>(
		"RS", "", FullTypeName(TopLevelTypeName("", "RS", 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr);
	typeDef->SetIsRecord(true);

	EXPECT_EQ(ambience.ConvertSymbol(*typeDef), "record struct RS");
}

TEST(CSharpAmbienceConvertSymbolTest, DefinitionKeywordForDelegateRendersInvokeSignature)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDefinitionKeyword
		| OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowReturnType);
	static auto invoke = std::make_shared<LookupMethod>("Invoke", Compilation());
	invoke->SetReturnType(MakeIntDef());
	invoke->SetParameters({MakeIntParam()});
	auto host = std::make_shared<MethodHostType>(
		"D", "", FullTypeName(TopLevelTypeName("", "D", 0)),
		TypeKind::Delegate, Accessibility::Public, Compilation(), nullptr);
	// The invoke must carry its declaring type definition (the C#
	// `invokeMethod.DeclaringTypeDefinition!` -- the delegate node's name and
	// TypeResolveResult annotation read it; a null definition leaves the NameToken
	// unset and the null-deref crashes).
	invoke->SetDeclaringTypeDefinition(host.get());
	host->SetMethods({invoke.get()});

	EXPECT_EQ(ambience.ConvertSymbol(*host), "delegate int D(int a)");
}

TEST(CSharpAmbienceConvertSymbolTest, DefinitionKeywordForNamespace)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDefinitionKeyword);

	// A minimal INamespace stub (the iteration-131 TestNamespace pattern).
	class TestNamespace : public ILSpy::Decompiler::TypeSystem::INamespace {
	public:
		explicit TestNamespace(std::string fullName) : fullName_(std::move(fullName)) {}

		TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
		std::string Name() const override { return fullName_; }
		std::string ExternAlias() const override { return {}; }
		std::string FullName() const override { return fullName_; }
		const ICompilation& Compilation() const override { return compilation_; }
		const INamespace* ParentNamespace() const override { return nullptr; }
		std::vector<const INamespace*> ChildNamespaces() const override { return {}; }
		std::vector<const ITypeDefinition*> Types() const override { return {}; }
		std::vector<const ::ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules()
			const override {
			return {};
		}
		const INamespace* GetChildNamespace(const std::string&) const override {
			return nullptr;
		}
		const ITypeDefinition* GetTypeDefinition(const std::string&, int) const override {
			return nullptr;
		}

	private:
		std::string fullName_;
		LookupCompilation compilation_;
	};
	TestNamespace ns("A.B.C");

	EXPECT_EQ(ambience.ConvertSymbol(ns), "namespace A.B.C");
}

TEST(CSharpAmbienceConvertSymbolTest, ReturnTypeAfterParameterList)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowReturnType
		| OF::ConversionFlags::PlaceReturnTypeAfterParameterList
		| OF::ConversionFlags::ShowBody);
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	method->SetParameters({MakeIntParam()});

	EXPECT_EQ(ambience.ConvertSymbol(*method), "M(int a) : int;");
}

TEST(CSharpAmbienceConvertSymbolTest, IndexerRendersThisKeywordAndBrackets)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowReturnType);
	auto indexer = std::make_shared<AmbienceProperty>("Item", MakeIntDef(), Compilation());
	indexer->SetSymbolKind(TS::SymbolKind::Indexer);
	indexer->SetParameters({MakeIntParam("index")});

	EXPECT_EQ(ambience.ConvertSymbol(*indexer), "int this[int index]");
}

TEST(CSharpAmbienceConvertSymbolTest, ParameterModifiersKeptAndStrippedUnderTheFlag)
{
	CSharpAmbience kept;
	kept.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowParameterModifiers);
	CSharpAmbience stripped;
	stripped.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames);

	static auto refParam = std::make_shared<AmbienceParameter>(MakeIntDef(), "a");
	refParam->SetReferenceKind(TS::ReferenceKind::Ref);
	auto methodKept = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	methodKept->SetParameters({refParam.get()});
	auto methodStripped = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	methodStripped->SetParameters({refParam.get()});

	EXPECT_EQ(kept.ConvertSymbol(*methodKept), "M(ref int a)");
	EXPECT_EQ(stripped.ConvertSymbol(*methodStripped), "M(int a)");
}

TEST(CSharpAmbienceConvertSymbolTest, ParameterDefaultStrippedUnderTheFlag)
{
	// With ShowParameterDefaultValues on, the default renders (ShowConstantValues is
	// TRUE through the ambience -- a CreateAstBuilder quirk; `=5` with no spaces
	// because SpaceAroundAssignment is false in the CreateEmpty policy); with the
	// flag off, the Detach arm strips the default expression.
	CSharpAmbience rendered;
	rendered.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowParameterDefaultValues);
	CSharpAmbience stripped;
	stripped.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames);

	static auto defaultParam = std::make_shared<AmbienceParameter>(MakeIntDef(), "a");
	defaultParam->SetIsOptional(true);
	defaultParam->SetHasConstantValueInSignature(true);
	defaultParam->SetConstantValue(std::int32_t{5});
	auto methodRendered = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	methodRendered->SetParameters({defaultParam.get()});
	auto methodStripped = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	methodStripped->SetParameters({defaultParam.get()});

	EXPECT_EQ(rendered.ConvertSymbol(*methodRendered), "M(int a=5)");
	EXPECT_EQ(stripped.ConvertSymbol(*methodStripped), "M(int a)");
}

TEST(CSharpAmbienceConvertSymbolTest, PropertyBodyRendersGetSet)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowBody);
	auto getter = MakeAccessor("get_P");
	auto setter = MakeAccessor("set_P");
	auto property = std::make_shared<AmbienceProperty>("P", MakeIntDef(), Compilation());
	property->SetGetter(getter.get());
	property->SetSetter(setter.get());

	EXPECT_EQ(ambience.ConvertSymbol(*property), "P { get; set; }");
}

TEST(CSharpAmbienceConvertSymbolTest, InitOnlySetterUpgradesUnderTheFlag)
{
	CSharpAmbience withFlag;
	withFlag.ConversionFlags(OF::ConversionFlags::ShowBody
		| OF::ConversionFlags::SupportInitAccessors);
	CSharpAmbience withoutFlag;
	withoutFlag.ConversionFlags(OF::ConversionFlags::ShowBody);

	auto getter = MakeAccessor("get_P");
	auto initSetter = MakeAccessor("set_P");
	initSetter->SetIsInitOnly(true);

	auto propertyA = std::make_shared<AmbienceProperty>("P", MakeIntDef(), Compilation());
	propertyA->SetGetter(getter.get());
	propertyA->SetSetter(initSetter.get());
	auto propertyB = std::make_shared<AmbienceProperty>("P", MakeIntDef(), Compilation());
	propertyB->SetGetter(getter.get());
	propertyB->SetSetter(initSetter.get());

	EXPECT_EQ(withFlag.ConvertSymbol(*propertyA), "P { get; init; }");
	EXPECT_EQ(withoutFlag.ConvertSymbol(*propertyB), "P { get; set; }");
}

TEST(CSharpAmbienceConvertSymbolTest, ConstructorRendersDeclaringTypeName)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames);
	auto ctor = std::make_shared<AmbienceMethod>(".ctor", MakeIntDef(), Compilation());
	ctor->SetSymbolKind(TS::SymbolKind::Constructor);
	ctor->SetDeclaringType(MakeHostDef("C"));
	ctor->SetParameters({MakeIntParam()});

	EXPECT_EQ(ambience.ConvertSymbol(*ctor), "C(int a)");
}

TEST(CSharpAmbienceConvertSymbolTest, DestructorRendersTildeAndDeclaringTypeName)
{
	CSharpAmbience ambience;
	auto dtor = std::make_shared<AmbienceMethod>("Finalize", MakeIntDef(), Compilation());
	dtor->SetSymbolKind(TS::SymbolKind::Destructor);
	dtor->SetDeclaringType(MakeHostDef("C"));

	EXPECT_EQ(ambience.ConvertSymbol(*dtor), "~C");
}

TEST(CSharpAmbienceConvertSymbolTest, ImplicitOperatorRendersKeywordAndReturnType)
{
	CSharpAmbience ambience;
	auto op = std::make_shared<AmbienceMethod>("op_Implicit", MakeIntDef(), Compilation());
	op->SetSymbolKind(TS::SymbolKind::Operator);

	EXPECT_EQ(ambience.ConvertSymbol(*op), "implicit operator int");
}

TEST(CSharpAmbienceConvertSymbolTest, ExplicitAndCheckedExplicitOperators)
{
	CSharpAmbience ambience;
	auto opExplicit = std::make_shared<AmbienceMethod>("op_Explicit", MakeIntDef(), Compilation());
	opExplicit->SetSymbolKind(TS::SymbolKind::Operator);
	EXPECT_EQ(ambience.ConvertSymbol(*opExplicit), "explicit operator int");

	// The `checked` token of op_CheckedExplicit is unconditional (the
	// SupportOperatorChecked flag gates only the token arm, not the explicit-operator
	// name dispatch), and it comes AFTER the `operator` keyword -- the C# writes
	// "explicit" "operator" "checked" then the return type.
	auto opCheckedExplicit = std::make_shared<AmbienceMethod>("op_CheckedExplicit", MakeIntDef(), Compilation());
	opCheckedExplicit->SetSymbolKind(TS::SymbolKind::Operator);
	EXPECT_EQ(ambience.ConvertSymbol(*opCheckedExplicit), "explicit operator checked int");
}

TEST(CSharpAmbienceConvertSymbolTest, OperatorTokenRendersTheToken)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames);
	auto op = std::make_shared<AmbienceMethod>("op_Addition", MakeIntDef(), Compilation());
	op->SetSymbolKind(TS::SymbolKind::Operator);
	op->SetParameters({MakeIntParam("a"), MakeIntParam("b")});

	EXPECT_EQ(ambience.ConvertSymbol(*op), "operator +(int a, int b)");
}

TEST(CSharpAmbienceConvertSymbolTest, CheckedOperatorWithoutTheFlagFallsBackToTheNameToken)
{
	// The faithful quirk: with SupportOperatorChecked off, the builder's operator
	// renderer takes the method fallback (the node is a MethodDeclaration named
	// op_CheckedAddition), and the token arm's condition fails -- the fallback writes
	// the node's own name token.
	CSharpAmbience ambience;
	auto op = std::make_shared<AmbienceMethod>("op_CheckedAddition", MakeIntDef(), Compilation());
	op->SetSymbolKind(TS::SymbolKind::Operator);

	EXPECT_EQ(ambience.ConvertSymbol(*op), "operator op_CheckedAddition");
}

TEST(CSharpAmbienceConvertSymbolTest, CheckedOperatorWithTheFlagRendersTheCheckedToken)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::SupportOperatorChecked);
	auto op = std::make_shared<AmbienceMethod>("op_CheckedAddition", MakeIntDef(), Compilation());
	op->SetSymbolKind(TS::SymbolKind::Operator);

	EXPECT_EQ(ambience.ConvertSymbol(*op), "operator checked +");
}

TEST(CSharpAmbienceConvertSymbolTest, ShowDeclaringTypePrefixesTheMember)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDeclaringType);
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	method->SetDeclaringType(MakeHostDef("C"));

	EXPECT_EQ(ambience.ConvertSymbol(*method), "C.M");
}

TEST(CSharpAmbienceConvertSymbolTest, LocalFunctionExcludedFromTheDeclaringTypePrefix)
{
	// The C# `!(member is LocalFunctionMethod)` guard: a local function's
	// DeclaringType is its container, but C# local-function syntax renders no
	// declaring-type prefix even under ShowDeclaringType.
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDeclaringType);
	auto baseMethod = std::make_shared<AmbienceMethod>("Local", MakeIntDef(), Compilation());
	baseMethod->SetDeclaringType(MakeHostDef("C"));
	ILSpy::Decompiler::TypeSystem::Implementation::LocalFunctionMethod localFunction(
		baseMethod, "Local", /*isStaticLocalFunction*/ false,
		/*numberOfCompilerGeneratedParameters*/ 0,
		/*numberOfCompilerGeneratedTypeParameters*/ 0);

	EXPECT_EQ(ambience.ConvertSymbol(localFunction), "Local");
}

TEST(CSharpAmbienceConvertSymbolTest, NestedTypeShowDeclaringTypePrefixes)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowDeclaringType);
	auto outer = MakeHostDef("Outer");
	auto inner = std::make_shared<LookupTypeDefinition>(
		"Inner", "", FullTypeName(TopLevelTypeName("", "Inner", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr);
	inner->SetDeclaringTypeDefinition(outer.get());

	EXPECT_EQ(ambience.ConvertSymbol(*inner), "Outer.Inner");
}

TEST(CSharpAmbienceConvertSymbolTest, FullyQualifiedEntityNamesPrefixTheNamespace)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::UseFullyQualifiedEntityNames);
	auto typeDef = MakeClassDef("C", "Ns");

	EXPECT_EQ(ambience.ConvertSymbol(*typeDef), "Ns.C");
}

TEST(CSharpAmbienceConvertSymbolTest, GenericMethodRendersTypeParameterList)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowTypeParameterList);
	static auto typeParam = std::make_shared<LookupTypeParameter>("T");
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	method->SetTypeParameters({typeParam.get()});
	method->SetParameters({MakeIntParam()});

	EXPECT_EQ(ambience.ConvertSymbol(*method), "M<T>(int a)");
}

TEST(CSharpAmbienceConvertSymbolTest, VarianceModifierStrippedWithoutTheFlag)
{
	// An interface with a covariant type parameter: the ShowTypeParameterVarianceModifier
	// flag off strips the `out` keyword (the RemoveVarianceModifier arm); with the flag
	// on it renders.
	CSharpAmbience stripped;
	stripped.ConversionFlags(OF::ConversionFlags::ShowTypeParameterList);
	CSharpAmbience kept;
	kept.ConversionFlags(OF::ConversionFlags::ShowTypeParameterList
		| OF::ConversionFlags::ShowTypeParameterVarianceModifier);

	static auto covariantT = std::make_shared<LookupTypeParameter>("T", TS::VarianceModifier::Covariant);
	auto interfaceA = std::make_shared<LookupTypeDefinition>(
		"I", "", FullTypeName(TopLevelTypeName("", "I", 1)),
		TypeKind::Interface, Accessibility::Public, Compilation(), nullptr);
	interfaceA->SetTypeParameters({covariantT.get()});
	auto interfaceB = std::make_shared<LookupTypeDefinition>(
		"I", "", FullTypeName(TopLevelTypeName("", "I", 1)),
		TypeKind::Interface, Accessibility::Public, Compilation(), nullptr);
	interfaceB->SetTypeParameters({covariantT.get()});

	EXPECT_EQ(stripped.ConvertSymbol(*interfaceA), "I<T>");
	EXPECT_EQ(kept.ConvertSymbol(*interfaceB), "I<out T>");
}

TEST(CSharpAmbienceConvertSymbolTest, ParameterizedPropertyTakesParametersFromTheSymbol)
{
	// C# property syntax has no parameter list, so the converted node carries none; a
	// parameterized property takes its parameters from the symbol (the C# comment at
	// CSharpAmbience.cs line 158).
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames);
	auto property = std::make_shared<AmbienceProperty>("P", MakeIntDef(), Compilation());
	property->SetParameters({MakeIntParam()});

	EXPECT_EQ(ambience.ConvertSymbol(*property), "P(int a)");
}

TEST(CSharpAmbienceConvertSymbolTest, WriterOverloadDrivesTheGivenWriter)
{
	// The 3-arg core drives a caller-supplied TokenWriter with the same rendering as
	// the string entry (the delegation the string overload performs).
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::ShowParameterList
		| OF::ConversionFlags::ShowParameterNames
		| OF::ConversionFlags::ShowReturnType);
	auto method = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	method->SetParameters({MakeIntParam()});

	std::ostringstream text;
	TextWriterTokenWriter writer(&text);
	ambience.ConvertSymbol(*method, &writer, ILSpy::Decompiler::CSharp::OutputVisitor::FormattingOptionsFactory::CreateEmpty());
	EXPECT_EQ(text.str(), "int M(int a)");
}

// ---------------------------------------------------------------------------
// ConvertVariable (the TrimEnd entry)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceConvertVariableTest, RendersTheDeclaration)
{
	// The trailing `;` of the rendered VariableDeclarationStatement is trimmed.
	CSharpAmbience ambience;
	AmbienceVariable variable("x", MakeIntDef());

	EXPECT_EQ(ambience.ConvertVariable(variable), "int x");
}

TEST(CSharpAmbienceConvertVariableTest, RendersConstDeclarationWithInitializer)
{
	// IsConst sets the Const modifier unconditionally (not gated on ShowModifiers),
	// and ShowConstantValues is TRUE through the ambience, so the const initializer
	// renders. The spaces around `=` come from AstNode.ToString's null-policy default
	// (FormattingOptionsFactory::CreateMono, whose SpaceAroundAssignment is true) -- a
	// non-obvious interplay: the ambience's own writer path uses CreateEmpty, but
	// ConvertVariable renders through the node's ToString.
	CSharpAmbience ambience;
	AmbienceVariable variable("Max", MakeIntDef());
	variable.SetIsConst(true);
	variable.SetConstantValue(std::int32_t{5});

	EXPECT_EQ(ambience.ConvertVariable(variable), "const int Max = 5");
}

// ---------------------------------------------------------------------------
// ConvertType (the string entry)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceConvertTypeTest, RendersTheShortName)
{
	CSharpAmbience ambience;

	EXPECT_EQ(ambience.ConvertType(*MakeIntDef()), "int");
}

TEST(CSharpAmbienceConvertTypeTest, FullyQualifiedWhenTheEntityFlagIsOn)
{
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::UseFullyQualifiedEntityNames);

	EXPECT_EQ(ambience.ConvertType(*MakeClassDef("C", "Ns")), "Ns.C");
}

TEST(CSharpAmbienceConvertTypeTest, UsesTheEntityNamesFlagNotTheTypeNamesFlag)
{
	// The C# quirk: ConvertType maps UseFullyQualifiedEntityNames (NOT the
	// UseFullyQualifiedTypeNames bit CreateAstBuilder maps) -- with only the TypeNames
	// bit set the rendering stays short.
	CSharpAmbience ambience;
	ambience.ConversionFlags(OF::ConversionFlags::UseFullyQualifiedTypeNames);

	EXPECT_EQ(ambience.ConvertType(*MakeClassDef("C", "Ns")), "C");
}

// ---------------------------------------------------------------------------
// ConvertConstantValue + WrapComment
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceConvertConstantValueTest, RendersIntegralValue)
{
	CSharpAmbience ambience;
	EXPECT_EQ(ambience.ConvertConstantValue(Syn::PrimitiveValue{std::int32_t{5}}), "5");
}

TEST(CSharpAmbienceConvertConstantValueTest, RendersStringValue)
{
	CSharpAmbience ambience;
	EXPECT_EQ(ambience.ConvertConstantValue(Syn::PrimitiveValue{std::string("abc")}), "\"abc\"");
}

TEST(CSharpAmbienceWrapCommentTest, PrependsTheSlashPrefix)
{
	CSharpAmbience ambience;
	EXPECT_EQ(ambience.WrapComment("foo"), "// foo");
}

// ---------------------------------------------------------------------------
// GetExplicitInterfaceType + WriteQualifiedName (the widened helpers)
// ---------------------------------------------------------------------------

TEST(CSharpAmbienceGetExplicitInterfaceTypeTest, ReturnsTheInterfaceMemberDeclaringType)
{
	CSharpAmbience ambience;
	auto interfaceType = MakeHostDef("I");
	static auto interfaceMember = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	interfaceMember->SetDeclaringType(interfaceType);
	auto member = std::make_shared<AmbienceMethod>("I.M", MakeIntDef(), Compilation());
	member->SetIsExplicitInterfaceImplementation(true);
	member->SetExplicitlyImplementedInterfaceMembers({interfaceMember.get()});

	auto result = ambience.GetExplicitInterfaceType(*member);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), interfaceType.get());
}

TEST(CSharpAmbienceGetExplicitInterfaceTypeTest, NullForNonExplicitAndEmptyMembers)
{
	CSharpAmbience ambience;
	auto member = std::make_shared<AmbienceMethod>("M", MakeIntDef(), Compilation());
	EXPECT_EQ(ambience.GetExplicitInterfaceType(*member), nullptr);

	member->SetIsExplicitInterfaceImplementation(true);
	// An empty explicitly-implemented list leaves no base member to read.
	EXPECT_EQ(ambience.GetExplicitInterfaceType(*member), nullptr);
}

TEST(CSharpAmbienceWriteQualifiedNameTest, RendersTheDottedChain)
{
	CSharpAmbience ambience;
	std::ostringstream text;
	TextWriterTokenWriter writer(&text);
	ambience.WriteQualifiedName("A.B", &writer,
		ILSpy::Decompiler::CSharp::OutputVisitor::FormattingOptionsFactory::CreateEmpty());

	EXPECT_EQ(text.str(), "A.B");
}
