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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// UserDefinedLogicOperator: the ILAst node for a user-defined short-circuiting
// `&&` / `||` operator (the C# `op_BitwiseAnd` / `op_BitwiseOr` overloads paired
// with `op_True` / `op_False`). Faithful to the generated
// UserDefinedLogicOperator in ICSharpCode.Decompiler/IL/Instructions.cs and
// the hand-written LogicInstructions.cs.
//
// The C# node is `ILInstruction, IInstructionWithMethodOperand` with Left
// (slot 0, canInlineInto) + Right (slot 1) children and a readonly IMethod
// operand; ResultType is always StackType.O. This port models the IMethod as
// the resolved method name (std::string) + declaring type (ITypePtr), the same
// stand-in UserDefinedCompoundAssign (D135) and Call use (this port has no
// IMethod). The Left/Right children are provided by the BinaryInstruction base
// (the ThreeValuedBoolAnd/Or D95 precedent); the C# Right slot is not
// canInlineInto, but this port does not model per-slot canInlineInto (the D95
// note documents the same divergence for ThreeValuedBoolAnd/Or), so both are
// inlineable here.
//
// DirectFlags is MayThrow | SideEffect | ControlFlow (the C# generated
// override -- a user-defined operator call can throw and the short-circuiting
// is control flow). The Flags() override ports the C# ComputeFlags: the left
// operand is always executed, the right only sometimes (short-circuit), so the
// right operand's flags combine via CombineBranches(None, right.Flags) -- the
// None side is the unreachable "short-circuited, did not evaluate the right"
// path (the NullCoalescingInstruction D88 / IfInstruction CombineBranches
// precedent).
//
// This is a tested-but-not-yet-wired foundation (the NullCoalescingInstruction
// / MatchInstruction / UsingInstruction / NumericCompoundAssign /
// UserDefinedCompoundAssign precedent): no pipeline transform constructs this
// node yet. The next in-order consumer is UserDefinedLogicTransform (the
// `LegacyPattern` / `RoslynOptimized` folds of the C# 7 user-defined
// short-circuiting logic operator), which needs this node plus the
// MatchCondition / MatchBitwiseCall helpers and the block-model adaptation for
// the if-as-final shape; this header is the prerequisite that transform needs.
// The OpCode::UserDefinedLogicOperator value was pre-declared in OpCode.hpp
// (line 109), so porting the node needed only the subclass header (the
// ThreeValuedBoolAnd/Or D95 / NumericCompoundAssign D125 precedent).

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// A user-defined short-circuiting logic operator.
// `user.logic Method(left, right)`; result is O (the operator's return type,
// a reference type on the eval stack). Port of the generated
// UserDefinedLogicOperator (ILInstruction, IInstructionWithMethodOperand,
// ResultType O, Left inlineable + Right).
class UserDefinedLogicOperator : public BinaryInstruction {
public:
    // The resolved method name ("Namespace.Type::op_BitwiseAnd" /
    // "op_BitwiseOr"), the faithful stand-in for the C# `IMethod` (this port
    // models a method by its resolved name + declaring type, like Call and
    // UserDefinedCompoundAssign). The seed and a future
    // UserDefinedLogicTransform consult the part after "::" to derive the C#
    // operator (op_BitwiseAnd -> `&&`, op_BitwiseOr -> `||`).
    std::string MethodName;
    // The resolved declaring type of the method (the C# `Method.DeclaringType`).
    // Carried so a future UserDefinedLogicTransform can consult it without a
    // MetadataFile handle (the pre-resolve-metadata-at-reader-time pattern).
    TypeSystem::ITypePtr MethodDeclaringType;

    UserDefinedLogicOperator(std::string methodName,
                             TypeSystem::ITypePtr methodDeclaringType,
                             std::unique_ptr<ILInstruction> left,
                             std::unique_ptr<ILInstruction> right)
        : BinaryInstruction(OpCode::UserDefinedLogicOperator,
                            std::move(left), std::move(right)),
          MethodName(std::move(methodName)),
          MethodDeclaringType(std::move(methodDeclaringType)) {}

    // Faithful to the C# `public override StackType ResultType => StackType.O`.
    // A user-defined `&&` / `||` evaluates to the operand type (a reference
    // type on the eval stack), so the result is always O.
    StackType ResultType() const override { return StackType::O; }

    // Faithful to the C# generated DirectFlags: a user-defined operator call
    // can throw (MayThrow), has a side effect (SideEffect), and short-circuits
    // (ControlFlow).
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow
             | InstructionFlags::SideEffect
             | InstructionFlags::ControlFlow;
    }
    // Faithful to the C# ComputeFlags: the left operand is always executed,
    // the right only sometimes (short-circuit), so the right operand's flags
    // combine via CombineBranches(None, ..) -- the None side is the
    // unreachable "short-circuited, did not evaluate the right" path (the
    // NullCoalescingInstruction D88 / IfInstruction CombineBranches precedent).
    InstructionFlags Flags() const override {
        InstructionFlags l = Left ? Left->Flags() : InstructionFlags::None;
        InstructionFlags r = Right ? Right->Flags() : InstructionFlags::None;
        return DirectFlags() | l | CombineBranches(InstructionFlags::None, r);
    }

    void WriteTo(std::string& out) const override {
        // Faithful to the C# WriteToCore: the OpCode + the method + the two
        // operands. The C# writes the OpCode name "UserDefinedLogicOperator";
        // this port uses the readable short mnemonic `user.logic` (the
        // ThreeValuedBoolAnd/Or `3vl.bool.and` / compound-assign
        // `compound.assign.userdefined` precedent) so the family is
        // distinguishable in the ILAst dump.
        out += "user.logic ";
        out += MethodName;
        out += '(';
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
