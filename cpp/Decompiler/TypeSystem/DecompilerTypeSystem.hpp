// Copyright (c) 2026 Jun Cai
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#pragma once

// The C# `ICSharpCode.Decompiler.TypeSystem.DecompilerTypeSystem`
// (DecompilerTypeSystem.cs): the compilation over the main module plus its
// REFERENCED assemblies. The reference set is the fix for cross-assembly
// member targets: a TypeRef scoped to an AssemblyRef resolves through
// MetadataModule::GetDeclaringModule -> ResolveModule ->
// FindModuleByReference over Compilation().Modules(), which this
// compilation fills with the resolved referenced modules (the C#
// SimpleCompilation.Init the DecompilerTypeSystem ctor drives).

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class IAssemblyResolver;
class MetadataFile;
} // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;
class KnownType;

class DecompilerTypeSystem final : public ICompilation {
public:
    // The C# `public DecompilerTypeSystem(MetadataFile mainModule,
    // IAssemblyResolver assemblyResolver, TypeSystemOptions
    // typeSystemOptions)`: the plain first-level AssemblyReference arm of
    // InitializeCoreAsync -- every row of the main module's AssemblyRef
    // table resolves through the resolver; the null results drop. The
    // same-name-different-version rows deduplicate to the HIGHEST version
    // (the referenceAssemblyVersionMap arm).
    // The resolver OUTLIVES this compilation (the port's resolver owns the
    // loaded files through its keep-alive registry -- the C# GC keeps them
    // alive through the compilation's module list; the port's caller holds
    // the resolver past the type system).
    // DEFERRED arms of the C# queue, loud: the module-reference seeds (the
    // multi-module AssemblyFiles walk), the ExportedTypes breadth-first
    // walk over the loaded assemblies (the .NET Core/PCL facade support),
    // the implicit-references set (System.Runtime.InteropServices et al),
    // the MinimalCorlib known-types safety net, and GetOptions (the
    // DecompilerSettings -> TypeSystemOptions mapping -- the Default
    // placeholder rides until that mapping ports with its own baseline
    // evaluation).
    DecompilerTypeSystem(
        const ::ILSpy::Decompiler::Metadata::MetadataFile& mainModule,
        const ::ILSpy::Decompiler::Metadata::IAssemblyResolver&
            assemblyResolver,
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeSystemOptions
            = ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default);
    ~DecompilerTypeSystem() override;

    // The ICompilation surface (the C# SimpleCompilation Init shape): the
    // main module leads Modules(); ReferencedModules() excludes it.
    const IModule& MainModule() const override;
    std::vector<const IModule*> Modules() const override;
    std::vector<const IModule*> ReferencedModules() const override;
    const INamespace& RootNamespace() const override;
    const INamespace* GetNamespaceForExternAlias(
        const std::string& alias) const override;
    const IType& FindType(
        ::ILSpy::Decompiler::TypeSystem::KnownTypeCode typeCode) const
        override;
    const StringComparer& NameComparer() const override;
    const ::ILSpy::Decompiler::Util::CacheManager& CacheManager()
        const override;
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions
    TypeSystemOptions() const override;

    // The C# `public new MetadataModule MainModule` -- the typed accessor
    // (the decompiler's entries take the MetadataModule; non-const: the
    // module's entity caches lazily initialize through it).
    MetadataModule& MainMetadataModule();

private:
    // A loaded referenced assembly: the file the resolver handed out
    // (non-owning -- the resolver's keep-alive registry owns it; the
    // resolver outlives this compilation) + its module.
    struct LoadedModule {
        const ::ILSpy::Decompiler::Metadata::MetadataFile* file = nullptr;
        std::unique_ptr<MetadataModule> module;
    };

    const ::ILSpy::Decompiler::Metadata::MetadataFile* mainFile_ = nullptr;
    std::unique_ptr<MetadataModule> mainModule_;
    std::vector<LoadedModule> referenced_;
    // The Modules()/ReferencedModules() snapshots (the C# Init builds the
    // lists once; the entity caches and the FindModuleByReference scans
    // consume them on every resolution).
    std::vector<const IModule*> modules_;
    std::vector<const IModule*> referencedModules_;
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options_;
    ::ILSpy::Decompiler::Util::CacheManager cacheManager_;
    // The FindType placeholder (the MinimalCorlib arm rides deferred; the
    // SingleModuleCompilation arm it replaces answers every code with the
    // single cached Object KnownType).
    std::unique_ptr<KnownType> knownType_;
};

} // namespace ILSpy::Decompiler::TypeSystem
