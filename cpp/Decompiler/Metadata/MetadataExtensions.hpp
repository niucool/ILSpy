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

// Port of ICSharpCode.Decompiler/Metadata/MetadataExtensions.cs (the first
// member). ToILNameString renders a FullTypeName in ILAsm syntax -- the shape
// the ReflectionDisassembler/EntityHandle.WriteTo catch-type and member-name
// rendering, the SortByNameProcessor sort keys, and the IL-view name output
// consume. The remaining MetadataExtensions members (CalculatePublicKeyToken,
// ToHexString, GetTopLevelTypeDefinitions -- the last already ported in
// TypeSystemExtensions -- and the minimalCorlibTypeProvider) land with the
// regions that consume them.

#pragma once

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <string>

namespace ILSpy::Decompiler::Metadata {

// The C# `public static string ToILNameString(this FullTypeName typeName,
// bool omitGenerics = false)` (MetadataExtensions.cs) -- the ILAsm type-name
// rendering:
//   * top-level: "Namespace.Name" (+"`N" for the total arity when
//     !omitGenerics);
//   * nested: the declaring type's rendering, '/', the innermost name
//     (+"`N" for that segment's own additional type-parameter count when
//     !omitGenerics), recursing through FullTypeName.GetDeclaringType so each
//     segment carries its own arity;
//   * the composed top-level name and each nested name pass through
//     DisassemblerHelpers.Escape (an ILAsm-keyword name renders quoted).
std::string ToILNameString(const TypeSystem::FullTypeName& typeName, bool omitGenerics = false);

// The C# `public static KnownTypeCode ToKnownTypeCode(this PrimitiveTypeCode
// typeCode)` (MetadataExtensions.cs): the known-type code a signature blob's
// primitive element type resolves to, None for the codes with no known-type
// equivalent. The FullTypeNameSignatureDecoder's primitive arm consumes it.
TypeSystem::KnownTypeCode ToKnownTypeCode(PrimitiveTypeCode typeCode);

} // namespace ILSpy::Decompiler::Metadata
