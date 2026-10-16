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

// Implementation of `UsingScope` (see the header): the two ctors (the public one with
// the C# null-context guard, the private one carrying the owned-DummyNamespace handle),
// the `Parent` context-chain read, and the `WithNestedNamespace` factory with the
// private nested `DummyNamespace` (the empty fallback namespace a nested namespace
// declaration resolves to when the metadata has no corresponding child).

#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"

#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"  // NamespaceDeclaration::BuildQualifiedName
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"  // CurrentUsingScope / WithUsingScope
#include "Decompiler/TypeSystem/INamespace.hpp"  // INamespace (the Namespace slot type)

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp::TypeSystem {

// The `TS` alias for the sibling TypeSystem namespace is inherited from the header
// (it is declared there inside this namespace; re-declaring it here would be a
// redefinition).

// The C# `sealed class DummyNamespace : INamespace` (UsingScope.cs lines 99-134) -- the
// empty fallback namespace: an empty-bodied namespace declaration that has no
// corresponding metadata namespace resolves its nested scopes against one of these.
// `final` (the C# `sealed`); defined out-of-line here so the ~12 interface overrides
// stay out of the header (the class was declared as a private nested member there).
class UsingScope::DummyNamespace final : public TS::INamespace {
public:
    DummyNamespace(const TS::INamespace& parentNamespace, std::string name)
        : parentNamespace_(parentNamespace), name_(std::move(name))
    {
    }

    // --- ISymbol ---
    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Namespace`.
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Namespace; }
    // The C# `public string Name => name`.
    std::string Name() const override { return name_; }

    // --- ICompilationProvider ---
    // The C# `ICompilation ICompilationProvider.Compilation => parentNamespace.Compilation`.
    const TS::ICompilation& Compilation() const override { return parentNamespace_.Compilation(); }

    // --- INamespace ---
    // The C# `string INamespace.ExternAlias => "";`
    std::string ExternAlias() const override { return {}; }
    // The C# `string INamespace.FullName` -- the qualified name joined onto the parent
    // namespace's full name (the `NamespaceDeclaration.BuildQualifiedName` empty-side
    // fold: a root parent with an empty full name yields the bare simple name).
    std::string FullName() const override
    {
        return ILSpy::Decompiler::CSharp::Syntax::NamespaceDeclaration::BuildQualifiedName(
            parentNamespace_.FullName(), name_);
    }
    // The C# `INamespace INamespace.ParentNamespace => parentNamespace;` (non-null --
    // a reference member, the non-null-reference convention).
    const TS::INamespace* ParentNamespace() const override { return &parentNamespace_; }
    // The C# `IEnumerable<INamespace> INamespace.ChildNamespaces` (the C#
    // `EmptyList<INamespace>.Instance`) -- no child namespaces.
    std::vector<const TS::INamespace*> ChildNamespaces() const override { return {}; }
    // The C# `IEnumerable<ITypeDefinition> INamespace.Types` -- no types.
    std::vector<const TS::ITypeDefinition*> Types() const override { return {}; }
    // The C# `IEnumerable<IModule> INamespace.ContributingModules` -- no modules.
    std::vector<const TS::IModule*> ContributingModules() const override { return {}; }
    // The C# `INamespace? INamespace.GetChildNamespace(string name)` -- always null.
    const TS::INamespace* GetChildNamespace(const std::string&) const override
    {
        return nullptr;
    }
    // The C# `ITypeDefinition? INamespace.GetTypeDefinition(...)` -- always null.
    const TS::ITypeDefinition* GetTypeDefinition(const std::string&, int) const override
    {
        return nullptr;
    }

private:
    const TS::INamespace& parentNamespace_;
    std::string name_;
};

// The public ctor (the C# single ctor): both C# null guards live here -- the context
// guard throws (a `shared_ptr` parameter can be null), the namespace guard is
// structurally unreachable through the reference parameter (the header convention (c)).
UsingScope::UsingScope(std::shared_ptr<CSharpTypeResolveContext> context,
                       const TS::INamespace& namespace_,
                       std::vector<const TS::INamespace*> usings)
    : parentContext_(std::move(context)),
      namespace_(&namespace_),
      ownedNamespace_(nullptr),
      usings_(std::move(usings))
{
    // The C# `this.parentContext = context ?? throw new ArgumentNullException(...)` --
    // the init-then-throw-body convention (the DefaultParameter null-type precedent).
    if (!parentContext_) {
        throw std::invalid_argument("UsingScope: context must not be null");
    }
}

// The private ctor the `WithNestedNamespace` path takes: additionally carries the owning
// handle that keeps an internally-created `DummyNamespace` alive for the scope's
// lifetime (the header convention (c)). The context is non-null by construction (the
// `WithUsingScope` factory result), so no guard is needed here.
UsingScope::UsingScope(std::shared_ptr<CSharpTypeResolveContext> context,
                       const TS::INamespace& namespace_,
                       std::vector<const TS::INamespace*> usings,
                       std::shared_ptr<const TS::INamespace> ownedNamespace)
    : parentContext_(std::move(context)),
      namespace_(&namespace_),
      ownedNamespace_(std::move(ownedNamespace)),
      usings_(std::move(usings))
{
}

// The C# `public UsingScope Parent { get { return parentContext.CurrentUsingScope; } }`
// -- the enclosing scope read THROUGH the parent context's slot (the context chain,
// not a stored sibling pointer; the header convention (g)).
std::shared_ptr<UsingScope> UsingScope::Parent() const
{
    return parentContext_->CurrentUsingScope();
}

// The C# `internal UsingScope WithNestedNamespace(string simpleName)` -- resolve the
// metadata child namespace by its simple name, fall back to the `DummyNamespace` when
// the metadata has none, and create the child scope against
// `parentContext.WithUsingScope(this)` so the child's `Parent` chain reaches this scope.
std::shared_ptr<UsingScope> UsingScope::WithNestedNamespace(const std::string& simpleName) const
{
    const TS::INamespace* ns = namespace_->GetChildNamespace(simpleName);
    std::shared_ptr<const TS::INamespace> owned;
    if (ns == nullptr) {
        // The C# `?? new DummyNamespace(Namespace, simpleName)` -- the empty fallback
        // the scope keeps alive itself (the header convention (c)).
        owned = std::make_shared<DummyNamespace>(*namespace_, simpleName);
        ns = owned.get();
    }
    // The C# `parentContext.WithUsingScope(this)`: `this` is a const member function's
    // receiver, so the owning handle comes from `shared_from_this()` (a
    // `shared_ptr<const UsingScope>`) `const_pointer_cast`ed back to the mutable handle
    // (the underlying object is mutable; the const is the accessor contract, the D515
    // precedent). Every C#-reachable construction site creates the scope heap-allocated,
    // so the `enable_shared_from_this` handle is valid.
    std::shared_ptr<CSharpTypeResolveContext> childContext = parentContext_->WithUsingScope(
        std::const_pointer_cast<UsingScope>(shared_from_this()));
    // The C# passes `[]` -- a nested namespace declaration imports no usings of its own.
    // `new` (not `make_shared`): the private-ctor allocation happens outside the
    // member's access scope inside `make_shared`, the IntersectionType::Create precedent
    // (a member CAN reach the private ctor through a plain `new`).
    return std::shared_ptr<UsingScope>(
        new UsingScope(std::move(childContext), *ns, std::vector<const TS::INamespace*>{},
                       std::move(owned)));
}

} // namespace ILSpy::Decompiler::CSharp::TypeSystem
