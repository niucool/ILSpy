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

#include "Decompiler/CSharp/CallBuilder.hpp"
#include "Decompiler/CSharp/StatementBuilder.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SwitchExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
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
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/PointerArithmeticOffset.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Unbox.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/RefAnyType.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/OpCodeName.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/SizeOfResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/ThrowResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeIsResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

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

// The C# `inst.UnwrapConv(kind)` (ILInstruction, Instructions.cs): recursively
// descend Conv chains of the requested ConversionKind. Mirrors the file-local
// UnwrapConv copies in the Transforms (the "copied next to its consumer"
// convention) -- this copy backs TranslateLocAlloc's sizeof matching.
const IL::ILInstruction* UnwrapConv(const IL::ILInstruction* inst,
                                    IL::ConversionKind kind)
{
    while (inst && inst->Op == IL::OpCode::Conv)
    {
        auto* conv = static_cast<const IL::Conv*>(inst);
        if (conv->Kind != kind)
            break;
        inst = conv->Argument.get();
    }
    return inst;
}


// The C# `ILInstruction.MatchLdcI(long)` (PatternMatching.cs line 73): the out-
// parameter form over LdcI8/LdcI4 with the conv unwrapping (a sign-extend conv
// recurses; a zero-extend from I4 recurses and clears the top 32 bits -- the
// reader never wraps constants in conv, so the plain forms cover every real
// shape; the conv arms follow the PointerArithmeticOffset local-copy precedent).
bool MatchLdcI(const IL::ILInstruction* inst, std::int64_t& val)
{
    if (inst == nullptr)
        return false;
    if (inst->Op == IL::OpCode::LdcI8)
    {
        val = static_cast<const IL::LdcI8*>(inst)->Value;
        return true;
    }
    if (inst->Op == IL::OpCode::LdcI4)
    {
        val = static_cast<const IL::LdcI4*>(inst)->Value;
        return true;
    }
    if (inst->Op == IL::OpCode::Conv)
    {
        auto* conv = static_cast<const IL::Conv*>(inst);
        if (conv->Kind == IL::ConversionKind::SignExtend)
            return MatchLdcI(conv->Argument.get(), val);
        if (conv->Kind == IL::ConversionKind::ZeroExtend && conv->InputType == IL::StackType::I4
            && MatchLdcI(conv->Argument.get(), val))
        {
            val &= 0xFFFFFFFFll; // clear top 32 bits
            return true;
        }
    }
    return false;
}

// The C# `ILInstruction.MatchLdcI(long val)` value form (PatternMatching.cs line
// 73): `MatchLdcI(out v) && v == val` -- spelled as a distinct helper because C++
// cannot overload (T&) against (T) for an lvalue argument (the C# overloads are
// distinguishable only by the parameter mode).
bool IsZeroLdc(const IL::ILInstruction* inst)
{
    std::int64_t val = 0;
    return MatchLdcI(inst, val) && val == 0;
}

// The C# `SyntaxExtensions.IsBitwise(BinaryOperatorType)` (SyntaxExtensions.cs line
// 49): bitwise and, bitwise or, or exclusive or. Ported as a file-local helper
// beside its consumer (the SyntaxExtensions extension-method convention -- the
// InsertParenthesesVisitor carries the same-shape file-local copy).
bool IsBitwise(Syntax::BinaryOperatorType op)
{
    return op == Syntax::BinaryOperatorType::BitwiseAnd
           || op == Syntax::BinaryOperatorType::BitwiseOr
           || op == Syntax::BinaryOperatorType::ExclusiveOr;
}

// The C# `ILInstruction.MatchBinaryNumericInstruction(BinaryNumericOperator op,
// out ILInstruction left, out ILInstruction right)` (PatternMatching.cs line 526):
// the bare BinaryNumericInstruction match. Returns false when the instruction is
// not a BinaryNumericInstruction of the requested operator (left/right untouched).
bool MatchBinaryNumericInstruction(const IL::ILInstruction* inst, IL::BinaryNumericOperator op,
                                   const IL::ILInstruction*& left, const IL::ILInstruction*& right)
{
    if (inst == nullptr || inst->Op != IL::OpCode::BinaryNumericInstruction)
        return false;
    const auto* bni = static_cast<const IL::BinaryNumericInstruction*>(inst);
    if (bni->Operator != op)
        return false;
    left = bni->Left.get();
    right = bni->Right.get();
    return true;
}

// The C# `inst.Value.MatchLdcI(1) || inst.Value.MatchLdcF4(1) ||
// inst.Value.MatchLdcF8(1)` shape of the EvaluatesToOldValue DEBUG assert in
// HandleCompoundAssignment: the post-increment constant is the integer 1 (an
// LdcI4/LdcI8, through MatchLdcI's conv unwrapping) or the float 1.0 (an
// LdcF4/LdcF8 -- the `double++`/`float++` shapes). File-local beside the other
// Match helpers.
bool MatchConstantOne(const IL::ILInstruction* inst)
{
    std::int64_t value = 0;
    if (MatchLdcI(inst, value))
        return value == 1;
    return inst != nullptr
           && ((inst->Op == IL::OpCode::LdcF4
                && static_cast<const IL::LdcF4*>(inst)->Value == 1.0f)
               || (inst->Op == IL::OpCode::LdcF8
                   && static_cast<const IL::LdcF8*>(inst)->Value == 1.0));
}

// The C# `ILInstruction.MatchSizeOf(out IType type)` (Instructions.cs line 8955):
// the bare SizeOf match returning the node's Type operand.
bool MatchSizeOf(const IL::ILInstruction* inst, const TS::IType*& type)
{
    if (inst == nullptr || inst->Op != IL::OpCode::SizeOf)
        return false;
    type = static_cast<const IL::SizeOf*>(inst)->Type.get();
    return true;
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

// The C# `internal TranslatedExpression TranslateTarget(ILInstruction? target,
// bool nonVirtualInvocation, bool memberStatic, IType memberDeclaringType,
// IType? constrainedTo = null)` (ExpressionBuilder.cs lines 2734-2844).
TranslatedExpression ExpressionBuilder::TranslateTarget(IL::ILInstruction* target,
                                                        bool nonVirtualInvocation,
                                                        bool memberStatic,
                                                        const TS::IType& memberDeclaringType,
                                                        const TS::IType* constrainedTo)
{
    if (!memberStatic && target != nullptr)
    {
        // -- The local functions (the C# closure bodies) -------------------

        // The C# `bool MatchLdThis(ILInstruction inst)` local function -- the
        // direct `ldloc this` match, then the struct `box T(ldobj T(ldloc this))
        //` shape the struct builder emits (checked only against the current type
        // definition's own struct kind). Declared before ShouldUseBaseReference
        // (the C# local functions are visible throughout the method).
        auto MatchLdThisLocal = [&](IL::ILInstruction* inst) {
            if (IL::MatchLdThis(inst))
                return true;
            const TS::ITypeDefinition* currentTypeDefinition =
                resolver->CurrentTypeDefinition();
            if (currentTypeDefinition == nullptr)
            {
                // The C# `resolver.CurrentTypeDefinition.Kind` null-deref (the
                // established invalid_argument convention).
                throw std::invalid_argument(
                    "NullReferenceException: Object reference not set to an instance of an object.");
            }
            if (currentTypeDefinition->Kind() != TS::TypeKind::Struct)
                return false;
            IL::ILInstruction* arg = nullptr;
            TS::ITypePtr type;
            if (!IL::MatchBox(inst, arg, type))
                return false;
            IL::ILInstruction* arg2 = nullptr;
            TS::ITypePtr type2;
            if (!IL::MatchLdObj(arg, arg2, type2))
                return false;
            if (type == nullptr || type2 == nullptr || !type->Equals(*type2)
                || !type->Equals(*currentTypeDefinition))
                return false;
            return IL::MatchLdThis(arg2);
        };

        // The C# `bool ShouldUseBaseReference()` local function (it calls the
        // MatchLdThis local function, which includes the struct-box arm).
        auto ShouldUseBaseReference = [&]() {
            if (!nonVirtualInvocation)
                return false;
            if (!MatchLdThisLocal(target))
                return false;
            if ((constrainedTo != nullptr ? constrainedTo : &memberDeclaringType)
                    ->GetDefinition()
                == resolver->CurrentTypeDefinition())
                return false;
            return true;
        };

        if (ShouldUseBaseReference())
        {
            const TS::ITypeDefinition* currentTypeDefinition =
                resolver->CurrentTypeDefinition();
            if (currentTypeDefinition == nullptr)
            {
                // The C# `resolver.CurrentTypeDefinition.DirectBaseTypes`
                // null-deref (the C# builder is only constructed with a current
                // type, so the null case is theoretical; the established
                // invalid_argument convention).
                throw std::invalid_argument(
                    "NullReferenceException: Object reference not set to an instance of an object.");
            }
            // The C# `DirectBaseTypes.FirstOrDefault(t => t.Kind != TypeKind.Interface)`.
            const TS::IType* baseReferenceType = nullptr;
            for (const TS::ITypePtr& baseType : currentTypeDefinition->DirectBaseTypes())
            {
                if (baseType != nullptr && baseType->Kind() != TS::TypeKind::Interface)
                {
                    baseReferenceType = baseType.get();
                    break;
                }
            }
            TS::ITypePtr thisType = std::const_pointer_cast<TS::IType>(
                baseReferenceType != nullptr ? baseReferenceType->shared_from_this()
                                             : memberDeclaringType.shared_from_this());
            return WithRR(
                WithILInstruction(*new Syntax::BaseReferenceExpression(), target),
                std::make_shared<Sem::ThisResolveResult>(thisType, nonVirtualInvocation));
        }
        else
        {
            // The pointer/ref type-hint machinery: a value-type receiver passes
            // the this pointer as a managed reference (a by-ref hint for a Ref
            // receiver, a pointer hint otherwise).
            TS::ITypePtr hintWrapper;
            const TS::IType* targetTypeHint =
                constrainedTo != nullptr ? constrainedTo : &memberDeclaringType;
            if (IL::ExpectedTypeForThisPointer(&memberDeclaringType, constrainedTo)
                == IL::StackType::Ref)
            {
                if (target->ResultType() == IL::StackType::Ref)
                {
                    hintWrapper = std::make_shared<TS::ByReferenceType>(
                        std::const_pointer_cast<TS::IType>(targetTypeHint->shared_from_this()));
                }
                else
                {
                    hintWrapper = std::make_shared<TS::PointerType>(
                        std::const_pointer_cast<TS::IType>(targetTypeHint->shared_from_this()));
                }
                targetTypeHint = hintWrapper.get();
            }
            TranslatedExpression translatedTarget = Translate(target, targetTypeHint);
            if (IL::ExpectedTypeForThisPointer(&memberDeclaringType, constrainedTo)
                == IL::StackType::Ref)
            {
                // When accessing members on value types, ensure we use a reference of the correct type,
                // and not a pointer or a reference to a different type (issue #1333)
                const auto* byRefTargetType =
                    dynamic_cast<const TS::ByReferenceType*>(&translatedTarget.Type());
                TS::IType& expectedElementType = const_cast<TS::IType&>(
                    constrainedTo != nullptr ? *constrainedTo : memberDeclaringType);
                bool sameElementType = byRefTargetType != nullptr
                    && TS::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
                        const_cast<TS::IType&>(*byRefTargetType->Element()), expectedElementType);
                if (!sameElementType)
                {
                    TS::ITypePtr refType = std::make_shared<TS::ByReferenceType>(
                        std::const_pointer_cast<TS::IType>(
                            (constrainedTo != nullptr ? constrainedTo : &memberDeclaringType)
                                ->shared_from_this()));
                    translatedTarget = translatedTarget.ConvertTo(*refType, *this);
                }
            }
            if (auto* directionExpression =
                    dynamic_cast<Syntax::DirectionExpression*>(translatedTarget.Expression()))
            {
                // (ref x).member => x.member
                translatedTarget = translatedTarget.UnwrapChild(directionExpression->Expression());
            }
            else if (auto* operatorExpression =
                         dynamic_cast<Syntax::UnaryOperatorExpression*>(translatedTarget.Expression());
                     operatorExpression != nullptr
                     && operatorExpression->Operator() == Syntax::UnaryOperatorType::NullConditional
                     && dynamic_cast<Syntax::DirectionExpression*>(operatorExpression->Expression())
                         != nullptr)
            {
                // (ref x)?.member => x?.member
                // note: we need to create a new ResolveResult for the null-conditional operator,
                // using the underlying type of the input expression without the DirectionExpression
                TranslatedExpression unwrapped = translatedTarget.UnwrapChild(
                    dynamic_cast<Syntax::DirectionExpression*>(operatorExpression->Expression())
                        ->Expression());
                translatedTarget =
                    WithRR(WithoutILInstruction(*new Syntax::UnaryOperatorExpression(
                               unwrapped.Expression(), Syntax::UnaryOperatorType::NullConditional)),
                           std::make_shared<Sem::ResolveResult>(std::const_pointer_cast<TS::IType>(
                               TS::GetUnderlyingType(unwrapped.Type()).shared_from_this())));
            }
            return EnsureTargetNotNullable(translatedTarget, target);
        }
    }
    else
    {
        const TS::IType& targetType =
            constrainedTo != nullptr ? *constrainedTo : memberDeclaringType;
        return WithRR(
            WithoutILInstruction(
                *new Syntax::TypeReferenceExpression(ConvertType(const_cast<TS::IType&>(targetType)))),
            std::make_shared<Sem::TypeResolveResult>(
                std::const_pointer_cast<TS::IType>(targetType.shared_from_this())));
    }
}

TranslatedExpression ExpressionBuilder::EnsureTargetNotNullable(TranslatedExpression expr,
                                                                 IL::ILInstruction* inst)
{
    // The C# body is fully commented out (the nullability-support TODO), so the
    // member is the identity pass-through; `inst` is unused there as well.
    (void)inst;
    return expr;
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
        case IL::OpCode::Comp:
            return VisitComp(inst, context);
        case IL::OpCode::BinaryNumericInstruction:
            return VisitBinaryNumericInstruction(inst, context);
        case IL::OpCode::UserDefinedCompoundAssign:
            return VisitUserDefinedCompoundAssign(inst, context);
        case IL::OpCode::NumericCompoundAssign:
            return VisitNumericCompoundAssign(inst, context);
        case IL::OpCode::SizeOf:
            return VisitSizeOf(inst, context);
        case IL::OpCode::LdTypeToken:
            return VisitLdTypeToken(inst, context);
        case IL::OpCode::NewArr:
            return VisitNewArr(inst, context);
        case IL::OpCode::Conv:
            return VisitConv(inst, context);
        case IL::OpCode::LocAlloc:
            return VisitLocAlloc(inst, context);
        case IL::OpCode::LocAllocSpan:
            return VisitLocAllocSpan(inst, context);
        case IL::OpCode::LdFtn:
            return VisitLdFtn(inst, context);
        case IL::OpCode::LdVirtFtn:
            return VisitLdVirtFtn(inst, context);
        case IL::OpCode::LdVirtDelegate:
            return VisitLdVirtDelegate(inst, context);
        case IL::OpCode::Unbox:
            return VisitUnbox(inst, context);
        case IL::OpCode::UnboxAny:
            return VisitUnboxAny(inst, context);
        case IL::OpCode::Box:
            return VisitBox(inst, context);
        case IL::OpCode::CastClass:
            return VisitCastClass(inst, context);
        case IL::OpCode::LdObj:
            return VisitLdObj(inst, context);
        case IL::OpCode::StObj:
            return VisitStObj(inst, context);
        case IL::OpCode::LdLen:
            return VisitLdLen(inst, context);
        case IL::OpCode::LdElema:
            return VisitLdElema(inst, context);
        case IL::OpCode::NullableRewrap:
            return VisitNullableRewrap(inst, context);
        case IL::OpCode::NullableUnwrap:
            return VisitNullableUnwrap(inst, context);
        case IL::OpCode::NullCoalescingInstruction:
            return VisitNullCoalescingInstruction(inst, context);
        case IL::OpCode::AddressOf:
            return VisitAddressOf(inst, context);
        case IL::OpCode::RefAnyType:
            return VisitRefAnyType(inst, context);
        case IL::OpCode::IfInstruction:
            return VisitIfInstruction(inst, context);
        case IL::OpCode::SwitchInstruction:
            return VisitSwitchInstruction(inst, context);
        case IL::OpCode::Call:
        case IL::OpCode::CallVirt:
        case IL::OpCode::NewObj:
            return VisitCall(inst, context);
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

// The C# `protected internal override TranslatedExpression VisitUnbox(Unbox inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 3322-3330): the
// managed-pointer unboxing -- a `ref (T)arg` DirectionExpression over a cast of the
// boxed argument to T with the UnboxingConversion, whose resolve result is a
// ByReferenceResolveResult over the cast's resolve result (ReferenceKind.Ref).
TranslatedExpression ExpressionBuilder::VisitUnbox(IL::ILInstruction* inst,
                                                   TranslationContext context)
{
    (void)context;
    auto* unbox = static_cast<IL::Unbox*>(inst);
    TranslatedExpression arg = Translate(unbox->Argument.get());
    auto* castExpr = new Syntax::CastExpression(ConvertType(*unbox->Type), arg.Expression());
    ExpressionWithResolveResult cast = WithRR(
        *castExpr,
        std::make_shared<Sem::ConversionResolveResult>(
            unbox->Type, SharedResolveResultAnnotation(*arg.Expression()),
            Sem::Conversions::UnboxingConversion()));
    return WithRR(
        WithILInstruction(
            *new Syntax::DirectionExpression(Syntax::FieldDirection::Ref,
                                             cast.Expression()),
            inst),
        std::make_shared<Sem::ByReferenceResolveResult>(
            SharedResolveResultAnnotation(*cast.Expression()), TS::ReferenceKind::Ref));
}

// The C# `protected internal override TranslatedExpression VisitUnboxAny(UnboxAny
// inst, TranslationContext context)` (ExpressionBuilder.cs lines 3285-3320): the
// unboxing conversion -- the `unbox.any T(isinst T(expr))` shortcut over a nullable
// value type or a reference type (an `expr as T` with the TryCast conversion), else
// a cast from object (or, for a type-parameter target the resolver rejects, via the
// type parameter's effective base class) with the UnboxingConversion.
TranslatedExpression ExpressionBuilder::VisitUnboxAny(IL::ILInstruction* inst,
                                                      TranslationContext context)
{
    (void)context;
    auto* unboxAny = static_cast<IL::UnboxAny*>(inst);
    TranslatedExpression arg;
    if (auto* isInst = dynamic_cast<IL::IsInst*>(unboxAny->Argument.get());
        isInst != nullptr && isInst->Type != nullptr
        && IsUnboxAnyWithIsInst(*unboxAny, *isInst->Type))
    {
        // unbox.any T(isinst T(expr)) ==> expr as T
        // This is used for generic types and nullable value types
        arg = UnwrapBoxingConversion(Translate(isInst->Argument.get()));
        auto* asExpr = new Syntax::AsExpression(arg.Expression(), ConvertType(*unboxAny->Type));
        return WithRR(
            WithILInstruction(*asExpr, inst),
            std::make_shared<Sem::ConversionResolveResult>(
                unboxAny->Type, SharedResolveResultAnnotation(*arg.Expression()),
                Sem::Conversions::TryCast()));
    }

    arg = Translate(unboxAny->Argument.get());
    TS::ITypePtr targetType = unboxAny->Type;
    if (targetType->Kind() == TS::TypeKind::TypeParameter)
    {
        auto rr = resolver->ResolveCast(*targetType,
                                        SharedResolveResultAnnotation(*arg.Expression()));
        if (rr->IsError())
        {
            // C# 6.2.7 Explicit conversions involving type parameters:
            // if we can't directly convert to a type parameter,
            // try via its effective base class.
            auto* typeParameter = static_cast<TS::ITypeParameter*>(targetType.get());
            arg = arg.ConvertTo(*typeParameter->EffectiveBaseClass(), *this);
        }
    }
    else
    {
        // Before unboxing arg must be a object
        arg = arg.ConvertTo(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Object)), *this);
    }

    auto* cast = new Syntax::CastExpression(ConvertType(*targetType), arg.Expression());
    return WithRR(
        WithILInstruction(*cast, inst),
        std::make_shared<Sem::ConversionResolveResult>(
            targetType, SharedResolveResultAnnotation(*arg.Expression()),
            Sem::Conversions::UnboxingConversion()));
}

// The C# `protected internal override TranslatedExpression VisitBox(Box inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 3332-3352): the boxing
// conversion -- the native-integer target preference (nint/nuint under
// NativeIntegers), the argument conversion to the target type, and the cast to
// object with the BoxingConversion.
TranslatedExpression ExpressionBuilder::VisitBox(IL::ILInstruction* inst,
                                                TranslationContext context)
{
    (void)context;
    auto* box = static_cast<IL::Box*>(inst);
    TS::ITypePtr targetType = box->Type;
    auto arg = Translate(box->Argument.get(), targetType.get());
    if (settings->NativeIntegers() && !arg.Type().Equals(*targetType))
    {
        if (IsKnownType(*targetType, KnownTypeCode::IntPtr))
        {
            targetType = TS::NInt();
        }
        else if (IsKnownType(*targetType, KnownTypeCode::UIntPtr))
        {
            targetType = TS::NUInt();
        }
    }
    arg = arg.ConvertTo(*targetType, *this);
    TS::IType& obj = const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Object));
    auto* cast = new Syntax::CastExpression(ConvertType(obj), arg.Expression());
    return WithRR(
        WithILInstruction(*cast, inst),
        std::make_shared<Sem::ConversionResolveResult>(
            obj.shared_from_this(), SharedResolveResultAnnotation(*arg.Expression()),
            Sem::Conversions::BoxingConversion()));
}

// The C# `protected internal override TranslatedExpression VisitCastClass(CastClass
// inst, TranslationContext context)` (ExpressionBuilder.cs lines 3354-3358): the
// explicit cast -- translate the argument and convert it to the target type.
TranslatedExpression ExpressionBuilder::VisitCastClass(IL::ILInstruction* inst,
                                                       TranslationContext context)
{
    (void)context;
    auto* castClass = static_cast<IL::CastClass*>(inst);
    return Translate(castClass->Argument.get()).ConvertTo(*castClass->Type, *this);
}

// The C# `protected internal override TranslatedExpression VisitLdObj(LdObj inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 2857-2892): the typed
// managed/raw load -- the type-hint preference (skipped for a pointer hint when
// the load type is used in the generic unaligned/ref-address shape), the
// `Unsafe.ReadUnaligned<T>` arm for an `unaligned.` prefix, and the dereference
// render through the LdObj helper.
TranslatedExpression ExpressionBuilder::VisitLdObj(IL::ILInstruction* inst,
                                                    TranslationContext context)
{
    auto* ldObj = static_cast<IL::LdObj*>(inst);
    TS::ITypePtr loadType = ldObj->Type;
    bool loadTypeUsedInGeneric = ldObj->UnalignedPrefix != 0
        || ldObj->Target->ResultType() == IL::StackType::Ref;
    if (context.TypeHint != nullptr && context.TypeHint->Kind() != TS::TypeKind::Unknown
        && TS::IsCompatibleTypeForMemoryAccess(
               const_cast<TS::IType&>(*context.TypeHint), const_cast<TS::IType&>(*loadType))
        && !(loadTypeUsedInGeneric && IsAnyPointer(context.TypeHint->Kind())))
    {
        loadType = const_cast<TS::IType&>(*context.TypeHint).shared_from_this();
    }
    if (ldObj->UnalignedPrefix != 0)
    {
        // Use one of: Unsafe.ReadUnaligned<T>(void*)
        //         or: Unsafe.ReadUnaligned<T>(ref byte)
        TranslatedExpression pointer = Translate(ldObj->Target.get());
        if (dynamic_cast<Syntax::DirectionExpression*>(pointer.Expression()) != nullptr)
        {
            pointer = pointer.ConvertTo(
                *std::make_shared<TS::ByReferenceType>(
                    const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                        .shared_from_this()),
                *this);
        }
        else
        {
            pointer = pointer.ConvertTo(
                *std::make_shared<TS::PointerType>(
                    const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Void))
                        .shared_from_this()),
                *this, /*checkForOverflow=*/false, /*allowImplicitConversion=*/true);
        }
        return CallUnsafeIntrinsic("ReadUnaligned", {pointer.Expression()}, *loadType, ldObj,
                                   std::vector<TS::ITypePtr>{loadType});
    }
    ExpressionWithResolveResult result = LdObj(ldObj->Target.get(), *loadType);
    return WithILInstruction(result, ldObj);
}

// The C# `protected internal override TranslatedExpression VisitStObj(StObj inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 2968-3036): the typed
// store -- the helper-call arm for an `unaligned.` prefix or a non-ref target of a
// managed type, else the pointer dereference and assignment render (with the
// `ref (a = ref b)` re-assignment shape).
TranslatedExpression ExpressionBuilder::VisitStObj(IL::ILInstruction* inst,
                                                    TranslationContext context)
{
    (void)context;
    auto* stObj = static_cast<IL::StObj*>(inst);
    if (stObj->UnalignedPrefix != 0
        || (stObj->Target->ResultType() != IL::StackType::Ref
            && !TS::IsUnmanagedType(*stObj->Type, settings->IntroduceUnmanagedConstraint())))
    {
        return StObjViaHelperCall(stObj);
    }

    TS::ITypePtr pointerTypeHint = stObj->Target->ResultType() == IL::StackType::Ref
        ? TS::ITypePtr(std::make_shared<TS::ByReferenceType>(stObj->Type))
        : TS::ITypePtr(std::make_shared<TS::PointerType>(stObj->Type));
    TranslatedExpression pointer = Translate(stObj->Target.get(), pointerTypeHint.get());
    TranslatedExpression target;
    TranslatedExpression value;
    TS::ITypePtr memoryType;
    // Check if we need to cast to pointer type:
    if (TS::IsCompatiblePointerTypeForMemoryAccess(
            const_cast<TS::IType&>(pointer.Type()), const_cast<TS::IType&>(*stObj->Type)))
    {
        // cast not necessary, we can use the existing type
        if (auto* ptr = dynamic_cast<const TS::PointerType*>(&pointer.Type()))
            memoryType = ptr->Element();
        else
            memoryType = static_cast<const TS::ByReferenceType&>(pointer.Type()).Element();
    }
    else
    {
        // We need to introduce a pointer cast
        value = Translate(stObj->Value.get(), stObj->Type.get());
        if (TS::IsCompatibleTypeForMemoryAccess(const_cast<TS::IType&>(value.Type()),
                                                const_cast<TS::IType&>(*stObj->Type)))
        {
            memoryType = const_cast<TS::IType&>(value.Type()).shared_from_this();
        }
        else
        {
            memoryType = stObj->Type;
        }
        if (dynamic_cast<Syntax::DirectionExpression*>(pointer.Expression()) != nullptr)
        {
            pointer = pointer.ConvertTo(*std::make_shared<TS::ByReferenceType>(memoryType), *this);
        }
        else
        {
            pointer = pointer.ConvertTo(*std::make_shared<TS::PointerType>(memoryType), *this);
        }
    }

    if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(pointer.Expression()))
    {
        // we can deference the managed reference by stripping away the 'ref'
        target = pointer.UnwrapChild(dirExpr->Expression());
    }
    else
    {
        if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(pointer.Expression());
            uoe != nullptr && uoe->Operator() == Syntax::UnaryOperatorType::AddressOf)
        {
            // *&ptr -> ptr
            target = pointer.UnwrapChild(uoe->Expression());
        }
        else
        {
            target = WithRR(
                WithoutILInstruction(*new Syntax::UnaryOperatorExpression(
                    pointer.Expression(), Syntax::UnaryOperatorType::Dereference)),
                std::make_shared<Sem::ResolveResult>(memoryType));
        }
    }
    if (value.Expression() == nullptr)
    {
        value = Translate(stObj->Value.get(), &target.Type());
    }
    if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(target.Expression()))
    {
        auto* lhsRefRR = dynamic_cast<const Sem::ByReferenceResolveResult*>(target.ResolveResult());
        if (lhsRefRR != nullptr)
        {
            // ref (re-)assignment, emit "ref (a = ref b)".
            std::shared_ptr<Sem::ResolveResult> lhsRRHandle =
                SharedResolveResultAnnotation(*target.Expression());
            target = target.UnwrapChild(dirExpr->Expression());
            value = value.ConvertTo(const_cast<TS::IType&>(lhsRefRR->Type()), *this,
                                    /*checkForOverflow=*/false,
                                    /*allowImplicitConversion=*/true);
            auto* assign = new Syntax::AssignmentExpression(target.Expression(),
                                                            value.Expression());
            WithRR(*assign,
                   std::make_shared<Sem::OperatorResolveResult>(
                       const_cast<TS::IType&>(target.Type()).shared_from_this(),
                       TS::ExpressionType::Assign,
                       std::vector<std::shared_ptr<Sem::ResolveResult>>{
                           lhsRRHandle, SharedResolveResultAnnotation(*value.Expression())}));
            return WithRR(
                WithoutILInstruction(*new Syntax::DirectionExpression(
                    Syntax::FieldDirection::Ref, assign)),
                lhsRRHandle);
        }
    }
    return WithILInstruction(Assignment(target, value), stObj);
}

// The C# `private TranslatedExpression StObjViaHelperCall(StObj inst)`
// (ExpressionBuilder.cs lines 3087-3125): `"unaligned.1; stobj"` becomes
// `Unsafe.WriteUnaligned<T>(void*, T)` (or `(ref byte, T)`) and `"stobj ManagedType"`
// becomes `Unsafe.Write<T>(void*, T)`.
TranslatedExpression ExpressionBuilder::StObjViaHelperCall(IL::ILInstruction* inst)
{
    auto* stObj = static_cast<IL::StObj*>(inst);
    TranslatedExpression pointer = Translate(stObj->Target.get());
    TranslatedExpression value = Translate(stObj->Value.get(), stObj->Type.get());
    if (dynamic_cast<Syntax::DirectionExpression*>(pointer.Expression()) != nullptr
        && stObj->UnalignedPrefix != 0)
    {
        pointer = pointer.ConvertTo(
            *std::make_shared<TS::ByReferenceType>(
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                    .shared_from_this()),
            *this);
    }
    else
    {
        pointer = pointer.ConvertTo(
            *std::make_shared<TS::PointerType>(
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Void))
                    .shared_from_this()),
            *this, /*checkForOverflow=*/false, /*allowImplicitConversion=*/true);
    }
    if (!TS::IsCompatibleTypeForMemoryAccess(const_cast<TS::IType&>(value.Type()),
                                             const_cast<TS::IType&>(*stObj->Type)))
    {
        value = value.ConvertTo(const_cast<TS::IType&>(*stObj->Type), *this);
    }
    if (stObj->UnalignedPrefix != 0)
    {
        return CallUnsafeIntrinsic(
            "WriteUnaligned", {pointer.Expression(), value.Expression()},
            compilation->FindType(KnownTypeCode::Void), stObj);
    }
    else
    {
        return CallUnsafeIntrinsic("Write", {pointer.Expression(), value.Expression()},
                                   *stObj->Type, stObj);
    }
}

// The C# `protected internal override TranslatedExpression VisitLdLen(LdLen
// inst, TranslationContext context)` (ExpressionBuilder.cs lines 3088-3116): the
// `ldlen` array-length render. The array is translated with the System.Array type
// hint; a non-array expression is converted to System.Array (the raw-pointer shape
// the C# emits before the member access). The load's StackType selects the member
// name and the result type -- I4 gives `Length` (Int32), every other stack type
// gives `LongLength` (Int64). The property is looked up on System.Array; when the
// type exposes no such property (the MinimalCorlib fixture), the resolve result
// degrades to a plain Int32/Int64 result.
TranslatedExpression ExpressionBuilder::VisitLdLen(IL::ILInstruction* inst,
                                                   TranslationContext context)
{
    (void)context;
    auto* ldLen = static_cast<IL::LdLen*>(inst);
    const TS::IType& arrayType = compilation->FindType(KnownTypeCode::Array);
    TranslatedExpression arrayExpr = Translate(ldLen->Argument.get(), &arrayType);
    if (arrayExpr.Type().Kind() != TS::TypeKind::Array)
    {
        arrayExpr = arrayExpr.ConvertTo(const_cast<TS::IType&>(arrayType), *this);
    }
    arrayExpr = EnsureTargetNotNullable(arrayExpr, ldLen->Argument.get());

    std::string memberName;
    KnownTypeCode code;
    if (ldLen->resultType == IL::StackType::I4)
    {
        memberName = "Length";
        code = KnownTypeCode::Int32;
    }
    else
    {
        memberName = "LongLength";
        code = KnownTypeCode::Int64;
    }

    const TS::IProperty* member = nullptr;
    {
        std::vector<const TS::IProperty*> props = arrayType.GetProperties(
            [&memberName](const TS::IProperty* p) { return p->Name() == memberName; });
        if (!props.empty())
            member = props.front();
    }
    std::shared_ptr<Sem::ResolveResult> rr =
        member == nullptr
            ? std::make_shared<Sem::ResolveResult>(
                  const_cast<TS::IType&>(compilation->FindType(code)).shared_from_this())
            : std::make_shared<Sem::MemberResolveResult>(
                  SharedResolveResultAnnotation(*arrayExpr.Expression()), member);
    auto* memberRef = new Syntax::MemberReferenceExpression(arrayExpr.Expression(), memberName);
    return WithRR(WithILInstruction(*memberRef, inst), rr);
}

// The C# `protected internal override TranslatedExpression VisitLdElema(LdElema
// inst, TranslationContext context)` (ExpressionBuilder.cs lines 3203-3229): the
// `ldelema` array-element-address render. The array is translated; when the
// translated type is not an array of the element type (a non-array expression or a
// mismatched element type), the array is converted to a fresh array of
// `inst.Type` and `inst.Indices.Count` dimensions. Each index goes through
// TranslateArrayIndex, or -- when the `withsystemindex` prefix is set -- is
// translated and converted against the System.Index hint. The result is the
// indexer expression wrapped in a `ref` DirectionExpression whose resolve result is
// a ByReferenceResolveResult over the element type's ResolveResult.
TranslatedExpression ExpressionBuilder::VisitLdElema(IL::ILInstruction* inst,
                                                    TranslationContext context)
{
    (void)context;
    auto* ldElema = static_cast<IL::LdElema*>(inst);
    TranslatedExpression arrayExpr = Translate(ldElema->Array.get());
    auto* arrayType = dynamic_cast<TS::ArrayType*>(&const_cast<TS::IType&>(arrayExpr.Type()));
    TS::ITypePtr ownedArrayType;
    if (arrayType == nullptr
        || !TS::IsCompatibleTypeForMemoryAccess(
               const_cast<TS::IType&>(*arrayType->Element()),
               const_cast<TS::IType&>(*ldElema->Type)))
    {
        // The C# `new ArrayType(compilation, inst.Type, inst.Indices.Count)`: one
        // dimension is the SZArray shape, more are the multi-dimensional rank (the
        // port's ArrayType splits the two forms over its two ctors).
        ownedArrayType = ldElema->Indices.size() == 1
                             ? std::make_shared<TS::ArrayType>(ldElema->Type)
                             : std::make_shared<TS::ArrayType>(
                                   ldElema->Type,
                                   static_cast<int>(ldElema->Indices.size()));
        arrayType = static_cast<TS::ArrayType*>(ownedArrayType.get());
        arrayExpr = arrayExpr.ConvertTo(*arrayType, *this);
    }
    auto* indexerExpr = new Syntax::IndexerExpression(arrayExpr.Expression());
    if (ldElema->WithSystemIndex)
    {
        const TS::IType& systemIndex = compilation->FindType(TS::KnownTypeCode::Index);
        for (const auto& index : ldElema->Indices)
        {
            TranslatedExpression translated = Translate(index.get(), &systemIndex);
            translated = translated.ConvertTo(const_cast<TS::IType&>(systemIndex), *this);
            indexerExpr->Arguments().Add(translated.Expression());
        }
    }
    else
    {
        for (const auto& index : ldElema->Indices)
            indexerExpr->Arguments().Add(TranslateArrayIndex(index.get()).Expression());
    }
    TranslatedExpression expr = WithRR(
        WithILInstruction(*indexerExpr, inst),
        std::make_shared<Sem::ResolveResult>(arrayType->Element()));
    return WithRR(
        WithoutILInstruction(*new Syntax::DirectionExpression(
            Syntax::FieldDirection::Ref, expr.Expression())),
        std::make_shared<Sem::ByReferenceResolveResult>(
            SharedResolveResultAnnotation(*expr.Expression()), TS::ReferenceKind::Ref));
}

// The C# `protected internal override TranslatedExpression
// VisitNullableRewrap(NullableRewrap inst, TranslationContext context)`
// (ExpressionBuilder.cs lines 4298-4309): the null-conditional join point. A
// non-nullable value-type Argument is lifted into `Nullable<T>` (the C#
// `NullableType.Create(compilation, type)`), a reference type is kept as-is; the
// render is a NullConditionalRewrap UnaryOperatorExpression whose resolve result is
// a plain ResolveResult of the (possibly lifted) type.
TranslatedExpression ExpressionBuilder::VisitNullableRewrap(IL::ILInstruction* inst,
                                                           TranslationContext context)
{
    (void)context;
    auto* rewrap = static_cast<IL::NullableRewrap*>(inst);
    TranslatedExpression arg = Translate(rewrap->Argument.get());
    TS::ITypePtr type = const_cast<TS::IType&>(arg.Type()).shared_from_this();
    if (TS::IsNonNullableValueType(arg.Type()))
    {
        type = TS::Create(*compilation, arg.Type());
    }
    auto* unary = new Syntax::UnaryOperatorExpression(
        arg.Expression(), Syntax::UnaryOperatorType::NullConditionalRewrap);
    return WithRR(WithILInstruction(*unary, inst),
                  std::make_shared<Sem::ResolveResult>(type));
}

// The C# `protected internal override TranslatedExpression
// VisitNullableUnwrap(NullableUnwrap inst, TranslationContext context)`
// (ExpressionBuilder.cs lines 4311-4321): the `?.` dereference. When the node has a
// RefInput (but not a ref-typed output) and the Argument rendered as a ref
// DirectionExpression, the `ref` is stripped -- the managed reference is
// dereferenced by removing the direction. The render is a NullConditional
// UnaryOperatorExpression whose resolve result is a plain ResolveResult of the
// underlying type (NullableType.GetUnderlyingType, the identity for a
// non-nullable input).
TranslatedExpression ExpressionBuilder::VisitNullableUnwrap(IL::ILInstruction* inst,
                                                           TranslationContext context)
{
    (void)context;
    auto* unwrap = static_cast<IL::NullableUnwrap*>(inst);
    TranslatedExpression arg = Translate(unwrap->Argument.get());
    if (unwrap->RefInput && !unwrap->RefOutput())
    {
        if (auto* dir = dynamic_cast<Syntax::DirectionExpression*>(arg.Expression()))
        {
            arg = arg.UnwrapChild(dir->Expression());
        }
    }
    const TS::IType& underlying = TS::GetUnderlyingType(arg.Type());
    auto* unary = new Syntax::UnaryOperatorExpression(
        arg.Expression(), Syntax::UnaryOperatorType::NullConditional);
    return WithRR(
        WithILInstruction(*unary, inst),
        std::make_shared<Sem::ResolveResult>(
            const_cast<TS::IType&>(underlying).shared_from_this()));
}

// The C# `protected internal override TranslatedExpression
// VisitNullCoalescingInstruction(NullCoalescingInstruction inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 3912-3953): the `a ?? b`
// render. Both operands are translated and the fallback is constant-adjusted to the
// value's type; the resolver's null-coalescing resolution becomes the resolve result
// when it succeeds. On an error the target type is recovered -- a throw fallback over
// NoType uses the value's underlying type (NullableType.GetUnderlyingType), two
// differing non-null-literal types fall back to `inst.UnderlyingResultType`, else the
// non-null operand's type -- and the operands are converted (a Nullable<T> wrap for
// the non-ref kinds, plus a second nullable wrap of the value for the Nullable kind)
// before a fresh ResolveResult replaces the error. The render is a
// BinaryOperatorExpression with the NullCoalescing operator.
TranslatedExpression ExpressionBuilder::VisitNullCoalescingInstruction(
    IL::ILInstruction* inst, TranslationContext context)
{
    (void)context;
    auto* coalescing = static_cast<IL::NullCoalescingInstruction*>(inst);
    TranslatedExpression value = Translate(coalescing->ValueInst.get());
    TranslatedExpression fallback = Translate(coalescing->FallbackInst.get());
    fallback = AdjustConstantExpressionToType(
        std::move(fallback), const_cast<TS::IType&>(value.Type()));
    std::shared_ptr<Sem::ResolveResult> rr = resolver->ResolveBinaryOperator(
        Syntax::BinaryOperatorType::NullCoalescing,
        SharedResolveResultAnnotation(*value.Expression()),
        SharedResolveResultAnnotation(*fallback.Expression()));
    if (rr->IsError())
    {
        TS::ITypePtr targetType;
        if (dynamic_cast<Syntax::ThrowExpression*>(fallback.Expression()) != nullptr
            && fallback.Type().Equals(*TS::NoType()))
        {
            targetType = const_cast<TS::IType&>(TS::GetUnderlyingType(value.Type()))
                             .shared_from_this();
        }
        else if (!value.Type().Equals(*TS::NullType())
                 && !fallback.Type().Equals(*TS::NullType())
                 && !value.Type().Equals(fallback.Type()))
        {
            targetType =
                const_cast<TS::IType&>(TS::FindType(*compilation,
                                                    coalescing->UnderlyingResultType))
                    .shared_from_this();
        }
        else
        {
            targetType = value.Type().Equals(*TS::NullType())
                             ? const_cast<TS::IType&>(fallback.Type()).shared_from_this()
                             : const_cast<TS::IType&>(value.Type()).shared_from_this();
        }
        if (coalescing->Kind != IL::NullCoalescingKind::Ref)
        {
            value = value.ConvertTo(*TS::Create(*compilation, *targetType), *this);
        }
        else
        {
            value = value.ConvertTo(*targetType, *this);
        }
        if (coalescing->Kind == IL::NullCoalescingKind::Nullable)
        {
            value = value.ConvertTo(*TS::Create(*compilation, *targetType), *this);
        }
        else
        {
            fallback = fallback.ConvertTo(*targetType, *this);
        }
        rr = std::make_shared<Sem::ResolveResult>(targetType);
    }
    auto* binary = new Syntax::BinaryOperatorExpression(
        value.Expression(), Syntax::BinaryOperatorType::NullCoalescing,
        fallback.Expression());
    return WithRR(WithILInstruction(*binary, inst), rr);
}

// The C# `protected internal override TranslatedExpression VisitAddressOf(AddressOf
// inst, TranslationContext context)` (ExpressionBuilder.cs lines 4231-4266): the
// `&value` render. The wrapped value is classified (ILInlining.ClassifyExpression)
// and, when it is a mutable lvalue whose address would let a C# method call mutate
// the original rather than a copy, a redundant cast is inserted so the C# compiler
// also creates the copy -- unless the immediate chain sits under an ldobj, which is
// a pure read where the copy is not observable. The render is a ref
// DirectionExpression carrying a ByReferenceResolveResult over the (possibly cast)
// value's resolve result.
TranslatedExpression ExpressionBuilder::VisitAddressOf(IL::ILInstruction* inst,
                                                       TranslationContext context)
{
    (void)context;
    auto* addressOf = static_cast<IL::AddressOf*>(inst);
    IL::ExpressionClassification classification =
        IL::ClassifyExpression(addressOf->Argument.get());
    TranslatedExpression value = Translate(addressOf->Argument.get(), addressOf->Type.get());
    value = value.ConvertTo(*addressOf->Type, *this);
    // The C# local function CanIgnoreCopy(): walk up the ldflda chain; when the
    // chain's parent is an ldobj the address feeds a load, so no cast is needed.
    bool canIgnoreCopy = false;
    {
        IL::ILInstruction* loadAddress = inst;
        while (auto* parent = dynamic_cast<IL::LdFlda*>(loadAddress->Parent))
            loadAddress = parent;
        if (dynamic_cast<IL::LdObj*>(loadAddress->Parent) != nullptr)
            canIgnoreCopy = true;
    }
    if (classification == IL::ExpressionClassification::MutableLValue
        && !canIgnoreCopy
        && dynamic_cast<Syntax::CastExpression*>(value.Expression()) == nullptr)
    {
        auto* cast = new Syntax::CastExpression(ConvertType(*addressOf->Type),
                                                value.Expression());
        value = WithRR(
            WithoutILInstruction(*cast),
            std::make_shared<Sem::ConversionResolveResult>(
                addressOf->Type,
                SharedResolveResultAnnotation(*value.Expression()),
                Sem::Conversions::IdentityConversion()));
    }
    auto* direction = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref,
                                                      value.Expression());
    return WithRR(
        WithILInstruction(*direction, inst),
        std::make_shared<Sem::ByReferenceResolveResult>(
            SharedResolveResultAnnotation(*value.Expression()),
            TS::ReferenceKind::Ref));
}

// The C# `protected internal override TranslatedExpression VisitRefAnyType(
// RefAnyType inst, TranslationContext context)` (ExpressionBuilder.cs lines
// 3386-3394): the `__reftype(typedReference).TypeHandle` render -- the
// RefType UndocumentedExpression over the translated argument, wrapped in a
// `TypeHandle` member reference whose resolve result is a TypeResolveResult for
// System.RuntimeTypeHandle.
TranslatedExpression ExpressionBuilder::VisitRefAnyType(IL::ILInstruction* inst,
                                                        TranslationContext context)
{
    (void)context;
    auto* refAnyType = static_cast<IL::RefAnyType*>(inst);
    auto* doc = new Syntax::UndocumentedExpression();
    doc->UndocumentedExpressionType(Syntax::UndocumentedExpressionType::RefType);
    doc->Arguments().Add(Translate(refAnyType->Argument.get()).Expression());
    auto* memberRef = new Syntax::MemberReferenceExpression(doc, "TypeHandle");
    // The C# `compilation.FindType(new TopLevelTypeName("System",
    // "RuntimeTypeHandle"))` -- the modules-scan extension over the full type
    // name (the VisitLdTypeToken precedent).
    TS::ITypePtr runtimeTypeHandleType =
        TS::FindType(*compilation,
                     TS::FullTypeName(TS::TopLevelTypeName("System", "RuntimeTypeHandle")));
    return WithRR(WithILInstruction(*memberRef, inst),
                  std::make_shared<Sem::TypeResolveResult>(
                      std::move(runtimeTypeHandleType)));
}

// The C# `protected internal override TranslatedExpression
// VisitIfInstruction(IfInstruction inst, TranslationContext context)`
// (ExpressionBuilder.cs lines 3956-4049): the if-as-expression render. The
// short-circuit `&&`/`||` shapes (an if whose unused arm is the 0/1 constant)
// become binary conditional operators when the rhs is boolean or the if itself
// sits in a condition slot; otherwise the two arms are translated as the two
// branches of a `?:` conditional, their types united through
// ResolveConditional (with the GetBestCommonType/target-type recovery when the
// resolver rejects the pair) and the result re-wrapped in a ref direction
// expression when the conditional produces a by-reference value.
TranslatedExpression ExpressionBuilder::VisitIfInstruction(IL::ILInstruction* inst,
                                                           TranslationContext context)
{
    auto* ifInst = static_cast<IL::IfInstruction*>(inst);
    auto translateArm = [&](IL::ILInstruction* arm) -> TranslatedExpression {
        if (arm != nullptr)
            return Translate(arm, context.TypeHint);
        // The C# generated FalseInst/TrueInst default is a Nop node; the port
        // models a missing arm as a null child (the reader's fall-through if),
        // whose translation is the Nop error expression.
        return ErrorExpression("OpCode not supported: "
                               + std::string(IL::OpCodeName(IL::OpCode::Nop)));
    };
    TranslatedExpression condition = TranslateCondition(ifInst->Condition.get());
    TranslatedExpression trueBranch = translateArm(ifInst->TrueInst.get());
    TranslatedExpression falseBranch = translateArm(ifInst->FalseInst.get());
    Syntax::BinaryOperatorType op = Syntax::BinaryOperatorType::Any;
    TranslatedExpression rhs;
    IL::ILInstruction* lhsInst = nullptr;
    IL::ILInstruction* rhsInst = nullptr;
    if (IL::MatchLogicAnd(ifInst, lhsInst, rhsInst) && !IL::MatchLdcI4(rhsInst, 1))
    {
        op = Syntax::BinaryOperatorType::ConditionalAnd;
        rhs = trueBranch;
    }
    else if (IL::MatchLogicOr(ifInst, lhsInst, rhsInst) && !IL::MatchLdcI4(rhsInst, 0))
    {
        op = Syntax::BinaryOperatorType::ConditionalOr;
        rhs = falseBranch;
    }
    // ILAst LogicAnd/LogicOr can return a different value than 0 or 1 if the rhs
    // is evaluated. We can only correctly translate it to C# if the rhs is of
    // type boolean, or if we're in a context where the result is only used as a
    // condition.
    if (op != Syntax::BinaryOperatorType::Any
        && (TS::IsKnownType(rhs.Type(), TS::KnownTypeCode::Boolean)
            || IL::IfInstruction::IsInConditionSlot(ifInst)))
    {
        if (GetSize(TS::GetStackType(rhs.Type())) > 4)
        {
            rhs = rhs.ConvertTo(*FindType(IL::StackType::I4, TS::GetSign(&rhs.Type())), *this);
        }
        rhs = rhs.ConvertToBoolean(*this);
        auto* boolBinary = new Syntax::BinaryOperatorExpression(
            condition.Expression(), op, rhs.Expression());
        return WithRR(
            WithILInstruction(*boolBinary, inst),
            std::make_shared<Sem::ResolveResult>(
                const_cast<TS::IType&>(compilation->FindType(TS::KnownTypeCode::Boolean))
                    .shared_from_this()));
    }

    condition = condition.UnwrapImplicitBoolConversion();
    trueBranch = AdjustConstantExpressionToType(
        std::move(trueBranch), const_cast<TS::IType&>(falseBranch.Type()));
    falseBranch = AdjustConstantExpressionToType(
        std::move(falseBranch), const_cast<TS::IType&>(trueBranch.Type()));

    std::shared_ptr<Sem::ResolveResult> rr = resolver->ResolveConditional(
        SharedResolveResultAnnotation(*condition.Expression()),
        SharedResolveResultAnnotation(*trueBranch.Expression()),
        SharedResolveResultAnnotation(*falseBranch.Expression()));
    if (rr->IsError())
    {
        TS::ITypePtr targetType;
        if (!trueBranch.Type().Equals(*TS::NullType())
            && !falseBranch.Type().Equals(*TS::NullType())
            && !trueBranch.Type().Equals(falseBranch.Type()))
        {
            bool success = false;
            targetType = Resolver::Detail::GetBestCommonType(
                *typeInference.compilation,
                Resolver::CSharpConversions::Get(*typeInference.compilation),
                {SharedResolveResultAnnotation(*trueBranch.Expression()),
                 SharedResolveResultAnnotation(*falseBranch.Expression())},
                success, typeInference.algorithm);
            if (!success || TS::GetStackType(*targetType) != ifInst->ResultType())
            {
                // Figure out the target type based on inst.ResultType.
                if (context.TypeHint != nullptr
                    && context.TypeHint->Kind() != TS::TypeKind::Unknown
                    && TS::GetStackType(*context.TypeHint) == ifInst->ResultType())
                {
                    targetType = const_cast<TS::IType*>(context.TypeHint)->shared_from_this();
                }
                else if (ifInst->ResultType() == IL::StackType::Ref)
                {
                    // targetType should be a ref-type
                    if (trueBranch.Type().Kind() == TS::TypeKind::ByReference)
                    {
                        targetType =
                            const_cast<TS::IType&>(trueBranch.Type()).shared_from_this();
                    }
                    else if (falseBranch.Type().Kind() == TS::TypeKind::ByReference)
                    {
                        targetType =
                            const_cast<TS::IType&>(falseBranch.Type()).shared_from_this();
                    }
                    else
                    {
                        // fall back to 'ref byte' if we can't determine a
                        // referenced type otherwise
                        targetType = std::make_shared<TS::ByReferenceType>(
                            const_cast<TS::IType&>(
                                compilation->FindType(TS::KnownTypeCode::Byte))
                                .shared_from_this());
                    }
                }
                else
                {
                    targetType = FindType(
                        ifInst->ResultType(),
                        context.TypeHint != nullptr ? TS::GetSign(context.TypeHint)
                                                    : TS::Sign::None);
                }
            }
        }
        else
        {
            targetType = trueBranch.Type().Equals(*TS::NullType())
                             ? const_cast<TS::IType&>(falseBranch.Type()).shared_from_this()
                             : const_cast<TS::IType&>(trueBranch.Type()).shared_from_this();
        }
        trueBranch = trueBranch.ConvertTo(*targetType, *this);
        falseBranch = falseBranch.ConvertTo(*targetType, *this);
        rr = std::make_shared<Sem::ResolveResult>(targetType);
    }
    if (rr->Type().Kind() == TS::TypeKind::ByReference)
    {
        // C# conditional ref looks like this:
        // ref (arr != null ? ref trueBranch : ref falseBranch);
        const auto* byRefType = dynamic_cast<const TS::ByReferenceType*>(&rr->Type());
        auto conditionalResolveResult = std::make_shared<Sem::ResolveResult>(
            byRefType != nullptr ? byRefType->Element() : TS::NoType());
        auto* condExpr = new Syntax::ConditionalExpression(
            condition.Expression(), trueBranch.Expression(), falseBranch.Expression());
        TranslatedExpression conditional =
            WithRR(WithILInstruction(*condExpr, inst), conditionalResolveResult);
        return WithRR(
            WithoutILInstruction(*new Syntax::DirectionExpression(
                Syntax::FieldDirection::Ref, conditional.Expression())),
            std::make_shared<Sem::ByReferenceResolveResult>(
                conditionalResolveResult, TS::ReferenceKind::Ref));
    }
    else
    {
        auto* condExpr = new Syntax::ConditionalExpression(
            condition.Expression(), trueBranch.Expression(), falseBranch.Expression());
        return WithRR(WithILInstruction(*condExpr, inst), rr);
    }
}

// The C# `protected internal override TranslatedExpression
// VisitSwitchInstruction(SwitchInstruction inst, TranslationContext context)`
// (ExpressionBuilder.cs lines 4176-4229): see the header comment for the contract.
TranslatedExpression ExpressionBuilder::VisitSwitchInstruction(IL::ILInstruction* inst,
                                                               TranslationContext context)
{
    auto* switchInst = static_cast<IL::SwitchInstruction*>(inst);
    // switch-expression does not support implicit conversions.
    SwitchValueTranslation translation = TranslateSwitchValue(*switchInst, true);

    IL::SwitchSection* defaultSection = switchInst->GetDefaultSection();
    auto* switchExpr = new Syntax::SwitchExpression();
    switchExpr->Expression(translation.Value.Expression());
    const TS::IType* resultType;
    if (context.TypeHint != nullptr && context.TypeHint->Kind() != TS::TypeKind::Unknown
        && TS::GetStackType(*context.TypeHint) == switchInst->ResultType())
    {
        resultType = context.TypeHint;
    }
    else
    {
        resultType = &TS::FindType(*compilation, switchInst->ResultType(), TS::Sign::None);
    }

    // The C# local function TranslateSectionBody (the arm body conversion to the
    // switch's result type, allowImplicitConversion true).
    auto translateSectionBody = [&](IL::SwitchSection& section) -> Syntax::Expression* {
        TranslatedExpression body = Translate(section.Body.get(), resultType);
        return body
            .ConvertTo(const_cast<TS::IType&>(*resultType), *this,
                       /*checkForOverflow*/ false,
                       /*allowImplicitConversion*/ true)
            .Expression();
    };

    for (auto& section : switchInst->Sections)
    {
        if (section.get() == defaultSection)
            continue;
        auto* ses = new Syntax::SwitchExpressionSection();
        if (section->HasNullLabel)
        {
            assert(section->Labels.Count() == 0);
            ses->Pattern(new Syntax::NullReferenceExpression());
        }
        else
        {
            std::vector<long long> values = section->Labels.Values();
            long long val = values.at(0);
            const std::vector<std::pair<std::optional<std::string>, int>>* map =
                translation.StringToInt != nullptr ? &translation.StringToInt->Map : nullptr;
            auto labels = const_cast<StatementBuilder*>(statementBuilder)->CreateTypedCaseLabel(
                val, const_cast<TS::IType&>(*translation.CaseType), map);
            ses->Pattern(astBuilder->ConvertConstantValue(labels.at(0)));
        }
        ses->Body(translateSectionBody(*section));
        switchExpr->SwitchSections().Add(ses);
    }

    if (defaultSection != nullptr && !defaultSection->IsCompilerGeneratedDefaultSection)
    {
        auto* defaultSES = new Syntax::SwitchExpressionSection();
        defaultSES->Pattern(new Syntax::IdentifierExpression("_"));
        defaultSES->Body(translateSectionBody(*defaultSection));
        switchExpr->SwitchSections().Add(defaultSES);
    }

    return WithRR(WithILInstruction(*switchExpr, inst),
                  std::make_shared<Sem::ResolveResult>(
                      const_cast<TS::IType&>(*resultType).shared_from_this()));
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

// The C# `protected internal override TranslatedExpression VisitConv(Conv inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 2227-2386): the numeric
// conversion render. We're dealing with two conversions: a) the implicit one from
// the argument's C# type to the conv's input stack type (oversized when widening,
// undersized when narrowing), b) the conv instruction itself from the input stack
// type to the target type. In C#, zero vs. sign-extension depends on the input
// type, but in the ILAst conv instruction it depends on the output type --
// however, in the conv.ovf instructions the .NET runtime behavior depends on the
// input type (in violation of the ECMA-335 spec).
TranslatedExpression ExpressionBuilder::VisitConv(IL::ILInstruction* inst,
                                                 TranslationContext context)
{
    auto* conv = static_cast<IL::Conv*>(inst);
    TS::Sign hintSign = conv->InputSign;
    if (hintSign == TS::Sign::None)
    {
        hintSign = TS::GetSign(context.TypeHint);
    }
    TranslatedExpression arg =
        Translate(conv->Argument.get(), FindArithmeticType(conv->InputType, hintSign).get());
    const TS::IType& inputType = TS::GetUnderlyingType(arg.Type());
    IL::StackType inputStackType = conv->InputType;

    // The C# local function `IType GetType(KnownTypeCode typeCode)`: the FindType
    // with the n(u)int preference over (U)IntPtr under the NativeIntegers setting,
    // then the IsLifted Nullable<T> wrap.
    auto GetType = [&](TS::KnownTypeCode typeCode) -> TS::ITypePtr
    {
        TS::ITypePtr type = const_cast<TS::IType&>(compilation->FindType(typeCode))
                                .shared_from_this();
        // Prefer n(u)int over (U)IntPtr
        if (typeCode == TS::KnownTypeCode::IntPtr && settings->NativeIntegers()
            && !type->Equals(*context.TypeHint))
        {
            type = TS::NInt();
        }
        else if (typeCode == TS::KnownTypeCode::UIntPtr && settings->NativeIntegers()
                 && !type->Equals(*context.TypeHint))
        {
            type = TS::NUInt();
        }
        if (conv->IsLifted)
        {
            type = TS::Create(*compilation, *type);
        }
        return type;
    };

    if (conv->CheckForOverflow || conv->Kind == IL::ConversionKind::IntToFloat)
    {
        // We need to first convert the argument to the expected sign.
        // We also need to perform any input narrowing conversion so that it doesn't
        // get mixed up with the overflow check. Because casts with overflow check
        // match C# semantics (zero/sign-extension depends on source type), we can
        // just directly cast to the target type.
        if (TS::GetSize(&inputType) > TS::GetSize(inputStackType)
            || TS::GetSign(&inputType) != conv->InputSign)
        {
            arg = arg.ConvertTo(
                *GetType(TS::ToKnownTypeCode(inputStackType, conv->InputSign)), *this);
        }
        TranslatedExpression result =
            arg.ConvertTo(*GetType(TS::ToKnownTypeCode(conv->TargetType)), *this,
                          conv->CheckForOverflow);
        return WithILInstruction(result, conv);
    }

    switch (conv->Kind)
    {
        case IL::ConversionKind::StartGCTracking:
            // A "start gc tracking" conversion is inserted in the ILAst whenever
            // some instruction expects a managed pointer, but we pass an unmanaged
            // pointer. We'll leave the C#-level conversion (from T* to ref T) to the
            // consumer that expects the managed pointer.
            return arg;
        case IL::ConversionKind::StopGCTracking:
            if (inputType.Kind() == TS::TypeKind::ByReference)
            {
                if (IsFixedVariableInstruction(*conv->Argument))
                {
                    // cast to corresponding pointer type:
                    auto pointerType = std::make_shared<TS::PointerType>(
                        static_cast<const TS::ByReferenceType&>(inputType).Element());
                    TranslatedExpression result = arg.ConvertTo(*pointerType, *this);
                    return WithILInstruction(result, conv);
                }
                else
                {
                    // emit Unsafe.AsPointer() intrinsic:
                    auto pointerType = std::make_shared<TS::PointerType>(
                        const_cast<TS::IType&>(
                            compilation->FindType(TS::KnownTypeCode::Void))
                            .shared_from_this());
                    return CallUnsafeIntrinsic("AsPointer", {arg.Expression()},
                                               *pointerType, conv);
                }
            }
            else if (IL::IsIntegerType(TS::GetStackType(arg.Type())))
            {
                // ConversionKind.StopGCTracking should only be used with managed
                // references, but it's possible that we're supposed to stop tracking
                // something we just started to track.
                return arg;
            }
            else
            {
                goto defaultArm;
            }
        case IL::ConversionKind::SignExtend:
            // We just need to ensure the input type before the conversion is signed.
            // Also, if the argument was translated into an oversized C# type, we need
            // to perform the truncatation to the input stack type. An undersized C#
            // type is handled just fine: if it is unsigned we'll zero-extend it to
            // the width of the inputStackType here, and if it is signed we just
            // combine the two sign-extensions into a single sign-extending
            // conversion. Then we can just return the argument as-is: the
            // ExpressionBuilder post-condition allows us to force our parent
            // instruction to handle the actual sign-extension conversion (our caller
            // may have more information to pick a better fitting target type).
            if (TS::GetSign(&inputType) != TS::Sign::Signed
                || ValueMightBeOversized(*arg.ResolveResult(), inputStackType))
            {
                arg = arg.ConvertTo(
                    *GetType(TS::ToKnownTypeCode(inputStackType, TS::Sign::Signed)), *this);
            }
            return WithILInstruction(arg, conv);
        case IL::ConversionKind::ZeroExtend:
            // If overflow check cannot fail, handle this just like sign extension
            // (except for swapped signs)
            if (TS::GetSign(&inputType) != TS::Sign::Unsigned
                || TS::GetSize(&inputType) > TS::GetSize(inputStackType))
            {
                arg = arg.ConvertTo(
                    *GetType(TS::ToKnownTypeCode(inputStackType, TS::Sign::Unsigned)), *this);
            }
            return WithILInstruction(arg, conv);
        case IL::ConversionKind::Nop:
            // no need to generate any C# code for a nop conversion
            return WithILInstruction(arg, conv);
        case IL::ConversionKind::Truncate:
            // There are three sizes involved here: A = inputType.GetSize(),
            // B = inputStackType.GetSize(), C = TargetType.GetSize() (and C < B).
            if (IL::IsSmallIntegerType(conv->TargetType))
            {
                // If the target type is a small integer type, IL will implicitly
                // sign- or zero-extend the result after the truncation back to
                // StackType.I4 (which means there's actually 3 conversions
                // involved!). We must handle truncation to small integer types
                // ourselves: our caller only sees the StackType.I4 and doesn't know
                // to truncate to the small type.
                if (TS::GetSize(&inputType) <= IL::GetSize(conv->TargetType)
                    && TS::GetSign(&inputType) == IL::GetSign(conv->TargetType))
                {
                    // There's no actual truncation involved, and the result of the
                    // Conv instruction is extended the same way as the original
                    // instruction -> we can return arg directly.
                    return WithILInstruction(arg, conv);
                }
                else
                {
                    // We need to actually truncate; *or* we need to change the sign
                    // for the remaining extension to I4.
                    goto defaultArm;  // Emit simple cast to inst.TargetType
                }
            }
            else
            {
                // For non-small integer types, we can let the whole unchecked
                // truncation get handled by our caller (using the ExpressionBuilder
                // post-condition). Case 4 (left-over extension from implicit
                // conversion) can also be handled by our caller.
                return WithILInstruction(arg, conv);
            }
        case IL::ConversionKind::Invalid:
            if (conv->InputType == IL::StackType::Unknown
                && conv->TargetType == IL::PrimitiveType::None
                && arg.Type().Kind() == TS::TypeKind::Unknown)
            {
                // Unknown -> O conversion. Our post-condition allows us to also use
                // expressions with unknown type where O is expected, so avoid
                // introducing an `(object)` cast because we're likely to cast back
                // to the same unknown type, just in a signature context where we
                // know that it's a class type.
                return WithILInstruction(arg, conv);
            }
            goto defaultArm;
        default:
        defaultArm: {
            // We need to convert to inst.TargetType, or to an equivalent type.
            TS::ITypePtr targetType;
            if (conv->TargetType
                    == TS::ToPrimitiveType(&TS::GetUnderlyingType(*context.TypeHint))
                && TS::IsNullable(*context.TypeHint) == conv->IsLifted)
            {
                targetType = TS::ITypePtr(
                    const_cast<TS::IType*>(context.TypeHint),
                    [](TS::IType*) noexcept {});
            }
            else if (conv->TargetType == IL::PrimitiveType::Ref)
            {
                // converting to unknown ref-type
                targetType = std::make_shared<TS::ByReferenceType>(
                    const_cast<TS::IType&>(compilation->FindType(TS::KnownTypeCode::Byte))
                        .shared_from_this());
            }
            else if (conv->TargetType == IL::PrimitiveType::None)
            {
                // convert to some object type (e.g. invalid I4->O conversion)
                targetType = const_cast<TS::IType&>(
                    compilation->FindType(TS::KnownTypeCode::Object))
                                 .shared_from_this();
            }
            else
            {
                targetType = GetType(TS::ToKnownTypeCode(conv->TargetType));
            }
            TranslatedExpression result =
                arg.ConvertTo(*targetType, *this, conv->CheckForOverflow);
            return WithILInstruction(result, conv);
        }
    }
}

// The C# `bool ValueMightBeOversized(ResolveResult rr, StackType stackType)`
// (ExpressionBuilder.cs lines 2388-2406): whether the resolve result computes a
// value that might be oversized for the specified stack type.
bool ExpressionBuilder::ValueMightBeOversized(const Sem::ResolveResult& rr,
                                              IL::StackType stackType)
{
    const TS::IType& inputType = TS::GetUnderlyingType(rr.Type());
    if (TS::GetSize(&inputType) <= TS::GetSize(stackType))
    {
        // The input type is smaller or equal to the stack type, it can't be an
        // oversized value.
        return false;
    }
    if (const auto* orr = dynamic_cast<const Sem::OperatorResolveResult*>(&rr))
    {
        if (stackType == IL::StackType::I
            && orr->OperatorType() == TS::ExpressionType::Subtract
            && orr->Operands().size() == 2
            && orr->Operands()[0]->Type().Kind() == TS::TypeKind::Pointer
            && orr->Operands()[1]->Type().Kind() == TS::TypeKind::Pointer)
        {
            // Even though a pointer subtraction produces a value of type long in
            // C#, the value will always fit in a native int.
            return false;
        }
    }
    // We don't have any information about the value, so it might be oversized.
    return true;
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

// The C# `internal ILFunction? ResolveLocalFunction(IMethod method)`
// (ExpressionBuilder.cs lines 278-292).
IL::ILFunction* ExpressionBuilder::ResolveLocalFunction(const TS::IMethod& method) const
{
    assert(method.IsLocalFunction());
    // The C# `method = (IMethod)((IMethod)method.MemberDefinition!).ReducedFrom!.MemberDefinition`
    // -- the hard cast of the member definition to IMethod (the member's own
    // definition view) then the unwrapped base method's own member definition.
    // The C# `ReducedFrom!` NREs when null; the port carries the message.
    const auto* methodDef = dynamic_cast<const TS::IMethod*>(method.MemberDefinition());
    assert(methodDef != nullptr);
    const TS::IMethod* reducedFrom = methodDef->ReducedFrom();
    if (reducedFrom == nullptr)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    // The C# comparison is the member-definition identity (`f.Method!.MemberDefinition
    // .Equals(method)` over the reassigned, already-unwrapped method) -- both sides
    // normalize through MemberDefinition() (the two-view discipline).
    const TS::IMember* resolved = reducedFrom->MemberDefinition();

    if (currentFunction == nullptr)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    // The C# `currentFunction.Ancestors.OfType<ILFunction>()` INCLUDES the function
    // itself (Ancestors yields this first).
    for (const IL::ILInstruction* node = currentFunction; node != nullptr; node = node->Parent)
    {
        const auto* parent = dynamic_cast<const IL::ILFunction*>(node);
        if (parent == nullptr)
            continue;
        for (const auto& f : parent->LocalFunctions)
        {
            if (f == nullptr)
                continue;
            // The C# `f.Method!` -- the C# NREs when the local function carries no
            // method (the compiler-trust operator).
            if (f->Method == nullptr)
                throw std::runtime_error("Object reference not set to an instance of an object.");
            if (f->Method->MemberDefinition() == resolved)
                return f.get();
        }
    }
    return nullptr;
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

// The C# `internal bool IsCurrentOrContainingType(ITypeDefinition? type)`
// (ExpressionBuilder.cs lines 2476-2487).
bool ExpressionBuilder::IsCurrentOrContainingType(const TS::ITypeDefinition* type) const
{
    const TS::ITypeDefinition* currentTypeDefinition =
        decompilationContext->CurrentTypeDefinition();
    while (currentTypeDefinition != nullptr)
    {
        if (type == currentTypeDefinition)
            return true;
        currentTypeDefinition = currentTypeDefinition->DeclaringTypeDefinition();
    }
    return false;
}

// The C# `internal bool IsBaseTypeOfCurrentType(ITypeDefinition? type)`
// (ExpressionBuilder.cs lines 2488-2491): the C#
// `decompilationContext.CurrentTypeDefinition.GetAllBaseTypeDefinitions()
// .Any(t => t == type)` -- the Any predicate is a pointer equality over the
// definition chain. The C# NREs when the context carries no current type
// definition (a null receiver on the extension call); the port answers
// false there (the degenerate-stub shape documented at the declaration).
bool ExpressionBuilder::IsBaseTypeOfCurrentType(const TS::ITypeDefinition* type) const
{
    const TS::ITypeDefinition* currentTypeDefinition =
        decompilationContext->CurrentTypeDefinition();
    if (currentTypeDefinition == nullptr)
        return false;
    for (const TS::ITypeDefinition* base :
         TS::GetAllBaseTypeDefinitions(currentTypeDefinition))
    {
        if (base == type)
            return true;
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
    TS::IType& hint = const_cast<TS::IType&>(TS::GetUnderlyingType(typeHint));
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
// The comparison family (the VisitComp arm + the TranslateCeq/TranslateComp helpers)

// The C# `public static BinaryOperatorType ToBinaryOperatorType(this ComparisonKind
// kind)` (IL/Instructions/Comp.cs line 66): the IL ComparisonKind -> CSharp
// BinaryOperatorType mapping, ported as a free function beside its only ported
// consumer (see the header note).
Syntax::BinaryOperatorType ToBinaryOperatorType(IL::ComparisonKind kind)
{
    switch (kind)
    {
        case IL::ComparisonKind::Equality:
            return Syntax::BinaryOperatorType::Equality;
        case IL::ComparisonKind::Inequality:
            return Syntax::BinaryOperatorType::InEquality;
        case IL::ComparisonKind::LessThan:
            return Syntax::BinaryOperatorType::LessThan;
        case IL::ComparisonKind::LessThanOrEqual:
            return Syntax::BinaryOperatorType::LessThanOrEqual;
        case IL::ComparisonKind::GreaterThan:
            return Syntax::BinaryOperatorType::GreaterThan;
        case IL::ComparisonKind::GreaterThanOrEqual:
            return Syntax::BinaryOperatorType::GreaterThanOrEqual;
    }
    // The C# default arm throws ArgumentOutOfRangeException (an unnamed value);
    // unreachable through the port's exhaustive call sites.
    throw std::out_of_range("Invalid value for ComparisonKind");
}

// The C# `protected internal override TranslatedExpression VisitComp(Comp inst,
// TranslationContext context)` (ExpressionBuilder.cs lines 871-946): the comparison
// dispatch -- the ThreeValuedLogic lifted-not arm, the Ref arm over the Unsafe
// AreSame/IsAddressLessThan intrinsics, then TranslateCeq (equality/inequality) or
// TranslateComp (the relational operators).
TranslatedExpression ExpressionBuilder::VisitComp(IL::ILInstruction* inst, TranslationContext)
{
    auto* comp = static_cast<IL::Comp*>(inst);
    if (comp->LiftingKind == IL::ComparisonLiftingKind::ThreeValuedLogic)
    {
        if (comp->Kind == IL::ComparisonKind::Equality && MatchLdcI4(comp->Right.get(), 0))
        {
            // lifted logic.not
            TS::ITypePtr boolType = const_cast<TS::IType&>(
                compilation->FindType(KnownTypeCode::Boolean)).shared_from_this();
            TS::ITypePtr targetType = TS::Create(*compilation, *boolType);
            TranslatedExpression arg =
                Translate(comp->Left.get(), targetType.get()).ConvertTo(*targetType, *this);
            auto* unary = new Syntax::UnaryOperatorExpression(
                arg.Expression(), Syntax::UnaryOperatorType::Not);
            return WithILInstruction(
                WithRR(*unary, std::make_shared<Sem::OperatorResolveResult>(
                                   targetType, TS::ExpressionType::Not,
                                   std::vector<std::shared_ptr<Sem::ResolveResult>>{
                                       SharedResolveResultAnnotation(*arg.Expression())})),
                inst);
        }
        return ErrorExpression(
            "Nullable comparisons with three-valued-logic not supported in C#");
    }
    if (comp->InputType == IL::StackType::Ref)
    {
        // Reference comparison using Unsafe intrinsics
        // (The C# Debug.Assert(!inst.IsLifted) is compiled out of the shipped assembly.)
        const char* methodName;
        bool negate;
        switch (comp->Kind)
        {
            case IL::ComparisonKind::Equality:
                methodName = "AreSame";
                negate = false;
                break;
            case IL::ComparisonKind::Inequality:
                methodName = "AreSame";
                negate = true;
                break;
            case IL::ComparisonKind::LessThan:
                methodName = "IsAddressLessThan";
                negate = false;
                break;
            case IL::ComparisonKind::LessThanOrEqual:
                methodName = "IsAddressGreaterThan";
                negate = true;
                break;
            case IL::ComparisonKind::GreaterThan:
                methodName = "IsAddressGreaterThan";
                negate = false;
                break;
            case IL::ComparisonKind::GreaterThanOrEqual:
                methodName = "IsAddressLessThan";
                negate = true;
                break;
            default:
                throw std::logic_error("Invalid ComparisonKind");
        }
        TranslatedExpression left = Translate(comp->Left.get());
        TranslatedExpression right = Translate(comp->Right.get());
        if (left.Type().Kind() != TypeKind::ByReference
            || !TS::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
                const_cast<TS::IType&>(left.Type()),
                const_cast<TS::IType&>(right.Type())))
        {
            TS::ITypePtr commonRefType = std::make_shared<TS::ByReferenceType>(
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                    .shared_from_this());
            left = left.ConvertTo(*commonRefType, *this);
            right = right.ConvertTo(*commonRefType, *this);
        }
        TS::ITypePtr boolType = const_cast<TS::IType&>(
            compilation->FindType(KnownTypeCode::Boolean)).shared_from_this();
        TranslatedExpression expr = CallUnsafeIntrinsic(
            methodName, {left.Expression(), right.Expression()}, *boolType, inst);
        if (negate)
        {
            auto* unary = new Syntax::UnaryOperatorExpression(
                expr.Expression(), Syntax::UnaryOperatorType::Not);
            expr = WithRR(WithoutILInstruction(*unary),
                          std::make_shared<Sem::ResolveResult>(boolType));
        }
        return expr;
    }
    if (IL::IsEqualityOrInequality(comp->Kind))
    {
        bool negateOutput = false;
        TranslatedExpression result = TranslateCeq(*comp, negateOutput);
        if (negateOutput)
            return WithILInstruction(LogicNot(result), inst);
        else
            return result;
    }
    else
    {
        return TranslateComp(*comp);
    }
}

// The C# `TranslatedExpression AdjustConstantExpressionToType(TranslatedExpression
// expr, IType typeHint)` (ExpressionBuilder.cs line 3861): re-render the expression
// when AdjustConstantToType re-typed the resolve result (a new node over the new
// resolve result carrying the ORIGINAL expression's IL annotations), else keep the
// original.
TranslatedExpression ExpressionBuilder::AdjustConstantExpressionToType(
    TranslatedExpression expr, TS::IType& typeHint) const
{
    std::shared_ptr<Sem::ResolveResult> newRR =
        AdjustConstantToType(std::shared_ptr<Sem::ResolveResult>(
                                 SharedResolveResultAnnotation(*expr.Expression())),
                             typeHint);
    if (newRR.get() == SharedResolveResultAnnotation(*expr.Expression()).get())
    {
        return expr;
    }
    else
    {
        return WithILInstruction(ConvertConstantValue(std::move(newRR), true),
                                 expr.ILInstructions());
    }
}

// The C# `TranslatedExpression TryUniteEqualityOperandType(TranslatedExpression
// left, TranslatedExpression right)` (ExpressionBuilder.cs line 1098): the enum-flag
// check special case renders the 0 constant as the int32 literal (not the enum
// member), else the plain constant adjustment to the right operand's type.
TranslatedExpression ExpressionBuilder::TryUniteEqualityOperandType(
    TranslatedExpression left, TranslatedExpression right) const
{
    std::shared_ptr<Sem::ResolveResult> leftRR =
        SharedResolveResultAnnotation(*left.Expression());
    // Special case for enum flag check "(enum & EnumType.SomeValue) == 0"
    // so that the const 0 value is printed as 0 integer and not as enum type,
    // e.g. EnumType.None
    if (leftRR != nullptr && leftRR->IsCompileTimeConstant()
        && TS::IsCSharpPrimitiveIntegerType(&leftRR->Type())
        && leftRR->ConstantValue().has_value()
        && leftRR->ConstantValue().type() == typeid(std::int32_t)
        && std::any_cast<std::int32_t>(leftRR->ConstantValue()) == 0
        && TS::GetUnderlyingType(right.Type()).Kind() == TypeKind::Enum)
    {
        auto* binaryExpr = dynamic_cast<Syntax::BinaryOperatorExpression*>(right.Expression());
        if (binaryExpr != nullptr
            && binaryExpr->Operator() == Syntax::BinaryOperatorType::BitwiseAnd)
        {
            return AdjustConstantExpressionToType(
                std::move(left),
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32)));
        }
    }
    return AdjustConstantExpressionToType(std::move(left),
                                          const_cast<TS::IType&>(right.Type()));
}

// The C# `bool IsSpecialCasedReferenceComparisonWithNull(TranslatedExpression lhs,
// TranslatedExpression rhs)` (ExpressionBuilder.cs line 1111): when comparing a
// string/delegate with null, the C# compiler generates a reference comparison.
bool ExpressionBuilder::IsSpecialCasedReferenceComparisonWithNull(
    TranslatedExpression lhs, TranslatedExpression rhs) const
{
    if (lhs.Type().Kind() == TypeKind::Null)
        std::swap(lhs, rhs);
    const TS::ITypeDefinition* lhsDefinition = lhs.Type().GetDefinition();
    return rhs.Type().Kind() == TypeKind::Null
           && (lhs.Type().Kind() == TypeKind::Delegate
               || TS::IsKnownType(lhs.Type(), KnownTypeCode::String))
           && lhsDefinition != decompilationContext->CurrentTypeDefinition();
}

// The C# `ExpressionWithResolveResult CreateBuiltinBinaryOperator(TranslatedExpression
// left, BinaryOperatorType type, TranslatedExpression right, bool checkForOverflow =
// false)` (ExpressionBuilder.cs line 1117): the BinaryOperatorExpression over a fresh
// OperatorResolveResult with the Linq node type of the operator.
ExpressionWithResolveResult ExpressionBuilder::CreateBuiltinBinaryOperator(
    TranslatedExpression left, Syntax::BinaryOperatorType type, TranslatedExpression right,
    bool checkForOverflow) const
{
    auto* binary = new Syntax::BinaryOperatorExpression(left.Expression(), type,
                                                        right.Expression());
    return WithRR(
        *binary,
        std::make_shared<Sem::OperatorResolveResult>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Boolean))
                .shared_from_this(),
            Syntax::BinaryOperatorExpression::GetLinqNodeType(type, checkForOverflow),
            std::vector<std::shared_ptr<Sem::ResolveResult>>{
                SharedResolveResultAnnotation(*left.Expression()),
                SharedResolveResultAnnotation(*right.Expression())}));
}

// The C# `TranslatedExpression TranslateCeq(Comp inst, out bool negateOutput)`
// (ExpressionBuilder.cs lines 947-1107): the equality/inequality comparison.
TranslatedExpression ExpressionBuilder::TranslateCeq(IL::Comp& inst, bool& negateOutput)
{
    // Translate '(e as T) == null' to '!(e is T)'.
    // This is necessary for correctness when T is a value type.
    if (inst.Left->Op == IL::OpCode::IsInst && inst.Right->Op == IL::OpCode::LdNull)
    {
        negateOutput = inst.Kind == IL::ComparisonKind::Equality;
        return IsType(*static_cast<IL::IsInst*>(inst.Left.get()));
    }
    else if (inst.Right->Op == IL::OpCode::IsInst && inst.Left->Op == IL::OpCode::LdNull)
    {
        negateOutput = inst.Kind == IL::ComparisonKind::Equality;
        return IsType(*static_cast<IL::IsInst*>(inst.Right.get()));
    }

    TranslatedExpression left = Translate(inst.Left.get());
    TranslatedExpression right = Translate(inst.Right.get());

    // Remove redundant bool comparisons
    if (TS::IsKnownType(left.Type(), KnownTypeCode::Boolean))
    {
        if (MatchLdcI4(inst.Right.get(), 0))
        {
            // 'b == 0' => '!b'
            // 'b != 0' => 'b'
            negateOutput = inst.Kind == IL::ComparisonKind::Equality;
            return left;
        }
        if (MatchLdcI4(inst.Right.get(), 1))
        {
            // 'b == 1' => 'b'
            // 'b != 1' => '!b'
            negateOutput = inst.Kind == IL::ComparisonKind::Inequality;
            return left;
        }
    }
    else if (TS::IsKnownType(right.Type(), KnownTypeCode::Boolean))
    {
        if (MatchLdcI4(inst.Left.get(), 0))
        {
            // '0 == b' => '!b'
            // '0 != b' => 'b'
            negateOutput = inst.Kind == IL::ComparisonKind::Equality;
            return right;
        }
        if (MatchLdcI4(inst.Left.get(), 1))
        {
            // '1 == b' => 'b'
            // '1 != b' => '!b'
            negateOutput = inst.Kind == IL::ComparisonKind::Inequality;
            return right;
        }
    }
    // Handle comparisons between unsafe pointers and null:
    if (left.Type().Kind() == TypeKind::Pointer && IsZeroLdc(inst.Right.get()))
    {
        negateOutput = false;
        auto nullRR = std::make_shared<Sem::ConstantResolveResult>(
            NullType(), std::any{});
        auto* nullExpr = new Syntax::NullReferenceExpression();
        right = WithRR(WithILInstruction(*nullExpr, inst.Right.get()), nullRR);
        return WithILInstruction(CreateBuiltinBinaryOperator(
                                     left, ToBinaryOperatorType(inst.Kind), right),
                                 &inst);
    }
    else if (right.Type().Kind() == TypeKind::Pointer && IsZeroLdc(inst.Left.get()))
    {
        negateOutput = false;
        auto nullRR = std::make_shared<Sem::ConstantResolveResult>(
            NullType(), std::any{});
        auto* nullExpr = new Syntax::NullReferenceExpression();
        left = WithRR(WithILInstruction(*nullExpr, inst.Left.get()), nullRR);
        return WithILInstruction(CreateBuiltinBinaryOperator(
                                     left, ToBinaryOperatorType(inst.Kind), right),
                                 &inst);
    }

    // Special case comparisons with enum and char literals
    left = TryUniteEqualityOperandType(std::move(left), right);
    right = TryUniteEqualityOperandType(std::move(right), left);

    if (IsSpecialCasedReferenceComparisonWithNull(left, right))
    {
        // When comparing a string/delegate with null, the C# compiler generates a
        // reference comparison.
        negateOutput = false;
        return WithILInstruction(CreateBuiltinBinaryOperator(
                                     left, ToBinaryOperatorType(inst.Kind), right),
                                 &inst);
    }

    auto op = ToBinaryOperatorType(inst.Kind);
    auto opResult = resolver->ResolveBinaryOperator(
        op, SharedResolveResultAnnotation(*left.Expression()),
        SharedResolveResultAnnotation(*right.Expression()));
    auto* rr = dynamic_cast<Sem::OperatorResolveResult*>(opResult.get());
    if (rr == nullptr || rr->IsError() || rr->UserDefinedOperatorMethod() != nullptr
        || TS::GetStackType(TS::GetUnderlyingType(rr->Operands()[0]->Type()))
               != inst.InputType
        || !TS::IsKnownType(rr->Type(), KnownTypeCode::Boolean))
    {
        TS::ITypePtr targetType;
        if (inst.InputType == IL::StackType::O)
        {
            targetType = const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Object))
                             .shared_from_this();
        }
        else
        {
            const TS::IType& leftUType = TS::GetUnderlyingType(left.Type());
            const TS::IType& rightUType = TS::GetUnderlyingType(right.Type());
            if (TS::GetStackType(leftUType) == inst.InputType
                && !TS::IsSmallIntegerType(&leftUType))
            {
                targetType = const_cast<TS::IType&>(leftUType).shared_from_this();
            }
            else if (TS::GetStackType(rightUType) == inst.InputType
                     && !TS::IsSmallIntegerType(&rightUType))
            {
                targetType = const_cast<TS::IType&>(rightUType).shared_from_this();
            }
            else
            {
                targetType = FindType(inst.InputType, TS::GetSign(&leftUType));
            }
        }
        if (inst.IsLifted())
        {
            targetType = TS::Create(*compilation, *targetType);
        }
        if (targetType->Equals(left.Type()))
        {
            right = right.ConvertTo(*targetType, *this);
        }
        else
        {
            left = left.ConvertTo(*targetType, *this);
        }
        opResult = resolver->ResolveBinaryOperator(
            op, SharedResolveResultAnnotation(*left.Expression()),
            SharedResolveResultAnnotation(*right.Expression()));
        rr = dynamic_cast<Sem::OperatorResolveResult*>(opResult.get());
        if (rr == nullptr || rr->IsError() || rr->UserDefinedOperatorMethod() != nullptr
            || TS::GetStackType(TS::GetUnderlyingType(rr->Operands()[0]->Type()))
                   != inst.InputType
            || !TS::IsKnownType(rr->Type(), KnownTypeCode::Boolean))
        {
            // If converting one input wasn't sufficient, convert both:
            left = left.ConvertTo(*targetType, *this);
            right = right.ConvertTo(*targetType, *this);
            rr = new Sem::OperatorResolveResult(
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Boolean))
                    .shared_from_this(),
                Syntax::BinaryOperatorExpression::GetLinqNodeType(op, false),
                std::vector<std::shared_ptr<Sem::ResolveResult>>{
                    SharedResolveResultAnnotation(*left.Expression()),
                    SharedResolveResultAnnotation(*right.Expression())});
            opResult.reset(rr);
        }
    }
    negateOutput = false;
    auto* binary = new Syntax::BinaryOperatorExpression(left.Expression(), op,
                                                        right.Expression());
    return WithILInstruction(WithRR(*binary, opResult), &inst);
}

// The C# `TranslatedExpression TranslateComp(Comp inst)` (ExpressionBuilder.cs lines
// 1122-1187): handle the Comp instruction for operators other than
// equality/inequality.
TranslatedExpression ExpressionBuilder::TranslateComp(IL::Comp& inst)
{
    auto op = ToBinaryOperatorType(inst.Kind);
    TranslatedExpression left = Translate(inst.Left.get());
    TranslatedExpression right = Translate(inst.Right.get());

    if (left.Type().Kind() == TypeKind::Pointer
        && right.Type().Kind() == TypeKind::Pointer)
    {
        return WithILInstruction(CreateBuiltinBinaryOperator(left, op, right), &inst);
    }

    left = PrepareArithmeticArgument(std::move(left), inst.InputType, inst.Sign,
                                     inst.IsLifted());
    right = PrepareArithmeticArgument(std::move(right), inst.InputType, inst.Sign,
                                      inst.IsLifted());

    // Special case comparisons with enum and char literals
    left = AdjustConstantExpressionToType(std::move(left),
                                          const_cast<TS::IType&>(right.Type()));
    right = AdjustConstantExpressionToType(std::move(right),
                                           const_cast<TS::IType&>(left.Type()));

    // attempt comparison without any additional casts
    auto opResult = resolver->ResolveBinaryOperator(
        op, SharedResolveResultAnnotation(*left.Expression()),
        SharedResolveResultAnnotation(*right.Expression()));
    auto* rr = dynamic_cast<Sem::OperatorResolveResult*>(opResult.get());
    if (rr != nullptr && !rr->IsError())
    {
        const TS::IType& compUType =
            TS::GetUnderlyingType(rr->Operands()[0]->Type());
        if (TS::GetSign(&compUType) == inst.Sign
            && TS::GetStackType(compUType) == inst.InputType)
        {
            auto* binary = new Syntax::BinaryOperatorExpression(
                left.Expression(), op, right.Expression());
            return WithILInstruction(WithRR(*binary, opResult), &inst);
        }
    }

    if (IsIntegerType(inst.InputType))
    {
        // Ensure the inputs have the correct sign:
        TS::ITypePtr inputType = FindArithmeticType(inst.InputType, inst.Sign);
        if (inst.IsLifted())
        {
            inputType = TS::Create(*compilation, *inputType);
        }
        left = left.ConvertTo(*inputType, *this);
        right = right.ConvertTo(*inputType, *this);
    }
    else if (inst.InputType == IL::StackType::O)
    {
        // Unsafe.As<object, UIntPtr>(ref left) op Unsafe.As<object, UIntPtr>(ref right)
        // TTo Unsafe.As<TFrom, TTo>(ref TFrom source)
        TS::ITypePtr integerType =
            inst.Sign == Sign::Signed
                ? const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::IntPtr))
                      .shared_from_this()
                : const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::UIntPtr))
                      .shared_from_this();
        // The C# local function `WrapInUnsafeAs(TranslatedExpression expr, ILInstruction
        // inst)` (lines 1171-1176), modeled as a lambda capturing `this` and the
        // integer type (the C# closure over `integerType`).
        auto wrapInUnsafeAs = [&](TranslatedExpression expr,
                                  IL::ILInstruction* innerInst) -> TranslatedExpression {
            TS::ITypePtr type = const_cast<TS::IType&>(expr.Type()).shared_from_this();
            expr = WrapInRef(*expr.Expression(), *type);
            return CallUnsafeIntrinsic("As", {expr.Expression()}, *integerType, innerInst,
                                       std::vector<TS::ITypePtr>{type, integerType});
        };
        left = wrapInUnsafeAs(std::move(left), inst.Left.get());
        right = wrapInUnsafeAs(std::move(right), inst.Right.get());
    }
    auto* binary = new Syntax::BinaryOperatorExpression(left.Expression(), op,
                                                        right.Expression());
    return WithILInstruction(
        WithRR(
            *binary,
            std::make_shared<Sem::OperatorResolveResult>(
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Boolean))
                    .shared_from_this(),
                Syntax::BinaryOperatorExpression::GetLinqNodeType(op, false),
                std::vector<std::shared_ptr<Sem::ResolveResult>>{
                    SharedResolveResultAnnotation(*left.Expression()),
                    SharedResolveResultAnnotation(*right.Expression())})),
        &inst);
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
        isLifted ? &const_cast<TS::IType&>(TS::GetUnderlyingType(arg.Type())) : &arg.Type();
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
// The binary-numeric family (the VisitBinaryNumericInstruction arm + helpers)

// The C# `protected internal override TranslatedExpression
// VisitBinaryNumericInstruction(BinaryNumericInstruction inst, TranslationContext
// context)` (ExpressionBuilder.cs lines 1262-1298): the arithmetic dispatch.
TranslatedExpression ExpressionBuilder::VisitBinaryNumericInstruction(
    IL::ILInstruction* inst, TranslationContext context)
{
    auto* bni = static_cast<IL::BinaryNumericInstruction*>(inst);
    switch (bni->Operator)
    {
        case IL::BinaryNumericOperator::Add:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::Add, context);
        case IL::BinaryNumericOperator::Sub:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::Subtract, context);
        case IL::BinaryNumericOperator::Mul:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::Multiply, context);
        case IL::BinaryNumericOperator::Div:
        {
            // The C# `HandlePointerSubtraction(inst) ?? HandleBinaryNumeric(...)`
            // fallback order: the pointer-subtraction attempt runs first.
            if (auto ptrResult = HandlePointerSubtraction(*bni))
                return *ptrResult;
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::Divide, context);
        }
        case IL::BinaryNumericOperator::Rem:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::Modulus, context);
        case IL::BinaryNumericOperator::BitAnd:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::BitwiseAnd,
                                       context);
        case IL::BinaryNumericOperator::BitOr:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::BitwiseOr,
                                       context);
        case IL::BinaryNumericOperator::BitXor:
            return HandleBinaryNumeric(*bni, Syntax::BinaryOperatorType::ExclusiveOr,
                                       context);
        case IL::BinaryNumericOperator::ShiftLeft:
            return HandleShift(*bni, Syntax::BinaryOperatorType::ShiftLeft, context);
        case IL::BinaryNumericOperator::ShiftRight:
            return HandleShift(*bni, Syntax::BinaryOperatorType::ShiftRight, context);
        default:
            // The C# `throw new ArgumentOutOfRangeException()` (the parameterless
            // form; the ToBinaryOperatorType std::out_of_range convention).
            throw std::out_of_range(
                "Exception of type 'System.ArgumentOutOfRangeException' was thrown.");
    }
}

// The C# `TranslatedExpression? HandlePointerArithmetic(BinaryNumericInstruction
// inst, TranslatedExpression left, TranslatedExpression right, TranslationContext
// context)` (ExpressionBuilder.cs lines 1300-1384): translates pointer arithmetic
// (ptr + int / int + ptr / ptr - int). 'ptr - ptr' is not handled here, but in
// HandlePointerSubtraction. Returns nullopt when 'inst' is not performing pointer
// arithmetic.
std::optional<TranslatedExpression> ExpressionBuilder::HandlePointerArithmetic(
    IL::BinaryNumericInstruction& inst, TranslatedExpression left,
    TranslatedExpression right, TranslationContext context)
{
    if (!(inst.Operator == IL::BinaryNumericOperator::Add
          || inst.Operator == IL::BinaryNumericOperator::Sub))
        return std::nullopt;
    if (inst.CheckForOverflow || inst.IsLifted)
        return std::nullopt;
    if (!(inst.LeftInputType == IL::StackType::I && inst.RightInputType == IL::StackType::I))
        return std::nullopt;
    const TS::PointerType* pointerType = nullptr;
    IL::ILInstruction* byteOffsetInst = nullptr;
    TranslatedExpression byteOffsetExpr;
    if (left.Type().Kind() == TypeKind::Pointer)
    {
        byteOffsetInst = inst.Right.get();
        byteOffsetExpr = std::move(right);
        pointerType = static_cast<const TS::PointerType*>(&left.Type());
    }
    else if (right.Type().Kind() == TypeKind::Pointer)
    {
        if (inst.Operator != IL::BinaryNumericOperator::Add)
            return std::nullopt;
        byteOffsetInst = inst.Left.get();
        byteOffsetExpr = std::move(left);
        pointerType = static_cast<const TS::PointerType*>(&right.Type());
    }
    else
    {
        return std::nullopt;
    }
    TranslatedExpression offsetExpressionFromTypeHint;
    bool hasOffsetExpressionFromTypeHint = false;
    if (context.TypeHint != nullptr && context.TypeHint->Kind() == TypeKind::Pointer)
    {
        // We use the type hint if one of the following is true:
        // * The current element type is a non-primitive struct.
        // * The current element type has a different size than the type hint element
        //   type.
        // This prevents the type hint from overriding in undesirable situations (eg
        // changing char* to short*).
        const auto& typeHint = static_cast<const TS::PointerType&>(*context.TypeHint);
        int elementTypeSize = GetSize(pointerType->Element().get());
        if (elementTypeSize == 0 || GetSize(typeHint.Element().get()) != elementTypeSize)
        {
            if (auto offset = GetPointerArithmeticOffset(
                    byteOffsetInst, std::move(byteOffsetExpr), typeHint.Element().get(),
                    inst.CheckForOverflow))
            {
                offsetExpressionFromTypeHint = std::move(*offset);
                hasOffsetExpressionFromTypeHint = true;
                pointerType = &typeHint;
            }
        }
    }
    TS::ITypePtr pointerTypePtr;
    TranslatedExpression offsetExpr;
    if (hasOffsetExpressionFromTypeHint)
    {
        pointerTypePtr = const_cast<TS::PointerType*>(pointerType)->shared_from_this();
        offsetExpr = std::move(offsetExpressionFromTypeHint);
    }
    else if (auto offset = GetPointerArithmeticOffset(
                 byteOffsetInst, std::move(byteOffsetExpr), pointerType->Element().get(),
                 inst.CheckForOverflow))
    {
        pointerTypePtr = const_cast<TS::PointerType*>(pointerType)->shared_from_this();
        offsetExpr = std::move(*offset);
    }
    else
    {
        // FallBackToBytePointer: the C# local function reassigns the enclosing
        // `pointerType` local before returning the integer-typed offset.
        pointerTypePtr = std::make_shared<TS::PointerType>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                .shared_from_this());
        pointerType = static_cast<const TS::PointerType*>(pointerTypePtr.get());
        offsetExpr = EnsureIntegerType(std::move(byteOffsetExpr));
    }

    Syntax::BinaryOperatorType operatorType =
        inst.Operator == IL::BinaryNumericOperator::Add
            ? Syntax::BinaryOperatorType::Add
            : Syntax::BinaryOperatorType::Subtract;
    if (left.Type().Kind() == TypeKind::Pointer)
    {
        left = left.ConvertTo(*pointerTypePtr, *this);
        right = std::move(offsetExpr);
    }
    else
    {
        left = std::move(offsetExpr);
        right = right.ConvertTo(*pointerTypePtr, *this);
    }
    auto* binary = new Syntax::BinaryOperatorExpression(left.Expression(), operatorType,
                                                        right.Expression());
    return WithILInstruction(
        WithRR(*binary, std::make_shared<Sem::OperatorResolveResult>(
                            pointerTypePtr,
                            Syntax::BinaryOperatorExpression::GetLinqNodeType(
                                operatorType, inst.CheckForOverflow),
                            std::vector<std::shared_ptr<Sem::ResolveResult>>{
                                SharedResolveResultAnnotation(*left.Expression()),
                                SharedResolveResultAnnotation(*right.Expression())})),
        &inst);
}

// The C# `TranslatedExpression? HandleManagedPointerArithmetic(
// BinaryNumericInstruction inst, TranslatedExpression left, TranslatedExpression
// right)` (ExpressionBuilder.cs lines 1386-1484): translates pointer arithmetic with
// managed pointers (ref + int / int + ref / ref - int / ref - ref).
std::optional<TranslatedExpression> ExpressionBuilder::HandleManagedPointerArithmetic(
    IL::BinaryNumericInstruction& inst, TranslatedExpression left,
    TranslatedExpression right)
{
    if (!(inst.Operator == IL::BinaryNumericOperator::Add
          || inst.Operator == IL::BinaryNumericOperator::Sub))
        return std::nullopt;
    if (inst.CheckForOverflow || inst.IsLifted)
        return std::nullopt;
    if (inst.Operator == IL::BinaryNumericOperator::Sub
        && inst.LeftInputType == IL::StackType::Ref
        && inst.RightInputType == IL::StackType::Ref)
    {
        // ref - ref => i
        // ByteOffset() expects the parameters the wrong way around, so order using
        // named arguments
        auto* target = new Syntax::NamedArgumentExpression("target", left.Expression());
        auto* origin = new Syntax::NamedArgumentExpression("origin", right.Expression());
        return CallUnsafeIntrinsic(
            "ByteOffset", {static_cast<Syntax::Expression*>(target),
                           static_cast<Syntax::Expression*>(origin)},
            compilation->FindType(KnownTypeCode::IntPtr), &inst);
    }
    if (inst.LeftInputType == IL::StackType::Ref
        && IsIntegerType(inst.RightInputType))
    {
        // ref [+-] int
        const TS::ByReferenceType* brt =
            dynamic_cast<const TS::ByReferenceType*>(&left.Type());
        TS::ITypePtr brtPtr;
        if (brt == nullptr)
        {
            // The C# `GetReferenceType` local function: a pointer type re-types to a
            // reference of its element type, everything else to a byte reference.
            const TS::PointerType* pt =
                dynamic_cast<const TS::PointerType*>(&left.Type());
            brtPtr = std::make_shared<TS::ByReferenceType>(
                pt != nullptr
                    ? pt->Element()->shared_from_this()
                    : const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                          .shared_from_this());
            brt = static_cast<const TS::ByReferenceType*>(brtPtr.get());
            left = left.ConvertTo(*brtPtr, *this);
        }
        else
        {
            brtPtr = const_cast<TS::ByReferenceType*>(brt)->shared_from_this();
        }
        std::string name =
            inst.Operator == IL::BinaryNumericOperator::Sub ? "Subtract" : "Add";
        TS::ITypePtr brtElem = brt->Element()->shared_from_this();
        IL::PointerArithmeticOffset::DetectOutcome offsetInst =
            IL::PointerArithmeticOffset::Detect(inst.Right.get(), brtElem.get(),
                                                inst.CheckForOverflow);
        if (offsetInst)
        {
            // The fixed-buffer indexer arm: the C# `settings.FixedBuffers &&
            // Add && LdFlda-of-LdFlda && IsFixedField` shape -- deferred with the
            // ConvertField/IsFixedField machinery (the CSharpAmbience deferral);
            // every other shape falls through to the element-offset render.
            if (settings->FixedBuffers()
                && inst.Operator == IL::BinaryNumericOperator::Add
                && inst.Left != nullptr && inst.Left->Op == IL::OpCode::LdFlda
                && static_cast<const IL::LdFlda*>(inst.Left.get())->Target != nullptr
                && static_cast<const IL::LdFlda*>(inst.Left.get())->Target->Op
                       == IL::OpCode::LdFlda)
            {
                throw std::logic_error(
                    "FixedBuffers pointer indexing is not supported yet (the "
                    "ConvertField/IsFixedField machinery is deferred).");
            }
            right = Translate(const_cast<IL::ILInstruction*>(offsetInst.Inst));
            right = ConvertArrayIndex(std::move(right), inst.RightInputType, true);
            return CallUnsafeIntrinsic(name, {left.Expression(), right.Expression()},
                                       *brtPtr, &inst);
        }
        else
        {
            right = ConvertArrayIndex(std::move(right), inst.RightInputType, true);
            return CallUnsafeIntrinsic(name + "ByteOffset",
                                       {left.Expression(), right.Expression()},
                                       *brtPtr, &inst);
        }
    }

    if (inst.LeftInputType == IL::StackType::I
        && inst.RightInputType == IL::StackType::Ref
        && inst.Operator == IL::BinaryNumericOperator::Add)
    {
        // int + ref
        const TS::ByReferenceType* brt =
            dynamic_cast<const TS::ByReferenceType*>(&right.Type());
        TS::ITypePtr brtPtr;
        if (brt == nullptr)
        {
            const TS::PointerType* pt =
                dynamic_cast<const TS::PointerType*>(&right.Type());
            brtPtr = std::make_shared<TS::ByReferenceType>(
                pt != nullptr
                    ? pt->Element()->shared_from_this()
                    : const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                          .shared_from_this());
            brt = static_cast<const TS::ByReferenceType*>(brtPtr.get());
            right = right.ConvertTo(*brtPtr, *this);
        }
        else
        {
            brtPtr = const_cast<TS::ByReferenceType*>(brt)->shared_from_this();
        }
        TS::ITypePtr brtElem = brt->Element()->shared_from_this();
        IL::PointerArithmeticOffset::DetectOutcome offsetInst =
            IL::PointerArithmeticOffset::Detect(inst.Left.get(), brtElem.get(),
                                                inst.CheckForOverflow);
        if (offsetInst)
        {
            left = Translate(const_cast<IL::ILInstruction*>(offsetInst.Inst));
            left = ConvertArrayIndex(std::move(left), inst.LeftInputType, true);
            auto* elementOffset = new Syntax::NamedArgumentExpression("elementOffset",
                                                                      left.Expression());
            auto* source =
                new Syntax::NamedArgumentExpression("source", right.Expression());
            return CallUnsafeIntrinsic(
                "Add",
                {static_cast<Syntax::Expression*>(elementOffset),
                 static_cast<Syntax::Expression*>(source)},
                *brtPtr, &inst);
        }
        else
        {
            left = ConvertArrayIndex(std::move(left), inst.LeftInputType, true);
            auto* byteOffset =
                new Syntax::NamedArgumentExpression("byteOffset", left.Expression());
            auto* source =
                new Syntax::NamedArgumentExpression("source", right.Expression());
            return CallUnsafeIntrinsic(
                "AddByteOffset",
                {static_cast<Syntax::Expression*>(byteOffset),
                 static_cast<Syntax::Expression*>(source)},
                *brtPtr, &inst);
        }
    }
    return std::nullopt;
}

// The C# `TranslatedExpression? HandlePointerSubtraction(BinaryNumericInstruction
// inst)` (ExpressionBuilder.cs lines 1565-1619): called for divisions, detects and
// handles the code pattern div(sub(a, b), sizeof(T)) when a,b are of type T* -- what
// the C# compiler generates for pointer subtraction.
std::optional<TranslatedExpression> ExpressionBuilder::HandlePointerSubtraction(
    IL::BinaryNumericInstruction& inst)
{
    assert(inst.Operator == IL::BinaryNumericOperator::Div);
    if (inst.CheckForOverflow || inst.LeftInputType != IL::StackType::I)
        return std::nullopt;
    if (inst.Left == nullptr || inst.Left->Op != IL::OpCode::BinaryNumericInstruction)
        return std::nullopt;
    auto* sub = static_cast<IL::BinaryNumericInstruction*>(inst.Left.get());
    if (sub->Operator != IL::BinaryNumericOperator::Sub)
        return std::nullopt;
    if (sub->CheckForOverflow)
        return std::nullopt;
    // First, attempt to parse the 'sizeof' on the RHS
    const TS::IType* elementType = nullptr;
    long long elementSize = 0;
    if (MatchLdcI(inst.Right.get(), elementSize))
    {
        // OK, might be pointer subtraction if the element size matches
        }
    else if (MatchSizeOf(UnwrapConv(inst.Right.get(), IL::ConversionKind::SignExtend),
                         elementType))
    {
        // OK, might be pointer subtraction if the element type matches
    }
    else
    {
        return std::nullopt;
    }
    TranslatedExpression left = Translate(sub->Left.get());
    TranslatedExpression right = Translate(sub->Right.get());
    TS::ITypePtr pointerType;
    auto isMatchingPointerType = [&](const TS::IType& type) {
        if (const auto* pt = dynamic_cast<const TS::PointerType*>(&type))
        {
            if (elementType != nullptr)
                return elementType->Equals(*pt->Element());
            if (elementSize > 0)
                return IL::PointerArithmeticOffset::ComputeSizeOf(pt->Element().get())
                           == elementSize;
        }
        return false;
    };
    if (isMatchingPointerType(left.Type()))
    {
        pointerType = const_cast<TS::IType&>(left.Type()).shared_from_this();
    }
    else if (isMatchingPointerType(right.Type()))
    {
        pointerType = const_cast<TS::IType&>(right.Type()).shared_from_this();
    }
    else if (elementSize == 1 && left.Type().Kind() == TypeKind::Pointer
             && right.Type().Kind() == TypeKind::Pointer)
    {
        // two pointers (neither matching), we're dividing by 1 (debug builds only),
        // -> subtract two byte pointers
        pointerType = std::make_shared<TS::PointerType>(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Byte))
                .shared_from_this());
    }
    else
    {
        // neither is a matching pointer type
        // -> not a pointer subtraction after all
        return std::nullopt;
    }
    // We got a pointer subtraction.
    left = left.ConvertTo(*pointerType, *this);
    right = right.ConvertTo(*pointerType, *this);
    auto rr = std::make_shared<Sem::OperatorResolveResult>(
        const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int64))
            .shared_from_this(),
        TS::ExpressionType::Subtract,
        std::vector<std::shared_ptr<Sem::ResolveResult>>{
            SharedResolveResultAnnotation(*left.Expression()),
            SharedResolveResultAnnotation(*right.Expression())});
    auto* binary = new Syntax::BinaryOperatorExpression(
        left.Expression(), Syntax::BinaryOperatorType::Subtract, right.Expression());
    return WithILInstruction(WithRR(*binary, rr),
                             std::vector<IL::ILInstruction*>{&inst, sub});
}

// The C# `TranslatedExpression HandleBinaryNumeric(BinaryNumericInstruction inst,
// BinaryOperatorType op, TranslationContext context)` (ExpressionBuilder.cs lines
// 1619-1746): the shared arithmetic render.
TranslatedExpression ExpressionBuilder::HandleBinaryNumeric(
    IL::BinaryNumericInstruction& inst, Syntax::BinaryOperatorType op,
    TranslationContext context)
{
    std::shared_ptr<const Resolver::CSharpResolver> resolverWithOverflowCheck =
        resolver->WithCheckForOverflow(inst.CheckForOverflow);
    bool propagateTypeHint = IsBitwise(op) && inst.LeftInputType != inst.RightInputType;
    TranslatedExpression left =
        Translate(inst.Left.get(), propagateTypeHint ? context.TypeHint : nullptr);
    TranslatedExpression right =
        Translate(inst.Right.get(), propagateTypeHint ? context.TypeHint : nullptr);

    if (inst.UnderlyingResultType() == IL::StackType::Ref)
    {
        if (auto ptrResult =
                HandleManagedPointerArithmetic(inst, std::move(left), std::move(right)))
            return *ptrResult;
    }
    if (left.Type().Kind() == TypeKind::Pointer
        || right.Type().Kind() == TypeKind::Pointer)
    {
        if (auto ptrResult = HandlePointerArithmetic(inst, std::move(left),
                                                     std::move(right), context))
            return *ptrResult;
    }

    left = PrepareArithmeticArgument(std::move(left), inst.LeftInputType, inst.Sign,
                                     inst.IsLifted);
    right = PrepareArithmeticArgument(std::move(right), inst.RightInputType, inst.Sign,
                                      inst.IsLifted);

    if (op == Syntax::BinaryOperatorType::Subtract && IsZeroLdc(inst.Left.get()))
    {
        const TS::IType& rightUType = TS::GetUnderlyingType(right.Type());
        if (IsKnownType(rightUType, KnownTypeCode::Int32)
            || IsKnownType(rightUType, KnownTypeCode::Int64)
            || IsCSharpSmallIntegerType(&rightUType)
            || rightUType.Kind() == TypeKind::NInt)
        {
            // unary minus is supported on signed int, nint and long, and on the small
            // integer types (since they promote to int)
            auto* uoe = new Syntax::UnaryOperatorExpression(
                right.Expression(), Syntax::UnaryOperatorType::Minus);
            uoe->AddAnnotation(inst.CheckForOverflow
                                   ? Transforms::CheckedAnnotationHandle()
                                   : Transforms::UncheckedAnnotationHandle());
            TS::ITypePtr resultType =
                FindArithmeticType(inst.RightInputType, TS::Sign::Signed);
            if (inst.IsLifted)
                resultType = TS::Create(*compilation, *resultType);
            return WithILInstruction(
                WithRR(*uoe, std::make_shared<Sem::OperatorResolveResult>(
                                 std::move(resultType),
                                 inst.CheckForOverflow
                                     ? TS::ExpressionType::NegateChecked
                                     : TS::ExpressionType::Negate,
                                 std::vector<std::shared_ptr<Sem::ResolveResult>>{
                                     SharedResolveResultAnnotation(*right.Expression())})),
                &inst);
        }
    }
    if (IsBitwise(op)
        && (left.Type().Kind() == TypeKind::Enum
            || right.Type().Kind() == TypeKind::Enum))
    {
        left = AdjustConstantExpressionToType(
            std::move(left), const_cast<TS::IType&>(right.Type()));
        right = AdjustConstantExpressionToType(
            std::move(right), const_cast<TS::IType&>(left.Type()));
    }

    auto opResult = resolverWithOverflowCheck->ResolveBinaryOperator(
        op, SharedResolveResultAnnotation(*left.Expression()),
        SharedResolveResultAnnotation(*right.Expression()));
    auto* rr = dynamic_cast<Sem::OperatorResolveResult*>(opResult.get());
    if (rr == nullptr || rr->IsError()
        || TS::GetStackType(TS::GetUnderlyingType(rr->Type()))
                   != inst.UnderlyingResultType()
        || !IsCompatibleWithSign(rr->Type(), inst.Sign))
    {
        // Left and right operands are incompatible, so convert them to a common type
        TS::Sign sign = inst.Sign;
        if (sign == TS::Sign::None)
        {
            // If the sign doesn't matter, try to use the same sign as expected by the
            // context
            sign = TS::GetSign(context.TypeHint);
            if (sign == TS::Sign::None)
            {
                sign = IsBitwise(op) ? TS::Sign::Unsigned : TS::Sign::Signed;
            }
        }
        TS::ITypePtr targetType = FindArithmeticType(inst.UnderlyingResultType(), sign);
        left = left.ConvertTo(
            IsNullable(left.Type()) ? *TS::Create(*compilation, *targetType) : *targetType,
            *this);
        right = right.ConvertTo(
            IsNullable(right.Type()) ? *TS::Create(*compilation, *targetType) : *targetType,
            *this);
        opResult = resolverWithOverflowCheck->ResolveBinaryOperator(
            op, SharedResolveResultAnnotation(*left.Expression()),
            SharedResolveResultAnnotation(*right.Expression()));
    }
    if (IsBitwise(op))
    {
        if (left.ResolveResult() != nullptr
            && left.ResolveResult()->ConstantValue().has_value())
        {
            long long value = std::any_cast<long long>(::ILSpy::Decompiler::Util::Cast(
                TS::TypeCode::Int64, left.ResolveResult()->ConstantValue(), false));
            left = WithILInstruction(
                ConvertConstantValue(SharedResolveResultAnnotation(*left.Expression()),
                                     false, ShouldDisplayAsHex(value, left.Type())),
                left.ILInstructions());
        }
        if (right.ResolveResult() != nullptr
            && right.ResolveResult()->ConstantValue().has_value())
        {
            long long value = std::any_cast<long long>(::ILSpy::Decompiler::Util::Cast(
                TS::TypeCode::Int64, right.ResolveResult()->ConstantValue(), false));
            right = WithILInstruction(
                ConvertConstantValue(SharedResolveResultAnnotation(*right.Expression()),
                                     false,
                                     ShouldDisplayAsHex(value, right.Type())),
                right.ILInstructions());
        }
    }
    auto* resultExpr = new Syntax::BinaryOperatorExpression(left.Expression(), op,
                                                           right.Expression());
    auto withInst = WithILInstruction(*resultExpr, &inst);
    TranslatedExpression result = WithRR(withInst, opResult);
    if (BinaryOperatorMightCheckForOverflow(op)
        && !IsFloatType(inst.UnderlyingResultType()))
    {
        if (inst.CheckForOverflow)
        {
            result.Expression()->AddAnnotation(Transforms::CheckedAnnotationHandle());
        }
        else if (opResult->IsCompileTimeConstant()
                 && ConstantBinaryOperatorOverflows(
                     op, SharedResolveResultAnnotation(*left.Expression()),
                     SharedResolveResultAnnotation(*right.Expression())))
        {
            // A compile-time constant subexpression is always evaluated in a checked
            // context, even within an (implicitly) unchecked context, so emitting it
            // bare would fail to compile with CS0220 ("operation overflows at compile
            // time in checked mode"). Force an explicit unchecked(...) wrapper, just
            // like an overflowing constant n(u)int cast. This typically happens after
            // inlining turns a runtime accumulator (e.g. a GetHashCode prime chain)
            // into a constant subexpression.
            result.Expression()->AddAnnotation(
                Transforms::ExplicitUncheckedAnnotationHandle());
        }
        else
        {
            result.Expression()->AddAnnotation(Transforms::UncheckedAnnotationHandle());
        }
    }
    return result;
}

// The C# `bool ConstantBinaryOperatorOverflows(BinaryOperatorType op, ResolveResult
// left, ResolveResult right)` (ExpressionBuilder.cs lines 1842-1847): returns true if
// the (already unchecked-resolved) constant binary operation overflows when evaluated
// in a checked context. The C# GC-reference operands port to the shared handles (the
// SharedResolveResultAnnotation call-site convention -- ResolveResult is not
// shared_from_this-able).
bool ExpressionBuilder::ConstantBinaryOperatorOverflows(
    Syntax::BinaryOperatorType op, const std::shared_ptr<Sem::ResolveResult>& left,
    const std::shared_ptr<Sem::ResolveResult>& right) const
{
    if (!left->ConstantValue().has_value() || !right->ConstantValue().has_value())
        return false;
    auto checkedResult = resolver->WithCheckForOverflow(true)->ResolveBinaryOperator(
        op, left, right);
    return checkedResult->IsError();
}

// The C# `TranslatedExpression HandleShift(BinaryNumericInstruction inst,
// BinaryOperatorType op, TranslationContext context)` (ExpressionBuilder.cs lines
// 1849-1912).
TranslatedExpression ExpressionBuilder::HandleShift(IL::BinaryNumericInstruction& inst,
                                                    Syntax::BinaryOperatorType op,
                                                    TranslationContext context)
{
    TranslatedExpression left = Translate(inst.Left.get());
    TranslatedExpression right = Translate(inst.Right.get());

    left = PrepareArithmeticArgument(std::move(left), inst.LeftInputType, inst.Sign,
                                     inst.IsLifted);

    TS::Sign sign = inst.Sign;
    const TS::IType& leftUType = TS::GetUnderlyingType(left.Type());
    bool couldUseUnsignedRightShift =
        sign == TS::Sign::Unsigned && op == Syntax::BinaryOperatorType::ShiftRight
        && settings->UnsignedRightShift()
        && (IsCSharpPrimitiveIntegerType(&leftUType)
            || IsCSharpNativeIntegerType(&leftUType))
        // If we need to cast to unsigned anyway, don't use >>> operator.
        && TS::GetSign(context.TypeHint) != TS::Sign::Unsigned;
    if (IsCSharpSmallIntegerType(&leftUType)
        && inst.UnderlyingResultType() == IL::StackType::I4
        && (sign != TS::Sign::Unsigned || couldUseUnsignedRightShift))
    {
        // With small integer types, C# will promote to int and perform signed shifts.
        // We thus don't need any casts in this case.
        // The >>> operator also promotes to signed int, but then performs an unsigned
        // shift.
        if (sign == TS::Sign::Unsigned)
        {
            op = Syntax::BinaryOperatorType::UnsignedShiftRight;
        }
    }
    else if (couldUseUnsignedRightShift
             && GetSize(&leftUType) == GetSize(inst.UnderlyingResultType())
             && TS::GetSign(&leftUType) == TS::Sign::Signed)
    {
        // Use C# 11 unsigned right shift operator. We don't need any casts in this case.
        op = Syntax::BinaryOperatorType::UnsignedShiftRight;
    }
    else
    {
        // Insert cast to target type.
        if (sign == TS::Sign::None)
        {
            // if we don't need a specific sign, prefer keeping that of the input:
            sign = TS::GetSign(&leftUType);
        }
        TS::ITypePtr targetType = FindArithmeticType(inst.UnderlyingResultType(), sign);
        if (IsNullable(left.Type()))
        {
            targetType = TS::Create(*compilation, *targetType);
        }
        left = left.ConvertTo(*targetType, *this);
    }

    // Shift operators in C# always expect type 'int' on the right-hand-side
    if (IsNullable(right.Type()))
    {
        right = right.ConvertTo(
            *TS::Create(*compilation,
                        const_cast<TS::IType&>(
                            compilation->FindType(KnownTypeCode::Int32))),
            *this);
    }
    else
    {
        right = right.ConvertTo(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32)), *this);
    }

    auto opResult = resolver->ResolveBinaryOperator(
        op, SharedResolveResultAnnotation(*left.Expression()),
        SharedResolveResultAnnotation(*right.Expression()));
    auto* binary = new Syntax::BinaryOperatorExpression(left.Expression(), op,
                                                        right.Expression());
    return WithILInstruction(WithRR(*binary, opResult), &inst);
}
// ---------------------------------------------------------------------------
// The stackalloc arms (the C# lines 517-579 and 1506-1592)

// The C# `TranslatedExpression? GetPointerArithmeticOffset(ILInstruction
// byteOffsetInst, TranslatedExpression byteOffsetExpr, IType
// pointerElementType, bool checkForOverflow, bool unwrapZeroExtension)`
// (ExpressionBuilder.cs lines 1516-1531): run PointerArithmeticOffset.Detect
// over the byte-count instruction and translate the detected element-count
// instruction. When the detected instruction is the input itself the already
// translated byteOffsetExpr is reused; otherwise the freshly translated
// expression carries the ORIGINAL byte-offset instruction as its annotation
// (the C# removes the fresh constant's own annotation first).
std::optional<TranslatedExpression> ExpressionBuilder::GetPointerArithmeticOffset(
    IL::ILInstruction* byteOffsetInst, TranslatedExpression byteOffsetExpr,
    const TS::IType* pointerElementType, bool checkForOverflow, bool unwrapZeroExtension)
{
    IL::PointerArithmeticOffset::DetectOutcome countOffsetInst =
        IL::PointerArithmeticOffset::Detect(
            byteOffsetInst, pointerElementType, checkForOverflow, unwrapZeroExtension);
    if (!countOffsetInst)
        return std::nullopt;
    if (countOffsetInst.Inst == byteOffsetInst)
    {
        return EnsureIntegerType(std::move(byteOffsetExpr));
    }
    else
    {
        TranslatedExpression expr = Translate(
            const_cast<IL::ILInstruction*>(countOffsetInst.Inst));
        // Keep original ILInstruction as annotation.
        expr.Expression()->RemoveAnnotations<ILInstructionAnnotation>();
        return EnsureIntegerType(WithILInstruction(expr, byteOffsetInst));
    }
}

// The C# `TranslatedExpression EnsureIntegerType(TranslatedExpression expr)`
// (ExpressionBuilder.cs line 1506): pointer arithmetic accepts all primitive
// integer types, but no enums etc. -- convert anything else to the arithmetic
// type of the expression's own stack type and sign.
TranslatedExpression ExpressionBuilder::EnsureIntegerType(TranslatedExpression expr)
{
    if (!TS::IsCSharpPrimitiveIntegerType(&expr.Type())
        && !TS::IsCSharpNativeIntegerType(&expr.Type()))
    {
        expr = expr.ConvertTo(
            *FindArithmeticType(TS::GetStackType(expr.Type()), TS::GetSign(&expr.Type())),
            *this);
    }
    return expr;
}

// The C# `StackAllocExpression TranslateLocAllocSpan(LocAllocSpan inst, IType
// typeHint, out IType elementType)` (ExpressionBuilder.cs lines 530-539): the
// span's Type operand supplies the element type; the count is translated and
// converted to int32.
Syntax::StackAllocExpression* ExpressionBuilder::TranslateLocAllocSpan(
    IL::LocAllocSpan* inst, const TS::IType* /*typeHint*/, TS::ITypePtr& elementType)
{
    // `inst.Type.TypeArguments[0]` -- the port's IType carries no TypeArguments
    // virtual, so the dynamic_cast-to-ParameterizedType dispatch (the
    // ResolveMethod convention) reads the span's element type. An empty type
    // argument list throws the .NET IndexOutOfRangeException message.
    auto* spanType = dynamic_cast<TS::ParameterizedType*>(inst->Type.get());
    if (spanType == nullptr || spanType->TypeArguments().empty())
        throw std::out_of_range("Index was outside the bounds of the array.");
    elementType = spanType->TypeArguments().front();
    TranslatedExpression countExpression =
        Translate(inst->Argument.get())
            .ConvertTo(const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32)), *this);
    auto* expr = new Syntax::StackAllocExpression();
    expr->Type(ConvertType(*elementType));
    expr->CountExpression(countExpression.Expression());
    return expr;
}

// The C# `StackAllocExpression TranslateLocAlloc(LocAlloc inst, IType typeHint,
// out IType elementType)` (ExpressionBuilder.cs lines 541-579): the element
// type from the count's `sizeof` operand, from the type hint's pointer element
// (via GetPointerArithmeticOffset), or the byte fallback; the count is
// converted to int32.
Syntax::StackAllocExpression* ExpressionBuilder::TranslateLocAlloc(
    IL::LocAlloc* inst, const TS::IType* typeHint, TS::ITypePtr& elementType)
{
    TranslatedExpression countExpression;
    std::shared_ptr<TS::PointerType> pointerType;
    // `inst.Argument.MatchBinaryNumericInstruction(Mul, out left, out right)`
    // `&& right.UnwrapConv(SignExtend).UnwrapConv(ZeroExtend).MatchSizeOf(out
    // sizeOfElementType)`: determine the element type from the sizeof.
    bool sizeofArm = false;
    IL::BinaryNumericInstruction* mul = nullptr;
    if (inst->Argument != nullptr
        && inst->Argument->Op == IL::OpCode::BinaryNumericInstruction)
    {
        mul = static_cast<IL::BinaryNumericInstruction*>(inst->Argument.get());
        const IL::ILInstruction* unwrappedRight =
            UnwrapConv(UnwrapConv(mul->Right.get(), IL::ConversionKind::SignExtend),
                       IL::ConversionKind::ZeroExtend);
        if (unwrappedRight != nullptr && unwrappedRight->Op == IL::OpCode::SizeOf
            && static_cast<const IL::SizeOf*>(unwrappedRight)->Type != nullptr)
        {
            elementType = static_cast<const IL::SizeOf*>(unwrappedRight)->Type;
            sizeofArm = true;
        }
    }
    if (sizeofArm)
    {
        // Determine the element type from the sizeof
        countExpression = Translate(
            const_cast<IL::ILInstruction*>(UnwrapConv(mul->Left.get(), IL::ConversionKind::ZeroExtend)));
        pointerType = std::make_shared<TS::PointerType>(elementType);
    }
    else
    {
        // Determine the element type from the expected pointer type in this context
        auto* hintPointer =
            typeHint != nullptr ? dynamic_cast<const TS::PointerType*>(typeHint) : nullptr;
        if (hintPointer != nullptr)
        {
            if (auto offset = GetPointerArithmeticOffset(
                    inst->Argument.get(), Translate(inst->Argument.get()),
                    hintPointer->Element().get(), /*checkForOverflow=*/true,
                    /*unwrapZeroExtension=*/true))
            {
                countExpression = std::move(*offset);
                elementType = hintPointer->Element();
            }
        }
        if (elementType == nullptr)
        {
            // no matching pointer type: fall back to bytes
            elementType = const_cast<TS::IType&>(
                compilation->FindType(KnownTypeCode::Byte))
                .shared_from_this();
            countExpression = Translate(inst->Argument.get());
        }
        pointerType = std::make_shared<TS::PointerType>(elementType);
    }
    countExpression =
        countExpression.ConvertTo(const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32)), *this);
    auto* expr = new Syntax::StackAllocExpression();
    expr->Type(ConvertType(*elementType));
    expr->CountExpression(countExpression.Expression());
    return expr;
}

// The C# `protected internal override TranslatedExpression VisitLocAlloc(LocAlloc
// inst, TranslationContext context)` (ExpressionBuilder.cs lines 517-522): the
// stackalloc render with a pointer resolve result over the element type.
TranslatedExpression ExpressionBuilder::VisitLocAlloc(IL::ILInstruction* inst,
                                                     TranslationContext context)
{
    auto* locAlloc = static_cast<IL::LocAlloc*>(inst);
    TS::ITypePtr elementType;
    Syntax::StackAllocExpression* expr =
        TranslateLocAlloc(locAlloc, context.TypeHint, elementType);
    return WithRR(WithILInstruction(*expr, inst),
                  std::make_shared<Sem::ResolveResult>(
                      std::make_shared<TS::PointerType>(elementType)));
}

// The C# `protected internal override TranslatedExpression VisitLocAllocSpan(
// LocAllocSpan inst, TranslationContext context)` (ExpressionBuilder.cs lines
// 523-528): the span stackalloc render with the span type as the resolve result.
TranslatedExpression ExpressionBuilder::VisitLocAllocSpan(IL::ILInstruction* inst,
                                                         TranslationContext context)
{
    auto* locAllocSpan = static_cast<IL::LocAllocSpan*>(inst);
    TS::ITypePtr elementType;
    Syntax::StackAllocExpression* expr =
        TranslateLocAllocSpan(locAllocSpan, context.TypeHint, elementType);
    return WithRR(WithILInstruction(*expr, inst),
                  std::make_shared<Sem::ResolveResult>(locAllocSpan->Type));
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// The function-pointer / virtual-delegate render arms (the VisitLdFtn family,
// ExpressionBuilder.cs lines 4774-4841 + 497) -- the ldftn/ldvirtftn
// function-pointer render and the LdVirtDelegate delegate-construction entry
// (the CallBuilder.Build(LdVirtDelegate) delegation).

// The C# `protected internal override TranslatedExpression VisitLdFtn(LdFtn
// inst, TranslationContext context)` (lines 4774-4832).
// The local NamespaceOf copy (the 'copied next to its second consumer'
// convention -- the port's IType carries no Namespace virtual): the
// IEntity/ParameterizedType/UnknownType dispatch the CallConv type's
// namespace check reads through.
namespace {
std::string NamespaceOf(const TS::IType& type)
{
    if (const auto* entity = dynamic_cast<const TS::IEntity*>(&type))
        return entity->Namespace();
    if (const auto* pt =
            dynamic_cast<const TS::ParameterizedType*>(&type))
        return pt->GenericType() ? NamespaceOf(*pt->GenericType())
                                 : std::string();
    if (const auto* unknown = dynamic_cast<const class TS::UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    return std::string();
}

// The InvocationExpression two-argument factory (the C#
// `new InvocationExpression(target, arguments)` params ctor): the port's
// generated ctor takes only the target, with the Arguments collection added
// after (the ObjectCreateExpression precedent).
Syntax::InvocationExpression* InvocationExpressionOf(
    Syntax::Expression* target, std::vector<Syntax::Expression*> arguments)
{
    auto* invocation = new Syntax::InvocationExpression(target);
    for (Syntax::Expression* arg : arguments)
        invocation->Arguments().Add(arg);
    return invocation;
}

// The non-owning shared alias over a raw resolve-result pointer (the
// CallBuilder.cpp local copied beside its consumer -- the annotation channel
// owns the handle the C# GC reference aliases).
std::shared_ptr<Sem::ResolveResult> AliasResolveResult(const Sem::ResolveResult* target)
{
    if (target == nullptr)
        return nullptr;
    return std::shared_ptr<Sem::ResolveResult>(
        const_cast<Sem::ResolveResult*>(target), [](Sem::ResolveResult*) {});
}
} // namespace

TranslatedExpression ExpressionBuilder::VisitLdFtn(IL::ILInstruction* inst,
                                                  TranslationContext context)
{
    auto* ldftn = static_cast<IL::LdFtn*>(inst);
    assert(ldftn->Method != nullptr);
    const TS::IMethod& method = *ldftn->Method;
    CallBuilder delegateBuilder(this, *compilation, settings);
    ExpressionWithResolveResult delegateRef =
        delegateBuilder.BuildMethodReference(method, /*isVirtual*/ false);
    if (!method.IsStatic())
    {
        // C# 9 function pointers don't support instance methods
        return WithILInstruction(
            WithRR(
                *InvocationExpressionOf(
                    new Syntax::IdentifierExpression("__ldftn"),
                    {delegateRef.Expression()}),
                std::make_shared<Sem::ResolveResult>(
                    std::make_shared<TS::PointerType>(
                        const_cast<TS::IType&>(
                            compilation->FindType(TS::KnownTypeCode::Void))
                            .shared_from_this()))),
            inst);
    }
    // C# 9 function pointer
    TS::SignatureCallingConvention callingConvention =
        TS::SignatureCallingConvention::Default;
    std::vector<TS::ITypePtr> customCallingConventions;
    for (const TS::IAttribute* attr : method.GetAttributes())
    {
        if (attr != nullptr
            && TS::IsKnownType(attr->AttributeType(),
                               TS::KnownAttribute::UnmanagedCallersOnly))
        {
            callingConvention = TS::SignatureCallingConvention::Unmanaged;
            customCallingConventions.clear();
            for (const TS::CustomAttributeNamedArgument& namedArg :
                 attr->NamedArguments())
            {
                if (namedArg.Name() != "CallConvs")
                    continue;
                // The C# `callingConventionsArgument.Value is
                // ImmutableArray<CustomAttributeTypedArgument<IType>> array` --
                // the boxed array of type arguments, decoded by the CallConvs
                // convention table.
                std::vector<TS::CustomAttributeTypedArgument> array;
                if (namedArg.Value().has_value()
                    && namedArg.Value().type()
                        == typeid(std::vector<TS::CustomAttributeTypedArgument>))
                {
                    array = std::any_cast<std::vector<TS::CustomAttributeTypedArgument>>(
                        namedArg.Value());
                }
                for (const TS::CustomAttributeTypedArgument& a : array)
                {
                    if (a.Type() == nullptr)
                    {
                        customCallingConventions.push_back(nullptr);
                        continue;
                    }
                    const TS::IType& type = *a.Type();
                    TS::SignatureCallingConvention found = TS::SignatureCallingConvention::Default;
                    bool matched = false;
                    if (NamespaceOf(type) == "System.Runtime.CompilerServices")
                    {
                        const std::string& name = type.Name();
                        if (name == "CallConvCdecl") { found = TS::SignatureCallingConvention::CDecl; matched = true; }
                        else if (name == "CallConvFastcall") { found = TS::SignatureCallingConvention::FastCall; matched = true; }
                        else if (name == "CallConvStdcall") { found = TS::SignatureCallingConvention::StdCall; matched = true; }
                        else if (name == "CallConvThiscall") { found = TS::SignatureCallingConvention::ThisCall; matched = true; }
                    }
                    if (matched && callingConvention == TS::SignatureCallingConvention::Unmanaged)
                        callingConvention = found;
                    else
                        customCallingConventions.push_back(a.Type());
                }
            }
            break;
        }
    }
    std::vector<TS::ITypePtr> parameterTypes;
    std::vector<TS::ReferenceKind> parameterReferenceKinds;
    for (const TS::IParameter* p : method.Parameters())
    {
        parameterTypes.push_back(
            const_cast<TS::IType&>(p->Type()).shared_from_this());
        parameterReferenceKinds.push_back(p->ReferenceKind());
    }
    auto ftp = std::make_shared<TS::FunctionPointerType>(
        callingConvention, customCallingConventions,
        const_cast<TS::IType&>(method.ReturnType()).shared_from_this(),
        method.ReturnTypeIsRefReadOnly(), parameterTypes,
        parameterReferenceKinds);
    auto* addressOfNode = new Syntax::UnaryOperatorExpression(
        delegateRef.Expression(), Syntax::UnaryOperatorType::AddressOf);
    const Sem::ResolveResult* addressOfRr =
        WithRR(*addressOfNode, std::make_shared<Sem::ResolveResult>(TS::NoType()))
            .ResolveResult();
    auto conversion = Sem::Conversions::MethodGroupConversion(
        &method, /*isVirtualMethodLookup*/ false,
        /*delegateCapturesFirstArgument*/ false);
    return WithoutILInstruction(
        WithRR(*new Syntax::CastExpression(ConvertType(*ftp), addressOfNode),
               std::make_shared<Sem::ConversionResolveResult>(
                   std::static_pointer_cast<TS::IType>(ftp),
                   AliasResolveResult(addressOfRr), std::move(conversion))));
}

// The C# `protected internal override TranslatedExpression
// VisitLdVirtFtn(LdVirtFtn inst, TranslationContext context)` (lines 4834-4841).
TranslatedExpression ExpressionBuilder::VisitLdVirtFtn(IL::ILInstruction* inst,
                                                      TranslationContext context)
{
    auto* ldvirtftn = static_cast<IL::LdVirtFtn*>(inst);
    assert(ldvirtftn->Method != nullptr);
    CallBuilder delegateBuilder(this, *compilation, settings);
    ExpressionWithResolveResult delegateRef =
        delegateBuilder.BuildMethodReference(*ldvirtftn->Method, /*isVirtual*/ true);
    // C# 9 function pointers don't support instance methods
    return WithILInstruction(
        WithRR(
            *InvocationExpressionOf(
                new Syntax::IdentifierExpression("__ldvirtftn"),
                {delegateRef.Expression()}),
            std::make_shared<Sem::ResolveResult>(std::make_shared<TS::PointerType>(
                const_cast<TS::IType&>(
                    compilation->FindType(TS::KnownTypeCode::Void))
                    .shared_from_this()))),
        inst);
}

// The C# `protected internal override TranslatedExpression
// VisitLdVirtDelegate(LdVirtDelegate inst, TranslationContext context)` (line
// 497-500).
TranslatedExpression ExpressionBuilder::VisitLdVirtDelegate(IL::ILInstruction* inst,
                                                           TranslationContext context)
{
    CallBuilder delegateBuilder(this, *compilation, settings);
    return delegateBuilder.Build(*static_cast<IL::LdVirtDelegate*>(inst));
}

// The C# `protected internal override TranslatedExpression VisitCall(Call inst,
// TranslationContext context)` / VisitCallVirt siblings (ExpressionBuilder.cs
// lines 2455-2462) and the port's dispatch for the one-Call-node model's every
// call opcode (call/callvirt/newobj). The CallBuilder render is wrapped in the
// byref direction expression when the resolved method's return type is a
// by-reference type. A call whose resolved method is null (the port's Call
// carries an optional Method the IL reader has not yet wired) degrades to the
// Default error expression, the documented unported-call degradation.
TranslatedExpression ExpressionBuilder::VisitCall(IL::ILInstruction* inst, TranslationContext context)
{
    auto* call = static_cast<IL::Call*>(inst);
    if (call->Method == nullptr)
        return Default(inst, context);
    CallBuilder callBuilder(this, *compilation, settings);
    return WrapInRef(callBuilder.Build(*call), call->Method->ReturnType());
}

// The C# `TranslatedExpression WrapInRef(TranslatedExpression expr, IType type)`
// (ExpressionBuilder.cs lines 2464-2474): the `ref <expr>` direction expression
// over a by-reference-typed call, its resolve result a ByReferenceResolveResult
// over the call's own resolve result; non-byref calls pass through unchanged.
TranslatedExpression ExpressionBuilder::WrapInRef(TranslatedExpression expr,
                                                  const TS::IType& type)
{
    if (type.Kind() == TypeKind::ByReference)
    {
        return WithRR(
            WithoutILInstruction(*new Syntax::DirectionExpression(
                Syntax::FieldDirection::Ref, expr.Expression())),
            std::make_shared<Sem::ByReferenceResolveResult>(
                AliasResolveResult(expr.ResolveResult()), TS::ReferenceKind::Ref));
    }
    return expr;
}

// The user-defined compound-assignment arm (the VisitUserDefinedCompoundAssign
// slice) and the LdObj dereference helper it shares with the later LdObj arm

// The C# `ExpressionWithResolveResult LdObj(ILInstruction address, IType
// loadType)` (ExpressionBuilder.cs lines 2894-2962). See the header comment
// for the arm contract.
ExpressionWithResolveResult ExpressionBuilder::LdObj(IL::ILInstruction* address,
                                                     const TS::IType& loadType)
{
    TS::ITypePtr addressTypeHint;
    if (address->ResultType() == IL::StackType::Ref)
        addressTypeHint = std::make_shared<TS::ByReferenceType>(TS::ITypePtr(
            const_cast<TS::IType&>(loadType).shared_from_this()));
    else
        addressTypeHint = std::make_shared<TS::PointerType>(TS::ITypePtr(
            const_cast<TS::IType&>(loadType).shared_from_this()));
    TranslatedExpression target = Translate(address, addressTypeHint.get());
    if (TS::IsCompatiblePointerTypeForMemoryAccess(
            const_cast<TS::IType&>(target.Type()),
            const_cast<TS::IType&>(loadType)))
    {
        if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(target.Expression()))
        {
            // we can dereference the managed reference by stripping away the 'ref'
            TranslatedExpression unwrapped = target.UnwrapChild(dirExpr->Expression());
            return ExpressionWithResolveResult(unwrapped.Expression(),
                                               unwrapped.ResolveResult());
        }
        if (auto* pointerType = dynamic_cast<const TS::PointerType*>(&target.Type()))
        {
            if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(target.Expression());
                uoe != nullptr && uoe->Operator() == Syntax::UnaryOperatorType::AddressOf)
            {
                // We can dereference the pointer by stripping away the '&'
                TranslatedExpression unwrapped = target.UnwrapChild(uoe->Expression());
                return ExpressionWithResolveResult(unwrapped.Expression(),
                                                   unwrapped.ResolveResult());
            }
            // Dereference the existing pointer
            return WithRR(
                *new Syntax::UnaryOperatorExpression(
                    target.Expression(), Syntax::UnaryOperatorType::Dereference),
                std::make_shared<Sem::ResolveResult>(pointerType->Element()));
        }
        // reference type behind non-DirectionExpression?
        // this case should be impossible, but we can use a pointer cast
        // just to make sure
        target = target.ConvertTo(
            *std::make_shared<TS::PointerType>(TS::ITypePtr(
                const_cast<TS::IType&>(loadType).shared_from_this())),
            *this);
        return WithRR(
            *new Syntax::UnaryOperatorExpression(
                target.Expression(), Syntax::UnaryOperatorType::Dereference),
            std::make_shared<Sem::ResolveResult>(TS::ITypePtr(
                const_cast<TS::IType&>(loadType).shared_from_this())));
    }
    // We need to cast the pointer type:
    if (dynamic_cast<Syntax::DirectionExpression*>(target.Expression()) != nullptr)
    {
        target = target.ConvertTo(
            *std::make_shared<TS::ByReferenceType>(TS::ITypePtr(
                const_cast<TS::IType&>(loadType).shared_from_this())),
            *this);
    }
    else if (!TS::IsUnmanagedType(loadType, settings->IntroduceUnmanagedConstraint()))
    {
        // Use: Unsafe.Read<T>(void*)
        target = target.ConvertTo(
            *std::make_shared<TS::PointerType>(
                const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Void))
                    .shared_from_this()),
            *this, false, true);
        TranslatedExpression intrinsic =
            CallUnsafeIntrinsic(
                "Read", {target.Expression()}, loadType, nullptr,
                std::vector<TS::ITypePtr>{
                    const_cast<TS::IType&>(loadType).shared_from_this()});
        return ExpressionWithResolveResult(intrinsic.Expression(),
                                           intrinsic.ResolveResult());
    }
    else
    {
        target = target.ConvertTo(
            *std::make_shared<TS::PointerType>(TS::ITypePtr(
                const_cast<TS::IType&>(loadType).shared_from_this())),
            *this);
    }
    if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(target.Expression()))
    {
        TranslatedExpression unwrapped = target.UnwrapChild(dirExpr->Expression());
        return ExpressionWithResolveResult(unwrapped.Expression(),
                                           unwrapped.ResolveResult());
    }
    return WithRR(
        *new Syntax::UnaryOperatorExpression(target.Expression(),
                                             Syntax::UnaryOperatorType::Dereference),
        std::make_shared<Sem::ResolveResult>(TS::ITypePtr(
            const_cast<TS::IType&>(loadType).shared_from_this())));
}

// The C# `static bool IsCompatibleWithSwitch(IType type)` (the local function
// inside GetCSharpSwitchGoverningType, ExpressionBuilder.cs lines 4131-4146): the
// primitive set a C# switch statement/expression can govern on, after the nullable
// unwrap.
static bool IsCompatibleWithSwitch(const TS::IType& type)
{
    const TS::IType& underlying = TS::GetUnderlyingType(type);
    return TS::IsKnownType(underlying, TS::KnownTypeCode::Boolean)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::SByte)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::Byte)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::Int16)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::UInt16)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::Int32)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::UInt32)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::Int64)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::UInt64)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::Char)
        || TS::IsKnownType(underlying, TS::KnownTypeCode::String);
}

// The C# `static bool IsImplicitConversionOperator(IMethod operatorMethod)` (the
// local function inside GetCSharpSwitchGoverningType): the single-parameter
// op_Implicit operator filter for the GetMethods call.
static bool IsImplicitConversionOperator(const TS::IMethod* operatorMethod)
{
    if (operatorMethod == nullptr || !operatorMethod->IsOperator())
        return false;
    if (operatorMethod->Name() != "op_Implicit")
        return false;
    return operatorMethod->Parameters().size() == 1;
}

// The C# `static IType GetCSharpSwitchGoverningType(IType type)` (the static
// helper inside TranslateSwitchValue, ExpressionBuilder.cs lines 4107-4130): see
// the header comment for the contract.
const TS::IType* ExpressionBuilder::GetCSharpSwitchGoverningType(const TS::IType& type) const
{
    if (IsCompatibleWithSwitch(type))
        return &type;
    // The C# `type.GetMethods(IsImplicitConversionOperator).Where(m =>
    // IsCompatibleWithSwitch(m.ReturnType)).ToArray()`.
    std::vector<const TS::IMethod*> applicableImplicitConversionOperators;
    for (const TS::IMethod* m : type.GetMethods(&IsImplicitConversionOperator))
    {
        if (IsCompatibleWithSwitch(m->ReturnType()))
            applicableImplicitConversionOperators.push_back(m);
    }
    if (applicableImplicitConversionOperators.size() != 1)
        return &type;
    return &applicableImplicitConversionOperators[0]->ReturnType();
}

// The C# `internal (TranslatedExpression, IType, StringToInt?)
// TranslateSwitchValue(SwitchInstruction inst, bool isExpressionContext)`
// (ExpressionBuilder.cs lines 4059-4106): see the header comment for the contract.
ExpressionBuilder::SwitchValueTranslation ExpressionBuilder::TranslateSwitchValue(
    IL::SwitchInstruction& inst, bool isExpressionContext)
{
    TranslatedExpression value;
    const TS::IType* governingType = nullptr;
    IL::StringToInt* strToInt = dynamic_cast<IL::StringToInt*>(inst.Value.get());
    if (strToInt != nullptr)
    {
        value = Translate(strToInt->Argument.get());
        governingType = strToInt->ExpectedType
                            ? strToInt->ExpectedType.get()
                            : &compilation->FindType(TS::KnownTypeCode::String);
    }
    else
    {
        value = Translate(inst.Value.get());
        governingType = inst.Type ? inst.Type.get() : &value.Type();

        // validate the governing type
        if (inst.Value->ResultType() == IL::StackType::I8)
        {
            if (TS::GetStackType(*governingType) != IL::StackType::I8)
            {
                auto reFound = FindType(IL::StackType::I8, TS::GetSign(governingType));
                governingType = reFound.get();
            }
        }
        else if (inst.Value->ResultType() == IL::StackType::I4)
        {
            if (TS::GetStackType(*governingType) != IL::StackType::I4)
            {
                auto reFound = FindType(IL::StackType::I4, TS::GetSign(governingType));
                governingType = reFound.get();
            }
            if (TS::IsSmallIntegerType(governingType))
            {
                IL::SwitchSection* defaultSection = inst.GetDefaultSection();
                int bits = 8 * TS::GetSize(governingType);
                int minValue = TS::GetSign(governingType) == TS::Sign::Unsigned
                                  ? 0
                                  : -(1 << (bits - 1));
                int maxValue = TS::GetSign(governingType) == TS::Sign::Unsigned
                                  ? (1 << bits) - 1
                                  : (1 << (bits - 1)) - 1;
                for (auto& section : inst.Sections)
                {
                    if (section.get() == defaultSection)
                        continue;
                    Util::LongInterval interval = section->Labels.ContainingInterval();
                    if (interval.Start < minValue || interval.InclusiveEnd() > maxValue)
                    {
                        // governing type is too small to hold all case values
                        auto reFound = FindType(IL::StackType::I4, TS::Sign::Signed);
                        governingType = reFound.get();
                        break;
                    }
                }
            }
        }
        else
        {
            // The C# Debug.Asserts (an O-valued switch is lifted with a matching
            // Type); the port keeps the C# behavior of falling through with the
            // governing type as-is.
        }
    }

    if (isExpressionContext)
    {
        // switch-expression does not support implicit conversions
        value = value.ConvertTo(const_cast<TS::IType&>(*governingType), *this,
                                /*checkForOverflow*/ false,
                                /*allowImplicitConversion*/ false);
    }
    else
    {
        value = value.ConvertTo(const_cast<TS::IType&>(*governingType), *this,
                                /*checkForOverflow*/ false,
                                /*allowImplicitConversion*/ true);

        const TS::IType* csharpGoverningType = GetCSharpSwitchGoverningType(value.Type());
        if (!csharpGoverningType->Equals(*governingType))
        {
            value = value.ConvertTo(const_cast<TS::IType&>(*governingType), *this,
                                    /*checkForOverflow*/ false,
                                    /*allowImplicitConversion*/ false);
        }
    }

    const TS::IType* caseType =
        strToInt != nullptr ? &compilation->FindType(TS::KnownTypeCode::String)
                            : governingType;

    return SwitchValueTranslation{std::move(value), caseType, strToInt};
}

// The C# `protected internal override TranslatedExpression
// VisitUserDefinedCompoundAssign(UserDefinedCompoundAssign inst, TranslationContext
// context)` (ExpressionBuilder.cs lines 1912-1998). See the header comment for
// the arm contract.
TranslatedExpression ExpressionBuilder::VisitUserDefinedCompoundAssign(
    IL::ILInstruction* inst, TranslationContext context)
{
    auto* compoundAssign = static_cast<IL::UserDefinedCompoundAssign*>(inst);
    if (!compoundAssign->Method)
    {
        // The C# node's `readonly IMethod Method` is never null; the port's
        // seed string-stand-in construction form (TransformAssignment's
        // Call-based folds) carries no resolved method, and the C# Visit
        // consumes the method's parameters/return type unconditionally -- a
        // loud deferral is the only faithful behavior for the stand-in.
        throw std::logic_error(
            "VisitUserDefinedCompoundAssign: the seed string stand-in node has "
            "no resolved IMethod; the resolved-method construction form is "
            "required for the C# back end");
    }
    const TS::IMethod& method = *compoundAssign->Method;
    bool isSpanBasedStringConcat = CallBuilder::IsSpanBasedStringConcat(method);
    const TS::IType* loadType;
    if (isSpanBasedStringConcat)
    {
        loadType = &compilation->FindType(KnownTypeCode::String);
    }
    else
    {
        // The C# `inst.Method.Parameters[0].Type`.
        const auto& parameters = method.Parameters();
        if (parameters.empty() || parameters[0] == nullptr)
            throw std::out_of_range("VisitUserDefinedCompoundAssign: the operator "
                                    "method has no first parameter");
        loadType = &parameters[0]->Type();
    }

    ExpressionWithResolveResult target;
    if (compoundAssign->TargetKind == IL::CompoundTargetKind::Address)
    {
        target = LdObj(compoundAssign->Target.get(), *loadType);
    }
    else
    {
        TranslatedExpression translated = Translate(compoundAssign->Target.get(), loadType);
        target = ExpressionWithResolveResult(translated.Expression(),
                                             translated.ResolveResult());
    }
    auto opType = Syntax::OperatorDeclaration::GetOperatorType(method.Name());
    if (opType.has_value() && Syntax::OperatorDeclaration::IsChecked(*opType))
    {
        target.Expression()->AddAnnotation(Transforms::CheckedAnnotationHandle());
    }
    else if (Transforms::ReplaceMethodCallsWithOperators::HasCheckedEquivalent(method))
    {
        target.Expression()->AddAnnotation(Transforms::UncheckedAnnotationHandle());
    }
    if (IL::UserDefinedCompoundAssign::IsStringConcat(method))
    {
        assert(method.Parameters().size() == 2
               && "string.Concat compound assign must have two parameters");
        Syntax::Expression* valueExpr = nullptr;
        std::shared_ptr<Sem::ResolveResult> valueResolveResult;
        auto* newObj = dynamic_cast<IL::Call*>(compoundAssign->Value.get());
        if (isSpanBasedStringConcat && newObj != nullptr && newObj->IsNewObj
            && newObj->Arguments.size() == 1
            && newObj->Arguments[0]->Op == IL::OpCode::AddressOf
            && newObj->Arguments[0]->ChildCount() == 1)
        {
            // `inst.Value is NewObj { Arguments: [AddressOf addressOf] }` --
            // the port has no dedicated AddressOf node class, so the value's
            // single child (the wrapped computation) is read generically.
            const TS::IType& charType = compilation->FindType(KnownTypeCode::Char);
            TranslatedExpression value = Translate(
                newObj->Arguments[0]->GetChild(0),
                &charType).ConvertTo(const_cast<TS::IType&>(charType), *this);
            valueExpr = value.Expression();
            valueResolveResult = SharedResolveResultAnnotation(*value.Expression());
        }
        else
        {
            const auto& concatParams = method.Parameters();
            if (concatParams.size() < 2 || concatParams[1] == nullptr)
                throw std::out_of_range("VisitUserDefinedCompoundAssign: the "
                                        "string.Concat method has no second parameter");
            TranslatedExpression value =
                Translate(compoundAssign->Value.get())
                    .ConvertTo(const_cast<TS::IType&>(concatParams[1]->Type()), *this,
                               false, true);
            valueExpr = Syntax::Detach(Transforms::ReplaceMethodCallsWithOperators::
                                           RemoveRedundantToStringInConcat(
                                               value.Expression(), method, true));
            valueResolveResult = SharedResolveResultAnnotation(*value.Expression());
        }
        auto* assignment = new Syntax::AssignmentExpression(
            target.Expression(), Syntax::AssignmentOperatorType::Add, valueExpr);
        return WithILInstruction(
            WithRR(*assignment,
                   std::make_shared<Sem::OperatorResolveResult>(
                       const_cast<TS::IType&>(method.ReturnType()).shared_from_this(),
                       TS::ExpressionType::AddAssign, &method, compoundAssign->IsLifted,
                       std::vector<std::shared_ptr<Sem::ResolveResult>>{
                           SharedResolveResultAnnotation(*target.Expression()),
                           valueResolveResult})),
            inst);
    }
    if (method.Parameters().size() == 2)
    {
        const auto& parameters = method.Parameters();
        TranslatedExpression value =
            Translate(compoundAssign->Value.get())
                .ConvertTo(const_cast<TS::IType&>(parameters[1]->Type()), *this);
        std::optional<Syntax::AssignmentOperatorType> op =
            GetAssignmentOperatorTypeFromMetadataName(method.Name(), *settings);
        assert(op.has_value() && "the operator name must map to an assignment operator");

        auto* assignment = new Syntax::AssignmentExpression(
            target.Expression(), *op, value.Expression());
        return WithILInstruction(
            WithRR(*assignment,
                   std::make_shared<Sem::OperatorResolveResult>(
                       const_cast<TS::IType&>(method.ReturnType()).shared_from_this(),
                       Syntax::AssignmentExpression::GetLinqNodeType(*op, false), &method,
                       compoundAssign->IsLifted,
                       std::vector<std::shared_ptr<Sem::ResolveResult>>{
                           SharedResolveResultAnnotation(*target.Expression()),
                           SharedResolveResultAnnotation(*value.Expression())})),
            inst);
    }
    std::optional<Syntax::UnaryOperatorType> op = GetUnaryOperatorTypeFromMetadataName(
        method.Name(),
        compoundAssign->EvalMode == IL::CompoundEvalMode::EvaluatesToOldValue);
    assert(op.has_value() && "the operator name must map to a unary operator");

    auto* unary = new Syntax::UnaryOperatorExpression(target.Expression(), *op);
    return WithILInstruction(
        WithRR(*unary,
               std::make_shared<Sem::OperatorResolveResult>(
                   const_cast<TS::IType&>(method.ReturnType()).shared_from_this(),
                   Syntax::UnaryOperatorExpression::GetLinqNodeType(*op, false), &method,
                   compoundAssign->IsLifted,
                   std::vector<std::shared_ptr<Sem::ResolveResult>>{
                       SharedResolveResultAnnotation(*target.Expression())})),
        inst);
}

// ---------------------------------------------------------------------------
// The numeric compound-assignment family (VisitNumericCompoundAssign + the
// HandleCompoundAssignment / ConvertValue / HandleCompoundShift helpers)

// The C# `protected internal override TranslatedExpression
// VisitNumericCompoundAssign(NumericCompoundAssign inst, TranslationContext
// context)` (ExpressionBuilder.cs lines 2032-2073): the numeric compound-assignment
// dispatch. The eight arithmetic/bitwise operators route through
// HandleCompoundAssignment; the two shift operators route through
// HandleCompoundShift, with the ShiftRight gates preserving the C# `>>>=` spelling
// when the sign/small-integer combination allows it (the setting gates both).
TranslatedExpression ExpressionBuilder::VisitNumericCompoundAssign(
    IL::ILInstruction* inst, TranslationContext context)
{
    auto* compoundAssign = static_cast<IL::NumericCompoundAssign*>(inst);
    // The C# `inst.Type` is a non-nullable IType reference (the node ctor takes it);
    // the port's foundation ctor admits null until the transform lands, so the Visit
    // asserts the resolved-node contract.
    assert(compoundAssign->Type != nullptr
           && "NumericCompoundAssign.Type is non-nullable in C#");
    switch (compoundAssign->Operator)
    {
        case IL::BinaryNumericOperator::Add:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::Add);
        case IL::BinaryNumericOperator::Sub:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::Subtract);
        case IL::BinaryNumericOperator::Mul:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::Multiply);
        case IL::BinaryNumericOperator::Div:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::Divide);
        case IL::BinaryNumericOperator::Rem:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::Modulus);
        case IL::BinaryNumericOperator::BitAnd:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::BitwiseAnd);
        case IL::BinaryNumericOperator::BitOr:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::BitwiseOr);
        case IL::BinaryNumericOperator::BitXor:
            return HandleCompoundAssignment(*compoundAssign,
                                            Syntax::AssignmentOperatorType::ExclusiveOr);
        case IL::BinaryNumericOperator::ShiftLeft:
            return HandleCompoundShift(*compoundAssign,
                                       Syntax::AssignmentOperatorType::ShiftLeft);
        case IL::BinaryNumericOperator::ShiftRight:
            if (compoundAssign->Sign == Sign::Unsigned
                && TS::GetSign(compoundAssign->Type.get()) == Sign::Signed)
            {
                assert(settings->UnsignedRightShift());
                return HandleCompoundShift(*compoundAssign,
                                           Syntax::AssignmentOperatorType::UnsignedShiftRight);
            }
            else if (compoundAssign->Sign == Sign::Unsigned
                     && TS::IsCSharpSmallIntegerType(compoundAssign->Type.get())
                     && settings->UnsignedRightShift())
            {
                // For small unsigned integer types promoted to signed int, the sign bit
                // will be zero, so there is no difference between signed and unsigned
                // shift. However the IL still indicates which C# operator was used, so
                // preserve that if the setting allows us to.
                return HandleCompoundShift(*compoundAssign,
                                           Syntax::AssignmentOperatorType::UnsignedShiftRight);
            }
            else
            {
                return HandleCompoundShift(*compoundAssign,
                                           Syntax::AssignmentOperatorType::ShiftRight);
            }
        default:
            // The C# `throw new ArgumentOutOfRangeException()` (the parameterless
            // form; the ToBinaryOperatorType std::out_of_range convention).
            throw std::out_of_range(
                "Exception of type 'System.ArgumentOutOfRangeException' was thrown.");
    }
}

// The C# `TranslatedExpression HandleCompoundAssignment(NumericCompoundAssign
// inst, AssignmentOperatorType op)` (ExpressionBuilder.cs lines 2075-2165).
TranslatedExpression ExpressionBuilder::HandleCompoundAssignment(
    const IL::NumericCompoundAssign& inst, Syntax::AssignmentOperatorType op)
{
    assert(inst.Type != nullptr && "NumericCompoundAssign.Type is non-nullable in C#");
    ExpressionWithResolveResult target;
    if (inst.TargetKind == IL::CompoundTargetKind::Address)
    {
        target = LdObj(inst.Target.get(), *inst.Type);
    }
    else
    {
        TranslatedExpression translated = Translate(inst.Target.get(), inst.Type.get());
        target = ExpressionWithResolveResult(translated.Expression(),
                                             translated.ResolveResult());
    }

    TranslatedExpression resultExpr;
    if (inst.EvalMode == IL::CompoundEvalMode::EvaluatesToOldValue)
    {
        assert((op == Syntax::AssignmentOperatorType::Add
                || op == Syntax::AssignmentOperatorType::Subtract)
               && "EvaluatesToOldValue only supports Add/Subtract");
        // The C# #if DEBUG block: the pointer arm re-detects the element offset and
        // asserts the ldc.i4 1 shape; the constant arm asserts the constant 1 (the
        // integer or float post-increment shape). Debug-only: the Release build the
        // real engine ships compiles it out, so the port's Release build is neutral.
        if (const TS::PointerType* ptrType =
                dynamic_cast<const TS::PointerType*>(&target.Type()))
        {
            IL::PointerArithmeticOffset::DetectOutcome instValue =
                IL::PointerArithmeticOffset::Detect(inst.Value.get(), ptrType->Element().get(),
                                                    inst.CheckForOverflow);
            assert(instValue.Inst != nullptr);
            std::int64_t pointerOffset = 0;
            assert(MatchLdcI(instValue.Inst, pointerOffset) && pointerOffset == 1);
        }
        else
        {
            assert(MatchConstantOne(inst.Value.get()));
        }
        Syntax::UnaryOperatorType unary;
        TS::ExpressionType exprType;
        if (op == Syntax::AssignmentOperatorType::Add)
        {
            unary = Syntax::UnaryOperatorType::PostIncrement;
            exprType = TS::ExpressionType::PostIncrementAssign;
        }
        else
        {
            unary = Syntax::UnaryOperatorType::PostDecrement;
            exprType = TS::ExpressionType::PostDecrementAssign;
        }
        auto* unaryExpr = new Syntax::UnaryOperatorExpression(target.Expression(), unary);
        resultExpr = WithILInstruction(
            WithRR(*unaryExpr,
                   std::make_shared<Sem::OperatorResolveResult>(
                       const_cast<TS::IType&>(target.Type()).shared_from_this(), exprType,
                       std::vector<std::shared_ptr<Sem::ResolveResult>>{
                           SharedResolveResultAnnotation(*target.Expression())})),
            const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
    }
    else
    {
        TranslatedExpression value = Translate(inst.Value.get());
        value = PrepareArithmeticArgument(std::move(value), inst.RightInputType, inst.Sign,
                                          inst.IsLifted);
        switch (op)
        {
            case Syntax::AssignmentOperatorType::Add:
            case Syntax::AssignmentOperatorType::Subtract:
                if (target.Type().Kind() == TypeKind::Pointer)
                {
                    auto pao = GetPointerArithmeticOffset(
                        inst.Value.get(), value,
                        dynamic_cast<const TS::PointerType&>(target.Type()).Element().get(),
                        inst.CheckForOverflow);
                    if (pao.has_value())
                    {
                        value = *pao;
                    }
                    else
                    {
                        value.Expression()->AddTrailingTrivia(new Syntax::Comment(
                            "ILSpy Error: GetPointerArithmeticOffset() failed",
                            Syntax::CommentType::MultiLine));
                    }
                }
                else
                {
                    const TS::IType& targetType =
                        *TS::GetEnumUnderlyingType(&TS::GetUnderlyingType(target.Type()));
                    value = ConvertValue(std::move(value),
                                         const_cast<TS::IType&>(targetType),
                                         inst.CheckForOverflow);
                }
                break;
            case Syntax::AssignmentOperatorType::Multiply:
            case Syntax::AssignmentOperatorType::Divide:
            case Syntax::AssignmentOperatorType::Modulus:
            case Syntax::AssignmentOperatorType::BitwiseAnd:
            case Syntax::AssignmentOperatorType::BitwiseOr:
            case Syntax::AssignmentOperatorType::ExclusiveOr:
            {
                TS::IType& targetType =
                    const_cast<TS::IType&>(TS::GetUnderlyingType(target.Type()));
                value = ConvertValue(std::move(value), targetType, inst.CheckForOverflow);
                break;
            }
            default:
                break;
        }
        auto* assignment = new Syntax::AssignmentExpression(
            target.Expression(), op, value.Expression());
        resultExpr = WithILInstruction(
            WithRR(*assignment,
                   std::make_shared<Sem::OperatorResolveResult>(
                       const_cast<TS::IType&>(target.Type()).shared_from_this(),
                       Syntax::AssignmentExpression::GetLinqNodeType(op,
                                                                     inst.CheckForOverflow),
                       std::vector<std::shared_ptr<Sem::ResolveResult>>{
                           SharedResolveResultAnnotation(*target.Expression()),
                           SharedResolveResultAnnotation(*value.Expression())})),
            const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
    }
    if (AssignmentOperatorMightCheckForOverflow(op)
        && !IsFloatType(inst.UnderlyingResultType()))
    {
        if (inst.CheckForOverflow)
            resultExpr.Expression()->AddAnnotation(Transforms::CheckedAnnotationHandle());
        else
            resultExpr.Expression()->AddAnnotation(
                Transforms::UncheckedAnnotationHandle());
    }
    return resultExpr;
}

// The C# `TranslatedExpression ConvertValue(TranslatedExpression value, IType
// targetType)` -- the local function inside HandleCompoundAssignment
// (ExpressionBuilder.cs lines 2155-2164).
TranslatedExpression ExpressionBuilder::ConvertValue(
    TranslatedExpression value, TS::IType& inTargetType, bool checkForOverflow)
{
    TS::ITypePtr targetType =
        const_cast<TS::IType&>(inTargetType).shared_from_this();
    bool allowImplicitConversion = true;
    if (TS::GetStackType(*targetType) == IL::StackType::I)
    {
        // Force explicit cast for (U)IntPtr, keep allowing implicit conversion only
        // for n(u)int
        allowImplicitConversion = TS::IsCSharpNativeIntegerType(targetType.get());
        targetType = TS::GetSign(targetType.get()) == Sign::Unsigned ? TS::NUInt() : TS::NInt();
    }
    if (TS::IsNullable(value.Type()))
    {
        targetType = TS::Create(*compilation, *targetType);
    }
    return value.ConvertTo(*targetType, *this, checkForOverflow, allowImplicitConversion);
}

// The C# `TranslatedExpression HandleCompoundShift(NumericCompoundAssign inst,
// AssignmentOperatorType op)` (ExpressionBuilder.cs lines 2167-2188).
TranslatedExpression ExpressionBuilder::HandleCompoundShift(
    const IL::NumericCompoundAssign& inst, Syntax::AssignmentOperatorType op)
{
    assert(inst.EvalMode == IL::CompoundEvalMode::EvaluatesToNewValue);
    assert(inst.Type != nullptr && "NumericCompoundAssign.Type is non-nullable in C#");
    ExpressionWithResolveResult target;
    if (inst.TargetKind == IL::CompoundTargetKind::Address)
    {
        target = LdObj(inst.Target.get(), *inst.Type);
    }
    else
    {
        TranslatedExpression translated = Translate(inst.Target.get(), inst.Type.get());
        target = ExpressionWithResolveResult(translated.Expression(),
                                             translated.ResolveResult());
    }
    TranslatedExpression value = Translate(inst.Value.get());

    // Shift operators in C# always expect type 'int' on the right-hand-side
    if (TS::IsNullable(value.Type()))
    {
        value = value.ConvertTo(
            *TS::Create(*compilation, const_cast<TS::IType&>(compilation->FindType(
                                                           KnownTypeCode::Int32))),
            *this);
    }
    else
    {
        value = value.ConvertTo(
            const_cast<TS::IType&>(compilation->FindType(KnownTypeCode::Int32)), *this);
    }

    auto* assignment = new Syntax::AssignmentExpression(
        target.Expression(), op, value.Expression());
    return WithILInstruction(
        WithRR(*assignment,
               resolver->ResolveAssignment(
                   op, SharedResolveResultAnnotation(*target.Expression()),
                   SharedResolveResultAnnotation(*value.Expression()))),
        const_cast<IL::ILInstruction*>(static_cast<const IL::ILInstruction*>(&inst)));
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
    return sign == Sign::None || GetSign(&TS::GetUnderlyingType(type)) == sign;
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
    // The C# `brrr` local keeps the ByReferenceResolveResult alive while the
    // annotations are replaced (the GC reference); the port must capture the
    // owning handle BEFORE the RemoveAnnotations destroys the annotation that
    // owns the result -- reading `brrr` after the remove is use-after-free.
    std::shared_ptr<Sem::ResolveResult> elementResultShared =
        brrr->ElementResult() != nullptr ? brrr->ElementResultShared() : nullptr;
    dirExpr->RemoveAnnotations<Sem::ByReferenceResolveResult>();
    std::shared_ptr<Sem::ResolveResult> newBrrr;
    if (elementResultShared == nullptr)
        newBrrr = std::make_shared<Sem::ByReferenceResolveResult>(
            const_cast<TS::IType&>(brrr->ElementType()).shared_from_this(), kind);
    else
        newBrrr = std::make_shared<Sem::ByReferenceResolveResult>(
            std::move(elementResultShared), kind);
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
