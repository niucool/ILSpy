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

// Tests for `IProperty` (cpp/Decompiler/TypeSystem/IProperty.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IProperty.cs). `IProperty : IParameterizedMember` represents a
// property or indexer; it adds the `CanGet` / `CanSet` availability flags, the nullable
// `Getter` / `Setter` accessor back-references, the `IsIndexer` flag, and the
// `ReturnTypeIsRefReadOnly` flag.
//
// The test stub `TestProperty` derives from the real `IProperty` and overrides every
// `IParameterizedMember` / `IMember` / `IEntity` / `ICompilationProvider` / `INamedElement` /
// `ISymbol` pure-virtual plus the `IProperty`-own accessors, the shape a real `MetadataProperty`
// / `SpecializedProperty` takes.
//
// The inherited `ICompilationProvider::Compilation()` reference return needs a complete
// `ICompilation` to bind to, so this test file provides the IDENTICAL minimal `ICompilation`
// stand-in used by `ICompilationProvider_Test.cpp` / `IEntity_Test.cpp` / `ITypeParameter_Test.cpp`
// / `IMember_Test.cpp` / `IParameterizedMember_Test.cpp` / `IMethod_Test.cpp`
// (virtual destructor only); the identical class definitions across translation units satisfy
// the One Definition Rule. `IAttribute` is the real port (D386), included below. `IMethod` is the
// real port (D389), included below so the `Getter` / `Setter` accessors can hold concrete
// `TestMethod` instances.

#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// can return a compilation (the D379 test stand-in pattern).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id), mainModule_(*this) {}

    int id() const { return id_; }

    const ILSpy::Decompiler::TypeSystem::IModule& MainModule() const override
    {
        return mainModule_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> Modules() const override
    {
        return {&mainModule_};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ReferencedModules() const override
    {
        return {};
    }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override
    {
        return mainModule_.RootNamespace();
    }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetNamespaceForExternAlias(
        const std::string&) const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IType& FindType(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode) const override
    {
        return knownType_;
    }
    const ILSpy::Decompiler::TypeSystem::StringComparer& NameComparer() const override
    {
        return ILSpy::Decompiler::TypeSystem::StringComparer::Ordinal();
    }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
    }

private:
    int id_;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `IAttribute` for testing. Derives from the real `IAttribute` (D386) and
// implements every pure-virtual with simple defaults (`AttributeType` returns a
// `KnownType(Object)` by reference, `Constructor` is null, `HasDecodeErrors` is false, the
// argument vectors are empty); the test-specific `Kind()` accessor and `KnownAttribute kind_`
// member are kept so attribute-classification tests continue to work. IDENTICAL in shape to the
// `TestAttribute` in `IEntity_Test.cpp` / `IParameter_Test.cpp` / `IMember_Test.cpp` /
// `IParameterizedMember_Test.cpp` / `IMethod_Test.cpp`.
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute kind)
        : kind_(kind), attributeType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}
    ILSpy::Decompiler::TypeSystem::KnownAttribute Kind() const { return kind_; }

    // --- IAttribute ---
    const ILSpy::Decompiler::TypeSystem::IType& AttributeType() const override { return attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> FixedArguments() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    ILSpy::Decompiler::TypeSystem::KnownAttribute kind_;
    ILSpy::Decompiler::TypeSystem::KnownType attributeType_;
};

// A minimal concrete `IParameter` for testing: only `Name` and `SymbolKind` are exercised by
// the `IProperty` tests (the `Parameters` snapshot is checked by pointer identity and by reading
// a parameter's name through the snapshot); the remaining accessors return simple defaults so the
// concrete stub compiles. IDENTICAL in shape to the `TestParameter` in
// `IParameter_Test.cpp` / `IParameterizedMember_Test.cpp` / `IMethod_Test.cpp`.
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(ILSpy::Decompiler::TypeSystem::SymbolKind kind, std::string name,
                  ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : kind_(kind), name_(std::move(name)), type_(std::move(type)) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- IVariable ---
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override
    {
        return {};
    }

    // --- IParameter ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::ReferenceKind::None;
    }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override
    {
        return ILSpy::Decompiler::TypeSystem::LifetimeAnnotation{};
    }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override
    {
        return nullptr;
    }

private:
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
};

// A minimal concrete `IMethod` for testing: the `Getter` / `Setter` accessors of `TestProperty`
// return `const IMethod*` pointers to `TestMethod` instances, and the accessor-back-reference
// test sets a `TestMethod`'s `AccessorOwner` back to the property. The stub holds the
// configured scalar/pointer state and returns it from every accessor (the shape a real
// `MetadataMethod` / `SpecializedMethod` / `LocalFunctionMethod` takes). `Name()` is overridden
// ONCE and satisfies the `ISymbol::Name()` / `INamedElement::Name()` / `IEntity::Name()`
// contracts (the diamond is disambiguated by `IEntity`'s redeclaration, and a single override
// is the final overrider for all three). The three long-pole-dep members (`Substitution` /
// `Specialize` / `Equals`) use the `nullptr` / `this` / identity stand-ins, but `Specialize`
// uses the COVARIANT return (`const IMethod*`), the documented mirror of the C# `new IMethod
// Specialize(...)`. IDENTICAL in shape to the `TestMethod` in `IMethod_Test.cpp`.
class TestMethod : public ILSpy::Decompiler::TypeSystem::IMethod {
public:
    TestMethod(std::string name,
               std::string fullName,
               std::string reflectionName,
               std::string ns,
               ILSpy::Decompiler::TypeSystem::SymbolKind kind,
               const TestCompilation& compilation,
               std::uint32_t metadataToken,
               ILSpy::Decompiler::TypeSystem::ITypePtr returnType,
               ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
               bool isStatic, bool isAbstract, bool isSealed,
               bool isExplicitInterfaceImplementation,
               bool isVirtual, bool isOverride, bool isOverridable,
               bool returnTypeIsRefReadOnly, bool isInitOnly, bool thisIsRefReadOnly,
               bool isExtensionMethod, bool isLocalFunction, bool isConstructor,
               bool isDestructor, bool isOperator, bool hasBody, bool isAccessor,
               ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes accessorKind)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          reflectionName_(std::move(reflectionName)), namespace_(std::move(ns)),
          kind_(kind), compilation_(compilation), metadataToken_(metadataToken),
          returnType_(std::move(returnType)), accessibility_(accessibility),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed),
          isExplicitInterfaceImplementation_(isExplicitInterfaceImplementation),
          isVirtual_(isVirtual), isOverride_(isOverride), isOverridable_(isOverridable),
          returnTypeIsRefReadOnly_(returnTypeIsRefReadOnly), isInitOnly_(isInitOnly),
          thisIsRefReadOnly_(thisIsRefReadOnly), isExtensionMethod_(isExtensionMethod),
          isLocalFunction_(isLocalFunction), isConstructor_(isConstructor),
          isDestructor_(isDestructor), isOperator_(isOperator), hasBody_(hasBody),
          isAccessor_(isAccessor), accessorKind_(accessorKind) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return fullName_; }
    std::string ReflectionName() const override { return reflectionName_; }
    std::string Namespace() const override { return namespace_; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute /*attribute*/) const override
    {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute /*attribute*/) const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return isAbstract_; }
    bool IsSealed() const override { return isSealed_; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return isExplicitInterfaceImplementation_; }
    bool IsVirtual() const override { return isVirtual_; }
    bool IsOverride() const override { return isOverride_; }
    bool IsOverridable() const override { return isOverridable_; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override
    {
        return nullptr;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor* /*typeNormalization*/) const override
    {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override
    {
        return {};
    }

    // --- IMethod ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetReturnTypeAttributes() const override
    {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsInitOnly() const override { return isInitOnly_; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> TypeParameters() const override
    {
        return {};
    }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> TypeArguments() const override
    {
        return {};
    }
    bool IsExtensionMethod() const override { return isExtensionMethod_; }
    bool IsLocalFunction() const override { return isLocalFunction_; }
    bool IsConstructor() const override { return isConstructor_; }
    bool IsDestructor() const override { return isDestructor_; }
    bool IsOperator() const override { return isOperator_; }
    bool HasBody() const override { return hasBody_; }
    bool IsAccessor() const override { return isAccessor_; }
    const ILSpy::Decompiler::TypeSystem::IMember* AccessorOwner() const override { return accessorOwner_; }
    ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes AccessorKind() const override { return accessorKind_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* ReducedFrom() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* /*substitution*/) const override
    {
        return this;
    }

    // Test wiring (set the nullable accessor-owner slot after construction).
    void SetAccessorOwner(const ILSpy::Decompiler::TypeSystem::IMember* owner) { accessorOwner_ = owner; }

private:
    std::string name_, fullName_, reflectionName_, namespace_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    bool isStatic_, isAbstract_, isSealed_;
    bool isExplicitInterfaceImplementation_, isVirtual_, isOverride_, isOverridable_;
    bool returnTypeIsRefReadOnly_, isInitOnly_, thisIsRefReadOnly_;
    bool isExtensionMethod_, isLocalFunction_, isConstructor_, isDestructor_, isOperator_;
    bool hasBody_, isAccessor_;
    ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes accessorKind_;
    const ILSpy::Decompiler::TypeSystem::IMember* accessorOwner_ = nullptr;
};

// A minimal concrete `IProperty` for testing: holds the configured scalar/pointer state and
// returns it from every accessor (the shape a real `MetadataProperty` / `SpecializedProperty`
// takes). `Name()` is overridden ONCE and satisfies the `ISymbol::Name()` / `INamedElement::Name()`
// / `IEntity::Name()` contracts (the diamond is disambiguated by `IEntity`'s redeclaration, and
// a single override is the final overrider for all three). The three long-pole-dep members
// (`Substitution` / `Specialize` / `Equals`) use the `nullptr` / `this` / identity stand-ins.
// The `Getter` / `Setter` slots are nullable `const IMethod*` (set after construction), and the
// `Parameters` snapshot is a non-owning pointer list (the `IParameterizedMember::Parameters`
// convention).
class TestProperty : public ILSpy::Decompiler::TypeSystem::IProperty {
public:
    TestProperty(std::string name,
                 std::string fullName,
                 std::string reflectionName,
                 std::string ns,
                 ILSpy::Decompiler::TypeSystem::SymbolKind kind,
                 const TestCompilation& compilation,
                 std::uint32_t metadataToken,
                 ILSpy::Decompiler::TypeSystem::ITypePtr returnType,
                 ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
                 bool isStatic, bool isAbstract, bool isSealed,
                 bool isExplicitInterfaceImplementation,
                 bool isVirtual, bool isOverride, bool isOverridable,
                 bool canGet, bool canSet, bool isIndexer, bool returnTypeIsRefReadOnly)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          reflectionName_(std::move(reflectionName)), namespace_(std::move(ns)),
          kind_(kind), compilation_(compilation), metadataToken_(metadataToken),
          returnType_(std::move(returnType)), accessibility_(accessibility),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed),
          isExplicitInterfaceImplementation_(isExplicitInterfaceImplementation),
          isVirtual_(isVirtual), isOverride_(isOverride), isOverridable_(isOverridable),
          canGet_(canGet), canSet_(canSet), isIndexer_(isIndexer),
          returnTypeIsRefReadOnly_(returnTypeIsRefReadOnly) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    // The single `Name()` override is the final overrider for `ISymbol::Name()` /
    // `INamedElement::Name()` / `IEntity::Name()`.
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return fullName_; }
    std::string ReflectionName() const override { return reflectionName_; }
    std::string Namespace() const override { return namespace_; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return metadataToken_; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return attributes_;
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        for (auto* a : attributes_) {
            if (static_cast<const TestAttribute*>(a)->Kind() == attribute) return true;
        }
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute attribute) const override
    {
        for (auto* a : attributes_) {
            if (static_cast<const TestAttribute*>(a)->Kind() == attribute) return a;
        }
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return isAbstract_; }
    bool IsSealed() const override { return isSealed_; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return isExplicitInterfaceImplementation_; }
    bool IsVirtual() const override { return isVirtual_; }
    bool IsOverride() const override { return isOverride_; }
    bool IsOverridable() const override { return isOverridable_; }
    // The long-pole-dep stand-ins: `Substitution` returns `nullptr`
    // (`TypeParameterSubstitution` is forward-declared), `Specialize` returns `this` (a
    // degenerate stand-in for the real newly-specialized member), `Equals` is identity
    // comparison.
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* /*substitution*/) const override
    {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor* /*typeNormalization*/) const override
    {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override
    {
        return parameters_;
    }

    // --- IProperty ---
    bool CanGet() const override { return canGet_; }
    bool CanSet() const override { return canSet_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Getter() const override { return getter_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Setter() const override { return setter_; }
    bool IsIndexer() const override { return isIndexer_; }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }

    // Test wiring (set the nullable / collection slots after construction).
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void AddParameter(const ILSpy::Decompiler::TypeSystem::IParameter* p)
    {
        parameters_.push_back(p);
    }
    void SetGetter(const ILSpy::Decompiler::TypeSystem::IMethod* g) { getter_ = g; }
    void SetSetter(const ILSpy::Decompiler::TypeSystem::IMethod* s) { setter_ = s; }

private:
    std::string name_, fullName_, reflectionName_, namespace_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    bool isStatic_, isAbstract_, isSealed_;
    bool isExplicitInterfaceImplementation_, isVirtual_, isOverride_, isOverridable_;
    bool canGet_, canSet_, isIndexer_, returnTypeIsRefReadOnly_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters_;
    const ILSpy::Decompiler::TypeSystem::IMethod* getter_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IMethod* setter_ = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// IProperty -- the IProperty-own scalar/flag accessors return the configured values: the
// `CanGet` / `CanSet` availability flags, the `IsIndexer` flag, and the
// `ReturnTypeIsRefReadOnly` flag. A read-write property has both flags true; a read-only
// property has `CanSet` false; an indexer has `IsIndexer` true.
// ---------------------------------------------------------------------------
TEST(IPropertyTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestProperty readWrite("Count", "C.Count", "C.Count", "", SymbolKind::Property,
        compilation, 0x17000001u, returnType, Accessibility::Public,
        /*isStatic*/ false, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false,
        /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false,
        /*canGet*/ true, /*canSet*/ true, /*isIndexer*/ false, /*returnTypeIsRefReadOnly*/ false);

    IProperty* p = &readWrite;
    EXPECT_TRUE(p->CanGet());
    EXPECT_TRUE(p->CanSet());
    EXPECT_FALSE(p->IsIndexer());
    EXPECT_FALSE(p->ReturnTypeIsRefReadOnly());
}

// ---------------------------------------------------------------------------
// IProperty -- `Getter` / `Setter` return the configured non-owning `const IMethod*` pointers
// (the property's accessor methods), or null when the property is write-only / read-only. A
// read-write property has both a `Getter` and a `Setter`; a read-only property has a `Getter`
// but a null `Setter`; a write-only property has a null `Getter` but a `Setter`.
// ---------------------------------------------------------------------------
TEST(IPropertyTest, GetterAndSetterReturnConfiguredAccessors)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    // A read-write property: getter returns the configured IMethod, setter returns the
    // configured IMethod.
    TestProperty count("Count", "C.Count", "C.Count", "", SymbolKind::Property,
        compilation, 0x17000002u, stringType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canGet*/ true, /*canSet*/ true, /*isIndexer*/ false, /*returnTypeIsRefReadOnly*/ false);
    TestMethod getter("get_Count", "C.get_Count", "C.get_Count", "", SymbolKind::Method,
        compilation, 0x06000002u, voidType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Getter);
    TestMethod setter("set_Count", "C.set_Count", "C.set_Count", "", SymbolKind::Method,
        compilation, 0x06000003u, voidType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Setter);
    count.SetGetter(&getter);
    count.SetSetter(&setter);

    EXPECT_EQ(count.Getter(), &getter);
    EXPECT_EQ(count.Setter(), &setter);
    EXPECT_EQ(count.Getter()->Name(), "get_Count");
    EXPECT_EQ(count.Getter()->AccessorKind(), MethodSemanticsAttributes::Getter);
    EXPECT_EQ(count.Setter()->AccessorKind(), MethodSemanticsAttributes::Setter);

    // A read-only property: getter is set, setter is null.
    TestProperty readOnly("ReadOnly", "C.ReadOnly", "C.ReadOnly", "", SymbolKind::Property,
        compilation, 0x17000003u, stringType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canGet*/ true, /*canSet*/ false, /*isIndexer*/ false, /*returnTypeIsRefReadOnly*/ true);
    readOnly.SetGetter(&getter);
    EXPECT_EQ(readOnly.Getter(), &getter);
    EXPECT_EQ(readOnly.Setter(), nullptr);
    EXPECT_TRUE(readOnly.CanGet());
    EXPECT_FALSE(readOnly.CanSet());
    EXPECT_TRUE(readOnly.ReturnTypeIsRefReadOnly());
}

// ---------------------------------------------------------------------------
// IProperty -- the accessor-back-reference round-trip: a property's `Getter` is an `IMethod`
// whose `AccessorOwner` points back to the property (the `[MemberNotNullWhen]` invariant made
// concrete), and whose `IsAccessor` is true and `AccessorKind` is `Getter`. The property and
// its accessor form a two-way handle pair.
// ---------------------------------------------------------------------------
TEST(IPropertyTest, AccessorBackReferenceRoundTrip)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestProperty count("Count", "C.Count", "C.Count", "", SymbolKind::Property,
        compilation, 0x17000004u, stringType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canGet*/ true, /*canSet*/ true, /*isIndexer*/ false, /*returnTypeIsRefReadOnly*/ false);
    TestMethod getter("get_Count", "C.get_Count", "C.get_Count", "", SymbolKind::Method,
        compilation, 0x06000004u, voidType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Getter);
    getter.SetAccessorOwner(&count);
    count.SetGetter(&getter);

    // The property's getter points to the method, the method's accessor-owner points back.
    EXPECT_EQ(count.Getter(), &getter);
    ASSERT_NE(count.Getter(), nullptr);
    EXPECT_EQ(count.Getter()->AccessorOwner(), &count);
    EXPECT_TRUE(count.Getter()->IsAccessor());
    EXPECT_EQ(count.Getter()->AccessorKind(), MethodSemanticsAttributes::Getter);
    // The property is the accessor owner (an `IMember`); `IsAccessor` is an `IMethod`-own
    // accessor, NOT inherited by `IProperty` (which derives from `IParameterizedMember` /
    // `IMember`, NOT `IMethod`), so it is not reachable through the property.
    EXPECT_EQ(count.Getter()->AccessorOwner(), &count);
}

// ---------------------------------------------------------------------------
// IProperty -- an indexer is a parameterized property: `IsIndexer` is true and `Parameters`
// returns the configured non-owning snapshot (the indexer's index parameters), so the
// inherited `IParameterizedMember::Parameters` accessor dispatches through the `IProperty*`.
// ---------------------------------------------------------------------------
TEST(IPropertyTest, IndexerHasParametersAndIsIndexer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestProperty indexer("Item", "C.Item", "C.Item", "", SymbolKind::Property,
        compilation, 0x17000005u, stringType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canGet*/ true, /*canSet*/ true, /*isIndexer*/ true, /*returnTypeIsRefReadOnly*/ false);
    TestParameter index(SymbolKind::Parameter, "index", intType);
    indexer.AddParameter(&index);

    EXPECT_TRUE(indexer.IsIndexer());
    const auto params = indexer.Parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0], &index);
    EXPECT_EQ(params[0]->Name(), "index");
    EXPECT_EQ(params[0]->SymbolKind(), SymbolKind::Parameter);
}

// ---------------------------------------------------------------------------
// IProperty -- polymorphic dispatch through the `IParameterizedMember*` / `IMember*` /
// `IEntity*` / `ISymbol*` base pointers (an `IProperty` IS-A each of its bases, so each base
// pointer dispatches to the concrete override, including the single `Name()` that is the final
// overrider for all three name-declaring bases). The IProperty-own accessors are reachable
// through `IProperty*` / `IParameterizedMember*` but NOT through `IMember*` (a plain `IMember`
// field/event has no `CanGet` / `Getter`).
// ---------------------------------------------------------------------------
TEST(IPropertyTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto owned = std::make_unique<TestProperty>(
        "Count", "C.Count", "C.Count", "", SymbolKind::Property, compilation, 0x17000006u, stringType,
        Accessibility::Public, false, false, false, false, false, false, false,
        /*canGet*/ true, /*canSet*/ true, /*isIndexer*/ false, /*returnTypeIsRefReadOnly*/ false);
    IParameterizedMember* asPM = owned.get();
    IMember* asMember = owned.get();
    IEntity* asEntity = owned.get();
    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asPM->Name(), "Count");
    EXPECT_EQ(asPM->SymbolKind(), SymbolKind::Property);
    EXPECT_TRUE(asPM->Parameters().empty());
    EXPECT_EQ(asMember->Name(), "Count");
    EXPECT_EQ(asMember->MemberDefinition(), owned.get());
    EXPECT_EQ(asEntity->Name(), "Count");
    EXPECT_EQ(asEntity->MetadataToken(), 0x17000006u);
    EXPECT_EQ(asSymbol->Name(), "Count");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Property);
    // destroying `owned` runs the `TestProperty` destructor through the virtual `~IProperty()`
    // (which chains to `~IParameterizedMember()` / `~IMember()` / `~IEntity()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IProperty -- has a virtual destructor (a concrete subclass can be deleted through an
// `IProperty*` / `IParameterizedMember*` / `IMember*` / `IEntity*` / `ISymbol*` and the derived
// destructor runs), the established abstract-base contract; it is abstract (every accessor is
// pure-virtual) and polymorphic. The single-inheritance `IProperty` -> `IParameterizedMember`
// -> `IMember` -> `IEntity` -> ... chain composes correctly.
// ---------------------------------------------------------------------------
TEST(IPropertyTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IProperty>,
        "IProperty must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IParameterizedMember>,
        "IParameterizedMember (the base of IProperty) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IProperty>,
        "IProperty must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IProperty>,
        "IProperty must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    std::unique_ptr<IProperty> owned = std::make_unique<TestProperty>(
        "P", "C.P", "C.P", "", SymbolKind::Property, compilation, 0x17000007u, stringType,
        Accessibility::Public, false, false, false, false, false, false, false,
        /*canGet*/ true, /*canSet*/ false, /*isIndexer*/ false, /*returnTypeIsRefReadOnly*/ false);
    EXPECT_EQ(owned->Name(), "P");
    EXPECT_TRUE(owned->CanGet());
    EXPECT_FALSE(owned->CanSet());
    EXPECT_TRUE(owned->Parameters().empty());
    // destroying `owned` runs the `TestProperty` destructor through the virtual `~IProperty()`.
    owned.reset();
    SUCCEED();
}
