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
#include "Decompiler/IL/Transforms/DynamicCallSiteTransform.hpp"
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
#include "Decompiler/IL/Instructions/Await.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/InvalidInstructions.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"  // IsCompilerGeneratedStateMachine
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
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

// The block's last instruction in the C# list sense: the final if the
// slot is set, else the last list entry (the C# `block.Instructions.Last()`).
ILInstruction* LastInstruction(Block* block) {
    if (block->FinalInstruction != nullptr)
        return block->FinalInstruction.get();
    if (!block->Instructions.empty())
        return block->Instructions.back().get();
    return nullptr;
}

// The C# `ILInlining.FindFirstInlinedCall` (ILInlining.cs lines 915-928):
// walks the children while the slot accepts inlining, returning the first
// call found. The port has no per-child SlotInfo; the per-class
// CanInlineIntoSlot overrides carry the same gates (the default is
// permissive, the port's inliner convention).
Call* FindFirstInlinedCall(ILInstruction* inst) {
    for (int i = 0; i < inst->ChildCount(); i++) {
        ILInstruction* child = inst->GetChild(i);
        if (child == nullptr) continue;
        if (!inst->CanInlineIntoSlot(i, child)) break;
        if (Call* call = FindFirstInlinedCall(child))
            return call;
    }
    return dynamic_cast<Call*>(inst);
}

// The C# resolves every call's `Method` through the module's type system
// at ILAst-construction time; this port's deferred-resolution convention
// leaves out-of-module methods (a MemberRef into a referenced assembly)
// null. The Await nodes carry their methods for the result type and the AST
// emission, so a null surface synthesizes a FakeMethod from the reader's
// already-resolved call surfaces (the CreateDynamicAwaiterMethod precedent).
std::shared_ptr<TypeSystem::IMethod> CallMethodSurface(
    const Call* call, ILTransformContext* context) {
    if (call->Method != nullptr)
        return call->Method;
    if (context == nullptr || context->TypeSystem == nullptr)
        return nullptr;
    auto method = std::make_shared<TypeSystem::Implementation::FakeMethod>(
        *context->TypeSystem, TypeSystem::SymbolKind::Method);
    method->SetName(MethodNameTail(call->MethodName));
    if (call->ReturnIType != nullptr)
        method->SetReturnType(call->ReturnIType);
    if (call->DeclaringType != nullptr)
        method->SetDeclaringType(call->DeclaringType);
    return method;
}

// The C# `static ILInstruction UnwrapConvUnknown(ILInstruction inst)`
// (lines 1932-1940): a conv to the unknown target type unwraps to its
// argument.
ILInstruction* UnwrapConvUnknown(ILInstruction* inst) {
    if (auto* conv = dynamic_cast<Conv*>(inst)) {
        if (conv->TargetType == PrimitiveType::Unknown)
            return conv->Argument.get();
    }
    return inst;
}

// Port of ILInstruction.MatchLogicNot(out arg) (the
// ExpressionTransforms/NullableLifting precedent): logic.not(X) is this
// port's `comp(eq, X, ldc.i4 0)`.
bool MatchLogicNotLocal(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (inst == nullptr || inst->Op != OpCode::Comp) return false;
    auto* comp = static_cast<Comp*>(inst);
    if (comp->Kind != ComparisonKind::Equality || comp->Unsigned) return false;
    if (!comp->Right || comp->Right->Op != OpCode::LdcI4) return false;
    if (static_cast<LdcI4*>(comp->Right.get())->Value != 0) return false;
    arg = comp->Left.get();
    return true;
}

// The C# `MatchLdLoc(variable, out value)` match-against-variable store
// form (used as `stloc awaiterVar(ldfld ...)`): the C# checks the stored
// variable's identity and reports the value.
bool MatchStLocOf(ILInstruction* inst, const ILVariable* variable,
                  ILInstruction*& value) {
    value = nullptr;
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr || stloc->Variable.get() != variable)
        return false;
    value = stloc->Value.get();
    return true;
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

// The C# `static ILInstruction StackSlotValue(ILInstruction inst)` (lines
// 1714-1727): a single-definition stack-slot load resolves to the value its
// one store holds; anything else is returned unchanged.
ILInstruction* StackSlotValueLocal(ILInstruction* inst) {
    ILVariable* v = nullptr;
    if (MatchLdLocOut(inst, v) && v != nullptr &&
        v->Kind == VariableKind::StackSlot && v->IsSingleDefinition() &&
        v->StoreInstructions.size() == 1) {
        if (auto* stloc = dynamic_cast<StLoc*>(v->StoreInstructions[0]))
            return stloc->Value.get();
    }
    return inst;
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

    bool matched = MatchTaskCreationPattern(function);
    if (!matched) {
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

    AnalyzeStateMachine(function);
    DetectAwaitPattern(function);
    CleanDoFinallyBodies(function);

    context_->StepOnce("Translate fields to local accesses");
    YieldReturnDecompiler::TranslateFieldsToLocalAccess(
        function, function.Body.get(), fieldToParameterMap_);
    TranslateCachedFieldsToLocals();

    FinalizeInlineMoveNext(function);
    // The C# sets the container's expected result type from the underlying
    // return (the enumerator shapes are deferred with their arms).
    if (auto* container =
            dynamic_cast<BlockContainer*>(function.Body.get())) {
        container->ExpectedResultType =
            underlyingReturnType_ != nullptr
                ? StackTypeOf(underlyingReturnType_.get())
                : StackType::Unknown;
    }
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
    // The released try block still carries the try-catch as its parent (the
    // moveNext function's tree owns that); the function owns the body now,
    // and the parent chain the walks consult (IsDescendantOf, the CFG's
    // function-exit test) must end at the function -- the reader wires a
    // root body's parent to the function the same way.
    function.Body->Parent = &function;
    function.AsyncReturnType = underlyingReturnType_;
    function.IsIterator = false;  // the enumerator shapes are deferred
    // The C# clears moveNextFunction.Variables and takes the body out; the
    // port's unique_ptr ownership transfers the try block to the function
    // and leaves the moveNext function holding the catch handler.
    // Register the variables the inlined body uses. The registration takes
    // OWNING copies (the nodes' shared_ptr members): the state machine
    // function -- the variables' other owner -- dies with the temporary
    // decompiler when Run returns, and the body's loads/stores can be
    // deleted by the later inlining passes; the function's own variable
    // list must keep the transferred variables alive on its own.
    {
        std::vector<ILInstruction*> regStack{function.Body.get()};
        while (!regStack.empty()) {
            ILInstruction* node = regStack.back();
            regStack.pop_back();
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
                    regStack.push_back(child);
            }
        }
    }
    std::vector<ILInstruction*> stack{function.Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* branch = dynamic_cast<Branch*>(node)) {
            if (branch->TargetBlock == setResultReturnBlock_) {
                // The result variable's owning handle lives in the
                // function's registered variables (the registration below
                // took owning copies of the body's loads/stores); the LdLoc
                // takes that handle so the function keeps the variable
                // alive past the state machine function's death.
                ILVariablePtr resultHandle;
                if (resultVar_ != nullptr) {
                    for (const ILVariablePtr& v : function.Variables) {
                        if (v.get() == resultVar_) {
                            resultHandle = v;
                            break;
                        }
                    }
                }
                std::unique_ptr<ILInstruction> value =
                    resultHandle != nullptr
                        ? std::unique_ptr<ILInstruction>(
                              new LdLoc(std::move(resultHandle)))
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
    // The C# retargets the leaves that came over from MoveNext (they still
    // target the moveNext function's root container) to the new body and
    // records them as the await candidates; the setResultYieldBlock
    // (the enumerator shape) is deferred with the enumerator arms.
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* leave = dynamic_cast<Leave*>(node)) {
                if (leave->TargetContainer ==
                    dynamic_cast<BlockContainer*>(
                        moveNextFunction_->Body.get())) {
                    leave->TargetContainer =
                        dynamic_cast<BlockContainer*>(function.Body.get());
                    moveNextLeaves_.insert(leave);
                }
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

// ---------------------------------------------------------------------------
// AnalyzeStateMachine (the C# lines 1306-1398) + DetectAwaitPattern (the C#
// lines 1789-1930) + the tail cleanups.
// ---------------------------------------------------------------------------

void AsyncAwaitDecompiler::AnalyzeStateMachine(ILFunction& function) {
    context_->StepOnce("AnalyzeStateMachine()");
    smallestAwaiterVarIndex_ = std::numeric_limits<int>::max();
    // function.Descendants.OfType<BlockContainer>() -- pre-order.
    std::vector<BlockContainer*> containers;
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* container = dynamic_cast<BlockContainer*>(node))
                containers.push_back(container);
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    for (auto* container : containers) {
        // The NormalizeAwaitOnCompletedDualBranch fold (the runtime
        // ICriticalNotifyCompletion type check Roslyn emits for dynamic /
        // some generic awaiters) is deferred with the dynamic-awaiter arms;
        // the dynamic callsite conversion runs per block here (the C#
        // DynamicCallSiteTransform.RunOnBasicBlock).
        // Use a separate state range analysis per container.
        ControlFlow::StateRangeAnalysis sra(
            ControlFlow::StateRangeAnalysisMode::AsyncMoveNext, stateField_,
            cachedStateVar_);
        sra.doFinallyBodies = doFinallyBodies_;
        sra.AssignStateRanges(container, Util::LongSet::Universe());
        auto stateToBlockMap = sra.GetBlockStateSetMapping(*container);

        for (auto& blockPtr : container->Blocks) {
            Block* block = blockPtr.get();
            DynamicCallSiteTransform::RunOnBasicBlock(block, *context_);
            // This is likely an 'await' block.
            if (auto* leave = dynamic_cast<Leave*>(LastInstruction(block))) {
                if (moveNextLeaves_.count(leave) != 0) {
                    ILVariable* awaiter = nullptr;
                    const TypeSystem::IField* awaiterField = nullptr;
                    int state = 0;
                    int yieldOffset = -1;
                    bool matched = AnalyzeAwaitBlock(
                        block, awaiter, awaiterField, state, yieldOffset);
                    if (matched) {
                        block->Instructions.push_back(
                            std::make_unique<Await>(std::make_unique<LdLoca>(
                                ILVariablePtr(ILVariablePtr(), awaiter))));
                        Block* targetBlock = stateToBlockMap.GetOrDefault(state);
                        if (targetBlock != nullptr) {
                            // The C# records the await debug info
                            // (yieldOffset -> the resume block's offset);
                            // the port's ILFunction carries no
                            // AsyncDebugInfo surface (deferred with it).
                            block->SetFinal(
                                std::make_unique<Branch>(targetBlock));
                        } else {
                            auto invalid =
                                std::make_unique<InvalidBranch>();
                            invalid->Message =
                                "Could not find block for state " +
                                std::to_string(state);
                            block->SetFinal(std::move(invalid));
                        }
                        awaitBlocks_[block] = {awaiter, awaiterField};
                        if (awaiter->Index < smallestAwaiterVarIndex_)
                            smallestAwaiterVarIndex_ = awaiter->Index;
                    }
                }
            }
            // The `else if` branch-to-setResultYieldBlock arm (the async
            // enumerator 'yield return') and the
            // TransformYieldBreak / SimplifyIfDisposeMode arms are deferred
            // with the enumerator slice.
        }
        // Skip the state dispatcher and directly jump to the initial state.
        Block* entryPoint = stateToBlockMap.GetOrDefault(initialState_);
        if (entryPoint != nullptr) {
            auto newBlock = std::make_unique<Block>();
            newBlock->SetFinal(std::make_unique<Branch>(entryPoint));
            container->Blocks.insert(
                container->Blocks.begin(), std::move(newBlock));
            // The insert shifted the blocks; re-parent and re-index (the
            // ChildIndex slots drive the container walks).
            for (std::size_t i = 0; i < container->Blocks.size(); i++) {
                container->Blocks[i]->Parent = container;
                container->Blocks[i]->ChildIndex = i;
            }
        }
        container->SortBlocks(true);
        // CoalesceDynamicAwaiterBlocks (the dynamic callsite re-join) is
        // deferred with the dynamic arms.
    }
}

bool AsyncAwaitDecompiler::AnalyzeAwaitBlock(
    Block* block, ILVariable*& awaiter, const TypeSystem::IField*& awaiterField,
    int& state, int& yieldOffset) {
    awaiter = nullptr;
    awaiterField = nullptr;
    state = 0;
    yieldOffset = -1;
    const int count = static_cast<int>(block->Instructions.size()) +
                      (block->FinalInstruction != nullptr ? 1 : 0);
    int pos = count - 2;
    if (pos >= 0 && doFinallyBodies_ != nullptr &&
        dynamic_cast<StLoc*>(BlockInstructionAt(block, pos)) != nullptr) {
        auto* storeDoFinallyBodies =
            static_cast<StLoc*>(BlockInstructionAt(block, pos));
        if (!(storeDoFinallyBodies->Variable != nullptr &&
              storeDoFinallyBodies->Variable->Kind == VariableKind::Local &&
              storeDoFinallyBodies->Variable->Type != nullptr &&
              TypeSystem::IsKnownType(
                  *storeDoFinallyBodies->Variable->Type,
                  TypeSystem::KnownTypeCode::Boolean) &&
              storeDoFinallyBodies->Variable->Index ==
                  doFinallyBodies_->Index)) {
            return false;
        }
        int stored = 0;
        if (!MatchLdcI4Out(storeDoFinallyBodies->Value.get(), stored) ||
            stored != 0)
            return false;
        pos--;
    }

    std::vector<ILInstruction*> callArgs;
    if (pos >= 0 && MatchCall(BlockInstructionAt(block, pos),
                              "AwaitUnsafeOnCompleted", callArgs)) {
        // call AwaitUnsafeOnCompleted(ldflda <>t__builder(ldloc this),
        // ldloca awaiter, ldloc this)
    } else if (pos >= 0 && MatchCall(BlockInstructionAt(block, pos),
                                     "AwaitOnCompleted", callArgs)) {
        // call AwaitOnCompleted(ldflda <>t__builder(ldloc this), ldloca
        // awaiter, ldloc this): the non-unsafe call when the awaiter does
        // not implement ICriticalNotifyCompletion.
    } else {
        return false;
    }
    if (callArgs.size() != 3)
        return false;
    if (!IsBuilderFieldOnThis(callArgs[0]))
        return false;
    if (!MatchLdLocaOut(callArgs[1], awaiter))
        return false;
    if (MatchLdThis(callArgs[2])) {
        // OK (if state machine is a struct)
        pos--;
    } else {
        ILVariable* tempVar = nullptr;
        if (!MatchLdLocaOut(callArgs[2], tempVar))
            return false;
        // Roslyn, non-optimized uses a class for the state machine:
        // stloc tempVar(ldloc this)
        // call AwaitUnsafeOnCompleted(..., ldloca awaiter, ldloca tempVar)
        ILInstruction* tempVal = nullptr;
        if (!(pos > 0 && MatchStLocOf(BlockInstructionAt(block, pos - 1),
                                      tempVar, tempVal)))
            return false;
        if (!MatchLdThis(tempVal))
            return false;
        pos -= 2;
    }
    // stfld StateMachine.<>awaiter(ldloc this, ldloc awaiter)
    ILInstruction* target = nullptr;
    ILInstruction* value = nullptr;
    if (!MatchStFldRaw(BlockInstructionAt(block, pos), target, awaiterField,
                       value))
        return false;
    if (!MatchLdThis(target))
        return false;
    {
        ILVariable* loaded = nullptr;
        if (!MatchLdLocOut(value, loaded) || loaded != awaiter)
            return false;
    }
    pos--;
    // Store IL offset for debug info:
    if (pos >= 0 && BlockInstructionAt(block, pos) != nullptr)
        yieldOffset = BlockInstructionAt(block, pos)->EndILOffset;

    // stloc S_10(ldloc this)
    // stloc S_11(ldc.i4 0)
    // stloc cachedStateVar(ldloc S_11)
    // stfld <>1__state(ldloc S_10, ldloc S_11)
    const TypeSystem::IField* stateStoreField = nullptr;
    if (!MatchStFldRaw(BlockInstructionAt(block, pos), target,
                       stateStoreField, value))
        return false;
    if (!MatchLdThis(StackSlotValueLocal(target)))
        return false;
    const TypeSystem::IMember* fieldDefinition =
        stateStoreField != nullptr ? stateStoreField->MemberDefinition()
                                   : nullptr;
    if (fieldDefinition != stateField_)
        return false;
    if (!MatchLdcI4Out(StackSlotValueLocal(value), state))
        return false;
    if (pos > 0) {
        if (auto* stloc = dynamic_cast<StLoc*>(
                BlockInstructionAt(block, pos - 1))) {
            if (stloc->Variable != nullptr &&
                stloc->Variable->Kind == VariableKind::Local &&
                cachedStateVar_ != nullptr &&
                stloc->Variable->Index == cachedStateVar_->Index) {
                int cachedValue = 0;
                if (MatchLdcI4Out(StackSlotValueLocal(stloc->Value.get()),
                                  cachedValue) &&
                    cachedValue == state) {
                    // also delete the assignment to cachedStateVar
                    pos--;
                }
            }
        }
    }
    // Delete the matched tail (the C# RemoveRange over the list -- the
    // port's terminator slot included, so the leave goes too).
    if (pos < 0)
        pos = 0;
    if (pos <= static_cast<int>(block->Instructions.size()))
        block->Instructions.erase(
            block->Instructions.begin() + static_cast<std::size_t>(pos),
            block->Instructions.end());
    block->FinalInstruction.reset();
    // delete preceding dead stores:
    while (pos > 0) {
        auto* stloc2 =
            dynamic_cast<StLoc*>(BlockInstructionAt(block, pos - 1));
        if (stloc2 == nullptr || stloc2->Variable == nullptr) break;
        if (!stloc2->Variable->IsSingleDefinition()) break;
        if (stloc2->Variable->LoadCount != 0) break;
        if (stloc2->Variable->Kind != VariableKind::StackSlot) break;
        if (!IsPure(stloc2->Value != nullptr ? stloc2->Value->Flags()
                                             : InstructionFlags::None))
            break;
        pos--;
    }
    if (pos > 0 && pos <= static_cast<int>(block->Instructions.size())) {
        block->Instructions.erase(
            block->Instructions.begin() + static_cast<std::size_t>(pos),
            block->Instructions.end());
    }
    return true;
}


void AsyncAwaitDecompiler::DetectAwaitPattern(ILFunction& function) {
    context_->StepOnce("DetectAwaitPattern");
    std::vector<BlockContainer*> containers;
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* container = dynamic_cast<BlockContainer*>(node))
                containers.push_back(container);
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    for (auto* container : containers) {
        for (auto& block : container->Blocks)
            DetectAwaitPattern(block.get());
        container->SortBlocks(true);
    }
}

void AsyncAwaitDecompiler::DetectAwaitPattern(Block* block) {
    // block:
    //   stloc awaiterVar(callvirt GetAwaiter(...))
    //   if (call get_IsCompleted(ldloca awaiterVar)) br completedBlock
    //   br awaitBlock
    // awaitBlock:
    //   ..
    //   br resumeBlock
    // resumeBlock:
    //   ..
    //   br completedBlock
    // The C# shape carries the if and the branch-to-awaitBlock as two list
    // entries; this port's reader lowers the brtrue to an if in the
    // FinalInstruction slot with the fall-through to the await block left
    // implicit (the block model convention), so the await block is the next
    // block in the container.
    const int count = static_cast<int>(block->Instructions.size()) +
                      (block->FinalInstruction != nullptr ? 1 : 0);
    if (count < 2)
        return;
    // stloc awaiterVar(callvirt GetAwaiter(...))
    auto* stLocAwaiter =
        dynamic_cast<StLoc*>(block->Instructions.back().get());
    if (stLocAwaiter == nullptr)
        return;
    ILVariable* awaiterVar = stLocAwaiter->Variable.get();
    ILInstruction* awaitedValue = nullptr;
    std::shared_ptr<TypeSystem::IMethod> getAwaiterMethod;
    // The dynamic-await arm (DynamicInvokeMemberInstruction /
    // DynamicGetMemberInstruction) is deferred with the dynamic-callsite
    // arms.
    {
        auto* getAwaiterCall =
            dynamic_cast<Call*>(stLocAwaiter->Value.get());
        if (getAwaiterCall == nullptr || getAwaiterCall->IsNewObj)
            return;
        if (getAwaiterCall->Method != nullptr) {
            if (getAwaiterCall->Method->Name() != "GetAwaiter")
                return;
        } else if (MethodNameTail(getAwaiterCall->MethodName) !=
                   "GetAwaiter") {
            return;
        }
        if (getAwaiterCall->Arguments.size() != 1)
            return;
        // The C# `(!call.Method.IsStatic || call.Method.IsExtensionMethod)`:
        // the port's resolved surface has no static bit; the reader's
        // instance-call bit is the equivalent (a Task.GetAwaiter is a
        // callvirt).
        if (!getAwaiterCall->IsInstanceCall)
            return;
        awaitedValue = getAwaiterCall->Arguments[0].get();
        getAwaiterMethod = CallMethodSurface(getAwaiterCall, context_);
    }
    // if (call get_IsCompleted(ldloca awaiterVar)) br completedBlock
    auto* ifInst = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (ifInst == nullptr)
        return;
    ILInstruction* condition = ifInst->Condition.get();
    auto* trueBranch = dynamic_cast<Branch*>(ifInst->TrueInst.get());
    if (trueBranch == nullptr)
        return;
    Block* completedBlock = trueBranch->TargetBlock;
    // br awaitBlock -- the port's implicit fall-through to the next block.
    auto* parent = dynamic_cast<BlockContainer*>(block->Parent);
    if (parent == nullptr || block->ChildIndex < 0 ||
        static_cast<std::size_t>(block->ChildIndex) + 1 >=
            parent->Blocks.size())
        return;
    Block* awaitBlock =
        parent->Blocks[static_cast<std::size_t>(block->ChildIndex) + 1]
            .get();
    // condition might be inverted, swap branches:
    ILInstruction* negatedCondition = nullptr;
    if (MatchLogicNotLocal(condition, negatedCondition)) {
        condition = negatedCondition;
        std::swap(completedBlock, awaitBlock);
    }
    // continue matching call get_IsCompleted(ldloca awaiterVar)
    std::vector<ILInstruction*> isCompletedArgs;
    if (!MatchCall(condition, "get_IsCompleted", isCompletedArgs) ||
        isCompletedArgs.size() != 1) {
        return;
    }
    {
        ILVariable* loaded = nullptr;
        if (!MatchLdLocRef(UnwrapConvUnknown(isCompletedArgs[0]), loaded) ||
            loaded != awaiterVar)
            return;
    }
    // Check awaitBlock and resumeBlock:
    auto awaitIt = awaitBlocks_.find(awaitBlock);
    if (awaitIt == awaitBlocks_.end())
        return;
    ILVariable* awaitBlockVar = awaitIt->second.first;
    if (awaitBlockVar != awaiterVar)
        return;
    Block* resumeBlock = nullptr;
    const TypeSystem::IField* stackField = nullptr;
    if (!CheckAwaitBlock(awaitBlock, resumeBlock, stackField))
    if (!CheckResumeBlock(resumeBlock, awaiterVar,
                          awaitIt->second.second, completedBlock,
                          stackField))
        return;
    // Check completedBlock. The first instruction involves the GetResult
    // call, but it might have been inlined into another instruction.
    if (completedBlock->Instructions.empty())
        return;
    Call* getResultCall =
        FindFirstInlinedCall(completedBlock->Instructions[0].get());
    if (getResultCall == nullptr)
        return;
    std::vector<ILInstruction*> getResultArgs;
    if (!MatchCall(getResultCall, "GetResult", getResultArgs) ||
        getResultArgs.size() != 1)
        return;
    {
        ILVariable* loaded = nullptr;
        if (!MatchLdLocRef(UnwrapConvUnknown(getResultArgs[0]), loaded) ||
            loaded != awaiterVar)
            return;
    }
    // All checks successful, let's transform.
    // The awaited value and the methods are captured before the stores are
    // removed (the GetAwaiter call is destroyed with its stloc).
    std::unique_ptr<ILInstruction> awaitedValueOwned;
    std::shared_ptr<TypeSystem::IMethod> getResultMethod =
        CallMethodSurface(getResultCall, context_);
    if (awaitedValue != nullptr && awaitedValue->Parent != nullptr)
        awaitedValueOwned = awaitedValue->Parent->TakeChild(
            awaitedValue->ChildIndex);
    // remove getAwaiter call
    block->Instructions.pop_back();
    // remove if (isCompleted); instead, directly jump to completed block
    block->SetFinal(std::make_unique<Branch>(completedBlock));
    auto awaitInst = std::make_unique<Await>(
        std::unique_ptr<ILInstruction>(UnwrapConvUnknown(
            awaitedValueOwned.release())));
    awaitInst->GetResultMethod = getResultMethod;
    awaitInst->GetAwaiterMethod = getAwaiterMethod;
    getResultCall->ReplaceWith(std::move(awaitInst));

    // Remove useless reset of awaiterVar.
    if (completedBlock->Instructions.size() > 1) {
        if (auto* stobj = dynamic_cast<StObj*>(
                completedBlock->Instructions[1].get())) {
            ILVariable* resetVar = nullptr;
            if (MatchLdLocaOut(stobj->Target.get(), resetVar) &&
                resetVar == awaiterVar &&
                stobj->Value != nullptr &&
                stobj->Value->Op == OpCode::DefaultValue) {
                completedBlock->Instructions.erase(
                    completedBlock->Instructions.begin() + 1);
            }
        }
    }
}

bool AsyncAwaitDecompiler::CheckAwaitBlock(Block* block,
                                           Block*& resumeBlock,
                                           const TypeSystem::IField*& stackField) {
    // awaitBlock:
    //   (pre-roslyn: save stack)
    //   await(ldloca V_2)
    //   br resumeBlock
    resumeBlock = nullptr;
    stackField = nullptr;
    const int count = static_cast<int>(block->Instructions.size()) +
                      (block->FinalInstruction != nullptr ? 1 : 0);
    if (count < 2)
        return false;
    int pos = 0;
    if (dynamic_cast<StLoc*>(BlockInstructionAt(block, pos)) != nullptr) {
        auto* stloc = static_cast<StLoc*>(BlockInstructionAt(block, pos));
        if (stloc->Variable != nullptr &&
            stloc->Variable->IsSingleDefinition()) {
            ILInstruction* target = nullptr;
            ILInstruction* value = nullptr;
            if (!MatchStFldRaw(BlockInstructionAt(block, pos + 1), target,
                               stackField, value))
                return false;
            if (!MatchLdThis(target))
                return false;
            pos += 2;
        }
    }
    // await(ldloca awaiterVar)
    if (BlockInstructionAt(block, pos) == nullptr ||
        BlockInstructionAt(block, pos)->Op != OpCode::Await)
        return false;
    // br resumeBlock
    auto* branch =
        dynamic_cast<Branch*>(BlockInstructionAt(block, pos + 1));
    if (branch == nullptr)
        return false;
    resumeBlock = branch->TargetBlock;
    return true;
}

bool AsyncAwaitDecompiler::CheckResumeBlock(
    Block* block, ILVariable* awaiterVar,
    const TypeSystem::IField* awaiterField, Block* completedBlock,
    const TypeSystem::IField* stackField) {
    int pos = 0;
    // The C# RestoreStack (the pre-roslyn stack save/restore) is reached
    // only with a stackField; null means nothing to restore.
    if (stackField != nullptr) {
        // The pre-roslyn stack restore is deferred with the legacy
        // codegen arms (the Roslyn shapes carry no stack field).
        return false;
    }

    // stloc awaiterVar(ldfld awaiterField(ldloc this))
    {
        ILInstruction* value = nullptr;
        if (!MatchStLocOf(BlockInstructionAt(block, pos), awaiterVar, value))
            return false;
        // If the awaiter is a reference type, it might get stored in a
        // field of type `object` and cast back to the awaiter type in the
        // resume block.
        if (auto* castClass = dynamic_cast<CastClass*>(value)) {
            value = castClass->Argument.get();
        }
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        if (!MatchLdFld(value, target, field))
            return false;
        if (!MatchLdThis(target))
            return false;
        if (field != awaiterField)
            return false;
        pos++;
    }

    // [optional] stfld awaiterField(ldloc this, default.value)
    // (the C# MatchResetAwaiterField)
    {
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* value = nullptr;
        if (MatchStFldRaw(BlockInstructionAt(block, pos), target, field,
                          value) &&
            MatchLdThis(target) && field == awaiterField &&
            value != nullptr &&
            (value->Op == OpCode::DefaultValue || value->Op == OpCode::LdNull)) {
            pos++;
        } else {
            // {stloc V_6(default.value TaskAwaiter)}
            // {stobj TaskAwaiter(ldflda awaiterField, ldloc V_6)}
            ILVariable* variable = nullptr;
            auto* resetTmp = dynamic_cast<StLoc*>(
                BlockInstructionAt(block, pos));
            if (resetTmp != nullptr)
                variable = resetTmp->Variable.get();
            ILInstruction* resetValue =
                resetTmp != nullptr ? resetTmp->Value.get() : nullptr;
            if (resetTmp != nullptr && variable != nullptr &&
                resetValue != nullptr &&
                resetValue->Op == OpCode::DefaultValue &&
                MatchStFldRaw(BlockInstructionAt(block, pos + 1), target,
                              field, value) &&
                field == awaiterField) {
                ILVariable* stored = nullptr;
                if (MatchLdLocOut(value, stored) && stored == variable)
                    pos += 2;
            }
        }
    }

    // stloc S_28(ldc.i4 -1)
    // stloc cachedStateVar(ldloc S_28)
    // stfld <>1__state(ldloc this, ldloc S_28)
    // (the C# MatchStateFieldAssignement; the Visual Basic order is
    // deferred with the VB arms)
    {
        ILVariable* m1Var = nullptr;
        auto* stlocM1 =
            dynamic_cast<StLoc*>(BlockInstructionAt(block, pos));
        if (stlocM1 != nullptr) {
            int stored = 0;
            if (stlocM1->Variable != nullptr &&
                stlocM1->Variable->Kind == VariableKind::StackSlot &&
                MatchLdcI4Out(stlocM1->Value.get(), stored) &&
                stored == initialState_) {
                m1Var = stlocM1->Variable.get();
                pos++;
            }
        }
        if (dynamic_cast<StLoc*>(BlockInstructionAt(block, pos)) !=
            nullptr) {
            auto* stlocCachedState =
                static_cast<StLoc*>(BlockInstructionAt(block, pos));
            if (stlocCachedState->Variable != nullptr &&
                stlocCachedState->Variable->Kind == VariableKind::Local &&
                cachedStateVar_ != nullptr &&
                stlocCachedState->Variable->Index ==
                    cachedStateVar_->Index) {
                ILVariable* cachedSource = nullptr;
                int cachedConst = 0;
                if ((MatchLdLocOut(stlocCachedState->Value.get(),
                                   cachedSource) &&
                     cachedSource == m1Var) ||
                    (MatchLdcI4Out(stlocCachedState->Value.get(),
                                   cachedConst) &&
                     cachedConst == initialState_)) {
                    pos++;
                }
            }
        }
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* value = nullptr;
        if (!MatchStFldRaw(BlockInstructionAt(block, pos), target, field,
                           value))
            return false;
        if (!MatchLdThis(target))
            return false;
        const TypeSystem::IMember* fieldDefinition =
            field != nullptr ? field->MemberDefinition() : nullptr;
        if (stateField_ == nullptr ||
            fieldDefinition != stateField_->MemberDefinition())
            return false;
        ILVariable* storedVar = nullptr;
        int storedConst = 0;
        if (!((MatchLdcI4Out(value, storedConst) &&
               storedConst == initialState_) ||
              (MatchLdLocOut(value, storedVar) && storedVar == m1Var)))
            return false;
        pos++;
    }
    auto* branch =
        dynamic_cast<Branch*>(BlockInstructionAt(block, pos));
    return branch != nullptr && branch->TargetBlock == completedBlock;
}

void AsyncAwaitDecompiler::CleanDoFinallyBodies(ILFunction& function) {
    if (doFinallyBodies_ == nullptr) {
        return;  // roslyn-compiled code doesn't use doFinallyBodies
    }
    context_->StepOnce("CleanDoFinallyBodies");
    // The doFinallyBodies elimination (the entry-point init removal, the
    // misdetected-variable rollback, and the try-finally if removal) is
    // deferred with the Visual Basic / legacy-codegen arms; the Roslyn
    // shapes reach this method with a null doFinallyBodies only.
    (void)function;
}

void AsyncAwaitDecompiler::TranslateCachedFieldsToLocals() {
    // The cachedFieldToParameterMap capture (the entry-stloc caching of
    // parameter fields) is deferred with it; the map is always empty, so
    // the parameter-variable rewrites have nothing to do.
}

void AsyncAwaitDecompiler::FinalizeInlineMoveNext(ILFunction& function) {
    context_->StepOnce("FinalizeInlineMoveNext()");
    std::vector<ILInstruction*> stack{function.Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* leave = dynamic_cast<Leave*>(node)) {
            if (moveNextLeaves_.count(leave) != 0) {
                auto invalid = std::make_unique<InvalidBranch>();
                invalid->Message =
                    "leave MoveNext - await not detected correctly";
                leave->ReplaceWith(std::move(invalid));
                // ReplaceWith destroys the leave; it has no children to
                // walk (the port's ownership, the yield-part precedent).
                continue;
            }
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    // Delete dead loads of the state cache variable:
    std::vector<Block*> blocks;
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* b = dynamic_cast<Block*>(node))
                blocks.push_back(b);
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    for (auto* block : blocks) {
        for (int i = static_cast<int>(block->Instructions.size()) - 1;
             i >= 0; i--) {
            auto* stloc = dynamic_cast<StLoc*>(
                block->Instructions[static_cast<std::size_t>(i)].get());
            if (stloc == nullptr || stloc->Variable == nullptr)
                continue;
            if (!stloc->Variable->IsSingleDefinition()) continue;
            if (stloc->Variable->LoadCount != 0) continue;
            if (cachedStateVar_ == nullptr) continue;
            ILVariable* loaded = nullptr;
            if (stloc->Value == nullptr) continue;
            if (!MatchLdLocOut(stloc->Value.get(), loaded) ||
                loaded != cachedStateVar_)
                continue;
            block->Instructions.erase(
                block->Instructions.begin() + static_cast<std::size_t>(i));
        }
    }
}

} // namespace ILSpy::Decompiler::IL
