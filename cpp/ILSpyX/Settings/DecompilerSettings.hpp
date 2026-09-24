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

// Port of ICSharpCode.ILSpyX/Settings/DecompilerSettings.cs: the
// XML-loadable settings section over the engine's DecompilerSettings
// (--ilspy-settingsfile's target, and -ds's option surface).
//
// C#-to-C++ porting decisions:
//  * The C# reflects over the engine settings' Browsable public
//    properties (all one hundred ten are bool); C++ has no reflection,
//    so the port carries the same list as an explicit name ->
//    getter/setter table (kFlagRows, generated from the C# property
//    declarations in declaration order -- the order SaveToXml writes the
//    attributes in).
//  * The C# `(bool?)section.Attribute(p.Name)` parse: XAttribute's bool
//    cast accepts the XmlConvert forms ("true"/"false"/"1"/"0") and throws
//    FormatException for anything else; the port parses the same forms
//    and throws for the rest (the file-local ParseXmlBoolean).
//  * The C# `public override DecompilerSettings Clone()` (returning the
//    wrapper type) does not port: the engine's Clone returns by value,
//    and a C++ override cannot covary a value return (the engine slice is
//    what the port's value semantics hand back -- a documented
//    divergence with no consumer in the CLI-relevant subset).

#pragma once

#include "ILSpyX/Settings/ISettingsProvider.hpp"

#include "Decompiler/DecompilerSettings.hpp"

#include <string>

namespace ILSpy::ILSpyX::Settings {

using EngineSettings = ::ILSpy::Decompiler::DecompilerSettings;

// The C# `public class DecompilerSettings : Decompiler.DecompilerSettings,
// ISettingsSection`.
class DecompilerSettings : public ::ILSpy::Decompiler::DecompilerSettings,
                           public ISettingsSection {
public:
    // The C# `public XName SectionName => "DecompilerSettings"`.
    std::string SectionName() const override { return "DecompilerSettings"; }

    // The C# `public void LoadFromXml(XElement section)`: every flag whose
    // attribute is present (and bool-parseable).
    void LoadFromXml(const Decompiler::Xml::XElement& section) override;

    // The C# `public XElement SaveToXml()`: the section element carrying
    // every Browsable flag as an attribute.
    std::shared_ptr<Decompiler::Xml::XElement> SaveToXml() const override;

    // The C# `public static bool IsKnownOption(string name, out
    // PropertyInfo? property)`: whether name is one of the reflected
    // flags (the port answers the membership half; the CLI's -ds parse
    // walks the same table for the value).
    static bool IsKnownOption(const std::string& name);
};

}  // namespace ILSpy::ILSpyX::Settings
