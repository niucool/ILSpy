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

// Port of the ILInstruction abstract base (ICSharpCode.Decompiler/IL/Instructions/
// ILInstruction.cs). The ILAst is a strict tree: each child belongs to exactly one
// parent. In C++ the parent owns its children via std::unique_ptr slots and holds a
// non-owning Parent back-pointer; SetChild enforces the tree invariant (a new child
// must not already have a parent). Flags recompute bottom-up from DirectFlags and
// children for the foundation; the C# caches with parent-chain invalidation, added
// later when transforms need the speed. CheckInvariant runs in debug builds after
// every transform, exactly as in C# -- it is the safety net for the loss of NRTs.

#pragma once

#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// Invariant phases tighten over time (matches the C# ILPhase). The invariant
// checks branch on the phase; in InILReader branches may still point at offsets.
enum class ILPhase {
    InILReader,
    Normal,
    InAsyncAwait,
};

class ILInstruction {
public:
    const OpCode Op;
    ILInstruction* Parent = nullptr;  // non-owning back-pointer; null at a root
    int ChildIndex = -1;

    // The IL byte-offset range this instruction was decoded from -- the C#
    // ILInstruction.ILRange (an Interval). Default {0,0} is the empty range
    // (Start >= End), matching the C# default Interval. Populated by the IL
    // reader at decode time (the [StartILOffset, EndILOffset) span of the opcode
    // and its operands); transforms that clone or merge instructions should
    // propagate the range via AddILRange. Consulted by transforms that compare
    // instruction positions against the method body -- notably
    // ILInlining.IsInConstructorInitializer (whether a hoisted null-guard sits
    // in the constructor initializer, before the chained : base/: this call).
    std::int32_t StartILOffset = 0;
    std::int32_t EndILOffset = 0;

    // True when the range is empty (Start >= End). Matches the C# Interval.IsEmpty
    // (Start > InclusiveEnd == End - 1), simplified to Start >= End; the C# universe
    // special-case (Start == End == int.MinValue) is not modelled here because no
    // current consumer needs it.
    bool IsILRangeEmpty() const noexcept { return StartILOffset >= EndILOffset; }

    // Set the range directly (the reader's per-instruction assignment).
    void SetILRange(std::int32_t start, std::int32_t end) {
        StartILOffset = start;
        EndILOffset = end;
    }
    // Copy the range from another instruction (transforms that move an
    // instruction into a new node, mirroring ILInstruction.SetILRange(source)).
    void SetILRange(const ILInstruction& src) {
        StartILOffset = src.StartILOffset;
        EndILOffset = src.EndILOffset;
    }
    // Combine another range into this one (mirrors ILInstruction.AddILRange /
    // CombineILRange): an empty side adopts the other; disjoint ranges keep the
    // earlier one; overlapping/adjacent ranges join into [min Start, max End).
    void AddILRange(std::int32_t start, std::int32_t end);
    void AddILRange(const ILInstruction& src) { AddILRange(src.StartILOffset, src.EndILOffset); }

    virtual ~ILInstruction() = default;

    // Flags this node contributes directly (excluding descendants).
    virtual InstructionFlags DirectFlags() const = 0;
    // The stack type this node produces when evaluated (Unknown/Void for none).
    virtual StackType ResultType() const = 0;

    // Tree structure. Children are owned by the parent via typed slots; GetChild
    // returns a non-owning pointer to the i-th child in slot order.
    virtual int ChildCount() const = 0;
    virtual ILInstruction* GetChild(int index) const = 0;

    // Flags including descendants (DirectFlags | union of children's Flags).
    // Virtual: control-flow nodes (If/TryCatch/...) combine branch flags
    // instead of unioning (the endpoint is reachable if any path is).
    virtual InstructionFlags Flags() const;

    // Place newChild into slot `index`, taking ownership. The new child must not
    // already have a parent (strict-tree invariant); the previous occupant is
    // destroyed. Asserts on a violation in debug builds.
    void SetChild(int index, std::unique_ptr<ILInstruction> newChild);
    // Remove and return ownership of the child in slot `index` (orphaning it).
    std::unique_ptr<ILInstruction> TakeChild(int index);

    // Replace this node with `other` in its parent's slot (ILInstruction.ReplaceWith).
    // This node is destroyed; `other` must be detached (no parent).
    void ReplaceWith(std::unique_ptr<ILInstruction> other);

    // Create a deep clone of this instruction (port of ILInstruction.Clone).
    // The clone is disconnected (no parent, ChildIndex -1) and owns its own
    // copy of every child and scalar field. ILVariables, Branch target blocks,
    // and Leave target containers are references (shared / non-owning), not
    // owned children, so they are copied by reference -- matching the C# where
    // those are not owned subtrees; a clone inserted elsewhere keeps pointing
    // at the original tree's variables/blocks/containers and the caller fixes
    // them up. The IL byte-range (StartILOffset/EndILOffset) is copied.
    virtual std::unique_ptr<ILInstruction> Clone() const;

    // True if this is connected to a root (has a parent, or is itself a root).
    bool IsConnected() const;
    // ILFunction is the tree root; overrides return true.
    virtual bool IsRoot() const { return false; }

    // True if `ancestor` is a transitive parent of this node (exclusive).
    bool IsDescendantOf(const ILInstruction* ancestor) const {
        for (const ILInstruction* p = Parent; p != nullptr; p = p->Parent)
            if (p == ancestor) return true;
        return false;
    }

    // Debug-only tree invariant (parent/child consistency, flag consistency,
    // connectedness). Mirrors ILInstruction.CheckInvariant; a no-op in NDEBUG.
    void CheckInvariant(ILPhase phase) const;

    // Dump the tree to text (mirrors WriteTo).
    virtual void WriteTo(std::string& out) const = 0;
    std::string ToString() const;

    // Whether the tree rooted at this node contains a cycle (a node is its own
    // descendant). The strict-tree invariant forbids this (a child belongs to
    // one parent), but a transform bug could produce a Parent-pointer cycle
    // that WriteTo -- a recursive, unbounded walker -- would follow forever,
    // emitting a repeated token (e.g. `lock (...)`) ad infinitum and OOM-ing.
    // Detecting a cycle before a full-tree dump (ToString) lets the caller
    // emit a marker instead of the runaway. Generic walk via ChildCount/
    // GetChild (every node implements them) with a visited-set; O(n) time/
    // space. Mirrors the cycle-detection CheckInvariant would do, but usable
    // in release builds where CheckInvariant is a no-op.
    bool HasCycle() const;

protected:
    explicit ILInstruction(OpCode opCode) : Op(opCode) {}

    // Slot implementation: store newChild in slot `index`, return the prior
    // occupant (or nullptr). Each concrete instruction overrides to manage its
    // typed unique_ptr slots / collection.
    virtual std::unique_ptr<ILInstruction> SetChildRaw(int index,
        std::unique_ptr<ILInstruction> newChild) = 0;
};

} // namespace ILSpy::Decompiler::IL
