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
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

// Forward declaration of the attribute-classification enum (KnownAttribute.hpp,
// already ported) -- a scoped enum with a fixed underlying type is
// opaque-declarable (`enum class KnownAttribute : int;`), so the predicate
// family below needs only the declaration (the header-include-graph minimal
// convention).
namespace ILSpy::Decompiler::TypeSystem { enum class KnownAttribute : int; }

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

// The C# `public static FullTypeName GetFullTypeName(this ExportedType type,
// MetadataReader metadata)` (SRMExtensions.cs line 461): an ExportedType
// (table 0x27) row's full name -- the row's own name split through
// ReflectionHelper.SplitTypeParameterCountFromReflectionName, then either
// the top-level form (ns + name, the row's own TypeNamespace column) or, when
// the row's Implementation column targets ANOTHER ExportedType row (the
// nested-forwarder chain the .NET facades carry), the outer row's full name
// with this row nested inside it. The key
// MetadataFile.GetTypeForwarder(FullTypeName) builds its reverse lookup
// from. The C# overload takes the ExportedType ROW struct (never nil), so
// the port's token-shaped entry adds the reader-family nil contract itself:
// std::invalid_argument for a nil token, std::out_of_range for an invalid
// row (the GetFullTypeNameFromReference/Definition convention). The cyclic
// chain the C# would infinitely recurse on is capped at the family's
// kMaxNestingWalkDepth (a documented divergence confined to corrupt
// metadata).
TypeSystem::FullTypeName GetFullTypeNameFromExportedType(
    const MetadataFile& metadata, std::uint32_t exportedTypeToken);

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

// The C# `public static bool IsKnownType(this EntityHandle handle,
// MetadataReader reader, KnownTypeCode knownType)` (SRMExtensions.cs line
// 246): whether the entity names the known type -- a TypeRef in the
// referenced assembly (a Module/ModuleRef/AssemblyRef scope; nested and
// nil-scoped refs are rejected), a top-level TypeDef (nested types are
// rejected), or a TypeSpec naming it (the SignatureIsKnownType blob walk,
// the primitive comparisons plus the cmod/GENERICINST recursions and the
// CLASS/VALUETYPE coded-index recursion). Every other kind is false, and
// every row read happens under the C# `catch (BadImageFormatException)`
// (a corrupt row is FALSE, not a throw). The port takes the raw entity
// token (0x01 / 0x02 / 0x1B / ...; nil is false). KnownTypeCode::None has
// no KnownTypeReference (the C# `Get(None).TypeName` NREs); the port maps
// it to the standard NRE message.
bool IsKnownType(const MetadataFile& metadata, std::uint32_t entityToken,
                 TypeSystem::KnownTypeCode knownType);

// The C# `internal static bool IsKnownType(this EntityHandle handle,
// MetadataReader reader, KnownAttribute knownType)` (SRMExtensions.cs line
// 252): the KnownAttribute variant over the same core -- the attribute's
// type name (KnownAttribute::GetTypeName) against the entity.
bool IsKnownType(const MetadataFile& metadata, std::uint32_t entityToken,
                 TypeSystem::KnownAttribute knownAttribute);

// The C# `public static EntityHandle GetAttributeType(this SRM.CustomAttribute
// attribute, MetadataReader reader)` (SRMExtensions.cs line 606): the
// attribute's constructor's declaring type -- a MethodDef constructor's
// declaring TypeDef, or a MemberRef constructor's MemberRefParent. The C#
// takes the CustomAttribute ROW; the port takes the row's token (the
// GetCustomAttribute read; a bogus token throws std::out_of_range, the
// reader-family row-fetch convention). Any other constructor kind throws
// std::out_of_range carrying the exact C# BadImageFormatException message
// (the HandleKind name rendered for the token's table byte, decimal for a
// kind outside the enum). A bogus ctor ROW (a reserved coded-index tag) read
// as a nil token by GetCustomAttribute's never-throw decode lands in that
// throw too -- the C# throws BadImageFormatException from the same shape
// (a documented divergence only in the message's kind spelling).
std::uint32_t GetAttributeType(const MetadataFile& metadata,
                               std::uint32_t attributeToken);

// The C# `public static bool HasKnownAttribute(this
// CustomAttributeHandleCollection customAttributes, MetadataReader metadata,
// KnownAttribute type)` (SRMExtensions.cs line 622): whether any custom
// attribute of the ENTITY is the known attribute -- each row classified
// through GetAttributeType + IsKnownType. The C# takes the collection
// handle; the port takes the PARENT token (the GetCustomAttributeTokens
// composition -- the HasSemantics/GetCustomAttributeTokens convention). An
// attribute row with an unexpected constructor kind propagates
// GetAttributeType's throw (the C# propagates the BadImageFormatException).
bool HasKnownAttribute(const MetadataFile& metadata, std::uint32_t entityToken,
                       TypeSystem::KnownAttribute attribute);

// The C# `public static bool IsValueType(this TypeDefinition typeDefinition,
// MetadataReader reader)` (SRMExtensions.cs line 64): the Extends column is
// System.Enum, or it is System.ValueType and this type is not System.Enum
// itself. Takes the raw TypeDef token.
bool IsValueType(const MetadataFile& metadata, std::uint32_t typeDefToken);

// The C# `public static bool IsEnum(this TypeDefinition typeDefinition,
// MetadataReader reader)` (SRMExtensions.cs line 82): the Extends column is
// System.Enum.
bool IsEnum(const MetadataFile& metadata, std::uint32_t typeDefToken);

// The C# `public static bool IsEnum(this TypeDefinition typeDefinition,
// MetadataReader reader, out PrimitiveTypeCode underlyingType)`
// (SRMExtensions.cs line 96): the Extends column is System.Enum, and the
// first non-static field's FIELD-signature blob names the underlying
// primitive (the element-type byte after the 0x06 field header; a non-field
// signature header or a truncated byte read is the C# BadImageFormatException
// family, mapped to std::out_of_range). An enum with no instance field is
// FALSE (the C# loop falls through). `underlyingType` is assigned 0 (the
// C# `underlyingType = 0`) before every arm.
bool IsEnum(const MetadataFile& metadata, std::uint32_t typeDefToken,
            PrimitiveTypeCode& underlyingType);

// The C# `public static bool IsDelegate(this TypeDefinition typeDefinition,
// MetadataReader reader)` (SRMExtensions.cs line 124): the Extends column is
// System.MulticastDelegate.
bool IsDelegate(const MetadataFile& metadata, std::uint32_t typeDefToken);

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
    // The provider's generic-context type (the walker's TGenericContext --
    // the disassembler's MetadataGenericContext, the default).
    using TGenericContext = MetadataGenericContext;

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
