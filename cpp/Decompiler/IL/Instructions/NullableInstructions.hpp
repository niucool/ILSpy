// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// NullableRewrap / NullableUnwrap: the ILAst nodes for the C# null-conditional
// (`?.`) operator. Faithful to the generated NullableRewrap / NullableUnwrap in
// ICSharpCode.Decompiler/IL/Instructions.cs and the hand-written
// NullableInstructions.cs. This header mirrors the C# NullableInstructions.cs
// grouping (the port's ArrayInstructions.hpp / MemoryInstructions.hpp /
// TryInstructions.hpp / ThreeValuedBoolInstructions.hpp multi-node convention).
//
// `x?.Member` lowers to `nullable.rewrap(Member(nullable.unwrap(x)))`:
//
//   * NullableUnwrap is the `?.` dereference. It evaluates its Argument (a
//     reference type, a Nullable<T>, or a generic) and either returns the
//     unwrapped value (for a non-null input) or transfers control to the
//     nearest surrounding NullableRewrap (for a null input). It carries the
//     MayUnwrapNull flag so the surrounding NullableRewrap can find it.
//
//   * NullableRewrap is the join point. If a nested NullableUnwrap took the
//     null branch, the NullableRewrap evaluates to null (the whole `?.`
//     expression is null); otherwise it evaluates to its Argument (the
//     access-chain result). It strips the MayUnwrapNull / EndPointUnreachable
//     flags from its Argument (the unwrap's null branch is not a real
//     endpoint) and adds ControlFlow (the implicit null branch).
//
// NullableUnwrap has a RefInput flag (the C# compiler sometimes passes a
// managed reference to the nullable to avoid copying the whole struct before
// the null-check) and a ResultType field (the unwrapped type, set by the
// constructor -- for RefOutput the ResultType is Ref). NullableRewrap's
// ResultType is O for a non-void Argument (a boxed Nullable<T> on the eval
// stack) and Void for a void Argument (a `?.` statement, e.g. `x?.M();` with
// no value used).
//
// Neither node is an IStoreInstruction (both are UnaryInstruction with no
// Variable), so like NullCoalescingInstruction (D88) / ThreeValuedBool (D95)
// they need no ComputeVariableUsage store-counting case; the default branch
// plus the Argument child recursion handles them.
//
// The OpCode::NullableRewrap / OpCode::NullableUnwrap enum values were
// already declared in the port's OpCode.hpp (the enum was seeded with every
// instruction kind early), so porting these nodes needed only the
// UnaryInstruction-subclass header.
//
// This is a tested-but-not-yet-wired foundation (the NullCoalescingInstruction
// / MatchInstruction / UsingInstruction / ThreeValuedBool precedent): no
// pipeline transform constructs these nodes yet. The next in-order consumer
// is NullPropagationTransform (the C# `v != null ? v.AccessChain : null` ->
// `v?.AccessChain` lowering, a separate 583-line transform consulted first
// inside NullableLiftingTransform.Lift and as a per-statement child of
// StatementTransform); this header is the prerequisite that transform needs
// (the `?.` result is a NullableRewrap around the access chain, and
// IntroduceUnwrap rewrites the access-chain receiver loads into NullableUnwrap
// nodes).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// The C# NullableUnwrap.WriteToCore writes `nullable.unwrap.[refinput.]<ResultType>(arg)`.
// StackType has no ToString in this port; this helper mirrors the C# stack-type
// names used in the ILAst dump.
inline const char* StackTypeName(StackType s) {
    switch (s) {
        case StackType::I4: return "I4";
        case StackType::I: return "I";
        case StackType::I8: return "I8";
        case StackType::F4: return "F4";
        case StackType::F8: return "F8";
        case StackType::O: return "O";
        case StackType::Ref: return "Ref";
        case StackType::Void: return "Void";
        case StackType::Unknown: return "Unknown";
    }
    return "?";
}

// NullableRewrap: the join point of a `?.` expression. UnaryInstruction;
// DirectFlags = ControlFlow. ResultType is O for a non-void Argument (the
// `?.` expression's nullable result) and Void for a void Argument (a `?.`
// statement whose value is discarded). Port of the generated NullableRewrap
// (UnaryInstruction, DirectFlags ControlFlow) + the hand-written
// NullableInstructions.cs NullableRewrap (ComputeFlags / ResultType).
class NullableRewrap : public UnaryInstruction {
public:
    explicit NullableRewrap(std::unique_ptr<ILInstruction> argument)
        : UnaryInstruction(OpCode::NullableRewrap, std::move(argument)) {}

    InstructionFlags DirectFlags() const override { return InstructionFlags::ControlFlow; }

    // Faithful to the C# NullableRewrap.ComputeFlags: strip the Argument's
    // MayUnwrapNull (the unwrap's null branch is handled here, not a real
    // branch past the rewrap) and EndPointUnreachable (the rewrap's endpoint
    // is reachable through the implicit nullable-unwrap branch), then add
    // ControlFlow.
    InstructionFlags Flags() const override {
        using IF = InstructionFlags;
        const IF remove = IF::MayUnwrapNull | IF::EndPointUnreachable;
        IF arg = Argument ? Argument->Flags() : IF::None;
        return (arg & ~remove) | IF::ControlFlow;
    }

    // Faithful to the C# NullableRewrap.ResultType: Void for a void Argument
    // (a `?.` statement), else O (the nullable result).
    StackType ResultType() const override {
        if (Argument && Argument->ResultType() == StackType::Void) return StackType::Void;
        return StackType::O;
    }

    void WriteTo(std::string& out) const override {
        out += "nullable.rewrap(";
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// NullableUnwrap: the `?.` dereference. UnaryInstruction; the result is the
// unwrapped type (set by the constructor -- the underlying type for a
// Nullable<T>, the reference type itself for a reference type, or Ref for the
// RefOutput generic case). Carries MayUnwrapNull (directly in DirectFlags and
// via the Flags() override) so the surrounding NullableRewrap can find it.
// Port of the generated NullableUnwrap (UnaryInstruction, DirectFlags
// MayUnwrapNull, ComputeFlags base | MayUnwrapNull) + the hand-written
// NullableInstructions.cs NullableUnwrap (RefInput, ResultType, WriteToCore).
class NullableUnwrap : public UnaryInstruction {
public:
    // The unwrapped result type (set by the constructor). For RefOutput this
    // is Ref (the input reference is returned, not the value).
    StackType ResultTypeField = StackType::Unknown;
    // Whether the Argument is a managed reference to the nullable (the C#
    // compiler sometimes avoids copying the whole Nullable<T> struct before
    // the null-check). When true the Argument's ResultType is Ref; else O.
    bool RefInput = false;

    NullableUnwrap(StackType unwrappedType, std::unique_ptr<ILInstruction> argument,
                   bool refInput = false)
        : UnaryInstruction(OpCode::NullableUnwrap, std::move(argument)),
          ResultTypeField(unwrappedType), RefInput(refInput) {}

    // Faithful to the C# NullableUnwrap.DirectFlags: base.DirectFlags
    // (UnaryInstruction None) | MayUnwrapNull.
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None | InstructionFlags::MayUnwrapNull;
    }

    // Faithful to the C# NullableUnwrap.ComputeFlags: base.ComputeFlags |
    // MayUnwrapNull (base.ComputeFlags is the union of the Argument's flags,
    // so the unwrap's MayUnwrapNull propagates up to the surrounding rewrap).
    InstructionFlags Flags() const override {
        return UnaryInstruction::Flags() | InstructionFlags::MayUnwrapNull;
    }

    // The unwrapped type (the constructor-set field), faithful to the C#
    // NullableUnwrap.ResultType.
    StackType ResultType() const override { return ResultTypeField; }

    // Faithful to the C# NullableUnwrap.RefOutput (ResultType == Ref).
    bool RefOutput() const { return ResultTypeField == StackType::Ref; }

    void WriteTo(std::string& out) const override {
        out += "nullable.unwrap.";
        if (RefInput) out += "refinput.";
        out += StackTypeName(ResultTypeField);
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
