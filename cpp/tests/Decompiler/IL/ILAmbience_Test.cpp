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
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for ILAmbience (the IL-view IAmbience implementation). The
// ConvertSymbol flag prefixes are driven against a REAL .NET Framework
// mscorlib through the port's MetadataFile (the MetadataAttributes_Test
// fixture convention -- GTEST_SKIP when the runtime assembly is absent) with
// the flag values independently verified against the System.Reflection
// metadata rows (dnlib probe): System.String::Empty 0x36
// (public static initonly), m_firstChar 0x81 (private notserialized),
// Object::ToString 0x1C6 (public hidebysig newslot virtual),
// String::IsNullOrEmpty 0x96 (public hidebysig static),
// String::Length / AppDomain::AssemblyLoad rows all zero flags,
// System.Object 0x102001 (public auto beforefieldinit),
// System.Collections.IEnumerable 0xA1 (public interface abstract).
//
// The ConvertType visitor arms run over the concrete IType leaves
// (Array/Pointer/ByReference/ModOpt/ModReq/Parameterized/Tuple/FunctionPointer)
// with a VisitableDef stub (the AcceptVisitor -> VisitTypeDefinition bridge
// the shared LookupTypeDefinition lacks) carrying the KnownTypeCode the
// keyword arm switches on.

#include "Decompiler/IL/ILAmbience.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/IAmbience.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace MD = ::ILSpy::Decompiler::Metadata;

using ILSpy::Decompiler::IL::ILAmbience;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupEvent;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupModule;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;
using ILSpy::Decompiler::TypeSystem::ToILSyntax;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using OF = ::ILSpy::Decompiler::Output::ConversionFlags;

namespace {

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::uint32_t FindType(MD::MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindFieldToken(MD::MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& fld : f.GetFields(typeToken)) {
        if (fld.Name == name) return fld.Token;
    }
    return 0;
}

std::uint32_t FindMethodToken(MD::MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

std::uint32_t FindPropertyToken(MD::MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& p : f.GetProperties(typeToken)) {
        if (p.Name == name) return p.Token;
    }
    return 0;
}

std::uint32_t FindEventToken(MD::MetadataFile& f, std::uint32_t typeToken, std::string_view name) {
    for (const auto& e : f.GetEvents(typeToken)) {
        if (e.Name == name) return e.Token;
    }
    return 0;
}

// The shared per-test fixture: a real mscorlib MetadataFile plus the
// LookupCompilation/LookupModule pair the symbol stubs hang off (the module's
// MetadataFile is what ConvertSymbol reads the flag rows through).
struct Fixture {
    std::unique_ptr<MD::MetadataFile> file;
    LookupCompilation compilation;
    LookupModule module;

    Fixture() : module(compilation, "mscorlib") {
        const char* path = FixturePath();
        if (std::filesystem::exists(path)) {
            file = std::make_unique<MD::MetadataFile>(path);
            if (file->IsValid())
                module.SetMetadataFile(file.get());
        }
    }

    bool Available() const { return file && file->IsValid(); }
};

// A `LookupTypeDefinition` whose AcceptVisitor routes to `VisitTypeDefinition`
// (the shared stub routes to VisitOtherType -- the NormalizeTypeVisitor
// VisitableDefinition precedent), carrying a KnownTypeCode and an arity.
class VisitableDef : public LookupTypeDefinition {
public:
    VisitableDef(std::string name, const ICompilation& compilation,
                 TS::KnownTypeCode code = TS::KnownTypeCode::None, int arity = 0,
                 std::string ns = std::string())
        : LookupTypeDefinition(name, ns,
              TS::FullTypeName(TS::TopLevelTypeName(ns, name, arity)),
              TS::TypeKind::Class, TS::Accessibility::Public, compilation,
              &compilation.MainModule(), code) {}

    ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
        return visitor.VisitTypeDefinition(*this);
    }
};

// A `LookupTypeParameter` whose AcceptVisitor routes to `VisitTypeParameter`
// (the TypeSystemAstBuilder VisitableTypeParameter precedent).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    explicit VisitableTypeParameter(std::string name,
                                     VarianceModifier variance = VarianceModifier::Invariant)
        : LookupTypeParameter(std::move(name), variance) {}

    ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A configurable `IField` stub (the TypeSystemAstBuilderMemberRenderers TestField
// shape) extended with the metadata token / module the ConvertSymbol flag
// prefixes read.
class TestField : public ILSpy::Decompiler::TypeSystem::IField {
public:
    TestField(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    void SetIsStatic(bool v) { isStatic_ = v; }
    void SetMetadataToken(std::uint32_t t) { metadataToken_ = t; }
    void SetParentModule(const ILSpy::Decompiler::TypeSystem::IModule* m) { parentModule_ = m; }
    void SetDeclaringTypeDefinition(const ILSpy::Decompiler::TypeSystem::ITypeDefinition* d) {
        declaringTypeDefinition_ = d;
    }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override {
        return declaringTypeDefinition_;
    }
    ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return parentModule_;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override {
        return this;
    }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
        ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const TypeVisitor*) const override {
        return obj == this;
    }

    // --- IVariable ---
    const IType& Type() const override { return *returnType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }

    // --- IField ---
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    bool isStatic_ = false;
    std::uint32_t metadataToken_ = 0;
    const ILSpy::Decompiler::TypeSystem::IModule* parentModule_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition_ = nullptr;
};

// A configurable `IParameter` stub (the GetApplicableConversionOperators
// TestParameter shape).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type,
                           ILSpy::Decompiler::TypeSystem::ReferenceKind refKind =
                               ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                           std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override {
        return refKind_;
    }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    std::string name_;
    ITypePtr type_;
    ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A configurable `IProperty` stub (the TypeSystemAstBuilderMemberRenderers
// TestProperty shape, extended with the token / module the `.property` prefix
// reads).
class TestProperty : public ILSpy::Decompiler::TypeSystem::IProperty {
public:
    TestProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : name_(std::move(name)), returnType_(std::move(returnType)),
          compilation_(compilation) {}

    void SetMetadataToken(std::uint32_t t) { metadataToken_ = t; }
    void SetParentModule(const ILSpy::Decompiler::TypeSystem::IModule* m) { parentModule_ = m; }
    void SetParameters(std::vector<const IParameter*> p) { parameters_ = std::move(p); }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Property; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override {
        return parentModule_;
    }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override { return TS::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override {
        return this;
    }
    const IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*>
        ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const TypeVisitor*) const override {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return parameters_; }

    // --- IProperty ---
    bool CanGet() const override { return true; }
    bool CanSet() const override { return true; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Getter() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Setter() const override { return nullptr; }
    bool IsIndexer() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    std::string name_;
    ITypePtr returnType_;
    const ICompilation& compilation_;
    std::uint32_t metadataToken_ = 0;
    const ILSpy::Decompiler::TypeSystem::IModule* parentModule_ = nullptr;
    std::vector<const IParameter*> parameters_;
};

// An indexer-shaped TestProperty (the `not SymbolKind::Property` guard's
// positive side: a parameterized member whose kind is Indexer DOES render the
// parameter list).
class TestIndexerProperty : public TestProperty {
public:
    TestIndexerProperty(std::string name, ITypePtr returnType, const ICompilation& compilation)
        : TestProperty(std::move(name), std::move(returnType), compilation) {}
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Indexer; }
};

// The IField diamond (IVariable : ISymbol vs IMember : IEntity : ISymbol, the
// D372 flattening): the direct TestField -> `const ISymbol&` conversion is
// ambiguous. Route through the IEntity branch (the single path IMember
// provides), which is the branch the C# interface unification exposes.
const ILSpy::Decompiler::TypeSystem::ISymbol& AsSymbol(const TestField& f) {
    return static_cast<const ILSpy::Decompiler::TypeSystem::IEntity&>(f);
}

// A constructor-shaped LookupMethod (the `{ SymbolKind: not Constructor }`
// return-type guard's negative side).
class TestCtorMethod : public LookupMethod {
public:
    TestCtorMethod(std::string name, const ICompilation& compilation)
        : LookupMethod(std::move(name), compilation) {}
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Constructor; }
};

} // namespace

// ===========================================================================
// The ConvertSymbol dispatch arms (ILAmbience.cs lines 45-302).
// ===========================================================================
TEST(ILAmbienceTest, StaleEntityWithoutModuleFallsBackToName) {
	ILAmbience ambience;
	LookupCompilation compilation;
	// No ParentModule: the entity's owning module was torn down (or was never
	// attached) -- the stale-entity fallback renders the bare name.
	LookupMethod method("M", compilation);
	EXPECT_EQ(ambience.ConvertSymbol(method), "M");
}

TEST(ILAmbienceTest, NonEntitySymbolFallsBackToName) {
	ILAmbience ambience;
	// A type parameter is an ISymbol but not an IEntity: no metadata to read.
	LookupTypeParameter tp("T");
	EXPECT_EQ(ambience.ConvertSymbol(tp), "T");
}

TEST(ILAmbienceTest, FieldRendersDefinitionKeywordOnly) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringTok = FindType(*fx.file, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto emptyTok = FindFieldToken(*fx.file, stringTok, "Empty");
	ASSERT_NE(emptyTok, 0u);

	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	TestField field("Empty", stringType, fx.compilation);
	field.SetMetadataToken(emptyTok);
	field.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword);
	EXPECT_EQ(ambience.ConvertSymbol(AsSymbol(field)), ".field Empty");
}

TEST(ILAmbienceTest, FieldRendersMetadataFlagsForStaticField) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringTok = FindType(*fx.file, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto emptyTok = FindFieldToken(*fx.file, stringTok, "Empty");
	ASSERT_NE(emptyTok, 0u);

	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	TestField field("Empty", stringType, fx.compilation);
	field.SetMetadataToken(emptyTok);
	field.SetParentModule(&fx.module);
	field.SetIsStatic(true);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowAccessibility
		| OF::ShowModifiers);
	// String::Empty flags 0x36: public (6), static (0x10), initonly (0x20) --
	// the WriteEnum visibility half then the WriteFlags remainder.
	EXPECT_EQ(ambience.ConvertSymbol(AsSymbol(field)), ".field public static initonly Empty");
}

TEST(ILAmbienceTest, InstanceFieldRendersInstanceAndHiddenFlags) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringTok = FindType(*fx.file, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto firstCharTok = FindFieldToken(*fx.file, stringTok, "m_firstChar");
	ASSERT_NE(firstCharTok, 0u);

	auto charType = std::make_shared<VisitableDef>("Char", fx.compilation, KnownTypeCode::Char);
	TestField field("m_firstChar", charType, fx.compilation);
	field.SetMetadataToken(firstCharTok);
	field.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowAccessibility
		| OF::ShowModifiers);
	// m_firstChar flags 0x81: private (1), notserialized (0x80) -- a non-static
	// field adds the `instance ` modifier.
	EXPECT_EQ(ambience.ConvertSymbol(AsSymbol(field)), ".field private notserialized instance m_firstChar");
}

TEST(ILAmbienceTest, MethodRendersMetadataFlagsForVirtualMethod) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto objectTok = FindType(*fx.file, "System", "Object");
	ASSERT_NE(objectTok, 0u);
	auto toStringTok = FindMethodToken(*fx.file, objectTok, "ToString");
	ASSERT_NE(toStringTok, 0u);

	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	LookupMethod method("ToString", fx.compilation);
	method.SetMetadataToken(toStringTok);
	method.SetParentModule(&fx.module);
	method.SetReturnType(stringType);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowAccessibility
		| OF::ShowModifiers);
	// Object::ToString flags 0x1C6: public (6), virtual (0x40), hidebysig
	// (0x80), newslot (0x100) -- rendered in methodAttributeFlags table order.
	EXPECT_EQ(ambience.ConvertSymbol(method),
		".method public hidebysig newslot virtual instance ToString");
}

TEST(ILAmbienceTest, StaticMethodOmitsInstanceModifier) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringTok = FindType(*fx.file, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto isNullOrEmptyTok = FindMethodToken(*fx.file, stringTok, "IsNullOrEmpty");
	ASSERT_NE(isNullOrEmptyTok, 0u);

	auto boolType = std::make_shared<VisitableDef>("Boolean", fx.compilation,
		KnownTypeCode::Boolean);
	LookupMethod method("IsNullOrEmpty", fx.compilation);
	method.SetMetadataToken(isNullOrEmptyTok);
	method.SetParentModule(&fx.module);
	method.SetReturnType(boolType);
	method.SetStatic(true);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowAccessibility
		| OF::ShowModifiers);
	// String::IsNullOrEmpty flags 0x96: public (6), hidebysig (0x80), static
	// (0x10) -- a static method takes no `instance ` modifier.
	EXPECT_EQ(ambience.ConvertSymbol(method), ".method public hidebysig static IsNullOrEmpty");
}

TEST(ILAmbienceTest, PropertyRendersInstanceModifierWithoutFlags) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringTok = FindType(*fx.file, "System", "String");
	ASSERT_NE(stringTok, 0u);
	auto lengthTok = FindPropertyToken(*fx.file, stringTok, "Length");
	ASSERT_NE(lengthTok, 0u);

	auto intType = std::make_shared<VisitableDef>("Int32", fx.compilation, KnownTypeCode::Int32);
	TestProperty property("Length", intType, fx.compilation);
	property.SetMetadataToken(lengthTok);
	property.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowModifiers);
	// String::Length's property row carries all-zero flags (verified: the C#
	// compilers do not emit SpecialName on property rows) -- only the
	// non-static `instance ` modifier renders.
	EXPECT_EQ(ambience.ConvertSymbol(property), ".property instance Length");
}

TEST(ILAmbienceTest, PropertySuppressesParameterList) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto intType = std::make_shared<VisitableDef>("Int32", fx.compilation, KnownTypeCode::Int32);
	TestProperty property("P", intType, fx.compilation);
	property.SetParentModule(&fx.module);
	auto p = std::make_shared<TestParameter>(intType);
	property.SetParameters({p.get()});

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowParameterList | OF::ShowParameterNames);
	// A C# property cannot carry parameters -- the `not SymbolKind::Property`
	// guard suppresses the list even for a parameterized stub.
	EXPECT_EQ(ambience.ConvertSymbol(property), "P");
}

TEST(ILAmbienceTest, IndexerRendersParameterList) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto intType = std::make_shared<VisitableDef>("Int32", fx.compilation, KnownTypeCode::Int32);
	TestIndexerProperty indexer("Item", intType, fx.compilation);
	indexer.SetParentModule(&fx.module);
	auto p = std::make_shared<TestParameter>(intType, ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
		"index");
	indexer.SetParameters({p.get()});

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowParameterList | OF::ShowParameterNames);
	EXPECT_EQ(ambience.ConvertSymbol(indexer), "Item(int32 index)");
}

TEST(ILAmbienceTest, EventRendersInstanceModifier) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto appDomainTok = FindType(*fx.file, "System", "AppDomain");
	ASSERT_NE(appDomainTok, 0u);
	auto assemblyLoadTok = FindEventToken(*fx.file, appDomainTok, "AssemblyLoad");
	ASSERT_NE(assemblyLoadTok, 0u);

	auto handlerType = std::make_shared<VisitableDef>("AssemblyLoadEventHandler",
		fx.compilation);
	LookupEvent evt("AssemblyLoad", handlerType, fx.compilation);
	evt.SetMetadataToken(assemblyLoadTok);
	evt.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowModifiers);
	// AppDomain::AssemblyLoad's event row carries all-zero flags; only the
	// non-static `instance ` modifier renders.
	EXPECT_EQ(ambience.ConvertSymbol(evt), ".event instance AssemblyLoad");
}

TEST(ILAmbienceTest, TypeDefinitionRendersClassPrefixAndFlags) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto objectTok = FindType(*fx.file, "System", "Object");
	ASSERT_NE(objectTok, 0u);

	LookupTypeDefinition objectDef("Object", "System",
		FullTypeName(TopLevelTypeName("System", "Object")), TS::TypeKind::Class,
		TS::Accessibility::Public, fx.compilation, &fx.module);
	objectDef.SetMetadataToken(objectTok);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowAccessibility
		| OF::ShowModifiers);
	// System.Object typedef flags 0x102001: public (1), serializable (0x2000 --
	// the .NET Framework mscorlib row really carries it), beforefieldinit
	// (0x100000); the auto/ansi defaults are the masked-out zero bits.
	EXPECT_EQ(ambience.ConvertSymbol(objectDef),
		".class public serializable beforefieldinit Object");
}

TEST(ILAmbienceTest, InterfaceDefinitionRendersInterfaceKeyword) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto enumerableTok = FindType(*fx.file, "System.Collections", "IEnumerable");
	ASSERT_NE(enumerableTok, 0u);

	LookupTypeDefinition interfaceDef("IEnumerable", "System.Collections",
		FullTypeName(TopLevelTypeName("System.Collections", "IEnumerable")),
		TS::TypeKind::Interface, TS::Accessibility::Public, fx.compilation, &fx.module);
	interfaceDef.SetMetadataToken(enumerableTok);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowAccessibility
		| OF::ShowModifiers);
	// System.Collections.IEnumerable flags 0xA1: public (1), interface (0x20 --
	// the "interface " keyword), abstract (0x80).
	EXPECT_EQ(ambience.ConvertSymbol(interfaceDef),
		".class interface public abstract IEnumerable");
}

TEST(ILAmbienceTest, ShowReturnTypeBeforeRendersMemberType) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	TestField field("Empty", stringType, fx.compilation);
	field.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowReturnType);
	EXPECT_EQ(ambience.ConvertSymbol(AsSymbol(field)), ".field string Empty");
}

TEST(ILAmbienceTest, PlaceReturnTypeAfterRendersColonForm) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	TestField field("Empty", stringType, fx.compilation);
	field.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowReturnType
		| OF::PlaceReturnTypeAfterParameterList);
	EXPECT_EQ(ambience.ConvertSymbol(AsSymbol(field)), ".field Empty : string");
}

TEST(ILAmbienceTest, ConstructorExcludedFromReturnType) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto intType = std::make_shared<VisitableDef>("Int32", fx.compilation, KnownTypeCode::Int32);
	TestCtorMethod ctor(".ctor", fx.compilation);
	ctor.SetParentModule(&fx.module);
	ctor.SetReturnType(intType);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDefinitionKeyword | OF::ShowReturnType
		| OF::PlaceReturnTypeAfterParameterList);
	// A constructor renders neither the leading return type nor the " : "
	// suffix -- the `{ SymbolKind: not Constructor }` guard.
	EXPECT_EQ(ambience.ConvertSymbol(ctor), ".method .ctor");
}

TEST(ILAmbienceTest, DeclaringTypePrefixWithDoubleColon) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	LookupTypeDefinition host("Host", "", FullTypeName(TopLevelTypeName("", "Host")),
		TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, &fx.module);
	TestField field("Empty", stringType, fx.compilation);
	field.SetParentModule(&fx.module);
	field.SetDeclaringTypeDefinition(&host);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowDeclaringType);
	EXPECT_EQ(ambience.ConvertSymbol(AsSymbol(field)), "Host::Empty");
}

TEST(ILAmbienceTest, FullyQualifiedEntityNamesRendersNamespace) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto stringTok = FindType(*fx.file, "System", "String");
	ASSERT_NE(stringTok, 0u);

	LookupTypeDefinition stringDef("String", "System",
		FullTypeName(TopLevelTypeName("System", "String")), TS::TypeKind::Class,
		TS::Accessibility::Public, fx.compilation, &fx.module);
	stringDef.SetMetadataToken(stringTok);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::UseFullyQualifiedEntityNames);
	EXPECT_EQ(ambience.ConvertSymbol(stringDef), "System.String");
}

TEST(ILAmbienceTest, NestedTypeRendersDeclaringChain) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	LookupTypeDefinition outer("Outer", "", FullTypeName(TopLevelTypeName("", "Outer")),
		TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, &fx.module);
	LookupTypeDefinition inner("Inner", "", FullTypeName(TopLevelTypeName("", "Inner")),
		TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, &fx.module);
	inner.SetDeclaringTypeDefinition(&outer);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::UseFullyQualifiedEntityNames);
	EXPECT_EQ(ambience.ConvertSymbol(inner), "Outer.Inner");
}

TEST(ILAmbienceTest, TypeArityRendersSingleBacktick) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto t1 = std::make_shared<LookupTypeParameter>("T");
	auto t2 = std::make_shared<LookupTypeParameter>("U");
	LookupTypeDefinition fooDef("Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 2)),
		TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, &fx.module);
	fooDef.SetTypeParameters({t1.get(), t2.get()});

	ILAmbience ambience;
	// No flags: the arity marker renders regardless (only the <T, U> list is
	// gated on ShowTypeParameterList).
	EXPECT_EQ(ambience.ConvertSymbol(fooDef), "Foo`2");
}

TEST(ILAmbienceTest, MethodArityRendersDoubleBacktick) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto t1 = std::make_shared<LookupTypeParameter>("T");
	auto t2 = std::make_shared<LookupTypeParameter>("U");
	LookupMethod method("M", fx.compilation);
	method.SetParentModule(&fx.module);
	method.SetTypeParameters({t1.get(), t2.get()});

	ILAmbience ambience;
	EXPECT_EQ(ambience.ConvertSymbol(method), "M``2");
}

TEST(ILAmbienceTest, MethodAritySubtractsDeclaringTypeCount) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto t1 = std::make_shared<LookupTypeParameter>("T");
	auto t2 = std::make_shared<LookupTypeParameter>("U");
	LookupTypeDefinition host("Host", "", FullTypeName(TopLevelTypeName("", "Host", 1)),
		TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, &fx.module);
	LookupMethod method("M", fx.compilation);
	method.SetParentModule(&fx.module);
	method.SetDeclaringTypeDefinition(&host);
	method.SetTypeParameters({t1.get(), t2.get()});

	ILAmbience ambience;
	// A method on a generic declaring type reports only its OWN arity: 2 - 1.
	EXPECT_EQ(ambience.ConvertSymbol(method), "M``1");
}

TEST(ILAmbienceTest, TypeParameterListRendersVarianceSigns) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto t1 = std::make_shared<LookupTypeParameter>("T", VarianceModifier::Covariant);
	auto t2 = std::make_shared<LookupTypeParameter>("U", VarianceModifier::Contravariant);
	LookupTypeDefinition fooDef("Foo", "", FullTypeName(TopLevelTypeName("", "Foo", 2)),
		TS::TypeKind::Class, TS::Accessibility::Public, fx.compilation, &fx.module);
	fooDef.SetTypeParameters({t1.get(), t2.get()});

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowTypeParameterList | OF::ShowTypeParameterVarianceModifier);
	// The C# writes "," (no space) between the type parameters.
	EXPECT_EQ(ambience.ConvertSymbol(fooDef), "Foo`2<+T,-U>");
}

TEST(ILAmbienceTest, ParameterListRendersTypesAndNames) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto intType = std::make_shared<VisitableDef>("Int32", fx.compilation, KnownTypeCode::Int32);
	auto stringType = std::make_shared<VisitableDef>("String", fx.compilation,
		KnownTypeCode::String);
	auto a = std::make_shared<TestParameter>(intType,
		ILSpy::Decompiler::TypeSystem::ReferenceKind::None, "a");
	auto b = std::make_shared<TestParameter>(stringType,
		ILSpy::Decompiler::TypeSystem::ReferenceKind::None, "b");
	LookupMethod method("M", fx.compilation);
	method.SetParentModule(&fx.module);
	method.SetParameters({a.get(), b.get()});

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowParameterList | OF::ShowParameterNames);
	EXPECT_EQ(ambience.ConvertSymbol(method), "M(int32 a, string b)");
}

TEST(ILAmbienceTest, DispatchesThroughTheIAmbienceInterface) {
	Fixture fx;
	if (!fx.Available()) GTEST_SKIP() << "mscorlib fixture not present";
	auto intType = std::make_shared<VisitableDef>("Int32", fx.compilation, KnownTypeCode::Int32);
	TestField field("Max", intType, fx.compilation);
	field.SetParentModule(&fx.module);

	ILAmbience ambience;
	ambience.ConversionFlags(OF::ShowReturnType);
	::ILSpy::Decompiler::Output::IAmbience& interface_ = ambience;
	EXPECT_EQ(interface_.ConvertSymbol(AsSymbol(field)), "int32 Max");
	EXPECT_EQ(interface_.ConversionFlags(), OF::ShowReturnType);
	EXPECT_EQ(interface_.ConvertType(*intType), "int32");
	EXPECT_EQ(interface_.WrapComment("note"), "// note");
}

TEST(ILAmbienceTest, ConvertConstantValueThrows) {
	ILAmbience ambience;
	EXPECT_THROW(ambience.ConvertConstantValue(std::int32_t{5}), std::logic_error);
}

TEST(ILAmbienceTest, WrapCommentPrefixesDoubleSlash) {
	ILAmbience ambience;
	EXPECT_EQ(ambience.WrapComment("foo"), "// foo");
	EXPECT_EQ(ambience.WrapComment(""), "// ");
}

TEST(ILAmbienceTest, ConversionFlagsRoundTrip) {
	ILAmbience ambience;
	EXPECT_EQ(ambience.ConversionFlags(), OF::None);
	ambience.ConversionFlags(OF::StandardConversionFlags);
	EXPECT_EQ(ambience.ConversionFlags(), OF::StandardConversionFlags);
}

// ===========================================================================
// ConvertType: the TypeToStringVisitor arms (ILAmbience.cs lines 304-502).
// ===========================================================================
TEST(ILAmbienceConvertTypeTest, KnownTypeCodesRenderILKeywords) {
	LookupCompilation compilation;
	const std::pair<KnownTypeCode, const char*> cases[] = {
		{KnownTypeCode::Object, "object"},
		{KnownTypeCode::Boolean, "bool"},
		{KnownTypeCode::Char, "char"},
		{KnownTypeCode::SByte, "int8"},
		{KnownTypeCode::Byte, "uint8"},
		{KnownTypeCode::Int16, "int16"},
		{KnownTypeCode::UInt16, "uint16"},
		{KnownTypeCode::Int32, "int32"},
		{KnownTypeCode::UInt32, "uint32"},
		{KnownTypeCode::Int64, "int64"},
		{KnownTypeCode::UInt64, "uint64"},
		{KnownTypeCode::Single, "float32"},
		{KnownTypeCode::Double, "float64"},
		{KnownTypeCode::String, "string"},
		{KnownTypeCode::Void, "void"},
		{KnownTypeCode::IntPtr, "native int"},
		{KnownTypeCode::UIntPtr, "native uint"},
		{KnownTypeCode::TypedReference, "typedref"},
	};
	ILAmbience ambience;
	for (const auto& [code, expected] : cases) {
		SCOPED_TRACE("code ordinal " + std::to_string(static_cast<int>(code)));
		auto type = std::make_shared<VisitableDef>("Known", compilation, code);
		EXPECT_EQ(ambience.ConvertType(*type), expected);
	}
}

TEST(ILAmbienceConvertTypeTest, NonKnownDefinitionRendersNameAndArity) {
	LookupCompilation compilation;
	ILAmbience ambience;
	// A definition-shaped type routes through VisitTypeDefinition but falls to
	// the default WriteType arm: the escaped name plus the `N arity suffix.
	auto foo = std::make_shared<VisitableDef>("Foo", compilation, KnownTypeCode::None, 2);
	EXPECT_EQ(ambience.ConvertType(*foo), "Foo`2");
}

TEST(ILAmbienceConvertTypeTest, FullyQualifiedTypeNamesRendersFullName) {
	LookupCompilation compilation;
	ILAmbience ambience;
	ambience.ConversionFlags(OF::UseFullyQualifiedTypeNames);
	auto foo = std::make_shared<VisitableDef>("Foo", compilation, KnownTypeCode::None, 2, "System");
	EXPECT_EQ(ambience.ConvertType(*foo), "System.Foo`2");
}

TEST(ILAmbienceConvertTypeTest, ArrayRendersElementThenRank) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	ILAmbience ambience;
	auto sz = std::make_shared<ILSpy::Decompiler::TypeSystem::ArrayType>(int32);
	EXPECT_EQ(ambience.ConvertType(*sz), "int32[]");
	auto twoDim = std::make_shared<ILSpy::Decompiler::TypeSystem::ArrayType>(int32, 2);
	EXPECT_EQ(ambience.ConvertType(*twoDim), "int32[,]");
	auto threeDim = std::make_shared<ILSpy::Decompiler::TypeSystem::ArrayType>(int32, 3);
	EXPECT_EQ(ambience.ConvertType(*threeDim), "int32[,,]");
}

TEST(ILAmbienceConvertTypeTest, PointerRendersStar) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	ILAmbience ambience;
	auto ptr = std::make_shared<ILSpy::Decompiler::TypeSystem::PointerType>(int32);
	EXPECT_EQ(ambience.ConvertType(*ptr), "int32*");
}

TEST(ILAmbienceConvertTypeTest, ByReferenceRendersAmpersand) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	ILAmbience ambience;
	auto byRef = std::make_shared<ILSpy::Decompiler::TypeSystem::ByReferenceType>(int32);
	EXPECT_EQ(ambience.ConvertType(*byRef), "int32&");
}

TEST(ILAmbienceConvertTypeTest, ModOptRendersModifier) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	auto isConst = std::make_shared<VisitableDef>("IsConst", compilation);
	ILAmbience ambience;
	auto modopt = std::make_shared<ILSpy::Decompiler::TypeSystem::ModifiedType>(isConst,
		int32, false);
	EXPECT_EQ(ambience.ConvertType(*modopt), "int32 modopt(IsConst)");
}

TEST(ILAmbienceConvertTypeTest, ModReqRendersModifier) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	auto isVolatile = std::make_shared<VisitableDef>("IsVolatile", compilation);
	ILAmbience ambience;
	auto modreq = std::make_shared<ILSpy::Decompiler::TypeSystem::ModifiedType>(isVolatile,
		int32, true);
	EXPECT_EQ(ambience.ConvertType(*modreq), "int32 modreq(IsVolatile)");
}

TEST(ILAmbienceConvertTypeTest, ParameterizedRendersGenericAndArguments) {
	LookupCompilation compilation;
	auto list = std::make_shared<VisitableDef>("List", compilation, KnownTypeCode::None, 2);
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	auto str = std::make_shared<VisitableDef>("String", compilation, KnownTypeCode::String);
	ILAmbience ambience;
	auto parameterized = std::make_shared<ILSpy::Decompiler::TypeSystem::ParameterizedType>(
		list, std::vector<ITypePtr>{int32, str});
	EXPECT_EQ(ambience.ConvertType(*parameterized), "List`2<int32,string>");
}

TEST(ILAmbienceConvertTypeTest, TupleRendersUnderlying) {
	LookupCompilation compilation;
	auto valueTuple = std::make_shared<VisitableDef>("ValueTuple", compilation,
		KnownTypeCode::None, 2);
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	auto str = std::make_shared<VisitableDef>("String", compilation, KnownTypeCode::String);
	ILAmbience ambience;
	auto underlying = std::make_shared<ILSpy::Decompiler::TypeSystem::ParameterizedType>(
		valueTuple, std::vector<ITypePtr>{int32, str});
	auto tuple = std::make_shared<ILSpy::Decompiler::TypeSystem::TupleType>(
		underlying, std::vector<ITypePtr>{int32, str});
	// A tuple renders its underlying System.ValueTuple<...> chain verbatim
	// (the definition's `2 arity included, the WriteType default arm).
	EXPECT_EQ(ambience.ConvertType(*tuple), "ValueTuple`2<int32,string>");
}

TEST(ILAmbienceConvertTypeTest, FunctionPointerDefaultConvention) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	auto str = std::make_shared<VisitableDef>("String", compilation, KnownTypeCode::String);
	ILAmbience ambience;
	auto fpt = std::make_shared<ILSpy::Decompiler::TypeSystem::FunctionPointerType>(
		SignatureCallingConvention::Default, std::vector<ITypePtr>{}, int32, false,
		std::vector<ITypePtr>{int32, str},
		std::vector<ILSpy::Decompiler::TypeSystem::ReferenceKind>{
			ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
			ILSpy::Decompiler::TypeSystem::ReferenceKind::None});
	EXPECT_EQ(ambience.ConvertType(*fpt), "method int32 *(int32, string)");
}

TEST(ILAmbienceConvertTypeTest, FunctionPointerCDeclConvention) {
	LookupCompilation compilation;
	auto int32 = std::make_shared<VisitableDef>("Int32", compilation, KnownTypeCode::Int32);
	ILAmbience ambience;
	auto fpt = std::make_shared<ILSpy::Decompiler::TypeSystem::FunctionPointerType>(
		SignatureCallingConvention::CDecl, std::vector<ITypePtr>{}, int32, false,
		std::vector<ITypePtr>{},
		std::vector<ILSpy::Decompiler::TypeSystem::ReferenceKind>{});
	EXPECT_EQ(ambience.ConvertType(*fpt), "method unmanaged cdecl int32 *()");
}

TEST(ILAmbienceConvertTypeTest, FunctionPointerNullReturnType) {
	LookupCompilation compilation;
	ILAmbience ambience;
	auto fpt = std::make_shared<ILSpy::Decompiler::TypeSystem::FunctionPointerType>(
		SignatureCallingConvention::Default, std::vector<ITypePtr>{}, ITypePtr{}, false,
		std::vector<ITypePtr>{},
		std::vector<ILSpy::Decompiler::TypeSystem::ReferenceKind>{});
	// A null return type renders nothing between the "method " prefix and the
	// " *(" opening -- the two spaces are the faithful juxtaposition.
	EXPECT_EQ(ambience.ConvertType(*fpt), "method  *()");
}

TEST(ILAmbienceConvertTypeTest, TypeParameterRendersEscapedName) {
	LookupCompilation compilation;
	ILAmbience ambience;
	auto tp = std::make_shared<VisitableTypeParameter>("T");
	EXPECT_EQ(ambience.ConvertType(*tp), "T");
	auto odd = std::make_shared<VisitableTypeParameter>("a b");
	EXPECT_EQ(ambience.ConvertType(*odd), "a\\u0020b");
}

TEST(ILAmbienceConvertTypeTest, OtherTypeRendersNameAndArity) {
	LookupCompilation compilation;
	ILAmbience ambience;
	// A definition-shaped type WITHOUT the AcceptVisitor bridge routes to
	// VisitOtherType (the IType default) -- the same WriteType rendering.
	// make_shared: the visitor's return path runs through shared_from_this (the
	// AcceptVisitor discipline -- a stack type would throw bad_weak_ptr).
	auto foo = std::make_shared<LookupTypeDefinition>("Foo", "",
		FullTypeName(TopLevelTypeName("", "Foo", 2)),
		TS::TypeKind::Class, TS::Accessibility::Public, compilation,
		&compilation.MainModule());
	EXPECT_EQ(ambience.ConvertType(*foo), "Foo`2");
}

TEST(ILAmbienceToILSyntaxTest, RendersAllConventions) {
	using SCC = SignatureCallingConvention;
	EXPECT_EQ(ToILSyntax(SCC::Default), "default");
	EXPECT_EQ(ToILSyntax(SCC::CDecl), "unmanaged cdecl");
	EXPECT_EQ(ToILSyntax(SCC::StdCall), "unmanaged stdcall");
	EXPECT_EQ(ToILSyntax(SCC::ThisCall), "unmanaged thiscall");
	EXPECT_EQ(ToILSyntax(SCC::FastCall), "unmanaged fastcall");
	EXPECT_EQ(ToILSyntax(SCC::VarArgs), "vararg");
	EXPECT_EQ(ToILSyntax(SCC::Unmanaged), "unmanaged");
	// An unnamed value renders its numeric string (the C# ToString default).
	EXPECT_EQ(ToILSyntax(static_cast<SCC>(7)), "7");
}

// ===========================================================================
// EscapeName (ILAmbience.cs lines 512-531).
// ===========================================================================
TEST(ILAmbienceEscapeNameTest, AsciiPassesThrough) {
	EXPECT_EQ(ILAmbience::EscapeName("Foo_Bar`2"), "Foo_Bar`2");
}

TEST(ILAmbienceEscapeNameTest, WhitespaceAndControlEscape) {
	// The \x01 literal is split before the 'd' (the C++ hex escape is greedy:
	// "\x01d" would be the single character 0x1D).
	EXPECT_EQ(ILAmbience::EscapeName("a b\tc\x01" "d"), "a\\u0020b\\u0009c\\u0001d");
}

TEST(ILAmbienceEscapeNameTest, NonBmpRendersBothSurrogateHalves) {
	// U+1F600 (the UTF-8 bytes F0 9F 98 80) iterates as the two UTF-16 halves
	// \ud83d\ude00 in the C# -- each IsSurrogate half escapes separately.
	EXPECT_EQ(ILAmbience::EscapeName("\xF0\x9F\x98\x80"), "\\ud83d\\ude00");
}

TEST(ILAmbienceEscapeNameTest, BuilderOverloadAppendsAndReturns) {
	std::string sb = "x";
	auto& result = ILAmbience::EscapeName(sb, "a b");
	EXPECT_EQ(&result, &sb);
	EXPECT_EQ(sb, "xa\\u0020b");
}
