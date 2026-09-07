// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// The out-of-line half of TranslatedExpression.hpp (the wrapper-struct ctors and the
// UnwrapChild walk). See the header for the port conventions.

#include "Decompiler/CSharp/TranslatedExpression.hpp"

#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"  // WithCheckForOverflow (the ResolveCast wrap)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"  // IdentityConversion / IsBoxingConversionOrInvolvingTypeParameter (the free functions)
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ConditionalExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThrowExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TupleExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"  // LdFlda (the IsFixedVariable scan)
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/TupleResolveResult.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <any>
#include <stdexcept>
#include <typeindex>

namespace ILSpy::Decompiler::CSharp {

std::vector<IL::ILInstruction*> GetILInstructions(const Syntax::AstNode& node) {
    std::vector<IL::ILInstruction*> result;
    for (Syntax::AnnotationBase* annotation : node.Annotations()) {
        if (auto* holder =
                dynamic_cast<ILInstructionAnnotation*>(annotation)) {
            result.push_back(holder->Instruction);
        }
    }
    return result;
}

ExpressionWithILInstruction::ExpressionWithILInstruction(Syntax::Expression* expression)
    : expression_(expression) {
    assert(expression_ != nullptr &&
           "ExpressionWithILInstruction: expression must not be null");
}

ExpressionWithResolveResult::ExpressionWithResolveResult(Syntax::Expression* expression)
    : expression_(expression),
      resolveResult_(expression->Annotation<Sem::ResolveResult>()) {
    assert(expression_ != nullptr &&
           "ExpressionWithResolveResult: expression must not be null");
    if (resolveResult_ == nullptr)
        resolveResult_ = &Sem::ErrorResolveResult::UnknownError();
}

ExpressionWithResolveResult::ExpressionWithResolveResult(
    Syntax::Expression* expression, const Sem::ResolveResult* resolveResult)
    : expression_(expression),
      resolveResult_(resolveResult) {
    assert(expression_ != nullptr &&
           "ExpressionWithResolveResult: expression must not be null");
    assert(resolveResult_ != nullptr &&
           "ExpressionWithResolveResult: resolveResult must not be null");
    assert(expression_->Annotation<Sem::ResolveResult>() == resolveResult_ &&
           "ExpressionWithResolveResult: the resolve result must be the expression's "
           "annotation");
}

TranslatedExpression::TranslatedExpression(Syntax::Expression* expression)
    : expression_(expression),
      resolveResult_(expression->Annotation<Sem::ResolveResult>()) {
    assert(expression_ != nullptr &&
           "TranslatedExpression: expression must not be null");
    if (resolveResult_ == nullptr)
        resolveResult_ = &Sem::ErrorResolveResult::UnknownError();
}

TranslatedExpression::TranslatedExpression(
    Syntax::Expression* expression, const Sem::ResolveResult* resolveResult)
    : expression_(expression),
      resolveResult_(resolveResult) {
    assert(expression_ != nullptr &&
           "TranslatedExpression: expression must not be null");
    assert(resolveResult_ != nullptr &&
           "TranslatedExpression: resolveResult must not be null");
    assert(expression_->Annotation<Sem::ResolveResult>() == resolveResult_ &&
           "TranslatedExpression: the resolve result must be the expression's "
           "annotation");
}

TranslatedExpression TranslatedExpression::UnwrapChild(
    Syntax::Expression* descendant) const {
    if (descendant == expression_)
        return *this;
    for (Syntax::AstNode* parent = descendant->Parent(); parent != nullptr;
         parent = parent->Parent()) {
        for (IL::ILInstruction* inst : GetILInstructions(*parent))
            descendant->AddAnnotation(std::make_shared<ILInstructionAnnotation>(inst));
        if (parent == expression_)
            return TranslatedExpression(Syntax::Detach(descendant));
    }
    throw std::invalid_argument(
        "descendant must be a descendant of the current node");
}

// ---------------------------------------------------------------------------
// The conversion machinery (the C# TranslatedExpression.cs tail)

// The REAL type-system namespace alias (the ExpressionBuilder TS:: convention -- the
// CSharp/TypeSystem sub-namespace shadows the plain TypeSystem:: lookup here).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

// The owning shared handles the C# GC-reference reads alias: the resolve result
// the TranslatedExpression carries (resolveResultShared) and the resolve-result
// annotation of a node (SharedRR). The port's ResolveResult is not
// enable_shared_from_this, so the annotation channel's owning shared_ptr view is
// the shared handle.
std::shared_ptr<Sem::ResolveResult> SharedRR(const Syntax::AstNode& node)
{
    for (const auto& a : node.SharedAnnotations())
    {
        if (dynamic_cast<Sem::ResolveResult*>(a.get()) != nullptr)
            return std::static_pointer_cast<Sem::ResolveResult>(a);
    }
    return nullptr;
}

// The C# annotation channel stores the annotation OBJECT (a GC reference) and
// the C# code reads the owning reference back out (`conversion` in the
// implicit-conversion unwrap arm is the SAME object the resolve result holds).
// The port's channel owns through shared_ptr<AnnotationBase>; this helper finds
// the owning shared handle of the first `T` annotation on a node (nullopt when
// absent) so the re-owning sites keep the C# object identity.
template <class T>
std::shared_ptr<T> FindSharedAnnotation(const Syntax::AstNode& node)
{
    static_assert(std::is_base_of_v<Syntax::AnnotationBase, T>);
    for (const auto& a : node.SharedAnnotations())
    {
        if (dynamic_cast<T*>(a.get()) != nullptr)
            return std::static_pointer_cast<T>(a);
    }
    return nullptr;
}


// The C# `bool CastCanBeMadeImplicit(Resolver.CSharpConversions conversions,
// Conversion conversion, IType inputType, IType oldTargetType, IType
// newTargetType)` private helper.
bool CastCanBeMadeImplicit(Resolver::CSharpConversions& conversions,
                           const TS::ICompilation* expressionBuilderCompilation,
                           const Sem::Conversion& conversion,
                           TS::IType& inputType,
                           TS::IType& oldTargetType,
                           TS::IType& newTargetType)
{
    if (!conversion.IsImplicit())
    {
        // If the cast was required for the old conversion, avoid making it implicit.
        return false;
    }
    if (oldTargetType.Kind() == TS::TypeKind::NInt || oldTargetType.Kind() == TS::TypeKind::NUInt
        || newTargetType.Kind() == TS::TypeKind::NInt || newTargetType.Kind() == TS::TypeKind::NUInt)
    {
        // nint has identity conversion with IntPtr, but the two have different implicit conversions
        return false;
    }
    if (conversion.IsBoxingConversion())
    {
        return Resolver::Detail::IsBoxingConversionOrInvolvingTypeParameter(
            *expressionBuilderCompilation, inputType, newTargetType);
    }
    if (conversion.IsInterpolatedStringConversion())
    {
        return IsKnownType(newTargetType, TS::KnownTypeCode::FormattableString)
               || IsKnownType(newTargetType, TS::KnownTypeCode::IFormattable);
    }
    return Resolver::Detail::IdentityConversion(oldTargetType, newTargetType);
}



// The C# `bool IsFixedVariable()` private helper: whether the DirectionExpression's
// inner expression carries an IL-instruction annotation that computes the address of
// a fixed variable (the `PointerArithmeticOffset.IsFixedVariable` scan).
bool IsFixedVariable(const Syntax::Expression* expression)
{
    if (auto* dirExpr = dynamic_cast<const Syntax::DirectionExpression*>(expression))
    {
        // The C# `dirExpr.Expression.Annotation<ILInstruction>()` -- the FIRST
        // IL-instruction annotation on the inner expression.
        for (IL::ILInstruction* inst : GetILInstructions(*dirExpr->Expression()))
            return IsFixedVariableInstruction(*inst);
        return false;
    }
    return false;
}

// The boxed-int/uint zero test the ConvertTo pointer-constant arm reads
// (the C# `0.Equals(ResolveResult.ConstantValue) || 0u.Equals(...)`).
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

} // namespace

// The C# `PointerArithmeticOffset.IsFixedVariable(ILInstruction)` scan
// (IL/PointerArithmeticOffset.cs line 120): whether an instruction computes the
// address of a fixed variable -- an uncaptured local (LdLoca; this port's IL
// reader does not yet model closures, so every local is uncaptured), the address
// of another fixed field (LdFlda recursion), or any other StackType.I
// computation. Declared in the header so VisitConv's StopGCTracking arm shares
// the one implementation with the IsFixedVariable(Expression) helper above.
bool IsFixedVariableInstruction(const IL::ILInstruction& inst)
{
    switch (inst.Op)
    {
        case IL::OpCode::LdLoca:
            // The C# `ldloca.Variable.CaptureScope == null` -- the port's IL
            // reader never sets a capture scope (the closure machinery is not
            // ported yet), so every local is uncaptured.
            return true;
        case IL::OpCode::LdFlda:
            if (const auto* ldflda = dynamic_cast<const IL::LdFlda*>(&inst))
            {
                return ldflda->Target != nullptr
                       && IsFixedVariableInstruction(*ldflda->Target);
            }
            return false;
        default:
            return inst.ResultType() == IL::StackType::I;
    }
}

// The C# `typeDef.Fields.Any(f => f.GetConstantValue() is { } val &&
// (ulong)CSharpPrimitiveCast.Cast(TypeCode.UInt64, val, false) == 0L)` filter of
// the ConvertToBoolean enum arm: whether the enum definition declares any field
// whose constant value is zero (the C# casts the boxed value through the UInt64
// code unchecked).
bool EnumHasAZeroValuedField(const TS::ITypeDefinition& typeDef)
{
    for (const ::ILSpy::Decompiler::TypeSystem::IField* field : typeDef.Fields())
    {
        if (field == nullptr)
            continue;
        std::any val = field->GetConstantValue(false);
        if (!val.has_value())
            continue;
        std::any cast = ::ILSpy::Decompiler::Util::Cast(
            TS::TypeCode::UInt64, val, false);
        if (!cast.has_value())
            continue;
        if (cast.type() == typeid(std::uint64_t)
            && std::any_cast<std::uint64_t>(cast) == 0ULL)
            return true;
    }
    return false;
}

// The C# `public TranslatedExpression UnwrapImplicitBoolConversion(Func<IType, bool>?
// typeFilter = null)`.
TranslatedExpression TranslatedExpression::UnwrapImplicitBoolConversion(
    std::function<bool(const TS::IType&)> typeFilter) const
{
    if (!TS::IsKnownType(Type(), TS::KnownTypeCode::Boolean))
        return *this;
    const auto* rr = dynamic_cast<const Sem::ConversionResolveResult*>(resolveResult_);
    if (rr == nullptr)
        return *this;
    const Sem::Conversion* conversion = rr->ConversionProperty();
    if (conversion == nullptr || !conversion->IsUserDefined() || !conversion->IsImplicit())
        return *this;
    if (typeFilter && !typeFilter(const_cast<TS::IType&>(rr->Input()->Type())))
        return *this;
    if (auto* cast = dynamic_cast<Syntax::CastExpression*>(expression_))
    {
        return UnwrapChild(cast->Expression());
    }
    return *this;
}

// The C# `public TranslatedExpression ConvertToBoolean(ExpressionBuilder
// expressionBuilder, bool negate = false)`.
TranslatedExpression TranslatedExpression::ConvertToBoolean(
    const ExpressionBuilder& expressionBuilder, bool negate)
{
    if (TS::IsKnownType(Type(), TS::KnownTypeCode::Boolean) || Type().Kind() == TS::TypeKind::Unknown)
    {
        if (negate)
        {
            return WithoutILInstruction(expressionBuilder.LogicNot(*this));
        }
        else
        {
            return *this;
        }
    }
    assert(IsIntegerType(TS::GetStackType(Type())));
    auto boolType =
        const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::Boolean))
            .shared_from_this();
    if (resolveResult_->IsCompileTimeConstant() && resolveResult_->ConstantValue().has_value()
        && resolveResult_->ConstantValue().type() == typeid(std::int32_t))
    {
        bool val = std::any_cast<std::int32_t>(resolveResult_->ConstantValue()) != 0;
        val ^= negate;
        auto* primitive = new Syntax::PrimitiveExpression(val);
        return WithRR(WithILInstruction(*primitive, GetILInstructions(*expression_)),
                      std::make_shared<Sem::ConstantResolveResult>(boolType, val));
    }
    else if (resolveResult_->IsCompileTimeConstant()
             && resolveResult_->ConstantValue().has_value()
             && resolveResult_->ConstantValue().type() == typeid(std::uint8_t))
    {
        bool val = std::any_cast<std::uint8_t>(resolveResult_->ConstantValue()) != 0;
        val ^= negate;
        auto* primitive = new Syntax::PrimitiveExpression(val);
        return WithRR(WithILInstruction(*primitive, GetILInstructions(*expression_)),
                      std::make_shared<Sem::ConstantResolveResult>(boolType, val));
    }
    else if (Type().Kind() == TS::TypeKind::Pointer)
    {
        auto* nullRef = new Syntax::NullReferenceExpression();
        TranslatedExpression nullExpr =
            WithRR(WithoutILInstruction(*nullRef),
                   std::make_shared<Sem::ConstantResolveResult>(TS::NullType(), std::any{}));
        auto op = negate ? Syntax::BinaryOperatorType::Equality
                         : Syntax::BinaryOperatorType::InEquality;
        auto* binary =
            new Syntax::BinaryOperatorExpression(expression_, op, nullRef);
        return WithRR(WithoutILInstruction(*binary),
                      std::make_shared<Sem::OperatorResolveResult>(
                          boolType, TS::ExpressionType::NotEqual,
                          std::vector<std::shared_ptr<Sem::ResolveResult>>{
                              SharedRR(*expression_), SharedRR(*nullExpr.Expression())}));
    }
    else if (Type().Kind() == TS::TypeKind::Enum && Type().GetDefinition() != nullptr
             && EnumHasAZeroValuedField(*Type().GetDefinition()))
    {
        auto zeroRR = std::make_shared<Sem::ConstantResolveResult>(
            const_cast<TS::IType&>(Type()).shared_from_this(), std::int32_t(0));
        TranslatedExpression zero =
            WithILInstruction(expressionBuilder.ConvertConstantValue(zeroRR, true),
                              GetILInstructions(*expression_));
        auto op = negate ? Syntax::BinaryOperatorType::Equality
                         : Syntax::BinaryOperatorType::InEquality;
        auto* binary =
            new Syntax::BinaryOperatorExpression(expression_, op, zero.Expression());
        return WithRR(WithoutILInstruction(*binary),
                      std::make_shared<Sem::OperatorResolveResult>(
                          boolType, TS::ExpressionType::NotEqual,
                          std::vector<std::shared_ptr<Sem::ResolveResult>>{
                              SharedRR(*expression_), SharedRR(*zero.Expression())}));
    }
    else
    {
        auto* zero = new Syntax::PrimitiveExpression(std::int32_t(0));
        TranslatedExpression zeroExpr =
            WithRR(WithoutILInstruction(*zero),std::make_shared<Sem::ConstantResolveResult>(
                    const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::Int32))
                        .shared_from_this(),
                    std::int32_t(0)));
        auto op = negate ? Syntax::BinaryOperatorType::Equality
                         : Syntax::BinaryOperatorType::InEquality;
        auto* binary =
            new Syntax::BinaryOperatorExpression(expression_, op, zero);
        return WithRR(WithoutILInstruction(*binary),
                      std::make_shared<Sem::OperatorResolveResult>(
                          boolType, TS::ExpressionType::NotEqual,
                          std::vector<std::shared_ptr<Sem::ResolveResult>>{
                              SharedRR(*expression_), SharedRR(*zeroExpr.Expression())}));
    }
}

// The C# `public TranslatedExpression ConvertTo(IType targetType,
// ExpressionBuilder expressionBuilder, bool checkForOverflow = false, bool
// allowImplicitConversion = false)` -- the full cast-insertion machinery (see the
// C# remarks for the post-condition contract).
TranslatedExpression TranslatedExpression::ConvertTo(TS::IType& targetType,
                                                     const ExpressionBuilder& expressionBuilder,
                                                     bool checkForOverflow,
                                                     bool allowImplicitConversion)
{
    TS::IType& type = const_cast<TS::IType&>(Type());
    if (TS::NormalizeTypeVisitor::IgnoreNullabilityAndTuples().EquivalentTypes(
            type, targetType))
    {
        // Make explicit conversion implicit, if possible
        if (allowImplicitConversion)
        {
            if (const auto* conversion =
                    dynamic_cast<const Sem::ConversionResolveResult*>(resolveResult_))
            {
                if (auto* cast = dynamic_cast<Syntax::CastExpression*>(expression_))
                {
                    auto& conversions =
                        Resolver::CSharpConversions::Get(*expressionBuilder.compilation);
                    if (CastCanBeMadeImplicit(
                            conversions, expressionBuilder.compilation, *conversion->ConversionProperty(),
                            const_cast<TS::IType&>(conversion->Input()->Type()), type,
                            targetType))
                    {
                        TranslatedExpression result = UnwrapChild(cast->Expression());
                        if (conversion->ConversionProperty()->IsUserDefined())
                        {
                            // The C# `result.Expression.AddAnnotation(new
                            // ImplicitConversionAnnotation(conversion))` aliases the
                            // EXISTING conversion resolve result (the holder stores the
                            // C# reference; the port re-owns it through the annotation
                            // channel's SharedAnnotations view).
                            std::shared_ptr<Sem::ConversionResolveResult> shared =
                                FindSharedAnnotation<Sem::ConversionResolveResult>(*expression_);
                            if (shared)
                            {
                                result.Expression()->AddAnnotation(
                                    std::make_shared<ImplicitConversionAnnotation>(shared));
                            }
                        }
                        return result;
                    }
                    else if (auto* oce =
                                 dynamic_cast<Syntax::ObjectCreateExpression*>(expression_))
                    {
                        if (conversion->ConversionProperty()->IsMethodGroupConversion()
                            && oce->Arguments().Count() == 1
                            && expressionBuilder.settings->UseImplicitMethodGroupConversion())
                        {
                            return UnwrapChild(oce->Arguments().FirstOrNull());
                        }
                    }
                }
            }
            else if (const auto* invocation =
                         dynamic_cast<const Sem::InvocationResolveResult*>(resolveResult_))
            {
                if (auto* oce = dynamic_cast<Syntax::ObjectCreateExpression*>(expression_))
                {
                    if (oce->Arguments().Count() == 1
                        && TS::IsKnownType(const_cast<TS::IType&>(invocation->Type()),
                                       TS::KnownTypeCode::NullableOfT))
                    {
                        return UnwrapChild(oce->Arguments().FirstOrNull());
                    }
                }
            }
        }
        return *this;
    }
    if (targetType.Kind() == TS::TypeKind::Void || targetType.Kind() == TS::TypeKind::None)
    {
        return *this; // don't attempt to insert cast to '?' or 'void' as these are not valid.
    }
    else if (targetType.Kind() == TS::TypeKind::Unknown)
    {
        // don't attempt cast to '?', or casts between an unknown type and a known
        // type with same name
        if (targetType.Name() == "?" || targetType.ReflectionName() == type.ReflectionName())
        {
            return *this;
        }
        // However we still want explicit casts to types that are merely unresolved
    }
    if (auto* convAnnotation = expression_->Annotation<ImplicitConversionAnnotation>())
    {
        // If an implicit user-defined conversion was stripped from this expression;
        // it needs to be re-introduced before we can apply other casts to this expression.
        // This happens when the CallBuilder discovers that the conversion is necessary in
        // order to choose the correct overload.
        expression_->RemoveAnnotations<ImplicitConversionAnnotation>();
        return WithRR(WithoutILInstruction(
                          *new Syntax::CastExpression(
                              expressionBuilder.ConvertType(
                                  const_cast<TS::IType&>(convAnnotation->TargetType())),
                              expression_)),
                      convAnnotation->ConversionResolveResult)
            .ConvertTo(targetType, expressionBuilder, checkForOverflow, allowImplicitConversion);
    }
    if (dynamic_cast<Syntax::ThrowExpression*>(expression_) != nullptr && allowImplicitConversion)
    {
        return *this; // Throw expressions have no type and are implicitly convertible to any type
    }
    if (auto* tupleExpr = dynamic_cast<Syntax::TupleExpression*>(expression_))
    {
        const auto* targetTupleType = dynamic_cast<const TS::TupleType*>(&targetType);
        if (targetTupleType != nullptr
            && tupleExpr->Elements().Count()
                   == static_cast<int>(targetTupleType->ElementTypes().size()))
        {
            // Conversion of a tuple literal: convert element-wise
            auto* newTupleExpr = new Syntax::TupleExpression();
            std::vector<std::shared_ptr<Sem::ResolveResult>> newElementRRs;
            // element names: discard existing names and use targetTupleType instead
            const std::vector<std::string>& newElementNames = targetTupleType->ElementNames();
            std::size_t index = 0;
            for (int elementIndex = 0; elementIndex < tupleExpr->Elements().Count();
                 ++elementIndex)
            {
                Syntax::Expression* elementExpr = tupleExpr->Elements().At(elementIndex);
                TS::IType& elementTargetType = const_cast<TS::IType&>(
                    *targetTupleType->ElementTypes()[index]);
                Syntax::Expression* unwrapped = elementExpr;
                if (auto* nae = dynamic_cast<Syntax::NamedArgumentExpression*>(elementExpr))
                    unwrapped = nae->Expression();
                TranslatedExpression newElementExpr =
                    TranslatedExpression(Syntax::Detach(unwrapped))
                        .ConvertTo(elementTargetType, expressionBuilder, checkForOverflow,
                                   allowImplicitConversion);
                const std::string* name =
                    index < newElementNames.size() ? &newElementNames[index] : nullptr;
                if (name == nullptr || name->empty())
                {
                    newTupleExpr->Elements().Add(newElementExpr.Expression());
                }
                else
                {
                    newTupleExpr->Elements().Add(
                        new Syntax::NamedArgumentExpression(*name, newElementExpr.Expression()));
                }
                newElementRRs.push_back(SharedRR(*newElementExpr.Expression()));
                index++;
            }
            // The C# `valueTupleAssembly: targetTupleType.GetDefinition()?.ParentModule`.
            return WithRR(
                WithoutILInstruction(*newTupleExpr),
                std::make_shared<Sem::TupleResolveResult>(
                    std::move(newElementRRs), std::nullopt,
                    TS::CreateTupleType(*expressionBuilder.compilation,
                                        targetTupleType->ElementTypes(),
                                        targetTupleType->ElementNames(),
                                        targetTupleType->GetDefinition() != nullptr
                                            ? targetTupleType->GetDefinition()->ParentModule()
                                            : nullptr)));
        }
    }
    auto& conversions = Resolver::CSharpConversions::Get(*expressionBuilder.compilation);
    if (const auto* conv = dynamic_cast<const Sem::ConversionResolveResult*>(resolveResult_))
    {
        if (auto* cast = dynamic_cast<Syntax::CastExpression*>(expression_))
        {
            if (!conv->ConversionProperty()->IsUserDefined()
                && CastCanBeMadeImplicit(
                    conversions, expressionBuilder.compilation, *conv->ConversionProperty(),
                    const_cast<TS::IType&>(conv->Input()->Type()), type, targetType))
            {
                TranslatedExpression unwrapped = UnwrapChild(cast->Expression());
                if (allowImplicitConversion)
                    return unwrapped;
                return unwrapped.ConvertTo(targetType, expressionBuilder, checkForOverflow,
                                           allowImplicitConversion);
            }
        }
    }
    if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expression_))
    {
        if (uoe->Operator() == Syntax::UnaryOperatorType::NullConditional
            && TS::IsReferenceType(&targetType) == true)
        {
            // "(T)(x?).AccessChain" is invalid, but "((T)x)?.AccessChain" is valid and equivalent
            auto* renamed = new Syntax::UnaryOperatorExpression(
                UnwrapChild(uoe->Expression())
                    .ConvertTo(targetType, expressionBuilder, checkForOverflow,
                               allowImplicitConversion)
                    .Expression(),
                Syntax::UnaryOperatorType::NullConditional);
            return WithRR(WithoutILInstruction(*renamed),
                          std::make_shared<Sem::ResolveResult>(targetType.shared_from_this()));
        }
    }
    const TS::IType* utype = TS::GetEnumUnderlyingType(&type);
    const TS::IType* targetUType = TS::GetEnumUnderlyingType(&targetType);
    if (TS::IsKnownType(type, TS::KnownTypeCode::Boolean)
        && !TS::IsKnownType(*targetUType, TS::KnownTypeCode::Boolean)
        && IsIntegerType(TS::GetStackType(*targetUType)))
    {
        // convert from boolean to integer (or enum)
        auto* conditional = new Syntax::ConditionalExpression(
            expression_,
            ExpressionBuilder::LdcI4(*expressionBuilder.compilation, 1)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow)
                .Expression(),
            ExpressionBuilder::LdcI4(*expressionBuilder.compilation, 0)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow)
                .Expression());
        return WithRR(WithoutILInstruction(*conditional),
                      std::make_shared<Sem::ResolveResult>(targetType.shared_from_this()));
    }
    if (TS::IsKnownType(targetType, TS::KnownTypeCode::Boolean))
    {
        // convert to boolean through byte, to simulate the truncation to 8 bits
        return ConvertTo(const_cast<TS::IType&>(
                             expressionBuilder.compilation->FindType(TS::KnownTypeCode::Byte)),
                         expressionBuilder, checkForOverflow)
            .ConvertToBoolean(expressionBuilder);
    }

    // Special-case IntPtr and UIntPtr: they behave extremely weird, see IntPtr.txt for details.
    if (TS::IsKnownType(type, TS::KnownTypeCode::IntPtr))
    { // Conversion from IntPtr
      // Direct cast only works correctly for IntPtr -> long.
      // IntPtr -> int works correctly only in checked context.
      // Everything else can be worked around by casting via long.
        if (!(TS::IsKnownType(targetType, TS::KnownTypeCode::Int64) || targetType.Kind() == TS::TypeKind::NInt
              || (checkForOverflow && TS::IsKnownType(targetType, TS::KnownTypeCode::Int32))
              || TS::IsAnyPointer(targetType.Kind()) || targetType.Kind() == TS::TypeKind::ByReference))
        {
            auto convertVia = expressionBuilder.settings->NativeIntegers()
                                  ? TS::NInt()
                                  : const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::Int64))
                             .shared_from_this();
            return ConvertTo(*convertVia, expressionBuilder, checkForOverflow)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow,
                           allowImplicitConversion);
        }
    }
    else if (TS::IsKnownType(type, TS::KnownTypeCode::UIntPtr))
    { // Conversion from UIntPtr
      // Direct cast only works correctly for UIntPtr -> ulong.
      // UIntPtr -> uint works correctly only in checked context.
      // Everything else can be worked around by casting via ulong.
        if (!(TS::IsKnownType(targetType, TS::KnownTypeCode::UInt64)
              || targetType.Kind() == TS::TypeKind::NUInt
              || (checkForOverflow && TS::IsKnownType(targetType, TS::KnownTypeCode::UInt32))
              || TS::IsAnyPointer(targetType.Kind()) || targetType.Kind() == TS::TypeKind::ByReference))
        {
            auto convertVia = expressionBuilder.settings->NativeIntegers()
                                  ? TS::NUInt()
                                  : const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::UInt64))
                             .shared_from_this();
            return ConvertTo(*convertVia, expressionBuilder, checkForOverflow)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow,
                           allowImplicitConversion);
        }
    }
    if (TS::IsKnownType(*targetUType, TS::KnownTypeCode::IntPtr) && IsIntegerType(TS::GetStackType(*utype)))
    { // Conversion to IntPtr
        if (TS::IsKnownType(type, TS::KnownTypeCode::Int32) || type.Kind() == TS::TypeKind::NInt)
        {
            // normal casts work for int/nint (both in checked and unchecked context)
            // note that pointers only allow normal casts in unchecked contexts
        }
        else if (expressionBuilder.settings->NativeIntegers())
        {
            // if native integer types are available, prefer using those
            return ConvertTo(*TS::NInt(), expressionBuilder, checkForOverflow)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow,
                           allowImplicitConversion);
        }
        else if (checkForOverflow)
        {
            // if overflow-checking is enabled, we can simply cast via long:
            // (and long itself works directly in checked context)
            if (!TS::IsKnownType(type, TS::KnownTypeCode::Int64))
            {
                return ConvertTo(
                           const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::Int64)),
                           expressionBuilder, checkForOverflow)
                    .ConvertTo(targetType, expressionBuilder, checkForOverflow);
            }
        }
        else if (!TS::IsAnyPointer(type.Kind()))
        {
            // If overflow-checking is disabled, the only way to truncate to native size
            // without throwing an exception in 32-bit mode is to use a pointer type.
            return ConvertTo(
                *std::make_shared<TS::PointerType>(const_cast<TS::IType&>(
                    expressionBuilder.compilation->FindType(TS::KnownTypeCode::Void))
                                                       .shared_from_this()),
                expressionBuilder, checkForOverflow)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow);
        }
    }
    else if (TS::IsKnownType(*targetUType, TS::KnownTypeCode::UIntPtr)
             && IsIntegerType(TS::GetStackType(*utype)))
    { // Conversion to UIntPtr
        if (TS::IsKnownType(type, TS::KnownTypeCode::UInt32) || TS::IsAnyPointer(type.Kind())
            || type.Kind() == TS::TypeKind::NUInt)
        {
            // normal casts work for uint/nuint and pointers (both in checked and unchecked context)
        }
        else if (expressionBuilder.settings->NativeIntegers())
        {
            // if native integer types are available, prefer using those
            return ConvertTo(*TS::NUInt(), expressionBuilder, checkForOverflow)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow,
                           allowImplicitConversion);
        }
        else if (checkForOverflow)
        {
            // if overflow-checking is enabled, we can simply cast via ulong:
            // (and ulong itself works directly in checked context)
            if (!TS::IsKnownType(type, TS::KnownTypeCode::UInt64))
            {
                return ConvertTo(
                           const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::UInt64)),
                           expressionBuilder, checkForOverflow)
                    .ConvertTo(targetType, expressionBuilder, checkForOverflow);
            }
        }
        else
        {
            // If overflow-checking is disabled, the only way to truncate to native size
            // without throwing an exception in 32-bit mode is to use a pointer type.
            return ConvertTo(
                *std::make_shared<TS::PointerType>(const_cast<TS::IType&>(
                    expressionBuilder.compilation->FindType(TS::KnownTypeCode::Void))
                                                       .shared_from_this()),
                expressionBuilder, checkForOverflow)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow);
        }
    }

    if (TS::IsAnyPointer(targetType.Kind()) && type.Kind() == TS::TypeKind::Enum)
    {
        // enum to pointer: C# doesn't allow such casts
        // -> convert via underlying type
        return ConvertTo(const_cast<TS::IType&>(*TS::GetEnumUnderlyingType(&type)),
                         expressionBuilder, checkForOverflow)
            .ConvertTo(targetType, expressionBuilder, checkForOverflow);
    }
    else if (targetUType->Kind() == TS::TypeKind::Enum && TS::IsAnyPointer(type.Kind()))
    {
        // pointer to enum: C# doesn't allow such casts
        // -> convert via underlying type
        return ConvertTo(const_cast<TS::IType&>(*TS::GetEnumUnderlyingType(targetUType)),
                         expressionBuilder, checkForOverflow)
            .ConvertTo(targetType, expressionBuilder, checkForOverflow);
    }
    if ((TS::IsAnyPointer(targetType.Kind()) && TS::IsKnownType(type, TS::KnownTypeCode::Char))
        || (TS::IsKnownType(*targetUType, TS::KnownTypeCode::Char) && TS::IsAnyPointer(type.Kind())))
    {
        // char <-> pointer: C# doesn't allow such casts
        // -> convert via ushort
        return ConvertTo(const_cast<TS::IType&>(expressionBuilder.compilation->FindType(TS::KnownTypeCode::UInt16)),
                         expressionBuilder, checkForOverflow)
            .ConvertTo(targetType, expressionBuilder, checkForOverflow);
    }
    if (targetType.Kind() == TS::TypeKind::Pointer && type.Kind() == TS::TypeKind::ByReference
        && dynamic_cast<Syntax::DirectionExpression*>(expression_) != nullptr)
    {
        // convert from reference to pointer
        auto* dirExpr = static_cast<Syntax::DirectionExpression*>(expression_);
        Syntax::Expression* arg = Syntax::Detach(dirExpr->Expression());
        auto pointerType = std::make_shared<TS::PointerType>(
            static_cast<const TS::ByReferenceType&>(type).Element());
        if (auto* argUOE = dynamic_cast<Syntax::UnaryOperatorExpression*>(arg))
        {
            if (argUOE->Operator() == Syntax::UnaryOperatorType::Dereference)
            {
                // &*ptr -> ptr
                return TranslatedExpression(argUOE)
                    .UnwrapChild(argUOE->Expression())
                    .ConvertTo(targetType, expressionBuilder);
            }
        }
        auto* pointerExpr =
            new Syntax::UnaryOperatorExpression(arg, Syntax::UnaryOperatorType::AddressOf);
        TranslatedExpression pointerTranslated =
            WithRR(WithILInstruction(*pointerExpr, GetILInstructions(*expression_)),
                   std::make_shared<Sem::ResolveResult>(pointerType));
        // perform remaining pointer cast, if necessary
        return pointerTranslated.ConvertTo(targetType, expressionBuilder);
    }
    if (targetType.Kind() == TS::TypeKind::ByReference)
    {
        if (TS::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
                targetType, const_cast<TS::IType&>(Type())))
        {
            return *this;
        }
        TS::IType& elementType = const_cast<TS::IType&>(
            *static_cast<const TS::ByReferenceType&>(targetType).Element());
        bool anyAddressOf = false;
        for (IL::ILInstruction* inst : GetILInstructions(*expression_))
        {
            if (inst->Op == IL::OpCode::AddressOf)
            {
                anyAddressOf = true;
                break;
            }
        }
        auto* thisDir = dynamic_cast<Syntax::DirectionExpression*>(expression_);
        const Sem::ResolveResult* tempRR =
            thisDir != nullptr ? GetResolveResult(*thisDir->Expression()) : nullptr;
        if (thisDir != nullptr && anyAddressOf && tempRR != nullptr
            && TS::GetStackType(tempRR->Type()) == TS::GetStackType(elementType))
        {
            // When converting a reference to a temporary to a different type,
            // apply the cast to the temporary instead.
            TranslatedExpression convertedTemp =
                UnwrapChild(thisDir->Expression())
                    .ConvertTo(elementType, expressionBuilder, checkForOverflow);
            auto* result = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref,
                                                           convertedTemp.Expression());
            return WithRR(WithILInstruction(*result, GetILInstructions(*expression_)),
                          std::make_shared<Sem::ByReferenceResolveResult>(
                              SharedRR(*convertedTemp.Expression()),
                              TS::ReferenceKind::Ref));
        }
        Syntax::Expression* expr;
        if (Type().Kind() == TS::TypeKind::ByReference && !IsFixedVariable(expression_))
        {
            // Convert between managed reference types.
            // We can't do this by going through a pointer type because that would
            // temporarily stop GC tracking.
            // Instead, emit `ref Unsafe.As<T>(ref expr)`
            return expressionBuilder.CallUnsafeIntrinsic(
                "As", {expression_}, targetType, nullptr,
                std::vector<TS::ITypePtr>{
                    static_cast<const TS::ByReferenceType&>(Type()).Element(),
                    static_cast<const TS::ByReferenceType&>(targetType).Element()});
        }
        // Convert from integer/pointer to reference.
        // First, convert to the corresponding pointer type:
        TranslatedExpression arg = ConvertTo(
            *std::make_shared<TS::PointerType>(static_cast<const TS::ByReferenceType&>(targetType).Element()),
            expressionBuilder, checkForOverflow);
        std::shared_ptr<Sem::ResolveResult> elementRR;
        if (auto* unary = dynamic_cast<Syntax::UnaryOperatorExpression*>(arg.Expression());
            unary != nullptr && unary->Operator() == Syntax::UnaryOperatorType::AddressOf)
        {
            // If we already have an address -> unwrap
            expr = UnwrapChild(unary->Expression()).Expression();
            elementRR = FindSharedAnnotation<Sem::ResolveResult>(*expr);
            if (!elementRR)
            {
                // The C# `expr.GetResolveResult()` fallback is the UnknownError
                // singleton -- the no-op-deleter alias (the type system / error
                // registry owns it for the process lifetime).
                elementRR = std::shared_ptr<Sem::ResolveResult>(
                    const_cast<Sem::ResolveResult*>(
                        static_cast<const Sem::ResolveResult*>(
                            &Sem::ErrorResolveResult::UnknownError())),
                    [](Sem::ResolveResult*) noexcept {});
            }
        }
        else
        {
            // Otherwise dereference the pointer:
            expr = new Syntax::UnaryOperatorExpression(arg.Expression(),
                                                      Syntax::UnaryOperatorType::Dereference);
            elementRR = std::make_shared<Sem::ResolveResult>(
                static_cast<const TS::ByReferenceType&>(targetType).Element());
            expr->AddAnnotation(elementRR);
        }
        // And then take a reference:
        auto* result = new Syntax::DirectionExpression(Syntax::FieldDirection::Ref, expr);
        return WithRR(WithoutILInstruction(*result),
                      std::make_shared<Sem::ByReferenceResolveResult>(elementRR,
                                                                      TS::ReferenceKind::Ref));
    }
    if (resolveResult_->IsCompileTimeConstant() && resolveResult_->ConstantValue().has_value()
        && TS::IsNullable(targetType) && !utype->Equals(*targetUType)
        && IsIntegerType(TS::GetStackType(*targetUType)))
    {
        // Casts like `(uint?)-1` are only valid in an explicitly unchecked context, but we
        // don't have logic to ensure such a context (usually we emit into an implicitly
        // unchecked context).
        // This only applies with constants as input (int->uint? is fine in implicitly
        // unchecked context).
        // We use an intermediate cast to the nullable's underlying type, which results
        // in a constant conversion, so the final output will be something like
        // `(uint?)uint.MaxValue`
        return ConvertTo(const_cast<TS::IType&>(*targetUType), expressionBuilder,
                         checkForOverflow, false)
            .ConvertTo(targetType, expressionBuilder, checkForOverflow, allowImplicitConversion);
    }
    auto rr = expressionBuilder.resolver->WithCheckForOverflow(checkForOverflow)->ResolveCast(
        targetType, SharedRR(*expression_));
    if (rr->IsCompileTimeConstant() && !rr->IsError())
    {
        TranslatedExpression convertedResult =
            WithILInstruction(expressionBuilder.ConvertConstantValue(rr, allowImplicitConversion),
                              GetILInstructions(*expression_));
        if (auto* outputLiteral = dynamic_cast<Syntax::PrimitiveExpression*>(convertedResult.Expression()))
        {
            if (auto* inputLiteral = dynamic_cast<Syntax::PrimitiveExpression*>(expression_))
                outputLiteral->Format(inputLiteral->Format());
        }
        return convertedResult;
    }
    else if (rr->IsError() && TS::IsReferenceType(&targetType) == true && TS::IsReferenceType(&type) == true)
    {
        // Conversion between two reference types, but no direct cast allowed? cast via object
        // Just make sure we avoid infinite recursion even if the resolver falsely claims
        // we can't cast directly:
        if (!(TS::IsKnownType(targetType, TS::KnownTypeCode::Object)
              || TS::IsKnownType(type, TS::KnownTypeCode::Object)))
        {
            return ConvertTo(const_cast<TS::IType&>(
                                 expressionBuilder.compilation->FindType(TS::KnownTypeCode::Object)),
                             expressionBuilder)
                .ConvertTo(targetType, expressionBuilder, checkForOverflow,
                           allowImplicitConversion);
        }
    }
    else if (type.Kind() == TS::TypeKind::Dynamic && TS::IsReferenceType(&targetType) == true
             && !TS::IsKnownType(targetType, TS::KnownTypeCode::Object))
    {
        // "static" conversion between dynamic and a reference type requires us to add a
        // cast to object, otherwise recompilation would produce a dynamic cast.
        // (T)dynamicExpression is a "dynamic" cast
        // (T)(object)dynamicExpression is a "static" cast
        // as "dynamic" casts are handled differently by ExpressionBuilder.
        // VisitDynamicConvertInstruction we can always insert the cast to object, if we
        // encounter a conversion from any reference type to dynamic.
        return ConvertTo(const_cast<TS::IType&>(
                             expressionBuilder.compilation->FindType(TS::KnownTypeCode::Object)),
                         expressionBuilder)
            .ConvertTo(targetType, expressionBuilder, checkForOverflow, allowImplicitConversion);
    }
    if (TS::IsAnyPointer(targetType.Kind()) && IsZeroConstant(resolveResult_->ConstantValue()))
    {
        if (allowImplicitConversion)
        {
            auto* nullLiteral = new Syntax::NullReferenceExpression();
            return WithRR(WithILInstruction(*nullLiteral, GetILInstructions(*expression_)),
                          std::make_shared<Sem::ConstantResolveResult>(TS::NullType(), std::any{}));
        }
        auto* castNull = new Syntax::CastExpression(
            expressionBuilder.ConvertType(targetType), new Syntax::NullReferenceExpression());
        return WithRR(WithILInstruction(*castNull, GetILInstructions(*expression_)),
                      std::make_shared<Sem::ConstantResolveResult>(targetType.shared_from_this(),
                                                                   std::any{}));
    }
    if (allowImplicitConversion)
    {
        if (conversions.ImplicitConversion(*resolveResult_, targetType)->IsValid())
        {
            return *this;
        }
    }
    else
    {
        if (TS::NormalizeTypeVisitor::IgnoreNullabilityAndTuples().EquivalentTypes(
                type, targetType))
        {
            // avoid an explicit cast when types differ only in nullability of reference types
            return *this;
        }
    }
    // BaseReferenceExpression must not be used with CastExpressions
    Syntax::Expression* expr;
    expr = dynamic_cast<Syntax::BaseReferenceExpression*>(expression_) != nullptr
               ? static_cast<Syntax::Expression*>(new Syntax::ThisReferenceExpression())
               : expression_;
    if (expr != expression_)
        WithILInstruction(*expr, GetILInstructions(*expression_));
    auto* castExpr = new Syntax::CastExpression(expressionBuilder.ConvertType(targetType), expr);
    bool needsCheckAnnotation = IsIntegerType(TS::GetStackType(*targetUType));
    if (needsCheckAnnotation)
    {
        if (checkForOverflow)
        {
            castExpr->AddAnnotation(Transforms::CheckedAnnotationHandle());
        }
        else if (resolveResult_->IsCompileTimeConstant()
                 && TS::IsCSharpNativeIntegerType(targetUType))
        {
            // unchecked potentially-overflowing cast of constant to n(u)int:
            // Placement in implicitly unchecked context is not good enough when applied
            // to compile-time constant, the constant must be placed into an explicit
            // unchecked block.
            // (note that non-potentially-overflowing casts will be handled by constant
            // folding and won't get here)
            castExpr->AddAnnotation(Transforms::ExplicitUncheckedAnnotationHandle());
        }
        else
        {
            castExpr->AddAnnotation(Transforms::UncheckedAnnotationHandle());
        }
    }
    return WithRR(WithoutILInstruction(*castExpr), std::move(rr));
}

}  // namespace ILSpy::Decompiler::CSharp
