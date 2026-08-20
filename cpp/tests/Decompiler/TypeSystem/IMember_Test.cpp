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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `IMember` (cpp/Decompiler/TypeSystem/IMember.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IMember.cs). `IMember : IEntity` is the root of the
// resolved member hierarchy: every resolved method / field / property / event is an
// `IMember`. It adds the original `MemberDefinition`, the non-null `ReturnType`, the
// `ExplicitlyImplementedInterfaceMembers` snapshot, the explicit-implementation / virtual /
// override / overridable flags, and the three long-pole-dep members `Substitution` /
// `Specialize` / `Equals` (which reference the not-yet-ported `TypeParameterSubstitution` /
// `TypeVisitor`).
//
// The long-pole deps `TypeParameterSubstitution` and `TypeVisitor` are only forward-declared
// in `IMember.hpp` (they are not yet ported -- `TypeVisitor` needs the concrete `IType`
// `VisitChildren` plus the missing `TupleType` / `ModifiedType` / `NullabilityAnnotatedType`
// / `FunctionPointerType` concrete types). They are C# `class`es (reference types), so the
// three `IMember` members that touch them use POINTER types and a test stub can implement
// them with `nullptr` stand-ins (the documented "forward-declared-IMember, deferring the
// long-pole deps" shell strategy): `Substitution()` returns `nullptr`, `Specialize(nullptr)`
// returns `this`, and `Equals(nullptr, nullptr)` returns the identity comparison. This is a
// TEST STAND-IN, NOT a faithful specialization -- the real implementations (when the long-pole
// lands) return `&TypeParameterSubstitution::Identity`, a newly-specialized member, and a
// type-normalized equality.
//
// The inherited `ICompilationProvider::Compilation()` reference return needs a complete
// `ICompilation` to bind to, so this test file provides the IDENTICAL minimal `ICompilation`
// stand-in used by `ICompilationProvider_Test.cpp` / `IEntity_Test.cpp` /
// `ITypeParameter_Test.cpp` (virtual destructor only); the four identical class definitions
// across translation units satisfy the One Definition Rule. `IAttribute` is the real port
// (D386), included below. `ITypeDefinition` / `IModule` stay forward-declared (the stub
// returns `nullptr` for `DeclaringTypeDefinition` / `ParentModule`, which needs no complete
// type).

#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
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
// `IEntity_Test.cpp` / `IParameter_Test.cpp`.
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

// A minimal concrete `IMember` for testing: holds the configured scalar/pointer state and
// returns it from every accessor (the shape a real `MetadataMethod` / `MetadataField` /
// `MetadataProperty` / `MetadataEvent` takes). `Name()` is overridden ONCE and satisfies the
// `ISymbol::Name()` / `INamedElement::Name()` / `IEntity::Name()` contracts (the diamond is
// disambiguated by `IEntity`'s redeclaration, and a single override is the final overrider for
// all three). `DeclaringType` is inherited from `IEntity` (the C# `new IType` is a nullability
// re-statement with no C++ counterpart, the D374 single-inheritance precedent). The three
// long-pole members use `nullptr` / `this` / identity stand-ins (see the file header).
class TestMember : public ILSpy::Decompiler::TypeSystem::IMember {
public:
    TestMember(std::string name,
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

    // Test wiring (set the nullable / collection slots after construction).
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void AddExplicitlyImplementedInterfaceMember(const ILSpy::Decompiler::TypeSystem::IMember* m)
    {
        explicitlyImplementedInterfaceMembers_.push_back(m);
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
};

} // namespace

// ---------------------------------------------------------------------------
// IMember -- the IMember-own scalar/pointer accessors return the configured values (the
// shape a real member exposes: its original definition, return type, explicit-interface
// list, and the explicit/virtual/override/overridable flags).
// ---------------------------------------------------------------------------
TEST(IMemberTest, MemberAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestMember member("Count", "System.Counter.Count", "System.Counter.Count", "System.Counter",
                      SymbolKind::Property, compilation, 0x06000001u, returnType,
                      Accessibility::Public, /*isStatic*/ false, /*isAbstract*/ false,
                      /*isSealed*/ false, /*isExplicitInterfaceImplementation*/ true,
                      /*isVirtual*/ true, /*isOverride*/ false, /*isOverridable*/ true);

    // MemberDefinition returns `this` for a non-specialized member.
    EXPECT_EQ(member.MemberDefinition(), &member);
    // ReturnType is the non-null configured type (a const reference, pointer-identity).
    EXPECT_EQ(&member.ReturnType(), returnType.get());
    EXPECT_EQ(member.ReturnType().Name(), "Int32");
    // The flags.
    EXPECT_TRUE(member.IsExplicitInterfaceImplementation());
    EXPECT_TRUE(member.IsVirtual());
    EXPECT_FALSE(member.IsOverride());
    EXPECT_TRUE(member.IsOverridable());
}

// ---------------------------------------------------------------------------
// IMember -- `ExplicitlyImplementedInterfaceMembers` returns the configured non-owning
// snapshot (the member owns its implemented-interface references; the caller holds raw
// pointers), matching the `IEntity::GetAttributes` precedent.
// ---------------------------------------------------------------------------
TEST(IMemberTest, ExplicitlyImplementedInterfaceMembersReturnsConfiguredSnapshot)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestMember member("Dispose", "System.IDisposable.Dispose", "System.IDisposable.Dispose",
                      "System", SymbolKind::Method, compilation, 0x06000002u, voidType,
                      Accessibility::Private, false, false, false,
                      /*isExplicitInterfaceImplementation*/ true,
                      /*isVirtual*/ true, /*isOverride*/ false, /*isOverridable*/ true);
    // A member can implement itself as a degenerate test (the pointer identity is what
    // matters, not the target).
    member.AddExplicitlyImplementedInterfaceMember(&member);

    const auto impls = member.ExplicitlyImplementedInterfaceMembers();
    EXPECT_EQ(impls.size(), 1u);
    EXPECT_EQ(impls[0], &member);
}

// ---------------------------------------------------------------------------
// IMember -- the three long-pole-dep members use the forward-declared-dep stand-ins:
// `Substitution()` returns `nullptr` (the `TypeParameterSubstitution` dep is not yet
// ported), `Specialize(nullptr)` returns `this` (a degenerate stand-in for the real
// newly-specialized member), and `Equals` is identity comparison. These stand-ins are
// TEST FIXTURES, replaced by the real semantics when `TypeParameterSubstitution` /
// `TypeVisitor` land.
// ---------------------------------------------------------------------------
TEST(IMemberTest, LongPoleDepMembersUseForwardDeclaredStandIns)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestMember member("M", "C.M", "C.M", "", SymbolKind::Method, compilation, 0x06000003u,
                      voidType, Accessibility::Public, false, false, false, false,
                      /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false);

    // Substitution: the forward-declared-dep nullptr stand-in.
    EXPECT_EQ(member.Substitution(), nullptr);
    // Specialize: the degenerate `this` stand-in (a real impl returns a newly-specialized
    // member; the pointer parameter is nullptr for the test).
    EXPECT_EQ(member.Specialize(nullptr), &member);
    // Equals: identity comparison stand-in (a real impl does a type-normalized equality).
    EXPECT_TRUE(member.Equals(&member, nullptr));
    EXPECT_FALSE(member.Equals(nullptr, nullptr));
}

// ---------------------------------------------------------------------------
// IMember -- the inherited `IEntity` scalar/pointer accessors dispatch through `IMember*`
// (the shape a real member exposes: its kind, names, metadata token, accessibility,
// static/abstract/sealed flags, and the nullable `DeclaringType`).
// ---------------------------------------------------------------------------
TEST(IMemberTest, InheritedEntityAccessorsDispatchThroughIMemberPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto declaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TestMember member("Name", "C.Name", "C.Name", "", SymbolKind::Property, compilation,
                      0x06000004u, returnType, Accessibility::Public,
                      /*isStatic*/ true, /*isAbstract*/ false, /*isSealed*/ false, false,
                      /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false);
    member.SetDeclaringType(declaringType);

    EXPECT_EQ(member.SymbolKind(), SymbolKind::Property);
    EXPECT_EQ(member.Name(), "Name");
    EXPECT_EQ(member.FullName(), "C.Name");
    EXPECT_EQ(member.ReflectionName(), "C.Name");
    EXPECT_EQ(member.Namespace(), "");
    EXPECT_EQ(member.MetadataToken(), 0x06000004u);
    EXPECT_EQ(member.Accessibility(), Accessibility::Public);
    EXPECT_TRUE(member.IsStatic());
    EXPECT_FALSE(member.IsAbstract());
    EXPECT_FALSE(member.IsSealed());
    ASSERT_NE(member.DeclaringType(), nullptr);
    EXPECT_EQ(member.DeclaringType()->Kind(), TypeKind::Class);
    // The nullable slots that are unset for this stub.
    EXPECT_EQ(member.DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(member.ParentModule(), nullptr);
}

// ---------------------------------------------------------------------------
// IMember -- the inherited attribute triple (`GetAttributes` / `HasAttribute` /
// `GetAttribute`) classifies by `KnownAttribute` through `IMember*`, matching the
// `IEntity` contract.
// ---------------------------------------------------------------------------
TEST(IMemberTest, InheritedAttributeTripleClassifiesByKnownKind)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
    TestAttribute compilerGenerated(KnownAttribute::CompilerGenerated);
    TestMember member("Op", "C.Op", "C.Op", "", SymbolKind::Method, compilation, 0x06000005u,
                      voidType, Accessibility::Public, true, false, false, false, false, false, false);
    member.AddAttribute(&compilerGenerated);

    const auto attrs = member.GetAttributes();
    ASSERT_EQ(attrs.size(), 1u);
    EXPECT_EQ(attrs[0], &compilerGenerated);
    EXPECT_TRUE(member.HasAttribute(KnownAttribute::CompilerGenerated));
    EXPECT_FALSE(member.HasAttribute(KnownAttribute::Extension));
    EXPECT_EQ(member.GetAttribute(KnownAttribute::CompilerGenerated), &compilerGenerated);
    EXPECT_EQ(member.GetAttribute(KnownAttribute::Extension), nullptr);
}

// ---------------------------------------------------------------------------
// IMember -- polymorphic dispatch through an `IMember*` reaches the concrete accessors of
// every inherited base (`ISymbol` / `ICompilationProvider` / `INamedElement` / `IEntity`)
// AND the IMember-own accessors (the dynamic dispatch the type-system paths rely on:
// `TypeSystemAstBuilder` / the resolver hold a member as an `IMember*` and read its names,
// kind, return type, virtuality, and attributes through the base).
// ---------------------------------------------------------------------------
TEST(IMemberTest, DispatchesPolymorphicallyThroughIMemberPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestMember member("Get", "C.Get", "C.Get", "", SymbolKind::Method, compilation, 0x06000006u,
                      returnType, Accessibility::Public, false, false, false, false,
                      /*isVirtual*/ true, /*isOverride*/ false, /*isOverridable*/ true);
    IMember* base = &member;
    EXPECT_EQ(base->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(base->Name(), "Get");
    EXPECT_EQ(base->FullName(), "C.Get");
    EXPECT_EQ(base->ReflectionName(), "C.Get");
    EXPECT_EQ(&base->Compilation(), &compilation);
    EXPECT_EQ(base->MetadataToken(), 0x06000006u);
    EXPECT_EQ(base->Accessibility(), Accessibility::Public);
    EXPECT_EQ(&base->ReturnType(), returnType.get());
    EXPECT_EQ(base->ReturnType().Name(), "Int32");
    EXPECT_EQ(base->MemberDefinition(), &member);
    EXPECT_TRUE(base->IsVirtual());
    EXPECT_TRUE(base->IsOverridable());
    EXPECT_EQ(base->Substitution(), nullptr);
    EXPECT_EQ(base->Specialize(nullptr), &member);
    EXPECT_TRUE(base->Equals(&member, nullptr));
}

// ---------------------------------------------------------------------------
// IMember -- polymorphic dispatch through the `IEntity*` / `ISymbol*` base pointers (an
// `IMember` IS-A `IEntity` IS-A `ISymbol`, so each base pointer dispatches to the concrete
// override, including the single `Name()` that is the final overrider for all three bases).
// ---------------------------------------------------------------------------
TEST(IMemberTest, DispatchesPolymorphicallyThroughIEntityAndISymbolPointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(7);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    auto owned = std::make_unique<TestMember>(
        "Run", "C.Run", "C.Run", "", SymbolKind::Method, compilation, 0x06000007u, returnType,
        Accessibility::Public, false, false, false, false, false, false, false);
    IEntity* asEntity = owned.get();
    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asEntity->Name(), "Run");
    EXPECT_EQ(asEntity->SymbolKind(), SymbolKind::Method);
    EXPECT_EQ(asEntity->MetadataToken(), 0x06000007u);
    EXPECT_EQ(asSymbol->Name(), "Run");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::Method);
    // destroying `owned` runs the `TestMember` destructor through the virtual `~IMember()`
    // (which chains to `~IEntity()` / `~ISymbol()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IMember -- has a virtual destructor (a concrete subclass can be deleted through an
// `IMember*` / `IEntity*` / `ISymbol*` and the derived destructor runs), the established
// abstract-base contract; the single-inheritance `IMember` -> `IEntity` -> ... chain
// composes correctly.
// ---------------------------------------------------------------------------
TEST(IMemberTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IMember>,
        "IMember must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IEntity>,
        "IEntity (the base of IMember) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IMember>,
        "IMember must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IMember>,
        "IMember must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(8);
    auto returnType = std::make_shared<KnownType>(KnownTypeCode::Void);
    std::unique_ptr<IMember> owned = std::make_unique<TestMember>(
        "X", "C.X", "C.X", "", SymbolKind::Field, compilation, 0x04000001u, returnType,
        Accessibility::Public, true, false, false, false, false, false, false);
    EXPECT_EQ(owned->Name(), "X");
    EXPECT_EQ(owned->SymbolKind(), SymbolKind::Field);
    // destroying `owned` runs the `TestMember` destructor through the virtual `~IMember()`.
    owned.reset();
    SUCCEED();
}
