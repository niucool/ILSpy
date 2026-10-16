// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/FlattenSwitchBlocks.cs -- the AST
// transform that unwraps a switch section whose sole statement is a block (the
// `case x: { ... }` shape) so the C# output does not carry a redundant brace scope
// around the case body. It walks every `SwitchSection` descendant; when the section has
// exactly one statement and that statement is a `BlockStatement` carrying no local
// declaration that would change scope meaning, the block is removed and its statements
// are moved up into the section.
//
// The local-declaration guard (`ContainsLocalDeclaration`) walks a section statement's
// descendants looking for a `VariableDeclarationStatement`, `LocalFunctionDeclarationStatement`,
// or `OutVarDeclarationExpression`; a nested `BlockStatement` stops the walk (its own scope
// preserves the declaration), so only declarations that would be hoisted into the switch
// section by the flatten suppress it.
//
// It is the fourth concrete `IAstTransform` over the iteration-160 `TransformContext`
// foundation, following `EscapeInvalidIdentifiers`, `RemoveCLSCompliantAttribute`, and
// `RemoveCompilerGeneratedAssemblyAttributes` in the `CSharpDecompiler.GetAstTransforms()`
// order (the transform sits after `NormalizeBlockStatements`). The transform never consults
// the context beyond the debug `Step` no-op, so `Run` ignores its `TransformContext`
// argument.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `class FlattenSwitchBlocks : IAstTransform`. The transform is stateless; a single
// instance runs over the whole tree.
class FlattenSwitchBlocks : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: the
    // `rootNode.Descendants.OfType<SwitchSection>()` walk, the single-block-statement
    // filter, the `ContainsLocalDeclaration` guard, and the block-to-section statement
    // move. `context` is only read for the debug `Step` call (a no-op in the port).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
