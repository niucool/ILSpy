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

// Implementation of `InsertParenthesesVisitor` (see the header for the port notes). The
// per-node `Visit` overrides parenthesize the relevant child slots, then call the inherited
// `DepthFirstAstVisitor::VisitXxx(node)` (the C# `base.VisitXxx(node)`) to recurse into the
// now-parenthesized children.

#include "Decompiler/CSharp/OutputVisitor/InsertParenthesesVisitor.hpp"

#include <functional>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AnonymousMethodExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/QueryExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SwitchExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

using namespace ILSpy::Decompiler::CSharp::Syntax;

// The C# `static void Parenthesize(Expression expr)` -- `expr.ReplaceWith(e => new
// ParenthesizedExpression { Expression = e })`. The lambda receives the removed node and
// returns a detached `ParenthesizedExpression` that wraps it (the `ReplaceWith` contract).
// `new` (not `make_unique`): the Syntax layer uses the non-owning model (`AstNode::~AstNode`
// does not delete children), mirroring the C# GC-owned `new`.
void InsertParenthesesVisitor::Parenthesize(Expression* expr) {
    expr->ReplaceWith([](AstNode* e) -> AstNode* {
        auto* paren = new ParenthesizedExpression();
        paren->Expression(static_cast<Expression*>(e));
        return paren;
    });
}

// The C# `static void ParenthesizeIfRequired(Expression? expr, PrecedenceLevel)`.
void InsertParenthesesVisitor::ParenthesizeIfRequired(Expression* expr, PrecedenceLevel minimumPrecedence) {
    if (expr == nullptr)
        return;
    if (GetPrecedence(expr) < minimumPrecedence)
        Parenthesize(expr);
}

// The C# `static PrecedenceLevel GetPrecedence(Expression expr)`.
InsertParenthesesVisitor::PrecedenceLevel InsertParenthesesVisitor::GetPrecedence(Expression* expr) {
    // `expr is QueryExpression || expr is LambdaExpression` (the spec table has no row for
    // queries/lambdas; they get a high precedence so they are parenthesized inside primaries).
    if (dynamic_cast<QueryExpression*>(expr) != nullptr || dynamic_cast<LambdaExpression*>(expr) != nullptr)
        return PrecedenceLevel::QueryOrLambda;

    if (auto* uoe = dynamic_cast<UnaryOperatorExpression*>(expr)) {
        switch (uoe->Operator()) {
            case UnaryOperatorType::PostDecrement:
            case UnaryOperatorType::PostIncrement:
            case UnaryOperatorType::NullConditional:
            case UnaryOperatorType::SuppressNullableWarning:
                return PrecedenceLevel::Primary;
            case UnaryOperatorType::NullConditionalRewrap:
                return PrecedenceLevel::NullableRewrap;
            case UnaryOperatorType::IsTrue:
                return PrecedenceLevel::Conditional;
            default:
                return PrecedenceLevel::Unary;
        }
    }

    if (dynamic_cast<CastExpression*>(expr) != nullptr)
        return PrecedenceLevel::Unary;

    if (auto* primitive = dynamic_cast<PrimitiveExpression*>(expr)) {
        // A literal is `Unary` precedence when it is a negative number (it would render with a
        // leading `-` that could bind to a surrounding operator). The C# `value is int i && i < 0`
        // etc. port to variant-alternative tests (`std::monostate` is the C# null, never negative).
        const PrimitiveValue& value = primitive->Value();
        if (std::holds_alternative<std::int32_t>(value) && std::get<std::int32_t>(value) < 0)
            return PrecedenceLevel::Unary;
        if (std::holds_alternative<std::int64_t>(value) && std::get<std::int64_t>(value) < 0)
            return PrecedenceLevel::Unary;
        if (std::holds_alternative<float>(value) && std::get<float>(value) < 0)
            return PrecedenceLevel::Unary;
        if (std::holds_alternative<double>(value) && std::get<double>(value) < 0)
            return PrecedenceLevel::Unary;
        if (std::holds_alternative<DecimalValue>(value) && std::get<DecimalValue>(value).isNegative)
            return PrecedenceLevel::Unary;
        return PrecedenceLevel::Primary;
    }

    if (auto* boe = dynamic_cast<BinaryOperatorExpression*>(expr)) {
        switch (boe->Operator()) {
            case BinaryOperatorType::Range:
                return PrecedenceLevel::Range;
            case BinaryOperatorType::Multiply:
            case BinaryOperatorType::Divide:
            case BinaryOperatorType::Modulus:
                return PrecedenceLevel::Multiplicative;
            case BinaryOperatorType::Add:
            case BinaryOperatorType::Subtract:
                return PrecedenceLevel::Additive;
            case BinaryOperatorType::ShiftLeft:
            case BinaryOperatorType::ShiftRight:
            case BinaryOperatorType::UnsignedShiftRight:
                return PrecedenceLevel::Shift;
            case BinaryOperatorType::GreaterThan:
            case BinaryOperatorType::GreaterThanOrEqual:
            case BinaryOperatorType::LessThan:
            case BinaryOperatorType::LessThanOrEqual:
                return PrecedenceLevel::RelationalAndTypeTesting;
            case BinaryOperatorType::Equality:
            case BinaryOperatorType::InEquality:
                return PrecedenceLevel::Equality;
            case BinaryOperatorType::BitwiseAnd:
                return PrecedenceLevel::BitwiseAnd;
            case BinaryOperatorType::ExclusiveOr:
                return PrecedenceLevel::ExclusiveOr;
            case BinaryOperatorType::BitwiseOr:
                return PrecedenceLevel::BitwiseOr;
            case BinaryOperatorType::ConditionalAnd:
                return PrecedenceLevel::ConditionalAnd;
            case BinaryOperatorType::ConditionalOr:
                return PrecedenceLevel::ConditionalOr;
            case BinaryOperatorType::NullCoalescing:
                return PrecedenceLevel::NullCoalescing;
            case BinaryOperatorType::IsPattern:
                return PrecedenceLevel::RelationalAndTypeTesting;
            default:
                // The C# `throw new NotSupportedException("Invalid value for BinaryOperatorType")`.
                // `Any` (the pattern wildcard) is the only unhandled value; it does not occur in a
                // real tree, so a throw mirrors the C# (the closed-throw switch convention).
                throw std::out_of_range("InsertParenthesesVisitor::GetPrecedence: invalid BinaryOperatorType");
        }
    }

    if (dynamic_cast<SwitchExpression*>(expr) != nullptr)
        return PrecedenceLevel::Switch;
    if (dynamic_cast<IsExpression*>(expr) != nullptr || dynamic_cast<AsExpression*>(expr) != nullptr)
        return PrecedenceLevel::RelationalAndTypeTesting;
    if (dynamic_cast<ConditionalExpression*>(expr) != nullptr || dynamic_cast<DirectionExpression*>(expr) != nullptr)
        return PrecedenceLevel::Conditional;
    if (dynamic_cast<AssignmentExpression*>(expr) != nullptr)
        return PrecedenceLevel::Assignment;
    return PrecedenceLevel::Primary;
}

// The C# `static bool TypeCanBeMisinterpretedAsExpression(AstType type)`.
bool InsertParenthesesVisitor::TypeCanBeMisinterpretedAsExpression(AstType* type) {
    // A `SimpleType` reads as an `IdentifierExpression`; a non-double-colon `MemberType` reads as
    // a `MemberReferenceExpression`. `PrimitiveType`/`ComposedType` can never be misread.
    if (auto* mt = dynamic_cast<MemberType*>(type))
        return !mt->IsDoubleColon();
    return dynamic_cast<SimpleType*>(type) != nullptr;
}

// The C# `static bool IsBitwise(BinaryOperatorType op)`.
bool InsertParenthesesVisitor::IsBitwise(BinaryOperatorType op) {
    return op == BinaryOperatorType::BitwiseAnd
        || op == BinaryOperatorType::BitwiseOr
        || op == BinaryOperatorType::ExclusiveOr;
}

// The C# `BinaryOperatorType? GetBinaryOperatorType(Expression? expr)`.
std::optional<BinaryOperatorType> InsertParenthesesVisitor::GetBinaryOperatorType(Expression* expr) {
    if (auto* boe = dynamic_cast<BinaryOperatorExpression*>(expr))
        return boe->Operator();
    return std::nullopt;
}

// The C# `private bool IsConditionalRefExpression(ConditionalExpression)`.
bool InsertParenthesesVisitor::IsConditionalRefExpression(ConditionalExpression* conditionalExpression) {
    return dynamic_cast<DirectionExpression*>(conditionalExpression->TrueExpression()) != nullptr
        || dynamic_cast<DirectionExpression*>(conditionalExpression->FalseExpression()) != nullptr;
}

// The C# `private void HandleAssignmentRHS(Expression right)`.
void InsertParenthesesVisitor::HandleAssignmentRHS(Expression* right) {
    if (right == nullptr)
        return;
    if (InsertParenthesesForReadability && dynamic_cast<DirectionExpression*>(right) == nullptr) {
        ParenthesizeIfRequired(right, PrecedenceLevel::Conditional + 1);
    } else {
        ParenthesizeIfRequired(right, PrecedenceLevel::Assignment);
    }
}

// The C# `void HandleLambdaOrQuery(Expression expr)`.
void InsertParenthesesVisitor::HandleLambdaOrQuery(Expression* expr) {
    // `expr.Slot?.Kind == Slots.Left` (a lambda/query used as the LEFT operand of a binary
    // operator): the greedy query/lambda end would swallow the operator, so parenthesize.
    if (expr->Slot() != nullptr && expr->Slot()->Kind() == &Slots::Left)
        Parenthesize(expr);
    if (dynamic_cast<IsExpression*>(expr->Parent()) != nullptr
        || dynamic_cast<AsExpression*>(expr->Parent()) != nullptr)
        Parenthesize(expr);
    if (InsertParenthesesForReadability) {
        if (dynamic_cast<UnaryOperatorExpression*>(expr->Parent()) != nullptr
            || dynamic_cast<BinaryOperatorExpression*>(expr->Parent()) != nullptr)
            Parenthesize(expr);
    }
}

// ---- Primary expressions -----------------------------------------------

void InsertParenthesesVisitor::VisitMemberReferenceExpression(MemberReferenceExpression* memberReferenceExpression) {
    ParenthesizeIfRequired(memberReferenceExpression->Target(), PrecedenceLevel::Primary);
    DepthFirstAstVisitor::VisitMemberReferenceExpression(memberReferenceExpression);
}

void InsertParenthesesVisitor::VisitPointerReferenceExpression(PointerReferenceExpression* pointerReferenceExpression) {
    ParenthesizeIfRequired(pointerReferenceExpression->Target(), PrecedenceLevel::Primary);
    DepthFirstAstVisitor::VisitPointerReferenceExpression(pointerReferenceExpression);
}

void InsertParenthesesVisitor::VisitInvocationExpression(InvocationExpression* invocationExpression) {
    ParenthesizeIfRequired(invocationExpression->Target(), PrecedenceLevel::Primary);
    DepthFirstAstVisitor::VisitInvocationExpression(invocationExpression);
}

void InsertParenthesesVisitor::VisitIndexerExpression(IndexerExpression* indexerExpression) {
    // An implicit-element-access indexer (e.g. the `[i]` in a dictionary initializer) has no
    // target.
    if (indexerExpression->Target() != nullptr) {
        ParenthesizeIfRequired(indexerExpression->Target(), PrecedenceLevel::Primary);
        // The C# `switch (indexerExpression.Target)` with `when` guards: an `ArrayCreateExpression`
        // or `StackAllocExpression` target without an initializer (or always, in readability mode)
        // must be parenthesized -- `(new int[1])[0]` / `(stackalloc int[1])[0]`.
        Expression* target = indexerExpression->Target();
        if (auto* ace = dynamic_cast<ArrayCreateExpression*>(target)) {
            if (InsertParenthesesForReadability || ace->Initializer() == nullptr)
                Parenthesize(indexerExpression->Target());
        } else if (auto* sae = dynamic_cast<StackAllocExpression*>(target)) {
            if (InsertParenthesesForReadability || sae->Initializer() == nullptr)
                Parenthesize(indexerExpression->Target());
        }
    }
    DepthFirstAstVisitor::VisitIndexerExpression(indexerExpression);
}

// ---- Unary expressions -------------------------------------------------

void InsertParenthesesVisitor::VisitUnaryOperatorExpression(UnaryOperatorExpression* unaryOperatorExpression) {
    ParenthesizeIfRequired(unaryOperatorExpression->Expression(), GetPrecedence(unaryOperatorExpression));
    // In readability mode, parenthesize a nested unary operand (e.g. `-(-a)`).
    UnaryOperatorExpression* child = dynamic_cast<UnaryOperatorExpression*>(unaryOperatorExpression->Expression());
    if (child != nullptr && InsertParenthesesForReadability)
        Parenthesize(child);
    DepthFirstAstVisitor::VisitUnaryOperatorExpression(unaryOperatorExpression);
}

void InsertParenthesesVisitor::VisitCastExpression(CastExpression* castExpression) {
    // Even in readability mode, don't parenthesize casts of casts.
    if (dynamic_cast<CastExpression*>(castExpression->Expression()) == nullptr) {
        ParenthesizeIfRequired(castExpression->Expression(),
            InsertParenthesesForReadability ? PrecedenceLevel::NullableRewrap : PrecedenceLevel::Unary);
    }
    // The C# grammar ambiguity: `(int)-1` is fine but `(A)-b` is not a cast. A cast whose operand
    // is a unary operator other than `~`/`!`, with a type that could be misread as an expression,
    // needs the operand parenthesized.
    UnaryOperatorExpression* uoe = dynamic_cast<UnaryOperatorExpression*>(castExpression->Expression());
    if (uoe != nullptr && !(uoe->Operator() == UnaryOperatorType::BitNot || uoe->Operator() == UnaryOperatorType::Not)) {
        if (TypeCanBeMisinterpretedAsExpression(castExpression->Type()))
            Parenthesize(castExpression->Expression());
    }
    // The same ambiguity with a negative literal operand: `(int)-1` needs `(-1)` parenthesized
    // when the type could be misread as an expression. The C# `Type.GetTypeCode` switch over the
    // signed numeric alternatives; the variant stores small signed ints as `int32` (the C# SByte
    // / Int16 cases are subsumed by the Int32 case), so the int32/int64/float/double/decimal
    // alternatives are tested.
    PrimitiveExpression* pe = dynamic_cast<PrimitiveExpression*>(castExpression->Expression());
    if (pe != nullptr && !std::holds_alternative<std::monostate>(pe->Value())) {  // `pe.Value != null`
        if (TypeCanBeMisinterpretedAsExpression(castExpression->Type())) {
            const PrimitiveValue& value = pe->Value();
            if (std::holds_alternative<std::int32_t>(value) && std::get<std::int32_t>(value) < 0)
                Parenthesize(castExpression->Expression());
            else if (std::holds_alternative<std::int64_t>(value) && std::get<std::int64_t>(value) < 0)
                Parenthesize(castExpression->Expression());
            else if (std::holds_alternative<float>(value) && std::get<float>(value) < 0)
                Parenthesize(castExpression->Expression());
            else if (std::holds_alternative<double>(value) && std::get<double>(value) < 0)
                Parenthesize(castExpression->Expression());
            else if (std::holds_alternative<DecimalValue>(value) && std::get<DecimalValue>(value).isNegative)
                Parenthesize(castExpression->Expression());
        }
    }
    DepthFirstAstVisitor::VisitCastExpression(castExpression);
}

// ---- Binary operators --------------------------------------------------

void InsertParenthesesVisitor::VisitBinaryOperatorExpression(BinaryOperatorExpression* binaryOperatorExpression) {
    PrecedenceLevel precedence = GetPrecedence(binaryOperatorExpression);
    if (binaryOperatorExpression->Operator() == BinaryOperatorType::NullCoalescing) {
        if (InsertParenthesesForReadability) {
            ParenthesizeIfRequired(binaryOperatorExpression->Left(), PrecedenceLevel::NullableRewrap);
            if (GetBinaryOperatorType(binaryOperatorExpression->Right()) == BinaryOperatorType::NullCoalescing) {
                ParenthesizeIfRequired(binaryOperatorExpression->Right(), precedence);
            } else {
                ParenthesizeIfRequired(binaryOperatorExpression->Right(), PrecedenceLevel::NullableRewrap);
            }
        } else {
            // `??` is right-associative: the left operand needs one higher precedence.
            ParenthesizeIfRequired(binaryOperatorExpression->Left(), precedence + 1);
            ParenthesizeIfRequired(binaryOperatorExpression->Right(), precedence);
        }
    } else {
        if (InsertParenthesesForReadability && precedence < PrecedenceLevel::Equality) {
            // Boost the priority of the left side unless it is the same operator; the right side
            // is always boosted (bitwise ops boost only to `Unary`, others to `Equality`).
            PrecedenceLevel boostTo = IsBitwise(binaryOperatorExpression->Operator())
                ? PrecedenceLevel::Unary : PrecedenceLevel::Equality;
            if (GetBinaryOperatorType(binaryOperatorExpression->Left()) == binaryOperatorExpression->Operator()) {
                ParenthesizeIfRequired(binaryOperatorExpression->Left(), precedence);
            } else {
                ParenthesizeIfRequired(binaryOperatorExpression->Left(), boostTo);
            }
            ParenthesizeIfRequired(binaryOperatorExpression->Right(), boostTo);
        } else {
            // All other binary operators are left-associative: the right operand needs one
            // higher precedence than the left.
            ParenthesizeIfRequired(binaryOperatorExpression->Left(), precedence);
            ParenthesizeIfRequired(binaryOperatorExpression->Right(), precedence + 1);
        }
    }
    DepthFirstAstVisitor::VisitBinaryOperatorExpression(binaryOperatorExpression);
}

// ---- `is` / `as` -------------------------------------------------------

void InsertParenthesesVisitor::VisitIsExpression(IsExpression* isExpression) {
    if (InsertParenthesesForReadability) {
        // The precedence of `is` is not widely known, so always parenthesize in readable mode.
        ParenthesizeIfRequired(isExpression->Expression(), PrecedenceLevel::NullableRewrap);
    } else {
        ParenthesizeIfRequired(isExpression->Expression(), PrecedenceLevel::RelationalAndTypeTesting);
    }
    DepthFirstAstVisitor::VisitIsExpression(isExpression);
}

void InsertParenthesesVisitor::VisitAsExpression(AsExpression* asExpression) {
    if (InsertParenthesesForReadability) {
        ParenthesizeIfRequired(asExpression->Expression(), PrecedenceLevel::NullableRewrap);
    } else {
        ParenthesizeIfRequired(asExpression->Expression(), PrecedenceLevel::RelationalAndTypeTesting);
    }
    DepthFirstAstVisitor::VisitAsExpression(asExpression);
}

// ---- Interpolation -----------------------------------------------------

namespace {
// The C# nested local function `InterpolationNeedsParenthesis(AstNode node)` -- true when the
// subtree contains a `global::` (`MemberType { IsDoubleColon: true }`) that is not shielded by a
// `ParenthesizedExpression` or an anonymous-method / block-bodied lambda. An
// `InvocationExpression` recurses on its target, a `CastExpression` on its operand; otherwise
// the sibling chain (`FirstChild`/`NextSibling`) is walked.
bool InterpolationNeedsParenthesis(AstNode* node) {
    if (node == nullptr)
        return false;
    if (auto* mt = dynamic_cast<MemberType*>(node)) {
        if (mt->IsDoubleColon())
            return true;
    }
    if (dynamic_cast<ParenthesizedExpression*>(node) != nullptr)
        return false;
    if (dynamic_cast<AnonymousMethodExpression*>(node) != nullptr)
        return false;
    if (auto* lam = dynamic_cast<LambdaExpression*>(node)) {
        if (dynamic_cast<BlockStatement*>(lam->Body()) != nullptr)
            return false;
    }
    if (auto* invocation = dynamic_cast<InvocationExpression*>(node))
        return InterpolationNeedsParenthesis(invocation->Target());
    if (auto* cast = dynamic_cast<CastExpression*>(node))
        return InterpolationNeedsParenthesis(cast->Expression());
    for (AstNode* child = node->FirstChild(); child != nullptr; child = child->NextSibling()) {
        if (InterpolationNeedsParenthesis(child))
            return true;
    }
    return false;
}
} // namespace

void InsertParenthesesVisitor::VisitInterpolation(Interpolation* interpolation) {
    // Do this first, in case the descendents parenthesize themselves.
    DepthFirstAstVisitor::VisitInterpolation(interpolation);
    // If an interpolation contains `global::`, parenthesize the expression.
    if (InterpolationNeedsParenthesis(interpolation))
        Parenthesize(interpolation->Expression());
}

// ---- Conditional `?:` --------------------------------------------------

void InsertParenthesesVisitor::VisitConditionalExpression(ConditionalExpression* conditionalExpression) {
    // Inside a string interpolation `?:` always needs parentheses.
    if (dynamic_cast<Interpolation*>(conditionalExpression->Parent()) != nullptr)
        Parenthesize(conditionalExpression);
    // `?:` associativity: only the CONDITION of an outer `?:` strictly needs parens; readability
    // mode parenthesizes all three arms (unless this is a ref-conditional).
    if (InsertParenthesesForReadability && !IsConditionalRefExpression(conditionalExpression)) {
        ParenthesizeIfRequired(conditionalExpression->Condition(), PrecedenceLevel::NullableRewrap);
        ParenthesizeIfRequired(conditionalExpression->TrueExpression(), PrecedenceLevel::NullableRewrap);
        ParenthesizeIfRequired(conditionalExpression->FalseExpression(), PrecedenceLevel::NullableRewrap);
    } else {
        ParenthesizeIfRequired(conditionalExpression->Condition(), PrecedenceLevel::Conditional + 1);
        ParenthesizeIfRequired(conditionalExpression->TrueExpression(), PrecedenceLevel::Conditional);
        ParenthesizeIfRequired(conditionalExpression->FalseExpression(), PrecedenceLevel::Conditional);
    }
    DepthFirstAstVisitor::VisitConditionalExpression(conditionalExpression);
}

// ---- Assignment --------------------------------------------------------

void InsertParenthesesVisitor::VisitAssignmentExpression(AssignmentExpression* assignmentExpression) {
    // An assignment inside an array initializer needs extra parens to disambiguate a member
    // assignment from an initializer element (the resolver uses `NamedExpression` for the
    // member-assignment shape, so an `IndexerExpression` left is left unparenthesized).
    if (dynamic_cast<ArrayInitializerExpression*>(assignmentExpression->Parent()) != nullptr
        && dynamic_cast<IndexerExpression*>(assignmentExpression->Left()) == nullptr) {
        Parenthesize(assignmentExpression);
    }
    // Assignment is right-associative: the left operand needs one higher precedence.
    ParenthesizeIfRequired(assignmentExpression->Left(), PrecedenceLevel::Assignment + 1);
    HandleAssignmentRHS(assignmentExpression->Right());
    DepthFirstAstVisitor::VisitAssignmentExpression(assignmentExpression);
}

void InsertParenthesesVisitor::VisitVariableInitializer(VariableInitializer* variableInitializer) {
    if (variableInitializer->Initializer() != nullptr)
        HandleAssignmentRHS(variableInitializer->Initializer());
    DepthFirstAstVisitor::VisitVariableInitializer(variableInitializer);
}

// ---- Lambdas / queries -------------------------------------------------

void InsertParenthesesVisitor::VisitQueryExpression(QueryExpression* queryExpression) {
    HandleLambdaOrQuery(queryExpression);
    DepthFirstAstVisitor::VisitQueryExpression(queryExpression);
}

void InsertParenthesesVisitor::VisitLambdaExpression(LambdaExpression* lambdaExpression) {
    HandleLambdaOrQuery(lambdaExpression);
    DepthFirstAstVisitor::VisitLambdaExpression(lambdaExpression);
}

// ---- Named argument / switch expression --------------------------------

void InsertParenthesesVisitor::VisitNamedExpression(NamedExpression* namedExpression) {
    if (InsertParenthesesForReadability)
        ParenthesizeIfRequired(namedExpression->Expression(), PrecedenceLevel::RelationalAndTypeTesting + 1);
    DepthFirstAstVisitor::VisitNamedExpression(namedExpression);
}

void InsertParenthesesVisitor::VisitSwitchExpression(SwitchExpression* switchExpression) {
    ParenthesizeIfRequired(switchExpression->Expression(), PrecedenceLevel::Switch + 1);
    DepthFirstAstVisitor::VisitSwitchExpression(switchExpression);
}

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor
