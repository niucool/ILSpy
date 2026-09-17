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

#include "Decompiler/CSharp/Transforms/PrettifyAssignments.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AssignmentExpression;
using Syntax::AssignmentOperatorType;
using Syntax::BinaryOperatorExpression;
using Syntax::BinaryOperatorType;
using Syntax::Expression;

namespace PM = Syntax::PatternMatching;

void PrettifyAssignments::Run(Syntax::AstNode& rootNode, TransformContext& context)
{
    context_ = &context;
    try {
        rootNode.AcceptVisitor(*this);
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

AssignmentOperatorType PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
    BinaryOperatorType bop)
{
    switch (bop) {
        case BinaryOperatorType::Add: return AssignmentOperatorType::Add;
        case BinaryOperatorType::Subtract: return AssignmentOperatorType::Subtract;
        case BinaryOperatorType::Multiply: return AssignmentOperatorType::Multiply;
        case BinaryOperatorType::Divide: return AssignmentOperatorType::Divide;
        case BinaryOperatorType::Modulus: return AssignmentOperatorType::Modulus;
        case BinaryOperatorType::ShiftLeft: return AssignmentOperatorType::ShiftLeft;
        case BinaryOperatorType::ShiftRight: return AssignmentOperatorType::ShiftRight;
        case BinaryOperatorType::UnsignedShiftRight: return AssignmentOperatorType::UnsignedShiftRight;
        case BinaryOperatorType::BitwiseAnd: return AssignmentOperatorType::BitwiseAnd;
        case BinaryOperatorType::BitwiseOr: return AssignmentOperatorType::BitwiseOr;
        case BinaryOperatorType::ExclusiveOr: return AssignmentOperatorType::ExclusiveOr;
        default: return AssignmentOperatorType::Assign;
    }
}

bool PrettifyAssignments::CanConvertToCompoundAssignment(Expression* left)
{
    if (auto* mre = dynamic_cast<Syntax::MemberReferenceExpression*>(left)) {
        return IsWithoutSideEffects(mre->Target());
    }
    if (auto* ie = dynamic_cast<Syntax::IndexerExpression*>(left)) {
        if (!IsWithoutSideEffects(ie->Target()))
            return false;
        for (int i = 0; i < ie->Arguments().Count(); i++) {
            if (!IsWithoutSideEffects(ie->Arguments()[i]))
                return false;
        }
        return true;
    }
    if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(left)) {
        if (uoe->Operator() == Syntax::UnaryOperatorType::Dereference)
            return IsWithoutSideEffects(uoe->Expression());
    }
    return IsWithoutSideEffects(left);
}

bool PrettifyAssignments::IsWithoutSideEffects(Expression* left)
{
    return dynamic_cast<Syntax::ThisReferenceExpression*>(left) != nullptr
        || dynamic_cast<Syntax::IdentifierExpression*>(left) != nullptr
        || dynamic_cast<Syntax::TypeReferenceExpression*>(left) != nullptr
        || dynamic_cast<Syntax::BaseReferenceExpression*>(left) != nullptr;
}

void PrettifyAssignments::VisitAssignmentExpression(AssignmentExpression* assignment)
{
    Syntax::DepthFirstAstVisitor::VisitAssignmentExpression(assignment);
    // Combine "x = x op y" into "x op= y".
    // The cast-wrapped "x = (T)(x op y)" variant is deferred: accepting it needs the
    // resolver's implicit-conversion check (CSharpConversions) between the binary right-hand
    // side and the cast's type, which the port cannot evaluate here.
    Expression* rhs = assignment->Right();
    if (assignment->Operator() != AssignmentOperatorType::Assign)
        return;
    auto* binary = dynamic_cast<BinaryOperatorExpression*>(rhs);
    if (binary == nullptr)
        return;
    if (!CanConvertToCompoundAssignment(assignment->Left())
        || !PM::PatternExtensions::IsMatch(*assignment->Left(), binary->Left())
        || binary->Right() == nullptr) {
        return;
    }
    AssignmentOperatorType newOperator = GetAssignmentOperatorForBinaryOperator(binary->Operator());
    if (newOperator == AssignmentOperatorType::Assign)
        return;
    if (context_ != nullptr)
        context_->Step("Convert assignment to compound assignment", assignment);
    assignment->Operator(newOperator);
    // If we found a shorter operator, get rid of the BinaryOperatorExpression.
    CopyAnnotationsFrom(assignment, *binary);
    assignment->Right(binary->Right());
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
