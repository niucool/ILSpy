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
    InstructionFlags Flags() const;

    // Place newChild into slot `index`, taking ownership. The new child must not
    // already have a parent (strict-tree invariant); the previous occupant is
    // destroyed. Asserts on a violation in debug builds.
    void SetChild(int index, std::unique_ptr<ILInstruction> newChild);
    // Remove and return ownership of the child in slot `index` (orphaning it).
    std::unique_ptr<ILInstruction> TakeChild(int index);

    // Replace this node with `other` in its parent's slot (ILInstruction.ReplaceWith).
    // This node is destroyed; `other` must be detached (no parent).
    void ReplaceWith(std::unique_ptr<ILInstruction> other);

    // True if this is connected to a root (has a parent, or is itself a root).
    bool IsConnected() const;
    // ILFunction is the tree root; overrides return true.
    virtual bool IsRoot() const { return false; }

    // Debug-only tree invariant (parent/child consistency, flag consistency,
    // connectedness). Mirrors ILInstruction.CheckInvariant; a no-op in NDEBUG.
    void CheckInvariant(ILPhase phase) const;

    // Dump the tree to text (mirrors WriteTo).
    virtual void WriteTo(std::string& out) const = 0;
    std::string ToString() const;

protected:
    explicit ILInstruction(OpCode opCode) : Op(opCode) {}

    // Slot implementation: store newChild in slot `index`, return the prior
    // occupant (or nullptr). Each concrete instruction overrides to manage its
    // typed unique_ptr slots / collection.
    virtual std::unique_ptr<ILInstruction> SetChildRaw(int index,
        std::unique_ptr<ILInstruction> newChild) = 0;
};

} // namespace ILSpy::Decompiler::IL
