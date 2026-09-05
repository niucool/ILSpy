// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the first handler-port slice (cpp/BamlDecompiler/Handlers/ --
// Handlers/Records.hpp and Handlers/Blocks.hpp): the 17 CreateBuiltinHandlers
// manifest rows, and every handler's Translate behavior, gold-pinned against
// the REAL internal handlers from the installed ICSharpCode.BamlDecompiler.dll
// driven through reflection by the gold probe
// (C:/temp-probe/HandlerMapProbe/Program.cs, sections C and D of
// handlermap_gold.txt) over the identical walk fixture and crafted records:
//  * the ProcessChildren end-to-end drive (the probe's section C): the real
//    registry over the walk document renders the <Document> wrapper with the
//    PI-mapped ToolBar element and its text child, byte-exact;
//  * the DocumentHandler drive (D0 -- the XamlDecompiler.Decompose root
//    shape: a null parent, the root block, the pseudo-namespaced <Document>
//    element) with the BamlElement tree the probe dumped;
//  * the simple record handlers (D1-D6): the text/converter/id adds, the
//    BamlConnectionId annotation, and the two prefixed attributes;
//  * the null-returning handlers (D7): the nine info/mapping/line handlers
//    contribute nothing;
//  * the exception arms the probe pinned: the null-parent NRE (D9) and the
//    missing-string-id ArgumentNullException (D8).
//  * the section-E property-family drives (the HandlerMapProbe's E1-E16):
//    PropertyHandler's three arm shapes (attached / non-attached / x:Name)
//    over the mscorlib-backed fixture rows, the PropertyWithConverter
//    subclass, the four property-element blocks, ConstructorParametersStart,
//    and ConstructorParameterType's TypeExtension renders -- every parent
//    and element render byte-exact against the real handlers' drives.

#include "BamlTestSupport.hpp"
#include "BamlDecompiler/BamlConnectionId.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/Handlers/Records.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
namespace Handlers = ILSpy::BamlDecompiler::Handlers;
using ILSpy::BamlDecompiler::BamlConnectionId;
using ILSpy::BamlDecompiler::BamlElement;
using ILSpy::BamlDecompiler::HandlerMap;
using ILSpy::BamlDecompiler::IHandler;
using ILSpy::BamlDecompiler::XamlContext;
using ILSpy::Tests::Baml::XamlContextFixture;

// The gold render of the probe's section-C drive (the real registry's
// ProcessChildren over the walk document, dumped from the real engine).
const std::string kProcessChildrenGoldRender =
    "<Document>\r\n"
    "  <ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>\r\n"
    "</Document>";

// The gold render of the DocumentHandler drive (the probe's D0): the
// pseudo-namespaced <Document> wrapper (the default-namespace rebinding the
// XmlWriter emits for the ILSpy pseudo namespace).
const std::string kDocumentHandlerGoldRender =
    "<Document xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
    "  <ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>\r\n"
    "</Document>";

// The fixture the probe drives: the six-module KnownThings compilation and
// the walk document. The registry is NOT cleared -- the drive tests run the
// real manifest (the lazy population installs it at the first lookup; the
// HandlerMap registry tests' ClearHandlers resets only their own runs).
class HandlersTest : public ::testing::Test {
protected:
    XamlContextFixture fixture_;

    // The lookup the tests drive (asserts the manifest row exists).
    IHandler* Lookup(Baml::BamlRecordType type)
    {
        IHandler* handler = HandlerMap::LookupHandler(type);
        EXPECT_NE(handler, nullptr);
        return handler;
    }
};

// The manifest: the 17 ported rows, every record type distinct, each inside
// the gold registry inventory (the HandlerMapTest guard), with the class
// The manifest: the 25 ported rows, every record type distinct, each inside
// the gold registry inventory (the HandlerMapTest guard), with the class
// identities the manifest constructs.
TEST(HandlersManifestTest, CreateBuiltinHandlersHasTheTwentyFivePortedRows)
{
    std::vector<std::unique_ptr<IHandler>> handlers = HandlerMap::CreateBuiltinHandlers();
    ASSERT_EQ(handlers.size(), 25u);

    const Baml::BamlRecordType expected[] = {
        Baml::BamlRecordType::DocumentStart,
        Baml::BamlRecordType::ElementStart,
        Baml::BamlRecordType::Property,
        Baml::BamlRecordType::PropertyWithConverter,
        Baml::BamlRecordType::PropertyComplexStart,
        Baml::BamlRecordType::PropertyArrayStart,
        Baml::BamlRecordType::PropertyListStart,
        Baml::BamlRecordType::PropertyDictionaryStart,
        Baml::BamlRecordType::ConstructorParametersStart,
        Baml::BamlRecordType::ConstructorParameterType,
        Baml::BamlRecordType::Text,
        Baml::BamlRecordType::TextWithConverter,
        Baml::BamlRecordType::DefAttribute,
        Baml::BamlRecordType::PIMapping,
        Baml::BamlRecordType::AssemblyInfo,
        Baml::BamlRecordType::TypeInfo,
        Baml::BamlRecordType::TypeSerializerInfo,
        Baml::BamlRecordType::AttributeInfo,
        Baml::BamlRecordType::DeferableContentStart,
        Baml::BamlRecordType::ConnectionId,
        Baml::BamlRecordType::ContentProperty,
        Baml::BamlRecordType::TextWithId,
        Baml::BamlRecordType::PresentationOptionsAttribute,
        Baml::BamlRecordType::LineNumberAndPosition,
        Baml::BamlRecordType::LinePosition,
    };
    ASSERT_EQ(std::size(expected), 25u);
    for (Baml::BamlRecordType type : expected) {
        bool found = false;
        for (const auto& handler : handlers)
            found = found || handler->Type() == type;
        EXPECT_TRUE(found) << "manifest row missing for " << Baml::RecordTypeName(type);
    }
    // No duplicates (the C# reflection walk's Dictionary.Add invariant).
    for (std::size_t i = 0; i < handlers.size(); i++)
        for (std::size_t j = i + 1; j < handlers.size(); j++)
            EXPECT_NE(handlers[i]->Type(), handlers[j]->Type());

    // The class identities the rows construct (the dynamic_cast pins the
    // manifest's class, not just its record type).
    EXPECT_NE(HandlerMap::LookupHandler(Baml::BamlRecordType::DocumentStart), nullptr);
    EXPECT_NE(dynamic_cast<Handlers::DocumentHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::DocumentStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::ElementHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::ElementStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::TextHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::Text)),
        nullptr);
    // The TextWithConverter row is the TextHandler subclass (the C#
    // explicit-interface re-bind): it answers through the base cast too.
    IHandler* converter = HandlerMap::LookupHandler(Baml::BamlRecordType::TextWithConverter);
    ASSERT_NE(converter, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::TextWithConverterHandler*>(converter), nullptr);
    EXPECT_NE(dynamic_cast<Handlers::TextHandler*>(converter), nullptr);
    // The PropertyWithConverter row is the PropertyHandler subclass (the
    // same re-bind pattern).
    IHandler* propertyConverter =
        HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyWithConverter);
    ASSERT_NE(propertyConverter, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyWithConverterHandler*>(propertyConverter), nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyHandler*>(propertyConverter), nullptr);
    // The four property-element blocks and the two constructor handlers.
    EXPECT_NE(dynamic_cast<Handlers::PropertyComplexHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyComplexStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyArrayHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyArrayStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyListHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyListStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyDictionaryHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyDictionaryStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::ConstructorParametersStartHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::ConstructorParametersStart)),
        nullptr);
    EXPECT_NE(dynamic_cast<Handlers::ConstructorParameterTypeHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::ConstructorParameterType)),
        nullptr);
}

// The section-C end-to-end drive: the real manifest over the walk document
// renders byte-identically to the real registry (the gold the probe dumped).
TEST_F(HandlersTest, ProcessChildrenOverTheWalkFixtureMatchesTheGoldRender)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);

    BamlElement parentElem(root);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Document");
    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    EXPECT_EQ(parentElem.Xaml.Element->ToString(), kProcessChildrenGoldRender);
    // The ToolBar BamlElement the real ElementHandler returned is wired into
    // the parent's children with its back-pointer.
    ASSERT_EQ(parentElem.Children.size(), 1u);
    EXPECT_EQ(parentElem.Children[0]->Node, root->Children[5].get());
    EXPECT_EQ(parentElem.Children[0]->Parent, &parentElem);
}

// The D0 drive: DocumentHandler over the root block with a null parent (the
// Decompile shape) -- the pseudo-namespaced <Document> wrapper and the tree.
TEST_F(HandlersTest, DocumentHandlerDriveMatchesTheGold)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);

    IHandler* handler = Lookup(Baml::BamlRecordType::DocumentStart);
    std::unique_ptr<BamlElement> elem = handler->Translate(*ctx, *root, nullptr);
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->Node, root);
    EXPECT_EQ(elem->Parent, nullptr);
    EXPECT_EQ(elem->Xaml.Element->ToString(), kDocumentHandlerGoldRender);
    // The pseudo name (GetPseudoName -- the ILSpy namespace).
    EXPECT_EQ(elem->Xaml.Element->Name().NamespaceName(),
        "https://github.com/icsharpcode/ILSpy");
    EXPECT_EQ(elem->Xaml.Element->Name().LocalName(), "Document");

    // The tree the probe dumped: one child (the ToolBar block), parent set,
    // both elements carrying their own scope annotation.
    ASSERT_EQ(elem->Children.size(), 1u);
    BamlElement* toolBar = elem->Children[0].get();
    EXPECT_EQ(toolBar->Parent, elem.get());
    EXPECT_EQ(toolBar->Node, root->Children[5].get());
    EXPECT_EQ(toolBar->Xaml.Element->Name().NamespaceName(), "http://probe.pi/ns");
    EXPECT_EQ(toolBar->Xaml.Element->Name().LocalName(), "ToolBar");

    auto* scope = elem->Xaml.Element->Annotation<
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope>>();
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(scope->get()->Element(), elem.get());
    auto* toolBarScope = toolBar->Xaml.Element->Annotation<
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope>>();
    ASSERT_NE(toolBarScope, nullptr);
    EXPECT_EQ(toolBarScope->get()->Element(), toolBar);

    // The ElementHandler's owning XamlType annotation: the type the context's
    // cache resolved (the annotation roots the shared cache entry).
    auto* typeAnnotation =
        toolBar->Xaml.Element->Annotation<std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlType>>();
    ASSERT_NE(typeAnnotation, nullptr);
    EXPECT_EQ((*typeAnnotation)->TypeName, "ToolBar");
    EXPECT_EQ(typeAnnotation->get(), ctx->ResolveType(0xFD63));
}

// D1: TextHandler adds the record's value as the parent element's text.
TEST_F(HandlersTest, TextHandlerAddsTheRecordValue)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::Text);

    auto record = std::make_unique<Baml::TextRecord>();
    record->Value = "hello";
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent>hello</Parent>");
}

// D9: the null-parent NRE (the probed .NET message).
TEST_F(HandlersTest, TextHandlerWithANullParentThrowsTheNRE)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::Text);

    auto record = std::make_unique<Baml::TextRecord>();
    record->Value = "x";
    Baml::BamlRecordNode node(record.get());
    try {
        handler->Translate(*ctx, node, nullptr);
        FAIL() << "the null-parent translate must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// D2: TextWithConverterHandler inherits the TextHandler translation.
TEST_F(HandlersTest, TextWithConverterHandlerInheritsTheTextTranslate)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::TextWithConverter);

    auto record = std::make_unique<Baml::TextWithConverterRecord>();
    record->Value = "cv";
    record->ConverterTypeId = 0;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent>cv</Parent>");
}

// D3: TextWithIdHandler resolves the value through the string-id table.
TEST_F(HandlersTest, TextWithIdHandlerResolvesTheStringId)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::TextWithId);
    // The fixture's StringInfo row: id 0 -> "s0".
    EXPECT_EQ(ctx->ResolveString(0), std::optional<std::string>("s0"));

    auto record = std::make_unique<Baml::TextWithIdRecord>();
    record->ValueId = 0;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent>s0</Parent>");
}

// D4: ConnectionIdHandler annotates the parent element with the owning
// BamlConnectionId payload.
TEST_F(HandlersTest, ConnectionIdHandlerAnnotatesTheElement)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::ConnectionId);

    auto record = std::make_unique<Baml::ConnectionIdRecord>();
    record->ConnectionId = 42;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent />");
    auto* annotation =
        parentElem.Xaml.Element->Annotation<std::shared_ptr<BamlConnectionId>>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ((*annotation)->Id, 42u);
}

// D5: DefAttributeHandler adds the x:-namespaced attribute (the auto-prefix
// render the probe dumped).
TEST_F(HandlersTest, DefAttributeHandlerAddsTheXamlAttribute)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::DefAttribute);
    // The known string id 0xffff resolves to "Name" (the KnownThings strings
    // table's first row).
    EXPECT_EQ(ctx->ResolveString(0xFFFF), std::optional<std::string>("Name"));

    auto record = std::make_unique<Baml::DefAttributeRecord>();
    record->NameId = 0xFFFF;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent p1:Name=\"v\" xmlns:p1=\"http://schemas.microsoft.com/winfx/2006/xaml\" />");
}

// D8: the missing-string-id arm -- ResolveString answers null and the
// `xNs + name` operator hits the XName.Get(null) ArgumentNullException
// (the probed .NET message).
TEST_F(HandlersTest, DefAttributeHandlerWithAMissingStringIdThrows)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::DefAttribute);
    // No StringInfo row carries id 999: the missing-low-id null.
    EXPECT_EQ(ctx->ResolveString(999), std::nullopt);

    auto record = std::make_unique<Baml::DefAttributeRecord>();
    record->NameId = 999;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    try {
        handler->Translate(*ctx, node, &parentElem);
        FAIL() << "the missing-string-id translate must throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Value cannot be null. (Parameter 'localName')");
    }
}

// D6: PresentationOptionsAttributeHandler adds the presentation-options
// attribute with the parent element as the namespace context.
TEST_F(HandlersTest, PresentationOptionsAttributeHandlerAddsTheOptionsAttribute)
{
    auto ctx = fixture_.MakeContext();
    IHandler* handler = Lookup(Baml::BamlRecordType::PresentationOptionsAttribute);

    auto record = std::make_unique<Baml::PresentationOptionsAttributeRecord>();
    record->NameId = 0xFFFF;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent p1:Name=\"v\" "
        "xmlns:p1=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation/options\" />");
}

// D7: the nine null-returning handlers -- each claims its record type,
// contributes no element, and leaves the parent untouched.
TEST_F(HandlersTest, TheNullReturningHandlersContributeNothing)
{
    auto ctx = fixture_.MakeContext();

    struct NullCase {
        Baml::BamlRecordType type;
        std::unique_ptr<Baml::BamlRecord> record;
    };
    std::vector<NullCase> cases;
    {
        auto rec = std::make_unique<Baml::AssemblyInfoRecord>();
        rec->AssemblyId = 0;
        rec->AssemblyFullName = "PresentationFramework, Version=4.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35";
        cases.push_back({ Baml::BamlRecordType::AssemblyInfo, std::move(rec) });
    }
    {
        auto rec = std::make_unique<Baml::AttributeInfoRecord>();
        rec->AttributeId = 0;
        rec->OwnerTypeId = 0xFD63;
        rec->AttributeUsage = 0;
        rec->Name = "Width";
        cases.push_back({ Baml::BamlRecordType::AttributeInfo, std::move(rec) });
    }
    {
        auto rec = std::make_unique<Baml::ContentPropertyRecord>();
        rec->AttributeId = 0;
        cases.push_back({ Baml::BamlRecordType::ContentProperty, std::move(rec) });
    }
    cases.push_back({ Baml::BamlRecordType::DeferableContentStart,
        std::make_unique<Baml::DeferableContentStartRecord>() });
    {
        auto rec = std::make_unique<Baml::LineNumberAndPositionRecord>();
        rec->LineNumber = 3;
        rec->LinePosition = 5;
        cases.push_back({ Baml::BamlRecordType::LineNumberAndPosition, std::move(rec) });
    }
    {
        auto rec = std::make_unique<Baml::LinePositionRecord>();
        rec->LinePosition = 5;
        cases.push_back({ Baml::BamlRecordType::LinePosition, std::move(rec) });
    }
    {
        auto rec = std::make_unique<Baml::PIMappingRecord>();
        rec->XmlNamespace = "http://probe.pi/ns";
        rec->ClrNamespace = "System.Windows.Controls";
        rec->AssemblyId = 0;
        cases.push_back({ Baml::BamlRecordType::PIMapping, std::move(rec) });
    }
    {
        auto rec = std::make_unique<Baml::TypeInfoRecord>();
        rec->TypeId = 0;
        rec->AssemblyId = 0;
        rec->TypeFullName = "System.Windows.Controls.Button";
        cases.push_back({ Baml::BamlRecordType::TypeInfo, std::move(rec) });
    }
    {
        auto rec = std::make_unique<Baml::TypeSerializerInfoRecord>();
        rec->TypeId = 0;
        rec->AssemblyId = 0;
        rec->TypeFullName = "System.Windows.Controls.Button";
        rec->SerializerTypeId = 0;
        cases.push_back({ Baml::BamlRecordType::TypeSerializerInfo, std::move(rec) });
    }

    ASSERT_EQ(cases.size(), 9u);
    for (NullCase& nullCase : cases) {
        IHandler* handler = Lookup(nullCase.type);
        ASSERT_NE(handler, nullptr);
        Baml::BamlRecordNode node(nullCase.record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");

        std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
        EXPECT_EQ(result, nullptr) << Baml::RecordTypeName(nullCase.type);
        EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent />")
            << Baml::RecordTypeName(nullCase.type);
        EXPECT_EQ(parentElem.Children.size(), 0u) << Baml::RecordTypeName(nullCase.type);
    }
}

// ===== the property-family drives (the probe's section E) ===================
//
// Every expectation below is the byte-exact gold the HandlerMapProbe
// dumped driving the REAL handlers from the installed
// ICSharpCode.BamlDecompiler.dll over the identical section-E fixture (the
// walk document plus the mscorlib-backed string/type rows whose members
// resolve through the stub main-module types).

// A parent BamlElement over a plain <Parent> element (the handler's
// contribution target), optionally annotated with the XamlType the
// ElementHandler would have attached.
BamlElement MakeParentElem()
{
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<ILSpy::Decompiler::Xml::XElement>("Parent");
    return parentElem;
}

// E1: the attached arm -- a parent with no XamlType annotation (a null
// elemType makes IsAttachedTo true), and the fixture's Width property
// (whose synthetic ToolBar declaring type resolves no member, so even an
// annotated parent stays attached).
TEST_F(HandlersTest, PropertyHandlerAttachedRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::Property);

    auto record = std::make_unique<Baml::PropertyRecord>();
    record->AttributeId = 0;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem = MakeParentElem();

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent p1:ToolBar.Width=\"v\" xmlns:p1=\"http://probe.pi/ns\" />");
}

// E2: the parent carries the ToolBar XamlType annotation -- the synthetic
// type resolves no member, so IsAttachedTo stays true (the same render).
TEST_F(HandlersTest, PropertyHandlerToolBarAnnotatedRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::Property);

    BamlElement parentElem = MakeParentElem();
    parentElem.Xaml.Element->AddAnnotation(ctx->ResolveTypeOwning(0xFD63));

    auto record = std::make_unique<Baml::PropertyRecord>();
    record->AttributeId = 0;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent p1:ToolBar.Width=\"v\" xmlns:p1=\"http://probe.pi/ns\" />");
}

// E3: the null-parent NRE (the probed .NET message).
TEST_F(HandlersTest, PropertyHandlerWithANullParentThrowsTheNRE)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::Property);

    auto record = std::make_unique<Baml::PropertyRecord>();
    record->AttributeId = 0;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());

    try {
        handler->Translate(*ctx, node, nullptr);
        FAIL() << "the null-parent translate must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// E4: the non-attached arm -- a REAL resolved member (String.Length declared
// on the annotated main-module String type): the plain local-name form with
// the clr-namespace declaration ResolveNamespace attached.
TEST_F(HandlersTest, PropertyHandlerNonAttachedRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::Property);

    BamlElement parentElem = MakeParentElem();
    parentElem.Xaml.Element->AddAnnotation(ctx->ResolveTypeOwning(1));

    auto record = std::make_unique<Baml::PropertyRecord>();
    record->AttributeId = 1;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent xmlns:system=\"clr-namespace:System\" Length=\"v\" />");
}

// E5: the x:Name arm -- a real Name member on the main-module System.Type
// (GetDefinition()?.ParentModule.IsMainModule): the XAML-namespaced Name
// attribute with the p2 auto-prefix (the xmlns:system push bumped the
// writer's prefix counter).
TEST_F(HandlersTest, PropertyHandlerXNameRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::Property);

    BamlElement parentElem = MakeParentElem();
    parentElem.Xaml.Element->AddAnnotation(ctx->ResolveTypeOwning(2));

    auto record = std::make_unique<Baml::PropertyRecord>();
    record->AttributeId = 2;
    record->Value = "v";
    Baml::BamlRecordNode node(record.get());

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent xmlns:system=\"clr-namespace:System\" "
        "p2:Name=\"v\" xmlns:p2=\"http://schemas.microsoft.com/winfx/2006/xaml\" />");
}

// E6: PropertyWithConverterHandler inherits the PropertyHandler
// translation over its own record type.
TEST_F(HandlersTest, PropertyWithConverterHandlerInheritsThePropertyTranslate)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::PropertyWithConverter);

    auto record = std::make_unique<Baml::PropertyWithConverterRecord>();
    record->AttributeId = 0;
    record->Value = "cv";
    record->ConverterTypeId = 0;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem = MakeParentElem();

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent p1:ToolBar.Width=\"cv\" xmlns:p1=\"http://probe.pi/ns\" />");
}

// A crafted block node: the header record plus a Text child (the
// ProcessChildren walk's contribution).
struct BlockFixture {
    std::unique_ptr<Baml::BamlRecord> header;
    std::unique_ptr<Baml::BamlRecord> childRecord;
    Baml::BamlBlockNode node;

    BlockFixture(std::unique_ptr<Baml::BamlRecord> headerRecord,
        std::unique_ptr<Baml::BamlRecord> child)
        : header(std::move(headerRecord))
        , childRecord(std::move(child))
    {
        node.Header = header.get();
        node.Children.push_back(
            std::make_unique<Baml::BamlRecordNode>(childRecord.get()));
    }
};

std::unique_ptr<Baml::BamlRecord> PropertyStartRecord(Baml::BamlRecordType type,
    std::uint16_t attributeId)
{
    // The four record classes all derive from PropertyComplexStartRecord;
    // the shared body reads only the AttributeId member, so the fixture
    // builds the concrete class per type.
    if (type == Baml::BamlRecordType::PropertyComplexStart) {
        auto record = std::make_unique<Baml::PropertyComplexStartRecord>();
        record->AttributeId = attributeId;
        return record;
    }
    if (type == Baml::BamlRecordType::PropertyArrayStart) {
        auto record = std::make_unique<Baml::PropertyArrayStartRecord>();
        record->AttributeId = attributeId;
        return record;
    }
    if (type == Baml::BamlRecordType::PropertyListStart) {
        auto record = std::make_unique<Baml::PropertyListStartRecord>();
        record->AttributeId = attributeId;
        return record;
    }
    auto record = std::make_unique<Baml::PropertyDictionaryStartRecord>();
    record->AttributeId = attributeId;
    return record;
}

// E7/E8/E9/E10: the four property-element blocks -- identical bodies over
// their own record types (the C# classes are literally identical): the
// property element under the parent with the PI-mapped namespace, the
// recursive Text child inside, and the element's own XmlnsScope annotation.
TEST_F(HandlersTest, ThePropertyElementBlocksRenderTheGold)
{
    struct Row {
        Baml::BamlRecordType type;
        const char* childText;
    };
    const Row rows[] = {
        { Baml::BamlRecordType::PropertyComplexStart, "hello" },
        { Baml::BamlRecordType::PropertyArrayStart, "a" },
        { Baml::BamlRecordType::PropertyListStart, "l" },
        { Baml::BamlRecordType::PropertyDictionaryStart, "d" },
    };
    for (const Row& row : rows) {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(row.type);
        ASSERT_NE(handler, nullptr);

        auto text = std::make_unique<Baml::TextRecord>();
        text->Value = row.childText;
        BlockFixture block(PropertyStartRecord(row.type, 0), std::move(text));
        BamlElement parentElem = MakeParentElem();

        std::unique_ptr<BamlElement> result = handler->Translate(
            *ctx, block.node, &parentElem);
        ASSERT_NE(result, nullptr) << Baml::RecordTypeName(row.type);
        // The returned element: the property's element with the text child,
        // and its own scope annotation from ProcessChildren.
        EXPECT_EQ(result->Node, &block.node) << Baml::RecordTypeName(row.type);
        EXPECT_EQ(result->Parent, nullptr) << Baml::RecordTypeName(row.type);
        EXPECT_EQ(result->Xaml.Element->ToString(),
            "<ToolBar.Width xmlns=\"http://probe.pi/ns\">" + std::string(row.childText)
                + "</ToolBar.Width>")
            << Baml::RecordTypeName(row.type);
        auto* scope = result->Xaml.Element->Annotation<
            std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope>>();
        ASSERT_NE(scope, nullptr) << Baml::RecordTypeName(row.type);
        EXPECT_EQ(scope->get()->Element(), result.get())
            << Baml::RecordTypeName(row.type);
        EXPECT_EQ(parentElem.Xaml.Element->ToString(),
            "<Parent>\r\n  <ToolBar.Width xmlns=\"http://probe.pi/ns\">"
                + std::string(row.childText)
                + "</ToolBar.Width>\r\n</Parent>")
            << Baml::RecordTypeName(row.type);
        // The property annotation the rewrite passes read back.
        auto* property = result->Xaml.Element->Annotation<
            std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlProperty>>();
        ASSERT_NE(property, nullptr) << Baml::RecordTypeName(row.type);
        EXPECT_EQ((*property)->PropertyName, "Width")
            << Baml::RecordTypeName(row.type);
    }
}

// E11: the property block over the mscorlib-backed property (the
// main-module namespace arm of ResolveNamespace -- the system-prefixed
// full name with the clr-namespace declaration on the property's own
// element).
TEST_F(HandlersTest, PropertyComplexHandlerOverAMainModulePropertyRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::PropertyComplexStart);

    auto text = std::make_unique<Baml::TextRecord>();
    text->Value = "hello";
    BlockFixture block(PropertyStartRecord(Baml::BamlRecordType::PropertyComplexStart, 1),
        std::move(text));
    BamlElement parentElem = MakeParentElem();

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, block.node, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<system:String.Length xmlns:system=\"clr-namespace:System\">"
        "hello</system:String.Length>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n  <system:String.Length xmlns:system=\"clr-namespace:System\">"
        "hello</system:String.Length>\r\n</Parent>");
}

// E12: the null-parent NRE (the parent deref precedes the children walk).
TEST_F(HandlersTest, PropertyComplexHandlerWithANullParentThrowsTheNRE)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::PropertyComplexStart);

    auto text = std::make_unique<Baml::TextRecord>();
    text->Value = "hello";
    BlockFixture block(PropertyStartRecord(Baml::BamlRecordType::PropertyComplexStart, 0),
        std::move(text));

    try {
        handler->Translate(*ctx, block.node, nullptr);
        FAIL() << "the null-parent translate must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// E13: ConstructorParametersStartHandler -- the pseudo-named <Ctor>
// wrapper with the recursive Text child.
TEST_F(HandlersTest, ConstructorParametersStartHandlerRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::ConstructorParametersStart);

    auto text = std::make_unique<Baml::TextRecord>();
    text->Value = "hello";
    BlockFixture block(std::make_unique<Baml::ConstructorParametersStartRecord>(),
        std::move(text));
    BamlElement parentElem = MakeParentElem();

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, block.node, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Xaml.Element->Name().NamespaceName(),
        "https://github.com/icsharpcode/ILSpy");
    EXPECT_EQ(result->Xaml.Element->Name().LocalName(), "Ctor");
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">hello</Ctor>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">"
        "hello</Ctor>\r\n</Parent>");
}

// E14: ConstructorParameterTypeHandler over the mscorlib-backed string
// type -- the TypeExtension element (the XAML namespace bound as its
// default), its Ctor pseudo-child (the ILSpy namespace), and the
// clr-namespace declaration ctx.ToString attached to the PARENT.
TEST_F(HandlersTest, ConstructorParameterTypeHandlerOverStringRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::ConstructorParameterType);

    auto record = std::make_unique<Baml::ConstructorParameterTypeRecord>();
    record->TypeId = 1;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem = MakeParentElem();

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Node, &node);
    EXPECT_EQ(result->Parent, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">system:String</Ctor>\r\n"
        "</TypeExtension>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent xmlns:system=\"clr-namespace:System\">\r\n"
        "  <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">system:String</Ctor>\r\n"
        "  </TypeExtension>\r\n"
        "</Parent>");
    // The TypeExtension element carries the known TypeExtension type
    // annotation (id 0xfd4d).
    auto* typeAnnotation = result->Xaml.Element->Annotation<
        std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlType>>();
    ASSERT_NE(typeAnnotation, nullptr);
    EXPECT_EQ((*typeAnnotation)->TypeName, "TypeExtension");
    EXPECT_EQ(typeAnnotation->get(), ctx->ResolveType(0xfd4d));
}

// E15: over the synthetic ToolBar type (the PI-mapped namespace renders
// with no prefix and no clr-namespace declaration).
TEST_F(HandlersTest, ConstructorParameterTypeHandlerOverToolBarRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::ConstructorParameterType);

    auto record = std::make_unique<Baml::ConstructorParameterTypeRecord>();
    record->TypeId = 0xFD63;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem = MakeParentElem();

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>\r\n"
        "</TypeExtension>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>\r\n"
        "  </TypeExtension>\r\n"
        "</Parent>");
}

// E16: the null-parent NRE (the GetKnownNamespace parent read).
TEST_F(HandlersTest, ConstructorParameterTypeHandlerWithANullParentThrowsTheNRE)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = Lookup(Baml::BamlRecordType::ConstructorParameterType);

    auto record = std::make_unique<Baml::ConstructorParameterTypeRecord>();
    record->TypeId = 1;
    Baml::BamlRecordNode node(record.get());

    try {
        handler->Translate(*ctx, node, nullptr);
        FAIL() << "the null-parent translate must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

} // namespace
