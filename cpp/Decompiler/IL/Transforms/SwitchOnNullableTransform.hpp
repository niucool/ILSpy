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

// Port of ICSharpCode.Decompiler/IL/Transforms/SwitchOnNullableTransform.cs.
//
// Detects the two `switch`-on-Nullable<T> code shapes the C# compiler emits and
// folds them into a single lifted SwitchInstruction carrying a `case null:`
// section (SwitchSection.HasNullLabel), so the seed renders a switch over a
// nullable with an explicit null arm:
//
//   1. Legacy csc `MatchSwitchOnNullable`:
//        stloc tmp(ldloca V)
//        stloc sw(call GetValueOrDefault(ldloc tmp))
//        if (!call get_HasValue(ldloc tmp)) br nullCase
//        br switchBlock
//      where switchBlock holds `switch (ldloc sw) { ... }`. The block's
//      GetValueOrDefault/get_HasValue calls both receive `ldloc tmp` (the
//      address of the nullable). Folded into a lifted `switch (ldloc V)` whose
//      sections are switchBlock's switch's sections plus `case null: nullCase`.
//
//   2. Roslyn `MatchRoslynSwitchOnNullable`:
//        if (!call get_HasValue(target)) br nullCase
//        br switchBlock
//      where switchBlock holds either `stloc sw(call GetValueOrDefault(target));
//      switch (ldloc sw) { ... }` or the inlined `switch (call
//      GetValueOrDefault(target)) { ... }`. Folded into a lifted `switch
//      (target)` (or `switch (ldobj target)` when target is not an address)
//      whose sections are switchBlock's switch's sections plus `case null:`.
//
// Gated on the LiftNullables setting (default true, matching DecompilerSettings
// which only turns it off for C# < 2). Runs after SwitchDetection and before
// LoopDetection (the GetILTransforms() order), so ifs are still block finals
// with positional fall-through (ConditionDetection has not run) and loops are
// flat back-edges.
//
// Adapted to this port's block model: the C# carries the if as a non-terminal at
// Instructions[Count-2] with an explicit fall-through Branch as the block's last
// instruction; this port makes the IfInstruction the block's FinalInstruction
// with an implicit fall-through to the next block in the container. So the
// `br switchBlock` fall-through is NextBlockInContainer(block) (or the if's
// FalseInst target when an earlier transform set one), and the switchBlock's
// `switch (...)` is its FinalInstruction (Instructions empty), not
// Instructions[0]. The C# SortBlocks(deleteUnreachableBlocks: true) cleanup is
// unsafe in this port (D58 -- a block may hold nested containers referenced by
// external branches, and erasing it dangles those TargetBlock pointers), so the
// dead switchBlock stays in the tree (unreachable, harmless) and only the edge
// counts are refreshed.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <memory>

namespace ILSpy::Decompiler::IL {

class ILFunction;
class ILInstruction;
class Block;
class SwitchInstruction;

class SwitchOnNullableTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // Match the legacy csc switch-on-nullable shape in `block` and, on success,
    // build the lifted SwitchInstruction in `newSwitch` (taking ownership of
    // switchBlock's switch sections). Mirrors the C# MatchSwitchOnNullable,
    // adapted to the if-as-final block model.
    bool MatchSwitchOnNullable(Block* block, ILTransformContext& context,
                               std::unique_ptr<SwitchInstruction>& newSwitch);

    // Match the Roslyn switch-on-nullable shape in `block` and, on success,
    // build the lifted SwitchInstruction in `newSwitch`. Mirrors the C#
    // MatchRoslynSwitchOnNullable, adapted to the if-as-final block model.
    bool MatchRoslynSwitchOnNullable(Block* block, ILTransformContext& context,
                                     std::unique_ptr<SwitchInstruction>& newSwitch);
};

} // namespace ILSpy::Decompiler::IL
