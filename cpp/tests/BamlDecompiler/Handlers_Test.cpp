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

#include "BamlTestSupport.hpp"
#include "BamlDecompiler/BamlConnectionId.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/Handlers/Records.hpp"
#include "BamlDecompiler/IHandlers.hpp"
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
// identities the manifest constructs.
TEST(HandlersManifestTest, CreateBuiltinHandlersHasTheSeventeenPortedRows)
{
    std::vector<std::unique_ptr<IHandler>> handlers = HandlerMap::CreateBuiltinHandlers();
    ASSERT_EQ(handlers.size(), 17u);

    const Baml::BamlRecordType expected[] = {
        Baml::BamlRecordType::DocumentStart,
        Baml::BamlRecordType::ElementStart,
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
    ASSERT_EQ(std::size(expected), 17u);
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

} // namespace
