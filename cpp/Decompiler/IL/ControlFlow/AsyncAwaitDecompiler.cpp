// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
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
//
// The C# AsyncAwaitDecompiler body (part 1: the task-creation pattern).

#include "Decompiler/IL/ControlFlow/AsyncAwaitDecompiler.hpp"

#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"


#include "Decompiler/IL/ControlFlow/YieldReturnDecompiler.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"  // IsCompilerGeneratedStateMachine
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TaskType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

namespace ILSpy::Decompiler::IL {

namespace {

// The C# TaskType helpers' `ns` constant (TaskType.cs line 67).
constexpr const char* kBuilderNs = "System.Runtime.CompilerServices";

// The method-name tail of a reader-resolved call: the reader records
// "Namespace.Type::Method"; the C# reads `call.Method.Name` (the resolved
// method's bare name).
std::string MethodNameTail(const std::string& methodName) {
    std::size_t pos = methodName.rfind("::");
    return pos == std::string::npos ? methodName
                                   : methodName.substr(pos + 2);
}

// The metadata type name without the arity suffix: the port's reader types
// carry the raw metadata name ("AsyncTaskMethodBuilder`1"), while the C#
// `IType.Name` strips the arity (the TopLevelTypeName comparison the C#
// performs sees the bare name).
std::string NameWithoutArity(const std::string& name) {
    std::size_t pos = name.rfind('`');
    if (pos == std::string::npos) return name;
    for (std::size_t i = pos + 1; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9')
            return name;  // not an arity suffix
    }
    return name.substr(0, pos);
}

// The port's position reader: the C# reads the block's instruction list
// (the terminators included); the port's reader carries the terminator in
// the FinalInstruction slot, so a position at the end of the list reads the
// final (the YieldReturnDecompiler InstructionAt precedent).
ILInstruction* BlockInstructionAt(Block* block, int pos) {
    if (block == nullptr)
        return nullptr;
    if (pos >= 0 && pos < static_cast<int>(block->Instructions.size()))
        return block->Instructions[static_cast<std::size_t>(pos)].get();
    if (pos == static_cast<int>(block->Instructions.size()))
        return block->FinalInstruction.get();
    return nullptr;
}

// The shared PatternMatching MatchStFld (stobj over ldflda), unshadowed
// from the class's stateMachineVar-typed member.
bool MatchStFldRaw(ILInstruction* inst, ILInstruction*& target,
                   const TypeSystem::IField*& field, ILInstruction*& value) {
    return ILSpy::Decompiler::IL::MatchStFld(inst, target, field, value);
}

// File-local MatchLdcI4 out-form (the C# `MatchLdcI4(out int value)`): an
// LdcI4 reporting its value. The shared MatchLdcI4 overload is the
// match-against-value form (the D66/D94 discipline).
bool MatchLdcI4Out(ILInstruction* inst, int& value) {
    auto* ldc = dynamic_cast<LdcI4*>(inst);
    if (ldc == nullptr) {
        value = 0;
        return false;
    }
    value = ldc->Value;
    return true;
}

// File-local MatchLdLoca out-form (the C# `MatchLdLoca(out ILVariable
// variable)`): an LdLoca reporting its variable. Same discipline as
// MatchLdLocOut below.
bool MatchLdLocaOut(ILInstruction* inst, ILVariable*& variable) {
    auto* ldloca = dynamic_cast<LdLoca*>(inst);
    if (ldloca == nullptr) {
        variable = nullptr;
        return false;
    }
    variable = ldloca->Variable.get();
    return true;
}

// File-local MatchLdLoc out-form (the C# `MatchLdLoc(out ILVariable
// variable)`): an LdLoc reporting its variable. The shared MatchLdLoc is
// the match-against form (the D66/D94 discipline).
bool MatchLdLocOut(ILInstruction* inst, ILVariable*& variable) {
    auto* ldloc = dynamic_cast<LdLoc*>(inst);
    if (ldloc == nullptr) {
        variable = nullptr;
        return false;
    }
    variable = ldloc->Variable.get();
    return true;
}

} // namespace

void AsyncAwaitDecompiler::Run(ILFunction& function,
                               ILTransformContext& context) {
    if (!context.Settings.AsyncAwait)
        return;  // abort if async/await decompilation is disabled
    context_ = &context;
    fieldToParameterMap_.clear();

    // The port's deferred-resolution convention: the pattern matchers read
    // field and method identities from the driver-decoded body, so the
    // reader surfaces resolve first (the CreateILAst precedent).
    YieldReturnDecompiler::ResolveReaderSurfaces(function, context);

    if (!MatchTaskCreationPattern(function)) {
        // The async-enumerator creation pattern and the runtime-async
        // transforms are deferred with their slices.
        return;
    }

    try {
        AnalyzeMoveNext();
        ValidateCatchBlock();
        AnalyzeDisposeAsync();
    } catch (const ControlFlow::SymbolicAnalysisFailedException&) {
        return;
    }

    InlineBodyOfMoveNext(function);
    CleanUpBodyOfMoveNext(function);

    // SLICE STATE (part 3): AnalyzeStateMachine + DetectAwaitPattern +
    // CleanDoFinallyBodies + the field translations + FinalizeInlineMoveNext.
    // Until they land, Run stops after the body inlining (the function is
    // marked async and carries the inlined MoveNext body, but the await
    // points stay as their raw state-machine instructions).
}

void AsyncAwaitDecompiler::AnalyzeMoveNext() {
    const Metadata::MetadataFile& metadata = *context_->Metadata;
    // The C# `metadata.GetTypeDefinition(stateMachineType).GetMethods()
    // .FirstOrDefault(f => ...Name == "MoveNext")` -- the raw-token scan.
    std::uint32_t moveNextMethod = 0;
    for (const auto& m : metadata.GetMethods(stateMachineType_->MetadataToken())) {
        if (m.Name == "MoveNext") {
            moveNextMethod = m.Token;
            break;
        }
    }
    if (moveNextMethod == 0)
        throw ControlFlow::SymbolicAnalysisFailedException("MoveNext not found");
    moveNextFunction_ = YieldReturnDecompiler::CreateILAst(moveNextMethod, *context_);
    auto* blockContainer =
        dynamic_cast<BlockContainer*>(moveNextFunction_->Body.get());
    if (blockContainer == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("MoveNext body not a container");
    Block* entry = blockContainer->EntryPoint();
    if (entry == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("no entry block");
    // The C# `entry.IncomingEdgeCount != 1` -- the C# counts the function
    // entry as the one incoming edge; the port's edge count tracks branch
    // predecessors only, so the entry's count is 0 (no branch targets it).
    if (entry->IncomingEdgeCount != 0) {
        throw ControlFlow::SymbolicAnalysisFailedException("entry incoming edges");
    }
    blocksAnalyzed_.assign(blockContainer->Blocks.size(), false);
    cachedStateVar_ = nullptr;
    int pos = 0;
    // Visual Basic state machines initialize doFinallyBodies at the start of
    // MoveNext(): stloc doFinallyBodies(ldc.i4 1). Deferred with the VB
    // arms (isVisualBasicStateMachine is always false).
    while (pos < static_cast<int>(entry->Instructions.size()) &&
           dynamic_cast<StLoc*>(entry->Instructions[static_cast<std::size_t>(
                                    pos)].get()) != nullptr) {
        auto* stloc = static_cast<StLoc*>(
            entry->Instructions[static_cast<std::size_t>(pos)].get());
        // stloc V_1(ldfld <>4__this(ldloc this))
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        if (!MatchLdFld(stloc->Value.get(), target, field))
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
        if (!MatchLdThis(target))
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
        const TypeSystem::IMember* definition =
            field != nullptr ? field->MemberDefinition() : nullptr;
        if (definition == stateField_ && cachedStateVar_ == nullptr) {
            // stloc(cachedState, ldfld(valuetype StateMachineStruct::<>1__state, ldloc(this)))
            cachedStateVar_ = stloc->Variable.get();
        } else {
            auto it = fieldToParameterMap_.find(
                static_cast<const TypeSystem::IField*>(definition));
            if (it != fieldToParameterMap_.end()) {
                if (stloc->Variable == nullptr ||
                    !stloc->Variable->IsSingleDefinition())
                    throw ControlFlow::SymbolicAnalysisFailedException("entry stloc not single-def");
                // cachedFieldToParameterMap is deferred with the
                // TranslateCachedFieldsToLocals arm (part 3).
            } else {
                throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
            }
        }
        pos++;
    }
    // The port's post-pipeline shape: the aggressive EarlyILTransforms
    // inline the cached-state stores away, so the try-catch is the entry
    // block's instruction at the first non-stloc position (the C# reads the
    // same position).
    if (pos >= static_cast<int>(entry->Instructions.size()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    mainTryCatch_ = dynamic_cast<TryCatch*>(
        entry->Instructions[static_cast<std::size_t>(pos)].get());
    if (mainTryCatch_ == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    // CatchHandler will be validated in ValidateCatchBlock()

    // stloc doFinallyBodies(ldc.i4 1)
    if (auto* tryContainer =
            dynamic_cast<BlockContainer*>(mainTryCatch_->TryBlock.get())) {
        Block* tryEntry = tryContainer->EntryPoint();
        if (tryEntry != nullptr && !tryEntry->Instructions.empty()) {
            auto* initDoFinallyBodies = dynamic_cast<StLoc*>(
                tryEntry->Instructions[0].get());
            if (initDoFinallyBodies != nullptr &&
                initDoFinallyBodies->Variable != nullptr &&
                initDoFinallyBodies->Variable->Kind == VariableKind::Local &&
                initDoFinallyBodies->Variable->Type != nullptr &&
                TypeSystem::IsKnownType(
                    *initDoFinallyBodies->Variable->Type,
                    TypeSystem::KnownTypeCode::Boolean) &&
                MatchLdcI4(initDoFinallyBodies->Value.get(), 1)) {
                doFinallyBodies_ = initDoFinallyBodies->Variable.get();
            }
        }
    }

    // The port's reader emits an explicit branch for the fall-through into
    // the try-catch block (the C# block layout has no such branch); the
    // blocks after the entry follow the C# indexing once the entry block
    // holds the try-catch.
    blocksAnalyzed_[0] = true;
    int blockPos = 1;
    setResultYieldBlock_ = nullptr;  // MatchYieldBlock (enumerator-only, deferred)
    setResultReturnBlock_ =
        CheckSetResultReturnBlock(blockContainer, blockPos, blocksAnalyzed_);

    for (bool analyzed : blocksAnalyzed_) {
        if (!analyzed)
            throw ControlFlow::SymbolicAnalysisFailedException("too many blocks");
    }
}

Block* AsyncAwaitDecompiler::CheckSetResultReturnBlock(
    BlockContainer* blockContainer, int setResultReturnBlockIndex,
    std::vector<bool>& blocksAnalyzed) {
    if (setResultReturnBlockIndex >=
        static_cast<int>(blockContainer->Blocks.size())) {
        // This block can be absent if the function never exits normally,
        // but always throws an exception/loops infinitely.
        resultVar_ = nullptr;
        finalStateKnown_ = false;  // final state will be detected in ValidateCatchBlock() instead
        return nullptr;
    }

    Block* block =
        blockContainer->Blocks[static_cast<std::size_t>(
                                  setResultReturnBlockIndex)]
            .get();

    int pos = 0;
    // [vb-only] stloc S_10(ldloc this)
    if (pos < static_cast<int>(block->Instructions.size()) &&
        MatchLdThis(block->Instructions[static_cast<std::size_t>(pos)]
                        .get())) {
        // handled by the VB-only slot check below (skipped)
    }
    if (pos < static_cast<int>(block->Instructions.size())) {
        auto* stlocThisCache = dynamic_cast<StLoc*>(
            block->Instructions[static_cast<std::size_t>(pos)].get());
        if (stlocThisCache != nullptr &&
            stlocThisCache->Value != nullptr &&
            MatchLdThis(stlocThisCache->Value.get()) &&
            stlocThisCache->Variable != nullptr &&
            stlocThisCache->Variable->Kind == VariableKind::StackSlot) {
            pos++;
        }
    }
    // [vb-only] stloc S_11(ldc.i4 -2)
    if (pos < static_cast<int>(block->Instructions.size())) {
        auto* stlocFinalState = dynamic_cast<StLoc*>(
            block->Instructions[static_cast<std::size_t>(pos)].get());
        if (stlocFinalState != nullptr &&
            dynamic_cast<LdcI4*>(stlocFinalState->Value.get()) != nullptr &&
            stlocFinalState->Variable != nullptr &&
            stlocFinalState->Variable->Kind == VariableKind::StackSlot) {
            pos++;
        }
    }

    // stfld <>1__state(ldloc this, ldc.i4 -2)
    if (pos >= static_cast<int>(block->Instructions.size()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!MatchStateAssignment(
            block->Instructions[static_cast<std::size_t>(pos)].get(),
            finalState_))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    finalStateKnown_ = true;
    pos++;

    // [optional] stfld <>u__N(ldloc this, ldnull)
    MatchHoistedLocalCleanup(block, pos);

    // The async-iterator combined-tokens disposal (the
    // MatchDisposeCombinedTokens arm) is deferred with the enumerator
    // slice; a block whose remaining shape matches it fails the
    // SetResultAndExit check below (the documented rollback).

    MatchHoistedLocalCleanup(block, pos);
    CheckSetResultAndExit(blockContainer, block, pos);
    blocksAnalyzed[static_cast<std::size_t>(block->ChildIndex)] = true;
    return blockContainer
        ->Blocks[static_cast<std::size_t>(setResultReturnBlockIndex)]
        .get();
}

void AsyncAwaitDecompiler::MatchHoistedLocalCleanup(Block* block, int& pos) {
    while (pos < static_cast<int>(block->Instructions.size())) {
        // https://github.com/dotnet/roslyn/pull/39735 hoisted local cleanup
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* value = nullptr;
        if (!MatchStFldRaw(BlockInstructionAt(block, pos),
                           target, field, value))
            break;
        if (!MatchLdThis(target))
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
        if (!MatchDefaultOrNullOrZero(value))
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
        pos++;
    }
}

void AsyncAwaitDecompiler::CheckSetResultAndExit(BlockContainer* blockContainer,
                                                 Block* block, int& pos) {
    // [optional] call Complete(ldflda <>t__builder(ldloc this)) (Roslyn >=3.9)
    // call SetResult(ldflda <>t__builder(ldloc this), ldloc result)
    // [optional] call Complete(ldflda <>t__builder(ldloc this))
    // leave IL_0000
    MatchCompleteCall(block, pos);
    if (pos >= static_cast<int>(block->Instructions.size()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    std::vector<ILInstruction*> args;
    if (!MatchCall(block->Instructions[static_cast<std::size_t>(pos)].get(),
                   "SetResult", args))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!IsBuilderOrPromiseFieldOnThis(args.empty() ? nullptr : args[0]))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    switch (methodType_) {
        case AsyncMethodType::TaskOfT:
            if (args.size() != 2)
                throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
            if (!MatchLdLocOut(args[1], resultVar_))
                throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
            break;
        case AsyncMethodType::Task:
        case AsyncMethodType::Void:
            resultVar_ = nullptr;
            if (args.size() != 1)
                throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
            break;
        case AsyncMethodType::AsyncEnumerable:
        case AsyncMethodType::AsyncEnumerator:
            resultVar_ = nullptr;
            if (args.size() != 2)
                throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
            if (!MatchLdcI4(args[1], 0))
                throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
            break;
    }
    pos++;
    MatchCompleteCall(block, pos);
    if (BlockInstructionAt(block, pos) == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!MatchLeave(BlockInstructionAt(block, pos),
                    blockContainer))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
}

void AsyncAwaitDecompiler::MatchCompleteCall(Block* block, int& pos) {
    if (pos >= static_cast<int>(block->Instructions.size()))
        return;
    std::vector<ILInstruction*> args;
    if (MatchCall(BlockInstructionAt(block, pos),
                  "Complete", args)) {
        if (!(args.size() == 1 && IsBuilderFieldOnThis(args[0])))
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
        pos++;
    }
}

bool AsyncAwaitDecompiler::IsBuilderFieldOnThis(ILInstruction* inst) {
    ILInstruction* target = nullptr;
    const TypeSystem::IField* field = nullptr;
    const std::optional<bool> builderIsRef =
        builderType_ != nullptr ? builderType_->IsReferenceType()
                                : std::optional<bool>{};
    if (builderIsRef.has_value() && *builderIsRef) {
        // ldfld(StateMachine::<>t__builder, ldloc(this))
        if (!MatchLdFld(inst, target, field))
            return false;
    } else {
        // ldflda(StateMachine::<>t__builder, ldloc(this))
        if (!MatchLdFlda(inst, target, field))
            return false;
    }
    return MatchLdThis(target) &&
           field != nullptr && field->MemberDefinition() == builderField_;
}

bool AsyncAwaitDecompiler::IsBuilderOrPromiseFieldOnThis(ILInstruction* inst) {
    // The enumerator promise fields accept any field (the C# TODO); the
    // enumerator shapes are deferred, so this reduces to the builder check.
    return IsBuilderFieldOnThis(inst);
}

bool AsyncAwaitDecompiler::MatchStateAssignment(ILInstruction* inst,
                                                int& newState) {
    // stfld(StateMachine::<>1__state, ldloc(this), ldc.i4(stateId))
    ILInstruction* target = nullptr;
    const TypeSystem::IField* field = nullptr;
    ILInstruction* value = nullptr;
    if (MatchStFldRaw(inst, target, field, value) &&
        MatchLdThis(target) &&
        field != nullptr && field->MemberDefinition() == stateField_ &&
        MatchLdcI4Out(value, newState)) {
        return true;
    }
    newState = 0;
    return false;
}

void AsyncAwaitDecompiler::ValidateCatchBlock() {
    // catch E_143 : System.Exception if (ldc.i4 1) BlockContainer { ... }
    if (mainTryCatch_ == nullptr || mainTryCatch_->Handlers.size() != 1)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    auto& handler = mainTryCatch_->Handlers[0];
    if (handler->Variable == nullptr || handler->Variable->Type == nullptr ||
        !TypeSystem::IsKnownType(*handler->Variable->Type,
                                 TypeSystem::KnownTypeCode::Exception))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!MatchLdcI4(handler->Filter.get(), 1))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    auto* handlerContainer =
        dynamic_cast<BlockContainer*>(handler->Body.get());
    if (handlerContainer == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    std::vector<bool> blocksAnalyzed(handlerContainer->Blocks.size(), false);
    Block* catchBlock = handlerContainer->EntryPoint();
    if (catchBlock == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    int pos = 0;
    // [vb-only] call SetProjectError(ldloc E_143) -- deferred with the VB
    // arms.
    // stloc exception(ldloc E_143)
    if (pos >= static_cast<int>(catchBlock->Instructions.size()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    auto* stloc = dynamic_cast<StLoc*>(
        catchBlock->Instructions[static_cast<std::size_t>(pos++)].get());
    if (stloc == nullptr || stloc->Value == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!MatchLdLoc(stloc->Value.get(), handler->Variable.get()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    // stfld <>1__state(ldloc this, ldc.i4 -2)
    if (pos >= static_cast<int>(catchBlock->Instructions.size()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    int newState = 0;
    if (!MatchStateAssignment(
            catchBlock->Instructions[static_cast<std::size_t>(pos++)].get(),
            newState))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (finalStateKnown_) {
        if (newState != finalState_)
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    } else {
        finalState_ = newState;
        finalStateKnown_ = true;
    }

    // [optional] stfld <>u__N(ldloc this, ldnull)
    MatchHoistedLocalCleanup(catchBlock, pos);
    // [optional] call Complete(ldfld <>t__builder(ldloc this))
    MatchCompleteCall(catchBlock, pos);

    // call SetException(ldfld <>t__builder(ldloc this), ldloc exception)
    if (BlockInstructionAt(catchBlock, pos) == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    std::vector<ILInstruction*> args;
    if (!MatchCall(
            BlockInstructionAt(catchBlock, pos),
            "SetException", args))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (args.size() != 2)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!IsBuilderOrPromiseFieldOnThis(args[0]))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!MatchLdLoc(args[1], stloc->Variable.get()))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");

    pos++;
    // [optional] call Complete(ldfld <>t__builder(ldloc this))
    MatchCompleteCall(catchBlock, pos);
    // [vb-only] call ClearProjectError() -- deferred with the VB arms.

    // leave IL_0000
    if (BlockInstructionAt(catchBlock, pos) == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    if (!MatchLeave(BlockInstructionAt(catchBlock, pos),
                    dynamic_cast<BlockContainer*>(
                        moveNextFunction_->Body.get())))
        throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    blocksAnalyzed[static_cast<std::size_t>(catchBlock->ChildIndex)] = true;
    for (bool analyzed : blocksAnalyzed) {
        if (!analyzed)
            throw ControlFlow::SymbolicAnalysisFailedException("async analysis failed");
    }
}

void AsyncAwaitDecompiler::AnalyzeDisposeAsync() {
    // The disposeModeField (the enumerator-only surface): a no-op for the
    // task shapes.
}

void AsyncAwaitDecompiler::InlineBodyOfMoveNext(ILFunction& function) {
    context_->StepOnce("Inline body of MoveNext()");
    // The C# `function.Body = mainTryCatch.TryBlock` -- the try block is a
    // BlockContainer node; the static cast is the C# reference assignment.
    function.Body.reset(
        static_cast<BlockContainer*>(mainTryCatch_->TryBlock.release()));
    function.AsyncReturnType = underlyingReturnType_;
    function.IsIterator = false;  // the enumerator shapes are deferred
    // The C# clears moveNextFunction.Variables and takes the body out; the
    // port's unique_ptr ownership transfers the try block to the function
    // and leaves the moveNext function holding the catch handler.
    std::vector<ILInstruction*> stack{function.Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* branch = dynamic_cast<Branch*>(node)) {
            if (branch->TargetBlock == setResultReturnBlock_) {
                // The result variable stays owned by the state
                // machine's function; the LdLoc carries a non-owning alias
                // (an owning shared_ptr over the raw pointer would double
                // free it when the leave dies).
                std::unique_ptr<ILInstruction> value =
                    resultVar_ != nullptr
                        ? std::unique_ptr<ILInstruction>(
                              new LdLoc(ILVariablePtr(ILVariablePtr(),
                                                      resultVar_)))
                        : nullptr;
                std::unique_ptr<ILInstruction> replacement(
                    new Leave(dynamic_cast<BlockContainer*>(function.Body.get()),
                              std::move(value)));
                replacement->SetILRange(*branch);
                branch->ReplaceWith(std::move(replacement));
                // ReplaceWith destroys the branch (the port's unique_ptr
                // ownership); the C# GC lets the walk continue over the
                // replaced node, but the branch carries no children, so
                // skipping its child walk is the same traversal.
                continue;
            }
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    // The C# adds the setResultYieldBlock (the enumerator shape) and
    // retargets the leaves from moveNextFunction.Body to the new body --
    // the enumerator arms are deferred, and the function-level leaves
    // already target the (transferred) container.
    // Register the variables the inlined body uses.
    if (resultVar_ != nullptr)
        function.RegisterExistingVariable(
            ILVariablePtr(ILVariablePtr(), resultVar_));
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* stloc = dynamic_cast<StLoc*>(node)) {
                if (stloc->Variable != nullptr)
                    function.RegisterExistingVariable(stloc->Variable);
            } else if (auto* ldloc = dynamic_cast<LdLoc*>(node)) {
                if (ldloc->Variable != nullptr)
                    function.RegisterExistingVariable(ldloc->Variable);
            } else if (auto* ldloca = dynamic_cast<LdLoca*>(node)) {
                if (ldloca->Variable != nullptr)
                    function.RegisterExistingVariable(ldloca->Variable);
            }
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
}

void AsyncAwaitDecompiler::CleanUpBodyOfMoveNext(ILFunction& function) {
    context_->StepOnce("CleanUpBodyOfMoveNext");
    // Copy-propagate stack slots holding an 'ldloca'.
    {
        std::vector<StLoc*> stlocs;
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* stloc = dynamic_cast<StLoc*>(node)) {
                if (stloc->Variable != nullptr &&
                    stloc->Variable->Kind == VariableKind::StackSlot &&
                    stloc->Variable->IsSingleDefinition() &&
                    stloc->Value != nullptr &&
                    dynamic_cast<LdLoca*>(stloc->Value.get()) != nullptr)
                    stlocs.push_back(stloc);
            }
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
        for (StLoc* stloc : stlocs)
            CopyPropagation::Propagate(stloc, *context_);
    }

    // Simplify stobj(ldloca) -> stloc
    {
        std::vector<StObj*> stobjs;
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* stobj = dynamic_cast<StObj*>(node))
                stobjs.push_back(stobj);
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
        (void)stobjs;
    }
    StObjToStLoc().Run(function, *context_);

    // The Visual Basic builder-field stack stores and the lone ldc.i4
    // removal are deferred with the VB arms.

    // Copy-propagate temporaries holding a copy of 'this'.
    {
        std::vector<StLoc*> stlocs;
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* stloc = dynamic_cast<StLoc*>(node)) {
                if (stloc->Variable != nullptr &&
                    stloc->Variable->IsSingleDefinition() &&
                    stloc->Value != nullptr &&
                    MatchLdThis(stloc->Value.get()))
                    stlocs.push_back(stloc);
            }
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
        for (StLoc* stloc : stlocs)
            CopyPropagation::Propagate(stloc, *context_);
    }
    RemoveDeadVariableInit().Run(function, *context_);
    // The per-block inlining (the C# InlineAllInBlock; the enumerator
    // ldc.i4 removal rides with the enumerator slice).
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* block = dynamic_cast<Block*>(node)) {
                // Inline, but don't remove dead variables (they might get
                // revived by the field translation).
                for (int i = static_cast<int>(block->Instructions.size()) - 1;
                     i >= 0; i--) {
                    InlineOneIfPossible(block, i, *context_);
                }
            }
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
}

bool AsyncAwaitDecompiler::MatchTaskCreationPattern(
    ILFunction& function, ILTransformContext& context) {
    context_ = &context;
    YieldReturnDecompiler::ResolveReaderSurfaces(function, context);
    return MatchTaskCreationPattern(function);
}

bool AsyncAwaitDecompiler::MatchTaskCreationPattern(ILFunction& function) {
    methodType_ = AsyncMethodType::Void;
    stateMachineType_ = nullptr;
    builderType_ = nullptr;
    builderField_ = nullptr;
    stateField_ = nullptr;
    initialState_ = 0;
    fieldToParameterMap_.clear();

    auto* blockContainer = dynamic_cast<BlockContainer*>(function.Body.get());
    if (blockContainer == nullptr)
        return false;
    if (blockContainer->Blocks.size() != 1)
        return false;
    // The port's terminator convention: the reader carries the method's
    // `ret` in the FinalInstruction slot, so the C# instruction list maps
    // to the port's Instructions followed by the final. The accessor below
    // keeps the C# index arithmetic verbatim (a C# list index < size reads
    // the port's instruction; the last index reads the final).
    Block* entry = blockContainer->EntryPoint();
    const std::vector<std::unique_ptr<ILInstruction>>& body =
        entry->Instructions;
    const int count = static_cast<int>(body.size()) +
                      (entry->FinalInstruction != nullptr ? 1 : 0);
    if (count < 4)
        return false;
    auto BodyAt = [&](int index) -> ILInstruction* {
        if (index >= 0 && index < static_cast<int>(body.size()))
            return body[static_cast<std::size_t>(index)].get();
        if (index == static_cast<int>(body.size()) &&
            entry->FinalInstruction != nullptr)
            return entry->FinalInstruction.get();
        return nullptr;
    };
    ILInstruction* bodyLast =
        entry->FinalInstruction != nullptr ? entry->FinalInstruction.get()
                                           : (!body.empty()
                                                  ? body.back().get()
                                                  : nullptr);

    // Check the second-to-last instruction (the start call) first, as we can
    // get the most information from that.
    int pos = count - 2;
    auto* startCall = dynamic_cast<Call*>(BodyAt(pos));
    if (startCall == nullptr)
        return false;
    if (MethodNameTail(startCall->MethodName) != "Start")
        return false;
    if (function.Method == nullptr)
        return false;
    taskType_ = std::const_pointer_cast<TypeSystem::IType>(
        std::shared_ptr<const TypeSystem::IType>(
            std::shared_ptr<const TypeSystem::IType>(),
            &function.Method->ReturnType()));
    // The builder type: the start call's declaring type (the reader
    // resolves the call token's parent; the C# reads
    // startCall.Method.DeclaringType).
    builderType_ = startCall->DeclaringType;
    if (builderType_ == nullptr)
        return false;
    if (!ClassifyTaskType(function, startCall, *builderType_)) {
        return false;
    }
    if (startCall->Arguments.size() != 2)
        return false;
    ILInstruction* loadBuilderExpr = startCall->Arguments[0].get();
    ILVariable* stateMachineVar = nullptr;
    if (!MatchLdLocaOut(startCall->Arguments[1].get(), stateMachineVar))
        return false;
    if (stateMachineVar == nullptr || stateMachineVar->Type == nullptr)
        return false;
    stateMachineType_ = ResolveStateMachineType(
        *stateMachineVar->Type, *context_->Metadata, *context_);
    if (stateMachineType_ == nullptr) {
        return false;
    }
    pos--;

    // A reference-type builder is copied to a local first
    // (`stloc builder(ldfld StateMachine::<>t__builder(ldloc stateMachine))`).
    if (MatchLdLocRef(loadBuilderExpr, stateMachineVar)) {
        // Check third-to-last instruction (copy of builder).
        ILVariable* builderVar = nullptr;
        if (pos < 0 ||
            !MatchStLoc(body[static_cast<std::size_t>(pos)].get(), builderVar,
                       loadBuilderExpr) ||
            builderVar == nullptr)
            return false;
        pos--;
    }
    ILInstruction* loadStateMachineForBuilderExpr = nullptr;
    if (MatchLdFld(loadBuilderExpr, loadStateMachineForBuilderExpr, builderField_)) {
        // OK, calling Start on copy of stateMachine.<>t__builder
    } else if (MatchLdFlda(loadBuilderExpr, loadStateMachineForBuilderExpr,
                           builderField_)) {
        // OK, Roslyn 3.6 started directly calling Start without making a copy
    } else {
        return false;
    }
    builderField_ = builderField_ != nullptr
                        ? static_cast<const TypeSystem::IField*>(
                              builderField_->MemberDefinition())
                        : nullptr;
    if (!(MatchLdLocRef(loadStateMachineForBuilderExpr, stateMachineVar) ||
          MatchLdLoc(loadStateMachineForBuilderExpr, stateMachineVar))) {
        return false;
    }

    // Check the last instruction (ret). The port's terminator convention:
    // the C# `body.Last()` is the FinalInstruction.
    if (methodType_ == AsyncMethodType::Void) {
        if (!MatchLeave(bodyLast, blockContainer)) {
            return false;
        }
    } else {
        // ret(call(AsyncTaskMethodBuilder::get_Task, ldflda(StateMachine::<>t__builder, ldloca(stateMachine))))
        ILInstruction* returnValue = nullptr;
        if (!MatchReturn(bodyLast, returnValue)) {
            return false;
        }
        std::vector<ILInstruction*> getTaskArgs;
        if (returnValue == nullptr ||
            !MatchCall(returnValue, "get_Task", getTaskArgs) ||
            getTaskArgs.size() != 1)
            return false;
        ILInstruction* target = nullptr;
        const TypeSystem::IField* builderField2 = nullptr;
        const std::optional<bool> builderIsRef = builderType_->IsReferenceType();
        if (builderIsRef.has_value() && *builderIsRef) {
            if (!MatchLdFld(getTaskArgs[0], target, builderField2))
                return false;
        } else {
            if (!MatchLdFlda(getTaskArgs[0], target, builderField2))
                return false;
        }
        if (builderField2 == nullptr ||
            builderField2->MemberDefinition() != builderField_)
            return false;
        if (!(MatchLdLoc(target, stateMachineVar) ||
              MatchLdLoca(target, stateMachineVar)))
            return false;
    }

    // The Visual Basic state machine initialization order is deferred with
    // the VB arms (the IsPotentialVisualBasicStateMachineInitialiation
    // check). The C# state machine initialization follows.

    // Check the last field assignment - this should be the state field
    // stfld <>1__state(ldloca stateField, ldc.i4 -1)
    ILInstruction* initialStateExpr = nullptr;
    if (pos < 0 ||
        !MatchStFld(BodyAt(pos), stateMachineVar,
                    stateField_, initialStateExpr))
        return false;
    if (!MatchLdcI4Out(initialStateExpr, initialState_))
        return false;
    if (initialState_ != -1)
        return false;

    int stopPos = pos;
    pos = 0;
    if (stateMachineType_->Kind() == TypeSystem::TypeKind::Class) {
        // If state machine is a class, the first instruction creates an
        // instance: stloc stateMachine(newobj StateMachine.ctor())
        ILVariable* var = nullptr;
        ILInstruction* init = nullptr;
        if (!MatchStLoc(BodyAt(pos), var, init) ||
            var != stateMachineVar)
            return false;
        auto* newobj = dynamic_cast<Call*>(init);
        if (newobj == nullptr || !newobj->IsNewObj || !newobj->Arguments.empty())
            return false;
        pos++;
    }
    bool builderFieldIsInitialized = false;
    for (; pos < stopPos; pos++) {
        // stfld StateMachine.field(ldloca stateMachine, ldvar(param))
        const TypeSystem::IField* field = nullptr;
        ILInstruction* fieldInit = nullptr;
        if (!MatchStFld(BodyAt(pos), stateMachineVar, field, fieldInit))
            return false;
        if (field == builderField_) {
            // stfld StateMachine.builder(ldloca stateMachine, call Create())
            auto* createCall = dynamic_cast<Call*>(fieldInit);
            if (createCall == nullptr ||
                MethodNameTail(createCall->MethodName) != "Create" ||
                !createCall->Arguments.empty())
                return false;
            builderFieldIsInitialized = true;
        } else {
            // stfld StateMachine.field(ldloca stateMachine, ldvar(param)):
            // the C# `fieldInit.MatchLdLoc(out var v) && v.Kind == Parameter`
            // -- the match-against form would need the variable first, so
            // the ldloc is read directly (the file-local out-form shape).
            ILVariable* v = nullptr;
            if (!MatchLdLocOut(fieldInit, v) || v == nullptr ||
                v->Kind != VariableKind::Parameter) {
                // The struct-`this` capture (ldobj(ldloc this)) is deferred
                // with the fieldToParameterMap arms that consume it (the
                // fixture's methods are not struct methods).
                return false;
            }
            // OK, copies parameter into state machine
            fieldToParameterMap_[field] = v;
        }
    }

    return builderFieldIsInitialized;
}

bool AsyncAwaitDecompiler::ClassifyTaskType(ILFunction& function,
                                             ILInstruction* startCall,
                                             const TypeSystem::IType& builderType) {
    (void)startCall;
    if (function.Method == nullptr)
        return false;
    const TypeSystem::IType& taskType = function.Method->ReturnType();
    if (TypeSystem::IsKnownType(taskType, TypeSystem::KnownTypeCode::Void)) {
        methodType_ = AsyncMethodType::Void;
        taskType_ = std::const_pointer_cast<TypeSystem::IType>(
            std::shared_ptr<const TypeSystem::IType>(
                std::shared_ptr<const TypeSystem::IType>(), &taskType));
        underlyingReturnType_ = taskType_;
        if (!(builderType.Namespace() == kBuilderNs &&
              NameWithoutArity(builderType.Name()) == "AsyncVoidMethodBuilder"))
            return false;
        return true;
    }
    TypeSystem::FullTypeName builderTypeNameFromTask;
    if (TypeSystem::IsNonGenericTaskType(taskType, builderTypeNameFromTask)) {
        methodType_ = AsyncMethodType::Task;
        underlyingReturnType_ = std::make_shared<TypeSystem::KnownType>(
            TypeSystem::KnownTypeCode::Void);
        if (!(builderType.Namespace() == kBuilderNs &&
              NameWithoutArity(builderType.Name()) == "AsyncTaskMethodBuilder"))
            return false;
        return true;
    }
    if (TypeSystem::IsGenericTaskType(taskType, builderTypeNameFromTask)) {
        methodType_ = AsyncMethodType::TaskOfT;
        if (TypeSystem::IsKnownType(taskType, TypeSystem::KnownTypeCode::TaskOfT)) {
            underlyingReturnType_ = TypeSystem::UnpackTask(
                context_->TypeSystem->MainModule().Compilation(), taskType);
        } else {
            // A custom generic Task-like: the "T" is the builder type's
            // first type argument (the C#
            // startCall.Method.DeclaringType.TypeArguments[0]).
            std::vector<TypeSystem::ITypePtr> args;
            if (const auto* parameterized =
                    dynamic_cast<const TypeSystem::ParameterizedType*>(
                        &builderType))
                args = parameterized->TypeArguments();
            if (args.empty() || args[0] == nullptr)
                return false;
            underlyingReturnType_ = args[0];
        }
        if (underlyingReturnType_ == nullptr)
            return false;
        if (!(builderType.Namespace() == kBuilderNs &&
              NameWithoutArity(builderType.Name()) == "AsyncTaskMethodBuilder" &&
              builderType.TypeParameterCount() == 1)) {
            return false;
        }
        return true;
    }
    return false;
}

bool AsyncAwaitDecompiler::MatchCall(ILInstruction* inst,
                                      const std::string& name,
                                      std::vector<ILInstruction*>& args) {
    // The C# `inst is CallInstruction call && (call.OpCode == Call ||
    // CallVirt) && call.Method.Name == name && !call.Method.IsStatic` -- the
    // port models Call/CallVirt as one node with the reader-resolved method
    // name and the instance-call bit.
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj)
        return false;
    // The resolved method's bare name: the resolved IMethod when present,
    // else the reader's "Namespace.Type::Method" tail.
    if (call->Method != nullptr) {
        if (call->Method->Name() != name)
            return false;
        // The C# `!call.Method.IsStatic`: the port's resolved methods carry
        // no static bit on this surface; the reader's IsInstanceCall is the
        // signature-derived equivalent.
        if (!call->IsInstanceCall)
            return false;
    } else {
        if (MethodNameTail(call->MethodName) != name)
            return false;
        if (!call->IsInstanceCall)
            return false;
    }
    args.clear();
    args.reserve(call->Arguments.size());
    for (auto& arg : call->Arguments)
        args.push_back(arg.get());
    return !args.empty();
}

bool AsyncAwaitDecompiler::MatchStFld(ILInstruction* stfld,
                                      ILVariable* stateMachineVar,
                                      const TypeSystem::IField*& field,
                                      ILInstruction*& value) {
    ILInstruction* target = nullptr;
    if (!MatchStFldRaw(stfld, target, field, value))
        return false;
    // The C# `field.MemberDefinition as IField`.
    field = field != nullptr
                ? static_cast<const TypeSystem::IField*>(field->MemberDefinition())
                : nullptr;
    return field != nullptr && MatchLdLocRef(target, stateMachineVar);
}

const TypeSystem::ITypeDefinition*
AsyncAwaitDecompiler::ResolveStateMachineType(
    const TypeSystem::IType& localType,
    const Metadata::MetadataFile& metadata, ILTransformContext& context) {
    (void)context;
    // The reader's local types are name-only stand-ins; the state machine is
    // the nested type with the local's name in the current type.
    const std::string name = localType.Name();
    for (const auto& t : metadata.TypeDefs()) {
        auto info = metadata.GetTypeDefNameInfo(t.Token);
        if (!info.has_value() || info->Name != name)
            continue;
        if (info->DeclaringTypeToken == 0)
            continue;
        if (!Metadata::IsCompilerGeneratedStateMachine(metadata, t.Token))
            continue;
        if (context.TypeSystem == nullptr)
            return nullptr;
        const auto* module = dynamic_cast<const TypeSystem::MetadataModule*>(
            &context.TypeSystem->MainModule());
        if (module == nullptr)
            return nullptr;
        return module->GetDefinition(t.Token);
    }
    return nullptr;
}

} // namespace ILSpy::Decompiler::IL
