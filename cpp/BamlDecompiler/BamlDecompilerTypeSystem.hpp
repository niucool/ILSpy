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

// Port of ICSharpCode.BamlDecompiler/BamlDecompilerTypeSystem.cs (the C# file
// carries Siegfried Pammer's 2021 copyright): the `SimpleCompilation` subclass
// the BAML decompiler drives -- a WPF binary's type system with the well-known
// WPF assemblies guaranteed present.
//
// The ctor queues every module reference of the main module (a ModuleRef row
// whose name matches a metadata-bearing File row), its AssemblyRef rows, and
// the seven default BAML references (mscorlib/System/WindowsBase/
// PresentationCore/PresentationFramework/PresentationUI/System.Xml, 4.0.0.0),
// resolves the queue through the `IAssemblyResolver` (dedup by the reference
// key: "A:"+FullName for assembly references, "M:"+name for module names),
// and walks each resolved assembly's ExportedType rows transitively (an
// AssemblyReference implementation re-queues the referenced assembly; an
// AssemblyFile implementation re-queues the module name). Every unresolved
// well-known assembly is substituted by a `SyntheticWpfModule` stand-in --
// KnownThings assumes these assemblies are always present, and the stand-in
// (with the presentation xmlns mapping for the four WPF assemblies) upholds
// that invariant so the decompiler degrades gracefully on a machine without
// WPF. When neither the main module nor a resolved reference defines Void or
// Int32, `MinimalCorlib.Instance` is appended so the primitive types exist.
//
// C#-to-C++ porting decisions:
//  * The C# `class BamlDecompilerTypeSystem : SimpleCompilation,
//    IDecompilerTypeSystem` -- the port derives `SimpleCompilation` only and
//    realizes the `IDecompilerTypeSystem` surface (the `ICompilation` members
//    plus the `new MetadataModule MainModule` narrowing) directly: deriving
//    the C# interface too would fork `ICompilation` into a C++ diamond (the
//    interface's `MainModule` has no C++ multiple-inheritance shape here).
//    This is the port's established IDecompilerTypeSystem narrowing
//    convention (documented at the `KnownThings`/`BamlContext` ctors, which
//    take the `ICompilation` view of the same object).
//  * The C# `public new MetadataModule MainModule { get; }` (the
//    IDecompilerTypeSystem narrowing; assigned `= (MetadataModule)
//    base.MainModule` at the ctor's end -- the cast is guaranteed by
//    construction, the main reference being `MetadataFile.WithOptions`)
//    ports as a COVARIANT virtual override of `ICompilation::MainModule`
//    (`MetadataModule` derives `IModule`), which gives every caller shape
//    the narrowed static type the C# `new` property gives.
//  * The C# `ArgumentNullException` arms for a null `mainModule` /
//    `assemblyResolver` are N/A (a C++ reference parameter is non-null).
//  * The C# GC roots the module references the ctor builds (the
//    `MetadataFile.WithOptions` adapters and the `SyntheticWpfModule`
//    references) through the compilation's non-owning module pointers; the
//    port owns them as members for the compilation's lifetime (the resolved
//    modules live inside the references -- `MetadataFileWithOptions` owns its
//    `MetadataModule`, `SyntheticModuleReference` keeps a registry).
//  * The C# `KeyComparer.Create` hash-set key (the string "A:"+FullName /
//    "M:"+name -- the comparer projects the tuple to the key string, so the
//    tuple's MainModule is NOT part of the key) ports to an
//    `std::unordered_set<std::string>` (HashSet order is never observable --
//    only membership).
//  * The C# `HashSet<string>(StringComparer.OrdinalIgnoreCase)` membership
//    sets (the resolved-assembly names and the four presentation-xmlns
//    assembly names) hold at most eight ASCII names; the port scans a
//    vector with `StringComparer::OrdinalIgnoreCase().Equals` (behaviorally
//    identical, the convention-(d) ASCII fold).

#pragma once

#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <memory>
#include <vector>

// Global-scope forward declarations (the iteration-69 MSVC learning: a
// qualified namespace-declaration inside the enclosing namespace creates a
// NEW nested chain, so sibling-namespace forward declarations sit at global
// scope). `MetadataFile`/`IAssemblyResolver` are the ctor's parameter types
// (references need only the declaration).
namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
class IAssemblyResolver;
} // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::BamlDecompiler {

// The decompiler type system for a WPF binary. Not `final` (the C# class is
// unsealed).
class BamlDecompilerTypeSystem
    : public ILSpy::Decompiler::TypeSystem::SimpleCompilation {
public:
    // The C# `public BamlDecompilerTypeSystem(MetadataFile mainModule,
    // IAssemblyResolver assemblyResolver)` -- the protected default ctor
    // (the compilation initializes when `Init` runs in the body). The queue
    // walk, the synthetic-stand-in substitution, and the MinimalCorlib
    // fallback run here; see the header comment for the full contract.
    BamlDecompilerTypeSystem(
        const ILSpy::Decompiler::Metadata::MetadataFile& mainModule,
        const ILSpy::Decompiler::Metadata::IAssemblyResolver& assemblyResolver);

    // The C# `public new MetadataModule MainModule { get; }` -- the
    // IDecompilerTypeSystem narrowing as a covariant override (see the
    // header comment). Returns the resolved main module (guaranteed a
    // `MetadataModule` -- the main module reference is the
    // `MetadataFile.WithOptions` adapter).
    const ILSpy::Decompiler::TypeSystem::MetadataModule& MainModule()
        const;

private:
    // The module references the ctor built and `Init` resolved through: the
    // main module's `MetadataFile.WithOptions` adapter and the referenced
    // modules' adapters plus the `SyntheticWpfModule` references, in the
    // `Init` order (the resolved references first, then the synthetic
    // stand-ins). The C# GC roots them; the port owns them for the
    // compilation's lifetime.
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::IModuleReference>
        mainModuleReference_;
    std::vector<std::unique_ptr<
        ILSpy::Decompiler::TypeSystem::IModuleReference>>
            referencedModuleReferences_;

    // The C# `MainModule` property backing: the resolved main module cast to
    // `MetadataModule`.
    const ILSpy::Decompiler::TypeSystem::MetadataModule* mainModule_ =
        nullptr;
};

} // namespace ILSpy::BamlDecompiler
