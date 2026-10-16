// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE, NONINFRINGEMENT, OR AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

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
//  (a) THE C# GENERIC IS A CLASS TEMPLATE OVER THE PROVIDER: both C#
//      decoders are generic over TType (`CustomAttributeDecoder<TType>` over
//      an `ICustomAttributeTypeProvider<TType>`), with two instantiations in
//      the repo -- the ILSpy type system's `IType` (over the port's
//      TypeProvider) and the ReflectionDisassembler SecurityDeclarationDecoder's
//      `(PrimitiveTypeCode Code, string Name)` tuple. The port follows the
//      `Metadata/SignatureTypeProvider` precedent: `CustomAttributeDecoderT<TProvider>`
//      with `TType = typename TProvider::TType`, CTAD from the constructor's
//      provider argument, and the member bodies inline in the header (every
//      provider is a local of the driving function). The concrete
//      `CustomAttributeDecoder` (the old absorbed IType class) remains as a
//      thin derived class over `CustomAttributeDecoderT<TypeProvider>` so the
//      bare `CustomAttributeDecoder` spelling stays a plain class type (the
//      alias-of-specialization cannot replace it: a class-template alias has
//      no CTAD in C++17 and no parameter-type spelling without template
//      arguments). The `...T`-suffix-plus-alias shape carries the SRM generic
//      argument structs (`CustomAttributeTypedArgumentT` /
//      `CustomAttributeNamedArgumentT` / `CustomAttributeValueT`) with their
//      IType-instantiation aliases.
//  (b) The C# takes the decoding MetadataReader in the ctor (`_reader`);
//      the port takes the port's MetadataFile (the row/blob reads all route
//      through it -- GetSignatureBlob / GetMemberReference /
//      GetTypeSpecSignatureBlob). The reader and the provider are
//      independent, exactly as in the C# (the minimal provider decodes a
//      foreign module's attribute rows through its reader-parameterized
//      GetTypeFromDefinition/GetTypeFromReference overloads).
//  (c) The `ref BlobReader` parameters port to the `BlobReader` cursor
//      (below): the positioned (data, size, pos) triple with the C#
//      BlobReader method surface the decode paths call, the same shape the
//      port's other SRM gap fills use (the ReflectionDisassembler
//      blob-helper precedent); the "generic context" reader's
//      `default(BlobReader)` (Length == 0) is a null data pointer / zero
//      size, faithful to the C#'s only test of it. The SecurityDeclaration
//      paths (ReflectionDisassembler.TryDecodeSecurityDeclaration) drive the
//      same reader over the permission-set blob.
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
//  (e) The C# `object? Value` boxes (the CustomAttributeTypedArgumentT
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
//  (g) The provider contract the template drives (the C#
//      ICustomAttributeTypeProvider<TType>): GetPrimitiveType /
//      GetSystemType / GetSZArrayType / GetTypeFromDefinition /
//      GetTypeFromReference / GetTypeFromSerializedName /
//      GetUnderlyingEnumType / IsSystemType. The TType-bearing members
//      receive the TType BY VALUE (the C# passes the value tuple / IType
//      reference); the TypeProvider supplies the ITypePtr-taking
//      GetUnderlyingEnumType/IsSystemType overloads for its instantiation.

#pragma once

#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

// The SRM exception texts (the convention-(d) messages), shared by the
// BlobReader methods and the decoder bodies.
inline constexpr const char* kBadImageFormat =
    "Format of the executable (.exe) or library (.dll) is invalid.";
inline constexpr const char* kInvalidSerializedString = "Invalid serialized string.";
inline constexpr const char* kInvalidCompressedInteger = "Invalid compressed integer.";
inline constexpr const char* kReadOutOfBounds = "Read out of bounds.";
inline constexpr const char* kNotTypeDefOrRef =
    "Specified handle is not a TypeDefinitionHandle or TypeReferenceHandle.";

[[noreturn]] inline void ThrowBadImageFormat() {
    throw std::invalid_argument(kBadImageFormat);
}

// The C# `System.Reflection.Metadata.BlobReader` surface the custom-
// attribute / security-declaration decode paths drive (the SRM gap fill):
// the positioned cursor (data, size, pos) with the reading members the
// decoders call -- a null `data` / zero `size` is the C#
// `default(BlobReader)` (Length == 0). Distinct from the file-local
// SignatureDecoder cursor (that one latches a `failed` flag instead of
// throwing); this one throws the SRM bounds/serialization exceptions
// faithfully.
struct BlobReader {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;

    // The C# `BlobReader.ReadUInt16()` (little-endian).
    std::uint16_t ReadUInt16();
    // The C# `BlobReader.ReadByte()`.
    std::uint8_t ReadByte();
    // The C# `BlobReader.ReadCompressedIntegerOrInvalid()`.
    int ReadCompressedIntegerOrInvalid(int& bytesRead);
    // The C# `BlobReader.ReadCompressedInteger()` -- the throwing form.
    int ReadCompressedInteger();
    // The C# `BlobReader.ReadSerializationTypeCode()`.
    int ReadSerializationTypeCode();
    // The C# `BlobReader.ReadSignatureTypeCode()`.
    int ReadSignatureTypeCode();
    // The C# `BlobReader.ReadTypeHandle()` -- the TypeDefOrRefOrSpec coded
    // index as the raw token (0 = the nil handle).
    std::uint32_t ReadTypeHandle();
    // The C# `BlobReader.ReadSerializedString()` -- the SerString.
    std::optional<std::string> ReadSerializedString();
};

// The BCL `System.Reflection.Metadata.CustomAttributeValue<TType>`: the
// decoded fixed (positional) and named arguments of one custom attribute.
template <typename TType>
struct CustomAttributeValueT {
    std::vector<TypeSystem::CustomAttributeTypedArgumentT<TType>> FixedArguments;
    std::vector<TypeSystem::CustomAttributeNamedArgumentT<TType>> NamedArguments;
};

// The IType instantiation (convention (a)).
using CustomAttributeValue = CustomAttributeValueT<TypeSystem::ITypePtr>;

// The custom-attribute blob value decoder (see the header comment): the C#
// `CustomAttributeDecoder<TType>` over an `ICustomAttributeTypeProvider<TType>`.
// One instance per (metadata, provider) pair; every member is const -- the C#
// decoder is a readonly struct. Header-only (convention (a)): every provider
// is a local of the driving function.
template <typename TProvider>
class CustomAttributeDecoderT {
public:
    using TType = typename TProvider::TType;
    using TypedArgument = TypeSystem::CustomAttributeTypedArgumentT<TType>;
    using NamedArgument = TypeSystem::CustomAttributeNamedArgumentT<TType>;
    using Value = CustomAttributeValueT<TType>;

    // The C# `CustomAttributeDecoder(ICustomAttributeTypeProvider<TType>
    // provider, MetadataReader reader, bool provideBoxingTypeInfo = false)`.
    // The provider is a NON-CONST reference: the port's ISignatureTypeProvider
    // contract (GetPrimitiveType / GetSZArrayType) is non-const, mirroring the
    // C#'s non-readonly provider surface.
    CustomAttributeDecoderT(const MetadataFile& metadata, TProvider& provider,
        bool provideBoxingTypeInfo = false)
        : metadata_(metadata),
          provider_(provider),
          provideBoxingTypeInfo_(provideBoxingTypeInfo) {}

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
    Value DecodeValue(std::uint32_t constructorToken,
        const std::uint8_t* valueData, std::size_t valueSize) const {
        // The C# ctor-signature dispatch: a MethodDef ctor's Signature column, a
        // MemberRef ctor's Signature column plus (when the parent is a TypeSpec)
        // the instantiation blob that scopes the ctor's VAR references.
        std::optional<std::vector<std::uint8_t>> signature;
        std::optional<std::vector<std::uint8_t>> genericContextBlob;
        const std::uint8_t table = static_cast<std::uint8_t>(constructorToken >> 24);
        if (table == 0x06) {  // HandleKind.MethodDefinition
            signature = metadata_.GetSignatureBlob(constructorToken);
            if (!signature)
                ThrowBadImageFormat();
        } else if (table == 0x0A) {  // HandleKind.MemberReference
            auto memberReference = metadata_.GetMemberReference(constructorToken);
            if (!memberReference)
                ThrowBadImageFormat();
            signature = metadata_.GetSignatureBlob(constructorToken);
            if (!signature)
                ThrowBadImageFormat();
            if (memberReference->ParentToken != 0
                && (memberReference->ParentToken >> 24) == 0x1B) {
                genericContextBlob =
                    metadata_.GetTypeSpecSignatureBlob(memberReference->ParentToken);
                if (!genericContextBlob)
                    ThrowBadImageFormat();
            }
        } else {
            ThrowBadImageFormat();
        }

        BlobReader valueReader{valueData, valueSize, 0};
        if (valueReader.ReadUInt16() != 0x0001)
            ThrowBadImageFormat();

        BlobReader signatureReader{signature->data(), signature->size(), 0};
        // The C# `ReadSignatureHeader()` + the Kind/IsGeneric checks
        // (SignatureKind.Method is the 0 convention byte).
        std::uint8_t headerByte = signatureReader.ReadByte();
        if ((headerByte & 0x0F) != 0x00 || (headerByte & 0x10) != 0)
            ThrowBadImageFormat();
        int count = signatureReader.ReadCompressedInteger();
        if (signatureReader.ReadSignatureTypeCode() != 0x01)  // Void
            ThrowBadImageFormat();

        BlobReader genericContext{};
        if (genericContextBlob) {
            genericContext = BlobReader{genericContextBlob->data(),
                genericContextBlob->size(), 0};
            if (genericContext.ReadSignatureTypeCode() == 21) {  // GenericTypeInstance
                // The CLASS (18) / VALUETYPE (17) marker, read as a RAW
                // compressed integer (not through the TypeHandle mapping).
                int num = genericContext.ReadCompressedInteger();
                if (num != 18 && num != 17)
                    ThrowBadImageFormat();
                genericContext.ReadTypeHandle();
            } else {
                genericContext = BlobReader{};  // the C# `default(BlobReader)`
            }
        }

        auto fixedArguments = DecodeFixedArguments(
            signatureReader, valueReader, count, genericContext);
        auto namedArguments = DecodeNamedArguments(valueReader);
        return Value{std::move(fixedArguments), std::move(namedArguments)};
    }

    // The repo copy's public entry (`DecodeNamedArguments(ref BlobReader
    // valueReader, int count)`): decodes `count` named arguments from the
    // value blob positioned at `pos` (the caller reads the named-arg count
    // itself -- the security-declaration shape). `pos` advances past the
    // decoded arguments.
    std::vector<NamedArgument> DecodeNamedArguments(const std::uint8_t* base,
        std::size_t size, std::size_t& pos, int count) const {
        BlobReader valueReader{base, size, pos};
        auto result = DecodeNamedArgumentsLoop(valueReader, count);
        pos = valueReader.pos;
        return result;
    }

private:
    // The C# `private struct ArgumentTypeInfo`.
    struct ArgumentTypeInfo {
        TType Type{};
        TType ElementType{};
        SerializationTypeCode TypeCode = SerializationTypeCode::Invalid;
        SerializationTypeCode ElementTypeCode = SerializationTypeCode::Invalid;
    };

    std::vector<TypedArgument> DecodeFixedArguments(BlobReader& signatureReader,
        BlobReader& valueReader, int count, BlobReader& genericContextReader) const {
        if (count == 0)
            return {};
        std::vector<TypedArgument> arguments;
        for (int i = 0; i < count; i++) {
            ArgumentTypeInfo info = DecodeFixedArgumentType(signatureReader,
                genericContextReader, false);
            arguments.push_back(DecodeArgument(valueReader, info));
        }
        return arguments;
    }

    // The SRM private `DecodeNamedArguments(ref BlobReader)`: reads the
    // named-argument count itself (a UInt16).
    std::vector<NamedArgument> DecodeNamedArguments(
        BlobReader& valueReader) const {
        int num = valueReader.ReadUInt16();
        if (num == 0)
            return {};
        return DecodeNamedArgumentsLoop(valueReader, num);
    }

    // The shared loop (the repo copy's body, count supplied by the caller).
    std::vector<NamedArgument> DecodeNamedArgumentsLoop(BlobReader& valueReader,
        int count) const {
        std::vector<NamedArgument> arguments;
        for (int i = 0; i < count; i++) {
            int kindByte = valueReader.ReadSerializationTypeCode();
            auto kind = static_cast<TypeSystem::CustomAttributeNamedArgumentKind>(
                kindByte);
            if (kind != TypeSystem::CustomAttributeNamedArgumentKind::Field
                && kind != TypeSystem::CustomAttributeNamedArgumentKind::Property) {
                ThrowBadImageFormat();
            }
            ArgumentTypeInfo info = DecodeNamedArgumentType(valueReader, false);
            std::optional<std::string> name = valueReader.ReadSerializedString();
            TypedArgument argument = DecodeArgument(valueReader, info);
            arguments.emplace_back(name.value_or(""), kind, argument.Type(),
                argument.Value());
        }
        return arguments;
    }

    ArgumentTypeInfo DecodeFixedArgumentType(BlobReader& signatureReader,
        BlobReader& genericContextReader, bool isElementType) const {
        int code = signatureReader.ReadSignatureTypeCode();
        ArgumentTypeInfo result;
        result.TypeCode = static_cast<SerializationTypeCode>(code);
        switch (code) {
            case 2:   // Boolean
            case 3:   // Char
            case 4:   // SByte
            case 5:   // Byte
            case 6:   // Int16
            case 7:   // UInt16
            case 8:   // Int32
            case 9:   // UInt32
            case 10:  // Int64
            case 11:  // UInt64
            case 12:  // Single
            case 13:  // Double
            case 14:  // String
                result.Type =
                    provider_.GetPrimitiveType(static_cast<PrimitiveTypeCode>(code));
                break;
            case 28: {  // Object
                result.TypeCode = SerializationTypeCode::TaggedObject;
                result.Type = provider_.GetPrimitiveType(PrimitiveTypeCode::Object);
                break;
            }
            case 64: {  // TypeHandle (the raw CLASS/VALUETYPE element)
                std::uint32_t handle = signatureReader.ReadTypeHandle();
                result.Type = GetTypeFromHandle(handle);
                result.TypeCode = provider_.IsSystemType(result.Type)
                    ? SerializationTypeCode::Type
                    : static_cast<SerializationTypeCode>(
                        provider_.GetUnderlyingEnumType(result.Type));
                break;
            }
            case 29: {  // SZArray
                if (isElementType) {
                    // jagged arrays are not allowed.
                    ThrowBadImageFormat();
                }
                ArgumentTypeInfo elementInfo =
                    DecodeFixedArgumentType(signatureReader, genericContextReader,
                        true);
                result.ElementType = elementInfo.Type;
                result.ElementTypeCode = elementInfo.TypeCode;
                result.Type = provider_.GetSZArrayType(result.ElementType);
                break;
            }
            case 19: {  // GenericTypeParameter (VAR)
                if (genericContextReader.size == 0) {
                    ThrowBadImageFormat();
                }
                int num = signatureReader.ReadCompressedInteger();
                int num2 = genericContextReader.ReadCompressedInteger();
                if (num >= num2) {
                    ThrowBadImageFormat();
                }
                while (num > 0) {
                    SkipType(genericContextReader);
                    num--;
                }
                // A fresh empty generic context: a nested generic-parameter
                // decode throws BadImageFormatException rather than re-reading
                // the outer context (the C# passes a default BlobReader here).
                BlobReader emptyContext;
                return DecodeFixedArgumentType(genericContextReader, emptyContext,
                    true);
            }
            default:
                ThrowBadImageFormat();
        }
        return result;
    }

    ArgumentTypeInfo DecodeNamedArgumentType(BlobReader& valueReader,
        bool isElementType) const {
        ArgumentTypeInfo result;
        int code = valueReader.ReadSerializationTypeCode();
        result.TypeCode = static_cast<SerializationTypeCode>(code);
        switch (code) {
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
                result.Type =
                    provider_.GetPrimitiveType(static_cast<PrimitiveTypeCode>(code));
                break;
            case 80:  // Type
                result.Type = provider_.GetSystemType();
                break;
            case 81:  // TaggedObject
                result.Type = provider_.GetPrimitiveType(PrimitiveTypeCode::Object);
                break;
            case 29: {  // SZArray
                if (isElementType) {
                    // jagged arrays are not allowed.
                    ThrowBadImageFormat();
                }
                ArgumentTypeInfo elementInfo =
                    DecodeNamedArgumentType(valueReader, true);
                result.ElementType = elementInfo.Type;
                result.ElementTypeCode = elementInfo.TypeCode;
                result.Type = provider_.GetSZArrayType(result.ElementType);
                break;
            }
            case 85: {  // Enum
                std::optional<std::string> typeName =
                    valueReader.ReadSerializedString();
                if (!typeName) {
                    // The C# GetTypeFromSerializedName(null) returns null, and
                    // GetUnderlyingEnumType(null) NREs at the extension call --
                    // the faithful message (the BamlNode NRE convention).
                    throw std::runtime_error(
                        "Object reference not set to an instance of an object.");
                }
                result.Type = provider_.GetTypeFromSerializedName(*typeName);
                result.TypeCode = static_cast<SerializationTypeCode>(
                    provider_.GetUnderlyingEnumType(result.Type));
                break;
            }
            default:
                ThrowBadImageFormat();
        }
        return result;
    }

    TypedArgument DecodeArgument(BlobReader& valueReader,
        const ArgumentTypeInfo& info) const {
        ArgumentTypeInfo outer = info;
        ArgumentTypeInfo decoded = info;
        if (decoded.TypeCode == SerializationTypeCode::TaggedObject) {
            decoded = DecodeNamedArgumentType(valueReader, false);
        }
        std::any value;
        switch (decoded.TypeCode) {
            case SerializationTypeCode::Boolean:
                value = valueReader.ReadByte() != 0;
                break;
            case SerializationTypeCode::Byte:
                value = valueReader.ReadByte();
                break;
            case SerializationTypeCode::Char: {
                std::uint16_t raw = valueReader.ReadUInt16();
                value = static_cast<char16_t>(raw);
                break;
            }
            case SerializationTypeCode::Double: {
                if (valueReader.size - valueReader.pos < 8)
                    throw std::invalid_argument(kReadOutOfBounds);
                std::uint64_t bits = 0;
                for (int i = 0; i < 8; i++)
                    bits |= static_cast<std::uint64_t>(
                        valueReader.data[valueReader.pos + i]) << (8 * i);
                valueReader.pos += 8;
                double d;
                std::memcpy(&d, &bits, sizeof(d));
                value = d;
                break;
            }
            case SerializationTypeCode::Int16: {
                std::uint16_t raw = valueReader.ReadUInt16();
                value = static_cast<std::int16_t>(raw);
                break;
            }
            case SerializationTypeCode::Int32: {
                if (valueReader.size - valueReader.pos < 4)
                    throw std::invalid_argument(kReadOutOfBounds);
                std::uint32_t raw = 0;
                for (int i = 0; i < 4; i++)
                    raw |= static_cast<std::uint32_t>(
                        valueReader.data[valueReader.pos + i]) << (8 * i);
                valueReader.pos += 4;
                value = static_cast<std::int32_t>(raw);
                break;
            }
            case SerializationTypeCode::Int64: {
                if (valueReader.size - valueReader.pos < 8)
                    throw std::invalid_argument(kReadOutOfBounds);
                std::uint64_t bits = 0;
                for (int i = 0; i < 8; i++)
                    bits |= static_cast<std::uint64_t>(
                        valueReader.data[valueReader.pos + i]) << (8 * i);
                valueReader.pos += 8;
                value = static_cast<std::int64_t>(bits);
                break;
            }
            case SerializationTypeCode::SByte:
                value = static_cast<std::int8_t>(valueReader.ReadByte());
                break;
            case SerializationTypeCode::Single: {
                if (valueReader.size - valueReader.pos < 4)
                    throw std::invalid_argument(kReadOutOfBounds);
                std::uint32_t bits = 0;
                for (int i = 0; i < 4; i++)
                    bits |= static_cast<std::uint32_t>(
                        valueReader.data[valueReader.pos + i]) << (8 * i);
                valueReader.pos += 4;
                float f;
                std::memcpy(&f, &bits, sizeof(f));
                value = f;
                break;
            }
            case SerializationTypeCode::UInt16:
                value = valueReader.ReadUInt16();
                break;
            case SerializationTypeCode::UInt32: {
                if (valueReader.size - valueReader.pos < 4)
                    throw std::invalid_argument(kReadOutOfBounds);
                std::uint32_t raw = 0;
                for (int i = 0; i < 4; i++)
                    raw |= static_cast<std::uint32_t>(
                        valueReader.data[valueReader.pos + i]) << (8 * i);
                valueReader.pos += 4;
                value = raw;
                break;
            }
            case SerializationTypeCode::UInt64: {
                if (valueReader.size - valueReader.pos < 8)
                    throw std::invalid_argument(kReadOutOfBounds);
                std::uint64_t bits = 0;
                for (int i = 0; i < 8; i++)
                    bits |= static_cast<std::uint64_t>(
                        valueReader.data[valueReader.pos + i]) << (8 * i);
                valueReader.pos += 8;
                value = bits;
                break;
            }
            case SerializationTypeCode::String: {
                // A null SerString boxes as a null object (the empty std::any).
                auto s = valueReader.ReadSerializedString();
                if (s)
                    value = std::move(*s);
                break;
            }
            case SerializationTypeCode::Type: {
                std::optional<std::string> name =
                    valueReader.ReadSerializedString();
                // The C# `GetTypeFromSerializedName(name)` returns null for a
                // null name -- the null ITypePtr, no provider call.
                if (name)
                    value = provider_.GetTypeFromSerializedName(*name);
                break;
            }
            case SerializationTypeCode::SZArray: {
                auto array = DecodeArrayArgument(valueReader, decoded);
                if (array)
                    value = std::move(*array);
                break;
            }
            default:
                ThrowBadImageFormat();
        }
        if (provideBoxingTypeInfo_
            && outer.TypeCode == SerializationTypeCode::TaggedObject) {
            return TypedArgument(outer.Type,
                TypedArgument(decoded.Type, value));
        }
        return TypedArgument(decoded.Type, value);
    }

    // The C# `ImmutableArray<...>? DecodeArrayArgument`: nullopt is the C#
    // null (the -1 count), an engaged empty vector the count-0 array.
    std::optional<std::vector<TypedArgument>> DecodeArrayArgument(
        BlobReader& valueReader, const ArgumentTypeInfo& info) const {
        if (valueReader.size - valueReader.pos < 4)
            throw std::invalid_argument(kReadOutOfBounds);
        std::uint32_t raw = 0;
        for (int i = 0; i < 4; i++)
            raw |= static_cast<std::uint32_t>(
                valueReader.data[valueReader.pos + i]) << (8 * i);
        valueReader.pos += 4;
        std::int32_t count = static_cast<std::int32_t>(raw);
        if (count == -1) {
            return std::nullopt;
        }
        if (count == 0) {
            return std::vector<TypedArgument>{};
        }
        if (count < 0) {
            ThrowBadImageFormat();
        }
        ArgumentTypeInfo elementInfo;
        elementInfo.Type = info.ElementType;
        elementInfo.TypeCode = info.ElementTypeCode;
        std::vector<TypedArgument> array;
        for (int i = 0; i < count; i++) {
            array.push_back(DecodeArgument(valueReader, elementInfo));
        }
        return array;
    }

    TType GetTypeFromHandle(std::uint32_t token) const {
        const std::uint8_t table = static_cast<std::uint8_t>(token >> 24);
        if (table == 0x02) {  // TypeDefinition
            return provider_.GetTypeFromDefinition(metadata_, token, 0);
        }
        if (table == 0x01) {  // TypeReference
            return provider_.GetTypeFromReference(metadata_, token, 0);
        }
        throw std::invalid_argument(kNotTypeDefOrRef);
    }

    // The C# `SkipType` -- the decompiled SRM switch over the RAW compressed
    // element-type code, reproduced exactly (the header's convention (f)): the
    // no-operand codes; the wrapper codes (Pointer/ByReference/SZArray/Sentinel/
    // Pinned) recursing; FunctionPointer's header/count/return/parameters walk;
    // Array's element + shape; the CModReqd/CModOpt handle + wrapped type;
    // GenericTypeInstance's marker-skip + count + arguments; VAR's compressed
    // index; and the CLASS/VALUETYPE (17/18) quirk -- skipped AS IF the coded
    // index that follows were a nested type.
    static void SkipType(BlobReader& reader) {
        switch (reader.ReadCompressedInteger()) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 22:
            case 24:
            case 25:
            case 28:
                break;
            case 15:
            case 16:
            case 29:
            case 69:
                SkipType(reader);
                break;
            case 27: {
                // FunctionPointer: an optional generic count, the parameter
                // count, the return type, then each parameter type.
                std::uint8_t header = reader.ReadByte();
                if ((header & 0x10) != 0) {
                    reader.ReadCompressedInteger();
                }
                int num2 = reader.ReadCompressedInteger();
                SkipType(reader);
                for (int j = 0; j < num2; j++) {
                    SkipType(reader);
                }
                break;
            }
            case 20: {
                // Array: the element type, the rank, the sizes, the lower bounds.
                SkipType(reader);
                reader.ReadCompressedInteger();
                int num3 = reader.ReadCompressedInteger();
                for (int k = 0; k < num3; k++) {
                    reader.ReadCompressedInteger();
                }
                int num4 = reader.ReadCompressedInteger();
                for (int l = 0; l < num4; l++) {
                    // ReadCompressedSignedInteger: the compressed payload with
                    // the II.23.2.2 sign bits rotated off (only the skip
                    // matters).
                    int bytesRead;
                    int v = reader.ReadCompressedIntegerOrInvalid(bytesRead);
                    if (v == 0x7FFFFFFF)
                        throw std::invalid_argument(kInvalidCompressedInteger);
                    bool sign = (v & 1) != 0;
                    v >>= 1;
                    if (sign) {
                        switch (bytesRead) {
                            case 1:
                                v |= -64;
                                break;
                            case 2:
                                v |= -8192;
                                break;
                            default:
                                v |= -268435456;
                                break;
                        }
                    }
                }
                break;
            }
            case 31:
            case 32:
                reader.ReadTypeHandle();
                SkipType(reader);
                break;
            case 21: {
                // GenericTypeInstance: the CLASS/VALUETYPE marker is skipped as
                // a type (the 17/18 quirk consumes the marker's coded index as
                // the "nested type"), then the argument count and arguments.
                SkipType(reader);
                int num = reader.ReadCompressedInteger();
                for (int i = 0; i < num; i++) {
                    SkipType(reader);
                }
                break;
            }
            case 19:
                reader.ReadCompressedInteger();
                break;
            case 17:
            case 18:
                SkipType(reader);
                break;
            default:
                ThrowBadImageFormat();
        }
    }

    const MetadataFile& metadata_;
    TProvider& provider_;
    bool provideBoxingTypeInfo_;
};

// The IType instantiation (convention (a)): the concrete class the
// TypeSystem-driving consumers use. A derived class (not an alias) so the
// bare `CustomAttributeDecoder` spelling stays a plain class type with a
// regular constructor -- the existing users spell it as a parameter type
// and construct it through CTAD-free call sites.
class CustomAttributeDecoder
    : public CustomAttributeDecoderT<TypeSystem::TypeProvider> {
public:
    CustomAttributeDecoder(const MetadataFile& metadata,
        TypeSystem::TypeProvider& provider,
        bool provideBoxingTypeInfo = false)
        : CustomAttributeDecoderT<TypeSystem::TypeProvider>(
              metadata, provider, provideBoxingTypeInfo) {}
};

} // namespace ILSpy::Decompiler::Metadata
