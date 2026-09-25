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
// SLICE STATE (parts 1-2): the task-creation pattern match + the MoveNext
// analyses (AnalyzeMoveNext / ValidateCatchBlock / AnalyzeDisposeAsync)
// + InlineBodyOfMoveNext + CleanUpBodyOfMoveNext are ported; the state
// machine await analysis (AnalyzeStateMachine / DetectAwaitPattern / the
// final translations) is not ported yet -- Run stops after the body
// inlining. The async-enumerator arms (the
// MatchAsyncEnumeratorCreationPattern family) are deferred with the
// enumerator slice.

#pragma once

#include "Decompiler/IL/ControlFlow/StateRangeAnalysis.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <map>
#include <memory>
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
};

} // namespace ILSpy::Decompiler::IL
