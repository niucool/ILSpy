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

// Port-authored stand-in for System.Xml.Linq.XDocumentType (the DTD node):
// the name/publicId/systemId/internalSubset quartet. The name is validated
// through XmlConvert.VerifyName (a QName-style name -- colons are allowed,
// unlike the NCName rule; see XmlConvert.hpp). The C# nullable string fields
// port as std::optional<std::string>: the null-vs-empty distinction is
// observable through DeepEquals (a null publicId does not equal an empty
// one).
//
// Gold-pinned against the real .NET 10 System.Xml.Linq via the XDomProbe
// probe (C:/temp-probe/XDomProbe/Program.cs): the QName acceptance of
// "ns:e"/":a"/"a:"/"a:b:c", the bad-name XmlException messages, and the
// null-vs-empty DeepEquals rejection. The change-notification arms of the
// setters are not ported (events are not ported).

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "XmlConvert.hpp"
#include "XNode.hpp"
#include "XmlWriter.hpp"
#include "XmlNodeType.hpp"

namespace ILSpy::Decompiler::Xml {

class XDocumentType : public XNode {
public:
    XDocumentType(std::string name, std::optional<std::string> publicId, std::optional<std::string> systemId,
        std::optional<std::string> internalSubset)
        : name_(VerifyName(name))
        , publicId_(std::move(publicId))
        , systemId_(std::move(systemId))
        , internalSubset_(std::move(internalSubset))
    {
    }

    explicit XDocumentType(const XDocumentType& other)
        : name_(other.name_)
        , publicId_(other.publicId_)
        , systemId_(other.systemId_)
        , internalSubset_(other.internalSubset_)
    {
    }

    XmlNodeType NodeType() const override { return XmlNodeType::DocumentType; }

    const std::string& Name() const { return name_; }
    void Name(std::string value)
    {
        std::string verified = VerifyName(value);
        name_ = std::move(verified);
    }

    const std::optional<std::string>& PublicId() const { return publicId_; }
    void PublicId(std::optional<std::string> value) { publicId_ = std::move(value); }

    const std::optional<std::string>& SystemId() const { return systemId_; }
    void SystemId(std::optional<std::string> value) { systemId_ = std::move(value); }

    const std::optional<std::string>& InternalSubset() const { return internalSubset_; }
    void InternalSubset(std::optional<std::string> value) { internalSubset_ = std::move(value); }

    std::shared_ptr<XNode> CloneNode() const override { return std::make_shared<XDocumentType>(*this); }

    // XDocumentType.WriteTo. The null-vs-empty distinction of the three
    // fields is observable through the rendered DOCTYPE.
    void WriteTo(XmlWriter& writer) const override
    {
        writer.WriteDocType(name_, publicId_ ? publicId_->c_str() : nullptr,
            systemId_ ? systemId_->c_str() : nullptr,
            internalSubset_ ? internalSubset_->c_str() : nullptr);
    }

    bool DeepEquals(const XNode& other) const override
    {
        const XDocumentType* otherType = dynamic_cast<const XDocumentType*>(&other);
        return otherType != nullptr && name_ == otherType->name_ && publicId_ == otherType->publicId_
            && systemId_ == otherType->systemId_ && internalSubset_ == otherType->internalSubset_;
    }

    std::int32_t GetDeepHashCode() const override
    {
        std::int32_t hash = static_cast<std::int32_t>(std::hash<std::string> {}(name_));
        if (publicId_.has_value())
            hash ^= static_cast<std::int32_t>(std::hash<std::string> {}(*publicId_));
        if (systemId_.has_value())
            hash ^= static_cast<std::int32_t>(std::hash<std::string> {}(*systemId_));
        if (internalSubset_.has_value())
            hash ^= static_cast<std::int32_t>(std::hash<std::string> {}(*internalSubset_));
        return hash;
    }

private:
    std::string name_;
    std::optional<std::string> publicId_;
    std::optional<std::string> systemId_;
    std::optional<std::string> internalSubset_;
};

} // namespace ILSpy::Decompiler::Xml
