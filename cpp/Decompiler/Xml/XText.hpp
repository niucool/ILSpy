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

// Port-authored stand-in for System.Xml.Linq.XText (the text node): the
// `text` internal field the container machinery appends adjacent string
// content to, the copy constructor the clone paths use, and the
// NodeType-based DeepEquals (XCData inherits it and only matches another
// CDATA through the NodeType comparison).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs). DEFERRED with the
// serialization slice: WriteTo(XmlWriter) (no XmlWriter type exists yet);
// the Value setter's change-notification arms (events are not ported, so the
// plain assignment is the whole behavior). ArgumentNullException arms are
// unreachable: std::string has no null.

#pragma once

#include <memory>
#include <string>

#include "XNode.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XContainer;
class Inserter;

class XText : public XNode {
public:
    explicit XText(std::string value)
        : text_(std::move(value))
    {
    }

    // The C# `public XText(XText other)`: copies the text only (a fresh,
    // unparented, annotation-free node -- the base is default-constructed).
    explicit XText(const XText& other)
        : text_(other.text_)
    {
    }

    XmlNodeType NodeType() const override { return XmlNodeType::Text; }

    const std::string& Value() const { return text_; }
    void Value(std::string value) { text_ = std::move(value); }

    std::shared_ptr<XNode> CloneNode() const override { return std::make_shared<XText>(*this); }

    bool DeepEquals(const XNode& other) const override;

    void AppendText(std::string& text) const override { text += text_; }

    std::int32_t GetDeepHashCode() const override;

protected:
    // C# internal: the container append machinery writes this field
    // directly (AddStringSkipNotify concatenates into an adjacent XText).
    std::string text_;

    friend class XContainer;
    friend class Inserter;
};

} // namespace ILSpy::Decompiler::Xml
