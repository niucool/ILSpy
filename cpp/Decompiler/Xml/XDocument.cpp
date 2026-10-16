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

#include "Decompiler/Xml/XDocument.hpp"

#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <stdexcept>

#include <cstdio>

namespace ILSpy::Decompiler::Xml {

XDocument::XDocument(XContent content)
{
    AddContentSkipNotify(std::move(content));
}

XDocument::XDocument(const XDocument& other)
    : XContainer(other)
{
}

XDocumentType* XDocument::DocumentType()
{
    return GetFirstNode<XDocumentType>();
}

XElement* XDocument::Root()
{
    return GetFirstNode<XElement>();
}

std::shared_ptr<XNode> XDocument::CloneNode() const
{
    return std::make_shared<XDocument>(*this);
}

bool XDocument::DeepEquals(const XNode& other) const
{
    const XDocument* otherDocument = dynamic_cast<const XDocument*>(&other);
    return otherDocument != nullptr && ContentsEqual(*otherDocument);
}

std::int32_t XDocument::GetDeepHashCode() const
{
    return ContentsHashCode();
}

void XDocument::ValidateNode(const XNode& node, const XNode* previous)
{
    switch (node.NodeType()) {
    case XmlNodeType::Text:
        ValidateString(static_cast<const XText&>(node).Value());
        break;
    case XmlNodeType::Element:
        ValidateDocument(previous, XmlNodeType::DocumentType, XmlNodeType::None);
        break;
    case XmlNodeType::DocumentType:
        ValidateDocument(previous, XmlNodeType::None, XmlNodeType::Element);
        break;
    case XmlNodeType::CDATA:
        throw std::invalid_argument("A node of type CDATA cannot be added to content.");
    case XmlNodeType::Document:
        throw std::invalid_argument("A node of type Document cannot be added to content.");
    default:
        break;
    }
}

void XDocument::ValidateDocument(const XNode* previous, XmlNodeType allowBefore, XmlNodeType allowAfter)
{
    XNode* xNode = ContentNode();
    if (xNode == nullptr)
        return;
    if (previous == nullptr)
        allowBefore = allowAfter;
    do {
        xNode = xNode->next_;
        XmlNodeType nodeType = xNode->NodeType();
        if (nodeType == XmlNodeType::Element || nodeType == XmlNodeType::DocumentType) {
            if (nodeType != allowBefore)
                throw std::runtime_error("This operation would create an incorrectly structured document.");
            allowBefore = XmlNodeType::None;
        }
        if (xNode == previous)
            allowBefore = allowAfter;
    } while (xNode != ContentNode());
}

void XDocument::ValidateString(const std::string& s)
{
    // The C# ContainsAnyExcept(" \t\r\n") over UTF-16 units; every UTF-8
    // byte of a non-whitespace character fails the same test.
    for (char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            throw std::invalid_argument("Non-whitespace characters cannot be added to content.");
    }
}

void XDocument::AddAttribute(std::shared_ptr<XAttribute>)
{
    throw std::invalid_argument("An attribute cannot be added to content.");
}

void XDocument::AddAttributeSkipNotify(std::shared_ptr<XAttribute>)
{
    throw std::invalid_argument("An attribute cannot be added to content.");
}

template <typename T>
T* XDocument::GetFirstNode()
{
    XNode* xNode = ContentNode();
    if (xNode != nullptr) {
        do {
            xNode = xNode->next_;
            if (T* result = dynamic_cast<T*>(xNode))
                return result;
        } while (xNode != ContentNode());
    }
    return nullptr;
}

template XDocumentType* XDocument::GetFirstNode<XDocumentType>();
template XElement* XDocument::GetFirstNode<XElement>();

XDocument::XDocument(XDeclaration declaration, XContent content)
    : XDocument(std::move(content))
{
    declaration_ = std::move(declaration);
}

void XDocument::WriteTo(XmlWriter& writer) const
{
    if (declaration_.has_value()) {
        if (declaration_->Standalone == "yes")
            writer.WriteStartDocument(true);
        else if (declaration_->Standalone == "no")
            writer.WriteStartDocument(false);
        else
            writer.WriteStartDocument();
    } else {
        writer.WriteStartDocument();
    }
    WriteContentTo(writer);
    writer.WriteEndDocument();
}

void XDocument::Save(const std::string& fileName) const
{
    Save(fileName, GetSaveOptionsFromAnnotations());
}

void XDocument::Save(const std::string& fileName, SaveOptions options) const
{
    XmlWriterSettings settings = GetXmlWriterSettings(options);
    if (declaration_.has_value() && !declaration_->Encoding.empty()) {
        // The C# resolves Encoding.GetEncoding(name) and silently keeps the
        // default when the name is unknown; the port carries the web name
        // verbatim (the file bytes stay UTF-8 regardless -- see the
        // XmlWriter.hpp sink note).
        settings.Encoding = declaration_->Encoding;
    }
    XmlWriter writer(std::move(settings), XmlWriterSink::File);
    WriteTo(writer);
    writer.Close();
    std::vector<std::uint8_t> bytes = writer.FileBytes();
    std::FILE* file = std::fopen(fileName.c_str(), "wb");
    if (file == nullptr)
        throw std::runtime_error("Cannot create file '" + fileName + "'.");
    std::fwrite(bytes.data(), 1, bytes.size(), file);
    std::fclose(file);
}

} // namespace ILSpy::Decompiler::Xml
