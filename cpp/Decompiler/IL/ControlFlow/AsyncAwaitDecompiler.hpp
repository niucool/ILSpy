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
// The C# `public class AsyncAwaitDecompiler : IILTransform`
// (IL/ControlFlow/AsyncAwaitDecompiler.cs): the decompiler step for C# 5
// async/await -- matches the compiler-generated state machine, inlines its
// MoveNext body, and converts the await points into Await instructions.
//
// SLICE STATE (parts 1-3): the task-creation pattern match, the MoveNext
// analyses + body inlining, and the state machine await analysis
// (AnalyzeStateMachine / DetectAwaitPattern / the tail cleanups) are
// ported -- the await points become Await nodes and Run drives the whole
// tail (TranslateFieldsToLocalAccess / FinalizeInlineMoveNext). The
// dynamic-call arms (NormalizeAwaitOnCompletedDualBranch,
// DynamicCallSiteTransform, CoalesceDynamicAwaiterBlocks, the dynamic
// GetAwaiter/GetResult sites) and the async-enumerator arms (the
// MatchAsyncEnumeratorCreationPattern family, yield return / yield break,
// the dispose mode) are deferred with their slices, as are the
// Visual Basic arms (the doFinallyBodies elimination beyond the null
// early-out, the VB catch order), the pre-roslyn stack save/restore, and
// the AsyncDebugInfo map (no ILFunction surface).

#pragma once

#include "Decompiler/IL/ControlFlow/StateRangeAnalysis.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <map>
#include <memory>
#include <set>
#include <vector>

namespace ILSpy::Decompiler::IL {

// The C# `enum AsyncMethodType` (AsyncAwaitDecompiler.cs lines 84-91).
enum class AsyncMethodType {
    Void,
    Task,
    TaskOfT,
    AsyncEnumerator,
    AsyncEnumerable,
};

class AsyncAwaitDecompiler : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // The C# `bool MatchTaskCreationPattern(ILFunction function)` (lines
    // 289-477): the async method body's shape -- the builder field
    // initialization, the state assignment, the Start call, and the task
    // return. Public (the C# keeps it private; the port exposes the pattern
    // match for the regression tests, the MatchEnumeratorCreationNewObj
    // precedent).
    bool MatchTaskCreationPattern(ILFunction& function);

    // The driver's Run resolves the reader surfaces (the deferred field/
    // method tokens) before it matches; a caller that drives the pattern
    // directly -- outside Run -- needs the same resolution, so this
    // overload stores the context and resolves first.
    bool MatchTaskCreationPattern(ILFunction& function,
                                  ILTransformContext& context);

    // The pattern's extracted state (the C# sets the same fields; the test
    // seam reads them through these accessors).
    AsyncMethodType MethodType() const { return methodType_; }
    const TypeSystem::IField* StateField() const { return stateField_; }
    const TypeSystem::IField* BuilderField() const { return builderField_; }
    int InitialState() const { return initialState_; }

private:
    // The C# `static bool MatchCall(ILInstruction inst, string name, out
    // InstructionCollection<ILInstruction> args)` (lines 553-562): a
    // (potentially virtual) instance method call by name.
    static bool MatchCall(ILInstruction* inst, const std::string& name,
                          std::vector<ILInstruction*>& args);

    // The C# `static bool MatchStFld(ILInstruction stfld, ILVariable
    // stateMachineVar, out IField field, out ILInstruction value)` (lines
    // 564-572): a store to the state machine.
    static bool MatchStFld(ILInstruction* stfld, ILVariable* stateMachineVar,
                           const TypeSystem::IField*& field,
                           ILInstruction*& value);

    // The port's state machine type resolution: the C# reads
    // `stateMachineVar.Type.GetDefinition()`, but the reader's local types
    // are name-only stand-ins (the deferred-resolution convention), so the
    // definition resolves by scanning the metadata for the nested type with
    // the local's name. Returns null when no such type exists.
    static const TypeSystem::ITypeDefinition* ResolveStateMachineType(
        const TypeSystem::IType& localType,
        const Metadata::MetadataFile& metadata, ILTransformContext& context);

    // The task classification arm of MatchTaskCreationPattern (the C#
    // inlines it): classifies the return type and validates the builder
    // type name.
    bool ClassifyTaskType(ILFunction& function, ILInstruction* startCall,
                          const TypeSystem::IType& builderType);

    // The C# `void AnalyzeMoveNext()` (lines 759-880): the MoveNext decode
    // and the analyses of everything outside the big try-catch.
    void AnalyzeMoveNext();
    // The C# `void ValidateCatchBlock()` (lines 1020-1157): the catch
    // handler shape.
    void ValidateCatchBlock();
    // The C# `void AnalyzeDisposeAsync()` (lines 1192-1230): the enumerator
    // dispose-mode field (a no-op for the task shapes).
    void AnalyzeDisposeAsync();
    // The C# `void InlineBodyOfMoveNext(ILFunction function)` (lines
    // 1234-1290): the body swap + the return-block conversions.
    void InlineBodyOfMoveNext(ILFunction& function);
    // The C# `void CleanUpBodyOfMoveNext(ILFunction function)` (lines
    // 250-287): the copy propagation + inlining cleanup after the body
    // swap.
    void CleanUpBodyOfMoveNext(ILFunction& function);
    // The C# `Block CheckSetResultReturnBlock(...)` (lines 886-960).
    Block* CheckSetResultReturnBlock(BlockContainer* blockContainer, int pos,
                                     std::vector<bool>& blocksAnalyzed);
    // The C# `void CheckSetResultAndExit(...)` (lines 979-1015).
    void CheckSetResultAndExit(BlockContainer* blockContainer, Block* block,
                               int& pos);
    // The C# `void MatchHoistedLocalCleanup(Block, ref int)` (lines 995-
    // 1004): the Roslyn 39735 hoisted-local nulling.
    void MatchHoistedLocalCleanup(Block* block, int& pos);
    // The C# `void MatchCompleteCall(Block, ref int)` (lines 1159-1168).
    void MatchCompleteCall(Block* block, int& pos);
    // The C# `bool IsBuilderFieldOnThis(ILInstruction)` (lines 1170-1186).
    bool IsBuilderFieldOnThis(ILInstruction* inst);
    // The C# `bool IsBuilderOrPromiseFieldOnThis(ILInstruction)` (lines
    // 1188-1200): the enumerator promise fields accept any field (deferred
    // with the enumerator arms).
    bool IsBuilderOrPromiseFieldOnThis(ILInstruction* inst);
    // The C# `bool MatchStateAssignment(ILInstruction, out int)` (lines
    // 1202-1215).
    bool MatchStateAssignment(ILInstruction* inst, int& newState);

    // The C# `void AnalyzeStateMachine(ILFunction function)` (lines
    // 1306-1398): per container, a state range analysis maps states to
    // blocks; the blocks ending in a MoveNext leave become await blocks
    // (Await + branch to the resume block), and the container's entry
    // skips the state dispatcher.
    void AnalyzeStateMachine(ILFunction& function);
    // The C# `bool AnalyzeAwaitBlock(Block, out awaiter, out awaiterField,
    // out state, out yieldOffset)` (lines 1645-1712): the await block tail
    // (the awaiter field store, the AwaitUnsafeOnCompleted call, the state
    // assignment) -- matched and removed, leaving the block to carry the
    // Await node.
    bool AnalyzeAwaitBlock(Block* block, ILVariable*& awaiter,
                           const TypeSystem::IField*& awaiterField,
                           int& state, int& yieldOffset);
    // The C# `void DetectAwaitPattern(ILFunction function)` + the block
    // overload (lines 1789-1930): the
    // stloc-awaiter/if-IsCompleted/br-awaitBlock shape collapses into the
    // Await node over the awaited value.
    void DetectAwaitPattern(ILFunction& function);
    void DetectAwaitPattern(Block* block);
    // The C# `bool CheckAwaitBlock(Block, out resumeBlock, out
    // stackField)` (lines 1968-1989).
    bool CheckAwaitBlock(Block* block, Block*& resumeBlock,
                         const TypeSystem::IField*& stackField);
    // The C# `bool CheckResumeBlock(Block, awaiterVar, awaiterField,
    // completedBlock, stackField)` (lines 1991-2112).
    bool CheckResumeBlock(Block* block, ILVariable* awaiterVar,
                          const TypeSystem::IField* awaiterField,
                          Block* completedBlock,
                          const TypeSystem::IField* stackField);
    // The C# `void CleanDoFinallyBodies(ILFunction function)` (lines
    // 2158-2197): the VB-family doFinallyBodies elimination.
    void CleanDoFinallyBodies(ILFunction& function);
    // The C# `void TranslateCachedFieldsToLocals()` (lines 2215-2229):
    // the cached field loads become the parameter locals.
    void TranslateCachedFieldsToLocals();
    // The C# `void FinalizeInlineMoveNext(ILFunction function)` (lines
    // 1272-1297): the undetected await leaves become InvalidBranch + the
    // dead cached-state stores go.
    void FinalizeInlineMoveNext(ILFunction& function);

    ILTransformContext* context_ = nullptr;

    // These fields are set by MatchTaskCreationPattern() (the C# field
    // block, lines 96-113).
    TypeSystem::ITypePtr taskType_;  // return type of the async method
    TypeSystem::ITypePtr underlyingReturnType_;  // the "T" in Task<T>
    AsyncMethodType methodType_ = AsyncMethodType::Void;
    const TypeSystem::ITypeDefinition* stateMachineType_ = nullptr;
    TypeSystem::ITypePtr builderType_;
    const TypeSystem::IField* builderField_ = nullptr;
    const TypeSystem::IField* stateField_ = nullptr;
    int initialState_ = 0;
    std::map<const TypeSystem::IField*, ILVariable*> fieldToParameterMap_;

    // These fields are set by AnalyzeMoveNext() (the C# field block, lines
    // 115-121).
    std::unique_ptr<ILFunction> moveNextFunction_;
    ILVariable* cachedStateVar_ = nullptr;  // caches the stateField in MoveNext
    TryCatch* mainTryCatch_ = nullptr;
    Block* setResultReturnBlock_ = nullptr;  // the return statement block
    int finalState_ = 0;        // final state after the setResultAndExitBlock
    bool finalStateKnown_ = false;
    ILVariable* resultVar_ = nullptr;  // returned by the setResultReturnBlock
    Block* setResultYieldBlock_ = nullptr;  // the 'yield return' block
    ILVariable* doFinallyBodies_ = nullptr;
    std::vector<bool> blocksAnalyzed_;

    // The C# `HashSet<Leave> moveNextLeaves` (line 103) + the await bookkeeping
    // (lines 102-112): the leaves carried over from MoveNext (the await
    // candidates), the blocks recognized as await points, and the smallest
    // awaiter variable index.
    std::set<Leave*> moveNextLeaves_;
    std::map<Block*, std::pair<ILVariable*, const TypeSystem::IField*>> awaitBlocks_;
    int smallestAwaiterVarIndex_ = -1;
};

} // namespace ILSpy::Decompiler::IL
