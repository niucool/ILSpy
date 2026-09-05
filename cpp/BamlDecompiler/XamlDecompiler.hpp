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
// engine facade -- the rewrite-pass chain plus the `Decompile` composition
// (BamlReader.ReadDocument -> XamlContext.Construct ->
// HandlerMap.LookupHandler(RootNode.Type) -> Translate -> the passes -> the
// BamlDecompilationResult).
//
// C#-to-C++ porting decisions:
//  * The four C# constructors all build a `BamlDecompilerTypeSystem` (the
//    `fileName` pair through `CreateTypeSystemFromFile`'s PEFile +
//    UniversalAssemblyResolver, the `PEFile` pair through the resolver, and
//    the `BamlDecompilerTypeSystem` pair directly) and run the
//    `TypeSystemOptions.Uncached` check over the concrete MetadataModule --
//    all gated on the Phase-7 MetadataModule back end (the port has no
//    concrete metadata-loading module type system yet), documented here as
//    deferred with it. The port's constructor takes the `ICompilation`
//    surface (the `XamlContext.Construct` narrowing precedent); the
//    settings parameter keeps the C# shape (nullable -- the C# ctor accepts
//    a null settings and `XamlContext.Construct`'s `?? new
//    BamlDecompilerSettings()` builds the default).
//  * `CancellationToken` (the property and the per-record/per-pass
//    `ThrowIfCancellationRequested` calls) is a documented deferral (the
//    XamlContext convention: no ported consumer cancels).
//  * The C# `static readonly IRewritePass[] rewritePasses` (XClass,
//    MarkupExtension, Attribute, ConnectionId, Document) ports to a
//    function-local static array of the four ported passes in the C# order;
//    ConnectionIdRewritePass's row stays deferred with the Phase-3/4
//    ILAst machinery (its position -- between AttributeRewritePass and
//    DocumentRewritePass -- is what the port resumes when that lands). For
//    a document with no ConnectionId annotations the pass is a no-op in
//    the C#, so the port's four-pass chain is observably identical over
//    every stream the port can currently decompile.
//  * `Decompile(Stream stream)` ports to the byte-span convention
//    (`BamlReader.ReadDocument`'s signature).
//  * The C# `ctx.RootNode.Type` and `elem.Xaml.Element` null-derefs (an
//    empty document's `BamlNode.Parse` returns null; a root handler
//    returning null) port to explicit `std::runtime_error`s carrying the
//    standard .NET NullReferenceException message (the XmlnsDictionary
//    convention).

#pragma once

#include "BamlDecompiler/BamlDecompilationResult.hpp"
#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"

#include <cstddef>
#include <cstdint>

namespace ILSpy::BamlDecompiler {

// The C# `public class XamlDecompiler`.
class XamlDecompiler {
public:
    // The port's stand-in for the C# constructor family (see the header
    // porting decisions: the BamlDecompilerTypeSystem/PEFile/resolver
    // machinery is deferred with the Phase-7 MetadataModule back end);
    // `settings` may be null (the C# null-tolerant ctor shape -- Decompile
    // passes it to `XamlContext.Construct`, which builds the default).
    XamlDecompiler(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
        const BamlDecompilerSettings* settings = nullptr)
        : typeSystem_(typeSystem), settings_(settings)
    {
    }

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
    // The C# `readonly BamlDecompilerTypeSystem typeSystem` field (the
    // ICompilation surface -- see the header porting decisions).
    const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem_;

    // The C# `BamlDecompilerSettings settings` field (nullable).
    const BamlDecompilerSettings* settings_;
};

} // namespace ILSpy::BamlDecompiler
