// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
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

// Tests for `ITypeDefinition` (cpp/Decompiler/TypeSystem/ITypeDefinition.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/ITypeDefinition.cs). `ITypeDefinition` represents a class,
// enum, interface, struct, delegate, record, or VB module. The C#
// `interface ITypeDefinition : ITypeDefinitionOrUnknown, IType, IEntity` ports to a C++ abstract
// base multiply-inheriting `ITypeDefinitionOrUnknown` (D377) and `IEntity` (D381). The C# lists
// `IType` explicitly, but that listing is REDUNDANT (`ITypeDefinitionOrUnknown : IType` already
// makes `ITypeDefinition` IS-A `IType`); the C++ port OMITS the redundant direct `IType` base
// (the D381 EntityHandle pragmatic-deviation precedent) so `ITypeDefinition` IS-A `IType`
// unambiguously via the single `IType` subobject inside `ITypeDefinitionOrUnknown` and `td` ->
// `const IType&` / `IType*` conversions are clean (a direct-vs-indirect `IType` diamond would
// otherwise make every `IType` conversion ambiguous, forcing consumers to disambiguate).
//
// THE IType-vs-IEntity NAME/REFLECTIONNAME DIAMOND (the genuine diamond that remains, the
// ITypeParameter D383 precedent): `ITypeDefinitionOrUnknown : IType` contributes `IType::Name()` /
// `IType::ReflectionName()`, and `IEntity : ISymbol, ICompilationProvider, INamedElement`
// contributes `IEntity::Name()` (overriding `ISymbol::Name()` / `INamedElement::Name()`) and
// `INamedElement::ReflectionName()`. So `Name` and `ReflectionName` are each inherited via TWO
// independent paths and are ambiguous through an `ITypeDefinition*`; the C++ port REDECLARES
// `Name()` and `ReflectionName()` (the D383 `IType` + `ISymbol` `Name()` diamond precedent). The
// REST of the `IType` surface (`Kind` / `TypeParameterCount` / `StructuralEquals` / `Equals`) is
// inherited UNAMBIGUOUSLY (only via the single `IType` subobject), so `td->Kind()` /
// `td->Equals(other)` / `td` -> `const IType&` compile and dispatch naturally. The test dispatches
// through `ITypeDefinition*`, `ITypeDefinitionOrUnknown*`, `IType*`, `IEntity*`, `ISymbol*`, and
// `INamedElement*` -- every upcast is unambiguous with the redundant direct `IType` base omitted.
//
// The test stub `TestTypeDefinition` derives from the real `ITypeDefinition` and overrides every
// pure-virtual across the `IType` / `ITypeDefinitionOrUnknown` / `ISymbol` / `INamedElement` /
// `ICompilationProvider` / `IEntity` / `ITypeDefinition`-own surface, the shape a real
// `MetadataTypeDefinition` takes. `Name()` / `ReflectionName()` / `TypeParameterCount()` derive
// from the stored `FullTypeName` (the `ITypeDefinitionOrUnknown_Test` convention); `FullName()` /
// `Namespace()` / `MetadataName()` are separate configured strings. The non-virtual
// `Equals(const IType&)` is inherited from `IType` (unambiguous, single `IType` subobject; NOT
// redeclared) and delegates to the overridden `Kind()` / `StructuralEquals()`.
//
// The `ICompilation` stand-in is IDENTICAL to the stand-in in the other Phase-5 test files
// (ODR-safe across translation units); `IAttribute` is the real port (D386), included below. The
// member-family snapshot element types (`IMember` / `IField` / `IMethod` / `IProperty` / `IEvent`)
// are forward-declared by `ITypeDefinition.hpp` and complete via the includes below, but the
// snapshot tests use `NestedTypes` (self-referential `ITypeDefinition*`) for the non-empty
// passthrough check and empty-by-default for the member-family snapshots (the build verifies the
// accessors exist with the right `std::vector<const T*>` signature; the `NestedTypes` non-empty
// check verifies the snapshot-passthrough mechanism, identical for the other five).

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/ITypeDefinitionOrUnknown.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

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
// by reference, `Constructor` is null, `HasDecodeErrors` is false, the argument vectors are empty);
// the test-specific `Kind()` accessor and `KnownAttribute kind_` member are kept so the
// `HasAttribute` / `GetAttribute` tests (which classify attributes by `KnownAttribute`) work.
// IDENTICAL in shape to the `TestAttribute` in the other member-family test files.
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

// A minimal concrete `ITypeDefinition` for testing: holds the configured state and returns it
// from every accessor (the shape a real `MetadataTypeDefinition` takes). `Name()` /
// `ReflectionName()` / `TypeParameterCount()` derive from the stored `FullTypeName` (the
// `ITypeDefinitionOrUnknown_Test` convention); `FullName()` / `Namespace()` / `MetadataName()`
// are separate configured strings. The single `Name()` / `ReflectionName()` overrides are the
// final overrider for the `IType`-vs-`IEntity` diamond (the `ITypeDefinition` redeclarations of
// `Name` / `ReflectionName` disambiguate lookup; a single override is the final overrider for
// both paths) -- the `IType` surface (`Kind` / `TypeParameterCount` / `StructuralEquals` /
// `Equals`) is inherited unambiguously (single `IType` subobject via `ITypeDefinitionOrUnknown`,// the redundant direct `IType` base omitted). The return types `SymbolKind` / `KnownTypeCode` /
// `FullTypeName` / `ExtensionInfo` / `Accessibility` / `Nullability` are GLOBALLY QUALIFIED
// because the stub INHERITS the same-named member functions (via `ISymbol` / `ITypeDefinition` /
// `ITypeDefinitionOrUnknown` / `IEntity`), which hide the namespace-scope enums/classes in the
// stub's class body (the D372 cross-scope name-hiding crux). The non-virtual `Equals(const
// IType&)` is inherited from `IType` (unambiguous, single `IType` subobject; NOT redeclared) and
// delegates to the overridden `Kind()` / `StructuralEquals()`.
class TestTypeDefinition : public ILSpy::Decompiler::TypeSystem::ITypeDefinition {
public:
    TestTypeDefinition(std::string fullName,
                       std::string ns,
                       std::string metadataName,
                       ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName,
                       ILSpy::Decompiler::TypeSystem::TypeKind typeKind,
                       ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind,
                       const TestCompilation& compilation,
                       std::uint32_t metadataToken,
                       ILSpy::Decompiler::TypeSystem::KnownTypeCode knownTypeCode,
                       ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
                       ILSpy::Decompiler::TypeSystem::Nullability nullableContext,
                       bool isStatic, bool isAbstract, bool isSealed,
                       bool isReadOnly, bool hasExtensions, bool isRecord)
        : fullName_(std::move(fullName)), namespace_(std::move(ns)),
          metadataName_(std::move(metadataName)), fullTypeName_(std::move(fullTypeName)),
          typeKind_(typeKind), symbolKind_(symbolKind), compilation_(compilation),
          metadataToken_(metadataToken), knownTypeCode_(knownTypeCode),
          accessibility_(accessibility), nullableContext_(nullableContext),
          isStatic_(isStatic), isAbstract_(isAbstract), isSealed_(isSealed),
          isReadOnly_(isReadOnly), hasExtensions_(hasExtensions), isRecord_(isRecord) {}

    // --- IType (inherited unambiguously; only Name/ReflectionName are redeclared in ITypeDefinition) ---
    ILSpy::Decompiler::TypeSystem::TypeKind Kind() const override { return typeKind_; }
    // The single `Name()` / `ReflectionName()` override is the final overrider for the
    // IType-vs-IEntity diamond (the `ITypeDefinition` redeclarations); `Kind()` /
    // `TypeParameterCount()` are the inherited `IType` pure-virtuals (single `IType` subobject).
    std::string Name() const override { return fullTypeName_.Name(); }
    // The single `ReflectionName()` override is the final overrider for the IType-vs-INamedElement
    // diamond (the `ITypeDefinition` redeclaration); `TypeParameterCount()` is the inherited `IType`
    // pure-virtual (single `IType` subobject).
    std::string ReflectionName() const override { return fullTypeName_.ReflectionName(); }
    int TypeParameterCount() const override { return fullTypeName_.TypeParameterCount(); }
    // `Equals(const IType&)` is the NON-virtual `IType::Equals` (inherited unambiguously, single
    // `IType` subobject; NOT redeclared in `ITypeDefinition`); it delegates to the overridden
    // `Kind()` / `StructuralEquals()`.

    // --- ITypeDefinitionOrUnknown ---
    const ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override { return fullTypeName_; }

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return symbolKind_; }

    // --- INamedElement ---
    std::string FullName() const override { return fullName_; }
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
        return declaringTypeDefinition_;
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

    // --- ITypeDefinition-own ---
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> NestedTypes() const override
    {
        return nestedTypes_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> Members() const override
    {
        return members_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IField*> Fields() const override
    {
        return fields_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> Methods() const override
    {
        return methods_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> Properties() const override
    {
        return properties_;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> Events() const override
    {
        return events_;
    }
    ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode() const override { return knownTypeCode_; }
    ILSpy::Decompiler::TypeSystem::ITypePtr EnumUnderlyingType() const override { return enumUnderlyingType_; }
    bool IsReadOnly() const override { return isReadOnly_; }
    std::string MetadataName() const override { return metadataName_; }
    bool HasExtensions() const override { return hasExtensions_; }
    const ILSpy::Decompiler::TypeSystem::ExtensionInfo* ExtensionInfo() const override { return extensionInfo_; }
    ILSpy::Decompiler::TypeSystem::Nullability NullableContext() const override { return nullableContext_; }
    bool IsRecord() const override { return isRecord_; }

    // Test wiring (set the nullable / collection slots after construction).
    void SetDeclaringTypeDefinition(const ILSpy::Decompiler::TypeSystem::ITypeDefinition* d) { declaringTypeDefinition_ = d; }
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }
    void SetEnumUnderlyingType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { enumUnderlyingType_ = std::move(t); }
    void SetExtensionInfo(const ILSpy::Decompiler::TypeSystem::ExtensionInfo* e) { extensionInfo_ = e; }
    void AddAttribute(const ILSpy::Decompiler::TypeSystem::IAttribute* a) { attributes_.push_back(a); }
    void SetNestedTypes(std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> v) { nestedTypes_ = std::move(v); }
    void SetMembers(std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> v) { members_ = std::move(v); }
    void SetFields(std::vector<const ILSpy::Decompiler::TypeSystem::IField*> v) { fields_ = std::move(v); }
    void SetMethods(std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> v) { methods_ = std::move(v); }
    void SetProperties(std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> v) { properties_ = std::move(v); }
    void SetEvents(std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> v) { events_ = std::move(v); }

protected:
    bool StructuralEquals(const ILSpy::Decompiler::TypeSystem::IType& other) const override {
        const auto& o = static_cast<const TestTypeDefinition&>(other);
        return typeKind_ == o.typeKind_ && fullTypeName_ == o.fullTypeName_;
    }

private:
    std::string fullName_, namespace_, metadataName_;
    ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    ILSpy::Decompiler::TypeSystem::TypeKind typeKind_;
    ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind_;
    const TestCompilation& compilation_;
    std::uint32_t metadataToken_;
    ILSpy::Decompiler::TypeSystem::KnownTypeCode knownTypeCode_;
    ILSpy::Decompiler::TypeSystem::Accessibility accessibility_;
    ILSpy::Decompiler::TypeSystem::Nullability nullableContext_;
    bool isStatic_, isAbstract_, isSealed_;
    bool isReadOnly_, hasExtensions_, isRecord_;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declaringTypeDefinition_ = nullptr;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
    ILSpy::Decompiler::TypeSystem::ITypePtr enumUnderlyingType_;
    const ILSpy::Decompiler::TypeSystem::ExtensionInfo* extensionInfo_ = nullptr;
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> attributes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> nestedTypes_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> members_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IField*> fields_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> methods_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> properties_;
    std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> events_;
};

} // namespace

// ---------------------------------------------------------------------------
// ITypeDefinition -- the ITypeDefinition-own scalar accessors return the configured values: the
// `KnownTypeCode` (None for a non-known type), `EnumUnderlyingType` (null for a non-enum),
// `IsReadOnly` (readonly struct), `MetadataName` (short name WITH the arity backtick suffix),
// `HasExtensions`, `NullableContext`, and `IsRecord` flags.
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionTest, OwnScalarAccessorsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(1);
    TestTypeDefinition td(
        /*fullName*/ "System.Collections.Generic.Dictionary", /*ns*/ "System.Collections.Generic",
        /*metadataName*/ "Dictionary`2",
        FullTypeName("System.Collections.Generic.Dictionary`2"), TypeKind::Class,
        SymbolKind::TypeDefinition, compilation, 0x02000001u, KnownTypeCode::None,
        Accessibility::Public, Nullability::NotNullable,
        /*isStatic*/ false, /*isAbstract*/ false, /*isSealed*/ true,
        /*isReadOnly*/ false, /*hasExtensions*/ false, /*isRecord*/ false);
    EXPECT_EQ(td.KnownTypeCode(), KnownTypeCode::None);
    EXPECT_EQ(td.EnumUnderlyingType(), nullptr);
    EXPECT_FALSE(td.IsReadOnly());
    EXPECT_EQ(td.MetadataName(), "Dictionary`2");
    EXPECT_FALSE(td.HasExtensions());
    EXPECT_EQ(td.NullableContext(), Nullability::NotNullable);
    EXPECT_FALSE(td.IsRecord());

    // An enum has an underlying type and a different kind.
    TestTypeDefinition enumTd(
        "Foo", "", "Foo", FullTypeName("Foo"), TypeKind::Enum, SymbolKind::TypeDefinition,
        compilation, 0x02000002u, KnownTypeCode::None, Accessibility::Public, Nullability::Oblivious,
        false, false, false, false, false, false);
    auto intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    enumTd.SetEnumUnderlyingType(intType);
    EXPECT_EQ(enumTd.Kind(), TypeKind::Enum);
    ASSERT_NE(enumTd.EnumUnderlyingType(), nullptr);
    EXPECT_EQ(enumTd.EnumUnderlyingType()->Name(), "Int32");
    EXPECT_EQ(enumTd.NullableContext(), Nullability::Oblivious);

    // A record struct is read-only and a record.
    TestTypeDefinition recordStruct(
        "Point", "", "Point", FullTypeName("Point"), TypeKind::Struct, SymbolKind::TypeDefinition,
        compilation, 0x02000003u, KnownTypeCode::None, Accessibility::Public, Nullability::NotNullable,
        false, false, false,
        /*isReadOnly*/ true, /*hasExtensions*/ false, /*isRecord*/ true);
    EXPECT_TRUE(recordStruct.IsReadOnly());
    EXPECT_TRUE(recordStruct.IsRecord());
}

// ---------------------------------------------------------------------------
// ITypeDefinition -- the member-family snapshots (`NestedTypes` / `Members` / `Fields` /
// `Methods` / `Properties` / `Events`) return the configured non-owning pointer snapshots. The
// `NestedTypes` snapshot is self-referential (`const ITypeDefinition*` to other type definitions)
// and is checked non-empty with pointer identity; the other five are empty by default (the build
// verifies the accessors exist with the right `std::vector<const T*>` signature, and the
// `NestedTypes` non-empty check verifies the identical snapshot-passthrough mechanism).
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionTest, MemberFamilySnapshotsReturnConfiguredValues)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(2);
    TestTypeDefinition outer(
        "Outer", "", "Outer", FullTypeName("Outer"), TypeKind::Class, SymbolKind::TypeDefinition,
        compilation, 0x02000010u, KnownTypeCode::None, Accessibility::Public, Nullability::NotNullable,
        false, false, false, false, false, false);
    TestTypeDefinition nestedA(
        "Outer.InnerA", "", "Outer.InnerA", FullTypeName("Outer+InnerA"), TypeKind::Class,
        SymbolKind::TypeDefinition, compilation, 0x02000011u, KnownTypeCode::None,
        Accessibility::Private, Nullability::NotNullable, false, false, false, false, false, false);
    TestTypeDefinition nestedB(
        "Outer.InnerB", "", "Outer.InnerB", FullTypeName("Outer+InnerB"), TypeKind::Struct,
        SymbolKind::TypeDefinition, compilation, 0x02000012u, KnownTypeCode::None,
        Accessibility::Private, Nullability::NotNullable, false, false, false, false, false, false);
    outer.SetNestedTypes({&nestedA, &nestedB});

    // NestedTypes: non-empty self-referential snapshot (pointer identity + size + reading through
    // the snapshot).
    const auto nested = outer.NestedTypes();
    ASSERT_EQ(nested.size(), 2u);
    EXPECT_EQ(nested[0], &nestedA);
    EXPECT_EQ(nested[1], &nestedB);
    EXPECT_EQ(nested[0]->Name(), "InnerA");
    EXPECT_EQ(nested[1]->Kind(), TypeKind::Struct);

    // The other five member-family snapshots are empty by default (the build verifies they
    // exist with the right signature; the snapshot-passthrough mechanism is verified for
    // NestedTypes above and is identical for the other five).
    EXPECT_TRUE(outer.Members().empty());
    EXPECT_TRUE(outer.Fields().empty());
    EXPECT_TRUE(outer.Methods().empty());
    EXPECT_TRUE(outer.Properties().empty());
    EXPECT_TRUE(outer.Events().empty());
}

// ---------------------------------------------------------------------------
// ITypeDefinition -- THE IType-vs-IEntity NAME/REFLECTIONNAME DIAMOND: the single `Name()` /
// `ReflectionName()` overrides are the final overrider for both paths of the IType-vs-IEntity
// diamond (`IType::Name()` / `IType::ReflectionName()` via `ITypeDefinitionOrUnknown` AND
// `IEntity::Name()` / `INamedElement::ReflectionName()` via `IEntity`), so dispatch through
// `ITypeDefinition*`, `ITypeDefinitionOrUnknown*`, `IType*`, and `IEntity*` (each an unambiguous
// upcast -- the redundant direct `IType` base is omitted, so `IType*` is unambiguous via the
// single `IType` subobject) all reach the SAME override and return the same value. The inherited
// non-virtual `Equals(const IType&)` (unambiguous, single `IType` subobject; NOT redeclared)
// delegates to the overridden `Kind()` / `StructuralEquals()`. The `FullTypeName` (inherited
// unambiguously from `ITypeDefinitionOrUnknown`) dispatches too.
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionTest, ITypeVsEntityDiamondNameAndReflectionNameDispatchUnambiguously)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(3);
    auto owned = std::make_unique<TestTypeDefinition>(
        "System.Collections.Generic.Dictionary", "System.Collections.Generic",
        "Dictionary`2", FullTypeName("System.Collections.Generic.Dictionary`2"), TypeKind::Class,
        SymbolKind::TypeDefinition, compilation, 0x02000020u, KnownTypeCode::None,
        Accessibility::Public, Nullability::NotNullable, false, false, false, false, false, false);
    TestTypeDefinition* td = owned.get();

    // Through ITypeDefinition*: `Name` / `ReflectionName` are redeclared here to disambiguate
    // the IType-vs-IEntity diamond; the rest of the IType surface (`Kind` / `TypeParameterCount`
    // / `Equals` / `StructuralEquals`) is inherited unambiguously (single IType subobject).
    EXPECT_EQ(td->Kind(), TypeKind::Class);
    EXPECT_EQ(td->Name(), "Dictionary");
    EXPECT_EQ(td->ReflectionName(), "System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(td->TypeParameterCount(), 2);

    // Through ITypeDefinitionOrUnknown* (the IType base view -- ITypeDefinition IS-A IType via
    // the single IType subobject inside ITypeDefinitionOrUnknown; the redundant direct IType
    // base is omitted, so this upcast is unambiguous).
    ITypeDefinitionOrUnknown* asDefOrUnknown = td;
    EXPECT_EQ(asDefOrUnknown->Kind(), TypeKind::Class);
    EXPECT_EQ(asDefOrUnknown->Name(), "Dictionary");
    EXPECT_EQ(asDefOrUnknown->ReflectionName(), "System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(asDefOrUnknown->TypeParameterCount(), 2);
    EXPECT_EQ(asDefOrUnknown->FullTypeName().ReflectionName(), "System.Collections.Generic.Dictionary`2");

    // Through IType* directly (unambiguous -- the redundant direct IType base is OMITTED, so
    // there is a single IType subobject and the IType* upcast is clean; a literal mirror of the
    // C# base list would make this upcast ambiguous).
    IType* asIType = td;
    EXPECT_EQ(asIType->Kind(), TypeKind::Class);
    EXPECT_EQ(asIType->Name(), "Dictionary");
    EXPECT_EQ(asIType->ReflectionName(), "System.Collections.Generic.Dictionary`2");
    EXPECT_EQ(asIType->TypeParameterCount(), 2);

    // Through IEntity* (unambiguous: one IEntity subobject).
    IEntity* asEntity = td;
    EXPECT_EQ(asEntity->Name(), "Dictionary");
    EXPECT_EQ(asEntity->SymbolKind(), SymbolKind::TypeDefinition);

    // All four dispatch the SAME final overrider for Name (the single Name() override is the
    // final overrider for the IType and IEntity/ISymbol/INamedElement paths).
    EXPECT_EQ(td->Name(), asDefOrUnknown->Name());
    EXPECT_EQ(asDefOrUnknown->Name(), asIType->Name());
    EXPECT_EQ(asIType->Name(), asEntity->Name());

    // The inherited non-virtual Equals(const IType&) (unambiguous, single IType subobject; NOT
    // redeclared) delegates to the overridden Kind() / StructuralEquals(). A same-kind,
    // same-full-name type is structurally equal; a different-kind type is not (Kind mismatch
    // short-circuits before StructuralEquals). Passing a TestTypeDefinition as `const IType&` is
    // unambiguous (single IType subobject -- the ergonomic win of omitting the redundant direct
    // IType base).
    TestTypeDefinition twin(
        "System.Collections.Generic.Dictionary", "System.Collections.Generic",
        "Dictionary`2", FullTypeName("System.Collections.Generic.Dictionary`2"), TypeKind::Class,
        SymbolKind::TypeDefinition, compilation, 0x02000021u, KnownTypeCode::None,
        Accessibility::Public, Nullability::NotNullable, false, false, false, false, false, false);
    TestTypeDefinition otherKind(
        "System.Collections.Generic.List", "System.Collections.Generic",
        "List`1", FullTypeName("System.Collections.Generic.List`1"), TypeKind::Struct,
        SymbolKind::TypeDefinition, compilation, 0x02000022u, KnownTypeCode::None,
        Accessibility::Public, Nullability::NotNullable, false, false, false, false, false, false);
    EXPECT_TRUE(td->Equals(twin));
    EXPECT_FALSE(td->Equals(otherKind));
}

// ---------------------------------------------------------------------------
// ITypeDefinition -- the inherited `IEntity` accessors dispatch through the `ITypeDefinition*`:
// `GetAttributes` / `HasAttribute` / `GetAttribute` (the attribute family), `MetadataToken`,
// `DeclaringTypeDefinition`, `DeclaringType`, `ParentModule`, `Accessibility`, `IsStatic`,
// `IsAbstract`, `IsSealed`, and `SymbolKind`. The `ITypeDefinition`-own accessors are reachable
// through `ITypeDefinition*` but NOT through `IEntity*` (a plain `IEntity` has no `KnownTypeCode` /
// `IsRecord` / ...).
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionTest, InheritedEntityAccessorsDispatchThroughITypeDefinitionPointer)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(4);
    TestTypeDefinition outer(
        "Outer", "", "Outer", FullTypeName("Outer"), TypeKind::Class, SymbolKind::TypeDefinition,
        compilation, 0x02000030u, KnownTypeCode::None, Accessibility::Public, Nullability::NotNullable,
        /*isStatic*/ false, /*isAbstract*/ true, /*isSealed*/ true, false, false, false);
    TestTypeDefinition inner(
        "Outer.Inner", "", "Outer.Inner", FullTypeName("Outer+Inner"), TypeKind::Class,
        SymbolKind::TypeDefinition, compilation, 0x02000031u, KnownTypeCode::None,
        Accessibility::Private, Nullability::NotNullable, false, false, false, false, false, false);
    auto declaringType = std::make_shared<KnownType>(KnownTypeCode::Object);
    inner.SetDeclaringTypeDefinition(&outer);
    inner.SetDeclaringType(declaringType);
    TestAttribute attr(KnownAttribute::Serializable);
    inner.AddAttribute(&attr);

    EXPECT_EQ(inner.SymbolKind(), SymbolKind::TypeDefinition);
    EXPECT_EQ(inner.MetadataToken(), 0x02000031u);
    EXPECT_EQ(inner.DeclaringTypeDefinition(), &outer);
    ASSERT_NE(inner.DeclaringType(), nullptr);
    EXPECT_EQ(inner.DeclaringType()->Kind(), TypeKind::Class);
    EXPECT_EQ(inner.ParentModule(), nullptr);
    EXPECT_EQ(inner.Accessibility(), Accessibility::Private);
    EXPECT_FALSE(inner.IsStatic());
    EXPECT_TRUE(outer.IsAbstract());
    EXPECT_TRUE(outer.IsSealed());
    EXPECT_TRUE(inner.HasAttribute(KnownAttribute::Serializable));
    EXPECT_FALSE(inner.HasAttribute(KnownAttribute::Obsolete));
    EXPECT_EQ(inner.GetAttribute(KnownAttribute::Serializable), &attr);
    EXPECT_EQ(inner.GetAttribute(KnownAttribute::Obsolete), nullptr);
    const auto attrs = inner.GetAttributes();
    ASSERT_EQ(attrs.size(), 1u);
    EXPECT_EQ(attrs[0], &attr);
}

// ---------------------------------------------------------------------------
// ITypeDefinition -- polymorphic dispatch through the unambiguous base pointers
// (`ITypeDefinitionOrUnknown*` / `IType*` / `IEntity*` / `ISymbol*` / `INamedElement*` /
// `ICompilationProvider*`), each of which appears once in the inheritance graph (the redundant
// direct `IType` base is omitted, so `IType*` is unambiguous -- reached via the single `IType`
// subobject inside `ITypeDefinitionOrUnknown`). The `ITypeDefinition`-own accessors are reachable
// through `ITypeDefinition*` but NOT through the base pointers.
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionTest, DispatchesPolymorphicallyThroughBasePointers)
{
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(5);
    auto owned = std::make_unique<TestTypeDefinition>(
        "System.String", "System", "String", FullTypeName("System.String"), TypeKind::Class,
        SymbolKind::TypeDefinition, compilation, 0x02000040u, KnownTypeCode::String,
        Accessibility::Public, Nullability::NotNullable, false, false, false, false, false, false);

    ITypeDefinitionOrUnknown* asDefOrUnknown = owned.get();
    EXPECT_EQ(asDefOrUnknown->Name(), "String");
    EXPECT_EQ(asDefOrUnknown->Kind(), TypeKind::Class);
    EXPECT_EQ(asDefOrUnknown->FullTypeName().ReflectionName(), "System.String");

    // The IType* upcast is unambiguous (single IType subobject via ITypeDefinitionOrUnknown).
    IType* asIType = owned.get();
    EXPECT_EQ(asIType->Name(), "String");
    EXPECT_EQ(asIType->Kind(), TypeKind::Class);
    EXPECT_EQ(asIType->ReflectionName(), "System.String");
    EXPECT_EQ(asIType->TypeParameterCount(), 0);

    IEntity* asEntity = owned.get();
    EXPECT_EQ(asEntity->Name(), "String");
    EXPECT_EQ(asEntity->FullName(), "System.String");
    EXPECT_EQ(asEntity->Namespace(), "System");
    EXPECT_EQ(asEntity->MetadataToken(), 0x02000040u);

    ISymbol* asSymbol = owned.get();
    EXPECT_EQ(asSymbol->Name(), "String");
    EXPECT_EQ(asSymbol->SymbolKind(), SymbolKind::TypeDefinition);

    INamedElement* asNamed = owned.get();
    EXPECT_EQ(asNamed->ReflectionName(), "System.String");
    EXPECT_EQ(asNamed->Namespace(), "System");

    ICompilationProvider* asCompProvider = owned.get();
    EXPECT_EQ(&asCompProvider->Compilation(), &compilation);

    // destroying `owned` runs the `TestTypeDefinition` destructor through the virtual
    // `~ITypeDefinition()` (which chains to `~ITypeDefinitionOrUnknown()` / `~IType()` /
    // `~IEntity()` / ...).
    owned.reset();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// ITypeDefinition -- has a virtual destructor (a concrete subclass can be deleted through an
// `ITypeDefinition*` / `ITypeDefinitionOrUnknown*` / `IType*` / `IEntity*` / `ISymbol*` and the
// derived destructor runs), the established abstract-base contract; the multiple-inheritance of
// `ITypeDefinitionOrUnknown` (which `: IType`) + `IEntity` (each with its own virtual destructor)
// composes correctly -- the redundant direct `IType` base is omitted, so there is a single
// `IType` subobject (via `ITypeDefinitionOrUnknown`). `ITypeDefinition` is abstract (every own
// accessor is pure-virtual) and polymorphic.
// ---------------------------------------------------------------------------
TEST(ITypeDefinitionTest, HasVirtualDestructorAndIsAbstract)
{
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ITypeDefinition>,
        "ITypeDefinition must have a virtual destructor for abstract-base deletion");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::ITypeDefinitionOrUnknown>,
        "ITypeDefinitionOrUnknown (a base of ITypeDefinition) must have a virtual destructor");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IType>,
        "IType (a base of ITypeDefinition via ITypeDefinitionOrUnknown) must have a virtual destructor");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::TypeSystem::IEntity>,
        "IEntity (a base of ITypeDefinition) must have a virtual destructor");
    static_assert(std::is_abstract_v<ILSpy::Decompiler::TypeSystem::ITypeDefinition>,
        "ITypeDefinition must be abstract (every own accessor is pure-virtual)");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::TypeSystem::ITypeDefinition>,
        "ITypeDefinition must be polymorphic");
    using namespace ILSpy::Decompiler::TypeSystem;
    TestCompilation compilation(6);
    std::unique_ptr<ITypeDefinition> owned = std::make_unique<TestTypeDefinition>(
        "Foo", "", "Foo", FullTypeName("Foo"), TypeKind::Class, SymbolKind::TypeDefinition,
        compilation, 0x02000050u, KnownTypeCode::None, Accessibility::Public, Nullability::Oblivious,
        false, false, false, false, false, false);
    EXPECT_EQ(owned->Name(), "Foo");
    EXPECT_EQ(owned->Kind(), TypeKind::Class);
    EXPECT_EQ(owned->MetadataName(), "Foo");
    EXPECT_EQ(owned->KnownTypeCode(), KnownTypeCode::None);
    // destroying `owned` runs the `TestTypeDefinition` destructor through the virtual
    // `~ITypeDefinition()` (chaining to `~ITypeDefinitionOrUnknown()` / `~IType()` / `~IEntity()` / ...).
    owned.reset();
    SUCCEED();
}
