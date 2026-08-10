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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// CompoundAssignmentInstruction: the ILAst node family for C# compound
// assignment operators (`+=`, `-=`, `++`, ...). Faithful to the generated
// CompoundAssignmentInstruction in ICSharpCode.Decompiler/IL/Instructions.cs
// and the hand-written CompoundAssignmentInstruction.cs. The base carries the
// two children Target (slot 0, inlineable -- the store target, an address or a
// property-getter call) and Value (slot 1, inlineable -- the RHS), plus the
// EvalMode (whether the expression evaluates to the old or the new value) and
// the TargetKind (Address / Property / Dynamic -- how the Target is
// interpreted). DirectFlags = None; ComputeFlags = Target.Flags | Value.Flags
// (the base; NumericCompoundAssign adds SideEffect + MayThrow).
//
// NumericCompoundAssign is the numeric/relational compound assignment
// (`target op= value`, e.g. `i += 1`). It carries the BinaryNumericOperator,
// CheckForOverflow, Sign, LeftInputType / RightInputType, UnderlyingResultType,
// IsLifted (the ILiftableInstruction impl), and the Type operand (the type of
// the store -- the C# `public IType Type`). ResultType = IsLifted ? O :
// UnderlyingResultType (a lifted compound assign operates on boxed Nullable<T>
// and produces O). DirectFlags = SideEffect (| MayThrow when CheckForOverflow
// or the operator is Div/Rem, faithful to the C# ComputeFlags/DirectFlags).
//
// The C# NumericCompoundAssign constructor copies Operator/Sign/LeftInputType/
// RightInputType/UnderlyingResultType/IsLifted from a BinaryNumericInstruction.
// This port's BinaryNumericInstruction carries Operator/Signed(bool)/
// CheckForOverflow/ResultStackType/IsLifted but not the Sign enum or the
// per-operand input types (it carries a Signed bool, not the C# Sign, and
// derives the result type, not the per-operand input types). The node therefore
// takes its fields explicitly (the D48 lifted-BNI precedent): the foundation
// tests construct it directly, and the future TransformAssignment.
// HandleCompoundAssign transform will build it from a BinaryNumericInstruction
// (approximating Sign from the binary's Signed bool + operator, and the input
// types from the operands' ResultType) -- a BNI Sign/input-type reconciliation
// that is a separate, contained model change deferred to that transform.
//
// The C# CheckValidTarget asserts the Target's ResultType/Op for each
// TargetKind (Address -> Ref/I; Property -> a Call; Dynamic -> a Dynamic*
// instruction). This port's CheckInvariant is non-virtual and checks tree
// consistency only (the other ported nodes skip their C# per-node asserts the
// same way), so those are not enforced here. Per decision D1 the generated
// *output* is the source of truth.
//
// This is a tested-but-not-yet-wired foundation (the MatchInstruction /
// UsingInstruction / NullCoalescingInstruction / ThreeValuedBoolAnd/Or
// precedent): no pipeline transform constructs a NumericCompoundAssign yet, so
// `--csharp` output is unchanged (the seed already renders `V op= expr` from
// the StLoc pattern at the text level; this node is the ILAst-level
// representation the future TransformAssignment.HandleCompoundAssign produces
// for the real back end). The OpCode::NumericCompoundAssign /
// OpCode::UserDefinedCompoundAssign / OpCode::DynamicCompoundAssign values
// were pre-declared in OpCode.hpp, so porting the node needed only the
// subclass header. The MakeAssignmentExpressions + IntroduceIncrementAndDecrement
// settings (DecompilerSettings, both default true) gate the transform that
// builds these nodes and are added ahead of it.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <cassert>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

// Forward declaration for the IsBinaryCompatibleWithType settings parameter
// (ILTransformSettings is defined in Transforms/IILTransform.hpp; the node
// header avoids including it to keep the include graph lean).
struct ILTransformSettings;

// Forward declaration of Call: the UserDefinedCompoundAssign static helpers
// (IsIncrementOrDecrement / IsStringConcat) take the operator Call the
// TransformAssignment folds build the node from. Call.hpp is not included here
// to keep the node header's include graph lean; the helpers are implemented
// out-of-line in CompoundAssignmentInstruction.cpp (which includes Call.hpp).
class Call;

// Whether a compound.assign evaluates to the old (post-increment/decrement) or
// the new (compound assignment / pre-increment) value. Faithful to the C#
// CompoundEvalMode enum.
enum class CompoundEvalMode : std::uint8_t {
	EvaluatesToOldValue,
	EvaluatesToNewValue,
};

// How the Target child is interpreted. Faithful to the C# CompoundTargetKind.
enum class CompoundTargetKind : std::uint8_t {
	// The Target evaluates to an address; the compound.assign implicitly
	// loads/stores from/to that address (a ldloca / ldflda / ldobj target).
	Address,
	// The Target is a call to a property getter; the compound.assign implicitly
	// calls the corresponding property setter.
	Property,
	// The Target is a dynamic call.
	Dynamic,
};

// The abstract base for the compound-assignment family (the C#
// CompoundAssignmentInstruction). Two children: Target (slot 0, inlineable)
// and Value (slot 1, inlineable); plus EvalMode and TargetKind. DirectFlags =
// None (faithful); the base ComputeFlags is Target.Flags | Value.Flags
// (subclasses add SideEffect/MayThrow). ResultType is subclass-defined
// (NumericCompoundAssign returns IsLifted ? O : UnderlyingResultType).
class CompoundAssignmentInstruction : public ILInstruction {
public:
	CompoundEvalMode EvalMode = CompoundEvalMode::EvaluatesToNewValue;
	CompoundTargetKind TargetKind = CompoundTargetKind::Address;

	std::unique_ptr<ILInstruction> Target;  // slot 0, inlineable
	std::unique_ptr<ILInstruction> Value;   // slot 1, inlineable

	CompoundAssignmentInstruction(OpCode op,
	                              CompoundEvalMode evalMode,
	                              std::unique_ptr<ILInstruction> target,
	                              CompoundTargetKind targetKind,
	                              std::unique_ptr<ILInstruction> value)
		: ILInstruction(op),
		  EvalMode(evalMode),
		  TargetKind(targetKind),
		  Target(std::move(target)),
		  Value(std::move(value)) {
		if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
		if (Value)  { Value->Parent = this;  Value->ChildIndex = 1; }
	}

	InstructionFlags DirectFlags() const override { return InstructionFlags::None; }

	int ChildCount() const override {
		return (Target ? 1 : 0) + (Value ? 1 : 0);
	}
	ILInstruction* GetChild(int i) const override {
		if (i == 0) return Target.get();
		if (i == 1) return Value.get();
		return nullptr;
	}

protected:
	std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
		assert(i >= 0 && i <= 1);
		if (i == 0) { auto old = std::move(Target); Target = std::move(n); return old; }
		auto old = std::move(Value); Value = std::move(n); return old;
	}
};

// NumericCompoundAssign: the numeric/relational compound assignment
// (`target op= value`). Faithful to the C# NumericCompoundAssign
// (CompoundAssignmentInstruction + ILiftableInstruction). Carries the
// BinaryNumericOperator, CheckForOverflow, Sign, LeftInputType /
// RightInputType, UnderlyingResultType, IsLifted, and the Type operand (the
// store type). ResultType = IsLifted ? O : UnderlyingResultType (a lifted
// compound assign operates on boxed Nullable<T> and produces O).
class NumericCompoundAssign : public CompoundAssignmentInstruction {
public:
	BinaryNumericOperator Operator = BinaryNumericOperator::Add;
	bool CheckForOverflow = false;
	TypeSystem::Sign Sign = TypeSystem::Sign::None;
	StackType LeftInputType = StackType::I4;
	StackType RightInputType = StackType::I4;
	// Faithful to the C# `public StackType UnderlyingResultType { get; }`
	// (ILiftableInstruction): the underlying (non-lifted) result stack type.
	StackType UnderlyingResultTypeField = StackType::I4;
	bool IsLifted = false;
	// The type operand (the type of the store). Faithful to the C#
	// `public IType Type`. Defaults to null; set by the transform that builds the
	// node. ResultType falls back to UnderlyingResultType when null (the
	// Type's GetStackType is only consulted by the real back end).
	TypeSystem::ITypePtr Type;

	// Constructor taking the fields explicitly (the D48 lifted-BNI precedent):
	// the foundation tests construct the node directly, and the future
	// TransformAssignment.HandleCompoundAssign transform will build it from a
	// BinaryNumericInstruction (approximating Sign from the binary's Signed
	// bool + operator, and the input types from the operands' ResultType). The
	// C# constructor copies these from a BinaryNumericInstruction; this port's
	// BNI does not carry Sign / LeftInputType / RightInputType, so they are taken
	// explicitly here.
	NumericCompoundAssign(BinaryNumericOperator op,
	                      bool checkForOverflow,
	                      TypeSystem::Sign sign,
	                      StackType leftInputType,
	                      StackType rightInputType,
	                      StackType underlyingResultType,
	                      bool isLifted,
	                      TypeSystem::ITypePtr type,
	                      CompoundEvalMode evalMode,
	                      std::unique_ptr<ILInstruction> target,
	                      CompoundTargetKind targetKind,
	                      std::unique_ptr<ILInstruction> value)
		: CompoundAssignmentInstruction(OpCode::NumericCompoundAssign, evalMode,
			std::move(target), targetKind, std::move(value)),
		  Operator(op), CheckForOverflow(checkForOverflow), Sign(sign),
		  LeftInputType(leftInputType), RightInputType(rightInputType),
		  UnderlyingResultTypeField(underlyingResultType), IsLifted(isLifted),
		  Type(std::move(type)) {}

	InstructionFlags DirectFlags() const override {
		InstructionFlags f = InstructionFlags::SideEffect;
		if (CheckForOverflow || Operator == BinaryNumericOperator::Div || Operator == BinaryNumericOperator::Rem)
			f = f | InstructionFlags::MayThrow;
		return f;
	}
	// Faithful to the C# ComputeFlags: Target.Flags | Value.Flags | SideEffect
	// (+ MayThrow for div/rem/ovf).
	InstructionFlags Flags() const override {
		InstructionFlags f = (Target ? Target->Flags() : InstructionFlags::None)
			| (Value ? Value->Flags() : InstructionFlags::None)
			| InstructionFlags::SideEffect;
		if (CheckForOverflow || Operator == BinaryNumericOperator::Div || Operator == BinaryNumericOperator::Rem)
			f = f | InstructionFlags::MayThrow;
		return f;
	}
	// Faithful to the C# ResultType: O for a lifted compound assign (a boxed
	// Nullable<T>); the underlying result type otherwise.
	StackType ResultType() const override {
		return IsLifted ? StackType::O : UnderlyingResultTypeField;
	}
	// Faithful to the C# UnderlyingResultType (ILiftableInstruction).
	StackType UnderlyingResultType() const { return UnderlyingResultTypeField; }

	// Port of NumericCompoundAssign.IsBinaryCompatibleWithType: whether the
	// specific binary instruction is compatible with a compound operation on
	// the specified type. The gate the future TransformAssignment.
	// HandleCompoundAssign consults before building a NumericCompoundAssign from
	// a `stloc V(binary.op(ldloc V, rhs))` pattern. The settings parameter is
	// nullable (matching the C# `DecompilerSettings?`); a null settings pointer
	// is treated as the defaults (NativeIntegers/UnsignedRightShift permissive),
	// matching the C# Debug.Assert call from the constructor.
	//
	// The Pointer case is deferred: the C# consults PointerArithmeticOffset.
	// Detect (needs the SizeOf node + ComputeSizeOf + UnwrapConv +
	// NormalizeTypeVisitor.TypeErasure.EquivalentTypes -- a substantial deferred
	// slice), so the port returns false conservatively for pointer types (no
	// pointer compound assignment is confirmed). This is behavior-preserving for
	// the .NET Framework 4 mscorlib corpus (pointer arithmetic requires `unsafe`,
	// absent from C#-compiled code); the hand-built tests verify the non-Pointer
	// gates and the conservative pointer reject.
	static bool IsBinaryCompatibleWithType(const BinaryNumericInstruction* binary,
	                                       const TypeSystem::IType* type,
	                                       const ILTransformSettings* settings);

	void WriteTo(std::string& out) const override {
		// Faithful to the C# WriteToCore:
		// `compound.assign.<operator>[.ovf][.unsigned|.signed].<type>[.lifted].<suffix>(target, value)`
		// where suffix is `.address`/`.property` (TargetKind) + `.new`/`.old`
		// (EvalMode).
		out += "compound.assign.";
		switch (Operator) {
			case BinaryNumericOperator::Add: out += "add"; break;
			case BinaryNumericOperator::Sub: out += "sub"; break;
			case BinaryNumericOperator::Mul: out += "mul"; break;
			case BinaryNumericOperator::Div: out += "div"; break;
			case BinaryNumericOperator::Rem: out += "rem"; break;
			case BinaryNumericOperator::BitAnd: out += "bit.and"; break;
			case BinaryNumericOperator::BitOr: out += "bit.or"; break;
			case BinaryNumericOperator::BitXor: out += "bit.xor"; break;
			case BinaryNumericOperator::ShiftLeft: out += "bit.shl"; break;
			case BinaryNumericOperator::ShiftRight: out += "bit.shr"; break;
			default: out += "?"; break;
		}
		if (CheckForOverflow) out += ".ovf";
		if (Sign == TypeSystem::Sign::Unsigned) out += ".unsigned";
		else if (Sign == TypeSystem::Sign::Signed) out += ".signed";
		out += '.';
		out += StackTypeName(UnderlyingResultTypeField);
		if (IsLifted) out += ".lifted";
		if (TargetKind == CompoundTargetKind::Address) out += ".address";
		else if (TargetKind == CompoundTargetKind::Property) out += ".property";
		if (EvalMode == CompoundEvalMode::EvaluatesToNewValue) out += ".new";
		else out += ".old";
		out += '(';
		if (Target) Target->WriteTo(out); else out += "(null)";
		out += ", ";
		if (Value) Value->WriteTo(out); else out += "(null)";
		out += ')';
	}

private:
	// Faithful to the C# UnderlyingResultType.ToString().ToLowerInvariant()
	// (the ILAst dump appends the result stack type's name in lowercase).
	static const char* StackTypeName(StackType s) {
		switch (s) {
			case StackType::I4: return "i4";
			case StackType::I: return "i";
			case StackType::I8: return "i8";
			case StackType::F4: return "r4";
			case StackType::F8: return "r8";
			case StackType::O: return "o";
			case StackType::Ref: return "ref";
			case StackType::Void: return "void";
			case StackType::Unknown: return "unknown";
		}
		return "?";
	}
};

// UserDefinedCompoundAssign: a compound assignment built from a user-defined
// operator call (`target op= value` where op is a user-defined operator, or
// `target++`/`++target` for op_Increment/op_Decrement). Faithful to the C#
// UserDefinedCompoundAssign (CompoundAssignmentInstruction; the generated node
// overrides ComputeFlags/DirectFlags to add SideEffect|MayThrow). This port
// has no IMethod, so the C# `readonly IMethod Method` is modelled as the
// resolved method name + declaring type + the return StackType (Method.
// ReturnType.GetStackType() drives ResultType); the IsLifted flag is hardcoded
// false, faithful to the C# `public bool IsLifted => false; // TODO`.
//
// The IsIncrementOrDecrement / IsStringConcat static helpers are the gates the
// TransformAssignment folds consult on the operator Call BEFORE building the
// node (the C# takes the IMethod; this port takes the Call, which carries the
// method metadata -- IsOperator, IsInstanceCall, MethodName, DeclaringType).
// They are declared here (forward-declared Call) and implemented out-of-line in
// CompoundAssignmentInstruction.cpp.
//
// This is a tested-but-not-yet-wired foundation (the NumericCompoundAssign /
// MatchInstruction / UsingInstruction precedent): no pipeline transform
// constructs a UserDefinedCompoundAssign yet, so `--csharp` output is
// unchanged. The OpCode::UserDefinedCompoundAssign value was pre-declared in
// OpCode.hpp, so porting the node needed only the subclass header. The node
// unblocks the operator-call (op_Increment/op_Decrement) case of the
// TransformAssignment increment/decrement folds (D134's first deferred target).
class UserDefinedCompoundAssign : public CompoundAssignmentInstruction {
public:
	// The resolved method name ("Namespace.Type::Method"), the faithful
	// stand-in for the C# `IMethod` (this port models a method by its resolved
	// name + declaring type, like Call). The seed and the dump consult the part
	// after "::" to derive the C# operator.
	std::string MethodName;
	// The resolved declaring type of the method (the C# `Method.DeclaringType`).
	// Carried so the IsStringConcat helper and a future HandleCompoundAssign
	// string.Concat case can consult it without a MetadataFile handle.
	TypeSystem::ITypePtr MethodDeclaringType;
	// The method's return StackType (the C# `Method.ReturnType.GetStackType()`).
	// Drives ResultType -- a user-defined compound assign evaluates to the
	// operator's return type (e.g. op_Increment returns the type, so `target++`
	// evaluates to that type).
	StackType MethodReturnType = StackType::Unknown;
	// Faithful to the C# `public bool IsLifted => false; // TODO: implement
	// lifted user-defined compound assignments`. A settable field so a future
	// resolver-backed path can mark a lifted user-defined operator.
	bool IsLifted = false;

	UserDefinedCompoundAssign(std::string methodName,
	                          TypeSystem::ITypePtr methodDeclaringType,
	                          StackType methodReturnType,
	                          CompoundEvalMode evalMode,
	                          std::unique_ptr<ILInstruction> target,
	                          CompoundTargetKind targetKind,
	                          std::unique_ptr<ILInstruction> value)
		: CompoundAssignmentInstruction(OpCode::UserDefinedCompoundAssign, evalMode,
			std::move(target), targetKind, std::move(value)),
		  MethodName(std::move(methodName)),
		  MethodDeclaringType(std::move(methodDeclaringType)),
		  MethodReturnType(methodReturnType) {}

	// Faithful to the C# ComputeFlags/DirectFlags: base (Target.Flags |
	// Value.Flags) | SideEffect | MayThrow (a user-defined operator call can
	// throw).
	InstructionFlags DirectFlags() const override {
		return InstructionFlags::SideEffect | InstructionFlags::MayThrow;
	}
	InstructionFlags Flags() const override {
		return (Target ? Target->Flags() : InstructionFlags::None)
			| (Value ? Value->Flags() : InstructionFlags::None)
			| InstructionFlags::SideEffect | InstructionFlags::MayThrow;
	}
	// Faithful to the C# `public override StackType ResultType => Method.
	// ReturnType.GetStackType()`.
	StackType ResultType() const override { return MethodReturnType; }

	// Faithful port of UserDefinedCompoundAssign.IsIncrementOrDecrement: the
	// operator Call is a static operator overload named op_Increment /
	// op_Decrement (always), or op_CheckedIncrement / op_CheckedDecrement (only
	// when the C# 11.0 CheckedOperators setting is on, matching the C#
	// `settings?.CheckedOperators ?? true`). The C# takes the IMethod; this
	// port takes the Call (carrying IsOperator / IsInstanceCall / MethodName).
	// Returns false for a non-operator, an instance method, or an unrelated
	// operator name.
	static bool IsIncrementOrDecrement(const Call* call, const ILTransformSettings* settings);

	// Faithful port of UserDefinedCompoundAssign.IsStringConcat: the operator
	// Call is a static `string.Concat` (the C# compound-assign lowering of
	// `s += "..."` rewrites the Concat call into a UserDefinedCompoundAssign
	// that renders as `s += value`). Recognised by the method name "Concat" +
	// a static call + a System.String declaring type.
	static bool IsStringConcat(const Call* call);

	void WriteTo(std::string& out) const override {
		// Faithful to the C# WriteToCore: `OpCode` + WriteSuffix (the C#
		// `user.compound` root + `.address`/`.property` + `.new`/`.old`) then
		// the method and the Target/Value operands. This port renders the
		// `compound.assign` family root (the NumericCompoundAssign precedent)
		// with a `.userdefined` marker so the family is consistent and the
		// kind is distinguishable in the ILAst dump.
		out += "compound.assign.userdefined";
		if (TargetKind == CompoundTargetKind::Address) out += ".address";
		else if (TargetKind == CompoundTargetKind::Property) out += ".property";
		if (EvalMode == CompoundEvalMode::EvaluatesToNewValue) out += ".new";
		else out += ".old";
		out += ' ';
		out += MethodName;
		out += '(';
		if (Target) Target->WriteTo(out); else out += "(null)";
		out += ", ";
		if (Value) Value->WriteTo(out); else out += "(null)";
		out += ')';
	}
};

} // namespace ILSpy::Decompiler::IL
