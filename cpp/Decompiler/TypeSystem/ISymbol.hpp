// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
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

// Port of ICSharpCode.Decompiler/TypeSystem/ISymbol.cs (the `ISymbol` interface,
// the `SymbolKind` enum half already lives in `SymbolKind.hpp`). `ISymbol` is the
// root interface every type-system symbol implements -- type definitions, members
// (methods/fields/properties/events), namespaces, parameters, variables, type
// parameters -- and exposes just two read-only properties: the `SymbolKind` (which
// derived interface the concrete symbol implements) and the short `Name`. The C#
// `interface ISymbol { SymbolKind SymbolKind { get; } string Name { get; } }` ports
// to a C++ abstract base with a virtual destructor and two pure-virtual accessors,
// the established C#-interface-to-C++-abstract-base convention (the `IType` /
// `IAstVisitor` precedents in this port). It is the leaf TypeSystem dependency of
// `CSharpAmbience` (which `ConvertSymbol(ISymbol)` and switches on
// `symbol.SymbolKind`), unblocking the next in-order output-stage file; the
// concrete `ITypeDefinition` / `IMember` / `INamespace` / ... sub-interfaces land
// with the rest of the Phase-2/5 type system.

#pragma once

#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

// The root of the type-system symbol hierarchy. A concrete symbol subclasses
// `ISymbol` and overrides `SymbolKind()` (reporting which kind it is) and
// `Name()` (the short display name). Equality is by identity (the C# interface
// has no equality contract); concrete symbols may add their own.
class ISymbol {
public:
    virtual ~ISymbol() = default;

    // The C# `SymbolKind SymbolKind { get; }` -- which kind of symbol this is
    // (which derived interface the concrete symbol implements). The `SymbolKind`
    // enum lives in this namespace; the unqualified return type resolves to it
    // (the method name does not shadow the enum in the same namespace).
    virtual SymbolKind SymbolKind() const = 0;

    // The C# `string Name { get; }` -- the short name of the symbol.
    virtual std::string Name() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
