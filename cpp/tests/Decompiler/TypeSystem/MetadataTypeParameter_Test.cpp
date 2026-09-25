// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The MetadataTypeParameter test suite for the custom-attribute surface --
// `GetAttributes`, `NullabilityConstraint`, and `TypeConstraints` (the
// members the port completed over the already-ported `AttributeListBuilder` /
// `CustomAttributeDecoder` / `MetadataModule.ResolveType`).
//
// Fixtures:
//   * mscorlib 4.8: ZERO custom attributes on GenericParam or
//     GenericParamConstraint rows (probed over the whole table), so the
//     type-parameter attribute list is empty and `NullabilityConstraint`
//     falls back to the owner's `NullableContext` (Oblivious). The constraint
//     readers still exercise real rows: `Nullable`1.T` carries the
//     `System.ValueType` constraint row plus the `struct` flag (so the
//     ValueType tail re-appends it), while `List`1.T` has no rows (the Object
//     tail).
//   * .NET 10 CoreLib: the GenericParam rows carry `[Nullable]`,
//     `[DynamicallyAccessedMembers]`, and `[IsUnmanaged]` attributes.
//     `Action`1.T` is a stable `[Nullable(2)]` fixture.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/INamedElement.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeConstraint.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

const char* CoreLibPath() {
#if defined(_WIN32)
    return "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\"
           "System.Private.CoreLib.dll";
#else
    return "";
#endif
}

bool FileExists(const char* path) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }
bool CoreLibAvailable() { return FileExists(CoreLibPath()); }

// A module reference resolving to an externally-owned module (the
// MetadataMethod_Test fixture precedent).
class FixedModuleRef : public TS::IModuleReference {
public:
    explicit FixedModuleRef(const TS::IModule* module = nullptr)
        : module_(module) {}

    const TS::IModule* Resolve(
        const TS::ITypeResolveContext&) const override {
        return module_;
    }

private:
    const TS::IModule* module_;
};

// The port's SimpleCompilation with the protected Init exposed.
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                    std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

struct MtpFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile coreLibFile{ CoreLibPath() };

    TestCompilation mscComp;
    TS::MetadataModule msc{ mscComp, &mscorlibFile,
                            TS::TypeSystemOptions::Default };
    FixedModuleRef mscRef{ &msc };

    TestCompilation coreComp;
    TS::MetadataModule core{ coreComp, &coreLibFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef coreRef{ &core };

    MtpFixture() {
        mscComp.Initialize(mscRef, {});
        coreComp.Initialize(coreRef, {});
    }

    const TS::ITypeDefinition* Type(TS::MetadataModule& module,
                                    const char* ns, const char* name,
                                    int arity) {
        return module.GetTypeDefinition(
            TS::TopLevelTypeName(ns, name, arity));
    }

    const TS::ITypeParameter* FirstTypeParameter(
        const TS::ITypeDefinition& td) {
        const std::vector<const TS::ITypeParameter*> tps =
            td.TypeParameters();
        EXPECT_FALSE(tps.empty()) << "fixture type has no type parameters";
        return tps.empty() ? nullptr : tps.front();
    }
};

// The C# `IType.FullName` for an attribute type: an `IEntity` reports
// `INamedElement::FullName()`, else `ReflectionName()` (the
// AutoEventDecompiler file-local convention).
std::string AttrTypeName(const TS::IType& type) {
    if (const auto* named =
            dynamic_cast<const TS::INamedElement*>(&type))
        return named->FullName();
    return type.ReflectionName();
}

// ---------------------------------------------------------------------------
// GetAttributes
// ---------------------------------------------------------------------------

TEST(MetadataTypeParameterTest, GetAttributesEmptyOnMscorlib) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* nullable =
        fx.Type(fx.msc, "System", "Nullable", 1);
    ASSERT_NE(nullable, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*nullable);
    ASSERT_NE(t, nullptr);
    // mscorlib carries zero GenericParam custom attributes.
    EXPECT_TRUE(t->GetAttributes().empty());
}

TEST(MetadataTypeParameterTest, GetAttributesDynamicallyAccessedMembersOnCoreLib) {
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* lazyDebugView =
        fx.Type(fx.core, "System", "LazyDebugView", 1);
    ASSERT_NE(lazyDebugView, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*lazyDebugView);
    ASSERT_NE(t, nullptr);
    const std::vector<const TS::IAttribute*> attrs = t->GetAttributes();
    ASSERT_EQ(attrs.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(AttrTypeName(attrs[0]->AttributeType()),
              "System.Diagnostics.CodeAnalysis.DynamicallyAccessedMembersAttribute");
    // The list is cached: a second read yields the same instance.
    const std::vector<const TS::IAttribute*> again = t->GetAttributes();
    ASSERT_EQ(again.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(again[0], attrs[0]);
}

// ---------------------------------------------------------------------------
// NullabilityConstraint
// ---------------------------------------------------------------------------

TEST(MetadataTypeParameterTest, NullabilityConstraintNullableOnCoreLib) {
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* action =
        fx.Type(fx.core, "System", "Action", 1);
    ASSERT_NE(action, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*action);
    ASSERT_NE(t, nullptr);
    // Action`1.T carries [Nullable(2)].
    EXPECT_EQ(t->NullabilityConstraint(), TS::Nullability::Nullable);
}

TEST(MetadataTypeParameterTest, NullabilityConstraintObliviousOnMscorlib) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* nullable =
        fx.Type(fx.msc, "System", "Nullable", 1);
    ASSERT_NE(nullable, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*nullable);
    ASSERT_NE(t, nullptr);
    // No [Nullable] row and an Oblivious owner context.
    EXPECT_EQ(t->NullabilityConstraint(), TS::Nullability::Oblivious);
}

// ---------------------------------------------------------------------------
// TypeConstraints
// ---------------------------------------------------------------------------

TEST(MetadataTypeParameterTest, TypeConstraintsValueTypeTail) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* nullable =
        fx.Type(fx.msc, "System", "Nullable", 1);
    ASSERT_NE(nullable, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*nullable);
    ASSERT_NE(t, nullptr);
    // The GenericParamConstraint row (System.ValueType) plus the
    // `struct`-flag tail re-appends ValueType.
    const std::vector<TS::TypeConstraint> constraints =
        t->TypeConstraints();
    ASSERT_EQ(constraints.size(), static_cast<std::size_t>(2));
    for (const TS::TypeConstraint& c : constraints) {
        ASSERT_NE(c.Type(), nullptr);
        EXPECT_EQ(c.Type()->GetDefinition() == nullptr
                      ? TS::KnownTypeCode::None
                      : c.Type()->GetDefinition()->KnownTypeCode(),
                  TS::KnownTypeCode::ValueType);
    }
}

TEST(MetadataTypeParameterTest, TypeConstraintsObjectTail) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* list =
        fx.Type(fx.msc, "System.Collections.Generic", "List", 1);
    ASSERT_NE(list, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*list);
    ASSERT_NE(t, nullptr);
    // No constraint rows and no non-interface constraint: the Object tail.
    const std::vector<TS::TypeConstraint> constraints =
        t->TypeConstraints();
    ASSERT_EQ(constraints.size(), static_cast<std::size_t>(1));
    ASSERT_NE(constraints[0].Type(), nullptr);
    const TS::ITypeDefinition* def =
        constraints[0].Type()->GetDefinition();
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->KnownTypeCode(), TS::KnownTypeCode::Object);
}

TEST(MetadataTypeParameterTest, TypeConstraintsInterfaceOnlyAddsObjectTail) {
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* deferred =
        fx.Type(fx.core, "System.Threading", "DeferredDisposableLifetime", 1);
    ASSERT_NE(deferred, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*deferred);
    ASSERT_NE(t, nullptr);
    // `where T : IDeferredDisposable` is an interface-only constraint, so the
    // interface is kept and the Object tail is appended.
    const std::vector<TS::TypeConstraint> constraints =
        t->TypeConstraints();
    ASSERT_EQ(constraints.size(), static_cast<std::size_t>(2));
    ASSERT_NE(constraints[0].Type(), nullptr);
    EXPECT_EQ(constraints[0].Type()->Kind(), TS::TypeKind::Interface);
    EXPECT_EQ(constraints[0].Type()->ReflectionName(),
              "System.Threading.IDeferredDisposable");
    ASSERT_NE(constraints[1].Type(), nullptr);
    ASSERT_NE(constraints[1].Type()->GetDefinition(), nullptr);
    EXPECT_EQ(constraints[1].Type()->GetDefinition()->KnownTypeCode(),
              TS::KnownTypeCode::Object);
}

TEST(MetadataTypeParameterTest, TypeConstraintsCached) {
    if (!MscorlibAvailable())
        GTEST_SKIP() << "mscorlib fixture not available";
    MtpFixture fx;
    const TS::ITypeDefinition* nullable =
        fx.Type(fx.msc, "System", "Nullable", 1);
    ASSERT_NE(nullable, nullptr);
    const TS::ITypeParameter* t = fx.FirstTypeParameter(*nullable);
    ASSERT_NE(t, nullptr);
    const std::vector<TS::TypeConstraint> first = t->TypeConstraints();
    const std::vector<TS::TypeConstraint> second = t->TypeConstraints();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); i++)
        EXPECT_EQ(first[i].Type().get(), second[i].Type().get());
}

} // namespace
