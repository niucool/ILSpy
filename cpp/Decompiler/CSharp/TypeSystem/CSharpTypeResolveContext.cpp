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

// Implementation of `CSharpTypeResolveContext` (see the header): the two ctors (the
// public 4-slot one and the private full one the `With*` factories use), the
// `Compilation` delegation through the module, and the three `With*` factories.

#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"

#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"  // UsingScope (the CurrentUsingScope slot)
#include "Decompiler/TypeSystem/IModule.hpp"  // IModule::Compilation (the base-class chain)

#include <utility>

namespace ILSpy::Decompiler::CSharp::TypeSystem {

// The public ctor (the C# single public ctor): the C# null-module guard is structurally
// unreachable through the reference parameter (the header convention (a)); the three
// nullable slots keep their C# defaults. Delegates to the private full ctor so the
// always-empty `methodTypeParameterNames` (the header convention (d)) is threaded
// exactly once.
CSharpTypeResolveContext::CSharpTypeResolveContext(
    const ILSpy::Decompiler::TypeSystem::IModule& module,
    std::shared_ptr<UsingScope> usingScope,
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition,
    const ILSpy::Decompiler::TypeSystem::IMember* member)
    : CSharpTypeResolveContext(module, std::move(usingScope), typeDefinition, member, {})
{
}

// The private full ctor (the C# private ctor; the SimpleTypeResolveContext
// private-ctor pattern). The member-init order matches the declaration order in the
// header.
CSharpTypeResolveContext::CSharpTypeResolveContext(
    const ILSpy::Decompiler::TypeSystem::IModule& module,
    std::shared_ptr<UsingScope> usingScope,
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition,
    const ILSpy::Decompiler::TypeSystem::IMember* member,
    std::vector<std::string> methodTypeParameterNames)
    : module_(&module),
      currentUsingScope_(std::move(usingScope)),
      currentTypeDefinition_(typeDefinition),
      currentMember_(member),
      methodTypeParameterNames_(std::move(methodTypeParameterNames))
{
}

// The C# `public ICompilation Compilation { get { return module.Compilation; } }` --
// the module is an `ICompilationProvider`, so the compilation is read through it.
const ILSpy::Decompiler::TypeSystem::ICompilation& CSharpTypeResolveContext::Compilation() const
{
    return module_->Compilation();
}

// The C# public `WithCurrentTypeDefinition` (whose explicit interface twin returns the
// interface): a NEW context with the type-definition slot replaced, the other slots
// carried over (the `methodTypeParameterNames` threaded through unchanged). `new` (a
// private-ctor allocation `make_shared`/`make_unique` cannot reach, the header
// convention (c)).
std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
CSharpTypeResolveContext::WithCurrentTypeDefinition(
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition) const
{
    return std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>(
        new CSharpTypeResolveContext(*module_, currentUsingScope_, typeDefinition,
                                     currentMember_, methodTypeParameterNames_));
}

// The C# public `WithCurrentMember` (the same explicit-interface-twin shape).
std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>
CSharpTypeResolveContext::WithCurrentMember(
    const ILSpy::Decompiler::TypeSystem::IMember* member) const
{
    return std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext>(
        new CSharpTypeResolveContext(*module_, currentUsingScope_, currentTypeDefinition_,
                                     member, methodTypeParameterNames_));
}

// The C# `public CSharpTypeResolveContext WithUsingScope(UsingScope usingScope)` -- NOT
// on the interface, so the concrete return type is free (the header convention (b)):
// an owning `shared_ptr` (the `UsingScope::WithNestedNamespace` factory consumes this
// exact shape).
std::shared_ptr<CSharpTypeResolveContext> CSharpTypeResolveContext::WithUsingScope(
    std::shared_ptr<UsingScope> usingScope) const
{
    return std::shared_ptr<CSharpTypeResolveContext>(
        new CSharpTypeResolveContext(*module_, std::move(usingScope), currentTypeDefinition_,
                                     currentMember_, methodTypeParameterNames_));
}

} // namespace ILSpy::Decompiler::CSharp::TypeSystem
