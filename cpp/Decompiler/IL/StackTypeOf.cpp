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

namespace ILSpy::Decompiler::IL {

using namespace ILSpy::Decompiler::TypeSystem;

StackType StackTypeOf(const ITypePtr& type) {
    return StackTypeOf(type.get());
}

StackType StackTypeOf(const IType* type) {
    if (!type) return StackType::Unknown;
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
    return StackType::O;
}

} // namespace ILSpy::Decompiler::IL
