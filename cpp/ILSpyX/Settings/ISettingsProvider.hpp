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

// Port of ICSharpCode.ILSpyX/Settings/ISettingsProvider.cs and
// ISettingsSection.cs (SettingsServiceBase.cs declares the section
// contract; IChildSettings lives there too).
//
// C#-to-C++ porting decisions:
//  * The C# `XElement this[XName section]` indexer ports as Section(name)
//    (the sections are always plain local names -- no namespace form is
//    ever used by the settings XML).
//  * The C# `void Update(Action<XElement> action)` ports as
//    std::function over a mutable element reference.
//  * ISettingsSection's INotifyPropertyChanged base does not port: the
//    change-notification has no consumer in the CLI-relevant subset, and
//    SettingsServiceBase's Section_PropertyChanged stays the virtual
//    no-op hook it is in the C#.

#pragma once

#include "Decompiler/Xml/XElement.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ILSpy::ILSpyX::Settings {

// The C# `public interface ISettingsProvider`.
class ISettingsProvider {
public:
    virtual ~ISettingsProvider() = default;

    // The C# `XElement this[XName section]`: the section element, or a
    // fresh empty element with that name.
    virtual std::shared_ptr<Decompiler::Xml::XElement> Section(
        const std::string& name) const = 0;

    virtual void Update(
        const std::function<void(Decompiler::Xml::XElement&)>& action) = 0;

    // The C# `void SaveSettings(XElement section)`.
    virtual void SaveSettings(
        std::shared_ptr<Decompiler::Xml::XElement> section) = 0;
};

// The C# `public interface ISettingsSection : INotifyPropertyChanged`
// (the notification base does not port -- see the header note).
class ISettingsSection {
public:
    virtual ~ISettingsSection() = default;

    virtual std::string SectionName() const = 0;
    virtual void LoadFromXml(const Decompiler::Xml::XElement& section) = 0;
    virtual std::shared_ptr<Decompiler::Xml::XElement> SaveToXml() const = 0;
};

// The C# `public interface IChildSettings` (a child section's back-pointer
// to its parent section; no settings section in the CLI-relevant subset
// implements it, but the contract is part of the surface).
class IChildSettings {
public:
    virtual ~IChildSettings() = default;
    virtual ISettingsSection& Parent() const = 0;
};

}  // namespace ILSpy::ILSpyX::Settings
