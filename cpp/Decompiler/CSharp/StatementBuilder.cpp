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
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/CaseLabel.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

#include <functional>
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
        case IL::OpCode::TryCatch:
            return VisitTryCatch(inst);
        case IL::OpCode::TryFinally:
            return VisitTryFinally(inst);
        case IL::OpCode::TryFault:
            return VisitTryFault(inst);
        case IL::OpCode::LockInstruction:
            return VisitLockInstruction(inst);
        case IL::OpCode::Initblk:
            return VisitInitblk(inst);
        case IL::OpCode::Cpblk:
            return VisitCpblk(inst);
        case IL::OpCode::Ckfinite:
            return VisitCkfinite(inst);
        case IL::OpCode::SwitchInstruction:
            return VisitSwitchInstruction(inst);
        case IL::OpCode::Block:
            return VisitBlock(inst);
        case IL::OpCode::BlockContainer:
            return VisitBlockContainer(inst);
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

// The C# `TryCatchStatement MakeTryCatch(ILInstruction tryBlock)` (lines
// 445-454): converts the try block once; a nested try-catch statement whose
// finally block is absent IS the result (the extend-existing path -- the C#
// comment '// extend existing try-catch'), and everything else wraps in a
// fresh TryCatchStatement whose try block is the converted BlockStatement or
// a new block holding it (the C# `as BlockStatement ?? new BlockStatement {
// tryBlockConverted }` collection initializer).
Syntax::TryCatchStatement* StatementBuilder::MakeTryCatch(IL::ILInstruction* tryBlock)
{
    Syntax::Statement* tryBlockConverted = Convert(tryBlock);
    if (auto* tryCatch = dynamic_cast<Syntax::TryCatchStatement*>(tryBlockConverted))
    {
        if (tryCatch->FinallyBlock() == nullptr)
            return tryCatch;  // extend existing try-catch
    }
    auto* tryCatch = new Syntax::TryCatchStatement();
    if (auto* block = dynamic_cast<Syntax::BlockStatement*>(tryBlockConverted))
    {
        tryCatch->TryBlock(block);
    }
    else
    {
        auto* blockStatement = new Syntax::BlockStatement();
        blockStatement->Statements().Add(tryBlockConverted);
        tryCatch->TryBlock(blockStatement);
    }
    return tryCatch;
}

// The C# `protected internal override TranslatedStatement VisitTryCatch(
// TryCatch inst)` (lines 456-485): the try/catch statement over the converted
// try block and one CatchClause per handler. The caught variable is annotated
// first (the handler instruction annotation, then the variable resolve
// result); it is NAMED and its type rendered only when it has a store besides
// its use (or is read/addressed), and the type alone when it is not the
// `object`-typed bare catch (`catch (T)` vs `catch`). A filter that is not
// the ldc.i4 1 constant translates to the `when` condition.
TranslatedStatement StatementBuilder::VisitTryCatch(IL::ILInstruction* inst)
{
    auto* tryCatchInst = static_cast<IL::TryCatch*>(inst);
    auto* tryCatch = new Syntax::TryCatchStatement();
    tryCatch->TryBlock(ConvertAsBlock(tryCatchInst->TryBlock.get()));
    for (const auto& handler : tryCatchInst->Handlers)
    {
        auto* catchClause = new Syntax::CatchClause();
        // The handler instruction rides the clause as the bare-IL
        // annotation (the ILInstructionAnnotation holder channel).
        catchClause->AddAnnotation(std::make_shared<ILInstructionAnnotation>(handler.get()));
        const IL::ILVariablePtr& v = handler->Variable;
        if (v != nullptr)
        {
            catchClause->AddAnnotation(
                std::make_shared<ILVariableResolveResult>(v, v->Type));
            if (v->StoreCount > 1 || v->LoadCount > 0 || v->AddressCount > 0)
            {
                catchClause->VariableName(v->Name);
                catchClause->Type(exprBuilder->ConvertType(*v->Type));
            }
            else if (!TS::IsKnownType(*v->Type, TS::KnownTypeCode::Object))
            {
                catchClause->Type(exprBuilder->ConvertType(*v->Type));
            }
        }
        if (!IL::MatchLdcI4(handler->Filter.get(), 1))
            catchClause->Condition(
                exprBuilder->TranslateCondition(handler->Filter.get()).Expression());
        catchClause->Body(ConvertAsBlock(handler->Body.get()));
        tryCatch->CatchClauses().Add(catchClause);
    }
    return WithILInstruction(*tryCatch, inst);
}

// The C# `protected internal override TranslatedStatement VisitTryFinally(
// TryFinally inst)` (lines 486-492): the finally block over the
// MakeTryCatch-reused (or freshly wrapped) try statement.
TranslatedStatement StatementBuilder::VisitTryFinally(IL::ILInstruction* inst)
{
    auto* tryFinallyInst = static_cast<IL::TryFinally*>(inst);
    Syntax::TryCatchStatement* tryCatch =
        MakeTryCatch(tryFinallyInst->TryBlock.get());
    tryCatch->FinallyBlock(ConvertAsBlock(tryFinallyInst->FinallyBlock.get()));
    return WithILInstruction(*tryCatch, inst);
}

// The C# `protected internal override TranslatedStatement VisitTryFault(
// TryFault inst)` (lines 493-505): the fault block becomes a catch clause
// body -- the empty 'try-fault' statement inserted before the block's first
// statement (FirstOrDefault() == null inserts at the head of an empty block)
// and a bare throw appended.
TranslatedStatement StatementBuilder::VisitTryFault(IL::ILInstruction* inst)
{
    auto* tryFaultInst = static_cast<IL::TryFault*>(inst);
    auto* tryCatch = new Syntax::TryCatchStatement();
    tryCatch->TryBlock(ConvertAsBlock(tryFaultInst->TryBlock.get()));
    Syntax::BlockStatement* faultBlock =
        ConvertAsBlock(tryFaultInst->FaultBlock.get());
    auto* tryFaultStatement = new Syntax::EmptyStatement();
    tryFaultStatement->AddTrailingTrivia(new Syntax::Comment("try-fault"));
    faultBlock->Statements().InsertBefore(faultBlock->Statements().FirstOrNull(),
                                          tryFaultStatement);
    faultBlock->Statements().Add(new Syntax::ThrowStatement());
    auto* catchClause = new Syntax::CatchClause();
    catchClause->Body(faultBlock);
    tryCatch->CatchClauses().Add(catchClause);
    return WithILInstruction(*tryCatch, inst);
}

// The C# `protected internal override TranslatedStatement
// VisitLockInstruction(LockInstruction inst)` (lines 506-510): the lock
// statement over the translated monitor expression and the converted body.
TranslatedStatement StatementBuilder::VisitLockInstruction(IL::ILInstruction* inst)
{
    auto* lockInst = static_cast<IL::LockInstruction*>(inst);
    auto* lockStatement = new Syntax::LockStatement();
    lockStatement->Expression(
        exprBuilder->Translate(lockInst->OnExpression.get()).Expression());
    lockStatement->EmbeddedStatement(ConvertAsBlock(lockInst->Body.get()));
    return WithILInstruction(*lockStatement, inst);
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

// The C# `protected internal override TranslatedStatement VisitInitblk(Initblk
// inst)` (lines 1609-1623): the Unsafe.InitBlock / Unsafe.InitBlockUnaligned
// intrinsic call over the (address, value, size) translations, with the
// '// IL initblk instruction' comment as leading trivia.
TranslatedStatement StatementBuilder::VisitInitblk(IL::ILInstruction* inst)
{
    auto* initblk = static_cast<IL::Initblk*>(inst);
    // The translates are sequenced through named locals (the C# evaluates the
    // argument array left to right; C++ argument evaluation order is
    // unspecified -- the established porting convention).
    Syntax::Expression* address = exprBuilder->Translate(initblk->Address.get()).Expression();
    Syntax::Expression* value = exprBuilder->Translate(initblk->Value.get()).Expression();
    Syntax::Expression* size = exprBuilder->Translate(initblk->Size.get()).Expression();
    auto* stmt = new Syntax::ExpressionStatement(
        exprBuilder->CallUnsafeIntrinsic(
            initblk->UnalignedPrefix != 0 ? "InitBlockUnaligned" : "InitBlock",
            {address, value, size},
            exprBuilder->compilation->FindType(TS::KnownTypeCode::Void), inst)
            .Expression());
    stmt->AddLeadingTrivia(new Syntax::Comment(" IL initblk instruction"));
    return WithILInstruction(*stmt, inst);
}

// The C# `protected internal override TranslatedStatement VisitCpblk(Cpblk inst)`
// (lines 1627-1641): the Unsafe.CopyBlock / Unsafe.CopyBlockUnaligned sibling.
TranslatedStatement StatementBuilder::VisitCpblk(IL::ILInstruction* inst)
{
    auto* cpblk = static_cast<IL::Cpblk*>(inst);
    Syntax::Expression* destAddress =
        exprBuilder->Translate(cpblk->DestAddress.get()).Expression();
    Syntax::Expression* sourceAddress =
        exprBuilder->Translate(cpblk->SourceAddress.get()).Expression();
    Syntax::Expression* size = exprBuilder->Translate(cpblk->Size.get()).Expression();
    auto* stmt = new Syntax::ExpressionStatement(
        exprBuilder->CallUnsafeIntrinsic(
            cpblk->UnalignedPrefix != 0 ? "CopyBlockUnaligned" : "CopyBlock",
            {destAddress, sourceAddress, size},
            exprBuilder->compilation->FindType(TS::KnownTypeCode::Void), inst)
            .Expression());
    stmt->AddLeadingTrivia(new Syntax::Comment(" IL cpblk instruction"));
    return WithILInstruction(*stmt, inst);
}

// The C# `protected internal override TranslatedStatement VisitCkfinite(Ckfinite
// inst)` (lines 1645-1669): the `if (!float.IsFinite(<arg>)) throw new
// ArithmeticException();` guard.
TranslatedStatement StatementBuilder::VisitCkfinite(IL::ILInstruction* inst)
{
    auto* ckfinite = static_cast<IL::Ckfinite*>(inst);
    auto* isFiniteCall = new Syntax::InvocationExpression();
    auto* target = new Syntax::MemberReferenceExpression();
    target->Target(new Syntax::TypeReferenceExpression(new Syntax::PrimitiveType("float")));
    target->MemberName("IsFinite");
    isFiniteCall->Target(target);
    isFiniteCall->Arguments().Add(
        exprBuilder->Translate(ckfinite->Argument.get()).Expression());
    // The C# `typeSystem.FindType(typeof(ArithmeticException))` -- the Type input
    // resolves through the full name over the compilation's modules (the port's
    // FindType(compilation, FullTypeName) form).
    TS::ITypePtr arithmeticException =
        TS::FindType(*typeSystem, TS::FullTypeName("System.ArithmeticException"));
    auto* arithmeticExceptionSyntax = new Syntax::SimpleType("ArithmeticException");
    arithmeticExceptionSyntax->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(arithmeticException));
    return WithILInstruction(
        *new Syntax::IfElseStatement(
            new Syntax::UnaryOperatorExpression(isFiniteCall, Syntax::UnaryOperatorType::Not),
            new Syntax::ThrowStatement(
                new Syntax::ObjectCreateExpression(arithmeticExceptionSyntax))),
        inst);
}

// The C# `internal IEnumerable<ConstantResolveResult> CreateTypedCaseLabel(
// long i, IType type, List<(string? Key, int Value)>? map = null)` (lines
// 156-202): see the header comment for the arm contract.
std::vector<std::shared_ptr<Sem::ConstantResolveResult>>
StatementBuilder::CreateTypedCaseLabel(
    long long i, TS::IType& type,
    const std::vector<std::pair<std::optional<std::string>, int>>* map)
{
    std::vector<std::shared_ptr<Sem::ConstantResolveResult>> labels;
    // unpack nullable type, if necessary: we need to do this in all cases,
    // because there are nullable bools and enum types as well.
    TS::IType& unwrapped = TS::GetUnderlyingType(type);
    std::any value;
    if (TS::IsKnownType(unwrapped, TS::KnownTypeCode::Boolean))
    {
        value = i != 0;
    }
    else if (map != nullptr)
    {
        // The C# Debug.Assert(type.IsKnownType(KnownTypeCode.String)).
        // One label per key mapping to i (the C# Where/Select over the map). A
        // null key boxes as the C# null literal (an empty any -- the null box is
        // the C#'s null reference, not a null-holding container).
        for (const auto& entry : *map)
        {
            if (entry.second == i)
                labels.push_back(std::make_shared<Sem::ConstantResolveResult>(
                    TS::ITypePtr(const_cast<TS::IType&>(unwrapped).shared_from_this()),
                    entry.first.has_value() ? std::any(*entry.first) : std::any()));
        }
        return labels;
    }
    else if (unwrapped.Kind() == TS::TypeKind::Enum)
    {
        // The C# `type.GetDefinition()!.EnumUnderlyingType` -- the definition's
        // underlying primitive (a null underlying type keeps the raw long).
        TS::ITypePtr enumType;
        if (const TS::ITypeDefinition* definition = unwrapped.GetDefinition())
            enumType = definition->EnumUnderlyingType();
        TS::TypeCode typeCode =
            enumType ? TS::GetTypeCode(*enumType) : TS::TypeCode::Empty;
        if (typeCode != TS::TypeCode::Empty)
            value = Util::Cast(typeCode, std::any(i), /*checkForOverflow*/ false);
        else
            value = i;
    }
    else
    {
        TS::TypeCode typeCode = TS::GetTypeCode(unwrapped);
        if (typeCode != TS::TypeCode::Empty)
            value = Util::Cast(typeCode, std::any(i), /*checkForOverflow*/ false);
        else
            value = i;
    }
    labels.push_back(std::make_shared<Sem::ConstantResolveResult>(
        TS::ITypePtr(const_cast<TS::IType&>(unwrapped).shared_from_this()), value));
    return labels;
}

// The C# `private void ConvertSwitchSectionBody(Syntax.SwitchSection astSection,
// ILInstruction bodyInst)` (lines 321-346): the converted body plus the
// EndPointUnreachable-gated break insertion.
void StatementBuilder::ConvertSwitchSectionBody(Syntax::SwitchSection* astSection,
                                                  IL::ILInstruction* bodyInst)
{
    Syntax::Statement* body = Convert(bodyInst);
    astSection->Statements().Add(body);
    if ((bodyInst->Flags() & IL::InstructionFlags::EndPointUnreachable)
        != IL::InstructionFlags::EndPointUnreachable)
    {
        // we need to insert 'break;'
        if (auto* block = dynamic_cast<Syntax::BlockStatement*>(body))
        {
            block->Statements().Add(new Syntax::BreakStatement());
        }
        else
        {
            astSection->Statements().Add(new Syntax::BreakStatement());
        }
    }
}

// The C# `SwitchStatement TranslateSwitch(BlockContainer? switchContainer,
// SwitchInstruction inst)` (lines 208-320): see the header comment for the
// contract. The C# `container.Descendants.OfType<Branch>()` enumerations port to
// the recursive walker (the SwitchDetection WalkContainers precedent).
Syntax::SwitchStatement* StatementBuilder::TranslateSwitch(IL::BlockContainer* switchContainer,
                                                            IL::SwitchInstruction& inst)
{
    IL::BlockContainer* oldBreakTarget = breakTarget;
    breakTarget = switchContainer;  // 'break' within a switch would only leave the switch
    std::optional<CaseLabelMapping> oldCaseLabelMapping = std::move(caseLabelMapping);
    caseLabelMapping = CaseLabelMapping{};

    auto [value, type, strToInt] =
        exprBuilder->TranslateSwitchValue(inst, /*isExpressionContext*/ false);

    IL::SwitchSection* defaultSection = inst.GetDefaultSection();

    auto* stmt = new Syntax::SwitchStatement(value.Expression());
    std::unordered_map<IL::SwitchSection*, Syntax::SwitchSection*> translationDictionary;
    // The `switchContainer.Descendants.OfType<Branch>().Where(b => b.TargetBlock ==
    // br.TargetBlock).All(...)` inline gate (the C# computes it twice): all
    // branches to the section's target block must live in THIS switch container
    // (no branch from a nested switch container may share the block).
    auto sectionInlinable = [&](IL::Branch* br) {
        if (br->TargetContainer() != switchContainer || switchContainer == nullptr)
            return false;
        bool all = true;
        std::function<void(IL::ILInstruction*)> walk = [&](IL::ILInstruction* node) {
            if (!all || node == nullptr) return;
            if (auto* b = dynamic_cast<IL::Branch*>(node))
            {
                if (b->TargetBlock == br->TargetBlock
                    && IL::BlockContainer::FindClosestSwitchContainer(b) != switchContainer)
                    all = false;
            }
            for (int c = 0; c < node->ChildCount(); ++c) walk(node->GetChild(c));
        };
        walk(switchContainer);
        return all;
    };
    // initialize C# switch sections.
    for (auto& section : inst.Sections)
    {
        // This is used in the block-label mapping.
        std::shared_ptr<Sem::ConstantResolveResult> firstValueResolveResult;
        auto* astSection = new Syntax::SwitchSection();
        // Create case labels:
        if (section.get() == defaultSection)
        {
            astSection->CaseLabels().Add(new Syntax::CaseLabel());
            firstValueResolveResult = nullptr;
        }
        else
        {
            const std::vector<std::pair<std::optional<std::string>, int>>* strMap =
                strToInt != nullptr ? &strToInt->Map : nullptr;
            std::vector<std::shared_ptr<Sem::ConstantResolveResult>> values;
            for (long long labelValue : section->Labels.Values())
            {
                for (auto& typed : CreateTypedCaseLabel(labelValue,
                                                        const_cast<TS::IType&>(*type),
                                                        strMap))
                    values.push_back(std::move(typed));
            }
            if (section->HasNullLabel)
            {
                astSection->CaseLabels().Add(
                    new Syntax::CaseLabel(new Syntax::NullReferenceExpression()));
                firstValueResolveResult = std::make_shared<Sem::ConstantResolveResult>(
                    TS::NullType(), nullptr);
            }
            else
            {
                firstValueResolveResult = values.empty() ? nullptr : values.front();
            }
            for (auto& label : values)
            {
                astSection->CaseLabels().Add(new Syntax::CaseLabel(
                    exprBuilder
                        ->ConvertConstantValue(label, /*allowImplicitConversion*/ true)
                        .Expression()));
            }
        }
        if (auto* br = dynamic_cast<IL::Branch*>(section->Body.get()))
        {
            // we can only inline the block, if all branches are in the switchContainer.
            if (sectionInlinable(br))
                caseLabelMapping->emplace(br->TargetBlock,
                                           std::move(firstValueResolveResult));
        }
        translationDictionary.emplace(section.get(), astSection);
        stmt->SwitchSections().Add(astSection);
    }
    for (auto& section : inst.Sections)
    {
        Syntax::SwitchSection* astSection = translationDictionary[section.get()];
        if (auto* br = dynamic_cast<IL::Branch*>(section->Body.get()))
        {
            // we can only inline the block, if all branches are in the switchContainer.
            if (sectionInlinable(br))
                ConvertSwitchSectionBody(astSection, br->TargetBlock);
            else
                ConvertSwitchSectionBody(astSection, section->Body.get());
        }
        else if (auto* leave = dynamic_cast<IL::Leave*>(section->Body.get()))
        {
            if (astSection->CaseLabels().Count() == 1
                && astSection->CaseLabels()[0]->Expression() == nullptr
                && leave->TargetContainer == switchContainer)
            {
                stmt->SwitchSections().Remove(astSection);
                continue;
            }
            ConvertSwitchSectionBody(astSection, section->Body.get());
        }
        else
        {
            ConvertSwitchSectionBody(astSection, section->Body.get());
        }
    }
    if (switchContainer != nullptr && stmt->SwitchSections().Count() > 0)
    {
        // Translate any remaining blocks:
        auto& lastSectionStatements =
            stmt->SwitchSections()[stmt->SwitchSections().Count() - 1]->Statements();
        for (std::size_t blockIndex = 1; blockIndex < switchContainer->Blocks.size();
             ++blockIndex)
        {
            IL::Block* block = switchContainer->Blocks[blockIndex].get();
            if (caseLabelMapping->find(block) != caseLabelMapping->end())
                continue;
            lastSectionStatements.Add(
                new Syntax::LabelStatement(EnsureUniqueLabel(block)));
            for (auto& nestedInst : block->Instructions)
            {
                Syntax::Statement* nestedStmt = Convert(nestedInst.get());
                if (auto* b = dynamic_cast<Syntax::BlockStatement*>(nestedStmt))
                {
                    for (std::size_t s = 0; s < b->Statements().Count(); ++s)
                        lastSectionStatements.Add(Syntax::Detach(b->Statements()[s]));
                }
                else
                {
                    lastSectionStatements.Add(nestedStmt);
                }
            }
            // The C# Debug.Assert(block.FinalInstruction.OpCode == OpCode.Nop);
            // the port's nullable FinalInstruction treats a null as the Nop
            // shape (the VisitLeave convention).
        }
        if (auto it = endContainerLabels.find(switchContainer); it != endContainerLabels.end())
        {
            lastSectionStatements.Add(new Syntax::LabelStatement(it->second));
            lastSectionStatements.Add(new Syntax::BreakStatement());
        }
    }

    breakTarget = oldBreakTarget;
    caseLabelMapping = std::move(oldCaseLabelMapping);
    return stmt;
}

// The C# `protected internal override TranslatedStatement
// VisitSwitchInstruction(SwitchInstruction inst)` (line 203): the switch
// statement over TranslateSwitch with no switch container (the container-driven
// shape comes through the VisitBlockContainer arm).
TranslatedStatement StatementBuilder::VisitSwitchInstruction(IL::ILInstruction* inst)
{
    return WithILInstruction(
        *TranslateSwitch(nullptr, *static_cast<IL::SwitchInstruction*>(inst)), inst);
}

// The C# `protected internal override TranslatedStatement VisitBlock(Block block)`
// (line 1280): see the header comment for the contract. The
// TransformToForeachWithoutDispose arm is the documented deferral (a null
// return -- every statement converts through the normal path).
TranslatedStatement StatementBuilder::VisitBlock(IL::ILInstruction* inst)
{
    auto* block = static_cast<IL::Block*>(inst);
    if (block->Kind != IL::BlockKind::ControlFlow)
        return Default(block);
    // Block without container
    auto* blockStatement = new Syntax::BlockStatement();
    for (std::size_t i = 0; i < block->Instructions.size(); i++)
    {
        // The C# `if (TransformToForeachWithoutDispose(block, ref i) is Statement
        // foreachStmt)` -- the foreach machinery deferral: no statement converts
        // through the foreach shape yet.
        blockStatement->Statements().Add(Convert(block->Instructions[i].get()));
    }
    // The port's nullable FinalInstruction: null is the C#'s Nop shape.
    if (block->FinalInstruction != nullptr
        && block->FinalInstruction->Op != IL::OpCode::Nop)
        blockStatement->Statements().Add(Convert(block->FinalInstruction.get()));
    return WithILInstruction(*blockStatement, block);
}

// The C# `static bool IsFinalLeave(Leave leave)` (lines 1599-1608): the
// value-less leave that is the very last instruction of the container's last
// block targeting that container.
bool StatementBuilder::IsFinalLeave(IL::Leave* leave)
{
    // The C# `!leave.Value.MatchNop()` -- the port's null Value is the Nop shape.
    if (!(leave->Value == nullptr || leave->Value->Op == IL::OpCode::Nop))
        return false;
    auto* block = static_cast<IL::Block*>(leave->Parent);
    if (leave->ChildIndex != static_cast<int>(block->Instructions.size()) - 1
        || !(block->FinalInstruction == nullptr
             || block->FinalInstruction->Op == IL::OpCode::Nop))
        return false;
    auto* container = static_cast<IL::BlockContainer*>(block->Parent);
    return block->ChildIndex == static_cast<int>(container->Blocks.size()) - 1
           && container == leave->TargetContainer;
}

// The C# `protected internal override TranslatedStatement
// VisitBlockContainer(BlockContainer container)` (lines 1300-1320): the loop /
// switch-entry / plain-block dispatch.
TranslatedStatement StatementBuilder::VisitBlockContainer(IL::ILInstruction* inst)
{
    auto* container = static_cast<IL::BlockContainer*>(inst);
    if (container->Kind != IL::ContainerKind::Normal
        && container->EntryPoint()->IncomingEdgeCount > 1)
    {
        IL::Block* oldContinueTarget = continueTarget;
        int oldContinueCount = continueCount;
        IL::BlockContainer* oldBreakTarget = breakTarget;
        Syntax::Statement* loop = ConvertLoop(container);
        // The C# `loop.AddAnnotation(container)` -- the container is annotated
        // DIRECTLY (the ILInstructionAnnotation channel); the WithILInstruction
        // wrap below adds it a second time (the C# AddAnnotation does not
        // dedupe).
        loop->AddAnnotation(std::make_shared<ILInstructionAnnotation>(container));
        continueTarget = oldContinueTarget;
        continueCount = oldContinueCount;
        breakTarget = oldBreakTarget;
        return WithILInstruction(*loop, container);
    }
    else if (container->EntryPoint()->Instructions.size() == 1
             && container->EntryPoint()->Instructions[0]->Op == IL::OpCode::SwitchInstruction)
    {
        return WithILInstruction(
            *TranslateSwitch(
                container,
                *static_cast<IL::SwitchInstruction*>(
                    container->EntryPoint()->Instructions[0].get())),
            container);
    }
    else
    {
        return WithILInstruction(*ConvertBlockContainer(container, false), container);
    }
}

// The C# `Statement ConvertLoop(BlockContainer container)` (lines 1321-1430):
// see the header comment for the per-kind contract. The DeclareLocalFunctions
// calls are the documented deferral (a no-op -- the seed pipeline produces no
// local functions).
Syntax::Statement* StatementBuilder::ConvertLoop(IL::BlockContainer* container)
{
    IL::ILInstruction* condition = nullptr;
    IL::Block* loopBody = nullptr;
    Syntax::BlockStatement* blockStatement = nullptr;
    continueCount = 0;
    breakTarget = container;
    switch (container->Kind)
    {
        case IL::ContainerKind::Loop:
        {
            continueTarget = container->EntryPoint();
            blockStatement = ConvertBlockContainer(container, true);
            assert(continueCount < container->EntryPoint()->IncomingEdgeCount);
            if (container->EntryPoint()->IncomingEdgeCount == continueCount + 1)
            {
                // Remove the entrypoint label if all jumps to the label were replaced with
                // 'continue;' statements
                blockStatement->Statements().Remove(blockStatement->Statements().FirstOrNull());
            }
            if (auto* continueStmt =
                    dynamic_cast<Syntax::ContinueStatement*>(
                        blockStatement->Statements().LastOrNull()))
                continueStmt->Remove();
            return new Syntax::WhileStatement(new Syntax::PrimitiveExpression(true),
                                              blockStatement);
        }
        case IL::ContainerKind::While:
        {
            continueTarget = container->EntryPoint();
            if (!container->MatchConditionBlock(continueTarget, condition, loopBody))
                throw std::invalid_argument("Invalid condition block in while loop.");
            blockStatement = ConvertAsBlock(loopBody);
            if ((loopBody->Flags() & IL::InstructionFlags::EndPointUnreachable)
                != IL::InstructionFlags::EndPointUnreachable)
                blockStatement->Statements().Add(new Syntax::BreakStatement());
            // The C# `container.Blocks.Skip(1).Except(new[] { loopBody })`.
            std::vector<IL::Block*> rest;
            for (std::size_t i = 1; i < container->Blocks.size(); i++)
                if (container->Blocks[i].get() != loopBody)
                    rest.push_back(container->Blocks[i].get());
            blockStatement = ConvertBlockContainer(blockStatement, container, rest, true);
            assert(continueCount < container->EntryPoint()->IncomingEdgeCount);
            if (continueCount + 1 < container->EntryPoint()->IncomingEdgeCount)
            {
                // There's an incoming edge to the entry point (=while condition) that wasn't
                // represented as "continue;" -> emit a real label.
                // We'll also remove any "continue;" in front of the label, as it's redundant.
                if (dynamic_cast<Syntax::ContinueStatement*>(
                        blockStatement->Statements().LastOrNull())
                    != nullptr)
                    blockStatement->Statements().Remove(
                        blockStatement->Statements().LastOrNull());
                blockStatement->Statements().Add(
                    new Syntax::LabelStatement(EnsureUniqueLabel(container->EntryPoint())));
            }
            if (auto* continueStmt =
                    dynamic_cast<Syntax::ContinueStatement*>(
                        blockStatement->Statements().LastOrNull()))
                continueStmt->Remove();
            return new Syntax::WhileStatement(
                exprBuilder->TranslateCondition(condition).Expression(), blockStatement);
        }
        case IL::ContainerKind::DoWhile:
        {
            continueTarget = container->Blocks.back().get();
            if (!container->MatchConditionBlock(continueTarget, condition, loopBody))
                throw std::invalid_argument("Invalid condition block in do-while loop.");
            // The C# `container.Blocks.SkipLast(1)`.
            std::vector<IL::Block*> rest;
            for (std::size_t i = 0; i + 1 < container->Blocks.size(); i++)
                rest.push_back(container->Blocks[i].get());
            blockStatement = ConvertBlockContainer(new Syntax::BlockStatement(), container, rest,
                                                   true);
            if (container->EntryPoint()->IncomingEdgeCount == 2)
            {
                // Remove the entry-point label, if there are only two jumps to the entry-point:
                // from outside the loop and from the condition-block.
                blockStatement->Statements().Remove(blockStatement->Statements().FirstOrNull());
            }
            if (auto* continueStmt =
                    dynamic_cast<Syntax::ContinueStatement*>(
                        blockStatement->Statements().LastOrNull()))
                continueStmt->Remove();
            if (continueTarget->IncomingEdgeCount > continueCount)
            {
                // if there are branches to the condition block, that were not converted
                // to continue statements, we have to introduce an extra label.
                blockStatement->Statements().Add(
                    new Syntax::LabelStatement(EnsureUniqueLabel(continueTarget)));
            }
            if (blockStatement->Statements().Count() == 0)
            {
                auto* whileStatement = new Syntax::WhileStatement(
                    exprBuilder->TranslateCondition(condition).Expression(), blockStatement);
                return whileStatement;
            }
            auto* doWhile = new Syntax::DoWhileStatement();
            doWhile->EmbeddedStatement(blockStatement);
            doWhile->Condition(exprBuilder->TranslateCondition(condition).Expression());
            return doWhile;
        }
        case IL::ContainerKind::For:
        {
            continueTarget = container->Blocks.back().get();
            if (!container->MatchConditionBlock(container->EntryPoint(), condition, loopBody))
                throw std::invalid_argument("Invalid condition block in for loop.");
            blockStatement = ConvertAsBlock(loopBody);
            if ((loopBody->Flags() & IL::InstructionFlags::EndPointUnreachable)
                != IL::InstructionFlags::EndPointUnreachable)
                blockStatement->Statements().Add(new Syntax::BreakStatement());
            if (!container->MatchIncrementBlock(continueTarget))
                throw std::invalid_argument("Invalid increment block in for loop.");
            // The C# `container.Blocks.SkipLast(1).Skip(1).Except(new[] { loopBody })`.
            std::vector<IL::Block*> rest;
            for (std::size_t i = 1; i + 1 < container->Blocks.size(); i++)
                if (container->Blocks[i].get() != loopBody)
                    rest.push_back(container->Blocks[i].get());
            blockStatement = ConvertBlockContainer(blockStatement, container, rest, true);
            auto* forStatement = new Syntax::ForStatement();
            forStatement->Condition(exprBuilder->TranslateCondition(condition).Expression());
            forStatement->EmbeddedStatement(blockStatement);
            if (auto* continueStmt =
                    dynamic_cast<Syntax::ContinueStatement*>(
                        blockStatement->Statements().LastOrNull()))
                continueStmt->Remove();
            for (std::size_t i = 0; i + 1 < continueTarget->Instructions.size(); i++)
            {
                forStatement->Iterators().Add(Convert(continueTarget->Instructions[i].get()));
            }
            if (continueTarget->IncomingEdgeCount > continueCount)
                blockStatement->Statements().Add(
                    new Syntax::LabelStatement(EnsureUniqueLabel(continueTarget)));
            return forStatement;
        }
        default:
            // The C# `throw new ArgumentOutOfRangeException()`.
            throw std::out_of_range("container->Kind");
    }
}

// The C# `BlockStatement ConvertBlockContainer(BlockContainer container, bool
// isLoop)` (lines 1432-1465): the wrapper. The DeclareLocalFunctions call is
// the documented deferral (the TypeSystemAstBuilder.ConvertEntity long pole);
// the EmitAsRefReadOnly helper emission keeps its C# gate (the CallBuilder
// EnforceExplicitIn write is the only setter).
Syntax::BlockStatement* StatementBuilder::ConvertBlockContainer(IL::BlockContainer* container,
                                                                 bool isLoop)
{
    // The C# worker call over `container.Blocks` (a copy of the block pointers).
    std::vector<IL::Block*> allBlocks;
    for (auto& b : container->Blocks)
        allBlocks.push_back(b.get());
    auto* blockStatement = ConvertBlockContainer(new Syntax::BlockStatement(), container,
                                                  allBlocks, isLoop);
    // DeclareLocalFunctions(currentFunction, container, blockStatement) -- the
    // documented local-function deferral (a no-op until the
    // TypeSystemAstBuilder.ConvertEntity machinery lands).
    if (currentFunction->Body.get() == container)
    {
        if (EmitAsRefReadOnly)
        {
            auto* methodDecl = new Syntax::MethodDeclaration();
            if (settings->StaticLocalFunctions())
            {
                methodDecl->Modifiers(Syntax::Modifiers::Static);
            }

            auto* returnType = new Syntax::ComposedType();
            returnType->HasReadOnlySpecifier(true);
            returnType->HasRefSpecifier(true);
            returnType->BaseType(new Syntax::SimpleType("T"));
            methodDecl->ReturnType(returnType);
            methodDecl->Name("ILSpyHelper_AsRefReadOnly");
            methodDecl->TypeParameters().Add(new Syntax::TypeParameterDeclaration("T"));
            auto* paramDecl = new Syntax::ParameterDeclaration();
            paramDecl->ParameterModifier(TS::ReferenceKind::In);
            paramDecl->Type(new Syntax::SimpleType("T"));
            paramDecl->Name("temp");
            methodDecl->Parameters().Add(paramDecl);

            auto* body = new Syntax::BlockStatement();
            methodDecl->Body(body);
            auto* commentStatement = new Syntax::EmptyStatement();
            commentStatement->AddTrailingTrivia(new Syntax::Comment(
                "ILSpy generated this function to help ensure overload resolution can pick the"
                " overload using 'in'"));
            body->Statements().Add(commentStatement);
            body->Statements().Add(new Syntax::ReturnStatement(new Syntax::DirectionExpression(
                Syntax::FieldDirection::Ref, new Syntax::IdentifierExpression("temp"))));

            blockStatement->Statements().Add(
                new Syntax::LocalFunctionDeclarationStatement(methodDecl));
        }
    }
    return blockStatement;
}

// The C# `BlockStatement ConvertBlockContainer(BlockStatement blockStatement,
// BlockContainer container, IEnumerable<Block> blocks, bool isLoop)` (lines
// 1529-1593): the worker. The TransformToForeachWithoutDispose arm is the
// documented deferral (a null return).
Syntax::BlockStatement* StatementBuilder::ConvertBlockContainer(
    Syntax::BlockStatement* blockStatement, IL::BlockContainer* container,
    const std::vector<IL::Block*>& blocks, bool isLoop)
{
    for (IL::Block* block : blocks)
    {
        if (block->IncomingEdgeCount > 1 || block != container->EntryPoint())
        {
            // If there are any incoming branches to this block, add a label:
            blockStatement->Statements().Add(
                new Syntax::LabelStatement(EnsureUniqueLabel(block)));
        }
        for (std::size_t i = 0; i < block->Instructions.size(); i++)
        {
            IL::ILInstruction* inst = block->Instructions[i].get();
            if (!isLoop)
            {
                // The C# `inst is Leave leave && IsFinalLeave(leave)` -- the cast
                // guards the helper call.
                auto* leave = dynamic_cast<IL::Leave*>(inst);
                if (leave != nullptr && IsFinalLeave(leave))
                {
                    // skip the final 'leave' instruction and just fall out of the BlockStatement
                    blockStatement->AddAnnotation(
                        std::make_shared<ImplicitReturnAnnotation>(leave));
                    continue;
                }
            }
            // The C# `TransformToForeachWithoutDispose(block, ref i) ?? Convert(inst)`
            // -- the foreach machinery deferral keeps the plain conversion.
            Syntax::Statement* stmt = Convert(inst);
            if (auto* b = dynamic_cast<Syntax::BlockStatement*>(stmt))
            {
                for (std::size_t s = 0; s < b->Statements().Count(); ++s)
                    blockStatement->Statements().Add(Syntax::Detach(b->Statements()[s]));
            }
            else
            {
                blockStatement->Statements().Add(Syntax::Detach(stmt));
            }
        }
        // The port's nullable FinalInstruction: null is the C#'s Nop shape.
        if (block->FinalInstruction != nullptr
            && block->FinalInstruction->Op != IL::OpCode::Nop)
        {
            blockStatement->Statements().Add(Convert(block->FinalInstruction.get()));
        }
    }
    if (auto it = endContainerLabels.find(container); it != endContainerLabels.end())
    {
        if (isLoop
            && dynamic_cast<Syntax::ContinueStatement*>(
                   blockStatement->Statements().LastOrNull())
                   == nullptr)
        {
            blockStatement->Statements().Add(new Syntax::ContinueStatement());
        }
        blockStatement->Statements().Add(new Syntax::LabelStatement(it->second));
        if (isLoop)
        {
            blockStatement->Statements().Add(new Syntax::BreakStatement());
        }
    }
    return blockStatement;
}

}  // namespace ILSpy::Decompiler::CSharp
