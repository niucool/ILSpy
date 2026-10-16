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
// compilation fills with the resolved referenced modules.
//
// The C# hierarchy: `DecompilerTypeSystem : SimpleCompilation,
// IDecompilerTypeSystem` -- the reference loading happens in the ctor and
// feeds the inherited Init (the module-reference list), so the merged
// root namespace, the KnownTypeCache-backed FindType, and the module
// snapshots all come from the base class.

#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

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
class IModuleReference;

class DecompilerTypeSystem final : public SimpleCompilation {
public:
    // The C# `public DecompilerTypeSystem(MetadataFile mainModule,
    // IAssemblyResolver assemblyResolver, TypeSystemOptions
    // typeSystemOptions)`: the plain first-level AssemblyReference arm of
    // InitializeCoreAsync -- every row of the main module's AssemblyRef
    // table resolves through the resolver; the null results drop. The
    // same-name-different-version rows deduplicate to the HIGHEST version
    // (the referenceAssemblyVersionMap arm), and the missing known types
    // are filled by the MinimalCorlib net (the IsMissing arm).
    // The resolver OUTLIVES this compilation (the port's resolver owns the
    // loaded files through its keep-alive registry -- the C# GC keeps them
    // alive through the compilation's module list; the port's caller holds
    // the resolver past the type system).
    // DEFERRED arms of the C# queue, loud: the module-reference seeds (the
    // multi-module AssemblyFiles walk), the ExportedTypes breadth-first
    // walk over the loaded assemblies (the .NET Core/PCL facade support),
    // the implicit-references set (System.Runtime.InteropServices et al),
    // and GetOptions (the DecompilerSettings -> TypeSystemOptions mapping
    // -- the Default placeholder rides until that mapping ports with its
    // own baseline evaluation).
    DecompilerTypeSystem(
        const ::ILSpy::Decompiler::Metadata::MetadataFile& mainModule,
        const ::ILSpy::Decompiler::Metadata::IAssemblyResolver&
            assemblyResolver,
        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeSystemOptions
            = ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default);
    ~DecompilerTypeSystem() override;

    // The C# `public new MetadataModule MainModule` -- the typed accessor
    // (the decompiler's entries take the MetadataModule; non-const: the
    // module's entity caches lazily initialize through it).
    MetadataModule& MainMetadataModule();

    // The C# `public override TypeSystemOptions TypeSystemOptions` -- the
    // options the ctor received (the base returns Default).
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions()
        const override;

private:
    // An `IModuleReference` over a (MetadataFile, options) pair (the C#
    // `file.WithOptions(options)` -- a MetadataFile IS an
    // IModuleReference whose Resolve builds the MetadataModule over the
    // resolving compilation). The adapter owns the module it resolved.
    class FileModuleReference;

    // The Init-feeding references (the adapters own the MetadataModules).
    std::unique_ptr<FileModuleReference> mainReference_;
    std::vector<std::unique_ptr<FileModuleReference>> referencedReferences_;
    // The MinimalCorlib net (the reference owns its resolved module).
    std::unique_ptr<IModuleReference> minimalCorlib_;
    // The typed main module (non-owning -- the adapter owns it).
    MetadataModule* mainMetadataModule_ = nullptr;
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options_;
};

} // namespace ILSpy::Decompiler::TypeSystem
