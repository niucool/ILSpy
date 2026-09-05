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

// Tests for the BamlContext port (ICSharpCode.BamlDecompiler/Baml/
// BamlContext.cs) -- the record id maps and the assembly resolution. Every
// expectation is gold-pinned against the REAL internal BamlContext from the
// installed ICSharpCode.BamlDecompiler.dll, driven through the
// C:/temp-probe/BamlContextProbe reflection probe over the same fixtures:
//  * the two real markup-compiler .baml streams (findtoolbar /
//    installationerror, read through the real BamlReader) pin the four id
//    maps row for row (the generated tables below, extracted from the probe's
//    dump) and every ResolveAssembly arm over real ids -- including the raw
//    AssemblyId 4096 a real TypeInfo carries (masked to id 0);
//  * the synthetic walk document (the probe's D fixture) pins the
//    `id == map.Count` guard (an out-of-order id skipped until its turn,
//    duplicates never re-added, noise records never added), the
//    TypeSerializerInfoRecord subclass landing in TypeIdMap, the main-module
//    match arm (the parsed Name equal to MainModule.AssemblyName), the
//    FindMatchingReference pick over the WindowsBase 3.0.0.0/4.0.0.0/4.0.0.0
//    trio (the highest version, the LAST of the equals), the 0xfff id mask
//    (0x1000/0x8001/0x1002/0xFFF3), and the per-masked-id cache;
//  * the empty document (the probe's E fixture) pins the empty maps and the
//    (null, null) miss arm (the null string mapping to the empty string);
//  * the mscorlib-only compilation (the probe's F fixture) pins the
//    KnownThings failure propagating out of the ctor (the port's documented
//    KnownThings divergence: the original exception, not the C#
//    DecompilerException wrap).

#include "BamlDecompiler/Baml/BamlContext.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/SyntheticWpfModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include "BamlDecompiler/BamlTestSupport.hpp"
#include "TestFixtures/RealBaml.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
namespace TS = ILSpy::Decompiler::TypeSystem;
// LookupStubs.hpp declares its stubs inside
// ILSpy::Decompiler::TypeSystem::TestSupport (the KnownThings_Test
// convention).
using namespace ILSpy::Decompiler::TypeSystem;

// ===== The generated gold tables ============================================
// Row-for-row pins of the four id maps over the two real fixtures, extracted
// from the BamlContextProbe dump of the real BamlContext by
// C:/temp-probe/gen_bamlcontext_expectations3.py (regenerate with that
// script). Index == id == the record's own id field (the ConstructContext
// guard makes the three identical).
struct GoldAssemblyRow { int Id; const char* AssemblyFullName; };
struct GoldAttributeRow { int Id; int OwnerTypeId; int AttributeUsage; const char* Name; };
struct GoldStringRow { int Id; const char* Value; };
struct GoldTypeRow { int Id; std::uint16_t AssemblyId; const char* TypeFullName; };

constexpr std::size_t kFindToolbarAssembliesCount = 5;
constexpr GoldAssemblyRow kFindToolbarAssemblies[] = {
    { 0, "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 1, "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 2, "PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 3, "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 4, "System.Xaml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089" },
};

constexpr std::size_t kFindToolbarAttributesCount = 59;
constexpr GoldAttributeRow kFindToolbarAttributes[] = {
    { 0, 64867, 1, "Language" },
    { 1, 64867, 3, "Name" },
    { 2, 64864, 0, "IsLocked" },
    { 3, 1, 0, "DirectionalNavigation" },
    { 4, 1, 0, "TabNavigation" },
    { 5, 65323, 0, "IsFocusScope" },
    { 6, 65310, 0, "Resources" },
    { 7, 64958, 0, "Color" },
    { 8, 65178, 0, "StartPoint" },
    { 9, 65178, 0, "EndPoint" },
    { 10, 65284, 0, "Color" },
    { 11, 65284, 0, "Offset" },
    { 12, 64916, 0, "TargetType" },
    { 13, 64980, 0, "Property" },
    { 14, 64980, 0, "Value" },
    { 15, 65516, 0, "Path" },
    { 16, 65516, 0, "RelativeSource" },
    { 17, 65428, 0, "TargetType" },
    { 18, 65486, 0, "Padding" },
    { 19, 65486, 3, "Name" },
    { 20, 65434, 3, "Name" },
    { 21, 65428, 0, "Triggers" },
    { 22, 64848, 0, "Property" },
    { 23, 64848, 0, "Value" },
    { 24, 64980, 0, "TargetName" },
    { 25, 65117, 0, "Conditions" },
    { 26, 65438, 0, "Property" },
    { 27, 65438, 0, "Value" },
    { 28, 64916, 0, "BasedOn" },
    { 29, 65094, 3, "Name" },
    { 30, 65051, 3, "Name" },
    { 31, 65051, 0, "AllowsTransparency" },
    { 32, 65051, 0, "VerticalOffset" },
    { 33, 65441, 0, "TypeInTargetAssembly" },
    { 34, 65441, 0, "ResourceId" },
    { 35, 64842, 0, "SnapsToDevicePixels" },
    { 36, 65310, 0, "Triggers" },
    { 37, 65338, 0, "RoutedEvent" },
    { 38, 65338, 0, "SourceName" },
    { 39, 64928, 0, "TargetName" },
    { 40, 64928, 0, "TargetProperty" },
    { 41, 65454, 0, "From" },
    { 42, 65454, 0, "To" },
    { 43, 64871, 0, "Duration" },
    { 44, 64871, 0, "AutoReverse" },
    { 45, 65367, 0, "From" },
    { 46, 65367, 0, "To" },
    { 47, 64935, 0, "Orientation" },
    { 48, 65282, 3, "Name" },
    { 49, 65190, 3, "Name" },
    { 50, 64897, 3, "Name" },
    { 51, 64897, 0, "MaxLength" },
    { 52, 65310, 0, "ToolTip" },
    { 53, 64862, 0, "ShowOnDisabled" },
    { 54, 65481, 3, "Name" },
    { 55, 65135, 3, "Name" },
    { 56, 65135, 0, "IsMainMenu" },
    { 57, 65133, 3, "Name" },
    { 58, 65133, 0, "IsCheckable" },
};

constexpr std::size_t kFindToolbarStringsCount = 7;
constexpr GoldStringRow kFindToolbarStrings[] = {
    { 0, "HasFocusBorderBrush" },
    { 1, "HasFocusBrush" },
    { 2, "IsPressedBrush" },
    { 3, "FillBrush" },
    { 4, "FindPreviousContent" },
    { 5, "FindNextContent" },
    { 6, "OptionsMenuItemStyle" },
};

constexpr std::size_t kFindToolbarTypesCount = 2;
constexpr GoldTypeRow kFindToolbarTypes[] = {
    { 0, 4096, "MS.Internal.Documents.FindToolBar" },
    { 1, 3, "System.Windows.Input.KeyboardNavigation" },
};

constexpr std::size_t kInstallationErrorAssembliesCount = 5;
constexpr GoldAssemblyRow kInstallationErrorAssemblies[] = {
    { 0, "PresentationUI" },
    { 1, "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 2, "PresentationCore, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 3, "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35" },
    { 4, "System.Xaml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089" },
};

constexpr std::size_t kInstallationErrorAttributesCount = 21;
constexpr GoldAttributeRow kInstallationErrorAttributes[] = {
    { 0, 65282, 1, "Language" },
    { 1, 65178, 0, "StartPoint" },
    { 2, 65178, 0, "EndPoint" },
    { 3, 65284, 0, "Color" },
    { 4, 65284, 0, "Offset" },
    { 5, 65282, 0, "ColumnDefinitions" },
    { 6, 65282, 0, "RowDefinitions" },
    { 7, 64842, 0, "Opacity" },
    { 8, 64978, 0, "Stretch" },
    { 9, 64978, 0, "StrokeStartLineCap" },
    { 10, 64978, 0, "StrokeEndLineCap" },
    { 11, 65470, 0, "Top" },
    { 12, 65470, 0, "Left" },
    { 13, 64842, 0, "OpacityMask" },
    { 14, 64990, 0, "ScaleX" },
    { 15, 64990, 0, "ScaleY" },
    { 16, 65310, 0, "Resources" },
    { 17, 64916, 0, "TargetType" },
    { 18, 64980, 0, "Property" },
    { 19, 64980, 0, "Value" },
    { 20, 65311, 3, "Name" },
};

constexpr std::size_t kInstallationErrorTypesCount = 1;
constexpr GoldTypeRow kInstallationErrorTypes[] = {
    { 0, 4096, "Microsoft.Internal.DeploymentUI.InstallationErrorPage" },
};

// The real BAML reference tokens (the probe fixtures used the same names).
const char* kTokenB77 = "b77a5c561934e089";
const char* kTokenBf = "31bf3856ad364e35";

// The BamlContextProbe compilation shape: a main module named "mscorlib"
// (standing in for the probe's real mscorlib PEFile main module -- the
// main-module-match arm compares the parsed Name against
// MainModule.AssemblyName) plus the seven SyntheticWpfModule stand-ins the
// KnownThings ctor needs, with the WindowsBase trio (3.0.0.0 / 4.0.0.0 /
// 4.0.0.0) pinning FindMatchingReference's highest-version pick and the
// LAST-of-equal tiebreak (gold refIdx=3: the second 4.0.0.0). The referenced
// list order matches the probe's ReferencedModules exactly.
class BamlContextFixture {
public:
    BamlContextFixture()
    {
        compilation_.SetMainModuleAssemblyName("mscorlib");
        AddSynthetic("System", "4.0.0.0", kTokenB77);
        windowsBase3_ = AddSynthetic("WindowsBase", "3.0.0.0", kTokenBf);
        windowsBase4a_ = AddSynthetic("WindowsBase", "4.0.0.0", kTokenBf);
        windowsBase4b_ = AddSynthetic("WindowsBase", "4.0.0.0", kTokenBf);
        presentationCore_ = AddSynthetic("PresentationCore", "4.0.0.0", kTokenBf);
        presentationFramework_ = AddSynthetic("PresentationFramework", "4.0.0.0", kTokenBf);
        AddSynthetic("System.Xml", "4.0.0.0", kTokenB77);
    }

    TS::ICompilation& Compilation() { return compilation_; }
    const TS::IModule* MainModule() const { return &compilation_.MainModule(); }
    const TS::IModule* WindowsBase3() const { return windowsBase3_; }
    const TS::IModule* WindowsBase4a() const { return windowsBase4a_; }
    const TS::IModule* WindowsBase4b() const { return windowsBase4b_; }
    const TS::IModule* PresentationCore() const { return presentationCore_; }
    const TS::IModule* PresentationFramework() const { return presentationFramework_; }

private:
    // A synthetic stand-in resolved once into the compilation's Modules list
    // AND its ReferencedModules list (the probe's SimpleCompilation lists
    // each reference module in both; the KnownFixture AddSynthetic shape).
    const TS::IModule* AddSynthetic(const std::string& name, const char* version, const char* token)
    {
        auto reference = ILSpy::BamlDecompiler::SyntheticWpfModule::CreateReference(
            std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
                ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                    name + ", Version=" + version + ", Culture=neutral, PublicKeyToken=" + token)),
            std::nullopt);
        TS::SimpleTypeResolveContext context(compilation_);
        const TS::IModule* module = reference->Resolve(context);
        references_.push_back(std::move(reference));
        compilation_.AddModule(module);
        compilation_.AddReferencedModule(module);
        return module;
    }

    TestSupport::LookupCompilation compilation_;
    std::vector<std::unique_ptr<TS::IModuleReference>> references_;
    const TS::IModule* windowsBase3_ = nullptr;
    const TS::IModule* windowsBase4a_ = nullptr;
    const TS::IModule* windowsBase4b_ = nullptr;
    const TS::IModule* presentationCore_ = nullptr;
    const TS::IModule* presentationFramework_ = nullptr;
};

// The probe's synthetic walk fixture (the D document): the guard, the noise,
// and the TypeSerializerInfoRecord subclass arm, in the exact record order
// the probe drove.
std::unique_ptr<Baml::BamlDocument> MakeWalkDocument()
{
    auto doc = std::make_unique<Baml::BamlDocument>();

    auto assemblyMscorlib = std::make_unique<Baml::AssemblyInfoRecord>();
    assemblyMscorlib->AssemblyId = 0;
    assemblyMscorlib->AssemblyFullName = "mscorlib, Version=4.0.0.0";
    doc->Add(std::move(assemblyMscorlib));

    auto noise = std::make_unique<Baml::TextRecord>();
    noise->Value = "noise";
    doc->Add(std::move(noise));

    auto outOfOrder = std::make_unique<Baml::AssemblyInfoRecord>();
    outOfOrder->AssemblyId = 2; // arrives before its turn (the map holds 0)
    outOfOrder->AssemblyFullName = "SkippedOutOfOrder, Version=1.0.0.0";
    doc->Add(std::move(outOfOrder));

    auto assemblyWindowsBase = std::make_unique<Baml::AssemblyInfoRecord>();
    assemblyWindowsBase->AssemblyId = 1;
    assemblyWindowsBase->AssemblyFullName =
        "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35";
    doc->Add(std::move(assemblyWindowsBase));

    auto assemblyPresentationUI = std::make_unique<Baml::AssemblyInfoRecord>();
    assemblyPresentationUI->AssemblyId = 2; // now its turn (the map holds 0..1)
    assemblyPresentationUI->AssemblyFullName =
        "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35";
    doc->Add(std::move(assemblyPresentationUI));

    auto attrWidth = std::make_unique<Baml::AttributeInfoRecord>();
    attrWidth->AttributeId = 0;
    attrWidth->OwnerTypeId = 0;
    attrWidth->AttributeUsage = 0;
    attrWidth->Name = "Width";
    doc->Add(std::move(attrWidth));

    auto attrDup = std::make_unique<Baml::AttributeInfoRecord>();
    attrDup->AttributeId = 0; // duplicate id: never re-added
    attrDup->OwnerTypeId = 9;
    attrDup->AttributeUsage = 9;
    attrDup->Name = "Dup";
    doc->Add(std::move(attrDup));

    auto attrHeight = std::make_unique<Baml::AttributeInfoRecord>();
    attrHeight->AttributeId = 1;
    attrHeight->OwnerTypeId = 1;
    attrHeight->AttributeUsage = 2;
    attrHeight->Name = "Height";
    doc->Add(std::move(attrHeight));

    auto stringS0 = std::make_unique<Baml::StringInfoRecord>();
    stringS0->StringId = 0;
    stringS0->Value = "s0";
    doc->Add(std::move(stringS0));

    auto stringDup = std::make_unique<Baml::StringInfoRecord>();
    stringDup->StringId = 0; // duplicate id: never re-added
    stringDup->Value = "dup";
    doc->Add(std::move(stringDup));

    auto stringSkipped = std::make_unique<Baml::StringInfoRecord>();
    stringSkipped->StringId = 5; // out of order: skipped
    stringSkipped->Value = "skipped";
    doc->Add(std::move(stringSkipped));

    auto typeButton = std::make_unique<Baml::TypeInfoRecord>();
    typeButton->TypeId = 0;
    typeButton->AssemblyId = 0;
    typeButton->TypeFullName = "System.Windows.Controls.Button";
    doc->Add(std::move(typeButton));

    auto typeTextBlock = std::make_unique<Baml::TypeSerializerInfoRecord>();
    typeTextBlock->TypeId = 1;
    typeTextBlock->AssemblyId = 1;
    typeTextBlock->TypeFullName = "System.Windows.Controls.TextBlock";
    typeTextBlock->SerializerTypeId = 5;
    doc->Add(std::move(typeTextBlock));

    auto typeSkipped = std::make_unique<Baml::TypeInfoRecord>();
    typeSkipped->TypeId = 3; // id 2 never arrived: skipped
    typeSkipped->AssemblyId = 0;
    typeSkipped->TypeFullName = "Skipped";
    doc->Add(std::move(typeSkipped));

    auto connection = std::make_unique<Baml::ConnectionIdRecord>();
    connection->ConnectionId = 7; // noise: never touches the maps
    doc->Add(std::move(connection));

    return doc;
}

// --- ConstructContext: the record walk (gold: the D fixture) ----------------

TEST(BamlContextTest, ConstructContextWalksRecordsIntoIdMaps)
{
    BamlContextFixture fixture;
    auto doc = MakeWalkDocument();
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), *doc);

    // The guard: only ascending consecutive ids -- id 2 arriving before id 1
    // is skipped, then id 1 and the LATER id 2 are added (gold D).
    ASSERT_EQ(ctx->AssemblyIdMap.size(), 3u);
    EXPECT_EQ(ctx->AssemblyIdMap[0]->AssemblyId, 0);
    EXPECT_STREQ(ctx->AssemblyIdMap[0]->AssemblyFullName.c_str(), "mscorlib, Version=4.0.0.0");
    EXPECT_EQ(ctx->AssemblyIdMap[1]->AssemblyId, 1);
    EXPECT_STREQ(ctx->AssemblyIdMap[1]->AssemblyFullName.c_str(),
        "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35");
    EXPECT_EQ(ctx->AssemblyIdMap[2]->AssemblyId, 2);
    EXPECT_STREQ(ctx->AssemblyIdMap[2]->AssemblyFullName.c_str(),
        "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35");

    // The duplicate AttributeInfo id 0 never re-added (the "Dup" row is
    // gone); id 1 is added.
    ASSERT_EQ(ctx->AttributeIdMap.size(), 2u);
    EXPECT_EQ(ctx->AttributeIdMap[0]->AttributeId, 0);
    EXPECT_EQ(ctx->AttributeIdMap[0]->OwnerTypeId, 0);
    EXPECT_EQ(ctx->AttributeIdMap[0]->AttributeUsage, 0);
    EXPECT_STREQ(ctx->AttributeIdMap[0]->Name.c_str(), "Width");
    EXPECT_EQ(ctx->AttributeIdMap[1]->AttributeId, 1);
    EXPECT_EQ(ctx->AttributeIdMap[1]->OwnerTypeId, 1);
    EXPECT_EQ(ctx->AttributeIdMap[1]->AttributeUsage, 2);
    EXPECT_STREQ(ctx->AttributeIdMap[1]->Name.c_str(), "Height");

    // The duplicate StringInfo id 0 and the out-of-order id 5 are skipped.
    ASSERT_EQ(ctx->StringIdMap.size(), 1u);
    EXPECT_EQ(ctx->StringIdMap[0]->StringId, 0);
    EXPECT_STREQ(ctx->StringIdMap[0]->Value.c_str(), "s0");

    // TypeSerializerInfoRecord (a TypeInfoRecord subclass) lands in TypeIdMap
    // keeping its own fields; the id-3 record arriving after id 1 is skipped;
    // the Text and ConnectionId noise records never touch any map.
    ASSERT_EQ(ctx->TypeIdMap.size(), 2u);
    EXPECT_EQ(ctx->TypeIdMap[0]->TypeId, 0);
    EXPECT_EQ(ctx->TypeIdMap[0]->AssemblyId, 0);
    EXPECT_STREQ(ctx->TypeIdMap[0]->TypeFullName.c_str(), "System.Windows.Controls.Button");
    EXPECT_EQ(ctx->TypeIdMap[1]->TypeId, 1);
    EXPECT_EQ(ctx->TypeIdMap[1]->AssemblyId, 1);
    EXPECT_STREQ(ctx->TypeIdMap[1]->TypeFullName.c_str(), "System.Windows.Controls.TextBlock");
    const Baml::TypeSerializerInfoRecord* serializer =
        dynamic_cast<const Baml::TypeSerializerInfoRecord*>(ctx->TypeIdMap[1]);
    ASSERT_NE(serializer, nullptr);
    EXPECT_EQ(serializer->SerializerTypeId, 5);
}

// --- ConstructContext: the empty document (gold: the E fixture) -------------

TEST(BamlContextTest, ConstructContextEmptyDocumentLeavesMapsEmpty)
{
    BamlContextFixture fixture;
    Baml::BamlDocument doc;
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), doc);

    EXPECT_EQ(ctx->AssemblyIdMap.size(), 0u);
    EXPECT_EQ(ctx->AttributeIdMap.size(), 0u);
    EXPECT_EQ(ctx->StringIdMap.size(), 0u);
    EXPECT_EQ(ctx->TypeIdMap.size(), 0u);

    // Every id is the (null, null) miss arm: the empty-string null mapping and
    // the null module.
    Baml::ResolvedAssembly miss = ctx->ResolveAssembly(0);
    EXPECT_TRUE(miss.FullAssemblyName.empty());
    EXPECT_EQ(miss.Assembly, nullptr);
}

// --- ConstructContext over the real fixtures (gold: row for row) ------------

TEST(BamlContextTest, ConstructContextOverFindToolbarMatchesGold)
{
    BamlContextFixture fixture;
    Baml::BamlDocument doc =
        ILSpy::Tests::Baml::ReadBaml(ILSpy::Tests::FindToolbarBamlBytes());
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), doc);

    ASSERT_EQ(ctx->AssemblyIdMap.size(), kFindToolbarAssembliesCount);
    for (std::size_t i = 0; i < kFindToolbarAssembliesCount; i++) {
        EXPECT_EQ(ctx->AssemblyIdMap[i]->AssemblyId, kFindToolbarAssemblies[i].Id)
            << "assembly row " << i;
        EXPECT_STREQ(ctx->AssemblyIdMap[i]->AssemblyFullName.c_str(),
            kFindToolbarAssemblies[i].AssemblyFullName)
            << "assembly row " << i;
    }

    ASSERT_EQ(ctx->AttributeIdMap.size(), kFindToolbarAttributesCount);
    for (std::size_t i = 0; i < kFindToolbarAttributesCount; i++) {
        EXPECT_EQ(ctx->AttributeIdMap[i]->AttributeId, kFindToolbarAttributes[i].Id)
            << "attribute row " << i;
        EXPECT_EQ(ctx->AttributeIdMap[i]->OwnerTypeId, kFindToolbarAttributes[i].OwnerTypeId)
            << "attribute row " << i;
        EXPECT_EQ(ctx->AttributeIdMap[i]->AttributeUsage, kFindToolbarAttributes[i].AttributeUsage)
            << "attribute row " << i;
        EXPECT_STREQ(ctx->AttributeIdMap[i]->Name.c_str(), kFindToolbarAttributes[i].Name)
            << "attribute row " << i;
    }

    ASSERT_EQ(ctx->StringIdMap.size(), kFindToolbarStringsCount);
    for (std::size_t i = 0; i < kFindToolbarStringsCount; i++) {
        EXPECT_EQ(ctx->StringIdMap[i]->StringId, kFindToolbarStrings[i].Id)
            << "string row " << i;
        EXPECT_STREQ(ctx->StringIdMap[i]->Value.c_str(), kFindToolbarStrings[i].Value)
            << "string row " << i;
    }

    ASSERT_EQ(ctx->TypeIdMap.size(), kFindToolbarTypesCount);
    for (std::size_t i = 0; i < kFindToolbarTypesCount; i++) {
        EXPECT_EQ(ctx->TypeIdMap[i]->TypeId, kFindToolbarTypes[i].Id) << "type row " << i;
        EXPECT_EQ(ctx->TypeIdMap[i]->AssemblyId, kFindToolbarTypes[i].AssemblyId)
            << "type row " << i;
        EXPECT_STREQ(ctx->TypeIdMap[i]->TypeFullName.c_str(), kFindToolbarTypes[i].TypeFullName)
            << "type row " << i;
    }
}

TEST(BamlContextTest, ConstructContextOverInstallationErrorMatchesGold)
{
    BamlContextFixture fixture;
    Baml::BamlDocument doc =
        ILSpy::Tests::Baml::ReadBaml(ILSpy::Tests::InstallationErrorBamlBytes());
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), doc);

    ASSERT_EQ(ctx->AssemblyIdMap.size(), kInstallationErrorAssembliesCount);
    for (std::size_t i = 0; i < kInstallationErrorAssembliesCount; i++) {
        EXPECT_EQ(ctx->AssemblyIdMap[i]->AssemblyId, kInstallationErrorAssemblies[i].Id)
            << "assembly row " << i;
        EXPECT_STREQ(ctx->AssemblyIdMap[i]->AssemblyFullName.c_str(),
            kInstallationErrorAssemblies[i].AssemblyFullName)
            << "assembly row " << i;
    }

    ASSERT_EQ(ctx->AttributeIdMap.size(), kInstallationErrorAttributesCount);
    for (std::size_t i = 0; i < kInstallationErrorAttributesCount; i++) {
        EXPECT_EQ(ctx->AttributeIdMap[i]->AttributeId, kInstallationErrorAttributes[i].Id)
            << "attribute row " << i;
        EXPECT_EQ(ctx->AttributeIdMap[i]->OwnerTypeId,
            kInstallationErrorAttributes[i].OwnerTypeId)
            << "attribute row " << i;
        EXPECT_EQ(ctx->AttributeIdMap[i]->AttributeUsage,
            kInstallationErrorAttributes[i].AttributeUsage)
            << "attribute row " << i;
        EXPECT_STREQ(ctx->AttributeIdMap[i]->Name.c_str(),
            kInstallationErrorAttributes[i].Name)
            << "attribute row " << i;
    }

    // The fixture carries no StringInfo records (gold: count=0).
    EXPECT_EQ(ctx->StringIdMap.size(), 0u);

    ASSERT_EQ(ctx->TypeIdMap.size(), kInstallationErrorTypesCount);
    for (std::size_t i = 0; i < kInstallationErrorTypesCount; i++) {
        EXPECT_EQ(ctx->TypeIdMap[i]->TypeId, kInstallationErrorTypes[i].Id) << "type row " << i;
        EXPECT_EQ(ctx->TypeIdMap[i]->AssemblyId, kInstallationErrorTypes[i].AssemblyId)
            << "type row " << i;
        EXPECT_STREQ(ctx->TypeIdMap[i]->TypeFullName.c_str(),
            kInstallationErrorTypes[i].TypeFullName)
            << "type row " << i;
    }

    // The record's raw full name is carried VERBATIM: id 0's record carries
    // the bare short name "PresentationUI" (no version parts) and resolves to
    // itself (gold: full="PresentationUI", no matching reference).
    Baml::ResolvedAssembly presentationUI = ctx->ResolveAssembly(0);
    EXPECT_STREQ(presentationUI.FullAssemblyName.c_str(), "PresentationUI");
    EXPECT_EQ(presentationUI.Assembly, nullptr);
}

// --- ResolveAssembly: the arms (gold: the D and real-fixture resolves) ------

TEST(BamlContextTest, ResolveAssemblyMainModuleMatchArm)
{
    BamlContextFixture fixture;
    auto doc = MakeWalkDocument();
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), *doc);

    // "mscorlib, Version=4.0.0.0" parses to Name "mscorlib", equal to
    // MainModule.AssemblyName: the tuple's module IS the main module (gold:
    // isMain=True).
    Baml::ResolvedAssembly mscorlib = ctx->ResolveAssembly(0);
    EXPECT_STREQ(mscorlib.FullAssemblyName.c_str(), "mscorlib, Version=4.0.0.0");
    EXPECT_EQ(mscorlib.Assembly, fixture.MainModule());
}

TEST(BamlContextTest, ResolveAssemblyFindMatchingReferencePicksHighestLastOfEqual)
{
    BamlContextFixture fixture;
    auto doc = MakeWalkDocument();
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), *doc);

    // The WindowsBase trio: the highest version wins (not the 3.0.0.0) and
    // `<=` keeps the LAST of the equal 4.0.0.0 pair (gold refIdx=3 -- the
    // SECOND 4.0.0.0 stand-in).
    Baml::ResolvedAssembly windowsBase = ctx->ResolveAssembly(1);
    EXPECT_STREQ(windowsBase.FullAssemblyName.c_str(),
        "WindowsBase, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35");
    EXPECT_EQ(windowsBase.Assembly, fixture.WindowsBase4b());
    EXPECT_NE(windowsBase.Assembly, fixture.WindowsBase4a());
    EXPECT_NE(windowsBase.Assembly, fixture.WindowsBase3());
}

TEST(BamlContextTest, ResolveAssemblyRealFixtureArmsMatchGold)
{
    BamlContextFixture fixture;
    Baml::BamlDocument doc =
        ILSpy::Tests::Baml::ReadBaml(ILSpy::Tests::FindToolbarBamlBytes());
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), doc);

    // PresentationUI: a full name with NO matching reference keeps the name
    // and a null module (gold: asm=<null>).
    Baml::ResolvedAssembly presentationUI = ctx->ResolveAssembly(0);
    EXPECT_STREQ(presentationUI.FullAssemblyName.c_str(),
        "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35");
    EXPECT_EQ(presentationUI.Assembly, nullptr);

    Baml::ResolvedAssembly windowsBase = ctx->ResolveAssembly(1);
    EXPECT_EQ(windowsBase.Assembly, fixture.WindowsBase4b());
    Baml::ResolvedAssembly presentationCore = ctx->ResolveAssembly(2);
    EXPECT_EQ(presentationCore.Assembly, fixture.PresentationCore());
    Baml::ResolvedAssembly presentationFramework = ctx->ResolveAssembly(3);
    EXPECT_EQ(presentationFramework.Assembly, fixture.PresentationFramework());

    // System.Xaml: no matching reference in the compilation -> null module.
    Baml::ResolvedAssembly systemXaml = ctx->ResolveAssembly(4);
    EXPECT_STREQ(systemXaml.FullAssemblyName.c_str(),
        "System.Xaml, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(systemXaml.Assembly, nullptr);

    // Unmapped ids past the table end: the (null, null) miss.
    Baml::ResolvedAssembly unmapped = ctx->ResolveAssembly(5);
    EXPECT_TRUE(unmapped.FullAssemblyName.empty());
    EXPECT_EQ(unmapped.Assembly, nullptr);
}

TEST(BamlContextTest, ResolveAssemblyMasksIdWith0xfffBeforeTheLookup)
{
    BamlContextFixture fixture;
    auto doc = MakeWalkDocument();
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), *doc);

    // 0x1000 masks to 0 (the main-module arm), 0x8001 to 1 (WindowsBase),
    // 0x1002 to 2 (PresentationUI, null), 0xFFF3 to 4083 (unmapped: the
    // miss) -- all gold-pinned.
    Baml::ResolvedAssembly maskedMain = ctx->ResolveAssembly(0x1000);
    EXPECT_STREQ(maskedMain.FullAssemblyName.c_str(), "mscorlib, Version=4.0.0.0");
    EXPECT_EQ(maskedMain.Assembly, fixture.MainModule());

    Baml::ResolvedAssembly maskedWindowsBase = ctx->ResolveAssembly(0x8001);
    EXPECT_EQ(maskedWindowsBase.Assembly, fixture.WindowsBase4b());

    Baml::ResolvedAssembly maskedPresentationUI = ctx->ResolveAssembly(0x1002);
    EXPECT_STREQ(maskedPresentationUI.FullAssemblyName.c_str(),
        "PresentationUI, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35");
    EXPECT_EQ(maskedPresentationUI.Assembly, nullptr);

    Baml::ResolvedAssembly maskedMiss = ctx->ResolveAssembly(0xFFF3);
    EXPECT_TRUE(maskedMiss.FullAssemblyName.empty());
    EXPECT_EQ(maskedMiss.Assembly, nullptr);

    // The plain id 3 (past the D table's end) is the miss too.
    Baml::ResolvedAssembly plainMiss = ctx->ResolveAssembly(3);
    EXPECT_TRUE(plainMiss.FullAssemblyName.empty());
    EXPECT_EQ(plainMiss.Assembly, nullptr);
}

TEST(BamlContextTest, ResolveAssemblyCachesPerMaskedId)
{
    BamlContextFixture fixture;
    auto doc = MakeWalkDocument();
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), *doc);

    // The cache is keyed by the MASKED id and caches every arm: the plain and
    // the masked spelling of id 1 return the same resolved module (gold: the
    // repeated id=0x0001 resolve is identical).
    Baml::ResolvedAssembly first = ctx->ResolveAssembly(1);
    EXPECT_EQ(first.Assembly, fixture.WindowsBase4b());
    Baml::ResolvedAssembly second = ctx->ResolveAssembly(1);
    Baml::ResolvedAssembly masked = ctx->ResolveAssembly(0x8001);
    EXPECT_EQ(first.Assembly, second.Assembly);
    EXPECT_EQ(first.Assembly, masked.Assembly);
    EXPECT_STREQ(first.FullAssemblyName.c_str(), second.FullAssemblyName.c_str());
    EXPECT_STREQ(first.FullAssemblyName.c_str(), masked.FullAssemblyName.c_str());
}

// --- The KnownThings wiring ---------------------------------------------------

TEST(BamlContextTest, KnownThingsAccessorExposesTheConstructedKnownThings)
{
    BamlContextFixture fixture;
    auto doc = MakeWalkDocument();
    auto ctx = Baml::BamlContext::ConstructContext(fixture.Compilation(), *doc);

    // The ctor-constructed KnownThings (slot 0 -- mscorlib -- resolves to the
    // compilation's main module, the first module named mscorlib).
    EXPECT_EQ(ctx->KnownThings().FrameworkAssembly(), fixture.MainModule());
}

TEST(BamlContextTest, CtorPropagatesKnownThingsFailure)
{
    // The probe's F fixture: a compilation whose main module is mscorlib and
    // nothing else -- the KnownThings ctor inside BamlContext throws at the
    // first unresolvable known assembly ('System' after mscorlib). The C#
    // wraps it in DecompilerException; the port's KnownThings rethrows the
    // original exception (the documented divergence of that port).
    TestSupport::LookupCompilation compilation;
    compilation.SetMainModuleAssemblyName("mscorlib");
    Baml::BamlDocument doc;
    try {
        auto ctx = Baml::BamlContext::ConstructContext(compilation, doc);
        (void)ctx;
        FAIL() << "the KnownThings failure must propagate out of the BamlContext ctor";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Could not resolve known assembly 'System'!");
    }
}

// --- The FindMatchingReference version criterion -----------------------------

TEST(BamlContextTest, VersionOrderingFollowsCompareTo)
{
    // The .NET `Version.CompareTo` ordering the FindMatchingReference
    // criterion consumes (decompiled from the .NET 10 runtime):
    // component-wise, an unspecified component (-1) sorting BELOW a
    // specified one -- so Version(4, 0) < Version(4, 0, 0).
    EXPECT_TRUE(TS::Version(4, 0) < TS::Version(4, 0, 0));
    EXPECT_FALSE(TS::Version(4, 0, 0) <= TS::Version(4, 0));
    EXPECT_TRUE(TS::Version(4, 0) <= TS::Version(4, 0));
    EXPECT_TRUE(TS::Version(4, 0) <= TS::Version(4, 0, 0, 0));
    EXPECT_TRUE(TS::Version(4, 1) < TS::Version(4, 2));
    EXPECT_FALSE(TS::Version(4, 2) < TS::Version(4, 1));
    EXPECT_TRUE(TS::Version(3, 0, 0, 0) < TS::Version(4, 0));
    EXPECT_TRUE(TS::Version(4, 0, 0) < TS::Version(4, 0, 1));
    EXPECT_TRUE(TS::Version(4, 0, 0, 0) < TS::Version(4, 0, 0, 1));
}

} // namespace
