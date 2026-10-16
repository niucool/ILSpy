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

// Port of ICSharpCode.Decompiler/IL/Transforms/ProxyCallReplacer.cs: replaces
// calls to compiler-generated pass-through methods (an async state machine's
// forwarding shims) with the method they forward to. The C# decodes the
// proxy's body with a fresh IL reader (`context.CreateILReader().ReadIL(...)`)
// and runs CSharpDecompiler.EarlyILTransforms() over the decoded function;
// the port routes the decode through the context's DelegateBodyResolver hook
// (the CreateILReader bridge the delegate-construction path established --
// the facade wires it to ReadIL over the module) and runs the early list
// (ControlFlowSimplification + SplitVariables + ILInlining) in place.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include "Decompiler/IL/Instructions/Call.hpp"  // the per-call Run parameter

namespace ILSpy::Decompiler::IL {

class ILFunction;

class ProxyCallReplacer : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

private:
    // The C# `void Run(CallInstruction inst, ILTransformContext context)`
    // (the per-call step the descendant walk dispatches to).
    void Run(Call& inst, ILFunction& function, ILTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
