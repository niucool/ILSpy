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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/NullPropagationTransform.cs
// (subset): the C# 6.0 null-conditional operator (`?.`) lowering -- converts
// code patterns like `v != null ? v.M() : null` to `v?.M()`. The transform is
// a `readonly struct` in the C# (constructed per-call with the ILTransformContext);
// this port exposes the self-contained pieces as static helpers and the `Run`
// entry as a static method returning the lifted instruction (owned) or nullptr.
//
// The C# transform has two entry points:
//   * `Run(condition, trueInst, falseInst)` -- consulted first inside
//     NullableLiftingTransform.Lift (before the LiftNullables-gated lift
//     paths). A conditional whose condition is a null test on a variable
//     (`comp(ldloc v == null)` for a reference type, `call get_HasValue(..)`
//     for a Nullable<T>) and whose non-null arm is an access chain on that
//     variable folds into a `NullableRewrap(access_chain)`.
//   * `RunStatements(block, pos)` -- the per-statement entry
//     (NullPropagationStatementTransform), the void-call and
//     unconstrained-generic patterns.
//
// This port ports the `IsProtectedIfInst` static helper, the `MatchNullableRewrap`
// helper, and the `Run` entry for the ReferenceType mode (the `comp(ldloc v
// ==/!= null)` condition), the NullableByValue mode (`call get_HasValue(ldloca v)`
// condition), and the NullableByReference mode (`call get_HasValue(ldloc v)`
// condition), each with the `ldnull` and `default(Nullable<T>)` output cases
// (the `NullCoalescing` output case needs InferType /
// NullableType.IsNonNullableValueType, deferred). The void-call subset of
// RunStatements is ported via `NullPropagationStatementTransform` (the `?.`
// statement form). The UnconstrainedType mode and the
// TransformNullPropagationOnUnconstrainedGenericExpression pattern are deferred
// (the latter needs a 5-instruction block-model sequence + a corpus probe).
//
// The access chain analysis (IsValidAccessChain) is approximated for the
// Call case: this port's Call carries no IsStatic / IsExtensionMethod /
// IsAccessor / IsGetter / ConstrainedTo metadata, so the `IsInstanceCall` flag
// (true for instance call/callvirt, false for static and newobj) is the
// faithful gate (a static method cannot be `?.`-ed; newobj is already excluded
// by the `!IsNewObj` check). The AddressOf / LdObjIfRef special cases and the
// Dynamic* nodes are not modeled and are conservatively rejected. The LdFld /
// LdFlda / LdLen / LdElema / NullableUnwrap cases are faithfully matched.

#pragma once

#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <memory>

namespace ILSpy::Decompiler::IL {

class ILInstruction;
class ILVariable;
class IfInstruction;

class NullPropagationTransform {
public:
    // Port of NullPropagationTransform.IsProtectedIfInst (static): excludes a
    // logic.and/or in a condition slot from null-propagation. The C# comment:
    // "We exclude logic.and to avoid turning
    // `logic.and(comp(interfaces != ldnull), call get_Count(interfaces))` into
    // `if ((interfaces?.Count ?? 0) != 0)`." Returns true when `ifInst` is an
    // IfInstruction that is a logic.and (`if (a) ... else ldc.i4 0`) or a
    // logic.or (`if (a) ldc.i4 1 else ...`) AND it occurs in a condition slot
    // (an IfInstruction's Condition at ChildIndex 0, or nested in one). A null
    // ifInst (the BinaryNumericInstruction caller -- a BNI is not an
    // IfInstruction) returns false, matching the C# `ifInst as IfInstruction`
    // yielding null.
    static bool IsProtectedIfInst(IfInstruction* ifInst);

    // Port of ILInstruction.MatchNullableRewrap(out arg): matches a
    // NullableRewrap node and reports its Argument. Used by TryNullPropagation
    // to strip a rewrap wrapper from the non-null arm before the access chain
    // analysis.
    static bool MatchNullableRewrap(ILInstruction* inst, ILInstruction*& arg);

    // Port of NullPropagationTransform.Run(condition, trueInst, falseInst):
    // the `?.` lowering entry consulted first inside Lift (before the
    // LiftNullables-gated paths). `condition` is the un-negated condition
    // (the caller peels logic.not); `trueInst`/`falseInst` are the arms. Returns
    // the lifted instruction (a NullableRewrap, owned) or nullptr if no fold
    // fired. The ReferenceType mode (`comp(ldloc v ==/!= null)`), the
    // NullableByValue mode (`call get_HasValue(ldloca v)`), and the
    // NullableByReference mode (`call get_HasValue(ldloc v)`) are ported with
    // the `ldnull` and `default(Nullable<T>)` output cases (the `NullCoalescing`
    // output case needs InferType / NullableType.IsNonNullableValueType,
    // deferred). The UnconstrainedType mode is deferred. The caller checks the
    // NullPropagation setting and IsProtectedIfInst before calling.
    static std::unique_ptr<ILInstruction> Run(ILInstruction* condition,
                                               ILInstruction* trueInst,
                                               ILInstruction* falseInst);
};

// Port of NullPropagationStatementTransform (the C# IStatementTransform child of
// StatementTransform that calls NullPropagationTransform.RunStatements). This
// ports the void-call subset of RunStatements: `if (testedVar != null) {
// testedVar.AccessChain(); }` folds into `testedVar?.AccessChain();` (a void
// NullableRewrap, the `?.` statement form). The if is the block's
// FinalInstruction (this port's if-as-final model), the TrueInst is a Block with
// exactly one instruction (the void call), and the FalseInst is null (no else).
// The UnconstrainedType mode and the
// TransformNullPropagationOnUnconstrainedGenericExpression pattern are
// deferred (the latter needs a 5-instruction block-model sequence + a corpus
// probe). Gated on the NullPropagation setting.
class NullPropagationStatementTransform : public IStatementTransform {
public:
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
