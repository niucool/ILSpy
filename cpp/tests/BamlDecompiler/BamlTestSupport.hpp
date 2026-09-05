// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
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

// Shared helpers for the Phase-9 BAML suites: the record-type name table
// (the C# BamlRecordType enum's ToString spelling, shared by the reader
// and node test renderers) and the byte-span ReadDocument call every
// fixture goes through.

#pragma once

#include "BamlDecompiler/Baml/BamlReader.hpp"
#include "BamlDecompiler/SyntheticWpfModule.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Tests::Baml {

// A readable record type name for the failure diffs (the C# enum member
// spelling): the lib-side Baml::RecordTypeName (BamlRecords.hpp -- the
// HandlerMap duplicate-key message's key spelling), which this scaffold
// table predated; the tree-render tests exercise it against the gold dumps.

// Reads a fixture's bytes through the reader (the byte-span convention).
inline ILSpy::BamlDecompiler::Baml::BamlDocument ReadBaml(const std::string& bytes) {
    return ILSpy::BamlDecompiler::Baml::ReadDocument(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

// The XamlContext gold fixture (the XamlContextProbe shape): the six-module
// KnownThings compilation (the stub main module named mscorlib plus one
// SyntheticWpfModule stand-in per known assembly, PresentationCore the ONE
// module carrying the XmlnsDefinitionAttribute rows) and the walk document
// (a DocumentStart-rooted two-block record chain whose leaves are the info
// records ConstructContext maps and the PIMapping BuildPIMappings feeds).
// Every XamlContext/XamlType/XamlProperty expectation is pinned against the
// real internal classes driven over this identical fixture shape.
constexpr const char* kPresentationXmlns =
    "http://schemas.microsoft.com/winfx/2006/xaml/presentation";
constexpr const char* kProbePiNs = "http://probe.pi/ns";
constexpr const char* kPresentationFrameworkFullName =
    "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35";
constexpr const char* kPresentationCoreFullName =
    "PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35";

class XamlContextFixture {
public:
    XamlContextFixture()
    {
        compilation_.SetMainModuleAssemblyName("mscorlib");
        AddSynthetic("System", std::nullopt);
        AddSynthetic("WindowsBase", std::nullopt);
        presentationCore_ = AddSynthetic("PresentationCore",
            std::optional<std::string>(kPresentationXmlns));
        presentationFramework_ = AddSynthetic("PresentationFramework", std::nullopt);
        AddSynthetic("System.Xml", std::nullopt);
    }

    ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() { return compilation_; }
    const ILSpy::Decompiler::TypeSystem::IModule* PresentationCore() const
    {
        return presentationCore_;
    }
    const ILSpy::Decompiler::TypeSystem::IModule* PresentationFramework() const
    {
        return presentationFramework_;
    }

    // The probe's walk document (a DocumentStart-rooted two-block chain -- the
    // real BamlNode.Parse NREs when a leaf precedes the first header).
    ILSpy::BamlDecompiler::Baml::BamlDocument MakeDocument()
    {
        ILSpy::BamlDecompiler::Baml::BamlDocument doc;
        namespace Rec = ::ILSpy::BamlDecompiler::Baml;
        auto add = [&doc](std::unique_ptr<Rec::BamlRecord> r) { doc.Add(std::move(r)); };

        add(std::make_unique<Rec::DocumentStartRecord>());
        auto pi = std::make_unique<Rec::PIMappingRecord>();
        pi->XmlNamespace = kProbePiNs;
        pi->ClrNamespace = "System.Windows.Controls";
        pi->AssemblyId = 0;
        add(std::move(pi));

        auto assembly = std::make_unique<Rec::AssemblyInfoRecord>();
        assembly->AssemblyId = 0;
        assembly->AssemblyFullName = kPresentationFrameworkFullName;
        add(std::move(assembly));

        auto s = std::make_unique<Rec::StringInfoRecord>();
        s->StringId = 0;
        s->Value = "s0";
        add(std::move(s));

        auto type = std::make_unique<Rec::TypeInfoRecord>();
        type->TypeId = 0;
        type->AssemblyId = 0;
        type->TypeFullName = "System.Windows.Controls.Button";
        add(std::move(type));

        auto attr = std::make_unique<Rec::AttributeInfoRecord>();
        attr->AttributeId = 0;
        attr->OwnerTypeId = 0xFD63;
        attr->AttributeUsage = 0;
        attr->Name = "Width";
        add(std::move(attr));


        auto element = std::make_unique<Rec::ElementStartRecord>();
        element->TypeId = 0xFD63;
        add(std::move(element));

        auto text = std::make_unique<Rec::TextRecord>();
        text->Value = "hello";
        add(std::move(text));

        add(std::make_unique<Rec::ElementEndRecord>());
        add(std::make_unique<Rec::DocumentEndRecord>());
        return doc;
    }

    // Constructs the XamlContext over a fresh walk document. The document is
    // owned by the fixture for the context's lifetime (the id maps, the node
    // map, and the block tree all point into it).
    std::unique_ptr<::ILSpy::BamlDecompiler::XamlContext> MakeContext(
        const ::ILSpy::BamlDecompiler::BamlDecompilerSettings* settings = nullptr)
    {
        document_ = std::make_unique<ILSpy::BamlDecompiler::Baml::BamlDocument>(MakeDocument());
        return ::ILSpy::BamlDecompiler::XamlContext::Construct(compilation_, *document_, settings);
    }

private:
    const ILSpy::Decompiler::TypeSystem::IModule* AddSynthetic(
        const std::string& name, std::optional<std::string> xmlns)
    {
        auto reference = ::ILSpy::BamlDecompiler::SyntheticWpfModule::CreateReference(
            std::make_shared<::ILSpy::Decompiler::Metadata::AssemblyNameReference>(
                ::ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                    name + ", Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35")),
            std::move(xmlns));
        ::ILSpy::Decompiler::TypeSystem::SimpleTypeResolveContext context(compilation_);
        const ILSpy::Decompiler::TypeSystem::IModule* module = reference->Resolve(context);
        references_.push_back(std::move(reference));
        compilation_.AddModule(module);
        compilation_.AddReferencedModule(module);
        return module;
    }

    ::ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation compilation_;
    std::vector<std::unique_ptr<::ILSpy::Decompiler::TypeSystem::IModuleReference>> references_;
    std::unique_ptr<ILSpy::BamlDecompiler::Baml::BamlDocument> document_;
    const ILSpy::Decompiler::TypeSystem::IModule* presentationCore_ = nullptr;
    const ILSpy::Decompiler::TypeSystem::IModule* presentationFramework_ = nullptr;
};

} // namespace ILSpy::Tests::Baml
