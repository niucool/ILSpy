// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformAssignment.cs
// (foundation subset). The C# TransformAssignment is an IStatementTransform (a
// child of the GetILTransforms() StatementTransform, after ILInlining /
// ExpressionTransforms / DynamicIsEventAssignmentTransform) that rewrites
// inline-assignment and compound-assignment patterns into the cleaner ILAst
// forms -- the NumericCompoundAssign (`V op= rhs`) / UserDefinedCompoundAssign
// nodes the D125 foundation ported, and the inline-assignment Block shape. Its
// Run() dispatches to TransformInlineAssignmentStObjOrCall /
// TransformInlineAssignmentLocal (gated on MakeAssignmentExpressions) and
// TransformPostIncDecOperatorWithInlineStore / TransformPostIncDecOperator /
// TransformPreIncDecOperatorWithInlineStore (gated on
// IntroduceIncrementAndDecrement), each of which consults the shared
// IsCompoundStore / IsMatchingCompoundLoad / UnwrapSmallIntegerConv /
// ValidateCompoundAssign helpers + RecombineVariables.
//
// This iteration ports the self-contained UnwrapSmallIntegerConv helper as a
// tested-but-not-yet-wired foundation, ahead of the full transform (the
// established MatchInstruction / UsingInstruction / NullCoalescingInstruction /
// NumericCompoundAssign precedent). The helper the compound-assignment folds
// consult to peel the compiler's `conv` truncation to a small integer that a
// compound assign to a small-integer local/field carries:
//   stloc V(conv.i1(binary.add(ldloc V, ldc.i4 1)))
// the `conv.i1` (a Truncate to a small-integer TargetType) is the implicit
// truncation C# performs on the small-integer store; UnwrapSmallIntegerConv
// returns the conv's Argument (the binary) so the transform can build the
// NumericCompoundAssign from the binary + record the conv for ValidateCompoundAssign.
//
// The full TransformAssignment (IsCompoundStore / IsMatchingCompoundLoad /
// ValidateCompoundAssign + the BNI Sign/input-type reconciliation + the
// RecombineVariables finalizeMatch + the per-statement Run wiring) is a larger
// slice and a subsequent iteration.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <functional>
#include <memory>

namespace ILSpy::Decompiler::TypeSystem { class IType; }

namespace ILSpy::Decompiler::IL {

class Conv;
class BinaryNumericInstruction;
class ILFunction;
class ILVariable;
struct ILTransformSettings;

// The CompoundTargetKind enum is defined in CompoundAssignmentInstruction.hpp;
// forward-declared here so this header need not pull in the node header (which
// brings in BinaryNumericInstruction.hpp and the ILiftableInstruction fields).
enum class CompoundTargetKind : std::uint8_t;

// The finalizeMatch callback the compound-assignment folds invoke after a
// successful IsMatchingCompoundLoad match to fix up minor mismatches (the C#
// `Action<ILTransformContext>`). The LdLoc/StLoc case sets it to collapse
// split-fragment variables via `ILFunction::RecombineVariables`; the caller
// passes the ILFunction (obtained via a Parent-chain walk in the per-statement
// Run, since the StatementTransformContext carries no function handle). A null
// callback means no fix-up is needed (the common same-variable case).
using CompoundFinalizeMatch = std::function<void(ILFunction&)>;

// Port of TransformAssignment.IsCompoundStore (StLoc case). For a `stloc V(...)`
// whose Variable is a Local/Parameter, sets `storeType` to V.Type and `value` to
// the stored value (a non-owning view -- it is a child of `inst`), and returns
// true. Returns false (with null outputs) for the StObj case (needs InferType for
// the target's real type, unwrapping a ByReferenceType/PointerType element type
// via IsCompatibleTypeForMemoryAccess) and the Call case (needs IsSameMember +
// IMethod for the property-setter gate) -- the deferred cases.
bool IsCompoundStore(ILInstruction* inst, TypeSystem::ITypePtr& storeType,
                     ILInstruction*& value);

// Port of TransformAssignment.IsMatchingCompoundLoad (LdLoc/StLoc case). Checks
// whether `load` and `store` both access the same store and can be combined into
// a compound assignment. For a load that is an LdLoc and a store that is an
// StLoc of the same variable (per the ILVariableEqualityComparer -- same object,
// or split fragments with the same Kind + Index), sets `target` to a fresh
// LdLoca of that variable, `targetKind` to Address, and `finalizeMatch` to a
// callback that collapses the split-fragment variables via
// `ILFunction::RecombineVariables(ldloc.Variable, stloc.Variable)` (a no-op when
// they are the same variable); returns true. `forbiddenVariable` rejects the
// match when the load/store uses it (some transforms effectively move a store,
// valid only if the variable is not in the load/store). Returns false (with
// null/zeroed outputs) for the LdObj/StObj case (needs
// IsDuplicatedAddressComputation + the previousInstruction) and the
// getter/setter case (needs IMethod/AccessorOwner) -- the deferred cases.
bool IsMatchingCompoundLoad(ILInstruction* load, ILInstruction* store,
                            std::unique_ptr<ILInstruction>& target,
                            CompoundTargetKind& targetKind,
                            CompoundFinalizeMatch& finalizeMatch,
                            const ILVariable* forbiddenVariable = nullptr);

// Port of TransformAssignment.ValidateCompoundAssign: the gate the compound-
// assignment folds (HandleCompoundAssign / TransformPostIncDecOperator* /
// TransformPreIncDecOperatorWithInlineStore) consult before building a
// NumericCompoundAssign. Calls `NumericCompoundAssign::IsBinaryCompatibleWithType`
// (D129); then, when a small-integer conv was unwrapped (`conv` != null), checks
// the conv's TargetType matches the (possibly sign-swapped) target type's
// PrimitiveType and the conv's CheckForOverflow matches the binary's. `conv`
// and `targetType` are non-owning views (`conv` may be null -- the no-conv case);
// `settings` may be null (treated as the defaults, matching the C# constructor
// Debug.Assert call).
bool ValidateCompoundAssign(const BinaryNumericInstruction* binary, const Conv* conv,
                            const TypeSystem::IType* targetType,
                            const ILTransformSettings* settings);

// Port of TransformAssignment.UnwrapSmallIntegerConv. For a compound assignment
// to a small integer, the compiler emits a `conv` (a Truncate to a
// small-integer TargetType) wrapping the binary; this peels that conv and
// reports it via `conv` so the caller can pass it to ValidateCompoundAssign.
// Returns `inst` unchanged (with `conv` set to nullptr) when `inst` is not such
// a conv -- the non-truncating / non-small-integer-target case. `inst` is a
// non-owning view (the caller holds the ownership); the returned pointer is
// either `inst` itself or `inst`'s Argument child (still owned by `inst`).
ILInstruction* UnwrapSmallIntegerConv(ILInstruction* inst, Conv*& conv);

// Port of TransformAssignment.ImplicitTruncationResult: whether a store to a
// small-integer type would implicitly truncate the value. ValuePreserved: the
// value fits without truncation; ValueChanged: the value is truncated;// ValueChangedDueToSignMismatch: the value is truncated only because the target
// sign is wrong (the caller can fix it by flipping the target's sign).
enum class ImplicitTruncationResult : std::uint8_t {
	ValuePreserved,
	ValueChanged,
	ValueChangedDueToSignMismatch,
};

// Port of TransformAssignment.CheckImplicitTruncation: whether `stobj type(...,
// value)` would evaluate to a different value than `value` due to implicit
// truncation. Implicit truncation in ILAst only happens for small-integer
// types (the ILReader inserts `conv` for other truncations); the analysis
// recurses into LdcI4 constants, Convs, Comps (always fit: 0/1), and
// BitAnd/BitOr/BitXor binaries + IfInstruction arms (the result fits iff both
// sides fit). The C# else-branch consults `value.InferType(compilation)` to
// compare the inferred type's size/sign against the target; this minimal type
// system has no InferType, so the else-branch is approximated conservatively as
// ValueChanged (matching the C# Unknown case -- a value whose inferred type is
// unknown might be truncated), so a compound assignment to a small integer with
// an unmodeled RHS does not fold. `allowNullableValue` (the C# consults it only
// in the InferType else-branch) is kept for API fidelity but not consulted by
// the conservative approximation. `value` is a non-owning view.
ImplicitTruncationResult CheckImplicitTruncation(const ILInstruction* value,
                                                 const TypeSystem::IType* type,
                                                 bool allowNullableValue = false);

// Port of TransformAssignment.IsImplicitTruncation: true when
// CheckImplicitTruncation != ValuePreserved (the value would be changed by an
// implicit truncation to `type`).
bool IsImplicitTruncation(const ILInstruction* value,
                          const TypeSystem::IType* type,
                          bool allowNullableValue = false);

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformAssignment.cs as an
// IStatementTransform (a child of the GetILTransforms() StatementTransform, after
// ILInlining / ExpressionTransforms / the deferred DynamicIsEventAssignmentTransform).
// The C# Run() dispatches to TransformInlineAssignmentStObjOrCall /
// TransformInlineAssignmentLocal (gated on MakeAssignmentExpressions) and
// TransformPostIncDecOperatorWithInlineStore / TransformPostIncDecOperator /
// TransformPreIncDecOperatorWithInlineStore (gated on
// IntroduceIncrementAndDecrement); each consults the shared IsCompoundStore /
// IsMatchingCompoundLoad / UnwrapSmallIntegerConv / ValidateCompoundAssign /
// RecombineVariables helpers above. This iteration ports the self-contained
// TransformPostIncDecOperatorWithInlineStore binary case (the local/StLoc
// post-increment `stloc target(binary.add(stloc tmp(ldloc target), ldc.i4 1))`
// -> `stloc tmp(compound.assign.add.old(ldloca target, ldc.i4 1))` = `tmp =
// target++`), the simplest wired fold the D131 helpers unblock. The operator-call
// (op_Increment/op_Decrement) case (needs the UserDefinedCompoundAssign node +
// Call.IsLifted) and the TransformInlineAssignment* / TransformPreIncDecOperatorWithInlineStore
// StObj/Call cases (need InferType / IsSameMember / IMethod) are deferred.
class TransformAssignment : public IStatementTransform {
public:
	void Run(Block& block, int pos, StatementTransformContext& context) override;

private:
	// TransformPostIncDecOperatorWithInlineStore (binary case): folds the local
	// post-increment/decrement `stloc target(binary.op(stloc tmp(ldloc target),
	// ldc.i4 1))` (a single non-terminal at block.Instructions[pos]) into `stloc
	// tmp(NumericCompoundAssign.op.old(ldloca target, ldc.i4 1))` (= `tmp =
	// target++`), the C# `EvaluatesToOldValue` compound assign. Gated on
	// IntroduceIncrementAndDecrement. The if-as-final block-model adaptation is
	// trivial (the store is a non-terminal, not a final). Returns true if a fold
	// fired.
	bool TransformPostIncDecOperatorWithInlineStore(Block& block, int pos,
	                                                StatementTransformContext& context);

	// TransformPostIncDecOperator (binary, non-inline-store case): folds the
	// two-instruction post-increment/decrement
	//   stloc tmp(ldloc target)              at Instructions[i]
	//   stloc target(binary.op(ldloc tmp, 1)) at Instructions[i+1]
	// into `stloc tmp(NumericCompoundAssign.op.old(ldloca target, 1))` (= `tmp =
	// target++`), and removes the store at i+1. When tmp is dead (single-def,
	// load-count 0), the StLoc is replaced with the compound assign directly (a
	// statement-level `target++`). The legacy csc emits this two-instruction shape
	// for local post-increments (the WithInlineStore expression form is Roslyn-era).
	// Returns true if a fold fired.
	bool TransformPostIncDecOperator(Block& block, int pos,
	                                 StatementTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL
