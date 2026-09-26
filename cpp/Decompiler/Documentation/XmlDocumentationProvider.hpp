// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <map>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::Documentation {

// The C# `XmlDocumentationProvider` (the adjacent-.xml documentation
// provider): the `<member name="...">` elements of a compiler-generated
// XML documentation file, keyed by the documentation ID string
// (`T:Namespace.Type`, `M:...` -- the IdStringProvider forms). The
// documentation value is the member element's INNER XML -- the C#
// `ReadInnerXml()` (the raw markup between the member tags), which the
// doc-comment renderer emits verbatim modulo the indentation stripping.
//
// KEY PORT CONVENTIONS:
//  (a) The C# streams the file with a hash-sorted index + a
//      per-key cache (the memory-footprint shape). The port loads the
//      whole file once and scans the member elements into a map -- the
//      corpus XML is a few megabytes and every render consults many
//      keys, so the up-front map is the bounded divergence from the C#'s
//      lazy streaming.
//  (b) The C# resolves localized variants + redirection attributes
//      (LookupLocalizedXmlDoc + the `redirect` attribute) and falls
//      back to the runtime ref packs. The port's loader implements the
//      ADJACENT-FILE convention only (`<assembly>.xml` beside the
//      module); the runtime discovery is deferred (the corpus and the
//      fixtures carry the adjacent file).
//  (c) The member content is captured VERBATIM (no entity decoding):
//      the C# ReadInnerXml decodes character entities in text nodes,
//      but the doc-comment lines carry the markup as written and the
//      corpus lines are entity-free; a doc with an escaped `&lt;`
//      keeps its escaped spelling (the bounded divergence, noted for
//      the member-ID slices that follow).
class XmlDocumentationProvider {
public:
    // The C# XmlDocLoader.LoadDocumentation's adjacent-file convention:
    // `<assembly>.xml` beside the module. An empty handle when the file
    // is absent (the C# null provider).
    static std::shared_ptr<XmlDocumentationProvider> LoadBeside(
        const std::string& assemblyFileName);

    // The C# `string GetDocumentation(string key)`: the member element's
    // inner XML, empty when the key has no element.
    std::string GetDocumentation(const std::string& key) const;

private:
    std::map<std::string, std::string> members_;
};

} // namespace ILSpy::Decompiler::Documentation
