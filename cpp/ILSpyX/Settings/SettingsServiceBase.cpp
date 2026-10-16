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

// Implementation of the non-template half of
// ILSpyX/Settings/SettingsServiceBase.hpp.

#include "ILSpyX/Settings/SettingsServiceBase.hpp"

namespace ILSpy::ILSpyX::Settings {

// The C# `protected static void SaveSection(ISettingsSection section,
// XElement root)`.
void SettingsServiceBase::SaveSection(ISettingsSection& section,
    Decompiler::Xml::XElement& root)
{
    std::shared_ptr<Decompiler::Xml::XElement> element = section.SaveToXml();
    if (Decompiler::Xml::XElement* existing =
            root.Element(Decompiler::Xml::XName(section.SectionName())))
        existing->ReplaceWith(element);
    else
        root.Add(element);
}

}  // namespace ILSpy::ILSpyX::Settings
