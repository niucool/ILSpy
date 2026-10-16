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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PrettifyAssignments.cs -- the transform
// that simplifies `x = x op y` to `x op= y` where possible.
//
// The C# type is `class PrettifyAssignments : DepthFirstAstVisitor, IAstTransform`. The port
// derives from the void `DepthFirstAstVisitor` and `IAstTransform`.
//
// The port is landed in slices. This slice lands the compound-assignment rewrite: the direct
// `x = x op y` shape becomes `x op= y` (the structural `x` match, the
// `GetAssignmentOperatorForBinaryOperator` mapping, and the side-effect-free left-hand-side
// checks). The cast-wrapped `x = (T)(x op y)` form -- whose acceptance needs the resolver's
// implicit-conversion check (`CSharpConversions`) against the cast's type -- and the
// resolver-driven increment/decrement rewrite (`context.Settings.IntroduceIncrementAndDecrement`)
// stay deferred, named at the visit that would run them.

#pragma once

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class TransformContext;

// The C# `class PrettifyAssignments : DepthFirstAstVisitor, IAstTransform`.
class PrettifyAssignments : public Syntax::DepthFirstAstVisitor, public IAstTransform {
public:
    // The C# `void IAstTransform.Run(AstNode node, TransformContext context)`: stores the
    // context, drives the visitor over the root, and clears the context on the way out (the
    // C# `try/finally`).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public override void VisitAssignmentExpression(AssignmentExpression assignment)`:
    // walks the children first, then combines `x = x op y` into `x op= y` (and, in C#, also
    // the increment/decrement forms -- deferred).
    void VisitAssignmentExpression(Syntax::AssignmentExpression* assignment) override;

    // The C# `public static AssignmentOperatorType
    // GetAssignmentOperatorForBinaryOperator(BinaryOperatorType bop)`: the compound-assignment
    // operator corresponding to a binary operator, or `Assign` when no compound form exists.
    static Syntax::AssignmentOperatorType GetAssignmentOperatorForBinaryOperator(
        Syntax::BinaryOperatorType bop);

private:
    // The C# `static bool CanConvertToCompoundAssignment(Expression left)`: the left-hand side
    // can be reused in a compound assignment when it is side-effect free, possibly through a
    // member access, an indexer, or a pointer dereference.
    static bool CanConvertToCompoundAssignment(Syntax::Expression* left);

    // The C# `static bool IsWithoutSideEffects(Expression? left)`: the node forms that are
    // safe to evaluate twice (`this`, an identifier, a type reference, `base`).
    static bool IsWithoutSideEffects(Syntax::Expression* left);

    // The C# `[AllowNull] TransformContext context` -- the run state. Null outside a run.
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
