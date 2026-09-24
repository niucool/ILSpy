// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/NormalizeBlockStatements.cs --
// normalizes embedded statements: with AlwaysUseBraces every embedded statement
// except an `else if` gets a block wrapper; without it, redundant single-statement
// blocks are unwrapped and non-block statements that may not appear as embedded
// statements get a block. Also records the single top-level namespace for the
// FileScopedNamespaces setting, and (with
// UseExpressionBodyForCalculatedGetterOnlyProperties) converts a getter-only
// property/indexer whose getter is a single return into an expression body.
// The C# `class ... : DepthFirstAstVisitor, IAstTransform` ports to a concrete
// IAstTransform driving an internal DepthFirstAstVisitor.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class NormalizeBlockStatements final : public IAstTransform {
public:
    void Run(::ILSpy::Decompiler::CSharp::Syntax::AstNode& rootNode,
             TransformContext& context) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms