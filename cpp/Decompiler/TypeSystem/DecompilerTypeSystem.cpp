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

#include <set>

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
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
    // The C# InitializeCoreAsync's reference-loading queue. The main
    // module's AssemblyReference rows are the seeds (`AddToQueue(true,
    // mainModule, refs)`); every LOADED assembly then contributes its own
    // ExportedTypes' implementations (an AssemblyReference implementation
    // queues the forwarder's target -- the shared System.Runtime's
    // forwarders pull in System.Private.CoreLib; an AssemblyFile
    // implementation queues the sibling module file by name), and the
    // queue-empty tail adds the implicit references for the
    // .NETCoreApp/.NETStandard/.NET targets (the compile-time-only
    // attribute assemblies the metadata never references). The
    // (isAssembly, mainModule, reference) set dedups the queue; a null
    // resolution drops (the `if (asm != null)` gate). The dedup by assembly
    // name (keeping the HIGHEST version, the referenceAssemblyVersionMap
    // arm) runs over the COLLECTED list after the drain, exactly as in the
    // C# -- the queue can resolve the same assembly from several parents
    // before that.
    //
    // DIVERGENCE (termination): the C# re-arms the implicit-references tail
    // every time the queue drains, and re-queues an implicit reference that
    // failed to resolve (a fresh parsed-name object defeats the queue's
    // tuple dedup), so an unresolvable implicit reference spins the C# loop
    // forever. The port arms the tail ONCE; an unresolvable implicit
    // reference drops like any other null resolution.
    struct QueueEntry {
        bool isAssembly;       // the C# AddToQueue's first argument
        bool isRowBacked;      // a row-backed AssemblyRef vs a parsed name
        const Metadata::MetadataFile* parent;
        std::uint32_t token = 0;  // the AssemblyRef row (row-backed form)
        std::string name;         // the module-file / parsed-name form
        // The C# HashSet<(bool, MetadataFile, object)> key equality.
        bool operator<(const QueueEntry& other) const {
            if (isAssembly != other.isAssembly) return isAssembly < other.isAssembly;
            if (parent != other.parent) return parent < other.parent;
            if (isRowBacked != other.isRowBacked) return isRowBacked < other.isRowBacked;
            if (token != other.token) return token < other.token;
            return name < other.name;
        }
    };
    std::set<QueueEntry> inQueue;
    std::vector<QueueEntry> queue;
    auto addToQueue = [&](QueueEntry entry) {
        if (inQueue.insert(entry).second)
            queue.push_back(entry);
    };
    for (const auto& reference : mainModule.GetAssemblyReferences())
        addToQueue(QueueEntry{true, true, &mainModule, reference.Token, {}});

    // The C# queue drain: resolve, append, walk the loaded assembly's
    // ExportedTypes.
    std::vector<const Metadata::MetadataFile*> resolvedFiles;
    // The resolved-name gate: the queue dedups by (isAssembly, parent,
    // token), but a referenced assembly's forwarder rows reach the SAME
    // target through dozens of facade parents (netstandard's facade set
    // queues System.Private.CoreLib once per parent) -- and the resolver
    // loads a fresh file per entry, retaining every copy. The C# cache
    // returns the one loaded instance; the name gate gives the port the
    // same single-load semantics.
    std::set<std::pair<bool, std::string>> resolvedNames;
    bool implicitReferencesArmed = false;
    while (true) {
        if (queue.empty()) {
            if (implicitReferencesArmed)
                break;
            // The C# queue-empty tail: the implicit references for the
            // .NETCoreApp/.NETStandard/.NET targets, added by parsed name
            // when the resolved list does not already carry them.
            implicitReferencesArmed = true;
            std::optional<std::string> targetFrameworkId =
                Metadata::DetectTargetFrameworkId(mainModule);
            Metadata::ParsedTargetFramework parsed =
                Metadata::ParseTargetFramework(
                    targetFrameworkId.value_or(""));
            switch (parsed.Identifier) {
                case Metadata::TargetFrameworkIdentifier::NETCoreApp:
                case Metadata::TargetFrameworkIdentifier::NETStandard:
                case Metadata::TargetFrameworkIdentifier::NET: {
                    const TypeSystem::Version& version = parsed.ParsedVersion;
                    const std::string versionSuffix =
                        ", Version=" + std::to_string(version.Major) + "."
                        + std::to_string(version.Minor) + "."
                        + std::to_string(version.Build) + ".0"
                        + ", Culture=neutral";
                    for (const char* item :
                         {"System.Runtime.InteropServices",
                          "System.Runtime.CompilerServices.Unsafe"}) {
                        bool existing = false;
                        for (const Metadata::MetadataFile* file :
                             resolvedFiles) {
                            if (file->Name() == item) {
                                existing = true;
                                break;
                            }
                        }
                        if (!existing)
                            addToQueue(QueueEntry{
                                true, false, &mainModule, 0,
                                std::string(item) + versionSuffix});
                    }
                    break;
                }
                default:
                    break;
            }
            if (queue.empty())
                break;
            continue;
        }
        QueueEntry entry = queue.front();
        queue.erase(queue.begin());
        const Metadata::MetadataFile* file = nullptr;
        if (entry.isAssembly) {
            if (entry.isRowBacked) {
                // The C# `new AssemblyReference(asm, handle)` row wrapper
                // feeding the resolver.
                Metadata::AssemblyReference asmRef(*entry.parent,
                                                   entry.token);
                if (!resolvedNames
                         .insert(std::make_pair(true, asmRef.Name()))
                         .second)
                    continue;
                file = assemblyResolver.Resolve(asmRef);
            } else {
                // The C# `AssemblyNameReference.Parse(...)` for the
                // implicit references.
                Metadata::AssemblyNameReference asmRef =
                    Metadata::AssemblyNameReference::Parse(entry.name);
                if (!resolvedNames
                         .insert(std::make_pair(true, asmRef.Name()))
                         .second)
                    continue;
                file = assemblyResolver.Resolve(asmRef);
            }
        } else {
            if (!resolvedNames
                     .insert(std::make_pair(false, entry.name))
                     .second)
                continue;
            file = assemblyResolver.ResolveModule(*entry.parent, entry.name);
        }
        if (file == nullptr)
            continue;
        resolvedFiles.push_back(file);
        // The C# `foreach (var h in metadata.ExportedTypes)`: the loaded
        // assembly's forwarder rows. Only the AssemblyReference and
        // AssemblyFile implementations queue anything (the C# switch has
        // no other arm -- a nested-ExportedType implementation is skipped).
        for (const auto& exportedType : file->GetExportedTypes()) {
            const std::uint32_t kind =
                exportedType.ImplementationToken >> 24;
            if (kind == 0x23u) {
                // `case HandleKind.AssemblyReference: AddToQueue(true, asm,
                // new AssemblyReference(asm, handle));`
                addToQueue(QueueEntry{
                    true, true, file, exportedType.ImplementationToken, {}});
            } else if (kind == 0x26u) {
                // `case HandleKind.AssemblyFile: AddToQueue(false, asm,
                // metadata.GetString(file.Name));` -- the File-table row the
                // implementation points at.
                for (const auto& assemblyFile : file->GetAssemblyFiles()) {
                    if (assemblyFile.Token
                        == exportedType.ImplementationToken) {
                        addToQueue(QueueEntry{
                            false, false, file, 0, assemblyFile.Name});
                        break;
                    }
                }
            }
        }
    }

    // The C# dedup pass over the collected list: by assembly name, keeping
    // the HIGHEST version (a same-name lower-version file replaces
    // nothing). A netmodule file joins the set unconditionally (the
    // `if (file.IsAssembly)` gate -- no name/version pair to dedup on).
    std::vector<const Metadata::MetadataFile*> referencedFiles;
    std::map<std::string, std::pair<
        Metadata::MetadataFile::AssemblyDefinitionInfo, std::size_t>>
        referenceAssemblyVersionMap;
    for (const Metadata::MetadataFile* file : resolvedFiles) {
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
