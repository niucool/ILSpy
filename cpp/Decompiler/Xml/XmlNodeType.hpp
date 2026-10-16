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

// Port-authored stand-in for System.Xml.XmlNodeType (the BCL enum the C#
// System.Xml.Linq classes use): the node type every XObject reports through
// XObject.NodeType and the XDocument validation dispatches on. The numeric
// values are fixed by the System.Xml API (probed from the .NET 10 runtime:
// None=0 .. XmlDeclaration=17, contiguous).

#pragma once

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::Xml {

enum class XmlNodeType : std::int32_t {
    None = 0,
    Element = 1,
    Attribute = 2,
    Text = 3,
    CDATA = 4,
    EntityReference = 5,
    Entity = 6,
    ProcessingInstruction = 7,
    Comment = 8,
    Document = 9,
    DocumentType = 10,
    DocumentFragment = 11,
    Notation = 12,
    Whitespace = 13,
    SignificantWhitespace = 14,
    EndElement = 15,
    EndEntity = 16,
    XmlDeclaration = 17,
};

// .NET Enum.ToString() for a value of this type: XmlNodeType is not [Flags],
// so every value renders as its member name. Used by the XDocument
// "A node of type {0} cannot be added to content." rejection message.
inline std::string ToString(XmlNodeType type)
{
    switch (type) {
    case XmlNodeType::None: return "None";
    case XmlNodeType::Element: return "Element";
    case XmlNodeType::Attribute: return "Attribute";
    case XmlNodeType::Text: return "Text";
    case XmlNodeType::CDATA: return "CDATA";
    case XmlNodeType::EntityReference: return "EntityReference";
    case XmlNodeType::Entity: return "Entity";
    case XmlNodeType::ProcessingInstruction: return "ProcessingInstruction";
    case XmlNodeType::Comment: return "Comment";
    case XmlNodeType::Document: return "Document";
    case XmlNodeType::DocumentType: return "DocumentType";
    case XmlNodeType::DocumentFragment: return "DocumentFragment";
    case XmlNodeType::Notation: return "Notation";
    case XmlNodeType::Whitespace: return "Whitespace";
    case XmlNodeType::SignificantWhitespace: return "SignificantWhitespace";
    case XmlNodeType::EndElement: return "EndElement";
    case XmlNodeType::EndEntity: return "EndEntity";
    case XmlNodeType::XmlDeclaration: return "XmlDeclaration";
    }
    return std::to_string(static_cast<std::int32_t>(type));
}

} // namespace ILSpy::Decompiler::Xml
