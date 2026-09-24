// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// CSharpDecompiler.GetILTransforms() + ILFunction.RunTransforms -- the fixed
// per-body transform pipeline the C# engine runs over every decompiled method.
// The C# returns the list of IILTransforms and lets callers iterate
// (RunTransforms is a plain loop over Run with invariant checks); this port
// flattens the list into one function so every consumer drives the same
// sequence (previously the --csharp CLI carried it inline, and the BamlDecompiler's
// ConnectionIdRewritePass -- the C# `function.RunTransforms(
// CSharpDecompiler.GetILTransforms(), context)` site -- needs the same list).
// The port has no CSharpDecompiler class yet, so the runner lives in the IL
// namespace; the CSharpDecompiler-static home is the Phase-7 landing.
//
// The port-local approximations the CLI comments documented are carried as-is:
// a transform the C# pipeline has no port for is simply absent from this list
// (the same set of .Run calls the CLI made inline, in the same order).

#pragma once

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/FixRemainingIncrements.hpp"
#include "Decompiler/IL/Transforms/HighLevelLoopTransform.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/Transforms/SplitVariables.hpp"
#include "Decompiler/IL/Transforms/IntroduceNativeIntTypeOnLocals.hpp"
#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"
#include "Decompiler/IL/Transforms/NamedArgumentTransform.hpp"
#include "Decompiler/IL/Transforms/DeconstructionTransform.hpp"
#include "Decompiler/IL/Transforms/IndexRangeTransform.hpp"
#include "Decompiler/IL/Transforms/TransformArrayInitializers.hpp"
#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"
#include "Decompiler/IL/Transforms/TransformExpressionTrees.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/InterpolatedStringTransform.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/NullCoalescingTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/NullPropagationTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/ReduceNestingTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/UserDefinedLogicTransform.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/DetectExitPoints.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"
#include "Decompiler/IL/ControlFlow/RemoveUnreachableBlocks.hpp"

namespace ILSpy::Decompiler::IL {

// The C# `CSharpDecompiler.GetILTransforms()` list, driven inline in the
// GetILTransforms + RunTransforms order. Every comment documents the C#
// transform it stands in for.
// The C# `public static List<IILTransform> GetILTransforms()` (CSharpDecompiler.cs
// line 89): the fixed per-body transform pipeline as a fresh-instance list --
// the consumers iterate (RunTransforms is a plain loop over Run with the
// invariant checks between entries, the ILFunction.RunTransforms driver).
// The list order mirrors the C# GetILTransforms() list exactly, with the
// port-local approximations the inline driver documented carried per-entry.
// The BlockILTransform grouping (the C# block-scoped runs) is flattened:
// every port transform is a function-level IILTransform entry (the C#
// grouping is a scheduling detail the port's flat loop reproduces in
// order).
// The IILTransform adapter for the port's plain-Run transforms (the
// BlockILTransform-grouped members whose port shape is a plain class with a
// Run member or a static Run -- the C# wraps them in BlockILTransform
// containers; the port's flat loop drives them through this adapter, which
// forwards to the underlying Run exactly as the inline driver did).
template <typename TTransform>
class RunAdapter final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override {
        TTransform::Run(function, context);
    }
};

// The member-Run adapter: the port's plain classes whose Run is a non-static
// member (the C# BlockILTransform-grouped members; the adapter holds the
// instance and forwards).
template <typename TTransform>
class MemberRunAdapter final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override {
        instance_.Run(function, context);
    }
private:
    TTransform instance_;
};

inline std::vector<std::unique_ptr<IILTransform>> GetILTransforms() {
    std::vector<std::unique_ptr<IILTransform>> transforms;
    transforms.push_back(std::make_unique<ControlFlowSimplification>());
    // The C# list head (CSharpDecompiler.cs line ~91-93):
    // ControlFlowSimplification { aggressivelyDuplicateReturnBlocks } ->
    // SplitVariables -> ILInlining; the port's ControlFlowSimplification
    // has no duplication flag, so the SplitVariables entry follows it.
    transforms.push_back(std::make_unique<SplitVariables>());
    transforms.push_back(std::make_unique<StObjToStLoc>());
    transforms.push_back(std::make_unique<ILInlining>());
    transforms.push_back(std::make_unique<InlineReturnTransform>());
    transforms.push_back(std::make_unique<RemoveInfeasiblePathTransform>());
    transforms.push_back(std::make_unique<DetectPinnedRegions>());
    transforms.push_back(std::make_unique<DetectCatchWhenConditionBlocks>());
    transforms.push_back(std::make_unique<LdLocaDupInitObjTransform>());
    transforms.push_back(std::make_unique<EarlyExpressionTransforms>());
    transforms.push_back(std::make_unique<RemoveDeadVariableInit>());
    transforms.push_back(std::make_unique<ControlFlowSimplification>());
    transforms.push_back(std::make_unique<SwitchDetection>());
    transforms.push_back(std::make_unique<SwitchOnStringTransform>());
    transforms.push_back(std::make_unique<SwitchOnNullableTransform>());
    transforms.push_back(std::make_unique<LoopDetection>());
    transforms.push_back(std::make_unique<PatternMatchingTransform>());
    transforms.push_back(std::make_unique<DetectExitPoints>());
    transforms.push_back(std::make_unique<ConditionDetection>());
    transforms.push_back(std::make_unique<LockTransform>());
    transforms.push_back(std::make_unique<UsingTransform>());
    transforms.push_back(
        std::make_unique<MemberRunAdapter<CachedDelegateInitialization>>());
    transforms.push_back(
        std::make_unique<MemberRunAdapter<CachedReadOnlySpanInitialization>>());
    {
        auto statementTransform = std::make_unique<StatementTransform>();
        // The interleaved per-statement children (the C# GetILTransforms()
        // order: ILInlining first because it does not trigger re-runs, the
        // rerun-mechanics transforms after).
        statementTransform->AddChild(std::make_unique<ILInlining>());
        statementTransform->AddChild(std::make_unique<ExpressionTransforms>());
        statementTransform->AddChild(std::make_unique<TransformAssignment>());
        statementTransform->AddChild(std::make_unique<NullCoalescingTransform>());
        statementTransform->AddChild(
            std::make_unique<NullableLiftingStatementTransform>());
        statementTransform->AddChild(
            std::make_unique<NullPropagationStatementTransform>());
        statementTransform->AddChild(
            std::make_unique<TransformArrayInitializers>());
        statementTransform->AddChild(
            std::make_unique<TransformCollectionAndObjectInitializers>());
        statementTransform->AddChild(
            std::make_unique<TransformExpressionTrees>());
        statementTransform->AddChild(std::make_unique<DeconstructionTransform>());
        statementTransform->AddChild(std::make_unique<IndexRangeTransform>());
        statementTransform->AddChild(std::make_unique<NamedArgumentTransform>());
        statementTransform->AddChild(std::make_unique<UserDefinedLogicTransform>());
        statementTransform->AddChild(
            std::make_unique<InterpolatedStringTransform>());
        transforms.push_back(std::move(statementTransform));
    }
    transforms.push_back(std::make_unique<FixRemainingIncrements>());
    transforms.push_back(std::make_unique<CopyPropagation>());
    // The C# slot between CopyPropagation and LocalFunctionDecompiler
    // (CSharpDecompiler.cs line 168): the transform Run embeds the decoded
    // lambda bodies (the deep-decode rides the context's
    // DelegateBodyResolver hook).
    transforms.push_back(std::make_unique<DelegateConstruction>());
    transforms.push_back(std::make_unique<LocalFunctionDecompiler>());
    transforms.push_back(std::make_unique<TransformDisplayClassUsage>());
    transforms.push_back(
        std::make_unique<RunAdapter<HighLevelLoopTransform>>());
    transforms.push_back(std::make_unique<ReduceNestingTransform>());
    transforms.push_back(std::make_unique<RemoveUnreachableBlocks>());
    transforms.push_back(std::make_unique<RemoveRedundantReturn>());
    // The C# slot between RemoveRedundantReturn and AssignVariableNames
    // (CSharpDecompiler.cs lines 185-186): IntroduceDynamicTypeOnLocals
    // stays deferred with the DynamicInstruction argument-info surface;
    // the native-integer introduction is ported.
    // transforms.push_back(std::make_unique<IntroduceDynamicTypeOnLocals>());
    transforms.push_back(std::make_unique<IntroduceNativeIntTypeOnLocals>());
    // The C# tail ends with AssignVariableNames.
    transforms.push_back(std::make_unique<AssignVariableNames>());
    return transforms;
}

inline void RunGetILTransforms(ILFunction& function, ILTransformContext& context)
{
    // The C# `function.RunTransforms(GetILTransforms(), context)` -- the
    // list + the RunTransforms driver (the CheckInvariant between the
    // entries and the per-transform step groups are the driver's job now,
    // replacing the inline call sequence).
    function.RunTransforms(GetILTransforms(), context);
}

} // namespace ILSpy::Decompiler::IL
