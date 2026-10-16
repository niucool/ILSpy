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

// Implementation of the AttributeRewritePass port (the file note carries the
// porting decisions; this file is the translation).

#include "BamlDecompiler/Rewrite/AttributeRewritePass.hpp"

#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler::Rewrite {

void AttributeRewritePass::Run(XamlContext& ctx, Xml::XDocument& document)
{
    // The C# `key = ctx.GetKnownNamespace("Key",
    // XamlContext.KnownNamespace_Xaml);`.
    key_ = std::optional<Xml::XName>(
        ctx.GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml));

    bool doWork;
    do {
        doWork = false;
        for (const auto& elem : document.Elements())
            doWork |= ProcessElement(ctx, *elem);
    } while (doWork);
}

bool AttributeRewritePass::ProcessElement(XamlContext& ctx, Xml::XElement& elem)
{
    bool doWork = false;
    // The LAZY child walk: removing the yielded child (RewriteElement's
    // elem.Remove) ends the sequence -- the C# iterator's stop condition
    // (the iteration-44 XContainerElements contract).
    for (const auto& child : elem.Elements()) {
        doWork |= RewriteElement(ctx, elem, *child);
        doWork |= ProcessElement(ctx, *child);
    }
    return doWork;
}

bool AttributeRewritePass::RewriteElement(XamlContext& ctx, Xml::XElement& parent,
    Xml::XElement& elem)
{
    // The C# `if (elem.HasAttributes || elem.HasElements) return false;`.
    if (elem.HasAttributes() || elem.HasElements())
        return false;

    // The C# `var attrName = elem.Name; if (attrName != key) { ... }`.
    Xml::XName attrName = elem.Name();
    if (attrName != *key_) {
        // The C# `var property = elem.Annotation<XamlProperty>();
        // if (property is null) return false;`.
        auto* propertyAny = elem.Annotation<std::shared_ptr<Xaml::XamlProperty>>();
        if (propertyAny == nullptr)
            return false;
        Xaml::XamlProperty& property = **propertyAny;

        // The C# `if (property.ResolvedMember is IProperty propertyDef &&
        // !propertyDef.CanSet) return false;`.
        auto* propertyDef = dynamic_cast<const ILSpy::Decompiler::TypeSystem::IProperty*>(
            property.ResolvedMember);
        if (propertyDef != nullptr && !propertyDef->CanSet())
            return false;

        // The C# `attrName = property.ToXName(ctx, parent,
        // property.IsAttachedTo(parent.Annotation<XamlType>()));` -- a
        // parent without the annotation passes the C# null through.
        Xaml::XamlType* parentType = nullptr;
        if (auto* typeAny = parent.Annotation<std::shared_ptr<Xaml::XamlType>>())
            parentType = typeAny->get();
        attrName = property.ToXName(ctx, &parent, property.IsAttachedTo(parentType));
    }

    // The C# `ctx.CancellationToken.ThrowIfCancellationRequested();` stays
    // deferred with the XamlContext CancellationToken deferral.

    // The C# `var attr = new XAttribute(attrName, elem.Value); var list =
    // new List<XAttribute>(parent.Attributes()); if (attrName == key)
    // list.Insert(0, attr); else list.Add(attr); parent.RemoveAttributes();
    // parent.ReplaceAttributes(list); elem.Remove();`.
    auto attr = std::make_shared<Xml::XAttribute>(attrName, elem.Value());
    std::vector<Xml::XContent> list;
    for (const auto& existing : parent.Attributes())
        list.emplace_back(existing);
    if (attrName == *key_)
        list.insert(list.begin(), Xml::XContent(attr));
    else
        list.emplace_back(attr);
    parent.RemoveAttributes();
    parent.ReplaceAttributes(std::move(list));
    elem.Remove();

    return true;
}

} // namespace ILSpy::BamlDecompiler::Rewrite
