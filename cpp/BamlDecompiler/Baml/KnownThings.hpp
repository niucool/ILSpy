// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
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

// Port of ICSharpCode.BamlDecompiler/Baml/KnownThings.cs (Ki, 2015, MIT) -- the
// class half of the generated KnownThings pair: the ctor that seeds the five
// dictionaries (assemblies, types, members, strings, resources) from the
// KnownThingsTables rows by RESOLVING them through a type system, the
// Func-shaped accessors the XAML handlers read the resolved entities through,
// and the KnownMember row class. The data half (the tables this consumes) is
// KnownThingsTables.hpp; the id enums are KnownTypes.hpp / KnownMembers.hpp.
//
// Every ctor behavior was gold-pinned against the REAL internal KnownThings from
// the shipped ICSharpCode.BamlDecompiler.dll through the C:/temp-probe/KtProbe
// reflection probe over fixtures the real BamlDecompilerTypeSystem cannot
// produce (a SimpleCompilation-based IDecompilerTypeSystem):
//  * A compilation whose main module is real mscorlib plus five
//    SyntheticWpfModule stand-ins: the ctor SUCCEEDS (the substitution's whole
//    purpose), all 759 type rows resolve (the 19 mscorlib-slot rows through the
//    real module's GetTypeDefinition, the 740 WPF/System rows through
//    RegisterType), and all 267 KnownMember.Property values are null (synthetic
//    types carry no members).
//  * A compilation missing a known assembly: ResolveAssembly throws
//    "Could not resolve known assembly 'X'!" and the C# ctor's catch wraps it
//    in DecompilerException (the probe pins the chain: DecompilerException
//    wrapping the plain Exception, File = the main module's metadata file).
//
// C#-to-C++ porting decisions:
//  * The ctor parameter is IDecompilerTypeSystem (ICompilation plus the
//    `new MetadataModule MainModule` narrowing). The port takes the
//    `const ICompilation&` base surface: everything KnownThings reads
//    (Modules in ResolveAssembly) is on ICompilation, and the MetadataModule
//    narrowing is observable only in the ctor's catch arm
//    (`typeSystem.MainModule.MetadataFile` for the DecompilerException), which
//    the port defers (below). The IDecompilerTypeSystem interface itself lands
//    with the MetadataModule back end (Phase 7).
//  * The ctor's catch arm wraps every Init failure in
//    `DecompilerException(typeSystem.MainModule.MetadataFile, ex.Message, ex)`
//    -- the (MetadataFile, message, inner) ctor of the Phase-7 unported
//    DecompilerException class. The port rethrows the ORIGINAL exception (a
//    documented divergence: callers see the original exception type where the
//    C# sees the DecompilerException wrapper; the wrap arm lands with
//    DecompilerException).
//  * The Func-shaped C# properties (`Func<KnownTypes, ITypeDefinition> Types`,
//    `Func<KnownMembers, KnownMember> Members`, `Func<short, string> Strings`,
//    `Func<short, (string, string, string)> Resources`) port as methods taking
//    the id -- every consumer (XamlContext, the record handlers) invokes the
//    delegate immediately with an id. A missing id throws the C#
//    KeyNotFoundException ("The given key '0' was not present in the
//    dictionary.") -- the port's std::out_of_range via map::at (the C#
//    dictionary-indexer convention).
//  * The `Dictionary<KnownTypes, ITypeDefinition>` values may be NULL (a real
//    module's GetTypeDefinition miss -- e.g. a type the engine cannot resolve),
//    so Types() returns a nullable pointer, as does KnownMember's
//    DeclaringType/Type and Property.
//  * KnownMember ports with PUBLIC members named after the C# properties (the
//    NamespaceMap public-fields precedent); its ctor resolves Property with the
//    .NET SingleOrDefault semantics gold-pinned by the probe: the single
//    same-named property declared ON the type, null when none, and the >1 arm
//    throws ("Sequence contains more than one element.").
//  * KnownThings owns its KnownMember rows (the C# GC references) -- the
//    members map holds unique_ptr and the accessor returns the non-owning
//    pointer.
//  * The resources tuple is the C# ValueTuple (string, string, string) whose
//    field names the consumers already use (`res.Item1 + "." + res.Item2`).

#pragma once

#include "BamlDecompiler/Baml/KnownMembers.hpp"
#include "BamlDecompiler/Baml/KnownTypes.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations keeping the include graph minimal: the TypeSystem types
// are mentioned as pointer/reference types only (the IModule.hpp
// forward-declaration convention). A real consumer that dereferences them
// includes the corresponding header.
namespace ILSpy::Decompiler::TypeSystem {
class ICompilation;
class IModule;
class IProperty;
class ITypeDefinition;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::BamlDecompiler::Baml {

// The C# `(string, string, string)` ValueTuple of the resources dictionary --
// the SystemColors/SystemParameters resource triple. The names are the
// ValueTuple's own field names (the handler consumers read `res.Item1`,
// `res.Item2`, `res.Item3`).
struct KnownResource {
    std::string Item1; // the owning class name ("SystemColors" / "SystemParameters" / ...)
    std::string Item2; // the *Key property name (the resource-key suffix form)
    std::string Item3; // the resource property name
};

// The C# `internal class KnownMember` -- one well-known dependency-property
// row: the parent known type, the declaring type definition, the property
// name, and the property type, with the resolved IProperty (null when the
// declaring type carries no such property -- e.g. an attached dependency
// property, or a synthetic type that materializes no members at all).
class KnownMember {
public:
    // The C# ctor:
    //   `Property = declType.GetProperties(p => p.Name == name,
    //       GetMemberOptions.IgnoreInheritedMembers).SingleOrDefault();`
    // -- the property resolution runs in the ctor body (the filter matches the
    // member's own name; inherited members are excluded). The SingleOrDefault
    // semantics: null when no property matches, the single match otherwise, and
    // the more-than-one arm throws (gold-pinned: "Sequence contains more than
    // one element."). A null declType is the C# NullReferenceException (the
    // port's std::runtime_error with the standard message, the XmlnsDictionary
    // NRE convention).
    KnownMember(KnownTypes parent,
                const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declType,
                const std::string& name,
                const ILSpy::Decompiler::TypeSystem::ITypeDefinition* type);

    // The C# properties (public fields -- the NamespaceMap precedent). The
    // declaration order is the C# property order.
    KnownTypes Parent;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringType;
    const ILSpy::Decompiler::TypeSystem::IProperty* Property;
    std::string Name;
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* Type;
};

// The C# `internal partial class KnownThings` -- the well-known-entity
// resolution: the six framework assemblies by slot, every KnownTypes id to its
// ITypeDefinition, every KnownMembers id to its KnownMember, the two
// well-known strings, and the SystemColors resource-key table.
class KnownThings {
public:
    // The C# `KnownThings(IDecompilerTypeSystem typeSystem)` -- resolves the
    // KnownThingsTables rows through the type system's modules (see the header
    // porting decisions for the ICompilation parameter surface). Throws when a
    // known assembly cannot be resolved.
    explicit KnownThings(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem);

    // The C# `Func<KnownTypes, ITypeDefinition> Types => id => types[id]` --
    // the resolved definition for the well-known type id (nullable: a real
    // module's lookup miss seeds null). The id must have a table row; the
    // C# KeyNotFoundException maps to std::out_of_range.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* Types(KnownTypes id) const;

    // The C# `Func<KnownMembers, KnownMember> Members => id => members[id]` --
    // the resolved member row (never null over a successful ctor; every id in
    // the table gets a row).
    const KnownMember* Members(KnownMembers id) const;

    // The C# `Func<short, string> Strings => id => strings[id]`.
    std::string Strings(std::int16_t id) const;

    // The C# `Func<short, (string, string, string)> Resources => id =>
    // resources[id]`.
    KnownResource Resources(std::int16_t id) const;

    // The C# `IModule FrameworkAssembly => assemblies[0]` -- the mscorlib
    // module (always resolved by a successful ctor).
    const ILSpy::Decompiler::TypeSystem::IModule* FrameworkAssembly() const;

private:
    // The C# `IModule ResolveAssembly(string name)`:
    //   `typeSystem.Modules.FirstOrDefault(m => m.AssemblyName == name)` -- the
    // first module whose assembly short name matches, or the plain Exception
    // (the port's std::runtime_error with the exact message).
    const ILSpy::Decompiler::TypeSystem::IModule* ResolveAssembly(const std::string& name) const;

    // The C# `ITypeDefinition InitType(IModule assembly, string ns, string name)`:
    //   a SyntheticWpfModule stand-in materializes the type through RegisterType
    // (keeping the well-known set bounded); any other module resolves through
    // GetTypeDefinition(TopLevelTypeName(ns, name)) -- nullable on a miss.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* InitType(
        const ILSpy::Decompiler::TypeSystem::IModule* assembly,
        const std::string& ns,
        const std::string& name);

    // The C# `KnownMember InitMember(KnownTypes parent, string name,
    // ITypeDefinition type) => new KnownMember(parent, types[parent], name,
    // type)` -- the KnownMember construction over the parent's already-seeded
    // row (the caller keys the members dictionary by the member id). The
    // unique_ptr return models the C# GC ownership transfer into the
    // dictionary.
    std::unique_ptr<KnownMember> InitMember(
        KnownTypes parent,
        const std::string& name,
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* type);

    // The Init* methods of KnownThings.g.cs, driven over the KnownThingsTables
    // rows (the generated `assemblies[0] = ResolveAssembly("mscorlib")` /
    // `types[KnownTypes.X] = InitType(...)` insertions).
    void InitAssemblies();
    void InitTypes();
    void InitMembers();
    void InitStrings();
    void InitResources();

    // The C# `readonly IDecompilerTypeSystem typeSystem` (the ICompilation
    // surface -- see the header porting decisions).
    const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem_;

    // The C# `Dictionary<int, IModule> assemblies` -- the six slots in
    // KnownAssemblies order (slot = index; all six are assigned or the ctor
    // threw).
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> assemblies_;

    // The C# `Dictionary<KnownTypes, ITypeDefinition> types` (nullable values).
    std::unordered_map<KnownTypes, const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> types_;

    // The C# `Dictionary<KnownMembers, KnownMember> members` (owned rows).
    std::unordered_map<KnownMembers, std::unique_ptr<KnownMember>> members_;

    // The C# `Dictionary<int, string> strings` / `Dictionary<int, (string,
    // string, string)> resources` (short keys -- the C# exposes them as
    // Func<short, ...>).
    std::unordered_map<std::int16_t, std::string> strings_;
    std::unordered_map<std::int16_t, KnownResource> resources_;
};

} // namespace ILSpy::BamlDecompiler::Baml
