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

// Tests for the IHandler/IDeferHandler/HandlerMap port
// (ICSharpCode.BamlDecompiler/IHandlers.cs). Every expectation is pinned
// against the REAL HandlerMap from the installed ICSharpCode.BamlDecompiler.dll
// through the C:/temp-probe/HandlerMapProbe reflection probe:
//  * the registry inventory: the probe dumps the real `handlers` static
//    dictionary after its static ctor ran -- 37 (record type, handler class)
//    rows, byte-backed enum values, 5 IDeferHandler implementors -- the
//    completion target the port's CreateBuiltinHandlers manifest grows
//    toward (one construction line per ported handler; the guard test keeps
//    every landed row inside the gold inventory);
//  * LookupHandler: the release form -- null for every unregistered type
//    (the probe's StringInfo/DocumentEnd lookups), the DEBUG-only
//    NotSupportedException arm compiled out of the shipped assembly;
//  * InstallHandler's duplicate-key message: the probed .NET
//    Dictionary.Add ArgumentException text ("An item with the same key has
//    already been added. Key: Text");
//  * ProcessChildren over the identical XamlContextFixture walk document:
//    the probe drives the real ProcessChildren over the real handler map --
//    the scope before/after is null (the PushScope/PopScope pairing), the
//    parent element's XmlnsScope annotation has Element() == the element
//    itself, the info records are skipped, and the dispatch order is the
//    document order. The port tests drive stub handlers through the same
//    fixture shape for the wiring machinery; the landed real-handler slices
//    (35 rows -- the Handlers/Records leaf handlers, the property-family
//    records, the x:Key defer family, the static-resource family, the misc
//    family (XmlnsProperty, PropertyTypeReference, PropertyWithExtension),
//    and the Handlers/Blocks Document/Element/key/property-element/
//    constructor handlers) are driven
//    end-to-end against
//    the probe's render gold in Handlers_Test.cpp.

#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/XmlnsDictionary.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include "BamlTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ILSpy::Tests::Baml::XamlContextFixture;
using ILSpy::BamlDecompiler::BamlElement;
using ILSpy::BamlDecompiler::HandlerMap;
using ILSpy::BamlDecompiler::IDeferHandler;
using ILSpy::BamlDecompiler::IHandler;
using ILSpy::BamlDecompiler::XamlContext;
namespace Baml = ILSpy::BamlDecompiler::Baml;
namespace Handlers = ILSpy::BamlDecompiler::Handlers;
namespace Xml = ILSpy::Decompiler::Xml;

// ===== the gold registry inventory (the probe's 37 rows) =====================

// One row of the real registry: the record-type VALUE (byte), its name (the
// C# enum member -- also what RecordTypeName must render and what the
// duplicate-key message suffix uses), the handler class the reflection walk
// instantiates, and whether the class also implements IDeferHandler.
struct GoldHandlerRow {
    int value;
    const char* typeName;
    const char* className;
    bool defer;
};

// Dumped from the shipped assembly's `handlers` dictionary
// (C:/temp-probe/handlermap_gold.txt): the CreateBuiltinHandlers completion
// target. Class names diverge from some file names (DefAttributeKeyString-
// Handler.cs declares DefAttributeStringHandler; ConstructorParameters-
// Handler.cs declares ConstructorParametersStartHandler) -- the probe is the
// authority.
constexpr GoldHandlerRow kGoldRegistry[] = {
    { 0x01, "DocumentStart", "DocumentHandler", false },
    { 0x03, "ElementStart", "ElementHandler", false },
    { 0x05, "Property", "PropertyHandler", false },
    { 0x06, "PropertyCustom", "PropertyCustomHandler", false },
    { 0x07, "PropertyComplexStart", "PropertyComplexHandler", false },
    { 0x09, "PropertyArrayStart", "PropertyArrayHandler", false },
    { 0x0b, "PropertyListStart", "PropertyListHandler", false },
    { 0x0d, "PropertyDictionaryStart", "PropertyDictionaryHandler", false },
    { 0x0f, "LiteralContent", "LiteralContentHandler", false },
    { 0x10, "Text", "TextHandler", false },
    { 0x11, "TextWithConverter", "TextWithConverterHandler", false },
    { 0x14, "XmlnsProperty", "XmlnsPropertyHandler", false },
    { 0x19, "DefAttribute", "DefAttributeHandler", false },
    { 0x1b, "PIMapping", "PIMappingHandler", false },
    { 0x1c, "AssemblyInfo", "AssemblyInfoHandler", false },
    { 0x1d, "TypeInfo", "TypeInfoHandler", false },
    { 0x1e, "TypeSerializerInfo", "TypeSerializerInfoHandler", false },
    { 0x1f, "AttributeInfo", "AttributeInfoHandler", false },
    { 0x22, "PropertyTypeReference", "PropertyTypeReferenceHandler", false },
    { 0x23, "PropertyWithExtension", "PropertyWithExtensionHandler", false },
    { 0x24, "PropertyWithConverter", "PropertyWithConverterHandler", false },
    { 0x25, "DeferableContentStart", "DeferableContentStartHandler", false },
    { 0x26, "DefAttributeKeyString", "DefAttributeStringHandler", true },
    { 0x27, "DefAttributeKeyType", "DefAttributeTypeHandler", true },
    { 0x28, "KeyElementStart", "KeyElementStartHandler", true },
    { 0x2a, "ConstructorParametersStart", "ConstructorParametersStartHandler", false },
    { 0x2c, "ConstructorParameterType", "ConstructorParameterTypeHandler", false },
    { 0x2d, "ConnectionId", "ConnectionIdHandler", false },
    { 0x2e, "ContentProperty", "ContentPropertyHandler", false },
    { 0x30, "StaticResourceStart", "StaticResourceStartHandler", true },
    { 0x32, "StaticResourceId", "StaticResourceIdHandler", false },
    { 0x33, "TextWithId", "TextWithIdHandler", false },
    { 0x34, "PresentationOptionsAttribute", "PresentationOptionsAttributeHandler", false },
    { 0x35, "LineNumberAndPosition", "LineNumberAndPositionHandler", false },
    { 0x36, "LinePosition", "LinePositionHandler", false },
    { 0x37, "OptimizedStaticResource", "OptimizedStaticResourceHandler", true },
    { 0x38, "PropertyWithStaticResourceId", "PropertyWithStaticResourceIdHandler", false },
};

// The record types the real registry holds NO handler for (the probe: every
// lookup of these returns null, including the four info records the DEBUG
// LookupHandler arm exempts -- the record walk consumes them instead). These
// stay null FOREVER: the guard test pins that no future manifest row claims
// one of them.
constexpr Baml::BamlRecordType kNeverHandled[] = {
    Baml::BamlRecordType::ClrEvent,
    Baml::BamlRecordType::Comment,
    Baml::BamlRecordType::ConstructorParametersEnd,
    Baml::BamlRecordType::DefTag,
    Baml::BamlRecordType::DocumentEnd,
    Baml::BamlRecordType::ElementEnd,
    Baml::BamlRecordType::EndAttributes,
    Baml::BamlRecordType::KeyElementEnd,
    Baml::BamlRecordType::LastRecordType,
    Baml::BamlRecordType::NamedElementStart,
    Baml::BamlRecordType::ProcessingInstruction,
    Baml::BamlRecordType::PropertyArrayEnd,
    Baml::BamlRecordType::PropertyComplexEnd,
    Baml::BamlRecordType::PropertyDictionaryEnd,
    Baml::BamlRecordType::PropertyListEnd,
    Baml::BamlRecordType::PropertyStringReference,
    Baml::BamlRecordType::RoutedEvent,
    Baml::BamlRecordType::StaticResourceEnd,
    Baml::BamlRecordType::StringInfo,
    Baml::BamlRecordType::XmlAttribute,
};

// ===== stub handlers ========================================================

// A recording IHandler: returns a fresh BamlElement over the node (or null
// with returnNull) and captures everything ProcessChildren hands it,
// including the XmlnsScope current at Translate time.
class RecordingHandler final : public IHandler {
public:
    explicit RecordingHandler(Baml::BamlRecordType type, bool returnNull = false)
        : type_(type)
        , returnNull_(returnNull)
    {
    }

    Baml::BamlRecordType Type() const override { return type_; }

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override
    {
        ctxSeen = &ctx;
        nodeSeen = &node;
        parentSeen = parent;
        scopeSeen = ctx.XmlNs().CurrentScope();
        translateCount++;
        if (returnNull_)
            return nullptr;
        auto elem = std::make_unique<BamlElement>(&node);
        createdElem = elem.get();
        return elem;
    }

    Baml::BamlRecordType type_;
    bool returnNull_;
    XamlContext* ctxSeen = nullptr;
    Baml::BamlNode* nodeSeen = nullptr;
    BamlElement* parentSeen = nullptr;
    std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scopeSeen;
    BamlElement* createdElem = nullptr;
    int translateCount = 0;
};

// An IHandler that recurses like ElementHandler does: builds a fresh element
// over the block node and drives ProcessChildren over the block's own
// children before returning it.
class RecursingHandler final : public IHandler {
public:
    Baml::BamlRecordType Type() const override { return Baml::BamlRecordType::ElementStart; }

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override
    {
        auto elem = std::make_unique<BamlElement>(&node);
        elem->Xaml = std::make_shared<Xml::XElement>("Inner");
        scopeBeforeNested = ctx.XmlNs().CurrentScope();
        HandlerMap::ProcessChildren(ctx, static_cast<Baml::BamlBlockNode&>(node), *elem);
        scopeAfterNested = ctx.XmlNs().CurrentScope();
        createdElem = elem.get();
        return elem;
    }

    std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scopeBeforeNested;
    std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> scopeAfterNested;
    BamlElement* createdElem = nullptr;
};

// A handler implementing both interfaces (the KeyElementStartHandler shape):
// LookupHandler answers the IHandler side, the IDeferHandler side is a
// separate cast at the call site.
class DeferringHandler final : public IHandler, public IDeferHandler {
public:
    Baml::BamlRecordType Type() const override { return Baml::BamlRecordType::KeyElementStart; }

    std::unique_ptr<BamlElement> Translate(XamlContext&, Baml::BamlNode&,
        BamlElement*) override
    {
        deferRan = false;
        return nullptr;
    }

    std::unique_ptr<BamlElement> TranslateDefer(XamlContext&, Baml::BamlNode& node,
        BamlElement*) override
    {
        deferRan = true;
        return std::make_unique<BamlElement>(&node);
    }

    bool deferRan = false;
};

// ===== the HandlerMap registry =============================================

class HandlerMapTest : public ::testing::Test {
protected:
    // The registry is process-global: every test starts from the empty
    // not-yet-populated state and leaves it that way for the next test.
    void SetUp() override { HandlerMap::ClearHandlers(); }
    void TearDown() override { HandlerMap::ClearHandlers(); }

    XamlContextFixture fixture_;
};

// The registry-only tests need no XamlContext; they still reset the global
// registry around themselves.
class HandlerMapRegistryTest : public ::testing::Test {
protected:
    void SetUp() override { HandlerMap::ClearHandlers(); }
    void TearDown() override { HandlerMap::ClearHandlers(); }
};

TEST_F(HandlerMapRegistryTest, LookupReturnsRegisteredHandlerAndNullForMissing)
{
    auto stub = std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text);
    RecordingHandler* raw = stub.get();
    HandlerMap::InstallHandler(std::move(stub));

    EXPECT_EQ(HandlerMap::LookupHandler(Baml::BamlRecordType::Text), raw);
    // The release LookupHandler never throws (the DEBUG-only
    // NotSupportedException arm is compiled out of the shipped assembly):
    // an unregistered type -- never handled in the real registry either --
    // answers null.
    EXPECT_EQ(HandlerMap::LookupHandler(Baml::BamlRecordType::ClrEvent), nullptr);
}

TEST_F(HandlerMapRegistryTest, LookupOfTheInfoRecordsAnswersTheirBuiltinHandlers)
{
    // The three info records the DEBUG arm exempts carry NULL-RETURNING
    // builtin handlers in the real registry (the probe: the AssemblyInfo
    // lookup answers AssemblyInfoHandler); StringInfo carries none (it is
    // one of the never-handled types -- the record walk consumes it).
    for (Baml::BamlRecordType type : { Baml::BamlRecordType::AssemblyInfo,
              Baml::BamlRecordType::TypeInfo,
              Baml::BamlRecordType::AttributeInfo }) {
        IHandler* handler = HandlerMap::LookupHandler(type);
        EXPECT_NE(handler, nullptr) << Baml::RecordTypeName(type);
        EXPECT_EQ(handler->Type(), type);
    }
    EXPECT_EQ(HandlerMap::LookupHandler(Baml::BamlRecordType::StringInfo), nullptr);
}

TEST_F(HandlerMapRegistryTest, InstallDuplicateTypeThrowsTheNetMessage)
{
    auto first = std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text);
    RecordingHandler* firstRaw = first.get();
    HandlerMap::InstallHandler(std::move(first));

    auto second = std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text);
    try {
        HandlerMap::InstallHandler(std::move(second));
        FAIL() << "the duplicate install must throw";
    } catch (const std::invalid_argument& ex) {
        EXPECT_STREQ(ex.what(),
            "An item with the same key has already been added. Key: Text");
    }
    // The first registration survives the rejected duplicate.
    EXPECT_EQ(HandlerMap::LookupHandler(Baml::BamlRecordType::Text), firstRaw);
}

TEST_F(HandlerMapRegistryTest, ClearHandlersResetsToTheUnpopulatedState)
{
    HandlerMap::InstallHandler(std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text));
    EXPECT_NE(HandlerMap::LookupHandler(Baml::BamlRecordType::Text), nullptr);

    HandlerMap::ClearHandlers();
    // The reset re-runs the manifest at the next lookup: the builtin
    // TextHandler row comes back (the manifest is no longer empty).
    IHandler* reloaded = HandlerMap::LookupHandler(Baml::BamlRecordType::Text);
    EXPECT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->Type(), Baml::BamlRecordType::Text);

    // Re-installing after the clear works: the explicit stub shadows the
    // manifest row for its type (the seam precedence -- see the next test).
    auto stub = std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text);
    RecordingHandler* stubRaw = stub.get();
    HandlerMap::ClearHandlers();
    HandlerMap::InstallHandler(std::move(stub));
    EXPECT_EQ(HandlerMap::LookupHandler(Baml::BamlRecordType::Text), stubRaw);
}

TEST_F(HandlerMapRegistryTest, ExplicitlyInstalledHandlersShadowTheManifest)
{
    // The lazy population SKIPS record types an explicit install has already
    // claimed -- the test/embedding-seam precedence (the C# static ctor runs
    // before any user code and its reflection walk is the only writer, so the
    // port's seam must not fight the manifest over a claimed type).
    auto stub = std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text);
    RecordingHandler* stubRaw = stub.get();
    HandlerMap::InstallHandler(std::move(stub));

    // The first lookup populates the manifest; the claimed Text row is
    // skipped and the stub answers.
    EXPECT_EQ(HandlerMap::LookupHandler(Baml::BamlRecordType::Text), stubRaw);
    // The unclaimed rows still install (DocumentStart's builtin row).
    EXPECT_NE(HandlerMap::LookupHandler(Baml::BamlRecordType::DocumentStart), nullptr);
    EXPECT_NE(dynamic_cast<Handlers::DocumentHandler*>(
                  HandlerMap::LookupHandler(Baml::BamlRecordType::DocumentStart)),
        nullptr);
}

TEST_F(HandlerMapTest, LookupHandlerAnswersTheIDeferHandlerCastSite)
{
    auto ctx = fixture_.MakeContext();
    HandlerMap::InstallHandler(std::make_unique<DeferringHandler>());

    IHandler* handler = HandlerMap::LookupHandler(Baml::BamlRecordType::KeyElementStart);
    ASSERT_NE(handler, nullptr);
    // The ElementHandler shape: the IDeferHandler arm is a separate cast on
    // the same instance the map hands out.
    IDeferHandler* defer = dynamic_cast<IDeferHandler*>(handler);
    ASSERT_NE(defer, nullptr);
    Baml::BamlRecordNode node(nullptr);
    BamlElement parent(nullptr);
    std::unique_ptr<BamlElement> deferred = defer->TranslateDefer(*ctx, node, &parent);
    EXPECT_NE(deferred, nullptr);
    EXPECT_TRUE(static_cast<DeferringHandler*>(handler)->deferRan);
}

// ===== ProcessChildren over the walk fixture ================================

TEST_F(HandlerMapTest, ProcessChildrenDispatchesInDocumentOrderAndWiresChildren)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->Children.size(), 6u);

    auto piHandler = std::make_unique<RecordingHandler>(Baml::BamlRecordType::PIMapping);
    auto asmHandler = std::make_unique<RecordingHandler>(Baml::BamlRecordType::AssemblyInfo);
    RecordingHandler* pi = piHandler.get();
    RecordingHandler* asmStub = asmHandler.get();
    HandlerMap::InstallHandler(std::move(piHandler));
    HandlerMap::InstallHandler(std::move(asmHandler));

    BamlElement parentElem(root);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Document");
    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    // The dispatch order is the child order of the block tree (the PIMapping
    // leaf first, the AssemblyInfo leaf second; the StringInfo/TypeInfo/
    // AttributeInfo leaves in between carry the builtin null-returning
    // handlers, and the ElementStart block's builtin ElementHandler runs
    // last -- the real-handler drive below pins its render byte-exactly).
    EXPECT_EQ(pi->translateCount, 1);
    EXPECT_EQ(asmStub->translateCount, 1);
    EXPECT_EQ(pi->ctxSeen, ctx.get());
    EXPECT_EQ(asmStub->ctxSeen, ctx.get());
    EXPECT_EQ(pi->nodeSeen, root->Children[0].get());
    EXPECT_EQ(pi->parentSeen, &parentElem);
    EXPECT_EQ(asmStub->nodeSeen, root->Children[1].get());
    EXPECT_EQ(asmStub->parentSeen, &parentElem);

    // The returned elements land in the parent's children list, in order,
    // each with the Parent back-pointer the Add does not assign (the two
    // stub elements, then the real ElementHandler's ToolBar BamlElement).
    ASSERT_EQ(parentElem.Children.size(), 3u);
    EXPECT_EQ(parentElem.Children[0].get(), pi->createdElem);
    EXPECT_EQ(parentElem.Children[1].get(), asmStub->createdElem);
    EXPECT_EQ(parentElem.Children[0]->Node, root->Children[0].get());
    EXPECT_EQ(parentElem.Children[1]->Node, root->Children[1].get());
    EXPECT_EQ(parentElem.Children[0]->Parent, &parentElem);
    EXPECT_EQ(parentElem.Children[1]->Parent, &parentElem);
    EXPECT_EQ(parentElem.Children[2]->Node, root->Children[5].get());
    EXPECT_EQ(parentElem.Children[2]->Parent, &parentElem);
    EXPECT_EQ(parentElem.Xaml.Element->ToString(),
        "<Document>\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>\r\n"
        "</Document>");
}

TEST_F(HandlerMapTest, ProcessChildrenSkipsUnhandledChildrenAndContinues)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);

    // Only the LAST child (the ElementStart block) carries a stub: the five
    // leaves before it are handled-but-null or skipped (the builtin
    // null-returning handlers for PIMapping/AssemblyInfo/TypeInfo/
    // AttributeInfo, the never-handled StringInfo) and the walk still
    // reaches the block (the release Debug.WriteLine no-op path).
    auto elemHandler = std::make_unique<RecordingHandler>(Baml::BamlRecordType::ElementStart);
    RecordingHandler* elemStub = elemHandler.get();
    HandlerMap::InstallHandler(std::move(elemHandler));

    BamlElement parentElem(root);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Document");
    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    EXPECT_EQ(elemStub->translateCount, 1);
    EXPECT_EQ(elemStub->nodeSeen, root->Children[5].get());
    ASSERT_EQ(parentElem.Children.size(), 1u);
    EXPECT_EQ(parentElem.Children[0]->Node, root->Children[5].get());
}

TEST_F(HandlerMapTest, ProcessChildrenHandlerReturningNullAddsNothing)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);

    // The null-returning stubs claim BOTH the PIMapping leaf and the
    // ElementStart block (whose builtin ElementHandler would otherwise
    // contribute its own element): nothing is wired into the children list.
    HandlerMap::InstallHandler(
        std::make_unique<RecordingHandler>(Baml::BamlRecordType::PIMapping, /*returnNull=*/true));
    HandlerMap::InstallHandler(
        std::make_unique<RecordingHandler>(Baml::BamlRecordType::ElementStart, /*returnNull=*/true));

    BamlElement parentElem(root);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Document");
    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    EXPECT_EQ(parentElem.Children.size(), 0u);
}

TEST_F(HandlerMapTest, ProcessChildrenPushesPopsAndAnnotatesTheElementScope)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);

    auto piHandler = std::make_unique<RecordingHandler>(Baml::BamlRecordType::PIMapping);
    RecordingHandler* pi = piHandler.get();
    HandlerMap::InstallHandler(std::move(piHandler));

    BamlElement parentElem(root);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Document");
    EXPECT_EQ(ctx->XmlNs().CurrentScope(), nullptr);

    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    // During the walk the scope pushed for the parent element is current
    // (the probe: the scope's Element is the element itself).
    ASSERT_NE(pi->scopeSeen, nullptr);
    EXPECT_EQ(pi->scopeSeen->Element(), &parentElem);
    // The scope is popped afterwards, restoring the pre-call state.
    EXPECT_EQ(ctx->XmlNs().CurrentScope(), nullptr);
    // The parent's element carries the scope as an annotation -- the
    // shared_ptr roots it (the C# GC reference the prefix readers fish out).
    auto* annotation = parentElem.Xaml.Element->Annotation<std::shared_ptr<
        ILSpy::BamlDecompiler::XmlnsScope>>();
    ASSERT_NE(annotation, nullptr);
    EXPECT_EQ(annotation->get(), pi->scopeSeen.get());
}

TEST_F(HandlerMapTest, ProcessChildrenWithoutAnElementSkipsTheAnnotation)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);

    HandlerMap::InstallHandler(
        std::make_unique<RecordingHandler>(Baml::BamlRecordType::PIMapping));
    // The ElementStart block's builtin ElementHandler would NRE on the
    // string-Xaml parent (the real engine throws there); the stub keeps
    // this test on ProcessChildren's own no-element guard.
    HandlerMap::InstallHandler(
        std::make_unique<RecordingHandler>(Baml::BamlRecordType::ElementStart, /*returnNull=*/true));

    // A string XamlNode (no XElement): the C# `if (nodeElem.Xaml.Element !=
    // null)` guard skips the annotation; the scope pairing still runs.
    BamlElement parentElem(root);
    parentElem.Xaml = std::string("text");
    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    EXPECT_EQ(ctx->XmlNs().CurrentScope(), nullptr);
    EXPECT_EQ(parentElem.Children.size(), 1u);
}

TEST_F(HandlerMapTest, ProcessChildrenNestedWalksStackTheirScopes)
{
    auto ctx = fixture_.MakeContext();
    Baml::BamlBlockNode* root = ctx->RootNode();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->Children.size(), 6u);

    auto recursing = std::make_unique<RecursingHandler>();
    auto textHandler = std::make_unique<RecordingHandler>(Baml::BamlRecordType::Text);
    RecursingHandler* elemHandler = recursing.get();
    RecordingHandler* text = textHandler.get();
    HandlerMap::InstallHandler(std::move(recursing));
    HandlerMap::InstallHandler(std::move(textHandler));

    BamlElement parentElem(root);
    parentElem.Xaml = std::make_shared<Xml::XElement>("Document");
    HandlerMap::ProcessChildren(*ctx, *root, parentElem);

    // The outer scope (pushed for parentElem) is current when the recursing
    // handler runs and again right after its nested ProcessChildren returns
    // (the probe's before/after null pairing, one level down).
    std::shared_ptr<ILSpy::BamlDecompiler::XmlnsScope> outerScope = elemHandler->scopeBeforeNested;
    ASSERT_NE(outerScope, nullptr);
    EXPECT_EQ(outerScope->Element(), &parentElem);
    EXPECT_EQ(elemHandler->scopeAfterNested.get(), outerScope.get());

    // The nested walk pushed its own scope for the fresh inner element
    // (chained to the outer one) and the inner Text handler saw it.
    ASSERT_NE(text->scopeSeen, nullptr);
    EXPECT_EQ(text->scopeSeen->Element(), elemHandler->createdElem);
    EXPECT_EQ(text->scopeSeen->PreviousScope().get(), outerScope.get());
    EXPECT_EQ(text->parentSeen, elemHandler->createdElem);

    // The nested element wired its Text child; the outer walk wired the
    // nested element into the parent.
    ASSERT_EQ(parentElem.Children.size(), 1u);
    EXPECT_EQ(parentElem.Children[0].get(), elemHandler->createdElem);
    EXPECT_EQ(parentElem.Children[0]->Parent, &parentElem);
    ASSERT_EQ(elemHandler->createdElem->Children.size(), 1u);
    EXPECT_EQ(elemHandler->createdElem->Children[0]->Parent, elemHandler->createdElem);

    // Everything unwound back to the pre-call state.
    EXPECT_EQ(ctx->XmlNs().CurrentScope(), nullptr);
}

// ===== the gold inventory guard =============================================

TEST(HandlerMapGoldRegistryTest, GoldRegistryHasThe37ProbedRows)
{
    EXPECT_EQ(std::size(kGoldRegistry), 37u);
    // Distinct record types, and the enum values/names the probe dumped
    // render through RecordTypeName.
    std::vector<int> values;
    for (const GoldHandlerRow& row : kGoldRegistry) {
        values.push_back(row.value);
        auto type = static_cast<Baml::BamlRecordType>(row.value);
        EXPECT_EQ(Baml::RecordTypeName(type), row.typeName);
    }
    std::sort(values.begin(), values.end());
    EXPECT_EQ(std::adjacent_find(values.begin(), values.end()), values.end());
    // The five IDeferHandler implementors the probe pinned
    // (DefAttributeStringHandler, DefAttributeTypeHandler,
    // KeyElementStartHandler, StaticResourceStartHandler,
    // OptimizedStaticResourceHandler).
    EXPECT_EQ(std::count_if(std::begin(kGoldRegistry), std::end(kGoldRegistry),
                  [](const GoldHandlerRow& row) { return row.defer; }),
        5);
}

TEST(HandlerMapGoldRegistryTest, NeverHandledTypesAreOutsideTheGoldRegistry)
{
    for (Baml::BamlRecordType type : kNeverHandled) {
        const bool handled = std::any_of(std::begin(kGoldRegistry), std::end(kGoldRegistry),
            [type](const GoldHandlerRow& row) {
                return static_cast<Baml::BamlRecordType>(row.value) == type;
            });
        EXPECT_FALSE(handled) << Baml::RecordTypeName(type);
    }
    EXPECT_EQ(std::size(kNeverHandled), 20u);
}

TEST(HandlerMapGoldRegistryTest, BuiltinManifestStaysInsideTheGoldInventory)
{
    // Every manifest row must be one of the probed registry's 37 -- and
    // never one of the types the real registry holds no handler for at all
    // (the landed slices: the Handlers/Records and Handlers/Blocks classes
    // ported so far).
    for (const auto& handler : HandlerMap::CreateBuiltinHandlers()) {
        const int value = static_cast<int>(handler->Type());
        const bool known = std::any_of(std::begin(kGoldRegistry), std::end(kGoldRegistry),
            [value](const GoldHandlerRow& row) { return row.value == value; });
        EXPECT_TRUE(known) << "handler for a record type outside the gold registry: "
                          << Baml::RecordTypeName(handler->Type());
        const bool never = std::find(std::begin(kNeverHandled), std::end(kNeverHandled),
                              handler->Type()) != std::end(kNeverHandled);
        EXPECT_FALSE(never) << Baml::RecordTypeName(handler->Type());
    }
}

TEST(BamlRecordTypeNameTest, NamesAndTheUnnamedValueFallback)
{
    EXPECT_EQ(Baml::RecordTypeName(Baml::BamlRecordType::DocumentStart), "DocumentStart");
    EXPECT_EQ(Baml::RecordTypeName(Baml::BamlRecordType::PIMapping), "PIMapping");
    EXPECT_EQ(Baml::RecordTypeName(Baml::BamlRecordType::StringInfo), "StringInfo");
    EXPECT_EQ(Baml::RecordTypeName(Baml::BamlRecordType::LastRecordType), "LastRecordType");
    // The .NET enum ToString fallback: a value with no member renders as the
    // decimal of the underlying byte.
    EXPECT_EQ(Baml::RecordTypeName(static_cast<Baml::BamlRecordType>(0x00)), "0");
    EXPECT_EQ(Baml::RecordTypeName(static_cast<Baml::BamlRecordType>(0x3a)), "58");
    EXPECT_EQ(Baml::RecordTypeName(static_cast<Baml::BamlRecordType>(0xff)), "255");
}

} // namespace
