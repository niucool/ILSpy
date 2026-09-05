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
// The probe's real mscorlib PEFile full name (the .NET Framework 4.8 GAC
// assembly the HandlerMapProbe drives).
constexpr const char* kMscorlibFullName =
    "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089";

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
        RegisterMscorlibStubs();
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

    // The section-E document (the HandlerMapProbe's MakeDocumentE): the walk
    // fixture plus the mscorlib-backed string/type rows -- a REAL resolved
    // member (String.Length / Type.Name over the stub main-module types),
    // which the non-attached and x:Name arms of PropertyHandler need.
    ILSpy::BamlDecompiler::Baml::BamlDocument MakeDocumentE()
    {
        ILSpy::BamlDecompiler::Baml::BamlDocument doc = MakeDocument();
        namespace Rec = ::ILSpy::BamlDecompiler::Baml;
        // The insertion point: before the ElementStart block (the info
        // records must stay inside the DocumentStart block).
        std::size_t insertAt = doc.Records.size() - 3;
        auto insert = [&doc, &insertAt](std::unique_ptr<Rec::BamlRecord> r) {
            doc.Records.insert(doc.Records.begin() + insertAt++, std::move(r));
        };

        auto mscorlib = std::make_unique<Rec::AssemblyInfoRecord>();
        mscorlib->AssemblyId = 1;
        mscorlib->AssemblyFullName = kMscorlibFullName;
        insert(std::move(mscorlib));

        // The Escape arm's leading-brace string (the PropertyWithExtension
        // plain-string initializer gold -- no E/F/G drive reads id 1, so the
        // row is additively invisible to the pinned section-E/F/G gold).
        auto brace = std::make_unique<Rec::StringInfoRecord>();
        brace->StringId = 1;
        brace->Value = "{Brace}";
        insert(std::move(brace));

        auto stringType = std::make_unique<Rec::TypeInfoRecord>();
        stringType->TypeId = 1;
        stringType->AssemblyId = 1;
        stringType->TypeFullName = "System.String";
        insert(std::move(stringType));

        auto length = std::make_unique<Rec::AttributeInfoRecord>();
        length->AttributeId = 1;
        length->OwnerTypeId = 1;
        length->AttributeUsage = 0;
        length->Name = "Length";
        insert(std::move(length));

        auto typeType = std::make_unique<Rec::TypeInfoRecord>();
        typeType->TypeId = 2;
        typeType->AssemblyId = 1;
        typeType->TypeFullName = "System.Type";
        insert(std::move(typeType));

        auto name = std::make_unique<Rec::AttributeInfoRecord>();
        name->AttributeId = 2;
        name->OwnerTypeId = 2;
        name->AttributeUsage = 0;
        name->Name = "Name";
        insert(std::move(name));
        return doc;
    }

    // Constructs the XamlContext over a fresh section-E document (the
    // handler drives' fixture; each drive constructs its own -- the
    // ResolveNamespace mutations couple drives that share a context). The
    // main module takes the probe's real four-part mscorlib full name for
    // these contexts (the real PEFile the HandlerMapProbe drives -- the
    // XamlType.ResolveNamespace main-module arm compares against it; the
    // short "mscorlib" shape the walk-fixture contexts keep is what the
    // MainModuleArm fixture tests pin).
    std::unique_ptr<::ILSpy::BamlDecompiler::XamlContext> MakeContextE()
    {
        compilation_.SetMainModuleFullAssemblyName(kMscorlibFullName);
        document_ = std::make_unique<ILSpy::BamlDecompiler::Baml::BamlDocument>(MakeDocumentE());
        return ::ILSpy::BamlDecompiler::XamlContext::Construct(compilation_, *document_, nullptr);
    }

    // The section-H document (the probe's MakeDocumentH): the section-E
    // rows plus the System.Windows.Style / TargetType rows -- a REAL
    // resolved member whose declaring type's FullName is
    // System.Windows.Style (the TargetTypeAnnotation arm's gate).
    ILSpy::BamlDecompiler::Baml::BamlDocument MakeDocumentH()
    {
        ILSpy::BamlDecompiler::Baml::BamlDocument doc = MakeDocumentE();
        namespace Rec = ::ILSpy::BamlDecompiler::Baml;
        // The insertion point: before the ElementStart block (the info
        // records must stay inside the DocumentStart block).
        std::size_t insertAt = doc.Records.size() - 3;
        auto insert = [&doc, &insertAt](std::unique_ptr<Rec::BamlRecord> r) {
            doc.Records.insert(doc.Records.begin() + insertAt++, std::move(r));
        };

        auto style = std::make_unique<Rec::TypeInfoRecord>();
        style->TypeId = 3;
        style->AssemblyId = 1;
        style->TypeFullName = "System.Windows.Style";
        insert(std::move(style));

        auto targetType = std::make_unique<Rec::AttributeInfoRecord>();
        targetType->AttributeId = 3;
        targetType->OwnerTypeId = 3;
        targetType->AttributeUsage = 0;
        targetType->Name = "TargetType";
        insert(std::move(targetType));
        return doc;
    }

    // Constructs the XamlContext over a fresh section-H document: the
    // probe's MakeCtxH counterpart over the stub fixture (the real
    // PresentationFramework main replaced by the stub mscorlib main module
    // carrying the XmlnsDefinitionAttribute rows the Style resolution and
    // the XmlnsProperty CLR-namespaces arm read, plus the System.Windows.
    // Style / TargetType stubs a real resolved member needs). The
    // registrations are idempotent (a shared fixture calling both
    // MakeContextE and MakeContextH keeps each shape intact).
    std::unique_ptr<::ILSpy::BamlDecompiler::XamlContext> MakeContextH()
    {
        ConfigureStyleStubs();
        compilation_.SetMainModuleFullAssemblyName(kMscorlibFullName);
        document_ = std::make_unique<ILSpy::BamlDecompiler::Baml::BamlDocument>(MakeDocumentH());
        return ::ILSpy::BamlDecompiler::XamlContext::Construct(compilation_, *document_, nullptr);
    }

private:
    // The Style / TargetType / XmlnsDefinitionAttribute stubs the
    // section-H contexts need (the TargetTypeAnnotation and
    // CLR-namespaces arms), registered once per fixture (idempotent --
    // SetMainModuleTypeDefinition replaces the same key, and the attribute
    // list is rebuilt wholesale).
    void ConfigureStyleStubs()
    {
        namespace TS = ::ILSpy::Decompiler::TypeSystem;
        if (styleType_ != nullptr)
            return;

        // System.Windows.Style with its TargetType property (a REAL
        // resolved member on a main-module type -- the FullNameIs gate:
        // the property's DeclaringType aliases its own definition, the
        // IsAttachedTo / FullName comparisons).
        const TS::IModule* mainModule = &compilation_.MainModule();
        styleType_ = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "System.Windows.Style", "System.Windows",
            TS::FullTypeName(TS::TopLevelTypeName("System.Windows", "Style")),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation_, mainModule);
        targetTypeProperty_ = std::make_shared<TS::TestSupport::LookupProperty>(
            "TargetType", TS::ITypePtr(styleType_.get(), [](TS::IType*) {}), compilation_);
        targetTypeProperty_->SetDeclaringType(
            TS::ITypePtr(styleType_.get(), [](TS::IType*) {}));
        styleType_->SetProperties({ targetTypeProperty_.get() });
        compilation_.SetMainModuleTypeDefinition(
            TS::TopLevelTypeName("System.Windows", "Style"), styleType_.get());

        // The main module's XmlnsDefinitionAttribute rows (the real
        // PresentationFramework's reconstructed counterpart over the stub
        // main module): the presentation xmlns mapping to System.Windows
        // (the Style render's namespace resolution) and to
        // System.Windows.Controls (the second row of the multi-row
        // CLR-namespaces drive -- the yield order is the attribute order).
        xmlnsDefinitionAttributeType_ =
            std::make_shared<TS::TestSupport::LookupTypeDefinition>(
                "System.Windows.Markup.XmlnsDefinitionAttribute",
                "System.Windows.Markup",
                TS::FullTypeName(TS::TopLevelTypeName(
                    "System.Windows.Markup", "XmlnsDefinitionAttribute")),
                TS::TypeKind::Class, TS::Accessibility::Public, compilation_, mainModule);
        // The compilation's String type for the fixed-argument values (the
        // SyntheticWpfModule.cpp precedent: the FindType reference is const,
        // and the no-op-deleter alias needs the non-const pointer).
        TS::ITypePtr stringType(
            const_cast<TS::IType*>(&compilation_.FindType(TS::KnownTypeCode::String)),
            [](TS::IType*) {});
        auto makeRow = [&](const char* xmlNs, const char* clrNs) {
            std::vector<TS::CustomAttributeTypedArgument> fixedArguments;
            fixedArguments.emplace_back(stringType, std::string(xmlNs));
            fixedArguments.emplace_back(stringType, std::string(clrNs));
            return std::make_shared<TS::TestSupport::LookupAttribute>(
                TS::ITypePtr(xmlnsDefinitionAttributeType_.get(), [](TS::IType*) {}),
                std::move(fixedArguments));
        };
        xmlnsDefinitionAttributes_.push_back(makeRow(kPresentationXmlns, "System.Windows"));
        xmlnsDefinitionAttributes_.push_back(
            makeRow(kPresentationXmlns, "System.Windows.Controls"));
        std::vector<const TS::IAttribute*> attributes;
        for (const auto& attribute : xmlnsDefinitionAttributes_)
            attributes.push_back(attribute.get());
        static_cast<TS::TestSupport::LookupModule&>(
            const_cast<TS::IModule&>(compilation_.MainModule()))
            .SetAssemblyAttributes(std::move(attributes));
    }

    // The mscorlib-backed stub types the section-E document resolves
    // (System.String with its Length property, System.Type with its Name
    // property): a REAL resolved member on a main-module type, which the
    // PropertyHandler non-attached and x:Name arms need. The definitions
    // answer the module walk's `GetTypeDefinition` through the main
    // module's type map; the properties' DeclaringType aliases their own
    // definition (the IsAttachedTo FullName comparison).
    void RegisterMscorlibStubs()
    {
        namespace TS = ::ILSpy::Decompiler::TypeSystem;
        const TS::IModule* mainModule = &compilation_.MainModule();
        auto makeType = [&](const std::string& ns, const std::string& name) {
            return std::make_shared<TS::TestSupport::LookupTypeDefinition>(
                ns + "." + name, ns, TS::FullTypeName(TS::TopLevelTypeName(ns, name)),
                TS::TypeKind::Class, TS::Accessibility::Public, compilation_, mainModule);
        };
        stringType_ = makeType("System", "String");
        typeType_ = makeType("System", "Type");
        auto makeProperty = [&](const std::string& name,
                                TS::TestSupport::LookupTypeDefinition& declaring) {
            // The property type is never read by the property-resolution
            // paths (TryResolve's filter reads Name, IsAttachedTo reads
            // DeclaringType) -- the declaring definition stands in.
            auto property = std::make_shared<TS::TestSupport::LookupProperty>(
                name, TS::ITypePtr(&declaring, [](TS::IType*) {}), compilation_);
            property->SetDeclaringType(
                TS::ITypePtr(&declaring, [](TS::IType*) {}));
            return property;
        };
        lengthProperty_ = makeProperty("Length", *stringType_);
        nameProperty_ = makeProperty("Name", *typeType_);
        stringType_->SetProperties({ lengthProperty_.get() });
        typeType_->SetProperties({ nameProperty_.get() });
        compilation_.SetMainModuleTypeDefinition(
            TS::TopLevelTypeName("System", "String"), stringType_.get());
        compilation_.SetMainModuleTypeDefinition(
            TS::TopLevelTypeName("System", "Type"), typeType_.get());
    }

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
    // The mscorlib-backed stubs (RegisterMscorlibStubs) -- owned here so the
    // compilation's non-owning registrations stay valid for the fixture's
    // lifetime.
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition> stringType_;
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition> typeType_;
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupProperty> lengthProperty_;
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupProperty> nameProperty_;
    // The section-H stubs (ConfigureStyleStubs) -- owned here so the
    // compilation's non-owning registrations stay valid for the fixture's
    // lifetime.
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition> styleType_;
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition>
        xmlnsDefinitionAttributeType_;
    std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupProperty> targetTypeProperty_;
    std::vector<std::shared_ptr<::ILSpy::Decompiler::TypeSystem::TestSupport::LookupAttribute>>
        xmlnsDefinitionAttributes_;
};

} // namespace ILSpy::Tests::Baml
