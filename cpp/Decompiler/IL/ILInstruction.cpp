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

#include "Decompiler/IL/ILInstruction.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"

#include <functional>
#include <set>

#include <cassert>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

InstructionFlags ILInstruction::Flags() const {
    InstructionFlags f = DirectFlags();
    for (int i = 0; i < ChildCount(); ++i) {
        if (auto* c = GetChild(i)) f = f | c->Flags();
    }
    return f;
}

void ILInstruction::AddILRange(std::int32_t start, std::int32_t end) {
    // Port of ILInstruction.CombineILRange: merge (start, end) into the existing
    // (StartILOffset, EndILOffset). An empty side adopts the other; disjoint
    // ranges keep the earlier one; overlapping/adjacent ranges join into
    // [min Start, max End). The C# uses unchecked arithmetic for the int.MaxValue+1
    // universe sentinel; IL offsets are far below that, so plain int32 is safe.
    const bool oldEmpty = (StartILOffset >= EndILOffset);
    const bool newEmpty = (start >= end);
    if (oldEmpty) { StartILOffset = start; EndILOffset = end; return; }
    if (newEmpty) return;  // keep the existing range
    if (start <= StartILOffset) {
        if (end < StartILOffset) {
            StartILOffset = start;  // the new range is entirely earlier; adopt it
            EndILOffset = end;
        } else if (end > EndILOffset) {
            StartILOffset = start;  // join overlapping/adjacent
            EndILOffset = end;
        } else {
            StartILOffset = start;  // new covers old's start; end within old
        }
    } else if (start <= EndILOffset) {
        if (end > EndILOffset) EndILOffset = end;  // join overlapping/adjacent
        // else new is wholly inside old; keep old
    }
    // else new is entirely after old; keep the existing range
}

void ILInstruction::SetChild(int index, std::unique_ptr<ILInstruction> newChild) {
    assert(newChild && "SetChild: newChild must not be null");
    assert((newChild->Parent == nullptr) && "SetChild: child already has a parent (ILAst must form a tree)");
    auto prev = SetChildRaw(index, std::move(newChild));
    // The slot now holds the new child; wire its back-pointer.
    auto* placed = GetChild(index);
    if (placed) { placed->Parent = this; placed->ChildIndex = index; }
    // prev is released (destroyed) on scope exit.
    (void)prev;
}

std::unique_ptr<ILInstruction> ILInstruction::TakeChild(int index) {
    auto prev = SetChildRaw(index, nullptr);
    if (prev) { prev->Parent = nullptr; prev->ChildIndex = -1; }
    return prev;
}

void ILInstruction::ReplaceWith(std::unique_ptr<ILInstruction> other) {
    assert(Parent && "ReplaceWith: node has no parent");
    assert(other && !other->Parent && "ReplaceWith: replacement must be detached");
    ILInstruction* parent = Parent;
    int index = ChildIndex;
    parent->TakeChild(index);  // destroys `this`; no member access past this line
    parent->SetChild(index, std::move(other));
}

bool ILInstruction::IsConnected() const {
    // A node is connected iff it is part of a tree rooted at an ILFunction. A
    // standalone subtree (root not an ILFunction) is disconnected, and so are
    // its children -- this is the property CheckInvariant checks for every pair.
    if (IsRoot()) return true;
    return Parent != nullptr && Parent->IsConnected();
}

void ILInstruction::CheckInvariant(ILPhase phase) const {
#ifndef NDEBUG
    for (int i = 0; i < ChildCount(); ++i) {
        auto* child = GetChild(i);
        if (!child) continue;
        assert(child->Parent == this && "ILAst: child Parent mismatch");
        assert(child->ChildIndex == i && "ILAst: child ChildIndex mismatch");
        assert(GetChild(child->ChildIndex) == child && "ILAst: GetChild(ChildIndex) != child");
        assert(child->IsConnected() == this->IsConnected() && "ILAst: connectedness mismatch");
        child->CheckInvariant(phase);
    }
    assert((static_cast<std::uint32_t>(DirectFlags()) & ~static_cast<std::uint32_t>(Flags())) == 0
           && "ILAst: DirectFlags not a subset of Flags");
#else
    (void)phase;
#endif
}

std::string ILInstruction::ToString() const {
    std::string out;
    WriteTo(out);
    return out;
}

bool ILInstruction::HasCycle() const {
    // Iterative DFS over the strict tree via the generic ChildCount/GetChild
    // API (every node implements them). A visited-set of child pointers detects
    // a cycle (a node reachable from itself); the stack tracks the (node, next-
    // child-index) pairs so a single pass covers the whole tree. A strict tree
    // has no cycles; a Parent-pointer cycle (a transform bug) makes a node its
    // own descendant and is caught here.
    std::unordered_set<const ILInstruction*> seen;
    struct Frame {
        const ILInstruction* node;
        int nextChild;
    };
    std::vector<Frame> stack;
    stack.push_back({this, 0});
    seen.insert(this);
    while (!stack.empty()) {
        auto& top = stack.back();
        if (top.nextChild >= top.node->ChildCount()) {
            stack.pop_back();
            continue;
        }
        const ILInstruction* child = top.node->GetChild(top.nextChild);
        ++top.nextChild;
        if (!child) continue;
        if (!seen.insert(child).second) return true;  // already visited -> cycle
        stack.push_back({child, 0});
    }
    return false;
}

} // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::IL {

// The C# `internal static bool MayReorder(ILInstruction inst1, ILInstruction
// inst2)` (SemanticHelper.cs): whether the sequence 'inst1; inst2;' may be
// ordered 'inst2; inst1;'. NOT the pure flag-pair approximation: the C#
// checks the written variables against the read variables (the C#
// Inst2MightWriteToVariableReadByInst1), so a store to one local may reorder
// past a load of another. The flag-level MayReorder(InstructionFlags,
// InstructionFlags) overload above stays for the flag-only call sites.
bool MayReorder(ILInstruction* inst1, ILInstruction* inst2) {
    if (inst1 == nullptr || inst2 == nullptr) return false;
    const auto isPure = [](const ILInstruction* inst) {
        const InstructionFlags pureFlags =
            InstructionFlags::MayReadLocals | InstructionFlags::ControlFlow;
        return (inst->Flags() & ~pureFlags) == InstructionFlags::None;
    };
    if (!isPure(inst1) && !isPure(inst2)) return false;
    // Inst2MightWriteToVariableReadByInst1: whether writer might write a
    // variable reader reads (the C# walks the LdLoc reads and the direct
    // MayWriteLocals writes; indirect writes through address-taken locals
    // block any reorder when the writer has a side effect).
    const auto mightWriteToVariableReadBy = [](const ILInstruction* reader,
                                               const ILInstruction* writer) {
        if (!HasFlag(reader->Flags(), InstructionFlags::MayReadLocals))
            return false;
        std::set<const ILVariable*> variables;
        std::function<void(const ILInstruction*)> collect =
            [&](const ILInstruction* inst) {
                if (!inst) return;
                if (inst->Op == OpCode::LdLoc) {
                    if (auto* v = static_cast<const LdLoc*>(inst)->Variable.get())
                        variables.insert(v);
                    return;  // LdLoc has no instruction children
                }
                for (int i = 0; i < inst->ChildCount(); ++i)
                    collect(inst->GetChild(i));
            };
        collect(reader);
        if (HasFlag(writer->Flags(), InstructionFlags::SideEffect)) {
            bool addressTaken = false;
            std::function<void(const ILInstruction*)> checkAddresses =
                [&](const ILInstruction* inst) {
                    if (!inst || addressTaken) return;
                    if (auto* ldloca = dynamic_cast<const LdLoca*>(inst)) {
                        if (ldloca->Variable != nullptr &&
                            variables.count(ldloca->Variable.get()) != 0 &&
                            ldloca->Variable->AddressCount > 0) {
                            addressTaken = true;
                            return;
                        }
                    }
                    for (int i = 0; i < inst->ChildCount(); ++i)
                        checkAddresses(inst->GetChild(i));
                };
            checkAddresses(writer);
            if (addressTaken) return true;
        }
        bool foundWrite = false;
        std::function<void(const ILInstruction*)> checkWrites =
            [&](const ILInstruction* inst) {
                if (!inst || foundWrite) return;
                if (HasFlag(inst->DirectFlags(), InstructionFlags::MayWriteLocals)) {
                    if (auto* stloc = dynamic_cast<const StLoc*>(inst)) {
                        if (stloc->Variable != nullptr &&
                            variables.count(stloc->Variable.get()) != 0) {
                            foundWrite = true;
                            return;
                        }
                    }
                }
                for (int i = 0; i < inst->ChildCount(); ++i)
                    checkWrites(inst->GetChild(i));
            };
        checkWrites(writer);
        return foundWrite;
    };
    if (mightWriteToVariableReadBy(inst1, inst2)) return false;
    if (mightWriteToVariableReadBy(inst2, inst1)) return false;
    return true;
}

} // namespace ILSpy::Decompiler::IL