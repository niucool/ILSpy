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

// The out-of-line half of the MinimalCorlib port (see MinimalCorlib.hpp for the
// porting-decision map and the C# source reference).

#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <array>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The C# `$"[MinimalCorlibType {typeCode}]"` interpolates the enum's ToString() --
// the KnownTypeCode MEMBER NAME. The port spells the names from this table (the
// enum-name render the .NET interpolation produces).
constexpr std::array<std::string_view, 60> KnownTypeCodeNames = {
    "None",
    "Object",
    "DBNull",
    "Boolean",
    "Char",
    "SByte",
    "Byte",
    "Int16",
    "UInt16",
    "Int32",
    "UInt32",
    "Int64",
    "UInt64",
    "Single",
    "Double",
    "Decimal",
    "DateTime",
    "String",
    "Void",
    "Type",
    "Array",
    "Attribute",
    "ValueType",
    "Enum",
    "Delegate",
    "MulticastDelegate",
    "Exception",
    "IntPtr",
    "UIntPtr",
    "IEnumerable",
    "IEnumerator",
    "IEnumerableOfT",
    "IEnumeratorOfT",
    "ICollection",
    "ICollectionOfT",
    "IList",
    "IListOfT",
    "IReadOnlyCollectionOfT",
    "IReadOnlyListOfT",
    "Task",
    "TaskOfT",
    "ValueTask",
    "ValueTaskOfT",
    "NullableOfT",
    "IDisposable",
    "IAsyncDisposable",
    "INotifyCompletion",
    "ICriticalNotifyCompletion",
    "TypedReference",
    "IFormattable",
    "FormattableString",
    "DefaultInterpolatedStringHandler",
    "SpanOfT",
    "ReadOnlySpanOfT",
    "MemoryOfT",
    "Unsafe",
    "IAsyncEnumerableOfT",
    "IAsyncEnumeratorOfT",
    "Index",
    "Range",
};

} // namespace

// --- MinimalCorlib (statics + ctor) ---

// The C# `public static readonly IModuleReference Instance = new
// CorlibModuleReference(KnownTypeReference.AllKnownTypes)` -- the process-lifetime
// singleton (the function-local static, header convention (a)).
const IModuleReference& MinimalCorlib::Instance()
{
    static const CorlibModuleReference instance(KnownTypeReference::AllKnownTypes());
    return instance;
}

// The C# `public static IModuleReference CreateWithTypes(
// IEnumerable<KnownTypeReference> types) => new CorlibModuleReference(types)`.
std::unique_ptr<IModuleReference> MinimalCorlib::CreateWithTypes(
    std::vector<const KnownTypeReference*> types)
{
    return std::make_unique<CorlibModuleReference>(std::move(types));
}

// The C# private ctor: materializes the type definitions eagerly (in table order)
// and the root namespace.
MinimalCorlib::MinimalCorlib(const ICompilation& compilation,
                             std::vector<const KnownTypeReference*> types)
    : compilation_(&compilation)
{
    typeDefinitions_.reserve(types.size());
    for (const KnownTypeReference* ktr : types) {
        // The C# `ktr.KnownTypeCode` NREs on a null entry; the port dereferences
        // (header convention (f) -- unreachable through Get/AllKnownTypes).
        typeDefinitions_.push_back(
            std::make_shared<CorlibTypeDefinition>(this, ktr->Code()));
    }
    rootNamespace_ = std::make_shared<CorlibNamespace>(
        this, nullptr, std::string(), std::string());
}

// --- ICompilationProvider ---

// The C# `ICompilation ICompilationProvider.Compilation => compilation` (the
// back-reference captured at Resolve time).
const ICompilation& MinimalCorlib::Compilation() const
{
    return *compilation_;
}

// --- ISymbol ---

// The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Module`.
::ILSpy::Decompiler::TypeSystem::SymbolKind MinimalCorlib::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Module;
}

// The C# `string ISymbol.Name => "corlib"`.
std::string MinimalCorlib::Name() const
{
    return "corlib";
}

// --- IModule ---

// The C# `MetadataFile IModule.MetadataFile => null`.
const ::ILSpy::Decompiler::Metadata::MetadataFile* MinimalCorlib::MetadataFile() const
{
    return nullptr;
}

// The C# `bool IModule.IsMainModule => Compilation.MainModule == this`.
bool MinimalCorlib::IsMainModule() const
{
    return &compilation_->MainModule() == this;
}

// The C# `string IModule.AssemblyName => "corlib"`.
std::string MinimalCorlib::AssemblyName() const
{
    return "corlib";
}

// The C# `Version IModule.AssemblyVersion => asmVersion` where
// `readonly Version asmVersion = new Version(0, 0, 0, 0)` (header convention (g):
// constructed per call, value-equal).
Version MinimalCorlib::AssemblyVersion() const
{
    return Version(0, 0, 0, 0);
}

// The C# `string IModule.FullAssemblyName => "corlib"`.
std::string MinimalCorlib::FullAssemblyName() const
{
    return "corlib";
}

// The C# `IEnumerable<IAttribute> IModule.GetAssemblyAttributes() =>
// EmptyList<IAttribute>.Instance` (the same body for GetModuleAttributes).
std::vector<const IAttribute*> MinimalCorlib::GetAssemblyAttributes() const
{
    return {};
}

std::vector<const IAttribute*> MinimalCorlib::GetModuleAttributes() const
{
    return {};
}

// The C# `bool IModule.InternalsVisibleTo(IModule module) => module == this` --
// reference equality (gold: a SECOND minimal corlib instance is not visible).
bool MinimalCorlib::InternalsVisibleTo(const IModule& module) const
{
    return &module == this;
}

// The C# `INamespace IModule.RootNamespace => rootNamespace`.
const INamespace& MinimalCorlib::RootNamespace() const
{
    return *rootNamespace_;
}

// The C# `public ITypeDefinition GetTypeDefinition(TopLevelTypeName topLevelTypeName)`
// -- the linear scan over the materialized definitions comparing
// `td.FullTypeName == topLevelTypeName`. Identity-stable (the same instance comes
// back for the same name, gold-pinned).
const ITypeDefinition* MinimalCorlib::GetTypeDefinition(
    const TopLevelTypeName& topLevelTypeName) const
{
    const FullTypeName wanted(topLevelTypeName);
    for (const auto& td : typeDefinitions_) {
        if (td->FullTypeName() == wanted)
            return td.get();
    }
    return nullptr;
}

// The C# `IEnumerable<ITypeDefinition> TopLevelTypeDefinitions =>
// typeDefinitions.Where(td => td != null)` (the null filter is defensive -- the
// ctor never materializes nulls; kept faithfully).
std::vector<const ITypeDefinition*> MinimalCorlib::TopLevelTypeDefinitions() const
{
    std::vector<const ITypeDefinition*> result;
    result.reserve(typeDefinitions_.size());
    for (const auto& td : typeDefinitions_) {
        if (td != nullptr)
            result.push_back(td.get());
    }
    return result;
}

// The C# `IEnumerable<ITypeDefinition> IModule.TypeDefinitions =>
// TopLevelTypeDefinitions` (a minimal corlib has no nested types).
std::vector<const ITypeDefinition*> MinimalCorlib::TypeDefinitions() const
{
    return TopLevelTypeDefinitions();
}

// --- CorlibModuleReference ---

// The C# `IModule IModuleReference.Resolve(ITypeResolveContext context) => new
// MinimalCorlib(context.Compilation, types)` -- a FRESH module per call (gold:
// distinct instances across compilations), kept alive in the registry (header
// convention (b); the raw `new` + shared_ptr because make_shared cannot reach the
// private ctor from the nested class's member function context).
const IModule* MinimalCorlib::CorlibModuleReference::Resolve(
    const ITypeResolveContext& context) const
{
    auto module = std::shared_ptr<MinimalCorlib>(
        new MinimalCorlib(context.Compilation(), types_));
    resolved_.push_back(std::move(module));
    return resolved_.back().get();
}

// --- CorlibNamespace ---

// The C# `ICompilation ICompilationProvider.Compilation => corlib.Compilation`.
const ICompilation& MinimalCorlib::CorlibNamespace::Compilation() const
{
    return corlib_->Compilation();
}

// The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Namespace`.
::ILSpy::Decompiler::TypeSystem::SymbolKind MinimalCorlib::CorlibNamespace::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Namespace;
}

// The C# `IEnumerable<INamespace> INamespace.ChildNamespaces => childNamespaces`
// -- the never-populated list (header convention (d)): always empty.
std::vector<const INamespace*> MinimalCorlib::CorlibNamespace::ChildNamespaces() const
{
    return childNamespaces_;
}

// The C# `IEnumerable<ITypeDefinition> INamespace.Types =>
// corlib.TopLevelTypeDefinitions.Where(td => td.Namespace == FullName)`.
std::vector<const ITypeDefinition*> MinimalCorlib::CorlibNamespace::Types() const
{
    std::vector<const ITypeDefinition*> types;
    for (const ITypeDefinition* td : corlib_->TopLevelTypeDefinitions()) {
        if (td->Namespace() == fullName_)
            types.push_back(td);
    }
    return types;
}

// The C# `IEnumerable<IModule> INamespace.ContributingModules => new[] { corlib }`.
std::vector<const IModule*> MinimalCorlib::CorlibNamespace::ContributingModules() const
{
    return {corlib_};
}

// The C# `INamespace INamespace.GetChildNamespace(string name) =>
// childNamespaces.FirstOrDefault(ns => ns.Name == name)` -- always null (the list
// is never populated).
const INamespace* MinimalCorlib::CorlibNamespace::GetChildNamespace(
    const std::string& name) const
{
    for (const INamespace* ns : childNamespaces_) {
        if (ns->Name() == name)
            return ns;
    }
    return nullptr;
}

// The C# `ITypeDefinition INamespace.GetTypeDefinition(string name, int
// typeParameterCount) => corlib.GetTypeDefinition(FullName, name,
// typeParameterCount)` -- the TypeSystemExtensions extension wrapping the module's
// `GetTypeDefinition(new TopLevelTypeName(ns, name, tpc))` (header convention (e)).
const ITypeDefinition* MinimalCorlib::CorlibNamespace::GetTypeDefinition(
    const std::string& name, int typeParameterCount) const
{
    return corlib_->GetTypeDefinition(TopLevelTypeName(fullName_, name, typeParameterCount));
}

// --- CorlibTypeDefinition ---

// The C# ctor: reads the table row for the code, captures the kind, and builds the
// metadata name with the backtick arity.
MinimalCorlib::CorlibTypeDefinition::CorlibTypeDefinition(
    const MinimalCorlib* corlib, ::ILSpy::Decompiler::TypeSystem::KnownTypeCode typeCode)
    : corlib_(corlib),
      typeCode_(typeCode)
{
    const KnownTypeReference* ktr = KnownTypeReference::Get(typeCode);
    typeKind_ = ktr->Kind();
    metadataName_ = std::string(ktr->Name());
    if (ktr->TypeParameterCount() > 0)
        metadataName_ += "`" + std::to_string(ktr->TypeParameterCount());
    fullTypeName_ = ::ILSpy::Decompiler::TypeSystem::FullTypeName(ktr->TypeName());
}

// The C# `public override string ToString() => $"[MinimalCorlibType {typeCode}]"`
// -- the enum member name (the .NET interpolation).
std::string MinimalCorlib::CorlibTypeDefinition::ToString() const
{
    const auto index = static_cast<std::size_t>(typeCode_);
    std::string_view name = "None";
    if (index < KnownTypeCodeNames.size())
        name = KnownTypeCodeNames[index];
    return "[MinimalCorlibType " + std::string(name) + "]";
}

// --- IType ---

// The C# `string ISymbol.Name => KnownTypeReference.Get(typeCode).Name` (the
// metadata name WITHOUT the arity).
std::string MinimalCorlib::CorlibTypeDefinition::Name() const
{
    return std::string(KnownTypeReference::Get(typeCode_)->Name());
}

// The C# `string INamedElement.ReflectionName =>
// KnownTypeReference.Get(typeCode).TypeName.ReflectionName` (WITH the arity).
std::string MinimalCorlib::CorlibTypeDefinition::ReflectionName() const
{
    return KnownTypeReference::Get(typeCode_)->TypeName().ReflectionName();
}

int MinimalCorlib::CorlibTypeDefinition::TypeParameterCount() const
{
    return KnownTypeReference::Get(typeCode_)->TypeParameterCount();
}

// The C# `bool? IType.IsReferenceType`: Class/Interface => true, Struct/Enum =>
// false, anything else (Void) => null.
std::optional<bool> MinimalCorlib::CorlibTypeDefinition::IsReferenceType() const
{
    using ::ILSpy::Decompiler::TypeSystem::TypeKind;
    switch (typeKind_) {
        case TypeKind::Class:
        case TypeKind::Interface:
            return std::optional<bool>(true);
        case TypeKind::Struct:
        case TypeKind::Enum:
            return std::optional<bool>(false);
        default:
            return std::nullopt;
    }
}

// The C# `Nullability IType.Nullability => Nullability.Oblivious`.
::ILSpy::Decompiler::TypeSystem::Nullability
MinimalCorlib::CorlibTypeDefinition::Nullability() const
{
    return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
}

// The C# `IType IType.ChangeNullability(Nullability nullability)`: Oblivious =>
// this; otherwise a `NullabilityAnnotatedType` wrapping this.
ITypePtr MinimalCorlib::CorlibTypeDefinition::ChangeNullability(
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability)
{
    if (nullability == ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious)
        return shared_from_this();
    return std::make_shared<NullabilityAnnotatedType>(shared_from_this(), nullability);
}

// The C# `IType IType.AcceptVisitor(TypeVisitor visitor) =>
// visitor.VisitTypeDefinition(this)`.
ITypePtr MinimalCorlib::CorlibTypeDefinition::AcceptVisitor(TypeVisitor& visitor)
{
    return visitor.VisitTypeDefinition(*this);
}

// The C# `IType IType.VisitChildren(TypeVisitor visitor) => this` -- the visitor
// is IGNORED (gold: a recording visitor records nothing).
ITypePtr MinimalCorlib::CorlibTypeDefinition::VisitChildren(TypeVisitor& visitor)
{
    (void)visitor;
    return shared_from_this();
}

// The C# `IReadOnlyList<ITypeParameter> IType.TypeParameters =>
// DummyTypeParameter.GetClassTypeParameterList(...TypeParameterCount)` (header
// convention (j)).
std::vector<const ITypeParameter*>
MinimalCorlib::CorlibTypeDefinition::TypeParameters() const
{
    return DummyTypeParameter::GetClassTypeParameterList(
        KnownTypeReference::Get(typeCode_)->TypeParameterCount());
}

// The C# `IEnumerable<IType> IType.DirectBaseTypes`:
//   `var baseType = KnownTypeReference.Get(typeCode).baseType;`
//   `if (baseType != KnownTypeCode.None) return new[] { corlib.Compilation.FindType(baseType) };`
//   `else return EmptyList<IType>.Instance;`
// The `FindType` result is a compilation-cache-owned `IType` -- the snapshot
// aliases it with a no-op deleter (the KnownTypeCache convention).
std::vector<ITypePtr> MinimalCorlib::CorlibTypeDefinition::DirectBaseTypes() const
{
    ::ILSpy::Decompiler::TypeSystem::KnownTypeCode baseType =
        KnownTypeReference::Get(typeCode_)->BaseType();
    if (baseType == ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::None)
        return {};
    const IType& found = corlib_->Compilation().FindType(baseType);
    return {std::shared_ptr<IType>(const_cast<IType*>(&found),
                                  [](IType*) { /* no-op: the compilation owns it */ })};
}

// --- INamedElement ---

// The C# `string INamedElement.FullName => ktr.Namespace + "." + ktr.Name` -- NO
// arity suffix (gold: "System.Nullable" while ReflectionName is "System.Nullable`1").
std::string MinimalCorlib::CorlibTypeDefinition::FullName() const
{
    const KnownTypeReference* ktr = KnownTypeReference::Get(typeCode_);
    return std::string(ktr->Namespace()) + "." + std::string(ktr->Name());
}

// The C# `string INamedElement.Namespace => ktr.Namespace`.
std::string MinimalCorlib::CorlibTypeDefinition::Namespace() const
{
    return std::string(KnownTypeReference::Get(typeCode_)->Namespace());
}

// --- ICompilationProvider ---

// The C# `ICompilation ICompilationProvider.Compilation => corlib.Compilation`.
const ICompilation& MinimalCorlib::CorlibTypeDefinition::Compilation() const
{
    return corlib_->Compilation();
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
