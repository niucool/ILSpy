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

// Port of the StatementBuilder SKELETON (ICSharpCode.Decompiler/CSharp/
// StatementBuilder.cs -- the ILVisitor<TranslatedStatement> that translates ILAst
// blocks to C# statements): the ctor with its ExpressionBuilder construction (the
// C# builds the expression builder passing `this` -- the mutual reference), the
// translation entry family (Convert / ConvertAsBlock), the real C# `Default`
// fallback (an ExpressionStatement over the expression translation -- unlike the
// ExpressionBuilder's error-expression fallback, this one is the C#'s own
// behavior), and the per-instruction Visit arms the slices have landed (the leaf
// statement stores/nops and the isinst expression statement).
//
// The C# `ILVisitor<TranslatedStatement>` base is a double-dispatch: every
// ILInstruction overrides AcceptVisitor<T> to call the visitor's Visit<Instr>
// (this). The port's IL tree has no AcceptVisitor surface (the ExpressionBuilder
// convention): `Visit` is an OpCode switch dispatching to the per-instruction
// Visit methods, and `Default` is the C#'s own not-overridden fallback -- the
// ExpressionStatement over `exprBuilder.Translate(inst)`.
//
// The C# fields the ctor copies: `currentReturnContainer` (the body cast to
// BlockContainer -- the port's ILFunction::Body is statically a BlockContainer, so
// the C# InvalidCastException shape is unreachable), `currentIsIterator` (the C#
// `IsIterator` field -- the port's seed ILFunction copies it), and
// `currentResultType` (the C# `IsAsync ? AsyncReturnType! : ReturnType` -- the
// port's seed ILFunction carries both fields, null until the type-system plumbing
// populates them).
//
// Deferrals (each named at the member that needs it): the CancellationToken (the
// cooperative-cancel ThrowIfCancellationRequested in Convert is a no-op in the
// port, the DecompileRun convention), the using / foreach Visit arms (the foreach
// machinery they ride -- TransformToForeach and friends -- lands with the
// UsingInstruction slice), and, inside the block-container
// region, the DeclareLocalFunctions local-function declarations (the
// TypeSystemAstBuilder.ConvertEntity long pole; the port's seed pipeline
// produces no local functions, so the no-op is unobservable today) and the
// TransformToForeachWithoutDispose arm of the block instruction loop (a null
// return -- statements convert through the normal path). An instruction whose
// C# Visit method has not been ported yet degrades to the Default expression
// statement instead of crashing. The try-construction region (the C#
// MakeTryCatch helper + VisitTryCatch/VisitTryFinally/VisitTryFault, lines
// 445-505) and the VisitLockInstruction sibling (lines 506-510) have landed
// beside the leaf arms, the switch region (CreateTypedCaseLabel +
// TranslateSwitch + VisitSwitchInstruction, lines 156-346) has landed with the
// StringToInt node and the ExpressionBuilder TranslateSwitchValue entry it
// rides, the block-container region (VisitBlock + VisitBlockContainer +
// ConvertLoop + ConvertBlockContainer, lines 1280-1608) completes the
// statement-level dispatch over the ILAst control flow, and the pinned-region
// arm (VisitPinnedRegion + the IsAddressOfMoveableVar/IsFixedSizeBuffer
// helpers, lines 1201-1278) has landed with the IL::GetPinnableReference node
// the deferred array/string pinned-region post-passes will populate.
//
// The goto/leave state (StatementBuilder.cs lines 338-373 + 1576-1597) landed
// with the leaf arms: the block->label maps (labels/duplicateLabels +
// EnsureUniqueLabel), the breakTarget/endContainerLabels pair a Leave consults,
// and the continueTarget/continueCount pair a Branch consults. They default to
// the C#-idle shape (no mappings, null targets) until the VisitBlockContainer /
// TranslateSwitch slices write them.

#pragma once

#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/TranslatedStatement.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

// The forward declarations at GLOBAL scope (the nested-namespace trap: a declaration
// written inside namespace CSharp would create CSharp::IL and shadow the real
// ILSpy::Decompiler::IL).
namespace ILSpy::Decompiler::IL {
class IsInst;
class StLoc;
class StObj;
class Nop;
class IfInstruction;
class Branch;
class Leave;
class Throw;
class Rethrow;
class YieldReturn;
class Ckfinite;
class Cpblk;
class Initblk;
class SwitchInstruction;
}  // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::CSharp {

// The REAL type-system namespace alias: the CSharp/TypeSystem sub-namespace
// (CSharpTypeResolveContext/UsingScope) shadows the plain `TypeSystem::` lookup
// inside this namespace, so every type-system reference goes through the
// fully-qualified alias (the ExpressionBuilder TS:: convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The ExpressionBuilder skeleton. The C# `sealed class StatementBuilder :
// ILVisitor<TranslatedStatement>`; the port models the visitor as the OpCode-switch
// `Visit` (the ExpressionBuilder convention) with one `VisitXxx` member per ported
// instruction kind.
class StatementBuilder {
public:
    // The C# `public StatementBuilder(IDecompilerTypeSystem typeSystem,
    // ITypeResolveContext decompilationContext, ILFunction currentFunction,
    // DecompilerSettings settings, DecompileRun decompileRun, CancellationToken
    // cancellationToken)`: builds the ExpressionBuilder over `this`, casts the
    // function body to the currentReturnContainer, and copies the
    // iterator/async-result state. The C# `Debug.Assert(typeSystem != null &&
    // decompilationContext != null)` maps to the port's invalid_argument guard for
    // the pointer parameters (the D424 convention -- the reference parameters carry
    // their non-nullness in their type); the IDecompilerTypeSystem surface ports as
    // the ICompilation narrowing (the ExpressionBuilder convention); the
    // CancellationToken is the documented deferral.
    // The out-of-line destructor: the unique_ptr<ExpressionBuilder> member over a
    // forward-declared-in-some-TUs complete type would instantiate the default_delete
    // in every consuming TU (the XamlContext convention).
    ~StatementBuilder();
    // The defaulted move (the destructor suppresses the implicit one; the fixture
    // factory returns by value).
    StatementBuilder(StatementBuilder&&) = default;

    explicit StatementBuilder(const TS::ICompilation& typeSystem,
                              const TS::ITypeResolveContext& decompilationContext,
                              IL::ILFunction* currentFunction,
                              const DecompilerSettings* settings,
                              const DecompileRun* decompileRun);

    // -- The translation entry family -------------------------------------------------

    // The C# `public Statement Convert(ILInstruction inst)`: the visitor dispatch
    // (the C# `inst.AcceptVisitor(this)` double dispatch through the port's OpCode
    // switch; the C# ThrowIfCancellationRequested is the documented no-op
    // deferral). The returned TranslatedStatement converts to its Statement
    // through the C# implicit operator.
    Syntax::Statement* Convert(IL::ILInstruction* inst);

    // The C# `public BlockStatement ConvertAsBlock(ILInstruction inst)`: re-attaches
    // the IL-instruction annotation and wraps the converted statement in a
    // BlockStatement unless it already is one (the C# `stmt as BlockStatement ??
    // new BlockStatement { stmt }`).
    Syntax::BlockStatement* ConvertAsBlock(IL::ILInstruction* inst);

    // The C# `internal readonly ExpressionBuilder exprBuilder` -- the expression
    // builder this statement builder owns (the C# GC roots it through the field;
    // the mutual reference back is the non-owning statementBuilder pointer).
    std::unique_ptr<ExpressionBuilder> exprBuilder;

    // -- The C# `internal` state fields ----------------------------------------------

    // The C# `readonly ILFunction currentFunction`.
    IL::ILFunction* currentFunction = nullptr;
    // The C# `internal BlockContainer currentReturnContainer` -- the body cast to
    // BlockContainer (the port's Body is statically a BlockContainer, so the C#
    // InvalidCastException shape is unreachable).
    IL::BlockContainer* currentReturnContainer = nullptr;
    // The C# `internal IType currentResultType` -- the async return type when the
    // function is async, the function's own return type otherwise (the C#
    // `currentFunction.IsAsync ? AsyncReturnType! : ReturnType`).
    const TS::IType* currentResultType = nullptr;
    // The C# `internal bool currentIsIterator` -- the YieldReturnDecompiler's flag.
    bool currentIsIterator = false;
    // The C# `internal bool EmitAsRefReadOnly` -- the CallBuilder's
    // EnforceExplicitIn writes it; the StatementBuilder's own consumption
    // (the as-readonly emission) is the StatementBuilder slice that owns it.
    bool EmitAsRefReadOnly = false;

    // -- The goto/leave state (StatementBuilder.cs lines 338-373 + 1576-1597) ------
    // The C# fields are private; the port's no-visibility-level convention keeps
    // them public for the tests (the VisitBlockContainer / TranslateSwitch slices
    // write them; the Visit arms consume them). All default to the C#-idle shape.

    // The C# `Dictionary<Block, ConstantResolveResult?>? caseLabelMapping` (line
    // 338): the switch's block->case-label mapping. Null when not translating a
    // switch; a mapped NULL value is the 'goto default' case (the C# nullable
    // value), a mapped non-null value the 'goto case <value>' target.
    using CaseLabelMapping = std::unordered_map<IL::Block*, std::shared_ptr<Sem::ConstantResolveResult>>;
    std::optional<CaseLabelMapping> caseLabelMapping;

    // The C# `Block? continueTarget` (line 341) + `int continueCount` (line 343):
    // the block a 'continue;' statement would continue to and how many
    // ContinueStatements were created for it (VisitBlockContainer seeds it).
    IL::Block* continueTarget = nullptr;
    int continueCount = 0;

    // The C# `BlockContainer? breakTarget` (line 371): the container a 'break;'
    // statement would break out of. Null when not inside a breakable construct.
    IL::BlockContainer* breakTarget = nullptr;

    // The C# `readonly Dictionary<BlockContainer, string> endContainerLabels` (line
    // 372): the 'goto end_<label>' name per escaped container (VisitLeave invents
    // the names; VisitBlockContainer emits the LabelStatements).
    std::unordered_map<IL::BlockContainer*, std::string> endContainerLabels;

    // The C# `readonly Dictionary<Block, string> labels` (line 1578) + `readonly
    // Dictionary<string, int> duplicateLabels` (line 1579): the block->label map
    // EnsureUniqueLabel fills and the label->occurrence-count map it shares with
    // the end-container naming (the same duplicateLabels dictionary in the C#).
    std::unordered_map<IL::Block*, std::string> labels;
    std::unordered_map<std::string, int> duplicateLabels;

    // The C# `string EnsureUniqueLabel(Block block)` (lines 1581-1597): the block's
    // IL_xxxx label, deduplicated through the labels map with the `_N` suffix the
    // shared duplicateLabels count produces. The C# is private; the port's
    // no-visibility-level convention keeps it public for the tests.
    std::string EnsureUniqueLabel(IL::Block* block);

    // -- The switch-construction region (StatementBuilder.cs lines 156-346) -------------

    // The C# `internal IEnumerable<ConstantResolveResult> CreateTypedCaseLabel(
    // long i, IType type, List<(string? Key, int Value)>? map = null)` (lines
    // 156-202): the typed case-label constant for a switch over the type -- the
    // boolean re-box, the string-map one-label-per-key, the enum underlying-type
    // cast, the primitive TypeCode cast, and the raw-long fallback. The C#
    // nullable-key tuple list ports to the optional-string pair vector. The C# is
    // internal; the port's no-visibility-level convention keeps it public.
    std::vector<std::shared_ptr<Sem::ConstantResolveResult>> CreateTypedCaseLabel(
        long long i, TS::IType& type,
        const std::vector<std::pair<std::optional<std::string>, int>>* map = nullptr);

    // The C# `SwitchStatement TranslateSwitch(BlockContainer? switchContainer,
    // SwitchInstruction inst)` (lines 208-320): the switch-statement render over
    // TranslateSwitchValue -- the per-section case labels (the default section's
    // bare label, the null label, the typed constants), the branch-body inlining
    // gate, the case-label mapping the VisitBranch goto-case arm consumes, the
    // default-only Leave-section removal, the remaining-blocks trailing labels,
    // and the end-container break. The C# is private; the port's
    // no-visibility-level-for-tests convention keeps it public (the
    // EnsureUniqueLabel precedent -- the VisitBlockContainer arm is still
    // deferred, so the container-driven shape is reachable only directly).
    Syntax::SwitchStatement* TranslateSwitch(IL::BlockContainer* switchContainer,
                                             IL::SwitchInstruction& inst);

private:
    // The C# `readonly IDecompilerTypeSystem typeSystem` / `DecompilerSettings
    // settings` / `internal readonly DecompileRun decompileRun` fields.
    const TS::ICompilation* typeSystem = nullptr;
    const DecompilerSettings* settings = nullptr;
    const DecompileRun* decompileRun = nullptr;

    // -- The visitor dispatch (the C# ILVisitor<TranslatedStatement> base) -----------

    // The port's stand-in for the C# AcceptVisitor double dispatch: the OpCode
    // switch over the landed Visit arms, falling back to Default (the C#'s own
    // fallback -- an ExpressionStatement over the expression translation).
    TranslatedStatement Visit(IL::ILInstruction* inst);

    // The C# `protected override TranslatedStatement Default(ILInstruction inst)`.
    TranslatedStatement Default(IL::ILInstruction* inst);

    // The C# `protected internal override TranslatedStatement VisitIsInst(IsInst
    // inst)`: the unused-result `is` test over the boxing-unwrapped argument.
    TranslatedStatement VisitIsInst(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitStLoc(StLoc
    // inst)` / VisitStObj sibling: the statement-level store renders the assignment
    // and strips the top-level ref on ref re-assignment.
    TranslatedStatement VisitStLoc(IL::ILInstruction* inst);
    TranslatedStatement VisitStObj(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitNop(Nop inst)`:
    // the empty statement with the nop's comment as trailing trivia.
    TranslatedStatement VisitNop(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement
    // VisitIfInstruction(IfInstruction inst)`: the if/else statement over the
    // translated condition (a false arm that is a Nop is the C#'s no-else shape).
    TranslatedStatement VisitIfInstruction(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitBranch(Branch
    // inst)` (lines 347-362): the continue / goto-case / goto-label fix, with the
    // continue arm first (the C# order) so a continue-target branch never renders
    // as a goto.
    TranslatedStatement VisitBranch(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitLeave(Leave
    // inst)` (lines 377-423): the break / yield-break / return / goto-end fix, with
    // the possible-loss-of-type-information cast the lambda/expr-tree arm inserts.
    TranslatedStatement VisitLeave(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitThrow(Throw
    // inst)` (lines 424-427): the throw statement over the translated argument.
    TranslatedStatement VisitThrow(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitRethrow(Rethrow
    // inst)` (lines 429-432): a bare throw statement (no expression).
    TranslatedStatement VisitRethrow(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitYieldReturn(
    // YieldReturn inst)` (lines 434-444): the yield return statement over the
    // element-typed value (the async return type, else the IEnumerable unwrap).
    TranslatedStatement VisitYieldReturn(IL::ILInstruction* inst);
    // The C# `TryCatchStatement MakeTryCatch(ILInstruction tryBlock)` (lines
    // 445-454): reuses a converted nested try-catch statement without a finally
    // block (the extend-existing path) or wraps the converted statement in a
    // fresh TryCatchStatement.
    Syntax::TryCatchStatement* MakeTryCatch(IL::ILInstruction* tryBlock);
    // The C# `protected internal override TranslatedStatement VisitTryCatch(
    // TryCatch inst)` (lines 456-485): the try/catch statement over the
    // converted try block and one CatchClause per handler (the caught variable's
    // name/type from its store counts, the `when` filter over every non-ldc.i4.1
    // filter).
    TranslatedStatement VisitTryCatch(IL::ILInstruction* inst);
    // The C# VisitTryFinally sibling (lines 486-492): the finally block over
    // MakeTryCatch's reused-or-wrapped try statement.
    TranslatedStatement VisitTryFinally(IL::ILInstruction* inst);
    // The C# VisitTryFault sibling (lines 493-505): the fault block becomes a
    // catch clause body carrying the 'try-fault' empty statement and a bare
    // throw.
    TranslatedStatement VisitTryFault(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement
    // VisitLockInstruction(LockInstruction inst)` (lines 506-510): the lock
    // statement over the translated monitor expression and the converted body.
    TranslatedStatement VisitLockInstruction(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitInitblk(Initblk
    // inst)` (lines 1609-1623): the Unsafe.InitBlock/InitBlockUnaligned intrinsic
    // call over the (address, value, size) translations with the IL comment trivia.
    TranslatedStatement VisitInitblk(IL::ILInstruction* inst);
    // The C# VisitCpblk sibling (lines 1627-1641): Unsafe.CopyBlock/CopyBlockUnaligned.
    TranslatedStatement VisitCpblk(IL::ILInstruction* inst);
    // The C# VisitCkfinite (lines 1645-1669): the `if (!float.IsFinite(<arg>)) throw
    // new ArithmeticException();` guard.
    TranslatedStatement VisitCkfinite(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement
    // VisitSwitchInstruction(SwitchInstruction inst)` (line 203): the switch
    // statement over TranslateSwitch (the null-container shape -- the container
    // driven shape comes through the VisitBlockContainer arm, still deferred).
    TranslatedStatement VisitSwitchInstruction(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitBlock(Block
    // block)` (line 1280): the ControlFlow block as a BlockStatement over its
    // instructions plus the non-Nop final instruction (the foreach conversion
    // arm inside the loop is deferred with the TransformToForeach machinery).
    TranslatedStatement VisitBlock(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement
    // VisitBlockContainer(BlockContainer container)` (line 1300): the loop /
    // switch-entry / plain-block dispatch over the container kind and the
    // entry point's incoming-edge count.
    TranslatedStatement VisitBlockContainer(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement
    // VisitPinnedRegion(PinnedRegion inst)` (lines 1201-1278): the `fixed`
    // statement over the pinned variable and its init expression -- the
    // GetPinnableReference unwrap, the pointer-to-ref retype, the
    // DirectionExpression address-of surgery, and the Unsafe.AsRef fallback for
    // an already-unmanaged pointer.
    TranslatedStatement VisitPinnedRegion(IL::ILInstruction* inst);

public:
    // The C# `private static bool IsAddressOfMoveableVar(Expression initExpr)`
    // (lines 1262-1271): whether an `&expr` init takes the address of a moveable
    // variable (the PointerArithmeticOffset.IsFixedVariable gate). The C# is
    // private; the port's no-visibility-level-for-tests convention keeps it public.
    static bool IsAddressOfMoveableVar(Syntax::Expression* initExpr);

    // The C# `private static bool IsFixedSizeBuffer(Expression initExpr)` (lines
    // 1273-1277): whether the init resolves to a fixed-size buffer field (the
    // CSharpDecompiler.IsFixedField predicate). The C# is private; the port's
    // no-visibility-level-for-tests convention keeps it public.
    static bool IsFixedSizeBuffer(Syntax::Expression* initExpr);

private:

    // The C# `private void ConvertSwitchSectionBody(Syntax.SwitchSection
    // astSection, ILInstruction bodyInst)` (lines 321-346): the converted body
    // plus the EndPointUnreachable-gated break insertion (into the body block
    // when the body converted to one, else as a trailing section statement).
    void ConvertSwitchSectionBody(Syntax::SwitchSection* astSection, IL::ILInstruction* bodyInst);

    // -- The block-container region (StatementBuilder.cs lines 1280-1608) -----------------------

    // The C# `Statement ConvertLoop(BlockContainer container)` (lines 1321-1430):
    // the four loop kinds -- Loop (the while-true shape with the entry-point
    // label removal), While (the condition-block shape with the reachability
    // break and the not-continue entry label), DoWhile (the last-block condition
    // shape), and For (the increment-block iterators). Declared private like the
    // C#; the tests drive through Convert's dispatch.
    Syntax::Statement* ConvertLoop(IL::BlockContainer* container);

    // The C# `BlockStatement ConvertBlockContainer(BlockContainer container,
    // bool isLoop)` (lines 1432-1465): the wrapper over the worker -- the
    // local-function declarations (the DeclareLocalFunctions deferral below) and
    // the ref-readonly helper emission for the function body container.
    Syntax::BlockStatement* ConvertBlockContainer(IL::BlockContainer* container, bool isLoop);

    // The C# `BlockStatement ConvertBlockContainer(BlockStatement blockStatement,
    // BlockContainer container, IEnumerable<Block> blocks, bool isLoop)` (lines
    // 1529-1608): the worker -- the per-block labels (any block with an incoming
    // multi-edge or a non-entry position), the instruction conversion with the
    // final-leave skip (the ImplicitReturnAnnotation) and the nested-block
    // flattening, the non-Nop final instruction, and the end-container label
    // (with the loop's continue/break pair).
    Syntax::BlockStatement* ConvertBlockContainer(Syntax::BlockStatement* blockStatement,
                                                   IL::BlockContainer* container,
                                                   const std::vector<IL::Block*>& blocks,
                                                   bool isLoop);

    // The C# `static bool IsFinalLeave(Leave leave)` (lines 1599-1608): the
    // value-less leave that is the very last instruction of the container's
    // last block targeting that container -- the function's implicit return.
    static bool IsFinalLeave(IL::Leave* leave);
};

}  // namespace ILSpy::Decompiler::CSharp
