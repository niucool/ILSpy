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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of the StatementBuilder first slice (see StatementBuilder.hpp
// for the port notes).

#include "Decompiler/CSharp/StatementBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>

namespace ILSpy::Decompiler::CSharp {

namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

StatementBuilder::StatementBuilder(const TS::ICompilation& typeSystem,
                                   const TS::ITypeResolveContext& decompilationContext,
                                   IL::ILFunction* currentFunction,
                                   const DecompilerSettings* settings,
                                   const DecompileRun* decompileRun)
    // The C# ctor: `exprBuilder = new ExpressionBuilder(this, typeSystem,
    // decompilationContext, currentFunction, settings, decompileRun,
    // cancellationToken)`, then the current-function state
    // (`currentReturnContainer = (BlockContainer)currentFunction.Body`,
    // `currentIsIterator = currentFunction.IsIterator`, `currentResultType =
    // currentFunction.IsAsync ? currentFunction.AsyncReturnType! :
    // currentFunction.ReturnType`).
    : currentReturnContainer(currentFunction != nullptr && currentFunction->Body != nullptr
                                 ? currentFunction->Body.get()
                                 : nullptr),
      currentResultType(currentFunction != nullptr
                            ? (currentFunction->AsyncReturnType != nullptr
                                   ? currentFunction->AsyncReturnType
                                   : currentFunction->ReturnType)
                            : nullptr),
      currentIsIterator(currentFunction != nullptr && currentFunction->IsIterator)
{
    assert(currentFunction != nullptr);
    exprBuilder = std::make_unique<ExpressionBuilder>(
        /*statementBuilder=*/this, typeSystem, decompilationContext, currentFunction,
        settings, decompileRun);
}

// The C# `protected override TranslatedStatement Default(ILInstruction inst)`:
// `new ExpressionStatement(exprBuilder.Translate(inst)).WithILInstruction(inst)`.
TranslatedStatement StatementBuilder::Default(IL::ILInstruction* inst) {
    TranslatedExpression expr = exprBuilder->Translate(inst);
    return WithILInstruction(*new Syntax::ExpressionStatement(expr.Expression()),
                             inst);
}

// The C# `public Statement Convert(ILInstruction inst)`:
// `cancellationToken.ThrowIfCancellationRequested(); return
// inst.AcceptVisitor(this);` -- the port dispatches through the dynamic_cast
// chain (convention (b)). This slice handles the leaf statements; the
// structured-control-flow instructions fall through to Default (the C#
// ILVisitor's default arm), which renders them as expressions -- the C#
// StatementBuilder itself does the same for any instruction whose Visit arm
// has not been reached.
TranslatedStatement StatementBuilder::Convert(IL::ILInstruction* inst) {
    assert(inst != nullptr);
    if (auto* isInst = dynamic_cast<IL::IsInst*>(inst))
        return VisitIsInst(isInst);
    if (auto* stLoc = dynamic_cast<IL::StLoc*>(inst))
        return VisitStLoc(stLoc);
    if (auto* stObj = dynamic_cast<IL::StObj*>(inst))
        return VisitStObj(stObj);
    if (auto* nop = dynamic_cast<IL::Nop*>(inst))
        return VisitNop(nop);
    if (auto* throwInst = dynamic_cast<IL::Throw*>(inst))
        return VisitThrow(throwInst);
    if (auto* rethrow = dynamic_cast<IL::Rethrow*>(inst))
        return VisitRethrow(rethrow);
    if (auto* branch = dynamic_cast<IL::Branch*>(inst))
        return VisitBranch(branch);
    if (auto* leave = dynamic_cast<IL::Leave*>(inst))
        return VisitLeave(leave);
    if (auto* ifInstruction = dynamic_cast<IL::IfInstruction*>(inst))
        return VisitIfInstruction(ifInstruction);
    if (auto* tryCatch = dynamic_cast<IL::TryCatch*>(inst))
        return VisitTryCatch(tryCatch);
    if (auto* tryFinally = dynamic_cast<IL::TryFinally*>(inst))
        return VisitTryFinally(tryFinally);
    if (auto* tryFault = dynamic_cast<IL::TryFault*>(inst))
        return VisitTryFault(tryFault);
    if (auto* lockInstruction = dynamic_cast<IL::LockInstruction*>(inst))
        return VisitLockInstruction(lockInstruction);
    if (auto* block = dynamic_cast<IL::Block*>(inst))
        return VisitBlock(block);
    return Default(inst);
}

// The C# `public BlockStatement ConvertAsBlock(ILInstruction inst)`:
// `Statement stmt = Convert(inst).WithILInstruction(inst); return stmt as
// BlockStatement ?? new BlockStatement { stmt };` -- the port's
// WithILInstruction re-wraps the converted statement (the duplicate
// annotation mirrors the C# -- Convert's arms already attached it), and the
// BlockStatement probe is the `as`/null-coalesce.
TranslatedStatement StatementBuilder::ConvertAsBlock(IL::ILInstruction* inst) {
    TranslatedStatement stmt = Convert(inst);
    Syntax::Statement* node =
        dynamic_cast<Syntax::BlockStatement*>(stmt.Statement()) != nullptr
            ? stmt.Statement()
            : nullptr;
    if (node != nullptr)
        return TranslatedStatement(node);
    auto* block = new Syntax::BlockStatement();
    block->Statements().Add(stmt.Statement());
    return TranslatedStatement(block);
}

// The C# `protected internal override TranslatedStatement VisitIsInst(IsInst
// inst)` (StatementBuilder.cs lines 97-114): isinst on top-level (unused
// result) translates in general (even for value types) by using `is` instead
// of `as` -- this happens when the result of `expr is T` is unused and the C#
// compiler optimizes away the null check portion of the `is` operator.
TranslatedStatement StatementBuilder::VisitIsInst(IL::IsInst* inst) {
    TranslatedExpression arg = exprBuilder->Translate(inst->Argument.get());
    arg = ExpressionBuilder::UnwrapBoxingConversion(std::move(arg));
    auto* isExpr =
        new Syntax::IsExpression(arg.Expression(),
                                 exprBuilder->ConvertType(*inst->Type));
    TranslatedExpression inner = WithILInstruction(
        WithRR(*isExpr,
               std::make_shared<Sem::ResolveResult>(
                   std::const_pointer_cast<TS::IType>(
                       exprBuilder->compilation->FindType(TS::KnownTypeCode::Boolean)
                           .shared_from_this()))),
        inst);
    return WithILInstruction(*new Syntax::ExpressionStatement(inner.Expression()),
                             inst);
}

// The C# `protected internal override TranslatedStatement VisitStLoc(StLoc
// inst)` (StatementBuilder.cs lines 116-126): strip the top-level ref on a ref
// re-assignment.
TranslatedStatement StatementBuilder::VisitStLoc(IL::StLoc* inst) {
    TranslatedExpression expr = exprBuilder->Translate(inst);
    // strip top-level ref on ref re-assignment
    if (auto* dirExpr =
            dynamic_cast<Syntax::DirectionExpression*>(expr.Expression())) {
        expr = expr.UnwrapChild(dirExpr->Expression());
    }
    return WithILInstruction(*new Syntax::ExpressionStatement(expr.Expression()),
                             inst);
}

// The C# `protected internal override TranslatedStatement VisitStObj(StObj
// inst)` (StatementBuilder.cs lines 127-137): same as the StLoc arm.
TranslatedStatement StatementBuilder::VisitStObj(IL::StObj* inst) {
    TranslatedExpression expr = exprBuilder->Translate(inst);
    // strip top-level ref on ref re-assignment
    if (auto* dirExpr =
            dynamic_cast<Syntax::DirectionExpression*>(expr.Expression())) {
        expr = expr.UnwrapChild(dirExpr->Expression());
    }
    return WithILInstruction(*new Syntax::ExpressionStatement(expr.Expression()),
                             inst);
}

// The C# `protected internal override TranslatedStatement VisitNop(Nop inst)`
// (StatementBuilder.cs lines 138-147): an EmptyStatement, with the
// instruction's diagnostic comment (if any) as a trailing comment.
TranslatedStatement StatementBuilder::VisitNop(IL::Nop* inst) {
    auto* stmt = new Syntax::EmptyStatement();
    if (!inst->Comment.empty()) {
        stmt->AddTrailingTrivia(new Syntax::Comment(inst->Comment));
    }
    std::fprintf(stderr, "DBG nop: comment=%d trivia=%zu\n",
                 (int)!inst->Comment.empty(), stmt->TrailingTrivia().size());
    return WithILInstruction(*stmt, inst);
}

// The C# `protected internal override TranslatedStatement VisitThrow(Throw
// inst)` (StatementBuilder.cs lines 424-428).
TranslatedStatement StatementBuilder::VisitThrow(IL::Throw* inst) {
    return WithILInstruction(
        *new Syntax::ThrowStatement(
            exprBuilder->Translate(inst->Argument.get()).Expression()),
        inst);
}

// The C# `protected internal override TranslatedStatement
// VisitRethrow(Rethrow inst)` (StatementBuilder.cs lines 429-433): a bare
// `throw;`.
TranslatedStatement StatementBuilder::VisitRethrow(IL::Rethrow* inst) {
    return WithILInstruction(*new Syntax::ThrowStatement(), inst);
}


namespace {

// The C# `inst.Value.MatchNop()` extension: whether the instruction is a
// `nop` (the port probes the Op directly).
bool StatementBuilderMatchNop(const IL::ILInstruction* inst) {
    return inst != nullptr && inst->Op == IL::OpCode::Nop;
}

// The C# `handler.Filter.MatchLdcI4(1)` extension: whether the instruction is
// an `ldc.i4 <value>` with the given constant.
bool StatementBuilderMatchLdcI4(const IL::ILInstruction* inst, std::int32_t value) {
    return inst != nullptr && inst->Op == IL::OpCode::LdcI4
        && static_cast<const IL::LdcI4*>(inst)->Value == value;
}

} // namespace

// The C# `string EnsureUniqueLabel(Block block)` (StatementBuilder.cs lines
// 1566-1582): the per-block label, with the duplicate-label `_N` suffixes
// (the C# `duplicateLabels[block.Label]++` walk).
std::string StatementBuilder::EnsureUniqueLabel(IL::Block* block) {
    auto it = labels.find(block);
    if (it != labels.end())
        return it->second;
    auto dup = duplicateLabels.find(block->Label);
    if (dup == duplicateLabels.end()) {
        labels.emplace(block, block->Label);
        duplicateLabels.emplace(block->Label, 1);
        return block->Label;
    }
    std::string label = block->Label + "_" + std::to_string(dup->second + 1);
    duplicateLabels[block->Label]++;
    labels.emplace(block, label);
    return label;
}

// The C# `protected internal override TranslatedStatement VisitBranch(Branch
// inst)` (StatementBuilder.cs lines 347-368): a `continue;` to the continue
// target, a `goto case`/`goto default` through the case-label mapping, or a
// `goto` to the uniquely-labelled block.
TranslatedStatement StatementBuilder::VisitBranch(IL::Branch* inst) {
    if (inst->TargetBlock == continueTarget) {
        continueCount++;
        return WithILInstruction(*new Syntax::ContinueStatement(), inst);
    }
    if (caseLabelMapping.has_value()) {
        auto labelIt = caseLabelMapping->find(inst->TargetBlock);
        if (labelIt != caseLabelMapping->end()) {
            if (!labelIt->second.has_value())
                return WithILInstruction(*new Syntax::GotoDefaultStatement(), inst);
            return WithILInstruction(
                *new Syntax::GotoCaseStatement(
                    exprBuilder->ConvertConstantValue(labelIt->second.value(),
                                                      /*allowImplicitConversion=*/true)
                        .Expression()),
                inst);
        }
    }
    return WithILInstruction(*new Syntax::GotoStatement(EnsureUniqueLabel(inst->TargetBlock)),
                             inst);
}

// The C# `protected internal override TranslatedStatement VisitLeave(Leave
// inst)` (StatementBuilder.cs lines 369-423): a `break;` to the break target,
// a `yield break;`/`return [expr];` when leaving the return container, or a
// `goto end_<label>;` for the enclosing-container exit.
TranslatedStatement StatementBuilder::VisitLeave(IL::Leave* inst) {
    if (inst->TargetContainer == breakTarget)
        return WithILInstruction(*new Syntax::BreakStatement(), inst);
    if (inst->TargetContainer == currentReturnContainer) {
        if (currentIsIterator)
            return WithILInstruction(*new Syntax::YieldBreakStatement(), inst);
        if (!StatementBuilderMatchNop(inst->Value.get())) {
            // The C# lambda/expr-tree arm (the IsPossibleLossOfTypeInformation
            // cast for an ILFunctionKind.ExpressionTree/Delegate function) is
            // deferred with the ILFunctionKind surface; the plain return with
            // the implicit conversion is the non-lambda path.
            TranslatedExpression expr =
                exprBuilder->Translate(inst->Value.get(), currentResultType.get())
                    .ConvertTo(const_cast<TS::IType&>(*currentResultType),
                               *exprBuilder,
                               /*checkForOverflow=*/false,
                               /*allowImplicitConversion=*/true);
            return WithILInstruction(*new Syntax::ReturnStatement(expr.Expression()),
                                     inst);
        }
        return WithILInstruction(*new Syntax::ReturnStatement(), inst);
    }
    std::string label;
    auto it = endContainerLabels.find(inst->TargetContainer);
    if (it == endContainerLabels.end()) {
        label = "end_" + std::string(inst->TargetContainer != nullptr
                                         ? inst->TargetContainer->EntryPoint() != nullptr
                                             ? inst->TargetContainer->EntryPoint()->Label
                                             : std::string()
                                         : std::string());
        auto dup = duplicateLabels.find(label);
        if (dup == duplicateLabels.end()) {
            duplicateLabels.emplace(label, 1);
        } else {
            duplicateLabels[label]++;
            label += "_" + std::to_string(dup->second);
        }
        endContainerLabels.emplace(inst->TargetContainer, label);
    } else {
        label = it->second;
    }
    return WithILInstruction(*new Syntax::GotoStatement(label), inst);
}



// The C# `protected internal override TranslatedStatement VisitIfInstruction
// (IfInstruction inst)` (StatementBuilder.cs lines 148-155).
TranslatedStatement StatementBuilder::VisitIfInstruction(IL::IfInstruction* inst) {
    TranslatedExpression condition = exprBuilder->TranslateCondition(inst->Condition.get());
    TranslatedStatement trueStatement = Convert(inst->TrueInst.get());
    // The C# `inst.FalseInst.OpCode == OpCode.Nop ? null : Convert(inst.FalseInst)`
    // -- the false statement is the raw Statement pointer (null for the Nop).
    Syntax::Statement* falseStatement =
        inst->FalseInst->Op == IL::OpCode::Nop
            ? nullptr
            : Convert(inst->FalseInst.get()).Statement();
    return WithILInstruction(
        *new Syntax::IfElseStatement(condition.Expression(),
                                     trueStatement.Statement(),
                                     falseStatement),
        inst);
}

// The C# `protected internal override TranslatedStatement VisitTryCatch
// (TryCatch inst)` (StatementBuilder.cs lines 456-485): the catch clauses with
// the variable/type/filter shape.
TranslatedStatement StatementBuilder::VisitTryCatch(IL::TryCatch* inst) {
    auto* tryCatch = new Syntax::TryCatchStatement();
    tryCatch->TryBlock(
        dynamic_cast<Syntax::BlockStatement*>(ConvertAsBlock(inst->TryBlock.get()).Statement()));
    for (const auto& handler : inst->Handlers) {
        auto* catchClause = new Syntax::CatchClause();
        // The C# `catchClause.AddAnnotation(handler)` -- the caught-exception
        // handler instruction rides the IL-instruction annotation channel.
        catchClause->AddAnnotation(
            std::make_shared<ILInstructionAnnotation>(handler.get()));
        IL::ILVariable* v = handler->Variable.get();
        if (v != nullptr) {
            // The C# `catchClause.AddAnnotation(new ILVariableResolveResult(v,
            // v.Type))` -- the variable resolve result the ambience reads.
            catchClause->AddAnnotation(
                std::make_shared<ILVariableResolveResult>(handler->Variable,
                                                          v->Type));
            if (v->StoreCount > 1 || v->LoadCount > 0 || v->AddressCount > 0) {
                catchClause->VariableName(v->Name);
                catchClause->Type(exprBuilder->ConvertType(*v->Type));
            } else if (!TS::IsKnownType(*v->Type, TS::KnownTypeCode::Object)) {
                catchClause->Type(exprBuilder->ConvertType(*v->Type));
            }
        }
        if (!StatementBuilderMatchLdcI4(handler->Filter.get(), 1))
            catchClause->Condition(
                exprBuilder->TranslateCondition(handler->Filter.get()).Expression());
        catchClause->Body(
            dynamic_cast<Syntax::BlockStatement*>(ConvertAsBlock(handler->Body.get()).Statement()));
        tryCatch->CatchClauses().Add(catchClause);
    }
    return WithILInstruction(*tryCatch, inst);
}

// The C# `protected internal override TranslatedStatement VisitTryFinally
// (TryFinally inst)` (StatementBuilder.cs lines 486-492): the MakeTryCatch
// reuse plus the `finally` block.
TranslatedStatement StatementBuilder::VisitTryFinally(IL::TryFinally* inst) {
    TranslatedStatement tryBlock = MakeTryCatch(inst->TryBlock.get());
    auto* tryCatch = static_cast<Syntax::TryCatchStatement*>(tryBlock.Statement());
    tryCatch->FinallyBlock(
        dynamic_cast<Syntax::BlockStatement*>(ConvertAsBlock(inst->FinallyBlock.get()).Statement()));
    return WithILInstruction(*tryCatch, inst);
}

// The C# `protected internal override TranslatedStatement VisitTryFault
// (TryFault inst)` (StatementBuilder.cs lines 493-505): the `try { } fault { }`
// render with the `/*try-fault*/` comment marker and the implicit `throw;`.
TranslatedStatement StatementBuilder::VisitTryFault(IL::TryFault* inst) {
    auto* tryCatch = new Syntax::TryCatchStatement();
    tryCatch->TryBlock(
        dynamic_cast<Syntax::BlockStatement*>(ConvertAsBlock(inst->TryBlock.get()).Statement()));
    TranslatedStatement fault = ConvertAsBlock(inst->FaultBlock.get());
    auto* faultBlock = static_cast<Syntax::BlockStatement*>(fault.Statement());
    auto* marker = new Syntax::EmptyStatement();
    marker->AddTrailingTrivia(new Syntax::Comment("try-fault"));
    faultBlock->Statements().InsertBefore(
        faultBlock->Statements().Count() > 0
            ? faultBlock->Statements().At(0)
            : nullptr,
        marker);
    faultBlock->Statements().Add(new Syntax::ThrowStatement());
    auto* faultCatch = new Syntax::CatchClause();
    faultCatch->Body(faultBlock);
    tryCatch->CatchClauses().Add(faultCatch);
    return WithILInstruction(*tryCatch, inst);
}

// The C# `protected internal override TranslatedStatement VisitLockInstruction
// (LockInstruction inst)` (StatementBuilder.cs lines 506-511).
TranslatedStatement StatementBuilder::VisitLockInstruction(IL::LockInstruction* inst) {
    auto* lockStatement = new Syntax::LockStatement();
    lockStatement->Expression(exprBuilder->Translate(inst->OnExpression.get()).Expression());
    lockStatement->EmbeddedStatement(
        ConvertAsBlock(inst->Body.get()).Statement());
    return WithILInstruction(*lockStatement, inst);
}


// The C# `TryCatchStatement MakeTryCatch(ILInstruction tryBlock)`
// (StatementBuilder.cs lines 88-95): the try-block conversion with the
// extend-existing-try-catch reuse (a converted TryCatchStatement with no
// finally block IS the try-catch; anything else wraps).
TranslatedStatement StatementBuilder::MakeTryCatch(IL::ILInstruction* tryBlock) {
    TranslatedStatement tryBlockConverted = Convert(tryBlock);
    auto* tryCatch =
        dynamic_cast<Syntax::TryCatchStatement*>(tryBlockConverted.Statement());
    if (tryCatch != nullptr && tryCatch->FinallyBlock() == nullptr)
        return TranslatedStatement(tryCatch); // extend existing try-catch
    Syntax::BlockStatement* wrapped =
        dynamic_cast<Syntax::BlockStatement*>(tryBlockConverted.Statement());
    if (wrapped == nullptr) {
        wrapped = new Syntax::BlockStatement();
        wrapped->Statements().Add(tryBlockConverted.Statement());
    }
    auto* result = new Syntax::TryCatchStatement();
    result->TryBlock(wrapped);
    return TranslatedStatement(result);
}


// The C# `protected internal override TranslatedStatement VisitBlock(Block
// block)` (StatementBuilder.cs lines 1280-1298): a ControlFlow block renders as
// the converted statement list (the final instruction appended when it is not
// a `nop`); the TransformToForeachWithoutDispose arm is deferred with the
// foreach surface.
TranslatedStatement StatementBuilder::VisitBlock(IL::Block* block) {
    if (block->Kind != IL::BlockKind::ControlFlow)
        return Default(block);
    // Block without container
    auto* blockStatement = new Syntax::BlockStatement();
    for (auto& instruction : block->Instructions)
        blockStatement->Statements().Add(Convert(instruction.get()).Statement());
    if (block->FinalInstruction != nullptr
        && block->FinalInstruction->Op != IL::OpCode::Nop)
        blockStatement->Statements().Add(Convert(block->FinalInstruction.get()).Statement());
    return WithILInstruction(*blockStatement, block);
}

} // namespace ILSpy::Decompiler::CSharp
