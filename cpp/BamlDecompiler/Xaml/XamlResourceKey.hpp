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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlResourceKey.cs (Ki, 2015,
// MIT): the resource-key annotation the key-record handlers attach to the
// block tree -- the key node, the handler-assigned key element, the collected
// static resources, and the sibling/ancestor key lookups.
//
// C#-to-C++ porting decisions:
//  * The Debug.Assert/Debug.WriteLine calls inside the private ctor are
//    compiled out of the release assembly the tool ships (Conditional("DEBUG")
//    methods), so the port carries only their observable control flow: the
//    third arm's Debug.WriteLine does nothing and the not-found walk ends
//    with no annotation.
//  * The C# GC roots the XamlResourceKey through the node annotations
//    (KeyElementStartHandler discards Create's return and re-reads the key
//    from node.Annotation later), so the port's annotations must hold the
//    OWNING shared_ptr. The private ctor therefore records the
//    `X.Annotation = this` targets and Create applies them with the owning
//    shared_ptr -- the ctor-local alias pattern would dangle.
//  * The port's BamlNode.Annotation stores shared_ptr<XamlResourceKey> (the
//    live-object annotation convention) and the Find* lookups read it back
//    with the same instance identity.
//  * The C# `(IBamlDeferRecord)node.Record` cast throws InvalidCastException
//    ("Unable to cast object of type '<record>' to type
//    'ICSharpCode.BamlDecompiler.Baml.IBamlDeferRecord'.") for a header that
//    implements no defer interface -- unreachable through the engine (the
//    three Create callers only pass key-record nodes), so the port maps it to
//    a fixed-message std::runtime_error (the dynamic C# type name is the
//    documented divergence). A null record (a hand-built headerless block)
//    keeps the C#'s null-cast + null-deref shape: the NRE at
//    keyRecord.Record.
//  * `BamlElement KeyElement { get; set; }` and `IList<BamlNode>
//    StaticResources` are non-owning (the handler's BamlElement tree and the
//    block tree own their nodes).
//  * The .NET NullReferenceExceptions (the null parent at each read site,
//    the null resolved defer target at keyRecord.Record.Type) map to
//    std::runtime_error carrying the standard message.

#pragma once

#include "BamlDecompiler/Baml/BamlNode.hpp"

#include <memory>
#include <vector>

namespace ILSpy::BamlDecompiler {

class BamlElement;

namespace Xaml {

// The C# `internal class XamlResourceKey`.
class XamlResourceKey {
public:
    // The C# `public static XamlResourceKey Create(BamlNode node)` -- the
    // private ctor's arm walk plus the annotation attachment (the owning
    // shared_ptr -- see the file note).
    static std::shared_ptr<XamlResourceKey> Create(Baml::BamlNode& node);

    // The C# `public static XamlResourceKey FindKeyInSiblings(BamlNode
    // node)` -- scans the parent's children BACKWARD from the node's own
    // position for the nearest annotated sibling (the node itself included).
    static std::shared_ptr<XamlResourceKey> FindKeyInSiblings(Baml::BamlNode& node);

    // The C# `public static XamlResourceKey FindKeyInAncestors(BamlNode
    // node)` -- delegates to the two-arg overload discarding `found`.
    static std::shared_ptr<XamlResourceKey> FindKeyInAncestors(Baml::BamlNode& node);

    // The C# `public static XamlResourceKey FindKeyInAncestors(BamlNode
    // node, out BamlNode found)` -- walks the node and its ancestors for the
    // first key annotation, reporting the carrying node.
    static std::shared_ptr<XamlResourceKey> FindKeyInAncestors(Baml::BamlNode& node,
        Baml::BamlNode*& found);

    // The C# `public BamlNode KeyNode { get; set; }` -- non-owning (the
    // block tree owns the node).
    Baml::BamlNode* KeyNode = nullptr;

    // The C# `public BamlElement KeyElement { get; set; }` -- non-owning
    // (the handler's BamlElement tree owns it; assigned by the
    // KeyElementStart/DefAttributeKey* handlers).
    BamlElement* KeyElement = nullptr;

    // The C# `public IList<BamlNode> StaticResources` -- the collected
    // static-resource nodes (non-owning; the
    // StaticResourceStart/OptimizedStaticResource handlers append).
    std::vector<Baml::BamlNode*> StaticResources;

private:
    explicit XamlResourceKey(Baml::BamlNode& node);

    // The C# ctor's `X.Annotation = this` targets in assignment order,
    // applied by Create with the owning shared_ptr (see the file note).
    std::vector<Baml::BamlNode*> annotateTargets_;
};

} // namespace ILSpy::BamlDecompiler::Xaml

} // namespace ILSpy::BamlDecompiler
