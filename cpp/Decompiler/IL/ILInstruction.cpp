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

#include <cassert>
#include <utility>

namespace ILSpy::Decompiler::IL {

InstructionFlags ILInstruction::Flags() const {
    InstructionFlags f = DirectFlags();
    for (int i = 0; i < ChildCount(); ++i) {
        if (auto* c = GetChild(i)) f = f | c->Flags();
    }
    return f;
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

} // namespace ILSpy::Decompiler::IL
