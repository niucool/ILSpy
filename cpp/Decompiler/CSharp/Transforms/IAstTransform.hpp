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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/IAstTransform.cs -- the C# AST
// transform contract every `CSharp/Transforms/` pass implements. The C#
// `void Run(AstNode rootNode, TransformContext context)` ports to a
// `Syntax::AstNode&` root plus a `TransformContext&` (no null in either -- the C#
// passes a real tree root and a context the caller owns).

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class TransformContext;

// The C# `public interface IAstTransform`. A C++ abstract class with a pure
// virtual `Run` (implemented by each concrete pass).
class IAstTransform {
public:
    virtual ~IAstTransform() = default;

    // The C# `void Run(AstNode rootNode, TransformContext context)`. `rootNode` is
    // the tree the transform rewrites in place; `context` carries the type-system /
    // settings / position state the pass consults.
    virtual void Run(Syntax::AstNode& rootNode, TransformContext& context) = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
