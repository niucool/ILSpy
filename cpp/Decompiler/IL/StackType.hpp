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

// Port of ICSharpCode.Decompiler/IL/StackType.cs. The CLI evaluation-stack type
// lattice used by the IL reader. Numeric ordering matters: when two branches
// meet with different slot types, the merged type is the one with the higher
// numeric value (ILReader.MergeStacks).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::IL {

enum class StackType : std::uint8_t {
    Unknown,
    I4,
    I,
    I8,
    F4,
    F8,
    O,
    Ref,
    Void,
};

// Port of ILTypeExtensions.IsIntegerType(StackType): the integer evaluation-stack
// types (I4, I, I8). F4/F8/O/Ref/Unknown/Void are not.
inline bool IsIntegerType(StackType s) {
    return s == StackType::I4 || s == StackType::I || s == StackType::I8;
}

// Port of ILTypeExtensions.IsFloatType(StackType): the floating-point
// evaluation-stack types (F4, F8).
inline bool IsFloatType(StackType s) {
    return s == StackType::F4 || s == StackType::F8;
}

} // namespace ILSpy::Decompiler::IL
