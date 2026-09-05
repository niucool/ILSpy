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

// Port of ICSharpCode.BamlDecompiler/Rewrite/MarkupExtensionRewritePass.cs (Ki,
// 2015, MIT): the second rewrite pass -- a property element whose single
// child is a markup extension becomes the parent's attribute rendered
// through XamlExtension.ToString (the `{Extension args, name=value}`
// inline form).
//
// C#-to-C++ porting decisions:
//  * The non-key arm requires BOTH annotations: the child's XamlProperty
//    and the PARENT's XamlType (the gold-pinned gate -- a property element
//    under an unannotated parent never inlines through this pass).
//  * CanInlineExt's base-chain walk starts at the resolved type's FIRST
//    base type (`ResolvedType.GetDefinition()?.DirectBaseTypes.FirstOrDefault()`)
//    and never compares the type itself -- a type whose own full name IS
//    System.Windows.Markup.MarkupExtension does not match (the faithful
//    C# quirk). Elements with neither a XamlType nor a XamlProperty
//    annotation inline only under the pseudo-Ctor name.
//  * `InlineObject`'s C# null (a non-text/non-element node, or a failed
//    InlineExtension) ports to the XamlObject monostate arm; InlineCtor's
//    null check reads that arm, while InlineExtension's named-argument
//    store keeps it (the iteration-50 XamlExtension contract).
//  * The C# `ctx.CancellationToken.ThrowIfCancellationRequested()` stays
//    deferred with the XamlContext CancellationToken deferral.

#pragma once

#include "BamlDecompiler/Rewrite/IRewritePass.hpp"
#include "BamlDecompiler/Xaml/XamlExtension.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace ILSpy::BamlDecompiler::Rewrite {

// The C# `internal class MarkupExtensionRewritePass : IRewritePass`.
class MarkupExtensionRewritePass final : public IRewritePass {
public:
    // The C# `void Run(XamlContext ctx, XDocument document)`.
    void Run(XamlContext& ctx, Xml::XDocument& document) override;

private:
    // The C# `bool ProcessElement(XamlContext ctx, XElement elem)`.
    bool ProcessElement(XamlContext& ctx, Xml::XElement& elem);

    // The C# `bool RewriteElement(XamlContext ctx, XElement parent,
    // XElement elem)`.
    bool RewriteElement(XamlContext& ctx, Xml::XElement& parent, Xml::XElement& elem);

    // The C# `bool CanInlineExt(XamlContext ctx, XElement ctxElement)`.
    bool CanInlineExt(Xml::XElement& ctxElement) const;

    // The C# `object InlineObject(XamlContext ctx, XNode obj)`.
    Xaml::XamlObject InlineObject(Xml::XNode& obj);

    // The C# `object[] InlineCtor(XamlContext ctx, XElement ctor)`.
    std::optional<std::vector<Xaml::XamlObject>> InlineCtor(Xml::XElement& ctor);

    // The C# `XamlExtension InlineExtension(XamlContext ctx, XElement
    // ctxElement)`.
    std::shared_ptr<Xaml::XamlExtension> InlineExtension(Xml::XElement& ctxElement);

    // The C# `XName key` / `XName ctor` fields (null until Run assigns
    // them -- the port's optionals over the default-ctor-less XName).
    std::optional<Xml::XName> key_;
    std::optional<Xml::XName> ctor_;
};

} // namespace ILSpy::BamlDecompiler::Rewrite
