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

// Port of ICSharpCode.Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.cs -- the
// C#-flavoured type resolve context: the four resolution slots of the ported
// `ITypeResolveContext` interface (module / using scope / type definition / member)
// plus the C#-specific `CurrentUsingScope` slot the CSharpResolver's name-resolution
// arms walk. The CSharpResolver holds one as its `context` field (the resolver ctor
// builds `new CSharpTypeResolveContext(compilation.MainModule)`), and every `With*`
// factory produces a NEW context with one slot replaced (the immutable-context
// pattern: `WithCurrentTypeDefinition` / `WithCurrentMember` / `WithUsingScope`).
//
// This is one half of the mutually-dependent CSharp/TypeSystem pair (with `UsingScope`,
// which holds the context it was created against): the two headers forward-declare each
// other and the two .cpps include each other's header -- no include cycle.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `sealed class CSharpTypeResolveContext : ITypeResolveContext` ports to
//      a C++ `final` class deriving the ported `ITypeResolveContext` (D409). The three
//      nullable C# slots port to nullable pointers / an empty `shared_ptr`:
//      `IModule? CurrentModule`'s STORAGE is the non-null `const IModule*` (the ctor
//      takes `const IModule&`, the D374 non-null-reference convention -- the C#
//      ArgumentNullException on a null module is structurally unreachable);
//      `ITypeDefinition? CurrentTypeDefinition` / `IMember? CurrentMember` port to
//      nullable non-owning `const` pointers (the compilation owns the entities, the
//      `IEntity::ParentModule` nullable-pointer precedent); `UsingScope
//      CurrentUsingScope` ports to an owning `std::shared_ptr<UsingScope>` (the scope
//      is heap-allocated resolver-side state shared by every context that references
//      it, and the scope is MUTABLE through the handle -- the resolver fills its
//      ResolveCache through this slot).
//  (b) The C# `With*` public methods return the covariant `CSharpTypeResolveContext`;
//      C++ has no covariant returns for `std::unique_ptr`, so the port splits the C#
//      public-vs-explicit-interface pair the way `SimpleTypeResolveContext` (D409) did:
//      `WithCurrentTypeDefinition` / `WithCurrentMember` keep the interface signature
//      (`std::unique_ptr<ITypeResolveContext>`, nullable pointer parameter) -- the
//      override IS the C# public method for those two; `WithUsingScope` (NOT on the
//      interface) keeps the concrete return type (`std::shared_ptr
//      <CSharpTypeResolveContext>`), which is the shape the `UsingScope::WithNested
//      Namespace` factory consumes. A future CSharpResolver slice can reach the
//      concrete type from the interface returns by `static_cast` (the class is
//      `final`, so the downcast is safe) or by adding typed convenience overloads then.
//  (c) The C# private ctor + the `With*` factories mirror `SimpleTypeResolveContext`'s
//      private-ctor pattern: the private ctor stays private and the factories use
//      `new` (a `std::unique_ptr`/`std::shared_ptr` from `new` -- `make_shared` cannot
//      reach a private ctor, the IntersectionType::Create precedent).
//  (d) The C# `readonly string[] methodTypeParameterNames` field is carried for shape
//      fidelity (`std::vector<std::string>`, threaded through the private ctor and the
//      `With*` factories exactly as the C# threads it) even though NO C# code ever
//      reads it and no reachable construction sets it non-null (the public ctor leaves
//      it null; only the `With*` factories pass it, always passing the current --
//      always-null -- value through): it is provably always empty, like the C#
//      `CSharpConversions.explicitConversionCache` vestigial-field precedent the port
//      also carried.
//  (e) `Compilation` is inherited from `ICompilationProvider` through the interface
//      chain; the override delegates to `module->Compilation()` (an `IModule` IS an
//      `ICompilationProvider`). No name hiding: none of the accessors collide with a
//      namespace-scope type name in this or the sibling namespaces.

#pragma once

#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::TypeSystem {

class UsingScope;  // the mutable per-scope state held in the CurrentUsingScope slot

// The C# `public sealed class CSharpTypeResolveContext : ITypeResolveContext`.
class CSharpTypeResolveContext final : public ILSpy::Decompiler::TypeSystem::ITypeResolveContext {
public:
    ~CSharpTypeResolveContext() override = default;

    // The C# public ctor `CSharpTypeResolveContext(IModule module, UsingScope usingScope
    // = null, ITypeDefinition typeDefinition = null, IMember member = null)` -- the C#
    // null-module guard is structurally unreachable through the `const IModule&`
    // parameter (the D374 convention). The three nullable slots default to their null
    // values (an empty `shared_ptr` / null pointers).
    CSharpTypeResolveContext(
        const ILSpy::Decompiler::TypeSystem::IModule& module,
        std::shared_ptr<UsingScope> usingScope = nullptr,
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition = nullptr,
        const ILSpy::Decompiler::TypeSystem::IMember* member = nullptr);

    // The C# `public UsingScope CurrentUsingScope { get; }` -- the current using scope
    // (nullable). Returns the owning `shared_ptr` handle so the resolver can MUTATE the
    // scope through it (the ResolveCache / AllExtensionMethods slots); `const` because
    // the getter only reads the slot.
    std::shared_ptr<UsingScope> CurrentUsingScope() const { return currentUsingScope_; }

    // ---- The `ITypeResolveContext` surface ------------------------------------------------
    // The C# `public ICompilation Compilation { get { return module.Compilation; } }`.
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override;

    // The C# `public IModule CurrentModule { get; }` (non-null in practice; the
    // interface declares it nullable, so the port keeps the nullable pointer return).
    const ILSpy::Decompiler::TypeSystem::IModule* CurrentModule() const override
    {
        return module_;
    }

    // The C# `public ITypeDefinition CurrentTypeDefinition { get; }` (nullable).
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition() const override
    {
        return currentTypeDefinition_;
    }

    // The C# `public IMember CurrentMember { get; }` (nullable).
    const ILSpy::Decompiler::TypeSystem::IMember* CurrentMember() const override
    {
        return currentMember_;
    }

    // The C# public `CSharpTypeResolveContext WithCurrentTypeDefinition(ITypeDefinition
    // typeDefinition)` (whose explicit interface twin returns the interface): returns a
    // NEW context with the type-definition slot replaced, the other slots (module /
    // using scope / member / method type parameter names) carried over. The D409
    // `SimpleTypeResolveContext` signature convention (`std::unique_ptr
    // <ITypeResolveContext>`, nullable pointer parameter -- pass `nullptr` to clear the
    // slot). `const` (reads `this` without modifying it).
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
    WithCurrentTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition) const override;

    // The C# public `CSharpTypeResolveContext WithCurrentMember(IMember member)` (the
    // same covariant-split shape as `WithCurrentTypeDefinition`).
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext> WithCurrentMember(
        const ILSpy::Decompiler::TypeSystem::IMember* member) const override;

    // The C# `public CSharpTypeResolveContext WithUsingScope(UsingScope usingScope)` --
    // NOT on the `ITypeResolveContext` interface, so the concrete return type is free:
    // an owning `shared_ptr<CSharpTypeResolveContext>` (the `UsingScope::WithNested
    // Namespace` factory consumes exactly this shape). The nullable C# parameter ports
    // to an empty-able `shared_ptr`.
    std::shared_ptr<CSharpTypeResolveContext> WithUsingScope(
        std::shared_ptr<UsingScope> usingScope) const;

private:
    // The C# private full ctor the `With*` factories use (the SimpleTypeResolveContext
    // private-ctor pattern; `make_shared` cannot reach it, so the factories use `new`).
    CSharpTypeResolveContext(
        const ILSpy::Decompiler::TypeSystem::IModule& module,
        std::shared_ptr<UsingScope> usingScope,
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition,
        const ILSpy::Decompiler::TypeSystem::IMember* member,
        std::vector<std::string> methodTypeParameterNames);

    const ILSpy::Decompiler::TypeSystem::IModule* module_;
    std::shared_ptr<UsingScope> currentUsingScope_;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* currentTypeDefinition_;
    const ILSpy::Decompiler::TypeSystem::IMember* currentMember_;
    // The C# `readonly string[] methodTypeParameterNames` -- carried for shape fidelity
    // (header convention (d)); provably always empty in every reachable construction.
    std::vector<std::string> methodTypeParameterNames_;
};

} // namespace ILSpy::Decompiler::CSharp::TypeSystem
