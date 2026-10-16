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

#include "Decompiler/Xml/XContainer.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include "Decompiler/Xml/XCData.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <algorithm>
#include <stdexcept>

namespace ILSpy::Decompiler::Xml {

// ---------------------------------------------------------------------------
// Inserter
// ---------------------------------------------------------------------------

Inserter::Inserter(XContainer& parent, XNode* anchor)
    : parent_(parent)
    , previous_(anchor)
{
}

void Inserter::Add(XContent content)
{
    AddContent(content);
    if (!text_)
        return;
    if (std::holds_alternative<std::monostate>(parent_.content_)) {
        // The C# SkipNotify branch (unconditional here, events are not
        // ported): the pending text becomes string content -- no XText node
        // is materialized.
        parent_.content_ = *text_;
        return;
    }
    if (!text_->empty()) {
        XText* xText = dynamic_cast<XText*>(previous_);
        if (xText != nullptr && dynamic_cast<XCData*>(previous_) == nullptr) {
            xText->Value(xText->Value() + *text_);
            return;
        }
        parent_.ConvertTextToNode();
        InsertNode(std::make_shared<XText>(*text_));
    }
}

void Inserter::AddContent(const XContent& content)
{
    if (content.IsNull())
        return;
    if (const std::shared_ptr<XNode>* node = content.GetNode()) {
        AddNode(*node);
        return;
    }
    if (const std::string* s = content.GetString()) {
        AddString(*s);
        return;
    }
    if (const std::vector<XContent>* list = content.GetList()) {
        for (const XContent& item : *list)
            AddContent(item);
        return;
    }
    if (content.GetAttribute() != nullptr)
        throw std::invalid_argument("An attribute cannot be added to content.");
    // The C# XStreamingElement and value.ToString() fallback arms are not
    // modeled in XContent (see XNode.hpp).
}

void Inserter::AddNode(std::shared_ptr<XNode> node)
{
    parent_.ValidateNode(*node, previous_);
    if (node->parent_ != nullptr) {
        node = node->CloneNode();
    } else {
        XNode* root = &parent_;
        while (root->parent_ != nullptr)
            root = root->parent_;
        if (node.get() == root)
            node = node->CloneNode();
    }
    parent_.ConvertTextToNode();
    if (text_) {
        if (!text_->empty()) {
            XText* xText = dynamic_cast<XText*>(previous_);
            if (xText != nullptr && dynamic_cast<XCData*>(previous_) == nullptr) {
                xText->Value(xText->Value() + *text_);
            } else {
                InsertNode(std::make_shared<XText>(*text_));
            }
        }
        text_.reset();
    }
    InsertNode(std::move(node));
}

void Inserter::AddString(std::string s)
{
    parent_.ValidateString(s);
    if (text_)
        *text_ += s;
    else
        text_ = std::move(s);
}

void Inserter::InsertNode(std::shared_ptr<XNode> node)
{
    if (node->parent_ != nullptr)
        throw std::runtime_error("This operation was corrupted by external code.");
    XNode* raw = node.get();
    raw->parent_ = &parent_;
    if (std::holds_alternative<std::monostate>(parent_.content_)
        || std::holds_alternative<std::string>(parent_.content_)) {
        raw->next_ = raw;
        parent_.content_ = raw;
    } else if (previous_ == nullptr) {
        XNode* last = parent_.ContentNode();
        raw->next_ = last->next_;
        last->next_ = raw;
    } else {
        raw->next_ = previous_->next_;
        previous_->next_ = raw;
        if (parent_.ContentNode() == previous_)
            parent_.content_ = raw;
    }
    previous_ = raw;
    parent_.owned_.push_back(std::move(node));
}

// ---------------------------------------------------------------------------
// XContainer
// ---------------------------------------------------------------------------

XContainer::XContainer(const XContainer& other)
    : XNode()
{
    if (const std::string* text = std::get_if<std::string>(&other.content_)) {
        content_ = *text;
        return;
    }
    if (XNode* xNode = other.ContentNode()) {
        do {
            xNode = xNode->next_;
            AppendNodeSkipNotify(xNode->CloneNode());
        } while (xNode != other.ContentNode());
    }
}

XNode* XContainer::FirstNode()
{
    XNode* last = LastNode();
    return last != nullptr ? last->next_ : nullptr;
}

XNode* XContainer::LastNode()
{
    if (std::holds_alternative<std::monostate>(content_))
        return nullptr;
    if (const std::string* text = std::get_if<std::string>(&content_)) {
        if (text->empty())
            return nullptr;
        // The C# getter materializes nonempty string content into an XText
        // node on first read.
        auto xText = std::make_shared<XText>(*text);
        xText->parent_ = this;
        xText->next_ = xText.get();
        owned_.push_back(xText);
        content_ = xText.get();
        return xText.get();
    }
    return std::get<XNode*>(content_);
}

void XContainer::Add(XContent content)
{
    // The C# Add routes through AddContentSkipNotify when no ancestor has a
    // change subscriber; no subscriber is possible in the port (events are
    // not ported), so this is the whole method.
    AddContentSkipNotify(std::move(content));
}

void XContainer::AddFirst(XContent content)
{
    Inserter(*this, nullptr).Add(std::move(content));
}

XContainerNodes XContainer::Nodes()
{
    return XContainerNodes(*this);
}

void XContainer::RemoveNodes()
{
    // RemoveNodesSkipNotify: detach every child without materializing string
    // content.
    if (XNode* xNode = ContentNode()) {
        do {
            XNode* next = xNode->next_;
            xNode->parent_ = nullptr;
            xNode->next_ = nullptr;
            xNode = next;
        } while (xNode != ContentNode());
    }
    content_ = std::monostate {};
    owned_.clear();
}

void XContainer::ReplaceNodes(XContent content)
{
    RemoveNodes();
    Add(std::move(content));
}

void XContainer::ValidateNode(const XNode&, const XNode*)
{
}

void XContainer::ValidateString(const std::string&)
{
}

void XContainer::AddAttribute(std::shared_ptr<XAttribute>)
{
}

void XContainer::AddAttributeSkipNotify(std::shared_ptr<XAttribute>)
{
}

void XContainer::AddContentSkipNotify(XContent content)
{
    if (content.IsNull())
        return;
    if (const std::shared_ptr<XNode>* node = content.GetNode()) {
        AddNodeSkipNotify(*node);
        return;
    }
    if (const std::string* s = content.GetString()) {
        AddStringSkipNotify(*s);
        return;
    }
    if (const std::shared_ptr<XAttribute>* attribute = content.GetAttribute()) {
        AddAttributeSkipNotify(*attribute);
        return;
    }
    if (const std::vector<XContent>* list = content.GetList()) {
        for (const XContent& item : *list)
            AddContentSkipNotify(item);
        return;
    }
}

void XContainer::AddNodeSkipNotify(std::shared_ptr<XNode> node)
{
    ValidateNode(*node, this);
    if (node->parent_ != nullptr) {
        node = node->CloneNode();
    } else {
        XNode* root = this;
        while (root->parent_ != nullptr)
            root = root->parent_;
        if (node.get() == root)
            node = node->CloneNode();
    }
    ConvertTextToNode();
    AppendNodeSkipNotify(std::move(node));
}

void XContainer::AddStringSkipNotify(std::string s)
{
    ValidateString(s);
    if (std::holds_alternative<std::monostate>(content_)) {
        content_ = std::move(s);
        return;
    }
    if (s.empty())
        return;
    if (const std::string* text = std::get_if<std::string>(&content_)) {
        content_ = *text + s;
        return;
    }
    XText* xText = dynamic_cast<XText*>(ContentNode());
    if (xText != nullptr && dynamic_cast<XCData*>(xText) == nullptr) {
        xText->text_ += s;
        return;
    }
    AppendNodeSkipNotify(std::make_shared<XText>(std::move(s)));
}

void XContainer::AppendNodeSkipNotify(std::shared_ptr<XNode> node)
{
    XNode* raw = node.get();
    raw->parent_ = this;
    if (std::holds_alternative<std::monostate>(content_)
        || std::holds_alternative<std::string>(content_)) {
        raw->next_ = raw;
    } else {
        XNode* last = ContentNode();
        raw->next_ = last->next_;
        last->next_ = raw;
    }
    content_ = raw;
    owned_.push_back(std::move(node));
}

void XContainer::ConvertTextToNode()
{
    if (const std::string* text = std::get_if<std::string>(&content_)) {
        if (text->empty())
            return;
        auto xText = std::make_shared<XText>(*text);
        xText->parent_ = this;
        xText->next_ = xText.get();
        owned_.push_back(xText);
        content_ = xText.get();
    }
}

void XContainer::RemoveNode(XNode* node)
{
    if (node->parent_ != this)
        throw std::runtime_error("This operation was corrupted by external code.");
    XNode* xNode = ContentNode();
    while (xNode->next_ != node)
        xNode = xNode->next_;
    if (xNode == node) {
        content_ = std::monostate {};
    } else {
        if (ContentNode() == node)
            content_ = xNode;
        xNode->next_ = node->next_;
    }
    node->parent_ = nullptr;
    node->next_ = nullptr;
    auto owned = std::find_if(
        owned_.begin(), owned_.end(), [node](const std::shared_ptr<XNode>& p) { return p.get() == node; });
    if (owned != owned_.end())
        owned_.erase(owned);
}

void XContainer::AppendText(std::string& text) const
{
    if (const std::string* value = std::get_if<std::string>(&content_)) {
        text += *value;
        return;
    }
    if (XNode* xNode = ContentNode()) {
        do {
            xNode = xNode->next_;
            xNode->AppendText(text);
        } while (xNode != ContentNode());
    }
}

std::optional<std::string> XContainer::GetTextOnly() const
{
    if (std::holds_alternative<std::monostate>(content_))
        return std::nullopt;
    if (const std::string* text = std::get_if<std::string>(&content_))
        return *text;
    std::string text;
    XNode* xNode = ContentNode();
    do {
        xNode = xNode->next_;
        if (xNode->NodeType() != XmlNodeType::Text)
            return std::nullopt;
        text += static_cast<const XText*>(xNode)->Value();
    } while (xNode != ContentNode());
    return text;
}

std::string XContainer::CollectText(XNode*& node) const
{
    std::string text;
    while (node != nullptr && node->NodeType() == XmlNodeType::Text) {
        text += static_cast<const XText*>(node)->Value();
        node = node != ContentNode() ? node->next_ : nullptr;
    }
    return text;
}

bool XContainer::ContentsEqual(const XContainer& other) const
{
    if (content_ == other.content_)
        return true;
    std::optional<std::string> textOnly = GetTextOnly();
    if (textOnly.has_value())
        return *textOnly == other.GetTextOnly();
    XNode* xNode = ContentNode();
    XNode* xNode2 = other.ContentNode();
    if (xNode != nullptr && xNode2 != nullptr) {
        xNode = xNode->next_;
        xNode2 = xNode2->next_;
        while (CollectText(xNode) == other.CollectText(xNode2)) {
            if (xNode == nullptr && xNode2 == nullptr)
                return true;
            if (xNode == nullptr || xNode2 == nullptr || !xNode->DeepEquals(*xNode2))
                break;
            xNode = xNode != ContentNode() ? xNode->next_ : nullptr;
            xNode2 = xNode2 != other.ContentNode() ? xNode2->next_ : nullptr;
        }
    }
    return false;
}

std::int32_t XContainer::ContentsHashCode() const
{
    std::optional<std::string> textOnly = GetTextOnly();
    if (textOnly.has_value()) {
        return static_cast<std::int32_t>(std::hash<std::string> {}(*textOnly));
    }
    std::int32_t num = 0;
    if (XNode* n = ContentNode()) {
        do {
            n = n->next_;
            XNode* position = n;
            std::string text = CollectText(position);
            if (!text.empty())
                num ^= static_cast<std::int32_t>(std::hash<std::string> {}(text));
            if (position == nullptr)
                break;
            n = position;
            num ^= n->GetDeepHashCode();
        } while (n != ContentNode());
    }
    return num;
}

void XContainerNodes::Iterator::Advance()
{
    if (!started_) {
        started_ = true;
        XNode* last = container_->LastNode();
        if (last == nullptr) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = last->next_;
        current_ = position_->shared_from_this();
        return;
    }
    // The C# do-while bottom condition, re-read fresh against the tree (the
    // load-bearing stop rule: removing the yielded node ends the sequence).
    if (position_->parent_ != container_ || position_ == container_->ContentNode()) {
        done_ = true;
        current_.reset();
        return;
    }
    position_ = position_->next_;
    current_ = position_->shared_from_this();
}

void XContainer::WriteContentTo(XmlWriter& writer) const
{
    if (std::holds_alternative<std::monostate>(content_))
        return;
    if (const std::string* text = std::get_if<std::string>(&content_)) {
        if (dynamic_cast<const XDocument*>(this))
            writer.WriteWhitespace(*text);
        else
            writer.WriteString(*text);
        return;
    }
    const XNode* last = std::get<XNode*>(content_);
    const XNode* n = last;
    do {
        n = n->next_;
        n->WriteTo(writer);
    } while (n != last);
}

} // namespace ILSpy::Decompiler::Xml
