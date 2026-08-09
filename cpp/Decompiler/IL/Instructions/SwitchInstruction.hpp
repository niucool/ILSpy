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

// SwitchInstruction + SwitchSection, faithful to the generated Instructions.cs.
// switch(value) dispatches to one of N sections by the value's int label; the
// default section carries no labels. Each section's Body is the branch to its
// target block (the IL reader emits Branch(targetOffset); BlockBuilder resolves).
// DirectFlags = ControlFlow; result Void.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class SwitchSection : public ILInstruction {
public:
    // The case constants for this section. The default section carries an empty
    // LongSet. LongSet (intervals) rather than std::set<int64_t> because the
    // switch-family transforms compute complements (e.g. `new LongSet(val).Invert()`),
    // which are infinite and need the interval representation.
    Util::LongSet Labels;
    std::unique_ptr<ILInstruction> Body;  // a Branch (offset form) post-reader
    // True when this section is the `case null:` arm of a lifted switch on a
    // Nullable<T> (set by SwitchOnNullableTransform / the AddNullCase path).
    // Carries no labels (null is not an integer label); the seed renders it as
    // `case null:`. Mirrors SwitchSection.HasNullLabel in the generated
    // Instructions.cs.
    bool HasNullLabel = false;
    SwitchSection() : ILInstruction(OpCode::SwitchSection) {}
    explicit SwitchSection(Util::LongSet labels) : ILInstruction(OpCode::SwitchSection), Labels(std::move(labels)) {}
    InstructionFlags DirectFlags() const override { return InstructionFlags::None; }
    StackType ResultType() const override { return StackType::Void; }
    int ChildCount() const override { return Body ? 1 : 0; }
    ILInstruction* GetChild(int i) const override { return i == 0 ? Body.get() : nullptr; }
    // Set the section body, wiring the child's parent/index (the ctor can't,
    // since Body is often assigned after construction).
    void SetBody(std::unique_ptr<ILInstruction> body) {
        if (body) { body->Parent = this; body->ChildIndex = 0; }
        Body = std::move(body);
    }
    void WriteTo(std::string& out) const override {
        out += "section(";
        bool first = true;
        for (const auto& iv : Labels.Intervals()) {
            if (!first) out += ", ";
            if (iv.Start == iv.InclusiveEnd())
                out += std::to_string(iv.Start);
            else
                out += std::to_string(iv.Start) + ".." + std::to_string(iv.InclusiveEnd());
            first = false;
        }
        if (first) out += "default";
        if (HasNullLabel) out += ", null";
        out += "): ";
        if (Body) Body->WriteTo(out); else out += "(null)";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i == 0);
        auto old = std::move(Body);
        Body = std::move(n);
        return old;
    }
};

class SwitchInstruction : public ILInstruction {
public:
    std::unique_ptr<ILInstruction> Value;
    std::vector<std::unique_ptr<SwitchSection>> Sections;
    // True when this switch dispatches on a Nullable<T>'s underlying value and
    // carries a `case null:` section (set by SwitchOnNullableTransform).
    // Mirrors SwitchInstruction.IsLifted in the generated Instructions.cs.
    bool IsLifted = false;
    // For a lifted switch, the Nullable<T> type the switch dispatches on (the
    // lift context for the null case). Mirrors SwitchInstruction.Type.
    TypeSystem::ITypePtr Type;
    explicit SwitchInstruction(std::unique_ptr<ILInstruction> value)
        : ILInstruction(OpCode::SwitchInstruction), Value(std::move(value)) {
        if (Value) { Value->Parent = this; Value->ChildIndex = 0; }
    }
    InstructionFlags DirectFlags() const override { return InstructionFlags::ControlFlow; }
    StackType ResultType() const override { return StackType::Void; }

    int ChildCount() const override { return (Value ? 1 : 0) + static_cast<int>(Sections.size()); }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return Value.get();
        int s = i - 1;
        return (s >= 0 && s < static_cast<int>(Sections.size())) ? Sections[s].get() : nullptr;
    }

    void AddSection(std::unique_ptr<SwitchSection> s) {
        if (s) { s->Parent = this; s->ChildIndex = static_cast<int>(Sections.size()) + 1; }
        Sections.push_back(std::move(s));
    }

    void WriteTo(std::string& out) const override {
        out += "switch ";
        if (Value) Value->WriteTo(out); else out += "(null)";
        if (IsLifted) out += " lifted";
        out += " {\n";
        for (auto& s : Sections) {
            out += "    ";
            if (s) s->WriteTo(out); else out += "(null)";
            out += '\n';
        }
        out += "  }";
    }
protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(Value); Value = std::move(n); return old; }
        int s = i - 1;
        if (s < 0 || s >= static_cast<int>(Sections.size())) return n;
        auto old = std::move(Sections[s]);
        Sections[s].reset(static_cast<SwitchSection*>(n.release()));
        return old;
    }
};

} // namespace ILSpy::Decompiler::IL
