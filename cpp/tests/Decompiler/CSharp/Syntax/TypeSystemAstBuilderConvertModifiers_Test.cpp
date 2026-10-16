// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the TypeSystemAstBuilder "Convert Modifiers" region
// (cpp/Decompiler/CSharp/Syntax/TypeSystemAstBuilder.{hpp,cpp}, the port of
// TypeSystemAstBuilder.cs lines 2518-2596: NeedsAccessibility +
// GetMemberModifiers; the ModifierFromAccessibility free function the region
// composes with is already tested alongside its own landing).
//
// The load-bearing cruxes:
//  (a) NeedsAccessibility: the explicit-interface-implementation short circuit,
//      the static-constructor and destructor special cases, the
//      interface-declared members needing accessibility only when not public,
//      and the local-function exclusion (`member is not IMethod ||
//      !method.IsLocalFunction` -- a local function carries no accessibility
//      modifier);
//  (b) GetMemberModifiers: the accessibility bits under
//      ShowAccessibility && NeedsAccessibility (including the
//      UsePrivateProtectedAccessibility gate on ProtectedAndInternal), the
//      local-function branch reading ONLY IsStaticLocalFunction (the wrapper's
//      unconditionally-true IsStatic is deliberately NOT read -- a non-static
//      local function carries no `static` modifier), the Readonly bit for
//      ThisIsRefReadOnly methods on NON-readonly declaring-type definitions
//      (the `?.IsReadOnly == false` lifted-bool crux: a null definition or a
//      readonly definition yields no bit), and the interface-vs-class
//      Abstract / Virtual / Override / Sealed spread (the interface
//      default-implementation Sealed arm requiring an IMethod with a body; the
//      static-interface-member Abstract/Virtual arms; the explicit-interface
//      suppression of Override and Sealed).
//
// The stub shapes: TestMethod models a method with every flag the region reads
// configurable; NonMethodMember models the `member is not IMethod` side (a
// field); ReadOnlyDef models a `readonly struct` declaring type (the
// IsReadOnly=true side of the Readonly gate); the real ported LocalFunctionMethod
// wrapper (with a TestMethod base) exercises the concrete-class RTTI branch.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Modifiers.hpp"

#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/LocalFunctionMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::Modifiers;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEntity;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::Implementation::LocalFunctionMethod;
using ILSpy::Decompiler::TypeSystem::KnownAttribute;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                              const std::string& ns,
                                              TypeKind kind,
                                              const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, 0)), kind,
        Accessibility::Public, compilation, nullptr, KnownTypeCode::None);
}

// A `readonly struct` declaring type (the IsReadOnly == true side of the
// GetMemberModifiers Readonly gate; LookupTypeDefinition hardcodes false).
class ReadOnlyDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    bool IsReadOnly() const override { return true; }
};

// A configurable `IMethod` stub (the LookupMethod shape, but with every member
// flag the Convert Modifiers region reads configurable): the SymbolKind
// (Constructor / Destructor / Method), the DeclaringType /
// DeclaringTypeDefinition pair, the Accessibility, and the IsStatic /
// IsVirtual / IsAbstract / IsOverride / IsSealed /
// IsExplicitInterfaceImplementation / ThisIsRefReadOnly / HasBody /
// IsLocalFunction flags. Every default preserves the plain-shape behavior, so
// a test sets only the flags its arm reads.
class TestMethod : public IMethod {
public:
    TestMethod(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetSymbolKind(TS::SymbolKind k) { kind_ = k; }
    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }
    void SetDeclaringTypeDefinition(const ITypeDefinition* d) { declaringTypeDefinition_ = d; }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetStatic(bool v) { isStatic_ = v; }
    void SetIsVirtual(bool v) { isVirtual_ = v; }
    void SetIsAbstract(bool v) { isAbstract_ = v; }
    void SetIsOverride(bool v) { isOverride_ = v; }
    void SetIsSealed(bool v) { isSealed_ = v; }
    void SetIsExplicitInterfaceImplementation(bool v) {
        isExplicitInterfaceImplementation_ = v;
    }
    void SetThisIsRefReadOnly(bool v) { thisIsRefReadOnly_ = v; }
    void SetHasBody(bool v) { hasBody_ = v; }
    void SetIsLocalFunction(bool v) { isLocalFunction_ = v; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override {
        return declaringTypeDefinition_;
    }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return isAbstract_; }
    bool IsSealed() const override { return isSealed_; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override {
        return isExplicitInterfaceImplementation_;
    }
    bool IsVirtual() const override { return isVirtual_; }
    bool IsOverride() const override { return isOverride_; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMethod* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

    // --- IParameterizedMember ---
    std::vector<const IParameter*> Parameters() const override { return {}; }

    // --- IMethod ---
    std::vector<const IAttribute*> GetReturnTypeAttributes() const override { return {}; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return thisIsRefReadOnly_; }
    std::vector<const ITypeParameter*> TypeParameters() const override { return {}; }
    std::vector<ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return isLocalFunction_; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return hasBody_; }
    bool IsAccessor() const override { return false; }
    const IMember* AccessorOwner() const override { return nullptr; }
    MethodSemanticsAttributes AccessorKind() const override {
        return MethodSemanticsAttributes::None;
    }
    const IMethod* ReducedFrom() const override { return nullptr; }

private:
    std::string name_;
    const ICompilation& compilation_;
    KnownType returnType_{ KnownTypeCode::Object };
    TS::SymbolKind kind_ = TS::SymbolKind::Method;
    ITypePtr declaringType_;
    const ITypeDefinition* declaringTypeDefinition_ = nullptr;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    bool isStatic_ = false;
    bool isVirtual_ = false;
    bool isAbstract_ = false;
    bool isOverride_ = false;
    bool isSealed_ = false;
    bool isExplicitInterfaceImplementation_ = false;
    bool thisIsRefReadOnly_ = false;
    bool hasBody_ = false;
    bool isLocalFunction_ = false;
};

// A configurable non-method `IMember` stub (the `member is not IMethod` side:
// a field). The DeclaringType / Accessibility / IsStatic are configurable for
// the interface-vs-class arms; every other member flag stays false.
class NonMethodMember : public IMember {
public:
    NonMethodMember(std::string name, const ICompilation& compilation)
        : name_(std::move(name)), compilation_(compilation) {}

    void SetDeclaringType(ITypePtr t) { declaringType_ = std::move(t); }
    void SetAccessibility(TS::Accessibility a) { accessibility_ = a; }
    void SetStatic(bool v) { isStatic_ = v; }

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
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return declaringType_; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return accessibility_; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return returnType_; }
    std::vector<const IMember*> ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const IMember* Specialize(const TypeParameterSubstitution*) const override { return this; }
    bool Equals(const IMember* obj, const TypeVisitor*) const override { return obj == this; }

private:
    std::string name_;
    const ICompilation& compilation_;
    KnownType returnType_{ KnownTypeCode::Object };
    ITypePtr declaringType_;
    TS::Accessibility accessibility_ = TS::Accessibility::Public;
    bool isStatic_ = false;
};

// The fixture: a compilation, the class / interface / readonly-struct
// declaring types the members are declared in, and a resolver-less builder
// (the default configuration: ShowAccessibility / ShowModifiers /
// UsePrivateProtectedAccessibility all true).
struct ConvertModifiersFixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> classDef;
    std::shared_ptr<LookupTypeDefinition> interfaceDef;
    std::shared_ptr<LookupTypeDefinition> nonReadOnlyStructDef;
    std::shared_ptr<ReadOnlyDef> readOnlyStructDef;
    TypeSystemAstBuilder builder;

    ConvertModifiersFixture()
        : classDef(MakeDef("Host", "Ns", TypeKind::Class, compilation)),
          interfaceDef(MakeDef("IHost", "Ns", TypeKind::Interface, compilation)),
          nonReadOnlyStructDef(MakeDef("Plain", "Ns", TypeKind::Struct, compilation)),
          readOnlyStructDef(std::make_shared<ReadOnlyDef>(
              "Ro", "Ns", FullTypeName(TopLevelTypeName("Ns", "Ro", 0)), TypeKind::Struct,
              Accessibility::Public, compilation, nullptr, KnownTypeCode::None)) {}

    // A plain public instance class method (the base shape every test mutates
    // through the setters).
    std::shared_ptr<TestMethod> MakeMethod(const char* name = "M") {
        auto m = std::make_shared<TestMethod>(name, compilation);
        m->SetDeclaringType(classDef);
        return m;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// NeedsAccessibility
// ---------------------------------------------------------------------------

TEST(NeedsAccessibilityTest, ExplicitInterfaceImplementationReturnsFalse) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsExplicitInterfaceImplementation(true);
    EXPECT_FALSE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, InstanceConstructorNeedsAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetSymbolKind(TS::SymbolKind::Constructor);
    EXPECT_TRUE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, StaticConstructorDoesNotNeedAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetSymbolKind(TS::SymbolKind::Constructor);
    m->SetStatic(true);
    EXPECT_FALSE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, DestructorNeverNeedsAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetSymbolKind(TS::SymbolKind::Destructor);
    EXPECT_FALSE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, InterfacePublicMemberDoesNotNeedAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    EXPECT_FALSE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, InterfaceNonPublicMemberNeedsAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetAccessibility(Accessibility::Internal);
    EXPECT_TRUE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, NonMethodMemberNeedsAccessibility) {
    ConvertModifiersFixture f;
    NonMethodMember field("F", f.compilation);
    field.SetDeclaringType(f.classDef);
    EXPECT_TRUE(f.builder.NeedsAccessibility(field));
}

TEST(NeedsAccessibilityTest, LocalFunctionMethodDoesNotNeedAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsLocalFunction(true);
    EXPECT_FALSE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, PlainMethodNeedsAccessibility) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    EXPECT_TRUE(f.builder.NeedsAccessibility(*m));
}

TEST(NeedsAccessibilityTest, NullDeclaringTypeFallsToDefaultArm) {
    ConvertModifiersFixture f;
    // No DeclaringType set (the C# `declaringType?.Kind` null-conditional reads
    // a null declaring type as not-an-interface, so the default arm decides).
    TestMethod m("M", f.compilation);
    EXPECT_TRUE(f.builder.NeedsAccessibility(m));
}

TEST(NeedsAccessibilityTest, LocalFunctionWrapperDoesNotNeedAccessibility) {
    ConvertModifiersFixture f;
    auto base = f.MakeMethod("Local");
    auto localFunction = std::make_shared<LocalFunctionMethod>(
        base, "Local", /*isStaticLocalFunction=*/false, 0, 0);
    EXPECT_FALSE(f.builder.NeedsAccessibility(*localFunction));
}

// ---------------------------------------------------------------------------
// GetMemberModifiers
// ---------------------------------------------------------------------------

TEST(GetMemberModifiersTest, PublicClassMethodYieldsPublic) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public);
}

TEST(GetMemberModifiersTest, PrivateClassMethodYieldsPrivate) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetAccessibility(Accessibility::Private);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Private);
}

TEST(GetMemberModifiersTest, ProtectedAndInternalWithFlagYieldsPrivateProtected) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetAccessibility(Accessibility::ProtectedAndInternal);
    // The default configuration has UsePrivateProtectedAccessibility == true.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m),
              Modifiers::Private | Modifiers::Protected);
}

TEST(GetMemberModifiersTest, ProtectedAndInternalWithoutFlagYieldsProtected) {
    ConvertModifiersFixture f;
    f.builder.UsePrivateProtectedAccessibility() = false;
    auto m = f.MakeMethod();
    m->SetAccessibility(Accessibility::ProtectedAndInternal);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Protected);
}

TEST(GetMemberModifiersTest, ShowAccessibilityFalseSuppressesAccessibilityBits) {
    ConvertModifiersFixture f;
    f.builder.ShowAccessibility() = false;
    auto m = f.MakeMethod();
    m->SetAccessibility(Accessibility::Private);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::None);
}

TEST(GetMemberModifiersTest, DestructorYieldsNoBits) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetSymbolKind(TS::SymbolKind::Destructor);
    // A destructor needs no accessibility (NeedsAccessibility false) and the
    // general branch produces no modifier bits for the plain shape.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::None);
}

TEST(GetMemberModifiersTest, StaticMethodYieldsStatic) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetStatic(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Static);
}

TEST(GetMemberModifiersTest, StaticLocalFunctionYieldsStaticOnly) {
    ConvertModifiersFixture f;
    auto base = f.MakeMethod("Local");
    auto localFunction = std::make_shared<LocalFunctionMethod>(
        base, "Local", /*isStaticLocalFunction=*/true, 0, 0);
    // No accessibility bits (a local function needs none) and only the
    // source-level static flag decides.
    EXPECT_EQ(f.builder.GetMemberModifiers(*localFunction), Modifiers::Static);
}

TEST(GetMemberModifiersTest, NonStaticLocalFunctionYieldsNoStaticBit) {
    ConvertModifiersFixture f;
    auto base = f.MakeMethod("Local");
    auto localFunction = std::make_shared<LocalFunctionMethod>(
        base, "Local", /*isStaticLocalFunction=*/false, 0, 0);
    // The wrapper's IsStatic() is unconditionally true, but the local-function
    // branch reads only IsStaticLocalFunction -- a non-static local function
    // carries no `static` modifier.
    EXPECT_EQ(localFunction->IsStatic(), true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*localFunction), Modifiers::None);
}

TEST(GetMemberModifiersTest, ThisIsRefReadOnlyYieldsReadonly) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetThisIsRefReadOnly(true);
    m->SetDeclaringTypeDefinition(f.nonReadOnlyStructDef.get());
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Readonly);
}

TEST(GetMemberModifiersTest, ThisIsRefReadOnlyOnReadOnlyDeclaringTypeYieldsNoReadonly) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetThisIsRefReadOnly(true);
    m->SetDeclaringTypeDefinition(f.readOnlyStructDef.get());
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public);
}

TEST(GetMemberModifiersTest, ThisIsRefReadOnlyWithNullDeclaringTypeYieldsNoReadonly) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetThisIsRefReadOnly(true);
    // No DeclaringTypeDefinition set: the C# `DeclaringTypeDefinition?.IsReadOnly
    // == false` is the lifted-bool == (true only for a definite false), so a
    // null definition yields no bit.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public);
}

TEST(GetMemberModifiersTest, InterfaceDefaultImplementationMethodYieldsSealed) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetHasBody(true);
    // An interface member that is public needs no accessibility bit; the
    // default-implementation shape (non-static, non-virtual, non-abstract,
    // non-override, with a body) renders as `sealed`.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Sealed);
}

TEST(GetMemberModifiersTest, InterfaceNonMethodMemberDoesNotGetSealed) {
    ConvertModifiersFixture f;
    NonMethodMember field("F", f.compilation);
    field.SetDeclaringType(f.interfaceDef);
    // The Sealed arm requires `member is IMethod method2 && method2.HasBody`;
    // a field never gets it.
    EXPECT_EQ(f.builder.GetMemberModifiers(field), Modifiers::None);
}

TEST(GetMemberModifiersTest, InterfacePrivateMethodYieldsPrivateNotSealed) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetHasBody(true);
    m->SetAccessibility(Accessibility::Private);
    // The Private accessibility excludes the Sealed arm but IS rendered
    // (interface-declared non-public members need accessibility).
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Private);
}

TEST(GetMemberModifiersTest, InterfaceMethodWithoutBodyDoesNotGetSealed) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetHasBody(false);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::None);
}

TEST(GetMemberModifiersTest, InterfaceStaticAbstractMethodYieldsStaticAbstract) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetStatic(true);
    m->SetIsAbstract(true);
    // The general branch adds Static before the interface check (a static
    // interface member IS rendered `static`), then the static-in-interface arm
    // adds Abstract.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Static | Modifiers::Abstract);
}

TEST(GetMemberModifiersTest, InterfaceStaticVirtualMethodYieldsStaticVirtual) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetStatic(true);
    m->SetIsVirtual(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Static | Modifiers::Virtual);
}

TEST(GetMemberModifiersTest, InterfaceStaticVirtualOverrideYieldsOnlyStatic) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetDeclaringType(f.interfaceDef);
    m->SetStatic(true);
    m->SetIsVirtual(true);
    m->SetIsOverride(true);
    // An override does not re-state `virtual` (the !IsOverride guard); the
    // static member keeps its `static` modifier.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Static);
}

TEST(GetMemberModifiersTest, ClassAbstractMethodYieldsAbstract) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsAbstract(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Abstract);
}

TEST(GetMemberModifiersTest, ClassVirtualMethodYieldsVirtual) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsVirtual(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Virtual);
}

TEST(GetMemberModifiersTest, ClassVirtualOverrideYieldsOverrideOnly) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsVirtual(true);
    m->SetIsOverride(true);
    // An override does not re-state `virtual` (the !IsOverride guard); the
    // public class method keeps its accessibility bit.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Override);
}

TEST(GetMemberModifiersTest, ClassSealedMethodYieldsSealed) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsSealed(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Sealed);
}

TEST(GetMemberModifiersTest, ExplicitInterfaceImplementationSuppressesOverrideAndSealed) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsVirtual(true);
    m->SetIsOverride(true);
    m->SetIsSealed(true);
    m->SetIsExplicitInterfaceImplementation(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::None);
}

TEST(GetMemberModifiersTest, ShowModifiersFalseSuppressesModifierBits) {
    ConvertModifiersFixture f;
    f.builder.ShowModifiers() = false;
    auto m = f.MakeMethod();
    m->SetStatic(true);
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public);
}

TEST(GetMemberModifiersTest, AbstractWinsOverVirtual) {
    ConvertModifiersFixture f;
    auto m = f.MakeMethod();
    m->SetIsAbstract(true);
    m->SetIsVirtual(true);
    // The else-if: an abstract member does not also state `virtual`.
    EXPECT_EQ(f.builder.GetMemberModifiers(*m), Modifiers::Public | Modifiers::Abstract);
}
