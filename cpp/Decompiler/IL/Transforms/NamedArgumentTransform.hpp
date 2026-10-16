// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

// Port of ICSharpCode.Decompiler/IL/Transforms/NamedArgumentTransform.cs. When a
// stloc's value can only be inlined into a call argument that is evaluated after
// another argument with observable effects, the transform promotes that argument
// to a C# named argument (a Block of kind CallWithNamedArgs whose final is the
// call, with the promoted argument stored early into a NamedArgument variable) so
// the value can be evaluated before the remaining arguments. The C# static
// helpers are also called by ILInlining.FindLoadInNext / DoInline, so they live as
// static methods here and ILInlining.cpp includes this header.
//
// Like the C# this transform is a per-statement child of the StatementTransform
// pipeline; the port leaves it out of GetILTransforms() until the CallWithNamedArgs
// block render in CallBuilder lands (the two form one coherent feature), so it is
// verified by direct unit tests.

#pragma once

#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class Call;
class ILInstruction;
class ILVariable;

class NamedArgumentTransform : public IStatementTransform {
public:
    // The C# `internal static FindResult CanIntroduceNamedArgument(CallInstruction
    // call, ILInstruction child, ILVariable v, ILInstruction expressionBeingMoved)`:
    // can the call `call` have its argument containing `child` promoted to a named
    // argument so that the load of `v` becomes reachable? Returns NamedArgument
    // (with the load and the argument to promote) when a later argument contains
    // the load, otherwise Stop.
    static FindResult CanIntroduceNamedArgument(Call* call, ILInstruction* child,
                                                ILVariable* v,
                                                ILInstruction* expressionBeingMoved);

    // The C# `internal static FindResult CanExtendNamedArgument(Block block,
    // ILVariable v, ILInstruction expressionBeingMoved)`: the search for the load
    // when the next instruction is already a CallWithNamedArgs block. The port
    // expects `block` to be such a block.
    static FindResult CanExtendNamedArgument(Block* block, ILVariable* v,
                                             ILInstruction* expressionBeingMoved);

    // The C# `internal static void IntroduceNamedArgument(ILInstruction arg,
    // ILTransformContext context)`: promote `arg` (a call argument) to a named
    // argument, creating/wrapping the CallWithNamedArgs block and the
    // NamedArgument variable.
    static void IntroduceNamedArgument(ILInstruction* arg, ILTransformContext& context);

    // IStatementTransform: run one named-argument inlining attempt at `pos`.
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
