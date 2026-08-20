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

// Tests for `IEvent` (cpp/Decompiler/TypeSystem/IEvent.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IEvent.cs). `IEvent : IMember` represents an event; it
// adds the `CanAdd` / `CanRemove` / `CanInvoke` availability flags and the nullable
// `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` accessor back-references. It is
// structurally the `IProperty` (D390) twin, but derives from `IMember` directly (events have
// no parameter list, so they are NOT `IParameterizedMember`s).
//
// The test stub `TestEvent` derives from the real `IEvent` and overrides every
// `IMember` / `IEntity` / `ICompilationProvider` / `INamedElement` / `ISymbol` pure-virtual plus
// the `IEvent`-own accessors, the shape a real `MetadataEvent` / `SpecializedEvent` takes.
//
// The inherited `ICompilationProvider::Compilation()` reference return needs a complete
// `ICompilation` to bind to, so this test file provides the IDENTICAL minimal `ICompilation`
// stand-in used by the other TypeSystem test files (virtual destructor only); the identical
// class definitions across translation units satisfy the One Definition Rule. `IAttribute` is
// the real port (D386), included below. `IMethod` is the real port (D389), included below so the
// `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` accessors can hold concrete `TestMethod`
// instances.

#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
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
// `TestAttribute` in the other member-family test files.
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

// A minimal concrete `IMethod` for testing: the `AddAccessor` / `RemoveAccessor` /
// `InvokeAccessor` accessors of `TestEvent` return `const IMethod*` pointers to `TestMethod`
// instances, and the accessor-back-reference test sets a `TestMethod`'s `AccessorOwner` back to
// the event. The stub holds the configured scalar/pointer state and returns it from every
// accessor (the shape a real `MetadataMethod` / `SpecializedMethod` takes). `Name()` is
// overridden ONCE and satisfies the `ISymbol::Name()` / `INamedElement::Name()` / `IEntity::Name()`
// contracts (the diamond is disambiguated by `IEntity`'s redeclaration, and a single override is
// the final overrider for all three). The three long-pole-dep members (`Substitution` /
// `Specialize` / `Equals`) use the `nullptr` / `this` / identity stand-ins, but `Specialize`
// uses the COVARIANT return (`const IMethod*`), the documented mirror of the C# `new IMethod
// Specialize(...)`. IDENTICAL in shape to the `TestMethod` in `IMethod_Test.cpp` /
// `IProperty_Test.cpp`.
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

// A minimal concrete `IEvent` for testing: holds the configured scalar/pointer state and
// returns it from every accessor (the shape a real `MetadataEvent` / `SpecializedEvent`
// takes). `Name()` is overridden ONCE and satisfies the `ISymbol::Name()` / `INamedElement::Name()`
// / `IEntity::Name()` contracts (the diamond is disambiguated by `IEntity`'s redeclaration, and
// a single override is the final overrider for all three). The three long-pole-dep members
// (`Substitution` / `Specialize` / `Equals`) use the `nullptr` / `this` / identity stand-ins.
// The `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` slots are nullable `const IMethod*`
// (set after construction). `IEvent : IMember` (NOT `IParameterizedMember`), so there is no
// `Parameters` accessor to override.
class TestEvent : public ILSpy::Decompiler::TypeSystem::IEvent {
public:
    TestEvent(std::string name,
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
              bool canAdd, bool canRemove, bool canInvoke)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          reflectionName_(std::move(reflectionName)), namespace_(std::move(ns)),
          kind_(kind), compilation_(compilation), metadataToken_(metadataToken),
          returnType_(std::move(returnType)), accessibility_(accessibility),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed),
          isExplicitInterfaceImplementation_(isExplicitInterfaceImplementation),
          isVirtual_(isVirtual), isOverride_(isOverride), isOverridable_(isOverridable),
          canAdd_(canAdd), canRemove_(canRemove), canInvoke_(canInvoke) {}

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

    // --- IEvent ---
    bool CanAdd() const override { return canAdd_; }
    bool CanRemove() const override { return canRemove_; }
    bool CanInvoke() const override { return canInvoke_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* AddAccessor() const override { return addAccessor_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* RemoveAccessor() const override { return removeAccessor_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* InvokeAccessor() const override { return invokeAccessor_; }

    // Test wiring (set the nullable / collection slots after construction).
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void SetAddAccessor(const ILSpy::Decompiler::TypeSystem::IMethod* m) { addAccessor_ = m; }
    void SetRemoveAccessor(const ILSpy::Decompiler::TypeSystem::IMethod* m) { removeAccessor_ = m; }
    void SetInvokeAccessor(const ILSpy::Decompiler::TypeSystem::IMethod* m) { invokeAccessor_ = m; }

private:
    std::string name_, fullName_, reflectionName_, namespace_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    bool isStatic_, isAbstract_, isSealed_;
    bool isExplicitInterfaceImplementation_, isVirtual_, isOverride_, isOverridable_;
    bool canAdd_, canRemove_, canInvoke_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    const ILSpy::Decompiler::TypeSystem::IMethod* addAccessor_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IMethod* removeAccessor_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IMethod* invokeAccessor_ = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// IEvent -- the IEvent-own scalar/flag accessors return the configured values: the
// `CanAdd` / `CanRemove` / `CanInvoke` availability flags. A field-like event (with all three
// accessors) has all three flags true; an event with only add/remove has `CanInvoke` false.
// ---------------------------------------------------------------------------
TEST(IEventTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TestEvent full("Click", "C.Click", "C.Click", "", SymbolKind::Event,
        compilation, 0x14000001u, returnType, Accessibility::Public,
        /*isStatic*/ false, /*isAbstract*/ false, /*isSealed*/ false,
        /*isExplicitInterfaceImplementation*/ false,
        /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false,
        /*canAdd*/ true, /*canRemove*/ true, /*canInvoke*/ true);

    IEvent* e = &full;
    EXPECT_TRUE(e->CanAdd());
    EXPECT_TRUE(e->CanRemove());
    EXPECT_TRUE(e->CanInvoke());
}

// ---------------------------------------------------------------------------
// IEvent -- `AddAccessor` / `RemoveAccessor` / `InvokeAccessor` return the configured
// non-owning `const IMethod*` pointers (the event's accessor methods), or null when the
// accessor is absent. A field-like event has all three; an event with only add/remove has a
// null `InvokeAccessor`.
// ---------------------------------------------------------------------------
TEST(IEventTest, AccessorsReturnConfiguredIMethodPointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto handlerType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TestEvent click("Click", "C.Click", "C.Click", "", SymbolKind::Event,
        compilation, 0x14000002u, handlerType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canAdd*/ true, /*canRemove*/ true, /*canInvoke*/ false);
    TestMethod adder("add_Click", "C.add_Click", "C.add_Click", "", SymbolKind::Method,
        compilation, 0x06000002u, voidType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Adder);
    TestMethod remover("remove_Click", "C.remove_Click", "C.remove_Click", "", SymbolKind::Method,
        compilation, 0x06000003u, voidType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Remover);
    click.SetAddAccessor(&adder);
    click.SetRemoveAccessor(&remover);
    // No invoke accessor for this event.

    EXPECT_EQ(click.AddAccessor(), &adder);
    EXPECT_EQ(click.RemoveAccessor(), &remover);
    EXPECT_EQ(click.InvokeAccessor(), nullptr);
    EXPECT_EQ(click.AddAccessor()->Name(), "add_Click");
    EXPECT_EQ(click.AddAccessor()->AccessorKind(), MethodSemanticsAttributes::Adder);
    EXPECT_EQ(click.RemoveAccessor()->AccessorKind(), MethodSemanticsAttributes::Remover);
    EXPECT_TRUE(click.CanAdd());
    EXPECT_TRUE(click.CanRemove());
    EXPECT_FALSE(click.CanInvoke());
}

// ---------------------------------------------------------------------------
// IEvent -- the accessor-back-reference round-trip: an event's `AddAccessor` is an `IMethod`
// whose `AccessorOwner` points back to the event (the `[MemberNotNullWhen]` invariant made
// concrete), and whose `IsAccessor` is true and `AccessorKind` is `Adder`. The event and its
// accessor form a two-way handle pair.
// ---------------------------------------------------------------------------
TEST(IEventTest, AccessorBackReferenceRoundTrip)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto handlerType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TestEvent click("Click", "C.Click", "C.Click", "", SymbolKind::Event,
        compilation, 0x14000004u, handlerType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canAdd*/ true, /*canRemove*/ true, /*canInvoke*/ true);
    TestMethod adder("add_Click", "C.add_Click", "C.add_Click", "", SymbolKind::Method,
        compilation, 0x06000004u, voidType, Accessibility::Public,
        false, false, false, false, false, false, true,
        false, false, false, false, false, false, false, false, true, true,
        MethodSemanticsAttributes::Adder);
    adder.SetAccessorOwner(&click);
    click.SetAddAccessor(&adder);

    // The event's add accessor points to the method, the method's accessor-owner points back.
    EXPECT_EQ(click.AddAccessor(), &adder);
    ASSERT_NE(click.AddAccessor(), nullptr);
    EXPECT_EQ(click.AddAccessor()->AccessorOwner(), &click);
    EXPECT_TRUE(click.AddAccessor()->IsAccessor());
    EXPECT_EQ(click.AddAccessor()->AccessorKind(), MethodSemanticsAttributes::Adder);
}

// ---------------------------------------------------------------------------
// IEvent -- the inherited entity accessors dispatch through the `IEvent*`: `GetAttributes` /
// `HasAttribute` / `GetAttribute` (the `IEntity` attribute family). The `IEvent`-own accessors
// are reachable through `IEvent*` but NOT through `IMember*` (a plain `IMember` has no
// `CanAdd` / `AddAccessor`).
// ---------------------------------------------------------------------------
TEST(IEventTest, InheritedEntityAccessorsDispatchThroughIEventPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    auto handlerType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TestEvent click("Click", "C.Click", "C.Click", "", SymbolKind::Event,
        compilation, 0x14000005u, handlerType, Accessibility::Public,
        false, false, false, false, false, false, false,
        /*canAdd*/ true, /*canRemove*/ true, /*canInvoke*/ true);
    TestAttribute attr(KnownAttribute::Obsolete);
    click.AddAttribute(&attr);

    EXPECT_EQ(click.SymbolKind(), SymbolKind::Event);
    EXPECT_TRUE(click.HasAttribute(KnownAttribute::Obsolete));
    EXPECT_FALSE(click.HasAttribute(KnownAttribute::Serializable));
    EXPECT_EQ(click.GetAttribute(KnownAttribute::Obsolete), &attr);
    EXPECT_EQ(click.GetAttribute(KnownAttribute::Serializable), nullptr);
    const auto attrs = click.GetAttributes();
    ASSERT_EQ(attrs.size(), 1u);
    EXPECT_EQ(attrs[0], &attr);
}

// ---------------------------------------------------------------------------
// IEvent -- polymorphic dispatch through the `IMember*` / `IEntity*` / `ISymbol*` base pointers
// (an `IEvent` IS-A each of its bases, so each base pointer dispatches to the concrete
// override, including the single `Name()` that is the final overrider for all three
// name-declaring bases). The IEvent-own accessors are reachable through `IEvent*` but NOT
// through `IMember*` (a plain `IMember` field/event has no `CanAdd` / `AddAccessor`).
// ---------------------------------------------------------------------------
TEST(IEventTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto handlerType = std::make_shared<KnownType>(KnownTypeCode::Object);
    auto owned = std::make_unique<TestEvent>(
        "Click", "C.Click", "C.Click", "", SymbolKind::Event, compilation, 0x14000006u, handlerType,
        Accessibility::Public, false, false, false, false, false, false, false,
        /*canAdd*/ true, /*canRemove*/ true, /*canInvoke*/ true);
    IMember* asMember = owned.get();
    IEntity* asEntity = owned.get();
    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asMember->Name(), "Click");
    EXPECT_EQ(asMember->MemberDefinition(), owned.get());
    EXPECT_EQ(asEntity->Name(), "Click");
    EXPECT_EQ(asEntity->MetadataToken(), 0x14000006u);
    EXPECT_EQ(asSymbol->Name(), "Click");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Event);
    // destroying `owned` runs the `TestEvent` destructor through the virtual `~IEvent()` (which
    // chains to `~IMember()` / `~IEntity()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IEvent -- has a virtual destructor (a concrete subclass can be deleted through an `IEvent*`
// / `IMember*` / `IEntity*` / `ISymbol*` and the derived destructor runs), the established
// abstract-base contract; it is abstract (every accessor is pure-virtual) and polymorphic. The
// single-inheritance `IEvent` -> `IMember` -> `IEntity` -> ... chain composes correctly.
// ---------------------------------------------------------------------------
TEST(IEventTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IEvent>,
        "IEvent must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IMember>,
        "IMember (the base of IEvent) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IEvent>,
        "IEvent must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IEvent>,
        "IEvent must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    auto handlerType = std::make_shared<KnownType>(KnownTypeCode::Object);
    std::unique_ptr<IEvent> owned = std::make_unique<TestEvent>(
        "E", "C.E", "C.E", "", SymbolKind::Event, compilation, 0x14000007u, handlerType,
        Accessibility::Public, false, false, false, false, false, false, false,
        /*canAdd*/ true, /*canRemove*/ true, /*canInvoke*/ false);
    EXPECT_EQ(owned->Name(), "E");
    EXPECT_TRUE(owned->CanAdd());
    EXPECT_TRUE(owned->CanRemove());
    EXPECT_FALSE(owned->CanInvoke());
    // destroying `owned` runs the `TestEvent` destructor through the virtual `~IEvent()`.
    owned.reset();
    SUCCEED();
}
