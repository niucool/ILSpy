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
// (foundation subset) -- the UnwrapSmallIntegerConv helper. See
// TransformAssignment.hpp for the transform this is a foundation for.

#include "Decompiler/IL/Transforms/TransformAssignment.hpp"

#include "Decompiler/IL/ConversionKind.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"

namespace ILSpy::Decompiler::IL {

ILInstruction* UnwrapSmallIntegerConv(ILInstruction* inst, Conv*& conv) {
    conv = dynamic_cast<Conv*>(inst);
    if (conv != nullptr && conv->Kind == ConversionKind::Truncate &&
        IsSmallIntegerType(conv->TargetType)) {
        // For compound assignments to small integers, the compiler emits a
        // "conv" instruction (a Truncate to the small-integer TargetType).
        // Return the conv's argument so the caller can build the compound
        // assign from the underlying binary and validate the conv separately.
        return conv->Argument.get();
    }
    // Not a small-integer-truncating conv: leave the instruction unchanged.
    // (conv may still be set to the dynamic_cast result for a non-Truncate or
    // non-small-integer conv, matching the C# `conv = inst as Conv` assignment;
    // the caller only consults `conv` when the returned instruction is a
    // BinaryNumericInstruction, which a Conv never is, so a stale non-null
    // conv on the no-unwrap path is harmless.)
    return inst;
}

} // namespace ILSpy::Decompiler::IL
