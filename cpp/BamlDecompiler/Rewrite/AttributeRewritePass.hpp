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

// Port of ICSharpCode.BamlDecompiler/Rewrite/AttributeRewritePass.cs (Ki,
// 2015, MIT): the third rewrite pass -- a bare property-element child
// (`<Type.Property>value</Type.Property>`) becomes an attribute on its
// parent element.
//
// C#-to-C++ porting decisions:
//  * `Run`'s do-while is a FIXPOINT: it repeats until a full round changes
//    nothing. Combined with the LAZY `elem.Elements()` iteration inside
//    `ProcessElement` (removing the yielded child fails the iterator's stop
//    condition, ending the sequence), one round rewrites at most one child
//    per element after a removal -- the port's XContainerElements
//    implements the same stop condition, so a plain range-for reproduces
//    the shape.
//  * The gold (C:/temp-probe/RewritePassProbe) pins the observable
//    consequence: a parent carrying TWO children that inline to the SAME
//    attribute name throws InvalidOperationException "Duplicate attribute."
//    out of the second round's ReplaceAttributes (the list from round one
//    already holds the name), and the second child survives the throw. The
//    port maps the exception to std::runtime_error (the iteration-45
//    convention) thrown from the same ReplaceAttributes site.
//  * The x:Key arm inserts the new attribute at position 0; every other
//    name appends. `elem.Value` is the element's concatenated text (a bare
//    child has no elements, so text-only).
//  * `ctx.CancellationToken.ThrowIfCancellationRequested()` stays deferred
//    with the XamlContext CancellationToken deferral (no ported consumer
//    cancels; the CLI runs single-threaded).

#pragma once

#include "BamlDecompiler/Rewrite/IRewritePass.hpp"

namespace ILSpy::BamlDecompiler::Rewrite {

// The C# `internal class AttributeRewritePass : IRewritePass`.
class AttributeRewritePass final : public IRewritePass {
public:
    // The C# `void Run(XamlContext ctx, XDocument document)`.
    void Run(XamlContext& ctx, Xml::XDocument& document) override;

private:
    // The C# `bool ProcessElement(XamlContext ctx, XElement elem)`.
    bool ProcessElement(XamlContext& ctx, Xml::XElement& elem);

    // The C# `bool RewriteElement(XamlContext ctx, XElement parent,
    // XElement elem)`.
    bool RewriteElement(XamlContext& ctx, Xml::XElement& parent, Xml::XElement& elem);

    // The C# `XName key` field (null until Run assigns it -- the port's
    // optional over the default-ctor-less XName; a pass instance is
    // single-threaded like the C#).
    std::optional<Xml::XName> key_;
};

} // namespace ILSpy::BamlDecompiler::Rewrite
