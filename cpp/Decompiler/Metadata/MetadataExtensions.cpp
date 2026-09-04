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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Decompiler/Metadata/MetadataExtensions.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

namespace ILSpy::Decompiler::Metadata {

using Disassembler::Escape;
using TypeSystem::FullTypeName;
using TypeSystem::KnownTypeCode;

KnownTypeCode ToKnownTypeCode(PrimitiveTypeCode typeCode)
{
    switch (typeCode) {
        case PrimitiveTypeCode::Boolean: return KnownTypeCode::Boolean;
        case PrimitiveTypeCode::Byte: return KnownTypeCode::Byte;
        case PrimitiveTypeCode::SByte: return KnownTypeCode::SByte;
        case PrimitiveTypeCode::Char: return KnownTypeCode::Char;
        case PrimitiveTypeCode::Int16: return KnownTypeCode::Int16;
        case PrimitiveTypeCode::UInt16: return KnownTypeCode::UInt16;
        case PrimitiveTypeCode::Int32: return KnownTypeCode::Int32;
        case PrimitiveTypeCode::UInt32: return KnownTypeCode::UInt32;
        case PrimitiveTypeCode::Int64: return KnownTypeCode::Int64;
        case PrimitiveTypeCode::UInt64: return KnownTypeCode::UInt64;
        case PrimitiveTypeCode::Single: return KnownTypeCode::Single;
        case PrimitiveTypeCode::Double: return KnownTypeCode::Double;
        case PrimitiveTypeCode::IntPtr: return KnownTypeCode::IntPtr;
        case PrimitiveTypeCode::UIntPtr: return KnownTypeCode::UIntPtr;
        case PrimitiveTypeCode::Object: return KnownTypeCode::Object;
        case PrimitiveTypeCode::String: return KnownTypeCode::String;
        case PrimitiveTypeCode::TypedReference: return KnownTypeCode::TypedReference;
        case PrimitiveTypeCode::Void: return KnownTypeCode::Void;
        default: return KnownTypeCode::None;
    }
}

std::string ToILNameString(const FullTypeName& typeName, bool omitGenerics)
{
    std::string name;
    if (typeName.IsNested())
    {
        name = typeName.Name();
        if (!omitGenerics)
        {
            int localTypeParameterCount =
                typeName.GetNestedTypeAdditionalTypeParameterCount(typeName.NestingLevel() - 1);
            if (localTypeParameterCount > 0)
                name += "`" + std::to_string(localTypeParameterCount);
        }
        name = Escape(name);
        return ToILNameString(typeName.GetDeclaringType(), omitGenerics) + "/" + name;
    }
    if (!typeName.GetTopLevelTypeName().Namespace().empty())
    {
        name = typeName.GetTopLevelTypeName().Namespace() + "." + typeName.Name();
        if (!omitGenerics && typeName.TypeParameterCount() > 0)
            name += "`" + std::to_string(typeName.TypeParameterCount());
    }
    else
    {
        name = typeName.Name();
        if (!omitGenerics && typeName.TypeParameterCount() > 0)
            name += "`" + std::to_string(typeName.TypeParameterCount());
    }
    return Escape(name);
}

} // namespace ILSpy::Decompiler::Metadata
