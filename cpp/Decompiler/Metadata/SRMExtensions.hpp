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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/SRMExtensions.cs (the GetFullTypeName
// reader family). The C# extension methods resolve an EntityHandle /
// TypeDefinitionHandle / TypeReferenceHandle to a FullTypeName -- the
// name-normalized model the EntityHandle.WriteTo catch-type writer, the
// ReflectionDisassembler member-name rendering, and the SortByNameProcessor
// sort keys consume. The port models SRM handles as 32-bit metadata tokens
// (table id << 24 | 1-based row) and the MetadataReader as the MetadataFile
// (the MetadataGenericContext convention), so the C# handle-typed overloads
// become distinctly named free functions:
//   * GetFullTypeNameFromDefinition -- the TypeDefinitionHandle overload;
//   * GetFullTypeNameFromReference  -- the TypeReferenceHandle overload;
//   * GetFullTypeName               -- the EntityHandle dispatch entry.
// The per-row reads each reader composes (Name/Namespace columns, the
// NestedClass-table and resolution-scope declaring-type walks) land on
// MetadataFile (GetTypeDefNameInfo / GetTypeRefNameInfo -- the pimpl
// constraint: the winmd database lives only in MetadataFile.cpp).

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"

#include <cstdint>

namespace ILSpy::Decompiler::Metadata {

// The C# `public static FullTypeName GetFullTypeName(this TypeReferenceHandle
// handle, MetadataReader reader)` (SRMExtensions.cs): the TypeRef row's name
// split through ReflectionHelper.SplitTypeParameterCountFromReflectionName,
// then either a top-level name (a Module/ModuleRef/AssemblyRef resolution
// scope) or the declaring TypeRef's name with this row nested inside it (the
// resolution-scope walk). Throws std::invalid_argument for a nil token (the
// C# ArgumentNullException) and std::out_of_range for an invalid row (the C#
// SRM row-fetch throw).
TypeSystem::FullTypeName GetFullTypeNameFromReference(const MetadataFile& metadata,
                                                       std::uint32_t typeRefToken);

// The C# `public static FullTypeName GetFullTypeName(this TypeDefinitionHandle
// handle, MetadataReader reader)` (SRMExtensions.cs -- the overload and the
// TypeDefinition-row twin below it share one body shape in the port): the
// TypeDef row's name split through
// ReflectionHelper.SplitTypeParameterCountFromReflectionName, then either a
// top-level name or the declaring TypeDef's name with this row nested inside
// it (the NestedClass-table walk). Same throw contract as the reference
// reader.
TypeSystem::FullTypeName GetFullTypeNameFromDefinition(const MetadataFile& metadata,
                                                       std::uint32_t typeDefToken);

// The C# `public static FullTypeName GetFullTypeName(this EntityHandle handle,
// MetadataReader reader)` (SRMExtensions.cs): dispatch on the token's table --
// TypeDef (0x02) and TypeRef (0x01) delegate to the readers above; TypeSpec
// (0x1B) is deferred on FullTypeNameSignatureDecoder (not yet ported) and
// throws std::logic_error; any other kind throws std::out_of_range (the C#
// ArgumentOutOfRangeException). A nil token throws std::invalid_argument.
TypeSystem::FullTypeName GetFullTypeName(const MetadataFile& metadata, std::uint32_t entityToken);

} // namespace ILSpy::Decompiler::Metadata
