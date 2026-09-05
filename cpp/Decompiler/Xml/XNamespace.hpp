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

// Port-authored stand-in for System.Xml.Linq.XNamespace (the BCL class the
// C# ICSharpCode.BamlDecompiler uses directly): an XML namespace -- nothing
// more than its namespace-name URI, plus the None/Xml/Xmlns well-known
// namespaces and the `ns + "local"` name construction.
//
// Every behavior was gold-pinned against the real .NET 10 System.Xml.Linq
// through the C:/temp-probe/XmlNameProbe probe and the decompiled
// System.Private.Xml.Linq XNamespace.cs:
//  * Get("") == None (NamespaceName ""); Xmlns is
//    "http://www.w3.org/2000/xmlns/"; Xml is the XML 1.0 xml namespace.
//  * ToString() returns the namespace name; equality is by namespace name.
//  * GetName(localName) validates the local name as an NCName (see
//    XName.hpp for the error shapes).
//
// C#-to-C++ porting decisions:
//  * The C# class is atomized (XNamespace.Get returns the interned
//    instance, so ReferenceEquals holds across Get calls). The port is a
//    value type with structural equality: reference identity is not
//    observable through any member the BAML decompiler consumes (all its
//    comparisons and dictionary keys go through ==/GetHashCode, which the
//    C# operator implements as reference equality over the interned
//    instances -- extensionally the same as comparing the namespace names).
//    This divergence is documented here and pinned by the value-equality
//    tests instead.
//  * The C# null namespace (distinct from None) surfaces at the consumers
//    as std::optional<XNamespace> (the nullable-Version convention, D36).
//  * GetName and operator+ are declared here but defined in XName.hpp (they
//    return XName by value).
//  * The C# ArgumentNullException for a null namespaceName is unreachable
//    -- std::string has no null.

#pragma once

#include <string>
#include <utility>

namespace ILSpy::Decompiler::Xml {

class XName;

class XNamespace {
public:
    // XNamespace.Get(namespaceName): any URI string, no validation.
    static XNamespace Get(std::string namespaceName)
    {
        return XNamespace(std::move(namespaceName));
    }

    // The namespace corresponding to no namespace ("").
    static const XNamespace& None();

    // The http://www.w3.org/XML/1998/namespace URI (the "xml" prefix).
    static const XNamespace& Xml();

    // The http://www.w3.org/2000/xmlns/ URI (the "xmlns" prefix).
    static const XNamespace& Xmlns();

    const std::string& NamespaceName() const noexcept
    {
        return namespaceName_;
    }

    // Declared here, defined in XName.hpp after XName is complete.
    XName GetName(std::string localName) const;

    std::string ToString() const
    {
        return namespaceName_;
    }

    friend bool operator==(const XNamespace& lhs, const XNamespace& rhs)
    {
        return lhs.namespaceName_ == rhs.namespaceName_;
    }

    friend bool operator!=(const XNamespace& lhs, const XNamespace& rhs)
    {
        return !(lhs == rhs);
    }

private:
    friend class XName;
    explicit XNamespace(std::string namespaceName)
        : namespaceName_(std::move(namespaceName))
    {
    }

    std::string namespaceName_;
};

// XNamespace + localName -> XName (the C# operator+). Defined in XName.hpp.
XName operator+(const XNamespace& ns, const std::string& localName);

} // namespace ILSpy::Decompiler::Xml
