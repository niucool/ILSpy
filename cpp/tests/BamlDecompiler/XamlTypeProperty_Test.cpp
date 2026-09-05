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

// Tests for the XamlType port (ICSharpCode.BamlDecompiler/Xaml/XamlType.cs)
// and the XamlProperty port (Xaml/XamlProperty.cs) -- every expectation is
// gold-pinned against the REAL internal classes from the installed
// ICSharpCode.BamlDecompiler.dll, driven through the
// C:/temp-probe/XamlContextProbe reflection probe over the identical fixture
// shape (the XamlContextFixture) and real System.Xml.Linq elements:
//  * ResolveNamespace's arms in order: the already-set early-out, the
//    PIMapping table, the assembly's XmlnsDefinitionAttribute rows (the
//    PresentationCore synthetic), and the clr-namespace fallback -- whose
//    prefix generation the gold pins exactly: the last dot-segment
//    lowercased ("SEG" -> the "seg" prefix while the URI keeps "SEG"), the
//    global/empty prefix split, the collision loop renaming seg -> seg1 ONLY
//    when the in-scope prefix binds a DIFFERENT namespace (a same-namespace
//    collision adds no attribute), the main-module comparison dropping the
//    ";assembly=" tail, and the AddBeforeSelf comment for the empty
//    namespace;
//  * ToXName's no-namespace arm (the plain None-namespace name) and the
//    namespaced arm;
//  * TryResolve's four arms in order (the instance property, the
//    "<name>Property" field, the event, the "<name>Event" field), the
//    already-resolved early-out, the null-ResolvedType NRE (the port's
//    std::runtime_error with the standard message), and the
//    null-GetDefinition give-up;
//  * IsAttachedTo's walk: the null short-circuits (true), the declaring-type
//    match on the target itself and through a base link (false), and the
//    no-chain miss (true);
//  * ToXName's full/short forms with and without a parent whose default
//    namespace matches.

#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include "BamlTestSupport.hpp"

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Xml = ILSpy::Decompiler::Xml;
using ILSpy::Tests::Baml::XamlContextFixture;
using ILSpy::Tests::Baml::kPresentationFrameworkFullName;
using ILSpy::BamlDecompiler::Xaml::XamlProperty;
using ILSpy::BamlDecompiler::Xaml::XamlType;

// A parented holder element (ResolveNamespace's comment arm calls
// AddBeforeSelf, which throws for an unparented element -- the probe's Holder
// shape).
Xml::XElement* AddHolder(Xml::XDocument& doc, Xml::XElement& root, const char* name)
{
    auto holder = std::make_shared<Xml::XElement>(Xml::XName::Get(name));
    root.Add(holder);
    return holder.get();
}

TEST(XamlTypeTest, CtorChainsAndToString)
{
    // The 4-arg ctor chains with a null xmlns (Namespace nullopt until
    // ResolveNamespace runs).
    XamlType fourArgs(nullptr, "Asm, Version=1.0.0.0", "Ns", "Type");
    EXPECT_EQ(fourArgs.Assembly, nullptr);
    EXPECT_EQ(fourArgs.FullAssemblyName, "Asm, Version=1.0.0.0");
    EXPECT_EQ(fourArgs.TypeNamespace, "Ns");
    EXPECT_EQ(fourArgs.TypeName, "Type");
    EXPECT_FALSE(fourArgs.Namespace().has_value());
    EXPECT_EQ(fourArgs.ResolvedType, nullptr);
    EXPECT_EQ(fourArgs.ToString(), "Type");

    XamlType fiveArgs(nullptr, "Asm, Version=1.0.0.0", "Ns", "Type",
        Xml::XNamespace::Get("http://t"));
    ASSERT_TRUE(fiveArgs.Namespace().has_value());
    EXPECT_EQ(fiveArgs.Namespace()->NamespaceName(), "http://t");
}

TEST(XamlTypeTest, ToXNameBothArms)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold: the null-namespace arm renders the plain (None-namespace) name.
    XamlType nullNs(nullptr, "Asm, Version=1.0.0.0", "Ns", "MyType");
    Xml::XName plain = nullNs.ToXName(*ctx);
    EXPECT_EQ(plain.ToString(), "MyType");
    EXPECT_EQ(plain.NamespaceName(), "");

    // Gold: the namespaced arm.
    XamlType withNs(nullptr, "Asm, Version=1.0.0.0", "Ns", "MyType",
        Xml::XNamespace::Get("http://t"));
    EXPECT_EQ(withNs.ToXName(*ctx).ToString(), "{http://t}MyType");
}

TEST(XamlTypeResolveNamespaceTest, EarlyOutKeepsPresetNamespace)
{
    // Gold (arm A): the preset xmlns is kept and the element is untouched.
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    auto doc = std::make_shared<Xml::XDocument>();
    auto root = std::make_shared<Xml::XElement>(Xml::XName::Get("root"));
    doc->Add(root);
    Xml::XElement* holder = AddHolder(*doc, *root, "a");

    XamlType type(nullptr, kPresentationFrameworkFullName, "System.Windows.Controls",
        "ToolBar", Xml::XNamespace::Get("http://preset"));
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "http://preset");
    EXPECT_FALSE(holder->HasAttributes());
}

TEST(XamlTypeResolveNamespaceTest, PIMappingTableArm)
{
    // Gold (arm B): the PI-table hit resolves the namespace and touches no
    // attribute.
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    auto doc = std::make_shared<Xml::XDocument>();
    auto root = std::make_shared<Xml::XElement>(Xml::XName::Get("root"));
    doc->Add(root);
    Xml::XElement* holder = AddHolder(*doc, *root, "b");

    XamlType type(nullptr, kPresentationFrameworkFullName, "System.Windows.Controls",
        "Button");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), ILSpy::Tests::Baml::kProbePiNs);
    EXPECT_FALSE(holder->HasAttributes());
}

TEST(XamlTypeResolveNamespaceTest, XmlnsDefinitionAttributeArm)
{
    // Gold (arm C): the assembly's XmlnsDefinitionAttribute rows (the
    // PresentationCore synthetic, seeded with System.Windows) resolve through
    // TryGetXmlNamespace.
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    auto doc = std::make_shared<Xml::XDocument>();
    auto root = std::make_shared<Xml::XElement>(Xml::XName::Get("root"));
    doc->Add(root);
    Xml::XElement* holder = AddHolder(*doc, *root, "c");

    XamlType type(fixture.PresentationCore(),
        ILSpy::Tests::Baml::kPresentationCoreFullName, "System.Windows", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), ILSpy::Tests::Baml::kPresentationXmlns);
    EXPECT_FALSE(holder->HasAttributes());
}

// The clr-namespace fallback arms (the probe's D/E/F series) over fresh
// holder elements.
class XamlTypeResolveNamespaceFallbackTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ctx = fixture.MakeContext();
        doc = std::make_shared<Xml::XDocument>();
        root = std::make_shared<Xml::XElement>(Xml::XName::Get("root"));
        doc->Add(root);
    }

    Xml::XElement* FreshHolder(const char* name)
    {
        return AddHolder(*doc, *root, name);
    }

    XamlContextFixture fixture;
    std::unique_ptr<ILSpy::BamlDecompiler::XamlContext> ctx;
    std::shared_ptr<Xml::XDocument> doc;
    std::shared_ptr<Xml::XElement> root;
};

TEST_F(XamlTypeResolveNamespaceFallbackTest, DottedNamespaceFreshPrefix)
{
    // Gold (D1): "clr-namespace:My.Nested.Seg;assembly=SomeAssembly", the
    // xmlns:seg attribute (the last segment lowercased).
    Xml::XElement* holder = FreshHolder("d");
    XamlType type(nullptr, "SomeAssembly, Version=1.0.0.0", "My.Nested.Seg", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "clr-namespace:My.Nested.Seg;assembly=SomeAssembly");
    ASSERT_TRUE(holder->HasAttributes());
    EXPECT_EQ(holder->ToString(),
        "<d xmlns:seg=\"clr-namespace:My.Nested.Seg;assembly=SomeAssembly\" />");
}

TEST_F(XamlTypeResolveNamespaceFallbackTest, PrefixCollisionWithDifferentNamespace)
{
    // Gold (D2): the in-scope "seg" binds http://other.ns, so the generated
    // prefix becomes seg1 and the new attribute is appended.
    auto holder = std::make_shared<Xml::XElement>(Xml::XName::Get("d2"),
        std::make_shared<Xml::XAttribute>(
            Xml::XNamespace::Xmlns() + "seg", std::string("http://other.ns")));
    root->Add(holder);
    XamlType type(nullptr, "SomeAssembly, Version=1.0.0.0", "My.Nested.Seg", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "clr-namespace:My.Nested.Seg;assembly=SomeAssembly");
    EXPECT_EQ(holder->ToString(),
        "<d2 xmlns:seg=\"http://other.ns\" "
        "xmlns:seg1=\"clr-namespace:My.Nested.Seg;assembly=SomeAssembly\" />");
}

TEST_F(XamlTypeResolveNamespaceFallbackTest, PrefixCollisionWithSameNamespace)
{
    // Gold (D3): the in-scope "seg" already binds the generated clr-namespace,
    // so NO attribute is added.
    auto holder = std::make_shared<Xml::XElement>(Xml::XName::Get("d3"),
        std::make_shared<Xml::XAttribute>(
            Xml::XNamespace::Xmlns() + "seg",
            std::string("clr-namespace:My.Nested.Seg;assembly=SomeAssembly")));
    root->Add(holder);
    XamlType type(nullptr, "SomeAssembly, Version=1.0.0.0", "My.Nested.Seg", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "clr-namespace:My.Nested.Seg;assembly=SomeAssembly");
    EXPECT_EQ(holder->ToString(),
        "<d3 xmlns:seg=\"clr-namespace:My.Nested.Seg;assembly=SomeAssembly\" />");
}

TEST_F(XamlTypeResolveNamespaceFallbackTest, NamespaceWithoutDot)
{
    // Gold (D4): the whole namespace is the prefix.
    Xml::XElement* holder = FreshHolder("d4");
    XamlType type(nullptr, "SomeAssembly, Version=1.0.0.0", "Custom", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(holder->ToString(),
        "<d4 xmlns:custom=\"clr-namespace:Custom;assembly=SomeAssembly\" />");
}

TEST_F(XamlTypeResolveNamespaceFallbackTest, EmptyNamespaceGlobalComment)
{
    // Gold (D5): the empty namespace takes the "global" prefix and the
    // AddBeforeSelf comment lands in front of the element.
    Xml::XElement* holder = FreshHolder("d5");
    XamlType type(nullptr, "SomeAssembly, Version=1.0.0.0", "", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "clr-namespace:;assembly=SomeAssembly");
    EXPECT_EQ(holder->ToString(),
        "<d5 xmlns:global=\"clr-namespace:;assembly=SomeAssembly\" />");
    auto* comment = dynamic_cast<Xml::XComment*>(holder->PreviousNode());
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->Value(), "'global' is prefix for the global namespace");
}

TEST_F(XamlTypeResolveNamespaceFallbackTest, MainModuleArmDropsAssemblyTail)
{
    // Gold (arm E): the main module's full name drops the ";assembly=" tail
    // (the fixture's stub main module is named "mscorlib").
    Xml::XElement* holder = FreshHolder("e");
    XamlType type(nullptr, "mscorlib", "Probe.MainNs", "App");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "clr-namespace:Probe.MainNs");
    EXPECT_EQ(holder->ToString(), "<e xmlns:mainns=\"clr-namespace:Probe.MainNs\" />");
}

TEST_F(XamlTypeResolveNamespaceFallbackTest, UppercaseSegmentLowercasedPrefixOnly)
{
    // Gold (arm F): the ToLowerInvariant prefix lowercases the last segment
    // while the clr-namespace URI keeps the original casing.
    Xml::XElement* holder = FreshHolder("f");
    XamlType type(nullptr, "SomeAssembly, Version=1.0.0.0", "My.Nested.SEG", "Foo");
    type.ResolveNamespace(*holder, *ctx);
    ASSERT_TRUE(type.Namespace().has_value());
    EXPECT_EQ(type.Namespace()->NamespaceName(), "clr-namespace:My.Nested.SEG;assembly=SomeAssembly");
    EXPECT_EQ(holder->ToString(),
        "<f xmlns:seg=\"clr-namespace:My.Nested.SEG;assembly=SomeAssembly\" />");
}

// ===== XamlProperty ==========================================================

class XamlPropertyTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ctx = fixture.MakeContext();
        hostType = std::make_unique<XamlType>(nullptr, "Asm, Version=1.0.0.0", "Ns", "Host");
        host = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "Ns.Host", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "Host", 0)), TS::TypeKind::Class,
            TS::Accessibility::Public, fixture.Compilation(), nullptr);
        hostType->ResolvedType = host;
    }

    XamlContextFixture fixture;
    std::unique_ptr<ILSpy::BamlDecompiler::XamlContext> ctx;
    std::unique_ptr<XamlType> hostType;
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> host;
};

TEST_F(XamlPropertyTest, CtorAndToString)
{
    XamlProperty property(hostType.get(), "Width");
    EXPECT_EQ(property.DeclaringType, hostType.get());
    EXPECT_EQ(property.PropertyName, "Width");
    EXPECT_EQ(property.ResolvedMember, nullptr);
    EXPECT_EQ(property.ToString(), "Width");
}

TEST_F(XamlPropertyTest, TryResolvePropertyArm)
{
    TS::TestSupport::LookupProperty widthProperty("Width", host, fixture.Compilation());
    host->SetProperties({ &widthProperty });
    XamlProperty property(hostType.get(), "Width");
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, &widthProperty);
}

TEST_F(XamlPropertyTest, TryResolveStaticPropertyFieldArm)
{
    TS::TestSupport::LookupField widthPropertyField("WidthProperty", host, fixture.Compilation());
    host->SetFields({ &widthPropertyField });
    XamlProperty property(hostType.get(), "Width");
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, &widthPropertyField);
}

TEST_F(XamlPropertyTest, TryResolveEventArm)
{
    TS::TestSupport::LookupEvent clickEvent("Width", host, fixture.Compilation());
    host->SetEvents({ &clickEvent });
    XamlProperty property(hostType.get(), "Width");
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, &clickEvent);
}

TEST_F(XamlPropertyTest, TryResolveEventFieldArm)
{
    TS::TestSupport::LookupField widthEventField("WidthEvent", host, fixture.Compilation());
    host->SetFields({ &widthEventField });
    XamlProperty property(hostType.get(), "Width");
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, &widthEventField);
}

TEST_F(XamlPropertyTest, TryResolveMissStaysNullAndEarlyOutHolds)
{
    XamlProperty property(hostType.get(), "Nonexistent");
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, nullptr);

    // The already-resolved early-out: TryResolve never re-runs the walk.
    TS::TestSupport::LookupEvent clickEvent("Click", host, fixture.Compilation());
    property.ResolvedMember = &clickEvent;
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, &clickEvent);
}

TEST_F(XamlPropertyTest, TryResolveNullResolvedTypeIsTheNre)
{
    // The C# `DeclaringType.ResolvedType.GetDefinition()` over a null
    // ResolvedType is the null-receiver NRE (the port's std::runtime_error
    // with the standard message).
    XamlType noResolution(nullptr, "Asm, Version=1.0.0.0", "Ns", "Host");
    XamlProperty property(&noResolution, "Width");
    try {
        property.TryResolve();
        FAIL() << "expected the NullReferenceException mapping";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(),
            "Object reference not set to an instance of an object.");
    }
}

TEST_F(XamlPropertyTest, TryResolveNullDefinitionGivesUp)
{
    // The C# `typeDef == null` return: an IType with no definition (the
    // port's UnknownType) leaves the member unresolved.
    // The UnknownType class/function name collision (the KnownTypeCache
    // elaborated-type-specifier note): the colliding free function hides
    // the class name in template arguments, so the alias spells the class.
    using UnknownTypeClass = class TS::UnknownType;
    hostType->ResolvedType = std::make_shared<UnknownTypeClass>(
        std::optional<std::string>("Ns"), "U", 0);
    XamlProperty property(hostType.get(), "Width");
    property.TryResolve();
    EXPECT_EQ(property.ResolvedMember, nullptr);
}

TEST_F(XamlPropertyTest, IsAttachedToWalk)
{
    // A member whose declaring type is Ns.Base (the SetDeclaringType stub
    // arm) walked against target types.
    auto baseType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Ns.Base", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "Base", 0)), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.Compilation(), nullptr);
    TS::TestSupport::LookupMember member("Width", TS::SymbolKind::Property, baseType,
        fixture.Compilation());
    member.SetDeclaringType(baseType);

    XamlType baseXaml(nullptr, "Asm, Version=1.0.0.0", "Ns", "Base");
    baseXaml.ResolvedType = baseType;
    XamlProperty property(hostType.get(), "Width");
    property.ResolvedMember = &member;

    // The null short-circuits (all true).
    EXPECT_TRUE(property.IsAttachedTo(nullptr));
    XamlProperty unresolved(hostType.get(), "Other");
    EXPECT_TRUE(unresolved.IsAttachedTo(&baseXaml));
    XamlType noResolvedType(nullptr, "Asm, Version=1.0.0.0", "Ns", "Base");
    EXPECT_TRUE(unresolved.IsAttachedTo(&noResolvedType));

    // The declaring-type match on the target itself (gold: False).
    EXPECT_FALSE(property.IsAttachedTo(&baseXaml));

    // The match found through a base link (gold: False).
    auto derived = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "Ns.Derived", "Ns", TS::FullTypeName(TS::TopLevelTypeName("Ns", "Derived", 0)), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.Compilation(), nullptr);
    derived->AddDirectBaseType(baseType);
    XamlType derivedXaml(nullptr, "Asm, Version=1.0.0.0", "Ns", "Derived");
    derivedXaml.ResolvedType = derived;
    EXPECT_FALSE(property.IsAttachedTo(&derivedXaml));

    // The no-chain miss (gold: True).
    EXPECT_TRUE(property.IsAttachedTo(hostType.get()));
}

TEST_F(XamlPropertyTest, IsAttachedToNullDeclaringTypeIsTheNre)
{
    // The C# `declType.FullName` over a member with no declaring type is the
    // null-receiver NRE.
    TS::TestSupport::LookupMember member("Width", TS::SymbolKind::Property, host,
        fixture.Compilation());
    XamlProperty property(hostType.get(), "Width");
    property.ResolvedMember = &member;
    EXPECT_THROW(property.IsAttachedTo(hostType.get()), std::runtime_error);
}

TEST_F(XamlPropertyTest, ToXNameForms)
{
    // Gold: the null-namespace declaring type over a None-default-namespace
    // parent renders the plain "Host.Width".
    XamlProperty plain(hostType.get(), "Width");
    Xml::XElement parent(Xml::XName::Get("p"));
    EXPECT_EQ(plain.ToXName(*ctx, &parent).ToString(), "Host.Width");
    EXPECT_EQ(plain.ToXName(*ctx, nullptr).ToString(), "Host.Width");

    // Gold: the namespaced declaring type without a parent ->
    // "{http://t}Host.Width"; with a matching-default parent -> "Host.Width";
    // with a non-matching parent -> the qualified name again.
    XamlType namespaced(nullptr, "Asm, Version=1.0.0.0", "Ns", "Host",
        Xml::XNamespace::Get("http://t"));
    XamlProperty qualified(&namespaced, "Width");
    EXPECT_EQ(qualified.ToXName(*ctx, nullptr).ToString(), "{http://t}Host.Width");

    Xml::XElement matching(Xml::XName::Get("m"),
        std::make_shared<Xml::XAttribute>(Xml::XName::Get("xmlns", ""),
            std::string("http://t")));
    EXPECT_EQ(qualified.ToXName(*ctx, &matching).ToString(), "Host.Width");

    Xml::XElement nonMatching(Xml::XName::Get("m"));
    EXPECT_EQ(qualified.ToXName(*ctx, &nonMatching).ToString(), "{http://t}Host.Width");

    // Gold: the short form (isFullName false) is the bare property name.
    EXPECT_EQ(qualified.ToXName(*ctx, nullptr, false).ToString(), "Width");
}

} // namespace
