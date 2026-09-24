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

// Port of ICSharpCode.ILSpyX/Settings/ILSpySettings.cs: the XML-backed
// settings provider over the settings sidecar file
// (--ilspy-settingsfile's target).
//
// C#-to-C++ porting decisions:
//  * The C# static `Func<string>? SettingsFilePathProvider` ports as a
//    settable static std::function; a call with no provider set throws
//    the ArgumentNullException
//    ("Value cannot be null. (Parameter 'SettingsFilePathProvider')")
//    as std::invalid_argument (the repo convention).
//  * The C# `XDocument.Load` / `doc.Save` pair ports through the port's
//    Xml layer (ParseDocumentText / XDocument::Save); the IOException and
//    XmlException catch-to-fresh-ILSpySettings arms map to the port's
//    read failure and parse exceptions respectively.
//  * The version attribute the Update path writes carries the
//    DecompilerVersionInfo template constants (11.0.0 with the
//    $INSERTREVISION$ placeholder -- the C# build substitutes the real
//    revision; the port ships the template values, file-local below).

#pragma once

#include "ILSpyX/Settings/ISettingsProvider.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ILSpy::ILSpyX::Settings {

// The C# `public class ILSpySettings : ISettingsProvider`.
class ILSpySettings : public ISettingsProvider {
public:
    // The C# `ILSpySettings(XElement? root = null)`: the empty root is
    // the C# null arm's `new XElement("ILSpy")`.
    explicit ILSpySettings(
        std::shared_ptr<Decompiler::Xml::XElement> root = nullptr);

    // The C# `public static Func<string>? SettingsFilePathProvider`:
    // disengaged is the C# null.
    static std::optional<std::function<std::string()>>&
    SettingsFilePathProvider();

    // The C# `public static ILSpySettings Load()`: the file from the
    // provider; a missing file or malformed XML yields the empty
    // settings.
    static ILSpySettings Load();

    // The C# `XElement this[XName section]`.
    std::shared_ptr<Decompiler::Xml::XElement> Section(
        const std::string& name) const override;

    // The C# `public void Update(Action<XElement> action)`: re-reads the
    // file under the mutex (so another instance's changes are not
    // clobbered), stamps the version attribute, applies the action, and
    // saves.
    void Update(
        const std::function<void(Decompiler::Xml::XElement&)>& action) override;

    // The C# `public void SaveSettings(XElement section)`.
    void SaveSettings(
        std::shared_ptr<Decompiler::Xml::XElement> section) override;

private:
    std::shared_ptr<Decompiler::Xml::XElement> root_;
};

}  // namespace ILSpy::ILSpyX::Settings
