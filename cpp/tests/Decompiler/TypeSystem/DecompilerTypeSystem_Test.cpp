// Copyright (c) 2026 Jun Cai
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

#include "Decompiler/TypeSystem/DecompilerTypeSystem.hpp"

#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The DecompilerTypeSystem (DecompilerTypeSystem.cs): the reference-set
// wiring -- the compilation loads the main module's referenced assemblies
// through the resolver, so a TypeRef scoped to an AssemblyRef resolves into
// the referenced module's real type definition instead of the UnknownType
// fallback.

// The net48 PresentationFramework fixture (the baml-provisioned reference
// assemblies): PresentationFramework's FrameworkContentElement.Loaded event
// type is System.Windows.RoutedEventHandler, a TypeRef scoped to the
// PresentationCore AssemblyRef -- a genuine cross-assembly resolution.
constexpr const char* kNet48PresentationFramework =
    "/home/jim/ilspy-test-fixtures/net48/PresentationFramework.dll";

namespace {

// The Loaded event's return type resolved through the module's event
// entity (the MetadataEvent::ReturnType path: ResolveType over the
// event's type token).
const TS::IType* LoadedEventReturnType(const TS::MetadataModule& module,
                                       const std::string& typeName,
                                       const std::string& eventName)
{
    const ::ILSpy::Decompiler::Metadata::MetadataFile* file =
        module.MetadataFile();
    for (const auto& t : file->TypeDefs()) {
        if (t.Name != typeName)
            continue;
        for (const auto& e : file->GetEvents(t.Token)) {
            if (e.Name != eventName)
                continue;
            const TS::IEvent* event = module.GetDefinitionEvent(e.Token);
            if (event == nullptr)
                return nullptr;
            return &event->ReturnType();
        }
    }
    return nullptr;
}

} // namespace

// The plain first-level AssemblyRef arm (the C# InitializeCoreAsync queue's
// main-module seeds): the resolver loads PresentationCore from the search
// directory (the main file's own directory, the C# CLI's
// CreateTypeSystemFromFile resolver shape), and the cross-assembly TypeRef
// resolves to the REAL entity -- Kind() Delegate, not the UnknownType
// fallback Kind() Unknown.
TEST(DecompilerTypeSystemTest, ReferencedAssembliesResolveCrossAssemblyTypeRefs)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kNet48PresentationFramework, ec))
        GTEST_SKIP() << "the net48 fixture set is not provisioned";
    ::ILSpy::Decompiler::Metadata::MetadataFile pf(kNet48PresentationFramework);
    ASSERT_TRUE(pf.IsValid());

    // The C# CLI's GetDecompiler resolver shape: the resolver over the main
    // file's own name (the ctor derives the base directory and registers it
    // as the FIRST search directory -- the corpus), throwOnError FALSE (the
    // CLI's literal), the target framework from the module.
    ::ILSpy::Decompiler::Metadata::UniversalAssemblyResolver resolver(
        std::string(kNet48PresentationFramework), false,
        ::ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(pf));
    ::ILSpy::Decompiler::TypeSystem::DecompilerTypeSystem ts(pf, resolver);

    // The referenced set carries the corpus assemblies PresentationFramework
    // references (PresentationCore among them).
    bool foundPresentationCore = false;
    for (const TS::IModule* module : ts.ReferencedModules()) {
        if (module->Name() == "PresentationCore")
            foundPresentationCore = true;
    }
    EXPECT_TRUE(foundPresentationCore)
        << "PresentationCore loaded from the search directory";

    // The cross-assembly resolution: the Loaded event's type is the REAL
    // PresentationCore delegate, not the UnknownType name-only fallback.
    const TS::IType* eventType =
        LoadedEventReturnType(ts.MainMetadataModule(),
                               "FrameworkContentElement", "Loaded");
    ASSERT_NE(eventType, nullptr);
    EXPECT_EQ(eventType->Kind(),
              ::ILSpy::Decompiler::TypeSystem::TypeKind::Delegate)
        << "RoutedEventHandler resolves through the PresentationCore reference";
}

// The resolution-scope walk (MetadataModule::GetDeclaringModule over the
// AssemblyRef scope) hands the REFERENCED module back, and the compilation's
// Modules() is the main module first, then the referenced set (the C#
// SimpleCompilation Init order).
TEST(DecompilerTypeSystemTest, ModulesIsTheMainModuleFirst)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kNet48PresentationFramework, ec))
        GTEST_SKIP() << "the net48 fixture set is not provisioned";
    ::ILSpy::Decompiler::Metadata::MetadataFile pf(kNet48PresentationFramework);
    ASSERT_TRUE(pf.IsValid());

    ::ILSpy::Decompiler::Metadata::UniversalAssemblyResolver resolver(
        std::string(kNet48PresentationFramework), false,
        ::ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(pf));
    ::ILSpy::Decompiler::TypeSystem::DecompilerTypeSystem ts(pf, resolver);

    const std::vector<const TS::IModule*>& modules = ts.Modules();
    ASSERT_FALSE(modules.empty());
    EXPECT_EQ(modules[0], &ts.MainModule())
        << "the main module leads the compilation's module list";
    EXPECT_EQ(modules.size(), ts.ReferencedModules().size() + 1)
        << "Modules() is the main module plus the referenced set";
}

// The C# SimpleCompilation.FindType forwards to the KnownTypeCache: the
// known type resolves through the compilation's module set (the main
// module + the reference set -- mscorlib's System.Object over the corpus),
// a real ITypeDefinition, not a stand-in.
TEST(DecompilerTypeSystemTest, FindTypeResolvesThroughTheKnownTypeCache)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kNet48PresentationFramework, ec))
        GTEST_SKIP() << "the net48 fixture set is not provisioned";
    ::ILSpy::Decompiler::Metadata::MetadataFile pf(kNet48PresentationFramework);
    ASSERT_TRUE(pf.IsValid());

    ::ILSpy::Decompiler::Metadata::UniversalAssemblyResolver resolver(
        std::string(kNet48PresentationFramework), false,
        ::ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(pf));
    ::ILSpy::Decompiler::TypeSystem::DecompilerTypeSystem ts(pf, resolver);

    // System.Object: found in the referenced mscorlib's type definitions
    // (the KnownTypeCache's SearchType module scan).
    const TS::IType& object =
        ts.FindType(TS::KnownTypeCode::Object);
    EXPECT_EQ(object.Namespace(), "System");
    EXPECT_EQ(object.Name(), "Object");
    EXPECT_NE(dynamic_cast<const TS::ITypeDefinition*>(&object), nullptr)
        << "the known type is the real module entity, not a stand-in";
}
