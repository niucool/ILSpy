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

// Implementation of `KnownThings` (see the header for the port conventions).
// Every behavior was gold-pinned against the real ICSharpCode.BamlDecompiler
// KnownThings driven through the C:/temp-probe/KtProbe reflection probe.

#include "BamlDecompiler/Baml/KnownThings.hpp"

#include "BamlDecompiler/Baml/KnownThingsTables.hpp"
#include "BamlDecompiler/SyntheticWpfModule.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::BamlDecompiler::Baml {

// The standard .NET NullReferenceException message (the XmlnsDictionary
// convention for the C# null-dereference arms).
static const char* const kNullReferenceMessage =
    "Object reference not set to an instance of an object.";

// The C# KnownMember ctor:
//   `Property = declType.GetProperties(p => p.Name == name,
//       GetMemberOptions.IgnoreInheritedMembers).SingleOrDefault();`
KnownMember::KnownMember(KnownTypes parent,
                         const ILSpy::Decompiler::TypeSystem::ITypeDefinition* declType,
                         const std::string& name,
                         const ILSpy::Decompiler::TypeSystem::ITypeDefinition* type)
    : Parent(parent),
      DeclaringType(declType),
      Name(name),
      Type(type)
{
    // A null declaring type is the C# NullReferenceException on
    // `declType.GetProperties(...)` (reachable when the parent's row resolved
    // to null -- the type moved away from where the table expects it).
    if (declType == nullptr)
        throw std::runtime_error(kNullReferenceMessage);

    const std::vector<const ILSpy::Decompiler::TypeSystem::IProperty*> matches =
        declType->GetProperties(
            [this](const ILSpy::Decompiler::TypeSystem::IProperty* property) {
                return property->Name() == Name;
            },
            ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers);
    // SingleOrDefault: the single match, null when none, and the gold-pinned
    // .NET more-than-one message.
    if (matches.size() > 1)
        throw std::runtime_error("Sequence contains more than one element.");
    Property = matches.empty() ? nullptr : matches.front();
}

// The C# ctor:
//   `InitAssemblies(); InitTypes(); InitMembers(); InitStrings(); InitResources();`
//   all inside `try { ... } catch (Exception ex) { throw new DecompilerException(
//   typeSystem.MainModule.MetadataFile, ex.Message, ex); }`.
KnownThings::KnownThings(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem)
    : typeSystem_(typeSystem)
{
    try {
        InitAssemblies();
        InitTypes();
        InitMembers();
        InitStrings();
        InitResources();
    } catch (const std::exception&) {
        // The C# wraps every Init failure in
        // `new DecompilerException(typeSystem.MainModule.MetadataFile, ex.Message, ex)`
        // -- the (MetadataFile, message, inner) ctor of the Phase-7 unported
        // DecompilerException class (the gold probe pins the wrapper: the
        // message is preserved and the File is the main module's metadata
        // file). Until that class lands the port rethrows the ORIGINAL
        // exception -- a documented divergence (the header porting decisions).
        throw;
    }
}

// The C# `Func<KnownTypes, ITypeDefinition> Types => id => types[id]`.
const ILSpy::Decompiler::TypeSystem::ITypeDefinition* KnownThings::Types(KnownTypes id) const
{
    return types_.at(id);
}

// The C# `Func<KnownMembers, KnownMember> Members => id => members[id]`.
const KnownMember* KnownThings::Members(KnownMembers id) const
{
    return members_.at(id).get();
}

// The C# `Func<short, string> Strings => id => strings[id]`.
std::string KnownThings::Strings(std::int16_t id) const
{
    return strings_.at(id);
}

// The C# `Func<short, (string, string, string)> Resources => id => resources[id]`.
KnownResource KnownThings::Resources(std::int16_t id) const
{
    return resources_.at(id);
}

// The C# `IModule FrameworkAssembly => assemblies[0]`.
const ILSpy::Decompiler::TypeSystem::IModule* KnownThings::FrameworkAssembly() const
{
    return assemblies_.at(0);
}

// The C# `IModule ResolveAssembly(string name)`:
//   `IModule module = typeSystem.Modules.FirstOrDefault(m => m.AssemblyName == name);`
//   `if (module == null) throw new Exception("Could not resolve known assembly '" + name + "'!");`
const ILSpy::Decompiler::TypeSystem::IModule* KnownThings::ResolveAssembly(
    const std::string& name) const
{
    for (const auto* module : typeSystem_.Modules()) {
        if (module->AssemblyName() == name)
            return module;
    }
    throw std::runtime_error("Could not resolve known assembly '" + name + "'!");
}

// The C# `ITypeDefinition InitType(IModule assembly, string ns, string name)`:
//   `if (assembly is SyntheticWpfModule synthetic) return synthetic.RegisterType(ns, name);`
//   `return assembly.GetTypeDefinition(new TopLevelTypeName(ns, name));`
const ILSpy::Decompiler::TypeSystem::ITypeDefinition* KnownThings::InitType(
    const ILSpy::Decompiler::TypeSystem::IModule* assembly,
    const std::string& ns,
    const std::string& name)
{
    if (const auto* synthetic = dynamic_cast<const SyntheticWpfModule*>(assembly))
        return synthetic->RegisterType(ns, name);
    return assembly->GetTypeDefinition(
        ILSpy::Decompiler::TypeSystem::TopLevelTypeName(ns, name));
}

// The C# `KnownMember InitMember(...) => new KnownMember(parent, types[parent], name, type)`.
std::unique_ptr<KnownMember> KnownThings::InitMember(
    KnownTypes parent,
    const std::string& name,
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* type)
{
    return std::make_unique<KnownMember>(parent, types_.at(parent), name, type);
}

// The C# InitAssemblies (KnownThings.g.cs):
//   `assemblies[0] = ResolveAssembly("mscorlib"); ... assemblies[5] =
//    ResolveAssembly("System.Xml");`
// -- the six slots in KnownAssemblies order (a missing assembly throws at its
// slot, so the first slot's failure names 'mscorlib' -- gold-pinned).
void KnownThings::InitAssemblies()
{
    assemblies_.reserve(6);
    for (const char* name : KnownAssemblies)
        assemblies_.push_back(ResolveAssembly(name));
}

// The C# InitTypes: one insertion per KnownTypesTable row, in table (ascending
// id) order.
void KnownThings::InitTypes()
{
    for (const auto& entry : KnownTypesTable) {
        types_[entry.Id] = InitType(assemblies_.at(entry.Row.AssemblyIndex),
                                    entry.Row.Namespace, entry.Row.Name);
    }
}

// The C# InitMembers:
//   `members[KnownMembers.X_M] = InitMember(KnownTypes.X, "M",
//    InitType(assemblies[slot], "ns", "PropertyType"));`
// -- the member row's property type resolves through its own InitType call,
// and the parent's row is already seeded by InitTypes.
void KnownThings::InitMembers()
{
    for (const auto& entry : KnownMembersTable) {
        members_[entry.Id] = InitMember(
            entry.Row.Parent, entry.Row.Name,
            InitType(assemblies_.at(entry.Row.Type.AssemblyIndex),
                     entry.Row.Type.Namespace, entry.Row.Type.Name));
    }
}

// The C# InitStrings: `strings[1] = "Name"; strings[2] = "Uid";`.
void KnownThings::InitStrings()
{
    for (const auto& row : KnownStringsTable)
        strings_[row.Id] = row.Value;
}

// The C# InitResources: the SystemColors triple per KnownResourcesTable row.
void KnownThings::InitResources()
{
    for (const auto& row : KnownResourcesTable)
        resources_[row.Id] = KnownResource{ row.ClassName, row.KeyName, row.ResourceName };
}

} // namespace ILSpy::BamlDecompiler::Baml
