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

// Tests for the XamlContext port (ICSharpCode.BamlDecompiler/XamlContext.cs)
// plus the BamlElement/XamlNode value types (BamlElement.cs) and the
// BamlDecompilerSettings bag -- every XamlContext expectation is gold-pinned
// against the REAL internal XamlContext from the installed
// ICSharpCode.BamlDecompiler.dll, driven through the
// C:/temp-probe/XamlContextProbe reflection probe over the identical fixture
// shape (the XamlContextFixture: the six-module KnownThings compilation, the
// walk document with its PIMapping, and the PresentationCore synthetic
// carrying the XmlnsDefinitionAttribute rows):
//  * Construct: the RootNode over the DocumentStart block and the two-row
//    NodeMap (DocumentStartRecord/ElementStartRecord -> BamlBlockNode), the
//    default settings (ThrowOnAssemblyResolveErrors true), and the
//    passed-settings instance kept by pointer identity;
//  * ResolveString: the two known strings (0xFFFF/0xFFFE -> "Name"/"Uid" --
//    the (ushort)(-index) wire arithmetic), the StringIdMap hit, the missing
//    id's null (nullopt), and the out-of-table known id's throw;
//  * ResolveType: the known arm's fields (the ToolBar row 0xFD63 and the
//    AccessText row 0xFFFF, both hitting the PIMapping table -- gold: the PI
//    table is the ONLY lookup ResolveType performs, so a PresentationCore/
//    System.Windows type like ContentElement resolves with a NULL namespace),
//    the per-id cache (the same instance), the out-of-table and
//    missing-record throws, and the BAML-record arm's real resolution through
//    ReflectionHelper.ParseReflectionName (the synthetic module seeded by the
//    KnownThings ctor answers the parser's module walk -- the real-engine
//    gold: the PI xmlns attaches and ResolvedType is the synthetic Button);
//  * ResolveProperty: the AttributeInfoRecord arm (the declaring type routed
//    through the same cached ResolveType instance, the synthetic type's empty
//    member tables leaving ResolvedMember null) and the known-members arm
//    (AccessText.Text / XmlDataProvider.XmlSerializer, both member-null over
//    the synthetic fixture);
//  * GetXmlNamespace (the value-equality stand-in for the C# atomization --
//    the documented XNamespace divergence), TryGetXmlNamespace (the
//    null-module, PresentationCore hit, seeded-namespace, miss, and
//    null-xmlns-module arms), GetKnownNamespace (the matching-default-namespace
//    context collapsing to the plain name), and GetPseudoName.

#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include "BamlTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ILSpy::Tests::Baml::XamlContextFixture;
using ILSpy::Tests::Baml::kPresentationCoreFullName;
using ILSpy::Tests::Baml::kPresentationXmlns;
using ILSpy::Tests::Baml::kPresentationFrameworkFullName;
using ILSpy::Tests::Baml::kProbePiNs;
using ILSpy::BamlDecompiler::BamlDecompilerSettings;
using ILSpy::BamlDecompiler::BamlElement;
using ILSpy::BamlDecompiler::XamlContext;
using ILSpy::BamlDecompiler::XamlNode;

// ===== BamlDecompilerSettings ================================================

TEST(BamlDecompilerSettingsTest, DefaultsTrueAndSettable)
{
    BamlDecompilerSettings settings;
    EXPECT_TRUE(settings.ThrowOnAssemblyResolveErrors());
    settings.SetThrowOnAssemblyResolveErrors(false);
    EXPECT_FALSE(settings.ThrowOnAssemblyResolveErrors());
}

// ===== XamlNode (the readonly struct's element/string arms) =================

TEST(XamlNodeTest, ElementAndStringArms)
{
    auto element = std::make_shared<ILSpy::Decompiler::Xml::XElement>("e");
    XamlNode fromElement(element);
    EXPECT_EQ(fromElement.Element, element);
    EXPECT_FALSE(fromElement.String.has_value());

    XamlNode fromString(std::string("text"));
    EXPECT_EQ(fromString.Element, nullptr);
    ASSERT_TRUE(fromString.String.has_value());
    EXPECT_EQ(*fromString.String, "text");

    // The string-literal arm (the C# implicit string conversion).
    XamlNode fromLiteral("literal");
    ASSERT_TRUE(fromLiteral.String.has_value());
    EXPECT_EQ(*fromLiteral.String, "literal");
}

TEST(XamlNodeTest, ConversionsAndDefaults)
{
    // The C# implicit operator XElement / operator string: the conversion
    // operators hand back the field (both possibly null).
    auto element = std::make_shared<ILSpy::Decompiler::Xml::XElement>("e");
    XamlNode node(element);
    std::shared_ptr<ILSpy::Decompiler::Xml::XElement> roundTrip = node;
    EXPECT_EQ(roundTrip, element);

    std::optional<std::string> asString = XamlNode(std::string("s"));
    EXPECT_EQ(asString, std::optional<std::string>("s"));

    // The default instance: both fields null (the C# default struct).
    XamlNode empty;
    EXPECT_EQ(empty.Element, nullptr);
    EXPECT_FALSE(empty.String.has_value());
    std::optional<std::string> nullString = empty;
    EXPECT_FALSE(nullString.has_value());
}

// ===== BamlElement ===========================================================

TEST(BamlElementTest, FieldsAndChildren)
{
    BamlElement element(nullptr);
    EXPECT_EQ(element.Node, nullptr);
    EXPECT_EQ(element.Parent, nullptr);
    EXPECT_TRUE(element.Children.empty());

    // The Xaml slot takes either arm (the C# implicit XamlNode conversions).
    auto xamlElement = std::make_shared<ILSpy::Decompiler::Xml::XElement>("x");
    element.Xaml = xamlElement;
    EXPECT_EQ(element.Xaml.Element, xamlElement);
    element.Xaml = std::string("text");
    ASSERT_TRUE(element.Xaml.String.has_value());
    EXPECT_EQ(*element.Xaml.String, "text");

    // AddChild appends without touching Parent (the C# Children.Add leaves
    // the back-pointer for the handlers to assign by hand).
    auto child = std::make_unique<BamlElement>(nullptr);
    BamlElement* childPtr = child.get();
    element.AddChild(std::move(child));
    ASSERT_EQ(element.Children.size(), 1u);
    EXPECT_EQ(element.Children[0].get(), childPtr);
    EXPECT_EQ(element.Parent, nullptr);
    EXPECT_EQ(childPtr->Parent, nullptr);

    // The hand-set back-pointer (the ElementHandler keyElem.Parent = doc arm).
    childPtr->Parent = &element;
    EXPECT_EQ(childPtr->Parent, &element);
}

// ===== XamlContext.Construct =================================================

TEST(XamlContextConstructTest, BuildsRootNodeNodeMapAndSettings)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    ASSERT_NE(ctx->RootNode(), nullptr);
    EXPECT_EQ(ctx->RootNode()->Type(), ILSpy::BamlDecompiler::Baml::BamlRecordType::DocumentStart);

    // The NodeMap: one row per block header (the DocumentStart root and the
    // ElementStart child), each value a BamlBlockNode (gold: count=2, both
    // record types present).
    const auto& nodeMap = ctx->NodeMap();
    ASSERT_EQ(nodeMap.size(), 2u);
    std::vector<ILSpy::BamlDecompiler::Baml::BamlRecordType> keyTypes;
    for (const auto& [record, block] : nodeMap) {
        keyTypes.push_back(record->Type());
        EXPECT_NE(block, nullptr);
    }
    ASSERT_EQ(keyTypes.size(), 2u);
    EXPECT_NE(std::find(keyTypes.begin(), keyTypes.end(),
                  ILSpy::BamlDecompiler::Baml::BamlRecordType::DocumentStart),
        keyTypes.end());
    EXPECT_NE(std::find(keyTypes.begin(), keyTypes.end(),
                  ILSpy::BamlDecompiler::Baml::BamlRecordType::ElementStart),
        keyTypes.end());
    // The map's value for the root block is the RootNode itself.
    EXPECT_NE(nodeMap.find(ctx->RootNode()->Header), nodeMap.end());

    // The default settings (gold: ThrowOnAssemblyResolveErrors True).
    EXPECT_TRUE(ctx->Settings().ThrowOnAssemblyResolveErrors());
}

TEST(XamlContextConstructTest, PassedSettingsKeptByPointer)
{
    XamlContextFixture fixture;
    BamlDecompilerSettings passed;
    passed.SetThrowOnAssemblyResolveErrors(false);
    auto ctx = fixture.MakeContext(&passed);
    EXPECT_EQ(&ctx->Settings(), &passed);
    EXPECT_FALSE(ctx->Settings().ThrowOnAssemblyResolveErrors());
}

// ===== XamlContext.ResolveString =============================================

TEST(XamlContextResolveStringTest, KnownAndRecordArms)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // The known strings (gold: 0xFFFF -> "Name", 0xFFFE -> "Uid" -- the
    // (ushort)(-index) wire form).
    auto known1 = ctx->ResolveString(0xFFFF);
    ASSERT_TRUE(known1.has_value());
    EXPECT_EQ(*known1, "Name");
    auto known2 = ctx->ResolveString(0xFFFE);
    ASSERT_TRUE(known2.has_value());
    EXPECT_EQ(*known2, "Uid");

    // The StringIdMap hit.
    auto fromRecord = ctx->ResolveString(0);
    ASSERT_TRUE(fromRecord.has_value());
    EXPECT_EQ(*fromRecord, "s0");

    // The missing id (gold: null).
    EXPECT_FALSE(ctx->ResolveString(5).has_value());

    // The out-of-table known id (gold: KeyNotFoundException '-32768'; the
    // port's .at() convention).
    EXPECT_THROW(ctx->ResolveString(0x8000), std::out_of_range);
}

// ===== XamlContext.ResolveType ===============================================

TEST(XamlContextResolveTypeTest, KnownArmResolvesThroughPIMapping)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold (id=0xFD63): the ToolBar row over the PresentationFramework
    // synthetic, the PI-table namespace hit.
    ILSpy::BamlDecompiler::Xaml::XamlType* toolBar = ctx->ResolveType(0xFD63);
    ASSERT_NE(toolBar, nullptr);
    EXPECT_EQ(toolBar->Assembly, fixture.PresentationFramework());
    EXPECT_EQ(toolBar->FullAssemblyName, kPresentationFrameworkFullName);
    EXPECT_EQ(toolBar->TypeNamespace, "System.Windows.Controls");
    EXPECT_EQ(toolBar->TypeName, "ToolBar");
    ASSERT_TRUE(toolBar->Namespace().has_value());
    EXPECT_EQ(toolBar->Namespace()->NamespaceName(), kProbePiNs);
    ASSERT_NE(toolBar->ResolvedType, nullptr);
    EXPECT_EQ(toolBar->ResolvedType->Name(), "ToolBar");
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* resolved =
        toolBar->ResolvedType->GetDefinition();
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->Namespace(), "System.Windows.Controls");

    // The cache: the same instance (gold: cached=True).
    EXPECT_EQ(ctx->ResolveType(0xFD63), toolBar);

    // Gold (id=0xFFFF): the AccessText row, the same PI-table hit.
    ILSpy::BamlDecompiler::Xaml::XamlType* accessText = ctx->ResolveType(0xFFFF);
    ASSERT_NE(accessText, nullptr);
    EXPECT_EQ(accessText->TypeName, "AccessText");
    EXPECT_EQ(accessText->TypeNamespace, "System.Windows.Controls");
    EXPECT_EQ(accessText->FullAssemblyName, kPresentationFrameworkFullName);
    ASSERT_TRUE(accessText->Namespace().has_value());
    EXPECT_EQ(accessText->Namespace()->NamespaceName(), kProbePiNs);
}

TEST(XamlContextResolveTypeTest, KnownArmConsultsOnlyThePITable)
{
    // Gold (id=0xFF9B, the known ContentElement row): a type whose assembly
    // carries XmlnsDefinitionAttribute rows mapping its namespace still
    // resolves with a NULL namespace -- ResolveType consults the PIMapping
    // table only; the XmlnsDefinitionAttribute walk lives in
    // XamlType.ResolveNamespace.
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    ILSpy::BamlDecompiler::Xaml::XamlType* contentElement = ctx->ResolveType(0xFF9B);
    ASSERT_NE(contentElement, nullptr);
    EXPECT_EQ(contentElement->TypeName, "ContentElement");
    EXPECT_EQ(contentElement->TypeNamespace, "System.Windows");
    EXPECT_EQ(contentElement->FullAssemblyName, kPresentationCoreFullName);
    EXPECT_FALSE(contentElement->Namespace().has_value());
}

TEST(XamlContextResolveTypeTest, OutOfTableAndMissingRecordThrows)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold: the out-of-table known id (KeyNotFound '32767').
    EXPECT_THROW(ctx->ResolveType(0x8001), std::out_of_range);
    // Gold: the missing record id (KeyNotFound '5').
    EXPECT_THROW(ctx->ResolveType(5), std::out_of_range);
}

TEST(XamlContextResolveTypeTest, BamlRecordArmResolvesThroughParseReflectionName)
{
    // The BAML-record arm resolves the record's "System.Windows.Controls.Button"
    // through ReflectionHelper.ParseReflectionName over the compilation (the
    // iteration-49 lift of the former loud logic_error deferral). The KnownThings
    // ctor's RegisterType walk seeds the synthetic PresentationFramework module,
    // so the parser's plain module scan finds the synthetic Button definition
    // -- the real-engine gold: Assembly = the synthetic module, FullAssemblyName =
    // the record's raw name, TypeNamespace = "System.Windows.Controls",
    // TypeName = "Button", Namespace = the probe PI xmlns, ResolvedType = the
    // Button type.
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    ILSpy::BamlDecompiler::Xaml::XamlType* t = ctx->ResolveType(0);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->TypeNamespace, "System.Windows.Controls");
    EXPECT_EQ(t->TypeName, "Button");
    EXPECT_EQ(t->Assembly, fixture.PresentationFramework());
    EXPECT_EQ(t->FullAssemblyName,
              "PresentationFramework, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=31bf3856ad364e35");
    ASSERT_NE(t->Namespace(), std::nullopt);
    EXPECT_EQ(t->Namespace()->NamespaceName(), kProbePiNs);
    // The resolved type is the synthetic module's registered Button definition
    // (the parser's plain-walk hit -- a Class-kind type named "Button").
    ASSERT_NE(t->ResolvedType, nullptr);
    EXPECT_EQ(t->ResolvedType->Name(), "Button");
    EXPECT_EQ(t->ResolvedType->Kind(),
              ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    // The per-id cache hands back the same instance.
    EXPECT_EQ(ctx->ResolveType(0), t);
}

// ===== XamlContext.ResolveProperty ============================================

TEST(XamlContextResolvePropertyTest, RecordAndKnownArms)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold (id=0): the AttributeInfoRecord arm -- the declaring type routed
    // through the SAME cached ResolveType instance, the name from the record,
    // and the synthetic type's empty member tables leaving ResolvedMember
    // null after TryResolve.
    ILSpy::BamlDecompiler::Xaml::XamlProperty* width = ctx->ResolveProperty(0);
    ASSERT_NE(width, nullptr);
    EXPECT_EQ(width->DeclaringType, ctx->ResolveType(0xFD63));
    EXPECT_EQ(width->DeclaringType->TypeName, "ToolBar");
    EXPECT_EQ(width->PropertyName, "Width");
    EXPECT_EQ(width->ResolvedMember, nullptr);
    EXPECT_EQ(ctx->ResolveProperty(0), width);

    // Gold (id=0xFFFF): the known-members arm -- AccessText.Text.
    ILSpy::BamlDecompiler::Xaml::XamlProperty* text = ctx->ResolveProperty(0xFFFF);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->DeclaringType->TypeName, "AccessText");
    EXPECT_EQ(text->PropertyName, "Text");
    EXPECT_EQ(text->ResolvedMember, nullptr);

    // Gold (id=0xFEF4): the last known-members row -- XmlDataProvider.XmlSerializer.
    ILSpy::BamlDecompiler::Xaml::XamlProperty* serializer = ctx->ResolveProperty(0xFEF4);
    ASSERT_NE(serializer, nullptr);
    EXPECT_EQ(serializer->DeclaringType->TypeName, "XmlDataProvider");
    EXPECT_EQ(serializer->PropertyName, "XmlSerializer");
    EXPECT_EQ(serializer->ResolvedMember, nullptr);
}

// ===== The XML-namespace helpers =============================================

TEST(XamlContextXmlNamespaceTest, GetXmlNamespaceCachesByValue)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold: XNamespace.Get is atomized (ReferenceEquals True). The port's
    // XNamespace is a value type with structural equality (the documented
    // iteration-43 divergence), so the pin is the value-equality pair.
    auto first = ctx->GetXmlNamespace(std::optional<std::string>("http://x"));
    auto second = ctx->GetXmlNamespace(std::optional<std::string>("http://x"));
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
    EXPECT_EQ(first->NamespaceName(), "http://x");

    // The null input (gold: null-in <null>).
    EXPECT_FALSE(ctx->GetXmlNamespace(std::nullopt).has_value());
}

TEST(XamlContextXmlNamespaceTest, TryGetXmlNamespaceMatrix)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold: the null module -> null.
    EXPECT_FALSE(ctx->TryGetXmlNamespace(nullptr, "System.Windows").has_value());
    // Gold: the PresentationCore rows (the seeded System.Windows /
    // System.Windows.Media namespaces -> the presentation xmlns).
    auto hit = ctx->TryGetXmlNamespace(fixture.PresentationCore(), "System.Windows");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, kPresentationXmlns);
    auto hitMedia = ctx->TryGetXmlNamespace(fixture.PresentationCore(), "System.Windows.Media");
    ASSERT_TRUE(hitMedia.has_value());
    EXPECT_EQ(*hitMedia, kPresentationXmlns);
    // Gold: the unseeded namespace -> null.
    EXPECT_FALSE(
        ctx->TryGetXmlNamespace(fixture.PresentationCore(), "No.Such.Namespace").has_value());
    // Gold: the null-xmlns module (WindowsBase) -> null.
    EXPECT_FALSE(ctx->TryGetXmlNamespace(fixture.PresentationFramework(), "System.Windows")
                     .has_value());
}

TEST(XamlContextXmlNamespaceTest, GetKnownNamespaceShapes)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();

    // Gold: ("TypeExtension", the xaml namespace) ->
    // "{http://schemas.microsoft.com/winfx/2006/xaml}TypeExtension".
    ILSpy::Decompiler::Xml::XName xaml =
        ctx->GetKnownNamespace("TypeExtension", XamlContext::KnownNamespace_Xaml);
    EXPECT_EQ(xaml.ToString(),
        "{http://schemas.microsoft.com/winfx/2006/xaml}TypeExtension");
    EXPECT_EQ(xaml.NamespaceName(), "http://schemas.microsoft.com/winfx/2006/xaml");
    EXPECT_EQ(xaml.LocalName(), "TypeExtension");

    // Gold: a context element whose default namespace IS the target
    // collapses to the plain (no-namespace) name.
    ILSpy::Decompiler::Xml::XElement matching(
        ILSpy::Decompiler::Xml::XName::Get("w"),
        std::make_shared<ILSpy::Decompiler::Xml::XAttribute>(
            ILSpy::Decompiler::Xml::XName::Get("xmlns", ""),
            std::string("http://schemas.microsoft.com/winfx/2006/xaml")));
    ILSpy::Decompiler::Xml::XName plain =
        ctx->GetKnownNamespace("TypeExtension", XamlContext::KnownNamespace_Xaml, &matching);
    EXPECT_EQ(plain.ToString(), "TypeExtension");
    EXPECT_EQ(plain.NamespaceName(), "");

    // A context whose default namespace differs keeps the qualified name.
    ILSpy::Decompiler::Xml::XElement other(ILSpy::Decompiler::Xml::XName::Get("w"));
    ILSpy::Decompiler::Xml::XName qualified =
        ctx->GetKnownNamespace("TypeExtension", XamlContext::KnownNamespace_Xaml, &other);
    EXPECT_EQ(qualified.ToString(),
        "{http://schemas.microsoft.com/winfx/2006/xaml}TypeExtension");
}

TEST(XamlContextXmlNamespaceTest, GetPseudoName)
{
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    // Gold: "{https://github.com/icsharpcode/ILSpy}Ctor".
    ILSpy::Decompiler::Xml::XName pseudo = ctx->GetPseudoName("Ctor");
    EXPECT_EQ(pseudo.ToString(), "{https://github.com/icsharpcode/ILSpy}Ctor");
    EXPECT_EQ(pseudo.NamespaceName(), "https://github.com/icsharpcode/ILSpy");
}

TEST(XamlContextXmlNamespaceTest, MutableLists)
{
    // XClassNames / GeneratedMembers are the handlers' mutable lists.
    XamlContextFixture fixture;
    auto ctx = fixture.MakeContext();
    EXPECT_TRUE(ctx->XClassNames().empty());
    ctx->XClassNames().push_back("Probe.MainWindow");
    EXPECT_EQ(ctx->XClassNames().size(), 1u);
    EXPECT_EQ(ctx->XClassNames()[0], "Probe.MainWindow");
    EXPECT_TRUE(ctx->GeneratedMembers().empty());
    ctx->GeneratedMembers().push_back(0x06000001u);
    EXPECT_EQ(ctx->GeneratedMembers()[0], 0x06000001u);
}

} // namespace
