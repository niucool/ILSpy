// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `KnownTypeReference` (cpp/Decompiler/TypeSystem/KnownTypeReference.hpp,
// the port of the `KnownTypeReference` class from
// ICSharpCode.Decompiler/TypeSystem/KnownTypeReference.cs). It is the first
// concrete `ITypeReference` (the D408 base): a `final` class carrying the
// known-type metadata (code / kind / namespace / name / arity / base type) and
// resolving itself to an `IType` against an `ITypeResolveContext` via
// `context.Compilation.FindType(knownTypeCode)`. The static `Get` /
// `AllKnownTypes` / `GetCSharpNameByTypeCode` surface is what `TypeSystemAstBuilder`
// (the long-pole remaining blocker of `CSharpAmbience`) reaches to look up a
// known type's metadata name and C# primitive keyword.

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeReference.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::INamespace;
using ILSpy::Decompiler::TypeSystem::IMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeReference;
using ILSpy::Decompiler::TypeSystem::ITypeResolveContext;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::KnownTypeReference;
using ILSpy::Decompiler::TypeSystem::StringComparer;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;

// A compact `ICompilation` backing the `TestResolveContext`: overrides the nine
// `ICompilation` pure-virtuals with trivial returns, EXCEPT `FindType` which is
// CODE-AWARE -- it builds and caches a `KnownType(code)` per code so
// `KnownTypeReference::Resolve` (which calls `context.Compilation.FindType(code)`)
// returns the `IType` matching the reference's code (the crux the `Resolve` tests
// pin via `Kind()` / `Name()`). The cache is a fixed-size array of
// `unique_ptr<KnownType>` indexed by code (no `KnownType` copy needed). The
// `mainModule_` uses the shared `TestSupport::TestModule` (D399); `mainModule_(*this)`
// binds it to the already-constructed `ICompilation` base.
class TestCompilation : public ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}

    const IModule& MainModule() const override { return mainModule_; }
    std::vector<const IModule*> Modules() const override { return {&mainModule_}; }
    std::vector<const IModule*> ReferencedModules() const override { return {}; }
    const INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const IType& FindType(KnownTypeCode code) const override
    {
        const auto i = static_cast<std::size_t>(code);
        if (!cache_[i])
            cache_[i] = std::make_unique<KnownType>(code);
        return *cache_[i];
    }
    const StringComparer& NameComparer() const override { return StringComparer::Ordinal(); }
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
    mutable std::array<std::unique_ptr<KnownType>, 60> cache_;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A trivial concrete `ITypeResolveContext` for `KnownTypeReference::Resolve`:
// overrides all six pure-virtuals with trivial returns backed by the held
// `TestCompilation`. `KnownTypeReference::Resolve` reads only
// `context.Compilation().FindType(code)`, never the current-module / -type /
// -member slots, so the nullable accessors return null and the `With*` factories
// return null `unique_ptr`s (this test never calls them).
class TestResolveContext : public ITypeResolveContext {
public:
    TestResolveContext() = default;

    const ICompilation& Compilation() const override { return compilation_; }
    const IModule* CurrentModule() const override { return nullptr; }
    const ITypeDefinition* CurrentTypeDefinition() const override { return nullptr; }
    const IMember* CurrentMember() const override { return nullptr; }
    std::unique_ptr<ITypeResolveContext> WithCurrentTypeDefinition(
        const ITypeDefinition*) const override
    {
        return {};
    }
    std::unique_ptr<ITypeResolveContext> WithCurrentMember(const IMember*) const override
    {
        return {};
    }

private:
    TestCompilation compilation_;
};

} // namespace

// ---------------------------------------------------------------------------
// Get -- `Get(None)` returns `nullptr` (the C# `null` at index 0 of the
// `knownTypeReferences` table).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, GetReturnsNullForNone)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::None), nullptr);
}

// ---------------------------------------------------------------------------
// Get -- `Get(code)` returns the non-null table entry whose accessors carry the
// metadata for that code. Pinned for `Object` (Class, "System", "Object", arity
// 0) and `IEnumerableOfT` (Interface, "System.Collections.Generic",
// "IEnumerable", arity 1).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, GetReturnsEntryWithMetadataForCode)
{
    const auto* obj = KnownTypeReference::Get(KnownTypeCode::Object);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->Code(), KnownTypeCode::Object);
    EXPECT_EQ(obj->Kind(), TypeKind::Class);
    EXPECT_EQ(obj->Namespace(), "System");
    EXPECT_EQ(obj->Name(), "Object");
    EXPECT_EQ(obj->TypeParameterCount(), 0);

    const auto* ienum = KnownTypeReference::Get(KnownTypeCode::IEnumerableOfT);
    ASSERT_NE(ienum, nullptr);
    EXPECT_EQ(ienum->Code(), KnownTypeCode::IEnumerableOfT);
    EXPECT_EQ(ienum->Kind(), TypeKind::Interface);
    EXPECT_EQ(ienum->Namespace(), "System.Collections.Generic");
    EXPECT_EQ(ienum->Name(), "IEnumerable");
    EXPECT_EQ(ienum->TypeParameterCount(), 1);
}

// ---------------------------------------------------------------------------
// Get -- the table is a single static instance: `Get(code)` returns the SAME
// entry on every call (pointer-identity, the C# `static readonly` table).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, GetReturnsSameInstanceForSameCode)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::String),
              KnownTypeReference::Get(KnownTypeCode::String));
    // Distinct codes yield distinct entries.
    EXPECT_NE(KnownTypeReference::Get(KnownTypeCode::Object),
              KnownTypeReference::Get(KnownTypeCode::String));
}

// ---------------------------------------------------------------------------
// BaseType -- a struct's base type is `ValueType` (the ctor's struct->ValueType
// adjustment), NOT the default `Object`.
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, BaseTypeDefaultsToValueTypeForStructs)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Boolean)->BaseType(),
              KnownTypeCode::ValueType);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Int32)->BaseType(),
              KnownTypeCode::ValueType);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::NullableOfT)->BaseType(),
              KnownTypeCode::ValueType);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::SpanOfT)->BaseType(),
              KnownTypeCode::ValueType);
}

// ---------------------------------------------------------------------------
// BaseType -- the explicit base-type overrides from the C# table:
// Object->None, Void->ValueType, Enum->ValueType, MulticastDelegate->Delegate,
// TaskOfT->Task, FormattableString->IFormattable.
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, BaseTypeExplicitOverrides)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Object)->BaseType(),
              KnownTypeCode::None);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Void)->BaseType(),
              KnownTypeCode::ValueType);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Enum)->BaseType(),
              KnownTypeCode::ValueType);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::MulticastDelegate)->BaseType(),
              KnownTypeCode::Delegate);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::TaskOfT)->BaseType(),
              KnownTypeCode::Task);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::FormattableString)->BaseType(),
              KnownTypeCode::IFormattable);
}

// ---------------------------------------------------------------------------
// BaseType -- a class or interface without an explicit base defaults to
// `Object` (no struct adjustment for non-structs).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, BaseTypeDefaultsToObjectForClassesAndInterfaces)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::DBNull)->BaseType(),
              KnownTypeCode::Object);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Exception)->BaseType(),
              KnownTypeCode::Object);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::IEnumerable)->BaseType(),
              KnownTypeCode::Object);
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::IDisposable)->BaseType(),
              KnownTypeCode::Object);
}

// ---------------------------------------------------------------------------
// TypeName -- `TypeName()` returns the `TopLevelTypeName` built from the
// entry's namespace / name / arity. Pinned for `IEnumerableOfT` and `Void`.
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, TypeNameReturnsTopLevelTypeName)
{
    const auto* ienum = KnownTypeReference::Get(KnownTypeCode::IEnumerableOfT);
    ASSERT_NE(ienum, nullptr);
    TopLevelTypeName tn = ienum->TypeName();
    EXPECT_EQ(tn.Name(), "IEnumerable");
    EXPECT_EQ(tn.TypeParameterCount(), 1);

    const auto* v = KnownTypeReference::Get(KnownTypeCode::Void);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(v->TypeName().Name(), "Void");
    EXPECT_EQ(v->TypeName().TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// Resolve -- the crux: `Resolve` returns the `IType` the compilation holds for
// the reference's code (`context.Compilation.FindType(knownTypeCode)`). Pinned
// by the `Kind()` / `Name()` the code maps to (the `TestCompilation.FindType`
// builds a `KnownType(code)`).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, ResolveReturnsCompilationKnownType)
{
    TestResolveContext ctx;
    const auto* ref = KnownTypeReference::Get(KnownTypeCode::Int32);
    ASSERT_NE(ref, nullptr);
    const IType& result = ref->Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Struct);
    EXPECT_EQ(result.Name(), "Int32");
}

TEST(KnownTypeReferenceTest, ResolveReturnsObjectForObject)
{
    TestResolveContext ctx;
    const IType& result = KnownTypeReference::Get(KnownTypeCode::Object)->Resolve(ctx);
    EXPECT_EQ(result.Kind(), TypeKind::Class);
    EXPECT_EQ(result.Name(), "Object");
}

// ---------------------------------------------------------------------------
// Resolve -- dispatches polymorphically through an `ITypeReference*` (the
// dynamic dispatch the type-system paths rely on: a type reference is held as
// an `ITypeReference` and the resolved `IType` reached through the base
// pointer).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, ResolveDispatchesThroughITypeReferencePointer)
{
    const ITypeReference* base = KnownTypeReference::Get(KnownTypeCode::String);
    ASSERT_NE(base, nullptr);
    TestResolveContext ctx;
    EXPECT_EQ(base->Resolve(ctx).Kind(), TypeKind::Class);
    EXPECT_EQ(base->Resolve(ctx).Name(), "String");
}

// ---------------------------------------------------------------------------
// GetCSharpNameByTypeCode -- the C# primitive keyword for a primitive known
// type, `std::nullopt` for a non-primitive.
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, GetCSharpNameByTypeCodeReturnsPrimitiveKeywords)
{
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::Object),
              std::optional<std::string_view>("object"));
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::Boolean),
              std::optional<std::string_view>("bool"));
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::Int32),
              std::optional<std::string_view>("int"));
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::String),
              std::optional<std::string_view>("string"));
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::Void),
              std::optional<std::string_view>("void"));
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::Double),
              std::optional<std::string_view>("double"));
}

TEST(KnownTypeReferenceTest, GetCSharpNameByTypeCodeReturnsNulloptForNonPrimitives)
{
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::DBNull),
              std::nullopt);
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::Type),
              std::nullopt);
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::IEnumerable),
              std::nullopt);
    EXPECT_EQ(KnownTypeReference::GetCSharpNameByTypeCode(KnownTypeCode::None),
              std::nullopt);
}

// ---------------------------------------------------------------------------
// ToString -- the C# primitive keyword for a primitive, otherwise the metadata
// full name (`Namespace + "." + Name`).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, ToStringReturnsPrimitiveKeywordForPrimitives)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Object)->ToString(), "object");
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Int32)->ToString(), "int");
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::String)->ToString(), "string");
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Void)->ToString(), "void");
}

TEST(KnownTypeReferenceTest, ToStringReturnsMetadataFullNameForNonPrimitives)
{
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::DBNull)->ToString(), "System.DBNull");
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::IEnumerable)->ToString(),
              "System.Collections.IEnumerable");
    EXPECT_EQ(KnownTypeReference::Get(KnownTypeCode::Task)->ToString(),
              "System.Threading.Tasks.Task");
}

// ---------------------------------------------------------------------------
// AllKnownTypes -- every non-`None` table entry (the 59 known types). Each is
// non-null and distinct; the set covers codes 1..59.
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, AllKnownTypesReturnsAllNonNoneEntries)
{
    const auto all = KnownTypeReference::AllKnownTypes();
    EXPECT_EQ(all.size(), 59u);
    for (const auto* e : all)
        ASSERT_NE(e, nullptr);
    // The first is `Object` (code 1), the last is `Range` (code 59).
    EXPECT_EQ(all.front()->Code(), KnownTypeCode::Object);
    EXPECT_EQ(all.back()->Code(), KnownTypeCode::Range);
}

// ---------------------------------------------------------------------------
// KnownTypeReference is `final` (the C# `sealed`), derives from `ITypeReference`,
// is polymorphic, and has a virtual destructor (for abstract-base deletion via
// the `ITypeReference` interface).
// ---------------------------------------------------------------------------
TEST(KnownTypeReferenceTest, IsFinalAndIsAnITypeReference)
{
    static_assert(std::is_final_v<KnownTypeReference>,
                  "KnownTypeReference must be final (the C# sealed).");
    static_assert(std::is_base_of_v<ITypeReference, KnownTypeReference>,
                  "KnownTypeReference must derive from ITypeReference.");
    static_assert(std::has_virtual_destructor_v<KnownTypeReference>,
                  "KnownTypeReference must have a virtual destructor (via ITypeReference).");
    static_assert(std::is_polymorphic_v<KnownTypeReference>,
                  "KnownTypeReference must be polymorphic.");
    // A `KnownTypeReference*` upcasts to the base `ITypeReference*`.
    const KnownTypeReference* derived = KnownTypeReference::Get(KnownTypeCode::Object);
    const ITypeReference* base = derived;
    EXPECT_NE(base, nullptr);
}
