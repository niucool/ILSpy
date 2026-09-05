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

// Port-authored stand-in for System.Xml.Linq.XCData (the CDATA text node): an
// XText subclass carrying only the CDATA node type -- it inherits the text
// storage, the Value surface and the NodeType-based DeepEquals (a CDATA only
// ever equals another CDATA). A CDATA node is rejected by XDocument content
// validation and never merged into by the adjacent-text append machinery (the
// Inserter/AddString `!(xText is XCData)` guards).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs).

#pragma once

#include "XText.hpp"

namespace ILSpy::Decompiler::Xml {

class XCData : public XText {
public:
    explicit XCData(std::string value)
        : XText(std::move(value))
    {
    }

    explicit XCData(const XCData& other)
        : XText(other)
    {
    }

    XmlNodeType NodeType() const override { return XmlNodeType::CDATA; }

    std::shared_ptr<XNode> CloneNode() const override { return std::make_shared<XCData>(*this); }
};

} // namespace ILSpy::Decompiler::Xml
