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
// OTHERWISE, ARISING FROM, IN OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `IParameter` (cpp/Decompiler/TypeSystem/IParameter.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IParameter.cs) and its dedicated sub-leaf
// `LifetimeAnnotation` (cpp/Decompiler/TypeSystem/LifetimeAnnotation.hpp, the C# 11
// scoped-reference struct co-located in IParameter.cs). `IParameter : IVariable` adds
// the parameter's attributes (`GetAttributes`), the reference kind (`ReferenceKind`),
// the C# 11 scoped annotation (`Lifetime`), the `IsParams` / `IsOptional` /
// `HasConstantValueInSignature` flags, and the owning `IParameterizedMember` (nullable).
//
// The cyclic member types `IAttribute` and `IParameterizedMember` are only
// forward-declared in `IParameter.hpp` (they are not yet ported), so this test file
// provides minimal complete stand-ins for both in the `ILSpy::Decompiler::TypeSystem`
// namespace (virtual destructors only) so the `TestParameter` stub can hold and return
// concrete instances. The `IAttribute` stand-in is IDENTICAL to the one in
// `IEntity_Test.cpp`; the two identical class definitions across translation units
// satisfy the One Definition Rule (a class type may be defined identically in multiple
// TUs, as a header is), so both test files coexist in the `ilspy_tests` executable. The
// `IParameterizedMember` stand-in is the first; future test files that need it complete
// must define the IDENTICAL stand-in until the real `IParameterizedMember.hpp` lands.
// These stand-ins are TEST FIXTURES, NOT faithful ports of the full surfaces; they are
// dropped when the real headers land.

#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// `IAttribute` is now the real port (cpp/Decompiler/TypeSystem/IAttribute.hpp, D386);
// it is included above rather than forward-declared as a stand-in. `IMethod` (its
// `Constructor` slot return type) remains forward-declared inside `IAttribute.hpp`.
// `IParameterizedMember` is now the real port (cpp/Decompiler/TypeSystem/IParameterizedMember.hpp,
// D388); it is included above rather than forward-declared as a stand-in. Its base `IMember`
// (which forward-declares `TypeParameterSubstitution` / `TypeVisitor`) is pulled in
// transitively, so the reconciled `TestParameterizedMember` stub below derives from the real
// interface and overrides every pure-virtual (the documented stand-in-reconciliation step,
// the D386 `IAttribute`-stand-in-reconciliation precedent).

// Minimal test stand-in for `ICompilation` (the parent compilation interface). IDENTICAL to
// the stand-in in `ICompilationProvider_Test.cpp` / `IEntity_Test.cpp` / `ITypeParameter_Test.cpp`
// / `IMember_Test.cpp` / `IParameterizedMember_Test.cpp` (a virtual destructor only); the
// identical class definitions across translation units satisfy the One Definition Rule.
// Replaced by the real `ICompilation.hpp` when that lands. Required here because the
// reconciled `TestParameterizedMember` (deriving from the real `IParameterizedMember` ->
// `IMember` -> `IEntity` -> `ICompilationProvider`) must override `Compilation()` returning
// `const ICompilation&`, which needs a complete `ICompilation` to bind to.
class ICompilation {
public:
    virtual ~ICompilation() = default;
};

} // namespace ILSpy::Decompiler::TypeSystem

namespace {

// A minimal concrete `ICompilation` stand-in so the reconciled `TestParameterizedMember`
// can hold and return a compilation (the D379 test stand-in pattern).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id) {}
    int id() const { return id_; }
private:
    int id_;
};

// A concrete `IParameterizedMember` for testing (identity-testable via pointer compare).
// Reconciled to derive from the REAL `IParameterizedMember` (D388) now that the header has
// landed: it overrides every `IMember` / `IEntity` / `ICompilationProvider` / `INamedElement`
// / `ISymbol` pure-virtual with simple defaults, plus the `IParameterizedMember`-own
// `Parameters` (empty). The test-specific `id()` accessor and `id_` member are kept so the
// existing `Owner` identity test (`static_cast<const TestParameterizedMember*>(...)->id()`)
// continues to work. The single `Name()` override is the final overrider for the
// `ISymbol::Name()` / `INamedElement::Name()` / `IEntity::Name()` diamond (disambiguated by
// `IEntity`'s redeclaration). The three long-pole-dep members (`Substitution` /
// `Specialize` / `Equals`) use the `nullptr` / `this` / identity stand-ins (the documented
// "forward-declared-IMember, deferring the long-pole deps" shell strategy).
class TestParameterizedMember : public ILSpy::Decompiler::TypeSystem::IParameterizedMember {
public:
    explicit TestParameterizedMember(int id)
        : id_(id),
          returnType_(std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
              ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void)) {}

    int id() const { return id_; }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }
    std::string Name() const override { return "method"; }

    // --- INamedElement ---
    std::string FullName() const override { return "method"; }
    std::string ReflectionName() const override { return "method"; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0u; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override
    {
        return nullptr;
    }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return nullptr; }
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
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override
    {
        return ILSpy::Decompiler::TypeSystem::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(
        const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override
    {
        return this;
    }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj,
                const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override
    {
        return obj == this;
    }

    // --- IParameterizedMember ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override
    {
        return {};
    }

private:
    int id_;
    TestCompilation compilation_{0};
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
};

// A minimal concrete `IAttribute` for testing (identity-testable via pointer compare).
// Derives from the real `IAttribute` (D386) and implements every pure-virtual with simple
// defaults (`AttributeType` returns a `KnownType(Object)` by reference, `Constructor` is
// null, `HasDecodeErrors` is false, the argument vectors are empty); the test-specific `id()`
// accessor and `id_` member are kept so the existing `GetAttributes` pointer-identity tests
// continue to work.
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(int id) : id_(id), attributeType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}
    int id() const { return id_; }

    // --- IAttribute ---
    const ILSpy::Decompiler::TypeSystem::IType& AttributeType() const override { return attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> FixedArguments() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    int id_;
    ILSpy::Decompiler::TypeSystem::KnownType attributeType_;
};

// A minimal concrete `IParameter` for testing: holds the configured scalar/pointer state
// and returns it from every accessor (the shape a real `MetadataParameter` /
// `DefaultParameter` takes: it stores its kind, name, type, const-ness, boxed default,
// attributes, reference kind, lifetime annotation, params/optional/has-constant flags, and
// owner). The `SymbolKind` member/return type is qualified because the inherited
// `ISymbol::SymbolKind()` member function hides the namespace-scope `SymbolKind` enum
// inside this derived class (the D372 cross-scope name-hiding crux).
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(ILSpy::Decompiler::TypeSystem::SymbolKind kind, std::string name,
                  ILSpy::Decompiler::TypeSystem::ITypePtr type, bool isConst,
                  std::any constantValue,
                  ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind,
                  ILSpy::Decompiler::TypeSystem::LifetimeAnnotation lifetime,
                  bool isParams, bool isOptional, bool hasConstantValueInSignature)
        : kind_(kind), name_(std::move(name)), type_(std::move(type)),
          isConst_(isConst), constantValue_(std::move(constantValue)),
          referenceKind_(referenceKind), lifetime_(lifetime),
          isParams_(isParams), isOptional_(isOptional),
          hasConstantValueInSignature_(hasConstantValueInSignature) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- IVariable ---
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override
    {
        return constantValue_;
    }

    // --- IParameter ---
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return attributes_;
    }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    {
        return referenceKind_;
    }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override
    {
        return lifetime_;
    }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return hasConstantValueInSignature_; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override
    {
        return owner_;
    }

    // Test wiring (set the nullable / collection slots after construction).
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a)
    {
        attributes_.push_back(a);
    }
    void SetOwner(const ILSpy::Decompiler::TypeSystem::IParameterizedMember* owner)
    {
        owner_ = owner;
    }

private:
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
    bool isConst_;
    std::any constantValue_;
    ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation lifetime_;
    bool isParams_, isOptional_, hasConstantValueInSignature_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* owner_ = nullptr;
};

} // namespace

// ---------------------------------------------------------------------------
// LifetimeAnnotation -- default-constructed to all-false (the
// `DefaultParameter.Lifetime => default` case), and the `ScopedRef` get/set accessor
// delegates to the `RefScoped` field (the C# property backing).
// ---------------------------------------------------------------------------
TEST(LifetimeAnnotationTest, DefaultIsAllFalseAndScopedRefDelegatesToRefScoped)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    LifetimeAnnotation la;
    EXPECT_FALSE(la.RefScoped);
    EXPECT_FALSE(la.ValueScoped);
    EXPECT_FALSE(la.ScopedRef());

    // The getter delegates to RefScoped.
    la.RefScoped = true;
    EXPECT_TRUE(la.ScopedRef());
    // The setter delegates to RefScoped.
    la.ScopedRef(false);
    EXPECT_FALSE(la.RefScoped);
    EXPECT_FALSE(la.ScopedRef());
    la.ScopedRef(true);
    EXPECT_TRUE(la.RefScoped);
    EXPECT_TRUE(la.ScopedRef());
}

// ---------------------------------------------------------------------------
// LifetimeAnnotation -- the `ValueScoped` field is independent of `RefScoped` /
// `ScopedRef` (the C# 11 preview `ref scoped` annotation, no longer supported, but kept
// verbatim for API fidelity).
// ---------------------------------------------------------------------------
TEST(LifetimeAnnotationTest, ValueScopedIsIndependentOfRefScoped)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    LifetimeAnnotation la;
    la.ValueScoped = true;
    EXPECT_TRUE(la.ValueScoped);
    EXPECT_FALSE(la.RefScoped);
    EXPECT_FALSE(la.ScopedRef());
    la.ScopedRef(true);
    EXPECT_TRUE(la.RefScoped);
    EXPECT_TRUE(la.ValueScoped);
}

// ---------------------------------------------------------------------------
// IParameter -- every scalar accessor returns the configured value (the shape a real
// type-system parameter exposes: its kind, name, type, const-ness, reference kind,
// lifetime annotation, and the params/optional/has-constant flags).
// ---------------------------------------------------------------------------
TEST(IParameterTest, ScalarAccessorsReturnConfiguredValues)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TS::LifetimeAnnotation lifetime;
    lifetime.ScopedRef(true);
    TestParameter param(TS::SymbolKind::Parameter, "value", intType, /*isConst*/ false,
        std::any(), TS::ReferenceKind::Ref, lifetime,
        /*isParams*/ false, /*isOptional*/ true, /*hasConstantValueInSignature*/ true);
    EXPECT_EQ(param.SymbolKind(), TS::SymbolKind::Parameter);
    EXPECT_EQ(param.Name(), "value");
    EXPECT_EQ(&param.Type(), intType.get());
    EXPECT_EQ(param.Type().Name(), "Int32");
    EXPECT_FALSE(param.IsConst());
    EXPECT_FALSE(param.GetConstantValue().has_value());
    EXPECT_EQ(param.ReferenceKind(), TS::ReferenceKind::Ref);
    EXPECT_TRUE(param.Lifetime().ScopedRef());
    EXPECT_TRUE(param.Lifetime().RefScoped);
    EXPECT_FALSE(param.IsParams());
    EXPECT_TRUE(param.IsOptional());
    EXPECT_TRUE(param.HasConstantValueInSignature());
}

// ---------------------------------------------------------------------------
// IParameter -- `GetAttributes` returns the configured non-owning snapshot (the
// parameter owns its attributes; the caller holds raw pointers), matching the AST
// non-owning model and the `IEntity::GetAttributes` precedent.
// ---------------------------------------------------------------------------
TEST(IParameterTest, GetAttributesReturnsConfiguredSnapshot)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    TestParameter param(TS::SymbolKind::Parameter, "x", intType, /*isConst*/ false,
        std::any(), TS::ReferenceKind::None, TS::LifetimeAnnotation{},
        /*isParams*/ true, /*isOptional*/ false, /*hasConstantValueInSignature*/ false);
    TestAttribute callerMemberName(1);
    TestAttribute maybeNull(2);
    param.AddAttribute(&callerMemberName);
    param.AddAttribute(&maybeNull);

    const auto attrs = param.GetAttributes();
    EXPECT_EQ(attrs.size(), 2u);
    EXPECT_EQ(attrs[0], &callerMemberName);
    EXPECT_EQ(attrs[1], &maybeNull);
}

// ---------------------------------------------------------------------------
// IParameter -- `Owner` returns the configured parameterized member, or nullptr for a
// lambda/anonymous-method parameter (the C# `May return null` doc comment). A nullable
// pointer return (the `IEntity::ParentModule` precedent).
// ---------------------------------------------------------------------------
TEST(IParameterTest, OwnerReturnsConfiguredMemberOrNull)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);

    // The owned case: a parameter of a method.
    TestParameterizedMember method(7);
    TestParameter param(TS::SymbolKind::Parameter, "arg", intType, /*isConst*/ false,
        std::any(), TS::ReferenceKind::In, TS::LifetimeAnnotation{},
        /*isParams*/ false, /*isOptional*/ false, /*hasConstantValueInSignature*/ false);
    param.SetOwner(&method);
    EXPECT_EQ(param.Owner(), &method);
    EXPECT_EQ(static_cast<const TestParameterizedMember*>(param.Owner())->id(), 7);

    // The null case: a lambda/anonymous-method parameter has no owner.
    TestParameter lambdaParam(TS::SymbolKind::Parameter, "captured", intType, /*isConst*/ false,
        std::any(), TS::ReferenceKind::None, TS::LifetimeAnnotation{},
        /*isParams*/ false, /*isOptional*/ false, /*hasConstantValueInSignature*/ false);
    EXPECT_EQ(lambdaParam.Owner(), nullptr);
}

// ---------------------------------------------------------------------------
// IParameter -- polymorphic dispatch through an `IParameter*` reaches the concrete
// accessors of every inherited base (ISymbol / IVariable) AND the IParameter-own
// accessors (the dynamic dispatch the type-system paths rely on: `TypeSystemAstBuilder`
// / the resolver hold a parameter as an `IParameter*` and read its names, kind, type,
// reference kind, lifetime, and attributes through the base).
// ---------------------------------------------------------------------------
TEST(IParameterTest, DispatchesPolymorphicallyThroughIParameterPointer)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto stringType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    TS::LifetimeAnnotation lifetime;
    lifetime.ScopedRef(true);
    TestParameter param(TS::SymbolKind::Parameter, "name", stringType, /*isConst*/ false,
        std::any(std::string("default")), TS::ReferenceKind::Out, lifetime,
        /*isParams*/ false, /*isOptional*/ true, /*hasConstantValueInSignature*/ true);
    TS::IParameter* base = &param;
    EXPECT_EQ(base->SymbolKind(), TS::SymbolKind::Parameter);
    EXPECT_EQ(base->Name(), "name");
    EXPECT_EQ(base->Type().Name(), "String");
    EXPECT_EQ(base->ReferenceKind(), TS::ReferenceKind::Out);
    EXPECT_TRUE(base->Lifetime().ScopedRef());
    EXPECT_TRUE(base->IsOptional());
    const auto cv = base->GetConstantValue();
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(std::any_cast<std::string>(cv), "default");
}

// ---------------------------------------------------------------------------
// IParameter -- polymorphic dispatch through the `IVariable*` and `ISymbol*` base
// pointers (an `IParameter` IS-A each of its bases, so each base pointer dispatches to
// the concrete override). `IVariable` IS-A `ISymbol`, so `ISymbol*` dispatches the
// inherited `SymbolKind()`/`Name()` contract.
// ---------------------------------------------------------------------------
TEST(IParameterTest, DispatchesPolymorphicallyThroughIVariableAndISymbolPointers)
{
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto owned = std::make_unique<TestParameter>(
        TS::SymbolKind::Parameter, "count", intType, /*isConst*/ false,
        std::any(std::int32_t(42)), TS::ReferenceKind::None, TS::LifetimeAnnotation{},
        /*isParams*/ false, /*isOptional*/ false, /*hasConstantValueInSignature*/ false);
    TS::IVariable* asVariable = owned.get();
    TS::ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asVariable->Name(), "count");
    EXPECT_EQ(asVariable->Type().Name(), "Int32");
    const auto cv = asVariable->GetConstantValue();
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(cv), 42);
    EXPECT_EQ(asSymbol->SymbolKind(), TS::SymbolKind::Parameter);
    EXPECT_EQ(asSymbol->Name(), "count");
    // destroying `owned` runs the `TestParameter` destructor through the virtual
    // `~IParameter()` (which chains to `~IVariable()` / `~ISymbol()`).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IParameter -- has a virtual destructor (a concrete subclass can be deleted through an
// `IParameter*` / `IVariable*` / `ISymbol*` and the derived destructor runs), the
// established abstract-base contract; the single-inheritance `IVariable` -> `ISymbol`
// chain composes correctly.
// ---------------------------------------------------------------------------
TEST(IParameterTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IParameter>,
        "IParameter must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IVariable>,
        "IVariable (the base of IParameter) must have a virtual destructor");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ISymbol>,
        "ISymbol (the base of IVariable) must have a virtual destructor");
    namespace TS = ILSpy::Decompiler::TypeSystem;
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    std::unique_ptr<TS::IParameter> owned = std::make_unique<TestParameter>(
        TS::SymbolKind::Parameter, "z", intType, /*isConst*/ false, std::any(),
        TS::ReferenceKind::None, TS::LifetimeAnnotation{},
        /*isParams*/ false, /*isOptional*/ false, /*hasConstantValueInSignature*/ false);
    EXPECT_EQ(owned->Name(), "z");
    // destroying `owned` runs the `TestParameter` destructor through the virtual
    // `~IParameter()` (which chains to `~IVariable()` / `~ISymbol()`).
    owned.reset();
    SUCCEED();
}
