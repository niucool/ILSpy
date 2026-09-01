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

namespace ILSpy::Decompiler::Metadata {

using Disassembler::Escape;
using TypeSystem::FullTypeName;

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
