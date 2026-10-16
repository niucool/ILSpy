// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/AddXmlDocumentationTransform.cs --
// the final `IAstTransform` in the `CSharpDecompiler.GetAstTransforms()` order. For
// every `EntityDeclaration` it asks the run's `DocumentationProvider` for the
// entity's XML documentation and prepends the doc's lines as `CommentType.
// Documentation` trivia. A parameterized property decompiles to its accessor
// methods, so the property's documentation is shown on its first accessor (getter
// else setter).
//
// It is the last transform of the `GetAstTransforms()` list, following
// `FixNameCollisions`. The transform only reads the context for the settings, the
// provider, and the debug `Step` no-ops.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class AddXmlDocumentationTransform : IAstTransform`. Stateless; a
// single instance runs over the whole tree.
class AddXmlDocumentationTransform : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: the
    // early-out when XML documentation is disabled or no provider is set, the
    // per-`EntityDeclaration` documentation lookup (with the parameterized-property
    // accessor fallback), and the line-wise trivia insertion. An `XmlException`
    // thrown while reading a doc string is reported as a leading documentation
    // comment on the root.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

private:
    // The C# `static void InsertXmlDocumentation(AstNode node, StringReader r)`:
    // read all lines, find the first non-blank line to derive the shared
    // indentation, then prepend each non-blank line as a `" " + line` documentation
    // comment, restoring the blank lines that were skipped between them.
    static void InsertXmlDocumentation(Syntax::AstNode& node, const std::string& doc);
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
