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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlExtension.cs (Ki, 2015, MIT):
// the markup-extension value a `{...}` attribute renders -- the extension's
// XamlType, its positional initializer arguments, and the named-argument
// table, with the ToString that renders the `{TypeName args, name=value}`
// spelling (the leading 'Extension' suffix of the rendered type name is
// stripped per the C#).
//
// C#-to-C++ porting decisions:
//  * The C# `object[] Initializer` / `IDictionary<string, object>
//    NamedArguments` values are modeled by XamlObject (a three-arm variant):
//    every reachable shape is a `string`, a nested `XamlExtension`, or the
//    C# null (PropertyWithExtensionHandler stores ctx.ToString strings and
//    the nested Type/Static extensions; MarkupExtensionRewritePass.InlineObject
//    stores XText values and nested inline extensions -- and its
//    `NamedArguments[name] = value` arm CAN store the null of a
//    non-text/non-element single node, which the render's `value.ToString()`
//    then NREs on). The null ports to std::monostate and the NRE to
//    std::runtime_error carrying the .NET message.
//  * `object[] Initializer { get; set; }` starts null and its null state is
//    observable (the `Initializer != null && Initializer.Length > 0` render
//    gate -- an EMPTY array renders no space while a null one also renders
//    none, but MarkupExtensionRewritePass's `if (ext.Initializer != null)`
//    guard distinguishes them), so it ports to
//    `std::optional<std::vector<XamlObject>>`.
//  * `IDictionary<string, object> NamedArguments` iterates in INSERTION order
//    (the .NET Dictionary convention) with the indexer's replace-in-place
//    semantics -- the insertion-ordered vector of pairs plus the SetNamedArgument
//    helper reproducing the indexer (the XmlnsDictionary precedent).
//  * `XamlType ExtensionType { get; }` is a non-owning pointer: the C# holds
//    the shared mutable instance the XamlContext's ResolveType cache (or the
//    caller's element annotation) owns, and ToString's ResolveNamespace
//    mutates it.

#pragma once

#include "Decompiler/Xml/XElement.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ILSpy::BamlDecompiler {

// The BAML decompiler's C# `using System.Xml.Linq` -- an alias to the port's
// stand-in namespace (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so its Xml references need the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

class XamlContext;

namespace Xaml {

class XamlType;

class XamlExtension;

// The C# `object` values the Initializer array and the NamedArguments table
// carry (see the file note): a string, a nested extension, or the C# null
// (std::monostate).
using XamlObject = std::variant<std::monostate, std::string,
    std::shared_ptr<XamlExtension>>;

// The C# `internal class XamlExtension`.
class XamlExtension {
public:
    // The C# ctor (the named-arguments table starts empty).
    explicit XamlExtension(XamlType* type);

    // The C# `string ToString(XamlContext ctx, XElement ctxElement)` -- the
    // `{TypeName ...}` render (see the file note).
    std::string ToString(XamlContext& ctx, Xml::XElement& ctxElement);

    // The C# `XamlType ExtensionType { get; }` -- non-owning (the context's
    // ResolveType cache or the caller's annotation owns the instance).
    XamlType* ExtensionType = nullptr;

    // The C# `object[] Initializer { get; set; }` (null ports to nullopt).
    std::optional<std::vector<XamlObject>> Initializer;

    // The C# `IDictionary<string, object> NamedArguments { get; }` --
    // insertion-ordered with the indexer's replace-in-place semantics.
    std::vector<std::pair<std::string, XamlObject>> NamedArguments;

    // The C# `NamedArguments[key] = value` indexer: replaces an existing
    // key's value in place (keeping its position), else appends.
    void SetNamedArgument(std::string key, XamlObject value);

private:
    // The C# `static void WriteObject(StringBuilder, XamlContext, XElement,
    // object)`.
    static void WriteObject(std::string& sb, XamlContext& ctx,
        Xml::XElement& ctxElement, const XamlObject& value);
};

} // namespace ILSpy::BamlDecompiler::Xaml

} // namespace ILSpy::BamlDecompiler
