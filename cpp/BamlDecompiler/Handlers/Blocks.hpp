// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.BamlDecompiler/Handlers/Blocks (Ki, 2015, MIT): the
// block-header handlers -- the half of the registry that drives the recursive
// HandlerMap.ProcessChildren walk. Landed so far:
//  * DocumentHandler (DocumentStart): the <Document> pseudo-element root the
//    XamlDecompiler.Decompile call renders (the caller wraps the returned
//    element into the XDocument).
//  * ElementHandler (ElementStart): the element translation -- the resolved
//    type's name, the owning XamlType annotation, the recursive children
//    walk, the resource-key defer branch, and the ResolveNamespace + rename
//    pair that attaches the xmlns.
//  * PropertyComplexHandler/PropertyArrayHandler/PropertyListHandler/
//    PropertyDictionaryHandler (the four property-element blocks -- the C#
//    classes carry literally identical Translate bodies): the property
//    element under the parent, the recursive children walk, and the
//    ResolveNamespace + rename pair.
//  * ConstructorParametersStartHandler (ConstructorParametersStart): the
//    pseudo-named <Ctor> wrapper element.
// (KeyElementStartHandler -- the ElementHandler subclass -- lands with the
// key/static-resource defer handlers.)
//
// C#-to-C++ porting decisions:
//  * The element annotations hold the OWNING `shared_ptr<XamlType>` the
//    context's type cache hands out (the C# GC reference -- the returned
//    XDocument outlives the XamlContext in XamlDecompiler.Decompile, so a
//    raw-pointer annotation would dangle; see ResolveTypeOwning).
//  * The C# `(IDeferHandler)HandlerMap.LookupHandler(...)` cast on the key
//    branch: a null handler NREs at the TranslateDefer call (the C# null
//    cast succeeds); a non-defer handler is the InvalidCastException with
//    the fixed '<handler>' placeholder (all three key-record handlers
//    implement IDeferHandler, so neither arm is reachable through a real
//    XamlResourceKey).

#pragma once

#include "BamlDecompiler/IHandlers.hpp"

namespace ILSpy::BamlDecompiler::Handlers {

// The C# `internal class DocumentHandler : IHandler`.
class DocumentHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class ElementHandler : IHandler` (KeyElementStartHandler's
// base -- its TranslateDefer re-drives this Translate over the key element).
class ElementHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The four property-element blocks (PropertyComplexStart/PropertyArrayStart/
// PropertyListStart/PropertyDictionaryStart): the C# classes carry literally
// identical Translate bodies over their own record types -- the port's four
// Translate members share the PropertyElementBlock helper.
class PropertyComplexHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

class PropertyArrayHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

class PropertyListHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

class PropertyDictionaryHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class ConstructorParametersStartHandler : IHandler` --
// the pseudo-named <Ctor> wrapper block (the constructor-argument children
// render inside it).
class ConstructorParametersStartHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

} // namespace ILSpy::BamlDecompiler::Handlers
