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
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/Xaml/XamlUtils.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/Xml/XAttribute.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

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

} // namespace ILSpy::BamlDecompiler::Handlers
