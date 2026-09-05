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

// Port of ICSharpCode.BamlDecompiler/IHandlers.cs (Ki, 2015, MIT): the
// record-type-to-handler dispatch of the XAML translation step --
//  * `IHandler`: one BAML record type (`Type`) plus the `Translate` step
//    that turns the record's node into a `BamlElement` of the XAML tree
//    (or null -- the handler contributes no node).
//  * `IDeferHandler`: the deferred-translation arm the key/static-resource
//    handlers implement (`TranslateDefer`) -- `ElementHandler` looks a
//    `XamlResourceKey`'s key node back up through the map and casts the
//    handler to this interface.
//  * `HandlerMap`: the process-wide registry the reflection walk of the C#
//    static ctor populates, `LookupHandler` over it, and `ProcessChildren`
//    -- the recursive walk that pushes/pops an XmlnsScope around every
//    block's children, annotates the element with that scope, dispatches
//    every child through its handler, and wires the returned elements into
//    the parent's children list.
//
// C#-to-C++ porting decisions:
//  * The C# static ctor discovers every `IHandler` implementation through
//    `typeof(IHandler).Assembly.GetTypes()` + `Activator.CreateInstance` --
//    C++ has no reflection, so the port carries the registry explicitly:
//    `CreateBuiltinHandlers()` constructs every PORTED handler class once
//    (the one-line-per-handler manifest -- the gold inventory it must
//    eventually reproduce is pinned in HandlerMap_Test.cpp: the 37 (record
//    type, class) rows the real engine's registry holds, dumped from the
//    shipped assembly through the C:/temp-probe/HandlerMapProbe reflection
//    probe). The map is populated lazily at first use (the function-local
//    static, the C# static-ctor timing) and the instances are owned by the
//    registry (`unique_ptr`, the C# GC holding the singletons).
//    The population runs through `InstallHandler` EXCEPT for record types an
//    explicit install has already claimed: the C# static ctor runs before
//    any user code and its reflection walk is the only writer, so the
//    pre-claimed skip is the port's test/embedding-seam precedence -- a
//    `ClearHandlers` + `InstallHandler` run shadows the manifest rows for
//    the types it claimed (manifest-internal duplicates still throw through
//    `InstallHandler`, the C# reflection-walk invariant).
//  * The C# `handlers.Add(handler.Type, handler)` throws
//    `ArgumentException` ("An item with the same key has already been
//    added. Key: Text") when two handler classes claim the same record
//    type -- the invariant the reflection walk relies on. The port's
//    `InstallHandler` reproduces the exact message (the key rendered
//    through `Baml::RecordTypeName`, the probed .NET enum ToString
//    spelling) as `std::invalid_argument`.
//  * The C# `LookupHandler` DEBUG block (a `NotSupportedException` for any
//    type outside AssemblyInfo/TypeInfo/AttributeInfo/StringInfo with no
//    handler) is compiled out of the release assembly the tool ships --
//    the port targets the release behavior (the XamlResourceKey
//    Debug.Assert convention): every lookup of an unregistered type
//    returns null and `ProcessChildren` skips the child.
//  * The C# `Debug.WriteLine("BAML Handler {0} not implemented.", ...)`
//    is a release no-op; the port documents the skip at its site.
//  * `ctx.CancellationToken.ThrowIfCancellationRequested()` per child is a
//    documented deferral (the XamlContext token convention: no ported
//    consumer cancels).
//  * `ProcessChildren`'s registry mutations: the C# static field has no
//    reset and its only writer is the static ctor; the port's explicit
//    registry needs `InstallHandler` (the shared `handlers.Add` path --
//    `EnsureLoaded` routes the manifest through it) and `ClearHandlers`
//    (the test/embedding seam: resets to the not-yet-populated state so
//    the next lookup re-runs the manifest; a `BamlElement`-tree translation
//    never calls them).
//  * The C# handler returns a `BamlElement` reference or null; the port
//    returns `std::unique_ptr<BamlElement>` (the `Children`-list ownership
//    convention of `BamlElement`).

#pragma once

#include "BamlDecompiler/Baml/BamlRecords.hpp"

#include <memory>
#include <vector>

namespace ILSpy::BamlDecompiler {

class BamlElement;
class XamlContext;

namespace Baml {
class BamlNode;
class BamlBlockNode;
} // namespace ILSpy::BamlDecompiler::Baml

// The C# `internal interface IHandler`: one record type plus the node ->
// BamlElement translation step (null: the handler contributes no element).
class IHandler {
public:
    virtual ~IHandler() = default;

    // The C# `BamlRecordType Type { get; }`.
    virtual Baml::BamlRecordType Type() const = 0;

    // The C# `BamlElement Translate(XamlContext ctx, BamlNode node,
    // BamlElement parent)` -- the parent is NULLABLE (the
    // XamlDecompiler.Decompile root call passes null; the null-parent derefs
    // the handlers perform throw the .NET NullReferenceException message,
    // the XmlnsDictionary convention).
    virtual std::unique_ptr<BamlElement> Translate(XamlContext& ctx,
        Baml::BamlNode& node, BamlElement* parent) = 0;
};

// The C# `internal interface IDeferHandler`: the deferred arm the
// key/static-resource handlers add to their `IHandler` translation.
class IDeferHandler {
public:
    virtual ~IDeferHandler() = default;

    // The C# `BamlElement TranslateDefer(XamlContext ctx, BamlNode node,
    // BamlElement parent)` (the nullable-parent convention of Translate).
    virtual std::unique_ptr<BamlElement> TranslateDefer(XamlContext& ctx,
        Baml::BamlNode& node, BamlElement* parent) = 0;
};

// The C# `internal static class HandlerMap`: the record-type dispatch.
class HandlerMap {
public:
    // The C# `static IHandler LookupHandler(BamlRecordType type)` -- the
    // RELEASE form (no throw: the DEBUG-only NotSupportedException block
    // is compiled out of the shipped assembly). Runs the manifest
    // population at first use (the C# static ctor's timing).
    static IHandler* LookupHandler(Baml::BamlRecordType type);

    // The C# `static void ProcessChildren(XamlContext ctx, BamlBlockNode
    // node, BamlElement nodeElem)`: push a scope for the element, annotate
    // the element's XElement with it, dispatch every child through its
    // handler (unregistered types are skipped -- the release
    // Debug.WriteLine no-op), wire the returned elements into the
    // children list with their Parent back-pointer, and pop the scope.
    static void ProcessChildren(XamlContext& ctx, Baml::BamlBlockNode& node,
        BamlElement& nodeElem);

    // The C# static ctor's registry population: constructs every ported
    // handler class ONCE (the explicit manifest standing in for the
    // reflection walk -- one construction line per ported handler). The
    // vector hands the instances to `EnsureLoaded`, which routes them
    // through `InstallHandler` (the shared `handlers.Add` path), skipping
    // record types an explicit install has already claimed (the seam
    // precedence -- see the porting decisions).
    static std::vector<std::unique_ptr<IHandler>> CreateBuiltinHandlers();

    // The `handlers.Add(handler.Type, handler)` step: takes ownership of
    // the handler, keyed by its `Type()`. Two handlers claiming the same
    // record type throw `std::invalid_argument` with the probed .NET
    // duplicate-key message.
    static void InstallHandler(std::unique_ptr<IHandler> handler);

    // Resets the registry to the not-yet-populated state (the C# static
    // field has no reset -- the test/embedding seam: the next lookup
    // re-runs the manifest).
    static void ClearHandlers();
};

} // namespace ILSpy::BamlDecompiler
