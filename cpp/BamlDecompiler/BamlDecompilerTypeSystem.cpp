// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The BamlDecompilerTypeSystem ctor (see the header for the class contract):
// the reference-queue walk, the synthetic-stand-in substitution, the
// MinimalCorlib fallback, and the `Init` call.

#include "BamlDecompilerTypeSystem.hpp"

#include "SyntheticWpfModule.hpp"
#include "XamlContext.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ILSpy::BamlDecompiler {

namespace {

// The C# `string[] defaultBamlReferences` instance field -- the seven
// well-known references every BAML decompilation needs (the 4.0 framework
// identities WPF's markup compiler compiles against).
constexpr const char* DefaultBamlReferences[] = {
    "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    "System, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
    "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
    "PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
    "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
    "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35",
    "System.Xml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089",
};

// The C# `static readonly HashSet<string> presentationXmlnsAssemblies` -- the
// WPF assemblies whose types serialize under the presentation XML namespace.
// When one of these has to be synthesized, the synthetic module reproduces
// its XmlnsDefinitionAttribute mapping so known types still emit the clean
// presentation xmlns rather than a clr-namespace fallback.
constexpr const char* PresentationXmlnsAssemblies[] = {
    "WindowsBase",
    "PresentationCore",
    "PresentationFramework",
    "PresentationUI",
};

// The queue entry -- the C# `(bool IsAssembly, MetadataFile MainModule,
// object Reference)` tuple. The `object Reference` is an assembly reference
// (`AssemblyReference` row wrapper or the parsed `AssemblyNameReference`) in
// the IsAssembly arm and the module name string in the module arm, so the
// port carries a variant; the shared_ptr keeps every queued reference alive
// for the walk (the row wrappers are constructed here, the parsed references
// by the caller loop below).
struct QueueEntry {
    bool IsAssembly;
    const ILSpy::Decompiler::Metadata::MetadataFile* MainModule;
    std::variant<std::shared_ptr<const ILSpy::Decompiler::Metadata::
                                         IAssemblyReference>,
                 std::string>
        Reference;
};

using AssemblyRefPtr =
    std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference>;

} // namespace

BamlDecompilerTypeSystem::BamlDecompilerTypeSystem(
    const ILSpy::Decompiler::Metadata::MetadataFile& mainModule,
    const ILSpy::Decompiler::Metadata::IAssemblyResolver& assemblyResolver)
    : SimpleCompilation()
{
    std::vector<const ILSpy::Decompiler::Metadata::MetadataFile*>
        referencedAssemblies;
    std::deque<QueueEntry> assemblyReferenceQueue;

    // The ctor keeps every queued assembly reference alive for the walk
    // (the C# GC roots the queue entries' objects).
    std::vector<AssemblyRefPtr> ownedReferences;

    // `mainMetadata.GetModuleReferences()`: a module reference is queued only
    // when a File-table row carries the same name AND metadata (the C#
    // `StringComparer.Equals` is the ordinal case-sensitive string
    // comparison; `ContainsMetadata` is the Flags column == 0).
    const auto moduleReferences = mainModule.GetModuleReferences();
    const auto assemblyFiles = mainModule.GetAssemblyFiles();
    for (const auto& moduleRef : moduleReferences) {
        for (const auto& file : assemblyFiles) {
            if (file.Name == moduleRef.Name && file.ContainsMetadata) {
                assemblyReferenceQueue.push_back(
                    QueueEntry{false, &mainModule, moduleRef.Name});
                break;
            }
        }
    }

    // `mainModule.AssemblyReferences`: the AssemblyRef rows as the metadata-
    // backed `AssemblyReference` wrappers.
    for (const auto& row : mainModule.GetAssemblyReferences()) {
        ownedReferences.push_back(
            std::make_shared<ILSpy::Decompiler::Metadata::AssemblyReference>(
                mainModule, row.Token));
        assemblyReferenceQueue.push_back(
            QueueEntry{true, &mainModule, ownedReferences.back()});
    }

    // `defaultBamlReferences.Select(AssemblyNameReference.Parse)`: the seven
    // parsed default references.
    std::vector<AssemblyRefPtr> defaultReferences;
    for (const char* fullName : DefaultBamlReferences) {
        defaultReferences.push_back(
            std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
                ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                    fullName)));
        assemblyReferenceQueue.push_back(
            QueueEntry{true, &mainModule, defaultReferences.back()});
    }

    // The `KeyComparer.Create` processed set -- the key is the projected
    // string ("A:"+FullName / "M:"+name; the tuple's MainModule is not part
    // of the key), so an unordered_set of the key strings is the faithful
    // stand-in (HashSet order is never observable, only membership).
    std::unordered_set<std::string> processedAssemblyReferences;

    // The resolve loop: dequeue, dedup by key, resolve, and walk the resolved
    // assembly's ExportedType implementations (an AssemblyReference
    // implementation re-queues the referenced assembly through a fresh row
    // wrapper over the RESOLVED file; an AssemblyFile implementation re-queues
    // the module name; an ExportedType implementation -- a nested forwarder
    // -- matches no case and is skipped).
    while (!assemblyReferenceQueue.empty()) {
        QueueEntry asmRef = assemblyReferenceQueue.front();
        assemblyReferenceQueue.pop_front();
        std::string key;
        if (asmRef.IsAssembly) {
            key = "A:" +
                std::get<AssemblyRefPtr>(asmRef.Reference)->FullName();
        } else {
            key = "M:" + std::get<std::string>(asmRef.Reference);
        }
        if (!processedAssemblyReferences.insert(std::move(key)).second)
            continue;

        const ILSpy::Decompiler::Metadata::MetadataFile* asmFile = nullptr;
        if (asmRef.IsAssembly) {
            asmFile = assemblyResolver.Resolve(
                *std::get<AssemblyRefPtr>(asmRef.Reference));
        } else {
            asmFile = assemblyResolver.ResolveModule(
                *asmRef.MainModule,
                std::get<std::string>(asmRef.Reference));
        }
        if (asmFile == nullptr)
            continue;

        referencedAssemblies.push_back(asmFile);
        for (const auto& exportedType : asmFile->GetExportedTypes()) {
            const std::uint32_t kind = exportedType.ImplementationToken >> 24;
            if (kind == 0x23u) {
                // HandleKind.AssemblyReference
                ownedReferences.push_back(std::make_shared<
                    ILSpy::Decompiler::Metadata::AssemblyReference>(
                    *asmFile, exportedType.ImplementationToken));
                assemblyReferenceQueue.push_back(
                    QueueEntry{true, asmFile, ownedReferences.back()});
            } else if (kind == 0x26u) {
                // HandleKind.AssemblyFile -- the C# reads the row's Name
                // directly (`metadata.GetString(file.Name)`); the port's
                // per-token read degrades a nil/corrupt row to the empty
                // string (unreachable through real metadata -- the
                // implementation handle points at a valid File row).
                assemblyReferenceQueue.push_back(QueueEntry{
                    false, asmFile,
                    asmFile
                        ->GetAssemblyFileName(exportedType.ImplementationToken)
                        .value_or("")});
            }
        }
    }

    // The `WithOptions` references: the main module's adapter plus one per
    // resolved assembly. Owned as members -- the resolved modules live inside
    // the references and the compilation keeps non-owning pointers to them
    // (the C# GC roots the locals).
    mainModuleReference_ = mainModule.WithOptions(
        ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default);
    for (const ILSpy::Decompiler::Metadata::MetadataFile* file :
         referencedAssemblies) {
        referencedModuleReferences_.push_back(file->WithOptions(
            ILSpy::Decompiler::TypeSystem::TypeSystemOptions::Default));
    }

    // `resolvedAssemblyNames` -- the resolved assemblies' names plus the main
    // module's, membership by StringComparer.OrdinalIgnoreCase (the set holds
    // at most eight names, so the port scans with the comparer's Equals).
    std::vector<std::string> resolvedAssemblyNames;
    for (const ILSpy::Decompiler::Metadata::MetadataFile* file :
         referencedAssemblies) {
        resolvedAssemblyNames.push_back(file->Name());
    }
    resolvedAssemblyNames.push_back(mainModule.Name());

    const ILSpy::Decompiler::TypeSystem::StringComparer& ignoreCase =
        ILSpy::Decompiler::TypeSystem::StringComparer::OrdinalIgnoreCase();

    // The synthetic stand-ins: one per unresolved default reference, with
    // the presentation xmlns mapping for the WPF assemblies.
    for (const AssemblyRefPtr& reference : defaultReferences) {
        bool resolved = false;
        for (const std::string& name : resolvedAssemblyNames) {
            if (ignoreCase.Equals(name, reference->Name())) {
                resolved = true;
                break;
            }
        }
        if (resolved)
            continue;

        bool isPresentationXmlns = false;
        for (const char* name : PresentationXmlnsAssemblies) {
            if (ignoreCase.Equals(name, reference->Name())) {
                isPresentationXmlns = true;
                break;
            }
        }
        std::optional<std::string> presentationXmlns;
        if (isPresentationXmlns) {
            presentationXmlns =
                std::string(XamlContext::KnownNamespace_Presentation);
        }
        referencedModuleReferences_.push_back(
            SyntheticWpfModule::CreateReference(reference, presentationXmlns));
    }

    // The `HasType` local function: Void/Int32 defined by the main module or
    // any RESOLVED reference (the synthetic stand-ins define nothing until
    // KnownThings registers types into them).
    auto HasType = [&](ILSpy::Decompiler::TypeSystem::KnownTypeCode code) {
        const ILSpy::Decompiler::TypeSystem::TopLevelTypeName typeName =
            ILSpy::Decompiler::TypeSystem::KnownTypeReference::Get(code)
                ->TypeName();
        if (mainModule.GetTypeDefinition(typeName) != 0)
            return true;
        for (const ILSpy::Decompiler::Metadata::MetadataFile* file :
             referencedAssemblies) {
            if (file->GetTypeDefinition(typeName) != 0)
                return true;
        }
        return false;
    };

    // `Init` over the reference list in the built order (the resolved
    // references, then the synthetic stand-ins; MinimalCorlib appended last
    // when the primitive types are missing). The C# MinimalCorlib arm calls
    // `mainModule.WithOptions(Default)` a second time -- a fresh adapter over
    // the same file resolves to an equivalent module, so the port reuses the
    // adapter it built above (the adapter is resolve-once).
    std::vector<const ILSpy::Decompiler::TypeSystem::IModuleReference*>
        initReferences;
    initReferences.reserve(referencedModuleReferences_.size());
    for (const auto& reference : referencedModuleReferences_) {
        initReferences.push_back(reference.get());
    }
    if (!HasType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Void)
        || !HasType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32)) {
        initReferences.push_back(
            &ILSpy::Decompiler::TypeSystem::Implementation::
                 MinimalCorlib::Instance());
    }
    Init(*mainModuleReference_, std::move(initReferences));

    // `MainModule = (MetadataModule)base.MainModule` -- the cast is
    // guaranteed by construction (the main reference is the WithOptions
    // adapter, whose Resolve constructs a MetadataModule).
    mainModule_ = static_cast<
        const ILSpy::Decompiler::TypeSystem::MetadataModule*>(
        &SimpleCompilation::MainModule());
}

// The C# `public new MetadataModule MainModule { get; }` -- the covariant
// override over the resolved-and-cast main module (see the header).
const ILSpy::Decompiler::TypeSystem::MetadataModule&
BamlDecompilerTypeSystem::MainModule() const {
    return *mainModule_;
}

} // namespace ILSpy::BamlDecompiler
