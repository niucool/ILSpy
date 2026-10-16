// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The in-memory ISettingsProvider test double: one root element, Update
// applying actions to it (the shape the C# settings tests use).

#pragma once

#include "ILSpyX/Settings/ISettingsProvider.hpp"

#include "Decompiler/Xml/XElement.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ILSpy::Tests {

class InMemorySettingsProvider final
    : public ILSpy::ILSpyX::Settings::ISettingsProvider {
public:
    InMemorySettingsProvider()
        : root_(std::make_shared<Decompiler::Xml::XElement>(
              Decompiler::Xml::XName("Settings")))
    {
    }

    std::shared_ptr<Decompiler::Xml::XElement> Section(
        const std::string& name) const override
    {
        // The C# indexer: the section element, or a fresh empty element.
        for (const auto& child : root_->Elements()) {
            if (child->Name().LocalName() == name) {
                return std::shared_ptr<Decompiler::Xml::XElement>(root_,
                    child.get());
            }
        }
        return std::make_shared<Decompiler::Xml::XElement>(
            Decompiler::Xml::XName(name));
    }

    void Update(
        const std::function<void(Decompiler::Xml::XElement&)>& action)
        override
    {
        action(*root_);
    }

    void SaveSettings(
        std::shared_ptr<Decompiler::Xml::XElement> section) override
    {
        savedSection_ = std::move(section);
    }

    const Decompiler::Xml::XElement& Root() const { return *root_; }

private:
    std::shared_ptr<Decompiler::Xml::XElement> root_;
    std::shared_ptr<Decompiler::Xml::XElement> savedSection_;
};

}  // namespace ILSpy::Tests
