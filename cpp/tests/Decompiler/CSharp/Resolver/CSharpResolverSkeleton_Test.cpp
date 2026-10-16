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

// Tests for the CSharpResolver class skeleton (cpp/Decompiler/CSharp/Resolver/
// CSharpResolver.{hpp,cpp}, the port of CSharpResolver.cs lines 49-322): the two public
// ctors, the immutable `With*` clone factories, the per-CurrentTypeDefinition cache, the
// `ImmutableStack`-based local-variable management, and the object-initializer context.
//
// The load-bearing cruxes:
//  (a) the IMMUTABLE-RESOLVER pattern -- every `With*` / `AddVariables` /
//      `PushObjectInitializer` returns a CLONE; the original resolver's state is never
//      mutated (the parent's locals stay intact after `AddVariables`, the original
//      member/scope/definition slots stay intact after each `With*`);
//  (b) the two IDENTITY-PRESERVING early-outs -- `WithCheckForOverflow` with the same
//      flag and `WithCurrentTypeDefinition` with the same definition return `this`
//      (pointer identity through `shared_from_this`), while `WithIsWithinLambdaExpression`
//      has NO early-out in the C# (a fresh clone even for the same flag);
//  (c) the context-slot delegation -- `CurrentMember` / `CurrentUsingScope` /
//      `CurrentTypeDefinition` read the CONTEXT's slots, and the clone factories replace
//      exactly one slot while carrying the others (module identity, using-scope identity);
//  (d) the LOCAL-VARIABLE STACK order -- `LocalVariables` flattens the immutable stack
//      TOP-FIRST (the innermost block's variables come first, the LIFO enumeration of
//      `ImmutableStack`), and clones share the caller's dictionary handle;
//  (e) the OBJECT-INITIALIZER stack -- pushes nest (the current is the innermost), pops
//      unwind to the enclosing one, and the null-object sentinels: no initializer open
//      means the `ErrorResolveResult.UnknownError` singleton (reference identity) and
//      its `UnknownType`;
//  (f) the ctor wiring -- the compilation ctor builds the context over the compilation's
//      MAIN MODULE and resolves the conversions through the per-compilation
//      `CSharpConversions::Get` factory (reference identity with a second `Get` call);
//      the context ctor stores the given context and builds the per-type-definition
//      cache when the context carries a current type definition (the cache itself is
//      private with no skeleton-level observable -- it becomes observable when the
//      `ResolveSimpleName` arms land; the delegated `CurrentTypeDefinition` accessor is
//      what the tests pin here).
//
// The resolver must be SHARED-MANAGED (`std::make_shared`) because the two
// identity-preserving early-outs go through `enable_shared_from_this` (the header
// convention (a)); every test constructs resolvers via `make_shared` for that reason.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::VariableMap;
using ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext;
using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
using ILSpy::Decompiler::Semantics::ErrorResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IVariable;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// The shared compilation (a plain `LookupCompilation` -- no `FindType` registration is
// needed for the skeleton; the conversions resolve over it and the contexts are built
// over its main module). The per-compilation `CSharpConversions::Get` factory stores its
// instance in the compilation's CacheManager, so the compilation must outlive every
// resolver constructed over it (it does -- a function-local static).
LookupCompilation& Compilation() {
    static LookupCompilation compilation;
    return compilation;
}

// A fresh resolver over the shared compilation (the `make_shared` discipline the two
// identity-preserving early-outs require).
std::shared_ptr<CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

// A primitive type handle (a `KnownType` is not an `ITypeDefinition`, but the skeleton's
// object-initializer tests only read `Kind()` off it -- the `KnownType(Int32)` maps to
// `TypeKind::Struct`).
ITypePtr IntType() {
    return std::make_shared<KnownType>(KnownTypeCode::Int32);
}

ITypePtr StringType() {
    return std::make_shared<KnownType>(KnownTypeCode::String);
}

// A minimal `IVariable` for the local-variable-stack tests (name + type; the C#
// `ILocalVariable` instances the `AddVariables` callers push).
class TestVariable : public IVariable {
public:
    TestVariable(std::string name, ITypePtr type)
        : name_(std::move(name)), type_(std::move(type)) {}

    // The D372 name-hiding crux: the `SymbolKind()` member hides the namespace-scope
    // `SymbolKind` enum inside the class body, so the return type must be fully
    // qualified (`TS::SymbolKind`, the TestParameter/TestVariable stub convention).
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Variable; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }

private:
    std::string name_;
    ITypePtr type_;
};

std::shared_ptr<TestVariable> MakeVariable(std::string name, ITypePtr type) {
    return std::make_shared<TestVariable>(std::move(name), std::move(type));
}

// A one-entry variable map (a block's `Dictionary<string, IVariable>`).
std::shared_ptr<const VariableMap> MakeMap(std::shared_ptr<TestVariable> variable) {
    auto map = std::make_shared<VariableMap>();
    map->emplace(variable->Name(), std::move(variable));
    return map;
}

// A `LookupTypeDefinition` for the `CurrentTypeDefinition` slot (the `MakeDef`
// precedent).
std::shared_ptr<LookupTypeDefinition> MakeTypeDef() {
    return std::make_shared<LookupTypeDefinition>(
        "ResolverTestType", "",
        FullTypeName(TopLevelTypeName("", "ResolverTestType", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
}

// A context over the shared compilation's main module (the shape the context-ctor
// tests and the `UsingScope` construction consume).
std::shared_ptr<CSharpTypeResolveContext> MakeContext() {
    return std::make_shared<CSharpTypeResolveContext>(Compilation().MainModule());
}

// A `ResolveResult` over a type (the pushed object initializer).
std::shared_ptr<ResolveResult> MakeResult(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

} // namespace

TEST(CSharpResolverSkeletonTest, CtorFromCompilationInitializesState)
{
    auto resolver = MakeResolver();

    EXPECT_FALSE(resolver->CheckForOverflow());
    EXPECT_FALSE(resolver->IsWithinLambdaExpression());
    EXPECT_EQ(resolver->CurrentTypeDefinition(), nullptr);
    EXPECT_EQ(resolver->CurrentMember(), nullptr);
    EXPECT_EQ(resolver->CurrentUsingScope(), nullptr);
    EXPECT_TRUE(resolver->LocalVariables().empty());
    EXPECT_FALSE(resolver->IsInObjectInitializer());
    // The null-object-initializer sentinel: the C# static `ErrorResult`
    // (`ErrorResolveResult.UnknownError`).
    EXPECT_EQ(&resolver->CurrentObjectInitializer(), &ErrorResolveResult::UnknownError());
    EXPECT_EQ(resolver->CurrentObjectInitializerType().Kind(), TypeKind::Unknown);
}

TEST(CSharpResolverSkeletonTest, CtorFromCompilationBuildsContextOverMainModule)
{
    auto resolver = MakeResolver();

    auto context = resolver->CurrentTypeResolveContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->CurrentModule(), &Compilation().MainModule());
    // The context handle is stable across reads (the resolver stores it, not a fresh
    // one per read).
    EXPECT_EQ(resolver->CurrentTypeResolveContext().get(), context.get());
}

TEST(CSharpResolverSkeletonTest, CtorFromCompilationResolvesConversionsViaGetFactory)
{
    auto resolver = MakeResolver();

    // The per-compilation factory: the resolver's conversions and a second `Get` call
    // resolve to the SAME instance (the CacheManager-backed singleton).
    EXPECT_EQ(&resolver->Conversions(), &CSharpConversions::Get(Compilation()));
}

TEST(CSharpResolverSkeletonTest, CtorFromNullContextThrows)
{
    std::shared_ptr<CSharpTypeResolveContext> nullContext;
    // The assignment keeps the `make_shared` result used (a discarded
    // smart-pointer return is C4858 under the repo's zero-warning build).
    std::shared_ptr<CSharpResolver> resolver;
    EXPECT_THROW(resolver = std::make_shared<CSharpResolver>(nullContext),
                 std::invalid_argument);
}

TEST(CSharpResolverSkeletonTest, CtorFromContextStoresContextAndCompilation)
{
    auto context = MakeContext();
    auto resolver = std::make_shared<CSharpResolver>(context);

    // The context is stored (shared ownership), not cloned.
    EXPECT_EQ(resolver->CurrentTypeResolveContext().get(), context.get());
    EXPECT_EQ(&resolver->Compilation(), &Compilation());
    // A context with no current type definition builds no cache -- the delegated
    // accessor reports the null slot.
    EXPECT_EQ(resolver->CurrentTypeDefinition(), nullptr);
}

TEST(CSharpResolverSkeletonTest, CtorFromContextWithCurrentTypeDefinitionExposesIt)
{
    auto typeDefinition = MakeTypeDef();
    auto context = std::make_shared<CSharpTypeResolveContext>(
        Compilation().MainModule(), nullptr, typeDefinition.get(), nullptr);
    auto resolver = std::make_shared<CSharpResolver>(context);

    // The C# ctor also builds the per-type-definition cache here; the cache is private
    // with no skeleton-level observable (its dictionaries become observable when the
    // `ResolveSimpleName` arms land) -- the delegated slot accessor is what pins the
    // wiring.
    EXPECT_EQ(resolver->CurrentTypeDefinition(), typeDefinition.get());
}

TEST(CSharpResolverSkeletonTest, WithCheckForOverflowReturnsCloneWithFlag)
{
    auto resolver = MakeResolver();

    auto clone = resolver->WithCheckForOverflow(true);
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), resolver.get());
    EXPECT_TRUE(clone->CheckForOverflow());
    // The clone carries the context (shared handle) and the conversions (shared
    // instance).
    EXPECT_EQ(clone->CurrentTypeResolveContext().get(),
              resolver->CurrentTypeResolveContext().get());
    EXPECT_EQ(&clone->Conversions(), &resolver->Conversions());
    // The original is unchanged (the immutable-resolver pattern).
    EXPECT_FALSE(resolver->CheckForOverflow());
}

TEST(CSharpResolverSkeletonTest, WithCheckForOverflowSameFlagReturnsThis)
{
    auto resolver = MakeResolver();

    // The identity-preserving early-out: `return this` when the flag is unchanged.
    auto same = resolver->WithCheckForOverflow(false);
    EXPECT_EQ(same.get(), resolver.get());
}

TEST(CSharpResolverSkeletonTest, WithIsWithinLambdaExpressionSetsFlagWithoutEarlyOut)
{
    auto resolver = MakeResolver();

    auto clone = resolver->WithIsWithinLambdaExpression(true);
    ASSERT_NE(clone, nullptr);
    EXPECT_TRUE(clone->IsWithinLambdaExpression());
    EXPECT_FALSE(resolver->IsWithinLambdaExpression());

    // The C# has no identity-preserving early-out here: even the SAME flag value
    // produces a fresh clone.
    auto same = clone->WithIsWithinLambdaExpression(true);
    ASSERT_NE(same, nullptr);
    EXPECT_NE(same.get(), clone.get());
}

TEST(CSharpResolverSkeletonTest, CurrentMemberDelegatesToContextSlot)
{
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    auto context = std::make_shared<CSharpTypeResolveContext>(
        Compilation().MainModule(), nullptr, nullptr, method.get());
    auto resolver = std::make_shared<CSharpResolver>(context);

    EXPECT_EQ(resolver->CurrentMember(), method.get());
}

TEST(CSharpResolverSkeletonTest, WithCurrentMemberReplacesSlotAndCarriesContext)
{
    auto resolver = MakeResolver();
    auto method = std::make_shared<LookupMethod>("M", Compilation());

    auto clone = resolver->WithCurrentMember(method.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->CurrentMember(), method.get());
    // The other context slots are carried (a NEW context with the member slot
    // replaced).
    ASSERT_NE(clone->CurrentTypeResolveContext().get(),
              resolver->CurrentTypeResolveContext().get());
    EXPECT_EQ(clone->CurrentTypeResolveContext()->CurrentModule(),
              &Compilation().MainModule());
    EXPECT_EQ(clone->CurrentTypeDefinition(), nullptr);
    // The original resolver is unchanged.
    EXPECT_EQ(resolver->CurrentMember(), nullptr);
}

TEST(CSharpResolverSkeletonTest, WithCurrentUsingScopeReplacesScope)
{
    auto resolver = MakeResolver();
    auto scope = std::make_shared<UsingScope>(
        MakeContext(), Compilation().RootNamespace(), std::vector<const TS::INamespace*>{});

    auto clone = resolver->WithCurrentUsingScope(scope);
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->CurrentUsingScope().get(), scope.get());
    // The original is unchanged.
    EXPECT_EQ(resolver->CurrentUsingScope(), nullptr);
}

TEST(CSharpResolverSkeletonTest, WithCurrentTypeDefinitionReplacesDefinition)
{
    auto resolver = MakeResolver();
    auto typeDefinition = MakeTypeDef();

    auto clone = resolver->WithCurrentTypeDefinition(typeDefinition.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->CurrentTypeDefinition(), typeDefinition.get());
    // The context slot was replaced through a NEW context; the member slot is carried
    // (null here).
    EXPECT_EQ(clone->CurrentMember(), nullptr);
    // The original is unchanged.
    EXPECT_EQ(resolver->CurrentTypeDefinition(), nullptr);
}

TEST(CSharpResolverSkeletonTest, WithCurrentTypeDefinitionSamePointerReturnsThis)
{
    auto typeDefinition = MakeTypeDef();
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(typeDefinition.get());
    ASSERT_NE(resolver, nullptr);

    // The identity-preserving early-out: `return this` when the definition is unchanged.
    auto same = resolver->WithCurrentTypeDefinition(typeDefinition.get());
    EXPECT_EQ(same.get(), resolver.get());
}

TEST(CSharpResolverSkeletonTest, WithCurrentTypeDefinitionNullClears)
{
    auto typeDefinition = MakeTypeDef();
    auto resolver = MakeResolver()->WithCurrentTypeDefinition(typeDefinition.get());
    ASSERT_NE(resolver, nullptr);

    auto cleared = resolver->WithCurrentTypeDefinition(nullptr);
    ASSERT_NE(cleared, nullptr);
    EXPECT_EQ(cleared->CurrentTypeDefinition(), nullptr);
}

TEST(CSharpResolverSkeletonTest, AddVariablesMakesVariablesVisible)
{
    auto resolver = MakeResolver();
    auto variable = MakeVariable("x", IntType());

    auto clone = resolver->AddVariables(MakeMap(variable));
    ASSERT_NE(clone, nullptr);
    auto locals = clone->LocalVariables();
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(locals[0].get(), variable.get());
    EXPECT_EQ(locals[0]->Name(), "x");
}

TEST(CSharpResolverSkeletonTest, AddVariablesStacksBlocksInnermostFirst)
{
    auto resolver = MakeResolver();
    auto outer = MakeVariable("outer", IntType());
    auto inner = MakeVariable("inner", StringType());

    auto afterOuter = resolver->AddVariables(MakeMap(outer));
    ASSERT_NE(afterOuter, nullptr);
    auto afterInner = afterOuter->AddVariables(MakeMap(inner));
    ASSERT_NE(afterInner, nullptr);

    // The immutable stack enumerates TOP-FIRST: the innermost block's variable comes
    // first (the C# `SelectMany` over `ImmutableStack`).
    auto locals = afterInner->LocalVariables();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_EQ(locals[0].get(), inner.get());
    EXPECT_EQ(locals[1].get(), outer.get());
}

TEST(CSharpResolverSkeletonTest, AddVariablesCarriesParentLocalsAndLeavesOriginalIntact)
{
    auto resolver = MakeResolver();
    auto first = MakeVariable("a", IntType());

    auto parent = resolver->AddVariables(MakeMap(first));
    ASSERT_NE(parent, nullptr);
    auto child = parent->AddVariables(MakeMap(MakeVariable("b", StringType())));
    ASSERT_NE(child, nullptr);

    // The child sees both blocks.
    EXPECT_EQ(child->LocalVariables().size(), 2u);
    // The immutable-resolver crux: the PARENT still sees only its own block (the push
    // produced a new stack, it did not mutate the parent's).
    EXPECT_EQ(parent->LocalVariables().size(), 1u);
    EXPECT_EQ(resolver->LocalVariables().size(), 0u);
}

TEST(CSharpResolverSkeletonTest, AddVariablesSharesTheCallerDictionary)
{
    auto resolver = MakeResolver();
    auto map = MakeMap(MakeVariable("shared", IntType()));

    // Two clones pushed with the SAME dictionary handle both see it (the C# pushes
    // the reference).
    auto first = resolver->AddVariables(map);
    ASSERT_NE(first, nullptr);
    auto second = resolver->AddVariables(map);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first->LocalVariables().size(), 1u);
    EXPECT_EQ(second->LocalVariables().size(), 1u);
    EXPECT_EQ(first->LocalVariables()[0].get(), second->LocalVariables()[0].get());
}

TEST(CSharpResolverSkeletonTest, AddNullVariablesThrows)
{
    auto resolver = MakeResolver();
    std::shared_ptr<const VariableMap> nullMap;

    EXPECT_THROW(resolver->AddVariables(nullMap), std::invalid_argument);
}

TEST(CSharpResolverSkeletonTest, PushObjectInitializerSetsCurrent)
{
    auto resolver = MakeResolver();
    auto result = MakeResult(IntType());

    auto clone = resolver->PushObjectInitializer(result);
    ASSERT_NE(clone, nullptr);
    EXPECT_TRUE(clone->IsInObjectInitializer());
    EXPECT_EQ(&clone->CurrentObjectInitializer(), result.get());
    EXPECT_EQ(clone->CurrentObjectInitializerType().Kind(), TypeKind::Struct);
    // The original is unchanged.
    EXPECT_FALSE(resolver->IsInObjectInitializer());
}

TEST(CSharpResolverSkeletonTest, PushObjectInitializerTwiceNests)
{
    auto resolver = MakeResolver();
    auto outer = MakeResult(IntType());
    auto inner = MakeResult(StringType());

    auto afterOuter = resolver->PushObjectInitializer(outer);
    ASSERT_NE(afterOuter, nullptr);
    auto afterInner = afterOuter->PushObjectInitializer(inner);
    ASSERT_NE(afterInner, nullptr);

    // The current initializer is the innermost push.
    EXPECT_EQ(&afterInner->CurrentObjectInitializer(), inner.get());

    // Popping unwinds to the enclosing initializer.
    auto popped = afterInner->PopObjectInitializer();
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(&popped->CurrentObjectInitializer(), outer.get());
}

TEST(CSharpResolverSkeletonTest, PopObjectInitializerOnEmptyThrows)
{
    auto resolver = MakeResolver();

    EXPECT_THROW(resolver->PopObjectInitializer(), std::runtime_error);
}

TEST(CSharpResolverSkeletonTest, PopObjectInitializerToEmptyClearsInitializer)
{
    auto resolver = MakeResolver()->PushObjectInitializer(MakeResult(IntType()));
    ASSERT_NE(resolver, nullptr);

    auto popped = resolver->PopObjectInitializer();
    ASSERT_NE(popped, nullptr);
    EXPECT_FALSE(popped->IsInObjectInitializer());
    // The null-object sentinels: the UnknownError singleton and its UnknownType.
    EXPECT_EQ(&popped->CurrentObjectInitializer(), &ErrorResolveResult::UnknownError());
    EXPECT_EQ(popped->CurrentObjectInitializerType().Kind(), TypeKind::Unknown);
}

TEST(CSharpResolverSkeletonTest, PushNullObjectInitializerThrows)
{
    auto resolver = MakeResolver();
    std::shared_ptr<ResolveResult> nullResult;

    EXPECT_THROW(resolver->PushObjectInitializer(nullResult), std::invalid_argument);
}

TEST(CSharpResolverSkeletonTest, CloneCarriesObjectInitializerStack)
{
    auto result = MakeResult(IntType());
    auto resolver = MakeResolver()->PushObjectInitializer(result);
    ASSERT_NE(resolver, nullptr);

    // Every clone factory threads the object-initializer stack through the private
    // full ctor -- a flag clone keeps the open initializer (and its current result).
    auto clone = resolver->WithCheckForOverflow(true);
    ASSERT_NE(clone, nullptr);
    EXPECT_TRUE(clone->IsInObjectInitializer());
    EXPECT_EQ(&clone->CurrentObjectInitializer(), result.get());

    // The local-variable stack carries the same way.
    auto withVariables = clone->AddVariables(MakeMap(MakeVariable("v", IntType())));
    ASSERT_NE(withVariables, nullptr);
    EXPECT_TRUE(withVariables->IsInObjectInitializer());
}

TEST(CSharpResolverSkeletonTest, WithChainComposesState)
{
    auto typeDefinition = MakeTypeDef();
    auto method = std::make_shared<LookupMethod>("M", Compilation());
    auto variable = MakeVariable("x", IntType());
    auto initialized = MakeResult(StringType());

    // The flagship: a resolver built through a chain of every clone factory carries
    // all the state simultaneously.
    auto resolver = MakeResolver()
                        ->WithCheckForOverflow(true)
                        ->WithCurrentTypeDefinition(typeDefinition.get())
                        ->WithCurrentMember(method.get())
                        ->AddVariables(MakeMap(variable))
                        ->PushObjectInitializer(initialized);
    ASSERT_NE(resolver, nullptr);

    EXPECT_TRUE(resolver->CheckForOverflow());
    EXPECT_EQ(resolver->CurrentTypeDefinition(), typeDefinition.get());
    EXPECT_EQ(resolver->CurrentMember(), method.get());
    ASSERT_EQ(resolver->LocalVariables().size(), 1u);
    EXPECT_EQ(resolver->LocalVariables()[0].get(), variable.get());
    EXPECT_EQ(&resolver->CurrentObjectInitializer(), initialized.get());
}
