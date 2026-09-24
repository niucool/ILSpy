// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/NamedArgumentTransform.cs -- the
// per-statement transform that converts a call argument's inlined load into a
// named argument when argument evaluation order would otherwise reorder
// side effects. The C# pieces:
//   * `internal static FindResult CanIntroduceNamedArgument(CallInstruction
//     call, ILInstruction child, ILVariable v, ILInstruction
//     expressionBeingMoved)` -- the call gates (this-pointer slot, operators and
//     accessors, varargs, delegate/anonymous-type constructors, empty parameter
//     names) and the scan for the load in a later argument slot.
//   * `internal static FindResult CanExtendNamedArgument(Block block,
//     ILVariable v, ILInstruction expressionBeingMoved)` -- the scan over an
//     existing CallWithNamedArgs block's slots.
//   * `internal static void IntroduceNamedArgument(ILInstruction arg,
//     ILTransformContext context)` -- the CallWithNamedArgs block creation
//     (the this-pointer slot included) and the StLoc/LdLoc promotion.
//   * `public void Run(Block block, int pos, StatementTransformContext
//     context)` -- the settings gate plus the OptionsForBlock
//     (IntroduceNamedArguments | AllowChangingOrderOfEvaluationForExceptions)
//     and the InlineOneIfPossible retry.
// The port's ILInlining (ILInlining.hpp) models the C# `InliningOptions` and
// threads `FindResultType::NamedArgument` through `FindLoadInNext`, whose Call
// arm calls `CanIntroduceNamedArgument` (the C# static call). This port's
// reader-decoded Call nodes carry no resolved IMethod; the call gates stop
// those (the C# reader always resolves the IMethod).

#ifndef ILSPY_DECOMPILER_IL_TRANSFORMS_NAMEDARGUMENTTRANSFORM_HPP
#define ILSPY_DECOMPILER_IL_TRANSFORMS_NAMEDARGUMENTTRANSFORM_HPP

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <string>

namespace ILSpy::Decompiler::IL {

class ILInstruction;
class ILVariable;
class Block;
class Call;
struct FindResult;

// The C# `internal static FindResult CanIntroduceNamedArgument(CallInstruction
// call, ILInstruction child, ILVariable v, ILInstruction
// expressionBeingMoved)` -- whether the load of `v` inside `child` can be
// replaced by `expressionBeingMoved` by promoting `child` to a named argument
// of `call`. Returns the NamedArgument find result (with the call argument)
// or Stop.
FindResult NamedArgumentCanIntroduce(Call* call, ILInstruction* child,
                                     ILVariable* v,
                                     ILInstruction* expressionBeingMoved);

// The C# `internal static void IntroduceNamedArgument(ILInstruction arg,
// ILTransformContext context)` -- promote `arg` (a call argument) to a named
// argument: create the CallWithNamedArgs block (with the this-pointer slot for
// an instance call), store the argument into a fresh NamedArgument variable and
// load it back in the argument slot.
void NamedArgumentIntroduce(ILInstruction* arg, ILTransformContext& context);

// The C# `internal static FindResult CanExtendNamedArgument(Block block,
// ILVariable v, ILInstruction expressionBeingMoved)` -- the scan over an
// existing CallWithNamedArgs block. Exposed so ILInlining's FindLoadInNext can
// consult it for the BlockKind.CallWithNamedArgs arm (the C# call is the
// static `NamedArgumentTransform.CanExtendNamedArgument`).
FindResult NamedArgumentCanExtend(Block* block, ILVariable* v,
                                  ILInstruction* expressionBeingMoved);

// The C# `public class NamedArgumentTransform : IStatementTransform`.
class NamedArgumentTransform : public IStatementTransform {
public:
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL

#endif // ILSPY_DECOMPILER_IL_TRANSFORMS_NAMEDARGUMENTTRANSFORM_HPP