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

// Implementation of the XClassRewritePass port (the file note carries the
// porting decisions; this file is the translation).

#include "BamlDecompiler/Rewrite/XClassRewritePass.hpp"

#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

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

void XClassRewritePass::Run(XamlContext& ctx, Xml::XDocument& document)
{
    // The C# `foreach (var elem in
    // document.Elements(ctx.GetPseudoName("Document")).Elements())
    // RewriteClass(ctx, elem);` -- the chained lazy sequences.
    for (const auto& wrapper : document.Elements(ctx.GetPseudoName("Document"))) {
        for (const auto& elem : wrapper->Elements())
            RewriteClass(ctx, *elem);
    }
}

void XClassRewritePass::RewriteClass(XamlContext& ctx, Xml::XElement& elem)
{
    // The C# `var type = elem.Annotation<XamlType>(); if (type == null ||
    // type.ResolvedType == null) return;`.
    auto* typeAny = elem.Annotation<std::shared_ptr<Xaml::XamlType>>();
    if (typeAny == nullptr)
        return;
    Xaml::XamlType& type = **typeAny;
    if (type.ResolvedType == nullptr)
        return;

    // The C# `var typeDef = type.ResolvedType.GetDefinition(); if (typeDef ==
    // null || !typeDef.ParentModule.IsMainModule) return;`.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDef =
        type.ResolvedType->GetDefinition();
    if (typeDef == nullptr || !typeDef->ParentModule()->IsMainModule())
        return;

    // The C# `var newType = typeDef.DirectBaseTypes.First().GetDefinition();`
    // -- First() throws on an empty base list (the probed .NET message, no
    // trailing period).
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> baseTypes =
        typeDef->DirectBaseTypes();
    if (baseTypes.empty())
        throw std::runtime_error("Sequence contains no elements");
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* newType =
        baseTypes.front()->GetDefinition();
    if (newType == nullptr)
        return;

    // The C# `var xamlType = new XamlType(newType.ParentModule,
    // newType.ParentModule.FullAssemblyName, newType.Namespace,
    // newType.Name); xamlType.ResolveNamespace(elem, ctx);`.
    Xaml::XamlType xamlType(newType->ParentModule(),
        newType->ParentModule()->FullAssemblyName(),
        newType->Namespace(), newType->Name());
    xamlType.ResolveNamespace(elem, ctx);

    // The C# `elem.Name = xamlType.ToXName(ctx);`.
    elem.Name(xamlType.ToXName(ctx));

    // The C# `var attrName = ctx.GetKnownNamespace("Class",
    // XamlContext.KnownNamespace_Xaml, elem);`.
    Xml::XName attrName = ctx.GetKnownNamespace("Class", XamlContext::KnownNamespace_Xaml,
        &elem);

    // The C# `var attrs = elem.Attributes().ToList();`.
    std::vector<Xml::XContent> attrs;
    for (const auto& attr : elem.Attributes())
        attrs.emplace_back(attr);

    // The C# `if (typeDef.Accessibility != Accessibility.Public) { ... attrs.
    // Insert(0, new XAttribute(classModifierName, "internal")); } attrs.Insert(
    // 0, new XAttribute(attrName, type.ResolvedType.FullName));`.
    if (typeDef->Accessibility() != ILSpy::Decompiler::TypeSystem::Accessibility::Public) {
        Xml::XName classModifierName = ctx.GetKnownNamespace("ClassModifier",
            XamlContext::KnownNamespace_Xaml, &elem);
        attrs.insert(attrs.begin(),
            Xml::XContent(std::make_shared<Xml::XAttribute>(classModifierName, "internal")));
    }
    attrs.insert(attrs.begin(),
        Xml::XContent(std::make_shared<Xml::XAttribute>(attrName, FullNameOf(*type.ResolvedType))));

    // The C# `ctx.XClassNames.Add(type.ResolvedType.FullName);`.
    ctx.XClassNames().push_back(FullNameOf(*type.ResolvedType));

    // The C# `elem.ReplaceAttributes(attrs);`.
    elem.ReplaceAttributes(std::move(attrs));
}

} // namespace ILSpy::BamlDecompiler::Rewrite
