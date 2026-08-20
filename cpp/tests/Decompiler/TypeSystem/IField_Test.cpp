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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for `IField` (cpp/Decompiler/TypeSystem/IField.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/IField.cs). `IField : IMember, IVariable` is the
// interface for a field or constant. It adds the three own flags `IsReadOnly` /
// `ReturnTypeIsRefReadOnly` / `IsVolatile` and, crucially, is the FIRST ported interface whose
// two bases BOTH derive (transitively) from a SHARED base -- `IMember` : `IEntity` : `ISymbol`
// and `IVariable` : `ISymbol` -- so `ISymbol` appears in BOTH branches of the inheritance graph.
//
// The shared-`ISymbol`-base diamond (the key new crux) means the non-virtual C++ port has TWO
// `ISymbol` subobjects, so BOTH `Name` and `SymbolKind` (the whole `ISymbol` surface) are
// ambiguous through an `IField*` and BOTH are redeclared in `IField.hpp` to disambiguate
// lookup. The C# `IField` redeclares only `Name` (C# interface flattening unifies the single
// `ISymbol.SymbolKind`); the C++ port adds a `SymbolKind` redeclaration purely so
// `field->SymbolKind()` compiles and dispatches the same single override `field.SymbolKind`
// reaches in C#. The test dispatches `Name()` / `SymbolKind()` through `IField*`, `IMember*`,
// and `IVariable*` (each an unambiguous upcast -- `IMember` and `IVariable` each appear once)
// and asserts all three reach the same override; it does NOT upcast to `ISymbol*` (ambiguous:
// two `ISymbol` subobjects -- a documented C++-vs-C# divergence).
//
// The `ICompilation` stand-in is IDENTICAL to the stand-in in the other Phase-5 test files
// (ODR-safe across translation units); `IAttribute` is the real port (D386), included below.

#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
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

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base can
// return a compilation (the D379 test stand-in pattern).
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
// implements every pure-virtual with simple defaults (`AttributeType` returns a `KnownType(Object)`
// by reference, `Constructor` is null, `HasDecodeErrors` is false, the argument vectors are
// empty); the test-specific `Kind()` accessor and `KnownAttribute kind_` member are kept so the
// `HasAttribute` / `GetAttribute` tests (which classify attributes by `KnownAttribute`) work.
// IDENTICAL in shape to the `TestAttribute` in `IEntity_Test.cpp` / `IParameter_Test.cpp` /
// `ITypeParameter_Test.cpp` / `IMember_Test.cpp`.
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

// A minimal concrete `IField` for testing: holds the configured state and returns it from every
// accessor (the shape a real `MetadataField` / `SpecializedField` takes). The
// `SymbolKind()` / `Name()` overrides are the final overrider for the `ISymbol` surface in BOTH
// `ISymbol` subobjects of the shared-`ISymbol`-base diamond (the `IField` redeclarations
// disambiguate lookup; a single override is the final overrider for both subobjects). The
// `SymbolKind` return type is qualified because the inherited `ISymbol::SymbolKind()` member
// function hides the namespace-scope `SymbolKind` enum in this derived class (the D372
// cross-scope name-hiding crux). `ReturnType()` (from `IMember`) and `Type()` (from
// `IVariable`) both return the field's type (the C# `MetadataField` semantics -- a field's
// return type IS its variable type). The long-pole `IMember` members (`Substitution` /
// `Specialize` / `Equals`) use the forward-declared-dep nullptr / `this` / identity stand-ins
// (the D387 `IMember` test-stand-in convention; `IField` does NOT redeclare `Specialize`, so the
// inherited `IMember::Specialize` is overridden with the degenerate `return this;` stand-in --
// the D390 `IProperty` precedent).
class TestField : public ILSpy::Decompiler::TypeSystem::IField {
public:
    TestField(std::string name,
              std::string fullName,
              std::string reflectionName,
              std::string ns,
              ILSpy::Decompiler::TypeSystem::SymbolKind kind,
              const TestCompilation& compilation,
              std::uint32_t metadataToken,
              ILSpy::Decompiler::TypeSystem::ITypePtr fieldType,
              ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
              bool isStatic, bool isAbstract, bool isSealed,
              bool isExplicitInterfaceImplementation,
              bool isVirtual, bool isOverride, bool isOverridable,
              bool isReadOnly, bool returnTypeIsRefReadOnly, bool isVolatile,
              bool isConst,
              std::any constantValue)
        : name_(std::move(name)), fullName_(std::move(fullName)),
          reflectionName_(std::move(reflectionName)), namespace_(std::move(ns)),
          kind_(kind), compilation_(compilation), metadataToken_(metadataToken),
          fieldType_(std::move(fieldType)), accessibility_(accessibility),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed),
          isExplicitInterfaceImplementation_(isExplicitInterfaceImplementation),
          isVirtual_(isVirtual), isOverride_(isOverride), isOverridable_(isOverridable),
          isReadOnly_(isReadOnly), returnTypeIsRefReadOnly_(returnTypeIsRefReadOnly),
          isVolatile_(isVolatile), isConst_(isConst),
          constantValue_(std::move(constantValue)) {}

    // --- ISymbol (IField redeclarations: disambiguate the shared-ISymbol-base diamond) ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    // The single `Name()` override is the final overrider for `IMember.Name` (via `IEntity`),
    // `IVariable.Name` (via `ISymbol`), and the `IField` redeclaration.
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
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *fieldType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override
    {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return isExplicitInterfaceImplementation_; }
    bool IsVirtual() const override { return isVirtual_; }
    bool IsOverride() const override { return isOverride_; }
    bool IsOverridable() const override { return isOverridable_; }
    // The long-pole-dep stand-ins (the D387 convention): `Substitution` returns `nullptr`
    // (`TypeParameterSubstitution` is forward-declared), `Specialize` returns `this` (a
    // degenerate stand-in for the real newly-specialized member), `Equals` is identity.
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

    // --- IVariable ---
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *fieldType_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override
    {
        return constantValue_;
    }

    // --- IField ---
    bool IsReadOnly() const override { return isReadOnly_; }
    bool ReturnTypeIsRefReadOnly() const override { return returnTypeIsRefReadOnly_; }
    bool IsVolatile() const override { return isVolatile_; }

    // Test wiring (set the nullable / collection slots after construction).
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }

private:
    std::string name_, fullName_, reflectionName_, namespace_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::ITypePtr fieldType_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    bool isStatic_, isAbstract_, isSealed_;
    bool isExplicitInterfaceImplementation_, isVirtual_, isOverride_, isOverridable_;
    bool isReadOnly_, returnTypeIsRefReadOnly_, isVolatile_;
    bool isConst_;
    std::any constantValue_;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
};

} // namespace

// ---------------------------------------------------------------------------
// IField -- the three IField-own scalar accessors return the configured values (the shape a
// real `MetadataField` exposes: its readonly-ness, ref-readonly-ness, and volatility).
// ---------------------------------------------------------------------------
TEST(IFieldTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    auto fieldType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestField field("count", "C.count", "C.count", "", SymbolKind::Field, compilation,
                    0x04000001u, fieldType, Accessibility::Private,
                    /*isStatic*/ false, /*isAbstract*/ false, /*isSealed*/ false,
                    /*isExplicitInterfaceImplementation*/ false,
                    /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false,
                    /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ true,
                    /*isConst*/ false, std::any());
    EXPECT_TRUE(field.IsReadOnly());
    EXPECT_FALSE(field.ReturnTypeIsRefReadOnly());
    EXPECT_TRUE(field.IsVolatile());
}

// ---------------------------------------------------------------------------
// IField -- the inherited `IVariable` accessors (`Type` / `IsConst` / `GetConstantValue`)
// dispatch through an `IField*` unambiguously (each name lives on only ONE of the two bases),
// returning the field's type and boxed constant value. A `const` field carries a constant
// value (the C# `MetadataField` `decimalConstantState` / `constantValue` path); a plain field
// has none.
// ---------------------------------------------------------------------------
TEST(IFieldTest, InheritedVariableAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TestField constField("MaxValue", "C.MaxValue", "C.MaxValue", "", SymbolKind::Field,
                        compilation, 0x04000002u, intType, Accessibility::Public,
                        /*isStatic*/ true, /*isAbstract*/ false, /*isSealed*/ false, false,
                        /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false,
                        /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
                        /*isConst*/ true, std::any(std::int32_t(42)));
    EXPECT_EQ(&constField.Type(), intType.get());
    EXPECT_EQ(constField.Type().Name(), "Int32");
    EXPECT_TRUE(constField.IsConst());
    const auto cv = constField.GetConstantValue();
    ASSERT_TRUE(cv.has_value());
    ASSERT_EQ(cv.type(), typeid(std::int32_t));
    EXPECT_EQ(std::any_cast<std::int32_t>(cv), 42);

    // A plain (non-const) field has no constant value.
    TestField plainField("count", "C.count", "C.count", "", SymbolKind::Field, compilation,
                        0x04000003u, intType, Accessibility::Private,
                        false, false, false, false, false, false, false,
                        /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
                        /*isConst*/ false, std::any());
    EXPECT_FALSE(plainField.IsConst());
    EXPECT_FALSE(plainField.GetConstantValue().has_value());
}

// ---------------------------------------------------------------------------
// IField -- THE SHARED-ISymbol-BASE DIAMOND CRUX: the single `Name()` and `SymbolKind()`
// overrides are the final overrider for the `ISymbol` surface in BOTH `ISymbol` subobjects (one
// inside `IMember` via `IEntity`, one inside `IVariable`), so dispatch through `IField*`,
// `IMember*`, and `IVariable*` (each an unambiguous upcast -- `IMember` and `IVariable` each
// appear once) all reach the SAME override and return the same value. An upcast to `ISymbol*`
// from an `IField*` is AMBIGUOUS (two `ISymbol` subobjects) and is NOT attempted here -- the
// documented C++-vs-C# divergence (C# flattens the shared interface; non-virtual C++ does not).
// ---------------------------------------------------------------------------
TEST(IFieldTest, NameAndSymbolKindDispatchThroughAllUnambiguousBases)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto fieldType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto owned = std::make_unique<TestField>(
        "count", "C.count", "C.count", "", SymbolKind::Field, compilation, 0x04000004u,
        fieldType, Accessibility::Private, false, false, false, false, false, false, false,
        /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
        /*isConst*/ false, std::any());
    TestField* field = owned.get();

    // Through IField* (the redeclarations make Name / SymbolKind unambiguous).
    EXPECT_EQ(field->Name(), "count");
    EXPECT_EQ(field->SymbolKind(), SymbolKind::Field);

    // Through IMember* (unambiguous: one IMember subobject; Name via IEntity, SymbolKind via ISymbol).
    IMember* asMember = field;
    EXPECT_EQ(asMember->Name(), "count");
    EXPECT_EQ(asMember->SymbolKind(), SymbolKind::Field);

    // Through IVariable* (unambiguous: one IVariable subobject; Name and SymbolKind via ISymbol).
    IVariable* asVariable = field;
    EXPECT_EQ(asVariable->Name(), "count");
    EXPECT_EQ(asVariable->SymbolKind(), SymbolKind::Field);

    // All three dispatch the SAME final overrider.
    EXPECT_EQ(field->Name(), asMember->Name());
    EXPECT_EQ(asMember->Name(), asVariable->Name());
    EXPECT_EQ(field->SymbolKind(), asMember->SymbolKind());
    EXPECT_EQ(asMember->SymbolKind(), asVariable->SymbolKind());
}

// ---------------------------------------------------------------------------
// IField -- `IMember.ReturnType` and `IVariable.Type` are DIFFERENT names that both denote the
// field's type (the C# `MetadataField` implements both to return the same `IType`). The two
// accessors dispatch through `IField*` unambiguously (each name lives on only one base) and
// return the same configured type by reference (pointer-identity).
// ---------------------------------------------------------------------------
TEST(IFieldTest, ReturnTypeAndTypeAreTheSameFieldType)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    auto fieldType = std::make_shared<KnownType>(KnownTypeCode::String);
    TestField field("name", "C.name", "C.name", "", SymbolKind::Field, compilation,
                    0x04000005u, fieldType, Accessibility::Private,
                    false, false, false, false, false, false, false,
                    /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
                    /*isConst*/ false, std::any());
    // Both accessors return the same field type (pointer-identity reference).
    EXPECT_EQ(&field.ReturnType(), &field.Type());
    EXPECT_EQ(&field.ReturnType(), fieldType.get());
    EXPECT_EQ(field.ReturnType().Name(), "String");
    EXPECT_EQ(field.Type().Name(), "String");
}

// ---------------------------------------------------------------------------
// IField -- polymorphic dispatch through the `IMember*` base pointer reaches the concrete
// accessors of every inherited base (`ISymbol` / `ICompilationProvider` / `INamedElement` /
// `IEntity` / `IMember`) AND the IField-own accessors (the dynamic dispatch the type-system
// paths rely on: `TypeSystemAstBuilder` holds a field as an `IMember*` / `IField` and reads its
// names, kind, return type, readonly-ness, and attributes through the base).
// ---------------------------------------------------------------------------
TEST(IFieldTest, DispatchesPolymorphicallyThroughIMemberPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto fieldType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto declaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    TestField field("count", "C.count", "C.count", "", SymbolKind::Field, compilation,
                    0x04000006u, fieldType, Accessibility::Public,
                    /*isStatic*/ true, /*isAbstract*/ false, /*isSealed*/ false, false,
                    /*isVirtual*/ false, /*isOverride*/ false, /*isOverridable*/ false,
                    /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
                    /*isConst*/ false, std::any());
    field.SetDeclaringType(declaringType);

    IMember* base = &field;
    EXPECT_EQ(base->Name(), "count");
    EXPECT_EQ(base->SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(base->FullName(), "C.count");
    EXPECT_EQ(base->ReflectionName(), "C.count");
    EXPECT_EQ(base->Namespace(), "");
    EXPECT_EQ(&base->Compilation(), &compilation);
    EXPECT_EQ(base->MetadataToken(), 0x04000006u);
    EXPECT_EQ(base->Accessibility(), Accessibility::Public);
    EXPECT_TRUE(base->IsStatic());
    EXPECT_EQ(&base->ReturnType(), fieldType.get());
    EXPECT_EQ(base->ReturnType().Name(), "Int32");
    EXPECT_EQ(base->MemberDefinition(), &field);
    ASSERT_NE(base->DeclaringType(), nullptr);
    EXPECT_EQ(base->DeclaringType()->Kind(), TypeKind::Class);
    EXPECT_EQ(base->DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(base->ParentModule(), nullptr);
}

// ---------------------------------------------------------------------------
// IField -- polymorphic dispatch through the `IVariable*` base pointer reaches the `IVariable`
// surface (`Type` / `IsConst` / `GetConstantValue`) and the inherited `ISymbol` surface
// (`Name` / `SymbolKind`), all of which dispatch to the concrete `TestField` override. An
// `IVariable` IS-A `IVariable` IS-A `ISymbol`; the upcast `IVariable* v = field` is unambiguous
// (one `IVariable` subobject), and `v->SymbolKind()` resolves through the single `ISymbol`
// subobject inside `IVariable` (NOT the ambiguous two-subobject `IField*` path).
// ---------------------------------------------------------------------------
TEST(IFieldTest, DispatchesPolymorphicallyThroughIVariablePointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto owned = std::make_unique<TestField>(
        "count", "C.count", "C.count", "", SymbolKind::Field, compilation, 0x04000007u,
        intType, Accessibility::Private, false, false, false, false, false, false, false,
        /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
        /*isConst*/ true, std::any(std::int32_t(7)));
    IVariable* base = owned.get();
    EXPECT_EQ(base->Name(), "count");
    EXPECT_EQ(base->SymbolKind(), SymbolKind::Field);
    EXPECT_EQ(&base->Type(), intType.get());
    EXPECT_EQ(base->Type().Name(), "Int32");
    EXPECT_TRUE(base->IsConst());
    const auto cv = base->GetConstantValue();
    ASSERT_TRUE(cv.has_value());
    EXPECT_EQ(std::any_cast<std::int32_t>(cv), 7);
    // destroying `owned` runs the `TestField` destructor through the virtual `~IField()`
    // (which chains to `~IMember()` / `~IVariable()` / `~IEntity()` / `~ISymbol()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// IField -- has a virtual destructor (a concrete subclass can be deleted through an `IField*` /
// `IMember*` / `IVariable*` and the derived destructor runs), the established abstract-base
// contract; the multiple-inheritance of `IMember` + `IVariable` (each with its own virtual
// destructor, and each transitively carrying a virtual-destructor `ISymbol` subobject)
// composes correctly despite the two-`ISymbol`-subobject layout. `IField` is abstract.
// ---------------------------------------------------------------------------
TEST(IFieldTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IField>,
        "IField must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IMember>,
        "IMember (a base of IField) must have a virtual destructor");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IVariable>,
        "IVariable (a base of IField) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::IField>,
        "IField must be abstract (every accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::IField>,
        "IField must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(7);
    auto fieldType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    std::unique_ptr<IField> owned = std::make_unique<TestField>(
        "count", "C.count", "C.count", "", SymbolKind::Field, compilation, 0x04000008u,
        fieldType, Accessibility::Public, true, false, false, false, false, false, false,
        /*isReadOnly*/ true, /*returnTypeIsRefReadOnly*/ false, /*isVolatile*/ false,
        /*isConst*/ false, std::any());
    EXPECT_EQ(owned->Name(), "count");
    EXPECT_EQ(owned->SymbolKind(), SymbolKind::Field);
    EXPECT_TRUE(owned->IsReadOnly());
    // destroying `owned` runs the `TestField` destructor through the virtual `~IField()`.
    owned.reset();
    SUCCEED();
}
