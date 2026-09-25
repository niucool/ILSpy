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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"

#include "Decompiler/DecompilerSettings.hpp"

#include <optional>
#include <vector>

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
// The include-order hazard (the ledger): AddCheckedBlocks.hpp /
// TransformContext.hpp transitively open ILSpy::Decompiler::CSharp::
// TypeSystem, which shadows the plain TypeSystem:: lookup -- they come
// AFTER the KnownTypeCode/IParameter includes the file's own code reads
// through the unqualified names.
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

// The C# `bool IsStringParameter(IParameter p)` local function (lines
// 396-402): a `params` array parameter's element type is unwrapped before the
// known-type check (`if (p.IsParams && ty.Kind == TypeKind.Array) ty = ((ArrayType)
// ty).ElementType`). The port's IParameter carries no IsParams flag (the C#
// `bool IParameter.IsParams` lives on the resolved interface the minimal port
// does not model), so the array-unwrap arm reads false -- only a directly
// string-typed parameter matches (the conservative direction; the params form
// is deferred with the IParameter.IsParams surface).
bool IsStringParameter(const TS::IParameter& p) {
    return TS::IsKnownType(p.Type(), TS::KnownTypeCode::String);
}

} // namespace

// The C# `internal static bool HasCheckedEquivalent(IMethod method)`
// (ReplaceMethodCallsWithOperators.cs lines 264-271).
bool ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
    const TS::IMethod& method) {
    std::string name = method.Name();
    if (name.rfind("op_", 0) == 0)
        name = "op_Checked" + name.substr(3);
    // The C# `method.DeclaringType.GetMethods(m => m.IsOperator && m.Name == name).Any()`.
    TS::ITypePtr declaringType = method.DeclaringType();
    if (!declaringType)
        return false;
    auto methods = declaringType->GetMethods([&name](const TS::IMethod* m) {
        return m != nullptr && m->IsOperator() && m->Name() == name;
    });
    return !methods.empty();
}

// The port's ToStringCallPattern.Match stand-in (the C# `static readonly Pattern
// ToStringCallPattern` Choice, lines 339-351).
ReplaceMethodCallsWithOperators::ToStringCallMatch
ReplaceMethodCallsWithOperators::MatchToStringCallPattern(Syntax::Expression* expr) {
    ToStringCallMatch match;
    if (expr == nullptr)
        return match;
    // Pattern 1: `target.ToString()` -- InvocationExpression(MemberReference
    // Expression(AnyNode("target"), "ToString")).
    if (auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr)) {
        auto* memberRef =
            dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
        if (memberRef != nullptr && memberRef->MemberName() == "ToString") {
            match.call = invocation;
            match.target = memberRef->Target();
            match.nullConditional = false;
            return match;
        }
    }
    // Pattern 2: `target?.ToString()` -- UnaryOperatorExpression(
    // NullConditionalRewrap, InvocationExpression(MemberReferenceExpression(
    // UnaryOperatorExpression(NullConditional, AnyNode("target")), "ToString"))).
    if (auto* rewrap = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr)) {
        if (rewrap->Operator() == Syntax::UnaryOperatorType::NullConditionalRewrap) {
            auto* invocation =
                dynamic_cast<Syntax::InvocationExpression*>(rewrap->Expression());
            auto* memberRef =
                invocation != nullptr
                    ? dynamic_cast<Syntax::MemberReferenceExpression*>(
                          invocation->Target())
                    : nullptr;
            if (memberRef != nullptr && memberRef->MemberName() == "ToString") {
                if (auto* nullCheck =
                        dynamic_cast<Syntax::UnaryOperatorExpression*>(
                            memberRef->Target())) {
                    if (nullCheck->Operator()
                            == Syntax::UnaryOperatorType::NullConditional) {
                        match.call = invocation;
                        match.target = nullCheck->Expression();
                        match.nullConditional = true;
                        return match;
                    }
                }
            }
        }
    }
    return match;
}

// The C# `static bool ToStringIsKnownEffectFree(IType type)` (lines 406-427).
bool ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
    const TS::IType& type) {
    // The C# `NullableType.GetUnderlyingType(type)`.
    const TS::IType& unwrapped = TS::GetUnderlyingType(type);
    const TS::ITypeDefinition* definition = unwrapped.GetDefinition();
    if (definition == nullptr)
        return false;
    // Fully qualified (the ledger's sibling-namespace rule): this TU now
    // includes TransformContext.hpp / AddCheckedBlocks.hpp, which open the
    // nested CSharp::TypeSystem -- the plain `TypeSystem::` lookup would
    // hit it instead of the sibling ILSpy::Decompiler::TypeSystem.
    using ::ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    switch (definition->KnownTypeCode()) {
        case KnownTypeCode::Boolean:
        case KnownTypeCode::Char:
        case KnownTypeCode::SByte:
        case KnownTypeCode::Byte:
        case KnownTypeCode::Int16:
        case KnownTypeCode::UInt16:
        case KnownTypeCode::Int32:
        case KnownTypeCode::UInt32:
        case KnownTypeCode::Int64:
        case KnownTypeCode::UInt64:
        case KnownTypeCode::Single:
        case KnownTypeCode::Double:
        case KnownTypeCode::Decimal:
        case KnownTypeCode::IntPtr:
        case KnownTypeCode::UIntPtr:
        case KnownTypeCode::String:
            return true;
        default:
            return false;
    }
}

// The C# `internal static Expression RemoveRedundantToStringInConcat(
// Expression expr, IMethod concatMethod, bool isLastArgument)` (lines 353-404).
Syntax::Expression* ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
    Syntax::Expression* expr, const TS::IMethod& concatMethod,
    bool isLastArgument) {
    ToStringCallMatch m = MatchToStringCallPattern(expr);
    if (m.call == nullptr)
        return expr;

    // The C# `if (!concatMethod.Parameters.All(IsStringParameter))`.
    for (const ::ILSpy::Decompiler::TypeSystem::IParameter* p : concatMethod.Parameters()) {
        if (p == nullptr || !IsStringParameter(*p))
            return expr;
    }

    // The C# `var toStringMethod = m.Get<Expression>("call").Single().GetSymbol()
    // as IMethod` -- the invocation's resolve-result symbol.
    const TS::ISymbol* symbol = CSharp::GetSymbol(*m.call);
    const TS::IMethod* toStringMethod =
        symbol != nullptr
            ? dynamic_cast<const TS::IMethod*>(symbol)
            : nullptr;

    // The C# `var type = target.GetResolveResult().Type`.
    const Semantics::ResolveResult* targetRR =
        m.target != nullptr ? CSharp::GetResolveResult(*m.target) : nullptr;
    if (targetRR == nullptr)
        return expr;
    const TS::IType& type = targetRR->Type();

    if (type.IsByRefLike()) {
        // ref structs cannot be converted to object for use with +
        return expr;
    }
    if (!(isLastArgument || ToStringIsKnownEffectFree(type))) {
        // ToString() order of evaluation matters, see CheckArgumentsForStringConcat().
        return expr;
    }
    if (type.IsReferenceType() != std::optional<bool>(false) && !m.nullConditional) {
        // ToString() might throw NullReferenceException, but the builtin operator+ doesn't.
        return expr;
    }
    if (!ToStringIsKnownEffectFree(type) && toStringMethod != nullptr
        && IL::MethodRequiresCopyForReadonlyLValue(toStringMethod)) {
        // ToString() on a struct may mutate the struct. For operator+ the C#
        // compiler creates a temporary copy before implicitly calling ToString(),
        // whereas an explicit ToString() call would mutate the original lvalue.
        // So we can't remove the compiler-generated ToString() call in cases
        // where this might make a difference.
        return expr;
    }

    // All checks succeeded, we can eliminate the ToString() call.
    // The C# compiler will generate an equivalent call if the code is recompiled.
    return m.target;
}

// ---- the instance IAstTransform surface (the user-defined-operator core) ----

// The C# `void IAstTransform.Run(AstNode rootNode, TransformContext context)`:
// `this.context = context; rootNode.AcceptVisitor(this);` in a try/finally that
// nulls the slot after (an exception re-throws with the slot cleared).
void ReplaceMethodCallsWithOperators::Run(Syntax::AstNode& rootNode,
                                          TransformContext& context) {
    context_ = &context;
    try {
        rootNode.AcceptVisitor(*this);
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

// The C# `public override void VisitInvocationExpression(InvocationExpression
// invocationExpression)`: the children first, then this node's rewrite.
void ReplaceMethodCallsWithOperators::VisitInvocationExpression(
    Syntax::InvocationExpression* invocationExpression) {
    DepthFirstAstVisitor::VisitInvocationExpression(invocationExpression);
    ProcessInvocationExpression(invocationExpression);
}

namespace {

// The C# `static BinaryOperatorType? GetBinaryOperatorTypeFromMetadataName(
// string name, out bool isChecked, DecompilerSettings settings)` (lines
// 434-484): the op_ metadata-name table. The C# 11 checked-operator names gate
// on the CheckedOperators setting; the unsigned-right-shift name on
// UnsignedRightShift.
std::optional<Syntax::BinaryOperatorType>
GetBinaryOperatorTypeFromMetadataName(const std::string& name, bool& isChecked,
                                       const DecompilerSettings& settings) {
    isChecked = false;
    if (name == "op_Addition")
        return Syntax::BinaryOperatorType::Add;
    if (name == "op_Subtraction")
        return Syntax::BinaryOperatorType::Subtract;
    if (name == "op_Multiply")
        return Syntax::BinaryOperatorType::Multiply;
    if (name == "op_Division")
        return Syntax::BinaryOperatorType::Divide;
    if (name == "op_CheckedAddition" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Add;
    }
    if (name == "op_CheckedSubtraction" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Subtract;
    }
    if (name == "op_CheckedMultiply" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Multiply;
    }
    if (name == "op_CheckedDivision" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Divide;
    }
    if (name == "op_Modulus")
        return Syntax::BinaryOperatorType::Modulus;
    if (name == "op_BitwiseAnd")
        return Syntax::BinaryOperatorType::BitwiseAnd;
    if (name == "op_BitwiseOr")
        return Syntax::BinaryOperatorType::BitwiseOr;
    if (name == "op_ExclusiveOr")
        return Syntax::BinaryOperatorType::ExclusiveOr;
    if (name == "op_LeftShift")
        return Syntax::BinaryOperatorType::ShiftLeft;
    if (name == "op_RightShift")
        return Syntax::BinaryOperatorType::ShiftRight;
    if (name == "op_UnsignedRightShift" && settings.UnsignedRightShift())
        return Syntax::BinaryOperatorType::UnsignedShiftRight;
    if (name == "op_Equality")
        return Syntax::BinaryOperatorType::Equality;
    if (name == "op_Inequality")
        return Syntax::BinaryOperatorType::InEquality;
    if (name == "op_LessThan")
        return Syntax::BinaryOperatorType::LessThan;
    if (name == "op_LessThanOrEqual")
        return Syntax::BinaryOperatorType::LessThanOrEqual;
    if (name == "op_GreaterThan")
        return Syntax::BinaryOperatorType::GreaterThan;
    if (name == "op_GreaterThanOrEqual")
        return Syntax::BinaryOperatorType::GreaterThanOrEqual;
    return std::nullopt;
}

// The C# `static UnaryOperatorType? GetUnaryOperatorTypeFromMetadataName(
// string name, out bool isChecked, DecompilerSettings settings)` (lines
// 486-514): the unary op_ metadata-name table.
std::optional<Syntax::UnaryOperatorType>
GetUnaryOperatorTypeFromMetadataName(const std::string& name, bool& isChecked,
                                      const DecompilerSettings& settings) {
    isChecked = false;
    if (name == "op_LogicalNot")
        return Syntax::UnaryOperatorType::Not;
    if (name == "op_OnesComplement")
        return Syntax::UnaryOperatorType::BitNot;
    if (name == "op_UnaryNegation")
        return Syntax::UnaryOperatorType::Minus;
    if (name == "op_CheckedUnaryNegation" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Minus;
    }
    if (name == "op_UnaryPlus")
        return Syntax::UnaryOperatorType::Plus;
    if (name == "op_Increment")
        return Syntax::UnaryOperatorType::Increment;
    if (name == "op_Decrement")
        return Syntax::UnaryOperatorType::Decrement;
    if (name == "op_CheckedIncrement" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Increment;
    }
    if (name == "op_CheckedDecrement" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Decrement;
    }
    return std::nullopt;
}

} // namespace

// The C# `void ProcessInvocationExpression(InvocationExpression
// invocationExpression)` (lines 63-262): the method symbol's metadata-name
// dispatch. THIS PORT carries the user-defined-operator arms (the binary
// table, the unary table, the explicit conversion, op_True in a condition).
// DEFERRED arms, loud at their slots: the String.Concat reduction (the
// IsStringConcat/CheckArgumentsForStringConcat gates + the
// RemoveRedundantToStringInConcat chain), the `switch (method.FullName)`
// System.* special methods (GetTypeFromHandle, GetFieldFromHandle,
// Activator.CreateInstance, RuntimeHelpers.GetSubArray, the lift/event
// operator arms), the op_Increment/op_Decrement decimal special case (the
// legacy-csc `d + 1m` reverse optimization -- the non-decimal behavior, the
// call staying, is the port's observable), and the VisitCastExpression
// methodof pattern.
void ReplaceMethodCallsWithOperators::ProcessInvocationExpression(
    Syntax::InvocationExpression* invocationExpression) {
    const TS::IMethod* method = dynamic_cast<const TS::IMethod*>(
        GetSymbol(*invocationExpression));
    if (method == nullptr)
        return;
    // The C# `var arguments = invocationExpression.Arguments.ToArray()`.
    std::vector<Syntax::Expression*> arguments;
    for (int i = 0; i < invocationExpression->Arguments().Count(); ++i)
        arguments.push_back(invocationExpression->Arguments().At(i));

    bool isChecked;
    std::optional<Syntax::BinaryOperatorType> bop =
        GetBinaryOperatorTypeFromMetadataName(
            method->Name(), isChecked, context_->DecompileRun->Settings());
    if (bop.has_value() && arguments.size() == 2) {
        context_->StepOnce("Replace operator method with binary operator",
                           invocationExpression);
        // The C# `invocationExpression.Arguments.Clear()` -- detach the
        // arguments from the invocation (the locals keep them).
        invocationExpression->Arguments().Clear();
        if (isChecked) {
            invocationExpression->AddAnnotation(CheckedAnnotationHandle());
        } else if (HasCheckedEquivalent(*method)) {
            invocationExpression->AddAnnotation(UncheckedAnnotationHandle());
        }
        auto* binaryOperator = new Syntax::BinaryOperatorExpression(
            UnwrapInDirectionExpression(arguments[0]), *bop,
            UnwrapInDirectionExpression(arguments[1]));
        CopyAnnotationsFrom(binaryOperator, *invocationExpression);
        invocationExpression->ReplaceWith(binaryOperator);
        return;
    }
    std::optional<Syntax::UnaryOperatorType> uop =
        GetUnaryOperatorTypeFromMetadataName(
            method->Name(), isChecked, context_->DecompileRun->Settings());
    if (uop.has_value() && arguments.size() == 1) {
        if (isChecked) {
            invocationExpression->AddAnnotation(CheckedAnnotationHandle());
        } else if (HasCheckedEquivalent(*method)) {
            invocationExpression->AddAnnotation(UncheckedAnnotationHandle());
        }
        if (*uop == Syntax::UnaryOperatorType::Increment
            || *uop == Syntax::UnaryOperatorType::Decrement) {
            // The C# decimal arm (the legacy-csc `d + 1m` reverse
            // optimization) is DEFERRED loudly (see the method comment) --
            // the non-decimal behavior is that `op_Increment(a)` is not
            // equivalent to `++a` and the call stays.
        } else {
            context_->StepOnce("Replace operator method with unary operator",
                               invocationExpression);
            // The C# `arguments[0].Remove()` -- detach the single argument.
            arguments[0]->Remove();
            auto* unaryOperator = new Syntax::UnaryOperatorExpression(
                UnwrapInDirectionExpression(arguments[0]), *uop);
            CopyAnnotationsFrom(unaryOperator, *invocationExpression);
            invocationExpression->ReplaceWith(unaryOperator);
        }
        return;
    }
    if ((method->Name() == "op_Explicit" || method->Name() == "op_CheckedExplicit")
        && arguments.size() == 1) {
        context_->StepOnce("Replace conversion operator method with cast",
                           invocationExpression);
        arguments[0]->Remove();
        if (method->Name() == "op_CheckedExplicit") {
            invocationExpression->AddAnnotation(CheckedAnnotationHandle());
        } else if (HasCheckedEquivalent(*method)) {
            invocationExpression->AddAnnotation(UncheckedAnnotationHandle());
        }
        auto* cast = new Syntax::CastExpression(
            context_->TypeSystemAstBuilder->ConvertType(
                const_cast<TS::IType&>(method->ReturnType())),
            UnwrapInDirectionExpression(arguments[0]));
        CopyAnnotationsFrom(cast, *invocationExpression);
        invocationExpression->ReplaceWith(cast);
        return;
    }
    if (method->Name() == "op_True" && arguments.size() == 1
        && invocationExpression->Slot() != nullptr
        && invocationExpression->Slot()->Kind()
               == &Syntax::Slots::Condition) {
        context_->StepOnce("Remove op_True from condition", invocationExpression);
        auto* condition = UnwrapInDirectionExpression(arguments[0]);
        invocationExpression->ReplaceWith(condition);
        return;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
