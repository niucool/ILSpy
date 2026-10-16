// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
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

// Tests for `ICodeContext` (cpp/Decompiler/TypeSystem/ICodeContext.hpp, the port of
// ICSharpCode.Decompiler/TypeSystem/ICodeContext.cs) -- the lambda-body resolution
// context: an `ITypeResolveContext` (D409) plus the two code-level members
// (`LocalVariables` / `IsWithinLambdaExpression`). In the C# source the interface's only
// implementor is `CSharpResolver` (whose skeleton lands with this iteration; the
// derivation itself is deferred there -- see the CSharpResolver.hpp header convention
// (b)), so the tests pin the contract through a local stub the way the
// `ITypeResolveContext_Test` / `ArrayTypeReference_Test` stubs pin that interface's
// contract: virtual dispatch of both new members through an `ICodeContext*`, the
// inherited four-slot surface, the `With*` round trips through the interface's
// `std::unique_ptr<ITypeResolveContext>` signature, and the abstract-class shape.

#include "Decompiler/TypeSystem/ICodeContext.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::TypeSystem::ICodeContext;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeResolveContext;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;

LookupCompilation& Compilation() {
    static LookupCompilation compilation;
    return compilation;
}

// A minimal `IVariable` (the `LocalVariables` element type).
class TestVariable : public IVariable {
public:
    explicit TestVariable(std::string name)
        : name_(std::move(name)),
          type_(std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    const TS::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }

private:
    std::string name_;
    TS::ITypePtr type_;
};

// A trivial concrete `ICodeContext` -- the four inherited resolution slots (module /
// type definition / member + the compilation) plus the two code-level members, all
// configurable so the tests can pin the dispatch and the `With*` round trips.
class TestCodeContext final : public ICodeContext {
public:
    TestCodeContext(const TS::IModule* module,
                    const ITypeDefinition* typeDefinition,
                    const TS::IMember* member,
                    std::vector<std::shared_ptr<const IVariable>> localVariables,
                    bool isWithinLambdaExpression)
        : module_(module),
          typeDefinition_(typeDefinition),
          member_(member),
          localVariables_(std::move(localVariables)),
          isWithinLambdaExpression_(isWithinLambdaExpression) {}

    // ---- ICompilationProvider / ITypeResolveContext -----------------------------------
    // `::Compilation()` -- the D372 name-hiding crux: the `Compilation()` member hides
    // the file-local free factory inside the class body, so an unqualified call would
    // recurse into the member itself (C4717); the leading `::` reaches the anonymous-
    // namespace free function injected into the global scope (the DefaultParameter
    // stub precedent).
    const TS::ICompilation& Compilation() const override { return ::Compilation(); }

    const TS::IModule* CurrentModule() const override { return module_; }
    const ITypeDefinition* CurrentTypeDefinition() const override { return typeDefinition_; }
    const TS::IMember* CurrentMember() const override { return member_; }

    std::unique_ptr<TS::ITypeResolveContext> WithCurrentTypeDefinition(
        const ITypeDefinition* typeDefinition) const override {
        return std::unique_ptr<TS::ITypeResolveContext>(new TestCodeContext(
            module_, typeDefinition, member_, localVariables_, isWithinLambdaExpression_));
    }

    std::unique_ptr<TS::ITypeResolveContext> WithCurrentMember(
        const TS::IMember* member) const override {
        return std::unique_ptr<TS::ITypeResolveContext>(new TestCodeContext(
            module_, typeDefinition_, member, localVariables_, isWithinLambdaExpression_));
    }

    // ---- ICodeContext ------------------------------------------------------------------
    std::vector<std::shared_ptr<const IVariable>> LocalVariables() const override {
        return localVariables_;
    }

    bool IsWithinLambdaExpression() const override { return isWithinLambdaExpression_; }

private:
    const TS::IModule* module_;
    const ITypeDefinition* typeDefinition_;
    const TS::IMember* member_;
    std::vector<std::shared_ptr<const IVariable>> localVariables_;
    bool isWithinLambdaExpression_;
};

} // namespace

// The interface is an abstract base (a C# interface): it cannot be instantiated, and the
// two members are pure virtual.
static_assert(std::is_abstract_v<ICodeContext>);
static_assert(std::is_base_of_v<TS::ITypeResolveContext, ICodeContext>);

TEST(ICodeContextTest, LocalVariablesDispatchThroughInterface)
{
    auto x = std::make_shared<TestVariable>("x");
    auto y = std::make_shared<TestVariable>("y");
    std::vector<std::shared_ptr<const IVariable>> locals{x, y};

    TestCodeContext context(&Compilation().MainModule(), nullptr, nullptr,
                            std::move(locals), false);

    ICodeContext& interface = context;
    auto result = interface.LocalVariables();
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), x.get());
    EXPECT_EQ(result[1].get(), y.get());
}

TEST(ICodeContextTest, IsWithinLambdaExpressionDispatches)
{
    TestCodeContext inside(&Compilation().MainModule(), nullptr, nullptr, {}, true);
    TestCodeContext outside(&Compilation().MainModule(), nullptr, nullptr, {}, false);

    ICodeContext& insideInterface = inside;
    ICodeContext& outsideInterface = outside;
    EXPECT_TRUE(insideInterface.IsWithinLambdaExpression());
    EXPECT_FALSE(outsideInterface.IsWithinLambdaExpression());
}

TEST(ICodeContextTest, InheritedTypeResolveContextSurface)
{
    auto typeDefinition = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "CtxType", "",
        TS::FullTypeName(TS::TopLevelTypeName("", "CtxType", 0)),
        TS::TypeKind::Class, TS::Accessibility::Public, Compilation(), nullptr,
        TS::KnownTypeCode::None);

    TestCodeContext context(&Compilation().MainModule(), typeDefinition.get(), nullptr,
                            {}, false);

    // The four inherited slots are reachable through the `ICodeContext*` (the
    // `ITypeResolveContext` base).
    ICodeContext& interface = context;
    EXPECT_EQ(&interface.Compilation(), &Compilation());
    EXPECT_EQ(interface.CurrentModule(), &Compilation().MainModule());
    EXPECT_EQ(interface.CurrentTypeDefinition(), typeDefinition.get());
    EXPECT_EQ(interface.CurrentMember(), nullptr);
}

TEST(ICodeContextTest, WithFactoriesRoundTripThroughInterface)
{
    auto typeDefinition = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "CtxType", "",
        TS::FullTypeName(TS::TopLevelTypeName("", "CtxType", 0)),
        TS::TypeKind::Class, TS::Accessibility::Public, Compilation(), nullptr,
        TS::KnownTypeCode::None);
    auto x = std::make_shared<TestVariable>("x");

    TestCodeContext context(&Compilation().MainModule(), nullptr, nullptr,
                             std::vector<std::shared_ptr<const IVariable>>{x}, false);

    // The `WithCurrentTypeDefinition` round trip: the returned context carries the
    // replaced slot, the inherited carried-over slots, AND the code-level members (a
    // code context keeps its locals across a type-definition change).
    auto withType = context.WithCurrentTypeDefinition(typeDefinition.get());
    ASSERT_NE(withType, nullptr);
    EXPECT_EQ(withType->CurrentTypeDefinition(), typeDefinition.get());
    EXPECT_EQ(withType->CurrentModule(), &Compilation().MainModule());
    // `LocalVariables` lives on `ICodeContext`, not the `ITypeResolveContext` the
    // `With*` factories are declared on -- downcast to the known concrete
    // implementation to read it.
    auto* withTypeAsCodeContext = static_cast<ICodeContext*>(withType.get());
    auto locals = withTypeAsCodeContext->LocalVariables();
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(locals[0].get(), x.get());

    // The `WithCurrentMember` round trip on the interface-typed context.
    ITypeResolveContext& interface = context;
    auto withMember = interface.WithCurrentMember(nullptr);
    ASSERT_NE(withMember, nullptr);
    EXPECT_EQ(withMember->CurrentMember(), (const TS::IMember*)nullptr);
}
