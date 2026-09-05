// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Metadata/CustomAttributeDecoder.cs -- the
// custom-attribute blob value decoder -- FUSED with the SRM
// System.Reflection.Metadata.Ecma335.CustomAttributeDecoder<TType>.DecodeValue
// entry the C# `attr.DecodeValue(provider)` API drives. The repo's own file is
// a stripped-down copy of SRM's internal decoder ("This is a stripped-down
// copy of SRM's internal CustomAttributeDecoder. We need it to decode security
// declarations."), whose unique member is the provideBoxingTypeInfo boxing of
// TaggedObject arguments; the fixed-argument half (DecodeValue, driven by the
// constructor's signature blob) exists only in the SRM original, which the
// port needs for ApplyAttributeTypeVisitor.ProcessAttribute -- so this one
// class carries both halves faithfully: DecodeValue (the SRM semantics) and
// DecodeNamedArguments (the repo copy's public entry, with the
// provideBoxingTypeInfo flag defaulting to false -- the value at which the
// two implementations are byte-identical).
//
// Consumers:
//   * ApplyAttributeTypeVisitor.ApplyAttributesToType's ProcessAttribute
//     (the Dynamic/NativeInteger/TupleElementNames/Nullable attribute
//     arguments) -- through minimalCorlibTypeProvider.
//   * AttributeListBuilder.ReadBinarySecurityAttribute (the binary security
//     declaration decode, the repo copy's named-args half).
//   * ReflectionDisassembler's SecurityDeclarationDecoder (the
//     provideBoxingTypeInfo=true shape).
//   * MetadataTypeDefinition's attribute snapshots (through
//     module.TypeProvider).
//
// KEY PORT CONVENTIONS:
//  (a) BCL GENERIC ABSORBED AS A CONCRETE CLASS (the CustomAttributeTypedArgument
//      D385 convention): both C# decoders are generic over TType, but the
//      ILSpy type system uses only the IType instantiation (the one
//      exception -- ReflectionDisassembler's SecurityDeclarationDecoder
//      instantiates the repo copy with a (PrimitiveTypeCode, string) tuple --
//      is deferred with that slice); the port therefore absorbs <ITypePtr>
//      here, over the port's TypeProvider (both the module-backed and the
//      MinimalAttributeTypeProvider shapes satisfy the same class).
//  (b) The C# takes the decoding MetadataReader in the ctor (`_reader`);
//      the port takes the port's MetadataFile (the row/blob reads all route
//      through it -- GetSignatureBlob / GetMemberReference /
//      GetTypeSpecSignatureBlob). The reader and the provider are
//      independent, exactly as in the C# (the minimal provider decodes a
//      foreign module's attribute rows through its reader-parameterized
//      GetTypeFromDefinition/GetTypeFromReference overloads).
//  (c) The `ref BlobReader` parameters port to the port's (base, size, pos)
//      cursor convention (the ReflectionDisassembler blob-helper precedent);
//      the "generic context" reader's `default(BlobReader)` (Length == 0) is
//      a null data pointer / zero size, faithful to the C#'s only test of it.
//  (d) EXCEPTIONS: the SRM BadImageFormatException arms port to
//      std::invalid_argument carrying the exact .NET messages (the
//      TypeProvider convention-(f) mapping): the parameterless
//      BadImageFormatException renders "Format of the executable (.exe) or
//      library (.dll) is invalid."; the SRM Throw helpers carry
//      "Invalid compressed integer." / "Invalid serialized string." /
//      "Read out of bounds."; the GetTypeFromHandle default arm carries the
//      System.SR.NotTypeDefOrRefHandle text. The EnumUnderlyingTypeResolveException
//      PROPAGATES unchanged (the C# does not wrap it -- the whole
//      minimal-provider enum-attribute decode throws it, gold-pinned over the
//      real mscorlib partition: 1117 of 20891 rows).
//  (e) The C# `object? Value` boxes (the CustomAttributeTypedArgument D385
//      convention): a null string (the SerString 0xFF form), a null Type
//      argument (the null serialized name), and a null ARRAY (count == -1)
//      all port to an EMPTY std::any; an empty string / empty array are
//      non-empty anys holding "" / the empty vector -- the distinctions are
//      observable through the decode and pinned by the gold.
//  (f) The SkipType walk reproduces the DECOMPILED SRM switch over the RAW
//      compressed element-type code exactly, including the shipped quirk
//      that a raw CLASS/VALUETYPE (17/18) is skipped as if it were a WRAPPER
//      whose operand is the following coded index parsed as a type (the
//      decompiled `case 17: case 18: SkipType(...)`) -- a generic
//      instantiation's marker+handle bytes are consumed through whatever
//      case the coded index's compressed value lands in, the shipped
//      behavior the port must reproduce byte-for-byte.

#pragma once

#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Forward declarations (the .cpp includes the full headers).
namespace ILSpy::Decompiler::Metadata { class MetadataFile; }
namespace ILSpy::Decompiler::TypeSystem { class TypeProvider; }

namespace ILSpy::Decompiler::Metadata {

// The BCL `System.Reflection.Metadata.SerializationTypeCode` (ECMA-335
// II.23.1.16): the type codes a serialized attribute value carries. The
// members carry the serialization bytes (each member's numeric value is its
// wire code); `Field` (0x53) / `Property` (0x54) live on the TypeSystem
// `CustomAttributeNamedArgumentKind` (the D385 split, unchanged here).
enum class SerializationTypeCode : std::uint8_t {
    Invalid = 0,
    Boolean = 2,
    Char = 3,
    SByte = 4,
    Byte = 5,
    Int16 = 6,
    UInt16 = 7,
    Int32 = 8,
    UInt32 = 9,
    Int64 = 10,
    UInt64 = 11,
    Single = 12,
    Double = 13,
    String = 14,
    SZArray = 29,
    Type = 80,
    TaggedObject = 81,
    Enum = 85,
};

// The BCL `System.Reflection.Metadata.CustomAttributeValue<TType>` absorbed
// over ITypePtr (the D385 convention): the decoded fixed (positional) and
// named arguments of one custom attribute.
struct CustomAttributeValue {
    std::vector<TypeSystem::CustomAttributeTypedArgument> FixedArguments;
    std::vector<TypeSystem::CustomAttributeNamedArgument> NamedArguments;
};

// The custom-attribute blob value decoder (see the header comment). One
// instance per (metadata, provider) pair; every member is const -- the C#
// decoder is a readonly struct.
class CustomAttributeDecoder {
public:
    // The C# `CustomAttributeDecoder(ICustomAttributeTypeProvider<TType>
    // provider, MetadataReader reader, bool provideBoxingTypeInfo = false)`.
    // The provider is a NON-CONST reference: the port's ISignatureTypeProvider
    // contract (GetPrimitiveType / GetSZArrayType) is non-const, mirroring the
    // C#'s non-readonly provider surface.
    CustomAttributeDecoder(const MetadataFile& metadata,
        TypeSystem::TypeProvider& provider,
        bool provideBoxingTypeInfo = false);

    // The SRM `CustomAttributeDecoder<TType>.DecodeValue(EntityHandle
    // constructor, BlobHandle value)` behind the public
    // `CustomAttribute.DecodeValue(provider)`: decodes the attribute VALUE
    // blob against the CONSTRUCTOR's signature blob (a MethodDef or MemberRef
    // ctor token; a MemberRef whose parent is a TypeSpec carries the generic
    // context of the instantiation). `valueData`/`valueSize` are the row's
    // value blob (the C# BlobHandle).
    //
    // Throws std::invalid_argument (the BadImageFormatException arms, the
    // exact .NET messages); propagates EnumUnderlyingTypeResolveException
    // from the provider's enum decode.
    CustomAttributeValue DecodeValue(std::uint32_t constructorToken,
        const std::uint8_t* valueData, std::size_t valueSize) const;

    // The repo copy's public entry (`DecodeNamedArguments(ref BlobReader
    // valueReader, int count)`): decodes `count` named arguments from the
    // value blob positioned at `pos` (the caller reads the named-arg count
    // itself -- the security-declaration shape). `pos` advances past the
    // decoded arguments.
    std::vector<TypeSystem::CustomAttributeNamedArgument> DecodeNamedArguments(
        const std::uint8_t* base, std::size_t size, std::size_t& pos,
        int count) const;

    // A positioned reader over a byte span (the C# `ref BlobReader`): data
    // null / size 0 is the C# `default(BlobReader)` (Length == 0). Public
    // because the blob-primitive helpers in the .cpp take it (the C++
    // anonymous-namespace / nested-type access rule).
    struct Reader {
        const std::uint8_t* data = nullptr;
        std::size_t size = 0;
        std::size_t pos = 0;
    };

private:
    // The C# `private struct ArgumentTypeInfo`.
    struct ArgumentTypeInfo {
        TypeSystem::ITypePtr Type;
        TypeSystem::ITypePtr ElementType;
        SerializationTypeCode TypeCode = SerializationTypeCode::Invalid;
        SerializationTypeCode ElementTypeCode = SerializationTypeCode::Invalid;
    };

    std::vector<TypeSystem::CustomAttributeTypedArgument> DecodeFixedArguments(
        Reader& signatureReader, Reader& valueReader, int count,
        Reader& genericContextReader) const;
    // The SRM private `DecodeNamedArguments(ref BlobReader)`: reads the
    // named-argument count itself.
    std::vector<TypeSystem::CustomAttributeNamedArgument> DecodeNamedArguments(
        Reader& valueReader) const;
    // The shared loop (the repo copy's body, count supplied by the caller).
    std::vector<TypeSystem::CustomAttributeNamedArgument> DecodeNamedArgumentsLoop(
        Reader& valueReader, int count) const;
    ArgumentTypeInfo DecodeFixedArgumentType(Reader& signatureReader,
        Reader& genericContextReader, bool isElementType) const;
    ArgumentTypeInfo DecodeNamedArgumentType(Reader& valueReader,
        bool isElementType) const;
    TypeSystem::CustomAttributeTypedArgument DecodeArgument(
        Reader& valueReader, const ArgumentTypeInfo& info) const;
    // The C# `ImmutableArray<...>? DecodeArrayArgument`: nullopt is the C#
    // null (the -1 count), an engaged empty vector the count-0 array.
    std::optional<std::vector<TypeSystem::CustomAttributeTypedArgument>>
    DecodeArrayArgument(Reader& valueReader, const ArgumentTypeInfo& info) const;
    TypeSystem::ITypePtr GetTypeFromHandle(std::uint32_t token) const;
    static void SkipType(Reader& reader);

    const MetadataFile& metadata_;
    TypeSystem::TypeProvider& provider_;
    bool provideBoxingTypeInfo_;
};

} // namespace ILSpy::Decompiler::Metadata
