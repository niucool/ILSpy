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

// Port of ICSharpCode.Decompiler/IL/Transforms/TupleTransform.cs -- the static
// helpers that recognize C# 7 tuple accesses and constructions in ILAst. This
// slice lands `MatchTupleFieldAccess` (the ExpressionBuilder.VisitLdFlda and
// DeconstructionTransform consumer); `MatchTupleConstruction` (the flattening
// `newobj ValueTuple<...>` matcher) stays with its transform consumers.

#pragma once

#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"

namespace ILSpy::Decompiler::IL {

// The C# `class TupleTransform` -- the tuple-pattern matching helpers.
class TupleTransform {
public:
    // The C# `public static bool MatchTupleFieldAccess(LdFlda inst, out IType
    // tupleType, out ILInstruction target, out int position)` -- matches an
    // `ldflda` accessing a tuple element, e.g. `ldflda Item1(ldflda Rest(target))`.
    // `tupleType` is the field's declaring type after walking the `Rest` chain;
    // `target` is the underlying tuple expression (the target before the `Rest`
    // unwraps); `position` is the flattened element position (1-based). Returns
    // false with `position` zeroed when the field is not an `Item<N>` on a
    // tuple-compatible type.
    //
    // The port takes the `IType` handle by shared pointer (the C# `out IType`
    // refers to the field's `DeclaringType`, which the port returns by value) and
    // the target as a non-owning pointer into `inst.Target`.
    static bool MatchTupleFieldAccess(const LdFlda& inst, TypeSystem::ITypePtr& tupleType,
                                      ILInstruction*& target, int& position);
};

} // namespace ILSpy::Decompiler::IL
