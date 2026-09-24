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
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ThrowStatement.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cassert>

namespace ILSpy::Decompiler::CSharp {

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

} // namespace ILSpy::Decompiler::CSharp