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

#include "Decompiler/TypeSystem/DecompilerTypeSystem.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// The `IModuleReference` over a (MetadataFile, options) pair (the C#
// `file.WithOptions(options)`: a MetadataFile IS an IModuleReference whose
// Resolve builds the MetadataModule over the resolving compilation). The
// adapter owns the module it resolved (the C# GC owns it; the
// CorlibModuleReference keep-alive convention).
class DecompilerTypeSystem::FileModuleReference final
    : public IModuleReference {
public:
    FileModuleReference(const ::ILSpy::Decompiler::Metadata::MetadataFile*
                            file,
                        ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions
                            options)
        : file_(file), options_(options) {}

    const IModule* Resolve(const ITypeResolveContext& context) const override {
        if (module_ == nullptr) {
            module_ = std::make_unique<MetadataModule>(
                context.Compilation(), file_, options_);
        }
        return module_.get();
    }

private:
    const ::ILSpy::Decompiler::Metadata::MetadataFile* file_;
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options_;
    mutable std::unique_ptr<MetadataModule> module_;
};

namespace {

// The C# `newFileVersion >= info.version` -- the System.Version comparison
// (the four lexicographic fields, the C# Version.Equals/> semantics).
bool VersionAtLeast(const Metadata::MetadataFile::AssemblyDefinitionInfo& v,
                    const Metadata::MetadataFile::AssemblyDefinitionInfo& w)
{
    if (v.MajorVersion != w.MajorVersion)
        return v.MajorVersion > w.MajorVersion;
    if (v.MinorVersion != w.MinorVersion)
        return v.MinorVersion > w.MinorVersion;
    if (v.BuildNumber != w.BuildNumber)
        return v.BuildNumber > w.BuildNumber;
    return v.RevisionNumber >= w.RevisionNumber;
}

} // namespace

DecompilerTypeSystem::DecompilerTypeSystem(
    const ::ILSpy::Decompiler::Metadata::MetadataFile& mainModule,
    const ::ILSpy::Decompiler::Metadata::IAssemblyResolver& assemblyResolver,
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeSystemOptions)
    : options_(typeSystemOptions) {
    // The C# InitializeCoreAsync's main-module seeds: every
    // AssemblyReference row resolves through the resolver (`AddToQueue(true,
    // mainModule, refs)` + the queue drain). The null resolutions drop (the
    // `if (asm != null)` gate); the resolved files deduplicate by assembly
    // name, keeping the HIGHEST version (the referenceAssemblyVersionMap
    // arm -- a same-name lower-version file replaces nothing).
    std::vector<const Metadata::MetadataFile*> referencedFiles;
    std::map<std::string, std::pair<
        Metadata::MetadataFile::AssemblyDefinitionInfo, std::size_t>>
        referenceAssemblyVersionMap;
    for (const auto& reference : mainModule.GetAssemblyReferences()) {
        // The C# `new AssemblyReference(asm, handle)` row wrapper feeding
        // the resolver.
        Metadata::AssemblyReference asmRef(mainModule, reference.Token);
        const Metadata::MetadataFile* file = assemblyResolver.Resolve(asmRef);
        if (file == nullptr)
            continue;
        // The C# `if (file.IsAssembly)` gate on the dedup: a netmodule file
        // joins the set unconditionally (no name/version pair to dedup on).
        auto definition = file->GetAssemblyDefinition();
        if (definition.has_value()) {
            auto it = referenceAssemblyVersionMap.find(definition->Name);
            if (it != referenceAssemblyVersionMap.end()) {
                if (VersionAtLeast(*definition, it->second.first)) {
                    referencedFiles[it->second.second] = file;
                    it->second.first = *definition;
                }
                continue;
            }
            referenceAssemblyVersionMap.emplace(
                definition->Name, std::make_pair(*definition,
                                                  referencedFiles.size()));
        }
        referencedFiles.push_back(file);
    }

    // The C# missing-known-types arm (InitializeCoreAsync's tail):
    // `KnownTypeReference.AllKnownTypes.Where(IsMissing)` -- a type is
    // missing when neither the main module nor any referenced assembly
    // defines it; the MinimalCorlib net fills the gaps (the C#
    // `referencedAssembliesWithOptions.Concat(new[] {
    // MinimalCorlib.CreateWithTypes(missingKnownTypes) })`) so the known
    // types stay resolvable when the reference set misses them (the
    // attribute literals keep the uncast form -- the resolved Int32 kinds
    // as Struct rather than the Unknown fallback). The C# IsMissing reads
    // the FILES' GetTypeDefinition (before any module exists).
    std::vector<const KnownTypeReference*> missingKnownTypes;
    for (const KnownTypeReference* ktr :
         KnownTypeReference::AllKnownTypes()) {
        if (mainModule.GetTypeDefinition(ktr->TypeName()) != 0)
            continue;
        bool found = false;
        for (const Metadata::MetadataFile* file : referencedFiles) {
            if (file->GetTypeDefinition(ktr->TypeName()) != 0) {
                found = true;
                break;
            }
        }
        if (!found)
            missingKnownTypes.push_back(ktr);
    }

    // The C# Init(mainModuleWithOptions, referencedAssembliesWithOptions):
    // the file references feed the inherited Init (which resolves each
    // against this compilation, dedups by module identity, and builds the
    // merged root namespace), the MinimalCorlib net appended when the
    // reference set misses known types.
    mainReference_ = std::make_unique<FileModuleReference>(
        &mainModule, options_);
    std::vector<const IModuleReference*> references;
    for (const Metadata::MetadataFile* file : referencedFiles) {
        referencedReferences_.push_back(
            std::make_unique<FileModuleReference>(file, options_));
        references.push_back(referencedReferences_.back().get());
    }
    if (!missingKnownTypes.empty()) {
        minimalCorlib_ = Implementation::MinimalCorlib::CreateWithTypes(
            std::move(missingKnownTypes));
        references.push_back(minimalCorlib_.get());
    }
    Init(*mainReference_, std::move(references));
    // The typed main module (the C# `public new MetadataModule MainModule`)
    // -- the FileModuleReference resolved it; Init stored the non-owning
    // pointer.
    mainMetadataModule_ = static_cast<MetadataModule*>(
        const_cast<IModule*>(mainModule_));
}

DecompilerTypeSystem::~DecompilerTypeSystem() = default;

MetadataModule& DecompilerTypeSystem::MainMetadataModule() {
    return *mainMetadataModule_;
}

::ILSpy::Decompiler::TypeSystem::TypeSystemOptions
DecompilerTypeSystem::TypeSystemOptions() const {
    return options_;
}

} // namespace ILSpy::Decompiler::TypeSystem
