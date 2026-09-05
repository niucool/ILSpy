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

// Port-authored stand-in for System.Xml.Linq.XElement: the element half of
// the XLinq DOM -- a container with a name and a separate circular attribute
// list (lastAttr -> first -> ... -> lastAttr), plus the attribute attach/
// detach machinery, the attribute/child-element lookups, the namespace
// prefix resolution, and the lazy Elements()/Attributes() sequences.
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XElemProbe
// probe (C:/temp-probe/XElemProbe/Program.cs); the C# semantics were taken
// from the decompiled System.Private.Xml.Linq XElement.cs / XContainer.cs.
//
// The lazy sequences reproduce the C# iterator state machines EXACTLY (see
// XNode.hpp): both GetAttributes and GetElements stop after the yielded
// item when its parent link has been detached (an in-loop Remove ends the
// sequence after one item), and the stop conditions are re-read against the
// tree on every step.
//
// C#-to-C++ porting decisions:
//  * The mutation paths route through the C# *SkipNotify variants (events
//    are not ported -- see XObject.hpp); with no subscriber possible the
//    observable behavior is identical.
//  * SetAttributeValue's `object? value` with null = remove maps to
//    std::optional<std::string> (nullopt = remove; the string values are the
//    already-converted form, like the XAttribute ctor).
//  * The C# `Attributes(XName?)/Elements(XName?)` null-name arms (returning
//    the empty sequence) are covered by the no-argument overloads; the port
//    has no nullable name to pass.
//  * ToString()/Save/WriteTo (through the ElementWriter and the ported
//    XmlWriter stand-in) and XAttribute::ToString are ported with the
//    serialization slice. DEFERRED (the XmlReader paths): Load/Parse and
//    XElement.Parse (the BamlDecompiler's LiteralContentHandler needs it --
//    it requires the XmlReader text-parser stand-in). DEFERRED (unused by
//    the BamlDecompiler): Ancestors/AncestorsAndSelf/Descendants/
//    DescendantNodes/DescendantsAndSelf and the ElementsAfterSelf family
//    (they walk BamlNodes and IL trees in this codebase, not XElements).
//  * The cast operators (explicit operator string/int/... over Value) are
//    not ported (same reason as XAttribute's).

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "XAttribute.hpp"
#include "XContainer.hpp"
#include "XmlWriter.hpp"
#include "XName.hpp"
#include "XNamespace.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

// The lazy `Attributes()` sequence over an element's attribute list (the
// C# GetAttributes iterator: the stop condition re-read against the tree on
// every step, so removing the yielded attribute ends the sequence).
class XElementAttributes final {
public:
    class Iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::shared_ptr<XAttribute>;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::shared_ptr<XAttribute>*;
        using reference = const std::shared_ptr<XAttribute>&;

        // The end sentinel.
        Iterator() = default;

        reference operator*() const { return current_; }
        Iterator& operator++()
        {
            Advance();
            return *this;
        }

        friend bool operator==(const Iterator& lhs, const Iterator& rhs)
        {
            return lhs.done_ == rhs.done_;
        }
        friend bool operator!=(const Iterator& lhs, const Iterator& rhs)
        {
            return !(lhs == rhs);
        }

    private:
        friend class XElementAttributes;
        explicit Iterator(XElement* element, const XName* name)
            : element_(element)
            , name_(name)
            , done_(false)
        {
            Advance();
        }

        void Advance();

        XElement* element_ = nullptr;
        const XName* name_ = nullptr;
        XAttribute* position_ = nullptr;
        std::shared_ptr<XAttribute> current_;
        bool started_ = false;
        bool done_ = true;
    };

    explicit XElementAttributes(XElement& element)
        : element_(&element)
    {
    }

    // The filtered form (XElement.Attributes(XName)).
    XElementAttributes(XElement& element, const XName& name)
        : element_(&element)
        , name_(name)
    {
    }

    Iterator begin() const { return Iterator(element_, name_ ? &*name_ : nullptr); }
    Iterator end() const { return Iterator(); }

private:
    XElement* element_;
    std::optional<XName> name_;
};

class XElement : public XContainer {
public:
    // The C# `XElement(XName name)`.
    explicit XElement(XName name)
        : name_(std::move(name))
    {
    }

    // The C# `XElement(XName name, object? content)` / the params form -- a
    // braced XContent list is the params array.
    XElement(XName name, XContent content)
        : name_(std::move(name))
    {
        AddContentSkipNotify(std::move(content));
    }

    // The C# `XElement(XElement other)`: the deep-copy constructor (the
    // node content through the XContainer copy ctor, the attributes as
    // fresh copies in order).
    explicit XElement(const XElement& other);

    XmlNodeType NodeType() const override { return XmlNodeType::Element; }

    // XElement.Name: the expanded name.
    const XName& Name() const noexcept { return name_; }

    // XElement.Name setter.
    void Name(XName value) { name_ = std::move(value); }

    // XElement.Parse(text) -- LoadOptions.None (the only form the
    // BamlDecompiler's LiteralContentHandler uses). Implemented in
    // XmlTextParser.cpp (the XmlReader text-parser stand-in).
    static std::shared_ptr<XElement> Parse(const std::string& text);

    // XElement.FirstAttribute: the first attribute (in insertion order), or
    // null.
    XAttribute* FirstAttribute() const { return lastAttr_ != nullptr ? lastAttr_->next_ : nullptr; }

    // XElement.LastAttribute: the last attribute (in insertion order), or
    // null.
    XAttribute* LastAttribute() const { return lastAttr_; }

    // XElement.HasAttributes / HasElements / IsEmpty.
    bool HasAttributes() const { return lastAttr_ != nullptr; }
    bool HasElements() const;
    bool IsEmpty() const
    {
        return std::holds_alternative<std::monostate>(content_);
    }

    // XElement.Value: the concatenated text content ("" when there is none).
    std::string Value() const;

    // XElement.Value setter: removes every child node and appends the text.
    void Value(std::string value);

    // XElement.Attribute(XName): the attribute with the exact name, or null.
    XAttribute* Attribute(const XName& name) const;

    // XElement.Attributes()/Attributes(XName): the lazy attribute sequences
    // (see XElementAttributes).
    XElementAttributes Attributes() { return XElementAttributes(*this); }
    XElementAttributes Attributes(const XName& name) { return XElementAttributes(*this, name); }

    // XElement.SetAttributeValue: sets the attribute's value, adds the
    // attribute, or removes it (a disengaged optional is the C# null).
    void SetAttributeValue(const XName& name, std::optional<std::string> value);

    // XElement.RemoveAll: removes nodes and attributes.
    void RemoveAll()
    {
        RemoveAttributes();
        RemoveNodes();
    }

    // XElement.RemoveAttributes: detaches every attribute.
    void RemoveAttributes();

    // XElement.ReplaceAttributes: RemoveAttributes followed by Add.
    void ReplaceAttributes(XContent content)
    {
        RemoveAttributes();
        Add(std::move(content));
    }

    // XElement.ReplaceAll: RemoveAll followed by Add.
    void ReplaceAll(XContent content)
    {
        RemoveAll();
        Add(std::move(content));
    }

    // XElement.GetDefaultNamespace: the default namespace in scope (the
    // None namespace when none is declared).
    XNamespace GetDefaultNamespace() const;

    // XElement.GetNamespaceOfPrefix: the namespace bound to the prefix in
    // scope (nullopt when nothing binds it; the special xml/xmlns prefixes
    // fall back to their implicit namespaces).
    std::optional<XNamespace> GetNamespaceOfPrefix(const std::string& prefix) const;

    // XElement.GetPrefixOfNamespace: the prefix bound to the namespace
    // through the in-scope declarations (innermost scope wins), falling
    // back to the implicit xml/xmlns prefixes; nullopt when nothing binds
    // it (a default-namespace declaration never answers -- its own name
    // carries an empty namespace).
    std::optional<std::string> GetPrefixOfNamespace(const XNamespace& ns) const;

    // XElement.WriteTo: the ElementWriter serialization (the xmlns scope
    // walk and the prefix resolution). Defined in XElement.cpp.
    void WriteTo(XmlWriter& writer) const override;

    // XElement.Save(fileName): the file render (the declaration with the
    // settings' encoding, indented per the save options). The SaveOptions
    // overload is the C#'s Save(string, SaveOptions).
    void Save(const std::string& fileName) const;
    void Save(const std::string& fileName, SaveOptions options) const;

    // internal XElement.RemoveAttribute(XAttribute): detaches the attribute
    // from this element (throws when the attribute belongs to another
    // element). Public for the port's standing internal-surface convention
    // (XAttribute::Remove routes through it).
    void RemoveAttribute(XAttribute* attribute);

    std::shared_ptr<XNode> CloneNode() const override;

    bool DeepEquals(const XNode& other) const override;

    std::int32_t GetDeepHashCode() const override;

    // The circular attribute list: the LAST attribute (the list is empty
    // when null; lastAttr->next_ is the first). C# internal: public for the
    // XAttribute sibling walks and the attach machinery.
    XAttribute* lastAttr_ = nullptr;

    // internal XElement.AppendAttributeSkipNotify: the list attach (no
    // duplicate check). C# internal; public for the XmlTextParser load path
    // (the same access the C# XLinq ContentReader has).
    void AppendAttributeSkipNotify(std::shared_ptr<XAttribute> attribute);

    // The C# internal `name` field: public for the XContainer::Element/
    // Elements lookups (they compare it directly, like the C# internal
    // access). The Name() getter remains the public surface.
    XName name_;

protected:
    // internal overrides: the attribute attach with the duplicate check and
    // the clone-on-attach (the C# AddAttribute/AddAttributeSkipNotify).
    void AddAttribute(std::shared_ptr<XAttribute> attribute) override;
    void AddAttributeSkipNotify(std::shared_ptr<XAttribute> attribute) override;

    // internal XElement.ValidateNode override: documents and document types
    // cannot be added to an element.
    void ValidateNode(const XNode& node, const XNode* previous) override;

private:
    friend class XElementAttributes;

    // internal XElement.AppendAttribute: the list attach with the
    // parented-node guard (AppendAttributeSkipNotify is public above for
    // the XmlTextParser load path).
    void AppendAttribute(std::shared_ptr<XAttribute> attribute);

    // private XElement.AttributesEqual: the pairwise attribute comparison
    // behind DeepEquals.
    bool AttributesEqual(const XElement& other) const;

    // private XElement.GetNamespaceOfPrefixInScope: the in-scope walk from
    // this element up to (excluding) outOfScope (a null outOfScope walks to
    // the root).
    std::optional<std::string> GetNamespaceOfPrefixInScope(
        const std::string& prefix, const XElement* outOfScope) const;

    // The ownership list of every attached attribute (the GC stand-in; order
    // is irrelevant -- the next_ links carry it).
    std::vector<std::shared_ptr<XAttribute>> ownedAttrs_;
};

} // namespace ILSpy::Decompiler::Xml
