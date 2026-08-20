// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Shared test stubs for the `ICompilation` reconciliation (the D386 `IAttribute` /
// D388 `IParameterizedMember` / D393 `ITypeDefinition` / D396 `IModule` stand-in-
// reconciliation precedent, here for the `ICompilation` interface that every
// TypeSystem test stand-in defines as a minimal `class ICompilation { virtual
// ~ICompilation() = default; };` to satisfy the `ICompilationProvider::Compilation()`
// reference return).
//
// Now that the real `ICompilation.hpp` (D399) lands in `ILSpy::Decompiler::TypeSystem`,
// each of the 13 existing TypeSystem test files must drop its `ICompilation` stand-in,
// include the real header, and expand its `TestCompilation` stub to override the nine
// `ICompilation` pure-virtuals. Five of those nine accessors return a non-null
// reference (`MainModule` / `RootNamespace` / `FindType` / `NameComparer` /
// `CacheManager`), so the `TestCompilation` stub needs a concrete `IModule` (which
// itself needs a concrete `INamespace` for its `RootNamespace`) to bind them. Rather
// than duplicate the ~26 trivial overrides of a minimal `IModule` / `INamespace` in
// each of the 13 files, this header provides a single shared pair -- `TestModule` /
// `TestNamespace` in the `TestSupport` namespace -- that each `TestCompilation` holds
// by value. The names live in `TestSupport` (not the anonymous namespace), so they do
// not collide with the file-local `TestModule` / `TestNamespace` stubs the reconciled
// `IEntity_Test` / `IModule_Test` / `INamespace_Test` already carry for their own
// tests (those are in the anonymous namespace -- a different scope).
//
// The stubs override EVERY pure-virtual with a trivial return (empty string / empty
// snapshot / null pointer / `false`), so a `TestCompilation` holding a `TestModule` is
// valid for its lifetime without any external wiring. The `TestModule` takes
// `const ICompilation&` (the forward-declarable base, NOT the concrete `TestCompilation`)
// so it can be constructed from `*this` inside a `TestCompilation` ctor's member-init
// list (the `ICompilation` base is already constructed by then), and the
// `TestNamespace` member inside `TestModule` is likewise bound to the same compilation
// reference. The existing tests never read `MainModule` / `RootNamespace` / etc.
// through their `TestCompilation` (they only use `Compilation()` to get the
// `ICompilation&` and cast to `TestCompilation&` to read `id()`), so the stubs' trivial
// returns are dead code that merely needs to compile -- but they are LIVE (not
// function-local statics) so a future test that does read them gets a valid reference
// rather than a dangling one.

#pragma once

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <string>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::TestSupport {

// A minimal concrete `INamespace` for the `ICompilation` reconciliation: overrides
// every `INamespace` / `ISymbol` / `ICompilationProvider` pure-virtual with a trivial
// return. It lives in the `TestSupport` namespace (not the anonymous namespace), so it
// does not ODR-conflict with the richer file-local `TestNamespace` stubs the reconciled
// `IModule_Test` / `INamespace_Test` carry; the identical definition here is the SOLE
// definition (ODR-safe across the translation units that include this header). It is
// held by value as the `rootNamespace_` member of `TestModule` (below), bound to the
// compilation reference `TestModule` was constructed with.
class TestNamespace : public ILSpy::Decompiler::TypeSystem::INamespace {
public:
    explicit TestNamespace(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(compilation) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
    }
    std::string Name() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- INamespace ---
    std::string ExternAlias() const override { return {}; }
    std::string FullName() const override { return {}; }
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
};

// A minimal concrete `IModule` for the `ICompilation` reconciliation: overrides every
// `IModule` / `ISymbol` / `ICompilationProvider` pure-virtual with a trivial return.
// `RootNamespace()` returns a reference to the held `TestNamespace` (non-null, the
// `IVariable::Type()` non-null-reference convention), and `Compilation()` returns the
// compilation reference the module was constructed with. Held by value as the
// `mainModule_` member of each `TestCompilation` (bound to `*this` in the ctor's
// member-init list, the `ICompilation` base already constructed).
class TestModule : public ILSpy::Decompiler::TypeSystem::IModule {
public:
    explicit TestModule(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
        : compilation_(compilation), rootNamespace_(compilation) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Module;
    }
    std::string Name() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override
    {
        return compilation_;
    }

    // --- IModule ---
    const ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const override
    {
        return nullptr;
    }
    bool IsMainModule() const override { return false; }
    std::string AssemblyName() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::Version AssemblyVersion() const override { return {}; }
    std::string FullAssemblyName() const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAssemblyAttributes() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetModuleAttributes() const override
    {
        return {};
    }
    bool InternalsVisibleTo(const ILSpy::Decompiler::TypeSystem::IModule&) const override
    {
        return false;
    }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override
    {
        return rootNamespace_;
    }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* GetTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::TopLevelTypeName&) const override
    {
        return nullptr;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*>
    TopLevelTypeDefinitions() const override
    {
        return {};
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> TypeDefinitions() const override
    {
        return {};
    }

private:
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    TestNamespace rootNamespace_;
};

} // namespace ILSpy::Decompiler::TypeSystem::TestSupport
