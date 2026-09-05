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

// Port of ICSharpCode.BamlDecompiler/IRewritePass.cs (Ki, 2015, MIT): the
// pass interface XamlDecompiler.Decompile drives over the finished XDocument
// (in order: XClassRewritePass, MarkupExtensionRewritePass,
// AttributeRewritePass, ConnectionIdRewritePass, DocumentRewritePass --
// the port's Rewrite/ directory carries the four IL-free passes;
// ConnectionIdRewritePass needs the ILAst/ILReader back end and stays
// deferred with the Phase-3/4 machinery).

#pragma once

#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/Xml/XDocument.hpp"

namespace ILSpy::BamlDecompiler::Rewrite {

// The C# `internal interface IRewritePass`.
class IRewritePass {
public:
    virtual ~IRewritePass() = default;

    // The C# `void Run(XamlContext ctx, XDocument document)`.
    virtual void Run(XamlContext& ctx, Xml::XDocument& document) = 0;
};

} // namespace ILSpy::BamlDecompiler::Rewrite
