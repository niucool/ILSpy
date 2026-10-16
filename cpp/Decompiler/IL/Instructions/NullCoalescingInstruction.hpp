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

// NullCoalescingInstruction: the ILAst node for the C# `??` (null-coalescing)
// operator. Faithful to the generated NullCoalescingInstruction in
// ICSharpCode.Decompiler/IL/Instructions.cs and the hand-written
// NullCoalescingInstruction.cs. The node evaluates ValueInst once and, when it
// is null (a reference type) or lacks a value (a Nullable<T>), evaluates
// FallbackInst; the result is the non-null value or the fallback. Two children:
// ValueInst (slot 0, inlineable -- the value tested for null) and FallbackInst
// (slot 1 -- the alternative). DirectFlags = ControlFlow; ResultType =
// FallbackInst.ResultType (the C# returns fallbackInst.ResultType).
//
// Kind distinguishes the three semantics the C# back end lowers differently:
//   Ref                     -- both sides are reference types (`a ?? b`)
//   Nullable                -- both sides are Nullable<T> (`a ?? b` keeps the
//                              nullable type; valueInst.HasValue is the test)
//   NullableWithValueFallback -- ValueInst is Nullable<T>, FallbackInst is a
//                              non-nullable value type (`a ?? b` unwraps via
//                              valueInst.Value; the fallback's type is the
//                              result). This is the kind the
//                              `Nullable<T>.GetValueOrDefault(value, fallback)`
//                              -> `??` fold in ExpressionTransforms.VisitCall
//                              produces.
//
// UnderlyingResultType is the stack type the C# back end uses to recover the
// result type when Kind == Nullable (the result is a Nullable<U> of that
// underlying type); it defaults to O and is otherwise the fallback's type. The
// C# CheckInvariant also asserts ValueInst.ResultType == O and the
// Kind-dependent ResultType relation; this port's CheckInvariant is non-virtual
// and checks tree consistency only (the other ported nodes skip their C#
// per-node asserts the same way), so those are not enforced here. Per decision
// D1 the generated *output* is the source of truth.
//
// This is a tested-but-not-yet-wired foundation (like MatchInstruction /
// UsingInstruction / the NullableLifting helpers): no pipeline transform
// constructs it yet. The next in-order consumer is ExpressionTransforms.VisitCall
// (the `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold, which needs the
// 2-arg MatchGetValueOrDefault helper + SemanticHelper.IsPure), and the later
// NullCoalescingTransform (a separate StatementTransform child that builds
// NullCoalescingInstructions from `if.notnull` block tails); this node is the
// prerequisite both need.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// Kind of null-coalescing operator. Port of NullCoalescingKind in
// NullCoalescingInstruction.cs. The C# back end lowers each kind differently
// (Ref: `a != null ? a : b`; Nullable: `a.HasValue ? a : b`;
// NullableWithValueFallback: `a.HasValue ? a.Value : b`).
enum class NullCoalescingKind : std::uint8_t {
    // Both ValueInst and FallbackInst are reference types.
    Ref,
    // Both ValueInst and FallbackInst are Nullable<T>.
    Nullable,
    // ValueInst is Nullable<T>, FallbackInst is a non-nullable value type.
    NullableWithValueFallback,
};

class NullCoalescingInstruction : public ILInstruction {
public:
    // The coalescing semantics (see NullCoalescingKind). Faithful to the C#
    // readonly Kind field.
    NullCoalescingKind Kind = NullCoalescingKind::Ref;
    // The stack type the C# back end uses to recover the result type when
    // Kind == Nullable (the result is a Nullable<U> of this underlying type).
    // Defaults to O; otherwise set to the fallback's ResultType. Faithful to
    // the C# `public StackType UnderlyingResultType = StackType.O;` field.
    StackType UnderlyingResultType = StackType::O;

    std::unique_ptr<ILInstruction> ValueInst;    // slot 0, inlineable
    std::unique_ptr<ILInstruction> FallbackInst;  // slot 1

    NullCoalescingInstruction(NullCoalescingKind kind,
                               std::unique_ptr<ILInstruction> valueInst,
                               std::unique_ptr<ILInstruction> fallbackInst)
        : ILInstruction(OpCode::NullCoalescingInstruction),
          Kind(kind),
          ValueInst(std::move(valueInst)),
          FallbackInst(std::move(fallbackInst)) {
        if (ValueInst) { ValueInst->Parent = this; ValueInst->ChildIndex = 0; }
        if (FallbackInst) { FallbackInst->Parent = this; FallbackInst->ChildIndex = 1; }
    }

    InstructionFlags DirectFlags() const override { return InstructionFlags::ControlFlow; }
    // Faithful to the C# ComputeFlags: valueInst is always executed, fallbackInst
    // only sometimes (the branch endpoint is the join), so the fallback's flags
    // are combined via CombineBranches(None, ..) -- the None side is the
    // unreachable "value was null but we took the value anyway" path.
    InstructionFlags Flags() const override {
        InstructionFlags v = ValueInst ? ValueInst->Flags() : InstructionFlags::None;
        InstructionFlags f = FallbackInst ? FallbackInst->Flags() : InstructionFlags::None;
        return InstructionFlags::ControlFlow | v | CombineBranches(InstructionFlags::None, f);
    }
    // The C# returns fallbackInst.ResultType (the result is the fallback when
    // the value is null; when the value is non-null it has the same type for
    // Ref, or the underlying type for the nullable kinds). The CheckInvariant
    // asserts the Kind == Nullable case separately; the getter is faithful.
    StackType ResultType() const override {
        return FallbackInst ? FallbackInst->ResultType() : UnderlyingResultType;
    }

    int ChildCount() const override {
        return (ValueInst ? 1 : 0) + (FallbackInst ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return ValueInst.get();
        if (i == 1) return FallbackInst.get();
        return nullptr;
    }

    void WriteTo(std::string& out) const override {
        // Faithful to the C# WriteToCore: `if.notnull(value, fallback)`. The C#
        // does not render the Kind in the dump; this port annotates the
        // non-default kinds (Nullable / NullableWithValueFallback) as a suffix
        // so the semantic flavour is visible (matching the MatchInstruction /
        // UsingInstruction convention of surfacing flags in the dump). The
        // default Ref kind renders bare, exactly as the C# does.
        out += "if.notnull";
        if (Kind == NullCoalescingKind::Nullable) out += ".nullable";
        else if (Kind == NullCoalescingKind::NullableWithValueFallback) out += ".value";
        out += '(';
        if (ValueInst) ValueInst->WriteTo(out); else out += "(null)";
        out += ", ";
        if (FallbackInst) FallbackInst->WriteTo(out); else out += "(null)";
        out += ')';
    }

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        assert(i >= 0 && i <= 1);
        if (i == 0) { auto old = std::move(ValueInst); ValueInst = std::move(n); return old; }
        auto old = std::move(FallbackInst); FallbackInst = std::move(n); return old;
    }
};

} // namespace ILSpy::Decompiler::IL
