// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING IN, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the static pattern-matching helpers of
// ICSharpCode.Decompiler/IL/Transforms/NullableLiftingTransform.cs that the
// switch-on-nullable family (SwitchOnNullableTransform, and SwitchDetection's
// AddNullCase) depend on. The full NullableLiftingStatementTransform / lift
// rewriting is a larger slice and is deferred; this header exposes the
// self-contained shape matchers that recognise Nullable<T>'s HasValue /
// GetValueOrDefault access patterns:
//
//   - MatchHasValueCall: `call get_HasValue(arg)` on System.Nullable<T> -> arg.
//   - MatchGetValueOrDefault: `call GetValueOrDefault(arg)` (the 1-arg form)
//     on System.Nullable<T> -> arg, and the 2-argument form
//     `call GetValueOrDefault(nullableValue, fallback)` -> (nullableValue, fallback)
//     consumed by the ExpressionTransforms.VisitCall `a ?? b` fold.
//   - MatchCompOrDecimal: recognises a non-lifted IL `Comp` and reports its
//     Kind/Left/Right/IsLifted via the CompOrDecimal struct; the lift
//     machinery consults it to recognise the C#-style lifted comparison shape.
//     The Decimal-operator branch (a Call to op_Equality/... on System.Decimal,
//     needs Call.Method.IsOperator + KnownTypeCode::Decimal) is deferred.
//
// The C# checks `call.Method.Name` and `call.Method.DeclaringTypeDefinition?
// .KnownTypeCode == KnownTypeCode.NullableOfT`. This port's Call carries the
// resolved declaring type as an IType (Call::DeclaringType, set by the IL
// reader); the generic definition is unwrapped (a ParameterizedType's
// GenericType, e.g. Nullable<int> -> Nullable`1) and its KnownTypeCode read.
// A null declaring type is treated like the C# null DeclaringTypeDefinition --
// the helpers return false, so a method whose declaring type cannot be
// resolved never matches.

#pragma once

#include "Decompiler/IL/Instructions/Comp.hpp"

namespace ILSpy::Decompiler::IL {

class ILInstruction;

// Port of NullableLiftingTransform.CompOrDecimal: either a non-lifted IL `Comp`
// or a call to one of the 6 comparison operators on System.Decimal. This port
// carries only the `Comp` case (the Decimal case needs Call.Method.IsOperator +
// KnownTypeCode::Decimal, deferred); the fields mirror the C# struct so the
// nullable-lifting lift machinery that consults it reads the same shape.
// `Instruction` is the matched node (a Comp); `Left`/`Right` are its operands;
// `Kind` is the comparison kind; `IsLifted` is Comp.IsLifted().
struct CompOrDecimal {
    ILInstruction* Instruction = nullptr;
    ComparisonKind Kind = ComparisonKind::Equality;
    ILInstruction* Left = nullptr;
    ILInstruction* Right = nullptr;
    bool IsLifted = false;
};

// The static helper subset of NullableLiftingTransform. The full
// NullableLiftingStatementTransform (the nullable-expression lifting) is
// deferred; only the shape matchers the switch-on-nullable family consumes are
// exposed here, as static methods so call sites read
// `NullableLiftingTransform::MatchHasValueCall(..)` as in the C#.
class NullableLiftingTransform {
public:
    // Port of NullableLiftingTransform.MatchHasValueCall(inst, out ILInstruction arg):
    // returns true and sets `arg` when `inst` is `call get_HasValue(arg)` on
    // System.Nullable<T> (1 argument). The call's declaring type must resolve to
    // KnownTypeCode::NullableOfT (a generic instantiation unwraps to its
    // generic definition); a null DeclaringType does not match.
    static bool MatchHasValueCall(ILInstruction* inst, ILInstruction*& arg);

    // Port of NullableLiftingTransform.MatchGetValueOrDefault(inst, out ILInstruction arg):
    // the 1-argument form `call GetValueOrDefault(arg)` on System.Nullable<T>
    // (the underlying-value accessor). The switch-on-nullable patterns use this
    // 1-arg form.
    static bool MatchGetValueOrDefault(ILInstruction* inst, ILInstruction*& arg);

    // Port of NullableLiftingTransform.MatchGetValueOrDefault(inst, out
    // nullableValue, out fallback): the 2-argument form
    // `call GetValueOrDefault(nullableValue, fallback)` on System.Nullable<T>
    // (the value-or-fallback accessor -- the `a ?? b` lowering the
    // ExpressionTransforms.VisitCall fold consumes). The call must resolve its
    // declaring type to KnownTypeCode::NullableOfT (a generic instantiation
    // unwraps to its generic definition); a null DeclaringType does not match.
    static bool MatchGetValueOrDefault(ILInstruction* inst,
                                       ILInstruction*& nullableValue,
                                       ILInstruction*& fallback);

    // Port of NullableLiftingTransform.MatchCompOrDecimal(inst, out result):
    // recognises a non-lifted IL `Comp` and reports its Kind/Left/Right/IsLifted
    // via `result`. The Decimal-operator branch (a Call to op_Equality/
    // op_Inequality/op_LessThan/... on System.Decimal) is deferred -- it needs
    // Call.Method.IsOperator (this port's Call carries only a resolved name +
    // declaring type, no operator flag) -- so a Call never matches here. Returns
    // false for every other instruction kind, matching the C# fall-through.
    static bool MatchCompOrDecimal(ILInstruction* inst, CompOrDecimal& result);
};

} // namespace ILSpy::Decompiler::IL
