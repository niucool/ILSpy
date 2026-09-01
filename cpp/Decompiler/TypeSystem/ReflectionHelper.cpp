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

// Port of `ReflectionHelper.GetTypeCode` + `ReflectionHelper.FindType(ICompilation, TypeCode)`
// -- see the header.

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (FindType's compilation)
#include "Decompiler/TypeSystem/IType.hpp"  // IType
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (KnownTypeCode)
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"  // TopLevelTypeName (SplitTypeParameterCount)

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

// The C# `public static IType FindType(this ICompilation compilation, TypeCode typeCode)`
// (ReflectionHelper.cs line 106): `return compilation.FindType((KnownTypeCode)typeCode);` -- a
// straight numeric cast forwarded to the interface lookup. The `KnownTypeCode` values 0-17
// align with `TypeCode` 0-17 by construction (`None` <-> `Empty`, the rest identity), so the
// cast is faithful for every `TypeCode` value.
const IType& FindType(const ICompilation& compilation, TypeCode typeCode) {
    return compilation.FindType(static_cast<KnownTypeCode>(typeCode));
}

// The C# `public static string SplitTypeParameterCountFromReflectionName(string)`
// (ReflectionHelper.cs line 66): the position of the LAST '`'; no backtick -> the name
// as-is; else the prefix before it. Unlike the 2-arg overload this strip is
// UNCONDITIONAL -- the digits after the backtick are never checked.
std::string SplitTypeParameterCountFromReflectionName(std::string_view reflectionName) {
    auto pos = reflectionName.rfind('`');
    if (pos == std::string_view::npos) return std::string(reflectionName);
    return std::string(reflectionName.substr(0, pos));
}

// The C# `public static string SplitTypeParameterCountFromReflectionName(string, out int)`
// (ReflectionHelper.cs line 83): delegate to the existing
// TopLevelTypeName::SplitTypeParameterCount (the same strip-only-when-the-tail-parses
// semantics, already mirrored for TopLevelTypeName's reflection-name ctor).
std::string SplitTypeParameterCountFromReflectionName(std::string_view reflectionName,
                                                       int& typeParameterCount) {
    return TopLevelTypeName::SplitTypeParameterCount(reflectionName, typeParameterCount);
}

} // namespace ILSpy::Decompiler::TypeSystem
