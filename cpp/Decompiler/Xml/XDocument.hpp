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
// XDeclaration, ToString/Save/WriteTo are ported with the XmlWriter slice
// (Save(fileName) writes the UTF-8 preamble + the declaration). DEFERRED
// (the XmlReader paths): Load/Parse and the line-info/base-URI annotations
// they record.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include "XContainer.hpp"
#include "XmlNodeType.hpp"
#include "XmlWriter.hpp"

namespace ILSpy::Decompiler::Xml {

class XDocumentType;

// System.Xml.Linq.XDeclaration (the members the Save/WriteTo paths read: the
// encoding web name for the file save, the standalone value for the
// declaration).
class XDeclaration {
public:
    XDeclaration(std::string version, std::string encoding, std::string standalone)
        : Version(std::move(version))
        , Encoding(std::move(encoding))
        , Standalone(std::move(standalone))
    {
    }

    std::string Version;
    std::string Encoding;
    std::string Standalone;
};

class XDocument : public XContainer {
public:
    XDocument() = default;

    // XDocument.Parse(text) -- LoadOptions.None. Implemented in
    // XmlTextParser.cpp (the XmlReader text-parser stand-in).
    static std::shared_ptr<XDocument> Parse(const std::string& text);

    // The C# `XDocument(XDeclaration? declaration, params object?[] content)`.
    XDocument(XDeclaration declaration, XContent content);

    // The C# `XDocument(params object?[] content)` -- a braced XContent list
    // is the params form; a single content item is the one-element array.
    explicit XDocument(XContent content);

    // The C# `XDocument(XDocument other)`: the deep-copy constructor.
    explicit XDocument(const XDocument& other);

    // XDocument.Declaration: the XML declaration of this document (null when
    // the document was built without one).
    const XDeclaration* Declaration() const { return declaration_ ? &*declaration_ : nullptr; }
    void Declaration(XDeclaration value) { declaration_ = std::move(value); }

    XmlNodeType NodeType() const override { return XmlNodeType::Document; }

    // XDocument.DocumentType: the first document-type child, if any.
    XDocumentType* DocumentType();

    // XDocument.Root: the first child element (the document's root), or
    // null. Defined with the complete XElement type in XDocument.cpp.
    XElement* Root();

    std::shared_ptr<XNode> CloneNode() const override;

    bool DeepEquals(const XNode& other) const override;

    std::int32_t GetDeepHashCode() const override;

    // XDocument.WriteTo: the declaration (per XDeclaration.Standalone) +
    // the content + WriteEndDocument.
    void WriteTo(XmlWriter& writer) const override;

    // XDocument.Save(fileName): the file render (the declaration with the
    // XDeclaration encoding when it names one, else the settings' utf-8).
    void Save(const std::string& fileName) const;
    void Save(const std::string& fileName, SaveOptions options) const;

protected:
    void ValidateNode(const XNode& node, const XNode* previous) override;
    void ValidateString(const std::string& s) override;

    void AddAttribute(std::shared_ptr<XAttribute> attribute) override;
    void AddAttributeSkipNotify(std::shared_ptr<XAttribute> attribute) override;

private:
    template <typename T>
    T* GetFirstNode();

    std::optional<XDeclaration> declaration_;

    void ValidateDocument(const XNode* previous, XmlNodeType allowBefore, XmlNodeType allowAfter);
};

} // namespace ILSpy::Decompiler::Xml
