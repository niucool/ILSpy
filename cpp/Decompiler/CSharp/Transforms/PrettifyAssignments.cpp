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

#include "Decompiler/CSharp/Transforms/PrettifyAssignments.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"  // ILInstructionAnnotation
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <any>
#include <cstdio>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {
namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace CS = ::ILSpy::Decompiler::CSharp;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Util = ::ILSpy::Decompiler::Util;
namespace SemNS = ::ILSpy::Decompiler::Semantics;
namespace UtilNS = ::ILSpy::Decompiler::Util;
namespace ResolverNS = ::ILSpy::Decompiler::CSharp::Resolver;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// The C# `static bool IsWithoutSideEffects(Expression? left)`.
bool IsWithoutSideEffects(const Syntax::Expression* left) {
    if (left == nullptr) return false;
    return dynamic_cast<const Syntax::ThisReferenceExpression*>(left) != nullptr ||
           dynamic_cast<const Syntax::IdentifierExpression*>(left) != nullptr ||
           dynamic_cast<const Syntax::TypeReferenceExpression*>(left) != nullptr ||
           dynamic_cast<const Syntax::BaseReferenceExpression*>(left) != nullptr;
}

// The C# `static bool CanConvertToCompoundAssignment(Expression left)`.
bool CanConvertToCompoundAssignment(const Syntax::Expression* left) {
    if (left == nullptr) return false;
    if (const auto* mre = dynamic_cast<const Syntax::MemberReferenceExpression*>(left)) {
        return IsWithoutSideEffects(mre->Target());
    }
    if (const auto* ie = dynamic_cast<const Syntax::IndexerExpression*>(left)) {
        bool all = true;
        for (int i = 0; i < ie->Arguments().Count(); ++i) {
            if (!IsWithoutSideEffects(ie->Arguments().At(i))) all = false;
        }
        return IsWithoutSideEffects(ie->Target()) && all;
    }
    if (const auto* uoe = dynamic_cast<const Syntax::UnaryOperatorExpression*>(
            const_cast<Syntax::Expression*>(left));
        uoe != nullptr && uoe->Operator() == Syntax::UnaryOperatorType::Dereference) {
        return IsWithoutSideEffects(uoe->Expression());
    }
    return IsWithoutSideEffects(left);
}

// The C# local function `bool IsImplicitlyConvertible(Expression rhs, IType?
// expectedType)`: no expected type is trivially true; otherwise the CSharpConversions
// implicit conversion from the rhs's resolve-result type.
bool IsImplicitlyConvertible(const Syntax::Expression& rhs,
                             const TS::IType* expectedType,
                             TransformContext& context) {
    if (expectedType == nullptr) return true;
    const Sem::ResolveResult* rr = CS::GetResolveResult(rhs);
    if (rr == nullptr) return false;
    auto conversion = ResolverNS::CSharpConversions::Get(*context.TypeSystem)
                          .ImplicitConversion(
                              *const_cast<TS::IType*>(&rr->Type()),
                              *const_cast<TS::IType*>(expectedType));
    return conversion != nullptr && conversion->IsImplicit();
}

// The C# `assignment.Annotation<IL.CallInstruction>() == null &&
// assignment.Annotation<IL.UserDefinedCompoundAssign>() == null &&
// assignment.Annotation<IL.DynamicCompoundAssign>() == null` -- the port's
// IL-instruction annotation channel carries every instruction kind on one
// annotation type, so the check scans the aliased instructions for the three
// op-code families (a call, the user-defined compound fold, the dynamic
// compound fold).
bool IsCustomOperatorAssignment(const Syntax::AssignmentExpression& assignment) {
    for (IL::ILInstruction* inst : CS::GetILInstructions(assignment)) {
        if (inst == nullptr) continue;
        if (inst->Op == IL::OpCode::UserDefinedCompoundAssign ||
            inst->Op == IL::OpCode::DynamicCompoundAssign) {
            return true;
        }
        if (auto* call = dynamic_cast<IL::Call*>(inst);
            call != nullptr && !call->IsNewObj) {
            return true;
        }
    }
    return false;
}

// The C# `class PrettifyAssignments : DepthFirstAstVisitor, IAstTransform` --
// the visitor half. The C# `AllowNull TransformContext context` field ports to
// a pointer; the C# `IAstTransform.Run` explicit-interface implementation is
// the outer Run that drives `node.AcceptVisitor(*this)`.
class PrettifyAssignmentsVisitor final : public Syntax::DepthFirstAstVisitor {
public:
    TransformContext* context = nullptr;

    // The C# `public override void VisitAssignmentExpression(AssignmentExpression
    // assignment)`.
    void VisitAssignmentExpression(Syntax::AssignmentExpression* assignment) override {
        if (assignment == nullptr || context == nullptr) return;
        DepthFirstAstVisitor::VisitAssignmentExpression(assignment);
        // Combine "x = x op y" into "x op= y".
        // Also supports "x = (T)(x op y)" -> "x op= y", if x.GetType() == T
        // and y is implicitly convertible to T.
        Syntax::Expression* rhs = assignment->Right();
        const TS::IType* expectedType = nullptr;
        if (auto* cast = dynamic_cast<Syntax::CastExpression*>(assignment->Right())) {
            // The C# `astType.GetResolveResult().Type` -- the cast's type
            // annotation carries the resolve result; an unannotated AstType
            // yields the UnknownError fallback whose Type() is Unknown (not a
            // usable expected type, so treat it as absent).
            rrTypeHolder_ = CS::GetSharedResolveResult(*cast->Type());
            if (rrTypeHolder_ != nullptr && !rrTypeHolder_->IsError()) {
                expectedType = &rrTypeHolder_->Type();
            }
            rhs = cast->Expression();
        }
        if (auto* binary = dynamic_cast<Syntax::BinaryOperatorExpression*>(rhs);
            binary != nullptr &&
            assignment->Operator() == Syntax::AssignmentOperatorType::Assign) {
            if (CanConvertToCompoundAssignment(assignment->Left()) &&
                static_cast<Syntax::PatternMatching::INode*>(assignment->Left())
                    ->DoMatch(binary->Left(), Syntax::PatternMatching::Match::CreateNew()) &&
                binary->Right() != nullptr &&
                IsImplicitlyConvertible(*binary->Right(), expectedType, *context)) {
                const Syntax::AssignmentOperatorType newOperator =
                    PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
                        binary->Operator());
                if (newOperator != Syntax::AssignmentOperatorType::Assign) {
                    context->StepOnce("Convert assignment to compound assignment",
                                      assignment);
                    assignment->Operator(newOperator);
                    // If we found a shorter operator, get rid of the
                    // BinaryOperatorExpression:
                    CS::CopyAnnotationsFrom(assignment, *binary);
                    assignment->Right(binary->Right());
                }
            }
        }
        if (context->DecompileRun != nullptr &&
            context->DecompileRun->Settings().IntroduceIncrementAndDecrement() &&
            (assignment->Operator() == Syntax::AssignmentOperatorType::Add ||
             assignment->Operator() == Syntax::AssignmentOperatorType::Subtract)) {
            // detect increment/decrement; ++/-- on float/double compiles to the
            // same IL as adding/subtracting a constant 1 (which both types
            // represent exactly), so a constant 1 right-hand side qualifies
            // there just like for integers
            const Sem::ResolveResult* rr =
                CS::GetResolveResult(*assignment->Right());
            if (rr != nullptr && rr->IsCompileTimeConstant() &&
                (TS::IsCSharpPrimitiveIntegerType(&rr->Type()) ||
                 TS::IsKnownType(rr->Type(), TS::KnownTypeCode::Single) ||
                 TS::IsKnownType(rr->Type(), TS::KnownTypeCode::Double))) {
                // The C# casts the literal `1` to the constant's own type code
                // and compares with the constant value.
                const std::any castOne = UtilNS::Cast(
                    TS::GetTypeCode(rr->Type()), std::any(1),
                    /*checkForOverflow=*/false);
                const bool matches = BoxedValuesEqual(rr->ConstantValue(), castOne);
                if (matches) {
                    // only if it's not a custom operator
                    if (!IsCustomOperatorAssignment(*assignment)) {
                        Syntax::UnaryOperatorType type;
                        // When the parent is an expression statement, pre- or
                        // post-increment doesn't matter; so we can pick
                        // post-increment which is more commonly used (for (int
                        // i = 0; i < x; i++))
                        if (dynamic_cast<Syntax::ExpressionStatement*>(
                                assignment->Parent()) != nullptr) {
                            type = assignment->Operator() ==
                                           Syntax::AssignmentOperatorType::Add
                                       ? Syntax::UnaryOperatorType::PostIncrement
                                       : Syntax::UnaryOperatorType::PostDecrement;
                        } else {
                            type = assignment->Operator() ==
                                           Syntax::AssignmentOperatorType::Add
                                       ? Syntax::UnaryOperatorType::Increment
                                       : Syntax::UnaryOperatorType::Decrement;
                        }
                        context->StepOnce(
                            type == Syntax::UnaryOperatorType::Increment ||
                                    type == Syntax::UnaryOperatorType::PostIncrement
                                ? "Convert assignment to increment"
                                : "Convert assignment to decrement",
                            assignment);
                        auto* unaryOperator = new Syntax::UnaryOperatorExpression(
                            Syntax::Detach(assignment->Left()), type);
                        CS::CopyAnnotationsFrom(unaryOperator, *assignment);
                        assignment->ReplaceWith(unaryOperator);
                        // The replaced node is destroyed; do not touch it.
                    }
                }
            }
        }
        rrTypeHolder_ = nullptr;
    }

private:
    static bool BoxedValuesEqual(const std::any& a, const std::any& b) {
        if (a.type() != b.type()) return false;
        if (const int* ia = std::any_cast<int>(&a)) {
            return *ia == *std::any_cast<int>(&b);
        }
        if (const double* da = std::any_cast<double>(&a)) {
            return *da == *std::any_cast<double>(&b);
        }
        if (const long long* la = std::any_cast<long long>(&a)) {
            return *la == *std::any_cast<long long>(&b);
        }
        if (const float* fa = std::any_cast<float>(&a)) {
            return *fa == *std::any_cast<float>(&b);
        }
        return false;
    }

    // The shared resolve-result handle behind the cast's type annotation (the
    // annotation channel owns it; the pointer into it is only read within one
    // VisitAssignmentExpression invocation).
    std::shared_ptr<Sem::ResolveResult> rrTypeHolder_;
};

} // namespace

Syntax::AssignmentOperatorType
PrettifyAssignments::GetAssignmentOperatorForBinaryOperator(
    Syntax::BinaryOperatorType bop) {
    switch (bop) {
        case Syntax::BinaryOperatorType::Add:
            return Syntax::AssignmentOperatorType::Add;
        case Syntax::BinaryOperatorType::Subtract:
            return Syntax::AssignmentOperatorType::Subtract;
        case Syntax::BinaryOperatorType::Multiply:
            return Syntax::AssignmentOperatorType::Multiply;
        case Syntax::BinaryOperatorType::Divide:
            return Syntax::AssignmentOperatorType::Divide;
        case Syntax::BinaryOperatorType::Modulus:
            return Syntax::AssignmentOperatorType::Modulus;
        case Syntax::BinaryOperatorType::ShiftLeft:
            return Syntax::AssignmentOperatorType::ShiftLeft;
        case Syntax::BinaryOperatorType::ShiftRight:
            return Syntax::AssignmentOperatorType::ShiftRight;
        case Syntax::BinaryOperatorType::UnsignedShiftRight:
            return Syntax::AssignmentOperatorType::UnsignedShiftRight;
        case Syntax::BinaryOperatorType::BitwiseAnd:
            return Syntax::AssignmentOperatorType::BitwiseAnd;
        case Syntax::BinaryOperatorType::BitwiseOr:
            return Syntax::AssignmentOperatorType::BitwiseOr;
        case Syntax::BinaryOperatorType::ExclusiveOr:
            return Syntax::AssignmentOperatorType::ExclusiveOr;
        default:
            return Syntax::AssignmentOperatorType::Assign;
    }
}

void PrettifyAssignments::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    PrettifyAssignmentsVisitor visitor;
    visitor.context = &context;
    rootNode.AcceptVisitor(visitor);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms