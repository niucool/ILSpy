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
#include "Decompiler/TypeSystem/TypeKind.hpp"

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

} // namespace ILSpy::Decompiler::TypeSystem
