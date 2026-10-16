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

// Port of ICSharpCode.BamlDecompiler/BamlElement.cs (Ki, 2015, MIT): the two
// value types the XAML-translation handlers carry between the BAML block tree
// and the System.Xml.Linq DOM.
//  * `XamlNode` is the readonly struct holding EITHER an `XElement` OR a
//    `string` (both possibly null) -- the `BamlElement.Xaml` slot the handlers
//    assign a whole element or a bare text value into, and read back through
//    the `implicit operator XElement` / `implicit operator string`.
//  * `BamlElement` pairs one `BamlNode` (the block-tree position) with its
//    translated `XamlNode`, a parent back-pointer, and the children list --
//    the tree `HandlerMap.ProcessChildren` recurses over.
//
// C#-to-C++ porting decisions:
//  * The C# `XElement Element` reference ports to `std::shared_ptr<XElement>`
//    (the DOM's GC-ownership model -- the node is simultaneously held by the
//    tree it was added to), null shared_ptr = the C# null; the `string String`
//    ports to `std::optional<std::string>`, nullopt = the C# null. The two
//    null states are observable (a text handler can resolve a missing string
//    id to null, and `parent.Xaml.Element` is read unguarded).
//  * The C# implicit conversion operators port to implicit converting
//    constructors and conversion operators. The `const char*` constructor
//    mirrors the C# string-literal arm (C++ would otherwise need two
//    user-defined conversions from a literal through std::string -- the
//    XName(const char*) precedent).
//  * `BamlElement.Node` ports to a non-owning `const BamlNode*` (the block
//    tree owns the nodes: `XamlContext.RootNode`'s unique_ptr tree); `Parent`
//    is the handlers' explicit back-pointer (the C# `Children.Add` does NOT
//    set it -- `ElementHandler` assigns it by hand), so `AddChild` appends
//    without touching it.
//  * The C# `IList<BamlElement> Children` (handler-created children the GC
//    keeps alive) ports to an owning `std::vector<std::unique_ptr<BamlElement>>`
//    with the `AddChild` mover -- the C# `doc.Children.Add(keyElem)` shape.

#pragma once

#include "Decompiler/Xml/XElement.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {
class BamlNode;
} // namespace ILSpy::BamlDecompiler::Baml

namespace ILSpy::BamlDecompiler {

// The BAML decompiler's C# `using System.Xml.Linq` -- an alias to the port's
// stand-in namespace (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so its Xml references need the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

// The C# `internal readonly struct XamlNode`.
struct XamlNode {
    // The C# `public readonly XElement Element;` (null for the string arm).
    std::shared_ptr<Xml::XElement> Element;

    // The C# `public readonly string String;` (null for the element arm).
    std::optional<std::string> String;

    XamlNode() = default;

    // The C# `XamlNode(XElement value)` + `implicit operator XamlNode(XElement)`.
    XamlNode(std::shared_ptr<Xml::XElement> value)
        : Element(std::move(value))
    {
    }

    // The C# `XamlNode(string value)` + `implicit operator XamlNode(string)`.
    XamlNode(std::string value)
        : String(std::move(value))
    {
    }

    // The string-literal arm (the XName(const char*) two-conversions rule).
    XamlNode(const char* value)
        : String(std::string(value))
    {
    }

    // The C# `implicit operator XElement(XamlNode)` -- the element, or null.
    operator std::shared_ptr<Xml::XElement>() const
    {
        return Element;
    }

    // The C# `implicit operator string(XamlNode)` -- the string, or null.
    operator std::optional<std::string>() const
    {
        return String;
    }
};

// The C# `internal class BamlElement`.
class BamlElement {
public:
    // The C# `BamlElement(BamlNode node)` ctor (the children list starts
    // empty).
    explicit BamlElement(const Baml::BamlNode* node)
        : Node(node)
    {
    }

    // The C# `BamlNode Node { get; }` (ctor-set; a plain field -- the class
    // has no encapsulated invariants).
    const Baml::BamlNode* Node = nullptr;

    // The C# `XamlNode Xaml { get; set; }`.
    XamlNode Xaml;

    // The C# `BamlElement Parent { get; set; }` -- assigned by hand by the
    // handlers (a non-owning back-pointer; the parent's children list owns).
    BamlElement* Parent = nullptr;

    // The C# `IList<BamlElement> Children { get; }` -- the owning child list
    // (the GC reference the C# list holds).
    std::vector<std::unique_ptr<BamlElement>> Children;

    // The C# `Children.Add(...)` arm (an owning move; Parent stays as the
    // caller set it -- the C# Add does not assign it).
    void AddChild(std::unique_ptr<BamlElement> child)
    {
        Children.push_back(std::move(child));
    }
};

} // namespace ILSpy::BamlDecompiler
