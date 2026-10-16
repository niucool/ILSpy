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

// Port-authored stand-in for System.Xml.Linq.XObject (the abstract base of
// every node and attribute in the XLinq DOM): the parent back-pointer, the
// annotation list, and the Document walk. The BamlDecompiler annotates
// XElements with XamlType/XamlProperty/XmlnsScope instances and reads them
// back through Annotation<T>() (the rewrite passes), so the annotation
// machinery is load-bearing for the XAML decompiler chain.
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs).
//
// C#-to-C++ porting decisions:
//  * Annotations: the C# `object annotations` field is a single reference or
//    an array with null-growth compaction (a memory optimization); the port
//    keeps a std::vector<std::any> with the same observable order semantics
//    (AddAnnotation appends, Annotation<T>/Annotations<T> scan in insertion
//    order, RemoveAnnotations<T> keeps the rest in order). The C# `is T`
//    instance test allows derived matches; std::any matches the exact held
//    type -- every annotation type the BamlDecompiler uses (XamlType,
//    XamlProperty, BamlConnectionId, XmlnsScope, TargetTypeAnnotation) is a
//    concrete final type, so the difference is unobservable for this port's
//    consumers (documented divergence). A returned T* points into the stored
//    element and is invalidated by a later AddAnnotation on the same object
//    (the C# GC keeps the instance alive); consumers that need stability
//    store a std::shared_ptr<T> as the annotation.
//  * Events: the Changed/Changing events and the NotifyChanging/NotifyChanged/
//    SkipNotify notification machinery are NOT ported (no BamlDecompiler
//    consumer subscribes to a DOM change event). In the C#, every mutation
//    routes through SkipNotify() and takes the notification-free "skip notify"
//    path when no ancestor has a subscriber; the port implements exactly those
//    skip-notify paths, so the observable tree behavior is identical.
//  * BaseUri/HasBaseUri/SetBaseUri and the line-info members (LineNumber,
//    LinePosition, SetLineInfo, IXmlLineInfo) are NOT ported: they only carry
//    information the XmlReader LOAD paths record, and the BamlDecompiler
//    builds DOMs programmatically (documented deferral).
//  * XObject.Parent (`parent as XElement`) needs the complete XElement type;
//    it is declared here and defined in XElement.cpp with the XElement slice.
//  * ArgumentNullException arms are unreachable: std::string has no null.

#pragma once

#include <any>
#include <cstdint>
#include <vector>

#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XContainer;
class XDocument;
class XElement;
class XNode;

// System.Xml.Linq.SaveOptions.
enum class SaveOptions : std::uint32_t {
    None = 0,
    DisableFormatting = 1,
    OmitDuplicateNamespaces = 2,
};

class XObject {
public:
    XObject() = default;
    XObject(const XObject&) = delete;
    XObject& operator=(const XObject&) = delete;
    virtual ~XObject() = default;

    // Whether any annotation is held (the C# `annotations != null` walk gate
    // of GetSaveOptionsFromAnnotations).
    bool HasAnnotations() const
    {
        return !annotations_.empty();
    }

    // XObject.GetSaveOptionsFromAnnotations (internal): walks up the parent
    // chain for a SaveOptions annotation (the boxed enum value the C#
    // stores), or SaveOptions::None.
    SaveOptions GetSaveOptionsFromAnnotations() const;

    // The node type of this XObject (element, text, comment, ...).
    virtual XmlNodeType NodeType() const = 0;

    // XObject.Document: the owning XDocument (the ancestor walk stops at the
    // root; an unparented XDocument is its own Document).
    XDocument* Document();

    // XObject.Parent (`parent as XElement`): the parent when it is an element,
    // null for a document parent or an unparented node. Defined in
    // XElement.cpp (dynamic_cast needs the complete type).
    XElement* Parent();

    // XObject.AddAnnotation: appends to the annotation list.
    template <typename T>
    void AddAnnotation(T annotation)
    {
        annotations_.push_back(std::any(std::move(annotation)));
    }

    // XObject.Annotation<T>(): the first annotation of the held type, in
    // insertion order, or null.
    template <typename T>
    T* Annotation() const
    {
        for (const std::any& annotation : annotations_) {
            if (const T* value = std::any_cast<T>(&annotation))
                return const_cast<T*>(value);
        }
        return nullptr;
    }

    // XObject.Annotations<T>(): every annotation of the held type, in
    // insertion order.
    template <typename T>
    std::vector<T*> Annotations() const
    {
        std::vector<T*> result;
        for (const std::any& annotation : annotations_) {
            if (const T* value = std::any_cast<T>(&annotation))
                result.push_back(const_cast<T*>(value));
        }
        return result;
    }

    // XObject.RemoveAnnotations<T>(): removes every annotation of the held
    // type, keeping the others in order.
    template <typename T>
    void RemoveAnnotations()
    {
        std::vector<std::any> kept;
        kept.reserve(annotations_.size());
        for (std::any& annotation : annotations_) {
            if (!std::any_cast<T>(&annotation))
                kept.push_back(std::move(annotation));
        }
        annotations_ = std::move(kept);
    }

public:
    // The container this object is attached to (null while unparented).
    // C# internal: written by the XContainer attach/detach machinery and read
    // by every node-level sibling/ancestor walk; public here because the
    // derived-container code paths access it through XNode* (the C++ protected
    // access rule would require a friend web across the whole DOM).
    XContainer* parent_ = nullptr;

private:
    std::vector<std::any> annotations_;
};

} // namespace ILSpy::Decompiler::Xml
