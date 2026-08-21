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
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// DEALINGS IN THE SOFTWARE.

// Tests for `NamespaceResolveResult` (cpp/Decompiler/Semantics/NamespaceResolveResult.hpp,
// the D433 port of ICSharpCode.Decompiler/Semantics/NamespaceResolveResult.cs) -- the
// `ResolveResult` for an expression that resolved to a namespace. The C# source declares
// the ctor (`: base(SpecialType.NoType)`), the `Namespace` / `NamespaceName` accessors,
// and a custom `ToString`. The C++ port additionally overrides `ClassName()` (the
// polymorphic `GetType().Name` in the custom `ToString`) and `ShallowClone()` (the
// runtime-type-preserving clone, avoiding the C++-only slicing).
//
// The tests pin the ctor-stores-namespace contract, the `SpecialType.NoType` base-type
// forwarding (`Type().Kind() == TypeKind::None`), the `NamespaceName` / `FullName`
// accessor, the custom `ToString` format (`[NamespaceResolveResult <FullName>]`), the
// `ShallowClone` runtime-type preservation (not sliced) plus shared-namespace-pointer
// (both the original and the clone point to the same `INamespace`), the inherited base
// defaults the subclass does NOT override, the polymorphic dispatch through a
// `ResolveResult*` base pointer, and the `is_base_of` / not-`final` class-shape
// static_asserts. The `NoType()` convenience (the prerequisite this leaf required) is
// pinned by the `ConstructorForwardsNoTypeToBase` test.

#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A minimal concrete `ICompilation` for the `INamespace` stub (the `Compilation()`
// accessor the `INamespace` interface inherits from `ICompilationProvider` requires a
// concrete `ICompilation` to return). Follows the `ICompilationProvider_Test`
// `TestCompilation` pattern (D379): holds a `TestSupport::TestModule` bound to `*this`
// so `MainModule()` / `RootNamespace()` return valid references. The nine
// `ICompilation` pure-virtuals are overridden with trivial returns.
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
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `INamespace` for testing: takes a full name string (and the
// compilation it belongs to) and returns it from `FullName()` / the short name from
// `Name()`. Overrides every `ISymbol` / `ICompilationProvider` / `INamespace`
// pure-virtual with a trivial return (empty / null / `false`), faithful to the
// `TestSupport::TestNamespace` pattern but with a configurable `FullName` so the
// `NamespaceResolveResult` `NamespaceName()` / `ToString()` tests can pin a
// meaningful name (e.g. "System.Collections").
class TestNamespace : public ILSpy::Decompiler::TypeSystem::INamespace {
public:
    TestNamespace(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                  std::string fullName)
        : compilation_(compilation), fullName_(std::move(fullName)) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }
    std::string Name() const override
    {
        // The short name is the last dotted segment of the full name (or the whole
        // name if there is no dot). Faithful to how `MergedNamespace` derives the
        // short name from the full dotted name.
        auto pos = fullName_.rfind('.');
        return pos == std::string::npos ? fullName_ : fullName_.substr(pos + 1);
    }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return fullName_; }
    const ILSpy::Decompiler::TypeSystem::INamespace* ParentNamespace() const override
    {
        return nullptr;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::INamespace*> ChildNamespaces() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> Types() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ContributingModules() const override
    {
        return {};
    }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetChildNamespace(
        const std::string&) const override
    {
        return nullptr;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const std::string&, int) const override
    {
        return nullptr;
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    std::string fullName_;
};

// A test fixture holding a `TestCompilation` + a `TestNamespace` with a known
// full name, so the `NamespaceResolveResult` tests can construct the result over a
// real `INamespace` with a meaningful `FullName`.
struct NamespaceResolveResultFixture {
    TestCompilation compilation;
    TestNamespace ns;
    NamespaceResolveResultFixture()
        : ns(compilation, "System.Collections") {}
};

} // namespace

TEST(NamespaceResolveResultTest, ConstructorStoresNamespace)
{
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    EXPECT_EQ(nrr.Namespace(), &f.ns);
}

TEST(NamespaceResolveResultTest, ConstructorForwardsNoTypeToBase)
{
    // The C# ctor forwards `SpecialType.NoType` (the `TypeKind::None` null object)
    // to the `ResolveResult` base. The C++ port uses the `NoType()` convenience
    // (the prerequisite this leaf required), so `Type().Kind()` is `TypeKind::None`
    // -- distinct from `UnknownType()` which is `TypeKind::Unknown`.
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    EXPECT_EQ(nrr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::None);
}

TEST(NamespaceResolveResultTest, NamespaceNameReturnsFullName)
{
    // The C# `NamespaceName => ns.FullName` -- the dotted full name.
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    EXPECT_EQ(nrr.NamespaceName(), "System.Collections");
}

TEST(NamespaceResolveResultTest, NamespaceNameMatchesNamespaceFullName)
{
    // The `NamespaceName` accessor delegates to `ns_->FullName()`, so it matches
    // the `INamespace::FullName()` of the held namespace.
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    EXPECT_EQ(nrr.NamespaceName(), f.ns.FullName());
}

TEST(NamespaceResolveResultTest, ToStringReportsClassNameAndFullName)
{
    // The C# `ToString` is `string.Format(..., "[{0} {1}]", GetType().Name, ns)`.
    // The C++ port uses `ClassName()` ("NamespaceResolveResult") + `ns_->FullName()`
    // (the closest faithful representation, since `INamespace` has no `ToString`
    // virtual in the C++ port -- a documented deviation from the C# `ns.ToString()`).
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    EXPECT_EQ(nrr.ToString(), "[NamespaceResolveResult System.Collections]");
}

TEST(NamespaceResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the runtime
    // type, so a cloned `NamespaceResolveResult` stays a `NamespaceResolveResult`
    // (not sliced to the `ResolveResult` base).
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    auto clone = nrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::NamespaceResolveResult*>(clone.get()),
              nullptr);
}

TEST(NamespaceResolveResultTest, ShallowCloneSharesNamespace)
{
    // The clone shares the `INamespace` (the default copy ctor copies the raw
    // `ns_` pointer, so both the original and the clone point to the same
    // compilation-owned `INamespace`), faithful to the C# `MemberwiseClone`
    // reference-copy. The `ShallowClone` return is a `unique_ptr<ResolveResult>`
    // (the base return type), so the subclass-own `Namespace()` / `NamespaceName()`
    // accessors are reached via a `dynamic_cast` downcast (the accessor-family-
    // boundary convention: subclass-own accessors are NOT on the `ResolveResult`
    // base).
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    auto clone = nrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* cloneNs = dynamic_cast<ILSpy::Decompiler::Semantics::NamespaceResolveResult*>(
        clone.get());
    ASSERT_NE(cloneNs, nullptr);
    EXPECT_EQ(cloneNs->Namespace(), &f.ns);
    EXPECT_EQ(cloneNs->NamespaceName(), "System.Collections");
}

TEST(NamespaceResolveResultTest, ShallowCloneIsDistinctInstance)
{
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    auto clone = nrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &nrr);
}

TEST(NamespaceResolveResultTest, InheritedDefaultsArePreserved)
{
    // `NamespaceResolveResult` does NOT override `IsCompileTimeConstant` /
    // `ConstantValue` / `IsError` / `GetChildResults`, so the inherited
    // `ResolveResult` base defaults hold.
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    EXPECT_FALSE(nrr.IsCompileTimeConstant());
    EXPECT_FALSE(nrr.ConstantValue().has_value());
    EXPECT_FALSE(nrr.IsError());
    EXPECT_TRUE(nrr.GetChildResults().empty());
}

TEST(NamespaceResolveResultTest, DispatchesPolymorphicallyThroughBasePointer)
{
    // The `ToString` / `ClassName` / `ShallowClone` overrides dispatch through a
    // `ResolveResult*` base pointer (the virtual dispatch the C# resolver relies on).
    NamespaceResolveResultFixture f;
    ILSpy::Decompiler::Semantics::NamespaceResolveResult nrr(&f.ns);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &nrr;
    EXPECT_EQ(base->ToString(), "[NamespaceResolveResult System.Collections]");
    auto clone = base->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::NamespaceResolveResult*>(clone.get()),
              nullptr);
}

TEST(NamespaceResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::NamespaceResolveResult>,
                  "NamespaceResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::NamespaceResolveResult>,
                  "NamespaceResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::NamespaceResolveResult>,
                  "NamespaceResolveResult is polymorphic (the ToString/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `NamespaceResolveResult` is NOT sealed (no C# subclass derives from it but
    // it is unsealed), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::NamespaceResolveResult>,
                  "NamespaceResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
