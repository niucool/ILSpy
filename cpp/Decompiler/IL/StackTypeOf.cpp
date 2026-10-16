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

#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

namespace ILSpy::Decompiler::IL {

using namespace ILSpy::Decompiler::TypeSystem;

StackType StackTypeOf(const ITypePtr& type) {
    return StackTypeOf(type.get());
}

StackType StackTypeOf(const IType* type) {
    if (!type) return StackType::Unknown;
    // The C# GetStackType reads the GetEnumUnderlyingType().GetDefinition().
    // KnownTypeCode -- a real definition's code (the CorlibTypeDefinition /
    // MetadataTypeDefinition shapes the minimal KnownType wrapper misses) -- with
    // the minimal-port synthetic wrapper (KnownType) and the decorated types
    // (ByReference/Pointer) checked first.
    if (const auto* k = dynamic_cast<const KnownType*>(type)) {
        switch (k->Code()) {
            case KnownTypeCode::Boolean: case KnownTypeCode::Char:
            case KnownTypeCode::SByte: case KnownTypeCode::Byte:
            case KnownTypeCode::Int16: case KnownTypeCode::UInt16:
            case KnownTypeCode::Int32: case KnownTypeCode::UInt32:
                return StackType::I4;
            case KnownTypeCode::Int64: case KnownTypeCode::UInt64:
                return StackType::I8;
            case KnownTypeCode::Single: return StackType::F4;
            case KnownTypeCode::Double: return StackType::F8;
            case KnownTypeCode::IntPtr: case KnownTypeCode::UIntPtr:
                return StackType::I;
            case KnownTypeCode::Void: return StackType::Void;
            default: return StackType::O;
        }
    }
    if (dynamic_cast<const ByReferenceType*>(type)) return StackType::Ref;
    if (dynamic_cast<const PointerType*>(type)) return StackType::I;  // unmanaged pointer
    if (type->Kind() == TypeKind::Unknown) {
        // The C# GetStackType(TypeKind.Unknown): O only when IsReferenceType == true,
        // else StackType.Unknown (the error-type null object is not a reference type).
        return GetStackType(*type);
    }
    // An enum's evaluation-stack type is its underlying primitive (the C#
    // reads GetEnumUnderlyingType(); the common underlying is Int32 -> I4).
    // A resolved definition carries the underlying; a name-only SimpleType
    // (this port's signature model for a non-known in-module type) does
    // not, and I4 there matches every enum underlying for the consumers --
    // the switch-value widening accepts any of I4/I8, as the C#'s
    // underlying read does for every underlying.
    if (type->Kind() == TypeKind::Enum) {
        if (const ITypeDefinition* def = type->GetDefinition()) {
            if (ITypePtr underlying = def->EnumUnderlyingType())
                return StackTypeOf(underlying);
        }
        return StackType::I4;
    }
    if (const ITypeDefinition* def = type->GetDefinition()) {
        switch (def->KnownTypeCode()) {
            case KnownTypeCode::Boolean: case KnownTypeCode::Char:
            case KnownTypeCode::SByte: case KnownTypeCode::Byte:
            case KnownTypeCode::Int16: case KnownTypeCode::UInt16:
            case KnownTypeCode::Int32: case KnownTypeCode::UInt32:
                return StackType::I4;
            case KnownTypeCode::Int64: case KnownTypeCode::UInt64:
                return StackType::I8;
            case KnownTypeCode::Single: return StackType::F4;
            case KnownTypeCode::Double: return StackType::F8;
            case KnownTypeCode::IntPtr: case KnownTypeCode::UIntPtr:
                return StackType::I;
            case KnownTypeCode::Void: return StackType::Void;
            case KnownTypeCode::None: return StackType::O;
            default: return StackType::O;
        }
    }
    return StackType::O;
}

} // namespace ILSpy::Decompiler::IL
