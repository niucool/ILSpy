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
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IsExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp {

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

}  // namespace ILSpy::Decompiler::CSharp
