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
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <optional>

namespace ILSpy::Decompiler::TypeSystem {

// Declared in TypeSystemExtensions.hpp, defined in TypeSystemExtensions.cpp;
// re-declared here so the GetEnumUnderlyingType leaf below can call it without
// pulling the full TypeSystemExtensions.hpp (and its ICompilation.hpp) into
// every TypeUtils.hpp consumer.
const IType* SkipModifiers(const IType& type);

// Port of TypeUtils.GetEnumUnderlyingType(IType) (TypeUtils.cs line 323): if
// `type` is an enumeration type, returns the underlying type; otherwise,
// returns `type` unmodified. The C# rebinds through `SkipModifiers()` first,
// so a custom-modifier-decorated input is unwrapped BEFORE the Kind check --
// for a non-enum the passthrough is the UNWRAPPED element, not the
// ModifiedType. The port takes the nullable `const IType*` (the file
// convention: the C# never sees a null IType, but the port's readers can hand
// back a null resolution); a null input, a degenerate ModifiedType with a
// null element (where the C# `type.Kind` deref would NRE), and an Enum kind
// whose definition does not resolve (where the C#
// `type.GetDefinition().EnumUnderlyingType` deref would NRE) all yield nullptr
// (the D516 safe-fallback convention). An enum whose `EnumUnderlyingType` is
// null yields nullptr too (the faithful C# result for a definition that
// reports no underlying type). The returned pointer is non-owning: the
// passthrough is the input or its SkipModifiers-unwrapped element (both
// reachable through the input), and the enum arm is the definition's
// `EnumUnderlyingType` handle (owned by the definition, which is reachable
// through `type`).
inline const IType* GetEnumUnderlyingType(const IType* type)
{
    if (!type) return nullptr;
    const IType* t = SkipModifiers(*type);
    if (!t) return nullptr;
    if (t->Kind() != TypeKind::Enum) return t;
    const ITypeDefinition* def = t->GetDefinition();
    if (!def) return nullptr;
    return def->EnumUnderlyingType().get();
}

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
// unwraps SkipModifiers + GetEnumUnderlyingType before the definition lookup;
// this port reads the KnownType code directly instead, so an Enum type yields
// None (the conv.nop.lifted call site only deals with primitive underlying
// types).
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
    // The C# reads the GetEnumUnderlyingType().GetDefinition().KnownTypeCode (a real
    // definition's code -- the CorlibTypeDefinition/MetadataTypeDefinition shapes the
    // minimal KnownType wrapper misses), falling back to the wrapper's own code for
    // the minimal-port synthetic types.
    KnownTypeCode code = KnownTypeCode::None;
    const IType* underlying = GetEnumUnderlyingType(type);
    if (const ITypeDefinition* def =
            underlying != nullptr ? underlying->GetDefinition() : nullptr)
        code = def->KnownTypeCode();
    else if (const auto* k = dynamic_cast<const KnownType*>(type))
        code = k->Code();
    switch (code) {
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
// GetEnumUnderlyingType().GetDefinition(); this port reads the KnownType code
// directly instead, so an Enum type yields None (the conv.nop.lifted case
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

// Port of TypeUtils.ToKnownTypeCode(PrimitiveType): the KnownTypeCode a
// primitive target type maps to. I1..I8 -> the signed sizes, R4 -> Single,
// R8/R -> Double, U1..U8 -> the unsigned sizes, I -> IntPtr, U -> UIntPtr;
// Ref/None/Unknown yield None (the C# default arm). The VisitConv callers
// map a Conv's TargetType/input stack type through here before FindType.
inline KnownTypeCode ToKnownTypeCode(ILSpy::Decompiler::IL::PrimitiveType primitiveType) {
    using ILSpy::Decompiler::IL::PrimitiveType;
    switch (primitiveType) {
        case PrimitiveType::I1: return KnownTypeCode::SByte;
        case PrimitiveType::I2: return KnownTypeCode::Int16;
        case PrimitiveType::I4: return KnownTypeCode::Int32;
        case PrimitiveType::I8: return KnownTypeCode::Int64;
        case PrimitiveType::R4: return KnownTypeCode::Single;
        case PrimitiveType::R8:
        case PrimitiveType::R:
            return KnownTypeCode::Double;
        case PrimitiveType::U1: return KnownTypeCode::Byte;
        case PrimitiveType::U2: return KnownTypeCode::UInt16;
        case PrimitiveType::U4: return KnownTypeCode::UInt32;
        case PrimitiveType::U8: return KnownTypeCode::UInt64;
        case PrimitiveType::I: return KnownTypeCode::IntPtr;
        case PrimitiveType::U: return KnownTypeCode::UIntPtr;
        default: return KnownTypeCode::None;
    }
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
// kinds (Pointer/ByReference/Class/NInt/NUInt) report this size from GetSize. An
// Enum defers to its underlying type (the GetEnumUnderlyingType unwrap) and a
// ModOpt/ModReq to SkipModifiers, exactly as the C# switch does; the final arm
// reads GetDefinition()'s KnownTypeCode (the Definition-vs-wrapper dispatch -- a
// KnownType wrapper and a CorlibTypeDefinition/MetadataTypeDefinition both reach
// the same table, matching the C# type.GetDefinition() access; the
// GetStackType/GetSign precedent), and a definitionless type reports 0.
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
            type = GetEnumUnderlyingType(type);
            if (!type) return 0;
            break;
        case TypeKind::ModOpt:
        case TypeKind::ModReq:
            return GetSize(SkipModifiers(*type));
        default:
            break;
    }
    // The C# reads type.GetDefinition(); the minimal-port synthetic types carry
    // no definition, so the KnownType wrapper's own code falls back beside it
    // (the GetSign dual-shape convention).
    const ITypeDefinition* typeDef = type->GetDefinition();
    KnownTypeCode code = typeDef != nullptr ? typeDef->KnownTypeCode()
                                            : KnownTypeCode::None;
    if (code == KnownTypeCode::None)
    {
        if (const auto* k = dynamic_cast<const KnownType*>(type))
            code = k->Code();
        if (code == KnownTypeCode::None) return 0;
    }
    switch (code) {
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

// Port of TypeUtils.GetSize(StackType) (TypeUtils.cs line 90): the size in bytes
// of an evaluation-stack type -- 4 for I4, 8 for I8, NativeIntSize for I and
// Ref, 0 for everything else.
inline int GetSize(IL::StackType stackType) {
    switch (stackType) {
        case IL::StackType::I4:
            return 4;
        case IL::StackType::I8:
            return 8;
        case IL::StackType::I:
        case IL::StackType::Ref:
            return kNativeIntSize;
        default:
            return 0;
    }
}

// Port of TypeUtils.GetStackType(IType) (TypeUtils.cs line 263): the evaluation-
// stack type corresponding to a type. Unknown -> O or Unknown (by its
// IsReferenceType state); ByReference -> Ref; Pointer/NInt/NUInt/FunctionPointer
// -> I; TypeParameter -> O (always, even when instantiated with a primitive);
// ModOpt/ModReq -> the unwrapped element's stack type (SkipModifiers); every
// other kind resolves through GetEnumUnderlyingType().GetDefinition()'s
// KnownTypeCode: the 4-byte-or-less integer/enum family (Boolean through
// UInt32, Char included) -> I4, Int64/UInt64 -> I8, Single -> F4, Double -> F8,
// Void -> Void, IntPtr/UIntPtr -> I, everything else (definitions, classes,
// tuples, ...) -> O. A definitionless type (type parameters already handled,
// open generics) yields O.
inline IL::StackType GetStackType(const IType& type) {
    using ILSpy::Decompiler::IL::StackType;
    switch (type.Kind()) {
        case TypeKind::Unknown:
            if (IsReferenceType(&type) == true)
                return StackType::O;
            return StackType::Unknown;
        case TypeKind::ByReference:
            return StackType::Ref;
        case TypeKind::Pointer:
        case TypeKind::NInt:
        case TypeKind::NUInt:
        case TypeKind::FunctionPointer:
            return StackType::I;
        case TypeKind::TypeParameter:
            return StackType::O;
        case TypeKind::ModOpt:
        case TypeKind::ModReq:
            if (const IType* skipped = SkipModifiers(type))
                return GetStackType(*skipped);
            return StackType::O;
        default:
            break;
    }
    // The KnownType stand-in (the reader/fixture type carrying its
    // KnownTypeCode directly, with no ITypeDefinition behind it) resolves
    // through its Code() -- the same answer the IsKnownType helper gives for
    // the reader stand-in.
    if (auto* known = dynamic_cast<const KnownType*>(&type)) {
        switch (known->Code()) {
            case KnownTypeCode::Boolean:
            case KnownTypeCode::Char:
            case KnownTypeCode::SByte:
            case KnownTypeCode::Byte:
            case KnownTypeCode::Int16:
            case KnownTypeCode::UInt16:
            case KnownTypeCode::Int32:
            case KnownTypeCode::UInt32:
                return StackType::I4;
            case KnownTypeCode::Int64:
            case KnownTypeCode::UInt64:
                return StackType::I8;
            case KnownTypeCode::Single:
                return StackType::F4;
            case KnownTypeCode::Double:
                return StackType::F8;
            case KnownTypeCode::Void:
                return StackType::Void;
            case KnownTypeCode::IntPtr:
            case KnownTypeCode::UIntPtr:
                return StackType::I;
            default:
                return StackType::O;
        }
    }
    const ITypeDefinition* typeDef = nullptr;
    if (const IType* underlying = GetEnumUnderlyingType(&type))
        typeDef = underlying->GetDefinition();
    if (typeDef == nullptr)
        return StackType::O;
    switch (typeDef->KnownTypeCode()) {
        case KnownTypeCode::Boolean:
        case KnownTypeCode::Char:
        case KnownTypeCode::SByte:
        case KnownTypeCode::Byte:
        case KnownTypeCode::Int16:
        case KnownTypeCode::UInt16:
        case KnownTypeCode::Int32:
        case KnownTypeCode::UInt32:
            return StackType::I4;
        case KnownTypeCode::Int64:
        case KnownTypeCode::UInt64:
            return StackType::I8;
        case KnownTypeCode::Single:
            return StackType::F4;
        case KnownTypeCode::Double:
            return StackType::F8;
        case KnownTypeCode::Void:
            return StackType::Void;
        case KnownTypeCode::IntPtr:
        case KnownTypeCode::UIntPtr:
            return StackType::I;
        default:
            return StackType::O;
    }
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
// C# does not consider bool, char or enums to be small integers. The C# reads
// type.GetDefinition()?.KnownTypeCode (the definition-vs-wrapper dispatch -- a
// real CorlibTypeDefinition/MetadataTypeDefinition and the minimal KnownType
// wrapper both reach the same switch, the GetSize dual-shape convention).
// The compound-assignment validation consults this to decide whether a
// small-integer LHS requires the binary to be signed (C# numeric-promotes a
// small integer to int).
inline bool IsCSharpSmallIntegerType(const IType* type) {
    const ITypeDefinition* definition = type != nullptr ? type->GetDefinition() : nullptr;
    KnownTypeCode code = definition != nullptr ? definition->KnownTypeCode()
                                               : KnownTypeCode::None;
    if (code == KnownTypeCode::None)
    {
        if (const auto* k = dynamic_cast<const KnownType*>(type))
            code = k->Code();
    }
    switch (code) {
        case KnownTypeCode::Byte:
        case KnownTypeCode::SByte:
        case KnownTypeCode::Int16:
        case KnownTypeCode::UInt16:
            return true;
        default:
            return false;
    }
}

// Port of TypeUtils.IsCSharpNativeIntegerType(IType) (TypeUtils.cs line 147):
// whether the type is a C# 9 native integer type -- nint or nuint (the synthetic
// TypeKind values; the C# `switch (type.Kind)` over the two kinds with `false` for
// everything else). Returns false for (U)IntPtr (the managed wrappers are Struct
// kinds, not NInt/NUInt) -- the doc comment's explicit distinction. A null input
// yields false (the file's nullable-pointer convention).
inline bool IsCSharpNativeIntegerType(const IType* type) {
    if (!type) return false;
    switch (type->Kind()) {
        case TypeKind::NInt:
        case TypeKind::NUInt:
            return true;
        default:
            return false;
    }
}

// Port of TypeUtils.IsCSharpPrimitiveIntegerType(IType) (TypeUtils.cs line 164):
// whether the type is a C# primitive integer type -- byte, sbyte, short, ushort,
// int, uint, long or ulong (the `switch (type.GetDefinition()?.KnownTypeCode)`
// over the eight codes with `false` for everything else). Unlike the ILAst, C#
// does not consider bool, enums, pointers or IntPtr to be integers. A null input
// or a definitionless type yields false (the C# null-conditional `?.` maps the
// missing definition to the switch default; the file's nullable-pointer
// convention). The TypeSystemAstBuilder ConvertConstantValue literal path consults
// this for the PrintIntegralValuesAsHex hexadecimal-literal gate.
inline bool IsCSharpPrimitiveIntegerType(const IType* type) {
    const ITypeDefinition* definition = type != nullptr ? type->GetDefinition() : nullptr;
    if (definition == nullptr)
        return false;
    switch (definition->KnownTypeCode()) {
        case KnownTypeCode::Byte:
        case KnownTypeCode::SByte:
        case KnownTypeCode::Int16:
        case KnownTypeCode::UInt16:
        case KnownTypeCode::Int32:
        case KnownTypeCode::UInt32:
        case KnownTypeCode::Int64:
        case KnownTypeCode::UInt64:
            return true;
        default:
            return false;
    }
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

// Port of TypeUtils.IsCompatibleTypeForMemoryAccess(IType, IType) (TypeUtils.cs
// line 241): whether reading/writing an element of accessType from the pointer
// is equivalent to reading/writing an element of memoryType. The C# normalizes
// both inputs through NormalizeTypeVisitor.TypeErasure before comparing (an
// object->dynamic / tuple->underlying-type erasure, not just this type).
// NON-CONST inputs: `IType::AcceptVisitor` is non-const (the D406 convention).
inline bool IsCompatibleTypeForMemoryAccess(IType& memoryType, IType& accessType) {
    ITypePtr memory = memoryType.AcceptVisitor(NormalizeTypeVisitor::TypeErasure());
    ITypePtr access = accessType.AcceptVisitor(NormalizeTypeVisitor::TypeErasure());
    if (memory->Equals(*access))
        return true;
    // If the types are not equal, the access still might produce equal results in some cases:
    // 1) Both types are reference types
    if (IsReferenceType(memory.get()) == std::optional<bool>(true)
        && IsReferenceType(access.get()) == std::optional<bool>(true))
        return true;
    // 2) Both types are integer types of equal size
    IL::StackType memoryStackType = GetStackType(*memory);
    IL::StackType accessStackType = GetStackType(*access);
    if (memoryStackType == accessStackType && IL::IsIntegerType(memoryStackType)
        && GetSize(memory.get()) == GetSize(access.get()))
        return true;
    // 3) Any of the types is unknown: we assume they are compatible.
    return memory->Kind() == TypeKind::Unknown || access->Kind() == TypeKind::Unknown;
}

// Port of TypeUtils.IsCompatiblePointerTypeForMemoryAccess(IType, IType)
// (TypeUtils.cs line 223): whether reading/writing an element of accessType
// from the pointer is equivalent to reading/writing an element of the
// pointer's element type. The C# `((TypeWithElementType)pointerType).
// ElementType` cast ports to the PointerType/ByReferenceType dynamic-cast pair
// (the port carries the two leaf classes with no shared base); a type that is
// neither answers false before the memory-access comparison. NON-CONST inputs
// (the IsCompatibleTypeForMemoryAccess convention).
inline bool IsCompatiblePointerTypeForMemoryAccess(IType& pointerType,
                                                   IType& accessType) {
    ITypePtr memoryType;
    if (auto* ptr = dynamic_cast<PointerType*>(&pointerType)) {
        memoryType = ptr->Element();
    } else if (auto* byRef = dynamic_cast<ByReferenceType*>(&pointerType)) {
        memoryType = byRef->Element();
    } else {
        return false;
    }
    return IsCompatibleTypeForMemoryAccess(*memoryType, accessType);
}

} // namespace ILSpy::Decompiler::TypeSystem
