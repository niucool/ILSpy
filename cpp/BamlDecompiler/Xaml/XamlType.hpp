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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlType.cs (Ki, 2015, MIT): one
// resolved type of the XAML translation -- the assembly it lives in (nullable:
// the type may fail to resolve), its full assembly name, CLR namespace and
// name, the XML namespace `XamlContext.ResolveType` picks for it (nullable
// until `ResolveNamespace` runs), and the resolved `IType`.
//
// C#-to-C++ porting decisions:
//  * The C# `IModule Assembly { get; }` ("Can be null") ports to a nullable
//    non-owning `const IModule*`; `ResolvedType { get; set; }` ports to a
//    public `ITypePtr` member (the D271 shared handle).
//  * `XNamespace Namespace { get; private set; }` -- the null state is
//    observable (`ToXName` renders the no-namespace arm before
//    `ResolveNamespace` has run), so the field is
//    `std::optional<XNamespace>` with a public getter and private mutation
//    from `ResolveNamespace` (the private-setter surface).
//  * `ResolveNamespace`'s `elem.Annotation<XmlnsScope>()`: nothing in the C#
//    ever attaches a scope to an element (the `XmlnsDictionary.PushScope`
//    chain is the only scope producer and does not annotate), so the arm is
//    dead-but-ported; the port's live-object annotation convention holds an
//    annotated scope by `shared_ptr`, so the lookup spells
//    `Annotation<std::shared_ptr<XmlnsScope>>()` (the C# annotates the live
//    object, and a by-value `std::any` copy would silently detach the
//    in-place `NamespaceMap` fixups).
//  * `ToLowerInvariant` (the last namespace segment) is ASCII-scoped -- the
//    iteration-31 `ToUpperInvariant` precedent (the full .NET casing table
//    maps only exotic units; a CLR namespace segment is ASCII in practice).
//  * The C# `string.Split('.')`'s last element is exactly the substring after
//    the LAST '.' (or the whole string when there is none) -- the port takes
//    it directly (empty entries preserved: "a." and "" both yield "").
//  * The C# NRE surfaces (a null `ResolvedType` at `IsAttachedTo`'s caller
//    site is the only reachable one here -- none in this class) map to
//    `std::runtime_error` with the standard message where they occur.

#pragma once

#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XNamespace.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ILSpy::BamlDecompiler {

// The BAML decompiler's C# `using System.Xml.Linq` -- an alias to the port's
// stand-in namespace (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so its Xml references need the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

class XamlContext;
class XmlnsScope;

} // namespace ILSpy::BamlDecompiler

namespace ILSpy::BamlDecompiler::Xaml {

// The C# `internal class XamlType`.
class XamlType {
public:
    // The C# 4-arg ctor chains to the 5-arg one with a null xmlns.
    XamlType(const ILSpy::Decompiler::TypeSystem::IModule* assembly,
             std::string fullAssemblyName, std::string ns, std::string name)
        : XamlType(assembly, std::move(fullAssemblyName), std::move(ns), std::move(name),
                   std::nullopt)
    {
    }

    // The C# 5-arg ctor.
    XamlType(const ILSpy::Decompiler::TypeSystem::IModule* assembly,
             std::string fullAssemblyName, std::string ns, std::string name,
             std::optional<Xml::XNamespace> xmlns)
        : Assembly(assembly),
          FullAssemblyName(std::move(fullAssemblyName)),
          TypeNamespace(std::move(ns)),
          TypeName(std::move(name)),
          namespace_(std::move(xmlns))
    {
    }

    // The C# `void ResolveNamespace(XElement elem, XamlContext ctx)` -- picks
    // the XML namespace: the element's own scope annotation first, the
    // PIMapping table second, the assembly's XmlnsDefinitionAttribute third,
    // and the clr-namespace fallback last (which appends the
    // `xmlns:<prefix>` attribute -- and the global-namespace comment for the
    // empty namespace). Defined in XamlContext.cpp (the complete XamlContext
    // type).
    void ResolveNamespace(Xml::XElement& elem, XamlContext& ctx);

    // The C# `XName ToXName(XamlContext ctx)`.
    Xml::XName ToXName(XamlContext& ctx) const;

    // The C# `override string ToString() => TypeName`.
    std::string ToString() const
    {
        return TypeName;
    }

    // The C# `IModule Assembly { get; }` ("Can be null"; the assembly that
    // contains the type definition).
    const ILSpy::Decompiler::TypeSystem::IModule* Assembly = nullptr;

    // The C# `string FullAssemblyName { get; }`.
    std::string FullAssemblyName;

    // The C# `string TypeNamespace { get; }`.
    std::string TypeNamespace;

    // The C# `string TypeName { get; }`.
    std::string TypeName;

    // The C# `IType ResolvedType { get; set; }`.
    ILSpy::Decompiler::TypeSystem::ITypePtr ResolvedType;

    // The C# `XNamespace Namespace { get; private set; }` -- null until
    // `ResolveNamespace` runs.
    const std::optional<Xml::XNamespace>& Namespace() const
    {
        return namespace_;
    }

private:
    std::optional<Xml::XNamespace> namespace_;
};

} // namespace ILSpy::BamlDecompiler::Xaml
