// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/IILTransform.cs: the per-function
// transform interface and its run context. The C# context carries the type
// system, debug-info provider, full DecompilerSettings and a debug Stepper;
// this port carries the settings the ported transforms consult (growing as
// more of Phase 4 lands) plus a simple step-trace hook.

#pragma once

#include <functional>

namespace ILSpy::Decompiler::IL {

class ILFunction;
class ILInstruction;

// Settings the IL transforms consult (a subset of DecompilerSettings for now).
struct ILTransformSettings {
    bool RemoveDeadStores = false;  // DecompilerSettings.RemoveDeadStores (default false)
    // Sort switch sections by their label value instead of by IL offset. The C#
    // default is false (sort by branch-target IL offset, preserving the original
    // case order); true is a diffing aid for obfuscated assemblies. Used by
    // SwitchDetection.SortSwitchSections.
    bool SortSwitchSections = false;
    // Whether to detect switches compiled to if-chains (non-contiguous case
    // labels) and reconstruct them as SwitchInstructions. DecompilerSettings.
    // SparseIntegerSwitch -- a C# 1.0 setting, default true. SwitchDetection.Run
    // is a no-op when this is off.
    bool SparseIntegerSwitch = true;
    // Whether to delete unreachable blocks left over after a transform.
    // DecompilerSettings.RemoveDeadAndSideEffectFreeCodeUseWithCaution -- an F#
    // decompilation aid, default false. SwitchDetection uses it to choose
    // between SortBlocks(deleteUnreachableBlocks) and Blocks.RemoveAll(empty);
    // this port leaves the dead blocks in place either way (see D58), so the
    // setting only affects whether the analysis allows unreachable cases.
    bool RemoveDeadCode = false;
    // Whether to lift nullable-value operations into nullable-aware forms.
    // DecompilerSettings.LiftNullables -- a C# 2.0 setting, default true (false
    // for C# 1). Gates SwitchOnNullableTransform and the nullable-lifting
    // expression transforms. The nullable-lifting helper subset
    // (MatchHasValueCall / MatchGetValueOrDefault) consults this implicitly via
    // the transforms that call them.
    bool LiftNullables = true;
};

class ILTransformContext {
public:
    ILTransformSettings Settings;
    // Debug transition log (C# ILTransformContext.Step). Set by tools/tests to
    // observe per-step rewrites; null in production.
    std::function<void(const char* what)> Step;

    void StepOnce(const char* what) const {
        if (Step) Step(what);
    }
};

// Per-function ILAst transform.
class IILTransform {
public:
    virtual ~IILTransform() = default;
    virtual void Run(ILFunction& function, ILTransformContext& context) = 0;
};

} // namespace ILSpy::Decompiler::IL
