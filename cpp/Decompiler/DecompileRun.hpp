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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/DecompileRun.cs -- the per-run state bag the
// CSharpDecompiler fills before decompiling: the #if/#define symbols, the namespaces
// the RequiredNamespaceCollector gathered, the DecompilerSettings, and the
// C#-using-scope root. The ExpressionBuilder / StatementBuilder / CallBuilder
// slices read `decompileRun.UsingScope` and route their member-hiding queries
// through the run.
//
// Deferrals (documented at each member): the CancellationToken (the port has no
// cooperative-cancel machinery -- the C# ctor only stores it, and every consumer
// calls ThrowIfCancellationRequested() which is a no-op here); RecordDecompilers /
// TypeHierarchyIsKnown (the CSharpDecompiler type-declaration slices that fill
// them are not ported yet -- the dictionaries land with those slices). The
// DocumentationProvider is a caller-supplied interface pointer (the concrete
// XmlDocumentationProvider stays with a later Decompiler slice).

#pragma once

#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompilerSettings.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ILSpy::Decompiler::Documentation {
class IDocumentationProvider;
}

namespace ILSpy::Decompiler::TypeSystem {
class IEvent;
class IField;
}

namespace ILSpy::Decompiler {

// The C# `enum EnumValueDisplayMode` (the ICSharpCode.Decompiler root namespace):
// how enum member values are displayed. Declaration order None/All/AllHex/FirstOnly.
enum class EnumValueDisplayMode {
    None,
    All,
    AllHex,
    FirstOnly
};

// The C# `internal class DecompileRun`. The C# `RecordDecompilers` /
// `AutomaticEvents` / `TypeHierarchyIsKnown` members are the documented deferrals
// above.
class DecompileRun {
public:
    // The C# `public HashSet<string> DefinedSymbols { get; }` -- the #define symbols
    // of the current run. Read-only after construction (the C# get-only property);
    // callers mutate through the reference, so the port hands the container back.
    std::unordered_set<std::string>& DefinedSymbols() { return definedSymbols_; }
    const std::unordered_set<std::string>& DefinedSymbols() const { return definedSymbols_; }

    // The C# `public HashSet<string> Namespaces { get; set; }` -- the set the
    // RequiredNamespaceCollector fills. The C# property is nullable (assigned by the
    // caller between construction and use), so the port carries the optional.
    std::optional<std::unordered_set<std::string>>& Namespaces() { return namespaces_; }
    const std::optional<std::unordered_set<std::string>>& Namespaces() const {
        return namespaces_;
    }
    void SetNamespaces(std::optional<std::unordered_set<std::string>> namespaces) {
        namespaces_ = std::move(namespaces);
    }

    // The C# `public DecompilerSettings Settings { get; }` (a reference the run
    // aliases; the caller owns the bag -- the XamlDecompiler `const Settings*`
    // convention).
    const DecompilerSettings& Settings() const { return *settings_; }

    // The C# `public UsingScope UsingScope { get; }` -- the root using scope the
    // CSharpResolver is built from. The owning shared handle (the C# GC reference;
    // the CSharpTypeResolveContext ctor takes the shared_ptr).
    const std::shared_ptr<CSharp::TypeSystem::UsingScope>& UsingScope() const {
        return usingScope_;
    }

    // The C# `public IDocumentationProvider? DocumentationProvider { get; set; }` --
    // the optional XML documentation source. A non-owning pointer (the C# nullable
    // reference; the caller owns the provider). Null when no documentation is
    // supplied, which is the `AddXmlDocumentationTransform` early-out.
    const Documentation::IDocumentationProvider* DocumentationProvider() const {
        return documentationProvider_;
    }
    void SetDocumentationProvider(const Documentation::IDocumentationProvider* provider) {
        documentationProvider_ = provider;
    }

    // The C# `public Dictionary<IEvent, IField?> AutomaticEvents { get; }` -- the
    // memoized AutoEventDecompiler verdicts (a null value is the "not an automatic
    // event" verdict). The C# ctor initializes it empty; consumers route through
    // AutoEventDecompiler.IsAutomaticEvent, which memoizes so all consumers decide
    // from the same analysis. The keys/values are non-owning (the type system owns
    // the members); the map keys on the IEvent identity (the C# reference equality).
    std::unordered_map<const TypeSystem::IEvent*, const TypeSystem::IField*>&
    AutomaticEvents() {
        return automaticEvents_;
    }

    // The C# ctor `DecompileRun(DecompilerSettings settings, UsingScope usingScope)`
    // with both ArgumentNullException guards. The settings port alias: the caller
    // owns the bag (the C# GC reference); the using scope is the port's
    // shared_ptr-managed UsingScope (the CSharpTypeResolveContext::WithUsingScope
    // ownership convention).
    DecompileRun(const DecompilerSettings* settings,
                 std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope)
        : settings_(settings), usingScope_(std::move(usingScope))
    {
        if (settings_ == nullptr)
            throw std::invalid_argument("settings");
        if (usingScope_ == nullptr)
            throw std::invalid_argument("usingScope");
    }

private:
    std::unordered_set<std::string> definedSymbols_;
    std::optional<std::unordered_set<std::string>> namespaces_;
    const DecompilerSettings* settings_;
    std::shared_ptr<CSharp::TypeSystem::UsingScope> usingScope_;
    const Documentation::IDocumentationProvider* documentationProvider_ = nullptr;
    std::unordered_map<const TypeSystem::IEvent*, const TypeSystem::IField*>
        automaticEvents_;
};

} // namespace ILSpy::Decompiler
