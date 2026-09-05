// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Implementation of `BamlContext` (see the header for the port conventions).
// Every behavior was gold-pinned against the real ICSharpCode.BamlDecompiler
// BamlContext driven through the C:/temp-probe/BamlContextProbe reflection
// probe.

#include "BamlDecompiler/Baml/BamlContext.hpp"

#include "BamlDecompiler/Baml/BamlDocument.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"

#include <utility>

namespace ILSpy::BamlDecompiler::Baml {

// The C# private ctor:
//   `TypeSystem = typeSystem; KnownThings = new KnownThings(typeSystem);`
// (the maps start empty; the KnownThings resolution runs before any record
// is walked).
BamlContext::BamlContext(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem)
    : typeSystem_(typeSystem),
      knownThings_(typeSystem)
{
}

// The C# `static BamlContext ConstructContext(IDecompilerTypeSystem typeSystem,
// BamlDocument document, CancellationToken token)`: the one-pass record walk
// over the document. Each map accepts only records whose id equals the map's
// running Count -- ascending consecutive ids -- so a record arriving before
// its turn (an out-of-order id), a duplicate id, or a noise record of another
// type never lands in a map (gold-pinned: the out-of-order record is skipped
// until its turn comes, the duplicate AttributeInfo id 0 and the StringInfo
// id 5 are never added, and the Text/ConnectionId noise records never touch
// the maps).
std::unique_ptr<BamlContext> BamlContext::ConstructContext(
    const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
    BamlDocument& document)
{
    auto ctx = std::make_unique<BamlContext>(typeSystem);

    // The C# `foreach (var record in document)` with the `record is
    // XInfoRecord x` arms in the order Assembly, Attribute, String, Type.
    // dynamic_cast matches subclasses, so a TypeSerializerInfoRecord (a
    // TypeInfoRecord subclass) lands in TypeIdMap keeping its own fields
    // (gold-pinned).
    for (const std::unique_ptr<BamlRecord>& record : document.Records) {
        BamlRecord& rec = *record;
        if (auto* assemblyInfo = dynamic_cast<AssemblyInfoRecord*>(&rec)) {
            if (assemblyInfo->AssemblyId == ctx->AssemblyIdMap.size())
                ctx->AssemblyIdMap.push_back(assemblyInfo);
        } else if (auto* attrInfo = dynamic_cast<AttributeInfoRecord*>(&rec)) {
            if (attrInfo->AttributeId == ctx->AttributeIdMap.size())
                ctx->AttributeIdMap.push_back(attrInfo);
        } else if (auto* strInfo = dynamic_cast<StringInfoRecord*>(&rec)) {
            if (strInfo->StringId == ctx->StringIdMap.size())
                ctx->StringIdMap.push_back(strInfo);
        } else if (auto* typeInfo = dynamic_cast<TypeInfoRecord*>(&rec)) {
            if (typeInfo->TypeId == ctx->TypeIdMap.size())
                ctx->TypeIdMap.push_back(typeInfo);
        }
    }

    return ctx;
}

const ILSpy::Decompiler::TypeSystem::ICompilation& BamlContext::TypeSystem() const
{
    return typeSystem_;
}

const Baml::KnownThings& BamlContext::KnownThings() const
{
    return knownThings_;
}

// The C# `(string FullAssemblyName, IModule Assembly) ResolveAssembly(ushort id)`:
// the mask, the cache, the three arms, and the unconditional cache write (the
// C# assigns `assemblyMap[id] = assembly` on every path, including the miss).
ResolvedAssembly BamlContext::ResolveAssembly(std::uint16_t id) const
{
    // The mask runs BEFORE the cache lookup, so every masked spelling of an
    // id (the raw 4096 and the 0 of a plain first assembly; a TypeInfo's
    // AssemblyId flag bits and the plain id) shares one cache entry.
    id &= 0xfff;

    auto cached = assemblyMap_.find(id);
    if (cached != assemblyMap_.end())
        return cached->second;

    ResolvedAssembly assembly;
    if (id < AssemblyIdMap.size()) {
        const AssemblyInfoRecord* assemblyRec = AssemblyIdMap[id];
        // The C# `Metadata.AssemblyNameReference.Parse(assemblyRec.AssemblyFullName)`
        // -- the record's raw string is parsed for the name/version match but
        // carried verbatim in the tuple (gold-pinned: an unparsed partial
        // name like "PresentationUI" resolves to itself).
        const ILSpy::Decompiler::Metadata::AssemblyNameReference assemblyName =
            ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(assemblyRec->AssemblyFullName);

        assembly.FullAssemblyName = assemblyRec->AssemblyFullName;
        if (assemblyName.Name() == TypeSystem().MainModule().AssemblyName()) {
            // The main-module match: the assembly IS the main module.
            assembly.Assembly = &TypeSystem().MainModule();
        } else {
            assembly.Assembly = FindMatchingReference(assemblyName);
        }
    }
    // else: the C# `(null, null)` miss arm (the null string maps to "" --
    // see the header porting decisions).

    assemblyMap_[id] = assembly;
    return assembly;
}

// The C# `private IModule FindMatchingReference(AssemblyNameReference name)`.
const ILSpy::Decompiler::TypeSystem::IModule* BamlContext::FindMatchingReference(
    const ILSpy::Decompiler::Metadata::AssemblyNameReference& name) const
{
    const ILSpy::Decompiler::TypeSystem::IModule* bestMatch = nullptr;
    for (const ILSpy::Decompiler::TypeSystem::IModule* module : TypeSystem().ReferencedModules()) {
        if (module->AssemblyName() == name.Name()) {
            // using highest version as criterion (the C# comment) -- `<=`
            // keeps the LAST of equal versions (gold-pinned over the
            // WindowsBase 3.0.0.0/4.0.0.0/4.0.0.0 trio: the second 4.0.0.0
            // wins).
            if (bestMatch == nullptr
                || bestMatch->AssemblyVersion() <= module->AssemblyVersion()) {
                bestMatch = module;
            }
        }
    }
    return bestMatch;
}

} // namespace ILSpy::BamlDecompiler::Baml
