// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `SyntheticWpfModule` (see the header for the port conventions).
// Every fact below was gold-pinned against the real ICSharpCode.BamlDecompiler
// SyntheticWpfModule driven through the C:/temp-probe/SwmProbe reflection probe.

#include "BamlDecompiler/SyntheticWpfModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <algorithm>
#include <utility>

namespace ILSpy::BamlDecompiler {

// The C# `public static IModuleReference CreateReference(...)` -- hands out the
// deferred-resolution handle (the C# `new SyntheticModuleReference(...)`).
std::unique_ptr<ILSpy::Decompiler::TypeSystem::IModuleReference> SyntheticWpfModule::CreateReference(
    std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName,
    std::optional<std::string> presentationXmlnsNamespace)
{
    return std::make_unique<SyntheticWpfModule::SyntheticModuleReference>(
        std::move(assemblyName), std::move(presentationXmlnsNamespace));
}

// The C# private ctor: stores the compilation / assembly reference / xmlns namespace and
// builds the root namespace (`new SyntheticNamespace(this, null, string.Empty,
// string.Empty)`).
SyntheticWpfModule::SyntheticWpfModule(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    std::shared_ptr<const ILSpy::Decompiler::Metadata::IAssemblyReference> assemblyName,
    std::optional<std::string> presentationXmlnsNamespace)
    : compilation_(&compilation),
      assemblyName_(std::move(assemblyName)),
      presentationXmlnsNamespace_(std::move(presentationXmlnsNamespace))
{
    rootNamespace_ = std::make_shared<SyntheticWpfModule::SyntheticNamespace>(
        this, nullptr, std::string(), std::string());
}

// The C# `public ITypeDefinition RegisterType(string ns, string name)`:
//   `var typeName = new TopLevelTypeName(ns, name);`
//   `if (!typeDefinitions.TryGetValue(typeName, out var typeDef)) {`
//   `    typeDef = new SyntheticTypeDefinition(this, typeName);`
//   `    typeDefinitions.Add(typeName, typeDef);`
//   `    assemblyAttributes = null;` (invalidate; a namespace may have appeared)
//   `}`
//   `return typeDef;`
const ILSpy::Decompiler::TypeSystem::ITypeDefinition* SyntheticWpfModule::RegisterType(
    const std::string& ns, const std::string& name)
{
    ILSpy::Decompiler::TypeSystem::TopLevelTypeName typeName(ns, name);
    for (const auto& entry : typeDefinitions_) {
        if (entry.first == typeName)
            return entry.second.get();
    }
    auto typeDef = std::make_shared<SyntheticWpfModule::SyntheticTypeDefinition>(this, typeName);
    typeDefinitions_.emplace_back(std::move(typeName), std::move(typeDef));
    // Invalidate the cached xmlns attributes; a namespace may have appeared.
    assemblyAttributes_.reset();
    return typeDefinitions_.back().second.get();
}

// --- ICompilationProvider ---

// The C# `public ICompilation Compilation { get; }` -- the compilation the module was
// resolved against (the back-reference the nested types forward to).
const ILSpy::Decompiler::TypeSystem::ICompilation& SyntheticWpfModule::Compilation() const
{
    return *compilation_;
}

// --- ISymbol ---

// The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Module`.
ILSpy::Decompiler::TypeSystem::SymbolKind SyntheticWpfModule::SymbolKind() const
{
    return ILSpy::Decompiler::TypeSystem::SymbolKind::Module;
}

// The C# `string ISymbol.Name => assemblyName.Name` (the assembly short name).
std::string SyntheticWpfModule::Name() const
{
    return assemblyName_->Name();
}

// --- IModule ---

// The C# `MetadataFile IModule.MetadataFile => null` -- the module carries no metadata.
const ILSpy::Decompiler::Metadata::MetadataFile* SyntheticWpfModule::MetadataFile() const
{
    return nullptr;
}

// The C# `bool IModule.IsMainModule => Compilation.MainModule == this`.
bool SyntheticWpfModule::IsMainModule() const
{
    return &compilation_->MainModule() == this;
}

// The C# `string IModule.AssemblyName => assemblyName.Name`.
std::string SyntheticWpfModule::AssemblyName() const
{
    return assemblyName_->Name();
}

// The C# `Version IModule.AssemblyVersion => assemblyName.Version` -- the C# nullable
// Version maps to the by-value `Version` return: a null optional (an assembly reference
// without a version) renders the default-constructed 0.0.0.0 value, a documented
// divergence (the port's IModule::AssemblyVersion is non-nullable by value).
ILSpy::Decompiler::TypeSystem::Version SyntheticWpfModule::AssemblyVersion() const
{
    return assemblyName_->Version().value_or(ILSpy::Decompiler::TypeSystem::Version());
}

// The C# `string IModule.FullAssemblyName => assemblyName.FullName`.
std::string SyntheticWpfModule::FullAssemblyName() const
{
    return assemblyName_->FullName();
}

// The C# `IReadOnlyList<IAttribute> GetAssemblyAttributes()`:
//   `if (presentationXmlnsNamespace == null) return EmptyList<IAttribute>.Instance;`
//   `var attributes = assemblyAttributes;`
//   `if (attributes != null) return attributes;`
//   -- rebuild: a fresh SyntheticTypeDefinition for System.Windows.Markup
//      .XmlnsDefinitionAttribute (NOT registered in typeDefinitions -- a plain
//      GetTypeDefinition lookup for it returns null, gold-pinned), the compilation's
//      String type for both fixed arguments, one DefaultAttribute per distinct seeded
//      CLR namespace (first-occurrence order over the registration order), each with
//      the fixed arguments (presentationXmlnsNamespace, ns).
//   `attributes = list; assemblyAttributes = attributes; return attributes;`
std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>
SyntheticWpfModule::GetAssemblyAttributes() const
{
    if (!presentationXmlnsNamespace_.has_value())
        return {};
    if (assemblyAttributes_.has_value()) {
        std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> snapshot;
        snapshot.reserve(assemblyAttributes_->size());
        for (const auto& attribute : *assemblyAttributes_)
            snapshot.push_back(attribute.get());
        return snapshot;
    }

    // Reconstruct the XmlnsDefinitionAttribute set the real WPF assembly would carry:
    // one entry per seeded CLR namespace, all mapping to the presentation XML namespace.
    auto attributeType = std::make_shared<SyntheticWpfModule::SyntheticTypeDefinition>(
        this,
        ILSpy::Decompiler::TypeSystem::TopLevelTypeName("System.Windows.Markup",
                                                        "XmlnsDefinitionAttribute"));
    // The C# `var stringType = Compilation.FindType(KnownTypeCode.String);` -- the C#
    // holds the GC reference the compilation's known-type cache owns; the port
    // snapshots it as a NON-OWNING shared_ptr alias (the KnownTypeCache convention (d)
    // no-op-deleter precedent) so the fixed-argument ITypePtr neither extends nor
    // requires enable_shared_from_this ownership of the looked-up type.
    auto* stringTypeRaw = const_cast<ILSpy::Decompiler::TypeSystem::IType*>(
        &compilation_->FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode::String));
    ILSpy::Decompiler::TypeSystem::ITypePtr stringType(
        stringTypeRaw, [](ILSpy::Decompiler::TypeSystem::IType*) {});

    // The C# `typeDefinitions.Keys.Select(t => t.Namespace).Distinct()` -- the distinct
    // namespaces in first-occurrence order over the registration order (the Dictionary
    // insertion-order iteration the flat vector reproduces).
    std::vector<std::string> namespaces;
    for (const auto& entry : typeDefinitions_) {
        const auto& ns = entry.first.Namespace();
        if (std::find(namespaces.begin(), namespaces.end(), ns) == namespaces.end())
            namespaces.push_back(ns);
    }

    auto list = std::vector<std::shared_ptr<
        ILSpy::Decompiler::TypeSystem::Implementation::DefaultAttribute>>();
    list.reserve(namespaces.size());
    for (const auto& ns : namespaces) {
        std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> fixedArguments;
        fixedArguments.emplace_back(stringType, *presentationXmlnsNamespace_);
        fixedArguments.emplace_back(stringType, ns);
        list.push_back(std::make_shared<
            ILSpy::Decompiler::TypeSystem::Implementation::DefaultAttribute>(
                std::static_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(attributeType),
                std::move(fixedArguments),
                std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument>()));
    }
    assemblyAttributes_ = std::move(list);

    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> snapshot;
    snapshot.reserve(assemblyAttributes_->size());
    for (const auto& attribute : *assemblyAttributes_)
        snapshot.push_back(attribute.get());
    return snapshot;
}

// The C# `IEnumerable<IAttribute> IModule.GetModuleAttributes() =>
// EmptyList<IAttribute>.Instance`.
std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>
SyntheticWpfModule::GetModuleAttributes() const
{
    return {};
}

// The C# `bool IModule.InternalsVisibleTo(IModule module) => module == this`.
bool SyntheticWpfModule::InternalsVisibleTo(
    const ILSpy::Decompiler::TypeSystem::IModule& module) const
{
    return &module == this;
}

// The C# `INamespace IModule.RootNamespace => rootNamespace`.
const ILSpy::Decompiler::TypeSystem::INamespace& SyntheticWpfModule::RootNamespace() const
{
    return *rootNamespace_;
}

// The C# `ITypeDefinition GetTypeDefinition(TopLevelTypeName topLevelTypeName)` -- the
// seeded lookup; a plain lookup never creates types.
const ILSpy::Decompiler::TypeSystem::ITypeDefinition* SyntheticWpfModule::GetTypeDefinition(
    const ILSpy::Decompiler::TypeSystem::TopLevelTypeName& topLevelTypeName) const
{
    for (const auto& entry : typeDefinitions_) {
        if (entry.first == topLevelTypeName)
            return entry.second.get();
    }
    return nullptr;
}

// The C# `IEnumerable<ITypeDefinition> TopLevelTypeDefinitions => typeDefinitions.Values`
// / `IEnumerable<ITypeDefinition> TypeDefinitions => typeDefinitions.Values` -- both
// enumerate the same seeded set (only top-level types are ever registered), in
// registration order.
std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*>
SyntheticWpfModule::TopLevelTypeDefinitions() const
{
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> snapshot;
    snapshot.reserve(typeDefinitions_.size());
    for (const auto& entry : typeDefinitions_)
        snapshot.push_back(entry.second.get());
    return snapshot;
}

std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*>
SyntheticWpfModule::TypeDefinitions() const
{
    return TopLevelTypeDefinitions();
}

// --- SyntheticModuleReference ---

// The C# `IModule IModuleReference.Resolve(ITypeResolveContext context) => new
// SyntheticWpfModule(context.Compilation, assemblyName, presentationXmlnsNamespace)`
// -- a fresh module per call, kept alive in the registry (the header ownership
// decision), the raw pointer returned.
const ILSpy::Decompiler::TypeSystem::IModule*
SyntheticWpfModule::SyntheticModuleReference::Resolve(
    const ILSpy::Decompiler::TypeSystem::ITypeResolveContext& context) const
{
    // The C# `new SyntheticWpfModule(context.Compilation, ...)` -- the raw `new` (NOT
    // make_shared, whose template instantiation context cannot reach the enclosing
    // class's private ctor; the new-expression written HERE in the nested member
    // function enjoys the nested class's member access to the enclosing privates).
    auto module = std::shared_ptr<SyntheticWpfModule>(
        new SyntheticWpfModule(context.Compilation(), assemblyName_, presentationXmlnsNamespace_));
    resolved_.push_back(std::move(module));
    return resolved_.back().get();
}

// --- SyntheticNamespace ---

// The C# `IEnumerable<ITypeDefinition> INamespace.Types =>
// module.TopLevelTypeDefinitions.Where(td => td.Namespace == FullName)`.
std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*>
SyntheticWpfModule::SyntheticNamespace::Types() const
{
    std::vector<const ILSpy::Decompiler::TypeSystem::ITypeDefinition*> types;
    for (const auto* td : module_->TopLevelTypeDefinitions()) {
        if (td->Namespace() == fullName_)
            types.push_back(td);
    }
    return types;
}

// The C# `IEnumerable<IModule> INamespace.ContributingModules => new[] { module }`.
std::vector<const ILSpy::Decompiler::TypeSystem::IModule*>
SyntheticWpfModule::SyntheticNamespace::ContributingModules() const
{
    return {module_};
}

// The C# `ICompilation ICompilationProvider.Compilation => module.Compilation`.
const ILSpy::Decompiler::TypeSystem::ICompilation&
SyntheticWpfModule::SyntheticNamespace::Compilation() const
{
    return module_->Compilation();
}

// The C# `INamespace INamespace.GetChildNamespace(string name) => null` -- always null
// (the parameter is never consulted).
const ILSpy::Decompiler::TypeSystem::INamespace*
SyntheticWpfModule::SyntheticNamespace::GetChildNamespace(const std::string& name) const
{
    (void)name;
    return nullptr;
}

// The C# `ITypeDefinition INamespace.GetTypeDefinition(string name, int
// typeParameterCount)`:
//   `if (typeParameterCount != 0) return null;`
//   `return module.GetTypeDefinition(new TopLevelTypeName(FullName, name));`
const ILSpy::Decompiler::TypeSystem::ITypeDefinition*
SyntheticWpfModule::SyntheticNamespace::GetTypeDefinition(const std::string& name,
                                                          int typeParameterCount) const
{
    if (typeParameterCount != 0)
        return nullptr;
    return module_->GetTypeDefinition(
        ILSpy::Decompiler::TypeSystem::TopLevelTypeName(fullName_, name));
}

// --- SyntheticTypeDefinition ---

// The C# `IType IType.AcceptVisitor(TypeVisitor visitor) => visitor.VisitTypeDefinition(this)`.
ILSpy::Decompiler::TypeSystem::ITypePtr
SyntheticWpfModule::SyntheticTypeDefinition::AcceptVisitor(
    ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor)
{
    return visitor.VisitTypeDefinition(*this);
}

// The C# `ICompilation ICompilationProvider.Compilation => module.Compilation`.
const ILSpy::Decompiler::TypeSystem::ICompilation&
SyntheticWpfModule::SyntheticTypeDefinition::Compilation() const
{
    return module_->Compilation();
}

// The C# `IModule IEntity.ParentModule => module`.
const ILSpy::Decompiler::TypeSystem::IModule*
SyntheticWpfModule::SyntheticTypeDefinition::ParentModule() const
{
    return module_;
}

} // namespace ILSpy::BamlDecompiler
