// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `UsingScope` (UsingScope.cs): the ctor surface (the null-context throw and
// the stored namespace/usings), the `Parent` context-chain read (a scope's parent is the
// scope in its parent CONTEXT's CurrentUsingScope slot, not a stored sibling pointer),
// the always-empty alias surface, the `ResolveCache` first-writer-wins memoization, the
// `AllExtensionMethods` LazyInit field contract, and the `WithNestedNamespace` factory
// (the metadata-child resolution vs. the `DummyNamespace` fallback, whose FullName is
// the `NamespaceDeclaration.BuildQualifiedName` join onto the parent's full name).
//
// The load-bearing cruxes: (1) `Parent` reads THROUGH the parent context -- a scope
// created against a context with no using-scope slot reports null, while the
// `WithNestedNamespace` child (created against `parentContext.WithUsingScope(this)`)
// reports the enclosing scope; (2) `WithNestedNamespace` resolves the METADATA child
// when `GetChildNamespace` finds one and falls back to the internal `DummyNamespace`
// only when it does not (the dummy carries the simple name, the qualified name, and the
// parent namespace); (3) the ResolveCache `TryAdd` is first-writer-wins (a later add on
// the same key does NOT overwrite).

#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::Semantics::ResolveResult;

// A configurable `INamespace` stub: a fixed full name plus a name->namespace child map
// the `WithNestedNamespace` metadata-child arm consults (each test file carries its own
// stubs; the TestSupport::TestNamespace is hardcoded empty and cannot exercise the
// child-resolution crux).
class TestNs : public TS::INamespace {
public:
    TestNs(const TS::ICompilation& compilation, std::string fullName)
        : compilation_(compilation), fullName_(std::move(fullName))
    {
    }

    void AddChildNamespace(const std::string& name, const TS::INamespace* child)
    {
        children_[name] = child;
    }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    std::string Name() const override { return fullName_; }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override { return compilation_; }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return fullName_; }
    const TS::INamespace* ParentNamespace() const override { return nullptr; }
    std::vector<const TS::INamespace*> ChildNamespaces() const override
    {
        std::vector<const TS::INamespace*> result;
        for (const auto& entry : children_)
            result.push_back(entry.second);
        return result;
    }
    std::vector<const TS::ITypeDefinition*> Types() const override { return {}; }
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    const TS::INamespace* GetChildNamespace(const std::string& name) const override
    {
        auto it = children_.find(name);
        return it == children_.end() ? nullptr : it->second;
    }
    const TS::ITypeDefinition* GetTypeDefinition(const std::string&, int) const override
    {
        return nullptr;
    }

private:
    const TS::ICompilation& compilation_;
    std::string fullName_;
    std::map<std::string, const TS::INamespace*> children_;
};

// A fresh `ResolveResult` value for the ResolveCache tests (a plain ResolveResult over
// a KnownType -- distinct instances so the stored-vs-overwritten identity checks are
// meaningful).
std::shared_ptr<ResolveResult> MakeResult()
{
    return std::make_shared<ResolveResult>(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32));
}

// The per-test scaffolding: a stack-local LookupCompilation (its LookupModule is bound to
// it; never moved), the root context over its main module, and a namespace to declare the
// scope against. The compilation is declared FIRST so it outlives the context and scopes.
struct ScopeFixture {
    TS::TestSupport::LookupCompilation compilation;
    std::shared_ptr<CSharpTypeResolveContext> context;
    TestNs ns;

    ScopeFixture()
        : context(std::make_shared<CSharpTypeResolveContext>(compilation.MainModule())),
          ns(compilation, "System")
    {
    }
};

TEST(UsingScopeTest, IsNotFinalAndIsSharedFromThisEnabled)
{
    // The C# `public class UsingScope` is unsealed; the enable_shared_from_this base is
    // the port's owning-handle mechanism for the WithNestedNamespace parent-context call.
    static_assert(!std::is_final_v<UsingScope>);
    static_assert(std::is_base_of_v<std::enable_shared_from_this<UsingScope>, UsingScope>);
    SUCCEED();
}

TEST(UsingScopeTest, CtorThrowsOnNullContext)
{
    ScopeFixture f;
    EXPECT_THROW((UsingScope(nullptr, f.ns, std::vector<const TS::INamespace*>{})),
                 std::invalid_argument);
}

TEST(UsingScopeTest, CtorStoresNamespaceAndUsingsSnapshot)
{
    ScopeFixture f;
    TestNs using1(f.compilation, "System.Linq");
    TestNs using2(f.compilation, "System.Collections");
    auto scope = std::make_shared<UsingScope>(
        f.context, f.ns, std::vector<const TS::INamespace*>{&using1, &using2});
    EXPECT_EQ(&scope->Namespace(), static_cast<const TS::INamespace*>(&f.ns));
    ASSERT_EQ(scope->Usings().size(), 2u);
    EXPECT_EQ(scope->Usings()[0], static_cast<const TS::INamespace*>(&using1));
    EXPECT_EQ(scope->Usings()[1], static_cast<const TS::INamespace*>(&using2));
}

TEST(UsingScopeTest, ParentIsNullWhenContextHasNoScope)
{
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    EXPECT_EQ(scope->Parent(), nullptr);
}

TEST(UsingScopeTest, ParentReturnsTheScopeInParentContext)
{
    // The load-bearing crux: `Parent` reads the parent CONTEXT's CurrentUsingScope
    // slot -- a scope created against a context whose slot holds another scope reports
    // THAT scope, whatever the sibling construction order.
    ScopeFixture f;
    auto scopeA = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    auto contextHoldingA = f.context->WithUsingScope(scopeA);
    auto scopeB = std::make_shared<UsingScope>(contextHoldingA, f.ns, std::vector<const TS::INamespace*>{});
    EXPECT_EQ(scopeB->Parent().get(), scopeA.get());
    // The original context's slot is untouched (the With* immutability).
    EXPECT_EQ(f.context->CurrentUsingScope(), nullptr);
    // And A itself, created against the slot-less context, still reports null.
    EXPECT_EQ(scopeA->Parent(), nullptr);
}

TEST(UsingScopeTest, AliasSurfaceIsAlwaysEmpty)
{
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    EXPECT_TRUE(scope->UsingAliases().empty());
    EXPECT_TRUE(scope->ExternAliases().empty());
    EXPECT_FALSE(scope->HasAlias("System"));
    EXPECT_FALSE(scope->HasAlias(""));
}

TEST(UsingScopeTest, ResolveCacheTryGetValueMissDefaultsTheOutParam)
{
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    std::shared_ptr<ResolveResult> out = MakeResult();
    EXPECT_FALSE(scope->ResolveCache.TryGetValue("missing", out));
    EXPECT_EQ(out, nullptr); // the C# out-param default on a miss
}

TEST(UsingScopeTest, ResolveCacheTryAddIsFirstWriterWins)
{
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    auto first = MakeResult();
    auto second = MakeResult();
    EXPECT_TRUE(scope->ResolveCache.TryAdd("identifier", first));
    std::shared_ptr<ResolveResult> out;
    EXPECT_TRUE(scope->ResolveCache.TryGetValue("identifier", out));
    EXPECT_EQ(out.get(), first.get());
    // The first writer wins: a later TryAdd on the same key fails and does NOT
    // overwrite the stored value.
    EXPECT_FALSE(scope->ResolveCache.TryAdd("identifier", second));
    EXPECT_TRUE(scope->ResolveCache.TryGetValue("identifier", out));
    EXPECT_EQ(out.get(), first.get());
}

TEST(UsingScopeTest, AllExtensionMethodsFollowsTheLazyInitContract)
{
    // The `AllExtensionMethods` field is the LazyInit `VolatileRead`/`GetOrSet` storage
    // the resolver's GetAllExtensionMethods populates (first writer wins, every later
    // call returns the cached instance).
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    EXPECT_EQ(ILSpy::Decompiler::Util::VolatileRead(&scope->AllExtensionMethods), nullptr);
    auto groups =
        std::make_shared<std::vector<std::vector<const TS::IMethod*>>>();
    auto stored = ILSpy::Decompiler::Util::GetOrSet(&scope->AllExtensionMethods, groups);
    EXPECT_EQ(stored.get(), groups.get());
    auto other = std::make_shared<std::vector<std::vector<const TS::IMethod*>>>();
    auto again = ILSpy::Decompiler::Util::GetOrSet(&scope->AllExtensionMethods, other);
    EXPECT_EQ(again.get(), groups.get()); // the first writer wins
    EXPECT_EQ(ILSpy::Decompiler::Util::VolatileRead(&scope->AllExtensionMethods).get(),
              groups.get());
}

TEST(UsingScopeTest, WithNestedNamespaceResolvesTheMetadataChild)
{
    ScopeFixture f;
    TestNs child(f.compilation, "System.Collections");
    f.ns.AddChildNamespace("Collections", &child);
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    auto nested = scope->WithNestedNamespace("Collections");
    ASSERT_NE(nested, nullptr);
    // The metadata child is used as-is (pointer identity), not a dummy.
    EXPECT_EQ(&nested->Namespace(), static_cast<const TS::INamespace*>(&child));
    // The child scope is created against parentContext.WithUsingScope(this), so its
    // Parent chain reaches the enclosing scope.
    EXPECT_EQ(nested->Parent().get(), scope.get());
    // The C# passes [] -- a nested namespace declaration imports no usings.
    EXPECT_TRUE(nested->Usings().empty());
}

TEST(UsingScopeTest, WithNestedNamespaceCreatesTheDummyFallbackNamespace)
{
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    auto nested = scope->WithNestedNamespace("Collections"); // no metadata child
    ASSERT_NE(nested, nullptr);
    const TS::INamespace& dummy = nested->Namespace();
    EXPECT_EQ(dummy.Name(), "Collections");
    // The FullName is the BuildQualifiedName join onto the parent's full name.
    EXPECT_EQ(dummy.FullName(), "System.Collections");
    EXPECT_EQ(dummy.ParentNamespace(), static_cast<const TS::INamespace*>(&f.ns));
    EXPECT_EQ(dummy.SymbolKind(), TS::SymbolKind::Namespace);
    EXPECT_EQ(dummy.ExternAlias(), "");
    // the parent namespace's compilation (the pointer-identity check goes through the
    // interface: a LookupCompilation* would not deduce against a const ICompilation*)
    EXPECT_EQ(&dummy.Compilation(), static_cast<const TS::ICompilation*>(&f.compilation));
    EXPECT_TRUE(dummy.ChildNamespaces().empty());
    EXPECT_TRUE(dummy.Types().empty());
    EXPECT_TRUE(dummy.ContributingModules().empty());
    EXPECT_EQ(dummy.GetChildNamespace("Anything"), nullptr);
    EXPECT_EQ(dummy.GetTypeDefinition("Anything", 0), nullptr);
}

TEST(UsingScopeTest, DummyUnderRootNamespaceHasTheBareSimpleName)
{
    // The BuildQualifiedName empty-side fold: a root parent (empty full name) yields
    // the bare simple name.
    ScopeFixture f;
    TestNs root(f.compilation, "");
    auto scope = std::make_shared<UsingScope>(f.context, root, std::vector<const TS::INamespace*>{});
    auto nested = scope->WithNestedNamespace("X");
    EXPECT_EQ(nested->Namespace().FullName(), "X");
}

TEST(UsingScopeTest, WithNestedNamespaceYieldsFreshScopesPerCall)
{
    ScopeFixture f;
    auto scope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    auto first = scope->WithNestedNamespace("X");
    auto second = scope->WithNestedNamespace("X");
    EXPECT_NE(first.get(), second.get());
    // The two fallback dummies are distinct objects with the same name.
    EXPECT_EQ(first->Namespace().Name(), second->Namespace().Name());
    EXPECT_NE(&first->Namespace(), &second->Namespace());
}

TEST(UsingScopeTest, NestedScopeChainsTheParentThroughNestedLevels)
{
    // A two-level chain: System -> System.Text -> System.Text.RegularExpressions, with
    // the middle level itself created via WithNestedNamespace (so the grandchild's
    // Parent is the middle scope, which itself resolves against the metadata child).
    ScopeFixture f;
    TestNs text(f.compilation, "System.Text");
    f.ns.AddChildNamespace("Text", &text);
    TestNs regularExpressions(f.compilation, "System.Text.RegularExpressions");
    text.AddChildNamespace("RegularExpressions", &regularExpressions);
    auto systemScope = std::make_shared<UsingScope>(f.context, f.ns, std::vector<const TS::INamespace*>{});
    auto textScope = systemScope->WithNestedNamespace("Text");
    auto regexScope = textScope->WithNestedNamespace("RegularExpressions");
    EXPECT_EQ(&regexScope->Namespace(),
              static_cast<const TS::INamespace*>(&regularExpressions));
    EXPECT_EQ(regexScope->Parent().get(), textScope.get());
    EXPECT_EQ(textScope->Parent().get(), systemScope.get());
    EXPECT_EQ(systemScope->Parent(), nullptr);
}

} // namespace
