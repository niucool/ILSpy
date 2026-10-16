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

// Port of the internal System.Xml.Linq serialization pair behind
// XElement.WriteTo/ToString: the ElementWriter (the iterative element-tree
// walk that pushes the xmlns scopes and drives the XmlWriter) and its
// NamespaceResolver (the prefix scope stack over the ancestor and element
// namespace declarations).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XmlWriteProbe
// probe; the C# semantics were taken from the decompiled
// System.Private.Xml.Linq sources (ElementWriter.cs, NamespaceResolver.cs).
//
// KEY DESIGN (the resolver as a walk-order vector): the C# NamespaceResolver is
// a circular linked list of NamespaceDeclaration nodes with a rover cache --
// _declaration points at the OLDEST entry and _declaration.prev at the
// NEWEST, GetPrefixOfNamespace walks newest-to-oldest, Add appends at the
// newest end, AddFirst inserts at the oldest end (so the innermost ancestor,
// AddFirst'd first, is walked before outer ones), and PopScope unlinks the
// current scope's entries. The port models the same walk order as a plain
// vector in visit order (index 0 = the most recent): Add inserts at the front,
// AddFirst appends at the end, PopScope erases the current scope's entries,
// and the visibility rule ("a more recent re-binding of the same prefix
// shadows an older one") is the scan for an earlier same-prefix entry. The
// rover cache is a performance-only structure and is not ported.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "XElement.hpp"
#include "XmlWriter.hpp"

namespace ILSpy::Decompiler::Xml {

// The ElementWriter's NamespaceResolver (System.Xml.Linq/NamespaceResolver.cs).
class ElementNamespaceResolver {
public:
    void PushScope()
    {
        ++scope_;
    }

    void PopScope();

    // Add: an entry of the current scope, taking priority over every earlier
    // entry (the current element's own declarations, newest-first).
    void Add(const std::string& prefix, const std::string& ns);

    // AddFirst: an entry at the lowest priority (PushAncestors's inner-to-outer
    // walk, so the first call -- the innermost ancestor -- still shadows the
    // later, outer ones).
    void AddFirst(const std::string& prefix, const std::string& ns);

    // GetPrefixOfNamespace: the newest visible binding for the namespace, or
    // nullopt. A binding is visible when no more recent entry re-defines its
    // prefix. With allowDefaultNamespace false the empty prefix (the default
    // namespace) is not a usable answer and the walk continues past it.
    std::optional<std::string> GetPrefixOfNamespace(const std::string& ns, bool allowDefaultNamespace) const;

private:
    struct Entry {
        std::string prefix;
        std::string ns;
        int scope;
    };
    std::vector<Entry> entries_; // in visit order (index 0 = most recent)
    int scope_ = 0;
};

// The ElementWriter (System.Xml.Linq/ElementWriter.cs): the iterative
// element-tree walk behind XElement.WriteTo.
class ElementWriter {
public:
    explicit ElementWriter(XmlWriter& writer)
        : writer_(writer)
    {
    }

    void WriteElement(const XElement& e);

private:
    // GetPrefixOfNamespace: the resolver's binding for the namespace, with the
    // "xml"/"xmlns" reserved-namespace fallbacks (null when nothing binds).
    std::optional<std::string> GetPrefixOfNamespace(const std::string& namespaceName, bool allowDefaultNamespace) const;

    void PushAncestors(const XElement& e);
    void PushElement(const XElement& e);
    void WriteStartElement(const XElement& e);
    void WriteEndElement();
    void WriteFullEndElement();

    XmlWriter& writer_;
    ElementNamespaceResolver resolver_;
};

} // namespace ILSpy::Decompiler::Xml
