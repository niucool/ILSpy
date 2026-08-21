// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT ANY WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `GetDefinition()` accessor on the minimal-port `IType` hierarchy
// (faithful port of IType.cs `ITypeDefinition? GetDefinition()`). The C# interface
// declares this `abstract` (no AbstractType default); the minimal port makes it
// virtual-WITH-DEFAULT `nullptr` (the D406 flattened-AbstractType convention,
// mirroring the C# AbstractType.GetDefinition() `return null`) so the C++-only
// minimal types and the not-yet-concrete interfaces inherit it without a big-bang
// churn. The delegating decorators (ModifiedType / NullabilityAnnotatedType /
// ParameterizedType / TupleType) override it to forward to their element / generic
// / underlying / base type; the C++-only minimal types (KnownType / SimpleType /
// SpecialType / UnknownType / TypeParameter) and the array / pointer / by-reference
// / function-pointer types inherit the `nullptr` default.
//
// This is the prerequisite leaf the D458 next-in-order analysis flagged for
// `IsObjectOrValueType` (TypeSystemAstBuilder.cs line 2736), whose body is
// `d = type.GetDefinition(); return d != null && (d.KnownTypeCode == Object ||
// d.KnownTypeCode == ValueType)` -- it reads `GetDefinition()` then
// `ITypeDefinition.KnownTypeCode`, both now ported. The consumer lands in a
// follow-up iteration; these tests pin the prerequisite surface.
//
// The non-null delegation cases wrap a `TestDefinition` (a compact concrete
// `ITypeDefinition` stub) in a decorator and assert the decorator's
// `GetDefinition()` returns the SAME pointer the wrapped definition's
// `GetDefinition()` returns (pointer-identity, the C# reference-equality). The
// `TestDefinition` overrides `GetDefinition()` to return `this` -- the faithful
// `MetadataTypeDefinition.IType.GetDefinition() => this` pattern (a resolved type
// definition IS an `ITypeDefinition`, so its own definition is itself).

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeDefinitionOrUnknown.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameter;
using ILSpy::Decompiler::TypeSystem::UnknownType;
using ILSpy::Decompiler::TypeSystem::Version;
using ILSpy::Decompiler::Util::CacheManager;

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider`
// base of `TestDefinition` can return a compilation (the D379 / ITypeDefinition_Test
// test stand-in pattern; the shared `TestSupport::TestModule` backs `MainModule`).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}

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
        return StringComparer::Ordinal();
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
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A compact concrete `ITypeDefinition` stub -- the shape a real
// `MetadataTypeDefinition` takes for the `GetDefinition` surface. It overrides
// `GetDefinition()` to return `this` (the faithful `MetadataTypeDefinition` =>
// `this` pattern: a resolved type definition IS an `ITypeDefinition`, so its own
// definition is itself). The accessors the `GetDefinition` tests do NOT read return
// trivial defaults (empty snapshots / null pointers / `false`); `Name()` /
// `ReflectionName()` / `TypeParameterCount()` derive from the stored `FullTypeName`
// (the ITypeDefinitionOrUnknown_Test convention), and `KnownTypeCode()` is
// configurable so the SAME stub will back the `IsObjectOrValueType` consumer tests
// in the follow-up iteration.
class TestDefinition : public ITypeDefinition {
public:
    // The ctor parameter types are fully-qualified to dodge the D393/D411 cross-scope
    // name-hiding crux: the inherited accessors `FullTypeName()` (ITypeDefinitionOrUnknown),
    // `SymbolKind()` (ISymbol), `Accessibility()` (IEntity), `KnownTypeCode()` (ITypeDefinition)
    // are member functions that hide the same-named namespace-scope types in the derived
    // class body (including the ctor parameter list and the override return types).
    TestDefinition(::ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName,
                    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode knownTypeCode,
                    TypeKind typeKind, const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : fullTypeName_(std::move(fullTypeName)),
          knownTypeCode_(knownTypeCode),
          typeKind_(typeKind),
          compilation_(compilation) {}

    // --- IType (inherited unambiguously; only Name/ReflectionName are redeclared) ---
    TypeKind Kind() const override { return typeKind_; }
    std::string Name() const override { return fullTypeName_.Name(); }
    std::string ReflectionName() const override { return fullTypeName_.ReflectionName(); }
    int TypeParameterCount() const override { return fullTypeName_.TypeParameterCount(); }

    // --- ITypeDefinitionOrUnknown ---
    const ::ILSpy::Decompiler::TypeSystem::FullTypeName& FullTypeName() const override { return fullTypeName_; }

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition; }

    // --- INamedElement ---
    std::string FullName() const override { return fullTypeName_.ReflectionName(); }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(
        ILSpy::Decompiler::TypeSystem::KnownAttribute) const override
    {
        return nullptr;
    }
    ::ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- ITypeDefinition-own ---
    std::vector<const ITypeDefinition*> NestedTypes() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> Members() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IField*> Fields() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> Methods() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> Properties() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IEvent*> Events() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode KnownTypeCode() const override { return knownTypeCode_; }
    ITypePtr EnumUnderlyingType() const override { return {}; }
    bool IsReadOnly() const override { return false; }
    std::string MetadataName() const override { return fullTypeName_.ReflectionName(); }
    bool HasExtensions() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::ExtensionInfo* ExtensionInfo() const override
    {
        return nullptr;
    }
    Nullability NullableContext() const override { return Nullability::Oblivious; }
    bool IsRecord() const override { return false; }

    // --- The load-bearing override: a resolved type definition IS its own definition ---
    // (the C# `MetadataTypeDefinition IType.GetDefinition() => this`).
    const ITypeDefinition* GetDefinition() const override { return this; }

protected:
    bool StructuralEquals(const IType& other) const override {
        const auto& o = static_cast<const TestDefinition&>(other);
        return fullTypeName_ == o.fullTypeName_ && knownTypeCode_ == o.knownTypeCode_;
    }

private:
    ::ILSpy::Decompiler::TypeSystem::FullTypeName fullTypeName_;
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode knownTypeCode_;
    TypeKind typeKind_;
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
};

// A custom-modifier type (System.Runtime.CompilerServices.IsConst), used as the
// `modifier` arg of `ModifiedType` (its `GetDefinition` delegates to the *element*,
// not the modifier) -- the IsReferenceType_Test convention.
ITypePtr IsConstModifier() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System.Runtime.CompilerServices", "IsConst"));
}

} // namespace

// ---- Default: the C++-only minimal types inherit the nullptr default ----

TEST(GetDefinitionTest, KnownTypeInheritsNullDefault) {
    auto obj = std::make_shared<KnownType>(KnownTypeCode::Object);
    EXPECT_EQ(obj->GetDefinition(), nullptr);
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_EQ(int32->GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, SimpleTypeInheritsNullDefault) {
    auto simple = std::make_shared<SimpleType>(TopLevelTypeName("System", "Object"));
    EXPECT_EQ(simple->GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, SpecialTypeInheritsNullDefault) {
    auto special = std::make_shared<SpecialType>(TypeKind::Unknown, "Unknown");
    EXPECT_EQ(special->GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, UnknownTypeInheritsNullDefault) {
    // The elaborated-type-specifier targets the class (not the UnknownType() helper);
    // the namespace-unknown form (std::nullopt ns) is the Resolve-fallback shape.
    class UnknownType u(std::nullopt, "?", 0);
    EXPECT_EQ(u.GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, TypeParameterInheritsNullDefault) {
    TypeParameter tp(0, TypeParameter::OwnerKind::Class, "T");
    EXPECT_EQ(tp.GetDefinition(), nullptr);
}

// ---- Default: the array / pointer / by-reference / function-pointer types ----
// inherit the nullptr default (the C# AbstractType.GetDefinition() `return null`)

TEST(GetDefinitionTest, ArrayTypeInheritsNullDefault) {
    auto sz = std::make_shared<ArrayType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    EXPECT_EQ(sz->GetDefinition(), nullptr);
    auto multi = std::make_shared<ArrayType>(std::make_shared<KnownType>(KnownTypeCode::Int32), 2);
    EXPECT_EQ(multi->GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, ByReferenceTypeInheritsNullDefault) {
    auto br = std::make_shared<ByReferenceType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    EXPECT_EQ(br->GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, PointerTypeInheritsNullDefault) {
    auto ptr = std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    EXPECT_EQ(ptr->GetDefinition(), nullptr);
}

TEST(GetDefinitionTest, FunctionPointerTypeInheritsNullDefault) {
    // The minimal port assumes TypeSystemOptions.FunctionPointers is enabled (the D404
    // module-field deferral), so GetDefinition returns null (the C# common case); the
    // UIntPtr-alias fallback stays deferred with the module field.
    FunctionPointerType fpt(
        SignatureCallingConvention::Default, /*customCallingConventions*/ {},
        std::make_shared<KnownType>(KnownTypeCode::Int32), /*returnIsRefReadOnly*/ false,
        /*parameterTypes*/ {}, /*parameterReferenceKinds*/ {});
    EXPECT_EQ(fpt.GetDefinition(), nullptr);
}

// ---- ModifiedType: delegates to the decorated element type ----

TEST(GetDefinitionTest, ModifiedTypeDelegatesToElement) {
    TestCompilation compilation;
    TestDefinition objectDef(FullTypeName("System.Object"), KnownTypeCode::Object,
                              TypeKind::Class, compilation);
    auto objectDefPtr = std::shared_ptr<TestDefinition>(&objectDef, [](TestDefinition*) {});
    auto mod = std::make_shared<ModifiedType>(IsConstModifier(), objectDefPtr, /*isRequired*/ false);
    // The modifier's GetDefinition delegates to the ELEMENT's GetDefinition (the
    // TestDefinition's `this`), NOT the modifier's own (a SimpleType -> nullptr).
    EXPECT_EQ(mod->GetDefinition(), static_cast<const ITypeDefinition*>(&objectDef));
}

TEST(GetDefinitionTest, ModifiedTypeDelegatesToNullElement) {
    auto mod = std::make_shared<ModifiedType>(
        IsConstModifier(), std::make_shared<KnownType>(KnownTypeCode::Int32), /*isRequired*/ false);
    // The element (KnownType) inherits the nullptr default, so the modifier delegates to null.
    EXPECT_EQ(mod->GetDefinition(), nullptr);
}

// ---- NullabilityAnnotatedType: delegates to the wrapped base type ----

TEST(GetDefinitionTest, NullabilityAnnotatedTypeDelegatesToBaseType) {
    TestCompilation compilation;
    TestDefinition valueTypeDef(FullTypeName("System.ValueType"), KnownTypeCode::ValueType,
                                TypeKind::Class, compilation);
    auto valueTypeDefPtr = std::shared_ptr<TestDefinition>(&valueTypeDef, [](TestDefinition*) {});
    auto nat = std::make_shared<NullabilityAnnotatedType>(valueTypeDefPtr, Nullability::Nullable);
    EXPECT_EQ(nat->GetDefinition(), static_cast<const ITypeDefinition*>(&valueTypeDef));
}

TEST(GetDefinitionTest, NullabilityAnnotatedTypeDelegatesToNullBaseType) {
    auto nat = std::make_shared<NullabilityAnnotatedType>(
        std::make_shared<KnownType>(KnownTypeCode::String), Nullability::Nullable);
    EXPECT_EQ(nat->GetDefinition(), nullptr);
}

// ---- ParameterizedType: delegates to the generic definition ----

TEST(GetDefinitionTest, ParameterizedTypeDelegatesToGenericType) {
    TestCompilation compilation;
    TestDefinition listDef(FullTypeName(TopLevelTypeName("System.Collections.Generic", "List", 1)),
                            KnownTypeCode::None, TypeKind::Class, compilation);
    auto listDefPtr = std::shared_ptr<TestDefinition>(&listDef, [](TestDefinition*) {});
    auto param = std::make_shared<ParameterizedType>(
        listDefPtr, std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)});
    EXPECT_EQ(param->GetDefinition(), static_cast<const ITypeDefinition*>(&listDef));
}

TEST(GetDefinitionTest, ParameterizedTypeDelegatesToNullGenericType) {
    auto param = std::make_shared<ParameterizedType>(
        std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic", "List", 1)),
        std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)});
    EXPECT_EQ(param->GetDefinition(), nullptr);
}

// ---- TupleType: delegates to the underlying ValueTuple<...> type ----

TEST(GetDefinitionTest, TupleTypeDelegatesToUnderlyingType) {
    TestCompilation compilation;
    TestDefinition valueTupleDef(FullTypeName(TopLevelTypeName("System.ValueTuple", "ValueTuple", 1)),
                                 KnownTypeCode::None, TypeKind::Struct, compilation);
    auto valueTupleDefPtr = std::shared_ptr<TestDefinition>(&valueTupleDef, [](TestDefinition*) {});
    auto tuple = std::make_shared<TupleType>(
        valueTupleDefPtr,
        std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)});
    EXPECT_EQ(tuple->GetDefinition(), static_cast<const ITypeDefinition*>(&valueTupleDef));
}

TEST(GetDefinitionTest, TupleTypeDelegatesToNullUnderlyingType) {
    // A null underlying type (the deferred ICompilation-driven ctor sentinel) makes
    // the delegation yield null, faithful to the element-null branch.
    auto tuple = std::make_shared<TupleType>(
        nullptr, std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)});
    EXPECT_EQ(tuple->GetDefinition(), nullptr);
}

// ---- A resolved type definition IS its own definition (the MetadataTypeDefinition crux) ----

TEST(GetDefinitionTest, TypeDefinitionReturnsItself) {
    TestCompilation compilation;
    TestDefinition objectDef(FullTypeName("System.Object"), KnownTypeCode::Object,
                              TypeKind::Class, compilation);
    EXPECT_EQ(objectDef.GetDefinition(), static_cast<const ITypeDefinition*>(&objectDef));
}

TEST(GetDefinitionTest, TypeDefinitionGetDefinitionDispatchesThroughITypePointer) {
    TestCompilation compilation;
    TestDefinition objectDef(FullTypeName("System.Object"), KnownTypeCode::Object,
                              TypeKind::Class, compilation);
    // Dispatch through the IType base pointer reaches the TestDefinition override
    // (the virtual GetDefinition), returning the ITypeDefinition* (the object itself).
    const IType* asIType = &objectDef;
    EXPECT_EQ(asIType->GetDefinition(), static_cast<const ITypeDefinition*>(&objectDef));
}
