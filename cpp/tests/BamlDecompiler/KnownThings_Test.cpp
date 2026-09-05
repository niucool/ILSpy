// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
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

// Tests for the KnownThings port (ICSharpCode.BamlDecompiler/Baml/KnownThings.cs)
// -- the well-known-entity resolution over a compilation. Every expectation is
// gold-pinned against the REAL internal KnownThings from the installed
// ICSharpCode.BamlDecompiler.dll, driven through the C:/temp-probe/KtProbe
// reflection probe over the same fixture shapes (a SimpleCompilation-based
// IDecompilerTypeSystem):
//  * the ctor SUCCEEDS with SyntheticWpfModule stand-ins standing in for the
//    unresolvable known assemblies (fixture A: 759/759 type rows resolve, the
//    mscorlib-slot rows through the real module's GetTypeDefinition, all 267
//    KnownMember.Property values null -- synthetic types carry no members);
//  * the ctor throws "Could not resolve known assembly 'X'!" at the FIRST
//    missing slot (mscorlib before System -- the InitAssemblies order), and
//    the C# catch wraps it in DecompilerException (the port's documented
//    deferral rethrows the original);
//  * the .NET SingleOrDefault semantics of the KnownMember property lookup:
//    "Sequence contains more than one element." on the ambiguous arm;
//  * the missing-id KeyNotFoundException ("The given key '0' was not present
//    in the dictionary.") -- the port's std::out_of_range via map::at.

#include "BamlDecompiler/Baml/KnownThings.hpp"
#include "BamlDecompiler/Baml/KnownThingsTables.hpp"
#include "BamlDecompiler/SyntheticWpfModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace KB = ILSpy::BamlDecompiler::Baml;
// LookupStubs.hpp declares its stubs inside ILSpy::Decompiler::TypeSystem::TestSupport;
// the using-directive makes the `TestSupport::` qualifier resolvable (the
// SyntheticWpfModule_Test convention).
using namespace ILSpy::Decompiler::TypeSystem;

// The real BAML reference tokens (the probe fixtures used the same names).
const char* kTokenB77 = "b77a5c561934e089";
const char* kTokenBf = "31bf3856ad364e35";

// A test compilation with the module set a KnownThings fixture needs: the
// shared LookupCompilation (whose own main module is the extra "LookupTests"
// entry ResolveAssembly never matches) plus configurable extra modules -- a
// "mscorlib"-named module with a hand-wired GetTypeDefinition table (the
// real-module InitType arm) and/or SyntheticWpfModule stand-ins resolved from
// SyntheticWpfModule::CreateReference (the probe's fixture A shape).
class KnownFixture {
public:
    KnownFixture() = default;

    // The "mscorlib" module: a plain LookupModule with a (by default empty)
    // GetTypeDefinition map.
    void AddMscorlibModule()
    {
        mscorlib_ = std::make_unique<TestSupport::LookupModule>(compilation_, "mscorlib");
        compilation_.AddModule(mscorlib_.get());
    }

    // A synthetic stand-in for the named assembly (CreateReference over the
    // bare assembly name, resolved once into the compilation's Modules list).
    const TS::IModule* AddSynthetic(const std::string& name, const char* token)
    {
        auto reference = ILSpy::BamlDecompiler::SyntheticWpfModule::CreateReference(
            std::make_shared<ILSpy::Decompiler::Metadata::AssemblyNameReference>(
                ILSpy::Decompiler::Metadata::AssemblyNameReference::Parse(
                    name + ", Version=4.0.0.0, Culture=neutral, PublicKeyToken=" + token)),
            std::nullopt);
        TS::SimpleTypeResolveContext context(compilation_);
        const TS::IModule* module = reference->Resolve(context);
        references_.push_back(std::move(reference));
        syntheticModules_.push_back(module);
        compilation_.AddModule(module);
        return module;
    }

    // The full fixture A shape: a real-map mscorlib module plus the five
    // synthetic stand-ins for the other known assemblies.
    void AddAllSix()
    {
        AddMscorlibModule();
        AddSynthetic("System", kTokenB77);
        AddSynthetic("WindowsBase", kTokenBf);
        AddSynthetic("PresentationCore", kTokenBf);
        AddSynthetic("PresentationFramework", kTokenBf);
        AddSynthetic("System.Xml", kTokenB77);
    }

    // A synthetic "mscorlib" (all six known assemblies synthetic -- every row
    // resolves through the RegisterType arm).
    void AddAllSixSynthetic()
    {
        AddSynthetic("mscorlib", kTokenB77);
        AddSynthetic("System", kTokenB77);
        AddSynthetic("WindowsBase", kTokenBf);
        AddSynthetic("PresentationCore", kTokenBf);
        AddSynthetic("PresentationFramework", kTokenBf);
        AddSynthetic("System.Xml", kTokenB77);
    }

    TS::ICompilation& Compilation() { return compilation_; }
    TestSupport::LookupModule* MscorlibModule() const { return mscorlib_.get(); }
    const TS::IModule* PresentationFrameworkSynthetic() const { return syntheticModules_[3]; }

    // A shared type-definition stub the mscorlib table maps a slot-0 row to.
    std::shared_ptr<TestSupport::LookupTypeDefinition> MakeType(const std::string& ns,
                                                                const std::string& name)
    {
        return std::make_shared<TestSupport::LookupTypeDefinition>(
            name, ns, TS::FullTypeName(TS::TopLevelTypeName(ns, name)),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation_, nullptr);
    }

private:
    TestSupport::LookupCompilation compilation_;
    std::unique_ptr<TestSupport::LookupModule> mscorlib_;
    std::vector<std::unique_ptr<TS::IModuleReference>> references_;
    std::vector<const TS::IModule*> syntheticModules_;
};

// Ctor + FrameworkAssembly: the six slots resolve by assembly short name (the
// mscorlib module is found although it is NOT the compilation's main module),
// and FrameworkAssembly is slot 0.
TEST(KnownThingsTest, CtorResolvesSixAssembliesAndFrameworkAssembly)
{
    KnownFixture fixture;
    fixture.AddAllSix();
    KB::KnownThings known(fixture.Compilation());
    EXPECT_EQ(known.FrameworkAssembly(), fixture.MscorlibModule());
    EXPECT_EQ(known.FrameworkAssembly()->AssemblyName(), "mscorlib");
}

// A known assembly missing from the compilation throws at its slot with the
// exact C# message (the probe's fixture B: mscorlib resolves, 'System' throws).
TEST(KnownThingsTest, CtorThrowsWhenKnownAssemblyMissing)
{
    KnownFixture fixture;
    fixture.AddMscorlibModule();
    try {
        KB::KnownThings known(fixture.Compilation());
        FAIL() << "expected the missing-assembly throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Could not resolve known assembly 'System'!");
    }
}

// InitAssemblies resolves mscorlib FIRST: with no mscorlib-named module the
// throw names the first slot (the probe's fixture C -- main module System.dll).
TEST(KnownThingsTest, CtorThrowsAtMscorlibSlotFirst)
{
    KnownFixture fixture;
    try {
        KB::KnownThings known(fixture.Compilation());
        FAIL() << "expected the missing-assembly throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Could not resolve known assembly 'mscorlib'!");
    }
}

// The synthetic InitType arm: a SyntheticWpfModule stand-in seeds the row
// through RegisterType (the seeded definition becomes visible to the module's
// own GetTypeDefinition lookup -- the RegisterType contract), with the row's
// namespace/name and the standing-in assembly.
TEST(KnownThingsTest, InitTypeSyntheticArmSeedsTheSyntheticModule)
{
    KnownFixture fixture;
    fixture.AddAllSix();
    KB::KnownThings known(fixture.Compilation());

    const TS::ITypeDefinition* accessText = known.Types(KB::KnownTypes::AccessText);
    ASSERT_NE(accessText, nullptr);
    EXPECT_EQ(accessText->Namespace(), "System.Windows.Controls");
    EXPECT_EQ(accessText->Name(), "AccessText");
    EXPECT_EQ(accessText->ParentModule()->AssemblyName(), "PresentationFramework");
    // The seeding went through RegisterType: the same instance the module's
    // lookup now finds, and repeated accessors return the cached instance.
    EXPECT_EQ(accessText, fixture.PresentationFrameworkSynthetic()->GetTypeDefinition(
                              TS::TopLevelTypeName("System.Windows.Controls", "AccessText")));
    EXPECT_EQ(known.Types(KB::KnownTypes::AccessText), accessText);
}

// The real-module InitType arm: a non-synthetic module resolves each row
// through GetTypeDefinition(TopLevelTypeName(ns, name)) -- the mapped
// definition, and null on a miss (a type the module does not carry).
TEST(KnownThingsTest, InitTypeRealArmResolvesThroughGetTypeDefinition)
{
    KnownFixture fixture;
    fixture.AddMscorlibModule();
    fixture.AddSynthetic("System", kTokenB77);
    fixture.AddSynthetic("WindowsBase", kTokenBf);
    fixture.AddSynthetic("PresentationCore", kTokenBf);
    fixture.AddSynthetic("PresentationFramework", kTokenBf);
    fixture.AddSynthetic("System.Xml", kTokenB77);

    auto booleanType = fixture.MakeType("System", "Boolean");
    auto stringType = fixture.MakeType("System", "String");
    fixture.MscorlibModule()->SetTypeDefinition(
        TS::TopLevelTypeName("System", "Boolean"), booleanType.get());
    fixture.MscorlibModule()->SetTypeDefinition(
        TS::TopLevelTypeName("System", "String"), stringType.get());

    KB::KnownThings known(fixture.Compilation());
    EXPECT_EQ(known.Types(KB::KnownTypes::Boolean), booleanType.get());
    EXPECT_EQ(known.Types(KB::KnownTypes::String), stringType.get());
    // The un-mapped mscorlib row resolves to null (a lookup miss is a legal
    // dictionary value -- no throw).
    EXPECT_EQ(known.Types(KB::KnownTypes::Guid), nullptr);

    // The member rows' PROPERTY TYPES resolve through the same InitType arm:
    // AccessText_Text's property type is the mscorlib String row (gold-pinned
    // by the probe: "type=mscorlib|System|String").
    const KB::KnownMember* text = known.Members(KB::KnownMembers::AccessText_Text);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->Type, stringType.get());
}

// The probe's fixture A invariant sweep: with all six known assemblies as
// synthetic stand-ins the ctor succeeds and EVERY row resolves -- all 759 type
// rows through RegisterType (name/namespace/assembly matching the table), all
// 267 member rows with a null Property (synthetic types carry no members), the
// two strings and the 227 resources.
TEST(KnownThingsTest, AllSyntheticSeedIsComplete)
{
    KnownFixture fixture;
    fixture.AddAllSixSynthetic();
    KB::KnownThings known(fixture.Compilation());

    int resolved = 0;
    for (const auto& entry : KB::KnownTypesTable) {
        const TS::ITypeDefinition* type = known.Types(entry.Id);
        ASSERT_NE(type, nullptr);
        resolved++;
        EXPECT_EQ(type->Name(), entry.Row.Name);
        EXPECT_EQ(type->Namespace(), entry.Row.Namespace);
        EXPECT_EQ(type->ParentModule()->AssemblyName(),
                  KB::KnownAssemblies[entry.Row.AssemblyIndex]);
    }
    EXPECT_EQ(resolved, 759);

    int membersResolved = 0;
    for (const auto& entry : KB::KnownMembersTable) {
        const KB::KnownMember* member = known.Members(entry.Id);
        ASSERT_NE(member, nullptr);
        membersResolved++;
        EXPECT_EQ(member->Parent, entry.Row.Parent);
        EXPECT_EQ(member->Name, entry.Row.Name);
        EXPECT_EQ(member->Property, nullptr);
        ASSERT_NE(member->DeclaringType, nullptr);
        EXPECT_EQ(member->DeclaringType, known.Types(entry.Row.Parent));
        ASSERT_NE(member->Type, nullptr);
        EXPECT_EQ(member->Type->Name(), entry.Row.Type.Name);
        EXPECT_EQ(member->Type->Namespace(), entry.Row.Type.Namespace);
    }
    EXPECT_EQ(membersResolved, 267);

    EXPECT_EQ(known.Strings(1), "Name");
    EXPECT_EQ(known.Strings(2), "Uid");
    EXPECT_EQ(known.Resources(1).Item1, "SystemColors");
    EXPECT_EQ(known.Resources(235).Item3, "InactiveSelectionHighlightTextBrush");
}

// KnownMember resolves the property through GetProperties with the member's
// own name and IgnoreInheritedMembers: the single declared match.
TEST(KnownThingsTest, KnownMemberResolvesSingleOwnProperty)
{
    KnownFixture fixture;
    auto accessText = fixture.MakeType("System.Windows.Controls", "AccessText");
    auto stringType = fixture.MakeType("System", "String");
    auto textProperty = std::make_shared<TestSupport::LookupProperty>(
        "Text", stringType, fixture.Compilation());
    accessText->SetProperties({ textProperty.get() });

    KB::KnownMember member(KB::KnownTypes::AccessText, accessText.get(), "Text",
                            stringType.get());
    EXPECT_EQ(member.Parent, KB::KnownTypes::AccessText);
    EXPECT_EQ(member.DeclaringType, accessText.get());
    EXPECT_EQ(member.Property, textProperty.get());
    EXPECT_EQ(member.Name, "Text");
    EXPECT_EQ(member.Type, stringType.get());
}

// The IgnoreInheritedMembers flag is load-bearing: a property declared only on
// a BASE type is NOT resolved (without the flag the GetMembersHelper base-type
// walk would find it).
TEST(KnownThingsTest, KnownMemberIgnoresInheritedProperties)
{
    KnownFixture fixture;
    auto base = fixture.MakeType("System.Windows.Controls.Primitives", "ButtonBase");
    auto derived = fixture.MakeType("System.Windows.Controls", "Button");
    derived->AddDirectBaseType(base);
    auto stringType = fixture.MakeType("System", "String");
    auto commandProperty = std::make_shared<TestSupport::LookupProperty>(
        "Command", stringType, fixture.Compilation());
    base->SetProperties({ commandProperty.get() });

    KB::KnownMember member(KB::KnownTypes::ButtonBase, derived.get(), "Command",
                            stringType.get());
    EXPECT_EQ(member.Property, nullptr);
}

// The SingleOrDefault more-than-one arm (gold-pinned .NET message).
TEST(KnownThingsTest, KnownMemberThrowsOnAmbiguousProperties)
{
    KnownFixture fixture;
    auto accessText = fixture.MakeType("System.Windows.Controls", "AccessText");
    auto stringType = fixture.MakeType("System", "String");
    auto first = std::make_shared<TestSupport::LookupProperty>(
        "Text", stringType, fixture.Compilation());
    auto second = std::make_shared<TestSupport::LookupProperty>(
        "Text", stringType, fixture.Compilation());
    accessText->SetProperties({ first.get(), second.get() });

    try {
        KB::KnownMember member(KB::KnownTypes::AccessText, accessText.get(), "Text",
                               stringType.get());
        FAIL() << "expected the SingleOrDefault more-than-one throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Sequence contains more than one element.");
    }
}

// A null declaring type (the parent's row resolved to null) is the C#
// NullReferenceException on declType.GetProperties -- the standard message.
TEST(KnownThingsTest, KnownMemberThrowsOnNullDeclaringType)
{
    KnownFixture fixture;
    auto stringType = fixture.MakeType("System", "String");
    try {
        KB::KnownMember member(KB::KnownTypes::AccessText, nullptr, "Text",
                               stringType.get());
        FAIL() << "expected the null-declaring-type throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// The C# dictionary indexer's KeyNotFoundException for an id with no row --
// the port's std::out_of_range (KnownTypes.Unknown = 0, the KnownMembers 137
// hole, the strings past 2, the resources at the id-61 hole).
TEST(KnownThingsTest, AccessorsThrowForUnseededIds)
{
    KnownFixture fixture;
    fixture.AddAllSix();
    KB::KnownThings known(fixture.Compilation());

    EXPECT_THROW(known.Types(KB::KnownTypes::Unknown), std::out_of_range);
    EXPECT_THROW(known.Members(static_cast<KB::KnownMembers>(137)), std::out_of_range);
    EXPECT_THROW(known.Strings(3), std::out_of_range);
    EXPECT_THROW(known.Resources(61), std::out_of_range);
}

} // namespace
