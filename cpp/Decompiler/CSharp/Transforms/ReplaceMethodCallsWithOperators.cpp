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
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
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

namespace PatternMatching = ::ILSpy::Decompiler::CSharp::Syntax::PatternMatching;

// The C# `static readonly MemberReferenceExpression
// typeHandleOnTypeOfPattern` (lines 39-46): `__.TypeHandle` where __ is a
// Choice of `typeof(...)` or the `__refvalue`-shaped UndocumentedExpression
// -- the RuntimeTypeHandle argument form the typeof rewrite unwraps.
struct TypeHandleOnTypeOfPatternHolder {
    PatternMatching::AnyNode typeOfOperand;
    Syntax::TypeOfExpression typeOf{Syntax::AstType::ToType(typeOfOperand)};
    // The C# `new UndocumentedExpression { UndocumentedExpressionType =
    // UndocumentedExpressionType.RefType, Arguments = { new AnyNode() } }`.
    Syntax::UndocumentedExpression refValue{
        Syntax::UndocumentedExpressionType::RefType};
    PatternMatching::AnyNode refValueOperand;
    PatternMatching::Choice targetChoice;
    Syntax::MemberReferenceExpression pattern{
        Syntax::Expression::ToExpression(targetChoice), "TypeHandle"};

    TypeHandleOnTypeOfPatternHolder() {
        refValue.Arguments().Add(Syntax::Expression::ToExpression(
            refValueOperand));
        targetChoice.Add(typeOf);
        targetChoice.Add(refValue);
    }
};

Syntax::MemberReferenceExpression& TypeHandleOnTypeOfPattern() {
    static TypeHandleOnTypeOfPatternHolder holder;
    return holder.pattern;
}

// The C# `private bool IsStringConcat(IParameterizedMember member)`
// (lines 332-336): the method is a String.Concat overload.
bool IsStringConcat(const TS::IMethod& method) {
    return method.Name() == "Concat"
        && TS::IsKnownType(*method.DeclaringType(),
                           TS::KnownTypeCode::String);
}

// The C# `bool CheckArgumentsForStringConcat(Expression[] arguments)`
// (lines 277-330): the evaluation-order safety gates -- at least two
// arguments, no named arguments, every non-last argument's ToString
// effect-free, no nested string.Concat (the Roslyn/mcs flattening), no
// by-ref-like operand, and one of the first two operands a String (the +
// operator resolves to string concatenation only then).
bool CheckArgumentsForStringConcat(
    const std::vector<Syntax::Expression*>& arguments) {
    if (arguments.size() < 2)
        return false;
    for (Syntax::Expression* argument : arguments) {
        if (dynamic_cast<Syntax::NamedArgumentExpression*>(argument)
            != nullptr)
            return false;
    }
    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
        const Semantics::ResolveResult* rr =
            CSharp::GetResolveResult(*arguments[i]);
        if (!ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
                rr->Type()))
            return false;
    }
    for (Syntax::Expression* argument : arguments) {
        const Semantics::ResolveResult* rr =
            CSharp::GetResolveResult(*argument);
        if (const auto* irr =
                dynamic_cast<const Semantics::InvocationResolveResult*>(rr)) {
            if (const auto* member =
                    dynamic_cast<const TS::IMethod*>(irr->Member())) {
                if (IsStringConcat(*member))
                    return false;
            }
        }
        if (rr->Type().IsByRefLike())
            return false;
    }
    return TS::IsKnownType(
               CSharp::GetResolveResult(*arguments[0])->Type(),
               TS::KnownTypeCode::String)
        || TS::IsKnownType(
               CSharp::GetResolveResult(*arguments[1])->Type(),
               TS::KnownTypeCode::String);
}

// The C# `bool IsInstantiableTypeParameter(IType type)` (lines 272-275):
// a type parameter with the `new()` constraint.
bool IsInstantiableTypeParameter(const TS::IType& type) {
    const auto* typeParameter =
        dynamic_cast<const TS::ITypeParameter*>(&type);
    return typeParameter != nullptr
        && typeParameter->HasDefaultConstructorConstraint();
}

// The C# `invocationExpression.Ancestors.OfType<LambdaExpression>().Any(
// lambda => lambda.Annotation<IL.ILFunction>()?.Kind ==
// IL.ILFunctionKind.ExpressionTree)`: whether the call sits inside an
// expression-tree lambda (no ToString elimination there).
bool IsInExpressionTree(Syntax::AstNode& invocationExpression) {
    for (Syntax::AstNode* ancestor : invocationExpression.Ancestors()) {
        auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(ancestor);
        if (lambda == nullptr)
            continue;
        IL::ILFunction* function = CSharp::GetILFunctionAnnotation(*lambda);
        if (function != nullptr
            && function->Kind == IL::ILFunctionKind::ExpressionTree)
            return true;
    }
    return false;
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
// table, the unary table, the explicit conversion, op_True in a condition),
// the String.Concat reduction, the System.* special methods
// (GetTypeFromHandle, Activator.CreateInstance, GetSubArray), and the
// decimal increment reverse optimization.
// The VisitCastExpression methodof pattern (the
// getMethodOrConstructorFromHandlePattern over the LdTokenPattern /
// TypePattern classes) is DEAD CODE UPSTREAM and does not port: the
// LdTokenAnnotation those patterns read is only ever read, never attached
// anywhere in the C# (the GetFieldFromHandle arm's own comment documents
// this), so the pattern never matches -- like the GetFieldFromHandle arm,
// resolved as not-porting rather than deferred.
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

    // The C# String.Concat reduction (lines 66-105): `String.Concat(a, b)`
    // becomes `a + b` (the params-array overload flattens first). The
    // evaluation-order gates (CheckArgumentsForStringConcat) and the
    // ToString-elimination chain (RemoveRedundantToStringInConcat, the
    // static half) carry; an expression-tree lambda suppresses the
    // elimination (the tree must keep the explicit call).
    if (IsStringConcat(*method)
        && context_->DecompileRun->Settings().StringConcat()) {
        // The C# `arguments is [ArrayCreateExpression { Initializer: { }
        // aceInitializer }] && method.Parameters is [{ Type: ArrayType }]`.
        if (arguments.size() == 1) {
            auto* arrayCreate =
                dynamic_cast<Syntax::ArrayCreateExpression*>(arguments[0]);
            if (arrayCreate != nullptr
                && arrayCreate->Initializer() != nullptr) {
                const auto& parameters = method->Parameters();
                if (!parameters.empty() && parameters[0] != nullptr
                    && parameters[0]->Type().Kind()
                           == TS::TypeKind::Array) {
                    arguments.clear();
                    auto& elements = arrayCreate->Initializer()->Elements();
                    for (int i = 0; i < elements.Count(); ++i)
                        arguments.push_back(elements.At(i));
                }
            }
        }
        if (!CheckArgumentsForStringConcat(arguments))
            return;
        bool isInExpressionTree = IsInExpressionTree(*invocationExpression);
        context_->StepOnce("Replace String.Concat with +",
                           invocationExpression);
        Syntax::Expression* arg0 = Syntax::Detach(arguments[0]);
        Syntax::Expression* arg1 = Syntax::Detach(arguments[1]);
        if (!isInExpressionTree) {
            arg1 = Syntax::Detach(RemoveRedundantToStringInConcat(
                arg1, *method, /*isLastArgument*/ arguments.size() == 2));
            if (TS::IsKnownType(
                    CSharp::GetResolveResult(*arg1)->Type(),
                    TS::KnownTypeCode::String)) {
                arg0 = Syntax::Detach(RemoveRedundantToStringInConcat(
                    arg0, *method, /*isLastArgument*/ false));
            }
        }
        auto* expr = new Syntax::BinaryOperatorExpression(
            arg0, Syntax::BinaryOperatorType::Add, arg1);
        for (std::size_t i = 2; i < arguments.size(); ++i) {
            Syntax::Expression* argument = Syntax::Detach(arguments[i]);
            if (!isInExpressionTree) {
                argument = Syntax::Detach(RemoveRedundantToStringInConcat(
                    argument, *method,
                    /*isLastArgument*/ i + 1 == arguments.size()));
            }
            expr = new Syntax::BinaryOperatorExpression(
                expr, Syntax::BinaryOperatorType::Add, argument);
        }
        CopyAnnotationsFrom(expr, *invocationExpression);
        invocationExpression->ReplaceWith(expr);
        return;
    }

    // The C# `switch (method.FullName)` (lines 107-173): the System.*
    // special methods.
    const std::string fullName = method->FullName();
    if (fullName == "System.Type.GetTypeFromHandle") {
        // The C# GetFieldFromHandle arm is dead code upstream (the
        // LdTokenAnnotation is never added) and does not port.
        if (arguments.size() == 1) {
            if (Syntax::IsMatchPattern(TypeHandleOnTypeOfPattern(),
                                        arguments[0])) {
                context_->StepOnce("Replace GetTypeFromHandle with typeof",
                                   invocationExpression);
                auto* memberReference =
                    static_cast<Syntax::MemberReferenceExpression*>(
                        arguments[0]);
                Syntax::Expression* target = memberReference->Target();
                CopyInstructionsFrom(target, *invocationExpression);
                invocationExpression->ReplaceWith(target);
                return;
            }
        }
    } else if (fullName == "System.Activator.CreateInstance") {
        if (context_->DecompileRun->Settings()
                .UseObjectCreationOfGenericTypeParameter()
            && arguments.empty()) {
            const auto& typeArguments = method->TypeArguments();
            if (typeArguments.size() == 1
                && IsInstantiableTypeParameter(*typeArguments[0])) {
                context_->StepOnce(
                    "Replace Activator.CreateInstance with new",
                    invocationExpression);
                auto* objectCreate = new Syntax::ObjectCreateExpression(
                    context_->TypeSystemAstBuilder->ConvertType(
                        const_cast<TS::IType&>(*typeArguments[0])));
                invocationExpression->ReplaceWith(objectCreate);
                return;
            }
        }
    } else if (fullName
               == "System.Runtime.CompilerServices.RuntimeHelpers.GetSubArray") {
        if (arguments.size() == 2
            && context_->DecompileRun->Settings().Ranges()) {
            context_->StepOnce(
                "Replace RuntimeHelpers.GetSubArray with range indexer",
                invocationExpression);
            auto* slicing = new Syntax::IndexerExpression(
                Syntax::Detach(arguments[0]));
            slicing->Arguments().Add(Syntax::Detach(arguments[1]));
            CopyAnnotationsFrom(slicing, *invocationExpression);
            invocationExpression->ReplaceWith(slicing);
            return;
        }
    }

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
            // The C# decimal arm (lines 218-229): the legacy csc optimizes
            // `d + 1m` into `op_Increment(d)`, so reverse that here. On any
            // other declaring type `op_Increment(a)` is not equivalent to
            // `++a` (it does not assign) and the call stays.
            if (TS::IsKnownType(*method->DeclaringType(),
                                TS::KnownTypeCode::Decimal)) {
                context_->StepOnce(
                    "Replace decimal increment method with arithmetic",
                    invocationExpression);
                auto* arithmetic = new Syntax::BinaryOperatorExpression(
                    Syntax::Detach(
                        UnwrapInDirectionExpression(arguments[0])),
                    *uop == Syntax::UnaryOperatorType::Increment
                        ? Syntax::BinaryOperatorType::Add
                        : Syntax::BinaryOperatorType::Subtract,
                    new Syntax::PrimitiveExpression(
                        Syntax::DecimalValue::One(),
                        Syntax::LiteralFormat::None));
                CopyAnnotationsFrom(arithmetic, *invocationExpression);
                invocationExpression->ReplaceWith(arithmetic);
            }
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
