// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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

// Port of ICSharpCode.Decompiler/IL/PrimitiveType.cs. The primitive IL type
// taxonomy -- distinguishes the signed/unsigned integer sizes that the
// StackType lattice collapses (I4 covers both I4 and U4, I8 covers I8/U8, I
// covers I/U). The Conv node carries the target PrimitiveType so its
// ConversionKind can be computed faithfully (sign- vs zero-extension, the
// conv.r.un R target), and so future transforms (VisitConv) can query
// TargetType.IsIntegerType()/IsFloatType().

#pragma once

#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <cstdint>

namespace ILSpy::Decompiler::IL {

enum class PrimitiveType : std::uint8_t {
    None,
    I1,
    I2,
    I4,
    I8,
    R4,
    R8,
    U1,
    U2,
    U4,
    U8,
    I,
    U,
    // Managed reference (ByRef).
    Ref,
    // Floating point of unspecified size (only "conv.r.un" targets this). The
    // C# treats R identically to R8 as a stack type (both F8); the only consumer
    // that distinguishes them is the conv.r.un + conv.r[48] combining logic.
    R,
    Unknown,
};

// Port of ILTypeExtensions.GetStackType(PrimitiveType): the evaluation-stack
// StackType a primitive type lands on. I1/U1/I2/U2/I4/U4 -> I4; I8/U8 -> I8;
// I/U -> I; R4 -> F4; R8/R -> F8; Ref -> Ref; Unknown/None -> Unknown.
inline StackType GetStackType(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::I1:
        case PrimitiveType::U1:
        case PrimitiveType::I2:
        case PrimitiveType::U2:
        case PrimitiveType::I4:
        case PrimitiveType::U4:
            return StackType::I4;
        case PrimitiveType::I8:
        case PrimitiveType::U8:
            return StackType::I8;
        case PrimitiveType::I:
        case PrimitiveType::U:
            return StackType::I;
        case PrimitiveType::R4:
            return StackType::F4;
        case PrimitiveType::R8:
        case PrimitiveType::R:
            return StackType::F8;
        case PrimitiveType::Ref:
            return StackType::Ref;
        case PrimitiveType::Unknown:
            return StackType::Unknown;
        default:
            return StackType::Unknown;
    }
}

// Port of ILTypeExtensions.IsIntegerType(PrimitiveType): the integer primitive
// types (I1/I2/I4/I8/U1/U2/U4/U8/I/U). R4/R8/R/Ref/None/Unknown are not.
inline bool IsIntegerType(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::I1:
        case PrimitiveType::I2:
        case PrimitiveType::I4:
        case PrimitiveType::I8:
        case PrimitiveType::U1:
        case PrimitiveType::U2:
        case PrimitiveType::U4:
        case PrimitiveType::U8:
        case PrimitiveType::I:
        case PrimitiveType::U:
            return true;
        default:
            return false;
    }
}

// Port of ILTypeExtensions.IsFloatType(PrimitiveType): the floating-point
// primitive types (R4/R8/R).
inline bool IsFloatType(PrimitiveType p) {
    return p == PrimitiveType::R4 || p == PrimitiveType::R8 || p == PrimitiveType::R;
}

// Port of ILTypeExtensions.GetSize(PrimitiveType): the size in bytes of a
// primitive type. 1 for I1/U1, 2 for I2/U2, 4 for I4/U4/R4, 8 for I8/R8/U8/R,
// TypeUtils::NativeIntSize (6) for I/U/Ref, 0 for None/Unknown. The native-int
// size sits between Int32 (4) and Int64 (8) so a native int is larger than an
// int but smaller than a long, matching the C# TypeUtils.NativeIntSize.
inline int GetSize(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::I1:
        case PrimitiveType::U1:
            return 1;
        case PrimitiveType::I2:
        case PrimitiveType::U2:
            return 2;
        case PrimitiveType::I4:
        case PrimitiveType::U4:
        case PrimitiveType::R4:
            return 4;
        case PrimitiveType::I8:
        case PrimitiveType::R8:
        case PrimitiveType::U8:
        case PrimitiveType::R:
            return 8;
        case PrimitiveType::I:
        case PrimitiveType::U:
        case PrimitiveType::Ref:
            return 6; // TypeUtils.NativeIntSize
        default:
            return 0;
    }
}

// Port of ILTypeExtensions.IsSmallIntegerType(PrimitiveType): a small integer
// type is one whose size is less than 4 bytes (I1/U1/I2/U2). GetSize < 4, so
// None/Unknown (size 0) also yield true -- faithful to the C# (which only ever
// consults this for a real Conv.TargetType the reader populated from an opcode,
// never None/Unknown). The TransformAssignment UnwrapSmallIntegerConv consults
// this to recognise the compiler's `conv` truncation to a small integer that a
// compound assignment to a small-integer local/field carries.
inline bool IsSmallIntegerType(PrimitiveType p) {
    return GetSize(p) < 4;
}

// Port of ILTypeExtensions.GetSign(PrimitiveType): the sign of a primitive
// type. Signed for I1/I2/I4/I8/R4/R8/R/I; Unsigned for U1/U2/U4/U8/U; None for
// Ref/None/Unknown. Used by the TransformAssignment SwapSign + the compound-
// assignment validation that checks the binary's Sign matches the store type's
// sign (a small-integer compound assign must use the signed binary).
inline TypeSystem::Sign GetSign(PrimitiveType p) {
    switch (p) {
        case PrimitiveType::I1:
        case PrimitiveType::I2:
        case PrimitiveType::I4:
        case PrimitiveType::I8:
        case PrimitiveType::R4:
        case PrimitiveType::R8:
        case PrimitiveType::R:
        case PrimitiveType::I:
            return TypeSystem::Sign::Signed;
        case PrimitiveType::U1:
        case PrimitiveType::U2:
        case PrimitiveType::U4:
        case PrimitiveType::U8:
        case PrimitiveType::U:
            return TypeSystem::Sign::Unsigned;
        default:
            return TypeSystem::Sign::None;
    }
}

} // namespace ILSpy::Decompiler::IL
