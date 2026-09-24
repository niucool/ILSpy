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
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoCaseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoDefaultStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/GotoStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/YieldBreakStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/Util/Interval.hpp"
#include "Decompiler/Util/LongSet.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

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
    // The C# ctor assigns the field before the ExpressionBuilder.
    this->currentFunction = currentFunction;
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
    if (auto* pinnedRegion = dynamic_cast<IL::PinnedRegion*>(inst))
        return VisitPinnedRegion(pinnedRegion);
    if (auto* switchInstruction = dynamic_cast<IL::SwitchInstruction*>(inst))
        return VisitSwitchInstruction(switchInstruction);
    if (auto* usingInstruction = dynamic_cast<IL::UsingInstruction*>(inst))
        return VisitUsingInstruction(usingInstruction);
    if (auto* initblk = dynamic_cast<IL::Initblk*>(inst))
        return VisitInitblk(initblk);
    if (auto* cpblk = dynamic_cast<IL::Cpblk*>(inst))
        return VisitCpblk(cpblk);
    if (auto* ckfinite = dynamic_cast<IL::Ckfinite*>(inst))
        return VisitCkfinite(ckfinite);
    if (auto* blockContainer = dynamic_cast<IL::BlockContainer*>(inst))
        return VisitBlockContainer(blockContainer);
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

// The C# `br.TargetContainer` (Branch.cs line 69): the target block's parent
// container (the C# `targetBlock?.Parent` downcast).
IL::BlockContainer* BranchTargetContainer(const IL::Branch* br) {
    if (br == nullptr || br->TargetBlock == nullptr)
        return nullptr;
    return dynamic_cast<IL::BlockContainer*>(br->TargetBlock->Parent);
}

// The C# `handler.Filter.MatchLdcI4(1)` extension: whether the instruction is
// an `ldc.i4 <value>` with the given constant.
bool StatementBuilderMatchLdcI4(const IL::ILInstruction* inst, std::int32_t value) {
    return inst != nullptr && inst->Op == IL::OpCode::LdcI4
        && static_cast<const IL::LdcI4*>(inst)->Value == value;
}

// The C# `inst.MatchLeave(container)` (PatternMatching.cs line 226): a Leave
// targeting `container` whose Value is a `nop` (the port treats a missing
// Value as a `nop` -- the reader's value-less leave).
bool StatementBuilderMatchLeave(const IL::ILInstruction* inst,
                                const IL::BlockContainer* container) {
    auto* leave = dynamic_cast<const IL::Leave*>(inst);
    if (leave == nullptr || leave->TargetContainer != container)
        return false;
    return leave->Value == nullptr || leave->Value->Op == IL::OpCode::Nop;
}

// The C# `inst.MatchBranch(out targetBlock)` (PatternMatching.cs line 171).
bool StatementBuilderMatchBranch(const IL::ILInstruction* inst, IL::Block*& targetBlock) {
    auto* br = dynamic_cast<const IL::Branch*>(inst);
    if (br == nullptr || br->TargetBlock == nullptr)
        return false;
    targetBlock = br->TargetBlock;
    return true;
}

// The C# `BlockContainer.MatchConditionBlock(Block, out condition,
// out bodyStartBlock)` (BlockContainer.cs lines 356-365): the block holds a
// single `if` whose false arm leaves this container and whose true arm
// branches to the body start. The port reads the if from the block's
// FinalInstruction (the port's reader puts the conditional terminator there);
// the C#-shaped single-instruction form is accepted too (transforms that
// build the C# shape by hand).
bool StatementBuilderMatchConditionBlock(const IL::BlockContainer& container,
                                         IL::Block* block,
                                         IL::ILInstruction*& condition,
                                         IL::Block*& bodyStartBlock) {
    condition = nullptr;
    bodyStartBlock = nullptr;
    if (block == nullptr)
        return false;
    IL::IfInstruction* iff = nullptr;
    if (block->Instructions.empty() && block->FinalInstruction != nullptr)
        iff = dynamic_cast<IL::IfInstruction*>(block->FinalInstruction.get());
    if (iff == nullptr && block->Instructions.size() == 1)
        iff = dynamic_cast<IL::IfInstruction*>(block->Instructions[0].get());
    if (iff == nullptr)
        return false;
    IL::Block* body = nullptr;
    if (!StatementBuilderMatchLeave(iff->FalseInst.get(), &container))
        return false;
    if (!StatementBuilderMatchBranch(iff->TrueInst.get(), body))
        return false;
    condition = iff->Condition.get();
    bodyStartBlock = body;
    return true;
}

// The C# `BlockContainer.MatchIncrementBlock(Block)` (BlockContainer.cs lines
// 367-374): the block ends in a branch back to the entry point.
bool StatementBuilderMatchIncrementBlock(const IL::BlockContainer& container,
                                         IL::Block* block) {
    if (block == nullptr)
        return false;
    // The port's shape: the terminator is the FinalInstruction.
    if (block->FinalInstruction != nullptr) {
        auto* br = dynamic_cast<IL::Branch*>(block->FinalInstruction.get());
        return br != nullptr && br->TargetBlock == container.EntryPoint();
    }
    // The C# shape: the branch is the last non-final instruction.
    if (block->Instructions.empty())
        return false;
    auto* br = dynamic_cast<IL::Branch*>(block->Instructions.back().get());
    return br != nullptr && br->TargetBlock == container.EntryPoint();
}

// The C# `static bool IsFinalLeave(Leave leave)` (StatementBuilder.cs lines
// 1575-1586): a value-less leave that is the last instruction of the last
// block of its own target container (the implicitly executed return). The
// C# checks the leave's ChildIndex against the instruction count and requires
// a `nop` final; the port accepts either shape -- the leave as the block's
// terminator (the port's reader puts the final in `FinalInstruction`) or the
// C#-shaped leave as the last list instruction with a `nop` final.
bool StatementBuilderIsFinalLeave(const IL::Leave* leave) {
    if (leave == nullptr)
        return false;
    if (leave->Value != nullptr && leave->Value->Op != IL::OpCode::Nop)
        return false;
    auto* block = dynamic_cast<IL::Block*>(leave->Parent);
    if (block == nullptr)
        return false;
    if (block->FinalInstruction.get() == leave) {
        // The port shape: the leave is the block terminator.
    } else if (leave->ChildIndex
                   == static_cast<int>(block->Instructions.size()) - 1
               && (block->FinalInstruction == nullptr
                   || block->FinalInstruction->Op == IL::OpCode::Nop)) {
        // The C# shape: the leave is the last list instruction.
    } else {
        return false;
    }
    auto* container = dynamic_cast<IL::BlockContainer*>(block->Parent);
    if (container == nullptr)
        return false;
    return block->ChildIndex == static_cast<int>(container->Blocks.size()) - 1
        && container == leave->TargetContainer;
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


// The C# `protected internal override TranslatedStatement VisitPinnedRegion
// (PinnedRegion inst)` (StatementBuilder.cs lines 1201-1263): the `fixed`
// statement render. The GetPinnableReference arm is deferred with the
// GetPinnableReference IL node (the port's ILAst does not carry it); the
// pointer-pinning workaround (the IsAddressOfMoveableVar/IsFixedSizeBuffer
// probes + the `Unsafe.AsRef` re-pin) is deferred with the
// PointerArithmeticOffset.IsFixedVariable/IsFixedField consumer surface --
// the plain path (the type conversion with the DirectionExpression
// dereference/addressof unwrap) is what every non-deferred case takes.
TranslatedStatement StatementBuilder::VisitPinnedRegion(IL::PinnedRegion* inst) {
    auto* fixedStmt = new Syntax::FixedStatement();
    fixedStmt->Type(exprBuilder->ConvertType(*inst->Variable->Type));
    IL::ILInstruction* init = inst->Init.get();
    TS::ITypePtr refType = inst->Variable->Type;
    if (auto* pointerType = dynamic_cast<TS::PointerType*>(const_cast<TS::IType*>(refType.get()))) {
        refType = std::make_shared<TS::ByReferenceType>(pointerType->Element());
    }
    TranslatedExpression initExpr = exprBuilder->Translate(init, refType.get());
    initExpr = initExpr.ConvertTo(const_cast<TS::IType&>(*refType), *exprBuilder,
                                  /*checkForOverflow=*/false,
                                  /*allowImplicitConversion=*/false);
    if (auto* dirExpr =
            dynamic_cast<Syntax::DirectionExpression*>(initExpr.Expression())) {
        if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(
                dirExpr->Expression());
            uoe != nullptr
            && uoe->Operator() == Syntax::UnaryOperatorType::Dereference) {
            // The C# `initExpr = uoe.Expression.Detach()` -- the detached node
            // keeps its own annotations (the C# TranslatedExpression ctor
            // reads them).
            initExpr = TranslatedExpression(Syntax::Detach(uoe->Expression()));
        } else {
            initExpr = WithRR(
                WithoutILInstruction(*new Syntax::UnaryOperatorExpression(
                    Syntax::Detach(dirExpr->Expression()),
                    Syntax::UnaryOperatorType::AddressOf)),
                std::make_shared<Sem::ResolveResult>(refType));
        }
    }
    auto* initializer = new Syntax::VariableInitializer(inst->Variable->Name,
                                                        initExpr.Expression());
    WithILVariable(*initializer, inst->Variable);
    fixedStmt->Variables().Add(initializer);
    fixedStmt->EmbeddedStatement(Convert(inst->Body.get()).Statement());
    return WithILInstruction(*fixedStmt, inst);
}


// The C# `internal IEnumerable<ConstantResolveResult> CreateTypedCaseLabel(long i,
// IType type, List<(string? Key, int Value)>? map = null)` (StatementBuilder.cs
// lines 156-201): the typed case-label constant over the switch's governing
// type. The C# generator (`yield return`) ports to a materializing vector (the
// eager-iteration convention); the map arm (the StringToInt case) asserts --
// the StringToInt node is not ported.
std::vector<std::shared_ptr<Sem::ConstantResolveResult>>
StatementBuilder::CreateTypedCaseLabel(
    std::int64_t i, const TS::IType& type,
    const std::optional<std::vector<std::pair<std::optional<std::string>, int>>>& map)
    const {
    std::vector<std::shared_ptr<Sem::ConstantResolveResult>> results;
    // unpack nullable type, if necessary:
    // we need to do this in all cases, because there are nullable bools and
    // enum types as well.
    const TS::IType& underlying = TS::GetUnderlyingType(type);
    if (TS::IsKnownType(underlying, TS::KnownTypeCode::Boolean)) {
        results.push_back(std::make_shared<Sem::ConstantResolveResult>(
            std::const_pointer_cast<TS::IType>(
                const_cast<TS::IType&>(underlying).shared_from_this()),
            std::any(i != 0)));
        return results;
    }
    if (map.has_value()) {
        assert(TS::IsKnownType(underlying, TS::KnownTypeCode::String)
               && "CreateTypedCaseLabel: the map arm requires a string switch");
        for (const auto& entry : *map) {
            if (entry.second == i)
                results.push_back(std::make_shared<Sem::ConstantResolveResult>(
                    std::const_pointer_cast<TS::IType>(
                        const_cast<TS::IType&>(underlying).shared_from_this()),
                    std::any(entry.first)));
        }
        return results;
    }
    std::any value;
    if (underlying.Kind() == TS::TypeKind::Enum) {
        const TS::IType* enumType = TS::GetEnumUnderlyingType(&underlying);
        TS::TypeCode typeCode = enumType != nullptr
            ? TS::GetTypeCode(*enumType)
            : TS::TypeCode::Empty;
        if (typeCode != TS::TypeCode::Empty) {
            value = ::ILSpy::Decompiler::Util::Cast(typeCode, std::any(i),
                                                    /*checkForOverflow=*/false);
        } else {
            value = std::any(i);
        }
    } else {
        TS::TypeCode typeCode = TS::GetTypeCode(underlying);
        if (typeCode != TS::TypeCode::Empty) {
            value = ::ILSpy::Decompiler::Util::Cast(typeCode, std::any(i),
                                                    /*checkForOverflow=*/false);
        } else {
            value = std::any(i);
        }
    }
    results.push_back(std::make_shared<Sem::ConstantResolveResult>(
        std::const_pointer_cast<TS::IType>(
            const_cast<TS::IType&>(underlying).shared_from_this()),
        std::move(value)));
    return results;
}

// The C# `public SwitchSection GetDefaultSection()` (SwitchInstruction.cs
// lines 177-189): the section with the most labels stands default.
IL::SwitchSection* StatementBuilder::GetDefaultSection(
    IL::SwitchInstruction* inst) const {
    IL::SwitchSection* defaultSection = inst->Sections.front().get();
    for (const auto& section : inst->Sections) {
        if (static_cast<std::int64_t>(section->Labels.Count())
            > static_cast<std::int64_t>(defaultSection->Labels.Count())) {
            defaultSection = section.get();
        }
    }
    return defaultSection;
}

// The C# `private void ConvertSwitchSectionBody(Syntax.SwitchSection
// astSection, ILInstruction bodyInst)` (StatementBuilder.cs lines 321-337):
// the section body conversion with the `break;` insertion (the C# checks
// `EndPointUnreachable`).
void StatementBuilder::ConvertSwitchSectionBody(Syntax::SwitchSection* astSection,
                                                IL::ILInstruction* bodyInst) {
    TranslatedStatement body = Convert(bodyInst);
    astSection->Statements().Add(body.Statement());
    if (!IL::HasFlag(bodyInst->Flags(), IL::InstructionFlags::EndPointUnreachable)) {
        // we need to insert 'break;'
        if (auto* block = dynamic_cast<Syntax::BlockStatement*>(body.Statement())) {
            block->Statements().Add(new Syntax::BreakStatement());
        } else {
            astSection->Statements().Add(new Syntax::BreakStatement());
        }
    }
}


// The C# `SwitchStatement TranslateSwitch(BlockContainer? switchContainer,
// SwitchInstruction inst)` (StatementBuilder.cs lines 203-319): the switch
// construction -- the break/continue state swap, the value translation (the
// StringToInt arm deferred with the StringToInt IL node), the per-section
// case labels + the branch inlining through the case-label mapping, and the
// remaining-block fallthrough with the end-container goto.
// The C# `protected internal override TranslatedStatement
// VisitSwitchInstruction(SwitchInstruction inst)` (StatementBuilder.cs line
// 203): a free-standing switch translates with no surrounding container (the
// `break` bookkeeping inside TranslateSwitch leaves the switch immediately).
TranslatedStatement StatementBuilder::VisitSwitchInstruction(
    IL::SwitchInstruction* inst) {
    return WithILInstruction(*TranslateSwitch(nullptr, inst), inst);
}

Syntax::SwitchStatement* StatementBuilder::TranslateSwitch(
    IL::BlockContainer* switchContainer, IL::SwitchInstruction* inst) {
    IL::BlockContainer* oldBreakTarget = breakTarget;
    // 'break' within a switch would only leave the switch
    breakTarget = switchContainer;
    std::optional<std::map<IL::Block*, std::optional<std::shared_ptr<Sem::ResolveResult>>>>
        oldCaseLabelMapping = caseLabelMapping;
    caseLabelMapping =
        std::map<IL::Block*, std::optional<std::shared_ptr<Sem::ResolveResult>>>{};

    // The C# `exprBuilder.TranslateSwitchValue(inst, false)` -- the port's
    // machinery lives on the ExpressionBuilder (the StringToInt arm is
    // deferred with the StringToInt IL node).
    auto [value, type] =
        exprBuilder->TranslateSwitchValue(inst, /*isExpressionContext=*/false);

    IL::SwitchSection* defaultSection = GetDefaultSection(inst);

    auto* stmt = new Syntax::SwitchStatement();
    stmt->Expression(value.Expression());
    std::map<IL::SwitchSection*, Syntax::SwitchSection*> translationDictionary;
    // initialize C# switch sections.
    for (const auto& section : inst->Sections) {
        // This is used in the block-label mapping.
        std::shared_ptr<Sem::ConstantResolveResult> firstValueResolveResult;
        auto* astSection = new Syntax::SwitchSection();
        // Create case labels:
        if (section.get() == defaultSection) {
            astSection->CaseLabels().Add(new Syntax::CaseLabel());
            firstValueResolveResult = nullptr;
        } else {
            // The C# `section.Labels.Values.SelectMany(i => CreateTypedCaseLabel(i,
            // type, strToInt?.Map)).ToArray()` -- the map is absent (the
            // StringToInt arm is deferred), so each label maps to its own
            // typed case constant.
            std::vector<std::shared_ptr<Sem::ConstantResolveResult>> values;
            for (long long label : section->Labels.Values())
                for (const auto& rr : CreateTypedCaseLabel(label, *type, std::nullopt))
                    values.push_back(rr);
            if (section->HasNullLabel) {
                astSection->CaseLabels().Add(new Syntax::CaseLabel(
                    new Syntax::NullReferenceExpression()));
                firstValueResolveResult = std::make_shared<Sem::ConstantResolveResult>(
                    TS::NullType(), std::any());
            } else {
                assert(!values.empty()
                       && "a non-default switch section has at least one label");
                firstValueResolveResult = values.front();
            }
            for (const auto& label : values) {
                astSection->CaseLabels().Add(
                    new Syntax::CaseLabel(exprBuilder->ConvertConstantValue(
                                              label, /*allowImplicitConversion=*/true)
                                              .Expression()));
            }
        }
        if (auto* br = dynamic_cast<IL::Branch*>(section->Body.get())) {
            // we can only inline the block, if all branches are in the
            // switchContainer. The C# `Descendants.OfType<Branch>()` walk is
            // approximated with the branch's own container check (the port's
            // switch sections carry their branches with resolved targets).
            if (BranchTargetContainer(br) == switchContainer)
                caseLabelMapping->emplace(br->TargetBlock, firstValueResolveResult);
        }
        translationDictionary.emplace(section.get(), astSection);
        stmt->SwitchSections().Add(astSection);
    }
    for (const auto& section : inst->Sections) {
        auto* astSection = translationDictionary[section.get()];
        if (auto* br = dynamic_cast<IL::Branch*>(section->Body.get())) {
            // we can only inline the block, if all branches are in the
            // switchContainer.
            if (BranchTargetContainer(br) == switchContainer)
                ConvertSwitchSectionBody(astSection, br->TargetBlock);
            else
                ConvertSwitchSectionBody(astSection, section->Body.get());
        } else if (auto* leave = dynamic_cast<IL::Leave*>(section->Body.get())) {
            if (astSection->CaseLabels().Count() == 1
                && astSection->CaseLabels().At(0)->Expression() == nullptr
                && leave->TargetContainer == switchContainer) {
                stmt->SwitchSections().Remove(astSection);
            } else {
                ConvertSwitchSectionBody(astSection, section->Body.get());
            }
        } else {
            ConvertSwitchSectionBody(astSection, section->Body.get());
        }
    }
    if (switchContainer != nullptr && stmt->SwitchSections().Count() > 0) {
        // Translate any remaining blocks:
        Syntax::SwitchSection* lastSection = stmt->SwitchSections().At(
            stmt->SwitchSections().Count() - 1);
        for (const auto& block : switchContainer->Blocks) {
            if (caseLabelMapping->count(block.get()) > 0)
                continue;
            lastSection->Statements().Add(
                new Syntax::LabelStatement(EnsureUniqueLabel(block.get())));
            for (const auto& nestedInst : block->Instructions) {
                TranslatedStatement nestedStmt = Convert(nestedInst.get());
                if (auto* b = dynamic_cast<Syntax::BlockStatement*>(nestedStmt.Statement())) {
                    for (int i = 0; i < b->Statements().Count(); i++) {
                        lastSection->Statements().Add(
                            Syntax::Detach(b->Statements().At(i)));
                    }
                } else {
                    lastSection->Statements().Add(nestedStmt.Statement());
                }
            }
            assert(block->FinalInstruction == nullptr
                   || block->FinalInstruction->Op == IL::OpCode::Nop);
        }
        auto endLabel = endContainerLabels.find(switchContainer);
        if (endLabel != endContainerLabels.end()) {
            lastSection->Statements().Add(new Syntax::LabelStatement(endLabel->second));
            lastSection->Statements().Add(new Syntax::BreakStatement());
        }
    }

    breakTarget = oldBreakTarget;
    caseLabelMapping = oldCaseLabelMapping;
    return stmt;
}

// The C# `protected internal override TranslatedStatement
// VisitUsingInstruction(UsingInstruction inst)` (StatementBuilder.cs lines
// 961-1036): the `using` render with the IDisposable probe (the fallback
// try/finally render when the type does not implement IDisposable directly)
// and the resource-acquisition declaration. The TransformToForeach arm is
// deferred with the foreach surface (the AST pattern nodes -- AnyNode/
// NamedNode/Choice -- are not ported, so the GetEnumerator/MoveNext patterns
// cannot match); a foreach-shaped using renders as the plain using statement.
bool UsingInstructionIsValidInCSharp(StatementBuilder& builder,
                                     const IL::UsingInstruction* inst,
                                     TS::KnownTypeCode code) {
    (void)builder;
    // The C# `inst.ResourceExpression.MatchLdNull()`.
    if (inst->ResourceExpression != nullptr
        && inst->ResourceExpression->Op == IL::OpCode::LdNull)
        return true;
    if (inst->IsRefStruct)
        return true;
    const IL::ILVariable* var = inst->Variable.get();
    // The C# `NullableType.GetUnderlyingType(var.Type).GetAllBaseTypes()
    // .Any(b => b.IsKnownType(code))`.
    const TS::IType& underlying = TS::GetUnderlyingType(*var->Type);
    for (const TS::IType* baseType : TS::GetAllBaseTypes(&underlying)) {
        if (TS::IsKnownType(*baseType, code))
            return true;
    }
    return false;
}

TranslatedStatement StatementBuilder::VisitUsingInstruction(
    IL::UsingInstruction* inst) {
    Syntax::Expression* resource =
        exprBuilder->Translate(inst->ResourceExpression.get()).Expression();
    // The C# `var transformed = TransformToForeach(inst, resource); if
    // (transformed != null) return transformed;` -- deferred with the foreach
    // surface (see the IsValidInCSharp comment above).
    const IL::ILVariablePtr& var = inst->Variable;
    TS::KnownTypeCode knownTypeCode;
    TS::ITypePtr disposeType;
    const char* disposeTypeMethodName;
    if (inst->IsAsync) {
        knownTypeCode = TS::KnownTypeCode::IAsyncDisposable;
        disposeType = std::const_pointer_cast<TS::IType>(
            const_cast<TS::IType&>(exprBuilder->compilation->FindType(
                TS::KnownTypeCode::IAsyncDisposable))
                .shared_from_this());
        disposeTypeMethodName = "DisposeAsync";
    } else {
        knownTypeCode = TS::KnownTypeCode::IDisposable;
        disposeType = std::const_pointer_cast<TS::IType>(
            const_cast<TS::IType&>(exprBuilder->compilation->FindType(
                TS::KnownTypeCode::IDisposable))
                .shared_from_this());
        disposeTypeMethodName = "Dispose";
    }
    if (!UsingInstructionIsValidInCSharp(*this, inst, knownTypeCode)) {
        assert(var->Kind == IL::VariableKind::UsingLocal
               || var->Kind == IL::VariableKind::Local);
        var->Kind = IL::VariableKind::Local;
        auto disposeVariable = currentFunction->RegisterVariable(
            IL::VariableKind::Local, disposeType);
        Syntax::Expression* disposeInvocation = new Syntax::InvocationExpression(
            new Syntax::MemberReferenceExpression(
                exprBuilder->ConvertVariable(disposeVariable).Expression(),
                disposeTypeMethodName));
        if (inst->IsAsync) {
            auto* awaitExpr = new Syntax::UnaryOperatorExpression();
            awaitExpr->Expression(disposeInvocation);
            awaitExpr->Operator(Syntax::UnaryOperatorType::Await);
            disposeInvocation = awaitExpr;
        }
        auto* block = new Syntax::BlockStatement();
        block->Statements().Add(new Syntax::ExpressionStatement(
            new Syntax::AssignmentExpression(
                exprBuilder->ConvertVariable(var).Expression(),
                Syntax::Detach(resource))));
        auto* tryCatch = new Syntax::TryCatchStatement(
            dynamic_cast<Syntax::BlockStatement*>(
                ConvertAsBlock(inst->Body.get()).Statement()));
        auto* finallyBlock = new Syntax::BlockStatement();
        finallyBlock->Statements().Add(new Syntax::ExpressionStatement(
            new Syntax::AssignmentExpression(
                exprBuilder->ConvertVariable(disposeVariable).Expression(),
                new Syntax::AsExpression(
                    exprBuilder->ConvertVariable(var).Expression(),
                    exprBuilder->ConvertType(*disposeType)))));
        finallyBlock->Statements().Add(new Syntax::IfElseStatement(
            new Syntax::BinaryOperatorExpression(
                exprBuilder->ConvertVariable(disposeVariable).Expression(),
                Syntax::BinaryOperatorType::InEquality,
                new Syntax::NullReferenceExpression()),
            new Syntax::ExpressionStatement(disposeInvocation)));
        tryCatch->FinallyBlock(finallyBlock);
        block->Statements().Add(tryCatch);
        return WithILInstruction(*block, inst);
    }
    Syntax::AstNode* usingInit = resource;
    if (var->LoadCount > 0 || var->AddressCount > 0) {
        // The C# `settings.AnonymousTypes && var.Type.ContainsAnonymousType()`
        // gate: the anonymous-type detection is not ported (the NRExtensions
        // surface is deferred), so the gate is the plain type conversion.
        Syntax::AstType* type = exprBuilder->ConvertType(*var->Type);
        auto* vds = new Syntax::VariableDeclarationStatement(type, var->Name);
        vds->Variables().At(0)->AddAnnotation(
            std::make_shared<ILVariableResolveResult>(var, var->Type));
        usingInit = vds;
    }
    auto* usingStatement = new Syntax::UsingStatement(
        usingInit,
        dynamic_cast<Syntax::BlockStatement*>(
            ConvertAsBlock(inst->Body.get()).Statement()));
    usingStatement->IsAsync(inst->IsAsync);
    return WithILInstruction(*usingStatement, inst);
}

// The C# `protected internal override TranslatedStatement VisitInitblk(
// Initblk inst)` (StatementBuilder.cs lines 1609-1625): the
// `Unsafe.InitBlock(Unaligned)` call with the leading ` IL initblk
// instruction` comment.
TranslatedStatement StatementBuilder::VisitInitblk(IL::Initblk* inst) {
    TranslatedExpression call = exprBuilder->CallUnsafeIntrinsic(
        inst->UnalignedPrefix != 0 ? "InitBlockUnaligned" : "InitBlock",
        {exprBuilder->Translate(inst->Address.get()).Expression(),
         exprBuilder->Translate(inst->Value.get()).Expression(),
         exprBuilder->Translate(inst->Size.get()).Expression()},
        exprBuilder->compilation->FindType(TS::KnownTypeCode::Void), inst);
    auto* stmt = new Syntax::ExpressionStatement(call.Expression());
    stmt->AddLeadingTrivia(new Syntax::Comment(" IL initblk instruction"));
    return WithILInstruction(*stmt, inst);
}

// The C# `protected internal override TranslatedStatement VisitCpblk(
// Cpblk inst)` (StatementBuilder.cs lines 1627-1643): the
// `Unsafe.CopyBlock(Unaligned)` call with the leading ` IL cpblk instruction`
// comment.
TranslatedStatement StatementBuilder::VisitCpblk(IL::Cpblk* inst) {
    TranslatedExpression call = exprBuilder->CallUnsafeIntrinsic(
        inst->UnalignedPrefix != 0 ? "CopyBlockUnaligned" : "CopyBlock",
        {exprBuilder->Translate(inst->DestAddress.get()).Expression(),
         exprBuilder->Translate(inst->SourceAddress.get()).Expression(),
         exprBuilder->Translate(inst->Size.get()).Expression()},
        exprBuilder->compilation->FindType(TS::KnownTypeCode::Void), inst);
    auto* stmt = new Syntax::ExpressionStatement(call.Expression());
    stmt->AddLeadingTrivia(new Syntax::Comment(" IL cpblk instruction"));
    return WithILInstruction(*stmt, inst);
}

// The C# `protected internal override TranslatedStatement VisitCkfinite(
// Ckfinite inst)` (StatementBuilder.cs lines 1645-1675): the
// `float.IsFinite(argument)` guard throwing ArithmeticException on the not-
// finite path.
TranslatedStatement StatementBuilder::VisitCkfinite(IL::Ckfinite* inst) {
    auto* isFiniteCall = new Syntax::InvocationExpression(
        new Syntax::MemberReferenceExpression(
            new Syntax::TypeReferenceExpression(
                new Syntax::PrimitiveType("float")),
            "IsFinite"));
    isFiniteCall->Arguments().Add(
        exprBuilder->Translate(inst->Argument.get()).Expression());
    // The C# `typeSystem.FindType(typeof(ArithmeticException))` -- the
    // reflection-name lookup (the type is not a KnownTypeCode).
    TS::ITypePtr arithmeticException = TS::FindType(
        *exprBuilder->compilation,
        TS::FullTypeName(TS::TopLevelTypeName("System", "ArithmeticException")));
    auto* exceptionSyntax = new Syntax::SimpleType("ArithmeticException");
    exceptionSyntax->AddAnnotation(std::make_shared<Sem::TypeResolveResult>(
        arithmeticException));
    auto* ifElse = new Syntax::IfElseStatement(
        new Syntax::UnaryOperatorExpression(
            isFiniteCall, Syntax::UnaryOperatorType::Not),
        new Syntax::ThrowStatement(
            new Syntax::ObjectCreateExpression(exceptionSyntax)));
    return WithILInstruction(*ifElse, inst);
}

// The C# `protected internal override TranslatedStatement
// VisitBlockContainer(BlockContainer container)` (StatementBuilder.cs lines
// 1300-1323): a loop-kind container with more than one entry edge renders as a
// loop; an entry point holding a single switch instruction translates as a
// switch with the container as the `break` target; anything else renders as
// the plain block list.
TranslatedStatement StatementBuilder::VisitBlockContainer(
    IL::BlockContainer* container) {
    if (container->Kind != IL::ContainerKind::Normal
        && container->EntryPoint() != nullptr
        && container->EntryPoint()->IncomingEdgeCount > 1) {
        IL::Block* oldContinueTarget = continueTarget;
        int oldContinueCount = continueCount;
        IL::BlockContainer* oldBreakTarget = breakTarget;
        Syntax::Statement* loop = ConvertLoop(container);
        continueTarget = oldContinueTarget;
        continueCount = oldContinueCount;
        breakTarget = oldBreakTarget;
        return WithILInstruction(*loop, container);
    }
    if (container->EntryPoint() != nullptr
        && container->EntryPoint()->Instructions.empty()
        && dynamic_cast<IL::SwitchInstruction*>(
               container->EntryPoint()->FinalInstruction.get())
               != nullptr) {
        auto* switchInst = static_cast<IL::SwitchInstruction*>(
            container->EntryPoint()->FinalInstruction.get());
        return WithILInstruction(*TranslateSwitch(container, switchInst),
                                 container);
    }
    Syntax::BlockStatement* blockStmt = ConvertBlockContainer(container, false);
    return WithILInstruction(*blockStmt, container);
}

// The C# `Statement ConvertLoop(BlockContainer container)` (StatementBuilder.cs
// lines 1325-1442): the While/DoWhile/For/Loop kinds over the container's
// blocks. `DeclareLocalFunctions` is a no-op here (the local-function
// declaration machinery is deferred -- it throws when a local function would
// actually be emitted).
Syntax::Statement* StatementBuilder::ConvertLoop(IL::BlockContainer* container) {
    IL::ILInstruction* condition = nullptr;
    IL::Block* loopBody = nullptr;
    Syntax::BlockStatement* blockStatement = nullptr;
    continueCount = 0;
    breakTarget = container;
    switch (container->Kind) {
        case IL::ContainerKind::Loop: {
            continueTarget = container->EntryPoint();
            blockStatement = ConvertBlockContainer(container, true);
            assert(continueCount < container->EntryPoint()->IncomingEdgeCount);
            // Remove the entrypoint label if all jumps to the label were
            // replaced with 'continue;' statements.
            if (container->EntryPoint()->IncomingEdgeCount
                    == continueCount + 1
                && blockStatement->Statements().Count() > 0) {
                blockStatement->Statements().At(0)->Remove();
            }
            if (blockStatement->Statements().Count() > 0) {
                Syntax::Statement* last = blockStatement->Statements().At(
                    blockStatement->Statements().Count() - 1);
                if (dynamic_cast<Syntax::ContinueStatement*>(last) != nullptr)
                    last->Remove();
            }
            DeclareLocalFunctions(container, blockStatement);
            return new Syntax::WhileStatement(
                new Syntax::PrimitiveExpression(true), blockStatement);
        }
        case IL::ContainerKind::While: {
            continueTarget = container->EntryPoint();
            if (!StatementBuilderMatchConditionBlock(
                    *container, continueTarget, condition, loopBody))
                throw std::logic_error(
                    "Invalid condition block in while loop.");
            blockStatement = dynamic_cast<Syntax::BlockStatement*>(
                ConvertAsBlock(loopBody).Statement());
            assert(blockStatement != nullptr);
            if (!IL::HasFlag(loopBody->Flags(),
                             IL::InstructionFlags::EndPointUnreachable))
                blockStatement->Statements().Add(new Syntax::BreakStatement());
            // The remaining blocks (the C# `Blocks.Skip(1).Except(loopBody)`).
            std::vector<IL::Block*> remaining;
            for (size_t i = 1; i < container->Blocks.size(); i++) {
                if (container->Blocks[i].get() != loopBody)
                    remaining.push_back(container->Blocks[i].get());
            }
            ConvertBlockContainer(blockStatement, container, remaining, true);
            assert(continueCount < container->EntryPoint()->IncomingEdgeCount);
            if (continueCount + 1 < container->EntryPoint()->IncomingEdgeCount) {
                // There's an incoming edge to the entry point (=while
                // condition) that wasn't represented as "continue;" -> emit a
                // real label. Any "continue;" in front of the label is
                // redundant and removed.
                if (blockStatement->Statements().Count() > 0) {
                    Syntax::Statement* last = blockStatement->Statements().At(
                        blockStatement->Statements().Count() - 1);
                    if (dynamic_cast<Syntax::ContinueStatement*>(last) != nullptr)
                        last->Remove();
                }
                blockStatement->Statements().Add(
                    new Syntax::LabelStatement(
                        EnsureUniqueLabel(container->EntryPoint())));
            }
            if (blockStatement->Statements().Count() > 0) {
                Syntax::Statement* last = blockStatement->Statements().At(
                    blockStatement->Statements().Count() - 1);
                if (dynamic_cast<Syntax::ContinueStatement*>(last) != nullptr)
                    last->Remove();
            }
            DeclareLocalFunctions(container, blockStatement);
            return new Syntax::WhileStatement(
                exprBuilder->TranslateCondition(condition).Expression(),
                blockStatement);
        }
        case IL::ContainerKind::DoWhile: {
            continueTarget = container->Blocks.back().get();
            if (!StatementBuilderMatchConditionBlock(
                    *container, continueTarget, condition, loopBody))
                throw std::logic_error(
                    "Invalid condition block in do-while loop.");
            // The body blocks (the C# `Blocks.SkipLast(1)`).
            std::vector<IL::Block*> bodyBlocks;
            bodyBlocks.reserve(container->Blocks.size() - 1);
            for (size_t i = 0; i + 1 < container->Blocks.size(); i++)
                bodyBlocks.push_back(container->Blocks[i].get());
            blockStatement = new Syntax::BlockStatement();
            ConvertBlockContainer(blockStatement, container, bodyBlocks, true);
            if (container->EntryPoint()->IncomingEdgeCount == 2
                && blockStatement->Statements().Count() > 0) {
                // Remove the entry-point label if there are only two jumps to
                // the entry point: from outside the loop and from the
                // condition block.
                blockStatement->Statements().At(0)->Remove();
            }
            if (blockStatement->Statements().Count() > 0) {
                Syntax::Statement* last = blockStatement->Statements().At(
                    blockStatement->Statements().Count() - 1);
                if (dynamic_cast<Syntax::ContinueStatement*>(last) != nullptr)
                    last->Remove();
            }
            if (continueTarget->IncomingEdgeCount > continueCount) {
                // Branches to the condition block that were not converted to
                // continue statements need an extra label.
                blockStatement->Statements().Add(
                    new Syntax::LabelStatement(
                        EnsureUniqueLabel(continueTarget)));
            }
            DeclareLocalFunctions(container, blockStatement);
            if (blockStatement->Statements().Count() == 0) {
                return new Syntax::WhileStatement(
                    exprBuilder->TranslateCondition(condition).Expression(),
                    blockStatement);
            }
            return new Syntax::DoWhileStatement(blockStatement,
                exprBuilder->TranslateCondition(condition).Expression());
        }
        case IL::ContainerKind::For: {
            continueTarget = container->Blocks.back().get();
            if (!StatementBuilderMatchConditionBlock(
                    *container, container->EntryPoint(), condition, loopBody))
                throw std::logic_error(
                    "Invalid condition block in for loop.");
            blockStatement = dynamic_cast<Syntax::BlockStatement*>(
                ConvertAsBlock(loopBody).Statement());
            assert(blockStatement != nullptr);
            if (!IL::HasFlag(loopBody->Flags(),
                             IL::InstructionFlags::EndPointUnreachable))
                blockStatement->Statements().Add(new Syntax::BreakStatement());
            if (!StatementBuilderMatchIncrementBlock(*container, continueTarget))
                throw std::logic_error(
                    "Invalid increment block in for loop.");
            // The C# `Blocks.SkipLast(1).Skip(1).Except(loopBody)`: the middle
            // blocks without the entry point and the increment block.
            std::vector<IL::Block*> remaining;
            for (size_t i = 1; i + 1 < container->Blocks.size(); i++) {
                if (container->Blocks[i].get() != loopBody)
                    remaining.push_back(container->Blocks[i].get());
            }
            ConvertBlockContainer(blockStatement, container, remaining, true);
            auto* forStmt = new Syntax::ForStatement();
            forStmt->Condition(
                exprBuilder->TranslateCondition(condition).Expression());
            forStmt->EmbeddedStatement(blockStatement);
            if (blockStatement->Statements().Count() > 0) {
                Syntax::Statement* last = blockStatement->Statements().At(
                    blockStatement->Statements().Count() - 1);
                if (dynamic_cast<Syntax::ContinueStatement*>(last) != nullptr)
                    last->Remove();
            }
            for (size_t i = 0;
                 i + 1 < continueTarget->Instructions.size(); i++) {
                forStmt->Iterators().Add(Convert(
                    continueTarget->Instructions[i].get()).Statement());
            }
            if (continueTarget->IncomingEdgeCount > continueCount)
                blockStatement->Statements().Add(
                    new Syntax::LabelStatement(
                        EnsureUniqueLabel(continueTarget)));
            DeclareLocalFunctions(container, blockStatement);
            return forStmt;
        }
        default:
            throw std::logic_error("Invalid loop container kind.");
    }
}

// The C# `BlockStatement ConvertBlockContainer(BlockContainer container, bool
// isLoop)` (StatementBuilder.cs lines 1444-1476): all blocks with the label
// insertion; the EmitAsRefReadOnly helper-function synthesis is deferred (the
// local-function declaration machinery is not ported -- it throws when the
// flag is set).
Syntax::BlockStatement* StatementBuilder::ConvertBlockContainer(
    IL::BlockContainer* container, bool isLoop) {
    auto* blockStatement = new Syntax::BlockStatement();
    std::vector<IL::Block*> blocks;
    blocks.reserve(container->Blocks.size());
    for (auto& block : container->Blocks)
        blocks.push_back(block.get());
    ConvertBlockContainer(blockStatement, container, blocks, isLoop);
    DeclareLocalFunctions(container, blockStatement);
    return blockStatement;
}

// The C# `void DeclareLocalFunctions(ILFunction currentFunction,
// BlockContainer container, BlockStatement blockStatement)` (StatementBuilder.cs
// lines 1485-1546): the local functions scoped to this container. The port
// defers the local-function declaration machinery (the
// `ReducedMethod`/`DeclarationScope` ILFunction surface and the
// `CleanUpMethodDeclaration` pipeline are not ported) and throws when a local
// function would actually be emitted.
void StatementBuilder::DeclareLocalFunctions(
    IL::BlockContainer* container, Syntax::BlockStatement* blockStatement) {
    (void)blockStatement;
    if (currentFunction != nullptr && !currentFunction->LocalFunctions.empty())
        throw std::logic_error(
            "StatementBuilder::DeclareLocalFunctions: the local-function "
            "declaration machinery is not ported");
}

// The C# `BlockStatement ConvertBlockContainer(BlockStatement blockStatement,
// BlockContainer container, IEnumerable<Block> blocks, bool isLoop)`
// (StatementBuilder.cs lines 1548-1582): the block walk with the label
// insertion, the final-leave skip (the ImplicitReturnAnnotation), the
// foreach-without-dispose arm (deferred), and the end-container label with the
// loop break/continue bookkeeping.
void StatementBuilder::ConvertBlockContainer(
    Syntax::BlockStatement* blockStatement, IL::BlockContainer* container,
    const std::vector<IL::Block*>& blocks, bool isLoop) {
    for (IL::Block* block : blocks) {
        if (block->IncomingEdgeCount > 1 || block != container->EntryPoint()) {
            // If there are any incoming branches to this block, add a label.
            blockStatement->Statements().Add(
                new Syntax::LabelStatement(EnsureUniqueLabel(block)));
        }
        for (int i = 0; i < static_cast<int>(block->Instructions.size()); i++) {
            IL::ILInstruction* inst = block->Instructions[i].get();
            auto* leave = dynamic_cast<IL::Leave*>(inst);
            if (!isLoop && leave != nullptr
                && StatementBuilderIsFinalLeave(leave)) {
                // skip the final 'leave' instruction and just fall out of the
                // BlockStatement
                blockStatement->AddAnnotation(
                    std::make_shared<ImplicitReturnAnnotation>(leave));
                continue;
            }
            // The C# `TransformToForeachWithoutDispose(block, ref i) ??
            // Convert(inst)`: the foreach arm is deferred with the foreach
            // surface.
            Syntax::Statement* stmt = Convert(inst).Statement();
            if (auto* b = dynamic_cast<Syntax::BlockStatement*>(stmt)) {
                for (int j = 0; j < b->Statements().Count(); j++)
                    blockStatement->Statements().Add(
                        Syntax::Detach(b->Statements().At(j)));
            } else {
                blockStatement->Statements().Add(Syntax::Detach(stmt));
            }
        }
        if (block->FinalInstruction != nullptr
            && block->FinalInstruction->Op != IL::OpCode::Nop) {
            Syntax::Statement* stmt =
                Convert(block->FinalInstruction.get()).Statement();
            if (auto* b = dynamic_cast<Syntax::BlockStatement*>(stmt)) {
                for (int j = 0; j < b->Statements().Count(); j++)
                    blockStatement->Statements().Add(
                        Syntax::Detach(b->Statements().At(j)));
            } else {
                blockStatement->Statements().Add(Syntax::Detach(stmt));
            }
        }
    }
    auto endLabel = endContainerLabels.find(container);
    if (endLabel != endContainerLabels.end()) {
        Syntax::Statement* last = nullptr;
        if (blockStatement->Statements().Count() > 0) {
            last = blockStatement->Statements().At(
                blockStatement->Statements().Count() - 1);
        }
        if (isLoop && dynamic_cast<Syntax::ContinueStatement*>(last) == nullptr)
            blockStatement->Statements().Add(new Syntax::ContinueStatement());
        blockStatement->Statements().Add(
            new Syntax::LabelStatement(endLabel->second));
        if (isLoop)
            blockStatement->Statements().Add(new Syntax::BreakStatement());
    }
}

} // namespace ILSpy::Decompiler::CSharp
