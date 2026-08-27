// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of `ReflectionHelper.GetTypeCode` -- see the header.

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"

#include "Decompiler/TypeSystem/IType.hpp"  // IType
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (KnownTypeCode)

namespace ILSpy::Decompiler::TypeSystem {

TypeCode GetTypeCode(const IType& type) {
    const ITypeDefinition* def = dynamic_cast<const ITypeDefinition*>(&type);
    if (def != nullptr) {
        const KnownTypeCode typeCode = def->KnownTypeCode();
        if (typeCode <= KnownTypeCode::String && typeCode != KnownTypeCode::Void) {
            // The C# `(TypeCode)typeCode` -- a numeric cast (the KnownTypeCode values 0-17 align with
            // TypeCode 0-17 by construction; the guard excludes Void and anything past String).
            return static_cast<TypeCode>(typeCode);
        }
        return TypeCode::Empty;
    }
    return TypeCode::Empty;
}

} // namespace ILSpy::Decompiler::TypeSystem
