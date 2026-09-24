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

// Port of ICSharpCode.Decompiler/IL/Instructions/DeconstructInstruction.cs and
// DeconstructResultInstruction.cs (subset):
//   deconstruct {
//       init: stloc v(value) ...                (the InlineDeconstructionInitializer
//                                                temporary stores, can-inline slots)
//       deconstruct: match.deconstruct[Method](v = tested) { sub-patterns }
//                    | match.tuple(v = tested) { sub-patterns }
//       conversions: Block(DeconstructionConversions) { stloc conv(conv(...)) ... }
//       assignments: Block(DeconstructionAssignments) { assignments ... }
//   }
// and the `deconstruct.result N(...)` node the pattern's sub-expressions read
// the Deconstruct call's out-values with.
//
// The DeconstructInstruction.IsAssignment / GetPointerElementType helpers
// (the C# statics over InferType / ByReferenceType / pointer element types)
// port with the StLoc/Call/StObj arms; the pointer-target element-type
// recovery walks the definition chain bounded at 4 steps (the C# bound).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class DeconstructInstruction;

// The C# `partial class DeconstructResultInstruction : UnaryInstruction`
// (DeconstructResultInstruction.cs): reads the out-value of the enclosing
// match.deconstruct's Deconstruct call (the result-slot ordinal + the stack
// type the slot produces).
class DeconstructResultInstruction : public UnaryInstruction {
public:
    int Index = 0;
    StackType resultType = StackType::Unknown;

    DeconstructResultInstruction(int index, StackType resultType,
                                 std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::DeconstructResultInstruction, std::move(argument)),
          Index(index), resultType(resultType) {}

    StackType ResultType() const override { return resultType; }
    void WriteTo(std::string& out) const override {
        out += "deconstruct.result ";
        out += std::to_string(Index);
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// The C# `partial class DeconstructInstruction : ILInstruction`
// (DeconstructInstruction.cs): the folded deconstruction assignment. Children:
// the Init stores (can-inline slots), then the Pattern (a MatchInstruction),
// then the Conversions and Assignments blocks.
class DeconstructInstruction : public ILInstruction {
public:
    // The C# `public readonly InstructionCollection<StLoc> Init` -- the
    // temporary stores the InlineDeconstructionInitializer folds in. Owned
    // here; appended by the transform and the inline arm.
    std::vector<std::unique_ptr<StLoc>> Init;
    MatchInstruction* pattern_ = nullptr;
    std::unique_ptr<Block> conversions_;
    std::unique_ptr<Block> assignments_;

    DeconstructInstruction() : ILInstruction(OpCode::DeconstructInstruction) {}

    MatchInstruction* Pattern() const { return pattern_; }
    void SetPattern(std::unique_ptr<MatchInstruction> p) {
        if (p) { p->Parent = this; p->ChildIndex = static_cast<int>(Init.size()); }
        pattern_ = p.release();
    }

    Block* Conversions() const { return conversions_.get(); }
    void SetConversions(std::unique_ptr<Block> b) {
        if (b) { b->Parent = this; b->ChildIndex = static_cast<int>(Init.size()) + 1; }
        conversions_ = std::move(b);
    }

    Block* Assignments() const { return assignments_.get(); }
    void SetAssignments(std::unique_ptr<Block> b) {
        if (b) { b->Parent = this; b->ChildIndex = static_cast<int>(Init.size()) + 2; }
        assignments_ = std::move(b);
    }

    void AddInit(std::unique_ptr<StLoc> store) {
        if (store) { store->Parent = this; store->ChildIndex = static_cast<int>(Init.size()); }
        Init.push_back(std::move(store));
        // The pattern/conversions/assignments children shift down one slot.
        if (pattern_) pattern_->ChildIndex = static_cast<int>(Init.size());
        if (conversions_) conversions_->ChildIndex = static_cast<int>(Init.size()) + 1;
        if (assignments_) assignments_->ChildIndex = static_cast<int>(Init.size()) + 2;
    }

    int ChildCount() const override {
        return static_cast<int>(Init.size()) + (pattern_ ? 1 : 0) +
               (conversions_ ? 1 : 0) + (assignments_ ? 1 : 0);
    }
    std::unique_ptr<ILInstruction> SetChildRaw(int index,
        std::unique_ptr<ILInstruction> newChild) override {
        if (index < static_cast<int>(Init.size())) {
            auto prior = std::move(Init[index]);
            Init[index] = std::unique_ptr<StLoc>(
                static_cast<StLoc*>(newChild.release()));
            if (Init[index]) {
                Init[index]->Parent = this;
                Init[index]->ChildIndex = index;
            }
            return prior;
        }
        const int k = index - static_cast<int>(Init.size());
        std::unique_ptr<ILInstruction> prior;
        if (k == 0) {
            prior.reset(pattern_);
            pattern_ = nullptr;
            if (newChild) {
                pattern_ = static_cast<MatchInstruction*>(newChild.get());
                pattern_->Parent = this;
                pattern_->ChildIndex = index;
                newChild.release();
            }
            return prior;
        }
        if (k == 1) {
            prior = std::move(conversions_);
            conversions_.reset(static_cast<Block*>(newChild.get()));
            newChild.release();
        }
        prior = std::move(assignments_);
        assignments_.reset(static_cast<Block*>(newChild.get()));
        newChild.release();
        return prior;
    }
    ILInstruction* GetChild(int i) const override {
        if (i < static_cast<int>(Init.size())) return Init[i].get();
        const int k = i - static_cast<int>(Init.size());
        if (k == 0) return pattern_;
        if (k == 1) return conversions_.get();
        if (k == 2) return assignments_.get();
        return nullptr;
    }

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayWriteLocals | InstructionFlags::SideEffect |
               InstructionFlags::MayThrow | InstructionFlags::ControlFlow;
    }
    StackType ResultType() const override { return StackType::Void; }

    void WriteTo(std::string& out) const override {
        out += "deconstruct {\n";
        out += "  init: ";
        for (const auto& store : Init) {
            if (store) store->WriteTo(out);
            out += ' ';
        }
        out += "\n  deconstruct: ";
        if (pattern_) pattern_->WriteTo(out); else out += "(null)";
        out += "\n  conversions: ";
        if (conversions_) conversions_->WriteTo(out); else out += "(null)";
        out += "\n  assignments: ";
        if (assignments_) assignments_->WriteTo(out); else out += "(null)";
        out += "\n}";
    }

    // The C# `internal static bool IsAssignment(ILInstruction inst,
    // ICompilation typeSystem, out IType expectedType, out ILInstruction value)`
    // (DeconstructInstruction.cs line 213): a setter call, a plain stloc, or a
    // stobj whose target is a plain field chain (the C# walks the ldobj case's
    // pointer element type through InferType + the ByReferenceType/PointerType
    // split; the port uses the store's own type for the StObj arm and defers
    // the pointer-target element-type recovery).
    static bool IsAssignment(ILInstruction* inst,
                             const TypeSystem::ICompilation* typeSystem,
                             const TypeSystem::IType*& expectedType,
                             ILInstruction*& value);
};

} // namespace ILSpy::Decompiler::IL