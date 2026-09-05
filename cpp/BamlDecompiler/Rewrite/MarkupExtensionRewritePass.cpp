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

// Implementation of the MarkupExtensionRewritePass port (the file note
// carries the porting decisions; this file is the translation).

#include "BamlDecompiler/Rewrite/MarkupExtensionRewritePass.hpp"

#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <string>
#include <utility>

namespace ILSpy::BamlDecompiler::Rewrite {

namespace {

// The C# `IType.FullName` (the INamedElement property the C# interface
// exposes; the port's IType carries Name/ReflectionName only, so the
// AbstractType `Namespace + "." + Name` composition is a local helper --
// the ILAmbience FullNameOf shape, copied next to its consumers).
std::string FullNameOf(const ILSpy::Decompiler::TypeSystem::IType& type)
{
    std::string ns;
    if (const auto* entity = dynamic_cast<
            const ILSpy::Decompiler::TypeSystem::IEntity*>(&type))
        ns = entity->Namespace();
    else if (const auto* pt = dynamic_cast<
            const ILSpy::Decompiler::TypeSystem::ParameterizedType*>(&type))
        ns = pt->GenericType() ? FullNameOf(*pt->GenericType()) : std::string();
    else if (const auto* unknown = dynamic_cast<
            const class ILSpy::Decompiler::TypeSystem::UnknownType*>(&type))
        ns = unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    if (ns.empty())
        return type.Name();
    return ns + "." + type.Name();
}

} // namespace

void MarkupExtensionRewritePass::Run(XamlContext& ctx, Xml::XDocument& document)
{
    // The C# `key = ctx.GetKnownNamespace("Key",
    // XamlContext.KnownNamespace_Xaml); ctor = ctx.GetPseudoName("Ctor");`.
    key_ = std::optional<Xml::XName>(
        ctx.GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml));
    ctor_ = std::optional<Xml::XName>(ctx.GetPseudoName("Ctor"));

    bool doWork;
    do {
        doWork = false;
        for (const auto& elem : document.Elements())
            doWork |= ProcessElement(ctx, *elem);
    } while (doWork);
}

bool MarkupExtensionRewritePass::ProcessElement(XamlContext& ctx, Xml::XElement& elem)
{
    bool doWork = false;
    // The LAZY child walk (the RewriteElement elem.Remove ends the sequence
    // -- the iteration-44 XContainerElements contract).
    for (const auto& child : elem.Elements()) {
        doWork |= RewriteElement(ctx, elem, *child);
        doWork |= ProcessElement(ctx, *child);
    }
    return doWork;
}

bool MarkupExtensionRewritePass::RewriteElement(XamlContext& ctx, Xml::XElement& parent,
    Xml::XElement& elem)
{
    // The C# `var type = parent.Annotation<XamlType>(); var property =
    // elem.Annotation<XamlProperty>();`.
    Xaml::XamlType* type = nullptr;
    if (auto* typeAny = parent.Annotation<std::shared_ptr<Xaml::XamlType>>())
        type = typeAny->get();
    Xaml::XamlProperty* property = nullptr;
    if (auto* propertyAny = elem.Annotation<std::shared_ptr<Xaml::XamlProperty>>())
        property = propertyAny->get();

    // The C# `if (elem.Name != key) { if (property == null || type == null)
    // return false; if (property.ResolvedMember is IProperty { CanSet: false })
    // return false; }`.
    if (elem.Name() != *key_) {
        if (property == nullptr || type == nullptr)
            return false;

        auto* propertyDef = dynamic_cast<const ILSpy::Decompiler::TypeSystem::IProperty*>(
            property->ResolvedMember);
        if (propertyDef != nullptr && !propertyDef->CanSet())
            return false;
    }

    // The C# `if (elem.Elements().Count() != 1 || elem.Attributes().Any(t =>
    // t.Name.Namespace != XNamespace.Xmlns)) return false;`.
    std::size_t elementCount = 0;
    std::shared_ptr<Xml::XElement> value;
    for (const auto& child : elem.Elements()) {
        value = child;
        ++elementCount;
    }
    if (elementCount != 1)
        return false;
    for (const auto& attr : elem.Attributes()) {
        if (attr->Name().Namespace() != Xml::XNamespace::Xmlns())
            return false;
    }

    // The C# `var value = elem.Elements().Single();` (the count check above
    // pins the single element).

    // The C# `if (!CanInlineExt(ctx, value)) return false;`.
    if (!CanInlineExt(*value))
        return false;

    // The C# `var ext = InlineExtension(ctx, value); if (ext == null)
    // return false;`.
    std::shared_ptr<Xaml::XamlExtension> ext = InlineExtension(*value);
    if (ext == nullptr)
        return false;

    // The C# `ctx.CancellationToken.ThrowIfCancellationRequested();` stays
    // deferred with the XamlContext CancellationToken deferral.

    // The C# `var extValue = ext.ToString(ctx, parent);`.
    std::string extValue = ext->ToString(ctx, parent);

    // The C# `var attrName = elem.Name; if (attrName != key) attrName =
    // property.ToXName(ctx, parent, property.IsAttachedTo(type));`.
    Xml::XName attrName = elem.Name();
    if (attrName != *key_)
        attrName = property->ToXName(ctx, &parent, property->IsAttachedTo(type));

    // The C# `if (!parent.Attributes(attrName).Any()) { ... } elem.Remove();
    // return true;` -- the element is removed even when the parent already
    // carries the attribute (the existing value survives).
    bool hasAttr = false;
    for (const auto& existing : parent.Attributes(attrName)) {
        (void)existing;
        hasAttr = true;
        break;
    }
    if (!hasAttr) {
        auto attr = std::make_shared<Xml::XAttribute>(attrName, std::move(extValue));
        std::vector<Xml::XContent> list;
        for (const auto& existing : parent.Attributes())
            list.emplace_back(existing);
        if (attrName == *key_)
            list.insert(list.begin(), Xml::XContent(attr));
        else
            list.emplace_back(attr);
        parent.RemoveAttributes();
        parent.ReplaceAttributes(std::move(list));
    }
    elem.Remove();

    return true;
}

bool MarkupExtensionRewritePass::CanInlineExt(Xml::XElement& ctxElement) const
{
    // The C# `var type = ctxElement.Annotation<XamlType>(); if (type != null
    // && type.ResolvedType != null) { ... }`.
    Xaml::XamlType* type = nullptr;
    if (auto* typeAny = ctxElement.Annotation<std::shared_ptr<Xaml::XamlType>>())
        type = typeAny->get();
    if (type != nullptr && type->ResolvedType != nullptr) {
        // The C# `var typeDef = type.ResolvedType.GetDefinition()?
        // .DirectBaseTypes.FirstOrDefault();` -- the walk starts at the FIRST
        // base type; the type itself is never compared.
        ILSpy::Decompiler::TypeSystem::ITypePtr typeDef;
        if (const auto* definition = type->ResolvedType->GetDefinition()) {
            std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> bases =
                definition->DirectBaseTypes();
            typeDef = bases.empty() ? nullptr : bases.front();
        }
        bool isExt = false;
        while (typeDef != nullptr) {
            // The C# `typeDef.FullName ==
            // "System.Windows.Markup.MarkupExtension"`.
            if (FullNameOf(*typeDef) == "System.Windows.Markup.MarkupExtension") {
                isExt = true;
                break;
            }
            std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> bases =
                typeDef->DirectBaseTypes();
            typeDef = bases.empty() ? nullptr : bases.front();
        }
        if (!isExt)
            return false;
    }
    else if (ctxElement.Annotation<std::shared_ptr<Xaml::XamlProperty>>() == nullptr
        && ctxElement.Name() != *ctor_) {
        // The C# `else if (ctxElement.Annotation<XamlProperty>() == null &&
        // ctxElement.Name != ctor) return false;`.
        return false;
    }

    for (const auto& child : ctxElement.Elements()) {
        if (!CanInlineExt(*child))
            return false;
    }
    return true;
}

Xaml::XamlObject MarkupExtensionRewritePass::InlineObject(Xml::XNode& obj)
{
    // The C# `if (obj is XText) return ((XText)obj).Value; else if (obj is
    // XElement) return InlineExtension(ctx, (XElement)obj); else return
    // null;` -- the null ports to the monostate arm.
    if (auto* text = dynamic_cast<Xml::XText*>(&obj))
        return Xaml::XamlObject(text->Value());
    if (auto* element = dynamic_cast<Xml::XElement*>(&obj)) {
        std::shared_ptr<Xaml::XamlExtension> ext = InlineExtension(*element);
        if (ext == nullptr)
            return Xaml::XamlObject(std::monostate{});
        return Xaml::XamlObject(std::move(ext));
    }
    return Xaml::XamlObject(std::monostate{});
}

std::optional<std::vector<Xaml::XamlObject>> MarkupExtensionRewritePass::InlineCtor(
    Xml::XElement& ctor)
{
    // The C# `if (ctor.HasAttributes) return null;`.
    if (ctor.HasAttributes())
        return std::nullopt;

    // The C# `foreach (var child in ctor.Nodes()) { var arg = InlineObject(...);
    // if (arg == null) return null; args.Add(arg); }` -- the null reads the
    // monostate arm.
    std::vector<Xaml::XamlObject> args;
    for (const auto& child : ctor.Nodes()) {
        Xaml::XamlObject arg = InlineObject(*child);
        if (std::holds_alternative<std::monostate>(arg))
            return std::nullopt;
        args.push_back(std::move(arg));
    }
    return args;
}

std::shared_ptr<Xaml::XamlExtension> MarkupExtensionRewritePass::InlineExtension(
    Xml::XElement& ctxElement)
{
    // The C# `var type = ctxElement.Annotation<XamlType>(); if (type == null)
    // return null; var ext = new XamlExtension(type);`.
    Xaml::XamlType* type = nullptr;
    if (auto* typeAny = ctxElement.Annotation<std::shared_ptr<Xaml::XamlType>>())
        type = typeAny->get();
    if (type == nullptr)
        return nullptr;

    auto ext = std::make_shared<Xaml::XamlExtension>(type);

    // The C# `foreach (var attr in ctxElement.Attributes().Where(attr =>
    // attr.Name.Namespace != XNamespace.Xmlns)) ext.NamedArguments[attr.Name.
    // LocalName] = attr.Value;`.
    for (const auto& attr : ctxElement.Attributes()) {
        if (attr->Name().Namespace() != Xml::XNamespace::Xmlns())
            ext->SetNamedArgument(attr->Name().LocalName(), Xaml::XamlObject(attr->Value()));
    }

    // The C# `foreach (var child in ctxElement.Nodes()) { ... }`.
    for (const auto& child : ctxElement.Nodes()) {
        auto* elem = dynamic_cast<Xml::XElement*>(child.get());
        if (elem == nullptr)
            return nullptr;

        if (elem->Name() == *ctor_) {
            // The C# `if (ext.Initializer != null) return null; var args =
            // InlineCtor(ctx, elem); if (args == null) return null;
            // ext.Initializer = args; continue;`.
            if (ext->Initializer.has_value())
                return nullptr;

            std::optional<std::vector<Xaml::XamlObject>> args = InlineCtor(*elem);
            if (!args.has_value())
                return nullptr;

            ext->Initializer = std::move(*args);
            continue;
        }

        // The C# `var property = elem.Annotation<XamlProperty>(); if
        // (property == null || elem.Nodes().Count() != 1 || elem.Attributes().
        // Any(attr => attr.Name.Namespace != XNamespace.Xmlns)) return null;`.
        Xaml::XamlProperty* property = nullptr;
        if (auto* propertyAny = elem->Annotation<std::shared_ptr<Xaml::XamlProperty>>())
            property = propertyAny->get();
        if (property == nullptr)
            return nullptr;
        std::size_t nodeCount = 0;
        std::shared_ptr<Xml::XNode> single;
        for (const auto& node : elem->Nodes()) {
            single = node;
            ++nodeCount;
        }
        if (nodeCount != 1)
            return nullptr;
        for (const auto& attr : elem->Attributes()) {
            if (attr->Name().Namespace() != Xml::XNamespace::Xmlns())
                return nullptr;
        }

        // The C# `var name = property.PropertyName; var value =
        // InlineObject(ctx, elem.Nodes().Single()); ext.NamedArguments[name]
        // = value;` -- a null value stays stored (the monostate arm).
        Xaml::XamlObject value = InlineObject(*single);
        ext->SetNamedArgument(property->PropertyName, std::move(value));
    }
    return ext;
}

} // namespace ILSpy::BamlDecompiler::Rewrite
