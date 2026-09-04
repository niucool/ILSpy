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
#include <string>

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"  // SignatureCallingConvention

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
// (0x1B) delegates to GetFullTypeNameFromSpecification below; any other kind
// throws std::out_of_range (the C# ArgumentOutOfRangeException). A nil token
// throws std::invalid_argument.
TypeSystem::FullTypeName GetFullTypeName(const MetadataFile& metadata, std::uint32_t entityToken);

// The C# `public static FullTypeName GetFullTypeName(this
// TypeSpecificationHandle handle, MetadataReader reader)` (SRMExtensions.cs):
// the TypeSpec row's signature blob decoded through the
// FullTypeNameSignatureDecoder -- the shrinking provider whose
// GetGenericInstantiation returns only the generic head, whose
// array/pointer/byref/pinned/cmod arms strip to the element type, and whose
// TypeDef/TypeRef arms reuse the name readers above. The blob is passed
// directly (the GetTypeSpecSignatureBlob read) so the walk can be driven over
// synthetic bytes in the tests. VAR/MVAR/FNPTR positions decode to the empty
// FullTypeName (the C# default(FullTypeName)); a malformed blob throws
// std::out_of_range (the C# BadImageFormatException), an unrecognized element
// type std::logic_error (the C# throws on the undecodable byte).
TypeSystem::FullTypeName GetFullTypeNameFromSpecification(
    const MetadataFile& metadata, const std::uint8_t* data, std::size_t size);

// The C# `public static EntityHandle GetDeclaringType(this EntityHandle
// entity, MetadataReader metadata)` (SRMExtensions.cs): the declaring type
// of a member entity, as a raw token (0 = the nil handle -- a top-level
// TypeDef/TypeRef). The ported arms: TypeDefinition (the NestedClass walk),
// TypeReference (the resolution-scope walk), FieldDefinition,
// MethodDefinition, MemberReference (mr.Parent), and MethodSpecification
// (recursing into the underlying method). The TypeSpecification
// (GetGenericType's blob-head parse), Event, and Property (the MethodSemantics
// accessor walk) arms defer -- no ported consumer reaches them (a
// CustomAttribute's constructor is always a MethodDef or MemberRef, the only
// caller's domain), so they land in the default throw. A nil token throws
// std::invalid_argument (the C# ArgumentNullException); an unsupported kind
// throws std::out_of_range (the C# ArgumentOutOfRangeException).
std::uint32_t GetDeclaringType(const MetadataFile& metadata,
                               std::uint32_t entityToken);

// The C# `public static string ToILSyntax(this SignatureCallingConvention
// callConv)` (SRMExtensions.cs line 783) -- the ILAsm calling-convention
// spelling the SignatureHeader.WriteTo writer (IL/InstructionOutputExtensions)
// and ILAmbience consume: "default" / "unmanaged cdecl" / "unmanaged stdcall"
// / "unmanaged thiscall" / "unmanaged fastcall" / "vararg" / "unmanaged";
// any other convention renders its enum name lower-cased (the C# `ToString().
// ToLowerInvariant()` fallback, unreachable for the 7 ported members).
std::string ToILSyntax(SignatureCallingConvention callConv);

// The C# `sealed class FieldValueSizeDecoder : ISignatureTypeProvider<int,
// GenericContext>` (SRMExtensions.cs -- the private class behind
// GetInitialValue): the provider that decodes a FIELD signature to the byte
// SIZE of the field's initial value -- the `.data` blob length the
// ReflectionDisassembler.DisassembleField HasFieldRVA arm reads through
// GetFieldInitialValue. A nested private class in the C#, lifted public
// here (the tests drive it over synthetic signature blobs through the
// SignatureTypeProviderDecoder, the same walker GetFieldInitialValue
// drives).
//
// The port models the null-typeSystem shape only (the C# ctor's `module`
// field stays null): the pointer size is IntPtr.Size of the x64 process (8),
// and GetTypeFromReference reads 0 (the typeSystem-bearing TypeRef resolution
// and the PE32 4-byte pointer arm defer with the type system -- the CLI
// never reaches them).
//
// The size arms, from the C#:
//  * primitives -- the Boolean(1)/Char(2)/Int32(4)/Int64(8)/IntPtr(8) table,
//    Void/String/TypedReference/Object 0 (no inline initial value);
//  * Array/SZArray -- GetPrimitiveType(Object), i.e. 0;
//  * Ptr/ByRef/FnPtr -- the pointer size;
//  * GenericInstantiation -- the generic HEAD's size;
//  * VAR/MVAR -- 0;
//  * cmod/pinned -- the unmodified/element size;
//  * TypeDef -- the ClassLayout ClassSize (GetTypeLayoutSize);
//  * TypeRef -- 0 (null typeSystem);
//  * TypeSpec -- the row's blob decoded through this same provider.
class FieldValueSizeDecoder final : public ISignatureTypeProvider<int> {
public:
    // The provider's result type (the walker's TType).
    using TType = int;

    explicit FieldValueSizeDecoder(const MetadataFile& module);

    int GetPrimitiveType(PrimitiveTypeCode typeCode) override;
    int GetTypeFromDefinition(std::uint32_t typeDefToken,
        std::uint8_t rawTypeKind) override;
    int GetTypeFromReference(std::uint32_t typeRefToken,
        std::uint8_t rawTypeKind) override;
    int GetTypeFromSpecification(std::uint32_t typeSpecToken,
        std::uint8_t rawTypeKind,
        const MetadataGenericContext& genericContext) override;
    int GetSZArrayType(int elementType) override;
    int GetPointerType(int elementType) override;
    int GetByReferenceType(int elementType) override;
    int GetPinnedType(int elementType) override;
    int GetArrayType(int elementType, const ArrayShape& shape) override;
    int GetGenericInstantiation(int genericType,
        std::vector<int> typeArguments) override;
    int GetGenericTypeParameter(const MetadataGenericContext& genericContext,
        int index) override;
    int GetGenericMethodParameter(const MetadataGenericContext& genericContext,
        int index) override;
    int GetModifiedType(int modifier, int unmodifiedType,
        bool isRequired) override;
    int GetFunctionPointerType(
        const ProviderMethodSignature<int>& signature) override;

private:
    const MetadataFile& module_;
    // IntPtr.Size under the null-typeSystem shape -- the x64 process.
    int pointerSize_ = 8;
};

} // namespace ILSpy::Decompiler::Metadata
