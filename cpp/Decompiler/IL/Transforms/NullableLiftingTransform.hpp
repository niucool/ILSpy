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
// rewriting is a larger slice and is deferred; this header exposes only the
// self-contained shape matchers that recognise Nullable<T>'s HasValue /
// GetValueOrDefault access patterns:
//
//   - MatchHasValueCall: `call get_HasValue(arg)` on System.Nullable<T> -> arg.
//   - MatchGetValueOrDefault: `call GetValueOrDefault(arg)` (the 1-arg form)
//     on System.Nullable<T> -> arg.
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

namespace ILSpy::Decompiler::IL {

class ILInstruction;

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
    // (the underlying-value accessor). The 2-argument form with a fallback
    // default is deferred (the switch-on-nullable patterns use the 1-arg form).
    static bool MatchGetValueOrDefault(ILInstruction* inst, ILInstruction*& arg);
};

} // namespace ILSpy::Decompiler::IL
