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
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/Util/CacheManager.hpp"

namespace ILSpy::Decompiler::TypeSystem {

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

// The C# SimpleCompilation's Init-resolve context: the partially
// constructed compilation (the `IModuleReference.Resolve` contract -- no
// user code observes it; the MinimalCorlib resolution is the one consumer).
class CompilationResolveContext final : public ITypeResolveContext {
public:
    explicit CompilationResolveContext(const ICompilation& compilation)
        : compilation_(&compilation) {}

    const ICompilation& Compilation() const override { return *compilation_; }
    const IModule* CurrentModule() const override { return nullptr; }
    const ITypeDefinition* CurrentTypeDefinition() const override {
        return nullptr;
    }
    const IMember* CurrentMember() const override { return nullptr; }
    // The With* factories are unreachable on the Init-resolve context
    // (the C# SimpleTypeResolveContext arms); the null-object returns
    // carry the slots over.
    std::unique_ptr<ITypeResolveContext> WithCurrentTypeDefinition(
        const ITypeDefinition*) const override {
        return std::make_unique<CompilationResolveContext>(*compilation_);
    }
    std::unique_ptr<ITypeResolveContext> WithCurrentMember(
        const IMember*) const override {
        return std::make_unique<CompilationResolveContext>(*compilation_);
    }

private:
    const ICompilation* compilation_;
};

} // namespace

DecompilerTypeSystem::DecompilerTypeSystem(
    const ::ILSpy::Decompiler::Metadata::MetadataFile& mainModule,
    const ::ILSpy::Decompiler::Metadata::IAssemblyResolver& assemblyResolver,
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeSystemOptions)
    : mainFile_(&mainModule), options_(typeSystemOptions),
      knownTypeCache_(*this) {
    // The C# InitializeCoreAsync's main-module seeds: every
    // AssemblyReference row resolves through the resolver (`AddToQueue(true,
    // mainModule, refs)` + the queue drain). The null resolutions drop (the
    // `if (asm != null)` gate); the resolved files deduplicate by assembly
    // name, keeping the HIGHEST version (the referenceAssemblyVersionMap
    // arm -- a same-name lower-version file replaces nothing).
    std::map<std::string, std::pair<
        Metadata::MetadataFile::AssemblyDefinitionInfo, std::size_t>>
        referenceAssemblyVersionMap;
    for (const auto& reference : mainFile_->GetAssemblyReferences()) {
        // The C# `new AssemblyReference(asm, handle)` row wrapper feeding
        // the resolver.
        Metadata::AssemblyReference asmRef(*mainFile_, reference.Token);
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
                    referenced_[it->second.second].file = file;
                    it->second.first = *definition;
                }
                continue;
            }
            referenceAssemblyVersionMap.emplace(
                definition->Name, std::make_pair(*definition,
                                                  referenced_.size()));
        }
        referenced_.push_back(LoadedModule{file, nullptr});
    }

    // The C# Init(mainModuleWithOptions, referencedAssembliesWithOptions):
    // the main module binds the compilation reference first, then the
    // referenced modules (the MetadataModule ctor takes the compilation
    // reference; the Modules() list is built after every module exists).
    mainModule_ = std::make_unique<MetadataModule>(*this, mainFile_,
                                                    options_);
    modules_.push_back(mainModule_.get());
    for (auto& loaded : referenced_) {
        loaded.module = std::make_unique<MetadataModule>(
            *this, loaded.file, options_);
        modules_.push_back(loaded.module.get());
        referencedModules_.push_back(loaded.module.get());
    }
    // The C# missing-known-types arm (InitializeCoreAsync's tail):
    // `KnownTypeReference.AllKnownTypes.Where(IsMissing)` -- a type is
    // missing when neither the main module nor any referenced assembly
    // defines it; the MinimalCorlib net fills the gaps (the C#
    // `referencedAssembliesWithOptions.Concat(new[] {
    // MinimalCorlib.CreateWithTypes(missingKnownTypes) })`) so the known
    // types stay resolvable when the reference set misses them (the
    // attribute literals keep the uncast form -- the resolved Int32 kinds
    // as Struct rather than the Unknown fallback).
    std::vector<const KnownTypeReference*> missingKnownTypes;
    for (const KnownTypeReference* ktr : KnownTypeReference::AllKnownTypes()) {
        // The C# IsMissing: `!mainModule.GetTypeDefinition(name).IsNil`
        // over the main file, then every referenced file.
        if (mainModule_->GetTypeDefinition(ktr->TypeName()) != nullptr)
            continue;
        bool found = false;
        for (auto& loaded : referenced_) {
            if (loaded.module->GetTypeDefinition(ktr->TypeName())
                    != nullptr) {
                found = true;
                break;
            }
        }
        if (!found)
            missingKnownTypes.push_back(ktr);
    }
    if (!missingKnownTypes.empty()) {
        minimalCorlib_ = Implementation::MinimalCorlib::CreateWithTypes(
            std::move(missingKnownTypes));
        // The C# Init resolves the module reference against the partially
        // constructed compilation (the IModuleReference contract; the
        // reference owns the resolved module -- the CorlibModuleReference
        // keep-alive vector).
        CompilationResolveContext resolveContext(*this);
        const IModule* minimalCorlib = minimalCorlib_->Resolve(resolveContext);
        modules_.push_back(minimalCorlib);
        referencedModules_.push_back(minimalCorlib);
    }
}

DecompilerTypeSystem::~DecompilerTypeSystem() = default;

const IModule& DecompilerTypeSystem::MainModule() const {
    return *mainModule_;
}

std::vector<const IModule*> DecompilerTypeSystem::Modules() const {
    return modules_;
}

std::vector<const IModule*> DecompilerTypeSystem::ReferencedModules() const {
    return referencedModules_;
}

const INamespace& DecompilerTypeSystem::RootNamespace() const {
    // The C# SimpleCompilation merges the root namespaces of every module
    // into one tree; the port's stand-in is the main module's root (the
    // facade walks -- the namespace collection -- consult the main module
    // only today; the merged tree rides with the SimpleCompilation port).
    return mainModule_->RootNamespace();
}

const INamespace* DecompilerTypeSystem::GetNamespaceForExternAlias(
    const std::string&) const {
    // The C# resolves the alias through the module reference set (the
    // extern-alias metadata); the placeholder carries no aliases.
    return nullptr;
}

const IType& DecompilerTypeSystem::FindType(
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode typeCode) const {
    // The C# SimpleCompilation: `knownTypeCache.FindType(typeCode)` -- the
    // module scan over the compilation's Modules() (the main module + the
    // reference set this ctor loaded) with the UnknownType fallback (the
    // C# MinimalCorlib net fills the gaps the reference set leaves; that
    // arm rides deferred).
    return knownTypeCache_.FindType(typeCode);
}

const StringComparer& DecompilerTypeSystem::NameComparer() const {
    return StringComparer::Ordinal();
}

const ::ILSpy::Decompiler::Util::CacheManager&
DecompilerTypeSystem::CacheManager() const {
    return cacheManager_;
}

::ILSpy::Decompiler::TypeSystem::TypeSystemOptions
DecompilerTypeSystem::TypeSystemOptions() const {
    return options_;
}

MetadataModule& DecompilerTypeSystem::MainMetadataModule() {
    return *mainModule_;
}

} // namespace ILSpy::Decompiler::TypeSystem
