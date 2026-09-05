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

#include "Decompiler/Xml/XNode.hpp"

#include "Decompiler/Xml/XContainer.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::Xml {

XNode* XNode::NextNode() const
{
    if (parent_ != nullptr && this != parent_->ContentNode())
        return next_;
    return nullptr;
}

XNode* XNode::PreviousNode() const
{
    if (parent_ == nullptr)
        return nullptr;
    XNode* xNode = parent_->ContentNode()->next_;
    XNode* result = nullptr;
    while (xNode != this) {
        result = xNode;
        xNode = xNode->next_;
    }
    return result;
}

void XNode::AddAfterSelf(XContent content)
{
    if (parent_ == nullptr)
        throw std::runtime_error("The parent is missing.");
    Inserter(*parent_, this).Add(std::move(content));
}

void XNode::AddBeforeSelf(XContent content)
{
    if (parent_ == nullptr)
        throw std::runtime_error("The parent is missing.");
    XNode* xNode = parent_->ContentNode();
    // An attached `this` implies node content (any node attach materializes
    // string content first), so the walk below cannot see a null anchor.
    while (xNode->next_ != this)
        xNode = xNode->next_;
    if (xNode == parent_->ContentNode())
        xNode = nullptr;
    Inserter(*parent_, xNode).Add(std::move(content));
}

void XNode::Remove()
{
    if (parent_ == nullptr)
        throw std::runtime_error("The parent is missing.");
    parent_->RemoveNode(this);
}

void XNode::ReplaceWith(XContent content)
{
    if (parent_ == nullptr)
        throw std::runtime_error("The parent is missing.");
    XContainer* xContainer = parent_;
    XNode* xNode = xContainer->ContentNode();
    while (xNode->next_ != this)
        xNode = xNode->next_;
    if (xNode == xContainer->ContentNode())
        xNode = nullptr;
    xContainer->RemoveNode(this);
    if (xNode != nullptr && xNode->parent_ != xContainer)
        throw std::runtime_error("This operation was corrupted by external code.");
    Inserter(*xContainer, xNode).Add(std::move(content));
}

bool XNode::IsAfter(const XNode* node) const
{
    return CompareDocumentOrder(this, node) > 0;
}

bool XNode::IsBefore(const XNode* node) const
{
    return CompareDocumentOrder(this, node) < 0;
}

std::int32_t XNode::CompareDocumentOrder(const XNode* n1, const XNode* n2)
{
    if (n1 == n2)
        return 0;
    if (n1 == nullptr)
        return -1;
    if (n2 == nullptr)
        return 1;
    const XNode* a = n1;
    const XNode* b = n2;
    if (a->parent_ != b->parent_) {
        std::int32_t num = 0;
        const XNode* xNode = a;
        while (xNode->parent_ != nullptr) {
            xNode = xNode->parent_;
            num++;
        }
        const XNode* xNode2 = b;
        while (xNode2->parent_ != nullptr) {
            xNode2 = xNode2->parent_;
            num--;
        }
        if (xNode != xNode2)
            throw std::runtime_error("A common ancestor is missing.");
        if (num < 0) {
            do {
                b = b->parent_;
                num++;
            } while (num != 0);
            if (a == b)
                return -1;
        } else if (num > 0) {
            do {
                a = a->parent_;
                num--;
            } while (num != 0);
            if (a == b)
                return 1;
        }
        while (a->parent_ != b->parent_) {
            a = a->parent_;
            b = b->parent_;
        }
    } else if (a->parent_ == nullptr) {
        throw std::runtime_error("A common ancestor is missing.");
    }
    XNode* xNode3 = a->parent_->ContentNode();
    do {
        xNode3 = xNode3->next_;
        if (xNode3 == a)
            return -1;
    } while (xNode3 != b);
    return 1;
}

bool XNode::DeepEquals(const XNode* n1, const XNode* n2)
{
    if (n1 == n2)
        return true;
    if (n1 == nullptr || n2 == nullptr)
        return false;
    return n1->DeepEquals(*n2);
}

XNodesAfterSelf XNode::NodesAfterSelf() const
{
    return XNodesAfterSelf(*const_cast<XNode*>(this));
}

XNodesBeforeSelf XNode::NodesBeforeSelf() const
{
    return XNodesBeforeSelf(*const_cast<XNode*>(this));
}

void XNodesAfterSelf::Iterator::Advance()
{
    XNode* check = position_ != nullptr ? position_ : const_cast<XNode*>(self_);
    if (check->parent_ == nullptr || check == check->parent_->ContentNode()) {
        done_ = true;
        current_.reset();
        return;
    }
    position_ = check->next_;
    // Root the yielded node across the consumer body (the C# iterator
    // GC-roots the current element the same way).
    current_ = position_->shared_from_this();
}

void XNodesBeforeSelf::Iterator::Advance()
{
    if (!started_) {
        started_ = true;
        if (self_->parent_ == nullptr) {
            done_ = true;
            current_.reset();
            return;
        }
        XNode* n = self_->parent_->ContentNode();
        // An attached `this` implies node content (see AddBeforeSelf).
        if (n == nullptr) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = n->next_;
        if (position_ == self_) {
            done_ = true;
            current_.reset();
            return;
        }
        current_ = position_->shared_from_this();
        return;
    }
    // The C# do-while bottom condition, then the body.
    if (self_->parent_ == nullptr || self_->parent_ != position_->parent_) {
        done_ = true;
        current_.reset();
        return;
    }
    position_ = position_->next_;
    if (position_ == self_) {
        done_ = true;
        current_.reset();
        return;
    }
    current_ = position_->shared_from_this();
}

// --- the serialization surface (XNode.ToString / GetXmlString) ---

SaveOptions XObject::GetSaveOptionsFromAnnotations() const
{
    // The C# walks up the parent chain: an object with no annotations is
    // skipped, the first SaveOptions annotation wins.
    const XObject* x = this;
    while (true) {
        if (x != nullptr && !x->HasAnnotations()) {
            x = x->parent_;
            continue;
        }
        if (x == nullptr)
            return SaveOptions::None;
        if (const SaveOptions* options = x->Annotation<SaveOptions>())
            return *options;
        x = x->parent_;
    }
}

XmlWriterSettings XNode::GetXmlWriterSettings(SaveOptions o)
{
    XmlWriterSettings settings;
    if ((static_cast<std::uint32_t>(o) & static_cast<std::uint32_t>(SaveOptions::DisableFormatting)) == 0)
        settings.Indent = true;
    if ((static_cast<std::uint32_t>(o) & static_cast<std::uint32_t>(SaveOptions::OmitDuplicateNamespaces))
        != static_cast<std::uint32_t>(SaveOptions::None))
        settings.NamespaceHandling = static_cast<NamespaceHandling>(
            static_cast<std::uint32_t>(settings.NamespaceHandling)
            | static_cast<std::uint32_t>(NamespaceHandling::OmitDuplicates));
    return settings;
}

std::string XNode::ToString() const
{
    return GetXmlString(GetSaveOptionsFromAnnotations());
}

std::string XNode::ToString(SaveOptions options) const
{
    return GetXmlString(options);
}

std::string XNode::GetXmlString(SaveOptions o) const
{
    XmlWriterSettings settings;
    settings.OmitXmlDeclaration = true;
    if ((static_cast<std::uint32_t>(o) & static_cast<std::uint32_t>(SaveOptions::DisableFormatting)) == 0)
        settings.Indent = true;
    if ((static_cast<std::uint32_t>(o) & static_cast<std::uint32_t>(SaveOptions::OmitDuplicateNamespaces))
        != static_cast<std::uint32_t>(SaveOptions::None))
        settings.NamespaceHandling = static_cast<NamespaceHandling>(
            static_cast<std::uint32_t>(settings.NamespaceHandling)
            | static_cast<std::uint32_t>(NamespaceHandling::OmitDuplicates));
    if (dynamic_cast<const XText*>(this))
        settings.ConformanceLevel = ConformanceLevel::Fragment;
    XmlWriter writer(std::move(settings), XmlWriterSink::Text);
    if (const XDocument* document = dynamic_cast<const XDocument*>(this))
        document->WriteContentTo(writer);
    else
        WriteTo(writer);
    writer.Close();
    return writer.OutputUtf8();
}

} // namespace ILSpy::Decompiler::Xml
