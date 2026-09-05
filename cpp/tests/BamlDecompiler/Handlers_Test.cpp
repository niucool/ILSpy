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
//  * the section-F x:Key defer-family drives (the HandlerMapProbe's F1-F10):
//    the three key handlers' end-to-end ProcessChildren walks (the
//    ElementHandler defer branch renders the x:Key element inside the value
//    element) and the direct Translate/TranslateDefer drives over the
//    crafted key+value sibling pairs, with the annotation wiring, the
//    KeyElement back-pointer, and every exception arm -- byte-exact against
//    the real handlers' drives.
//  * the section-G static-resource-family drives (the HandlerMapProbe's
//    G1-G18): the end-to-end ProcessChildren walks over the crafted
//    key+resource+value-block wiring (the real findtoolbar defer-block
//    layout -- the registrations precede the consumer-carrying value
//    blocks), the StaticResourceStart block's direct drives, the
//    OptimizedStaticResource arm matrix (the string/type/static-low arms and
//    the static-high SystemResourceIds magic ranges), the StaticId /
//    PropertyWithStaticResourceId consumer drives, and every exception arm
//    -- byte-exact against the real handlers' drives.
//  * the section-H misc-family drives (the HandlerMapProbe's H1-H22):
//    XmlnsProperty's end-to-end walk and direct drives (the NamespaceMap
//    adds, the prefixed/escaped declarations, the assembly-id loop, the
//    main-module CLR-rows arm, the scope-less Add and null-parent NREs),
//    PropertyTypeReference's String.Length and Style.TargetType drives (the
//    latter over the section-H context -- the real PresentationFramework
//    main's stub counterpart carrying the XmlnsDefinitionAttribute rows and
//    the Style stub -- attaching the TargetTypeAnnotation), and
//    PropertyWithExtension's four initializer arms (the {x:Type} plain and
//    annotated forms, the valTypeExt/valStaticExt nested wraps, the
//    TemplateBinding/static-low/static-high arms with the magic-range
//    resource rows, the escaped string arm, and the exception arms) -- every
//    render byte-exact against the real handlers' drives.

#include "BamlTestSupport.hpp"
#include "BamlDecompiler/BamlConnectionId.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/Handlers/Records.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlResourceKey.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
namespace Handlers = ILSpy::BamlDecompiler::Handlers;
namespace Xml = ILSpy::Decompiler::Xml;
using ILSpy::BamlDecompiler::BamlConnectionId;
using ILSpy::BamlDecompiler::BamlElement;
using ILSpy::BamlDecompiler::HandlerMap;
using ILSpy::BamlDecompiler::IHandler;
using ILSpy::BamlDecompiler::IDeferHandler;
using ILSpy::BamlDecompiler::XamlContext;
using ILSpy::Tests::Baml::XamlContextFixture;
using ILSpy::Tests::Baml::kPresentationXmlns;
using ILSpy::Tests::Baml::kProbePiNs;

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

// The manifest: the 35 ported rows, every record type distinct, each inside
// the gold registry inventory (the HandlerMapTest guard), with the class
// identities the manifest constructs.
TEST(HandlersManifestTest, CreateBuiltinHandlersHasTheThirtyFivePortedRows)
{
    std::vector<std::unique_ptr<IHandler>> handlers = HandlerMap::CreateBuiltinHandlers();
    ASSERT_EQ(handlers.size(), 35u);

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
        Baml::BamlRecordType::DefAttributeKeyString,
        Baml::BamlRecordType::DefAttributeKeyType,
        Baml::BamlRecordType::KeyElementStart,
        Baml::BamlRecordType::ConnectionId,
        Baml::BamlRecordType::ContentProperty,
        Baml::BamlRecordType::TextWithId,
        Baml::BamlRecordType::PresentationOptionsAttribute,
        Baml::BamlRecordType::LineNumberAndPosition,
        Baml::BamlRecordType::LinePosition,
        Baml::BamlRecordType::StaticResourceStart,
        Baml::BamlRecordType::StaticResourceId,
        Baml::BamlRecordType::OptimizedStaticResource,
        Baml::BamlRecordType::PropertyWithStaticResourceId,
        Baml::BamlRecordType::XmlnsProperty,
        Baml::BamlRecordType::PropertyTypeReference,
        Baml::BamlRecordType::PropertyWithExtension,
    };
    ASSERT_EQ(std::size(expected), 35u);
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
    // The key-family rows: the two record handlers implement the defer
    // interface directly; the KeyElementStart row is the ElementHandler
    // subclass whose EXPLICIT IHandler.Translate re-bind the base cast still
    // answers (the TextWithConverter re-bind pattern).
    IHandler* keyString = HandlerMap::LookupHandler(Baml::BamlRecordType::DefAttributeKeyString);
    ASSERT_NE(keyString, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::DefAttributeStringHandler*>(keyString), nullptr);
    EXPECT_NE(dynamic_cast<IDeferHandler*>(keyString), nullptr);
    IHandler* keyType = HandlerMap::LookupHandler(Baml::BamlRecordType::DefAttributeKeyType);
    ASSERT_NE(keyType, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::DefAttributeTypeHandler*>(keyType), nullptr);
    EXPECT_NE(dynamic_cast<IDeferHandler*>(keyType), nullptr);
    IHandler* keyElement = HandlerMap::LookupHandler(Baml::BamlRecordType::KeyElementStart);
    ASSERT_NE(keyElement, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::KeyElementStartHandler*>(keyElement), nullptr);
    EXPECT_NE(dynamic_cast<Handlers::ElementHandler*>(keyElement), nullptr);
    EXPECT_NE(dynamic_cast<IDeferHandler*>(keyElement), nullptr);
    // The static-resource family rows: the two resource handlers implement
    // the defer interface (the consumers' TranslateDefer re-drive), the two
    // consumers do not.
    IHandler* srStart = HandlerMap::LookupHandler(Baml::BamlRecordType::StaticResourceStart);
    ASSERT_NE(srStart, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::StaticResourceStartHandler*>(srStart), nullptr);
    EXPECT_NE(dynamic_cast<IDeferHandler*>(srStart), nullptr);
    IHandler* srId = HandlerMap::LookupHandler(Baml::BamlRecordType::StaticResourceId);
    ASSERT_NE(srId, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::StaticResourceIdHandler*>(srId), nullptr);
    EXPECT_EQ(dynamic_cast<IDeferHandler*>(srId), nullptr);
    IHandler* osr = HandlerMap::LookupHandler(Baml::BamlRecordType::OptimizedStaticResource);
    ASSERT_NE(osr, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::OptimizedStaticResourceHandler*>(osr), nullptr);
    EXPECT_NE(dynamic_cast<IDeferHandler*>(osr), nullptr);
    IHandler* pwid =
        HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyWithStaticResourceId);
    ASSERT_NE(pwid, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyWithStaticResourceIdHandler*>(pwid), nullptr);
    EXPECT_EQ(dynamic_cast<IDeferHandler*>(pwid), nullptr);
    // The misc-family rows (no defer interface among them).
    IHandler* xmlnsProp = HandlerMap::LookupHandler(Baml::BamlRecordType::XmlnsProperty);
    ASSERT_NE(xmlnsProp, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::XmlnsPropertyHandler*>(xmlnsProp), nullptr);
    EXPECT_EQ(dynamic_cast<IDeferHandler*>(xmlnsProp), nullptr);
    IHandler* ptr = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyTypeReference);
    ASSERT_NE(ptr, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyTypeReferenceHandler*>(ptr), nullptr);
    EXPECT_EQ(dynamic_cast<IDeferHandler*>(ptr), nullptr);
    IHandler* pwe = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyWithExtension);
    ASSERT_NE(pwe, nullptr);
    EXPECT_NE(dynamic_cast<Handlers::PropertyWithExtensionHandler*>(pwe), nullptr);
    EXPECT_EQ(dynamic_cast<IDeferHandler*>(pwe), nullptr);
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


// ===== Section F: the x:Key defer-handler family ============================
// (DefAttributeStringHandler / DefAttributeTypeHandler / KeyElementStartHandler
// -- the gold the probe's section F dumped: the crafted key+value sibling
// pairs over the section-E fixture, driven end-to-end through
// HandlerMap.ProcessChildren (the ElementHandler defer branch renders the
// x:Key element inside the value element) and directly (Translate creating
// the key annotation pair, TranslateDefer rendering the x:Key element). The
// crafted shape is the real defer-block wiring of findtoolbar.baml: the key
// record's deferred `Record` property points at the value element's
// ElementStart record instance, and XamlResourceKey.Create's children-walk
// arm annotates BOTH the key node and the value block.)

// The crafted defer-block pair (the probe's MakePair): the key node and the
// value element block as SIBLINGS under a container block, every child's
// Parent back-pointer wired (the parsed-tree shape -- the Create walk reads
// node.Parent.Children).
struct KeyPairFixture {
    std::unique_ptr<Baml::BamlRecord> containerHeader;
    std::unique_ptr<Baml::BamlRecord> keyRecord;
    std::unique_ptr<Baml::BamlRecord> keyText;
    std::unique_ptr<Baml::BamlRecord> keyEnd;
    std::unique_ptr<Baml::BamlRecord> valueStart;
    std::unique_ptr<Baml::BamlRecord> valueText;
    std::unique_ptr<Baml::BamlRecord> valueEnd;

    Baml::BamlBlockNode container;
    std::unique_ptr<Baml::BamlBlockNode> valueBlock;
    Baml::BamlNode* keyNode = nullptr;
    Baml::BamlBlockNode* valueBlockPtr = nullptr;

    // The construction guard the tests assert on first (ASSERT_* inside a
    // constructor fails MSVC C2534 -- the CraftedTree convention).
    bool Ok = false;

    KeyPairFixture(std::unique_ptr<Baml::BamlRecord> keyRecordArg, bool keyIsBlock)
    {
        keyRecord = std::move(keyRecordArg);
        containerHeader = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(containerHeader.get())->TypeId = 0xFD63;
        valueStart = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(valueStart.get())->TypeId = 0xFD63;
        valueText = std::make_unique<Baml::TextRecord>();
        static_cast<Baml::TextRecord*>(valueText.get())->Value = "hello";
        valueEnd = std::make_unique<Baml::ElementEndRecord>();
        keyEnd = std::make_unique<Baml::KeyElementEndRecord>();
        keyText = std::make_unique<Baml::TextRecord>();
        static_cast<Baml::TextRecord*>(keyText.get())->Value = "inner";

        // The deferred target: the value block's header record instance
        // (the real defer-block wiring -- Create's children-walk arm matches
        // it against the value block's own record).
        auto* deferRecord = dynamic_cast<Baml::IBamlDeferRecord*>(keyRecord.get());
        if (deferRecord == nullptr)
            return; // a non-defer record leaves the fixture unbuilt; the
                    // tests' ASSERT_TRUE(pair.Ok) reports it
        deferRecord->SetRecord(valueStart.get());

        // The value block: [Text(hello)], the parent wired.
        valueBlock = std::make_unique<Baml::BamlBlockNode>();
        valueBlock->Header = valueStart.get();
        valueBlock->Footer = valueEnd.get();
        auto valueTextNode = std::make_unique<Baml::BamlRecordNode>(valueText.get());
        valueTextNode->Parent = valueBlock.get();
        valueBlock->Children.push_back(std::move(valueTextNode));

        // The key node: a leaf record node, or the keyed-content block (the
        // KeyElementStart case -- its own children are the keyed element's
        // content).
        std::unique_ptr<Baml::BamlNode> key;
        if (keyIsBlock) {
            auto keyBlock = std::make_unique<Baml::BamlBlockNode>();
            keyBlock->Header = keyRecord.get();
            keyBlock->Footer = keyEnd.get();
            auto keyTextNode = std::make_unique<Baml::BamlRecordNode>(keyText.get());
            keyTextNode->Parent = keyBlock.get();
            keyBlock->Children.push_back(std::move(keyTextNode));
            key = std::move(keyBlock);
        } else {
            key = std::make_unique<Baml::BamlRecordNode>(keyRecord.get());
        }
        key->Parent = &container;

        // The container: [key, value].
        container.Header = containerHeader.get();
        valueBlock->Parent = &container;
        valueBlockPtr = valueBlock.get();
        container.Children.push_back(std::move(key));
        keyNode = container.Children.back().get();
        container.Children.push_back(std::move(valueBlock));
        Ok = true;
    }

    // The key annotation a node carries (null when none).
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> KeyOf(Baml::BamlNode& node)
    {
        auto* annotation =
            std::any_cast<std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey>>(
                &node.Annotation);
        return annotation == nullptr ? nullptr : *annotation;
    }
};

// F1: the DefAttributeKeyString end-to-end drive -- ProcessChildren over the
// crafted defer block: the key handler annotates the pair, the value
// element's ElementHandler defer branch renders the x:Key element (the
// resolved known string id 0xffff -> "Name") AFTER the walked children.
TEST_F(HandlersTest, DefAttributeStringKeyRendersInsideTheValueElement)
{
    auto ctx = fixture_.MakeContextE();
    auto keyRecord = std::make_unique<Baml::DefAttributeKeyStringRecord>();
    keyRecord->ValueId = 0xFFFF;
    KeyPairFixture pair(std::move(keyRecord), false);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    HandlerMap::ProcessChildren(*ctx, pair.container, parentElem);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">hello"
        "<Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">Name</Key>"
        "</ToolBar>\r\n"
        "</Parent>");

    // The annotations: BOTH the key node and the value block carry the SAME
    // key instance whose KeyNode is the key node (not the value block).
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);
    EXPECT_EQ(keyAnn->KeyNode, pair.keyNode);
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> valueAnn =
        pair.KeyOf(*pair.valueBlockPtr);
    ASSERT_NE(valueAnn, nullptr);
    EXPECT_EQ(valueAnn, keyAnn);
    EXPECT_NE(valueAnn->KeyNode, pair.valueBlockPtr);

    // The tree: the parent holds the value doc, the value doc holds the Key
    // BamlElement (node = the key node) with its back-pointer, and the key
    // annotation's KeyElement points at it.
    ASSERT_EQ(parentElem.Children.size(), 1u);
    BamlElement* valueDoc = parentElem.Children[0].get();
    EXPECT_EQ(valueDoc->Node, pair.valueBlockPtr);
    ASSERT_EQ(valueDoc->Children.size(), 1u);
    BamlElement* keyElem = valueDoc->Children[0].get();
    EXPECT_EQ(keyElem->Node, pair.keyNode);
    EXPECT_EQ(keyElem->Parent, valueDoc);
    EXPECT_EQ(keyAnn->KeyElement, keyElem);
    EXPECT_EQ(keyElem->Xaml.Element->Name().NamespaceName(),
        "http://schemas.microsoft.com/winfx/2006/xaml");
    EXPECT_EQ(keyElem->Xaml.Element->Name().LocalName(), "Key");
    EXPECT_EQ(keyElem->Xaml.Element->Value(), "Name");
}

// F2: the DefAttributeKeyType end-to-end drive -- the x:Key element carries
// the {x:Type} TypeExtension child (the PI-mapped ToolBar type's name).
TEST_F(HandlersTest, DefAttributeTypeKeyRendersTheTypeExtensionInsideTheValueElement)
{
    auto ctx = fixture_.MakeContextE();
    auto keyRecord = std::make_unique<Baml::DefAttributeKeyTypeRecord>();
    static_cast<Baml::DefAttributeKeyTypeRecord*>(keyRecord.get())->TypeId = 0xFD63;
    KeyPairFixture pair(std::move(keyRecord), false);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    HandlerMap::ProcessChildren(*ctx, pair.container, parentElem);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">hello"
        "<Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">"
        "<TypeExtension><Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>"
        "</TypeExtension></Key>"
        "</ToolBar>\r\n"
        "</Parent>");
    EXPECT_NE(pair.KeyOf(*pair.keyNode), nullptr);
    EXPECT_NE(pair.KeyOf(*pair.valueBlockPtr), nullptr);
}

// F3: the KeyElementStart end-to-end drive -- the key BLOCK (its own
// children are the keyed element's content) renders the x:Key element
// wrapping the keyed element (the block header's type) inside the value
// element.
TEST_F(HandlersTest, KeyElementStartRendersTheKeyedElementInsideTheValueElement)
{
    auto ctx = fixture_.MakeContextE();
    auto keyHeader = std::make_unique<Baml::KeyElementStartRecord>();
    static_cast<Baml::KeyElementStartRecord*>(keyHeader.get())->TypeId = 0xFD63;
    KeyPairFixture pair(std::move(keyHeader), true);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    HandlerMap::ProcessChildren(*ctx, pair.container, parentElem);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">hello"
        "<Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">"
        "<ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>"
        "</Key>"
        "</ToolBar>\r\n"
        "</Parent>");

    // The annotations over the key BLOCK and the value block.
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);
    EXPECT_EQ(keyAnn->KeyNode, pair.keyNode);
    EXPECT_NE(pair.KeyOf(*pair.valueBlockPtr), nullptr);
}

// F4: the DefAttributeString direct drive -- Translate contributes nothing
// and TranslateDefer renders the x:Key element into the parent (the
// StaticResourceId consumer's shape).
TEST_F(HandlersTest, DefAttributeStringDirectTranslateAndDeferRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    auto keyRecord = std::make_unique<Baml::DefAttributeKeyStringRecord>();
    keyRecord->ValueId = 0xFFFF;
    KeyPairFixture pair(std::move(keyRecord), false);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    IHandler* handler = Lookup(Baml::BamlRecordType::DefAttributeKeyString);
    std::unique_ptr<BamlElement> translated = handler->Translate(*ctx, *pair.keyNode, &parentElem);
    EXPECT_EQ(translated, nullptr);
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);

    auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    std::unique_ptr<BamlElement> result = deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Node, pair.keyNode);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">Name</Key>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">Name</Key>\r\n"
        "</Parent>");
    EXPECT_EQ(keyAnn->KeyElement, result.get());
}

// F5: the DefAttributeType direct drive -- the x:Key element wrapping the
// TypeExtension child (the resolved type's name through ctx.ToString).
TEST_F(HandlersTest, DefAttributeTypeDirectTranslateAndDeferRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    auto keyRecord = std::make_unique<Baml::DefAttributeKeyTypeRecord>();
    static_cast<Baml::DefAttributeKeyTypeRecord*>(keyRecord.get())->TypeId = 0xFD63;
    KeyPairFixture pair(std::move(keyRecord), false);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    IHandler* handler = Lookup(Baml::BamlRecordType::DefAttributeKeyType);
    std::unique_ptr<BamlElement> translated = handler->Translate(*ctx, *pair.keyNode, &parentElem);
    EXPECT_EQ(translated, nullptr);
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);

    auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    std::unique_ptr<BamlElement> result = deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "  <TypeExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>\r\n"
        "  </TypeExtension>\r\n"
        "</Key>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "    <TypeExtension>\r\n"
        "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>\r\n"
        "    </TypeExtension>\r\n"
        "  </Key>\r\n"
        "</Parent>");
    EXPECT_EQ(keyAnn->KeyElement, result.get());
}

// F6: the KeyElementStart direct drive -- the inherited ElementHandler walk
// over the key block's own children renders the keyed element inside the
// x:Key element (the walk's own BamlElement return is discarded).
TEST_F(HandlersTest, KeyElementStartDirectTranslateAndDeferRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    auto keyHeader = std::make_unique<Baml::KeyElementStartRecord>();
    static_cast<Baml::KeyElementStartRecord*>(keyHeader.get())->TypeId = 0xFD63;
    KeyPairFixture pair(std::move(keyHeader), true);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    IHandler* handler = Lookup(Baml::BamlRecordType::KeyElementStart);
    std::unique_ptr<BamlElement> translated = handler->Translate(*ctx, *pair.keyNode, &parentElem);
    EXPECT_EQ(translated, nullptr);
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);

    auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    std::unique_ptr<BamlElement> result = deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>\r\n"
        "</Key>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "    <ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>\r\n"
        "  </Key>\r\n"
        "</Parent>");
    EXPECT_EQ(keyAnn->KeyElement, result.get());
    // The x:Key element wraps the keyed element (the inherited walk's
    // element), which carries the owning XamlType annotation of the block
    // header's type.
    std::shared_ptr<ILSpy::Decompiler::Xml::XElement> keyedElement;
    for (const std::shared_ptr<ILSpy::Decompiler::Xml::XElement>& element :
        result->Xaml.Element->Elements()) {
        keyedElement = element;
        break;
    }
    ASSERT_NE(keyedElement, nullptr);
    auto* typeAnnotation =
        keyedElement->Annotation<std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlType>>();
    ASSERT_NE(typeAnnotation, nullptr);
    EXPECT_EQ((*typeAnnotation)->TypeName, "ToolBar");
}

// F8a: the missing-annotation arm -- TranslateDefer over a fresh key node
// (no Create ran) NREs at the (XamlResourceKey)node.Annotation cast, for
// each of the three handlers.
TEST_F(HandlersTest, KeyHandlersWithoutTheAnnotationThrowTheNRE)
{
    for (Baml::BamlRecordType type : { Baml::BamlRecordType::DefAttributeKeyString,
            Baml::BamlRecordType::DefAttributeKeyType }) {
        auto ctx = fixture_.MakeContextE();
        std::unique_ptr<Baml::BamlRecord> keyRecord;
        if (type == Baml::BamlRecordType::DefAttributeKeyString) {
            auto record = std::make_unique<Baml::DefAttributeKeyStringRecord>();
            record->ValueId = 0xFFFF;
            keyRecord = std::move(record);
        } else {
            auto record = std::make_unique<Baml::DefAttributeKeyTypeRecord>();
            record->TypeId = 0xFD63;
            keyRecord = std::move(record);
        }
        KeyPairFixture pair(std::move(keyRecord), false);
        ASSERT_TRUE(pair.Ok);
        BamlElement parentElem = MakeParentElem();

        IHandler* handler = Lookup(type);
        auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
        ASSERT_NE(deferHandler, nullptr);
        try {
            deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
            FAIL() << "the missing-annotation defer must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }

    // The KeyElementStart block arm (the record cast precedes the annotation
    // cast -- the block node with its KeyElementStart header passes it).
    {
        auto ctx = fixture_.MakeContextE();
        auto keyHeader = std::make_unique<Baml::KeyElementStartRecord>();
        static_cast<Baml::KeyElementStartRecord*>(keyHeader.get())->TypeId = 0xFD63;
        KeyPairFixture pair(std::move(keyHeader), true);
        ASSERT_TRUE(pair.Ok);
        BamlElement parentElem = MakeParentElem();

        IHandler* handler = Lookup(Baml::BamlRecordType::KeyElementStart);
        auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
        ASSERT_NE(deferHandler, nullptr);
        try {
            deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
            FAIL() << "the missing-annotation defer must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
}

// F9: the DefAttributeType ORDER pin -- ctx.ToString(parent.Xaml, type) runs
// BEFORE the annotation cast: a mscorlib-backed type resolves the namespace
// against the PARENT (the clr-namespace declaration the E14 gold pins) and
// THEN the missing annotation NREs.
TEST_F(HandlersTest, DefAttributeTypeDeferResolvesTheTypeBeforeTheAnnotationRead)
{
    auto ctx = fixture_.MakeContextE();
    auto keyRecord = std::make_unique<Baml::DefAttributeKeyTypeRecord>();
    static_cast<Baml::DefAttributeKeyTypeRecord*>(keyRecord.get())->TypeId = 1;
    KeyPairFixture pair(std::move(keyRecord), false);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    IHandler* handler = Lookup(Baml::BamlRecordType::DefAttributeKeyType);
    auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    try {
        deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
        FAIL() << "the missing-annotation defer must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
    // The ToString mutation landed on the parent BEFORE the throw: the
    // clr-namespace declaration of the mscorlib-backed System.String type.
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent xmlns:system=\"clr-namespace:System\" />");
}

// F9b: the missing-string-id arm -- ResolveString(999) answers the C# null
// and the XElement.Value assignment throws its ArgumentNullException
// ("Value cannot be null. (Parameter 'value')") AFTER the x:Key element is
// attached to the parent.
TEST_F(HandlersTest, DefAttributeStringWithAMissingStringIdThrowsTheNetArgumentNull)
{
    auto ctx = fixture_.MakeContextE();
    auto keyRecord = std::make_unique<Baml::DefAttributeKeyStringRecord>();
    keyRecord->ValueId = 999;
    KeyPairFixture pair(std::move(keyRecord), false);
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    IHandler* handler = Lookup(Baml::BamlRecordType::DefAttributeKeyString);
    std::unique_ptr<BamlElement> translated = handler->Translate(*ctx, *pair.keyNode, &parentElem);
    EXPECT_EQ(translated, nullptr);

    auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    try {
        deferHandler->TranslateDefer(*ctx, *pair.keyNode, &parentElem);
        FAIL() << "the missing-string-id defer must throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(), "Value cannot be null. (Parameter 'value')");
    }
    // The x:Key element was attached before the throw.
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\" />\r\n"
        "</Parent>");
}

// F10: the null-parent TranslateDefer arms -- the annotation present (a
// prior Translate), the parent null at the direct drive.
TEST_F(HandlersTest, KeyHandlersWithANullParentAtTranslateDeferThrowTheNRE)
{
    for (Baml::BamlRecordType type : { Baml::BamlRecordType::DefAttributeKeyString,
            Baml::BamlRecordType::DefAttributeKeyType,
            Baml::BamlRecordType::KeyElementStart }) {
        auto ctx = fixture_.MakeContextE();
        std::unique_ptr<Baml::BamlRecord> keyRecord;
        bool keyIsBlock = false;
        if (type == Baml::BamlRecordType::DefAttributeKeyString) {
            auto record = std::make_unique<Baml::DefAttributeKeyStringRecord>();
            record->ValueId = 0xFFFF;
            keyRecord = std::move(record);
        } else if (type == Baml::BamlRecordType::DefAttributeKeyType) {
            auto record = std::make_unique<Baml::DefAttributeKeyTypeRecord>();
            record->TypeId = 0xFD63;
            keyRecord = std::move(record);
        } else {
            auto record = std::make_unique<Baml::KeyElementStartRecord>();
            static_cast<Baml::KeyElementStartRecord*>(record.get())->TypeId = 0xFD63;
            keyRecord = std::move(record);
            keyIsBlock = true;
        }
        KeyPairFixture pair(std::move(keyRecord), keyIsBlock);
        ASSERT_TRUE(pair.Ok);
        BamlElement parentElem = MakeParentElem();

        IHandler* handler = Lookup(type);
        std::unique_ptr<BamlElement> translated = handler->Translate(*ctx, *pair.keyNode, &parentElem);
        EXPECT_EQ(translated, nullptr);
        ASSERT_NE(pair.KeyOf(*pair.keyNode), nullptr);

        auto* deferHandler = dynamic_cast<ILSpy::BamlDecompiler::IDeferHandler*>(handler);
        ASSERT_NE(deferHandler, nullptr);
        try {
            deferHandler->TranslateDefer(*ctx, *pair.keyNode, nullptr);
            FAIL() << "the null-parent defer must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
}

// ===== the static-resource family (the probe's section G) ===================
//
// The crafted wiring (the probe's MakeStaticPair): the key node, the
// resource node, and the keyed value block as SIBLINGS under the container
// -- the key record's deferred Record pointing at the value block's header,
// the resource registrations BEFORE the consumer-carrying value block in
// document order (the real findtoolbar defer-block layout), and the
// consumers INSIDE the keyed value block whose annotation the ancestors
// walk finds.

namespace {

// The crafted StaticResourceStart block node (the header/footer/text
// bundle): the records move into `owned` (the caller keeps them alive for
// the node tree's raw pointers).
std::unique_ptr<Baml::BamlBlockNode> MakeStaticResourceStartBlock(
    std::vector<std::unique_ptr<Baml::BamlRecord>>& owned)
{
    auto header = std::make_unique<Baml::StaticResourceStartRecord>();
    header->TypeId = 0xFD63;
    auto footer = std::make_unique<Baml::StaticResourceEndRecord>();
    auto text = std::make_unique<Baml::TextRecord>();
    text->Value = "inner";
    auto block = std::make_unique<Baml::BamlBlockNode>();
    block->Header = header.get();
    block->Footer = footer.get();
    auto textNode = std::make_unique<Baml::BamlRecordNode>(text.get());
    textNode->Parent = block.get();
    block->Children.push_back(std::move(textNode));
    owned.push_back(std::move(header));
    owned.push_back(std::move(footer));
    owned.push_back(std::move(text));
    return block;
}

// The crafted OptimizedStaticResource leaf node over its record.
std::unique_ptr<Baml::BamlRecordNode> MakeOptimizedStaticResourceNode(
    std::vector<std::unique_ptr<Baml::BamlRecord>>& owned, std::uint8_t flags,
    std::uint16_t valueId)
{
    auto record = std::make_unique<Baml::OptimizedStaticResourceRecord>();
    record->Flags = flags;
    record->ValueId = valueId;
    auto node = std::make_unique<Baml::BamlRecordNode>(record.get());
    owned.push_back(std::move(record));
    return node;
}

std::unique_ptr<Baml::BamlRecord> MakeStaticIdRecord(std::uint16_t id)
{
    auto record = std::make_unique<Baml::StaticResourceIdRecord>();
    record->StaticResourceId = id;
    return record;
}

std::unique_ptr<Baml::BamlRecord> MakePwidRecord(std::uint16_t attributeId,
    std::uint16_t id)
{
    auto record = std::make_unique<Baml::PropertyWithStaticResourceIdRecord>();
    record->AttributeId = attributeId;
    record->StaticResourceId = id;
    return record;
}

// The crafted static-resource wiring: the container [key, resource node?,
// value block[...consumer]], every child's Parent back-pointer wired (the
// parsed-tree shape -- the Create walk and the ancestors walk read it).
struct StaticPairFixture {
    std::unique_ptr<Baml::BamlRecord> containerHeader;
    std::unique_ptr<Baml::BamlRecord> keyRecord;
    std::unique_ptr<Baml::BamlRecord> valueStart;
    std::unique_ptr<Baml::BamlRecord> valueEnd;
    std::unique_ptr<Baml::BamlRecord> consumerRecord;
    std::vector<std::unique_ptr<Baml::BamlRecord>> ownedRecords;

    Baml::BamlBlockNode container;
    std::unique_ptr<Baml::BamlBlockNode> valueBlock;
    Baml::BamlNode* keyNode = nullptr;
    Baml::BamlNode* resNodePtr = nullptr;
    Baml::BamlBlockNode* valueBlockPtr = nullptr;
    Baml::BamlNode* consumerNode = nullptr;

    // The construction guard the tests assert on first (ASSERT_* inside a
    // constructor fails MSVC C2534 -- the CraftedTree convention).
    bool Ok = false;

    StaticPairFixture(std::unique_ptr<Baml::BamlNode> resNodeArg,
        std::vector<std::unique_ptr<Baml::BamlRecord>> extraRecords,
        std::unique_ptr<Baml::BamlRecord> consumerRecordArg)
    {
        ownedRecords = std::move(extraRecords);
        consumerRecord = std::move(consumerRecordArg);
        keyRecord = std::make_unique<Baml::DefAttributeKeyStringRecord>();
        static_cast<Baml::DefAttributeKeyStringRecord*>(keyRecord.get())->ValueId = 0xFFFF;
        containerHeader = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(containerHeader.get())->TypeId = 0xFD63;
        valueStart = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(valueStart.get())->TypeId = 0xFD63;
        valueEnd = std::make_unique<Baml::ElementEndRecord>();

        // The deferred target: the value block's header record instance
        // (the real defer-block wiring -- Create's children-walk arm
        // annotates the key node and the value block).
        auto* deferRecord = dynamic_cast<Baml::IBamlDeferRecord*>(keyRecord.get());
        if (deferRecord == nullptr)
            return; // unreachable: DefAttributeKeyStringRecord implements the defer interface
        deferRecord->SetRecord(valueStart.get());

        valueBlock = std::make_unique<Baml::BamlBlockNode>();
        valueBlock->Header = valueStart.get();
        valueBlock->Footer = valueEnd.get();
        if (consumerRecord != nullptr) {
            auto consumerHolder = std::make_unique<Baml::BamlRecordNode>(consumerRecord.get());
            consumerHolder->Parent = valueBlock.get();
            consumerNode = consumerHolder.get();
            valueBlock->Children.push_back(std::move(consumerHolder));
        }

        container.Header = containerHeader.get();
        valueBlock->Parent = &container;
        valueBlockPtr = valueBlock.get();

        auto keyHolder = std::make_unique<Baml::BamlRecordNode>(keyRecord.get());
        keyHolder->Parent = &container;
        keyNode = keyHolder.get();
        container.Children.push_back(std::move(keyHolder));

        if (resNodeArg != nullptr) {
            resNodeArg->Parent = &container;
            resNodePtr = resNodeArg.get();
            container.Children.push_back(std::move(resNodeArg));
        }

        container.Children.push_back(std::move(valueBlock));
        Ok = true;
    }

    // The key annotation a node carries (null when none).
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> KeyOf(Baml::BamlNode& node)
    {
        auto* annotation =
            std::any_cast<std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey>>(
                &node.Annotation);
        return annotation == nullptr ? nullptr : *annotation;
    }

    // Runs the KEY handler's Translate over the key node (the Create that
    // annotates the pair) -- the probe's RunKeyCreate.
    void RunKeyCreate(XamlContext& ctx, BamlElement& parentElem)
    {
        IHandler* keyHandler = HandlerMap::LookupHandler(
            Baml::BamlRecordType::DefAttributeKeyString);
        ASSERT_NE(keyHandler, nullptr);
        EXPECT_EQ(keyHandler->Translate(ctx, *keyNode, &parentElem), nullptr);
    }
};

// The osr arm drive (the probe's DriveGDirect): a fresh section-E context,
// the crafted pair, the key Create, the registration, and the defer
// render -- asserting the result and parent renders against the gold.
void DriveOsrArm(XamlContextFixture& fixture, const char* name, std::uint8_t flags,
    std::uint16_t valueId, const std::string& expectedResultRender,
    const std::string& expectedParentRender)
{
    auto ctx = fixture.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto osrNode = MakeOptimizedStaticResourceNode(owned, flags, valueId);
    StaticPairFixture pair(std::move(osrNode), std::move(owned), nullptr);
    ASSERT_TRUE(pair.Ok) << name;
    BamlElement parentElem = MakeParentElem();

    pair.RunKeyCreate(*ctx, parentElem);
    IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::OptimizedStaticResource);
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->Translate(*ctx, *pair.resNodePtr, &parentElem), nullptr) << name;
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr) << name;
    ASSERT_EQ(keyAnn->StaticResources.size(), 1u) << name;
    EXPECT_EQ(keyAnn->StaticResources[0], pair.resNodePtr) << name;

    auto* deferHandler = dynamic_cast<IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr) << name;
    std::unique_ptr<BamlElement> result =
        deferHandler->TranslateDefer(*ctx, *pair.resNodePtr, &parentElem);
    ASSERT_NE(result, nullptr) << name;
    EXPECT_EQ(result->Xaml.Element->ToString(), expectedResultRender) << name;
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), expectedParentRender) << name;
}

} // namespace

// G1: the StaticResourceStart + StaticResourceId end-to-end drive -- the
// container [key, srBlock, valueBlock[StaticId 0]]: the srBlock registers
// under the key, the StaticId consumer inside the keyed value block
// re-renders it through TranslateDefer, and the ElementHandler defer branch
// appends the x:Key element after the walked children.
TEST_F(HandlersTest, StaticResourceStartAndStaticIdRenderEndToEnd)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto srBlock = MakeStaticResourceStartBlock(owned);
    Baml::BamlNode* srBlockPtr = srBlock.get();
    StaticPairFixture pair(std::move(srBlock), std::move(owned), MakeStaticIdRecord(0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    HandlerMap::ProcessChildren(*ctx, pair.container, parentElem);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">\r\n"
        "    <ToolBar>inner</ToolBar>\r\n"
        "    <Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">Name</Key>\r\n"
        "  </ToolBar>\r\n"
        "</Parent>");

    // The registration: the srBlock node sits in the key annotation's
    // StaticResources list (the handler's own Translate during the walk).
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);
    ASSERT_EQ(keyAnn->StaticResources.size(), 1u);
    EXPECT_EQ(keyAnn->StaticResources[0], srBlockPtr);

    // The tree: the parent holds the value doc; the value doc holds the
    // StaticId resElem (the ProcessChildren add of the returned element --
    // the port's single-add form of the C#'s unread double-add) and the
    // Key element.
    ASSERT_EQ(parentElem.Children.size(), 1u);
    BamlElement* valueDoc = parentElem.Children[0].get();
    EXPECT_EQ(valueDoc->Node, pair.valueBlockPtr);
    ASSERT_EQ(valueDoc->Children.size(), 2u);
    EXPECT_EQ(valueDoc->Children[0]->Node, srBlockPtr);
    EXPECT_EQ(valueDoc->Children[0]->Parent, valueDoc);
    EXPECT_EQ(valueDoc->Children[1]->Node, pair.keyNode);
    EXPECT_EQ(valueDoc->Children[1]->Xaml.Element->Name().NamespaceName(),
        "http://schemas.microsoft.com/winfx/2006/xaml");
}

// G2: the OptimizedStaticResource + PropertyWithStaticResourceId
// end-to-end drive -- the container [key, osr(string), valueBlock[PWID
// (String.Length, id 0)]]: the osr registers under the key, the PWID
// consumer renders the property element wrapping the {StaticResource}
// extension element (the default-namespace rebinding the inner
// no-namespace extension element carries inside the pi-namespaced scope).
TEST_F(HandlersTest, OptimizedStaticResourceAndPwidRenderEndToEnd)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto osrNode = MakeOptimizedStaticResourceNode(owned, 0, 0);
    StaticPairFixture pair(std::move(osrNode), std::move(owned), MakePwidRecord(1, 0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    HandlerMap::ProcessChildren(*ctx, pair.container, parentElem);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">\r\n"
        "    <system:String.Length xmlns:system=\"clr-namespace:System\">\r\n"
        "      <StaticResourceExtension xmlns=\"\">\r\n"
        "        <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "      </StaticResourceExtension>\r\n"
        "    </system:String.Length>\r\n"
        "    <Key xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">Name</Key>\r\n"
        "  </ToolBar>\r\n"
        "</Parent>");

    // The registration under the key.
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);
    ASSERT_EQ(keyAnn->StaticResources.size(), 1u);
    EXPECT_EQ(keyAnn->StaticResources[0], pair.resNodePtr);

    // The tree: the value doc holds the PWID doc (with the osr resElem
    // inside it -- the handler's own children add) and the Key element.
    ASSERT_EQ(parentElem.Children.size(), 1u);
    BamlElement* valueDoc = parentElem.Children[0].get();
    ASSERT_EQ(valueDoc->Children.size(), 2u);
    EXPECT_EQ(valueDoc->Children[0]->Node, pair.consumerNode);
    ASSERT_EQ(valueDoc->Children[0]->Children.size(), 1u);
    EXPECT_EQ(valueDoc->Children[0]->Children[0]->Node, pair.resNodePtr);
    EXPECT_EQ(valueDoc->Children[0]->Children[0]->Parent, valueDoc->Children[0].get());
    EXPECT_EQ(valueDoc->Children[1]->Node, pair.keyNode);
}

// G3: the StaticResourceStart direct drive -- Translate registers the
// block, TranslateDefer renders its element into the parent with the
// walked children (the StaticId consumer's re-render target).
TEST_F(HandlersTest, StaticResourceStartDirectTranslateAndDeferRendersTheGold)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto srBlock = MakeStaticResourceStartBlock(owned);
    StaticPairFixture pair(std::move(srBlock), std::move(owned), MakeStaticIdRecord(0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    pair.RunKeyCreate(*ctx, parentElem);
    IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceStart);
    std::unique_ptr<BamlElement> translated =
        handler->Translate(*ctx, *pair.resNodePtr, &parentElem);
    EXPECT_EQ(translated, nullptr);
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        pair.KeyOf(*pair.keyNode);
    ASSERT_NE(keyAnn, nullptr);
    ASSERT_EQ(keyAnn->StaticResources.size(), 1u);
    EXPECT_EQ(keyAnn->StaticResources[0], pair.resNodePtr);

    auto* deferHandler = dynamic_cast<IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    std::unique_ptr<BamlElement> result =
        deferHandler->TranslateDefer(*ctx, *pair.resNodePtr, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Node, pair.resNodePtr);
    EXPECT_EQ(result->Parent, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>\r\n"
        "</Parent>");
    // The walked children's scope annotation roots through the rendered
    // element (the ProcessChildren inside TranslateDefer).
    auto* scope = result->Xaml.Element->Annotation<
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope>>();
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(scope->get()->Element(), result.get());
}

// G4 + G7f: the osr string arm -- the Ctor carries the resolved string
// (the known string id 0 -> "s0"); a MISSING string id answers the C# null
// and the Ctor's XContainer.Add(null) is the null-content no-op (the
// element renders empty).
TEST_F(HandlersTest, OptimizedStaticResourceStringArmRendersTheGold)
{
    DriveOsrArm(fixture_, "string", 0, 0,
        "<StaticResourceExtension>\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "</StaticResourceExtension>",
        "<Parent>\r\n"
        "  <StaticResourceExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "  </StaticResourceExtension>\r\n"
        "</Parent>");
    DriveOsrArm(fixture_, "missingString", 0, 999,
        "<StaticResourceExtension>\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\" />\r\n"
        "</StaticResourceExtension>",
        "<Parent>\r\n"
        "  <StaticResourceExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\" />\r\n"
        "  </StaticResourceExtension>\r\n"
        "</Parent>");
}

// G5: the osr IsType arm -- the Ctor carries the {x:Type} TypeExtension
// element whose own Ctor holds the resolved type's name.
TEST_F(HandlersTest, OptimizedStaticResourceTypeArmRendersTheTypeExtensionChild)
{
    DriveOsrArm(fixture_, "type", 1, 0xFD63,
        "<StaticResourceExtension>\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
        "    <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>\r\n"
        "    </TypeExtension>\r\n"
        "  </Ctor>\r\n"
        "</StaticResourceExtension>",
        "<Parent>\r\n"
        "  <StaticResourceExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
        "      <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "        <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">ToolBar</Ctor>\r\n"
        "      </TypeExtension>\r\n"
        "    </Ctor>\r\n"
        "  </StaticResourceExtension>\r\n"
        "</Parent>");
}

// G6: the osr IsStatic low arm -- the Ctor carries the {x:Static}
// StaticExtension element with the resolved property's prefixed name, and
// the ResolveNamespace mutation attached the clr-namespace declaration to
// the PARENT element before the render.
TEST_F(HandlersTest, OptimizedStaticResourceStaticLowArmRendersTheStaticExtensionChild)
{
    DriveOsrArm(fixture_, "staticLow", 2, 1,
        "<StaticResourceExtension>\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
        "    <StaticExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">system:String.Length</Ctor>\r\n"
        "    </StaticExtension>\r\n"
        "  </Ctor>\r\n"
        "</StaticResourceExtension>",
        "<Parent xmlns:system=\"clr-namespace:System\">\r\n"
        "  <StaticResourceExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
        "      <StaticExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "        <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">system:String.Length</Ctor>\r\n"
        "      </StaticExtension>\r\n"
        "    </Ctor>\r\n"
        "  </StaticResourceExtension>\r\n"
        "</Parent>");
}

// G7: the osr IsStatic high arm matrix -- the ValueId's two's-complement
// bamlId and the SystemResourceIds magic ranges select the KnownThings
// resource row and the *Key-property-name vs resource-name form: the plain
// id form (bamlId 1 and 200), the first magic range (233 -> 1, the
// resource name), the second (465 -> 234, the *Key name), and the third
// (468 -> 234, the resource name).
TEST_F(HandlersTest, OptimizedStaticResourceStaticHighArmsRenderTheResourceRows)
{
    struct Arm {
        std::uint16_t valueId;
        const char* ctorName;
    };
    const Arm arms[] = {
        { 0xFFFF, "SystemColors.ActiveBorderBrushKey" },
        { 0xFF38, "SystemParameters.ForegroundFlashCountKey" },
        { 0xFF17, "SystemColors.ActiveBorderBrush" },
        { 0xFE2F, "SystemColors.InactiveSelectionHighlightBrushKey" },
        { 0xFE2C, "SystemColors.InactiveSelectionHighlightBrush" },
    };
    for (const Arm& arm : arms) {
        const std::string ctorName = arm.ctorName;
        const std::string resultRender =
            "<StaticResourceExtension>\r\n"
            "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
            "    <StaticExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
            "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">" + ctorName +
            "</Ctor>\r\n"
            "    </StaticExtension>\r\n"
            "  </Ctor>\r\n"
            "</StaticResourceExtension>";
        const std::string parentRender =
            "<Parent>\r\n"
            "  <StaticResourceExtension>\r\n"
            "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">\r\n"
            "      <StaticExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
            "        <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">" + ctorName +
            "</Ctor>\r\n"
            "      </StaticExtension>\r\n"
            "    </Ctor>\r\n"
            "  </StaticResourceExtension>\r\n"
            "</Parent>";
        DriveOsrArm(fixture_, arm.ctorName, 2, arm.valueId, resultRender, parentRender);
    }
}

// G8: the StaticId consumer drive -- the ancestors walk finds the value
// block's key, the registered StaticResourceStart re-renders through
// TranslateDefer into the parent, and the returned element's Parent
// back-pointer is set (the port leaves the parent's children-list add to
// the ProcessChildren caller -- the documented divergence; no C# reader
// iterates the list).
TEST_F(HandlersTest, StaticResourceIdConsumerReDrivesTheRegisteredStaticResourceStart)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto srBlock = MakeStaticResourceStartBlock(owned);
    StaticPairFixture pair(std::move(srBlock), std::move(owned), MakeStaticIdRecord(0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    pair.RunKeyCreate(*ctx, parentElem);
    IHandler* resourceHandler = Lookup(Baml::BamlRecordType::StaticResourceStart);
    EXPECT_EQ(resourceHandler->Translate(*ctx, *pair.resNodePtr, &parentElem), nullptr);

    IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
    std::unique_ptr<BamlElement> result =
        handler->Translate(*ctx, *pair.consumerNode, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Node, pair.resNodePtr);
    EXPECT_EQ(result->Parent, &parentElem);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">inner</ToolBar>\r\n"
        "</Parent>");
    EXPECT_TRUE(parentElem.Children.empty());
}

// G9: the StaticId consumer over the registered osr leaf -- the
// {StaticResource} extension element re-renders into the parent.
TEST_F(HandlersTest, StaticResourceIdConsumerReDrivesTheRegisteredOptimizedStaticResource)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto osrNode = MakeOptimizedStaticResourceNode(owned, 0, 0);
    StaticPairFixture pair(std::move(osrNode), std::move(owned), MakeStaticIdRecord(0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    pair.RunKeyCreate(*ctx, parentElem);
    IHandler* resourceHandler = Lookup(Baml::BamlRecordType::OptimizedStaticResource);
    EXPECT_EQ(resourceHandler->Translate(*ctx, *pair.resNodePtr, &parentElem), nullptr);

    IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
    std::unique_ptr<BamlElement> result =
        handler->Translate(*ctx, *pair.consumerNode, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Node, pair.resNodePtr);
    EXPECT_EQ(result->Parent, &parentElem);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<StaticResourceExtension>\r\n"
        "  <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "</StaticResourceExtension>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <StaticResourceExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "  </StaticResourceExtension>\r\n"
        "</Parent>");
}

// G10: the PropertyWithStaticResourceId consumer over the registered osr
// leaf (AttributeId 1 -> String.Length): the property element wraps the
// {StaticResource} extension element, the resElem lands in the doc's own
// children with its back-pointer, and the ResolveNamespace + rename pair
// re-renders the property's prefixed name with the clr-namespace
// declaration attached.
TEST_F(HandlersTest, PropertyWithStaticResourceIdConsumerRendersThePropertyElement)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto osrNode = MakeOptimizedStaticResourceNode(owned, 0, 0);
    StaticPairFixture pair(std::move(osrNode), std::move(owned), MakePwidRecord(1, 0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    pair.RunKeyCreate(*ctx, parentElem);
    IHandler* resourceHandler = Lookup(Baml::BamlRecordType::OptimizedStaticResource);
    EXPECT_EQ(resourceHandler->Translate(*ctx, *pair.resNodePtr, &parentElem), nullptr);

    IHandler* handler = Lookup(Baml::BamlRecordType::PropertyWithStaticResourceId);
    std::unique_ptr<BamlElement> result =
        handler->Translate(*ctx, *pair.consumerNode, &parentElem);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Node, pair.consumerNode);
    EXPECT_EQ(result->Parent, nullptr);
    EXPECT_EQ(result->Xaml.Element->ToString(),
        "<system:String.Length xmlns:system=\"clr-namespace:System\">\r\n"
        "  <StaticResourceExtension>\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "  </StaticResourceExtension>\r\n"
        "</system:String.Length>");
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent>\r\n"
        "  <system:String.Length xmlns:system=\"clr-namespace:System\">\r\n"
        "    <StaticResourceExtension>\r\n"
        "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">s0</Ctor>\r\n"
        "    </StaticResourceExtension>\r\n"
        "  </system:String.Length>\r\n"
        "</Parent>");
    // The doc's own children: the resElem with its back-pointer (the
    // handler's add targets its OWN doc -- faithful to the C#).
    ASSERT_EQ(result->Children.size(), 1u);
    EXPECT_EQ(result->Children[0]->Node, pair.resNodePtr);
    EXPECT_EQ(result->Children[0]->Parent, result.get());
}

// G11 + G12: the registration arms with NO annotated sibling --
// FindKeyInSiblings answers null and the key.StaticResources read NREs
// (for the StaticResourceStart block and the OptimizedStaticResource
// leaf alike).
TEST_F(HandlersTest, ResourceHandlersWithoutAnAnnotatedSiblingThrowTheNRE)
{
    {
        auto ctx = fixture_.MakeContextE();
        std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
        auto srBlock = MakeStaticResourceStartBlock(owned);
        auto header = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(header.get())->TypeId = 0xFD63;
        Baml::BamlBlockNode container;
        container.Header = header.get();
        srBlock->Parent = &container;
        Baml::BamlNode* srBlockPtr = srBlock.get();
        container.Children.push_back(std::move(srBlock));

        IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceStart);
        try {
            handler->Translate(*ctx, *srBlockPtr, nullptr);
            FAIL() << "the unregistered-sibling translate must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
    {
        auto ctx = fixture_.MakeContextE();
        std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
        auto osrNode = MakeOptimizedStaticResourceNode(owned, 0, 0);
        auto header = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(header.get())->TypeId = 0xFD63;
        Baml::BamlBlockNode container;
        container.Header = header.get();
        osrNode->Parent = &container;
        Baml::BamlNode* osrPtr = osrNode.get();
        container.Children.push_back(std::move(osrNode));

        IHandler* handler = Lookup(Baml::BamlRecordType::OptimizedStaticResource);
        try {
            handler->Translate(*ctx, *osrPtr, nullptr);
            FAIL() << "the unregistered-sibling translate must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
}

// G13: the StaticResourceStart TranslateDefer with a NULL parent (the
// registration done first -- the parent.Xaml read NREs).
TEST_F(HandlersTest, StaticResourceStartDeferWithANullParentThrowsTheNRE)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto srBlock = MakeStaticResourceStartBlock(owned);
    StaticPairFixture pair(std::move(srBlock), std::move(owned), MakeStaticIdRecord(0));
    ASSERT_TRUE(pair.Ok);
    BamlElement parentElem = MakeParentElem();

    pair.RunKeyCreate(*ctx, parentElem);
    IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceStart);
    EXPECT_EQ(handler->Translate(*ctx, *pair.resNodePtr, &parentElem), nullptr);

    auto* deferHandler = dynamic_cast<IDeferHandler*>(handler);
    ASSERT_NE(deferHandler, nullptr);
    try {
        deferHandler->TranslateDefer(*ctx, *pair.resNodePtr, nullptr);
        FAIL() << "the null-parent defer must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// G14 + G15: the consumer arms with no usable key up the ancestor chain --
// the plain System.Exception "Cannot find StaticResource @<position>"
// (the crafted records' Position defaults to 0): a consumer under a bare
// container (no key at all), and a consumer inside a keyed value block
// whose key collected no static resources (the do-while walks PAST the
// found key and exhausts the chain).
TEST_F(HandlersTest, StaticResourceIdConsumersWithoutAUsableKeyThrowCannotFind)
{
    {
        auto ctx = fixture_.MakeContextE();
        auto idRecord = std::make_unique<Baml::StaticResourceIdRecord>();
        idRecord->StaticResourceId = 0;
        auto header = std::make_unique<Baml::ElementStartRecord>();
        static_cast<Baml::ElementStartRecord*>(header.get())->TypeId = 0xFD63;
        Baml::BamlBlockNode container;
        container.Header = header.get();
        auto idNode = std::make_unique<Baml::BamlRecordNode>(idRecord.get());
        idNode->Parent = &container;
        Baml::BamlNode* idPtr = idNode.get();
        container.Children.push_back(std::move(idNode));

        IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
        BamlElement parentElem = MakeParentElem();
        try {
            handler->Translate(*ctx, *idPtr, &parentElem);
            FAIL() << "the no-key consumer must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Cannot find StaticResource @0");
        }
    }
    {
        auto ctx = fixture_.MakeContextE();
        std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
        StaticPairFixture pair(nullptr, std::move(owned), MakeStaticIdRecord(0));
        ASSERT_TRUE(pair.Ok);
        BamlElement parentElem = MakeParentElem();

        pair.RunKeyCreate(*ctx, parentElem);
        IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
        try {
            handler->Translate(*ctx, *pair.consumerNode, &parentElem);
            FAIL() << "the empty-key consumer must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Cannot find StaticResource @0");
        }
    }
}

// G16: the do-while's null-parent NRE -- a key annotated DIRECTLY on the
// container (an empty StaticResources list) and an id that stays out of
// range: the second iteration calls FindKeyInAncestors(found.Parent) with
// a null parent (the container has no parent of its own).
TEST_F(HandlersTest, StaticResourceIdWithAnAnnotatedRootAndOutOfRangeIdThrowsTheNRE)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> sourceOwned;
    StaticPairFixture source(nullptr, std::move(sourceOwned), nullptr);
    ASSERT_TRUE(source.Ok);
    BamlElement dummy = MakeParentElem();
    source.RunKeyCreate(*ctx, dummy);
    std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
        source.KeyOf(*source.keyNode);
    ASSERT_NE(keyAnn, nullptr);
    ASSERT_TRUE(keyAnn->StaticResources.empty());

    auto idRecord = std::make_unique<Baml::StaticResourceIdRecord>();
    idRecord->StaticResourceId = 5;
    auto header = std::make_unique<Baml::ElementStartRecord>();
    static_cast<Baml::ElementStartRecord*>(header.get())->TypeId = 0xFD63;
    Baml::BamlBlockNode container;
    container.Header = header.get();
    container.Annotation = keyAnn;
    auto idNode = std::make_unique<Baml::BamlRecordNode>(idRecord.get());
    idNode->Parent = &container;
    Baml::BamlNode* idPtr = idNode.get();
    container.Children.push_back(std::move(idNode));

    IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
    BamlElement parentElem = MakeParentElem();
    try {
        handler->Translate(*ctx, *idPtr, &parentElem);
        FAIL() << "the out-of-range annotated-root consumer must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// G17 + G18: a hand-registered resource node whose handler is not a defer
// handler (a Text node -- the InvalidCastException with the fixed
// '<handler>' placeholder, the documented divergence from the concrete
// class name the real engine renders), and one whose record type has no
// handler at all (a StringInfo node -- the null handler's TranslateDefer
// call NREs).
TEST_F(HandlersTest, StaticResourceIdWithAHandRegisteredForeignResourceThrows)
{
    {
        auto ctx = fixture_.MakeContextE();
        std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
        StaticPairFixture pair(nullptr, std::move(owned), MakeStaticIdRecord(0));
        ASSERT_TRUE(pair.Ok);
        BamlElement parentElem = MakeParentElem();
        pair.RunKeyCreate(*ctx, parentElem);

        auto textRecord = std::make_unique<Baml::TextRecord>();
        textRecord->Value = "not-a-resource";
        auto textNode = std::make_unique<Baml::BamlRecordNode>(textRecord.get());
        Baml::BamlNode* textPtr = textNode.get();
        std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
            pair.KeyOf(*pair.keyNode);
        ASSERT_NE(keyAnn, nullptr);
        keyAnn->StaticResources.push_back(textPtr);

        IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
        try {
            handler->Translate(*ctx, *pair.consumerNode, &parentElem);
            FAIL() << "the non-defer resource must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(),
                "Unable to cast object of type '<handler>' to type "
                "'ICSharpCode.BamlDecompiler.IDeferHandler'.");
        }
    }
    {
        auto ctx = fixture_.MakeContextE();
        std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
        StaticPairFixture pair(nullptr, std::move(owned), MakeStaticIdRecord(0));
        ASSERT_TRUE(pair.Ok);
        BamlElement parentElem = MakeParentElem();
        pair.RunKeyCreate(*ctx, parentElem);

        auto stringInfoRecord = std::make_unique<Baml::StringInfoRecord>();
        auto stringInfoNode = std::make_unique<Baml::BamlRecordNode>(stringInfoRecord.get());
        Baml::BamlNode* stringInfoPtr = stringInfoNode.get();
        std::shared_ptr<ILSpy::BamlDecompiler::Xaml::XamlResourceKey> keyAnn =
            pair.KeyOf(*pair.keyNode);
        ASSERT_NE(keyAnn, nullptr);
        keyAnn->StaticResources.push_back(stringInfoPtr);

        IHandler* handler = Lookup(Baml::BamlRecordType::StaticResourceId);
        try {
            handler->Translate(*ctx, *pair.consumerNode, &parentElem);
            FAIL() << "the unhandled resource must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
}

// ===== the misc family (the probe's section H) =============================

// H1: the XmlnsProperty end-to-end walk (the realistic shape: the record
// inside an element block -- ProcessChildren pushes the scope, the handler
// adds the NamespaceMap and the xmlns attribute, and the element's scope
// annotation carries the rows).
TEST_F(HandlersTest, XmlnsPropertyEndToEndOverAnElementBlockMatchesTheGold)
{
    auto ctx = fixture_.MakeContextE();
    std::vector<std::unique_ptr<Baml::BamlRecord>> owned;
    auto header = std::make_unique<Baml::ElementStartRecord>();
    header->TypeId = 0xFD63;
    auto xmlnsRecord = std::make_unique<Baml::XmlnsPropertyRecord>();
    xmlnsRecord->Prefix = "";
    xmlnsRecord->XmlNamespace = kProbePiNs;
    xmlnsRecord->AssemblyIds = { 0 };
    auto block = std::make_unique<Baml::BamlBlockNode>();
    block->Header = header.get();
    auto leaf = std::make_unique<Baml::BamlRecordNode>(xmlnsRecord.get());
    leaf->Parent = block.get();
    block->Children.push_back(std::move(leaf));
    owned.push_back(std::move(header));
    owned.push_back(std::move(xmlnsRecord));

    // The parent element resolves INTO the declared namespace (the real
    // documents' parents always do -- a no-namespace element carrying a
    // default xmlns declaration throws at serialization).
    BamlElement nodeElem(nullptr);
    nodeElem.Xaml = std::make_shared<Xml::XElement>(
        Xml::XNamespace::Get(kProbePiNs) + "Parent");

    HandlerMap::ProcessChildren(*ctx, *block, nodeElem);
    EXPECT_EQ(nodeElem.Xaml.Element->ToString(), "<Parent xmlns=\"http://probe.pi/ns\" />");

    // The scope the walk pushed (the ProcessChildren annotation): the one
    // plain map over the PresentationFramework record.
    auto* scopeAnn = nodeElem.Xaml.Element
        ->Annotation<std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope>>();
    ASSERT_NE(scopeAnn, nullptr);
    ASSERT_EQ((*scopeAnn)->Maps().size(), 1u);
    EXPECT_EQ((*scopeAnn)->Maps()[0]->ToString(),
        ":[PresentationFramework, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=31bf3856ad364e35|http://probe.pi/ns]");
}

// H2 + H3: the direct drives over a pushed scope -- the prefixed declaration
// (xmlns:p) and the EncodeLocalName wiring (a prefix with a space renders
// xmlns:a_x0020_b while the NamespaceMap keeps the RAW prefix).
TEST_F(HandlersTest, XmlnsPropertyPrefixedAndEscapedPrefixesRenderTheGold)
{
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
        auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
        record->Prefix = "p";
        record->XmlNamespace = kProbePiNs;
        record->AssemblyIds = { 0 };
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<Xml::XElement>("Parent");

        ctx->XmlNs().PushScope(&parentElem);
        EXPECT_EQ(handler->Translate(*ctx, node, &parentElem), nullptr);
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scope = ctx->XmlNs().CurrentScope();
        ctx->XmlNs().PopScope();

        EXPECT_EQ(parentElem.Xaml.Element->ToString(),
            "<Parent xmlns:p=\"http://probe.pi/ns\" />");
        ASSERT_EQ(scope->Maps().size(), 1u);
        EXPECT_EQ(scope->Maps()[0]->ToString(),
            "p:[PresentationFramework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35|http://probe.pi/ns]");
    }
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
        auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
        record->Prefix = "a b";
        record->XmlNamespace = kProbePiNs;
        record->AssemblyIds = { 0 };
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<Xml::XElement>("Parent");

        ctx->XmlNs().PushScope(&parentElem);
        EXPECT_EQ(handler->Translate(*ctx, node, &parentElem), nullptr);
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scope = ctx->XmlNs().CurrentScope();
        ctx->XmlNs().PopScope();

        EXPECT_EQ(parentElem.Xaml.Element->ToString(),
            "<Parent xmlns:a_x0020_b=\"http://probe.pi/ns\" />");
        ASSERT_EQ(scope->Maps().size(), 1u);
        EXPECT_EQ(scope->Maps()[0]->ToString(),
            "a b:[PresentationFramework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35|http://probe.pi/ns]");
    }
}

// H4 + H5: the assembly-id loop -- one plain map per id (the synthetic
// PresentationFramework record AND the main module), and the main-module
// branch with no XmlnsDefinitionAttribute rows adds no clr-namespace maps.
TEST_F(HandlersTest, XmlnsPropertyAddsOneMapPerAssemblyId)
{
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
        auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
        record->Prefix = "";
        record->XmlNamespace = kProbePiNs;
        record->AssemblyIds = { 0, 1 };
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<Xml::XElement>(
            Xml::XNamespace::Get(kProbePiNs) + "Parent");

        ctx->XmlNs().PushScope(&parentElem);
        EXPECT_EQ(handler->Translate(*ctx, node, &parentElem), nullptr);
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scope = ctx->XmlNs().CurrentScope();
        ctx->XmlNs().PopScope();

        EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent xmlns=\"http://probe.pi/ns\" />");
        ASSERT_EQ(scope->Maps().size(), 2u);
        EXPECT_EQ(scope->Maps()[0]->ToString(),
            ":[PresentationFramework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35|http://probe.pi/ns]");
        EXPECT_EQ(scope->Maps()[1]->ToString(),
            ":[mscorlib, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089|http://probe.pi/ns]");
    }
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
        auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
        record->Prefix = "";
        record->XmlNamespace = kProbePiNs;
        record->AssemblyIds = { 1 };
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<Xml::XElement>(
            Xml::XNamespace::Get(kProbePiNs) + "Parent");

        ctx->XmlNs().PushScope(&parentElem);
        EXPECT_EQ(handler->Translate(*ctx, node, &parentElem), nullptr);
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scope = ctx->XmlNs().CurrentScope();
        ctx->XmlNs().PopScope();

        EXPECT_EQ(parentElem.Xaml.Element->ToString(), "<Parent xmlns=\"http://probe.pi/ns\" />");
        // The main-module branch fired but the stub main module carries no
        // XmlnsDefinitionAttribute rows in the E contexts: only the plain map.
        ASSERT_EQ(scope->Maps().size(), 1u);
        EXPECT_EQ(scope->Maps()[0]->ToString(),
            ":[mscorlib, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=b77a5c561934e089|http://probe.pi/ns]");
    }
}

// H8: the CLR-namespaces arm over the section-H context (the probe's real
// PresentationFramework main replaced by the stub main module carrying the
// configured XmlnsDefinitionAttribute rows): the plain map plus one
// clr-namespace map per matching row, in attribute order.
TEST_F(HandlersTest, XmlnsPropertyMainModuleClrRowsMatchTheGoldShape)
{
    auto ctx = fixture_.MakeContextH();
    IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
    auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
    record->Prefix = "";
    record->XmlNamespace = kPresentationXmlns;
    record->AssemblyIds = { 1 };
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<Xml::XElement>(
        Xml::XNamespace::Get(kPresentationXmlns) + "Parent");

    ctx->XmlNs().PushScope(&parentElem);
    EXPECT_EQ(handler->Translate(*ctx, node, &parentElem), nullptr);
    std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scope = ctx->XmlNs().CurrentScope();
    ctx->XmlNs().PopScope();

    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Parent xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" />");
    ASSERT_EQ(scope->Maps().size(), 3u);
    EXPECT_EQ(scope->Maps()[0]->ToString(),
        ":[mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089|http://schemas.microsoft.com/winfx/2006/xaml/presentation]");
    EXPECT_EQ(scope->Maps()[1]->ToString(),
        ":[mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089|System.Windows]");
    EXPECT_EQ(scope->Maps()[2]->ToString(),
        ":[mscorlib, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=b77a5c561934e089|System.Windows.Controls]");
}

// H6 + H7: the exception arms -- the Add without a current scope NREs (the
// XmlnsDictionary convention), and the null-parent drive adds the maps
// FIRST (the scope state survives) and NREs at the parent deref.
TEST_F(HandlersTest, XmlnsPropertyExceptionArmsThrowTheNetNre)
{
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
        auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
        record->Prefix = "";
        record->XmlNamespace = kProbePiNs;
        record->AssemblyIds = { 0 };
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<Xml::XElement>(
            Xml::XNamespace::Get(kProbePiNs) + "Parent");

        try {
            handler->Translate(*ctx, node, &parentElem);
            FAIL() << "the scope-less Add must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = Lookup(Baml::BamlRecordType::XmlnsProperty);
        auto record = std::make_unique<Baml::XmlnsPropertyRecord>();
        record->Prefix = "";
        record->XmlNamespace = kProbePiNs;
        record->AssemblyIds = { 0 };
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem(nullptr);
        parentElem.Xaml = std::make_shared<Xml::XElement>(
            Xml::XNamespace::Get(kProbePiNs) + "Parent");

        ctx->XmlNs().PushScope(&parentElem);
        try {
            handler->Translate(*ctx, node, nullptr);
            FAIL() << "the null-parent translate must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
        // The adds ran before the parent deref: the scope holds the map.
        std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scope = ctx->XmlNs().CurrentScope();
        ctx->XmlNs().PopScope();
        ASSERT_EQ(scope->Maps().size(), 1u);
        EXPECT_EQ(scope->Maps()[0]->ToString(),
            ":[PresentationFramework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35|http://probe.pi/ns]");
    }
}

// The PropertyTypeReference drive helper (the probe's H9/H10): a fresh
// context, the crafted record, the parent element, and the byte-exact
// result/parent render assertions.
void DrivePropertyTypeReference(XamlContextFixture& fixture, const char* name,
    std::uint16_t attributeId, std::uint16_t typeId,
    const std::string& expectedResultRender, const std::string& expectedParentRender)
{
    auto ctx = attributeId == 3 ? fixture.MakeContextH() : fixture.MakeContextE();
    IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyTypeReference);
    ASSERT_NE(handler, nullptr) << name;
    auto record = std::make_unique<Baml::PropertyTypeReferenceRecord>();
    record->AttributeId = attributeId;
    record->TypeId = typeId;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Parent");

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    ASSERT_NE(result, nullptr) << name;
    ASSERT_NE(result->Xaml.Element, nullptr) << name;
    EXPECT_EQ(result->Xaml.Element->ToString(), expectedResultRender) << name;
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), expectedParentRender) << name;
}

// H9: the PropertyTypeReference over String.Length -- the element named in
// the clr-namespace the ToString resolved against the parent (the attached
// child renders with the parent's in-scope prefix), the TypeExtension child
// with the Ctor pseudo-element carrying the prefixed type name.
TEST_F(HandlersTest, PropertyTypeReferenceOverStringRendersTheGold)
{
    DrivePropertyTypeReference(fixture_, "string", 1, 1,
        "<system:String.Length xmlns:system=\"clr-namespace:System\">\r\n"
        "  <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">system:String</Ctor>\r\n"
        "  </TypeExtension>\r\n"
        "</system:String.Length>",
        "<Parent xmlns:system=\"clr-namespace:System\">\r\n"
        "  <system:String.Length>\r\n"
        "    <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">system:String</Ctor>\r\n"
        "    </TypeExtension>\r\n"
        "  </system:String.Length>\r\n"
        "</Parent>");
}

// H10: the TargetTypeAnnotation arm -- the REAL Style.TargetType member
// (FullNameIs matches), so the parent carries the annotation and the
// property element resolves through the assembly's XmlnsDefinitionAttribute
// rows (the presentation namespace, the detached-child default rebind).
TEST_F(HandlersTest, PropertyTypeReferenceStyleAttachesTheTargetTypeAnnotation)
{
    DrivePropertyTypeReference(fixture_, "style", 3, 3,
        "<Style.TargetType xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">\r\n"
        "  <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "    <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">Style</Ctor>\r\n"
        "  </TypeExtension>\r\n"
        "</Style.TargetType>",
        "<Parent>\r\n"
        "  <Style.TargetType xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\">\r\n"
        "    <TypeExtension xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml\">\r\n"
        "      <Ctor xmlns=\"https://github.com/icsharpcode/ILSpy\">Style</Ctor>\r\n"
        "    </TypeExtension>\r\n"
        "  </Style.TargetType>\r\n"
        "</Parent>");

    // The annotation the drive attached: the owning handle holding the
    // resolved Style XamlType.
    auto ctx = fixture_.MakeContextH();
    IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyTypeReference);
    ASSERT_NE(handler, nullptr);
    auto record = std::make_unique<Baml::PropertyTypeReferenceRecord>();
    record->AttributeId = 3;
    record->TypeId = 3;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Parent");
    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    ASSERT_NE(result, nullptr);
    auto* targetAnn = parentElem.Xaml.Element
        ->Annotation<std::shared_ptr<Handlers::TargetTypeAnnotation>>();
    ASSERT_NE(targetAnn, nullptr);
    ASSERT_NE(*targetAnn, nullptr);
    ASSERT_NE((*targetAnn)->Type, nullptr);
    EXPECT_EQ((*targetAnn)->Type->TypeNamespace, "System.Windows");
    EXPECT_EQ((*targetAnn)->Type->TypeName, "Style");
}

// H11: the null-parent NRE (the ToString's parent.Xaml deref).
TEST_F(HandlersTest, PropertyTypeReferenceWithANullParentThrowsTheNRE)
{
    auto ctx = fixture_.MakeContextE();
    IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyTypeReference);
    ASSERT_NE(handler, nullptr);
    auto record = std::make_unique<Baml::PropertyTypeReferenceRecord>();
    record->AttributeId = 1;
    record->TypeId = 1;
    Baml::BamlRecordNode node(record.get());

    try {
        handler->Translate(*ctx, node, nullptr);
        FAIL() << "the null-parent translate must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
    }
}

// The PropertyWithExtension drive helper (the probe's H12-H21): a fresh
// section-E context, the crafted record, an optionally annotated parent,
// and the byte-exact parent render assertion (the handler contributes only
// the attribute).
void DrivePropertyWithExtension(XamlContextFixture& fixture, const char* name,
    std::uint16_t flags, std::uint16_t valueId, bool annotatedParent,
    const std::string& expectedParentRender)
{
    auto ctx = fixture.MakeContextE();
    IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyWithExtension);
    ASSERT_NE(handler, nullptr) << name;
    auto record = std::make_unique<Baml::PropertyWithExtensionRecord>();
    record->AttributeId = 1;
    record->Flags = flags;
    record->ValueId = valueId;
    Baml::BamlRecordNode node(record.get());
    BamlElement parentElem(nullptr);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Parent");
    if (annotatedParent)
        parentElem.Xaml.Element->AddAnnotation(ctx->ResolveTypeOwning(1));

    std::unique_ptr<BamlElement> result = handler->Translate(*ctx, node, &parentElem);
    EXPECT_EQ(result, nullptr) << name;
    EXPECT_EQ(parentElem.Xaml.Element->ToString(), expectedParentRender) << name;
}

// H12 + H13: the TypeExtension arm (Flags 691 == KnownTypes.TypeExtension,
// ValueId 0 = the Button type) -- the plain parent takes the attached
// full-form attribute name; the String-annotated parent takes the short
// form (IsAttachedTo's resolved-member walk finds String.Length).
TEST_F(HandlersTest, PropertyWithExtensionTypeArmRendersTheGold)
{
    DrivePropertyWithExtension(fixture_, "type", 691, 0, false,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "String.Length=\"{markup:Type Button}\" />");
    DrivePropertyWithExtension(fixture_, "typeAnnotated", 691, 0, true,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "Length=\"{markup:Type Button}\" />");
}

// H14: the valTypeExt arm (Flags 0x4000 | 691): the initializer wraps the
// rendered type name in a nested TypeExtension extension.
TEST_F(HandlersTest, PropertyWithExtensionValTypeExtWrapsTheNestedTypeExtension)
{
    DrivePropertyWithExtension(fixture_, "valTypeExt", 0x42B3, 0, false,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "String.Length=\"{markup:Type {markup:Type Button}}\" />");
}

// H15: the TemplateBinding arm (Flags 634 == KnownTypes.
// TemplateBindingExtension, ValueId 1 = the String.Length property): the
// value arm resolves String's namespace against the parent (the xmlns:
// system attach), and the full attribute name renders with that prefix.
TEST_F(HandlersTest, PropertyWithExtensionTemplateBindingArmRendersTheGold)
{
    DrivePropertyWithExtension(fixture_, "templateBinding", 634, 1, false,
        "<Parent xmlns:windows=\"clr-namespace:System.Windows;assembly=PresentationFramework\" "
        "xmlns:system=\"clr-namespace:System\" "
        "system:String.Length=\"{windows:TemplateBinding system:String.Length}\" />");
}

// H16 + H17 + H18: the StaticExtension arms (Flags 602 == KnownTypes.
// StaticExtension): the low id resolves the property, the high id decodes
// through the SystemResourceIds magic ranges into the KnownThings resource
// row (bamlId 1 -> SystemColors.ActiveBorderBrushKey), and the valStaticExt
// flag wraps the initializer in a nested StaticExtension.
TEST_F(HandlersTest, PropertyWithExtensionStaticArmsRenderTheGold)
{
    DrivePropertyWithExtension(fixture_, "staticLow", 602, 1, false,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "xmlns:system=\"clr-namespace:System\" "
        "system:String.Length=\"{markup:Static system:String.Length}\" />");
    DrivePropertyWithExtension(fixture_, "staticHigh", 602, 0xFFFF, false,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "String.Length=\"{markup:Static SystemColors.ActiveBorderBrushKey}\" />");
    // The high arm's first magic range (ValueId 0xFF17 -> bamlId 233 -> the
    // isKey=false resource form).
    DrivePropertyWithExtension(fixture_, "staticHighNonKey", 602, 0xFF17, false,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "String.Length=\"{markup:Static SystemColors.ActiveBorderBrush}\" />");
    DrivePropertyWithExtension(fixture_, "valStaticExt", 0x225A, 1, false,
        "<Parent xmlns:markup=\"clr-namespace:System.Windows.Markup;assembly=PresentationFramework\" "
        "xmlns:system=\"clr-namespace:System\" "
        "system:String.Length=\"{markup:Static {markup:Static system:String.Length}}\" />");
}

// H19: the plain-string arm (Flags 1 = the AccessText known type, ValueId 1
// = the "{Brace}" string): the Escape arm's leading-brace render.
TEST_F(HandlersTest, PropertyWithExtensionStringArmEscapesTheBraces)
{
    DrivePropertyWithExtension(fixture_, "string", 1, 1, false,
        "<Parent String.Length=\"{AccessText {}{Brace}}\" />");
}

// H20 + H21: the exception arms -- the missing string id (ResolveString
// answers null and XamlUtils.Escape(null) NREs) and the null parent (the
// elemType annotation read).
TEST_F(HandlersTest, PropertyWithExtensionExceptionArmsThrowTheNetNre)
{
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyWithExtension);
        ASSERT_NE(handler, nullptr);
        auto record = std::make_unique<Baml::PropertyWithExtensionRecord>();
        record->AttributeId = 1;
        record->Flags = 1;
        record->ValueId = 5;
        Baml::BamlRecordNode node(record.get());
        BamlElement parentElem = MakeParentElem();

        try {
            handler->Translate(*ctx, node, &parentElem);
            FAIL() << "the missing string id must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
    {
        auto ctx = fixture_.MakeContextE();
        IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::PropertyWithExtension);
        ASSERT_NE(handler, nullptr);
        auto record = std::make_unique<Baml::PropertyWithExtensionRecord>();
        record->AttributeId = 1;
        record->Flags = 691;
        record->ValueId = 0;
        Baml::BamlRecordNode node(record.get());

        try {
            handler->Translate(*ctx, node, nullptr);
            FAIL() << "the null-parent translate must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(), "Object reference not set to an instance of an object.");
        }
    }
}
} // namespace
