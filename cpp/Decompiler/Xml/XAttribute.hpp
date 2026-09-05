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

// Port-authored stand-in for System.Xml.Linq.XAttribute: the attribute half
// of the XLinq DOM. An XAttribute is an XObject but NOT an XNode -- it never
// enters a container's child list; it lives in the owning XElement's separate
// circular attribute list (lastAttr -> ... -> lastAttr).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XElemProbe
// probe (C:/temp-probe/XElemProbe/Program.cs); the C# semantics were taken
// from the decompiled System.Private.Xml.Linq XAttribute.cs.
//
// KEY DESIGN (ownership): the same shared_ptr GC-stand-in model as the node
// half (see XNode.hpp) -- the owning element's attribute-ownership list holds
// every attached attribute, and Remove() drops that reference so the detached
// attribute survives exactly while a shared_ptr still holds it (the
// collect-detach-re-add pattern the BamlDecompiler rewrite passes perform).
// XAttribute derives std::enable_shared_from_this so the lazy Attributes()
// sequence can root the current attribute across the consumer body.
//
// C#-to-C++ porting decisions:
//  * The C# `XAttribute(XName name, object value)` routes value through
//    XContainer.GetStringValue; the port takes the already-string value
//    directly (the numeric value.ToString() fallback arms are not modeled in
//    this port's content types -- documented divergence, no BamlDecompiler
//    call site passes a non-string attribute value).
//  * The C# cast operators (explicit operator string/int/double/... on the
//    attribute value) are not ported: no BamlDecompiler call site uses them
//    (they parse through System.Xml.XmlConvert), and the C++ equivalent is
//    a plain value parse by the consumer.
//  * DEFERRED (with the serialization slice): ToString() -- it renders
//    through an XmlWriter over a fragment (name="value" with the XmlWriter
//    attribute-value escaping and the auto-generated p{n} prefix for
//    namespace names not in scope), which needs the XmlWriter stand-in.
//  * ArgumentNullException arms (null name/value in the ctor, null value in
//    the Value setter) are unreachable: std::string has no null.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "XName.hpp"
#include "XObject.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XElement;

class XAttribute : public XObject, public std::enable_shared_from_this<XAttribute> {
public:
    // The C# `XAttribute(XName name, object value)` (see the porting note:
    // value arrives as the string GetStringValue would produce).
    XAttribute(XName name, std::string value);

    // The C# `XAttribute(XAttribute other)`: copies the name and value
    // only (a fresh, unparented, annotation-free attribute).
    explicit XAttribute(const XAttribute& other);

    XAttribute(const XAttribute&&) = delete;
    XAttribute& operator=(const XAttribute&) = delete;
    XAttribute& operator=(const XAttribute&&) = delete;

    // XAttribute.Name: the expanded name.
    const XName& Name() const noexcept { return name_; }

    // XAttribute.Value: the attribute value.
    const std::string& Value() const noexcept { return value_; }

    // XAttribute.Value setter: validates the new value against the name
    // (the xmlns rules) and assigns it.
    void Value(std::string value);

    // XAttribute.NextAttribute: the next attribute of the parent element
    // (null for the last attribute or an unparented attribute).
    XAttribute* NextAttribute() const;

    // XAttribute.PreviousAttribute: the previous attribute of the parent
    // element (null for the first or an unparented attribute).
    XAttribute* PreviousAttribute() const;

    // XAttribute.IsNamespaceDeclaration: true when this attribute declares a
    // namespace (a bare `xmlns` local name or the xmlns namespace).
    bool IsNamespaceDeclaration() const;

    // XAttribute.Remove: detaches this attribute from its parent element
    // (throws when unparented).
    void Remove();

    // XAttribute.SetValue: the Value setter through the string conversion
    // (see the porting note -- the port takes the string directly).
    void SetValue(std::string value) { Value(std::move(value)); }

    XmlNodeType NodeType() const override { return XmlNodeType::Attribute; }

    // internal XAttribute.GetDeepHashCode: the name hash xored with the
    // value hash (the C# string hash is randomized; only equality is
    // meaningful, never the number).
    std::int32_t GetDeepHashCode() const;

    // internal XAttribute.GetPrefixOfNamespace: the prefix bound to the
    // namespace through the owning element's in-scope declarations, falling
    // back to the implicit xml/xmlns prefixes (null when nothing binds it).
    // The XElement overload performs the full ancestor walk; this one is
    // reachable only for an unparented attribute (its ToString render).
    std::optional<std::string> GetPrefixOfNamespace(const XNamespace& ns) const;

    // The circular attribute-list link (see the class note). C# internal:
    // public for the same access-rule reason as XNode::next_ (the element
    // machinery and the Attributes() iterator read it through XAttribute*).
    XAttribute* next_ = nullptr;

    // The C# internal `name`/`value` fields: public for the same reason
    // (XElement's AttributesEqual and the Attributes() iterator compare
    // them directly). The Name()/Value() getters remain the public surface.
    XName name_;
    std::string value_;

private:
    // The static ValidateAttribute: the namespace-declaration validation the
    // ctor and the Value setter run (the exact C# ArgumentException
    // messages, SR.Argument_NamespaceDeclarationPrefixed/Xml/Xmlns).
    static void ValidateAttribute(const XName& name, const std::string& value);
};

} // namespace ILSpy::Decompiler::Xml
