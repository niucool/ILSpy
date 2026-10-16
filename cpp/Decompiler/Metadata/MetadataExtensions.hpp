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
// members ported). ToILNameString renders a FullTypeName in ILAsm syntax -- the shape
// the ReflectionDisassembler/EntityHandle.WriteTo catch-type and member-name
// rendering, the SortByNameProcessor sort keys, and the IL-view name output
// consume. The strong-name assembly-identity family landed next:
// CalculatePublicKeyToken (the SHA-1 public-key-token derivation),
// GetPublicKeyToken / GetFullAssemblyName over the assembly definition (the
// reader extensions), GetFullAssemblyName over an AssemblyReference row, and
// the two TryGetFullAssemblyName forms. The minimalCorlibTypeProvider (the
// static TypeProvider over a SimpleCompilation(MinimalCorlib.Instance) the
// NullableContext / NullablePublicOnly / DefaultMember attribute-value decoders
// consume through CustomAttribute.DecodeValue) is the latest slice. The
// remaining MetadataExtensions members (ToHexString over a BlobReader,
// AppendHexString, and GetTopLevelTypeDefinitions -- the last already ported
// in TypeSystemExtensions) land with the regions that consume them.

#pragma once

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Forward declaration (the .cpp includes the full header): the return type
// of the minimal-corlib accessors below.
namespace ILSpy::Decompiler::TypeSystem { class TypeProvider; }

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

// The C# `internal static readonly TypeProvider minimalCorlibTypeProvider =
//     new TypeProvider(new SimpleCompilation(MinimalCorlib.Instance))`
// (MetadataExtensions.cs lines 215-216) and its two public accessors:
// `public static ICustomAttributeTypeProvider<IType>
// MinimalAttributeTypeProvider { get => minimalCorlibTypeProvider; }` and
// `public static ISignatureTypeProvider<IType, TypeSystem.GenericContext>
// MinimalSignatureTypeProvider { get => minimalCorlibTypeProvider; }` -- one
// process-lifetime provider over a compilation of nothing but MinimalCorlib,
// usable to decode attribute signatures and other metadata blobs that only
// mention built-in types (the GetNullableContext /
// FindMinimumAccessibilityForNRT / GetDefaultMemberName consumers drive it
// through CustomAttribute.DecodeValue). The C# properties are typed as the
// SRM interfaces; the port returns the concrete TypeProvider (the attribute
// half of its surface ports as plain methods -- the TypeProvider.hpp
// convention (a), until the custom-attribute decoder slice ports the
// interface). Both accessors return the SAME provider (the C# static field
// behind both properties).

// The C# `static string CalculatePublicKeyToken(BlobHandle blob,
// MetadataReader reader)` (MetadataExtensions.cs): the strong-name public-key
// token of the blob at the raw #Blob heap offset -- the SHA-1 digest of the
// payload bytes, its last 8 bytes reversed, rendered as lowercase hex. The
// hash runs through the vendored Util/Sha1ForNonSecretPurposes (ECMA-335
// defines the token as a SHA-1 digest, so the managed non-secret implementation
// keeps the read independent of the host crypto policy). The C# BlobHandle
// parameter ports as the raw heap offset (the handle's offset value); the blob
// reads through MetadataFile::CorBlob, which throws std::invalid_argument for
// an offset past the heap (the BadImageFormatException 'Read out of bounds.'
// analog).
std::string CalculatePublicKeyToken(const MetadataFile& file,
                                    std::uint32_t blobOffset);

// The C# `public static string GetPublicKeyToken(this MetadataReader reader)`
// (MetadataExtensions.cs): the assembly definition's public-key token -- the
// empty string for a netmodule (no Assembly table), "null" for a nil PublicKey
// column, the calculated token otherwise. Note the C# comment: the
// AssemblyFlags.PublicKey bit does not apply to assembly definitions -- the
// column is always hashed, never read as a ready-made token.
std::string GetPublicKeyToken(const MetadataFile& file);

// The C# `public static string GetFullAssemblyName(this MetadataReader reader)`
// (MetadataExtensions.cs): the display name of the assembly definition --
// "<Name>, Version=<M.m.b.r>, Culture=<culture or neutral>,
// PublicKeyToken=<token or null>"; the empty string for a netmodule. The reads
// go through the raw table surface, so a corrupt row (a heap offset past the
// heap) throws std::invalid_argument where the C# throws BadImageFormatException
// -- the TryGetFullAssemblyName forms catch it.
std::string GetFullAssemblyName(const MetadataFile& file);

// The C# `public static string GetFullAssemblyName(this SRM.AssemblyReference
// reference, MetadataReader reader)` (MetadataExtensions.cs): the display
// name of the AssemblyReference row -- the same geometry as the assembly
// definition form plus the Retargetable=true tail, with the PublicKey flag
// selecting the full-key SHA-1 token over the plain token bytes (an AssemblyRef
// carrying the PublicKey bit stores a FULL public key in the PublicKeyOrToken
// column). The C# reference parameter ports as the row's raw token
// (0x23000000 | row -- the GetFullTypeNameFromExportedType convention).
std::string GetFullAssemblyName(const MetadataFile& file,
                                std::uint32_t assemblyReferenceToken);

// The C# `public static bool TryGetFullAssemblyName(this MetadataReader reader,
// out string assemblyName)` and the SRM.AssemblyReference variant
// (MetadataExtensions.cs): the try forms over the two GetFullAssemblyName
// overloads. The C# out-parameter-plus-bool pair ports to
// std::optional<std::string> (nullopt = the false result); the catch narrows to
// the BadImageFormatException family -- std::invalid_argument (the winmd
// seek/row throws) and std::out_of_range (the port's own reader throws).
std::optional<std::string> TryGetFullAssemblyName(const MetadataFile& file);
std::optional<std::string> TryGetFullAssemblyName(
    const MetadataFile& file, std::uint32_t assemblyReferenceToken);

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

// The C# `public static PrimitiveTypeCode ToPrimitiveTypeCode(this
// KnownTypeCode typeCode)` (MetadataExtensions.cs line 234): the inverse
// mapping -- the signature-blob primitive code for a known type, 0 (the C#
// `default` value / `Unknown`) for everything that is not a primitive. The
// TypeProvider's `GetUnderlyingEnumType` (the attribute-decoder arm) consumes
// it: an enum's underlying known type code renders back into its
// ELEMENT_TYPE byte.
PrimitiveTypeCode ToPrimitiveTypeCode(TypeSystem::KnownTypeCode typeCode);

// The two minimal-corlib accessors (the header comment above the namespace):
// the C# `MinimalAttributeTypeProvider` / `MinimalSignatureTypeProvider`
// properties. Both return the same process-lifetime provider (the C# static
// field); thread-safe first-use initialization (the C# static-ctor semantics
// via the magic-static). The reference is non-const: the C# signature-provider
// interface members are non-const (the port's TypeProvider matches), so a
// const reference could not drive them.
TypeSystem::TypeProvider& MinimalAttributeTypeProvider();
TypeSystem::TypeProvider& MinimalSignatureTypeProvider();

} // namespace ILSpy::Decompiler::Metadata
