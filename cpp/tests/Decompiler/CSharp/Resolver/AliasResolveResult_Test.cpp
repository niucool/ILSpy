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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the twin Alias `ResolveResult` subclasses (the first
// `cpp/Decompiler/CSharp/Resolver/` leaves, the D467 port of
// ICSharpCode.Decompiler/CSharp/Resolver/AliasTypeResolveResult.cs and
// AliasNamespaceResolveResult.cs). Both record the extern-alias used to resolve
// the underlying result: `AliasTypeResolveResult` wraps a `TypeResolveResult`
// (D425) and forwards the underlying type to the base; `AliasNamespaceResolveResult`
// wraps a `NamespaceResolveResult` (D433) and forwards the underlying namespace to
// the base. The C# surface for each is a `string Alias` property plus a forwarding
// ctor; the C++ port additionally overrides `ClassName()` (the polymorphic
// `GetType().Name` in the inherited `ToString`) and `ShallowClone()` (the
// runtime-type-preserving clone, avoiding the C++-only slicing).
//
// The tests pin the ctor-stores-alias+forwarded-state contract, the pointer-identity
// of the forwarded type/namespace, the `IsError` dispatch through the
// `TypeResolveResult` base (the alias over an unknown-type underlying is an
// error), the inherited `ToString` subclass-class-name format, the `ShallowClone`
// runtime-type preservation plus shared-type/shared-namespace plus value-copied
// alias, the polymorphic dispatch through a `ResolveResult*` base pointer, the
// inherited base defaults, and the `is_base_of` / not-`final` class-shape
// static_asserts.

#include "Decompiler/CSharp/Resolver/AliasTypeResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/AliasNamespaceResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

// A minimal concrete `ICompilation` for the `INamespace` stub (the `Compilation()`
// accessor the `INamespace` interface inherits from `ICompilationProvider` requires a
// concrete `ICompilation` to return). Follows the `NamespaceResolveResult_Test`
// `TestCompilation` pattern (D433): holds a `TestSupport::TestModule` bound to `*this`
// so `MainModule()` / `RootNamespace()` return valid references. The nine
// `ICompilation` pure-virtuals are overridden with trivial returns.
class TestCompilation : public TS::ICompilation {
public:
    TestCompilation() : mainModule_(*this) {}

    const TS::IModule& MainModule() const override { return mainModule_; }
    std::vector<const TS::IModule*> Modules() const override { return {&mainModule_}; }
    std::vector<const TS::IModule*> ReferencedModules() const override { return {}; }
    const TS::INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const TS::IType& FindType(TS::KnownTypeCode) const override { return knownType_; }
    const TS::StringComparer& NameComparer() const override
    {
        return TS::StringComparer::Ordinal();
    }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    TS::TypeSystemOptions TypeSystemOptions() const override
    {
        return TS::TypeSystemOptions::None;
    }

private:
    TS::TestSupport::TestModule mainModule_;
    TS::KnownType knownType_{TS::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `INamespace` for testing: takes a full name string (and the
// compilation it belongs to) and returns it from `FullName()` / the short name
// from `Name()`. Mirrors the `NamespaceResolveResult_Test` `TestNamespace` stub
// (D433) so the `AliasNamespaceResolveResult` `NamespaceName()` / `ToString()`
// tests can pin a meaningful name (e.g. "System.Collections").
class TestNamespace : public TS::INamespace {
public:
    TestNamespace(const TS::ICompilation& compilation, std::string fullName)
        : compilation_(compilation), fullName_(std::move(fullName)) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override
    {
        auto pos = fullName_.rfind('.');
        return pos == std::string::npos ? fullName_ : fullName_.substr(pos + 1);
    }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override { return compilation_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return fullName_; }
    const TS::INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const TS::INamespace*> ChildNamespaces() const override { return {}; }
    std::vector<const TS::ITypeDefinition*> Types() const override { return {}; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const TS::INamespace* GetChildNamespace(const std::string&) const override { return nullptr; }
    const TS::ITypeDefinition* GetTypeDefinition(const std::string&, int) const override
    {
        return nullptr;
    }

private:
    const TS::ICompilation& compilation_;
    std::string fullName_;
};

// A test fixture holding a `TestCompilation` + a `TestNamespace` with a known
// full name, so the `AliasNamespaceResolveResult` tests can construct the
// underlying `NamespaceResolveResult` over a real `INamespace`.
struct AliasNamespaceFixture {
    TestCompilation compilation;
    TestNamespace ns;
    AliasNamespaceFixture() : ns(compilation, "System.Collections") {}
};

} // namespace

// ---------------------------------------------------------------------------
// AliasTypeResolveResult
// ---------------------------------------------------------------------------

TEST(AliasTypeResolveResultTest, ConstructorStoresAliasAndForwardsType)
{
    // The underlying `TypeResolveResult` over a known type; the alias ctor forwards
    // the underlying's stored `IType` handle to the `TypeResolveResult` base and
    // stores the alias.
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::String));
    Res::AliasTypeResolveResult alias("System", underlying);
    EXPECT_EQ(alias.Alias(), "System");
    // The type is forwarded by shared ownership (TypePtr pointer-identity).
    EXPECT_EQ(alias.TypePtr(), underlying.TypePtr());
}

TEST(AliasTypeResolveResultTest, TypeIsForwardedFromUnderlyingResult)
{
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
    Res::AliasTypeResolveResult alias("global", underlying);
    // The inherited `Type()` returns the forwarded `IType` -- the same underlying type.
    EXPECT_EQ(&alias.Type(), &underlying.Type());
    EXPECT_EQ(alias.Type().Kind(), underlying.Type().Kind());
}

TEST(AliasTypeResolveResultTest, IsErrorFalseForKnownType)
{
    // A known-type underlying is not an error, so the alias (which inherits the
    // `TypeResolveResult::IsError` dispatch on the forwarded type) is not an error.
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
    Res::AliasTypeResolveResult alias("global", underlying);
    EXPECT_FALSE(alias.IsError());
}

TEST(AliasTypeResolveResultTest, IsErrorTrueForUnknownTypeUnderlying)
{
    // An unknown-type underlying is an error, and the alias inherits that dispatch
    // through the forwarded type -- the load-bearing crux distinguishing the alias
    // (which forwards the type) from a fresh result.
    Sem::TypeResolveResult underlying(TS::UnknownType());
    Res::AliasTypeResolveResult alias("global", underlying);
    EXPECT_TRUE(alias.IsError());
}

TEST(AliasTypeResolveResultTest, ToStringReportsSubclassClassName)
{
    // The inherited `ResolveResult::ToString` is `[<ClassName> <ReflectionName>]`;
    // the `ClassName()` override yields "AliasTypeResolveResult", and the
    // `KnownType(String)` `ReflectionName` is "System.String".
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::String));
    Res::AliasTypeResolveResult alias("System", underlying);
    EXPECT_EQ(alias.ToString(), "[AliasTypeResolveResult System.String]");
}

TEST(AliasTypeResolveResultTest, ShallowClonePreservesRuntimeType)
{
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
    Res::AliasTypeResolveResult alias("global", underlying);
    auto clone = alias.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<Res::AliasTypeResolveResult*>(clone.get()), nullptr);
}

TEST(AliasTypeResolveResultTest, ShallowCloneSharesTypeAndCopiesAlias)
{
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
    Res::AliasTypeResolveResult alias("global", underlying);
    auto clone = alias.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* aliasClone = dynamic_cast<Res::AliasTypeResolveResult*>(clone.get());
    ASSERT_NE(aliasClone, nullptr);
    // The clone shares the underlying type (shared_ptr copy, faithful to C# MemberwiseClone).
    EXPECT_EQ(aliasClone->TypePtr(), alias.TypePtr());
    // The clone value-copies the alias string.
    EXPECT_EQ(aliasClone->Alias(), "global");
}

TEST(AliasTypeResolveResultTest, ShallowCloneIsDistinctInstance)
{
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
    Res::AliasTypeResolveResult alias("global", underlying);
    auto clone = alias.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &alias);
}

TEST(AliasTypeResolveResultTest, DispatchesPolymorphicallyThroughBasePointer)
{
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::String));
    Res::AliasTypeResolveResult alias("System", underlying);
    Sem::ResolveResult* base = &alias;
    EXPECT_EQ(base->ToString(), "[AliasTypeResolveResult System.String]");
    auto clone = base->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<Res::AliasTypeResolveResult*>(clone.get()), nullptr);
}

TEST(AliasTypeResolveResultTest, InheritedDefaultsArePreserved)
{
    // `AliasTypeResolveResult` does NOT override `IsCompileTimeConstant` /
    // `ConstantValue` / `GetChildResults`, and inherits the `TypeResolveResult`
    // `IsError` override (tested above), so the remaining inherited defaults hold.
    Sem::TypeResolveResult underlying(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object));
    Res::AliasTypeResolveResult alias("global", underlying);
    EXPECT_FALSE(alias.IsCompileTimeConstant());
    EXPECT_FALSE(alias.ConstantValue().has_value());
    EXPECT_TRUE(alias.GetChildResults().empty());
}

TEST(AliasTypeResolveResultTest, IsTypeResolveResultAndResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<Sem::TypeResolveResult, Res::AliasTypeResolveResult>,
                  "AliasTypeResolveResult derives from TypeResolveResult.");
    static_assert(std::is_base_of_v<Sem::ResolveResult, Res::AliasTypeResolveResult>,
                  "AliasTypeResolveResult (transitively) derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<Res::AliasTypeResolveResult>,
                  "AliasTypeResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<Res::AliasTypeResolveResult>,
                  "AliasTypeResolveResult is polymorphic (the ToString/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `AliasTypeResolveResult` is NOT sealed (no C# subclass derives from it but
    // it is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<Res::AliasTypeResolveResult>,
                  "AliasTypeResolveResult is not final (the C# class is unsealed).");
}

// ---------------------------------------------------------------------------
// AliasNamespaceResolveResult
// ---------------------------------------------------------------------------

TEST(AliasNamespaceResolveResultTest, ConstructorStoresAliasAndForwardsNamespace)
{
    // The underlying `NamespaceResolveResult` over a real `INamespace`; the alias
    // ctor forwards the underlying's stored `INamespace*` to the
    // `NamespaceResolveResult` base and stores the alias.
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    EXPECT_EQ(alias.Alias(), "global");
    // The namespace is forwarded (pointer-identity).
    EXPECT_EQ(alias.Namespace(), &f.ns);
    EXPECT_EQ(alias.Namespace(), underlying.Namespace());
}

TEST(AliasNamespaceResolveResultTest, NamespaceNameMatchesForwardedNamespace)
{
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    // The inherited `NamespaceName()` delegates to the forwarded namespace's
    // `FullName()` -- the same dotted name as the underlying.
    EXPECT_EQ(alias.NamespaceName(), "System.Collections");
    EXPECT_EQ(alias.NamespaceName(), underlying.NamespaceName());
}

TEST(AliasNamespaceResolveResultTest, TypeIsNoTypeFromBase)
{
    // The `NamespaceResolveResult` base forwards `NoType()` (`TypeKind::None`), so
    // the alias's inherited `Type().Kind()` is `TypeKind::None` (distinct from
    // `UnknownType()` which is `TypeKind::Unknown`).
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    EXPECT_EQ(alias.Type().Kind(), TS::TypeKind::None);
}

TEST(AliasNamespaceResolveResultTest, ToStringReportsSubclassClassName)
{
    // The inherited `NamespaceResolveResult::ToString` is `[<ClassName> <FullName>]`;
    // the `ClassName()` override yields "AliasNamespaceResolveResult", and the
    // forwarded namespace's `FullName()` is "System.Collections".
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    EXPECT_EQ(alias.ToString(), "[AliasNamespaceResolveResult System.Collections]");
}

TEST(AliasNamespaceResolveResultTest, ShallowClonePreservesRuntimeType)
{
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    auto clone = alias.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<Res::AliasNamespaceResolveResult*>(clone.get()), nullptr);
}

TEST(AliasNamespaceResolveResultTest, ShallowCloneSharesNamespaceAndCopiesAlias)
{
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    auto clone = alias.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* aliasClone = dynamic_cast<Res::AliasNamespaceResolveResult*>(clone.get());
    ASSERT_NE(aliasClone, nullptr);
    // The clone shares the forwarded namespace (raw-pointer copy).
    EXPECT_EQ(aliasClone->Namespace(), &f.ns);
    EXPECT_EQ(aliasClone->NamespaceName(), "System.Collections");
    // The clone value-copies the alias string.
    EXPECT_EQ(aliasClone->Alias(), "global");
}

TEST(AliasNamespaceResolveResultTest, ShallowCloneIsDistinctInstance)
{
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    auto clone = alias.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &alias);
}

TEST(AliasNamespaceResolveResultTest, DispatchesPolymorphicallyThroughBasePointer)
{
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    Sem::ResolveResult* base = &alias;
    EXPECT_EQ(base->ToString(), "[AliasNamespaceResolveResult System.Collections]");
    auto clone = base->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<Res::AliasNamespaceResolveResult*>(clone.get()), nullptr);
}

TEST(AliasNamespaceResolveResultTest, InheritedDefaultsArePreserved)
{
    // `AliasNamespaceResolveResult` does NOT override `IsCompileTimeConstant` /
    // `ConstantValue` / `IsError` / `GetChildResults`, so the inherited base
    // defaults hold (the `NamespaceResolveResult` does not override them either).
    AliasNamespaceFixture f;
    Sem::NamespaceResolveResult underlying(&f.ns);
    Res::AliasNamespaceResolveResult alias("global", underlying);
    EXPECT_FALSE(alias.IsCompileTimeConstant());
    EXPECT_FALSE(alias.ConstantValue().has_value());
    EXPECT_FALSE(alias.IsError());
    EXPECT_TRUE(alias.GetChildResults().empty());
}

TEST(AliasNamespaceResolveResultTest, IsNamespaceResolveResultAndResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<Sem::NamespaceResolveResult, Res::AliasNamespaceResolveResult>,
                  "AliasNamespaceResolveResult derives from NamespaceResolveResult.");
    static_assert(std::is_base_of_v<Sem::ResolveResult, Res::AliasNamespaceResolveResult>,
                  "AliasNamespaceResolveResult (transitively) derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<Res::AliasNamespaceResolveResult>,
                  "AliasNamespaceResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<Res::AliasNamespaceResolveResult>,
                  "AliasNamespaceResolveResult is polymorphic (the ToString/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `AliasNamespaceResolveResult` is NOT sealed (no C# subclass derives from it
    // but it is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<Res::AliasNamespaceResolveResult>,
                  "AliasNamespaceResolveResult is not final (the C# class is unsealed).");
}
