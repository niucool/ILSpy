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

// Port of ICSharpCode.BamlDecompiler/Rewrite/DocumentRewritePass.cs (Ki,
// 2015, MIT): the last rewrite pass -- the pseudo-named <Document> wrapper
// the DocumentHandler emits collapses onto its single child element (the
// XML document gets its real root back), the wrapper's attributes moving
// over to the surviving root.
//
// C#-to-C++ porting decisions:
//  * The gold (C:/temp-probe/RewritePassProbe) pins two release-build
//    behaviors the C# source only implies:
//     - `Debug.Assert(attr.IsNamespaceDeclaration)` is compiled out of the
//       shipped release assembly, so a PLAIN attribute on the wrapper moves
//       exactly like a namespace declaration (no throw, no skip).
//     - The attribute walk is the LAZY `elem.Attributes()` iterator with
//       `attr.Remove()` inside the body: removing the yielded attribute
//       fails the iterator's stop condition, so only the FIRST wrapper
//       attribute ever moves (a wrapper carrying two or three attributes
//       loses every one after the first when it is replaced). The port's
//       XElementAttributes implements the same stop condition, so a plain
//    range-for reproduces the one-attribute-then-stop shape.
//  * `elem.Elements().Count() != 1` counts the wrapper's child ELEMENTS
//    (text nodes never count), and `Single()` after the count check cannot
//    throw; the port materializes the single child during the count.

#pragma once

#include "BamlDecompiler/Rewrite/IRewritePass.hpp"

namespace ILSpy::BamlDecompiler::Rewrite {

// The C# `internal class DocumentRewritePass : IRewritePass`.
class DocumentRewritePass final : public IRewritePass {
public:
    // The C# `void Run(XamlContext ctx, XDocument document)`.
    void Run(XamlContext& ctx, Xml::XDocument& document) override;
};

} // namespace ILSpy::BamlDecompiler::Rewrite
