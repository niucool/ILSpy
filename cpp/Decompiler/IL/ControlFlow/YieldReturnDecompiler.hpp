// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/ControlFlow/YieldReturnDecompiler.cs:
// the inversion of the compiler-generated iterator state machine back into
// yield-return code.
//
// SLICE STATE (parts 2-4): this part carries the Run gates, the
// enumerator-creation pattern match, and the four metadata analyses (the
// ctor's state field, get_Current's current field, the GetEnumerator field
// mapping, and Dispose's exception table). The MoveNext conversion
// (ConvertBody and its helpers) and the try-finally reconstruction are the
// next parts; until they land, Run matches and analyzes but does not rewrite
// the body, and the transform is NOT registered in the driver.

#pragma once

#include "Decompiler/IL/ControlFlow/StateRangeAnalysis.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <map>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::IL {

class Block;
class BlockContainer;
class ILVariable;
class StLoc;

class YieldReturnDecompiler : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // The C# `internal static Block SingleBlock(ILInstruction body)`: the
    // body as a single basic block (a lone Block, or a one-block container).
    static Block* SingleBlock(ILInstruction* body);

    // The C# `internal static bool MatchEnumeratorCreationNewObj(...)`: the
    // newobj of the compiler-generated enumerator class (the ctor takes the
    // initial state constant, -2 or 0). The port takes raw tokens (the
    // MethodDef table is 0x06).
    static bool MatchEnumeratorCreationNewObj(
        ILInstruction* inst, const Metadata::MetadataFile& metadata,
        std::uint32_t currentType, std::uint32_t& enumeratorCtor,
        std::uint32_t& enumeratorType);

    // The C# `MatchMonoEnumeratorCreationNewObj`: the mcs shape -- a no-arg
    // ctor newobj.
    static bool MatchMonoEnumeratorCreationNewObj(
        ILInstruction* inst, const Metadata::MetadataFile& metadata,
        std::uint32_t currentType, std::uint32_t& enumeratorCtor,
        std::uint32_t& enumeratorType);

    // The C# `internal static void ResolveIEnumerableIEnumeratorFieldMapping`:
    // the GetEnumerator body's `this.loadField = storeField` copies extend the
    // field-to-parameter map.
    static void ResolveIEnumerableIEnumeratorFieldMapping(
        const Metadata::MetadataFile& metadata, ILTransformContext& context,
        std::map<const TypeSystem::IField*, ILVariable*>& fieldToParameterMap,
        std::uint32_t getEnumeratorMethod);

    // The C# `internal static void TranslateFieldsToLocalAccess(ILFunction
    // function, ILInstruction inst, Dictionary<IField, ILVariable>
    // fieldToVariableMap, bool isCompiledWithMono = false)`: rewrites the
    // this-field accesses to the hoisted local variables.
    static void TranslateFieldsToLocalAccess(
        ILFunction& function, ILInstruction* inst,
        std::map<const TypeSystem::IField*, ILVariable*>& fieldToVariableMap,
        bool isCompiledWithMono = false);

private:
    // The C# member analyses.
    bool MatchEnumeratorCreationPattern(ILFunction& function,
                                        ILTransformContext& context);
    void AnalyzeCtor(ILTransformContext& context);
    void AnalyzeCurrentProperty(ILTransformContext& context);
    void ResolveIEnumerableIEnumeratorFieldMapping(ILTransformContext& context);
    void ConstructExceptionTable(ILTransformContext& context);
    // The C# `BlockContainer AnalyzeMoveNext(ILFunction function)`: the
    // MoveNext decode + the copy propagation + the state-field fallback +
    // the Mono skip-finally discovery + the field-copy propagation + the
    // range analysis + the body conversion. Returns the converted body
    // (null on failure -- the caller leaves the state machine as-is).
    std::unique_ptr<BlockContainer> AnalyzeMoveNext(
        ILFunction& function, ILTransformContext& context);
    // The C# `void PropagateCopiesOfFields(BlockContainer body)`: undo the
    // Roslyn optimization that copies the immutable fields into locals.
    void PropagateCopiesOfFields(BlockContainer& body);
    // The C# `BlockContainer ConvertBody(BlockContainer oldBody,
    // StateRangeAnalysis rangeAnalysis)`.
    std::unique_ptr<BlockContainer> ConvertBody(
        BlockContainer& oldBody, ControlFlow::StateRangeAnalysis& rangeAnalysis);

    // The transform state (the C# fields; the raw tokens replace the SRM
    // handles, the port's raw-token convention).
    ILTransformContext* context_ = nullptr;
    const Metadata::MetadataFile* metadata_ = nullptr;
    std::uint32_t currentType_ = 0;
    std::uint32_t enumeratorType_ = 0;
    std::uint32_t enumeratorCtor_ = 0;
    bool isCompiledWithMono_ = false;
    bool isCompiledWithVisualBasic_ = false;
    bool isCompiledWithLegacyVisualBasic_ = false;
    std::uint32_t disposeMethod_ = 0;
    const TypeSystem::IField* stateField_ = nullptr;
    const TypeSystem::IField* currentField_ = nullptr;
    const TypeSystem::IField* disposingField_ = nullptr;
    std::map<const TypeSystem::IField*, ILVariable*> fieldToParameterMap_;
    std::map<const TypeSystem::IMethod*, Util::LongSet> finallyMethodToStateRange_;
    bool hasFinallyMethodToStateRange_ = false;
    // The C# temporary stores for 'yield break' (the state-variable stores
    // whose loads become the new-body leaves).
    std::vector<StLoc*> returnStores_;
    // The Mono/VB local flags + the cached state vars (the C# fields).
    ILVariable* skipFinallyBodies_ = nullptr;
    ILVariable* doFinallyBodies_ = nullptr;
    std::vector<ILVariable*> cachedStateVars_;
};

} // namespace ILSpy::Decompiler::IL
