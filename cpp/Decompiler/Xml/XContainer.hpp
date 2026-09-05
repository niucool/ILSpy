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

// Port-authored stand-in for System.Xml.Linq.XContainer (the abstract base of
// XElement and XDocument): the child storage, the Add machinery, the lazy
// Nodes() sequence, RemoveNodes/ReplaceNodes, and the Inserter that powers
// AddFirst/AddBeforeSelf/AddAfterSelf/ReplaceWith.
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs); the C# semantics were taken
// from the decompiled System.Private.Xml.Linq sources (XContainer.cs,
// Inserter in XNode.cs).
//
// KEY DESIGN (content storage): the C# `internal object content` field holds
// either null, a System.String (a container whose only content is text), or
// the LAST child XNode of a circular singly-linked sibling list whose `next`
// wraps back to the first child. The port models the field as a
// std::variant<std::monostate, std::string, XNode*> with exactly those
// alternatives, and keeps every public read path observably identical --
// including the LAZY materialization in LastNode() (the C# getter converts
// nonempty string content into an XText node on first read) and the
// empty-string-content state (XDocument.Add("") records "" as content, which
// Nodes() skips but DeepEquals distinguishes from no content at all).
//
// KEY DESIGN (ownership): the C# DOM is garbage-collected. The port models it
// with std::shared_ptr: every ATTACHED node is held by its container's
// owned_ list (the GC stand-in), created through std::make_shared by the
// caller and adopted on attach. Detaching (Remove/RemoveNodes) drops the
// container's reference -- a detached node survives exactly while some
// shared_ptr still holds it, which is what the BamlDecompiler rewrite passes
// rely on when they collect detached attributes and re-add them. The sibling
// next_ links are raw order-carrying pointers (see XNode).
//
// The mutation paths route through the C# *SkipNotify variants: the notify
// variants exist in the C# only to raise the Changed/Changing events, which
// are not ported (see XObject.hpp) -- with no subscriber possible the C#
// SkipNotify() check is unconditionally true, so the observable behavior is
// identical.
//
// WriteContentTo (the serialization walk over the content) is ported with the
// XmlWriter slice. The Element/Elements surface is declared here and
// defined in XElement.cpp with the XElement slice (it needs the complete
// XElement type). XContainer::AddNode's `ValidateNode(n, this)` quirk is
// ported faithfully (the C# passes the container itself as `previous`, so
// the document-structure position check never flips on the append path --
// only the Inserter path passes a real predecessor). The ancestor/descendant
// sequences (Ancestors/Descendants/DescendantNodes) are deferred: no
// BamlDecompiler call site walks XElement ancestors or descendants.
//
// The DOM API is non-const throughout: the C# has no notion of const and
// several C# "getters" mutate (LastNode materializes string content).

#pragma once

#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "XName.hpp"
#include "XNode.hpp"
#include "XmlWriter.hpp"

namespace ILSpy::Decompiler::Xml {

class XAttribute;
class XElement;

// The C# internal Inserter (XNode.cs): the insert-before/after engine over a
// container and an anchor node, with the pending-text batching that merges
// adjacent text content. AddAfterSelf anchors at the node itself;
// AddBeforeSelf/ReplaceWith anchor at the predecessor (null = insert first).
class Inserter {
public:
    Inserter(XContainer& parent, XNode* anchor);

    // Inserter.Add: add the content at the anchor, flushing any batched text.
    void Add(XContent content);

private:
    friend class XContainer;

    void AddContent(const XContent& content);
    void AddNode(std::shared_ptr<XNode> node);
    void AddString(std::string s);
    void InsertNode(std::shared_ptr<XNode> node);

    XContainer& parent_;
    XNode* previous_;
    std::optional<std::string> text_;
};

// The lazy `Elements()` sequence over a container's child elements (the C#
// GetElements iterator: only XElement children pass, the stop condition
// re-read against the tree on every step -- removing the yielded element
// ends the iteration). Defined in XElement.cpp (the iterator needs the
// complete XElement type).
class XContainerElements final {
public:
    class Iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::shared_ptr<XElement>;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::shared_ptr<XElement>*;
        using reference = const std::shared_ptr<XElement>&;

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
        friend class XContainerElements;
        explicit Iterator(XContainer* container, const XName* name)
            : container_(container)
            , name_(name)
            , done_(false)
        {
            Advance();
        }

        void Advance();

        XContainer* container_ = nullptr;
        const XName* name_ = nullptr;
        XNode* position_ = nullptr;
        std::shared_ptr<XElement> current_;
        bool started_ = false;
        bool done_ = true;
    };

    explicit XContainerElements(XContainer& container)
        : container_(&container)
    {
    }

    // The filtered form (XContainer.Elements(XName)).
    XContainerElements(XContainer& container, const XName& name)
        : container_(&container)
        , name_(name)
    {
    }

    Iterator begin() const { return Iterator(container_, name_ ? &*name_ : nullptr); }
    Iterator end() const { return Iterator(); }

private:
    XContainer* container_;
    std::optional<XName> name_;
};

// The lazy `Nodes()` sequence over a container's children (the C# iterator's
// stop conditions re-checked against the tree on every step -- removing the
// current node ends the iteration).
class XContainerNodes final {
public:
    class Iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::shared_ptr<XNode>;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::shared_ptr<XNode>*;
        using reference = const std::shared_ptr<XNode>&;

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
        friend class XContainerNodes;
        explicit Iterator(XContainer* container)
            : container_(container)
            , done_(false)
        {
            Advance();
        }

        void Advance();

        XContainer* container_ = nullptr;
        XNode* position_ = nullptr;
        std::shared_ptr<XNode> current_;
        bool started_ = false;
        bool done_ = true;
    };

    explicit XContainerNodes(XContainer& container)
        : container_(&container)
    {
    }

    Iterator begin() const { return Iterator(container_); }
    Iterator end() const { return Iterator(); }

private:
    XContainer* container_;
};

class XContainer : public XNode {
public:
    // XContainer.FirstNode: the first child node (null when there is none; a
    // nonempty string content materializes on the read).
    XNode* FirstNode();

    // XContainer.LastNode: the last child node. A nonempty string content is
    // MATERIALIZED into an XText node by this read (the C# getter mutates).
    XNode* LastNode();

    // XContainer.Add: appends the content (a node is cloned when it is
    // already parented or is an ancestor of this container).
    void Add(XContent content);

    // XContainer.AddFirst: inserts the content before all children.
    void AddFirst(XContent content);

    // XContainer.Nodes: the lazy child sequence.
    XContainerNodes Nodes();

    // XContainer.Element(XName): the first child element with the exact
    // name, or null. Defined in XElement.cpp (the complete XElement type).
    XElement* Element(const XName& name);

    // XContainer.Elements()/Elements(XName): the lazy child-element
    // sequences (see XContainerElements).
    XContainerElements Elements() { return XContainerElements(*this); }
    XContainerElements Elements(const XName& name) { return XContainerElements(*this, name); }

    // XContainer.RemoveNodes: detaches every child (string content is
    // dropped without materializing it).
    void RemoveNodes();

    // XContainer.ReplaceNodes: RemoveNodes followed by Add. (The C# snapshots
    // lazy content first; the port's XContent collections are already
    // materialized, so the snapshot is a no-op.)
    void ReplaceNodes(XContent content);

    // The raw last-child pointer behind the content, or null for string or
    // empty content. C# internal (`content as XNode`).
    XNode* ContentNode() const
    {
        if (std::holds_alternative<XNode*>(content_))
            return std::get<XNode*>(content_);
        return nullptr;
    }

protected:
    XContainer() = default;

    // The C# internal XContainer(XContainer) copy constructor: deep-clones
    // the node content (or copies the string content verbatim). The derived
    // XDocument/XElement copy constructors chain through this.
    explicit XContainer(const XContainer& other);

    // internal virtual content validation, overridden by XDocument and
    // (with the XElement slice) XElement.
    virtual void ValidateNode(const XNode& node, const XNode* previous);
    virtual void ValidateString(const std::string& s);

    // internal virtual attribute attach. The XContainer base is a NO-OP (the
    // C# drops the attribute); XDocument throws; XElement (next slice)
    // attaches.
    virtual void AddAttribute(std::shared_ptr<XAttribute> attribute);
    virtual void AddAttributeSkipNotify(std::shared_ptr<XAttribute> attribute);

    // internal XContainer.AddContentSkipNotify: the content dispatch the
    // public Add routes through (see the SkipNotify note on the class).
    void AddContentSkipNotify(XContent content);

    void AddNodeSkipNotify(std::shared_ptr<XNode> node);
    void AddStringSkipNotify(std::string s);
    void AppendNodeSkipNotify(std::shared_ptr<XNode> node);
    void ConvertTextToNode();

    // internal XContainer.RemoveNode: unlinks the node and drops the
    // container's reference (the node survives while externally held).
    void RemoveNode(XNode* node);

    // internal XContainer.WriteContentTo: serializes the child content (a
    // string content through WriteString, or WriteWhitespace for a document;
    // the node list in document order).
    void WriteContentTo(XmlWriter& writer) const;

    // internal XContainer.AppendText: appends the concatenated text content
    // (string content directly; a node list through each node's AppendText).
    void AppendText(std::string& text) const override;

private:
    // internal XContainer.GetTextOnly: the text content when this container
    // holds nothing but text (string or all-XText children), nullopt
    // otherwise (the null-vs-value distinction ContentsEqual compares).
    std::optional<std::string> GetTextOnly() const;

    // internal XContainer.CollectText: consumes the run of text nodes at the
    // cursor, advancing it past the run (to null at the end of the list).
    std::string CollectText(XNode*& node) const;

public:

    // internal XContainer.ContentsEqual/ContentsHashCode: the deep value
    // comparison over child content.
    bool ContentsEqual(const XContainer& other) const;
    std::int32_t ContentsHashCode() const;

    // The `internal object content` field: no content, a text-only string, or
    // the LAST child of the circular sibling list (see the KEY DESIGN note).
    std::variant<std::monostate, std::string, XNode*> content_;

    // The ownership list of every attached child (the GC stand-in; order is
    // irrelevant -- the sibling links carry it).
    std::vector<std::shared_ptr<XNode>> owned_;

    friend class XNode;
    friend class Inserter;
    friend class XContainerNodes;
};

} // namespace ILSpy::Decompiler::Xml
