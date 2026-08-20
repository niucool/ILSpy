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

// Port of ICSharpCode.Decompiler/TypeSystem/INamedElement.cs -- the `INamedElement`
// interface, the name-bearing base of `IType` and `IEntity`. `INamedElement` exposes
// the four name strings every named type-system element carries: the fully
// qualified `FullName`, the short `Name`, the round-trippable `ReflectionName`, and
// the containing `Namespace`. The C# `interface INamedElement { string FullName {
// get; } string Name { get; } string ReflectionName { get; } string Namespace {
// get; } }` ports to a C++ abstract base with a virtual destructor and four
// pure-virtual string accessors, the established C#-interface-to-C++-abstract-base
// convention (the `ISymbol` / `IType` precedents in this port).
//
// It is a leaf TypeSystem dependency toward `IEntity` (which `: ISymbol,
// ICompilationProvider, INamedElement`) and toward `TypeSystemAstBuilder` (the
// remaining blocker of `CSharpAmbience`): `TypeSystemAstBuilder` reads
// `entity.FullName` / `entity.Namespace` / `entity.ReflectionName` throughout its
// type- and member-printing paths, and `IType` (already a "minimal port" in
// `IType.hpp` that does not yet derive from `INamedElement`) carries the same four
// name strings. `INamedElement` is ported here as a standalone abstract base (the
// existing `IType.hpp` minimal port is left undisturbed); the `IEntity` port will
// derive from it. NOTE for the `IEntity` port: both `ISymbol` and `INamedElement`
// declare `Name()` with the same signature, so a concrete entity overrides `Name()`
// once -- the C# `IEntity` redeclares `Name` with `new` (hiding), but in C++ a single
// override satisfies both base contracts when the signatures match (the D374
// `IVariable` "inherited virtual covers the `new`" precedent); the multiple-inheritance
// diamond this creates is the `IEntity` port's concern, not this leaf's.

#pragma once

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

// The name-bearing base of `IType` and `IEntity`. A concrete named element
// subclasses `INamedElement` and overrides the four name accessors. The four names
// are: `FullName` (the dotted fully-qualified name, e.g. "System.Collections.Generic.List"),
// `Name` (the short name, e.g. "List"), `ReflectionName` (the round-trippable
// reflection name, e.g. "System.Collections.Generic.List`1[[System.String]]"), and
// `Namespace` (the containing namespace's full name, e.g. "System.Collections.Generic").
class INamedElement {
public:
    virtual ~INamedElement() = default;

    // The C# `string FullName { get; }` -- the dotted fully-qualified name.
    virtual std::string FullName() const = 0;

    // The C# `string Name { get; }` -- the short name. (Also declared by `ISymbol`;
    // a concrete `IEntity` overrides this once and satisfies both base contracts.)
    virtual std::string Name() const = 0;

    // The C# `string ReflectionName { get; }` -- the round-trippable reflection name.
    virtual std::string ReflectionName() const = 0;

    // The C# `string Namespace { get; }` -- the full name of the containing namespace.
    virtual std::string Namespace() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
