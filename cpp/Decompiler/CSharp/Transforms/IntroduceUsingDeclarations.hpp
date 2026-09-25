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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/IntroduceUsingDeclarations.cs -- the
// pass that gathers the namespaces the tree references, inserts the `using` declarations
// (when `DecompilerSettings.UsingDeclarations` is on), attaches the root `UsingScope`
// annotation the later `IntroduceExtensionMethods` pass reads, and then rewrites the
// still-ambiguous simple type names to their qualified form.
//
// The class is declared here with only the `IAstTransform` surface so this header does not
// pull the `CSharp::TypeSystem` namespace into every translation unit that includes it (the
// `IntroduceExtensionMethods` convention): the two nested visitors (`FindRequiredImports`
// and `FullyQualifyAmbiguousTypeNamesVisitor`) live in the .cpp.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class IntroduceUsingDeclarations : IAstTransform`.
class IntroduceUsingDeclarations : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: gathers the
    // required imports, inserts the `using` declarations, builds and attaches the root
    // `UsingScope`, then fully qualifies the ambiguous simple type names.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
