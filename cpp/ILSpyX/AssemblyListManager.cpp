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

#include "ILSpyX/AssemblyListManager.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <algorithm>
#include <filesystem>

namespace ILSpy::ILSpyX {

namespace {

namespace Xml = ILSpy::Decompiler::Xml;

}  // namespace

AssemblyListManager::AssemblyListManager(
    std::shared_ptr<Settings::ISettingsProvider> settingsProvider)
    : settings_(*settingsProvider)
{
    // The C# ctor reads the stored list names.
    const auto doc = settings_.Section("AssemblyLists");
    for (const auto& list : doc->Elements(Xml::XName("List"))) {
        const auto* name = list->Attribute("name");
        if (name != nullptr) {
            if (!ContainsList(name->Value())) {
                assemblyLists_.push_back(name->Value());
            }
        }
    }
}

std::unique_ptr<AssemblyList> AssemblyListManager::LoadList(
    const std::string& listName)
{
    std::unique_ptr<AssemblyList> list = DoLoadList(&listName);
    if (!ContainsList(list->ListName())) {
        assemblyLists_.push_back(list->ListName());
    }
    return list;
}

std::unique_ptr<AssemblyList> AssemblyListManager::DoLoadList(
    const std::string* listName)
{
    const auto doc = settings_.Section("AssemblyLists");
    if (listName != nullptr) {
        for (const auto& list : doc->Elements(Xml::XName("List"))) {
            const auto* name = list->Attribute("name");
            if (name != nullptr && name->Value() == *listName) {
                return std::make_unique<AssemblyList>(*this, *list);
            }
        }
    }
    return std::make_unique<AssemblyList>(*this,
        listName != nullptr ? *listName : DefaultListName);
}

void AssemblyListManager::SaveList(AssemblyList& list)
{
    // The C# Update closure: ensure the AssemblyLists section, replace the
    // matching <List> or append the fresh one.
    settings_.Update([&list](Xml::XElement& root) {
        Xml::XElement* doc = root.Element(Xml::XName("AssemblyLists"));
        if (doc == nullptr) {
            auto fresh = std::make_shared<Xml::XElement>(
                Xml::XName("AssemblyLists"));
            doc = fresh.get();
            root.Add(std::move(fresh));
        }
        // Locate the matching <List> first, then replace: mutating the
        // container inside its live Elements() range is undefined for the
        // lazy sequence (the C# FirstOrDefault materializes the same way).
        Xml::XElement* match = nullptr;
        for (const auto& listElement : doc->Elements(Xml::XName("List"))) {
            const auto* name = listElement->Attribute("name");
            if (name != nullptr && name->Value() == list.ListName()) {
                match = listElement.get();
                break;
            }
        }
        if (match != nullptr) {
            match->ReplaceWith(list.SaveAsXml());
        } else {
            doc->Add(list.SaveAsXml());
        }
    });
}

bool AssemblyListManager::AddListIfNotExists(AssemblyList& list)
{
    if (!ContainsList(list.ListName())) {
        assemblyLists_.push_back(list.ListName());
        SaveList(list);
        return true;
    }
    return false;
}

bool AssemblyListManager::DeleteList(const std::string& name)
{
    const auto it = std::find(
        assemblyLists_.begin(), assemblyLists_.end(), name);
    if (it == assemblyLists_.end()) {
        return false;
    }
    assemblyLists_.erase(it);
    settings_.Update([&name](Xml::XElement& root) {
        Xml::XElement* doc = root.Element(Xml::XName("AssemblyLists"));
        if (doc == nullptr) {
            return;
        }
        // Locate first, then remove (no container mutation inside the
        // live Elements() range).
        Xml::XElement* match = nullptr;
        for (const auto& listElement : doc->Elements(Xml::XName("List"))) {
            const auto* listName = listElement->Attribute("name");
            if (listName != nullptr && listName->Value() == name) {
                match = listElement.get();
                break;
            }
        }
        if (match != nullptr) {
            match->Remove();
        }
    });
    return true;
}

void AssemblyListManager::ClearAll()
{
    assemblyLists_.clear();
    settings_.Update([](Xml::XElement& root) {
        if (Xml::XElement* doc =
                root.Element(Xml::XName("AssemblyLists"));
            doc != nullptr) {
            doc->Remove();
        }
    });
}

bool AssemblyListManager::CloneList(const std::string& selectedAssemblyList,
    const std::string& newListName)
{
    // The C# clones through the copy ctor: the source's assemblies are
    // adopted shared under the new name.
    std::unique_ptr<AssemblyList> source = DoLoadList(&selectedAssemblyList);
    auto newList = std::make_unique<AssemblyList>(*this, newListName);
    newList->AdoptAssembliesFrom(*source);
    return AddListIfNotExists(*newList);
}

bool AssemblyListManager::RenameList(const std::string& selectedAssemblyList,
    const std::string& newListName)
{
    // The C# clones under the new name, deletes the source, then adds the
    // clone.
    std::unique_ptr<AssemblyList> source = DoLoadList(&selectedAssemblyList);
    auto renamed = std::make_unique<AssemblyList>(*this, newListName);
    renamed->AdoptAssembliesFrom(*source);
    const bool removed = DeleteList(selectedAssemblyList);
    return AddListIfNotExists(*renamed) && removed;
}

std::unique_ptr<AssemblyList> AssemblyListManager::CreateList(
    const std::string& name)
{
    return std::make_unique<AssemblyList>(*this, name);
}

void AssemblyListManager::CreateDefaultAssemblyLists()
{
    if (!assemblyLists_.empty()) {
        return;
    }

    if (!ContainsList(DotNet4List)) {
        std::unique_ptr<AssemblyList> dotnet4 =
            CreateDefaultList(DotNet4List);
        if (dotnet4->Count() > 0) {
            AddListIfNotExists(*dotnet4);
        }
    }

    if (!ContainsList(DotNet35List)) {
        std::unique_ptr<AssemblyList> dotnet35 =
            CreateDefaultList(DotNet35List);
        if (dotnet35->Count() > 0) {
            AddListIfNotExists(*dotnet35);
        }
    }

    if (!ContainsList(ASPDotNetMVC3List)) {
        std::unique_ptr<AssemblyList> mvc =
            CreateDefaultList(ASPDotNetMVC3List);
        if (mvc->Count() > 0) {
            AddListIfNotExists(*mvc);
        }
    }
}

std::unique_ptr<AssemblyList> AssemblyListManager::CreateDefaultList(
    const std::string& name, const std::string* path,
    const std::string* newName)
{
    auto list = std::make_unique<AssemblyList>(*this,
        newName != nullptr ? *newName : name);
    // The C# switch arms; the GAC seeding no-ops on a host without a
    // machine GAC (GetAssemblyInGac finds nothing).
    if (name == DotNet4List) {
        AddToListFromGac(*list,
            "mscorlib, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Core, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data.DataSetExtensions, Version=4.0.0.0, "
            "Culture=neutral, PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Xaml, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Xml, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Xml.Linq, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "Microsoft.CSharp, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
        AddToListFromGac(*list,
            "PresentationCore, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "PresentationFramework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "WindowsBase, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
    } else if (name == DotNet35List) {
        AddToListFromGac(*list,
            "mscorlib, Version=2.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System, Version=2.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Core, Version=3.5.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data, Version=2.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data.DataSetExtensions, Version=3.5.0.0, "
            "Culture=neutral, PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Xml, Version=2.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Xml.Linq, Version=3.5.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "PresentationCore, Version=3.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "PresentationFramework, Version=3.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "WindowsBase, Version=3.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
    } else if (name == ASPDotNetMVC3List) {
        AddToListFromGac(*list,
            "mscorlib, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.ComponentModel.DataAnnotations, Version=4.0.0.0, "
            "Culture=neutral, PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Configuration, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
        AddToListFromGac(*list,
            "System.Core, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data.DataSetExtensions, Version=4.0.0.0, "
            "Culture=neutral, PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Data.Entity, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Drawing, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
        AddToListFromGac(*list,
            "System.EnterpriseServices, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
        AddToListFromGac(*list,
            "System.Web, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
        AddToListFromGac(*list,
            "System.Web.Abstractions, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.ApplicationServices, Version=4.0.0.0, "
            "Culture=neutral, PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.DynamicData, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.Entity, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Web.Extensions, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.Mvc, Version=3.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.Routing, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.Services, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
        AddToListFromGac(*list,
            "System.Web.WebPages, Version=1.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Web.Helpers, Version=1.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35");
        AddToListFromGac(*list,
            "System.Xml, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "System.Xml.Linq, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089");
        AddToListFromGac(*list,
            "Microsoft.CSharp, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b03f5f7f11d50a3a");
    } else if (path != nullptr) {
        AddFrameworkAssembliesFromDirectory(*list, *path);
    }
    return list;
}

void AssemblyListManager::AddToListFromGac(AssemblyList& list,
    const std::string& fullName)
{
    const auto reference =
        Decompiler::Metadata::AssemblyNameReference::Parse(fullName);
    const auto file = Decompiler::Metadata::UniversalAssemblyResolver::
        GetAssemblyInGac(reference);
    if (file.has_value()) {
        list.OpenAssembly(*file);
    }
}

void AssemblyListManager::AddFrameworkAssembliesFromDirectory(
    AssemblyList& list, const std::string& directory)
{
    // The C# Directory.GetFiles(directory, "*.dll") -- case-insensitive on
    // Windows; the port filters the extension case-insensitively.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory,
             ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const std::string file = entry.path().string();
        const std::string fileName = entry.path().filename().string();
        if (fileName.size() >= 4) {
            std::string extension = fileName.substr(fileName.size() - 4);
            std::transform(extension.begin(), extension.end(),
                extension.begin(), [](char c) {
                    return c >= 'A' && c <= 'Z'
                        ? static_cast<char>(c - 'A' + 'a')
                        : c;
                });
            if (extension != ".dll") {
                continue;
            }
        } else {
            continue;
        }
        if (IsIncludedFrameworkFile(fileName)) {
            list.OpenAssembly(file);
        }
    }
}

bool AssemblyListManager::IsIncludedFrameworkFile(const std::string& fileName)
{
    if (fileName == "Microsoft.DiaSymReader.Native.amd64.dll") {
        return false;
    }
    // The C# EndsWith(OrdinalIgnoreCase) arm.
    if (fileName.size() >= 9) {
        std::string tail = fileName.substr(fileName.size() - 9);
        std::transform(tail.begin(), tail.end(), tail.begin(), [](char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        });
        if (tail == "_cor3.dll") {
            return false;
        }
    }
    // The C# `char.IsUpper(fileName[0])` reads index 0 unguarded; the
    // port guards the empty case to false (GetFiles never yields an empty
    // name).
    if (!fileName.empty() && fileName[0] >= 'A' && fileName[0] <= 'Z') {
        return true;
    }
    if (fileName == "netstandard.dll") {
        return true;
    }
    if (fileName == "mscorlib.dll") {
        return true;
    }
    return false;
}

}  // namespace ILSpy::ILSpyX
