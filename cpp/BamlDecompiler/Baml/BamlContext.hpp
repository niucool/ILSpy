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

// Port of ICSharpCode.BamlDecompiler/Baml/BamlContext.cs (Ki, 2015, MIT) --
// the BAML-side resolution context: the four info-record id maps
// ConstructContext builds from the flat document (the assembly, attribute,
// string, and type ids the record stream carries), the KnownThings instance
// the ctor resolves over the type system, and the assembly resolution
// ResolveAssembly performs over the maps (the record's full name parsed as
// an AssemblyNameReference, the main-module match, FindMatchingReference's
// highest-version pick over ReferencedModules, and the 0xfff id mask).
//
// Every behavior was gold-pinned against the REAL internal BamlContext from
// the shipped ICSharpCode.BamlDecompiler.dll through the
// C:/temp-probe/BamlContextProbe reflection probe (the record walks over the
// two real markup-compiler fixtures read through the real BamlReader plus
// hand-built synthetic documents; the compilation shape: the real mscorlib
// PEFile as the main module plus SyntheticWpfModule stand-ins, with a
// three-way WindowsBase reference set pinning FindMatchingReference's
// highest-version / last-of-equal pick):
//  * ConstructContext's `id == map.Count` guard accepts only records
//    arriving in ascending consecutive id order (an out-of-order id is
//    skipped until its turn, duplicates are never re-added, and noise
//    records of other types never touch the maps);
//    TypeSerializerInfoRecord (a TypeInfoRecord subclass) lands in
//    TypeIdMap keeping its own fields.
//  * ResolveAssembly carries the record's raw full name VERBATIM (not the
//    parsed canonical form); the main-module match returns MainModule
//    itself; FindMatchingReference picks the highest version and the LAST of
//    equal versions; an unmatched name keeps the full name with a null
//    module; an unmapped id is the (null, null) miss; and every id is masked
//    with 0xfff BEFORE the lookup (a TypeInfo's raw AssemblyId 4096 resolves
//    as id 0).
//  * The KnownThings failure inside the ctor propagates out (the C# wraps
//    it in DecompilerException; the port's KnownThings rethrows the original
//    exception -- the documented divergence of that port).
//
// C#-to-C++ porting decisions:
//  * The ctor parameter is IDecompilerTypeSystem (ICompilation plus the
//    `new MetadataModule MainModule` narrowing); the port takes the
//    `const ICompilation&` surface (the KnownThings porting precedent):
//    everything BamlContext reads (MainModule.AssemblyName,
//    ReferencedModules) is on ICompilation. The IDecompilerTypeSystem
//    interface lands with the MetadataModule back end (Phase 7).
//  * The CancellationToken parameters are dropped (the established
//    deferral; every ported caller passes CancellationToken.None).
//  * The C# `Dictionary<ushort, XInfoRecord>` id maps port as
//    `std::vector<const XInfoRecord*>` where INDEX == id: the
//    ConstructContext guard makes every table exactly {0 .. Count-1}, so the
//    vector reproduces both the keyed lookup and the C# Dictionary's
//    add-only insertion-order iteration (XamlDecompiler iterates
//    AssemblyIdMap's values) deterministically. The maps are public data
//    members (the NamespaceMap public-fields precedent); the records are
//    owned by the caller's BamlDocument (non-owning pointers -- the document
//    must outlive the context, the provider-outlives-writers convention).
//  * The `(string FullAssemblyName, IModule Assembly)` tuple ports as the
//    ResolvedAssembly struct (the ValueTuple named-element convention); the
//    C# null string of the no-record arm maps to the empty string (the
//    null=="" equivalence -- unobservable through every consumer:
//    BuildPIMappings flows the full name into NamespaceMap's plain-string
//    field, where a C# null renders as "" anyway).
//  * ResolveAssembly is const with a mutable assemblyMap_ (the C# mutates
//    the private cache field from the method; the RegisterType
//    mutable-registry precedent). The cache is keyed by the MASKED id and
//    caches every arm (the C# `assemblyMap[id] = assembly` runs on every
//    path, including the miss).
//  * The C# `KnownThings KnownThings` self-named property: the accessor
//    uses the qualified `Baml::` form (MSVC hides a class name behind a
//    same-named member; the LookupStubs convention), and the context owns
//    the instance by value (the C# GC reference).
//  * The C# private ctor is public in the port (ConstructContext and the
//    tests construct directly).

#pragma once

#include "BamlDecompiler/Baml/KnownThings.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations keeping the include graph minimal (the IModule.hpp
// forward-declaration convention): the record classes live in
// BamlRecords.hpp (same namespace) and the document in BamlDocument.hpp; a
// consumer that dereferences the map pointers includes BamlRecords.hpp.
namespace ILSpy::Decompiler::Metadata {
class AssemblyNameReference;
} // namespace ILSpy::Decompiler::Metadata
namespace ILSpy::BamlDecompiler::Baml {
class AssemblyInfoRecord;
class AttributeInfoRecord;
class BamlDocument;
class StringInfoRecord;
class TypeInfoRecord;
} // namespace ILSpy::BamlDecompiler::Baml

namespace ILSpy::BamlDecompiler::Baml {

// The C# `(string FullAssemblyName, IModule Assembly)` ValueTuple of
// ResolveAssembly -- the field names are the tuple's declared element names
// (the deconstruction the XamlContext consumer spells).
struct ResolvedAssembly {
    // The record's raw AssemblyFullName (carried verbatim, never the parsed
    // canonical form); the null of the no-record arm maps to the empty string.
    std::string FullAssemblyName;

    // The resolved module (null when no reference matches the name); the
    // pointer is non-owning (the compilation keeps the modules alive).
    const ILSpy::Decompiler::TypeSystem::IModule* Assembly = nullptr;
};

// The C# `internal class BamlContext`.
class BamlContext {
public:
    // The C# private ctor:
    //   `KnownThings = new KnownThings(typeSystem);`
    // -- the KnownThings resolution runs FIRST, so an unresolvable known
    // assembly throws out of the BamlContext ctor before any record is
    // walked (the KnownThings porting precedent; its catch rethrows the
    // original exception instead of wrapping it in DecompilerException).
    explicit BamlContext(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem);

    // The C# `static BamlContext ConstructContext(IDecompilerTypeSystem,
    // BamlDocument, CancellationToken)` (the token deferral): the one-pass
    // record walk over the document building the four id maps. Each map
    // accepts only records whose id equals the map's running Count --
    // ascending consecutive ids -- so a record arriving before its turn (an
    // out-of-order id), a duplicate id, or a noise record of another type
    // never lands in a map.
    static std::unique_ptr<BamlContext> ConstructContext(
        const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
        BamlDocument& document);

    // The C# `IDecompilerTypeSystem TypeSystem` (the ICompilation surface --
    // see the header porting decisions).
    const ILSpy::Decompiler::TypeSystem::ICompilation& TypeSystem() const;

    // The C# `KnownThings KnownThings` (the self-named-accessor MSVC trap:
    // the qualified `Baml::` return type).
    const Baml::KnownThings& KnownThings() const;

    // The C# `(string FullAssemblyName, IModule Assembly)
    // ResolveAssembly(ushort id)`: the 0xfff mask FIRST (a raw id may carry
    // wire-format flag bits -- findtoolbar's first TypeInfo carries
    // AssemblyId 4096, which resolves as id 0), then the cached lookup, then
    // either the main-module match (the parsed Name equal to
    // MainModule.AssemblyName returns MainModule itself), the
    // FindMatchingReference pick over ReferencedModules, or the unmapped-id
    // (null, null) miss -- every arm caches the result under the masked id.
    ResolvedAssembly ResolveAssembly(std::uint16_t id) const;

    // The C# `Dictionary<ushort, AssemblyInfoRecord> AssemblyIdMap` (index
    // == id -- see the header porting decisions). The records are owned by
    // the caller's BamlDocument.
    std::vector<const AssemblyInfoRecord*> AssemblyIdMap;

    // The C# `Dictionary<ushort, AttributeInfoRecord> AttributeIdMap`.
    std::vector<const AttributeInfoRecord*> AttributeIdMap;

    // The C# `Dictionary<ushort, StringInfoRecord> StringIdMap`.
    std::vector<const StringInfoRecord*> StringIdMap;

    // The C# `Dictionary<ushort, TypeInfoRecord> TypeIdMap` (a
    // TypeSerializerInfoRecord row lands here too -- the subclass match).
    std::vector<const TypeInfoRecord*> TypeIdMap;

private:
    // The C# `private IModule FindMatchingReference(AssemblyNameReference name)`:
    // the scan over ReferencedModules matching the parsed short name, keeping
    // the highest version (`bestMatch.AssemblyVersion <=
    // module.AssemblyVersion` keeps the LAST of equal versions -- the
    // gold-pinned WindowsBase 3.0.0.0/4.0.0.0/4.0.0.0 trio picks the second
    // 4.0.0.0). Null when no reference matches.
    const ILSpy::Decompiler::TypeSystem::IModule* FindMatchingReference(
        const ILSpy::Decompiler::Metadata::AssemblyNameReference& name) const;

    // The C# `readonly IDecompilerTypeSystem typeSystem` (the ICompilation
    // surface -- see the header porting decisions).
    const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem_;

    // The C# `KnownThings KnownThings` field (by value: the ownership
    // stand-in for the C# GC reference; the qualified `Baml::` form --
    // the accessor above hides the class name).
    Baml::KnownThings knownThings_;

    // The C# `Dictionary<ushort, (string FullAssemblyName, IModule Assembly)>
    // assemblyMap` -- the ResolveAssembly cache, keyed by the MASKED id
    // (mutable: ResolveAssembly is const like the C# method mutating the
    // field).
    mutable std::unordered_map<std::uint16_t, ResolvedAssembly> assemblyMap_;
};

} // namespace ILSpy::BamlDecompiler::Baml
