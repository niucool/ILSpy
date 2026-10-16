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

// Implementation of the Handlers/Blocks port (the block-header handlers).

#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Xaml/XamlResourceKey.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <any>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::BamlDecompiler::Handlers {

namespace {

// The .NET NullReferenceException message (the port's mapping convention).
constexpr const char* kNullReferenceMessage =
    "Object reference not set to an instance of an object.";

// The C# `(BamlBlockNode)node` cast: only a leaf node can fail it (the
// block-opening record types always parse as blocks), so the source type in
// the .NET InvalidCastException message is exact.
Baml::BamlBlockNode& BlockNodeOf(Baml::BamlNode& node)
{
    auto* blockNode = dynamic_cast<Baml::BamlBlockNode*>(&node);
    if (blockNode == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.BamlRecordNode' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.BamlBlockNode'.");
    return *blockNode;
}

// The C# `parent.Xaml.Element` read: a null parent NREs at `parent.Xaml`, a
// string-Xaml parent at the returned null element's use -- both arms carry
// the same .NET message.
Xml::XElement& ParentElementOf(BamlElement* parent)
{
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    return *parent->Xaml.Element;
}

// The C# `(XamlResourceKey)node.Annotation` unboxing cast of the key
// handlers' TranslateDefer bodies (the second copy next to its consumer --
// the Records.cpp original): a null annotation is the .NET
// NullReferenceException (the unbox of a null reference), a foreign payload
// the InvalidCastException with the fixed '<annotation>' placeholder
// (unreachable through the engine).
std::shared_ptr<Xaml::XamlResourceKey> KeyAnnotationOf(Baml::BamlNode& node)
{
    if (!node.Annotation.has_value())
        throw std::runtime_error(kNullReferenceMessage);
    auto* keyAny = std::any_cast<std::shared_ptr<Xaml::XamlResourceKey>>(
        &node.Annotation);
    if (keyAny == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type '<annotation>' to type "
            "'ICSharpCode.BamlDecompiler.Xaml.XamlResourceKey'.");
    return *keyAny;
}

} // namespace

// ===== DocumentHandler ======================================================

Baml::BamlRecordType DocumentHandler::Type() const
{
    return Baml::BamlRecordType::DocumentStart;
}

std::unique_ptr<BamlElement> DocumentHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    // The C# body never touches the parent (the XamlDecompiler.Decompile
    // root call passes null).
    (void)parent;
    // The C# `new BamlElement(node)` + the pseudo-named root element.
    auto doc = std::make_unique<BamlElement>(&node);
    doc->Xaml = std::make_shared<Xml::XElement>(ctx.GetPseudoName("Document"));
    // The C# `HandlerMap.ProcessChildren(ctx, (BamlBlockNode)node, doc)`.
    HandlerMap::ProcessChildren(ctx, BlockNodeOf(node), *doc);
    return doc;
}

// ===== ElementHandler =======================================================

Baml::BamlRecordType ElementHandler::Type() const
{
    return Baml::BamlRecordType::ElementStart;
}

std::unique_ptr<BamlElement> ElementHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    // The C# `var record = (ElementStartRecord)((BamlBlockNode)node).Header`
    // -- a headerless block keeps the C#'s null-cast shape and NREs at the
    // record read below; a non-ElementStart header is the InvalidCastException
    // with the fixed '<record>' placeholder (unreachable through a parsed
    // tree: an ElementStart-typed block always carries the matching header).
    Baml::BamlRecord* header = BlockNodeOf(node).Header;
    if (header == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto* record = dynamic_cast<Baml::ElementStartRecord*>(header);
    if (record == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.ElementStartRecord'.");
    Baml::BamlBlockNode& blockNode = static_cast<Baml::BamlBlockNode&>(node);

    auto doc = std::make_unique<BamlElement>(&node);
    // The C# `ctx.ResolveType(record.TypeId)` -- the owning form: the element
    // annotation below is the type's only root once the context is gone
    // (the XamlContext cache hands the shared handle out).
    std::shared_ptr<Xaml::XamlType> elemType = ctx.ResolveTypeOwning(record->TypeId);
    doc->Xaml = std::make_shared<Xml::XElement>(elemType->ToXName(ctx));

    doc->Xaml.Element->AddAnnotation(elemType);
    // The C# `parent.Xaml.Element.Add(...)`: the null-parent deref NREs.
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    parent->Xaml.Element->Add(doc->Xaml.Element);

    HandlerMap::ProcessChildren(ctx, blockNode, *doc);
    // The C# `if (node.Annotation is XamlResourceKey key && key.KeyNode.Record
    // != node.Record)` -- the key handlers (DefAttributeStringHandler,
    // DefAttributeTypeHandler, KeyElementStartHandler) annotate the key node
    // with the OWNING shared_ptr (the XamlResourceKey.Create convention).
    if (auto* keyAny = std::any_cast<std::shared_ptr<Xaml::XamlResourceKey>>(
            &node.Annotation)) {
        std::shared_ptr<Xaml::XamlResourceKey> key = *keyAny;
        if (key->KeyNode != nullptr && key->KeyNode->Record() != node.Record()) {
            // The C# `(IDeferHandler)HandlerMap.LookupHandler(...)`: a null
            // handler NREs at the TranslateDefer call (the C# null cast
            // succeeds); a non-defer handler is the InvalidCastException with
            // the fixed '<handler>' placeholder. Both arms are unreachable
            // through a real XamlResourceKey (all three key-record handlers
            // implement IDeferHandler).
            IHandler* handler = HandlerMap::LookupHandler(
                key->KeyNode->Record()->Type());
            if (handler == nullptr)
                throw std::runtime_error(kNullReferenceMessage);
            auto* deferHandler = dynamic_cast<IDeferHandler*>(handler);
            if (deferHandler == nullptr)
                throw std::runtime_error(
                    "Unable to cast object of type '<handler>' to type "
                    "'ICSharpCode.BamlDecompiler.IDeferHandler'.");
            std::unique_ptr<BamlElement> keyElem =
                deferHandler->TranslateDefer(ctx, *key->KeyNode, doc.get());
            // The C# `doc.Children.Add(keyElem); keyElem.Parent = doc;` -- the
            // Add does not assign the back-pointer (the BamlElement
            // convention); a null TranslateDefer result NREs at the Parent
            // write.
            BamlElement* rawKeyElem = keyElem.get();
            doc->AddChild(std::move(keyElem));
            if (rawKeyElem == nullptr)
                throw std::runtime_error(kNullReferenceMessage);
            rawKeyElem->Parent = doc.get();
        }
    }

    // The C# pair that attaches the xmlns: ResolveNamespace mutates the type
    // (picking the namespace and appending the xmlns declaration), then the
    // element re-renders its name.
    elemType->ResolveNamespace(*doc->Xaml.Element, ctx);
    doc->Xaml.Element->Name(elemType->ToXName(ctx));
    return doc;
}

// ===== the property-element blocks ==========================================

namespace {

// The shared Translate body of PropertyComplexHandler/PropertyArrayHandler/
// PropertyListHandler/PropertyDictionaryHandler -- the C# classes carry
// literally identical bodies over their own record types (all four derive
// from PropertyComplexStartRecord).
std::unique_ptr<BamlElement> PropertyElementBlock(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    Baml::BamlBlockNode& blockNode = BlockNodeOf(node);
    // The C# `var record = (PropertyComplexStartRecord)((BamlBlockNode)
    // node).Header` -- a headerless block keeps the null cast and NREs at
    // the AttributeId read below.
    Baml::BamlRecord* header = blockNode.Header;
    if (header == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto* record = dynamic_cast<Baml::PropertyComplexStartRecord*>(header);
    if (record == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.PropertyComplexStartRecord'.");

    auto doc = std::make_unique<BamlElement>(&node);
    // The C# `ctx.ResolveProperty(record.AttributeId)` -- the owning form: the
    // element annotation below is the property's only root once the context
    // is gone.
    std::shared_ptr<Xaml::XamlProperty> elemAttr =
        ctx.ResolvePropertyOwning(record->AttributeId);
    // The C# `doc.Xaml = new XElement(elemAttr.ToXName(ctx, null))` -- the
    // name is built BEFORE ResolveNamespace runs, so an unresolved type
    // namespace renders the bare (unqualified) name here; the rename below
    // re-renders it after the namespace attached.
    doc->Xaml = std::make_shared<Xml::XElement>(elemAttr->ToXName(ctx, nullptr));

    doc->Xaml.Element->AddAnnotation(elemAttr);
    // The C# `parent.Xaml.Element.Add(...)`: the null-parent deref NREs.
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    parent->Xaml.Element->Add(doc->Xaml.Element);

    HandlerMap::ProcessChildren(ctx, blockNode, *doc);
    // The C# pair that attaches the xmlns and re-renders the name (the same
    // ElementHandler pair, over the property's own element). The C#
    // `elemAttr.DeclaringType.ResolveNamespace(...)` mutates through the
    // property's readonly `XamlType DeclaringType` reference (legal in C#);
    // the port's non-owning pointer is const, so the cast carries the
    // documented mutation.
    const_cast<Xaml::XamlType*>(elemAttr->DeclaringType)
        ->ResolveNamespace(*doc->Xaml.Element, ctx);
    doc->Xaml.Element->Name(elemAttr->ToXName(ctx, nullptr));
    return doc;
}

} // namespace

Baml::BamlRecordType PropertyComplexHandler::Type() const
{
    return Baml::BamlRecordType::PropertyComplexStart;
}

std::unique_ptr<BamlElement> PropertyComplexHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    return PropertyElementBlock(ctx, node, parent);
}

Baml::BamlRecordType PropertyArrayHandler::Type() const
{
    return Baml::BamlRecordType::PropertyArrayStart;
}

std::unique_ptr<BamlElement> PropertyArrayHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    return PropertyElementBlock(ctx, node, parent);
}

Baml::BamlRecordType PropertyListHandler::Type() const
{
    return Baml::BamlRecordType::PropertyListStart;
}

std::unique_ptr<BamlElement> PropertyListHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    return PropertyElementBlock(ctx, node, parent);
}

Baml::BamlRecordType PropertyDictionaryHandler::Type() const
{
    return Baml::BamlRecordType::PropertyDictionaryStart;
}

std::unique_ptr<BamlElement> PropertyDictionaryHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    return PropertyElementBlock(ctx, node, parent);
}

// ===== ConstructorParametersStartHandler ====================================

Baml::BamlRecordType ConstructorParametersStartHandler::Type() const
{
    return Baml::BamlRecordType::ConstructorParametersStart;
}

std::unique_ptr<BamlElement> ConstructorParametersStartHandler::Translate(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    auto doc = std::make_unique<BamlElement>(&node);
    doc->Xaml = std::make_shared<Xml::XElement>(ctx.GetPseudoName("Ctor"));
    // The C# `parent.Xaml.Element.Add(doc.Xaml.Element)`: the null-parent
    // deref NREs -- BEFORE the (BamlBlockNode)node cast at the ProcessChildren
    // call (a record node adds its element and then throws the cast).
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    parent->Xaml.Element->Add(doc->Xaml.Element);

    HandlerMap::ProcessChildren(ctx, BlockNodeOf(node), *doc);
    return doc;
}

// ===== KeyElementStartHandler ================================================

Baml::BamlRecordType KeyElementStartHandler::Type() const
{
    // The C# `BamlRecordType IHandler.Type => BamlRecordType.KeyElementStart`
    // -- the EXPLICIT interface implementation over the inherited
    // ElementHandler's ElementStart (the port models both as plain
    // overrides; the inherited public members stay reachable through a
    // base-class reference).
    return Baml::BamlRecordType::KeyElementStart;
}

std::unique_ptr<BamlElement> KeyElementStartHandler::Translate(
    XamlContext&, Baml::BamlNode& node, BamlElement*)
{
    // The C# EXPLICIT `IHandler.Translate` -- the Create-and-null body, NOT
    // the inherited ElementHandler walk (the keyed element renders through
    // TranslateDefer below).
    Xaml::XamlResourceKey::Create(node);
    return nullptr;
}

std::unique_ptr<BamlElement> KeyElementStartHandler::TranslateDefer(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    // The C# `var record = (KeyElementStartRecord)((BamlBlockNode)node).Header`
    // -- the double cast is observable (a hand-built lying header); the
    // record payload itself is never read (the keyed element's type comes
    // from the header's ElementStartRecord base through the inherited walk).
    Baml::BamlBlockNode& blockNode = BlockNodeOf(node);
    Baml::BamlRecord* header = blockNode.Header;
    if (header == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto* record = dynamic_cast<Baml::KeyElementStartRecord*>(header);
    if (record == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.KeyElementStartRecord'.");
    (void)record;
    std::shared_ptr<Xaml::XamlResourceKey> key = KeyAnnotationOf(node);
    // The C# `new XElement(ctx.GetKnownNamespace("Key", ..., parent.Xaml))`:
    // the `parent.Xaml` read NREs for a null parent, the Add below for the
    // null element.
    if (parent == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto keyElement = std::make_shared<Xml::XElement>(
        ctx.GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml,
            parent->Xaml.Element.get()));
    auto bamlElem = std::make_unique<BamlElement>(&node);
    bamlElem->Xaml = keyElement;
    ParentElementOf(parent).Add(keyElement);
    key->KeyElement = bamlElem.get();
    // The C# `base.Translate(ctx, node, bamlElem)` -- the inherited
    // ElementHandler walk over the key block's OWN children (the keyed
    // element's content): the walk's header cast accepts the
    // KeyElementStartRecord (a DefAttributeKeyTypeRecord IS an
    // ElementStartRecord), the keyed element lands INSIDE the x:Key element,
    // the walk's own defer branch does not re-enter (the annotation's
    // KeyNode is this very node), and the walk's returned BamlElement is
    // discarded -- the XAML tree carries the content, the walk's own
    // BamlElement children list dies with it.
    ElementHandler::Translate(ctx, node, bamlElem.get());
    return bamlElem;
}

} // namespace ILSpy::BamlDecompiler::Handlers
