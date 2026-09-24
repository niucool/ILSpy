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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PrettifyAssignments.cs --
// simplifies "x = x op y" into "x op= y" and "x = x + 1" into "x++"/"--".
// Because the two "x" may refer to different ILVariables, this transform must
// run after DeclareVariables; also after ReplaceMethodCallsWithOperators (so
// custom operators work) and after AddCheckedBlocks ("for (;; x =
// unchecked(x op y))" cannot become "x += y").
//
// The C# `class PrettifyAssignments : DepthFirstAstVisitor, IAstTransform`
// ports to a concrete IAstTransform that runs the depth-first walk over an
// internal visitor (the C# context field becomes the visitor's member; the
// C# `IAstTransform.Run` explicit-interface implementation becomes the port's
// Run entry that drives the walk).

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"  // AssignmentOperatorType
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"  // BinaryOperatorType

namespace ILSpy::Decompiler::CSharp::Transforms {

class PrettifyAssignments final : public IAstTransform {
public:
    // The C# `public static AssignmentOperatorType
    // GetAssignmentOperatorForBinaryOperator(BinaryOperatorType)` -- the shared
    // mapping (the C# public static).
    static Syntax::AssignmentOperatorType GetAssignmentOperatorForBinaryOperator(
        Syntax::BinaryOperatorType bop);

    void Run(::ILSpy::Decompiler::CSharp::Syntax::AstNode& rootNode,
             TransformContext& context) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms