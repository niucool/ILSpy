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

// Port-authored stand-in for System.Xml.Linq.XProcessingInstruction: the
// target/data pair with the target validated as an NCName that must not be
// "xml" in any case (the SR.Argument_InvalidPIName rejection).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs): the "xml"/"XML" rejection
// message, the NCName messages for a bad start unit and an embedded colon,
// and the target-then-data DeepEquals. The change-notification arms of the
// setters are not ported (events are not ported).

#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include "XmlConvert.hpp"
#include "XNode.hpp"
#include "XmlWriter.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XProcessingInstruction : public XNode {
public:
    XProcessingInstruction(std::string target, std::string data)
        : target_(std::move(target))
        , data_(std::move(data))
    {
        ValidateName(target_);
    }

    explicit XProcessingInstruction(const XProcessingInstruction& other)
        : target_(other.target_)
        , data_(other.data_)
    {
    }

    XmlNodeType NodeType() const override { return XmlNodeType::ProcessingInstruction; }

    const std::string& Target() const { return target_; }
    void Target(std::string value)
    {
        ValidateName(value);
        target_ = std::move(value);
    }

    const std::string& Data() const { return data_; }
    void Data(std::string value) { data_ = std::move(value); }

    std::shared_ptr<XNode> CloneNode() const override
    {
        return std::make_shared<XProcessingInstruction>(*this);
    }

    // XProcessingInstruction.WriteTo.
    void WriteTo(XmlWriter& writer) const override
    {
        writer.WriteProcessingInstruction(target_, data_);
    }

    bool DeepEquals(const XNode& other) const override
    {
        const XProcessingInstruction* otherPi = dynamic_cast<const XProcessingInstruction*>(&other);
        return otherPi != nullptr && target_ == otherPi->target_ && data_ == otherPi->data_;
    }

    std::int32_t GetDeepHashCode() const override
    {
        return static_cast<std::int32_t>(std::hash<std::string> {}(target_))
            ^ static_cast<std::int32_t>(std::hash<std::string> {}(data_));
    }

private:
    // The C# ValidateName: an NCName that must not be "xml"
    // (StringComparison.OrdinalIgnoreCase).
    static void ValidateName(const std::string& name)
    {
        VerifyNCName(name);
        if (name.size() == 3 && (name[0] == 'x' || name[0] == 'X') && (name[1] == 'm' || name[1] == 'M')
            && (name[2] == 'l' || name[2] == 'L'))
            throw std::invalid_argument("'" + name + "' is an invalid name for a processing instruction.");
    }

    std::string target_;
    std::string data_;
};

} // namespace ILSpy::Decompiler::Xml
