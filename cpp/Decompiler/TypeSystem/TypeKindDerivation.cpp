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

#include "Decompiler/TypeSystem/TypeKindDerivation.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// ECMA-335 II.23.1.15: ClassSemanticsMask = 0x20, Interface = 0x20.
constexpr std::uint32_t kClassSemanticsMask = 0x00000020u;

TypeKind DeriveTypeKind(std::uint32_t flags, const ITypePtr& base,
                        std::string_view selfRefName) {
    if ((flags & kClassSemanticsMask) == 0x20u) return TypeKind::Interface;
    if (!base) return TypeKind::Class;  // System.Object: no base type

    // The base resolves to a KnownType for the framework base types (Enum,
    // ValueType, MulticastDelegate), so dispatch on its KnownTypeCode rather
    // than string-comparing reflection names.
    const auto* k = dynamic_cast<const KnownType*>(base.get());
    if (k) {
        switch (k->Code()) {
            case KnownTypeCode::Enum:
                return TypeKind::Enum;
            case KnownTypeCode::ValueType:
                // System.Void is a value type but gets its own kind; System.Enum
                // itself extends ValueType yet is a class.
                if (selfRefName == "System.Void") return TypeKind::Void;
                if (selfRefName == "System.Enum") return TypeKind::Class;
                return TypeKind::Struct;
            case KnownTypeCode::MulticastDelegate:
                return TypeKind::Delegate;
            default:
                return TypeKind::Class;
        }
    }
    return TypeKind::Class;
}

} // namespace ILSpy::Decompiler::TypeSystem
