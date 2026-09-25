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
// SLICE STATE (part 1): the task-creation pattern match + the Run skeleton
// are ported; the state machine analysis (AnalyzeMoveNext /
// ValidateCatchBlock / InlineBodyOfMoveNext / AnalyzeStateMachine /
// DetectAwaitPattern and the cleanups) is not ported yet -- Run bails after
// the pattern match. The async-enumerator arms (the
// MatchAsyncEnumeratorCreationPattern family) are deferred with the
// enumerator slice.

#pragma once

#include "Decompiler/IL/ControlFlow/StateRangeAnalysis.hpp"
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
};

} // namespace ILSpy::Decompiler::IL
