// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs --
// the pattern-based statement transforms over the C# AST (the cascading if-else
// simplification, the conditional-logic reassociation, the negated-equality
// rewrite, and the later foreach/for/property/destructor arms). The C#
// `PatternStatementTransform : ContextTrackingVisitor<AstNode>, IAstTransform`
// ports to the IAstTransform + internal DepthFirstAstVisitor convention (the
// NormalizeBlockStatements precedent), with the ContextTrackingVisitor re-visit
// loop (VisitChildren's do-while-until-stable) realized over the void visitor
// through an explicit result-node channel.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class PatternStatementTransform final : public IAstTransform {
public:
    void Run(::ILSpy::Decompiler::CSharp::Syntax::AstNode& rootNode,
             TransformContext& context) override;

private:
    // The C# `[AllowNull] TransformContext context` field -- the reentrancy
    // guard's state (null between runs).
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
