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

// Tests for `CSharpTypeResolveContext` (CSharpTypeResolveContext.cs): the ctor slots
// (module / using scope / type definition / member, with their C# null defaults), the
// `Compilation` delegation through the module, and the three `With*` factories
// (`WithCurrentTypeDefinition` / `WithCurrentMember` through the interface signature,
// `WithUsingScope` through the concrete return type).
//
// The load-bearing cruxes: (1) every `With*` produces a NEW context with exactly one
// slot replaced, the others carried over, and leaves the ORIGINAL context unchanged
// (the immutable-context pattern the CSharpResolver's cloning depends on); (2) the
// `WithCurrentTypeDefinition` / `WithCurrentMember` overrides keep the D409 interface
// signature (`std::unique_ptr<ITypeResolveContext>`), so the concrete type is recovered
// by a `static_cast` downcast (safe: the class is `final`) -- exactly what the future
// CSharpResolver slice does; (3) `WithUsingScope` keeps the CONCRETE return type (the
// `UsingScope::WithNestedNamespace` consumer's shape).

#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;

// The per-test scaffolding: a stack-local LookupCompilation (never moved; its LookupModule
// holds a reference to it), and a `LookupTypeDefinition` factory for the type-definition
// slot (the CSharpConversionsNumeric_Test.cpp MakeDef convention).
struct ContextFixture {
    TS::TestSupport::LookupCompilation compilation;
    TS::TestSupport::TestNamespace ns;

    ContextFixture() : ns(compilation) {}

    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> MakeTypeDef()
    {
        return std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "T", "", TS::FullTypeName(TS::TopLevelTypeName("", "T", 0)),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr);
    }

    std::shared_ptr<TS::TestSupport::LookupMethod> MakeMember()
    {
        return std::make_shared<TS::TestSupport::LookupMethod>("M", compilation);
    }
};

// The D409 interface-signature recovery: the class is `final`, so the downcast from the
// interface return is safe (the shape the future CSharpResolver slice uses).
std::shared_ptr<CSharpTypeResolveContext> Concrete(
    std::unique_ptr<TS::ITypeResolveContext> context)
{
    return std::shared_ptr<CSharpTypeResolveContext>(
        static_cast<CSharpTypeResolveContext*>(context.release()));
}

// The slot-identity helpers: EXPECT_EQ over a `const ITypeDefinition*` accessor and a
// `LookupTypeDefinition*` (or the IMember pair) does not deduce (the mixed-pointer-type
// EqHelper trap), so every identity check goes through the interface-typed helper.
const TS::ITypeDefinition* TD(const std::shared_ptr<TS::TestSupport::LookupTypeDefinition>& p)
{
    return p.get();
}
const TS::IMember* MB(const std::shared_ptr<TS::TestSupport::LookupMethod>& p)
{
    return p.get();
}

TEST(CSharpTypeResolveContextTest, ClassShape)
{
    // The C# `public sealed class CSharpTypeResolveContext : ITypeResolveContext`.
    static_assert(std::is_final_v<CSharpTypeResolveContext>);
    static_assert(std::is_base_of_v<TS::ITypeResolveContext, CSharpTypeResolveContext>);
    static_assert(std::is_polymorphic_v<CSharpTypeResolveContext>);
    SUCCEED();
}

TEST(CSharpTypeResolveContextTest, CtorDefaultsTheNullableSlots)
{
    ContextFixture f;
    auto context = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    EXPECT_EQ(context->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(context->CurrentUsingScope(), nullptr);
    EXPECT_EQ(context->CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(context->CurrentMember(), nullptr);
}

TEST(CSharpTypeResolveContextTest, CtorStoresTheExplicitSlots)
{
    ContextFixture f;
    auto td = f.MakeTypeDef();
    auto member = f.MakeMember();
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(
        outerContext, f.ns, std::vector<const TS::INamespace*>{});
    auto context = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(), scope,
                                                              TD(td), MB(member));
    EXPECT_EQ(context->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(context->CurrentUsingScope().get(), scope.get());
    EXPECT_EQ(context->CurrentTypeDefinition(), TD(td));
    EXPECT_EQ(context->CurrentMember(), MB(member));
}

TEST(CSharpTypeResolveContextTest, CompilationDelegatesToTheModule)
{
    // The C# `Compilation { get { return module.Compilation; } }` -- the module is an
    // ICompilationProvider bound to its compilation.
    ContextFixture f;
    auto context = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    EXPECT_EQ(&context->Compilation(), static_cast<const TS::ICompilation*>(&f.compilation));
}

TEST(CSharpTypeResolveContextTest, WithCurrentTypeDefinitionReplacesTheSlot)
{
    ContextFixture f;
    auto tdA = f.MakeTypeDef();
    auto tdB = f.MakeTypeDef();
    auto member = f.MakeMember();
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(
        outerContext, f.ns, std::vector<const TS::INamespace*>{});
    auto context = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(), scope,
                                                              TD(tdA), MB(member));
    auto replaced = Concrete(context->WithCurrentTypeDefinition(TD(tdB)));
    ASSERT_NE(replaced, nullptr);
    EXPECT_NE(replaced.get(), context.get()); // a NEW context, not a mutation
    EXPECT_EQ(replaced->CurrentTypeDefinition(), TD(tdB)); // the replaced slot
    // The other slots are carried over.
    EXPECT_EQ(replaced->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(replaced->CurrentUsingScope().get(), scope.get());
    EXPECT_EQ(replaced->CurrentMember(), MB(member));
}

TEST(CSharpTypeResolveContextTest, WithCurrentMemberReplacesTheSlot)
{
    ContextFixture f;
    auto td = f.MakeTypeDef();
    auto memberA = f.MakeMember();
    auto memberB = f.MakeMember();
    auto context =
        std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(), nullptr,
                                                   TD(td), MB(memberA));
    auto replaced = Concrete(context->WithCurrentMember(MB(memberB)));
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->CurrentMember(), MB(memberB)); // the replaced slot
    EXPECT_EQ(replaced->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(replaced->CurrentTypeDefinition(), TD(td)); // carried over
    EXPECT_EQ(replaced->CurrentUsingScope(), nullptr);
}

TEST(CSharpTypeResolveContextTest, WithUsingScopeReplacesTheSlot)
{
    // `WithUsingScope` keeps the CONCRETE return type (no interface twin -- the shape
    // `UsingScope::WithNestedNamespace` consumes).
    ContextFixture f;
    auto td = f.MakeTypeDef();
    auto member = f.MakeMember();
    auto context = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(), nullptr,
                                                              TD(td), MB(member));
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(
        outerContext, f.ns, std::vector<const TS::INamespace*>{});
    auto replaced = context->WithUsingScope(scope);
    ASSERT_NE(replaced, nullptr);
    EXPECT_NE(replaced.get(), context.get());
    EXPECT_EQ(replaced->CurrentUsingScope().get(), scope.get()); // the replaced slot
    // The other slots are carried over.
    EXPECT_EQ(replaced->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(replaced->CurrentTypeDefinition(), TD(td));
    EXPECT_EQ(replaced->CurrentMember(), MB(member));
}

TEST(CSharpTypeResolveContextTest, WithStarLeaveTheOriginalUnchanged)
{
    // The immutable-context crux: after any `With*` call, the original context keeps
    // every slot it had (the CSharpResolver clones freely without aliasing hazards).
    ContextFixture f;
    auto tdA = f.MakeTypeDef();
    auto tdB = f.MakeTypeDef();
    auto memberA = f.MakeMember();
    auto memberB = f.MakeMember();
    auto context =
        std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(), nullptr,
                                                   TD(tdA), MB(memberA));
    (void)Concrete(context->WithCurrentTypeDefinition(TD(tdB)));
    (void)Concrete(context->WithCurrentMember(MB(memberB)));
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(
        outerContext, f.ns, std::vector<const TS::INamespace*>{});
    (void)context->WithUsingScope(scope);
    EXPECT_EQ(context->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(context->CurrentUsingScope(), nullptr);
    EXPECT_EQ(context->CurrentTypeDefinition(), TD(tdA));
    EXPECT_EQ(context->CurrentMember(), MB(memberA));
}

TEST(CSharpTypeResolveContextTest, WithFactoriesCompose)
{
    // A WithCurrentTypeDefinition then WithCurrentMember chain: both replaced slots are
    // set on the composed result, and the chain carries the module and scope along.
    ContextFixture f;
    auto tdA = f.MakeTypeDef();
    auto tdB = f.MakeTypeDef();
    auto memberA = f.MakeMember();
    auto memberB = f.MakeMember();
    auto outerContext = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    auto scope = std::make_shared<UsingScope>(
        outerContext, f.ns, std::vector<const TS::INamespace*>{});
    auto context = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(), scope,
                                                              TD(tdA), MB(memberA));
    auto first = Concrete(context->WithCurrentTypeDefinition(TD(tdB)));
    auto composed = Concrete(first->WithCurrentMember(MB(memberB)));
    EXPECT_EQ(composed->CurrentTypeDefinition(), TD(tdB));
    EXPECT_EQ(composed->CurrentMember(), MB(memberB));
    EXPECT_EQ(composed->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(composed->CurrentUsingScope().get(), scope.get());
}

TEST(CSharpTypeResolveContextTest, DispatchesThroughTheInterfaceBasePointer)
{
    // The `ITypeResolveContext` surface is live through a base pointer: the accessors
    // and both interface `With*` factories dispatch polymorphically to this class.
    ContextFixture f;
    auto td = f.MakeTypeDef();
    auto member = f.MakeMember();
    auto contextPtr = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule(),
                                                                 nullptr, TD(td), MB(member));
    TS::ITypeResolveContext* base = contextPtr.get();
    EXPECT_EQ(base->CurrentModule(), &f.compilation.MainModule());
    EXPECT_EQ(base->CurrentTypeDefinition(), TD(td));
    EXPECT_EQ(base->CurrentMember(), MB(member));
    EXPECT_EQ(&base->Compilation(), static_cast<const TS::ICompilation*>(&f.compilation));
    auto memberB = f.MakeMember();
    auto replacedBase = base->WithCurrentMember(MB(memberB));
    ASSERT_NE(replacedBase, nullptr);
    auto replaced = Concrete(std::move(replacedBase));
    EXPECT_EQ(replaced->CurrentMember(), MB(memberB));
    EXPECT_EQ(replaced->CurrentTypeDefinition(), TD(td));
    // Clearing a slot through the nullable pointer parameter (the C# null typeDefinition).
    auto cleared = Concrete(contextPtr->WithCurrentTypeDefinition(nullptr));
    EXPECT_EQ(cleared->CurrentTypeDefinition(), nullptr);
}

TEST(CSharpTypeResolveContextTest, InterfaceDefaultHoldsNoState)
{
    // The context carries no state of its own beyond the four slots: two contexts
    // constructed with the same arguments are distinct objects (no hidden singletons).
    ContextFixture f;
    auto a = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    auto b = std::make_shared<CSharpTypeResolveContext>(f.compilation.MainModule());
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(a->CurrentUsingScope(), nullptr);
    EXPECT_EQ(b->CurrentUsingScope(), nullptr);
}

} // namespace
