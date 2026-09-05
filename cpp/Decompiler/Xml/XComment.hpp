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

// Port-authored stand-in for System.Xml.Linq.XComment (the comment node): the
// comment text, the copy constructor, and the type-checked DeepEquals (unlike
// XText, the comparison matches the CONCRETE type, not the node type).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs).

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "XNode.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XComment : public XNode {
public:
    explicit XComment(std::string value)
        : value_(std::move(value))
    {
    }

    explicit XComment(const XComment& other)
        : value_(other.value_)
    {
    }

    XmlNodeType NodeType() const override { return XmlNodeType::Comment; }

    const std::string& Value() const { return value_; }
    void Value(std::string value) { value_ = std::move(value); }

    std::shared_ptr<XNode> CloneNode() const override { return std::make_shared<XComment>(*this); }

    bool DeepEquals(const XNode& other) const override
    {
        const XComment* otherComment = dynamic_cast<const XComment*>(&other);
        return otherComment != nullptr && value_ == otherComment->value_;
    }

    std::int32_t GetDeepHashCode() const override
    {
        return static_cast<std::int32_t>(std::hash<std::string> {}(value_));
    }

private:
    std::string value_;
};

} // namespace ILSpy::Decompiler::Xml
