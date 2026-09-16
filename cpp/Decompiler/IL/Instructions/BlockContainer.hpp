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

// BlockContainer: a single-entry control-flow region owning a list of Blocks.
// The CFG lives in the tree: a Branch targets a block of this or an enclosing
// container; a Leave exits this container. Transforms introduce more containers
// (loops become ContainerKind.Loop, switches ContainerKind.Switch).

#pragma once

#include "Decompiler/IL/Instructions/Block.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

enum class ContainerKind : std::uint8_t {
    Normal,
    Loop,
    While,
    DoWhile,
    // A while-style loop whose increment lives in a dedicated block (moved to
    // the container's end by HighLevelLoopTransform's for-loop match): the
    // emitter renders it as `for (; cond; increment) { body }`.
    For,
    Switch,
};

class BlockContainer : public ILInstruction {
public:
    std::vector<std::unique_ptr<Block>> Blocks;
    ContainerKind Kind = ContainerKind::Normal;

    BlockContainer() : ILInstruction(OpCode::BlockContainer) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Void; }

    int ChildCount() const override { return static_cast<int>(Blocks.size()); }
    ILInstruction* GetChild(int i) const override {
        return (i >= 0 && i < static_cast<int>(Blocks.size())) ? Blocks[i].get() : nullptr;
    }

    void AddBlock(std::unique_ptr<Block> b) {
        if (b) { b->Parent = this; b->ChildIndex = static_cast<int>(Blocks.size()); }
        Blocks.push_back(std::move(b));
    }

    // The C# `public Block EntryPoint` (BlockContainer.cs line 66): the container's
    // entry point -- the first block in the Blocks collection. The C# reads a private
    // field assigned by the BlockBuilder's Normalize; the port's containers are
    // statically normalized (the invariant `Blocks.Count > 0 && EntryPoint ==
    // Blocks[0]` holds by construction), so the accessor computes it. Null for a
    // degenerate block-less container (the C# HACK would deref `entryPoint!`).
    Block* EntryPoint() const {
        return Blocks.empty() ? nullptr : Blocks.front().get();
    }

    // The C# `public static BlockContainer? FindClosestSwitchContainer(
    // ILInstruction? inst)` (BlockContainer.cs line 345): walks the parent chain
    // for the closest enclosing Switch-kind container (null when none). The
    // port's containers are complete here, so the dynamic_cast on each parent
    // is well-formed.
    static BlockContainer* FindClosestSwitchContainer(ILInstruction* inst) {
        while (inst != nullptr) {
            if (auto* bc = dynamic_cast<BlockContainer*>(inst);
                bc != nullptr && bc->Kind == ContainerKind::Switch)
                return bc;
            inst = inst->Parent;
        }
        return nullptr;
    }

    // The C# `public bool MatchConditionBlock(Block block, out ILInstruction?
    // condition, out Block? bodyStartBlock)` (BlockContainer.cs line 356): a
    // single-instruction block whose one instruction is an IfInstruction whose
    // false arm leaves THIS container and whose true arm branches to the loop
    // body start (the loop-condition block shape the Loop/While/For loop
    // matches consume). Defined out-of-line in Block.cpp (the shared pattern
    // matchers it calls pull the include chain).
    bool MatchConditionBlock(Block* block, ILInstruction*& condition, Block*& bodyStartBlock);

    // The C# `public bool MatchIncrementBlock(Block block)` (BlockContainer.cs
    // line 367): the block whose last instruction branches to the container's
    // entry point (the for-loop increment-block shape). Out-of-line in
    // Block.cpp.
    bool MatchIncrementBlock(Block* block);

    void WriteTo(std::string& out) const override {
        out += "BlockContainer {\n";
        for (auto& b : Blocks) {
            out += "  ";
            if (b) b->WriteTo(out); else out += "(null)";
            out += '\n';
        }
        out += "}";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i < 0 || i >= static_cast<int>(Blocks.size())) return n;
        auto old = std::move(Blocks[i]);
        // A BlockContainer slot always holds a Block.
        Blocks[i].reset(static_cast<Block*>(n.release()));
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
