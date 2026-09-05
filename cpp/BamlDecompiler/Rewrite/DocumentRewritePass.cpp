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

// Implementation of the DocumentRewritePass port (the file note carries the
// porting decisions; this file is the translation).

#include "BamlDecompiler/Rewrite/DocumentRewritePass.hpp"

#include <memory>
#include <vector>

namespace ILSpy::BamlDecompiler::Rewrite {

void DocumentRewritePass::Run(XamlContext& ctx, Xml::XDocument& document)
{
    // The C# `document.Elements(ctx.GetPseudoName("Document")).ToList()`:
    // the materialized walk (the ReplaceWith below mutates the top level).
    std::vector<std::shared_ptr<Xml::XElement>> wrappers;
    for (const auto& elem : document.Elements(ctx.GetPseudoName("Document")))
        wrappers.push_back(elem);

    for (const auto& elem : wrappers) {
        // The C# `if (elem.Elements().Count() != 1) continue;`.
        std::size_t count = 0;
        std::shared_ptr<Xml::XElement> docElem;
        for (const auto& child : elem->Elements()) {
            docElem = child;
            ++count;
        }
        if (count != 1)
            continue;

        // The C# `foreach (var attr in elem.Attributes())` -- the LAZY
        // iterator with attr.Remove() inside (only the first wrapper
        // attribute moves; the file note carries the gold). The
        // Debug.Assert(attr.IsNamespaceDeclaration) is compiled out of the
        // release assembly the engine ships.
        for (const auto& attr : elem->Attributes()) {
            attr->Remove();
            docElem->Add(attr);
        }

        // The C# `elem.ReplaceWith(docElem)`.
        elem->ReplaceWith(docElem);
    }
}

} // namespace ILSpy::BamlDecompiler::Rewrite
