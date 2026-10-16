// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and
// to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IAssembly.cs -- the `IModule` interface
// (defined alongside `IModuleReference` in IAssembly.cs), a resolved metadata module.
// The C# `interface IModule : ISymbol, ICompilationProvider` ports to a C++ abstract
// base multiply-inheriting those two independent abstract bases (the `ISymbol` D372 /
// `ICompilationProvider` D379 precedents). A module exposes the underlying
// `MetadataFile` (nullable -- a module may not originate from a file), the `IsMainModule`
// flag, the assembly `AssemblyName` / `AssemblyVersion` / `FullAssemblyName`, the
// `GetAssemblyAttributes` / `GetModuleAttributes` snapshots, the `InternalsVisibleTo`
// query, the `RootNamespace` (this module's own namespace tree, distinct from the
// compilation's combined root namespace), and the `GetTypeDefinition` /
// `TopLevelTypeDefinitions` / `TypeDefinitions` lookups.
//
// `IModule` is the twin of `INamespace` (D394): both are `ISymbol, ICompilationProvider`
// with a single `ISymbol` subobject (ICompilationProvider does NOT derive from
// `ISymbol`), so `Name` AND `SymbolKind` are unambiguous through an `IModule*` with NO
// redeclaration (the D394 single-ISymbol-subobject precedent). Like `INamespace`, it
// does NOT derive from `IEntity` / `INamedElement` (unlike the member family), so it
// carries NO inherited attribute family -- only the `ISymbol` / `ICompilationProvider`
// inherited surface plus the `IModule`-own accessors.
//
// It is a leaf TypeSystem dependency toward `ICompilation` (which holds the `IModule`
// collection) and toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole
// remaining blocker of `CSharpAmbience`): `TypeSystemAstBuilder` reads
// `module.RootNamespace` / `module.AssemblyName` / `module.AssemblyVersion` /
// `module.GetAssemblyAttributes()` to emit `using` / `[assembly:...]` directives. All
// its deps are now ported: `ISymbol` (D372), `ICompilationProvider` (D379), `Version`
// (D395, the `AssemblyVersion` by-value return), `INamespace` (D394, the `RootNamespace`
// return), `ITypeDefinition` (D393, the snapshot element type / `GetTypeDefinition`
// return), `IAttribute` (D386, the attribute snapshots), `TopLevelTypeName` (the
// `GetTypeDefinition` parameter). The cross-layer `MetadataFile` (in
// `ILSpy::Decompiler::Metadata`, already ported) is a nullable pointer return, so it is
// FORWARD-DECLARED here (a pointer return to an incomplete type needs only a forward
// declaration, the `IEntity::ParentModule` forward-declared-`IModule` precedent); the
// TypeSystem-side deps are FORWARD-DECLARED too where only a pointer/reference type is
// mentioned, keeping the includes minimal (the `INamespace` forward-declared-already-
// ported-leaf precedent).

#pragma once

#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/ICompilationProvider.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <string>
#include <vector>

// Forward declaration of the cross-layer `MetadataFile` (in `ILSpy::Decompiler::Metadata`,
// already ported). `MetadataFile()` returns a nullable pointer to it, so only a forward
// declaration is needed here (the `IEntity::ParentModule` nullable-pointer precedent); a
// real consumer that dereferences the pointer includes `MetadataFile.hpp`.
namespace ILSpy::Decompiler::Metadata { class MetadataFile; }

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the snapshot element / return types. `IAttribute` (D386),
// `INamespace` (D394), `ITypeDefinition` (D393) are all already ported; `TopLevelTypeName`
// is the concrete value-struct parameter of `GetTypeDefinition`. All are forward-declared
// here (not included) because the `IModule`-own accessors only mention them as pointer /
// reference types (`const IAttribute*`, `const INamespace&`, `const ITypeDefinition*`,
// `const TopLevelTypeName&`), which are complete with the pointee incomplete, and keeping
// the includes minimal preserves include-graph isolation (the `IEntity::GetAttributes`
// forward-declared-`IAttribute` / `INamespace` forward-declared-`ITypeDefinition`
// precedents). `Version` is the one exception: `AssemblyVersion` RETURNS it by value, so
// `Version.hpp` is included above.
class IAttribute;
class INamespace;
class ITypeDefinition;
class TopLevelTypeName;

// A resolved metadata module. A concrete module subclasses `IModule` and overrides the
// inherited `ISymbol::SymbolKind()` / `ISymbol::Name()` (reporting `SymbolKind::Module`
// and the assembly short name), the inherited `ICompilationProvider::Compilation()` (the
// parent compilation), and the twelve `IModule`-own accessors declared below.
//
// The C# `IModule` does NOT redeclare `Name` / `SymbolKind` (it inherits them unchanged
// from `ISymbol`), and single inheritance from `ISymbol` (the only base declaring `Name`;
// `ICompilationProvider` does not) means the inherited `ISymbol::Name()` / `ISymbol::SymbolKind()`
// virtuals cover them with NO C++ redeclaration (the D374 single-inheritance "inherited
// virtual covers the `new`" precedent, here with no `new` at all). This is the structural
// twin of `INamespace` (D394) and the distinction from the `IEntity` (D381) /
// `ITypeParameter` (D383) / `IField` (D391) multiple-inheritance diamonds.
class IModule : public ISymbol, public ICompilationProvider {
public:
    // The C# `MetadataFile? MetadataFile { get; }` -- the underlying metadata file, or
    // null if the module was not created from a file. The cross-layer
    // `ILSpy::Decompiler::Metadata::MetadataFile` is forward-declared above; the return is
    // a nullable raw pointer (the `IEntity::ParentModule` nullable-pointer precedent). The
    // accessor is named after its pointer return type (the `ITypeDefinition::ExtensionInfo()`
    // getter-named-after-pointer-return-type convention).
    virtual const ILSpy::Decompiler::Metadata::MetadataFile* MetadataFile() const = 0;

    // The C# `bool IsMainModule { get; }` -- whether this assembly is the main assembly of
    // the compilation.
    virtual bool IsMainModule() const = 0;

    // The C# `string AssemblyName { get; }` -- the assembly short name.
    virtual std::string AssemblyName() const = 0;

    // The C# `Version AssemblyVersion { get; }` -- the assembly version, returned BY VALUE
    // (the `Version` D395 value struct; the C# property returns the `System.Version` value
    // type, and the C++ `Version` is a value struct, so a by-value return mirrors it
    // faithfully -- distinct from the `const INamespace& RootNamespace` reference return
    // below, where `INamespace` is a polymorphic interface).
    virtual Version AssemblyVersion() const = 0;

    // The C# `string FullAssemblyName { get; }` -- the full assembly name (including the
    // public key token etc.).
    virtual std::string FullAssemblyName() const = 0;

    // The C# `IEnumerable<IAttribute> GetAssemblyAttributes()` -- all assembly attributes,
    // as a non-owning `const IAttribute*` snapshot (the `IEntity::GetAttributes`
    // `IEnumerable` -> `std::vector<const T*>` by-value precedent; `IAttribute`
    // forward-declared above).
    virtual std::vector<const IAttribute*> GetAssemblyAttributes() const = 0;

    // The C# `IEnumerable<IAttribute> GetModuleAttributes()` -- all module attributes, as a
    // non-owning `const IAttribute*` snapshot.
    virtual std::vector<const IAttribute*> GetModuleAttributes() const = 0;

    // The C# `bool InternalsVisibleTo(IModule module)` -- whether the internals of this
    // assembly are visible in the specified assembly. The C# `IModule` (a reference type,
    // non-null) ports to `const IModule&` (the non-null-reference convention, the
    // `IType::Equals(const IType&)` precedent); `IModule` is complete within its own class
    // body, so the self-referential reference parameter is valid.
    virtual bool InternalsVisibleTo(const IModule& module) const = 0;

    // The C# `INamespace RootNamespace { get; }` -- the root namespace for this module
    // (non-null: always the nameless namespace; it contains only this module's
    // subnamespaces and types, distinct from `ICompilation.RootNamespace`). Ports to a
    // non-null `const INamespace&` reference return (the `IVariable::Type()` /
    // `ICompilationProvider::Compilation()` non-null-reference convention); `INamespace`
    // is forward-declared above (a reference return to an incomplete type needs only a
    // forward declaration, the `ICompilationProvider::Compilation` precedent).
    virtual const INamespace& RootNamespace() const = 0;

    // The C# `ITypeDefinition? GetTypeDefinition(TopLevelTypeName topLevelTypeName)` -- the
    // type definition for a top-level type, or null when not found (uses ordinal name
    // comparison, not the compilation's name comparer). `TopLevelTypeName` (the concrete
    // value-struct parameter) and `ITypeDefinition` (the nullable return) are both
    // forward-declared above.
    virtual const ITypeDefinition* GetTypeDefinition(const TopLevelTypeName& topLevelTypeName) const = 0;

    // The C# `IEnumerable<ITypeDefinition> TopLevelTypeDefinitions { get; }` -- all
    // non-nested types in the assembly, as a non-owning `const ITypeDefinition*` snapshot.
    virtual std::vector<const ITypeDefinition*> TopLevelTypeDefinitions() const = 0;

    // The C# `IEnumerable<ITypeDefinition> TypeDefinitions { get; }` -- all types in the
    // assembly (including nested types), as a non-owning `const ITypeDefinition*` snapshot.
    virtual std::vector<const ITypeDefinition*> TypeDefinitions() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
