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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// ThreeValuedBoolAnd / ThreeValuedBoolOr: the ILAst nodes for the three-valued
// logic `&` and `|` on `bool?` (Nullable<bool>). Faithful to the generated
// ThreeValuedBoolAnd / ThreeValuedBoolOr in ICSharpCode.Decompiler/IL/
// Instructions.cs and the hand-written LogicInstructions.cs.
//
// Both are BinaryInstruction (Left + Right children, both inlineable). The
// inputs are `bool?` or I4; the output is `bool?` (StackType::O, a boxed
// Nullable<bool> on the eval stack). Unlike logic.and()/logic.or() these do
// NOT short-circuit: both operands always evaluate. They arise from the C#
// `&` / `|` operators on `bool?` (e.g. `a & b` where a, b are bool?), which
// Roslyn lowers without short-circuiting because the three-valued truth
// tables require evaluating both sides to detect a `null` result.
//
// Both implement ILiftableInstruction (IsLifted = true, UnderlyingResultType =
// I4). This port has no ILiftableInstruction interface; the IsLifted() /
// UnderlyingResultType() methods are added directly (the Comp precedent), so a
// future lift consumer (the `&` / `|` on bool? path of
// NullableLiftingTransform.Run(IfInstruction), or DoLift) can recognise them.
//
// DirectFlags is None (inherited from BinaryInstruction); the base Flags()
// (DirectFlags | union of children = None | Left | Right) equals the C#
// ComputeFlags (left.Flags | right.Flags | None), so no Flags() override is
// needed. The C# CheckInvariant also asserts Left.ResultType is I4 or O; this
// port's CheckInvariant is non-virtual and checks tree consistency only (the
// other ported nodes skip their C# per-node asserts the same way), so that is
// not enforced here. Per decision D1 the generated *output* is the source of
// truth.
//
// This is a tested-but-not-yet-wired foundation (the NullCoalescingInstruction /
// MatchInstruction / UsingInstruction / NullableLifting-helpers precedent): no
// pipeline transform constructs these nodes yet. The next in-order consumer is
// the `&` / `|` on bool? path of NullableLiftingTransform.Run(IfInstruction)
// (the section of `Lift` after the bool? equality folds), which needs these
// nodes plus MatchLogicOr / MatchLogicAnd and MatchThreeValuedLogicCondition
// Pattern; this header is the prerequisite that path needs.

#pragma once

#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <string>

namespace ILSpy::Decompiler::IL {

// Three-valued logic AND. `3vl.bool.and(left, right)`; result is bool? (O).
// Port of the generated ThreeValuedBoolAnd (BinaryInstruction, ResultType O,
// ILiftableInstruction IsLifted=true / UnderlyingResultType I4).
class ThreeValuedBoolAnd : public BinaryInstruction {
public:
    ThreeValuedBoolAnd(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right)
        : BinaryInstruction(OpCode::ThreeValuedBoolAnd, std::move(left), std::move(right)) {}

    // The result is always bool? (a boxed Nullable<bool> on the eval stack).
    StackType ResultType() const override { return StackType::O; }

    // Port of ILiftableInstruction.IsLifted: a ThreeValuedBoolAnd is always a
    // lifted operation (its result is the nullable bool?).
    bool IsLifted() const { return true; }
    // Port of ILiftableInstruction.UnderlyingResultType: the underlying (non-
    // nullable) result is a Boolean (I4 on the eval stack).
    StackType UnderlyingResultType() const { return StackType::I4; }

    void WriteTo(std::string& out) const override {
        out += "3vl.bool.and(";
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

// Three-valued logic OR. `3vl.bool.or(left, right)`; result is bool? (O).
// Port of the generated ThreeValuedBoolOr (BinaryInstruction, ResultType O,
// ILiftableInstruction IsLifted=true / UnderlyingResultType I4).
class ThreeValuedBoolOr : public BinaryInstruction {
public:
    ThreeValuedBoolOr(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right)
        : BinaryInstruction(OpCode::ThreeValuedBoolOr, std::move(left), std::move(right)) {}

    StackType ResultType() const override { return StackType::O; }

    bool IsLifted() const { return true; }
    StackType UnderlyingResultType() const { return StackType::I4; }

    void WriteTo(std::string& out) const override {
        out += "3vl.bool.or(";
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
