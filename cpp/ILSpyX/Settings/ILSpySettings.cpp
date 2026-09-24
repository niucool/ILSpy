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

// Implementation of ILSpyX/Settings/ILSpySettings.hpp (the porting
// decisions are on the header).

#include "ILSpyX/Settings/ILSpySettings.hpp"

#include "ILSpyX/Settings/MutexProtector.hpp"

#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XmlTextParser.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace ILSpy::ILSpyX::Settings {
namespace {

// The C# `const string ConfigFileMutex = "01A91708-49D1-410D-B8EB-
// 4DE2662B3971"`.
const char* kConfigFileMutex = "01A91708-49D1-410D-B8EB-4DE2662B3971";

// The DecompilerVersionInfo template constants (the C# build substitutes
// the real revision; the port ships the template values).
const char* kVersionMajor = "11";
const char* kVersionMinor = "0";
const char* kVersionBuild = "0";
const char* kVersionRevision = "$INSERTREVISION$";

// The C# `static string GetConfigFile()`: the provider's path, or the
// ArgumentNullException.
std::string GetConfigFile()
{
    std::optional<std::function<std::string()>>& provider =
        ILSpySettings::SettingsFilePathProvider();
    if (!provider)
        throw std::invalid_argument(
            "Value cannot be null. (Parameter 'SettingsFilePathProvider')");
    return (*provider)();
}

// The C# `static XDocument LoadFile(string fileName)`:
// XDocument.Load(fileName, LoadOptions.None) -- the file decoded with
// its BOM consumed (what .NET's stream decoding does for the UTF-8 sidecar
// the port's own Save writes), the text parsed by the port's parser. A
// read failure or a parse failure both surface as exceptions the Load
// arms catch.
std::shared_ptr<Decompiler::Xml::XDocument> LoadFile(const std::string& fileName)
{
    std::ifstream in(fileName, std::ios::binary);
    if (!in)
        throw std::runtime_error("Could not find file '" + fileName + "'.");
    std::string text((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    // The UTF-8 BOM: XDocument.Load's decoding consumes it; the text
    // parser must not see it. (UTF-16 sidecars are a documented
    // divergence: the port reads UTF-8, the encoding .NET's own save
    // path writes.)
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return Decompiler::Xml::ParseDocumentText(text);
}

}  // namespace

ILSpySettings::ILSpySettings(
    std::shared_ptr<Decompiler::Xml::XElement> root)
    : root_(root != nullptr
              ? std::move(root)
              : std::make_shared<Decompiler::Xml::XElement>(
                    Decompiler::Xml::XName("ILSpy")))
{
}

std::optional<std::function<std::string()>>&
ILSpySettings::SettingsFilePathProvider()
{
    static std::optional<std::function<std::string()>> provider;
    return provider;
}

ILSpySettings ILSpySettings::Load()
{
    MutexProtector mutex(kConfigFileMutex);
    // The C# `return new ILSpySettings(LoadFile(GetConfigFile()).Root);`
    // with the IOException / XmlException arms yielding the empty
    // settings: the port's read failure (the C# IOException family) and
    // the parse exception (the XmlException, a std::runtime_error
    // subclass) both map to the fresh root. The catch stays at
    // std::runtime_error so the provider's ArgumentNullException (an
    // std::invalid_argument) propagates exactly as the C# specific
    // catches let it.
    try {
        std::shared_ptr<Decompiler::Xml::XDocument> doc =
            LoadFile(GetConfigFile());
        if (Decompiler::Xml::XElement* root = doc->Root())
            return ILSpySettings(
                std::static_pointer_cast<Decompiler::Xml::XElement>(
                    root->shared_from_this()));
        return ILSpySettings();
    } catch (const std::runtime_error&) {
        return ILSpySettings();
    }
}

std::shared_ptr<Decompiler::Xml::XElement> ILSpySettings::Section(
    const std::string& name) const
{
    // The C# `root.Element(section) ?? new XElement(section)`.
    if (Decompiler::Xml::XElement* element =
            root_->Element(Decompiler::Xml::XName(name)))
        return std::static_pointer_cast<Decompiler::Xml::XElement>(
            element->shared_from_this());
    return std::make_shared<Decompiler::Xml::XElement>(
        Decompiler::Xml::XName(name));
}

void ILSpySettings::SaveSettings(
    std::shared_ptr<Decompiler::Xml::XElement> section)
{
    // The C# Update(rootElement => { replace-or-add by the section's own
    // name }).
    Update([&section](Decompiler::Xml::XElement& rootElement) {
        if (Decompiler::Xml::XElement* existing =
                rootElement.Element(section->Name()))
            existing->ReplaceWith(section);
        else
            rootElement.Add(section);
    });
}

void ILSpySettings::Update(
    const std::function<void(Decompiler::Xml::XElement&)>& action)
{
    // We always reload the file on updates to ensure we aren't
    // overwriting unrelated changes performed by another ILSpy instance.
    MutexProtector mutex(kConfigFileMutex);
    const std::string config = GetConfigFile();
    std::shared_ptr<Decompiler::Xml::XDocument> doc;
    try {
        doc = LoadFile(config);
    } catch (const std::exception&) {
        // The C# IOException arm also ensures the directory exists.
        std::filesystem::path path(config);
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        doc = std::make_shared<Decompiler::Xml::XDocument>();
        doc->Add(std::make_shared<Decompiler::Xml::XElement>(
            Decompiler::Xml::XName("ILSpy")));
    }
    // The C# `doc.Root!.SetAttributeValue("version", Major + "." + Minor +
    // "." + Build + "." + Revision)`.
    doc->Root()->SetAttributeValue(Decompiler::Xml::XName("version"),
        std::string(kVersionMajor) + "." + kVersionMinor + "."
            + kVersionBuild + "." + kVersionRevision);
    action(*doc->Root());
    doc->Save(config);
    if (Decompiler::Xml::XElement* root = doc->Root())
        root_ = std::static_pointer_cast<Decompiler::Xml::XElement>(
            root->shared_from_this());
}

}  // namespace ILSpy::ILSpyX::Settings
