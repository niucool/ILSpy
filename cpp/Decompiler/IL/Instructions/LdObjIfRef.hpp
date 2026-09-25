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

// LdObjIfRef: the constrained.-prefix this-argument wrapper the IL reader emits
// when a constrained `this` argument must be loaded as an object reference.
// Port of the C# generated `LdObjIfRef` node (IL/Instructions.cs): a single
// `Target` child (canInlineInto), a `Type` operand, `ResultType` Ref,
// `DirectFlags` SideEffect|MayThrow with `ComputeFlags()` = the target's flags
// OR the direct ones.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class LdObjIfRef final : public ILInstruction {
public:
    // The C# `IType type` operand (the type the reference points at).
    TypeSystem::ITypePtr Type;

    LdObjIfRef(std::unique_ptr<ILInstruction> target, TypeSystem::ITypePtr type)
        : ILInstruction(OpCode::LdObjIfRef), Type(std::move(type)) {
        if (target) {
            target->Parent = this;
            target->ChildIndex = 0;
        }
        Target_ = std::move(target);
    }

    // The C# `public ILInstruction Target` child (the TargetSlot, canInlineInto).
    ILInstruction* Target() const { return Target_.get(); }

    // The C# `public override StackType ResultType { get { return StackType.Ref; } }`.
    StackType ResultType() const override { return StackType::Ref; }

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::SideEffect | InstructionFlags::MayThrow;
    }

    // The C# `protected override InstructionFlags ComputeFlags()` -- the target's
    // flags OR'd with the direct ones.
    InstructionFlags Flags() const override {
        auto flags = DirectFlags();
        if (Target_) flags = flags | Target_->Flags();
        return flags;
    }

    int ChildCount() const override { return Target_ ? 1 : 0; }

    ILInstruction* GetChild(int i) const override { return i == 0 ? Target_.get() : nullptr; }

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Target_);
        if (n) {
            n->Parent = this;
            n->ChildIndex = 0;
        }
        Target_ = std::move(n);
        return old;
    }

public:
    void WriteTo(std::string& out) const override {
        out += "ldobj_ifref ";
        out += Type ? Type->ReflectionName() : std::string("?");
        out += '(';
        if (Target_) Target_->WriteTo(out); else out += "(null)";
        out += ')';
    }

private:
    std::unique_ptr<ILInstruction> Target_;
};

} // namespace ILSpy::Decompiler::IL
