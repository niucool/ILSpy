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

#include "Decompiler/Xml/XAttribute.hpp"

#include "Decompiler/Xml/XElement.hpp"

#include <functional>
#include <stdexcept>

namespace ILSpy::Decompiler::Xml {

XAttribute::XAttribute(XName name, std::string value)
    : name_(std::move(name))
    , value_(std::move(value))
{
    ValidateAttribute(name_, value_);
}

XAttribute::XAttribute(const XAttribute& other)
    : name_(other.name_)
    , value_(other.value_)
{
}

void XAttribute::Value(std::string value)
{
    ValidateAttribute(name_, value);
    value_ = std::move(value);
}

XAttribute* XAttribute::NextAttribute() const
{
    if (parent_ == nullptr)
        return nullptr;
    auto* owner = static_cast<XElement*>(parent_);
    if (owner->lastAttr_ == this)
        return nullptr;
    return next_;
}

XAttribute* XAttribute::PreviousAttribute() const
{
    if (parent_ == nullptr)
        return nullptr;
    auto* owner = static_cast<XElement*>(parent_);
    XAttribute* lastAttr = owner->lastAttr_;
    while (lastAttr->next_ != this)
        lastAttr = lastAttr->next_;
    if (lastAttr == owner->lastAttr_)
        return nullptr;
    return lastAttr;
}

bool XAttribute::IsNamespaceDeclaration() const
{
    const std::string& namespaceName = name_.NamespaceName();
    if (namespaceName.empty())
        return name_.LocalName() == "xmlns";
    return namespaceName == "http://www.w3.org/2000/xmlns/";
}

void XAttribute::Remove()
{
    if (parent_ == nullptr)
        throw std::runtime_error("The parent is missing.");
    static_cast<XElement*>(parent_)->RemoveAttribute(this);
}

std::int32_t XAttribute::GetDeepHashCode() const
{
    return static_cast<std::int32_t>(std::hash<XName> {}(name_) ^ std::hash<std::string> {}(value_));
}

std::optional<std::string> XAttribute::GetPrefixOfNamespace(const XNamespace& ns) const
{
    const std::string& namespaceName = ns.NamespaceName();
    if (namespaceName.empty())
        return std::string();
    if (parent_ != nullptr)
        return static_cast<XElement*>(parent_)->GetPrefixOfNamespace(ns);
    if (namespaceName == "http://www.w3.org/XML/1998/namespace")
        return std::string("xml");
    if (namespaceName == "http://www.w3.org/2000/xmlns/")
        return std::string("xmlns");
    return std::nullopt;
}

void XAttribute::ValidateAttribute(const XName& name, const std::string& value)
{
    const std::string& namespaceName = name.NamespaceName();
    if (namespaceName == "http://www.w3.org/2000/xmlns/") {
        if (value.empty())
            throw std::invalid_argument(
                "The prefix '" + name.LocalName() + "' cannot be bound to the empty namespace name.");
        if (value == "http://www.w3.org/XML/1998/namespace") {
            if (name.LocalName() != "xml")
                throw std::invalid_argument(
                    "The prefix 'xml' is bound to the namespace name "
                    "'http://www.w3.org/XML/1998/namespace'. Other prefixes must not be bound to "
                    "this namespace name, and it must not be declared as the default namespace.");
            return;
        }
        if (value == "http://www.w3.org/2000/xmlns/")
            throw std::invalid_argument(
                "The prefix 'xmlns' is bound to the namespace name "
                "'http://www.w3.org/2000/xmlns/'. It must not be declared. Other prefixes must "
                "not be bound to this namespace name, and it must not be declared as the default "
                "namespace.");
        const std::string& localName = name.LocalName();
        if (localName == "xml")
            throw std::invalid_argument(
                "The prefix 'xml' is bound to the namespace name "
                "'http://www.w3.org/XML/1998/namespace'. Other prefixes must not be bound to "
                "this namespace name, and it must not be declared as the default namespace.");
        if (localName == "xmlns")
            throw std::invalid_argument(
                "The prefix 'xmlns' is bound to the namespace name "
                "'http://www.w3.org/2000/xmlns/'. It must not be declared. Other prefixes must "
                "not be bound to this namespace name, and it must not be declared as the default "
                "namespace.");
    } else if (namespaceName.empty() && name.LocalName() == "xmlns") {
        if (value == "http://www.w3.org/XML/1998/namespace")
            throw std::invalid_argument(
                "The prefix 'xml' is bound to the namespace name "
                "'http://www.w3.org/XML/1998/namespace'. Other prefixes must not be bound to "
                "this namespace name, and it must not be declared as the default namespace.");
        if (value == "http://www.w3.org/2000/xmlns/")
            throw std::invalid_argument(
                "The prefix 'xmlns' is bound to the namespace name "
                "'http://www.w3.org/2000/xmlns/'. It must not be declared. Other prefixes must "
                "not be bound to this namespace name, and it must not be declared as the default "
                "namespace.");
    }
}

} // namespace ILSpy::Decompiler::Xml
