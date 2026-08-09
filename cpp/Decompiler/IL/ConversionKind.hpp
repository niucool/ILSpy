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

// Port of the ConversionKind enum and Conv.GetConversionKind in
// ICSharpCode.Decompiler/IL/Instructions/Conv.cs. The semantic meaning of a
// Conv instruction (truncate / sign-extend / zero-extend / int-to-float / ...),
// derived from the target PrimitiveType, the input StackType, and the input
// Sign. The array-index cleanup (ExpressionTransforms.CleanUpArrayIndices)
// consults Kind to drop redundant widening convs (SignExtend / ZeroExtend /
// checked-Truncate) from array indices; the conv.r.un combining logic
// (ExpressionTransforms.VisitConv) consults Kind == IntToFloat.

#pragma once

#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

namespace ILSpy::Decompiler::IL {

enum class ConversionKind : std::uint8_t {
    Invalid,
    Nop,
    IntToFloat,
    FloatToInt,
    FloatPrecisionChange,
    SignExtend,
    ZeroExtend,
    Truncate,
    StopGCTracking,
    StartGCTracking,
    ObjectInterior,
};

using ILSpy::Decompiler::TypeSystem::Sign;

// Faithful port of Conv.GetConversionKind(targetType, inputType, inputSign) in
// Conv.cs -- Ecma-335 Table 8: Conversion Operators. The Sign argument is only
// consulted where the target width is ambiguous between sign- and
// zero-extension (I8/U8 and I/U from I4/I), matching the C#.
inline ConversionKind GetConversionKind(PrimitiveType targetType, StackType inputType, Sign inputSign) {
    auto signedToI8OrU8 = [&](PrimitiveType t) {
        // targetType I8 vs U8 from I4/I: SignExtend for the signed target, ZeroExtend
        // for the unsigned target (or when the input is explicitly unsigned).
        if (inputSign == Sign::None)
            return t == PrimitiveType::I8 ? ConversionKind::SignExtend : ConversionKind::ZeroExtend;
        return inputSign == Sign::Signed ? ConversionKind::SignExtend : ConversionKind::ZeroExtend;
    };
    auto signedToIOrU = [&](PrimitiveType t) {
        if (inputSign == Sign::None)
            return t == PrimitiveType::I ? ConversionKind::SignExtend : ConversionKind::ZeroExtend;
        return inputSign == Sign::Signed ? ConversionKind::SignExtend : ConversionKind::ZeroExtend;
    };
    switch (targetType) {
        case PrimitiveType::I1:
        case PrimitiveType::I2:
        case PrimitiveType::U1:
        case PrimitiveType::U2:
            switch (inputType) {
                case StackType::I4:
                case StackType::I8:
                case StackType::I:
                    return ConversionKind::Truncate;
                case StackType::F4:
                case StackType::F8:
                    return ConversionKind::FloatToInt;
                default:
                    return ConversionKind::Invalid;
            }
        case PrimitiveType::I4:
        case PrimitiveType::U4:
            switch (inputType) {
                case StackType::I4:
                    return ConversionKind::Nop;
                case StackType::I:
                case StackType::I8:
                    return ConversionKind::Truncate;
                case StackType::F4:
                case StackType::F8:
                    return ConversionKind::FloatToInt;
                default:
                    return ConversionKind::Invalid;
            }
        case PrimitiveType::I8:
        case PrimitiveType::U8:
            switch (inputType) {
                case StackType::I4:
                case StackType::I:
                    return signedToI8OrU8(targetType);
                case StackType::I8:
                    return ConversionKind::Nop;
                case StackType::F4:
                case StackType::F8:
                    return ConversionKind::FloatToInt;
                case StackType::Ref:
                case StackType::O:
                    return ConversionKind::StopGCTracking;
                default:
                    return ConversionKind::Invalid;
            }
        case PrimitiveType::I:
        case PrimitiveType::U:
            switch (inputType) {
                case StackType::I4:
                    return signedToIOrU(targetType);
                case StackType::I:
                    return ConversionKind::Nop;
                case StackType::I8:
                    return ConversionKind::Truncate;
                case StackType::F4:
                case StackType::F8:
                    return ConversionKind::FloatToInt;
                case StackType::Ref:
                case StackType::O:
                    return ConversionKind::StopGCTracking;
                default:
                    return ConversionKind::Invalid;
            }
        case PrimitiveType::R4:
            switch (inputType) {
                case StackType::I4:
                case StackType::I:
                case StackType::I8:
                    return ConversionKind::IntToFloat;
                case StackType::F4:
                    return ConversionKind::Nop;
                case StackType::F8:
                    return ConversionKind::FloatPrecisionChange;
                default:
                    return ConversionKind::Invalid;
            }
        case PrimitiveType::R:
        case PrimitiveType::R8:
            switch (inputType) {
                case StackType::I4:
                case StackType::I:
                case StackType::I8:
                    return ConversionKind::IntToFloat;
                case StackType::F4:
                    return ConversionKind::FloatPrecisionChange;
                case StackType::F8:
                    return ConversionKind::Nop;
                default:
                    return ConversionKind::Invalid;
            }
        case PrimitiveType::Ref:
            switch (inputType) {
                case StackType::I4:
                case StackType::I:
                case StackType::I8:
                    return ConversionKind::StartGCTracking;
                case StackType::O:
                    return ConversionKind::ObjectInterior;
                default:
                    return ConversionKind::Invalid;
            }
        default:
            return ConversionKind::Invalid;
    }
}

} // namespace ILSpy::Decompiler::IL
