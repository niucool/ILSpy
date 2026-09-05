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

#include "Decompiler/Xml/XElement.hpp"

#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace ILSpy::Decompiler::Xml {

// XObject::Parent (`parent as XElement`): defined here with the complete
// XElement type (see XObject.hpp).
XElement* XObject::Parent()
{
    return dynamic_cast<XElement*>(parent_);
}

// ---------------------------------------------------------------------------
// XElement
// ---------------------------------------------------------------------------

XElement::XElement(const XElement& other)
    : XContainer(other)
    , name_(other.name_)
{
    XAttribute* xAttribute = other.lastAttr_;
    if (xAttribute != nullptr) {
        do {
            xAttribute = xAttribute->next_;
            AppendAttributeSkipNotify(std::make_shared<XAttribute>(*xAttribute));
        } while (xAttribute != other.lastAttr_);
    }
}

bool XElement::HasElements() const
{
    XNode* xNode = ContentNode();
    if (xNode != nullptr) {
        do {
            if (dynamic_cast<XElement*>(xNode) != nullptr)
                return true;
            xNode = xNode->next_;
        } while (xNode != ContentNode());
    }
    return false;
}

std::string XElement::Value() const
{
    if (std::holds_alternative<std::monostate>(content_))
        return std::string();
    if (const std::string* text = std::get_if<std::string>(&content_))
        return *text;
    std::string text;
    AppendText(text);
    return text;
}

void XElement::Value(std::string value)
{
    RemoveNodes();
    Add(std::move(value));
}

XAttribute* XElement::Attribute(const XName& name) const
{
    XAttribute* xAttribute = lastAttr_;
    if (xAttribute != nullptr) {
        do {
            xAttribute = xAttribute->next_;
            if (xAttribute->name_ == name)
                return xAttribute;
        } while (xAttribute != lastAttr_);
    }
    return nullptr;
}

void XElement::SetAttributeValue(const XName& name, std::optional<std::string> value)
{
    XAttribute* xAttribute = Attribute(name);
    if (!value.has_value()) {
        if (xAttribute != nullptr)
            RemoveAttribute(xAttribute);
    } else if (xAttribute != nullptr) {
        xAttribute->Value(*value);
    } else {
        AppendAttribute(std::make_shared<XAttribute>(name, *value));
    }
}

void XElement::RemoveAttributes()
{
    // RemoveAttributesSkipNotify (unconditional here, events are not
    // ported): detach every attribute from the list.
    if (lastAttr_ != nullptr) {
        XAttribute* xAttribute = lastAttr_;
        do {
            XAttribute* next = xAttribute->next_;
            xAttribute->parent_ = nullptr;
            xAttribute->next_ = nullptr;
            xAttribute = next;
        } while (xAttribute != lastAttr_);
        lastAttr_ = nullptr;
    }
    ownedAttrs_.clear();
}

XNamespace XElement::GetDefaultNamespace() const
{
    std::optional<std::string> namespaceOfPrefixInScope = GetNamespaceOfPrefixInScope("xmlns", nullptr);
    if (!namespaceOfPrefixInScope.has_value())
        return XNamespace::Get(std::string());
    return XNamespace::Get(*namespaceOfPrefixInScope);
}

std::optional<XNamespace> XElement::GetNamespaceOfPrefix(const std::string& prefix) const
{
    if (prefix.empty())
        throw std::invalid_argument("The value cannot be an empty string. (Parameter 'prefix')");
    if (prefix == "xmlns")
        return XNamespace::Xmlns();
    std::optional<std::string> namespaceOfPrefixInScope = GetNamespaceOfPrefixInScope(prefix, nullptr);
    if (namespaceOfPrefixInScope.has_value())
        return XNamespace::Get(*namespaceOfPrefixInScope);
    if (prefix == "xml")
        return XNamespace::Xml();
    return std::nullopt;
}

std::optional<std::string> XElement::GetPrefixOfNamespace(const XNamespace& ns) const
{
    const std::string& namespaceName = ns.NamespaceName();
    bool sawXmlnsDeclaration = false;
    const XElement* xElement = this;
    do {
        XAttribute* xAttribute = xElement->lastAttr_;
        if (xAttribute != nullptr) {
            bool elementDeclaresXmlns = false;
            do {
                xAttribute = xAttribute->next_;
                if (xAttribute->IsNamespaceDeclaration()) {
                    if (xAttribute->Value() == namespaceName
                        && xAttribute->Name().NamespaceName().length() != 0
                        && (!sawXmlnsDeclaration
                            || xElement->GetNamespaceOfPrefixInScope(xAttribute->Name().LocalName(), xElement)
                                == std::nullopt)) {
                        return xAttribute->Name().LocalName();
                    }
                    elementDeclaresXmlns = true;
                }
            } while (xAttribute != xElement->lastAttr_);
            sawXmlnsDeclaration |= elementDeclaresXmlns;
        }
        xElement = xElement->parent_ != nullptr ? dynamic_cast<XElement*>(xElement->parent_) : nullptr;
    } while (xElement != nullptr);
    if (namespaceName == "http://www.w3.org/XML/1998/namespace") {
        if (!sawXmlnsDeclaration || GetNamespaceOfPrefixInScope("xml", nullptr) == std::nullopt)
            return std::string("xml");
    } else if (namespaceName == "http://www.w3.org/2000/xmlns/") {
        return std::string("xmlns");
    }
    return std::nullopt;
}

void XElement::RemoveAttribute(XAttribute* attribute)
{
    if (attribute->parent_ != this)
        throw std::runtime_error("This operation was corrupted by external code.");
    XAttribute* xAttribute = lastAttr_;
    while (xAttribute->next_ != attribute)
        xAttribute = xAttribute->next_;
    if (xAttribute == attribute) {
        lastAttr_ = nullptr;
    } else {
        if (lastAttr_ == attribute)
            lastAttr_ = xAttribute;
        xAttribute->next_ = attribute->next_;
    }
    attribute->parent_ = nullptr;
    attribute->next_ = nullptr;
    auto owned = std::find_if(ownedAttrs_.begin(), ownedAttrs_.end(),
        [attribute](const std::shared_ptr<XAttribute>& p) { return p.get() == attribute; });
    if (owned != ownedAttrs_.end())
        ownedAttrs_.erase(owned);
}

void XElement::AddAttribute(std::shared_ptr<XAttribute> attribute)
{
    if (Attribute(attribute->Name()) != nullptr)
        throw std::runtime_error("Duplicate attribute.");
    if (attribute->parent_ != nullptr)
        attribute = std::make_shared<XAttribute>(*attribute);
    AppendAttribute(std::move(attribute));
}

void XElement::AddAttributeSkipNotify(std::shared_ptr<XAttribute> attribute)
{
    if (Attribute(attribute->Name()) != nullptr)
        throw std::runtime_error("Duplicate attribute.");
    if (attribute->parent_ != nullptr)
        attribute = std::make_shared<XAttribute>(*attribute);
    AppendAttributeSkipNotify(std::move(attribute));
}

void XElement::ValidateNode(const XNode& node, const XNode*)
{
    if (dynamic_cast<const XDocument*>(&node) != nullptr)
        throw std::invalid_argument("A node of type Document cannot be added to content.");
    if (dynamic_cast<const XDocumentType*>(&node) != nullptr)
        throw std::invalid_argument("A node of type DocumentType cannot be added to content.");
}

void XElement::AppendAttribute(std::shared_ptr<XAttribute> attribute)
{
    if (attribute->parent_ != nullptr)
        throw std::runtime_error("This operation was corrupted by external code.");
    AppendAttributeSkipNotify(std::move(attribute));
}

void XElement::AppendAttributeSkipNotify(std::shared_ptr<XAttribute> attribute)
{
    XAttribute* raw = attribute.get();
    raw->parent_ = this;
    if (lastAttr_ == nullptr) {
        raw->next_ = raw;
    } else {
        raw->next_ = lastAttr_->next_;
        lastAttr_->next_ = raw;
    }
    lastAttr_ = raw;
    ownedAttrs_.push_back(std::move(attribute));
}

bool XElement::AttributesEqual(const XElement& other) const
{
    XAttribute* xAttribute = lastAttr_;
    XAttribute* xAttribute2 = other.lastAttr_;
    if (xAttribute != nullptr && xAttribute2 != nullptr) {
        do {
            xAttribute = xAttribute->next_;
            xAttribute2 = xAttribute2->next_;
            if (xAttribute->name_ != xAttribute2->name_ || xAttribute->value_ != xAttribute2->value_)
                return false;
        } while (xAttribute != lastAttr_);
        return xAttribute2 == other.lastAttr_;
    }
    if (xAttribute == nullptr)
        return xAttribute2 == nullptr;
    return false;
}

std::optional<std::string> XElement::GetNamespaceOfPrefixInScope(
    const std::string& prefix, const XElement* outOfScope) const
{
    for (const XElement* xElement = this; xElement != outOfScope;
         xElement = xElement->parent_ != nullptr ? dynamic_cast<XElement*>(xElement->parent_) : nullptr) {
        XAttribute* xAttribute = xElement->lastAttr_;
        if (xAttribute != nullptr) {
            do {
                xAttribute = xAttribute->next_;
                if (xAttribute->IsNamespaceDeclaration() && xAttribute->Name().LocalName() == prefix)
                    return xAttribute->Value();
            } while (xAttribute != xElement->lastAttr_);
        }
    }
    return std::nullopt;
}

std::shared_ptr<XNode> XElement::CloneNode() const
{
    return std::make_shared<XElement>(*this);
}

bool XElement::DeepEquals(const XNode& node) const
{
    if (const XElement* xElement = dynamic_cast<const XElement*>(&node);
        xElement != nullptr && name_ == xElement->name_ && ContentsEqual(*xElement)) {
        return AttributesEqual(*xElement);
    }
    return false;
}

std::int32_t XElement::GetDeepHashCode() const
{
    std::int32_t hashCode = static_cast<std::int32_t>(std::hash<XName> {}(name_));
    hashCode ^= ContentsHashCode();
    XAttribute* xAttribute = lastAttr_;
    if (xAttribute != nullptr) {
        do {
            xAttribute = xAttribute->next_;
            hashCode ^= xAttribute->GetDeepHashCode();
        } while (xAttribute != lastAttr_);
    }
    return hashCode;
}

// ---------------------------------------------------------------------------
// XElementAttributes (the lazy attribute sequence)
// ---------------------------------------------------------------------------

void XElementAttributes::Iterator::Advance()
{
    // The C# GetAttributes iterator: a = a.next; yield-if-match; the bottom
    // condition (a.parent == this && a != lastAttr) re-read fresh against
    // the tree -- removing the yielded attribute ends the sequence.
    if (!started_) {
        started_ = true;
        if (element_->lastAttr_ == nullptr) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = element_->lastAttr_->next_;
    } else {
        if (position_->parent_ != element_ || position_ == element_->lastAttr_) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = position_->next_;
    }
    while (true) {
        if (name_ == nullptr || position_->name_ == *name_) {
            current_ = position_->shared_from_this();
            return;
        }
        if (position_->parent_ != element_ || position_ == element_->lastAttr_) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = position_->next_;
    }
}

// ---------------------------------------------------------------------------
// XContainer::Element/Elements (declared in XContainer.hpp, defined here
// with the complete XElement type)
// ---------------------------------------------------------------------------

XElement* XContainer::Element(const XName& name)
{
    XNode* xNode = ContentNode();
    if (xNode != nullptr) {
        do {
            xNode = xNode->next_;
            if (XElement* xElement = dynamic_cast<XElement*>(xNode);
                xElement != nullptr && xElement->name_ == name)
                return xElement;
        } while (xNode != ContentNode());
    }
    return nullptr;
}

void XContainerElements::Iterator::Advance()
{
    // The C# GetElements iterator: n = n.next; yield-if-element-and-match;
    // the bottom condition (n.parent == this && n != content) re-read fresh
    // against the tree -- removing the yielded element ends the sequence.
    if (!started_) {
        started_ = true;
        XNode* content = container_->ContentNode();
        if (content == nullptr) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = content->next_;
    } else {
        if (position_->parent_ != container_ || position_ == container_->ContentNode()) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = position_->next_;
    }
    while (true) {
        if (XElement* xElement = dynamic_cast<XElement*>(position_)) {
            if (name_ == nullptr || xElement->name_ == *name_) {
                current_ = std::static_pointer_cast<XElement>(position_->shared_from_this());
                return;
            }
        }
        if (position_->parent_ != container_ || position_ == container_->ContentNode()) {
            done_ = true;
            current_.reset();
            return;
        }
        position_ = position_->next_;
    }
}

} // namespace ILSpy::Decompiler::Xml
