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

// Port-authored stand-in for System.Xml.Linq.XDocument: the concrete
// container this slice tests the XContainer machinery through. It carries
// the document-level content validation -- whitespace-only text, at most
// one document type (and only before an element), at most one root element
// (with the XElement slice), no CDATA or nested documents -- and rejects
// attribute content outright.
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs): the whitespace-only text rule
// ("Non-whitespace characters cannot be added to content."), the CDATA and
// Document rejections ("A node of type CDATA cannot be added to content."),
// the second-document-type rejection ("This operation would create an
// incorrectly structured document.") while a document type AFTER a comment
// is still accepted, and the doctype-then-comments content walk.
//
// DEFERRED (with the serialization slice): XDeclaration/Declaration,
// ToString/Save/WriteTo. DEFERRED (with the XElement slice): Root (the
// first child element). DEFERRED (the XmlReader paths): Load/Parse and the
// line-info/base-URI annotations they record.

#pragma once

#include <memory>
#include <string>

#include "XContainer.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XDocumentType;

class XDocument : public XContainer {
public:
    XDocument() = default;

    // The C# `XDocument(params object?[] content)` -- a braced XContent list
    // is the params form; a single content item is the one-element array.
    explicit XDocument(XContent content);

    // The C# `XDocument(XDocument other)`: the deep-copy constructor.
    explicit XDocument(const XDocument& other);

    XmlNodeType NodeType() const override { return XmlNodeType::Document; }

    // XDocument.DocumentType: the first document-type child, if any.
    XDocumentType* DocumentType();

    std::shared_ptr<XNode> CloneNode() const override;

    bool DeepEquals(const XNode& other) const override;

    std::int32_t GetDeepHashCode() const override;

protected:
    void ValidateNode(const XNode& node, const XNode* previous) override;
    void ValidateString(const std::string& s) override;

    void AddAttribute(std::shared_ptr<XAttribute> attribute) override;
    void AddAttributeSkipNotify(std::shared_ptr<XAttribute> attribute) override;

private:
    template <typename T>
    T* GetFirstNode();

    void ValidateDocument(const XNode* previous, XmlNodeType allowBefore, XmlNodeType allowAfter);
};

} // namespace ILSpy::Decompiler::Xml
