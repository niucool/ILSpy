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

// Port of ICSharpCode.BamlDecompiler/XamlContext.cs (Ki, 2015, MIT): the
// XAML-translation context one `XamlDecompiler.Decompile` run builds --
// the `BamlContext` (the record id maps and KnownThings), the parsed block
// tree (`RootNode`) and its header-to-block map, the PIMapping-fed
// `XmlnsDictionary`, the type/property/string id resolution with its caches,
// and the XML-namespace helpers.
//
// C#-to-C++ porting decisions:
//  * The `IDecompilerTypeSystem TypeSystem` parameter and property narrow to
//    `const ICompilation&` (the KnownThings/BamlContext porting precedent --
//    the port has no MetadataModule MainModule yet, so the interface's only
//    narrowing is documented as deferred with the Phase-7 back end).
//  * `CancellationToken` is a documented deferral (the BamlContext precedent:
//    no ported consumer cancels; the token parameter of `Construct` and
//    `BamlNode.Parse` drops).
//  * The `IDictionary<BamlRecord, BamlBlockNode> NodeMap` ports to an
//    `unordered_map` keyed by the non-owning record pointer (the C# reference
//    equality) with non-owning block values (the `RootNode` tree owns the
//    blocks; the document owns the records).
//  * `List<EntityHandle> GeneratedMembers`: an `EntityHandle` ports to its raw
//    token (the iteration-13 IDebugInfoProvider convention) --
//    `std::uint32_t`.
//  * `ResolveString`'s C# null (the missing-id arm) ports to
//    `std::optional<std::string>` nullopt; `GetXmlNamespace`'s null input and
//    output port to `std::optional<XNamespace>` the same way.
//  * The type/property caches own their rows by `unique_ptr` (the C#
//    `Dictionary<ushort, XamlType>` holds GC references the accessors hand
//    out); the handed-out pointers stay valid for the context's lifetime.
//  * `Baml.ResolveType`'s BAML-record arm calls
//    `ReflectionHelper.ParseReflectionName` -- NOT yet ported (it needs the
//    System.Reflection.Metadata `TypeName` parser). Following the loud
//    `std::logic_error` deferral convention, the arm throws until that lands;
//    the KnownThings arm (ids above 0x7fff) is fully functional.
//  * The `Baml()` accessor hides the namespace name `Baml` for the rest of
//    the class body (the self-named-accessor MSVC trap), so every
//    `Baml`-qualified declaration after it spells the fully-qualified
//    `ILSpy::BamlDecompiler::Baml::` form.

#pragma once

#include "BamlDecompiler/Baml/BamlContext.hpp"
#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "BamlDecompiler/XmlnsDictionary.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XName.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ILSpy::BamlDecompiler::Xaml {
class XamlProperty;
class XamlType;
} // namespace ILSpy::BamlDecompiler::Xaml

namespace ILSpy::BamlDecompiler {

// The BAML decompiler's C# `using System.Xml.Linq` -- an alias to the port's
// stand-in namespace (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so its Xml references need the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

// The C# `internal class XamlContext`.
class XamlContext {
public:
    // The C# `static XamlContext Construct(IDecompilerTypeSystem,
    // BamlDocument, CancellationToken, BamlDecompilerSettings)` (the token
    // deferral): the BamlContext record walk, the block-tree parse, the
    // PIMapping feed, and the node map, in the C# order. A null settings
    // pointer constructs the fresh default (the C# `?? new
    // BamlDecompilerSettings()`).
    static std::unique_ptr<XamlContext> Construct(
        const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
        Baml::BamlDocument& document,
        const BamlDecompilerSettings* bamlDecompilerOptions = nullptr);

    // The C# `IDecompilerTypeSystem TypeSystem { get; }` (the ICompilation
    // surface -- see the header porting decisions).
    const ILSpy::Decompiler::TypeSystem::ICompilation& TypeSystem() const
    {
        return typeSystem_;
    }

    // The C# `BamlDecompilerSettings Settings { get; private set; }`.
    const BamlDecompilerSettings& Settings() const
    {
        return *settings_;
    }

    // The C# `BamlContext Baml { get; private set; }`.
    const Baml::BamlContext& Baml() const
    {
        return *baml_;
    }

    // The C# `BamlNode RootNode { get; private set; }` (null for an empty
    // document -- `BamlNode.Parse` returns null).
    Baml::BamlBlockNode* RootNode() const
    {
        return rootNode_.get();
    }

    // The C# `IDictionary<BamlRecord, BamlBlockNode> NodeMap { get; }`.
    const std::unordered_map<const Baml::BamlRecord*, const Baml::BamlBlockNode*>&
    NodeMap() const
    {
        return nodeMap_;
    }

    // The C# `List<string> XClassNames { get; }` (mutable -- the
    // XClassRewritePass appends).
    std::vector<std::string>& XClassNames()
    {
        return xClassNames_;
    }

    // The C# `List<EntityHandle> GeneratedMembers { get; }` (mutable; the raw
    // token convention -- see the header porting decisions).
    std::vector<std::uint32_t>& GeneratedMembers()
    {
        return generatedMembers_;
    }

    // The C# `XmlnsDictionary XmlNs { get; }` (mutable -- the handlers
    // PushScope/PopScope on it).
    XmlnsDictionary& XmlNs()
    {
        return xmlNs_;
    }

    // The C# `XamlType ResolveType(ushort id)` -- the known-types arm (ids
    // above 0x7fff) through KnownThings, the BAML-record arm through the
    // record's assembly and (deferred) ParseReflectionName; the result is
    // cached under the raw id.
    Xaml::XamlType* ResolveType(std::uint16_t id);

    // The C# `XamlProperty ResolveProperty(ushort id)` -- the known-members
    // arm (ids above 0x7fff) through KnownThings, the AttributeInfoRecord arm
    // through `ResolveType(record.OwnerTypeId)`; `TryResolve` runs before the
    // result is returned, and it is cached under the raw id.
    Xaml::XamlProperty* ResolveProperty(std::uint16_t id);

    // The C# `string ResolveString(ushort id)` (the null of the missing-id arm
    // ports to nullopt).
    std::optional<std::string> ResolveString(std::uint16_t id);

    // The C# `XNamespace GetXmlNamespace(string xmlns)` (null in, null out --
    // the optional convention; the created namespace is cached).
    std::optional<Xml::XNamespace> GetXmlNamespace(const std::optional<std::string>& xmlns);

    // The C# `string TryGetXmlNamespace(IModule assembly, string
    // typeNamespace)` -- the assembly's XmlnsDefinitionAttribute rows for the
    // CLR namespace, preferring the Presentation namespace.
    std::optional<std::string> TryGetXmlNamespace(
        const ILSpy::Decompiler::TypeSystem::IModule* assembly,
        const std::string& typeNamespace) const;

    // The C# `XName GetKnownNamespace(string name, string xmlNamespace,
    // XElement context = null)`.
    Xml::XName GetKnownNamespace(const std::string& name, const std::string& xmlNamespace,
                                 const Xml::XElement* context = nullptr);

    // The C# `XName GetPseudoName(string name)`.
    Xml::XName GetPseudoName(const std::string& name) const;

    // The C# `public const string KnownNamespace_Xaml` /
    // `KnownNamespace_Presentation` / `KnownNamespace_PresentationOptions`.
    static constexpr const char* KnownNamespace_Xaml =
        "http://schemas.microsoft.com/winfx/2006/xaml";
    static constexpr const char* KnownNamespace_Presentation =
        "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
    static constexpr const char* KnownNamespace_PresentationOptions =
        "http://schemas.microsoft.com/winfx/2006/xaml/presentation/options";

    // The out-of-line destructor: the caches hold the forward-declared
    // XamlType/XamlProperty by unique_ptr, so it must be instantiated where
    // they are complete (the .cpp).
    ~XamlContext();

private:
    // The C# private ctor (the four mutable containers start empty).
    explicit XamlContext(const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem);

    // The C# `void BuildNodeMap(BamlBlockNode node)`.
    void BuildNodeMap(Baml::BamlBlockNode* node);

    // The C# `void BuildPIMappings(BamlDocument document)`.
    void BuildPIMappings(Baml::BamlDocument& document);

    // The C# `readonly IDecompilerTypeSystem typeSystem` (the ICompilation
    // surface).
    const ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem_;

    // The C# `BamlContext Baml` field: owned (the C# holds the instance
    // `ConstructContext` returns). The fully-qualified form: the `Baml()`
    // accessor above hides the namespace name for the rest of the class body.
    std::unique_ptr<ILSpy::BamlDecompiler::Baml::BamlContext> baml_;

    // The C# `BamlNode RootNode` field (owned -- `BamlNode.Parse`'s tree).
    std::unique_ptr<ILSpy::BamlDecompiler::Baml::BamlBlockNode> rootNode_;

    // The C# `IDictionary<BamlRecord, BamlBlockNode> NodeMap`.
    std::unordered_map<const ILSpy::BamlDecompiler::Baml::BamlRecord*,
                       const ILSpy::BamlDecompiler::Baml::BamlBlockNode*>
        nodeMap_;

    // The C# `Dictionary<ushort, XamlType> typeMap` (owning -- the C# GC
    // reference the dictionary holds).
    std::unordered_map<std::uint16_t, std::unique_ptr<Xaml::XamlType>> typeMap_;

    // The C# `Dictionary<ushort, XamlProperty> propertyMap` (owning).
    std::unordered_map<std::uint16_t, std::unique_ptr<Xaml::XamlProperty>> propertyMap_;

    // The C# `Dictionary<string, XNamespace> xmlnsMap`.
    std::unordered_map<std::string, Xml::XNamespace> xmlnsMap_;

    // The C# `List<string> XClassNames`.
    std::vector<std::string> xClassNames_;

    // The C# `List<EntityHandle> GeneratedMembers` (the raw-token convention).
    std::vector<std::uint32_t> generatedMembers_;

    // The C# `XmlnsDictionary XmlNs`.
    XmlnsDictionary xmlNs_;

    // The C# `BamlDecompilerSettings Settings`: the caller's instance is
    // kept non-owning; a null Construct argument makes this the owner of a
    // fresh default.
    const BamlDecompilerSettings* settings_ = nullptr;
    std::unique_ptr<BamlDecompilerSettings> ownedSettings_;
};

} // namespace ILSpy::BamlDecompiler
