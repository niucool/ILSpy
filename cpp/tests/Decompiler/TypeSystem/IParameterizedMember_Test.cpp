// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, the rights to use, copy,
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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for `IParameterizedMember` (cpp/Decompiler/TypeSystem/IParameterizedMember.hpp, the
// port of ICSharpCode.Decompiler/TypeSystem/IParameterizedMember.cs). `IParameterizedMember
// : IMember` is the interface a member that carries a parameter list (a method or a
// property) implements; it adds the single read-only `Parameters` accessor. `IMethod` and
// `IProperty` derive from it; `IEvent` / `IField` stay plain `IMember`s.
//
// This is the stand-in-reconciliation iteration: the real `IParameterizedMember.hpp` lands
// here, so `IParameter_Test.cpp` (which defined a minimal `IParameterizedMember` stand-in so
// its `TestParameter::Owner()` could return a parameterized member) must now include the
// real header, drop its stand-in, and make its `TestParameterizedMember` stub derive from
// the real `IParameterizedMember` -- the documented "stand-ins are dropped when the real
// headers land" step (the D386 `IAttribute`-stand-in-reconciliation precedent). That
// reconciliation is performed in this iteration alongside the new dedicated test.
//
// The inherited `ICompilationProvider::Compilation()` reference return needs a complete
// `ICompilation` to bind to, so this test file provides the IDENTICAL minimal `ICompilation`
// stand-in used by `ICompilationProvider_Test.cpp` / `IEntity_Test.cpp` /
// `ITypeParameter_Test.cpp` / `IMember_Test.cpp` (virtual destructor only); the identical
// class definitions across translation units satisfy the One Definition Rule. `IAttribute`
// is the real port (D386), included below. `IParameter` is the real port (D382), included
// below so the `Parameters` snapshot can hold concrete `TestParameter` instances.

#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

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
// member are kept so the `HasAttribute` / `GetAttribute` tests (which classify attributes by
// `KnownAttribute`) continue to work. IDENTICAL in shape to the `TestAttribute` in
// `IEntity_Test.cpp` / `IParameter_Test.cpp` / `IMember_Test.cpp`.
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

// A minimal concrete `IParameter` for testing: holds the configured scalar state and returns
// it from every accessor (the shape a real `MetadataParameter` / `DefaultParameter` takes).
// Only `Name` and `SymbolKind` are exercised by the `IParameterizedMember` tests (the
// `Parameters` snapshot is checked by pointer identity and by reading a parameter's name
// through the snapshot); the remaining accessors return simple defaults so the concrete stub
// compiles. IDENTICAL in shape to the `TestParameter` in `IParameter_Test.cpp`.
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

// A minimal concrete `IParameterizedMember` for testing: holds the configured
// scalar/pointer state and returns it from every accessor (the shape a real `MetadataMethod`
// / `MetadataProperty` takes). `Name()` is overridden ONCE and satisfies the
// `ISymbol::Name()` / `INamedElement::Name()` / `IEntity::Name()` contracts (the diamond is
// disambiguated by `IEntity`'s redeclaration, and a single override is the final overrider
// for all three). The three long-pole-dep members (`Substitution` / `Specialize` / `Equals`)
// use the `nullptr` / `this` / identity stand-ins (the documented
// "forward-declared-IMember, deferring the long-pole deps" shell strategy, the `IMember_Test`
// precedent). The `IParameterizedMember`-own `Parameters` accessor returns the configured
// non-owning snapshot.
class TestParameterizedMember : public ILSpy::Decompiler::TypeSystem::IParameterizedMember {
public:
    TestParameterizedMember(std::string name,
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
                            bool isVirtual, bool isOverride, bool isOverridable)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          reflectionName_(std::move(reflectionName)), namespace_(std::move(ns)),
          kind_(kind), compilation_(compilation), metadataToken_(metadataToken),
          returnType_(std::move(returnType)), accessibility_(accessibility),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed),
          isExplicitInterfaceImplementation_(isExplicitInterfaceImplementation),
          isVirtual_(isVirtual), isOverride_(isOverride), isOverridable_(isOverridable) {}

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
    // The long-pole-dep stand-ins: `Substitution` returns `nullptr` (`TypeParameterSubstitution`
    // is forward-declared), `Specialize` returns `this` (a degenerate stand-in for the real
    // newly-specialized member), and `Equals` is identity comparison (a degenerate stand-in for
    // the real type-normalized equality).
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

    // Test wiring (set the nullable / collection slots after construction).
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void AddExplicitlyImplementedInterfaceMember(const ILSpy::Decompiler::TypeSystem::IMember* m)
    {
        explicitlyImplementedInterfaceMembers_.push_back(m);
    }
    void AddParameter(const ILSpy::Decompiler::TypeSystem::IParameter* p)
    {
        parameters_.push_back(p);
    }

private:
    std::string name_, fullName_, reflectionName_, namespace_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    bool isStatic_, isAbstract_, isSealed_;
    bool isExplicitInterfaceImplementation_, isVirtual_, isOverride_, isOverridable_;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> explicitlyImplementedInterfaceMembers_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters_;
};

} // namespace

// ---------------------------------------------------------------------------
// IParameterizedMember -- `Parameters` returns the configured non-owning snapshot (the
// member owns its parameters; the caller holds raw pointers), matching the AST non-owning
// model and the `IEntity::GetAttributes` / `IMember::ExplicitlyImplementedInterfaceMembers`
// precedent. A parameterized member with two parameters returns them in insertion order,
// and the snapshot's pointers are identity-comparable to the originals.
// ---------------------------------------------------------------------------
TEST(IParameterizedMemberTest, ParametersReturnsConfiguredSnapshot)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestParameterizedMember method("Add", "C.Add", "C.Add", "", SymbolKind::Method,
        compilation, 0x06000001u, returnType, Accessibility::Public,
        /*isStatic*/ false, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false,
        /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false);
    TestParameter x(SymbolKind::Parameter, "x", intType);
    TestParameter y(SymbolKind::Parameter, "y", stringType);
    method.AddParameter(&x);
    method.AddParameter(&y);

    const auto params = method.Parameters();
    ASSERT_EQ(params.size(), 2u);
    // Non-owning pointer identity: the snapshot holds the originals.
    EXPECT_EQ(params[0], &x);
    EXPECT_EQ(params[1], &y);
    // The snapshot is read-only access: reading a parameter's name through the snapshot
    // reaches the concrete `IParameter` override.
    EXPECT_EQ(params[0]->Name(), "x");
    EXPECT_EQ(params[1]->Name(), "y");
    EXPECT_EQ(params[0]->SymbolKind(), SymbolKind::Parameter);
}

// ---------------------------------------------------------------------------
// IParameterizedMember -- `Parameters` is empty by default for a parameterless member (the
// C# `IReadOnlyList<IParameter>` of a parameterless method/property is empty, never null).
// The C++ port returns an empty `std::vector` (a by-value snapshot), so the caller never
// sees a null collection.
// ---------------------------------------------------------------------------
TEST(IParameterizedMemberTest, ParametersIsEmptyByDefault)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestParameterizedMember method("Dispose", "C.Dispose", "C.Dispose", "", SymbolKind::Method,
        compilation, 0x06000002u, returnType, Accessibility::Public,
        false, false, false, false, false, false, false);

    const auto params = method.Parameters();
    EXPECT_TRUE(params.empty());
    EXPECT_EQ(params.size(), 0u);
}

// ---------------------------------------------------------------------------
// IParameterizedMember -- the inherited `IMember` scalar/pointer accessors dispatch through
// an `IParameterizedMember*` (the shape a real method/property exposes: its name, kind,
// return type, and the `Parameters` list), confirming the single-inheritance chain
// (`IParameterizedMember` -> `IMember` -> `IEntity` -> ...) composes correctly and the new
// `Parameters` accessor is reachable alongside the inherited ones.
// ---------------------------------------------------------------------------
TEST(IParameterizedMemberTest, InheritedMemberAccessorsDispatchThroughIParameterizedMemberPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestParameterizedMember method("Count", "C.Count", "C.Count", "", SymbolKind::Property,
        compilation, 0x06000003u, returnType, Accessibility::Public,
        /*isStatic*/ false, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false,
        /*isVirtual*/ true, /*isOverride*/ false, /*isOverridable*/ true);
    TestParameter value(SymbolKind::Parameter, "value", intType);
    method.AddParameter(&value);

    IParameterizedMember* base = &method;
    // Inherited IMember accessors.
    EXPECT_EQ(base->Name(), "Count");
    EXPECT_EQ(base->SymbolKind(), SymbolKind::Property);
    EXPECT_EQ(&base->ReturnType(), returnType.get());
    EXPECT_EQ(base->ReturnType().Name(), "Int32");
    EXPECT_EQ(base->MemberDefinition(), &method);
    EXPECT_TRUE(base->IsVirtual());
    EXPECT_TRUE(base->IsOverridable());
    EXPECT_EQ(base->Substitution(), nullptr);
    EXPECT_EQ(base->Specialize(nullptr), &method);
    EXPECT_TRUE(base->Equals(&method, nullptr));
    // The IParameterizedMember-own accessor.
    const auto params = base->Parameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0], &value);
    EXPECT_EQ(params[0]->Name(), "value");
}

// ---------------------------------------------------------------------------
// IParameterizedMember -- polymorphic dispatch through the `IMember*` / `IEntity*` /
// `ISymbol*` base pointers (an `IParameterizedMember` IS-A each of its bases, so each base
// pointer dispatches to the concrete override, including the single `Name()` that is the
// final overrider for all three name-declaring bases). The `Parameters` accessor is NOT
// reachable through an `IMember*` (it is `IParameterizedMember`-own), which is the
// structural point: a plain `IMember` (a field/event) has no `Parameters`.
// ---------------------------------------------------------------------------
TEST(IParameterizedMemberTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto owned = std::make_unique<TestParameterizedMember>(
        "Run", "C.Run", "C.Run", "", SymbolKind::Method, compilation, 0x06000004u, returnType,
        Accessibility::Public, false, false, false, false, false, false, false);
    IMember* asMember = owned.get();
    IEntity* asEntity = owned.get();
    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asMember->Name(), "Run");
    EXPECT_EQ(asMember->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(asMember->MemberDefinition(), owned.get());
    EXPECT_EQ(asEntity->Name(), "Run");
    EXPECT_EQ(asEntity->MetadataToken(), 0x06000004u);
    EXPECT_EQ(asSymbol->Name(), "Run");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Method);
    // destroying `owned` runs the `TestParameterizedMember` destructor through the virtual
    // `~IParameterizedMember()` (which chains to `~IMember()` / `~IEntity()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IParameterizedMember -- has a virtual destructor (a concrete subclass can be deleted
// through an `IParameterizedMember*` / `IMember*` / `IEntity*` / `ISymbol*` and the derived
// destructor runs), the established abstract-base contract; it is abstract (every accessor
// is pure-virtual) and polymorphic. The single-inheritance `IParameterizedMember` ->
// `IMember` -> `IEntity` -> ... chain composes correctly.
// ---------------------------------------------------------------------------
TEST(IParameterizedMemberTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IParameterizedMember>,
        "IParameterizedMember must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IMember>,
        "IMember (the base of IParameterizedMember) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IParameterizedMember>,
        "IParameterizedMember must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IParameterizedMember>,
        "IParameterizedMember must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    std::unique_ptr<IParameterizedMember> owned = std::make_unique<TestParameterizedMember>(
        "M", "C.M", "C.M", "", SymbolKind::Method, compilation, 0x06000005u, returnType,
        Accessibility::Public, false, false, false, false, false, false, false);
    EXPECT_EQ(owned->Name(), "M");
    EXPECT_TRUE(owned->Parameters().empty());
    // destroying `owned` runs the `TestParameterizedMember` destructor through the virtual
    // `~IParameterizedMember()`.
    owned.reset();
    SUCCEED();
}
