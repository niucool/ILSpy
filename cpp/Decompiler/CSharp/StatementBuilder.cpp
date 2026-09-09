// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The StatementBuilder skeleton implementation (see StatementBuilder.hpp for the
// porting notes). Each Visit arm mirrors its C# counterpart (StatementBuilder.cs
// lines 53-153); the members the later slices consume carry their C# line numbers.

#include "Decompiler/CSharp/StatementBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldReturnStatement.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp {

// The file-local annotation-bridge helper (the CallBuilder.cpp convention): the
// shared_ptr owning the resolve-result annotation on an expression, for callers
// that need to store it inside another resolve result.
std::shared_ptr<Sem::ResolveResult> SharedResolveResultAnnotation(
    const Syntax::Expression& expr)
{
    for (const auto& a : expr.SharedAnnotations())
    {
        if (dynamic_cast<Sem::ResolveResult*>(a.get()) != nullptr)
            return std::static_pointer_cast<Sem::ResolveResult>(a);
    }
    return nullptr;
}

// The C# `static bool IsPossibleLossOfTypeInformation(IType givenType, IType
// expectedType)` local function inside VisitLeave (StatementBuilder.cs lines
// 404-419): whether the return-value conversion loses type information a cast can
// preserve (tuple-vs-underlying identity, 'dynamic', the null literal's type).
bool IsPossibleLossOfTypeInformation(const TS::IType& givenType,
                                     const TS::IType& expectedType)
{
    if (::ILSpy::Decompiler::ContainsAnonymousType(expectedType))
        return false;
    // The C# `NormalizeTypeVisitor.IgnoreNullability.EquivalentTypes(givenType,
    // expectedType)` -- non-const refs (the established const_cast convention).
    if (TS::NormalizeTypeVisitor::IgnoreNullability().EquivalentTypes(
            const_cast<TS::IType&>(givenType), const_cast<TS::IType&>(expectedType)))
        return false;
    // The C# `expectedType is TupleType { ElementNames.IsEmpty: false }`: any tuple
    // with at least one element (the C# ctor fills the names with nulls when not
    // provided, so IsEmpty tracks the element count, not real names).
    if (const auto* tuple = dynamic_cast<const TS::TupleType*>(&expectedType);
        tuple != nullptr && !tuple->ElementNames().empty())
        return true;
    // The C# `expectedType == SpecialType.Dynamic` / `givenType ==
    // SpecialType.NullType` -- reference equality against the singletons; the
    // port's Kind-based convention (the CSharpConversionsHelpers convention).
    if (expectedType.Kind() == TS::TypeKind::Dynamic)
        return true;
    if (givenType.Kind() == TS::TypeKind::Null)
        return true;
    return false;
}

StatementBuilder::~StatementBuilder() = default;

StatementBuilder::StatementBuilder(const TS::ICompilation& typeSystemValue,
                                   const TS::ITypeResolveContext& decompilationContext,
                                   IL::ILFunction* currentFunctionValue,
                                   const DecompilerSettings* settingsValue,
                                   const DecompileRun* decompileRunValue)
{
    // The C# `Debug.Assert(typeSystem != null && decompilationContext != null)` --
    // the reference parameters carry their non-nullness in their type; the pointer
    // parameters the C# would NRE on get the invalid_argument guard (the D424
    // convention).
    if (currentFunctionValue == nullptr)
        throw std::invalid_argument("currentFunction");
    if (settingsValue == nullptr)
        throw std::invalid_argument("settings");
    if (decompileRunValue == nullptr)
        throw std::invalid_argument("decompileRun");

    exprBuilder = std::make_unique<ExpressionBuilder>(this, typeSystemValue,
                                                      decompilationContext,
                                                      currentFunctionValue, settingsValue,
                                                      decompileRunValue);
    currentFunction = currentFunctionValue;
    // The C# `this.currentReturnContainer = (BlockContainer)currentFunction.Body` --
    // the port's Body is statically a BlockContainer, so the C# InvalidCastException
    // shape is unreachable.
    currentReturnContainer = currentFunction->Body.get();
    // The C# `this.currentResultType = currentFunction.IsAsync ?
    // currentFunction.AsyncReturnType! : currentFunction.ReturnType`.
    currentResultType = currentFunction->IsAsync() ? currentFunction->AsyncReturnType.get()
                                                  : currentFunction->ReturnType.get();
    currentIsIterator = currentFunction->IsIterator;
    typeSystem = &typeSystemValue;
    settings = settingsValue;
    decompileRun = decompileRunValue;
}

// The C# `public Statement Convert(ILInstruction inst)` (StatementBuilder.cs line
// 79): the visitor dispatch -- `inst.AcceptVisitor(this)` double dispatches to the
// per-instruction Visit method, returning a TranslatedStatement that the implicit
// operator converts to its Statement. The C# ThrowIfCancellationRequested() is the
// documented no-op deferral.
Syntax::Statement* StatementBuilder::Convert(IL::ILInstruction* inst)
{
    return Visit(inst).Statement();
}

// The C# `public BlockStatement ConvertAsBlock(ILInstruction inst)` (line 85):
// re-attaches the IL-instruction annotation and wraps the converted statement in a
// BlockStatement unless it already is one.
Syntax::BlockStatement* StatementBuilder::ConvertAsBlock(IL::ILInstruction* inst)
{
    Syntax::Statement* stmt = WithILInstruction(*Convert(inst), inst).Statement();
    if (auto* block = dynamic_cast<Syntax::BlockStatement*>(stmt))
        return block;
    auto* blockStatement = new Syntax::BlockStatement();
    blockStatement->Statements().Add(stmt);
    return blockStatement;
}

// The port's stand-in for the C# AcceptVisitor double dispatch: the OpCode switch
// over the landed Visit arms, falling back to Default.
TranslatedStatement StatementBuilder::Visit(IL::ILInstruction* inst)
{
    switch (inst->Op)
    {
        case IL::OpCode::IsInst:
            return VisitIsInst(inst);
        case IL::OpCode::StLoc:
            return VisitStLoc(inst);
        case IL::OpCode::StObj:
            return VisitStObj(inst);
        case IL::OpCode::Nop:
            return VisitNop(inst);
        case IL::OpCode::IfInstruction:
            return VisitIfInstruction(inst);
        case IL::OpCode::Branch:
            return VisitBranch(inst);
        case IL::OpCode::Leave:
            return VisitLeave(inst);
        case IL::OpCode::Throw:
            return VisitThrow(inst);
        case IL::OpCode::Rethrow:
            return VisitRethrow(inst);
        case IL::OpCode::YieldReturn:
            return VisitYieldReturn(inst);
        default:
            return Default(inst);
    }
}

// The C# `protected override TranslatedStatement Default(ILInstruction inst)`
// (line 91): an expression statement over the expression translation -- the C#'s
// own fallback for instructions with no dedicated statement Visit method.
TranslatedStatement StatementBuilder::Default(IL::ILInstruction* inst)
{
    auto* expression = new Syntax::ExpressionStatement(
        exprBuilder->Translate(inst).Expression());
    return WithILInstruction(*expression, inst);
}

// The C# `protected internal override TranslatedStatement VisitIsInst(IsInst inst)`
// (lines 97-114): an unused-result `is` test. The C# remark: "isinst on top-level
// (unused result) can be translated in general (even for value types) by using
// 'is' instead of 'as'. This can happen when the result of 'expr is T' is unused
// and the C# compiler optimizes away the null check portion of the 'is' operator."
TranslatedStatement StatementBuilder::VisitIsInst(IL::ILInstruction* inst)
{
    auto* isInst = static_cast<IL::IsInst*>(inst);
    TranslatedExpression arg = exprBuilder->Translate(isInst->Argument.get());
    arg = ExpressionBuilder::UnwrapBoxingConversion(arg);
    auto* isExpr = new Syntax::IsExpression(arg.Expression(),
                                            exprBuilder->ConvertType(*isInst->Type));
    WithRR(*isExpr, std::make_shared<Sem::ResolveResult>(
                        const_cast<TS::IType&>(exprBuilder->compilation->FindType(
                            TS::KnownTypeCode::Boolean))
                            .shared_from_this()));
    // The C# inner `.WithILInstruction(inst)` attaches the annotation to the
    // IsExpression itself, and the outer one to the ExpressionStatement -- the
    // statement carries both.
    WithILInstruction(*isExpr, inst);
    return WithILInstruction(*new Syntax::ExpressionStatement(isExpr), inst);
}

// The C# `protected internal override TranslatedStatement VisitStLoc(StLoc inst)`
// (lines 116-125) / its VisitStObj sibling (127-136): the statement-level store
// renders the assignment expression and strips the top-level ref on ref
// re-assignment (the assignment's own DirectionExpression wrapper).
TranslatedStatement StatementBuilder::VisitStLoc(IL::ILInstruction* inst)
{
    TranslatedExpression expr = exprBuilder->Translate(inst);
    if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression()))
        expr = expr.UnwrapChild(dirExpr->Expression());
    return WithILInstruction(*new Syntax::ExpressionStatement(expr.Expression()), inst);
}

TranslatedStatement StatementBuilder::VisitStObj(IL::ILInstruction* inst)
{
    TranslatedExpression expr = exprBuilder->Translate(inst);
    if (auto* dirExpr = dynamic_cast<Syntax::DirectionExpression*>(expr.Expression()))
        expr = expr.UnwrapChild(dirExpr->Expression());
    return WithILInstruction(*new Syntax::ExpressionStatement(expr.Expression()), inst);
}

// The C# `protected internal override TranslatedStatement VisitNop(Nop inst)`
// (lines 138-146): an empty statement, with the nop's comment attached as trailing
// trivia when the comment is set.
TranslatedStatement StatementBuilder::VisitNop(IL::ILInstruction* inst)
{
    auto* nop = static_cast<IL::Nop*>(inst);
    auto* stmt = new Syntax::EmptyStatement();
    if (nop->Comment)
        stmt->AddTrailingTrivia(new Syntax::Comment(*nop->Comment));
    return WithILInstruction(*stmt, inst);
}

// The C# `protected internal override TranslatedStatement
// VisitIfInstruction(IfInstruction inst)` (lines 148-153): the if/else statement
// over the translated condition; a false arm that is a Nop is the C#'s no-else
// shape (`inst.FalseInst.OpCode == OpCode.Nop ? null : Convert(inst.FalseInst)`).
TranslatedStatement StatementBuilder::VisitIfInstruction(IL::ILInstruction* inst)
{
    auto* ifInst = static_cast<IL::IfInstruction*>(inst);
    Syntax::Expression* condition =
        exprBuilder->TranslateCondition(ifInst->Condition.get()).Expression();
    Syntax::Statement* trueStatement = Convert(ifInst->TrueInst.get());
    Syntax::Statement* falseStatement = nullptr;
    if (ifInst->FalseInst && ifInst->FalseInst->Op != IL::OpCode::Nop)
        falseStatement = Convert(ifInst->FalseInst.get());
    return WithILInstruction(
        *new Syntax::IfElseStatement(condition, trueStatement, falseStatement), inst);
}

// The C# `protected internal override TranslatedStatement VisitBranch(Branch inst)`
// (lines 347-362): a branch renders as the continue / goto-case / goto-label fix --
// the continue-target arm first, the case-label mapping second, and the deduped
// block-label goto last.
TranslatedStatement StatementBuilder::VisitBranch(IL::ILInstruction* inst)
{
    auto* branch = static_cast<IL::Branch*>(inst);
    if (branch->TargetBlock == continueTarget)
    {
        continueCount++;
        return WithILInstruction(*new Syntax::ContinueStatement(), inst);
    }
    if (caseLabelMapping)
    {
        auto it = caseLabelMapping->find(branch->TargetBlock);
        if (it != caseLabelMapping->end())
        {
            if (it->second == nullptr)
                return WithILInstruction(*new Syntax::GotoDefaultStatement(), inst);
            auto caseValue =
                exprBuilder->ConvertConstantValue(it->second, /*allowImplicitConversion*/ true);
            return WithILInstruction(*new Syntax::GotoCaseStatement(caseValue.Expression()),
                                     inst);
        }
    }
    return WithILInstruction(
        *new Syntax::GotoStatement(EnsureUniqueLabel(branch->TargetBlock)), inst);
}

// The C# `protected internal override TranslatedStatement VisitLeave(Leave inst)`
// (lines 377-423): the break / yield-break / return / goto-end fix. The port's
// nullable Leave::Value maps the C#'s non-nullable Value-with-Nop normalization: a
// null Value behaves as the C#'s `MatchNop() == true` (a value-less leave).
TranslatedStatement StatementBuilder::VisitLeave(IL::ILInstruction* inst)
{
    auto* leave = static_cast<IL::Leave*>(inst);
    if (leave->TargetContainer == breakTarget)
        return WithILInstruction(*new Syntax::BreakStatement(), inst);
    if (leave->TargetContainer == currentReturnContainer)
    {
        if (currentIsIterator)
            return WithILInstruction(*new Syntax::YieldBreakStatement(), inst);
        // A null Value is the port's shape of the C#'s Nop value (the value-less
        // leave), so it counts as MatchNop() == true.
        bool isNopValue =
            leave->Value == nullptr || dynamic_cast<IL::Nop*>(leave->Value.get()) != nullptr;
        if (!isNopValue)
        {
            // The C# `currentFunction.Kind is ILFunctionKind.ExpressionTree or
            // ILFunctionKind.Delegate` -- the lambda/expr-tree cast arm's gate.
            bool isLambdaOrExprTree = currentFunction->Kind == IL::ILFunctionKind::ExpressionTree
                                   || currentFunction->Kind == IL::ILFunctionKind::Delegate;
            assert(currentResultType != nullptr
                   && "VisitLeave: currentResultType must be set for a value-returning leave");
            TS::IType& resultType = const_cast<TS::IType&>(*currentResultType);
            TranslatedExpression expr =
                exprBuilder->Translate(leave->Value.get(), &resultType)
                    .ConvertTo(resultType, *exprBuilder, /*checkForOverflow*/ false,
                               /*allowImplicitConversion*/ true);
            if (isLambdaOrExprTree && IsPossibleLossOfTypeInformation(expr.Type(), resultType))
            {
                expr = WithoutILInstruction(WithRR(
                    *new Syntax::CastExpression(exprBuilder->ConvertType(resultType),
                                                expr.Expression()),
                    std::make_shared<Sem::ConversionResolveResult>(
                        const_cast<TS::IType*>(currentResultType)->shared_from_this(),
                        SharedResolveResultAnnotation(*expr.Expression()),
                        Sem::Conversions::IdentityConversion())));
            }
            return WithILInstruction(*new Syntax::ReturnStatement(expr.Expression()), inst);
        }
        return WithILInstruction(*new Syntax::ReturnStatement(), inst);
    }
    std::string label;
    auto it = endContainerLabels.find(leave->TargetContainer);
    if (it == endContainerLabels.end())
    {
        label = "end_" + leave->TargetLabel();
        auto dup = duplicateLabels.find(label);
        if (dup == duplicateLabels.end())
        {
            duplicateLabels.emplace(label, 1);
        }
        else
        {
            // The C# `duplicateLabels[label]++; label += "_" + (count + 1);` -- the
            // suffix uses the pre-increment count (read before the increment).
            int count = dup->second;
            dup->second++;
            label += "_" + std::to_string(count + 1);
        }
        endContainerLabels.emplace(leave->TargetContainer, label);
    }
    else
    {
        label = it->second;
    }
    return WithILInstruction(*new Syntax::GotoStatement(label), inst);
}

// The C# `protected internal override TranslatedStatement VisitThrow(Throw inst)`
// (lines 424-427): the throw statement over the translated argument.
TranslatedStatement StatementBuilder::VisitThrow(IL::ILInstruction* inst)
{
    auto* throwInst = static_cast<IL::Throw*>(inst);
    return WithILInstruction(
        *new Syntax::ThrowStatement(exprBuilder->Translate(throwInst->Argument.get()).Expression()),
        inst);
}

// The C# `protected internal override TranslatedStatement VisitRethrow(Rethrow
// inst)` (lines 429-432): a bare throw statement (no expression).
TranslatedStatement StatementBuilder::VisitRethrow(IL::ILInstruction* inst)
{
    return WithILInstruction(*new Syntax::ThrowStatement(), inst);
}

// The C# `protected internal override TranslatedStatement VisitYieldReturn(
// YieldReturn inst)` (lines 434-444): the yield return statement over the value
// converted to the element type -- the async return type when the function is an
// async iterator, else the IEnumerable unwrap of the function's return type.
TranslatedStatement StatementBuilder::VisitYieldReturn(IL::ILInstruction* inst)
{
    auto* yieldReturn = static_cast<IL::YieldReturn*>(inst);
    TS::ITypePtr elementType = currentFunction->AsyncReturnType;
    if (!elementType)
    {
        // The C# `currentFunction.ReturnType.GetElementTypeFromIEnumerable(typeSystem,
        // true, out _)` -- the discarded isGeneric.
        std::optional<bool> isGeneric;
        assert(currentFunction->ReturnType != nullptr
               && "VisitYieldReturn: ReturnType must be set for a non-async iterator");
        elementType = TS::GetElementTypeFromIEnumerable(*currentFunction->ReturnType, *typeSystem,
                                                        true, isGeneric);
    }
    TranslatedExpression expr =
        exprBuilder->Translate(yieldReturn->Value.get(), elementType.get())
            .ConvertTo(*elementType, *exprBuilder, /*checkForOverflow*/ false,
                       /*allowImplicitConversion*/ true);
    return WithILInstruction(*new Syntax::YieldReturnStatement(expr.Expression()), inst);
}

// The C# `string EnsureUniqueLabel(Block block)` (lines 1581-1597): the block's
// own label when unencountered, the `IL_xxxx_N` suffix for a repeated name.
std::string StatementBuilder::EnsureUniqueLabel(IL::Block* block)
{
    auto it = labels.find(block);
    if (it != labels.end())
        return it->second;
    const std::string blockLabel = block->Label();
    auto dup = duplicateLabels.find(blockLabel);
    if (dup == duplicateLabels.end())
    {
        labels.emplace(block, blockLabel);
        duplicateLabels.emplace(blockLabel, 1);
        return blockLabel;
    }
    std::string label = blockLabel + "_" + std::to_string(dup->second + 1);
    dup->second++;
    labels.emplace(block, label);
    return label;
}

}  // namespace ILSpy::Decompiler::CSharp
