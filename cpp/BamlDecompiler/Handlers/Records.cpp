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

// Implementation of the Handlers/Records port (the leaf-record handlers).
// The header carries the porting decisions; this file is the translations.

#include "BamlDecompiler/BamlConnectionId.hpp"
#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Records.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlResourceKey.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/Xaml/XamlUtils.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/Xml/XAttribute.hpp"

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ILSpy::BamlDecompiler::Handlers {

namespace {

// The .NET NullReferenceException message (the port's mapping convention).
constexpr const char* kNullReferenceMessage =
    "Object reference not set to an instance of an object.";

// The C# `(<RecordClass>)((BamlRecordNode)node).Record` double cast: both
// casts throw .NET InvalidCastException on a node (or record) of another
// type; only a hand-built lying tree reaches either arm, and the source type
// renders through the fixed '<node>'/'<record>' placeholder (the
// XamlResourceKey convention).
template <typename TRecord>
TRecord& CheckedRecord(Baml::BamlNode& node, const char* recordClassName)
{
    auto* recordNode = dynamic_cast<Baml::BamlRecordNode*>(&node);
    if (recordNode == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<node>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.BamlRecordNode'.");
    auto* record = dynamic_cast<TRecord*>(recordNode->Record());
    if (record == nullptr)
        throw std::runtime_error(std::string(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.") + recordClassName + "'.");
    return *record;
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

// The C# `(BamlBlockNode)node` cast of the static-resource block handlers
// (the Blocks.cpp BlockNodeOf twin): only a leaf node can fail it (the
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

// The C# `ctx.GetKnownNamespace(name, ...)` over a null-resolved string id:
// the `xNs + name` operator hands the null to XName.Get, whose
// ArgumentNullException arm the port maps with the probed .NET message.
std::string CheckedStringId(XamlContext& ctx, std::uint16_t id)
{
    std::optional<std::string> name = ctx.ResolveString(id);
    if (!name)
        throw std::invalid_argument("Value cannot be null. (Parameter 'localName')");
    return std::move(*name);
}

// The C# `(XamlResourceKey)node.Annotation` unboxing cast of the key
// handlers' TranslateDefer bodies: a null annotation is the .NET
// NullReferenceException (the unbox of a null reference -- the gold-pinned
// missing-Create arm), a foreign payload the InvalidCastException --
// unreachable through the engine (only XamlResourceKey.Create sets the key
// annotation), so the source type renders through the fixed '<annotation>'
// placeholder (the XamlResourceKey convention).
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

// ===== PropertyHandler ======================================================

Baml::BamlRecordType PropertyHandler::Type() const
{
    return Baml::BamlRecordType::Property;
}

std::unique_ptr<BamlElement> PropertyHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    Baml::PropertyRecord& record = CheckedRecord<Baml::PropertyRecord>(
        node, "PropertyRecord");
    // The C# `parent.Xaml.Element.Annotation<XamlType>()` -- the parent read
    // precedes the property resolution, and the annotation is the OWNING
    // shared_ptr the ElementHandler attached (a plain parent carries none,
    // and a null elemType takes the attached arm below).
    Xml::XElement& parentElement = ParentElementOf(parent);
    Xaml::XamlType* elemType = nullptr;
    if (auto* annotation = parentElement.Annotation<std::shared_ptr<Xaml::XamlType>>())
        elemType = annotation->get();
    std::shared_ptr<Xaml::XamlProperty> xamlProp = ctx.ResolvePropertyOwning(
        record.AttributeId);
    std::string value = Xaml::Escape(record.Value);
    // The C# `xamlProp.DeclaringType.ResolveNamespace(parent.Xaml, ctx)` --
    // the mutation the arm selection below reads (the xmlns lands on the
    // parent before the attribute is built). The C# mutates through the
    // property's readonly `XamlType DeclaringType` reference (legal in C#);
    // the port's non-owning pointer is const, so the cast carries the
    // documented mutation.
    const_cast<Xaml::XamlType*>(xamlProp->DeclaringType)
        ->ResolveNamespace(parentElement, ctx);

    // The C# local function `ConstructXAttribute()`, evaluated at Add time
    // (after the ResolveNamespace mutation). The arm order: attached,
    // x:Name, plain.
    std::shared_ptr<Xml::XAttribute> attribute;
    if (xamlProp->IsAttachedTo(elemType)) {
        attribute = std::make_shared<Xml::XAttribute>(
            xamlProp->ToXName(ctx, &parentElement, true), value);
    } else if (xamlProp->PropertyName == "Name") {
        // The C# `elemType.ResolvedType.GetDefinition()?.ParentModule.IsMainModule
        // == true` -- the PropertyName == "Name" short-circuit guard means a
        // null elemType never reaches here (IsAttachedTo(null) is true), but a
        // null ResolvedType is the C# NRE at the GetDefinition call.
        if (elemType->ResolvedType == nullptr)
            throw std::runtime_error(kNullReferenceMessage);
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* definition =
            elemType->ResolvedType->GetDefinition();
        const ILSpy::Decompiler::TypeSystem::IModule* module =
            definition != nullptr ? definition->ParentModule() : nullptr;
        if (module != nullptr && module->IsMainModule())
            attribute = std::make_shared<Xml::XAttribute>(
                ctx.GetKnownNamespace("Name", XamlContext::KnownNamespace_Xaml,
                    &parentElement),
                value);
    }
    if (attribute == nullptr)
        attribute = std::make_shared<Xml::XAttribute>(
            xamlProp->ToXName(ctx, &parentElement, false), value);
    parentElement.Add(std::move(attribute));
    return nullptr;
}

// ===== PropertyWithConverterHandler =========================================

Baml::BamlRecordType PropertyWithConverterHandler::Type() const
{
    return Baml::BamlRecordType::PropertyWithConverter;
}

// ===== ConstructorParameterTypeHandler =======================================

Baml::BamlRecordType ConstructorParameterTypeHandler::Type() const
{
    return Baml::BamlRecordType::ConstructorParameterType;
}

std::unique_ptr<BamlElement> ConstructorParameterTypeHandler::Translate(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    Baml::ConstructorParameterTypeRecord& record =
        CheckedRecord<Baml::ConstructorParameterTypeRecord>(
            node, "ConstructorParameterTypeRecord");
    // The C# `ctx.GetKnownNamespace("TypeExtension", ..., parent.Xaml)` -- the
    // `parent.Xaml` read NREs for a null parent, while a string-Xaml parent
    // hands the null element to the context parameter (no default-namespace
    // collapse; the Add below still NREs).
    if (parent == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    Xml::XElement* context = parent->Xaml.Element.get();
    auto elem = std::make_shared<Xml::XElement>(
        ctx.GetKnownNamespace("TypeExtension", XamlContext::KnownNamespace_Xaml,
            context));
    // The C# `elem.AddAnnotation(ctx.ResolveType(0xfd4d))` -- the known type
    // TypeExtension (the `(ushort)(-index)` wire form), held by the OWNING
    // annotation handle.
    elem->AddAnnotation(ctx.ResolveTypeOwning(0xfd4d));

    auto bamlElem = std::make_unique<BamlElement>(&node);
    bamlElem->Xaml = elem;
    ParentElementOf(parent).Add(elem);

    // The C# `var type = ctx.ResolveType(record.TypeId); var typeName =
    // ctx.ToString(parent.Xaml, type)` -- the ToString resolves the type's
    // namespace against the PARENT element (the mutation the prefix render
    // reads).
    std::shared_ptr<Xaml::XamlType> type = ctx.ResolveTypeOwning(record.TypeId);
    std::string typeName = Xaml::ToString(ctx, ParentElementOf(parent), *type);
    elem->Add(std::make_shared<Xml::XElement>(ctx.GetPseudoName("Ctor"), typeName));
    return bamlElem;
}

// ===== TextHandler ==========================================================

Baml::BamlRecordType TextHandler::Type() const
{
    return Baml::BamlRecordType::Text;
}

std::unique_ptr<BamlElement> TextHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    (void)ctx;
    Baml::TextRecord& record = CheckedRecord<Baml::TextRecord>(node, "TextRecord");
    ParentElementOf(parent).Add(record.Value);
    return nullptr;
}

// ===== TextWithIdHandler ====================================================

Baml::BamlRecordType TextWithIdHandler::Type() const
{
    return Baml::BamlRecordType::TextWithId;
}

std::unique_ptr<BamlElement> TextWithIdHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    Baml::TextWithIdRecord& record = CheckedRecord<Baml::TextWithIdRecord>(
        node, "TextWithIdRecord");
    // The C# `parent.Xaml.Element.Add(ctx.ResolveString(record.ValueId))`:
    // the receiver is evaluated first (the null-parent/element NRE fires
    // before the id resolves), and the missing-low-id null flows into
    // XContainer.Add(null) -- the C# null-content no-op (nothing is added).
    Xml::XElement& element = ParentElementOf(parent);
    std::optional<std::string> value = ctx.ResolveString(record.ValueId);
    if (value)
        element.Add(*value);
    return nullptr;
}

// ===== TextWithConverterHandler =============================================

Baml::BamlRecordType TextWithConverterHandler::Type() const
{
    return Baml::BamlRecordType::TextWithConverter;
}

// ===== ConnectionIdHandler ==================================================

Baml::BamlRecordType ConnectionIdHandler::Type() const
{
    return Baml::BamlRecordType::ConnectionId;
}

std::unique_ptr<BamlElement> ConnectionIdHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    (void)ctx;
    Baml::ConnectionIdRecord& record = CheckedRecord<Baml::ConnectionIdRecord>(
        node, "ConnectionIdRecord");
    // The owning shared_ptr: the annotation is the payload's only root once
    // the context is gone (the C# GC reference -- see BamlConnectionId.hpp).
    ParentElementOf(parent).AddAnnotation(
        std::make_shared<BamlConnectionId>(record.ConnectionId));
    return nullptr;
}

// ===== DefAttributeHandler ==================================================

Baml::BamlRecordType DefAttributeHandler::Type() const
{
    return Baml::BamlRecordType::DefAttribute;
}

std::unique_ptr<BamlElement> DefAttributeHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    Baml::DefAttributeRecord& record = CheckedRecord<Baml::DefAttributeRecord>(
        node, "DefAttributeRecord");
    std::string attrName = CheckedStringId(ctx, record.NameId);
    ParentElementOf(parent).Add(std::make_shared<Xml::XAttribute>(
        ctx.GetKnownNamespace(attrName, XamlContext::KnownNamespace_Xaml), record.Value));
    return nullptr;
}

// ===== PresentationOptionsAttributeHandler ==================================

Baml::BamlRecordType PresentationOptionsAttributeHandler::Type() const
{
    return Baml::BamlRecordType::PresentationOptionsAttribute;
}

std::unique_ptr<BamlElement> PresentationOptionsAttributeHandler::Translate(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    Baml::PresentationOptionsAttributeRecord& record =
        CheckedRecord<Baml::PresentationOptionsAttributeRecord>(
            node, "PresentationOptionsAttributeRecord");
    // The C# reads `parent.Xaml` (the implicit XElement -- possibly null for
    // a string-Xaml parent) as the GetKnownNamespace context: a null context
    // skips the default-namespace collapse, and the null element NREs at the
    // Add below.
    Xml::XElement* context = parent != nullptr ? parent->Xaml.Element.get() : nullptr;
    std::string attrName = CheckedStringId(ctx, record.NameId);
    auto attribute = std::make_shared<Xml::XAttribute>(
        ctx.GetKnownNamespace(attrName,
            XamlContext::KnownNamespace_PresentationOptions, context),
        record.Value);
    ParentElementOf(parent).Add(std::move(attribute));
    return nullptr;
}

// ===== DefAttributeStringHandler ===========================================

Baml::BamlRecordType DefAttributeStringHandler::Type() const
{
    return Baml::BamlRecordType::DefAttributeKeyString;
}

std::unique_ptr<BamlElement> DefAttributeStringHandler::Translate(
    XamlContext&, Baml::BamlNode& node, BamlElement*)
{
    // The C# `XamlResourceKey.Create(node); return null;` -- the handler
    // contributes no element; the Create call is the whole body (the key
    // annotation pair the value element's ElementHandler defer branch
    // reads).
    Xaml::XamlResourceKey::Create(node);
    return nullptr;
}

std::unique_ptr<BamlElement> DefAttributeStringHandler::TranslateDefer(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    Baml::DefAttributeKeyStringRecord& record =
        CheckedRecord<Baml::DefAttributeKeyStringRecord>(
            node, "DefAttributeKeyStringRecord");
    std::shared_ptr<Xaml::XamlResourceKey> key = KeyAnnotationOf(node);
    // The C# `new XElement(ctx.GetKnownNamespace("Key", ..., parent.Xaml))`:
    // the `parent.Xaml` read NREs for a null parent, and the Add below NREs
    // for a string-Xaml parent's null element (the GetKnownNamespace context
    // parameter takes the null element without a throw).
    if (parent == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto keyElement = std::make_shared<Xml::XElement>(
        ctx.GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml,
            parent->Xaml.Element.get()));
    auto bamlElem = std::make_unique<BamlElement>(&node);
    bamlElem->Xaml = keyElement;
    ParentElementOf(parent).Add(keyElement);
    // The C# `bamlElem.Xaml.Element.Value = ctx.ResolveString(record.ValueId)`
    // -- the missing-id arm hands the null to the XElement.Value setter,
    // whose ArgumentNullException the gold pinned (AFTER the x:Key element
    // is attached to the parent).
    std::optional<std::string> value = ctx.ResolveString(record.ValueId);
    if (!value)
        throw std::invalid_argument("Value cannot be null. (Parameter 'value')");
    keyElement->Value(std::move(*value));
    // The C# `key.KeyElement = bamlElem` -- the non-owning back-pointer the
    // StaticResource consumer lookups read.
    key->KeyElement = bamlElem.get();
    return bamlElem;
}

// ===== DefAttributeTypeHandler ==============================================

Baml::BamlRecordType DefAttributeTypeHandler::Type() const
{
    return Baml::BamlRecordType::DefAttributeKeyType;
}

std::unique_ptr<BamlElement> DefAttributeTypeHandler::Translate(
    XamlContext&, Baml::BamlNode& node, BamlElement*)
{
    // The C# `XamlResourceKey.Create(node); return null;`.
    Xaml::XamlResourceKey::Create(node);
    return nullptr;
}

std::unique_ptr<BamlElement> DefAttributeTypeHandler::TranslateDefer(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    Baml::DefAttributeKeyTypeRecord& record =
        CheckedRecord<Baml::DefAttributeKeyTypeRecord>(
            node, "DefAttributeKeyTypeRecord");
    // The C# order: `ctx.ResolveType(record.TypeId)` THEN `ctx.ToString(
    // parent.Xaml, type)` -- the parent read NREs HERE, BEFORE the annotation
    // cast (the order the gold pinned: the ToString mutation lands on the
    // parent even when the missing annotation NREs right after).
    std::shared_ptr<Xaml::XamlType> type = ctx.ResolveTypeOwning(record.TypeId);
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    std::string typeName = Xaml::ToString(ctx, *parent->Xaml.Element, *type);
    std::shared_ptr<Xaml::XamlResourceKey> key = KeyAnnotationOf(node);
    auto keyElement = std::make_shared<Xml::XElement>(
        ctx.GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml,
            parent->Xaml.Element.get()));
    auto bamlElem = std::make_unique<BamlElement>(&node);
    bamlElem->Xaml = keyElement;
    ParentElementOf(parent).Add(keyElement);
    // The C# TypeExtension child: the {x:Type} extension element (the known
    // type annotation id 0xfd4d) wrapping the Ctor pseudo-element carrying
    // the resolved type's name.
    auto typeElem = std::make_shared<Xml::XElement>(
        ctx.GetKnownNamespace("TypeExtension", XamlContext::KnownNamespace_Xaml,
            parent->Xaml.Element.get()));
    typeElem->AddAnnotation(ctx.ResolveTypeOwning(0xfd4d));
    typeElem->Add(std::make_shared<Xml::XElement>(
        ctx.GetPseudoName("Ctor"), std::move(typeName)));
    keyElement->Add(typeElem);
    key->KeyElement = bamlElem.get();
    return bamlElem;
}

// ===== the null-returning handlers ==========================================

Baml::BamlRecordType AssemblyInfoHandler::Type() const
{
    return Baml::BamlRecordType::AssemblyInfo;
}

std::unique_ptr<BamlElement> AssemblyInfoHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

Baml::BamlRecordType AttributeInfoHandler::Type() const
{
    return Baml::BamlRecordType::AttributeInfo;
}

std::unique_ptr<BamlElement> AttributeInfoHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

Baml::BamlRecordType ContentPropertyHandler::Type() const
{
    return Baml::BamlRecordType::ContentProperty;
}

std::unique_ptr<BamlElement> ContentPropertyHandler::Translate(XamlContext&,
    Baml::BamlNode& node, BamlElement*)
{
    // The C# body casts the record (`var record = ...` before its `// TODO:
    // What to do here?` null return) -- the cast check is the observable
    // behavior.
    (void)CheckedRecord<Baml::ContentPropertyRecord>(node, "ContentPropertyRecord");
    return nullptr;
}

Baml::BamlRecordType DeferableContentStartHandler::Type() const
{
    return Baml::BamlRecordType::DeferableContentStart;
}

std::unique_ptr<BamlElement> DeferableContentStartHandler::Translate(XamlContext&,
    Baml::BamlNode& node, BamlElement*)
{
    // The C# casts the record, then its Debug.Assert footer check is
    // compiled out of the release assembly the tool ships -- the release
    // handler is the null return.
    (void)CheckedRecord<Baml::DeferableContentStartRecord>(
        node, "DeferableContentStartRecord");
    return nullptr;
}

Baml::BamlRecordType LineNumberAndPositionHandler::Type() const
{
    return Baml::BamlRecordType::LineNumberAndPosition;
}

std::unique_ptr<BamlElement> LineNumberAndPositionHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

Baml::BamlRecordType LinePositionHandler::Type() const
{
    return Baml::BamlRecordType::LinePosition;
}

std::unique_ptr<BamlElement> LinePositionHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

Baml::BamlRecordType PIMappingHandler::Type() const
{
    return Baml::BamlRecordType::PIMapping;
}

std::unique_ptr<BamlElement> PIMappingHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

Baml::BamlRecordType TypeInfoHandler::Type() const
{
    return Baml::BamlRecordType::TypeInfo;
}

std::unique_ptr<BamlElement> TypeInfoHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

Baml::BamlRecordType TypeSerializerInfoHandler::Type() const
{
    return Baml::BamlRecordType::TypeSerializerInfo;
}

std::unique_ptr<BamlElement> TypeSerializerInfoHandler::Translate(XamlContext&,
    Baml::BamlNode&, BamlElement*)
{
    return nullptr;
}

// ===== the static-resource family ==========================================

namespace {

// The C# registration body the StaticResourceStart and
// OptimizedStaticResource Translate arms share (both handlers carry the
// identical FindKeyInSiblings + Add pair over their own record casts).
void RegisterStaticResource(Baml::BamlNode& node)
{
    // The C# `var key = XamlResourceKey.FindKeyInSiblings(node)` -- a null
    // answer NREs at the `key.StaticResources` read (the unregistered-
    // sibling arm).
    std::shared_ptr<Xaml::XamlResourceKey> key =
        Xaml::XamlResourceKey::FindKeyInSiblings(node);
    if (key == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    key->StaticResources.push_back(&node);
}

// The C# `((IDeferHandler)HandlerMap.LookupHandler(resNode.Type))` cast of
// the two consumer handlers: a null handler NREs at the TranslateDefer
// call (the C# null cast succeeds); a non-defer handler is the
// InvalidCastException -- reachable through a hand-registered resource node
// of another record type, with the concrete class name rendered through the
// fixed '<handler>' placeholder (the Blocks.cpp convention).
IDeferHandler* DeferHandlerOf(Baml::BamlNode& resNode)
{
    IHandler* handler = HandlerMap::LookupHandler(resNode.Type());
    if (handler == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto* deferHandler = dynamic_cast<IDeferHandler*>(handler);
    if (deferHandler == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type '<handler>' to type "
            "'ICSharpCode.BamlDecompiler.IDeferHandler'.");
    return deferHandler;
}

// The C# do-while ancestors walk the two consumer handlers share: keep
// walking while the found key's StaticResources list is too short for the
// id, starting the search at the node's own parent (a null parent is the
// FindKeyInAncestors(null) NRE at n.Annotation).
Baml::BamlNode& FindStaticResourceNode(Baml::BamlNode& node,
    std::uint16_t staticResourceId)
{
    Baml::BamlNode* found = &node;
    std::shared_ptr<Xaml::XamlResourceKey> key;
    do {
        Baml::BamlNode* next = found->Parent;
        if (next == nullptr)
            throw std::runtime_error(kNullReferenceMessage);
        key = Xaml::XamlResourceKey::FindKeyInAncestors(*next, found);
    } while (key != nullptr && staticResourceId >= key->StaticResources.size());

    // The C# `throw new Exception("Cannot find StaticResource @" +
    // node.Record.Position)` -- the plain System.Exception over the
    // record's absolute stream position.
    if (key == nullptr)
        throw std::runtime_error("Cannot find StaticResource @" +
            std::to_string(node.Record()->Position));
    return *key->StaticResources[staticResourceId];
}

} // namespace

// ===== StaticResourceStartHandler ===========================================

Baml::BamlRecordType StaticResourceStartHandler::Type() const
{
    return Baml::BamlRecordType::StaticResourceStart;
}

std::unique_ptr<BamlElement> StaticResourceStartHandler::Translate(
    XamlContext&, Baml::BamlNode& node, BamlElement*)
{
    // The C# `var record = (StaticResourceStartRecord)((BamlBlockNode)
    // node).Record` -- the cast is the only observable behavior (the record
    // local is unused): a null header casts fine, a wrong-typed header is
    // the InvalidCastException (the fixed '<record>' placeholder).
    Baml::BamlBlockNode& blockNode = BlockNodeOf(node);
    if (blockNode.Header != nullptr &&
        dynamic_cast<Baml::StaticResourceStartRecord*>(blockNode.Header) == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.StaticResourceStartRecord'.");
    RegisterStaticResource(node);
    return nullptr;
}

std::unique_ptr<BamlElement> StaticResourceStartHandler::TranslateDefer(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    // The C# `var record = (StaticResourceStartRecord)((BamlBlockNode)node)
    // .Record` -- a headerless block keeps the null-cast shape and NREs at
    // the TypeId read; a wrong-typed header is the InvalidCastException
    // (the KeyElementStartHandler precedent).
    Baml::BamlBlockNode& blockNode = BlockNodeOf(node);
    Baml::BamlRecord* header = blockNode.Header;
    if (header == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto* record = dynamic_cast<Baml::StaticResourceStartRecord*>(header);
    if (record == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.StaticResourceStartRecord'.");

    auto doc = std::make_unique<BamlElement>(&node);
    std::shared_ptr<Xaml::XamlType> elemType = ctx.ResolveTypeOwning(record->TypeId);
    doc->Xaml = std::make_shared<Xml::XElement>(elemType->ToXName(ctx));
    doc->Xaml.Element->AddAnnotation(elemType);
    // The C# `parent.Xaml.Element.Add(...)`: the null-parent deref NREs.
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    parent->Xaml.Element->Add(doc->Xaml.Element);
    HandlerMap::ProcessChildren(ctx, blockNode, *doc);
    return doc;
}

// ===== StaticResourceIdHandler ==============================================

Baml::BamlRecordType StaticResourceIdHandler::Type() const
{
    return Baml::BamlRecordType::StaticResourceId;
}

std::unique_ptr<BamlElement> StaticResourceIdHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    Baml::StaticResourceIdRecord& record =
        CheckedRecord<Baml::StaticResourceIdRecord>(node, "StaticResourceIdRecord");
    Baml::BamlNode& resNode = FindStaticResourceNode(node, record.StaticResourceId);
    std::unique_ptr<BamlElement> resElem =
        DeferHandlerOf(resNode)->TranslateDefer(ctx, resNode, parent);
    // The C# `parent.Children.Add(resElem); resElem.Parent = parent;` -- a
    // null resElem NREs at the Parent write; the children-list add is the
    // caller's (see the header note for the C#'s unread double-add).
    BamlElement* rawResElem = resElem.get();
    if (parent == nullptr || rawResElem == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    rawResElem->Parent = parent;
    return resElem;
}

// ===== OptimizedStaticResourceHandler =======================================

Baml::BamlRecordType OptimizedStaticResourceHandler::Type() const
{
    return Baml::BamlRecordType::OptimizedStaticResource;
}

std::unique_ptr<BamlElement> OptimizedStaticResourceHandler::Translate(
    XamlContext&, Baml::BamlNode& node, BamlElement*)
{
    // The C# `var record = (OptimizedStaticResourceRecord)((BamlRecordNode)
    // node).Record` -- the cast is the only observable behavior.
    CheckedRecord<Baml::OptimizedStaticResourceRecord>(
        node, "OptimizedStaticResourceRecord");
    RegisterStaticResource(node);
    return nullptr;
}

std::unique_ptr<BamlElement> OptimizedStaticResourceHandler::TranslateDefer(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    Baml::OptimizedStaticResourceRecord& record =
        CheckedRecord<Baml::OptimizedStaticResourceRecord>(
            node, "OptimizedStaticResourceRecord");
    auto bamlElem = std::make_unique<BamlElement>(&node);
    // The C# `object key` -- an XElement (the extension arms), a string (the
    // value arm), or null (the missing string id, the XContainer null-
    // content no-op at the Add below).
    std::variant<std::monostate, std::string,
        std::shared_ptr<Xml::XElement>> key;
    if (record.IsType()) {
        std::shared_ptr<Xaml::XamlType> value = ctx.ResolveTypeOwning(record.ValueId);
        // The C# `ctx.GetKnownNamespace("TypeExtension", ..., parent.Xaml)`
        // -- the parent.Xaml read NREs for a null parent; a string-Xaml
        // parent's null element reaches the ToString below (the
        // ConstructorParameterTypeHandler shape).
        if (parent == nullptr)
            throw std::runtime_error(kNullReferenceMessage);
        auto typeElem = std::make_shared<Xml::XElement>(
            ctx.GetKnownNamespace("TypeExtension", XamlContext::KnownNamespace_Xaml,
                parent->Xaml.Element.get()));
        typeElem->AddAnnotation(ctx.ResolveTypeOwning(0xfd4d));
        typeElem->Add(std::make_shared<Xml::XElement>(
            ctx.GetPseudoName("Ctor"),
            Xaml::ToString(ctx, ParentElementOf(parent), *value)));
        key = std::move(typeElem);
    } else if (record.IsStatic()) {
        std::string attrName;
        if (record.ValueId > 0x7fff) {
            // The C# `short bamlId = unchecked((short)-record.ValueId)` --
            // the two's-complement wrap of the negated id, then the
            // SystemResourceIds magic ranges (each range shifts the id into
            // the KnownThings resource rows and picks the *Key property-name
            // form or the resource form).
            std::int16_t bamlId = static_cast<std::int16_t>(
                (0x10000u - record.ValueId) & 0xFFFFu);
            bool isKey = true;
            if (bamlId > 232 && bamlId < 464) {
                bamlId = static_cast<std::int16_t>(bamlId - 232);
                isKey = false;
            } else if (bamlId > 464 && bamlId < 467) {
                bamlId = static_cast<std::int16_t>(bamlId - 231);
            } else if (bamlId > 467 && bamlId < 470) {
                bamlId = static_cast<std::int16_t>(bamlId - 234);
                isKey = false;
            }
            Baml::KnownResource res = ctx.Baml().KnownThings().Resources(bamlId);
            std::string name =
                isKey ? res.Item1 + "." + res.Item2 : res.Item1 + "." + res.Item3;
            // The C# GetXmlNamespace never answers null for the non-null
            // presentation constant.
            Xml::XNamespace xmlns =
                *ctx.GetXmlNamespace(XamlContext::KnownNamespace_Presentation);
            attrName = Xaml::ToString(ctx, ParentElementOf(parent),
                xmlns.GetName(std::move(name)));
        } else {
            std::shared_ptr<Xaml::XamlProperty> value =
                ctx.ResolvePropertyOwning(record.ValueId);
            // The C# `value.DeclaringType.ResolveNamespace(parent.Xaml, ctx)`
            // -- the first parent.Xaml read of the arm (the mutation the
            // prefixed-name render below reads; the readonly-reference
            // const_cast convention).
            Xml::XElement& parentElement = ParentElementOf(parent);
            const_cast<Xaml::XamlType*>(value->DeclaringType)
                ->ResolveNamespace(parentElement, ctx);
            Xml::XName xName = value->ToXName(ctx, &parentElement);
            attrName = Xaml::ToString(ctx, parentElement, xName);
        }
        auto staticElem = std::make_shared<Xml::XElement>(
            ctx.GetKnownNamespace("StaticExtension", XamlContext::KnownNamespace_Xaml,
                &ParentElementOf(parent)));
        staticElem->AddAnnotation(ctx.ResolveTypeOwning(0xfda6));
        staticElem->Add(std::make_shared<Xml::XElement>(
            ctx.GetPseudoName("Ctor"), std::move(attrName)));
        key = std::move(staticElem);
    } else {
        std::optional<std::string> value = ctx.ResolveString(record.ValueId);
        if (value)
            key = std::move(*value);
    }

    // The C# tail: the {StaticResource} extension element (the known type
    // 0xfda5 -- StaticResourceExtension) carrying the Ctor pseudo-element.
    std::shared_ptr<Xaml::XamlType> extType = ctx.ResolveTypeOwning(0xfda5);
    auto resElem = std::make_shared<Xml::XElement>(extType->ToXName(ctx));
    resElem->AddAnnotation(extType);
    bamlElem->Xaml = resElem;
    ParentElementOf(parent).Add(resElem);

    auto attrElem = std::make_shared<Xml::XElement>(ctx.GetPseudoName("Ctor"));
    // The C# `attrElem.Add(key)`: the element arm appends the element, the
    // string arm the text, the null arm nothing (the XContainer
    // null-content no-op).
    if (auto* elemKey = std::get_if<std::shared_ptr<Xml::XElement>>(&key))
        attrElem->Add(*elemKey);
    else if (auto* stringKey = std::get_if<std::string>(&key))
        attrElem->Add(*stringKey);
    resElem->Add(attrElem);
    return bamlElem;
}

// ===== PropertyWithStaticResourceIdHandler ==================================

Baml::BamlRecordType PropertyWithStaticResourceIdHandler::Type() const
{
    return Baml::BamlRecordType::PropertyWithStaticResourceId;
}

std::unique_ptr<BamlElement> PropertyWithStaticResourceIdHandler::Translate(
    XamlContext& ctx, Baml::BamlNode& node, BamlElement* parent)
{
    Baml::PropertyWithStaticResourceIdRecord& record =
        CheckedRecord<Baml::PropertyWithStaticResourceIdRecord>(
            node, "PropertyWithStaticResourceIdRecord");
    auto doc = std::make_unique<BamlElement>(&node);
    std::shared_ptr<Xaml::XamlProperty> elemAttr =
        ctx.ResolvePropertyOwning(record.AttributeId);
    // The C# `doc.Xaml = new XElement(elemAttr.ToXName(ctx, null))` -- the
    // name is built BEFORE ResolveNamespace runs (the bare unqualified
    // name; the rename below re-renders it after the namespace attached).
    doc->Xaml = std::make_shared<Xml::XElement>(elemAttr->ToXName(ctx, nullptr));
    doc->Xaml.Element->AddAnnotation(elemAttr);
    // The C# `parent.Xaml.Element.Add(...)`: the null-parent deref NREs
    // BEFORE the ancestors walk.
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    parent->Xaml.Element->Add(doc->Xaml.Element);

    Baml::BamlNode& resNode = FindStaticResourceNode(node, record.StaticResourceId);
    std::unique_ptr<BamlElement> resElem =
        DeferHandlerOf(resNode)->TranslateDefer(ctx, resNode, doc.get());
    // The C# `doc.Children.Add(resElem); resElem.Parent = doc;` -- the add
    // targets the handler's OWN doc, so the owning AddChild is faithful (a
    // null resElem NREs at the Parent write, after the add).
    BamlElement* rawResElem = resElem.get();
    doc->AddChild(std::move(resElem));
    if (rawResElem == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    rawResElem->Parent = doc.get();

    // The C# pair that attaches the xmlns and re-renders the name (the
    // ElementHandler pair, over the property's own element).
    const_cast<Xaml::XamlType*>(elemAttr->DeclaringType)
        ->ResolveNamespace(*doc->Xaml.Element, ctx);
    doc->Xaml.Element->Name(elemAttr->ToXName(ctx, nullptr));
    return doc;
}

} // namespace ILSpy::BamlDecompiler::Handlers
