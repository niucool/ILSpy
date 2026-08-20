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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `IMethod` (cpp/Decompiler/TypeSystem/IMethod.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IMethod.cs). `IMethod : IParameterizedMember` represents a
// method, constructor, destructor, or operator; it adds the return-type attributes, the
// ref-readonly / init-only / readonly-struct flags, the method's own type parameters and type
// arguments, the extension-method / local-function / constructor / destructor / operator /
// has-body flags, the accessor triple (`IsAccessor` / `AccessorOwner` / `AccessorKind`), the
// reduced-from pointer, and a covariant `Specialize` override (the FIRST ported interface
// member using a covariant return type to mirror the C# `new IMethod Specialize(...)`).
//
// The test stub `TestMethod` derives from the real `IMethod` and overrides every
// `IParameterizedMember` / `IMember` / `IEntity` / `ICompilationProvider` / `INamedElement` /
// `ISymbol` pure-virtual plus the `IMethod`-own accessors, the shape a real `MetadataMethod`
// / `SpecializedMethod` / `LocalFunctionMethod` takes.
//
// The inherited `ICompilationProvider::Compilation()` reference return needs a complete
// `ICompilation` to bind to, so this test file provides the IDENTICAL minimal `ICompilation`
// stand-in used by `ICompilationProvider_Test.cpp` / `IEntity_Test.cpp` /
// `ITypeParameter_Test.cpp` / `IMember_Test.cpp` / `IParameterizedMember_Test.cpp`
// (virtual destructor only); the identical class definitions across translation units satisfy
// the One Definition Rule. `IAttribute` is the real port (D386), included below.
// `ITypeParameter` is the real port (D383), included below so the `TypeParameters` snapshot can
// hold concrete `TestTypeParameter` instances.

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
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
// `IParameterizedMember_Test.cpp`.
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
// the `IMethod` tests (the `Parameters` snapshot is checked by pointer identity and by reading
// a parameter's name through the snapshot); the remaining accessors return simple defaults so
// the concrete stub compiles. IDENTICAL in shape to the `TestParameter` in
// `IParameter_Test.cpp` / `IParameterizedMember_Test.cpp`.
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

// A minimal concrete `ITypeParameter` for testing: only `Name` and `SymbolKind` are exercised
// by the `IMethod` tests (the `TypeTemplates` snapshot is checked by pointer identity and by
// reading a type parameter's name through the snapshot). The remaining accessors return simple
// defaults so the concrete stub compiles.
class TestTypeParameter : public ILSpy::Decompiler::TypeSystem::ITypeParameter {
public:
    TestTypeParameter(std::string name,
                      ILSpy::Decompiler::TypeSystem::SymbolKind ownerKind)
        : name_(std::move(name)), ownerKind_(ownerKind) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }
    // The single `Name()` override is the final overrider for `IType::Name()` and
    // `ISymbol::Name()` (the D383 ITypeParameter diamond, disambiguated by the redeclaration).
    std::string Name() const override { return name_; }

    // --- IType (the minimal-port accessors) ---
    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override
    {
        return ILSpy::Decompiler::TypeSystem::TypeKind::TypeParameter;
    }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }

protected:
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType& /*other*/) const override
    {
        return false;
    }

public:
    // --- ITypeParameter ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override { return ownerKind_; }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return 0; }
    ILSpy::Decompiler::TypeSystem::ITypePtr EffectiveBaseClass() const override { return nullptr; }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> EffectiveInterfaceSet() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    ILSpy::Decompiler::TypeSystem::VarianceModifier Variance() const override
    {
        return ILSpy::Decompiler::TypeSystem::VarianceModifier::Invariant;
    }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override
    {
        return ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    std::vector<ILSpy::Decompiler::TypeSystem::TypeConstraint> TypeConstraints() const override
    {
        return {};
    }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::SymbolKind ownerKind_;
};

// A minimal concrete `IMethod` for testing: holds the configured scalar/pointer state and
// returns it from every accessor (the shape a real `MetadataMethod` / `SpecializedMethod` /
// `LocalFunctionMethod` takes). `Name()` is overridden ONCE and satisfies the
// `ISymbol::Name()` / `INamedElement::Name()` / `IEntity::Name()` contracts (the diamond is
// disambiguated by `IEntity`'s redeclaration, and a single override is the final overrider for
// all three). The three long-pole-dep members (`Substitution` / `Specialize` / `Equals`) use
// the `nullptr` / `this` / identity stand-ins, but `Specialize` uses the COVARIANT return
// (`const IMethod*`), the documented mirror of the C# `new IMethod Specialize(...)`.
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
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return declaringType_; }
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
        return explicitlyImplementedInterfaceMembers_;
    }
    bool IsExplicitInterfaceImplementation() const override { return isExplicitInterfaceImplementation_; }
    bool IsVirtual() const override { return isVirtual_; }
    bool IsOverride() const override { return isOverride_; }
    bool IsOverridable() const override { return isOverridable_; }
    // The long-pole-dep stand-ins: `Substitution` returns `nullptr`
    // (`TypeParameterSubstitution` is forward-declared), `Equals` is identity comparison.
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
        return parameters_;
    }

    // --- IMethod ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetReturnTypeAttributes() const override
    {
        return returnTypeAttributes_;
    }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsInitOnly() const override { return isInitOnly_; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> TypeParameters() const override
    {
        return typeParameters_;
    }
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> TypeArguments() const override
    {
        return typeArguments_;
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
    const ILSpy::Decompiler::TypeSystem::IMethod* ReducedFrom() const override { return reducedFrom_; }
    // The covariant `Specialize` override (the C# `new IMethod Specialize(...)`): returns
    // `this` (a degenerate stand-in for the real newly-specialized method) with the covariant
    // `const IMethod*` return type, overriding `IMember::Specialize`'s `const IMember*`.
    const ILSpy::Decompiler::TypeSystem::IMethod* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* /*substitution*/) const override
    {
        return this;
    }

    // Test wiring (set the nullable / collection slots after construction).
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void AddReturnTypeAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a)
    {
        returnTypeAttributes_.push_back(a);
    }
    void AddExplicitlyImplementedInterfaceMember(const ILSpy::Decompiler::TypeSystem::IMember* m)
    {
        explicitlyImplementedInterfaceMembers_.push_back(m);
    }
    void AddParameter(const ILSpy::Decompiler::TypeSystem::IParameter* p)
    {
        parameters_.push_back(p);
    }
    void AddTypeParameter(const ILSpy::Decompiler::TypeSystem::ITypeParameter* t)
    {
        typeParameters_.push_back(t);
    }
    void AddTypeArgument(ILSpy::Decompiler::TypeSystem::ITypePtr t)
    {
        typeArguments_.push_back(std::move(t));
    }
    void SetAccessorOwner(const ILSpy::Decompiler::TypeSystem::IMember* owner) { accessorOwner_ = owner; }
    void SetReducedFrom(const ILSpy::Decompiler::TypeSystem::IMethod* reducedFrom) { reducedFrom_ = reducedFrom; }

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
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> returnTypeAttributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> explicitlyImplementedInterfaceMembers_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters_;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeParameter*> typeParameters_;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments_;
    const ILSpy::Decompiler::TypeSystem::IMember* accessorOwner_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IMethod* reducedFrom_ = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// IMethod -- the IMethod-own scalar/flag accessors return the configured values: the
// ref-readonly / init-only / readonly-struct flags, the extension-method / local-function /
// constructor / destructor / operator / has-body flags, and the accessor-kind enum.
// ---------------------------------------------------------------------------
TEST(IMethodTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestMethod op_implicit("op_Implicit", "C.op_Implicit", "C.op_Implicit", "", SymbolKind::Method,
        compilation, 0x06000001u, returnType, Accessibility::Public,
        /*isStatic*/ true, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false,
        /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false,
        /*returnTypeIsRefReadOnly*/ false, /*isInitOnly*/ false, /*thisIsRefReadOnly*/ false,
        /*isExtensionMethod*/ false, /*isLocalFunction*/ false, /*isConstructor*/ false,
        /*isDestructor*/ false, /*isOperator*/ true, /*hasBody*/ true, /*isAccessor*/ false,
        /*accessorKind*/ MethodSemanticsAttributes::None);

    IMethod* m = &op_implicit;
    EXPECT_EQ(m->ReturnTypeIsRefReadOnly(), false);
    EXPECT_EQ(m->IsInitOnly(), false);
    EXPECT_EQ(m->ThisIsRefReadOnly(), false);
    EXPECT_EQ(m->IsExtensionMethod(), false);
    EXPECT_EQ(m->IsLocalFunction(), false);
    EXPECT_EQ(m->IsConstructor(), false);
    EXPECT_EQ(m->IsDestructor(), false);
    EXPECT_EQ(m->IsOperator(), true);
    EXPECT_EQ(m->HasBody(), true);
    EXPECT_EQ(m->IsAccessor(), false);
    EXPECT_EQ(m->AccessorKind(), MethodSemanticsAttributes::None);
    EXPECT_EQ(m->ReducedFrom(), nullptr);
    EXPECT_EQ(m->AccessorOwner(), nullptr);
}

// ---------------------------------------------------------------------------
// IMethod -- `GetReturnTypeAttributes` returns the configured non-owning snapshot (the
// method owns its return-type attributes; the caller holds raw pointers), matching the
// `IEntity::GetAttributes` precedent. A method with two return-type attributes returns them
// in insertion order, and the snapshot's pointers are identity-comparable to the originals.
// ---------------------------------------------------------------------------
TEST(IMethodTest, GetReturnTypeAttributesReturnsConfiguredSnapshot)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestMethod method("Add", "C.Add", "C.Add", "", SymbolKind::Method,
        compilation, 0x06000002u, returnType, Accessibility::Public,
        false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, true, false,
        MethodSemanticsAttributes::None);
    TestAttribute marshalAs(KnownAttribute::MarshalAs);
    TestAttribute obsolete(KnownAttribute::Obsolete);
    method.AddReturnTypeAttribute(&marshalAs);
    method.AddReturnTypeAttribute(&obsolete);

    const auto attrs = method.GetReturnTypeAttributes();
    ASSERT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0], &marshalAs);
    EXPECT_EQ(attrs[1], &obsolete);
    EXPECT_EQ(static_cast<const TestAttribute*>(attrs[0])->Kind(), KnownAttribute::MarshalAs);
    EXPECT_EQ(static_cast<const TestAttribute*>(attrs[1])->Kind(), KnownAttribute::Obsolete);
    // The entity-level attributes are separate from the return-type attributes.
    EXPECT_TRUE(method.GetAttributes().empty());
}

// ---------------------------------------------------------------------------
// IMethod -- `TypeParameters` returns the configured non-owning snapshot of the method's own
// generic type parameters (empty for a non-generic method), and `TypeArguments` returns the
// shared-handle snapshot of the type arguments passed to the method.
// ---------------------------------------------------------------------------
TEST(IMethodTest, TypeParametersAndTypeArgumentsReturnConfiguredSnapshots)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestMethod method("Map", "C.Map", "C.Map", "", SymbolKind::Method,
        compilation, 0x06000003u, returnType, Accessibility::Public,
        false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, true, false,
        MethodSemanticsAttributes::None);
    TestTypeParameter tParam("T", SymbolKind::Method);
    TestTypeParameter uParam("U", SymbolKind::Method);
    method.AddTypeParameter(&tParam);
    method.AddTypeParameter(&uParam);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    method.AddTypeArgument(intType);
    method.AddTypeArgument(stringType);

    const auto tparams = method.TypeParameters();
    ASSERT_EQ(tparams.size(), 2u);
    EXPECT_EQ(tparams[0], &tParam);
    EXPECT_EQ(tparams[1], &uParam);
    EXPECT_EQ(tparams[0]->Name(), "T");
    EXPECT_EQ(tparams[1]->Name(), "U");
    EXPECT_EQ(tparams[0]->SymbolKind(), SymbolKind::TypeParameter);

    const auto targs = method.TypeArguments();
    ASSERT_EQ(targs.size(), 2u);
    EXPECT_EQ(targs[0].get(), intType.get());
    EXPECT_EQ(targs[1].get(), stringType.get());
    EXPECT_EQ(targs[0]->Name(), "Int32");
}

// ---------------------------------------------------------------------------
// IMethod -- the accessor triple: `IsAccessor` true, `AccessorOwner` returns the configured
// property/event, and `AccessorKind` is the configured semantics (Getter / Setter / Adder /
// Remover / Raiser). A non-accessor method has `IsAccessor` false and `AccessorOwner` null.
// ---------------------------------------------------------------------------
TEST(IMethodTest, AccessorTriple)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    // A property getter: IsAccessor true, AccessorOwner the property, AccessorKind Getter.
    TestMethod getter("get_Count", "C.get_Count", "C.get_Count", "", SymbolKind::Method,
        compilation, 0x06000004u, returnType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Getter);
    // A minimal property (a second method acting as the accessor owner -- the shape a real
    // IProperty takes is not yet ported, so a second IMethod stands in as the IMember owner).
    TestMethod countProperty("Count", "C.Count", "C.Count", "", SymbolKind::Property,
        compilation, 0x06000005u, returnType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, false,
        MethodSemanticsAttributes::None);
    getter.SetAccessorOwner(&countProperty);

    EXPECT_TRUE(getter.IsAccessor());
    EXPECT_EQ(getter.AccessorOwner(), &countProperty);
    EXPECT_EQ(getter.AccessorKind(), MethodSemanticsAttributes::Getter);
    // The non-accessor property-as-owner has IsAccessor false and AccessorOwner null.
    EXPECT_FALSE(countProperty.IsAccessor());
    EXPECT_EQ(countProperty.AccessorOwner(), nullptr);
    EXPECT_EQ(countProperty.AccessorKind(), MethodSemanticsAttributes::None);
}

// ---------------------------------------------------------------------------
// IMethod -- the COVARIANT `Specialize` override (the C# `new IMethod Specialize(...)`):
// dispatching through an `IMethod*` returns a `const IMethod*`; dispatching through an
// `IMember*` reaches the SAME override (one C++ slot) and the returned `const IMethod*`
// implicitly converts to `const IMember*` (the derived-to-base pointer conversion) -- the
// observable behavior of the C# two vtable slots (the public `IMethod` slot and the explicit
// `IMember` slot both returning the same specialized method object), realized as one C++ slot.
// ---------------------------------------------------------------------------
TEST(IMethodTest, CovariantSpecializeOverride)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestMethod method("Run", "C.Run", "C.Run", "", SymbolKind::Method,
        compilation, 0x06000006u, returnType, Accessibility::Public,
        false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, true, false,
        MethodSemanticsAttributes::None);

    IMethod* asIMethod = &method;
    const IMethod* specializedThroughIMethod = asIMethod->Specialize(nullptr);
    EXPECT_EQ(specializedThroughIMethod, &method);

    IMember* asIMember = &method;
    const IMember* specializedThroughIMember = asIMember->Specialize(nullptr);
    EXPECT_EQ(specializedThroughIMember, static_cast<const IMember*>(&method));
    // The covariant override returned the SAME object (one slot), viewable as IMethod or IMember.
    EXPECT_EQ(static_cast<const void*>(specializedThroughIMember),
              static_cast<const void*>(specializedThroughIMethod));
}

// ---------------------------------------------------------------------------
// IMethod -- `ReducedFrom` returns the configured original method for a reduced method (an
// extension method or a local function with the leading parameter / trailing generated
// parameters stripped), null for a non-reduced method.
// ---------------------------------------------------------------------------
TEST(IMethodTest, ReducedFromReturnsConfiguredOriginal)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestMethod original("Where", "C.Where", "C.Where", "", SymbolKind::Method,
        compilation, 0x06000007u, returnType, Accessibility::Public,
        /*isStatic*/ true, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false, /*isVirtual*/ false, /*isOverride*/ false,
        /*isOverridable*/ false,
        /*returnTypeIsRefReadOnly*/ false, /*isInitOnly*/ false, /*thisIsRefReadOnly*/ false,
        /*isExtensionMethod*/ true, /*isLocalFunction*/ false, /*isConstructor*/ false,
        /*isDestructor*/ false, /*isOperator*/ false, /*hasBody*/ true, /*isAccessor*/ false,
        MethodSemanticsAttributes::None);
    TestMethod reduced("Where", "C.Where", "C.Where", "", SymbolKind::Method,
        compilation, 0x06000008u, returnType, Accessibility::Public,
        /*isStatic*/ true, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false, /*isVirtual*/ false, /*isOverride*/ false,
        /*isOverridable*/ false,
        /*returnTypeIsRefReadOnly*/ false, /*isInitOnly*/ false, /*thisIsRefReadOnly*/ false,
        /*isExtensionMethod*/ true, /*isLocalFunction*/ false, /*isConstructor*/ false,
        /*isDestructor*/ false, /*isOperator*/ false, /*hasBody*/ true, /*isAccessor*/ false,
        MethodSemanticsAttributes::None);
    reduced.SetReducedFrom(&original);

    EXPECT_EQ(reduced.IsExtensionMethod(), true);
    EXPECT_EQ(reduced.ReducedFrom(), &original);
    EXPECT_EQ(original.ReducedFrom(), nullptr);
}

// ---------------------------------------------------------------------------
// IMethod -- polymorphic dispatch through the `IParameterizedMember*` / `IMember*` /
// `IEntity*` / `ISymbol*` base pointers (an `IMethod` IS-A each of its bases, so each base
// pointer dispatches to the concrete override, including the single `Name()` that is the
// final overrider for all three name-declaring bases). The IMethod-own accessors are
// reachable through `IMethod*` / `IParameterizedMember*` but NOT through `IMember*`.
// ---------------------------------------------------------------------------
TEST(IMethodTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(7);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto owned = std::make_unique<TestMethod>(
        "Run", "C.Run", "C.Run", "", SymbolKind::Method, compilation, 0x06000009u, returnType,
        Accessibility::Public, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, true, false,
        MethodSemanticsAttributes::None);
    IParameterizedMember* asPM = owned.get();
    IMember* asMember = owned.get();
    IEntity* asEntity = owned.get();
    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asPM->Name(), "Run");
    EXPECT_EQ(asPM->SymbolKind(), SymbolKind::Method);
    EXPECT_TRUE(asPM->Parameters().empty());
    EXPECT_EQ(asMember->Name(), "Run");
    EXPECT_EQ(asMember->MemberDefinition(), owned.get());
    EXPECT_EQ(asEntity->Name(), "Run");
    EXPECT_EQ(asEntity->MetadataToken(), 0x06000009u);
    EXPECT_EQ(asSymbol->Name(), "Run");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Method);
    // destroying `owned` runs the `TestMethod` destructor through the virtual `~IMethod()`
    // (which chains to `~IParameterizedMember()` / `~IMember()` / `~IEntity()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IMethod -- has a virtual destructor (a concrete subclass can be deleted through an
// `IMethod*` / `IParameterizedMember*` / `IMember*` / `IEntity*` / `ISymbol*` and the derived
// destructor runs), the established abstract-base contract; it is abstract (every accessor is
// pure-virtual) and polymorphic. The single-inheritance `IMethod` -> `IParameterizedMember`
// -> `IMember` -> `IEntity` -> ... chain composes correctly.
// ---------------------------------------------------------------------------
TEST(IMethodTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IMethod>,
        "IMethod must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IParameterizedMember>,
        "IParameterizedMember (the base of IMethod) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IMethod>,
        "IMethod must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IMethod>,
        "IMethod must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(8);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    std::unique_ptr<IMethod> owned = std::make_unique<TestMethod>(
        "M", "C.M", "C.M", "", SymbolKind::Method, compilation, 0x0600000Au, returnType,
        Accessibility::Public, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false, true, false,
        MethodSemanticsAttributes::None);
    EXPECT_EQ(owned->Name(), "M");
    EXPECT_TRUE(owned->Parameters().empty());
    EXPECT_TRUE(owned->TypeParameters().empty());
    EXPECT_TRUE(owned->TypeArguments().empty());
    // destroying `owned` runs the `TestMethod` destructor through the virtual `~IMethod()`.
    owned.reset();
    SUCCEED();
}
