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

// The ElementWriter implementation (see ElementWriter.hpp).

#include "ElementWriter.hpp"

namespace ILSpy::Decompiler::Xml {

// --- ElementNamespaceResolver ---

void ElementNamespaceResolver::PopScope()
{
    std::vector<Entry> kept;
    kept.reserve(entries_.size());
    for (const Entry& entry : entries_) {
        if (entry.scope != scope_)
            kept.push_back(entry);
    }
    entries_ = std::move(kept);
    --scope_;
}

void ElementNamespaceResolver::Add(const std::string& prefix, const std::string& ns)
{
    entries_.insert(entries_.begin(), Entry {prefix, ns, scope_});
}

void ElementNamespaceResolver::AddFirst(const std::string& prefix, const std::string& ns)
{
    entries_.push_back(Entry {prefix, ns, scope_});
}

std::optional<std::string> ElementNamespaceResolver::GetPrefixOfNamespace(const std::string& ns,
    bool allowDefaultNamespace) const
{
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!(entries_[i].ns == ns))
            continue;
        // The shadow check: a more recent re-binding of the same prefix
        // hides this entry.
        bool shadowed = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (entries_[j].prefix == entries_[i].prefix) {
                shadowed = true;
                break;
            }
        }
        if (shadowed)
            continue;
        if (allowDefaultNamespace)
            return entries_[i].prefix;
        if (!entries_[i].prefix.empty())
            return entries_[i].prefix;
        // The default prefix is not usable: keep scanning for an older
        // prefixed binding of the same namespace.
    }
    return std::nullopt;
}

// --- ElementWriter ---

std::optional<std::string> ElementWriter::GetPrefixOfNamespace(const std::string& namespaceName,
    bool allowDefaultNamespace) const
{
    if (namespaceName.empty())
        return std::string();
    std::optional<std::string> prefix = resolver_.GetPrefixOfNamespace(namespaceName, allowDefaultNamespace);
    if (prefix)
        return prefix;
    if (namespaceName == "http://www.w3.org/XML/1998/namespace")
        return std::string("xml");
    if (namespaceName == "http://www.w3.org/2000/xmlns/")
        return std::string("xmlns");
    return std::nullopt;
}

void ElementWriter::PushAncestors(const XElement& e)
{
    const XElement* ancestor = nullptr;
    const XContainer* parent = e.parent_;
    while (true) {
        ancestor = dynamic_cast<const XElement*>(parent);
        if (ancestor == nullptr)
            break;
        const XAttribute* a = ancestor->lastAttr_;
        if (a != nullptr) {
            do {
                a = a->next_;
                if (a->IsNamespaceDeclaration()) {
                    resolver_.AddFirst(
                        a->Name().NamespaceName().empty() ? std::string() : a->Name().LocalName(),
                        a->Value());
                }
            } while (a != ancestor->lastAttr_);
        }
        parent = ancestor->parent_;
    }
}

void ElementWriter::PushElement(const XElement& e)
{
    resolver_.PushScope();
    const XAttribute* a = e.lastAttr_;
    if (a == nullptr)
        return;
    do {
        a = a->next_;
        if (a->IsNamespaceDeclaration()) {
            resolver_.Add(
                a->Name().NamespaceName().empty() ? std::string() : a->Name().LocalName(),
                a->Value());
        }
    } while (a != e.lastAttr_);
}

void ElementWriter::WriteStartElement(const XElement& e)
{
    PushElement(e);
    const std::string& ns = e.Name().NamespaceName();
    std::optional<std::string> prefix = GetPrefixOfNamespace(ns, true);
    writer_.WriteStartElement(prefix ? prefix->c_str() : nullptr, e.Name().LocalName(), ns.c_str());
    const XAttribute* a = e.lastAttr_;
    if (a != nullptr) {
        do {
            a = a->next_;
            const std::string& attrNs = a->Name().NamespaceName();
            const std::string& localName = a->Name().LocalName();
            std::optional<std::string> attrPrefix = GetPrefixOfNamespace(attrNs, false);
            // The xmlns attribute of a declaration carries the empty namespace
            // name in its XName; the writer recognizes the declaration by the
            // xmlns namespace URI.
            std::string nsArg = attrNs;
            if (nsArg.empty() && localName == "xmlns")
                nsArg = "http://www.w3.org/2000/xmlns/";
            writer_.WriteAttributeString(attrPrefix ? attrPrefix->c_str() : nullptr, localName,
                nsArg.c_str(), a->Value().c_str());
        } while (a != e.lastAttr_);
    }
}

void ElementWriter::WriteEndElement()
{
    writer_.WriteEndElement();
    resolver_.PopScope();
}

void ElementWriter::WriteFullEndElement()
{
    writer_.WriteFullEndElement();
    resolver_.PopScope();
}

void ElementWriter::WriteElement(const XElement& e)
{
    // The C# iterative walk over the circular sibling list: descend into
    // element children, serialize the other nodes, and close every element
    // whose last child was just processed.
    PushAncestors(e);
    const XNode* n = &e;
    while (true) {
        if (const XElement* element = dynamic_cast<const XElement*>(n)) {
            WriteStartElement(*element);
            if (std::holds_alternative<std::monostate>(element->content_)) {
                WriteEndElement();
            } else if (const std::string* text = std::get_if<std::string>(&element->content_)) {
                writer_.WriteString(*text);
                WriteFullEndElement();
            } else {
                n = std::get<XNode*>(element->content_)->next_;
                continue;
            }
        } else {
            n->WriteTo(writer_);
        }
        // Close every ancestor (up to e) whose last child n is.
        while (n != &e) {
            const XContainer* parent = n->parent_;
            const XNode* last = parent != nullptr
                ? (std::holds_alternative<XNode*>(parent->content_) ? std::get<XNode*>(parent->content_) : nullptr)
                : nullptr;
            if (last != n)
                break;
            n = parent;
            WriteFullEndElement();
        }
        if (n != &e) {
            n = n->next_;
            continue;
        }
        break;
    }
}

} // namespace ILSpy::Decompiler::Xml
