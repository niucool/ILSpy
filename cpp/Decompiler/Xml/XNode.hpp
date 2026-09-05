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

// Port-authored stand-in for System.Xml.Linq.XNode (the abstract node base of
// the XLinq DOM): the circular sibling link, the sibling navigation, the
// insert-before/after-self engine (Inserter, with XContainer), Remove/
// ReplaceWith, the lazy sibling sequences, and the DeepEquals machinery.
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs); the C# semantics were taken
// from the decompiled System.Private.Xml.Linq sources (XNode.cs, Inserter).
//
// KEY DESIGN (the `object? content` parameter crux): the C# Add/AddAfterSelf/
// AddBeforeSelf/ReplaceWith overloads all take `object? content` and dispatch
// on the runtime type (null / XNode / string / XAttribute / XStreamingElement /
// object[] / IEnumerable / value.ToString()). C++ has no boxed object, so the
// port models the parameter as XContent -- a variant over exactly the
// alternatives the dispatch distinguishes: null, string, XNode, XAttribute,
// and a materialized collection. The C# params-object[] form is a braced
// initializer list of XContent items (`doc.Add({node, "text", attr})`).
// DEFERRED: the value.ToString() fallback (int/double/bool content, e.g.
// `doc.Add(42)` renders "42" through XmlConvert) and XStreamingElement
// content -- no BamlDecompiler call site passes either (they pass elements,
// attributes and strings), and XContent carries no alternative for them.
//
// KEY DESIGN (ownership): the C# DOM is garbage-collected -- a node survives
// Remove() while anything references it, and containers hold their children.
// The port models this with std::shared_ptr: nodes are created via
// std::make_shared and adopted by the attaching container (see XContainer);
// a detached node stays alive exactly while a shared_ptr still holds it. XNode
// derives std::enable_shared_from_this so the lazy sequences can keep the
// current node rooted while the consumer body runs (the C# iterator GC-roots
// the current element the same way). Nodes must therefore be heap-constructed
// through make_shared; a stack-constructed node cannot be attached (the C#
// would adopt anything -- the port's documented divergence).
//
// The lazy sibling sequences reproduce the C# iterator state machines
// EXACTLY, including the mutation-during-iteration semantics the BamlDecompiler
// depends on: AttributeRewritePass removes the current node inside
// `foreach (var child in elem.Elements())`, and the C# iterator's bottom
// condition (`n.parent == this && n != content`) then STOPS the iteration after
// the removed node -- the outer do-while re-runs the pass over the remaining
// children. An eager materialization would process them all in one pass and
// change the decompiled output.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <variant>
#include <vector>

#include "XObject.hpp"
#include "XmlNodeType.hpp"
#include "XmlWriter.hpp"

namespace ILSpy::Decompiler::Xml {

class XAttribute;
class XContainer;
class XNodesAfterSelf;
class XNodesBeforeSelf;
class XText;

// The C# `object? content` parameter of the DOM insertion methods (see the
// KEY DESIGN note on XNode).
class XContent {
public:
    XContent() = default;
    XContent(std::nullptr_t) {}
    XContent(std::string value)
        : value_(std::move(value))
    {
    }
    XContent(const char* value)
        : value_(std::string(value))
    {
    }
    XContent(std::shared_ptr<XNode> node)
        : value_(std::move(node))
    {
    }
    // The shared_ptr<Derived> form (a single implicit conversion -- C++ would
    // otherwise need two user-defined conversions in a row from
    // shared_ptr<XComment> through shared_ptr<XNode> to XContent).
    template <typename TNode, typename = std::enable_if_t<std::is_convertible_v<TNode*, XNode*>>>
    XContent(std::shared_ptr<TNode> node)
        : value_(std::move(node))
    {
    }
    XContent(std::shared_ptr<XAttribute> attribute)
        : value_(std::move(attribute))
    {
    }
    XContent(std::vector<XContent> items)
        : value_(std::move(items))
    {
    }
    // The params-object[] form: `doc.Add({node, "text"})`.
    XContent(std::initializer_list<XContent> items)
        : value_(std::vector<XContent>(items))
    {
    }

    bool IsNull() const { return std::holds_alternative<std::monostate>(value_); }

    const std::string* GetString() const
    {
        return std::get_if<std::string>(&value_);
    }
    const std::shared_ptr<XNode>* GetNode() const
    {
        return std::get_if<std::shared_ptr<XNode>>(&value_);
    }
    const std::shared_ptr<XAttribute>* GetAttribute() const
    {
        return std::get_if<std::shared_ptr<XAttribute>>(&value_);
    }
    const std::vector<XContent>* GetList() const
    {
        return std::get_if<std::vector<XContent>>(&value_);
    }

private:
    std::variant<std::monostate, std::string, std::shared_ptr<XNode>, std::shared_ptr<XAttribute>,
        std::vector<XContent>> value_;
};

class XNode : public XObject, public std::enable_shared_from_this<XNode> {
public:
    // The C# XNode/XObject copy operations do not exist (clone is explicit
    // through CloneNode); suppress them so a DOM node is never sliced.
    XNode(const XNode&) = delete;
    XNode& operator=(const XNode&) = delete;

    // XNode.NextNode: the next sibling, or null for the last child of its
    // parent (or an unparented node).
    XNode* NextNode() const;

    // XNode.PreviousNode: the previous sibling (a walk over the circular
    // list), or null for the first child / unparented node.
    XNode* PreviousNode() const;

    // XNode.AddAfterSelf/AddBeforeSelf: insert content immediately after /
    // before this node (Inserter-based; throws when unparented).
    void AddAfterSelf(XContent content);
    void AddBeforeSelf(XContent content);

    // XNode.Remove: detach this node from its parent (the node survives
    // while a shared_ptr still holds it; throws when unparented).
    void Remove();

    // XNode.ReplaceWith: detach this node, then insert the content where it
    // was.
    void ReplaceWith(XContent content);

    // XNode.IsAfter/IsBefore: document-order comparisons against another
    // node (null compares as before everything).
    bool IsAfter(const XNode* node) const;
    bool IsBefore(const XNode* node) const;

    // XNode.CompareDocumentOrder: -1/0/1 for the relative document order of
    // two nodes; null orders before any node; throws when the nodes do not
    // share a common ancestor.
    static std::int32_t CompareDocumentOrder(const XNode* n1, const XNode* n2);

    // XNode.DeepEquals (public static): value equality over two nodes and
    // their descendants.
    static bool DeepEquals(const XNode* n1, const XNode* n2);

    // The lazy sibling sequences (see the KEY DESIGN note: the C# iterator
    // state machines, not materializations).
    XNodesAfterSelf NodesAfterSelf() const;
    XNodesBeforeSelf NodesBeforeSelf() const;

    // XNode.WriteTo: serialize this node to the writer (abstract; the leaf
    // overrides live on the node classes, XElement's on the ElementWriter).
    virtual void WriteTo(XmlWriter& writer) const = 0;

    // XNode.ToString(): the XML for this node, honoring a SaveOptions
    // annotation (the walk up the parent chain). XNode.ToString(options)
    // renders with the given options regardless of annotations.
    std::string ToString() const;
    std::string ToString(SaveOptions options) const;

    // internal XNode.GetXmlWriterSettings: the settings the Save(TextWriter /
    // file) overloads build from the save options.
    static XmlWriterSettings GetXmlWriterSettings(SaveOptions o);

private:
    // internal XNode.GetXmlString: the StringWriter-based render behind
    // ToString (OmitXmlDeclaration, Indent unless DisableFormatting, fragment
    // conformance for a text node).
    std::string GetXmlString(SaveOptions o) const;

public:
    // internal XNode.CloneNode(): a deep copy of this node, detached (the
    // Add paths clone already-parented nodes through this). Exposed public
    // for tests, the port's standing internal-surface convention.
    virtual std::shared_ptr<XNode> CloneNode() const = 0;

    // internal XNode.DeepEquals(XNode): the per-type value comparison behind
    // the public static DeepEquals.
    virtual bool DeepEquals(const XNode& other) const = 0;

    // internal XNode.AppendText: appends this node's text content to the
    // builder (no-op for everything but text and containers).
    virtual void AppendText(std::string& text) const {}

    // internal XNode.GetDeepHashCode: the per-type hash of this node's value
    // (the C# string hash is randomized in .NET; the port's values differ
    // from the runtime's -- only equality is meaningful, never the number).
    virtual std::int32_t GetDeepHashCode() const = 0;

    // The circular sibling list link. A container's content_ points at its
    // LAST child; content_->next_ is the first child, and the last child's
    // next_ wraps back to the first. Attached nodes are held by their
    // container's ownership list; this pointer carries order only.
    // C# internal: public here for the same access-rule reason as
    // XObject::parent_ (the container machinery and the sequence iterators
    // read it through XNode*).
    XNode* next_ = nullptr;

protected:
    XNode() = default;
};

// The lazy `NodesAfterSelf()` sequence: the siblings after this node, in
// document order (the C# iterator's stop conditions re-checked against the
// tree on every step, so a removal of the current node ends the sequence).
class XNodesAfterSelf final {
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
        friend class XNodesAfterSelf;
        explicit Iterator(const XNode* self)
            : self_(self)
            , done_(false)
        {
            Advance();
        }

        void Advance();

        const XNode* self_ = nullptr;
        XNode* position_ = nullptr;
        std::shared_ptr<XNode> current_;
        bool done_ = true;
    };

    explicit XNodesAfterSelf(const XNode& self)
        : self_(&self)
    {
    }

    Iterator begin() const { return Iterator(self_); }
    Iterator end() const { return Iterator(); }

private:
    const XNode* self_;
};

// The lazy `NodesBeforeSelf()` sequence: the siblings before this node, in
// document order.
class XNodesBeforeSelf final {
public:
    class Iterator final {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::shared_ptr<XNode>;
        using difference_type = std::ptrdiff_t;
        using pointer = const std::shared_ptr<XNode>*;
        using reference = const std::shared_ptr<XNode>&;

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
        friend class XNodesBeforeSelf;
        explicit Iterator(const XNode* self)
            : self_(self)
            , done_(false)
        {
            Advance();
        }

        void Advance();

        const XNode* self_ = nullptr;
        XNode* position_ = nullptr;
        std::shared_ptr<XNode> current_;
        bool started_ = false;
        bool done_ = true;
    };

    explicit XNodesBeforeSelf(const XNode& self)
        : self_(&self)
    {
    }

    Iterator begin() const { return Iterator(self_); }
    Iterator end() const { return Iterator(); }

private:
    const XNode* self_;
};

} // namespace ILSpy::Decompiler::Xml
