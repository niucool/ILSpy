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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Shared type queries over IType, mirroring ICSharpCode.Decompiler/TypeSystem/
// TypeUtils.cs and the IsReferenceType property on each C# IType subclass. The
// C# `IType.IsReferenceType` is a tri-state `bool?`: `true` for reference types
// (Class, Interface, Delegate, Array, Dynamic, Null), `false` for value types
// (Struct, Enum, Void, NInt, NUInt, FunctionPointer), and `null` when it cannot
// be determined from the kind alone (TypeParameter, ByReference, Pointer,
// Unknown, None, Other, UnboundTypeArgument, ArgList, Tuple, Intersection,
// ModOpt, ModReq -- these depend on attributes / the underlying type / generic
// constraints the minimal port does not carry). The C# checks
// `IsReferenceType == true`, `== false`, `!= true` map onto the optional
// accordingly (`opt && *opt`, `opt && !*opt`, `!opt || !*opt`). A null ITypePtr
// yields nullopt (the C# never sees a null IType, but the port's readers can
// hand back a null resolution). This promotes the previously file-local
// helpers (PatternMatchingTransform.cpp) and the inline IsRef(TypeKind) bool
// checks (EarlyExpressionTransforms / LdLocaDupInitObjTransform) to a single
// faithful, reusable query.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <optional>

namespace ILSpy::Decompiler::TypeSystem {

// `bool? IType.IsReferenceType` for the minimal port. Returns true for the
// reference-type kinds, false for the value-type kinds, and nullopt when the
// kind alone is not enough (TypeParameter/ByRef/Pointer/Unknown/None/Other/
// UnboundTypeArgument/ArgList/Tuple/Intersection/ModOpt/ModReq). A null IType
// is nullopt. ParameterizedType/ModifiedType/Tuple defer to their underlying
// type's Kind() (already folded in by this port's Kind() overrides), so the
// Kind() switch covers them: a ParameterizedType over a Class generic definition
// reports Class (true), a ModifiedType delegates to its element's Kind, etc.
inline std::optional<bool> IsReferenceType(const IType* t) {
    if (!t) return std::nullopt;
    switch (t->Kind()) {
        // Reference types (C# returns true).
        case TypeKind::Class:
        case TypeKind::Interface:
        case TypeKind::Delegate:
        case TypeKind::Array:
        case TypeKind::Dynamic:
        case TypeKind::Null:
            return true;
        // Value types (C# returns false).
        case TypeKind::Struct:
        case TypeKind::Enum:
        case TypeKind::Void:
        case TypeKind::NInt:
        case TypeKind::NUInt:
        case TypeKind::FunctionPointer:
            return false;
        // Cannot be determined from the kind alone (C# returns null).
        case TypeKind::TypeParameter:
        case TypeKind::ByReference:
        case TypeKind::Pointer:
        case TypeKind::Unknown:
        case TypeKind::None:
        case TypeKind::Other:
        case TypeKind::UnboundTypeArgument:
        case TypeKind::ArgList:
        case TypeKind::Tuple:
        case TypeKind::Intersection:
        case TypeKind::ModOpt:
        case TypeKind::ModReq:
            return std::nullopt;
    }
    return std::nullopt;  // unreachable; keeps the compiler quiet on a new kind
}

// Port of TypeUtils.GetSign(IType): the sign of an integer value. None for an
// unknown / not-applicable type; Signed for the signed primitives + float/
// decimal; Unsigned for the unsigned primitives + char + bool + pointer /
// native-uint / function-pointer; Signed for native-int. A non-known type
// (TypeParameter / ByReference / Unknown / ...) yields None. The C# also
// unwraps SkipModifiers + GetEnumUnderlyingType; this minimal port models neither
// (no ModifiedType, no per-enum underlying type), so a KnownType is used directly
// and an Enum KnownType falls through to None (the conv.nop.lifted case only
// deals with primitive underlying types).
inline Sign GetSign(const IType* type) {
    if (!type) return Sign::None;
    switch (type->Kind()) {
        case TypeKind::Pointer:
        case TypeKind::NUInt:
        case TypeKind::FunctionPointer:
            return Sign::Unsigned;
        case TypeKind::NInt:
            return Sign::Signed;
        default:
            break;
    }
    if (const auto* k = dynamic_cast<const KnownType*>(type)) {
        switch (k->Code()) {
            case KnownTypeCode::SByte:
            case KnownTypeCode::Int16:
            case KnownTypeCode::Int32:
            case KnownTypeCode::Int64:
            case KnownTypeCode::IntPtr:
            case KnownTypeCode::Single:
            case KnownTypeCode::Double:
            case KnownTypeCode::Decimal:
                return Sign::Signed;
            case KnownTypeCode::UIntPtr:
            case KnownTypeCode::Char:
            case KnownTypeCode::Boolean:
            case KnownTypeCode::Byte:
            case KnownTypeCode::UInt16:
            case KnownTypeCode::UInt32:
            case KnownTypeCode::UInt64:
                return Sign::Unsigned;
            default:
                return Sign::None;
        }
    }
    return Sign::None;
}

// Port of TypeUtils.ToPrimitiveType(KnownTypeCode): maps a known primitive's
// code to its PrimitiveType (the signed/unsigned size the StackType lattice
// collapses). Unknown / non-primitive codes yield None. Used by the
// NullableLifting conv.nop.lifted case to get the conv's target PrimitiveType.
inline ILSpy::Decompiler::IL::PrimitiveType ToPrimitiveType(KnownTypeCode code) {
    using ILSpy::Decompiler::IL::PrimitiveType;
    switch (code) {
        case KnownTypeCode::SByte: return PrimitiveType::I1;
        case KnownTypeCode::Int16: return PrimitiveType::I2;
        case KnownTypeCode::Int32: return PrimitiveType::I4;
        case KnownTypeCode::Int64: return PrimitiveType::I8;
        case KnownTypeCode::Single: return PrimitiveType::R4;
        case KnownTypeCode::Double: return PrimitiveType::R8;
        case KnownTypeCode::Byte: return PrimitiveType::U1;
        case KnownTypeCode::UInt16:
        case KnownTypeCode::Char:
            return PrimitiveType::U2;
        case KnownTypeCode::UInt32: return PrimitiveType::U4;
        case KnownTypeCode::UInt64: return PrimitiveType::U8;
        case KnownTypeCode::IntPtr: return PrimitiveType::I;
        case KnownTypeCode::UIntPtr: return PrimitiveType::U;
        default: return PrimitiveType::None;
    }
}

// Port of TypeUtils.ToPrimitiveType(IType): the PrimitiveType of a resolved
// type. Unknown -> PrimitiveType::Unknown; ByReference -> Ref; NInt /
// FunctionPointer -> I; NUInt -> U; otherwise the KnownTypeCode's ToPrimitiveType
// (None for a non-primitive / non-known type). The C# unwraps
// GetEnumUnderlyingType().GetDefinition(); this minimal port has no per-enum
// underlying type, so an Enum KnownType yields None (the conv.nop.lifted case
// only deals with primitive underlying types).
inline ILSpy::Decompiler::IL::PrimitiveType ToPrimitiveType(const IType* type) {
    using ILSpy::Decompiler::IL::PrimitiveType;
    if (!type) return PrimitiveType::None;
    switch (type->Kind()) {
        case TypeKind::Unknown:
            return PrimitiveType::Unknown;
        case TypeKind::ByReference:
            return PrimitiveType::Ref;
        case TypeKind::NInt:
        case TypeKind::FunctionPointer:
            return PrimitiveType::I;
        case TypeKind::NUInt:
            return PrimitiveType::U;
        default:
            break;
    }
    if (const auto* k = dynamic_cast<const KnownType*>(type))
        return ToPrimitiveType(k->Code());
    return PrimitiveType::None;
}

// Port of TypeUtils.ToKnownTypeCode(StackType, Sign): the KnownTypeCode a
// StackType + Sign maps to. I4 -> Int32 (or UInt32 when unsigned); I8 -> Int64
// (or UInt64); I -> IntPtr (or UIntPtr); F4 -> Single; F8 -> Double; O -> Object;
// Void -> Void; Unknown / Ref -> None. Used by the NullableLifting
// conv.nop.lifted case to build the conv's target type from the GVO call's
// ResultType (a StackType).
inline KnownTypeCode ToKnownTypeCode(ILSpy::Decompiler::IL::StackType stackType,
                                      Sign sign = Sign::None) {
    using ILSpy::Decompiler::IL::StackType;
    switch (stackType) {
        case ILSpy::Decompiler::IL::StackType::I4:
            return sign == Sign::Unsigned ? KnownTypeCode::UInt32 : KnownTypeCode::Int32;
        case ILSpy::Decompiler::IL::StackType::I8:
            return sign == Sign::Unsigned ? KnownTypeCode::UInt64 : KnownTypeCode::Int64;
        case ILSpy::Decompiler::IL::StackType::I:
            return sign == Sign::Unsigned ? KnownTypeCode::UIntPtr : KnownTypeCode::IntPtr;
        case ILSpy::Decompiler::IL::StackType::F4:
            return KnownTypeCode::Single;
        case ILSpy::Decompiler::IL::StackType::F8:
            return KnownTypeCode::Double;
        case ILSpy::Decompiler::IL::StackType::O:
            return KnownTypeCode::Object;
        case ILSpy::Decompiler::IL::StackType::Void:
            return KnownTypeCode::Void;
        default:
            return KnownTypeCode::None;
    }
}

// The native-int size in bytes, between Int32 (4) and Int64 (8). Faithful to
// the C# TypeUtils.NativeIntSize. Pointer-sized types (I/U/Ref, and the IType
// kinds Pointer/ByReference/Class/NInt/NUInt) report this size from GetSize.
inline constexpr int kNativeIntSize = 6;

// Port of TypeUtils.GetSize(IType): the size in bytes of a type. Pointer-sized
// kinds (Pointer/ByReference/Class/NInt/NUInt) report kNativeIntSize; an Enum
// defers to its underlying type (this minimal port has no per-enum underlying
// type, so an Enum KnownType falls through to the kind switch and reports
// kNativeIntSize via the Enum kind not being in the pointer-size list -- the
// C# unwraps GetEnumUnderlyingType first, which this port does not model, so a
// KnownType Enum reports 0 unless it is one of the primitive KnownTypeCodes);
// a KnownType reports its primitive size by KnownTypeCode (1 for Boolean/SByte/
// Byte, 2 for Char/Int16/UInt16, 4 for Int32/UInt32/Single, kNativeIntSize for
// IntPtr/UIntPtr, 8 for Int64/UInt64/Double); 0 otherwise (O/F/Void/Unknown).
inline int GetSize(const IType* type) {
    if (!type) return 0;
    switch (type->Kind()) {
        case TypeKind::Pointer:
        case TypeKind::ByReference:
        case TypeKind::Class:
        case TypeKind::NInt:
        case TypeKind::NUInt:
            return kNativeIntSize;
        case TypeKind::Enum:
            // The C# unwraps GetEnumUnderlyingType().GetDefinition() and reports
            // its size; this minimal port has no per-enum underlying type, so an
            // Enum KnownType falls through to the KnownTypeCode switch below
            // (which yields 0 for a bare Enum kind). The compound-assignment
            // validation only consults IsCSharpSmallIntegerType (the KnownTypeCode
            // switch), so this is the faithful best-effort.
            break;
        default:
            break;
    }
    if (const auto* k = dynamic_cast<const KnownType*>(type)) {
        switch (k->Code()) {
            case KnownTypeCode::Boolean:
            case KnownTypeCode::SByte:
            case KnownTypeCode::Byte:
                return 1;
            case KnownTypeCode::Char:
            case KnownTypeCode::Int16:
            case KnownTypeCode::UInt16:
                return 2;
            case KnownTypeCode::Int32:
            case KnownTypeCode::UInt32:
            case KnownTypeCode::Single:
                return 4;
            case KnownTypeCode::IntPtr:
            case KnownTypeCode::UIntPtr:
                return kNativeIntSize;
            case KnownTypeCode::Int64:
            case KnownTypeCode::UInt64:
            case KnownTypeCode::Double:
                return 8;
            default:
                return 0;
        }
    }
    return 0;
}

// Port of TypeUtils.IsSmallIntegerType(IType): a small integer type is one
// whose size is greater than 0 and less than 4 bytes (Boolean/SByte/Byte/Char/
// Int16/UInt16, and enums with such an underlying type). The TransformAssignment
// UnwrapSmallIntegerConv + the small-integer store-type guards consult this.
inline bool IsSmallIntegerType(const IType* type) {
    int size = GetSize(type);
    return size > 0 && size < 4;
}

// Port of TypeUtils.IsCSharpSmallIntegerType(IType): whether the type is a C#
// small integer (byte/sbyte/short/ushort). Unlike the ILAst IsSmallIntegerType,
// C# does not consider bool, char or enums to be small integers. The compound-
// assignment validation consults this to decide whether a small-integer LHS
// requires the binary to be signed (C# numeric-promotes a small integer to int).
inline bool IsCSharpSmallIntegerType(const IType* type) {
    if (const auto* k = dynamic_cast<const KnownType*>(type)) {
        switch (k->Code()) {
            case KnownTypeCode::Byte:
            case KnownTypeCode::SByte:
            case KnownTypeCode::Int16:
            case KnownTypeCode::UInt16:
                return true;
            default:
                break;
        }
    }
    return false;
}

// Port of TransformAssignment.SwapSign: the type with the opposite sign for a
// primitive integer type (I1<->U1, I2<->U2, I4<->U4, I8<->U8, I<->U). Returns a
// fresh KnownType for the opposite-sign KnownTypeCode, or nullptr for a type
// with no opposite sign (the C# throws ArgumentException; this port returns
// nullptr because the callers only consult SwapSign after a sign-mismatch
// guard, and a nullptr propagates as a no-fold). The compound-assignment
// post-inc/dec transform consults this to fix a conv sign mismatch against the
// store type.
inline ITypePtr SwapSign(const IType* type) {
    using ILSpy::Decompiler::IL::PrimitiveType;
    if (!type) return nullptr;
    const auto pt = ToPrimitiveType(type);
    KnownTypeCode target = KnownTypeCode::None;
    switch (pt) {
        case PrimitiveType::I1: target = KnownTypeCode::Byte; break;
        case PrimitiveType::I2: target = KnownTypeCode::UInt16; break;
        case PrimitiveType::I4: target = KnownTypeCode::UInt32; break;
        case PrimitiveType::I8: target = KnownTypeCode::UInt64; break;
        case PrimitiveType::U1: target = KnownTypeCode::SByte; break;
        case PrimitiveType::U2: target = KnownTypeCode::Int16; break;
        case PrimitiveType::U4: target = KnownTypeCode::Int32; break;
        case PrimitiveType::U8: target = KnownTypeCode::Int64; break;
        case PrimitiveType::I:  target = KnownTypeCode::UIntPtr; break;
        case PrimitiveType::U:  target = KnownTypeCode::IntPtr; break;
        default: return nullptr;
    }
    return std::make_shared<KnownType>(target);
}

} // namespace ILSpy::Decompiler::TypeSystem
