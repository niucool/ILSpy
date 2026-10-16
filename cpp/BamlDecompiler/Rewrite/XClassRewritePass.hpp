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

// Port of ICSharpCode.BamlDecompiler/Rewrite/XClassRewritePass.cs (Ki,
// 2015, MIT): the first rewrite pass -- a main-module class element is
// renamed to its DIRECT BASE type's name (the XAML root that survives the
// .g.cs regeneration) and gains the x:Class attribute carrying the
// original type's full name (plus x:ClassModifier="internal" when the type
// is not public).
//
// C#-to-C++ porting decisions:
//  * `typeDef.DirectBaseTypes.First()` throws InvalidOperationException on
//    an empty base list; the port maps it to std::runtime_error with the
//    .NET "Sequence contains no elements" text (no trailing period -- the
//    probed .NET 10 message; distinct from SingleOrDefault's
//    "more than one element." spelling).
//  * The pass runs over the pseudo-Document wrapper's CHILD elements (not
//    the whole tree): `document.Elements(pseudo).Elements()` -- the two
//    chained lazy sequences (the wrapper is never renamed, so the walk is
//    stable; the child rename never detaches).
//  * `type.ResolvedType.FullName` is the IType full name (the local
//    FullNameOf helper -- the established "copied next to its consumer"
//    convention).

#pragma once

#include "BamlDecompiler/Rewrite/IRewritePass.hpp"

namespace ILSpy::BamlDecompiler::Rewrite {

// The C# `internal class XClassRewritePass : IRewritePass`.
class XClassRewritePass final : public IRewritePass {
public:
    // The C# `void Run(XamlContext ctx, XDocument document)`.
    void Run(XamlContext& ctx, Xml::XDocument& document) override;

private:
    // The C# `void RewriteClass(XamlContext ctx, XElement elem)`.
    void RewriteClass(XamlContext& ctx, Xml::XElement& elem);
};

} // namespace ILSpy::BamlDecompiler::Rewrite
