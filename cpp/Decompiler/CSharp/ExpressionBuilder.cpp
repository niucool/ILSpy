// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// ExpressionBuilder skeleton implementation -- see the header for the port notes.

#include "Decompiler/CSharp/ExpressionBuilder.hpp"

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/ILTypeExtensions.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/OpCodeName.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/SizeOfResolveResult.hpp"
#include "Decompiler/Semantics/ThrowResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeIsResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <unordered_set>

namespace ILSpy::Decompiler::CSharp {

// The REAL type-system namespace alias (the ExpressionBuilder TS:: convention -- the
// CSharp/TypeSystem sub-namespace shadows the plain TypeSystem:: lookup here).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

using namespace ILSpy::Decompiler::TypeSystem;
using IL::ILVariable;
using IL::ILVariablePtr;
using Syntax::AstType;
using Syntax::Expression;

namespace {

// The C#-small-integer cast arm of ConvertConstantValue: the
// `KnownTypeReference.GetCSharpNameByTypeCode(rr.Type.GetDefinition().KnownTypeCode)`
// keyword. The C# `!` non-null assert is the port's checked access -- the
// small-integer codes always resolve.
std::string CSharpNameByKnownTypeCode(KnownTypeCode code)
{
    auto name = KnownTypeReference::GetCSharpNameByTypeCode(code);
    assert(name.has_value());
    return std::string(name.value_or("int"));
}

// The C# `$"IL_{offset:x4}"` interpolation: lowercase hex, minimum four digits
// (the C# 'x4' format pads to four, more digits for larger values).
std::string ILOffsetHex(std::int32_t offset)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%x", static_cast<std::uint32_t>(offset));
    std::string result(buffer);
    while (result.size() < 4)
        result.insert(result.begin(), '0');
    return result;
}

// The C# `object.Equals(rr.ConstantValue, 0)` / `(0u)` pair (the
// AdjustConstantToType zero check): the value is a boxed int/uint zero.
bool IsZeroConstant(const std::any& value)
{
    if (!value.has_value())
        return false;
    if (value.type() == typeid(std::int32_t))
        return std::any_cast<std::int32_t>(value) == 0;
    if (value.type() == typeid(std::uint32_t))
        return std::any_cast<std::uint32_t>(value) == 0u;
    return false;
}

// The C# `expression.Annotation<ResolveResult>()` query over an Expression node.
const Sem::ResolveResult* ResolveResultAnnotation(const Expression& expr)
{
    return expr.Annotation<Sem::ResolveResult>();
}


// The owning shared_ptr of the node's resolve-result annotation (the port's
// ResolveResult is not enable_shared_from_this; the annotation channel owns the
// shared handle the C# GC reference aliases).
std::shared_ptr<Sem::ResolveResult> SharedResolveResultAnnotation(const Expression& expr)
{
    for (const auto& a : expr.SharedAnnotations())
    {
        if (dynamic_cast<Sem::ResolveResult*>(a.get()) != nullptr)
            return std::static_pointer_cast<Sem::ResolveResult>(a);
    }
    return nullptr;
}


// The shared handle of a node's resolve-result annotation (the port's ResolveResult
// is not enable_shared_from_this; the annotation channel owns the shared handle the
// C# GC reference aliases).
std::shared_ptr<Sem::ResolveResult> WithSharedResolveResult(const Expression& expr)
{
    for (const auto& a : expr.SharedAnnotations())
    {
        if (dynamic_cast<Sem::ResolveResult*>(a.get()) != nullptr)
            return std::static_pointer_cast<Sem::ResolveResult>(a);
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// ctor

ExpressionBuilder::ExpressionBuilder(const StatementBuilder* statementBuilderValue,
                                     const TS::ICompilation& typeSystem,
                                     const TS::ITypeResolveContext& decompilationContext,
                                     IL::ILFunction* currentFunctionValue,
                                     const DecompilerSettings* settingsValue,
                                     const DecompileRun* decompileRunValue)
{
    // The C# `Debug.Assert(decompilationContext != null)` -- the port's guard.
    if (decompileRunValue == nullptr)
        throw std::invalid_argument("decompileRun");
    if (settingsValue == nullptr)
        throw std::invalid_argument("settings");
    statementBuilder = statementBuilderValue;
    this->decompilationContext = &decompilationContext;
    currentFunction = currentFunctionValue;
    settings = settingsValue;
    decompileRun = decompileRunValue;
    compilation = &decompilationContext.Compilation();
    const auto context = std::make_shared<TypeSystem::CSharpTypeResolveContext>(
        compilation->MainModule(), decompileRun->UsingScope(),
        decompilationContext.CurrentTypeDefinition(),
        decompilationContext.CurrentMember());
    resolver = std::make_shared<const Resolver::CSharpResolver>(context);
    astBuilder.emplace(std::make_shared<const Resolver::CSharpResolver>(*resolver));
    astBuilder->AlwaysUseShortTypeNames() = true;
    astBuilder->AddResolveResultAnnotations() = true;
    astBuilder->ShowAttributes() = true;
    astBuilder->UseNullableSpecifierForValueTypes() = settings->LiftNullables();
    astBuilder->AlwaysUseGlobal() = settings->AlwaysUseGlobal();
    typeInference = TypeInferenceInstance{compilation,
                                          Resolver::TypeInferenceAlgorithm::Improved};
}

// ---------------------------------------------------------------------------
// ConvertType / ConvertConstantValue

AstType* ExpressionBuilder::ConvertType(TS::IType& type) const
{
    return astBuilder->ConvertType(type);
}

ExpressionWithResolveResult ExpressionBuilder::ConvertConstantValue(
    std::shared_ptr<Sem::ResolveResult> rr, bool allowImplicitConversion) const
{
    if (!rr)
        throw std::invalid_argument("rr");
    Expression* expr = astBuilder->ConvertConstantValue(rr);
    if (!allowImplicitConversion)
    {
        const TS::IType& type = rr->Type();
        if (auto* nullExpr = dynamic_cast<Syntax::NullReferenceExpression*>(expr);
            nullExpr != nullptr && type.Kind() != TypeKind::Null)
        {
            expr = new Syntax::CastExpression(ConvertType(const_cast<TS::IType&>(type)),
                                              nullExpr);
        }
        else if (IsCSharpSmallIntegerType(&type))
        {
            expr = new Syntax::CastExpression(
                new Syntax::PrimitiveType(CSharpNameByKnownTypeCode(
                    type.GetDefinition()->KnownTypeCode())),
                expr);
            // Note: no unchecked annotation necessary, because the constant was
            // folded to be in-range
        }
        else if (IsCSharpNativeIntegerType(&type))
        {
            expr = new Syntax::CastExpression(new Syntax::PrimitiveType(type.Name()), expr);
            // Note: no unchecked annotation necessary, because the rr wouldn't be
            // a constant if the value wasn't in-range on 32bit
        }
    }
    const Sem::ResolveResult* exprRR = ResolveResultAnnotation(*expr);
    if (exprRR == nullptr)
    {
        exprRR = rr.get();
        expr->AddAnnotation(rr);
    }
    return ExpressionWithResolveResult(expr, exprRR);
}

ExpressionWithResolveResult ExpressionBuilder::ConvertConstantValue(
    std::shared_ptr<Sem::ResolveResult> rr, bool allowImplicitConversion, bool displayAsHex) const
{
    astBuilder->PrintIntegralValuesAsHex() = displayAsHex;
    // The C# try/finally (restore the flag on every path).
    struct Restore
    {
        Syntax::TypeSystemAstBuilder& builder;
        ~Restore() { builder.PrintIntegralValuesAsHex() = false; }
    } restore{*astBuilder};
    return ConvertConstantValue(std::move(rr), allowImplicitConversion);
}

// ---------------------------------------------------------------------------
// Translate / TranslateCondition / the visitor dispatch

TranslatedExpression ExpressionBuilder::Translate(IL::ILInstruction* inst,
                                                 const TS::IType* typeHint)
{
    assert(inst != nullptr);
    // The UnknownType null object is a fresh handle per call; hold it through the
    // visit (the .get()-of-a-temporary dangling trap).
    TS::ITypePtr unknownHint;
    if (typeHint == nullptr)
        unknownHint = UnknownType();
    TranslationContext context;
    context.TypeHint = typeHint != nullptr ? typeHint : unknownHint.get();
    TranslatedExpression cexpr = Visit(inst, context);
#ifndef NDEBUG
    // The C# DEBUG post-condition validation (documented at the top of the C#
    // file): validate the translated type against the instruction's result type.
    const auto& instType = cexpr.Type();
    if (inst->ResultType() != IL::StackType::Void
        && instType.Kind() != TypeKind::Unknown && inst->ResultType() != IL::StackType::Unknown
        && instType.Kind() != TypeKind::None)
    {
        if (IsIntegerType(inst->ResultType()))
        {
            assert(IsIntegerType(GetStackType(instType))
                   && "IL instructions of integer type must convert into C# expressions of integer type");
            assert(GetSign(&instType) != Sign::None && "Must have a sign specified for zero/sign-extension");
        }
        else if (inst->ResultType() == IL::StackType::Ref)
        {
            assert(GetStackType(instType) == IL::StackType::Ref
                   || IsIntegerType(GetStackType(instType)));
        }
        else
        {
            assert(GetStackType(instType) == inst->ResultType());
        }
    }
#endif
    return cexpr;
}

TranslatedExpression ExpressionBuilder::TranslateCondition(IL::ILInstruction* condition, bool negate)
{
    assert(condition->ResultType() == IL::StackType::I4);
    TranslatedExpression expr = Translate(condition, &compilation->FindType(KnownTypeCode::Boolean));
    if (GetSize(GetStackType(expr.Type())) > 4)
    {
        expr = expr.ConvertTo(*FindType(IL::StackType::I4, GetSign(&expr.Type())), *this);
    }
    return expr.ConvertToBoolean(*this, negate);
}

TranslatedExpression ExpressionBuilder::Visit(IL::ILInstruction* inst, TranslationContext context)
{
    switch (inst->Op)
    {
        case IL::OpCode::LdLoc:
            return VisitLdLoc(inst, context);
        case IL::OpCode::LdLoca:
            return VisitLdLoca(inst, context);
        case IL::OpCode::LdNull:
            return VisitLdNull(inst, context);
        case IL::OpCode::DefaultValue:
            return VisitDefaultValue(inst, context);
        case IL::OpCode::LdStr:
            return VisitLdStr(inst, context);
        case IL::OpCode::LdcI4:
            return VisitLdcI4(inst, context);
        case IL::OpCode::LdcI8:
            return VisitLdcI8(inst, context);
        case IL::OpCode::LdcF4:
            return VisitLdcF4(inst, context);
        case IL::OpCode::LdcF8:
            return VisitLdcF8(inst, context);
        case IL::OpCode::LdcDecimal:
            return VisitLdcDecimal(inst, context);
        case IL::OpCode::BitNot:
            return VisitBitNot(inst, context);
        case IL::OpCode::Throw:
            return VisitThrow(inst, context);
        case IL::OpCode::ThreeValuedBoolAnd:
            return VisitThreeValuedBoolAnd(inst, context);
        case IL::OpCode::ThreeValuedBoolOr:
            return VisitThreeValuedBoolOr(inst, context);
        case IL::OpCode::IsInst:
            return VisitIsInst(inst, context);
        case IL::OpCode::StLoc:
            return VisitStLoc(inst, context);
        case IL::OpCode::SizeOf:
            return VisitSizeOf(inst, context);
        case IL::OpCode::LdTypeToken:
            return VisitLdTypeToken(inst, context);
        case IL::OpCode::NewArr:
            return VisitNewArr(inst, context);
        default:
            return Default(inst, context);
    }
}

TranslatedExpression ExpressionBuilder::Default(IL::ILInstruction* inst, TranslationContext)
{
    return ErrorExpression("OpCode not supported: " + std::string(IL::OpCodeName(inst->Op)));
}

// ---------------------------------------------------------------------------
// The ported Visit arms

TranslatedExpression ExpressionBuilder::VisitLdLoc(IL::ILInstruction* inst, TranslationContext)
{
    auto* ldloc = static_cast<IL::LdLoc*>(inst);
    const IL::ILVariablePtr& variable = ldloc->Variable;
    if (variable && variable->Kind == IL::VariableKind::StackSlot && variable->IsSingleDefinition())
    {
        loadedVariablesSet.insert(variable);
    }
    return WithILInstruction(ConvertVariable(variable), inst);
}

TranslatedExpression ExpressionBuilder::VisitLdLoca(IL::ILInstruction* inst, TranslationContext)
{
    auto* ldloca = static_cast<IL::LdLoca*>(inst);
    // Note that we put the instruction on the IdentifierExpression instead of the
    // DirectionExpression, because the DirectionExpression might get removed by
    // dereferencing instructions such as LdObj
    TranslatedExpression expr = WithILInstruction(ConvertVariable(ldloca->Variable), inst);
    return WithRR(
        WithoutILInstruction(
            *new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, expr.Expression())),
        std::make_shared<Sem::ByReferenceResolveResult>(
            SharedResolveResultAnnotation(*expr.Expression()), TS::ReferenceKind::Ref));
}

TranslatedExpression ExpressionBuilder::VisitLdNull(IL::ILInstruction* inst, TranslationContext)
{
    // The factory returns a fresh handle; keep it alive through the call (the
    // .get()-of-a-temporary dangling trap).
    const TS::ITypePtr nullType = NullType();
    return WithILInstruction(GetDefaultValueExpression(*nullType), inst);
}

TranslatedExpression ExpressionBuilder::VisitDefaultValue(IL::ILInstruction* inst, TranslationContext)
{
    auto* defaultValue = static_cast<IL::DefaultValue*>(inst);
    return WithILInstruction(GetDefaultValueExpression(*defaultValue->Type), inst);
}

TranslatedExpression ExpressionBuilder::VisitLdStr(IL::ILInstruction* inst, TranslationContext)
{
    auto* ldstr = static_cast<IL::LdStr*>(inst);
    return WithRR(WithILInstruction(
                      *new Syntax::PrimitiveExpression(ldstr->Value), inst),
                  std::make_shared<Sem::ConstantResolveResult>(
                      const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::String))
                          .shared_from_this(),
                      ldstr->Value));
}

TranslatedExpression ExpressionBuilder::VisitLdcI4(IL::ILInstruction* inst, TranslationContext context)
{
    auto* ldc = static_cast<IL::LdcI4*>(inst);
    std::shared_ptr<Sem::ResolveResult> rr;
    if (GetSign(context.TypeHint) == Sign::Unsigned)
    {
        rr = std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::UInt32))
                .shared_from_this(),
            static_cast<std::uint32_t>(ldc->Value));
    }
    else
    {
        rr = std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32))
                .shared_from_this(),
            ldc->Value);
    }
    rr = AdjustConstantToType(rr, const_cast<TS::IType&>(*context.TypeHint));
    return WithILInstruction(ConvertConstantValue(std::move(rr), true), inst);
}

TranslatedExpression ExpressionBuilder::VisitLdcI8(IL::ILInstruction* inst, TranslationContext context)
{
    auto* ldc = static_cast<IL::LdcI8*>(inst);
    std::shared_ptr<Sem::ResolveResult> rr;
    if (GetSign(context.TypeHint) == Sign::Unsigned)
    {
        rr = std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::UInt64))
                .shared_from_this(),
            static_cast<std::uint64_t>(ldc->Value));
    }
    else
    {
        rr = std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int64))
                .shared_from_this(),
            ldc->Value);
    }
    rr = AdjustConstantToType(rr, const_cast<TS::IType&>(*context.TypeHint));
    return WithILInstruction(ConvertConstantValue(std::move(rr), true), inst);
}

TranslatedExpression ExpressionBuilder::VisitLdcF4(IL::ILInstruction* inst, TranslationContext)
{
    auto* ldc = static_cast<IL::LdcF4*>(inst);
    auto* expr = astBuilder->ConvertConstantValue(
        const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Single)), ldc->Value);
    return TranslatedExpression(WithILInstruction(*expr, inst).Expression());
}

TranslatedExpression ExpressionBuilder::VisitLdcF8(IL::ILInstruction* inst, TranslationContext)
{
    auto* ldc = static_cast<IL::LdcF8*>(inst);
    auto* expr = astBuilder->ConvertConstantValue(
        const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Double)), ldc->Value);
    return TranslatedExpression(WithILInstruction(*expr, inst).Expression());
}

TranslatedExpression ExpressionBuilder::VisitLdcDecimal(IL::ILInstruction* inst, TranslationContext)
{
    auto* ldc = static_cast<IL::LdcDecimal*>(inst);
    auto* expr = astBuilder->ConvertConstantValue(
        const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Decimal)), ldc->Value);
    return TranslatedExpression(WithILInstruction(*expr, inst).Expression());
}

// The C# `protected internal override TranslatedExpression VisitBitNot(BitNot inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 746-777): translate the
// argument, and when the argument type is undersized for the operator (smaller
// than the underlying result stack type, a small-integer enum, a native integer
// over StackType.I, or a bool/char that does not support `~`), sign/zero-extend
// it to the arithmetic type first -- the extension sign comes from the type hint,
// falling back to the argument type's own sign.
TranslatedExpression ExpressionBuilder::VisitBitNot(IL::ILInstruction* inst, TranslationContext context)
{
    auto* bitNot = static_cast<IL::BitNot*>(inst);
    TranslatedExpression argument = Translate(bitNot->Argument.get());
    const TS::IType& argUType = TS::GetUnderlyingType(argument.Type());

    if (TS::GetSize(TS::GetStackType(argUType)) < TS::GetSize(bitNot->UnderlyingResultType)
        || (argUType.Kind() == TypeKind::Enum && TS::IsSmallIntegerType(&argUType))
        || (TS::GetStackType(argUType) == IL::StackType::I
            && !TS::IsCSharpNativeIntegerType(&argUType))
        || TS::IsKnownType(argUType, KnownTypeCode::Boolean)
        || TS::IsKnownType(argUType, KnownTypeCode::Char))
    {
        // Argument is undersized (even after implicit integral promotion to I4)
        // -> we need to perform sign/zero-extension before the BitNot.
        // Same if the argument is an enum based on a small integer type
        // (those don't undergo numeric promotion in C# the way non-enum small
        // integer types do).
        // Same if the type is one that does not support ~ (IntPtr, bool and char).
        Sign sign = TS::GetSign(context.TypeHint);
        if (sign == Sign::None)
        {
            sign = TS::GetSign(&argUType);
        }
        TS::ITypePtr targetType = FindArithmeticType(bitNot->UnderlyingResultType, sign);
        if (bitNot->IsLifted)
        {
            targetType = TS::Create(*compilation, *targetType);
        }
        argument = argument.ConvertTo(*targetType, *this);
    }

    auto* unary = new Syntax::UnaryOperatorExpression(argument.Expression(),
                                                      Syntax::UnaryOperatorType::BitNot);
    return WithILInstruction(
        WithRR(*unary,
               resolver->ResolveUnaryOperator(Syntax::UnaryOperatorType::BitNot,
                                              SharedResolveResultAnnotation(*argument.Expression()))),
        inst);
}

// The C# `protected internal override TranslatedExpression VisitThrow(Throw inst,
// TranslationContext context)` (line 1226): the throw-expression node over the
// translated operand, with the ThrowResolveResult annotation.
TranslatedExpression ExpressionBuilder::VisitThrow(IL::ILInstruction* inst, TranslationContext)
{
    auto* throwInst = static_cast<IL::Throw*>(inst);
    auto* throwExpr = new Syntax::ThrowExpression(Translate(throwInst->Argument.get()).Expression());
    return WithILInstruction(WithRR(*throwExpr, std::make_shared<Sem::ThrowResolveResult>()), inst);
}

// The C# `TranslatedExpression HandleThreeValuedLogic(BinaryInstruction inst,
// BinaryOperatorType op, ExpressionType eop)` (lines 1197-1224): the shared body of
// the two three-valued-logic arms. Both operands always evaluate (the C# `&`/`|` on
// `bool?` does not short-circuit): the nullable side converts to Nullable<bool>, the
// non-nullable side to plain bool, and the operator resolve result is the LIFTED
// bitwise operator over Nullable<bool>.
TranslatedExpression ExpressionBuilder::HandleThreeValuedLogic(IL::BinaryInstruction& inst,
                                                              Syntax::BinaryOperatorType op,
                                                              TS::ExpressionType eop)
{
    TranslatedExpression left = Translate(inst.Left.get());
    TranslatedExpression right = Translate(inst.Right.get());
    TS::ITypePtr boolType =
        const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Boolean)).shared_from_this();
    TS::ITypePtr nullableBoolType = TS::Create(*compilation, *boolType);
    if (TS::IsNullable(left.Type()))
    {
        left = left.ConvertTo(*nullableBoolType, *this);
        if (TS::IsNullable(right.Type()))
        {
            right = right.ConvertTo(*nullableBoolType, *this);
        }
        else
        {
            right = right.ConvertTo(*boolType, *this);
        }
    }
    else
    {
        left = left.ConvertTo(*boolType, *this);
        right = right.ConvertTo(*nullableBoolType, *this);
    }
    auto* binary = new Syntax::BinaryOperatorExpression(left.Expression(), op, right.Expression());
    return WithILInstruction(
        WithRR(*binary,
               std::make_shared<Sem::OperatorResolveResult>(
                   nullableBoolType, eop, static_cast<const TS::IMethod*>(nullptr), true,
                   std::vector<std::shared_ptr<Sem::ResolveResult>>{
                       SharedResolveResultAnnotation(*left.Expression()),
                       SharedResolveResultAnnotation(*right.Expression())})),
        &inst);
}

// The C# `protected internal override TranslatedExpression VisitThreeValuedBoolAnd(
// ThreeValuedBoolAnd inst, TranslationContext context)` (line 1188) / its Or sibling
// (1192): the three-valued `&` / `|` over bool?.
TranslatedExpression ExpressionBuilder::VisitThreeValuedBoolAnd(IL::ILInstruction* inst,
                                                               TranslationContext context)
{
    return HandleThreeValuedLogic(static_cast<IL::BinaryInstruction&>(*inst),
                                  Syntax::BinaryOperatorType::BitwiseAnd, TS::ExpressionType::And);
}

TranslatedExpression ExpressionBuilder::VisitThreeValuedBoolOr(IL::ILInstruction* inst,
                                                              TranslationContext context)
{
    return HandleThreeValuedLogic(static_cast<IL::BinaryInstruction&>(*inst),
                                  Syntax::BinaryOperatorType::BitwiseOr, TS::ExpressionType::Or);
}

// The C# `TranslatedExpression IsType(IsInst inst)` helper (ExpressionBuilder.cs lines
// 425-432): the `expr is T` expression the comp and unbox.any special cases build. The
// type renders through `TupleUnderlyingTypeOrSelf` (a tuple element's `is` test names the
// underlying ValueTuple type).
TranslatedExpression ExpressionBuilder::IsType(IL::IsInst& inst)
{
    TranslatedExpression arg = Translate(inst.Argument.get());
    arg = UnwrapBoxingConversion(arg);
    TS::ITypePtr tupleUnderlying = TS::TupleUnderlyingTypeOrSelf(*inst.Type);
    auto* isExpr = new Syntax::IsExpression(arg.Expression(), ConvertType(*tupleUnderlying));
    return WithRR(WithILInstruction(*isExpr, &inst),
                  std::make_shared<Sem::TypeIsResolveResult>(
                      SharedResolveResultAnnotation(*arg.Expression()), inst.Type,
                      const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Boolean)).shared_from_this()));
}

// The C# `protected internal override TranslatedExpression VisitIsInst(IsInst inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 434-483): the reference-type
// arm renders `expr as T` over the boxing-unwrapped input; the value-type/unconstrained
// generic arm renders the pure-argument fallback `expr is T ? expr : null` (an isinst
// over a value type boxes, which C# cannot express -- Roslyn pattern matching emits
// exactly this shape, so the doubling of the pure side effect is harmless) or the loud
// error expression.
TranslatedExpression ExpressionBuilder::VisitIsInst(IL::ILInstruction* inst, TranslationContext context)
{
    auto* isInst = static_cast<IL::IsInst*>(inst);
    TranslatedExpression arg = Translate(isInst->Argument.get());
    if (TS::IsReferenceType(isInst->Type.get()) != std::optional<bool>(true))
    {
        // isinst with a value type results in an expression of "boxed value type",
        // which is not supported in C#.
        // It's also not supported for unconstrained generic types.
        // Note that several other instructions special-case isinst arguments:
        //  unbox.any T(isinst T(expr)) ==> "expr as T" for nullable value types and class-constrained generic types
        //  comp(isinst T(expr) != null) ==> "expr is T"
        //  on block level (StatementBuilder.VisitIsInst) => "expr is T"
        if (IL::IsPure(isInst->Argument ? isInst->Argument->Flags() : IL::InstructionFlags::None))
        {
            // We can emulate isinst using
            //   expr is T ? expr : null
            // (doubling the boxing side-effect is harmless because the "expr is T" part won't observe object identity,
            //  and we need to support this because Roslyn pattern matching sometimes generates such code.)
            auto* isExpr = new Syntax::IsExpression(arg.Expression(), ConvertType(*isInst->Type));
            WithILInstruction(*isExpr, inst);
            auto* condExpr = new Syntax::ConditionalExpression(
                isExpr, arg.Expression()->Clone(), new Syntax::NullReferenceExpression());
            return WithRR(WithoutILInstruction(*condExpr),
                          std::make_shared<Sem::ResolveResult>(
                              const_cast<TS::IType&>(arg.Type()).shared_from_this()));
        }
        else
        {
            return ErrorExpression("isinst with value type is only supported in some contexts");
        }
    }
    arg = UnwrapBoxingConversion(arg);
    auto* asExpr = new Syntax::AsExpression(arg.Expression(), ConvertType(*isInst->Type));
    return WithRR(
        WithILInstruction(*asExpr, inst),
        std::make_shared<Sem::ConversionResolveResult>(
            isInst->Type, SharedResolveResultAnnotation(*arg.Expression()),
            Sem::Conversions::TryCast()));
}

// The C# `protected internal override TranslatedExpression VisitSizeOf(SizeOf inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 712-734): `sizeof T` over an
// unmanaged type, else the `System.Unsafe.SizeOf<T>()` intrinsic (a managed type's size
// is not a compile-time constant).
TranslatedExpression ExpressionBuilder::VisitSizeOf(IL::ILInstruction* inst, TranslationContext context)
{
    auto* sizeOf = static_cast<IL::SizeOf*>(inst);
    if (sizeOf->Type && TS::IsUnmanagedType(*sizeOf->Type, settings->IntroduceUnmanagedConstraint()))
    {
        auto* sizeOfExpr = new Syntax::SizeOfExpression(ConvertType(*sizeOf->Type));
        return WithRR(WithILInstruction(*sizeOfExpr, inst),
                      std::make_shared<Sem::SizeOfResolveResult>(
                          const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32)).shared_from_this(),
                          sizeOf->Type, std::nullopt));
    }
    else
    {
        std::optional<std::vector<TS::ITypePtr>> typeArguments;
        if (sizeOf->Type)
            typeArguments = std::vector<TS::ITypePtr>{sizeOf->Type};
        return CallUnsafeIntrinsic(
            "SizeOf", {},
            compilation->FindType(KnownTypeCode::Int32), inst,
            typeArguments);
    }
}

// The C# `protected internal override TranslatedExpression VisitLdTypeToken(
// LdTypeToken inst, TranslationContext context)` (ExpressionBuilder.cs lines 736-744):
// the `typeof(T).TypeHandle` render. The port's degenerate `arglist`-as-LdTypeToken
// decode keeps a null Type (the C# `Arglist` is its own node), so the null-type shape
// falls back to the error expression (the C# would NRE on the null `inst.Type`).
TranslatedExpression ExpressionBuilder::VisitLdTypeToken(IL::ILInstruction* inst, TranslationContext context)
{
    auto* token = static_cast<IL::LdTypeToken*>(inst);
    if (!token->Type)
        return ErrorExpression("ldtoken without a type operand");
    auto* typeofExpr = new Syntax::TypeOfExpression(ConvertType(*token->Type));
    WithRR(*typeofExpr,
           std::make_shared<Sem::TypeOfResolveResult>(
               const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Type)).shared_from_this(),
               token->Type));
    auto* memberRef = new Syntax::MemberReferenceExpression(typeofExpr, "TypeHandle");
    // The C# `compilation.FindType(new TopLevelTypeName("System", "RuntimeTypeHandle"))` --
    // the modules-scan extension over the full type name (the ICompilation member FindType
    // only resolves KnownTypeCodes, and RuntimeTypeHandle is not one).
    TS::ITypePtr runtimeTypeHandleType =
        TS::FindType(*compilation, TS::FullTypeName(TS::TopLevelTypeName("System", "RuntimeTypeHandle")));
    return WithRR(
        WithILInstruction(*memberRef, inst),
        std::make_shared<Sem::TypeOfResolveResult>(
            std::move(runtimeTypeHandleType),
            token->Type));
}


// The C# `protected internal override TranslatedExpression VisitNewArr(NewArr inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 502-516): the `new T[...]`
// array-creation render -- every index through TranslateArrayIndex, the ComposedType
// specifier move ("new (int[,])[10]" to "new int[10][,]"), the size arguments added
// to the AST node, and the ArrayCreateResolveResult over the reconstructed array type.
TranslatedExpression ExpressionBuilder::VisitNewArr(IL::ILInstruction* inst,
                                                    TranslationContext context)
{
    auto* newArr = static_cast<IL::NewArr*>(inst);
    int dimensions = static_cast<int>(newArr->Indices.size());
    std::vector<TranslatedExpression> args;
    for (const std::unique_ptr<IL::ILInstruction>& arg : newArr->Indices)
        args.push_back(TranslateArrayIndex(arg.get()));
    auto* expr = new Syntax::ArrayCreateExpression(ConvertType(*newArr->Type));
    if (auto* ct = dynamic_cast<Syntax::ComposedType*>(expr->Type()))
    {
        // change "new (int[,])[10]" to "new int[10][,]"
        ct->ArraySpecifiers().MoveTo(expr->AdditionalArraySpecifiers());
    }
    for (const TranslatedExpression& arg : args)
        expr->Arguments().Add(arg.Expression());
    // The C# `new ArrayType(compilation, inst.Type, dimensions)`: one dimension is
    // the SZArray shape, more are the multi-dimensional rank (the port's ArrayType
    // splits the two forms over its two ctors).
    TS::ITypePtr arrayType = dimensions == 1
                                 ? std::make_shared<TS::ArrayType>(newArr->Type)
                                 : std::make_shared<TS::ArrayType>(newArr->Type, dimensions);
    std::vector<std::shared_ptr<Sem::ResolveResult>> sizeArguments;
    for (const TranslatedExpression& arg : args)
        sizeArguments.push_back(SharedResolveResultAnnotation(*arg.Expression()));
    return WithRR(
        WithILInstruction(*expr, inst),
        std::make_shared<Sem::ArrayCreateResolveResult>(
            std::move(arrayType), std::move(sizeArguments),
            std::optional<std::vector<std::shared_ptr<Sem::ResolveResult>>>{
                std::vector<std::shared_ptr<Sem::ResolveResult>>{}}));
}

// The C# `TranslatedExpression TranslateArrayIndex(ILInstruction i)` (a private
// helper, ExpressionBuilder.cs line 3248): translate the index and convert it to its
// own stack type with allowIntPtr: false.
TranslatedExpression ExpressionBuilder::TranslateArrayIndex(IL::ILInstruction* i)
{
    TranslatedExpression input = Translate(i);
    return ConvertArrayIndex(std::move(input), i->ResultType(), false);
}

// The C# `TranslatedExpression ConvertArrayIndex(TranslatedExpression input,
// StackType stackType, bool allowIntPtr)` (a private helper, ExpressionBuilder.cs
// line 3253): the array-index conversion decision tree.
TranslatedExpression ExpressionBuilder::ConvertArrayIndex(TranslatedExpression input,
                                                          IL::StackType stackType,
                                                          bool allowIntPtr)
{
    if (TS::GetSize(&input.Type()) > TS::GetSize(stackType))
    {
        // truncate oversized result
        return input.ConvertTo(*FindType(stackType, TS::GetSign(&input.Type())), *this);
    }
    if (TS::IsCSharpPrimitiveIntegerType(&input.Type())
        || TS::IsCSharpNativeIntegerType(&input.Type()))
    {
        // can be used as array index as-is
        return input;
    }
    if (allowIntPtr
        && (TS::IsKnownType(input.Type(), KnownTypeCode::IntPtr)
            || TS::IsKnownType(input.Type(), KnownTypeCode::UIntPtr)))
    {
        return input;
    }
    if (stackType != IL::StackType::I4
        && TS::GetStackType(input.Type()) == IL::StackType::I4)
    {
        // prefer casting to int if that's big enough
        stackType = IL::StackType::I4;
    }
    TS::ITypePtr targetType = FindArithmeticType(stackType, TS::GetSign(&input.Type()));
    return input.ConvertTo(*targetType, *this);
}

// The C# `protected internal override TranslatedExpression VisitStLoc(StLoc inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 809-870): the assignment
// arm -- the stack-slot type refinement (the ILAst's stack slots carry inaccurate
// widened types), the by-ref re-assignment `ref (a = ref b)` shape, and the plain
// Assignment.
TranslatedExpression ExpressionBuilder::VisitStLoc(IL::ILInstruction* inst,
                                                   TranslationContext context)
{
    auto* stLoc = static_cast<IL::StLoc*>(inst);
    const IL::ILVariablePtr& variable = stLoc->Variable;
    TranslatedExpression translatedValue =
        Translate(stLoc->Value.get(), variable ? variable->Type.get() : nullptr);
    if (variable && variable->Kind == IL::VariableKind::StackSlot
        && loadedVariablesSet.count(variable) == 0)
    {
        // Stack slots in the ILAst have inaccurate types (e.g. System.Object for StackType.O)
        // so we should replace them with more accurate types where possible:
        if (CanUseTypeForStackSlot(*variable, translatedValue.Type())
            && variable->StackType() == TS::GetStackType(translatedValue.Type())
            && translatedValue.Type().Kind() != TypeKind::Null)
        {
            variable->Type = const_cast<TS::IType&>(translatedValue.Type()).shared_from_this();
        }
        else
        {
            TS::ITypePtr defaultValueType;
            if (MatchDefaultValue(stLoc->Value.get(), defaultValueType)
                && IsOtherValueType(*defaultValueType))
            {
                variable->Type = std::move(defaultValueType);
            }
        }
    }
    TranslatedExpression lhs = WithoutILInstruction(ConvertVariable(variable));
    auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(lhs.Expression());
    auto* lhsRefRR = dynamic_cast<Sem::ByReferenceResolveResult*>(
        const_cast<Sem::ResolveResult*>(lhs.ResolveResult()));
    if (dirExpr != nullptr && lhsRefRR != nullptr)
    {
        // ref (re-)assignment, emit "ref (a = ref b)".
        lhs = lhs.UnwrapChild(dirExpr->Expression());
        translatedValue = translatedValue.ConvertTo(const_cast<TS::IType&>(lhsRefRR->Type()), *this,
                                                    /*checkForOverflow=*/false,
                                                    /*allowImplicitConversion=*/true);
        auto* assign = new Syntax::AssignmentExpression(lhs.Expression(),
                                                       translatedValue.Expression());
        WithRR(*assign,
               std::make_shared<Sem::OperatorResolveResult>(
                   const_cast<TS::IType&>(lhs.Type()).shared_from_this(),
                   TS::ExpressionType::Assign,
                   std::vector<std::shared_ptr<Sem::ResolveResult>>{
                       SharedResolveResultAnnotation(*dirExpr),
                       SharedResolveResultAnnotation(*translatedValue.Expression())}));
        auto* outer = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, assign);
        // The C# `.WithRR(lhsRefRR)` re-attaches the SAME ByReferenceResolveResult
        // object to the outer DirectionExpression.
        return WithRR(WithoutILInstruction(*outer),
                      SharedResolveResultAnnotation(*dirExpr));
    }
    else
    {
        return WithILInstruction(Assignment(lhs, translatedValue), inst);
    }
}

// The C# `bool CanUseTypeForStackSlot(ILVariable v, IType type)` local function
// of VisitStLoc.
bool ExpressionBuilder::CanUseTypeForStackSlot(const IL::ILVariable& variable,
                                               const TS::IType& type)
{
    return variable.IsSingleDefinition()
           || IsOtherValueType(type) || variable.StackType() == IL::StackType::Ref
           || AllStoresUseConsistentType(StoreInstructionsOf(variable), type);
}

// The C# `bool IsOtherValueType(IType type)` local function of VisitStLoc:
// a value type the eval stack carries as O.
bool ExpressionBuilder::IsOtherValueType(const TS::IType& type)
{
    return TS::IsReferenceType(&type) == std::optional<bool>(false)
           && TS::GetStackType(type) == IL::StackType::O;
}

// The C# `bool AllStoresUseConsistentType(IReadOnlyList<IStoreInstruction>
// storeInstructions, IType expectedType)` local function of VisitStLoc.
bool ExpressionBuilder::AllStoresUseConsistentType(
    const std::vector<IL::ILInstruction*>& storeInstructions,
    const TS::IType& expectedType)
{
    TS::ITypePtr expected =
        const_cast<TS::IType&>(expectedType).AcceptVisitor(TS::NormalizeTypeVisitor::TypeErasure());
    for (IL::ILInstruction* store : storeInstructions)
    {
        if (store == nullptr || store->Op != IL::OpCode::StLoc)
            return false;
        auto* stloc = static_cast<IL::StLoc*>(store);
        if (stloc->Value == nullptr)
            return false;
        TS::ITypePtr type = IL::InferType(*stloc->Value, compilation);
        type = type->AcceptVisitor(TS::NormalizeTypeVisitor::TypeErasure());
        if (!type->Equals(*expected))
            return false;
    }
    return true;
}

// The port stand-in for the C# `ILVariable.StoreInstructions` list (see the header
// note): one recursive scan of the current function's live body grouping every
// IStoreInstruction-shaped node by its Variable.
void ExpressionBuilderCollectStores(IL::ILInstruction* inst,
                                    std::unordered_map<const IL::ILVariable*,
                                                       std::vector<IL::ILInstruction*>>& out)
{
    if (inst == nullptr)
        return;
    const IL::ILVariable* variable = nullptr;
    switch (inst->Op)
    {
        case IL::OpCode::StLoc:
            variable = static_cast<IL::StLoc*>(inst)->Variable.get();
            break;
        case IL::OpCode::MatchInstruction:
            variable = static_cast<IL::MatchInstruction*>(inst)->Variable.get();
            break;
        case IL::OpCode::UsingInstruction:
            variable = static_cast<IL::UsingInstruction*>(inst)->Variable.get();
            break;
        case IL::OpCode::TryCatchHandler:
            variable = static_cast<IL::TryCatchHandler*>(inst)->Variable.get();
            break;
        case IL::OpCode::PinnedRegion:
            variable = static_cast<IL::PinnedRegion*>(inst)->Variable.get();
            break;
        default:
            break;
    }
    if (variable != nullptr)
        out[variable].push_back(inst);
    for (int i = 0; i < inst->ChildCount(); ++i)
        ExpressionBuilderCollectStores(inst->GetChild(i), out);
}

const std::vector<IL::ILInstruction*>& ExpressionBuilder::StoreInstructionsOf(
    const IL::ILVariable& variable)
{
    if (storeScanFunction != currentFunction)
    {
        storeInstructions.clear();
        storeScanFunction = currentFunction;
        // The scan starts at the FUNCTION node (not the Body): ILFunction's
        // children are the Body plus the LocalFunctions collection, so stores
        // inside nested local functions are gathered too (the C# list covers
        // every connected store instruction).
        if (currentFunction != nullptr)
            ExpressionBuilderCollectStores(currentFunction, storeInstructions);
    }
    static const std::vector<IL::ILInstruction*> empty;
    auto it = storeInstructions.find(&variable);
    return it != storeInstructions.end() ? it->second : empty;
}

// ---------------------------------------------------------------------------
// The value helpers

ExpressionWithResolveResult ExpressionBuilder::ConvertVariable(const IL::ILVariablePtr& variable)
{
    Expression* expr;
    if (variable && variable->Kind == IL::VariableKind::Parameter && variable->Index < 0)
        expr = new Syntax::ThisReferenceExpression();
    else
        expr = new Syntax::IdentifierExpression(variable ? variable->Name : std::string());
    const TS::IType* type = variable ? variable->Type.get() : nullptr;
    if (type && type->Kind() == TypeKind::ByReference)
    {
        // When loading a by-ref parameter, use 'ref paramName'.
        // We'll strip away the 'ref' when dereferencing.

        // Ensure that the IdentifierExpression itself also gets a resolve result,
        // as that might get used after the 'ref' is stripped away:
        const auto& brType = static_cast<const ByReferenceType&>(*type);
        const ITypePtr& elementType = brType.Element();
        auto elementRR = std::make_shared<ILVariableResolveResult>(variable, elementType);
        WithRR(*expr, elementRR);

        expr = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, expr);
        return WithRR(*expr, std::make_shared<Sem::ByReferenceResolveResult>(
                                 elementRR, TS::ReferenceKind::Ref));
    }
    else
    {
        return WithRR(*expr, std::make_shared<ILVariableResolveResult>(variable,
                                                                            variable->Type));
    }
}

// The C# `ExpressionWithResolveResult Assignment(TranslatedExpression left,
// TranslatedExpression right)` (ExpressionBuilder.cs line 1255): convert the value
// to the assignment target type (implicit conversions allowed) and build the
// AssignmentExpression over the Assign OperatorResolveResult.
ExpressionWithResolveResult ExpressionBuilder::Assignment(TranslatedExpression left,
                                                         TranslatedExpression right)
{
    right = right.ConvertTo(const_cast<TS::IType&>(left.Type()), *this,
                            /*checkForOverflow=*/false,
                            /*allowImplicitConversion=*/true);
    auto* assign = new Syntax::AssignmentExpression(left.Expression(), right.Expression());
    return WithRR(*assign,
                  std::make_shared<Sem::OperatorResolveResult>(
                      const_cast<TS::IType&>(left.Type()).shared_from_this(),
                      TS::ExpressionType::Assign,
                      std::vector<std::shared_ptr<Sem::ResolveResult>>{
                          SharedResolveResultAnnotation(*left.Expression()),
                          SharedResolveResultAnnotation(*right.Expression())}));
}

bool ExpressionBuilder::HidesVariableWithName(const std::string& name) const
{
    return currentFunction != nullptr && HidesVariableWithName(*currentFunction, name);
}

bool ExpressionBuilder::HidesVariableWithName(const IL::ILFunction& currentFunctionValue,
                                              const std::string& name)
{
    // The C# `currentFunction.Ancestors.OfType<ILFunction>()` INCLUDES the
    // function itself (Ancestors yields this first).
    for (const IL::ILInstruction* node = &currentFunctionValue; node != nullptr;
         node = node->Parent)
    {
        const auto* function = dynamic_cast<const IL::ILFunction*>(node);
        if (function == nullptr)
            continue;
        for (const auto& v : function->Variables)
        {
            if (v && v->Name == name)
                return true;
        }
        for (const auto& f : function->LocalFunctions)
        {
            if (f && f->Name == name)
                return true;
        }
    }
    return false;
}

ExpressionWithResolveResult ExpressionBuilder::LogicNot(const TranslatedExpression& exprIn) const
{
    TranslatedExpression expr = exprIn;
    // "!expr" implicitly converts to bool so we can remove the cast;
    // but only if doing so wouldn't cause us to call a user-defined "operator !"
    expr = expr.UnwrapImplicitBoolConversion(
        [](const TS::IType& type) {
            for (const IMethod* m : type.GetMethods([](const IMethod* method) {
                     return method->IsOperator() && method->Name() == "op_LogicalNot";
                 }))
            {
                (void)m;
                return true;
            }
            return false;
        });
    Expression* notExpr =
        new Syntax::UnaryOperatorExpression(expr.Expression(), Syntax::UnaryOperatorType::Not);
    return WithRR(*notExpr, std::make_shared<Sem::OperatorResolveResult>(
                                 const_cast<TS::IType&>(
                                     compilation->FindType(KnownTypeCode::Boolean))
                                     .shared_from_this(),
                                 TS::ExpressionType::Not,
                                 std::vector<std::shared_ptr<Sem::ResolveResult>>{
                                     WithSharedResolveResult(*expr.Expression())}));
}

ExpressionWithResolveResult ExpressionBuilder::GetDefaultValueExpression(TS::IType& type) const
{
    Expression* expr;
    TS::ITypePtr constantTypeHandle;  // the fresh-singleton arms (the null literal)
    const TS::IType* constantType;
    std::any constantValue;
    if (IsReferenceType(&type) == true)
    {
        expr = new Syntax::NullReferenceExpression();
        constantTypeHandle = NullType();
        constantType = constantTypeHandle.get();
        constantValue = {};  // null
        return WithRR(*expr, std::make_shared<Sem::ConstantResolveResult>(
                                 const_cast<TS::IType*>(constantType)->shared_from_this(),
                                 constantValue));
    }
    else if (IsKnownType(type, KnownTypeCode::NullableOfT))
    {
        expr = new Syntax::NullReferenceExpression();
        constantTypeHandle = NullType();
        constantType = constantTypeHandle.get();
        constantValue = {};
        auto crr = std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType*>(constantType)->shared_from_this(), constantValue);
        return WithRR(
            *new Syntax::CastExpression(ConvertType(type), expr),
            std::make_shared<Sem::ConversionResolveResult>(
                type.shared_from_this(), crr, Sem::Conversions::NullLiteralConversion()));
    }
    else if (IsKnownType(type, KnownTypeCode::Decimal))
    {
        expr = new Syntax::PrimitiveExpression(IL::DecimalValue{});
        constantType = &type;
        constantValue = IL::DecimalValue{};
        return WithRR(*expr, std::make_shared<Sem::ConstantResolveResult>(
                                 const_cast<TS::IType*>(constantType)->shared_from_this(),
                                 constantValue));
    }
    else
    {
        expr = new Syntax::DefaultValueExpression(ConvertType(type));
        constantType = &type;
        constantValue = Resolver::CSharpResolver::GetDefaultValue(type);
        return WithRR(*expr, std::make_shared<Sem::ConstantResolveResult>(
                                 const_cast<TS::IType*>(constantType)->shared_from_this(),
                                 constantValue));
    }
}

bool ExpressionBuilder::ShouldDisplayAsHex(long long value, const TS::IType& type) const
{
    if (value >= 0 && value <= 9)
        return false;
    if (value < 0 && GetSign(&type) == Sign::Signed)
        return false;
    return true;
}

std::shared_ptr<Sem::ResolveResult> ExpressionBuilder::AdjustConstantToType(
    std::shared_ptr<Sem::ResolveResult> rr, TS::IType& typeHint) const
{
    if (!rr->IsCompileTimeConstant())
        return rr;
    TS::IType& hint = *const_cast<TS::IType*>(GetEnumUnderlyingType(&typeHint));
    if (rr->Type().Equals(hint))
        return rr;
    // Convert to type hint, if this is possible without loss of accuracy
    if (IsKnownType(hint, KnownTypeCode::Boolean))
    {
        auto equalsValue = [&](int v) {
            return rr->ConstantValue().has_value()
                   && (rr->ConstantValue().type() == typeid(std::int32_t)
                           ? std::any_cast<std::int32_t>(rr->ConstantValue()) == v
                           : rr->ConstantValue().type() == typeid(std::uint32_t)
                                 ? static_cast<int>(std::any_cast<std::uint32_t>(rr->ConstantValue()))
                                       == v
                                 : false);
        };
        if (equalsValue(0))
        {
            rr = std::make_shared<Sem::ConstantResolveResult>(hint.shared_from_this(), false);
        }
        else if (equalsValue(1))
        {
            rr = std::make_shared<Sem::ConstantResolveResult>(hint.shared_from_this(), true);
        }
    }
    else if (hint.Kind() == TypeKind::Enum || IsKnownType(hint, KnownTypeCode::Char)
             || IsCSharpSmallIntegerType(&hint))
    {
        auto castRR = resolver->WithCheckForOverflow(true)->ResolveCast(
            hint, rr);
        if (castRR->IsCompileTimeConstant() && !castRR->IsError())
        {
            rr = castRR;
        }
    }
    else if (IsAnyPointer(hint.Kind()) && IsZeroConstant(rr->ConstantValue()))
    {
        rr = std::make_shared<Sem::ConstantResolveResult>(hint.shared_from_this(),
                                                          std::any{});
    }
    return rr;
}

// ---------------------------------------------------------------------------
// The arithmetic-type helpers

TS::ITypePtr ExpressionBuilder::FindType(IL::StackType stackType, Sign sign) const
{
    if (stackType == IL::StackType::I && settings->NativeIntegers())
    {
        return sign == Sign::Unsigned ? NUInt() : NInt();
    }
    else
    {
        return const_cast<TS::IType&>(TS::FindType(*compilation, stackType, sign)).shared_from_this();
    }
}

TS::ITypePtr ExpressionBuilder::FindArithmeticType(IL::StackType stackType,
                                                           Sign sign) const
{
    if (stackType == IL::StackType::I)
    {
        if (settings->NativeIntegers())
        {
            return sign == Sign::Unsigned ? NUInt() : NInt();
        }
        else
        {
            // If native integers are not available, use 64-bit arithmetic instead
            stackType = IL::StackType::I8;
        }
    }
    return FindType(stackType, sign);
}

TranslatedExpression ExpressionBuilder::PrepareArithmeticArgument(TranslatedExpression arg,
                                                                 IL::StackType argStackType,
                                                                 Sign sign, bool isLifted) const
{
    if (isLifted && !IsNullable(arg.Type()))
    {
        isLifted = false; // don't cast to nullable if this input wasn't already nullable
    }
    const TS::IType* argUType =
        isLifted ? GetEnumUnderlyingType(&arg.Type()) : &arg.Type();
    if (IsIntegerType(argStackType) && GetSize(argStackType) < GetSize(argUType))
    {
        // If the argument is oversized (needs truncation to match stack size of its
        // ILInstruction), perform the truncation now.
        TS::ITypePtr targetType = FindType(argStackType, sign);
        argUType = targetType.get();
        if (isLifted)
            targetType = TS::Create(*compilation, *targetType);
        arg = arg.ConvertTo(*targetType, *this);
    }
    if (IsKnownType(*argUType, KnownTypeCode::IntPtr)
        || IsKnownType(*argUType, KnownTypeCode::UIntPtr))
    {
        // None of the operators we might want to apply are supported by
        // IntPtr/UIntPtr. Also, pointer arithmetic has different semantics (works
        // in number of elements, not bytes). So any inputs of size StackType.I
        // must be converted to long/ulong.
        TS::ITypePtr targetType = FindArithmeticType(IL::StackType::I, sign);
        if (isLifted)
            targetType = TS::Create(*compilation, *targetType);
        arg = arg.ConvertTo(*targetType, *this);
    }
    return arg;
}

// ---------------------------------------------------------------------------
// The self-contained statics

std::optional<Syntax::AssignmentOperatorType>
ExpressionBuilder::GetAssignmentOperatorTypeFromMetadataName(const std::string& name,
                                                             const DecompilerSettings& settings)
{
    if (name == "op_Addition") return Syntax::AssignmentOperatorType::Add;
    if (name == "op_Subtraction") return Syntax::AssignmentOperatorType::Subtract;
    if (name == "op_Multiply") return Syntax::AssignmentOperatorType::Multiply;
    if (name == "op_Division") return Syntax::AssignmentOperatorType::Divide;
    if (name == "op_Modulus") return Syntax::AssignmentOperatorType::Modulus;
    if (name == "op_BitwiseAnd") return Syntax::AssignmentOperatorType::BitwiseAnd;
    if (name == "op_BitwiseOr") return Syntax::AssignmentOperatorType::BitwiseOr;
    if (name == "op_ExclusiveOr") return Syntax::AssignmentOperatorType::ExclusiveOr;
    if (name == "op_LeftShift") return Syntax::AssignmentOperatorType::ShiftLeft;
    if (name == "op_RightShift") return Syntax::AssignmentOperatorType::ShiftRight;
    if (name == "op_UnsignedRightShift" && settings.UnsignedRightShift())
        return Syntax::AssignmentOperatorType::UnsignedShiftRight;
    return std::nullopt;
}

std::optional<Syntax::UnaryOperatorType>
ExpressionBuilder::GetUnaryOperatorTypeFromMetadataName(const std::string& name, bool isPostfix)
{
    if (name == "op_Increment" || name == "op_CheckedIncrement")
        return isPostfix ? Syntax::UnaryOperatorType::PostIncrement
                         : Syntax::UnaryOperatorType::Increment;
    if (name == "op_Decrement" || name == "op_CheckedDecrement")
        return isPostfix ? Syntax::UnaryOperatorType::PostDecrement
                         : Syntax::UnaryOperatorType::Decrement;
    return std::nullopt;
}

bool ExpressionBuilder::IsCompatibleWithSign(const TS::IType& type, Sign sign)
{
    return sign == Sign::None || GetSign(GetEnumUnderlyingType(&type)) == sign;
}

bool ExpressionBuilder::BinaryOperatorMightCheckForOverflow(Syntax::BinaryOperatorType op)
{
    switch (op)
    {
        case Syntax::BinaryOperatorType::BitwiseAnd:
        case Syntax::BinaryOperatorType::BitwiseOr:
        case Syntax::BinaryOperatorType::ExclusiveOr:
        case Syntax::BinaryOperatorType::ShiftLeft:
        case Syntax::BinaryOperatorType::ShiftRight:
        case Syntax::BinaryOperatorType::UnsignedShiftRight:
            return false;
        default:
            return true;
    }
}

bool ExpressionBuilder::AssignmentOperatorMightCheckForOverflow(Syntax::AssignmentOperatorType op)
{
    switch (op)
    {
        case Syntax::AssignmentOperatorType::BitwiseAnd:
        case Syntax::AssignmentOperatorType::BitwiseOr:
        case Syntax::AssignmentOperatorType::ExclusiveOr:
        case Syntax::AssignmentOperatorType::ShiftLeft:
        case Syntax::AssignmentOperatorType::ShiftRight:
            return false;
        default:
            return true;
    }
}

bool ExpressionBuilder::IsUnboxAnyWithIsInst(const IL::UnboxAny& unboxAny,
                                             const TS::IType& isInstType)
{
    return unboxAny.Type->Equals(isInstType)
           && (IsKnownType(*unboxAny.Type, KnownTypeCode::NullableOfT)
               || IsReferenceType(&isInstType) == true);
}

TranslatedExpression ExpressionBuilder::UnwrapBoxingConversion(TranslatedExpression arg)
{
    if (auto* cast = dynamic_cast<Syntax::CastExpression*>(arg.Expression());
        cast != nullptr && IsKnownType(arg.Type(), KnownTypeCode::Object))
    {
        if (const auto* crr =
                dynamic_cast<const Sem::ConversionResolveResult*>(arg.ResolveResult());
            crr != nullptr && crr->ConversionProperty()->IsBoxingConversion())
        {
            // When 'is' or 'as' is used with a value type or type parameter,
            // the C# compiler implicitly boxes the input.
            arg = arg.UnwrapChild(cast->Expression());
        }
    }
    return arg;
}

TranslatedExpression ExpressionBuilder::ChangeDirectionExpressionTo(TranslatedExpression input,
                                                                   ReferenceKind kind,
                                                                   bool isAddressOf)
{
    auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(input.Expression());
    const auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(input.ResolveResult());
    if (dirExpr == nullptr || brrr == nullptr)
        return input;
    if ((isAddressOf || dynamic_cast<Syntax::ThisReferenceExpression*>(dirExpr->Expression()) != nullptr)
        && (kind == TS::ReferenceKind::In || kind == TS::ReferenceKind::RefReadOnly))
    {
        return input.UnwrapChild(dirExpr->Expression());
    }
    switch (kind)
    {
        case TS::ReferenceKind::Ref:
            dirExpr->FieldDirection(Syntax::FieldDirection::Ref);
            break;
        case TS::ReferenceKind::Out:
            dirExpr->FieldDirection(Syntax::FieldDirection::Out);
            break;
        case TS::ReferenceKind::In:
            dirExpr->FieldDirection(Syntax::FieldDirection::In);
            break;
        case TS::ReferenceKind::RefReadOnly:
            dirExpr->FieldDirection(Syntax::FieldDirection::In);
            break;
        default:
            throw std::runtime_error("Unsupported reference kind: " + std::to_string(static_cast<int>(kind)));
    }
    dirExpr->RemoveAnnotations<Sem::ByReferenceResolveResult>();
    std::shared_ptr<Sem::ResolveResult> newBrrr;
    if (brrr->ElementResult() == nullptr)
        newBrrr = std::make_shared<Sem::ByReferenceResolveResult>(
            const_cast<TS::IType&>(brrr->ElementType()).shared_from_this(), kind);
    else
        newBrrr = std::make_shared<Sem::ByReferenceResolveResult>(brrr->ElementResultShared(),
                                                                  kind);
    dirExpr->AddAnnotation(newBrrr);
    return TranslatedExpression(dirExpr);
}

TranslatedExpression ExpressionBuilder::ErrorExpression(const std::string& message)
{
    auto* e = new Syntax::ErrorExpression();
    e->AddTrailingTrivia(new Syntax::Comment(message, Syntax::CommentType::MultiLine));
    std::shared_ptr<Sem::ResolveResult> errorRR(
        const_cast<Sem::ErrorResolveResult*>(&Sem::ErrorResolveResult::UnknownError()),
        [](Sem::ResolveResult*) noexcept {});
    return WithRR(WithoutILInstruction(*e), std::move(errorRR));
}

TranslatedExpression ExpressionBuilder::CallUnsafeIntrinsic(
    const std::string& name, std::vector<Expression*> arguments, const TS::IType& returnType,
    IL::ILInstruction* inst, std::optional<std::vector<TS::ITypePtr>> typeArguments) const
{
    auto* target = new Syntax::MemberReferenceExpression();
    target->Target(new Syntax::TypeReferenceExpression(
        astBuilder->ConvertType(const_cast<TS::IType&>(
            compilation->FindType(KnownTypeCode::Unsafe)))));
    target->MemberName(name);
    if (typeArguments.has_value())
    {
        for (const auto& typeArgument : typeArguments.value())
            target->TypeArguments().Add(astBuilder->ConvertType(const_cast<TS::IType&>(*typeArgument)));
    }
    auto* invocationExpr = new Syntax::InvocationExpression();
    invocationExpr->Target(target);
    for (Expression* argument : arguments)
        invocationExpr->Arguments().Add(argument);
    Expression* invocation = invocationExpr;
    if (inst != nullptr)
        WithILInstruction(*invocation, inst);
    if (returnType.Kind() == TypeKind::ByReference)
    {
        return WrapInRef(*invocation, *static_cast<const TS::ByReferenceType&>(returnType).Element());
    }
    else
    {
        return WithRR(WithoutILInstruction(*invocation),
                      std::make_shared<Sem::ResolveResult>(
                          const_cast<TS::IType&>(returnType).shared_from_this()));
    }
}

TranslatedExpression ExpressionBuilder::WrapInRef(Expression& expression, const TS::IType& type)
{
    auto* direction = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, &expression);
    return WithRR(WithoutILInstruction(*direction),
                  std::make_shared<Sem::ByReferenceResolveResult>(
                      const_cast<TS::IType&>(type).shared_from_this(), TS::ReferenceKind::Ref));
}

TranslatedExpression ExpressionBuilder::LdcI4(const TS::ICompilation& compilationValue,
                                              std::int32_t val)
{
    return WithRR(
        WithoutILInstruction(*new Syntax::PrimitiveExpression(val)),
        std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(const_cast<TS::ICompilation&>(compilationValue)
                                       .FindType(KnownTypeCode::Int32))
                .shared_from_this(),
            val));
}

} // namespace ILSpy::Decompiler::CSharp
