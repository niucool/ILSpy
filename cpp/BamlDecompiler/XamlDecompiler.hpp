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

// Port of ICSharpCode.BamlDecompiler/XamlDecompiler.cs (Ki, 2015, MIT): the
// full class -- the constructor family over the `CreateTypeSystemFromFile`
// chain, the private statics, the rewrite-pass chain, and the `Decompile`
// composition (BamlReader.ReadDocument -> XamlContext.Construct ->
// HandlerMap.LookupHandler(RootNode.Type) -> Translate -> the passes -> the
// BamlDecompilationResult).
//
// C#-to-C++ porting decisions:
//  * The four C# constructors all build a `BamlDecompilerTypeSystem` and run
//    the `TypeSystemOptions.Uncached` check over its `MainModule` (the C#
//    throws ArgumentException "Cannot use an uncached type system in the
//    decompiler."). The port realizes the family as four constructors:
//    the concrete `(BamlDecompilerTypeSystem, settings)` one (the C# ctor-4
//    shape -- caller-owned type system, no check possible over the interface
//    stand-in), the `(MetadataFile, IAssemblyResolver, settings)` one (the
//    caller file/resolver outlive the decompiler -- the BamlDecompilerTypeSystem
//    ctor's documented liveness convention; the decompiler owns the type
//    system), the `(fileName, IAssemblyResolver, settings)` one (the
//    decompiler owns the `LoadPEFile`-loaded file and the type system; the
//    caller's resolver keeps the resolved referenced files alive -- the
//    resolver's keep-alive registry owns them, so it must outlive the
//    decompiler), and the `(fileName, settings)` one (the decompiler owns
//    everything -- file, the built `UniversalAssemblyResolver`, and the type
//    system). The `CreateTypeSystemFromFile` static ports as the private
//    helper returning all three owned pieces (the C# return value embeds
//    them through GC).
//  * The C# `static PEFile LoadPEFile(string, BamlDecompilerSettings)` ports
//    as the private static loading the `MetadataFile` and reproducing the C#
//    `new FileStream`/`new PEReader`/`MetadataFile`-ctor failure arms over
//    the file bytes (the .NET 10 PEHeaders eager parse in
//    Decompiler/Metadata/PEReaderParse): an unopenable path is the
//    FileNotFoundException/DirectoryNotFoundException arm by parent
//    existence, a malformed image the exact BadImageFormatException message
//    of the PEHeaders parse arm, a valid image without metadata the
//    MetadataFileNotSupportedException, and a corrupt metadata blob the
//    representative OverflowException message. The `settings` parameter has
//    no effect in the C# (it is unused).
//  * The C# `private MetadataModule module` field ports as the concrete
//    `MetadataModule` pointer (the ICompilation stand-in ctor leaves it null
//    -- test stubs are not BamlDecompilerTypeSystems and the field's only
//    reader is the Uncached check).
//  * `CancellationToken` (the property and the per-record/per-pass
//    `ThrowIfCancellationRequested` calls) is a documented deferral (the
//    XamlContext convention: no ported consumer cancels).
//  * The C# `static readonly IRewritePass[] rewritePasses` (XClass,
//    MarkupExtension, Attribute, ConnectionId, Document) ports to a
//    function-local static array of the five ported passes in that order
//    (ConnectionIdRewritePass is the ILAst-driven x:Name/event wiring).
//  * `Decompile(Stream stream)` ports to the byte-span convention
//    (`BamlReader.ReadDocument`'s signature).
//  * The C# `ctx.RootNode.Type` and `elem.Xaml.Element` null-derefs (an
//    empty document's `BamlNode.Parse` returns null; a root handler
//    returning null) port to explicit `std::runtime_error`s carrying the
//    standard .NET NullReferenceException message (the XmlnsDictionary
//    convention).
//  * The C# ctors 1-3 accept a NULL settings (the value flows to ctor 4,
//    which stores it and lets `XamlContext.Construct` build the default);
//    the `(fileName, settings)` ctor's `CreateTypeSystemFromFile` chain
//    loads the file through `LoadPEFile` (which ignores the settings) and
//    THEN dereferences the settings for `ThrowOnAssemblyResolveErrors`, so
//    a null settings NREs only after the file loads -- the port's reference
//    parameter makes that arm unreachable (the documented divergence).

#pragma once

#include "BamlDecompiler/BamlDecompilationResult.hpp"
#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
class UniversalAssemblyResolver;
class IAssemblyResolver;
} // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::Decompiler::TypeSystem {
class MetadataModule;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::BamlDecompiler {

class BamlDecompilerTypeSystem;

// The C# `public class XamlDecompiler`.
class XamlDecompiler {
public:
    // The out-of-line destructor: the owning unique_ptr members are over
    // types only forward-declared here (UniversalAssemblyResolver), so the
    // implicit destructor instantiated in a consuming TU would need the
    // complete types (the XamlContext convention).
    ~XamlDecompiler();

    // The C# `public XamlDecompiler(string fileName, BamlDecompilerSettings
    // settings)` -- through `CreateTypeSystemFromFile`: the decompiler owns
    // the loaded file, the built `UniversalAssemblyResolver`, and the type
    // system (the C# GC roots all three).
    XamlDecompiler(const std::string& fileName,
                   const BamlDecompilerSettings& settings);

    // The C# `public XamlDecompiler(string fileName, IAssemblyResolver
    // assemblyResolver, BamlDecompilerSettings settings)` -- through
    // `LoadPEFile`: the decompiler owns the loaded file and the type system;
    // the caller's resolver must outlive the decompiler (its keep-alive
    // registry owns every resolved referenced file).
    XamlDecompiler(const std::string& fileName,
                   const ILSpy::Decompiler::Metadata::IAssemblyResolver&
                       assemblyResolver,
                   const BamlDecompilerSettings* settings = nullptr);

    // The C# `public XamlDecompiler(PEFile module, IAssemblyResolver
    // assemblyResolver, BamlDecompilerSettings settings)` -- the caller
    // file and resolver must outlive the decompiler.
    XamlDecompiler(const ILSpy::Decompiler::Metadata::MetadataFile& module,
                   const ILSpy::Decompiler::Metadata::IAssemblyResolver&
                       assemblyResolver,
                   const BamlDecompilerSettings* settings = nullptr);

    // The C# `public XamlDecompiler(BamlDecompilerTypeSystem typeSystem,
    // BamlDecompilerSettings settings)` -- the direct ctor: the caller-owned
    // type system, with the `TypeSystemOptions.Uncached` check over its
    // `MainModule` (an uncached type system is the ArgumentException).
    XamlDecompiler(const BamlDecompilerTypeSystem& typeSystem,
                   const BamlDecompilerSettings* settings = nullptr);

    // The port's interface-typed stand-in for the same C# ctor (the test
    // stubs are not BamlDecompilerTypeSystems): the Uncached check is not
    // possible over the interface and the module field stays null (the
    // check's only consumer). Passes a `BamlDecompilerTypeSystem` bind the
    // concrete overload above. Out-of-line like its siblings: an inline
    // definition would make every consuming TU compute this ctor's
    // exception specification over the owning unique_ptr members, whose
    // deleters need the complete forward-declared types.
    XamlDecompiler(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
                   const BamlDecompilerSettings* settings = nullptr);

    // The C# `BamlDecompilerSettings Settings { get; set; }` (the stored
    // reference, possibly null -- the C# get has no null-substitute).
    const BamlDecompilerSettings* Settings() const { return settings_; }
    void SetSettings(const BamlDecompilerSettings* settings) { settings_ = settings; }

    // The C# `BamlDecompilationResult Decompile(Stream stream)`: reads the
    // document, constructs the context, translates the root block through
    // its handler, wraps the element in the result document, runs every
    // rewrite pass, and collects the AssemblyIdMap full names, the first
    // x:Class name, and the generated-member tokens.
    BamlDecompilationResult Decompile(const std::uint8_t* data, std::size_t size);

private:
    // The C# `static PEFile LoadPEFile(string fileName, BamlDecompilerSettings
    // settings)` -- the loaded file with the C# failure arms (see the header
    // porting decisions).
    static std::unique_ptr<ILSpy::Decompiler::Metadata::MetadataFile>
    LoadPEFile(const std::string& fileName);

    // The C# private static `CreateTypeSystemFromFile`'s return value: the
    // type system plus the pieces the C# GC roots through it (the loaded
    // file and the built resolver, whose keep-alive registry owns every
    // resolved referenced file).
    struct CreateTypeSystemFromFileResult {
        std::unique_ptr<ILSpy::Decompiler::Metadata::MetadataFile> file;
        std::unique_ptr<ILSpy::Decompiler::Metadata::UniversalAssemblyResolver>
            resolver;
        std::unique_ptr<BamlDecompilerTypeSystem> typeSystem;
    };

    // The C# `static BamlDecompilerTypeSystem CreateTypeSystemFromFile(string
    // fileName, BamlDecompilerSettings settings)`.
    static CreateTypeSystemFromFileResult CreateTypeSystemFromFile(
        const std::string& fileName, const BamlDecompilerSettings& settings);

    // The C# ctor-4 body over a concrete type system: the field assignments
    // then the Uncached check.
    void Init(const BamlDecompilerTypeSystem& typeSystem,
              const BamlDecompilerSettings* settings);

    // The C# `readonly BamlDecompilerTypeSystem typeSystem` field (the
    // ICompilation surface -- XamlContext.Construct's parameter).
    const ILSpy::Decompiler::TypeSystem::ICompilation* typeSystem_;

    // The owning pieces of the file-based ctors (the C# GC roots the locals):
    // declared resolver -> file -> type system so the destruction order
    // (reverse) frees the type system before its file and before the
    // resolver whose keep-alive registry owns the resolved files.
    std::unique_ptr<ILSpy::Decompiler::Metadata::UniversalAssemblyResolver>
        ownedResolver_;
    std::unique_ptr<ILSpy::Decompiler::Metadata::MetadataFile> ownedFile_;
    std::unique_ptr<BamlDecompilerTypeSystem> ownedTypeSystem_;

    // The C# `BamlDecompilerSettings settings` field (nullable).
    const BamlDecompilerSettings* settings_;

    // The C# `private MetadataModule module` field (the ctor-4 assignment;
    // null over the interface stand-in).
    const ILSpy::Decompiler::TypeSystem::MetadataModule* module_ = nullptr;
};

} // namespace ILSpy::BamlDecompiler
