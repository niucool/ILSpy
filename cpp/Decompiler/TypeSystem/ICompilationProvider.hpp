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

// Port of ICSharpCode.Decompiler/TypeSystem/ICompilation.cs (the `ICompilationProvider`
// interface half, lines 83-91). `ICompilationProvider` is the 1-accessor base of
// `IEntity` (which `: ISymbol, ICompilationProvider, INamedElement`) and of `IModule`
// and `INamespace` (each `: ISymbol, ICompilationProvider`): it exposes just the parent
// `ICompilation` the entity/module/namespace belongs to. The C#
// `interface ICompilationProvider { ICompilation Compilation { get; } }` ports to a C++
// abstract base with a virtual destructor and one pure-virtual accessor returning
// `const ICompilation&`, the established C#-interface-to-C++-abstract-base convention
// (the `ISymbol` / `INamedElement` precedents in this port).
//
// The C# doc comment "This property never returns null" ports to a non-null reference
// return `const ICompilation&` (the D374 `IVariable::Type()` const-reference precedent):
// a compilation provider always has a compilation. `ICompilation` itself is NOT yet
// ported (its surface pulls `IModule` / `INamespace` / `IType` / `KnownTypeCode` /
// `CacheManager` / `TypeSystemOptions` / `StringComparer`, the D375/D376-noted cyclic
// `ICompilation` <-> `ICompilationProvider` <-> `IModule` / `INamespace` chain); it is
// only FORWARD-DECLARED here, because a reference return needs only a forward declaration
// (the `IAmbience.hpp` `ISymbol` / `IType` reference-parameter precedent, applied here to
// a reference return). The concrete `ICompilation` interface lands as a separate later
// leaf; this header compiles with `ICompilation` incomplete.
//
// It is a leaf TypeSystem dependency toward `IEntity` (the last of `IEntity`'s three bases
// after `ISymbol` (D372) and `INamedElement` (D375)), toward `IModule` / `INamespace`, and
// toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`).

#pragma once

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of `ICompilation` -- the parent compilation interface, not yet
// ported (its surface is a later leaf). A reference return needs only this forward
// declaration (the `IAmbience.hpp` `ISymbol` / `IType` precedent), so this header
// compiles with `ICompilation` incomplete.
class ICompilation;

// The 1-accessor base of `IEntity` / `IModule` / `INamespace`: exposes the parent
// `ICompilation`. A concrete compilation provider subclasses `ICompilationProvider`
// and overrides `Compilation()` (returning the compilation it belongs to, never null).
class ICompilationProvider {
public:
    virtual ~ICompilationProvider() = default;

    // The C# `ICompilation Compilation { get; }` -- the parent compilation. Never null
    // (the C# doc comment), so a non-null reference return (the `IVariable::Type()`
    // const-reference precedent).
    virtual const ICompilation& Compilation() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
