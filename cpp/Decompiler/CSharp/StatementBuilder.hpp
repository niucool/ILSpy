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

// Port of ICSharpCode.Decompiler/CSharp/StatementBuilder.cs -- `sealed class
// StatementBuilder : ILVisitor<TranslatedStatement>`: the statement-level walk of
// the ILAst to the C# AST, the twin of the ExpressionBuilder (which it constructs
// and owns, passing `this`). This file is the FIRST SLICE: the class skeleton
// (ctor + state), `Convert`/`ConvertAsBlock`/`Default`, and the leaf statement
// visitors (`VisitIsInst`, `VisitStLoc`, `VisitStObj`, `VisitNop`, `VisitThrow`,
// `VisitRethrow`). Later slices land the branch state machine
// (`VisitBranch`/`VisitLeave`/the break/continue/label bookkeeping), the
// structured-control-flow visitors (`VisitIfInstruction`, `VisitSwitchInstruction`
// + `TranslateSwitchValue`, `VisitTryCatch`/`TryFinally`/`TryFault`,
// `VisitLockInstruction`, `VisitUsingInstruction`, `VisitBlock`/
// `VisitBlockContainer`, `VisitPinnedRegion`, `VisitInitblk`), and the
// `EnforceExplicitIn` flag consumer (`EmitAsRefReadOnly`).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `internal readonly ExpressionBuilder exprBuilder` ports to a
//      non-owning `unique_ptr` member the ctor allocates, passing `this` (the
//      C# GC reference convention; the ExpressionBuilder ctor takes the
//      non-owning pointer). The pointer is NON-CONST because the CallBuilder's
//      `EnforceExplicitIn` arm writes `EmitAsRefReadOnly` through it (the C#
//      writes through the GC reference).
//  (b) The C# ILVisitor double dispatch (`inst.AcceptVisitor(this)`) ports to a
//      dynamic_cast chain in `Convert` (the port's IL instruction nodes have no
//      visitor infrastructure; the ExpressionBuilder dispatch precedent).
//  (c) The C# `internal` members are widened to public for direct TDD (the
//      internal-widening convention); the per-instruction visitors stay in the
//      .cpp (the C# `protected internal override` surface).
//  (d) `currentReturnContainer` is a non-owning `BlockContainer*` over the
//      function body (the C# downcast of `currentFunction.Body`).
//  (e) The CancellationToken is a no-op in the port (the DecompileRun
//      convention).
//  (f) `currentResultType` is an owning `ITypePtr` (the C# `IType` GC
//      reference; `IsAsync ? AsyncReturnType : ReturnType`).

#pragma once

#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"
#include "Decompiler/CSharp/TranslatedStatement.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler {

class DecompileRun;
class ExpressionBuilder;

namespace IL {
class Block;
class BlockContainer;
class Block;
class Branch;
class ILFunction;
class IfInstruction;
class IsInst;
class Leave;
class LockInstruction;
class TryCatch;
class TryCatchHandler;
class TryFinally;
class TryFault;
class Nop;
class Rethrow;
class PinnedRegion;
class StLoc;
class StObj;
class SwitchInstruction;
class SwitchSection;
class UsingInstruction;
class Initblk;
class Cpblk;
class Ckfinite;
class Throw;
} // namespace IL

} // namespace ILSpy::Decompiler

namespace ILSpy::Decompiler::TypeSystem {
class ICompilation;
class ITypeResolveContext;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp {

class ExpressionBuilder;

// The TS alias (the CallBuilder TS:: convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The Syntax/Sem namespace aliases (the file-local conventions; the AST and
// Semantics types appear in the switch-family signatures).
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// Port of the C# `sealed class StatementBuilder : ILVisitor<TranslatedStatement>`
// (see the header comment). The C# ctor
// `StatementBuilder(IDecompilerTypeSystem typeSystem, ITypeResolveContext
// decompilationContext, ILFunction currentFunction, DecompilerSettings settings,
// DecompileRun decompileRun, CancellationToken cancellationToken)` ports to the
// 5-parameter form (the cancellation token dropped, convention (e)).
class StatementBuilder {
public:
    // The C# `internal readonly ExpressionBuilder exprBuilder` (convention (a)).
    std::unique_ptr<ExpressionBuilder> exprBuilder;
    // The C# `internal bool EmitAsRefReadOnly` -- the flag the CallBuilder's
    // `EnforceExplicitIn` arm sets for every `in T` argument it wraps; the
    // `VisitUsingInstruction` arm consults it.
    bool EmitAsRefReadOnly = false;

    // The C# ctor fields (conventions (d)/(f)); the currentFunction,
    // settings, and decompileRun handles ride the ExpressionBuilder's own
    // slots (the C# stores them on both builders; the port forwards).
    IL::BlockContainer* currentReturnContainer = nullptr;
    TS::ITypePtr currentResultType;
    bool currentIsIterator = false;
    // The C# `ILFunction currentFunction` field (read by VisitBlockContainer's
    // `currentFunction.Body == container` check and the DeclareLocalFunctions
    // walk).
    IL::ILFunction* currentFunction = nullptr;

    StatementBuilder(const ::ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
                     const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext& decompilationContext,
                     IL::ILFunction* currentFunction,
                     const DecompilerSettings* settings,
                     const DecompileRun* decompileRun);

    // The C# `public Statement Convert(ILInstruction inst)`: the ILVisitor
    // dispatch (convention (b)).
    TranslatedStatement Convert(IL::ILInstruction* inst);

    // The C# `public BlockStatement ConvertAsBlock(ILInstruction inst)`:
    // `Convert(inst).WithILInstruction(inst)`, then the `as BlockStatement ??
    // new BlockStatement { stmt }` wrap.
    TranslatedStatement ConvertAsBlock(IL::ILInstruction* inst);

    // The branch state machine (the C# fields at StatementBuilder.cs lines
    // 336-344): the continue/break/case-label/end-container bookkeeping the
    // loop- and switch-translation slices drive. `continueTarget` is a
    // non-owning Block*; `caseLabelMapping` maps a block to the constant case
    // value (`std::nullopt` marks the default case); `labels` /
    // `duplicateLabels` / `endContainerLabels` are the EnsureUniqueLabel /
    // end-label de-dup tables.
    IL::Block* continueTarget = nullptr;
    int continueCount = 0;
    // The C# `Dictionary<Block, ConstantResolveResult?>?` -- the port models
    // the nullability with an optional map; the value is the `ResolveResult`
    // shared handle, nullopt for the default case.
    std::optional<std::map<IL::Block*, std::optional<std::shared_ptr<Sem::ResolveResult>>>>
        caseLabelMapping;
    IL::BlockContainer* breakTarget = nullptr;
    std::map<IL::BlockContainer*, std::string> endContainerLabels;
    std::map<IL::Block*, std::string> labels;
    std::map<std::string, int> duplicateLabels;

    // The C# `string EnsureUniqueLabel(Block block)` (StatementBuilder.cs):
    // the per-block label with the duplicate `_N` suffixes.
    std::string EnsureUniqueLabel(IL::Block* block);

private:
    // The C# `protected internal override` leaf visitors this slice ports; the
    // private members are the C# `protected internal` surface (the port keeps
    // them out-of-line, declared here for the dynamic_cast dispatch).
    TranslatedStatement VisitIsInst(IL::IsInst* inst);
    TranslatedStatement VisitStLoc(IL::StLoc* inst);
    TranslatedStatement VisitStObj(IL::StObj* inst);
    TranslatedStatement VisitNop(IL::Nop* inst);
    TranslatedStatement VisitThrow(IL::Throw* inst);
    TranslatedStatement VisitRethrow(IL::Rethrow* inst);
    TranslatedStatement VisitBranch(IL::Branch* inst);
    TranslatedStatement VisitLeave(IL::Leave* inst);
    TranslatedStatement VisitIfInstruction(IL::IfInstruction* inst);
    TranslatedStatement VisitTryCatch(IL::TryCatch* inst);
    TranslatedStatement VisitTryFinally(IL::TryFinally* inst);
    TranslatedStatement VisitTryFault(IL::TryFault* inst);
    TranslatedStatement VisitLockInstruction(IL::LockInstruction* inst);
    TranslatedStatement VisitBlock(IL::Block* inst);
    TranslatedStatement VisitPinnedRegion(IL::PinnedRegion* inst);
    // The C# `TryCatchStatement MakeTryCatch(ILInstruction tryBlock)`: the
    // try-block conversion with the extend-existing-try-catch reuse.
    TranslatedStatement MakeTryCatch(IL::ILInstruction* tryBlock);

    // The switch family (the C# `VisitSwitchInstruction`/`TranslateSwitch`/
    // `CreateTypedCaseLabel`/`ConvertSwitchSectionBody`, CallBuilder.cs's
    // sibling slices at StatementBuilder.cs lines 156-346). The C#
    // `IEnumerable<ConstantResolveResult> CreateTypedCaseLabel(long, IType,
    // List<(string?, int)>?)` ports to a materializing vector (the eager-
    // iteration convention); the StringToInt map arm asserts (the node is not
    // ported).
    TranslatedStatement VisitSwitchInstruction(IL::SwitchInstruction* inst);
    Syntax::SwitchStatement* TranslateSwitch(IL::BlockContainer* switchContainer,
                                             IL::SwitchInstruction* inst);
    std::vector<std::shared_ptr<Sem::ConstantResolveResult>> CreateTypedCaseLabel(
        std::int64_t i, const TS::IType& type,
        const std::optional<std::vector<std::pair<std::optional<std::string>, int>>>& map)
        const;
    void ConvertSwitchSectionBody(Syntax::SwitchSection* astSection,
                                  IL::ILInstruction* bodyInst);
    IL::SwitchSection* GetDefaultSection(IL::SwitchInstruction* inst) const;

    // The block-container/loop family (the C# `VisitBlockContainer`/
    // `ConvertLoop`/`ConvertBlockContainer`/`DeclareLocalFunctions`,
    // StatementBuilder.cs lines 1300-1582). The C# IEnumerable block sequences
    // (Skip/Except/SkipLast) port to materialized vectors; the
    // TransformToForeachWithoutDispose arm is deferred with the foreach
    // surface; DeclareLocalFunctions throws when it would emit (the
    // local-function declaration machinery is not ported).
    TranslatedStatement VisitUsingInstruction(IL::UsingInstruction* inst);
    // The memory-instruction family (the C# `VisitInitblk`/`VisitCpblk`/
    // `VisitCkfinite`, StatementBuilder.cs lines 1609-1678): the Unsafe
    // intrinsic calls with the leading `IL ... instruction` comments and the
    // `float.IsFinite` throw-guard render.
    TranslatedStatement VisitInitblk(IL::Initblk* inst);
    TranslatedStatement VisitCpblk(IL::Cpblk* inst);
    TranslatedStatement VisitCkfinite(IL::Ckfinite* inst);
    TranslatedStatement VisitBlockContainer(IL::BlockContainer* container);
    Syntax::Statement* ConvertLoop(IL::BlockContainer* container);
    Syntax::BlockStatement* ConvertBlockContainer(IL::BlockContainer* container,
                                                  bool isLoop);
    void ConvertBlockContainer(Syntax::BlockStatement* blockStatement,
                               IL::BlockContainer* container,
                               const std::vector<IL::Block*>& blocks, bool isLoop);
    void DeclareLocalFunctions(IL::BlockContainer* container,
                               Syntax::BlockStatement* blockStatement);

private:
    // The C# `protected override TranslatedStatement Default(ILInstruction
    // inst)`: `new ExpressionStatement(exprBuilder.Translate(inst))` wrapped
    // with the IL instruction.
    TranslatedStatement Default(IL::ILInstruction* inst);
};

} // namespace ILSpy::Decompiler::CSharp