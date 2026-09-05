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

// Port-authored stand-in for System.Xml.Linq.XName (the BCL class the C#
// ICSharpCode.BamlDecompiler uses directly): an atomized {namespace, local
// name} pair -- the key type every XElement/XAttribute name, dictionary key
// and comparison in the XAML decompiler flows through.
//
// Every behavior was gold-pinned against the real .NET 10 System.Xml.Linq
// through the C:/temp-probe/XmlNameProbe probe and the decompiled
// System.Private.Xml.Linq XName.cs:
//  * Get("foo"): LocalName "foo", NamespaceName "" (== None),
//    ToString() "foo" (no braces for the empty namespace).
//  * Get("foo", "http://ns") == Get("{http://ns}foo") (ToString
//    "{http://ns}foo"); a null namespaceName is NOT accepted
//    (ArgumentNullException -- unreachable in the port, an empty string is
//    the None namespace).
//  * The expanded-name parse splits at the LAST '}' and requires a
//    non-empty namespace and a non-empty local name:
//    Get("{a}b}c") -> namespace "a}b", local "c"; Get("{a:b}c"),
//    Get("{ }a") and Get("{a{b}c") parse (the namespace is arbitrary
//    text); Get("{}a"), Get("{ns}"), Get("{a") throw ArgumentException
//    "'<input>' is an invalid expanded name." (the `num <= 1` guard covers
//    both the missing '}' and the empty namespace).
//  * The local name is validated as an NCName on every path (XmlException
//    through XmlConvert.VerifyNCName, see XmlConvert.hpp): Get("a:b"),
//    Get("{ns}a:b") -> "The ':' character, hexadecimal value 0x3A, cannot
//    be included in a name."
//  * GetHashCode is ns-hash ^ local-hash; == compares both parts.
//
// C#-to-C++ porting decisions:
//  * The C# atomizes names per namespace (ReferenceEquals across Get calls
//    with the same local/ns pair). The port is a value type with structural
//    equality (the XNamespace.hpp divergence note); == and the hash are
//    extensionally identical for every consumer (dictionary keys,
//    comparisons), which the tests pin.
//  * The C# implicit string->XName conversion maps to a non-explicit
//    single-argument constructor that parses the expanded name (the C#
//    operator delegates to Get(expandedName) and maps null to null XName --
//    the null half is the consumers' std::optional business).
//  * The C# ArgumentNullException arms (null localName/namespaceName/
//    expandedName) are unreachable -- std::string has no null.
//  * std::hash<XName> is provided so XName works as an unordered_map key
//    (the C# XName is a Dictionary key type throughout the handlers).

#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

#include "XmlConvert.hpp"
#include "XNamespace.hpp"

namespace ILSpy::Decompiler::Xml {

class XName {
public:
    // XName.Get(expandedName): "{namespace}local" or a bare local name.
    static XName Get(std::string expandedName);

    // XName.Get(localName, namespaceName): the namespaceName may be empty
    // (the None namespace) but not null.
    static XName Get(std::string localName, std::string namespaceName)
    {
        return XNamespace::Get(std::move(namespaceName)).GetName(std::move(localName));
    }

    // The C# implicit operator XName(string) -- the const char* form lets
    // string literals convert with a single user-defined conversion
    // (C++ forbids the const char* -> std::string -> XName chain).
    XName(const char* expandedName)
        : XName(Get(std::string(expandedName)))
    {
    }

    // The C# implicit operator XName(string).
    XName(std::string expandedName)
        : XName(Get(std::move(expandedName)))
    {
    }

    const std::string& LocalName() const noexcept
    {
        return localName_;
    }

    const std::string& NamespaceName() const noexcept
    {
        return namespaceName_;
    }

    XNamespace Namespace() const
    {
        return XNamespace(namespaceName_);
    }

    // The expanded name: "{namespace}local", or the bare local name when the
    // namespace is empty.
    std::string ToString() const
    {
        if (namespaceName_.empty())
            return localName_;
        return "{" + namespaceName_ + "}" + localName_;
    }

    friend bool operator==(const XName& lhs, const XName& rhs)
    {
        return lhs.localName_ == rhs.localName_ && lhs.namespaceName_ == rhs.namespaceName_;
    }

    friend bool operator!=(const XName& lhs, const XName& rhs)
    {
        return !(lhs == rhs);
    }

private:
    friend class XNamespace;
    // The internal C# ctor: an unvalidated local+namespace pair (the public
    // paths validate through VerifyNCName before reaching it).
    XName(std::string localName, std::string namespaceName)
        : localName_(std::move(localName))
        , namespaceName_(std::move(namespaceName))
    {
    }

    std::string localName_;
    std::string namespaceName_;
};

// XNamespace.GetName: validates the local name as an NCName. The C# null
// check (ArgumentNullException) is unreachable; the empty name reaches
// VerifyNCName's ArgumentException.
inline XName XNamespace::GetName(std::string localName) const
{
    VerifyNCName(localName);
    return XName(std::move(localName), namespaceName_);
}

inline XName operator+(const XNamespace& ns, const std::string& localName)
{
    return ns.GetName(localName);
}

} // namespace ILSpy::Decompiler::Xml

namespace std {

template <>
struct hash<ILSpy::Decompiler::Xml::XName> {
    size_t operator()(const ILSpy::Decompiler::Xml::XName& name) const noexcept
    {
        // The C# ns.GetHashCode() ^ localName.GetHashCode() shape.
        return hash<std::string>()(name.NamespaceName()) ^ hash<std::string>()(name.LocalName());
    }
};

} // namespace std
